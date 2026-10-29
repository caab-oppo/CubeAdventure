# RE: `WorldInfo_placeStructure` @ 0x5F0CE0 — wie native Gebaeude in die Welt kommen

**BLUF:** Der `zone == 0x0D`-Zweig laeuft nur dann ohne Crash, wenn `param_6` ein
**gueltiger Chunk-Zeiger** ist. Alle zonenspezifischen Marker-Zweige (0x47, 0x48, 0x4D,
0x26 usw.) haengen ihr erzeugtes Objekt in Container **innerhalb von `param_6`** ein
(`std::vector<0x188>` bei `param_6+0x0C/+0x10/+0x14` bzw. `std::list` bei
`param_6+0x04/+0x08`). Mit `param_6 == 0` fuehrt der erste getroffene Markerblock zu einer
Null-Dereferenzierung auf Adresse 0x0C bzw. 0x04. Zusaetzlich gilt: ist `param_6 != 0`,
aber die Struktur-Bounding-Box ueberlappt die 256x256-Flaeche dieses Chunks **nicht**,
kehrt die Funktion **wortlos zurueck** (kein Crash, aber auch kein Gebaeude) — siehe
`WorldInfo.cpp:5307-5340`.

Quelle: Ghidra-Decompile, `cube/world/WorldInfo.cpp` Zeilen 5099-7143 (Funktionsrumpf),
Aufrufer `Chunk_generateObjects` @ 0x5AF170 in `cube/game_misc/game_misc.cpp` 23028-30762,
plus `WorldInfo_generateBiomeContent` @ 0x5E4850 (`WorldInfo.cpp` ab Zeile 888).

---

## 1. Argumenttabelle

| # | Name im Decompile | Bedeutung | Was wird dereferenziert | Verhalten bei 0 / ungueltig |
|---|---|---|---|---|
| 1 | `int dungeon` | **Voxel-Modell (Vorlage)**, aus `vectorU32_at` | `+0x30` = Farbdaten (3 Byte/Voxel), `+0x44` = X-Groesse, `+0x48` = Y-Groesse, `+0x4C` = Z-Groesse | **BELEGT**: sofortiger Absturz, `+0x44` wird als Allererstes gelesen (Z. 5297) |
| 2 | `int *origin` | `int[3]` Weltposition (Blockkoordinaten), **Ecke, nicht Mitte** | `origin[0..2]` (Z. 5303-5305), spaeter nochmal `*local_6f0`, `local_6f0[1]` (Z. 5343) | **BELEGT**: Absturz |
| 3 | `uint param_3` | Rotation 0..3 (`param_3 & 3`). Beeinflusst Koordinaten-Mapping und Objekt-Winkel (`param_3 * -0x5A` = -90 Grad/Schritt, Z. 5989, 6265) | — (Wert) | Wird maskiert, harmlos |
| 4 | `byte param_4` | **Blocktyp/Flags** des gesetzten Voxels: `setColor4b(farbe, param_4 \| 0x40)` (Z. 7129) | — (Wert) | harmlos. Vanilla: `6` (Chunk_generateObjects) bzw. `0x28` (`game_misc.cpp:45867`) |
| 5 | `int zone` | **Kultur-/Gebaeudetyp-ID**, KEIN Zonenzeiger. Steuert die Interpretation der Markerfarben | — (Wert) | harmlos, aber `zone==0` schaltet die gesamte Marker-Logik AB (Z. 5641-5645) |
| 6 | `int param_6` | **Chunk-/Field-Objekt** (256x256-Kachel). Siehe Layout unten | `+0x60`,`+0x64` (Z. 5311/5318), `+0x04`/`+0x08` (Objektliste), `+0x0C`/`+0x10`/`+0x14` (Objektvektor), `+0xA8` indirekt ueber `world_getChunkCell256` | `0` ist fuer den reinen Blockbau **erlaubt** (globale Suche), aber **jeder Marker-Zweig crasht** |
| 7 | `char param_7` | **Fundament bauen**: verlaengert die Struktur nach unten bis zum Boden (Z. 5432-5487) | — (Wert) | harmlos |
| 8 | `int *param_8` | **`int[4]` Beschnitt-Raender** (crop margins), NICHT Farbpuffer. Vanilla immer `{0,0,0,0}` | `param_8[0..3]` (Z. 5361-5381) | **BELEGT**: Absturz — wird unbedingt gelesen |
| 9 | `int param_9` | **Optionaler Aenderungs-Recorder**: `std::list` bei `+0x18`, Groesse bei `+0x1C`. Jeder geschriebene Voxel wird zusaetzlich als `{pos, rgba}` protokolliert | `+0x18`, `+0x1C` — aber **immer** hinter `if (param_9 != 0)` | **`0` ist der Normalfall.** Alle Vanilla-Aufrufstellen uebergeben 0 |

### Korrekturen am bisherigen Kenntnisstand

