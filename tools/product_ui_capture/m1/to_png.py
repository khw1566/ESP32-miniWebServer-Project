from pathlib import Path
import sys
from PIL import Image
for path in Path(sys.argv[1]).glob('*.rgb565'):
 data=path.read_bytes()
 assert len(data)==240*320*2
 pixels=[]
 for i in range(0,len(data),2):
  value=data[i]|data[i+1]<<8
  pixels.append((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
 image=Image.new('RGB',(240,320));image.putdata(pixels);image.save(path.with_suffix('.png'))
