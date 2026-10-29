# Karte freilegen, Minimap agiler, Spiel flüssiger

> **NACHTRAG 2026-10-06: Minimap wird Zone für Zone schwarz.** Diesen Fehler gab es schon beim Bergdorf.
>
> Was damals half, steht in `MountainVillage.cpp`:
>
> 1. Nicht alle Chunks auf einmal neu vermaschen, nur den Bereich um die Änderung.
> 2. Nur bauen, wenn der Spieler nah ist (unter 250 Blöcke) und die ganze Fläche geladen ist. Ein `SetBlock` in eine Zone am Streaming-Rand machte die Karte schwarz.
>
> Der BiomeMod hielt sich an Regel 2 nicht. Er dekorierte jede Zone bis 900 Blöcke vom Spieler (Pflanzen, Hangbäume, Dungeons, Minen), also genau am Streaming-Rand. Mit den Hangbäumen sind es deutlich mehr Schreibvorgänge je Zone.
>
> Neu belegt (capstone):
>
> - Der Kartenthread hält beim Erzeugen jeder Kartenkachel die Sperre **World+0x8000D8**: `0x469B62` → `streamTileChunk 0x469B75` → Freigabe `0x469B82`.
> - Der Chunk-Thread nimmt erst `+0x8000C0`, dann `+0x8000D8` (`0x46AF84`, `0x46AF91`).
> - Der Mod nahm bisher nur `+0x8000C0` (`World::Lock`), bei Pflanzen und Dungeons gar keine Sperre. Der Kartenthread konnte also Säulen lesen, während der Mod sie schrieb.
>
> Geändert:
>
> - **BiomeMod:** Eine Zone wird nur noch bearbeitet, wenn ihre Mitte höchstens 320 Blöcke vom Spieler liegt und sie samt allen 8 Nachbarzonen geladen ist. Die nächste kommt zuerst. Läuft der Spieler weg oder wird ein Nachbar entladen, geht die Zone zurück in die Warteschlange.
> - **Sperren:** Jedes Setzen (Pflanzen, Hangbäume, Dungeons, Minen) läuft jetzt unter beiden Sperren, `+0x8000C0` und dann `+0x8000D8`. Das gilt auch für alle Schreibstellen des Bergdorfs (`SafeLockWorld`).
> - **Folge:** Pflanzen und Bäume erscheinen erst, wenn man sich einer Zone bis etwa 320 Blöcke nähert.
> - **Ungeprüft im Spiel:** Ob die Sperre allein schon reicht, ist nicht getrennt getestet. Beide Maßnahmen sind aktiv.


> **STAND 2026-10-03 21:10: Kartennachladung des Mods ist AUS.** Derselbe Absturz kam ein drittes Mal, nach etwa 3 Minuten Spielzeit, mit Byte für Byte demselben Stapel (bad_alloc im Kartenthread). Deshalb sind Kartenfenster, Kartenthread-Takt und Landschaftsfenster jetzt hart abgeschaltet (`kMapThreadOff` in `PerfTweaks.cpp`). Diese Bytes werden nicht mehr angefasst.
>
> Weiter aktiv bleiben:
>
> - „Abdunklung aus“: reine Darstellung;
> - der Chunk-Thread-Takt;
> - der Bildtakt.
>
> **Bildtakt:** Er war wegen ASLR bisher nie aktiv („Stelle passt NICHT“). Jetzt wird die verschobene IAT-Adresse erwartet.

