#include "tmc5160.h"

/* private declarations */
static enum hrtimer_restart step_timer_callback(struct hrtimer *timer);
static void update_position(struct tmc5160_dev *dev);
static bool update_step_count(struct tmc5160_dev *dev);
static void update_ramp(struct tmc5160_dev *dev);

/* function definitions */
int tmc5160_motion_init(struct tmc5160_dev *dev)
{
    init_completion(&dev->move_done);

    spin_lock_init(&dev->motion_lock);

    atomic_set(&dev->position_steps, 0);
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

void tmc5160_motion_exit(struct tmc5160_dev *dev)
{
    hrtimer_cancel(&dev->step_timer);
}

int tmc5160_move(struct tmc5160_dev *dev, u32 steps, int direction)
{
    unsigned long flags;

    if(steps == 0)
        return 0;

    if(direction != TMC_DIR_FORWARD && direction != TMC_DIR_REVERSE)
        return -EINVAL;

    if(atomic_cmpxchg(&dev->in_motion, 0, 1))
        return -EBUSY; // device is busy processing previous request

    /* Protected by Spinlock */
    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->direction = direction;
    dev->total_steps = steps;
    dev->steps_remaining = steps;
    dev->active_ramp_steps = dev->ramp_steps;
    dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    reinit_completion(&dev->move_done);

    tmc5160_set_dir(dev, direction); // set motor direction first

    hrtimer_start(&dev->step_timer, ns_to_ktime(dev->current_interval_ns), HRTIMER_MODE_REL);

    return 0;
}

static enum hrtimer_restart step_timer_callback(struct hrtimer *timer)
{
    struct tmc5160_dev *dev;
    dev = container_of(timer, struct tmc5160_dev, step_timer);

    /* Generate one step pulse */
    tmc5160_step_pulse(dev);

    update_position(dev);

    if(update_step_count(dev))
        return HRTIMER_NORESTART;
    
    update_ramp(dev);

    hrtimer_forward_now(timer, ns_to_ktime(dev->current_interval_ns));
    
    return HRTIMER_RESTART;

}

void tmc5160_stop(struct tmc5160_dev *dev)
{
    unsigned long flags;

    hrtimer_cancel(&dev->step_timer);
    atomic_set(&dev->in_motion, 0);

     /* Protected by Spinlock */
    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->steps_remaining = 0;
    dev->total_steps = 0;
    dev->direction = TMC_DIR_FORWARD;
    dev->active_ramp_steps = dev->ramp_steps;
    dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    complete(&dev->move_done);
}


int tmc5160_get_position(struct tmc5160_dev *dev)
{
    return atomic_read(&dev->position_steps);
}

int tmc5160_set_speed(struct tmc5160_dev *dev, u32 interval_ns)
{
    unsigned long flags;

     /* Protected by Spinlock */
    spin_lock_irqsave(&dev->motion_lock, flags);

    if(interval_ns < TMC_MIN_INTERVAL_NS || interval_ns > TMC_MAX_INTERVAL_NS)
    { 
        spin_unlock_irqrestore(&dev->motion_lock, flags);
        return -EINVAL;
    }
    
    dev->interval_run_ns = interval_ns;
    dev->interval_start_ns = interval_ns + TMC_START_MARGIN_NS;

    if(!atomic_read(&dev->in_motion))
        dev->current_interval_ns = dev->interval_start_ns;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    return 0;
}


int tmc5160_set_accel(struct tmc5160_dev *dev, u32 ramp_steps)
{
    unsigned long flags;

    if(ramp_steps < TMC_MIN_RAMP_STEPS || ramp_steps > TMC_MAX_RAMP_STEPS)
        return -EINVAL;
    
    if(atomic_read(&dev->in_motion))
        return -EBUSY;

     /* Protected by Spinlock */
    spin_lock_irqsave(&dev->motion_lock, flags);

    dev->ramp_steps = ramp_steps;

    spin_unlock_irqrestore(&dev->motion_lock, flags);

    return 0;
}

static void update_position(struct tmc5160_dev *dev)
{
    if(dev->direction == TMC_DIR_FORWARD)
        atomic_inc(&dev->position_steps);
    else
        atomic_dec(&dev->position_steps);
}

static bool update_step_count(struct tmc5160_dev *dev)
{
    bool finished = false;

     /* Protected by Spinlock */
    spin_lock(&dev->motion_lock);
    if(dev->steps_remaining > 0)
        dev->steps_remaining--;

    if(dev->steps_remaining == 0)
        finished = true;

    spin_unlock(&dev->motion_lock);

    if(finished)
    {
        atomic_set(&dev->in_motion, 0);
        complete(&dev->move_done);
        return true;
    }
    return false;
}

static void update_ramp(struct tmc5160_dev *dev)
{
    u32 completed_steps;
    u32 effective_ramp;
    u32 delta;
    u64 temp;

     /* Protected by Spinlock */
    spin_lock(&dev->motion_lock);

    completed_steps = dev->total_steps - dev->steps_remaining;

    effective_ramp = min_t(u32, dev->active_ramp_steps, dev->total_steps / 2);

    if(effective_ramp == 0)
    {
        dev->current_interval_ns = dev->interval_run_ns;
        goto out;
    }

    if(dev->interval_start_ns <= dev->interval_run_ns)
    {
        dev->current_interval_ns = dev->interval_run_ns;
        goto out;
    }

    delta = dev->interval_start_ns - dev->interval_run_ns;

    /* Acceleration phase */
    if(completed_steps < effective_ramp)
    {
        temp = (u64)delta * completed_steps;
        temp = temp / effective_ramp;
        dev->current_interval_ns = dev->interval_start_ns - (u32)temp;
    }

    /* Deceleration phase */
    else if(dev->steps_remaining < effective_ramp)
    {
        temp = (u64)delta * (effective_ramp - dev->steps_remaining);
        temp = temp / effective_ramp;
        dev->current_interval_ns = dev->interval_run_ns + (u32)temp;
    }

    /* Constant Speed */
    else
        dev->current_interval_ns = dev->interval_run_ns;

    out:
    spin_unlock(&dev->motion_lock);
}