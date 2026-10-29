# tierbau2.py (2026-10-16) -- Bausteine fuer Tiere mit sauber verbundenen Teilen.
#
# Warum: Die Teile werden vom Spiel um feste Drehpunkte (je Rasse) gezeichnet, jedes Teil um seine Mitte. Frei
# geformte Ovale passten deshalb nicht an die Gelenke (Luecken, Ueberlappungen, kein Hals). Hier richtet sich jedes
# Teil nach den GELENKPUNKTEN der Basisrasse: Aus dem nativen Teil werden nur Lage und Ausdehnung je Scheibe gelesen
# (wo Hals, Kopf, Bein, Pfote liegen); die Form selbst wird neu gebaut -- abgerundete Querschnitte (Superellipse,
# kein Oval), schlankere Beine, eigene Farben/Muster, Gesicht (Augen mit Weiss und Pupille, Nase, Maul), Ohren,
# Hoerner, Maehnen. Kopfteile duerfen ringsum Rand bekommen (Platz fuer Ohren/Hoerner); der Rand ist symmetrisch, das
# Teil bleibt also auf seinem Drehpunkt (der Mob setzt dafuer "auto_ausrichtung=0").
import os, sys, math, hashlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cubdb
from tierbau import V, safe, mix, H

BLACK, WHITE = (26, 22, 20), (242, 240, 234)

def nmask(name):
    r = cubdb.get(name + '.cub'); assert r, name
    sx, sy, sz, b = r
    a = np.frombuffer(b, np.uint8)[:sx * sy * sz * 3].reshape(sz, sy, sx, 3)
    return a.sum(3) > 0

def _components2d(m):
    """Zusammenhaengende Flaechen einer 2D-Maske (4er-Nachbarschaft) -> Liste von Boolean-Masken."""
    h, w = m.shape; seen = np.zeros_like(m); out = []
    for i in range(h):
        for j in range(w):
            if m[i, j] and not seen[i, j]:
                st = [(i, j)]; seen[i, j] = True; comp = np.zeros_like(m)
                while st:
                    a, b = st.pop(); comp[a, b] = True
                    for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        c, d = a + da, b + db
                        if 0 <= c < h and 0 <= d < w and m[c, d] and not seen[c, d]: seen[c, d] = True; st.append((c, d))
                out.append(comp)
    return out

def rebuild(mask, axis='z', power=3.0, shrink=1.0, minr=0.6):
    """Neue Form: jede Scheibe (quer zur Achse) wird je Flaeche durch eine Superellipse in deren Huelle ersetzt."""
    sz, sy, sx = mask.shape; out = np.zeros_like(mask)
    rng = {'z': range(sz), 'y': range(sy), 'x': range(sx)}[axis]
    for k in rng:
        sl = mask[k] if axis == 'z' else (mask[:, k, :] if axis == 'y' else mask[:, :, k])
        for comp in _components2d(sl):
            ii, jj = np.nonzero(comp)
            c0, c1 = (ii.min() + ii.max() + 1) / 2, (jj.min() + jj.max() + 1) / 2
            r0, r1 = max(minr, (ii.max() - ii.min() + 1) / 2 * shrink), max(minr, (jj.max() - jj.min() + 1) / 2 * shrink)
            for i in range(int(c0 - r0) - 1, int(c0 + r0) + 2):
                for j in range(int(c1 - r1) - 1, int(c1 + r1) + 2):
                    if not (0 <= i < sl.shape[0] and 0 <= j < sl.shape[1]): continue
                    if abs((i + .5 - c0) / r0) ** power + abs((j + .5 - c1) / r1) ** power <= 1.0:
                        if axis == 'z': out[k, i, j] = True
                        elif axis == 'y': out[i, k, j] = True
                        else: out[i, j, k] = True
    return out

