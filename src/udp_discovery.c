/**
 * @file udp_discovery.c
 * @brief UDP-based automatic device discovery implementation
 */
#include <net/if.h>
#include <sys/ioctl.h>
#include "udp_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * Build a discovery packet from this device's context.
 */
static void build_packet(const DiscoveryContext *ctx,
                         DiscoveryPacketType type,
                         DiscoveryPacket *pkt)
{
    memset(pkt, 0, sizeof(DiscoveryPacket));
    pkt->magic        = htonl(DISCOVERY_MAGIC);
    pkt->type         = htonl((uint32_t)type);
    pkt->tcp_port     = htons(ctx->my_tcp_port);
    pkt->capabilities = 0;
    strncpy(pkt->device_id,   ctx->my_device_id,   MAX_DEVICE_ID_LEN   - 1);
    strncpy(pkt->device_type, ctx->my_device_type,  MAX_DEVICE_TYPE_LEN - 1);
}

/**
 * Send a unicast response directly to the IP that announced itself.
 */
static void send_response(DiscoveryContext *ctx, const char *dest_ip)
{
    DiscoveryPacket pkt;
    build_packet(ctx, DISC_RESPONSE, &pkt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(DISCOVERY_PORT);
    inet_pton(AF_INET, dest_ip, &addr.sin_addr);

    sendto(ctx->udp_sock, &pkt, sizeof(pkt), 0,
           (struct sockaddr *)&addr, sizeof(addr));

    printf("[DISCOVERY] Sent response to %s\n", dest_ip);
}

/**
 * Process a received discovery packet: update registry and optionally reply.
 */
static void handle_packet(DiscoveryContext *ctx,
                           const DiscoveryPacket *pkt,
                           const char *sender_ip)
{
    /* Validate magic number */
    if (ntohl(pkt->magic) != DISCOVERY_MAGIC) {
        return;
    }

    DiscoveryPacketType type = (DiscoveryPacketType)ntohl((uint32_t)pkt->type);
    uint16_t tcp_port        = ntohs(pkt->tcp_port);

    /* Ignore our own packets */
    if (strcmp(pkt->device_id, ctx->my_device_id) == 0) {
        return;
    }

    switch (type) {

        case DISC_ANNOUNCE:
            printf("[DISCOVERY] Announcement from %s (%s) at %s:%d\n",
                   pkt->device_id, pkt->device_type, sender_ip, tcp_port);

            /* Add to registry if not already known */
            if (!registry_get_device(ctx->registry, pkt->device_id)) {
                registry_add_device(ctx->registry,
                                    pkt->device_id,
                                    pkt->device_type,
                                    sender_ip,
                                    tcp_port);
                printf("[DISCOVERY] Registered new device: %s\n", pkt->device_id);
            }

            /* Reply so the newcomer learns about us */
            send_response(ctx, sender_ip);
            break;

        case DISC_RESPONSE:
            printf("[DISCOVERY] Response from %s (%s) at %s:%d\n",
                   pkt->device_id, pkt->device_type, sender_ip, tcp_port);

            if (!registry_get_device(ctx->registry, pkt->device_id)) {
                registry_add_device(ctx->registry,
                                    pkt->device_id,
                                    pkt->device_type,
                                    sender_ip,
                                    tcp_port);
                printf("[DISCOVERY] Registered new device: %s\n", pkt->device_id);
            }
            break;

        case DISC_GOODBYE:
            printf("[DISCOVERY] Goodbye from %s — marking as disconnected\n",
                   pkt->device_id);
            registry_set_connected(ctx->registry, pkt->device_id, 0);
            break;

        default:
            printf("[DISCOVERY] Unknown packet type from %s, ignoring\n", sender_ip);
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Listener thread                                                     */
/* ------------------------------------------------------------------ */

static void *listener_thread(void *arg)
{
    DiscoveryContext *ctx = (DiscoveryContext *)arg;
    
    printf("[DISCOVERY] Listener thread started on UDP port %d\n", DISCOVERY_PORT);

    /* Send initial announcement */
    discovery_announce(ctx);
    /* Periodic re-announcement timer */
    int ticks = 0;

    while (ctx->running) {

        /* Use select() with a 1-second timeout so we can check running flag */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctx->listen_sock, &fds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(ctx->listen_sock + 1, &fds, NULL, NULL, &tv);

        if (ready > 0 && FD_ISSET(ctx->listen_sock, &fds)) {

            DiscoveryPacket pkt;
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);

            ssize_t n = recvfrom(ctx->listen_sock, &pkt, sizeof(pkt), 0,
                                 (struct sockaddr *)&sender, &sender_len);

            if (n == (ssize_t)sizeof(pkt)) {
                char sender_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof(sender_ip));
                handle_packet(ctx, &pkt, sender_ip);
            }
        }

        /* Re-announce every DISCOVERY_INTERVAL seconds */
        ticks++;
        if (ticks >= DISCOVERY_INTERVAL) {
            ticks = 0;
            discovery_announce(ctx);
        }
    }

    printf("[DISCOVERY] Listener thread stopped\n");
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Interface detection                                                 */
/* ------------------------------------------------------------------ */

static void detect_iface(char *iface_out, size_t iface_len,
                          char *bcast_out,  size_t bcast_len)
{
    const char *candidates[] = { "wlan0", "p2p0" };
    int tmp = socket(AF_INET, SOCK_DGRAM, 0);
    if (tmp < 0) {
        strncpy(iface_out, "wlan0", iface_len - 1);
        iface_out[iface_len - 1] = '\0';
        strncpy(bcast_out, DISCOVERY_BROADCAST, bcast_len - 1);
        bcast_out[bcast_len - 1] = '\0';
        return;
    }
    for (int i = 0; i < 2; i++) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, candidates[i], IFNAMSIZ - 1);
        if (ioctl(tmp, SIOCGIFADDR, &ifr) != 0)
            continue;

        strncpy(iface_out, candidates[i], iface_len - 1);
        iface_out[iface_len - 1] = '\0';

        /* Try to obtain the broadcast address for this interface */
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, candidates[i], IFNAMSIZ - 1);
        if (ioctl(tmp, SIOCGIFBRDADDR, &ifr) == 0) {
            struct sockaddr_in *bcast = (struct sockaddr_in *)&ifr.ifr_broadaddr;
            inet_ntop(AF_INET, &bcast->sin_addr, bcast_out, (socklen_t)(bcast_len - 1));
            bcast_out[bcast_len - 1] = '\0';
        } else {
            strncpy(bcast_out, DISCOVERY_BROADCAST, bcast_len - 1);
            bcast_out[bcast_len - 1] = '\0';
        }

        close(tmp);
        printf("[DISCOVERY] Detected interface: %s, broadcast: %s\n", iface_out, bcast_out);
        return;
    }
    close(tmp);
    strncpy(iface_out, "wlan0", iface_len - 1);
    iface_out[iface_len - 1] = '\0';
    strncpy(bcast_out, DISCOVERY_BROADCAST, bcast_len - 1);
    bcast_out[bcast_len - 1] = '\0';
    printf("[DISCOVERY] No active interface found - falling back to wlan0/%s\n", bcast_out);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

