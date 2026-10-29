# Neue Elfen (groesser, dunkler) und untote Zwerge -- je 5 Varianten weiblich/maennlich.
# Nur UMFAERBEN: die Form (welche Voxel gefuellt sind) bleibt bei jedem Kopf exakt gleich.
# Verdeckte Fuellvoxel (Tuerkis/Lila/Gruen im Inneren) bleiben unveraendert.
import struct, numpy as np, os, sys, hashlib
SRC='/tmp/mdl'
def load(n):
    b=open(os.path.join(SRC,n+'.cub'),'rb').read(); sx,sy,sz=struct.unpack_from('<3i',b,0)
    return np.frombuffer(b,np.uint8,offset=12).reshape(sz,sy,sx,3).copy()
def save(a,path):
    sz,sy,sx,_=a.shape; os.makedirs(os.path.dirname(path),exist_ok=True)
    open(path,'wb').write(struct.pack('<3i',sx,sy,sz)+a.astype(np.uint8).tobytes())
def H(*k):
    return int(hashlib.md5(repr(k).encode()).hexdigest()[:8],16)/0xFFFFFFFF
def exposed(occ):
    sz,sy,sx=occ.shape; p=np.zeros((sz+2,sy+2,sx+2),bool); p[1:-1,1:-1,1:-1]=occ
    return occ & ~(p[2:,1:-1,1:-1]&p[:-2,1:-1,1:-1]&p[1:-1,2:,1:-1]&p[1:-1,:-2,1:-1]&p[1:-1,1:-1,2:]&p[1:-1,1:-1,:-2])
def skin(c):
    r,g,b=[int(v) for v in c]
    return r>=180 and r>=g and g>=b-15 and r-b>=15 and min(r,g,b)<225
def clamp(c): return tuple(int(max(1,min(255,round(v)))) for v in c)
def vary(c,h,amp): return clamp([v+(h-0.5)*2*amp for v in c])
def front_map(occ):
    sz,sy,sx=occ.shape; F={}
    for z in range(sz):
        for x in range(sx):
            ys=np.nonzero(occ[z,:,x])[0]
            if len(ys): F[(x,z)]=int(ys.max())
    return F

# ---------------------------------------------------------------- Elfen
ELF_TONES=[(0.86,0.79,0.74),(0.77,0.66,0.58),(0.74,0.69,0.73),(0.80,0.75,0.60),(0.66,0.55,0.49)]
ELF_TONE_N=['honig','bronze','daemmerung','oliv','tiefbraun']
ELF_EYES=[((30,190,110),(10,110,60)),((170,90,230),(90,40,150)),((240,170,40),(160,90,10)),((150,200,240),(70,110,170)),((230,215,120),(150,120,40))]
ROBES_F=[((52,96,58),(176,150,80)),((40,52,96),(180,180,200)),((96,30,48),(210,170,90)),((84,96,80),(160,150,110)),((24,86,90),(210,200,150))]
ROBES_M=[((46,70,40),(150,120,60)),((34,40,70),(160,160,180)),((70,40,30),(190,140,70)),((60,64,60),(140,130,100)),((20,60,70),(180,170,120))]
MARK=[None,None,(200,210,220),None,(40,140,130)]

