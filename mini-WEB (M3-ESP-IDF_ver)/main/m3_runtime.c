#include "m3_sd_apps.h"
#include "m3_music.h"
#include "m3_photos.h"
#include "m3_weather.h"
#include "m3_audio.h"
#include "m3_recorder.h"
#include "m3_gallery.h"
#include "m3_runtime.h"
#include "web_server.h"
#include "m3_web_assets.h"
#include "sd_media.h"
#include "m3_sd_settings.h"
#include "m3_azure_audio.h"
#include "m3_voice.h"
#include "m3_preferences.h"
#include "m3_feedback.h"
#include "m3_i18n.h"
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
typedef struct { m3_wifi_action_t action; network_settings_t settings; bool retry; } wifi_request_t;
static QueueHandle_t browse_queue;
static QueueHandle_t wifi_queue;
typedef struct { char source[256], destination[256]; bool remove, sd; } mutation_t;
static QueueHandle_t mutation_queue;
static atomic_bool mutation_pending;
static atomic_bool wifi_pending;
static atomic_bool web_pending;
static atomic_int web_request = -1;
static bool web_paused, domain_paused;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static m3_runtime_state_t state;
static void publish(const m3_runtime_state_t *next);
static bool setup_requested;
static bool clock_started;
static int64_t next_wifi_retry;
/* Samples each HP core's busy share from the idle tasks' run-time
 * counters (microseconds with the default esp_timer clock), so core placement
 * can be checked on the board with real apps running. IDF 5.5's kernel only
 * reads the calling core's idle counter, so each read runs on its own core. */
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static void read_idle_counter(void *out) { *(configRUN_TIME_COUNTER_TYPE *)out = ulTaskGetIdleRunTimeCounter(); }
#endif
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static unsigned core_busy(uint64_t elapsed, uint64_t idle) {
    if (!elapsed || idle >= elapsed) return 0;
    return 100 - (unsigned)(idle * 100 / elapsed);
}
#endif
static void log_core_load(m3_runtime_state_t *next) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    static int64_t last_us, last_log;
    static configRUN_TIME_COUNTER_TYPE last_idle[2];
    int64_t now = esp_timer_get_time();
    if (last_us && now - last_us < 1000000) return;
    unsigned busy[2] = {0};
    configRUN_TIME_COUNTER_TYPE idle[2] = {0};
    for (int core = 0; core < 2; ++core) {
        if (esp_ipc_call_blocking(core, read_idle_counter, &idle[core]) != ESP_OK) return;
    }
    for (int core = 0; core < 2; ++core) {
        if (last_us && now > last_us) {
            busy[core] = core_busy(now - last_us, (configRUN_TIME_COUNTER_TYPE)(idle[core] - last_idle[core]));
        }
        last_idle[core] = idle[core];
    }
    if (last_us) {
        next->cpu_valid = true;
        next->cpu_percent[0] = busy[0]; next->cpu_percent[1] = busy[1];
        ++next->cpu_revision;
        if (now - last_log >= 30000000) {
            ESP_LOGI("M3", "CPU load: core0 %u%%, core1 %u%%", busy[0], busy[1]); last_log = now;
        }
    }
    last_us = now;
