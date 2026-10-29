# Modell-Registry — identifizierte Indizes

**Kurz:** Alles hier ist **im Spiel gesehen**, nicht hergeleitet. Meine frühere
Herleitung aus der Reihenfolge der Zeichenketten in `.rdata` war falsch und ist
verworfen — sie hatte 0x8FE als Landeplatz und 0x8FF als Kriegerhaus, tatsächlich
sind es Wachturm und Durchgang.

Registry liegt bei `GC+0x300`, 2569 Einträge, gelesen über
`vectorU32_at @ 0x4120C0` (thiscall, ECX = Registry).

---

## Bestätigt

Mit **★** markiert ist, was im Bergdorf verwendet wird.

| Index | Was es ist | Größe | |
|---|---|---|---|
| `0x832` | **Baum** | 15x15 | ★ |
| `0x84C` | **Tempel** | 32x32 | |
| `0x850` | **Tempel** | 32x32 | |
| `0x87E` | **Haus** | 33x33 | ★ |
| `0x87F` | **Haus** | 33x33 | ★ |
| `0x880` | **Haus** | 33x33 | ★ |
| `0x882` | **Brunnen** | 10x10 | |
| `0x884` | Ständerbau mit Steg | 32x32 | |
| `0x885` | Ständerbau mit Steg | 32x32 | |
| `0x886` | Ständerbau mit Steg | 20x30 | |
| `0x887` | **Turm mit Andockstelle** (Luftschiff) | 32x32 | |
| `0x889` | **Treppe** | 14x14 | |
| `0x8FE` | **Wachturm** | 20x20 | |
| `0x8FF` | **Durchgang** | 32x32 | |
| `0x903` | **Schmiede** | 32x31 | ★ |
| `0x9E2` | **Standfackeln** | 5x5 | |
| `0x9F4` | **Schiff** | 16x26 | |

## Wichtig: zwei getrennte Nummernkreise

Die Indizes oben sind **Registry-Modelle** und werden mit
`NativeHouseProbe::PlaceBuilding` gesetzt (intern `placeStructure`).

Die Objekttypen weiter unten (`0x15` Marktstand, `0x32` Laterne, …) sind eine
**andere Nummerierung** und werden mit `AppendProp` gesetzt. Ein Baum ist also
Registry-`0x832`, NICHT Objekttyp `0x832`. Verwechseln der beiden war die
Ursache mehrerer Fehlversuche.

## Noch offen

Durchgesehen ist erst ab 0x839. Der Browser startet dort (STRG+O). Gesucht:

- Gasthaus / Laden
- Zäune und Tore, die zu den Häusern passen
- Blumen als Ganzmodelle

---

## Wie die Modelle in die Welt kommen

Ein Modell ist eine `.cub`-Voxeldatei. `WorldInfo_placeStructure @ 0x5F0CE0`
stanzt sie als **Blöcke** in den Chunk — deshalb sieht ein Objektscan der
`static_objects` keine Wände, ein Blockscan aber schon.

Aufruf (`__stdcall`, 9 Argumente, durch Aufrufstellen-Analyse belegt):

```c
w = tmpl->field_0x44;  h = tmpl->field_0x48;
if (rotation & 1) swap(w, h);
pos = { baseX - w/2, baseY - h/2, z };     // ZENTRIERT, nicht Ecke
WorldInfo_placeStructure(tmpl, pos, rotation, 6, 0, zone, 1, farbe, 0);
```

Danach muss neu vermescht werden (`Chunk::needs_update = 1` auf allen geladenen
Chunks), sonst bleibt alles unsichtbar.

## Wie der Generator Häuser zusammensetzt

`Chunk_generateObjects @ 0x5AF170`:

1. `House::ctor_0(3,3,4)` — Raster aus 3x3x4 Zellen à 12 Byte
2. Zellen füllen: `VoxelChunk_cellAt(x,y,z)[0] = 1|2|3` (Etage | Fundament | Dach),
   `[+3]` / `[+8]` markieren Tür und Fenster
3. je Zelle ein `placeStructure` mit der passenden Vorlage aus dem Stilsatz

Rastermaße, unabhängig bestätigt durch den Objektscan eines nativen Hauses
(die Wandlampen standen exakt darin):

- **13 Blöcke** je Hauszelle in X und Y
- **7 Blöcke** je Stockwerk in Z
- Wände sitzen 6–7 Blöcke von der Zellmitte auf der Zellkante

## Objekttypen (static_objects, andere Nummerierung als die Registry!)

Aus dem Objektscan eines nativen Hauses und aus vorhandenem Mod-Code:

| Typ | Was | Belegt durch |
|---|---|---|
| `0x01`, `0x03` | Wandlampen | Objektscan, 13er-Raster |
| `0x13` | Tisch/Stuhl (Vierergruppe) | Objektscan |
| `0x15`–`0x17` | Marktstände | Mod, sichtbar |
| `0x18` | Fass | Mod |
| `0x19`, `0x1A` | Kiste | Mod |
| `0x2F`–`0x31` | Blumenkästen | Mod |
| `0x32` | Laterne | Mod |
| `0x34`–`0x37` | Zaunpfosten | Objektscan (Reihe im 2er-Takt) |
| `0x48` | Amboss | Mod |
| `0x4D` | CustomizationBench | Mod |

Diese Nummern sind **nicht** dieselben wie die Registry-Indizes oben.
