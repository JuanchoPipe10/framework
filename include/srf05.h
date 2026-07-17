/**
 * @file srf05.h
 * @brief SRF05 ultrasonic sensor driver via AXI GPIO
 *
 * Trig → 0xA0020000 (axi_gpio_2, output)
 * Echo ← 0xA0010008 (axi_gpio_1, GPIO2 input)
 */
#ifndef SRF05_H
#define SRF05_H

#include <stdint.h>

#define SRF05_TRIG_ADDR  0xA0010000  // axi_gpio_1 GPIO1 - Trig output
#define SRF05_ECHO_ADDR  0xA0010008  // axi_gpio_1 GPIO2 - Echo input

/**
 * @brief Initialize SRF05 sensor
 * @return 0 on success, -1 on error
 */
int srf05_init(void);

/**
 * @brief Read distance from sensor
 * @return Distance in centimeters, -1 on error
 */
float srf05_read_cm(void);

/**
 * @brief Release SRF05 resources
 */
void srf05_cleanup(void);

/**
 * @brief Get the raw mapped base pointer
 * @return Pointer to axi_gpio_1 base (offset 0x00 = TRIG, 0x08 = ECHO), or NULL if not initialized
 */
volatile uint32_t *srf05_get_ptr(void);

#endif // SRF05_H