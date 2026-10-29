# RE: Was macht ein Objekt BENUTZBAR und FEST?

Quelle: Ghidra-Decompile-Baum `<Downloads>\CubeWorld-Reversal-master-new`.
Alle Zeilenangaben beziehen sich auf diesen Baum (Client = `cube/`, Server = `server/`).
ImageBase 0x400000, 32-Bit MSVC.

---

## Kurzantwort (BLUF)

**Beides haengt am OBJEKTTYP des 0x188-Byte-Records (Feld +0x00), nicht am Blocktyp.**
Der Blocktyp entscheidet nur ueber die *Voxel*-Kollision der Wand ringsum.

| Frage | Antwort | Beleg |
|---|---|---|
| Interaktion | Objekttyp `record+0x00`. Der Wert, den `interactSpecialObject` prueft, ist genau dieses Feld. | `cube/control/GameController.cpp:83654` (Handle-Aufloesung, Stride 0x188) + `:39709` (`*piVar38`) + `:33928` (Vergleich) |
| Kollision (Objekt) | Objekttyp + Zustandsbyte `record+0x30` + Halbmasse `record+0x24/0x28/0x2C` | `cube/control/GameController.cpp:101256-101340`; sauberer im Server: `server/game_misc/game_misc.cpp:45785-45880` |
| Kollision (Welt) | Voxel: `(BlockColor.Type & 0x1F) != 0 && != 2` | `cube/control/GameController.cpp:13650` (`block_isSolidType`) |
| Zone-Finalisierung | `WorldInfo_generateBiomeContent @0x5E4850` baut am Ende den Objekt-Ortsindex und haengt den Chunk erst dann in das Weltgitter. | `cube/world/WorldInfo.cpp:5048-5080` |

**Fuer eine funktionierende Tuer reicht ein 0x188-Record mit `type = 1`, `+0x30 = 1`,
sinnvollen Skalen in `+0x24/0x28/0x2C` und einem Eintrag im Ortsindex `zone+0xAC`.
Bloecke braucht die Tuer selbst NICHT — im Original wird an der Tuerstelle bewusst
kein Block gesetzt.**

---

## 0. Wichtige Korrekturen an den Ausgangspunkten

Zwei der im Auftrag genannten Startpunkte sind vom Ghidra-Audit FALSCH benannt.
Das ist belegt, nicht vermutet:

### `isInteractableTileType @0x47F9F0` ist KEINE Tile-Pruefung

Die Funktion ist ein **Item-Praedikat** (vermutlich `item::isStackableType`).
Beide Aufrufstellen im Client liegen im Inventar-Code, nicht im Weltcode:

* `cube/control/GameController.cpp:25342` — Suche nach einem bestehenden Stapel
  im Inventar: `if (isInteractableTileType() && Item_equals(&itemType)) { *piVar1 += count; }`
  Direkt darueber wird `*pItem == '\f'` (0x0C, Muenzen) und `*pItem == '\r'` (0x0D)
  auf Waehrungszaehler gebucht (`cube/control/GameController.cpp:25301-25331`).
* `cube/control/GameController.cpp:35612` — HUD-Textausgabe fuer die Stapelzahl.

Die geprueften Werte `{1, 0x0A, 0x0B, 0x0C, 0x0D, 0x15}` sind also **Item-Typen**,
nicht Blocktypen und nicht Objekttypen. Fuer Tueren ist die Funktion irrelevant.

### `block_isPassableType @0x434870` ist ebenfalls ein Item-Praedikat

`cube/control/GameController.cpp:12678`. Die Typmenge
`{0x0C, 0x0D, 0x15, 0x0B (ausser Sub 0x0E), 0, 0x19, 0x14, 0x18, 0x17}` ist
**wortgleich** mit der Item-Fallunterscheidung in `GameController.cpp:25301-25308`.
Aufrufstelle `:35635` benutzt sie, um zu entscheiden, ob hinter den Item-Namen
noch eine Stufenzahl geschrieben wird. Kein Weltbezug.

### Belastbar sind dagegen

* `block_isSolidType @0x43B480` — echte Voxel-Kollision.
* `GameController_interactSpecialObject @0x488030` — echte Objektinteraktion.
* `RandomInteractionBehavior_pickObjectAt @0x59ED30` — echte Haus-/Raumsuche.
* `WalkPathBehavior_sampleStepCollision @0x59F850` — echte Voxel-Kollision.

---

## 1. Das Weltgitter (belegt)

```
World +0x0BC : Region*  [1024][1024]           // je Region 64x64 Zonen
Region+0x018 : Field    [4096]  (16 Byte)      // Region-eigene Spalten (nicht genutzt fuer Objekte)
Region+0x10018: Zone*   [64][64]               // je Zone 256x256 Bloecke
Region+0x14018: SubRegion[8][8] (0x68 Byte)
```
Beleg: `Region::ctor_0` in `cube/world/Region.cpp:6-46`
(4096 x 16 Byte ab +0x18 endet exakt bei +0x10018, danach 4096 Zeiger bis +0x14018,
danach 64 x 0x68 = 0x1A00 bis +0x15A18 — genau `self[0x5686]`).

`world_getRegionCell64 @0x434D10` (`cube/game_misc/game_misc.cpp:2410`) liefert den
`Zone*` fuer eine Zonenkoordinate:

```c
// blockX/blockY sind hier ZONENkoordinaten (= Weltblock >> 8)
Zone* world_getRegionCell64(uint zx, uint zy) {
  if (zx < 0 || zy < 0 || zx >= 0x10000 || zy >= 0x10000) return 0;   // Weltgrenze
  Region* r = region_getCell1024(zx >> 6, zy >> 6);                    // 1024x1024 Regionen
  if (!r) return 0;
  return *(Zone**)((char*)r + 0x10018 + ((zx & 63) * 64 + (zy & 63)) * 4);
}
```

### Zone-Layout (256 x 256 Bloecke)

Deckt sich mit `cwsdk/cube/Zone.h` des Mods, mit zwei Ergaenzungen:

