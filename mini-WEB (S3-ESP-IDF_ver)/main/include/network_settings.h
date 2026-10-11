#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    uint32_t version;
    char ssid[33];
    char pass[64];
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns[16];
    uint8_t use_static;
} network_settings_t;
bool network_settings_load(network_settings_t *settings);
bool network_settings_valid(const network_settings_t *settings);
esp_err_t network_save_configuration(const network_settings_t *settings);
bool network_setup_client_local(int socket_fd);
