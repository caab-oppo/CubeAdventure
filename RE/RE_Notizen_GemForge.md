# RE-Notizen: GemForge / Customization Bench

**Kurz:** GemForge konnte nicht funktionieren, weil es an fünf Stellen gegen das
tatsächliche Verhalten der Cube.exe arbeitete — die Bench wurde ohne Item
geöffnet, der Render-Hook war nie aktiv, die Material-IDs existieren nicht, die
Aufrufkonvention war geraten, und die 32-Cube-Grenze wurde an der falschen
Stelle gesucht. Alle fünf Punkte sind jetzt am dekompilierten Code belegt und
behoben.

Quelle für alle Adressen und Decompiles: der lokale Reversal-Baum
`CubeWorld-Reversal-master-new` (Ghidra 12.1.2, Alpha-Build mit Paraglider-Fix).
Alle Adressen relativ zu ImageBase `0x400000`.

---

## 1. Wie die Customization Bench wirklich funktioniert

Das Wiki beschreibt das Verhalten, der Reversal-Baum die Mechanik dahinter.
Beides deckt sich vollständig.

### 1.1 Das Fenster

`cube::VoxelWidget` (Ctor `0x587F70`, Dtor `0x5881B0`), Zeiger liegt in
`GameController + 0x8008F4`. Das zugehörige Interaktions-Panel hängt an
`GameController + 0x8008DC`.

Aus dem Ctor rekonstruiertes Layout (die Feld-Indizes `self[n]` im Decompile
sind DWORD-Indizes, also `self + n*4`):

| Offset | Bedeutung | Beleg |
|---|---|---|
| `+0x160` | **`Item*`** — das zu bearbeitende Item | siehe 1.2 |
| `+0x164/168/16C` | Rotationsgrenzen (`0`, `-90.0f`, `0`, `+90.0f`) | Ctor `self[0x59]=0xc2b40000`, `self[0x5b]=0x42b40000` |
| `+0x170/174/178` | `std::vector` (begin/end/cap), Elementgröße `0x11C` | Dtor: `operator_delete(*(void**)(this+0x170))` |
| `+0x17C` | Index des gewählten Cubes im Vorrat, `-1` = keiner | Ctor `self[0x5f]=0xffffffff` |
| `+0x180` | eingebettetes `Item` (Arbeitskopie), `count` bei `+0x294` | Ctor: `memset(self+0x65, 0, 0x100)`, `*(u16*)(self+100)=1` (level=1) |
| `+0x298` | `GameController*` (owner) | Ctor `self[0xa6] = owner` |
| `+0x29C/2A0/2A4` | getroffenes Voxel (x, y, z) aus dem Maus-Raycast | `drawWeaponCustomization`, Zeile ~80528 |
| `+0x2A8` | `bool` — Raycast hat getroffen | ebd. Zeile ~80408/80424 |
| `+0x2AC` | Index eines bereits gesetzten Cubes unter dem Cursor, `-1` = keiner | Ctor `self[0xab]=0xffffffff` |
| `+0x2B0` | zweites eingebettetes `Item` (Vorschau), `count` bei `+0x3C4` | Ctor, und `0x47..`: `if (-1 < *(int*)(w+0x2ac)) return w + 0x2b0;` (Tooltip-Item) |
| `+0x3C8` | Vorschau-Cube ist gesetzt | `tryPlaceCube` |
| `+0x3CC/3D0/3D4` | Position des Vorschau-Cubes | ebd. |
| `+0x3D8/3DC` | Material und Level des Vorschau-Cubes | ebd. |

Ein Vorrats-Element ist `{ int32 anzahl; Item item; }` = `4 + 0x118 = 0x11C`
Byte. Beleg: `tryPlaceCube` liest `elem + 0x11` als Material (= `4 + 0x0D`) und
`*(short*)(elem + 0x14)` als Level (= `4 + 0x10`). Dasselbe Layout hat auch das
Spieler-Inventar (`Creature + 0x11DC`, Vector von Buckets, Elemente im Abstand
`0x47` DWORDs = `0x11C`).

### 1.2 `+0x160` ist der Item-Zeiger, kein Modus

`cube::GameController::drawWeaponCustomization @0x588500` (läuft jeden Frame,
solange das Fenster offen ist):

```c
*(u32*)(this + 0x174) = *(u32*)(this + 0x170);   // Cube-Vorrat leeren
if (*(int*)(this + 0x160) == 0) goto LAB_0058bbf5;  // <-- bei 0: Rest wird übersprungen
local_36c = *(u8*)(*(int*)(this + 0x160) + 0xd);   // Material der Waffe
local_378._0_2_ = 0xa0b;                            // Suchmuster: category 0x0B = Cube
... Inventar (GC+0x8006D0 -> +0x11DC) nach passenden Cubes durchsuchen ...
... *(int*)(*(int*)(this + 0x160) + 0x114)          // adaption_count
```

Das ist genau die Wiki-Regel „The customization bench will automatically display
only the valid type of cubes depending on the selected weapon" bzw. „The Material
Cube used for upgrading must match the weapon's base material".

Gebunden wird `+0x160` normalerweise durch einen Klick auf eine Waffe im
Inventar (Klick-Handler in `GameController.cpp`, ~`0x47D7xx`):

