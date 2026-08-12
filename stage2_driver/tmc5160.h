/*
 * tmc5160.h
 *
 * Unified shared header for the TMC5160 Linux SPI stepper motor driver.
 * This file is the single contract between:
 *   - tmc5160_hw.c
 *   - tmc5160_motion.c
 *   - tmc5160_cdev.c
 *   - tmc5160_main.c
 */


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

#include "tmc5160.h"


/* Device structure*/

struct tmc5160_dev {

    /* -------------------- main layer -------------------- */

    struct spi_device *spi;

    /* -------------------- cdev layer -------------------- */

    struct cdev cdev;
    struct device *device;

    struct mutex lock;

    atomic_t open_count;

    u32 fault_flags;

    wait_queue_head_t poll_wait_queue;

    s32 limit_min_mdeg;
    s32 limit_max_mdeg;
    bool limits_set;

    dev_t devt;
};



/* IOCTL numbers*/

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



/* ------------------------------------------------------------------ */
/* Character device layer exports                                     */
/* ------------------------------------------------------------------ */

int tmc5160_cdev_init(struct tmc5160_dev *dev,
                      struct class *tmc5160_class);

void tmc5160_cdev_cleanup(struct tmc5160_dev *dev);

#endif /* TMC5160_H */
