"""Tier-Haltungen wie das Spiel: Ruhe, Gehen, Angriffe (Quelle: Infinite-Cubes alpha/src/cube/Animation.cpp, nur Logik).

pose_mats(eff, quad, app, race, zeit_ms, art, gelenke) -> [(teil, Matrix, Voxel)]
  art: "ruhe" | "gehen" | "biss" | "sturzbiss" | "kopfstoss" | "stampfen" | "hornangriff"
  Gehen: Amplitude 0.21 (Gleichgewicht bei ~60 Bildern/s), Phase = zeit * 0.006 (eine Schrittfolge ~1 s).
  Angriffe: Zielhaltung je Zeitabschnitt (Ausholen / Schlag / Halten), die Haltung folgt ihr wie im Spiel:
    je Millisekunde value += (ziel - value) * blendRate (Animation.h blend), blendRate 0.01 (Stampfen Phase 2: 0.02).
  Zeiten (CreatureStats.cpp, Monster-Tempo 0.75 = 0.0625 * Seltenheit 0 + 0.75):
    Biss/Stampfen: 300/300/500 ms, Sturzbiss: 400/300/500, Kopfstoss/Hornangriff: 300/1000/800 -- je / 0.75.
  gelenke=True: Gelenk-Patch (TierGelenke.cpp): die erste Bein-Drehung um den Modellursprung entfaellt (nur
    feetRotation bleibt) -> die Beine schwingen um ihre Modellmitte = Gelenk (Bein-.cub darauf aufgefuellt).
Matrizen (Animation.cpp 2272ff.):
  Kreatur  = RZ(creatureRot z) RY RX(creatureRot x)                         (Kreaturdrehung der Pose)
  Koerper  = Kreatur T(0,0,bob) T(bodyOffset) RZ(bodyRot z) RY(bodyRot y) RX(chestRotation + bodyRot x)
  Brust    = Koerper T(chestPos) RZ(swingDeg*0.5) S center
  Kopf     = Koerper T(headOffset + headPos - (0,0,bob/2)) T(0,0,-3) RX(-bodyRot x - chestRot) RY(-bodyRot y)
             RZ(headRot z - bodyRot z) RY(headRot y) RX(headRot x) T(0,0,3) S off
  Schwanz  = Koerper T(chestPos) T(tailPos) RX(tailRotation) RZ(-armSwing deg) RZ(tailRot z) RY RX(tailRot x) S*cs T(-sx/2,-sy,-sz/2)
  Haende   = Kreatur T(0,0,bob) T(bodyOffset) RZ(bodyRot z) RY RX(bodyRot x) | links: [Vierb. RX(rfs)] T(Pos+Offset)
             RZ(az) RY(ay) [Vierb. RX(rfs)] RX(ax + handRot x) S center   (rechts mit lfs, Skala -s)
  Fuesse   = Kreatur RZ(bodyTurn) RX(feetRot + lfs) T(x, y, stride*3 + z + lift) S RX(lfs) center
             (rechts: RX(rfs) ... RX(feetRot + rfs))
"""
import math
import numpy as np
from tierbild import T, S, R, P_HEAD, P_HAIR, P_HANDS, P_FEET, P_BODY, P_TAIL, P_WINGS

TEMPO = 0.75
ARTEN = {   # (Ausholen, Schlag, Halten) in ms bei Tempo 1
    "biss": (300, 300, 500), "sturzbiss": (400, 300, 500), "kopfstoss": (300, 1000, 800),
    "stampfen": (300, 300, 500), "hornangriff": (300, 1000, 800),
}
V0 = (0.0, 0.0, 0.0)

