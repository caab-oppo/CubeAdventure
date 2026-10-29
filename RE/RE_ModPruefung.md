# Prüfung: warum keine nativen Gebäude mehr gesetzt werden

**Kurz:** Das neunte Argument von `placeStructure` ist **kein Schalter, sondern ein
Zeiger auf eine Liste**. Der Mod übergibt dort die Zahl `1` (`g_argExtra`,
NativeHouseProbe.cpp:55). Sobald die Funktion die Liste anfasst, liest sie auf
Adresse `0x19` — Zugriffsverletzung, vom `__except` in `PlaceModel` geschluckt,
Rückgabe `false`. Der Rückfall in `PlaceBuilding` benutzt **dasselbe** `g_argExtra = 1`
und stürzt genauso ab; deshalb melden alle vier Gebäude „FEHLGESCHLAGEN", obwohl
`mode = 0` früher zuverlässig lief. Bäume überleben, weil sie die betroffenen
Codepfade nicht auslösen (keine Markerfarben, praktisch keine Innenhohlräume).

Beleglage: `WorldInfo.cpp:5816–5827`, `5964–5977`, `5659–5672`, `6393–6402` —
`param_9` wird dort als `*(int*)(param_9+0x18)` und `*(int*)(param_9+0x1c)`
dereferenziert. Alle 22 nativen Aufrufstellen im Reversal übergeben `0`.

---

## 1. Fehlerliste, nach Schwere

### F1 — BELEGT, tödlich: `g_argExtra = 1` ist ein Zeiger, kein Flag

**Datei:** `NativeHouseProbe.cpp:55` (Definition), `:321` (Übergabe), `:270` (Typedef)

Die Typdeklaration im Mod:

```cpp
// NativeHouseProbe.cpp:261-270
typedef void(__stdcall* Fn_PlaceStructure)(
    void* tmpl, int* origin, unsigned rot, unsigned char color, int kind,
    void* zone, char flag, int* pal,
    int extra);          // <-- FALSCHER TYP
```

Im Reversal (`cube/world/WorldInfo.cpp:5099`) heißt dasselbe Argument `param_9`
und wird an drei Stellen als Container-Zeiger benutzt:

```c
/* WorldInfo.cpp:5816-5827  (Voxel liegt in einem Innenhohlraum) */
if (param_9 != 0) {
  local_6a4 = *(_Container_base0 **)(param_9 + 0x18);   // Listenkopf
  ...
  if (*(int *)(param_9 + 0x1c) == 0x9249248)            // Elementzahl
    std::_Xlength_error("list<T> too long");
  *(int *)(param_9 + 0x1c) = *(int *)(param_9 + 0x1c) + 1;
}
```

Dieselbe Sequenz noch einmal bei `WorldInfo.cpp:5964-5977` und, über `iVar14`
(gesetzt aus `param_9` bei `:5536`), bei `WorldInfo.cpp:5659-5672`. Im
Markerzweig `zone == 0xd` steht die gleiche Prüfung mit einem `thiscall`-Push
(`WorldInfo.cpp:6393-6402`, `list_pushBack_via451e80`).

`0x9249248 * 0x1C = 0xFFFFFFE0` — die Elementgröße ist 0x1C Byte, also eine
`std::list`. `param_9` ist ein Protokoll der geschriebenen Blöcke (Farbe +
Position, siehe `WorldInfo.cpp:6394-6400`), **nicht** ein „Marker löschen"-Schalter.

Alle nativen Aufrufe übergeben 0:

| Aufrufstelle | param_9 |
|---|---|
| `game_misc.cpp:28519` / `:28534` | `uVar36 = 0` |
| `game_misc.cpp:28757` (Klassenhäuser 0x900–0x905) | `uVar30 = 0` |
| `game_misc.cpp:28792` / `:28831` / `:28902` | `uVar33 = 0` |
| `game_misc.cpp:28971` / `:29009` (Tempel) | Literal `0` |
| `game_misc.cpp:45884`–`:45994` | Literal `0` |

