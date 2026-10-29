# RE: Wie die nativen Dungeons entstehen – und wie man eigene hinzufügt

Stand 2026-10-19. Quellen: Reversal-Baum (`GameController.cpp`, `game_misc.cpp`, `WorldInfo.cpp`), Cube.exe-Disassembly, `RE_Biome.md` 6b.

**Kurzfassung:**
- Ein großer Dungeon (Burg, Ruine, Katakombe, Palast, Tempel, Pyramide) ist kein Modell aus einer Datei.
- Er wird beim Erzeugen seiner einen Zone komplett prozedural gebaut: Ein 3×3×3-Kammerraster wird zufällig verbunden, Räume werden per BSP geteilt, dann werden Wände/Böden blockweise geschrieben, Requisiten mit `placeStructure` gesetzt und Monster/Kisten als `cube::Spawn` angelegt.
- „Zufällig“ heißt `rand()`. Das Spiel setzt den Samen vorher aus Welt und Zone, deshalb ist jeder Dungeon pro Welt reproduzierbar.

---

## 1. Wo ein Dungeon hinkommt (Region, 16384 × 16384 Blöcke)

`GameController::generateRegion` 0x5DA280 (GameController.cpp:87800ff.):

1. Je 2048er-Teilfeld wird ein Ortskandidat gewürfelt (Ortstabelle `Region+0x14018 + i*0x68`). Die Kandidaten werden sortiert (`FUN_005c2270`).
2. Jeder **zweite** Kandidat wird Typ **14 = großer Dungeon** (88231: `+0x18 = 0xE`, Radius 150.0):
   - Der Mittelpunkt wird auf die Zonenmitte gerundet (`& ~0xFF + 0x80`).
   - Der **Zonen-Datensatz** dieser Zone (`Region+0x18 + idx*0x10`, idx = `(x>>8 & 63)*64 + (y>>8 & 63)`) bekommt Byte **+0 = 3** und Byte **+1 = Unterart = rand() % 4**.
   - Danach greift eine Klimaprüfung (`sampleHumidityGrid` > 0.8 …), die die Unterart nach Klima anpasst (Pyramide in trockenen Gegenden, VERMUTET nach den Namen).
3. **Unterarten** (Namenstabelle `World::ctor_1`): 0 Castle, 1 Ruins, 2 Catacombs, 3 Palace, 4 Temple, 5 Pyramid.
4. **Zonen-Datensatz Byte 5** erzeugt denselben Bau mit Unterart 3.

Der BiomeMod kann Typ 14 je Biom abschalten (Orte-Filter, `RE_Biome.md` 6b).

## 2. Wann er gebaut wird (Zone, 256 × 256 Blöcke)

- `WorldInfo_generateBiomeContent` 0x5E4850 (WorldInfo.cpp:2816) ruft für jede neue Zone auf:
  - Byte +0 == 3 → `World_populateRegionDecorations(zone, idx, x, y, unterart)` (0x5CC510)
  - Byte +0 == 5 → dieselbe Funktion mit Unterart 3
- Der ganze Dungeon passt in **eine** Zone. Er wird nicht über Zonengrenzen verteilt.

## 3. Der Bau: `World_populateRegionDecorations` 0x5CC510 (game_misc.cpp:36132, ~4000 Zeilen)

### 3a. Bewohner je Unterart

Zwei Listen (Vektoren mit 0x18-Einträgen über `World_getOrCreateEntry` 0x5F4C30), Zeilen 36545ff.:

| Unterart | Liste 1 | Liste 2 / Boss |
|---|---|---|
| 1 Ruins, 2 Catacombs | UndeadMale, UndeadFemale | UndeadMale/Female, Zombie; Boss zufällig Werewolf / Skeleton / Vampire |
| 3 Palace | HumanMale, HumanFemale | HumanMale/Female, 0x13 |
| 5 Pyramid | Horus, Anubis | Skeleton, Djinn, Mosquito, Cat |
| sonst (0 Castle, 4 Temple) | OrcMale, OrcFemale | Ogre, 0x13 |

0x13 ist laut Rassentabelle Collie, wahrscheinlich ist hier aber ein anderer Kennwert gemeint (VERMUTET).

### 3b. Grundriss

- **Kammerraster:** 3 × 3 × 3 Kammern (27 Einträge à 7 int, `local_334`/`local_338`). Pro Kammer werden Versatz, Größe und Flags gewürfelt (`rand()%3-1`, `rand()&1`, 36650ff.).
- **Verbindungen:**
  - Unterart 2 (Katakomben) bekommt feste Verbindungslisten (`std_list_insert_node_32`, 36700ff.).
  - Die anderen Unterarten verbinden zufällig.
- **Räume:** `dungeon_splitRegion_recursive` 0x5C52A0 teilt sie BSP-artig, setzt Trennwände und Türen (`World_copyCellRecord`). Rasterweite 7 bzw. 10 Blöcke (`… * 7`, `… * 10`).
- **Belegungsvolumen:** `voxelVolume_at` 0x5C46F0 mit Codes, z.B. 3 = frei/Gang, 4 = Eingang. Der Eingang wird als 4 markiert (`*puVar7 = 4`).

### 3c. Bauen

- **Wände und Böden:** blockweise über `tilemap_writeGlyphColumn` und `world_getColumnData`. Farbe und Material hängen von der Unterart ab.
- **Requisiten an Wänden:** `WorldInfo_placeStructure` in alle 4 Richtungen, sobald neben einer Zelle ein fester Block steht (39180ff.). Gemeint sind Fackeln, Regale, Türen …
- **Monster, Boss und Kisten:** `cube::Spawn::ctor` an 5 Stellen (38750ff., 39240). Rassen kommen aus 3a. Beute erzeugt `Item_generateStats`. Gespawnt wird wie sonst über Zone+0x18 und `updateSubsystems`.
- **Palast-Sonderfall:** jeder 10. Platz bekommt ein zusätzliches Objekt (`local_798 = 0x38`, 39200).

