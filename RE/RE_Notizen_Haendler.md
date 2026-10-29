# RE-Notizen: Wandernder Händler / Vendor-System

Sammlung aller bisher identifizierten Adressen, Funktionen und Struct-Offsets
rund um das Händler-/Vendor-Feature. ImageBase = `0x00400000`, alle
`FUN_xxxxxxxx`-Namen sind Ghidra-Adressen (statisch, ASLR-sicher über
`RebaseAddr()` umzurechnen).

## Native Funktionen

| Adresse | Name/Rolle | Status |
|---|---|---|
| `FUN_004772b0` | "Vendor-Katalog initialisieren" (baut Kreatur+0x11dc-Vorlage per Namens-Lookup) | **Stürzt reproduzierbar ab** bei SEH `0x477784`, kurz nach dem `FUN_0044ad30`-Vektor-Copy-Aufruf. Weg aufgegeben. |
| `FUN_0042c080` | einziger bekannter Aufrufer von `FUN_004772b0` | nur analysiert, nicht genutzt |
| `FUN_0043bc00` | baut Namens-String (puVar3) für Template-Lookup | analysiert |
| `FUN_0044af00` | schreibt Namen nach Kreatur+0x300 | analysiert |
| `FUN_0043c020` | Vorlagen-Lookup anhand des Namens | analysiert |
| `FUN_004a2300` | kopiert beim `[R]`-Öffnen IMMER Kreatur+0x11dc → GC+0x800c0c (via `FUN_0044ad30`) | **aktiv genutzt/umgangen**: wir schreiben deshalb an BEIDE Stellen |
| `FUN_0044ad30` | generische STDVector-Kopierfunktion | von `FUN_004a2300` und `FUN_004772b0` benutzt |
| `FUN_004e4bd0` | nativer "Ware würfeln"-Aufruf (Template-Registry `gc+0x314`, Weltkontext `gc+0x2e4`, `fieldClass`, `PosSeed*`, Akkumulator) | **funktioniert zuverlässig, stürzt nie** – Basis für `[K]`/`TryRollOneNativeWare` |
| `FUN_00595010` | Item-Konstruktion per Template-Lookup (worldCtx, nameContainer, itemBytes, outAccumulator) | **Stürzt IMMER ab**, zuletzt bei SEH `0x4da1d3` (nach PrimeWorldTileCache-Fix, für alle 10 getesteten category/item-Kombis identische Absturzadresse → fehlender Kontext, nicht item-spezifisch) |
| `FUN_0040eb60` | füllt MSVC-`wstring`-Objekt (this, `wchar_t*`) | sicher, aktiv genutzt |
| `FUN_00593e50` | räumt Name-Container wieder auf | sicher, aktiv genutzt |
| `FUN_004681e0` | ersetzt Live-`wares`-Vektor einer Session durch Akkumulator | sicher, aktiv genutzt (`ApplyWaresSessionFixup`) |
| `FUN_00487da0` | "PrimeWorldTileCache": aktiviert/lädt World-Tile bei (chunkX,chunkY), macht `gc+0x318` (Tree-Root) gültig | wird vor `FUN_00595010` aufgerufen, hat den ursprünglichen Absturz NICHT vollständig behoben |
| `FUN_004882e0` | öffnet/initialisiert eine Vendor-Session vollständig (UI-Flag, Metadaten, wares-Swap) | als Vorlage für `ApplyWaresSessionFixup` reverse-engineered |
| `FUN_004e5f90` | rendert die Shop-Liste **NICHT** aus `wares`, sondern aus separatem Array bei Session`+0x178` (0x18 Byte/Eintrag, MSVC-`wstring`) | **Durchbruch** – Grund, warum `wares` allein nicht reicht |
| `FUN_00488ee0` | periodischer Cleanup: wenn Session`+0x170` (Render-Cursor) ≥ `wares.size()`, wird Slot per `FUN_00411a90` unsichtbar gemacht + `backref` (`+0x188`) auf 0 gesetzt | erklärt "Shop wird leer"-Verhalten, auch bei echten nativen Händlern (siehe Crash-Beobachtung unten) |
| `FUN_00411a90` | versteckt/zeigt Container-Objekt (Param 0 = verstecken) | analysiert |
| `0x0068d668` | spieleigenes `operator_new` (`__cdecl(unsigned int size)`) | aktiv genutzt für alle eigenen Allokationen (gleicher Heap wie das Spiel) |
| `FUN_004c1bb0` | Konstruktor von InventoryWidget1/2/3 + CharacterWidget | bestätigt, alle 4 Instanzen |
| `FUN_004c2050` | **gemeinsame Render-Funktion** aller vier Widgets, liest `+0x194` (Modus 0-3, 3=Vendor) und `+0x160`/`+0x1b4` | **UNTERSUCHUNG LÄUFT** – `VendorPanelRenderResearch.java` erstellt, noch nicht ausgeführt/ausgewertet |
| `FUN_004889e0` | entscheidet über Kreatur`+0x140`-Typ-Byte (0x80/0x81/0x82), ob Vorlage geladen wird; auch Teil der Widget-"neu zeichnen"-Flag-Kette | analysiert |
| `FUN_0042f3e0` | native Item-Init (level=1, category_id/item_id setzen, customization_data nullen) | manuell nachgebaut in `TryBuildOneWareEntry` |

