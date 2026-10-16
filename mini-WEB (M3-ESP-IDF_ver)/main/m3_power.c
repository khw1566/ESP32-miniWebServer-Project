#include "m3_power.h"
#include "m3_board.h"
#include "esp_log.h"
#include <string.h>

/* Manufacturer schematic: IP5306 LED1/2/3 have no MCU connection.
 * A revision using IP5306_I2C must be verified on hardware before its
 * status bits are interpreted as battery/charger readings. */
void m3_power_init(void) {
    i2c_master_bus_handle_t bus = m3_board_i2c();
    esp_err_t err = bus ? i2c_master_probe(bus, 0x75, 25) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK)
        ESP_LOGW("M3_POWER", "I2C 0x75 responds; confirm board revision/IP5306_I2C before enabling telemetry");
    else
        ESP_LOGI("M3_POWER", "No verified battery telemetry; I2C 0x75: %s", esp_err_to_name(err));
    /* Read-only address probe. Never change charger, voltage or boost registers. */
}
void m3_power_normalize(m3_power_state_t *s) {
    if (!s->battery_known) s->battery_present = false;
    if (!s->battery_present) { s->percent_known = false; s->millivolts = 0; }
    if (!s->percent_known) s->percent = 0;
    if (s->percent > 100) s->percent = 100;
    if (!s->wired_known) s->wired = false;
    if (!s->charge_known || !s->battery_present || (s->wired_known && !s->wired)) s->charging = false;
}
void m3_power_poll(m3_power_state_t *out) {
    /* Unavailable is distinct from absent, zero percent and wired power.
     * Replace with a verified board backend once MCU telemetry is confirmed.
     * UI consumes only a runtime snapshot; no sensor I/O on the LVGL task. */
    memset(out, 0, sizeof(*out));
}
