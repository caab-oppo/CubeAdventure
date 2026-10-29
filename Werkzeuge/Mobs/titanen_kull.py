# titanen_kull.py (2026-10-19) -- zwei neue Voelker als eigene Rassen:
#   TITANEN (griechische Mythologie): riesige, edle Urgoetter-Gestalten mit Bart, Lorbeer/Krone, Chiton (Gewand mit
#            Goldsaum), je nach Titan Licht (Hyperion), Meer (Okeanos), Zeit (Kronos), Himmelstraeger (Atlas), Feuer
#            (Prometheus). Basis Cyclops (grosses Menschen-Skelett), Groesse x2.6..3.2.
#   KULL (Eragon/Paolini): die Elite der Urgals -- 8-9 Fuss gross, graue Haut, gelbe Augen, Widderhoerner, die sich
#            von den Schlaefen nach hinten/unten winden, schwere Stirn, Hauer im Unterbiss, grobe Ruestung.
#            Basis Ogre (Unterbiss-Kopf, breiter Rumpf), Groesse x1.6..1.8.
# Jedes Teil hat GENAU die Masse des nativen Teils der Basisrasse (Drehpunkt = Mitte), Gesicht vorne (+y), oben +z.
# Aufruf (aus Projekt\Werkzeuge\Mobs):  python titanen_kull.py [Strukturen-Ordner]
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tierbau as T
from tierbau import V, mob, mix, HEAD, HAIR, HANDS, FEET, BODY

if len(sys.argv) > 1: T.OUT = sys.argv[1]
T.CFG.update({'race_base': 380, 'mob_dir': 'Voelker', 'part_dir': 'Voelker'})
WHITE, BLACK, GOLD = (236, 234, 226), (24, 22, 22), (232, 186, 60)

def front(v, x, z):
    x, z = int(round(x)), int(round(z))
    if not (0 <= x < v.sx and 0 <= z < v.sz): return None
    ys = [y for y in range(v.sy) if v.o[z, y, x]]
    return max(ys) if ys else None
def side(v, y, z, sg):
    y, z = int(round(y)), int(round(z))
    xs = [x for x in range(v.sx) if v.o[z, y, x]]
    if not xs: return None
    return max(xs) if sg > 0 else min(xs)

