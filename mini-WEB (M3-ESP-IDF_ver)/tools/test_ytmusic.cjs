// Actual YouTube Music controller bodies with host service mocks; no native renderer/board.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const source=read('main/m3_ytmusic_ui.inc'),tube=read('main/m3_tube.c');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,n=1;while(n){if(source[i]==='{')n++;if(source[i]==='}')n--;i++;}return source.slice(start,i-1);}
const c={T_NONE:0,T_TUBE_NOT_CONFIGURED:1,T_TUBE_BUSY:2,T_TUBE_CONNECTING:3,T_TUBE_BUFFERING:4,T_TUBE_PAUSED:5,T_YTM_IDLE:6,
 APP_YTMUSIC:1,current_app:1,LV_ANIM_OFF:0,LV_STATE_PRESSED:1,LV_STATE_DISABLED:2,LV_SYMBOL_PAUSE:'pause',LV_SYMBOL_PLAY:'play',
 ytm_pending:true,ytm_submitted:false,ytm_was_active:false,ytm_pending_until:8000000,ytm_request_revision:0,ytm_finished_revision:0,ytm_error:0,
 ytm_queue:[{id:'aaaaaaaaaaa'}],ytm_queue_count:1,ytm_index:0,ytm_start_s:0,ytm_duration_s:180,
 seen_ytm_player:0,now:0,configured:true,accept:false,calls:0,
 player:{active:false,audio_only:false,revision:0,id:'',position_ms:0,buffering:false,ended:false},
 update_ytm_list:()=>{},ytm_update_cache:()=>{},ytm_update_cover:()=>{},ytm_prepare_pair:()=>{c.prepared=(c.prepared||0)+1;},
 lv_obj_add_flag:()=>{},LV_OBJ_FLAG_HIDDEN:1,ytm_cover_canvas:{},m3_tube_player:()=>({...c.player}),m3_tube_configured:()=>c.configured,
 m3_tube_play_audio:()=>{c.calls++;return c.accept;},esp_timer_get_time:()=>c.now,
 strcmp:(a,b)=>a===b?0:1,tr:id=>'T'+id,status_text:s=>s,segment_value:()=>1,
 lv_obj_has_state:()=>false,lv_obj_remove_state:()=>{},lv_obj_add_state:()=>{},lv_slider_set_range:()=>{},lv_slider_set_value:()=>{},
 lv_obj_get_child:o=>o,set_text:(o,s)=>{o.text=s;},activity_set:(o,on)=>{o.on=on;},
 format_clock:(seconds,out)=>{},ytm_start:()=>{throw Error('unexpected auto next');}};
for(const name of ['ytm_title','ytm_artist','ytm_seek','ytm_time','ytm_total','ytm_toggle','ytm_state','ytm_wait','ytm_autonext','ytm_prev_song','ytm_next_song'])c[name]={};
vm.createContext(c);
let update=body(source,'update_ytm').replace('m3_tube_player_t player; m3_tube_player(&player);','let player=m3_tube_player();')
 .replace(/\bbool\s+/g,'let ').replace('char text[64];','let text="";').replace('const char *state','let state')
 .replace(/\bunsigned\s+/g,'let ')
 .replaceAll('audio->','audio.').replaceAll('(int32_t)','').replaceAll('sizeof(text)','64')
 .replace('snprintf(text, 64, "%s  %u%%", state, player.buffer_percent)','text=state+"  "+player.buffer_percent+"%"');
vm.runInContext('function update(audio){'+update+'}',c);
c.update({playing:false,message:'idle'});assert(c.ytm_pending);assert.equal(c.ytm_state.text,'T3');
c.now=9000000;c.update({playing:false,message:'idle'});assert(!c.ytm_pending);assert.equal(c.ytm_state.text,'T2','queue rejection remains visible even with unchanged inactive player revision');
c.now=0;c.ytm_pending=true;c.ytm_error=0;c.ytm_submitted=false;c.accept=true;c.calls=0;
c.update({playing:false,message:'idle'});assert(c.ytm_pending&&c.ytm_submitted);assert.equal(c.calls,1);
c.update({playing:false,message:'idle'});assert.equal(c.calls,1,'accepted request is never enqueued twice');
c.player={...c.player,active:true,audio_only:true,revision:1,id:'aaaaaaaaaaa',buffering:true,buffer_percent:17};
c.update({playing:true,message:'buffering'});assert(!c.ytm_pending);assert(c.ytm_state.text.includes('17%'));
c.ytm_was_active=false;c.ytm_pending=true;c.ytm_submitted=true;c.ytm_request_revision=1;
c.player={...c.player,active:false,revision:2,buffering:false};
c.update({playing:false,message:'TLS connection failed'});assert(!c.ytm_pending);assert.equal(c.ytm_state.text,'TLS connection failed','fast failure reaches the user');
c.update({playing:false,message:'TLS connection failed'});assert.equal(c.ytm_state.text,'TLS connection failed');
// EOF following belongs to the audio worker. The UI adopts the next song without enqueuing it again.
c.ytm_pending=false;c.ytm_queue=[{id:'aaaaaaaaaaa',title:'First',channel:'Artist'},{id:'bbbbbbbbbbb',title:'Next',channel:'Artist',duration:180}];c.ytm_queue_count=2;c.ytm_index=0;
c.player={...c.player,active:false,audio_only:true,ended:true,revision:5,id:'aaaaaaaaaaa'};
const next=[];c.ytm_start=(i)=>{next.push(i);};c.update({playing:false,message:'Music ended'});
assert.equal(next.length,0,'UI never duplicates the audio-worker successor');
c.player={...c.player,active:true,ended:false,id:'bbbbbbbbbbb',revision:6};c.update({playing:true,message:'Music playing from PSRAM'});
assert.equal(c.ytm_index,1);assert.equal(c.ytm_title.text,'Next');assert.equal(c.prepared,1,'next pair advances even without an observed EOF edge');
c.update({playing:true,message:'Music playing from PSRAM'});assert.equal(c.prepared,1,'same track is adopted only once');
c.player.active=false;c.player.ended=true;c.update({playing:true,message:'Radio playing'});assert.equal(next.length,0,'stale EOF cannot interrupt another source');
assert(!source.includes('if (ytm_was_active && !active'),'no sampled-edge dependence');
assert(source.includes('m3_tube_music_prepare(ytm_queue[ytm_index].id'));
assert(tube.includes('unsigned revision = player.revision + 1;')&&tube.includes('player.revision = revision;'));
for(const music of [false,true]){
 let selected=0;const sets=[];for(let i=0;i<8;i++){selected=(music?2:0)+((selected^1)&1);sets.push(selected);}
 assert.deepEqual([...new Set(sets)].sort(),music?[2,3]:[0,1]);
}
assert(source.includes('ytm_list.thumbs != seen_ytm_thumbs'),'thumbnails update after metadata READY');
assert(source.includes('memcpy(ytm_cover_pixels, pixels, sizeof(ytm_cover_pixels))'),'playing cover survives another search');
assert(source.includes('static M3_PSRAM_BSS uint8_t ytm_cover_pixels'));
assert(read('main/m3_ui.c').includes('if (app_icons[index].app == APP_YTMUSIC)'));
console.log('PASS: actual music controller queue retry/timeout, no duplicate submit, playback/fast failure, thumbnail-pair isolation and retained-cover policies (host mocks only).');
