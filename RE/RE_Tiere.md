# RE: Tiere, Rassen, friedlich/feindlich, Namensschild, natürlicher Spawn

Stand 2026-10-19. Alle Adressen gelten für Cube.exe (Basis 0x400000). Im Mod werden sie über `Host::Re()` umgerechnet.

**Kurzfassung:**
- Ob ein wildes Tier von sich aus angreift, entscheidet allein `creature_isPassive` (0x444680). Die Funktion prüft die Rasse gegen eine feste Liste und Flags7E & 0x1A00.
- Den Namen über dem Kopf holt das Spiel in zwei Schritten: Rassentabelle (0x59AA60), dann Sprachtabelle (0x480E00).
- Eigene Rassen (≥ 200) brauchen beides. Der Mod hängt sich an beide Stellen.

---

## 1. Kreatur – wichtige Felder

| Offset | Bedeutung | Beleg |
|---|---|---|
| +0x08 | GUID (u64) | Entity-Map |
| +0x10/+0x18/+0x20 | Position (int64, 65536 = 1 Block) | |
| +0x60 | Feindlichkeit (Byte): 0 Spieler, 1 wild/Monster, 3 NPC (Vorname), 5 Begleiter/Haustier, 6 Objekt/Pflanze | `CombatBehavior_isHostileTo` 0x596CA0, `World_areEntitiesHostile` 0x5988A0 |
| +0x64 | Rasse (int) | |
| +0x68 / +0x6C | Aktion / Aktionszeit | 0x33 weghüpfen, 0x4B Biss/Unbewaffnet, 0x06 Faustschlag |
| +0x78 … +0x123 | Aussehen (0xAC Byte, POD) = Spawn+0x74 | Kopie 0x459800 |
| +0x7E | Aussehens-Flags (u16, siehe 3.) | |
| +0x80/+0x84/+0x88 | Größe / Kollision | |
| +0x124 | Zustands-Flags (0x04 bewegt, 0x40 im Kampf, 0x20 Aggro) | |
| +0x16C | HP | |
| +0x190 | Level | |
| +0x198 | Besitzer-GUID (Haustier) | |
| +0x1168 | Name[16] (leer = Rassenname) | Schild 0x49A887 |
| +0x1178 | Buff-Liste (Typ-Byte an Knoten+8) | |
| +0x1190 | Schleichen (0..1) | |
| +0x1350 | Anzeigeposition (int64 × 3), für Namensschilder | Schleife 0x490920 |
| +0x13E4 | Verhaltensbaum (Behavior*) | |
| +0x1D2C | Leine/Aktivierungsradius (= Spawn+0x08) | |
| +0xDF0 | Lampen-Platz (Item, Seltenheit +0xDFC) | Lampenlicht 0x4AF86B |

Aussehen relativ zu +0x78 (Generator 0x43F7C0):

| Offset | Bedeutung |
|---|---|
| +0x08..+0x10 | Kollision |
| +0x14..+0x22 | Modell-IDs (Kopf, Haare, Hände, Füße, Körper, Schwanz, Schulter, Flügel) |
| +0x24 / +0x28 / +0x2C / +0x30 | Skala Kopf / Körper / Hände / Füße |
| +0x3C / +0x40 / +0x44 | Skala Schwanz / Schulter / Flügel |
| +0x64 / +0x70 / +0x7C / +0x88 | Position Körper / Kopf / Hände / Füße (vec3) |
| +0x94 | vec3, vom Generator nie gesetzt |
| +0xA0 | Position Flügel |

## 2. Verhaltensbaum wilder Tiere

**Aufbau:** Spawn → Kreatur in `GameController_updateSubsystems` (0x60FFDB: operator new 0x1E60, Kreatur-ctor 0x43B690). Ohne eigenes Verhalten (Spawn+0x109C = 0) baut das Spiel diesen Baum:

`Sequential{ Combat, LookAtPlayer, RandomWalk }`

- **Combat:** 0x6FD310, ctor 0x42C550 (+4 Zeit 20000, +8 Radius 20, +0x10 Sperrzeit)
- **LookAtPlayer:** 0x702D60
- **RandomWalk:** 0x7039E8. Entfällt bei Feindlichkeit 6, bestimmten Klassen 0x80..0x87 und Flags7E & 0x40.

