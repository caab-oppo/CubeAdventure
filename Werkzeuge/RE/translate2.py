# Adressuebersetzer Cube.exe -> Server.exe (Stufe 2)
#  1) Signatur: maskierte Bytesignatur ab der Adresse, eindeutige Fundstelle im Server-.text
#  2) Ausbreitung: jedes gesicherte Paar (Client, Server) wird im Gleichschritt disassembliert.
#     Solange die Instruktionen (maskiert) gleich sind, entsprechen sich alle Instruktionsadressen,
#     alle Sprung-/Aufrufziele und alle absoluten Adressen (auch Daten). Neue Codepaare werden
#     wieder durchlaufen (Fixpunkt). Widerspruechliche Zuordnungen werden verworfen.
# Aufruf: translate2.py Cube.exe Server.exe addrs.txt out.txt [out.inc]
import capstone, struct, sys, re
from collections import defaultdict
def load(p):
    d=open(p,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]
    n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]; secs=[]
    for i in range(n):
        o=pe+24+opt+i*40
        secs.append((d[o:o+8].rstrip(b'\0').decode(),*struct.unpack_from('<IIII',d,o+8)))
    return d,secs
C,Cs=load(sys.argv[1]); S,Ss=load(sys.argv[2])
def sec(secs,name):
    for nm,vsz,vaddr,rs,raw in secs:
        if nm==name: return 0x400000+vaddr,0x400000+vaddr+max(vsz,rs)
def rd(d,secs,va,n):
    rva=va-0x400000
    for nm,vsz,vaddr,rs,raw in secs:
        if rs and vaddr<=rva<vaddr+min(vsz,rs): return d[raw+rva-vaddr:raw+rva-vaddr+n]
    return None
def text(d,secs):
    for nm,vsz,vaddr,rs,raw in secs:
        if nm=='.text': return d[raw:raw+rs],0x400000+vaddr
CT=sec(Cs,'.text'); ST=sec(Ss,'.text')
def img(secs):
    lo=0x400000; hi=max(0x400000+v+max(z,r) for n,z,v,r,_ in secs); return lo,hi
CI=img(Cs); SI=img(Ss)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True

def masked(ins, lo, hi):
    """Bytes + Liste der (offset, wert) absoluter/relativer Adressen."""
    b=bytearray(ins.bytes); refs=[]
    if (b[0] in (0xE8,0xE9) and len(b)==5) or (len(b)==6 and b[0]==0x0F and 0x80<=b[1]<=0x8F):
        t=(ins.address+len(b)+struct.unpack_from('<i',bytes(b),len(b)-4)[0])&0xFFFFFFFF
        refs.append(('rel',len(b)-4,t))
    elif len(b)==2 and (b[0]==0xEB or 0x70<=b[0]<=0x7F):
        t=ins.address+2+struct.unpack_from('<b',bytes(b),1)[0]; refs.append(('rel8',1,t))
    k=0
    while k<=len(b)-4:
        if any(r[1]<=k<r[1]+4 for r in refs if r[0]=='rel'): k+=1; continue
        v=struct.unpack_from('<I',bytes(b),k)[0]
        if lo<=v<hi: refs.append(('abs',k,v)); k+=4; continue
        k+=1
    m=bytearray(b)
    for kind,o,v in refs:
        w=4 if kind!='rel8' else 1
        for q in range(o,o+w): m[q]=0
    return bytes(m),refs

def sig(va,nbytes):
    code=rd(C,Cs,va,nbytes+16)
    if not code: return None
    pat=b''; used=0
    for ins in md.disasm(code,va):
        mb,refs=masked(ins,*CI); b=ins.bytes
        hole=set()
        for kind,o,v in refs:
            if kind=='rel8': continue
            hole.update(range(o,o+4))
        pat+=b''.join((b'.' if k in hole else re.escape(bytes([b[k]]))) for k in range(len(b)))
        used+=len(b)
        if used>=nbytes: break
    return pat
St,Sbase=text(S,Ss)
targets=[int(l,16) for l in open(sys.argv[3]) if l.strip()]

# ---- Stufe 1: Signaturen ---------------------------------------------------
pairs={}; how={}
for a in targets:
    if not (CT[0]<=a<CT[1]): continue
    for n in (32,48,64,96,128):
        p=sig(a,n)
        if p is None: break
        ms=[m.start() for m in re.finditer(p,St,re.S)]
        if len(ms)==1: pairs[a]=Sbase+ms[0]; how[a]='signatur'; break
        if not ms: break
print('Stufe 1 (Signatur):',len(pairs))

# ---- Stufe 2: Ausbreitung im Gleichschritt ----------------------------------
obs=defaultdict(set)          # client -> {server}
for a,s in pairs.items(): obs[a].add(s)
import difflib, os
def dis(d,secs,va,limit,lo,hi):
    code=rd(d,secs,va,limit)
    if not code: return []
    return [(i.address,)+masked(i,lo,hi) for i in md.disasm(code,va)]
