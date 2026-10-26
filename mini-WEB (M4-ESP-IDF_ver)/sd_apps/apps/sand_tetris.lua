-- SD-loaded Sand Tetris. No game rules are compiled into the firmware.
-- Host API: rect(x,y,w,h,RGB565), score(n), update(input_bits), SEED.
local W, H, CELL, TOP = 40, 44, 4, 16
local colors = {0xfbe4, 0x459f, 0xf26b}
local shapes = {
  {{0,1},{1,1},{2,1},{3,1}}, {{1,0},{2,0},{1,1},{2,1}},
  {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{0,1},{1,1}},
  {{0,0},{1,0},{1,1},{2,1}}, {{0,0},{0,1},{1,1},{2,1}},
  {{2,0},{0,1},{1,1},{2,1}}
}
local grid, piece, px, py, color, points, over, ticks
math.randomseed(SEED or 12345)
local function key(x,y) return y*W+x+1 end
local function blocked(x,y)
  return x<0 or x>=W or y<0 or y>=H or grid[key(x,y)]~=0
end
local function fits(shape,x,y)
  for _,p in ipairs(shape) do
    for dy=0,2 do for dx=0,2 do
      if blocked(x+p[1]*3+dx,y+p[2]*3+dy) then return false end
    end end
  end
  return true
end
local function spawn()
  piece=shapes[math.random(#shapes)]; px=14; py=0; color=math.random(#colors)
  if not fits(piece,px,py) then over=true end
end
local function reset()
  grid={}; for i=1,W*H do grid[i]=0 end
  points=0; ticks=0; over=false; spawn(); score(points)
end
local function deposit()
  for _,p in ipairs(piece) do
    for dy=0,2 do for dx=0,2 do grid[key(px+p[1]*3+dx,py+p[2]*3+dy)]=color end end
  end
  spawn()
end
local function settle()
  -- Bottom-up traversal moves each grain at most once per pass.
  local reverse=ticks%2==0
  for y=H-2,0,-1 do for col=0,W-1 do
    local x=reverse and W-1-col or col
    local i=key(x,y); local c=grid[i]
    if c~=0 then
      local dx=0
      if grid[i+W]~=0 then
        local side=reverse and -1 or 1
        if x+side>=0 and x+side<W and grid[i+W+side]==0 then dx=side
        elseif x-side>=0 and x-side<W and grid[i+W-side]==0 then dx=-side
        else dx=nil end
      end
      if dx then
        local occupied=false
        if not over then
          for _,p in ipairs(piece) do
            local bx=px+p[1]*3; local by=py+p[2]*3
            if x+dx>=bx and x+dx<bx+3 and y+1>=by and y+1<by+3 then occupied=true; break end
          end
        end
        if not occupied then grid[i+W+dx]=c; grid[i]=0 end
      end
    end
  end end
end
local function clear_connections()
  local visited={}
  for y=0,H-1 do
    local start=key(0,y); local c=grid[start]
    if c~=0 and not visited[start] then
      local queue={start}; local head=1; local right=false; visited[start]=true
      while head<=#queue do
        local i=queue[head]; head=head+1
        local x=(i-1)%W; local row=(i-1)//W
        if x==W-1 then right=true end
        local neighbors={}
        if x>0 then neighbors[#neighbors+1]=i-1 end
        if x<W-1 then neighbors[#neighbors+1]=i+1 end
        if row>0 then neighbors[#neighbors+1]=i-W end
        if row<H-1 then neighbors[#neighbors+1]=i+W end
        for _,n in ipairs(neighbors) do
          if not visited[n] and grid[n]==c then visited[n]=true; queue[#queue+1]=n end
        end
      end
      if right then
        for _,i in ipairs(queue) do grid[i]=0 end
        points=points+#queue; score(points)
      end
    end
  end
end
local function paint()
  rect(0,0,160,192,0x0862)
  -- Three color swatches and a height bar; the UI shows the numeric score.
  for i,c in ipairs(colors) do rect((i-1)*54,2,50,8,c) end
  for y=0,H-1 do for x=0,W-1 do
    local c=grid[key(x,y)]
    if c~=0 then rect(x*CELL,TOP+y*CELL,CELL,CELL,colors[c]) end
  end end
  if not over then
    for _,p in ipairs(piece) do rect((px+p[1]*3)*CELL,TOP+(py+p[2]*3)*CELL,12,12,colors[color]) end
  else
    rect(8,68,144,40,0xa104); rect(16,84,128,8,0xffff)
  end
end
function update(buttons)
  if buttons&16~=0 then reset() end
  ticks=ticks+1
  if not over then
    settle(); settle()
    if ticks%10==0 then clear_connections() end
    if buttons&1~=0 and fits(piece,px-2,py) then px=px-2 end
    if buttons&2~=0 and fits(piece,px+2,py) then px=px+2 end
    if buttons&4~=0 then
      local rotated={}
      for i,p in ipairs(piece) do rotated[i]={3-p[2],p[1]} end
      for _,kick in ipairs({0,-3,3,-6,6}) do
        if fits(rotated,px+kick,py) then piece=rotated; px=px+kick; break end
      end
    end
    if buttons&8~=0 then
      while fits(piece,px,py+1) do py=py+1 end
      deposit()
    elseif fits(piece,px,py+1) then py=py+1
    else deposit() end
  end
  paint()
end
reset()
