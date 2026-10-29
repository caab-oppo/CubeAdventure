# AdventureMod — Modulübersicht zum Weiterverwenden

**Kurz:** Der Mod besteht aus einer Trägerschicht (Injektion, Frame-Tick,
Zonen-Callback) und neun weitgehend unabhängigen Bausteinen. Wer Teile
kopieren will, braucht in aller Regel nur `cwsdk/` + `AdventureModFramework`
+ das gewünschte Modul.

Alle Adressen sind **statisch, relativ zu ImageBase `0x400000`**; zur Laufzeit
wird über `GetModuleHandleA(NULL)` umgerechnet (`RebaseAddr()`).

---

## 1. Trägerschicht — braucht man fast immer

| Datei | Was sie tut |
|---|---|
| `main.cpp` (358) | `DllMain` → Thread → D3D9-`EndScene`/`Reset`-Hook. Ruft jeden Frame `AdventureFramework::DispatchGameTick()`, zeichnet Overlays (D3DX-Font), wertet alle Hotkeys aus, registriert beim Start alle Module. |
| `AdventureModFramework.h/.cpp` (92/198) | Kleiner Mod-Loader: `IAdventureMod` mit `OnInitialize` / `OnGameTick` / `OnZoneGenerated`. `InstallZoneGeneratedHook()` patcht die 5-Byte-`CALL`-Instruktion bei `0x46ACD1` (Ziel `0x5E4850` `World::GenerateZone`) — **kein MinHook**, sondern direkter Callsite-Patch mit Byte-Signaturprüfung. |
| `cwsdk/` | Vendorierter Header-Baum (Andoryuuta/cwsdk) mit `cube::Creature`, `cube::GameController`, `cube::World`, `Item`, MSVC-ABI-kompatible `map`/`vector`/`string`. Offsets gegen den Reversal-Baum geprüft. |

**Zum Kopieren:** `AdventureModFramework` ist eigenständig und hängt nur an
`cwsdk`. Der EndScene-Hook in `main.cpp` ist Standard-MinHook-Code und lässt
sich fast unverändert übernehmen.

---

## 2. Die Bausteine

### `GemForge` (2090 Zeilen) — Customization Bench erweitern
Der mit Abstand größte und am gründlichsten belegte Teil. Erweitert die native
Waffen-Werkbank von 32/16 auf 100 Cubes je Material (max. 500), macht
Edelstein-Zutaten setzbar und färbt sie ein.

Gehookte Funktionen:

| Adresse | Funktion | Zweck im Mod |
|---|---|---|
| `0x471B60` | `renderAdaptionCubes` | Extra-Cubes nachzeichnen (Batches zu ≤32) |
| `0x588250` | `VoxelWidget::tryPlaceCube` | vor dem Setzen einen Platz im Item freimachen |
| `0x4482A0` | `render_uploadModelViewMatrices` | Basismatrix der Bench erkennen, Extras in die Vorschau zeichnen |
| `0x4C7F60` | `item_weaponDamage` | Schaden um die Extras skalieren (native Kurve) |
| `0x4C7250` | `material_toColor` | Edelsteinfarben für Materialien `0x0D`–`0x10` |
| `0x448280` | `render_setUniform30` | Farb-Uniform nach der Injektion zurückschreiben |
| `0x4C7660` | `Item_get_stack_or_grid_size` | Zähler „Upgrades n/m" auf die Gesamtzahl |
| `0x58CE40` | `VoxelWidget::toggleSelection` | Auswahl gegen den Clamp der Bench merken |

Wiederverwendbar **unabhängig vom Rest**:

- **`AnalyzeConvention` / `DetectArgModel` / `TailIsRetImm` / `ScanCallSites`** —
  bestimmen zur Laufzeit die Aufrufkonvention *und* das Argumentmodell einer
  Spielfunktion aus dem Maschinencode. Das ist der wertvollste Teil des ganzen
  Projekts, wenn man weitere Funktionen hooken will. Braucht nur `windows.h`.
- **`GetCodeRange()`** — `.text`-Bereich aus den PE-Headern.
- **`ItemKey()`** — stabiler Hash eines `Item` über seine unveränderlichen
  Felder; überlebt das Kopieren zwischen Inventar und Ausrüstung.
- **`SaveStore()` / `LoadStore()`** — simple Binärpersistenz neben `Cube.exe`.
- **Inventar-Walk** in `InventoryImpl()` — `Creature+0x11DC/0x11E0` →
  Kategorien (0x0C Byte) → Slots (`{int32 anzahl; Item item;}`, 0x11C Byte).