> **NACHTRAG 2026-10-03 20:30: derselbe Absturz trotz Landschaft ±3.** Der Aufrufstapel ist Byte für Byte derselbe wie am 01.10. (Rücksprünge geprüft):
>
> `0x46995E` (Aufruf `0x469959` → `loadLandscapeTile 0x6024D0`) → `0x602FD8` (→ `VoxelModel_build_mesh 0x4E7870`) → `0x4E8916` (→ `0x486830`) → `0x48689E` (→ `0x46E8F0`) → `0x46E57E` (→ `operator new 0x68D668`) → C++-Ausnahme.
>
> Der Radius allein ist also nicht die Ursache: Nach langer Spielzeit ist der **2-GB-Adressraum** voll, und die Landschaftskachel ist nur die Anforderung, die zufällig scheitert. Gegenmaßnahmen:
>
> - **Logger:**
>   - Der Bericht nennt jetzt den C++-Typ (liest `ThrowInfo` aus dem Prozess, z. B. `.?AVbad_alloc@std@@`).
>   - Er zeigt eine **Adressraum-Zeile**: belegt, frei, größter freier Block.
>   - Er gibt bei Speichermangel eine Deutung aus.
>   - Neu ist der Knopf **„4 GB freischalten“**. Er setzt nach Spielende das Large-Address-Aware-Bit in genau der Cube.exe, die lief. Vorher legt er `Cube.exe.vor-4GB.bak` an, und ein erneuter Klick nimmt es zurück.
> - **Im Spiel:** Ein Wächter-Thread prüft alle 3 s den Adressraum. Ist er knapp (größter Block < 96 MB oder frei < 256 MB), wird das Kartenfenster auf Vanilla ±10 begrenzt, und das Protokoll meldet es. Das LAB zeigt die Werte unter „Karte & Leistung“.
> - **Ungeprüft:** Ob Cube World mit LAA in jedem Fall stabil läuft, ist nicht getestet. Spielcode, der Zeiger als Vorzeichenzahl vergleicht, ist nicht ausgeschlossen. Der Mod selbst rechnet mit `uint32_t`.

> **KORREKTUR 2026-10-01 — Absturz durch das Landschaftsfenster.** Mit ±5 statt ±3 Zonen
> stürzte Cube World ohne Meldung ab. Der Logger-Bericht belegt die Kette:
> Kartenthread `World_streamChunksAroundPlayer` (Aufruf `0x469959`, Rücksprung `0x46995E`)
> → `loadLandscapeTile @0x6024D0` → `VoxelModel_build_mesh @0x4E7870` → Vektor-Reallokation
> → `operator new @0x68D668` wirft `std::bad_alloc` (C++-Ausnahme `0xE06D7363`, nicht gefangen).
> Ursache: Cube.exe ist **nicht Large-Address-Aware** (Characteristics `0x0102`), hat also nur
> **2 GB** Adressraum. Bei ±5 sind es 121 statt 49 Landschafts-Meshes. Standard ist jetzt wieder
> **±3** (Vanilla), das Maximum ist 4. Eine alte Einstellung (Version 1) wird beim Start
> zurückgesetzt. Das Kartenfenster ist vorsorglich von ±20 auf **±16** gesenkt, weil jede Kachel
> ebenfalls ein Mesh ist.

**Die blaue Kartenverdeckung ist kein Nebel, sondern ein Platzhalter für fehlende Kartendaten.** Das Spiel erzeugt Kartendaten nur in einem Fenster von **±10 Kacheln (±40 Blöcken)** um den Spieler, und zwar in einem eigenen Hintergrundthread. Alles außerhalb bleibt blau, auch wenn die Welt dort längst geladen ist. Das Modul `PerfTweaks` vergrößert dieses Fenster, macht den Thread in Bewegung schneller und schaltet die zusätzliche Abdunklung unentdeckter Kacheln ab. In Schritt 2 kommt ein exakter Bildtakt dazu, ein schnelleres Chunk-Streaming in Bewegung und eine Messung, welcher Mod-Baustein wie viel Bildzeit kostet.

Alle Stellen sind **byte-genau gegen die vorliegende `Cube.exe` geprüft**: mit capstone disassembliert, Originalbytes verglichen, und jedes Muster kommt in `.text` genau einmal vor. Vor jedem Schreiben vergleicht der Mod zur Laufzeit erneut. Weicht eine Stelle ab, bleibt ihre Gruppe aus und es steht im Protokoll. Alles ist im LAB unter **„Karte & Leistung …"** umschaltbar und vollständig zurücknehmbar („Vanilla-Werte" oder „aktiv" abhaken).

---

## Schritt 1 — Karte und Minimap

### Woher das Blau kommt

Der Kartenrenderer `WorldMap::render @0x5FC1B0` (im Reversal fälschlich `GameController_processNetworkTick`) zeichnet je Kartenkachel:

