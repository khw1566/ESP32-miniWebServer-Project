// Host checks for theme colors, outline/corner options and procedural wallpapers. No firmware build or display.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m3_ui.c'),themeUi=read('main/m3_theme_ui.inc'),wall=read('main/m3_wallpaper.c');
const header=read('main/include/m3_theme.h'),store=read('main/m3_theme.c'),cmake=read('main/CMakeLists.txt');
const count=Number(/M3_WALLPAPER_COUNT (\d+)/.exec(header)[1]),live=Number(/M3_WALLPAPER_LIVE_FIRST (\d+)/.exec(header)[1]);
assert(cmake.includes('"m3_theme.c"')&&cmake.includes('"m3_wallpaper.c"'),'theme sources are registered');
// Accents are runtime variables: no static table may capture them.
assert(/#define CYAN theme_primary/.test(ui)&&/#define MAGENTA theme_accent/.test(ui)&&/#define ON_ACCENT theme_on_accent/.test(ui));
for(const src of [ui,...['m3_phone_ui.inc','m3_gallery_ui.inc','m3_reader_ui.inc','m3_ytmusic_ui.inc','m3_theme_ui.inc'].map(f=>read('main/'+f))])
 assert(!/static const[^;=]*=\s*\{[^;]*\b(?:CYAN|MAGENTA|ON_ACCENT)\b/.test(src),'static initializer uses a theme color');
// Saved blob, defaults and settings start-up order.
assert(/sizeof\(m3_theme_t\) == 12/.test(store)&&store.includes('"theme"')&&store.includes('"m3_device"'));
assert(ui.indexOf('m3_theme_load(&ui_theme); theme_resolve();')<ui.indexOf('styles_init(); theme_styles();'));
assert(ui.indexOf('theme_card(system);')>ui.indexOf('T_SET_WAKE_HINT')&&ui.indexOf('theme_card(system);')<ui.indexOf('card(system, T_SET_FONT)'));
// Every wallpaper has a name, background, timer period and a preview color.
const names=[.../T_WALL_(\w+)/g[Symbol.matchAll](/names\[M3_WALLPAPER_COUNT\] = \{([^}]*)\}/.exec(themeUi)[1])].map(m=>m[1]);
assert.equal(names.length,count);
assert.equal((/backgrounds\[M3_WALLPAPER_COUNT\]\[2\] = \{([\s\S]*?)\};/.exec(wall)[1].match(/\{0x/g)||[]).length,count);
const periods=/periods\[M3_WALLPAPER_COUNT\] = \{([^}]*)\}/.exec(wall)[1].split(',').map(Number);
assert.equal(periods.length,count);
periods.forEach((ms,i)=>assert(i<live?ms===0:ms>=60,`wallpaper ${i} period ${ms}`));
// Live motion stops outside Home, while the screen is off and with animations disabled.
assert(/current_app == APP_HOME && !screen_blanked && ui_preferences.animations/.test(themeUi));
for(const hook of ['screen_blanked = true; theme_wallpaper_sync();','screen_release_at = now; theme_wallpaper_sync();',
 'm3_photos_activate(current_app == APP_HOME && home_index == HOME_PHOTOS);\n    theme_wallpaper_sync();'])assert(ui.includes(hook),hook);
assert(/MAX_PARTS = 32/.test(wall),'live wallpapers stay small');
// Port of theme_distinct(): accents never equal a neutral, icon or each other, so the recolor walk is unambiguous.
const neutral=[...themeUi.split('neutral[] = {')[1].split('};')[0].matchAll(/0x[0-9a-f]+|[A-Z_]+/g)].map(m=>m[0]);
const define=n=>n.startsWith('0x')?parseInt(n,16):parseInt(new RegExp('#define '+n+' (0x[0-9a-f]+)').exec(ui)[1],16);
const reserved=new Set([...neutral.map(define),...[...ui.matchAll(/\{APP_\w+, (0x[0-9a-f]+), (0x[0-9a-f]+),/g)].flatMap(m=>[parseInt(m[1],16),parseInt(m[2],16)])]);
function distinct(c,a,b){for(let s=0;s<16;++s){if(c!==a&&c!==b&&!reserved.has(c))break;c^=1<<(s%3*8);}return c;}
const swatches=/theme_swatches\[\] = \{([^}]*)\}/.exec(themeUi)[1].split(',').map(n=>parseInt(n,16));
for(const p of [...swatches,0xffffff,0x000000,0x2dd4bf])for(const q of [...swatches,0xffffff]){
 const P=distinct(p,-1,-1),Q=distinct(q,P,-1);assert(P!==Q&&!reserved.has(P)&&!reserved.has(Q),`${p.toString(16)}/${q.toString(16)}`);}
for(const id of ['THEME','THEME_HINT','THEME_RESET','WALL_LIVE',...names.map(n=>'WALL_'+n)])assert(read('main/include/m3_i18n.h').includes(`TEXT(${id})`),id);
console.log(`PASS: ${count} wallpapers (${count-live} live), runtime accent colors, unambiguous recolor values, NVS blob and motion pause hooks.`);
