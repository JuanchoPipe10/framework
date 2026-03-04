/**
 * @file device_registry.c
 * @brief Implementation of device registry
 */

#include "device_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int registry_init(DeviceRegistry *reg, const char *my_device_id) {
    memset(reg, 0, sizeof(DeviceRegistry));
    strncpy(reg->my_device_id, my_device_id, MAX_DEVICE_ID_LEN - 1);
    reg->count = 0;
    printf("[REGISTRY] Initialized for device: %s\n", my_device_id);
    return 0;
}

int registry_load_from_file(DeviceRegistry *reg, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "[REGISTRY] Warning: Could not open %s\n", filename);
        return -1;
    }

    char line[256];
    int loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        char id[MAX_DEVICE_ID_LEN];
        char type[MAX_DEVICE_TYPE_LEN];
        char ip[MAX_IP_LEN];
        int port;

        if (sscanf(line, "%31[^,],%15[^,],%15[^,],%d", id, type, ip, &port) == 4) {
            // Don't add ourselves
            if (strcmp(id, reg->my_device_id) != 0) {
                registry_add_device(reg, id, type, ip, (uint16_t)port);
                loaded++;
            }
        }
    }

    fclose(fp);
    printf("[REGISTRY] Loaded %d devices from %s\n", loaded, filename);
    return loaded;
}

int registry_add_device(DeviceRegistry *reg, const char *id, const char *type,
                        const char *ip, uint16_t port) {
    if (reg->count >= MAX_DEVICES) {
        fprintf(stderr, "[REGISTRY] Max devices reached\n");
        return -1;
    }

    Device *dev = &reg->devices[reg->count];
    strncpy(dev->device_id, id, MAX_DEVICE_ID_LEN - 1);
    strncpy(dev->device_type, type, MAX_DEVICE_TYPE_LEN - 1);
    strncpy(dev->ip, ip, MAX_IP_LEN - 1);
    dev->tcp_port = port;
    dev->connected = 0;
    dev->last_contact = 0;

    reg->count++;
    printf("[REGISTRY] Added: %s (%s) at %s:%d\n", id, type, ip, port);
    return 0;
}

Device* registry_get_device(DeviceRegistry *reg, const char *device_id) {
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->devices[i].device_id, device_id) == 0) {
            return &reg->devices[i];
        }
    }
    return NULL;
}

int registry_get_other_devices(DeviceRegistry *reg, Device **devices) {
    *devices = reg->devices;
    return reg->count;
}

void registry_set_connected(DeviceRegistry *reg, const char *device_id, int connected) {
    Device *dev = registry_get_device(reg, device_id);
    if (dev) {
        dev->connected = connected;
        if (connected) {
            dev->last_contact = time(NULL);
        }
    }
}

void registry_update_contact(DeviceRegistry *reg, const char *device_id) {
    Device *dev = registry_get_device(reg, device_id);
    if (dev) {
        dev->last_contact = time(NULL);
    }
}

void registry_print(DeviceRegistry *reg) {
    printf("\n=== Device Registry (%s) ===\n", reg->my_device_id);
    printf("%-15s %-12s %-18s %-8s %-10s\n", 
           "Device ID", "Type", "Address", "Port", "Status");
    printf("-------------------------------------------------------------\n");
    
    for (int i = 0; i < reg->count; i++) {
        Device *dev = &reg->devices[i];
        printf("%-15s %-12s %-18s %-8d %-10s\n",
               dev->device_id,
               dev->device_type,
               dev->ip,
               dev->tcp_port,
               dev->connected ? "Connected" : "Disconnected");
    }
    printf("=============================================================\n\n");
}