int discovery_init(DiscoveryContext *ctx,
                   const char *device_id,
                   const char *device_type,
                   uint16_t tcp_port,
                   DeviceRegistry *registry)
{
    memset(ctx, 0, sizeof(DiscoveryContext));
    strncpy(ctx->my_device_id,   device_id,   MAX_DEVICE_ID_LEN   - 1);
    strncpy(ctx->my_device_type, device_type, MAX_DEVICE_TYPE_LEN - 1);
    ctx->my_tcp_port = tcp_port;
    ctx->registry    = registry;
    ctx->running     = 1;
    ctx->udp_sock    = -1;
    ctx->listen_sock = -1;
    detect_iface(ctx->iface, sizeof(ctx->iface),
                 ctx->broadcast_addr, sizeof(ctx->broadcast_addr));

    /* --- Broadcast sender socket --- */
    ctx->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->udp_sock < 0) {
        perror("[DISCOVERY] Failed to create sender socket");
        return -1;
    }

    int broadcast = 1;
    if (setsockopt(ctx->udp_sock, SOL_SOCKET, SO_BROADCAST,
                   &broadcast, sizeof(broadcast)) < 0) {
        perror("[DISCOVERY] Failed to enable broadcast");
        close(ctx->udp_sock);
        return -1;
    }

    // Bind sender to detected interface
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ctx->iface, IFNAMSIZ - 1);
    if (setsockopt(ctx->udp_sock, SOL_SOCKET, SO_BINDTODEVICE,
                   &ifr, sizeof(ifr)) < 0) {
        perror("[DISCOVERY] Failed to bind to interface");
    }

    /* --- Listener socket --- */
    ctx->listen_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->listen_sock < 0) {
        perror("[DISCOVERY] Failed to create listener socket");
        close(ctx->udp_sock);
        return -1;
    }


    int reuse = 1;
    setsockopt(ctx->listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(ctx->listen_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons(DISCOVERY_PORT);

    if (bind(ctx->listen_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("[DISCOVERY] Failed to bind listener socket");
        close(ctx->udp_sock);
        close(ctx->listen_sock);
        return -1;
    }
    printf("[DISCOVERY] Sockets created OK\n");
    printf("[DISCOVERY] Initialized for device: %s (type: %s, tcp_port: %d, iface: %s, bcast: %s)\n",
           device_id, device_type, tcp_port, ctx->iface, ctx->broadcast_addr);
    return 0;
}

int discovery_announce(DiscoveryContext *ctx)
{
    DiscoveryPacket pkt;
    build_packet(ctx, DISC_ANNOUNCE, &pkt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(DISCOVERY_PORT);
    inet_pton(AF_INET, ctx->broadcast_addr, &addr.sin_addr);

    ssize_t sent = sendto(ctx->udp_sock, &pkt, sizeof(pkt), 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        perror("[DISCOVERY] Broadcast failed");
        return -1;
    }

    printf("[DISCOVERY] Announced presence on network (port %d)\n", ctx->my_tcp_port);
    return 0;
}

int discovery_goodbye(DiscoveryContext *ctx)
{
    DiscoveryPacket pkt;
    build_packet(ctx, DISC_GOODBYE, &pkt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(DISCOVERY_PORT);
    inet_pton(AF_INET, ctx->broadcast_addr, &addr.sin_addr);

    sendto(ctx->udp_sock, &pkt, sizeof(pkt), 0,
           (struct sockaddr *)&addr, sizeof(addr));

    printf("[DISCOVERY] Goodbye broadcast sent\n");
    return 0;
}

int discovery_start_listener(DiscoveryContext *ctx)
{
    pthread_t tid;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024); // 64KB stack
    
    if (pthread_create(&tid, &attr, listener_thread, ctx) != 0) {
        perror("[DISCOVERY] Failed to start listener thread");
        pthread_attr_destroy(&attr);
        return -1;
    }
    pthread_attr_destroy(&attr);
    pthread_detach(tid);
    return 0;
}

void discovery_stop(DiscoveryContext *ctx)
{
    ctx->running = 0;
    discovery_goodbye(ctx);

    if (ctx->udp_sock    >= 0) close(ctx->udp_sock);
    if (ctx->listen_sock >= 0) close(ctx->listen_sock);

    ctx->udp_sock    = -1;
    ctx->listen_sock = -1;

    printf("[DISCOVERY] Stopped\n");
}