class Part:
    """Teil mit Rand pad (Voxel ringsum); occ/col im gepolsterten Raster; save() -> tierbau.V."""
    def __init__(s, native, tag, pad=0):
        s.tag = tag; s.pad = pad; m = nmask(native)
        s.nz, s.ny, s.nx = m.shape
        s.sz, s.sy, s.sx = s.nz + 2 * pad, s.ny + 2 * pad, s.nx + 2 * pad
        s.native = np.zeros((s.sz, s.sy, s.sx), bool); s.native[pad:pad + s.nz, pad:pad + s.ny, pad:pad + s.nx] = m
        s.occ = np.zeros_like(s.native); s.col = np.zeros((s.sz, s.sy, s.sx, 3), np.float32)
    def fill_from(s, occ, colfn):
        for z, y, x in zip(*np.nonzero(occ)):
            s.occ[z, y, x] = True; s.col[z, y, x] = colfn(x, y, z)
    def put(s, x, y, z, c):
        x, y, z = int(round(x)), int(round(y)), int(round(z))
        if 0 <= x < s.sx and 0 <= y < s.sy and 0 <= z < s.sz: s.occ[z, y, x] = True; s.col[z, y, x] = c
    def clear(s, x, y, z):
        x, y, z = int(round(x)), int(round(y)), int(round(z))
        if 0 <= x < s.sx and 0 <= y < s.sy and 0 <= z < s.sz: s.occ[z, y, x] = False
    def ell(s, cx, cy, cz, rx, ry, rz, c, power=2.0, only_empty=False):
        for z in range(int(cz - rz) - 1, int(cz + rz) + 2):
            for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
                for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                    if abs((x + .5 - cx) / rx) ** power + abs((y + .5 - cy) / ry) ** power + abs((z + .5 - cz) / rz) ** power <= 1:
                        if only_empty and 0 <= x < s.sx and 0 <= y < s.sy and 0 <= z < s.sz and s.occ[z, y, x]: continue
                        s.put(x, y, z, c(x, y, z) if callable(c) else c)
    def cap(s, p0, p1, r0, r1, c, only_empty=False):
        L = math.dist(p0, p1); n = max(2, int(L * 3))
        for i in range(n + 1):
            t = i / n; p = [p0[k] + (p1[k] - p0[k]) * t for k in range(3)]; r = r0 + (r1 - r0) * t
            s.ell(p[0], p[1], p[2], r, r, r, c, 2.0, only_empty)
    def bbox(s, occ=None):
        z, y, x = np.nonzero(s.occ if occ is None else occ)
        return x.min(), x.max(), y.min(), y.max(), z.min(), z.max()
    def surface_x(s, y, z, side):
        """aeusserster belegter x in Zeile (y,z): side -1 links, +1 rechts; None = leer"""
        y, z = int(round(y)), int(round(z))
        if not (0 <= y < s.sy and 0 <= z < s.sz): return None
        xs = np.nonzero(s.occ[z, y, :])[0]
        if not len(xs): return None
        return xs.min() if side < 0 else xs.max()
    def front_y(s, x, z):
        x, z = int(round(x)), int(round(z))
        if not (0 <= x < s.sx and 0 <= z < s.sz): return None
        ys = np.nonzero(s.occ[z, :, x])[0]
        return ys.max() if len(ys) else None
    def shade(s, outline=0.82, light=True):
        """Konturen etwas dunkler (Kanten zu Luft), oben heller -> klare Formen statt Klumpen"""
        o = s.occ; out = s.col.copy()
        for z, y, x in zip(*np.nonzero(o)):
            f = 1.0
            if light: f *= 0.86 + 0.26 * (z / max(1, s.sz - 1))
            n = 0
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if not (0 <= xx < s.sx and 0 <= yy < s.sy) or not o[z, yy, xx]: n += 1
            if n >= 2: f *= outline
            out[z, y, x] = out[z, y, x] * f
        s.col = out
    def to_v(s, noise=5):
        v = V(s.sx, s.sy, s.sz, s.tag)
        for z, y, x in zip(*np.nonzero(s.occ)):
            c = s.col[z, y, x]; nn = (H(s.tag, x, y, z) - 0.5) * 2 * noise
            v.a[z, y, x] = safe([c[0] + nn, c[1] + nn, c[2] + nn]); v.o[z, y, x] = True
        return v