def elf_head(name,v,tag):
    a=load(name); occ=a.any(3); ex=exposed(occ); F=front_map(occ)
    sz,sy,sx=occ.shape; tone=ELF_TONES[v]; eye=ELF_EYES[v]
    fy=max(F.values())                                   # Gesichtsebene (vorderste y)
    for z,y,x in zip(*np.nonzero(occ)):
        c=a[z,y,x]
        if not ex[z,y,x]: continue
        if skin(c):
            a[z,y,x]=clamp([c[0]*tone[0],c[1]*tone[1],c[2]*tone[2]])
        elif y==fy and z<=sz-1:
            r,g,b=[int(t) for t in c]
            if max(r,g,b)-min(r,g,b)>=80 and not skin(c) and max(r,g,b)>60:   # Iris (gesaettigt)
                br=(r+g+b)/3
                a[z,y,x]=eye[0] if br>=110 else eye[1]
    # Zeichnung auf der Stirn (Varianten 3/5): zwei Voxel mittig ueber den Augen
    if MARK[v] is not None:
        xs=sorted(x for (x,z),y in F.items() if y==fy)
        cx=(min(xs)+max(xs))/2.0
        eyez=[z for (x,z),y in F.items() if y==fy and not skin(a[z,y,x])]
        top=max(eyez)+2 if eyez else sz//2
        for x in (int(cx-0.5),int(cx+0.5)):
            for z in (top,top+1):
                if (x,z) in F and F[(x,z)]==fy and skin(load(name)[z,fy,x]): a[z,fy,x]=MARK[v]
    return a
def elf_body(v,male):
    a=load('body1_lang'); occ=a.any(3); ex=exposed(occ)
    robe,trim=(ROBES_M if male else ROBES_F)[v]
    for z,y,x in zip(*np.nonzero(ex)):
        c=tuple(int(t) for t in a[z,y,x])
        if c==(199,203,64): a[z,y,x]=vary(robe,H('r',v,male,x,y,z),5)
        elif c==(186,134,10): a[z,y,x]=trim
    return a