**Warum das genau das Symptom erklärt:** `1` ist eine Adresse in den ersten
64 KB — dort ist unter Win32 nie gemappt. Der erste Zugriff auf `0x19` ist eine
sichere Zugriffsverletzung. Der Pfad bei `:5659`/`:5816` läuft, wenn eine Spalte
des Modells zwischen ihrem untersten und obersten vollen Voxel eine Lücke hat
(`local_6cc < z < local_6c8`, berechnet bei `WorldInfo.cpp:5395-5429`). **Häuser
sind hohl → trifft sofort zu. Bäume sind massiv und haben keine Markerfarben →
trifft nicht zu.** Das ist genau die beobachtete Trennung.

**Korrektur:**

```cpp
// NativeHouseProbe.cpp:55
static int* g_argExtra = nullptr;   // param_9: std::list<BlockLog>* oder 0

// NativeHouseProbe.cpp:261-270
typedef void(__stdcall* Fn_PlaceStructure)(
    void* tmpl, int* origin, unsigned rot, unsigned char kind, int mode,
    void* zone, char flag, int* clip,
    void* blockLog);    // 0 = kein Protokoll (so macht es das Spiel überall)

// NativeHouseProbe.cpp:321
f(tmpl, origin, (unsigned)(rot & 3), g_argKind, mode, zone, g_argFlag, pal,
  nullptr);
```

---

### F2 — BELEGT, tödlich in Kombination: der Rückfall wiederholt den Fehler

**Datei:** `NativeHouseProbe.cpp:413-419`

```cpp
if (!PlaceModel(reg, zc, modelIndex, cx, cy, cz, rot, mode)) {
    if (!PlaceModel(reg, zc, modelIndex, cx, cy, cz, rot, 0)) return false;
    ...
}
```

Der Rückfall ändert **nur** `mode`. `g_argKind`, `g_argFlag` und vor allem
`g_argExtra` sind globale Variablen und bleiben auf `1` (`:321`). Der Zustand,
der früher „alle 4 gesetzt" lieferte, war `mode = 0` **und** `extra = 0` — genau
den stellt der Rückfall nicht her. Deshalb schlägt auch der zweite Versuch fehl
und `PlaceBuilding` liefert `false`.

Zweiter Punkt, VERMUTET: der erste Versuch stürzt **mitten in** `placeStructure`
ab. Die Funktion hat zu diesem Zeitpunkt bereits Blöcke geschrieben und
möglicherweise `zone->static_objects` (Offset +0x0C/+0x10/+0x14, `WorldInfo.cpp:5726-5772`)
vergrößert. Das `__except` fängt die Ausnahme, aber die C++-Abwicklung des
Spiel-Frames (`local_8`-Statusvariable, `ExceptionList` bei `WorldInfo.cpp:5282-5285`)
läuft nicht sauber zu Ende. Ein zweiter Aufruf auf derselben Zone ist danach
nicht mehr vertrauenswürdig — ein Rückfall direkt nach einer Ausnahme ist
grundsätzlich die falsche Reaktion.

**Korrektur:** Argumente explizit übergeben statt global halten, und nach einer
Ausnahme **nicht** erneut in dieselbe Zone schreiben:

```cpp
struct PlaceArgs { unsigned char kind; int mode; char flag; void* log; };

static bool PlaceModel(uint32_t reg, void* zone, int modelIndex,
                       int bx, int by, int bz, int rot,
                       const PlaceArgs& a, const char*& why);

bool PlaceBuilding(...) {
    const char* why = "";
    PlaceArgs a = { 6, mode, 1, nullptr };
    if (PlaceModel(reg, zc, modelIndex, cx, cy, cz, rot, a, why)) return true;
    std::cout << "[Bau] Gebaeude 0x" << std::hex << modelIndex << std::dec
              << " nicht gesetzt: " << why << std::endl;
    if (std::strcmp(why, "Ausnahme im Aufruf") == 0) return false; // NICHT nachlegen
    a.mode = 0;
    return PlaceModel(reg, zc, modelIndex, cx, cy, cz, rot, a, why);
}
```

---

### F3 — BELEGT: `PlaceModel` kann fünf verschiedene Ursachen nicht unterscheiden

**Datei:** `NativeHouseProbe.cpp:310-324`

