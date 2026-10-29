# RE: Der Ortsindex `zone+0xAC` — Signaturen, Layout, Nachbau

Quelle: Ghidra-Decompile-Baum `<Downloads>\CubeWorld-Reversal-master-new`.
Client = `cube/`, Server = `server/`. ImageBase 0x400000, 32-Bit MSVC.
Alle Zeilenangaben beziehen sich auf diesen Baum.

---

## BLUF

**Ja, ein Mod darf `getTileCell32` und `WorldInfo_mapInsertEntry` jederzeit nach der
Zonengenerierung aufrufen — beide sind reine Container-Operationen ohne
Generierungszustand.** Drei Dinge muessen aber stimmen, sonst schreibt man in fremden
Speicher:

1. **Beide Funktionen sind `__thiscall`.** `getTileCell32` will `ECX = World*`,
   `WorldInfo_mapInsertEntry` will `ECX = std::list<ObjRef>*` (die Zelle selbst).
2. **Der Listenkopf ist 8 Byte gross, nicht 12**: `{ _Myhead, _Mysize }`.
   Der bestehende Mod-Code `MountainVillage.cpp:1231-1253` (`IndexInsert`) liest
   `lst[1]` als Head und schreibt `lst[2]` als Size — das ist **um ein Feld verschoben**
   und zerstoert den Kopf der *naechsten* Indexzelle. Siehe Abschnitt 7.
3. **`zone+0xAC` ist ein ZEIGER auf ein Heap-Array von 1024 Listenkoepfen**, nicht das
   Array selbst. `getTileCell32` dereferenziert `*(void**)(zone+0xAC)`.

---

## 1. Signaturtabelle

| Adresse | Name | Signatur | Konvention | Stack-Cleanup |
|---|---|---|---|---|
| 0x0042F640 | `getTileCell32` | `std::list<ObjRef>* __thiscall f(World* this, uint cellX, uint cellY, Zone* zoneOrNull)` | **__thiscall** | callee, `ret 0Ch` (hergeleitet) |
| 0x005F4B50 | `WorldInfo_mapInsertEntry` | `void __thiscall f(std::list<ObjRef>* this, const ObjRef* value)` | **__thiscall** | callee, `ret 4` (hergeleitet) |
| 0x00608A20 | `map_insertVec4Node` | `Node* f(Node* next, Node* prev, const uint32_t value[4])` | **__cdecl** (kein Registerlesen) | caller |
| 0x0046D590 | `list_buyNode_18` | `Node* f(Node* next, Node* prev)` | **__cdecl** (kein Registerlesen) | caller |
| 0x005A0910 | `resolveVectorElementByHandle` | `StaticObject* __thiscall f(World* this, const uint32_t handle[3])` | **__thiscall** | callee, `ret 4` (hergeleitet) |
| 0x00434D10 | `world_getRegionCell64` | `Zone* __thiscall f(World* this, uint zoneX, uint zoneY)` | **__thiscall** | callee, `ret 8` (hergeleitet) |
| 0x00594B40 | `std_vector_Reserve_grow_188` | `void __thiscall f(vector* this, uint additionalCount)` | **__thiscall** | callee, `ret 4` (hergeleitet) |

### 1.1 Konventionsbeleg je Funktion

Der Baum enthaelt **nur Ghidra-Pseudocode, keine Disassembly und keine Binary**
(`find … -iname "*.exe"` ueber alle vier eingehaengten Ordner: 0 Treffer).
Der vom Auftrag geforderte Direktbeleg „steht hinter dem CALL ein `add esp,N`?"
ist aus diesem Material **nicht** fuehrbar. Statt zu raten, hier die drei Belegarten,
die das Material hergibt — und in Abschnitt 8 die exakte Nachpruefanweisung.

**Ghidra-Konvention in diesem Baum:** eine im Rumpf *uneingeleitet gelesene* lokale
Variable ist ein Registereingang. Der Baum benennt sie teils `in_ECX`, teils
umbenannt (`self`, `this`, `list`). Nachweis, dass das ECX ist:
`cube/_library/audit.cpp:5127-5133` (`vector_at_0x188`: `int *in_ECX;
return param_1 * 0x188 + *in_ECX;`) und `cube/control/GameController.cpp:1347-1353`
(`arrayElem_stride8`: `int self; return self + index*8;`) — beides klassische
`__thiscall`-Getter.

#### `getTileCell32 @0x42F640` — __thiscall(World*, cellX, cellY, Zone*)

* **Beleg A (im Client, zwingend):** `cube/control/GameController.cpp:10275-10277`
  ruft `world_getRegionCell64(cellX>>5, cellY>>5)` mit **zwei** Argumenten auf.
  `world_getRegionCell64` seinerseits ruft `region_getCell1024(a,b)`
  (`cube/game_misc/game_misc.cpp:2419`), und `region_getCell1024` liest
  `self+0xbc` aus **ECX** (`cube/control/GameController.cpp:12769-12777`).
  Weder `getTileCell32` noch `world_getRegionCell64` setzen ECX. Also wird ECX
  von aussen durchgereicht ⇒ beide sind `__thiscall` auf demselben `World*`.
  (`World+0xBC` = `Region* main_regions[1024][1024]` — deckt sich mit
  `cwsdk/cube/World.h:35`.)
* **Beleg B (Server-Zwilling, von Ghidra annotiert):** dieselbe Funktion im Server ist
  `server/_library/crt_stl.cpp:8968`:
  `int __thiscall World_getVoxelField(void *this, uint param_1, uint param_2, int param_3)`
  @0x41C9E0. Der Rumpf ist Zeile fuer Zeile identisch mit dem Client
  (`0x1fffff`-Grenze, `+0x60`/`+0x64`-Zonenursprung, `+0xac`-Basis, `*8`-Stride)
  und ruft dort `Region_getChunkCell(this, …)`, das Ghidra ebenfalls als
  `__thiscall` typisiert hat (`server/game_misc/game_misc.cpp:595`).
* **Aufrufstellen:** `cube/control/GameController.cpp:11700, 11827, 11954, 12081,
  12208, 86345` zeigen drei Stackargumente (`getTileCell32(x, y, 0)`);
  das `this` ist bei Ghidras Default-`__cdecl`-Modell unsichtbar.
* **Konsequenz:** wird `zoneOrNull != 0` uebergeben, wird `this` im Rumpf **nie**
  dereferenziert (der `this`-Zweig liegt komplett in `if (chunk == 0)`).
  Trotzdem muss der Aufrufer 3 Argumente pushen und die Konvention
  `__thiscall` benutzen, weil der Callee den Stack raeumt.

