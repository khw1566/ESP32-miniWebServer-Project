#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Each known flag is independent: an I2C failure does not mean no battery. */
typedef struct {
    bool battery_known, battery_present, percent_known;
    bool wired_known, wired, charge_known, charging;
    unsigned percent, millivolts;
} m3_power_state_t;
/* Battery comes from the BAT+ divider on GPIO53; external 5V is inferred.
 * Read-only diagnostic and ADC setup at board startup, before workers. */
void m3_power_init(void);
/* Service-task owner only. Never perform sensor I/O on the LVGL task. */
void m3_power_poll(m3_power_state_t *out);
void m3_power_normalize(m3_power_state_t *state);