* **`param_2` wird NICHT in der Funktion zentriert.** Die Zentrierung `base - breite/2`
  macht der **Aufrufer**. BELEGT in `WorldInfo.cpp:3040-3041`:
  ```c
  local_2b4 = (int)*(float*)(iVar26+0x8000f0) - *(int*)((int)local_1308 + 0x44) / 2;
  local_2b0 = (int)*(float*)(iVar26+0x8000f4) - *(int*)((int)local_1308 + 0x48) / 2;
  WorldInfo_placeStructure(local_1308,&local_2b4,0,6,0,uVar5,1,local_1668);
  ```
  Ebenso `game_misc.cpp:28770-28773` und `28843-28846`. `placeStructure` selbst benutzt
  `origin` als **untere linke Ecke**.
* **`param_8` ist kein Farbpuffer.** BELEGT: `game_misc.cpp:28964-28966` /
  `45855-45860` fuellen es mit `setVec4(0,0,0,0); vec4_floatToInt(...)`, also 4 Ints = 0;
  in `placeStructure` Z. 5359-5381 werden die 4 Werte als Start-/Endversatz auf die
  Modell-Schleifengrenzen angewendet.
* **`param_5`/`zone` ist keine Zone**, sondern eine kleine Typ-ID. Die echte Zone/Chunk ist
  `param_6`.
* **`param_9` steuert nicht "das Loeschen von Markerbloecken"**, sondern protokolliert
  *alle* Blockschreibvorgaenge (inkl. der "Marker wird zu Luft"-Eintraege) in eine Liste.
  Ohne `param_9` werden die Bloecke trotzdem gesetzt (ueber `tilemap_writeGlyphColumn`).

### Chunk-Layout (`param_6`) — abgeleitet

```
+0x04  std::list<Prop, node=0x48>  Kopf      // Baeume/Requisiten/Moebel-Props
+0x08  size_t                                 // Kappung 0x38E38E2 (= 0xFFFFFFFF/0x48)
+0x0C  T* begin   \
+0x10  T* end      >  std::vector<Interaktionsobjekt>, Elementgroesse 0x188
+0x14  T* cap     /
+0x60  int chunkX   (in 256-Bloecken)
+0x64  int chunkY   (in 256-Bloecken)
+0xA8  Cell[256*256], Stride 0x20         // ueber world_getChunkCell256
```
BELEGT:
* `+0x60/+0x64/+0xA8`: `world_getChunkCell256` @ 0x4347A0, `GameController.cpp:12628-12656`.
* `+0x04/+0x08`: inline expandiert in `WorldInfo.cpp:5945-5952`
  (`iVar4 = *(int*)(local_6e8+4); ChunkBuffer_loadAndNotify(); if (*(int*)(param_6+8)==0x38e38e2) ...; *(int*)(param_6+8)++;`).
* `+0x0C/+0x10/+0x14`: inline expandiert in `WorldInfo.cpp:5724-5744`
  (`local_6a8 = param_6 + 0xc; ... *(uint**)(param_6+0x10) ... *(int*)(param_6+0x14) ... std_vector_Reserve_grow_188(1)`).
* Dass es dasselbe Objekt ist, bestaetigt `RandomInteractionBehavior.cpp:219-222`:
  `chunkY = world_getRegionCell64(x,y); iVar3 = *(int*)(chunkY+0xc); idx < (*(int*)(chunkY+0x10)-iVar3)/0x188` —
  d.h. der Vektor mit Stride 0x188 haengt an genau dem Objekt, das die Chunk-Aufloesung liefert.

Zell-Layout (`world_getChunkCell256`-Rueckgabe, Stride 0x20), BELEGT aus Nutzung:
`+0x08` float (Feuchte/Vegetation), `+0x10` int Basis-Z der Spalte, `+0x18` Zeiger auf
Voxelarray (4 Byte/Voxel), `+0x1C` Anzahl Voxel.

---

## 2. Ablauf als kommentiertes Pseudo-C

