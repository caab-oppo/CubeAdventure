# Erzeugt AdventureMod\TierBasis.inc: Aussehen der nativen Rassen 0..155 fuer die LAB-Tier-Werkstatt.
# Quelle: Infinite-Cubes alpha/src/cube/CreatureStats.cpp fillAppearance (spielquelle.py), Modellnummern = Registry-Index
# (geprueft: 0x172 alpaca-head, 0x1AA dragon-head, 0x0FF crow-head in LabModelNames.h).
import os, spielquelle as SQ
M = ["headModel", "hairModel", "handsModel", "feetModel", "chestModel", "tailModel", "shoulderModel", "wingsModel"]
S = ["headScale", "headScale", "handsScale", "feetScale", "chestScale", "tailScale", "shoulderArmorScale", "wingScale"]
P = ["headPosition", "headPosition", "handsPosition", "feetPosition", "chestPosition", "tailPosition", None, "wingsPosition"]
L = ["// AUTOMATISCH ERZEUGT von Projekt\\Werkzeuge\\Mobs\\tierbasis_inc.py -- nicht von Hand pflegen.",
     "// { flags, size[3], chestRot, feetRot, wingsRot, tailRot, model[8], scale[8], pos[8][3] }  Teile: Kopf Haare Haende Fuesse Koerper Schwanz Schulter Fluegel"]
for r in range(156):
    a = SQ.appearance(r)
    f = lambda v: (lambda s: s + ("f" if ("." in s or "e" in s) else ".0f"))("%.4g" % float(v))
    pos = ",".join("{%s}" % ",".join(f(x) for x in (a[p] if p else (0, 0, 0))) for p in P)
    L.append("{ %d, {%s}, %s, %s, %s, %s, {%s}, {%s}, {%s} },  // %d" % (int(a["appearanceFlags"]), ",".join(f(x) for x in a["size"]),
             f(a["chestRotation"]), f(a["feetRotation"]), f(a["wingsRotation"]), f(a["tailRotation"]),
             ",".join(str(int(a[m])) for m in M), ",".join(f(a[s]) for s in S), pos, r))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "AdventureMod", "TierBasis.inc"), "w", newline="\r\n").write("\n".join(L) + "\n")
print(len(L) - 2, "Rassen")
