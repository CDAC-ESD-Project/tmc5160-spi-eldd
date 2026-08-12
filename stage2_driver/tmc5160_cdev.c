/*
 * tmc5160_cdev.c
 *
 * Character device interface layer for the TMC5160 driver.
 * Owns:
 *   - file_operations
 *   - ioctl dispatch
 *   - read/write interface
 *   - sysfs attributes
 *
 * This file must never access GPIOs or SPI directly.
 */


#include "tmc5160.h"

#define TMC5160_WRITE_TIMEOUT_MS   30000U

/* Private helper functions                                           */

static u32 tmc5160_decode_fault_flags(u32 drv_status, u32 gstat)
{
    u32 flags;

    flags = drv_status &
            (TMC5160_DRV_STATUS_OT   |
             TMC5160_DRV_STATUS_OTPW |
             TMC5160_DRV_STATUS_S2GA |
             TMC5160_DRV_STATUS_S2GB |
             TMC5160_DRV_STATUS_OLA  |
             TMC5160_DRV_STATUS_OLB);

    flags |= (gstat & TMC5160_GSTAT_CLEAR_ALL) << 8;

    return flags;
}

static inline u8 tmc5160_cs_actual(u32 drv_status)
{
    return (drv_status &
            TMC5160_DRV_STATUS_CS_ACTUAL_MASK)
            >> TMC5160_DRV_STATUS_CS_ACTUAL_SHIFT;
}

static int tmc5160_refresh_faults(struct tmc5160_dev *dev, u32 *drv_out)
{
    u32 drv;
    u32 gstat;
    int ret;

    ret = tmc5160_read_faults(dev,&drv,&gstat);
    if (ret)
        return ret;

    dev->fault_flags =
        tmc5160_decode_fault_flags(drv, gstat);

    if (drv & TMC5160_DRV_STATUS_STALLGUARD)
        atomic_set(&dev->stall_pending, 1);

    if (drv_out)
        *drv_out = drv;

    return 0;
}

/* Angle / step conversion helpers                                    */

static u32 tmc5160_steps_from_mdeg(struct tmc5160_dev *dev,s32 angle_mdeg)
{
    u32 usteps;
    u64 steps_per_rev;
    u64 magnitude;


    steps_per_rev =
        (u64)TMC5160_FULLSTEPS_PER_REV * usteps;

    magnitude = (u64)abs(angle_mdeg);

    return (u32)div_u64(magnitude * steps_per_rev,360000);
}

static s32 tmc5160_mdeg_from_steps(struct tmc5160_dev *dev,s64 steps)
{
    u32 usteps;
    s64 steps_per_rev;

    usteps = dev->current_usteps ?mdev->current_usteps :TMC5160_DEFAULT_USTEPS;

    steps_per_rev =
        (s64)TMC5160_FULLSTEPS_PER_REV * usteps;

    if (!steps_per_rev)
        return 0;

    return (s32)div_s64(steps * 360000,steps_per_rev);
}

/* file_operations: open / release                                    */

static int tmc5160_open(struct inode *inode,struct file *file)
{
    struct tmc5160_dev *dev;

    dev = container_of(inode->i_cdev,struct tmc5160_dev,cdev);

    if (atomic_inc_return(&dev->open_count) != 1) {
        atomic_dec(&dev->open_count);
        return -EBUSY;
    }

    file->private_data = dev;

    return 0;
}

static int tmc5160_release(struct inode *inode,struct file *file)
{
    struct tmc5160_dev *dev = file->private_data;

    atomic_dec(&dev->open_count);

    if (tmc5160_is_moving(dev))
        tmc5160_stop(dev);

    return 0;
}

/* file_operations: write / read                                      */

