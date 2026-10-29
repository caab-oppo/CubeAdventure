# RE: Ein neues Biom registrieren und aufbauen

**BLUF:** Cube World Alpha hat **keine Biom-Tabelle**, in die man einen Eintrag schreiben könnte. Biome entstehen aus Klimazellen. Jede Zelle hat Temperatur, Feuchte, Höhe und **ein einziges Sonderbit**, und das ist das **Lava-Biom**. Rund 15 Generatorfunktionen lesen diese Werte und verzweigen über feste Schwellen. Ein neues Biom registriert man deshalb so, wie das Spiel sein Lava-Biom anlegt:

1. Im Zellerzeuger `0x5D7A70` je Zelle deterministisch entscheiden: „diese Zelle ist Biom X".
2. Eine eigene Gewichtsfunktion nach dem Muster von `0x5EF040` liefert 0..1 je Block.
3. Mit diesem Gewicht hängt man sich an die Bausteine, die das Lava-Biom auch nutzt.

Das Lava-Biom wird von **10 Stellen** ausgewertet (§3). Ein vollständiges neues Biom braucht dieselben 10 Bausteine.

**Achtung, Korrektur:** Im Ghidra-Reversal sind **Temperatur und Feuchte vertauscht** benannt (§1.3). Alle Angaben unten verwenden die korrigierte Bedeutung.

Quellen:

- Decompile: `<Downloads>\CubeWorld-Reversal-master-new`
- Disassembly: die vorliegende `Cube.exe` (capstone; jede `ret N` und jede Aufrufstelle geprüft)
- BELEGT: Adresse oder Datei:Zeile angegeben. VERMUTET: ausdrücklich so markiert.

---

## 1. Woraus ein Biom besteht

### 1.1 Klimazelle (`0x1C` Byte), BELEGT

**Erzeugt wird sie in `0x5D7A70`** (`GameController.cpp:87650`):

- Aufruf `__thiscall(World*, int cx, int cy)`, Rückgabe `Zelle*`, `ret 8`.
- Einziger Aufrufer ist `0x5DA33F` in `generateRegion @0x5DA280`. Der legt beim ersten Betreten einer Region die Zellen im **5×5-Umfeld** (−2..+2) an (`0x5DA324..0x5DA34D`).

**Ablage:**

- Raster `World+0x4000BC`, 1024 × 1024 Zeiger, Index `cx*0x400 + cy`.
- Eine Zelle deckt **16384 × 16384 Blöcke** ab (64 × 64 Zonen).
- Zufall: `srand(cx + 0x108A + cy*0x400 + seed*3)`, Weltsamen bei `World+0x800188`.

| Offset | Typ | Inhalt | Werte |
|---|---|---|---|
| +0x00 | int | Mittelpunkt X (Blöcke) | zufällig in der Zelle, auf 2048er-Raster + 0x400 eingerastet |
| +0x04 | int | Mittelpunkt Y | dto. |
| +0x08 | byte | **Sonderbit = Lava** | 1 nur im gemäßigten Zweig mit `rand()%10 == 0`; wird bei Ozean wieder 0 |
| +0x09..0x0B | — | nie beschrieben (`operator new`, uninitialisiert) | **nicht benutzen**: Lesezugriffe nicht ausgeschlossen |
| +0x0C | float | **Feuchte** | siehe 1.2 |
| +0x10 | float | **Temperatur** | siehe 1.2 |
| +0x14 | int | Zellsamen `cy*0x400 + cx + seed` | |
| +0x18 | int | Höhe/Land | Perlin `(n1+1)*100 − 70 + n2*30`; **< 1 ⇒ Ozean, dann fest −100** |

### 1.2 Wie die Klimawerte gewürfelt werden, BELEGT `0x5D7A70`

| Zweig (je 50 %) | Temperatur (+0x10) | Feuchte (+0x0C) | Ergebnis |
|---|---|---|---|
| gemäßigt | 0,4–0,8 | 0,3–0,7 | Grasland/Wald |
| ↳ davon 10 % **Lava** | **0,8–1,0** | **0,0–0,5** | Sonderbit = 1 |
| extrem | 0–0,1 **oder** 0,9–1,0 | 0–0,1 **oder** 0,9–1,0 | Schnee, Wüste, Dschungel, kalt-nass |
| Ozean (Höhe < 1) | 0,9–1,0 oder 0,4–0,8 | 0,9–1,0 oder 0,3–0,7 | Sonderbit gelöscht |
| Startzelle des Spielers (`World+0x8000F0/F4`) | gemäßigt | gemäßigt | Höhe 20..69, nie Ozean |

Daraus folgt: **Lava ist das einzige diskrete Biom.** Alles andere ist ein Kontinuum aus Temperatur und Feuchte.

### 1.3 Korrektur: Ghidra-Namen vertauscht, BELEGT dreifach

| Adresse | Ghidra-Name | liest Zelle | **tatsächlich** |
|---|---|---|---|
| `0x5C4800` | `GameController_sampleTemperatureGrid` | `+0x0C` | **Feuchte** |
| `0x5C4DD0` | `GameController_sampleHumidityGrid` | `+0x10` | **Temperatur** |
| `0x5C8310` | `getTemperatureAt` (Cache `+4`) | — | **Feuchte** |
| `0x5C8350` | `getHumidityAt` (Cache `+8`) | — | **Temperatur** |

Die Zuordnung ist dreifach belegt:

1. **Kreaturentabelle** `0x5F56C0`. Wert `0x5C4DD0` < 0,2 ergibt Pinguin, SnowRunner und PolarGnoll. `0x5C4DD0` ≥ 0,8 zusammen mit `0x5C4800` < 0,2 ergibt Kamel, SandHorror und Nomaden.
2. **Bodenfarbe** `0x5C56E0`. Der Aufruf `0x5E532E` übergibt Cache `+4` und `+8`. Ist `+8` < 0,3, wird es weiß-blau mit Blocktyp 0x0A (Schnee). Ist `+8` > 0,75 und `+4` < 0,2, wird es sandgelb mit Typ 9.
3. **Regen/Wolken** (Client `0x4B1564`): Niederschlag nur, wenn der Wert von `0x5C4800` > 0,01 ist.

Auch die Lava-Zelle passt dazu: heiß (`+0x10` 0,8–1,0) und eher trocken.

### 1.4 Blend über die Zellgrenze, BELEGT

**Temperatur und Feuchte** (`0x5C4DD0` / `0x5C4800`): Die Blockkoordinate wird mit Perlin um ±768 Blöcke verschoben (Frequenz 0,0005). Dann wird zwischen den Zellen im 3×3-Umfeld nach Entfernung gewichtet. Jede Funktion ist `__thiscall(World*, x, y)`, Rückgabe `float` in ST0, `ret 8`.

**Lava-Gewicht** `0x5EF040` (8 Aufrufer, `ret 8`):

