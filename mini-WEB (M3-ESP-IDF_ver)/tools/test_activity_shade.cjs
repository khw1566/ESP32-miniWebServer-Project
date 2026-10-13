// Host-only policies and UI API contracts; no screen or live memory measurement.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),ui=fs.readFileSync(path.join(root,'main/m3_ui.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(ui);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(ui[end]==='{')depth++;if(ui[end]==='}')depth--;end++;}return ui.slice(start,end-1);}
const ctx={};vm.createContext(ctx);
for(const [name,args] of [['wifi_bars','rssi'],['usage_percent','occupied,total'],['shade_should_open','start,dx,dy']])
 vm.runInContext('function '+name+'('+args+'){'+body(name).replace('(unsigned)(occupied * 100 / total)','Math.trunc(occupied * 100 / total)')+'}',ctx);
for(const [rssi,bars] of [[-40,4],[-55,4],[-56,3],[-67,3],[-68,2],[-75,2],[-76,1],[-85,1],[-86,0]])assert.equal(ctx.wifi_bars(rssi),bars);
for(const [used,total,value] of [[0,0,0],[25,100,25],[100,100,100],[150,100,100],[3,32,9]])assert.equal(ctx.usage_percent(used,total),value);
assert(!ctx.shade_should_open(-756,0,69));assert(ctx.shade_should_open(-756,0,70));
assert(!ctx.shade_should_open(-756,91,100));assert(!ctx.shade_should_open(-756,0,-100));
assert(ctx.shade_should_open(0,0,-49));assert(!ctx.shade_should_open(0,0,-50));
assert(ctx.shade_should_open(0,91,-100),'horizontal drags do not close open panel');
const gesture={HOME_TOP:756,shade_y:-756,shade_start_y:0,shade_drag:false,shade_origin:{},
 shade:{},shade_scrim:{},home_strip:{},input:true,settled:null,
 LV_EVENT_PRESSED:1,LV_EVENT_PRESSING:2,LV_EVENT_RELEASED:3,LV_EVENT_PRESS_LOST:4,
 lv_indev_active:()=>gesture.input,lv_indev_get_point:(_,p)=>Object.assign(p,gesture.point),lv_event_get_code:e=>e.code,
 lv_anim_delete:()=>{},shade_motion:(_,y)=>gesture.shade_y=Math.max(-756,Math.min(0,y)),
 shade_set:open=>{gesture.settled=open;gesture.shade_y=open?0:-756;gesture.shade_drag=false;},
 hide_keyboard:()=>{},lv_obj_move_to_index:()=>{},shade_should_open:ctx.shade_should_open};
vm.createContext(gesture);vm.runInContext('function touch(event){'+body('shade_touch')
 .replace('lv_indev_t *input','let input').replace('lv_point_t point;','let point = {};')
 .replace('&point','point').replace('lv_event_code_t code','let code').replace('int dx','let dx')+'}',gesture);
function touch(code,x,y){gesture.point={x,y};gesture.touch({code});}
touch(1,200,20);touch(2,200,60);assert.equal(gesture.shade_y,-716,'panel follows finger');
touch(3,200,60);assert.equal(gesture.settled,false,'short pull rolls back');
touch(1,200,20);touch(2,200,95);touch(3,200,95);assert.equal(gesture.settled,true);
touch(1,200,40);touch(2,200,0);touch(4,200,0);assert.equal(gesture.settled,true,'lost press restores open state');
touch(1,200,80);touch(2,200,20);touch(3,200,20);assert.equal(gesture.settled,false,'top handle swipe closes');
touch(1,200,250);touch(2,200,400);assert.equal(gesture.shade_y,-756,'app scrolling must not open shade');
assert(ui.includes('lv_arc_set_range(usage_arcs[i], 0, 100)'));
assert(ui.includes('snapshot.flash.total - snapshot.flash.reserved'));
assert(ui.includes('heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)'));
assert(ui.includes('job_update(2, tr(T_JOB_VOICE)'));assert(ui.includes('job_update(4, tr(T_JOB_WEB)'));
assert(!ui.includes('"Wi-Fi  %d dBm'),'status header uses icon rather than numeric quality');
console.log('PASS: Wi-Fi icon thresholds, bounded memory percentages, top-only pull/release/cancel/close, live jobs and independent Flash file capacity.');
