# Wie das Spiel Mobs spawnt

**Kurz:** Alle Kreaturen der Welt — Dorfbewohner, Monster, Vogelscheuchen — entstehen
in **einer** Funktion: `Chunk_generateObjects @ 0x005AF170`. Sie legt keine
`Creature` an, sondern einen **Bausatz** vom Typ `cube::Spawn` (0x10F0 Byte,
ctor `0x005ADB20`). Erst später wird daraus per `Creature::ctor_0 @ 0x0043B690`
eine lebende Kreatur, wobei ein festes Feld-für-Feld-Kopierschema läuft. Wer die
Flags eines Mobs verstehen will, muss dieses Kopierschema kennen — es steht
weiter unten vollständig.

Alle Adressen statisch, ImageBase `0x400000`. Baum: `CubeWorld-Reversal-master-new`.

---

## 1. Die zwei Wege

| | Nativer Weg (Spiel) | Mod-Weg (`cube::util::SpawnCreature`) |
|---|---|---|
| Wo | `Chunk_generateObjects` bei Zonengenerierung | jederzeit, aus dem EndScene-Hook |
| Was zuerst | `cube::Spawn`-Bausatz (0x10F0) | direkt `cube::Creature` (0x1E60) |
| Registrierung | `rbtree_findOrInsert_pairKey @ 0x468AD0` | dieselbe Funktion |
| Aussehen | `Spawn_initDefaults @ 0x428750` + `creature_generateAppearance @ 0x43F7C0` | nur `creature_generateAppearance` |
| Ausrüstung | `World_generate*SpawnList` → `addItemToInventory` | keine |
| KI | Verhaltensbaum **im Bausatz** vorgefertigt | `World_initCompanion @ 0x5EE7A0` |

Der Mod überspringt also den Bausatz und baut die Kreatur von Hand. Das
funktioniert, erklärt aber, warum unsere NPCs nackt sind und warum wir die KI
nachträglich umhängen müssen (`AttachRandomWalk`).

---

## 2. `Chunk_generateObjects @ 0x005AF170`

```c
void Chunk_generateObjects(int zone, undefined4* params);
```

Aufgerufen aus `WorldInfo.cpp:3267`, und zwar **nur**, wenn
`params[6] == 1 || params[6] == 5`. `params` (im Pseudocode `local_5cc0`) ist der
Ortsdatensatz:

| Feld | Bedeutung | Beleg |
|---|---|---|
| `params[0..3]` | Weltposition des Ortes (int64 X/Y) | `__alldiv(..., 0x10000)` → Fixed16 |
| `params[6]` | **Ortsart**: `1` = Dorf mit Häusern, `5` = Lager/Dungeon | Verzweigung `game_misc.cpp:29400` |
| `params[7]` | **Unterart** 0..4 — bestimmt die Rassenliste | `switch` `game_misc.cpp:30152` |
| `params[8]` | Ortsseed (`% 3` wählt den Anführer) | `game_misc.cpp:30240` |
| `params[9]` | **Stufe** des Ortes → Level jeder Kreatur | überall `piVar18[0xd] = local_5cc0[9]` |
| `params[10]` | Byte → `Spawn+0x58` | `game_misc.cpp:30328` |

---

## 3. Der Bausatz `cube::Spawn` (0x10F0 Byte)

`ctor_0 @ 0x005ADB20` setzt die Vorgaben, dann überschreibt der Generator
gezielt. Die Bedeutung der Felder ist **nicht geraten** — sie folgt aus der
Übergabe an die fertige Kreatur (`GameController.cpp:96652-96690`):

```c
pCVar22 = (Creature*)operator_new(0x1e60);
Creature::ctor_0();
rbtree_findOrInsert_pairKey();            // in die EntitesMap
vec6_copy();                              //  Spawn+0x10   -> Creature+0x10   Position
*pfVar18                = Spawn+0x54;     //               -> Rotation (Yaw)
*(float*)(Cre + 400)    = Spawn+0x34;     //  400 = 0x190  -> Level
Cre[0x60]               = Spawn[0x28];    //               -> hostility_flags
*(float*)(Cre + 100)    = Spawn+0x2c;     //  100 = 0x64   -> race
Cre[0x140]              = Spawn[0x30];    //               -> char_class
Cre[0x141]              = Spawn[0x31];    //               -> char_specialization
Cre[0x1a8]              = Spawn[0x58];    //               -> (unbenannt)
Struct_copy_acBytes();                    //  Appearance-Block
*(int64*)(Cre + 0x178)  = Spawn+0xf58;    //  Groessen/Skalen
*(float*)(Cre + 0x180..0x188) = Spawn+0xf60..0xf68;
Cre[0x1d8]              = Spawn[0x5c];
*(float*)(Cre + 0x1ac)  = Spawn+0x10c8;
strcpy(Cre + 0x1168,      Spawn + 0x10cc);//               -> Name
list_buildAndFree(Spawn + 0x10a0);        //               -> Verhaltensbaum
*(float*)(Cre + 0x1d2c) = Spawn+0x08;     //               -> Aggro-/Leine-Radius
*(float*)(Cre + 0x1d30) = Spawn+0x10e0;
*(float*)(Cre + 0x1d34) = Spawn+0x10e4;
Cre[0x1d38]             = Spawn[0x10e8];
```