#### `WorldInfo_mapInsertEntry @0x5F4B50` — __thiscall(std::list<ObjRef>*, const ObjRef*)

* **Rumpf** (`cube/world/WorldInfo.cpp:7146-7160`): deklariert `int *list;` **ohne
  Zuweisung** und benutzt es als Erstes (`head = *list;`). Das ist der ECX-Eingang.
  `value` ist der einzige Stackparameter (Header: `cube/world/WorldInfo.h:15`).
* **Beleg (Server-Zwilling, von Ghidra annotiert):** byte-gleiche Funktion
  `server/_library/crt_stl.cpp:18141`:
  `void __thiscall std_list_push_back_428450(void *this, undefined4 *param_1)` @0x428450.
  Identische fuenf Anweisungen inkl. `0xaaaaaa9`-Laengenpruefung, identische
  Reihenfolge. Zwei weitere Klone: `server/_library/crt_stl.cpp:57341` (`lib_fn_5283b0`)
  und `:60578` (`lib_fn_5307f0`) — alle drei `__thiscall`.
* **Semantik:** das ist schlicht `std::list<T>::push_back`. Nichts anderes.

#### `map_insertVec4Node @0x608A20` — __cdecl(next, prev, value4)

* **Rumpf** (`cube/_library/audit.cpp:41757-41770`): drei benannte Parameter, **kein**
  uneingeleitetes Register. Waere es `__thiscall`, muesste `WorldInfo_mapInsertEntry`
  vor dem Aufruf ECX setzen — es tut das nicht, und Ghidra haette im Rumpf
  ein `in_ECX` gezeigt.
* Ruft `list_buyNode_18(next, prev)` (`operator new(0x18)`,
  `cube/_library/audit.cpp:10323-10341`) und schreibt danach vier Dwords auf
  `node+0x08 … node+0x14`.
* **Server-Zwilling:** `std_map_insert_node_list(p1,p2,p3)`
  (`server/_library/crt_stl.cpp:11832`) — von Ghidra **nicht** als `__thiscall`
  markiert, gleiche 3-Parameter-Form.
* `__cdecl` vs. `__stdcall` ist ohne Disassembly **nicht** unterscheidbar → UNSICHER (9.1).
  Fuer den Mod irrelevant: man ruft `WorldInfo_mapInsertEntry`, nicht diese Funktion.

#### `resolveVectorElementByHandle @0x5A0910` — __thiscall(World*, uint32_t handle[3])

* **Rumpf** (`cube/control/GameController.cpp:83654-83667`):
  ```c
  chunk = world_getRegionCell64(*query, query[1]);   // zwei Argumente
  ```
  `world_getRegionCell64` braucht `ECX = World*` (siehe oben). `resolveVectorElementByHandle`
  setzt ECX nirgends ⇒ es reicht sein eigenes `this` durch ⇒ **__thiscall**.
  **Das ist der praxisrelevanteste Fund dieses Abschnitts:** wer die Funktion als
  `__cdecl` typedeft, laeuft mit Muell in ECX in `region_getCell1024` und stuerzt ab.
* Ein Stackparameter: Zeiger auf drei `int32` `{zoneX, zoneY, index}`.
* Rueckgabe: `StaticObject*` (`objBegin + index*0x188`) oder `0`.
* Bounds: `index >= 0` und `index < (zone+0x10 - zone+0xC)/0x188`.

#### `world_getRegionCell64 @0x434D10` — __thiscall(World*, zoneX, zoneY)

* `cube/game_misc/game_misc.cpp:2412-2434`. Ruft `region_getCell1024(a,b)`, das
  `ECX+0xbc` liest ⇒ ECX kommt von aussen. Server-Zwilling `Region_getChunkCell`
  ist explizit `__thiscall` (`server/game_misc/game_misc.cpp:595`).
* Der Mod hat das bereits richtig: `cwsdk/cube_funcs.h:17`
  `typedef cube::Zone*(__thiscall* world_getzone_t)(cube::World*, int, int);`

---

## 2. Originalcode `cube/world/WorldInfo.cpp:5026-5077`, zeilenweise kommentiert

Abgeschrieben aus `WorldInfo_generateBiomeContent @0x5E4850`.
Bezeichner-Zuordnung (belegt im selben Rumpf):

| Ghidra-Name | Bedeutung | Beleg |
|---|---|---|
| `local_1360` | `param_1` = **zoneX** | `WorldInfo.cpp:1426` |
| `local_1368` | `param_2` = **zoneY** | `WorldInfo.cpp:1427` |
| `local_130c` / `uVar5` | **`Zone*`** aus `cube::Zone::ctor_0` | `WorldInfo.cpp:1527-1529` |
| `local_1380` | **`Region*`** = `world+0xBC + …` | `WorldInfo.cpp:1503` |
| `local_13b0` | Tile-Index `(zx&63)*64 + (zy&63)` | `WorldInfo.cpp:2287` |
| `local_12dc` | Schleifenzaehler **i** ueber `static_objects` | `WorldInfo.cpp:5048` |

