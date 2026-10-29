#!/usr/bin/env python3
"""Dungeon-Baukasten, Format 2 (Konstruktionselemente) fuer Strukturen\\Dungeons (Dungeons.cpp).

Aufruf:
  python dungeons_bau.py <Strukturen-Ordner>                 Module + dungeon.txt schreiben
  python dungeons_bau.py <Strukturen-Ordner> --pruefen       zusaetzlich 300 Grundrisse je Dungeon wuerfeln (wie Dungeons.cpp)
  python dungeons_bau.py <Strukturen-Ordner> --vorschau      zusaetzlich vorschau_*.png je Dungeon

Legt an: Dungeons\\Burg (Burg + Tempelteile) und Dungeons\\Apfelhain, je dungeon.txt + Module\\*.cub/.txt.
Dungeons\\Tempel wird abgeschaltet (aktiv=0, steckt jetzt in der Burg).

Regeln (muessen zu Dungeons.cpp passen):
  * .cub-Layout wie das Spiel: x laeuft am schnellsten, dann y, dann z. y+ = Norden, x+ = Osten.
  * Boden z=0..1, begehbar ab z=2. Waende 2 dick (= Wandstaerke des Anschluss-Typs).
  * Anschluss "anschluss=typ,richtung,u,z": Oeffnung (Breite/Hoehe des Typs) in der Wand 'richtung',
    u = kleinste x- (N/S) bzw. y-Koordinate (O/W) der Oeffnung, z = Boden-Oberkante.
  * Burg: Ebenenhoehe 14. Treppen immer 3 breit, ueber jeder Stufe mindestens 4 Bloecke frei.
    Durchgaenge 7 breit, 10 hoch (auch fuer Oger/Trolle).
  * Markerfarben: Monster (254,1,254)  Boss (254,1,128)  Truhe (254,128,1)  Vase (128,1,254)  Licht (254,254,1)
"""
import os, sys, struct, random
import numpy as np

MONSTER, BOSS, CHEST, VASE, LIGHT = (254, 1, 254), (254, 1, 128), (254, 128, 1), (128, 1, 254), (254, 254, 1)
N, E, S, W = "N", "O", "S", "W"
DIRS = {N: 0, E: 1, S: 2, W: 3}


class Room:
    def __init__(self, sx, sy, sz):
        self.sx, self.sy, self.sz = sx, sy, sz
        self.v = np.zeros((sz, sy, sx, 3), dtype=np.uint8)      # [z][y][x]

    def inside(self, x, y, z):
        return 0 <= x < self.sx and 0 <= y < self.sy and 0 <= z < self.sz

    def set(self, x, y, z, c):
        if self.inside(x, y, z):
            self.v[z, y, x] = [max(1, min(255, int(k))) for k in c]

    def clear(self, x, y, z):
        if self.inside(x, y, z):
            self.v[z, y, x] = 0

    def solid(self, x, y, z):
        return self.inside(x, y, z) and bool(self.v[z, y, x].any())

    def _clip(self, x0, y0, z0, x1, y1, z1):
        return (max(0, x0), max(0, y0), max(0, z0), min(self.sx - 1, x1), min(self.sy - 1, y1), min(self.sz - 1, z1))

    def box(self, x0, y0, z0, x1, y1, z1, col, amp=0, seed=0):
        """inklusiv; col = Farbe; amp = Helligkeitsrauschen je Block."""
        x0, y0, z0, x1, y1, z1 = self._clip(x0, y0, z0, x1, y1, z1)
        if x0 > x1 or y0 > y1 or z0 > z1:
            return
        shape = (z1 - z0 + 1, y1 - y0 + 1, x1 - x0 + 1)
        c = np.array(col, dtype=np.int32).reshape(1, 1, 1, 3) * np.ones(shape + (3,), dtype=np.int32)
        if amp:
            rng = np.random.default_rng(seed + x0 * 7 + y0 * 131 + z0 * 977)
            c = c + rng.integers(-amp, amp + 1, size=shape + (1,))
        self.v[z0:z1 + 1, y0:y1 + 1, x0:x1 + 1] = np.clip(c, 1, 255).astype(np.uint8)

    def carve(self, x0, y0, z0, x1, y1, z1):
        x0, y0, z0, x1, y1, z1 = self._clip(x0, y0, z0, x1, y1, z1)
        if x0 > x1 or y0 > y1 or z0 > z1:
            return
        self.v[z0:z1 + 1, y0:y1 + 1, x0:x1 + 1] = 0

    def paste(self, other, ox, oy, oz):
        """feste Voxel von other bei (ox,oy,oz) einsetzen (abgeschnitten am Rand)"""
        x0, y0, z0 = max(0, ox), max(0, oy), max(0, oz)
        x1, y1, z1 = min(self.sx, ox + other.sx), min(self.sy, oy + other.sy), min(self.sz, oz + other.sz)
        if x0 >= x1 or y0 >= y1 or z0 >= z1:
            return
        src = other.v[z0 - oz:z1 - oz, y0 - oy:y1 - oy, x0 - ox:x1 - ox]
        dst = self.v[z0:z1, y0:y1, x0:x1]
        m = src.any(3)
        dst[m] = src[m]

    def save(self, path):
        with open(path, "wb") as f:
            f.write(struct.pack("<3i", self.sx, self.sy, self.sz))
            f.write(self.v.tobytes())


def load_cub(path):
    b = open(path, "rb").read()
    sx, sy, sz = struct.unpack_from("<3i", b, 0)
    r = Room(sx, sy, sz)
    r.v = np.frombuffer(b, np.uint8, offset=12).reshape(sz, sy, sx, 3).copy()
    return r


def rot90(r):
    """wie Lab::Structure::RotateZ90: (x,y) -> (sy-1-y, x)"""
    n = Room(r.sy, r.sx, r.sz)
    for y in range(r.sy):
        n.v[:, :, r.sy - 1 - y] = r.v[:, y, :]
    return n


def scaled(r, f):
    """naechster Nachbar, alle Achsen gleich"""
    nx, ny, nz = max(1, round(r.sx * f)), max(1, round(r.sy * f)), max(1, round(r.sz * f))
    ix = np.minimum((np.arange(nx) / f).astype(int), r.sx - 1)
    iy = np.minimum((np.arange(ny) / f).astype(int), r.sy - 1)
    iz = np.minimum((np.arange(nz) / f).astype(int), r.sz - 1)
    n = Room(nx, ny, nz)
    n.v = r.v[iz][:, iy][:, :, ix].copy()
    return n


def cropped(r):
    s = r.v.any(3)
    zs, ys, xs = np.nonzero(s)
    n = Room(xs.max() - xs.min() + 1, ys.max() - ys.min() + 1, zs.max() + 1)
    n.v = r.v[:zs.max() + 1, ys.min():ys.max() + 1, xs.min():xs.max() + 1].copy()
    return n


# ---- Baukasten-Helfer ------------------------------------------------------------------------------
TYPES = {}          # je Dungeon gesetzt: name -> (w, h, t)


def opening(r, typ, d, u, z):
    w, h, t = TYPES[typ]
    if d == N: r.carve(u, r.sy - t, z, u + w - 1, r.sy - 1, z + h - 1)
    if d == S: r.carve(u, 0, z, u + w - 1, t - 1, z + h - 1)
    if d == E: r.carve(r.sx - t, u, z, r.sx - 1, u + w - 1, z + h - 1)
    if d == W: r.carve(0, u, z, t - 1, u + w - 1, z + h - 1)


