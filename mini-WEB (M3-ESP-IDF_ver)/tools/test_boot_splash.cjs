// Boot splash and HP core placement contracts.
const fs = require('fs'), path = require('path'), assert = require('assert');
const root = path.resolve(__dirname, '..'), read = f => fs.readFileSync(path.join(root, f), 'utf8');
const splash = read('main/m3_boot_splash.c'), main = read('main/main.c'), cmake = read('main/CMakeLists.txt');
// Every glyph the splash prints must exist in the generated font it uses.
function glyphs(file) {
  const src = read(file), start = parseInt(src.match(/range_start=0x([0-9a-f]+)/)[1], 16);
  const list = src.match(/unicode_list\[\] = \{([^}]*)\}/)[1].split(',').map(v => start + parseInt(v, 16));
  return new Set(list);
}
const fonts = {m3_font_splash: glyphs('main/m3_font_splash.c'), m3_font_splash_small: glyphs('main/m3_font_splash_small.c')};
for (const [, text, font] of splash.matchAll(/text\("([^"]+)", &(m3_font_splash(?:_small)?)/g))
  for (const ch of text) assert(fonts[font].has(ch.codePointAt(0)), `${font} lacks ${ch} for "${text}"`);
assert(splash.includes('"창업경진대회"') && splash.includes('"MiniWEB 시제품"'));
// Logo table matches its grid and only references defined palette entries.
const logo = read('main/m3_boot_logo.inc'), grid = +logo.match(/GRID (\d+)/)[1];
const palette = logo.match(/palette\[\] = \{([^}]*)\}/)[1].split(',').length;
const dots = logo.split('m3_boot_logo_dots')[1].match(/^\s+[\d,]+,$/gm).join(',').split(',').filter(v => v.trim()).map(Number);
assert.equal(dots.length, grid * grid); assert(dots.every(v => v >= 0 && v < palette)); assert(dots.some(v => v));
// Buffer lives in PSRAM, the splash always dismisses itself and frees on delete.
assert(splash.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'));
assert(/deleted\(lv_event_t[\s\S]*lv_timer_delete\(frame_timer\)[\s\S]*heap_caps_free\(pixels\)/.test(splash));
assert(splash.includes('if (!frame_timer) lv_obj_delete(root)') && splash.includes('lv_obj_delete_async(root)'));
assert(splash.includes('lv_layer_top()'), 'home UI builds underneath on the active screen');
assert(main.indexOf('m3_boot_splash_start()') > main.indexOf('m3_board_init()') && main.indexOf('m3_boot_splash_start()') < main.indexOf('m3_ui_start()'));
for (const f of ['m3_boot_splash.c', 'm3_font_splash.c', 'm3_font_splash_small.c']) assert(cmake.includes(`"${f}"`), f);
// Core plan: media work on core 1 beside LVGL, network side on core 0.
assert(read('main/m3_audio.c').includes('NULL, 4, NULL, M3_CORE_MEDIA)'));
assert(read('main/m3_radio.c').includes('&dec, 4, NULL, M3_CORE_MEDIA)'));
assert(read('main/m3_radio.c').includes('xTaskCreatePinnedToCore(radio_receive, "m3_radio_rx", 12288, &rx, 5, NULL, 0)'));
assert(read('main/cloud_relay.c').includes('M3_CORE_NET'));
assert(read('main/dns_server.c').includes('M3_CORE_NET'));
assert(read('main/web_server.c').includes('&sd_index_task_handle,M3_CORE_MEDIA'));
assert(read('main/m3_board.c').includes('port.task_affinity = 1'));
const defaults = read('sdkconfig.defaults'), policy = read('cmake/memory_policy.cmake');
for (const key of ['CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y', 'CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y'])
  assert(defaults.includes(key) && policy.includes(key), key);
assert(policy.includes('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360'), 'old CPU frequency choice is cleared');
assert(read('main/m3_runtime.c').includes('#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS'));
console.log(`PASS: splash glyphs, ${grid}x${grid} dot logo, PSRAM buffer lifetime, boot order, core plan and 400 MHz policy.`);