def _targets(art, t, ant, strike, dur):
    """Zielhaltung zum Zeitpunkt t (ms) -- 1:1 aus Animation.cpp (case AnimationClawStrike/DiveBite/Headbutt/Stomp/HornCharge)."""
    g = dict(head=V0, body=V0, bodyOff=V0, headOff=V0, lhr=V0, rhr=V0, lho=V0, rho=V0, tail=V0, crea=V0, rate=0.01)
    if art == "biss":                                                      # AbilityBite 75 -> AnimationClawStrike
        if t < ant:
            g.update(head=(10, 0, 0), body=(-20, 0, 0), bodyOff=(0, -1, 0), headOff=(0, 1, 1), lhr=(20, 0, 0), rhr=(20, 0, 0), lho=(0, 0, 2), rho=(0, 0, 2), tail=(30, 0, 10))
        elif t < ant + strike / 3:
            g.update(head=(-30, 0, 0), body=(50, 0, 0), bodyOff=(0, 0, 1), headOff=(0, -4, 2), lhr=(60, 0, 0), rhr=(70, 0, 0), tail=(-30, 0, 0))
        elif t < ant + strike * 2 / 3:
            g.update(head=(-30, 0, 0), body=(40, 0, 0), bodyOff=(0, 1, 1), headOff=(0, -2, 2), lhr=(70, 0, 0), rhr=(-30, 0, 0), rho=(0, 3, -2), tail=(-30, 0, -10))
        elif t < ant + strike:
            g.update(head=(-30, 0, 0), body=(10, 0, 0), bodyOff=(0, 1, 1), headOff=(0, -2, 2), lhr=(-30, 0, 0), rhr=(-40, 0, 0), lho=(0, 3, -2), tail=(-30, 0, -10))
        elif t < dur:
            g.update(head=(10, 0, -10), body=(-20, 0, 0), bodyOff=(0, 1, 0), headOff=(0, 1, 1), lhr=(20, 0, 0), rhr=(20, 0, 0), lho=(0, 0, 2), rho=(0, 0, 2), tail=(30, 0, 10))
    elif art == "sturzbiss":                                               # AbilityForwardRoll 51 -> AnimationDiveBite
        if t < ant: g.update(crea=(-30, 0, 0))
        elif t < ant + strike: g.update(crea=(120, 0, 0))
    elif art == "kopfstoss":                                               # AnimationHeadbutt
        if t < ant + strike / 2: g.update(head=(-30, 10, 90), body=(0, 0, 60), headOff=(-3, -2, 0))
        elif t < dur: g.update(head=(30, -10, -30), body=(10, 0, -20), headOff=(3, 0, 2))
    elif art == "stampfen":                                                # AnimationStomp
        if t < ant + strike / 2:
            g.update(head=(-20, 0, 0), body=(50, 0, 0), bodyOff=(0, -2, 2), headOff=(0, -6, -2), lhr=(20, 0, 0), rhr=(30, 0, 0), tail=(-50, 0, 0))
        elif t < dur:
            g.update(head=(-10, 0, 0), body=(0, 0, 0), headOff=(0, 0, 1), rate=0.02)
    elif art == "hornangriff":                                             # AnimationHornCharge
        if t < ant + strike / 2:
            g.update(head=(-70, 0, 0), body=(-10, 0, 0), headOff=(0, -3, -1), lhr=(20, 0, 0), rhr=(20, 0, 0))
        elif t < dur:
            g.update(head=(50, 0, 0), body=(20, 0, 0), bodyOff=(0, -3, 1), headOff=(0, 0, 2), rhr=(-20, 0, 0), lhr=(-20, 0, 0), tail=(-10, 0, 0))
    return g

def pose_state(art, zeit_ms, step=16):
    """Haltung nach zeit_ms (Blenden wie Animation.cpp, je Millisekunde)."""
    keys = ("head", "body", "bodyOff", "headOff", "lhr", "rhr", "lho", "rho", "tail", "crea")
    st = {k: np.zeros(3) for k in keys}
    if art not in ARTEN: return st
    ant, strike, hold = (x / TEMPO for x in ARTEN[art]); dur = ant + strike + hold
    t = 0.0
    while t < zeit_ms:
        g = _targets(art, t, ant, strike, dur)
        share = 1.0 - (1.0 - g["rate"]) ** step                           # blend(poseBlend, 1, ms, rate)
        for k in keys: st[k] = st[k] + share * (np.array(g[k], float) - st[k])
        t += step
    return st