#else
    (void)next;
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
    out->mutation_busy = atomic_load(&mutation_pending);
    out->web_control_busy = atomic_load(&web_pending);
}
bool m3_runtime_web(m3_web_action_t action) {
    if (action < M3_WEB_STOP_DOMAIN || action > M3_WEB_START_ALL || m3_preferences_restarting()) return false;
    portENTER_CRITICAL(&state_lock); bool ready = state.initialized; portEXIT_CRITICAL(&state_lock);
    bool expected = false;
    if (!ready || !atomic_compare_exchange_strong(&web_pending, &expected, true)) return false;
    atomic_store(&web_request, action); return true;
}
/* Only the service worker starts/stops HTTP. Never block the LVGL task on httpd_stop. */
static void web_execute(m3_runtime_state_t *next, int action) {
    esp_err_t result = ESP_OK;
    if (action == M3_WEB_STOP_DOMAIN) {
        domain_paused = true; cloud_relay_set_enabled(false);
    } else if (action == M3_WEB_STOP_ALL) {
        bool locked = !sd_media_ready() || sd_media_admin_lock();
        if (!locked || atomic_load(&mutation_pending)) {
            if (locked && sd_media_ready()) sd_media_admin_unlock();
            result = ESP_ERR_INVALID_STATE;
        } else {
            web_paused = domain_paused = true;
            cloud_relay_set_enabled(false); web_server_stop();
            if (sd_media_ready()) sd_media_admin_unlock();
        }
    } else {
        if (action == M3_WEB_START_ALL) {
            result = network_get_mode() == NET_MODE_IDLE ? ESP_ERR_INVALID_STATE : web_server_start();
            if (result == ESP_OK) web_paused = false;
        } else if (!web_server_is_running()) result = ESP_ERR_INVALID_STATE;
        if (result == ESP_OK) {
            result = cloud_relay_start();
            if (result == ESP_OK) { domain_paused = false; cloud_relay_set_enabled(true); }
        }
    }
    next->web = web_server_is_running(); next->cloud = cloud_relay_is_online();
    next->web_policy = web_paused ? 2 : domain_paused ? 1 : 0;
    next->web_control_result = result; ++next->web_control_revision;
    publish(next); atomic_store(&web_pending, false);
}
bool m3_runtime_file_action(bool sd, const char *source, const char *destination, bool remove) {
    if (!mutation_queue || !source || strlen(source) > 255 || (!remove && (!destination || strlen(destination) > 255))) return false;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&mutation_pending, &expected, true)) return false;
    mutation_t request = {.remove = remove, .sd = sd};
    snprintf(request.source, sizeof(request.source), "%s", source);
    if (!remove) snprintf(request.destination, sizeof(request.destination), "%s", destination);
    if (xQueueSend(mutation_queue, &request, 0) == pdPASS) return true;
    atomic_store(&mutation_pending, false); return false;
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
    if (!wifi_queue || action < M3_WIFI_SCAN || action > M3_WIFI_FORGET_PROFILE ||
        ((action == M3_WIFI_CONNECT || action == M3_WIFI_CONNECT_SAVED || action == M3_WIFI_USE_PROFILE ||
          action == M3_WIFI_FORGET_PROFILE) && !network_settings_valid(settings))) return false;
    portENTER_CRITICAL(&state_lock); bool ready = state.initialized && state.wifi_ready; portEXIT_CRITICAL(&state_lock);
    bool expected = false;
    if (!ready || !atomic_compare_exchange_strong(&wifi_pending, &expected, true)) return false;
    wifi_request_t request = {.action = action};
    if (settings && (action == M3_WIFI_CONNECT || action == M3_WIFI_CONNECT_SAVED ||
        action == M3_WIFI_USE_PROFILE || action == M3_WIFI_FORGET_PROFILE)) request.settings = *settings;
    bool sent = xQueueSend(wifi_queue, &request, 0) == pdPASS;
    memset(&request, 0, sizeof(request));
    if (!sent) atomic_store(&wifi_pending, false);
    return sent;
}
/* Resolve secrets only in the service task; never publish the saved password. */
static bool restore_wifi_password(network_settings_t *settings) {
    network_settings_t saved;
    bool found = network_settings_load(&saved) && saved.pass[0] && !strcmp(saved.ssid, settings->ssid);
    if (!found) found = m3_sd_wifi_find(settings->ssid, &saved) && saved.pass[0];
    if (found) snprintf(settings->pass, sizeof(settings->pass), "%s", saved.pass);
    memset(&saved, 0, sizeof(saved));
    return found;
}
static void wifi_execute(m3_runtime_state_t *next, wifi_request_t *request) {
    snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
        request->action == M3_WIFI_SCAN || request->retry ? "Searching nearby Wi-Fi..." :
        (request->action == M3_WIFI_CONNECT || request->action == M3_WIFI_CONNECT_SAVED) ? "Saving and connecting (up to 15 seconds)..." :
        request->action == M3_WIFI_IPV6 ? "Requesting IPv6 router discovery..." : "Starting setup hotspot...");
    publish(next);
    esp_err_t result = ESP_OK;
    if (request->action == M3_WIFI_SCAN) {
        int count = network_scan_wifi(next->networks, M3_WIFI_RESULTS);
        next->scan_count = count > 0 ? count : 0;
        snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
            count < 0 ? "Scan failed. Enter SSID manually or retry." :
            count == 0 ? "No visible networks. Enter SSID manually." : "Tap a network, enter its password, then connect.");
    } else if (request->action == M3_WIFI_FORGET_PROFILE) {
        result = m3_sd_wifi_forget(request->settings.ssid);
        snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
            result == ESP_OK ? "Saved Wi-Fi removed." : "Could not remove saved Wi-Fi.");
        network_settings_load(&next->settings);
        next->wifi_password_saved = next->settings.pass[0] != 0;
        memset(next->settings.pass, 0, sizeof(next->settings.pass));
        ++next->settings_revision;
    } else if (request->action == M3_WIFI_IPV6) {
        result = network_retry_ipv6();
        snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
            result == ESP_OK ? "IPv6 router discovery requested." : "IPv6 discovery unavailable; connect Wi-Fi first.");
    } else {
        if (request->action == M3_WIFI_CONNECT || request->action == M3_WIFI_CONNECT_SAVED || request->action == M3_WIFI_USE_PROFILE) {
            setup_requested = false;
            result = request->retry ? ESP_OK : request->action == M3_WIFI_USE_PROFILE ?
                (m3_sd_wifi_find(request->settings.ssid, &request->settings) ? ESP_OK : ESP_ERR_NOT_FOUND) :
                request->action == M3_WIFI_CONNECT_SAVED && !restore_wifi_password(&request->settings)
                ? ESP_ERR_INVALID_STATE : network_save_configuration(&request->settings);
            if (result == ESP_OK) {
                next->wifi_password_saved = request->settings.pass[0] != 0;
                next->settings = request->settings;
                memset(next->settings.pass, 0, sizeof(next->settings.pass));
                ++next->settings_revision;
                dns_server_stop();
                result = request->retry ? network_connect_saved() : network_connect_sta(request->settings.ssid, request->settings.pass);
                network_settings_load(&next->settings);
                next->wifi_password_saved = next->settings.pass[0] != 0;
                memset(next->settings.pass, 0, sizeof(next->settings.pass));
                ++next->settings_revision;
                if (result != ESP_OK) {
                    esp_err_t fallback = network_start_softap();
                    if (fallback == ESP_OK) dns_server_start();
                    snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
                        fallback == ESP_OK ? "Connection failed. Setup hotspot restored; check password/IP." :
                        "Connection and setup hotspot failed. Inspect serial log.");
                } else snprintf(next->wifi_message, sizeof(next->wifi_message), "Connected. Settings saved for next boot.");
            } else if (result == ESP_ERR_NO_MEM && next->saved_wifi_count >= M3_WIFI_PROFILE_LIMIT &&
                !m3_sd_wifi_known(request->settings.ssid))
                snprintf(next->wifi_message, sizeof(next->wifi_message), "Saved Wi-Fi is full. Remove a network first.");
            else snprintf(next->wifi_message, sizeof(next->wifi_message), "Save failed: %s", esp_err_to_name(result));
        } else {
            setup_requested = true;
            result = network_start_softap();
            if (result == ESP_OK) dns_server_start();
            snprintf(next->wifi_message, sizeof(next->wifi_message), "%s",
                result == ESP_OK ? "Setup hotspot ready. Saved credentials retained." : "Setup hotspot failed. Inspect serial log.");
        }
        if (!web_paused && !web_server_is_running() && network_get_mode() != NET_MODE_IDLE) {
            web_server_start();
            if (!domain_paused) cloud_relay_start();
        }
    }
    if (request->action != M3_WIFI_SCAN && request->action != M3_WIFI_IPV6 && request->action != M3_WIFI_FORGET_PROFILE)
        m3_feedback_emit(result == ESP_OK ? (network_get_mode() == NET_MODE_STA_CONNECTED ? M3_CUE_WIFI_CONNECTED : M3_CUE_WIFI_SETUP) : M3_CUE_WIFI_FAILED, m3_i18n_language());
    memset(request, 0, sizeof(*request));
    next_wifi_retry = esp_timer_get_time() + 60000000LL;
    next->saved_wifi_count = m3_sd_wifi_profiles(next->saved_wifi, M3_WIFI_PROFILE_LIMIT);
    for (int i = 0; i < next->scan_count; ++i) next->networks[i].saved =
        m3_sd_wifi_matches(next->networks[i].ssid, next->networks[i].authmode == WIFI_AUTH_OPEN);
    ++next->scan_revision;
    ++next->wifi_revision;
    /* Publish completion before releasing the admission gate. */
    publish(next);
    atomic_store(&wifi_pending, false);
}
/* A failed boot/manual attempt must not permanently disable saved Wi-Fi.
 * Keep the setup portal between attempts; explicit setup mode opts out. */
