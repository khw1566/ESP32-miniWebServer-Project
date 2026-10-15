// Execute the C engine's integer-only policies in JS. This does not compile firmware.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_crossing.c'),header=read('main/include/m3_crossing.h');
const c={M3_GAME_LEFT:0,M3_GAME_RIGHT:1,M3_GAME_UP:2,M3_GAME_DOWN:3,
 M3_CROSS_GRASS:0,M3_CROSS_ROAD:1,M3_CROSS_RIVER:2};
for(const m of header.matchAll(/^#define (M3_CROSS_\w+) (\d+)$/gm))c[m[1]]=Number(m[2]);
let js=source.replace(/^#.*$/gm,'').replace(/\/\*[\s\S]*?\*\//g,'')
 .replace(/^(?:static )?(?:uint32_t|void|int|bool|m3_cross_lane_t) (\w+)\(([^)]*)\)\s*\{/gm,
 (_,name,args)=>'function '+name+'('+args.replace(/\b(?:const |m3_crossing_t|uint32_t|m3_game_direction_t)\s*/g,'').replace(/\*/g,'')+'){')
 .replace(/\b(?:uint32_t|unsigned|int|bool|m3_cross_lane_t) (\w+)/g,'let $1')
 .replace(/\(int\)/g,'').replace(/\b(\d+)U\b/g,'$1').replaceAll('->','.')
 .replace('value / 7','Math.floor(value / 7)')
 .replace('memset(game, 0, sizeof(*game))','Object.assign(game,{seed:0,row:0,best_row:0,camera:0,ticks:0,idle_ticks:0,x:0,started:false,dead:false})');
vm.createContext(c);vm.runInContext(js,c);
const reset=(seed=7)=>{const g={};c.m3_crossing_reset(g,seed);return g;};
const place=(g,row,inside)=>{g.row=row;g.camera=Math.max(0,row-3);g.best_row=row;
 g.x=(c.m3_crossing_offset(g,row)+inside)%198;if(g.x<20)g.x+=198;};
let g=reset();const initial=JSON.stringify(g);
for(let i=0;i<1000;i++)c.m3_crossing_tick(g);
assert.equal(JSON.stringify(g),initial,'ready state waits for the first move');
for(let i=0;i<10;i++)c.m3_crossing_move(g,0);
assert.equal(g.x,22);c.m3_crossing_move(g,0);assert.equal(g.x,22);
for(let i=0;i<20;i++)c.m3_crossing_move(g,1);
assert.equal(g.x,374);c.m3_crossing_move(g,3);assert.equal(g.row,0);
const speeds=new Set(),lanes=new Set();
for(let seed=1;seed<=30;seed++)for(let row=1;row<=100;row++){
 g=reset(seed);const lane=c.m3_crossing_lane(g,row);lanes.add(lane);
 if(row%4===0)assert.equal(lane,0,'regular safe rest rows');
 const speed=c.m3_crossing_speed(g,row);speeds.add(speed);
 g.ticks=197;const old=c.m3_crossing_offset(g,row);g.ticks=0;
 assert.equal((c.m3_crossing_offset(g,row)-old+198)%198,(speed+198)%198,'traffic wraps smoothly');
 if(lane===1){
  place(g,row,20);assert(!c.m3_crossing_safe(g),'car overlap kills');
  place(g,row,c.m3_crossing_length(g,row)+40);assert(c.m3_crossing_safe(g),'road gap is safe');
 }else if(lane===2){
  place(g,row,63);assert(c.m3_crossing_safe(g),'log supports the entire player');
  g.started=true;const x=g.x;c.m3_crossing_tick(g);assert.equal(g.x,x+speed);assert(!g.dead,'player rides with the log');
  place(g,row,160);assert(!c.m3_crossing_safe(g),'water gap kills');
 }
}
assert.deepEqual([...lanes].sort(),[0,1,2]);assert.equal(speeds.size,8);
g=reset();g.started=true;g.row=4;g.camera=1;g.x=5;assert(!c.m3_crossing_safe(g),'river carry cannot leave the board');
g=reset();g.row=3;c.m3_crossing_move(g,2);assert.equal(g.best_row,4);assert.equal(g.camera,1);
c.m3_crossing_move(g,0);assert.equal(g.best_row,4,'sideways movement cannot farm score');
g=reset();g.started=true;
for(let i=0;i<140;i++)c.m3_crossing_tick(g);
assert(g.dead,'camera catches a player who never advances');
const frozen=JSON.stringify(g);c.m3_crossing_move(g,2);c.m3_crossing_tick(g);assert.equal(JSON.stringify(g),frozen);
c.m3_crossing_reset(g,99);assert(!g.dead&&!g.started);assert.equal(g.best_row,0);assert.equal(g.camera,0);
// Entry into a live obstacle ends the game immediately, before the next tick.
g=reset();while(c.m3_crossing_lane(g,1)!==1)g.seed++;
const target=g;place(target,1,20);target.row=0;target.camera=0;target.best_row=0;
c.m3_crossing_move(target,2);assert(target.dead);
// Drive the actual UI timer with mocks to verify pause/visibility admission.
const ui=read('main/m3_ui.c');
const tick=/static void crossing_tick\(lv_timer_t \*timer\) \{([\s\S]*?)\n\}/.exec(ui)[1].replace('(void)timer;','');
const screen={crossing_game:true,current_app:1,APP_GAMES:1,crossing:{dead:false},shade_y:-756,HOME_TOP:756,
 sd_panel:null,LV_OBJ_FLAG_HIDDEN:1,lv_obj_has_flag:()=>false,
 ticks:0,renders:0,m3_crossing_tick:()=>screen.ticks++,render_crossing:()=>screen.renders++};
vm.createContext(screen);vm.runInContext('function frame(){'+tick.replace('&crossing','crossing')+'}',screen);
screen.frame();assert.equal(screen.ticks,1);
for(const change of [{current_app:0},{crossing_game:false},{crossing:{dead:true}},{shade_y:0},{sd_panel:{}}]){
 Object.assign(screen,{current_app:1,crossing_game:true,crossing:{dead:false},shade_y:-756},change);
 screen.frame();assert.equal(screen.ticks,1,'hidden/finished games and the activity panel pause gameplay');
}
assert(read('main/CMakeLists.txt').includes('"m3_crossing.c"'));
assert(read('tools/ui_sim/build.ps1').includes('main\\m3_crossing.c'));
assert(ui.includes('T_GAME_MEMORY, T_GAME_CROSSING'));
console.log('PASS: C-engine lane generation, all 8 traffic speeds, 3000 collision/log/wrap cases, bounds, scoring, idle loss, death/reset and UI pause guards. Host policies only; no C compilation or board execution.');
