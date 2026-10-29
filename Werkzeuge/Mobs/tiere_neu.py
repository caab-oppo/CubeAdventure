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
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(_HERE, '..', '..', '..', 'Strukturen'))
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
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
RACE_BASE = 200
def mob(name, disp, base, factor, parts, host=1, note=''):
    rid = RACE_BASE + len(MOBS)
    L = ["# Eigener Mob (LAB). teil=Nr;Quelle;Index;Datei;Skala;Pos(0/1);x;y;z",
         "# Quelle: 0 = von der Basisrasse, 1 = keins, 2 = nativer Registry-Index, 3 = eigene .cub",
         f"# {note}" if note else "#", f"name={name}", f"basis={RACE[base]}", "groesse=0", f"faktor={factor}",
         f"feindlichkeit={host}", "level=-1", "ausruestung=0", "wandern=1",
         "# eigene Rasse: Kennung (>= 200) und Name im Spiel; Skelett/Bewegung von der Basisrasse",
         f"eigene_rasse={rid}", f"rassen_name={disp}", "rasse_setzen=1"]
    # Vierbeiner ohne eigenes Handmodell: das Spiel setzt die VORDERBEINE in den Handplatz mit demselben Fussmodell
    # (Aussehensgenerator 0x43F7C0: Pferd/Kuh/Mammut/Schwein/Kamel/Schaf/Schildkroete/Krokodil: item+0x18 = Fuss).
    if base in ('Horse', 'Cow', 'Mammoth', 'Pig', 'Camel', 'Sheep', 'Turtle', 'Crocodile') and isinstance(parts.get(FEET), V) and HANDS not in parts:
        parts = dict(parts); parts[HANDS] = parts[FEET]
    for i in range(8):
        p = parts.get(i)
        if p is None: L.append(f"teil={i};1;-1;;0;0;0;0;0")             # nichts von der Basis: alles eigen
        elif p == 'base': L.append(f"teil={i};0;-1;;0;0;0;0;0")
        else:
            rel = f"{name}-{SLOTN[i]}.cub"; p.save(os.path.join('Teile', 'Tiere', rel))
            L.append(f"teil={i};3;-1;..\\Teile\\Tiere\\{rel};0;0;0;0;0")
            PREV.append((name, SLOTN[i], p))
    pth = os.path.join(OUT, 'Mobs', 'Tiere', name + '.mob'); os.makedirs(os.path.dirname(pth), exist_ok=True)
    open(pth, 'w', newline='\n').write('\n'.join(L) + '\n')
    MOBS.append((name, disp, base, rid, note))

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

# ======================================================================== Tiere
def nashorn():
    g, g2, horn = (118, 116, 112), (96, 94, 92), (214, 202, 176)
    hd = dims('cow-head'); h = V(*hd, 'nh')
    sx, sy, sz = hd; cx = sx / 2
    h.ell(cx, sy * 0.40, sz * 0.40, sx * 0.40, sy * 0.38, sz * 0.32, g)                  # Schaedel
    h.ell(cx, sy * 0.78, sz * 0.32, sx * 0.30, sy * 0.24, sz * 0.24, g2)                 # Maul
    h.cap((cx - .5, sy * 0.86, sz * 0.48), (cx - .5, sy * 0.96, sz * 0.86), 2.2, 0.6, horn, 4)   # grosses Horn
    h.cap((cx - .5, sy * 0.62, sz * 0.62), (cx - .5, sy * 0.66, sz * 0.78), 1.4, 0.5, horn, 4)  # kleines Horn
    for sgn in (-1, 1):
        h.put(cx + sgn * sx * 0.32 - .5, sy * 0.62, sz * 0.50, (24, 20, 18), 0)          # Augen
        h.ell(cx + sgn * sx * 0.30, sy * 0.22, sz * 0.74, 1.2, 0.8, 1.8, g)               # Ohren
    for dx in (-1, 0): h.put(cx + dx, sy - 1, sz * 0.30, (40, 36, 34), 0)
    b = quad_body(dims('cow-body'), 'nb', g, g2)
    for y in range(b.sy):                                                                   # Hautfalten
        if y % 4 == 0:
            for x in range(b.sx):
                for z in range(b.sz):
                    if b.o[z, y, x]: b.a[z, y, x] = safe([v * 0.85 for v in b.a[z, y, x]])
    f = quad_leg(dims('cow-foot'), 'nf', g, (80, 78, 76))
    mob('nashorn', 'Nashorn', 'Cow', 1.4, {HEAD: h, BODY: b, FEET: f}, 1, 'Basis Kuh (Skelett), alles eigen')