```
Gibt es im 3x3-Umfeld (±0x4000 Blöcke) keine Zelle mit +8 == 1, gilt: return 0    (schneller Ausstieg, 0x5EF58E fldz)
x' = x + perlin(y*0.0005, 3423.0)  * 768                                    (Perlin @0x4C0EF0)
y' = y + perlin(x*0.0005, 23421.0) * 768
best = min über die 9 Zellen von d²(Zellmitte, (x',y'))                     (d in Blöcken)
w_i  = 1 - min(1, (d_i² - best) * 5e-7)
return  Σ_{Lava-Zellen} w_i  /  Σ_alle w_i                                  (0x5EF4FE..0x5EF56C)
```

Die **Grenzbreite** ergibt sich aus 5e-7: `d_i² − best² < 2·10⁶`. Bei rund 16384 Blöcken Abstand der Zellmitten ist der Übergang etwa 120 Blöcke breit (±60 um die Voronoi-Grenze; nachgerechnet mit dem Nachbau in `BiomeMod.cpp`). Die Perlin-Verschiebung von ±768 Blöcken macht die Grenze wellig.

`0x5F0720` ist dieselbe Funktion für **Ozean** (Bedingung `+0x18 < 0`). Ihr einziger Aufrufer ist der Himmel (`0x59DA51`).

### 1.5 Spalten-Cache je Zone, BELEGT

`world_getChunkCell256 @0x4347A0` liefert für einen Block `Zone+0xA8 + ((x&0xFF) + (y&0xFF)*256) * 0x20`, also **32 Byte je Blocksäule**. `WorldInfo_generateBiomeContent` füllt beim Zonenbau (`WorldInfo.cpp:1582–1588`):

| Offset | Inhalt |
|---|---|
| +0x04 | Feuchte |
| +0x08 | Temperatur |
| +0x0C | Lava-Gewicht |
| +0x10, +0x1C | int, genutzt in `Chunk_generateObjects` (Bedeutung offen) |

Alle Verbraucher prüfen zuerst diesen Cache und rechnen nur ohne Cache neu. **Für ein eigenes Gewicht ist hier kein Platz belegt frei.** Es braucht einen eigenen Cache (§4.2).

---

## 2. Ablauf der nativen Weltgenerierung, BELEGT

```
WorldInfo_generateBiomeContent @0x5E4850       je Zone (256×256 Blöcke)
 ├─ generateRegion @0x5DA280                   einmal je Region → Zellen 5×5 über 0x5D7A70
 ├─ Spalten-Cache füllen (+4/+8/+0xC)          WorldInfo.cpp:1582
 ├─ Höhe je Säule  0x5C5E20                    "terrain_generateColumnColor", liefert float, ret 0xC
 ├─ Boden: Farbe + Blocktyp  0x5C56E0          ret 0x1C
 ├─ Fels: Farbe  0x5C7140 / Basis 0x5F9620     ret 0x14 / 0x14
 ├─ Bewuchs/Deko je Oberflächen-Blocktyp       WorldInfo.cpp:4277–4700
 └─ Kreaturen: Rasse = 0x5F56C0(x,y,höhe,0)    Aufruf 0x5EB48C → Spawn+0x2C
Laufzeit (Client):
 ├─ Himmel/Nebel/Licht  0x59D640               ret 0x1C, Aufrufer 0x4AD7A1
 └─ Regen/Wolken        0x4B1564..             Feuchte, Lava-Gewicht
Karte: Kacheln aus den Blockfarben der geladenen Welt (RE_KarteUndLeistung.md)
```

**Oberflächen-Blocktypen** (untere 5 Bit von Byte 3; Bit 0x20 setzt `0x5C56E0` immer). Gesetzt werden sie in `0x5C56E0`. Die Namen sind VERMUTET, abgeleitet aus den gesetzten Farben:

| Typ | Bedingung in `0x5C56E0` | Farbe | Name |
|---|---|---|---|
| 4 | Startwert | Gras-Rauschen `0x5EE540` | Gras/Erde |
| 9 | Feuchte < 0,2 und Temperatur > 0,75; fällt auf 4 zurück, wenn die Mischung < 0,5 bleibt | `0x5F9BC0` (255 / 200–240 / 50–100) | Sand |
| (4) | Temperatur > 0,8 | Mischung zur Felsfarbe `0x5F9620` (heiß = rot) | rote Erde |
| 0x0A | Temperatur < 0,3 | `0x5F9E50` (100–190 / 180–220 / 255) | Schnee |
| 0x0C | Lava-Gewicht > 0,5 | `0x5F9620`-Ende: dunkel mit roten Adern | Lava-Boden |

Der Bewuchs verzweigt auf genau diese Typen (`WorldInfo.cpp:4415`: `4 || 9 || 0xC || 0xA`, Sonderpfade für 0xC/0xA ab `:4478`).

---

## 3. Vorlage: Was das Lava-Biom überall bewirkt

Das Lava-Biom ist die vollständige Bauanleitung, weil jeder Baustein im Code einzeln sichtbar ist:

| # | Baustein | Stelle | Wirkung | Beleg |
|---|---|---|---|---|
| 1 | Auswahl | `0x5D7A70` | 10 % der gemäßigten Landzellen, Sonderbit = 1 | §1.2 |
| 2 | Klima | `0x5D7A70` | Temperatur 0,8–1,0, Feuchte 0–0,5 | §1.2 |
| 3 | Gewicht | `0x5EF040` | 0..1 mit weicher Grenze | §1.4 |
| 4 | Cache | Spalte `+0x0C` | einmal je Zone | §1.5 |
| 5 | Höhe | `0x5F9340` | `+ Gewicht` auf das Höhenrauschen | `WorldInfo.cpp:8152` ff. |
| 6 | Boden | `0x5C56E0` | Mischung zur Lavafarbe, ab 0,5 Blocktyp 0x0C | `game_misc.cpp:32930–32945` |
| 7 | Fels | `0x5F9620` | dunkler Grund (10/10/10) mit roten Adern (Rauschen⁵) | `game_misc.cpp:49293–49310` |
| 8 | Bewuchs | Blocktyp 0x0C | eigener Zweig; Lava-Pflanzen sind Prop-Typ 8/9 | `WorldInfo.cpp:4478`; Zuordnung 8/9 **VERMUTET** |
| 9 | Kreaturen | `0x5F56C0` | ab Gewicht > 0,1 eigene Tabelle | §3.1 |
| 10 | Atmosphäre | `0x59D640`, `0x4B15B0` | Himmel und Licht rötlich; kein Regen ab Gewicht ≥ 0,01 | `GameController.cpp:82836`, `:48359` |

### 3.1 Die Kreaturentabelle `0x5F56C0`, BELEGT per Disassembly

`__thiscall(World*, int x, int y, int höhe, bool sonder)`, Rückgabe Rasse, `ret 0x10`. Aufrufer:

- `0x5EB48C` (Biom-Spawns, schreibt `Spawn+0x2C`, Level 1),
- `0x5D92A2` (sonder = 1), `0x5D92F0` (sonder = 0).

Aus der Liste wird **eine Rasse zufällig** gezogen (`rand() % n`). Ist die Liste leer, ist das Ergebnis 0x3C (Fly).

