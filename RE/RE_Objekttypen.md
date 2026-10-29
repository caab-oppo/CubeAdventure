# RE: Objekttypen — welcher Typwert ist welches Bauteil

**BLUF:** Die vollstaendige Typtabelle steht als Klartext im Binary. `GameController.cpp:22583`
legt einen Vektor mit **0x4E Eintraegen** an und fuellt ihn mit `tabelle[Objekttyp] = Modell`.
Damit ist jeder Typ direkt einem `.cub`-Modellnamen zugeordnet — **kein Raten noetig**:
Tuer = 1, Fenster = 3, Truhe = 0x0A, Tisch = 0x0C, Stuhl = 0x10, Bank = 0x12, Bett = 0x13,
Marktstand = 0x15..0x17, Blumenkasten = 0x2F..0x31, Zaun = 0x34..0x37, Ofen = 0x47,
Amboss = 0x48, Werkbank = 0x4C.
Alle 78 Eintraege sind unten aufgefuehrt.

**Zweite Kernaussage: es gibt ZWEI getrennte Typraeume.** "Schild" und "Fackel" sind **keine**
0x188-Objekte, sondern Eintraege der `std::list` bei `chunk+0x04` mit eigener Nummerierung
(`GameController.cpp:22407`, 0x40 Eintraege). Wer ein Schild als 0x188-Record mit Typ 0x22
anlegt, bekommt ein Regal (shelf3), kein Schild.

Quelle: Ghidra-Decompile `<Downloads>\CubeWorld-Reversal-master-new`,
Client = `cube/`, Server = `server/`. ImageBase 0x400000, 32-Bit MSVC.
Baut auf `RE_Interaktion.md` und `RE_placeStructure.md` auf.

---

## 1. Die Beweisstelle (Weg 1)

In `cube::GameController::init` (Funktionsrumpf ab ca. `GameController.cpp:21900`) werden
**nacheinander zwei Modelltabellen** angelegt:

```c
/* --- TABELLE 1: PROPS / DEKO, 0x40 = 64 Eintraege --- */
local_6f50 = (uint *)0x0;
std_vector_resize_erase_4(0x40, &local_6f50);          // GameController.cpp:22407
puVar9 = (undefined4 *)vector_at_stride4(0);           // Index = PROP-TYP
uVar2   = vectorU32_at(0x977);                          // Modell "flowers2.cub"
*puVar9 = uVar2;
...                                                     // bis Zeile 22580

/* --- TABELLE 2: STATIC OBJECTS (0x188-Record), 0x4E = 78 Eintraege --- */
local_6f5c = 0.0;
std_vector_resize_erase_4(0x4e, &local_6f5c);          // GameController.cpp:22583
puVar9 = (undefined4 *)vector_at_stride4(0);           // Index = OBJEKTTYP (Record+0x00)
uVar2   = vectorU32_at(0x9b1);                          // Modell "goddess2.cub"
*puVar9 = uVar2;
...                                                     // bis Zeile 22808
```

Die Modellindizes (`vectorU32_at(...)`) sind ueber den Modell-Lader aufloesbar. Beispiel
(`GameController.cpp:77589-77592`):

```c
uVar5 = string_ctorAppend(local_cd10, local_2c, "door.cub");
local_8 = 0x944;
vector_at_stride4(0x9b2);                               // <== Modellindex 0x9B2
VoxelModel_load_from_file_or_buffer(uVar5, puVar2, 1);
```

**Warum die Zuordnung sicher ist:** Tabelle 2 hat exakt 0x4E Eintraege, und 0x4D
(`customization-bench`) ist genau der hoechste Objekttyp, den der Interaktionscode kennt
(`GameController.cpp:39717`). Zusaetzlich stimmen **12 unabhaengig belegte Typen**
(1, 3, 0x0A, 0x10, 0x12, 0x13, 0x2D, 0x41, 0x44/0x45, 0x47, 0x48, 0x4D) mit dem
jeweiligen Modellnamen ueberein.

---

## 2. HAUPTTABELLE — StaticObject-Typ (`Record+0x00`, 0x188 Byte, Vektor `zone+0x0C`)

Spalte "Weg": **1** = Modelltabelle, **2** = Verhalten/UI-Text, **3** = Erzeugungsstelle.
Alle Zeilenangaben ohne Datei = `cube/control/GameController.cpp`.