def elefant():
    g, g2, tusk = (128, 126, 130), (106, 104, 110), (232, 226, 206)
    hd = dims('mammoth-head'); h = V(*hd, 'el'); sx, sy, sz = hd; cx = sx / 2
    h.ell(cx, sy * 0.45, sz * 0.62, sx * 0.42, sy * 0.28, sz * 0.28, g)
    h.cap((cx - .5, sy * 0.68, sz * 0.50), (cx - .5, sy * 0.86, sz * 0.10), 2.4, 1.3, g2)     # Ruessel
    for sgn in (-1, 1):
        h.ell(cx + sgn * sx * 0.40, sy * 0.36, sz * 0.58, 1.2, sy * 0.18, sz * 0.24, mix(g, (190, 150, 150), 0.25))   # grosse Ohren
        h.cap((cx + sgn * 2.5, sy * 0.66, sz * 0.40), (cx + sgn * 3.0, sy * 0.92, sz * 0.30), 1.0, 0.6, tusk, 3)       # Stosszaehne
        h.put(cx + sgn * sx * 0.25 - .5, sy * 0.68, sz * 0.66, (26, 22, 20), 0)
    b = quad_body(dims('mammoth-body'), 'eb', g, g2)
    f = quad_leg(dims('mammoth-foot'), 'ef', g, (210, 204, 190))
    mob('elefant', 'Elefant', 'Mammoth', 1.2, {HEAD: h, BODY: b, FEET: f}, 3, 'Basis Mammut (Skelett), alles eigen')

def fuchs(name, disp, fur, belly, tip):
    h = quad_head(dims('collie-head'), name + 'h', fur, belly, 4, belly, 'point', fur, ear_len=4)
    b = quad_body(dims('collie-body'), name + 'b', fur, belly)
    leg = (40, 34, 30) if name == 'fuchs' else (220, 222, 226)
    f = quad_leg(dims('collie-foot'), name + 'f', mix(fur, leg, 0.5), leg)
    hd = quad_leg(dims('collie-hand'), name + 'a', mix(fur, leg, 0.5), leg)
    t = tail(dims('collie-tail'), name + 't', fur, tip, bushy=True)
    mob(name, disp, 'Collie', 1.0, {HEAD: h, BODY: b, FEET: f, HANDS: hd, TAIL: t}, 3, 'Basis Collie (Skelett), alles eigen')

def luchs():
    fur, belly, spot = (196, 160, 112), (232, 214, 182), (120, 86, 56)
    h = quad_head(dims('cat-head'), 'lh', fur, belly, 2, belly, 'tuft', fur, ear_len=3)
    for k in range(3):                                                                       # Backenbart
        for sgn in (-1, 1): h.put(h.sx / 2 + sgn * (h.sx * 0.42) - .5, h.sy * 0.55, h.sz * 0.30 - k * 0.6, belly, 3)
    b = quad_body(dims('cat-body'), 'lb', fur, belly, spots=0.18, spot_c=spot)
    f = quad_leg(dims('cat-foot'), 'lf', fur, belly); a = quad_leg(dims('cat-hand'), 'la', fur, belly)
    t = tail((4, 5, 5), 'lt', fur, (40, 32, 28))
    mob('luchs', 'Luchs', 'Cat', 1.9, {HEAD: h, BODY: b, FEET: f, HANDS: a, TAIL: t}, 1, 'Basis Katze (Skelett), alles eigen')

