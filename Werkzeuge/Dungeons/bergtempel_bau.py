# bergtempel_bau.py (2026-10-27) -- Bergtempel fuer den Dungeon-Baukasten Format 2 (wird von dungeons_bau.py aufgerufen).
#
# Wie die Burg: Grundebene zuerst, obere Ebenen nur ueber Treppenraeume (oben=), Module mit Anschluessen.
# Unterschied: der Tempel liegt IN einem Berg (dungeon.txt lage=berg, Dungeons.cpp):
#   * nur Module MIT Decke -- placeStructure hoehlt eine Saeule nur zwischen ihrem untersten und obersten gefuellten
#     Voxel aus (RE_placeStructure.md, WorldInfo.cpp:5655); ohne Decke bliebe der Fels im Raum stehen.
#   * keine Zinnen, keine offenen Hoefe; die oberste Treppe endet unter der Decke.
#   * Eingang "felsportal" zeigt mit dem Anschluss "aussen" aus dem Hang; Dungeons.cpp graebt davor einen Stollen ins Freie.
import os
from dungeons_bau import Room, TYPES, opening, frame, write_mod, write_def, N, E, S, W, MONSTER, BOSS, CHEST, VASE, LIGHT


def bergtempel(root):
    D = os.path.join(root, "Bergtempel")
    M = os.path.join(D, "Module")
    TYPES.clear(); TYPES.update({"gang": (7, 10, 2), "aussen": (7, 10, 2)})
    L = 14
    stone, floor_c, ceil_c = (128, 128, 132), (98, 94, 90), (86, 82, 80)       # Schluesselfarben (biomfarben=1)
    dark, red, gold, marble, sand, blue, bone, wood = (92, 92, 96), (150, 30, 34), (220, 180, 60), (236, 234, 226), (222, 210, 178), (60, 100, 200), (226, 220, 200), (122, 84, 50)

    def room(Wd, Dp, H, conns, t=2, lights=True):
        r = Room(Wd, Dp, H)
        r.box(0, 0, 0, Wd - 1, Dp - 1, 1, floor_c)
        r.box(0, 0, 2, Wd - 1, t - 1, H - 2, stone); r.box(0, Dp - t, 2, Wd - 1, Dp - 1, H - 2, stone)
        r.box(0, 0, 2, t - 1, Dp - 1, H - 2, stone); r.box(Wd - t, 0, 2, Wd - 1, Dp - 1, H - 2, stone)
        r.box(0, 0, H - 1, Wd - 1, Dp - 1, H - 1, ceil_c)                      # immer Decke (Aushoehlen im Fels)
        for (typ, d, u, z) in conns:
            opening(r, typ, d, u, z); frame(r, typ, d, u, z, sand)
        if lights:
            for z in range(7, H - 2, L):
                for x in range(4, Wd - 4, 8):
                    for y in (t, Dp - 1 - t):
                        if not r.solid(x, y, z) and r.solid(x, y + (-1 if y == t else 1), z):
                            r.set(x, y, z, LIGHT)
        return r

    def pillar(r, x, y, z0, z1, col=sand, cap=gold):
        r.box(x, y, z0, x + 1, y + 1, z1, col); r.box(x - 1, y - 1, z1, x + 2, y + 2, z1, cap)

    def C(typ, d, u, z=2):
        return (typ, d, u, z)

    # -- Gaenge (mit Knochennischen) ---------------------------------------------------------------------
    for name, dims, conns, gew, mons in [
        ("gang_gerade", (11, 21), [C("gang", S, 2), C("gang", N, 2)], 16, 1),
        ("gang_lang", (11, 31), [C("gang", S, 2), C("gang", N, 2), C("gang", E, 12)], 10, 2),
        ("gang_ecke", (11, 11), [C("gang", S, 2), C("gang", E, 2)], 10, 0),
        ("gang_t", (11, 11), [C("gang", S, 2), C("gang", E, 2), C("gang", W, 2)], 9, 0),
        ("gang_kreuz", (11, 11), [C("gang", S, 2), C("gang", N, 2), C("gang", E, 2), C("gang", W, 2)], 6, 0),
    ]:
        r = room(dims[0], dims[1], L, conns)
        for y in range(4, dims[1] - 3, 6):
            if not r.solid(2, y, 3) and r.solid(1, y, 3): r.set(2, y, 3, bone)
            if not r.solid(dims[0] - 3, y, 3) and r.solid(dims[0] - 2, y, 3): r.set(dims[0] - 3, y, 3, bone)
        for i in range(mons):
            r.set(5, 6 + i * 10, 2, MONSTER)
        write_mod(M, name, r, "raum", conns, gew, dach=1)

    # -- Treppenraeume: wie die Burg (3 breit, 14 Stufen, ueber jeder Stufe mind. 4 frei) ----------------------
    TW, TD = 17, 21
    tconns = [C("gang", S, 5), C("gang", N, 5)]

    def flight(r, x0, rail_x):
        r.carve(x0, 3, L - 1, x0 + 2, 16, L - 1)
        for k in range(1, L - 1):
            y = 2 + k
            r.box(x0, y, 2, x0 + 2, y, 1 + k, sand)
            r.box(rail_x, y, 2, rail_x, y, min(L - 1, 2 + k), dark)

    def arrive(r, x0, rail_x):
        r.carve(x0, 3, 0, x0 + 2, 14, 1)
        r.box(x0, 15, 0, x0 + 2, 15, 0, sand); r.clear(x0, 15, 1); r.clear(x0 + 1, 15, 1); r.clear(x0 + 2, 15, 1)
        r.box(x0, 16, 0, x0 + 2, 16, 1, sand)
        r.box(rail_x, 3, 2, rail_x, 14, 2, dark); r.box(x0, 2, 2, x0 + 2, 2, 2, dark)

    r = room(TW, TD, L, tconns); flight(r, 12, 11); r.set(4, 8, 2, MONSTER)
    write_mod(M, "treppenhaus", r, "raum", tconns, 9, maxn=4, dach=1, pflicht=2, ebene="boden",
              note="Treppenraum Grundebene: Treppe nach oben (Ost), darueber treppenraum_mitte")
    with open(os.path.join(M, "treppenhaus.txt"), "a", encoding="utf-8") as f: f.write("oben=treppenraum_mitte\n")
    r = room(TW, TD, L, tconns); arrive(r, 12, 11); flight(r, 2, 5); r.set(8, 8, 2, MONSTER)
    write_mod(M, "treppenraum_mitte", r, "raum", tconns, 1, maxn=0, dach=1, ebene="oben", note="Ankunft Ost, Treppe weiter (West)")
    with open(os.path.join(M, "treppenraum_mitte.txt"), "a", encoding="utf-8") as f: f.write("oben=treppenraum_dach\n")
    r = room(TW, TD, L, tconns); arrive(r, 2, 5); r.set(10, 8, 2, MONSTER); r.set(14, 17, 2, CHEST)
    write_mod(M, "treppenraum_dach", r, "raum", tconns, 1, maxn=0, dach=1, ebene="oben", note="oberste Ebene, Ankunft West")

    # -- Felsportal (Eingang) -----------------------------------------------------------------------------
    conns = [C("aussen", S, 9), C("gang", N, 9)]
    r = room(25, 23, 20, [], lights=False)
    for (typ, d, u, z) in conns:
        opening(r, typ, d, u, z); frame(r, typ, d, u, z, gold)
    for x in (3, 19):                                                         # Waechterstatuen
        r.box(x, 8, 2, x + 2, 10, 3, sand); r.box(x, 8, 4, x + 2, 10, 12, marble); r.box(x, 8, 13, x + 2, 10, 14, gold)
    for y in (4, 15):
        pillar(r, 6, y, 2, 17); pillar(r, 17, y, 2, 17)
    r.box(9, 2, 1, 15, 20, 1, red)                                            # roter Teppich vom Tor zur Halle
    r.set(4, 4, 8, LIGHT); r.set(20, 4, 8, LIGHT); r.set(4, 18, 8, LIGHT); r.set(20, 18, 8, LIGHT)
    r.set(5, 13, 2, MONSTER); r.set(19, 13, 2, MONSTER); r.set(3, 19, 2, VASE); r.set(21, 19, 2, VASE)
    write_mod(M, "felsportal", r, "eingang", conns, 10, dach=1, ebene="boden", note="Eingang: aussen nach Sueden zeigt aus dem Hang")

    # -- Saeulenhalle (zwei Ebenen hoch) -------------------------------------------------------------------
    u = 14
    conns = [C("gang", S, u), C("gang", N, u), C("gang", E, u), C("gang", W, u)]
    r = room(35, 35, 2 * L, conns)
    for x in (6, 27):
        for y in (6, 13, 20, 27):
            pillar(r, x, y, 2, 2 * L - 3)
    for x in (13, 20):
        for y in (6, 27):
            pillar(r, x, y, 2, 2 * L - 3)
    r.box(15, 15, 2, 19, 19, 2, marble); r.box(16, 16, 3, 18, 18, 9, gold); r.set(17, 17, 10, LIGHT)
    r.set(10, 17, 2, MONSTER); r.set(24, 17, 2, MONSTER); r.set(17, 10, 2, MONSTER); r.set(17, 24, 2, MONSTER); r.set(30, 30, 2, CHEST)
    write_mod(M, "saeulenhalle", r, "raum", conns, 6, maxn=2, dach=1, note="zwei Ebenen hoch")

    # -- Wasserhalle: Becken mit Kreuzbruecke --------------------------------------------------------------
    u = 12
    conns = [C("gang", S, u), C("gang", N, u), C("gang", E, u), C("gang", W, u)]
    r = room(31, 31, 18, conns)
    r.carve(4, 4, 1, 26, 26, 1); r.box(4, 4, 1, 26, 26, 1, blue)               # Wasserflaeche
    r.box(12, 2, 1, 18, 28, 1, sand); r.box(2, 12, 1, 28, 18, 1, sand)         # Bruecken
    for (x, y) in ((7, 7), (22, 7), (7, 22), (22, 22)):
        pillar(r, x, y, 2, 15, marble)
    r.set(15, 15, 2, MONSTER); r.set(15, 6, 2, MONSTER); r.set(25, 15, 2, VASE)
    write_mod(M, "wasserhalle", r, "raum", conns, 5, maxn=2, dach=1)

    # -- Grabkammer -----------------------------------------------------------------------------------------
    conns = [C("gang", S, 9), C("gang", E, 9)]
    r = room(25, 25, L, conns)
    for x in (4, 10, 16):
        for y in (14, 19):
            r.box(x, y, 2, x + 3, y + 2, 3, (110, 106, 100)); r.box(x, y, 4, x + 3, y + 2, 4, gold)
    r.set(6, 8, 2, MONSTER); r.set(18, 8, 2, MONSTER); r.set(12, 10, 2, MONSTER); r.set(21, 22, 2, VASE); r.set(3, 22, 2, CHEST)
    write_mod(M, "grabkammer", r, "raum", conns, 8, maxn=3, dach=1)

    # -- Opferraum ------------------------------------------------------------------------------------------
    conns = [C("gang", S, 7), C("gang", N, 7)]
    r = room(21, 23, 16, conns)
    r.box(7, 9, 2, 13, 13, 3, (70, 66, 64)); r.box(8, 10, 4, 12, 12, 4, red)
    for (x, y) in ((4, 4), (16, 4), (4, 18), (16, 18)):
        r.box(x, y, 2, x, y, 4, dark); r.set(x, y, 5, LIGHT)
    r.set(10, 6, 2, MONSTER); r.set(10, 16, 2, MONSTER); r.set(10, 11, 5, CHEST)
    write_mod(M, "opferraum", r, "raum", conns, 7, maxn=2, dach=1)

    # -- Wachraum ---------------------------------------------------------------------------------------------
    conns = [C("gang", S, 5), C("gang", N, 5), C("gang", E, 7)]
    r = room(17, 21, L, conns)
    r.box(2, 15, 2, 4, 18, 2, wood); r.box(12, 15, 2, 14, 18, 2, wood)
    r.set(8, 6, 2, MONSTER); r.set(8, 14, 2, MONSTER); r.set(3, 3, 2, VASE)
    write_mod(M, "wachraum", r, "raum", conns, 9, maxn=4, dach=1)

    # -- Schriftenkammer + Schatzkammer -----------------------------------------------------------------------
    conns = [C("gang", S, 7), C("gang", W, 9)]
    r = room(21, 25, L, conns)
    for x in range(4, 18, 3):
        r.box(x, 21, 2, x + 1, 22, 9, (90, 60, 40))
        for z in (3, 5, 7):
            r.box(x, 21, z, x + 1, 21, z, sand)
    r.set(5, 14, 2, MONSTER); r.set(15, 14, 2, MONSTER); r.set(10, 19, 2, CHEST)
    write_mod(M, "schriftenkammer", r, "raum", conns, 6, maxn=2, dach=1)
    conns = [C("gang", S, 4)]
    r = room(15, 15, L, conns)
    r.box(2, 10, 2, 12, 12, 2, gold)
    r.set(3, 11, 3, CHEST); r.set(7, 11, 3, CHEST); r.set(11, 11, 3, CHEST); r.set(7, 6, 2, MONSTER)
    write_mod(M, "schatzkammer", r, "raum", conns, 4, maxn=2, dach=1)

    # -- Boss: Goetzensaal + Allerheiligstes ------------------------------------------------------------------
    conns = [C("gang", S, 14)]
    r = room(35, 35, 26, conns)
    r.box(10, 24, 2, 24, 32, 3, marble); r.box(13, 27, 4, 21, 32, 6, sand)
    r.box(15, 29, 7, 19, 32, 20, gold); r.box(14, 28, 21, 20, 32, 23, gold)   # Goetze
    r.set(16, 28, 18, red); r.set(18, 28, 18, red)
    for (x, y) in ((6, 8), (27, 8), (6, 20), (27, 20)):
        pillar(r, x, y, 2, 22)
    r.set(17, 17, 2, BOSS); r.set(9, 13, 2, MONSTER); r.set(25, 13, 2, MONSTER)
    r.set(11, 29, 4, CHEST); r.set(23, 29, 4, CHEST)
    write_mod(M, "goetzensaal", r, "boss", conns, 10, dach=1)
    conns = [C("gang", S, 12)]
    r = room(31, 31, 24, conns)
    r.box(10, 20, 2, 20, 27, 3, marble); r.box(13, 24, 4, 17, 27, 13, gold)
    r.set(15, 14, 2, BOSS); r.set(8, 10, 2, MONSTER); r.set(22, 10, 2, MONSTER); r.set(11, 22, 4, CHEST); r.set(19, 22, 4, CHEST)
    write_mod(M, "allerheiligstes", r, "boss", conns, 6, dach=1)

    write_def(D, """
# Bergtempel -- Baukasten Format 2 wie die Burg, aber IN einen Berg gegraben (lage=berg)
format=2
name=Bergtempel
aktiv=1
anteil=0.6
gebiet=ueberall
# lage=berg: nur wo das Gelaende ueber dem Grundriss hoeher ist als der Tempel -- felsdecke = Bloecke Fels ueber dem
# hoechsten Raum, berg_anteil = so viel % der Messpunkte muessen so bedeckt sein. Eingang am Hangfuss (Boden vor dem
# Portal = Eingangsboden), davor graebt der Mod einen Stollen bis ins Freie.
lage=berg
felsdecke=4
berg_anteil=75
typ=gang,7,10,2,128,128,132
typ=aussen,7,10,2,110,106,100
aussen=aussen
module=18,28
flaeche_min=90,90
flaeche_max=180,180
hoehe_max=28
versuche=24
zinnen=0
obere=5,12
biomfarben=1
farbe_wand=128,128,132
farbe_boden=98,94,90
farbe_decke=86,82,80
# Bewohner: Skelette, Untote, Spectrino, Anubis, Goblins -- Boss: Lich, Minotaurus
bewohner=17,15,16,80,77,5
boss=117,82
monster_anteil=85
level=+2
boss_level=+4
fundament=1
""")
