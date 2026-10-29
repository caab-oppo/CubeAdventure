# Native Dorfgebäude im Bergdorf verwenden — Rechercheergebnis

**Kurz:** Der Mod benutzt bereits den richtigen Mechanismus. Was fehlt, sind drei
Dinge: die Liste der nativen Objekttypen, die nativen Skalenwerte und der
Aufruf von `World_scaleFixedByLevel` — die Funktion, die ein Objekt sauber auf
den Boden setzt. Die eigentlichen Hauswände sind dagegen **keine** Objekte,
sondern Voxelblöcke; die lassen sich nicht als Fertigteil kopieren.

Alle Adressen statisch, ImageBase `0x400000`.

---

## 1. Zwei getrennte Mechanismen — das ist der Kern

Ein natives Dorf besteht aus zwei völlig verschiedenen Dingen, und nur eines
davon ist wiederverwendbar:

| | Wandflächen, Dächer, Böden | Werkbänke, Fässer, Zäune, Marktstände |
|---|---|---|
| **Speicherung** | Voxelzellen im Chunk | `zone_static_object`, 0x188 Byte |
| **Erzeugt von** | `VoxelChunk_cellAt` (239 Aufrufe im Generator) | `std_vector_pushBack_0x188` |
| **Kopierbar?** | nein — Blöcke, kein Prefab | **ja** |

Der Dorfgenerator ist `Chunk_generateObjects @ 0x5AF170` (7734 Zeilen
Pseudo-C). Er malt die Häuser Zelle für Zelle in den Chunk. Es gibt also kein
"Haus Nr. 3"-Fertigteil, das man anfordern könnte — genau deshalb baut
`MountainVillage` seine Häuser zu Recht selbst aus Blöcken.

Was man dagegen sehr wohl übernehmen kann, ist die **gesamte Einrichtung**: alle
Werkbänke, Handwerksstationen und Deko-Props der nativen Dörfer sind
0x188-Objekte, und die hängt der Mod mit `AppendBuilding()` bereits an.

---

## 2. Die native Platzierungssequenz

Aus dem Generator (z. B. `game_misc.cpp:29199`), dreistufig:

```c
Struct_InitDefaults();                       // 0x58EAA0 — Grundwerte
obj[0]  = 0x12;                              // Typ
setVec3(3.0f, 0.4f, 0.4f); vec3_copy(...);   // Skalen -> +0x24/+0x28/+0x2C
vec6_set(x,y,z); vec6_copy(...);             // Position fixed16 -> +0x08/+0x10/+0x18
obj[8]  = 0;                                 // +0x20 Orientierung

if (World_scaleFixedByLevel(obj, zone, 1))   // 0x5F4DA0 — Bodenanpassung, bool
    std_vector_pushBack_0x188(obj);          // 0x5A0E00 — anhängen
```

`Struct_InitDefaults` setzt exakt das, was `FillBuilding()` im Mod schon von Hand
schreibt (`+0x30 = 1`, `+0x34/0x38/0x40/0x44 = 0`, `+0x178/0x17C/0x180 = -1`).
Die Rekonstruktion im Mod ist also korrekt — bis auf `+0x184 = 0`, das der
`memset` ohnehin abdeckt.

### Der fehlende Schritt: `World_scaleFixedByLevel @ 0x5F4DA0`

Der Name aus dem Audit führt in die Irre. Die Funktion liest die Grundfläche des
Objekts (`+0x08..+0x1F`), rechnet sie über `__alldiv` in Blockkoordinaten um,
läuft mit `world_getChunkCell256` über jede Zelle der Fläche, prüft die
Materialbits (`& 0x1F`) auf Luft/Wasser und **schiebt das Objekt so lange in Z,
bis es auf festem Grund steht**. Rückgabe ist ein `char`: platzierbar ja/nein.

Das ist genau der Grund, warum native Props nie in der Luft hängen oder im Fels
stecken — und warum der Mod die Deckhöhe bisher selbst schätzen muss.

---

## 3. Objekttyp-Katalog

### Bedienbare Stationen

Belegt aus dem Interaktions-Schalter in `GameController::vfunc_10 @ 0x488EE0`
(ab `GameController.cpp:39478`). Jeder Fall setzt eine andere Aufforderungs-
Textzeile — das ist der Beweis, dass es eigenständige Stationstypen sind:

| Typ | Text-ID | Anmerkung |
|---|---|---|
| `0x10`, `0x12` | `0xBB` | gemeinsame Zeile |
| `0x13`, `0x44`, `0x45` | `0xBC` | gemeinsame Zeile |
| `0x2D` | `0xBD` | |
| `0x47` | `0xBE` | |
| `0x48` | `0xBF` | |
| `0x49` | `0xC0` | |
| `0x4A` | `0xC1` | |
| `0x4B` | `0xC2` | |
| `0x4C` | `0xC3` | |
| **`0x4D`** | `0xC4` | **CustomizationBench** — die vom Mod bereits genutzte |

