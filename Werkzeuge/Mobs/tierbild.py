#!/usr/bin/env python3
"""Tier-Werkstatt: eigene Tiere (.mob) so zusammensetzen, wie das Spiel sie zeichnet, und Teile ausrichten.

Rechnung 1:1 nach Infinite-Cubes alpha/src/cube/Animation.cpp (Creature::AnimationState::addSprites), Ruhehaltung
(keine Schritte, kein Wippen): Modell-Einheiten, Teil-Mitte an seiner Position, Skala je Teil, rechte Hand gespiegelt,
Schwanz ab Koerper + Schwanzposition nach hinten, Fluegel um -90/90 gedreht und -45 Grad angelegt.
Grundwerte je Basisrasse aus fillAppearance (spielquelle.py). Gesamtgroesse: Aussehen.size[0] / 11.2 je Einheit.

  python tierbild.py <Strukturen>\\Mobs [--bilder] [--ausrichten] [--nur name1,name2]
"""
import os, sys, re, math, struct
import numpy as np
import spielquelle as SQ
import cubdb

P_HEAD, P_HAIR, P_HANDS, P_FEET, P_BODY, P_TAIL, P_SHOULDER, P_WINGS = range(8)
FIELD_MODEL = ["headModel", "hairModel", "handsModel", "feetModel", "chestModel", "tailModel", "shoulderModel", "wingsModel"]
FIELD_SCALE = ["headScale", "headScale", "handsScale", "feetScale", "chestScale", "tailScale", "shoulderArmorScale", "wingScale"]
FIELD_POS = ["headPosition", "headPosition", "handsPosition", "feetPosition", "chestPosition", "tailPosition", None, "wingsPosition"]

# ---- Modelle laden ---------------------------------------------------------------------------------------------
_mcache = {}
def load_cub_file(path):
    b = open(path, "rb").read(); sx, sy, sz = struct.unpack_from("<3i", b, 0)
    return np.frombuffer(b, np.uint8, offset=12).reshape(sz, sy, sx, 3).copy()
def native_model(index):
    if index in _mcache: return _mcache[index]
    f = SQ.model_file(index); a = None
    if f:
        r = cubdb.get(f)
        if r: sx, sy, sz, d = r; a = np.frombuffer(d, np.uint8).reshape(sz, sy, sx, 3).copy()
    _mcache[index] = a
    return a

# ---- .mob ------------------------------------------------------------------------------------------------------
def read_mob(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    kv = {}; parts = {}
    for l in lines:
        if not l or l.startswith("#") or "=" not in l: continue
        k, v = l.split("=", 1)
        if k == "teil":
            f = v.split(";")
            parts[int(f[0])] = dict(src=int(f[1]), idx=int(f[2]), file=f[3], scale=float(f[4]), setpos=int(f[5]) != 0,
                                    pos=[float(f[6]), float(f[7]), float(f[8])])
        else: kv[k] = v
    return dict(path=path, lines=lines, kv=kv, parts=parts)

def write_mob(m):
    out = []; done = set()
    for l in m["lines"]:
        if l.startswith("teil="):
            n = int(l[5:].split(";")[0]); p = m["parts"][n]; done.add(n)
            out.append("teil=%d;%d;%d;%s;%g;%d;%g;%g;%g" % (n, p["src"], p["idx"], p["file"], round(p["scale"], 3), 1 if p["setpos"] else 0,
                                                           round(p["pos"][0], 2), round(p["pos"][1], 2), round(p["pos"][2], 2)))
        else: out.append(l)
    open(m["path"], "w", encoding="utf-8").write("\n".join(out) + "\n")

def resolve(m):
    """Wirksame Teile: Modell (Voxel), Skala, Position -- wie MobLab::ApplyToSpawn ueber dem Aussehen der Basisrasse."""
    base = int(m["kv"].get("basis", 2)); app = SQ.appearance(base)
    eff = {}
    for i in range(8):
        p = m["parts"].get(i, dict(src=0, idx=-1, file="", scale=0, setpos=False, pos=[0, 0, 0]))
        if p["src"] == 1: continue
        if p["src"] == 0: vox = native_model(app[FIELD_MODEL[i]]) if app[FIELD_MODEL[i]] >= 0 else None
        elif p["src"] == 2: vox = native_model(p["idx"])
        else:
            fp = os.path.normpath(os.path.join(os.path.dirname(m["path"]), p["file"].replace("\\", os.sep)))
            vox = load_cub_file(fp) if os.path.exists(fp) else None
        if vox is None: continue
        sc = p["scale"] if p["scale"] > 0 else app[FIELD_SCALE[i]]
        pos = list(p["pos"]) if (p["setpos"] and FIELD_POS[i]) else (list(app[FIELD_POS[i]]) if FIELD_POS[i] else [0, 0, 0])
        eff[i] = dict(vox=vox, scale=float(sc), pos=pos)
    quad = bool(int(app["appearanceFlags"]) & 1)
    size = list(app["size"]); f = float(m["kv"].get("faktor", 1) or 1)
    return eff, quad, app, [s * f for s in size]

# ---- Rechnung wie Animation.cpp ---------------------------------------------------------------------------------
def T(x, y, z): M = np.eye(4); M[:3, 3] = [x, y, z]; return M
def S(x, y, z): return np.diag([x, y, z, 1.0])
def R(ax, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg)); M = np.eye(4)
    i, j = {"x": (1, 2), "y": (2, 0), "z": (0, 1)}[ax]
    M[i, i] = c; M[i, j] = -s; M[j, i] = s; M[j, j] = c
    return M
