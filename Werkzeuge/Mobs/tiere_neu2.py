# tiere_neu2.py (2026-10-16): die 26 ersten Tiere (Rassen 200..225, gleiche Reihenfolge) mit den neuen Bauern
# (tierbau2: Gelenke der Basisrasse, abgerundete Formen, Gesicht, Hals). Aufruf: python tiere_neu2.py [Strukturen]
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tierbau as T
from tierbau import V, dims, mix, H, HEAD, HAIR, HANDS, FEET, BODY, TAIL, SHOULDER, WINGS, quad_head, quad_body, quad_leg, tail
import tierwelt_bau as TB
from tierbau2 import wing_part, head_part
if len(sys.argv) > 1: T.OUT = sys.argv[1]
T.CFG.update({'race_base': 200, 'mob_dir': 'Tiere', 'part_dir': 'Tiere'})
FACT = {'frostdrache': 3.0, 'giftdrache': 3.0, 'golddrache': 3.0, 'nashorn': 1.5, 'elefant': 1.8, 'braunbaer': 1.5, 'eisbaer': 1.6, 'schwarzbaer': 1.4, 'luchs': 1.2, 'adler': 1.4}
def REG(name, disp, base, factor, parts, host, medium, hab, weight=10, grp=(1, 2), note='', extra=()):
    ex = list(extra)
    if medium == 'luft': ex.append('fliegt=1')
    T.mob(name, disp, base, FACT.get(name, factor), parts, host, note, ex)
TB.REG = REG
def OLDMOB(name, disp, base, factor, parts, host, note=''):
    T.mob(name, disp, base, factor, parts, host, note, [])
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
    OLDMOB('geist', 'Geist', 'UndeadMale', 1.2, {HEAD: h, BODY: b, HANDS: a}, 1, 'Basis Untoter (Skelett), ohne Fuesse, alles eigen')

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
    OLDMOB('schlangenmensch', 'Schlangenmensch', 'LizardmanMale', 1.1, {HEAD: h, BODY: b, TAIL: t, HANDS: 'base'}, 1, 'Basis Echsenmensch (Skelett), Schwanz statt Fuessen: Position im Mob-Labor nachjustieren')

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
    OLDMOB('zentaur', 'Zentaur', 'Horse', 1.15, {HEAD: h, BODY: b, FEET: f, TAIL: t}, 3, 'Basis Pferd (Skelett), Kopfteil = Oberkoerper: Hoehe im Mob-Labor nachjustieren')