def baer(name, disp, fur, snout):
    h = quad_head(dims('wolf-head'), name + 'h', fur, snout, 2, snout, 'round', fur)
    b = quad_body(dims('wolf-body'), name + 'b', fur, mix(fur, (0, 0, 0), 0.15))
    f = quad_leg(dims('wolf-foot'), name + 'f', fur, mix(fur, (0, 0, 0), 0.4)); a = quad_leg(dims('wolf-hand'), name + 'a', fur, mix(fur, (0, 0, 0), 0.4))
    mob(name, disp, 'Wolf', 1.8, {HEAD: h, BODY: b, FEET: f, HANDS: a}, 1, 'Basis Wolf (Skelett, Groesse 1.8), alles eigen, ohne Schwanz')

def hirsch(name, disp, antler):
    fur, belly = (150, 102, 62), (226, 206, 176)
    h = quad_head(dims('pony-head'), name + 'h', fur, belly, 3, (60, 46, 36), 'point', fur, ear_len=3)
    if antler:
        cx = h.sx / 2; ac = (206, 186, 150)
        for sgn in (-1, 1):
            h.cap((cx + sgn * 2.2, h.sy * 0.42, h.sz * 0.72), (cx + sgn * 4.5, h.sy * 0.30, h.sz - 1), 0.6, 0.5, ac, 3)
            h.cap((cx + sgn * 3.4, h.sy * 0.36, h.sz * 0.86), (cx + sgn * 3.0, h.sy * 0.52, h.sz - 1), 0.5, 0.5, ac, 3)
    b = quad_body(dims('pony-body'), name + 'b', fur, belly, spots=0.10 if not antler else 0.0, spot_c=(236, 228, 210))
    f = quad_leg(dims('pony-foot'), name + 'f', fur, (50, 40, 34))
    t = tail(dims('pony-tail'), name + 't', fur, (240, 236, 228))
    mob(name, disp, 'Horse', 1.0, {HEAD: h, BODY: b, FEET: f, TAIL: t}, 3, 'Basis Pferd (Skelett), alles eigen')

def pegasus():
    white, mane, gold = (238, 238, 244), (196, 210, 240), (232, 196, 90)
    h = quad_head(dims('pony-head'), 'ph', white, white, 3, (220, 214, 222), 'point', white, nose=(150, 130, 140), ear_len=3)
    for z in range(int(h.sz * 0.4), h.sz):                                                   # Maehne
        h.put(h.sx / 2 - .5, h.sy * 0.18, z, mane, 6)
    b = quad_body(dims('pony-body'), 'pb', white, (226, 226, 232))
    f = quad_leg(dims('pony-foot'), 'pf', white, gold)
    t = tail(dims('pony-tail'), 'pt', mane, (226, 236, 255))
    wd = dims('dragon-wing'); w = V(*wd, 'pw')                                                # Federfluegel (flach)
    for y in range(w.sy):
        span = int(w.sx * (0.35 + 0.65 * math.sin(math.pi * (y + 1) / (w.sy + 1))))
        for x in range(span): w.put(x, y, 0, mix(white, mane, (x / max(1, span)) * 0.8) if (x + y) % 5 else (210, 216, 236), 5)
    mob('pegasus', 'Pegasus', 'Horse', 1.1, {HEAD: h, BODY: b, FEET: f, TAIL: t, WINGS: w}, 3, 'Basis Pferd (Skelett), Fluegel eigen: Position im Mob-Labor nachjustieren')

