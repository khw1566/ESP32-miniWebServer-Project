#include "screen_saver.h"
#include "board_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "SCREEN_SAVER"
#define WAKE_RETRY_US 1000000
#define WAKE_ARM_US 800000
#define WAKE_PRESS_POLLS 3
static bool requested, active, restored;
static int64_t retry_at;
static unsigned press_polls;

static unsigned internal_free(void) {
    return (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void screen_saver_request(void) {requested = true;}
bool screen_saver_active(void) {return active;}

bool screen_saver_service(bool can_enter, screen_saver_teardown_t teardown) {
    if (!active) {
        if (!requested || !can_enter || !teardown) return false;
        requested = false;
        unsigned before = internal_free();
        /* Widgets go first; the draw buffer must not be freed while LVGL can render. */
        teardown();
        board_display_sleep();
        active = true; restored = false; press_polls = 0;
        /* Ignore the tail of the tap that pressed the button. */
        retry_at = esp_timer_get_time() + WAKE_ARM_US;
        ESP_LOGI(TAG, "Enter: free %u -> %u", before, internal_free());
        return false;
    }
    if (!restored) {
        if (esp_timer_get_time() < retry_at) return false;
        if (!board_display_touch_down()) {press_polls = 0; return false;}
        if (++press_polls < WAKE_PRESS_POLLS) return false;
        if (board_display_wake() != ESP_OK) {
            retry_at = esp_timer_get_time() + WAKE_RETRY_US;
            ESP_LOGW(TAG, "Wake deferred: free=%u", internal_free());
            return false;
        }
        restored = true; press_polls = 0;
        ESP_LOGI(TAG, "Wake: free %u", internal_free());
        return false;
    }
    /* The waking touch must not reach the rebuilt page as a click. */
    if (board_display_touch_down()) {press_polls = 0; return false;}
    if (++press_polls < WAKE_PRESS_POLLS) return false;
    active = false; restored = false;
    return true;
}
void screen_saver_reveal(void) {
    lv_refr_now(NULL);
    board_display_backlight(true);
}
