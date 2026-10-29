# Neue Tierklassen (2026-10-13): Nashorn, Elefant, Fuchs, Luchs, Drachen, Baeren, Hirsche, Pegasus, Zentaur,
# Geist, Schlangenmensch + fliegende Tiere (Voegel, Insekten).
# Grundlage sind die NATIVEN Teilmodelle (data1.db) der Basisrasse: gleiche Groesse und Drehpunkte -> das Skelett,
# die KI und (bei Voegeln/Insekten) das Fliegen kommen von der Basisrasse. Geaendert werden Farben und es kommen
# Merkmale dazu (Horn, Geweih, Pinselohren, Fluegel ...). Ausgabe:
#   <OUT>/Mobs/Tiere/<name>.mob   und   <OUT>/Mobs/Teile/Tiere/<name>-<teil>.cub
import sys, os, re, struct, hashlib, colorsys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubdb

# Aufruf (aus Projekt\\Werkzeuge\\Mobs):  python animals.py   -> schreibt nach <Spielordner>\\Strukturen
_HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(_HERE, '..', '..', '..', 'Strukturen'))
SRC_CPP = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(os.path.join(_HERE, '..', '..', 'AdventureMod', 'MobLab.cpp'))   # Rassennamen -> IDs
names = re.findall(r'"([A-Za-z0-9() ]+)"', open(SRC_CPP, encoding='latin-1').read().split('kRace[')[1].split('};')[0])
RACE = {n: i for i, n in enumerate(names)}

def load(n):
    r = cubdb.get(n + '.cub'); assert r, n
    sx, sy, sz, b = r
    a = np.frombuffer(b, np.uint8).reshape(sz, sy, sx, 3).copy()
    find_eyes(a)                                                     # Augen dieses Modells merken
    return a
def save(a, rel):
    sz, sy, sx, _ = a.shape
    p = os.path.join(OUT, 'Mobs', rel); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(struct.pack('<3i', sx, sy, sz) + a.astype(np.uint8).tobytes())
def H(*k): return int(hashlib.md5(repr(k).encode()).hexdigest()[:8], 16) / 0xFFFFFFFF
def safe(c):
    c = [int(max(1, min(254, round(v)))) for v in c]
    if all(v in (0, 127, 255) for v in c): c[1] += 1           # nie Markerfarbe
    return c
def occ(a): return a.any(3)
def grow(a, sz=None, sy=None, sx=None):
    """Feld vergroessern (neue Zellen leer), Inhalt bleibt bei Index 0."""
    z, y, x, _ = a.shape
    b = np.zeros((sz or z, sy or y, sx or x, 3), np.uint8); b[:z, :y, :x] = a; return b

def lum(c): return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
def sat(c):
    h, l, s = colorsys.rgb_to_hls(*[v / 255 for v in c]); return s, l, h
EYES = set()                                                          # Farben der Augen des gerade bearbeiteten Modells
def find_eyes(a):
    """Augen = seltene Farben (< 2 % der Voxel), sehr bunt oder sehr dunkel. Haeufige bunte Farben sind Fell/Haut."""
    global EYES
    cols = [tuple(int(v) for v in a[z, y, x]) for z, y, x in zip(*np.nonzero(occ(a)))]
    n = max(1, len(cols)); cnt = {}
    for c in cols: cnt[c] = cnt.get(c, 0) + 1
    EYES = set()
    for c, k in cnt.items():
        s, l, h = sat(c)
        if k / n < 0.02 and ((s > 0.6 and 0.2 < l < 0.85) or l < 0.08): EYES.add(c)
    return EYES
def is_eye(c): return tuple(int(v) for v in c) in EYES
_load = None

def recolor(a, pal, keep_eyes=True, tag='', noise=8):
    """pal: Liste (Grenze Helligkeit 0..255, Zielfarbe), aufsteigend. Schattierung bleibt (rel. Helligkeit)."""
    find_eyes(a); b = a.copy()
    for z, y, x in zip(*np.nonzero(occ(a))):
        c = [int(v) for v in a[z, y, x]]
        if keep_eyes and is_eye(c): continue
        L = lum(c)
        tgt = pal[-1][1]
        for lim, col in pal:
            if L <= lim: tgt = col; break
        f = 0.85 + 0.3 * (L / 255.0)
        n = (H(tag, x, y, z) - 0.5) * 2 * noise
        b[z, y, x] = safe([v * f + n for v in tgt])
    return b

