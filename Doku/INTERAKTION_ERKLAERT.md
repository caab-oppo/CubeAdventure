# Wie „interagierbar" funktioniert

**Kurz:** Ob man eine Tür öffnen kann und ob man an ihr hängenbleibt, hängt am
**Objekttyp** im 0x188-Record — und daran, dass das Objekt im **Ortsindex
`zone+0xAC`** hängt. Der Vektor `zone->static_objects` allein bewirkt gar
nichts: Physik und Zielsuche lesen ihn nie.

Alle Adressen statisch, ImageBase `0x400000`.

---

## 1. Die zwei Datenstrukturen

### Der Vektor — nur Speicher

`zone->static_objects` ist ein `std::vector` mit Elementgröße `0x188`. Er hält
die Objektdaten, wird aber von keinem Spielsystem durchsucht. Er ist reine
Ablage.

```
+0x00  int    Objekttyp          <- entscheidet ALLES (Modell, Kollision, Interaktion)
+0x08  int64  X   (Fixed16, Blöcke * 65536)
+0x10  int64  Y
+0x18  int64  Z
+0x20  int    Drehung 0..3
+0x24  float  Skala X   \
+0x28  float  Skala Y    > zugleich die Kollisionsbox
+0x2C  float  Höhe      /
+0x30  int    Zustand   <- 0 = offen/inaktiv, 1 = zu/aktiv
+0x40/+0x44  Belegung (RandomInteractionBehavior)
+0x178/+0x17C/+0x180  Handle auf ein Partnerobjekt (Doppeltür)
```

### Der Ortsindex — hier passiert alles

`zone+0xAC` ist ein **Zeiger** auf ein Heap-Array von 1024 Listenköpfen
(`Zone::ctor_0 @0x6240F0`, `operator new(0x2004)` = 4 + 1024·8). Das sind
32×32 Zellen zu je 8×8 Blöcken.

- Listenkopf: **8 Byte** `{_Myhead, _Mysize}` — **kein** Proxy-Feld
- Zellenindex ist **Y-major**: `(cellY & 31) * 32 + (cellX & 31)`
- Knotenwert `ObjRef`, 16 Byte:

```c
struct ObjRef {
    StaticObject* obj;              // Rohzeiger in den Vektor
    int zoneX, zoneY, index;        // Handle: Zone + Platz im Vektor
};
```

Das Handle lässt sich mit `resolveVectorElementByHandle @0x5A0910` wieder
auflösen — deshalb steht der Index redundant im Knoten.

---

## 2. Wer den Index liest

### Kollision

Eine Schleife in `GameController_updateSubsystems` läuft vor der Voxelprüfung
über die Indexzellen um den Spieler (Client `GameController.cpp:101256-101340`,
Server byte-gleich `server/game_misc/game_misc.cpp:45785-45880`):

```c
for (cx = px8-1; cx <= px8+1; cx++)          // px8 = Spielerblock >> 3
for (cy = py8-1; cy <= py8+1; cy++) {
    cell = getTileCell32(cx, cy, zone);      // zone+0xAC[cy*32+cx]
    if (!cell) continue;
    for (node in *cell) {
        o = node->obj;                        // Knoten +0x08
        t = o->type;                          // Record +0x00
        // Typ 6, 7, 9      -> nie fest
        // Typ 1,2,3,5,8    -> fest, SOLANGE o->state(+0x30) != 0
        // alle anderen     -> immer fest
        // Box aus +0x24/+0x28/+0x2C, bei ungerader Drehung X/Y getauscht
    }
}
```

### Zielsuche für `[R]`

Dieselbe Zellenschleife, `GameController.cpp:40297-40425`: nächstes Objekt im
Umkreis suchen, dann `GameController_interactSpecialObject @0x488030`:

```c
if (typ != 1 && != 2 && != 3 && != 0x0A && != 0x10
    && != 0x12 && != 0x13 && != 0x45)
        -> "There is nothing special."
```

Trifft ein Typ zu, schaltet `Entity_onStateChangeEmitEffect @0x5A58E0` das Feld
`+0x30` um — damit schließt sich der Kreis: die Tür wird auf/zu, und die
Kollisionsschleife sieht die Änderung sofort.

**Beide Wege gehen ausschließlich über den Index.** Ein Record im Vektor ohne
Indexknoten ist unsichtbar für Physik und Interaktion.

---

## 3. Was ich vorher falsch behauptet hatte

Ich hatte gesagt, Interaktion hänge am **Blocktyp**, belegt mit
`isInteractableTileType @0x47F9F0` und dessen Werten `{1, 0x0A, 0x0B, 0x0C,
0x0D, 0x15}`. Das war falsch: die Funktion ist ein **Item**-Prädikat
(Stapelbarkeit), beide Aufrufstellen liegen im Inventarcode
(`GameController.cpp:25342` und `:35612`). Die Werte sind Item-Typen, keine
Tiles. Dasselbe gilt für `block_isPassableType @0x434870`.