```c
void WorldInfo_placeStructure(
        Model*  model,     // param_1  Voxel-Vorlage (vectorU32_at)
        int     origin[3], // param_2  Weltposition, UNTERE ECKE
        uint    rot,       // param_3  Rotation 0..3
        byte    blockFlags,// param_4  Blocktyp, wird mit |0x40 geschrieben
        int     zone,      // param_5  Kultur-/Gebaeudetyp-ID
        Chunk*  chunk,     // param_6  Ziel-Chunk (darf 0 sein, s. Warnungen)
        char    buildBase, // param_7  Fundament nach unten bauen
        int     crop[4],   // param_8  Beschnittraender, vanilla {0,0,0,0}
        Recorder* rec)     // param_9  optionales Aenderungsprotokoll (vanilla 0)
{
    /* --- 5290..5302: Ausdehnung holen; bei ungerader Rotation X/Y tauschen ------ */
    bool even = ((rot & 1) == 0);                  // Z.5290-5296
    int  sx = model->sizeX /*+0x44*/, sy = model->sizeY /*+0x48*/;   // Z.5297-5298
    if (!even) { sx = model->sizeY; sy = model->sizeX; }             // Z.5299-5301

    int bx = origin[0], by = origin[1], bz = origin[2];              // Z.5303-5305

    /* --- 5307..5340: EARLY-OUT. Nur weitermachen, wenn die Bounding-Box der -----
       Struktur die 256x256-Flaeche dieses Chunks ueberlappt. Bei chunk==0 wird
       der Test uebersprungen (dann greift spaeter die globale Chunksuche).      */
    if (chunk != 0) {
        int cx = chunk->chunkX * 256;                                // Z.5311
        int cy = chunk->chunkY * 256;                                // Z.5318
        if (!(cx < bx + sx && cy < by + sy && bx < cx + 256 && by < cy + 256))
            return;                                 // <== STILLES NICHTS-TUN
    }

    /* --- 5341..5349: "Feuchte" fuer vegetationsabhaengige Marker --------------- */
    Cell* c = world_getChunkCell256(origin[0], origin[1], chunk);
    float humid = (c == 0) ? GameController_sampleHumidityGrid(origin[0], origin[1])
                           : *(float*)(c + 8);

    /* --- 5351..5382: Schleifengrenzen aus dem Modell + Beschnitt crop[0..3] ---- */
    int x0 = 0, y0 = 0, x1 = model->sizeX, y1 = model->sizeY;        // Z.5351-5352
    switch (rot & 3) {                                               // Z.5359-5381
      case 0: x0=crop[0]; y0=crop[1]; x1-=crop[2]; y1-=crop[3]; break;
      case 1: x0=crop[3]; y0=crop[0]; x1-=crop[1]; y1-=crop[2]; break;
      case 2: x0=crop[2]; y0=crop[3]; x1-=crop[0]; y1-=crop[1]; break;
      case 3: x0=crop[1]; y0=crop[2]; x1-=crop[3]; y1-=crop[0]; break;
    }

    for (int mx = x0; mx < x1; ++mx)                                 // Z.5385-5391
    for (int my = y0; my < y1; ++my) {                               // Z.5387/5478

        /* --- 5393..5426: Vorabscan der Saeule: tiefstes und hoechstes ----------
           nicht-leeres Voxel bestimmen (zLo/zHi). "leer" = Farbe == DAT_0076b340. */
        int zLo = model->sizeZ, zHi = 0;                             // Z.5394-5396
        for (int mz = 0; mz < model->sizeZ; ++mz) {
            byte* col = &model->rgb[((model->sizeY*mz + my)*model->sizeX + mx)*3]; // Z.5406-5409
            if (memcmp(col, DAT_0076b340, 3) != 0) {                 // Z.5413
                zLo = min(zLo, mz); zHi = max(zHi, mz);              // Z.5415-5420
            }
        }

        /* --- 5432..5487: FUNDAMENT (nur wenn buildBase != 0) -------------------
           Wenn das Modell auf Hoehe 0 an (mx,my) gefuellt ist, wird von origin[2]-1
           abwaerts bis zur Bodenhoehe (cell+0x10) aufgefuellt, solange der dortige
           Block Luft(0) oder Wasser(2) ist.                                       */
        if (buildBase) {
            byte* col0 = &model->rgb[(model->sizeX*my + mx)*3];      // Z.5426-5431 (mz=0)
            if (memcmp(col0, DAT_0076b340, 3) != 0) {
                int wx, wy; rotate(rot, mx, my, &wx, &wy);           // Z.5445-5464
                Cell* cc = world_getChunkCell256(wx, wy, chunk);     // Z.5468
                for (int h = bz-1; cc && h > cc->baseZ; --h) {       // Z.5470-5497
                    GameController_voxelAt3D();                      // Z.5496
                    tilemap_writeGlyphColumn();                      // Z.5497  <-- Fundamentblock
                    if (blockTypeAt(h) is solid) break;              // Z.5518-5522
                }
            }
        }

        /* --- 5537..7141: Hauptschleife ueber Z, VON OBEN NACH UNTEN ------------ */
        for (int mz = model->sizeZ - 1; mz >= 0; --mz) {             // Z.5535-5539 / 7136-7141

            /* 5541..5566: Modellkoordinate -> Weltkoordinate, abhaengig von rot  */
            int wx, wy, wz;
            switch (rot & 3) {
              case 0: wx = bx + mx;                     wy = by + my;                     break;
              case 1: wx = bx + my;                     wy = by + (model->sizeX-mx) - 1;   break;
              case 2: wx = bx + (model->sizeX-mx) - 1;  wy = by + (model->sizeY-my) - 1;   break;
              case 3: wx = bx + (model->sizeY-my) - 1;  wy = by + mx;                      break;
            }
            wz = bz + mz;                                            // Z.5545/5567

            /* 5568..5579: Zeiger auf die 3 Farbbytes dieses Modellvoxels         */
            byte* rgb = &model->rgb[((model->sizeY*mz + my)*model->sizeX + mx)*3];

            /* 5581..5584: LEER? -> nicht bauen, aber ggf. Innenraum aushoehlen   */
            if (memcmp(rgb, DAT_0076b340, 3) == 0) {                 // Z.5581-5651
                if (zLo < mz && mz < zHi) {          // nur INNERHALB der Saeule  Z.5655
                    if (rec) recordBlock(rec, wx,wy,wz, 0xFF,0xFF,0xFF, 0xC0); // Z.5657-5673
                    tilemap_writeGlyphColumn(/* Luft, Flags 0xC0 */);          // Z.5675 -> LAB_005f4a73
                }
                continue;                                            // Z.5676/5679
            }

            /* 5586..5636: Bereits gesetzter Block mit Flag 0x40 an dieser Stelle?
               Dann pruefen, ob dort schon fester Untergrund ist -> Voxel skippen  */
            if (existingBlockAt(wx,wy,wz).flags & 0x40) {            // Z.5608
                if (typeOf(existingBlockAt(...)) not in {0,2}) continue;  // Z.5632-5635
            }

            /* 5637..5645: DIE ZONEN-WEICHE.
               Nur fuer zone in {1,2,3,4,5,6,7,9,10,11,12,13,14,15} werden die
               Markerfarben ueberhaupt interpretiert. Fuer zone==0 (und zone>15)
               springt der Code direkt zum normalen Blockschreiben -> die farbigen
               Markerbloecke bleiben als BLOECKE stehen. Genau das beobachtete
               Verhalten bei param_5=0.                                            */
            if (zone != 1 && zone != 2 && zone != 6 && zone != 3 && zone != 4 &&
                zone != 5 && zone != 7 && zone != 9 && zone != 10 && zone != 0xb &&
                zone != 0xc && zone != 0xd && zone != 0xe && zone != 0xf)
                goto WRITE_PLAIN_BLOCK;                              // Z.5641-5646 -> LAB_005f493b

            /* 5647..7105: Marker-Kette, siehe Tabelle Abschnitt 3               */
            ...

        WRITE_PLAIN_BLOCK:                                           // LAB_005f493b, Z.7108
            /* 7110..7124: Sonderfall zone==8: eine bestimmte Farbe wird durch
               einen Block mit Typ 2 (Wasser) ersetzt.                            */
            if (zone == 8 && color_equals3(rgb, <Farbe_W>)) {        // Z.7110-7113
                if (rec) recordBlock(rec, wx,wy,wz, 0xFF,0xFF,0xFF, 2);  // Z.7114-7121
                setColor3b(...);                                     // Z.7122
                goto EMIT;                                           // Z.7123
            }
            /* 7125..7131: NORMALFALL — Modellfarbe 1:1 in die Welt schreiben.
               blockFlags|0x40 markiert den Block als "kuenstlich/Bauwerk".        */
            if (rec) {
                clearTwoFields();
                setColor4b(rgb, blockFlags | 0x40);                  // Z.7129
                color_copy4(); vec3_copy(&wx);
                list_pushBack_via451e80(&record);                    // Z.7131 (rec+0x18)
            }
        EMIT:                                                        // LAB_005f4a46, Z.7086
            setColor4b(/* rgb, blockFlags|0x40 */);                  // Z.7087
            arrayElem_stride4(); arrayElem_stride4(1); arrayElem_stride4();
            tilemap_writeGlyphColumn(wx, wy, wz, &rgba, chunk);      // Z.7094 (LAB_005f4a73)
        }
    }
}
```