### `MountainVillage` (1353) — Bergdorf bauen
Baut per `World::SetBlock` ein komplettes Dorf (L-Schmiede, Häuser, Turm,
Lufthafen), häppchenweise über eine Queue pro Frame. Enthält
`AppendBuilding()`/`AppendProp()` zum Anhängen nativer `zone_static_object`-
Einträge und `RemeshChunks`. Ruft `AirshipTaxi`, `BlacksmithNpc`,
`WanderingMerchant`. Hotkey `STRG+0`.

**Wiederverwendbar:** Block-Setzen, Prop-Platzierung, Remesh, Persistenz-Marker.

### `WanderingMerchant` (678) — Händler mit nativem Shop
Spawnt eine Kreatur, setzt `hostility_flags=3` (macht sie mit `[R]` ansprechbar),
`char_class=0x84` (Innkeeper-Sentinel) und baut einen Vendor-Katalog.
Adressen: `0x40EB60`, `0x43AB30`, `0x593E50`.

### `AirshipSpawner` (596) + `Wegfinder` (214) + `PirateAirship` (148)
Luftschiffe: schreiben direkt in `gc->world.AirshipMap` (GC+0x2F0). `Wegfinder`
ist reine Wegpunkt-Bewegungslogik **ohne Spielbezug** — gut kopierbar.

### `AirshipTaxi` (392) — Reisen zwischen Dörfern
Vendor-NPC auf dem Turm, dessen vier Waren die Nachbardörfer sind.

### `OceanContent` (417) — Fische und Ozean-Weltgen
Hookt `0x487DA0` (Höhenabfrage) und `0x4E5498`.

### `PirateShip` (214) — Piraten auf See

### `BlacksmithNpc` (347) — Schmied + Werkbank-Zugang
Spawnt den Schmied, merkt sich die Werkbank-Positionen
(`OnBenchPlaced`, Deko-Typ `0x4D`) und öffnet die native Weapon-Customization
über den UI-Stack-Push `*(*(widget+0x3C)+0x94)[+0x68] = 1` an
`GC+0x8008F4` / `GC+0x8008DC`. Hookt `0x588500` und `0x587F70`.

**Wiederverwendbar:** `PushOpenWidget()` — das Muster, um beliebige native
UI-Fenster zu öffnen.

---

## 3. Muster, die im ganzen Projekt gelten

**Konvention nie raten.** Jeder Hook auf eine Spielfunktion wird vor der
Installation geprüft (`DetectArgModel`). Fällt das Urteil nicht eindeutig aus,
wird der Hook *nicht* gesetzt. Das hat sich mehrfach als der Unterschied
zwischen „läuft" und „Zugriffsverletzung" erwiesen.

**ECX/EDX durchreichen.** Detours sind `__fastcall(ecx, edx, …)`, damit
`this`-Zeiger erhalten bleiben. Ein `__stdcall`-Detour auf eine Methode
verwirft `ECX` — das war die Ursache mehrerer Abstürze.

**SEH um jeden Rohzugriff** (`__try`/`__except`), 100+ Stellen. Wichtige
Einschränkung: innerhalb eines Win32-Callbacks ist ein geschlucktes
`0xC0000005` trotzdem tödlich (`0xC000041D`) — SEH macht Fehler sichtbar, nicht
harmlos.

**Kill-Switches.** Jeder riskante Hook hat oben in der Datei ein
`static const bool kEnable…`.

---

## 4. Minimalsets zum Kopieren

| Ziel | Nötige Dateien |
|---|---|
| Nur Funktionen hooken | `cwsdk/`, MinHook, `AnalyzeConvention`+`DetectArgModel`+`GetCodeRange` aus `GemForge.cpp` |
| Eigenes Modul mit Frame-Tick | + `AdventureModFramework.*`, EndScene-Hook aus `main.cpp` |
| Auf Zonen-Generierung reagieren | + `InstallZoneGeneratedHook()` |
| Kreaturen spawnen | + `cwsdk/cube_util.cpp` (`SpawnCreature`, `SpawnMonster`, `AttachRandomWalk`) |
| Blöcke/Props setzen | + Block- und Prop-Teil aus `MountainVillage.cpp` |

## 5. Was ungeprüft ist

- Grafik-Init `0x3F7C0` in `cube_util.cpp` (nie eigenständig verifiziert).
- Die meisten Adressen in `cwsdk/cube_funcs.cpp` stammen unverändert aus dem
  öffentlichen cwsdk-Repo und passen möglicherweise zu einer anderen Version.
- Die Zuordnung Edelstein-Zutat → Farbe in `GemForge::kGems[]` beruht teils auf
  der Rarity-Reihenfolge, nicht auf einer Messung.