MINBLOCK=12
RESYNC=os.environ.get('TR_RESYNC')=='1'   # Neusynchronisierung nach Abweichung: erzeugte im Test falsche Paare -> aus
def emit(X,Y,blocks,out):
    for i,j,n in blocks:
        if n==0 or (n<MINBLOCK and not (i==0 and j==0)): continue
        for k in range(n):
            x=X[i+k]; y=Y[j+k]
            if len(x[2])!=len(y[2]): break
            out.append((x[0],y[0],'ins'))
            for (k1,o1,v1),(k2,o2,v2) in zip(x[2],y[2]):
                if k1!=k2 or o1!=o2: break
                out.append((v1,v2,'code' if (k1!='abs' or CT[0]<=v1<CT[1]) else 'data'))
def walk(ca,sa,limit=0x800):
    X=dis(C,Cs,ca,limit,*CI); Y=dis(S,Ss,sa,limit,*SI)
    if not X or not Y: return []
    out=[]; n=0
    while n<len(X) and n<len(Y) and X[n][1]==Y[n][1]:          # Gleichschritt bis Abweichung oder Funktionsende
        n+=1
        if X[n-1][1][:1] in (b'\xc3',b'\xc2',b'\xcc'): break      # nach ret nicht in die Nachbarfunktion laufen
    if not RESYNC or n>=len(X) or n>=len(Y) or X[n][1]==b'\xcc' or n>150:
        emit(X,Y,[(0,0,n)],out); return out
    sm=difflib.SequenceMatcher(None,[x[1] for x in X[:400]],[y[1] for y in Y[:400]],autojunk=False)
    emit(X,Y,sm.get_matching_blocks(),out)
    return out
VCACHE={}
def verify(c,s_,need=24):
    # neues Codepaar nur annehmen, wenn die ersten Instruktionen (maskiert) gleich sind
    k=(c,s_)
    if k in VCACHE: return VCACHE[k]
    X=dis(C,Cs,c,160,*CI); Y=dis(S,Ss,s_,160,*SI); ok=True; n=0
    for x,y in zip(X,Y):
        if x[1]!=y[1] or len(x[2])!=len(y[2]): ok=False; break
        n+=1
        if n>=need or x[1][:1] in (b'\xc3',b'\xc2',b'\xe9',b'\xeb'): break
    if n==0: ok=False
    VCACHE[k]=ok; return ok
import pickle,os,time
ST_F=os.environ.get('TR_STATE','tr_state.pkl'); T0=time.time(); LIM=float(os.environ.get('TR_SECS','1e9'))   # TR_SECS: Zeitlimit, dann Zustand sichern (TR_RESUME=1 setzt fort)
done=set(); work=list(pairs.items()); rounds=0
if os.environ.get('TR_RESUME')=='1' and os.path.exists(ST_F):
    obs,done,work=pickle.load(open(ST_F,'rb')); print('fortgesetzt:',len(done),'erledigt,',len(work),'offen')
while work and rounds<400000:
    if time.time()-T0>LIM:
        pickle.dump((obs,done,work),open(ST_F,'wb')); print('UNTERBROCHEN, offen:',len(work)); sys.exit(3)
    ca,sa=work.pop(); rounds+=1
    if (ca,sa) in done: continue
    done.add((ca,sa))
    for c,s,kind in walk(ca,sa):
        if s in obs[c]: continue
        if kind=='code' and CT[0]<=c<CT[1] and not verify(c,s): continue
        obs[c].add(s)
        if kind=='code' and len(obs[c])==1 and CT[0]<=c<CT[1] and ST[0]<=s<ST[1]:
            work.append((c,s))
print('Durchlaeufe:',rounds,' bekannte Client-Adressen:',len(obs))
# Eindeutigkeit auch rueckwaerts: eine Server-Adresse darf nur EINER Client-Adresse entsprechen
back=defaultdict(set)
for c,o in obs.items():
    if len(o)==1: back[next(iter(o))].add(c)
res={}
for a in targets:
    o=obs.get(a,set())
    if len(o)==1:
        s=next(iter(o))
        if len(back[s])>1: res[a]=(0,'widerspruch rueckwaerts '+','.join('%X'%v for v in sorted(back[s])))
        else: res[a]=(s, how.get(a,'ausbreitung'))
    elif len(o)>1: res[a]=(0,'widerspruch '+','.join('%X'%v for v in sorted(o)))
    else: res[a]=(0,'fehlt (client-only?)')
ok=sum(1 for v in res.values() if v[0]); print(f"{ok} von {len(res)} Adressen im Server gefunden")
with open(sys.argv[4],'w') as f:
    for a,(s,st) in sorted(res.items()):
        f.write(f"{{ 0x{a:06X}, 0x{s:06X} }},   // {st}\n")
if len(sys.argv)>5:
    with open(sys.argv[5],'w') as f:
        f.write("// Automatisch erzeugt von Werkzeuge/RE/translate2.py -- NICHT von Hand aendern.\n")
        f.write("// Cube.exe (Client) -> Server.exe: Signatur + Gleichschritt-Ausbreitung.\n")
        f.write("// 0 = im Server nicht vorhanden (Client-Funktion: Grafik, UI, Chat ...).\n")
        for a,(s,st) in sorted(res.items()):
            f.write(f"{{ 0x{a:06X}u, 0x{s:06X}u }},   // {st}\n")