### Der eigentliche Blockschreibvorgang

`tilemap_writeGlyphColumn` @ 0x44E7A0 (`_library/audit.cpp:5757-5796`):

```c
cell = world_getChunkCell256(x, y, chunk);   // <-- chunk == param_6
if (cell == 0) return;                        // ausserhalb -> STILL VERWORFEN
sparseArray_setAt(z - cell->baseZ, &rgba);    // setzt den Voxel
// danach: alle Bloecke oberhalb bis cell->baseZ werden mit Typ 6 ueberschrieben
```

**Konsequenz (BELEGT):** wird `param_6` als konkreter Chunk uebergeben, aber die
Struktur liegt (teilweise) ausserhalb dieses Chunks, verschwinden die betroffenen
Bloecke ersatzlos — ohne Fehler. Mit `param_6 = 0` sucht `world_getChunkCell256` den
Chunk global (`world_getRegionCell64`), dann funktioniert der Blockbau ueberall — aber
die Marker-Zweige crashen.

---

## 3. Tabelle aller Markerzweige

Die Marker werden in fester Reihenfolge geprueft; die erste passende Farbe gewinnt.
Farben als (R,G,B). "Typ" = `local_308[0]` / `local_618[0]` / `local_78[0]`, also die
Objekt-Typ-ID.

### 3a. Marker mit im Decompile sichtbarer Farbkonstante

