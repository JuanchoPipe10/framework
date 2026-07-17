/**
 * @file main.c
 * @brief Communication framework entry point
 *
 * Usage: ./framework <device_id> <device_type> <port> [config_file]
 * Example: ./framework car01 ultra96 60000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "framework_core.h"

static Framework g_fw;

static void my_callback(const char *from_ip, const CarMessage *msg)
{
    if (msg->sign_id >= 0) {
        printf("[NET] From %s: SIGN detected id=%d confidence=%.2f\n",
               from_ip, msg->sign_id, msg->sign_confidence);
    } else {
        printf("[NET] From %s: dist=%.2f speed=%.2f state=%u\n",
               from_ip, msg->x, msg->speed, msg->state);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device_id> <device_type> <port> [config_file]\n", argv[0]);
        fprintf(stderr, "Example: %s car01 ultra96 60000\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device_id   = argv[1];
    const char *device_type = argv[2];
    uint16_t    port        = (uint16_t)atoi(argv[3]);
    const char *config_file = argc > 4 ? argv[4] : NULL;

    printf("[MAIN] Starting as %s (type: %s, port: %d)\n", device_id, device_type, port);

    if (framework_init(&g_fw, device_id, device_type, port, MODE_HYBRID) < 0) {
        fprintf(stderr, "[MAIN] Framework init failed\n");
        return EXIT_FAILURE;
    }

    if (config_file) {
        if (framework_load_config(&g_fw, config_file) < 0)
            fprintf(stderr, "[MAIN] Could not load config — relying on discovery\n");
    }

    framework_set_callback(&g_fw, my_callback);

    framework_run(&g_fw);   /* blocks until signal */

    return 0;
}