Daraus die Feldtabelle:

| Spawn | Creature | Typ | Bedeutung | Vorgabe im ctor |
|---|---|---|---|---|
| `+0x08` | `+0x1D2C` | float | Aggro-/Leinenradius in Blöcken | `200.0` |
| `+0x10` | `+0x10` | 3× int64 | Position (Fixed16, 1 Block = 65536) | 0 |
| `+0x28` | `+0x60` | int | **`hostility_flags`** — siehe §4 | `1` |
| `+0x2C` | `+0x64` | int | **`race`** — siehe `Creature::Race` | `4` |
| `+0x30` | `+0x140` | byte | **`char_class` / Beruf** — siehe §5 | `0` |
| `+0x31` | `+0x141` | byte | `char_specialization` | `0` |
| `+0x34` | `+0x190` | int | **Level** | `1` |
| `+0x54` | Rotation | float | Blickrichtung in Grad (0..360) | 0 |
| `+0x58` | `+0x1A8` | byte | aus `params[10]` | 0 |
| `+0x7A` | Appearance | ushort | Flagbits, u. a. `0x200`, `0x1000` | 0 |
| `+0x10A0` | `+0x13E4` | list | Verhaltensbaum | leer |
| `+0x10CC` | `+0x1168` | char[] | Name (nullterminiert) | leer |
| `+0x10E8` | `+0x1D38` | byte | bei Wildmonstern `1` | 0 |
| `+0x109C` | — | ptr | `SequentialBehavior`, wird im dtor freigegeben | 0 |

> **Achtung:** `cube::Spawn` ist **nicht** dieselbe Struktur wie `cube::Creature`.
> Die Offsets stimmen nur bei der Position (`+0x10`) überein. Alles andere wird
> einzeln kopiert.

---

## 4. `hostility_flags` (`Spawn+0x28` → `Creature+0x60`)

Das ist **der** Schalter. Er entscheidet über Fraktion, Kollision und
Ansprechbarkeit. Belegte Werte aus dem Generator:

| Wert | Bedeutung | Wo im Spiel belegt |
|---|---|---|
| `0` | **Spieler** | — |
| `1` | **Wildes Monster** — feindlich gegenüber dem Spieler, untereinander gleiches Team | Lager/Dungeon-Zweig, `game_misc.cpp:30323` |
| `3` | **Ansprechbarer NPC** — `[R] Talk`, Dorfbewohner, Händler | alle fünf Haus-Zweige, `game_misc.cpp:29481` |
| `5` | **Begleiter** — folgt einem Besitzer | `World_initCompanion @ 0x5EE7A0` |
| `6` | **Passives Objekt** — Vogelscheuche | `game_misc.cpp:24800` (`race = 0x8C` = Scarecrow) |

Der Wert **2**, den einige Modstellen übergeben, kommt im Spiel **nirgends** vor.
Er erzeugt eine Kreatur, die weder Monster noch NPC ist. (Ist im Mod meist
folgenlos, weil `MakeHostile()` den Wert danach auf `1` zieht — aber nicht überall,
siehe §7.)

---

## 5. `char_class` (`Spawn+0x30` → `Creature+0x140`) — der Beruf

In einem Dorf (`params[6] == 1`) läuft eine Schleife über alle Häuser. Der
**Haustyp** steht im Hausobjekt bei `+0x60` und wählt den Zweig. Die fünf Zweige
sind bis auf **ein einziges Byte** identisch:

| Haustyp (`Haus+0x60`) | `char_class` | Ausrüstung erhält | Pseudocode |
|---|---|---|---|
| `1` | **`0x84`** | — | `game_misc.cpp:29490` |
| `2` | `0x80` | `World_generateMonsterSpawnList` | `:29568` |
| `3` | `0x81` | `World_generateNpcSpawnList` | `:29720` |
| `4` | `0x82` | — | `:29644` |
| `5` | `0x83` | `World_generateNpcSpawnList` | `:29796` |
| (Sonderfall am Schiff/Steg) | `0x88` | — | `:28613` |

