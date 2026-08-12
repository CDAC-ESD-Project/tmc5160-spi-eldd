#ifndef TMC5160_H
#define TMC5160_H

#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <log2.h>
#include <atomic.h>

/* register addresses */
#define REG_GCONF			0x00
#define REG_GSTAT			0x01
#define REG_IOIN			0x04
#define REG_IHOLD_IRUN		0x10
#define REG_TPOWERDOWN		0x11
#define REG_TCOOLTHRS		0x14
#define REG_GLOBAL_SCALER	0x0B
#define REG_CHOPCONF		0x6C
#define REG_DRV_STATUS		0x6F

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
	struct gpio_desc	*diag1_gpio;
	int					 diag1_irq;
    struct work_struct   diag0_work;
	struct work_struct   diag1_work;
	atomic_t             stall_pending;
	// for tmc5160_motion.c
    struct hrtimer       step_timer;
    struct completion    move_done;
    atomic_t             position_steps;
    atomic_t             in_motion;
    u32                  steps_remaining;
    int                  direction;
    ktime_t              interval_run;
    ktime_t              interval_start;
    u32                  ramp_steps;
    u32                  current_interval_ns;
    // for tmc5160_cdev.c
    struct cdev          cdev;
    struct device       *device;
    struct mutex         lock;
    atomic_t             open_count;
    u32                  fault_flags;
};

/* ioctl numbers */

/* motion parameters */

/* function prototypes for all three layers */
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
