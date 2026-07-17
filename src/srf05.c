/**
 * @file srf05.c
 * @brief SRF05 ultrasonic sensor implementation via AXI GPIO
 */

#include "srf05.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#define MAP_SIZE  4096
#define TRIG_PULSE_US  10       // 10 microseconds trigger pulse
#define TIMEOUT_US     30000    // 30ms timeout (max range ~5m)

static int fd = -1;
static volatile uint32_t *trig_reg = NULL;
static volatile uint32_t *echo_reg = NULL;

static long get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

int srf05_init(void) {
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[SRF05] Failed to open /dev/mem");
        return -1;
    }

    /* Map axi_gpio_1 base (0xA0010000) - contains both Trig and Echo */
    void *gpio1_map = mmap(NULL, MAP_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd,
                           0xA0010000);
    if (gpio1_map == MAP_FAILED) {
        perror("[SRF05] mmap gpio1 failed");
        close(fd);
        return -1;
    }

    /* Trig is at offset 0x00 (GPIO1 output) */
    trig_reg = (volatile uint32_t *)((uint8_t *)gpio1_map + 0x00);

    /* Echo is at offset 0x08 (GPIO2 input) */
    echo_reg = (volatile uint32_t *)((uint8_t *)gpio1_map + 0x08);

    printf("[SRF05] Initialized - Trig: 0xA0010000, Echo: 0xA0010008\n");
    return 0;
}

float srf05_read_cm(void) {
    if (trig_reg == NULL || echo_reg == NULL) {
        fprintf(stderr, "[SRF05] Not initialized\n");
        return -1.0f;
    }

    /* Send 10us trigger pulse */
    *trig_reg = 1;
    usleep(TRIG_PULSE_US);
    *trig_reg = 0;

    long start, end;
    long deadline = get_time_us() + TIMEOUT_US;

    /* Wait for Echo to go HIGH */
    while ((*echo_reg & 0x1) == 0) {
        if (get_time_us() > deadline) {
            printf("[SRF05] Timeout waiting for echo high\n");
            return -1.0f;
        }
    }
    start = get_time_us();

    /* Wait for Echo to go LOW */
    while ((*echo_reg & 0x1) == 1) {
        if (get_time_us() > deadline) {
            printf("[SRF05] Timeout waiting for echo low\n");
            return -1.0f;
        }
    }
    end = get_time_us();

    /* Calculate distance: time(us) / 58 = cm */
    float duration_us = (float)(end - start);
    float distance_cm = duration_us / 58.0f;

    printf("[SRF05] Distance: %.2f cm\n", distance_cm);
    return distance_cm;
}

volatile uint32_t *srf05_get_ptr(void) {
    return trig_reg;   /* base of gpio1 mapping; echo is at offset 0x08 */
}

void srf05_cleanup(void) {
    if (trig_reg != NULL) {
        munmap((void *)trig_reg, MAP_SIZE);
        trig_reg = NULL;
    }
    if (echo_reg != NULL) {
        munmap((void *)((uint8_t *)echo_reg -
               (SRF05_ECHO_ADDR & (MAP_SIZE - 1))), MAP_SIZE);
        echo_reg = NULL;
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    printf("[SRF05] Cleanup done\n");
}