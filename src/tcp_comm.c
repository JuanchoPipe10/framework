/**
 * @file tcp_comm.c
 * @brief Implementation of TCP communication layer
 */

#include "tcp_comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int tcp_server_init(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[TCP] Socket creation failed");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("[TCP] Failed to set SO_REUSEADDR");
        close(sock);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[TCP] Bind failed");
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        perror("[TCP] Listen failed");
        close(sock);
        return -1;
    }

    printf("[TCP] Server listening on port %d\n", port);
    return sock;
}

int tcp_server_accept(int server_sock) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
    if (client_sock < 0) {
        perror("[TCP] Accept failed");
        return -1;
    }

    char client_ip[16];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("[TCP] Client connected from %s\n", client_ip);

    return client_sock;
}

int tcp_client_connect(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[TCP] Client socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("[TCP] Invalid IP address");
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[TCP] Connection failed");
        close(sock);
        return -1;
    }

    printf("[TCP] Connected to %s:%d\n", ip, port);
    return sock;
}

int tcp_send_car_message(int sock, const CarMessage *msg) {
    ssize_t sent = send(sock, msg, sizeof(CarMessage), 0);
    
    if (sent < 0) {
        perror("[TCP] Send failed");
        return -1;
    }

    printf("[TCP] CarMessage sent: ID=%u, pos=(%.2f,%.2f), speed=%.2f, state=%u\n",
           msg->id, msg->x, msg->y, msg->speed, msg->state);

    return 0;
}

int tcp_receive_car_message(int sock, CarMessage *msg) {
    ssize_t received = recv(sock, msg, sizeof(CarMessage), 0);

    if (received < 0) {
        perror("[TCP] Receive failed");
        return -1;
    }

    if (received == 0) {
        printf("[TCP] Connection closed by peer\n");
        return -1;
    }

    printf("[TCP] CarMessage received: ID=%u, pos=(%.2f,%.2f), speed=%.2f, state=%u\n",
           msg->id, msg->x, msg->y, msg->speed, msg->state);

    return 0;
}

void tcp_close(int sock) {
    close(sock);
    printf("[TCP] Connection closed\n");
}