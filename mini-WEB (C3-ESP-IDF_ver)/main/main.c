#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include "network_manager.h"
#include "dns_server.h"
#include "web_server.h"
#include "cloud_relay.h"
#include "qr_printer.h"

static const char *TAG = "MAIN_APP";

static const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        default: return "OTHER";
    }
}

// -------------------------------------------------------------
// 내부: Wi-Fi 연결 성공 후 후처리 (웹서버 기동, QR 출력)
// -------------------------------------------------------------
static void start_webserver_and_qr(void) {
    char ip_str[32] = {0};
    network_get_ip_str(ip_str, sizeof(ip_str));

    char full_url[64] = {0};
    snprintf(full_url, sizeof(full_url), "http://%s/", ip_str);

    ESP_ERROR_CHECK(web_server_start());

    // IPv6 협상 대기 (최대 2초)
    char ip6_str[64] = "미발급";
    for (int wait = 0; wait < 20; wait++) {
        network_get_ip6_str(ip6_str, sizeof(ip6_str));
        if (strcmp(ip6_str, "미발급") != 0 && strlen(ip6_str) > 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    char ipv6_url[128] = {0};
    if (strcmp(ip6_str, "미발급") != 0) {
        snprintf(ipv6_url, sizeof(ipv6_url), "http://[%s]/", ip6_str);
    }

    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, " IPv4 LAN : %s", full_url);
    if (strlen(ipv6_url) > 0) {
        ESP_LOGI(TAG, " IPv6     : %s", ipv6_url);
    } else {
        ESP_LOGW(TAG, " IPv6     : 공유기 IPv6 대기 중 (백그라운드 협상)");
    }
    ESP_LOGI(TAG, "===========================================");

    char product_url[96] = {0};
    if (network_mdns_is_ready() && network_get_mdns_url(product_url, sizeof(product_url)) == ESP_OK) {
        ESP_LOGI(TAG, " Product  : %s", product_url);
        qr_printer_console(product_url);
    } else {
        qr_printer_console(full_url);
    }

}

// -------------------------------------------------------------
// 시리얼 Wi-Fi 설정 태스크
// 시리얼 모니터(UART)를 통해 SSID와 비밀번호를 입력받아 Wi-Fi 연결 시도
// 형식: "SSID:비밀번호" 또는 "SSID" (개방형)
// 예시: "MyRouter:mypassword123"
// -------------------------------------------------------------
static void serial_wifi_task(void *arg) {
    char line_buf[128] = {0};
    int  pos = 0;

    printf("\n");
    printf("=============================================\n");
    printf("  시리얼 Wi-Fi 설정 모드\n");
    printf("  SSID와 비밀번호를 아래 형식으로 입력하세요:\n");
    printf("  [SSID:비밀번호]  또는  [SSID] (개방형)\n");
    printf("  예: MyRouter:password123\n");
    printf("=============================================\n");
    printf("> ");
    fflush(stdout);

    while (1) {
        int ch = fgetc(stdin);
        if (ch == EOF || ch < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 에코 출력
        putchar(ch);
        fflush(stdout);

        if (ch == '\n' || ch == '\r') {
            if (pos == 0) {
                printf("> ");
                fflush(stdout);
                continue;
            }

            line_buf[pos] = '\0';
            pos = 0;

            // 앞뒤 공백 제거
            char *trimmed = line_buf;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            int tlen = strlen(trimmed);
            while (tlen > 0 && (trimmed[tlen-1] == ' ' || trimmed[tlen-1] == '\r')) {
                trimmed[--tlen] = '\0';
            }
            if (tlen == 0) {
                printf("> ");
                fflush(stdout);
                continue;
            }

            // SSID:PASSWORD 파싱
            char ssid[64] = {0};
            char pass[64] = {0};
            char *colon = strchr(trimmed, ':');
            if (colon) {
                int ssid_len = colon - trimmed;
                if (ssid_len <= 0 || ssid_len >= (int)sizeof(ssid)) {
                    printf("[오류] SSID 길이 오류. 다시 입력하세요.\n> ");
                    fflush(stdout);
                    continue;
                }
                strncpy(ssid, trimmed, ssid_len);
                ssid[ssid_len] = '\0';
                strncpy(pass, colon + 1, sizeof(pass) - 1);
            } else {
                strncpy(ssid, trimmed, sizeof(ssid) - 1);
            }

            printf("\n[시리얼] SSID: [%s], PW: [%s]\n", ssid, strlen(pass) > 0 ? "****" : "(없음)");
            printf("[시리얼] 연결 시도 중...\n");
            fflush(stdout);

            esp_err_t res = network_connect_sta(ssid, pass);
            if (res == ESP_OK) {
                printf("[시리얼] 연결 성공! 자격증명을 NVS에 저장합니다.\n");
                fflush(stdout);
                network_save_credentials(ssid, pass);

                // 웹서버가 아직 미실행 상태이면 기동
                if (network_get_mode() == NET_MODE_STA_CONNECTED) {
                    start_webserver_and_qr();
                }
                break; // 연결 성공 시 태스크 종료
            } else {
                printf("[시리얼] 연결 실패. SSID/비밀번호를 확인하고 다시 입력하세요.\n> ");
                fflush(stdout);
            }
        } else if (ch == 127 || ch == '\b') {
            // 백스페이스
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else {
            if (pos < (int)sizeof(line_buf) - 1) {
                line_buf[pos++] = (char)ch;
            }
        }
    }

    vTaskDelete(NULL);
}

// -------------------------------------------------------------
// 메인 진입점
// -------------------------------------------------------------
void app_main(void) {
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  Mini WebServer C3 (ESP-IDF v5.5) v1.2  ");
    ESP_LOGI(TAG, "===========================================");

    // NVS 초기화
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 로그 링버퍼 및 파일시스템 초기화
    web_server_log_init();
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGW(TAG, "[BOOT] wifi-reset-diag-2, reset=%s (%d)",
             reset_reason_name(reset_reason), (int)reset_reason);
    web_server_storage_init();

    // 네트워크 드라이버 초기화
    ESP_ERROR_CHECK(network_init());
    if (cloud_relay_start() != ESP_OK) ESP_LOGW(TAG, "Cloud relay task unavailable");

    char saved_ssid[64] = {0};
    char saved_pass[64] = {0};

    if (network_has_saved_credentials(saved_ssid, sizeof(saved_ssid),
                                      saved_pass, sizeof(saved_pass))) {
        ESP_LOGI(TAG, "저장된 Wi-Fi 발견: [%s]. STA 연결 시도...", saved_ssid);

        esp_err_t sta_res = network_connect_sta(saved_ssid, saved_pass);
        if (sta_res == ESP_OK) {
            start_webserver_and_qr();
            // 시리얼 모니터로 재설정 가능하도록 시리얼 태스크 유지 (백그라운드)
            xTaskCreate(serial_wifi_task, "serial_wifi", 4096, NULL, 3, NULL);
            return;
        }

        ESP_LOGW(TAG, "저장된 자격증명으로 연결 실패. SoftAP 진입.");
    }

    // 저장된 자격증명 없거나 연결 실패 → SoftAP 캡티브 포털
    ESP_LOGI(TAG, "SoftAP 캡티브 포털 시작...");
    ESP_ERROR_CHECK(network_start_softap());
    ESP_ERROR_CHECK(dns_server_start());
    ESP_ERROR_CHECK(web_server_start());

    qr_printer_console("http://192.168.4.1/setup");
    ESP_LOGI(TAG, "SSID '%s' 에 접속 후 http://192.168.4.1/setup 에서 Wi-Fi 설정",
             SOFTAP_SSID_DEFAULT);

    // 시리얼 모니터를 통한 Wi-Fi 연결 보조 수단 활성화
    xTaskCreate(serial_wifi_task, "serial_wifi", 4096, NULL, 3, NULL);
}