Der Blocktyp regelt nur die Voxelkollision der Wände ringsum. Die Tür selbst
bekommt **gar keinen Block** — im Original wird an der Türstelle bewusst keiner
gesetzt, sonst blockiert er auch im geöffneten Zustand.

---

## 4. Die drei Schritte, die ein Mod braucht

```cpp
// 1. Record anlegen, Zustand "zu"
uint8_t obj[0x188];
FillProp3(obj, typ, x, y, z, rot, sx, sy, sz);
*(int*)(obj + 0x30) = 1;
AppendBuilding(zone, obj);
int idx = (raw[1] - raw[0]) / 0x188 - 1;

// 2. PFLICHT: in den Ortsindex haengen
//    getTileCell32 @0x42F640  __thiscall(World*, cellX, cellY, Zone*)
void* cell = getTileCell32(w, x >> 3, y >> 3, zone);
uint32_t ref[4] = { (uint32_t)&static_objects[idx], zoneX, zoneY, idx };
//    WorldInfo_mapInsertEntry @0x5F4B50  __thiscall(list*, const ObjRef*)
//    ist woertlich std::list::push_back
mapInsertEntry(cell, ref);

// 3. Markerblock raeumen
PutBlock(w, x, y, z, 0,0,0, 0);
```

Konventionen sind aus den Aufrufstellen belegt: `getTileCell32` und
`resolveVectorElementByHandle` rufen beide `world_getRegionCell64` mit zwei
Argumenten, dessen Callee liest `ECX+0xbc` — ECX wird also durchgereicht,
also `__thiscall`.

---

## 5. Die Falle — BEHOBEN

**`ObjRef.obj` ist ein Rohzeiger in den Vektor.** Wächst der Vektor, zieht er
um, und **alle bereits eingehängten Knoten zeigen ins Leere**. Die
Kollisionsschleife folgt dann Müllzeigern — im Spiel als
`0xC0000005: Ausführen an 0x00004800` sichtbar.

Genau deshalb baut `WorldInfo_generateBiomeContent` (`WorldInfo.cpp:5048-5080`)
erst **alle** Objekte und danach in einem zweiten Durchgang den Index.

`ReplaceMarkers` macht es seit dem Umbau ebenso, in fünf Schritten:

1. **Sammeln** — alle Markerblöcke im Hausradius, Position + Typ, Grenze
   `kMaxMarkers = 256`.
2. **Gruppieren** — benachbarte Marker gleicher Farbe (Abstand ≤ 2 je Achse)
   werden zu einer Gruppe. Eine Tür ist 3×2 Blöcke, ein Fenster 2×2; vorher
   entstand pro *Block* ein Objekt, also sechs deckungsgleiche Türen.
3. **Anlegen** — `AppendObjectOnly` je Gruppe, Vektorplatz merken, **kein**
   Indexeintrag.
4. **Indizieren** — erst jetzt `IndexOne`. Der Vektor wächst nicht mehr, `obj`
   wird aus der endgültigen Basis neu berechnet.
5. **Räumen** — Markerblöcke löschen.

### Ankerpunkt einer Gruppe

Der ganzzahlige Durchschnitt war in zwei Richtungen falsch:

| Achse | falsch | richtig | Grund |
|---|---|---|---|
| X / Y | `sum/n` | `(min+max+1)/2` | bei gerader Breite (x = 10,11) ergibt `21/2 = 10` — ein halber Block zu weit links |
| Z | `sum/n` | `min` | ein Objekt hängt an seiner **Unterkante**, nicht an der Mitte; 3 hoch (z = 20..22) landete auf 21 |

Gemessen an Tür 2×3: alt `(10,5,21)` → neu `(11,5,20)`, also `+1` rechts und
`−1` runter — genau der im Spiel beobachtete Versatz.

## 6. Verhaltensmatrix

| gewünscht | `type` | `+0x30` |
|---|---|---|
| Tür zu, fest, benutzbar | 1 | 1 |
| Tür offen, durchlässig | 1 | 0 |
| Truhe | 0x0A | 1 (2 = leer) |
| Stuhl (setzt den Spieler hin) | 0x10 | 1 |
| Bett | 0x13 | 1 |
| Ofen / Amboss / Werkbank | 0x47 / 0x48 / 0x4D | 1 |
| Deko, fest, „nichts Besonderes" | 0x18–0x1C, 0x20–0x22 | 1 |
| Deko, komplett durchlässig | 6, 7, 9 | egal |

Doppeltür: Typ **9** und die Flügel gegenseitig verlinken über
`+0x178/+0x17C/+0x180 = {zoneX, zoneY, indexDesAnderen}`.

## 7. Ungeprüft

- Ob die Kollisionsbox `pos ± skala` oder `pos ± skala*0.5` ist, hängt an
  `ftol2()` und sollte im Spiel gemessen werden.
- Der `add esp,N`-Beleg für die Konventionen ließ sich nicht führen, weil im
  Reversal-Baum nur Pseudocode liegt, keine Binärdatei. Die Herleitung stützt
  sich auf die ECX-Durchreichung und die Server-Zwillinge.
