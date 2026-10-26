#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
esp_err_t cloud_relay_start(void);
/* Local administrator provisioning; takes effect after reboot. */
esp_err_t cloud_relay_provision(const char *token);
bool cloud_relay_is_online(void);
const char *cloud_relay_status(void);
esp_err_t cloud_relay_get_url(char *out, size_t length);
/* Configured public address, including while offline. Availability is reported separately. */
esp_err_t cloud_relay_get_public_url(char *out, size_t length);
bool cloud_relay_public_path(const char *path);
