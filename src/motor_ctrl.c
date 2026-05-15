/**
 * @file motor_ctrl.c
 * @brief Motor control implementation via AXI GPIO
 */

#include "motor_ctrl.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAP_SIZE    4096

static int fd = -1;
static void *map_base = NULL;

int motor_init(void) {
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[MOTOR] Failed to open /dev/mem");
        return -1;
    }

    map_base = mmap(NULL, MAP_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd,
                    MOTOR_AXI_BASE_ADDR);

    if (map_base == MAP_FAILED) {
        perror("[MOTOR] mmap failed");
        close(fd);
        fd = -1;
        return -1;
    }

    printf("[MOTOR] Initialized - AXI base: 0x%08X\n", MOTOR_AXI_BASE_ADDR);
    motor_stop_all();
    return 0;
}

void motor_set(uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3) {
    if (map_base == NULL) {
        fprintf(stderr, "[MOTOR] Not initialized\n");
        return;
    }

    uint32_t cmd = ((uint32_t)m3 << 24) |
                   ((uint32_t)m2 << 16) |
                   ((uint32_t)m1 << 8)  |
                   ((uint32_t)m0);

    *((volatile uint32_t *)map_base) = cmd;
}

uint8_t motor_cmd(int direction, int speed) {
    if (speed < 0)   speed = 0;
    if (speed > 100) speed = 100;
    return (uint8_t)((direction ? 0x80 : 0x00) | (speed & 0x7F));
}

void motor_stop_all(void) {
    motor_set(MOTOR_COAST, MOTOR_COAST, MOTOR_COAST, MOTOR_COAST);
    printf("[MOTOR] All motors stopped\n");
}

void motor_brake_all(void) {
    motor_set(MOTOR_BRAKE, MOTOR_BRAKE, MOTOR_BRAKE, MOTOR_BRAKE);
    printf("[MOTOR] All motors braking\n");
}

void motor_cleanup(void) {
    motor_stop_all();
    if (map_base != NULL) {
        munmap(map_base, MAP_SIZE);
        map_base = NULL;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    printf("[MOTOR] Cleanup done\n");
}