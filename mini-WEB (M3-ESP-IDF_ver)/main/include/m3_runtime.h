#pragma once
#include "storage_view.h"
#include "network_manager.h"
#define M3_WIFI_RESULTS 8
typedef enum { M3_WIFI_SCAN, M3_WIFI_CONNECT, M3_WIFI_SETUP, M3_WIFI_IPV6 } m3_wifi_action_t;
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
    bool wifi_busy, wifi_ready;
    unsigned scan_revision, settings_revision, wifi_revision;
    int scan_count;
    wifi_scan_item_t networks[M3_WIFI_RESULTS];
    network_settings_t settings; /* Public fields only; password is always cleared. */
    char wifi_message[128];
} m3_runtime_state_t;
esp_err_t m3_runtime_start(bool force_setup);
void m3_runtime_state(m3_runtime_state_t *out);
bool m3_runtime_browse(bool sd, const char *folder, unsigned offset);
bool m3_runtime_wifi(m3_wifi_action_t action, const network_settings_t *settings);
