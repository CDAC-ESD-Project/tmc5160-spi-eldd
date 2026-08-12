#ifndef TMC5160_H
#define TMC5160_H

#include <linux/cdev.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>


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

struct tmc5160_dev {
    /* for tmc5160_motion.c */ 
    spinlock_t motion_lock;

    struct hrtimer step_timer;
    struct completion move_done;

    atomic64_t position_steps;
    atomic_t in_motion;

    u32 total_steps;
    u32 steps_remaining;
    int direction;

    u32 interval_start_ns;
    u32 interval_run_ns;
    u32 current_interval_ns;

    u32 ramp_steps;
    u32 active_ramp_steps;

    /* -------------------- cdev layer -------------------- */
    wait_queue_head_t poll_wait_queue;
};

/* function prototypes for all three layers */
/* Motion layer exports */

int tmc5160_motion_init(struct tmc5160_dev *dev);
void tmc5160_motion_cleanup(struct tmc5160_dev *dev);

int tmc5160_move(struct tmc5160_dev *dev, u32 steps, int direction);

void tmc5160_stop(struct tmc5160_dev *dev);

s64 tmc5160_get_position(struct tmc5160_dev *dev);

int tmc5160_set_speed(struct tmc5160_dev *dev, u32 interval_ns);

int tmc5160_set_accel(struct tmc5160_dev *dev, u32 ramp_steps, u32 start_interval_ns);

int tmc5160_is_moving(struct tmc5160_dev *dev);

int tmc5160_wait_move(struct tmc5160_dev *dev, unsigned int timeout_ms);

#endif