| # | Farbe | Zeile Konstante | Zeile Rumpf | Bedingung | Erzeugtes Objekt | Ziel-Container |
|---|---|---|---|---|---|---|
| 1 | (FF,00,00) rot | 5647-5649 | 5698-5789 | `World_computeStructurePlacement` liefert != 0 | Typ **`(zone == 7) ? 2 : 1`** | `chunk+0x0C` Vektor (inline, Z.5724-5772) |
| 2 | (00,FF,00) gruen | 5691-5693 | 5862-5900 | `World_computeStructurePlacement` != 0 | Typ **3** | `chunk+0x0C` (`std_vector_pushBack_0x188`) |
| 3 | (00,7F,7F) petrol | 5855-5857 | 5915-5966 | `humid > 0.5` **und** Block dort Luft/Wasser **und** `rand()%5 == 0` | Typ **`rand()%3 + 0x29`** (0x29..0x2B) | `chunk+0x04` Liste (inline belegt Z.5945-5952) |
| 4 | (FF,7F,00) orange | 5910-5912 | 5993-6046 | immer | Typ **0x13** und Typ **0x14** (zwei Objekte) | `chunk+0x0C` (2x) |
| 5 | (FF,7F,7F) lachs | 5986-5988 | 6070-6115 | immer | Typ **`rand()%3 + 0x20`** (0x20..0x22) | `chunk+0x0C` |
| 6 | (7F,7F,00) oliv | 6055-6057 | 6129-6229 | immer | Typ **0x0C** und Typ **0x10** (zwei Objekte, zweites doppelt platziert) | `chunk+0x0C` |
| 7 | (FF,FF,00) gelb | 6123-6125 | 6252-6298 | immer | Typ **0x0D** | `chunk+0x04` Liste |
| 8 | (3F,00,00) dunkelrot | 6246-6248 | 6313-6355 | immer | Typ **0x30** | `chunk+0x04` Liste |
| 9 | (00,00,FF) blau | 6310-6312 | 6998-7069 | `zone`-abhaengig, sonst kein Objekt | `zone==2`->**0x21**, `3`->**0x22**, `4`->**0x23**, `5`->**0x24**, `6`->**0x25**, `0x0D`->**0x26**, `0x0E`->**0x27**, `0x0F`->**0x28**, sonst nichts | `chunk+0x04` Liste |

### 3b. Marker, deren Farbkonstante der Decompiler verschluckt hat

Diese neun/sechs Vergleiche laufen ueber `setColor3b(<konstante>)` + `color_equals3()`.
Ghidra hat die Argumente von `setColor3b` fallengelassen, die konkreten RGB-Werte sind
aus diesem Decompile **nicht** rekonstruierbar (siehe Abschnitt "UNSICHER").

| # | Kennung | Zeile Vergleich | Bedingung | Erzeugtes Objekt |
|---|---|---|---|---|
| A | Farbe A | 6363-6365 | `humid > 0.5` **und** `World_computeStructurePlacement` != 0 | Typ **`rand()%3 + 0x2F`** (0x2F..0x31), Z.6969 |
| B | Farbe B | 6367-6369 | `zone == 0x0D` | Typ **0x47**, Z.6816-6820 |
| B | Farbe B | " | `zone == 0x0E` | Typ **0x4B**, Z.6849-6853 |
| B | Farbe B | " | `zone == 0x0F` | Typ **0x49**, Z.6926-6930 |
| B | Farbe B | " | sonst (zone 1..7, 9..0x0C) | **`cube::Spawn` (0x10F0 Bytes)**, `+0x28`=6, `+0x2C`=`(zone!=10)?0x8E:0x8D`, Z.6883-6900 |
| C | Farbe C | 6371-6373 | `zone == 0x0D` | Typ **0x4D**, Z.6375-6404 — **sonst passiert gar nichts** |
| D | Farbe D | 6408-6411 | `zone == 0x0D` | Typ **0x48**, Z.6713-6717 |
| D | Farbe D | " | `zone == 0x0E` | Typ **0x4C**, Z.6746-6750 |
| D | Farbe D | " | `zone == 0x0F` | Typ **0x4A**, Z.6782-6786 |
| D | Farbe D | " | sonst | nichts (`goto LAB_005f4a7e`, Z.6782) |
| E | Farbe E | 6412-6415 | immer | Typ **`rand()%9 + 0x38`** (0x38..0x40), Z.6676-6680 |
| F | Farbe F | 6416-6418 | immer | **`cube::Spawn` (0x10F0)** mit NPC-Verhalten, grosser `switch(zone)` Z.6452-6604 |
| — | keine Uebereinstimmung | 6418 | — | normaler Block (`goto LAB_005f493b`) |
| W | Farbe W | 7110-7113 | `zone == 8` | Block mit Typ **2** (Wasser), Z.7114-7123 |

### 3c. NPC-Zweig (Farbe F) — `switch(zone)`, Z.6452-6604

| zone | Wirkung |
|---|---|
| 6 | `spawn+0x30 = 0x83` |
| 7 | `spawn+0x34 = 0x14`, `+0x2C = rand()%2 + 2`, `+0x30 = 0x89` |
| 9 | Vollausruestung, `+0x30 = 0x01`, Items 0x0F..0x11 |
| 10 | wie 9, `+0x30 = 0x02`, Item-Set 0x1A |
| 0x0B | `+0x30 = 0x04`, Item-Set 0x1B |
| 0x0C | `+0x30 = 0x03`, Item-Set 0x19 |
| **0x0D** | **`spawn+0x30 = 0x85`** |
| 0x0E | `spawn+0x30 = 0x86` |
| 0x0F | `spawn+0x30 = 0x87` |