def frame(r, typ, d, u, z, col):
    """Rahmen (Pfosten + Sturz) um eine Oeffnung, in der Wand."""
    w, h, t = TYPES[typ]
    if d in (N, S):
        y0, y1 = (r.sy - t, r.sy - 1) if d == N else (0, t - 1)
        r.box(u - 1, y0, z, u - 1, y1, z + h, col); r.box(u + w, y0, z, u + w, y1, z + h, col)
        r.box(u - 1, y0, z + h, u + w, y1, z + h, col)
    else:
        x0, x1 = (r.sx - t, r.sx - 1) if d == E else (0, t - 1)
        r.box(x0, u - 1, z, x1, u - 1, z + h, col); r.box(x0, u + w, z, x1, u + w, z + h, col)
        r.box(x0, u - 1, z + h, x1, u + w, z + h, col)


def centered(r, typ, d):
    w = TYPES[typ][0]
    return ((r.sx if d in (N, S) else r.sy) - w) // 2


def write_mod(dirp, name, r, art, conns, gewicht=10, maxn=99, dach=0, note="", ebene="", pflicht=0):
    os.makedirs(dirp, exist_ok=True)
    r.save(os.path.join(dirp, name + ".cub"))
    with open(os.path.join(dirp, name + ".txt"), "w", encoding="utf-8") as f:
        if note:
            f.write("# " + note + "\n")
        f.write("art=%s\ngewicht=%d\nmax=%d\ndach=%d\n" % (art, gewicht, maxn, dach))
        if ebene:
            f.write("ebene=%s\n" % ebene)
        if pflicht:
            f.write("pflicht=%d\n" % pflicht)
        for (typ, d, u, z) in conns:
            f.write("anschluss=%s,%s,%d,%d\n" % (typ, d, u, z))


def write_def(dirp, text):
    os.makedirs(dirp, exist_ok=True)
    with open(os.path.join(dirp, "dungeon.txt"), "w", encoding="utf-8") as f:
        f.write(text.strip() + "\n")