Weitere Knoten: Companion 0x6FD868, RandomInteraction 0x7039D0, Sequential 0x703A00, SpawnLocation 0x703B58, WalkPath 0x71A770.

`CombatBehavior::vfunc_0` (0x42CB20) – Zielsuche:

1. Gibt es eine Bedrohung (`combat_findTopThreat`), wird zurückgeschlagen. Das gilt für alle Tiere, auch friedliche.
2. Sonst werden Kreaturen im Radius (+8)² geprüft. Der Abstand wird dabei mit Schleichen und Levelunterschied skaliert: `1 - 1.5^(LvZiel-LvSelbst) * Ziel+0x1190`, mindestens 0.1.
3. **0x42D171:** Ist `creature_isPassive(self)` wahr, wird ein Spieler nur dann Ziel, wenn er Aktion 0x52 ausführt (Futter halten) und `Item 0x14` mit `Unterart == Rasse` trägt. Das ist das **Zähmen**.

## 3. `creature_isPassive` 0x444680 (Ghidra: „ability_isChanneled")

- thiscall(ecx = Kreatur), Ergebnis in AL.
- Rückgabe 1, wenn `(Flags7E & 0x1A00) == 0` **und** die Rasse in dieser Liste steht:
  - Collie 19, ShepherdDog 20, Alpaca 22, BrownAlpaca 23, Turtle 25, Terrier 26, ScottishTerrier 27
  - Cat 30, BrownCat 31, WhiteCat 32, Pig 33, Sheep 34, Bunny 35
  - Hornet 53, Crow 55, Chicken 56, Seagull 57, Parrot 58, Peacock 67, Frog 68
  - Duckbill 74, Mole 87, Koala 89, Owl 92, Penguin 93, Horse 98, Cow 100, Crab 106, SeaCrab 107
  - SapphireFish 145, LemonFish 146, Seahorse 147
- Wird auch in `isHostileTo` (0x596CA0) und `areEntitiesHostile` (0x5988A0) benutzt. Zwei Kreaturen mit Feindlichkeit 1 sind einander feind, wenn sich ihre Passivität unterscheidet. Deshalb greifen Wölfe Kühe an.

**Gemessen (Scan 2026-10-19):**
- Kuh (Flags 0x31), Hase (0x30), Krähe/Hornisse (0x32): friedlich
- Wolf (0x31), Borkenkäfer (0x31): aggressiv

Die Flags entscheiden also nicht.

**Aussehens-Flags (Kreatur+0x7E / Spawn+0x7A), soweit belegt:**

| Bit | Bedeutung |
|---|---|
| 0x01 | ? |
| 0x02 | kann fliegen. Mit 0x10 zusammen: im Kampf auffliegen bzw. weghüpfen (Aktion 0x33) |
| 0x08 | Nahkämpfer |
| 0x10 | unbewaffnet → Angriff 0x4B (`combat_selectAttackAnim` 0x445020) |
| 0x20 | ? |
| 0x40 | kein RandomWalk |
| 0x100 | Wassertier (alles außer Wasser ist fest) |
| 0x200 | Namensschild „rarename“ (Generator-Vorname) |
| 0x1A00 | nie passiv |
| 0x4000 / 0x8000 | im Spiel unbenutzt → Mod-Marken „friedlich“ / „feindlich“ |

**Mod:** Hook auf 0x444680 (`MobLab::Hook_IsPassive`), Prolog `0F B7 41 7E`. Reihenfolge:
1. Flags & 0x1A00 → wie im Spiel
2. 0x8000 → nicht passiv
3. 0x4000 → passiv
4. Rasse 200..511 → `friedlich=1` in der .mob (bzw. alte Feindlichkeit 2/3/4)
5. sonst wie im Spiel

Setzen über `MobLab::PeacefulFlags` / `HostileModFlags`: Biome-II-Regeln, LAB-Spawn, Stadtgehege (`SetPeaceful`).

## 4. Namensschild