**`0x84` ist der Gastwirt.** Belegt: `GameController.cpp:34127` prüft
`*(char*)(entity + 0x140) == -0x7c` (= `0x84`) und baut daraufhin die
UI-Seite `u"innkeeper"`. Das deckt sich mit dem, was im Mod schon per Live-Test
gefunden wurde (`README.md`, Zeile 52). `0x89` wird bei `:34349` geprüft.

Rasse aller Dorfbewohner: `rand() % 2 + 2` → `2 = HumanMale` / `3 = HumanFemale`.
Level: `params[9]` (Ortsstufe). Radius: die Vorgabe `200.0`.

Jeder Dorfbewohner bekommt denselben Verhaltensbaum:

```
SequentialBehavior
  ├── CombatBehavior(20.0f)      // 0x41A00000
  ├── LookAtPlayerBehavior
  └── WalkPathBehavior(2.0f)     // 0x40000000
```

---

## 6. Monster: die Rassenlisten (`params[7]`)

Für `params[6] == 5` baut der Generator **zwei** Vektoren von Rassen-IDs: eine
kleine für den Anführer, eine große für das Fußvolk. Danach:

```c
piVar18[10] = 1;                              // hostility_flags = 1 (Monster)
piVar18[0xd] = local_5cc0[9];                 // Level = Ortsstufe
local_5c54[0xb] = *vector_at_stride4(rand() % size);   // race aus der Liste
*(float*)(Spawn + 0x54) = rand() * 360.0 / 32767.0;    // zufaellige Blickrichtung
*(undefined1*)(Spawn + 0x43a) = 1;            // +0x10E8 = 1  (Wildmarkierung)
```

Die Listen, dezimal gegen `cube::Creature::Race` aufgelöst
(`game_misc.cpp:30152-30258`):

| `params[7]` | Anführer | Fußvolk |
|---|---|---|
| `0` | Frightener (41), Skeleton (17), Zombie (96) | + Bat (59), GreenSlime (37), PinkSlime (38), BlueSlime (40), YellowSlime (39) |
| `1` | DwarfMale (9), DwarfFemale (10) | + Alpaca (22), Crow (55) |
| `2` | NomadMale (83), NomadFemale (84) | + Hornet (53), Fly (60), Parrot (58) |
| `3` | SpikeCreature (76) | + Parrot (58), Fly (60), Djinn (81) |
| `4` | UndeadMale (15), UndeadFemale (16) | + Zombie (96); Boss nach `params[8] % 3`: Werewolf (94) / Skeleton (17) / Vampire (97) |
| sonst | OrcMale (11), OrcFemale (12) | + Ogre (46), Collie (19) |

Monster-Verhaltensbaum: `Sequential[ CombatBehavior(20.0f), WalkPathBehavior(2.0f) ]`
— **ohne** `LookAtPlayerBehavior`. Das ist der einzige Strukturunterschied zu
den Dorfbewohnern.

---

## 7. Was das für den Mod bedeutet

### 7.1 Ein echter Fehler in `MakeVillager`

```cpp
// MountainVillage.cpp:951-952
*((uint8_t*)c + 0x60) = 5;              // Faktion: 5=freundlich
c->entity_data.hostility_flags = 3;     // 3 = talkable
```

Das sind **dasselbe Byte**. Nachrechnen an `cwsdk/cube/Creature.h`:

```
+0x00 vftable(4) +0x04 field_4(4) +0x08 GUID(8)          -> 0x10
+0x10 position   Vector3<int64_t> = 24 Byte              -> 0x28
+0x28 rotation / +0x34 velocity / +0x40 acceleration
+0x4C retreat_force / +0x58 head_rotation                -> 0x5C
+0x5C physics_flags, field_4D, field_4E, field_4F        -> 0x60
+0x60 hostility_flags
```

Zeile 952 überschreibt Zeile 951. Das Ergebnis ist `3` — und `3` ist zufällig
genau der Wert, den das Spiel für Dorfbewohner verwendet. Es funktioniert also,
aber nicht aus dem Grund, der im Kommentar steht. Die `5` hat nie gewirkt.

### 7.2 Die `hostility = 2`-Aufrufe

| Stelle | Wert | danach `MakeHostile`? | Ergebnis |
|---|---|---|---|
| `AirshipSpawner.cpp:301` | 2 | ja → `1` | korrekt |
| `OceanContent.cpp:287` | 2 | ja → `1` | korrekt |
| `OceanContent.cpp:284` | 2 | **nein** | bleibt `2` — bewusster Vergleichsspawn |
| `PirateShip.cpp:127,143` | `kPirateHostility` | ja → `1` | korrekt |
| `AirshipTaxi.cpp:150`, `BlacksmithNpc.cpp:119`, `WanderingMerchant.cpp:381` | 0 | nein, aber danach `= 3` | korrekt |