```c
5026  VoxelChunk_propagateSunlight(local_135c,local_1330,local_1374,local_1330 + 0x40,0,uVar5);
      // Licht: Sonnenlicht durch die fertige Voxelspalte propagieren.
      // Laeuft VOR dem Index -- der Index braucht kein Licht, aber die
      // Objekthoehe in Zeile 5042 braucht die Hoehenkarte.

5027  local_12ec = (int *)(uVar5 + 4);
      // zone+0x04 = Kopf einer ZWEITEN std::list (in Zone::ctor_0 mit
      // list_allocNode(0,0) angelegt, Zone.cpp:84-85). Das ist NICHT der
      // Ortsindex, sondern die Arbeitsliste der eben erzeugten Objekte.
5028  deref_frontValue();          // it = list._Myhead->_Next   (begin())
5029  store_object_ref();          // Iterator in eine 2. Variable kopieren
5030  cVar2 = operator_ne_int();   // it != end()
5031  while (cVar2 != '\0') {
5032    iVar26 = getPtrPlus8();    // Wert des Knotens: node+0x08  (getPtrPlus8 @0x42C6C0
                                  // = *ECX + 8, GameController.cpp:9806-9811)
5033    uVar41 = (ulonglong)local_130c;                 // Zone* als Argument
5034    pCVar18 = (CRefTime *)arrayElem_stride8();      // &pos.? (int64-Feld)
5035    lVar19 = CRefTime::Millisecs(pCVar18);          // __alldiv(x, 0x10000)
                                  // *** WICHTIG ***: "CRefTime::Millisecs" ist eine
                                  // FALSCHE Bibliothekszuordnung von Ghidra. Der Rumpf
                                  // (GameController.cpp:10229-10234) ist
                                  //   __alldiv(lo, hi, 0x10000, 0)
                                  // also die Umrechnung Fixed16 -> Bloecke.
5036    pCVar18 = (CRefTime *)arrayElem_stride8(1);
5037    lVar20 = CRefTime::Millisecs(pCVar18);
5038    uVar37 = CONCAT44(lVar19,lVar20);               // (x,y) als Paar
5039    pCVar18 = (CRefTime *)arrayElem_stride8(0);
5040    lVar19 = CRefTime::Millisecs(pCVar18);
5041    iVar22 = map_getTileLightOrHeight(lVar19,uVar37,uVar41);
                                  // Gelaendehoehe/Helligkeit an (x,y) der Zone
5042    *(float *)(iVar26 + 0x28) = (float)iVar22;
                                  // Ergebnis in Knotenwert+0x28 schreiben.
                                  // Das ist die Vorabreichung fuer die Objekthoehe.
5043    list_popFront_node(local_1498,0);   // Knoten abarbeiten und entfernen
5044    store_object_ref();
5045    cVar2 = operator_ne_int();
5046    uVar5 = local_130c;
5047  }
      // ===== AB HIER: DER ORTSINDEX WIRD GEBAUT =====
      // Zu diesem Zeitpunkt sind ALLE Objekte der Zone bereits im Vektor
      // zone->static_objects. Der Index wird EINMAL, AM STUECK, NACHTRAEGLICH
      // gebaut -- genau deshalb duerfen die Knoten rohe StaticObject*-Zeiger
      // halten: der Vektor waechst danach nicht mehr.

5048  local_12dc = (float *)0x0;          // i = 0   (der Typ "float*" ist Ghidra-Muell)
5049  iVar26 = vector_size_0x188();       // n = (end-begin)/0x188, ECX = &zone->static_objects
                                         // (vector_size_0x188 @0x44E890, audit.cpp:5811-5817)
5050  if (0 < iVar26) {
5051    pfVar9 = (float *)0;
5052    do {
5053      uVar21 = uVar5;                          // Zone* merken
5054      vector_at_0x188(pfVar9);                 // o = &static_objects[i]   (@0x44B560)
5055      arrayElem_stride8();                     // -> Adresse eines der int64-Positionsfelder
5056      pCVar18 = (CRefTime *)int64_divScalar_double();
                                                 // ftol2( *(int64*)ECX / (double)divisor )
                                                 // (@0x469060, GameController.cpp:24945-24954)
5057      lVar19 = CRefTime::Millisecs(pCVar18);   // ... / 0x10000
                                                 // zusammen: pos / 8 / 65536 = Blockkoord/8
                                                 //           = ZELLENKOORDINATE
5058      uVar40 = CONCAT44(uVar21,lVar19);        // (Zone*, Koordinate) fuer den Call
5059      uVar37 = CONCAT44(8,local_1758);         // <-- der Divisor 8 wird hier sichtbar
5060      uVar10 = 0;
5061      vector_at_0x188(local_12dc);             // o = &static_objects[i]  (nochmal)
5062      arrayElem_stride8(uVar10);               // -> zweites Positionsfeld
5063      pCVar18 = (CRefTime *)int64_divScalar_double(uVar37);   // / 8
5064      lVar19 = CRefTime::Millisecs(pCVar18);                  // / 65536
                                                 // zweite Zellenkoordinate
5065      local_12ec = (int *)getTileCell32();
                                                 // ECX = World*, Args = (cellX, cellY, zone)
                                                 // -> &objIndex[(cellY&31)*32 + (cellX&31)]
5066      if (local_12ec != (int *)0x0) {
                                                 // 0 = Objekt liegt ausserhalb der 32x32-Zellen
                                                 // dieser Zone -> es wird STILL VERWORFEN,
                                                 // bleibt im Vektor, ist aber wirkungslos.
5067        vec3_set(local_1360,local_1368,local_12dc);
                                                 // Handle in einen Stack-Vec3 schreiben:
                                                 //   {zoneX, zoneY, i}
                                                 // (vec3_set @0x42C500, ECX = Ziel)
5068        uVar10 = vector_at_0x188(local_12dc); // objPtr = &static_objects[i]
5069        set4(uVar10,lVar19,uVar40);
                                                 // set4 @0x5C38A0 (game_misc.cpp:32110-32120):
                                                 //   ECX[0..3] = param_1..param_4
                                                 // ergibt den 16-Byte-Knotenwert
                                                 //   { StaticObject*, zoneX, zoneY, i }
5070        WorldInfo_mapInsertEntry();
                                                 // ECX = die Indexzelle aus 5065,
                                                 // Arg  = Zeiger auf den 16-Byte-Wert.
                                                 // == std::list::push_back
5071      }
5072      local_12dc = (float *)((int)local_12dc + 1);   // i++
5073      iVar26 = vector_size_0x188();                  // n neu lesen (der Vektor aendert sich nicht)
5074      pfVar9 = local_12dc;
5075    } while ((int)local_12dc < iVar26);
5076  }

5077  *(uint *)(local_1380 + 0x10018 + (int)local_13b0 * 4) = uVar5;
      // *** ERST JETZT *** wird die Zone im Weltgitter sichtbar:
      // Region->zones[tileIndex] = zone.
      // Vorher kann kein anderer Thread die Zone -- und damit auch nicht den
      // halbfertigen Index -- ueberhaupt finden. Das ist die Synchronisation
      // des Originals: Publish-nach-Fertigstellung, kein Lock.
```

### 2.1 Reihenfolge in Klartext

1. Alle Objekte werden waehrend der Generierung per `push_back` in
   `zone->static_objects` gelegt (u. a. `WorldInfo_placeStructure`,
   `WorldInfo.cpp:5731` mit `std_vector_Reserve_grow_188(1)`).
2. Licht + Hoehenwerte (5026-5047).
3. **Danach** in EINER Schleife der Index (5048-5076): pro Objekt
   Zellenkoordinate rechnen → Zelle holen → Knoten `{obj, zoneX, zoneY, i}` anhaengen.
4. **Zuletzt** Zone ins Weltgitter haengen (5077).

Der Grund fuer diese Reihenfolge ist zwingend: der Knoten speichert einen **rohen
Zeiger** auf das Vektorelement. Jedes spaetere `push_back` mit Reallokation macht
**alle** Knotenzeiger der Zone ungueltig.

---

## 3. Speicherlayout

### 3.1 Das Indexfeld an `zone+0xAC` (belegt: `cube/world/Zone.cpp:139-147`)