```
höhe < 0                        → Wasser:   SapphireFish LemonFish Seahorse Shark LanternFish MawFish Blowfish Piranha
sonder != 0                     → Völker:   HumanMale GoblinMale LizardmanMale DwarfMale OrcMale UndeadMale Gnobold Gnoll SpikeCreature
Lava-Gewicht > 0,1              → Lava:     Devourer Minotaur Imp EmberGolem FireBeetle Mosquito HellDemon
sonst: SkullBull Ogre (+ Rockling, falls 0x5EFFA0 > 0,3)
  höhe < 4                      → Küste:    Crab Seagull Spitter Turtle                       (Ende)
  0,2 ≤ T < 0,8                 → gemäßigt: 36 Rassen (Wolf, Chicken, Cow, Sheep, Horse, Hunde, Katzen, Käfer …, Bumblebee)
  T ≥ 0,8 und F < 0,2           → Wüste:    InsectGuard Midge DesertOnionling SandHorror Devourer DesertRunner Nomaden BarkBeetle FireBeetle Biter Mosquito Porcupine Camel
  T ≥ 0,8 und F ≥ 0,6           → Dschungel: InsectGuard Koala Crocodile Duckbill Onionling LeafRunner Frog Fly Parrot Peacock Nomaden Turtle Käfer Biter Mosquito Porcupine Monkey Alpakas
  T < 0,2                       → Schnee:   Wolf Chicken SnowRunner Squirrel Penguin SkullBull PolarGnoll Alpakas Bunny
```

Sprungstellen: `0x5F5753`, `0x5F580C`, `0x5F58E5`, `0x5F5CDD`, `0x5F5D67`, `0x5F5F62/68`, `0x5F68CC/D5`, `0x5F6A10/19`, `0x5F6BFC`.

Danach läuft die normale Spawn-Kette weiter: Aussehen, dann Ausrüster (RE_Mobs.md). **Eine andere Rasse hier bekommt also automatisch die richtigen Flags und die richtige Ausrüstung.**

---

## 4. Ein neues Biom registrieren

### 4.1 Auswahl: Hook auf `0x5D7A70`

```cpp
// __thiscall(World*, int cx, int cy) -> Cell*, ret 8   (verifiziert)
typedef void* (__fastcall* CellCreate_t)(void* world, void* edx, int cx, int cy);

void* __fastcall Hook_CellCreate(void* world, void* edx, int cx, int cy) {
    bool neu = Grid(world, cx, cy) == nullptr;          // World+0x4000BC, vor dem Original lesen
    uint8_t* c = (uint8_t*)o_CellCreate(world, edx, cx, cy);
    if (!neu || !c) return c;
    if (*(int*)(c + 0x18) < 1) return c;                // Ozean: nie
    if (IstStartzelle(world, cx, cy)) return c;         // World+0x8000F0/F4
    uint32_t h = Hash(cx, cy, *(int*)((uint8_t*)world + 0x800188));   // EIGENER Zufall, kein rand()
    for (auto& b : g_biome)                              // registrierte Biome, nach Priorität
        if (b.waehlt(h, *(float*)(c+0x10), *(float*)(c+0x0C))) {
            g_cellBiome[cx * 1024 + cy] = b.id;          // Seitentabelle, 1 MB
            *(uint8_t*)(c + 8) = 0;                      // nicht zugleich Lava
            if (b.setzeKlima) { *(float*)(c+0x10) = b.temp(h); *(float*)(c+0x0C) = b.feuchte(h); }
            break;
        }
    return c;
}
```

Regeln, die zwingend sind:

- **Deterministisch nur aus (cx, cy, Samen).** Zonen werden bei jedem Betreten neu erzeugt. Mit Zeit oder `rand()` sähe dieselbe Stelle beim nächsten Mal anders aus. `rand()` würde außerdem die native Würfelfolge verschieben.
- **Seitentabelle statt Zellbytes +9..+B.** Diese Bytes sind uninitialisiert, und ob etwas sie liest, ist nicht ausgeschlossen. Die Tabelle wird beim Samenwechsel (`World+0x800188`) geleert.
- **Klima überschreiben ist optional, wirkt aber überall.** Setzt man Temperatur und Feuchte passend, erben Regen, Nebel, Fels und Pflanzen schon das richtige Grundverhalten. Beispiel: Für einen Sumpf feucht und lau setzen, dann regnet es, ohne dass man es baut.
- **Thread:** Der Weltbau läuft vermutlich nicht nur auf dem Spielthread (VERMUTET). Deshalb wird die Tabelle mit einzelnen Bytes beschrieben, ohne Sperren und ohne Allokation im Hook.

### 4.2 Gewicht `W_b(x, y)`: genau wie `0x5EF040`

Man baut den Algorithmus aus §1.4 nach, nur mit der Bedingung `g_cellBiome[i] == b` statt `+8 == 1`:

- **dieselbe Perlin-Verschiebung** über das spieleigene `0x4C0EF0`, damit die Grenzen zu den nativen Biomen passen,
- dieselbe Formel und derselbe schnelle Ausstieg (keine Biom-Zelle im 3×3-Umfeld, dann 0).

**Cache:** je Zone 64 × 64 Byte (alle 4 Blöcke ein Wert, bilinear gelesen), also 4 KB je Zone. Er wird nur für geladene Zonen gehalten. Pro Block wären es 256 KB je Zone, und das ist im 2-GB-Adressraum zu viel (siehe Absturz durch `bad_alloc`, RE_KarteUndLeistung.md).

---

## 5. Ein neues Biom aufbauen: die Bausteine

Jeder Baustein ist ein Post-Hook. Das Original läuft zuerst, danach wird mit `W` gemischt. Bei `W == 0` ändert sich nichts, und die Welt bleibt Vanilla.

| # | Baustein | Hook | Signatur (verifiziert über `ret`) | Was der Hook tut |
|---|---|---|---|---|
| 1 | Auswahl + Klima | `0x5D7A70` | `(World*, cx, cy) → Cell*`, `ret 8` | §4.1 |
| 2 | Gewicht | eigene Funktion | — | §4.2 |
| 3 | Höhe/Form | `0x5C5E20` | 3 Args → float, `ret 0xC`, 13 Aufrufer | `h + W·(Versatz + Amplitude·Rauschen)`: Hochebene, Senke, Krater. Rückgabe ist die Säulenhöhe (`WorldInfo.cpp:1600`). **VERMUTET**, vor dem Bau im Spiel zu messen. |
| 4 | Boden | `0x5C56E0` | `(out[4], x, y, level, feuchte, temp, p7)`, `ret 0x1C` | RGB zur Biomfarbe mischen; ab `W > 0,5` Byte 3 = gewünschter Typ \| 0x20 |
| 5 | Fels | `0x5C7140`, `0x5F9620` | 5 Args, erstes = `float out[3]`, je `ret 0x14` | Felsfarbe mischen (Klippen, Höhlenwände) |
| 6 | Bewuchs | a) **Blocktyp wählen** (Baustein 4) b) eigene Deko nach dem Zonenbau | b) `OnZoneGenerated` (Framework) | a) Typ 4/9/0x0A/0x0C erbt den nativen Bewuchs. b) eigene Props (`chunk+0x04`, Typen 0..0x3A, RE_Objekttypen.md §3) oder eigene `.cub`-Modelle über den MobLab/LAB-Weg |
| 7 | Kreaturen | `0x5F56C0` | `(World*, x, y, höhe, sonder) → Rasse`, `ret 0x10` | bei `sonder == 0`, `höhe ≥ 0` und `W > Schwelle`: Rasse aus der eigenen Liste, Zufall aus Hash(x, y) |
| 8 | Atmosphäre | `0x59D640` | 7 Args, `ret 0x1C`, 1 Aufrufer | Himmel-, Nebel- und Lichtfarben mit W mischen (Parameter-Bedeutung noch offen, siehe §6) |
| 9 | Niederschlag | keiner | — | folgt dem Klima aus Baustein 1 |
| 10 | Karte | keiner | — | folgt den Blockfarben aus Baustein 4 |