Danach: `SequentialBehavior`, `CombatBehavior(20.0f)`, `LookAtPlayerBehavior`,
`WalkPathBehavior(2.0f)` werden angehaengt und der Spawn per
`std_vector_push_back_4byte` in einen Zeigervektor eingetragen (Z.6606-6647).

---

## 4. Vorbedingungen fuer den `zone == 0x0D`-Zweig

### Was der Zweig konkret braucht (BELEGT)

1. **`param_5 == 0x0D`.** Sonst greift entweder die Zonenweiche (Z.5641) oder die
   `zone==0xd`-Abfragen laufen ins Leere.
2. **`param_6` muss ein gueltiger Chunk sein.** Alle 0x0D-Zweige enden in
   `std_vector_pushBack_0x188(...)` bzw. `list_pushBack_via5c03f0(...)`. Beide Helfer
   erwarten den Container in `ECX`:
   * `std_vector_pushBack_0x188` @ 0x5A0E00 liest `in_ECX[0..2]` (`audit.cpp:37292-37310`).
   * `list_pushBack_via5c03f0` @ 0x4862E0 liest `*in_ECX` und `in_ECX[1]` (`audit.cpp:12221-12232`).
   Fuer den ROT-Zweig ist der Container **inline sichtbar** und lautet `param_6 + 0x0C`
   (`WorldInfo.cpp:5724`, `5730`, `5744`), fuer den Blau-Zweig `param_6 + 0x04`
   (`WorldInfo.cpp:5945-5952`). => `param_6 == 0` bedeutet Schreibzugriff auf
   Adresse 0x0C bzw. 0x04 => Access Violation.
3. **`param_1` (Modell) muss die passenden Markerfarben enthalten.** Zone und Modell
   sind in Vanilla **fest gepaart** (`game_misc.cpp:28654-28683`):

   | Grundstuecks-Typ (`+0x0C`) | `zone` an placeStructure | Modell-Index (`vectorU32_at`) |
   |---|---|---|
   | 10 (default) | 9 | 0x8FF |
   | 11 | 0x0A | 0x900 |
   | 12 | 0x0B | 0x901 |
   | 13 | 0x0C | 0x902 |
   | 14 | **0x0D** | **0x903** |
   | 15 | 0x0F | 0x905 |
   | 16 | 0x0E | 0x904 |

   Ein Haus-Modell aus einem anderen Index (z.B. 0x881 / 0x887, die vanilla mit `zone=7`
   gesetzt werden) enthaelt die 0x0D-Marker (Farben B/C/D) mit hoher Wahrscheinlichkeit
   gar nicht — dann passiert im 0x0D-Zweig schlicht nichts.
4. **Die Bounding-Box muss den Chunk ueberlappen** (Z.5307-5340), sonst kehrt die
   Funktion sofort und wortlos zurueck.
5. **`param_8` muss auf 4 lesbare Ints zeigen** (Z.5361-5381). Ein `nullptr` hier ist
   ein sicherer Crash — unabhaengig von `zone`.

### Erklaerung der beobachteten Symptome

| Beobachtung | Erklaerung |
|---|---|
| `param_5 = 0`: Gebaeude steht, Marker bleiben farbige Bloecke | **BELEGT.** `zone == 0` faellt durch die Weiche Z.5641-5645 nach `LAB_005f493b`, dort wird jede Modellfarbe 1:1 als Block geschrieben (Z.7125-7131). Kein einziger Container wird angefasst — deshalb auch kein Crash. |
| `param_5 = 1/2`: Tueren entstehen, 3 von 4 Aufrufen werfen | **VERMUTET, gut gestuetzt.** Bei zone 1/2 sind nur wenige Marker aktiv (v.a. Farbe B -> `cube::Spawn`, Blau -> Typ 0x21 bei zone==2, sowie die zonenunabhaengigen Marker 1-8). Jeder davon schreibt in `param_6`-Container. Ob ein Aufruf crasht, haengt also davon ab, **ob das jeweilige Modell einen solchen Marker enthaelt** — dass genau die 4 Rotationen unterschiedlich ausgehen, deutet zusaetzlich darauf hin, dass durch die Rotation unterschiedliche Modellbereiche in den (evtl. falschen) Chunk fallen. |
| `param_5 = 0x0D/0x0E`: gar kein Gebaeude | **VERMUTET.** Bei 0x0D/0x0E sind zusaetzlich die Farben B, C, D und Blau aktiv. Wenn der erste bearbeitete Modell-Voxel (Schleifenreihenfolge: `mx` aussen, `my` innen, `mz` **von oben nach unten**) auf einen dieser Marker trifft, fliegt die Exception, bevor nennenswerte Bloecke geschrieben wurden. |

