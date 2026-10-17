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
// Progress status lines and their "NN%" suffix use the small font too.
const statuses = [...splash.matchAll(/\[M3_BOOT_\w+\] = \{(\d+), "([^"]+)"\}/g)];
assert.equal(statuses.length, 8, 'one status per boot step');
assert.equal(+statuses.at(-1)[1], 100); assert(statuses.every((s, i) => !i || +s[1] > +statuses[i - 1][1]), 'percent rises');
for (const ch of statuses.map(s => s[2]).join('') + '0123456789% ') assert(fonts.m3_font_splash_small.has(ch.codePointAt(0)), `status lacks ${ch}`);
// The splash waits for boot to finish (with a safety timeout) and the steps are reported in boot order.
assert(/step == M3_BOOT_READY[\s\S]*MIN_HOLD_MS\) \|\| lv_tick_elaps\(started_ms\) >= MAX_HOLD_MS/.test(splash));
assert(!/elapsed >= HOLD_MS/.test(splash), 'no fixed-time exit');
assert(/atomic_compare_exchange_weak\(&reached_step/.test(splash), 'steps only move forward, lock free');
const runtime = read('main/m3_runtime.c'), order = (src, keys) => keys.map(k => src.indexOf(k)).every((v, i, a) => v >= 0 && (!i || v > a[i - 1]));
assert(order(main, ['m3_boot_splash_start()', 'M3_BOOT_WORKERS', 'm3_agent_start()', 'M3_BOOT_UI', 'm3_ui_start()', 'm3_runtime_start(force_setup)']));
assert(order(runtime, ['M3_BOOT_STORAGE', 'M3_BOOT_NETWORK', 'net = network_init()', 'M3_BOOT_CONNECT', 'M3_BOOT_WEB', 'next.web = web_server_start()', 'next.initialized = true', 'M3_BOOT_READY']));
// Logo table matches its grid and only references defined palette entries.
const logo = read('main/m3_boot_logo.inc'), grid = +logo.match(/GRID (\d+)/)[1];
const palette = logo.match(/palette\[\] = \{([^}]*)\}/)[1].split(',').length;
const dots = logo.split('m3_boot_logo_dots')[1].match(/^\s+[\d,]+,$/gm).join(',').split(',').filter(v => v.trim()).map(Number);
assert.equal(dots.length, grid * grid); assert(dots.every(v => v >= 0 && v < palette)); assert(dots.some(v => v));
// UC stays the dot emblem; the new product mark is a separate sharp image.
const {spawnSync} = require('child_process');
const generated = spawnSync('python', ['-X','utf8','-c',
  'import sys,json;sys.path.insert(0,sys.argv[1]);import generate_boot_splash as g;print(json.dumps([*g.placeholder(),list(g.product_mark()[1])]))',
  path.join(root,'tools')], {encoding:'utf8'});
assert.equal(generated.status,0,generated.stderr);
const [expectedDots, expectedPalette, expectedSharp] = JSON.parse(generated.stdout);
assert.deepEqual(dots,expectedDots,'original UC geometry and firmware dots must stay synchronized');
const actualPalette=logo.match(/palette\[\] = \{([^}]*)\}/)[1].split(',').slice(1).map(v=>parseInt(v,16));
assert.deepEqual(actualPalette,expectedPalette.map(([r,g,b])=>(r<<16)|(g<<8)|b));
assert(logo.includes('UC overlap emblem'));assert.equal(grid,33);
const sharp=read('main/m3_boot_product_mark.inc');
const sharpBytes=[...sharp.split('};',1)[0].matchAll(/0x([0-9a-f]{2})/g)].map(m=>parseInt(m[1],16));
assert.deepEqual(sharpBytes,expectedSharp);assert.equal(sharpBytes.length,56*56*4);
const alpha=sharpBytes.filter((_,i)=>i%4===3);assert(alpha.includes(0)&&alpha.includes(255)&&alpha.some(v=>v>0&&v<255),'sharp transparent mark has antialiased edges');
assert(splash.includes('lv_image_set_src(product_mark, &m3_boot_product_mark)'));
assert(splash.includes('#define ACCENT 0x00E5EF'));
assert(splash.includes('lv_obj_set_pos(product_mark, PRODUCT_MARK_X, PRODUCT_MARK_Y)'));
const number=n=>+splash.match(new RegExp('#define '+n+' ([\\d.]+)'))[1];
const halfLogo=(grid-1)*number('PITCH')/2+number('DOT_RADIUS');
assert(halfLogo<number('CANVAS_W')/2);
assert(halfLogo+number('WAVE_PX')*2.5<number('CANVAS_H')/2,'ripple must stay inside the canvas');
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
console.log(`PASS: splash glyphs, ${grid}x${grid} dot logo, PSRAM buffer lifetime, progress steps, boot order, core plan and 400 MHz policy.`);
