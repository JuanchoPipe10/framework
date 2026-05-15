/**
 * @file servo.c
 * @brief Servo motor implementation via AXI GPIO
 *
 * The FPGA generates the PWM signal for the servo.
 * We write the desired angle (0-180) directly to the register
 * and the FPGA handles the PWM timing internally.
 */

#include "servo.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAP_SIZE  4096

static int fd = -1;
static volatile uint32_t *servo_reg = NULL;

int servo_init(void) {
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[SERVO] Failed to open /dev/mem");
        return -1;
    }

    void *map = mmap(NULL, MAP_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd,
                     SERVO_ADDR);
    if (map == MAP_FAILED) {
        perror("[SERVO] mmap failed");
        close(fd);
        return -1;
    }

    servo_reg = (volatile uint32_t *)map;

    printf("[SERVO] Initialized - AXI base: 0x%08X\n", SERVO_ADDR);

    /* Center servo on startup */
    servo_center();
    return 0;
}

void servo_set_angle(int angle) {
    if (servo_reg == NULL) {
        fprintf(stderr, "[SERVO] Not initialized\n");
        return;
    }

    /* Clamp angle to valid range */
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;

    /* Write angle directly to register
     * The FPGA IP converts this to the appropriate PWM signal:
     * 0   degrees → ~1ms  pulse width
     * 90  degrees → ~1.5ms pulse width
     * 180 degrees → ~2ms  pulse width
     */
    *servo_reg = (uint32_t)angle;

    printf("[SERVO] Angle set to %d degrees\n", angle);
}

void servo_center(void) {
    servo_set_angle(90);
}

void servo_cleanup(void) {
    servo_center();

    if (servo_reg != NULL) {
        munmap((void *)servo_reg, MAP_SIZE);
        servo_reg = NULL;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    printf("[SERVO] Cleanup done\n");
}