### Praktische Empfehlung

* **`param_6` niemals 0 lassen**, wenn `param_5 != 0`. Den Chunk, in dem `origin` liegt,
  vorher aufloesen — z.B. ueber `world_getRegionCell64(origin[0] >> 8, origin[1] >> 8)`
  (@ 0x434D10, `game_misc.cpp:2412-2432`) bzw. genau ueber den Pfad, den
  `world_getChunkCell256(x, y, 0)` intern nimmt. Der zurueckgegebene Zeiger ist derselbe
  Objekttyp, der als `param_6` erwartet wird.
* Weil `param_6` gleichzeitig die Blockschreibvorgaenge auf **diesen einen** 256x256-Chunk
  begrenzt (`world_getChunkCell256` Z.12633-12648 gibt 0 fuer Fremdkoordinaten zurueck),
  muss die gesamte Struktur in diesem Chunk liegen — sonst wird sie abgeschnitten.
  Vanilla loest das, indem `Chunk_generateObjects` nur Positionen innerhalb des eigenen
  Chunks erzeugt (`game_misc.cpp:28770-28773`: `chunkX*0x100 + offset`).
* Wenn nur die Marker **verschwinden** sollen (ohne Moebel/NPCs), ist `param_5 = 0`
  plus eine eigene Nachbearbeitung der Markerfarben der sicherste Weg — dann wird kein
  einziger `param_6`-Container beruehrt.

---

## 5. Was der Aufrufer `Chunk_generateObjects` @ 0x5AF170 macht

`void Chunk_generateObjects(int chunk, undefined4* params)` — der erste Parameter heisst
im Decompile `zone`, wird sofort nach `local_5c74` kopiert (`game_misc.cpp:23583`) und
**genau so** als `param_6` an `placeStructure` weitergereicht. Er ist der Chunk.

Vorbereitung vor den Aufrufen (BELEGT):
* Z.23590-23600: fuellt einen Vektor mit Grundstuecks-IDs `{0x22, 0x1E, 0x13, 0x1A, 0x21}`.
* Z.23600-23607: `local_5cd0` = 4 oder 5 (Rasteraufloesung), Zellgroesse `0x100 / n`;
  `std_vector_resize_5bffd0(n*n)` legt die Grundstueckstabelle `local_5d2c` an
  (Stride 0x1C, Felder: `+0x04` Basis-Z, `+0x0C` Typ, `+0x14` Rotation).
* Z.24122-24124: `local_5cfc`/`local_5cf8` = Zeiger auf die Chunk-Koordinaten (X/Y).
* Vor jedem `placeStructure` wird die Bodenhoehe ermittelt: entweder ueber
  `world_getChunkCell256(...)` + `cell->baseZ + cell->count` (Z.24135-24136), oder mit
  einer `world_getColumnData(...) / block_isSolidType()`-Schleife, die so lange
  `z--` macht, bis fester Boden erreicht ist (Z.28777-28787, 28820-28829).

Konkrete Aufrufe (alle `__stdcall`, 9 Argumente):

| Zeile | Grundstuecks-Typ | Modell | `param_3` rot | `param_4` | `param_5` zone | `param_6` | `param_7` | `param_8` | `param_9` |
|---|---|---|---|---|---|---|---|---|---|
| 28519 | 3 | `vectorU32_at(0x881)` | Tabelle `+0x14` | **6** | **7** | chunk | **1** | `{0,0,0,0}` | **0** |
| 28534 | 4 | `vectorU32_at(0x887)` | Tabelle `+0x14` | 6 | 7 | chunk | 1 | `{0,0,0,0}` | 0 |
| 28757 | 10..16 | `vectorU32_at(0x8FF/0x900..0x905)` | Tabelle `+0x14` | 6 | **9..0x0F** | chunk | 1 | `{0,0,0,0}` | 0 |
| 28792 | 5 | `vectorU32_at(0x882)` | Tabelle `+0x14` | 6 | **8** | chunk | 1 | `{0,0,0,0}` | 0 |
| 28831 | 0x11 | `vectorU32_at(0xA04)` | Tabelle `+0x14` | 6 | ? | chunk | 1 | `{0,0,0,0}` | 0 |
| 28902 | weitere | — | — | 6 | — | chunk | 1 | `{0,0,0,0}` | 0 |
| 28971 / 29009 | — | `local_5c68` | Tabelle `+0x14` | **6** | **0** | — | — | — | — |

Zusaetzlicher Aufrufer ausserhalb: `WorldInfo_generateBiomeContent` @ 0x5E4850,
`WorldInfo.cpp:3042` — `placeStructure(modell, &pos, 0, 6, 0, chunk, 1, crop0)`;
dieselbe Funktion ruft in Zeile 3267 auch `Chunk_generateObjects(chunk)` auf. Und
`World_generateWaterOrPathFeature` @ 0x5DF960, `game_misc.cpp:45867-45994` — dort mit
`param_4 = 0x28`, `zone = 0`, `param_7 = 0`, `param_9 = 0`.

