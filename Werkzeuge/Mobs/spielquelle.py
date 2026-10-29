"""Liest Rassen-Aussehen aus dem Infinite-Cubes-Quelltext (alpha/src/cube/CreatureStats.cpp fillAppearance).

Nur als QUELLE fuer Werte/Logik -- keine Adressen. Pfad per Umgebungsvariable INFINITE_CUBES
(Ordner, der "alpha" enthaelt) oder ..\Infinite-Cubes-main neben diesem Repository.
"""
import os, re, glob

def _root():
    here = os.path.dirname(os.path.abspath(__file__))
    for p in [os.environ.get("INFINITE_CUBES", ""),
              os.path.join(here, "..", "..", "..", "Infinite-Cubes-main"),
              os.path.join(here, "..", "..", "..", "Infinite-Cubes-main", "Infinite-Cubes-main")]:
        if p and os.path.isdir(os.path.join(p, "alpha")):
            return os.path.join(p, "alpha")
    raise SystemExit("Infinite-Cubes nicht gefunden (INFINITE_CUBES setzen)")

class V(list):
    def __init__(self, *a): super().__init__(float(x) for x in a)
    def __mul__(self, f): return V(*[x * f for x in self])
    __rmul__ = __mul__

def _enum(path, prefix):
    out = {}
    for m in re.finditer(r"\b(%s\w*)\s*=\s*(0x[0-9a-fA-F]+|\d+)" % prefix, open(path, encoding="utf-8", errors="replace").read()):
        out[m.group(1)] = int(m.group(2), 0)
    return out

_cache = {}
def tables():
    if _cache: return _cache
    A = _root()
    races = _enum(os.path.join(A, "include/cube/RaceIds.h"), "Race")
    sprites = _enum(os.path.join(A, "include/cube/SpriteIds.h"), "Sprite")
    counts = {}
    for m in re.finditer(r"\b(\w+VariantCount)\s*=\s*([^,\n]+)", open(os.path.join(A, "include/cube/SpriteIds.h"), encoding="utf-8", errors="replace").read()):
        counts[m.group(1)] = m.group(2)
    files = {}
    for m in re.finditer(r"m_sprites\[(Sprite\w+)\]->load\(path \+ \"([^\"]+)\"", open(os.path.join(A, "src/cube/SpriteManager.cpp"), encoding="utf-8", errors="replace").read()):
        if m.group(1) in sprites: files[sprites[m.group(1)]] = m.group(2)
    flags = _enum(os.path.join(A, "include/cube/CreatureIds.h"), "AppearanceFlag")
    src = open(os.path.join(A, "src/cube/CreatureStats.cpp"), encoding="utf-8", errors="replace").read()
    body = src[src.index("void fillAppearance("):]
    sw = body.index("switch (race) {")
    pre = body[:sw]
    body = body[sw:body.index("\n    default:\n        break;\n    }\n}")]
    env = dict(V=V, RAND_MAX=32767.0)
    env.update(sprites); env.update(flags)
    def ev(expr):
        e = expr.strip()
        e = re.sub(r"plasma::Vector<3,\s*float>\(", "V(", e)
        e = re.sub(r"plasma::Vector<3,\s*unsigned char>\(", "V(", e)
        e = re.sub(r"(\d+\.\d*|\d+)f\b", r"\1", e)
        e = e.replace("rand()", "16383.0").replace("float(", "float(").replace("style->", "")
        for k, v in counts.items():
            e = re.sub(r"\b%s\b" % k, "(%s)" % v, e)
        e = e.replace("<<", "<<").replace("||", " or ").replace("&&", " and ")
        try:
            return eval(e, env)
        except Exception:
            return None
    result = {}
    for block in re.split(r"\n(?=    case Race)", body)[1:]:
        labels = re.findall(r"case (Race\w+):", block)
        app = {}
        for m in re.finditer(r"appearance\.(\w+)\s*(\|?=)\s*([^;]+);", block, re.S):
            k, op, v = m.group(1), m.group(2), " ".join(m.group(3).split())
            val = ev(v)
            if val is None and k.endswith("Model") and "appearance." in v:
                val = app.get(v.split("appearance.")[1].strip())
            if val is None: continue
            if op == "|=": app[k] = int(app.get(k, 0)) | int(val)
            else: app[k] = val
        for l in labels:
            if l in races: result[races[l]] = app
    _cache.update(dict(races=races, sprites=sprites, files=files, appearance=result, flags=flags))
    return _cache

DEFAULT = dict(size=V(0.8, 0.8, 1.8) * 1.2, headModel=-1, hairModel=-1, handsModel=-1, feetModel=-1, chestModel=-1, tailModel=-1,
               shoulderModel=-1, wingsModel=-1, headScale=1.01, chestScale=1.0, handsScale=1.0, feetScale=0.98, shoulderModelScale=1.0,
               weaponScale=0.95, tailScale=0.8, shoulderArmorScale=1.0, wingScale=1.0, chestRotation=0.0, handsRotation=V(0, 0, 0),
               feetRotation=0.0, wingsRotation=0.0, tailRotation=0.0, chestPosition=V(0, 0, -5), headPosition=V(0, 0.5, 5),
               handsPosition=V(6, 0, 0), feetPosition=V(3, 1, -10.5), tailPosition=V(0, -8, 2), wingsPosition=V(0, 0, 0), appearanceFlags=0)

def appearance(race):
    t = tables(); a = dict(DEFAULT); a.update(t["appearance"].get(race, {}))
    return a

def model_file(index):
    return tables()["files"].get(int(index))
