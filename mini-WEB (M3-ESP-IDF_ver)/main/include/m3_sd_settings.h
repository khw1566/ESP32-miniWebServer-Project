#pragma once
#include "network_settings.h"
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
