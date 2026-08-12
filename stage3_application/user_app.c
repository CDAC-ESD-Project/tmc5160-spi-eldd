#include "userspace.h"

#define DEVICE "/dev/tmc5160-0"
#define HOMESW_DEVICE "/dev/homesw"

static int home_fd = -1;
static double current_position_mm = 0.0;

/* Mechanical configuration */
#define LEAD_MM_PER_REV      8.0
#define FULL_STEPS_PER_REV   200.0

static int fd;
static int fd1;

/* Current motion configuration */
static double   current_speed_mm_s   = 10.0;
static double   current_accel_mm_s2  = 50.0;
static uint32_t current_microsteps   = 16;
static uint32_t start_interval_ns    = 1200000;

/* Protect configuration values */
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

/* Utility conversions */
static double steps_per_mm(void)
{
    return (FULL_STEPS_PER_REV * current_microsteps) / LEAD_MM_PER_REV;
}

static uint32_t speed_to_interval_ns(double speed_mm_s)
{
    double s_per_mm = steps_per_mm();
    double step_rate = speed_mm_s * s_per_mm;

    if (step_rate <= 1.0)
        step_rate = 1.0;

    double interval = 1e9 / step_rate;

    if (interval < 1000.0)
        interval = 1000.0;

    return (uint32_t)(interval + 0.5);
}

static uint32_t accel_to_ramp_steps(double accel_mm_s2)
{
    double s_per_mm = steps_per_mm();

    double v_steps = current_speed_mm_s * s_per_mm;
    double a_steps = accel_mm_s2 * s_per_mm;

    if (a_steps <= 1.0)
        a_steps = 1.0;

    double ramp = (v_steps * v_steps) / (2.0 * a_steps);

    if (ramp < 1.0)
        ramp = 1.0;

    return (uint32_t)(ramp + 0.5);
}

/* Motion thread */
static void *move_thread(void *arg)
{
    double distance_mm = *(double *)arg;

    free(arg);

    struct tmc5160_move_cmd cmd;

    cmd.angle_mdeg =
        (int32_t)((distance_mm * 360000.0) / LEAD_MM_PER_REV);

    cmd.flags = 0;     /* relative move */

    printf("[MOVE] %.3f mm\n", distance_mm);

    if (write(fd, &cmd, sizeof(cmd)) != sizeof(cmd)) {
        perror("[MOVE] write");
        return NULL;
    }

    printf("[MOVE] completed\n");

    return NULL;
}

static int home_actuator(void)
{
    struct pollfd pfd;
    char val;
    double home_distance = -500.0;   /* guaranteed to hit home */
    pthread_t thread;

    printf("[HOME] Starting homing sequence...\n");

    home_fd = open(HOMESW_DEVICE, O_RDONLY);

    if (home_fd < 0) {
        perror("[HOME] open homesw");
        return -1;
    }

    /* Check whether switch is already active */
    lseek(home_fd, 0, SEEK_SET);

    if (read(home_fd, &val, 1) == 1 && val == '1') {

        printf("[HOME] Already at home position\n");

        if (ioctl(fd, TMC_SET_HOME) < 0)
            perror("[HOME] TMC_SET_HOME");

        current_position_mm = 0.0;

        return 0;
    }

    /* Start slow move toward home */
    double *dist = malloc(sizeof(double));

    if (!dist)
        return -1;

    *dist = home_distance;

    if (pthread_create(&thread, NULL, move_thread, dist) != 0) {
        perror("[HOME] pthread_create");
        free(dist);
        return -1;
    }

    pthread_detach(thread);

    /* Wait for home switch interrupt */
    pfd.fd = home_fd;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, -1) < 0) {
        perror("[HOME] poll");
        return -1;
    }

    /* Confirm switch state */
    lseek(home_fd, 0, SEEK_SET);

    if (read(home_fd, &val, 1) == 1 && val == '1') {

        printf("[HOME] Switch triggered\n");

        if (ioctl(fd, TMC_STOP) < 0)
            perror("[HOME] STOP");

        if (ioctl(fd, TMC_SET_HOME) < 0)
            perror("[HOME] TMC_SET_HOME");

        current_position_mm = 0.0;

        printf("[HOME] Homing complete. Position = 0 mm\n");

        return 0;
    }

    return -1;
}

static int start_move(double distance_mm)
{
    pthread_t thread;
    double *distance;

    distance = malloc(sizeof(*distance));

    if (!distance) {
        perror("malloc");
        return -1;
    }

    *distance = distance_mm;

    if (pthread_create(&thread, NULL, move_thread, distance) != 0) {
        perror("pthread_create");
        free(distance);
        return -1;
    }

    pthread_detach(thread);

    return 0;
}

/* Set thread */
enum set_type {
    SET_SPEED,
    SET_ACCEL,
    SET_MICROSTEPS,
    SET_IRUN,
    SET_IHOLD
};

struct set_args {
    enum set_type type;
    double value;
};