```c
if (*pItem != '\x03') goto weiter;                 // muss category_id 3 (Waffe) sein
*(char**)(pThis[0x20023d] + 0x160) = pItem;        // pThis[0x20023d]*4 == 0x8008F4
GameController_ClearSelection();
```

### 1.3 Der Klick auf die Waffe

Ebenfalls im Klick-Handler, ~Zeile 29338:

```c
if (fenster_0x8008DC_ist_offen) {
    w    = *(int*)(GC + 0x8008F4);
    item = *(int*)(w + 0x160);
    if (item != 0) {
        if (*(int*)(w + 0x2AC) < 0)
            VoxelWidget_tryPlaceCube(*(u8*)(item + 0xD), (int)*(short*)(item + 0x10));  // 0x588250
        else
            VoxelWidget_selectHoveredCubeType();                                        // 0x58CE40
    }
}
```

Übergeben werden also **Material und Level der Waffe** — die beiden Größen aus
den Wiki-Regeln („Spirit Cubes used for upgrading must be at the same level as
the weapon, or within 10 levels less").

### 1.4 `VoxelWidget::tryPlaceCube @0x588250`

```c
if (!widget->hitValid || !(item = widget->item)) { ...aufräumen...; return; }
idx = widget->selectedCube;                       // +0x17C
n   = (widget->vecEnd - widget->vecBegin) / 0x11C;
if (idx < 0 || n <= idx) {
    ... Hover-/Vorschau-Zweig ...
    return;
}
else if (item->adaption_count < 0x20) {           // <<<<<< DAS 32er-GATE
    Item_addAdaptionCube(item, hitX, hitY, hitZ,
                         elem[idx].item.material, elem[idx].item.level);
    ... passenden Cube im Inventar suchen und counter_decrementClamp() ...
}
```

### 1.5 `Item::addAdaptionCube @0x4C69B0`

Im Reversal-Baum als `GameState_addOrUpdateHitEntry` fehlbenannt (Confidence
„med"), inhaltlich eindeutig:

```c
// this = Item, 5 Stack-Argumente: x, y, z, material, level
for (i = 0; i < this->adaption_count; i++)
    if (entry[i].xyz == (x,y,z)) { entry[i].material = material; entry[i].level = level; return; }
cap = (this->category_id == 3 && this->item_id in {5,6,7,8,0xA,0xB,0xF,0x10,0x11,0x12}) ? 0x20 : 0x10;
if (this->adaption_count < cap) { entry[count] = {x,y,z,material,level}; this->adaption_count++; }
```

Dieselbe Cap-Logik steht nochmal separat als `Item_get_stack_or_grid_size @0x4C7660`.
**Wichtig:** `tryPlaceCube` gated gegen `0x20`, `addAdaptionCube` gegen `0x20`
*oder* `0x10`. Bei Waffen mit 16er-Cap verbraucht das Spiel zwischen Cube 17 und
32 also den Inventar-Cube, ohne ihn zu setzen — ein Vanilla-Bug, den der neue
Hook nebenbei mit behebt (er greift ab `NativeCap`, nicht erst ab 32).

### 1.6 Die harte Grenze

`Item` ist `0x118` Byte:

```
+0x00 category_id  +0x01 item_id   +0x04 modifier/Seed  +0x08 field_8
+0x0C rarity       +0x0D material  +0x0E adapted        +0x10 level (u16)
+0x14 ItemAdaption[32]   (32 * 8 = 0x100)
+0x114 adaption_count
```

`adaption_count` liegt **direkt hinter Eintrag 31**. Ein 33. Eintrag würde das
Zählfeld selbst überschreiben, ein 34. das nächste `Item` in `WornEquipment`.
Mehr als 32 Cubes kann ein Item strukturell nicht tragen — unabhängig von jedem
Gate. Deshalb der Parallel-Store im Mod.

### 1.7 Der Renderer

`0x471B60`, im Reversal-Baum als `renderEntityLabelBillboard` geraten
(Confidence **„low"**) — der Rumpf zeigt eindeutig etwas anderes:

```c
void renderAdaptionCubes(Item* item, Matrix4* m, u32 shaderA, u32 shaderB, float* tint, float glow)
{
  if (item->category_id == 0) return;
  render_setVec3Uniform58(item_hasSpecialAbility());     // this = item
  for (i = 0, p = (char*)item + 0x15; i < *(int*)(item + 0x114); i++, p += 8) {
      x = p[-1]; y = p[0]; z = p[1];                     // = entry.x, entry.y, entry.z
      matrix = *m;
      matrix.col3 += basisX*x + basisY*y + basisZ*z;     // Cube um (x,y,z) versetzen
      material_toColor(col, p[2], tint, glow);           // = entry.material
      render_setUniform30(col);
      render_uploadModelViewMatrices(matrix, shaderA, shaderB);
      GameController_draw_mesh();                        // Einheitswürfel
  }
  render_setVec3Uniform58(0);
}
```

`p = item + 0x15` ist `&entry[0].y`, Schrittweite 8 = `sizeof(ItemAdaption)`,
Abbruch bei `item + 0x114` = `adaption_count`. Aufrufer u.a. `Creature_drawModel`
(4x, GameController.cpp ~50132) und `drawModelAtProjectedPos` (game_misc ~9803).

**Die Funktion prüft nirgends gegen 32** — sie läuft exakt `count`-mal. Genau das
nutzt der Fix aus.

**Sie ist eine Methode des Renderer-Objekts — ECX muss beim Eintritt stimmen.**
Das ist im Decompile nicht sofort sichtbar, ergibt sich aber aus den Callees:

```c
void render_setVec3Uniform58(undefined4 value) {          // @0x448FE0
    int self;                                              // freies Register = ECX
    (**(code**)(**(int**)(self + 0x1c) + 0x1b4))(*(int**)(self + 0x1c), ...);
}
void render_setUniform30(undefined4 value) {              // @0x448280 -- gleiches Muster
    (**(code**)(**(int**)(self + 0x1c) + 0x178))(...);
}
```

`renderAdaptionCubes` setzt `ECX` für diese Aufrufe **nicht selbst**, sondern
erbt es von seinem eigenen Aufrufer. Ein Detour muss `ECX` (und `EDX`) also
unverändert durchreichen. Laufzeitbeleg: mit einem `__stdcall`-Detour stürzte
Cube.exe beim Betreten der Welt mit `0xC0000005` bei `0x448FFA` ab, Lesezugriff
auf `0x0000001C` — also exakt `*(int**)(self + 0x1c)` mit `self == 0`.

Vollständige Signatur:

```c
void __thiscall renderAdaptionCubes(Renderer* this /*ECX*/,
                                    Item* item, Matrix4* m,
                                    u32 shaderA, u32 shaderB, float* tint, float glow);
// -> 6 Stack-Argumente, Callee-Cleanup, "ret 0x18"
```

### 1.8 Die Materialfarben

`material_toColor @0x4C7250` ist ein `switch` über die Material-ID:

| ID | Farbe | Verwendung |
|---|---|---|
| 1 | 0.7/0.7/0.7 × tint | Stein? |
| 2 | 0.4/0.3/0.2 × tint | Holz |
| 5 | 0.1 grau | dunkles Metall |
| 7 | 0.9 grau | helles Metall |
| **0x0B** | 1.0/0.7/0.2 × tint | **Gold** |
| 0x0C | 0.8/0.8/0.85 × tint | Silber |
| **0x80** | Glow rot (`g`, `0.5g`, `0.1g`) | **Ruby** |
| **0x81** | Glow grün (`0.3g`, `g`, `0.5g`) | **Emerald** |
| **0x82** | Glow blau (`0.3g`, `0.5g`, `g`) | **Sapphire** |
| **0x83** | Glow weiß (`0.8g`, `0.8g`, `g`) | **Diamond** |
| default | 0.5 grau | alles andere |

Gegenprobe: `CombatBehavior::spawnHitParticles @0x59A800` läuft über dieselbe
Cube-Liste und erzeugt Funken nur für `material > 0x7F` — die vier Glow-IDs sind
also die „Spirit Cubes".

### 1.8b Zwei Sorten Cubes — und warum Edelstein-Zutaten keine sind

Live-Befund aus dem Inventar-Scan: der Spieler besitzt Gegenstände mit
Kategorie `0x0B`, **item_id `0x00`** und Material `0x0D`/`0x0E`/`0x0F`
(Rarity 1/2/3, 50 Stück) — Emerald, Sapphire, Ruby. Genau diese Werte benutzte
die erste GemForge-Fassung als Adaptions-Materialien. Sie tauchen in der Bench
aber nie auf. Grund steht in `drawWeaponCustomization @0x588500`, das den
Vorrat in **zwei** Durchläufen zusammensucht:

```c
// Durchlauf 1 -- Materialwuerfel
local_378._0_2_ = 0xA0B;                       // Kategorie 0x0B, item_id 0x0A
local_36c       = item->material;              // Material == Basismaterial der WAFFE

// Durchlauf 2 -- Spirit-Cubes, ueber Level UND Farbe
for (lvl = max(1, waffenLevel-10); lvl <= waffenLevel; lvl++)
    for (mat = 0x80; mat < 0x84; mat++)        // <- genau vier Farben
        if (slot.item.category == 0x0B && slot.item.item_id == 0x0E
            && slot.item.material == mat && item_rarityScaled(slot.item) == lvl)
                ... in den Vorrat aufnehmen (local_13c[0] = 0xE0B) ...
```

Und beim Vorschau-Cube schreibt dieselbe Funktion:

```c
*(u8*)(widget+0x180) = 0x0B;                                  // Kategorie
*(u8*)(widget+0x181) = (0x7F < preview_material) * 4 + 0x0A;  // id 0x0E oder 0x0A
```

Damit ist belegt:

| Kategorie | item_id | Material | Bedeutung |
|---|---|---|---|
| `0x0B` | `0x0A` | = Basismaterial der Waffe | **Materialwürfel** (Eisen, Holz, …) |
| `0x0B` | `0x0E` | `0x80`–`0x83` | **Spirit-Cube** — rot, grün, blau, weiß |
| `0x0B` | `0x00` | `0x0D`–`0x10` | **Zutat/Edelstein** — Craftingmaterial, an der Bench nicht setzbar |

Die Schwelle `0x7F` trennt beide Sorten und ist dieselbe, die
`spawnHitParticles` für die Funken benutzt.

**Konsequenz für die alte GemForge-Fassung:** `kMaterial[] = {0x0B, 0x0D, 0x0E,
0x0F, 0x10}` mischte ein Materialwürfel-Material (`0x0B` Gold) mit vier
*Zutaten*-IDs. Als Adaptions-Material fallen `0x0D`–`0x10` in den
`default`-Zweig von `material_toColor` und werden grau — die ursprüngliche
Beobachtung „alle Gems sehen gleich aus" war also richtig, die Ursache aber
tiefer: es waren nie Cube-Materialien, sondern Zutaten-Materialien.

### 1.9 Die Schadensformel

`item_weaponDamage @0x4C7F60` (thiscall, ECX = Item, Rückgabe **float**):

```c
powerBonus = item->adaption_count * 0.1f;
dmg = math_pow2Mul(item->level + powerBonus, item->rarity) * {2|4|8};   // je Waffentyp
```

mit `math_pow2Mul(x, r) @0x445F60 = 2^(3*(1 - 1/((x-1)*0.05 + 1))) * 2^(r*0.25)`.

Cubes wirken im Original also **ausschließlich** über `adaption_count`, und zwar
wie +0.1 Waffenlevel pro Cube. Die Kurve sättigt bei Faktor 8. Dieselbe
`adaption_count * 0.1`-Formel steckt auch in `Item_compute_value @0x4C78C0`
(Verkaufswert) und `item_computeBlockValue @0x4C6A90`. `stat_calcHasteBonus`,
`stat_calcSpirit` und `stat_calcSpellPower` lesen `adaption_count` **nicht** —
die Gem-Zuordnung „Emerald = Tempo, Diamond = Krit, ..." war reine Mod-Erfindung.

---

## 2. Warum die alte Fassung nicht funktionierte

### 2.1 `ToggleRender()` war an keine Taste gebunden

`GemForge.h` und das Overlay warben mit „+V=Render", `main.cpp` rief bei STRG+V
aber `ApplyCubesToWeapon()` auf. `ToggleRender()` kam in keiner einzigen anderen
Datei vor (per `grep` über das ganze Projekt). Der Render-Hook wurde damit nie
per `MH_CreateHook`/`MH_EnableHook` installiert — das „extra Cubes rendern" war
toter Code.

### 2.2 Die Material-IDs existieren nicht

`kMaterial[] = { 0x0b, 0x0d, 0x0e, 0x0f, 0x10 }`. Laut 1.8 kennt
`material_toColor` von diesen fünf nur `0x0B`. `0x0D`, `0x0E`, `0x0F` und `0x10`
fallen alle in den `default`-Zweig → einheitliches Grau. Emerald, Sapphire, Ruby
und Diamond wären selbst bei aktivem Render-Hook nicht unterscheidbar gewesen.

### 2.3 Die Bench wurde ohne Item geöffnet

`BlacksmithNpc::OpenCustomizationUI()` setzte `*(u32*)(widget + 0x160) = 0` mit
dem Kommentar „Modus 0 = Weapon Customization". `+0x160` ist der Item-Zeiger
(1.2); bei `0` überspringt `drawWeaponCustomization` alles Weitere und
`tryPlaceCube` steigt in der ersten Zeile aus. Das Fenster ging auf und blieb
leer — kein Waffenmodell, kein Cube-Vorrat, kein Platzieren.

### 2.4 Die Aufrufkonvention des Render-Hooks war geraten

Kommentar im alten Code: *„Konvention `__stdcall`: aus der Absturz-Analyse
hergeleitet"*. `0x471B60` nimmt 6 Stack-Argumente; liegt man zwischen `__cdecl`
und `__stdcall` daneben, verschiebt sich der Stack bei **jedem** Aufruf um 24
Byte. Das Decompile gibt die Konvention nicht her, und die Cube.exe lag hier
nicht zum Nachschauen vor.

### 2.5 Die 32er-Grenze wurde an der falschen Stelle gesucht

`ApplyCubesToWeapon()` schrieb bis zu 32 Einträge direkt ins `Item` und setzte
`adaption_count = idx`. Für Waffen mit 16er-Cap (1.5) erzeugt das einen Zustand,
den das Spiel selbst nie herstellt. Gleichzeitig war die *echte* Grenze (das
`< 0x20`-Gate in `tryPlaceCube` plus die feste Arraygröße) gar nicht adressiert —
über 32 Cubes wäre so nie möglich gewesen.

### 2.6 Nebenbefunde

- Die vier Stat-Hooks deklarierten `double` als Rückgabetyp; die Originale geben
  `float` zurück. Auf x86 liegen beide in ST(0), das ging gerade noch gut.
- Der Self-Gate `self == gc->local_player` in den drei Stat-Hooks war korrekt —
  nachgerechnet über die Equipment-Offsets, siehe 3.
- `[L]` öffnet das Labor-Menü und war gleichzeitig STRG+L für „Gold-Cube".

---

## 3. Offset-Gegenprobe cwsdk ↔ Binary

`cwsdk/cube/Creature.h` wurde gegen die Feldzugriffe in `stat_calcSpirit`,
`stat_calcHasteBonus`, `stat_calcSpellPower` und dem Klick-Handler geprüft
(`offsetof` aus einem Testbuild gegen die Decompile-Konstanten):

| Feld | cwsdk | Binary | Fundstelle |
|---|---|---|---|
| `entity_data` | `0x010` | `0x010` | — |
| `hostility_flags` | `0x060` | `0x060` | `*(char*)(self+0x60)` |
| `appearance.movement_flags` | `0x07E` | `0x07E` | `*(byte*)(self+0x7e) & 0x20` |
| `char_class` | `0x140` | `0x140` | `blockType = *(char*)(self+0x140)` |
| `level` | `0x190` | `0x190` | `*(int*)(self+400)` |
| `power_base` | `0x1A8` | `0x1A8` | `2^(*(byte*)(self+0x1a8) * 0.25)` |
| `equipment.necklace` | `0x418` | `0x418` | `== 8` |
| `equipment.chest` | `0x530` | `0x530` | `== 4` |
| `equipment.feet` | `0x648` | `0x648` | `== 6` |
| `equipment.hands` | `0x760` | `0x760` | `== 5` |
| `equipment.shoulder` | `0x878` | `0x878` | `== 7` |
| `equipment.left_weapon` | `0x990` | `0x990` | `== 3` |
| `equipment.right_weapon` | `0xAA8` | `0xAA8` | `== 3`, und `state == creature + 0xaa8` |
| `equipment.left_ring` | `0xBC0` | `0xBC0` | `== 9` |
| `equipment.right_ring` | `0xCD8` | `0xCD8` | `== 9` |

14 von 14 Treffern. Zusätzlich bestätigt: `GameController + 0x8006D0 = local_player`
(`drawWeaponCustomization` liest von dort das Inventar bei `+0x11DC`).

`sizeof(Item) == 0x118`, `offsetof(customization_data) == 0x14`,
`offsetof(adaption_count) == 0x114` werden jetzt per `static_assert` in
`GemForge.cpp` bei jedem Build geprüft.

---

## 4. Was der Fix macht

| Hook | Adresse | Zweck |
|---|---|---|
| `VoxelWidget::tryPlaceCube` | `0x588250` | Hängt `widget+0x160` während des Aufrufs auf ein Scratch-Item mit `count = 0` um → das `< 0x20`-Gate ist offen, das Spiel verbraucht den Inventar-Cube ganz normal. |
| `Item::addAdaptionCube` | `0x4C69B0` | Fängt den Eintrag ab und legt ihn im Parallel-Store ab, statt ihn ins volle Item zu schreiben. |
| `item_renderAdaptionCubes` | `0x471B60` | Zeichnet nach dem Original die Extra-Cubes in Batches zu je ≤32 aus einem eigenen Item-Puffer. |
| `item_weaponDamage` | `0x4C7F60` | Skaliert exakt mit `f(level + 0.1·(nativ+extra)) / f(level + 0.1·nativ)`, `f(x) = 2^(3·(1 − 1/((x−1)·0.05 + 1)))`. |

Die ersten ≤32 Cubes (bzw. ≤16) bleiben **nativ im Item** — sie werden also
gespeichert, übers Netz gesynct und vom Spiel selbst gezeichnet. Nur was darüber
hinausgeht, lebt im Mod.

Zuordnung Store ↔ Waffe über einen FNV-Hash der unveränderlichen Item-Felder
(`category_id`, `item_id`, `modifier`, `field_8`, `rarity`, `material`, `level`).
Das überlebt das byteweise Kopieren zwischen Inventar und Ausrüstung
(`Item_copyStruct @0x42C5E0`).

### Aufrufkonventionen werden geprüft, nicht geraten

**Wichtig:** Der Reversal-Baum hält für Spielfunktionen *keine* Aufrufkonvention
fest — `__cdecl`/`__stdcall`/`__thiscall` stehen nur bei den per Signatur
erkannten Bibliotheksfunktionen. Für `0x471B60`, `0x588250` usw. ist die
Konvention aus dem Quellbaum also grundsätzlich nicht ablesbar.

**Erster Versuch (verworfen, war die Ursache des Absturzes 0xC0000005):** im
Funktionsrumpf nach `C2 <imm> 00` (`RET imm16`) suchen. Das ist eine Heuristik —
die Bytefolge kann in 500 Byte Code zufällig als Teil einer anderen Instruktion
auftreten. Ein Fehlgriff bei `0x471B60` verschiebt den Stack um 24 Byte pro
Aufruf; die Funktion feuert ab Spielstart permanent aus `Creature_drawModel`.
Ergebnis: Lesezugriff auf eine Müll-Adresse.

**Jetziges Verfahren (`AnalyzeConvention`), zwei unabhängige Belege:**

1. **Aufrufstellen.** Die `.text`-Sektion wird aus den PE-Headern geholt und nach
   `E8 <rel32>` durchsucht, wo `rel32` genau auf die Zielfunktion zeigt. Direkt
   hinter jedem gefundenen `CALL` wird geprüft, ob dort
   `add esp, n` (`83 C4 n` / `81 C4 n32`) oder `lea esp,[esp+n]` (`8D 64 24 n`)
   steht. Räumt auch nur *eine* Aufrufstelle ab → die Funktion kann kein
   Callee-Cleanup haben.
2. **Funktionsende.** Ab der Adresse der Folgefunktion rückwärts über die
   MSVC-Ausrichtungsbytes (`CC`/`90`) laufen; die letzte echte Instruktion muss
   dann `C2 <n> 00` sein. Das ist etwas völlig anderes als „irgendwo im Rumpf".

Entschieden wird nur, wenn beide Belege zusammenpassen:

| Aufrufstellen räumen ab | Ende ist `ret n` | Ergebnis |
|---|---|---|
| ja | nein | Aufrufer räumt ab (`__cdecl`) |
| nein | ja | Callee räumt ab (`stdcall`-artig) |
| sonst / keine Aufrufstelle gefunden | | **unklar → Hook wird nicht gesetzt** |

Bei „unklar" läuft das Spiel ohne die betroffene Funktion weiter, statt mit einem
zerlegten Stack. Die Konsole schreibt die Zählung mit (`N Aufrufstelle(n), davon
M mit 'add esp,24'; Rumpf endet auf 'ret 24': ja/nein`).

`0x4C7F60` hat keine Stack-Argumente — dort sind `thiscall(void)` und
`__fastcall(ecx, edx)` identisch, kein Risiko.

**`0x4C69B0` wird gar nicht mehr gehookt.** Das Spiel schreibt den Cube ohnehin
in das Item, auf das `widget+0x160` zeigt — und das ist während des Aufrufs
unser Scratch-Item. Der zweite Hook war ein überflüssiger weiterer Ratefall;
jetzt wird nach dem Aufruf einfach aus dem Scratch ausgelesen. Landet dort
nichts, passiert nichts (sicherer Fehlschlag mit Konsolenhinweis).

## 4b. Extra-Cubes in der Bench-Vorschau

`drawWeaponCustomization` benutzt **nicht** `0x471B60`, sondern zeichnet die
Cubes selbst:

```c
for (i = 0; i < widget->item->adaption_count; i++) {       // beides neu gelesen je Runde
    c = widget->item + 0x14 + i*8;
    m = basis;                                             // 16 floats kopiert
    m[12+j] += c.x*m[0+j] + c.y*m[4+j] + c.z*m[8+j];       // Zeile 3 = Translation
    material_toColor(col, c.material, {1,1,1,1}, 0);
    render_setUniform30(col);
    render_uploadModelViewMatrices(m, projA, projB);       // 0x4482A0
    GameController_draw_mesh();
}
```

Ein größerer Item-Puffer scheidet aus (Zähler fest bei `+0x114`, siehe 1.6), und
die Schleife mehrfach laufen zu lassen hieße, die gesamte Bench-UI mehrfach zu
zeichnen — inklusive `stringstream`-Textausgabe.

**Stattdessen wird `render_uploadModelViewMatrices` mitgelesen.** Dieser eine
Aufruf liefert alles, was `0x471B60` braucht:

| Was | Woher |
|---|---|
| `ECX` = Renderer-`this` | dieselbe Klasse wie `render_setUniform30` (`self+0x1c`, `self+0x24`) |
| Modellmatrix | Argument 1 |
| beide Projektions-Parameter | Argumente 2 und 3 — exakt `param_3`/`param_4` von `0x471B60` |
| Tint / Glow | in der Schleife fest `{1,1,1,1}` und `0` |

Aus der Matrix eines *bekannten* Cubes lässt sich die Basismatrix exakt
zurückrechnen:

```
basis[12+j] = m[12+j] − (x·m[0+j] + y·m[4+j] + z·m[8+j])
```

Das ist dieselbe Formel, die `0x471B60` intern verwendet — die Extras landen
also pixelgenau dort, wo sie auch in der Welt sitzen.

**Erkennung der Cube-Schleife ohne weiteren Hook:** zwei aufeinanderfolgende
Aufrufe, deren Matrixzeilen 0..2 identisch sind *und* deren
Translationsdifferenz genau der Differenz von Cube 0 zu Cube 1 entspricht. Beide
Koordinatensätze stehen im Item, die Prüfung ist damit selbstprüfend über alle
vier Komponenten. Trifft sie zu, gehört die Matrix zu Cube 1, und die Basis ist
bestimmt.

Reentranz ist abgesichert: `0x471B60` ruft seinerseits `0x4482A0` auf, während
der Injektion sperrt `g_injecting` den Hook.

### Signatur belegt

Der Decompile aus der konkreten `Cube.exe` bestätigt das Modell:

```c
void __thiscall FUN_004482a0(void *this, float *param_1, float *param_2, float *param_3)
```

`this` in `ECX`, drei Stack-Argumente, `ret 0xC` — exakt das, was
`Hook_Upload` als `__fastcall(ecx, edx, mat, a, b)` abbildet. Die Funktion
**schreibt nirgends durch ihre Parameter**, sie liest nur je 16 Floats und
schiebt sie über `(*(this+0x1c))->vtbl[0x178]` in Shader-Konstantenregister
(`this+0x24`, `this+0x28`, `this+0x2c`).

### Der erste Anlauf und warum er abstürzte

```
0xC0000005: Zugriffsverletzung beim Schreiben an Position 0xC100000C
0xC000041D: Ausnahmefehler während eines Benutzerrückrufs
```

Nach obigem Decompile war die **Konvention nicht die Ursache** — sie stimmte.
Ursache war die **Reentranz**: `renderAdaptionCubes` wurde mitten aus
`render_uploadModelViewMatrices` heraus aufgerufen, also genau zwischen
„Uniform setzen" und „Mesh zeichnen". Danach lief die Spielsequenz mit den von
der Injektion überschriebenen Uniformen weiter.

Nebenbefund, der für das ganze Projekt gilt: die zweite Meldung `0xC000041D`
(STATUS_USER_CALLBACK_EXCEPTION) zeigt, dass ein von `__except` geschlucktes
`0xC0000005` **innerhalb eines Win32-Callbacks trotzdem tödlich** ist. Die
SEH-Netze im Mod schützen dort also nicht — sie machen den Fehler nur sichtbar.

### Was jetzt anders ist

1. **Dreifach-Verifikation statt Doppel.** Erkannt wird die Cube-Schleife erst,
   wenn *drei* aufeinanderfolgende Matrizen dieselbe Basis haben **und** beide
   Translationsschritte exakt den Differenzen Cube 0→1 und Cube 1→2 aus dem
   Item entsprechen. Eine zufällige Übereinstimmung müsste acht
   Float-Komponenten gleichzeitig treffen. Das ist wichtig, weil eine
   Fehlauslösung mitten im Renderer passiert.
2. **Matrix wird zurückgeschrieben.** Nach der Injektion wird
   `render_uploadModelViewMatrices` noch einmal mit der Matrix des laufenden
   Spiel-Cubes aufgerufen, damit das Spiel seinen nächsten Cube nicht an
   unserer letzten Position zeichnet.
3. **Injiziert wird bei Cube 2**, nicht bei Cube 1 — dadurch ist die Erkennung
   abgeschlossen, bevor gezeichnet wird.

Nicht wiederherstellbar ist das **Farb-Uniform** (`render_setUniform30`); dafür
wären `material_toColor` und `setUniform30` mit je eigener, unbelegter Signatur
nötig. Ein einzelner nativer Cube kann darum kurz die Farbe des letzten
Extra-Cubes tragen.

Notbremse bleibt `kEnablePreviewHook = false` ganz oben in `GemForge.cpp`.

### Registerzustand: der eigentliche Stolperstein

Die Konvention allein reicht nicht. Mehrere dieser Funktionen sind Methoden und
erwarten `ECX` (`this`) beim Eintritt — teils, ohne es selbst zu setzen. Ein
gewöhnlicher C-Detour (`__cdecl`/`__stdcall`) verwirft `ECX`/`EDX`, bevor das
Original läuft. Deshalb ist **jeder** Detour hier `__fastcall` mit zwei
führenden Parametern, sodass `ECX`/`EDX` normale Argumente werden und
unverändert an das Original zurückgehen:

| Funktion | Detour-Signatur | Cleanup |
|---|---|---|
| `0x471B60` | `__fastcall(ecx, edx, item, mat, p3, p4, p5, p6)` | `ret 0x18` |
| `0x588250` | `__fastcall(ecx, edx, material, level)` | `ret 8` |
| `0x4C7F60` | `__fastcall(ecx, edx)` | `ret` |

Gemessene Belege aus dem Livelauf:

```
Konventions-Analyse item_renderAdaptionCubes (0x471B60):
  6 Aufrufstelle(n), davon 0 mit 'add esp,24'; Rumpf endet auf 'ret 24': ja
Konventions-Analyse VoxelWidget::tryPlaceCube (0x588250):
  1 Aufrufstelle(n), davon 0 mit 'add esp,8';  Rumpf endet auf 'ret 8':  ja
```

Beide eindeutig Callee-Cleanup — die Analyse hat also funktioniert, der Absturz
kam vom verworfenen `ECX`, nicht von der Stack-Bilanz.

### Notbremsen

Ganz oben in `GemForge.cpp` stehen drei Schalter, um im Zweifel zu bisektieren:

```cpp
static const bool kEnableRenderHook = true;  // 0x471B60 -- läuft ab Spielstart ständig
static const bool kEnableBenchHook  = true;  // 0x588250 -- nur bei offener Bench
static const bool kEnableDamageHook = true;  // 0x4C7F60 -- risikofrei
```

---

## 5. Bedienung

1. In die Schmiede des Bergdorfs gehen (Haus k==0, die L-Schmiede mit der
   offenen Esse-Bucht) und **an die Werkbank stellen**, dann `[R]`. Das Fenster
   öffnet sich und die angelegte Waffe wird gebunden (Konsole: „Bench an Waffe
   Typ N gebunden"). Alternativ funktioniert `[R]` weiterhin beim Schmied-NPC.
2. Passende Cubes müssen **im Inventar** liegen — das ist die native Regel, der
   Vorrat wird aus dem Inventar gefüllt und beim Setzen verbraucht. Material muss
   zum Waffenmaterial passen, Cube-Level ≤ Waffenlevel (und ≥ Waffenlevel − 10).
3. Cube im Vorrat anklicken, dann auf die Stelle am Waffenmodell klicken.
4. `[STRG+B]` bindet die angelegte Waffe erneut (z.B. nach Waffenwechsel).
   `[STRG+X]` verwirft die Extra-Cubes der angelegten Waffe.

Overlay unten zeigt `nativ n/cap + extra m = gesamt/500` sowie die Zähler je
Gem-Sorte.

### Werkbank als Auslöser

Der `CustomizationBench`-Prop (Deko-Typ `0x4D`) wird in `PlanSmithy` bzw.
`PlanHouse(smithy=true)` in die Deko-Warteschlange gelegt. Die native
`[R]`-Abfrage (`GameController_updateSelectedEntity @0x4889E0`) läuft über den
**Entity-Pick-Baum** — injizierte `zone_static_object`-Props tauchen dort nicht
auf. Deshalb meldet `PlaceVillageDeco` jede *tatsächlich gesetzte* Bank per
`BlacksmithNpc::OnBenchPlaced(x, y, z)`, und `OnGameTick` prüft die Distanz
(4 Blöcke horizontal, −4…+6 vertikal, damit Nachbaretagen nicht auslösen).

Die Registrierung hängt am Bauvorgang: Bänke aus einem früheren Spielstand sind
nach einem Weltwechsel erst wieder bekannt, wenn das Dorf neu gebaut wird —
dieselbe Lebensdauer wie beim Schmied-NPC (`OnWorldReload` leert beide Listen).

### Limits: 100 je Material, 500 gesamt

Gezählt wird **nativ + Parallel-Store**, damit sich das Limit nicht durch
Ab-/Anlegen der Waffe umgehen lässt. Das Material des zu setzenden Cubes liest
`PrePlace` aus dem Vorrats-Element: `{ int32 anzahl; Item item; }` (`0x11C`
Byte), also `material` bei `+0x11` — dieselbe Stelle, die `tryPlaceCube` selbst
an `addAdaptionCube` weiterreicht (`*(u8*)(vecBegin + idx*0x11C + 0x11)`).

Speicherbedarf: `8 + 4 + 500·8 = 4012` Byte je Waffe, bei 64 verfolgten Waffen
rund 251 KiB statisch. Der Renderer zeichnet in Batches zu je ≤32 Cubes, bei
500 Cubes also 16 zusätzliche Zeichenaufrufe pro Waffendarstellung.

---

## 6. Ehrliche Einschränkungen

1. **Der Parallel-Store ist nicht persistent.** Extra-Cubes (>32) sind nach dem
   Beenden des Spiels weg; die nativen ≤32 bleiben. Speichern wäre möglich
   (Datei neben der DLL, Schlüssel = Item-Hash), ist aber noch nicht gebaut.
2. **Der Platzierungs-Raycast kennt die Extra-Cubes nicht.** Sie stehen nicht im
   Item, also trifft der Strahl ab Cube 33 nur noch das Grundmodell — man kann
   nicht auf einem Extra-Cube aufbauen. Gezeichnet werden sie inzwischen aber
   überall, auch in der Bench (siehe Abschnitt 4b).
3. **Extra-Cubes lassen sich nicht einzeln entfernen.** Der native Entfernen-Pfad
   arbeitet auf dem Item. Nur `[STRG+X]` (alle verwerfen).
6. **500 Cubes sind praktisch nur mit sehr viel Nachschub erreichbar.** Jeder
   gesetzte Cube wird nativ aus dem Inventar verbraucht (bewusst so gewählt).
   Ob 100 Cubes einer Sorte überhaupt auf ein Waffenmodell passen, hängt an der
   freien Oberfläche des Modells — ab Cube 33 trifft der Platzierungs-Raycast
   nur noch das Grundmodell (siehe Punkt 2), belegte Voxel werden übersprungen.
4. **Multiplayer:** Extra-Cubes existieren nur lokal. Andere Spieler sehen die
   nativen ≤32. Der Schadensbonus ist clientseitig — was der Server damit macht,
   ist ungetestet.
5. **Nicht ingame getestet.** Alle Änderungen sind gegen den Reversal-Baum
   belegt und der Code kompiliert syntaktisch sauber, aber ein echter Testlauf
   in Cube.exe steht aus. Beim ersten Start unbedingt auf diese vier
   Konsolenzeilen achten:
   - `[GemForge] Render-Konvention: ...`
   - `[GemForge] tryPlaceCube-Konvention: ...`
   - `[GemForge] Hook Item::addAdaptionCube ... erstellt.` (oder die ABBRUCH-Meldung)
   - `[GemForge] Bereit -- bis zu 100 Cubes ...`

## 7. Offene Fragen

- Welche `item_id`-Werte sind welche Waffe? Die 32er-Cap-Liste
  `{5,6,7,8,0xA,0xB,0xF,0x10,0x11,0x12}` ist bekannt, die Namen nicht.
- Welche `item_id`s hat category `0x0B` (Cubes)? `drawWeaponCustomization`
  startet die Inventarsuche bei `0x0A` (`local_378._0_2_ = 0xa0b`) und
  `tryPlaceCube` schreibt beim Hover `0xE0B` — also mindestens `0x0A` und `0x0E`.
- Der `adapted`-Flag im Item (`+0x0E`) wird von uns nicht gesetzt. Wo liest das
  Spiel ihn?