## Sonstige statische Adressen

- `0xadacd1` – Callsite des `GenerateZone`-Hooks (AdventureModFramework)
- `+0x3B690` – CreatureCtor
- `+0x68AD0` – EntitesMap-Insert
- `+0x3F7C0` – init_creature_graphics_by_race (UNVERIFIZIERT)
- `+0x1EE7A0` – cube_world_init_companion

## Bekannte Crash-Adressen (SEH, statisch/Ghidra-relativ)

- `0x477784` – in `FUN_004772b0`, direkt nach `FUN_0044ad30`-Aufruf (native Katalog-Init, Weg aufgegeben)
- `0x4da1d3` – in `FUN_00595010`, für ALLE getesteten category_id/item_id (auch nach PrimeWorldTileCache-Fix) – deutet auf fehlenden gemeinsamen Kontext hin, nicht auf ungültige Items
- `0x00ABA3D5` (schreibt nach `0x0080242D`) – **nativer Crash bei einem ECHTEN Vendor** (nicht unser Mod!), vom Nutzer live beobachtet: Shop wurde leer, beim Wiederöffnen Absturz. Verdacht: Nullpointer + Offset im `0x8xxxxx`-Bereich, passt zur `FUN_00488ee0`-Cleanup-Logik (Session wird "unsichtbar" gemacht, danach ungültiger Zugriff beim erneuten Öffnen)

## GameController (`gc`) – Offsets

| Offset | Bedeutung |
|---|---|
| `+0x2e4` | worldCtx (Welt-/Tile-Kontext) |
| `+0x314` | Template-Registry |
| `+0x318` | Tree-Root (World-Tile-Cache), von `FUN_00487da0` befüllt |
| `+0x800954` | InventoryWidget1 |
| `+0x800958` | InventoryWidget2 |
| `+0x80095C` | InventoryWidget3 (== Modus 3 "Vendor", **`+0x160` zeigt auf `GC+0x800c0c`**) |
| `+0x800960` | CharacterWidget |
| `+0x800c0c/+0x800c10/+0x800c14` | Vendor-Kategorien-Vektor (Start/End/Cap), 0xC Byte/Eintrag `{itemArrayStart, itemArrayEnd, feld3}` |
| `+0x8008d8` | gecachtes Zielobjekt (aktuell anvisierte Kreatur) |
| `->vendor_session_pool` | STDVector aktiver Vendor-Session-Slot-Zeiger |
| `->local_player` | Zeiger auf lokale Spieler-Kreatur |

## Widget-Instanz – Offsets (relativ zu InventoryWidget1/2/3/CharacterWidget)

| Offset | Bedeutung |
|---|---|
| `+0x160` | Zeiger auf Listen-/Kategorien-Vektor |
| `+0x164` | konstant `0xebae020` bei allen Instanzen beobachtet (vermutlich Font-/Atlas-Manager) |
| `+0x184/+0x188` | meist `0xffffffff`, bei CharacterWidget Farbwerte (`0xff0000`/`0xffffff`) |
| `+0x194` | **MODUS**: 0=Equip-Geschwister, 1=Equipment, 2=Weapons/.../Formulas, 3=Vendor |
| `+0x198` | unbekannt |
| `+0x1a8/+0x1ac` | unbekannt (evtl. Zeilen/Spalten-Anzahl) |
| `+0x1b4` | SUBINDEX (z.B. welcher Formulas/Weapons-Slot) |

## Creature (`Kreatur`) – Offsets