| Typ | Was es ist | Modell | Weg-1-Beleg | Weg-2-Beleg (Verhalten) | Weg-3-Beleg (Erzeugung) | Wege |
|---|---|---|---|---|---|---|
| 0x00 | Goettinnenstatue | `goddess2.cub` | 22585 | — | — | 1 |
| **0x01** | **TUER** (Standardhaus) | `door.cub` | **22588** | schaltbar+fest (`server/game_misc/game_misc.cpp:45804`), Sonderobjekt (`:33928`), oeffnet/schliesst (`:97030`), Klang 0x36 (`game_misc.cpp:19855`), Tuertest `:86358` | Marker ROT, `world/WorldInfo.cpp:5726` (`(zone==7)+1`) | 1,2,3 |
| **0x02** | **TUER gross** (Burg, `zone==7`) | `big-door.cub` | **22591** | wie 0x01; `:86358` | `world/WorldInfo.cpp:5726` | 1,2,3 |
| **0x03** | **FENSTER** (Fachwerk) | `window.cub` | **22594** | schaltbar+fest (`45806`), Sonderobjekt (`:33928`), auf/zu (`:97030`) | Marker GRUEN, `world/WorldInfo.cpp:5867` | 1,2,3 |
| 0x04 | Fenster Burg | `castle-window.cub` | 22597 | immer fest (nicht in `{1,2,3,5,8}`) | — | 1 |
| 0x05 | Tor | `gate.cub` | 22600 | schaltbar+fest (`45806`), **eigener Klang 0x33** (`game_misc.cpp:19844`) | — | 1,2 |
| 0x06 | *kein Modell* | — | fehlt in Tabelle | nie fest (`45797`), Klang 0x35 nur beim Oeffnen (`19846`) | — | siehe UNSICHER |
| 0x07 | Stachelfalle | `spike-trap.cub` | 22603 | **nie fest** (`45797`), Klang 0x34 nur beim Ausloesen (`19850`) | — | 1,2 |
| 0x08 | Stampffalle | `stomp-trap.cub` | 22606 | schaltbar+fest (`45804`) | — | 1,2 |
| 0x09 | **Hebel** (mit Partnerobjekt) | `lever.cub` | 22609 | **nie fest** (`45797`), schaltet sich **und** das verlinkte Objekt `+0x178` (`:97051-97058`) | — | 1,2 |
| **0x0A** | **TRUHE (Korpus)** | `chest-base02.cub` | **22612** | Sonderobjekt (`:33928`), Loot + `+0x30=2` = geplündert, dann rieseln Bloecke (`:97001`, `game_misc.cpp:39050`) | — | 1,2 |
| 0x0B | Truhendeckel | `chest-top02.cub` | 22615 | **nicht** interaktiv (fehlt in `:33928`) | — | 1 |
| **0x0C** | **TISCH (Holz)** | `table.cub` | **22618** | immer fest | Marker OLIV, `world/WorldInfo.cpp:6134`; `game_misc.cpp:27789`, `28083` | 1,3 |
| 0x0D | Tisch (Stein) | `stone-table.cub` | 22621 | immer fest | — | 1 |
| 0x0E | *Eintrag fehlt* — vermutlich Tisch (Sandstein) | (`sandstone-table.cub`, Modell 0x9BE geladen, nie zugewiesen) | Luecke bei 22623 | — | — | siehe UNSICHER |
| 0x0F | Stuhl (Stein) | `stone-stool.cub` | 22624 | **nicht** sitzbar (fehlt in `:96966`) | — | 1 |
| **0x10** | **STUHL / Hocker (Holz)** | `stool.cub` | **22627** | **Sitzen**: Zustand 0x53 `'S'`, Blick `(rot+2)*90` (`:96966-96977`), Sonderobjekt (`:33928`), Tagesplatz eines Hauses (`game_misc.cpp:28108`) | Marker OLIV, `world/WorldInfo.cpp:6156`; `game_misc.cpp:27826-27907` (4x); `spawnRandomObject7` `game_misc.cpp:30826` | 1,2,3 |
| 0x11 | Stuhl (Sandstein) | `sandstone-stool.cub` | 22630 | **nicht** sitzbar | — | 1 |
| **0x12** | **BANK** | `bench.cub` | **22633** | **Sitzen** wie 0x10 (`:96966`), Sonderobjekt (`:33928`), Tagesplatz (`game_misc.cpp:28108`) | `game_misc.cpp:29201/29238/29278/29315`; `spawnRandomObject7` `:30820` | 1,2,3 |
| **0x13** | **BETT** | `bed.cub` | **22636** | **Liegen**: Zustand 0x54 `'T'`, Blick `rot*90` (`:96978-96990`), Sonderobjekt (`:33928`), Nachtplatz (`game_misc.cpp:28118`) | Marker ORANGE, `world/WorldInfo.cpp:5996`; `game_misc.cpp:27610` | 1,2,3 |
| 0x14 | Nachttisch | `bedtable.cub` | 22639 | immer fest | Marker ORANGE (2. Objekt), `world/WorldInfo.cpp:6014` | 1,3 |
| **0x15** | **MARKTSTAND 1** | `market-stand1.cub` | **22642** | immer fest | `spawnRandomObject3` @0x5BFF10, `game_misc.cpp:31183` (`rand()%3 + 0x15`) | 1,3 |
| **0x16** | **MARKTSTAND 2** | `market-stand2.cub` | **22645** | immer fest | dito | 1,3 |
| **0x17** | **MARKTSTAND 3** | `market-stand3.cub` | **22648** | immer fest | dito | 1,3 |
| 0x18 | Fass | `barrel.cub` | 22651 | immer fest | `spawnRandomObject7` `game_misc.cpp:30791` | 1,3 |
| 0x19 | Kiste | `crate.cub` | 22654 | immer fest | `game_misc.cpp:30797` | 1,3 |
| 0x1A | Kiste offen | `open-crate.cub` | 22657 | immer fest | `game_misc.cpp:30805` | 1,3 |
| 0x1B | Sack | `sack.cub` | 22660 | immer fest | `game_misc.cpp:30814` | 1,3 |
| 0x1C | Unterstand | `shelter.cub` | 22663 | immer fest | `game_misc.cpp:30832` | 1,3 |
| 0x1D | Schrank | `cupboard.cub` | 22666 | immer fest | — | 1 |
| 0x1E | Schreibtisch | `desktop.cub` | 22669 | immer fest | — | 1 |
| 0x1F | Theke | `counter.cub` | 22672 | immer fest | `game_misc.cpp:27914-28031` (4x) | 1,3 |
| 0x20 | Regal 1 | `shelf1.cub` | 22675 | immer fest | Marker LACHS, `world/WorldInfo.cpp:6073` (`rand()%3 + 0x20`) | 1,3 |
| 0x21 | Regal 2 | `shelf2.cub` | 22678 | immer fest | dito | 1,3 |
| 0x22 | Regal 3 | `shelf3.cub` | 22681 | immer fest | dito | 1,3 |
| 0x23..0x25 | Regal Burg 1..3 | `castle-shelf1..3.cub` | 22684/22687/22690 | immer fest | — | 1 |
| 0x26..0x28 | Regal Stein 1..3 | `stone-shelf1..3.cub` | 22693/22696/22699 | immer fest | — | 1 |
| 0x29..0x2B | Regal Sandstein 1..3 | `sandstone-shelf1..3.cub` | 22702/22705/22708 | immer fest | — | 1 |
| 0x2C | Leiche | `corpse.cub` | 22711 | immer fest | — | 1 |
| 0x2D | **Runenstein / Teleport** | `runestone.cub` | 22714 | loest `+0x178` auf, Effekt 0x2F, **versetzt den Spieler** (`:96928-96950`, `:39712`) | — | 1,2 |
| 0x2E | Artefakt | `artifact.cub` (0x81F) | 22717 | immer fest | — | 1 |
| **0x2F** | **BLUMENKASTEN 1** | `flower-box01.cub` | **22720** | immer fest | Marker A (Farbe verloren) + `humid > 0.5`, `world/WorldInfo.cpp:6969` (`rand()%3 + 0x2F`) | 1,3 |
| **0x30** | **BLUMENKASTEN 2** | `flower-box02.cub` | **22723** | immer fest | dito | 1,3 |
| **0x31** | **BLUMENKASTEN 3** | `flower-box03.cub` | **22726** | immer fest | dito | 1,3 |
| **0x32** | **STRASSENLATERNE 2** | `street-light02.cub` | **22729** | immer fest | — | 1 |
| **0x33** | **STRASSENLATERNE 1** | `street-light01.cub` | **22732** | immer fest | — | 1 |
| **0x34** | **ZAUN 1** | `fence01.cub` | **22762** | immer fest | `game_misc.cpp:24968`, `25029` (`(rand()&3) + 0x34`) | 1,3 |
| **0x35** | **ZAUN 2** | `fence02.cub` | **22765** | immer fest | dito | 1,3 |
| **0x36** | **ZAUN 3** | `fence03.cub` | **22768** | immer fest | dito | 1,3 |
| **0x37** | **ZAUN 4** | `fence04.cub` | **22771** | immer fest | dito | 1,3 |
| 0x38..0x40 | Blumenvase 1..9 | `flower-vase1..9.cub` (0x912..0x91A) | 22735-22759 | immer fest | Marker E (Farbe verloren), `world/WorldInfo.cpp:6680` (`rand()%9 + 0x38`) | 1,3 |
| **0x41** | **LAGERFEUER** | `campfire.cub` | **22795** | **UI-Text `L"Requires Campfire"`** (`:12483`, Typtest `:12460`), oeffnet UI ohne `set_field_1b4` (`:39743`) | `game_misc.cpp:22700` | 1,2,3 |
| 0x42 | Zelt | `tent.cub` | 22798 | immer fest | — | 1 |
| 0x43 | Sonnenschirm | `beach-umbrella.cub` | 22801 | immer fest | — | 1 |
| 0x44 | **Strandtuch (Liegen)** | `beach-towel.cub` | 22804 | **Liegen** (`:96982`), fehlt aber im Sonderobjekt-Set (`:33928`) -> Originalfehler | — | 1,2 |
| 0x45 | **Holzmatte (Liegen)** | `wood-mat.cub` | 22807 | **Liegen** (`:96982`) **und** Sonderobjekt (`:33928`) | — | 1,2 |
| 0x46 | *kein Modell, unbenutzt* | — | Luecke bei 22773 | — | — | siehe UNSICHER |
| **0x47** | **OFEN (Schmiede)** | `furnace.cub` | **22774** | **UI-Text `L"Requires Furnace"`** (`:11738`, Typtest `:11706`); Handwerks-UI **mit** `set_field_1b4` (`:39749`) | `world/WorldInfo.cpp:6820` (`zone == 0x0D`) | 1,2,3 |
| **0x48** | **AMBOSS (Schmiede)** | `anvil.cub` | **22777** | **UI-Text `L"Requires Anvil"`** (`:11865`, Typtest `:11833`); UI ohne `set_field_1b4` (`:39745`) | `world/WorldInfo.cpp:6717` (`zone == 0x0D`) | 1,2,3 |
| **0x49** | **SPINNRAD (Weberei)** | `spinningwheel.cub` | **22789** | **UI-Text `L"Requires Spinning Wheel"`** (`:11992`, Typtest `:11960`) | `world/WorldInfo.cpp:6930` (`zone == 0x0F`) | 1,2,3 |
| **0x4A** | **WEBSTUHL (Weberei)** | `loom.cub` | **22792** | **UI-Text `L"Requires Loom"`** (`:12119`, Typtest `:12087`) | `world/WorldInfo.cpp:6786` (`zone == 0x0F`) | 1,2,3 |
| **0x4B** | **SAEGEBANK (Zimmerei)** | `sawbench.cub` | **22780** | **UI-Text `L"Requires Saw"`** (`:12246`, Typtest `:12214`) | `world/WorldInfo.cpp:6853` (`zone == 0x0E`) | 1,2,3 |
| **0x4C** | **WERKBANK (Zimmerei)** | `workbench.cub` | **22783** | **UI-Text `L"Requires Workbench"`** (`:12361`, Typtest `:12338`) | `world/WorldInfo.cpp:6750` (`zone == 0x0E`) | 1,2,3 |
| **0x4D** | **ANPASSUNGSBANK** | `customization-bench.cub` | **22786** | oeffnet **zwei** Widgets, Sonderpfad vor allen anderen (`:39717`) | `world/WorldInfo.cpp:6379` (`zone == 0x0D`) | 1,2,3 |

