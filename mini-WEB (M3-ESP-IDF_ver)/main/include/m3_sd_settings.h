#pragma once
#include "network_settings.h"
#define M3_WIFI_PROFILE_LIMIT 32
typedef struct { char ssid[33]; bool secured; } m3_wifi_profile_info_t;
/* Cached metadata only: no passwords or SD I/O in UI snapshots. */
unsigned m3_sd_wifi_profiles(m3_wifi_profile_info_t *out, unsigned capacity);
bool m3_sd_wifi_known(const char *ssid);
bool m3_sd_wifi_matches(const char *ssid, bool open);
bool m3_sd_wifi_activate(const char *ssid);
esp_err_t m3_sd_wifi_forget(const char *ssid);
/* Called once by the service task after SD mount, before network startup. */
void m3_sd_settings_init(void);
const char *m3_sd_gemini_key(void);
const char *m3_sd_google_tts_key(void);
const char *m3_sd_openai_key(void);
const char *m3_sd_azure_key(void);
const char *m3_sd_azure_region(void);
/* A save hook runs only in network/service HTTP tasks, never the LVGL task. */
void m3_sd_wifi_save(const network_settings_t *settings);
bool m3_sd_wifi_find(const char *ssid, network_settings_t *settings);

/* M3 uses SD JSON directly, without an NVS credential copy. */
bool m3_sd_wifi_load(network_settings_t *settings);
esp_err_t m3_sd_wifi_store(const network_settings_t *settings);
bool m3_sd_key_copy(const char *name, char out[256]);
esp_err_t m3_sd_gemini_save(const char *key);
