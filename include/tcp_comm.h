/**
 * @file tcp_comm.h
 * @brief TCP-based direct communication module
 * 
 * Provides reliable point-to-point communication
 * for structured message exchange between devices.
 */

#ifndef TCP_COMM_H
#define TCP_COMM_H

#include <stdint.h>
#include "messages.h"

#define TCP_BUFFER_SIZE 4096

/**
 * @brief Initialize TCP server socket
 * @param port Port number to listen on
 * @return Server socket descriptor on success, -1 on error
 */
int tcp_server_init(uint16_t port);

/**
 * @brief Accept incoming TCP connection
 * @param server_sock Server socket descriptor
 * @return Client socket descriptor on success, -1 on error
 */
int tcp_server_accept(int server_sock);

/**
 * @brief Connect to TCP server as client
 * @param ip Server IP address
 * @param port Server port
 * @return Socket descriptor on success, -1 on error
 */
int tcp_client_connect(const char *ip, uint16_t port);

/**
 * @brief Send CarMessage structure via TCP
 * @param sock Socket descriptor
 * @param msg Pointer to CarMessage structure
 * @return 0 on success, -1 on error
 */
int tcp_send_car_message(int sock, const CarMessage *msg);

/**
 * @brief Receive CarMessage structure via TCP
 * @param sock Socket descriptor
 * @param msg Pointer to store received message
 * @return 0 on success, -1 on error
 */
int tcp_receive_car_message(int sock, CarMessage *msg);

/**
 * @brief Close TCP connection
 * @param sock Socket descriptor to close
 */
void tcp_close(int sock);

#endif // TCP_COMM_H