def face_y(a): return int(np.nonzero(occ(a).any(axis=(0, 2)))[0].max())
def top_z(a): return int(np.nonzero(occ(a).any(axis=(1, 2)))[0].max())

def put(a, x, y, z, c):
    if 0 <= z < a.shape[0] and 0 <= y < a.shape[1] and 0 <= x < a.shape[2]: a[z, y, x] = safe(c)

MOBS = []
def mob(name, base, factor, parts, host=1, note=''):
    """parts: dict Slot -> ('file', rel) | ('none',) ; fehlende Slots = von der Basisrasse."""
    L = ["# Eigener Mob (LAB). teil=Nr;Quelle;Index;Datei;Skala;Pos(0/1);x;y;z",
         "# Quelle: 0 = von der Basisrasse, 1 = keins, 2 = nativer Registry-Index, 3 = eigene .cub",
         f"# {note}" if note else "#", f"name={name}", f"basis={RACE[base]}", "groesse=0", f"faktor={factor}",
         f"feindlichkeit={host}", "level=-1", "ausruestung=0", "wandern=1"]
    for i in range(8):
        p = parts.get(i)
        if p is None: L.append(f"teil={i};0;-1;;0;0;0;0;0")
        elif p[0] == 'none': L.append(f"teil={i};1;-1;;0;0;0;0;0")
        else: L.append(f"teil={i};3;-1;..\\Teile\\Tiere\\{p[1]};0;0;0;0;0")
    p = os.path.join(OUT, 'Mobs', 'Tiere', name + '.mob'); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w', newline='\n').write('\n'.join(L) + '\n')
    MOBS.append((name, base, note))
HEAD, HAIR, HANDS, FEET, BODY, TAIL, SHOULDER, WINGS = range(8)
def part(name, slot, a):
    rel = f"{name}-{['kopf','haare','haende','fuesse','koerper','schwanz','schulter','fluegel'][slot]}.cub"
    save(a, os.path.join('Teile', 'Tiere', rel)); return ('file', rel)