def part_mats(eff, quad, app, race=-1, walk=None, joints=None):
    """Haltung wie Animation.cpp: Koerper/Kopf/Schwanz im Koerperrahmen (RX(chestRotation)), Kopf dreht die Neigung
    zurueck; Haende/Fluegel im Haenderahmen; Fuesse RX(feetRotation + Schwung) um den MODELLURSPRUNG, dann verschoben,
    dann noch einmal RX(Schwung) um die Fussmitte. walk = (Amplitude 0..1, Phase rad) -> Gehbewegung, None = Ruhe.
    Gehen (Animation.cpp:147-262): cycle = sin(phase + pi/2), swing = A * 2.199 * cycle (Vierbeiner x0.75),
    Fussschwung = deg(swing) (+-, beide Vorzeichen x1.5), Fusshub links A*4*cycle, rechts (1-cycle)*A*4,
    Schritthoehe |swing|*3 an den Fuessen, Koerper +|swing|*4 (Kopf +|swing|*2), Vorderbeine wie Fuesse der Gegenseite."""
    # joints: Gelenk-Modus (MobLab::InstallJointPatch, eigene Rassen >= 200): die erste Schwungdrehung der Beine um den
    # Modellursprung entfaellt (Aufrufe 0x423059/0x423405/0x423957/0x423B83 -> Winkel 0) -> Beine schwingen nur um ihre
    # Modellmitte, und die liegt durch Auffuellen der .cub im Gelenk (Huefte/Schulter). None = nach Rasse (>= 200).
    if joints is None: joints = race >= 200
    out = []   # (teil, Matrix, Voxel)
    def center(v): sz, sy, sx, _ = v.shape; return T(-sx * 0.5, -sy * 0.5, -sz * 0.5)
    A, ph = walk if walk else (0.0, 0.0)
    cycle = math.sin(ph + math.pi / 2); sw = A * 2.199114820062152 * cycle * (0.75 if quad else 1.0)
    lfs = math.degrees(sw) * 1.5; rfs = -math.degrees(sw) * 1.5
    lLift = A * 4.0 * cycle; rLift = (1.0 - cycle) * A * 4.0; stride = abs(sw); bob = stride * 4.0
    if not walk: lfs = rfs = lLift = rLift = stride = bob = 0.0
    cr = float(app.get("chestRotation", 0)); BODY = T(0, 0, bob) @ R("x", cr)
    if P_BODY in eff:
        e = eff[P_BODY]; out.append((P_BODY, BODY @ T(*e["pos"]) @ S(e["scale"], e["scale"], e["scale"]) @ center(e["vox"]), e["vox"]))
    for k in (P_HEAD, P_HAIR):
        if k in eff:
            e = eff[k]; v = e["vox"]; sz, sy, sx, _ = v.shape
            off = T(-sx * 0.5, -sy * (0.2 if race == 101 else 0.5), -sz * 0.5)   # Rasse 101 (Drache): Kopf weiter hinten am Hals
            out.append((k, BODY @ T(*e["pos"]) @ T(0, 0, -bob * 0.5 - 3) @ R("x", -cr) @ T(0, 0, 3) @ S(e["scale"], e["scale"], e["scale"]) @ off, v))
    if P_HANDS in eff:
        e = eff[P_HANDS]; hx, hy, hz = e["pos"]; s = e["scale"]; HM = T(0, 0, bob)
        if quad:
            yy = hy + 2.1 - A * 5.0 if walk else hy + 2.1
            j1, j2 = (0.0, 0.0) if joints else (rfs, lfs)
            out.append((P_HANDS, HM @ R("x", j1) @ T(-hx, yy, hz) @ R("x", rfs) @ S(s, s, s) @ center(e["vox"]), e["vox"]))
            out.append((P_HANDS, HM @ R("x", j2) @ T(hx, yy, hz) @ R("x", lfs) @ S(-s, s, s) @ center(e["vox"]), e["vox"]))
        else:
            out.append((P_HANDS, T(-hx, hy + 2.1, hz - 4.1) @ S(s, s, s) @ center(e["vox"]), e["vox"]))
            out.append((P_HANDS, T(hx, hy + 2.1, hz - 4.1) @ S(-s, s, s) @ center(e["vox"]), e["vox"]))
    if P_FEET in eff:
        e = eff[P_FEET]; fx, fy, fz = e["pos"]; s = e["scale"]; fr = float(app.get("feetRotation", 0))
        a1, a2 = (fr, 0.0) if joints else (fr + lfs, rfs)          # Gelenk-Patch: nur feetRotation bleibt (TierGelenke.cpp)
        out.append((P_FEET, R("x", a1) @ T(-fx, fy, stride * 3 + fz + lLift) @ S(s, s, s) @ R("x", lfs) @ center(e["vox"]), e["vox"]))
        out.append((P_FEET, R("x", a2) @ T(fx, fy, stride * 3 + fz + rLift) @ S(s, s, s) @ R("x", fr + rfs) @ center(e["vox"]), e["vox"]))
    if P_TAIL in eff and P_BODY in eff:
        e = eff[P_TAIL]; v = e["vox"]; sz, sy, sx, _ = v.shape; cs = eff[P_BODY]["scale"]
        out.append((P_TAIL, BODY @ T(*eff[P_BODY]["pos"]) @ T(*e["pos"]) @ R("x", float(app.get("tailRotation", 0))) @ S(e["scale"] * cs, e["scale"] * cs, e["scale"] * cs) @ T(-sx * 0.5, -sy, -sz * 0.5), v))
    if P_WINGS in eff:
        e = eff[P_WINGS]; v = e["vox"]; sz, sy, sx, _ = v.shape; wx, wy, wz = e["pos"]; s = e["scale"]; wr = float(app.get("wingsRotation", 0))
        for side, flap in ((-1, -45.0), (1, 45.0)):
            M = T(0, 0, bob) @ T(side * wx, wy, wz) @ R("x", wr) @ R("y", flap) @ R("z", -90) @ R("x", 90) @ S(s, s, s) @ T(-sx * 0.5, 0, -sz * 0.5)
            out.append((P_WINGS, M, v))
    return out

