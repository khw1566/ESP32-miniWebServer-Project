// Host checks for the YouTube client (main/m4_tube.c) and its contract with ../tube-relay.
// Executes the actual frame-scheduling C bodies with mocks; no firmware build or network.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const tube=read('main/m4_tube.c'),header=read('main/include/m4_tube.h'),audio=read('main/m4_audio.c'),ui=read('main/m4_ui.c');
function body(name,source=tube){
 const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;
 for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
 return source.slice(start,end-1);
}
// IDF may fail perform() while still exposing the relay's HTTP 401. Keep that
// status for the token-rejected UI instead of reporting a connection failure.
const getContext={ESP_OK:0,TAG:'fixture',result:0,status:200,overflow:false,sink:{overflow:false},client:{},data:[],capacity:16,url:'fixture',
 esp_http_client_set_url:()=>{},esp_http_client_perform:()=>{getContext.sink.overflow=getContext.overflow;return getContext.result;},
 esp_http_client_get_status_code:()=>getContext.status,esp_err_to_name:()=> 'fixture',ESP_LOGW:()=>{}};
vm.createContext(getContext);
vm.runInContext('function get(){'+body('http_get').replace(/esp_err_t result =/,'const result =').replace(/int status =/,'const status =').replaceAll('sink->','sink.')+'}',getContext);
assert.equal(getContext.get(),200,'successful response');
getContext.result=-1;getContext.status=401;
assert.equal(getContext.get(),401,'preserve rejected token despite IDF perform error');
getContext.status=200;assert.equal(getContext.get(),-1,'partial response remains a transport error');
getContext.status=0;assert.equal(getContext.get(),-1,'connection failure remains an error');
getContext.result=0;getContext.status=200;getContext.overflow=true;
assert.equal(getContext.get(),-2,'overflow remains an error');
const define=name=>Number(new RegExp('#define '+name+' (\\d+)').exec(header)[1]);
const enumValue=name=>Number(new RegExp('\\b'+name+' = ([^,]+?)[,\\n]').exec(tube)[1].replace(/\s*\*\s*/g,'*').split('*').reduce((a,b)=>a*Number(b),1));

// Frame slots: the UI shows the newest frame that is due, releases older ones,
// and never reuses the frame it is displaying.
const SLOTS=enumValue('FRAME_SLOTS');
const ctx={FRAME_SLOTS:SLOTS,SLOT_FREE:0,SLOT_DECODING:1,SLOT_READY:2,SLOT_SHOWN:3,frame_lock:{},clock:-1,
 slots:Array.from({length:SLOTS},(_,i)=>({data:'buf'+i,pts:0,state:0})),
 portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},atomic_load:()=>ctx.clock,clock_ms:null,NULL:null};
vm.createContext(ctx);
const js=src=>src.replace('atomic_load(&clock_ms)','atomic_load()').replaceAll('&frame_lock','frame_lock').replace(/\bconst uint8_t \*shown/,'let shown').replace(/\bint (\w+)/g,'let $1').replace(/\(uint32_t\)now/g,'now')
 .replace(/\(int\)slots/g,'slots');
vm.runInContext('function frame(){'+js(body('m4_tube_frame'))+'}\nfunction claim(now){'+js(body('frame_claim'))+'}',ctx);
const decode=(pts)=>{const s=ctx.claim(ctx.clock);if(s<0)return -1;ctx.slots[s].pts=pts;ctx.slots[s].state=2;return s;};
assert.equal(ctx.frame(),null,'no clock, no frame');
for(let i=0;i<4;++i)decode(i*67);
ctx.clock=0;assert.equal(ctx.frame(),ctx.slots[0].data);
ctx.clock=140;const shown=ctx.frame();assert.equal(shown,ctx.slots[2].data,'skips to the newest due frame');
assert.equal(ctx.slots.filter(s=>s.state===3).length,1);assert.equal(ctx.slots[0].state,0);assert.equal(ctx.slots[1].state,0);
assert.equal(ctx.frame(),null,'nothing new is due');
for(let i=0;i<10;++i)decode(300+i*67);
assert.equal(ctx.slots[2].state,3,'decoder never overwrites the displayed buffer');
assert(ctx.slots.filter(s=>s.state===2).length<=SLOTS-1);
ctx.clock=2000;assert.notEqual(decode(2100),-1,'frames the clock already passed are recycled');
assert.equal(ctx.frame()!==null,true);

