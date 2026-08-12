#ifndef TMC5160_H
#define TMC5160_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/log2.h>
#include <linux/minmax.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/gpio/consumer.h>
#include <linux/ktime.h>

/* Motion timing constants */
#define TMC_DEFAULT_START_NS      1200000U
#define TMC_DEFAULT_RUN_NS         312500U
#define TMC_MIN_INTERVAL_NS         50000U
#define TMC_MAX_INTERVAL_NS       5000000U
#define TMC_MIN_RAMP_STEPS              1U
#define TMC_MAX_RAMP_STEPS         100000U
#define TMC_DEFAULT_RAMP_STEPS       6667U
#define TMC_DIR_FORWARD                1
#define TMC_DIR_REVERSE               -1

/* shared constants */
#define TMC5160_VERSION_EXPECTED		0x30
#define TMC5160_FULLSTEPS_PER_REV		200
#define TMC5160_DEFAULT_USTEPS			16
#define TMC5160_DEFAULT_INTERVAL		800000
#define TMC5160_DEFAULT_IRUN			22
#define TMC5160_DEFAULT_IHOLD			10
#define TMC5160_STEP_HIGH_NS			2000
#define TMC5160_FAULT_PENDING			0xFFFFFFFFU
#define TMC5160_DEFAULT_IHOLDDELAY		6
#define TMC5160_DEFAULT_TPOWERDOWN		10
#define TMC5160_DEFAULT_TCOOLTHRS		0
#define TMC5160_DEFAULT_GLOBAL_SCALER	0xC8

#define TMC5160_CHOPCONF_TOFF			3
#define TMC5160_CHOPCONF_HSTRT			5
#define TMC5160_CHOPCONF_HEND			2
#define TMC5160_CHOPCONF_TBL			2
#define TMC5160_CHOPCONF_INTPOL			1

#define TMC5160_GCONF_EN_PWM_MODE		BIT(5)
#define TMC5160_GCONF_SPREAD_EN			BIT(6)
#define TMC5160_GCONF_DIAG0_ERROR		BIT(8)
#define TMC5160_GCONF_DIAG1_STALL		BIT(12)
#define TMC5160_GCONF_DIAG1_PUSHPULL	BIT(13)

struct tmc5160_dev {
    // for tmc5160_hw.c
    struct spi_device   *spi;
    struct gpio_desc    *step_gpio;
    struct gpio_desc    *dir_gpio;
    struct gpio_desc    *enable_gpio;
    struct gpio_desc    *diag0_gpio;
    int                  diag0_irq;
	  struct gpio_desc	  *diag1_gpio;
    int					         diag1_irq;
    struct work_struct   diag0_work;
	  struct work_struct   diag1_work;
	  atomic_t             stall_pending;
	  // for tmc5160_motion.c
    struct hrtimer       step_timer;
    struct completion    move_done;
    atomic64_t             position_steps;
    atomic_t             in_motion;
    u32                  total_steps;
    u32                  steps_remaining;
    int                  direction;
    ktime_t              interval_run;
    ktime_t              interval_start;
    u32                  ramp_steps;
    u32                  current_interval_ns;
    spinlock_t motion_lock;
    // for tmc5160_cdev.c
    struct cdev          cdev;
    struct device       *device;
    struct mutex         lock;
    atomic_t             open_count;
    u32                  fault_flags;
    wait_queue_head_t    poll_wait_queue;
    s32                  limit_min_mdeg;
    s32                  limit_max_mdeg;
    bool                 limits_set;
    dev_t                devt;
};

/* IOCTL numbers */
#define TMC_IOC_MAGIC        'T'

#define TMC_SET_VMAX         _IOW(TMC_IOC_MAGIC, 1, u32)
#define TMC_SET_AMAX         _IOW(TMC_IOC_MAGIC, 2, struct tmc5160_accel_cmd)
#define TMC_SET_IRUN         _IOW(TMC_IOC_MAGIC, 3, u8)
#define TMC_SET_IHOLD        _IOW(TMC_IOC_MAGIC, 4, u8)
#define TMC_SET_MICROSTEP    _IOW(TMC_IOC_MAGIC, 5, u32)
#define TMC_GET_STATUS       _IOR(TMC_IOC_MAGIC, 6, struct tmc5160_status)
#define TMC_STOP             _IO(TMC_IOC_MAGIC, 7)
#define TMC_ENABLE           _IO(TMC_IOC_MAGIC, 8)
#define TMC_DISABLE          _IO(TMC_IOC_MAGIC, 9)
#define TMC_SET_HOME         _IO(TMC_IOC_MAGIC,10)
#define TMC_SET_SOFT_LIMITS  _IOW(TMC_IOC_MAGIC,11, struct tmc5160_limits)

/*main layer*/
char *tmc5160_devnode(struct device *dev, umode_t *mode);

/* cdev exports */
int tmc5160_cdev_init(struct tmc5160_dev *dev, struct class *tmc5160_class);
void tmc5160_cdev_cleanup(struct tmc5160_dev *dev);

/* motion layer exports */
int tmc5160_motion_init(struct tmc5160_dev *dev);
void tmc5160_motion_cleanup(struct tmc5160_dev *dev);
int tmc5160_move(struct tmc5160_dev *dev, u32 steps, int direction);
void tmc5160_stop(struct tmc5160_dev *dev);
s64 tmc5160_get_position(struct tmc5160_dev *dev);
int tmc5160_set_speed(struct tmc5160_dev *dev, u32 interval_ns);
int tmc5160_set_accel(struct tmc5160_dev *dev, u32 ramp_steps, u32 start_interval_ns);
int tmc5160_is_moving(struct tmc5160_dev *dev);
int tmc5160_wait_move(struct tmc5160_dev *dev, unsigned int timeout_ms);

/* hardware layer exports */
int tmc5160_hw_init(struct tmc5160_dev *dev);
void tmc5160_hw_cleanup(struct tmc5160_dev *dev);
int tmc5160_hw_configure(struct tmc5160_dev *dev);
int tmc5160_write_reg(struct tmc5160_dev *dev, u8 addr, u32 data);
int tmc5160_read_reg(struct tmc5160_dev *dev, u8 addr, u32 *out);
void tmc5160_set_dir(struct tmc5160_dev *dev, int forward);
void tmc5160_set_enable(struct tmc5160_dev *dev, int enable);
void tmc5160_step_pulse(struct tmc5160_dev *dev);
int tmc5160_read_faults(struct tmc5160_dev *dev, u32 *drv, u32 *gstat);
int tmc5160_set_current(struct tmc5160_dev *dev, u8 irun, u8 ihold);
int tmc5160_set_microstep(struct tmc5160_dev *dev, u32 usteps);
int tmc5160_verify_ioin(struct tmc5160_dev *dev);

#endif 
