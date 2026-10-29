# RE-Notizen: Native Deko-/Prop-Typen

Aus dem World-Konstruktor `cube::World::ctor_1 @ 0x0058eb00` (baut eine Name→ID-Map via
`World_map_find_or_insert_int`). Diese IDs sind der **Typ eines static_object/Prop-Objekts**.

| ID | Name | | ID | Name |
|---|---|---|---|---|
| 0x00 | Statue        | | 0x1e | Desktop |
| 0x01 | Door          | | 0x1f | Counter |
| 0x02 | BigDoor       | | 0x20 | Shelf1 |
| 0x03 | Window        | | 0x21 | Shelf2 |
| 0x04 | CastleWindow  | | 0x22 | Shelf3 |
| 0x05 | Gate          | | 0x2c | Corpse |
| 0x06 | FireTrap      | | 0x2d | RuneStone |
| 0x07 | SpikeTrap     | | 0x2e | Artifact |
| 0x08 | StompTrap     | | 0x2f | FlowerBox1 |
| 0x09 | Lever         | | 0x30 | FlowerBox2 |
| 0x0a | Chest         | | 0x31 | FlowerBox3 |
| 0x0c/0x0d/0x0e | Table (3 Varianten) | | 0x32 | StreetLight |
| 0x0f/0x10/0x11 | Stool (3 Varianten) | | 0x33 | FireStreetLight |
| 0x12 | Bench         | | 0x34..0x37 | Fence1..4 |
| 0x14 | BedTable      | | 0x38..0x40 | Vase1..9 |
| 0x15..0x17 | MarketStand1..3 | | 0x42 | Tent |
| 0x18 | Barrel        | | 0x43 | BeachUmbrella |
| 0x19 | Crate         | | 0x44 | BeachTowel |
| 0x1a | OpenCrate     | | 0x45 | SleepingMat |
| 0x1b | Sack          | | 0x47 | Furnace |
| 0x1c | Shelter       | | 0x48 | Anvil |
| 0x1d | Cupboard      | | 0x49 | SpinningWheel |
|       |               | | 0x4a | Loom |
| (Painting)          | | 0x4b | SawBench |
|                     | | 0x4d | CustomizationBench |

**Wichtig (Rendering):** Diese Props sind **static_objects**, die während der Zonengenerierung
(`Chunk_generateObjects @ 0x5af170` → `World_buildStructureMesh @ 0x5d6040`) erzeugt und deren
Render-Mesh **generierungsintern** gebaut wird. `World_buildStructureMesh` ist eine ~1300-Zeilen-
Funktion am Generierungskontext — **keine saubere „Prop spawnen"-API**.

Frühere Beweislage: **injizierte static_objects rendern nicht** (die Render-Batches entstehen bei der
Generierung, nicht per Injektion aktualisierbar). Beim Serialisieren werden static_objects zudem nur
mit **1 Byte** gespeichert (Rest wird regeneriert) → auch der DB-Weg trägt keine eigenen Props.

Fazit: Native Prop-**Modelle** lassen sich mit dem bekannten Stand **nicht zuverlässig injizieren, dass
sie rendern** — dieselbe Wand wie bei den nativen Gebäuden.
