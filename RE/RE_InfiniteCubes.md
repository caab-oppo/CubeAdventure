# Infinite-Cubes (coremaze) – Abgleich mit dem AdventureMod (2026-10-27)

Ordner: `Downloads\Infinite-Cubes-main`. Für uns zählt `alpha/`: ein nachgebauter C++-Quelltext von Cube.exe und Server.exe (Alpha 2013-07-20), dazu die Plasma-Oberfläche. Grundlage ist `cube-world-alpha-re` (Stand 2026-10-26). Laut README sind rund 95 % geprüft.

## Ist der Mod kompatibel?

| Was | Ergebnis |
|---|---|
| Mit der ORIGINAL-Cube.exe | **Ja, unverändert.** Infinite-Cubes ändert keine Spieldateien. |
| Mit einer aus Infinite-Cubes GEBAUTEN Cube.exe | **Nein.** Der Mod hängt sich an feste Code-Adressen der Original-Exe (Hooks, Code-Höhlen, Byteprüfungen). Ein Neubau mit MSVC 2022 oder MinGW legt den Code ganz anders. Die Byteprüfungen schalten dann die meisten Bausteine ab, ungeprüfte MinHook-Stellen können abstürzen. |
| Datenstrukturen | Die Klassen sind feldgenau nachgebaut. `GameController.h` führt Lücken mit Original-Offsets (z. B. `unused800705[0x800710 - 0x800705]`). Unsere Daten-Offsets stimmen also. Spielstände (`Save/*.db`) sind dieselben Formate. |

## Was es uns bringt

1. **Lesbarer Quelltext mit Namen statt Ghidra-Ausgabe.** Die Logik ist direkt nachzulesen; Adressen sucht man danach im Binary, die Felder liegen gleich.
2. **Bestätigt** (nachgelesen):
   - Kreatur +0x11C8 = `m_companionId` (der eine Begleiter des Spielers).
   - Kreatur-IDs sind „signed“ (`std::set<long long>`); der Fix der Totenbeschwörung passt dazu.
   - G-Taste: `GameController::handleKeyDown`, Fall 'G' (Gleiter/Boot, `CreatureFlagInWater`).
   - O-Taste = `toggleSystemWidget`.
   - Ausrüstungsfelder: `m_equipmentSlots` (GC+0x800A28) und `m_equipmentBoxes` (GC+0x800A34).
     - Reihenfolge: Waffe L, Waffe R, Ring L, Ring R, Hals, Schulter, Brust, Hände, Füße, Licht, Spezial (10), Haustier.
     - Lage aus `resize`: x = W/2 − 200 + (i % 2) · 400, y = H/2 + (i / 2) · 56 + 10.
   - Datenbanken: `m_charactersDatabase` / `m_worldsDatabase` am Ende des GameControllers.
3. **Fehler im Boot-Platz gefunden:**
   - Der GameController-ctor löscht die Vorlagen „equipmentboxleft/right“ nach dem Klonen. `findNode` fand deshalb Kasten 0, den linken Waffenplatz mit seinem Bild.
   - Jetzt wird der Spezial-Kasten (Index 10) geklont, sein mitkopiertes Bild ausgeblendet und der Kasten „Boot“ beschriftet.
4. **Karte:** `MapOverlayWidget::draw` zeigt, wie Namen auf die Karte projiziert werden. Damit sind die Kartenmarker gebaut (MapMarkers.cpp):
   - WorldMap = GC+0x800D44 (m_projection +4, m_view +0x44, Matrix spaltenweise), m_mapOffset = GC+0x1000E4C, m_mapOpen = GC+0x8006E4.
   - Belegt durch Querverweise im Binary: 0x800D44 wird 34-mal benutzt, 0x800D48..0x800DC4 je 2-mal.
5. **Später möglich:** Teile des Mods direkt in einen eigenen Neubau übernehmen (keine Hooks mehr). Das wäre ein eigenes Großprojekt, denn der ganze Mod müsste umziehen.

## Vorgehen ab jetzt
- Neue Funktion: erst im Infinite-Cubes-Quelltext lesen, dann die Stelle im Original-Binary suchen (Strings, Konstanten, Feld-Offsets), dann hooken.
- Feld-Offsets aus den `unused…`-Lücken und der Reihenfolge der Header ableiten und immer am Binary gegenprüfen (Querverweise zählen).
