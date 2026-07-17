/**
 * @file servo.h
 * @brief Servo motor driver via AXI GPIO
 *
 * Servo → 0xA0010000 (axi_gpio_1, GPIO1 output)
 *
 * The servo is controlled by a PWM signal:
 * - 1ms pulse  = 0 degrees
 * - 1.5ms pulse = 90 degrees (center)
 * - 2ms pulse  = 180 degrees
 * PWM period = 20ms (50Hz)
 */
#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#define SERVO_ADDR  0xA0020000  // axi_gpio_2 - Servo output

/**
 * @brief Initialize servo controller
 * @return 0 on success, -1 on error
 */
int servo_init(void);

/**
 * @brief Set servo angle
 * @param angle Angle in degrees (0-180)
 */
void servo_set_angle(int angle);

/**
 * @brief Center the servo (90 degrees)
 */
void servo_center(void);

/**
 * @brief Release servo resources
 */
void servo_cleanup(void);

/**
 * @brief Get the raw mapped register pointer
 * @return Pointer to the AXI GPIO base (0xA0020000), or NULL if not initialized
 */
volatile uint32_t *servo_get_ptr(void);

#endif // SERVO_H