def leg_gap(eff, quad, app, race, A=0.21, steps=16):
    """Groesste Luecke (Modell-Einheiten) zwischen Beinoberkante und Koerperunterseite waehrend eines Schritts.
    A = 0.21: Gleichgewicht der Gehamplitude bei ~60 Bildern/s (WorldUpdate.cpp: +Sekunden je Bild, blend 0.005/ms)."""
    if P_BODY not in eff: return 0.0
    worst = -1e9
    for i in range(steps):
        mats = part_mats(eff, quad, app, race, (A, 2 * math.pi * i / steps))
        Mb = [M for k, M, v in mats if k == P_BODY][0]; vb = eff[P_BODY]["vox"]
        idx = np.argwhere(vb.any(3)); pb = np.c_[idx[:, 2] + .5, idx[:, 1] + .5, idx[:, 0] + .5, np.ones(len(idx))] @ Mb.T
        for k, M, v in mats:
            if k not in (P_FEET, P_HANDS) or (k == P_HANDS and not quad): continue
            o = np.argwhere(v.any(3))
            if not len(o): continue
            lo = o.min(0); hi = o.max(0) + 1
            top = np.array([(lo[2] + hi[2]) * 0.5, (lo[1] + hi[1]) * 0.5, hi[0], 1.0]) @ M.T
            sel = (np.abs(pb[:, 0] - top[0]) < 1.5) & (np.abs(pb[:, 1] - top[1]) < 1.5)
            if not sel.any(): worst = max(worst, 99.0); continue
            worst = max(worst, (pb[sel, 2].min() - 0.5 * eff[P_BODY]["scale"]) - top[2])
    return worst

def top_travel(eff, quad, app, race, part, A=0.21, steps=12):
    """Wie weit wandert die Oberkante (Mitte) eines Beins im Schritt (Modell-Einheiten, groesste Abweichung zur Ruhe)."""
    def tops(walk):
        out = []
        for k, M, v in part_mats(eff, quad, app, race, walk):
            if k != part: continue
            o = np.argwhere(v.any(3)); lo = o.min(0); hi = o.max(0) + 1
            out.append((np.array([(lo[2] + hi[2]) * 0.5, (lo[1] + hi[1]) * 0.5, hi[0], 1.0]) @ M.T)[:3])
        return out
    rest = tops(None); worst = 0.0
    for i in range(steps):
        for a, b in zip(rest, tops((A, 2 * math.pi * i / steps))): worst = max(worst, float(np.linalg.norm(a - b)))
    return worst