```cpp
static bool PlaceModel(uint32_t reg, void* zone, int modelIndex, ...) {
    void* tmpl = (void*)RegistryAt(reg, (uint32_t)modelIndex);
    if (!tmpl) return false;                       // (1) Vorlage leer
    int w = 0, h = 0;
    if (!ReadModelSize(tmpl, w, h)) return false;  // (2) Feld unlesbar ODER (3) unplausibel
    ...
    __try { f(...); return true; }
    __except (...) { return false; }               // (4) Ausnahme im Aufruf
}
```

Dazu kommen in `PlaceBuilding` (`:404-408`) noch „kein gc", „keine Registry" und
„Zone nicht geladen" — ebenfalls als nacktes `false`. Die Konsolenzeile
`MountainVillage.cpp:1185` druckt daraus ein einziges Wort. **Damit ist der
Fehlschlag prinzipiell nicht zuordenbar** — das ist selbst ein Befund, siehe
Abschnitt 3.

Was sich hier trotzdem **durch Ausschluss belegen** lässt:

- Registry ist da: `Available()` (`:331-339`) hat `true` geliefert, sonst hätte
  `MountainVillage.cpp:1167` gemeldet. `RegistryAt` funktioniert also.
- Zone ist geladen: die Bäume stehen bei Radius 74, die Häuser bei Radius 54
  (`MountainVillage.cpp:597`, `:1217`). Wäre `SafeZone` null, wären die Bäume
  zuerst ausgefallen.
- Bleibt: Vorlage/Größe modellspezifisch, oder die Ausnahme im Aufruf. Da
  `0x87E`–`0x880` und `0x903` laut `MODELL_INDEX.md` im Spiel bestätigt sind und
  `ReadModelSize` nur `w,h ∈ [1,512]` verlangt (33×33 bzw. 32×31 passen),
  bleibt die **Ausnahme im Aufruf** übrig. Das deckt sich mit F1.

---

### F4 — BELEGT: falsche Z-Höhe für die Gebäude

**Datei:** `MountainVillage.cpp:1181` (`deckZ + 1`), `NativeHouseProbe.cpp:317`

Der native Generator setzt die Klassenhäuser (`0x900`–`0x905`, darunter die
Schmiede `0x903`) mit dem Ursprung **sieben Blöcke unter** der Geländeoberkante:

```c
/* game_misc.cpp:28753-28757 */
uVar35 = vec3_set(..., ..., (int)local_5c7c + -7);   // local_5c7c = Terrain-Oberkante
WorldInfo_placeStructure(local_1c._4_4_, uVar35, rot, 6, piVar18, iVar5, 1, puVar10, 0);
```

Auch die anderen Aufrufe versenken das Modell (`-7` bei `game_misc.cpp:28516`,
`-5` bei `:28530`), oder sie fahren `z` in einer Schleife herunter, bis ein
solider Block gefunden ist (`game_misc.cpp:28959-28967`). Der Mod übergibt
stattdessen `deckZ + 1`, also die Oberkante **plus** eins — rund **acht Blöcke
zu hoch**. Der `kFloorBlocks = 7` aus `NativeHouseProbe.cpp:60` ist genau dieser
Versatz; er steht im Mod, wird aber nirgends angewendet.

Erklärt **nicht** das „FEHLGESCHLAGEN", erklärt aber die offene Frage
„Rampen/Höhenversatz" aus `RE_Notizen_NativeHaeuser.md` Abschnitt 7.

**Korrektur:**

```cpp
// MountainVillage.cpp:1180-1181
bool okB = NativeHouseProbe::PlaceBuilding(gc, g_houses[i].model,
    g_houses[i].hx, g_houses[i].hy, deckZ + 1 - kFloorBlocks,   // Sockel versenken
    g_houses[i].rot, mmode);
```

---

### F5 — BELEGT: `mode` gehört zum Modell, nicht zum Wunschergebnis

**Datei:** `NativeHouseProbe.h:123-124`, `MountainVillage.cpp:1178-1179`

Der Mod behandelt `MarkerSmithy = 0x0D` / `MarkerHouse = 0x0E` als frei wählbare
Betriebsart. Im Generator ist der Wert dagegen **fest an den Modellindex
gekoppelt** (`game_misc.cpp:28656-28683` wählt das Modell, `:28722-28742` den
dazu passenden Modus):

