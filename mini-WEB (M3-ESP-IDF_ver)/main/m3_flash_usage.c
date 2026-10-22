/* Flash usage table patched into the linked ELF by tools/flash_usage.py. */
#include "m3_flash_usage.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_ota_ops.h"
#endif

#define BLOB_MAGIC "M3-FLASH-USAGE1"
#define BLOB_PARTS 32
_Static_assert(M3_FLASH_PART_COUNT <= BLOB_PARTS, "grow BLOB_PARTS and tools/flash_usage.py together");

/* Layout is fixed: magic[16], state, image_bytes, count, bytes[32], all little-endian u32. */
typedef struct {
    char magic[16];
    uint32_t state;
    uint32_t image_bytes;
    uint32_t count;
    uint32_t bytes[BLOB_PARTS];
} flash_usage_blob_t;

#ifdef __GNUC__
__attribute__((used, aligned(4)))
#endif
static const flash_usage_blob_t blob = {.magic = BLOB_MAGIC};

#define PART_GROUP(id, group, en, ko, ja) M3_FLASH_GROUP_##group,
static const uint8_t groups[M3_FLASH_PART_COUNT] = {M3_FLASH_PARTS(PART_GROUP)};
#undef PART_GROUP
#define PART_NAME(id, group, en, ko, ja) {en, ko, ja},
static const char *const names[M3_FLASH_PART_COUNT][3] = {M3_FLASH_PARTS(PART_NAME)};
#undef PART_NAME
static const char *const group_names[M3_FLASH_GROUP_COUNT][3] = {
    {"Apps", "앱", "アプリ"}, {"Media and assets", "미디어·에셋", "メディア・アセット"},
    {"Core", "코어", "コア"}, {"Libraries", "라이브러리", "ライブラリ"}, {"Other", "기타", "その他"},
};

static unsigned lang_index(m3_lang_t language) {
    return language == M3_LANG_KO ? 1 : language == M3_LANG_JA ? 2 : 0;
}

#ifndef ESP_PLATFORM
/* Host UI preview: representative values from a measured build. */
static const uint32_t preview_bytes[M3_FLASH_PART_COUNT] = {
    77831, 150000, 60000, 240000, 95000, 52000, 45000, 20000, 30000, 90000,
    875292, 405700, 310892, 160000, 50000, 18000, 85133, 323825, 68947,
    180000, 230000, 330000, 190000, 130000, 420000, 3000,
};
#endif

void m3_flash_usage_get(m3_flash_usage_t *out) {
    memset(out, 0, sizeof(*out));
    const flash_usage_blob_t *source = &blob;
#ifdef __GNUC__
    /* The initializer is all zero; hide it from constant folding so the patched bytes are read. */
    __asm__ volatile("" : "+r"(source));
#endif
#ifdef ESP_PLATFORM
    const esp_partition_t *running = esp_ota_get_running_partition();
    out->slot_bytes = running ? running->size : 0;
    if (memcmp(source->magic, BLOB_MAGIC, sizeof(BLOB_MAGIC)) || source->state != 1 ||
        source->count != M3_FLASH_PART_COUNT) return;
    out->measured = true;
    out->image_bytes = source->image_bytes;
    memcpy(out->part_bytes, source->bytes, sizeof(out->part_bytes));
#else
    (void)source;
    out->slot_bytes = 0x760000;
    out->measured = true;
    memcpy(out->part_bytes, preview_bytes, sizeof(out->part_bytes));
    for (unsigned i = 0; i < M3_FLASH_PART_COUNT; ++i) out->image_bytes += preview_bytes[i];
#endif
}

m3_flash_group_t m3_flash_usage_group(m3_flash_part_t part) {
    return part < M3_FLASH_PART_COUNT ? (m3_flash_group_t)groups[part] : M3_FLASH_GROUP_OTHER;
}

const char *m3_flash_usage_part_name(m3_flash_part_t part, m3_lang_t language) {
    return part < M3_FLASH_PART_COUNT ? names[part][lang_index(language)] : "";
}

const char *m3_flash_usage_group_name(m3_flash_group_t group, m3_lang_t language) {
    return group < M3_FLASH_GROUP_COUNT ? group_names[group][lang_index(language)] : "";
}
