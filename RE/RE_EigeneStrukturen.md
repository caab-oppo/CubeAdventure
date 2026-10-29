# RE: Eigene Strukturen wie native Modelle laden, setzen und dauerhaft machen

**BLUF:** Eigene Strukturen lassen sich exakt wie native behandeln, weil `WorldInfo_placeStructure`
vom Modellobjekt **nur vier Felder** liest: `+0x30` (RGB-Zeiger), `+0x44/+0x48/+0x4C` (Groesse X/Y/Z).
Ein vom Mod angelegtes Objekt mit diesen vier Feldern wird also bitgleich wie ein natives verarbeitet,
inklusive Markerfarben. Das native `.cub`-Format ist trivial (`int32 X, int32 Y, int32 Z`, danach
`X*Y*Z*3` Byte RGB, X laeuft am schnellsten, dann Y, dann Z = Hoehe); in `data1.db` liegt es nur
zusaetzlich verwuerfelt (Algorithmus bekannt). Das Spiel laedt aus `data1.db` **ausschliesslich 2557
fest einprogrammierte Dateinamen** in feste Registry-Indizes; eigene Keys in der Datenbank werden nie
gelesen. Empfohlen ist deshalb: **(1)** der Mod laedt `Strukturen\*.cub` selbst beim Start und haelt
eigene Modellobjekte (nicht in der Registry), **(2)** gesetzte Strukturen werden wie im Vanilla-Spiel
**bei jeder Zonengenerierung neu gestempelt** (Platzierungsliste je Welt + `OnZoneGenerated`, der
Hook laeuft nachweislich nach dem Laden der gespeicherten Blockaenderungen). Das ist dauerhaft, braucht
keinen Eingriff in den Spielstand und verhaelt sich wie native Gebaeude, die ebenfalls bei jedem Laden
aus dem Seed neu entstehen.

Zwei Nebenbefunde, die bestehenden Mod-Code betreffen:
* **Modellobjekte tragen keinen Namen.** Die Suche nach einem Namenszeiger im Objekt
  (`ModelNameOf`, `FindModelByNameAddr` in `NativeHouseProbe.cpp`) kann nie treffen. Die Zuordnung
  Name → Index steht nur im Code von `0x4EE230` und ist hier vollstaendig ausgelesen (Anhang A).
* **`loadStructureBlob @0x4809A0` laedt keine Strukturen**, sondern die Eintraege der Weltliste aus
  `Save/worlds.db` (Name, zwei Werte, Vorschau-Voxelmodell). Mit der Modell-Registry hat er nichts zu tun.

Alle Adressen statisch (ImageBase `0x400000`), Belege als `Datei:Zeile` relativ zu
`CubeWorld-Reversal-master-new/cube/`. Kennzeichnung: **BELEGT** = direkt im Pseudo-C ablesbar,
**VERMUTET** = Schluss mit Begruendung.

---

## 1. Wie werden die Vorlagen geladen?

### 1.1 Der richtige Lader ist `0x4EE230`, nicht `loadStructureBlob`

| Funktion | Was sie wirklich tut | Beleg |
|---|---|---|
| `GameController_load_game_assets @0x4EE230` (Ghidra-Name irrefuehrend, es ist eine Methode des **SpriteManager**) | Fuellt die Modell-Registry aus `data1.db` | **BELEGT** `control/GameController.cpp:60856-78781` |
| `cube::GameController::loadStructureBlob @0x4809A0` | Laedt Weltlisten-Eintraege aus `Save/worlds.db`: Key = Weltindex als Dezimalzahl (`operator<<(int)`), Inhalt = `u32 Namenslaenge`, Name → `WorldInfo+0x08`, `u32` → `+0x20`, `u32` → `+0x24`, `int32 X,Y,Z`, `u32 Laenge`, zlib-Daten → Vorschau-Sprite bei `WorldInfo+0x04` | **BELEGT** `control/GameController.cpp:31228-31407`, Aufrufer mit `"Save/worlds.db"` und `operator_new(0x28)` + `WorldInfo::ctor_1` in `23523-23545` |
| `saveStructureBlob @0x4878A0` | Gegenstueck (Weltliste speichern) | **BELEGT** `control/GameController.cpp:33482` ff. |

`0x4EE230` wird aus `GameController::ctor_0` aufgerufen (`control/GameController.cpp:19392`), also
einmal beim Programmstart. **BELEGT.**

### 1.2 Ablauf von `0x4EE230` (ECX = SpriteManager)

1. `vectorInt_resize(0xa09)` auf `self+4`: der Vektor bekommt **2569** Plaetze.
   **BELEGT** `GameController.cpp:63447`.
2. Fuer **jeden** Platz: `operator_new(0x60)`, `cube::Sprite::ctor_0(*(self+0x10))`, Zeiger in den Platz.
   **BELEGT** `GameController.cpp:63449-63462`. Alle 2569 Objekte existieren also schon, bevor ein
   einziges Modell geladen wird.
3. Pfadpraefix: Ist das uebergebene String-Argument nicht leer, wird `arg + DAT_00701904` vorangestellt
   (`63466-63471`). Danach `cube::Database` (8 Byte, `+4` = `sqlite3*`) anlegen und
   `db_openBlobStore(praefix + "data1.db")` (`63472-63486`). **BELEGT.**
4. `string_assign_cstr(&DAT_006fc918)` setzt das Praefix zurueck (`63487`). **VERMUTET:** `DAT_006fc918`
   ist der Leerstring, denn sonst stuende der Ordnerpfad im Datenbank-Key. Der Inhalt der Konstante
   steht nicht im Decompile.
5. **2557** Aufrufe nach immer gleichem Muster (`63488` bis `78780`):
   ```c
   uVar5 = string_ctorAppend(tmp, praefix, "building-smithy.cub");   // Key = Dateiname
   vector_at_stride4(0x903);                                         // &sprites[0x903]
   VoxelModel_load_from_file_or_buffer(uVar5, database, 1);          // ECX = sprites[0x903]
   string_dtor();                                                    // Name wird verworfen
   ```
   **BELEGT** z.B. `GameController.cpp:76677-76682` (Schmiede) und `76155` ff. (Tanne).
   Bei den ersten 108 Aufrufen fehlt das `vector_at_stride4` im Pseudo-C (Index steckt dort in einem
   Register), ab `GameController.cpp:64031` steht der Index jeweils woertlich davor.

**Wichtig:** Die **Aufrufreihenfolge ist nicht die Indexreihenfolge** (z.B. `landingplace.cub` →
`0x887`, direkt danach `building-warrior.cub` → `0x8FF`, `GameController.cpp`, Suche nach
`vector_at_stride4(0x8ff)`). Das erklaert, warum die fruehere Herleitung aus der `.rdata`-Reihenfolge
falsch war.

### 1.3 Gibt es eine Name→Index-Zuordnung im Speicher?

**Nein. BELEGT:**
* `Sprite::ctor_0` (`entity/Sprite.cpp:7-42`) setzt keinen String und keinen Namenszeiger.
* `VoxelModel_load_from_file_or_buffer` schreibt ins Objekt nur `+0x55` und (ueber `set_data`) die
  Felder `+0x04..+0x24`, `+0x30`, `+0x44..+0x4C` (`game_misc/game_misc.cpp:15687`,
  `control/GameController.cpp:57935-57954`).
* Der Namensstring ist ein Temporaerobjekt und wird nach jedem Aufruf zerstoert (`string_dtor()`).
* In der gesamten Codebasis gibt es keine Abfrage, die Keys aufzaehlt: nur
  `SELECT value FROM blobs WHERE key = ?` (`GameController.cpp:16556`),
  `SELECT 1 FROM blobs WHERE key = ?` (`16658`), `INSERT`/`UPDATE` (`16645` ff.) und
  `DELETE` (`_library/crt_stl.cpp:2898`).

