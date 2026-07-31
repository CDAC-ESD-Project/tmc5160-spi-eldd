#include <linux/cdev.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>


// need to tune later
#define TMC_DEFAULT_START_NS    1200000U // 1200us
#define TMC_DEFAULT_RUN_NS       800000U // 800us

#define TMC_MIN_INTERVAL_NS       50000U // 50us
#define TMC_MAX_INTERVAL_NS     5000000U // 5000us / 5ms
#define TMC_START_MARGIN_NS      400000U // 400us margin

#define TMC_MIN_RAMP_STEPS            1U
#define TMC_MAX_RAMP_STEPS       100000U
#define TMC_DEFAULT_RAMP_STEPS      100U
//--------------------------------------------------------------

#define TMC_DIR_FORWARD     1
#define TMC_DIR_REVERSE    -1 

struct tmc5160_dev {
                    // // for tmc5160_hw.c ------------------------------------------------
                    // struct spi_device   *spi;
                    // struct gpio_desc    *step_gpio;
                    // struct gpio_desc    *dir_gpio;
                    // struct gpio_desc    *enable_gpio;
                    // struct gpio_desc    *diag0_gpio;
                    // int                  diag0_irq;

    // for tmc5160_motion.c --------------------------------------------
    /*Spinlock*/
    spinlock_t   motion_lock;

    /* High resolution step timer */
    struct hrtimer step_timer;

    /* Move completion object */
    struct completion move_done;

    /* Motion state */
    atomic_t position_steps; //software position counter(uSteps)
    atomic_t in_motion;
    u32 total_steps;
    u32 steps_remaining;
    int direction;

    /* Speed parameters */
    u32 interval_start_ns;
    u32 interval_run_ns;
    u32 current_interval_ns;

    /* Ramp parameters */
    u32 ramp_steps;
    u32 active_ramp_steps;


                    // // for tmc5160_cdev.c -----------------------------------------------
                    // struct cdev          cdev;
                    // struct device       *device;
                    // struct mutex         lock;
                    // atomic_t             open_count;
                    // u8                   fault_flags;
};

                    /* ioctl numbers */
                    /* motion parameters */
                    /* shared constants (steps/rev, etc.) */

/* function prototypes for all three layers */
/* API / functions exposed by motion layer */
int tmc5160_motion_init(struct tmc5160_dev *dev);
void tmc5160_motion_exit(struct tmc5160_dev *dev);
int tmc5160_move(struct tmc5160_dev *dev, u32 steps, int direction);
void tmc5160_stop(struct tmc5160_dev *dev);
int tmc5160_get_position(struct tmc5160_dev *dev);
int tmc5160_set_speed(struct tmc5160_dev *dev, u32 interval_ns);
int tmc5160_set_accel(struct tmc5160_dev *dev, u32 ramp_steps);