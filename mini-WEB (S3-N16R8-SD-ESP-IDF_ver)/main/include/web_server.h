#pragma once
#include <esp_err.h>
#include <esp_http_server.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct {
    FILE *file;
    char *owned_body;
    const char *body, *mime;
    size_t length, offset;
    int status;
    char content_range[80];
} web_public_stream_t;
bool web_server_public_open(const char *path, const char *range, web_public_stream_t *stream);
size_t web_server_public_read(web_public_stream_t *stream, void *buffer, size_t length);
void web_server_public_close(web_public_stream_t *stream);
bool web_server_is_running(void);
uint32_t web_server_home_requests(void);

esp_err_t web_server_storage_init(void);
esp_err_t web_server_log_init(void);
esp_err_t web_server_start(void);
void web_server_stop(void);
