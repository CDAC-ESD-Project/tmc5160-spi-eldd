/* tmc5160_motion.c
 *
 * Motion control layer for the TMC5160 driver.
 * Owns motion planning, software position tracking, and the hrtimer.
 */


#include "tmc5160.h"

/* Private declarations */

static enum hrtimer_restart step_timer_callback(struct hrtimer *timer);

/* Helper Function for HRTIMER_Callback*/
static void tmc5160_update_position(struct tmc5160_dev *dev);
static bool tmc5160_update_step_count(struct tmc5160_dev *dev);
static void tmc5160_update_ramp(struct tmc5160_dev *dev);

/* Initialization / cleanup */

int tmc5160_motion_init(struct tmc5160_dev *dev)
{
    init_completion(&dev->move_done);

    spin_lock_init(&dev->motion_lock);

    atomic64_set(&dev->position_steps, 0);
    atomic_set(&dev->in_motion, 0);

    dev->total_steps = 0;
    dev->steps_remaining = 0;
    dev->direction = TMC_DIR_FORWARD;

    dev->interval_start_ns = TMC_DEFAULT_START_NS;
    dev->interval_run_ns = TMC_DEFAULT_RUN_NS;
    dev->current_interval_ns = dev->interval_start_ns;

    dev->ramp_steps = TMC_DEFAULT_RAMP_STEPS;
    dev->active_ramp_steps = dev->ramp_steps;

    hrtimer_init(&dev->step_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

    dev->step_timer.function = step_timer_callback;

    return 0;
}

void tmc5160_motion_cleanup(struct tmc5160_dev *dev)
{
    hrtimer_cancel(&dev->step_timer);
}

/* Public motion API */

int tmc5160_move(struct tmc5160_dev *dev, u32 steps, int direction)
{
    unsigned long flags;

    if (steps == 0)
        return 0;

    if (direction != TMC_DIR_FORWARD &&
        direction != TMC_DIR_REVERSE)
        return -EINVAL;

    if (atomic_cmpxchg(&dev->in_motion, 0, 1))
        return -EBUSY;

    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->direction = direction;
    dev->total_steps = steps;
    dev->steps_remaining = steps;

    dev->active_ramp_steps = dev->ramp_steps;
    dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    reinit_completion(&dev->move_done);

    tmc5160_set_dir(dev, direction == TMC_DIR_FORWARD);

    hrtimer_start(&dev->step_timer, ns_to_ktime(dev->current_interval_ns), HRTIMER_MODE_REL);

    return 0;
}

void tmc5160_stop(struct tmc5160_dev *dev)
{
    unsigned long flags;

    hrtimer_cancel(&dev->step_timer);

    atomic_set(&dev->in_motion, 0);

    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->steps_remaining = 0;
    dev->total_steps = 0;

    dev->direction = TMC_DIR_FORWARD;

    dev->active_ramp_steps = dev->ramp_steps;
    dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    complete(&dev->move_done);

    wake_up_interruptible(&dev->poll_wait_queue);
}

s64 tmc5160_get_position(struct tmc5160_dev *dev)
{
    return atomic64_read(&dev->position_steps);
}

int tmc5160_set_speed(struct tmc5160_dev *dev, u32 interval_ns)
{
    unsigned long flags;

    if (interval_ns < TMC_MIN_INTERVAL_NS ||
        interval_ns > TMC_MAX_INTERVAL_NS)
        return -EINVAL;

    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->interval_run_ns = interval_ns;

    if (!atomic_read(&dev->in_motion))
        dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    return 0;
}

int tmc5160_set_accel(struct tmc5160_dev *dev, u32 ramp_steps, u32 start_interval_ns)
{
    unsigned long flags;

    if (ramp_steps < TMC_MIN_RAMP_STEPS ||
        ramp_steps > TMC_MAX_RAMP_STEPS)
        return -EINVAL;

    if (atomic_read(&dev->in_motion))
        return -EBUSY;

    if (start_interval_ns < dev->interval_run_ns)
        return -EINVAL;

    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->ramp_steps = ramp_steps;
    dev->interval_start_ns = start_interval_ns;

    if (!atomic_read(&dev->in_motion))
        dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    return 0;
}

int tmc5160_is_moving(struct tmc5160_dev *dev)
{
    return atomic_read(&dev->in_motion);
}

int tmc5160_wait_move(struct tmc5160_dev *dev, unsigned int timeout_ms)
{
    unsigned long timeout;

    timeout = wait_for_completion_timeout(&dev->move_done, msecs_to_jiffies(timeout_ms));

    if (timeout == 0)
        return -ETIMEDOUT;

    return 0;
}

/* Timer callback */

static enum hrtimer_restart
step_timer_callback(struct hrtimer *timer)
{
    struct tmc5160_dev *dev =
        container_of(timer, struct tmc5160_dev, step_timer);

    tmc5160_step_pulse(dev);

    tmc5160_update_position(dev);

    if (tmc5160_update_step_count(dev))
        return HRTIMER_NORESTART;

    tmc5160_update_ramp(dev);

    hrtimer_forward_now(timer, ns_to_ktime(dev->current_interval_ns));

    return HRTIMER_RESTART;
}

/* Internal helpers */

static void tmc5160_update_position(struct tmc5160_dev *dev)
{
    if (dev->direction == TMC_DIR_FORWARD)
        atomic64_inc(&dev->position_steps);
    else
        atomic64_dec(&dev->position_steps);
}

static bool tmc5160_update_step_count(struct tmc5160_dev *dev)
{
    bool finished = false;

    spin_lock(&dev->motion_lock);

    if (dev->steps_remaining > 0)
        dev->steps_remaining--;

    if (dev->steps_remaining == 0)
        finished = true;

    spin_unlock(&dev->motion_lock);

    if (finished) {
        atomic_set(&dev->in_motion, 0);

        complete(&dev->move_done);

        wake_up_interruptible(&dev->poll_wait_queue);

        return true;
    }

    return false;
}

static void tmc5160_update_ramp(struct tmc5160_dev *dev)
{
    u32 completed_steps;
    u32 effective_ramp;
    u32 delta;
    u64 temp;

    spin_lock(&dev->motion_lock);

    completed_steps = dev->total_steps - dev->steps_remaining;

    effective_ramp = min_t(u32, dev->active_ramp_steps, dev->total_steps / 2);

    if (effective_ramp == 0) {
        dev->current_interval_ns = dev->interval_run_ns;
        goto out;
    }

    if (dev->interval_start_ns <= dev->interval_run_ns) {
        dev->current_interval_ns = dev->interval_run_ns;
        goto out;
    }

    delta = dev->interval_start_ns - dev->interval_run_ns;

    if (completed_steps < effective_ramp) {

        temp = (u64)delta * completed_steps;
        temp = div_u64(temp, effective_ramp);

        dev->current_interval_ns = dev->interval_start_ns - (u32)temp;
    }
    else if (dev->steps_remaining < effective_ramp) {

        temp = (u64)delta * (effective_ramp - dev->steps_remaining);

        temp = div_u64(temp, effective_ramp);

        dev->current_interval_ns = dev->interval_run_ns + (u32)temp;
    }
    else {
        dev->current_interval_ns = dev->interval_run_ns;
    }

out:
    spin_unlock(&dev->motion_lock);
}