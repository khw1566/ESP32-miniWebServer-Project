#include "network_manager.h"
#include "m3_c6_update.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/netif.h"
#include "lwip/nd6.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "NET_MGR";

static _Atomic net_mode_t s_net_mode = NET_MODE_IDLE;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static int s_retry_num = 0;
static atomic_uint s_diagnostic_disconnects;
static atomic_uint s_diagnostic_last_reason;
unsigned network_get_disconnect_count(void) { return atomic_load(&s_diagnostic_disconnects); }
unsigned network_get_last_disconnect_reason(void) { return atomic_load(&s_diagnostic_last_reason); }
static char s_current_ip[32] = "0.0.0.0";
static bool s_sta_link_up = false;
ESP_EVENT_DEFINE_BASE(NET_IPV6_EVENT);

// STA 연결 시도 활성화 여부 플래그
// (SoftAP 모드에서 STA_START 이벤트 발생 시 자동 connect 차단)
static atomic_bool s_sta_connect_requested = false;
static atomic_bool s_auto_retry;
static esp_timer_handle_t s_recovery_timer;
ESP_EVENT_DEFINE_BASE(NET_RECOVERY_EVENT);

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_STA_STARTED_BIT BIT2
#define WIFI_MAX_RETRY     5
static EventGroupHandle_t s_wifi_event_group;

#include "network_mdns.inc"
static esp_err_t network_apply_sta_address(void);
static esp_err_t network_apply_setup_ap_address(void);

/* Recovery runs on the Wi-Fi event loop, not on the display or timer task. */
static void recovery_tick(void *arg) {
    esp_event_post(NET_RECOVERY_EVENT, 0, NULL, 0, 0);
}
static void recovery_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (!s_auto_retry || !s_sta_connect_requested || s_sta_link_up ||
        s_net_mode != NET_MODE_STA_CONNECTING || s_retry_num < WIFI_MAX_RETRY) return;
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    if (esp_wifi_connect() != ESP_OK) s_retry_num = WIFI_MAX_RETRY;
}

/* Run all lwIP mutations in the TCP/IP task, after default Wi-Fi handlers. */
static esp_err_t ipv6_start_tcpip(void *ctx) {
#if CONFIG_LWIP_IPV6 && CONFIG_LWIP_IPV6_AUTOCONFIG
    struct netif *netif = esp_netif_get_netif_impl((esp_netif_t *)ctx);
    if (!netif || !s_sta_link_up || !netif_is_up(netif) || !netif_is_link_up(netif)) return ESP_ERR_INVALID_STATE;
    netif_set_ip6_autoconfig_enabled(netif, 1);
    /* Recreating an existing preferred address would restart duplicate checks. */
    if (ip6_addr_isinvalid(netif_ip6_addr_state(netif, 0))) netif_create_ip6_linklocal_address(netif, 1);
    nd6_restart_netif(netif); /* Restart Router Solicitation, not just link-local. */
    return ESP_OK;
#else
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
static void ipv6_start_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    if (!s_sta_netif || !s_sta_link_up) return;
    esp_err_t err = esp_netif_tcpip_exec(ipv6_start_tcpip, s_sta_netif);
    if (err == ESP_OK) ESP_LOGI(TAG, "[IPv6] SLAAC enabled; Router Solicitation scheduled on STA");
    else ESP_LOGW(TAG, "[IPv6] Start failed: %s", esp_err_to_name(err));
}

