#pragma once
#include <esp_err.h>
#include <esp_http_server.h>
#include <stdint.h>
uint32_t web_server_home_requests(void);

esp_err_t web_server_storage_init(void);
esp_err_t web_server_log_init(void);
esp_err_t web_server_start(void);
void web_server_stop(void);
