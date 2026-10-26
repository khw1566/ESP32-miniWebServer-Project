#include "m4_power.h"
#include "esp_log.h"
#include <string.h>

/* The JC1060 reference does not establish a battery divider or charger
 * telemetry connection. Do not read or drive M3's GPIO53 on this board. */
void m4_power_init(void) {
    ESP_LOGI("M4_POWER", "JC1060 battery/charging telemetry unavailable; circuit unconfirmed");
}
void m4_power_normalize(m4_power_state_t *s) {
    if (!s->battery_known) s->battery_present = false;
    if (!s->battery_present) { s->percent_known = false; s->millivolts = 0; }
    if (!s->percent_known) s->percent = 0;
    if (s->percent > 100) s->percent = 100;
    if (!s->wired_known) s->wired = false;
    if (!s->charge_known || !s->battery_present || (s->wired_known && !s->wired)) s->charging = false;
}
void m4_power_poll(m4_power_state_t *out) {
    if (out) memset(out, 0, sizeof(*out));
}
