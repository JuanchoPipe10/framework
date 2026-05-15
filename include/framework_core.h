/**
 * @file framework_core.h
 * @brief Core framework functionality
 */
#ifndef FRAMEWORK_CORE_H
#define FRAMEWORK_CORE_H

#include "device_registry.h"
#include "messages.h"
#include "udp_discovery.h"

typedef enum {
    MODE_SERVER,
    MODE_CLIENT,
    MODE_HYBRID
} FrameworkMode;

// Callback type: called every time a message is received
typedef void (*MessageCallback)(const char *from_ip, const CarMessage *msg);

// Sensor callback type: returns current distance in cm
typedef float (*SensorReadCallback)(void);

typedef struct {
    DeviceRegistry     registry;
    DiscoveryContext   discovery;
    FrameworkMode      mode;
    uint16_t           my_port;
    char               my_device_type[MAX_DEVICE_TYPE_LEN];
    int                server_sock;
    volatile int       running;
    MessageCallback    on_message;    // NULL if not set
    SensorReadCallback read_sensor;   // NULL if not set
} Framework;

int framework_init(Framework *fw, const char *device_id,
                   const char *device_type, uint16_t port, FrameworkMode mode);
int framework_load_config(Framework *fw, const char *config_file);
int framework_run(Framework *fw);
void framework_stop(Framework *fw);
int framework_send_to_device(Framework *fw, const char *device_id, const CarMessage *msg);
int framework_broadcast(Framework *fw, const CarMessage *msg);

/**
 * @brief Register a callback function for incoming messages
 */
void framework_set_callback(Framework *fw, MessageCallback cb);

/**
 * @brief Register a sensor reading callback
 * Called periodically to get current distance and send to other cars
 */
void framework_set_sensor(Framework *fw, SensorReadCallback cb);

#endif // FRAMEWORK_CORE_H