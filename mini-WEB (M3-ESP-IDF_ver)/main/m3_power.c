#include "m3_power.h"
#include "m3_board.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "M3_POWER";

/* Manufacturer schematic 4_USB&IO: BAT+ -> R52 68k -> R55 0R -> GPIO53,
 * R57 100k + C61 10nF to GND. Vgpio = Vbat * 100 / 168.
 * IP5306 LED1/2/3 (I2C variant SCL/SDA/IRQ) and USB5V_IN have no MCU
 * connection, so external 5V is only inferred from the battery voltage. */
#define BATTERY_GPIO        53
#define DIVIDER_NUM         168
#define DIVIDER_DEN         100
#define SAMPLE_PERIOD_US    2000000LL
#define SAMPLES_PER_READ    16
#define RECENT_N            5    /* 10 s average used for display */
#define SPREAD_N            10   /* 20 s window for floating BAT pin */
#define SAMPLES_PER_BUCKET  30   /* 1 min trend buckets */
#define BUCKETS             6
#define ABSENT_LOW_MV       2500 /* below IP5306 cutoff: nothing on BAT */
#define ABSENT_HIGH_MV      4400 /* above Li-ion CV: charger output, no cell */
#define ABSENT_SPREAD_MV    300  /* charge/terminate cycling without a cell */
#define UNPLUG_STEP_MV      80
#define TREND_STEP_MV       2
#define FULL_MV             4150

typedef struct {
    unsigned recent[SPREAD_N], recent_count, recent_pos;
    unsigned bucket[BUCKETS], bucket_count, bucket_sum, bucket_samples;
    unsigned smooth;
    bool wired_known, wired, charging;
} m3_power_track_t;

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static adc_channel_t channel;
static bool adc_ready;
static m3_power_track_t track;
static m3_power_state_t last;

static bool calibration_init(adc_unit_t unit) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {.unit_id = unit, .chan = channel,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cfg, &cali) == ESP_OK) return true;
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line = {.unit_id = unit,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_line_fitting(&line, &cali) == ESP_OK) return true;
#endif
    (void)unit;
    return false;
}

static void battery_adc_init(void) {
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(BATTERY_GPIO, &unit, &channel);
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = unit};
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (err == ESP_OK) err = adc_oneshot_new_unit(&unit_cfg, &adc);
    if (err == ESP_OK) err = adc_oneshot_config_channel(adc, channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC GPIO%d unavailable: %s", BATTERY_GPIO, esp_err_to_name(err));
        return;
    }
    adc_ready = true;
    bool calibrated = calibration_init(unit);
    ESP_LOGI(TAG, "Battery ADC GPIO%d unit %d ch %d%s", BATTERY_GPIO, (int)unit + 1, (int)channel,
             calibrated ? "" : " (uncalibrated)");
}

/* Read-only diagnostics plus the BAT+ divider ADC. Never writes charger registers. */
void m3_power_init(void) {
    i2c_master_bus_handle_t bus = m3_board_i2c();
    esp_err_t err = bus ? i2c_master_probe(bus, 0x75, 25) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK)
        ESP_LOGW(TAG, "I2C 0x75 responds; confirm board revision/IP5306_I2C before enabling telemetry");
    else
        ESP_LOGI(TAG, "IP5306 has no I2C telemetry; I2C 0x75: %s", esp_err_to_name(err));
    battery_adc_init();
}

static bool read_battery_mv(unsigned *mv) {
    int sum = 0, count = 0;
    for (int i = 0; i < SAMPLES_PER_READ; ++i) {
        int raw, pin;
        if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) continue;
        if (!cali || adc_cali_raw_to_voltage(cali, raw, &pin) != ESP_OK) pin = raw * 3100 / 4095;
        sum += pin; ++count;
    }
    if (!count) return false;
    *mv = (unsigned)(sum / count) * DIVIDER_NUM / DIVIDER_DEN;
    return true;
}

