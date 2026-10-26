-- Original SD fruit-merging game. Only rect(), score(), SEED and update() are used.
-- Left/right aim; down or rotate drops; New resets. 20 simulation frames/second.
local radii={5,6,8,10,12,15,18,21,24,28,32}
local colors={0xd949,0xd194,0x897b,0xfb65,0xfd63,0xfccf,0xff84,0xfbea,0xb65f,0x9e64,0x2548}
local LEFT,RIGHT,FLOOR,LINE,MAX=5,155,188,44,40
local fruits,aim,current,nextfruit,points,cooldown,danger,over,won
math.randomseed(SEED or 12345)
local function clamp(v,a,b) return math.max(a,math.min(b,v)) end
local function reset()
  fruits={}; aim=80; current=math.random(5); nextfruit=math.random(5)
  points=0; cooldown=0; danger=0; over=false; won=false; score(0)
end
local function contain(f)
  local r=radii[f.level]
  if f.x-r<LEFT then f.x=LEFT+r; f.vx=math.abs(f.vx)*0.2 end
  if f.x+r>RIGHT then f.x=RIGHT-r; f.vx=-math.abs(f.vx)*0.2 end
  if f.y+r>FLOOR then
    f.y=FLOOR-r; f.vy=-math.abs(f.vy)*0.12; f.vx=f.vx*0.85
    if math.abs(f.vy)<0.3 then f.vy=0 end
  end