// -------------------------------------------------------------
// 이벤트 핸들러
// -------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "[AP] client associated, heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        ESP_LOGI(TAG, "[AP] client IPv4 assigned, heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_STARTED_BIT);
        // s_sta_connect_requested 플래그로 제어: STA 연결 요청이 있을 때만 connect 호출
        // SoftAP 모드에서 내부적으로 STA 인터페이스가 시작되어도 connect가 호출되지 않도록 차단
        if (s_sta_connect_requested) {
            ESP_LOGI(TAG, "[EVT] STA_START -> esp_wifi_connect() 호출");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_connect() 실패: 0x%x", err);
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            s_net_mode = NET_MODE_STA_CONNECTING;
        } else {
            ESP_LOGD(TAG, "[EVT] STA_START 수신 (연결 요청 없음, 무시)");
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_STA_STARTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        if (!s_sta_connect_requested) return;
        s_sta_link_up = true;
        if (s_sta_netif) {
            /* Queue behind default STA_CONNECTED handlers that bring netif up. */
            esp_err_t err = esp_event_post(NET_IPV6_EVENT, 0, NULL, 0, 0);
            if (err != ESP_OK) ESP_LOGW(TAG, "[IPv6] Request queue failed: %s", esp_err_to_name(err));
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        atomic_fetch_add(&s_diagnostic_disconnects, 1);
        if (event_data) atomic_store(&s_diagnostic_last_reason, ((wifi_event_sta_disconnected_t *)event_data)->reason);
        s_sta_link_up = false;
        /* Deliberate STA stop during SoftAP handover is not a connection failure. */
        if (!s_sta_connect_requested) return;
        snprintf(s_current_ip, sizeof(s_current_ip), "0.0.0.0");
        s_net_mode = NET_MODE_STA_CONNECTING;
        wifi_event_sta_disconnected_t *dc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "[EVT] STA_DISCONNECTED reason=%d, retry=%d/%d",
                 dc->reason, s_retry_num, WIFI_MAX_RETRY);

        if (s_sta_connect_requested && s_retry_num < WIFI_MAX_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "재연결 시도 (%d/%d)...", s_retry_num, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            if (!s_auto_retry) s_sta_connect_requested = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "연결 최종 실패. FAIL_BIT 설정.");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (!s_sta_connect_requested) return;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_current_ip, sizeof(s_current_ip));
        ESP_LOGI(TAG, "[EVT] IP 할당 완료: %s", s_current_ip);
        s_retry_num = 0;
        s_net_mode = NET_MODE_STA_CONNECTED;
        s_auto_retry = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        if (event->esp_netif != s_sta_netif || !s_sta_link_up) return;
        char address[64];
        const ip6_addr_t *ip = (const ip6_addr_t *)&event->ip6_info.ip;
        ip6addr_ntoa_r(ip, address, sizeof(address));
        uint32_t first = lwip_ntohl(ip->addr[0]);
        const char *type = ip6_addr_islinklocal(ip) ? "link-local (LAN only)" :
                          ((first & 0xE0000000UL) == 0x20000000UL) ? "global" : "private/local";
        ESP_LOGI(TAG, "[EVT] STA IPv6 assigned: [%s], type=%s", address, type);
    }
}

// -------------------------------------------------------------
// 초기화
// -------------------------------------------------------------
esp_err_t network_init(void) {
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        NET_RECOVERY_EVENT, ESP_EVENT_ANY_ID, &recovery_event, NULL, NULL), TAG, "recovery event");
    const esp_timer_create_args_t recovery_args = {.callback = recovery_tick, .name = "wifi_recovery"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&recovery_args, &s_recovery_timer), TAG, "recovery timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_recovery_timer, 30ULL * 1000000), TAG, "recovery timer start");

    /* Flash a C6 image staged on SD before Wi-Fi RPCs depend on its version. */
    esp_err_t c6_update = m3_c6_update_from_sd();
    if (c6_update != ESP_OK && c6_update != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "C6 update skipped: %s", esp_err_to_name(c6_update));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* A missing/incompatible C6 must leave the local display running. */
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "C6 Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");
    ESP_RETURN_ON_ERROR(network_identity_init(), TAG, "M3 identity");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL), TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL), TAG, "IPv4 events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_GOT_IP6, &wifi_event_handler, NULL, NULL), TAG, "IPv6 events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL), TAG, "AP events");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        NET_IPV6_EVENT, ESP_EVENT_ANY_ID, &ipv6_start_event, NULL, NULL), TAG, "IPv6 recovery events");
