#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "udp_discover.h"
#include "tcp_comm.h"
#include "messages.h"

// Variables globales
static int g_running = 1;
static int g_udp_sock = -1;
static int g_tcp_server_sock = -1;

// Manejador de señales para salida limpia
void signal_handler(int sig) {
    printf("\n🛑 Señal recibida, cerrando...\n");
    g_running = 0;
}

// Hilo para descubrimiento UDP
void* discovery_thread(void *arg) {
    (void)arg;  // Evitar warning de parámetro no usado
    
    while (g_running) {
        // Enviar broadcast cada 5 segundos
        udp_discover_send(g_udp_sock);
        
        // Escuchar por respuestas (con timeout de 1 segundo)
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(g_udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        DiscoveredDevice device;
        if (udp_discover_listen(g_udp_sock, &device) == 0) {
            printf("📡 Dispositivo descubierto: %s (%s) en %s:%d\n",
                   device.device_id, device.device_type, 
                   device.ip, device.tcp_port);
        }
        
        sleep(4); // Esperar antes del siguiente broadcast
    }
    
    return NULL;
}

// Hilo para servidor TCP (acepta conexiones entrantes)
void* tcp_server_thread(void *arg) {
    (void)arg;  // Evitar warning
    
    while (g_running) {
        printf("⏳ Esperando conexiones TCP...\n");
        
        int client_sock = tcp_server_accept(g_tcp_server_sock);
        if (client_sock < 0) {
            if (g_running) sleep(1);
            continue;
        }
        
        // Recibir mensajes del cliente
        CarMessage received_msg;
        while (g_running) {
            if (tcp_receive_car_message(client_sock, &received_msg) < 0) {
                break;
            }
            
            // Responder con nuestros propios datos
            CarMessage response = {
                .id = 999,  // ID de este dispositivo
                .x = 10.5,
                .y = 20.3,
                .speed = 5.5,
                .state = 1,
                .timestamp = (uint32_t)time(NULL)
            };
            
            tcp_send_car_message(client_sock, &response);
            sleep(1);
        }
        
        tcp_close(client_sock);
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    // Configurar manejo de señales
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Parámetros del dispositivo
    const char *device_id = (argc > 1) ? argv[1] : "device01";
    const char *device_type = (argc > 2) ? argv[2] : "generic";
    uint16_t tcp_port = (argc > 3) ? (uint16_t)atoi(argv[3]) : 60000;
    
    printf("🚀 Iniciando Framework de Comunicación\n");
    printf("   ID: %s\n", device_id);
    printf("   Tipo: %s\n", device_type);
    printf("   Puerto TCP: %d\n\n", tcp_port);
    
    // Inicializar UDP Discovery
    g_udp_sock = udp_discover_init(device_id, device_type, tcp_port);
    if (g_udp_sock < 0) {
        fprintf(stderr, "❌ Error iniciando UDP Discovery\n");
        return 1;
    }
    
    // Inicializar servidor TCP
    g_tcp_server_sock = tcp_server_init(tcp_port);
    if (g_tcp_server_sock < 0) {
        fprintf(stderr, "❌ Error iniciando servidor TCP\n");
        udp_discover_close(g_udp_sock);
        return 1;
    }
    
    // Crear hilos
    pthread_t discovery_tid, tcp_server_tid;
    
    pthread_create(&discovery_tid, NULL, discovery_thread, NULL);
    pthread_create(&tcp_server_tid, NULL, tcp_server_thread, NULL);
    
    // Esperar a que se presione Ctrl+C
    printf("\n✓ Framework iniciado. Presiona Ctrl+C para salir.\n\n");
    
    while (g_running) {
        sleep(1);
    }
    
    // Limpieza
    pthread_join(discovery_tid, NULL);
    pthread_cancel(tcp_server_tid);
    pthread_join(tcp_server_tid, NULL);
    
    udp_discover_close(g_udp_sock);
    tcp_close(g_tcp_server_sock);
    
    printf("👋 Framework cerrado correctamente\n");
    return 0;
}