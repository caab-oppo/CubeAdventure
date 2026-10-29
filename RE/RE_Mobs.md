# Mob-Labor: alle Rassen spawnen, eigene Mobs aus .cub-Teilen

**Das Mob-Labor spawnt jede der 156 Rassen** mit den Flags, die das Spiel selbst vergibt, und mit **nativer Ausrüstung**. Für die Ausrüstung läuft derselbe Ausrüster wie bei den Spielmobs. Dazu kommt ein **eigener Mob**: Kopf, Haare, Hände, Füße, Körper, Schwanz, Schulter und **Flügel** lassen sich durch eigene `.cub`-Dateien ersetzen. Die Teile werden zur Laufzeit als echte Spielmodelle (`cube::Sprite` mit Mesh) in die Modell-Registry gesetzt. Geöffnet wird es im LAB über **„Mobs …"** links unten.

Alle Adressen sind gegen die `Cube.exe` geprüft: disassembliert mit capstone, Aufrufkonvention über `ret N` und Aufrufstellen.

---

## 1. Die 156 Rassen

Die Namen stammen aus dem cwsdk-Enum `cube::Creature::Race`. Für die Alpha ist das geprüft: Für jede Rasse mit zuordenbarem Kopfmodell setzt der Aussehensgenerator `creature_generateAppearance @0x43F7C0` genau das passende Modell.

| Rasse | Kopfmodell (Registry) |
|---|---|
| 0 ElfMale | `0x4D4 elf-head-m01.cub` |
| 17 Skeleton | `0x148 skeleton-head.cub` |
| 59 Bat | `0x113 bat-head.cub` |
| 94 Werewolf | `0x877 werewolf-head.cub` |
| 101 Dragon | `0x1AA dragon-head.cub` |
| 148 Mermaid | `0x53F mermaid-head-f01.cub` |

Der Generator kennt 156 Fälle, das Enum nur 154. Ergänzt sind **95 = Santa** (`santa-head.cub`) und **129 = unbekannt**.

**Standard-Feindlichkeit** (`Kreatur+0x60`), ausgelesen an allen 35 nativen Spawn-Stellen (`Spawn::ctor_0` mit `+0x28`):

| Wert | Bedeutung | Belegt bei |
|---|---|---|
| 1 | Monster | Fledermaus `0x3B`, Seekrabbe `0x6B`, alle Lagermonster |
| 3 | ansprechbarer NPC (Beruf 0x80–0x88) | Dorfhäuser, Steg |
| 5 | Begleiter | `0x5BA400` |
| 6 | passiv / Objekt | Vogelscheuche `0x8C`, Vase `0x8F`, Eisenvorkommen `0x84` |

Das Labor setzt 6 für Pflanzen, Vorkommen und Objekte, sonst 1. Für Tiere ist 1 eine **Annahme**: Belegt sind nur Fledermaus und Krabbe. Wer ein friedliches Schaf will, stellt im Fenster 3 ein.

---

## 2. Native Ausrüstung