/* Resting Li-ion curve. Under charge the terminal voltage reads high. */
static unsigned m3_power_percent(unsigned mv) {
    static const unsigned table[][2] = {{3300, 0}, {3400, 2}, {3500, 5}, {3600, 12}, {3650, 20},
        {3700, 30}, {3750, 40}, {3800, 50}, {3900, 65}, {4000, 78}, {4100, 90}, {4180, 100}};
    const unsigned n = sizeof(table) / sizeof(table[0]);
    if (mv <= table[0][0]) return 0;
    for (unsigned i = 1; i < n; ++i) {
        if (mv <= table[i][0]) {
            unsigned v0 = table[i - 1][0], p0 = table[i - 1][1], v1 = table[i][0], p1 = table[i][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 100;
}

/* Pure estimator: one 2 s battery sample in, display state out. */
static void m3_power_estimate(m3_power_track_t *t, unsigned mv, m3_power_state_t *out) {
    memset(out, 0, sizeof(*out));
    t->recent[t->recent_pos] = mv;
    t->recent_pos = (t->recent_pos + 1) % SPREAD_N;
    if (t->recent_count < SPREAD_N) t->recent_count++;
    if (t->recent_count < 3) return;

    unsigned now = 0, before = 0, low = 99999, high = 0;
    unsigned now_n = t->recent_count < RECENT_N ? t->recent_count : RECENT_N, before_n = 0;
    for (unsigned i = 0; i < t->recent_count; ++i) {
        unsigned v = t->recent[(t->recent_pos + SPREAD_N - 1 - i) % SPREAD_N];
        if (i < now_n) now += v; else if (i < 2 * RECENT_N) { before += v; before_n++; }
        if (v < low) low = v;
        if (v > high) high = v;
    }
    now /= now_n;
    if (before_n) before /= before_n;

    out->battery_known = true;
    if (now < ABSENT_LOW_MV || now > ABSENT_HIGH_MV || high - low > ABSENT_SPREAD_MV) {
        /* No cell: the board is running from external 5V. */
        t->smooth = 0; t->bucket_count = t->bucket_sum = t->bucket_samples = 0;
        t->wired_known = true; t->wired = true; t->charging = false;
        out->wired_known = true; out->wired = true;
        return;
    }
    out->battery_present = true;
    t->smooth = t->smooth ? (t->smooth * 3 + now) / 4 : now;
    out->millivolts = t->smooth;
    out->percent_known = true;
    out->percent = m3_power_percent(t->smooth);

    /* While wired the charger feeds the load, so a sharp drop means unplugged. */
    if (t->wired_known && t->wired && before_n && before > now + UNPLUG_STEP_MV) {
        t->wired = false; t->charging = false;
        t->bucket_count = t->bucket_sum = t->bucket_samples = 0;
    }
    t->bucket_sum += mv;
    if (++t->bucket_samples >= SAMPLES_PER_BUCKET) {
        if (t->bucket_count == BUCKETS) {
            for (unsigned i = 1; i < BUCKETS; ++i) t->bucket[i - 1] = t->bucket[i];
            t->bucket_count--;
        }
        t->bucket[t->bucket_count++] = t->bucket_sum / t->bucket_samples;
        t->bucket_sum = t->bucket_samples = 0;
        if (t->bucket_count == BUCKETS) {
            /* Five rising minutes: charging. Load relaxation settles sooner. */
            bool rising = true, falling = true, flat = true;
            for (unsigned i = 1; i < BUCKETS; ++i) {
                if (t->bucket[i] < t->bucket[i - 1] + TREND_STEP_MV) rising = false;
                if (t->bucket[i] + TREND_STEP_MV > t->bucket[i - 1]) falling = false;
            }
            unsigned first = t->bucket[0], final = t->bucket[BUCKETS - 1];
            if ((final > first ? final - first : first - final) > 5) flat = false;
            if (rising) { t->wired_known = true; t->wired = true; t->charging = true; }
            else if (falling) { t->wired_known = true; t->wired = false; t->charging = false; }
            else if (flat && t->wired_known && t->wired && final >= FULL_MV) t->charging = false;
        }
    }
    out->wired_known = t->wired_known;
    out->wired = t->wired;
    out->charge_known = t->wired_known;
    out->charging = t->charging;
}

void m3_power_normalize(m3_power_state_t *s) {
    if (!s->battery_known) s->battery_present = false;
    if (!s->battery_present) { s->percent_known = false; s->millivolts = 0; }
    if (!s->percent_known) s->percent = 0;
    if (s->percent > 100) s->percent = 100;
    if (!s->wired_known) s->wired = false;
    if (!s->charge_known || !s->battery_present || (s->wired_known && !s->wired)) s->charging = false;
}

/* Service-task only; sampling is rate limited and never runs on LVGL. */
void m3_power_poll(m3_power_state_t *out) {
    static int64_t next_sample;
    int64_t now = esp_timer_get_time();
    unsigned mv;
    if (adc_ready && now >= next_sample) {
        next_sample = now + SAMPLE_PERIOD_US;
        if (read_battery_mv(&mv)) {
            m3_power_state_t previous = last;
            m3_power_estimate(&track, mv, &last);
            m3_power_normalize(&last);
            if (last.battery_present != previous.battery_present || last.wired != previous.wired ||
                last.charging != previous.charging)
                ESP_LOGI(TAG, "battery %s %umV %u%% wired %s charging %s",
                         last.battery_known ? (last.battery_present ? "present" : "absent") : "?",
                         last.millivolts, last.percent,
                         last.wired_known ? (last.wired ? "yes" : "no") : "?",
                         last.charging ? "yes" : "no");
        }
    }
    *out = last;
}