static void retry_saved_wifi(m3_runtime_state_t *next) {
    if (setup_requested || !next->wifi_ready ||
        (network_get_mode() != NET_MODE_AP_CAPTIVE && !network_recovery_pending()) ||
        esp_timer_get_time() < next_wifi_retry ||
        (network_get_mode() == NET_MODE_AP_CAPTIVE && network_setup_has_clients())) return;
    next_wifi_retry = esp_timer_get_time() + 60000000LL;
    wifi_request_t request = {.action = M3_WIFI_CONNECT, .retry = true};
    if (!m3_sd_wifi_profiles(NULL, 0)) return;
    bool expected = false;
    if (atomic_compare_exchange_strong(&wifi_pending, &expected, true)) wifi_execute(next, &request);
    memset(&request, 0, sizeof(request));
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
#if CONFIG_ULP_COPROC_ENABLED
    next.lp_configured = true;
#endif
    snprintf(next.message, sizeof(next.message), "Starting storage and C6 Wi-Fi"); publish(&next);
    m3_boot_splash_step(M3_BOOT_STORAGE);
    next.sd = sd_media_init() == ESP_OK;
    esp_err_t storage = web_server_storage_init();
    if (storage != ESP_OK) ESP_LOGW("M3", "SD file storage: %s", esp_err_to_name(storage));
    m3_web_assets_load(); /* Boot snapshot: before HTTP and cloud workers can read the immutable cache. */
    m3_sd_settings_init();
    m3_weather_init();
    m3_voice_set_key(m3_sd_openai_key());
    if (m3_azure_audio_ready()) { m3_voice_set_provider(3); m3_audio_tts_provider(3); }
    m3_boot_splash_step(M3_BOOT_NETWORK);
    esp_err_t net = network_init();
    next.wifi_ready = net == ESP_OK;
    bool saved_wifi = false, connected = false;
    if (net == ESP_OK) {
        saved_wifi = !setup_requested && m3_sd_wifi_profiles(NULL, 0) > 0;
        ESP_LOGI("M3", "Boot Wi-Fi: SD profiles=%u, explicit setup=%d", m3_sd_wifi_profiles(NULL, 0), setup_requested);
        if (saved_wifi) m3_boot_splash_step(M3_BOOT_CONNECT);
        connected = saved_wifi && network_connect_saved() == ESP_OK;
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
    m3_feedback_emit(M3_CUE_READY, m3_i18n_language());
    m3_feedback_emit(connected ? M3_CUE_WIFI_CONNECTED : saved_wifi ? M3_CUE_WIFI_FAILED : M3_CUE_WIFI_SETUP, m3_i18n_language());
    next.initialized = true;
    m3_boot_splash_step(M3_BOOT_READY);
    network_settings_load(&next.settings);
    next.wifi_password_saved = next.settings.pass[0] != 0;
    memset(next.settings.pass, 0, sizeof(next.settings.pass));
    next_wifi_retry = esp_timer_get_time() + 60000000LL;
    next.settings_revision = 1;
    next.saved_wifi_count = m3_sd_wifi_profiles(next.saved_wifi, M3_WIFI_PROFILE_LIMIT);
    snprintf(next.wifi_message, sizeof(next.wifi_message), "%s", connected ? "Connected. Settings saved for next boot." :
        saved_wifi ? "Connection failed. Setup hotspot restored; check password/IP." :
        "Enter Wi-Fi details here, or use the setup hotspot.");
    snprintf(next.message, sizeof(next.message), "%s", net == ESP_OK ? "Services ready" : "C6 Wi-Fi failed; inspect serial log");
    browse_t browse = {.sd = true};
    bool refresh = true;
    unsigned tick = 0;
    for (;;) {
        if (xQueueReceive(browse_queue, &browse, pdMS_TO_TICKS(1000)) == pdTRUE) refresh = true;
        mutation_t mutation;
        if (xQueueReceive(mutation_queue, &mutation, 0) == pdTRUE) {
            m3_audio_state_t audio; m3_audio_state(&audio);
            m3_recorder_state_t recording; m3_recorder_state(&recording);
            bool locked = !recording.busy && !audio.playing && !m3_gallery_active() && (!mutation.sd || sd_media_admin_lock());
            next.mutation_result = locked ? storage_view_mutate(mutation.sd, mutation.source, mutation.destination, mutation.remove) : ESP_ERR_INVALID_STATE;
            if (locked) {
                if (next.mutation_result == ESP_OK) { if (mutation.sd) sd_media_changed(); m3_music_refresh(mutation.sd); m3_photos_refresh(); }
                if (mutation.sd) sd_media_admin_unlock();
            }
            ++next.mutation_revision; refresh = true; publish(&next);
            atomic_store(&mutation_pending, false);
        }
        m3_preferences_poll();
        m3_typography_poll();
        m3_power_poll(&next.power);
        m3_feedback_poll();
        log_core_load(&next);
        int web_action = atomic_exchange(&web_request, -1);
        if (web_action >= 0) web_execute(&next, web_action);
        if (!m3_preferences_web_active()) { m3_music_poll(); m3_photos_poll(); m3_sd_apps_poll(); }
        wifi_request_t request;
        if (xQueueReceive(wifi_queue, &request, 0) == pdTRUE) wifi_execute(&next, &request);
        retry_saved_wifi(&next);
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
        if (!m3_preferences_web_active() && (refresh || (++tick % 5 == 0))) {
            storage_capacity_read(false, &next.flash);
            if (!next.sd) {
                memset(&next.sd_capacity, 0, sizeof(next.sd_capacity)); next.sd_capacity.error = ESP_ERR_INVALID_STATE;
                next.sd_capacity_stale = false;
            } else if (sd_media_admin_lock()) {
                storage_capacity_read(true, &next.sd_capacity);
                next.sd_capacity_stale = false;
                sd_media_admin_unlock();
            } else {
                next.sd_capacity_stale = true;
                if (!next.sd_capacity.total) next.sd_capacity.error = ESP_ERR_INVALID_STATE;
            }
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
    mutation_queue = xQueueCreate(1, sizeof(mutation_t));
    if (!browse_queue || !wifi_queue || !mutation_queue) {
        if (browse_queue) vQueueDelete(browse_queue);
        if (wifi_queue) vQueueDelete(wifi_queue);
        if (mutation_queue) vQueueDelete(mutation_queue);
        browse_queue = wifi_queue = mutation_queue = NULL; return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(services, "m3_services", 12288, NULL, 3, NULL, 0) != pdPASS) {
        vQueueDelete(mutation_queue); mutation_queue = NULL;
        vQueueDelete(browse_queue); vQueueDelete(wifi_queue);
        browse_queue = wifi_queue = mutation_queue = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