Die einzige Zuordnung ist also der Code selbst. Sie ist in **Anhang A** fuer alle bau-relevanten
Eintraege ausgelesen. Abgleich mit `MODELL_INDEX.md` (im Spiel gesehen): `0x832` fir-tree = Baum,
`0x84C` antique-building1 = Tempel, `0x87E/0x87F/0x880` frame-house01/frame-inn/frame-shop = Haus,
`0x882` stone-well = Brunnen, `0x884-0x886` stilt-hut/-path = Staenderbau mit Steg, `0x887`
landingplace = Turm mit Andockstelle, `0x889` entrance-stairs = Treppe, `0x8FE`
white-castle-round-tower = Wachturm, `0x903` smithy = Schmiede, `0x9E2` street-light01 = Standfackeln,
`0x9F4` boat2 = Schiff. **Einzige Abweichung:** `0x8FF` heisst im Code `building-warrior.cub`, im
Spiel wurde es als "Durchgang" notiert. Die Zuordnung Index ↔ Datei ist im Code woertlich belegt; ob das
Kriegerhaus wie ein Durchgang aussieht, ist eine Frage des Aussehens, nicht der Zuordnung.
Damit ist auch die offene Frage aus `MODELL_INDEX.md` beantwortet: **Gasthaus = `0x87F`
(frame-inn), Laden = `0x880` (frame-shop)**, und `0x8D0..0x8EE` (der Bereich `kModelFirst..kModelLast`
in `NativeHouseProbe.cpp`) sind die Bausaetze `antiqueruin-*`, `jungleruin-*`, `desertruin-*`, also
Ruinen- und Dungeonteile (passt zur dortigen Notiz "lieferten Dungeonteile statt Hauswaenden").
`framework-wall-lamp.cub` liegt bei **`0x895`**.

### 1.4 Der Modell-Lader `VoxelModel_load_from_file_or_buffer @0x4E7290`

Signatur (aus dem Rumpf, `game_misc/game_misc.cpp:15656-15776`):
`void __thiscall Sprite::load(const std::string* nameOrPath, cube::Database* db, char strict)`

* **ECX** = Zielobjekt (Ghidra: `in_ECX`). **BELEGT.** Die Aufrufkonvention `__thiscall` mit
  Stapelbereinigung durch die Funktion ist **VERMUTET** (MSVC-Regel fuer Methoden; die Rueckkehr
  `ret 0Ch` ist ohne Disassembly nicht pruefbar).
* `*(char*)(ECX+0x55) = strict` (`15687`). **BELEGT.**
* **`db == 0` → Datei von der Platte:** `ifstream(name, binary)` (`15690`); nur wenn der Stream gut
  ist (`15691`), werden `int32`, `int32`, `int32` und dann die RGB-Daten gelesen und
  `VoxelModel_set_data(erstes, zweites, drittes, pixel, strict)` aufgerufen (`15692-15711`). Fehlt die
  Datei, bleibt das Modell leer, ohne Absturz. **BELEGT.** Das Spiel benutzt diesen Weg selbst, z.B.
  `"build-cursor.cub"` (`control/GameController.cpp:22809-22821`) und die `framework-*.cub`-Teile in
  `world/World.cpp:4339-4353` (jeweils `operator_new(0x60)` → `Sprite::ctor_0` → Laden mit `db = 0`).
* **`db != 0` → Datenbank:** `db_getBlobInto(name, &puffer)` (`15726`), dann
  **`blob_deobfuscate()`** (`15728`), danach dieselben drei `int32` und `X*Y*Z*3` Byte RGB
  (`15729-15762`), dann `VoxelModel_set_data` (`15763`). **BELEGT.**

### 1.5 Das `.cub`-Format

| Offset | Typ | Inhalt | Beleg |
|---|---|---|---|
| `0x00` | `int32` LE | Groesse X → Modell `+0x44` | **BELEGT** `set_data` `GameController.cpp:57935` |
| `0x04` | `int32` LE | Groesse Y → `+0x48` | **BELEGT** `57936` |
| `0x08` | `int32` LE | Groesse Z (Hoehe) → `+0x4C` | **BELEGT** `57937` |
| `0x0C` | `X*Y*Z*3` Byte | Farbe je Voxel, 3 Byte | **BELEGT** `57942-57948` (memcpy 1:1) |

* Kein Magic, keine Version, keine Palette, kein Namensfeld. **BELEGT** (nichts anderes wird gelesen).
* Voxelreihenfolge: Index `((Y*z + y)*X + x)*3`, also **x laeuft am schnellsten, dann y, dann z**.
  **BELEGT** `GameController_voxelAt3D @0x4A2870` (`GameController.cpp:42657-42671`) und
  `WorldInfo.cpp:5406-5409`.
* `z` ist die Hoehe: `placeStructure` setzt Weltposition `origin[2] + mz`. **BELEGT**
  `WorldInfo.cpp:5545/5567`.
* Byte-Reihenfolge der Farbe **R, G, B**: **VERMUTET.** Gestuetzt durch die Markertabelle in
  `RE_placeStructure.md` (Byte 0 = FF bei "rot") und das im Spiel gesehene Verhalten; formal laesst sich
  "rot" aus dem Pseudo-C nicht von "blau" unterscheiden. Pruefbar mit einem Export (Abschnitt 4.4).
* **Leeres Voxel** = Farbe gleich `DAT_0076b340`. Der Startwert steht nicht im Decompile; **VERMUTET
  (0,0,0)**, weil `VoxelModel_allocate` neue Modelle mit `memset(0)` anlegt
  (`GameController.cpp:57891`) und diese als leer gelten muessen. Achtung, siehe Risiko R5.
* **Datenbank-Variante:** Inhalt identisch, aber verwuerfelt. Entschluesselung laut der (im Baum
  als round-trip-geprueft vermerkten) Notiz bei `game_misc/game_misc.cpp:7072-7088`:
  ```
  T = 44 int32 @0x6FFA68: 4242,9551,840,84800,9242,9846,127,9,9483,394,123,4834,32444,24355,
      2433,17,34234,42342,4243,14,184934,1987,3094,1901,89409,4813,37,143,3490,19483,1343,432,
      84732,9184,9612,1233,3434,1839,2984,1993,2984,4895,816583,13
  Decode: for i = n-1 .. 0: swap(b[i], b[(T[i%44]+i) % n]);  danach alle Bytes ~b
  Encode: alle Bytes ~b;  danach for i = 0 .. n-1: swap(b[i], b[(T[i%44]+i) % n])
  ```
  Der Pseudo-C-Rumpf (`7090-7115`) bestaetigt die Schleife. Dass Encode die Umkehrung ist, habe ich
  mit Zufallsdaten nachgerechnet.
* **Datenbank-Key** = Dateiname, z.B. `building-smithy.cub`. **VERMUTET** in Verbindung mit
  Punkt 1.2.4 (leeres Praefix).

### 1.6 Das Modellobjekt (`cube::Sprite`, 0x60 Byte)

Groesse **0x60**: **BELEGT** an allen Erzeugungsstellen (`GameController.cpp:63452`, `22809`,
`World.cpp:4339`). Felder aus `Sprite::ctor_0` (`entity/Sprite.cpp:7-42`), Destruktor
(`Sprite.cpp:47-82`), `set_data` und `build_mesh`; deckungsgleich mit `cwsdk/cube/Sprite.h`.

