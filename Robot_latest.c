#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#define MOTOR_GPIO_BASE   0xA0000000
#define SENSOR_GPIO_BASE  0xA0010000
#define SERVO_GPIO_BASE   0xA0020000
#define MAP_SIZE          0x10000

#define GPIO_DATA   0x00
#define GPIO_TRI    0x04
#define GPIO2_DATA  0x08
#define GPIO2_TRI   0x0C

#define FORWARD   1
#define BACKWARD  0

#define CMD_COAST  0x00
#define CMD_BRAKE  0xFF

#define MOTOR_PWM 25

#define OBSTACLE_THRESHOLD_CM 25.0

#define SERVO_PERIOD_US 20000
#define SERVO_LEFT_US   500     // 180 deg
#define SERVO_CENTER_US 1400    // 90 deg
#define SERVO_RIGHT_US  2550    // 0 deg

#define TURN_TIME_MS       1800
#define BACKWARD_TIME_MS   3000
#define BRAKE_TIME_MS      300
#define SERVO_SETTLE_MS    400

#define ECHO_TIMEOUT_US    30000 // 30 ms, maximum measurable distance with the sensor

#define LOCAL_SOCK_PATH    "/tmp/robot_fw.sock"

static double g_last_distance_cm = 0.0;
static volatile int interrupted   = 0;

static void sigint_handler(int sig)
{
    (void)sig;
    interrupted = 1;
}

typedef enum {
    Z0_IDLE,
    Z1_SCAN_FRONT,
    Z2_MOVE_FORWARD,
    Z3_BRAKE,
    Z4_SCAN_RIGHT,
    Z5_TURN_RIGHT,
    Z6_SCAN_LEFT,
    Z7_TURN_LEFT,
    Z8_MOVE_BACKWARD
} State;

static long time_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000L + t.tv_nsec / 1000L;
}

static void gpio_write(volatile uint32_t *gpio, uint32_t offset, uint32_t value)
{
    gpio[offset / 4] = value; // offset given in bytes, gpio index in 4-bytes
}

static uint32_t gpio_read(volatile uint32_t *gpio, uint32_t offset)
{
    return gpio[offset / 4];
}

/* ---------------- Console commands ---------------- */

static int get_console_command_nonblocking(char *buffer, size_t size)
{
    fd_set fds;
    struct timeval tv = {0, 0};

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
        if (fgets(buffer, size, stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0';
            return 1;
        }
    }

    return 0;
}

static int stop_requested(void)
{
    if (interrupted) return 1;

    char cmd[32];
    if (get_console_command_nonblocking(cmd, sizeof(cmd))) {
        if (strcmp(cmd, "stop") == 0) {
            return 1;
        }
    }

    return 0;
}

/* ---------------- Motor functions ---------------- */

static uint8_t motor_cmd(int direction, int pwm)
{
    if (pwm > 100) pwm = 100;
    if (pwm < 0) pwm = 0;

    return (uint8_t)(((direction & 1) << 7) | (pwm & 0x7F)); // creates 8-bit value with MSB = direction (& 1 for bit conversion)
    // and saving the pwm value in the other 7 bits (& 0x7F for bit conversion: 01111111)
}

static uint32_t pack_motors(uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3) // packs all motor registers in one 32-bit register
{
    return ((uint32_t)m3 << 24) |
           ((uint32_t)m2 << 16) |
           ((uint32_t)m1 << 8)  |
           ((uint32_t)m0);
}

/*
Motor order:
M0 = front left
M1 = back left
M2 = front right
M3 = back right
*/

static void motors_forward(volatile uint32_t *motors)
{
    uint8_t cmd = motor_cmd(FORWARD, MOTOR_PWM); // creates 8-bit value, PWM constant
    gpio_write(motors, GPIO_DATA, pack_motors(cmd, cmd, cmd, cmd)); // saves value to AXI GPIO Regs
}

static void motors_backward(volatile uint32_t *motors)
{
    uint8_t cmd = motor_cmd(BACKWARD, MOTOR_PWM);
    gpio_write(motors, GPIO_DATA, pack_motors(cmd, cmd, cmd, cmd));
}

static void motors_brake(volatile uint32_t *motors)
{
    gpio_write(motors, GPIO_DATA,
               pack_motors(CMD_BRAKE, CMD_BRAKE, CMD_BRAKE, CMD_BRAKE)); // vordefiniertes Register
}

