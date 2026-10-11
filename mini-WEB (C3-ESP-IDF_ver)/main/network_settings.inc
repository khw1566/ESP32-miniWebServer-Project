/* Standalone copies in each firmware are synchronized by sync_network_setup.py. */
#include "network_settings.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static bool settings_ip4(const char *text, uint32_t *out) {
    if (!text || !*text) return false;
    uint32_t value = 0;
    for (int part = 0; part < 4; ++part) {
        unsigned number = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            number = number * 10 + (unsigned)(*text++ - '0');
            if (++digits > 3 || number > 255) return false;
        }
        if (!digits || (part < 3 ? *text++ != '.' : *text != 0)) return false;
        value = (value << 8) | number;
    }
    *out = value;
    return true;
}
static bool settings_unicast(uint32_t ip) {
    return (ip >> 24) != 0 && (ip >> 24) != 127 && (ip >> 24) < 224;
}
bool network_settings_valid(const network_settings_t *s) {
    if (!s || s->version != 1 || s->use_static > 1 ||
        !memchr(s->ssid, 0, sizeof(s->ssid)) || !s->ssid[0] ||
        !memchr(s->pass, 0, sizeof(s->pass)) ||
        !memchr(s->ip, 0, sizeof(s->ip)) || !memchr(s->gateway, 0, sizeof(s->gateway)) ||
        !memchr(s->netmask, 0, sizeof(s->netmask)) || !memchr(s->dns, 0, sizeof(s->dns))) return false;
    size_t password_length = strlen(s->pass);
    if (password_length && password_length < 8) return false;
    if (!s->use_static) return true;
    uint32_t ip, gw, mask, dns;
    if (!settings_ip4(s->ip, &ip) || !settings_ip4(s->gateway, &gw) ||
        !settings_ip4(s->netmask, &mask) || !settings_ip4(s->dns, &dns)) return false;
    uint32_t hostmask = ~mask;
    if (!mask || hostmask < 3 || (hostmask & (hostmask + 1)) ||
        !settings_unicast(ip) || !settings_unicast(gw) || !settings_unicast(dns)) return false;
    if ((ip & mask) != (gw & mask) || ip == gw ||
        !(ip & hostmask) || (ip & hostmask) == hostmask ||
        !(gw & hostmask) || (gw & hostmask) == hostmask) return false;
    /* The provisioning AP is always 192.168.4.1/24. Avoid overlapping routes. */
    if ((ip & mask) == (0xC0A80401UL & mask)) return false;
    return true;
}
bool network_settings_load(network_settings_t *s) {
    if (!s) return false;
    memset(s, 0, sizeof(*s)); s->version = 1;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t length = sizeof(*s);
    esp_err_t err = nvs_get_blob(h, "setup_cfg", s, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Migrate previously installed firmware without erasing credentials. */
        size_t n = sizeof(s->ssid); err = nvs_get_str(h, NVS_KEY_SSID, s->ssid, &n);
        n = sizeof(s->pass); nvs_get_str(h, NVS_KEY_PASS, s->pass, &n);
        n = sizeof(s->ip); nvs_get_str(h, NVS_KEY_STATIC_IP, s->ip, &n);
        n = sizeof(s->gateway); nvs_get_str(h, NVS_KEY_STATIC_GW, s->gateway, &n);
        n = sizeof(s->netmask); nvs_get_str(h, NVS_KEY_STATIC_MASK, s->netmask, &n);
        snprintf(s->dns, sizeof(s->dns), "%s", s->gateway);
        s->use_static = s->ip[0] != 0;
        /* Ignore broken legacy addressing, retaining valid Wi-Fi credentials. */
        if (!network_settings_valid(s)) s->use_static = 0;
    } else if (err == ESP_OK && length != sizeof(*s)) err = ESP_ERR_INVALID_SIZE;
    nvs_close(h);
    if (err != ESP_OK || !network_settings_valid(s)) {
        memset(s, 0, sizeof(*s)); s->version = 1; return false;
    }
    return true;
}
esp_err_t network_save_configuration(const network_settings_t *s) {
    if (!network_settings_valid(s)) return ESP_ERR_INVALID_ARG;
    /* One NVS blob prevents mixed SSID/password/address settings after power loss. */
    network_settings_t clean = { .version = 1, .use_static = s->use_static };
    snprintf(clean.ssid, sizeof(clean.ssid), "%s", s->ssid);
    snprintf(clean.pass, sizeof(clean.pass), "%s", s->pass);
    if (s->use_static) {
        snprintf(clean.ip, sizeof(clean.ip), "%s", s->ip);
        snprintf(clean.gateway, sizeof(clean.gateway), "%s", s->gateway);
        snprintf(clean.netmask, sizeof(clean.netmask), "%s", s->netmask);
        snprintf(clean.dns, sizeof(clean.dns), "%s", s->dns);
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "setup_cfg", &clean, sizeof(clean));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    memset(&clean, 0, sizeof(clean));
    return err;
}
esp_err_t network_save_credentials(const char *ssid, const char *pass) {
    if (!ssid || strlen(ssid) > 32 || (pass && strlen(pass) > 63)) return ESP_ERR_INVALID_ARG;
    network_settings_t s; network_settings_load(&s);
    snprintf(s.ssid, sizeof(s.ssid), "%s", ssid);
    snprintf(s.pass, sizeof(s.pass), "%s", pass ? pass : "");
    esp_err_t err = network_save_configuration(&s); memset(&s, 0, sizeof(s)); return err;
}
bool network_has_saved_credentials(char *ssid, size_t slen, char *pass, size_t plen) {
    if (!ssid || !slen || !pass || !plen) return false;
    network_settings_t s; bool found = network_settings_load(&s);
    snprintf(ssid, slen, "%s", s.ssid); snprintf(pass, plen, "%s", s.pass);
    memset(&s, 0, sizeof(s)); return found;
}
esp_err_t network_save_static_ip(const char *ip, const char *gw, const char *mask) {
    if (!ip || !gw || !mask || strlen(ip)>15 || strlen(gw)>15 || strlen(mask)>15) return ESP_ERR_INVALID_ARG;
    network_settings_t s; network_settings_load(&s);
    s.use_static = ip[0] != 0;
    snprintf(s.ip, sizeof(s.ip), "%s", ip); snprintf(s.gateway, sizeof(s.gateway), "%s", gw);
    snprintf(s.netmask, sizeof(s.netmask), "%s", mask); snprintf(s.dns, sizeof(s.dns), "%s", gw);
    esp_err_t err = network_save_configuration(&s); memset(&s, 0, sizeof(s)); return err;
}
bool network_get_static_ip(char *ip, size_t ilen, char *gw, size_t glen, char *mask, size_t mlen) {
    if (!ip || !ilen || !gw || !glen || !mask || !mlen) return false;
    network_settings_t s; bool found = network_settings_load(&s) && s.use_static;
    snprintf(ip, ilen, "%s", s.ip); snprintf(gw, glen, "%s", s.gateway); snprintf(mask, mlen, "%s", s.netmask);
    memset(&s, 0, sizeof(s)); return found;
}
bool network_setup_client_local(int socket_fd) {
    struct sockaddr_in peer; socklen_t size = sizeof(peer);
    return network_get_mode() == NET_MODE_AP_CAPTIVE &&
        getpeername(socket_fd, (struct sockaddr *)&peer, &size) == 0 && peer.sin_family == AF_INET &&
        (ntohl(peer.sin_addr.s_addr) & 0xFFFFFF00UL) == 0xC0A80400UL;
}
static esp_err_t network_apply_setup_ap_address(void) {
    /* Reset a previously used M1 router subnet/DNS before provisioning. */
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
    esp_netif_ip_info_t ap = {0};
    ap.ip.addr = esp_ip4addr_aton("192.168.4.1"); ap.gw.addr = ap.ip.addr;
    ap.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    if ((err = esp_netif_set_ip_info(s_ap_netif, &ap)) != ESP_OK) return err;
    esp_netif_dns_info_t dns = {0}; dns.ip.type = ESP_IPADDR_TYPE_V4; dns.ip.u_addr.ip4.addr = ap.ip.addr;
    if ((err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns)) != ESP_OK) return err;
    uint8_t offer_dns = 1;
    if ((err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                    &offer_dns, sizeof(offer_dns))) != ESP_OK) return err;
    return esp_netif_dhcps_start(s_ap_netif);
}
static esp_err_t network_apply_sta_address(void) {
    network_settings_t s; network_settings_load(&s);
    esp_err_t err;
    if (s.use_static) {
        err = esp_netif_dhcpc_stop(s_sta_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) goto done;
        esp_netif_ip_info_t address = {0};
        address.ip.addr = esp_ip4addr_aton(s.ip);
        address.gw.addr = esp_ip4addr_aton(s.gateway);
        address.netmask.addr = esp_ip4addr_aton(s.netmask);
        err = esp_netif_set_ip_info(s_sta_netif, &address);
        if (err != ESP_OK) goto done;
        esp_netif_dns_info_t dns = {0}; dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(s.dns);
        err = esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
        if (err == ESP_OK) ESP_LOGI(TAG, "Static IPv4 configured: %s, DNS: %s", s.ip, s.dns);
    } else {
        /* A previous fixed address must not survive a switch back to DHCP. */
        esp_netif_ip_info_t empty = {0};
        esp_netif_dhcpc_stop(s_sta_netif);
        err = esp_netif_set_ip_info(s_sta_netif, &empty);
        if (err == ESP_OK) err = esp_netif_dhcpc_start(s_sta_netif);
        if (err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) err = ESP_OK;
    }
done:
    memset(&s, 0, sizeof(s)); return err;
}
