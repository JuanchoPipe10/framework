#ifndef TCP_COMM_H
#define TCP_COMM_H

#include <stdint.h>
#include "messages.h"

#define TCP_BUFFER_SIZE 4096

// Iniciar servidor TCP (espera conexiones)
int tcp_server_init(uint16_t port);
int tcp_server_accept(int server_sock);

// Conectar como cliente TCP
int tcp_client_connect(const char *ip, uint16_t port);

// Enviar/recibir mensajes estructurados
int tcp_send_car_message(int sock, const CarMessage *msg);
int tcp_receive_car_message(int sock, CarMessage *msg);

// Cerrar conexión
void tcp_close(int sock);

#endif // TCP_COMM_H