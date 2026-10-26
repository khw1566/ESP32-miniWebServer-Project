#include "m4_cores.h"
#include "dns_server.h"
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNS_SVR";
static int s_dns_sock = -1;
static TaskHandle_t s_dns_task_h = NULL;

typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

typedef struct __attribute__((__packed__)) {
    uint16_t type;
    uint16_t class;
} dns_question_t;

typedef struct __attribute__((__packed__)) {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t data_len;
    uint32_t addr;
} dns_answer_t;

static void dns_task(void *pvParameters) {
    // DNS 이름과 질문, 응답 레코드를 모두 담을 수 있도록 여유를 둔다.
    uint8_t rx_buffer[512];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "Captive DNS task started on port 53");

    while (1) {
        socklen = sizeof(source_addr);
        int len = recvfrom(s_dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (len >= (int)(sizeof(dns_header_t) + sizeof(dns_question_t))) {
            dns_header_t *header = (dns_header_t *)rx_buffer;
            size_t offset = sizeof(dns_header_t);
            bool valid_name = false;

            // 이름 끝의 루트 레이블과 질문의 QTYPE/QCLASS까지 수신됐는지
            // 확인한 뒤에만 응답 레코드를 추가한다.
            while (offset < (size_t)len) {
                uint8_t label_len = rx_buffer[offset];
                if (label_len == 0) {
                    offset++;
                    valid_name = true;
                    break;
                }
                // 압축 포인터와 잘못된 레이블은 이 간단한 포털 DNS에서 거부한다.
                if ((label_len & 0xC0) != 0 || label_len > 63 ||
                    offset + 1U + label_len >= (size_t)len) {
                    break;
                }
                offset += 1U + label_len;
            }
            if (valid_name && offset + sizeof(dns_question_t) <= (size_t)len &&
                ntohs(header->qdcount) > 0 &&
                offset + sizeof(dns_question_t) + sizeof(dns_answer_t) <= sizeof(rx_buffer)) {
                offset += sizeof(dns_question_t);

                dns_answer_t answer = {
                    .name = htons(0xC00C),
                    .type = htons(1),
                    .class = htons(1),
                    .ttl = htonl(60),
                    .data_len = htons(4),
                    .addr = inet_addr("192.168.4.1")
                };

                memcpy(rx_buffer + offset, &answer, sizeof(answer));
                header->flags = htons(0x8180);
                header->ancount = htons(1);
                header->nscount = 0;
                header->arcount = 0;

                sendto(s_dns_sock, rx_buffer, offset + sizeof(answer), 0,
                       (struct sockaddr *)&source_addr, socklen);
            }
        }
    }
}

esp_err_t dns_server_start(void) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        return ESP_FAIL;
    }

    int err = bind(s_dns_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind DNS: errno %d", errno);
        close(s_dns_sock);
        s_dns_sock = -1;
        return ESP_FAIL;
    }

    xTaskCreatePinnedToCore(dns_task, "dns_task", 4096, NULL, 5, &s_dns_task_h, M4_CORE_NET);
    return ESP_OK;
}

void dns_server_stop(void) {
    if (s_dns_task_h) {
        vTaskDelete(s_dns_task_h);
        s_dns_task_h = NULL;
    }
    if (s_dns_sock != -1) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    ESP_LOGI(TAG, "DNS server stopped");
}