static void motors_coast(volatile uint32_t *motors)
{
    gpio_write(motors, GPIO_DATA,
               pack_motors(CMD_COAST, CMD_COAST, CMD_COAST, CMD_COAST));
}

static void motors_turn_right(volatile uint32_t *motors)
{
    uint8_t left  = motor_cmd(FORWARD, MOTOR_PWM);
    uint8_t right = motor_cmd(BACKWARD, MOTOR_PWM);

    gpio_write(motors, GPIO_DATA, pack_motors(left, left, right, right));
}

static void motors_turn_left(volatile uint32_t *motors)
{
    uint8_t left  = motor_cmd(BACKWARD, MOTOR_PWM);
    uint8_t right = motor_cmd(FORWARD, MOTOR_PWM);

    gpio_write(motors, GPIO_DATA, pack_motors(left, left, right, right));
}

/* ---------------- Servo functions ---------------- */

static int angle_to_pulse_us(int angle_deg)
{
    if (angle_deg < 0) angle_deg = 0; // Der Servomotor bewegt sich zwischen 0°-180°
    if (angle_deg > 180) angle_deg = 180;

    return SERVO_RIGHT_US -
           (angle_deg * (SERVO_RIGHT_US - SERVO_LEFT_US)) / 180; // Umwandlung in PWM duty cycle [us]
}

static void servo_pulse(volatile uint32_t *servo, int high_time_us) // generates PWM signal
{
    gpio_write(servo, GPIO_DATA, 1); // on-intervall
    usleep(high_time_us);

    gpio_write(servo, GPIO_DATA, 0); // off-intervall
    usleep(SERVO_PERIOD_US - high_time_us);
}

static int servo_hold_angle(volatile uint32_t *servo, int angle_deg, int duration_ms)
{
    int pulse = angle_to_pulse_us(angle_deg);
    int cycles = duration_ms / 20; // 50 Hz PWM -> 20 ms Periode, berechnet benötige PWM Zyklen

    for (int i = 0; i < cycles; i++) {
        if (stop_requested()) return 1; 
        servo_pulse(servo, pulse); // Creats one PWM cycle.
        // possible cause of failure: for and if clauses take time: dead time between PWM cycles (more time off than desired)
    }

    return 0;
}

/* ---------------- Ultrasound functions ---------------- */

static double ultrasound_read_cm(volatile uint32_t *sensor)
{
    long start_wait, echo_start, echo_end;
    long pulse_width_us;

    gpio_write(sensor, GPIO_DATA, 0); // ensures Trig in 0 at the beginning
    usleep(2);

    gpio_write(sensor, GPIO_DATA, 1); // sends Impuls to Trig for 10 [us]
    usleep(10);
    gpio_write(sensor, GPIO_DATA, 0); // turns Trig signal on

    start_wait = time_us();

    while ((gpio_read(sensor, GPIO2_DATA) & 1) == 0) { // waits for Echo to go high (standard after activating Trig)
        if (time_us() - start_wait > ECHO_TIMEOUT_US) { 
            return -1.0;
        }
    }

    echo_start = time_us();

    while ((gpio_read(sensor, GPIO2_DATA) & 1) == 1) { // once Echo goes high, the sound is travelling
        if (time_us() - echo_start > ECHO_TIMEOUT_US) {
            return -1.0;
        }
    }

    echo_end = time_us();
    pulse_width_us = echo_end - echo_start; // travel time of sound

    return pulse_width_us / 58.0; // 58 = 2*1e4 /speed of sound
}

