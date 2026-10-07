#include "memory_monitor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define TAG "MEM"
/* Application and SDK tasks whose stacks are allocated from internal RAM. */
static const char *const s_tasks[] = {
    "m1_ui", "m1_network", "httpd", "cloud_relay", "ai_https", "dns_task",
    "mdns", "tiT", "wifi", "sys_evt", "esp_timer",
};
static esp_timer_handle_t s_timer;

static void report(void *arg) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG, "heap free=%u min=%u largest=%u total=%u",
        (unsigned)heap_caps_get_free_size(caps), (unsigned)heap_caps_get_minimum_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps), (unsigned)heap_caps_get_total_size(caps));
    char line[192]; int used = 0;
    for (size_t i = 0; i < sizeof(s_tasks) / sizeof(s_tasks[0]); ++i) {
        TaskHandle_t task = xTaskGetHandle(s_tasks[i]);
        if (!task) continue;
        /* ESP-IDF reports the high-water mark in bytes. */
        int n = snprintf(line + used, sizeof(line) - used, " %s=%u", s_tasks[i],
            (unsigned)uxTaskGetStackHighWaterMark(task));
        if (n < 0 || n >= (int)(sizeof(line) - used)) break;
        used += n;
    }
    if (used) ESP_LOGI(TAG, "stack_free%s", line);
}

esp_err_t memory_monitor_start(unsigned period_seconds) {
    if (s_timer) return ESP_OK;
    if (!period_seconds) return ESP_ERR_INVALID_ARG;
    const esp_timer_create_args_t args = {.callback = report, .name = "mem_mon"};
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(s_timer, (uint64_t)period_seconds * 1000000ULL);
}