**Wichtigste Erkenntnis daraus:** `param_9` ist in **keinem** Vanilla-Aufruf ungleich 0.
Die `if (param_9 != 0)`-Bloecke sind im Spiel toter Code (vermutlich Editor-/Server-Pfad).

---

## 6. UNSICHER — nicht belegbar aus diesem Decompile

1. **Die RGB-Werte der Markerfarben A–F und W.** Sie stehen als Immediates in den
   `setColor3b`-Aufrufen (`WorldInfo.cpp:6363, 6367, 6371, 6408, 6412, 6416, 7110`),
   die Ghidra ohne Argumente ausgibt. Ohne Disassembly oder Binary nicht ermittelbar.
   Die Werte `uVar24 = 0x5f33ea / 0x5f39f7 / 0x5f3b6d / 0x5f3fc4` in diesen Zeilen sind
   **Ruecksprungadressen**, keine Farben.
2. **Der `ECX`-Container der nicht-inline expandierten Push-Aufrufe.** Fuer den ROT-Zweig
   (`param_6+0x0C`) und den Petrol-/Blau-Zweig (`param_6+0x04`) ist er inline belegt.
   Fuer die uebrigen `std_vector_pushBack_0x188(local_308)`-Aufrufe (u.a. **alle**
   0x0D/0x0E/0x0F-Zweige) hat Ghidra `ECX` verworfen. Dass es derselbe Vektor ist, ist
   durch die identische Elementgroesse 0x188 und `RandomInteractionBehavior.cpp:219-222`
   sehr stark gestuetzt, aber **nicht direkt bewiesen**. Es koennte theoretisch auch ein
   globaler Vektor sein — dann waere die Crash-Ursache eine andere.
3. **Der `ECX`-Container von `std_vector_push_back_4byte(&spawn)`** in den beiden
   `cube::Spawn`-Zweigen (Z.6647, 6905). Nicht sichtbar.
4. **Die Argumente von `tilemap_writeGlyphColumn` an den Stellen `LAB_005f4a73` /
   `LAB_005f3377`.** Vollstaendig verschluckt. Dass es `(wx, wy, wz, &rgba, param_6)` ist,
   folgt aus der Signatur bei `_library/audit.cpp:5757` und dem Kontext, ist aber nicht
   direkt ablesbar.
5. **Der Wert von `DAT_0076b340`** (der "leer"-Sentinel, 3 Bytes). Sehr wahrscheinlich
   (0,0,0), aber nicht im Decompile enthalten.
6. **Die Bedeutung der Objekt-Typ-IDs** (0x03, 0x0C, 0x0D, 0x10, 0x13, 0x14, 0x20-0x22,
   0x26-0x31, 0x38-0x40, 0x47-0x4D). Sie sind nicht aufgeloest; welche davon Tueren,
   Fenster, Betten, Kisten sind, wurde hier nicht geklaert.
7. **Warum genau 3 von 4 Aufrufen mit `param_5=1/2` werfen.** Ohne die konkreten
   Modelldaten (welche Marker enthaelt das Modell an welcher Position) und ohne den
   `param_6`-Wert des Mods laesst sich das nicht abschliessend sagen. Die Rotation
   `param_3` aendert die getroffene Weltposition (Z.5541-5567) und damit sowohl den
   Chunk-Treffer als auch die Reihenfolge — beide Effekte sind plausible Ursachen.
8. **Ob `param_9` tatsaechlich ein "Recorder"/Undo-Objekt ist.** Belegt ist nur:
   `+0x18` = `std::list`-Kopf, `+0x1C` = Groesse, Knotengroesse 0x1C
   (Payload: 3 Ints Position + 4 Bytes Farbe + 1 Int). Die Semantik ist Interpretation.
9. **`Chunk_generateObjects` Aufrufstellen mit unvollstaendigen Argumenten**
   (`game_misc.cpp:26057, 26073, 26192, ... 39184-39205`) — dort hat Ghidra die
   Argumentliste gar nicht rekonstruiert; diese Aufrufe wurden nicht ausgewertet.


## Nachtrag 2026-10-07: Bauwerke über Zonengrenzen (belegt im Spiel)

Die Prüfschleife der Städte (City.cpp, `CheckZone`) liest nach dem Setzen jeden Oberflächenvoxel über `world_getColumnData` 0x42F7E0 zurück. Ergebnis (20 Fälle im Protokoll): Liegt die **untere Ecke** (origin) eines Bauwerks in der **Nachbarzone**, fehlen in der geprüften Zone **100 %** seiner Blöcke – auch nach erneutem Aufruf. Bauwerke mit Ecke in der Zone werden dort vollständig gesetzt. Folgerung: Pro Zone nur Bauwerke mit Ecke in dieser Zone setzen; ragt eines hinüber, je Zone den Ausschnitt mit eigener Ecke in der Zone setzen (City.cpp `StampPiece`). Welche Bedingung im Code das bewirkt, ist im dekompilierten Text nicht sichtbar (die Argumente von `world_getChunkCell256` fehlen dort).