### 2a. Die Handwerks-Trias — dreifache Bestaetigung

Zone, Station, Werkzeugpaar und Schild passen exakt zusammen. Das ist der staerkste
Konsistenzbeweis im ganzen Befund, weil drei voneinander unabhaengige Codestellen
uebereinstimmen:

| `zone` (param_5) | Beruf | Station A (mit `set_field_1b4`) | Station B (ohne) | Schild (Prop) |
|---|---|---|---|---|
| 0x0D | Schmied | **0x47** furnace ("Requires Furnace") | **0x48** anvil ("Requires Anvil") | **0x26** `smithy-sign` |
| 0x0E | Zimmerer | **0x4B** sawbench ("Requires Saw") | **0x4C** workbench ("Requires Workbench") | **0x27** `carpentersshop-sign` |
| 0x0F | Weber | **0x49** spinningwheel ("Requires Spinning Wheel") | **0x4A** loom ("Requires Loom") | **0x28** `weavingmill-sign` |

Belege: Stationstypen `WorldInfo.cpp:6717/6750/6786/6820/6853/6930`,
UI-Texte `GameController.cpp:11738/11865/11992/12119/12246/12361`,
Schilder `WorldInfo.cpp:7041-7056`, Modellzuordnung `GameController.cpp:22774-22792` bzw. `:22517-22523`.