| Offset | Inhalt | ctor setzt | Beleg / Status |
|---|---|---|---|
| `+0x00` | vtable `cube::Sprite::vftable` | ja | **BELEGT** `Sprite.cpp:21` |
| `+0x04/+0x10/+0x1C` | 3x `std::vector<int[3]>`: Ankerpunkte (reinrote, reingruene, reinblaue Voxel) | leer | **BELEGT** ctor `:22`, Ruecksetzen `GameController.cpp:57950-57954`; welcher Vektor welche Farbe: **VERMUTET** (ECX der push_back-Aufrufe fehlt) |
| `+0x28/+0x2C` | Baumkopf (rbtree) + Groesse | ja | **BELEGT** `Sprite.cpp:24-27`, Freigabe im dtor |
| **`+0x30`** | **`uint8_t* rgb`**, mit `new[]` angelegt, mit `delete[]` freigegeben | 0 | **BELEGT** `57938-57948`, dtor `Sprite.cpp:74-76` |
| `+0x34/+0x38` | D3D9 Vertex-/Indexbuffer | 0 | **BELEGT** `build_mesh` `GameController.cpp:59667-59670`, dtor (`Sprite.cpp:65-71`) ruft deren vfunc `+8` (Release) |
| `+0x3C/+0x40` | Vertex-/Dreieckszahl | (nicht) | **BELEGT** `59651-59652` |
| **`+0x44/+0x48/+0x4C`** | **Groesse X/Y/Z** | 0 | **BELEGT** |
| `+0x50` | unbekannt | 0 | **BELEGT** gesetzt, Bedeutung offen |
| `+0x54` | Byte, von `build_mesh` auf 0 gesetzt | 0 | **BELEGT** `59678` |
| `+0x55` | `strict`: 1 = nur `DAT_0076b340` ist durchsichtig; 0 = auch reines R/G/B ist durchsichtig und wird als Ankerpunkt gesammelt | **1** | **BELEGT** `match_transparent_color` `GameController.cpp:57821-57860`, `set_data` `57955` |
| `+0x56` | 1 = **kein Mesh bauen** | 0 | **BELEGT** `set_data` `58024` |
| `+0x58` | `IDirect3DDevice9*` (ctor-Argument) | ja | **BELEGT** ctor `Sprite.cpp:28`; `build_mesh` ruft vtable `+0x68/+0x6C` = `CreateVertexBuffer/CreateIndexBuffer` (`59654-59657`) |
| `+0x5C..+0x5E` | Bytes, in `build_mesh` als `/255.0` verwendet | 0 | **BELEGT** |

Die Registry selbst ist der **`SpriteManager` bei `World+0x1C` = `GC+0x300`**: `+0x04/+0x08/+0x0C`
= `std::vector<Sprite*>`, `+0x10` = `IDirect3DDevice9*`. **BELEGT** fuer die Offsets innerhalb des
Objekts (`load_game_assets` benutzt `self+4/+8` und reicht `self+0x10` an den ctor weiter;
`vectorU32_at` liest `self+4/+8`, `GameController.cpp:1362-1373`). Die Lage `GC+0x300` passt zu
`cwsdk/cube/World.h` (World bei `GC+0x2E4`, `SpriteManager` bei `World+0x1C`) und zum Scan im Mod.

---

## 2. Eigenes Modell zur Laufzeit

### 2.1 Welche Felder liest `placeStructure` wirklich?

**Nur `+0x30`, `+0x44`, `+0x48`, `+0x4C`. BELEGT:**
* Im ganzen Rumpf (`world/WorldInfo.cpp:5099-7143`) wird das Modell nur ueber `dungeon` bzw. die Kopie
  `local_6d8` angesprochen; Zugriffe: `+0x44` (13x), `+0x48` (12x), `+0x4C` (3x), `+0x30` (3x),
  sonst nichts.
* Weitergereicht wird es nur an `World_computeStructurePlacement @0x5CB3B0`, das es unveraendert an
  `World_resolveStructureFootprint @0x5EE0F0` gibt. Dort: `+0x30` (7x), `+0x44` (18x), `+0x48` (18x),
  `+0x4C` (8x), sonst nichts (`game_misc/game_misc.cpp:46751-46997`).
* Das im Fundamentzweig benutzte `GameController_voxelAt3D @0x4A2870` liest ebenfalls nur diese vier
  Felder (`GameController.cpp:42657-42671`).

**Folge:** Ein Objekt, das nur diese vier Felder sinnvoll gefuellt hat, wird von `placeStructure`
**exakt wie ein natives Modell** behandelt, einschliesslich aller Markerfarben-Zweige (die nur auf
die RGB-Werte schauen). Ein vtable, ein Mesh oder ein Registry-Eintrag sind dafuer nicht noetig.

### 2.2 Muss das Modell in die Registry?

Nein, nicht fuer `placeStructure`, denn die Funktion bekommt den Zeiger direkt (`Model* param_1`).
Die Registry braucht man nur, wenn **Vanilla-Code** das Modell ueber einen Index waehlen soll.

| Variante | Machbar | Risiko |
|---|---|---|
| **A. Schattenmodell** (eigener 0x60-Block, nur vier Felder, nicht in der Registry) | ja | **minimal**: das Spiel kennt das Objekt nicht, liest es nur waehrend des eigenen `placeStructure`-Aufrufs und gibt es nie frei |
| **B. `push_back` an die Registry** | ja | Der Vektor hat nach `vectorInt_resize(0xa09)` sehr wahrscheinlich genau 2569 Plaetze Kapazitaet, `push_back` verschiebt also das Zeigerfeld. Fremde Zeiger **auf Sprite-Objekte** bleiben gueltig (die Objekte wandern nicht). Ungueltig wuerden nur Zeiger **in das Feld hinein**, die jemand ueber Frames hinweg haelt; gefunden habe ich keine, ausgeschlossen ist es nicht. Ausserdem muss das Objekt dann ein echtes `Sprite` sein (vtable, Baumkopf), falls das Spiel es je ueber `vfunc_0` loescht. **VERMUTET mittel** |
| **C. Slot tauschen** (`sprites[0x903] = meinModell`) | ja | Kein Umkopieren, kein Groessenwechsel. Wirkt global: **jede** neu generierte Schmiede wird zu meinem Modell. Rueckgaengig durch Zuruecksetzen des alten Zeigers. Objekt muss echtes `Sprite` sein (siehe B). Die Mesh-Daten werden fuer Gebaeude nicht benutzt (Gebaeude sind Bloecke), schaden aber nicht |

### 2.3 Native Funktionen, um ein Modell aus Daten zu bauen

| Adresse | Signatur (aus dem Rumpf) | Wirkung | Status |
|---|---|---|---|
| `0x4E6A20` `Sprite::ctor_0` | `Sprite* __thiscall(Sprite* mem, IDirect3DDevice9* dev)` | initialisiert einen 0x60-Block, setzt vtable, `+0x55=1`, `+0x58=dev` | Rumpf **BELEGT**; Ghidra zeigt an den Aufrufstellen zwei Argumente (`ctor_0(x,0)`), der Rumpf benutzt eins. Argumentzahl daher **unsicher**, siehe 4.3 |
| `0x4E7650` `VoxelModel_set_data` | `void __thiscall(Sprite*, int x, int y, int z, const void* rgb, char strict)` | setzt Groessen, `delete[]` altes `+0x30`, neues `new[]` + memcpy, Ankervektoren leeren, Anker nur bei `strict==0`, Mesh nur bei `+0x56==0` | **BELEGT** `GameController.cpp:57905-58030` |
| `0x4E75C0` `VoxelModel_allocate` | `void __thiscall(Sprite*, int x, int y, int z)` | wie oben, aber Puffer mit 0 gefuellt | **BELEGT** `57872-57893` |
| `0x4E7290` `VoxelModel_load_from_file_or_buffer` | `void __thiscall(Sprite*, const std::string*, Database* oder 0, char strict)` | `.cub` aus Datei (`db==0`) oder aus Datenbank laden | **BELEGT**, braucht aber einen ABI-kompatiblen MSVC-2012-`std::string` (24 Byte: Puffer[16] bzw. Zeiger, `+0x10` Laenge, `+0x14` Kapazitaet) |
| `0x4A2870` `GameController_voxelAt3D` | `uint8_t* __thiscall(Model*, int x, int y, int z)` | Zeiger auf das Voxel oder auf `DAT_0076b340` bei Bereichsueberschreitung | **BELEGT** |

**Achtung `build_mesh`:** `set_data` baut das Mesh, solange `+0x56 == 0`, und ruft dabei
`IDirect3DDevice9::CreateVertexBuffer` ueber `+0x58`. Ausserhalb des Render-Threads oder mit
ungueltigem `+0x58` ist das ein Absturzkandidat. Fuer reine Baumodelle **`+0x56 = 1` setzen**.
**BELEGT** (Bedingung), Thread-Gefahr **VERMUTET** (D3D9 ist ohne Multithread-Flag nicht threadsicher).