| Offset | Inhalt | Beleg |
|---|---|---|
| +0x0C/+0x10/+0x14 | `std::vector<StaticObject>` (Stride **0x188**) | `cube/control/GameController.cpp:83660` |
| +0x30 | Map der aufhebbaren Items | `cube/control/GameController.cpp:40448` |
| +0x60/+0x64 | Zonenindex x/y | `cube/control/GameController.cpp:10275-10283` |
| +0x74 | byte "geaendert"-Flag | `cube/control/GameController.cpp:37168` |
| +0x88/+0x8C | `std::vector<House*>` | `cube/ai/RandomInteractionBehavior.cpp:440` |
| +0xA8 | `Field blocks[256][256]`, Stride 0x20 | `cube/_library/audit.cpp:37196` |
| **+0xAC** | **`std::list<ObjRef> objIndex[32][32]`, Stride 8 — Ortsindex der Statikobjekte, Zelle = 8x8 Bloecke** | `cube/control/GameController.cpp:10307` (`getTileCell32`) |

`ObjRef` ist ein 16-Byte-Listenknoten-Wert:
```c
struct ObjRef {            // Listenknoten: {next, prev, dann dieser Wert}
    StaticObject* obj;     // +0x08 im Knoten
    int zoneX, zoneY, index;  // +0x0C..+0x14 im Knoten  = das "Handle"
};
```
Beleg: `cube/ai/RandomInteractionBehavior.cpp:139-151` (`objNode[2]` = Objektzeiger,
`objNode+3` = 3 Ints, die mit `self+0x1478..0x1480` verglichen werden) und
`resolveVectorElementByHandle` (`cube/control/GameController.cpp:83654`), das genau
dieses Handle wieder aufloest:

```c
// Global::resolveVectorElementByHandle @ 0x5A0910
int resolveVectorElementByHandle(uint32_t* handle) {   // handle = {zoneX, zoneY, index}
  Zone* z = world_getRegionCell64(handle[0], handle[1]);   // Zone finden
  if (!z) return 0;
  int i = handle[2];                                        // Index in static_objects
  if (i < 0) return 0;
  if (i >= (z->objEnd - z->objBegin) / 0x188) return 0;     // Stride 0x188 = StaticObject
  return z->objBegin + i * 0x188;                           // Zeiger auf den Record
}
```

### Field (Blockspalte, 0x20 Byte)

```c
struct Field {
  int vftable; int f4; int f8; int fC;
  int minZ;        // +0x10  z der untersten gespeicherten Zelle
  int f14;
  BlockColor* blocks;  // +0x18
  int count;       // +0x1C  ANZAHL Zellen (im SDK faelschlich "MaxZ_Maybe")
};
```
Beleg: `world_getColumnData @0x42F7E0`, `cube/game_misc/game_misc.cpp:2318-2344`.

---

## 2. KOLLISION

### 2a. Voxel-Kollision — belegt

```c
/* Global::block_isSolidType @ 0x43B480
 * cube/control/GameController.cpp:13650
 * this (ECX) zeigt auf eine BlockColor {R,G,B,Type} */
undefined4 block_isSolidType(void) {
  int self;                                        // ECX = BlockColor*
  if (((*(byte*)(self + 3) & 0x1f) != 0) &&        // Byte 3 = Type; unteren 5 Bit = echter Typ
      ((*(byte*)(self + 3) & 0x1f) != 2)) {        // Typ 2 = Wasser -> NICHT fest
    return 1;                                      // alles andere 1..31 = fest
  }
  return 0;                                        // Typ 0 = Luft -> nicht fest
}
```

Die gleiche Pruefung steht ausgeschrieben in `WalkPathBehavior_sampleStepCollision`
(`cube/_library/audit.cpp:37216`) und in der Spielerphysik
(`cube/control/GameController.cpp:101638`).

**Blockabfrage inklusive Sonderfaellen** (`world_getColumnData @0x42F7E0`,
`cube/game_misc/game_misc.cpp:2318`):

```c
undefined* world_getColumnData(x, y, z, world) {
  Field* col = world_getChunkCell256(x, y, world);   // Spalte holen
  if (!col) return &DAT_0076afc4;                    // ausserhalb geladener Welt
  int base = col->minZ;                              // +0x10
  if (z < base)            return &DAT_0076afc4;     // unterhalb der Spalte
  if (base + col->count <= z) {                      // oberhalb der Spalte
     return (z > 0) ? &DAT_0076afc0                  //   ueber Meereshoehe -> LUFT
                    : &DAT_0076afbc;                 //   unter Meereshoehe -> WASSER
  }
  BlockColor* b = col->blocks[z - base];
  // *** Meeresregel ***: leerer Block unter z=1 wird zu Wasser,
  //     ES SEI DENN Bit 0x40 (Bauwerk) ist gesetzt.
  if ((b->Type & 0x1f) == 0 && z < 1 && (b->Type & 0x40) == 0) return &DAT_0076afbc;
  return b;
}
```

Das ist der einzige Ort, an dem Bit 0x40 im Blocktypbyte eine belegte Bedeutung hat:
**"kuenstlich gesetzt"** — verhindert, dass Innenraeume unter Meereshoehe volllaufen.
`WorldInfo_placeStructure` setzt es fuer jeden geschriebenen Bauwerksblock
(`cube/world/WorldInfo.cpp:7128`: `setColor4b(farbe, param_4 | 0x40)`).

**Die Spielerphysik prueft nur Voxel** (`GameController_updateSubsystems @0x60C510`,
`cube/control/GameController.cpp:101636-101656`):

```c
for (bx = xmin; bx <= xmax; bx++)
  for (by = ymin; by <= ymax; by++) {
     world_getColumnData(/* bx,by,bz */);
     if (block_isSolidType())                        // Voxel fest?
        creature->collisionFlags(+0x5C) |= 1;        // Bit 0 = "steht an/auf"
     world_getColumnData();
     if (getBlockType5bits() == 3)                   // Typ 3 = Sondergrund
        inSpecialGround = 1;
  }
```

### 2b. Objekt-Kollision — belegt, und das ist der Kern

Direkt VOR der Voxelschleife laeuft im selben Physiktick eine zweite Schleife
ueber den Ortsindex `zone+0xAC`. Client: `cube/control/GameController.cpp:101256-101340`.
Server (deutlich sauberer dekompiliert): `server/game_misc/game_misc.cpp:45785-45880`.
Beide Fassungen sind identisch — das ist die staerkste Bestaetigung im ganzen Befund.