**Name:** Ein Biomname existiert im Spiel nicht. Gefunden ist nur „Lands of …" / „Ocean" als Regionsname (Strings bei `0x302E10`). Einen Biomnamen müsste das LAB oder ein eigenes HUD anzeigen.

### 5.1 Biom-Definition (Vorschlag für das Modul)

```
[biom]
name      = Sumpf
id        = 1
auswahl   = gemaessigt, 8%          ; welcher native Zweig, wie oft
klima     = temp 0.45-0.6, feuchte 0.85-1.0
hoehe     = versatz -6, amplitude 3
boden     = rgb 70 90 40, typ 4
fels      = rgb 60 60 50
kreaturen = Frog Crocodile Mosquito Midge Turtle Biter
kreatur_ab= 0.3
himmel    = rgb 0.5 0.6 0.4, nebel 0.6
deko      = alga 3%, water-lily01 2%, tendril 1%
```

---

## 6. Offen, vor dem Bau zu klären

| Punkt | Warum wichtig | Wie |
|---|---|---|
| Rückgabe von `0x5C5E20` = Säulenhöhe? | Baustein 3 | im Spiel ±W·10 addieren und messen |
| Parameter von `0x59D640` (6 Ausgabe-Zeiger) | Baustein 8 | Werte im Spiel loggen, Lava-Gebiet gegen Grasland |
| Zuordnung Lava-Pflanzen (Prop 8/9) zu Blocktyp 0x0C | Baustein 6a | Zweig `WorldInfo.cpp:4478` bis zum `list_pushBack` verfolgen |
| Welcher Thread baut Zonen | Hooks threadsicher halten | Thread-ID im Hook loggen |
| Mehrspieler | Kreaturen spawnt der Server | Ohne Mod auf dem Server gibt es native Rassen. Gelände und Farben sind clientseitig (VERMUTET). |
| Zellbytes +9..+B | wären ein Speicherplatz ohne Tabelle | Lesezugriffe `[reg+9..B]` suchen; bis dahin Seitentabelle |

---

## 6b. Native Orte (Dörfer, Dungeons): wo sie entstehen und wie man sie abschaltet (2026-10-03)

**Die Orte stehen je Region in einer 8×8-Tabelle und lassen sich nach `generateRegion` gezielt löschen. Dazu setzt man den Ortstyp auf 0, den Typ für „nichts“.** BELEGT per Disassembly:

**Region:**

- `generateRegion @0x5DA280`: `__thiscall(World*, rx, ry)`, `ret 8`, einziger Aufrufer `0x5E4901`.
- Raster `World+0xBC + (rx*0x400+ry)*4`, Objekt 0x15A28 Byte.
- Eine Region ist genau so groß wie eine Klimazelle (16384 Blöcke, gleicher Index).
- Gibt es die Region schon, kehrt die Funktion sofort zurück.

**Ortstabelle:** `Region+0x14018 + i*0x68`, i = 0..63, ein Eintrag je 2048er-Teilfeld:

| Offset | Inhalt | Beleg |
|---|---|---|
| +0x00 | int64 X (Festkomma, `>>16` = Block) | `0x5DAD6B`, `0x5DB53E` |
| +0x08 | int64 Y | `0x5DB572` |
| +0x10 | float Radius | `0x5DACDF` (150), `0x5DB2C9` (128) |
| +0x18 | **int Ortstyp** | Schreibstellen `0x5DACD5` (0xE), `0x5DAFF1` (2), `0x5DB001` (3), `0x5DB037` (4/6/0xF), `0x5DB04F` (5), `0x5DB2AF` (7/0xF), `0x5DB2C2` (0xB), `0x5DB31F` (0xC), `0x5DB50F` (0xA) |
| +0x1C | int Unterart | |
| +0x20 | int Kennung/Samen | `0x5DB530` |

**Zonen-Datensätze der Region:** `Region+0x18 + idx*0x10`, mit idx = `((x>>8)&63)*64 + ((y>>8)&63)`. Für die 5 Orte vom Typ 10 schreibt das Spiel `Byte +0 = 4` und `+4 = Kennung` (`0x5DB5A2`, `0x5DB5AE`). `generateBiomeContent` baut dort etwas, wenn das Byte 4 ist (`WorldInfo.cpp:3268`).

**Was mit den Typen geschieht:** Die Namen stammen aus `SeedScout.h` und wurden vom Nutzer im Spiel geprüft. Belegt sind außerdem die Bauwege je Typ.

| Typ | Im Spiel | Bauweg |
|---|---|---|
| 0 | nichts | wird ausdrücklich übersprungen (`WorldInfo.cpp:3740`, `:3907`) |
| 1 | **Dorf** | `Chunk_generateObjects` = Dorfgenerator (`0x5E9672`, Bedingung `WorldInfo.cpp:3266`) |
| 5 | **Ruinen** | `Chunk_generateObjects`, eigener Zweig (`params[6]==5`) |
| 10 | **Portal** | Zonen-Datensatz mit Byte 4 (s. o.) |
| 14 | **großer Dungeon / Palast** | eigener Bauweg, von `World_buildPropInstance` ausgenommen (`:3740`) |
| 2, 3, 4, 6, 7, 8, 11, 12, 15 | **Berg-, Wald-, See-, Schlucht-, Tal-, Fels-, Baum-Dungeons** | `World_buildPropInstance`, Geländeformung (`0x5CBE00`, `0x5DF960`) |
| 9 | im Spiel nicht beobachtet | legt einen `cube::Spawn` an (Rasse 0x6B, `WorldInfo.cpp:3821`) |
| 13 | im Spiel nicht beobachtet | — |

Die Namenstabelle in `World::ctor_1` (`World.cpp:4068–4282`) liefert die Kartennamen der Orte. **Zuordnung gelesen (2026-10-13):** Jede Gruppe ist `pair_assign_2dword(Typ, Unterart)` → `pwVar9 = Name` → Insert. Das Muster ist geprüft an Portal = 10 (im Spiel bestätigt).

| Typ | Name |
|---|---|
| 1 | Village |
| 2 | Mountain |
| 3 | Forest |
| 4 | Lake |
| 5 | Ruins (Unterart 0–3), Gravesite (4) |
| 6 | Canyon |
| 7 | Valley |
| 8 | Crater |
| 9 | **Cave** |
| 10 | Portal |
| 11 | Rock |
| 12 | Tree |
| 13 | Peak |
| 14 | Castle 0, Ruins 1, Catacombs 2, Palace 3, Temple 4, Pyramid 5 |
| 15 | Island |

