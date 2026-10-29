# Markerfarben und Objekttypen — der komplette Ablauf

**Kurz:** Ein natives Gebäude wird als Voxelmodell gestanzt und enthält farbige
Platzhalter. Der Mod ersetzt jeden Platzhalter durch ein Objekt, hängt es in den
Ortsindex und räumt den Block weg. Erst der Indexeintrag macht es benutzbar.

---

## 1. Die Markerpalette — gemessen, nicht geraten

Erfasst mit dem Blockscanner (STRG+H) in einem gesetzten Haus. Marker benutzen
je Farbkanal **nur die Stufen 0, 127 und 255**. Baumaterial hat immer
Zwischenwerte (140, 168, 216, 94 …) und fällt damit sauber heraus.

| RGB | Stufe | Bedeutung | Objekttyp | Modell |
|---|---|---|---|---|
| `255,0,0` | 2·0·0 | Tür | `0x01` | door.cub |
| `0,255,0` | 0·2·0 | Fenster | `0x03` | window.cub |
| `255,255,0` | 2·2·0 | Lampe | `0x32` | street-light01.cub |
| `0,0,127` | 0·0·1 | **CustomizeBench** | `0x4D` | customization-bench.cub |
| `0,0,255` | 0·0·2 | Schild (Schmiede/Händler) | `0x2E` | shop-sign |
| `0,255,127` | 0·2·1 | **Amboss** | `0x48` | anvil.cub |
| `127,255,0` | 1·2·0 | **Schmiedeesse** | `0x47` | furnace.cub |
| `127,127,0` | 1·1·0 | Tisch | `0x0C` | table.cub |
| `255,127,127` | 2·1·1 | Blumenkasten | `0x2F` | flower-box01.cub |
| `127,0,127` | 1·0·1 | Schmied steht hier | — | **kein Objekt**, NPC-Standort |

Der frühere Fehler: meine Schwelle war „≥247 oder ≤8". Damit fielen **alle
127er-Marker** durch — genau die blieben als bunte Blöcke stehen.

## 2. Objekttypen — woher sie kommen

Die vollständige Tabelle steht als Vektor mit 78 Einträgen im Binary
(`GameController.cpp:22583`, `tabelle[Objekttyp] = Modell`). Gegengeprüft über
die Crafting-UI, die im Klartext meldet: `0x47` → „Requires Furnace",
`0x48` → „Requires Anvil", `0x4C` → „Requires Workbench".

**Zwei getrennte Nummernkreise beachten.** Fackeln, Laternen und Schilder gibt
es auch als Chunk-Props bei `chunk+0x04` mit eigener Nummerierung (Laterne Prop
`0x0D`, Fackel Prop `0x0E`). Ein 0x188-Record mit Typ `0x22` wäre ein Regal,
kein Schild.

## 3. Was ein Objekt benutzbar macht

Drei Schritte, alle nötig — Reihenfolge zählt:

```cpp
// 1. Record anlegen, Zustand "zu"/aktiv
FillProp3(obj, typ, x, y, z, 0, sxy, 1.0f, 1.0f);
*(int*)(obj + 0x30) = 1;
AppendBuilding(zone, obj);

// 2. PFLICHT: in den Ortsindex zone+0xAC haengen
void* cell = getTileCell32(x>>3, y>>3, zone);      // @0x42F640 __thiscall
ObjRef ref = { &static_objects[idx], zoneX, zoneY, idx };
WorldInfo_mapInsertEntry(cell, &ref);              // @0x5F4B50 __thiscall

// 3. Markerblock raeumen -- die Tuer selbst braucht KEINEN Block
PutBlock(w, x, y, z, 0,0,0, 0);
```

**Ohne Schritt 2 passiert nichts.** Physik und Zielsuche lesen ausschließlich
den Ortsindex, niemals den `static_objects`-Vektor. Das war der Grund, warum
Türen monatelang reine Optik waren.

Verhaltensmatrix:

| gewünscht | `type` | `+0x30` |
|---|---|---|
| Tür zu, fest, benutzbar | 1 | 1 |
| Tür offen, durchlässig | 1 | 0 |
| Truhe | 0x0A | 1 |
| Werkbank/Amboss/Ofen | 0x47/0x48/0x4D | 1 |
| Deko durchlässig | 6, 7, 9 | egal |

## 4. Aufrufparameter von placeStructure

`WorldInfo_placeStructure @0x5F0CE0`, `__stdcall`, 9 Argumente.

| # | Wert | Anmerkung |
|---|---|---|
| 1 | Modellzeiger | aus `vectorU32_at(index)` |
| 2 | `int[3]` Position | untere Ecke, **nicht** zentriert |
| 3 | Drehung 0..3 | −90° je Schritt |
| 4 | `6` | Farbe |
| 5 | **`0`** | mode. 1 → 1 von 4 Gebäuden; 0x0D/0x0E → gar keins |
| 6 | `cube::Zone*` | |
| 7 | `1` | |
| 8 | `int[4]` Beschnitt | immer `{0,0,0,0}` |
| 9 | **`0`** | **Zeiger auf std::list, KEIN Flag.** `1` = Zugriff auf `0x19` |

Danach `Chunk::needs_update = 1` auf allen geladenen Chunks, sonst unsichtbar.

## 5. Speicher — zwei behobene Fallen

`AppendBuilding` setzte die Kapazität gleich dem Ende; jede Anhängung
allozierte neu, ohne freizugeben. 1600 Objekte = 479 MB je Dorf, ~2 GB bei
vier → „nicht genügend Speicherressourcen". Jetzt Kapazitätsverdopplung: 1,5 MB.

`IndexInsert` nahm einen 12-Byte-Listenkopf an; er hat **8** (`{_Myhead,
_Mysize}`, ohne Proxy). Das `size++` schrieb in den Kopf der nächsten
Indexzelle. Jetzt wird die Spielfunktion benutzt.

## 6. Geometrie

- Registry: `GC+0x300`, 2569 Einträge
- Häuser: `0x87E`, `0x87F`, `0x880` (33×33) · Schmiede `0x903` (32×31) · Baum `0x832`
- Hausraster: 13 Blöcke je Zelle, 7 je Stockwerk
- Z: Gebäude auf `deckZ − 7` (Generator: Terrainoberkante − 7)

## 7. Offen

- **Proportionen von Tür und Fenster** stimmen noch nicht — die nativen Skalen
  je Typ stehen im Objektscan (`HausScan.txt`), sind aber noch nicht übernommen.
  Aktuell wird pauschal 1.0/1.0/1.0 gesetzt.
- `0x2E` für das Schild ist die einzige Zuordnung ohne zweiten Beleg.
- Ob der Schmied-Marker `127,0,127` automatisch als NPC-Standort genutzt werden
  soll, ist noch nicht umgesetzt — aktuell wird dort nichts gesetzt.