```c
// Serverfassung, Global::GameController_updateSubsystems @0x5322D0
// server/game_misc/game_misc.cpp:45785 ff.
for (cy = cyMin; cy <= cyMax; cy++)                     // 8-Block-Zellen um die Kreatur
 for (cx = cxMin; cx <= cxMax; cx++) {
   ObjList* cell = World_getVoxelField(world, cx, cy, 0);   // == getTileCell32: zone+0xAC[cx][cy]
   if (!cell) continue;
   for (it = cell->begin(); it != cell->end(); ++it) {
      StaticObject* o = it->obj;                            // Knoten +0x08
      int t = o->type;                                      // +0x00

      // (1) Typen 6, 7, 9 sind NIE fest -- reine Deko/Durchgang
      if (t == 7 || t == 6 || t == 9) continue;

      // (2) Objekte, die diese Kreatur gerade benutzt, werden ignoriert
      //     (Set an creature+0x1468; dort landen Sitz-/Liegemoebel)
      if (creature->usedObjects(+0x1468).find(it) != end) continue;

      // (3) OEFFENBARE Typen {1,2,3,5,8} sind nur fest, solange +0x30 != 0.
      //     +0x30 == 0 bedeutet "offen" -> man laeuft hindurch.
      //     Alle anderen Typen sind IMMER fest.
      if (!((t!=1 && t!=8 && t!=2 && t!=3 && t!=5) || o->state(+0x30) != 0)) continue;

      // (4) Halbmasse = die drei Skalen-Floats
      Vec3 half = o->scale;                     // +0x24, +0x28, +0x2C
      if (o->rotation(+0x20) & 1)               // 90 / 270 Grad
          swap(half.x, half.y);                 // Ausdehnung mitdrehen

      // (5) achsenparallele Box gegen Kreaturbox, alle drei Achsen
      if (  creature.pos.x - creature.half.x <  o->pos.x + half.x
         && creature.pos.x + creature.half.x >  o->pos.x - half.x
         && creature.pos.y - creature.half.y <  o->pos.y + half.y
         && creature.pos.y + creature.half.y >  o->pos.y - half.y
         && creature.pos.z - creature.half.z <  o->pos.z + half.z
         && creature.pos.z + creature.half.z >  o->pos.z - half.z )
      {
          collided = true;                      // local_2bc9 = 1
          canMove  = false;                     // local_2b2d = 0
          goto blocked;                         // Bewegung wird verworfen
      }
   }
 }
```

`Vec3_copy(local_148, piVar10 + 9)` in Zeile `server/game_misc/game_misc.cpp:45810`
kopiert drei Floats ab `int*+9` = Byte-Offset **0x24** — damit ist belegt, dass
`+0x24/+0x28/+0x2C` zusammen ein `Vector3<float>` sind und **alle drei** die
Kollisionsbox aufspannen. Die SDK-Namen `vis_scale / phys_scale / height`
in `cwsdk/non_cube.h:158-160` sind irrefuehrend.

Exakt dasselbe Praedikat noch einmal in `WalkPathBehavior_isAreaWalkable @0x59F0A0`
(`cube/_library/audit.cpp:37016-37020`), dort fuer die KI-Wegfindung, wenn
`param_3 != 0` uebergeben wird.

### 2c. Dritte Kollisionsquelle (Vollstaendigkeit)

Bewegliche Voxelmodelle (Bosse, Plattformen) werden ueber eine `mat4`-Transformation
und `GameController_voxelAt3D` geprueft: `cube/control/GameController.cpp:101126-101196`.
Fuer Tueren irrelevant.

---

## 3. INTERAKTION

### 3a. Wie das Ziel gewaehlt wird — belegt

In `cube::GameController::vfunc_10 @0x488EE0` (Zeilen 34496-45020) laeuft jeden Frame
eine Zielsuche ueber denselben Ortsindex `zone+0xAC`
(`cube/control/GameController.cpp:40297-40425`):

```c
for (cx = px8-1; cx <= px8+1; cx++)                    // px8 = Spielerblock >> 3
 for (cy = py8-1; cy <= py8+1; cy++) {
   cell = getTileCell32(cx, cy, /*zone*/);              // zone+0xAC[cx][cy]
   if (!cell) continue;
   for (node in *cell) {
      StaticObject* o = node->obj;
      d2 = lengthSq(o->pos - player.pos);
      if (d2 > 16.0f) continue;                         // Reichweite = 4 Bloecke
      if (!Entity_stepToward(...)) continue;            // erreichbar?
      // die 8 Ecken der Box (pos +- scale) projizieren:
      //   liegt das Fadenkreuz in der Bildschirmbox -> Kandidat
      // naehestes Objekt gewinnt, Handle {zoneX, zoneY, index} wird gespeichert
   }
 }
```

**Es gibt hier KEINEN Typfilter.** Jedes Statikobjekt in Reichweite kann angezielt
werden; der Typ entscheidet erst danach, was passiert.

### 3b. Was der Typ ausloest — belegt

`cube/control/GameController.cpp:39707-39745`:

```c
GameController_updateSelectedEntity();                  // 0x4889E0
handle = arrayElem_stride4();                           // {zoneX, zoneY, index}
if (*handle < 0) goto weiter;
obj = (int*)resolveVectorElementByHandle();             // -> 0x188-Record
if (!obj) goto weiter;
type = *obj;                                            // ***** Record+0x00 *****

if (type == 0x2D) { flagA = 1; flagB = 1; goto weiter; }        // Sonderobjekt
if (type == 0x4D) { ClearSelection(); zwei Widgets auf; goto weiter; }

if (type != 0x47 && type != 0x48 && type != 0x4B &&
    type != 0x4C && type != 0x49 && type != 0x4A && type != 0x41)
{
    // -- alle uebrigen Typen: Ereignis an die Welt schicken --
    Struct_reset_with256buf();
    ereignis.typ = 3;                                   // 3 = "Statikobjekt benutzen"
    vec3_copy();                                        // + Handle {zoneX,zoneY,index}
    list_pushBack_via451d70();                          // in die Ereignisliste
    GameController_interactSpecialObject();             // Clientseitige Rueckmeldung
    goto weiter;
}
// Werkbank/Amboss/Haendler: UI oeffnen
hasActiveWorldChunk(); setFlagAtSlot();
switch (type) {
  case 0x41: break;
  case 0x47: case 0x49: case 0x4B: GameController_set_field_1b4(); break;
  default: goto ohneInventar;
}
GameController_loadInventoryItems();
```

Damit ist `param_1` von `interactSpecialObject` **der 0x188-Record**, und
`*param_1` ist **der Objekttyp**:

```c
/* Global::GameController_interactSpecialObject @ 0x488030
 * cube/control/GameController.cpp:33893 */
void GameController_interactSpecialObject(int* pObject, undefined4 held) {
  ...
  int type = *pObject;                                   // Record+0x00 = Objekttyp
  if (type != 1 && type != 2 && type != 3 &&             // Tueren
      type != 10 && type != 0x12 && type != 0x10 &&      // Truhe, Bank, Stuhl
      type != 0x13 && type != 0x45)                      // Bett, Liege
  {
      // KEIN Sonderobjekt -> Sprechblase ueber dem Spieler
      u16string_assignCStr(L"There is nothing special.");
      ...
  }
  // fuer die aufgezaehlten Typen: nichts tun, die Welt macht den Rest (siehe 3c)
}
```

**Merke:** Typ `0x44` fehlt in dieser Liste, obwohl der Welt-Handler ihn als Liegemoebel
behandelt (`cube/control/GameController.cpp:96956`). Ein `0x44`-Objekt legt den Spieler
also hin UND zeigt "There is nothing special." — vermutlich ein Originalfehler.

### 3c. Was die Welt daraus macht — belegt

`GameController_updateSubsystems @0x60C510`, Ereigniszweig `case 3`
(`cube/control/GameController.cpp:96905-97050`):

```c
case 3:                                                  // Statikobjekt benutzen
  if (creature->state(+0x68) == 0x4F) { creature->state = 0; ... }   // Aufstehen zuerst
  zone = world_getRegionCell64(ev.zoneX, ev.zoneY);
  if (!zone) break;
  i = ev.index;
  if (i < 0 || i >= zone->static_objects.size()) break;
  StaticObject* o = &zone->static_objects[i];
  int t = o->type;

  if (t == 0x2D) {                                       // verlinktes Objekt anspringen
      // nur wenn diese Kreatur der lokale Spieler ist
      // loest o->link(+0x178..+0x180) auf, Effekt 0x2F, Position uebernehmen
  }
  else if ((t == 0x10 || t == 0x12 || t == 0x13 || t == 0x45 || t == 0x44)
        && o->occupantLo(+0x40) == 0 && o->occupantHi(+0x44) == 0     // frei?
        && (creature->state == 0 || state == 0x53 || state == 0x54))
  {
      o->occupantLo = creature->idLo;                    // Platz belegen
      o->occupantHi = creature->idHi;
      switch (t) {
        case 0x10: case 0x12:                            // Stuhl / Bank
            creature.pos   = o->pos + (0,0, creature.height*0.5 + o->scale.z);
            creature.yaw   = (o->rotation(+0x20) + 2) * 90;
            creature.state = 0x53;                       // 'S' = sitzt
            break;
        case 0x13: case 0x44: case 0x45:                 // Bett / Liege
            creature.pos   = o->pos + (0,0, creature.height*0.5 + o->scale.z);
            creature.yaw   =  o->rotation(+0x20) * 90;
            creature.state = 0x54;                       // 'T' = liegt
            break;
      }
  }
  else {
      if (t == 10 && o->state(+0x30) == 2) { /* leere Truhe: Bloecke rieseln */ }

      if (t == 9 || t == 1 || t == 2 || t == 3 || t == 10) {
          Entity_onStateChangeEmitEffect(o, eventList, newState);   // *** OEFFNEN/SCHLIESSEN ***
          struct_copy3_andCall(); std_list_insert_node_d();         // Nachbau-/Netzmeldung
      }
      if (t == 9) {                                      // Doppeltuer: zweiter Fluegel
          int j = o->link(+0x178);
          if (j >= 0 && j < zone->static_objects.size())
              Entity_onStateChangeEmitEffect(&zone->static_objects[j], ...);
      }
  }
  break;
```

Die Zustandsaenderung selbst (`cube/game_misc/game_misc.cpp:19820`):

```c
/* Global::Entity_onStateChangeEmitEffect @ 0x5A58E0
 * entity ist der 0x188-Record. */
void Entity_onStateChangeEmitEffect(int* o, int eventList, char newState) {
  if ((char)o[0xc] == newState) return;             // o[0xc] = +0x30 = Zustandsbyte
  o[0xd] = clamp(1000 - o[0xd], 0, ...);            // +0x34 = Animationszeit, wird gespiegelt
  *(char*)(o + 0xc) = newState;                     // +0x30 neu setzen  <-- KOLLISION AENDERT SICH HIER
  int t = o[0];                                     // +0x00 Typ -> Geraeusch
  if      (t == 5) sound = 0x33;
  else if (t == 6) { if (!newState) return; sound = 0x35; }
  else if (t == 7) { if (!newState) return; sound = 0x34; }
  else             sound = 0x36;                    // u.a. Tueren 1/2/3 und Truhe 10
  // Klang an der Objektposition (o+0x08/0x10/0x18, Fixed16) in die Effektliste
}
```

**Damit schliesst sich der Kreis:** Interaktion schaltet `+0x30` um, und `+0x30`
ist genau das Bit, das die Kollisionsschleife aus 2b bei den Typen `{1,2,3,5,8}` abfragt.
Tuer zu (`+0x30 = 1`) = fest. Tuer auf (`+0x30 = 0`) = durchlaessig.

---

## 4. Der 0x188-Record (StaticObject)

Belegt ueber `Struct_InitDefaults @0x58EAA0` (`cube/game_misc/game_misc.cpp:16674`),
`spawnRandomObject7 @0x5BF5B0` (`cube/game_misc/game_misc.cpp:30766`) und die Nutzer oben.

