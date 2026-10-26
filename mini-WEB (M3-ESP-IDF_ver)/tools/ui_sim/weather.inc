/* Deterministic layout fixtures only; no HTTP, SD persistence, or live forecast. */
#include "../../main/m3_weather_presets.inc"
static m3_weather_state_t sim_weather;
unsigned m3_weather_preset_count(void) { return sizeof(weather_places) / sizeof(weather_places[0]); }
const m3_weather_place_t *m3_weather_preset(unsigned i) { return i < m3_weather_preset_count() ? &weather_places[i] : NULL; }
void m3_weather_init(void) { }
void m3_weather_state(m3_weather_state_t *out) {
    if (!sim_weather.place.name[0]) {
        sim_weather.place = weather_places[2]; sim_weather.valid = sim_weather.saved = true;
        sim_weather.status = M3_WEATHER_READY;
        sim_weather.data = (m3_weather_data_t){.temperature = 19.6, .feels_like = 18.2, .humidity = 61, .wind = 2.8,
            .code = 2, .time = "2026-10-26T10:15", .days = {
                {.date="2026-10-26", .low=13, .high=21, .rain=10, .code=2, .valid=true},
                {.date="2026-10-27", .low=12, .high=19, .rain=70, .code=61, .valid=true},
                {.date="2026-10-28", .low=11, .high=20, .rain=5, .code=0, .valid=true}}};
        sim_weather.revision = 1;
    }
    *out = sim_weather;
}
bool m3_weather_refresh(void) { sim_weather.status = M3_WEATHER_READY; sim_weather.stale = false; ++sim_weather.revision; return true; }
bool m3_weather_select(const m3_weather_place_t *p) {
    sim_weather.place = *p; sim_weather.count = 0; sim_weather.saved = true; ++sim_weather.revision; return true;
}
bool m3_weather_search(const char *query, const char *language) {
    (void)query; (void)language;
    sim_weather.results[0] = weather_places[2]; sim_weather.results[1] = weather_places[1];
    sim_weather.count = 2; sim_weather.status = M3_WEATHER_READY; ++sim_weather.revision; return true;
}