# ===================================================================================================
# BURG (mit Tempelteilen) -- Ebenenhoehe 14, Durchgang 7 x 10
# ===================================================================================================
def burg(root):
    D = os.path.join(root, "Burg")
    M = os.path.join(D, "Module")
    TYPES.clear(); TYPES.update({"gang": (7, 10, 2), "aussen": (7, 10, 2)})
    L = 14                                                     # Ebenenhoehe
    # Schluesselfarben: Dungeons.cpp ersetzt sie je Biom durch EINE Farbe (biomfarben=1) -- darum ohne Rauschen/Muster
    stone, floor_c, ceil_c = (128, 128, 132), (98, 94, 90), (86, 82, 80)
    dark, wood, red, iron, gold = (92, 92, 96), (122, 84, 50), (150, 30, 34), (70, 70, 76), (220, 180, 60)
    sand, marble = (222, 210, 178), (236, 234, 226)
    cnt = [0]

    def seed():
        cnt[0] += 1
        return cnt[0] * 1009

    def room(Wd, Dp, H, conns, wall=stone, flo=floor_c, ceil=ceil_c, t=2, slits=True, lights=True):
        r = Room(Wd, Dp, H)
        s = seed()
        r.box(0, 0, 0, Wd - 1, Dp - 1, 1, flo)
        r.box(0, 0, 2, Wd - 1, t - 1, H - 2, wall); r.box(0, Dp - t, 2, Wd - 1, Dp - 1, H - 2, wall)
        r.box(0, 0, 2, t - 1, Dp - 1, H - 2, wall); r.box(Wd - t, 0, 2, Wd - 1, Dp - 1, H - 2, wall)
        if ceil is not None:
            r.box(0, 0, H - 1, Wd - 1, Dp - 1, H - 1, ceil)
        else:
            r.box(0, 0, H - 1, Wd - 1, Dp - 1, H - 1, wall)
            r.carve(t, t, H - 1, Wd - 1 - t, Dp - 1 - t, H - 1)
        if slits:                                               # Schiessscharten 1 breit, 3 hoch, alle 6 Bloecke
            for z in range(6, H - 4, L):
                for x in range(4, Wd - 4, 6):
                    r.carve(x, 0, z, x, t - 1, z + 2); r.carve(x, Dp - t, z, x, Dp - 1, z + 2)
                for y in range(4, Dp - 4, 6):
                    r.carve(0, y, z, t - 1, y, z + 2); r.carve(Wd - t, y, z, Wd - 1, y, z + 2)
        for (typ, d, u, z) in conns:
            opening(r, typ, d, u, z)
            frame(r, typ, d, u, z, dark)
        if lights:                                              # Fackeln an den Innenwaenden
            for z in range(8, H - 2, L):
                for x in range(4, Wd - 4, 8):
                    for y in (t, Dp - 1 - t):
                        if not r.solid(x, y, z) and r.solid(x, y + (-1 if y == t else 1), z):
                            r.set(x, y, z, LIGHT)
        return r

    def C(typ, d, u, z=2):
        return (typ, d, u, z)

    # -- Gaenge -----------------------------------------------------------------------------------
    for name, dims, conns, gew, mons in [
        ("gang_gerade", (11, 21), [C("gang", S, 2), C("gang", N, 2)], 16, 1),
        ("gang_lang", (11, 31), [C("gang", S, 2), C("gang", N, 2), C("gang", E, 12)], 10, 2),
        ("gang_ecke", (11, 11), [C("gang", S, 2), C("gang", E, 2)], 10, 0),
        ("gang_t", (11, 11), [C("gang", S, 2), C("gang", E, 2), C("gang", W, 2)], 9, 0),
        ("gang_kreuz", (11, 11), [C("gang", S, 2), C("gang", N, 2), C("gang", E, 2), C("gang", W, 2)], 6, 0),
    ]:
        r = room(dims[0], dims[1], L, conns, slits=False)
        for i in range(mons):
            r.set(5, 6 + i * 10, 2, MONSTER)
        write_mod(M, name, r, "raum", conns, gew, dach=1)

    # -- Treppenraeume (Aufstocken): Treppe 3 breit, 14 Stufen je 1 hoch / 1 tief, ueber jeder Stufe mind. 4 frei.
    #    treppenhaus (Grundebene, Treppe Ost)  -> darueber treppenraum_mitte (Bodenloch Ost, Treppe West)
    #    -> darueber treppenraum_dach (Bodenloch West). Ohne Raum darueber fuehrt die Treppe aufs Dach (Zinnen).
    TW, TD = 17, 21
    tconns = [C("gang", S, 5), C("gang", N, 5)]

    def flight(r, x0, rail_x):
        """Stufen k=1..14 bei y=2+k (oberster Block z=1+k, Standhoehe 2+k). Stufen 13/14 liegen schon in den Bodenlagen
        des Moduls darueber (arrive). Vorher das Deckenloch schneiden, sonst fehlt Stufe 12."""
        r.carve(x0, 3, L - 1, x0 + 2, 16, L - 1)
        for k in range(1, L - 1):
            y = 2 + k
            r.box(x0, y, 2, x0 + 2, y, 1 + k, (110, 106, 100))
            r.box(rail_x, y, 2, rail_x, y, min(L - 1, 2 + k), dark)

    def arrive(r, x0, rail_x):
        """Bodenloch ueber der Treppe darunter (y 3..14) + die Stufen 13/14 in den Bodenlagen + Gelaender."""
        r.carve(x0, 3, 0, x0 + 2, 14, 1)
        r.box(x0, 15, 0, x0 + 2, 15, 0, (110, 106, 100)); r.clear(x0, 15, 1); r.clear(x0 + 1, 15, 1); r.clear(x0 + 2, 15, 1)
        r.box(x0, 16, 0, x0 + 2, 16, 1, (110, 106, 100))
        r.box(rail_x, 3, 2, rail_x, 14, 2, dark); r.box(x0, 2, 2, x0 + 2, 2, 2, dark)

    r = room(TW, TD, L, tconns, slits=False)
    flight(r, 12, 11)
    r.set(4, 8, 2, MONSTER); r.set(3, 3, 8, LIGHT)
    write_mod(M, "treppenhaus", r, "raum", tconns, 9, maxn=4, dach=1, pflicht=2, ebene="boden",
              note="Treppenraum Grundebene: Treppe 3 breit nach oben (Ost), darueber kommt treppenraum_mitte")
    with open(os.path.join(M, "treppenhaus.txt"), "a", encoding="utf-8") as f: f.write("oben=treppenraum_mitte\n")

    r = room(TW, TD, L, tconns, slits=False)
    arrive(r, 12, 11)
    flight(r, 2, 5)
    r.set(8, 8, 2, MONSTER); r.set(15, 3, 8, LIGHT)
    write_mod(M, "treppenraum_mitte", r, "raum", tconns, 1, maxn=0, dach=1, ebene="oben",
              note="Treppenraum mittlere Ebene: Ankunft Ost, Treppe weiter nach oben (West) -- ohne Raum darueber aufs Dach")
    with open(os.path.join(M, "treppenraum_mitte.txt"), "a", encoding="utf-8") as f: f.write("oben=treppenraum_dach\n")

    r = room(TW, TD, L, tconns, slits=False)
    arrive(r, 2, 5)
    r.set(10, 8, 2, MONSTER); r.set(15, 3, 8, LIGHT); r.set(14, 17, 2, CHEST)
    write_mod(M, "treppenraum_dach", r, "raum", tconns, 1, maxn=0, dach=1, ebene="oben",
              note="Treppenraum oberste Ebene: Ankunft West")

    # -- Torhaus (Eingang) mit Flankentuermen --------------------------------------------------------
    conns = [C("aussen", S, 8), C("gang", N, 8)]
    r = room(23, 17, 22, [], slits=False, lights=False)
    r.carve(2, 2, L - 1, 20, 14, 21)                           # nur unten Raum, oben Tuerme
    r.box(0, 0, L - 1, 22, 16, L - 1, ceil_c)                  # Decke Torhalle
    for tx in (0, 16):                                         # Tuerme 7x7 vorn, bis 21 hoch
        r.box(tx, 0, L, tx + 6, 6, 21, stone)
        r.carve(tx + 2, 2, L, tx + 4, 4, 20)
        for k in range(tx, tx + 7, 2):
            r.clear(k, 0, 21); r.clear(k, 6, 21)
    for (typ, d, u, z) in conns:
        opening(r, typ, d, u, z); frame(r, typ, d, u, z, dark)
    for x in range(8, 15, 2):                                  # Fallgitterzaehne
        r.box(x, 1, 11, x, 1, 11, iron)
    r.set(3, 8, 2, MONSTER); r.set(19, 8, 2, MONSTER); r.set(3, 12, 2, VASE)
    r.set(2, 5, 8, LIGHT); r.set(20, 5, 8, LIGHT)
    write_mod(M, "torhaus", r, "eingang", conns, 10, dach=0, ebene="boden", note="Eingang: aussen nach Sueden bleibt offen")

    # -- Burghof (offen) ---------------------------------------------------------------------------
    for name, size, gew, mx in [("burghof", 45, 6, 2), ("hof_klein", 31, 8, 2)]:
        u = (size - 7) // 2
        conns = [C("gang", S, u), C("gang", N, u), C("gang", E, u), C("gang", W, u)]
        r = room(size, size, 16, conns, ceil=None, slits=False, lights=False)
        r.carve(2, 2, 14, size - 3, size - 3, 15)
        for k in range(0, size, 2):                            # Zinnen auf der Hofmauer
            for (x, y) in ((k, 0), (k, size - 1), (0, k), (size - 1, k)):
                r.clear(x, y, 15)
        c = size // 2
        r.box(c - 3, c - 3, 2, c + 3, c + 3, 3, (110, 110, 116)); r.carve(c - 2, c - 2, 2, c + 2, c + 2, 3)   # Brunnen
        r.box(c - 2, c - 2, 1, c + 2, c + 2, 1, (60, 100, 200))
        for (x, y) in ((6, 6), (size - 7, 6), (6, size - 7), (size - 7, size - 7)):
            r.box(x, y, 2, x, y, 5, wood); r.set(x, y, 6, LIGHT)
        r.set(c + 6, c, 2, MONSTER); r.set(c - 6, c + 3, 2, MONSTER); r.set(c, c - 7, 2, MONSTER)
        r.set(4, size - 5, 2, CHEST)
        write_mod(M, name, r, "raum", conns, gew, maxn=mx, dach=0, ebene="boden")

    # -- Rittersaal (zwei Ebenen hoch, ohne Galerie: obere Ebenen nur ueber Treppenhaeuser) ------------
    conns = [C("gang", S, 10), C("gang", N, 10), C("gang", E, 15), C("gang", W, 15)]
    r = room(27, 37, 2 * L, conns)
    for (x, y) in ((6, 8), (6, 18), (6, 28), (19, 8), (19, 18), (19, 28)):
        r.box(x, y, 2, x + 1, y + 1, 2 * L - 2, stone)         # Saeulen bis unter die Decke
    for x in (4, 21):                                          # Banner neben (nicht vor) den Toren
        r.box(x, 2, 6, x + 1, 2, 12, red); r.box(x, 34, 6, x + 1, 34, 12, red)
    r.box(11, 10, 2, 15, 26, 3, wood)                          # Tafel
    r.set(9, 14, 2, MONSTER); r.set(17, 20, 2, MONSTER); r.set(13, 30, 2, MONSTER)
    r.set(9, 6, 2, VASE)
    write_mod(M, "rittersaal", r, "raum", conns, 8, maxn=2, dach=1, note="zwei Ebenen hoch")

    # -- Wohn-/Nebenraeume ---------------------------------------------------------------------------
    conns = [C("gang", S, 7), C("gang", E, 7)]
    r = room(21, 21, L, conns)
    for x in (3, 7, 12, 16):
        r.box(x, 15, 2, x + 2, 18, 2, wood); r.box(x, 17, 3, x + 2, 18, 3, (200, 200, 190))
    r.set(6, 9, 2, MONSTER); r.set(14, 8, 2, MONSTER); r.set(17, 4, 2, VASE)
    write_mod(M, "kaserne", r, "raum", conns, 9, maxn=3, dach=1)

    conns = [C("gang", S, 5)]
    r = room(17, 17, L, conns)
    for y in range(4, 14, 3):
        r.box(2, y, 3, 2, y + 1, 8, iron); r.box(14, y, 3, 14, y + 1, 8, iron)
    r.box(7, 10, 2, 9, 12, 3, (60, 60, 64)); r.set(8, 11, 4, (230, 120, 40))
    r.set(5, 7, 2, MONSTER); r.set(11, 7, 2, MONSTER); r.set(8, 14, 2, CHEST)
    write_mod(M, "waffenkammer", r, "raum", conns, 7, maxn=2, dach=1)

    conns = [C("gang", S, 4)]
    r = room(15, 15, L, conns, slits=False)
    r.box(2, 10, 2, 12, 12, 2, gold)
    r.set(3, 11, 3, CHEST); r.set(7, 11, 3, CHEST); r.set(11, 11, 3, CHEST); r.set(7, 6, 2, MONSTER)
    write_mod(M, "schatzkammer", r, "raum", conns, 4, maxn=2, dach=1)

    conns = [C("gang", S, 7), C("gang", W, 9)]
    r = room(21, 25, L, conns)
    for x in range(4, 18, 3):
        r.box(x, 21, 2, x + 1, 22, 9, (90, 60, 40))
        for z in (3, 5, 7):
            r.box(x, 21, z, x + 1, 21, z, (140 + 30 * (x % 3), 70, 50))
    r.box(8, 11, 2, 12, 13, 3, wood)
    r.set(5, 14, 2, MONSTER); r.set(15, 14, 2, MONSTER); r.set(10, 19, 2, CHEST)
    write_mod(M, "bibliothek", r, "raum", conns, 6, maxn=2, dach=1)

    conns = [C("gang", S, 4)]
    r = room(15, 15, 34, conns, lights=False)                  # Turm (Sackgasse), Zinnen kommen vom Generator
    r.box(2, 2, L, 12, 12, L + 1, floor_c); r.carve(5, 5, L, 9, 9, L + 1)
    r.set(2, 7, 8, LIGHT); r.set(12, 7, 8, LIGHT); r.set(7, 7, 2, MONSTER); r.set(3, 11, L + 2, CHEST)
    write_mod(M, "turm", r, "raum", conns, 5, maxn=3, dach=1, note="Turm, Sackgasse, oben Truhe")

    # -- Tempelteile (Sandstein, Marmor, Gold) --------------------------------------------------------
    conns = [C("gang", S, 9), C("gang", N, 9)]
    r = room(25, 35, 24, conns, wall=stone, flo=floor_c, ceil=ceil_c)
    for y in (7, 14, 21, 28):
        for x in (5, 18):
            r.box(x, y, 2, x + 1, y + 1, 21, marble); r.box(x - 1, y - 1, 21, x + 2, y + 2, 21, gold)
    r.box(8, 24, 2, 16, 30, 2, marble); r.box(10, 27, 3, 14, 30, 3, marble); r.box(11, 29, 4, 13, 30, 9, gold)
    r.set(12, 25, 4, LIGHT); r.set(8, 25, 3, LIGHT); r.set(16, 25, 3, LIGHT)
    r.set(9, 12, 2, MONSTER); r.set(15, 18, 2, MONSTER); r.set(12, 27, 4, CHEST); r.set(3, 32, 2, VASE)
    write_mod(M, "kapelle", r, "raum", conns, 6, maxn=2, dach=1, note="Tempelteil")

    u = 14
    conns = [C("gang", S, u), C("gang", N, u), C("gang", E, u), C("gang", W, u)]
    r = room(35, 35, 24, conns, wall=stone, flo=floor_c, ceil=ceil_c)
    r.box(11, 11, 1, 23, 23, 1, (70, 120, 190)); r.box(10, 10, 2, 24, 10, 2, marble); r.box(10, 24, 2, 24, 24, 2, marble)
    r.box(10, 10, 2, 10, 24, 2, marble); r.box(24, 10, 2, 24, 24, 2, marble)
    for (x, y) in ((6, 6), (27, 6), (6, 27), (27, 27)):
        r.box(x, y, 2, x + 1, y + 1, 21, marble); r.box(x - 1, y - 1, 21, x + 2, y + 2, 21, gold)
        r.box(x, y, 22, x + 1, y + 1, 22, gold)
    r.box(16, 16, 2, 18, 18, 10, gold)                         # Goldstatue im Becken
    r.set(17, 17, 11, LIGHT)
    r.set(7, 17, 2, MONSTER); r.set(27, 17, 2, MONSTER); r.set(17, 7, 2, MONSTER); r.set(17, 28, 2, CHEST)
    write_mod(M, "tempelhalle", r, "raum", conns, 5, maxn=1, dach=1, note="Tempelteil")

    # -- Thronsaal (Boss) -----------------------------------------------------------------------------
    conns = [C("gang", S, 12)]
    r = room(31, 31, 22, conns)
    r.box(9, 22, 2, 21, 28, 2, red); r.box(12, 25, 3, 18, 28, 3, gold); r.box(14, 27, 4, 16, 28, 10, gold)
    for (x, y) in ((5, 5), (24, 5), (5, 18), (24, 18)):
        r.box(x, y, 2, x + 1, y + 1, 19, stone)
    for x in (4, 25):
        r.box(x, 28, 5, x + 1, 28, 14, red)
    r.set(15, 18, 2, BOSS); r.set(9, 12, 2, MONSTER); r.set(21, 12, 2, MONSTER)
    r.set(8, 26, 2, CHEST); r.set(22, 26, 2, CHEST)
    write_mod(M, "thronsaal", r, "boss", conns, 10, dach=1)

    conns = [C("gang", S, 12)]
    r = room(31, 31, 24, conns, wall=stone, flo=floor_c, ceil=ceil_c)
    r.box(10, 20, 2, 20, 27, 3, marble); r.box(13, 24, 4, 17, 27, 13, gold)
    r.set(15, 14, 2, BOSS); r.set(8, 10, 2, MONSTER); r.set(22, 10, 2, MONSTER); r.set(11, 22, 4, CHEST); r.set(19, 22, 4, CHEST)
    write_mod(M, "allerheiligstes", r, "boss", conns, 6, dach=1, note="Boss im Tempelteil")

    write_def(D, """
# Burg mit Tempelteilen -- Baukasten Format 2 (Module mit Anschluessen), jedes Mal anders
format=2
name=Burg
aktiv=1
anteil=0.8
gebiet=ueberall
# Durchgang 7 breit, 10 hoch (Oger/Trolle passen), Wand 2 dick; Ersatzfarbe beim Zumauern
typ=gang,7,10,2,128,128,132
typ=aussen,7,10,2,110,106,100
aussen=aussen
# Module ohne Boss (zufaellig dazwischen), Mindest- und Hoechstgroesse der Grundflaeche
module=22,32
flaeche_min=110,110
flaeche_max=200,200
hoehe_max=28
versuche=24
zinnen=1,128,128,132
# Grundebene zuerst (module=), danach obere Ebenen nur ueber Treppenhaeuser, jedes Modul ganz auf einem Dach darunter
obere=6,16
# Wand/Boden/Decke einfarbig je Biom (eigenes Biom: Dungeon-Farben der .biom; sonst je nativer Klimaklasse)
biomfarben=1
farbe_wand=128,128,132
farbe_boden=98,94,90
farbe_decke=86,82,80
# Bewohner: Orks, Goblins, Oger, Skelette, Untote, Kull -- Boss: Kull-Haeuptling, Oger, Lich, Minotaurus
bewohner=11,12,4,5,46,17,15,16,Voelker\\kull-krieger,Voelker\\kull-berserker,Voelker\\kull-waechter
boss=Voelker\\kull-haeuptling,46,117,82
monster_anteil=85
level=+2
boss_level=+4
uneben=22
fundament=1
""")