// URL encoding: run the actual C loop on the UTF-8 bytes of a Korean query.
const encoder={hex:'0123456789ABCDEF',isalnum:c=>/[A-Za-z0-9]/.test(String.fromCharCode(c))};vm.createContext(encoder);
vm.runInContext('function encode(bytes,size){let out=[];let n=0;let i=0;'+body('url_encode')
 .replace(/static const char hex\[\] = "[^"]*";/,'').replace('size_t n = 0;','')
 .replace('for (; *in && n + 4 < size; ++in) {','for (; i < bytes.length && n + 4 < size; ++i) {')
 .replace('unsigned char c = (unsigned char)*in;','const c = bytes[i];').replace('out[n++] = (char)c;','out[n++] = String.fromCharCode(c);')
 .replace('out[n] = 0;','')+'return out.join("");}',encoder);
const query=[...Buffer.from('뉴진스 MV&x=1','utf8')];
assert.equal(encoder.encode(query,1000),encodeURIComponent('뉴진스 MV&x=1'));
for(const text of ['日本語の歌 YOASOBI','夜に駆ける','日'.repeat(100)])
 assert.equal(encoder.encode([...Buffer.from(text)],3*385+1),encodeURIComponent(text),'Japanese UTF-8 URL bytes');
assert(!encoder.encode(query,12).includes('%E')||encoder.encode(query,12).length<=11,'stays inside the buffer');

