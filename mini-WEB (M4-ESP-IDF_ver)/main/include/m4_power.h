#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Each known flag is independent: an I2C failure does not mean no battery. */
typedef struct {
    bool battery_known, battery_present, percent_known;
    bool wired_known, wired, charge_known, charging;
    unsigned percent, millivolts;
} m4_power_state_t;
/* JC1060 battery/charger sensing is unconfirmed. All known flags remain false;
 * no inherited M3 battery ADC GPIO is initialized. */
void m4_power_init(void);
/* Service-task owner only. Never perform sensor I/O on the LVGL task. */
void m4_power_poll(m4_power_state_t *out);
void m4_power_normalize(m4_power_state_t *state);