# ===================================================================================================
# APFELHAIN -- Felder 64 x 64 mit Apfelbaum.cub, Hecken, Wege 8 breit; wenige Monster, Kisten
# ===================================================================================================
def apfelhain(root, apfel_path):
    D = os.path.join(root, "Apfelhain")
    M = os.path.join(D, "Module")
    TYPES.clear(); TYPES.update({"weg": (8, 5, 2), "aussen": (8, 5, 2)})
    T = 64
    tree = cropped(load_cub(apfel_path))                      # 41 x 40 x 51
    grass, hedge, wood, apple, hay = (96, 158, 62), (44, 104, 40), (122, 84, 50), (206, 36, 30), (214, 190, 96)
    cnt = [0]

    def field(conns, H=58, hedge_h=5):
        cnt[0] += 1
        s = cnt[0] * 313
        r = Room(T, T, H)
        r.box(0, 0, 0, T - 1, T - 1, 1, grass, 10, s)
        rr = random.Random(s)
        for _ in range(90):
            r.set(rr.randrange(3, T - 3), rr.randrange(3, T - 3), 1, rr.choice([(240, 220, 60), (240, 240, 240), (200, 90, 160), (120, 180, 70)]))
        for (x0, y0, x1, y1) in ((0, 0, T - 1, 1), (0, T - 2, T - 1, T - 1), (0, 0, 1, T - 1), (T - 2, 0, T - 1, T - 1)):
            r.box(x0, y0, 2, x1, y1, 1 + hedge_h, hedge, 10, s + x0 + y0)
        for (typ, d, u, z) in conns:
            opening(r, typ, d, u, z)
        # Wege (Erde) von den Oeffnungen zur Mitte
        dirt = (132, 104, 70)
        for (typ, d, u, z) in conns:
            if d in (N, S):
                ya, yb = (T // 2, T - 1) if d == N else (0, T // 2)
                r.box(u + 2, ya, 1, u + 5, yb, 1, dirt, 8, s + 9)
            else:
                xa, xb = (T // 2, T - 1) if d == E else (0, T // 2)
                r.box(xa, u + 2, 1, xb, u + 5, 1, dirt, 8, s + 9)
        return r

    def put_tree(r, t, cx, cy):
        r.paste(t, cx - t.sx // 2, cy - t.sy // 2, 2)

    def lantern(r, x, y):
        r.box(x, y, 2, x, y, 6, wood); r.set(x, y, 7, LIGHT)

    def C(typ, d):
        return (typ, d, 28, 2)

    small = cropped(scaled(tree, 0.5))
    mid = cropped(scaled(tree, 0.7))
    big = cropped(scaled(tree, 1.25))
    tree_r = rot90(tree)

    # Eingang: Holztor, zwei kleine Baeume, Laternen
    conns = [C("aussen", S), C("weg", N), C("weg", E), C("weg", W)]
    r = field(conns)
    r.box(27, 0, 2, 27, 1, 9, wood); r.box(36, 0, 2, 36, 1, 9, wood); r.box(27, 0, 9, 36, 1, 9, wood)
    put_tree(r, small, 14, 16); put_tree(r, small, 50, 16); put_tree(r, small, 14, 48); put_tree(r, small, 50, 48)
    lantern(r, 24, 6); lantern(r, 39, 6)
    write_mod(M, "eingang_tor", r, "eingang", conns, 10, note="Eingang, Tor nach Sueden")

    # Baumfeld: ein grosser Apfelbaum
    for i, (t, conns, gew, extra) in enumerate([
        (tree, [C("weg", S), C("weg", N), C("weg", E), C("weg", W)], 14, "monster"),
        (tree_r, [C("weg", S), C("weg", N), C("weg", E)], 10, "truhe"),
        (tree, [C("weg", S), C("weg", E)], 8, "bank"),
    ]):
        r = field(conns)
        put_tree(r, t, T // 2, T // 2)
        rr = random.Random(50 + i)
        for _ in range(14):                                    # Fallobst
            r.set(rr.randrange(12, 52), rr.randrange(12, 52), 2, apple)
        if extra == "monster":
            r.set(20, 44, 2, MONSTER)
        if extra == "truhe":
            r.set(44, 20, 2, CHEST); r.set(20, 20, 2, MONSTER)
        if extra == "bank":
            r.box(10, 50, 2, 16, 51, 2, wood); r.box(10, 52, 3, 16, 52, 4, wood); r.set(48, 48, 2, CHEST)
            r.box(52, 10, 2, 54, 12, 4, (222, 180, 70)); r.box(52, 10, 5, 54, 12, 5, wood)   # Bienenstock
        write_mod(M, "baumfeld_%d" % (i + 1), r, "raum", conns, gew, note="Apfelbaum (Apfelbaum.cub)")

    # Obstwiese: vier mittlere Baeume, Apfelkisten
    conns = [C("weg", S), C("weg", N), C("weg", E), C("weg", W)]
    r = field(conns)
    for (x, y) in ((16, 16), (48, 16), (16, 48), (48, 48)):
        put_tree(r, mid, x, y)
    for (x, y) in ((26, 40), (37, 23)):
        r.box(x, y, 2, x + 2, y + 2, 3, wood); r.box(x, y, 4, x + 2, y + 2, 4, apple)
    r.set(32, 44, 2, CHEST); r.set(40, 36, 2, MONSTER)
    write_mod(M, "obstwiese", r, "raum", conns, 9)

    # Brunnenplatz
    conns = [C("weg", S), C("weg", E), C("weg", W)]
    r = field(conns)
    c = T // 2
    r.box(c - 4, c - 4, 2, c + 4, c + 4, 4, (150, 150, 150)); r.carve(c - 3, c - 3, 2, c + 3, c + 3, 4)
    r.box(c - 3, c - 3, 1, c + 3, c + 3, 1, (60, 110, 200))
    r.box(c - 4, c - 4, 5, c - 4, c - 4, 10, wood); r.box(c + 4, c + 4, 5, c + 4, c + 4, 10, wood); r.box(c - 4, c - 4, 10, c + 4, c + 4, 10, (160, 70, 50))
    put_tree(r, mid, 14, 50); put_tree(r, mid, 50, 50)
    lantern(r, c - 7, c - 7); lantern(r, c + 7, c - 7)
    r.set(c, c + 8, 2, CHEST)
    write_mod(M, "brunnenplatz", r, "raum", conns, 7, maxn=1)

    # Scheune (Sackgasse) mit Kisten
    conns = [C("weg", S)]
    r = field(conns)
    r.box(16, 24, 2, 47, 49, 14, (140, 60, 40), 10, 7); r.carve(18, 26, 2, 45, 47, 13)
    r.carve(28, 24, 2, 35, 25, 9)                              # Scheunentor nach Sueden
    r.box(16, 24, 15, 47, 49, 15, (110, 70, 50)); r.box(20, 28, 16, 43, 45, 16, (110, 70, 50))
    for (x, y) in ((20, 40), (40, 42), (22, 30)):
        r.box(x, y, 2, x + 3, y + 3, 4, hay)
    r.set(30, 44, 2, CHEST); r.set(36, 44, 2, CHEST); r.set(32, 34, 2, MONSTER)
    r.set(18, 36, 9, LIGHT); r.set(45, 36, 9, LIGHT)
    put_tree(r, small, 8, 8); put_tree(r, small, 56, 8)
    write_mod(M, "scheune", r, "raum", conns, 5, maxn=1)

    # Boss: der alte Apfelbaum (1,25-fach)
    conns = [C("weg", S)]
    r = field(conns, H=big.sz + 4)
    put_tree(r, big, T // 2, T // 2 + 2)
    r.set(32, 14, 2, BOSS); r.set(14, 50, 2, CHEST); r.set(50, 50, 2, CHEST)
    lantern(r, 24, 10); lantern(r, 40, 10)
    write_mod(M, "alter_apfelbaum", r, "boss", conns, 10)

    write_def(D, """
# Apfelhain -- Felder 64 x 64 (Apfelbaum.cub), Heckenwege, wenige Monster, Kisten
format=2
name=Apfelhain
aktiv=1
anteil=1.5
gebiet=ueberall
boden=4
typ=weg,8,5,2,44,104,40
typ=aussen,8,5,2,44,104,40
aussen=aussen
module=6,8
flaeche_min=128,128
flaeche_max=236,236
hoehe_max=0
versuche=24
# Bewohner: Pflanzenwesen, Rettich, Zwiebelchen, Hornisse -- Boss: Troll
bewohner=69,70,71,53
boss=108
monster_anteil=50
level=+0
boss_level=+2
uneben=14
fundament=1
""")


def disable_old(root):
    p = os.path.join(root, "Tempel", "dungeon.txt")
    if os.path.exists(p):
        t = open(p, encoding="utf-8").read()
        t = "\n".join(("aktiv=0" if l.startswith("aktiv=") else l) for l in t.splitlines())
        open(p, "w", encoding="utf-8").write("# steckt jetzt in der Burg (Tempelteile) -- abgeschaltet\n" + t + "\n")


# ===================================================================================================
# Pruefen / Vorschau: derselbe Generator wie Dungeons.cpp (Zufall anders, Regeln gleich)
# ===================================================================================================
def read_lines(p):
    out = []
    for l in open(p, encoding="utf-8"):
        l = l.strip()
        if l and not l.startswith("#") and "=" in l:
            k, v = l.split("=", 1); out.append((k.strip(), v.strip()))
    return out


class Def:
    pass


def load_def(d):
    df = Def(); df.types = []; kv = read_lines(os.path.join(d, "dungeon.txt"))
    g = dict(kv)
    df.minM, df.maxM = map(int, g["module"].split(","))
    df.minW, df.minD = map(int, g["flaeche_min"].split(","))
    df.maxW, df.maxD = map(int, g["flaeche_max"].split(","))
    df.maxZ = int(g.get("hoehe_max", 28)); df.tries = int(g.get("versuche", 16)); df.uneven = int(g.get("uneben", 12))
    df.upMin, df.upMax = map(int, g.get("obere", "0,0").split(","))
    for k, v in kv:
        if k == "typ":
            q = v.split(","); df.types.append((q[0], int(q[1]), int(q[2]), int(q[3])))
    names = [t[0] for t in df.types]
    df.outside = names.index(g.get("aussen", "aussen"))
    df.mods = []
    md = os.path.join(d, "Module")
    for f in sorted(os.listdir(md)):
        if not f.endswith(".cub"):
            continue
        r = load_cub(os.path.join(md, f)); m = {"name": f[:-4], "s": r, "conns": [], "kind": "raum", "w": 10, "max": 99, "level": 0, "min": 0, "roof": False}
        for k, v in read_lines(os.path.join(md, f[:-4] + ".txt")):
            if k == "art": m["kind"] = v
            if k == "gewicht": m["w"] = int(v)
            if k == "max": m["max"] = int(v)
            if k == "ebene": m["level"] = 1 if v == "boden" else 2 if v == "oben" else 0
            if k == "pflicht": m["min"] = int(v)
            if k == "dach": m["roof"] = int(v) != 0
            if k == "oben": m["above_name"] = v
            if k == "anschluss":
                a = v.split(","); m["conns"].append((names.index(a[0]), DIRS[a[1]], int(a[2]), int(a[3])))
        df.mods.append(m)
    names_m = [m["name"] for m in df.mods]
    for m in df.mods:
        m["above"] = names_m.index(m["above_name"]) if m.get("above_name") in names_m else -1
    return df


SUPPORT = 0.7


def rot_conn(df, m, c, rot):
    sx, sy = m["s"].sx, m["s"].sy
    typ, d, u, z = c
    w = df.types[typ][1]; c2 = 2 * u + w
    X, Y = {0: (c2, 2 * sy), 2: (c2, 0), 1: (2 * sx, c2), 3: (0, c2)}[d]
    for _ in range(rot):
        X, Y = 2 * sy - Y, X; sx, sy = sy, sx; d = (d + 3) & 3
    return (typ, d, X, Y, z), sx, sy


def overlap(a, b):
    return all(a[i] < b[i + 3] and b[i] < a[i + 3] for i in range(3))


def grow(df, rnd):
    L = {"mods": [], "conns": [], "keep": [], "bb": None, "boss": -1, "phase": 0, "upper": 0}
    count = [0] * len(df.mods)

    def place(mi, rot, ci, at):
        m = df.mods[mi]
        rc, Wd, Dp = rot_conn(df, m, m["conns"][ci], rot)
        ox = oy = oz = 0; depth = 0
        if at is not None:
            t = L["conns"][at]
            if rc[0] != t["type"] or rc[1] != (t["dir"] + 2) & 3:
                return False
            dx, dy = t["x2"] - rc[2], t["y2"] - rc[3]
            if dx & 1 or dy & 1:
                return False
            ox, oy, oz = dx // 2, dy // 2, t["z"] - rc[4]; depth = L["mods"][t["pm"]]["depth"] + 1
        if oz < 0 or oz > df.maxZ:
            return False
        if (m["level"] == 1 and oz != 0) or (m["level"] == 2 and oz == 0) or (L["phase"] == 0 and oz != 0):
            return False
        b = (ox, oy, oz, ox + Wd, oy + Dp, oz + m["s"].sz)
        for p in L["mods"]:
            if overlap(b, p["box"]): return False
        for k in L["keep"]:
            if overlap(b, k): return False
        if oz > 0:                                                # mind. SUPPORT der Grundflaeche auf Daechern (Oberkante = oz)
            tops = [p["box"] for p in L["mods"] if p["box"][5] == oz and df.mods[p["mod"]]["roof"]]
            ys = list(range(b[1], b[4], 2)) + [b[4] - 1]; xs = list(range(b[0], b[3], 2)) + [b[3] - 1]
            hit = sum(1 for y in ys for x in xs if any(q[0] <= x < q[3] and q[1] <= y < q[4] for q in tops))
            if hit < SUPPORT * len(ys) * len(xs):
                return False
        bb = L["bb"] or (b[0], b[1], b[3], b[4])
        nb = (min(bb[0], b[0]), min(bb[1], b[1]), max(bb[2], b[3]), max(bb[3], b[4]))
        if nb[2] - nb[0] > df.maxW or nb[3] - nb[1] > df.maxD:
            return False
        pm = len(L["mods"])
        L["mods"].append({"mod": mi, "rot": rot, "ox": ox, "oy": oy, "oz": oz, "W": Wd, "D": Dp, "box": b, "depth": depth})
        if oz > 0:
            L["upper"] += 1
        L["bb"] = nb
        if at is not None:
            L["conns"][at]["state"] = 1
        for k, c in enumerate(m["conns"]):
            r2, _, _ = rot_conn(df, m, c, rot)
            o = {"pm": pm, "ci": k, "type": r2[0], "dir": r2[1], "x2": r2[2] + 2 * ox, "y2": r2[3] + 2 * oy, "z": r2[4] + oz, "state": 0}
            if k == ci and at is not None:
                o["state"] = 1
            else:
                for e in L["conns"]:
                    if e["state"] == 0 and e["type"] == o["type"] and e["dir"] == (o["dir"] + 2) & 3 and (e["x2"], e["y2"], e["z"]) == (o["x2"], o["y2"], o["z"]):
                        e["state"] = 1; o["state"] = 1; break
            L["conns"].append(o)
        return True

    def attach(at, kind):
        t = L["conns"][at]; cs = []
        for mi, m in enumerate(df.mods):
            if m["kind"] != kind or count[mi] >= m["max"]:
                continue
            for rot in range(4):
                for ci, c in enumerate(m["conns"]):
                    if c[0] != t["type"]:
                        continue
                    rc, _, _ = rot_conn(df, m, c, rot)
                    if rc[1] == (t["dir"] + 2) & 3:
                        cs.append((mi, rot, ci, m["w"]))
        for _ in range(14):
            if not cs:
                break
            c = rnd.choices(cs, [x[3] for x in cs])[0]; cs.remove(c)
            if place(c[0], c[1], c[2], at):
                count[c[0]] += 1; return True
        return False

    es = [i for i, m in enumerate(df.mods) if m["kind"] == "eingang"]
    e = rnd.choices(es, [df.mods[i]["w"] for i in es])[0]
    place(e, rnd.randrange(4), 0, None); count[e] += 1
    for o in L["conns"]:
        if o["type"] == df.outside:
            o["state"] = 2
            dep, w = df.uneven + 8, df.types[o["type"]][1] + 6
            cx, cy = o["x2"] // 2, o["y2"] // 2
            k = {0: (cx - w // 2, cy, 0, cx + w // 2 + 1, cy + dep, 999), 2: (cx - w // 2, cy - dep, 0, cx + w // 2 + 1, cy, 999),
                 1: (cx, cy - w // 2, 0, cx + dep, cy + w // 2 + 1, 999), 3: (cx - dep, cy - w // 2, 0, cx, cy + w // 2 + 1, 999)}[o["dir"]]
            L["keep"].append(k); bb = L["bb"]
            L["bb"] = (min(bb[0], k[0]), min(bb[1], k[1]), max(bb[2], k[3]), max(bb[3], k[4]))
    def ground(c):
        pm = L["mods"][c["pm"]]
        return pm["oz"] == 0 and c["z"] == 2
    target = df.minM + rnd.randrange(df.maxM - df.minM + 1)
    fails = 0
    while len(L["mods"]) < target and fails < 400:
        op = [i for i, c in enumerate(L["conns"]) if c["state"] == 0 and ground(c)]
        if not op:
            break
        at = rnd.choice(op)
        if not attach(at, "raum"):
            L["conns"][at]["state"] = 3; fails += 1
    for mi, m in enumerate(df.mods):                              # Pflicht-Module (Treppenhaeuser)
        guard = 0
        while count[mi] < m["min"] and guard < 60:
            guard += 1
            c = [i for i, cc in enumerate(L["conns"]) if cc["state"] in (0, 3) and ground(cc)]
            if not c:
                break
            at = rnd.choice(c); ok = False
            for rot in range(4):
                for ci, cn in enumerate(m["conns"]):
                    if ok or cn[0] != L["conns"][at]["type"]:
                        continue
                    st = L["conns"][at]["state"]; L["conns"][at]["state"] = 0
                    ok = place(mi, rot, ci, at)
                    if not ok:
                        L["conns"][at]["state"] = st
            if ok:
                count[mi] += 1
    def stack_on(g, mi):
        gp = L["mods"][g]; m = df.mods[mi]; oz = gp["oz"] + gp["box"][5] - gp["box"][2]
        if oz > df.maxZ or m["s"].sx != df.mods[gp["mod"]]["s"].sx or m["s"].sy != df.mods[gp["mod"]]["s"].sy:
            return False
        b = (gp["ox"], gp["oy"], oz, gp["ox"] + gp["W"], gp["oy"] + gp["D"], oz + m["s"].sz)
        if any(overlap(b, p["box"]) for p in L["mods"]):
            return False
        pm = len(L["mods"])
        L["mods"].append({"mod": mi, "rot": gp["rot"], "ox": gp["ox"], "oy": gp["oy"], "oz": oz, "W": gp["W"], "D": gp["D"], "box": b, "depth": gp["depth"] + 1})
        L["upper"] += 1
        for k, c in enumerate(m["conns"]):
            r2, _, _ = rot_conn(df, m, c, gp["rot"])
            o = {"pm": pm, "ci": k, "type": r2[0], "dir": r2[1], "x2": r2[2] + 2 * gp["ox"], "y2": r2[3] + 2 * gp["oy"], "z": r2[4] + oz, "state": 0}
            for e in L["conns"]:
                if e["state"] in (0, 3) and e["type"] == o["type"] and e["dir"] == (o["dir"] + 2) & 3 and (e["x2"], e["y2"], e["z"]) == (o["x2"], o["y2"], o["z"]):
                    e["state"] = 1; o["state"] = 1; break
            L["conns"].append(o)
        return True

    def stackable(i, h):
        p = L["mods"][i]; m = df.mods[p["mod"]]
        return m["kind"] == "raum" and m["roof"] and m["level"] != 1 and p["box"][5] - p["box"][2] == h

    if df.upMax > 0:
        L["phase"] = 1
        up = df.upMin + rnd.randrange(df.upMax - df.upMin + 1)
        for lvl in range(4):
            if L["upper"] >= up:
                break
            stairs = [i for i, p in enumerate(L["mods"]) if df.mods[p["mod"]]["above"] >= 0 and p["oz"] == lvl * 14]
            for si, st in enumerate(stairs):
                if L["upper"] >= up:
                    break
                h = L["mods"][st]["box"][5] - L["mods"][st]["box"][2]
                if not stack_on(st, df.mods[L["mods"][st]["mod"]]["above"]):
                    continue
                q = [st]; seen = {st}; done = 1
                per = max(2, (up - L["upper"]) // max(1, len(stairs) - si) + 1)
                hi = 0
                while hi < len(q) and done < per and L["upper"] < up:
                    cur = q[hi]; hi += 1
                    nb = []
                    for c in L["conns"]:
                        if c["pm"] != cur or c["state"] != 1:
                            continue
                        for e in L["conns"]:
                            if e["pm"] != cur and e["state"] == 1 and (e["x2"], e["y2"], e["z"]) == (c["x2"], c["y2"], c["z"]) and e["dir"] == (c["dir"] + 2) & 3:
                                nb.append(e["pm"])
                    rnd.shuffle(nb)
                    for n in nb:
                        if n in seen or L["mods"][n]["oz"] != L["mods"][st]["oz"] or not stackable(n, h):
                            continue
                        seen.add(n)
                        mi = df.mods[L["mods"][n]["mod"]]["above"]
                        mi = mi if mi >= 0 else L["mods"][n]["mod"]
                        if stack_on(n, mi):
                            q.append(n); done += 1
                            if done >= per or L["upper"] >= up:
                                break
    L["phase"] = 1
    cand = [i for i, c in enumerate(L["conns"]) if c["state"] in (0, 3)]
    cand.sort(key=lambda i: -L["mods"][L["conns"][i]["pm"]]["depth"])
    for at in cand:
        st = L["conns"][at]["state"]; L["conns"][at]["state"] = 0
        if attach(at, "boss"):
            L["boss"] = len(L["mods"]) - 1; break
        L["conns"][at]["state"] = st
    return L


def generate(df, seed):
    best = None
    for a in range(df.tries):
        L = grow(df, random.Random(seed * 1000 + a))
        bb = L["bb"]; Wd, Dp = bb[2] - bb[0], bb[3] - bb[1]; n = len(L["mods"]) - (1 if L["boss"] >= 0 else 0)
        big = max(Wd, Dp) >= max(df.minW, df.minD) and min(Wd, Dp) >= min(df.minW, df.minD)
        if L["boss"] >= 0 and n >= df.minM and big and L["upper"] >= df.upMin:
            return L, True, a + 1
        best = L
    return best, False, df.tries


def assemble(df, L):
    bb = L["bb"]; Wd, Dp = bb[2] - bb[0], bb[3] - bb[1]
    H = max(p["box"][5] for p in L["mods"]) + 2
    out = np.zeros((H, Dp, Wd, 3), np.uint8)
    for i, p in sorted(enumerate(L["mods"]), key=lambda t: t[1]["oz"]):
        m = df.mods[p["mod"]]; r = m["s"]
        for _ in range(p["rot"]):
            r = rot90(r)
        r = Room(r.sx, r.sy, r.sz) if False else r
        v = r.v.copy()
        for c in L["conns"]:
            if c["pm"] != i or c["state"] not in (0, 3):
                continue
            rc, _, _ = rot_conn(df, m, m["conns"][c["ci"]], p["rot"])
            _, w, h, t = df.types[rc[0]]
            ns = rc[1] in (0, 2); a0 = ((rc[2] if ns else rc[3]) - w) // 2
            for z in range(rc[4], rc[4] + h):
                for dp in range(t):
                    dc = {0: r.sy - 1 - dp, 2: dp, 1: r.sx - 1 - dp, 3: dp}[rc[1]]
                    for k in range(w):
                        x, y = (a0 + k, dc) if ns else (dc, a0 + k)
                        if not v[z, y, x].any():
                            v[z, y, x] = (200, 60, 60)          # zugemauert (rot in der Vorschau)
        mk = np.zeros(v.shape[:3], bool)
        for col in (MONSTER, BOSS, CHEST, VASE):
            mk |= (v == col).all(3)
        v[mk] = (255, 0, 255)
        x0, y0, z0 = p["ox"] - bb[0], p["oy"] - bb[1], p["oz"]
        sub = out[z0:z0 + r.sz, y0:y0 + r.sy, x0:x0 + r.sx]
        solid = v.any(3)
        sub[solid] = v[solid]
    return out


def render(a, path, s=3):
    from PIL import Image, ImageDraw
    sz, sy, sx, _ = a.shape
    a = a[:, ::-1, ::-1, :]
    solid = a.any(3)
    pad = np.zeros((sz + 2, sy + 2, sx + 2), bool); pad[1:-1, 1:-1, 1:-1] = solid
    top = solid & ~pad[2:, 1:-1, 1:-1]; front = solid & ~pad[1:-1, 2:, 1:-1]; left = solid & ~pad[1:-1, 1:-1, 2:]
    vis = top | front | left
    zs, ys, xs = np.nonzero(vis)
    order = np.lexsort((zs, xs + ys))
    cx = 0.866 * s; Wd = int((sx + sy) * cx) + 20; H = int(sz * s + (sx + sy) * s * 0.5) + 20
    img = Image.new("RGB", (Wd, H), (30, 34, 44)); d = ImageDraw.Draw(img)
    ox = sy * cx + 10; oy = sz * s + 10
    P = lambda x, y, z: (ox + (x - y) * cx, oy - z * s + (x + y) * s * 0.5)
    for i in order:
        x, y, z = int(xs[i]), int(ys[i]), int(zs[i]); c = tuple(int(v) for v in a[z, y, x])
        if top[z, y, x]: d.polygon([P(x, y, z + 1), P(x + 1, y, z + 1), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=c)
        if front[z, y, x]: d.polygon([P(x, y + 1, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=tuple(int(v * 0.8) for v in c))
        if left[z, y, x]: d.polygon([P(x + 1, y, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x + 1, y, z + 1)], fill=tuple(int(v * 0.62) for v in c))
    img.save(path)


def check(root, n=300, preview=False):
    for name in ("Burg", "Bergtempel", "Apfelhain"):
        d = os.path.join(root, name); df = load_def(d)
        ok = 0; sizes = []; mods = []; tries = []; ups = []
        for seed in range(n):
            L, good, t = generate(df, seed)
            ok += good; tries.append(t)
            if good:
                bb = L["bb"]; sizes.append((bb[2] - bb[0], bb[3] - bb[1])); mods.append(len(L["mods"])); ups.append(L["upper"])
        print("%s: %d/%d Grundrisse erreichen die Mindestgroesse; Module %s..%s; Flaeche %s..%s; Versuche im Mittel %.1f; obere Module %s..%s"
              % (name, ok, n, min(mods) if mods else "-", max(mods) if mods else "-",
                 min(sizes) if sizes else "-", max(sizes) if sizes else "-", sum(tries) / len(tries),
                 min(ups) if ups else "-", max(ups) if ups else "-"))
        if preview:
            for seed in (1, 2):
                L, good, _ = generate(df, seed * 77)
                render(assemble(df, L), os.path.join(d, "vorschau_%d.png" % seed), 2 if name in ("Burg", "Bergtempel") else 1)


if __name__ == "__main__":
    root_s = sys.argv[1] if len(sys.argv) > 1 else "."
    root = os.path.join(root_s, "Dungeons")
    os.makedirs(root, exist_ok=True)
    burg(root)
    from bergtempel_bau import bergtempel          # Bergtempel (lage=berg), eigene Datei
    bergtempel(root)
    apfelhain(root, os.path.join(root_s, "Apfelbaum.cub"))
    disable_old(root)
    print("fertig:", root)
    if "--pruefen" in sys.argv or "--vorschau" in sys.argv:
        check(root, 300 if "--pruefen" in sys.argv else 20, "--vorschau" in sys.argv)