Die Typen 2–8 und 11–15 sind also Landmarken (Gelände + Requisiten), keine Dungeons. Die Viertel-Namen gehören zu Typ 1: 1 Trade Quarter, 2 Crafting Quarter, 3 Class Quarter, 4 Pet Quarter.

**Typ 9 (Höhle) vergibt `generateRegion` nicht.** Die Auswahl dort ist `rand() % 8` und kann 2, 3, 4/6/15, 5, 6/15, 7/15, 11 oder 12 ergeben (`GameController.cpp:88333–88440`). Höhlen kommen also aus keiner Ortstabelle.

**Zonen-Datensätze, Korrektur 2026-10-03:** Sie lösen Bauten **unabhängig vom Ortseintrag** aus. Deshalb entstanden Stadtviertel und große Dungeons auch dann, wenn nur der Ortstyp auf 0 gesetzt war.

| Byte +0 | Bedeutung | Schreibstelle |
|---|---|---|
| 1 | Stadtviertel, Byte +1 = 1..4 (Handels-, Handwerks-, Klassen-, Tierviertel) | `0x5DBF5D..0x5DBFBD` |
| 3 | Zone eines großen Dungeons (gehört zu Ortstyp 14) | `0x5DAE3F`, im selben Zweig wie `0x5DACD5` |
| 4 | Portal (gehört zu Ortstyp 10) | `0x5DB5A2` |

**Abschalten (BiomeMod), Stand jetzt:**

- **Wo:** Ortseinträge und Zonen-Datensätze werden beide gefiltert. Ein Datensatz zählt als Typ 1, 14 bzw. 10, maßgeblich ist das Biom an der Zonenmitte.
- **Wann:**
  - im Post-Hook auf `0x5DA280`, für neue Regionen;
  - zusätzlich auf dem Spielthread für **alle bestehenden** Regionen, sobald sich Biome, Schalter oder erzwungene Zellen ändern. Regionen bleiben im Speicher, auch über „Welt verlassen“ hinweg.
- **Rücknahme:** Originalwerte werden gemerkt. Ist ein Ort wieder erlaubt, wird er wiederhergestellt.
- **Bergdorf:** Auch gespeicherte Marker-Dörfer werden im abgeschalteten Biom nicht mehr gebaut.

## 6c. Zonen-Spawns, BELEGT

`generateBiomeContent` hängt jeden `cube::Spawn*` mit `lea ecx,[Zone+0x18]; call 0x66ADD0` an einen `std::vector<Spawn*>` (`0x5EB4D4`). Die Rasse kommt aus `0x5F56C0` (`0x5EB48C` → `Spawn+0x2C`). Umgewandelt werden die Spawns in `GameController_updateSubsystems @0x60C510` (`0x60FFBA..`): Kreatur-ctor mit der GUID aus `Spawn+0x48`, danach Aussehen und Ausrüstung. Eigene Spawn-Datensätze sind damit möglich, aber noch nicht gebaut. Der BiomeMod spawnt eigene `.mob`-Kreaturen deshalb zur Laufzeit in Spielernähe, mit Obergrenze.

## 6d. Hilfsfunktionen für den Nachbau, BELEGT

- **Perlin `0x4C0EF0`:** `__cdecl(double, double)`, Rückgabe in ST0, rein rechnend (kein ECX, keine Weltdaten).
- **Verschiebung in `0x5EF040`:** `x' = (int)((float)x + perlin(y*0.0005, 3423.0)*3*256)` und `y' = (int)((float)y + perlin(x*0.0005, 23421.0)*3*256)` (`0x5EF145..0x5EF21C`).

---

## 6e. Geländeform und Bäume, BELEGT (2026-10-03)

| Adresse | Was | Signatur | Beleg |
|---|---|---|---|
| `0x5C5E20` | Grundhöhe einer Säule (Zell-Elevation gemischt, plus Rauschen und Ortsformen wie See/Schlucht/Gipfel) | `__thiscall(World*, x, y, p3)` → float in ST0, `ret 0xC` | 13 Aufrufer; der Zonenbau schreibt das Ergebnis in den Höhenpuffer (`WorldInfo.cpp:1606`) |
| `0x5EFFA0` | Berg- und Klippenfaktor m | `__thiscall(World*, x, y, float)` → float, `ret 0xC` | Oberfläche `z = h + (n+1.5)·60·m + Klippenterm·8` (`WorldInfo.cpp:1726`) |
| `0x5DF960` | Baum bauen (Stamm und Laubkugeln `0x5D7F60`) | `__thiscall(World*, x, y, z, a4, a5, a6, Zone*)`, `ret 0x1C` | Aufrufer: `0x5EA7AE` normale Baumschleife (Raster 18, Oberflächentyp 4/0xA/9), `0x5E7231` Riesenbaum Ortstyp 12, `0x5BC200` Dorf |

**Wasser, BELEGT:** Die Blockabfrage gibt über der Oberfläche bei `z ≤ 0` die Vorlage `DAT_0076DDF4` zurück, bei `z > 0` dagegen `DDF8`, also Luft (`WorldInfo.cpp:1905–1918`, gleich in `game_misc.cpp:35693–35706`). `DDF4` wird beim Start mit dem Typbyte `0x82` belegt (`other.cpp:212351–212353`), und `0x82 & 0x1F = 2` ist Wasser. Der Meeresspiegel ist also 0. Jede Säule mit Oberfläche unter 1 steht unter Wasser, egal ob Ozean oder Biom.

**Furchen, BELEGT:** Die Oberfläche ist `z = (n+1.5)·60·m + 8·m·(A+B) + h` (`WorldInfo.cpp:1666–1726`).

- A und B sind `(1−(1−|n|)³)²` aus Rauschen mit 25–50 Blöcken Wellenlänge. Auf den Nulllinien sind sie 0, sonst nahe 1.
- Der zweite Term erzeugt also ein Netz schmaler Rinnen, in der Tiefe skaliert mit dem Bergfaktor m. Im Gebirge sind das die Felsschluchten.
- Bleibt m im flachen Biom klein, aber nicht 0, bleiben davon 1 Block tiefe Furchen übrig.

Der Mod setzt m deshalb bei „Berge“ ≤ 0.1 ganz auf 0.

**Bauweise:** Alle drei Hooks laufen als Post-Hook bzw. Filter. Sie ziehen die Höhe mit `Smooth(W)·glätten` zur Zielform (`BiomeGen::TerrainTarget`) und dämpfen m mit `1 − Smooth(W)·(1 − berge)`. Bäume entfallen per Hash(x, y), wenn der Rücksprung `0x5EA7B3` ist.

## 6f. Flüsse und Seen, BELEGT (Korrektur 2026-10-03)

**Flüsse und Seen tragen Wasser in jeder Höhe.** Der Zonenbau schreibt dort echte Wasserblöcke (Typ 2), und zwar auf der Höhe des Geländes, nicht auf Meeresspiegel. Die frühere Aussage „Wasser nur unter Höhe 1“ gilt nur für den Ozean. Ein Fluss auf Höhe 136 ist also normal.