| Kachel-Typ | Modell | mode (param_5) |
|---|---|---|
| 0 | `0x900` | `0x0A` |
| 1 | `0x901` | `0x0B` |
| 2 | `0x902` | `0x0C` |
| 3 | **`0x903` Schmiede** | **`0x0D`** |
| 4 | `0x905` | `0x0F` |
| 5 | `0x904` | `0x0E` |

Für die Schmiede ist die Wahl `0x0D` also **richtig** — belegt. Für
`0x87E`/`0x87F`/`0x880` ist `0x0E` dagegen der Modus des Zimmermannshauses
`0x904`; welchen Modus die Dorfhäuser nativ bekommen, steht im vorliegenden
Reversal nicht (kein direkter Verweis auf `0x87E`). VERMUTET: die Markerfarben
dieser Modelle werden von den `0x0D`/`0x0E`-Zweigen gar nicht abgedeckt, weshalb
die Umwandlung auch bei korrektem `param_9` nur teilweise greift.

`mode = 0` ist im Übrigen sauber definiert: `WorldInfo.cpp:5641-5645` verlässt
den gesamten Markerblock (`goto LAB_005f493b`), wenn `mode` nicht in
`{1..7, 9, 10, 0x0B..0x0F}` liegt. Das deckt sich mit „mode 0 setzt, lässt aber
die bunten Blöcke stehen".

---

### F6 — VERMUTET, mittelschwer: kein Weltlock, Aufruf im Renderthread

**Datei:** `MountainVillage.cpp:1162-1164`, `:1226`, `AdventureModFramework.h:42-43`

```cpp
// MountainVillage.cpp:1162-1164
// BEWUSST VOR SafeLockWorld: placeStructure greift selbst auf die Zone zu
// und wuerde sich an unserer Sperre verklemmen.
```