static ssize_t tmc5160_write(struct file *file,const char __user *buf,size_t count,loff_t *ppos)
{
    struct tmc5160_dev *dev = file->private_data;
    struct tmc5160_move_cmd cmd;

    s64 current_steps;
    s32 target_mdeg;

    u32 steps;
    int direction;
    int ret;

    if (count != sizeof(cmd))
        return -EINVAL;

    if (copy_from_user(&cmd,buf,sizeof(cmd)))
        return -EFAULT;

    if (cmd.flags & ~TMC_MOVE_FLAG_ABSOLUTE)
        return -EINVAL;

    current_steps = tmc5160_get_position(dev);

    if (cmd.flags & TMC_MOVE_FLAG_ABSOLUTE) {
        target_mdeg = cmd.angle_mdeg;
    } else {
        target_mdeg =
            tmc5160_mdeg_from_steps(dev, current_steps) +
            cmd.angle_mdeg;
    }

    if (dev->limits_set) {
        if (target_mdeg < dev->limit_min_mdeg ||
            target_mdeg > dev->limit_max_mdeg)
            return -ERANGE;
    }

    if (cmd.flags & TMC_MOVE_FLAG_ABSOLUTE) {

        s32 delta_mdeg =
            target_mdeg -
            tmc5160_mdeg_from_steps(dev, current_steps);

        direction =
            (delta_mdeg >= 0) ?
            TMC_DIR_FORWARD :
            TMC_DIR_REVERSE;

        steps = tmc5160_steps_from_mdeg(dev,delta_mdeg);
    }
    else {

        direction =
            (cmd.angle_mdeg >= 0) ?
            TMC_DIR_FORWARD :
            TMC_DIR_REVERSE;

        steps = tmc5160_steps_from_mdeg(dev,cmd.angle_mdeg);
    }

    mutex_lock(&dev->lock);

    ret = tmc5160_move(dev,steps,direction);

    mutex_unlock(&dev->lock);

    if (ret)
        return ret;

    ret = tmc5160_wait_move(dev,TMC5160_WRITE_TIMEOUT_MS);

    if (ret)
        return ret;

    return count;
}

static ssize_t tmc5160_read(struct file *file,char __user *buf,size_t count,loff_t *ppos)
{
    struct tmc5160_dev *dev = file->private_data;
    struct tmc5160_status status;

    u32 drv = 0;
    int ret;

    if (count != sizeof(status))
        return -EINVAL;

    mutex_lock(&dev->lock);

    ret = tmc5160_refresh_faults(dev,&drv);

    if (ret) {
        mutex_unlock(&dev->lock);
        return ret;
    }

    memset(&status,
           0,
           sizeof(status));

    status.position_mdeg =
        tmc5160_mdeg_from_steps(
            dev,
            tmc5160_get_position(dev));

    status.velocity_interval =
        dev->interval_run_ns;

    status.fault_flags =
        dev->fault_flags;

    status.in_motion =
        tmc5160_is_moving(dev) ? 1 : 0;

    status.stalled =
        atomic_xchg(&dev->stall_pending,
                    0) ? 1 : 0;

    status.cs_actual =
        tmc5160_cs_actual(drv);

    mutex_unlock(&dev->lock);

    if (copy_to_user(buf,&status,sizeof(status)))
        return -EFAULT;

    return count;
}

/* file_operations: ioctl                                             */

