#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { M3_WEATHER_RESULTS = 5, M3_WEATHER_DAYS = 3, M3_WEATHER_QUERY_BYTES = 160 };
typedef struct { char name[96], area[128]; double latitude, longitude; } m3_weather_place_t;
typedef struct { char date[11]; double low, high, rain; int code; bool valid; } m3_weather_day_t;
typedef struct {
    double temperature, feels_like, humidity, wind;
    int code;
    char time[32];
    m3_weather_day_t days[M3_WEATHER_DAYS];
} m3_weather_data_t;
typedef enum {
    M3_WEATHER_IDLE, M3_WEATHER_LOADING, M3_WEATHER_READY, M3_WEATHER_OFFLINE,
    M3_WEATHER_NETWORK, M3_WEATHER_INVALID, M3_WEATHER_EMPTY, M3_WEATHER_MEMORY,
    M3_WEATHER_WAIT
} m3_weather_status_t;
typedef struct {
    unsigned revision, count;
    bool busy, valid, saved, stale;
    m3_weather_status_t status;
    m3_weather_place_t place, results[M3_WEATHER_RESULTS];
    m3_weather_data_t data;
    int64_t updated_us;
} m3_weather_state_t;

/* Queue-only UI calls. A single worker owns HTTPS and SD settings I/O. */
void m3_weather_init(void);
void m3_weather_state(m3_weather_state_t *out);
bool m3_weather_search(const char *query, const char *language);
bool m3_weather_select(const m3_weather_place_t *place);
bool m3_weather_refresh(void);
unsigned m3_weather_preset_count(void);
const m3_weather_place_t *m3_weather_preset(unsigned index);
/* Pure bounded parsing helpers shared by firmware and host fixtures. */
bool m3_weather_place_valid(const m3_weather_place_t *place);
bool m3_weather_encode(const char *input, char *out, size_t capacity);
bool m3_weather_parse(const char *json, m3_weather_data_t *out);
bool m3_weather_parse_places(const char *json, m3_weather_place_t *out, unsigned *count);
bool m3_weather_parse_setting(const char *json, m3_weather_place_t *out);