| Offset | Bedeutung |
|---|---|
| `+0x140` | Typ-Byte (0x80/0x81/0x82); unser Händler hat `0x80` |
| `+0x11dc/+0x11e0/+0x11e4` | **Vorlage-STDVector** (Start/End/Cap) – Quelle, aus der `FUN_004a2300` beim `[R]`-Öffnen `GC+0x800c0c` befüllt |
| `+0x11e8/+0x11ec` | Nachbarfelder, in allen Tests `0`, Zweck unklar |
| `+0x1304/+0x1308` | Nachbarfelder, in allen Tests `0`, Zweck unklar |
| `+0x1350.. +0x1364` | Metadaten, werden in Vendor-Session `+400.. +0x1a4` kopiert |
| `entity_data.field_1D8` | "fieldClass"-Parameter für `FUN_004e4bd0` |
| `+0x300` | Name-String (von `FUN_0044af00` geschrieben, Template-Lookup) |
| `entity_data.level` | Charakterlevel |
| `entity_data.char_class` | Klassen-Byte (Testcharakter: `0x4`, Level 190) |
| `entity_data.equipment` | `WornEquipment`-Struct (siehe unten) – **Layout live bestätigt** |
| `Inventory_Tabs_Vec` | **globale Item-Template-Datenbank** (konstante Counts 1772/392-396/114/60-62, `count=0` immer) – **NICHT** das tatsächliche Spielerinventar (vorbestehende Fehlbenennung im Code, nicht behoben) |
| gesamt `0x1E60` | `assert_size`-Zwang auf die volle Creature-Struct |

## Vendor-Session (`sessionPtr`, aus `vendor_session_pool`) – Offsets

| Offset | Bedeutung |
|---|---|
| `+0x148` | Start der Zeiger-Kette fürs UI-"neu zeichnen"-Flag (`*(+0x148)` → `+0x3c` → `+0x94`[Array] → Index `+0x68`) |
| `+0x160/+0x164/+0x168` | `wares`-STDVector: Array von `WareEntry{itemPtr(4B), qty(4B)}` – **Zeiger-Indirektion**, ANDERES Format als `GC+0x800c0c`! |
| `+0x16c` | Render-Bedarfs-Gate (auf `0x7fffffff` gesetzt = immer erfüllt) |
| `+0x170` | Render-"verarbeitet"-Cursor, von `FUN_004e5f90` hochgezählt, von `FUN_00488ee0` gegen `wares.size()` geprüft (Cleanup/Verstecken) |
| `+0x174` | Timer/Zähler, zurückgesetzt |
| `+0x178/+0x17c/+0x180` | **separates Anzeige-Array** aus MSVC-`wstring` (0x18=24 Byte/Eintrag) – tatsächliche Quelle für Name/Preis-Text im Sprechblasen-Fenster (`BuildDisplayCacheLines`) |
| `+0x188` | `backref` auf Händler-Kreatur |
| `+400, +0x194, +0x198, +0x19c, +0x1a0, +0x1a4` | Metadaten, kopiert aus Kreatur `+0x1350.. +0x1364` |

## Struct: `Item` (0x118 = 280 Byte, aus `cwsdk/non_cube.h`)

```cpp
struct ItemAdaption { uint8_t x, y, z, material; uint16_t unk_level_maybe; uint16_t _pad; };
struct Item {
    uint8_t category_id;
    uint8_t item_id;
    uint8_t field_2;
    uint8_t field_3;
    int32_t modifier;
    int32_t field_8;
    uint8_t rarity;
    uint8_t material;
    uint8_t adapted;
    uint8_t field_F;
    uint16_t level;
    uint16_t field_12;
    ItemAdaption customization_data[32];
    int32_t adaption_count;
};
```
`MemoryItemStack { Item item; int32_t count; }` = 284 Byte (Item liegt VOR Count).

## Struct: `WornEquipment` (Layout live bestätigt)

```cpp
struct WornEquipment {
    Item unk;          // category_id=0 (meist leer/unbenutzt)
    Item necklace;      // category_id=8
    Item chest;          // category_id=4
    Item feet;            // category_id=6
    Item hands;            // category_id=5
    Item shoulder;          // category_id=7
    Item left_weapon;        // category_id=3
    Item right_weapon;        // category_id=3
    Item left_ring;            // category_id=9
    Item right_ring;            // category_id=9
    Item light;                  // category_id=24 (Lampe)
    Item special;                 // category_id=23
    Item pet;                      // category_id=0 (meist leer)
};
```

## Bekannte `category_id`-Werte

| Wert | Ausrüstungsslot |
|---|---|
| 3 | Waffe |
| 4 | Brustpanzer |
| 5 | Handschuhe |
| 6 | Schuhe |
| 7 | Schulterpanzer |
| 8 | Amulett/Halskette |
| 9 | Ring |
| 0xB (11) | "itemvendoritem" (Test-Kategorie aus `[H]`) |
| 0x17 (23) | Special |
| 0x18 (24) | Licht/Lampe |

## Eigene Funktionen (`WanderingMerchant.cpp` / `main.cpp`)

