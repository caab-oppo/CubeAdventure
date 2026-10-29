# Tierwelt (2026-10-14): 150 neue Tiere -- 50 Land, 50 Luft, 50 Wasser. Jedes Tier eine eigene Rasse (226..375).
# Bausteine aus tierbau.py (Grundformen, keine nativen Modelle). Kleine Tiere klein, grosse sehr gross (faktor).
#  * Land/Luft: Skelett einer nativen Basisrasse (Teile mit GENAU deren Massen, damit sie richtig sitzen).
#  * Wasser + Fantasie-Schweber: Basis Hai (Rasse 150) = EIN Koerperteil (Platz 4), Form frei, Groesse bis 64.
#    "schwimmt=1" -> der Mod haelt sie im Wasser (MobLab::SwimTick), "fliegt=1" -> in der Luft (FlightTick).
# Ausgabe: Strukturen\Mobs\Tierwelt\*.mob, Teile in Strukturen\Mobs\Teile\Tierwelt\, Liste _tierwelt.txt (Spawner).
# Aufruf (aus Projekt\Werkzeuge\Mobs):  python tierwelt.py [Strukturen-Ordner] [Vorschau-Ordner]
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tierbau as T
from tierbau import V, dims, mix, H, safe, quad_head, quad_body, quad_leg, tail, HEAD, HAIR, HANDS, FEET, BODY, TAIL, SHOULDER, WINGS

if len(sys.argv) > 1: T.OUT = sys.argv[1]
PREVIEW_DIR = sys.argv[2] if len(sys.argv) > 2 else None
T.CFG.update({'race_base': 226, 'mob_dir': 'Tierwelt', 'part_dir': 'Tierwelt'})
LIST = []          # (name, medium, lebensraeume, gewicht, gruppe_min, gruppe_max)

def reg(name, disp, base, factor, parts, host, medium, hab, weight=10, grp=(1, 2), note='', extra=()):
    factor = FACTOR.get(name, factor)
    ex = list(extra)
    if medium == 'luft' and not any(e.startswith('fliegt=') for e in ex): ex.append('fliegt=1')
    if medium == 'land' and not any(e.startswith('fliegt=') for e in ex): ex.append('fliegt=0')
    T.mob(name, disp, base, factor, parts, host, note, ex)
    LIST.append((name, medium, hab, weight, grp[0], grp[1]))

TB_REG_PLACEHOLDER = None
BLACK, WHITE, PINK = (28, 24, 22), (240, 238, 232), (226, 150, 150)

# ===================================================================================== LAND
SKEL = {   # Basisrasse, Teile (Platz -> nativer Name)
    'collie': ('Collie', {HEAD: 'collie-head', BODY: 'collie-body', FEET: 'collie-foot', HANDS: 'collie-hand', TAIL: 'collie-tail'}),
    'wolf': ('Wolf', {HEAD: 'wolf-head', BODY: 'wolf-body', FEET: 'wolf-foot', HANDS: 'wolf-hand', TAIL: 'wolf-tail'}),
    'cat': ('Cat', {HEAD: 'cat-head', BODY: 'cat-body', FEET: 'cat-foot', HANDS: 'cat-hand', TAIL: 'cat-tail'}),
    'panther': ('Panther', {HEAD: 'panther-head', BODY: 'panther-body', FEET: 'panther-foot', HANDS: 'panther-hand', TAIL: 'panther-tail'}),
    'pony': ('Horse', {HEAD: 'pony-head', BODY: 'pony-body', FEET: 'pony-foot', TAIL: 'pony-tail'}),
    'cow': ('Cow', {HEAD: 'cow-head', BODY: 'cow-body', FEET: 'cow-foot'}),
    'mammoth': ('Mammoth', {HEAD: 'mammoth-head', BODY: 'mammoth-body', FEET: 'mammoth-foot'}),
    'pig': ('Pig', {HEAD: 'pig-head', BODY: 'pig-body', FEET: 'pig-foot'}),
    'camel': ('Camel', {HEAD: 'camel-head', BODY: 'camel-body', FEET: 'camel-foot'}),
    'alpaca': ('Alpaca', {HEAD: 'alpaca-head', BODY: 'alpaca-body', FEET: 'alpaca-foot', HANDS: 'alpaca-hand'}),
    'sheep': ('Sheep', {HEAD: 'sheep-head', BODY: 'sheep-body', FEET: 'sheep-foot'}),
}

import tierwelt_bau as TB
from tierwelt_bau import quad, roundy, critter, reptile, ape, scorpion, strauss, bird, bat, insect, beast

# Groessen (2026-10-16): grosse Tiere gross, aber nicht riesig; kleine klein.
FACTOR = {'elch': 1.6, 'wisent': 1.5, 'giraffe': 1.8, 'mammut': 2.2, 'gorilla': 1.6, 'strauss': 2.0, 'loewe': 1.4, 'tiger': 1.5,
          'kamel': 1.4, 'yak': 1.5, 'moosgolem': 2.4, 'basilisk': 1.8, 'einhorn': 1.2, 'schattenwolf': 1.4, 'wolf_grau': 1.15,
          'kondor': 1.8, 'albatros': 1.6, 'phoenix': 1.6, 'greif': 1.8, 'wyvern': 3.2, 'federschlange': 1.8, 'himmelsrochen': 2.0,
          'pelikan': 1.4, 'storch': 1.2, 'kranich': 1.3, 'schwan': 1.3, 'seeadler': 1.5, 'geier': 1.3,
          'blauwal': 4.5, 'buckelwal': 3.8, 'walhai': 3.0, 'orca': 2.2, 'riesenkraken': 3.5, 'seeschlange': 2.8, 'manta': 2.2,
          'mondfisch': 1.8, 'weisser_hai': 2.0, 'hammerhai': 1.6, 'narwal': 1.8, 'walross': 1.8, 'seekuh': 1.8, 'krokodil': 1.6,
          'wels': 1.4, 'thunfisch': 1.2, 'schwertfisch': 1.4, 'meeresschildkroete': 1.3}