| Offset | Typ | Bedeutung | Beleg |
|---|---|---|---|
| +0x00 | int | **Objekttyp** | `GameController.cpp:39709`, `96925` |
| +0x04 | int | Flags (Bit 0 = Drehungsparitaet fuer Hausteile) | `Structure_getWidthByRotation`, `game_misc.cpp:19886` |
| +0x08 | int64 | Position X (Fixed16) | `spawnRandomObject7`, `game_misc.cpp:30781` |
| +0x10 | int64 | Position Y | dito |
| +0x18 | int64 | Position Z | dito |
| +0x20 | int | **Drehung, 0..3 Vierteldrehungen** | `GameController.cpp:96975` (`(rot+2)*0x5A`) |
| +0x24 | float | Skala/Halbmasse X | `server/game_misc/game_misc.cpp:45810` |
| +0x28 | float | Skala/Halbmasse Y | dito |
| +0x2C | float | Skala/Halbmasse Z | dito |
| +0x30 | **byte** | **Zustand. Grundwert 1. 0 = offen/durchlaessig, 2 = Truhe geplündert** | `game_misc.cpp:16678`, `19833`, `39050` |
| +0x34 | int | Animationszeit 0..1000 | `game_misc.cpp:19829` |
| +0x38 | int | 0 | `game_misc.cpp:16681` |
| +0x40 | int | **Belegung lo (Kreatur-Id)**, 0 = frei | `GameController.cpp:96958` |
| +0x44 | int | Belegung hi, 0 = frei | dito |
| +0x48..0x54 | | `std::vector` (Stride 0x0C) | `WorldInfo.cpp:5619` |
| +0x58..0x174 | | eingebetteter Item-/Entity-Zustand (`entityState_copy`) | `WorldInfo.cpp:5621` |
| +0x178 | int | **Link auf anderes Objekt: zoneX**, Grundwert -1 | `game_misc.cpp:16683` |
| +0x17C | int | Link: zoneY, Grundwert -1 | dito |
| +0x180 | int | Link: index, Grundwert -1 | dito |
| +0x184 | int | 0 | dito |

Grundwerte einer frischen Instanz:

```c
/* Global::Struct_InitDefaults @ 0x58EAA0  == StaticObject::initDefaults
 * cube/game_misc/game_misc.cpp:16674 */
void Struct_InitDefaults(void) {
  int self;
  *(byte*)(self + 0x30) = 1;      // Zustand: GESCHLOSSEN / intakt  -> Objekt ist fest
  *(int*) (self + 0x34) = 0;      // Animationszeit
  *(int*) (self + 0x38) = 0;
  *(int*) (self + 0x40) = 0;      // Belegung frei
  *(int*) (self + 0x44) = 0;
  struct_initWithBuf();           // eingebetteter Item-Block
  *(int*) (self + 0x178) = -1;    // kein verlinktes Partnerobjekt
  *(int*) (self + 0x17c) = -1;
  *(int*) (self + 0x180) = -1;
  *(int*) (self + 0x184) = 0;
}
```

---

## 5. Woher die Objekte kommen: Markerfarben in der Hausvorlage

**Antwort auf Frage 3: Ein natives Dorfhaus hat an der Tuerstelle KEINEN Block.
Die Vorlage enthaelt dort einen ROTEN Markervoxel (255,0,0), und der Generator
tauscht ihn gegen ein Statikobjekt vom Typ 1 (in `zone == 7`: Typ 2) aus,
ohne einen Block zu schreiben.**

`WorldInfo_placeStructure @0x5F0CE0` (`cube/world/WorldInfo.cpp:5097`) liest eine
RGB-Vorlage (`tmpl+0x30`, 3 Byte je Voxel, Masse `tmpl+0x44/+0x48/+0x4C`) und
vergleicht jeden Voxel gegen eine Kette von Markerfarben.

```c
// Kern der Schleife, cube/world/WorldInfo.cpp:5580-5648
for (i = 0; i < 3; i++)
   if (tmplColor[i] != TRANSPARENT[i]) {                 // DAT_0076B340
       ... Bodenpruefungen ...
       if (zone not in {1..7, 9, 10, 0xB, 0xC, 0xD, 0xE, 0xF}) goto BLOCK_SCHREIBEN;
       marker = {0xFF, 0x00, 0x00};                      // erste Markerfarbe: ROT
       goto VERGLEICH_ROT;
   }
// alle drei gleich -> Voxel ist transparent, es passiert gar nichts
```

### Markertabelle (belegt, alle Zeilen aus `cube/world/WorldInfo.cpp`)

| Markerfarbe (R,G,B) | erzeugter Objekttyp | Zeile Markerkonstante | Zeile Typzuweisung |
|---|---|---|---|
| `DAT_0076B340` (transparent) | keiner, kein Block | — | — |
| **(255, 0, 0)** | **1**, in `zone == 7` **2** — **TUER** | 5647 | 5726 |
| (0, 255, 0) | 3 | 5691 | 5867 |
| (0, 127, 127) | 0x29..0x2B (Zufall, nur `rand()%5==0`) | 5855 | 5932 |
| (255, 127, 0) | 0x13 **(Bett)** + zusaetzlich 0x14 | 5910 | 5996 / 6014 |
| (255, 127, 127) | 0x20..0x22 (Zufall) | 5986 | 6073 |
| (127, 127, 0) | 0x0C + zusaetzlich 0x10 **(Stuhl)** | 6062 | 6134 / 6156 |
| (255, 255, 0) | 0x0D | 6125 | 6257 |
| (63, 0, 0) | 0x30 | 6246 | 6320 |
| (0, 0, 255) | Handwerks-/Dienstobjekt, Untermarker + `zone` entscheiden | 6310 | 6379, 6680, 6717, 6750, 6786, 6820, 6853, 6930, 6969 |
| alles andere | **kein Objekt, stattdessen Block schreiben** mit `Typ = param_4 \| 0x40` | — | 7108-7130 |

Der Tuerzweig im Klartext (`cube/world/WorldInfo.cpp:5700-5810`):

```c
// Marker ROT (255,0,0) hat gepasst -> Tuer statt Block
local_428 = 1;
local_460 = 1;                       // +0x30 Zustand = 1 = GESCHLOSSEN
local_45c = 0;                       // +0x34 Animation
local_450 = 0;                       // +0x40/+0x44 Belegung frei
local_318 = local_314 = uStack_310 = -1;   // +0x178..0x180 kein Link
...
objType = (zone == 7) + 1;           // *** Typ 1, in zone 7 Typ 2  (Zeile 5726)
...                                  // Position/Skala/Drehung fuellen
push_back in zone->static_objects;   // Stride 0x188
...
tilemap_writeGlyphColumn();          // nur der Kartenpunkt
goto LAB_005f4a7e;                   // *** SPRINGT AN DER BLOCKSCHREIBUNG VORBEI ***
```

Zum Vergleich der Normalfall (`cube/world/WorldInfo.cpp:7108-7130`):

```c
LAB_005f493b:                        // kein Marker -> normaler Bauwerksblock
   ...
   setColor4b(vorlagenFarbe, param_4 | 0x40);   // Typbyte = Bauwerkstyp | Bit 0x40
   color_copy4(); vec3_copy(&pos);
LAB_005f4a46:
   setColor4b();
   arrayElem_stride4(); arrayElem_stride4(1); arrayElem_stride4();   // Block in die Spalte
```

