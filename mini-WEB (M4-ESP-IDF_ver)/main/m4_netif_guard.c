#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_wifi.h"
#include "lwip/netif.h"

/* Linker wrapping leaves SDK/managed sources untouched. The Hosted event
 * stream can deliver a repeated START; IDF 5.5 unconditionally adds the netif.
 * Check membership, not netif_is_up: a started but disconnected STA is down.
 * Both the check and the real start execute in the same TCP/IP callback, so
 * a queued STOP cannot remove the interface between those operations. */
void __real_esp_netif_action_start(void *netif, esp_event_base_t base, int32_t id, void *data);

typedef struct {
    esp_netif_t *netif;
    esp_event_base_t base;
    int32_t id;
    void *data;
} start_event_t;

static esp_err_t start_once_tcpip(void *ctx) {
    start_event_t *event = ctx;
    struct netif *target = esp_netif_get_netif_impl(event->netif);
    if (!target) return ESP_ERR_INVALID_STATE;
    for (struct netif *item = netif_list; item; item = item->next) {
        if (item == target) {
            ESP_LOGW("M4_NETIF", "Duplicate Wi-Fi START ignored: %s (event %ld)",
                     esp_netif_get_ifkey(event->netif), (long)event->id);
            return ESP_OK;
        }
    }
    __real_esp_netif_action_start(event->netif, event->base, event->id, event->data);
    return ESP_OK;
}

void __wrap_esp_netif_action_start(void *netif, esp_event_base_t base, int32_t id, void *data) {
    if (base != WIFI_EVENT || (id != WIFI_EVENT_STA_START && id != WIFI_EVENT_AP_START)) {
        __real_esp_netif_action_start(netif, base, id, data);
        return;
    }
    start_event_t event = {.netif = netif, .base = base, .id = id, .data = data};
    esp_err_t err = esp_netif_tcpip_exec(start_once_tcpip, &event);
    if (err != ESP_OK) ESP_LOGE("M4_NETIF", "Wi-Fi START guard failed: %s", esp_err_to_name(err));
}