**Ablauf** (0x49A887..0x49AE3E, jedes Bild für Kreaturen, deren +0x1350 näher als 3600 liegt):
1. Name (+0x1168) gesetzt → als Eigenname anzeigen.
2. Sonst: Flags & 0x200 oder Feindlichkeit 3 → Vorname aus 0x5A0ED0 („rarename“).
3. **Rassenwort:** `World_getObjectName` 0x59AA60 = thiscall(World = GC+0x2E4, out, rasse). Liest `std::map<int, wstring>` mit Kopf bei World+0x800104 (Knoten: Schlüssel +0x10, wstring +0x14). Gefüllt im World-ctor 0x58EE44..
4. **Anzeigetext:** `lookupNestedStringMap` 0x480E00 = thiscall(GC+0x314, wstring* out, const wstring* wort, wstring form – als Wert, „singular“), ret 0x20. Ein unbekanntes Wort ergibt "".
5. CreatureLevelDisplay.dll springt bei 0x49AE36 ein und setzt „LV n “ davor.

**Mod:**
- Rassenwort eintragen: `MobLab::RegisterRaceName` mit map operator[] 0x4E2DF0 und wstring assign 0x40F7A0. `RaceNameTick` prüft alle 10 s alle .mob-Rassen.
- Sprachtabelle: Hook auf 0x480E00 (`Hook_Lookup`). Ist das Ergebnis leer und das Wort ein eigener Rassenname, wird das Wort selbst zurückgegeben.
- Mod-Kreaturen (GUID 0xCAFECAFE…) bekommen +0x1350 = +0x10 (`ModCreatureTick`).

## 5. Natürlicher Spawn (Zonen)

- **Zone+0x18 = `vector<cube::Spawn*>`:** push_back 0x66ADD0 (z.B. 0x5EB4D4), Größe → Zone+0xA0 bei 0x5EDD53.
- **Wilde Tiere:** `WorldInfo_generateBiomeContent` 0x5E4850. Raster 3×3 je Zone (0x5EAF75..0x5EBBC7: imul 0x55, cmp 3). Der Mod macht daraus 4×4 bis 6×6 (`BiomeMod::SetSpawnDensity`).
- **Rasse je Ort:** 0x5F56C0 (BiomeMod-Hook „Rasse“).
- **Anker:** Aufruf 0x5EDE84 (`call 0x59A0E0`, Zone = edi = [ebp-0x1308]) am Ende der Zonenerzeugung → `Hook_SunCall` → `Wildlife::OnZoneSpawns`.

**Spawn-Datensatz (`cube::Spawn`, 0x10F0 Byte):**

| Offset | Inhalt |
|---|---|
| +0x08 | Leine |
| +0x10 | Position |
| +0x28 | Feindlichkeit |
| +0x2C | Rasse |
| +0x30/+0x31 | Klasse / Spezialisierung |
| +0x34 | Level |
| +0x48 | GUID |
| +0x54 | Drehung |
| +0x74 | Aussehen (0xAC) |
| +0x120 | Ausrüstung |
| +0xF58 | Wertefaktoren (0x14) |
| +0x109C | eigenes Verhalten |
| +0x10CC | Name |

Ctor 0x5D7990, Aussehen erzeugen 0x43F7C0, Ausrüster 0x5C7730.

**Mod (Wildlife.cpp):**
- Gruppen gleicher Rasse innerhalb von 14 Blöcken werden zusammengefasst. Nur Tier/Wasser bzw. Monster, wenn ein eigenes Wesen mit gleicher Basisrasse bereitsteht (Kull ↔ Oger, Titanen ↔ Kyklop).
- Biome-II-Regeln (`.biom`, `_nativ.tiere`): `spawn=was;wo;gewicht;lvmin;lvmax;verhalten`. Rassen-Vorlage über `MobLab::MakeRaceTemplate` (Aussehen + Klasse + Faktoren, POD).
- Sonst Tierwelt: gleiche Basisrasse, Lebensraum ×6 bevorzugt.
- **Auf den Datensatz geschrieben:**
  - Aussehen (`MobLab::ApplyToSpawn`)
  - Rasse ≥ 200
  - Körperbau (0x43F7C0-Hook bzw. `ApplyBodyRaw`)
  - Verhalten (Mod-Marken)
  - Level (+0x34)
- Tiergruppen: Spalte 7 in `_tierwelt.txt` (`Werkzeuge\Mobs\tiergruppen.py`).

## 6. Körperbau je Rasse