---

## 3. ZWEITE TABELLE — Prop-Typ (`std::list` bei `chunk+0x04`, Knoten 0x48 Byte)

**Diese Typen sind NICHT mit den 0x188-Objekttypen austauschbar.** Sie haben keine
Kollision und keine Interaktion; sie werden ueber `list_pushBack_via5c03f0` eingehaengt.
Quelle Tabelle: `GameController.cpp:22407-22580`.

| Prop-Typ | Modell | Zeile | Bedeutung |
|---|---|---|---|
| 0x00..0x04 | `flowers2`, `flowers`, `grass`, `grass2`, `grass3` | 22409-22421 | Bodenbewuchs |
| 0x05..0x07 | `underwater-plant`, `coral`, `alga` | 22427/22424/22430 | Unterwasser |
| 0x08..0x0C | `lava-flower`, `lava-grass`, `thorn-plant`, `echinacea2`, `leaf` | 22433-22445 | Bewuchs |
| **0x0D** | **`lantern02.cub` — LATERNE** | **22448** | Marker GELB, `WorldInfo.cpp:6257`; `game_misc.cpp:27235`, Skala (0.6, 0.5, 0.4) |
| **0x0E** | **`torch.cub` — FACKEL** | **22451** | keine Erzeugungsstelle im Decompile gefunden |
| 0x0F/0x10 | `stone`, `stone2` | 22454/22457 | Steine |
| 0x11 | `christmas-tree` | 22526 | |
| 0x12 | *kein Modell* | Luecke | siehe UNSICHER |
| 0x13..0x1F | `tendril` … `water-lily01` | 22460-22496 | Pflanzen |
| 0x20 | `water-lily02` | 22499 | |
| **0x21** | **`inn-sign.cub` — Gasthausschild** | **22502** | `WorldInfo.cpp:7003`, `zone == 2` |
| **0x22** | **`shop-sign.cub` — Ladenschild** | **22508** | `WorldInfo.cpp:7029`, `zone == 3` |
| **0x23** | **`weapon-shop-sign.cub`** | **22511** | `WorldInfo.cpp:7039`, `zone == 4` |
| **0x24** | **`armor-shop-sign.cub`** | **22514** | `WorldInfo.cpp:7034`, `zone == 5` |
| **0x25** | **`identifier-sign.cub`** | **22505** | `WorldInfo.cpp:7024`, `zone == 6` |
| **0x26** | **`smithy-sign.cub`** | **22517** | `WorldInfo.cpp:7044`, `zone == 0x0D` |
| **0x27** | **`carpentersshop-sign.cub`** | **22520** | `WorldInfo.cpp:7049`, `zone == 0x0E` |
| **0x28** | **`weavingmill-sign.cub`** | **22523** | `WorldInfo.cpp:7055`, `zone == 0x0F` |
| 0x29..0x2B | `ivy`, `wall-roses-red`, `wall-roses-white` | 22529-22535 | Marker PETROL, `WorldInfo.cpp:5932` (`rand()%3 + 0x29`, nur bei `humid > 0.5` und 1 von 5) |
| 0x2C..0x2F | `inca-art1..4` | 22538-22547 | Wandbilder |
| 0x30 | `crest1.cub` — Wappen | 22550 | Marker DUNKELROT, `WorldInfo.cpp:6320` |
| 0x31 | `wall-skull` | 22556 | |
| 0x32 | `castle-chain` | 22553 | |
| 0x33..0x36 | `torch-read`, `torch-green`, `torch-blue`, `torch-yellow` | 22559-22568 | farbige Fackeln |
| 0x37 | `liana` | 22571 | |
| 0x38 | `chandelier` — Kronleuchter | 22574 | |
| 0x39/0x3A | `cobwebs`, `cobwebs2` | 22577/22580 | Spinnweben |