static long tmc5160_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct tmc5160_dev *dev = file->private_data;
    void __user *uarg = (void __user *)arg;

    int ret = 0;

    if (_IOC_TYPE(cmd) != TMC_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {

    /* Motion parameters                                               */

    case TMC_SET_VMAX: {

        u32 interval_ns;

        if (copy_from_user(&interval_ns,uarg,sizeof(interval_ns)))
            return -EFAULT;

        ret = tmc5160_set_speed(dev,interval_ns);

        if (ret)
            return ret;

        break;
    }

    case TMC_SET_AMAX: {

        struct tmc5160_accel_cmd accel;

        if (copy_from_user(&accel,uarg,sizeof(accel)))
            return -EFAULT;

        ret = tmc5160_set_accel(dev,accel.ramp_steps,accel.start_interval_ns);

        if (ret)
            return ret;

        break;
    }

    /* Current configuration                                           */

    case TMC_SET_IRUN: {

        u8 irun;

        if (copy_from_user(&irun,uarg,sizeof(irun)))
            return -EFAULT;

        mutex_lock(&dev->lock);

        ret = tmc5160_set_current(dev,irun,dev->current_ihold);

        mutex_unlock(&dev->lock);

        if (ret)
            return ret;

        break;
    }

    case TMC_SET_IHOLD: {

        u8 ihold;

        if (copy_from_user(&ihold,uarg,sizeof(ihold)))
            return -EFAULT;

        mutex_lock(&dev->lock);

        ret = tmc5160_set_current(dev,dev->current_irun,ihold);

        mutex_unlock(&dev->lock);

        if (ret)
            return ret;

        break;
    }

    /* Microstepping                                                   */

    case TMC_SET_MICROSTEP: {

        u32 usteps;

        if (copy_from_user(&usteps,uarg,sizeof(usteps)))
            return -EFAULT;

        if (tmc5160_is_moving(dev))
            return -EBUSY;

        mutex_lock(&dev->lock);

        ret = tmc5160_set_microstep(dev,usteps);

        mutex_unlock(&dev->lock);

        if (ret)
            return ret;

        break;
    }

    /* Status                                                          */

    case TMC_GET_STATUS: {

        struct tmc5160_status status;
        u32 drv = 0;

        mutex_lock(&dev->lock);

        ret = tmc5160_refresh_faults(dev,&drv);

        if (!ret) {

            memset(&status,0,sizeof(status));

            status.position_mdeg =
                tmc5160_mdeg_from_steps(
                    dev,
                    tmc5160_get_position(dev));

            status.velocity_interval =
                dev->interval_run_ns;

            status.fault_flags =
                dev->fault_flags;

            status.in_motion =
                tmc5160_is_moving(dev) ? 1 : 0;

            status.stalled =
                atomic_xchg(&dev->stall_pending,
                            0) ? 1 : 0;

            status.cs_actual =
                tmc5160_cs_actual(drv);
        }

        mutex_unlock(&dev->lock);

        if (ret)
            return ret;

        if (copy_to_user(uarg,&status,sizeof(status)))
            return -EFAULT;

        break;
    }

    /* Motion control                                                  */

    case TMC_STOP:

        tmc5160_stop(dev);
        break;

    case TMC_ENABLE:

        if (tmc5160_is_moving(dev))
            return -EBUSY;

        mutex_lock(&dev->lock);

        tmc5160_set_enable(dev,1);

        mutex_unlock(&dev->lock);

        break;

    case TMC_DISABLE:

        if (tmc5160_is_moving(dev))
            tmc5160_stop(dev);

        mutex_lock(&dev->lock);

        tmc5160_set_enable(dev,0);

        mutex_unlock(&dev->lock);

        break;

    case TMC_SET_HOME:

        atomic64_set(&dev->position_steps,0);

        break;

    /* Software limits                                                 */

    case TMC_SET_SOFT_LIMITS: {

        struct tmc5160_limits limits;

        if (copy_from_user(&limits,uarg,sizeof(limits)))
            return -EFAULT;

        if (limits.max_mdeg <= limits.min_mdeg)
            return -EINVAL;

        mutex_lock(&dev->lock);

        dev->limit_min_mdeg =
            limits.min_mdeg;

        dev->limit_max_mdeg =
            limits.max_mdeg;

        dev->limits_set = true;

        mutex_unlock(&dev->lock);

        break;
    }

    default:
        return -ENOTTY;
    }

    return ret;
}

/* file_operations: poll                                              */

static __poll_t tmc5160_poll(struct file *file,poll_table *wait)
{
    struct tmc5160_dev *dev = file->private_data;

    poll_wait(file,&dev->poll_wait_queue,wait);

    if (!tmc5160_is_moving(dev))
        return EPOLLIN | EPOLLRDNORM;

    return 0;
}

/* file_operations table                                              */

static const struct file_operations tmc5160_fops = {
    .owner          = THIS_MODULE,
    .open           = tmc5160_open,
    .release        = tmc5160_release,
    .write          = tmc5160_write,
    .read           = tmc5160_read,
    .unlocked_ioctl = tmc5160_ioctl,
    .poll           = tmc5160_poll,
    .llseek         = no_llseek,
};

/* Sysfs: position                                                    */

static ssize_t position_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    return sysfs_emit(buf,"%d\n",tmc5160_mdeg_from_steps(dev,tmc5160_get_position(dev)));
}

static DEVICE_ATTR_RO(position);

/* Sysfs: velocity_max                                                */

static ssize_t velocity_max_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    return sysfs_emit(buf,"%u\n",dev->interval_run_ns);
}

static ssize_t velocity_max_store(struct device *device,struct device_attribute *attr,const char *buf,size_t count)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    u32 interval_ns;
    int ret;

    ret = kstrtou32(buf,0,&interval_ns);

    if (ret)
        return ret;

    ret = tmc5160_set_speed(dev,interval_ns);

    if (ret)
        return ret;

    return count;
}

static DEVICE_ATTR_RW(velocity_max);

/* Sysfs: accel                                                       */

static ssize_t accel_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    return sysfs_emit(buf,"%u\n",dev->ramp_steps);
}

