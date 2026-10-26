// Host checks for theme styles, colors, outline/corner options and procedural wallpapers. No firmware build or display.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m3_ui.c'),themeUi=read('main/m3_theme_ui.inc'),wall=read('main/m3_wallpaper.c');
const header=read('main/include/m3_theme.h'),store=read('main/m3_theme.c'),cmake=read('main/CMakeLists.txt');
const num=name=>Number(new RegExp(name+' (\\d+)').exec(header)[1]);
const count=num('M3_WALLPAPER_COUNT');
assert(cmake.includes('"m3_theme.c"')&&cmake.includes('"m3_wallpaper.c"'),'theme sources are registered');
// Accents and neutral roles are runtime variables: no static table may capture them.
for(const [macro,value] of [['CYAN','theme_primary'],['MAGENTA','theme_accent'],['ON_ACCENT','theme_on_accent'],['INK','ui_palette.ink'],
 ['SURFACE','ui_palette.surface'],['TEXT','ui_palette.text'],['MUTED','ui_palette.muted'],['FIELD','ui_palette.field'],['ON_WALL','theme_on_wall']])
 assert(ui.includes(`#define ${macro} ${value}`),macro);
const roles=/(?:CYAN|MAGENTA|ON_ACCENT|INK|SURFACE|SURFACE_HI|LINE|TEXT|MUTED|FIELD|KEY_BG|KEY_ITEM|KEY_CHECKED|KEY_TEXT|DANGER_BG|DANGER_TEXT|ON_WALL|ON_WALL_MUTED)/;
for(const src of [ui,...['m3_phone_ui.inc','m3_gallery_ui.inc','m3_reader_ui.inc','m3_ytmusic_ui.inc','m3_theme_ui.inc','m3_sound_ui.inc'].map(f=>read('main/'+f))])
 for(const m of src.matchAll(/static const[^;=]*=\s*\{([^;]*)\};/g))assert(!new RegExp('\\b'+roles.source+'\\b').test(m[1].replace(/"(?:[^"\\]|\\.)*"/g,'')),'static initializer uses a theme color: '+m[0].slice(0,80));
// Saved blob v2 with v1 migration, and settings start-up order.
assert(/sizeof\(m3_theme_t\) == 16/.test(store)&&/length == 12 && blob\[0\] == 1/.test(store)&&store.includes('"theme"'));
assert(ui.indexOf('m3_theme_load(&ui_theme); theme_resolve();')<ui.indexOf('styles_init(); theme_styles();'));
// Settings tabs: Display, Theme, Sound, System; every card lives in exactly one tab page.
const settings=ui.slice(ui.indexOf('static void build_settings('),ui.indexOf('static void build_voice_panel('));
assert(ui.includes('{T_SET_SCREEN, T_THEME, T_SET_SOUND, T_SET_TAB_SYSTEM}'));
assert(settings.includes('theme_card(settings_pages[SET_TAB_THEME]);')&&!settings.includes('card(system,'),'no card outside the tab pages');
for(const [tab,cards] of [['display',['T_SET_SCREEN','T_SET_FONT','T_SET_PERFORMANCE','T_SEC_LANGUAGE']],['sound_tab',['T_SET_SOUND','T_ACCESSIBILITY','T_SEC_VOICE']],
 ['info',['T_SET_OPERATION','T_SYSTEM_RESTART','T_SET_FLASH_USAGE','T_SEC_ABOUT']]])for(const id of cards)assert(settings.includes(`card(${tab}, ${id})`),id);
// Four complete styles with sixteen distinct neutral roles each.
const styles=[...store.matchAll(/\[M3_STYLE_(\w+)\] = \{\{([^}]*)\}/g)];
assert.deepEqual(styles.map(m=>m[1]),['DARK','WHITE','MODERN','CLASSIC']);
for(const [,name,body] of styles){const v=body.match(/0x[0-9a-f]{6}/g);assert.equal(v.length,16,name);assert.equal(new Set(v).size,16,name+' roles must differ');}
const dark=styles[0][2].match(/0x[0-9a-f]{6}/g);
assert(ui.includes('static m3_palette_t ui_palette = {'+dark.slice(0,7).join(', ')+','),'boot palette equals the dark style');
for(const id of ['STYLE_DARK','STYLE_WHITE','STYLE_MODERN','STYLE_CLASSIC','THEME_STYLE'])assert(read('main/include/m3_i18n.h').includes(`TEXT(${id})`),id);
// Home text sits on the wallpaper and follows its brightness.
for(const hook of ['home_clock = label(home, "--:--", ON_WALL)','text(home, app_names[app_icons[i].app], ON_WALL)','home_net = label(status_card, "", ON_WALL)'])
 assert(ui.includes(hook),hook);
assert(/bool m3_wallpaper_light\(unsigned id\) \{ return id >= WALL_DAWN && id < M3_WALLPAPER_COUNT; \}/.test(wall));
// The status bar and activity panel use theme roles, not fixed dark colors.
for(const hook of ['glyph_part(power_plug,','lv_color_hex(SHADE_TOP)','lv_color_hex(SHADE_BOTTOM)','lv_color_hex(GLASS), 0); lv_obj_set_style_bg_opa(panel',
 'i < levels ? TEXT : LINE','power_charge, lv_color_hex(ON_ACCENT)'])assert(ui.includes(hook),hook);
assert(themeUi.includes('home_page_update(); render_game();'),'games repaint after a theme change');
// Every wallpaper has a name, background, timer period and a preview color.
const names=[...(/names\[M3_WALLPAPER_COUNT\] = \{([^}]*)\}/.exec(themeUi)[1]).matchAll(/T_WALL_(\w+)/g)].map(m=>m[1]);
assert.equal(names.length,count);
assert.equal((/backgrounds\[M3_WALLPAPER_COUNT\]\[2\] = \{([\s\S]*?)\};/.exec(wall)[1].match(/\{0x/g)||[]).length,count);
const periods=/periods\[M3_WALLPAPER_COUNT\] = \{([^}]*)\}/.exec(wall)[1].split(',').map(Number);
assert.equal(periods.length,count);
periods.forEach((ms,i)=>assert(ms===0||ms>=40&&ms<=100,`wallpaper ${i} period ${ms}`));
const liveCount=periods.filter(Boolean).length,lightIndex=names.indexOf('DAWN');
assert(periods.slice(lightIndex).filter(Boolean).length>=3,'light wallpapers include at least three live ones');
assert(/\[M3_STYLE_WHITE\][\s\S]*?M3_CORNER_ROUND, (\d+), 0\}/.exec(store)[1]>=lightIndex,'White starts on a light wallpaper');
// Live motion stops outside Home and while the screen is off, and does not depend on the animation setting.
assert(/m3_wallpaper_set_active\(current_app == APP_HOME && !screen_blanked\)/.test(themeUi));
for(const hook of ['screen_blanked = true; theme_wallpaper_sync();','screen_release_at = now; theme_wallpaper_sync();',
 'm3_photos_activate(current_app == APP_HOME && home_index == HOME_PHOTOS);\n    theme_wallpaper_sync();'])assert(ui.replace(/\r\n/g,'\n').includes(hook),hook);