**Flüsse** (`generateBiomeContent`, `WorldInfo.cpp:2697–2760`): Die Schleife läuft über jede Säule der Zone.

1. `r = 0x5F9340(x, y)`. Der Name „sampleTerrainHeight“ im Reversal ist irreführend: Die Funktion liefert den **Flussabstand**. Er setzt sich zusammen aus `|perlin·0.1 + perlin| · (0.8..1.0)`, also einer Nulllinie aus Rauschen (der Flusslauf). Dazu kommen Zuschläge nahe den Zellmitten, nahe Dorf, Berg, See und Gipfel (Typen 1/2/4/13) sowie nahe Schluchten (6/7), und das Lava-Gewicht. An all diesen Stellen gibt es keinen Fluss.
2. Fluss, wenn `1 − r·50 ≥ 0`, also `r < 0.02`. Zusätzliche Bedingung: `Terrain_sampleHeightAtWorldXY ≤ 0.95`, Bedeutung offen.
3. Wasserspiegel = Grundhöhe `0x5C5E20` (≥ 0), **auf 5 Blöcke abgestuft** (`(h/5)·5`). Daraus entstehen Stufen und kleine Wasserfälle.
4. Darunter liegt Typ 2 (Wasser, blau getönt). Das Flussbett ist Typ 3 in Felsfarbe `0x5F9620` (`WorldInfo.cpp:2756`, `:2785`).

Damit das Tal flach wird, dämpft `0x5C5E20` am Fluss Berge und Rauschen: Faktor `smoothstep(r·4)²` (`game_misc.cpp:33075ff.`).

**Wasserkörper eines Flusses, senkrecht** (`WorldInfo.cpp:2706–2760`), mit `h` = Grundhöhe:

- Stufe `S = (int)h/5·5`, Bruchteil `q = (h − S)/5`.
- Profil `f = 2q` für `q < 0.5`, sonst `f = 1 − (q − 0.5)·4`.
- Wasser liegt von `S − 5f + 2` bis `S`. Bei q = 0.5 sind das 4 Blöcke, bei q ≈ 0 ist es **keins**.
- Darüber schneidet das Spiel Luft bis `h + 2..9`.

Bei beliebigem `h` entstehen deshalb trockene oder halb gefüllte Rinnen bis 5 Blöcke unter dem Gelände. Die nativen Flüsse sehen genau so aus, als Stufenbecken.

Der BiomeMod setzt im Biom (W ≥ 0.9) deshalb `h = S + 2.5`:

- bei Seen mit S = Seemitte, auf 5 gerundet;
- bei Flüssen mit S = Gelände, auf 5 abgerundet.

Damit ist jeder Abschnitt voll gefüllt (Korrektur der „Einschlusslöcher“). Die Ufer laufen auf `S + 1` aus.

**Seen mit Stufen-Patch (2026-10-04):** Der oberste Wasserblock einer Seesäule ist `floor(h)`, der oberste Fels einer Landsäule ebenfalls `(int)` der Oberfläche (`local_12d8`, `WorldInfo.cpp:1726`; mit Bergfaktor 0 gleich `h`). Vorher lag der See auf `S + 2` und das Ufer auf `S + 1`, das Wasser stand also über.
- Spiegel `S` = abgerundetes Minimum der Biomform (Wellen, Hügel, Bergzüge) auf 6 Ringen × 40 Punkten zwischen 0,8 R und 1,9 R. Je Thread für 16 Seen gemerkt.
- See: `h = S + 0.5`. Ufer: beginnt auf `S + 0.5` und läuft weich ins Gelände. Es wird nie unter `S + 0.5` gezogen, auch nicht durch die native Resthöhe.
- Der Bergfaktor `0x5EFFA0` ist im See 0 und steigt am Ufer weich an.
- Prüfung (`/tmp/bt/lake.cpp`, Sumpf-Vorlage, 74 Seen, 179 970 Wassersäulen): kein Uferblock unter dem Wasser, jeder See bündig mit seinem niedrigsten Uferblock, einheitlicher Spiegel je See.

**Wassersäulen an steilen Flüssen, Vanilla-Fehler, behoben 2026-10-04:**

- **Ursache:** Jede Flusssäule bekommt ihren eigenen Spiegel `S = (int)h/5·5`. Am Hang springt `S` zwischen benachbarten Säulen um 5. Wasser fließt in Cube World nicht, also bleiben Wasserwände und ganze Wassertreppen stehen.
- **Erste Abhilfe (verworfen):** Flusssäulen an Stufenkanten unterdrücken. Das entfernte fast alle Flüsse.
- **Abhilfe jetzt, Stufen-Patch (2026-10-04)**, für alle Biome und auch außerhalb:
  - Code-Patch in der Flussschleife: `0x5E7F30` (5 B), `0x5E7F35` (2), `0x5E7F3F` (2), `0x5E7F41` (2), `0x5E7F43` (3), `0x5E7F46` (2), `0x5E7F4B` (3) werden NOP, `0x5E7F6F divss xmm2,xmm4` wird 4-Byte-NOP. `movss xmm4,[5.0]` (`0x5E7F37`) und `movaps xmm2,xmm0` (`0x5E7F48`) bleiben. Ergebnis: `S = (int)h`, `q = h − S`. `edx` wird danach nicht gelesen, `eax` bei `0x5E7FEF` neu geschrieben.
  - Der Höhen-Hook gibt nur dem Aufruf mit Rücksprung `0x5E7F03` den Wert `floor(h) + 0.5` zurück. Damit ist `q = 0.5`, `f = 1`, Wasser von `S − 3` bis `S` (4 Blöcke), Spiegel 1-Block-genau.
  - Das Wasser fällt also blockweise mit dem Gelände. Nur wo die Steigung über 1.5 Blöcke je Block liegt (Prüfung ±3 Blöcke), gilt die Säule nicht als Fluss.
  - Die Bytes werden vor dem Schreiben geprüft. Weichen sie ab, bleibt der Patch aus und die alte Stufenprüfung (Stufe verschieden oder Steigung > 0.35) gilt weiter.
  - Mit Patch entfällt das Einrasten der Biomflüsse auf `S + 2.5`. Seen bleiben flach (Mulde auf Seemitte + 2.5).

**Freie Ebene (ebene_an), 2026-10-04:** Mitte = `+0/+4` der nächsten Zelle des dominanten Bioms (`DominantRaw` liefert sie mit). Maske `PlainMask` (1 bis Radius, Smoothstep bis Radius + Rand) mal `Smooth(W)`. Sie zieht die Grundhöhe `0x5C5E20` auf die Ebenenhöhe, setzt den Bergfaktor `0x5EFFA0` auf 0 und hebt den Flussabstand `0x5F9340` über 0.02. Orte, Bergdorf, native Bäume, Pflanzen, Dungeons und Minen werden im Bereich Radius + Rand (+ Rand je Objekt) übersprungen, Seen mit Mitte in diesem Bereich entfallen ganz.

