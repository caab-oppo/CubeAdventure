# Native Häuser — Stand der Reverse-Engineering-Arbeit

**Kurz:** Native Gebäude lassen sich vom Mod aus setzen. Ein Aufruf von
`WorldInfo_placeStructure` mit `mode = 0x0D` und `extra = 1` stanzt das
Voxelmodell in den Chunk **und** baut die farbigen Markerblöcke selbst in
Türen, Fenster und Möbel um. Alle anderen Argumentbelegungen liefern entweder
gar nichts, ein Modell mit bunten Platzhaltern, oder Ausnahmen.

Alle Adressen statisch, ImageBase `0x400000`.

---

## 1. Adressen

| Adresse | Name | Konvention | Rolle |
|---|---|---|---|
| `0x5F0CE0` | `WorldInfo_placeStructure` | `__stdcall`, 9 Arg. | stanzt Modell + baut Marker um |
| `0x4120C0` | `vectorU32_at` | `__thiscall` (ECX = Registry) | Modellvorlage nach Index |
| `0x5AEB40` | `cube::House::ctor_0` | `__thiscall` | Hausraster 3×3×4 Zellen, 0x74 Byte |
| `0x5AF170` | `Chunk_generateObjects` | — | nativer Dorfgenerator, 7734 Zeilen |
| `0x5E4850` | `WorldInfo_generateBiomeContent` | — | Aufrufer davon (der Mod hookt das schon) |
| `0x4809A0` | `GameController::loadStructureBlob` | — | füllt die Registry beim Start |
| `0x47F9F0` | `isInteractableTileType` | — | Typ in {1, 0x0A, 0x0B, 0x0C, 0x0D, 0x15} |
| `0x488030` | `GameController_interactSpecialObject` | `__thiscall` | Typ in {1,2,3,0x0A,0x10,0x12,0x13,0x45} |
| `0x5A5A10` / `0x5A5A30` | `Structure_getWidth/HeightByRotation` | — | Maße je Drehung |

**Modell-Registry:** liegt bei `GC+0x300`, 2569 Einträge. Zur Laufzeit
gefunden, indem im GameController ein Tripel `{proxy, begin, end}` gesucht wird,
dessen Elementzahl bis 0x8EE reicht und dessen Einträge gültige Zeiger sind.

---

## 2. Die neun Argumente von `placeStructure`

```c
WorldInfo_placeStructure(tmpl, origin, rot, kind, mode, zone, flag, pal, extra)
```

| # | Name | Wert | Beleg |
|---|---|---|---|
| 1 | `tmpl` | Zeiger aus `vectorU32_at(index)` | — |
| 2 | `origin` | `int[3]`, **zentriert**: `base − breite/2` | Generator |
| 3 | `rot` | 0..3, im Code `−90° · rot` | Generator |
| 4 | `kind` | `6` | geht in `setColor4b(.., kind\|0x40)` |
| 5 | `mode` | **`0x0D`** | `if (zone == 0xd)` schaltet den Markerumbau frei |
| 6 | `zone` | `cube::Zone*` | hat `+0x60`/`+0x64` als Zonenkoordinaten |
| 7 | `flag` | `1` | Generator |
| 8 | `pal` | Zeiger auf 4 Nullen | Farbpuffer |
| 9 | `extra` | **`1`** | `if (param_9 != 0)` löscht den Markerblock |

Breite und Höhe stehen im Modell bei `+0x44` und `+0x48`; bei ungerader Drehung
werden sie getauscht.

**Nach dem Setzen muss neu vermescht werden** (`Chunk::needs_update = 1` auf
allen geladenen Chunks), sonst bleibt alles unsichtbar.

---

## 3. Das Markersystem — der Kern

Ein `.cub`-Modell enthält knallig gefärbte Platzhalterblöcke. `placeStructure`
vergleicht sie ab `WorldInfo.cpp:6363` per `color_equals3` und tauscht sie um:

```c
if (color_equals3(...)) {          // Markerfarbe erkannt
  if (zone == 0xd) {               // mode MUSS 0x0D sein
    Struct_InitDefaults();
    local_308[0] = 0x4d;           // Objekttyp, hier CustomizationBench
    std_vector_pushBack_0x188(...);// Objekt an die Zone anhängen
    if (param_9 != 0) {            // extra MUSS != 0 sein
      setColor3b(0,0,0);           // erst dann verschwindet der Marker
    }
  }
}
```

Beide Bedingungen sind unabhängig: `mode` erzeugt das Objekt, `extra` räumt den
Block weg. Mit `mode 1` entstanden deshalb Türen, während die bunten Blöcke
stehen blieben.

---

## 4. Rastermaße des nativen Hauses

Aus `Chunk_generateObjects`, **unabhängig bestätigt** durch einen Objektscan
eines echten Hauses (die Wandlampen standen exakt in diesem Raster):

- **13 Blöcke** je Hauszelle in X und Y
- **7 Blöcke** je Stockwerk in Z
- Wände sitzen 6–7 Blöcke von der Zellmitte auf der Zellkante
- Raster 3×3 Zellen, bis 4 Geschosse (`House::ctor_0(3,3,4)`)

Gemessenes Beispielhaus: Wände bei x = 1, 8, 21, 27 und y = −12, −5, 8, 14 —
Abstände 7 · 13 · 6, also 2×2 Zellen mit zwei Geschossen.

---

## 5. Zwei getrennte Nummernkreise

Das war die Quelle mehrerer Fehlversuche.

