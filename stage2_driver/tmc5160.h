
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
#define MICROSTEPS			16

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
    u8                   fault_flags;
};

/* ioctl numbers */

/* motion parameters */

/* function prototypes for all three layers */