### 2.4 Pflichtfelder

* **Fuer `placeStructure` (Variante A):** `+0x30` gueltiger Zeiger auf `X*Y*Z*3` Byte, `+0x44/+0x48/+0x4C`
  > 0. Empfohlen zusaetzlich: Rest mit 0 fuellen, `+0x55 = 1`, `+0x56 = 1` (falls doch einmal eine
  native Modellfunktion darauf losgelassen wird).
* **Fuer die Registry (B/C):** zusaetzlich ein echtes Objekt aus `Spiel-new(0x60)` + `Sprite::ctor_0`
  mit Device aus einem nativen Modell (`*(void**)(sprites[0x903]+0x58)`) oder aus
  `SpriteManager+0x10`, Daten ueber `VoxelModel_set_data` (damit `+0x30` mit dem Spiel-Allocator
  angelegt ist, der Destruktor gibt es mit `delete[]` frei).

---

## 3. Wie wird eine eigene Struktur dauerhaft?

### 3.1 (a) Eigenes `.cub` in `data1.db` schreiben

* **Neuer Key:** wirkungslos. Das Spiel laedt nur die 2557 fest kodierten Namen, eine Key-Aufzaehlung
  gibt es nicht. **BELEGT** (1.3).
* **Vorhandenen Key ueberschreiben** (z.B. `building-smithy.cub`, verwuerfelt mit Encode aus 1.5):
  funktioniert, das Spiel laedt dann das eigene Modell in `0x903`, und **jede** Schmiede in jeder Welt
  wird zum eigenen Modell, inklusive Markerumbau mit Typ `0x0D`. **VERMUTET** (folgt aus 1.2/1.4, nicht
  im Spiel getestet).
* **Bewertung:** Spielstaende werden nicht beruehrt (Gebaeude werden bei jedem Laden aus dem Seed
  generiert), aber die Spielinstallation wird veraendert, Austausch ist global und nicht pro Welt.
  Groessere Modelle als der Bauplatz koennen in Nachbargebaeude ragen. **Machbarkeit hoch, Nutzen
  gering, Risiko fuer Spielstaende gering, Risiko fuer die Installation mittel (Backup von `data1.db`).**
  Der gleiche Effekt ohne Dateiaenderung: Variante C (Slot tauschen) zur Laufzeit.

### 3.2 (b) Der Mod laedt `Strukturen\*.cub` beim Start

Die Vorlagen sind dann so dauerhaft wie die Dateien im Ordner. Das Format ist das native Rohformat
(1.5), deshalb sind exportierte native Modelle direkt wieder ladbar. **Machbarkeit hoch, Risiko keins**
(Variante A beruehrt keine Spieldaten). Diese Stufe ist Voraussetzung fuer (c) und (d).

### 3.3 (c) Gesetzte Bloecke im Spielstand speichern

Die offenen Punkte aus `RE_Notizen_Persistenz.md` lassen sich jetzt beantworten:

**Serializer und Ausloeser. BELEGT.**
* Speichern einer Zone: `0x5A5050` (Ghidra-Name `basic_stringbuf::ctor_13`, `_library/crt_stl.cpp:11293-11378`),
  `__thiscall`, ECX = World, Argument = Zone. Gespeichert wird nur, wenn
  `world+0xA4 != 0 && zone != 0 && (zone+0x75 != 0 || zone+0x68 != zone+0x6C)` (`11326-11328`).
  **Ein nicht-leerer 0x14-Vektor reicht also allein als "geaendert"-Signal**, `+0x75` ist dafuer nicht
  noetig.
* Ausloeser 1: Entladen einer Zone durch den Welt-Thread (`WorldGrid_destroyBlockObjectAt @0x5A4890`,
  `world/WorldInfo.cpp:842-876`, aufgerufen aus der Entladeschleife `WorldInfo.cpp:373`), nur wenn
  `world+0xB4 == 0`.
* Ausloeser 2: Schliessen/Wechseln der Welt, `0x5A52E0` speichert alle geladenen Zonen
  (`_library/crt_stl.cpp:11555-11570`).

**Region-Key. BELEGT bis auf zwei Konstanten:** `DAT_0071c764 << zoneX << DAT_0071c760 << zoneY` mit
`zoneX = zone+0x60`, `zoneY = zone+0x64` (`crt_stl.cpp:11349-11358`, Laden spiegelbildlich
`WorldInfo.cpp:5003-5010`). Die beiden Zeichenketten stehen nicht im Decompile. Fuer den nativen Weg
braucht man sie nicht.

**Blob-Format** (`serialize_struct_creatures @0x44E340`, `_library/crt_stl.cpp:3225-3350`; Laden
`deserializeRegionChunk @0x44CDB0`, `game_misc/game_misc.cpp:7389-7700`). **BELEGT:**

| Teil | Inhalt | Quelle in der Zone |
|---|---|---|
| `u32` | Version (`writer[1]`) | |
| `u32 n` + n x (`0x118` + `0x18` + `u32,u32,u8,u32,u32,i32`) | aufhebbare Gegenstaende (0x148-Records); `n = 0`, wenn `zone+0x75 == 0` | `+0x30..+0x34` |
| `u32 n` + n x `0x14` | **Blockaenderungen** | `+0x68..+0x70` |
| `u32 n` + n x (`u32,u32`) | je Objekt `obj+0x38`, `obj+0x3C` | Zeigervektor `+0x18..+0x1C` |
| `u32 n` + n x `u8` | **ein Zustandsbyte** `obj+0x30` je 0x188-Objekt | `+0x0C..+0x10` |

**0x14-Record. BELEGT** (`vector_insertSelfaware_0x14 @0x44D4B0`, `_library/audit.cpp:5605-5650`,
und Anwendung `game_misc.cpp:7575-7592`):
```
+0x00 int32 x   +0x04 int32 y   +0x08 int32 z     (absolute Blockkoordinaten)
+0x0C uint8 r, g, b, typ/flags                     (wie BlockColor; Bauwerk = typ|0x40)
+0x10 int32 tag                                    (Spieltag des Eintrags)
```
Beim Laden wird jeder Record mit `tilemap_writeGlyphColumn(x, y, z, &rgba, zone)` gesetzt und wieder in
`zone+0x68` eingetragen, **aber nur wenn `tag >= GC+0x800160 - 3` oder `tag < 0`**
(`game_misc.cpp:7588`). `GC+0x800160` ist der **Spieltag-Zaehler** (erhoeht, sobald die Tageszeit
`GC+0x80015C` 86.400.000 ms ueberschreitet, `GameController.cpp:95318-95322`, gespeichert/geladen
`95333`/`crt_stl.cpp:11643`). Das Spiel selbst stempelt Grab-/Zerstoerungsaenderungen mit dem aktuellen
Tag (`GameController.cpp:96244-96248`), sie **verfallen also nach drei Spieltagen** (Gelaende heilt).
**`tag = -1` ist dauerhaft.** Der Mod benutzt in `MountainVillage.cpp` bereits `-1`, das ist richtig.

**Was dabei nicht gespeichert wird. BELEGT:**
* Interaktive Objekte aus Markern (Tueren, Moebel, NPCs) werden **nicht als Objekte** gespeichert, nur
  ein Byte je 0x188-Objekt, und das nur, wenn die Anzahl nach der Generierung exakt gleich der
  gespeicherten ist (`game_misc.cpp:7685`). Gleiches gilt fuer den Zeigervektor `+0x18` (`7651`).
  Eigene Markerobjekte muessen also bei jedem Laden neu entstehen, Weg (c) allein reicht dafuer nicht.