| Zustand der Kachel | Darstellung | Beleg |
|---|---|---|
| Kartendaten vorhanden, entdeckt (`ZoneTile+0x30 & 1`) | echte Geländefarbe | — |
| Kartendaten vorhanden, **nicht** entdeckt | Geländefarbe **× 0,6** (abgedunkelt) | `0x5FEA03`: `test byte [esp+17],1; jne` → `mulss` mit `[0x6FD8DC] = 0.6` |
| **keine** Kartendaten (`tile == 0`, kein Sprite `+8`, oder Einblendung `+0x2C`) | Modell **`maptile.cub` (0xA07)** in **Blau** (RGB 0,2 / 0,3–0,4 / 1,0) | `GameController.cpp:90641–90660`, Konstanten `0x3E4CCCCD` / `0x3E99999A` |

Die Kartendaten entstehen in **`World_streamChunksAroundPlayer @0x469590`**, einem Hintergrundthread mit Endlosschleife:

```
Schleife:
  1. Landschaftskacheln (grobe Fernansicht) aus der Karten-DB laden, ±3 Zonen     0x4696C0..
  2. zehnmal:  im Fenster ±10 Kacheln die NÄCHSTE Kachel ohne Daten suchen,
               streamTileChunk @0x603A00 erzeugt sie aus den geladenen Weltdaten   0x469996..
  3. Sleep(10)                                                                     0x469BCF
```

Eine Kachel ist 1/64 Zone, also 4×4 Blöcke. Daten gibt es nur dort, wo die Welt geladen ist (`streamTileChunk` braucht das Kachelfeld der Zone und `world_getRegionCell64`). **Wirklich nie besuchtes Land kann deshalb kein Mod aufdecken**, denn dafür existieren keine Daten.

### Was geändert wird

| Gruppe | Stelle(n) | Vanilla | Standard im Mod | Wirkung |
|---|---|---|---|---|
| Abdunklung aus | `0x5FEA03` `75 4A` → `EB 4A` | an | **aus** | unentdeckte, aber bekannte Kacheln in voller Farbe |
| Kartenfenster | `0x46999D`, `0x4699A0`, `0x4699CC`, `0x4699D2`, `0x469B39` (je disp8) | ±10 | **±20** (±80 Bl.), max. ±32 | Karte und Minimap füllen sich im doppelten Umkreis |
| Landschaftsfenster | `0x4696C0`, `0x4696C5`, `0x4696E5`, `0x4696E8`, `0x469849`, `0x46984C` | ±3 Zonen | **±5** | mehr gespeicherte Fernansicht auf der großen Karte |
| Kartenthread-Pause | `0x469BCF` `push 0Ah` | 10 ms | **4 ms in Bewegung, 15 ms im Stand** | schnelles Nachfüllen beim Laufen/Gleiten, sparsamer als Vanilla im Stand |

**Warum adaptiv.** Die Suche prüft in **jedem** Durchgang das ganze Fenster, auch wenn nichts mehr fehlt. Die Kosten wachsen also mit Radius² geteilt durch die Pause. ±20 mit 4 ms Pause wäre dauerhaft etwa 10-mal so teuer wie Vanilla. Deshalb gilt die kurze Pause nur, solange du dich bewegst: ≥ 2 Blöcke je 250 ms, also zügiges Laufen, Reiten oder Gleiten. Dazu kommt ein Teleport, und danach 3 Sekunden Nachlauf. Im Stand wird es sparsamer als Vanilla. Das LAB zeigt die geschätzte Last laufend als „Kartenthread-Last ca. x-fach Vanilla".

**Warum höchstens ±32.** Reicht das Fenster in eine Zone ohne Kachelfeld (nicht geladen), gilt dort jede Kachel als „fehlt". `streamTileChunk` kann sie aber nicht füllen. Die Suche nimmt dann immer wieder diese nächste, unfüllbare Kachel und verhungert dahinterliegende, füllbare.

**Sperren.** Jede einzelne Kachelprüfung nimmt und gibt zwei Sperren frei (Welt, Karte). Der Hauptthread wartet also höchstens die Dauer einer Prüfung, im Bereich unter einer Mikrosekunde. Ein größeres Fenster kostet Rechenzeit im Hintergrundthread, nicht Bildzeit.

---

