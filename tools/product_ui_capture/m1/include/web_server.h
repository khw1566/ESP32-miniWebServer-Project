#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t web_server_start(void);
void web_server_stop(void);
bool web_server_is_running(void);