**Bewertung (c):** Machbar mit nativer Funktion `vector_insertSelfaware_0x14` (ECX = `zone+0x68`), dann
waechst der Vektor mit dem Spiel-Allocator wie im Original. **Risiko fuer den Spielstand: gering bis
mittel.** Das Format ist das native, das Spiel liest es selbst wieder ein; Gefahr droht nur durch
Heap-Fehler bei Handarbeit am Vektor und durch Datenmenge (ein Haus 33x33x20 sind bis zu ~20.000
Records = ~400 KB im Zonenblob). Nachteile: Struktur bleibt auch ohne Mod im Spielstand, Entfernen
erfordert gezieltes Loeschen der Records, Markerobjekte gehen trotzdem verloren.

Hinweis zum vorhandenen `AppendZoneRecord14` (`MountainVillage.cpp:894-911`): Es legt bei jedem Anhaengen
einen neuen Puffer exakt passender Groesse an und gibt den alten nicht frei. Fuer einen Marker ist das
egal, fuer tausende Records waere es quadratisch langsam und ein Speicherleck. Fuer Massenrecords
`0x44D4B0` benutzen.

### 3.4 (d) Neu stempeln bei jeder Zonengenerierung (wie Vanilla)

So macht es das Spiel selbst: Gebaeude werden nicht gespeichert, sondern bei jedem Laden der Zone aus
dem Seed neu gesetzt (`Chunk_generateObjects` innerhalb von `WorldInfo_generateBiomeContent`).

**Zeitpunkt. BELEGT:** Der Mod-Hook `OnZoneGenerated` sitzt an der Aufrufstelle `0x46ACD1`
(`AdventureModFramework.cpp:33`, Bytes `E8 7A 9B 17 00`). Ziel = `0x46ACD1 + 5 + 0x179B7A` =
**`0x5E4850` = `WorldInfo_generateBiomeContent`**. In dieser Funktion werden **zuerst** die Vanilla-
Objekte erzeugt (`Chunk_generateObjects`, `WorldInfo.cpp:3267`) und **danach** der gespeicherte
Zonenblob angewandt (`deserializeRegionChunk`, `WorldInfo.cpp:5019`). Der Hook laeuft also
**nach** beidem und im Welt-Thread, demselben Thread, in dem das Spiel selbst `placeStructure` ruft.

**Bewertung (d):** Machbarkeit hoch, **Risiko fuer den Spielstand keins** (kein Byte des Spielstands
wird geschrieben), Strukturen und Markerobjekte entstehen bei jedem Laden neu, Entfernen = Zeile aus der
Platzierungsliste loeschen. Grabschaeden "heilen" wie bei Vanilla-Gebaeuden.

**Bekannte Nebenwirkung (Mechanismus BELEGT, Auswirkung VERMUTET):** Fuegt `placeStructure` mit Typ
`!= 0` Markerobjekte hinzu, ist beim naechsten Speichern die Objektzahl der Zone `N + M`, nach der
Generierung beim Laden aber `N`. Der Vergleich in `game_misc.cpp:7685` schlaegt fehl, und die
Zustandsbytes **aller** 0x188-Objekte dieser Zone (auch der Vanilla-Objekte) werden nicht
wiederhergestellt. Was `obj+0x30` genau bedeutet, ist offen (`Struct_InitDefaults` setzt 1). Abhilfe
waere, die eigenen Objekte **vor** `deserializeRegionChunk` zu erzeugen, also an der Aufrufstelle von
`Chunk_generateObjects` in `0x5E4850` (`WorldInfo.cpp:3267`, Byteadresse nicht im Decompile). Mit Typ 0
(keine Marker) tritt das Problem nicht auf.

**Nicht kombinieren:** (d) mit Recorder/Records aus (c) bei jedem Laden wuerde die Records jedes Mal
erneut anhaengen und den Blob unbegrenzt wachsen lassen.

### 3.5 Gesamtbewertung

| Weg | Dauerhaft | Markerobjekte nach Neustart | Spielstand-Risiko | Aufwand |
|---|---|---|---|---|
| (a) Key in `data1.db` ueberschreiben | ja, global | ja (nativ) | keins | mittel, Installation aendern |
| (b) `.cub` aus Ordner laden | Vorlagen ja | (Basis fuer c/d) | keins | gering |
| (c) 0x14-Records, `tag = -1` | ja, im Spielstand | **nein** | gering bis mittel | mittel |
| **(d) Platzierungsliste + Neustempeln** | **ja, durch den Mod** | **ja** | **keins** | gering |

---

## 4. Empfehlung

**(b) + (d)**, mit Variante A (Schattenmodell). Optional spaeter Variante C (Slot tauschen), wenn ein
eigenes Modell ein Vanilla-Gebaeude in allen Doerfern ersetzen soll.

### 4.1 Dateien

* `Strukturen\<name>.cub`: natives Rohformat (1.5), unverwuerfelt. Genau das, was das Spiel bei
  `db == 0` selbst liest.
* `Strukturen\<name>.txt` (optional, Beiwerte, nicht im `.cub`, damit das Format nativ bleibt):
  `typ=0x0D` (Kultur-/Gebaeudetyp fuer Marker, 0 = Marker bleiben Bloecke), `fundament=1`,
  `drehung=0`.
* `Strukturen\platzierungen_<seed>.txt`: eine Zeile je gesetzter Struktur:
  `name x y z drehung typ fundament` (x/y/z = **untere Ecke**, Blockkoordinaten). Seed wie in
  `MountainVillage.cpp:799` (`gc + 0x2E4 + 0x800188`).

Editor-Hinweis: reines Rot `(FF,00,00)`, Gruen `(00,FF,00)`, Blau `(00,00,FF)` sowie die uebrigen
Farben aus `MARKER_TABELLE.md` sind Markerfarben, sobald `typ != 0`. Leer = `(0,0,0)` (VERMUTET, R5).

### 4.2 Adressen und Konventionen

| Adresse | Funktion | Konvention | Beleg |
|---|---|---|---|
| `0x5F0CE0` | `WorldInfo_placeStructure` | `__stdcall`, 9 Args | im Mod per Aufrufstellen-Scan bestaetigt (`NativeHouseProbe.cpp`) |
| `0x4120C0` | `vectorU32_at` | `__thiscall`, ECX = SpriteManager (`GC+0x300`) | im Mod in Benutzung |
| `0x44D4B0` | `vector_insertSelfaware_0x14` (push_back) | `__thiscall`, ECX = `zone+0x68`, 1 Arg | Rumpf **BELEGT**, Konvention **VERMUTET** (Methode) |
| `0x4E6A20` | `Sprite::ctor_0` | `__thiscall`, ECX = 0x60-Block, 1 Arg (Device) | **VERMUTET** (siehe 2.3) |
| `0x4E7650` | `VoxelModel_set_data` | `__thiscall`, 5 Args | Rumpf **BELEGT**, Konvention **VERMUTET** |
| `0x4A2870` | `GameController_voxelAt3D` | `__thiscall`, 3 Args | Rumpf **BELEGT** |
| `0x68D668` / `0x68D66E` | Import-Thunks `operator new` / `operator delete` (MSVCR110) | `__cdecl` | **BELEGT** `_library/crt_stl.cpp:21122-21143`, `new` im Mod in Benutzung |
| `0x76B340` | `DAT_0076b340`, Leerfarbe (3 Byte) | Daten | **BELEGT** Verwendung |
| `0x6FFA68` | Verwuerfelungstabelle (44 x int32) | Daten | **BELEGT** Notiz `game_misc.cpp:7079` |

Vor dem ersten Einsatz der als VERMUTET markierten `__thiscall`-Funktionen: `ScanCallSites` aus
`NativeHouseProbe.cpp` auf `0x44D4B0`, `0x4E6A20`, `0x4E7650` laufen lassen. Erwartung: keine
`add esp` hinter den Aufrufen (Funktion raeumt selbst ab).

### 4.3 C++-Skizze (Stil wie im Mod, `__try` nur in POD-Funktionen)