---

## 4. Braucht der Typ `+0x30`? — Kollisionsklassen

Belegt in `server/game_misc/game_misc.cpp:45796-45806`, wortgleich im Client
`cube/control/GameController.cpp:101265-101273`:

```c
iVar8 = *(int *)*puVar9;                                   // Record+0x00 = Typ
if ((iVar8 != 7) && (iVar8 != 6) && (iVar8 != 9)) {        // (1) nie fest
   if (nicht im Benutzt-Set creature+0x1468) {             // (2)
      iVar8 = *piVar10;
      if ((iVar8 != 1 && iVar8 != 8 && iVar8 != 2 &&
           iVar8 != 3 && iVar8 != 5) || (char)piVar10[0xc] != '\0') {
          /* Box aus +0x24/+0x28/+0x2C testen */           // piVar10[0xc] = +0x30
      }
   }
}
```

| Klasse | Typen | Braucht `+0x30`? |
|---|---|---|
| **nie fest** — man laeuft immer hindurch | **6, 7, 9** (— , spike-trap, lever) | Nein. `+0x30` steuert nur Klang/Animation. |
| **schaltbar fest** — fest genau dann, wenn `+0x30 != 0` | **1, 2, 3, 5, 8** (door, big-door, window, gate, stomp-trap) | **JA, zwingend.** `+0x30 = 1` = zu = fest, `+0x30 = 0` = offen = durchlaessig. |
| **immer fest** — Box aus `+0x24/0x28/0x2C` | **alle uebrigen**, u.a. 0x0A Truhe, 0x0C Tisch, 0x10 Stuhl, 0x12 Bank, 0x13 Bett, 0x15-0x17 Marktstand, 0x2F-0x31 Blumenkasten, 0x34-0x37 Zaun, 0x47-0x4D Handwerk | `+0x30` egal fuer Kollision. Trotzdem auf **1** lassen (Grundwert aus `Struct_InitDefaults`, `game_misc.cpp:16678`), sonst laeuft die Oeffnungsanimation falsch. |
| **Sonderfall Truhe** | 0x0A | `+0x30 = 1` intakt, `+0x30 = 2` = geplündert (`game_misc.cpp:39050`, Auswertung `GameController.cpp:97001`) |

---

## 5. Native Skalen (`+0x24`, `+0x28`, `+0x2C`, Halbmasse in Bloecken)

Alle Werte als Float-Immediates aus dem Decompile gelesen und dekodiert.

