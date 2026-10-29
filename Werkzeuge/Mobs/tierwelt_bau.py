# tierwelt_bau.py (2026-10-16) -- Bauer der Tierwelt mit sauberen Gelenken (tierbau2). Wird von tierwelt.py benutzt.
import math
import numpy as np
import tierbau as T
import tierbau2 as T2
from tierbau import HEAD, HAIR, HANDS, FEET, BODY, TAIL, SHOULDER, WINGS, mix, H
from tierbau2 import head_part, body_part, leg_part, tail_part, wing_part, Part, rebuild, ears_at, BLACK, WHITE

REG = None      # reg()-Funktion aus tierwelt.py (setzt tierwelt.py)

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
PREY_EYES = {'pony', 'cow', 'pig', 'camel', 'alpaca', 'sheep', 'mammoth'}      # seitliche Augen (Beutetiere)
HOOVES = {'pony', 'cow', 'mammoth', 'pig', 'camel', 'alpaca', 'sheep'}
AA = ['auto_ausrichtung=0']
import kopfbau as KB
NECK_HEAD = {'pony', 'alpaca'}
NECK = {'giraffe': 1.7}

def mane_c(fur): return mix(fur, (110, 66, 30), 0.6)

def quad(name, disp, skel, fur, belly, factor, host, hab, *, ear='point', ear_len=3, ear_c=None, snout=3, snout_c=None,
         nose=(30, 26, 26), spots=None, spot_c=None, stripes=False, tail_kind='normal', tail_c=None, tail_tip=None,
         leg_c=None, paw_c=None, horn=None, horn_c=(220, 206, 176), mane=None, hump=None, tusks=None, trunk=False,
         weight=10, grp=(1, 2), extra=(), wings=None):
    base, P = SKEL[skel]; parts = {}
    sn = {1: 0.7, 2: 0.85, 3: 1.0, 4: 1.2, 5: 1.35, 6: 1.5}.get(snout, 1.0)
    def hx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        if tusks:
            for sg in (-1, 1): p.cap((cx + sg * 1.6, y1 - 1, z0 + 2), (cx + sg * 2.4, min(p.sy - 1, y1 + 1), z0 + (5 if tusks == 'up' else 0)), 0.55, 0.4, (236, 230, 210))
        if trunk:
            p.cap((cx - .5, y1 - 1, z0 + (z1 - z0) * 0.35), (cx - .5, min(p.sy - 1, y1 + 2), max(0, z0 - 1)), 1.6, 0.9, mix(fur, BLACK, 0.08))
    parts[HEAD] = head_part(P[HEAD], name + 'h', fur, belly, snout=sn, snout_c=snout_c, nose=nose, ear=ear, ear_c=ear_c,
                            ear_size=ear_len, horn=horn, horn_c=horn_c, mane_kind={'ring': 'ring', 'crest': 'crest'}.get(mane),
                            mane_c=mane_c(fur) if mane == 'ring' else mix(fur, BLACK, 0.6), spots=spots, spot_c=spot_c,
                            stripes=stripes, stripe_c=spot_c, eyes_front=skel not in PREY_EYES, extra=hx)
    if skel in NECK_HEAD:          # 2026-10-27: kurzer Kopf mit aufrechtem Hals statt gestrecktem Pferdegesicht (kopfbau.py)
        parts[HEAD] = KB.huftier_kopf(name + 'h', fur, belly, neck=NECK.get(name, 0.45), snout_c=snout_c, nose=nose, ear=ear, ear_c=ear_c,
                                      ear_len=ear_len, horn={'giraffe': 'short'}.get(name, horn), horn_c=horn_c, mane=mane,
                                      spots=spots, spot_c=spot_c, stripes=stripes)
        extra = list(extra) + ['hals=1']
    parts[BODY] = body_part(P[BODY], name + 'b', fur, belly, spots=spots, spot_c=spot_c, stripes=stripes, stripe_c=spot_c, hump=hump or 0)
    lc = leg_c or fur; pc = paw_c or mix(lc, BLACK, 0.5)
    hoof = skel in HOOVES
    parts[FEET] = leg_part(P[FEET], name + 'f', lc, pc, hoof=hoof, claws=None if hoof else (230, 226, 214))
    if HANDS in P: parts[HANDS] = leg_part(P[HANDS], name + 'a', lc, pc, hoof=hoof, claws=None if hoof else (230, 226, 214))
    if TAIL in P:
        parts[TAIL] = None if tail_kind == 'none' else tail_part(P[TAIL], name + 't', tail_c or fur, tail_tip, bushy=(tail_kind == 'bushy'))
    if wings: parts[WINGS] = wing_part('dragon-wing', name + 'w', wings[0], wings[1])
    REG(name, disp, base, factor, parts, host, 'land', hab, weight, grp, f'Basis {base}: Gelenke der Basis, Form/Gesicht eigen', list(extra) + AA)

