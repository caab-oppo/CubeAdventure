# RE-Notizen: Welt-Persistenz / Block-Speicherung (für den Dorf-Marker)

Ziel: Einen persistenten „Marker"-Block setzen, der in der **Weltdatenbank** überlebt und
beim Zonenladen das Dorf-Spawnen triggert. ImageBase = `0x00400000` (Cube.exe 0.1.1),
Rebase = `imageBase + (addr - 0x400000)`. Adressen sind Ghidra-statisch und gelten für dein Binary.

## Kernbefund: Weltdatenbank = ein Key-Value-Store

```
CREATE TABLE blobs(key TEXT PRIMARY KEY, value BLOB)
INSERT INTO blobs(key, value) VALUES(?, ?)   -- bzw. UPDATE blobs SET value=? WHERE key=?
SELECT value FROM blobs WHERE key = ?
DELETE FROM blobs WHERE key = ?
```

Pro **Region** wird EIN Blob gespeichert (Key = String aus den Regionskoordinaten). Der Blob
enthält den serialisierten Zustand der Region: Entities (0x118-Structs), Block-Änderungs-Records
(0x14) und weitere Records (0x148).

## Bestätigte Funktionen

| Adresse | Name/Rolle | Status |
|---|---|---|
| `0x4499f0` | `db_storeBlob(key, data, len)` — Upsert: `SELECT 1` → `UPDATE` sonst `INSERT`; gibt bei Fehler „DATABASE WRITE ERROR" aus | **hoch** |
| `0x4499c0` | `db_storeBlobVec(key, vector)` — berechnet [begin,end) aus Vektor, ruft `db_storeBlob` | med |
| `0x44cdb0` | `deserializeRegionChunk(streamBuf, world)` — LOAD: liest count → 0x118-Structs; dann count → 0x14-Records und wendet sie via `tilemap_writeGlyphColumn` an; dann 0x148-Records | med |
| `~0x44e7a0` | `tilemap_writeGlyphColumn` == **unser `SetBlock` (`0x4E7A0`)** — setzt eine Block-/Tile-Spalte | hoch |
| `0x471920` | `GameController_modifyBlockAtWorld(worldX, worldY, height)` — DIG: löst Region-Cell (`world_getRegionCell64`), findet Spalte, `sparseArray_setAt(height-baseHeight, &DAT_0076b054/58)` | low |
| `0x44e700` | `sparseArray_setAt(index, voxel)` — schreibt in die globalen Modifikations-Sparse-Arrays `DAT_0076b054/58` | med |
| `0x42f860` | `world_getColumnAtFixedPos(...)` — liefert eine Spalte (0x20 Byte) | — |
| `0x44d4b0` | `vector_insertSelfaware_0x14` — fügt einen 0x14-Record in den Region-Vektor ein (nur in `deserialize` gesehen) | hoch |
| `db_getBlobInto` | LOAD-Seite: baut Key aus Region-X/Y (ostringstream, Konstanten `DAT_0071c760`/`0071c764`), `SELECT value FROM blobs`, dann `deserializeRegionChunk` (WorldInfo.cpp ~5262) | — |

## Struktur-Offsets (aus Disassembly)

- **Chunk/Region**: Spalten-Array-Zeiger bei `chunk + 0xa8`; Spaltenindex = `(worldY&0xff)*0x100 + (worldX&0xff)) * 0x20`.
- **Spalte** (0x20 Byte): `+0x10` = baseHeight, `+0x1c` = Höhen-Limit, `+3` = Byte mit 5-Bit-Feld (`& 0x1f`, in `deserialize` geprüft).
- **0x14-Record** (20 Byte, in `deserialize` gelesen): `[0x00]` 8 Byte Position (x,y als 2×int → an `tilemap_writeGlyphColumn` als arg1/arg2), `[0x08]` 8 Byte Wert (arg3 + Zeiger auf high), `[0x10]` 4 Byte ID (validiert gegen `world+0x800160 - 3`).
- **Region-dirty-Flag**: in `deserialize` wird bei Nichtübereinstimmung des 0x148-Vektors `*(byte*)(world+0x75) = 1` gesetzt → `+0x75` ist ein „geändert"-Byte der Region.

## Persistenz-Modell (Arbeitshypothese)

1. **Graben** → `modifyBlockAtWorld` ändert die Spalte im Speicher + registriert die Änderung in `DAT_0076b054/58` (sparseArray).
2. **Speichern** (Trigger noch offen) → Serializer läuft über geänderte Spalten/Records und schreibt sie als 0x14-Records in den Region-Blob (`db_storeBlob`).
3. **Laden** → `deserializeRegionChunk` liest die 0x14-Records und wendet sie via `tilemap_writeGlyphColumn` (= unser SetBlock) wieder an.

**Warum unsere Dörfer nicht persistieren:** Unser `SetBlock` == `tilemap_writeGlyphColumn` ändert die
Spalte, aber wir triggern **weder** die `sparseArray`-Registrierung **noch** setzen wir das
Spalten-/Region-„modified"-Bit. Der Serializer sieht die Änderung also nicht → sie wird nicht gespeichert.

## Plan A (elegant, evtl. persistiert sogar das ganze Dorf)

Nach unserem `SetBlock` das **„modified"-Signal** setzen, das der Serializer liest — dann speichert
das Spiel unsere Spalten beim normalen Region-Save selbst. Kein separater Marker nötig.
→ Braucht: **exaktes Spalten-/Region-modified-Flag** + Bestätigung, dass der Serializer danach greift.

## Plan B (Marker)

Genau EINEN Block als 0x14-Record persistent machen (via Dig-Pfad `modifyBlockAtWorld` an
SeedScout-Position, oder Record direkt in den Region-Vektor + `world+0x75=1`), beim Zonenladen
per `GetBlock` erkennen → Dorf bauen.

## Noch offen (das brauchen wir zum Umsetzen)

1. **Region-Serializer** (Gegenstück zu `deserializeRegionChunk`) + **Save-Trigger** (wann wird eine
   Region nach `db_storeBlob` geschrieben — beim Entladen? periodisch? Weltspeichern?).
2. **Exaktes „modified"-Flag**, das der Serializer prüft (Spalte `+3 & 0x1f`? Region `+0x75`? oder die
   `DAT_0076b054/58`-Sparse-Arrays?) — und ob `modifyBlockAtWorld` es setzt, unser `SetBlock` aber nicht.
3. **Exaktes Key-Format** des Region-Blobs (`DAT_0071c760/0071c764` + Koordinaten-Kodierung).
4. **0x14-Record-Feldbelegung** endgültig (Position vs. Wert vs. ID) via einer Beispiel-Deserialisierung.

## Risiko

Schreiben in den Save-/Serialize-Pfad kann Speicherstände beschädigen. **Nur mit Backups testen.**