# ----------------------------------------------------------------------------- Gesicht
def face(p, *, snout_front=True, eye_iris=(40, 30, 22), eye_big=False, nose=(40, 30, 30), mouth=True, eye_z=0.62, eye_back=0.30, brows=None):
    """Augen (Weiss + Pupille), Nase und Maul auf die Kopfflaeche setzen. Kopf = oberer vorderer Teil des Teils."""
    x0, x1, y0, y1, z0, z1 = p.bbox()
    # Kopfbereich: obere Haelfte des Teils, vorne
    hz0 = z0 + (z1 - z0) * 0.35
    ez = hz0 + (z1 - hz0) * eye_z
    ys = [y for y in range(y1, y0 - 1, -1) if p.surface_x(y, ez, 1) is not None]
    if not ys: return
    fy = ys[0]; ey = fy - max(1, int((fy - y0) * eye_back))
    for side in (-1, 1):
        ex = p.surface_x(ey, ez, side)
        if ex is None: continue
        p.put(ex, ey, ez, WHITE); p.put(ex, ey + 1, ez, eye_iris)
        p.put(ex, ey + 1, ez + 1, WHITE) if eye_big else None
        p.put(ex, ey, ez + 1, WHITE) if eye_big else None
        if brows: p.put(ex, ey, ez + 1 + (1 if eye_big else 0), brows)
    cx = (x0 + x1 + 1) / 2
    if nose:
        nzs = [z for z in range(int(hz0), z1 + 1) if p.front_y(cx - .5, z) is not None]
        if nzs:
            nz = nzs[len(nzs) // 3] if snout_front else nzs[len(nzs) // 2]
            for dx in (-1, 0):
                fy2 = p.front_y(cx + dx, nz)
                if fy2 is not None: p.put(cx + dx, fy2, nz, nose)
            if mouth:
                mz = max(int(hz0), nz - 2)
                for dx in (-2, -1, 0, 1):
                    fy3 = p.front_y(cx + dx, mz)
                    if fy3 is not None: p.put(cx + dx, fy3, mz, mix(nose, BLACK, 0.4))

def ears(p, kind, c, inner=None, size=3):
    x0, x1, y0, y1, z0, z1 = p.bbox(p.native)
    cx = (x0 + x1 + 1) / 2
    # Ohrposition: oberste Lage des Kopfes, hinteres Drittel des Kopfbereichs
    top = p.bbox()[5]
    ys = [y for y in range(p.sy) if p.occ[top].any() and p.occ[top, y].any()]
    ey = (min(ys) + max(ys)) / 2 if ys else (y0 + y1) / 2
    hw = max(1.5, (p.bbox()[1] - p.bbox()[0]) * 0.32)
    for sg in (-1, 1):
        bx = cx + sg * hw
        if kind == 'point':
            for k in range(size):
                w = max(0.6, 1.4 - k * 0.45)
                p.ell(bx, ey - 0.3 * k, top + 0.5 + k, w, 0.7, 0.6, c if k < size - 1 else mix(c, BLACK, 0.5))
            if inner: p.put(bx, ey + 1, top + 1, inner)
        elif kind == 'round':
            p.ell(bx, ey, top + 0.8, 1.5, 0.8, 1.3, c)
            if inner: p.put(bx, ey + 1, top + 1, inner)
        elif kind == 'long':
            for k in range(size + 3): p.ell(bx + sg * 0.2 * k, ey - 0.25 * k, top + 0.5 + k, 1.0, 0.6, 0.6, c)
        elif kind == 'big':
            p.ell(bx + sg * 1.8, ey, top - 0.5, 1.0, 2.2, 2.8, c)
            if inner: p.ell(bx + sg * 1.8, ey + 0.8, top - 0.5, 0.5, 1.0, 1.8, inner)
        elif kind == 'droop':
            p.ell(bx + sg * 1.5, ey, top - 1.5, 0.8, 1.6, 2.4, c)
        elif kind == 'tuft':
            for k in range(size): p.put(bx, ey, top + 1 + k, c if k < size - 1 else BLACK)

def horns(p, kind, c):
    x0, x1, y0, y1, z0, z1 = p.bbox()
    cx = (x0 + x1 + 1) / 2; top = z1
    ys = [y for y in range(p.sy) if p.occ[top, y].any()]
    hy = (min(ys) + max(ys)) / 2 if ys else (y0 + y1) / 2
    hw = max(1.5, (x1 - x0) * 0.25)
    Z = p.sz - 1
    for sg in (-1, 1):
        bx = cx + sg * hw
        if kind == 'antler':
            p.cap((bx, hy, top), (bx + sg * 3.5, hy - 1.5, min(Z, top + 6)), 0.6, 0.45, c)
            p.cap((bx + sg * 1.8, hy - 0.8, top + 3), (bx + sg * 1.2, hy + 1.5, min(Z, top + 6)), 0.5, 0.4, c)
            p.cap((bx + sg * 3.0, hy - 1.2, top + 5), (bx + sg * 4.5, hy, min(Z, top + 7)), 0.45, 0.4, c)
        elif kind == 'moose':
            p.cap((bx, hy, top), (bx + sg * 3, hy, top + 2), 0.7, 0.6, c)
            p.ell(bx + sg * 4.2, hy - 0.5, min(Z - 1, top + 3), 1.6, 2.4, 1.0, c)
        elif kind == 'curl':
            for k in range(10):
                a = k / 9 * 3.6
                p.ell(bx + sg * 0.25 * k, hy - math.sin(a) * 3.2, top + 1.2 + math.cos(a) * 2.2 - k * 0.12, 0.9, 0.9, 0.9, c)
        elif kind == 'bull':
            p.cap((bx, hy, top), (bx + sg * 3.5, hy, top + 1), 0.9, 0.6, c)
            p.cap((bx + sg * 3.5, hy, top + 1), (bx + sg * 3.8, hy + 0.8, min(Z, top + 3.5)), 0.6, 0.4, c)
        elif kind == 'spiral':
            for k in range(8): p.put(bx + sg * 0.15 * k, hy - 0.5 * k, min(Z, top + 1 + k), c if k % 2 else mix(c, BLACK, 0.3))
        elif kind == 'short':
            p.cap((bx, hy, top), (bx, hy + 0.5, top + 2), 0.6, 0.4, c)
    if kind == 'uni':
        fy = max(y for y in range(p.sy) if p.occ[:, y].any())
        p.cap((cx - .5, fy - 2, top - 1), (cx - .5, fy + 1, min(Z, top + 5)), 0.9, 0.3, c)

def mane(p, c, kind='ring', skull=None):
    x0, x1, y0, y1, z0, z1 = p.bbox()
    cx = (x0 + x1 + 1) / 2
    if kind == 'crest':                                                   # Kamm auf Hals/Nacken
        for y in range(y0, y1 + 1):
            zs = np.nonzero(p.occ[:, y, int(cx)])[0]
            if len(zs) and y < (y0 + y1) / 2 + 2: p.put(cx - .5, y, zs.max() + 1, c)
        return
    # Ring um den Schaedel (Loewe): 3 Scheiben hinter dem Gesicht, Radius = Schaedel + 2
    scx, scy, scz, srx, srz = skull
    for y in range(int(scy) - 2, int(scy) + 1):
        for z in range(p.sz):
            for x in range(p.sx):
                d = math.hypot((x + .5 - scx) / (srx + 2.2), (z + .5 - scz) / (srz + 2.0))
                if d <= 1.0 and 0 <= y < p.sy and not p.occ[z, y, x] and H(p.tag, 'm', x, z) < 0.92: p.put(x, y, z, mix(c, BLACK, 0.15 * H(p.tag, x, z)))

# ----------------------------------------------------------------------------- Teil-Bauer
def coat(top, under, *, spots=None, spot_c=None, stripes=None, stripe_c=None, tag='', zsplit=0.35, sz=10, sy=10, front=None):
    def f(x, y, z):
        c = under if z < sz * zsplit else top
        if front and y > sy * 0.75 and z < sz * 0.6: c = front
        if spots and z >= sz * zsplit and H(tag, 's', x // 2, y // 2, z // 2) < spots: c = spot_c
        if stripes and z >= sz * 0.3 and (y // 2) % 3 == 0: c = stripe_c
        return c
    return f

def head_part(native, tag, fur, belly, *, pad=3, snout=1.0, snout_c=None, nose=(40, 30, 30), iris=(40, 30, 22), eye_big=False,
              ear=None, ear_c=None, ear_in=None, ear_size=3, horn=None, horn_c=(220, 206, 176), mane_kind=None, mane_c=None,
              spots=None, spot_c=None, stripes=None, stripe_c=None, mouth=True, beak=None, extra=None, eyes_front=False, head_len=0.58,
              neck_c=None, snout_h=0.30, skull_h=0.95):
    """Kopf + Hals. Lage aus dem nativen Teil: vorderster Punkt = Schnauzenspitze, Kopf = vorderer Abschnitt, Rest = Hals
    (Hals in Scheiben neu gebaut, bleibt so am Koerper). Kopf selbst: Schaedel + Schnauze (eigene Formen), Gesicht."""
    p = Part(native, tag, pad)
    n = p.native
    zz, yy, xx = np.nonzero(n)
    fy = yy.max(); by = yy.min(); ny = fy - by + 1
    HL = int(max(5, min(ny, round(ny * head_len))))
    hm = n & (np.arange(p.sy)[None, :, None] >= fy - HL + 1)
    hz_, hy_, hx_ = np.nonzero(hm)
    x0, x1, z0, z1 = hx_.min(), hx_.max(), hz_.min(), hz_.max()
    cx = (x0 + x1 + 1) / 2; hw = (x1 - x0 + 1); hh = (z1 - z0 + 1)
    sc = snout_c or belly; nc = neck_c or fur
    neck = n & ~hm
    if neck.any():
        p.fill_from(rebuild(neck, 'z', 2.6, 0.9), lambda x, y, z: belly if (y > (by + fy) / 2 and z < z0) else nc)
    # Schaedel
    sy0 = fy - HL + 1; sl = HL * (0.62 if beak is None else 0.95)
    p.ell(cx, sy0 + sl / 2, z0 + hh * 0.52, hw / 2, sl / 2 + 0.3, hh / 2 * skull_h,
          lambda x, y, z: belly if z < z0 + hh * 0.22 else fur, 2.6)
    # Schnauze (vorne, etwas tiefer, kantiger)
    if beak is None:
        L = max(2.0, (HL - sl * 0.7) * snout)
        szc = z0 + hh * (0.30 + (0.30 - snout_h) * 0.5)
        p.ell(cx, min(p.sy - 1.5, fy + 0.5 - L / 2 + (snout - 1) * 1.0), szc, hw * 0.33, L / 2 + 0.4, hh * snout_h,
              lambda x, y, z: sc, 3.2)
    if spots or stripes:
        for z, y, x in zip(*np.nonzero(p.occ)):
            if spots and H(tag, 's', x // 2, y // 2, z // 2) < spots and z > z0 + 2: p.col[z, y, x] = spot_c
            if stripes and (z // 2) % 2 == 0 and y < fy - 3 and z > z0 + 1: p.col[z, y, x] = stripe_c
    if mane_kind: mane(p, mane_c or mix(fur, (100, 60, 30), 0.6), mane_kind, (cx, sy0 + sl * 0.45, z0 + hh * 0.52, hw / 2, hh / 2 * skull_h))
    if ear: ears_at(p, ear, ear_c or fur, ear_in, ear_size, cx, sy0 + sl * 0.35, hw)
    if horn: horns(p, horn, horn_c)
    # Gesicht
    zs_front = [z for z in range(p.sz) if p.front_y(cx - .5, z) is not None and p.front_y(cx - .5, z) >= p.bbox()[3] - 1]
    ez = z0 + hh * 0.72; ey = int(sy0 + sl * (0.80 if beak is None else 0.70))
    for sg in (-1, 1):
        if eyes_front:                                                        # nach vorn (Raubtiere, Affen, Eulen)
            ex = cx + sg * hw * 0.22 - (1 if sg < 0 else 0)
            fyy = p.front_y(ex, ez)
            if fyy is not None:
                p.put(ex, fyy, ez, WHITE); p.put(ex + (0 if sg < 0 else 0), fyy, ez, iris if not eye_big else WHITE)
                if eye_big: p.put(ex, fyy, ez - 1, iris)
                p.put(ex - sg, fyy, ez, WHITE) if eye_big else None
        else:                                                                 # seitlich (Beutetiere, Voegel)
            ex = p.surface_x(ey, ez, sg)
            if ex is not None:
                p.put(ex, ey, ez, WHITE); p.put(ex, ey + 1, ez, iris)
                if eye_big: p.put(ex, ey, ez + 1, WHITE); p.put(ex, ey + 1, ez + 1, iris)
    if beak is not None:
        kind, bc = beak
        fy2 = p.bbox()[3]
        bz = z0 + hh * 0.48
        L = {'short': 2, 'long': 5, 'hook': 3, 'flat': 3, 'pouch': 5, 'toucan': 5, 'curved': 4}[kind]
        L = max(1, min(L, p.sy - 1 - fy2))
        if kind == 'flat': p.ell(cx, fy2 + L / 2 + .5, bz - 0.5, 1.6, L / 2 + .5, 0.7, bc)
        elif kind == 'toucan': p.ell(cx, fy2 + L / 2 + .5, bz, 1.4, L / 2 + .5, 1.8, lambda x, y, z: (250, 160, 30) if y < fy2 + L * 0.8 else (220, 40, 30))
        elif kind == 'pouch': p.cap((cx, fy2, bz), (cx, fy2 + L, bz - 0.5), 1.0, 0.6, bc); p.ell(cx, fy2 + L / 2, bz - 1.6, 1.2, L / 2, 1.0, mix(bc, (240, 200, 160), 0.4))
        elif kind == 'curved': p.cap((cx, fy2, bz), (cx, fy2 + L, bz - 1), 0.9, 0.6, bc); p.put(cx, fy2 + L, bz - 2, BLACK)
        else:
            p.cap((cx, fy2, bz), (cx, fy2 + L, bz - (1 if kind == 'hook' else 0)), 1.0, 0.45, bc)
            if kind == 'hook': p.put(cx, fy2 + L, bz - 1.5, bc)
    elif nose:
        fyf = p.bbox()[3]
        zn = [z for z in range(p.sz) if p.front_y(cx - .5, z) == fyf]
        if zn:
            ztop = max(zn)
            for dx in (-1, 0): p.put(cx + dx, p.front_y(cx + dx, ztop) or fyf, ztop, nose)
            if mouth:
                mz = min(zn)
                for dx in (-1, 0):
                    f2 = p.front_y(cx + dx, mz + 1)
                    if f2 is not None: p.put(cx + dx, f2, mz + 1, mix(nose, BLACK, 0.5))
                # Maulspalte seitlich
                for sg in (-1, 1):
                    for dy in range(1, 4):
                        xs = p.surface_x(fyf - dy, mz + 1, sg)
                        if xs is not None: p.put(xs, fyf - dy, mz + 1, mix(sc, BLACK, 0.55))
    if extra: extra(p)
    p.shade()
    return p.to_v()

def ears_at(p, kind, c, inner, size, cx, ey, hw):
    top = max(z for z in range(p.sz) if p.occ[z, int(ey)].any()) if p.occ[:, int(ey)].any() else p.bbox()[5]
    w = max(1.2, hw * 0.30)
    for sg in (-1, 1):
        bx = cx + sg * w
        if kind == 'point':
            for k in range(size):
                r = max(0.55, 1.3 - k * 0.4)
                p.ell(bx, ey - 0.2 * k, top + 0.6 + k, r, 0.7, 0.55, c if k < size - 1 else mix(c, BLACK, 0.5))
            if inner: p.put(bx, ey + 1, top + 1, inner)
        elif kind == 'round':
            p.ell(bx + sg * 0.3, ey, top + 0.7, 1.4, 0.7, 1.2, c)
            if inner: p.put(bx + sg * 0.3, ey + 1, top + 1, inner)
        elif kind == 'long':
            for k in range(size + 3): p.ell(bx + sg * 0.15 * k, ey - 0.3 * k, top + 0.5 + k, 0.9, 0.55, 0.55, c)
            if inner: [p.put(bx + sg * 0.15 * k, ey - 0.3 * k + 0.6, top + 0.5 + k, inner) for k in range(1, size + 2)]
        elif kind == 'big':
            p.ell(bx + sg * 1.6, ey, top - 0.3, 0.9, 2.0, 2.6, c)
            if inner: p.ell(bx + sg * 1.6, ey + 0.7, top - 0.3, 0.5, 1.0, 1.8, inner)
        elif kind == 'droop':
            p.ell(bx + sg * 1.3, ey, top - 1.2, 0.8, 1.4, 2.2, c)
        elif kind == 'tuft':
            for k in range(size): p.put(bx, ey, top + 1 + k, c if k < size - 1 else BLACK)

def body_part(native, tag, top, under, *, power=3.0, spots=None, spot_c=None, stripes=None, stripe_c=None, hump=0, front=None, extra=None, shrink=1.0):
    p = Part(native, tag, 0)
    occ = rebuild(p.native, 'y', power, shrink)
    p.fill_from(occ, coat(top, under, spots=spots, spot_c=spot_c, stripes=stripes, stripe_c=stripe_c, tag=tag, sz=p.sz, sy=p.sy, front=front))
    if hump:
        x0, x1, y0, y1, z0, z1 = p.bbox()
        for k in range(hump): p.ell((x0 + x1 + 1) / 2, y0 + (y1 - y0) * (0.35 + 0.3 * k), z1 - 0.5, (x1 - x0) * 0.25, (y1 - y0) * 0.14, 1.6, top, 2.0, True)
    if extra: extra(p)
    p.shade()
    return p.to_v()

def leg_part(native, tag, fur, paw, *, shrink=0.8, power=2.6, hoof=False, claws=None):
    p = Part(native, tag, 0)
    occ = rebuild(p.native, 'z', power, shrink)
    x0, x1, y0, y1, z0, z1 = p.bbox(occ)
    def colf(x, y, z):
        if z <= z0 + (1 if hoof else 0) + (z1 - z0) * (0.18 if hoof else 0.1): return paw
        if z > z0 + (z1 - z0) * 0.45 and z < z0 + (z1 - z0) * 0.55: return mix(fur, BLACK, 0.12)   # Gelenk
        return fur
    p.fill_from(occ, colf)
    if claws:
        fy = p.bbox()[3]
        for dx in (-1, 0, 1):
            p.put((x0 + x1 + 1) / 2 + dx - .5, fy, z0, claws)
    p.shade(outline=0.88)
    return p.to_v()

def tail_part(native, tag, c, tip=None, bushy=False, power=2.2):
    p = Part(native, tag, 1 if bushy else 0)
    occ = rebuild(p.native, 'z', power, 1.0 if not bushy else 1.0)
    if bushy:                                                             # buschig: Querschnitt aufblaehen (im Rand)
        big = np.zeros_like(occ)
        for z, y, x in zip(*np.nonzero(occ)):
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if abs(dx) + abs(dy) < 2 and 0 <= y + dy < p.sy and 0 <= x + dx < p.sx: big[z, y + dy, x + dx] = True
        occ = big
    zs = np.nonzero(occ)[0]; zmin, zmax = zs.min(), zs.max()
    p.fill_from(occ, lambda x, y, z: tip if (tip and z < zmin + (zmax - zmin) * 0.3) else c)
    p.shade(outline=0.9)
    return p.to_v()

def wing_part(native, tag, c, tip=None, bar=None):
    p = Part(native, tag, 0)
    m = p.native
    x0, x1, y0, y1, z0, z1 = p.bbox(m)
    def colf(x, y, z):
        cc = c
        if tip and (y < y0 + (y1 - y0) * 0.3): cc = tip                     # Schwungfedern
        if bar and abs(x - (x0 + x1) / 2) < 1: cc = bar
        if (x + y) % 4 == 0: cc = mix(cc, BLACK, 0.12)                       # Federzeichnung
        return cc
    p.fill_from(m, colf)
    return p.to_v(noise=4)