## Schritt 2 — Flüssiger

### Bildbegrenzer: exakter Takt

Die Hauptschleife begrenzt die Bildrate so (`0x4C9437`):

```
verstrichen = timeGetTime() - Bildstart;              // ganze ms
if (verstrichen < cfg->minTimeStep) Sleep(cfg->minTimeStep - verstrichen);   // cfg = [0x76B1C8], +0x19C
```

`timeGetTime` und `Sleep` arbeiten in ganzen Millisekunden, und `Sleep(n)` kehrt nach n bis n+1 ms zurück. Die Bildabstände springen dadurch. Bei `minTimeStep 10` liegen sie zwischen etwa 10 und 12 ms, und das sieht man als feines Ruckeln. Weil kein VSync aktiv ist (`PresentationInterval = 0x80000000`), gleicht das auch nichts aus.

**Änderung:** Bei `0x4C9456` wird `call [Sleep]` (6 Byte) zu `call PreciseSleep; nop`. Das `push ecx` davor bleibt, und die Signatur ist dieselbe (`__stdcall`, ein Argument). `PreciseSleep` hält mit dem Hochleistungszähler einen festen Takt: Bis etwa 2 ms vor dem Ziel schläft es, den Rest wartet es aktiv. Nach langen Bildern rastet es neu ein. Das kostet etwa 2 ms aktives Warten je Bild auf dem Hauptthread.

**`minTimeStep` live:** Der Wert wird direkt in die Optionen geschrieben (`[0x76B1C8]+0x19C`, die einzige Lesestelle ist der Begrenzer). `16` ergibt 60 fps, `7` ergibt 144, `0` ist unbegrenzt, `-1` lässt den Wert aus `options.cfg`.

### Chunk-Streaming adaptiv

Das Vermaschen läuft im Hintergrundthread `0x4690A0` (Chunks nach Entfernung sortiert, `needs_update` an `Chunk+0x74`), mit `Sleep(5)` je Durchgang (`0x4694FE`). In Bewegung setzt der Mod die Pause auf **1 ms**, im Stand bleibt es bei 5 ms. Das Gelände baut sich beim Gleiten schneller auf.

### Was der Mod selbst falsch gemacht hat

`MountainVillage.cpp` warnt selbst: „Alles gleichzeitig neu zu meshen erzeugt einen riesigen Spike → schwarze Karte / Absturz". Trotzdem markierten **das LAB** (`RemeshAllChunks`) und die **Testwerkzeuge** (`NativeHouseProbe::RemeshAll`) bei jeder Aktion **alle** geladenen Chunks. Jetzt vermascht das LAB nur noch den Quader der Struktur plus 16 Blöcke Rand. Die Testwerkzeuge nehmen nur noch Chunks im Umkreis von 192 Blöcken um den Spieler.

### Messung

Im Fenster „Karte & Leistung", alle 0,5 s aktualisiert:

- **Bildzeiten** über die letzten 600 Bilder: fps, Median, 99 %, Maximum, Zahl der **Ruckler**. Als Ruckler zählt ein Bild, das länger als der doppelte Median und länger als 20 ms ist.
- **Kosten je Mod-Baustein:** Das Framework misst jetzt jedes `OnGameTick` einzeln (gleitender Mittelwert über ~20 Bilder). So sieht man direkt, ob ein Baustein bremst, statt es zu vermuten.
- der aktuelle Stand jeder Gruppe in der Exe und die geschätzte Last des Kartenthreads.

---

## Bewusst nicht angefasst

| Stelle | Warum nicht |
|---|---|
| `0x46A35F`: 20-ms-Takt in `World::updateActiveChunks` | Das ist sehr wahrscheinlich der Simulationstakt (50 Hz). Ändern würde das Spieltempo verändern. |
| `0x46B6A3`: `Sleep(20)` | gehört zu einer Netzwerk-/Serverschleife (nahe `networkReceiveThread @0x46B740`) |
| Glättung von Kamera und Minimap-Zoom (`0x428970` mit 0.025 / 1.6 / 0.8) | Welcher Wert was steuert, ist nicht belegt. `cameraSmoothness` in `options.cfg` ist die offizielle Stellschraube. |
| Nie besuchtes Land aufdecken | Dafür gibt es keine Daten. Dazu müsste die Welt dort generiert werden. |