def leg_gap(eff, quad, app, race, A=0.21, steps=16):
    """Groesste Luecke (Modell-Einheiten) zwischen Beinoberkante und Koerperunterseite waehrend eines Schritts.
    A = 0.21: Gleichgewicht der Gehamplitude bei ~60 Bildern/s (WorldUpdate.cpp: +Sekunden je Bild, blend 0.005/ms)."""
    if P_BODY not in eff: return 0.0
    worst = -1e9
    for i in range(steps):
        mats = part_mats(eff, quad, app, race, (A, 2 * math.pi * i / steps))
        Mb = [M for k, M, v in mats if k == P_BODY][0]; vb = eff[P_BODY]["vox"]
        idx = np.argwhere(vb.any(3)); pb = np.c_[idx[:, 2] + .5, idx[:, 1] + .5, idx[:, 0] + .5, np.ones(len(idx))] @ Mb.T
        for k, M, v in mats:
            if k not in (P_FEET, P_HANDS) or (k == P_HANDS and not quad): continue
            o = np.argwhere(v.any(3))
            if not len(o): continue
            lo = o.min(0); hi = o.max(0) + 1
            top = np.array([(lo[2] + hi[2]) * 0.5, (lo[1] + hi[1]) * 0.5, hi[0], 1.0]) @ M.T
            sel = (np.abs(pb[:, 0] - top[0]) < 1.5) & (np.abs(pb[:, 1] - top[1]) < 1.5)
            if not sel.any(): worst = max(worst, 99.0); continue
            worst = max(worst, (pb[sel, 2].min() - 0.5 * eff[P_BODY]["scale"]) - top[2])
    return worst

def top_travel(eff, quad, app, race, part, A=0.21, steps=12):
    """Wie weit wandert die Oberkante (Mitte) eines Beins im Schritt (Modell-Einheiten, groesste Abweichung zur Ruhe)."""
    def tops(walk):
        out = []
        for k, M, v in part_mats(eff, quad, app, race, walk):
            if k != part: continue
            o = np.argwhere(v.any(3)); lo = o.min(0); hi = o.max(0) + 1
            out.append((np.array([(lo[2] + hi[2]) * 0.5, (lo[1] + hi[1]) * 0.5, hi[0], 1.0]) @ M.T)[:3])
        return out
    rest = tops(None); worst = 0.0
    for i in range(steps):
        for a, b in zip(rest, tops((A, 2 * math.pi * i / steps))): worst = max(worst, float(np.linalg.norm(a - b)))
    return worst

def boxes(mats):
    """Huelle je Teil (Modell-Einheiten) aus den 8 Ecken."""
    bx = {}
    for k, M, v in mats:
        sz, sy, sx, _ = v.shape
        pts = np.array([[x, y, z, 1] for x in (0, sx) for y in (0, sy) for z in (0, sz)], float) @ M.T
        lo, hi = pts[:, :3].min(0), pts[:, :3].max(0)
        bx.setdefault(k, []).append((lo, hi))
    return bx

def rasterize(mats, res=0.5):
    pts = []; cols = []
    for k, M, v in mats:
        s = abs(np.linalg.det(M[:3, :3])) ** (1 / 3); n = max(1, int(math.ceil(s / res)))
        idx = np.argwhere(v.any(3))
        if not len(idx): continue
        c = v[idx[:, 0], idx[:, 1], idx[:, 2]]
        off = (np.arange(n) + 0.5) / n
        for ox in off:
            for oy in off:
                for oz in off:
                    p = np.c_[idx[:, 2] + ox, idx[:, 1] + oy, idx[:, 0] + oz, np.ones(len(idx))] @ M.T
                    pts.append(p[:, :3]); cols.append(c)
    P = np.vstack(pts); C = np.vstack(cols)
    q = np.floor(P / res).astype(int); q -= q.min(0)
    dim = q.max(0) + 1
    g = np.zeros((dim[2], dim[1], dim[0], 3), np.uint8)
    g[q[:, 2], q[:, 1], q[:, 0]] = C
    return g