- Hook auf `creature_generateAppearance` 0x43F7C0 (cdecl(&rasse, &aussehen, seed)). Danach werden die Felder aus `Strukturen\Mobs\_koerperbau.txt` überschrieben (`rasse;feld;wert[,wert,wert]`).
- Eigene Rassen (die 0x43F7C0 nie durchlaufen): `ApplyBodyRaw` in `ApplyToSpawn` und `SpawnCustomAt`.
- LAB: Mobs → „Körperbau der Rasse“.

## 7. Zähmen / Haustier

- Futter = Item 0x14 mit Unterart = Rasse (Byte). Rassen > 255 sind daher nativ nicht zähmbar.
- Haustier: Feindlichkeit 5 + Besitzer +0x198, Spieler+0x11C8 = Haustier-GUID. Companion-Knoten 0x6FD868 (init 0x5EE7A0).
- **Entlassen:** `GameController_updateSubsystems`, 0x60EAB1–0x60EB0B, läuft jedes Bild für jede Kreatur mit Feindlichkeit 5.
  - Besitzer = Lookup 0x42F000(+0x198).
  - Ist Besitzer+0x11C8/+0x11CC ≠ ihre GUID: `0x446330`, Leben (+0x16C) = 0, Feindlichkeit 3, +0x6C = 5000, Besitzer = 0.
  - Ein Spieler kann also nativ nur EINEN Begleiter halten.
- **Haustier-Gegenstand:** Spieler+0x1020 == 0x13 (0x60E492ff.).
  - Ist der Platz leer, übernimmt das Spiel die erste Kreatur mit Feindlichkeit 5 und passendem Besitzer.
  - Sonst legt es aus dem Gegenstand ein neues Haustier an (0x60E7E3).
- **Mod:** Code-Höhle an 0x60EAC7 (22 Bytes) fragt zusätzlich 16 unsichtbare Plätze ab (`Skills.cpp` `GhostPetC`), z. B. für die Totenbeschwörung.

## 8. Speicher

Kreaturen müssen mit dem operator new des Spiels angelegt werden (0x68D668, msvcr110). Das Spiel löscht sie selbst. Ein eigenes `new` führte zum Heap-Absturz vom 16.10.

## 9. Ausrüstung, Spezial-Platz, Inventarfenster (2026-10-27)

**Ausrüstung:** Kreatur+0x300, 13 × Item (0x118, reine Daten: `cwsdk/non_cube.h` Item).

| Platz | Offset | Kategorie |
|---|---|---|
| Lampe | +0xDF0 | 0x18 |
| Spezial | +0xF08 | 0x17; Unterart +0xF09: 0 Gleitschirm, 1 Boot |
| Haustier | +0x1020 | 0x13 |

**Mauszeiger:** Anzahl +0x11E8, Item +0x11EC. Inventar: +0x11DC; einfügen mit 0x46EBE0(inv, item*, -1).

**Taste [G]:** Tastentabelle 0x47E95C (Taste − 9), Sprungtabelle 0x47E92C. Fall 5 = 0x47E83C (Spezial-Gegenstand).
- Gleitschirm: Flag 0x10 in +0x124 (in der Luft).
- Boot: nur mit Physikflag +0x5C & 2 (schwimmt). Aktion +0x68 = 0x6B (segeln), erneutes G = absteigen.
- Weitere Fälle: F = Lampe (0x47E811), X = Skills, M = Karte, B/I = Inventar.

**Inventarfenster** (GC+0x8008F0 = offen):
- 12 Ausrüstungsfelder: Widgets GC+0x800A34, Feld-Elemente GC+0x800A28.
- Reihenfolge: 0x990, 0xAA8, 0xBC0, 0xCD8, 0x418, 0x878, 0x530, 0x760, 0x648, 0xDF0, 0xF08 (Index 10), 0x1020.
- Aufbau im GC-ctor 0x460A20..0x460B3D, je Feld:
  1. `0x64F4E0(gui, 0,0,0,0, L"")`
  2. `new 0x16C` + `0x51C310(gui, base, gc)` = Element
  3. Vorlage `0x633D70(root, L"equipmentboxleft"/"equipmentboxright")`
  4. `0x6326D0(Vorlage, root)` = Klon
  5. `0x631460(Klon, L"background", Element, 1)`
