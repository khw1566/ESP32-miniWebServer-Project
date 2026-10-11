#include "https_memory.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
static bool relay_active, exclusive;

bool https_memory_relay_enter(void) {
    portENTER_CRITICAL(&guard);
    bool allowed = !exclusive && !relay_active;
    if (allowed) relay_active = true;
    portEXIT_CRITICAL(&guard);
    return allowed;
}
void https_memory_relay_leave(void) {
    portENTER_CRITICAL(&guard);
    relay_active = false;
    portEXIT_CRITICAL(&guard);
}
bool https_memory_exclusive_pending(void) {
    portENTER_CRITICAL(&guard);
    bool pending = exclusive;
    portEXIT_CRITICAL(&guard);
    return pending;
}
bool https_memory_exclusive_begin(uint32_t timeout_ms) {
    portENTER_CRITICAL(&guard);
    if (exclusive) { portEXIT_CRITICAL(&guard); return false; }
    exclusive = true;
    portEXIT_CRITICAL(&guard);
    TickType_t start = xTaskGetTickCount();
    while (true) {
        portENTER_CRITICAL(&guard);
        bool ready = !relay_active;
        portEXIT_CRITICAL(&guard);
        if (ready) return true;
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            https_memory_exclusive_end();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void https_memory_exclusive_end(void) {
    portENTER_CRITICAL(&guard);
    exclusive = false;
    portEXIT_CRITICAL(&guard);
}