| Typ | Bauteil | Skala (sx, sy, sz) | Beleg |
|---|---|---|---|
| 0x0C | Tisch | **(3.0, 3.0, 1.0)** | `WorldInfo.cpp:6134`ff (`local_2e4 = 0x4040000040400000`-Block); `game_misc.cpp:27789` |
| 0x10 | Stuhl | **(1.0, 1.0, 0.5)** | `WorldInfo.cpp:6156`ff; `game_misc.cpp:27826`; `spawnRandomObject7` `game_misc.cpp:30826-30829` |
| 0x12 | Bank | **(3.0, 1.0, 0.4)** bzw. **(3.0, 0.4, 0.4)** | `spawnRandomObject7` `game_misc.cpp:30820-30823` / `game_misc.cpp:29224`ff |
| 0x13 | Bett | **(2.0, 3.0, 1.0)** | `WorldInfo.cpp:6005-6007` (`local_2e4 = 0x4040000040000000`, `local_2dc = 0x3f800000`); `game_misc.cpp:27649` |
| 0x14 | Nachttisch | **(1.0, 1.0, 1.0)** | `WorldInfo.cpp:6023-6026` |
| 0x15..0x17 | Marktstand | **(3.5, 2.0, 3.0)** | `spawnRandomObject3` @0x5BFF10, `game_misc.cpp:31184-31186` |
| 0x18 | Fass | **(2.0, 2.0, 2.0)** | `game_misc.cpp:30792-30794` |
| 0x19 | Kiste | **(s, s, s)**, s = 1.5 … 2.25 zufaellig | `game_misc.cpp:30798-30803` |
| 0x1A | Kiste offen | **(1.5r, 1.5r, 0.75r)**, r = 1.0 … 1.5 | `game_misc.cpp:30806-30812` |
| 0x1B | Sack | **(1.5, 1.5, 1.4)** | `game_misc.cpp:30815-30817` |
| 0x1C | Unterstand | **(3.0, 3.0, 2.5)** | `game_misc.cpp:30833-30835` |
| 0x1F | Theke | **(4.0, 1.0, 1.0)** | `game_misc.cpp:27914`ff |
| 0x20..0x22 | Regal | **(2.0, 3.0, 1.0)** | `WorldInfo.cpp:6086-6089` |
| 0x34..0x37 | **Zaun** | **(2.0, 1.0, 1.5)** | `game_misc.cpp:24966`, `25027` (`setVec3(0x40000000, 0x3f800000, 0x3fc00000)`) |
| 0x41 | Lagerfeuer | **(2.4, 2.4, 0.5)** | `game_misc.cpp:22700`ff (`local_330/32c/328`) |
| Prop 0x0D | Laterne | **(0.6, 0.5, 0.4)** | `game_misc.cpp:27233` (`setVec3(0x3f19999a, 0x3f000000, 0x3ecccccd)`) |

**Nicht rekonstruierbar** (Ghidra hat die `setVec3`-Argumente verworfen):
Tuer 1/2, Fenster 3, Blumenkasten 0x2F-0x31, Vase 0x38-0x40, Handwerk 0x47-0x4D.
Details unten unter UNSICHER.

Praktische Ableitung aus den belegten Werten: die Skala ist die **Halbmasse in Bloecken**
und passt jeweils zur Modellgroesse (Bank 3 lang / 0.4 hoch, Hocker 1x1x0.5, Zaun 2 breit /
1 tief / 1.5 hoch). Fuer eine Tuer ist damit etwa `(0.5, 0.1, 1.5)` plausibel — das ist
aber **VERMUTET**, nicht belegt.

---

## 6. Markerfarben (Weg 3, Rekonstruktionsversuch)

Neun der fuenfzehn Markerfarben stehen als Bytefolge im Decompile und sind damit **belegt**.
Sechs sind **nicht rekonstruierbar** — siehe Begruendung darunter.

### 6a. BELEGT

| Farbe (R,G,B) | Zeile Konstante | Erzeugt | Ziel |
|---|---|---|---|
| **(255, 0, 0)** rot | `WorldInfo.cpp:5650-5652` (`local_708`) | Typ **1**, bei `zone==7` Typ **2** — **TUER** | Vektor `chunk+0x0C` |
| **(0, 255, 0)** gruen | `WorldInfo.cpp:5694-5696` (`local_6fc`) | Typ **3** — **FENSTER** | Vektor |
| **(0, 127, 127)** petrol | `WorldInfo.cpp:5858-5860` (`local_704`) | Prop **0x29..0x2B** — Efeu / Wandrosen (nur `humid>0.5`, 1 von 5) | Liste `chunk+0x04` |
| **(255, 127, 0)** orange | `WorldInfo.cpp:5913-5915` (`local_70c`) | Typ **0x13 BETT** + Typ **0x14 Nachttisch** | Vektor (2x) |
| **(255, 127, 127)** lachs | `WorldInfo.cpp:5989-5991` (`local_714`) | Typ **0x20..0x22** — Regal | Vektor |
| **(127, 127, 0)** oliv | `WorldInfo.cpp:6062-6063` (`local_71c._0_2_ = 0x7f7f`, `._2_1_ = 0`) | Typ **0x0C TISCH** + Typ **0x10 STUHL** (zweimal platziert) | Vektor |
| **(255, 255, 0)** gelb | `WorldInfo.cpp:6125-6126` (`local_718._0_2_ = 0xffff`, `._2_1_ = 0`) | Prop **0x0D** — **LATERNE** | Liste |
| **(63, 0, 0)** dunkelrot | `WorldInfo.cpp:6249-6251` (`local_710`, `'?'` = 0x3F) | Prop **0x30** — Wappen (`crest1`) | Liste |
| **(0, 0, 255)** blau | `WorldInfo.cpp:6313-6315` (`local_700`) | Prop **0x21..0x28** — **SCHILD**, je nach `zone` (siehe 2a) | Liste |