> **KORREKTUR 2026-10-01.** Die erste Fassung ließ den Ausrüster ohne vorher erzeugtes
> Aussehen laufen und setzte die Flags `Spawn+0x7A` auf 0. Das Spiel macht es anders
> (`0x5D7990..0x5D79C9`): **erst** `creature_generateAppearance` auf dem Spawn (`+0x2C`, `+0x74`),
> **dann** der Ausrüster. Der Aussehensgenerator setzt bei 27 Rassen Flags in `Aussehen+6`,
> beim Drachen zum Beispiel `|0x33`. Bit `0x10` gehört zu `0x18` und heißt „unbewaffnet".
> Ohne diesen Schritt bekam ein Drache ein Schwert („Dragon … Klasse 0x1 Krieger" im Log).
> Das ist behoben.

### Der Ausrüster `@0x5C7730`

Im Reversal heißt er fälschlich `creature_initBehaviorByType`. Er arbeitet auf einem **`cube::Spawn`** (0x10F0 Byte, ctor `0x5ADB20`):

- Für gewöhnliche Rassen würfelt er die Klasse: `Spawn+0x30 = rand()%4 + 1`, also 1 Krieger, 2 Waldläufer, 3 Magier, 4 Schurke. Danach setzt er passende Waffen, zum Beispiel `+0x8C8 = 3` (Kategorie Waffe), `+0x8C9` = Waffenart, `+0x8D5` = Material.
- **Sonderrassen** haben feste Ausrüstung: 0x6C Troll, 0x6D DarkTroll, 0x72 Yeti, 0x2E Ogre, 0x51 Djinn, 0x52 Minotaur …
- **Berufe 0x80–0x89** bekommen Brustrüstung (`+0x350 = 4`) und eigene Wertefaktoren.
- Bit `0x40` in `Spawn+0x7A` heißt „keine Klasse", `0x18` heißt „unbewaffnet". Das Labor löscht beide.

Aufruf wie im Spiel (`0x5D79B4..0x5D79C9`): `push 0; push spawn; call 0x5C7730`. Die Funktion endet mit `ret 8`, liest `ECX` nicht und hat zwei Stack-Argumente.

### Übernahme in die Kreatur — Befehl für Befehl wie die native Umwandlung `0x61002B..0x61017F`

| Spawn | → Kreatur | Inhalt | Wie |
|---|---|---|---|
| `+0x120` | `+0x300` | 13 Ausrüstungsplätze à 0x118 | `0x44AF00`, `__thiscall(ziel, quelle)`, `ret 4` |
| `+0x30` / `+0x31` | `+0x140` / `+0x141` | Klasse bzw. Beruf, Spezialisierung | Byte |
| `+0xF58..+0xF68` | `+0x178..+0x188` | Wertefaktoren | 0x14 Byte |
| `+0x08` | `+0x1D2C` | Aggro-Radius | float |

Danach gibt der virtuelle Destruktor (`vtable[0]`, `ret 4`, Flag 1) den Spawn mit dem Allokator des Spiels frei.

**Bestimmte Klasse erzwingen.** Bei gewöhnlichen Rassen überschreibt der Ausrüster jede Vorgabe mit seinem Wurf. Das Labor ruft ihn deshalb so oft auf, bis die gewünschte Klasse fällt, höchstens 32-mal. Bei 1:4 trifft das praktisch immer. Sonderrassen behalten ihre feste Klasse.

`Creature_drawModel @0x4128F0` zeichnet die Waffe aus `+0x990` (linke Waffe) und die Rüstung aus `+0x530` (Brust). Die Ausrüstung ist also sichtbar.

---

## 3. Eigener Mob

### Teile = Registry-Indizes

Das Aussehen liegt ab `Kreatur+0x78`. Die Teile-IDs sind `int16`-Indizes in die Modell-Registry:

| Teil | ID | Skala | Position |
|---|---|---|---|
| Kopf | `+0x8C` | `+0x9C` | `+0xE8` |
| Haare | `+0x8E` | `+0x9C` (geteilt mit Kopf) | `+0xE8` (geteilt) |
| Hände | `+0x90` | `+0xA4` | `+0xF4` |
| Füße | `+0x92` | `+0xA8` | `+0x100` |
| Körper | `+0x94` | `+0xA0` | `+0xDC` |
| Schwanz | `+0x96` | `+0xB4` | — (nicht belegt) |
| Schulter | `+0x98` | `+0xB8` | — (nicht belegt) |
| **Flügel** | **`+0x9A`** | **`+0xBC`** | **`+0x118`** |
| Gesamtgröße | `+0x80` | | |

Belegt durch den Aussehensgenerator, zum Beispiel beim Skelett: `+0x14 = 0x1490148` (Kopf `skeleton-head`, Haare `skeleton-eyes`), `+0x1A = 0x14B014C` (Füße `skeleton-foot`, Körper `skeleton-body`). Die Kopfposition steht als float bei `+0x70..+0x78` des Aussehens.

`Creature_drawModel @0x4128F0` holt **jedes** Teil generisch über `registry[id]`, ohne Prüfung der Rasse. Eine ID < 0 bedeutet: nicht zeichnen. Damit funktionieren auch **Flügel an einem humanoiden Körper**.

### Eigene `.cub` als echtes Spielmodell

1. Freien Registry-Platz suchen: `SpriteManager+4/+8` ist der Vektor, freie Plätze sind Nullzeiger. Gibt es keinen, wird der Vektor um 64 Plätze vergrößert. Der alte Puffer bleibt stehen, damit kein anderer Thread ins Leere liest.
2. 0x60 Byte mit `operator new` des Spiels (`0x68D668`) anlegen, dann **`Sprite::ctor_0 @0x4E6A20`**, `__thiscall(this, device, 0)`, `ret 8`. Das Device steht bei `SpriteManager+0x10`.
3. **`VoxelModel_set_data @0x4E7650`**, `__thiscall(this, x, y, z, rgb, strict=1)`, `ret 0x14`, kopiert die Farben und baut das Mesh. So kopiert das Spiel bei `0x4879E2` selbst Modelle.
4. Den Zeiger in den Registry-Platz schreiben und die ID ins Aussehen setzen.

Dieselbe Datei bekommt immer dieselbe ID. Ist sie neuer gespeichert (Änderungszeit), wird sie **in denselben Sprite neu geladen**. Teil im Editor ändern, speichern, neu spawnen — fertig.

### Bedienung

- **„Neuer leerer Mob":** Alle Teile stehen auf „keins", die Basis ist HumanMale. Zu sehen ist nur, was du einsetzt.
- **„Auswahl als Basis":** Die links gewählte Rasse bestimmt Skelett, Bewegung und alle Teile, die auf „von der Basisrasse" stehen. Für Flügelwesen ist eine Fledermaus oder ein Vogel als Basis sinnvoll.
- **Teil wählen, dann die Quelle:**
  - von der Basisrasse
  - keins
  - natives Modell (Index, z. B. `0x113`)
  - eigene `.cub` — über **„…"** eine Datei wählen, oder über **„Ed."** die gerade im LAB-Editor offene Struktur übernehmen. Sie wird als `Strukturen\Mobs\Teile\<mob>-<teil>.cub` gesichert.
- Skala und Position sind optional. Mit **„Letzten Mob auslesen"** siehst du die echten Werte der Basisrasse und kannst daran anknüpfen.
- **Speichern/Laden:** `Strukturen\Mobs\<name>.mob`, eine Textdatei.

---

## 3b. Native Mobs editieren

**„Native Rasse laden (Aussehen + Ausrüstung)"** erzeugt für die gewählte Rasse einen Spawn wie das Spiel: Aussehen, dann Ausrüster. Es übernimmt:

- das **komplette Aussehen** (0xAC Byte ab `Kreatur+0x78`: Farben, Haare, Größen, Flags) als Grundlage,
- alle **Teile** als „natives Modell" mit ihrer echten ID, Skala und Position,
- alle **13 Ausrüstungsplätze** als vollständige Items (Kategorie, ID, Material, Seltenheit, Level und die übrigen Bytes).

Danach ist alles einzeln änderbar: ein Teil austauschen (z. B. eigene Flügel), Skala und Position ändern, einen Ausrüstungsplatz ändern, leeren oder auf „wie Ausrüster" zurückstellen. Beim Spawnen wird zuerst das Aussehen geschrieben, dann die Ausrüstung, dann die Teile. Alles wird in der `.mob`-Datei gespeichert.

Belegte Item-Felder: `+0x00` Kategorie (3 = Waffe, 4 = Brustrüstung), `+0x01` ID, `+0x0C` Seltenheit, `+0x0D` Material, `+0x10` Level. Waffen-IDs je Klasse laut Ausrüster: Krieger 0–2 bzw. 15–17, Waldläufer 6–8.

**Letzten Mob auslesen** sucht die Kreatur jetzt über ihre **GUID** in der Entity-Map (`std::map` bei `World+4`, Knoten `+0x10` GUID, `+0x18` Creature*). Vorher hielt der Code einen Rohzeiger, und beim Auslesen kamen Müllwerte heraus.

## 4. Ungeprüft

- **Im Spiel gelaufen ist nichts davon.** Geprüft sind die Konventionen in der Exe, die Offsets (deckungsgleich mit der nativen Umwandlung) und das Kompilieren.
- **Fähigkeiten**: Der Ausrüster legt Fähigkeits-IDs in `Spawn+0x10AC` ab. Die native Umwandlung übernimmt sie über einen Pfad, der noch nicht entschlüsselt ist. Das Labor übernimmt sie nicht. Sonderrassen kämpfen deshalb womöglich ohne ihre Spezialangriffe.
- **Verhalten**: Die KI ist dieselbe wie bei den bisherigen Mod-Mobs, also Kampf plus Umherwandern. Die nativen Wegpfade (`WalkPathBehavior`) baut das Labor nicht nach.
- **Drehungen** der Teile (`+0xC0..+0xD8`) sind im cwsdk als `int` geführt. Ob es floats sind, ist nicht belegt, deshalb sind sie nicht einstellbar.
- Ob das Spiel das Aussehen später neu erzeugt und eigene Teile wieder überschreibt, ist nicht beobachtet. Bei den bisherigen Mod-Spawns blieb das Aussehen stabil.
- Die **Größe eigener Teile** sollte sich an den nativen orientieren, etwa 10–20 Blöcke. Das Spiel zentriert und skaliert sie über die Skala-Felder.
