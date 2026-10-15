// Run the shipped SD Lua source in a prebuilt WebAssembly Lua VM. No C compilation.
const {LuaFactory}=require('wasmoon');
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..');
(async()=>{
 const lua=await new LuaFactory().createEngine();
 let draws=0,lastScore=0;
 lua.global.set('rect',(x,y,w,h,c)=>{
   assert([x,y,w,h,c].every(Number.isInteger));
   assert(x>=0&&y>=0&&x+w<=160&&y+h<=192&&w>=0&&h>=0);
   assert(c>=0&&c<=65535);draws++;
 });
 lua.global.set('score',n=>{assert(Number.isInteger(n)&&n>=0);lastScore=n;});
 lua.global.set('SEED',12345);
 try {
  await lua.doString(fs.readFileSync(path.join(root,'sd_apps/apps/sand_tetris.lua'),'utf8')+`
local function grains() local n=0; for _,c in ipairs(grid) do if c~=0 then n=n+1 end end; return n end
-- Disconnected left/right colors and diagonal contact do not clear.
reset(); grid[key(0,H-1)]=1; grid[key(W-1,H-1)]=1
clear_connections(); assert(grains()==2 and points==0)
-- A bent, orthogonally connected path spans the walls and clears in full.
reset(); for x=0,W-1 do grid[key(x,H-1)]=2 end
grid[key(10,H-2)]=2; grid[key(10,H-3)]=2; grid[key(10,H-4)]=3
clear_connections(); assert(points==W+2 and grains()==1)
reset(); for x=0,W-1 do grid[key(x,H-1)]=x%2+1 end
clear_connections(); assert(points==0 and grains()==W)
-- Settling conserves grains, including at both side walls.
reset(); for x=0,W-1 do grid[key(x,20)]=x%3+1 end
for i=1,60 do ticks=i; settle() end
assert(grains()==W)
-- A tetromino becomes 4 * 9 grains on hard drop.
reset(); update(8); assert(grains()==36)
update(16); assert(grains()==0 and points==0 and not over)
for x=0,W-1 do for y=0,11 do grid[key(x,y)]=1 end end
spawn(); assert(over); update(16); assert(not over and grains()==0)
-- Execute the same instruction ceiling used by the embedded hook, including
-- a full-board flood fill. This checks Lua work, not device wall-clock timing.
local count,maxcount=0,0
debug.sethook(function() count=count+1000; assert(count<=600000,'instruction ceiling exceeded') end,'',1000)
reset(); for i=1,W*H do grid[i]=1 end
count=0; clear_connections(); assert(grains()==0 and points==W*H)
maxcount=math.max(maxcount,count)
reset()
for i=1,1200 do
  count=0
  local buttons=({0,1,2,4,8})[math.random(5)]
  if over then buttons=16 end
  update(buttons)
  maxcount=math.max(maxcount,count)
  for _,c in ipairs(grid) do assert(c>=0 and c<=3) end
end
debug.sethook()
assert(maxcount<600000)
HOST_MAX_INSTRUCTIONS=maxcount
`);
  assert(draws>10000);assert(lastScore>=0);
  console.log(`Sand Tetris: actual Lua source passed rules, conservation, 1200 frames, drawing bounds; peak ${lua.global.get('HOST_MAX_INSTRUCTIONS')} Lua instructions per tested operation.`);
 } finally {lua.global.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