```cpp
// CustomStructures.cpp -- Skizze, nicht getestet
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <vector>

namespace CustomStructures {

	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint32_t Re(uint32_t a) { return Base() + (a - 0x400000); }

	static const uint32_t kFnPlaceStructure = 0x005F0CE0; // __stdcall, 9 Args
	static const uint32_t kFnVec14PushBack  = 0x0044D4B0; // __thiscall, ECX = zone+0x68
	static const uint32_t kDatEmptyColor    = 0x0076B340; // 3 Byte Leerfarbe

	// Layout = cube::Sprite (0x60). placeStructure liest NUR rgb, sx, sy, sz.
#pragma pack(push, 1)
	struct Model {
		uint8_t  head[0x30];      // +0x00 vtable, Ankervektoren, Baum (Schattenmodell: 0)
		uint8_t* rgb;             // +0x30 PFLICHT
		uint8_t  mesh[0x10];      // +0x34..+0x43 VB, IB, Zaehler (0)
		int32_t  sx, sy, sz;      // +0x44, +0x48, +0x4C PFLICHT
		uint8_t  tail[0x10];      // +0x50..+0x5F; tail[5]=+0x55 strict, tail[6]=+0x56 kein Mesh
	};
	struct Rec14 { int32_t x, y, z; uint8_t r, g, b, t; int32_t tag; };
#pragma pack(pop)
	static_assert(sizeof(Model) == 0x60, "Model muss 0x60 Byte haben");
	static_assert(sizeof(Rec14) == 0x14, "Rec14 muss 0x14 Byte haben");

	// ---- .cub lesen/schreiben (natives Rohformat) ---------------------------
	static Model* LoadCub(const char* path) {
		FILE* f = nullptr;
		if (fopen_s(&f, path, "rb") != 0 || !f) return nullptr;
		int32_t d[3] = { 0, 0, 0 };
		Model* m = nullptr;
		if (fread(d, 4, 3, f) == 3 && d[0] > 0 && d[1] > 0 && d[2] > 0
			&& d[0] <= 256 && d[1] <= 256 && d[2] <= 512) {
			size_t n = (size_t)d[0] * (size_t)d[1] * (size_t)d[2] * 3;
			uint8_t* buf = (uint8_t*)malloc(n);
			if (buf && fread(buf, 1, n, f) == n) {
				m = (Model*)calloc(1, sizeof(Model));
				if (m) { m->rgb = buf; m->sx = d[0]; m->sy = d[1]; m->sz = d[2];
				         m->tail[5] = 1; m->tail[6] = 1; }
				else free(buf);
			} else free(buf);
		}
		fclose(f);
		return m;
	}

	static bool SaveCub(const char* path, int sx, int sy, int sz, const uint8_t* rgb) {
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") != 0 || !f) return false;
		int32_t d[3] = { sx, sy, sz };
		size_t n = (size_t)sx * sy * sz * 3;
		bool ok = fwrite(d, 4, 3, f) == 3 && fwrite(rgb, 1, n, f) == n;
		fclose(f);
		return ok;
	}

	// ---- Natives Modell aus der Registry kopieren (fuer den Export) ---------
	// Reine SEH-Funktion: kein C++-Objekt im Rumpf (C2712).
	static uint8_t* CopyNativeModel(void* tmpl, int& sx, int& sy, int& sz) {
		__try {
			sx = *(int*)((uint8_t*)tmpl + 0x44);
			sy = *(int*)((uint8_t*)tmpl + 0x48);
			sz = *(int*)((uint8_t*)tmpl + 0x4C);
			const uint8_t* src = *(uint8_t**)((uint8_t*)tmpl + 0x30);
			if (sx <= 0 || sy <= 0 || sz <= 0 || !src) return nullptr;
			size_t n = (size_t)sx * sy * sz * 3;
			uint8_t* out = (uint8_t*)malloc(n);
			if (out) memcpy(out, src, n);
			return out;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	// Aufruf z.B.: tmpl = RegistryAt(reg, 0x903); CopyNativeModel(...); SaveCub("Strukturen\\Export\\building-smithy.cub", ...)

	// ---- Leerfarbe pruefen (Risiko R5) ---------------------------------------
	static bool EmptyColorIsBlack() {
		__try {
			const uint8_t* p = (const uint8_t*)Re(kDatEmptyColor);
			return p[0] == 0 && p[1] == 0 && p[2] == 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- placeStructure -------------------------------------------------------
	typedef void(__stdcall* Fn_PlaceStructure)(void* model, int* origin, unsigned rot,
		unsigned char blockFlags, int typ, void* zone, char fundament, int* crop, void* recorder);

	static bool CallPlace(void* model, int* origin, unsigned rot, int typ, void* zone, char fundament) {
		int crop[4] = { 0, 0, 0, 0 };                    // param_8 MUSS lesbar sein
		Fn_PlaceStructure f = (Fn_PlaceStructure)Re(kFnPlaceStructure);
		__try { f(model, origin, rot & 3, 6, typ, zone, fundament, crop, nullptr); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- Nur fuer Weg (c): Record nativ anhaengen ----------------------------
	typedef void(__fastcall* Fn_Vec14Push)(void* vec, void* edx, const Rec14* r);
	static bool PushRecord(void* zone, const Rec14* r) {
		Fn_Vec14Push f = (Fn_Vec14Push)Re(kFnVec14PushBack);
		__try { f((uint8_t*)zone + 0x68, nullptr, r); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- Verwaltung ------------------------------------------------------------
	struct Placement { std::string name; int x, y, z, rot, typ, fundament; };
	static std::map<std::string, Model*> g_models;      // Name -> Schattenmodell
	static std::vector<Placement> g_placements;         // aus platzierungen_<seed>.txt

	static int FloorDiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

	void LoadAllTemplates() {                           // einmal beim Start
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA("Strukturen\\*.cub", &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			std::string path = std::string("Strukturen\\") + fd.cFileName;
			Model* m = LoadCub(path.c_str());
			if (m) g_models[fd.cFileName] = m;          // Key inkl. ".cub", wie nativ
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}

	// Aus IAdventureMod::OnZoneGenerated (Welt-Thread, nach Vanilla-Generierung
	// und nach dem Anwenden des gespeicherten Zonenblobs).
	void OnZoneGenerated(int zx, int zy, void* zone) {
		if (!zone || !EmptyColorIsBlack()) return;
		for (size_t i = 0; i < g_placements.size(); i++) {
			const Placement& p = g_placements[i];
			std::map<std::string, Model*>::iterator it = g_models.find(p.name);
			if (it == g_models.end()) continue;
			Model* m = it->second;
			int w = (p.rot & 1) ? m->sy : m->sx;            // ungerade Drehung tauscht X/Y
			int d = (p.rot & 1) ? m->sx : m->sy;
			int zx0 = FloorDiv(p.x, 256), zx1 = FloorDiv(p.x + w - 1, 256);
			int zy0 = FloorDiv(p.y, 256), zy1 = FloorDiv(p.y + d - 1, 256);
			if (zx < zx0 || zx > zx1 || zy < zy0 || zy > zy1) continue;
			// Mit Markern (typ != 0) nur Strukturen, die ganz in EINER Zone liegen (siehe 4.5).
			if (p.typ != 0 && (zx0 != zx1 || zy0 != zy1)) continue;
			int origin[3] = { p.x, p.y, p.z };            // untere Ecke, nicht Mitte
			CallPlace(m, origin, (unsigned)p.rot, p.typ, zone, (char)p.fundament);
		}
	}
}
```

Platzieren durch den Spieler (Hotkey): Zeile an `g_placements` anhaengen, Datei schreiben, danach
sofort `CallPlace` mit der aktuellen Zone (`World::GetZone(FloorDiv(x,256), FloorDiv(y,256))`) und
Remesh wie in `MountainVillage.cpp` (`needs_update`). Beim naechsten Laden uebernimmt `OnZoneGenerated`.

### 4.4 Native Modelle exportieren

* **Im Spiel (einfachster Weg):** `RegistryAt(reg, index)` → `CopyNativeModel` → `SaveCub`. Dateiname
  aus Anhang A. Das Ergebnis ist byte-identisch mit dem entschluesselten Datenbankinhalt.