- `BuildCustomVendorCatalog(gc)` – baut Katalog direkt in `GC+0x800c0c` + `Kreatur+0x11dc`; pro Ausrüstungsslot: erst `BuildNativeCatalogItem` (nativ via `FUN_00595010`) versuchen, bei Fehlschlag Fallback auf `CopyPlayerSlotItem` (1:1-Kopie). **Aktuell schlägt der native Weg für ALLE Slots fehl**, es wird also durchgehend die 1:1-Kopie verwendet.
- `BuildNativeCatalogItem(...)` – ruft `TryBuildOneWareEntry` auf, dereferenziert `WareEntry.itemPtr`, kopiert die dort liegenden nativ-konstruierten Item-Bytes. **Schlägt aktuell immer fehl** (SEH bei `0x4da1d3`).
- `ApplyCustomVendorCatalog(...)` – POD-Hilfsfunktion (C2712-Workaround), baut Item-/Kategorie-Puffer per `operator_new`, schreibt an `GC+0x800c0c` UND `Kreatur+0x11dc`.
- `CopyPlayerSlotItem(...)` – reine 1:1-Struct-Kopie (aktueller Fallback).
- `MakeCatalogItem`/`MakeCatalogItemFromPlayerSlot` – ältere, jetzt ungenutzte Bausteine (rarity/level überschreiben).
- `TryBuildOneWareEntry(...)` – `PrimeWorldTileCache` + 3 native Aufrufe (`FUN_0040eb60`, `FUN_00595010` [stürzt], `FUN_00593e50`).
- `PrimeWorldTileCache(gc)` – ruft `FUN_00487da0(worldCtx,0,0)` auf.
- `TryRollOneNativeWare(...)` – ruft `FUN_004e4bd0` direkt auf (sicher, für `[K]`).
- `ApplyWaresSessionFixup(...)` – UI-Flag, Metadaten, `wares`-Swap, Timer-Reset (Nachbau von `FUN_004882e0`).
- `BuildDisplayCacheLines(...)` – baut das separate Anzeige-Array bei Session`+0x178` (gilt NUR für das Sprechblasen-Fenster, NICHT für `InventoryWidget3`/`GC+0x800c0c`).
- `FindOurVendorSessionPtr`/`FindOrRecallOurVendorSessionPtr` – findet unseren Session-Slot via `backref +0x188`.
- `DumpInventoryWidgetsDebug(gc)` – Debug-Dump aller vier Widgets + `GC+0x800c0c` + `Kreatur+0x11dc` (seit dieser Session gekürzt).
- `RebaseAddr(staticAddr)` – `imageBase + (staticAddr - 0x400000)`.
- `SehLogFilter(...)` – loggt Exception-Code + rohe und statische (Ghidra-relative) Absturzadresse.

## Ghidra-Skripte (`AdventureMod/ghidra_scripts/`)

- `CreatureCatalogFieldResearch.java` – fand `FUN_004772b0` über Scalar-Xref-Suche auf `0x11dc` & Nachbarfelder.
- `VendorInitCallerResearch.java` – fand `FUN_004772b0`s Aufrufer `FUN_0042c080`, decompilierte die Namens-Lookup-Kette.
- `VendorPanelRenderResearch.java` – **NEU, noch nicht ausgewertet**: decompiliert `FUN_004c2050` (Render-Funktion) + Aufrufer + `FUN_004c1bb0` (Konstruktor). Ziel: herausfinden, ob das Vendor-Panel Name/Preis direkt aus dem Item-Struct liest oder über eine eigene Zwischenschicht (analog zu Session`+0x178`).

## Offener Kernpunkt (aktueller Stand)

Items landen korrekt (byte-genau geprüft) in `GC+0x800c0c`, aber das
"VENDOR"-Panel zeigt sie teils als leere Platzhalter. Zwei Erklärungsansätze,
noch nicht entschieden:

1. **Fehlende Zwischenschicht**: Analog zum Sprechblasen-Fenster (Session`+0x178`)
   könnte auch `InventoryWidget3` Name/Preis aus einem separaten, noch nicht
   gefundenen Cache lesen statt direkt aus dem `Item`-Struct. → wird gerade mit
   `VendorPanelRenderResearch.java` geprüft.
2. **Fehlender Konstruktionskontext**: `FUN_00595010` (die einzige bekannte
   Funktion, die "vollständige" Items mit korrektem Namen liefert) stürzt
   IMMER ab, auch mit `PrimeWorldTileCache`-Fix – es fehlt also noch ein
   weiterer Setup-Schritt aus dem originalen `[R]`-Callpfad, den wir nicht
   nachgebaut haben.