---

## Ungeprüft

- **Im Spiel gelaufen ist nichts davon.** Geprüft sind die Bytes der Exe, die erzeugten Instruktionen (simuliert und disassembliert) und das Kompilieren (clang, 32-Bit-Windows-Header).
- Die **Lastschätzung** des Kartenthreads beruht auf ~80 ns je Prüfung (zwei ungestörte `EnterCriticalSection` plus Hash). Echte Werte zeigt der Taskmanager. Ist es zu viel, im LAB „Stand"-Pause erhöhen oder Radius senken.
- Ob die **Minimap** dieselben Kacheln nutzt wie die große Karte, ist belegt: Beide rufen `0x5FC1B0` auf (`0x4AE004` große Karte mit `param_10 = 1`, `0x4BABAF` Minimap). Ob der Minimap-Ausschnitt größer ist als ±80 Blöcke, ist nicht gemessen.
- **`PreciseSleep`** kostet je Bild bis zu 2 ms aktives Warten. Auf sehr schwachen CPUs kann „exakter Takt aus" die bessere Wahl sein.

---

## Absturz 2026-10-06 16:39: Klang-Aufräumen (AudioGuard)

Bericht: Zugriffsverletzung bei `Cube.exe+0x22346F` (Ghidra `0x62346F`), Lesen an `0x5BCF15FC`. Stapel: `0x48B369` → `0x4C8636` (Spielthread).

**Belegt:**

- `0x62346F` liegt in `cube::XAudio2Engine::vfunc_0 @0x623410`. Diese Funktion läuft einmal je Bild über die Liste `Engine+0x0C` (`std::list<IXAudio2SourceVoice*>`). Für jeden Eintrag ruft sie `GetState` (vtbl+0x64) auf, bei 0 Puffern `DestroyVoice` (vtbl+0x48) und nimmt den Knoten heraus.
- `EAX = 0x21465510` war das Klangobjekt, `ECX = 0x5BCF1598` seine vtable. Die vtable zeigte in nicht belegten Speicher: Das Klangobjekt war zerstört oder überschrieben.
- Das Spiel prüft weder hier noch beim Abspielen (`vfunc_6 @0x623610`) ein Ergebnis von XAudio2.
- Der Mod enthält keinen Klangcode.

**Unbelegt:** Die Ursache. Möglich sind ein Wechsel des Audiogeräts nach langer Untätigkeit (Monitor/Headset schläft, XAudio2 2.8 verliert das Gerät) oder fremde Speicherbeschädigung. Mit der `.dmp`-Datei kann man den Eintrag und die anderen Threads ansehen.

**Abhilfe `AudioGuard`:** Ein Hook ersetzt `0x623410` durch dieselbe Logik mit Prüfungen.

- **Plausibilitätsprüfung:** Objekt und vtable müssen lesbar sein, `GetState` und `DestroyVoice` müssen in ausführbarem Speicher liegen.
- **Absicherung:** Jeder Aufruf läuft unter SEH.
- **Kaputte Einträge:** Sie kommen aus der Liste, die Länge `Engine+0x10` wird mitgezählt. Der Knoten wird nicht freigegeben, ein Leck ist besser als ein Absturz.
- **Meldung:** Die Konsole meldet jeden Fund (höchstens 20).
- **Folge:** Statt abzustürzen fehlt schlimmstenfalls ein Klang. Fertige Klänge werden wie im Original zerstört und freigegeben (`operator delete @0x68D66E`).

## Ruckeln und langsames Laden nach dem Fluss-Hangtest (2026-10-06)

**Ursache:** Die Original-Höhenfunktion `0x5C5E20` ruft bei **jeder** Höhenabfrage selbst den Flussabstand `0x5F9340` auf (`0x5C60F1`). Seit dem Hangtest bekam der Flussabstand in Flussnähe zusätzlich 5 Höhen- und 12 Flussabfragen der Nachbarn. Zwischengespeichert war nur das letzte Ergebnis. So wurde jede Höhenabfrage nahe einem Fluss rund 20-mal so teuer. Am stärksten traf das den Zonen-Thread, also das Laden.