GRAY = [(70, (70, 72, 76)), (140, (112, 114, 118)), (255, (150, 150, 152))]
# ---------------------------------------------------------------- Nashorn (Kuh)
h = recolor(load('cow-head'), GRAY, tag='nh')
fy = face_y(h); tz = top_z(h); sz, sy, sx, _ = h.shape
h = grow(h, sz=sz + 8)
occh = occ(h)
cx = sx // 2
ys = [y for y in range(sy) if occh[:, y, cx].any()]
# Horn auf dem Nasenruecken: oberster Voxel der vordersten Spalten
snout_y = fy - 1
zt = int(np.nonzero(occh[:, snout_y, cx])[0].max())
horn = (214, 204, 182)
for k in range(7):                                                # grosses Horn, nach vorn-oben gebogen
    r = 1 if k < 4 else 0
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            put(h, cx + dx, min(sy - 1, snout_y + dy + k // 3), zt + 1 + k, [v - k * 4 for v in horn])
zt2 = int(np.nonzero(occh[:, max(0, snout_y - 4), cx])[0].max())
for k in range(3): put(h, cx, snout_y - 4, zt2 + 1 + k, horn)  # kleines Horn dahinter
b = recolor(load('cow-body'), GRAY, tag='nb'); f = recolor(load('cow-foot'), [(80, (58, 58, 62)), (255, (100, 100, 104))], tag='nf')
mob('nashorn', 'Cow', 1.35, {HEAD: part('nashorn', HEAD, h), BODY: part('nashorn', BODY, b), FEET: part('nashorn', FEET, f)}, 1, 'Basis Kuh')

# ---------------------------------------------------------------- Elefant (Mammut)
EG = [(60, (78, 80, 86)), (130, (120, 122, 128)), (205, (150, 150, 156)), (255, (236, 232, 218))]   # hellste = Stosszaehne
h = recolor(load('mammoth-head'), EG, tag='eh'); b = recolor(load('mammoth-body'), EG[:3], tag='eb')
f = recolor(load('mammoth-foot'), [(90, (70, 70, 76)), (255, (126, 126, 132))], tag='ef')
mob('elefant', 'Mammoth', 1.0, {HEAD: part('elefant', HEAD, h), BODY: part('elefant', BODY, b), FEET: part('elefant', FEET, f), HAIR: ('none',)}, 1, 'Basis Mammut, ohne Fell')

# ---------------------------------------------------------------- Fuchs (Collie)
def fox_head(a, main, light, tag):
    find_eyes(a); b = a.copy(); fy = face_y(b); oc = occ(b)
    zs = np.nonzero(oc.any(axis=(1, 2)))[0]; z0, z1 = zs.min(), zs.max()
    for z, y, x in zip(*np.nonzero(oc)):
        c = [int(v) for v in b[z, y, x]]
        if is_eye(c): continue
        L = lum(c); low = z <= z0 + (z1 - z0) * 0.35 and y >= fy - 4
        tgt = light if (low or L > 215) else main
        if L < 60: tgt = (40, 30, 26)
        b[z, y, x] = safe([v * (0.85 + 0.3 * L / 255) + (H(tag, x, y, z) - .5) * 14 for v in tgt])
    return b
for v, (nm, main, light) in enumerate([('fuchs', (206, 104, 38), (240, 232, 220)), ('polarfuchs', (232, 234, 238), (250, 250, 252))]):
    h = fox_head(load('collie-head'), main, light, nm)
    b = recolor(load('collie-body'), [(255, main)], tag=nm + 'b')
    f = recolor(load('collie-foot'), [(255, (52, 40, 34) if v == 0 else (220, 220, 226))], tag=nm + 'f')
    t = recolor(load('collie-tail'), [(255, main)], tag=nm + 't')
    tz = top_z(t)
    for z, y, x in zip(*np.nonzero(occ(t))):
        if z >= tz - 2: t[z, y, x] = safe(light)                    # weisse Schwanzspitze
    mob(nm, 'Collie', 0.85, {HEAD: part(nm, HEAD, h), BODY: part(nm, BODY, b), FEET: part(nm, FEET, f), TAIL: part(nm, TAIL, t)}, 1, 'Basis Collie')

# ---------------------------------------------------------------- Luchs (Katze, gross)
def spots(a, base, spot, tag, dens=0.18):
    find_eyes(a); b = a.copy()
    for z, y, x in zip(*np.nonzero(occ(a))):
        c = [int(v) for v in a[z, y, x]]
        if is_eye(c): continue
        L = lum(c); col = spot if H(tag, x // 2, y // 2, z // 2) < dens else base
        b[z, y, x] = safe([v * (0.85 + 0.3 * L / 255) + (H(tag, x, y, z) - .5) * 10 for v in col])
    return b
lb, ls = (176, 146, 110), (92, 70, 52)
h = spots(load('cat-head'), lb, ls, 'lh', 0.12)
tz = top_z(h); sz, sy, sx, _ = h.shape; h = grow(h, sz=sz + 3)
for x in range(sx):                                               # Pinselohren: schwarze Spitzen auf den Ohren
    for y in range(sy):
        if occ(h)[tz, y, x] and (x < sx // 3 or x > 2 * sx // 3):
            put(h, x, y, tz + 1, (24, 20, 18)); put(h, x, y, tz + 2, (24, 20, 18))
b = spots(load('cat-body'), lb, ls, 'lb'); f = spots(load('cat-foot'), lb, ls, 'lf', 0.1)
t = spots(load('cat-tail'), lb, ls, 'lt'); tz = top_z(t); t = t[:max(3, tz // 3 + 1)].copy()
t[-1][occ(t)[-1]] = (24, 20, 18)
mob('luchs', 'Cat', 1.9, {HEAD: part('luchs', HEAD, h), BODY: part('luchs', BODY, b), FEET: part('luchs', FEET, f), HANDS: part('luchs', HANDS, spots(load('cat-hand'), lb, ls, 'lha')), TAIL: part('luchs', TAIL, t)}, 1, 'Basis Katze, Groesse 1.9')

# ---------------------------------------------------------------- Drachen (Farbvarianten)
DRAG = {'frostdrache': [(70, (60, 98, 150)), (150, (150, 196, 230)), (255, (236, 246, 252))],
        'giftdrache':  [(70, (54, 30, 70)), (150, (70, 150, 60)), (255, (176, 220, 90))],
        'golddrache':  [(70, (120, 40, 24)), (150, (196, 140, 40)), (255, (246, 214, 110))]}
for nm, pal in DRAG.items():
    ps = {}
    for slot, src in [(HEAD, 'dragon-head'), (BODY, 'dragon-body'), (FEET, 'dragon-foot'), (TAIL, 'dragon-tail'), (WINGS, 'dragon-wing')]:
        ps[slot] = part(nm, slot, recolor(load(src), pal, tag=nm + str(slot)))
    mob(nm, 'Dragon', 1.0, ps, 1, 'Basis Drache')

# ---------------------------------------------------------------- Baeren (Wolf-Skelett)
for nm, main, dark in [('braunbaer', (110, 74, 44), (70, 46, 30)), ('eisbaer', (236, 234, 226), (196, 192, 184)), ('schwarzbaer', (40, 36, 38), (22, 20, 22))]:
    pal = [(110, dark), (255, main)]
    h = recolor(load('wolf-head'), pal, tag=nm + 'h')
    fy = face_y(h)
    for z, y, x in zip(*np.nonzero(occ(h))):                     # Schnauze heller
        if y >= fy - 1 and not is_eye([int(v) for v in h[z, y, x]]): h[z, y, x] = safe([min(250, v + 40) for v in main])
    mob(nm, 'Wolf', 1.7, {HEAD: part(nm, HEAD, h), BODY: part(nm, BODY, recolor(load('wolf-body'), pal, tag=nm + 'b')),
                          FEET: part(nm, FEET, recolor(load('wolf-foot'), [(255, dark)], tag=nm + 'f')),
                          HANDS: part(nm, HANDS, recolor(load('wolf-hand'), [(255, dark)], tag=nm + 'ha')), TAIL: ('none',)}, 1, 'Basis Wolf, Groesse 1.7')

# ---------------------------------------------------------------- Hirsch, Hirschkuh (Pferd/Pony)
def antlers(a, tag):
    tz = top_z(a); sz, sy, sx, _ = a.shape; fy = face_y(a)
    b = grow(a, sz=sz + 10, sx=sx + 8)
    b = np.roll(b, 4, axis=2)
    oc = occ(b); cx = sx // 2 + 4
    ys = [y for y in range(sy) if oc[tz, y, :].any()]; ya = (min(ys) + max(ys)) // 2 - 1 if ys else sy // 2
    ant = (150, 120, 84)
    for side in (-1, 1):
        x0 = cx + side * 2
        for k in range(9):
            x = x0 + side * (k // 2); z = tz + 1 + k
            put(b, x, ya, z, [v - k * 3 for v in ant])
            if k in (3, 6):                                      # Enden nach vorn und aussen
                for j in range(1, 3): put(b, x + side * j, ya + j, z + j, ant)
            if k == 8:
                for j in range(1, 3): put(b, x + side * j, ya, z + j, ant)
    return b
deer = [(90, (90, 58, 36)), (200, (150, 98, 58)), (255, (238, 226, 206))]
h = recolor(load('pony-head'), deer, tag='hh'); hb = recolor(load('pony-body'), deer, tag='hb')
for z, y, x in zip(*np.nonzero(occ(hb))):                         # helle Tupfen
    if H('hs', x, y, z) < 0.05 and z >= top_z(hb) - 2: hb[z, y, x] = safe((238, 226, 206))
mob('hirsch', 'Horse', 1.0, {HEAD: part('hirsch', HEAD, antlers(h, 'ha')), BODY: part('hirsch', BODY, hb),
                             FEET: part('hirsch', FEET, recolor(load('pony-foot'), [(255, (70, 48, 32))], tag='hf')),
                             TAIL: part('hirsch', TAIL, recolor(load('pony-tail'), [(255, (238, 226, 206))], tag='ht'))}, 6, 'Basis Pferd, friedlich')
mob('hirschkuh', 'Horse', 0.9, {HEAD: part('hirschkuh', HEAD, recolor(load('pony-head'), deer, tag='kh')), BODY: part('hirschkuh', BODY, hb),
                                FEET: part('hirschkuh', FEET, recolor(load('pony-foot'), [(255, (70, 48, 32))], tag='kf')),
                                TAIL: ('none',)}, 6, 'Basis Pferd, friedlich')

# ---------------------------------------------------------------- Pegasus (Pferd + Fluegel)
white = [(90, (170, 178, 196)), (200, (226, 230, 240)), (255, (250, 250, 252))]
w = load('dragon-wing')
wf = w.copy()
for z, y, x in zip(*np.nonzero(occ(w))):                          # Federn: Streifen entlang y
    g = 236 - 18 * ((y // 3) % 2) - (10 if x > w.shape[2] * 0.7 else 0)
    wf[z, y, x] = safe((g, g, min(254, g + 10)))
mob('pegasus', 'Horse', 1.0, {HEAD: part('pegasus', HEAD, recolor(load('pony-head'), white, tag='ph')), BODY: part('pegasus', BODY, recolor(load('pony-body'), white, tag='pb')),
                              FEET: part('pegasus', FEET, recolor(load('pony-foot'), [(255, (200, 190, 150))], tag='pf')),
                              TAIL: part('pegasus', TAIL, recolor(load('pony-tail'), [(255, (210, 220, 250))], tag='pt')),
                              WINGS: part('pegasus', WINGS, wf)}, 6, 'Basis Pferd, Fluegel: Position im Mob-Labor anpassen (UNGETESTET)')

# ---------------------------------------------------------------- Zentaur (Pferd, Kopfteil = Oberkoerper mit Kopf)
hm = load('human-head-m02') if cubdb.get('human-head-m02.cub') else load('undead-head-m01')
skin = [(90, (120, 80, 56)), (200, (196, 146, 108)), (255, (232, 196, 160))]
hm = recolor(hm, skin, tag='zk')
hz, hy, hx, _ = hm.shape
torso_h = 14
cent = np.zeros((hz + torso_h, max(hy, 10), hx, 3), np.uint8)
cent[torso_h:, :hy, :] = hm
cx0 = hx // 2 - 4
for z in range(torso_h):                                          # Oberkoerper (Lederharnisch), Arme an den Seiten
    for y in range(2, 8):
        for x in range(cx0, cx0 + 8):
            col = (120, 84, 50) if z > 4 else (180, 140, 100)
            if z > 10 and (x in (cx0, cx0 + 7)): col = (196, 146, 108)
            cent[z, y, x] = safe([v + (H('zt', x, y, z) - .5) * 12 for v in col])
mob('zentaur', 'Horse', 1.1, {HEAD: part('zentaur', HEAD, cent), BODY: part('zentaur', BODY, recolor(load('pony-body'), deer, tag='zb')),
                              FEET: part('zentaur', FEET, recolor(load('pony-foot'), [(255, (60, 40, 28))], tag='zf'))}, 1, 'Basis Pferd, Kopfteil = Oberkoerper (UNGETESTET, Hoehe im Mob-Labor anpassen)')

# ---------------------------------------------------------------- Geist (Untoter, schwebend ohne Fuesse)
ghost = [(90, (120, 150, 190)), (180, (186, 214, 236)), (255, (230, 244, 252))]
ug = load('undead-head-m01'); gh = recolor(ug, ghost, tag='gh')
for z, y, x in zip(*np.nonzero(occ(gh))):
    if is_eye([int(v) for v in ug[z, y, x]]): gh[z, y, x] = safe((90, 240, 250))
gb = recolor(load('wizard-body'), ghost, tag='gb')
for z, y, x in zip(*np.nonzero(occ(gb))):                         # ausfransender Saum unten
    if z <= 2 and H('gs', x, y) < 0.45: gb[z, y, x] = 0
mob('geist', 'UndeadMale', 1.0, {HEAD: part('geist', HEAD, gh), BODY: part('geist', BODY, gb),
                                 HANDS: part('geist', HANDS, recolor(load('undead-hand'), ghost, tag='gha')), FEET: ('none',), HAIR: ('none',)}, 1, 'Basis Untoter, ohne Fuesse')

# ---------------------------------------------------------------- Schlangenmensch (Echsenmensch + Schlangenschwanz)
def snake(a, tag):
    find_eyes(a); b = a.copy()
    for z, y, x in zip(*np.nonzero(occ(a))):
        c = [int(v) for v in a[z, y, x]]
        if is_eye(c): continue
        L = lum(c); d = ((x + z) // 2 + (y + z) // 2) % 4 == 0           # Rautenmuster
        col = (196, 176, 60) if d else ((54, 120, 60) if L > 70 else (34, 80, 40))
        b[z, y, x] = safe([v * (0.85 + 0.3 * L / 255) + (H(tag, x, y, z) - .5) * 10 for v in col])
    return b
tail = np.zeros((6, 28, 8, 3), np.uint8)                           # Schwanz: verjuengtes Rohr entlang y
for y in range(28):
    r = 3.2 - y * 0.1
    for z in range(6):
        for x in range(8):
            if (x - 3.5) ** 2 + (z - 2.5) ** 2 <= r * r:
                d = ((x + y) // 2) % 4 == 0
                tail[z, y, x] = safe((196, 176, 60) if d else (54, 120, 60) if z > 2 else (210, 196, 120))
mob('schlangenmensch', 'LizardmanMale', 1.1, {HEAD: part('schlangenmensch', HEAD, snake(load('lizard-head-m01'), 'sh')),
                                              BODY: part('schlangenmensch', BODY, snake(load('lizard-body'), 'sb')),
                                              HANDS: part('schlangenmensch', HANDS, snake(load('lizard-hand'), 'sha')),
                                              FEET: ('none',), TAIL: part('schlangenmensch', TAIL, tail), HAIR: ('none',)}, 1, 'Basis Echsenmensch, Schwanz statt Fuessen (Position im Mob-Labor anpassen)')

# ================================================================ Fliegende Tiere
# Voegel (Basis Kraehe: fliegt), Insekten (Basis Hornisse/Fliege/Hummel: fliegen)
def wing(a, inner, outer, tag, stripes=False):
    b = a.copy(); sx = a.shape[2]
    for z, y, x in zip(*np.nonzero(occ(a))):
        t = x / max(1, sx - 1); col = [inner[i] * (1 - t) + outer[i] * t for i in range(3)]
        if stripes and (y // 2) % 3 == 0: col = [v * 0.6 for v in col]
        b[z, y, x] = safe([v + (H(tag, x, y) - .5) * 10 for v in col])
    return b
BIRDS = {
    'adler':   ('eagle-head', [(120, (60, 40, 24)), (255, (236, 232, 222))], (86, 58, 36), (110, 76, 46), (52, 34, 20), 1.6, 1),
    'falke':   ('crow-head',  [(80, (52, 56, 70)), (255, (150, 156, 172))], (110, 116, 132), (120, 128, 146), (60, 64, 80), 1.0, 1),
    'kolibri': ('crow-head',  [(80, (30, 110, 70)), (255, (210, 40, 70))], (40, 160, 90), (60, 190, 110), (30, 120, 70), 0.5, 6),
    'schwalbe':('crow-head',  [(80, (26, 32, 70)), (255, (200, 60, 40))], (30, 38, 90), (240, 236, 230), (24, 30, 76), 0.7, 6),
}
for nm, (hsrc, hpal, bodyc, bellyc, wingc, fac, host) in BIRDS.items():
    h = recolor(load(hsrc), hpal, tag=nm + 'h')
    fy = face_y(h)
    for z, y, x in zip(*np.nonzero(occ(h))):                       # Schnabel gelb/dunkel
        if y >= fy - 1 and z <= top_z(h) // 2 + 2 and not is_eye([int(v) for v in h[z, y, x]]):
            h[z, y, x] = safe((230, 180, 40) if nm in ('adler', 'falke') else (40, 36, 34))
    b = load('crow-body'); bb = b.copy(); by = face_y(b)
    for z, y, x in zip(*np.nonzero(occ(b))):
        L = lum([int(v) for v in b[z, y, x]]); col = bellyc if y >= by - 2 and z < b.shape[0] * 0.6 else bodyc
        bb[z, y, x] = safe([v * (0.85 + 0.3 * L / 255) + (H(nm, x, y, z) - .5) * 10 for v in col])
    mob(nm, 'Crow', fac, {HEAD: part(nm, HEAD, h), BODY: part(nm, BODY, bb),
                          HANDS: part(nm, HANDS, wing(load('crow-hand'), wingc, [min(250, v + 50) for v in wingc], nm + 'w', stripes=(nm == 'falke'))),
                          FEET: part(nm, FEET, recolor(load('crow-foot'), [(255, (220, 170, 40))], tag=nm + 'f'))}, host, 'Basis Kraehe (fliegt)')
INSECTS = {
    'schmetterling': ('Fly', 'fly', (30, 30, 36), ((40, 110, 230), (20, 40, 120)), 1.3, 6),
    'monarchfalter': ('Fly', 'fly', (30, 26, 24), ((236, 130, 30), (40, 30, 26)), 1.3, 6),
    'libelle':       ('Mosquito', 'mosquito', (40, 170, 170), ((200, 230, 240), (150, 200, 220)), 1.2, 6),
    'gluehwuermchen':('Fly', 'fly', (60, 56, 40), ((200, 210, 170), (160, 170, 140)), 0.8, 6),
    'marienkaefer':  ('Bumblebee', None, (200, 30, 30), ((210, 220, 230), (170, 190, 210)), 1.0, 6),
}
for nm, (base, pre, bodyc, (w1, w2), fac, host) in INSECTS.items():
    ps = {}
    if pre:
        b = recolor(load(pre + '-body'), [(255, bodyc)], tag=nm + 'b')
        if nm == 'gluehwuermchen':                                 # leuchtender Hinterleib (hintere Haelfte, unten)
            for z, y, x in zip(*np.nonzero(occ(b))):
                if y < b.shape[1] * 0.45: b[z, y, x] = safe((236, 250, 120))
        ps[BODY] = part(nm, BODY, b)
        ps[HEAD] = part(nm, HEAD, recolor(load(pre + '-head'), [(255, bodyc)], tag=nm + 'h'))
        ps[HANDS] = part(nm, HANDS, wing(load(pre + '-hand'), w1, w2, nm + 'w', stripes=(nm in ('schmetterling', 'monarchfalter'))))
        ps[FEET] = part(nm, FEET, recolor(load(pre + '-foot'), [(255, (30, 30, 30))], tag=nm + 'f'))
    else:                                                          # Marienkaefer: Hummel-Koerper rot mit schwarzen Punkten
        b = load('bumblebee'); bb = b.copy()
        for z, y, x in zip(*np.nonzero(occ(b))):
            c = [int(v) for v in b[z, y, x]]
            if is_eye(c): continue
            dot = H('mk', x // 3, y // 3, z // 3) < 0.22
            bb[z, y, x] = safe((24, 22, 22) if dot else (200, 30, 30))
        ps[BODY] = part(nm, BODY, bb)
        ps[HANDS] = part(nm, HANDS, wing(load('bumblebee-hand'), w1, w2, nm + 'w'))
    mob(nm, base, fac, ps, host, f'Basis {base} (fliegt)')

with open(os.path.join(OUT, 'Mobs', 'Tiere', 'LIESMICH_Tiere.md'), 'w', encoding='utf-8', newline='\n') as f:
    f.write('# Neue Tiere (2026-10-13)\n\nJede `.mob` nimmt Skelett, KI und Bewegung (auch das Fliegen) von der Basisrasse; '
            'die Teile in `..\\Teile\\Tiere\\` sind umgefaerbte bzw. ergaenzte native Modelle derselben Rasse.\n\n| Mob | Basis | Hinweis |\n|---|---|---|\n')
    for n, b, note in MOBS: f.write(f'| {n} | {b} ({RACE[b]}) | {note} |\n')
print(len(MOBS), 'Mobs')
