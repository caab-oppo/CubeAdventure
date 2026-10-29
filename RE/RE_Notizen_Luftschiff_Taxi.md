# RE-Notizen: Luftschiff-Taxi zwischen Bergdoerfern

## Ablauf
1. **Dorfbau** → `MountainVillage::SpawnVillagers` ruft `AirshipTaxi::OnVillageBuilt(gc, cx, cy, deckZ)`.
   Spawnt einen **Vendor-NPC auf dem Turmdach** (`topZ = deckZ + 40`, NPC bei `cx+5, cy, topZ+1`)
   mit einem **4-Item-Katalog** (Nord/Ost/Sued/West).
2. **Ansprechen** ([R]) → das Spiel kopiert den Vendor-Katalog (`Creature+0x11dc`) in den aktiven
   Shop (`GC+0x800c0c`). Wir erkennen dort unseren **Richtungs-Marker** → `AirshipSpawner::DockAt`
   dockt ein Luftschiff ueber dem Turm an (schwebt, Spieler kann aufspringen).
3. **Kauf** einer Ware → die Menge dieser Richtung im aktiven Shop sinkt → `AirshipSpawner::Depart`
   mit Zieldorf aus `MountainVillage::NextVillageInDirection`. Schiff fliegt **~10 s** spaeter los,
   Terrain-Following bis zum Zieldorf, dann Despawn.

## Vendor-Katalog & Kauf-Erkennung
- Slot-Layout (aus RE_Notizen_Haendler): `{ int32 amount; Item item }` = **0x11C** Byte.
  `amount` @ slot+0, `Item` @ slot+4. Vendor-Vektor: `Creature+0x11dc/0x11e0/0x11e4` (start/end/cap),
  aktiver Shop: `GC+0x800c0c/0x800c10/0x800c14`.
- **Richtungs-Marker**: `Item.field_8 = 0x5A110000 | dir` (dir 0=N 1=O 2=S 3=W). `field_8` scheint
  fuer Name/Preis irrelevant → sicher als Erkennungsfeld nutzbar.
- Items sind Kopien eines getragenen Spieler-Items (gueltig/bepreist), je Richtung eigene Raritaet
  (2..5) zur optischen Unterscheidung. **Native Item-NAMEN** lassen sich nicht frei setzen (dieselbe
  Display-Cache-Wand wie beim Haendler) → Richtung wird ueber Reihenfolge + Sprechblase vermittelt.
- **Poll** jeden Frame: `GC+0x800c0c` durchlaufen, Marker-Slots lesen. Sinkt `amount` einer Richtung
  (oder verschwindet der Slot) gegenueber dem Vorframe → Kauf dieser Richtung.
  OFFEN/zu testen: ob das Spiel `GC+0x800c0c` beim Kauf wirklich dekrementiert (statt aus
  `Creature+0x11dc` neu aufzubauen). Falls nicht → alternativ `Creature+0x11dc` pollen oder Gold-Delta.

## AirshipSpawner-Erweiterung (Wiederverwendung des Flugcodes)
- `DockAt(gc, dockX, dockY, dockZ)` — Schiff erscheint am Turm, schwebt (Phase BOARDING, kein Ziel).
- `Depart(gc, tgtX, tgtY, delayMs)` — Ziel + Countdown; nach delayMs Phase CRUISING zum Zieldorf.
- `LaunchTo(...)` = DockAt + Depart (Convenience/Test). `IsBusy()` / `IsDocked()` als Status.
- Taxi-Flug nutzt **keine feindliche Crew** (`g_noCrew`). Bei Ankunft (<30 Bloecke) → Despawn.
- Alle Koordinaten in **Positionseinheiten** (1 Block = 65536).

## Dorf-Grid / Richtung
- `MountainVillage::NextVillageInDirection(gc, fromBlockX, fromBlockY, dir, outX, outY)`:
  jede Rasterzelle (`kCellZones=44`) hat GENAU ein Dorf → Nachbardorf = Dorf der Nachbarzelle.
  dir: 0=Nord(+Y) 1=Ost(+X) 2=Sued(-Y) 3=West(-X). (Achsen ggf. im Test justieren.)

## Sprechblase — Stand & Befund
- Gewuenscht: **echte native Blase** ueber dem NPC (`cube::SpeechWidget @ 0x4e5c90`,
  RTTI `0x766518`; `cube::Speech @ RTTI 0x7664ec`).
- Befund aus der (unvollstaendigen) Dekompilation: die SpeechWidget ist eine **einzige,
  vom GameController geteilte** Instanz mit Schreibmaschinen-Animation, Zeilenumbruch, Font-Cache
  und eigenem 3D-Transform (Text-`u16string` @ widget+0x178, aktuelle Zeile @ +0x170, Fortschritt
  @ +0x1d0, GC-Zeiger @ +0x1d8). **Instanzerzeugung und Text-/Show-Setter fehlen** in den
  rekonstruierten Quellen → offline nicht sauber lokalisierbar, Laufzeit-RE noetig.
- **Aktueller Stand**: `AirshipTaxi::GetBubble` liefert Text + NPC-Position; main.cpp zeichnet den
  Einladungstext als **eigenes Overlay** (zuverlaessig, absturzsicher), wenn der Spieler nah am
  Turm-NPC ist. Der native Widget-Hijack bzw. eine echte Weltpos→Screen-Projektion ist als
  **naechste Iteration** offen (braucht Laufzeit-Infos: Live-Instanzadresse / Test des Setters).
