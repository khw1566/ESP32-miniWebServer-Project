#include "m3_weather.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool m3_weather_place_valid(const m3_weather_place_t *p) {
    return p && p->name[0] && memchr(p->name, 0, sizeof(p->name)) && memchr(p->area, 0, sizeof(p->area)) &&
        isfinite(p->latitude) && isfinite(p->longitude) && fabs(p->latitude) <= 90 && fabs(p->longitude) <= 180;
}
bool m3_weather_encode(const char *input, char *out, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    if (!input || !out || !capacity) return false;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (used + (plain ? 1 : 3) >= capacity) { out[0] = 0; return false; }
        if (plain) out[used++] = (char)*p;
        else { out[used++] = '%'; out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 15]; }
    }
    out[used] = 0; return used > 0;
}
static bool number(const cJSON *item, double low, double high, double *out) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < low || item->valuedouble > high) return false;
    *out = item->valuedouble; return true;
}
static bool member(const cJSON *obj, const char *key, double low, double high, double *out) {
    return number(cJSON_GetObjectItemCaseSensitive(obj, key), low, high, out);
}
static bool string(const cJSON *obj, const char *key, char *out, size_t capacity) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || !item->valuestring[0] || strlen(item->valuestring) >= capacity) return false;
    snprintf(out, capacity, "%s", item->valuestring); return true;
}
static bool place(const cJSON *obj, m3_weather_place_t *out) {
    *out = (m3_weather_place_t){0};
    if (!string(obj, "name", out->name, sizeof(out->name)) ||
        !member(obj, "latitude", -90, 90, &out->latitude) || !member(obj, "longitude", -180, 180, &out->longitude)) return false;
    char admin[80] = "", country[40] = "";
    string(obj, "admin1", admin, sizeof(admin)); string(obj, "country", country, sizeof(country));
    snprintf(out->area, sizeof(out->area), "%s%s%s", admin, admin[0] && country[0] ? ", " : "", country);
    return m3_weather_place_valid(out);
}
bool m3_weather_parse_setting(const char *json, m3_weather_place_t *out) {
    cJSON *root = cJSON_Parse(json);
    m3_weather_place_t next;
    double version;
    bool ok = root && member(root, "version", 1, 1, &version) && place(root, &next);
    if (ok) { string(root, "area", next.area, sizeof(next.area)); *out = next; }
    cJSON_Delete(root); return ok;
}
bool m3_weather_parse_places(const char *json, m3_weather_place_t *out, unsigned *count) {
    *count = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    bool ok = cJSON_IsObject(root) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "error")) &&
        (!results || cJSON_IsArray(results));
    if (ok) {
        const cJSON *item;
        cJSON_ArrayForEach(item, results) {
            if (*count == M3_WEATHER_RESULTS) break;
            if (place(item, &out[*count])) ++*count;
        }
    }
    cJSON_Delete(root); return ok;
}
bool m3_weather_parse(const char *json, m3_weather_data_t *out) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    m3_weather_data_t next = {0}; double code;
    bool ok = member(current, "temperature_2m", -100, 70, &next.temperature) &&
        member(current, "apparent_temperature", -150, 100, &next.feels_like) &&
        member(current, "relative_humidity_2m", 0, 100, &next.humidity) &&
        member(current, "wind_speed_10m", 0, 200, &next.wind) &&
        member(current, "weather_code", 0, 99, &code) && code == floor(code) &&
        string(current, "time", next.time, sizeof(next.time));
    if (ok) {
        next.code = (int)code;
        for (unsigned i = 0; i < M3_WEATHER_DAYS; ++i) {
            m3_weather_day_t *day = &next.days[i];
            const cJSON *date = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "time"), i);
            day->valid = cJSON_IsString(date) && strlen(date->valuestring) == 10 &&
                number(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min"), i), -100, 70, &day->low) &&
                number(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max"), i), -100, 70, &day->high) &&
                day->low <= day->high &&
                number(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "precipitation_probability_max"), i), 0, 100, &day->rain) &&
                number(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "weather_code"), i), 0, 99, &code) && code == floor(code);
            if (day->valid) { snprintf(day->date, sizeof(day->date), "%s", date->valuestring); day->code = (int)code; }
        }
        *out = next;
    }
    cJSON_Delete(root); return ok;
}