`LAB_005f4a7e` (Zeile 7104) ist nur der Schleifenzaehler — **kein Block**.
Die Handwerksobjekte (0x47..0x4D) laufen dagegen ueber `LAB_005f3377` (Zeile 6983)
und schreiben zusaetzlich einen Block.

Die Sitz-/Schlafplaetze eines Hauses werden anschliessend in `Chunk_generateObjects @0x5AF170`
eingesammelt (`cube/game_misc/game_misc.cpp:28108-28121`):

```c
for (i = 0; i < zone->static_objects.size(); i++) {
   o = &zone->static_objects[i];
   if (o->type == 0x10 || o->type == 0x12)      // Stuhl / Bank
       house->daySpots(+0x48).push_back({zoneX, zoneY, i});
   if (o->type == 0x13)                          // Bett
       house->nightSpots(+0x54).push_back({zoneX, zoneY, i});
}
```

---

## 6. Zonen-Finalisierung (Frage 4) — belegt

**Ja, es gibt sie:** `WorldInfo_generateBiomeContent @0x5E4850`, Endteil in
`cube/world/WorldInfo.cpp:5048-5080`.

```c
VoxelChunk_propagateSunlight(...);                 // Licht setzen  (Zeile 5027)
... Hoehenwerte je Objekt setzen ...               // Zeilen 5030-5045

// *** Ortsindex zone+0xAC aufbauen ***
for (i = 0; i < zone->static_objects.size(); i++) {
    o     = &zone->static_objects[i];
    cellX = o->pos.x / (8 * 65536);                // Blockkoordinate / 8
    cellY = o->pos.y / (8 * 65536);
    cell  = getTileCell32(cellX, cellY, zone);     // zone+0xAC[cx][cy], 32x32
    if (cell) {
        vec3_set(cellX, cellY, i);                 // Handle {zoneX, zoneY, index}
        set4(o, ...);                              // Wert = {StaticObject*, handle[3]}
        WorldInfo_mapInsertEntry();                // Knoten in die Zellenliste
    }
}

// *** erst JETZT wird die Zone im Weltgitter sichtbar ***
region->zones[0x10018 + tileIndex * 4] = zone;     // Zeile 5077
```

`WorldInfo_mapInsertEntry @0x5F4B50` (`cube/world/WorldInfo.cpp:7144`) haengt den
Knoten per `map_insertVec4Node` an — "Vec4" = die vier Dwords
`{objPtr, zoneX, zoneY, index}`, genau das, was 2b/3a auslesen.

Die Umkehrfunktion ist `WorldGrid_destroyBlockObjectAt @0x5A4890`
(`cube/world/WorldInfo.cpp:840`): nullt den Zonenzeiger unter zwei kritischen
Abschnitten (`world+0x8000D8`, `world+0x8000C0`) und ruft dann den Destruktor.

**Konsequenz fuer den Mod:** Ein 0x188-Record, den man nachtraeglich in
`zone->static_objects` schiebt, ist unsichtbar fuer Kollision *und* Interaktion,
solange kein passender Knoten in `zone+0xAC` haengt. Beide Wege — Physik (2b) und
Zielsuche (3a) — gehen ausschliesslich ueber den Ortsindex, nie ueber den Vektor.

---

## 7. Typwerte

### 7a. Blocktyp (`BlockColor.Type`, Byte 3)

Bits 0..4 = Typ, Bits 5..7 = Flags.

| Wert (`& 0x1F`) | Bedeutung | Beleg |
|---|---|---|
| 0 | Luft: nicht sichtbar, nicht fest | `block_isSolidType` `GameController.cpp:13655`; Mesher `ChunkBuffer.cpp:752` |
| 2 | Wasser: sichtbar, **nicht fest**, Licht geht durch | `block_isSolidType`; `Terrain_sampleBlockBrightness` `game_misc.cpp:19780` |
| 3 | Sondergrund: eigener Schritteffekt (0x21..0x23 statt 0x20), eigener Zaehler `creature+0x1198` | `GameController.cpp:101645`, `102209` |
| 0x0D | Lichtquelle: Helligkeit fest 255 | `game_misc.cpp:19779`; `ChunkBuffer.cpp:2238` |
| 1, 4..0x0C, 0x0E..0x1F | fest | Umkehrschluss aus `block_isSolidType` |

| Flagbit | Bedeutung | Beleg |
|---|---|---|
| 0x40 | "kuenstlich gesetzt": Luftblock unter z=1 wird NICHT zu Wasser | `world_getColumnData` `game_misc.cpp:2338`; `getFlagBit6 @0x4348C0` |
| 0x20, 0x80 | unbekannt | siehe UNSICHER |

### 7b. Objekttyp (`StaticObject+0x00`)

**Kollisionsklassen (belegt, `server/game_misc/game_misc.cpp:45797-45807`):**

| Klasse | Typen | Verhalten |
|---|---|---|
| nie fest | 6, 7, 9 | laeuft man immer durch |
| schaltbar fest | **1, 2, 3, 5, 8** | fest solange `+0x30 != 0` |
| immer fest | alle uebrigen | Box aus `+0x24/0x28/0x2C` |

**Bekannte Einzeltypen:**