Die Begründung ist falsch. `World::Lock()` ist eine `CRITICAL_SECTION`
(`cwsdk/cube/World.cpp:6-8`), und CRITICAL_SECTIONs sind **rekursiv** — derselbe
Thread kann sie mehrfach betreten, ohne sich zu verklemmen. Der Aufruf läuft
außerdem laut `AdventureModFramework.h:42-43` und `:88-89` aus dem
D3D9-`EndScene`-Hook, also im Renderthread, während der Spiel-Thread dieselbe
Zone und dieselben Chunks beschreibt. `placeStructure` vergrößert dabei
`zone->static_objects` per `std_vector_Reserve_grow_188` (`WorldInfo.cpp:5731`,
`:5775`) — eine Reallokation mitten in einem Fremdzugriff ist ein klassisches
Datenrennen und eine plausible Zweitursache für sporadische Ausnahmen (das
„1 von 4" bei `mode = 1`).

**Korrektur:** Sperre setzen statt weglassen —

```cpp
// MountainVillage.cpp:1165 ff.
SafeLockWorld(w);            // CRITICAL_SECTION ist rekursiv, kein Deadlock
if (kUseNativeHouses) { ... }
// ... restliche Deko ...
SafeUnlockWorld(w);
```

---

### F7 — BELEGT, mittelschwer: `Probe()` testet nicht das, was im Dorf passiert

**Datei:** `NativeHouseProbe.cpp:272-280`

```cpp
static bool CallPlaceStructure(void* tmpl, int* origin, void* zone, int* pal) {
    ...
    f(tmpl, origin, 0, 6, 0, zone, 0, pal, 0);   // flag=0, extra=0
}
```

Die Probe (STRG+7) benutzt `flag = 0` und `extra = 0` — der Dorfbau benutzt
`flag = 1` und `extra = 1`. Der Generator benutzt durchgängig `flag = 1`
(`game_misc.cpp:28757`, `:28971`). Die Probe kann den Fehler also gar nicht
reproduzieren; sie meldet „ohne Ausnahme" und der Dorfbau stürzt trotzdem.
Kommentar `:275-276` behauptet „wie im Dorfgenerator", stimmt aber bei `flag`
nicht.

---

### F8 — BELEGT, gering: Zone wird aus der Mitte bestimmt, das Modell wird geklippt

**Datei:** `NativeHouseProbe.cpp:407` (`SafeZone(gc, cx, cy)`), `:317`

`placeStructure` prüft am Anfang, ob der Grundriss überhaupt die übergebene Zone
überlappt (`WorldInfo.cpp:5306-5340`, Vergleiche gegen `zone->+0x60 * 0x100`
und `zone->+0x64 * 0x100`). **Trifft das nicht zu, kehrt die Funktion still
zurück — ohne Ausnahme.** `PlaceModel` liefert dann `true`, obwohl nichts gesetzt
wurde. Ein 33×33-Haus, dessen Mitte nahe einer 256er-Zonengrenze liegt, ragt
über die Zone hinaus; die Schreibvorgänge laufen über `world_getChunkCell256`
mit genau dieser Zone als Hinweis (`WorldInfo.cpp:5586`, `:5760`).

VERMUTET: teilweise gesetzte Häuser bei ungünstiger Zonenlage. Das war
möglicherweise die Ursache für frühere „gesetzt, aber nichts zu sehen"-Fälle.

**Korrektur:** Zone aus der **Ecke** des Grundrisses holen, nicht aus der Mitte,
und den Erfolg nachträglich prüfen (Blockstichprobe im Grundriss).

---

### F9 — BELEGT, gering: Off-by-one in der Registry-Erkennung

**Datei:** `NativeHouseProbe.cpp:176`

```cpp
const int kNeed = (int)(kModelLast - kModelFirst) - 2;   // = 0x8EE-0x8D0-2 = 28
```

Die Schleife in `ScorePlausible` (`:165`) prüft `kModelLast - kModelFirst + 1 = 31`
Einträge. Der Schwellwert müsste `31 - 2 = 29` sein, nicht `28`. Folge: die
Erkennung ist eine Stufe zu großzügig. Nicht ursächlich, aber ein falscher
Treffer wäre schwer zu finden.

```cpp
const int kNeed = (int)(kModelLast - kModelFirst + 1) - 2;
```

---

### F10 — BELEGT, gering: Argumentnamen im Typedef widersprechen der Dokumentation

**Datei:** `NativeHouseProbe.cpp:264-266` gegen `RE_Notizen_NativeHaeuser.md` Abschnitt 2

Der Typedef nennt Argument 4 `color` und Argument 5 `kind`; die Notizen nennen
sie `kind` und `mode`; das Reversal nennt sie `param_4` (byte) und `zone`
(Ghidra-Fehlbenennung, tatsächlich der Modus). Übergeben wird `g_argKind` an den
Parameter namens `color` und `mode` an den Parameter namens `kind`
(`NativeHouseProbe.cpp:321`). Die **Werte** stimmen, die Namen nicht — bei der
nächsten Änderung ist das eine Falle.

Belegte Belegung, aus `game_misc.cpp:28757`:

```
placeStructure(tmpl, origin, rot, kind=6, mode=0x0A..0x0F, zone, flag=1, clip[4]={0,0,0,0}, log=0)
```

Argument 8 ist übrigens kein Farbpuffer, sondern ein `int[4]`-Randrechteck, das
je nach Drehung als `[0..3]` gelesen wird (`WorldInfo.cpp:5344-5382`). Vier
Nullen sind richtig, der Kommentar „Farbpuffer" (`:269`) ist es nicht.

---

### F11 — Geprüft, kein Fehler

- **`__try` neben C++-Objekten (C2712):** sauber getrennt. `ReadPair`,
  `ScorePlausible`, `ReadModelSize`, `SafeZone`, `ReadBlockNH`, `SetBlockNH`,
  `ReadZoneObjects`, `ReadObj`, `RemeshAll` enthalten nur PODs. `ScanArea` und
  `DumpRegistry` haben `std::ofstream`, aber kein `__try` im selben Rumpf.
- **Achsentausch bei ungerader Drehung** (`NativeHouseProbe.cpp:316`): korrekt.
  Der Mod tauscht nur für die Zentrierung; `placeStructure` tauscht intern
  nochmal für sich selbst (`WorldInfo.cpp:5292-5302`). Das ist genau das
  Verhalten des Generators (`game_misc.cpp:28944-28950`).
- **Zentrierung `base - breite/2`** (`:317`): entspricht dem Generator
  (`game_misc.cpp:28951-28954`). Beim Klassenhaus-Aufruf steht statt `w/2` die
  Konstante `-0x10` (`game_misc.cpp:28695`) — für ein 32 Blöcke breites Modell
  dasselbe. Die Y-Achse hat dort einen abweichenden festen Versatz (`-4`);
  VERMUTET ohne Auswirkung auf den Fehlschlag.
- **`vectorU32_at` als `__fastcall(ecx, edx, idx)`** (`:209-214`): richtig.
  `__thiscall` mit einem Stack-Argument räumt 4 Byte selbst ab, `__fastcall` mit
  drittem Stack-Argument ebenfalls.
- **`__stdcall` für `placeStructure`:** durch die Aufrufstellen-Analyse belegt
  und mit dem Reversal konsistent (kein `add esp` hinter den 37 Calls).
- **`origin`/`pal` als lokale Arrays** (`:317-318`): Lebensdauer reicht über den
  Aufruf, kein Fehler.
- **`FloorDivP`** (`:282`): korrekt für negative Koordinaten.
- **Zone-Layout:** `static_objects` bei +0x0C (`cwsdk/cube/Zone.h:13`) deckt sich
  mit `param_6 + 0xc/0x10/0x14` im Reversal. `+0x60`/`+0x64` sind die
  Zonenkoordinaten. Der Zeiger ist also richtig typisiert.

---

## 2. Reihenfolge der Reparatur

1. **F1** — `param_9` auf `nullptr`. Einzeiler, behebt aller Wahrscheinlichkeit
   nach das gesamte Symptom.
2. **F3** — getrennte Meldungen einbauen, **bevor** weiter geraten wird.
3. **F2** — Rückfall nicht nach einer Ausnahme, und Argumente lokal statt global.
4. **F4** — Z um `kFloorBlocks` versenken.
5. **F6** — Weltsperre setzen.
6. Rest.

---

## 3. Diagnose fehlt

Der Mod kann derzeit nicht sagen, **woran** ein Fehlschlag lag. Das hat die
Fehlersuche mehrfach in die falsche Richtung geschickt (siehe
`RE_Notizen_NativeHaeuser.md` Abschnitt 6). Nötig sind:

**a) Ursachencode statt `bool` aus `PlaceModel`**

