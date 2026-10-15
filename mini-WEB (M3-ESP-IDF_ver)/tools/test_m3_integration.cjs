// Host source contracts; does not build ESP-IDF or emulate board peripherals.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
const board=read('main/m3_board.c'),pins=read('main/include/m3_board.h');
const config=read('sdkconfig.defaults'),ui=read('main/m3_ui.c');
const runtime=read('main/m3_runtime.c'),audio=read('main/m3_audio.c'),main=read('main/main.c');
const pinMap=Object.fromEntries([...pins.matchAll(/^#define (M3_\w+) (\d+)$/gm)].map(m=>[m[1],+m[2]]));
assert.equal(pinMap.M3_WIDTH,480);assert.equal(pinMap.M3_HEIGHT,800);
assert.deepEqual(['LCD_RESET','LCD_BACKLIGHT','I2C_SDA','I2C_SCL','I2S_MCLK','I2S_BCLK','I2S_LRCK','I2S_DOUT','I2S_DIN','AMP_ENABLE'].map(n=>pinMap['M3_'+n]),[5,23,7,8,13,12,10,9,48,11]);
assert.equal(new Set(Object.entries(pinMap).filter(([n])=>!['M3_WIDTH','M3_HEIGHT'].includes(n)).map(([,v])=>v)).size,10);
assert(board.includes('.num_fbs = 2'));assert(board.includes('.flags.avoid_tearing = true'));
assert(/\.phy_clk_src\s*=\s*0\s*,/.test(board),'DSI PHY must use the revision-aware IDF default');
assert(!/\.phy_clk_src\s*=\s*MIPI_DSI_PHY_CLK_SRC_DEFAULT\b/.test(board),'legacy PLL_F20M aborts on P4 v3.x');
assert(board.includes('.direct_mode = true')&&!board.includes('.full_refresh = true'),'redraw only changed areas');assert(board.includes('.flags.use_dma2d = true'));
assert(board.includes('.chan_id = 3, .voltage_mv = 2500'));assert(board.includes('touch_address.dev_addr = address'));
assert(board.indexOf('esp_lcd_panel_init(panel)')<board.lastIndexOf('gpio_set_level(M3_LCD_BACKLIGHT, 1)'));
assert(board.includes('port.task_affinity = 1'));assert(read('main/web_server.c').includes('config.core_id = 0'));
assert.equal(480*800*2*2,1536000);assert(480*800*2*2<32*1024*1024/16);
for(const setting of ['CONFIG_LV_USE_CLIB_MALLOC=y','CONFIG_LV_USE_QRCODE=y','CONFIG_LV_FONT_MONTSERRAT_20=y','CONFIG_CODEC_ES8311_SUPPORT=y','CONFIG_M3_TOUCH_RESET=3'])assert(config.includes(setting));
const texts=read('main/m3_i18n.c');
for(const name of ['Home','Wi-Fi','Files','Audio','Settings','홈','파일','오디오','설정'])assert(texts.includes('"'+name+'"'));
assert(!/\b(fopen|fread|opendir|stat|vTaskDelay|network_connect_sta|network_scan_wifi|storage_view_read)\s*\(/.test(ui),'UI must not perform blocking storage/network work');
assert(!/\blv_\w+\s*\(/.test(runtime+audio),'worker tasks must not call LVGL');
assert(runtime.includes('sd_media_admin_lock()'));assert(runtime.includes('sd_media_admin_unlock()'));
assert(runtime.includes('xQueueOverwrite'));assert(runtime.includes('network_start_softap()'));
assert(main.indexOf('m3_ui_start()')<main.indexOf('m3_runtime_start(force_setup)'));
assert(audio.includes('sd_media_name_valid(name)'));assert(audio.includes('strcasecmp(ext, ".wav")'));
assert(audio.includes('recording ? M3_RECORDER_FOLDER : "/media"'));
assert(audio.includes('m3_wav_parse_limit(file, &wav, recording ? M3_RECORDER_MAX_SECONDS * M3_RECORDER_RATE * 2 + 44 : SD_MEDIA_MAX_BYTES)'));
assert(audio.includes('percent > 150 ? 150 : percent'));assert(audio.includes('esp_codec_dev_close(codec)'));
function body(name){const re=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{'),match=re.exec(audio);assert(match);let depth=1,end=match.index+match[0].length,start=end;for(;depth;end++){if(audio[end]==='{')depth++;if(audio[end]==='}')depth--;}return audio.slice(start,end-1);}
// Execute the actual small cancellation policy bodies with a queue mock.
const policy='function cancelled(){'+body('cancelled').replace('atomic_load(&cancel_epoch)','cancel_epoch')+'}\nfunction stop(){'+body('m3_audio_stop').replace('atomic_fetch_add(&cancel_epoch, 1)','cancel_epoch++')+'}';
const queue={pending:true},ctx={cancel_epoch:0,active_epoch:0,recorder_stops:0,requests:queue,xQueueReset:q=>q.pending=false,
 m3_voice_cancel:()=>{},m3_recorder_cancel:()=>ctx.recorder_stops++,gallery_stops:0,m3_gallery_audio_cancel:()=>ctx.gallery_stops++};
vm.createContext(ctx);vm.runInContext(policy,ctx);assert(!ctx.cancelled());ctx.stop();assert(ctx.cancelled());assert(!queue.pending);
assert.equal(ctx.recorder_stops,1,'global stop also releases a queued recorder request');
assert.equal(ctx.gallery_stops,1,'global stop also invalidates MP4 playback');
ctx.active_epoch=0;assert(ctx.cancelled(),'dequeued old command stays cancelled');ctx.active_epoch=1;assert(!ctx.cancelled(),'fresh command is accepted');ctx.stop();assert(ctx.cancelled());
assert(/active_epoch = request\.epoch;\s+if \(cancelled\(\)\) continue;/.test(audio));
assert(read('main/idf_component.yml').includes('"2.7.2"'));
assert(!main.includes('m3_audio_test()'),'startup must not automatically sound the speaker');
const cmake=read('main/CMakeLists.txt');
// Every application-owned configuration reference must have a Kconfig definition.
const kconfig=read('main/Kconfig.projbuild');
const relayConfig=read('main/include/cloud_relay_config.h');
assert(read('main/cloud_relay.c').includes('#include "cloud_relay_config.h"'));
const relayDefaults=[...relayConfig.matchAll(/#ifndef CONFIG_(\w+)\s+#define CONFIG_\1 "([^"]*)"\s+#endif/g)];
assert.equal(relayDefaults.length,3);
for(const match of relayDefaults){
 const entry=new RegExp('config '+match[1]+'\\s+string [^\\n]+\\s+default "([^"]*)"').exec(kconfig);
 assert(entry,`missing relay Kconfig ${match[1]}`);
 assert.equal(match[2],entry[1],`fallback differs from menuconfig: ${match[1]}`);
}
const defined=new Set([...kconfig.matchAll(/^\s*(?:menuconfig|config)\s+(\w+)/gm)].map(m=>m[1]));
for(const name of fs.readdirSync(path.join(root,'main')).filter(n=>n.endsWith('.c'))){
 for(const match of read('main/'+name).matchAll(/\bCONFIG_((?:MINI_WEB_|M3_)\w+)\b/g))
  assert(defined.has(match[1]),`${name}: missing Kconfig definition for ${match[1]}`);
}
for(const source of [...cmake.matchAll(/"([^"\n]+\.c)"/g)].map(m=>m[1]))assert(fs.existsSync(path.join(root,'main',source)),source);
const html=read('web/index.html'),factory=read('main/web_page_factory.c');
const decoded=[...factory.split('const char FACTORY_INDEX_HTML[] =')[1].split(';\n')[0].matchAll(/^\s*"(?:\\.|[^"\\])*"/gm)].map(m=>JSON.parse(m[0].trim())).join('');
assert.equal(decoded,html);assert(!/<(?:script|link|img)[^>]+(?:src|href)=["']https?:/i.test(html));
assert(html.includes('@media(max-width:760px)'));assert(html.includes('prefers-reduced-motion'));
assert(!html.includes('Waveshare'));assert(html.includes('Mini-WebServer-M3-Setup'));
console.log('PASS: M3 pin profile, buffer budget/synchronization, GUI isolation, SD locking, audio boundaries, boot recovery, CMake source integration and offline responsive factory page.');