static double ultrasound_median_cm(volatile uint32_t *sensor)
{
    double values[5];
    int count = 0;

    for (int i = 0; i < 5; i++) {
        double d = ultrasound_read_cm(sensor);

        if (d > 0) {
            values[count++] = d;
        }

        usleep(60000); // measures 5 times in intervals of 60ms (300ms to check veracity of measurement in case of positiveness)
    }

    if (count == 0) {
        return -1.0; // nothing measured
    }

    for (int i = 0; i < count - 1; i++) { // takes median to avoid noisy measurement
        for (int j = i + 1; j < count; j++) {
            if (values[j] < values[i]) {
                double tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }
    }

    return values[count / 2];
}

/* ---------------- Scan logic ---------------- */

static int obstacle_at_angle(volatile uint32_t *servo,
                             volatile uint32_t *sensor,
                             int angle_deg)
{
    //printf("Scanning angle %d deg...\n", angle_deg);

    if (servo_hold_angle(servo, angle_deg, SERVO_SETTLE_MS)) { // gives 400 ms (typ. value) to the servo to reach and stabilize at that angle
        return -1; // just means it didn't work
    }

    double d = ultrasound_median_cm(sensor); // measure distance

    if (d < 0) {
        printf("No echo / out of range\n");
        return 0;
    }

    g_last_distance_cm = d;
    printf("Distance: %.2f cm\n", d);

    return d < OBSTACLE_THRESHOLD_CM; // returns boolean: detected or not
}

static int scan_front(volatile uint32_t *servo, volatile uint32_t *sensor) // composed servo+sensor function, scans front with a 30° side span
{
    int angles[] = {60, 90, 120}; 

    for (int i = 0; i < 3; i++) {
        int obs = obstacle_at_angle(servo, sensor, angles[i]);
        if (obs == -1) return -1;
        if (obs == 1) return 1;
    }

    return 0;
}

static int scan_right(volatile uint32_t *servo, volatile uint32_t *sensor) // same, but to the right with just one side-span
{
    int angles[] = {150, 180};

    for (int i = 0; i < 2; i++) {
        int obs = obstacle_at_angle(servo, sensor, angles[i]);
        if (obs == -1) return -1;
        if (obs == 1) return 1;
    }

    return 0;
}

static int scan_left(volatile uint32_t *servo, volatile uint32_t *sensor) // to the left
{
    int angles[] = {0, 30};

    for (int i = 0; i < 2; i++) {
        int obs = obstacle_at_angle(servo, sensor, angles[i]);
        if (obs == -1) return -1;
        if (obs == 1) return 1;
    }

    return 0;
}

static int timed_action_ms(void (*action)(volatile uint32_t *), 
                           volatile uint32_t *motors,
                           int duration_ms) // action is pointer to a function
                           // the function basically starts the function and keeps it active for the given duration, can interrupt early if stop_requested()
{
    int steps = duration_ms / 50;

    action(motors);

    for (int i = 0; i < steps; i++) {
        if (stop_requested()) return 1;
        usleep(50000);
    }

    return 0;
}

/* ---------------- Framework IPC ---------------- */

static void notify_framework(const char *msg)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, LOCAL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "%s\n", msg);
        write(s, buf, len);
    }

    close(s);
}

/* ---------------- Main ---------------- */