```cpp
enum PlaceResult {
    PR_OK = 0,
    PR_NO_REGISTRY,      // FindModelRegistry lieferte 0
    PR_NO_ZONE,          // SafeZone lieferte nullptr
    PR_NO_TEMPLATE,      // RegistryAt(index) == 0
    PR_SIZE_UNREADABLE,  // Readable(tmpl,0x50) fehlgeschlagen
    PR_SIZE_IMPLAUSIBLE, // w/h ausserhalb 1..512  -> w,h mitloggen!
    PR_EXCEPTION,        // SEH im Aufruf
    PR_OUTSIDE_ZONE      // Grundriss ueberlappt die Zone nicht (still, kein Fehler)
};
static const char* PlaceResultName(PlaceResult r);
```

`ReadModelSize` muss dabei die **gelesenen Werte** mit ausgeben, auch wenn sie
unplausibel sind — sonst bleibt offen, ob dort Müll oder eine echte Größe steht.

**b) Vorher-Zeile mit allen neun Argumenten, geflusht**

Wie in `Probe()` (`NativeHouseProbe.cpp:946-951`) bereits richtig gemacht, aber
für den Dorfbau bisher nicht vorhanden. Ohne die Zeile ist nach einem
`0xC000041D` nicht rekonstruierbar, welcher Aufruf es war:

```cpp
std::cout << "[Bau] -> placeStructure tmpl=0x" << std::hex << (uint32_t)(uintptr_t)tmpl
          << " zone=0x" << (uint32_t)(uintptr_t)zone << std::dec
          << " idx=0x" << std::hex << modelIndex << std::dec
          << " origin=(" << origin[0] << "," << origin[1] << "," << origin[2] << ")"
          << " groesse=" << w << "x" << h
          << " rot=" << (rot & 3) << " kind=" << (int)a.kind << " mode=" << a.mode
          << " flag=" << (int)a.flag << " log=" << (void*)a.log << std::endl;
std::cout.flush();
```