**Sternmassiv (2026-10-04):** `BiomeGen::StarAt` ist rein rechnend; Mitte = Zellmitte wie bei der Ebene, Seed = `Hash(Mitte)`.
- Grundhöhe `0x5C5E20`: Massivform statt Biomform (Gewicht `mask · Smooth(W)`).
- Bergfaktor `0x5EFFA0`: mal `(1 − mask)`.
- Flussabstand `0x5F9340`: fremde Flüsse über 0.02 gehoben, eigener Fluss `u · 0.02/6 / Breite` mit `u` = Bogenabstand zur Talachse, nur von `rSrc` bis zur Seemitte.
- Seen: Talend-Seen über denselben Seecode (`LakeQuery`/`LakeLevel` mit `FormTarget`).
- Prüfungen in `/tmp/bt`:
  - `star2.cpp`: Steigung im Massiv höchstens 0.61, kein Sprung; Flussgefälle höchstens 0.145 je Block; quer ±9 flach; jeder Fluss endet in der Seemitte.
  - `star3.cpp`: 47 040 Wassersäulen, kein Uferblock unter dem Wasser.
  - `tree.cpp`: Bergfichten ohne Lücken in den Säulen; `placeStructure` macht Lücken innerhalb einer Säule zu Luft, `RE_placeStructure.md` Z. 5655.

**Hangbäume in der Zonenerzeugung (2026-10-06):**
- **Wo die Zone veröffentlicht wird:** `generateBiomeContent` trägt die neue Zone erst ganz am Ende in die Region ein (`Region+0x10018+i*4 = Zone`). Davor ruft es `VoxelChunk_propagateSunlight @0x59A0E0` auf: Aufruf `0x5EDE84`, `thiscall(World, x0, y0, x0+256, y0+256, 0, Zone)`, `ret 0x18`. Die Aufrufstelle `E8 57 C2 FA FF` wird auf `Hook_SunCall` umgebogen.
- **Was dort passiert:** Der Hook setzt die Hangbäume per `placeStructure(..., Zone)` direkt in die unveröffentlichte Zone und ruft danach das Original. Das geschieht im Zonen-Thread, vor dem Sonnenlicht und ohne Sperren, wie bei den nativen Bäumen.
- **Oberfläche:** wie in der nativen Baumschleife, `world_getColumnData @0x42F7E0` (`stdcall(x, y, z, Zone)`, `ret 0x10`) ab Säulenoberkante abwärts.
- **Danach:** Nach dem Generieren markiert der Zonen-Thread die Kartenkachel der Zone (`0x46ACDE..0x46AD04`, `+0x28 = 1`).

**Flüsse quer am Hang (2026-10-06):** Die Flussläufe (`0x5F9340` nativ, eigene Rauschlinien) kennen das Gelände nicht. Mit dem Stufen-Patch lagen sie schräg über Hängen als breite Wasserfläche.
- Neu in `RiverStepOrSteep`: Ab einer Hangneigung von 0.08 wird die Flussrichtung bestimmt. Es ist die Richtung, in der `r` an zwei gegenüberliegenden Punkten (Abstand 4, 6 Richtungen) am wenigsten wächst.
- Steigt das Gelände quer dazu stärker als `1.5 / (12 · Breite)` (Vanilla 0.125), gibt es dort keinen Fluss.
- Ergebnis: Flüsse laufen nur in der Ebene, im Tal (quer flach) oder höchstens ~12° neben der Falllinie (bei Neigung 0.6) den Hang hinunter.
- Prüfung: `/tmp/bt/rdir.cpp`, Kegelberg mit Neigung 0.6. Bei 0° und 15° zur Falllinie bleibt der Fluss, ab 30° fällt er weg.

**Kleine Pflanzen (2026-10-06, belegt):** Das Spiel zeichnet Schilf, Seerosen und Unterwasserpflanzen als verkleinerte Modelle.
- **Tabelle:** `std::vector<Sprite*>` bei `GameController+0x800718`, 64 Plätze. Der Konstruktor füllt sie bei `0x462545..0x462BE0`: 0 `flowers2`, 1 `flowers`, 2–4 `grass*`, 5–7 Unterwasser, 0x16 `reed`, 0x1F/0x20 `water-lily`. Frei sind 0x12 und 0x3B–0x3F.
- **Eintrag:** 0x40 Byte in der Liste `Zone+4` (`std::list`, Knoten 0x48).

  | Offset | Inhalt |
  |---|---|
  | `+0` | Platz |
  | `+8/+0x10/+0x18` | `int64` x/y/z (Block = 0x10000; x/y +0x8000 = Blockmitte) |
  | `+0x20` | Größe |
  | `+0x24` | Drehung in Grad |
  | `+0x28` | Licht (`generateBiomeContent` setzt es am Ende für alle Einträge) |
  | `+0x2C..+0x34` | Farbton |
  | `+0x38` | Flags: 4 = Wind, 1 = unbeleuchtet, Vorgabe 2 |

- **Funktionen:** Vorgaben `0x58E9F0`, Anhängen `0x4862E0` (`thiscall(list*, rec*)`, `ret 4`).
- **Beispiele:** Schilf `0x5EC0B7` (Größe 0.09, Platz 0x16, Flags 4); Unterwasser `0x5EC4A3` (0.075/0.1 × (1..2), Flags |= 4); Laterne `0x5F2CFA` (0.1, Flags 1, Farbton 0.6/0.5/0.4).
- **Zeichnen:** `0x4BD160` mit `Tabelle[Platz]`, Größe und Wind. `Sprite::draw @0x4E6DF0` zeichnet nur mit fertigen Puffern (`+0x34`/`+0x38`), sonst setzt es bei „faulen“ Sprites (`+0x56`) `+0x54`.
- **Eigene Sprites:** `Sprite::build_mesh @0x4E7870` baut die Puffer aus `+0x30` (RGB), `+0x44..+0x4C` (Größe), `+0x55` und dem Gerät `+0x58` (`GameController+0x134`). Die Puffer ersetzt es, die alten gibt es frei (null-geprüft).
- **Mod:** Er kopiert `Tabelle[1]` als Vorlage, setzt eigene Daten und `+0x56 = 0`, baut im Spielthread (EndScene) und hängt das Sprite in einen freien Platz. Die Einträge legt er im Zonen-Thread vor dem Lichtdurchgang an. Ungetestet.

**Seen** (Ortstyp 4):

- `0x5C5E20` gräbt eine Mulde: am Rand bis Ortshöhe −25, in der Mitte bis −50.
- `generateBiomeContent` füllt im inneren Halbkreis (`d² < 0.25`) jede Luft- oder Wasser-Zelle bis **Ortshöhe −25** mit Wasser (`DAT_0076DDF4`, `WorldInfo.cpp:2233–2250`). Die Ortshöhe ist `+0x14` des Ortseintrags, beim Anlegen der Region aus `0x5C5E20` berechnet.

**Ozean:** Säulen, deren Oberfläche bei 0 oder darunter liegt, gelten über der Oberfläche als Wasser (Abschnitt 6e).

**Rinnen** (`WorldInfo.cpp:1858ff.`) sind davon getrennt. Es sind gewundene Schluchten aus einer zweiten Rauschen-Nulllinie. Über Höhe 0 schreiben sie Luft, also sind sie trocken.