int main(void)
{
    int fd;
    void *motor_map, *sensor_map, *servo_map;

    volatile uint32_t *motors;
    volatile uint32_t *sensor;
    volatile uint32_t *servo;

    State state = Z0_IDLE;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    motor_map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, MOTOR_GPIO_BASE);
    sensor_map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, SENSOR_GPIO_BASE);
    servo_map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, SERVO_GPIO_BASE);

    if (motor_map == MAP_FAILED ||
        sensor_map == MAP_FAILED ||
        servo_map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    motors = (volatile uint32_t *)motor_map;
    sensor = (volatile uint32_t *)sensor_map;
    servo  = (volatile uint32_t *)servo_map;

    gpio_write(motors, GPIO_TRI, 0x00000000);   // motors output

    gpio_write(sensor, GPIO_TRI, 0x00000000);   // TRIG output
    gpio_write(sensor, GPIO2_TRI, 0x00000001);  // Sets ECHO as input

    gpio_write(servo, GPIO_TRI, 0x00000000);    // servo output

    motors_coast(motors);

    signal(SIGINT, sigint_handler);

    printf("Robot state machine starting, ports initialized\n");
    printf("Available commands:\n");
    printf("  start\n");
    printf("  stop\n\n");

    while (1) {
        char cmd[32];

        if (state != Z0_IDLE && stop_requested()) {
            printf("STOP requested -> Z0\n");
            motors_brake(motors);
            state = Z0_IDLE;
        }

        switch (state) {
            case Z0_IDLE:
                motors_brake(motors);
                notify_framework("STOPPED");
                printf("Z0_IDLE: waiting for start...\n");

                while (state == Z0_IDLE) {
                    if (interrupted) break;
                    if (get_console_command_nonblocking(cmd, sizeof(cmd))) {
                        if (strcmp(cmd, "start") == 0) {
                            printf("START -> Z1_SCAN_FRONT\n");
                            state = Z1_SCAN_FRONT;
                        }
                    }
                    usleep(100000); // checks console every 100 ms
                }
                break;

            case Z1_SCAN_FRONT: {
                printf("\nZ1_SCAN_FRONT\n");
                int obs = scan_front(servo, sensor); 

                if (obs == -1) { // stop requested 
                    state = Z0_IDLE;
                } else if (obs == 1) {
                    printf("Front blocked -> Z3_BRAKE\n");
                    state = Z3_BRAKE;
                } else {
                    printf("Front free -> Z2_MOVE_FORWARD\n");
                    state = Z2_MOVE_FORWARD;
                }
                break;
            }

            case Z2_MOVE_FORWARD: {
                printf("\nZ2_MOVE_FORWARD\n");
                notify_framework("MOVING");
                motors_forward(motors);

                while (1) {
                    if (stop_requested()) {
                        printf("STOP requested -> Z0\n");
                        state = Z0_IDLE;
                        break;
                    }

                    int obs = scan_front(servo, sensor);

                    if (obs == -1) {
                        state = Z0_IDLE;
                        break;
                    } else if (obs == 1) {
                        printf("Obstacle detected while moving -> Z3_BRAKE\n");
                        state = Z3_BRAKE;
                        break;
                    } else {
                        //printf("Front still free, continuing forward...\n");
                    }
                }
                break;
            }

            case Z3_BRAKE: {
                char obs_msg[64];
                snprintf(obs_msg, sizeof(obs_msg), "OBSTACLE %.2f", g_last_distance_cm);
                notify_framework(obs_msg);
                printf("\nZ3_BRAKE\n");
                if (timed_action_ms(motors_brake, motors, BRAKE_TIME_MS)) { // stop requested
                    state = Z0_IDLE;
                } else {
                    state = Z4_SCAN_RIGHT;
                }
                break;
            }

            case Z4_SCAN_RIGHT: {
                printf("\nZ4_SCAN_RIGHT\n");
                int obs = scan_right(servo, sensor);

                if (obs == -1) {
                    state = Z0_IDLE;
                } else if (obs == 0) {
                    printf("Right free -> Z5_TURN_RIGHT\n");
                    state = Z5_TURN_RIGHT;
                } else {
                    printf("Right blocked -> Z6_SCAN_LEFT\n");
                    state = Z6_SCAN_LEFT;
                }
                break;
            }

            case Z5_TURN_RIGHT:
                printf("\nZ5_TURN_RIGHT\n");
                if (timed_action_ms(motors_turn_right, motors, TURN_TIME_MS)) {
                    state = Z0_IDLE;
                } else {
                    motors_brake(motors);
                    state = Z1_SCAN_FRONT;
                }
                break;

            case Z6_SCAN_LEFT: {
                printf("\nZ6_SCAN_LEFT\n");
                int obs = scan_left(servo, sensor);

                if (obs == -1) {
                    state = Z0_IDLE;
                } else if (obs == 0) {
                    printf("Left free -> Z7_TURN_LEFT\n");
                    state = Z7_TURN_LEFT;
                } else {
                    printf("Left blocked -> Z8_MOVE_BACKWARD\n");
                    state = Z8_MOVE_BACKWARD;
                }
                break;
            }

            case Z7_TURN_LEFT:
                printf("\nZ7_TURN_LEFT\n");
                if (timed_action_ms(motors_turn_left, motors, TURN_TIME_MS)) {
                    state = Z0_IDLE;
                } else {
                    motors_brake(motors);
                    state = Z1_SCAN_FRONT;
                }
                break;

            case Z8_MOVE_BACKWARD:
                printf("\nZ8_MOVE_BACKWARD\n");
                if (timed_action_ms(motors_backward, motors, BACKWARD_TIME_MS)) {
                    state = Z0_IDLE;
                } else {
                    state = Z3_BRAKE;
                }
                break;
        }

        if (interrupted) break;
    }

    printf("Interrupted — braking and exiting\n");
    motors_brake(motors);

    munmap(motor_map, MAP_SIZE);
    munmap(sensor_map, MAP_SIZE);
    munmap(servo_map, MAP_SIZE);
    close(fd);

    return 0;
}