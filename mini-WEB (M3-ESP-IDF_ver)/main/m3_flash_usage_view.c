/* Settings view of the app image Flash usage measured at build time. */
#include "m3_flash_usage_view.h"
#include "m3_flash_usage.h"
#include "m3_i18n.h"
#include <stdio.h>

static const uint32_t group_colors[M3_FLASH_GROUP_COUNT] = {0x00e5ef, 0xb7c2d0, 0x6f7d90, 0x3c4757, 0x262e3a};
typedef enum { S_CAPTION, S_MISSING, S_SHOW, S_HIDE, S_USED, S_FREE, S_COUNT } view_text_t;
static const char *const texts[S_COUNT][3] = {
    {"Measured from the link map when this firmware was built. Percentages are of the app image.",
     "이 펌웨어를 빌드할 때 링크 맵에서 측정한 값입니다. 비율은 앱 이미지 기준입니다.",
     "このファームウェアのビルド時にリンクマップから測定した値です。割合はアプリイメージ基準です。"},
    {"This firmware has no usage table. It appears after the next build.",
     "이 펌웨어에는 용량 표가 없습니다. 다음 빌드부터 표시됩니다.",
     "このファームウェアには容量表がありません。次のビルドから表示されます。"},
    {"Show by function", "기능별 보기", "機能別に表示"},
    {"Hide details", "접기", "閉じる"},
    {"Used", "사용", "使用"},
    {"free", "여유", "空き"},
};

static m3_flash_usage_t usage;
static m3_flash_view_style_t view_style;
static lv_obj_t *summary, *note, *bar, *legend_names[M3_FLASH_GROUP_COUNT], *legend_values[M3_FLASH_GROUP_COUNT];
static lv_obj_t *toggle, *toggle_text, *details, *part_names[M3_FLASH_PART_COUNT];
static uint8_t order[M3_FLASH_PART_COUNT];

static unsigned lang(void) {
    m3_lang_t language = m3_i18n_language();
    return language == M3_LANG_KO ? 1 : language == M3_LANG_JA ? 2 : 0;
}
static const char *text_of(view_text_t id) { return texts[id][lang()]; }

static void format_size(char *out, size_t size, uint32_t bytes) {
    if (bytes >= 1024u * 1024u) snprintf(out, size, "%u.%02u MB", (unsigned)(bytes >> 20),
        (unsigned)(((bytes & 0xfffffu) * 100u) >> 20));
    else snprintf(out, size, "%u KB", (unsigned)((bytes + 512u) / 1024u));
}
static unsigned permille(uint32_t part, uint32_t whole) {
    return whole ? (unsigned)(((uint64_t)part * 1000u + whole / 2) / whole) : 0;
}
static void format_share(char *out, size_t size, uint32_t bytes, uint32_t whole) {
    char amount[24]; format_size(amount, sizeof(amount), bytes);
    unsigned share = permille(bytes, whole);
    snprintf(out, size, "%s  %u.%u%%", amount, share / 10, share % 10);
}
static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
static lv_obj_t *line_row(lv_obj_t *parent) {
    lv_obj_t *obj = plain(parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 10, 0);
    return obj;
}
static lv_obj_t *add_label(lv_obj_t *parent, uint32_t color, bool grow) {
    lv_obj_t *obj = lv_label_create(parent); lv_label_set_text(obj, "");
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (grow) { lv_obj_set_flex_grow(obj, 1); lv_obj_set_width(obj, 0); }
    return obj;
}
static uint32_t group_bytes(m3_flash_group_t group) {
    uint32_t total = 0;
    for (unsigned i = 0; i < M3_FLASH_PART_COUNT; ++i)
        if (m3_flash_usage_group((m3_flash_part_t)i) == group) total += usage.part_bytes[i];
    return total;
}

