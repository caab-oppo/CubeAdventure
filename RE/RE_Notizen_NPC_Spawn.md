# RE-Notizen: NPC-/Kreatur-Spawn (aus dem OceanMod übernommen)

Wie man Kreaturen zuverlässig spawnt — abgeleitet aus `OceanContent.cpp` (Fisch-Spawn, live erprobt)
und angewandt auf die Bergdorf-Bewohner (Zwerge) + den Händler.

## Kernfunktionen (`cube::util`, aus `cwsdk/cube_util.h`)

| Funktion | Zweck | Verifiziert |
|---|---|---|
| `SpawnMonster(pos, race, hostility, level)` | **wilde** Kreatur (owner_guid=0), **kein** Begleiter | ja (OceanMod live) |
| `SpawnCreature(pos, race, hostility, level, owner_guid)` | ruft zusätzlich **Companion-Init** (0x1EE7A0) → Kreatur wird als **friedlicher Begleiter** behandelt | ja (Händler) |
| `AttachRandomWalk(c, homeX, homeY)` | native Wander-KI; **ersetzt** die (nutzlose) CompanionBehavior → Kreatur läuft um `home` statt dem Spieler zu folgen | ja |
| `MakeHostile(c)` | setzt `+0x60 = Hostility::Monster` (**1**, nicht 0 — 0 ist der Spieler). **Kein** Aggro-Bit | ja |
| `SetAggroRadius(c, blocks)` | `+0x1D2C`; nativ 200. Ohne das reagiert der Mob erst auf 20 Blöcke | neu |
| `SetGraphicsInitEnabled(bool)` | (Un-)verifizierter Grafik-Init 0x3F7C0; Default an | teilw. |

Alle Spawn-Aufrufe sind **intern SEH-isoliert** (siehe `cube_util.cpp`) → ein Absturz darin wird
abgefangen und geloggt, statt den Prozess zu killen. Deshalb **kein eigenes `__try` nötig** — und
das ist wichtig, weil `Vector3<int64_t>` einen Destruktor hat und `__try` sonst **C2712** wirft.

## Faktion / Verhalten (Creature-Offsets)

> **Berichtigt.** Die frühere Fassung dieses Abschnitts führte `+0x60` und
> `entity_data.hostility_flags` als **zwei verschiedene Felder** auf. Es ist
> **dasselbe Byte** (Nachrechnung in `RE_MobSpawn.md` §7.1). Und `+0x60 = 0` ist
> **nicht** „feindlicher Mob“, sondern der **Spieler**. Die richtigen Werte
> stammen aus `Chunk_generateObjects @0x5AF170`, dem einzigen Mob-Spawner des
> Spiels — siehe **`RE_MobSpawn.md`** für die vollständige Herleitung.

`entity_data.hostility_flags` == `Creature+0x60`, benannt in
`cube::util::Hostility`:

| Wert | Konstante | Bedeutung |
|---|---|---|
| `0` | `Hostility::Player` | nur der Spieler selbst |
| `1` | `Hostility::Monster` | wildes Monster, feindlich gegenüber dem Spieler |
| `3` | `Hostility::Talkable` | NPC, mit `[R]` ansprechbar |
| `5` | `Hostility::Companion` | Begleiter, folgt einem Besitzer |
| `6` | `Hostility::Passive` | passives Objekt (nativ: Vogelscheuche) |

`2` kommt im Spiel nirgends vor — nicht verwenden.

- `+0x1D2C` = **Aggro-/Leinenradius** in Blöcken; nativ `200.0`. Die
  Verfolgungsschleife rechnet mit `(radius + 20.0)`. Setzen mit
  `cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius)`.
- `entity_data.char_class` == `Creature+0x140`, benannt in `cube::util::CharClass`:
  `0x80` = Vendor (Buy/Sell-UI), `0x84` = Innkeeper (belegt:
  `GameController.cpp:34127`), `0x81`–`0x83`/`0x88` = weitere Dorf-Berufe.
- `entity_data.appearance.movement_flags &= ~0x0080` → Shop-Gate-Bit löschen.
- Vendor-Katalog-Sentinels: `+0x1b0 = 0x8024`, `+0x1b4 = 0x8023`, `+0x1b8 = warenzahl`.
- `+0x124 & 0x20` = Aggro-Bit. **Nicht setzen** — machte die Kreatur zum
  Pseudo-Spieler mit Infighting und Absturz.

## Rezepte

**Wildes, FEINDLICHES Tier (z.B. Fische, Piraten):**
```cpp
cube::Creature* c = cube::util::SpawnMonster(pos, race, cube::util::Hostility::Monster, level);
if (c) {
    cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);  // nativ 200
    cube::util::AttachRandomWalk(c, pos.X, pos.Y);
}
```
`MakeHostile(c)` ist dann nicht mehr nötig — es schreibt genau denselben Wert.
Nötig bleibt es nur, wenn eine bereits gespawnte Kreatur nachträglich
umgeschaltet werden soll.

**Wildes, FRIEDLICHES Tier / Dorfbewohner (z.B. Zwerge):**
```cpp
// Hostility::Talkable (3) -- NICHT 0: das ist der SPIELER-Wert.
cube::Creature* c = cube::util::SpawnMonster(pos, race, cube::util::Hostility::Talkable, /*level=*/1);
if (c) {
    cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);
    cube::util::AttachRandomWalk(c, pos.X, pos.Y);   // KEIN MakeHostile
}
```
Für ein wirklich **passives** Objekt (Vogelscheuche, Deko-Tier)
`Hostility::Passive` (6) statt `Talkable`.
> Wichtig: **`SpawnCreature` NICHT für Bewohner** — es macht sie zu Begleitern (folgen dem Spieler /
> Fehlverhalten). Das war der Bug. `SpawnMonster` + `AttachRandomWalk` = wilder, friedlicher Wanderer.

**Vendor / unser Händler:** `SpawnCreature` (Begleiter-Pfad ist hier OK, weil der Vendor-Setup das
Verhalten überschreibt) + `hostility_flags=3` + `char_class=0x80` + Sentinels + `BuildCustomVendorCatalog`.
Gekapselt in `AdventureMod::SpawnMerchantAt(gc, pos)` (WanderingMerchant.cpp) — spawnt unseren
**echten** Händler mit vollem Katalog an einer festen Position.

## Anwendung im Bergdorf (`MountainVillage.cpp`, `SpawnVillagers`)

- Beim Bau-Abschluss eines Dorfs: **6 Zwerge** (DwarfMale=9 / DwarfFemale=10) via `SpawnMonster` +
  `AttachRandomWalk` rund um den Turm, friedlich.
- **1 Händler** in der Dorfmitte via `AdventureMod::SpawnMerchantAt(gc, mpos)`.
- **Dedup** über `g_npcSpawned` (Doerfer, in denen NPCs schon gespawnt wurden) → einmal je Dorf/Session.
- NPCs sind **Entities → nicht persistent** (anders als die Block-Marker). Sie despawnen beim
  Zonen-Entladen und werden bei erneutem Dorf-Bau (nächste Session / Annäherung) neu gespawnt.