def dauer(art):
    a = ARTEN.get(art); return 0 if not a else sum(a) / TEMPO

def pose_mats(eff, quad, app, race=-1, zeit_ms=0.0, art="ruhe", gelenke=False):
    out = []
    def center(v): sz, sy, sx, _ = v.shape; return T(-sx * 0.5, -sy * 0.5, -sz * 0.5)
    A = 0.21 if art == "gehen" else 0.0
    ph = zeit_ms * 0.006
    cycle = math.sin(ph + math.pi / 2)
    sw = A * 2.199114820062152 * cycle * (0.75 if quad else 1.0)
    lfs = math.degrees(sw) * 1.5; rfs = -math.degrees(sw) * 1.5
    lLift = A * 4.0 * cycle; rLift = (1.0 - cycle) * A * 4.0 if A else 0.0
    stride = abs(sw); bob = stride * 4.0; swingDeg = math.degrees(sw)
    ps = pose_state(art, zeit_ms)
    br, bo, ho, hr, tr, cr_ = ps["body"], ps["bodyOff"], ps["headOff"], ps["head"], ps["tail"], ps["crea"]
    chest = float(app.get("chestRotation", 0))
    CRE = R("z", cr_[2]) @ R("y", cr_[1]) @ R("x", cr_[0])
    BODY = CRE @ T(0, 0, bob) @ T(*bo) @ R("z", br[2]) @ R("y", br[1]) @ R("x", chest + br[0])
    HANDS = CRE @ T(0, 0, bob) @ T(*bo) @ R("z", br[2]) @ R("y", br[1]) @ R("x", br[0])
    if P_BODY in eff:
        e = eff[P_BODY]; s = e["scale"]
        out.append((P_BODY, BODY @ T(*e["pos"]) @ R("z", swingDeg * 0.5) @ S(s, s, s) @ center(e["vox"]), e["vox"]))
    for k in (P_HEAD, P_HAIR):
        if k in eff:
            e = eff[k]; v = e["vox"]; sz, sy, sx, _ = v.shape; s = e["scale"]
            off = T(-sx * 0.5, -sy * (0.2 if race == 101 else 0.5), -sz * 0.5)
            hp = np.array(e["pos"], float) + ho - np.array([0, 0, bob * 0.5])
            M = BODY @ T(*hp) @ T(0, 0, -3) @ R("x", -br[0] - chest) @ R("y", -br[1]) @ R("z", hr[2] - br[2]) @ R("y", hr[1]) @ R("x", hr[0]) @ T(0, 0, 3)
            out.append((k, M @ S(s, s, s) @ off, v))
    if P_HANDS in eff:
        e = eff[P_HANDS]; hx, hy, hz = e["pos"]; s = e["scale"]
        hrot = app.get("handsRotation", [0, 0, 0]) or [0, 0, 0]
        if quad:
            yy = hy + 2.1 - A * 5.0
            for side, pre in ((-1, rfs), (1, lfs)):
                o = ps["lho"] if side < 0 else ps["rho"]; rr = ps["lhr"] if side < 0 else ps["rhr"]
                ax = hrot[0] + rr[0]; ay = (-hrot[1] if side < 0 else hrot[1]); az = (-hrot[2] if side < 0 else hrot[2])
                p0 = 0.0 if gelenke else pre
                M = HANDS @ R("x", p0) @ T(side * hx + o[0], yy + o[1], hz + o[2]) @ R("z", az) @ R("y", ay) @ R("x", pre) @ R("x", ax)
                out.append((P_HANDS, M @ S(s if side < 0 else -s, s, s) @ center(e["vox"]), e["vox"]))
        else:
            for side in (-1, 1):
                o = ps["lho"] if side < 0 else ps["rho"]; rr = ps["lhr"] if side < 0 else ps["rhr"]
                M = HANDS @ T(side * hx + o[0], hy + 2.1 + o[1], hz - 4.1 + o[2]) @ R("x", rr[0])
                out.append((P_HANDS, M @ S(s if side < 0 else -s, s, s) @ center(e["vox"]), e["vox"]))
    if P_FEET in eff:
        e = eff[P_FEET]; fx, fy, fz = e["pos"]; s = e["scale"]; fr = float(app.get("feetRotation", 0))
        a1, a2 = (fr, 0.0) if gelenke else (fr + lfs, rfs)
        out.append((P_FEET, CRE @ R("x", a1) @ T(-fx, fy, stride * 3 + fz + lLift) @ S(s, s, s) @ R("x", lfs) @ center(e["vox"]), e["vox"]))
        out.append((P_FEET, CRE @ R("x", a2) @ T(fx, fy, stride * 3 + fz + rLift) @ S(s, s, s) @ R("x", fr + rfs) @ center(e["vox"]), e["vox"]))
    if P_TAIL in eff and P_BODY in eff:
        e = eff[P_TAIL]; v = e["vox"]; sz, sy, sx, _ = v.shape; cs = eff[P_BODY]["scale"]; ts = e["scale"] * cs
        M = BODY @ T(*eff[P_BODY]["pos"]) @ T(*e["pos"]) @ R("x", float(app.get("tailRotation", 0))) @ R("z", -swingDeg) @ R("z", tr[2]) @ R("y", tr[1]) @ R("x", tr[0])
        out.append((P_TAIL, M @ S(ts, ts, ts) @ T(-sx * 0.5, -sy, -sz * 0.5), v))
    if P_WINGS in eff:
        e = eff[P_WINGS]; v = e["vox"]; sz, sy, sx, _ = v.shape; wx, wy, wz = e["pos"]; s = e["scale"]; wr = float(app.get("wingsRotation", 0))
        for side, flap in ((-1, -45.0), (1, 45.0)):
            M = HANDS @ T(side * wx, wy, wz) @ R("x", wr) @ R("y", flap) @ R("z", -90) @ R("x", 90) @ S(s, s, s) @ T(-sx * 0.5, 0, -sz * 0.5)
            out.append((P_WINGS, M, v))
    return out

