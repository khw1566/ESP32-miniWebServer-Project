/* Product revision is independent of firmware version to keep printed URLs stable. */
#include "esp_mac.h"
#include "mdns.h"
#include <ctype.h>
#define PRODUCT_ADDRESS_VERSION "1-2"
#if CONFIG_IDF_TARGET_ESP32P4
#define PRODUCT_ADDRESS_NAME "p4"
#elif CONFIG_IDF_TARGET_ESP32C3
#define PRODUCT_ADDRESS_NAME "c3"
#elif CONFIG_IDF_TARGET_ESP32S3
#define PRODUCT_ADDRESS_NAME "s3"
#elif CONFIG_IDF_TARGET_ESP32
#define PRODUCT_ADDRESS_NAME "m1"
#else
#error "Product address supports ESP32-C3 and ESP32-S3"
#endif
static char s_product_serial[13];
static char s_product_revision[17];
static char s_product_hostname[64];
static bool s_mdns_started = false;
static esp_err_t network_identity_init(void) {
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) return err;
    snprintf(s_product_serial, sizeof(s_product_serial), "%02x%02x%02x%02x%02x%02x",
             (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    snprintf(s_product_revision, sizeof(s_product_revision), "%s", PRODUCT_ADDRESS_VERSION);
    nvs_handle_t h;
    err = nvs_open("product_id", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        char saved[sizeof(s_product_revision)] = {0}; size_t length = sizeof(saved);
        bool valid = nvs_get_str(h, "addr_version", saved, &length) == ESP_OK && saved[0] &&
                     memchr(saved, 0, sizeof(saved)) != NULL;
        for (size_t i = 0; valid && saved[i]; ++i)
            if (!((saved[i] >= '0' && saved[i] <= '9') || (saved[i] >= 'a' && saved[i] <= 'z') || saved[i] == '-')) valid = false;
        if (valid && (saved[0] == '-' || saved[strlen(saved) - 1] == '-')) valid = false;
        if (valid) snprintf(s_product_revision, sizeof(s_product_revision), "%s", saved);
        else {
            err = nvs_set_str(h, "addr_version", s_product_revision);
            if (err == ESP_OK) err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "Product revision persistence failed: %s", esp_err_to_name(err));
    snprintf(s_product_hostname, sizeof(s_product_hostname), "%s-v%s-%s",
             PRODUCT_ADDRESS_NAME, s_product_revision, s_product_serial);
    return ESP_OK;
}
const char *network_get_product_name(void) { return PRODUCT_ADDRESS_NAME; }
const char *network_get_product_serial(void) { return s_product_serial; }
const char *network_get_product_revision(void) { return s_product_revision; }
bool network_mdns_is_ready(void) { return s_mdns_started; }
esp_err_t network_get_mdns_url(char *out, size_t length) {
    if (!out || !length || !s_product_hostname[0]) return ESP_ERR_INVALID_ARG;
    char actual[MDNS_NAME_BUF_LEN]; const char *hostname = s_product_hostname;
    /* Include library conflict resolution in the UI instead of displaying a stale name. */
    if (s_mdns_started && mdns_hostname_get(actual) == ESP_OK && actual[0]) hostname = actual;
    int needed = snprintf(out, length, "http://%s.local/", hostname);
    return needed >= 0 && (size_t)needed < length ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
esp_err_t network_mdns_start(void) {
    if (s_mdns_started) return ESP_OK;
    if (!s_product_hostname[0]) return ESP_ERR_INVALID_STATE;
    esp_err_t err = mdns_init();
    if (err != ESP_OK) return err;
    err = mdns_hostname_set(s_product_hostname);
    if (err == ESP_OK) err = mdns_instance_name_set(s_product_hostname);
    mdns_txt_item_t txt[] = {
        {"path", "/"}, {"product", PRODUCT_ADDRESS_NAME},
        {"revision", s_product_revision}, {"serial", s_product_serial}
    };
    if (err == ESP_OK) err = mdns_service_add(NULL, "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) { mdns_free(); return err; }
    s_mdns_started = true;
    ESP_LOGI(TAG, "[mDNS] Product URL: http://%s.local/", s_product_hostname);
    return ESP_OK;
}
void network_mdns_stop(void) {
    if (s_mdns_started) { s_mdns_started = false; mdns_free(); }
}