#if !CONFIG_LWIP_IPV6_AUTOCONFIG
    ESP_LOGW(TAG, "[IPv6] SLAAC disabled in sdkconfig; global address autoconfiguration unavailable");
#endif
    return ESP_OK;
}

net_mode_t network_get_mode(void) {
    return s_net_mode;
}
bool network_setup_has_clients(void) {
    wifi_sta_list_t clients = {0};
    /* Leave an active setup session alone, including when the query fails. */
    return esp_wifi_ap_get_sta_list(&clients) != ESP_OK || clients.num > 0;
}

// -------------------------------------------------------------
// SoftAP (캡티브 포털) 시작
// -------------------------------------------------------------
esp_err_t network_start_softap(void) {
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) return ESP_ERR_NO_MEM;
    }
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif) return ESP_ERR_NO_MEM;
        esp_err_t hostname_err = esp_netif_set_hostname(s_sta_netif, s_product_hostname);
        if (hostname_err != ESP_OK) ESP_LOGW(TAG, "DHCP hostname setup failed: %s", esp_err_to_name(hostname_err));
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = SOFTAP_SSID_DEFAULT,
            .ssid_len = strlen(SOFTAP_SSID_DEFAULT),
            .channel = SOFTAP_CHANNEL,
            .password = SOFTAP_PASS_DEFAULT,
            .max_connection = SOFTAP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    // STA 연결 요청 비활성화: SoftAP 모드에서 STA_START 이벤트 발생해도 connect 차단
    s_auto_retry = false;
    s_sta_connect_requested = false;
    s_sta_link_up = false;
    s_net_mode = NET_MODE_IDLE;
    network_mdns_stop();
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) return err;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %d", err);
        return err;
    }
    err = network_apply_setup_ap_address();
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %d", err);
        return err;
    }

    strncpy(s_current_ip, "192.168.4.1", sizeof(s_current_ip));
    s_net_mode = NET_MODE_AP_CAPTIVE;
    ESP_LOGI(TAG, "SoftAP 시작. SSID: %s, IP: %s", SOFTAP_SSID_DEFAULT, s_current_ip);
    return ESP_OK;
}

// -------------------------------------------------------------
// STA 모드 Wi-Fi 연결 (단일 진입점)
// -------------------------------------------------------------
esp_err_t network_connect_sta(const char *ssid, const char *pass) {
    if (!ssid || strlen(ssid) == 0 || strlen(ssid)>32 || (pass && strlen(pass)>63)) {
        ESP_LOGE(TAG, "SSID가 비어있음");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (!s_sta_netif) return ESP_ERR_NO_MEM;
        esp_err_t hostname_err = esp_netif_set_hostname(s_sta_netif, s_product_hostname);
        if (hostname_err != ESP_OK) ESP_LOGW(TAG, "DHCP hostname setup failed: %s", esp_err_to_name(hostname_err));
    }

    char saved_ssid[33] = {0}, saved_pass[65] = {0};
    bool auto_retry = network_has_saved_credentials(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass)) &&
        !strcmp(saved_ssid, ssid) && !strcmp(saved_pass, pass ? pass : "");
    memset(saved_pass, 0, sizeof(saved_pass));
    s_sta_connect_requested = false;
    s_auto_retry = false;
    s_sta_link_up = false;

    // Wi-Fi 설정 구성
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    if (pass && strlen(pass) > 0) {
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.pmf_cfg.capable  = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Wi-Fi 스택이 이미 실행 중인 경우 정지 후 재설정
    wifi_mode_t cur_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&cur_mode);
    if (cur_mode != WIFI_MODE_NULL) {
        ESP_LOGI(TAG, "기존 Wi-Fi 정지 후 재구성 (현재 모드: %d)", cur_mode);
        s_sta_connect_requested = false;
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* Discard old stop/disconnect results before starting this attempt. */
    s_retry_num = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_auto_retry = auto_retry;
    s_net_mode = NET_MODE_STA_CONNECTING;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) 실패: %d", err);
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config 실패: %d", err);
        return err;
    }

    err = network_apply_sta_address();
    if (err != ESP_OK) return err;

    // 플래그 먼저 세팅 후 start → STA_START 이벤트에서 connect 호출됨
    s_sta_connect_requested = true;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start 실패: %d", err);
        s_sta_connect_requested = false;
        return err;
    }

    ESP_LOGI(TAG, "Wi-Fi 시작 완료. SSID: [%s] 연결 대기 중 (최대 15s)...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "연결 성공: %s (IP: %s)", ssid, s_current_ip);
        return ESP_OK;
    } else {
        if (!s_auto_retry) s_sta_connect_requested = false;
        else s_retry_num = WIFI_MAX_RETRY;
        ESP_LOGW(TAG, "연결 실패 또는 타임아웃");
        esp_wifi_disconnect();
        return ESP_FAIL;
    }
}

