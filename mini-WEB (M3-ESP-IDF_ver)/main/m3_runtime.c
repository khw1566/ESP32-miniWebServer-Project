#include "m3_music.h"
#include "m3_photos.h"
#include "m3_runtime.h"
#include "web_server.h"
#include "sd_media.h"
#include "cloud_relay.h"
#include "dns_server.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ipc.h"
#include "m3_boot_splash.h"
#include "esp_netif_sntp.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
typedef struct { bool sd; unsigned offset; char folder[256]; } browse_t;
typedef struct { m3_wifi_action_t action; network_settings_t settings; } wifi_request_t;
static QueueHandle_t browse_queue;
static QueueHandle_t wifi_queue;
static atomic_bool wifi_pending;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static m3_runtime_state_t state;
static bool setup_requested;
static bool clock_started;
/* Logs each HP core's busy share every 30 s from the idle tasks' run-time
 * counters (microseconds with the default esp_timer clock), so core placement
 * can be checked on the board with real apps running. IDF 5.5's kernel only
 * reads the calling core's idle counter, so each read runs on its own core. */
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static void read_idle_counter(void *out) { *(configRUN_TIME_COUNTER_TYPE *)out = ulTaskGetIdleRunTimeCounter(); }
#endif
static void log_core_load(void) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    static int64_t last_us;
    static configRUN_TIME_COUNTER_TYPE last_idle[2];
    int64_t now = esp_timer_get_time();
    if (last_us && now - last_us < 30000000) return;
    unsigned busy[2] = {0};
    for (int core = 0; core < 2; ++core) {
        configRUN_TIME_COUNTER_TYPE idle = 0;
        if (esp_ipc_call_blocking(core, read_idle_counter, &idle) != ESP_OK) return;
        if (last_us && now > last_us) {
            uint64_t share = (uint64_t)(idle - last_idle[core]) * 100 / (uint64_t)(now - last_us);
            busy[core] = share >= 100 ? 0 : 100 - (unsigned)share;
        }
        last_idle[core] = idle;
    }
    if (last_us) ESP_LOGI("M3", "CPU load: core0 %u%%, core1 %u%%", busy[0], busy[1]);
    last_us = now;