**Folgen für die Biome:**

- Flüsse und Seen nutzen dieselbe Grundhöhe `0x5C5E20`, die der Gelände-Hook verändert. Im Biom folgt das Wasser deshalb automatisch der neuen Form. Im Sumpf (Grundhöhe ~1) liegen Flüsse auf Höhe 0 und gehen in die Tümpel über.
- **Gebaut:** Hook auf `0x5F9340` (`ret 0xC`, 6 Aufrufer). Im Biom wird `r` durch die Breite geteilt (oder um 1 angehoben, wenn die nativen Flüsse aus sind). Weitere Werte gehen per Minimum ein:
  - eigene Flussläufe als Abstand zur Nulllinie eines Rauschfelds, `|f|/|∇f|`, mit Schwelle 0.02 bei 6 Blöcken Halbbreite;
  - Seen mit `d/R · 0.018`.

  Der Höhen-Hook ebnet den Seeboden auf die Höhe der Seemitte ein, damit der abgestufte Wasserspiegel über den ganzen See gleich hoch ist. Weil `0x5C5E20` selbst `0x5F9340` ruft, flacht das Spiel die Täler entlang der eigenen Flüsse und Seen von sich aus ab.

## 7. Empfohlene Reihenfolge

1. **Registrierung + Gewicht + Boden/Fels** (Bausteine 1–2, 4–5): sofort sichtbar, auch auf der Karte. Prüfbar mit einem LAB-Knopf „Biom hier: X, W = 0,73".
2. **Kreaturen** (Baustein 7): kleiner Hook, großer Effekt. Die Ausrüstung kommt nativ.
3. **Höhe** (Baustein 3), nachdem die Rückgabe gemessen ist.
4. **Bewuchs und Atmosphäre** (6, 8).

Testplätze wie immer **40 Blöcke versetzt**. Für ein Biom heißt das: per Teleport in eine registrierte Zelle springen, denn eine Zelle ist 16384 Blöcke groß.

**Flüsse am Hang, Umbau 2026-10-06 abends:** Die Hangprüfung lief bisher in jedem Aufruf des Flussabstands `0x5F9340`, und den ruft die Höhenfunktion bei jeder Abfrage auf (`0x5C60F1`). Das war zu teuer. Sie ist dort entfernt.

Jetzt entscheidet nur der eine Aufruf, nach dem die Flussschleife Wasser schreibt (`call @0x5E7E85`, Rücksprung `0x5E7E8A`), und nur wo `r ≤ 0.02`.

- **Höhen:** Die Höhen der 4 Nachbarn (Abstand 1) stammen aus dem Säulen-Speicher. Die Zone hat sie vorher schon berechnet.
- **Querrichtung:** Sie ergibt sich aus der Vorwärtsdifferenz von `r` in x und y.
- **Regeln:**
  - Mehr als 1.5 Blöcke zum Nachbarn: kein Wasser.
  - Steigung quer zum Fluss × 12 × Breite > 1.5 Blöcke: kein Wasser.
  - Ohne Stufen-Patch gilt weiter die alte 5er-Stufenkante.
- **Folge:** Das Flussbett, das die Höhenfunktion gräbt, bleibt an solchen Stellen trocken stehen (Bachbett).


## Aufhebbare Pflanzen: Herzblume, Pilz, Kürbis, Ananas (RE 2026-10-06, belegt)

Diese Pflanzen sind **Gegenstände, die am Boden liegen**, keine Blöcke und keine Deko.

**Liste:** `Zone+0x30` ist ein `std::vector` mit Einträgen zu je 0x148 Byte. Vorgaben schreibt `0x44A970` (thiscall), angehängt wird mit `0x44D590` (thiscall(vec*, rec*), `ret 4`).

**Aufbau eines Eintrags:**

| Offset | Inhalt |
|---|---|
| `+0x000` | Gegenstand, 0x118 Byte: `+0` Typ (0x0B = Material), `+1` Unterart, `+0x10` Wort = 1 |
| `+0x118/+0x120/+0x128` | `int64` x/y/z (Block = 0x10000; x/y +0x8000) |
| `+0x130` | Drehung in Grad |
| `+0x134` | Größe (Vorgabe 0.0714, Herzblume/Kürbis 0.1) |
| `+0x138` | Byte = 2 |

**Unterarten von Typ 0x0B** (Modell aus `item_icon_id @0x4EC400`):

| Unterart | Modell |
|---|---|
| 0x0F | Pilz |
| 0x10 | Kürbis |
| 0x11 | Ananas |
| 0x12 | Rettich |
| 0x13 | Schimmerpilz |
| 0x14 | Ginseng |
| 0x16 | Herzblume |
| 0x17 | Kaktusfeige |
| 0x18 | Frost-Herzblume |
| 0x19 | Seelenblume |
| 0x1B | Schneebeere |

**Wo das Spiel sie anlegt:**

- **Wildnis** (`generateBiomeContent`, `0x5ECB75..0x5ED176`):
  - auf Grasboden mit Wahrscheinlichkeit 1/n: Pilz (0x0F) oder Herzblume (0x16, Größe 0.1);
  - auf Schnee: Frost-Herzblume (0x18).
  - Schimmerpilz (0x13) entsteht bei `0x5E64E0`.
- **Dorffelder** (`Chunk_generateObjects`, `0x5B1C80..0x5B1EAD`, Feldtyp 6), je Feld eine von 6 Sorten:
  - 0 Kürbis: Blätter `0x17` + Gegenstand 0x10;
  - 1 Sonnenblume;
  - 2 Bohnenranke;
  - 3 Weizen oder Kornblume;
  - 4 Mais;
  - 5 Ananas: Blätter `0x18` + Gegenstand 0x11.
  - Auf jedem Feldblock gibt es mit 1/10 · 1/4 einen Gegenstand mit Größe 0.1, sonst das Deko-Gewächs aus der Pflanzentabelle.

**Name:** `buildObjectName @0x598A50`, thiscall(this, out wstring*, item), `ret 8`, liefert `out`. Der wstring wird mit `0x40EB60` (thiscall(str*, const wchar_t*)) gebaut.

**Eigene Gegenstände (Mod):**

- **Nummern:** Typ 0x0B, Unterarten 0x30–0x4F (32 Stück), fest in der `.zmod` gespeichert (`gegenstand_id`).
- **Modell-Hook:** `item_icon_id` liefert für diese Nummern ein eigenes Sprite (gebaut wie die kleinen Blumen, nicht in der Pflanzentabelle).
  - Fehlt das Modell, zeigt er die Herzblume, denn das Spiel prüft 0 nicht.
- **Namen-Hook:** `buildObjectName` liefert den eigenen Namen, als Ersatz „Eigene Pflanze“.
- **Ablage:** Die Einträge landen im Zonen-Thread vor dem Sonnenlicht in `Zone+0x30`.

**Ungeprüft:**

- ob das Aufheben ohne Weiteres klappt (Server-/Mehrspieler-Prüfung);
- wie andere Funktionen mit Unterarten ab 0x30 umgehen (Wert, Stapel, Handwerk);
- ob gesammelte Exemplare ohne den Mod beim Laden Probleme machen.