```c
// cube::Zone::ctor_0 @0x6240F0, Zone.cpp:129-147
alloc = operator_new_wrapper(0x200004);              // 4 + 0x10000 * 0x20
*alloc = 0x10000;
_eh_vector_constructor_iterator_(alloc+1, 0x20, 0x10000, …, Field::ctor_0);
self[0x2a] = alloc+1;                                // zone+0xA8 = Field* [256*256]

alloc = operator_new_wrapper(0x2004);                // 4 + 0x400 * 8
*alloc = 0x400;                                      // 1024 Elemente
_eh_vector_constructor_iterator_(alloc+1, 8, 0x400,  // Elementgroesse 8
                                 lib_fn_6240d0,      // ctor = std::list<ObjRef>::list()
                                 std_list_clear_4da480);  // dtor
self[0x2b] = alloc+1;                                // zone+0xAC = std::list<ObjRef>* [1024]
```

**Damit ist belegt:**

| Was | Wert |
|---|---|
| `zone+0xAC` | **Zeiger** auf ein Heap-Array (nicht das Array selbst) |
| Elementanzahl | **1024** = 32 x 32 |
| Elementgroesse | **8 Byte** |
| Zellengroesse in Bloecken | 256/32 = **8 x 8** |
| Konstruktor je Element | `lib_fn_6240d0 @0x6240D0` |
| Destruktor je Element | `std_list_clear_4da480 @0x4DA480` |
| Indexformel | `[(cellY & 31) * 32 + (cellX & 31)]` — **Y-major!** |

Die Indexformel ist doppelt belegt:
`cube/control/GameController.cpp:10307` (`getTileCell32`: `(blockY*0x20 + blockX)*8`,
wobei `blockY` der **zweite** Parameter ist) und, unabhaengig davon, in der KI:
`cube/ai/RandomInteractionBehavior.cpp:136`
`*(int *)(chunk + 0xac) + (uVar16 * 0x20 + uVar15) * 8` — dort ist `uVar16` aus
`chunkX` und `uVar15` aus `y`; die Herleitung in `:112-126` zeigt, dass `y` aus
`creature+0x10` (**pos.X**) und `chunkX` aus `creature+0x18` (**pos.Y**) stammt.
Also ebenfalls `cellY * 32 + cellX`. Die Ghidra-Variablennamen dort sind vertauscht.

> Achtung, Abweichung zum Regionsgitter: `world_getRegionCell64` indiziert
> **X-major** (`(zx&63)*64 + (zy&63)`, `game_misc.cpp:2430`). Der Zellenindex ist
> **Y-major**. Wer beides gleich behandelt, trifft die falsche Zelle.

### 3.2 Der Listenkopf (8 Byte) — belegt: `cube/_library/other.cpp:56066-56077`

```c
/* lib_fn_6240d0 @0x6240D0 == std::list<ObjRef>::list()  (Default-Ctor) */
void lib_fn_6240d0(void) {
  undefined4 *in_ECX;
  *in_ECX   = 0;
  in_ECX[1] = 0;
  uVar1 = list_buyNode_18(0,0);   // Sentinel: next=prev=self (audit.cpp:10330-10333)
  *in_ECX = uVar1;
}
```

```c
struct list_head {          // 8 Byte, MSVC std::list ohne _SECURE_SCL-Proxy
    Node* _Myhead;          // +0x00  Sentinelknoten, zirkulaer selbstverlinkt
    uint32_t _Mysize;       // +0x04  Elementanzahl
};
```

**Es gibt KEIN Proxy-Feld.** Der Kopf ist `{Head, Size}`, in dieser Reihenfolge.
Gegenbeleg zur 12-Byte-Annahme: die Elementgroesse im
`_eh_vector_constructor_iterator_`-Aufruf ist `8`, und `getTileCell32` multipliziert
den Zellenindex mit `8`.

Leseseite bestaetigt es: `cube/ai/RandomInteractionBehavior.cpp:138-139`
```c
timerPtr = (int *)*timerPtr;   // Zelle -> _Myhead      (Offset 0)
objNode  = (int *)*timerPtr;   // _Myhead->_Next        (begin())
… while (objNode != timerPtr)  // Abbruch am Sentinel
```

### 3.3 Der Listenknoten (0x18 = 24 Byte)

`list_buyNode_18 @0x46D590` (`cube/_library/audit.cpp:10323-10341`) allokiert
`operator new(0x18)` und setzt `[0]=next`, `[1]=prev` (bei `next==0` Selbstverlinkung).
`map_insertVec4Node @0x608A20` (`cube/_library/audit.cpp:41757-41770`) schreibt
danach vier Dwords ab `+0x08`.

```c
struct ObjNode {              // 0x18 Byte
    ObjNode* _Next;           // +0x00
    ObjNode* _Prev;           // +0x04
    // ---- Wert (ObjRef, 16 Byte) ab hier ----
    StaticObject* obj;        // +0x08  roher Zeiger in zone->static_objects
    int32_t       zoneX;      // +0x0C  \
    int32_t       zoneY;      // +0x10   >  Handle fuer resolveVectorElementByHandle
    int32_t       index;      // +0x14  /   (Index im Vektor, Stride 0x188)
};
```

Belege fuer die Feldbedeutung:

* **`+0x08` = Objektzeiger:** `cube/ai/RandomInteractionBehavior.cpp:151`
  liest `objNode[2]+0x40` / `+0x44` (Belegungsfelder des 0x188-Records).
  Server-Physik: `server/game_misc/game_misc.cpp:45796` `iVar8 = *(int *)*puVar9;`
  (Iterator-Deref liefert `node+8`, dessen Inhalt der `StaticObject*` ist,
  dessen erstes Dword der Objekttyp).
* **`+0x0C..+0x14` = Handle:** `cube/ai/RandomInteractionBehavior.cpp:143-150`
  vergleicht `objNode+3` (= `+0x0C`) drei Dwords lang mit `self+0x1478..0x1480`.
  `resolveVectorElementByHandle` (`GameController.cpp:83658-83664`) uebergibt
  `handle[0]`/`handle[1]` an `world_getRegionCell64`, das Werte `< 0x10000` erwartet
  ⇒ **Zonen**koordinaten, nicht Zellenkoordinaten. Das passt zur Schreibseite:
  `WorldInfo.cpp:5067` `vec3_set(local_1360, local_1368, i)` mit
  `local_1360/1368 = param_1/param_2` = Zonenkoordinaten (`WorldInfo.cpp:1426-1427`,
  identisch mit dem, was in `zone+0x60`/`+0x64` geschrieben wird, `:1533-1534`).