| Typ | Bedeutung | Beleg |
|---|---|---|
| 1 | **Tuer** (Standardhaus) | `WorldInfo.cpp:5726`; Interaktion `GameController.cpp:96952`; Kollision s.o. |
| 2 | **Tuer**, Variante fuer `zone == 7` | `WorldInfo.cpp:5726` |
| 3 | Tuerartig, aus Marker (0,255,0) | `WorldInfo.cpp:5867`; im Sonderobjekt-Set von `interactSpecialObject` |
| 5 | schaltbar, eigener Klang 0x33 | `game_misc.cpp:19844` |
| 6 | nie fest, Klang 0x35 nur beim Oeffnen | `game_misc.cpp:19846` |
| 7 | nie fest, Klang 0x34 nur beim Oeffnen | `game_misc.cpp:19850` |
| 8 | schaltbar fest | `server/game_misc/game_misc.cpp:45806` |
| 9 | nie fest, hat **Partnerobjekt** ueber `+0x178` (Doppeltuer) | `GameController.cpp:97037-97043` |
| 0x0A (10) | **Truhe**: Loot beim Oeffnen, danach `+0x30 = 2` | `game_misc.cpp:39025-39050`; `GameController.cpp:97001` |
| 0x0C | Deko aus Marker (127,127,0) | `WorldInfo.cpp:6134` |
| 0x0D | Deko aus Marker (255,255,0) | `WorldInfo.cpp:6257` |
| 0x10 (16) | **Stuhl** — Sitzen, Zustand `0x53 'S'`, Blick `(rot+2)*90` | `GameController.cpp:96966-96977`; `spawnRandomObject7` Skala (1, 1, 0.5) |
| 0x12 (18) | **Bank** — wie Stuhl | dito; Skala (3, 1, 0.4) |
| 0x13 (19) | **Bett** — Liegen, Zustand `0x54 'T'`, Blick `rot*90` | `GameController.cpp:96978-96990`; `WorldInfo.cpp:5996` |
| 0x14 | Beistellobjekt zum Bett | `WorldInfo.cpp:6014` |
| 0x18..0x1C | Zufallsdeko (Skalen fest im Code) | `spawnRandomObject7`, `game_misc.cpp:30789-30833` |
| 0x20..0x22 | Zufallsdeko | `WorldInfo.cpp:6073` |
| 0x29..0x2B | Zufallsdeko (nur 1 von 5) | `WorldInfo.cpp:5932` |
| 0x2D (45) | Sonderobjekt mit Linkziel, Effekt 0x2F, versetzt den Spieler | `GameController.cpp:96928-96950`, `39712` |
| 0x2F..0x31 | Zufallsdeko | `WorldInfo.cpp:6969`, `6320` |
| 0x38..0x40 | Zufallsdeko, 9 Varianten | `WorldInfo.cpp:6680` |
| 0x41 (65) | Dienst-/Handwerksobjekt, oeffnet UI ohne `set_field_1b4` | `GameController.cpp:39743` |
| 0x44 (68) | Liegemoebel (siehe Fehler in 3b) | `GameController.cpp:96956`, `96982` |
| 0x45 (69) | Liegemoebel | dito, und im Sonderobjekt-Set |
| 0x47, 0x49, 0x4B | Handwerksstation mit `set_field_1b4` + Inventar | `GameController.cpp:39749-39754`; `WorldInfo.cpp:6820/6930/6853` |
| 0x48, 0x4A, 0x4C | Handwerksstation ohne `set_field_1b4` | `GameController.cpp:39745`; `WorldInfo.cpp:6717/6786/6750` |
| 0x4D (77) | Sonderstation, oeffnet zwei Widgets | `GameController.cpp:39717`; `WorldInfo.cpp:6379` |

**Sonderobjekt-Set von `interactSpecialObject`** (alles andere ergibt
"There is nothing special."): `{1, 2, 3, 0x0A, 0x10, 0x12, 0x13, 0x45}` —
`cube/control/GameController.cpp:33928-33930`.

### 7c. Kreaturzustand `creature+0x68` (soweit belegt)

| Wert | Bedeutung | Beleg |
|---|---|---|
| 0x53 `'S'` | sitzt auf 0x10/0x12 | `GameController.cpp:96977`; `RandomInteractionBehavior.cpp:90` |
| 0x54 `'T'` | liegt auf 0x13/0x44/0x45 | `GameController.cpp:96990`; `RandomInteractionBehavior.cpp:90` |
| 0x4F | wird beim Interaktionsereignis zurueckgesetzt | `GameController.cpp:96906` |

---

## 8. Handlungsanweisung fuer den Mod

Ziel: eine Tuer, die fest ist, sich oeffnen laesst und dann durchlaessig wird.

### Schritt 1 — Record anlegen und in `zone->static_objects` schieben

```cpp
zone_static_object o;                 // 0x188 Byte, cwsdk/non_cube.h:152
memset(&o, 0, sizeof(o));

o.type            = 1;                // TUER. 2 waere die zone-7-Variante.
                                      // 6/7/9 waehlen heisst: nie fest.
o.position.x      = (int64_t)blockX << 16;   // Fixed16: Bloecke * 65536
o.position.y      = (int64_t)blockY << 16;
o.position.z      = (int64_t)blockZ << 16;
o.cuwo_orientation= rot & 3;          // +0x20, 0..3 Vierteldrehungen.
                                      // Ungerade Werte tauschen X/Y der Kollisionsbox.

// +0x24/+0x28/+0x2C: HALBMASSE der Kollisionsbox in Bloecken (Float, nicht Fixed).
// Nicht "nur Optik" -- Punkt 2b liest genau diese drei Felder.
*(float*)((char*)&o + 0x24) = 0.5f;   // halbe Breite  -> 1 Block breit
*(float*)((char*)&o + 0x28) = 0.1f;   // halbe Tiefe   -> duennes Tuerblatt
*(float*)((char*)&o + 0x2C) = 1.5f;   // halbe Hoehe   -> 3 Bloecke hoch

*(uint8_t*)((char*)&o + 0x30) = 1;    // ZUSTAND 1 = zu = FEST. 0 waere offen.
*(uint32_t*)((char*)&o + 0x34) = 0;   // Animationszeit
*(uint32_t*)((char*)&o + 0x40) = 0;   // Belegung frei
*(uint32_t*)((char*)&o + 0x44) = 0;
*(int32_t*)((char*)&o + 0x178) = -1;  // kein Partnerfluegel
*(int32_t*)((char*)&o + 0x17c) = -1;
*(int32_t*)((char*)&o + 0x180) = -1;

size_t index = zone->static_objects.size();
zone->static_objects.push_back(o);
```

### Schritt 2 — Pflicht: Eintrag im Ortsindex `zone+0xAC`

**Ohne diesen Schritt passiert nichts.** Weder Physik (2b) noch Zielsuche (3a)
lesen den Vektor; beide gehen nur ueber den Index.

```cpp
// getTileCell32 @0x42F640, thiscall auf dem World-Objekt:
//   int getTileCell32(uint cellX, uint cellY, Zone* zoneOrNull)
// cellX/cellY sind Weltblock >> 3 (8x8-Blockzellen).
int cellX = blockX >> 3;
int cellY = blockY >> 3;
void* cell = getTileCell32(cellX, cellY, zone);     // liefert &zone->objIndex[cx][cy]

// Wert des Listenknotens (16 Byte):
struct ObjRef { void* obj; int zoneX; int zoneY; int index; };
ObjRef ref;
ref.obj    = &zone->static_objects[index];
ref.zoneX  = blockX >> 8;      // ZONENkoordinate, wie world_getRegionCell64 sie erwartet
ref.zoneY  = blockY >> 8;
ref.index  = (int)index;
// per std::list push_back in *cell einhaengen
// (Original: WorldInfo_mapInsertEntry @0x5F4B50 -> map_insertVec4Node)
```

