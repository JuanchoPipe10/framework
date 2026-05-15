/**
 * @file main.c
 * @brief Main entry point for embedded communication framework
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framework_core.h"
#include "motor_ctrl.h"
#include "srf05.h"
#include "servo.h"

/**
 * Callback executed every time a message arrives from another car.
 */
void my_callback(const char *from_ip, const CarMessage *msg) {
    printf("[APP] >>> Message from %s: distance=%.2f speed=%.2f state=%u\n",
           from_ip, msg->x, msg->speed, msg->state);

    if (msg->state == 2) {
        printf("[APP] Other car in error - stopping\n");
        motor_brake_all();
    }
    else if (msg->x < 30.0f) {
        printf("[APP] Car too close (%.2f cm) - braking\n", msg->x);
        motor_brake_all();
    }
    else if (msg->x < 60.0f) {
        printf("[APP] Car nearby (%.2f cm) - slowing down\n", msg->x);
        motor_set(motor_cmd(0, 30), motor_cmd(0, 30),
                  motor_cmd(0, 30), motor_cmd(0, 30));
    }
    else {
        printf("[APP] All clear - full speed\n");
        motor_set(motor_cmd(0, 100), motor_cmd(0, 100),
                  motor_cmd(0, 100), motor_cmd(0, 100));
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device_id> <device_type> <port> [config_file]\n", argv[0]);
        fprintf(stderr, "Example: %s car01 ultra96 60000\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device_id   = argv[1];
    const char *device_type = argv[2];
    uint16_t    port        = (uint16_t)atoi(argv[3]);
    const char *config_file = (argc > 4) ? argv[4] : NULL;

    printf("==================================================\n");
    printf("  Embedded Communication Framework v1.2\n");
    printf("==================================================\n");
    printf("  Device ID:   %s\n", device_id);
    printf("  Device Type: %s\n", device_type);
    printf("  TCP Port:    %d\n", port);
    printf("  Config:      %s\n", config_file ? config_file : "(none - using discovery)");
    printf("==================================================\n\n");

    /* Initialize hardware */
    if (motor_init() < 0)
        fprintf(stderr, "[WARNING] Motors not available\n");

    if (srf05_init() < 0)
        fprintf(stderr, "[WARNING] SRF05 not available\n");

    if (servo_init() < 0)
        fprintf(stderr, "[WARNING] Servo not available\n");

    /* Center servo on startup */
    servo_center();

    /* Initialize framework */
    Framework fw;
    if (framework_init(&fw, device_id, device_type, port, MODE_HYBRID) < 0) {
        motor_cleanup();
        srf05_cleanup();
        servo_cleanup();
        return EXIT_FAILURE;
    }

    if (config_file) {
        if (framework_load_config(&fw, config_file) < 0)
            fprintf(stderr, "[WARNING] Could not load config - relying on discovery\n");
    }

    /* Main loop - read sensor and send to other cars */
    // Note: framework_run is blocking, sensor reading will be
    // integrated in the client_thread in the next step

    framework_set_sensor(&fw, srf05_read_cm);
    framework_set_callback(&fw, my_callback);

    int result = framework_run(&fw);

    /* Cleanup */
    motor_cleanup();
    srf05_cleanup();
    servo_cleanup();

    return result;
}