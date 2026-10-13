// Host checks for the YouTube client (main/m3_tube.c) and its contract with ../tube-relay.
// Executes the actual frame-scheduling C bodies with mocks; no firmware build or network.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const tube=read('main/m3_tube.c'),header=read('main/include/m3_tube.h'),audio=read('main/m3_audio.c'),ui=read('main/m3_ui.c');
function body(name,source=tube){
 const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;
 for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
 return source.slice(start,end-1);
}
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
vm.runInContext('function frame(){'+js(body('m3_tube_frame'))+'}\nfunction claim(now){'+js(body('frame_claim'))+'}',ctx);
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
assert(!encoder.encode(query,12).includes('%E')||encoder.encode(query,12).length<=11,'stays inside the buffer');

// Protocol contract with the relay.
const lib=fs.readFileSync(path.join(root,'..','tube-relay','lib.mjs'),'utf8');
const server=fs.readFileSync(path.join(root,'..','tube-relay','server.mjs'),'utf8');
const packets=Object.fromEntries([.../(\w+): (\d)/g[Symbol.matchAll](/PACKET = \{([^}]+)\}/.exec(lib)[1])].map(m=>[m[1],Number(m[2])]));
const firmware=/enum \{ PACKET_INFO = 1, PACKET_VIDEO, PACKET_AUDIO, PACKET_END, PACKET_WAIT \}/.test(tube);
assert(firmware);assert.deepEqual(packets,{INFO:1,VIDEO:2,AUDIO:3,END:4,WAIT:5});
assert(server.includes('video: { width: 480, height: 272, fps: 15')&&define('M3_TUBE_VIDEO_W')===480&&define('M3_TUBE_VIDEO_H')===272);
assert.equal(272%16,0,'hardware JPEG output height must be a multiple of 16');
assert(server.includes("audio: { rate: 24000, chunkMs: 40 }"));
const chunk=24000*4*40/1000;assert(chunk<=enumValue('AUDIO_LIMIT'),'one PCM packet fits the internal audio buffer');
assert(server.includes("format=yuvj420p"),'baseline 4:2:0 JPEG for the P4 decoder');
// Output latency equals the I2S DMA ring: 6 descriptors x 240 frames at 24 kHz.
assert(/AUDIO_DMA_DESCS = 6, AUDIO_DMA_FRAMES = 240/.test(audio));assert.equal(enumValue('OUTPUT_LATENCY_MS'),6*240*1000/24000);
assert(server.includes("'/api/thumb'")&&server.includes("'rgb565le'")&&define('M3_TUBE_THUMB_W')===160&&define('M3_TUBE_THUMB_H')===90);
assert(tube.includes('esp_http_client_set_header(http, "x-m3-token", M3_TUBE_TOKEN)'));
assert(server.includes("req.headers['x-m3-token']")&&server.includes('crypto.timingSafeEqual'));
assert(server.includes("mediaServer.listen(config.mediaPort, '127.0.0.1')"),'range proxy stays on loopback');
// Ownership: playback runs on the audio worker, nothing in the service calls LVGL, the token stays private.
assert(audio.includes('m3_tube_run(request.name, request.speed, &output)'));
assert(!/\blv_\w+\(/.test(tube),'tube worker must not call LVGL');
assert(read('../.gitignore').includes('m3_tube_private.h')||fs.readFileSync(path.join(root,'..','.gitignore'),'utf8').includes('m3_tube_private.h'));
assert(ui.includes('if (current_app == APP_TUBE && app != APP_TUBE) tube_leave();'),'leaving the app stops playback');
assert(ui.includes('lv_canvas_set_buffer(tube_video, (void *)frame, M3_TUBE_VIDEO_W, M3_TUBE_VIDEO_H, LV_COLOR_FORMAT_RGB565)'));
console.log('PASS: due-frame selection, displayed-buffer protection, stale-frame recycling, Korean URL encoding, packet/size/rate/latency contract with tube-relay, loopback proxy, worker ownership.');