# ---------------------------------------------------------------- Untote Zwerge
ROT=[(150,168,138),(158,156,150),(132,150,160),(160,140,112),(128,152,120)]       # Haut: gruengrau, aschgrau, blaeulich, mumienbraun, moosgruen
ROT_N=['gruengrau','asche','frost','mumie','moder']
BONE=[(232,226,204),(214,212,200),(226,230,236),(222,206,166),(236,232,214)]
GLOW=[((150,255,170),(60,220,110)),((150,220,255),(40,150,255)),((255,150,130),(230,40,30)),((230,170,255),(160,60,240)),((255,240,140),(240,200,30))]
SOCKET=(42,34,40); CAVITY=(58,44,46)
def undead_head(name,v):
    a=load(name); orig=a.copy(); occ=a.any(3); ex=exposed(occ); F=front_map(occ)
    sz,sy,sx=occ.shape; rot=ROT[v]; bone=BONE[v]; glow=GLOW[v]
    fy=max(F.values())
    xs=[x for (x,z) in F]; cx=(min(xs)+max(xs))/2.0
    # Augen: gesaettigte/dunkle/weisse Voxel der Gesichtsfront in der oberen Gesichtshaelfte
    def eyeish(c):
        r,g,b=[int(t) for t in c]
        return (b>r and b>g) or (g>r and g>b) or max(r,g,b)<70 or min(r,g,b)>225
    cand=[(x,z) for (x,z),y in F.items() if y>=fy-2 and eyeish(orig[z,y,x])]
    iz=[z for (x,z) in cand if (lambda c:(c[2]>c[0] and c[2]>c[1]) or (c[1]>c[0] and c[1]>c[2]))([int(t) for t in orig[z,F[(x,z)],x]])]
    def iris(c):
        r,g,b=[int(t) for t in c]; return (b>r and b>g) or (g>r and g>b)
    irisv=[(x,z) for (x,z),y in F.items() if y>=fy-2 and iris(orig[z,y,x])]
    if irisv:
        zc=max(z for x,z in irisv); irisv=[(x,z) for x,z in irisv if abs(z-zc)<=1]
    # Augen = Iris + angrenzende Augenvoxel (weiss/dunkel) direkt daneben
    eyes=set(irisv)
    for (x,z) in list(irisv):
        for dx in (-1,0,1):
            for dz in (-1,0,1):
                q=(x+dx,z+dz)
                if q in F and F[q]>=fy-2 and eyeish(orig[q[1],F[q],q[0]]): eyes.add(q)
    eyes=sorted(eyes)
    ez0=min(z for x,z in eyes) if eyes else sz//2; ez1=max(z for x,z in eyes) if eyes else sz//2
    L=[x for x,z in eyes if x<cx]; R=[x for x,z in eyes if x>=cx]
    boxes=[]
    if L: boxes.append((min(L),max(L)))
    if R: boxes.append((min(R),max(R)))
    def in_socket(x,z): return any(b0-1<=x<=b1+1 for b0,b1 in boxes) and ez0-1<=z<=ez1+1
    # 1) Haut -> Verwesung, gefleckt
    for z,y,x in zip(*np.nonzero(ex)):
        c=orig[z,y,x]
        if skin(c):
            h=H('s',name,v,x,y,z)
            col=vary(rot,h,14)
            if h>0.86: col=clamp([t*0.78 for t in rot])        # dunkle Flecken
            a[z,y,x]=col
    # 2) Schaedel oben: Knochenplatten (oberste Voxel, Rauschen)
    for (x,z),y in list(F.items()): pass
    for x in range(sx):
        for y in range(sy):
            zs=np.nonzero(occ[:,y,x])[0]
            if not len(zs): continue
            z=int(zs.max())
            if skin(orig[z,y,x]) and H('k',name,v,x//2,y//2)>0.55: a[z,y,x]=vary(bone,H('kb',x,y),8)
    # 3) Gesicht: Augenhoehlen, glimmende Augen, Brauenbogen, Jochbeine, Nasenloch, Zaehne
    for (x,z),y in F.items():
        c=orig[z,y,x]
        if (x,z) in eyes:                                   # nur die Iris glimmt, Weiss/Rand wird Hoehle
            r,g,b=[int(t) for t in c]
            if iris(c): a[z,y,x]=glow[0] if (r+g+b)/3>=110 else glow[1]
            else: a[z,y,x]=SOCKET
            continue
        if not ex[z,y,x]: continue
        if in_socket(x,z) and skin(c): a[z,y,x]=SOCKET; continue
        rr,gg,bb=[int(t) for t in c]
        if ez1+1<=z<=ez1+2 and rr<130 and rr>gg>=bb and y>=fy-3: a[z,y,x]=vary(bone,H('bw',x,z),6); continue   # Brauen -> Brauenbogen
        if z==ez1+2 and boxes and min(b[0] for b in boxes)-1<=x<=max(b[1] for b in boxes)+1 and skin(c): a[z,y,x]=vary(bone,H('br',x),6); continue
        if z==ez0-2 and any(b0-1<=x<=b1 for b0,b1 in boxes) and abs(x-cx)>1.5 and skin(c): a[z,y,x]=vary(bone,H('cb',x),6); continue
        if abs(x-cx)<=1.0 and ez0-3<=z<=ez0-1 and y>=fy-1 and not eyeish(c): a[z,y,x]=CAVITY; continue
        r,g,b=[int(t) for t in c]
        if abs(x-cx)<=3 and z<=ez0-3 and y>=fy-2 and (r>190 and g<140 or min(r,g,b)>225 or max(r,g,b)<70):   # Mund/Lippen -> Zahnreihe
            a[z,y,x]=bone if (x+z)%2==0 else SOCKET
    # 4) Seiten: Ohren bleiben Haut (verwest); ein paar Risse
    return a
def undead_body(name,v,female):
    a=load(name); orig=a.copy(); occ=a.any(3); ex=exposed(occ)
    sz,sy,sx=occ.shape; rot=ROT[v]; bone=BONE[v]
    ys=np.nonzero(occ)[1]; ymax=int(ys.max()); ymin=int(ys.min()); cx=(sx-1)/2.0
    rag=[(84,70,58),(70,68,66),(60,66,74),(96,78,52),(66,74,58)][v]
    for z,y,x in zip(*np.nonzero(ex)):
        c=tuple(int(t) for t in orig[z,y,x])
        if skin(c):
            col=vary(rot,H('b',name,v,x,y,z),12)
            top = z==sz-1 or not occ[min(z+1,sz-1),y,x] and z==sz-2
            front = y>=ymax-1 and not top; back = y<=ymin+1 and not top; side = (x==0 or x==sx-1) and not top
            if front and abs(x-cx)<=0.6 and 4<=z<=sz-2: col=vary(bone,H('st',z),6)             # Brustbein
            elif (front or side) and z in (sz-4,sz-6) and abs(x-cx)>0.6: col=vary(bone,H('ri',x,z),6)   # zwei Rippenboegen
            elif front and z<sz-6: col=clamp([t*0.72 for t in rot])                          # eingefallener Bauch
            if back and abs(x-cx)<=0.6 and z%2==0: col=vary(bone,H('sp',z),6)                 # Wirbelsaeule
            if top and y>=ymax-2 and abs(x-cx)>1.5: col=vary(bone,H('cl',x),6)                # Schluesselbeine
            a[z,y,x]=col
        elif c in [(101,66,34),(211,123,21),(20,11,3),(36,21,7),(31,36,25)]:      # Kleidung -> Grablumpen
            h=H('c',name,v,x,y,z)
            base=rag if c not in [(20,11,3),(36,21,7)] else (30,24,20)
            col=vary(base,h,10)
            if h>0.8: col=clamp([t*0.6 for t in base])                            # Loecher/Fetzen dunkel
            a[z,y,x]=col
    return a

OUT=sys.argv[1]
made=[]
def put(a,rel):
    save(a,os.path.join(OUT,rel)); made.append(rel)
EF=['elf-head-f01','elf-head-f02','elf-head-f01','elf-head-f02','elf-head-f01']
EM=['elf-head-m02','elf-head-m03','elf-head-m04','elf-head-m02','elf-head-m03']
DF=['dwarf-head-f03','dwarf-head-f04','dwarf-head-f03','dwarf-head-f04','dwarf-head-f03']
DM=['dwarf-head-m04','dwarf-head-m05','dwarf-head-m04','dwarf-head-m05','dwarf-head-m04']
for v in range(5):
    put(elf_head(EF[v],v,'f'),f'Elfen/elf-w{v+1}-kopf.cub'); put(elf_body(v,False),f'Elfen/elf-w{v+1}-koerper.cub')
    put(elf_head(EM[v],v,'m'),f'Elfen/elf-m{v+1}-kopf.cub'); put(elf_body(v,True),f'Elfen/elf-m{v+1}-koerper.cub')
    put(undead_head(DF[v],v),f'Zwerge/zwerg-untot-w{v+1}-kopf.cub'); put(undead_body('dwarf-body-female',v,True),f'Zwerge/zwerg-untot-w{v+1}-koerper.cub')
    put(undead_head(DM[v],v),f'Zwerge/zwerg-untot-m{v+1}-kopf.cub'); put(undead_body('dwarf-body',v,False),f'Zwerge/zwerg-untot-m{v+1}-koerper.cub')
# Formpruefung: Kopf-Belegung identisch mit dem Original
src={**{f'Elfen/elf-w{v+1}-kopf.cub':EF[v] for v in range(5)},**{f'Elfen/elf-m{v+1}-kopf.cub':EM[v] for v in range(5)},
     **{f'Zwerge/zwerg-untot-w{v+1}-kopf.cub':DF[v] for v in range(5)},**{f'Zwerge/zwerg-untot-m{v+1}-kopf.cub':DM[v] for v in range(5)}}
for rel,n in src.items():
    b=open(os.path.join(OUT,rel),'rb').read(); sx,sy,sz=struct.unpack_from('<3i',b)
    x=np.frombuffer(b,np.uint8,offset=12).reshape(sz,sy,sx,3).any(3); o=load(n).any(3)
    assert x.shape==o.shape and (x==o).all(), rel
print(len(made),'Modelle, Kopfformen unveraendert')
