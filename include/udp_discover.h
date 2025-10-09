#ifndef UDP_DISCOVER_H
#define UDP_DISCOVER_H

#include <stdint.h>
#include <time.h>
#include "messages.h"

// Configuración por defecto
#define UDP_BROADCAST_PORT 50000
#define UDP_BUFFER_SIZE 1024
#define DISCOVERY_INTERVAL 5  // segundos entre broadcasts

// Estructura para información de dispositivo descubierto
typedef struct {
    char ip[16];
    uint16_t tcp_port;
    char device_id[32];
    char device_type[16];
    time_t last_seen;
} DiscoveredDevice;

// Funciones principales
int udp_discover_init(const char *device_id, const char *device_type, uint16_t tcp_port);
int udp_discover_send(int sock);
int udp_discover_listen(int sock, DiscoveredDevice *device);
void udp_discover_close(int sock);

#endif // UDP_DISCOVER_H