// Protocol contract with the relay.
const lib=fs.readFileSync(path.join(root,'..','tube-relay','lib.mjs'),'utf8');
const server=fs.readFileSync(path.join(root,'..','tube-relay','server.mjs'),'utf8');
const packets=Object.fromEntries([.../(\w+): (\d)/g[Symbol.matchAll](/PACKET = \{([^}]+)\}/.exec(lib)[1])].map(m=>[m[1],Number(m[2])]));
const firmware=/enum \{ PACKET_INFO = 1, PACKET_VIDEO, PACKET_AUDIO, PACKET_END, PACKET_WAIT \}/.test(tube);
assert(firmware);assert.deepEqual(packets,{INFO:1,VIDEO:2,AUDIO:3,END:4,WAIT:5});
assert(server.includes('video: { width: 480, height: 272, fps: 15')&&define('M4_TUBE_VIDEO_W')===480&&define('M4_TUBE_VIDEO_H')===272);
assert.equal(272%16,0,'hardware JPEG output height must be a multiple of 16');
assert(server.includes("audio: { rate: 24000, channels: 1, chunkMs: 40 }")&&server.includes("music: { rate: 32000, channels: 1 }"));
// Mono packets are widened in place to stereo, so twice the payload must fit the PCM buffer.
for(const rate of [24000,32000])assert(2*(rate*2*40/1000)<=enumValue('AUDIO_LIMIT'),'one widened PCM packet fits the audio buffer');
assert(tube.includes('length > AUDIO_LIMIT / (3 - channels)')&&tube.includes('mono_to_stereo((int16_t *)pcm, frames)'));
const widen=new Function('samples','frames',body('mono_to_stereo').replace('unsigned i','let i')+';return samples;');
assert.deepEqual(widen([1,2,3,0,0,0],3),[1,1,2,2,3,3],'in-place mono to stereo keeps sample order');
assert(server.includes('videoFilter(profile)')&&lib.includes('format=yuvj420p'),'baseline 4:2:0 JPEG for every P4 quality profile');
// Output latency equals the I2S DMA ring: 6 descriptors x 240 frames at 24 kHz.
assert(/AUDIO_DMA_DESCS = 6, AUDIO_DMA_FRAMES = 240/.test(audio));assert.equal(enumValue('OUTPUT_FRAMES'),6*240);
assert(tube.includes('latency_ms = OUTPUT_FRAMES * 1000 / rate;'),'output latency follows the stream rate');
// Buffering hysteresis: refill more after a stall than before the first sample.
assert(enumValue('VIDEO_REBUFFER_BYTES')>enumValue('VIDEO_PREBUFFER_BYTES')&&enumValue('MUSIC_REBUFFER_BYTES')>enumValue('MUSIC_PREBUFFER_BYTES'));
for(const prefix of ['LOW','STANDARD']){
 assert(enumValue(prefix+'_REBUFFER_BYTES')>enumValue(prefix+'_PREBUFFER_BYTES'));
 assert(enumValue(prefix+'_PREBUFFER_BYTES')<enumValue('VIDEO_PREBUFFER_BYTES'),'lower bandwidth should not wait for the high-quality byte threshold');
}
assert(enumValue('RING_BYTES')>=4*enumValue('VIDEO_REBUFFER_BYTES'));
assert(server.includes("'/api/thumb'")&&server.includes("'rgb565le'")&&define('M4_TUBE_THUMB_W')===160&&define('M4_TUBE_THUMB_H')===90);
assert(tube.includes('esp_http_client_set_header(http, "x-m3-token", M4_TUBE_TOKEN)'));
assert(server.includes("req.headers['x-m3-token']")&&server.includes('crypto.timingSafeEqual'));
assert(server.includes("mediaServer.listen(config.mediaPort, '127.0.0.1')"),'range proxy stays on loopback');
// Ownership: playback runs on the audio worker, nothing in the service calls LVGL, the token stays private.
assert(audio.includes('m4_tube_run(request.name, request.speed, request.aac, &output)'));
// YouTube Music: song search, audio-only playback without the JPEG decoder, separate listing.
assert(tube.includes('request->music ? "&music=1" : ""')&&server.includes("url.searchParams.get('music') === '1'"));
assert(tube.includes('audio_only ? "&audio=1" : ""')&&server.includes("url.searchParams.get('audio') === '1'"));
assert(tube.includes('if (!audio_only && !frames_ready())'),'music never allocates video frames');
assert(tube.includes('target->thumb_set = (request->music ? 2 : 0) + ((target->thumb_set ^ 1) & 1);'),'music and video have separate thumbnail pairs');
assert(tube.includes('static uint8_t *thumb_sets[4]'));
assert(tube.includes('strlen(text) >= M4_TUBE_QUERY_BYTES')&&tube.includes('!m4_utf8_valid(text)'),'reject byte truncation rather than splitting a Japanese character');
assert(ui.includes('#include "m4_ytmusic_ui.inc"')&&ui.includes('if (player.audio_only) player.active = player.buffering = player.paused = false;'));
assert(!/\blv_\w+\(/.test(tube),'tube worker must not call LVGL');
assert(read('../.gitignore').includes('m4_tube_private.h')||fs.readFileSync(path.join(root,'..','.gitignore'),'utf8').includes('m4_tube_private.h'));
assert(ui.includes('if (current_app == APP_TUBE && app != APP_TUBE) tube_leave();'),'leaving the app stops playback');
assert(ui.includes('lv_canvas_set_buffer(tube_video, (void *)frame, M4_TUBE_VIDEO_W, M4_TUBE_VIDEO_H, LV_COLOR_FORMAT_RGB565)'));
console.log('PASS: due-frame selection, displayed-buffer protection, stale-frame recycling, Korean URL encoding, packet/size/rate/latency contract with tube-relay, loopback proxy, worker ownership.');
