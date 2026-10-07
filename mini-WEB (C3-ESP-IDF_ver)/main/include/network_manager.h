#pragma once
#include <esp_err.h>
#include <stdbool.h>
#include "esp_wifi_types.h"

#define SOFTAP_SSID_DEFAULT "Mini-WebServer-Setup"
#define SOFTAP_PASS_DEFAULT ""
#define SOFTAP_CHANNEL      1
#define SOFTAP_MAX_CONN     4

#define NVS_NAMESPACE       "mini_web"
#define NVS_KEY_SSID        "wifi_ssid"
#define NVS_KEY_PASS        "wifi_pass"

#define NVS_KEY_STATIC_IP   "static_ip"
#define NVS_KEY_STATIC_GW   "static_gw"
#define NVS_KEY_STATIC_MASK "static_mask"

typedef enum {
    NET_MODE_IDLE,
    NET_MODE_AP_CAPTIVE,
    NET_MODE_STA_CONNECTING,
    NET_MODE_STA_CONNECTED
} net_mode_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_scan_item_t;

esp_err_t network_init(void);
net_mode_t network_get_mode(void);
esp_err_t network_start_softap(void);
esp_err_t network_connect_sta(const char *ssid, const char *pass);
esp_err_t network_save_credentials(const char *ssid, const char *pass);
bool network_has_saved_credentials(char *out_ssid, size_t ssid_len, char *out_pass, size_t pass_len);
esp_err_t network_save_static_ip(const char *ip, const char *gw, const char *mask);
bool network_get_static_ip(char *out_ip, size_t ip_len, char *out_gw, size_t gw_len, char *out_mask, size_t mask_len);
esp_err_t network_get_ip_str(char *out_ip, size_t ip_len);
esp_err_t network_get_ip6_str(char *out_ip6, size_t ip6_len);
esp_err_t network_get_ip6_linklocal_str(char *out_ip6, size_t ip6_len);
esp_err_t network_retry_ipv6(void);
const char *network_get_ipv6_status(void);
int8_t network_get_rssi(void);
int network_scan_wifi(wifi_scan_item_t *items, int max_items);


// Stable product address for local Wi-Fi access.
const char *network_get_product_name(void);
const char *network_get_product_serial(void);
const char *network_get_product_revision(void);
esp_err_t network_get_mdns_url(char *out, size_t length);
esp_err_t network_mdns_start(void);
void network_mdns_stop(void);
bool network_mdns_is_ready(void);

// Diagnostic event counters since boot; reason 0 means no recorded disconnect.
unsigned network_get_disconnect_count(void);
unsigned network_get_last_disconnect_reason(void);