def roundy(name, disp, skel, col, belly, factor, host, hab, *, ears=None, spikes=None, bands=None, cap=None, snout=None,
           tail_c=None, weight=10, grp=(1, 3)):
    """Kleintier: Koerper mit Kopf vorne (Hase/Stachelschwein-Skelett)."""
    base = {'bunny': 'Bunny', 'porcupine': 'Porcupine'}[skel]
    p = Part(skel + '-body', name + 'b', 2)
    occ = rebuild(p.native, 'y', 2.6)
    x0, x1, y0, y1, z0, z1 = p.bbox(occ)
    def bc(x, y, z):
        if bands and (y // 2) % 2 == 0 and z > z0 + (z1 - z0) * 0.35: return mix(col, BLACK, 0.25)
        return belly if z < z0 + (z1 - z0) * 0.3 else col
    p.fill_from(occ, bc)
    cx = (x0 + x1 + 1) / 2
    if snout: p.ell(cx, y1 - 0.5, z0 + (z1 - z0) * 0.40, 1.4, 1.0, 1.0, snout)
    if ears: ears_at(p, ears, col, (230, 170, 170), 3, cx, y1 - (y1 - y0) * 0.30, (x1 - x0) * 0.6)
    if spikes:
        for z, y, x in zip(*np.nonzero(occ)):
            if z > z0 + (z1 - z0) * 0.45 and y < y1 - 4 and H(name, x, y, z) < 0.30:
                for k in (1, 2): p.put(x + (x - cx) * 0.1 * k, y - k * 0.6, z + k, spikes if k == 2 else mix(spikes, BLACK, 0.4))
    if cap:
        p.ell(cx, (y0 + y1) / 2, z1 + 1, (x1 - x0) * 0.62, (y1 - y0) * 0.5, 1.8, lambda x, y, z: WHITE if H(name, 'c', x // 2, y // 2) < 0.2 else cap)
    if tail_c: p.ell(cx, y0 + 0.5, z0 + (z1 - z0) * 0.55, 1.3, 1.0, 1.3, tail_c)
    T2.face(p, eye_iris=(30, 24, 20), nose=(60, 40, 40), mouth=True, eye_z=0.55, eye_back=0.18)
    p.shade()
    fp = leg_part(skel + '-foot', name + 'f', mix(col, BLACK, 0.15), mix(col, BLACK, 0.4), shrink=0.9)
    REG(name, disp, base, factor, {BODY: p.to_v(), FEET: fp}, host, 'land', hab, weight, grp, f'Basis {base}: Koerper mit Kopf, alles eigen', AA)

CRIT = {'squirrel': 'Squirrel', 'raccoon': 'Raccoon', 'koala': 'Koala', 'mole': 'Mole', 'monkey': 'Monkey'}
def critter(name, disp, skel, fur, belly, factor, host, hab, *, ear='round', snout=2, mask=None, stripe=None, upright=False,
            weight=10, grp=(1, 3), fur2=None):
    base = CRIT[skel]
    def hx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        if mask:
            for z in range(int(z0 + (z1 - z0) * 0.55), int(z0 + (z1 - z0) * 0.80)):
                for x in range(x0, x1 + 1):
                    fy = p.front_y(x, z)
                    if fy is not None and p.col[z, fy, x].sum() < 700: p.col[z, fy, x] = mask
        if stripe:
            for z, y, x in zip(*np.nonzero(p.occ)):
                if abs(x + .5 - cx) < 1.1 and z > z0 + (z1 - z0) * 0.5: p.col[z, y, x] = stripe
    sn = {1: 0.7, 2: 0.9, 3: 1.15}.get(snout, 1.0)
    parts = {HEAD: head_part(skel + '-head', name + 'h', fur, belly, snout=sn, ear=ear, ear_in=(220, 160, 160), eyes_front=(skel == 'monkey'), extra=hx, pad=2)}
    def bx(p):
        if skel == 'squirrel':
            x0, x1, y0, y1, z0, z1 = p.bbox()
            p.ell((x0 + x1 + 1) / 2, y0 + 1.5, z0 + (z1 - z0) * 0.7, (x1 - x0) * 0.25, 1.8, (z1 - z0) * 0.35, fur2 or fur)
    parts[BODY] = body_part(skel + '-body', name + 'b', fur2 or fur, belly, extra=bx)
    parts[FEET] = leg_part(skel + '-foot', name + 'f', fur, mix(fur, BLACK, 0.4), shrink=0.85, claws=(230, 226, 214))
    parts[HANDS] = leg_part(skel + '-hand', name + 'a', fur, mix(fur, BLACK, 0.3), shrink=0.85)
    REG(name, disp, base, factor, parts, host, 'land', hab, weight, grp, f'Basis {base}: Gelenke der Basis, Form/Gesicht eigen', AA)

def reptile(name, disp, skel, scale, belly, factor, host, hab, *, pattern=None, pat_c=None, legs=True, ridge=None, eye=(220, 180, 40),
            shell=None, moss=False, weight=10, grp=(1, 2), extra=()):
    base = {'croc': 'Crocodile', 'turtle': 'Turtle', 'frog': 'Frog'}[skel]
    pre = {'croc': 'crocodile', 'turtle': 'turtle', 'frog': 'frog'}[skel]
    spots = 0.25 if pattern == 'spots' else None
    parts = {HEAD: head_part(pre + '-head', name + 'h', scale, belly, snout=1.3 if skel == 'croc' else 0.8, iris=eye, eye_big=(skel == 'frog'),
                             spots=spots, spot_c=pat_c, nose=(30, 30, 24), pad=2, head_len=1.0 if skel != 'turtle' else 0.7,
                             snout_h=0.22 if skel == 'croc' else 0.3)}
    def bx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        if skel == 'turtle':
            for z, y, x in zip(*np.nonzero(p.occ)):
                if z > z0 + (z1 - z0) * 0.3: p.col[z, y, x] = shell if ((x // 3 + y // 3) % 2) else mix(shell, BLACK, 0.25)
            if moss:
                for x in range(p.sx):
                    for y in range(p.sy):
                        zs = np.nonzero(p.occ[:, y, x])[0]
                        if len(zs) and zs.max() + 1 < p.sz and H(name, 'm', x, y) < 0.55: p.put(x, y, zs.max() + 1, mix((70, 130, 50), (120, 170, 70), H(name, x, y)))
        if pattern in ('bands', 'diamond'):
            for z, y, x in zip(*np.nonzero(p.occ)):
                if z > z0 + (z1 - z0) * 0.3 and ((pattern == 'bands' and (y // 2) % 3 == 0) or (pattern == 'diamond' and ((x + y) % 6 == 0 or (x - y) % 6 == 0))): p.col[z, y, x] = pat_c
        if ridge:
            for y in range(y0 + 1, y1, 2):
                zs = np.nonzero(p.occ[:, y, int(cx)])[0]
                if len(zs) and zs.max() + 1 < p.sz: p.put(cx - .5, y, zs.max() + 1, ridge)
    parts[BODY] = body_part(pre + '-body', name + 'b', scale, belly, spots=spots, spot_c=pat_c, extra=bx, power=2.6)
    if legs:
        parts[FEET] = leg_part(pre + '-foot', name + 'f', mix(scale, BLACK, 0.1), mix(scale, BLACK, 0.35), shrink=0.9, claws=(220, 216, 200))
        if skel == 'frog': parts[HANDS] = leg_part('frog-hand', name + 'a', mix(scale, BLACK, 0.1), mix(scale, BLACK, 0.35), shrink=0.9)
    REG(name, disp, base, factor, parts, host, 'land', hab, weight, grp, f'Basis {base}: Gelenke der Basis, Form/Gesicht eigen', list(extra) + AA)

def ape(name, disp, fur, face_c, factor, host, hab, chest=None, weight=8, grp=(1, 2)):
    parts = {HEAD: head_part('monkey-head', name + 'h', fur, face_c, snout=0.6, snout_c=face_c, eyes_front=True, ear='round', ear_c=face_c, pad=2)}
    parts[BODY] = body_part('monkey-body', name + 'b', fur, chest or fur, front=chest)
    parts[FEET] = leg_part('monkey-foot', name + 'f', fur, face_c, shrink=0.9)
    parts[HANDS] = leg_part('monkey-hand', name + 'a', fur, face_c, shrink=0.9)
    REG(name, disp, 'Monkey', factor, parts, host, 'land', hab, weight, grp, 'Basis Affe: Gelenke der Basis, Form/Gesicht eigen', AA)

def scorpion(name, disp, shell, factor, host, hab, crystal=False, weight=8, grp=(1, 2)):
    parts = {HEAD: head_part('crab-head', name + 'h', shell, mix(shell, BLACK, 0.2), snout=0.5, eyes_front=True, iris=BLACK, nose=None, pad=1)}
    def bx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        for z, y, x in zip(*np.nonzero(p.occ)):
            if y % 3 == 0: p.col[z, y, x] = mix(shell, BLACK, 0.2)
        if crystal:
            for k in range(8):
                xx = x0 + 1 + H(name, k) * (x1 - x0 - 2); yy = y0 + 2 + H(name, 'y', k) * (y1 - y0 - 4)
                p.cap((xx, yy, z1 - 1), (xx, yy, min(p.sz - 1, z1 + 2)), 1.0, 0.4, mix((150, 220, 255), (210, 150, 255), H(name, 'c', k)))
        else:
            for k in range(9):
                a = k / 8 * math.pi * 0.9
                p.ell(cx, y0 + 1 + math.sin(a) * 2 + k * 0.3, z0 + 2 + (1 - math.cos(a)) * (z1 - z0) * 0.4, 1.2 - k * 0.06, 1.0, 1.0, shell)
            p.put(cx - .5, y0 + 4, z1, (40, 30, 30))
    parts[BODY] = body_part('crab-body', name + 'b', shell, mix(shell, BLACK, 0.15), extra=bx, power=2.4, shrink=0.85)
    parts[FEET] = leg_part('crab-foot', name + 'f', shell, mix(shell, BLACK, 0.3), shrink=0.9)
    parts[HANDS] = leg_part('crab-hand', name + 'a', mix(shell, BLACK, 0.1), mix(shell, BLACK, 0.3), shrink=0.95)
    REG(name, disp, 'Crab', factor, parts, host, 'land', hab, weight, grp, 'Basis Krabbe: Gelenke der Basis, Form/Gesicht eigen', AA)

def strauss():
    parts = {HEAD: head_part('chicken-head', 'strh', (210, 190, 180), (230, 210, 200), beak=('flat', (226, 196, 150)), pad=2, head_len=1.0)}
    def bx(p):
        y0 = p.bbox()[2]
        for z, y, x in zip(*np.nonzero(p.occ)):
            if y <= y0 + 1: p.col[z, y, x] = WHITE
    parts[BODY] = body_part('chicken-body', 'strb', (40, 34, 32), (60, 54, 50), extra=bx)
    parts[FEET] = leg_part('chicken-foot', 'strf', (210, 170, 150), (200, 160, 140), shrink=0.8)
    parts[HANDS] = wing_part('chicken-hand', 'stra', (46, 40, 36), WHITE)
    REG('strauss', 'Strauss', 'Chicken', 2.0, parts, 3, 'land', ['savanne', 'wueste'], 6, (1, 3), 'Basis Huhn (laeuft): Gelenke der Basis, Form eigen', AA)

# ===================================================================================== LUFT
BIRD_SKEL = {'crow': 'Crow', 'seagull': 'Seagull', 'parrot': 'Parrot', 'owl': 'Owl'}
def bird(name, disp, body_c, belly_c, head_c, beak_c, wing_c, factor, host, hab, *, skel='crow', bk='short', crest=None,
         eye=(20, 16, 14), tail_c=None, wing_tip=None, legs=(220, 170, 60), mask=None, weight=10, grp=(1, 3), extra=()):
    base = BIRD_SKEL[skel]
    def hx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        if crest:
            for k in range(4): p.put(cx - .5, y0 + (y1 - y0) * 0.45 - k * 0.6, min(p.sz - 1, z1 + 1 + k * 0.6), crest)
    if skel == 'owl':
        parts = {HEAD: head_part('owl-head', name + 'h', body_c, belly_c, beak=('short', beak_c), eyes_front=True, eye_big=True,
                                 iris=(250, 190, 40), extra=hx, pad=2, head_len=1.0, skull_h=1.0)}
    else:
        parts = {HEAD: head_part(skel + '-head', name + 'h', head_c, mix(head_c, belly_c, 0.5), beak=(bk, beak_c), iris=eye, extra=hx, pad=2, head_len=1.0)}
        def bx(p):
            x0, x1, y0, y1, z0, z1 = p.bbox()
            for z, y, x in zip(*np.nonzero(p.occ)):
                if y > y0 + (y1 - y0) * 0.55: p.col[z, y, x] = belly_c
            if tail_c:
                for z in range(z0, z0 + 4): p.put((x0 + x1 + 1) / 2 - .5, y0, z, tail_c)
        parts[BODY] = body_part(skel + '-body', name + 'b', body_c, belly_c, extra=bx, power=2.4)
    parts[FEET] = leg_part(skel + '-foot', name + 'f', legs, mix(legs, BLACK, 0.2), shrink=0.7, power=2.0)
    parts[HANDS] = wing_part(skel + '-hand', name + 'w', wing_c, wing_tip or mix(wing_c, BLACK, 0.3))
    REG(name, disp, base, factor, parts, host, 'luft', hab, weight, grp, f'Basis {base} (fliegt): Gelenke der Basis, Form/Gesicht eigen', list(extra) + AA)

def bat(name, disp, fur, wing, factor, host, hab, ears=(3, 2), weight=8, grp=(1, 4)):
    parts = {HEAD: head_part('bat-head', name + 'h', fur, mix(fur, (230, 180, 170), 0.3), snout=0.7, ear='big' if ears[0] > 2 else 'round', ear_c=fur,
                             ear_in=(220, 160, 160), eyes_front=True, iris=(250, 200, 60) if ears[0] > 2 else BLACK, pad=3, head_len=1.0)}
    parts[BODY] = body_part('bat-body', name + 'b', fur, mix(fur, BLACK, 0.2), power=2.4)
    parts[HANDS] = wing_part('bat-hand', name + 'w', wing, mix(wing, BLACK, 0.3))
    parts[FEET] = leg_part('bat-foot', name + 'f', BLACK, BLACK, shrink=0.6, power=2.0)
    REG(name, disp, 'Bat', factor, parts, host, 'luft', hab, weight, grp, 'Basis Fledermaus (fliegt): Gelenke der Basis, Form/Gesicht eigen', AA)

INS = {'Fly': 'fly', 'Mosquito': 'mosquito', 'Bumblebee': 'bumblebee', 'Hornet': 'hornet'}
def insect(name, disp, base, body_c, head_c, wing_c, factor, host, hab, pattern=None, glow=None, weight=8, grp=(1, 4)):
    pre = INS[base]
    def hx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        for sg in (-1, 1):
            p.ell(cx + sg * (x1 - x0) * 0.32, y1 - 1, z0 + (z1 - z0) * 0.6, 1.4, 1.2, 1.4, (24, 20, 30))           # Facettenaugen
            p.cap((cx + sg * 1.2, y1 - 1, z1), (cx + sg * 3, min(p.sy - 1, y1 + 1), min(p.sz - 1, z1 + 3)), 0.45, 0.4, (30, 26, 24))   # Fuehler
        if base == 'Bumblebee' and pattern == 'bands':
            for z, y, x in zip(*np.nonzero(p.occ)):
                if (y // 2) % 2 == 0 and y < y1 - 3: p.col[z, y, x] = BLACK
    parts = {HEAD: head_part(pre + '-head' if base != 'Bumblebee' else 'bumblebee', name + 'h', head_c, mix(head_c, BLACK, 0.2), snout=0.4,
                             nose=None, mouth=False, extra=hx, pad=3, head_len=1.0)}
    if base != 'Bumblebee':
        def bx(p):
            x0, x1, y0, y1, z0, z1 = p.bbox()
            for z, y, x in zip(*np.nonzero(p.occ)):
                if glow and z < z0 + (z1 - z0) * 0.35: p.col[z, y, x] = glow
                elif pattern == 'bands' and (z // 2) % 2 == 0: p.col[z, y, x] = mix(body_c, BLACK, 0.75)
        parts[BODY] = body_part(pre + '-body', name + 'b', body_c, body_c, extra=bx, power=2.2, shrink=0.9)
    parts[HANDS] = wing_part(pre + '-hand', name + 'w', wing_c, mix(wing_c, BLACK, 0.15) if pattern == 'veins' else None)
    parts[FEET] = leg_part(pre + '-foot', name + 'f', (30, 26, 24), (30, 26, 24), shrink=1.0, power=2.0)
    REG(name, disp, base, factor, parts, host, 'luft', hab, weight, grp, f'Basis {base} (fliegt): Gelenke der Basis, Form/Gesicht eigen', AA)

def beast(name, disp, scale_c, belly_c, wing_c, spine_c, eye_c, factor, host, hab, *, feathers=False, beak_c=None, head_c=None,
          serpent=False, weight=2, grp=(1, 1)):
    hc = head_c or scale_c
    def hx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        for sg in (-1, 1):
            if feathers: p.cap((cx + sg * 2, y0 + 3, z1), (cx + sg * 3, y0, min(p.sz - 1, z1 + 3)), 0.9, 0.5, spine_c)
            else: p.cap((cx + sg * 2.5, y0 + 4, z1), (cx + sg * 4, y0 + 1, min(p.sz - 1, z1 + 4)), 0.9, 0.4, spine_c)
    # 2026-10-27: kompakter Kopf mit Hals statt des 44 Voxel langen nativen Drachenkopfs (kopfbau.py)
    parts = {HEAD: KB.drachen_kopf(name + 'h', scale_c, belly_c, eye_c, beak_c=beak_c, spine_c=spine_c, feathers=feathers, head_c=head_c)}
    def bx(p):
        x0, x1, y0, y1, z0, z1 = p.bbox(); cx = (x0 + x1 + 1) / 2
        for y in range(y0 + 1, y1, 3):
            zs = np.nonzero(p.occ[:, y, int(cx)])[0]
            if len(zs) and zs.max() + 1 < p.sz: p.put(cx - .5, y, zs.max() + 1, spine_c)
        if serpent:
            for z, y, x in zip(*np.nonzero(p.occ)):
                if (y // 2) % 2 == 0 and z > z0 + 2: p.col[z, y, x] = belly_c
    parts[BODY] = body_part('dragon-body', name + 'b', scale_c, belly_c, extra=bx, shrink=0.85 if serpent else 1.0)
    parts[TAIL] = tail_part('dragon-tail', name + 't', scale_c, spine_c if feathers else None)
    if not serpent: parts[FEET] = leg_part('dragon-foot', name + 'f', belly_c if feathers else scale_c, mix(scale_c, BLACK, 0.4), shrink=0.85, claws=(236, 230, 214))
    parts[WINGS] = wing_part('dragon-wing', name + 'w', wing_c, spine_c if feathers else None, bar=None if feathers else spine_c)
    REG(name, disp, 'Dragon', factor, parts, host, 'luft', hab, weight, grp, 'Basis Drache (fliegt): Gelenke der Basis, Form/Gesicht eigen', AA + ['hals=1'])