def build_land():
    F = (150, 110, 70)
    # Wald und Wiese
    quad('wildschwein', 'Wildschwein', 'pig', (92, 70, 54), (120, 96, 76), 1.3, 1, ['wald'], ear='point', snout=3, snout_c=(70, 56, 48), tusks='up', grp=(1, 3))
    critter('dachs', 'Dachs', 'mole', (120, 118, 116), (40, 36, 34), 0.8, 3, ['wald', 'wiese'], stripe=WHITE)
    roundy('igel', 'Igel', 'porcupine', (120, 96, 70), (200, 180, 150), 0.45, 3, ['wald', 'wiese'], spikes=(90, 70, 52), snout=(200, 180, 150))
    critter('eichhoernchen', 'Eichhoernchen', 'squirrel', (196, 96, 44), (236, 214, 190), 0.45, 3, ['wald'], ear='point', grp=(1, 2))
    roundy('hase', 'Hase', 'bunny', (166, 136, 104), (226, 214, 196), 0.6, 3, ['wiese', 'wald', 'savanne'], ears='long', tail_c=WHITE, grp=(1, 3))
    critter('waschbaer', 'Waschbaer', 'raccoon', (130, 126, 122), (190, 186, 180), 0.7, 3, ['wald', 'sumpf'], mask=BLACK)
    quad('wolf_grau', 'Wolf', 'wolf', (128, 128, 132), (200, 198, 196), 1.3, 1, ['wald', 'schnee'], ear='point', ear_len=4, snout=4, tail_kind='bushy', grp=(2, 4))
    quad('elch', 'Elch', 'pony', (86, 62, 44), (120, 96, 74), 2.2, 3, ['wald', 'schnee'], snout=4, snout_c=(70, 52, 40), horn='moose', horn_c=(196, 176, 140), tail_kind='none')
    quad('wisent', 'Wisent', 'cow', (84, 60, 42), (110, 84, 62), 2.0, 3, ['wiese', 'wald'], ear='round', snout=2, horn='bull', horn_c=(60, 52, 46), hump=2, grp=(1, 3))
    quad('reh', 'Reh', 'pony', (172, 120, 74), (236, 220, 196), 0.85, 3, ['wald', 'wiese'], snout=3, snout_c=(60, 46, 36), tail_kind='none', grp=(1, 3))
    # Savanne und Steppe
    quad('loewe', 'Loewe', 'panther', (210, 170, 100), (232, 210, 160), 1.6, 1, ['savanne'], ear='round', snout=2, mane='ring', tail_tip=(110, 70, 40))
    quad('zebra', 'Zebra', 'pony', WHITE, WHITE, 1.2, 3, ['savanne'], spot_c=BLACK, stripes=True, mane='crest', tail_c=BLACK, grp=(2, 5))
    quad('giraffe', 'Giraffe', 'alpaca', (226, 182, 96), (240, 222, 180), 2.6, 3, ['savanne'], spots=0.35, spot_c=(140, 86, 40), ear='point', horn='uni', horn_c=(120, 80, 50), weight=6)
    quad('gnu', 'Gnu', 'cow', (96, 90, 92), (130, 124, 124), 1.4, 3, ['savanne'], horn='bull', horn_c=(50, 46, 44), mane='crest', grp=(2, 5))
    quad('gepard', 'Gepard', 'cat', (226, 186, 110), (244, 230, 200), 1.5, 1, ['savanne'], spots=0.30, spot_c=BLACK, ear='round', tail_tip=BLACK)
    quad('hyaene', 'Hyaene', 'wolf', (170, 150, 110), (200, 186, 156), 1.1, 1, ['savanne', 'wueste'], spots=0.25, spot_c=(80, 66, 50), ear='round', mane='crest', grp=(2, 4))
    strauss()
    quad('warzenschwein', 'Warzenschwein', 'pig', (110, 96, 88), (130, 116, 106), 1.0, 3, ['savanne'], tusks='up', mane='crest', snout=3)
    quad('antilope', 'Antilope', 'pony', (196, 140, 80), WHITE, 0.95, 3, ['savanne', 'wiese'], horn='spiral', horn_c=(60, 50, 44), tail_kind='none', grp=(2, 5))
    critter('erdmaennchen', 'Erdmaennchen', 'squirrel', (200, 170, 120), (226, 206, 170), 0.4, 3, ['savanne', 'wueste'], ear='round', mask=(90, 70, 50), grp=(2, 5))
    # Wueste
    quad('kamel', 'Kamel', 'camel', (206, 170, 110), (226, 200, 150), 1.6, 3, ['wueste'], ear='round', snout=3, grp=(1, 3))
    quad('fennek', 'Fennek', 'collie', (236, 210, 160), (246, 236, 214), 0.5, 3, ['wueste'], ear='big', snout=4, tail_kind='bushy', tail_tip=(80, 60, 40))
    scorpion('skorpion', 'Skorpion', (60, 50, 40), 1.0, 1, ['wueste'], grp=(1, 3))
    roundy('guerteltier', 'Guerteltier', 'porcupine', (170, 150, 140), (210, 190, 170), 0.55, 3, ['wueste', 'savanne'], bands=True, ears='round', snout=(200, 170, 160))
    reptile('waran', 'Waran', 'croc', (110, 104, 80), (170, 160, 120), 1.4, 1, ['wueste', 'dschungel'], pattern='spots', pat_c=(200, 190, 140))
    reptile('klapperschlange', 'Klapperschlange', 'croc', (176, 150, 100), (220, 200, 160), 0.9, 1, ['wueste'], pattern='diamond', pat_c=(90, 70, 50), legs=False, grp=(1, 1))
    # Dschungel
    quad('tiger', 'Tiger', 'panther', (226, 130, 40), (246, 236, 220), 1.8, 1, ['dschungel'], stripes=True, spot_c=BLACK, ear='round', tail_tip=BLACK)
    quad('panther_schwarz', 'Schwarzer Panther', 'panther', (34, 32, 38), (50, 46, 54), 1.5, 1, ['dschungel'], ear='round', nose=(80, 70, 70))
    ape('gorilla', 'Gorilla', (44, 42, 46), (70, 64, 64), 2.2, 3, ['dschungel'], chest=(80, 76, 80))
    critter('faultier', 'Faultier', 'koala', (150, 134, 106), (190, 176, 150), 0.9, 3, ['dschungel'], ear='round', mask=(90, 72, 54))
    quad('tapir', 'Tapir', 'pig', (40, 38, 42), WHITE, 1.3, 3, ['dschungel', 'sumpf'], trunk=True, ear='round')
    reptile('chamaeleon', 'Chamaeleon', 'croc', (80, 190, 90), (200, 230, 120), 0.4, 3, ['dschungel'], pattern='bands', pat_c=(230, 160, 60), ridge=(60, 150, 70))
    reptile('pfeilgiftfrosch', 'Pfeilgiftfrosch', 'frog', (40, 90, 230), (30, 40, 90), 0.3, 1, ['dschungel'], pattern='spots', pat_c=BLACK, eye=BLACK, grp=(1, 3))
    quad('ameisenbaer', 'Ameisenbaer', 'pig', (120, 100, 80), (60, 50, 44), 1.3, 3, ['dschungel', 'savanne'], snout=6, snout_c=(110, 92, 74), ear='round', stripes=True, spot_c=BLACK)
    # Schnee und Gebirge
    quad('schneeleopard', 'Schneeleopard', 'panther', (226, 226, 220), (246, 246, 244), 1.5, 1, ['schnee', 'gebirge'], spots=0.28, spot_c=(80, 80, 86), ear='round', tail_kind='bushy')
    quad('steinbock', 'Steinbock', 'sheep', (140, 120, 96), (190, 176, 150), 1.2, 3, ['gebirge', 'schnee'], horn='curl', horn_c=(110, 96, 80), grp=(1, 4))
    quad('yak', 'Yak', 'cow', (52, 46, 44), (80, 72, 66), 1.9, 3, ['schnee', 'gebirge'], horn='bull', horn_c=(226, 214, 190), hump=1, grp=(1, 3))
    critter('murmeltier', 'Murmeltier', 'squirrel', (150, 116, 80), (196, 170, 130), 0.55, 3, ['gebirge', 'wiese'], ear='round', grp=(1, 3))
    quad('mammut', 'Mammut', 'mammoth', (110, 70, 44), (130, 90, 60), 3.2, 3, ['schnee'], trunk=False, tusks='down', ear='round', hump=1, weight=4)
    quad('rentier', 'Rentier', 'pony', (130, 110, 90), (224, 216, 204), 1.2, 3, ['schnee'], horn='antler', horn_c=(200, 184, 150), tail_kind='none', grp=(2, 5))
    # Sumpf
    reptile('riesenkroete', 'Riesenkroete', 'frog', (110, 100, 60), (190, 176, 120), 1.6, 3, ['sumpf'], pattern='spots', pat_c=(80, 70, 40))
    reptile('schnappschildkroete', 'Schnappschildkroete', 'turtle', (90, 96, 70), (170, 160, 120), 1.4, 1, ['sumpf'], shell=(80, 84, 60), ridge=(60, 64, 46))
    critter('sumpfratte', 'Sumpfratte', 'mole', (96, 82, 64), (140, 126, 106), 0.6, 1, ['sumpf'], ear='round', snout=3, grp=(2, 4))
    # Fantasie
    quad('einhorn', 'Einhorn', 'pony', (246, 244, 250), (236, 236, 246), 1.3, 3, ['wiese', 'wald', 'fantasie'], horn='uni', horn_c=(240, 210, 120), mane='crest', tail_c=(220, 200, 250), weight=2)
    reptile('basilisk', 'Basilisk', 'croc', (60, 110, 60), (170, 190, 90), 2.4, 1, ['sumpf', 'dschungel', 'fantasie'], pattern='bands', pat_c=(40, 70, 40), ridge=(200, 60, 40), eye=(250, 220, 40), weight=2)
    scorpion('kristallkaefer', 'Kristallkaefer', (90, 80, 140), 1.1, 3, ['gebirge', 'fantasie'], crystal=True, weight=4)
    roundy('pilzling', 'Pilzling', 'bunny', (226, 214, 190), (240, 232, 214), 0.6, 3, ['wald', 'sumpf', 'fantasie'], cap=(200, 50, 50), weight=4, grp=(2, 4))
    reptile('moosgolem', 'Moosgolem-Schildkroete', 'turtle', (110, 110, 100), (150, 146, 130), 3.4, 3, ['wald', 'sumpf', 'fantasie'], shell=(96, 100, 90), moss=True, weight=2)
    reptile('glutsalamander', 'Glutsalamander', 'croc', (60, 30, 26), (240, 120, 40), 1.1, 1, ['wueste', 'gebirge', 'fantasie'], pattern='spots', pat_c=(250, 170, 40), ridge=(250, 90, 30), eye=(255, 230, 120), weight=3)
    quad('schattenwolf', 'Schattenwolf', 'wolf', (40, 36, 54), (70, 64, 90), 1.7, 1, ['wald', 'schnee', 'fantasie'], ear='point', ear_len=4, snout=4, tail_kind='bushy', tail_tip=(120, 100, 200), nose=(140, 120, 255), weight=3, grp=(1, 3))