**Abhilfe:**

- `Hook_River` merkt sich je Thread 2 × 8192 fertige Ergebnisse. Der Schlüssel ist Welt, Samen, x, y, Biomsatz, Version und An/Aus. Bei einem Treffer entfällt auch der native Aufruf.
- Die Hangprüfung läuft nur noch, wo wirklich Wasser wäre (`r < 0.025`).
- Die Zwischenspeicher liegen jetzt pro Thread auf dem Heap statt als `thread_local`-Feld. Das Feld legte Windows für jeden Thread des Prozesses an und belegte Adressraum.
- **Kleine Pflanzen:** Das Spiel zeichnet jeden Eintrag einzeln (`0x4BD160`). Je Zone sind es jetzt höchstens 40 je Modell und 80 insgesamt, die Vorgabedichte ist 12.

## Messung 2026-10-06 17:23 (Logger „Leistung ausgeben“) und Abhilfe

**Messwerte:**

- **Zonenerzeugung:** im Mittel **1261 ms** je Zone (Max 2030 ms). Die Mod-Objekte davon kosten nur 1 ms.
- **Gelände-Hooks:** je Zone ~114 000 Höhen- und ~352 000 Flussabfragen, 66 % davon neu gerechnet.
- **Bildzeiten:** Median 14 ms, aber das 99-%-Perzentil liegt bei 1007 ms. Der Spielthread stand 10,7 s in einem d3d9-Wartezustand (`0x4C8702`). Die eigenen Mod-Bausteine kosten je Bild < 0,01 ms.

**Deutung:** Die Zeit geht in die Gelände-Hooks während der Zonenerzeugung. Jeder Hook rechnete Biomgewicht, Sternmassiv (~1 µs), See und Flüsse für dieselbe Säule neu, und das bei rund 7 Aufrufen je Säule. Die Ruckler von etwa 1 s passen zur Dauer einer Zone. Dass der Spielthread auf die lange Zonenerzeugung wartet, ist plausibel, aber nicht belegt.

**Abhilfe:**

- **Säulen-Speicher je Thread:** 65 536 Einträge, direkt über `(x&255, y&255)` abgebildet, also eine ganze Zone. Er hält Biomgewicht, Sternmassiv, See, Höhe und Flussabstand (normal und verschachtelt).
  - **Schlüssel:** Welt, Samen, Biomsatz, Version, An/Aus und eine Regionen-Epoche. Jede neue oder nachgefilterte Region macht den Speicher ungültig, weil Orte die Höhe ändern können.
  - **Kosten:** etwa 6 MB je Thread, der Gelände abfragt.
  - **Seespiegel-Proben** (`FormTarget`) laufen bewusst am Speicher vorbei, weil sie mit dem Biom der See-Säule rechnen.
- **`StarAt`:** Die festen Werte je Arm (Winkel, Talköpfe, Seen) werden je Thread gemerkt. Nur die 4 umliegenden Arme werden genau gerechnet. Das senkt die Kosten von 0,97 auf 0,41 µs je Aufruf, mit gleichen Ergebnissen (`star2`/`star3` unverändert).

## Ergebnis Fehlersuche 2026-10-06 abends: der AudioGuard war die Bremse

Mit dem Schalter im LAB ließ sich das Ruckeln eindeutig dem AudioGuard zuordnen. Er ist **komplett entfernt**: kein Hook mehr, nicht mehr im Projekt, kein Schalter mehr. `AudioGuard.cpp/.h` liegen nur noch im Ordner und können gelöscht werden.

Damit gilt für den Klang-Absturz vom 06.10. 16:39 wieder das Verhalten des Spiels. Tritt er erneut auf, braucht es einen anderen Ansatz und vorher den Minidump.

**Vermutete Ursache, nicht gemessen:** Der Ersatz rief je Bild und Klang mehrfach `VirtualQuery` auf. Unter dem angehängten Debugger und bei vielen gleichzeitigen Klängen ist das teuer. Außerdem kann `DestroyVoice` blockieren, bis der Audio-Thread seinen Durchgang beendet hat.

Die Hangprüfung der Flüsse ist wieder an (`kRiverCrossCheck = true`, nur in der Flussschleife).