// -------------------------------------------------------------
// NVS: Wi-Fi 자격증명 저장
// -------------------------------------------------------------
#include "network_settings.inc"

esp_err_t network_get_ip_str(char *out_ip, size_t ip_len) {
    if (!out_ip) return ESP_ERR_INVALID_ARG;
    strncpy(out_ip, s_current_ip, ip_len - 1);
    out_ip[ip_len - 1] = '\0';
    return ESP_OK;
}

int8_t network_get_rssi(void) {
    if (s_net_mode == NET_MODE_STA_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return ap_info.rssi;
        }
    }
    return 0;
}

// Only return a currently preferred global unicast address for external URLs.
esp_err_t network_get_ip6_str(char *out_ip6, size_t ip6_len) {
    if (!out_ip6 || ip6_len == 0) return ESP_ERR_INVALID_ARG;
    if (s_sta_netif && s_sta_link_up) {
        esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
        int count = esp_netif_get_all_preferred_ip6(s_sta_netif, addresses);
        for (int i = 0; i < count; i++) {
            // 2000::/3: exclude link-local fe80::/10 and private fc00::/7.
            uint32_t first = lwip_ntohl(addresses[i].addr[0]);
            if ((first & 0xE0000000UL) == 0x20000000UL) {
                ip6addr_ntoa_r((const ip6_addr_t *)&addresses[i], out_ip6, ip6_len);
                return ESP_OK;
            }
        }
    }
    // Do not reuse a cached address after disconnect or prefix expiry.
    snprintf(out_ip6, ip6_len, "미발급");
    return ESP_OK;
}

esp_err_t network_get_ip6_linklocal_str(char *out_ip6, size_t ip6_len) {
    if (!out_ip6 || ip6_len == 0) return ESP_ERR_INVALID_ARG;
    if (s_sta_netif && s_sta_link_up) {
        esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
        int count = esp_netif_get_all_preferred_ip6(s_sta_netif, addresses);
        for (int i = 0; i < count; i++) {
            if (ip6_addr_islinklocal((const ip6_addr_t *)&addresses[i])) {
                ip6addr_ntoa_r((const ip6_addr_t *)&addresses[i], out_ip6, ip6_len);
                return ESP_OK;
            }
        }
    }
    snprintf(out_ip6, ip6_len, "미발급");
    return ESP_OK;
}

const char *network_get_ipv6_status(void) {
#if !CONFIG_LWIP_IPV6_AUTOCONFIG
    return "disabled";
#else
    if (!s_sta_netif || !s_sta_link_up) return "not_connected";
    char global[64], local[64];
    network_get_ip6_str(global, sizeof(global));
    if (strcmp(global, "미발급")) return "global_ready";
    network_get_ip6_linklocal_str(local, sizeof(local));
    return strcmp(local, "미발급") ? "no_global_prefix" : "linklocal_pending";
#endif
}

esp_err_t network_retry_ipv6(void) {
    if (!s_sta_netif || !s_sta_link_up) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_netif_tcpip_exec(ipv6_start_tcpip, s_sta_netif);
    if (err == ESP_OK) ESP_LOGI(TAG, "[IPv6] Router Solicitation restarted; waiting for router advertisement and global prefix");
    else ESP_LOGW(TAG, "[IPv6] Retry failed: %s", esp_err_to_name(err));
    return err;
}

