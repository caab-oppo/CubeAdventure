# tierbau.py -- Bausteine aus tiere_neu.py (2026-10-13) fuer weitere Tier-Generatoren.
# Tiere neu (2026-10-13, Stand 2): ALLE Teile von Grund auf aus Grundformen gebaut (keine nativen Modelle mehr).
# Von der Basisrasse kommen nur Skelett/Bewegung (Drehpunkte): jedes Teil hat GENAU die Masse des nativen Teils
# derselben Basisrasse, damit es an der richtigen Stelle sitzt. Jedes Tier bekommt eine EIGENE Rasse
# (eigene_rasse = 200 + n, rassen_name = Anzeigename), die der Mod beim Spawnen setzt und im Spiel benennt.
# Aufruf (aus Projekt\\Werkzeuge\\Mobs):  python tiere_neu.py [Strukturen-Ordner] [Vorschau.png]
import sys, os, re, struct, hashlib, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubdb

_HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(_HERE, '..', '..', '..', 'Strukturen'))   # tierwelt.py kann OUT setzen
SRC_CPP = os.path.normpath(os.path.join(_HERE, '..', '..', 'AdventureMod', 'MobLab.cpp'))
names = re.findall(r'"([A-Za-z0-9() ]+)"', open(SRC_CPP, encoding='latin-1').read().split('kRace[')[1].split('};')[0])
RACE = {n: i for i, n in enumerate(names)}

def dims(n):
    r = cubdb.get(n + '.cub'); assert r, n
    return r[0], r[1], r[2]
def H(*k): return int(hashlib.md5(repr(k).encode()).hexdigest()[:8], 16) / 0xFFFFFFFF
def safe(c):
    c = [int(max(2, min(253, round(v)))) for v in c]
    if all(v in (0, 127, 255) for v in c): c[1] += 1
    return c