static void build_details(void) {
    for (unsigned i = 0; i < M3_FLASH_PART_COUNT; ++i) order[i] = (uint8_t)i;
    for (unsigned i = 1; i < M3_FLASH_PART_COUNT; ++i)   /* insertion sort, largest first */
        for (unsigned j = i; j && usage.part_bytes[order[j]] > usage.part_bytes[order[j - 1]]; --j) {
            uint8_t swap = order[j]; order[j] = order[j - 1]; order[j - 1] = swap;
        }
    for (unsigned i = 0; i < M3_FLASH_PART_COUNT; ++i) {
        unsigned part = order[i];
        if (!usage.part_bytes[part]) continue;
        lv_obj_t *item = plain(details);
        lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(item, 4, 0);
        lv_obj_t *head = line_row(item);
        part_names[part] = add_label(head, view_style.text, true);
        lv_label_set_long_mode(part_names[part], LV_LABEL_LONG_DOT);
        lv_obj_set_height(part_names[part], lv_obj_get_style_text_font(part_names[part], LV_PART_MAIN)->line_height);
        char value[40]; format_share(value, sizeof(value), usage.part_bytes[part], usage.image_bytes);
        lv_label_set_text(add_label(head, view_style.muted, false), value);
        lv_obj_t *meter = lv_bar_create(item);
        lv_obj_set_size(meter, LV_PCT(100), 6);
        lv_bar_set_range(meter, 0, 1000);
        lv_bar_set_value(meter, (int32_t)permille(usage.part_bytes[part], usage.image_bytes), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(meter, lv_color_hex(view_style.track), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(meter, lv_color_hex(group_colors[m3_flash_usage_group((m3_flash_part_t)part)]),
            LV_PART_INDICATOR);
        lv_obj_set_style_radius(meter, 3, LV_PART_MAIN); lv_obj_set_style_radius(meter, 3, LV_PART_INDICATOR);
    }
}

static void toggle_details(lv_event_t *event) {
    (void)event;
    if (!details) {
        details = plain(lv_obj_get_parent(toggle));
        lv_obj_set_size(details, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(details, 10, 0);
        build_details();
    } else if (lv_obj_has_flag(details, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(details, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(details, LV_OBJ_FLAG_HIDDEN);
    m3_flash_usage_view_refresh();
}

void m3_flash_usage_view_refresh(void) {
    if (!summary) return;
    m3_lang_t language = m3_i18n_language();
    char used[24], slot[24], free_text[24], line[128];
    format_size(used, sizeof(used), usage.image_bytes);
    if (usage.slot_bytes) {
        uint32_t left = usage.slot_bytes > usage.image_bytes ? usage.slot_bytes - usage.image_bytes : 0;
        unsigned share = permille(usage.image_bytes, usage.slot_bytes);
        format_size(slot, sizeof(slot), usage.slot_bytes); format_size(free_text, sizeof(free_text), left);
        snprintf(line, sizeof(line), "%s %s / %s (%u.%u%%), %s %s", text_of(S_USED), used, slot,
            share / 10, share % 10, text_of(S_FREE), free_text);
    } else snprintf(line, sizeof(line), "%s %s", text_of(S_USED), used);
    lv_label_set_text(summary, usage.measured ? line : text_of(S_MISSING));
    lv_label_set_text(note, text_of(S_CAPTION));
    for (unsigned g = 0; g < M3_FLASH_GROUP_COUNT; ++g) if (legend_names[g])
        lv_label_set_text(legend_names[g], m3_flash_usage_group_name((m3_flash_group_t)g, language));
    for (unsigned i = 0; i < M3_FLASH_PART_COUNT; ++i) if (part_names[i])
        lv_label_set_text(part_names[i], m3_flash_usage_part_name((m3_flash_part_t)i, language));
    bool open = details && !lv_obj_has_flag(details, LV_OBJ_FLAG_HIDDEN);
    if (toggle_text) lv_label_set_text_fmt(toggle_text, "%s %s", open ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
        text_of(open ? S_HIDE : S_SHOW));
}

void m3_flash_usage_view_build(lv_obj_t *box, const m3_flash_view_style_t *style) {
    view_style = *style;
    m3_flash_usage_get(&usage);
    summary = add_label(box, view_style.text, false); lv_obj_set_width(summary, LV_PCT(100));
    note = add_label(box, view_style.muted, false); lv_obj_set_width(note, LV_PCT(100));
    if (!usage.measured) { m3_flash_usage_view_refresh(); return; }

    /* Stacked bar over the whole app slot; the empty track on the right is free space. */
    uint32_t scale = usage.slot_bytes > usage.image_bytes ? usage.slot_bytes : usage.image_bytes;
    bar = plain(box);
    lv_obj_set_size(bar, LV_PCT(100), 18);
    lv_obj_set_style_bg_color(bar, lv_color_hex(view_style.track), 0); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 9, 0); lv_obj_set_style_clip_corner(bar, true, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    for (unsigned g = 0; g < M3_FLASH_GROUP_COUNT; ++g) {
        unsigned share = permille(group_bytes((m3_flash_group_t)g), scale);
        if (!share) continue;
        lv_obj_t *segment = plain(bar);
        lv_obj_set_size(segment, LV_PCT((share + 5) / 10 ? (int32_t)((share + 5) / 10) : 1), LV_PCT(100));
        lv_obj_set_style_bg_color(segment, lv_color_hex(group_colors[g]), 0);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
    }
    for (unsigned g = 0; g < M3_FLASH_GROUP_COUNT; ++g) {
        uint32_t bytes = group_bytes((m3_flash_group_t)g);
        if (!bytes) continue;
        lv_obj_t *item = line_row(box);
        lv_obj_t *dot = plain(item);
        lv_obj_set_size(dot, 14, 14); lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(group_colors[g]), 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        legend_names[g] = add_label(item, view_style.text, true);
        legend_values[g] = add_label(item, view_style.muted, false);
        char value[40]; format_share(value, sizeof(value), bytes, usage.image_bytes);
        lv_label_set_text(legend_values[g], value);
    }
    toggle = lv_button_create(box);
    lv_obj_remove_style_all(toggle);
    lv_obj_add_style(toggle, view_style.button, 0); lv_obj_add_style(toggle, view_style.button_pressed, LV_STATE_PRESSED);
    toggle_text = lv_label_create(toggle); lv_obj_center(toggle_text);
    lv_obj_add_event_cb(toggle, toggle_details, LV_EVENT_CLICKED, NULL);
    m3_flash_usage_view_refresh();
}