**c) Ausnahmecode und Fehleradresse ausgeben, nicht nur „Ausnahme"**

`EXCEPTION_EXECUTE_HANDLER` verwirft die Information. Ein Filter behält sie:

```cpp
static DWORD g_lastExCode; static void* g_lastExAddr;
static int PlaceFilter(EXCEPTION_POINTERS* ep) {
    g_lastExCode = ep->ExceptionRecord->ExceptionCode;
    g_lastExAddr = (void*)ep->ExceptionRecord->ExceptionInformation[1]; // Zugriffsadresse
    return EXCEPTION_EXECUTE_HANDLER;
}
// ... __except (PlaceFilter(GetExceptionInformation())) { ... }
```

Bei F1 hätte diese eine Zeile sofort `0xC0000005 auf 0x00000019` gemeldet — und
`0x19 = 1 + 0x18` weist unmittelbar auf `param_9`.

**d) Erfolgskontrolle nach dem Aufruf**

Weil `placeStructure` bei fehlender Zonenüberlappung still zurückkehrt (F8), ist
„keine Ausnahme" kein Beweis für „gesetzt". Nach jedem Gebäude eine Stichprobe:

```cpp
// Wie viele nicht-leere Bloecke stehen jetzt im Grundriss?
int solid = CountSolid(gc, cx, cy, cz, w, h);
std::cout << "[Bau] " << solid << " Bloecke im Grundriss (vorher " << before << ")"
          << (solid <= before ? "  <-- NICHTS GESETZT" : "") << std::endl;
```

`InspectBuilding` (`NativeHouseProbe.cpp:452-476`) macht bereits fast genau das,
wird aber nur von `CycleArgs` gerufen, nicht vom Dorfbau.

**e) Teilerfolg sichtbar machen**

Stürzt der Aufruf mitten im Modell ab, stehen bereits Blöcke in der Welt. Die
Meldung „FEHLGESCHLAGEN" ist dann irreführend. Die Blockzählung aus (d) trennt
„gar nichts" von „halbes Haus".

---

## 4. Referenzstellen im Reversal

| Was | Datei:Zeile |
|---|---|
| Signatur `placeStructure` | `cube/world/WorldInfo.cpp:5099` |
| Zonen-Überlappungstor | `cube/world/WorldInfo.cpp:5306-5340` |
| `w`/`h` aus `tmpl+0x44/+0x48`, interner Achsentausch | `cube/world/WorldInfo.cpp:5292-5302` |
| Min/Max-Z je Spalte (`local_6cc`/`local_6c8`) | `cube/world/WorldInfo.cpp:5395-5429` |
| `param_9` als Liste (1) | `cube/world/WorldInfo.cpp:5659-5672` |
| `param_9` als Liste (2) | `cube/world/WorldInfo.cpp:5816-5827` |
| `param_9` als Liste (3) | `cube/world/WorldInfo.cpp:5964-5977` |
| `mode`-Weißliste, sonst Markerblock übersprungen | `cube/world/WorldInfo.cpp:5641-5645` |
| Markerzweig `mode == 0x0D`, Objekt `0x4D` | `cube/world/WorldInfo.cpp:6375-6402` |
| `switch(mode)` für Spawns, Fälle 6..0x0F | `cube/world/WorldInfo.cpp:6453-6513` |
| Zweige `mode == 0x0D` / `== 0x0E` | `cube/world/WorldInfo.cpp:6713`, `6746`, `6816`, `6849` |
| Modellwahl `0x900..0x905` | `cube/game_misc/game_misc.cpp:28656-28683` |
| Moduswahl `0x0A..0x0F` zum Modell | `cube/game_misc/game_misc.cpp:28722-28742` |
| Aufruf Klassenhäuser, `z = top - 7`, `param_9 = 0` | `cube/game_misc/game_misc.cpp:28744-28757` |
| Aufruf Tempel `0x84C`, zentriert, `param_9 = 0` | `cube/game_misc/game_misc.cpp:28944-28971` |