if __name__ == "__main__":
    # python tierpose.py <Strukturen>\Mobs <tier> [art] [zeiten ms, Komma] [bild.png]   -- gleiche Rechnung wie die LAB-Tier-Werkstatt
    import sys, os, glob
    import tierbild as TB
    from PIL import Image
    root, name = sys.argv[1], sys.argv[2]
    art = sys.argv[3] if len(sys.argv) > 3 else "gehen"
    zeiten = [float(x) for x in (sys.argv[4] if len(sys.argv) > 4 else "0,260,520,780").split(",")]
    out = sys.argv[5] if len(sys.argv) > 5 else "%s_%s.png" % (name, art)
    f = (glob.glob(os.path.join(root, "*", name + ".mob")) + glob.glob(os.path.join(root, name + ".mob")))[0]
    m = TB.read_mob(f); e, q, a, _ = TB.resolve(m); r = TB.draw_race(m)
    gel = r >= 200 and bool(TB.read_joints(m))
    ims = []
    for t in zeiten:
        TB.render_png(TB.rasterize(pose_mats(e, q, a, r, t, art, gel)), "/tmp/_tp.png" if os.name != "nt" else "_tp.png")
        ims.append(Image.open("/tmp/_tp.png" if os.name != "nt" else "_tp.png").copy())
    W = sum(i.width for i in ims); H = max(i.height for i in ims); o = Image.new("RGB", (W, H), (38, 43, 54)); x = 0
    for i in ims: o.paste(i, (x, 0)); x += i.width
    o.save(out); print(out, "Gelenke" if gel else "ohne Gelenke")
