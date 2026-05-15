/**
 * @file motor_ctrl.h
 * @brief Motor control via AXI GPIO memory-mapped interface
 *
 * Controls 4 motors through the FPGA fabric.
 * Base address: 0xA0000000
 *
 * Command format per motor (8 bits):
 *   Bit 7: direction (0=forward, 1=backward)
 *   Bits 6-0: speed (0-100%)
 *   0xFF = brake
 *   0x00 = coast
 */
#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include <stdint.h>

#define MOTOR_AXI_BASE_ADDR  0xA0000000
#define MOTOR_COAST          0x00
#define MOTOR_BRAKE          0xFF

/**
 * @brief Initialize motor controller (open /dev/mem and mmap)
 * @return 0 on success, -1 on error
 */
int motor_init(void);

/**
 * @brief Set all 4 motors at once
 * @param m0 Command for motor 0
 * @param m1 Command for motor 1
 * @param m2 Command for motor 2
 * @param m3 Command for motor 3
 */
void motor_set(uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3);

/**
 * @brief Build a motor command from direction and speed
 * @param direction 0=forward, 1=backward
 * @param speed 0-100 percent
 * @return 8-bit command value
 */
uint8_t motor_cmd(int direction, int speed);

/**
 * @brief Stop all motors (coast)
 */
void motor_stop_all(void);

/**
 * @brief Brake all motors
 */
void motor_brake_all(void);

/**
 * @brief Release motor controller resources
 */
void motor_cleanup(void);

#endif // MOTOR_CTRL_H