static ssize_t accel_store(struct device *device, struct device_attribute *attr,const char *buf,size_t count)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    u32 ramp_steps;
    int ret;

    ret = kstrtou32(buf,0,&ramp_steps);

    if (ret)
        return ret;

    ret = tmc5160_set_accel(dev,ramp_steps,dev->interval_start_ns);

    if (ret)
        return ret;

    return count;
}

static DEVICE_ATTR_RW(accel);

/* Sysfs: enabled                                                     */

static ssize_t enabled_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    return sysfs_emit(buf,"%d\n",!gpiod_get_value_cansleep(dev->enable_gpio));
}

static ssize_t enabled_store(struct device *device,struct device_attribute *attr,const char *buf,size_t count)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    bool enable;
    int ret;

    ret = kstrtobool(buf,&enable);

    if (ret)
        return ret;

    mutex_lock(&dev->lock);

    tmc5160_set_enable(dev,enable);

    mutex_unlock(&dev->lock);

    return count;
}

static DEVICE_ATTR_RW(enabled);

/* Sysfs: fault_status                                                */

static ssize_t fault_status_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    u32 flags;
    int ret;

    mutex_lock(&dev->lock);

    ret = tmc5160_refresh_faults(dev,NULL);

    flags = dev->fault_flags;

    mutex_unlock(&dev->lock);

    if (ret)
        return ret;

    if (!flags)
        return sysfs_emit(buf,"ok\n");

    return sysfs_emit(
            buf,
            "%s%s%s%s%s%s\n",
            (flags & TMC5160_DRV_STATUS_OT)   ? "overtemp "      : "",
            (flags & TMC5160_DRV_STATUS_OTPW) ? "overtemp_warn " : "",
            (flags & TMC5160_DRV_STATUS_S2GA) ? "short_to_gnd_a " : "",
            (flags & TMC5160_DRV_STATUS_S2GB) ? "short_to_gnd_b " : "",
            (flags & TMC5160_DRV_STATUS_OLA)  ? "open_load_a "    : "",
            (flags & TMC5160_DRV_STATUS_OLB)  ? "open_load_b "    : "");
}

static DEVICE_ATTR_RO(fault_status);

/* Sysfs: in_motion                                                   */

static ssize_t in_motion_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    return sysfs_emit(buf,"%d\n",tmc5160_is_moving(dev) ? 1 : 0);
}

static DEVICE_ATTR_RO(in_motion);

/* Sysfs: cs_actual                                                   */

static ssize_t cs_actual_show(struct device *device,struct device_attribute *attr,char *buf)
{
    struct tmc5160_dev *dev = dev_get_drvdata(device);

    u32 drv = 0;
    int ret;

    mutex_lock(&dev->lock);

    ret = tmc5160_refresh_faults(dev,&drv);

    mutex_unlock(&dev->lock);

    if (ret)
        return ret;

    return sysfs_emit(buf,"%u\n",tmc5160_cs_actual(drv));
}

static DEVICE_ATTR_RO(cs_actual);

/* Attribute group                                                    */

static struct attribute *tmc5160_attrs[] = {
    &dev_attr_position.attr,
    &dev_attr_velocity_max.attr,
    &dev_attr_accel.attr,
    &dev_attr_enabled.attr,
    &dev_attr_fault_status.attr,
    &dev_attr_in_motion.attr,
    &dev_attr_cs_actual.attr,
    NULL,
};

ATTRIBUTE_GROUPS(tmc5160);

/* Initialization / cleanup                                           */

int tmc5160_cdev_init(struct tmc5160_dev *dev,struct class *tmc5160_class)
{
    int ret;

    ret = alloc_chrdev_region(&dev->devt,0,1,"tmc5160");
    if (ret)
        return ret;

    cdev_init(&dev->cdev,&tmc5160_fops);

    dev->cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev,dev->devt,1);
    if (ret)
        goto err_unregister;

    dev->device =
        device_create_with_groups(
            tmc5160_class,
            &dev->spi->dev,
            dev->devt,
            dev,
            tmc5160_groups,
            "tmc5160-%d",
            MINOR(dev->devt));

    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_cdev;
    }

    init_waitqueue_head(&dev->poll_wait_queue);

    return 0;

err_cdev:
    cdev_del(&dev->cdev);

err_unregister:
    unregister_chrdev_region(dev->devt,1);

    return ret;
}

void tmc5160_cdev_cleanup(struct tmc5160_dev *dev)
{
    device_destroy(dev->device->class,dev->devt);

    cdev_del(&dev->cdev);

    unregister_chrdev_region(dev->devt,1);
}