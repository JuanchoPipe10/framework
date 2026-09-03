/**
 * @file device_registry.h
 * @brief Device registry management
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#define MAX_DEVICES 16
#define MAX_DEVICE_ID_LEN 32
#define MAX_DEVICE_TYPE_LEN 16
#define MAX_IP_LEN 16

typedef struct {
    char device_id[MAX_DEVICE_ID_LEN];
    char device_type[MAX_DEVICE_TYPE_LEN];
    char ip[MAX_IP_LEN];
    uint16_t tcp_port;
    int connected;
    int sock;                   // Persistent socket (-1 if not connected)
    time_t last_contact;
} Device;

typedef struct {
    Device devices[MAX_DEVICES];
    int count;
    char my_device_id[MAX_DEVICE_ID_LEN];
    pthread_mutex_t lock;
} DeviceRegistry;

/**
 * @brief Initialize device registry
 */
int registry_init(DeviceRegistry *reg, const char *my_device_id);

/**
 * @brief Load devices from configuration file
 */
int registry_load_from_file(DeviceRegistry *reg, const char *filename);

/**
 * @brief Add device manually
 */
int registry_add_device(DeviceRegistry *reg, const char *id, const char *type, 
                        const char *ip, uint16_t port);

/**
 * @brief Get device by ID
 */
Device* registry_get_device(DeviceRegistry *reg, const char *device_id);

/**
 * @brief Get all devices except myself
 */
int registry_get_other_devices(DeviceRegistry *reg, Device **devices);

/**
 * @brief Mark device as connected/disconnected
 */
void registry_set_connected(DeviceRegistry *reg, const char *device_id, int connected);

/**
 * @brief Update last contact time
 */
void registry_update_contact(DeviceRegistry *reg, const char *device_id);

/**
 * @brief Print registry status
 */
void registry_print(DeviceRegistry *reg);

#endif // DEVICE_REGISTRY_H