## 4. Höhlen und Landmarken sind etwas anderes

- **Typ 9 „Cave“:** kommt aus keiner Ortstabelle. Ausgewürfelt wird mit `rand() % 8` in GameController.cpp:88333–88440.
- **Typen 2–8, 11–15:** Landmarken. Gelände plus Requisiten über `World_buildPropInstance`, keine Innenräume.
- Die Modelle `overground-dungeon01.cub` (0x9EE) und `underground-dungeon01.cub` (0x9EF) sind Kartensymbole, keine Bauteile.

## 5. Eigene Dungeons hinzufügen – Wege

| Weg | Was | Aufwand / Risiko |
|---|---|---|
| **A. Bestand nutzen** | BiomeMod-Dungeons (`BiomeGen::MakeDungeon`, `dungeon_anteil` je Biom) und Zonen-Modelle der Art „Dungeon“ (.cub) gibt es schon. Sie entstehen mit der Zone. | klein, schon vorhanden |
| **B. Native Dungeons umbesetzen** | Hook auf 0x5CC510: vor dem Original die Bewohnerlisten (3a) je Unterart austauschen, z.B. Kull in der Burg, Titanen im Palast. Nach dem Original die Spawns in Zone+0x18 umschreiben (wie Biome II). | mittel, gut beherrschbar |
| **C. Neue Unterarten (6, 7, …)** | Hook auf 0x5CC510: Unterarten ≥ 6 baut der Mod selbst. `generateRegion`-Post-Hook setzt Byte +1 auf eigene Unterarten. Kartenname über die Namenstabelle (`World::ctor_1`). | groß |
| **D. Baukasten aus .cub-Räumen** | Eigener Generator wie nativ (Kammerraster 3×3×3, zufällige Verbindungen), aber jede Kammer ist ein Raum-Modell aus `Strukturen\Dungeons\Raeume\*.cub`. Türen markieren die Anschlüsse, Markerfarben stehen für Monster, Kisten, Boss und Fackeln. Gesetzt wird wie die Stadt (`StampInPlace`, zonenweise). Monster kommen als `cube::Spawn` in Zone+0x18, damit sie nativ entstehen, entladen werden und wiederkommen. | groß, aber am freiesten. Räume baust du im LAB-Editor. |

**Empfehlung:**
- **B zuerst**, als schneller Gewinn: Die nativen Dungeons werden mit eigenen Völkern und Tieren bewohnbar, sogar je Biom über Biome II.
- **Danach D**, wenn eigene Dungeon-Architektur gewünscht ist. Mit dem Raum-Baukasten lassen sich beliebig viele Dungeon-Arten anlegen, ohne Code zu ändern.

## 6. Umsetzung D (2026-10-19): `Dungeons.cpp`

**Einhängen:** `BiomeMod::ZonePlaceAll` → `ZoneDungeons`, direkt nach den kleinen Tempeln. Läuft im Zonen-Thread über `Hook_SunCall`, nach `Wildlife::OnZoneSpawns`.

**Spawns:**
- Erzeugt wie das Spiel: `operator new` 0x10F0, ctor 0x5ADB20, Aussehen 0x43F7C0, Ausrüster (`MobLab::NewSpawnNative`).
- GUID wird selbst gesetzt: `((zy<<16)+zx)<<8 + index` (wie `packWorldCoord64` 0x5C0120). Index = Position im Vektor, höchstens 250.
- Push: 0x66ADD0.

**Vase:** wie 0x5D257E – Rasse 0x8F, Feindlichkeit 6, Level = zone+0x80, +0xF58 = 10.0, Beute über `0x5C9F90(spawn+0xF6C, level, zone+0x84)`.

**Truhe:** wie 0x5D30E1 – Objekt 0x0A, dann:
1. `0x487380(obj+0x48, 5)`
2. 1–4 × (`0x5F7A60(World, Item, level, zone+0x84 + 1)` → `0x46EBE0(obj+0x48, item, -1)`)
3. `obj+0x30 = 2`

**Drehung:** `Lab::Structure::RotateZ90` ((x,y) → (sy−1−y, x)) dreht N→W→S→O. Die Türmaske wird beim ersten Aufruf am Modell gemessen.

## 7. Format 2 (2026-10-27): Module mit Anschlüssen

**Ablauf:** `Generate` (bis zu `versuche` Grundrisse) → `ZonePlace` (Platz in der Zone, eben bis `uneben`) → `Build`. Build macht nacheinander:
1. Module von unten nach oben stempeln, mit Fundament.
2. Offene Anschlüsse zumauern.
3. Zinnen auf freie Dachkanten setzen.
4. Eingangstreppe bauen.
5. Spawns, Truhen und Lichter setzen.

**Drehung:** `RotateZ90` (x,y) → (sy−1−y, x). Anschlusspunkte werden in doppelten Koordinaten gerechnet: (X,Y) → (2·sy − Y, X), Richtung N→W→S→O.

**Fundament:** `placeStructure` param_7 füllt jede Säule, deren Modell-Voxel bei z=0 belegt ist, nach unten bis zum Boden, solange dort Luft oder Wasser ist (`RE_placeStructure.md` Z.5432). Darum haben alle Module bei z 0–1 einen durchgehenden Boden.
