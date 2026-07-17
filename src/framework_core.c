/**
 * @file framework_core.c
 * @brief Core framework implementation
 */

#include "framework_core.h"
#include "tcp_comm.h"
#include "motor_ctrl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <time.h>

#define LOCAL_SOCK_PATH "/tmp/robot_fw.sock"


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

            uint32_t state;
            if (distance < 0) {
                state = 2;
            } else if (distance < 30.0f) {
                motor_brake_all();
                state = 3;
            } else {
                state = 1;
            }

            CarMessage msg = {
                .id               = 1,
                .x                = distance,
                .y                = 0.0f,
                .speed            = 0.0f,
                .state            = state,
                .timestamp        = (uint32_t)time(NULL),
                .sign_id          = -1,
                .sign_confidence  = 0.0f
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
            
            printf("[SERVER] Received - ID=%u, pos=(%.2f,%.2f), speed=%.2f, state=%u, sign_id=%d, sign_conf=%.2f\n",
                msg.id, msg.x, msg.y, msg.speed, msg.state, msg.sign_id, msg.sign_confidence);

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

static void *local_socket_thread(void *arg)
{
    Framework *fw = (Framework *)arg;

    unlink(LOCAL_SOCK_PATH);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("[LOCAL] socket");
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, LOCAL_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[LOCAL] bind");
        close(srv);
        return NULL;
    }

    if (listen(srv, 5) < 0) {
        perror("[LOCAL] listen");
        close(srv);
        unlink(LOCAL_SOCK_PATH);
        return NULL;
    }

    printf("[LOCAL] Listening on %s\n", LOCAL_SOCK_PATH);

    while (fw->running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        if (select(srv + 1, &fds, NULL, NULL, &tv) <= 0) continue;

        int conn = accept(srv, NULL, NULL);
        if (conn < 0) continue;

        char buf[128];
        ssize_t n = recv(conn, buf, sizeof(buf) - 1, 0);
        close(conn);
        if (n <= 0) continue;
        buf[n] = '\0';

        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';

        CarMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.timestamp = (uint32_t)time(NULL);

        msg.sign_id         = -1;
        msg.sign_confidence = 0.0f;

        if (strncmp(buf, "OBSTACLE", 8) == 0) {
            float dist = 0.0f;
            sscanf(buf + 8, "%f", &dist);
            msg.state = 3;
            msg.x     = dist;
            printf("[LOCAL] OBSTACLE %.2f cm -> broadcast\n", dist);
        } else if (strcmp(buf, "MOVING") == 0) {
            msg.state = 1;
            msg.x     = 999.0f;
            printf("[LOCAL] MOVING -> broadcast\n");
        } else if (strcmp(buf, "STOPPED") == 0) {
            msg.state = 0;
            msg.x     = 0.0f;
            printf("[LOCAL] STOPPED -> broadcast\n");
        } else if (strncmp(buf, "SIGN", 4) == 0) {
            int32_t sign_id = -1;
            float confidence = 0.0f;
            sscanf(buf + 4, "%d %f", &sign_id, &confidence);
            msg.state          = 1;
            msg.x              = 999.0f;
            msg.sign_id        = sign_id;
            msg.sign_confidence = confidence;
            printf("[LOCAL] SIGN id=%d conf=%.2f state=%u x=%.2f -> broadcast\n",
                   msg.sign_id, msg.sign_confidence, msg.state, msg.x);
        } else {
            printf("[LOCAL] Unknown: %s\n", buf);
            continue;
        }

        framework_broadcast(fw, &msg);
    }

    close(srv);
    unlink(LOCAL_SOCK_PATH);
    printf("[LOCAL] Thread stopped\n");
    return NULL;
}

int framework_run(Framework *fw) {
    pthread_t server_tid, client_tid, local_tid;
    int server_started = 0, client_started = 0, local_started = 0;

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

    // Start local IPC socket listener
    if (pthread_create(&local_tid, NULL, local_socket_thread, fw) == 0)
        local_started = 1;

    registry_print(&fw->registry);
    printf("[FRAMEWORK] Running. Press Ctrl+C to exit.\n\n");

    while (fw->running) sleep(1);

    // Shutdown
    printf("\n[FRAMEWORK] Shutting down...\n");
    discovery_stop(&fw->discovery);
    if (server_started) { pthread_cancel(server_tid); pthread_join(server_tid, NULL); }
    if (client_started) { pthread_cancel(client_tid); pthread_join(client_tid, NULL); }
    if (local_started)  { pthread_cancel(local_tid);  pthread_join(local_tid,  NULL); }
    unlink(LOCAL_SOCK_PATH);
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
        Device *dev = &devices[i];
        if (!dev->connected || dev->sock < 0) continue;
        if (tcp_send_car_message(dev->sock, msg) == 0) {
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