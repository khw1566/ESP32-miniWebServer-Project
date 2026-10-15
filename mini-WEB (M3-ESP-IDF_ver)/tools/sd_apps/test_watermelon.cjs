// Execute the delivered Lua game, including dense collision and drawing workloads.
const {LuaFactory}=require('wasmoon');
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
(async()=>{
 const lua=await new LuaFactory().createEngine();
 const pixels=new Uint16Array(160*192);let draws=0,lastScore=0;
 lua.global.set('SEED',76543);
 lua.global.set('score',n=>{assert(Number.isInteger(n)&&n>=0);lastScore=n;});
 lua.global.set('rect',(x,y,w,h,c)=>{
  assert([x,y,w,h,c].every(Number.isInteger));
  assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=160&&y+h<=192);
  assert(c>=0&&c<=65535);draws++;
  for(let row=y;row<y+h;row++)pixels.fill(c,row*160+x,row*160+x+w);
 });
 try {
  await lua.doString(fs.readFileSync(path.join(root,'sd_apps/apps/watermelon.lua'),'utf8')+`
local function fruit(x,y,level,age) return {x=x,y=y,level=level,age=age or 0,vx=0,vy=0} end
reset(); current=1; nextfruit=2; aim=-999; drop()
assert(#fruits==1 and fruits[1].x==LEFT+radii[1] and current==2 and cooldown==12)
drop(); assert(#fruits==1,'cooldown blocks rapid repeat')
-- Equal fruit merge exactly at contact; unequal fruit only collide.
reset(); fruits={fruit(50,150,1),fruit(60,150,1)}; collide()
assert(#fruits==1 and fruits[1].level==2 and points==40)
reset(); fruits={fruit(50,150,1),fruit(60,150,2)}; collide()
assert(#fruits==2 and points==0)
-- Three touching fruit cannot reuse a consumed fruit for two merges.
reset(); fruits={fruit(50,150,1),fruit(50,150,1),fruit(50,150,1)}; collide()
assert(#fruits==2 and points==40)
-- Every upgrade works, including the watermelon goal; max-level fruit remain.
for level=1,#radii-1 do
 reset(); fruits={fruit(80,130,level),fruit(80,130,level)}; collide()
 assert(#fruits==1 and fruits[1].level==level+1)
 assert(points==(level+1)^2*10)
 assert(won==(level+1==#radii))
end
reset(); fruits={fruit(80,130,11),fruit(80,130,11)}; collide()
assert(#fruits==2 and points==0)
-- Wall/floor containment, finite values, grace and continuous overflow timer.
local f=fruit(-90,500,11); f.vx=-10; f.vy=10; contain(f)
assert(f.x==LEFT+32 and f.y==FLOOR-32 and f.vx>=0 and f.vy<=0)
reset(); fruits={fruit(80,24,1,20)}; loss_check(); assert(danger==0)
fruits[1].age=21
for i=1,39 do loss_check() end
assert(not over and danger==39); loss_check(); assert(over)
reset(); fruits={fruit(80,24,1,21)}; loss_check(); fruits[1].y=100; loss_check(); assert(danger==0)
reset(); for i=1,MAX do fruits[i]=fruit(80,150,1) end
drop(); assert(over and #fruits==MAX)
update(16); assert(not over and #fruits==0 and points==0)
-- Measure VM instructions, including the largest permitted fruit population.
local count,maxcount=0,0
debug.sethook(function() count=count+1000; assert(count<=600000,'instruction ceiling exceeded') end,'',1000)
reset()
for i=1,MAX do fruits[i]=fruit(20+(i%7)*18,55+(i//7)*16,(i%11)+1,40) end
count=0; update(0); maxcount=math.max(maxcount,count)
reset()
for i=1,MAX do fruits[i]=fruit(80,120,11,40) end
count=0; update(0); maxcount=math.max(maxcount,count)
reset()
for i=1,1600 do
 count=0
 update(over and 16 or ({0,1,2,4,8})[math.random(5)])
 maxcount=math.max(maxcount,count)
 assert(#fruits<=MAX)
 for _,f in ipairs(fruits) do
  local r=radii[f.level]
  assert(f.x==f.x and f.y==f.y and f.vx==f.vx and f.vy==f.vy)
  assert(f.x>=LEFT+r-0.001 and f.x<=RIGHT-r+0.001 and f.y<=FLOOR-r+0.001)
 end
end
debug.sethook()
HOST_MAX_INSTRUCTIONS=maxcount
-- A deterministic gameplay canvas for optional visual inspection.
reset(); fruits={fruit(37,155,11),fruit(94,164,9),fruit(135,171,6),fruit(85,124,7),fruit(129,145,4),fruit(49,110,5)}
current=4; nextfruit=2; aim=109; points=2470; score(points); paint()
`);
  assert(draws>10000);assert.equal(lastScore,2470);
  if(process.argv.includes('--preview')){
   const rgb=Buffer.alloc(pixels.length*3);
   pixels.forEach((c,i)=>{rgb[i*3]=Math.round((c>>11)*255/31);rgb[i*3+1]=Math.round(((c>>5)&63)*255/63);rgb[i*3+2]=Math.round((c&31)*255/31);});
   fs.mkdirSync(path.join(__dirname,'out'),{recursive:true});
   fs.writeFileSync(path.join(__dirname,'out/watermelon.ppm'),Buffer.concat([Buffer.from('P6\n160 192\n255\n'),rgb]));
  }
  console.log(`Watermelon: actual Lua passed all upgrades, exact contact, cooldown, collision, loss/restart, 1600 frames and drawing bounds; peak ${lua.global.get('HOST_MAX_INSTRUCTIONS')} instructions.`);
 } finally {lua.global.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
