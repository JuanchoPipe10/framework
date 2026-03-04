/**
 * @file main.c
 * @brief Main entry point for embedded communication framework
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framework_core.h"

void my_callback(const char *from_ip, const CarMessage *msg) {
    printf("[APP] >>> Message from %s: pos=(%.2f,%.2f) speed=%.2f state=%u\n",
           from_ip, msg->x, msg->y, msg->speed, msg->state);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device_id> <device_type> <port> [config_file]\n", argv[0]);
        fprintf(stderr, "Example: %s car03 ultra96 60000\n", argv[0]);
        fprintf(stderr, "Example: %s pc01  pc      60001 config/devices.conf\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device_id   = argv[1];
    const char *device_type = argv[2];
    uint16_t    port        = (uint16_t)atoi(argv[3]);
    const char *config_file = (argc > 4) ? argv[4] : NULL;

    printf("==================================================\n");
    printf("  Embedded Communication Framework v1.1\n");
    printf("==================================================\n");
    printf("  Device ID:   %s\n", device_id);
    printf("  Device Type: %s\n", device_type);
    printf("  TCP Port:    %d\n", port);
    printf("  Config:      %s\n", config_file ? config_file : "(none - using discovery)");
    printf("==================================================\n\n");

    Framework fw;

    if (framework_init(&fw, device_id, device_type, port, MODE_HYBRID) < 0)
        return EXIT_FAILURE;

    if (config_file) {
        if (framework_load_config(&fw, config_file) < 0)
            fprintf(stderr, "[WARNING] Could not load config - relying on discovery\n");
    }
    framework_set_callback(&fw, my_callback);
    return framework_run(&fw);
}