static void *set_thread(void *arg)
{
    struct set_args *args = arg;
    int ret = 0;

    switch (args->type) {

    case SET_SPEED: {

        pthread_mutex_lock(&config_lock);

        current_speed_mm_s = args->value;

        uint32_t interval = speed_to_interval_ns(current_speed_mm_s);

        pthread_mutex_unlock(&config_lock);

        ret = ioctl(fd, TMC_SET_VMAX, &interval);

        if (ret < 0)
            perror("[SET] speed");
        else
            printf("[SET] speed = %.3f mm/s (interval %u ns)\n",
                   current_speed_mm_s, interval);

        break;
    }

    case SET_ACCEL: {

        pthread_mutex_lock(&config_lock);

        current_accel_mm_s2 = args->value;

        uint32_t ramp = accel_to_ramp_steps(current_accel_mm_s2);

        uint32_t start_int = start_interval_ns;

        pthread_mutex_unlock(&config_lock);

        struct tmc5160_accel_cmd accel;

        accel.ramp_steps = ramp;
        accel.start_interval_ns = start_int;

        ret = ioctl(fd, TMC_SET_AMAX, &accel);

        if (ret < 0)
            perror("[SET] accel");
        else
            printf("[SET] accel = %.3f mm/s^2 (ramp %u steps)\n",
                   current_accel_mm_s2, ramp);

        break;
    }

    case SET_MICROSTEPS: {

        uint32_t micro = (uint32_t)args->value;

        ret = ioctl(fd, TMC_SET_MICROSTEP, &micro);

        if (ret < 0)
            perror("[SET] microsteps");
        else {

            pthread_mutex_lock(&config_lock);
            current_microsteps = micro;
            pthread_mutex_unlock(&config_lock);

            printf("[SET] microsteps = %u\n", micro);
        }

        break;
    }

    case SET_IRUN: {

        uint8_t irun = (uint8_t)args->value;

        ret = ioctl(fd, TMC_SET_IRUN, &irun);

        if (ret < 0)
            perror("[SET] irun");
        else
            printf("[SET] irun = %u\n", irun);

        break;
    }

    case SET_IHOLD: {

        uint8_t ihold = (uint8_t)args->value;

        ret = ioctl(fd, TMC_SET_IHOLD, &ihold);

        if (ret < 0)
            perror("[SET] ihold");
        else
            printf("[SET] ihold = %u\n", ihold);

        break;
    }
    }

    free(args);

    return NULL;
}

static int start_set(enum set_type type, double value)
{
    pthread_t thread;
    struct set_args *args;

    args = malloc(sizeof(*args));

    if (!args) {
        perror("malloc");
        return -1;
    }

    args->type = type;
    args->value = value;

    if (pthread_create(&thread, NULL, set_thread, args) != 0) {
        perror("pthread_create");
        free(args);
        return -1;
    }

    pthread_detach(thread);

    return 0;
}

/* STOP */
static void stop_motion(void)
{
    printf("[STOP] Sending STOP...\n");

    if (ioctl(fd, TMC_STOP) < 0) {
        perror("[STOP] ioctl");
        return;
    }

    printf("[STOP] Motor stopped\n");
}

/* Command parser */
static void process_command(char *input)
{
    char cmd[32], param[32];
    double value;

    input[strcspn(input, "\n")] = '\0';

    if (strlen(input) == 0)
        return;

    /* stop */
    if (strcmp(input, "stop") == 0) {
        stop_motion();
        return;
    }

    /* move <signed_mm> */
    if (sscanf(input, "%31s %lf", cmd, &value) == 2) {

        if (strcmp(cmd, "move") == 0) {

            start_move(value);

            return;
        }
    }

    /* set <parameter> <value> */
    if (sscanf(input, "%31s %31s %lf", cmd, param, &value) == 3) {

        if (strcmp(cmd, "set") == 0) {

            if (strcmp(param, "speed") == 0) {
                start_set(SET_SPEED, value);
                return;
            }

            if (strcmp(param, "accel") == 0) {
                start_set(SET_ACCEL, value);
                return;
            }

            if (strcmp(param, "microsteps") == 0) {
                start_set(SET_MICROSTEPS, value);
                return;
            }

            if (strcmp(param, "irun") == 0) {
                start_set(SET_IRUN, value);
                return;
            }

            if (strcmp(param, "ihold") == 0) {
                start_set(SET_IHOLD, value);
                return;
            }
        }
    }

    printf("[ERROR] Invalid command: %s\n", input);
}

/* Main */
int main(void)
{
    char input[128];

    printf("=================================\n");
    printf("       TMC5160 Application\n");
    printf("=================================\n");

    fd = open(DEVICE, O_RDWR);

    if (fd < 0) {
        perror("open /dev/tmc5160-0");
        return 1;
    }

    fd1 = open(HOMESW_DEVICE, O_RDWR);

    if (fd1 < 0) {
        perror("open /dev/homesw");
        return 1;
    }

    /* Perform homing before accepting commands */
    if (home_actuator() < 0) {
        fprintf(stderr, "Homing failed\\n");
        close(fd1);
        return 1;
    }  

    printf("Device opened: %s\n", DEVICE);

    printf("\nCommands:\n");
    printf("  move <mm>\n");
    printf("  stop\n");
    printf("  set speed <mm/s>\n");
    printf("  set accel <mm/s^2>\n");
    printf("  set microsteps <value>\n");
    printf("  set irun <value>\n");
    printf("  set ihold <value>\n");
    printf("\n");

    while (1) {

        printf("TMC> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        process_command(input);
    }

    close(fd);

    return 0;
}