### 6b. NICHT REKONSTRUIERBAR

| Kennung | Vergleichszeile | Erzeugt |
|---|---|---|
| A | `WorldInfo.cpp:6363-6365` | Typ **0x2F..0x31 BLUMENKASTEN** (`:6969`), nur bei `humid > 0.5` |
| B | `WorldInfo.cpp:6367-6369` | `zone==0x0D` -> **0x47 Ofen**; `0x0E` -> **0x4B Saegebank**; `0x0F` -> **0x49 Spinnrad**; sonst `cube::Spawn` (NPC) |
| C | `WorldInfo.cpp:6371-6373` | `zone==0x0D` -> **0x4D Anpassungsbank**, sonst nichts |
| D | `WorldInfo.cpp:6408-6411` | `zone==0x0D` -> **0x48 Amboss**; `0x0E` -> **0x4C Werkbank**; `0x0F` -> **0x4A Webstuhl** |
| E | `WorldInfo.cpp:6412-6415` | Typ **0x38..0x40** — Blumenvase |
| F | `WorldInfo.cpp:6416-6418` | `cube::Spawn` (NPC, 0x10F0 Byte) |
| W | `WorldInfo.cpp:7110-7113` | nur `zone==8`: Block mit Typ 2 (Wasser) |

**Warum nicht rekonstruierbar — ueberprueft, nicht angenommen:**
Die neun belegten Marker schreiben ihre Farbe in ein **Stack-Byte-Array**
(`local_708[0] = -1; local_708[1] = '\0'; local_708[2] = 0;`), das Ghidra sichtbar laesst.
Die sechs verlorenen rufen stattdessen `setColor3b(<3 Immediates>)` auf und uebergeben das
Ergebnis direkt an `color_equals3()`. Ghidra gibt diese Aufrufe **argumentlos** aus:

```c
uVar8 = setColor3b();          // WorldInfo.cpp:6363 -- Argumente verworfen
uVar24 = 0x5f33ea;             // <== Ruecksprungadresse, KEINE Farbe
cVar2 = color_equals3(uVar8);
```

Geprueft und ergebnislos geblieben:
* Es gibt im gesamten Decompile-Baum nur drei `setColor3b`-Aufrufe mit erhaltenen
  Argumenten (`(0,0,0)` 13x, `(0xff,0xff,0xff)` 2x, `(0xff,0,0)` 1x) — keiner davon liegt
  in der Markerkette.
* `WorldInfo_placeStructure` existiert **nur im Client**; der Server-Decompile
  (`server/`) enthaelt die Funktion nicht, also gibt es keine zweite Fassung zum Abgleich.
* Der Baum enthaelt **keine** `.rdata`-Auszuege, keine `DAT_`-Wertetabellen und keine
  Binaerdatei (`find . ! -name "*.cpp" ! -name "*.h"` liefert nur `.md`/`.tsv`).
* Die genannten `uVar24`-Werte (0x5F33EA, 0x5F39F7, 0x5F3B6D, 0x5F3FC4) liegen alle im
  Codebereich der Funktion selbst und sind Ruecksprungadressen.

**Um die sechs Farben zu bekommen, muss man das Disassembly von `0x5F2F80`-`0x5F33B0`
lesen** (die `push imm8`-Folgen direkt vor jedem `call setColor3b`). Aus diesem
Decompile heraus geht es nicht.

---

## 7. UNSICHER — was NICHT belegt ist, und warum

1. **Objekttyp 0x06 hat keinen Modelleintrag.** In Tabelle 2 springt der Index von 5
   (`gate`) direkt auf 7 (`spike-trap`), `GameController.cpp:22600 -> 22602`. Belegt ist nur
   das Verhalten: nie fest, Klang 0x35 nur beim Oeffnen (`game_misc.cpp:19846`) — also
   fallenartig wie Typ 7. *Vermutung:* eine zweite, im Release entfernte Falle. Ein
   0x06-Objekt waere **unsichtbar** (Nullzeiger in der Modelltabelle) und ist als
   Bauteil unbrauchbar.
2. **Objekttyp 0x0E fehlt in Tabelle 2** (`GameController.cpp:22621 -> 22623` springt von
   0x0D auf 0x0F). Gleichzeitig wird `sandstone-table.cub` als Modell **0x9BE** geladen
   (`GameController.cpp:77661-77663`) und **nirgends** einem Typ zugewiesen. Reihenfolge:
   0x0C `table`, 0x0D `stone-table`, **[Luecke]**, 0x0F `stone-stool`, 0x10 `stool`,
   0x11 `sandstone-stool`. *Vermutung (sehr stark, aber unbelegt):* 0x0E = Sandsteintisch,
   der Tabelleneintrag fehlt im Original — ein Bug, der das Objekt unsichtbar macht.
   **Vor Gebrauch im Spiel testen.**
