#include "m4_hosted_boot.h"
#include "esp_hosted.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdatomic.h>

static atomic_bool app_ready;
int __real_esp_hosted_init(void);

/* Hosted 2.13 calls esp_hosted_init from a global constructor. On P4 v1.3,
 * the large startup-stack RAM region is not a usable heap until main_task
 * reclaims it. Its internal DMA mempool can fail before app_main is reached.
 * Keep managed sources intact and defer only this early constructor call. */
int __wrap_esp_hosted_init(void) {
    if (!atomic_load(&app_ready)) return ESP_OK;
    return __real_esp_hosted_init();
}

esp_err_t m4_hosted_start(void) {
    /* Once enabled, further SDK init calls retain Hosted's own idempotence. */
    atomic_store(&app_ready, true);
    ESP_LOGI("M4_HOSTED", "Starting after startup heap reclamation: internal=%u DMA=%u largest DMA=%u bytes",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    return __wrap_esp_hosted_init();
}
