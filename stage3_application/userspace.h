#ifndef TMC5160_USERSPACE_H
#define TMC5160_USERSPACE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <poll.h>

#define TMC_MOVE_FLAG_ABSOLUTE (1U << 0)

struct tmc5160_move_cmd {
    int32_t angle_mdeg;
    uint32_t flags;
};

struct tmc5160_status {
    int32_t position_mdeg;
    uint32_t velocity_interval;
    uint32_t fault_flags;
    uint8_t in_motion;
    uint8_t stalled;
    uint8_t cs_actual;
};

struct tmc5160_accel_cmd {
    uint32_t ramp_steps;
    uint32_t start_interval_ns;
};

#define TMC_IOC_MAGIC        'T'

#define TMC_SET_VMAX         _IOW(TMC_IOC_MAGIC, 1, uint32_t)
#define TMC_SET_AMAX         _IOW(TMC_IOC_MAGIC, 2, struct tmc5160_accel_cmd)
#define TMC_SET_IRUN         _IOW(TMC_IOC_MAGIC, 3, uint8_t)
#define TMC_SET_IHOLD        _IOW(TMC_IOC_MAGIC, 4, uint8_t)
#define TMC_SET_MICROSTEP    _IOW(TMC_IOC_MAGIC, 5, uint32_t)
#define TMC_GET_STATUS       _IOR(TMC_IOC_MAGIC, 6, struct tmc5160_status)
#define TMC_STOP             _IO(TMC_IOC_MAGIC, 7)
#define TMC_ENABLE           _IO(TMC_IOC_MAGIC, 8)
#define TMC_DISABLE          _IO(TMC_IOC_MAGIC, 9)
#define TMC_SET_HOME         _IO(TMC_IOC_MAGIC,10)

#endif