* **Ohne Spiel:** `data1.db` mit SQLite oeffnen, `SELECT value FROM blobs WHERE key='building-smithy.cub'`,
  Decode aus 1.5 anwenden, Ergebnis als `.cub` speichern.
  ```python
  T=[4242,9551,840,84800,9242,9846,127,9,9483,394,123,4834,32444,24355,2433,17,34234,42342,
     4243,14,184934,1987,3094,1901,89409,4813,37,143,3490,19483,1343,432,84732,9184,9612,1233,
     3434,1839,2984,1993,2984,4895,816583,13]
  def decode(b):
      b=bytearray(b); n=len(b)
      for i in range(n-1,-1,-1):
          j=(T[i%44]+i)%n; b[i],b[j]=b[j],b[i]
      return bytes(x^0xFF for x in b)
  def encode(b):                       # nur fuer Weg (a)
      b=bytearray(x^0xFF for x in b); n=len(b)
      for i in range(n):
          j=(T[i%44]+i)%n; b[i],b[j]=b[j],b[i]
      return bytes(b)
  ```
  Erster Test: Groessen der entschluesselten Datei muessen zu `MODELL_INDEX.md` passen (Schmiede 32x31).

### 4.5 Was beim Setzen zu beachten ist

* `typ` (param_5) und Markerfarben gehoeren zusammen: Schmiede-Marker mit `0x0D`, Zimmerei `0x0E`,
  Weberei `0x0F`, Klassengebaeude `9..0x0C`, Dorfhaeuser/Turm `7` (Tabelle in `RE_placeStructure.md`).
  Ein eigenes Modell verhaelt sich dabei exakt wie das native, weil nur die Farben zaehlen (2.1).
* `param_6` ist die Zone; mit `typ != 0` darf sie nicht 0 sein (`RE_placeStructure.md` Abschnitt 4).
* **Strukturen mit Markern ganz in eine 256x256-Zone legen.** Bloecke ausserhalb der uebergebenen Zone
  werden verworfen; nicht-leere Voxel ausserhalb werden aber trotzdem bis zur Markerpruefung gefuehrt
  (`world_getChunkCell256` liefert 0, dann wird ein Ersatzblock geprueft, `WorldInfo.cpp:5585-5642`),
  sodass ein Marker ausserhalb wohl ein Objekt in der falschen Zone erzeugt. **VERMUTET.** Vanilla
  setzt Gebaeude immer innerhalb der eigenen Zone (`game_misc.cpp:28770-28773`).
* Wiederholtes Stempeln ist unkritisch: Steht an der Zielstelle schon ein fester Block mit Flag `0x40`,
  wird das Voxel uebersprungen (`WorldInfo.cpp:5586-5636`); zu Luft gewordene Marker tragen Typ 0 und
  blockieren nichts.

---

## 5. Risiken

* **R1 Registry-Reallokation (nur Variante B).** Siehe 2.2. Vermeiden durch Variante A oder C.
* **R2 Mesh-Bau im falschen Thread.** `set_data`/`load` bauen D3D-Puffer, solange `+0x56 == 0`.
  `OnZoneGenerated` laeuft im Welt-Thread. Fuer Baumodelle `+0x56 = 1`.
* **R3 Objektzahl-Abgleich der Zone** (3.4): Zustandsbytes der Zone werden bei eigenen Markerobjekten
  nicht wiederhergestellt.
* **R4 Records ohne Ende** (3.3/3.4): Records nur einmalig anhaengen, nie bei jedem Laden.
* **R5 Leerfarbe kann sich aendern.** `cube::GameController::loadLandscapeTile @0x6024D0` enthaelt eine
  eingebettete Form von `voxelAt3D`, die bei Bereichsueberschreitung **in `DAT_0076b340` schreibt**
  (Werte `C8C8C8`, `DCDCDC`, `FFFFFF`; `GameController.cpp:92548-92603`). Tritt das je ein, gelten in
  allen Modellen andere Farben als leer, und `placeStructure` wuerde schwarze Luftvoxel als Bloecke
  setzen. Ob der Fall im Spiel vorkommt, ist offen. Deshalb vor jedem Setzen `EmptyColorIsBlack()`
  pruefen und bei Abweichung abbrechen und melden.
* **R6 Datenmenge (nur Weg c):** ein Record je Voxel.

---

## Ungeprueft

1. Startwert von `DAT_0076b340` (vermutlich `00 00 00`) und Inhalt von `DAT_006fc918` (vermutlich `""`).
2. Die Farbreihenfolge R,G,B im `.cub` (spricht alles dafuer, pruefbar mit einem Export).
3. Die Stapelbereinigung (`ret N`) von `0x4E6A20`, `0x4E7650`, `0x44D4B0`; beim Sprite-ctor zusaetzlich
   die Argumentzahl (Ghidra zeigt am Aufruf zwei, der Rumpf benutzt eins). Erst mit `ScanCallSites`
   pruefen.
4. Ob irgendein Spielteil ueber die gesamte Registry iteriert (z.B. bei D3D-Reset) oder Zeiger in das
   Zeigerfeld haelt; relevant nur fuer Variante B. Im `GameController`-Destruktor
   (`GameController.cpp:24254-24394`) wird der SpriteManager nicht sichtbar geleert.
5. Die Zeichenketten `DAT_0071c764`/`DAT_0071c760` des Region-Keys.
6. Bedeutung von `world+0xA4` (Speichern erlaubt) und `world+0xB4` (Speichern unterdrueckt); vermutlich
   Datenbank vorhanden bzw. Mehrspieler-Client.
7. Bedeutung des Zustandsbytes `obj+0x30` der 0x188-Objekte und der Werte `obj+0x38/+0x3C` im
   Zeigervektor `zone+0x18`.
8. Ob `placeStructure` Markerobjekte fuer Voxel ausserhalb der uebergebenen Zone tatsaechlich anlegt
   (4.5) und ob nach dem Stempeln in `OnZoneGenerated` ein Remesh noetig ist (die Sonnenlicht-
   Berechnung `VoxelChunk_propagateSunlight` laeuft in `0x5E4850` vor dem Hook, `WorldInfo.cpp:5026`).
9. Die Aufrufadresse von `Chunk_generateObjects` in `0x5E4850` fuer die "vor dem Deserialisieren"-
   Variante (R3).
10. Weg (a) wurde nicht im Spiel getestet.

---

## Anhang A: Registry-Index → Datei (bau-relevante Auswahl)

Quelle: `GameController_load_game_assets @0x4EE230`, jeweils `vector_at_stride4(INDEX)` direkt vor
`VoxelModel_load_from_file_or_buffer` mit dem Dateinamen aus dem vorangehenden `string_ctorAppend`
(`control/GameController.cpp:63488-78780`). **BELEGT.** 2442 Indizes sind so direkt ablesbar, 108 fruehe
Aufrufe (Kreaturteile) ohne sichtbaren Index. Vollstaendige Liste neu erzeugen:
`awk 'NR>=60856 && NR<=78781' control/GameController.cpp` und je Block Name + `vector_at_stride4`-Argument
paaren.

