/**
 * @file test_srf05.c
 * @brief Quick test for SRF05 sensor
 */
#include <stdio.h>
#include <unistd.h>
#include "srf05.h"

int main(void) {
    printf("SRF05 Test\n");

    if (srf05_init() < 0) {
        printf("Failed to initialize SRF05\n");
        return 1;
    }

    printf("Reading distance 10 times...\n");
    for (int i = 0; i < 10; i++) {
        float dist = srf05_read_cm();
        if (dist < 0)
            printf("Reading %d: ERROR\n", i+1);
        else
            printf("Reading %d: %.2f cm\n", i+1, dist);
        sleep(1);
    }

    srf05_cleanup();
    return 0;
}