/**
 * @file framework_core.c
 * @brief Core framework implementation
 */

#include "framework_core.h"
#include "tcp_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>


// Global framework pointer for signal handler
static Framework *g_framework = NULL;

void framework_signal_handler(int sig) {
    (void)sig;
    if (g_framework) {
        printf("\n[FRAMEWORK] Shutdown signal received\n");
        g_framework->running = 0;
    }
}

int framework_init(Framework *fw, const char *device_id, const char *device_type, uint16_t port, FrameworkMode mode) {
    memset(fw, 0, sizeof(Framework));
    
    registry_init(&fw->registry, device_id);
    strncpy(fw->my_device_type, device_type, MAX_DEVICE_TYPE_LEN - 1);
    fw->mode = mode;
    fw->my_port = port;
    fw->running = 1;
    fw->server_sock = -1;
    
    g_framework = fw;
    signal(SIGINT, framework_signal_handler);
    signal(SIGTERM, framework_signal_handler);
    
    printf("[FRAMEWORK] Initialized: %s on port %d (mode: %s)\n",
           device_id, port,
           mode == MODE_SERVER ? "SERVER" :
           mode == MODE_CLIENT ? "CLIENT" : "HYBRID");
    
    return 0;
}

int framework_load_config(Framework *fw, const char *config_file) {
    return registry_load_from_file(&fw->registry, config_file);
}

// Client thread: connects to all known devices periodically
void* client_thread(void *arg) {
    Framework *fw = (Framework*)arg;
    printf("[CLIENT] Thread started\n");

    while (fw->running) {
        Device *devices;
        int count = registry_get_other_devices(&fw->registry, &devices);

        for (int i = 0; i < count && fw->running; i++) {
            Device *dev = &devices[i];

            // Skip if already connected
            if (dev->connected) continue;

            int sock = tcp_client_connect(dev->ip, dev->tcp_port);
            if (sock < 0) {
                registry_set_connected(&fw->registry, dev->device_id, 0);
                continue;
            }

            registry_set_connected(&fw->registry, dev->device_id, 1);
            dev->sock = sock;
            printf("[CLIENT] Persistent connection established to %s (%s:%d)\n",
                   dev->device_id, dev->ip, dev->tcp_port);
        }

        // Send status to all connected devices
        for (int i = 0; i < count && fw->running; i++) {
            Device *dev = &devices[i];
            if (!dev->connected) continue;

            // Read sensor if available, otherwise use default
            float distance = 999.0f;
            if (fw->read_sensor != NULL) {
                distance = fw->read_sensor();
            }

            CarMessage msg = {
                .id        = 1,
                .x         = distance,
                .y         = 0.0f,
                .speed     = 0.0f,
                .state     = (distance < 0) ? 2 : 1,
                .timestamp = (uint32_t)time(NULL)
            };

            if (tcp_send_car_message(dev->sock, &msg) < 0) {
                // Connection dropped — mark for reconnect
                printf("[CLIENT] Lost connection to %s — will reconnect\n", dev->device_id);
                tcp_close(dev->sock);
                dev->sock = -1;
                registry_set_connected(&fw->registry, dev->device_id, 0);
                continue;
            }

            CarMessage response;
            if (tcp_receive_car_message(dev->sock, &response) == 0) {
                registry_update_contact(&fw->registry, dev->device_id);
            }
        }

        sleep(2);
    }

    // Cleanup — close all open connections
    Device *devices;
    int count = registry_get_other_devices(&fw->registry, &devices);
    for (int i = 0; i < count; i++) {
        if (devices[i].connected) {
            tcp_close(devices[i].sock);
            devices[i].connected = 0;
        }
    }

    printf("[CLIENT] Thread stopped\n");
    return NULL;
}