| | Registry-Modelle | Objekttypen |
|---|---|---|
| gesetzt mit | `PlaceBuilding` → `placeStructure` | `AppendProp` → 0x188-Record |
| Beispiel | `0x832` = Baum | `0x32` = Laterne |
| Wertebereich | 0…2568 | klein, < 0x100 |

Gleiche Zahl, völlig verschiedene Dinge.

Bestätigte Objekttypen: `0x01`/`0x03` Wandlampen, `0x13` Tisch/Stuhl,
`0x15`–`0x17` Marktstände, `0x18` Fass, `0x19`/`0x1A` Kiste, `0x2F`–`0x31`
Blumenkästen, `0x32` Laterne, `0x34`–`0x37` Zaunpfosten, `0x48` Amboss,
`0x4D` CustomizationBench.

Bestätigte Registry-Modelle siehe `MODELL_INDEX.md`.

---

## 6. Fehlschläge und was sie gelehrt haben

### Konventionsprüfung per Byte-Scan — falsch
Erster Versuch suchte linear nach `C3`/`C2` als Rücksprung. Ergebnis: „räumt
38283 Byte ab", und alle drei geprüften Funktionen meldeten dasselbe. Der Scan
war in ein Datenbyte gelaufen (`0x958B` = Teil eines `mov edx,[ebp+…]`). Ohne
Instruktionslängen-Dekoder kann das nicht funktionieren.
**Ersetzt durch Aufrufstellen-Analyse:** alle `call rel32` auf die Funktion
suchen und prüfen, ob dahinter ein `add esp,N` steht. Ergebnis eindeutig —
37 Aufrufstellen, keine mit Aufräumen, also `__stdcall`.

### Modellindizes aus der `.rdata`-Reihenfolge hergeleitet — falsch
Die Zeichenketten der Modellnamen liegen in einer bestimmten Reihenfolge im
Binary. Daraus abgeleitet, dass `0x8FE` der Landeplatz und `0x8FF` das
Kriegerhaus sei. Tatsächlich: Wachturm und Durchgang. Die Herleitung ist
verworfen, es zählt nur, was im Spiel gesehen wurde.

### Hausbau aus 13 Einzelteilen — unnötig
Annahme, ein Haus müsse aus `-base`, `-wall`, `-roof1` usw. zusammengesetzt
werden. Es gibt fertige Ganzgebäude (`0x87E`–`0x880`), ein Aufruf je Haus.
Der erste Versuch legte neunmal dieselbe Vorlage ins 3×3-Raster — Ergebnis
waren lose Wände und Aufgänge.

### Objektscan für die Hausschale — falscher Mechanismus
`zone->static_objects` enthält nur Möbel und Wandlampen. Die Wände sind Voxel,
weil `placeStructure` das `.cub`-Modell als **Blöcke** stanzt.

### `mode 1` — instabil
Erzeugte Türobjekte, warf aber bei drei von vier Gebäuden eine Ausnahme
(gemessen: 1/4 gesetzt). Die Bäume überlebten, weil sie klein sind und keine
Marker haben. Mit `mode 0` wurden vorher alle vier gesetzt.

### Versuchsreihen bis `mode 0xB` — zu kurz
Die Kombinationsliste deckte 1–7, 9, 10, 0xB ab. **`0x0D` war nie dabei** —
genau der Wert, auf den der Code prüft. Zusätzlich stand `extra` fast immer
auf 0, also wäre der Marker selbst bei richtigem `mode` nicht geräumt worden.

### Testhäuser nach außen gestaffelt — unsichtbar
Jede Kombination 40 Blöcke weiter versetzt, Nummer 8 lag bei +280 Blöcken und
damit jenseits der Renderweite (~150). Drei Kombinationen wurden nie gesehen.
Gleiches Problem beim Modell-Browser: 16 Modelle × 45 Blöcke = über 700.

---

## 7. Offen

- Ob `mode 0x0D` stabil über alle vier Gebäude läuft — die Konsole meldet je
  Gebäude „gesetzt" oder „FEHLGESCHLAGEN".
- Ob Türen damit auch **Kollision** haben, nicht nur Interaktion. Die beiden
  gefundenen Funktionen decken nur Letztere ab.
- Die Rampen unter den Häusern: `PlaceBuilding` setzt auf `deckZ + 1`, das
  Modell bringt einen eigenen Sockel mit. Höhenversatz noch nicht bestimmt.
- Welche Markerfarbe zu welchem Objekt wird — im Code steht je Farbe ein
  eigener Zweig, ausgelesen ist bisher nur `0x4D`.

## 8. Notbehelfe (auskommentiert in `PlaceBuilding`)

Falls der native Umbau doch nicht trägt:

- `MakeObjectsUsable(gc, cx, cy, cz, 26)` — setzt an allen Objektpositionen
  den Blocktyp auf 1.
- `MakeMarkersUsable(gc, cx, cy, cz, 20)` — findet Markerblöcke an ihrer
  Reinfarbe (ein Kanal ≥ 200, einer ≤ 60) und setzt deren Typ auf 1.

## 9. Werkzeuge im Mod

| Taste | Wirkung |
|---|---|
| STRG+7 | Vorprüfung: Registry, Prologe, Aufrufstellen |
| STRG+8 | Modell-Browser, 3×3-Raster mit Blockziffern beschriftet |
| STRG+O | Browser zurück auf 0x839 |
| STRG+L | ganze Registry nach `ModelRegistry.txt` |
| STRG+J / K | Objektscan → `HausScan.txt` / leeren |
| STRG+H / G | Voxelscan → `HausBloecke.txt` / leeren |
| STRG+Y | Argumentkombination weiterschalten + Testhaus 40 Blöcke voraus |