- Jedes Bild (0x48C7FC..0x48CBB4): Element+0x160 = Symbol `0x4EC400(gc+0x300, item*)`, Element+0x164 = item*.
- Maus über einem Feld: 0x47B1B0 (Hover 0x6294C0 je Element) → Item-Zeiger.
- Klick-Tausch mit dem Mauszeiger: 0x47C0E2..0x47C4D3 (Kategorie-Prüfung je Feld). Danach Anzahl = Kategorie ≠ 0 ? 1 : −1, Klang 0x484350(0x57/0x58).
- Rechtsklick im Inventar legt an: 0x47D4BD (Kategorie 0x17 → +0xF08).

## 10. EntitesMap-Schlüssel und Datenbanken (2026-10-27)

**EntitesMap** (World+4): `std::map<int64_t, Creature*>`. Die Suche 0x477D90 vergleicht das hohe Wort mit Vorzeichen (jg/jl) und das niedrige ohne (jae/jb). Eigene Suchen müssen deshalb `int64_t` vergleichen.

**Datenbankklasse des Spiels** (Objekt +4 = sqlite3*):

| Adresse | Aufruf |
|---|---|
| 0x4497B0 | Öffnen (`CREATE TABLE blobs(key TEXT PRIMARY KEY, value BLOB)`) |
| 0x449810 | Lesen |
| 0x4499F0 | Schreiben (Update oder Insert) |
| 0x449720 | Löschen |
| 0x449920 | Umbenennen |

**Charakter-DB:** GC+0x1001008 (sqlite3* bei GC+0x100100C), geöffnet im GC-ctor 0x464EFC mit „Save/characters.db“.

**SQLite 3.7.15.2 in Cube.exe** (cdecl):

| Adresse | Funktion |
|---|---|
| 0x525590 | open |
| 0x520620 | close |
| 0x520760 | exec |
| 0x526480 | prepare_v2 |
| 0x526B60 | bind_text |
| 0x5267A0 | bind_blob |
| 0x527300 | step |
| 0x528070 | finalize |
| 0x527590 | column_blob |
| 0x527610 | column_bytes |

## 11. Zusammenbau, KI-Felder, plasma-Fenster (2026-10-27)
- Boden in Modell-Einheiten = -5.6 * size.z / size.x (Position = Mitte der Kollisionshöhe; geprüft an Rassen 0, 2, 19, 22, 28, 151: Fußunterkante ±0.8).
- Aussehen-Positionen ab Kreatur+0xDC: Körper, Kopf +0xE8, Hände +0xF4, Füße +0x100, **Schwanz +0x10C**, Flügel +0x118 (Infinite-Cubes `Creature.h`, Nachbarn belegt).
- Kreatur: velocity +0x34, hostilityType +0x60, race +0x64, currentAbility +0x68 (uchar), timeSinceAbility +0x6C, Aussehen +0x78, Breite +0x80, Höhe +0x88, Leben +0x16C. ForwardRoll = 51, Bite = 75 (Nahkampftreffer, WorldUpdate.cpp:1743).
- Engine = GC+0x800710 (WndProc 0x4C83F0: [0x76B1CC] Engine, [0x76B1C8] Spiel). Fokus-Widget Engine+0xCC (0x6531E0), aktiv Widget+0x15C. characterEntered 0x652710, keyPressed 0x652730. GC-vtbl 0x6FFDC8: +8 handleKeyDown 0x47E1B0 (prüft Fokus 0x47E27C), +0x14 onKeyTyped 0x482A00. ChatWidget GC+0x800A14, Zeile aktiv +0x180.
- Feld wie Systemmenü (0x464A00..): instance 0x636040, transformation() 0x411C70 / display() 0x40F8E0 / widget() 0x411CF0, setTransformation 0x636B70, setDisplay 0x6368E0, resizeTo 0x62C570, moveTo 0x62A650 (x, y, verzögert), viewWidth 0x62F600, getViewHeight 0x6291D0, setVisible 0x411A90. m_blackWidget GC+0x8008C8, m_systemPanel GC+0x800910, m_worldNameNode GC+0x8009BC. setText(name, text, rek.) 0x636A00, getText(name, rek.) 0x635550 (Rückgabe per verstecktem Zeiger). Fenstergröße GC+0x11C/+0x120.
