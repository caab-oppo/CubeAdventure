import sys,os,struct,numpy as np
from PIL import Image, ImageDraw
def load(f):
    b=open(f,'rb').read(); sx,sy,sz=struct.unpack_from('<3i',b); return np.frombuffer(b,np.uint8,offset=12).reshape(sz,sy,sx,3)
def front(a,s=14,shade=True):
    sz,sy,sx,_=a.shape; occ=a.any(3)
    img=Image.new('RGB',(sx*s,sz*s),(30,34,44)); d=ImageDraw.Draw(img)
    ymax=max(np.nonzero(occ)[1])
    for z in range(sz):
        for x in range(sx):
            ys=np.nonzero(occ[z,:,x])[0]
            if not len(ys): continue
            y=ys.max(); c=a[z,y,x].astype(float)
            if shade: c=c*(0.65+0.35*(y-ys.min()+1)/(ymax+1)) if False else c*(0.75+0.25*y/ymax)
            d.rectangle([x*s,(sz-1-z)*s,x*s+s-1,(sz-1-z)*s+s-1],fill=tuple(int(v) for v in c))
    return img
def sheet(files,out,s=14,cols=5):
    ims=[front(load(f),s) for f in files]; w=max(i.width for i in ims)+10; h=max(i.height for i in ims)+18
    rows=(len(ims)+cols-1)//cols; o=Image.new('RGB',(w*cols,h*rows),(30,34,44)); d=ImageDraw.Draw(o)
    for k,(f,i) in enumerate(zip(files,ims)):
        x=(k%cols)*w; y=(k//cols)*h; o.paste(i,(x,y)); d.text((x,y+i.height+2),os.path.basename(f),fill=(220,220,220))
    o.save(out)
if __name__=='__main__':
    sheet(sys.argv[2:],sys.argv[1])