3. **Objekttyp 0x46 hat keinen Modelleintrag** (`22807 -> 22773`, Luecke zwischen
   `wood-mat` und `furnace`). Keine Fundstelle irgendwo im Code. Vermutlich reserviert.
4. **Prop-Typ 0x12 fehlt in Tabelle 1** (`GameController.cpp:22457 -> 22459`). Kein
   uebriggebliebenes Pflanzenmodell erkennbar. Ungeklaert.
5. **Die Leseseite der Modelltabellen wurde nicht gefunden.** Ich habe `cube/render/`,
   `SmoothMeshShape.cpp` und alle `vector_at_stride4`-Aufrufe mit variablem Index geprueft;
   die Zeichenroutine, die `tabelle[obj->type]` liest, ist im Decompile nicht auffindbar
   (Argumente verworfen). Die Zuordnung Typ->Modell ruht damit auf der **Schreibseite**
   plus 12 unabhaengigen Verhaltensbestaetigungen — sie ist damit sehr gut gestuetzt,
   aber der Rendering-Lesezugriff selbst ist nicht direkt gezeigt.
6. **Skalen fuer Tuer (1/2), Fenster (3), Blumenkasten (0x2F-0x31), Vase (0x38-0x40) und
   alle Handwerksstationen (0x47-0x4D).** Diese Zweige setzen die Skala ueber
   `setVec3(); vec3_copy();` **ohne sichtbare Argumente** (z.B. `WorldInfo.cpp:6829-6831`
   fuer den Ofen, `WorldInfo.cpp:5791-5793` liest fuer die Tuer nur `local_46c/468/464`,
   deren Zuweisung nirgends im Quelltext steht). Nicht rekonstruierbar.
7. **RGB der Marker A-F und W.** Siehe Abschnitt 6b, dort mit Begruendung und
   Loesungsweg (Disassembly 0x5F2F80-0x5F33B0).
8. **Erzeugungsstellen fuer Truhe (0x0A), Runenstein (0x2D), Strassenlaterne (0x32/0x33),
   Fackel-Prop (0x0E), Burgfenster (0x04), Tor (0x05), Fallen (0x07/0x08), Hebel (0x09),
   Leiche (0x2C), Artefakt (0x2E), Zelt/Schirm/Tuch/Matte (0x42-0x45)** wurden nicht
   gefunden. In `game_misc.cpp:38309-38998` und `39019-39228` gibt es rund 20 weitere
   `std_vector_pushBack_0x188()`- bzw. `list_pushBack_via5c03f0()`-Aufrufe, bei denen
   Ghidra **sowohl das Argument als auch die Typzuweisung** verworfen hat. Diese Objekte
   stammen sehr wahrscheinlich aus dem Dungeon-/Hoehlengenerator. *Achtung Fallstrick:*
   die dort sichtbaren Zeilen wie `local_8._0_1_ = 0x15;` sind **SEH-Zustandsnummern**,
   keine Objekttypen — sie zaehlen linear hoch und wurden hier bewusst ignoriert.
9. **Ob Typ 0x0B (Truhendeckel) ein eigenstaendiges Objekt oder nur die Animationshaelfte
   von 0x0A ist.** Belegt ist nur: eigener Modelleintrag, nicht im Sonderobjekt-Set
   (`GameController.cpp:33928`), keine Erzeugungsstelle gefunden. *Vermutung:* der Deckel
   wird zusammen mit 0x0A gesetzt und ueber `+0x178` verlinkt — die Verlinkungslogik im
   Weltbehandler gilt aber ausdruecklich nur fuer Typ **9** (`GameController.cpp:97051`),
   nicht fuer 0x0A. **Widerspruch, ungeklaert.**
10. **Widerspruch bei der Bank-Skala.** `spawnRandomObject7` setzt (3.0, **1.0**, 0.4)
    (`game_misc.cpp:30820-30823`), die vier Erzeugungsstellen in `Chunk_generateObjects`
    setzen (3.0, **0.4**, 0.4) (`game_misc.cpp:29224`ff). Beide sind belegt; welche
    Variante wo greift, haengt vom Kontext ab. **Kein Fehler in der Analyse, sondern
    zwei verschiedene Vanilla-Werte.**
11. **`street-light01` ist Typ 0x33 und `street-light02` ist Typ 0x32** — also
    absteigend, waehrend `flower-box01/02/03` aufsteigend auf 0x2F/0x30/0x31 liegen.
    Das ist so im Code (`GameController.cpp:22729` vs. `22732`), sieht aber nach einem
    Vertauscher im Original aus. Belegt, aber kontraintuitiv — beim Modden nicht
    "korrigieren".
