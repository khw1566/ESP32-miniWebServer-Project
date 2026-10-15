#include "m3_sd_apps.h"
#include "m3_memory.h"
#include "m3_preferences.h"
#include "sd_media.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define SOURCE_LIMIT (64 * 1024)
#define HEAP_LIMIT (768 * 1024)
#define PIXELS (M3_SD_APP_WIDTH * M3_SD_APP_HEIGHT)
static _Atomic(SemaphoreHandle_t) gate;
static M3_PSRAM_BSS m3_sd_apps_state_t state;
static atomic_bool busy, cancel;
static atomic_uint input;
static uint16_t *front;
static bool fresh;
static char selected[M3_SD_APP_NAME];
typedef struct { size_t used; int64_t deadline; unsigned instructions; uint16_t *pixels; } vm_t;

static bool valid_name(const char *name) {
    size_t n = strlen(name);
    if (n < 5 || n >= M3_SD_APP_NAME || strcmp(name + n - 4, ".lua")) return false;
    for (size_t i = 0; i < n - 4; ++i)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-')) return false;
    return true;
}
static int compare_names(const void *a, const void *b) { return strcmp(a, b); }
static bool file_eligible(const char *name, const struct stat *info) {
    return valid_name(name) && S_ISREG(info->st_mode) && info->st_size > 0 && info->st_size <= SOURCE_LIMIT;
}
static bool source_valid(const char *source, size_t n, bool io_error) {
    return !io_error && n > 0 && n <= SOURCE_LIMIT && !memchr(source, 0, n);
}
void m3_sd_apps_poll(void) {
    if (!gate) { gate = xSemaphoreCreateMutex(); if (!gate) return; }
    static int64_t next_scan;
    int64_t now = esp_timer_get_time();
    if (now < next_scan) return;
    next_scan = now + 3000000;
    m3_sd_apps_state_t catalog = { .mounted = sd_media_ready() };
    if (catalog.mounted) {
        if (!sd_media_admin_lock()) { next_scan = now + 500000; return; }
        DIR *dir = opendir(SD_MEDIA_ROOT "/apps");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL && catalog.count < M3_SD_APP_COUNT) {
                if (!valid_name(entry->d_name)) continue;
                char path[96]; struct stat info;
                snprintf(path, sizeof(path), SD_MEDIA_ROOT "/apps/%.47s", entry->d_name);
                if (stat(path, &info) || !file_eligible(entry->d_name, &info)) continue;
                memcpy(catalog.names[catalog.count++], entry->d_name, strlen(entry->d_name) + 1);
            }
            closedir(dir);
        }
        sd_media_admin_unlock();
    }
    qsort(catalog.names, catalog.count, M3_SD_APP_NAME, compare_names);
    xSemaphoreTake(gate, portMAX_DELAY);
    state.mounted = catalog.mounted; state.count = catalog.count;
    memcpy(state.names, catalog.names, sizeof(state.names));
    xSemaphoreGive(gate);
    if (!catalog.mounted) m3_sd_apps_stop();
}
bool m3_sd_apps_state(m3_sd_apps_state_t *out) {
    if (!out || !gate || xSemaphoreTake(gate, 0) != pdTRUE) return false;
    *out = state; out->running = atomic_load(&busy);
    xSemaphoreGive(gate); return true;
}
void m3_sd_apps_stop(void) { atomic_store(&cancel, true); }
void m3_sd_apps_input(unsigned buttons) { atomic_fetch_or(&input, buttons & 31); }
bool m3_sd_apps_frame(uint16_t *destination) {
    if (!destination || !gate || xSemaphoreTake(gate, 0) != pdTRUE) return false;
    bool copied = fresh && front;
    if (copied) { memcpy(destination, front, PIXELS * sizeof(uint16_t)); fresh = false; }
    xSemaphoreGive(gate); return copied;
}
static void *allocate(void *ud, void *ptr, size_t old, size_t size) {
    vm_t *vm = ud;
    if (!ptr) old = 0; /* Lua passes a type tag for new allocations. */
    if (!size) { free(ptr); vm->used -= old; return NULL; }
    if (size > HEAP_LIMIT || vm->used - old > HEAP_LIMIT - size) return NULL;
    void *next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (next) vm->used = vm->used - old + size;
    return next;
}
static vm_t *context(lua_State *L) { return *(vm_t **)lua_getextraspace(L); }
static void budget(lua_State *L, lua_Debug *debug) {
    (void)debug; vm_t *vm = context(L);
    vm->instructions += 1000;
    if (atomic_load(&cancel) || vm->instructions > 600000 || esp_timer_get_time() > vm->deadline)
        luaL_error(L, "Execution limit / stopped");
}
static void reset_budget(lua_State *L) {
    context(L)->instructions = 0;
    context(L)->deadline = esp_timer_get_time() + 40000;
    lua_sethook(L, budget, LUA_MASKCOUNT, 1000);
}
static int rectangle(lua_State *L) {
    lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
    lua_Integer w = luaL_checkinteger(L, 3), h = luaL_checkinteger(L, 4);
    uint16_t color = (uint16_t)luaL_checkinteger(L, 5);
    /* Validate before adding, so hostile integers cannot overflow. */
    if (x < -160 || x > 160 || y < -192 || y > 192 || w < 0 || w > 160 || h < 0 || h > 192) return 0;
    int endx = (int)(x + w), endy = (int)(y + h);
    if (endx > 160) endx = 160;
    if (endy > 192) endy = 192;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int row = (int)y; row < endy; ++row)
        for (int col = (int)x; col < endx; ++col) context(L)->pixels[row * 160 + col] = color;
    return 0;
}
static int score(lua_State *L) {
    lua_Integer value = luaL_checkinteger(L, 1);
    if (value < 0) value = 0;
    if (value > 999999999) value = 999999999;
    xSemaphoreTake(gate, portMAX_DELAY); state.score = (int)value; xSemaphoreGive(gate); return 0;
}
static int configure(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    /* No filesystem, network, binary loaders, protected calls or finalizers.
       Protected calls/coroutines would let scripts catch a budget exception. */
    const char *remove[] = {"dofile", "loadfile", "load", "pcall", "xpcall", "collectgarbage", "setmetatable", "getmetatable", "print", "warn"};
    for (unsigned i = 0; i < sizeof(remove) / sizeof(remove[0]); ++i) { lua_pushnil(L); lua_setglobal(L, remove[i]); }
    lua_pushcfunction(L, rectangle); lua_setglobal(L, "rect");
    lua_pushcfunction(L, score); lua_setglobal(L, "score");
    lua_pushinteger(L, esp_random()); lua_setglobal(L, "SEED");
    return 0;
}
static char *read_source(void) {
    if (!sd_media_ready() || !sd_media_admin_lock()) return NULL;
    char path[96]; snprintf(path, sizeof(path), SD_MEDIA_ROOT "/apps/%s", selected);
    char *source = heap_caps_malloc(SOURCE_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    FILE *file = source ? fopen(path, "rb") : NULL;
    if (file) {
        size_t n = fread(source, 1, SOURCE_LIMIT + 1, file);
        bool ok = source_valid(source, n, ferror(file) != 0);
        fclose(file);
        if (ok) source[n] = 0;
        else { free(source); source = NULL; }
    } else { free(source); source = NULL; }
    sd_media_admin_unlock(); return source;
}
static void worker(void *unused) {
    (void)unused;
    vm_t vm = {0}; lua_State *L = NULL;
    char error[128] = "Cannot load SD app";
    char *source = read_source();
    vm.pixels = heap_caps_calloc(PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t *frame = heap_caps_calloc(PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!source || !vm.pixels || !frame) goto done;
    L = lua_newstate(allocate, &vm);
    if (!L) { snprintf(error, sizeof(error), "Lua memory unavailable"); goto done; }
    *(vm_t **)lua_getextraspace(L) = &vm;
    reset_budget(L);
    lua_pushcfunction(L, configure);
    int result = lua_pcall(L, 0, 0, 0);
    if (result == LUA_OK) result = luaL_loadbufferx(L, source, strlen(source), selected, "t");
    free(source); source = NULL;
    if (result == LUA_OK) result = lua_pcall(L, 0, 0, 0);
    if (result != LUA_OK) goto lua_error;
    xSemaphoreTake(gate, portMAX_DELAY); front = frame; xSemaphoreGive(gate);
    while (!atomic_load(&cancel) && !m3_preferences_restarting()) {
        reset_budget(L);
        lua_getglobal(L, "update");
        lua_pushinteger(L, atomic_exchange(&input, 0));
        result = lua_pcall(L, 1, 0, 0);
        if (result != LUA_OK) goto lua_error;
        xSemaphoreTake(gate, portMAX_DELAY);
        memcpy(frame, vm.pixels, PIXELS * sizeof(uint16_t)); fresh = true;
        xSemaphoreGive(gate);
        vTaskDelay(pdMS_TO_TICKS(50)); /* Fixed 20 Hz app simulation. */
    }
    error[0] = 0; goto done;
lua_error:
    /* Error objects need not be strings; avoid executing tostring metamethods. */
    snprintf(error, sizeof(error), "%s", lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "Lua execution failed");
done:
    if (L) lua_close(L);
    free(source); free(vm.pixels);
    xSemaphoreTake(gate, portMAX_DELAY);
    front = NULL; fresh = false; free(frame);
    snprintf(state.error, sizeof(state.error), "%s", atomic_load(&cancel) ? "" : error);
    xSemaphoreGive(gate);
    atomic_store(&busy, false);
    vTaskDelete(NULL);
}
bool m3_sd_apps_start(const char *name) {
    if (!name || !valid_name(name) || !gate || m3_preferences_web_active() || m3_preferences_restarting()) return false;
    xSemaphoreTake(gate, portMAX_DELAY);
    unsigned index = 0;
    while (index < state.count && strcmp(name, state.names[index])) ++index;
    if (atomic_load(&busy) || !state.mounted || index >= state.count) { xSemaphoreGive(gate); return false; }
    snprintf(selected, sizeof(selected), "%s", state.names[index]);
    state.error[0] = 0; state.score = 0;
    atomic_store(&cancel, false); atomic_store(&input, 0); atomic_store(&busy, true);
    xSemaphoreGive(gate);
    if (xTaskCreate(worker, "sd_lua", 24576, NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(gate, portMAX_DELAY);
        snprintf(state.error, sizeof(state.error), "Worker memory unavailable");
        xSemaphoreGive(gate); atomic_store(&busy, false); return false;
    }
    return true;
}