* **Der Wert ist 16 Byte**, weil `list_buyNode_18` `0x18` allokiert und die
  Laengenpruefung in `WorldInfo_mapInsertEntry` `0xaaaaaa9` lautet
  (`= max_size` fuer Knotengroesse 24). Vergleichswerte im selben Baum:
  `0x9249248` → 0x1C, `0x3fffffe` → 0x40 (`server/_library/crt_stl.cpp:57365`, `:12551`).

### 3.4 `WorldInfo_mapInsertEntry` ausgeschrieben

```c
/* Global::WorldInfo_mapInsertEntry @0x5F4B50, cube/world/WorldInfo.cpp:7146-7160 */
void __thiscall WorldInfo_mapInsertEntry(/* ECX */ int *list, const uint32_t *value)
{
  int head = *list;                                   // _Myhead (Sentinel)
  int node = map_insertVec4Node(head,                 // neuer Knoten: _Next = head
                                *(int*)(head + 4),    //               _Prev = head->_Prev (bisher letzter)
                                value);               //               Wert = value[0..3]
  if (list[1] == 0xaaaaaa9) std::_Xlength_error("list<T> too long");
  list[1] = list[1] + 1;                              // _Mysize++
  *(int *)(head + 4) = node;                          // head->_Prev = node
  **(int **)(node + 4) = node;                        // node->_Prev->_Next = node
}
```

Das ist exakt `push_back`. Vier Speicherzugriffe ausserhalb des neuen Knotens:
`*list`, `list[1]`, `head+4`, `node->_Prev->_Next`. **Mehr wird nicht angefasst.**

---

## 4. Kann ein Mod das NACH der Generierung aufrufen?

**Ja.** Keine der beiden Funktionen liest globalen oder temporaeren Generierungszustand.

| Funktion | Dereferenziert | Lebensdauer dieses Zustands |
|---|---|---|
| `getTileCell32` | `ECX(World)+0xBC` (nur wenn `zone == 0`), `zone+0x60`, `zone+0x64`, `*(void**)(zone+0xAC)` | alles permanent, ab `Zone::ctor_0` |
| `WorldInfo_mapInsertEntry` | `ECX(list)+0x00`, `+0x04`, `head+0x04`, `node->_Prev`, `operator new(0x18)` | Listenkopf existiert ab `Zone::ctor_0 @0x6240F0` (`Zone.cpp:139-147`), also **vor** jeder Objekterzeugung |

Kein RNG-Seed, kein „aktueller Chunk"-Global, kein Vorlagenpuffer, kein Lock.
`WorldInfo_generateBiomeContent` benutzt sie am Ende in genau der Rolle, die ein
Mod braucht.

**Vier reale Fallstricke — keine Konventionsfrage, sondern Lebensdauer und Threads:**

1. **Zeigerinvalidierung.** Der Knoten haelt `StaticObject*` roh. Jedes
   `push_back` auf `zone->static_objects`, das reallokiert, macht **alle** Knoten
   dieser Zone ungueltig. Das Original umgeht das, indem es den Index erst nach
   dem letzten `push_back` baut (`WorldInfo.cpp:5048`). Ein Mod, der spaeter noch
   ein Objekt anhaengt, muss danach **alle** Knotenzeiger der Zone reparieren.
   Der `index` im Knoten ueberlebt eine Reallokation — daraus laesst sich der
   Zeiger neu berechnen (siehe `RepairIndexPointers` im Code unten).
2. **Allokator.** Der Zonendestruktor gibt die Knoten mit dem `operator delete`
   des Spiels frei (`std_list_clear_4da480 @0x4DA480`, `audit.cpp:14984-14986`).
   Ein Knoten, den der Mod mit seiner eigenen CRT allokiert, kracht dort.
   **Deshalb ist die Vorgabe „echte Spielfunktionen benutzen" nicht kosmetisch:**
   `WorldInfo_mapInsertEntry → map_insertVec4Node → list_buyNode_18 → operator new`
   nimmt den richtigen Heap.
3. **Threads.** Das Original braucht kein Lock, weil es die Zone erst nach dem
   Indexbau publiziert (`WorldInfo.cpp:5077`). Ein Mod arbeitet dagegen an einer
   bereits sichtbaren Zone; Physik (`GameController.cpp:101257 ff.`) und Zielsuche
   (`:40301 ff.`) iterieren dieselben Listen. Der Mod muss selbst serialisieren.
   Welche kritische Sektion dafuer zustaendig ist: siehe UNSICHER 9.3.
4. **Zellengrenze.** Liegt das Objekt nicht in einer der 32x32 Zellen **dieser**
   Zone, liefert `getTileCell32` `0` (Pruefungen `GameController.cpp:10279-10294`).
   Zone und Blockkoordinate muessen zusammenpassen.

---

## 5. Fertiger C++-Code fuer den Mod

Ein Block, keine handgebauten Listenknoten, alle Adressen ueber
`GetModuleHandleA(NULL)` rebased.

