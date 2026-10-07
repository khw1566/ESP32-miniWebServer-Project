/* Router mode: while the STA link to the upstream Wi-Fi stays up, a WPA2 AP
 * shares it with up to ROUTER_MAX_CONN clients through lwIP NAPT.
 * Included by network_manager.c; uses its netif and mode state.
 * The NAPT table (CONFIG_LWIP_IPV4_NAPT, IP_NAPT_MAX in the project
 * CMakeLists) is allocated only while router mode is on. */
#include "esp_random.h"

#define NVS_KEY_ROUTER_ON   "router_on"
#define NVS_KEY_ROUTER_PASS "router_pass"

static atomic_bool s_router_active;
static char s_router_ssid[33];
static char s_router_ip[16] = "192.168.4.1";

static esp_err_t router_nvs_flag(bool on) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(nvs, NVS_KEY_ROUTER_ON, on);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

bool network_router_saved(void) {
    nvs_handle_t nvs;
    uint8_t on = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    nvs_get_u8(nvs, NVS_KEY_ROUTER_ON, &on);
    nvs_close(nvs);
    return on;
}

/* The password is generated once per board and kept in NVS. */
esp_err_t network_router_password(char *out, size_t length) {
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
    static char cached[ROUTER_PASS_LEN + 1];
    if (!out || length < ROUTER_PASS_LEN + 1) return ESP_ERR_INVALID_ARG;
    if (cached[0]) {memcpy(out, cached, sizeof(cached)); return ESP_OK;}
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    size_t size = length;
    err = nvs_get_str(nvs, NVS_KEY_ROUTER_PASS, out, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        for (int i = 0; i < ROUTER_PASS_LEN; ++i) out[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
        out[ROUTER_PASS_LEN] = 0;
        err = nvs_set_str(nvs, NVS_KEY_ROUTER_PASS, out);
        if (err == ESP_OK) err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK && strlen(out) == ROUTER_PASS_LEN) memcpy(cached, out, sizeof(cached));
    return err;
}

const char *network_router_ssid(void) {
    if (!s_router_ssid[0]) {
        const char *serial = network_get_product_serial();
        size_t n = serial ? strlen(serial) : 0;
        snprintf(s_router_ssid, sizeof(s_router_ssid), "Mini-WEB-%s", n >= 4 ? serial + n - 4 : "M1");
        for (char *c = s_router_ssid + 9; *c; ++c) if (*c >= 'a' && *c <= 'z') *c -= 32;
    }
    return s_router_ssid;
}

bool network_router_active(void) { return atomic_load(&s_router_active); }
const char *network_router_ip(void) { return s_router_ip; }

int network_router_clients(void) {
    wifi_sta_list_t list;
    if (!network_router_active() || esp_wifi_ap_get_sta_list(&list) != ESP_OK) return 0;
    return list.num;
}

/* Called before any mode change that stops Wi-Fi; keeps the saved preference. */
static void router_teardown(void) {
    if (!atomic_exchange(&s_router_active, false)) return;
    if (s_ap_netif) esp_netif_napt_disable(s_ap_netif);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ESP_LOGI(TAG, "[Router] stopped, heap=%lu", (unsigned long)esp_get_free_heap_size());
}

/* Offer the upstream DNS server to AP clients, on a subnet distinct from the upstream LAN. */
static esp_err_t router_configure_ap_netif(void) {
    esp_netif_ip_info_t sta = {0}, ap = {0};
    esp_netif_get_ip_info(s_sta_netif, &sta);
    uint32_t third = (lwip_ntohl(sta.ip.addr) >> 8) & 0xFF;
    bool upstream_192_168 = (lwip_ntohl(sta.ip.addr) >> 16) == 0xC0A8;
    int subnet = upstream_192_168 && third == 4 ? 5 : 4;
    snprintf(s_router_ip, sizeof(s_router_ip), "192.168.%d.1", subnet);
    ap.ip.addr = esp_ip4addr_aton(s_router_ip);
    ap.gw.addr = ap.ip.addr;
    ap.netmask.addr = esp_ip4addr_aton("255.255.255.0");

    esp_netif_dns_info_t dns = {0};
    if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK || !dns.ip.u_addr.ip4.addr) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = sta.gw.addr;
    }
    uint8_t offer_dns = 1;
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    if ((err = esp_netif_set_ip_info(s_ap_netif, &ap)) != ESP_OK) return err;
    if ((err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns)) != ESP_OK) return err;
    if ((err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                      &offer_dns, sizeof(offer_dns))) != ESP_OK) return err;
    return esp_netif_dhcps_start(s_ap_netif);
}

esp_err_t network_router_start(bool remember) {
    if (s_net_mode != NET_MODE_STA_CONNECTED || !s_sta_netif) return ESP_ERR_INVALID_STATE;
    if (network_router_active()) return remember ? router_nvs_flag(true) : ESP_OK;
    char password[ROUTER_PASS_LEN + 1];
    esp_err_t err = network_router_password(password, sizeof(password));
    if (err != ESP_OK) return err;
    if (!s_ap_netif && !(s_ap_netif = esp_netif_create_default_wifi_ap())) return ESP_ERR_NO_MEM;

    /* The radio has one channel: the AP must follow the upstream AP. */
    wifi_ap_record_t upstream;
    if ((err = esp_wifi_sta_get_ap_info(&upstream)) != ESP_OK) return err;
    wifi_config_t config = {
        .ap = {
            .channel = upstream.primary,
            .max_connection = ROUTER_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    const char *ssid = network_router_ssid();
    config.ap.ssid_len = strlen(ssid);
    memcpy(config.ap.ssid, ssid, config.ap.ssid_len);
    memcpy(config.ap.password, password, ROUTER_PASS_LEN);
    memset(password, 0, sizeof(password));

    unsigned before = (unsigned)esp_get_free_heap_size();
    /* Adding the AP keeps the running STA association. */
    if ((err = esp_wifi_set_mode(WIFI_MODE_APSTA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_config(WIFI_IF_AP, &config)) != ESP_OK) goto failed;
    memset(&config, 0, sizeof(config));
    if ((err = router_configure_ap_netif()) != ESP_OK) goto failed;
    /* Modem sleep drops frames for AP clients. */
    esp_wifi_set_ps(WIFI_PS_NONE);
    if ((err = esp_netif_napt_enable(s_ap_netif)) != ESP_OK) goto failed;
    atomic_store(&s_router_active, true);
    ESP_LOGI(TAG, "[Router] started: SSID=%s, %s, channel %d, heap %u -> %lu", ssid, s_router_ip,
             upstream.primary, before, (unsigned long)esp_get_free_heap_size());
    return remember ? router_nvs_flag(true) : ESP_OK;

failed:
    memset(&config, 0, sizeof(config));
    ESP_LOGE(TAG, "[Router] start failed: %s", esp_err_to_name(err));
    esp_netif_napt_disable(s_ap_netif);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    return err;
}

esp_err_t network_router_stop(bool remember) {
    if (network_router_active()) {
        router_teardown();
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) return err;
    }
    return remember ? router_nvs_flag(false) : ESP_OK;
}