// Server thread: accepts incoming connections
void* server_thread(void *arg) {
    Framework *fw = (Framework*)arg;
    
    printf("[SERVER] Thread started\n");
    
    while (fw->running) {
        printf("[SERVER] Waiting for connections on port %d...\n", fw->my_port);
        
        int client_sock = tcp_server_accept(fw->server_sock);
        if (client_sock < 0) {
            if (fw->running) sleep(1);
            continue;
        }
        
        // Handle client communication
        CarMessage msg;
        while (fw->running) {
            if (tcp_receive_car_message(client_sock, &msg) < 0) {
                break;
            }
            
            printf("[SERVER] Received - ID=%u, pos=(%.2f,%.2f), speed=%.2f, state=%u\n",
                msg.id, msg.x, msg.y, msg.speed, msg.state);

            // Fire callback if registered
            if (fw->on_message) {
                char client_ip[INET_ADDRSTRLEN];
                // Get peer IP from socket
                struct sockaddr_in peer;
                socklen_t peer_len = sizeof(peer);
                getpeername(client_sock, (struct sockaddr*)&peer, &peer_len);
                inet_ntop(AF_INET, &peer.sin_addr, client_ip, sizeof(client_ip));
                fw->on_message(client_ip, &msg);
            }
            
            // Send acknowledgment
            CarMessage ack = {
                .id = 999,
                .x = 0,
                .y = 0,
                .speed = 0,
                .state = 1,
                .timestamp = (uint32_t)time(NULL)
            };
            
            tcp_send_car_message(client_sock, &ack);
        }
        
        tcp_close(client_sock);
    }
    
    printf("[SERVER] Thread stopped\n");
    return NULL;
}

int framework_run(Framework *fw) {
    pthread_t server_tid, client_tid;
    int server_started = 0, client_started = 0;

    // Start UDP discovery FIRST
    if (discovery_init(&fw->discovery,
                       fw->registry.my_device_id,
                       fw->my_device_type,
                       fw->my_port,
                       &fw->registry) < 0) {
        fprintf(stderr, "[WARNING] Discovery init failed - continuing without it\n");
    } else {
        discovery_start_listener(&fw->discovery);
        sleep(2);
    }

    // Start TCP server
    if (fw->mode == MODE_SERVER || fw->mode == MODE_HYBRID) {
        fw->server_sock = tcp_server_init(fw->my_port);
        if (fw->server_sock < 0) {
            fprintf(stderr, "[ERROR] Failed to start TCP server\n");
            return -1;
        }
        pthread_create(&server_tid, NULL, server_thread, fw);
        server_started = 1;
    }

    // Start TCP client
    if (fw->mode == MODE_CLIENT || fw->mode == MODE_HYBRID) {
        pthread_create(&client_tid, NULL, client_thread, fw);
        client_started = 1;
    }

    registry_print(&fw->registry);
    printf("[FRAMEWORK] Running. Press Ctrl+C to exit.\n\n");

    while (fw->running) sleep(1);

    // Shutdown
    printf("\n[FRAMEWORK] Shutting down...\n");
    discovery_stop(&fw->discovery);
    if (server_started) { pthread_cancel(server_tid); pthread_join(server_tid, NULL); }
    if (client_started) { pthread_cancel(client_tid); pthread_join(client_tid, NULL); }
    if (fw->server_sock >= 0) tcp_close(fw->server_sock);
    printf("[FRAMEWORK] Stopped\n");
    return 0;
}

void framework_stop(Framework *fw) {
    fw->running = 0;
}

int framework_send_to_device(Framework *fw, const char *device_id, const CarMessage *msg) {
    Device *dev = registry_get_device(&fw->registry, device_id);
    if (!dev) {
        fprintf(stderr, "[ERROR] Device %s not found\n", device_id);
        return -1;
    }
    
    int sock = tcp_client_connect(dev->ip, dev->tcp_port);
    if (sock < 0) {
        return -1;
    }
    
    int result = tcp_send_car_message(sock, msg);
    tcp_close(sock);
    
    return result;
}

int framework_broadcast(Framework *fw, const CarMessage *msg) {
    Device *devices;
    int count = registry_get_other_devices(&fw->registry, &devices);
    int sent = 0;
    
    for (int i = 0; i < count; i++) {
        if (framework_send_to_device(fw, devices[i].device_id, msg) == 0) {
            sent++;
        }
    }
    
    printf("[FRAMEWORK] Broadcast sent to %d/%d devices\n", sent, count);
    return sent;
    
}
void framework_set_callback(Framework *fw, MessageCallback cb) {
    fw->on_message = cb;
    printf("[FRAMEWORK] Message callback %s\n", cb ? "registered" : "cleared");
}
void framework_set_sensor(Framework *fw, SensorReadCallback cb) {
    fw->read_sensor = cb;
    printf("[FRAMEWORK] Sensor callback %s\n", cb ? "registered" : "cleared");
}