class V:
    """Voxelfeld a[z, y, x]; vorne = +y (Kopf: Gesicht bei y max), oben = +z."""
    def __init__(s, sx, sy, sz, tag):
        s.sx, s.sy, s.sz, s.tag = sx, sy, sz, tag
        s.a = np.zeros((sz, sy, sx, 3), np.uint8); s.o = np.zeros((sz, sy, sx), bool)
    def put(s, x, y, z, c, noise=7, over=True):
        x, y, z = int(round(x)), int(round(y)), int(round(z))
        if not (0 <= x < s.sx and 0 <= y < s.sy and 0 <= z < s.sz): return
        if s.o[z, y, x] and not over: return
        n = (H(s.tag, x, y, z) - 0.5) * 2 * noise
        sh = 0.88 + 0.24 * (z / max(1, s.sz - 1))                    # oben heller
        s.a[z, y, x] = safe([v * sh + n for v in c]); s.o[z, y, x] = True
    def ell(s, cx, cy, cz, rx, ry, rz, c, noise=7, over=True, cut=None):
        for z in range(int(cz - rz) - 1, int(cz + rz) + 2):
            for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
                for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                    d = ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 + ((z + .5 - cz) / rz) ** 2
                    if d <= 1 and (cut is None or cut(x, y, z)): s.put(x, y, z, c(x, y, z) if callable(c) else c, noise, over)
    def box(s, x0, y0, z0, x1, y1, z1, c, noise=7, over=True):
        for z in range(int(z0), int(z1) + 1):
            for y in range(int(y0), int(y1) + 1):
                for x in range(int(x0), int(x1) + 1): s.put(x, y, z, c(x, y, z) if callable(c) else c, noise, over)
    def cap(s, p0, p1, r0, r1, c, noise=6, over=True):
        L = math.dist(p0, p1); n = max(2, int(L * 3))
        for i in range(n + 1):
            t = i / n; p = [p0[k] + (p1[k] - p0[k]) * t for k in range(3)]; r = r0 + (r1 - r0) * t
            s.ell(p[0], p[1], p[2], r, r, r, c, noise, over)
    def clear(s, x, y, z):
        if 0 <= x < s.sx and 0 <= y < s.sy and 0 <= z < s.sz: s.o[z, y, x] = False; s.a[z, y, x] = 0
    def eye(s, x, y, z, iris=(30, 24, 20), big=False):
        s.put(x, y, z, iris, 0); s.put(x, y, z + 1, (236, 236, 228), 0) if big else None
    def mirror(s):                                                    # links = rechts (Symmetrie)
        for x in range(s.sx // 2):
            m = s.sx - 1 - x
            s.a[:, :, m] = np.where(s.o[:, :, x, None], s.a[:, :, x], s.a[:, :, m]); s.o[:, :, m] |= s.o[:, :, x]
    def save(s, rel):
        p = os.path.join(OUT, 'Mobs', rel); os.makedirs(os.path.dirname(p), exist_ok=True)
        a = s.a.copy(); a[~s.o] = 0
        open(p, 'wb').write(struct.pack('<3i', s.sx, s.sy, s.sz) + a.astype(np.uint8).tobytes())

HEAD, HAIR, HANDS, FEET, BODY, TAIL, SHOULDER, WINGS = range(8)
SLOTN = ['kopf', 'haare', 'haende', 'fuesse', 'koerper', 'schwanz', 'schulter', 'fluegel']
MOBS = []; PREV = []
CFG = {'race_base': 200, 'mob_dir': 'Tiere', 'part_dir': 'Tiere'}
def mob(name, disp, base, factor, parts, host=1, note='', extra=()):
    rid = CFG['race_base'] + len(MOBS)
    L = ["# Eigener Mob (LAB). teil=Nr;Quelle;Index;Datei;Skala;Pos(0/1);x;y;z",
         "# Quelle: 0 = von der Basisrasse, 1 = keins, 2 = nativer Registry-Index, 3 = eigene .cub",
         f"# {note}" if note else "#", f"name={name}", f"basis={RACE[base]}", "groesse=0", f"faktor={factor}",
         f"feindlichkeit={1 if host in (2, 3, 4) else host}"] + (["# friedlich wie die nativen Tiere (Huhn): Feindlichkeit 1 + Aussehensflag 0x10", "friedlich=1"] if host in (2, 3, 4) else []) + ["level=-1", "ausruestung=0", "wandern=1",
         "# eigene Rasse: Kennung (>= 200) und Name im Spiel; Skelett/Bewegung von der Basisrasse",
         f"eigene_rasse={rid}", f"rassen_name={disp}", "rasse_setzen=1"] + list(extra)
    # Vierbeiner ohne eigenes Handmodell: das Spiel setzt die VORDERBEINE in den Handplatz mit demselben Fussmodell
    # (Aussehensgenerator 0x43F7C0: Pferd/Kuh/Mammut/Schwein/Kamel/Schaf/Schildkroete/Krokodil: item+0x18 = Fuss).
    if base in ('Horse', 'Cow', 'Mammoth', 'Pig', 'Camel', 'Sheep', 'Turtle', 'Crocodile') and isinstance(parts.get(FEET), V) and HANDS not in parts:
        parts = dict(parts); parts[HANDS] = parts[FEET]
    for i in range(8):
        p = parts.get(i)
        if p is None: L.append(f"teil={i};1;-1;;0;0;0;0;0")
        elif p == 'base': L.append(f"teil={i};0;-1;;0;0;0;0;0")
        else:
            rel = f"{name}-{SLOTN[i]}.cub"; p.save(os.path.join('Teile', CFG['part_dir'], rel))
            L.append(f"teil={i};3;-1;..\\Teile\\{CFG['part_dir']}\\{rel};0;0;0;0;0")
            PREV.append((name, SLOTN[i], p))
    pth = os.path.join(OUT, 'Mobs', CFG['mob_dir'], name + '.mob'); os.makedirs(os.path.dirname(pth), exist_ok=True)
    open(pth, 'w', newline='\n').write('\n'.join(L) + '\n')
    MOBS.append((name, disp, base, rid, note))
    return rid

def mix(c1, c2, t): return [c1[k] + (c2[k] - c1[k]) * t for k in range(3)]

# ======================================================================== Vierbeiner-Bausteine
def quad_head(base_dims, tag, fur, belly, snout_len, snout_c=None, ear='point', ear_c=None, nose=(30, 26, 26), eye_c=(28, 22, 18), ear_len=3):
    sx, sy, sz = base_dims; v = V(sx, sy, sz, tag)
    cx = sx / 2; cz = sz * 0.45; cy = sy * 0.45
    rx = sx * 0.40; ry = sy * 0.33; rz = sz * 0.38
    v.ell(cx, cy, cz, rx, ry, rz, lambda x, y, z: belly if (z < cz - rz * 0.3 and y > cy) else fur)
    sc = snout_c or belly
    sl = snout_len
    v.ell(cx, min(sy - sl * 0.6 - 0.5, cy + ry * 0.75 + sl * 0.3), cz - rz * 0.35, rx * 0.55, sl * 0.7 + 1, rz * 0.48, lambda x, y, z: sc if z < cz - rz * 0.25 else mix(fur, sc, 0.4))
    ny = int(min(sy - 1, cy + ry * 0.75 + sl * 0.95)); nz = int(cz - rz * 0.15)
    for dx in (0, 1): v.put(int(cx) - 1 + dx, ny, nz, nose, 3)
    ey = int(cy + ry * 0.55); ez = int(cz + rz * 0.25)
    for sgn in (-1, 1):
        ex = int(cx + sgn * rx * 0.55) - (1 if sgn < 0 else 0)
        v.put(ex, ey + 1, ez, eye_c, 0); v.put(ex, ey + 1, ez + 1, (238, 238, 228), 0) if False else None
        ec = ear_c or fur
        bx = cx + sgn * rx * 0.55; by = cy - ry * 0.15
        if ear == 'point':
            for k in range(ear_len):
                w = max(0, 1 - k // 2)
                v.box(bx - w - 0.5, by - w, cz + rz * 0.7 + k, bx + w - 0.5, by + w, cz + rz * 0.7 + k, ec if k < ear_len - 1 else mix(ec, (30, 26, 24), .6), 5)
        elif ear == 'round':
            v.ell(bx, by, cz + rz * 0.85, 1.6, 1.0, 1.4, ec)
        elif ear == 'tuft':
            for k in range(ear_len): v.box(bx - 0.5, by, cz + rz * 0.7 + k, bx + 0.5, by, cz + rz * 0.7 + k, ec, 5)
            v.put(bx, by, cz + rz * 0.7 + ear_len, (30, 26, 24), 0); v.put(bx, by, cz + rz * 0.7 + ear_len + 1, (30, 26, 24), 0)
        elif ear == 'big':
            v.ell(bx + sgn * 2.5, by, cz + rz * 0.2, 1.0, 3.5, 4.5, ec)
    return v
def quad_body(d, tag, top, under, spots=None, spot_c=None, stripes=None):
    sx, sy, sz = d; v = V(sx, sy, sz, tag)
    cx, cy, cz = sx / 2, sy / 2, sz / 2
    def col(x, y, z):
        c = under if z < cz - sz * 0.22 else top
        if spots and H(tag, 's', x // 2, y // 2, z // 2) < spots and z > cz - 1: c = spot_c
        if stripes and (y // 2) % 3 == 0 and z > cz: c = spot_c
        return c
    v.ell(cx, cy, cz, sx * 0.47, sy * 0.48, sz * 0.48, col)
    return v
def quad_leg(d, tag, fur, paw, long_=False):
    sx, sy, sz = d; v = V(sx, sy, sz, tag)
    cx, cy = sx / 2, sy / 2
    v.cap((cx, cy, sz - 2), (cx, cy + 0.3, 2), sx * 0.30, sx * 0.24, fur)
    v.ell(cx, cy + 0.6, 1.2, sx * 0.30, sy * 0.40, 1.4, paw)
    return v
def tail(d, tag, c, tip=None, bushy=False):
    sx, sy, sz = d; v = V(sx, sy, sz, tag)
    cx, cy = sx / 2, sy / 2
    n = sz
    for z in range(n):
        r = (sx * 0.45 if bushy else sx * 0.28) * (0.7 + 0.5 * math.sin(math.pi * z / n))
        cc = tip if (tip and z < n * 0.3) else c
        v.ell(cx, cy - (n - z) * 0.08, z + 0.5, r, r, 0.7, cc)
    return v