```cpp
// ===========================================================================
//  CwObjIndex.h  --  Statikobjekt anlegen und korrekt in zone+0xAC eintragen
//  Alle Adressen: Cube World Alpha, ImageBase 0x400000, 32 Bit.
// ===========================================================================
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace cwobj {

// ---- Rebasing -------------------------------------------------------------
static inline uint32_t ImageBase()          { return (uint32_t)GetModuleHandleA(NULL); }
static inline uint32_t Rebase(uint32_t a)   { return ImageBase() + (a - 0x00400000u); }

// ---- Adressen (statisch, ImageBase 0x400000) ------------------------------
enum : uint32_t {
    A_getTileCell32     = 0x0042F640,  // __thiscall(World*, cellX, cellY, Zone*)
    A_mapInsertEntry    = 0x005F4B50,  // __thiscall(list*, const ObjRef*)
    A_worldGetZone      = 0x00434D10,  // __thiscall(World*, zoneX, zoneY)
    A_vecReserveGrow188 = 0x00594B40,  // __thiscall(vector*, additionalCount)
    A_resolveHandle     = 0x005A0910,  // __thiscall(World*, const int32_t[3])
};

// ---- Typedefs mit KORREKTER Konvention ------------------------------------
typedef void* (__thiscall* Fn_getTileCell32)  (void* world, unsigned cellX,
                                               unsigned cellY, void* zoneOrNull);
typedef void  (__thiscall* Fn_mapInsertEntry) (void* listHead, const void* value16);
typedef void* (__thiscall* Fn_worldGetZone)   (void* world, int zoneX, int zoneY);
typedef void  (__thiscall* Fn_vecReserveGrow) (void* vec, unsigned addCount);
typedef void* (__thiscall* Fn_resolveHandle)  (void* world, const int32_t handle[3]);

// ---- Layouts --------------------------------------------------------------
#pragma pack(push, 4)
struct ObjRef {                 // 16 Byte = Wert des Listenknotens (node+0x08)
    void*   obj;                // StaticObject* in zone->static_objects
    int32_t zoneX, zoneY;       // Zonenkoordinaten (Blockkoord >> 8)
    int32_t index;              // Index im Vektor, Stride 0x188
};
struct ObjNode {                // 0x18 Byte
    ObjNode* next;              // +0x00
    ObjNode* prev;              // +0x04
    ObjRef   val;               // +0x08
};
struct ListHead {               // 8 Byte  --  KEIN Proxy-Feld!
    ObjNode* head;              // +0x00  Sentinel
    uint32_t size;              // +0x04
};
#pragma pack(pop)

static const size_t kObjSize   = 0x188;   // StaticObject
static const int    kBlocksPerCell = 8;   // Indexzelle = 8x8 Bloecke
static const int    kBlocksPerZone = 256;

// zone->static_objects liegt bei zone+0x0C als {first, last, end}
static inline uint32_t* ZoneVec(void* zone) { return (uint32_t*)((uint8_t*)zone + 0x0C); }
// zone+0xAC ist ein ZEIGER auf ListHead[1024]
static inline ListHead* ZoneIndexArray(void* zone) { return *(ListHead**)((uint8_t*)zone + 0xAC); }

static inline int FloorDiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) --q; return q; }

// ---------------------------------------------------------------------------
// 1) Objekt an zone->static_objects anhaengen -- ueber die Vektor-Growth des
//    Spiels (0x594B40), damit Speicher aus dem richtigen Heap kommt und der
//    alte Puffer korrekt freigegeben wird.
//    Rueckgabe: Vektorindex, oder -1. *outRealloc sagt, ob der Puffer umgezogen
//    ist -- dann MUSS RepairIndexPointers laufen.
// ---------------------------------------------------------------------------
static int AppendStaticObject(void* zone, const uint8_t obj[0x188], bool* outRealloc)
{
    if (outRealloc) *outRealloc = false;
    if (!zone) return -1;
    __try {
        uint32_t* v = ZoneVec(zone);                 // {first,last,end}
        uint32_t  oldFirst = v[0];

        Fn_vecReserveGrow grow = (Fn_vecReserveGrow)Rebase(A_vecReserveGrow188);
        grow(v, 1);                                  // no-op, wenn Platz da ist

        if (!v[1] || v[1] + kObjSize > v[2]) return -1;
        memcpy((void*)v[1], obj, kObjSize);          // Vorlage ist genullt -> memcpy ok
        v[1] += kObjSize;

        if (outRealloc) *outRealloc = (v[0] != oldFirst);
        return (int)((v[1] - v[0]) / kObjSize) - 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// ---------------------------------------------------------------------------
// 2) Nach einer Vektor-Reallokation: alle Knotenzeiger der Zone neu berechnen.
//    Der Knoten kennt seinen eigenen Vektorindex (val.index) -- daraus laesst
//    sich val.obj rekonstruieren, ohne einen einzigen Knoten neu zu bauen.
// ---------------------------------------------------------------------------
static void RepairIndexPointers(void* zone)
{
    if (!zone) return;
    __try {
        ListHead* cells = ZoneIndexArray(zone);
        if (!cells) return;
        uint32_t* v     = ZoneVec(zone);
        uint8_t*  first = (uint8_t*)v[0];
        int       count = (int)((v[1] - v[0]) / kObjSize);
        for (int c = 0; c < 32 * 32; ++c) {
            ObjNode* h = cells[c].head;
            if (!h) continue;
            for (ObjNode* n = h->next; n && n != h; n = n->next)
                if (n->val.index >= 0 && n->val.index < count)
                    n->val.obj = first + (size_t)n->val.index * kObjSize;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// ---------------------------------------------------------------------------
// 3) Objekt in den Ortsindex haengen -- mit den echten Spielfunktionen.
//    blockX/blockY sind WELT-Blockkoordinaten des Objekts.
// ---------------------------------------------------------------------------
static bool IndexInsert(void* world, void* zone, int blockX, int blockY, int index)
{
    if (!world || !zone || index < 0) return false;
    if (blockX < 0 || blockY < 0) return false;      // getTileCell32 lehnt negativ ab
    __try {
        uint32_t* v = ZoneVec(zone);
        int count = (int)((v[1] - v[0]) / kObjSize);
        if (index >= count) return false;

        Fn_getTileCell32 getCell = (Fn_getTileCell32)Rebase(A_getTileCell32);
        void* cell = getCell(world,
                             (unsigned)(blockX / kBlocksPerCell),
                             (unsigned)(blockY / kBlocksPerCell),
                             zone);                  // Zone mitgeben -> kein World-Zugriff
        if (!cell) return false;                     // Objekt liegt ausserhalb dieser Zone

        ObjRef ref;
        ref.obj   = (uint8_t*)v[0] + (size_t)index * kObjSize;
        ref.zoneX = FloorDiv(blockX, kBlocksPerZone);
        ref.zoneY = FloorDiv(blockY, kBlocksPerZone);
        ref.index = index;

        Fn_mapInsertEntry push = (Fn_mapInsertEntry)Rebase(A_mapInsertEntry);
        push(cell, &ref);                            // == std::list::push_back
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------------------
// 4) Kontrolle: aufloesen wie das Spiel es tut.
// ---------------------------------------------------------------------------
static void* ResolveHandle(void* world, int zoneX, int zoneY, int index)
{
    __try {
        int32_t h[3] = { zoneX, zoneY, index };
        return ((Fn_resolveHandle)Rebase(A_resolveHandle))(world, h);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ---------------------------------------------------------------------------
// 5) Komplettbeispiel: eine benutzbare, feste Tuer setzen.
//    Feldbedeutungen siehe RE_Interaktion.md Abschnitt 4.
// ---------------------------------------------------------------------------
static void FillDoor(uint8_t o[0x188], int type, int blockX, int blockY, int blockZ,
                     int rot, float hx, float hy, float hz)
{
    memset(o, 0, kObjSize);
    *(int32_t*)(o + 0x00)  = type;                       // Objekttyp (1 = Tuer)
    *(int64_t*)(o + 0x08)  = (int64_t)blockX << 16;      // Position, Fixed16
    *(int64_t*)(o + 0x10)  = (int64_t)blockY << 16;
    *(int64_t*)(o + 0x18)  = (int64_t)blockZ << 16;
    *(int32_t*)(o + 0x20)  = rot & 3;                    // 0..3 Vierteldrehungen
    *(float*)  (o + 0x24)  = hx;                         // Halbmasse X
    *(float*)  (o + 0x28)  = hy;                         // Halbmasse Y
    *(float*)  (o + 0x2C)  = hz;                         // Halbmasse Z
    *(uint8_t*)(o + 0x30)  = 1;                          // Zustand: zu = FEST (BYTE!)
    *(int32_t*)(o + 0x34)  = 0;                          // Animationszeit
    *(int32_t*)(o + 0x40)  = 0;                          // Belegung frei
    *(int32_t*)(o + 0x44)  = 0;
    *(int32_t*)(o + 0x178) = -1;                         // kein Partnerobjekt
    *(int32_t*)(o + 0x17C) = -1;
    *(int32_t*)(o + 0x180) = -1;
    *(int32_t*)(o + 0x184) = 0;
}

// world = &GetGameController()->world   (GC+0x2E4)
// Aufrufer haelt die Weltsperre (World::Lock) -- siehe UNSICHER 9.3.
static bool PlaceDoor(void* world, int blockX, int blockY, int blockZ, int rot)
{
    Fn_worldGetZone getZone = (Fn_worldGetZone)Rebase(A_worldGetZone);
    void* zone = nullptr;
    __try { zone = getZone(world, FloorDiv(blockX, 256), FloorDiv(blockY, 256)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!zone) return false;                             // Zone nicht geladen

    uint8_t obj[kObjSize];
    FillDoor(obj, /*type*/ 1, blockX, blockY, blockZ, rot,
             /*hx*/ 0.5f, /*hy*/ 0.1f, /*hz*/ 1.5f);

    bool realloc = false;
    int index = AppendStaticObject(zone, obj, &realloc);
    if (index < 0) return false;

    if (realloc) RepairIndexPointers(zone);              // PFLICHT nach Umzug
    return IndexInsert(world, zone, blockX, blockY, index);
}

// ---------------------------------------------------------------------------
// 6) Mehrere Objekte: erst ALLE anhaengen, dann indizieren -- die Reihenfolge
//    des Originals (WorldInfo.cpp:5048). Spart die Reparaturlaeufe.
// ---------------------------------------------------------------------------
struct Pending { uint8_t obj[0x188]; int bx, by; };

static int PlaceBatch(void* world, void* zone, Pending* items, int n)
{
    int firstIndex = -1;
    for (int i = 0; i < n; ++i) {
        bool r = false;
        int idx = AppendStaticObject(zone, items[i].obj, &r);
        if (idx < 0) return 0;
        if (firstIndex < 0) firstIndex = idx;
    }
    RepairIndexPointers(zone);                  // einmal, egal ob umgezogen
    int ok = 0;
    for (int i = 0; i < n; ++i)
        if (IndexInsert(world, zone, items[i].bx, items[i].by, firstIndex + i)) ++ok;
    return ok;
}

} // namespace cwobj
```