# ===================================================================================== LUFT
def solo(name, disp, v, factor, host, medium, hab, weight=10, grp=(1, 3), extra=(), note=''):
    ex = list(extra)
    if medium == 'wasser': ex.append('schwimmt=1'); ex.append('fliegt=0')
    reg(name, disp, 'Shark', factor, {BODY: v}, host, medium, hab, weight, grp, note or 'Basis Hai: ein Koerperteil, Form frei', ex)

def fishbody(name, L, W, Hh, top, belly, *, kind='fish', fin_c=None, pattern=None, pat_c=None, eye=BLACK, tail='fork',
             dorsal='small', lateral=None, head_c=None, extra=None):
    """Stromlinienkoerper, Kopf bei y max. kind: fish/shark/whale/dolphin/eel/ray/sunfish/puffer."""
    L, W, Hh = min(L, 64), min(W, 64), min(Hh, 64)
    v = V(W, L, Hh, name); cx, cz = W / 2, Hh / 2; fc = fin_c or mix(top, BLACK, 0.15)
    bodyL = L * (0.86 if tail != 'none' else 1.0); y0 = L - bodyL
    for y in range(int(y0), L):
        t = (y - y0) / bodyL
        if kind == 'eel': prof = 0.55 + 0.45 * min(1, t * 4) * min(1, (1 - t) * 6)
        elif kind == "puffer": prof = max(0.0, math.sin(math.pi * min(1, max(0.0, t)))) ** 0.5
        else: prof = max(0.0, math.sin(math.pi * min(1, max(0.02, t * 0.92 + 0.04)))) ** (0.6 if kind in ('whale', 'sunfish') else 0.8)
        rx = W * 0.46 * prof * (0.55 if kind in ('fish', 'eel') else 1.0); rz = Hh * 0.40 * prof * (1.15 if kind == 'sunfish' else 1.0)
        if kind == "ray": rx = W * 0.48 * max(0.0, math.sin(math.pi * min(1, max(0.0, t)))) ** 0.7; rz = max(1.0, Hh * 0.18 * prof)
        if rx < 0.5 or rz < 0.5: continue
        oz = (math.sin(y * 0.35) * 1.2 if kind == 'eel' else 0)
        for z in range(Hh):
            for x in range(W):
                d = ((x + .5 - cx) / rx) ** 2 + ((z + .5 - cz - oz) / rz) ** 2
                if d > 1: continue
                c = top if z + .5 > cz + (-0.25 * rz if kind in ('whale', 'dolphin', 'shark') else 0) else belly
                if head_c and t > 0.82: c = head_c if z + .5 > cz else c
                if pattern == 'bands' and int(t * 10) % 3 == 0 and z + .5 > cz - rz * 0.6: c = pat_c
                if pattern == 'spots' and H(name, x // 2, y // 2, z // 2) < 0.18 and z + .5 > cz: c = pat_c
                if pattern == 'dots' and H(name, x, y, z) < 0.06: c = pat_c
                if pattern == 'stripe' and abs(z + .5 - cz) < 1.2: c = pat_c
                if pattern == 'vstripes' and (y // 3) % 3 == 0: c = pat_c
                if pattern == 'grooves' and z + .5 < cz - rz * 0.3 and x % 2 == 0: c = mix(belly, BLACK, 0.15)
                v.put(x, y, z, c, 6)
    # Augen
    ey = int(L - bodyL * 0.10) - 1; ez = int(cz + Hh * 0.08)
    for x in range(W):
        for yy in (ey,):
            pass
    for sgn in (-1, 1):
        for x in (range(W) if sgn > 0 else range(W - 1, -1, -1)):
            pass
    for sgn in (-1, 1):
        xs = range(W - 1, -1, -1) if sgn > 0 else range(W)
        for x in xs:
            if v.o[min(Hh - 1, ez), ey, x]:
                v.put(x, ey, ez, eye, 0); v.put(x, ey - 1, ez, WHITE, 0); v.put(x, ey, ez + 1, WHITE, 0)      # Auge mit Weiss
                gy = ey - max(2, int(bodyL * 0.08))                                                            # Kiemenbogen
                for dz in (-1, 0, 1):
                    zz = min(Hh - 1, max(0, ez - 1 + dz))
                    xs2 = [xx for xx in (range(W - 1, -1, -1) if sgn > 0 else range(W)) if v.o[zz, gy, xx]]
                    if xs2 and kind not in ('whale', 'dolphin', 'ray'): v.a[zz, gy, xs2[0]] = safe([c * 0.7 for c in v.a[zz, gy, xs2[0]]])
                break
    # Maul: vorderste Spalte unterhalb der Mitte dunkel
    mz = int(cz - Hh * 0.08)
    for x in range(W):
        ys = [y for y in range(L - 1, -1, -1) if 0 <= mz < Hh and v.o[mz, y, x]]
        if ys and abs(x + .5 - cx) < W * 0.3: v.a[mz, ys[0], x] = safe([30, 22, 22])
    # Schwanzflosse
    if tail != 'none':
        for y in range(0, int(y0) + 2):
            s = 1 - y / max(1, y0 + 1)
            if tail == 'fork':
                for z in range(Hh):
                    dz = abs(z + .5 - cz)
                    if dz < Hh * 0.46 * (0.4 + s * 0.6) and not (dz < Hh * 0.18 * s and y < y0 * 0.6): v.put(cx - .5, y, z, fc, 4)
            elif tail == 'shark':
                for z in range(int(cz - Hh * 0.2 * s), int(cz + Hh * 0.48 * (0.3 + s)) + 1): v.put(cx - .5, y, z, fc, 4)
            elif tail == 'fluke':                                                   # waagrecht (Wal/Delfin/Robbe)
                for x in range(W):
                    if abs(x + .5 - cx) < W * 0.48 * (0.3 + s * 0.7): v.put(x, y, cz - 0.5, fc, 4)
            elif tail == 'round':
                for z in range(Hh):
                    if abs(z + .5 - cz) < Hh * 0.30 * (0.6 + 0.4 * math.sin(math.pi * y / max(1, y0 + 1))): v.put(cx - .5, y, z, fc, 4)
            elif tail == 'whip':
                v.put(cx - .5, y, cz - .5, fc, 3)
    # Rueckenflosse
    if dorsal != 'none':
        dy0, dy1 = int(y0 + bodyL * 0.35), int(y0 + bodyL * (0.62 if dorsal != 'long' else 0.85))
        for y in range(dy0, dy1):
            tt = (y - dy0) / max(1, dy1 - dy0)
            hgt = {'small': Hh * 0.18, 'tall': Hh * 0.42, 'curved': Hh * 0.30, 'long': Hh * 0.16, 'sail': Hh * 0.45}[dorsal] * (math.sin(math.pi * tt) if dorsal in ('small', 'long', 'sail') else tt)
            top_z = max([z for z in range(Hh) if v.o[z, y, int(cx)]], default=int(cz))
            for k in range(1, int(hgt) + 1): v.put(cx - .5, y - (k * 0.4 if dorsal == 'curved' else 0), top_z + k, fc, 4)
    # Brustflossen
    py = int(y0 + bodyL * 0.66)
    for sgn in (-1, 1):
        for k in range(1, max(2, int(W * (0.25 if kind != 'whale' else 0.45)))):
            v.put(cx + sgn * (W * 0.20 + k) - .5, py - k * 0.5, cz - Hh * 0.18 - k * 0.3, fc, 4, over=False)
    if lateral: [v.put(x, y, cz, lateral, 2, over=True) for y in range(int(y0), L) for x in range(W) if v.o[int(cz), y, x] and (x == 0 or not v.o[int(cz), y, x - 1] or x == W - 1 or not v.o[int(cz), y, x + 1])]
    if extra: extra(v)
    return v

# --------------------------------------------------------------------------- Luft: 50
def build_air():
    # Wald und Land
    bird('eule', 'Eule', (150, 120, 90), (220, 200, 170), (160, 130, 100), (60, 50, 40), (140, 110, 80), 0.8, 3, ['wald'], skel='owl')
    bird('uhu', 'Uhu', (120, 90, 60), (200, 170, 130), (130, 100, 70), (50, 40, 30), (110, 84, 56), 1.3, 1, ['wald', 'gebirge'], skel='owl', crest=(100, 76, 50))
    bird('rabe', 'Rabe', (30, 30, 40), (40, 40, 52), (30, 30, 40), (24, 24, 30), (34, 34, 46), 1.1, 1, ['wald', 'gebirge', 'schnee'], bk='hook', legs=BLACK)
    bird('kraehe', 'Kraehe', (44, 44, 50), (60, 60, 66), (44, 44, 50), (30, 30, 34), (40, 40, 46), 0.8, 3, ['wiese', 'wald'], legs=BLACK, grp=(2, 5))
    bird('elster', 'Elster', (24, 28, 44), WHITE, (24, 28, 44), (24, 24, 30), (40, 60, 120), 0.75, 3, ['wiese', 'wald'], wing_tip=WHITE, tail_c=(30, 50, 100), legs=BLACK)
    bird('taube', 'Taube', (150, 150, 166), (176, 170, 186), (120, 130, 150), (220, 200, 200), (140, 140, 156), 0.6, 3, ['wiese', 'wald'], legs=(220, 110, 110), grp=(2, 6))
    bird('spatz', 'Spatz', (150, 110, 70), (210, 196, 176), (120, 100, 90), (60, 50, 40), (140, 100, 60), 0.35, 3, ['wiese', 'wald'], grp=(3, 6))
    bird('rotkehlchen', 'Rotkehlchen', (130, 110, 80), (230, 110, 50), (130, 110, 80), (40, 34, 30), (120, 100, 74), 0.35, 3, ['wald', 'wiese'], grp=(1, 3))
    bird('specht', 'Specht', BLACK, WHITE, (210, 40, 40), (60, 60, 60), (40, 40, 40), 0.55, 3, ['wald'], bk='long', wing_tip=WHITE)
    bird('eisvogel', 'Eisvogel', (40, 140, 220), (230, 130, 50), (40, 120, 210), BLACK, (30, 160, 230), 0.4, 3, ['wald', 'sumpf'], bk='long')
    # Wasser und Kueste
    bird('storch', 'Storch', WHITE, WHITE, WHITE, (230, 60, 40), WHITE, 1.4, 3, ['wiese', 'sumpf'], bk='long', wing_tip=BLACK, legs=(230, 70, 50))
    bird('kranich', 'Kranich', (170, 170, 176), (190, 190, 196), (60, 60, 64), (80, 80, 80), (160, 160, 168), 1.5, 3, ['sumpf', 'wiese'], bk='long', crest=(220, 40, 40), legs=BLACK, grp=(2, 4))
    bird('reiher', 'Reiher', (150, 156, 170), WHITE, WHITE, (230, 200, 60), (140, 146, 160), 1.3, 3, ['sumpf', 'kueste'], bk='long', crest=BLACK, legs=(200, 170, 60))
    bird('pelikan', 'Pelikan', WHITE, WHITE, WHITE, (240, 190, 90), (230, 230, 230), 1.7, 3, ['kueste'], skel='seagull', bk='pouch', wing_tip=BLACK, grp=(1, 3))
    bird('moewe', 'Moewe', WHITE, WHITE, WHITE, (240, 200, 60), (190, 196, 206), 0.8, 3, ['kueste'], skel='seagull', wing_tip=BLACK, grp=(2, 5))
    bird('albatros', 'Albatros', WHITE, WHITE, WHITE, (240, 200, 160), (60, 60, 66), 2.2, 3, ['kueste'], skel='seagull', bk='hook', weight=4)
    bird('kormoran', 'Kormoran', (30, 34, 34), (40, 44, 40), (30, 34, 34), (90, 80, 60), (34, 40, 40), 1.0, 3, ['kueste', 'sumpf'], skel='seagull', bk='hook', legs=BLACK)
    bird('schwan', 'Schwan', WHITE, WHITE, WHITE, (240, 120, 40), (240, 240, 240), 1.6, 3, ['sumpf', 'wiese'], skel='seagull', bk='flat', legs=BLACK)
    bird('wildgans', 'Wildgans', (130, 120, 110), (200, 196, 190), (50, 48, 46), BLACK, (120, 110, 100), 1.1, 3, ['sumpf', 'wiese'], skel='seagull', bk='flat', legs=BLACK, grp=(3, 6))
    bird('wildente', 'Wildente', (130, 110, 90), (170, 150, 130), (40, 120, 70), (230, 200, 60), (120, 100, 80), 0.6, 3, ['sumpf', 'wiese'], skel='seagull', bk='flat', legs=(230, 140, 40), grp=(2, 5))
    # Greifvoegel
    bird('geier', 'Geier', (60, 50, 44), (90, 76, 66), PINK, (200, 190, 170), (54, 44, 40), 1.6, 1, ['wueste', 'savanne'], bk='hook', grp=(1, 3))
    bird('kondor', 'Kondor', (30, 30, 30), WHITE, (200, 110, 100), (220, 210, 190), (24, 24, 24), 2.6, 1, ['gebirge'], bk='hook', wing_tip=WHITE, weight=4)
    bird('bussard', 'Bussard', (120, 90, 60), (210, 190, 160), (120, 96, 70), (230, 200, 60), (110, 82, 56), 1.2, 3, ['wiese', 'wald'], bk='hook')
    bird('milan', 'Milan', (170, 90, 50), (200, 140, 100), (210, 200, 196), (230, 200, 60), (150, 80, 40), 1.2, 3, ['wiese'], bk='hook', tail_c=(190, 100, 50))
    # Tropen
    bird('ara', 'Ara', (220, 40, 40), (230, 200, 40), (220, 40, 40), (240, 236, 230), (40, 90, 220), 0.9, 3, ['dschungel'], skel='parrot', bk='hook', tail_c=(40, 90, 220))
    bird('tukan', 'Tukan', BLACK, (250, 240, 200), BLACK, (250, 160, 30), BLACK, 0.7, 3, ['dschungel'], skel='parrot', bk='toucan')
    bird('kakadu', 'Kakadu', WHITE, WHITE, WHITE, (60, 60, 64), (236, 236, 236), 0.7, 3, ['dschungel', 'savanne'], skel='parrot', bk='hook', crest=(250, 220, 60), grp=(2, 4))
    bird('wellensittich', 'Wellensittich', (110, 210, 80), (130, 220, 90), (240, 230, 90), (240, 200, 160), (90, 170, 70), 0.3, 3, ['savanne', 'dschungel'], skel='parrot', grp=(3, 6))
    bird('flamingo', 'Flamingo', (240, 140, 160), (250, 170, 186), (240, 140, 160), (240, 220, 210), (230, 110, 130), 1.4, 3, ['sumpf', 'kueste'], bk='curved', wing_tip=BLACK, legs=(230, 120, 140), grp=(3, 6))
    # Nacht
    bat('fledermaus', 'Fledermaus', (60, 50, 50), (70, 60, 64), 0.5, 1, ['wald', 'gebirge', 'dschungel'], grp=(2, 5))
    bat('flughund', 'Flughund', (120, 80, 50), (70, 54, 46), 1.1, 3, ['dschungel'], ears=(2, 1))
    insect('motte', 'Motte', 'Fly', (160, 150, 130), (150, 140, 120), (200, 190, 170), 0.6, 3, ['wald', 'wiese'], pattern='veins')
    # Insekten
    insect('biene', 'Biene', 'Bumblebee', (240, 200, 40), (240, 200, 40), (220, 230, 240), 0.35, 3, ['wiese', 'wald'], pattern='bands', grp=(2, 5))
    insect('hummel', 'Hummel', 'Bumblebee', (240, 190, 40), (40, 36, 30), (210, 220, 230), 0.5, 3, ['wiese'], pattern='bands')
    insect('hornisse', 'Hornisse', 'Hornet', (200, 140, 40), (150, 60, 30), (230, 220, 200), 0.6, 1, ['wald'], pattern='bands', grp=(1, 3))
    insect('zikade', 'Zikade', 'Fly', (80, 110, 60), (60, 90, 50), (190, 220, 200), 0.5, 3, ['savanne', 'dschungel'], pattern='veins')
    insect('gluehkaefer', 'Gluehkaefer', 'Fly', (40, 60, 50), (40, 50, 40), (170, 190, 170), 0.4, 3, ['sumpf', 'wald'], glow=(150, 255, 140), grp=(3, 6))
    # Fantasie
    beast('phoenix', 'Phoenix', (240, 120, 30), (250, 210, 80), (250, 160, 40), (220, 40, 30), (255, 250, 200), 2.2, 3, ['wueste', 'gebirge', 'fantasie'], feathers=True, beak_c=(250, 220, 90))
    beast('greif', 'Greif', (200, 160, 90), (226, 200, 150), (150, 110, 70), (90, 70, 50), (250, 200, 40), 2.4, 1, ['gebirge', 'fantasie'], feathers=True, beak_c=(240, 200, 60), head_c=WHITE)
    beast('wyvern', 'Wyvern', (90, 60, 110), (170, 140, 170), (120, 80, 140), (50, 30, 60), (240, 80, 60), 2.8, 1, ['gebirge', 'sumpf', 'fantasie'])
    beast('federschlange', 'Federschlange', (40, 170, 120), (240, 210, 80), (60, 190, 160), (220, 60, 50), (250, 240, 120), 2.4, 3, ['dschungel', 'fantasie'], feathers=True, serpent=True, beak_c=None)
    flat = lambda v: v
    solo('himmelsrochen', 'Himmelsrochen', fishbody('himmelsrochen', 40, 44, 8, (70, 90, 170), (220, 226, 240), kind='ray', tail='whip', dorsal='none', pattern='dots', pat_c=(200, 220, 255)), 3.0, 3, 'luft', ['kueste', 'wiese', 'fantasie'], 2, (1, 1), ['fliegt=1'], 'Basis Hai: ein Koerperteil, schwebt (FlightTick)')
    solo('luftqualle', 'Luftqualle', jelly('luftqualle', 22, (200, 160, 240), (240, 200, 255)), 1.6, 3, 'luft', ['sumpf', 'wald', 'fantasie'], 3, (2, 4), ['fliegt=1'])
    insect('lichtfee', 'Lichtfee', 'Fly', (250, 240, 200), (250, 230, 210), (220, 240, 255), 0.4, 3, ['wald', 'fantasie'], glow=(255, 250, 200), weight=3, grp=(2, 4))
    solo('windgeist', 'Windgeist', spirit('windgeist', (200, 236, 240), (150, 210, 230)), 1.8, 3, 'luft', ['gebirge', 'schnee', 'fantasie'], 2, (1, 2), ['fliegt=1'])
    insect('kristallfalter', 'Kristallfalter', 'Fly', (120, 160, 220), (100, 140, 220), (180, 230, 255), 0.7, 3, ['gebirge', 'fantasie'], pattern='veins', weight=4)
    bird('funkenvogel', 'Funkenvogel', (250, 120, 30), (250, 200, 60), (250, 80, 30), (250, 230, 120), (250, 160, 40), 0.6, 3, ['wueste', 'fantasie'], tail_c=(250, 230, 120), weight=4, grp=(2, 4))
    bird('seeadler', 'Seeadler', (90, 64, 44), (110, 80, 56), WHITE, (240, 200, 60), (80, 58, 40), 2.0, 1, ['kueste', 'gebirge'], bk='hook', tail_c=WHITE, weight=5)
    bird('schneeeule', 'Schneeeule', WHITE, WHITE, WHITE, (40, 40, 40), (236, 236, 236), 1.0, 3, ['schnee'], skel='owl', weight=6)
    bird('sturmvogel', 'Sturmvogel', (70, 80, 110), (180, 190, 210), (60, 70, 100), (200, 210, 230), (60, 70, 120), 1.4, 3, ['kueste', 'gebirge', 'fantasie'], skel='seagull', bk='hook', wing_tip=(200, 230, 255), weight=4)

def jelly(name, size, bell, tent, glow=None):
    s = min(size, 40); v = V(s, s, int(s * 1.6), name); cx = s / 2; top = v.sz - 1
    v.ell(cx, cx, top - s * 0.32, s * 0.46, s * 0.46, s * 0.32, lambda x, y, z: glow if (glow and H(name, x, y, z) < 0.1) else (bell if z > top - s * 0.45 else mix(bell, WHITE, 0.3)), 8,
          cut=lambda x, y, z: z > top - s * 0.42)
    n = 10
    for k in range(n):
        a = k / n * 2 * math.pi; r = s * 0.32
        for z in range(int(top - s * 0.42), 0, -1):
            sway = math.sin(z * 0.4 + k) * 1.0
            v.put(cx + math.cos(a) * (r - (top - z) * 0.05) + sway, cx + math.sin(a) * (r - (top - z) * 0.05), z, tent, 6)
    return v

def spirit(name, c1, c2):
    v = V(18, 18, 30, name)
    for z in range(30):
        r = 6.5 * (0.35 + 0.65 * math.sin(math.pi * min(1, (z + 2) / 30)))
        for k in range(10):
            a = k / 10 * 2 * math.pi + z * 0.35
            v.put(9 + math.cos(a) * r, 9 + math.sin(a) * r, z, mix(c2, c1, z / 30), 8)
    v.ell(9, 9, 24, 4.5, 4.5, 4.5, c1)
    for sgn in (-1, 1): v.put(9 + sgn * 1.6, 13, 25, (40, 60, 90), 0)
    return v

# ===================================================================================== WASSER
def octopus(name, size, c, c2, arms=8, squid=False):
    s = min(size, 40); v = V(s, s, s, name); cx = s / 2
    if squid:
        v = V(s // 2 + 2, s, s // 2 + 2, name); w = v.sx; cx = w / 2
        v.ell(cx, s * 0.62, w / 2, w * 0.36, s * 0.34, w * 0.36, c)
        for sgn in (-1, 1): v.ell(cx + sgn * w * 0.32, s * 0.86, w / 2, w * 0.18, s * 0.10, 0.8, c2)
        for k in range(arms):
            a = k / arms * 2 * math.pi
            for y in range(int(s * 0.30), 0, -1):
                rr = 1.6 * (y / (s * 0.30)) + 0.4
                v.put(cx + math.cos(a) * rr, y, w / 2 + math.sin(a) * rr + math.sin(y * 0.5 + k) * 0.6, c2, 5)
        for sgn in (-1, 1): v.put(cx + sgn * w * 0.30 - .5, s * 0.36, w / 2 + 1, BLACK, 0)
        return v
    v.ell(cx, cx, s * 0.62, s * 0.30, s * 0.32, s * 0.30, lambda x, y, z: mix(c, BLACK, 0.2) if H(name, x // 2, y // 2, z // 2) < 0.2 else c)
    for sgn in (-1, 1): v.ell(cx + sgn * s * 0.16, cx + s * 0.22, s * 0.52, 1.6, 1.0, 1.6, (240, 220, 120)); v.put(cx + sgn * s * 0.16 - .5, cx + s * 0.30, s * 0.52, BLACK, 0)
    for k in range(arms):
        a = k / arms * 2 * math.pi
        for i in range(int(s * 0.5)):
            t = i / (s * 0.5); r = s * 0.12 + t * s * 0.36
            v.ell(cx + math.cos(a + t * 0.8) * r, cx + math.sin(a + t * 0.8) * r, s * 0.36 - t * s * 0.30 + math.sin(t * 6 + k) * 1.2, 1.4 * (1 - t) + 0.5, 1.4 * (1 - t) + 0.5, 1.2, c2 if i % 3 else mix(c2, WHITE, 0.4), 5)
    return v

def crab_body(name, size, c, lobster=False):
    s = min(size, 40); v = V(s, int(s * (1.5 if lobster else 1.0)), int(s * 0.6), name); W, L, Hh = v.sx, v.sy, v.sz; cx = W / 2
    if lobster:
        v.ell(cx, L * 0.55, Hh * 0.4, W * 0.20, L * 0.22, Hh * 0.30, c)
        for k in range(5): v.ell(cx, L * 0.30 - k * L * 0.05, Hh * 0.35, W * 0.17 - k * 0.3, L * 0.04, Hh * 0.24, mix(c, BLACK, 0.1 * (k % 2)))
        v.ell(cx, L * 0.04, Hh * 0.3, W * 0.20, L * 0.04, Hh * 0.1, c)
    else:
        v.ell(cx, L * 0.45, Hh * 0.42, W * 0.40, L * 0.30, Hh * 0.28, c)
    for sgn in (-1, 1):
        v.cap((cx + sgn * W * 0.15, L * 0.70, Hh * 0.4), (cx + sgn * W * 0.30, L * 0.86, Hh * 0.45), 1.2, 1.0, c)
        v.ell(cx + sgn * W * 0.32, L * 0.92, Hh * 0.45, W * 0.12, L * 0.07, Hh * 0.16, mix(c, (240, 200, 160), 0.2))
        for k in range(3 if lobster else 4):
            yy = L * (0.50 - k * 0.07) if lobster else L * (0.30 + k * 0.10)
            v.cap((cx + sgn * W * 0.20, yy, Hh * 0.35), (cx + sgn * W * 0.47, yy - 1, 0.5), 0.7, 0.5, mix(c, BLACK, 0.15))
        v.put(cx + sgn * 1.5 - .5, L * 0.72, Hh * 0.75, BLACK, 0)
        if lobster: v.cap((cx + sgn * 1, L * 0.72, Hh * 0.6), (cx + sgn * W * 0.40, L - 1, Hh * 0.9), 0.4, 0.3, c)
    return v

def starfish(name, size, c):
    s = min(size, 30); v = V(s, s, 4, name); cx = s / 2
    for k in range(5):
        a = k / 5 * 2 * math.pi + math.pi / 2
        for i in range(int(s * 0.47)):
            r = 2.6 * (1 - i / (s * 0.47)) + 0.6
            v.ell(cx + math.cos(a) * i, cx + math.sin(a) * i, 1.5, r, r, 1.4, c if (i % 3) else mix(c, WHITE, 0.3), 6)
    return v

def turtle_sea(name, size, shell, skin):
    s = min(size, 44); v = V(s, s, int(s * 0.45), name); cx = s / 2; Hh = v.sz
    v.ell(cx, s * 0.45, Hh * 0.45, s * 0.34, s * 0.36, Hh * 0.40, lambda x, y, z: (shell if (x // 4 + y // 4) % 2 else mix(shell, BLACK, 0.2)) if z > Hh * 0.35 else (220, 210, 160))
    v.ell(cx, s * 0.88, Hh * 0.45, s * 0.10, s * 0.10, Hh * 0.22, skin)
    for sgn in (-1, 1):
        v.put(cx + sgn * s * 0.06 - .5, s * 0.94, Hh * 0.55, BLACK, 0)
        v.cap((cx + sgn * s * 0.25, s * 0.62, Hh * 0.38), (cx + sgn * s * 0.49, s * 0.48, Hh * 0.30), s * 0.06, s * 0.03, skin)
        v.cap((cx + sgn * s * 0.22, s * 0.18, Hh * 0.38), (cx + sgn * s * 0.34, s * 0.06, Hh * 0.30), s * 0.04, s * 0.02, skin)
    return v

def seahorse(name, size, c, c2):
    s = min(size, 34); v = V(int(s * 0.4), int(s * 0.6), s, name); cx = v.sx / 2
    for i in range(s):
        t = i / s; z = s - 1 - i
        y = v.sy * 0.45 + math.sin(t * math.pi * 1.2) * v.sy * 0.15 - (t > 0.7) * (t - 0.7) * v.sy * 0.8
        r = 2.6 * math.sin(math.pi * min(1, t * 1.1 + 0.1)) + 0.5
        v.ell(cx, y, z, r * 0.8, r, 1.0, c if i % 3 else c2, 6)
    v.cap((cx, v.sy * 0.55, s - 3), (cx, v.sy - 1, s - 5), 1.0, 0.6, c)
    v.put(cx - .5, v.sy * 0.55, s - 2, BLACK, 0)
    return v

def nautilus(name, size, c):
    s = min(size, 30); v = V(int(s * 0.5), s, s, name); cx = v.sx / 2
    for i in range(160):
        a = i / 160 * 3.2 * math.pi; r = s * 0.06 + s * 0.12 * a / math.pi
        v.ell(cx, s * 0.45 + math.cos(a) * r * 0.9, s * 0.5 + math.sin(a) * r * 0.9, v.sx * 0.40 * (0.3 + a / (3.2 * math.pi)), 1.2, 1.2,
              (240, 230, 214) if int(a * 3) % 2 else c, 4)
    for k in range(8): v.cap((cx, s * 0.80, s * 0.45), (cx + (k % 3 - 1) * 1.5, s - 1, s * 0.30 + k), 0.6, 0.4, (220, 180, 160))
    return v

def mammal(name, L, W, Hh, top, belly, *, ears=False, tusks=False, whiskers=False, flippers=True, face=None, tail='fluke'):
    v = fishbody(name, L, W, Hh, top, belly, kind='whale', tail=tail, dorsal='none', head_c=face)
    cx = W / 2
    if ears:
        for sgn in (-1, 1): v.put(cx + sgn * W * 0.22 - .5, L * 0.84, Hh * 0.82, top, 3)
    if tusks:
        for sgn in (-1, 1): v.cap((cx + sgn * 1.5, L * 0.92, Hh * 0.30), (cx + sgn * 2.0, L - 1, 0.5), 0.8, 0.5, (240, 232, 210))
    if whiskers:
        for sgn in (-1, 1):
            for k in range(3): v.put(cx + sgn * (W * 0.26 + k) - .5, L - 2, Hh * 0.40 + k * 0.5, (220, 210, 190), 2)
    return v

def build_water():
    G = 'wasser'
    # Suesswasser
    solo('forelle', 'Forelle', fishbody('forelle', 26, 9, 10, (110, 120, 90), (230, 220, 200), pattern='dots', pat_c=(60, 50, 40), lateral=(230, 140, 150)), 0.7, 3, G, ['suess'], grp=(1, 3))
    solo('lachs', 'Lachs', fishbody('lachs', 32, 10, 12, (120, 140, 160), (240, 200, 190), pattern='dots', pat_c=BLACK), 0.9, 3, G, ['suess', 'meer'], grp=(2, 4))
    solo('karpfen', 'Karpfen', fishbody('karpfen', 28, 11, 14, (150, 120, 60), (220, 200, 140)), 0.9, 3, G, ['suess'], grp=(1, 3))
    solo('hecht', 'Hecht', fishbody('hecht', 38, 8, 9, (80, 110, 70), (210, 220, 180), pattern='spots', pat_c=(190, 200, 140), tail='fork', dorsal='small'), 1.2, 1, G, ['suess'])
    solo('barsch', 'Barsch', fishbody('barsch', 22, 9, 13, (120, 140, 70), (230, 210, 150), pattern='vstripes', pat_c=(40, 60, 30), dorsal='sail', fin_c=(220, 90, 50)), 0.6, 3, G, ['suess'], grp=(2, 5))
    solo('wels', 'Wels', fishbody('wels', 50, 14, 12, (70, 66, 60), (190, 180, 170), kind='eel', tail='round', dorsal='small',
                                  extra=lambda v: [v.cap((v.sx / 2 + s * 2, v.sy - 2, v.sz * 0.4), (v.sx / 2 + s * 6, v.sy - 6, 1), 0.5, 0.4, (60, 56, 50)) for s in (-1, 1)]), 2.0, 1, G, ['suess'], 5, (1, 1))
    solo('goldfisch', 'Goldfisch', fishbody('goldfisch', 16, 8, 10, (250, 140, 30), (250, 200, 80), tail='fork', dorsal='curved'), 0.35, 3, G, ['suess'], grp=(3, 6))
    solo('koi', 'Koi', fishbody('koi', 28, 10, 11, WHITE, WHITE, pattern='spots', pat_c=(230, 80, 30)), 0.8, 3, G, ['suess'], grp=(2, 4))
    solo('piranha', 'Piranha', fishbody('piranha', 18, 8, 14, (130, 140, 150), (220, 70, 50), pattern='dots', pat_c=(240, 240, 240)), 0.45, 1, G, ['suess'], grp=(3, 6))
    solo('zitteraal', 'Zitteraal', fishbody('zitteraal', 56, 6, 7, (70, 74, 60), (230, 160, 80), kind='eel', tail='none', dorsal='none', lateral=(250, 240, 120)), 1.6, 1, G, ['suess', 'sumpf'], 5, (1, 1))
    solo('axolotl', 'Axolotl', fishbody('axolotl', 22, 10, 7, (250, 180, 196), (250, 210, 220), kind='eel', tail='round', dorsal='long',
                                       extra=lambda v: [v.cap((v.sx / 2 + sg * 3, v.sy - 5, v.sz * 0.5), (v.sx / 2 + sg * 5, v.sy - 7, v.sz - 1), 0.6, 0.6, (230, 60, 110)) for sg in (-1, 1)]), 0.45, 3, G, ['suess', 'sumpf'], 6)
    # Riff
    solo('clownfisch', 'Clownfisch', fishbody('clownfisch', 14, 7, 9, (250, 120, 30), (250, 140, 40), pattern='bands', pat_c=WHITE, fin_c=BLACK, tail='round'), 0.3, 3, G, ['riff'], grp=(2, 4))
    solo('doktorfisch', 'Doktorfisch', fishbody('doktorfisch', 18, 7, 14, (40, 80, 220), (60, 110, 230), fin_c=(250, 220, 40), pattern='stripe', pat_c=BLACK), 0.45, 3, G, ['riff'], grp=(2, 4))
    solo('kugelfisch', 'Kugelfisch', fishbody('kugelfisch', 18, 16, 16, (200, 190, 140), (240, 236, 220), kind='puffer', pattern='dots', pat_c=(60, 50, 40), tail='round',
                                            extra=lambda v: [v.put(x, y, z + 1, (220, 210, 160), 2, over=False) for z in range(v.sz - 1) for y in range(v.sy) for x in range(v.sx) if v.o[z, y, x] and H('kf', x, y, z) < 0.08]), 0.5, 1, G, ['riff', 'meer'])
    solo('seepferdchen', 'Seepferdchen', seahorse('seepferdchen', 22, (250, 190, 60), (230, 150, 50)), 0.4, 3, G, ['riff'], extra=['schwimmtiefe=0.3'], grp=(1, 3))
    solo('seestern', 'Seestern', starfish('seestern', 18, (240, 110, 70)), 0.5, 3, G, ['riff', 'meer'], extra=['schwimmtiefe=0'], grp=(1, 3))
    solo('krabbe', 'Krabbe', crab_body('krabbe', 20, (220, 90, 50)), 0.6, 1, G, ['riff', 'meer', 'suess'], extra=['schwimmtiefe=0'], grp=(1, 3))
    solo('hummer', 'Hummer', crab_body('hummer', 18, (40, 70, 130), lobster=True), 0.9, 1, G, ['meer', 'riff'], extra=['schwimmtiefe=0'])
    solo('nautilus', 'Nautilus', nautilus('nautilus', 24, (190, 110, 70)), 0.7, 3, G, ['meer', 'tief'], extra=['schwimmtiefe=0.4'])
    # Hochsee
    solo('thunfisch', 'Thunfisch', fishbody('thunfisch', 44, 14, 16, (40, 60, 110), (210, 220, 230), fin_c=(240, 220, 60)), 1.6, 3, G, ['meer'], grp=(2, 5))
    solo('schwertfisch', 'Schwertfisch', fishbody('schwertfisch', 50, 10, 14, (50, 70, 120), (210, 216, 230), dorsal='sail',
                                                extra=lambda v: v.cap((v.sx / 2, v.sy * 0.92, v.sz * 0.5), (v.sx / 2, v.sy - 1, v.sz * 0.5), 1.2, 0.5, (90, 100, 120))), 1.8, 1, G, ['meer'])
    solo('mondfisch', 'Mondfisch', fishbody('mondfisch', 30, 8, 40, (150, 160, 170), (210, 216, 220), kind='sunfish', tail='round', dorsal='tall'), 2.6, 3, G, ['meer', 'tief'], 3, (1, 1))
    solo('barrakuda', 'Barrakuda', fishbody('barrakuda', 46, 7, 8, (120, 130, 140), (220, 226, 230), pattern='vstripes', pat_c=(80, 90, 100)), 1.4, 1, G, ['meer', 'riff'], grp=(1, 3))
    solo('rochen', 'Rochen', fishbody('rochen', 34, 32, 6, (150, 130, 100), (230, 226, 216), kind='ray', tail='whip', dorsal='none', pattern='spots', pat_c=(110, 90, 70)), 1.2, 3, G, ['meer', 'riff'], extra=['schwimmtiefe=0.1'])
    solo('manta', 'Manta', fishbody('manta', 40, 56, 8, (30, 34, 44), (236, 236, 240), kind='ray', tail='whip', dorsal='none',
                                   extra=lambda v: [v.cap((v.sx / 2 + sg * 5, v.sy - 4, v.sz / 2), (v.sx / 2 + sg * 6, v.sy - 1, v.sz / 2), 1.2, 0.8, (40, 44, 54)) for sg in (-1, 1)]), 3.2, 3, G, ['meer'], 3, (1, 1))
    solo('anglerfisch', 'Anglerfisch', fishbody('anglerfisch', 26, 16, 18, (50, 44, 50), (70, 60, 66), kind='puffer', tail='round', dorsal='none',
                                              extra=lambda v: (v.cap((v.sx / 2, v.sy * 0.75, v.sz - 2), (v.sx / 2, v.sy - 1, v.sz - 1), 0.4, 0.4, (90, 80, 80)), v.ell(v.sx / 2, v.sy - 1.5, v.sz - 1.5, 1.4, 1.4, 1.4, (200, 255, 210), 0),
                                                               [v.put(x, v.sy - 1, v.sz * 0.35, WHITE, 0) for x in range(2, v.sx - 2, 2)])), 1.1, 1, G, ['tief'])
    # Haie
    solo('hammerhai', 'Hammerhai', fishbody('hammerhai', 50, 14, 14, (110, 120, 130), (220, 224, 228), kind='shark', tail='shark', dorsal='tall',
                                           extra=lambda v: v.box(0, v.sy - 4, v.sz * 0.45, v.sx - 1, v.sy - 2, v.sz * 0.6, (110, 120, 130))), 2.4, 1, G, ['meer'], 4)
    solo('weisser_hai', 'Weisser Hai', fishbody('weisser_hai', 56, 18, 20, (100, 110, 120), (240, 240, 240), kind='shark', tail='shark', dorsal='tall',
                                              extra=lambda v: [v.put(x, v.sy - 3, v.sz * 0.30, WHITE, 0) for x in range(3, v.sx - 3)]), 3.2, 1, G, ['meer', 'tief'], 3, (1, 1))
    solo('walhai', 'Walhai', fishbody('walhai', 64, 26, 24, (60, 80, 110), (220, 226, 230), kind='shark', tail='shark', dorsal='small', pattern='dots', pat_c=(240, 240, 240)), 5.0, 3, G, ['meer', 'tief'], 2, (1, 1))
    # Meeressaeuger
    solo('delfin', 'Delfin', fishbody('delfin', 40, 12, 13, (110, 130, 150), (226, 230, 236), kind='dolphin', tail='fluke', dorsal='curved',
                                     extra=lambda v: v.cap((v.sx / 2, v.sy * 0.92, v.sz * 0.42), (v.sx / 2, v.sy - 1, v.sz * 0.40), 1.4, 1.0, (120, 140, 160))), 1.6, 3, G, ['meer'], grp=(2, 4), extra=['schwimmtiefe=0.85'])
    solo('orca', 'Orca', fishbody('orca', 56, 18, 20, BLACK, WHITE, kind='whale', tail='fluke', dorsal='tall',
                                 extra=lambda v: [v.ell(v.sx / 2 + sg * v.sx * 0.30, v.sy * 0.84, v.sz * 0.62, 1.2, 2.6, 1.4, WHITE) for sg in (-1, 1)]), 3.4, 1, G, ['meer', 'tief'], 3, (1, 3))
    solo('buckelwal', 'Buckelwal', fishbody('buckelwal', 64, 28, 24, (60, 66, 80), (210, 210, 216), kind='whale', tail='fluke', dorsal='small', pattern='grooves'), 7.0, 3, G, ['tief', 'meer'], 2, (1, 1), extra=['schwimmtiefe=0.7'])
    solo('narwal', 'Narwal', fishbody('narwal', 48, 16, 16, (150, 156, 166), (220, 222, 226), kind='whale', tail='fluke', dorsal='none', pattern='spots', pat_c=(100, 106, 116),
                                     extra=lambda v: [v.put(v.sx / 2 - .5 + (k % 2) * 0.4, v.sy - 8 + k, v.sz * 0.52, (240, 232, 210), 1) for k in range(8)]), 2.6, 3, G, ['meer'], 3, (1, 2))
    solo('seehund', 'Seehund', mammal('seehund', 30, 12, 11, (130, 126, 120), (200, 196, 186), whiskers=True), 1.0, 3, G, ['meer', 'kueste'], extra=['schwimmtiefe=0.85'], grp=(1, 3))
    solo('seeloewe', 'Seeloewe', mammal('seeloewe', 36, 13, 13, (120, 90, 60), (150, 120, 90), ears=True, whiskers=True), 1.4, 3, G, ['meer', 'kueste'], extra=['schwimmtiefe=0.85'], grp=(1, 3))
    solo('walross', 'Walross', mammal('walross', 40, 20, 18, (150, 110, 90), (170, 130, 110), tusks=True, whiskers=True), 2.4, 1, G, ['meer', 'kueste'], 4, extra=['schwimmtiefe=0.9'])
    solo('seeotter', 'Seeotter', mammal('seeotter', 24, 9, 8, (100, 74, 50), (190, 170, 150), ears=True, whiskers=True, face=(200, 186, 170)), 0.6, 3, G, ['kueste', 'meer'], extra=['schwimmtiefe=0.95'], grp=(1, 3))
    solo('seekuh', 'Seekuh', mammal('seekuh', 44, 20, 16, (140, 140, 140), (170, 170, 168), whiskers=True, tail='round'), 2.6, 3, G, ['kueste', 'suess'], 4, extra=['schwimmtiefe=0.6'])
    # Weitere
    solo('krake', 'Krake', octopus('krake', 28, (190, 80, 70), (220, 120, 100)), 1.4, 1, G, ['meer', 'riff'], extra=['schwimmtiefe=0.15'])
    solo('kalmar', 'Kalmar', octopus('kalmar', 30, (230, 200, 200), (240, 170, 170), arms=10, squid=True), 1.2, 3, G, ['meer', 'tief'], grp=(2, 4))
    solo('qualle', 'Qualle', jelly('qualle', 16, (200, 200, 250), (230, 220, 250)), 0.7, 1, G, ['meer', 'riff'], grp=(2, 5))
    solo('meeresschildkroete', 'Meeresschildkroete', turtle_sea('meeresschildkroete', 32, (100, 120, 70), (150, 160, 110)), 1.6, 3, G, ['meer', 'riff'])
    solo('krokodil', 'Krokodil', fishbody('krokodil', 60, 14, 9, (70, 90, 60), (190, 190, 140), kind='eel', tail='round', dorsal='long', fin_c=(60, 80, 50),
                                         extra=lambda v: ([v.cap((v.sx / 2 + sg * 4, v.sy * y, v.sz * 0.3), (v.sx / 2 + sg * 7, v.sy * y - 2, 0.5), 1.0, 0.8, (70, 90, 60)) for sg in (-1, 1) for y in (0.70, 0.45)],
                                                          [v.put(v.sx / 2 + sg * 2.5 - .5, v.sy - 2, v.sz * 0.35, WHITE, 0) for sg in (-1, 1)])), 2.4, 1, G, ['sumpf', 'suess'], 4, extra=['schwimmtiefe=0.95'])
    solo('wasserschlange', 'Wasserschlange', fishbody('wasserschlange', 58, 5, 5, (40, 60, 70), (200, 210, 160), kind='eel', tail='none', dorsal='none', pattern='bands', pat_c=(230, 220, 160)), 1.0, 1, G, ['riff', 'meer', 'sumpf'])
    solo('pinguin', 'Pinguin', fishbody('pinguin', 22, 10, 10, BLACK, WHITE, kind='dolphin', tail='round', dorsal='none', head_c=BLACK,
                                       extra=lambda v: v.cap((v.sx / 2, v.sy - 3, v.sz * 0.5), (v.sx / 2, v.sy - 1, v.sz * 0.46), 0.8, 0.4, (240, 160, 40))), 0.6, 3, G, ['meer', 'kueste'], grp=(3, 6), extra=['schwimmtiefe=0.8'])
    solo('blauwal', 'Blauwal', fishbody('blauwal', 64, 24, 20, (70, 90, 120), (150, 170, 190), kind='whale', tail='fluke', dorsal='small', pattern='dots', pat_c=(110, 130, 160)), 9.0, 3, G, ['tief'], 1, (1, 1), extra=['schwimmtiefe=0.6'])
    solo('muraene', 'Muraene', fishbody('muraene', 50, 7, 8, (110, 120, 40), (150, 160, 70), kind='eel', tail='none', dorsal='long', pattern='dots', pat_c=(40, 50, 20)), 1.3, 1, G, ['riff'], extra=['schwimmtiefe=0.1'])
    # Fantasie
    solo('seeschlange', 'Seeschlange', fishbody('seeschlange', 64, 10, 10, (30, 90, 110), (170, 210, 190), kind='eel', tail='fork', dorsal='long', fin_c=(220, 80, 60), pattern='bands', pat_c=(40, 140, 150)), 4.5, 1, G, ['meer', 'tief', 'fantasie'], 2, (1, 1))
    solo('riesenkraken', 'Riesenkraken', octopus('riesenkraken', 40, (120, 40, 50), (170, 70, 80)), 6.0, 1, G, ['tief', 'fantasie'], 1, (1, 1), extra=['schwimmtiefe=0.2'])
    solo('kristallqualle', 'Kristallqualle', jelly('kristallqualle', 24, (160, 230, 255), (200, 160, 255), glow=(240, 255, 255)), 1.4, 3, G, ['tief', 'meer', 'fantasie'], 3, (2, 4))

TB.REG = reg
build_land(); build_air(); build_water()

# Liste fuer den Spawner + Uebersicht
lst = os.path.join(T.OUT, 'Mobs', 'Tierwelt', '_tierwelt.txt')
import tiergruppen as TGR                       # Tiergruppen (5 je Medium) + die 26 Tiere aus Strukturen\Mobs\Tiere
with open(lst, 'w', newline='\n') as f:
    f.write(TGR.HEADER)
    for n, m, hab, w, g0, g1 in LIST: f.write(f"{n};{m};{','.join(hab)};{w};{g0};{g1};{TGR.group_of(n, m)}\n")
    for n, m, hab, w, g0, g1 in TGR.OLD: f.write(f"{n};{m};{hab};{w};{g0};{g1};{TGR.group_of(n, m)}\n")
with open(os.path.join(T.OUT, 'Mobs', 'Tierwelt', 'LIESMICH_Tierwelt.md'), 'w', newline='\n') as f:
    f.write("# Tierwelt (2026-10-14)\n\n150 Tiere aus Grundformen, je eine eigene Rasse (226..375). Der Spawner (Wildlife.cpp) setzt sie nach "
            "Lebensraum um den Spieler; Wassertiere schwimmen (schwimmt=1), Flieger fliegen (fliegt=1).\n\n| Mob | Name | Rasse | Basis | Medium | Lebensraum | Faktor |\n|---|---|---|---|---|---|---|\n")
    fac = {}
    for (n, d, b, r, note), (n2, m, hab, w, g0, g1) in zip(T.MOBS, LIST):
        txt = open(os.path.join(T.OUT, 'Mobs', 'Tierwelt', n + '.mob')).read(); fa = [l.split('=')[1] for l in txt.splitlines() if l.startswith('faktor=')][0]
        f.write(f"| {n} | {d} | {r} | {b} | {m} | {', '.join(hab)} | {fa} |\n")
from collections import Counter
print(len(T.MOBS), 'Tiere,', len(T.PREV), 'Teile', dict(Counter(m for _, m, *_ in LIST)), 'Rassen', T.MOBS[0][3], '..', T.MOBS[-1][3])

if PREVIEW_DIR:
    import numpy as np
    from PIL import Image
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    def proj(p):                                                                            # Seitenansicht (x = y-Achse, Hoehe = z)
        img = np.full((p.sz, p.sy, 3), 255, np.uint8)
        for y in range(p.sy):
            for z in range(p.sz):
                xs = np.nonzero(p.o[z, y, :])[0]
                if len(xs): img[p.sz - 1 - z, y] = p.a[z, y, xs[0]]
        return img
    by = {}
    for n, slot, p in T.PREV: by.setdefault(n, []).append((slot, p))
    names = [m[0] for m in T.MOBS]
    for gi in range(0, len(names), 25):
        tiles = []
        for n in names[gi:gi + 25]:
            ims = [proj(p) for _, p in sorted(by[n], key=lambda t: t[0])]
            hmax = max(i.shape[0] for i in ims); w = sum(i.shape[1] + 2 for i in ims)
            row = np.full((hmax + 2, w, 3), 255, np.uint8); x = 0
            for i in ims: row[hmax - i.shape[0]:hmax, x:x + i.shape[1]] = i; x += i.shape[1] + 2
            tiles.append(Image.fromarray(row).resize((row.shape[1] * 3, row.shape[0] * 3), Image.NEAREST))
        W = 5; cw = max(t.width for t in tiles) + 8; ch = max(t.height for t in tiles) + 8
        sheet = Image.new('RGB', (cw * W, ch * ((len(tiles) + W - 1) // W)), 'white')
        for i, t in enumerate(tiles): sheet.paste(t, ((i % W) * cw, (i // W) * ch))
        sheet.save(os.path.join(PREVIEW_DIR, f'tierwelt_{gi // 25 + 1}.png'))
    print('Vorschau:', PREVIEW_DIR)