`0x4D` ist der Typ, den `BlacksmithNpc::OnBenchPlaced` schon registriert. Die
Nachbarn `0x47`–`0x4C` sind damit die übrigen Handwerksstationen des nativen
Dorfes und über denselben Weg setzbar.

Weiter unten in derselben Funktion (`iVar8 != 0x47 && != 0x48 && != 0x4B &&
!= 0x4C && != 0x49 && != 0x4A && != 0x41`) werden `0x41` und `0x2D` gesondert
behandelt — die haben eigene Öffnungslogik.

### Deko und Möbel

Aus dem Dorfgenerator extrahiert, mit den nativ verwendeten Skalen
(`+0x24` / `+0x28` / `+0x2C`):

| Typ | Skalen | Fundstelle |
|---|---|---|
| `0x0C` | 3.0 / 3.0 / 1.0 | `game_misc.cpp:27789`, `28083` |
| `0x0D` | — | `27202` |
| `0x10` | 1.0 / 1.0 / 0.5 | `27826`, `27853` |
| `0x12` | 3.0 / 0.4 / 0.4 | `29224`–`29341`, viermal |
| `0x13` | 2.0 / 3.0 / 1.0 | `27649`, `27679` |
| `0x1F` | 4.0 / 1.0 / 1.0 | `27950`, `27977` |
| `0x15`–`0x17` | 3.5 / 2.0 / 3.0 | `spawnRandomObject3 @ 0x5BFF10`, `rand()%3 + 0x15` |

Zum Vergleich: `FillBuilding()` im Mod setzt pauschal 2.0 / 1.0 / 1.5. Für die
Werkbank funktioniert das, für die übrigen Typen sind die nativen Werte die
bessere Ausgangsbasis.

`spawnRandomObject3(out, pos, zone)` ist übrigens eine fertige Fabrik für
Zufallsdeko: sie initialisiert das komplette 0x188-Objekt, würfelt den Typ aus
`{0x15, 0x16, 0x17}` und setzt Position und Skalen. Ein einzelner Aufruf ersetzt
`FillProp` vollständig.

---

## 4. Was NPCs damit anfangen

`RandomInteractionBehavior` (`cube/ai/RandomInteractionBehavior.cpp:220 ff.`)
läuft über denselben 0x188-Vektor und prüft je Objekt `+0x40` und `+0x44` auf
Null — das ist die Belegungsmarkierung. Steht ein Dorfbewohner mit dieser
Behavior in Reichweite eines freien Objekts, benutzt er es.

Die Zwerge in `SpawnVillagers` bekommen derzeit nur `AttachRandomWalk`. Hängt man
ihnen zusätzlich eine `RandomInteractionBehavior` an, benutzen sie die
platzierten Stationen von selbst — dafür ist keine eigene Logik nötig.

---

## 5. Empfohlenes Vorgehen

1. **`World_scaleFixedByLevel` einbinden.** Größter Gewinn bei kleinstem Risiko:
   ein Aufruf vor `AppendBuilding`, Rückgabewert prüfen. Damit sitzen alle Props
   automatisch richtig, statt über `ComputeDeckZ` geschätzt zu werden.
   Konvention vorher mit `DetectArgModel` bestimmen — drei Argumente,
   `char`-Rückgabe.
2. **Stationstypen `0x47`–`0x4C` versuchsweise setzen**, je eine pro Testlauf,
   mit den nativen Skalen. `BlacksmithNpc::OnBenchPlaced` zeigt bereits, wie man
   die zugehörige UI öffnet — dasselbe Muster gilt für die anderen Typen mit
   ihrer jeweiligen Text-ID.
3. **`spawnRandomObject3` für Deko aufrufen** statt `FillProp` nachzubauen.
4. **`RandomInteractionBehavior` an die Zwerge hängen**, damit das Dorf lebt.

## 6. Ungeprüft

- Die Text-IDs `0xBB`–`0xC4` sind nicht in Klartext aufgelöst; welcher Typ welche
  Station *ist*, steht damit noch nicht fest. Das klärt am schnellsten ein
  Testlauf: je einen Typ setzen und die Aufforderung im Spiel ablesen.
- `World_scaleFixedByLevel` ist aus dem Decompile gelesen, nicht gegen die
  `Cube.exe` verifiziert. Vor dem Hooken Prolog und Argumentmodell prüfen.
- Ob `+0x20` Orientierung oder Zone hält, ist widersprüchlich: `FillBuilding`
  behandelt es als Orientierung, `spawnRandomObject3` schreibt dort den
  Zone-Parameter. Im Generator steht an der Stelle `0`.