Sauberer wäre, gleich `1` bzw. `3` zu übergeben und `MakeHostile` nur dort zu
verwenden, wo wirklich nachträglich umgeschaltet wird.

### 7.3 Was uns gegenüber dem nativen Weg fehlt

1. **Ausrüstung — KORRIGIERT.** `World_generateMonsterSpawnList` / `World_generateNpcSpawnList`
   erzeugen **keine Kampfausrüstung**, sondern Gegenstände der Kategorie 2 (Rezepte) für das
   **Händlersortiment** der Dorfbewohner. Die echte Ausrüstung vergibt der **Ausrüster
   `@0x5C7730`** (im Reversal fälschlich `creature_initBehaviorByType`): Er würfelt die Klasse
   und setzt Waffen und Rüstung in `Spawn+0x120` (13 Plätze à 0x118). Das Mob-Labor nutzt ihn —
   siehe `RE_Mobs.md`.
2. **Name.** Nativ aus `Spawn_initNameArrays`, kopiert nach `Creature+0x1168`.
3. **Aggro-Radius** `Creature+0x1D2C`. Der Mod setzt ihn nie; die Kollisions- und
   Verfolgungsschleife liest ihn als `radius + 20.0`
   (`GameController.cpp:95942`). Bleibt er 0, reagiert der Mob erst auf 20 Blöcke.
4. **`LookAtPlayerBehavior`.** Unsere NPCs drehen den Kopf nicht mit.

### 7.4 Kürzester Weg zu einem „richtigen" Mob im Mod

Die hier hergeleiteten Werte sind inzwischen in `cwsdk/cube_util.h` benannt
(`cube::util::Hostility`, `cube::util::CharClass`, `kNativeAggroRadius`) und
`SetAggroRadius()` ist als SEH-abgesicherter Helfer ergänzt:

```cpp
using namespace cube::util;
cube::Creature* c = SpawnMonster(pos, race, Hostility::Monster, level);
if (c) {
    SetAggroRadius(c, kNativeAggroRadius);        // +0x1D2C, nativ 200
    AttachRandomWalk(c, pos.X, pos.Y);            // Heimatanker +0x1C0/+0x1C8
}
```

Für einen ansprechbaren NPC `Hostility::Talkable` statt `Monster`, und für einen
Gastwirt zusätzlich `c->entity_data.char_class = (char)CharClass::Innkeeper;`.

---

## 8. Belegstellen

| Sache | Datei:Zeile |
|---|---|
| Spawner | `cube/game_misc/game_misc.cpp:23031` (`Chunk_generateObjects @ 0x5AF170`) |
| Aufrufer | `cube/world/WorldInfo.cpp:3267` |
| Bausatz-ctor | `cube/world/Spawn.cpp:74` (`@ 0x5ADB20`) |
| Vorgabewerte | `cube/world/Spawn.cpp:10` (`Spawn_initDefaults @ 0x428750`) |
| Dorfzweige (5 Haustypen) | `game_misc.cpp:29466-29850` |
| Rassenlisten (`switch params[7]`) | `game_misc.cpp:30152-30258` |
| Monsterzweig | `game_misc.cpp:30301-30380` |
| Vogelscheuche (`hostility 6`) | `game_misc.cpp:24797-24810` |
| Bausatz → Creature | `cube/control/GameController.cpp:96641-96690` |
| Gastwirt-UI (`char_class 0x84`) | `cube/control/GameController.cpp:34127` |
| Begleiter-Init | `game_misc.cpp:47070` (`@ 0x5EE7A0`, dort als `World_appendMeshBatch` fehlbenannt) |
| Wegpunkt-KI der Dorfbewohner | `cube/ai/SpawnLocationBehavior.cpp:8` (`@ 0x4DFAD0`) |

---

## 9. Ungeprüft

- `Spawn+0x7A`, Bits `0x200` und `0x1000` — werden nur bei Wildmonstern gesetzt.
  Landen über `Struct_copy_acBytes` im Appearance-Block; welches Feld genau, ist
  offen.
- `Spawn+0x58` / `Creature+0x1A8` — kommt aus `params[10]`, Bedeutung unbekannt.
- `Spawn+0x10E8` / `Creature+0x1D38 = 1` bei Wildmonstern — vermutlich
  „respawnt nach dem Tod", nicht nachgewiesen.
- Ob `Creature+0x1D2C` wirklich der Aggro-Radius ist, folgt aus **einer**
  Vergleichsstelle (`fVar12 = ... + 20.0; if (fVar12*fVar12 < abstandQuadrat)`).
  Im Spiel messbar, indem man den Wert auf 5 setzt und schaut, ab wann ein
  Monster angreift.
