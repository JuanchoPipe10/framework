/**
 * @file messages.h
 * @brief Data structures for inter-device communication
 * 
 * Defines common message formats used across the distributed
 * embedded communication framework.
 */

#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

/**
 * @brief Distinguishes status telemetry from remote control commands
 *
 * Placed right after `state` in CarMessage so it fills existing padding
 * instead of growing the struct (sizeof(CarMessage) stays 32 bytes).
 */
typedef enum {
    MSG_STATUS    = 0,   // Regular status/telemetry message (existing behavior)
    MSG_CMD_START = 1,   // Remote command: start
    MSG_CMD_STOP  = 2    // Remote command: stop
} CarMsgType;

/**
 * @brief Structure for vehicle/device status messages
 */
typedef struct {
    uint32_t id;          // Unique device identifier
    float x;              // Position X coordinate
    float y;              // Position Y coordinate
    float speed;          // Current speed
    uint8_t state;        // Device state (0=stopped, 1=moving, 2=error)
    uint8_t msg_type;     // CarMsgType: MSG_STATUS or a remote command
    uint32_t timestamp;   // Unix timestamp
    int32_t sign_id;          // Traffic sign ID: -1=none, 0-43=sign category
    float sign_confidence;    // Detection confidence [0.0, 1.0]
} CarMessage;

#endif // MESSAGES_H