# ---------------------------------------------------------------------------------------------- Titanen
def titan_head(tag, skin, hair, beard, iris, crown=None, crown_kind='laurel', glow=False):
    v = V(18, 16, 20, tag)
    shade = mix(skin, BLACK, 0.18)
    v.ell(9, 7.6, 11.2, 6.2, 6.0, 7.2, skin)                                   # Schaedel
    v.ell(9, 9.2, 6.4, 4.8, 4.6, 3.8, skin)                                    # Kiefer/Wangen
    for z in range(9, 13): v.box(8, 13, z, 9, 14, z, mix(skin, BLACK, 0.08) if z < 12 else skin)   # Nase
    v.put(8, 14, 8, shade); v.put(9, 14, 8, shade)
    for sg in (-1, 1):                                                           # Augen, Brauen
        ex = 9 + sg * 2.6 - (1 if sg < 0 else 0)
        fy = front(v, ex, 12) or 13
        v.put(ex, fy, 12, WHITE, 0); v.put(ex + (1 if sg < 0 else -1), fy, 12, iris if not glow else mix(iris, WHITE, 0.3), 0)
        for dx in (-1, 0, 1): v.put(ex + dx, (front(v, ex + dx, 13) or 13), 13, mix(hair, BLACK, 0.25), 3)
        if glow: v.put(ex + (1 if sg < 0 else -1), fy, 11, iris, 0)
    # Bart (laesst den Mund frei), Schnurrbart
    v.ell(9, 10.6, 4.6, 5.0, 4.4, 3.6, beard, over=True, cut=lambda x, y, z: y > 7)
    v.ell(9, 12.8, 3.0, 3.0, 2.4, 2.6, mix(beard, BLACK, 0.12), cut=lambda x, y, z: y > 9)
    for x in range(6, 12): v.put(x, front(v, x, 8) or 14, 8, mix(beard, BLACK, 0.1))
    for x in (7, 8, 9, 10): v.put(x, (front(v, x, 7) or 14), 7, (120, 50, 46), 0)  # Mund
    # Haar: oben und hinten, lockig
    v.ell(9, 6.6, 13.6, 6.8, 6.6, 5.8, lambda x, y, z: hair if (z + (y - 6) * 0.15) % 3 < 2.2 else mix(hair, BLACK, 0.2), over=True,
          cut=lambda x, y, z: z >= 13 or y <= 5)
    for sg in (-1, 1): v.ell(9 + sg * 5.6, 6.0, 9.5, 1.6, 3.5, 4.5, hair, over=True)   # Haar seitlich bis zum Bart
    # Krone / Lorbeer
    if crown:
        for a in range(0, 360, 12):
            r = math.radians(a); x = 9 + math.cos(r) * 6.4; y = 7.2 + math.sin(r) * 6.2; z = 15.6
            v.put(x, y, z, crown, 4)
            if crown_kind == 'laurel' and a % 36 == 0: v.put(x, y, z + 1, mix(crown, (40, 120, 40), 0.35), 4)
            if crown_kind == 'rays' and a % 24 == 0 and math.sin(r) > -0.3:
                for k in range(1, 4): v.put(9 + math.cos(r) * (6.4 + k * 0.3), 7.2 + math.sin(r) * (6.2 + k * 0.3), z + k, mix(crown, WHITE, k * 0.15), 2)
            if crown_kind == 'helm' : v.box(x, y, z - 2, x, y, z, crown, 3)
            if crown_kind == 'coral' and a % 30 == 0:
                for k in range(1, 3 + (a // 30) % 2): v.put(x, y, z + k, mix(crown, (255, 120, 110), k * 0.2), 3)
            if crown_kind == 'flame' and a % 24 == 0 and math.sin(r) > -0.4:
                for k in range(1, 4): v.put(x, y, z + k, mix((255, 200, 60), (230, 60, 20), k / 3), 0)
    return v

def titan_body(tag, skin, cloth, trim, belt, armor=None, globe=None):
    v = V(16, 10, 20, tag)
    v.ell(8, 5, 13.5, 6.4, 4.4, 6.6, skin)                                      # Brust/Schultern
    v.ell(8, 5, 7.5, 5.2, 4.0, 4.6, skin)                                       # Bauch
    for sg in (-1, 1): v.ell(8 + sg * 3.0, 7.6, 14.0, 2.6, 1.2, 2.2, mix(skin, WHITE, 0.06))   # Brustmuskeln
    # Chiton: von der linken Schulter schraeg ueber die Brust, ganz um den Unterleib
    def cloth_c(x, y, z):
        return trim if z < 5 or abs(z - (7 + (x - 2) * 0.55)) < 0.6 else cloth
    v.ell(8, 5, 6.5, 5.8, 4.5, 3.6, cloth_c, over=True)
    for z in range(5, 19):
        for x in range(2, 15):
            if z <= 7 + (x - 2) * 0.55 + 1.2:
                for y in range(10):
                    if v.o[z, y, x]: v.put(x, y, z, cloth_c(x, y, z), 4)
    for x in range(2, 15):                                                       # Guertel
        for y in range(10):
            if v.o[8, y, x]: v.put(x, y, 8, belt, 3)
    v.put(8, front(v, 8, 8) or 9, 8, GOLD, 0)
    if armor:                                                                     # Brustpanzer (Kronos)
        for z in range(10, 18):
            for x in range(3, 14):
                fy = front(v, x, z)
                if fy is not None: v.put(x, fy, z, armor if (x + z) % 4 else mix(armor, WHITE, 0.25), 3)
    if globe:                                                                     # Atlas: Himmelskugel auf dem Ruecken
        v.ell(8, 2.0, 16.0, 4.2, 2.0, 4.0, lambda x, y, z: globe if (x + z) % 3 else mix(globe, WHITE, 0.4), over=False)
    return v

def titan_hand(tag, skin, bracer):
    v = V(8, 12, 8, tag)
    v.ell(4, 6.5, 4, 3.4, 4.6, 3.4, skin)
    for x in range(1, 7): v.put(x, front(v, x, 5) or 10, 5, mix(skin, BLACK, 0.15))   # Knoechel
    v.ell(4, 2.0, 4, 3.6, 2.0, 3.6, bracer, over=True)                          # Armreif
    return v

def titan_foot(tag, skin, sandal):
    v = V(5, 9, 6, tag)
    v.ell(2.5, 4.5, 2.6, 2.3, 4.2, 2.4, skin)
    for y in range(9):
        for x in range(5):
            if v.o[0, y, x]: v.put(x, y, 0, sandal, 3)
            if v.o[1, y, x]: v.put(x, y, 1, sandal, 3)
    for z in (2, 3, 4): v.put(2, 3 + z, z, sandal, 2)                          # Riemen
    return v

TITANS = [
    # name, Anzeige, Haut, Haar, Bart, Iris, Krone, Kronenart, Gewand, Saum, Guertel, Zusatz
    ('titan-hyperion', 'Lichttitan',      (214, 168, 96),  (250, 232, 170), (246, 222, 150), (255, 210, 60),  GOLD,            'rays',  (248, 244, 230), GOLD,            (150, 100, 40), {'glow': True}, 3.0),
    ('titan-okeanos',  'Meerestitan',     (88, 150, 160),  (40, 110, 110),  (60, 140, 120),  (110, 240, 255), (240, 120, 100), 'coral', (40, 90, 120),   (220, 240, 250), (30, 70, 80),   {'glow': True}, 3.0),
    ('titan-kronos',   'Zeittitan',       (150, 146, 150), (60, 56, 64),    (90, 86, 94),    (230, 40, 40),   (110, 110, 120), 'helm',  (60, 40, 70),    (190, 160, 60),  (40, 30, 30),   {'armor': (120, 120, 130)}, 3.2),
    ('titan-atlas',    'Himmelstraeger',  (190, 140, 100), (70, 50, 36),    (90, 64, 44),    (90, 140, 220),  (170, 200, 120), 'laurel', (230, 220, 200), (160, 40, 40),   (110, 70, 40),  {'globe': (70, 110, 210)}, 2.8),
    ('titan-prometheus', 'Feuertitan',    (184, 96, 64),   (240, 140, 40),  (200, 90, 40),   (255, 160, 40),  (255, 170, 50),  'flame', (120, 40, 30),   (250, 190, 60),  (60, 30, 20),   {'glow': True}, 2.6),
]
for (name, disp, skin, hair, beard, iris, crown, ck, cloth, trim, belt, extra, fac) in TITANS:
    head = titan_head(name + 'h', skin, hair, beard, iris, crown, ck, glow=extra.get('glow', False))
    body = titan_body(name + 'b', skin, cloth, trim, belt, armor=extra.get('armor'), globe=extra.get('globe'))
    mob(name, disp, 'Cyclops', fac, {HEAD: head, BODY: body, HANDS: titan_hand(name + 'a', skin, trim), FEET: titan_foot(name + 'f', skin, belt)},
        host=1, note='Titan (griechische Mythologie), Basis Cyclops', extra=('auto_ausrichtung=0',))

# ---------------------------------------------------------------------------------------------- Kull
def ram_horn(v, sg, c, tip, thick=1.6):
    """Widderhorn: an der Schlaefe ansetzen, nach hinten-oben, dann in einer Spirale nach unten und wieder nach vorn."""
    cx0 = 9 + sg * 5.2
    pts = []
    for i in range(26):
        t = i / 25.0; a = math.radians(-40 + t * 300)                         # Drehwinkel um die x-Achse (y-z-Ebene)
        r = 3.6 - t * 1.4
        pts.append((cx0 + sg * (0.6 + t * 2.2), 7.0 - math.cos(a) * r * 0.9, 13.0 + math.sin(a) * r))
    for i in range(len(pts) - 1):
        k = i / (len(pts) - 1)
        v.cap(pts[i], pts[i + 1], thick * (1 - k * 0.55), thick * (1 - (k + 0.04) * 0.55),
              mix(c, tip, k ** 1.5) if (i % 3) else mix(mix(c, tip, k ** 1.5), BLACK, 0.18), noise=4)

def kull_head(tag, skin, iris, horn, horn_tip, hair=None, paint=None, helm=None):
    v = V(18, 16, 20, tag)
    dark = mix(skin, BLACK, 0.25)
    v.ell(9, 7.4, 10.8, 5.6, 5.8, 6.6, skin)                                   # Schaedel (flacher, hinten breit)
    v.ell(9, 10.0, 4.6, 5.4, 5.2, 3.4, skin)                                   # Unterkiefer, vorgeschoben (Unterbiss)
    v.ell(9, 11.6, 11.4, 5.4, 2.2, 1.4, dark)                                  # Stirnwulst
    for z in range(7, 11): v.box(8, 12, z, 9, 13, z, mix(skin, BLACK, 0.1))    # breite Nase
    v.put(8, 13, 7, BLACK); v.put(9, 13, 7, BLACK)
    for sg in (-1, 1):                                                           # gelbe Augen unter dem Wulst
        ex = 9 + sg * 2.6 - (1 if sg < 0 else 0)
        fy = max(front(v, ex, 10) or 12, front(v, ex, 11) or 12)               # buendig unter dem Stirnwulst
        v.put(ex, fy, 10, iris, 0); v.put(ex + (1 if sg < 0 else -1), fy, 10, mix(iris, (255, 255, 200), 0.3), 0)
    fyj = front(v, 9, 5) or 14
    for x in range(6, 12): v.put(x, fyj, 6, (60, 30, 30), 0)                   # Maul
    for sg in (-1, 1):                                                           # Hauer aus dem Unterkiefer
        hx = 9 + sg * 2.4 - (1 if sg < 0 else 0)
        for k in range(3): v.put(hx, fyj, 6 + k, (236, 226, 196), 0)
    for sg in (-1, 1): v.ell(9 + sg * 5.8, 8.0, 10.0, 0.9, 1.6, 1.6, dark)      # spitze Ohren
    if paint:                                                                    # Kriegsbemalung quer ueber die Augen
        for x in range(4, 14):
            fy = front(v, x, 9)
            if fy is not None: v.put(x, fy, 9, paint, 2)
    if hair:                                                                     # Zopf am Hinterkopf
        v.ell(9, 2.6, 14.0, 2.6, 2.0, 3.0, hair, over=True)
        for z in range(3, 13): v.put(9, 1, z, mix(hair, BLACK, 0.15 if z % 2 else 0.0))
    if helm:
        v.ell(9, 7.2, 14.4, 6.0, 6.0, 3.4, lambda x, y, z: helm if (x + y) % 5 else mix(helm, WHITE, 0.3), over=True, cut=lambda x, y, z: z >= 13)
    ram_horn(v, -1, horn, horn_tip); ram_horn(v, 1, horn, horn_tip)
    return v

def kull_body(tag, skin, leather, metal, fur=None, bare=False, robe=None, bones=False, scars=None):
    v = V(12, 12, 14, tag)
    v.ell(6, 6, 7.4, 5.6, 5.4, 6.4, skin)                                       # breiter, massiger Rumpf
    v.ell(6, 6.8, 10.6, 5.8, 4.6, 2.6, skin)                                    # Schultern
    if robe:                                                                      # Schamane: Robe bis unten
        v.ell(6, 6, 5.0, 5.8, 5.6, 4.8, robe, over=True)
    elif not bare:                                                               # Lederharnisch mit Eisenplatten
        for z in range(2, 11):
            for y in range(12):
                for x in range(12):
                    if v.o[z, y, x]: v.put(x, y, z, metal if (z in (6, 9) or (x in (3, 8) and z > 5)) else leather, 4)
    for x in range(12):                                                          # Guertel
        for y in range(12):
            if v.o[3, y, x]: v.put(x, y, 3, mix(leather, BLACK, 0.35), 3)
    v.put(6, front(v, 6, 3) or 11, 3, metal, 0)
    if fur:                                                                       # Fellmantel ueber den Schultern
        v.ell(6, 4.6, 11.4, 6.0, 4.4, 2.4, fur, over=True, cut=lambda x, y, z: z >= 10 or y < 5)
    if bones:                                                                     # Knochenkette
        for a in range(0, 360, 30):
            r = math.radians(a); x = 6 + math.cos(r) * 4.4; y = 6 + math.sin(r) * 4.6
            if math.sin(r) > -0.2: v.put(x, y, 10 - abs(math.cos(r)) * 2, (230, 222, 196), 0)
    if scars:
        for k in range(4):
            for t in range(5): v.put(3 + t + k, front(v, 3 + t + k, 5 + t) or 11, 5 + t, scars, 0)
    return v

def kull_hand(tag, skin, wrap):
    v = V(7, 8, 6, tag)
    v.ell(3.5, 4.2, 3.0, 3.3, 3.7, 2.8, skin)
    for x in (1, 3, 5): v.put(x, front(v, x, 2) or 7, 2, (40, 36, 30), 0)       # dunkle Krallen
    for x in range(7):
        for z in range(6):
            if v.o[z, 1, x]: v.put(x, 1, z, wrap, 3)
    return v

def kull_foot(tag, skin, boot):
    v = V(7, 9, 4, tag)
    v.ell(3.5, 4.4, 1.8, 3.3, 4.3, 1.9, boot)
    for x in (1, 3, 5): v.put(x, front(v, x, 1) or 8, 1, skin)                  # Zehen
    return v

KULL = [
    # name, Anzeige, Haut, Augen, Horn, Hornspitze, Haar, Bemalung, Helm, Leder, Metall, Fell, nackt, Robe, Knochen, Narben, Groesse
    ('kull-krieger',   'Kull',            (128, 130, 128), (240, 210, 40),  (176, 160, 130), (90, 80, 64),  (30, 26, 24), None,           None,            (96, 70, 46), (120, 120, 126), None,          False, None,          False, None,           1.7),
    ('kull-haeuptling','Kull-Haeuptling', (104, 104, 108), (255, 200, 30),  (150, 130, 100), GOLD,          (20, 18, 18), (150, 30, 30),  None,            (70, 50, 34), (170, 140, 70),  (90, 70, 50),  False, None,          True,  None,           1.8),
    ('kull-schamane',  'Kull-Schamane',   (140, 132, 116), (230, 230, 80),  (190, 180, 160), (110, 100, 80),None,         (200, 200, 220),None,            (80, 60, 40), (110, 100, 90),  None,          False, (70, 50, 90),  True,  None,           1.6),
    ('kull-berserker', 'Kull-Berserker',  (140, 116, 110), (255, 170, 20),  (160, 140, 110), (60, 50, 40),  (60, 20, 20), (190, 30, 30),  None,            (90, 64, 40), (110, 100, 96),  None,          True,  None,          False, (90, 50, 50),   1.75),
    ('kull-waechter',  'Kull-Waechter',   (112, 122, 132), (240, 220, 60),  (180, 170, 150), (100, 96, 90), None,         None,           (130, 130, 140), (70, 60, 50), (150, 150, 160), None,          False, None,          False, None,           1.8),
]
for (name, disp, skin, iris, horn, tip, hair, paint, helm, leather, metal, fur, bare, robe, bones, scars, fac) in KULL:
    head = kull_head(name + 'h', skin, iris, horn, tip, hair=hair, paint=paint, helm=helm)
    body = kull_body(name + 'b', skin, leather, metal, fur=fur, bare=bare, robe=robe, bones=bones, scars=scars)
    mob(name, disp, 'Ogre', fac, {HEAD: head, BODY: body, HANDS: kull_hand(name + 'a', skin, leather), FEET: kull_foot(name + 'f', skin, mix(leather, BLACK, 0.2))},
        host=1, note='Kull (Eragon: Elite der Urgals), Basis Ogre', extra=('auto_ausrichtung=0',))

print(len(T.MOBS), 'Mobs:', ', '.join(f'{n} ({r})' for n, d, b, r, _ in T.MOBS))