end
local function drop()
  if over or cooldown>0 then return end
  if #fruits>=MAX then over=true; return end
  local r=radii[current]
  fruits[#fruits+1]={x=clamp(aim,LEFT+r,RIGHT-r),y=24,vx=0,vy=0,level=current,age=0}
  current=nextfruit; nextfruit=math.random(5); cooldown=12
end
local function collide()
  local i=1
  while i<=#fruits do
    local a=fruits[i]; local j=i+1
    while j<=#fruits do
      local b=fruits[j]; local sum=radii[a.level]+radii[b.level]
      local dx,dy=b.x-a.x,b.y-a.y
      if math.abs(dx)<=sum and math.abs(dy)<=sum then
        local d2=dx*dx+dy*dy
        if d2<=sum*sum then
          if a.level==b.level and a.level<#radii then
            a.x=(a.x+b.x)*0.5; a.y=(a.y+b.y)*0.5
            a.vx=(a.vx+b.vx)*0.35; a.vy=(a.vy+b.vy)*0.35
            a.age=math.max(a.age,b.age); a.level=a.level+1
            table.remove(fruits,j); contain(a)
            points=points+a.level*a.level*10; score(points)
            if a.level==#radii then won=true end
          else
            local d=math.sqrt(d2)
            if d<0.001 then dx=1; dy=0; d=1 end
            local nx,ny=dx/d,dy/d
            local ra,rb=radii[a.level],radii[b.level]
            local wa=rb*rb/(ra*ra+rb*rb); local wb=1-wa
            local push=(sum-d)*0.8
            a.x=a.x-nx*push*wa; a.y=a.y-ny*push*wa
            b.x=b.x+nx*push*wb; b.y=b.y+ny*push*wb
            local closing=(b.vx-a.vx)*nx+(b.vy-a.vy)*ny
            if closing<0 then
              local impulse=-closing*1.1
              a.vx=a.vx-impulse*nx*wa; a.vy=a.vy-impulse*ny*wa
              b.vx=b.vx+impulse*nx*wb; b.vy=b.vy+impulse*ny*wb
            end
            contain(a); contain(b); j=j+1
          end
        else j=j+1 end
      else j=j+1 end
    end
    i=i+1
  end
end
local function physics()
  for _,f in ipairs(fruits) do f.age=f.age+1 end
  for step=1,2 do
    for _,f in ipairs(fruits) do
      f.vx=clamp(f.vx*0.985,-5,5); f.vy=clamp(f.vy+0.23,-5,5)
      f.x=f.x+f.vx; f.y=f.y+f.vy; contain(f)
    end
    collide()
  end
end
local function loss_check()
  local high=false
  for _,f in ipairs(fruits) do
    if f.age>20 and f.y-radii[f.level]<LINE then high=true; break end
  end
  danger=high and danger+1 or 0
  if danger>=40 then over=true end
end
-- Scanline circles use the existing bounded RGB565 rectangle API.
local function box(x,y,w,h,c)
  local x1=clamp(math.floor(x),0,160); local y1=clamp(math.floor(y),0,192)
  local x2=clamp(math.floor(x+w),0,160); local y2=clamp(math.floor(y+h),0,192)
  if x2>x1 and y2>y1 then rect(x1,y1,x2-x1,y2-y1,c) end
end
local function circle(x,y,r,c)
  x=math.floor(x); y=math.floor(y)
  for dy=-r,r do
    local span=math.floor(math.sqrt(r*r-dy*dy))
    box(x-span,y+dy,span*2+1,1,c)
  end
end
local function draw_fruit(x,y,level,small)
  local r=small and math.min(radii[level],7) or radii[level]
  circle(x,y,r,colors[level])
  if not small then
    if level==11 then
      for dx=-r+6,r-5,9 do box(x+dx,y-r*0.55,2,r*1.1,0x14a5) end
    elseif level==3 then
      circle(x-r*0.3,y-r*0.2,2,0xb2df)
    elseif level==9 then
      box(x-r*0.6,y-2,r*1.2,2,0x782e)
    end
    box(x-1,y-r-2,2,3,0x4c26); box(x+1,y-r-2,3,2,0x65e7)
    local eye=math.max(2,math.floor(r*0.3))
    box(x-eye,y,1,2,0x0841); box(x+eye,y,1,2,0x0841)
    if r>=10 then box(x-2,y+4,5,1,0x0841) end
    if r>=12 then box(x-r*0.45,y-r*0.45,3,2,0xffff) end
  end
end
local font={N={5,7,7,7,5},E={7,4,6,4,7},X={5,5,2,5,5},T={7,2,2,2,2},
 G={7,4,5,5,7},A={2,5,7,5,5},M={5,7,7,5,5},O={7,5,5,5,7},V={5,5,5,5,2},
 R={6,5,6,5,5},L={4,4,4,4,7},I={7,2,2,2,7},W={5,5,7,7,5},["!"]={2,2,2,0,2}}
local function word(letters,x,y,scale,color)
  for _,letter in ipairs(letters) do
    for row,bits in ipairs(font[letter]) do for col=0,2 do
      if bits&(1<<(2-col))~=0 then box(x+col*scale,y+(row-1)*scale,scale,scale,color) end
    end end
    x=x+4*scale
  end
end
local function paint()
  rect(0,0,160,192,0x10a4)
  rect(LEFT,38,RIGHT-LEFT,150,0x18e5)
  rect(2,38,3,153,0x75b0); rect(155,38,3,153,0x75b0); rect(2,188,156,3,0x75b0)
  for x=7,151,8 do rect(x,LINE,4,1,danger>0 and 0xf9c7 or 0x8c10) end
  for _,f in ipairs(fruits) do draw_fruit(f.x,f.y,f.level,false) end
  word({'N','E','X','T'},124,3,1,0xc679); draw_fruit(145,21,nextfruit,true)
  if not over then
    local x=clamp(aim,LEFT+radii[current],RIGHT-radii[current])
    for y=40,180,8 do box(x,y,1,2,0x39c8) end
    draw_fruit(x,24,current,false)
  end
  if danger>0 then box(5,36,150*danger/40,2,0xf9c7) end
  if cooldown>0 then box(5,3,24*(12-cooldown)/12,2,0x65e7) end
  if won then word({'M','E','L','O','N','!'},8,7,1,0x8fe8) end
  if over then
    box(12,72,136,38,0x8825); word({'G','A','M','E'},34,78,2,0xffff)
    word({'O','V','E','R'},34,94,2,0xffff)
  end
end
function update(buttons)
  if buttons&16~=0 then reset() end
  if not over then
    if buttons&1~=0 then aim=aim-5 end
    if buttons&2~=0 then aim=aim+5 end
    aim=clamp(aim,LEFT+radii[current],RIGHT-radii[current])
    cooldown=math.max(0,cooldown-1)
    if buttons&12~=0 then drop() end
    physics(); loss_check()
  end
  paint()
end
reset()
