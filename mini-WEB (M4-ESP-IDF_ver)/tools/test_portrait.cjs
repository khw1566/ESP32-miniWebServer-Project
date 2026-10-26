// Source contracts and actual gesture conversion; no native compilation.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
const board=read('main/m4_board.c'),pins=read('main/include/m4_board.h');
const config=read('sdkconfig.defaults'),kconfig=read('main/Kconfig.projbuild');
for(const [key,value] of Object.entries({M4_PANEL_WIDTH:1024,M4_PANEL_HEIGHT:600,M4_DISPLAY_WIDTH:600,M4_DISPLAY_HEIGHT:1024,M4_WIDTH:480,M4_HEIGHT:800}))
 assert(pins.includes(`#define ${key} ${value}`));
const defined=new Set([...kconfig.matchAll(/^\s*(?:config|choice)\s+(\w+)/gm)].map(m=>m[1]));
for(const file of ['main/m4_board.c','main/include/m4_board.h','main/m4_panel_init.c'])
 for(const m of read(file).matchAll(/CONFIG_(M4_\w+)/g))assert(defined.has(m[1]),`missing Kconfig ${m[1]}`);
for(const setting of ['CONFIG_M4_PANEL_V2=y','CONFIG_M4_LCD_RESET=0','CONFIG_M4_TOUCH_RESET=-1','CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000'])assert(config.includes(setting));
assert(!read('cmake/memory_policy.cmake').includes('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y'));
assert(board.includes('.lane_bit_rate_mbps = 750'));
assert(/\.phy_clk_src\s*=\s*0\s*,/.test(board));
assert(board.includes('esp_lcd_new_panel_jd9165(io, &dev, &panel)'));
assert(!board.includes('ST7701')&&!board.includes('esp_lcd_new_panel_dpi('));
assert(board.includes('.x_max = M4_PANEL_WIDTH, .y_max = M4_PANEL_HEIGHT'));
assert(board.includes('.sw_rotate = true')&&!board.includes('.direct_mode = true'));
assert(!board.includes('.flags.avoid_tearing = true')&&board.includes('.flags.use_dma2d = true'));
assert(board.includes('LV_DISPLAY_ROTATION_90')&&board.includes('LV_DISPLAY_ROTATION_270'));
assert(board.includes('.num_fbs = 1')&&board.includes('.buffer_size = M4_PANEL_WIDTH * 64'));
assert(board.indexOf('esp_lcd_panel_init(panel)')<board.lastIndexOf('m4_board_set_brightness(preferences.brightness)'));
const table=read('main/m4_panel_init.c');
assert(table.includes('{M4_PANEL_WIDTH, M4_PANEL_HEIGHT, 24, 136, 160, 2, 21, 12}'));
assert(table.includes('{M4_PANEL_WIDTH, M4_PANEL_HEIGHT, 40, 160, 160, 10, 23, 12}'));
assert.equal((table.match(/C\(LCD_CMD_COLMOD, 0x55\)/g)||[]).length,2);
assert.equal((table.match(/LCD_CMD_INVOFF/g)||[]).length,2);
const ui=read('main/m4_ui.c'),splash=read('main/m4_boot_splash.c');
assert(ui.includes('m4_layout_create_root(physical_screen)'));
assert(splash.includes('m4_layout_create_root(lv_layer_top())'));
// Execute the actual C arithmetic with a mocked LVGL point read.
const conversion=read('main/m4_layout.c').split('void m4_layout_get_point(')[1].split('{')[1].split('}')[0]
 .replace('lv_indev_get_point(indev, point);','').replace(/\/\*[\s\S]*?\*\//g,'')
 .replaceAll('point->','point.').replaceAll('M4_LAYOUT_SCALE','320').replaceAll('M4_LAYOUT_TOP','12');
for(const [x,y,wantX,wantY] of [[0,12,0,0],[300,512,240,400],[600,1012,480,800],[125,262,100,200]]){
 const ctx={point:{x,y}};vm.runInNewContext(conversion,ctx);assert.deepEqual(ctx.point,{x:wantX,y:wantY});
}
assert.equal(480*320/256,600);assert.equal(800*320/256+12*2,1024);
assert(!ui.includes('lv_indev_get_point('));assert(!read('main/m4_reader_ui.inc').includes('lv_indev_get_point('));
const power=read('main/m4_power.c');assert(!power.includes('adc_oneshot_')&&!power.includes('i2c_master_probe('));
assert(power.includes('memset(out, 0, sizeof(*out))'));
const cmake=read('main/CMakeLists.txt');
for(const m of cmake.matchAll(/"([^"\n]+\.c)"/g))assert(fs.existsSync(path.join(root,'main',m[1])),m[1]);
for(const src of ['m4_layout.c','m4_panel_init.c','m4_board.c','m4_audio.c','m4_sd_settings.c'])assert(cmake.includes(`"${src}"`));
assert(read('sdkconfig.rev3.defaults').includes('CONFIG_ESP32P4_REV_MIN_301=y'));
console.log('PASS: JD9165 V1/V2, portrait rotation/scaling/gesture points, build source integration and unknown battery.');

const relay=read('main/m4_tube.c');
assert(relay.includes('"x-m3-token"')&&relay.includes('"m3-tube-relay"'));
assert(!relay.includes('x-m4-token'));
assert(read('main/include/m4_tube_config.h').includes('#define M4_TUBE_TOKEN m4_sd_tube_token()'));
assert(!read('main/include/m4_gemini_config.h').includes('_private.h'));
assert(read('main/m4_sd_settings.c').includes('"tube_token"'));
