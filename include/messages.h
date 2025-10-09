#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

// Tipos de mensajes que puede enviar el sistema
typedef enum {
    MSG_TYPE_DISCOVERY = 1,    // Para descubrimiento inicial
    MSG_TYPE_CAR_STATUS = 2,   // Estado del carro
    MSG_TYPE_SENSOR_DATA = 3,  // Datos de sensores (para futuro)
    MSG_TYPE_COMMAND = 4       // Comandos (para futuro)
} MessageType;

// Estructura principal para mensajes de carro
typedef struct {
    uint32_t id;           // ID único del dispositivo
    float x;               // Posición X
    float y;               // Posición Y
    float speed;           // Velocidad actual
    uint8_t state;         // Estado (0=stopped, 1=moving, 2=error, etc)
    uint32_t timestamp;    // Marca de tiempo
} CarMessage;

// Estructura de respuesta al descubrimiento
typedef struct {
    char device_id[32];    // Ej: "car03"
    char device_type[16];  // Ej: "ultra96", "pc"
    uint16_t tcp_port;     // Puerto TCP para comunicación directa
    uint32_t capabilities; // Flags de capacidades (para futuro)
} DiscoveryResponse;

#endif // MESSAGES_H