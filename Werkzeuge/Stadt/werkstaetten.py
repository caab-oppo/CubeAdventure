#!/usr/bin/env python3
"""Baut die nativen Werkstaetten (Schmiede, Zimmerei, Weberei) als eigene Modelle nach.

Quelle: building-smithy.cub / building-carpentersshop.cub / building-weavingmill.cub aus data1.db (cubdb.py).
Markerfarben wie World::placeSprite (Infinite-Cubes alpha/src/cube/WorldGeneration.cpp): sie werden zu Luft, die
Arbeitsstationen kommen als Liste "station=typ,x,y,z,sx,sy,sz" (Modellkoordinaten, Typ = Zone-Objekt) in die .txt
daneben -- City.cpp setzt sie als native Objekte. So wird das Gebaeude wie jedes andere Stadtgebaeude gestempelt
(auch ueber Zonengrenzen), ohne den Sonderweg ueber das native Modell.

Aufruf: python werkstaetten.py <Strukturen-Ordner>   (CUBE_DATA1 = Pfad zu data1.db, sonst ../../../data1.db)
"""
import os, sys, struct
import numpy as np
import cubdb

# (Modell, Dateiname, Stationen je Markerfarbe: farbe -> (Objekttyp, Groesse))
WORK = [
    ("building-smithy.cub", "Werkstatt_Schmiede", {(0, 255, 255): (71, (3.0, 3.0, 6.0)),     # Ofen
                                                   (0, 255, 127): (72, (1.0, 1.0, 1.0))}),    # Amboss
    ("building-carpentersshop.cub", "Werkstatt_Zimmerei", {(0, 255, 255): (75, (1.5, 1.5, 1.0)),   # Saege
                                                           (0, 255, 127): (76, (1.8, 1.8, 1.2)),   # Werkbank
                                                           (0, 0, 127): (77, (1.8, 1.8, 1.2))}),   # Anpassungsbank
    ("building-weavingmill.cub", "Werkstatt_Weberei", {(0, 255, 255): (73, (1.5, 1.5, 1.0)),    # Spinnrad
                                                       (0, 255, 127): (74, (1.5, 1.5, 3.0)),    # Webstuhl
                                                       (0, 0, 127): (77, (1.8, 1.8, 1.2))}),    # Anpassungsbank
]
SMITHY_BENCH = (0, 0, 127)          # in der Schmiede ebenfalls eine Anpassungsbank (wie bisher mit Kultur 0x0D)
WORK[0][2][SMITHY_BENCH] = (77, (1.8, 1.8, 1.2))
# alle Markerfarben des Spiels (Tueren, Fenster, Efeu, Betten, Regale, Tische, Lampen, Wappen, Schilder, Blumen,
# Stationen, Vasen, Bewohner) -> Luft
MARKERS = {(255, 0, 0), (0, 255, 0), (0, 127, 127), (255, 127, 0), (255, 127, 127), (127, 127, 0), (255, 255, 0),
           (63, 0, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (0, 0, 127), (0, 255, 127), (0, 127, 0), (127, 0, 127)}


def build(root):
    out = os.path.join(root, "Stadt"); os.makedirs(out, exist_ok=True)
    for model, name, stations in WORK:
        r = cubdb.get(model)
        if not r:
            print("fehlt in data1.db:", model); continue
        sx, sy, sz, d = r
        a = np.frombuffer(d, np.uint8).reshape(sz, sy, sx, 3).copy()
        st = []
        for z in range(sz):
            for y in range(sy):
                for x in range(sx):
                    c = tuple(int(v) for v in a[z, y, x])
                    if c in stations:
                        t, size = stations[c]
                        st.append((t, x, y, z, size))
                    if c in MARKERS:
                        a[z, y, x] = 0
        with open(os.path.join(out, name + ".cub"), "wb") as f:
            f.write(struct.pack("<3i", sx, sy, sz)); f.write(a.tobytes())
        with open(os.path.join(out, name + ".txt"), "w", encoding="utf-8") as f:
            f.write("# Nachbau von %s (Markerfarben -> Luft). station=typ,x,y,z,sx,sy,sz (Modellkoordinaten)\n" % model)
            for (t, x, y, z, s) in st:
                f.write("station=%d,%d,%d,%d,%.1f,%.1f,%.1f\n" % (t, x, y, z, s[0], s[1], s[2]))
        print(name, sx, sy, sz, "Stationen", len(st))


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else ".")