def render_png(g, path, s=4):
    from PIL import Image, ImageDraw
    sz, sy, sx, _ = g.shape
    a = g[:, ::-1, :, :]                                   # Blick von vorne-links
    solid = a.any(3)
    pad = np.zeros((sz + 2, sy + 2, sx + 2), bool); pad[1:-1, 1:-1, 1:-1] = solid
    top = solid & ~pad[2:, 1:-1, 1:-1]; front = solid & ~pad[1:-1, 2:, 1:-1]; left = solid & ~pad[1:-1, 1:-1, 2:]
    zs, ys, xs = np.nonzero(top | front | left); order = np.lexsort((zs, xs + ys))
    cx = 0.866 * s; W = int((sx + sy) * cx) + 20; H = int(sz * s + (sx + sy) * s * 0.5) + 20
    img = Image.new("RGB", (W, H), (40, 44, 54)); d = ImageDraw.Draw(img)
    ox = sy * cx + 10; oy = sz * s + 10
    P = lambda x, y, z: (ox + (x - y) * cx, oy - z * s + (x + y) * s * 0.5)
    for i in order:
        x, y, z = int(xs[i]), int(ys[i]), int(zs[i]); c = tuple(int(v) for v in a[z, y, x])
        if top[z, y, x]: d.polygon([P(x, y, z + 1), P(x + 1, y, z + 1), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=c)
        if front[z, y, x]: d.polygon([P(x, y + 1, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x, y + 1, z + 1)], fill=tuple(int(v * .8) for v in c))
        if left[z, y, x]: d.polygon([P(x + 1, y, z), P(x + 1, y + 1, z), P(x + 1, y + 1, z + 1), P(x + 1, y, z + 1)], fill=tuple(int(v * .62) for v in c))
    img.save(path)

# ---- Ausrichtung -------------------------------------------------------------------------------------------------
def draw_race(m):
    """Rasse, mit der das Spiel zeichnet (Kreatur+0x64): eigene Rasse, wenn rasse_setzen, sonst die Basis."""
    kv = m["kv"]; own = int(kv.get("eigene_rasse", 0) or 0)
    return own if own >= 200 and kv.get("rasse_setzen", "1") != "0" else int(kv.get("basis", 2))

def occ_boxes(mats):
    """Huelle der BESETZTEN Voxel je Teil-Instanz (Modell-Einheiten): [(lo, hi)] je Teil, Reihenfolge wie part_mats."""
    out = {}
    for k, M, v in mats:
        idx = np.argwhere(v.any(3))
        if not len(idx): continue
        lo = idx.min(0); hi = idx.max(0) + 1
        pts = np.array([[x, y, z, 1] for x in (lo[2], hi[2]) for y in (lo[1], hi[1]) for z in (lo[0], hi[0])], float) @ M.T
        out.setdefault(k, []).append((pts[:, :3].min(0), pts[:, :3].max(0)))
    return out

def ground_z(app):
    """Boden in Modell-Einheiten: Position = Mitte der Kollisionshoehe (size[2]), Modell-Einheit = size[0]/11.2."""
    return -5.6 * app["size"][2] / app["size"][0]

def ausrichten(m, log=None):
    """Teile an Koerper und Boden setzen (alle Positionen/Skalen danach explizit, Pos=1). Rueckgabe: Liste der Aenderungen."""
    eff, quad, app, size = resolve(m); race = draw_race(m); kv = m["kv"]; notes = []
    if P_BODY not in eff: return ["kein Koerper"]
    swim = kv.get("schwimmt", "0") == "1"
    if quad and P_HANDS not in eff and P_FEET in eff and "wyvern" not in kv.get("name", "").lower():
        # Vierbeiner zeichnet die Vorderbeine mit dem Haende-Teil -- ohne es schwebt die Brust: Fuesse auch vorne
        fp = m["parts"][P_FEET]; m["parts"][P_HANDS] = dict(fp, pos=list(fp["pos"])); eff[P_HANDS] = dict(eff[P_FEET], pos=list(eff[P_FEET]["pos"]))
        notes.append("Vorderbeine = Fuesse")
    cr = float(app.get("chestRotation", 0)); fr = float(app.get("feetRotation", 0))
    RB = R("x", cr)[:3, :3]; RF = R("x", fr)[:3, :3]
    def box(k, inst=-1):
        bx = occ_boxes(part_mats(eff, quad, app, race)).get(k)
        if not bx: return None
        if inst >= 0: return bx[inst]
        return np.min([b[0] for b in bx], 0), np.max([b[1] for b in bx], 0)
    def body_bottom_over(b):              # tiefster Koerper-Voxel ueber der Grundflaeche eines Beins (geneigte Koerper!)
        e = eff[P_BODY]; M = [Mm for kk, Mm, v in part_mats(eff, quad, app, race) if kk == P_BODY][0]
        idx = np.argwhere(e["vox"].any(3)); p = np.c_[idx[:, 2] + .5, idx[:, 1] + .5, idx[:, 0] + .5, np.ones(len(idx))] @ M.T
        lo, hi = b; sel = (p[:, 0] >= lo[0] - 1) & (p[:, 0] <= hi[0] + 1) & (p[:, 1] >= lo[1] - 1) & (p[:, 1] <= hi[1] + 1)
        if not sel.any():                 # Bein steht neben dem Koerper: tiefster Punkt ueberhaupt
            return p[:, 2].min() - 0.5 * e["scale"]
        return p[sel, 2].min() - 0.5 * e["scale"]
    def shift(k, dw):                     # Teil k in Welt um dw verschieben (Position im jeweiligen Rahmen)
        dw = np.asarray(dw, float)
        if k in (P_BODY, P_HEAD, P_HAIR, P_TAIL): d = RB.T @ dw
        elif k == P_FEET: d = RF.T @ dw
        else: d = dw
        if k == P_TAIL: d = d / 1.0
        eff[k]["pos"] = list(np.asarray(eff[k]["pos"], float) + d)
    def move_body_group(dz):              # Koerper samt aufgesetzten Teilen (Kopf, Haare, Schwanz haengt am Koerper)
        for k in (P_BODY, P_HEAD, P_HAIR): 
            if k in eff: shift(k, (0, 0, dz))
        if not quad and P_HANDS in eff: shift(P_HANDS, (0, 0, dz))
        if P_WINGS in eff: shift(P_WINGS, (0, 0, dz))
    G = ground_z(app) + 0.1
    # 1. Beine: unten auf dem Boden, Koerper auf die Beine (Ueberlappung 15 %, mind. 0.5 Einheiten)
    if P_FEET in eff and not swim:
        lo, hi = box(P_FEET); shift(P_FEET, (0, 0, G - lo[2]))
        if quad and P_HANDS in eff: lo2, hi2 = box(P_HANDS); shift(P_HANDS, (0, 0, G - lo2[2]))
        lo, hi = box(P_FEET); legH = hi[2] - lo[2]; ov = max(0.5, 0.15 * legH)
        dz = (hi[2] - ov) - body_bottom_over(box(P_FEET, 1))
        if abs(dz) > 0.05: move_body_group(dz); notes.append("Koerper z %+.1f" % dz)
        if quad and P_HANDS in eff:      # Vorderbeine gleich hoch wie Hinterbeine (bis an den Koerper)
            pass
    blo, bhi = box(P_BODY); bc = (blo + bhi) * 0.5; bh = bhi - blo
    # 2. Beine seitlich/laengs unter den Koerper
    def place_leg(k, want_y):
        inst = box(k, 1)                  # rechte Instanz (+x)
        lo, hi = inst; hw = (hi[0] - lo[0]) * 0.5; cx = (lo[0] + hi[0]) * 0.5; cy = (lo[1] + hi[1]) * 0.5
        tx = max(hw + 0.3, (bhi[0] - bc[0]) - hw - 0.3) + bc[0]
        if not quad: tx = min(max(hw + 0.3, cx), max(hw + 0.3, (bhi[0] - bc[0]) - hw * 0.5) + bc[0])
        ty = want_y(hi[1] - lo[1]) if want_y else cy
        d = np.array([tx - cx, ty - cy, 0.0])
        if np.abs(d).max() > 0.05: shift(k, d); notes.append("%s x %+.1f y %+.1f" % ({P_FEET: "Fuesse", P_HANDS: "Haende"}[k], d[0], d[1]))
    if P_FEET in eff and not swim:
        if quad:
            L = bh[1]; ins = 0.12 * L
            place_leg(P_FEET, lambda dep: blo[1] + dep * 0.5 + ins)
            if P_HANDS in eff: place_leg(P_HANDS, lambda dep: bhi[1] - dep * 0.5 - ins)
        else:
            place_leg(P_FEET, lambda dep: bc[1])
    # 3. Arme (Zweibeiner): seitlich am Koerper, Mitte in Koerperhoehe
    if not quad and P_HANDS in eff:
        lo, hi = box(P_HANDS, 1); hw = (hi[0] - lo[0]) * 0.5; c = (lo + hi) * 0.5
        tx = bhi[0] - 0.5 + hw; ty = min(max(c[1], blo[1] + 0.5), bhi[1] - 0.5)
        tz = min(max(c[2], blo[2] + 0.25 * bh[2]), bhi[2] - 0.25 * bh[2])
        d = np.array([tx - c[0], ty - c[1], tz - c[2]])
        if lo[0] <= bhi[0] + 0.3 and hi[0] >= bhi[0] + 0.3: d[0] = 0.0      # beruehrt die Koerperseite schon: x lassen
        if np.abs(d).max() > 0.05: shift(P_HANDS, d); notes.append("Arme %+.1f/%+.1f/%+.1f" % tuple(d))
    # 4. Kopf: muss den Koerper beruehren (Vierbeiner vorne oben, sonst oben drauf)
    if P_HEAD in eff:
        lo, hi = box(P_HEAD); dep = hi - lo; d = np.zeros(3)
        ovy = min(3.0, max(1.0, 0.25 * dep[1])); ovz = min(3.0, max(1.0, 0.25 * dep[2]))
        if kv.get("hals", "0") == "1":    # Kopf mit Hals (kopfbau.py): Halsansatz hinten unten -> vorne oben in den Koerper
            s_ = eff[P_HEAD]["scale"]; d[1] = (bhi[1] - 4.0 * s_) - lo[1]; d[2] = (bhi[2] - 3.0 * s_) - lo[2]
        elif quad or cr != 0:
            if lo[1] > bhi[1] - ovy: d[1] = (bhi[1] - ovy) - lo[1]            # Kopf vor dem Koerper schwebend
            if hi[1] < bhi[1] + 0.3 * dep[1]: d[1] = (bhi[1] + 0.3 * dep[1]) - hi[1]   # Kopf im Koerper versunken
            if lo[2] > bhi[2] - ovz: d[2] = (bhi[2] - ovz) - lo[2]
            if hi[2] < blo[2] + 0.6 * bh[2]: d[2] = (blo[2] + 0.6 * bh[2]) - hi[2]
        else:
            if lo[2] > bhi[2] - ovz or lo[2] < bhi[2] - 0.5 * dep[2]: d[2] = (bhi[2] - ovz) - lo[2]
            cy = (lo[1] + hi[1]) * 0.5
            if abs(cy - bc[1]) > 0.5 * bh[1]: d[1] = bc[1] - cy
        if np.abs(d).max() > 0.05:
            shift(P_HEAD, d); notes.append("Kopf %+.1f/%+.1f/%+.1f" % tuple(d))
            if P_HAIR in eff: shift(P_HAIR, d)
    # 5. Schwanz: Wurzel hinten am Koerper
    if P_TAIL in eff:
        lo, hi = box(P_TAIL); d = np.zeros(3)
        if hi[1] < blo[1] + 0.5 or hi[1] > blo[1] + 2.5: d[1] = (blo[1] + 1.0) - hi[1]
        cz = (lo[2] + hi[2]) * 0.5
        if cz < blo[2] or cz > bhi[2]: d[2] = (bc[2] + 0.2 * bh[2]) - cz
        if np.abs(d).max() > 0.05: shift(P_TAIL, d); notes.append("Schwanz %+.1f/%+.1f/%+.1f" % tuple(d))
    # 6. Fluegel: Wurzel an der Koerperseite, obere Haelfte
    if P_WINGS in eff:
        wx, wy, wz = eff[P_WINGS]["pos"]
        tx = max(0.0, bhi[0] - 0.8); tz = min(max(wz, bc[2]), bhi[2] - 0.5); ty = min(max(wy, blo[1] + 0.2 * bh[1]), bhi[1] - 0.2 * bh[1])
        d = np.array([tx - wx, ty - wy, tz - wz])
        if np.abs(d).max() > 0.05: eff[P_WINGS]["pos"] = [tx, ty, tz]; notes.append("Fluegel %+.1f/%+.1f/%+.1f" % tuple(d))
    # zurueck in die .mob: alle wirksamen Teile mit expliziter Skala und Position
    for k, e in eff.items():
        p = m["parts"].setdefault(k, dict(src=0, idx=-1, file="", scale=0, setpos=False, pos=[0, 0, 0]))
        p["scale"] = e["scale"]
        if FIELD_POS[k]: p["setpos"] = True; p["pos"] = [float(x) for x in e["pos"]]
    if P_HAIR in eff and P_HEAD in eff: m["parts"][P_HAIR]["pos"] = list(m["parts"][P_HEAD]["pos"])
    return notes


# ---- Gelenke (2026-10-27) ------------------------------------------------------------------------------------------
# Jedes Bein bekommt ein Gelenk (Huefte bzw. Schulter): oben mittig im Bein, kGelenkTiefe Voxel unter der Oberkante.
# Die Bein-.cub wird so mit leeren Voxeln aufgefuellt, dass ihre MITTE im Gelenk liegt; die Position wird so verschoben,
# dass das Bein in Ruhe genau dort bleibt, wo es war. Mit dem Gelenk-Patch (MobLab::InstallJointPatch) schwingt das
# Spiel das Bein dann nur noch um diese Mitte -> die Oberkante bleibt beim Laufen am Koerper.
# In der .mob:  gelenk=<teil>;x;y;z  (Voxel der unaufgefuellten Form <datei>.orig.cub; LAB/Hand aenderbar).
kGelenkTiefe = 1.0
def _mob_part_path(m, p):
    return os.path.normpath(os.path.join(os.path.dirname(m["path"]), p["file"].replace("\\", os.sep)))
def read_joints(m):
    out = {}
    for l in m["lines"]:
        if l.startswith("gelenk="):
            f = l[7:].split(";")
            if len(f) >= 4: out[int(f[0])] = [float(f[1]), float(f[2]), float(f[3])]
    return out
def write_joints(m, joints):
    m["lines"] = [l for l in m["lines"] if not l.startswith("gelenk=")]
    i = max([k for k, l in enumerate(m["lines"]) if l.startswith("teil=")] or [len(m["lines"]) - 1]) + 1
    m["lines"][i:i] = ["gelenk=%d;%g;%g;%g" % (k, round(v[0], 2), round(v[1], 2), round(v[2], 2)) for k, v in sorted(joints.items())]
def pad_to_joint(v, j):
    """Voxel v (sz,sy,sx,3) so auffuellen, dass Punkt j=(x,y,z) (Voxelkoordinaten) die Modellmitte ist (nur y und z;
    x bleibt mittig, sonst stimmt das gespiegelte rechte Bein nicht). Rueckgabe: neues Feld + Versatz (dx,dy,dz)."""
    sz, sy, sx, _ = v.shape
    def axis(n, c):
        half = max(c, n - c); m2 = int(math.ceil(2 * half - 1e-6)); off = int(round(m2 / 2.0 - c))
        return max(m2, n + off), off
    ny, oy = axis(sy, j[1]); nz, oz = axis(sz, j[2])
    out = np.zeros((nz, ny, sx, 3), np.uint8); out[oz:oz + sz, oy:oy + sy, :, :] = v
    return out, (0, oy, oz)
def gelenke(m, write_cub=True):
    """Gelenke fuer die Beine (Fuesse; bei Vierbeinern auch die Haende = Vorderbeine) setzen. Rueckgabe: Notizen."""
    eff, quad, app, size = resolve(m); notes = []
    joints = read_joints(m); m.setdefault("vox_out", {})
    for k in ([P_FEET, P_HANDS] if quad else [P_FEET]):
        p = m["parts"].get(k)
        if not p or p["src"] != 3 or not p["file"] or k not in eff: continue
        fp = _mob_part_path(m, p); op = fp[:-4] + ".orig.cub"
        if k not in joints or not os.path.exists(op):                      # erste Anwendung: aktuelle Form = Original
            if os.path.exists(fp) and k not in joints: open(op, "wb").write(open(fp, "rb").read())
        base = load_cub_file(op) if os.path.exists(op) else eff[k]["vox"]
        occ = np.argwhere(base.any(3))
        if not len(occ): continue
        if k not in joints:
            top = occ[:, 0].max() + 1; ys = occ[occ[:, 0] >= top - 2][:, 1]
            joints[k] = [base.shape[2] / 2.0, float(ys.mean() + 0.5), float(top - kGelenkTiefe)]
        j = joints[k]
        cur = eff[k]["vox"]; csz, csy, csx, _ = cur.shape
        # alte Mitte (aktuelle Datei) in Original-Voxeln: aktuelle Datei = Original + bisheriger Versatz (falls schon gepolstert)
        prev = read_joints(m).get(k)
        if prev is not None:
            _, (ox0, oy0, oz0) = pad_to_joint(base, prev)
        else:
            ox0 = oy0 = oz0 = 0
        old_c = np.array([csx / 2.0 - ox0, csy / 2.0 - oy0, csz / 2.0 - oz0])   # in Original-Voxeln
        new, off = pad_to_joint(base, j)
        new_c = np.array([new.shape[2] / 2.0, new.shape[1] / 2.0 - off[1], new.shape[0] / 2.0 - off[2]])
        d = (new_c - old_c) * eff[k]["scale"]; d[0] = 0.0
        p["pos"] = [p["pos"][i] + d[i] for i in range(3)] if p["setpos"] else [eff[k]["pos"][i] + d[i] for i in range(3)]
        p["setpos"] = True; p["scale"] = eff[k]["scale"]
        m["vox_out"][fp] = new
        notes.append("%s: Gelenk (%.1f, %.1f, %.1f), Modell %dx%dx%d" % ("Hinterbeine" if k == P_FEET else "Vorderbeine", j[0], j[1], j[2], new.shape[2], new.shape[1], new.shape[0]))
    write_joints(m, joints)
    return notes

def save_vox_out(m):
    for fp, v in m.get("vox_out", {}).items():
        sz, sy, sx, _ = v.shape
        open(fp, "wb").write(struct.pack("<3i", sx, sy, sz) + v.astype(np.uint8).tobytes())

def set_kv(m, key, val):
    for i, l in enumerate(m["lines"]):
        if l.startswith(key + "="): m["lines"][i] = "%s=%s" % (key, val); m["kv"][key] = str(val); return
    j = max(i for i, l in enumerate(m["lines"]) if not l.startswith("teil=")) + 1 if m["lines"] else 0
    m["lines"].insert(j, "%s=%s" % (key, val)); m["kv"][key] = str(val)

def bild(m, path):
    eff, quad, app, size = resolve(m)
    render_png(rasterize(part_mats(eff, quad, app, draw_race(m))), path)

if __name__ == "__main__":
    import argparse, glob, copy
    ap = argparse.ArgumentParser(); ap.add_argument("root"); ap.add_argument("--bilder", action="store_true")
    ap.add_argument("--ausrichten", action="store_true"); ap.add_argument("--schreiben", action="store_true")
    ap.add_argument("--gelenke", action="store_true"); ap.add_argument("--gang", action="store_true")
    ap.add_argument("--nur", default=""); ap.add_argument("--aus", default="tierbilder")
    a = ap.parse_args(); nur = set(x for x in a.nur.split(",") if x)
    files = sorted(glob.glob(os.path.join(a.root, "Tiere", "*.mob")) + glob.glob(os.path.join(a.root, "Tierwelt", "*.mob")))
    os.makedirs(a.aus, exist_ok=True)
    for f in files:
        n = os.path.splitext(os.path.basename(f))[0]
        if nur and n not in nur: continue
        m = read_mob(f)
        if a.bilder: bild(m, os.path.join(a.aus, n + "_vorher.png"))
        if a.gelenke:
            notes = gelenke(m)
            if a.schreiben: write_mob(m); save_vox_out(m)
            print(n, "; ".join(notes) or "-")
            m = read_mob(f)
        if a.gang:                                                         # Gang-Bilder: Ruhe + zwei Schrittphasen
            eff, quad, app, _ = resolve(m); r = draw_race(m)
            for tag, w in (("ruhe", None), ("schritt1", (0.21, 0.0)), ("schritt2", (0.21, math.pi))):
                render_png(rasterize(part_mats(eff, quad, app, r, w)), os.path.join(a.aus, "%s_%s.png" % (n, tag)))
            print(n, "Luecke im Gehen %.1f" % leg_gap(eff, quad, app, r))
        if a.ausrichten:
            notes = ausrichten(m)
            if a.bilder: bild(m, os.path.join(a.aus, n + "_nachher.png"))
            if a.schreiben: set_kv(m, "auto_ausrichtung", 0); write_mob(m)
            print(n, "; ".join(notes) or "-")