def drache(name, disp, scale_c, belly_c, wing_c, spine_c, eye_c):
    hd = dims('dragon-head'); h = V(*hd, name + 'h'); sx, sy, sz = hd; cx = sx / 2
    h.ell(cx, sy * 0.30, sz * 0.52, sx * 0.42, sy * 0.22, sz * 0.40, scale_c)
    h.ell(cx, sy * 0.66, sz * 0.40, sx * 0.30, sy * 0.30, sz * 0.26, scale_c)             # Schnauze
    h.box(cx - sx * 0.22, sy * 0.45, 1, cx + sx * 0.22 - 1, sy * 0.92, sz * 0.22, belly_c)   # Unterkiefer
    for y in range(int(sy * 0.48), int(sy * 0.92), 2):
        for sgn in (-1, 1): h.put(cx + sgn * sx * 0.22 - (1 if sgn > 0 else 0), y, sz * 0.24, (240, 236, 226), 2)   # Zaehne
    for sgn in (-1, 1):
        h.put(cx + sgn * sx * 0.30 - .5, sy * 0.44, sz * 0.66, eye_c, 0)
        h.cap((cx + sgn * 3, sy * 0.20, sz * 0.80), (cx + sgn * 4.5, sy * 0.02, sz * 0.98), 1.0, 0.4, spine_c, 3)   # Hoerner
    for dx in (-1, 1): h.put(cx + dx - .5, sy - 2, sz * 0.48, (30, 24, 22), 0)
    b = quad_body(dims('dragon-body'), name + 'b', scale_c, belly_c)
    for y in range(2, b.sy - 2, 3): b.put(b.sx / 2 - .5, y, b.sz - 1, spine_c, 3)
    td = dims('dragon-tail'); t = V(*td, name + 't')
    for y in range(td[1]):
        r = 0.6 + 3.0 * (y / td[1])
        t.ell(td[0] / 2, y + 0.5, td[2] * 0.25 + (td[2] * 0.4) * (y / td[1]), r, 0.8, r * 0.9, scale_c)
        if y % 3 == 0 and y > 2: t.put(td[0] / 2 - .5, y, td[2] * 0.25 + (td[2] * 0.4) * (y / td[1]) + r, spine_c, 3)
    fd = dims('dragon-foot'); f = V(*fd, name + 'f')
    f.cap((fd[0] / 2, 2, fd[2] - 1), (fd[0] / 2, fd[1] * 0.5, 1.5), 1.6, 1.4, scale_c)
    for k in range(3): f.put(fd[0] / 2 - 1 + k, fd[1] - 1, 0.5, (236, 230, 214), 2)
    wd = dims('dragon-wing'); w = V(*wd, name + 'w')
    for y in range(wd[1]):
        span = int(wd[0] * (0.25 + 0.75 * math.sin(math.pi * (y + 1) / (wd[1] + 1)) ** 0.7))
        for x in range(span):
            bone = (y % 7 == 0) or x == 0
            w.put(x, y, 0, spine_c if bone else mix(wing_c, belly_c, 0.25 * (x / max(1, span))), 4)
    mob(name, disp, 'Dragon', 1.3, {HEAD: h, BODY: b, TAIL: t, FEET: f, WINGS: w}, 1, 'Basis Drache (Skelett, fliegt), alles eigen')

def vogel(name, disp, body_c, belly_c, head_c, beak_c, wing_c, factor):
    hd = dims('crow-head'); h = V(*hd, name + 'h'); sx, sy, sz = hd; cx = sx / 2
    h.ell(cx, sy * 0.40, sz * 0.52, sx * 0.44, sy * 0.32, sz * 0.36, head_c)
    h.cap((cx - .5, sy * 0.66, sz * 0.48), (cx - .5, sy - 1, sz * 0.40 if name != 'kolibri' else sz * 0.46), 1.4, 0.4, beak_c, 3)
    for sgn in (-1, 1): h.put(cx + sgn * sx * 0.32 - .5, sy * 0.58, sz * 0.62, (20, 16, 14), 0)
    bd = dims('crow-body'); b = V(*bd, name + 'b')
    b.ell(bd[0] / 2, bd[1] / 2, bd[2] * 0.5, bd[0] * 0.45, bd[1] * 0.45, bd[2] * 0.46, lambda x, y, z: belly_c if y > bd[1] * 0.55 else body_c)
    fd = dims('crow-foot'); f = V(*fd, name + 'f'); f.box(1, 1, 1, 1, 4, fd[2] - 1, (220, 170, 60)); f.box(0, 3, 0, 2, 5, 0, (220, 170, 60))
    wd = dims('crow-hand'); w = V(*wd, name + 'w')
    for y in range(wd[1]):
        span = int(wd[0] * (0.5 + 0.5 * math.sin(math.pi * (y + 1) / (wd[1] + 1))))
        for x in range(span): w.put(x, y, 0, mix(wing_c, (0, 0, 0), 0.25) if y > wd[1] * 0.7 else wing_c, 5)
    mob(name, disp, 'Crow', factor, {HEAD: h, BODY: b, FEET: f, HANDS: w}, 3, 'Basis Kraehe (Skelett, fliegt), alles eigen')

