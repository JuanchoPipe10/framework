/**
 * @file udp_discovery.h
 * @brief UDP-based automatic device discovery
 *
 * When a device starts up, it broadcasts an announcement to the network.
 * All other devices listening will receive it, register the new device,
 * and send back a unicast response so the newcomer knows about them too.
 */
#ifndef UDP_DISCOVERY_H
#define UDP_DISCOVERY_H

#include <stdint.h>
#include "device_registry.h"

#define DISCOVERY_PORT        55000       // UDP port used for discovery broadcasts
#define DISCOVERY_BROADCAST   "192.168.50.255"
#define DISCOVERY_MAGIC       0xCAFE1234  // Magic number to identify valid packets
#define DISCOVERY_INTERVAL    15          // Seconds between re-announcements

/**
 * @brief Types of discovery packets
 */
typedef enum {
    DISC_ANNOUNCE  = 1,   // "I just joined the network"
    DISC_RESPONSE  = 2,   // "I heard you, here I am"
    DISC_GOODBYE   = 3    // "I am shutting down"
} DiscoveryPacketType;

/**
 * @brief Discovery packet sent/received over UDP
 */
typedef struct {
    uint32_t magic;                     // Must equal DISCOVERY_MAGIC
    DiscoveryPacketType type;           // Packet type
    char device_id[MAX_DEVICE_ID_LEN];  // e.g. "car03"
    char device_type[MAX_DEVICE_TYPE_LEN]; // e.g. "ultra96"
    uint16_t tcp_port;                  // TCP port this device listens on
    uint32_t capabilities;              // Reserved for future use
} DiscoveryPacket;

/**
 * @brief Discovery module context
 */
typedef struct {
    int     udp_sock;                       // Socket for sending broadcasts
    int     listen_sock;                    // Socket for receiving broadcasts
    char    my_device_id[MAX_DEVICE_ID_LEN];
    char    my_device_type[MAX_DEVICE_TYPE_LEN];
    uint16_t my_tcp_port;
    DeviceRegistry *registry;              // Shared registry to update
    volatile int running;
} DiscoveryContext;

/**
 * @brief Initialize discovery module
 * @param ctx        Discovery context to initialize
 * @param device_id  This device's ID
 * @param device_type This device's type (e.g. "ultra96", "pc")
 * @param tcp_port   TCP port this device uses for data communication
 * @param registry   Pointer to the shared device registry
 * @return 0 on success, -1 on error
 */
int discovery_init(DiscoveryContext *ctx,
                   const char *device_id,
                   const char *device_type,
                   uint16_t tcp_port,
                   DeviceRegistry *registry);

/**
 * @brief Broadcast an announcement to the network
 * Called once at startup and then periodically.
 * @return 0 on success, -1 on error
 */
int discovery_announce(DiscoveryContext *ctx);

/**
 * @brief Send a goodbye broadcast before shutting down
 */
int discovery_goodbye(DiscoveryContext *ctx);

/**
 * @brief Start the background listener thread
 * Listens for announcements from other devices and updates the registry.
 * @return 0 on success, -1 on error
 */
int discovery_start_listener(DiscoveryContext *ctx);

/**
 * @brief Stop the listener thread and release resources
 */
void discovery_stop(DiscoveryContext *ctx);

#endif // UDP_DISCOVERY_H