| Index | Datei | Index | Datei | Index | Datei |
|---|---|---|---|---|---|
| `0x81C` | door.cub | `0x8AD` | whitewood-wall-indoor.cub | `0x8E9` | desertruin-wall-door.cub |
| `0x81D` | window.cub | `0x8AE` | whitewood-wall-balcony.cub | `0x8EA` | desertruin-wall-indoor.cub |
| `0x832` | fir-tree.cub | `0x8AF` | whitewood-wall-lamp.cub | `0x8EB` | desertruin-roof1.cub |
| `0x833` | thorn-tree.cub | `0x8B0` | whitewood-roof1.cub | `0x8EC` | desertruin-roof2.cub |
| `0x84C` | antique-building1.cub | `0x8B1` | whitewood-roof2.cub | `0x8ED` | desertruin-roof3.cub |
| `0x84D` | antique-building2.cub | `0x8B2` | whitewood-roof3.cub | `0x8EE` | desertruin-arc.cub |
| `0x84E` | antique-building3.cub | `0x8B3` | whitewood-arc.cub | `0x8EF` | wood-base.cub |
| `0x84F` | antique-building4.cub | `0x8B4` | clay-base.cub | `0x8F0` | wood-floor.cub |
| `0x850` | entrance-crypt.cub | `0x8B5` | clay-floor.cub | `0x8F1` | wood-floor-stairs.cub |
| `0x851` | entrance-barrow.cub | `0x8B6` | clay-floor-stairs.cub | `0x8F2` | wood-wall.cub |
| `0x852` | entrance-mine.cub | `0x8B7` | clay-wall.cub | `0x8F3` | wood-wall-window.cub |
| `0x853` | entrance-antique.cub | `0x8B8` | clay-wall-window.cub | `0x8F4` | wood-wall-door.cub |
| `0x854` | entrance-tomb.cub | `0x8B9` | clay-wall-door.cub | `0x8F5` | wood-wall-indoor.cub |
| `0x87E` | frame-house01.cub | `0x8BA` | clay-wall-indoor.cub | `0x8F6` | wood-wall-balcony.cub |
| `0x87F` | frame-inn.cub | `0x8BB` | clay-wall-balcony.cub | `0x8F7` | wood-wall-lamp.cub |
| `0x880` | frame-shop.cub | `0x8BC` | clay-wall-lamp.cub | `0x8F8` | wood-roof1.cub |
| `0x881` | frame-tower.cub | `0x8BD` | clay-roof1.cub | `0x8F9` | wood-roof2.cub |
| `0x882` | building-stone-well.cub | `0x8BE` | clay-roof2.cub | `0x8FA` | wood-roof3.cub |
| `0x883` | building-stilt-inn1.cub | `0x8BF` | clay-roof3.cub | `0x8FB` | wood-arc.cub |
| `0x884` | building-stilt-hut3.cub | `0x8C0` | clay-arc.cub | `0x8FE` | white-castle-round-tower.cub |
| `0x885` | building-stilt-hut2.cub | `0x8C1` | clay-entrance-stairs.cub | `0x8FF` | building-warrior.cub |
| `0x886` | building-stilt-path.cub | `0x8C2` | whiteclay-entrance-stairs.cub | `0x900` | building-ranger.cub |
| `0x887` | landingplace.cub | `0x8C3` | whiteclay-base.cub | `0x901` | building-rogue.cub |
| `0x888` | stone-bridge.cub | `0x8C4` | whiteclay-floor.cub | `0x902` | building-mage.cub |
| `0x889` | entrance-stairs.cub | `0x8C5` | whiteclay-floor-stairs.cub | `0x903` | building-smithy.cub |
| `0x88A` | carpet1.cub | `0x8C6` | whiteclay-wall.cub | `0x904` | building-carpentersshop.cub |
| `0x88B` | carpet2.cub | `0x8C7` | whiteclay-wall-window.cub | `0x905` | building-weavingmill.cub |
| `0x88C` | carpet3.cub | `0x8C8` | whiteclay-wall-door.cub | `0x993` | inn-sign.cub |
| `0x88D` | framework-base.cub | `0x8C9` | whiteclay-wall-indoor.cub | `0x994` | shop-sign.cub |
| `0x88E` | framework-floor.cub | `0x8CA` | whiteclay-wall-balcony.cub | `0x995` | weapon-shop-sign.cub |
| `0x88F` | framework-floor-stairs.cub | `0x8CB` | whiteclay-wall-lamp.cub | `0x996` | armor-shop-sign.cub |
| `0x890` | framework-wall.cub | `0x8CC` | whiteclay-roof1.cub | `0x997` | identifier-sign.cub |
| `0x891` | framework-wall-window.cub | `0x8CD` | whiteclay-roof2.cub | `0x998` | smithy-sign.cub |
| `0x892` | framework-wall-door.cub | `0x8CE` | whiteclay-roof3.cub | `0x999` | carpentersshop-sign.cub |
| `0x893` | framework-wall-indoor.cub | `0x8CF` | whiteclay-arc.cub | `0x99A` | weavingmill-sign.cub |
| `0x894` | framework-wall-balcony.cub | `0x8D0` | antiqueruin-entrance-stairs.cub | `0x9B2` | door.cub |
| `0x895` | framework-wall-lamp.cub | `0x8D1` | antiqueruin-base.cub | `0x9B3` | big-door.cub |
| `0x896` | framework-roof1.cub | `0x8D2` | antiqueruin-floor.cub | `0x9B4` | window.cub |
| `0x897` | framework-roof2.cub | `0x8D3` | antiqueruin-floor-stairs.cub | `0x9B5` | castle-window.cub |
| `0x898` | framework-roof3.cub | `0x8D4` | antiqueruin-wall.cub | `0x9B6` | gate.cub |
| `0x899` | framework-arc.cub | `0x8D5` | antiqueruin-roof1.cub | `0x9E2` | street-light01.cub |
| `0x89A` | stone-base.cub | `0x8D6` | antiqueruin-roof2.cub | `0x9E3` | fence01.cub |
| `0x89B` | stone-floor.cub | `0x8D7` | antiqueruin-roof3.cub | `0x9E4` | fence02.cub |
| `0x89C` | stone-floor-stairs.cub | `0x8D8` | antiqueruin-arc.cub | `0x9E5` | fence03.cub |
| `0x89D` | stone-wall.cub | `0x8D9` | jungleruin-entrance-stairs.cub | `0x9E6` | fence04.cub |
| `0x89E` | stone-wall-window.cub | `0x8DA` | jungleruin-base.cub | `0x9EE` | overground-dungeon01.cub |
| `0x89F` | stone-wall-door.cub | `0x8DB` | jungleruin-floor.cub | `0x9EF` | underground-dungeon01.cub |
| `0x8A0` | stone-wall-indoor.cub | `0x8DC` | jungleruin-floor-stairs.cub | `0x9F0` | castle-arc.cub |
| `0x8A1` | stone-wall-balcony.cub | `0x8DD` | jungleruin-wall.cub | `0x9F1` | temple-arc.cub |
| `0x8A2` | stone-wall-lamp.cub | `0x8DE` | jungleruin-wall-door.cub | `0x9F2` | bomb1.cub |
| `0x8A3` | stone-roof1.cub | `0x8DF` | jungleruin-wall-indoor.cub | `0x9F3` | glider.cub |
| `0x8A4` | stone-roof2.cub | `0x8E0` | jungleruin-roof1.cub | `0x9F4` | boat2.cub |
| `0x8A5` | stone-roof3.cub | `0x8E1` | jungleruin-roof2.cub | `0xA00` | mission.cub |
| `0x8A6` | stone-arc.cub | `0x8E2` | jungleruin-roof3.cub | `0xA01` | city.cub |
| `0x8A7` | whitewood-base.cub | `0x8E3` | jungleruin-arc.cub | `0xA02` | skull.cub |
| `0x8A8` | whitewood-floor.cub | `0x8E4` | desertruin-entrance-stairs.cub | `0xA03` | airship.cub |
| `0x8A9` | whitewood-floor-stairs.cub | `0x8E5` | desertruin-base.cub | `0xA04` | sphinx01.cub |
| `0x8AA` | whitewood-wall.cub | `0x8E6` | desertruin-floor.cub | `0xA05` | obelisk.cub |
| `0x8AB` | whitewood-wall-window.cub | `0x8E7` | desertruin-floor-stairs.cub | | |
| `0x8AC` | whitewood-wall-door.cub | `0x8E8` | desertruin-wall.cub | | |

Die Paarung Grundstueckstyp → Modell → `typ` aus `Chunk_generateObjects` (`game_misc.cpp:28654-28683`)
passt dazu: Typ 10..13 → `0x8FF..0x902` (Krieger, Waldlaeufer, Schurke, Magier) mit `typ 9..0x0C`,
Typ 14 → `0x903` Schmiede mit `0x0D`, Typ 16 → `0x904` Zimmerei mit `0x0E`, Typ 15 → `0x905` Weberei
mit `0x0F`.
