// Execute source helper bodies with host mocks; no compiler or real codec.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let at=m.index+m[0].length,end=at,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(at,end-1);}
function js(s){return s.replace(/\/\*[\s\S]*?\*\//g,'').replaceAll('->','.').replace(/\((?:uint64_t|uint32_t|uint16_t|int64_t|unsigned|int|size_t)\)/g,'')
 .replace(/\b(?:uint64_t|uint32_t|uint16_t|int64_t|uint8_t|unsigned|int|bool|size_t) (\w+)/g,'var $1').replace(/\b(\d+)U\b/g,'$1');}
const pixels=read('main/m3_mp4_pixels.c'),ctx={M3_PHOTO_W:432,M3_PHOTO_H:480,M3_PHOTO_BYTES:432*480*2,memset:(b,v,n)=>b.fill(v,0,n)};
vm.createContext(ctx);
for(const [name,args] of [['bits','b,count'],['ue','b'],['m3_mp4_avcc_sample','data,bytes,length_bytes'],['clamp','value'],['m3_mp4_i420_fit','data,bytes,width,height,canvas'],['m3_mp4_sps_valid','data,bytes,dimensions']]){
 let code=body(pixels,name).replace('!width || !height','!dimensions')
 .replaceAll('b->bit / 8','Math.floor(b->bit / 8)').replaceAll('sy / 2','Math.floor(sy / 2)').replaceAll('sx / 2','Math.floor(sx / 2)')
 .replace('x * width / w','Math.floor(x * width / w)').replace('y * height / h','Math.floor(y * height / h)')
 .replace('uint8_t rbsp[256];','var rbsp=new Uint8Array(256);')
 .replace('bits_t b = {.data = rbsp, .bytes = length};','var b={data:rbsp,bytes:length,bit:0,failed:false};')
 .replaceAll('&b','b').replaceAll('*width','dimensions.width').replaceAll('*height','dimensions.height')
 .replace('const uint8_t *u = data + width * height, *v = u + width * height / 4;', 'var u=data.subarray(width*height),v=u.subarray(width*height/4);');
 vm.runInContext('function '+name+'('+args+'){'+js(code)+'}',ctx);
}
const ue=n=>{let x=(n+1).toString(2);return '0'.repeat(x.length-1)+x;};
function sps({width=320,height=240,profile=66,flags=0x40,refs=1,progressive=1,crop=false}={}){
 const bits=[profile.toString(2).padStart(8,'0'),flags.toString(2).padStart(8,'0'),'00011110',ue(0),ue(0),ue(0),ue(0),ue(refs),'0',ue(Math.ceil(width/16)-1),ue(Math.ceil(height/16)-1),''+progressive,'1',crop?'1':'0',...(crop?[ue(0),ue(0),ue(0),ue((Math.ceil(height/16)*16-height)/2)]:[]),'0','1'].join('');
 const padded=bits.padEnd(Math.ceil(bits.length/8)*8,'0');return Buffer.from([0x67,...padded.match(/.{8}/g).map(x=>parseInt(x,2))]);
}
for(const options of [{},{width:640,height:480},{width:480,height:272},{width:320,height:238,crop:true}]){
 const b=sps(options),d={};assert(ctx.m3_mp4_sps_valid(b,b.length,d));assert.equal(d.width,options.width||320);assert.equal(d.height,options.height||240);
}
for(const options of [{profile:100},{flags:0},{width:656},{height:496},{refs:5},{progressive:0}]){const b=sps(options);assert(!ctx.m3_mp4_sps_valid(b,b.length,{}),JSON.stringify(options));}
assert(!ctx.m3_mp4_sps_valid(Buffer.from([0x67,66,64,30,0]),5,{}),'truncated SPS rejected');
const good=Buffer.from([0,0,0,2,0x65,0xff,0,0,0,1,9]);assert(ctx.m3_mp4_avcc_sample(good,good.length,4));
assert.deepEqual([...good],[0,0,0,1,0x65,0xff,0,0,0,1,9]);
for(const data of [[0,0,0],[0,0,0,0],[0,0,0,2,0x65],[0,0,0,1,0x67],[0,0,0,1,0x68],[0,0,0,1,0xe5]])assert(!ctx.m3_mp4_avcc_sample(Buffer.from(data),data.length,4));
assert(!ctx.m3_mp4_avcc_sample(good,good.length,2));
for(const [y,expected] of [[16,0],[235,0xffff]]){
 const input=Buffer.alloc(16*16*3/2,128);input.fill(y,0,16*16);const out=Buffer.alloc(432*480*2,0xff);
 assert(ctx.m3_mp4_i420_fit(input,input.length,16,16,out));assert.equal(out.readUInt16LE((24*432)*2),expected);
 assert.equal(out.readUInt16LE(0),0,'letterbox cleared');
}
assert(!ctx.m3_mp4_i420_fit(Buffer.alloc(10),10,16,16,Buffer.alloc(432*480*2)));
assert(!ctx.m3_mp4_i420_fit(Buffer.alloc(1000),1000,17,16,Buffer.alloc(432*480*2)));
// Execute the actual decode loop with consumed-byte / frame / cancellation mocks.
const player=read('main/m3_mp4.c');
let code=body(player,'decode').replace('esp_h264_dec_in_frame_t input = {.raw_data = {.buffer = data, .len = bytes}, .pts = pts < 0 ? 0 : (uint32_t)(pts / 1000)};',
 'var input={raw_data:{buffer:0,len:bytes},pts:pts<0?0:Math.floor(pts/1000),consume:0};')
 .replace('esp_h264_dec_out_frame_t output = {0};','var output={out_size:0};').replace('esp_h264_resolution_t size = {0};','var size={};')
 .replace('uint8_t *canvas','var canvas').replaceAll('&input','input').replaceAll('&output','output').replaceAll('&size','size')
 .replace('++*pictures','++pictures.count').replace('p->view->acquire(p->job, &slot)','p->view->acquire(p->job, {set value(x){slot=x;}})')
 .replace('esp_h264_err_t result','var result');
const decodeCtx={ESP_H264_ERR_OK:0,ESP_H264_ERR_MEM:2,M3_MP4_OK:0,M3_MP4_STOPPED:1,M3_MP4_MEMORY:4,M3_MP4_VIDEO_ERROR:5,
 alive:()=>true,wait_time:()=>true,clock_us:()=>0,m3_mp4_i420_fit:()=>true,esp_h264_dec_get_resolution:(p,s)=>{s.width=320;s.height=240;return 0;}};
vm.createContext(decodeCtx);vm.runInContext('function decode(p,data,bytes,pts,present,pictures){'+js(code)+'}',decodeCtx);
const p={info:{width:320,height:240,duration_ms:1000},view:{acquire:()=>null,publish:()=>{}}};
decodeCtx.esp_h264_dec_process=(h,i,o)=>{i.consume=4;return 0;};assert.equal(decodeCtx.decode(p,null,8,0,true,{count:0}),0);
decodeCtx.esp_h264_dec_process=(h,i,o)=>{i.consume=100;return 0;};assert.equal(decodeCtx.decode(p,null,8,0,true,{count:0}),5);
decodeCtx.esp_h264_dec_process=()=>0;assert.equal(decodeCtx.decode(p,null,8,0,true,{count:0}),5,'stalled decoder terminates');
decodeCtx.esp_h264_dec_process=()=>2;assert.equal(decodeCtx.decode(p,null,8,0,true,{count:0}),4,'OOM propagated');
decodeCtx.alive=()=>false;assert.equal(decodeCtx.decode(p,null,8,0,true,{count:0}),1,'cancellation exits');
assert(player.indexOf('while (video_started && !atomic_load(&p->video_done))')<player.indexOf('m3_mp4_demux_close(p->demux)'),'join before sharing file/index cleanup');
// Real metadata validator: byte bounds, table allocation counts and profile gates.
const demux=read('main/m3_mp4_demux.c'),metadata={SAMPLE_LIMIT:100000,UINT32_MAX:0xffffffff,INT32_MAX:0x7fffffff,
 BOX:(...chars)=>chars.reduce((n,c)=>n*256+c.charCodeAt(0),0),be32:p=>p.readUInt32BE(0),be64:p=>Number(p.readBigUInt64BE(0)),
 m3_mp4_sps_valid:(p,n,track)=>ctx.m3_mp4_sps_valid(p,n,track)};
vm.createContext(metadata);
for(const [name,args] of [['avcc','track,p,size'],['boxes','d,data,bytes,depth,track']]){
 let code=body(demux,name)
 .replace('const uint8_t *p = data + at + header;', 'var p=data.subarray(at+header);')
 .replace('const uint8_t *entry = p + 8 + i * unit;', 'var entry=p.subarray(8+i*unit);')
 .replace(/data \+ at \+ (\d+)/g,'data.subarray(at+$1)').replaceAll('data + at','data.subarray(at)')
 .replace(/p \+ (\d+)/g,'p.subarray($1)').replaceAll('p + pos','p.subarray(pos)').replaceAll('p + at','p.subarray(at)')
 .replace(/entry \+ (\d+)/g,'entry.subarray($1)')
 .replace('&track->width, &track->height','track').replace('&d->probe[d->tracks++]','d->probe[d->tracks++]')
 .replaceAll('NULL','null');
 vm.runInContext('function '+name+'('+args+'){'+js(code)+'}',metadata);
}
function be(n){const b=Buffer.alloc(4);b.writeUInt32BE(n);return b;}
function box(type,...data){const b=Buffer.concat(data);return Buffer.concat([be(b.length+8),Buffer.from(type),b]);}
const spsData=sps(),avc=Buffer.concat([Buffer.from([1,66,64,30,255,225,0,spsData.length]),spsData,Buffer.from([1,0,2,0x68,0])]);
assert(metadata.avcc({},avc,avc.length));
const high=Buffer.from(avc);high[1]=100;assert(!metadata.avcc({},high,high.length));
const table=(count,...entries)=>Buffer.concat([be(0),be(count),...entries.map(be)]);
function check(data){const d={current:()=>true,job:1,tracks:0,probe:Array.from({length:4},()=>({seen:0}))};return metadata.boxes(d,data,data.length,0,null);}
const stbl=box('stbl',box('stts',table(1,15,67)),box('stsz',be(0),be(100),be(15)),box('stsc',table(1,1,15,1)),box('stco',table(1,100)));
const mdhd=box('mdhd',be(0),be(0),be(0),be(1000),be(1000));
assert(check(box('trak',box('mdia',mdhd,box('minf',stbl)))));
for(const data of [Buffer.alloc(7),box('moof'),box('mvex'),box('trak',box('stts',table(100001))),
 box('trak',box('stts',table(1,1,100)),box('stts',table(1,1,100))),
 box('trak',box('ctts',table(1,1,1))),box('trak',box('stsc',table(1,0,1,1))),
 box('trak',box('stsz',be(0),be(0),be(100001)))])assert(!check(data),'invalid metadata rejected');
assert(read('main/CMakeLists.txt').includes('esp_audio_codec esp_h264'));
// Execute the orchestrator, including task completion before shared cleanup.
let orchestrator=body(player,'m3_mp4_play')
 .replace('player_t *p','var p').replace('(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED','null')
 .replace('esp_h264_dec_cfg_sw_t config = {.pic_type = ESP_H264_RAW_FMT_I420};','var config={};')
 .replace('uint8_t parameter[1400];','var parameter=Buffer.alloc(1400);')
 .replace('const uint8_t *data = m3_mp4_demux_parameter(p->demux, i != 0, 0, &bytes);','var data=Buffer.from([1]);bytes=1;')
 .replace('sizeof(parameter)','1400').replaceAll('sizeof(*p)','1')
 .replace('esp_h264_err_t created','var created').replace('m3_mp4_result_t result','var result')
 .replace('&p->demux','p').replace('&p->info','p.info').replace('&p->decoder','p').replace('&p->parameters','p')
 .replaceAll('&config','config').replaceAll('&ignored','{}')
 .replace(/&p->(stop|video_done|video_result)/g,'[p,"$1"]')
 .replaceAll('parameter + used','parameter.subarray(used)')
 .replaceAll('NULL','null').replaceAll('goto done;','break work;');
const split=orchestrator.split('done:');assert.equal(split.length,2);
orchestrator=js('work: {'+split[0]+'}\n'+split[1]);
function runPlayer({open=0,create=0,parameter=0,task=true,audioResult=0,cancel=false}={}){
 const events=[],c={Buffer,M3_MP4_FORMAT:2,M3_MP4_MEMORY:4,M3_MP4_OK:0,M3_MP4_STOPPED:1,M3_MP4_VIDEO_ERROR:5,
 ESP_H264_ERR_OK:0,ESP_H264_ERR_MEM:2,MALLOC_CAP_INTERNAL:1,MALLOC_CAP_8BIT:2,pdPASS:1,video_worker:()=>{},
 heap_caps_calloc:()=>({info:{},stop:false,video_done:false,video_result:0}),
 atomic_init:([p,k],v)=>p[k]=v,atomic_store:([p,k],v)=>p[k]=v,atomic_load:([p,k])=>p[k],
 m3_mp4_demux_open:(s,j,current,p)=>{events.push('index');p.demux={};return open;},
 esp_h264_dec_sw_new:(cfg,p)=>{events.push('decoder');p.decoder={};return create;},
 esp_h264_dec_open:()=>0,esp_h264_dec_sw_get_param_hd:()=>parameter,
 decode:()=>0,esp_timer_get_time:()=>0,alive:()=>!cancel,clock_us:()=>0,
 memset:(b,v,n)=>b.fill(v,0,n),memcpy:(to,from,n)=>{if(typeof from==='string')from=Buffer.from(from,'binary');from.copy(to,0,0,n);},
 xTaskCreatePinnedToCore:(worker,name,stack,p)=>{events.push('task');c.pending=p;return task?1:0;},
 audio_track:()=>audioResult,pdMS_TO_TICKS:x=>x,
 vTaskDelay:()=>{events.push('joined');assert(c.pending.stop || audioResult===0);c.pending.video_done=true;},
 esp_h264_dec_close:()=>events.push('close'),esp_h264_dec_del:()=>events.push('delete'),
 m3_mp4_demux_close:()=>{assert(!task||!events.includes('task')||c.pending.video_done);events.push('file-close');},free:()=>events.push('free')};
 vm.createContext(c);vm.runInContext('function play(relative,job,audio,view){'+orchestrator+'}',c);
 const result=c.play('/clip.mp4',1,{cancelled:()=>cancel,message:()=>{}},{current:()=>!cancel,paused:()=>false,acquire:()=>{},publish:()=>{}});
 return {result,events};
}
assert.equal(runPlayer().result,0);
assert.deepEqual(runPlayer({open:2}).events,['index','file-close','free']);
assert.equal(runPlayer({create:2}).result,4);
assert.equal(runPlayer({parameter:1}).result,5);
assert.equal(runPlayer({task:false}).result,4);
const failedAudio=runPlayer({audioResult:6});assert.equal(failedAudio.result,6);assert(failedAudio.events.indexOf('joined')<failedAudio.events.indexOf('delete'));
assert.equal(runPlayer({cancel:true}).result,1);
const clock={esp_timer_get_time:()=>clock.now,now:100,portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{}};
vm.createContext(clock);vm.runInContext('function time(p){'+js(body(player,'clock_us')).replaceAll('&p.clock_lock','null')+'}',clock);
let pause=false;const timing={view:{paused:()=>pause},origin:100,paused_us:0,clock_paused:false};
assert.equal(clock.time(timing),0);clock.now=200;pause=true;assert.equal(clock.time(timing),100);
clock.now=900;assert.equal(clock.time(timing),100,'paused clock holds both tracks');pause=false;assert.equal(clock.time(timing),100);
clock.now=1000;assert.equal(clock.time(timing),200,'resume excludes pause duration');
const ascCode=js(body(demux,'audio_config')).replace('static const var rates[] = {','var rates = [').replace('7350};','7350];')
 .replace('track.dsi + 2','track.dsi.subarray(2)');
const asc={memcmp:(a,b,n)=>Buffer.compare(a.subarray(0,n),Buffer.from(b,'binary'))};vm.createContext(asc);
vm.runInContext('function config(track,info){'+ascCode+'}',asc);
function config(bytes,{rate=44100,channels=2}={}){const info={};return asc.config({dsi:Buffer.from(bytes),dsi_bytes:bytes.length,sample_count:1,SampleDescription:{audio:{samplerate_hz:rate,channelcount:channels}}},info);}
assert(config([0x12,0x10]));assert(config([0x12,0x10,0x56,0xe5,0]));
for(const bytes of [[0x12,0x10,0x56,0xe5,0x80],[0x2a,0x10],[0x12,0x11],[0x12,0x10,0],[0x12,0x00]])assert(!config(bytes));
assert(!config([0x12,0x10],{rate:48000}));
console.log('PASS: source MP4 metadata/codec bounds, SPS/AVCC, I420 pixels, decoder errors/cancel, pause clock, playback cleanup/join, AAC-LC/FFmpeg configuration. Host mocks only.');
