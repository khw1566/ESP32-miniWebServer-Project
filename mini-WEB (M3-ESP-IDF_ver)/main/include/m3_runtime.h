#pragma once
#include "storage_view.h"
#include "network_manager.h"
#define M3_WIFI_RESULTS 8
typedef enum { M3_WIFI_SCAN, M3_WIFI_CONNECT, M3_WIFI_SETUP, M3_WIFI_IPV6, M3_WIFI_CONNECT_SAVED } m3_wifi_action_t;
typedef enum { M3_WEB_STOP_DOMAIN, M3_WEB_STOP_ALL, M3_WEB_START_DOMAIN, M3_WEB_START_ALL } m3_web_action_t;
typedef struct {
    bool initialized, web, sd, cloud;
    net_mode_t mode;
    int rssi;
    unsigned requests, disconnects, file_revision;
    char ip[32], url[96], message[96], folder[256];
    char cloud_url[96]; /* public https address while the cloud relay is online, else empty */
    char domain_url[96], ipv6_url[96], mdns_url[96];
    char ipv6_status[24], cloud_status[24];
    bool mdns_ready;
    bool browse_sd;
    storage_view_t files;
    storage_capacity_t flash;
    storage_capacity_t sd_capacity;
    bool cpu_valid, lp_configured, web_control_busy, sd_capacity_stale;
    uint8_t cpu_busy[2], web_policy; /* 0 serving, 1 domain paused, 2 all stopped */
    uint32_t cpu_sample_ms;
    unsigned web_control_revision;
    esp_err_t web_control_result;
    bool wifi_busy, wifi_ready, wifi_password_saved;
    unsigned scan_revision, settings_revision, wifi_revision;
    int scan_count;
    wifi_scan_item_t networks[M3_WIFI_RESULTS];
    network_settings_t settings; /* Public fields only; password is always cleared. */
    char wifi_message[128];
    unsigned mutation_revision;
    esp_err_t mutation_result;
    bool mutation_busy;
} m3_runtime_state_t;
esp_err_t m3_runtime_start(bool force_setup);
void m3_runtime_state(m3_runtime_state_t *out);
bool m3_runtime_browse(bool sd, const char *folder, unsigned offset);
bool m3_runtime_wifi(m3_wifi_action_t action, const network_settings_t *settings);
bool m3_runtime_file_action(bool sd, const char *source, const char *destination, bool remove);
bool m3_runtime_web(m3_web_action_t action);