---

## 6. Kurzcheckliste

| Punkt | Richtig |
|---|---|
| Konvention `getTileCell32` | `__thiscall`, `this = World*`, 3 Stackargumente |
| Konvention `mapInsertEntry` | `__thiscall`, `this = Indexzelle`, 1 Stackargument |
| Zellenkoordinate | Weltblock / 8 |
| Zellenindex | `(cellY & 31) * 32 + (cellX & 31)` |
| `zone+0xAC` | **Zeiger** auf `ListHead[1024]`, Stride 8 |
| Listenkopf | `{Node* head; uint32 size;}` — 8 Byte, kein Proxy |
| Knotenwert | `{StaticObject*, zoneX, zoneY, index}` bei `node+0x08` |
| Handle-Koordinaten | **Zonen**koordinaten (`block >> 8`), nicht Zellen |
| Reihenfolge | erst alle `push_back`, dann Index |
| Nach Realloc | alle `val.obj` der Zone neu berechnen |

---

## 7. Fehler im vorhandenen Mod-Code (belegt)

`MountainVillage.cpp:1231-1253`, Funktion `IndexInsert`:

```cpp
uint32_t* lst = (uint32_t*)cell;      // {proxy, head, size}   <-- FALSCH
uint32_t head = lst[1];               //  liest Size als Head
…
lst[2] = lst[2] + 1;                  //  schreibt in den Head der NAECHSTEN Zelle
```

Der Kopf hat **kein** Proxy-Feld und ist 8 Byte gross (Beleg 3.2). Korrekt waere
`lst[0]` = Head, `lst[1]` = Size. In der jetzigen Fassung:

* `head = lst[1]` liefert die **Size** der Zelle — bei einer leeren Zelle `0`,
  die Funktion gibt dann `false` zurueck (deshalb ist bisher nichts explodiert,
  solange die Zelle leer war);
* bei einer **nicht leeren** Zelle ist `lst[1]` eine kleine Zahl (1, 2, 3 …), die
  als Zeiger dereferenziert wird → Zugriffsverletzung, vom `__except` geschluckt;
* `lst[2] += 1` liegt bereits in der **Folgezelle** und inkrementiert deren
  `_Myhead`-**Zeiger** um 1 → deren Sentinel ist ab da um ein Byte verschoben.
  Das ist eine stille Speicherkorruption, die erst spaeter knallt.

Zweiter Punkt, `MountainVillage.cpp:1298` (`ObjectTester`): `*(int*)(obj + 0x30) = 1;`
schreibt 4 Byte. `+0x30` ist ein **Byte** (Zustand), `+0x34` ein `int`
(Animationszeit). Hier zufaellig harmlos (die oberen drei Byte werden 0, was auch
der Sollwert von `+0x31..0x33` ist), aber falsch typisiert.

Dritter Punkt, `MountainVillage.cpp:1021-1055` (`AppendBuilding`): allokiert selbst
und gibt den alten Puffer bewusst nie frei. Mit `std_vector_Reserve_grow_188 @0x594B40`
(`__thiscall`, `cube/_library/audit.cpp:36763-36782`) macht das Spiel es selbst,
inklusive Freigabe und korrektem Kopieren der eingebetteten Container
(`+0x48` `std::vector`, `+0x58..0x174` Entity-Zustand). Siehe Code in Abschnitt 5.

---

## 8. Nachpruefanweisung fuer die Konvention (das fehlende `add esp,N`)

In Ghidra am Original-Binary, je Funktion 60 Sekunden:

1. `Listing` → Adresse anspringen (`G`, z. B. `0042F640`).
2. **Letzte Instruktion ansehen.** `RET 0x000C` ⇒ Callee raeumt 12 Byte ⇒ 3
   Stackargumente ⇒ `__thiscall` (ECX ungezaehlt) oder `__stdcall`.
   Blankes `RET` ⇒ `__cdecl`.
3. **ECX-Test:** `Function Graph` des Rumpfes, Suche nach einem Lesezugriff auf ECX
   **vor** jedem Schreibzugriff. Bei `0x5F4B50` erwartet: `MOV EAX,[ECX]` ganz vorn.
   Bei `0x42F640` erwartet: **kein** ECX-Zugriff ausser der Weitergabe an
   `CALL 00434D10`.
4. **Aufrufstelle:** `References → Show References to Address`; an einer Fundstelle
   pruefen, ob nach dem `CALL` ein `ADD ESP,0Ch` steht (dann `__cdecl`) oder nicht
   (dann Callee-Cleanup).

Erwartete Ergebnisse nach dieser Analyse:

| Adresse | erwartetes `ret` | erwarteter ECX-Gebrauch |
|---|---|---|
| 0x42F640 | `RET 0Ch` | nur Weitergabe an 0x434D10 |
| 0x5F4B50 | `RET 4` | `MOV reg,[ECX]` als erste Leseoperation |
| 0x5A0910 | `RET 4` | nur Weitergabe an 0x434D10 |
| 0x434D10 | `RET 8` | nur Weitergabe an 0x434A90 |
| 0x608A20 | blankes `RET` | keiner |
| 0x46D590 | blankes `RET` | keiner |
| 0x594B40 | `RET 4` | `[ECX]`, `[ECX+4]`, `[ECX+8]` |

---

## 9. UNSICHER

Alles hier ist **nicht** durch Datei+Zeile abgesichert.

1. **`__cdecl` vs. `__stdcall` bei `map_insertVec4Node @0x608A20` und
   `list_buyNode_18 @0x46D590`.** Belegt ist nur: kein Registereingang, 3 bzw. 2
   Stackparameter. Wer den Stack raeumt, steht nicht im Pseudocode.
   Fuer den Mod irrelevant, solange nur `WorldInfo_mapInsertEntry` gerufen wird.
2. **Die `ret imm16`-Werte in der Signaturtabelle** sind aus Parameterzahl und
   Konvention **hergeleitet**, nicht gelesen. Nachpruefen nach Abschnitt 8.
3. **Welche kritische Sektion den Ortsindex schuetzt.** Die Zone hat eine eigene
   `CRITICAL_SECTION` bei **`zone+0xB0`** (`InitializeCriticalSectionAndSpinCount(self + 0x2c)`,
   `cube/world/Zone.cpp:128` — `self` ist `undefined4*`, also Byteoffset 0xB0;
   passt zu `assert_size(cube::Zone, 0xC8)` in `cwsdk/cube.h:30`).
   Ein `EnterCriticalSection` auf `zone+0xB0` findet sich im Client-Dekompilat
   **nicht** (nur ueber Zeigervariablen, deren Herkunft Ghidra verloren hat).
   Belegt benutzt werden `world+0x8000C0` und `world+0x8000D8` (je 3 Fundstellen).
   *Vermutung:* `world+0x8000C0` ist die allgemeine Weltsperre und reicht.
   **Nicht bewiesen.** Im Zweifel beide nehmen, oder die Einfuegung in den
   Spiel-Haupttick legen.
4. **Argumentzuordnung in `WorldInfo.cpp:5054-5065`.** Dass die beiden
   `int64_divScalar_double`/`Millisecs`-Ketten `cellX` und `cellY` liefern, ist
   belegt (Divisor 8 sichtbar in `:5059`, `/0x10000` im `Millisecs`-Rumpf).
   **Welche** Kette welche Achse liefert, hat Ghidra verloren
   (`arrayElem_stride8()` ohne Argument in `:5055`, mit `0` in `:5062`).
   Fuer den Nachbau egal, weil `getTileCell32` selbst eindeutig ist.
   Ebenso die genaue Argumentverteilung von `set4` in `:5069` — das **Ergebnis**
   `{obj, zoneX, zoneY, i}` ist dagegen ueber die Leseseite doppelt belegt (3.3).
5. **`std_list_clear_4da480 @0x4DA480` als Zonendestruktor.** Im Dekompilat
   (`cube/_library/audit.cpp:14971-14987`) steht **keine Schleife**: es wird der
   erste Elementknoten und der Sentinel freigegeben, nicht die ganze Kette.
   Entweder ein Dekompilat-Artefakt oder das Spiel leckt beim Zonenabbau alle
   Indexknoten ab dem zweiten. **Nicht geprueft.** Falls es stimmt, ist das ein
   Original-Leck, kein Mod-Problem — aber es heisst auch, dass ein Mod die
   Zellen nicht gefahrlos „leeren" kann.
6. **Ob es eine Entfernen-Funktion fuer Indexeintraege gibt.** Nicht gesucht.
   Der Mod kann nur anhaengen. Ein Objekt „loeschen" hiesse: Knoten von Hand
   aushaengen und mit dem `operator delete` des Spiels freigeben — Adresse dafuer
   ist in diesem Befund nicht bestimmt.
7. **Verhalten bei negativen Weltkoordinaten.** `getTileCell32` lehnt negative
   Zellenkoordinaten in `GameController.cpp:10269-10272` ab, damit ist die
   `& 0x8000001f`-Restrechnung nie negativ. Der Code oben blockt negative
   Blockkoordinaten deshalb vorsorglich. Ob Cube World ueberhaupt negative
   Weltkoordinaten kennt, wurde nicht geprueft.
8. **`std_vector_Reserve_grow_188(vec, 1)` als „reserve one more".** Der Rumpf
   (`audit.cpp:36769`) prueft `param_1 <= (cap-end)/0x188` — das ist MSVCs
   `_Reserve(_Count)`-Semantik „`_Count` **zusaetzliche** Elemente". Konsistent,
   aber nicht an einer zweiten Aufrufstelle gegengeprueft; `WorldInfo.cpp:5730-5731`
   ruft es nur, wenn `end == cap`.
9. **`memcpy` einer genullten 0x188-Vorlage** statt des echten Copy-Ctors
   (`WorldInfo.cpp:5743-5760` kopiert Feld fuer Feld und ruft
   `std_vector_assign_stride0c` fuer `+0x48`). Bei einer **genullten** Vorlage ist
   der eingebettete Vektor leer und `memcpy` deckungsgleich. Wer ein **bestehendes**
   Objekt kopiert, darf `memcpy` **nicht** benutzen — dann haetten zwei Records
   denselben `+0x48`-Puffer.
