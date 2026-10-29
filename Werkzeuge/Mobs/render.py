import sys, struct, numpy as np
from PIL import Image, ImageDraw
def load(f):
    b=open(f,'rb').read(); sx,sy,sz=struct.unpack_from('<3i',b,0)
    return np.frombuffer(b,dtype=np.uint8,offset=12).reshape(sz,sy,sx,3)
def render(a, out, s=4, flip=False):
    sz,sy,sx,_=a.shape
    if flip: a=a[:, ::-1, ::-1, :]
    solid=(a.sum(axis=3)>0)
    pad=np.zeros((sz+2,sy+2,sx+2),bool); pad[1:-1,1:-1,1:-1]=solid
    top=solid & ~pad[2:,1:-1,1:-1]; front=solid & ~pad[1:-1,2:,1:-1]; left=solid & ~pad[1:-1,1:-1,2:]
    vis=top|front|left
    zs,ys,xs=np.nonzero(vis)
    order=np.lexsort((zs, (xs+ys)))
    cx=0.866*s; W=int((sx+sy)*cx)+20; H=int(sz*s+(sx+sy)*s*0.5)+20
    img=Image.new('RGB',(W,H),(30,34,44)); d=ImageDraw.Draw(img)
    ox=sy*cx+10; oy=sz*s+10
    def P(x,y,z): return (ox+(x-y)*cx, oy-z*s+(x+y)*s*0.5)
    for i in order:
        x,y,z=int(xs[i]),int(ys[i]),int(zs[i]); c=tuple(int(v) for v in a[z,y,x])
        if top[z,y,x]: d.polygon([P(x,y,z+1),P(x+1,y,z+1),P(x+1,y+1,z+1),P(x,y+1,z+1)],fill=c)
        if front[z,y,x]: d.polygon([P(x,y+1,z),P(x+1,y+1,z),P(x+1,y+1,z+1),P(x,y+1,z+1)],fill=tuple(int(v*0.8) for v in c))
        if left[z,y,x]: d.polygon([P(x+1,y,z),P(x+1,y+1,z),P(x+1,y+1,z+1),P(x+1,y,z+1)],fill=tuple(int(v*0.62) for v in c))
    img.save(out)
if __name__=='__main__':
    render(load(sys.argv[1]), sys.argv[2], float(sys.argv[3]) if len(sys.argv)>3 else 4, len(sys.argv)>4)