// -------------------------------------------------------------
// Wi-Fi 스캔
// -------------------------------------------------------------
int network_scan_wifi(wifi_scan_item_t *items, int max_items) {
    if (!items || max_items <= 0) return -ESP_ERR_INVALID_ARG;
    if (!s_wifi_event_group) return -ESP_ERR_INVALID_STATE;

    /* A configured STA mode does not mean the radio has been started.
     * Initial boot no longer starts SoftAP, so prepare STA explicitly here.
     * Keep an already running STA/APSTA interface and connection intact.
     */
    wifi_mode_t mode;
    esp_err_t err=esp_wifi_get_mode(&mode);
    if(err!=ESP_OK) return -err;
    if(mode!=WIFI_MODE_STA && mode!=WIFI_MODE_APSTA) {
        err=esp_wifi_set_mode(mode==WIFI_MODE_AP?WIFI_MODE_APSTA:WIFI_MODE_STA);
        if(err!=ESP_OK) return -err;
    }
    if(!(xEventGroupGetBits(s_wifi_event_group)&WIFI_STA_STARTED_BIT)) {
        if(!s_sta_netif) {
            s_sta_netif=esp_netif_create_default_wifi_sta();
            if(!s_sta_netif) return -ESP_ERR_NO_MEM;
            err=esp_netif_set_hostname(s_sta_netif,s_product_hostname);
            if(err!=ESP_OK) return -err;
        }
        ESP_LOGI(TAG,"Starting STA interface for Wi-Fi scan");
        err=esp_wifi_start();
        if(err!=ESP_OK) {
            ESP_LOGE(TAG,"Scan startup failed: %s (0x%x)",esp_err_to_name(err),(unsigned)err);
            return -err;
        }
        EventBits_t ready=xEventGroupWaitBits(s_wifi_event_group,WIFI_STA_STARTED_BIT,
                                             pdFALSE,pdFALSE,pdMS_TO_TICKS(2000));
        if(!(ready&WIFI_STA_STARTED_BIT)) return -ESP_ERR_TIMEOUT;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE
    };

    ESP_LOGI(TAG, "Wi-Fi 스캔 시작...");
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 스캔 실패: %s (0x%x / %d)",esp_err_to_name(err),(unsigned)err,err);
        return -err;
    }

    uint16_t ap_count = 0;
    err=esp_wifi_scan_get_ap_num(&ap_count);
    if(err!=ESP_OK) {esp_wifi_clear_ap_list();return -err;}
    if (ap_count == 0) {
        ESP_LOGW(TAG, "AP 없음");
        return 0;
    }

    uint16_t fetch_count = (ap_count > (uint16_t)max_items) ? (uint16_t)max_items : ap_count;
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * fetch_count);
    if (!ap_records) {esp_wifi_clear_ap_list();return -ESP_ERR_NO_MEM;}

    esp_err_t rec_err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (rec_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records 실패: %d", rec_err);
        free(ap_records);
        esp_wifi_clear_ap_list();
        return -rec_err;
    }

    int valid_count = 0;
    for (int i = 0; i < fetch_count; i++) {
        if (strlen((char *)ap_records[i].ssid) == 0) continue;

        bool duplicate = false;
        for (int j = 0; j < valid_count; j++) {
            if (strcmp(items[j].ssid, (char *)ap_records[i].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        strncpy(items[valid_count].ssid, (char *)ap_records[i].ssid,
                sizeof(items[valid_count].ssid) - 1);
        items[valid_count].ssid[sizeof(items[valid_count].ssid) - 1] = '\0';
        items[valid_count].rssi     = ap_records[i].rssi;
        items[valid_count].authmode = ap_records[i].authmode;
        valid_count++;
        if (valid_count >= max_items) break;
    }

    free(ap_records);
    ESP_LOGI(TAG, "스캔 완료: %d개 AP 발견", valid_count);
    return valid_count;
}
