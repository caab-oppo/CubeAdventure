# kopfbau.py (2026-10-27): kurze Koepfe mit aufrechtem Hals fuer Huftiere (Hirsch, Reh, Rentier, Elch, Giraffe ...) und
# Drachen/Greife. Ersetzt die aus dem nativen Pferde-/Drachenkopf gestreckten Gesichter (lange, nach unten haengende
# Schnauze). Raster: +y vorne, +z oben; der Halsansatz liegt hinten unten -- tierbild.ausrichten setzt ihn (hals=1)
# vorne oben in den Koerper.
import math
import numpy as np
from tierbau import mix
from tierbau2 import Part, ears_at, horns, BLACK, WHITE
from tierbau import H

def blank(sx, sy, sz, tag):
    p = Part.__new__(Part); p.tag = tag; p.pad = 0; p.sx, p.sy, p.sz = sx, sy, sz
    p.nx, p.ny, p.nz = sx, sy, sz; p.native = np.zeros((sz, sy, sx), bool)
    p.occ = np.zeros((sz, sy, sx), bool); p.col = np.zeros((sz, sy, sx, 3), np.float32)
    return p

def _pattern(p, tag, spots, spot_c, stripes, zmin):
    for z, y, x in zip(*np.nonzero(p.occ)):
        if spots and H(tag, 's', x // 2, y // 2, z // 2) < spots and z > zmin: p.col[z, y, x] = spot_c
        if stripes and (z // 2) % 2 == 0 and z > zmin: p.col[z, y, x] = spot_c

def huftier_kopf(tag, fur, belly, *, neck=1.0, snout_c=None, nose=(30, 26, 26), ear='point', ear_c=None, ear_len=3,
                 horn=None, horn_c=(220, 206, 176), mane=None, mane_c=None, spots=None, spot_c=None, stripes=False):
    Hn = int(round(9 * neck)); W, L = 16, 17; Hh = Hn + 17
    p = blank(W, L, Hh, tag); cx = W / 2
    sc = snout_c or mix(fur, belly, 0.5)
    zb, zt = 2.5, 2.5 + Hn                                   # Hals unten / oben
    sk = (cx, 8.6, zt + 3.4)                                 # Schaedelmitte
    # Hals: Ansatz hinten unten, schraeg nach vorn oben; Kehle hell
    p.cap((cx, 4.6, zb), (cx, 7.2, zt + 1), 3.0, 2.3, lambda x, y, z: belly if (y + .5) > 4.6 + (z - zb) * 0.25 + 1.2 else fur)
    # Schaedel (rund), Schnauze kurz und waagrecht, Unterkiefer etwas heller
    p.ell(sk[0], sk[1], sk[2], 3.3, 3.5, 3.0, lambda x, y, z: belly if z < sk[2] - 1.8 else fur, 2.4)
    p.ell(cx, 12.0, sk[2] - 1.0, 2.3, 2.5, 2.0, lambda x, y, z: sc if z < sk[2] - 0.6 else mix(fur, sc, 0.5), 2.2)
    tip = max(y for y in range(L) if p.occ[:, y].any())
    if spots or stripes: _pattern(p, tag, spots, spot_c, stripes, zb + 2)
    for dx in (-1, 0): p.put(cx + dx, tip, sk[2] - 0.6, nose)               # Nase
    for dx in (-1, 0): p.put(cx + dx, tip, sk[2] - 2.0, mix(sc, BLACK, 0.45))   # Maul
    ez = sk[2] + 0.8; ey = 9.6
    for sg in (-1, 1):                                                        # Augen seitlich
        ex = p.surface_x(ey, ez, sg)
        if ex is not None: p.put(ex, ey, ez, WHITE); p.put(ex, ey + 1, ez, (30, 24, 20))
    if mane == 'crest':
        mc = mane_c or mix(fur, BLACK, 0.6)
        for k in range(12):
            t = k / 11; y = 4.6 + (7.2 - 4.6) * t - 2.6 + t * 0.6; z = zb + (zt + 1 - zb) * t + 1.6
            p.put(cx - .5, y, z, mc); p.put(cx - .5, y, z + 1, mc)
    if horn: horns(p, horn, horn_c)
    if ear: ears_at(p, ear, ear_c or fur, mix(belly, (230, 170, 160), 0.4), ear_len, cx, 7.0, 6.6)
    p.shade()
    return p.to_v()

def drachen_kopf(tag, scale_c, belly_c, eye_c, *, beak_c=None, spine_c=None, feathers=False, head_c=None):
    hc = head_c or scale_c
    W, L, Hh = 16, 24, 20; p = blank(W, L, Hh, tag); cx = W / 2
    zb = 3.0; sk = (cx, 12.0, 12.0)
    p.cap((cx, 4.0, zb), (cx, 9.5, sk[2] - 1), 3.2, 2.8, lambda x, y, z: belly_c if z < zb + (y - 4) * 0.9 - 1.5 else scale_c)
    p.ell(sk[0], sk[1], sk[2], 3.8, 4.2, 3.4, lambda x, y, z: belly_c if z < sk[2] - 2.2 else hc, 2.4)
    if beak_c:                                                                # Greif/Phoenix: Hakenschnabel
        p.cap((cx, 15.0, sk[2] - 0.5), (cx, 19.0, sk[2] - 1.5), 1.8, 0.9, beak_c)
        p.cap((cx, 19.0, sk[2] - 1.5), (cx, 19.6, sk[2] - 3.2), 0.9, 0.6, beak_c)
    else:                                                                     # Drache: Oberkiefer + Unterkiefer, kurz
        p.ell(cx, 17.0, sk[2] - 0.6, 2.6, 3.6, 1.7, hc, 2.4)
        p.ell(cx, 16.2, sk[2] - 2.6, 2.1, 3.0, 1.0, belly_c, 2.4)
        tip = max(y for y in range(L) if p.occ[:, y].any())
        for dx in (-1, 0): p.put(cx + dx * 2 + .5, tip, sk[2] - 0.2, (30, 24, 24))          # Nuestern
        for y in range(13, tip - 1, 2):                                       # Zaehne
            for sg in (-1, 1): p.put(cx + sg * 1.6 - (1 if sg < 0 else 0), y, sk[2] - 2.0, (236, 230, 214))
    ez = sk[2] + 1.0; ey = 13.6
    for sg in (-1, 1):
        ex = p.surface_x(ey, ez, sg)
        if ex is not None: p.put(ex, ey, ez, eye_c); p.put(ex, ey - 1, ez, mix(eye_c, BLACK, 0.5))
    sc = spine_c or mix(scale_c, BLACK, 0.4)
    for sg in (-1, 1):                                                        # Hoerner nach hinten / Federohren
        if feathers: p.cap((cx + sg * 2, 10.5, sk[2] + 2.5), (cx + sg * 3, 7.0, sk[2] + 5.5), 0.9, 0.5, sc)
        else: p.cap((cx + sg * 2.4, 10.5, sk[2] + 2.4), (cx + sg * 3.6, 6.0, min(Hh - 1, sk[2] + 6.5)), 0.95, 0.4, sc)
    for k in range(5):                                                        # Nackenkamm
        t = k / 4; p.put(cx - .5, 4.0 + 5.5 * t - 3.0, zb + (sk[2] - 1 - zb) * t + 2.6, sc)
    p.shade()
    return p.to_v()
