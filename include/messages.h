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
 * @brief Message types for device communication
 */
typedef enum {
    MSG_TYPE_DISCOVERY = 1,
    MSG_TYPE_CAR_STATUS = 2,
    MSG_TYPE_SENSOR_DATA = 3,
    MSG_TYPE_COMMAND = 4
} MessageType;

/**
 * @brief Structure for vehicle/device status messages
 */
typedef struct {
    uint32_t id;          // Unique device identifier
    float x;              // Position X coordinate
    float y;              // Position Y coordinate
    float speed;          // Current speed
    uint8_t state;        // Device state (0=stopped, 1=moving, 2=error)
    uint32_t timestamp;   // Unix timestamp
} CarMessage;

/**
 * @brief Discovery response structure
 */
typedef struct {
    char device_id[32];       // Device identifier (e.g., "car03")
    char device_type[16];     // Device type (e.g., "ultra96", "pc")
    uint16_t tcp_port;        // TCP port for direct communication
    uint32_t capabilities;    // Capability flags (reserved for future use)
} DiscoveryResponse;

#endif // MESSAGES_H