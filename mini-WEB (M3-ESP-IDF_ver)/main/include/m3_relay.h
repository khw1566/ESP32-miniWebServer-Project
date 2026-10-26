#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

enum { M3_RELAY_MAX = 8, M3_RELAY_URL_SIZE = 192, M3_RELAY_TOKEN_SIZE = 256 };
typedef struct {
    char url[M3_RELAY_URL_SIZE];
    char token[M3_RELAY_TOKEN_SIZE];
} m3_relay_profile_t;
/* Boot-only publication from SD. No private header or Flash credential fallback. */
bool m3_relay_load(const cJSON *json);
unsigned m3_relay_count(void);
bool m3_relay_current(m3_relay_profile_t *out);
/* Worker-only, with the caller's HTTPS lease held. Each request owns a copy. */
bool m3_relay_select(m3_relay_profile_t *out);
bool m3_relay_radio_route(const char *url, const char **suffix);