#endif
}
/* One service task owns SNTP, including when cloud access is unprovisioned. */
static void start_network_clock(void) {
    if (clock_started || network_get_mode() != NET_MODE_STA_CONNECTED) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    clock_started = err == ESP_OK || err == ESP_ERR_INVALID_STATE;
}
void m3_runtime_state(m3_runtime_state_t *out) {
    portENTER_CRITICAL(&state_lock); *out = state; portEXIT_CRITICAL(&state_lock);
    out->wifi_busy = atomic_load(&wifi_pending);
}
static void publish(const m3_runtime_state_t *next) {
    portENTER_CRITICAL(&state_lock); state = *next; portEXIT_CRITICAL(&state_lock);
}
bool m3_runtime_browse(bool sd, const char *folder, unsigned offset) {
    if (!browse_queue || !folder || strlen(folder) > 255 || strstr(folder, "..") ||
        (folder[0] && folder[0] != '/') || (!sd && folder[0])) return false;
    browse_t request = {.sd = sd, .offset = offset};
    snprintf(request.folder, sizeof(request.folder), "%s", folder);
    return xQueueOverwrite(browse_queue, &request) == pdPASS;
}
bool m3_runtime_wifi(m3_wifi_action_t action, const network_settings_t *settings) {
    if (!wifi_queue || action < M3_WIFI_SCAN || action > M3_WIFI_IPV6 ||
        (action == M3_WIFI_CONNECT && !network_settings_valid(settings))) return false;
    portENTER_CRITICAL(&state_lock); bool ready = state.initialized && state.wifi_ready; portEXIT_CRITICAL(&state_lock);
    bool expected = false;
    if (!ready || !atomic_compare_exchange_strong(&wifi_pending, &expected, true)) return false;
    wifi_request_t request = {.action = action};
    if (settings && action == M3_WIFI_CONNECT) request.settings = *settings;
    bool sent = xQueueSend(wifi_queue, &request, 0) == pdPASS;
    memset(&request, 0, sizeof(request));
    if (!sent) atomic_store(&wifi_pending, false);
    return sent;
}
static void wifi_execute(m3_runtime_state_t *next, wifi_request_t *request) {
    snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
        request->action == M3_WIFI_SCAN ? "Searching nearby Wi-Fi..." :
        request->action == M3_WIFI_CONNECT ? "Saving and connecting (up to 15 seconds)..." :
        request->action == M3_WIFI_IPV6 ? "Requesting IPv6 router discovery..." : "Starting setup hotspot...");
    publish(next);
    esp_err_t result = ESP_OK;
    if (request->action == M3_WIFI_SCAN) {
        int count = network_scan_wifi(next->networks, M3_WIFI_RESULTS);
        next->scan_count = count > 0 ? count : 0;
        ++next->scan_revision;
        snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
            count < 0 ? "Scan failed. Enter SSID manually or retry." :
            count == 0 ? "No visible networks. Enter SSID manually." : "Tap a network, enter its password, then connect.");
    } else if (request->action == M3_WIFI_IPV6) {
        result = network_retry_ipv6();
        snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
            result == ESP_OK ? "IPv6 router discovery requested." : "IPv6 discovery unavailable; connect Wi-Fi first.");
    } else {
        if (request->action == M3_WIFI_CONNECT) {
            result = network_save_configuration(&request->settings);
            if (result == ESP_OK) {
                next->settings = request->settings;
                memset(next->settings.pass, 0, sizeof(next->settings.pass));
                ++next->settings_revision;
                dns_server_stop();
                result = network_connect_sta(request->settings.ssid, request->settings.pass);
                if (result != ESP_OK) {
                    esp_err_t fallback = network_start_softap();
                    if (fallback == ESP_OK) dns_server_start();
                    snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
                        fallback == ESP_OK ? "Connection failed. Setup hotspot restored; check password/IP." :
                        "Connection and setup hotspot failed. Inspect serial log.");
                } else snprintf(next->wifi_message, sizeof(next->wifi_message), "Connected. Settings saved for next boot.");
            } else snprintf(next->wifi_message, sizeof(next->wifi_message), "Save failed: %s", esp_err_to_name(result));
        } else {
            result = network_start_softap();
            if (result == ESP_OK) dns_server_start();
            snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
                result == ESP_OK ? "Setup hotspot ready. Saved credentials retained." : "Setup hotspot failed. Inspect serial log.");
        }
        if (!web_server_is_running() && network_get_mode() != NET_MODE_IDLE) {
            web_server_start();
            cloud_relay_start();
        }
    }
    memset(request, 0, sizeof(*request));
    ++next->wifi_revision;
    /* Publish completion before releasing the admission gate. */
    publish(next);
    atomic_store(&wifi_pending, false);
}
/* Reject sentinel text, link-local and ULA; a public QR requires a current global address. */
static bool ipv6_url(const char *address, char *out, size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = 0;
    struct in6_addr parsed;
    if (!address || inet_pton(AF_INET6, address, &parsed) != 1 || (parsed.s6_addr[0] & 0xe0) != 0x20) return false;
    int n = snprintf(out, capacity, "http://[%s]/", address);
    if (n < 0 || (size_t)n >= capacity) { out[0] = 0; return false; }
    return true;
}
static void services(void *unused) {
    (void)unused;
    m3_runtime_state_t next = {0};
    snprintf(next.message, sizeof(next.message), "Starting storage and C6 Wi-Fi"); publish(&next);
    m3_boot_splash_step(M3_BOOT_STORAGE);
    esp_err_t storage = web_server_storage_init();
    if (storage != ESP_OK) ESP_LOGW("M3", "Flash storage: %s", esp_err_to_name(storage));
    next.sd = sd_media_init() == ESP_OK;
    m3_boot_splash_step(M3_BOOT_NETWORK);
    esp_err_t net = network_init();
    next.wifi_ready = net == ESP_OK;
    if (net == ESP_OK) {
        char ssid[64] = {0}, pass[64] = {0};
        bool saved = !setup_requested && network_has_saved_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
        if (saved) m3_boot_splash_step(M3_BOOT_CONNECT);
        bool connected = saved && network_connect_sta(ssid, pass) == ESP_OK;
        memset(pass, 0, sizeof(pass));
        if (!connected) {
            net = network_start_softap();
            if (net == ESP_OK) dns_server_start();
        }
        if (net == ESP_OK) {
            m3_boot_splash_step(M3_BOOT_WEB);
            next.web = web_server_start() == ESP_OK;
            cloud_relay_start();
        }
    }
    next.initialized = true;
    m3_boot_splash_step(M3_BOOT_READY);
    network_settings_load(&next.settings);
    memset(next.settings.pass, 0, sizeof(next.settings.pass));
    next.settings_revision = 1;
    snprintf(next.wifi_message, sizeof(next.wifi_message), "Enter Wi-Fi details here, or use the setup hotspot.");
    snprintf(next.message, sizeof(next.message), "%s", net == ESP_OK ? "Services ready" : "C6 Wi-Fi failed; inspect serial log");
    browse_t browse = {.sd = false};
    bool refresh = true;
    unsigned tick = 0;
    for (;;) {
        if (xQueueReceive(browse_queue, &browse, pdMS_TO_TICKS(1000)) == pdTRUE) refresh = true;
        m3_music_poll();
        log_core_load();
        m3_photos_poll();
        wifi_request_t request;
        if (xQueueReceive(wifi_queue, &request, 0) == pdTRUE) wifi_execute(&next, &request);
        next.mode = network_get_mode();
        start_network_clock();
        network_get_ip_str(next.ip, sizeof(next.ip));
        snprintf(next.url, sizeof(next.url), "http://%s/%s", next.ip[0] ? next.ip : "192.168.4.1",
                 next.mode == NET_MODE_AP_CAPTIVE ? "setup" : "");
        if (next.mode != NET_MODE_AP_CAPTIVE && next.mode != NET_MODE_STA_CONNECTED) next.url[0] = 0;
        next.ipv6_url[0] = 0;
        if (next.mode == NET_MODE_STA_CONNECTED) {
            char address[64] = {0};
            if (network_get_ip6_str(address, sizeof(address)) == ESP_OK)
                ipv6_url(address, next.ipv6_url, sizeof(next.ipv6_url));
        }
        snprintf(next.ipv6_status, sizeof(next.ipv6_status), "%s", network_get_ipv6_status());
        next.mdns_ready = network_mdns_is_ready();
        next.mdns_url[0] = 0;
        if (next.mdns_ready && network_get_mdns_url(next.mdns_url, sizeof(next.mdns_url)) != ESP_OK) next.mdns_url[0] = 0;
        next.rssi = next.mode == NET_MODE_STA_CONNECTED ? network_get_rssi() : 0;
        next.web = web_server_is_running(); next.cloud = cloud_relay_is_online();
        if (!next.cloud || cloud_relay_get_url(next.cloud_url, sizeof(next.cloud_url)) != ESP_OK) next.cloud_url[0] = 0;
        if (cloud_relay_get_public_url(next.domain_url, sizeof(next.domain_url)) != ESP_OK) next.domain_url[0] = 0;
        snprintf(next.cloud_status, sizeof(next.cloud_status), "%s", cloud_relay_status());
        next.requests = web_server_home_requests(); next.disconnects = network_get_disconnect_count();
        next.sd = sd_media_ready();
        if (refresh || (++tick % 5 == 0)) {
            storage_capacity_read(false, &next.flash);
            bool locked = !browse.sd || sd_media_admin_lock();
            if (locked) {
                storage_view_read(browse.sd, browse.folder, browse.offset, &next.files);
                if (browse.sd) sd_media_admin_unlock();
            } else {
                memset(&next.files, 0, sizeof(next.files)); next.files.error = ESP_ERR_INVALID_STATE;
            }
            next.browse_sd = browse.sd;
            snprintf(next.folder, sizeof(next.folder), "%s", browse.folder);
            ++next.file_revision;
            refresh = false;
        }
        publish(&next);
    }
}
esp_err_t m3_runtime_start(bool force_setup) {
    setup_requested = force_setup;
    browse_queue = xQueueCreate(1, sizeof(browse_t));
    wifi_queue = xQueueCreate(1, sizeof(wifi_request_t));
    if (!browse_queue || !wifi_queue) {
        if (browse_queue) vQueueDelete(browse_queue);
        if (wifi_queue) vQueueDelete(wifi_queue);
        browse_queue = wifi_queue = NULL; return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(services, "m3_services", 12288, NULL, 3, NULL, 0) != pdPASS) {
        vQueueDelete(browse_queue); vQueueDelete(wifi_queue);
        browse_queue = wifi_queue = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