assert(/MAX_PARTS = 32/.test(wall),'live wallpapers stay small');
assert(!/if \(active == value\) return;/.test(wall),'activation always re-syncs the timer');
// Port of theme_distinct(): roles never equal a fixed color or each other, so the recolor walk is unambiguous.
const fixedNames=[...themeUi.split('fixed[] = {')[1].split('};')[0].matchAll(/0x[0-9a-f]+|[A-Z_]+/g)].map(m=>m[0]);
const define=n=>n.startsWith('0x')?parseInt(n,16):parseInt(new RegExp('#define '+n+' (0x[0-9a-f]+)').exec(ui)[1],16);
const reserved=new Set([...fixedNames.map(define),...[...ui.matchAll(/\{APP_\w+, (0x[0-9a-f]+), (0x[0-9a-f]+),/g)].flatMap(m=>[parseInt(m[1],16),parseInt(m[2],16)])]);
function distinct(c,taken){for(let s=0;s<16;++s){if(!taken.includes(c)&&!reserved.has(c))break;c^=1<<(s%3*8);}return c;}
const swatches=/theme_swatches\[\] = \{([^}]*)\}/.exec(themeUi)[1].split(',').map(n=>parseInt(n,16));
for(const [,,body] of styles){
 const taken=[];for(const v of body.match(/0x[0-9a-f]{6}/g).map(n=>parseInt(n,16)))taken.push(distinct(v,taken));
 for(const p of [...swatches,0xffffff])for(const q of swatches){
  const all=[...taken];all.push(distinct(p,all));all.push(distinct(q,all));
  assert.equal(new Set(all).size,all.length);all.forEach(v=>assert(!reserved.has(v)));}
}
console.log(`PASS: ${styles.length} styles, ${count} wallpapers (${liveCount} live), runtime palette and accents, unambiguous recolor values, NVS v2 blob and motion hooks.`);