def insekt(name, disp, base, body_c, head_c, wing_c, pattern=None, factor=1.0, glow=None):
    pre = {'Fly': 'fly', 'Mosquito': 'mosquito', 'Bumblebee': 'bumblebee'}[base]
    hd = dims(pre + '-head') if base != 'Bumblebee' else dims('bumblebee'); slot_head = HEAD
    h = V(*hd, name + 'h'); sx, sy, sz = hd
    h.ell(sx / 2, sy / 2, sz / 2, sx * 0.38, sy * 0.38, sz * 0.38, head_c)
    for sgn in (-1, 1):
        h.ell(sx / 2 + sgn * sx * 0.28, sy * 0.66, sz * 0.58, 1.6, 1.4, 1.6, (24, 20, 30), 2)    # Facettenaugen
        h.cap((sx / 2 + sgn * 1.5, sy * 0.60, sz * 0.80), (sx / 2 + sgn * 3.5, sy * 0.85, sz - 1), 0.5, 0.4, (30, 26, 24), 2)   # Fuehler
    parts = {HEAD: h}
    if base != 'Bumblebee':
        bd = dims(pre + '-body'); b = V(*bd, name + 'b')
        def col(x, y, z):
            if glow and z < bd[2] * 0.35: return glow
            if pattern == 'dots' and H(name, x // 2, y // 2, z // 2) < 0.2: return (24, 20, 20)
            if pattern == 'bands' and (z // 2) % 2 == 0: return mix(body_c, (20, 20, 20), 0.6)
            return body_c
        b.ell(bd[0] / 2, bd[1] / 2, bd[2] * 0.5, bd[0] * 0.40, bd[1] * 0.40, bd[2] * 0.47, col)
        parts[BODY] = b
    wd = dims(pre + '-hand'); w = V(*wd, name + 'w')
    for y in range(wd[1]):
        span = int(wd[0] * (0.4 + 0.6 * math.sin(math.pi * (y + 1) / (wd[1] + 1))))
        for x in range(span):
            c = wing_c
            if pattern == 'monarch' and (x == span - 1 or y % 4 == 0): c = (24, 20, 20)
            if pattern == 'monarch' and x == span - 1 and y % 2 == 0: c = (240, 240, 236)
            w.put(x, y, 0, c, 5)
    parts[HANDS] = w
    fd = dims(pre + '-foot'); f = V(*fd, name + 'f')
    for y in range(fd[1]): f.put(0, y, fd[2] - 1 - y * fd[2] // max(1, fd[1]), (30, 26, 24), 2)
    parts[FEET] = f
    mob(name, disp, base, factor, parts, 3, f'Basis {base} (Skelett, fliegt), alles eigen')

def geist():
    c1, c2 = (206, 226, 240), (160, 190, 220)
    hd = (15, 15, 22); h = V(*hd, 'gh')
    h.ell(7.5, 7.5, 12, 6.5, 6.5, 8.5, lambda x, y, z: mix(c2, c1, z / 22))
    for sgn in (-1, 1): h.ell(7.5 + sgn * 2.4, 13.4, 13, 1.2, 0.8, 1.8, (24, 30, 50), 0)
    h.ell(7.5, 13.6, 8.5, 1.4, 0.7, 1.0, (40, 50, 70), 0)
    bd = dims('zombie-body'); b = V(*bd, 'gb')
    for z in range(bd[2]):
        r = bd[0] * (0.30 + 0.18 * (1 - z / bd[2]))
        b.ell(bd[0] / 2, bd[1] / 2, z + .5, r, r * 0.8, 0.7, mix(c2, c1, z / bd[2]))
    for x in range(0, bd[0], 2): b.clear(x, bd[1] // 2, 0)                                    # ausgefranster Saum
    ad = dims('zombie-hand'); a = V(*ad, 'ga'); a.ell(ad[0] / 2, ad[1] / 2, ad[2] / 2, ad[0] * 0.4, ad[1] * 0.45, ad[2] * 0.4, c1)
    mob('geist', 'Geist', 'UndeadMale', 1.2, {HEAD: h, BODY: b, HANDS: a}, 1, 'Basis Untoter (Skelett), ohne Fuesse, alles eigen')

def schlangenmensch():
    sc, belly, eye = (70, 140, 84), (206, 200, 140), (230, 190, 40)
    hd = (12, 16, 14); h = V(*hd, 'sh'); sx, sy, sz = hd; cx = sx / 2
    h.ell(cx, sy * 0.45, sz * 0.5, sx * 0.40, sy * 0.40, sz * 0.36, sc)
    h.ell(cx, sy * 0.78, sz * 0.36, sx * 0.30, sy * 0.20, sz * 0.18, belly)
    for sgn in (-1, 1): h.put(cx + sgn * sx * 0.30 - .5, sy * 0.66, sz * 0.62, eye, 0)
    for z in range(int(sz * 0.6), sz): h.put(cx - .5, sy * 0.25 - (z - sz * 0.6) * 0.3, z, (200, 60, 50), 4)   # Kamm
    bd = dims('lizard-body'); b = V(*bd, 'sb')
    b.ell(bd[0] / 2, bd[1] / 2, bd[2] / 2, bd[0] * 0.45, bd[1] * 0.40, bd[2] * 0.48, lambda x, y, z: belly if y > bd[1] * 0.6 else sc)
    td = (8, 24, 10); t = V(*td, 'st')                                                        # Schlangenschwanz statt Fuessen
    for y in range(td[1]):
        r = 3.2 - 2.6 * (y / td[1]); z = td[2] - 2 - 6 * math.sin(math.pi * y / td[1] * 0.8)
        t.ell(td[0] / 2, y + .5, max(r, z), r, 0.9, r, lambda xx, yy, zz: belly if zz < max(r, z) - r * 0.4 else sc)
    mob('schlangenmensch', 'Schlangenmensch', 'LizardmanMale', 1.1, {HEAD: h, BODY: b, TAIL: t, HANDS: 'base'}, 1, 'Basis Echsenmensch (Skelett), Schwanz statt Fuessen: Position im Mob-Labor nachjustieren')

def zentaur():
    skin, hair, fur = (222, 180, 146), (90, 60, 40), (128, 88, 56)
    hd = dims('pony-head'); h = V(*hd, 'zh'); sx, sy, sz = hd; cx = sx / 2              # Kopfteil = Oberkoerper + Kopf
    h.ell(cx, sy * 0.5, sz * 0.32, sx * 0.36, sy * 0.22, sz * 0.30, skin)                   # Oberkoerper
    h.ell(cx, sy * 0.5, sz * 0.78, sx * 0.24, sy * 0.24, sz * 0.20, skin)                   # Kopf
    h.ell(cx, sy * 0.44, sz * 0.86, sx * 0.26, sy * 0.24, sz * 0.16, hair)                  # Haar
    for sgn in (-1, 1):
        h.put(cx + sgn * 1.5 - .5, sy * 0.72, sz * 0.80, (30, 24, 20), 0)
        h.cap((cx + sgn * sx * 0.36, sy * 0.5, sz * 0.48), (cx + sgn * sx * 0.44, sy * 0.66, sz * 0.12), 1.0, 0.8, skin)   # Arme
    b = quad_body(dims('pony-body'), 'zb', fur, mix(fur, (240, 220, 200), 0.3))
    f = quad_leg(dims('pony-foot'), 'zf', fur, (50, 40, 34)); t = tail(dims('pony-tail'), 'zt', hair, None)
    mob('zentaur', 'Zentaur', 'Horse', 1.15, {HEAD: h, BODY: b, FEET: f, TAIL: t}, 3, 'Basis Pferd (Skelett), Kopfteil = Oberkoerper: Hoehe im Mob-Labor nachjustieren')

nashorn(); elefant()
fuchs('fuchs', 'Fuchs', (214, 112, 46), (240, 232, 218), (246, 244, 238))
fuchs('polarfuchs', 'Polarfuchs', (236, 238, 242), (250, 250, 252), (250, 250, 252))
luchs()
baer('braunbaer', 'Braunbaer', (112, 76, 48), (170, 130, 94)); baer('eisbaer', 'Eisbaer', (236, 234, 226), (210, 206, 196)); baer('schwarzbaer', 'Schwarzbaer', (44, 40, 40), (150, 120, 96))
hirsch('hirsch', 'Hirsch', True); hirsch('hirschkuh', 'Hirschkuh', False)
pegasus(); zentaur()
drache('frostdrache', 'Frostdrache', (120, 180, 220), (220, 236, 246), (170, 210, 236), (236, 244, 250), (60, 120, 255))
drache('giftdrache', 'Giftdrache', (84, 140, 50), (190, 210, 90), (120, 170, 60), (60, 40, 70), (230, 220, 40))
drache('golddrache', 'Golddrache', (214, 166, 60), (246, 220, 140), (230, 190, 90), (140, 90, 40), (200, 40, 40))
vogel('adler', 'Adler', (96, 66, 42), (120, 86, 56), (240, 238, 232), (232, 190, 60), (88, 60, 40), 1.6)
vogel('falke', 'Falke', (120, 96, 72), (226, 214, 190), (90, 80, 76), (60, 56, 50), (110, 90, 70), 1.2)
vogel('kolibri', 'Kolibri', (40, 170, 120), (220, 230, 220), (40, 160, 110), (30, 26, 24), (60, 190, 140), 0.6)
vogel('schwalbe', 'Schwalbe', (34, 40, 80), (236, 230, 220), (34, 40, 80), (30, 26, 24), (30, 36, 72), 0.8)
insekt('schmetterling', 'Schmetterling', 'Fly', (60, 50, 60), (60, 50, 60), (120, 170, 236), None, 0.9)
insekt('monarchfalter', 'Monarchfalter', 'Fly', (40, 34, 30), (40, 34, 30), (236, 130, 30), 'monarch', 0.9)
insekt('gluehwuermchen', 'Gluehwuermchen', 'Fly', (70, 60, 40), (60, 50, 40), (200, 210, 190), None, 0.6, glow=(220, 250, 120))
insekt('libelle', 'Libelle', 'Mosquito', (40, 150, 170), (40, 120, 160), (210, 230, 240), 'bands', 1.0)
insekt('marienkaefer', 'Marienkaefer', 'Bumblebee', (206, 40, 40), (30, 28, 28), (200, 40, 40), 'dots', 0.8)
geist(); schlangenmensch()

with open(os.path.join(OUT, 'Mobs', 'Tiere', 'LIESMICH_Tiere.md'), 'w', newline='\n') as f:
    f.write("# Neue Tiere (Stand 2, 2026-10-13)\n\nAlle Teile sind von Grund auf gebaut (Grundformen, keine nativen Modelle). "
            "Jedes Tier ist eine eigene Rasse (Kennung ab 200, Name im Spiel); die Basisrasse liefert nur Skelett und Bewegung "
            "(Fliegen). Jedes Teil hat die Masse des nativen Teils der Basisrasse.\n\n| Mob | Name | eigene Rasse | Skelett | Hinweis |\n|---|---|---|---|---|\n")
    for n, d, b, r, note in MOBS: f.write(f"| {n} | {d} | {r} | {b} ({RACE[b]}) | {note} |\n")
print(len(MOBS), 'Tiere,', len(PREV), 'Teile')
if PREVIEW:
    sys.path.insert(0, os.path.normpath(os.path.join(_HERE, '..', '..', '..', '..', '..', 'outputs', 'tools')))