TB.quad('nashorn', 'Nashorn', 'cow', (118, 116, 112), (96, 94, 92), 1.5, 1, [], ear='round', horn='uni', horn_c=(214, 202, 176), snout=3)
TB.quad('elefant', 'Elefant', 'mammoth', (128, 126, 130), (106, 104, 110), 1.8, 3, [], ear='big', trunk=True, tusks='down')
TB.quad('fuchs', 'Fuchs', 'collie', (214, 112, 46), (240, 232, 218), 0.8, 3, [], ear='point', ear_len=4, snout=5, tail_kind='bushy', tail_tip=(246, 244, 238), leg_c=(60, 44, 34))
TB.quad('polarfuchs', 'Polarfuchs', 'collie', (236, 238, 242), (250, 250, 252), 0.8, 3, [], ear='point', ear_len=3, snout=4, tail_kind='bushy', tail_tip=(250, 250, 252))
TB.quad('luchs', 'Luchs', 'cat', (196, 160, 112), (232, 214, 182), 1.2, 1, [], ear='tuft', spots=0.18, spot_c=(120, 86, 56), tail_tip=(40, 32, 28))
TB.quad('braunbaer', 'Braunbaer', 'wolf', (112, 76, 48), (150, 110, 80), 1.5, 1, [], ear='round', snout=3, snout_c=(170, 130, 94), tail_kind='none')
TB.quad('eisbaer', 'Eisbaer', 'wolf', (236, 234, 226), (220, 216, 206), 1.6, 1, [], ear='round', snout=3, snout_c=(210, 206, 196), tail_kind='none')
TB.quad('schwarzbaer', 'Schwarzbaer', 'wolf', (44, 40, 40), (60, 54, 54), 1.4, 1, [], ear='round', snout=3, snout_c=(150, 120, 96), tail_kind='none')
TB.quad('hirsch', 'Hirsch', 'pony', (150, 102, 62), (226, 206, 176), 1.1, 3, [], horn='antler', horn_c=(206, 186, 150), tail_kind='none')
TB.quad('hirschkuh', 'Hirschkuh', 'pony', (160, 110, 68), (230, 212, 184), 0.95, 3, [], spots=0.08, spot_c=(236, 228, 210), tail_kind='none')
TB.quad('pegasus', 'Pegasus', 'pony', (238, 238, 244), (226, 226, 232), 1.1, 3, [], mane='crest', tail_c=(196, 210, 240), paw_c=(232, 196, 90), wings=((238, 238, 244), (196, 210, 240)))
zentaur()
TB.beast('frostdrache', 'Frostdrache', (120, 180, 220), (220, 236, 246), (170, 210, 236), (236, 244, 250), (60, 120, 255), 1.4, 1, [])
TB.beast('giftdrache', 'Giftdrache', (84, 140, 50), (190, 210, 90), (120, 170, 60), (60, 40, 70), (230, 220, 40), 1.4, 1, [])
TB.beast('golddrache', 'Golddrache', (214, 166, 60), (246, 220, 140), (230, 190, 90), (140, 90, 40), (200, 40, 40), 1.4, 1, [])
TB.bird('adler', 'Adler', (96, 66, 42), (120, 86, 56), (240, 238, 232), (232, 190, 60), (88, 60, 40), 1.4, 3, [], bk='hook')
TB.bird('falke', 'Falke', (120, 96, 72), (226, 214, 190), (90, 80, 76), (60, 56, 50), (110, 90, 70), 1.0, 3, [], bk='hook')
TB.bird('kolibri', 'Kolibri', (40, 170, 120), (220, 230, 220), (40, 160, 110), (30, 26, 24), (60, 190, 140), 0.35, 3, [], bk='long')
TB.bird('schwalbe', 'Schwalbe', (34, 40, 80), (236, 230, 220), (34, 40, 80), (30, 26, 24), (30, 36, 72), 0.45, 3, [])
TB.insect('schmetterling', 'Schmetterling', 'Fly', (60, 50, 60), (60, 50, 60), (120, 170, 236), 0.6, 3, [])
TB.insect('monarchfalter', 'Monarchfalter', 'Fly', (40, 34, 30), (40, 34, 30), (236, 130, 30), 0.6, 3, [], pattern='veins')
TB.insect('gluehwuermchen', 'Gluehwuermchen', 'Fly', (70, 60, 40), (60, 50, 40), (200, 210, 190), 0.4, 3, [], glow=(220, 250, 120))
TB.insect('libelle', 'Libelle', 'Mosquito', (40, 150, 170), (40, 120, 160), (210, 230, 240), 0.8, 3, [], pattern='bands')
TB.insect('marienkaefer', 'Marienkaefer', 'Bumblebee', (206, 40, 40), (30, 28, 28), (200, 40, 40), 0.4, 3, [])
geist(); schlangenmensch()
# Pegasus: Fluegel nachtragen (Platz 7) -- die .mob neu schreiben ist einfacher: Fluegel als eigenes Teil speichern
names = [m[0] for m in T.MOBS]
print(len(T.MOBS), 'Tiere', T.MOBS[0][3], '..', T.MOBS[-1][3])
assert names == ['nashorn', 'elefant', 'fuchs', 'polarfuchs', 'luchs', 'braunbaer', 'eisbaer', 'schwarzbaer', 'hirsch', 'hirschkuh', 'pegasus',
                 'zentaur', 'frostdrache', 'giftdrache', 'golddrache', 'adler', 'falke', 'kolibri', 'schwalbe', 'schmetterling', 'monarchfalter',
                 'gluehwuermchen', 'libelle', 'marienkaefer', 'geist', 'schlangenmensch'], names
