#include "udp_discover.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

// Variables globales para este módulo
static char g_device_id[32] = {0};
static char g_device_type[16] = {0};
static uint16_t g_tcp_port = 0;

int udp_discover_init(const char *device_id, const char *device_type, uint16_t tcp_port) {
    // Guardar información del dispositivo
    strncpy(g_device_id, device_id, sizeof(g_device_id) - 1);
    strncpy(g_device_type, device_type, sizeof(g_device_type) - 1);
    g_tcp_port = tcp_port;

    // Crear socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Error creando socket UDP");
        return -1;
    }

    // Habilitar broadcast
    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, 
                   sizeof(broadcast_enable)) < 0) {
        perror("Error habilitando broadcast");
        close(sock);
        return -1;
    }

    // Configurar para reutilizar dirección (importante para recibir)
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Error configurando SO_REUSEADDR");
        close(sock);
        return -1;
    }

    // Bind para poder recibir mensajes
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_BROADCAST_PORT);

    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("Error en bind UDP");
        close(sock);
        return -1;
    }

    printf("✓ UDP Discovery inicializado: %s (%s) en puerto %d\n", 
           g_device_id, g_device_type, g_tcp_port);

    return sock;
}

int udp_discover_send(int sock) {
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    broadcast_addr.sin_port = htons(UDP_BROADCAST_PORT);

    // Formato: "HELLO;ID=car03;TYPE=ultra96;PORT=60000"
    char message[UDP_BUFFER_SIZE];
    snprintf(message, sizeof(message), "HELLO;ID=%s;TYPE=%s;PORT=%d",
             g_device_id, g_device_type, g_tcp_port);

    ssize_t sent = sendto(sock, message, strlen(message), 0,
                          (struct sockaddr*)&broadcast_addr, 
                          sizeof(broadcast_addr));

    if (sent < 0) {
        perror("Error enviando broadcast");
        return -1;
    }

    printf("→ Broadcast enviado: %s\n", message);
    return 0;
}

int udp_discover_listen(int sock, DiscoveredDevice *device) {
    char buffer[UDP_BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    // Recibir mensaje
    ssize_t received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                (struct sockaddr*)&sender_addr, &addr_len);

    if (received < 0) {
        return -1;  // Timeout o error (normal si no hay mensajes)
    }

    buffer[received] = '\0';
    
    // Obtener IP del emisor
    char sender_ip[16];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));

    printf("← Mensaje recibido de %s: %s\n", sender_ip, buffer);

    // Parsear el mensaje: "HELLO;ID=car03;TYPE=ultra96;PORT=60000"
    if (strncmp(buffer, "HELLO;", 6) == 0) {
        char *id_start = strstr(buffer, "ID=");
        char *type_start = strstr(buffer, "TYPE=");
        char *port_start = strstr(buffer, "PORT=");

        if (id_start && type_start && port_start) {
            // Extraer ID
            id_start += 3;
            char *id_end = strchr(id_start, ';');
            if (id_end) {
                size_t id_len = id_end - id_start;
                strncpy(device->device_id, id_start, 
                        id_len < 32 ? id_len : 31);
                device->device_id[id_len < 32 ? id_len : 31] = '\0';
            }

            // Extraer TYPE
            type_start += 5;
            char *type_end = strchr(type_start, ';');
            if (type_end) {
                size_t type_len = type_end - type_start;
                strncpy(device->device_type, type_start,
                        type_len < 16 ? type_len : 15);
                device->device_type[type_len < 16 ? type_len : 15] = '\0';
            }

            // Extraer PORT
            port_start += 5;
            device->tcp_port = (uint16_t)atoi(port_start);

            // Guardar IP y timestamp
            strncpy(device->ip, sender_ip, sizeof(device->ip) - 1);
            device->last_seen = time(NULL);

            // RESPONDER al emisor (solo si no somos nosotros mismos)
            if (strcmp(device->device_id, g_device_id) != 0) {
                char response[UDP_BUFFER_SIZE];
                snprintf(response, sizeof(response), 
                         "ACK;ID=%s;TYPE=%s;PORT=%d",
                         g_device_id, g_device_type, g_tcp_port);

                sendto(sock, response, strlen(response), 0,
                       (struct sockaddr*)&sender_addr, addr_len);

                printf("→ Respuesta enviada a %s: %s\n", sender_ip, response);
            }

            return 0; // Dispositivo descubierto correctamente
        }
    }

    return -1; // Mensaje inválido
}

void udp_discover_close(int sock) {
    close(sock);
    printf("✓ Socket UDP cerrado\n");
}