Wichtig: `ref.obj` muss nach jedem `push_back` auf `static_objects` neu gesetzt
werden, sonst zeigen alte Eintraege nach einer Reallokation ins Leere.
Deshalb erst alle Objekte anlegen, dann den Index bauen — genau die Reihenfolge,
die `WorldInfo_generateBiomeContent` benutzt (`cube/world/WorldInfo.cpp:5048`).

### Schritt 3 — die Wand ringsum

Die Tuer selbst braucht **keinen** Block. Fuer die Wand:

```cpp
BlockColor b;
b.Red = r; b.Green = g; b.Blue = bl;
b.Type = (steinTyp & 0x1F) | 0x40;   // 0x40 = Bauwerk, verhindert Wasser unter z<1
                                     // steinTyp != 0 und != 2, sonst nicht fest
```
Die Tuerlichtung selbst auf `Type = 0` (Luft) lassen — sonst blockiert der Block
auch dann, wenn die Tuer offen ist.

### Schritt 4 — Verhaltensmatrix zur Kontrolle

| Was ich will | `type` | `+0x30` |
|---|---|---|
| Tuer geschlossen, fest, benutzbar | 1 | 1 |
| Tuer offen, durchlaessig | 1 | 0 |
| Truhe, benutzbar, immer fest | 0x0A | 1 (2 = leer) |
| Stuhl, setzt den Spieler hin | 0x10 | 1 |
| Bett, legt den Spieler hin | 0x13 | 1 |
| Werkbank/Amboss, oeffnet UI, immer fest | 0x47 / 0x48 / 0x4B ... | 1 |
| Deko, fest, aber "nichts Besonderes" | 0x18..0x1C, 0x20..0x22 | 1 |
| Deko, komplett durchlaessig | 6, 7 oder 9 | egal |

### Schritt 5 — Doppeltuer

Typ **9** setzen und die beiden Fluegel gegenseitig verlinken:
`fluegelA->+0x178/0x17C/0x180 = {zoneX, zoneY, indexB}` und umgekehrt.
Der Welt-Handler schaltet dann beide zusammen
(`cube/control/GameController.cpp:97037-97043`).
Achtung: Typ 9 ist laut Kollisionspraedikat **nie fest** — eine Doppeltuer
blockiert im Original also nicht.

---

## 9. UNSICHER

Alles hier ist nicht durch Datei+Zeile abgesichert, sondern Auslegung oder Luecke.

1. **Bedeutung von Blocktyp 3.** Belegt ist nur: eigener Schritteffekt (0x21..0x23)
   und ein eigener Zaehler `creature+0x1198`, der mit 0.05/s laeuft.
   *Vermutung*: Schnee oder Sumpf. Nicht geprueft.
2. **Blocktyp-Flagbits 0x20 und 0x80.** Keine Fundstelle. Nur 0x40 ist belegt.
3. **Namen der Blocktypen 1, 4..0x0C, 0x0E..0x1F.** Nur "fest" ist belegt,
   keine Zuordnung zu Gras/Stein/Holz.
4. **Die RGB-Werte der Untermarker im Blau-Zweig** (`WorldInfo.cpp:6359-6982`).
   Ghidra hat die Argumente von `setColor3b()/color_equals3()` verloren
   (Immediates in Registern). Die *Objekttypen* dieses Zweigs sind belegt,
   die zugehoerigen Farben nicht. Muessten aus der Disassembly von 0x5F2F80..0x5F33B0
   nachgelesen werden.
5. **`DAT_0076B340`** — die Transparenzfarbe der Vorlage. Wert nicht im Quelltext.
   Ebenso `DAT_0076AFBC/AFC0/AFC4` und `DAT_0076B7CC/B7D0/B7D4` (Ersatzbloecke).
   Ihre *Rolle* ist aus dem Kontrollfluss belegt, ihr Byteinhalt nicht.
6. **Bedeutung des `zone`-Parameters** von `WorldInfo_placeStructure`
   (Werte 1..0xF). *Vermutung*: Dorf-/Biom-/Berufsart. Belegt ist nur, dass er
   den Objekttyp mitbestimmt (`(zone == 7) + 1` bei der Tuer, und die
   Handwerksstation-Auswahl bei 0xD/0xE/0xF).
7. **`creature+0x1468`** — das Set, dessen Treffer die Objektkollision ueberspringt.
   *Vermutung*: die gerade benutzten Sitz-/Liegemoebel. Der Einfuegeort wurde
   nicht gesucht.
8. **`+0x178..0x180` als Handle.** `Struct_InitDefaults` setzt drei Dwords auf -1,
   was zu {zoneX, zoneY, index} passt. Die Leseseite
   (`GameController.cpp:96938`, `97038`) liest aber `+0x178` direkt als Index
   und ruft `world_getRegionCell64` mit Werten, deren Herkunft Ghidra verloren hat.
   Entweder ist es ein Dekompilat-Artefakt oder das Handle liegt bei +0x170.
   **Vor Gebrauch in der Disassembly nachpruefen.**
9. **Reihenfolge der Markerkette.** Sie ist aus dem `goto`-Geflecht rekonstruiert
   (jede Markerkonstante wird unmittelbar vor dem Sprung auf ihr Vergleichslabel
   gesetzt). Die Zuordnung Farbe -> Typ ist damit hergeleitet, nicht gemessen.
   Der Tuerzweig (255,0,0) -> Typ 1 ist die sicherste Zeile der Tabelle, weil
   Markerkonstante (5647), Label (5688) und Typzuweisung (5726) direkt aufeinanderfolgen.
10. **Zustand `+0x30 == 2` bei Nicht-Truhen.** Belegt nur fuer Typ 10
    (`game_misc.cpp:39050`). Ob andere Typen den Wert 2 nutzen, ist offen.
11. **Genaue Halbmassen-Semantik.** Belegt: die drei Floats spannen die Box auf und
    die Sitzhoehe ist `obj.pos.z + scale.z`. Ob die Box `pos ± scale` oder
    `pos ± scale*0.5` ist, haengt an `ftol2()`, dessen Skalierung nicht im
    Quelltext steht. Die Sitzformel spricht fuer `pos ± scale`.
    **Im Spiel messen, bevor man sich darauf verlaesst.**
