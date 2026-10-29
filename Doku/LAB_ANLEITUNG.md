# Das LAB-Fenster

**Das LAB ist jetzt ein eigenes Windows-Fenster neben dem Spiel.** Es öffnet sich beim Start des Mods von selbst, **[F10]** im Spiel blendet es ein und aus. Es enthält alle bisherigen LAB-Funktionen als Knöpfe, dazu eine Bibliothek aller Strukturen, einen Ebenen-Editor mit 3D-Vorschau und den Weg zurück ins Spiel. Gesetzt wird über `WorldInfo_placeStructure`, also genau so, wie das Spiel seine eigenen Gebäude setzt. Gemerkte Strukturen sind nach einem Neustart wieder da.

Das alte **[L]**-Overlay im Spiel bleibt unverändert und funktioniert weiter.

---

## 1. Aufbau des Fensters

Das Fenster passt sich dem Bildschirm an. Es öffnet sich höchstens so groß wie der Arbeitsbereich, auch bei 125 % oder 150 % Windows-Skalierung. Es ist frei skalierbar, die Mindestgröße ist 1260 × 700.

| Bereich | Inhalt |
|---|---|
| **links** | **Spielfunktionen** als Liste: Doppelklick oder „Ausführen". Darunter „Karte & Leistung …" und das **Protokoll**. |
| **Mitte oben** | **Werkzeugleiste**, immer sichtbar: Neu, Öffnen …, **Speichern**, **Speichern unter …**, Rückgängig, Name. Darunter Größe, Zuschneiden, Drehen, Spiegeln. |
| **Mitte** | **3D-Ansicht** (Mausrad = Zoom) und **Ebenen-Editor**. Markerblöcke tragen dort einen weißen Rahmen und ihren Buchstaben. |
| **rechts oben** | Werkzeuge, RGB-Eingabe, **Palette** |
| **rechts unten** | Reiter **Bibliothek** · **Ins Spiel** · **Platzierungen** |

### Farben

- **Baufarben:** 84 Felder. Graustufen, fünf Reihen à 12 Farbtöne (kräftig, mittel, gedeckt, hell, dunkel) und eine Reihe Baustoffe.
- **RGB-Eingabe:** drei Zahlen eintippen, Enter oder „Farbe setzen". **Farbwahl …** öffnet den Windows-Farbdialog.
- **Eigene Farben:** 12 Plätze. **Rechtsklick legt die aktuelle Farbe ab**, Linksklick wählt sie. Die Plätze werden in `Strukturen\farben.txt` gespeichert.
- **In dieser Struktur:** die 24 häufigsten Farben der offenen Struktur.

### Marker — eigene Spalte, geschützt

Die Markerfarben stehen **rechts in einer eigenen, farbig abgesetzten Spalte**, beschriftet mit Tür, Fenster, Lampe, Customize-Bench, Schild, Amboss, Esse, Tisch, Blume und Schmied-NPC. Ist ein Marker gewählt, zeigt der Farbkopf in Orange: „MARKER: … wird beim Setzen zum Objekt".

**Schutz vor Versehen.** Die Markererkennung im Mod ist tolerant: Je Kanal zählt 0–10 als 0, 117–137 als 127 und 245–255 als 255. Eine Baufarbe wie (250, 4, 6) würde deshalb zur Tür. Jede Farbe, die **nicht** aus der Marker-Spalte kommt, wird darum geprüft: aus dem Raster, über RGB, Farbdialog oder eigene Plätze. Fällt sie in ein Markerband, verschiebt der Mod einen Kanal um wenige Stufen und meldet es im Protokoll. Keine der 84 Baufarben ist betroffen; das ist nachgerechnet. Mit der Pipette darf man vorhandene Marker bewusst wieder aufnehmen.

### Speichern

| Aktion | Wirkung |
|---|---|
| **Speichern** / **Strg+S** | in die Datei, aus der die Struktur stammt. Ist es noch keine eigene (neu, nativ, Scan, Export), geht sie nach `Strukturen\<Name>.cub`. Gibt es die Datei schon, wird nachgefragt. Ein **geänderter Name ergibt eine neue Datei**. |
| **Speichern unter …** / **Strg+Umschalt+S** | Windows-Dialog, beginnt im Ordner `Strukturen`. Außerhalb davon gespeicherte Dateien erscheinen nicht in der Bibliothek. |
| Titelleiste mit `*` | es gibt ungespeicherte Änderungen |
| Öffnen, Neu, Scannen … bei ungespeicherten Änderungen | Nachfrage: Speichern / Verwerfen / Abbrechen |

Gespeichert wird immer im nativen `.cub`-Format, die Beiwerte (typ, Fundament, Herkunft) kommen in eine gleichnamige `.txt`. Nach dem Speichern wird die Bibliothek neu eingelesen und die Datei darin markiert.

### Bedienung im Editor

| Eingabe | Wirkung |
|---|---|
| Linke Maustaste (ziehen) | Werkzeug anwenden: Stift, Radierer, Pipette oder Fläche füllen |
| Rechte Maustaste (ziehen) | radieren |
| Mausrad im Ebenen-Editor, **Bild auf/ab** | Ebene wechseln |
| **1 / 2 / 3 / 4** | Stift / Radierer / Pipette / Fläche füllen |
| **Strg+Z** | Editor rückgängig (40 Schritte) |

---

## 2. Woher die Strukturen kommen

Die Bibliothek lädt beim Öffnen **alle** bisherigen Strukturen. Die Suche filtert nach Name, „nur Bauten ≥ 10" blendet Kleinkram aus.

| Kürzel | Quelle | Wie |
|---|---|---|
| `[mod]` | **Turm (Bergdorf)**, **Haus (Blocknachbau)**, **Schmiede (Blocknachbau)** | Die Bauplanfunktionen aus `MountainVillage.cpp` werden ausgeführt, aber **nur aufgezeichnet**. Die Welt bleibt unberührt. Objekte (Amboss, Bench …) erscheinen als Markerfarben. |
| `[eigen]` | `Strukturen\*.cub` | eigene, im LAB gespeicherte Strukturen |
| `[export]` | `Strukturen\Export\*.cub` | native Modelle, die mit „Alle nativen Bauten exportieren" als Datei abgelegt wurden |
| `[scan]` | `HausBloecke.txt` | jeder Blockscan (STRG+H) als eigene Struktur |
| `[0x903]` … | **alle 2442 benannten nativen Modelle** | direkt aus dem Speicher des Spiels (Registry), mit Dateinamen und Größe. Liest automatisch, sobald man im Spiel ist. |

Außerdem kann man jede `.cub`-Datei über „Datei öffnen …" laden, und mit „Um mich scannen" oder „Quader scannen" ein Stück der Welt übernehmen.

---

## 3. Ins Spiel bringen

1. Ziel wählen: **„40 Bl. +X vor mir"** (die bewährte Testregel, weiter weg rendert das Spiel nicht zuverlässig) oder **„hier"**. Man kann X/Y/Z auch direkt eintippen. Es ist die **untere Ecke**, so wie das Spiel selbst platziert.
2. Drehung, **typ**, Fundament und die beiden Häkchen einstellen:
   - **Markerfarben → echte Objekte**: Tür-, Fenster-, Lampen-Marker usw. werden zu benutzbaren Objekten. Das ist derselbe Weg wie im Bergdorf: gruppieren, anlegen, indexieren.
   - **In dieser Welt merken**: Die Struktur wird als `.cub` gespeichert und in die Platzierungsliste der Welt eingetragen. Damit ist sie dauerhaft (siehe Abschnitt 4).
3. **SETZEN**.
4. Gefällt es nicht: **„Welt: Rückgängig"** stellt die Blöcke der letzten Platzierung wieder her. Objekte wie Türen bleiben bis zum Neuladen der Zone stehen.

### typ = 0 oder nicht?

`typ` ist `param_5` von `placeStructure`, der Kultur- bzw. Gebäudetyp. Er schaltet die **native Markerlogik** ein.

- **typ 0 (Voreinstellung, empfohlen)**: Das Spiel setzt nur Blöcke, die Marker ersetzt der Mod. Das ist erprobt.
- **typ ≠ 0 (experimentell)**: Das Spiel legt die Objekte selbst an. Zur Laufzeit hängt es sie aber **nicht** in den Ortsindex. Das LAB holt das nach, indem es die neuen Objekte zählt, indexiert und den Index der Zone nachzieht. Der Knopf **„Vorschlag"** liefert den Wert, den das Spiel für das jeweilige Gebäude nimmt: `0x0D` Schmiede, `0x0E` Zimmerei, `0x0F` Weberei, `9`–`0x0C` Klassengebäude.

---

## 4. „Nativ gespeichert" — wie Dauerhaftigkeit funktioniert

Der Subagent hat das im Reversal geklärt; Details und Belege stehen in **`RE_EigeneStrukturen.md`**. Das Wichtigste:

- **`placeStructure` liest vom Modell genau vier Felder**: `+0x30` (RGB-Zeiger), `+0x44/+0x48/+0x4C` (Größe). Ein selbst angelegter, genullter 0x60-Block mit diesen Feldern verhält sich exakt wie ein natives Modell. Das LAB baut daraus ein **Schattenmodell**. Es braucht keinen Registry-Eintrag und keine vtable.
- **Das `.cub`-Format ist das native Rohformat**: `int32 X, Y, Z`, danach `X·Y·Z·3` Byte RGB, x läuft am schnellsten, (0,0,0) ist leer. Exportierte native Modelle sind byte-gleich mit dem, was das Spiel lädt.
- **Dauerhaft wird nichts über den Spielstand.** Der Mod merkt sich je Welt, **was wo steht** (`Strukturen\platzierungen_<Seed>.txt`), und stempelt es **bei jeder Zonengenerierung neu**. Genau so erzeugt das Spiel seine eigenen Dörfer jedes Mal neu aus dem Seed. Dabei wird nichts in den Spielstand geschrieben, ein Spielstand kann also nicht beschädigt werden, und die Markerobjekte entstehen bei jedem Laden ebenfalls neu.
- Gemerkte Platzierungen erscheinen rechts unten. Dort kann man sie **öffnen** (Struktur samt Position in den Editor) oder **entfernen**. Eine entfernte Platzierung verschwindet beim nächsten Laden der Zone.

---

## 5. Sicherheit — warum das LAB das Spiel nicht zum Absturz bringt

| Regel | Umsetzung |
|---|---|
| **Das Fenster fasst das Spiel nie an.** | Jede Aktion ist ein Auftrag (`LabBridge::Post`). Abgearbeitet wird er im EndScene-Hook auf demselben Thread wie alle anderen Mod-Bausteine, höchstens ~12 ms pro Frame. |
| **Jeder Auftrag ist SEH-gekapselt.** | Stürzt einer ab, steht im Protokoll `Auftrag '…' abgestürzt (SEH abgefangen)`, und das Spiel läuft weiter. |
| **Neu-Stempeln nicht auf dem Generator-Thread.** | `OnZoneGenerated` merkt die Zone nur vor. Gestempelt wird 300 ms später im Spiel-Frame. |
| **Ortsindex nachziehen.** | Jedes Anhängen an `zone->static_objects` kann den Vektor verlegen. Dann zeigen **auch die nativen** Indexknoten der Zone ins Leere, das war der Absturz bei `0x00004800`. Neu ist `RepointZoneIndexRaw`: Es geht alle 1024 Zellen ab und setzt `obj` aus dem Handle `{zoneX, zoneY, index}` neu. Das läuft nach jedem Markerersatz, im LAB **und im Bergdorf**. |
| **Spiel-Hotkeys gesperrt, solange das LAB vorne ist.** | `GetAsyncKeyState` ist global. Vorher hätte Strg+X beim Ausschneiden im Suchfeld „Extra-Cubes verwerfen" ausgelöst. Alle Abfragen in `main.cpp` laufen jetzt über `Key()`, das bei aktivem LAB nichts meldet. |
| **Leerfarbe prüfen.** | Hält das Spiel eine andere Leerfarbe als (0,0,0) (`DAT_0076B340`), werden die Lücken im Schattenmodell darauf umgefärbt. Die Statuszeile zeigt „Leerfarbe ok/ABWEICHEND". |

---

## 6. Dateien

| Datei | Rolle |
|---|---|
| `LabWindow.h/.cpp` | Fenster, Editor, 3D-Ansicht, Bibliothek, alle Knöpfe |
| `LabBridge.h/.cpp` | Auftragswarteschlange, Protokoll-Abzweig, Import/Export, Setzen mit Schattenmodell, Platzierungsliste, Neu-Stempeln |
| `LabStructure.h/.cpp` | Datenmodell im `.cub`-Layout, Laden/Speichern, Scans aus `HausBloecke.txt`. Kennt das Spiel nicht und ist deshalb einzeln getestet. |
| `LabModelNames.h` | Registry-Index → Dateiname, 2442 Einträge, **automatisch erzeugt** aus `GameController_load_game_assets @0x4EE230` |
| `RE_EigeneStrukturen.md` | Bericht des Subagenten: Lader, `.cub`-Format, Modellobjekt, Persistenzwege, Anhang A |
| `Strukturen\` | eigene `.cub` + `.txt` (Beiwerte: typ, fundament, herkunft) |
| `Strukturen\Export\` | exportierte native Modelle |
| `Strukturen\platzierungen_<Seed>.txt` | gemerkte Platzierungen je Welt: `name x y z drehung typ fundament marker` |

**Geändert:**

- `main.cpp`: F10, Queue-Abarbeitung, `Key()`-Sperre, Fensterstart, Protokoll-Abzweig.
- `MountainVillage.cpp/.h`: `ReplaceMarkersBox` (Quader statt Radius), `RepointZoneIndexRaw`, `IndexObjectsFromRaw`, Bauplan-Erfassung, LAB-Schnittstelle.
- `NativeHouseProbe.cpp/.h`: `RegistryModel()`. Außerdem ist `FindWallLampModel` korrigiert: Die Namenssuche konnte nie treffen, weil Modellobjekte kein Namensfeld haben. Der Index ist fest `0x895`.
- `AdventureMod.vcxproj`: neue Dateien eingetragen.

---

## 7. Ungeprüft

- **Der ganze Fensterteil ist nicht im Spiel gelaufen.** Kompiliert ist er gegen 32-Bit-Windows-Header (clang/mingw), fehlerfrei und ohne Warnung. Der 3D-Renderer ist in Python nachgerechnet. Laufzeitverhalten unter MSVC und im Spiel steht aus.
- **typ ≠ 0 zur Laufzeit.** Früher ließ `mode 0x0D` jede Platzierung ausfallen, die Ursache ist nie geklärt worden. Deshalb ist es nur als Experiment freigeschaltet.
- **Rückgängig** erfasst nur den Quader der Struktur, nicht das Fundament nach unten. Blocktypen außer fest/Luft werden als „fest" zurückgeschrieben.
- **Bauplan-Erfassung des Turms** liest das Gelände an der Spielerposition (`SurfaceZ`). Das Fundament unterhalb der Deckhöhe wird bewusst weggelassen, weil es geländeabhängig ist.
- Ob `OnZoneGenerated` auf einem eigenen Thread läuft, ist offen. Das LAB verhält sich so, als ob, und ist deshalb in beiden Fällen sicher.

---

## Logger und Absturzfänger

**Der Logger ist ein eigener Prozess und bleibt offen, wenn Cube World abstürzt.** Er startet automatisch mit dem Mod. Ist er geschlossen, startet ihn der LAB-Knopf **„Logger / Absturzfänger"** neu.

Das LAB selbst läuft im Spielprozess. Stürzt das Spiel ab, endet es mit. Deshalb lebt der Logger außerhalb: Die Mod-DLL startet sich ein zweites Mal über `rundll32.exe AdventureMod.dll,LabLoggerMain <PID>`. In diesem Prozess startet der Mod nicht.

| Was | Wie |
|---|---|
| **Live-Log** | Jede Konsolenzeile wird sofort mit `WriteFile` nach `AdventureMod_log.txt` geschrieben. Das Betriebssystem behält die Daten, auch wenn der Prozess stirbt. Das Log des vorigen Starts liegt in `AdventureMod_log_vorher.txt`. |
| **Absturzfänger** | Der Logger hängt sich als **Debugger** an Cube.exe. Damit fängt er auch Abstürze **ohne Fehlermeldung**: Fail-Fast `0xC0000409`, Heap-Korruption `0xC0000374`, Stapelüberlauf `0xC00000FD`, `0xC000041D` im EndScene-Hook. |
| **Bericht** | `AdventureMod_crash_<Zeit>.txt`: Ausnahme mit Bedeutung, Ort als `Modul+Offset`, bei Cube.exe zusätzlich die **Ghidra-Adresse**, Register, Aufrufstapel, Rohsuche auf dem Stapel und die letzten Logzeilen |
| **Minidump** | `AdventureMod_crash_<Zeit>.dmp`. In Visual Studio öffnen, dann „Nur systemeigen debuggen" – mit PDB zeigt das die Quellzeile. |
| **Exit-Code** | Auch ohne Debugger erkennt der Logger das Prozessende und nennt Code und Bedeutung. |
| **Debugger trennen** | Gibt Cube.exe für **Visual Studio** frei, denn es kann immer nur ein Debugger hängen. Danach laufen Log und Exit-Code weiter. |
| **Rückfall** | Hängt kein Debugger, schreibt ein Absturzfilter im Spiel `AdventureMod_crash_inproc.txt`. |

Die Option „Erste-Chance-Ausnahmen zeigen" listet auch Ausnahmen, die das Spiel oder die SEH-Hüllen des Mods selbst abfangen. Das hilft, wenn ein Absturz sich durch viele abgefangene Zugriffsfehler ankündigt.

**Automatische Sicherung:** Solange der Editor ungespeicherte Änderungen hat, sichert er alle 60 s nach `Strukturen\_autosave\`. In der Bibliothek erscheinen die Sicherungen als `[auto]`.

---

## Größe skalieren

- **Modelle (Editor):** „Skalieren ×" in der Werkzeugleiste. Ein Faktor wirkt auf alle Achsen (`2`, `0.5`), drei Faktoren wirken je Achse (`2 2 1` = nur die Grundfläche). Skaliert wird mit nächstem Nachbarn, Strg+Z macht es rückgängig. Hin und zurück ist verlustfrei, das ist getestet.
- **Mobs und Tiere (Mob-Labor):** „Größe ×" beim Spawnen und beim eigenen Mob. Skaliert werden Darstellung, Trefferfläche und Körperhöhe gemeinsam (`Kreatur+0x80/+0x84/+0x88`), damit Bild und Kollision zusammenpassen. Der erlaubte Bereich ist 0,2–10.

---

## Biom-Werkstatt (Reiter „Biome“)

**In der Biom-Werkstatt legst du eigene Biome an.** Sie bestimmen Bodenfarben, Klima, Kreaturen, eigene Mobs, prozedurale Pflanzen und Dungeons. Native Orte (Dorf, Ruinen, Portal, Dungeons) und das Bergdorf lassen sich je Biom abschalten. Die vollständige Anleitung steht in `BIOM_ANLEITUNG.md`, die Herleitung in `RE_Biome.md`.

## Logger: Leistung und Threads (auch wenn LAB oder Spiel hängen)

Der Logger ist ein eigener Prozess. Er liest die Messwerte aus einem gemeinsamen Speicher, den das Spiel beschreibt. Darum funktioniert er auch, wenn das LAB-Fenster hängt.

- **„Leistung ausgeben“** schreibt einmal einen Bericht ins Logfenster und hängt ihn an `AdventureMod_leistung.txt` an. Er enthält:
  - **Herzschlag:** Spielthread, LAB und Zonen-Thread. „STEHT seit x s“ bedeutet, dass der Thread hängt.
  - **Zonenerzeugung:** Zahl der Zonen, Dauer zuletzt, im Mittel und maximal. Dazu, wie viel davon die Mod-Objekte kosten (Bäume, Pflanzen, Zonen-Modelle).
  - **Gelände-Hooks:** Aufrufe von Höhe und Fluss, und wie viele davon neu gerechnet werden mussten.
  - **Bericht des Spielthreads:** Bildrate, Median, 99 %, Ruckler, Kosten je Mod-Baustein, Adressraum, Stand der Biome.
- **„Leistung alle 10 s ausgeben“** wiederholt den Bericht automatisch.
- **„Threads ausgeben (Hänger / Last)“** misst 1 Sekunde lang die CPU-Last aller Threads von Cube World. Danach zeigt es für die wichtigen Threads den Aufrufstapel, also Threads mit Start in Cube.exe oder AdventureMod.dll und solche mit mindestens 2 % CPU. Daran sieht man, wo ein Thread hängt oder rechnet. Der Bericht landet ebenfalls in `AdventureMod_leistung.txt`.

## Logger: „4 GB freischalten“

Steht im Absturzbericht `bad_alloc` oder „SPEICHER ERSCHOEPFT“, ist der 2-GB-Adressraum der Cube.exe voll. Dann so vorgehen:

1. Cube World beenden. Der Logger bleibt offen.
2. **„4 GB freischalten“** klicken. Das setzt ein Bit im PE-Kopf der Cube.exe; vorher legt der Logger `Cube.exe.vor-4GB.bak` an.
3. Rückgängig: denselben Knopf erneut klicken.

## Zonen-Modelle (Reiter „Zonen-Modelle“)

Mit diesem Fenster wird ein eigenes Modell Teil der Weltgenerierung. Es entsteht zusammen mit der Zone, wie die nativen Bäume. Es gibt kein Aufpoppen und kein Nachbauen beim Laufen.

**So geht's:**

1. Modell im LAB-Editor bauen oder öffnen.
2. Im Fenster **„Neu: Baum“**, **„Neu: Blume“** oder **„Neu: Dungeon“** wählen und einen Namen vergeben.
3. Das Modell eintragen:
   - **„Aktuelles LAB-Modell übernehmen“** speichert das offene Modell als `Strukturen\Zonenmodelle\<Name>.cub`;
   - oder **„…“** wählt eine vorhandene `.cub`.
4. **Gebiet** wählen: überall, nur Vanilla (ohne eigenes Biom) oder ein bestimmtes eigenes Biom. Dazu den **Boden** wählen: jeder Naturboden, Gras, Sand, Schnee oder Lava-Boden.
5. **„aktiv“** anhaken und **„Speichern + laden“** klicken. Ab jetzt erscheint das Modell in jeder neu erzeugten Zone.
6. **„Test“** setzt das Modell einmal 40 Blöcke neben dich, zum Ansehen.

| Feld | Bedeutung |
|---|---|
| Dichte | Baum/Blume: Versuche je Zone (256×256). Dungeon: Chance in % je Zone (höchstens einer je Zone und Modell) |
| Hang min / max | erlaubte Steigung am Standort. Vorgabe: Baum 0–1,4, Blume 0–0,8 |
| Abstand | Mindestabstand zu anderen eigenen Objekten (Bäume/Blumen: Mitte zu Mitte; Dungeons: Rand zu Rand) |
| z-Versatz | tiefer (negativ) oder höher einsetzen |
| Bodenhöhe min/max | nur zwischen diesen Höhen |
| Unebenheit | Dungeon: größter Höhenunterschied der Grundfläche |
| zufällig drehen | 90-Grad-Schritte |
| Fundament | füllt unter dem Modell bis zum Boden auf (Vorgabe bei Dungeons) |

**Wie gesetzt wird:**

- **Baum:** Der Stamm steht auf dem tiefsten Bodenblock unter ihm, damit er am Hang nicht schwebt.
- **Blume:** direkt auf dem Boden.
- **Dungeon:** auf dem höchsten Punkt der Grundfläche, mit Fundament darunter.
- **Reihenfolge je Zone:** erst Dungeons, dann Bäume, dann Blumen, zuletzt die Hangbäume der Biome.

**Nie gesetzt wird:**

- in Stadtvierteln, Dungeon- und Portalzonen;
- im Umkreis von 90 Blöcken um native Orte;
- in Bergdorf-Zonen;
- auf freien Ebenen eines Bioms;
- auf Wasser.

**Leere Voxel:** Ist eine Säule des Modells zwischen zwei Voxeln leer, macht das Spiel diese Stellen zu Luft (`placeStructure`). Bei Bäumen am Hang also Säulen lückenlos bauen, sonst wird der Hang ausgehöhlt. Für Innenräume von Dungeons ist genau das erwünscht.

**Sammelbar (eigener Gegenstand wie die Herzblume):** Knopf **„Sammelbar“**.

1. Name des Modells und **Gegenstand-Name** eingeben, also den Namen im Inventar.
2. Modell (.cub) eintragen. Das große Modell wird in der Welt auf „Blume klein“ verkleinert, Vorgabe 0,1 wie die Herzblume.
3. Gebiet, Boden, Dichte (Versuche je Zone) und Abstand wählen.
4. „aktiv“ anhaken und „Speichern + laden“ klicken.

- **Nummer:** Beim ersten Speichern bekommt der Gegenstand eine feste Nummer (Typ 0x0B, 0x30–0x4F). Die steht in der `.zmod` und darf sich danach nicht mehr ändern, sonst passen gesammelte Exemplare nicht mehr. Höchstens 32 eigene Gegenstände.
- **„Test“** legt 5 Stück 40 Blöcke neben dich. Aufheben wie eine Herzblume.
- **Ungeprüft im Spiel:** ob das Aufheben ohne Weiteres klappt und wie Handel und Handwerk mit den neuen Gegenständen umgehen.

**Blume klein (wie die nativen Pflanzen):** Das Spiel setzt kleine Pflanzen wie Schilf, Seerosen und Unterwasserpflanzen nicht als Blöcke. Es zeichnet das große Modell verkleinert. Genau so arbeitet das Feld **„Blume klein“** bei der Art „Blume“:

| Wert | Wirkung |
|---|---|
| `0` | Das Modell wird wie bisher als Blöcke gesetzt. |
| `0,09` | Das große Modell wird auf 9 % gezeichnet, wie die Pflanzen des Spiels. Ein 20 Voxel hohes Modell wird knapp 2 Blöcke hoch. Jede Blume streut zufällig um ±15 % und ist zufällig gedreht. |

- **Wind:** Die Blume wiegt sich im Wind wie Schilf.
- **Höchstens 6 solche Modelle.** Das Spiel hat dafür eine Tabelle mit 64 Plätzen, frei sind die Nummern 0x12 und 0x3B–0x3F.
- **„Test“** setzt 16 kleine Blumen 40 Blöcke neben dich. Vorher muss das Modell mit „aktiv“ gespeichert und geladen sein.
- **Status:** Die Liste zeigt je Modell „klein ×…, Pflanze Nr. …: bereit“, sobald das Spiel das Modell gebaut hat. Das passiert im nächsten Bild nach „Speichern + laden“.
- **Kein Hindernis:** Kleine Blumen sind wie die nativen Pflanzen nur Schmuck. Man läuft durch sie hindurch.
- **Technik:** `RE_Biome.md`, Abschnitt „Kleine Pflanzen“.

**Grenzen:** Ein Modell darf höchstens 250×250 Blöcke groß sein. Eigene Dungeons bekommen keine Besatzung, denn das Modell ist nur Bauwerk. Gespeichert wird in `Strukturen\Zonenmodelle\*.zmod`. Es wirkt nur in neu erzeugten Zonen. Im Spiel ist es noch ungetestet.

## Rezepte (Reiter „Rezepte“)

Eigene Handwerksrezepte. Dateien: `Strukturen\Rezepte\*.rez`.

1. **Neu** drücken, Name eintragen.
2. **Ergebnis** wählen: Liste enthält eigene Sammelbare („Eigen (sammelbar): …“ – jede Pflanze aus Zonen-Modelle mit Art „Sammelbar“, sobald sie dort mit „Speichern + laden“ ihre Gegenstands-ID bekommen hat; die Liste wird beim Aufklappen und beim Wechsel auf den Reiter neu gelesen, also auch für Tränke sofort verfügbar), Pflanzen, Nuggets, Fasern usw. Oder Typ/Unterart/Material von Hand (hex, z. B. `0x0B`).
   Level und Seltenheit (0–4) gelten für den hergestellten Gegenstand.
3. **Station** wählen (keine, Wasser, Ofen, Amboss, Spinnrad, Webstuhl, Säge, Werkbank, Lagerfeuer).
4. Bis zu **6 Zutaten**: Auswahl füllt Typ/Unterart/Material, Anzahl eintragen. Leere Zeilen zählen nicht.
5. Unter dem Ergebnis steht, **wie es benutzt wird** (getrunken, gegessen, ausgerüstet, getragen oder nur Zutat) und mit welchem **Aussehen**. **Modell:** „Wählen …“ nimmt eine eigene `.cub` (höchstens 64×64×64, am besten unter `Strukturen\`), „Spiel-Modell“ stellt das Aussehen des Spiels wieder her. Das eigene Modell gilt überall, wo das Spiel den Gegenstand zeigt: Inventar, Hand/Ausrüstung, am Boden (Gegenstandsmodell 0x4EC400). Ein Ergebnis mit eigenem Modell bekommt eine eigene Kennung (eigener Stapel) und wird neu angelernt; die Konsole meldet „Eigenes Modell … bereit“.
6. **Speichern + laden** – sofort aktiv. Mit „sofort bekannt“ lernt der Spieler das Rezept innerhalb von ~5 s.
7. Handwerksmenü im Spiel neu öffnen.

Hinweise:
- Das Spiel kennt ein Rezept über seinen **Ergebnis-Gegenstand**. Ist das Ergebnis ein nativer Gegenstand, ersetzt das eigene Rezept dessen Zutaten/Station.
- Reiter im Handwerksmenü: Waffen (Typ 3), Rüstung (4–7), Schmuck (8–9), Alchemie (Typ 1, Unterart 1/2), sonst Kochen; eigene Gegenstände (Typ 0x0B) im letzten Reiter.
- Löschen entfernt die Datei. Schon gelernte Rezepte bleiben im Spielstand, haben dann aber wieder die Spiel-Zutaten (bzw. keine, wenn das Spiel keins kennt).

### Wirkungen (Effekt-Tränke)

Im Rezept unter **Wirkung** wählen:

| Wirkung | Wert bedeutet | Standard |
|---|---|---|
| keine | normaler Gegenstand | – |
| Heilen beim Trinken (wie bisher) | normale Trank-Heilung des Spiels | – |
| Regeneration | % der max. Lebenspunkte je Sekunde | 2 |
| Stärke | % mehr Schaden (Waffe, Fähigkeiten, Zauber) | 50 |
| Schutz | % mehr Rüstung und Widerstand | 50 |
| Schnelligkeit | % schneller laufen | 40 |
| Langsames Fallen | höchste Fallgeschwindigkeit in Blöcken/s | 5 |
| Nachtsicht | % Helligkeit Richtung Mittag (100 = wie 12 Uhr) | 80 |
| Mana-Fluss | % Mana je Sekunde | 10 |
| Ausdauer | Ausdauer bleibt voll | – |

- **Dauer (s)**: wie lange die Wirkung hält. **zusätzlich heilen**: der Trank heilt beim Trinken außerdem wie ein normaler Trank.
- Mit einer Wirkung wird das Ergebnis automatisch ein Trank/eine Speise (Typ 0x01). Unterart 0x01 = Lebenstrank-Flasche (3 s trinken), 0x02 = Kaktustrank-Flasche (10 s), 0x04/0x05/0x06/0x08/0x09 = Speisen (10 s essen).
- Die Wirkung beginnt, wenn der Trank **ganz** ausgetrunken bzw. aufgegessen ist (wird man unterbrochen, verfällt er). Oben rechts im Spiel steht die Restzeit, im Chat Beginn und Ende.
- Gleiche Wirkung erneut trinken setzt die Dauer neu. Verschiedene Wirkungen laufen gleichzeitig.
- Jeder Effekt-Trank bekommt beim ersten Speichern eine **effekt_id** (steht in der .rez-Datei). Darüber erkennt der Mod den Trank im Inventar – nicht ändern, sonst wirken schon gebraute Tränke nicht mehr.
- Im Handwerksmenü stehen alle Effekt-Tränke im neuen **7. Reiter „Effekte“** (Symbol wie Alchemie, ganz rechts). Handwerksmenü nach dem Speichern neu öffnen.

## Reiter oben im LAB

Oben im LAB-Fenster: **Bauen | Mobs | Biome | Zonen-Modelle | Rezepte | Karte & Leistung | Logger**.
- „Bauen“ ist der Struktur-Editor (Spielfunktionen, 3D, Ebenen, Bibliothek). Editor-Tasten (1–4, Strg+Z/S, Bild auf/ab) gelten nur hier.
- Die anderen Reiter waren früher eigene Fenster. Ist das LAB-Fenster kleiner als eine Seite, erscheinen Bildlaufleisten (Mausrad, Umschalt + Mausrad = waagrecht).
- „Logger“ startet den Logger als eigenes Programm – er muss getrennt laufen, um Abstürze und Hänger aufzuzeichnen.


## Mob-Labor: Teile „von der Basisrasse“ (2026-10-07)

- Die Quelle eines Teils gilt sofort beim Umstellen der Auswahl (vorher erst mit „Teil übernehmen“).
- „von der Basisrasse“ nimmt jetzt wirklich Modell, Skala und Position der gewählten **Basis** – auch wenn vorher eine native Vorlage einer anderen Rasse geladen wurde (deren Farben/Aussehen bleiben).
- Eigener Körper/Kopf mit anderer Höhe als das Teil davor wird beim Spawnen automatisch ausgerichtet (siehe `Strukturen\Mobs\LIESMICH_Elfen_Zwerge.md`).


## Gegenstände (Reiter „Gegenstände“, 2026-10-07)

Eigene Gegenstände. Dateien: `Strukturen\Gegenstaende\*.item`.

1. **Neu**, Name eintragen.
2. **Art** wählen (Material/Zutat, Trank/Speise, Waffe, Rüstung, Schmuck, frei). Die Art schlägt eine **Grundlage** vor: Typ/Unterart/Material eines nativen Gegenstands. Die Grundlage bestimmt Verhalten, Werte und Reiter im Handwerksmenü (Trank-Form wird getrunken, Waffen-Form ausgerüstet usw.). Andere Grundlagen: Liste „Grundlage“ oder **„Vom angelegten Gegenstand“** (liest Waffe rechts, Brust bzw. Halskette aus dem Spiel).
3. Level, Seltenheit, **Modell** (eigene `.cub`, höchstens 64×64×64; leer = Aussehen des Spiels).
4. **Speichern + laden**: Der Gegenstand bekommt eine feste Kennung (im Modifier, eigener Stapel). Name und Modell gelten im Inventar, in der Hand/Ausrüstung und am Boden.
5. Ins Spiel bringen:
   - **Rezepte:** Er steht als „Gegenstand: Name“ bei Ergebnis und Zutaten.
   - **Ins Inventar legen** (Anzahl).

### Waffen (2026-10-13)

Zeile **„Waffe“**: Klasse, Einhand/Zweihand und Waffenart wählen. Das LAB setzt Art = Waffe, Typ 3, die Unterart und ein passendes Material. Schaden und Tempo rechnet das Spiel aus Unterart, Level und Seltenheit. Mit „Modell“ bekommt die Waffe ein eigenes Aussehen.

| Klasse | Einhand | Zweihand |
|---|---|---|
| Krieger | Schwert 0x00, Axt 0x01, Streitkolben 0x02, Schild 0x0D (Nebenhand) | Großschwert 0x0F, Großaxt 0x10, Großer Streitkolben 0x11 |
| Waldläufer | – | Bogen 0x06, Armbrust 0x07, Bumerang 0x08 |
| Magier | Armreif 0x0C | Stab 0x0A, Zauberstab 0x0B |
| Schurke | Dolch 0x03, Faust 0x04 | Langschwert 0x05 |

Belege aus der Exe:
- **Klasse:** `item_classifyRecursive` 0x4C6F20. Das Spiel legt die Klasse über die Unterart fest, eine eigene Einstellung dafür gibt es nicht.
- **Zweihand:** Liste in `Item_get_stack_or_grid_size` 0x4C7660.
- **Namen:** Namenstabelle in World.cpp.

Hinweis: Ein Rezept mit einem eigenen Gegenstand als Ergebnis nimmt Name und Modell vom Gegenstand. Eine Wirkung (Effekt-Trank) gibt es nur bei Rezepten ohne eigenen Gegenstand.

### Luftschiff-Taxi: eigene Waren

Im Reiter „Händler“ unten (seit 2026-10-14 stehen dort alle Händler zusammen): Für **Nord, Ost, Süd, West** je einen eigenen Gegenstand wählen (oder „Standard“ = Kopie der angelegten Waffe wie bisher), dann **„Taxi-Waren übernehmen“**.
- Gilt für alle Taxi-NPCs (Bergdörfer und Städte). Vorhandene NPCs bekommen den Katalog sofort neu.
- Preis, Name und Modell kommen vom Gegenstand (Level und Seltenheit bestimmen den Preis wie im Spiel).
- **„Gekaufte Ware behalten“**: aus = die Ware ist eine Fahrkarte und verschwindet nach dem Abflug (wie bisher), an = sie bleibt im Inventar.
- Gespeichert in `Strukturen\Gegenstaende\_luftschifftaxi.txt`. Den Kauf erkennt der Mod am Richtungs-Marker und zusätzlich an der Kennung des Gegenstands.

## Skills (Reiter „Skills“, 2026-10-13)

Eigene Fähigkeiten für alle Klassen oder für eine einzelne Klasse. Dateien: `Strukturen\Skills\*.skill`, Bilder in `Strukturen\Skills\Bilder\`. Beim ersten Start legt der Mod **Teleport** und **Beschwören** samt Bildern an. Teleport: `U` legt einen benannten Runenstein ab (`U` neben einem Stein hebt ihn auf), `6` portet hin, bei mehreren Steinen öffnet `6` die Liste (`6` = weiter, `4` = teleportieren). Die Zahl der Steine ist der *Wert* des Rangs. Beschwören: Taste `5`, ruft die zuletzt besiegten Gegner (Rasse und Level) als Begleiter; das eigene Haustier bleibt. Die Steine stehen je Welt in `AdventureMod_runensteine_<Seed>.txt`.

- **Art:** Teleport oder Beschwören. Die Zeile unter den Werten erklärt, was „Wert“ und „Dauer“ bei der gewählten Art bedeuten.
- **Klasse:** „alle Klassen“ oder Krieger, Waldläufer, Magier, Schurke. Geprüft wird die Klasse der Spielfigur (Kreatur+0x140).
- **Bild:** png, bmp oder jpg wählen. Es erscheint als Vorschau im LAB und in der Skill-Leiste im Spiel.
- **Taste:** F1–F12 oder G, H, J, K, N, V, B, Z, X, C. Sie wirkt nur, wenn das Spielfenster vorne ist.
- **Fortschritt:** Rang 1 gibt es ab Level „Rang 1 ab Level“. Danach kommt alle „Level je Rang“ ein Rang dazu, bis „max. Rang“. Die Tabelle unten zeigt jeden Rang mit Level, Wert, Dauer, Abklingzeit und Mana.
- **Werte:** Wert (+ je Rang), Dauer (+ je Rang), Abklingzeit (− je Rang, mindestens 1 s), Mana in % des Manas.
- **Skill-Leiste im Spiel:** Bilder unten rechts, über der F9-Anzeige. Sie zeigt die Taste darunter und während der Abklingzeit die Restsekunden. Einstellbar mit dem Haken links (Schalter `skill_leiste`).

Im Spiel:
- **Teleport:** Du springst „Wert“ Blöcke in die zuletzt gelaufene Richtung und landest auf festem Boden mit 3 Blöcken Luft darüber. Findet der Skill dort keinen Platz, versucht er es kürzer. Über Wasser teleportiert er nicht.
- **Beschwören:** Die zuletzt in deiner Nähe (48 Blöcke) besiegten Gegner kehren als **Begleiter** zurück: Anzahl = Wert (abgerundet), Lebensdauer = Dauer, mit deinem Level. Das Spiel gibt ihnen seine eigene Begleiter-KI (kämpfen + folgen, `cube_world_init_companion` 0x5EE7A0).
- Hinweise wie „nicht genug Mana“, „noch 4 s“ oder „ab Level 5“ erscheinen im Chat.

Weitere Arten, auch klassenspezifische, kommen über einen Eintrag in `Skills::Kind` und einen Handler in `Skills.cpp::Execute` dazu. Danach stehen sie in der Liste „Art“ und lassen sich mit „Klasse“ auf eine Klasse beschränken.

## Neue Tiere (2026-10-13)

26 neue Mobs liegen unter `Strukturen\Mobs\Tiere\*.mob`, ihre Teile unter `Strukturen\Mobs\Teile\Tiere\`.

| Gruppe | Mobs |
|---|---|
| Landtiere | Nashorn, Elefant, Fuchs, Polarfuchs, Luchs, Braunbär, Eisbär, Schwarzbär, Hirsch, Hirschkuh |
| Fantasie | Frost-, Gift- und Golddrache, Pegasus, Zentaur, Geist, Schlangenmensch |
| **Fliegend** (Vögel) | Adler, Falke, Kolibri, Schwalbe; Basis Krähe |
| **Fliegend** (Insekten) | Schmetterling, Monarchfalter, Libelle, Glühwürmchen, Marienkäfer; Basis Fliege, Mücke, Hummel |

So sind sie gebaut:
- Jedes Tier nutzt Skelett, KI und Bewegung seiner Basisrasse. Deshalb fliegen Vögel und Insekten wie Krähe, Fliege und Hummel.
- Die Teile sind die nativen Modelle dieser Basisrasse, umgefärbt und ergänzt um Horn, Geweih, Pinselohren, Flügel, Schwanz usw. Damit passen Größe und Drehpunkte.

Benutzen:
- **Mob-Labor:** `.mob` laden, spawnen, Teile und Positionen anpassen.
- **Biom:** in der Biom-Werkstatt unter „.mob-Dateien“ eintragen, z. B. `Tiere\hirsch.mob`.
- **Feindlichkeit:** Raubtiere und Fantasiewesen sind Monster (1), Hirsch, Pegasus, Kolibri, Schwalbe und Insekten sind friedlich (6).

Ungetestet sind die Position der Pegasus-Flügel, der Oberkörper des Zentauren (als Kopfteil) und der Schwanz des Schlangenmenschen. Diese lassen sich im Mob-Labor über die Positionsfelder nachstellen. Die Liste mit Basisrassen steht in `Strukturen\Mobs\Tiere\LIESMICH_Tiere.md`. Der Generator liegt in `Projekt\Werkzeuge\Mobs\animals.py` (nicht mitliefern).

## Gebäude-Funktionen: Funktions- und Leuchtblöcke (Bauen → „Funktionen“, 2026-10-13)

Für jedes Gebäude lässt sich festlegen, welche Blockfarbe beim **Setzen** zu einer Funktion wird. Gespeichert wird das als `<name>.funk` neben der `.cub`, und zwar mit „Speichern“ oder „Funktionen speichern“.

1. Gebäude öffnen oder anlegen. Mit der Farbe, die eine Funktion bekommen soll, die Stellen malen.
2. Rechts den Reiter **„Funktionen“** wählen, eine Funktion in der Liste wählen und **„Pinselfarbe → Funktion“** klicken.
   - **Objekte:** Truhe, Tisch, Stuhl, Bank, Bett, Nachttisch, Marktstand, Fass, Kiste, Sack, Theke, Regal, Zaun, Straßenlaterne.
   - **Stationen:**
     - Lagerfeuer / Braustand (Station 9: Kochen, Tränke)
     - Ofen, Amboss, Spinnrad, Webstuhl, Säge, Werkbank (Stationen 3–8)
     - Anpassungsbank
   - **Bewohner:** native Händler A–D (0x80–0x83, Waren vom Spiel), Wirt, Alchemist (Rezeptzutaten), „Unser Händler (Posten)“.
   - Nebeneinander liegende Voxel derselben Farbe ergeben **ein** Objekt (Mitte, Unterkante), so wie bei den Markern.
3. **„Pinselfarbe leuchtet (an/aus)“** macht alle Voxel dieser Farbe zu **Lichtblöcken**:
   - Blocktyp 0x0D, laut Exe „Helligkeit fest 255“ (`Terrain_sampleBlockBrightness` 0x59FEA0, Mesher).
   - Der Block bleibt sichtbar und fest und erhellt die Flächen im Umkreis von etwa 3 Blöcken.
   - Gut geeignet für Fenster, Kristalle, Kessel, Lampenschirme und Deckenleuchten.
4. Die Liste zeigt je Eintrag, wie viele Voxel ihn tragen. Darunter steht, wie viele Objekte und Lichtblöcke beim Setzen entstehen.

Beim **SETZEN** (Reiter „Ins Spiel“) gilt Folgendes:
- Der Mod legt die Objekte an und hängt sie in den Ortsindex. Die Funktionsvoxel räumt er dabei weg.
- Die Leuchtvoxel bekommen den Blocktyp 0x0D.
- Bewohner werden wie in den Stadtvierteln gesetzt, sobald du in der Nähe bist.
- Gemerkte Platzierungen nehmen die Funktionen mit und wenden sie beim Neuladen der Zone wieder an.

In den Städten leuchten die **Tempel-Kugel** und die **Trankflüssigkeit im Braustand** jetzt ebenfalls als Lichtblock.


## Stand 2026-10-13 (3)

- **Skills im Skilltree:** Teleport und Beschwören nutzen die nativen Plätze *Fähigkeit 4/5* (Kreatur+0x1138, Platz 9/10). Das native Fenster (X) zeichnet sie nicht; daneben erscheint die Leiste „Weitere Fähigkeiten“. Punkte dort verteilen, dann im nativen Fenster übernehmen – gespeichert wird mit dem Charakter. Im LAB unter Skills: *Skilltree-Platz* (9/10, −1 = nach Level) und *Punkte je Rang*.
- **Händler (Reiter):** Waren und Beruf von Markthändler, Alchemist und den fünf Händlerhäusern (`Strukturen\Haendler\*.haendler`), darunter die Waren des Luftschiff-Taxi-NPCs.
- **Mob-Labor:** Feindlichkeit 2 und 4 wählbar, Level und *eigene Rasse* (Kennung ≥ 200 + Name) für eigene Mobs. Der Mod trägt den Namen in die native Rassentabelle ein und setzt die Kennung nach dem Spawnen (Skelett/Bewegung bleiben von der Basisrasse).
- **Tiere:** `Projekt\Werkzeuge\Mobs\tiere_neu.py` baut alle 26 Tiere von Grund auf neu (eigene Rassen 200–225). Die alten, umgefärbten Teile liegen in `Sicherungen\Stand_2026-10-13_Tiere_alt_umgefaerbt.zip`.

### Eigene Tiere per Nummer spawnen (2026-10-14)

Im Reiter „Mobs“ stehen unter den 156 nativen Rassen alle eigenen Tiere mit ihrer Rassennummer und dem Zusatz „eigen“: die Tiere 200–225 und die Tierwelt 226–375.
- **Auswählen + SPAWNEN** (oder Doppelklick) setzt das Tier vor dir. „Anzahl“, „Level“ und „Abstand“ gelten auch hier.
- **Suche:** Nummer oder Name eintippen, z. B. `371` oder `wal`.
- **„Auswahl als Basis“** lädt das eigene Tier rechts zum Bearbeiten.
- **„12 Rassen ab Auswahl“** setzt bei eigenen Tieren 12 Tiere ab der gewählten Nummer.
- Die Nummern aller Tierwelt-Tiere stehen in `Strukturen\Mobs\Tierwelt\LIESMICH_Tierwelt.md`.

### Friedliche Tiere wie im Spiel (2026-10-16, gemessen)

Gemessen mit „Tiere in der Nähe genau auslesen“: Das native friedliche Huhn und ein feindlicher Schneeläufer haben dieselbe Feindlichkeit (1), denselben Verhaltensbaum (Kampf r=20, Spieler anschauen, Umherlaufen) und dieselben Werte. Der einzige Unterschied liegt in den Aussehens-Flags (Kreatur+0x7E): beim Huhn **0x0032**, beim Läufer **0x0028**. Friedliche Tiere haben Bit 0x10 („unbewaffnet“), feindliche Bit 0x08. **Korrektur 2026-10-16:** Entscheidend ist 0x02 **und** 0x10 zusammen (Huhn 0x32). Die Kampf-KI (CombatBehavior 0x42CB20) wählt dann keinen Angriff, sondern hüpft weg (Aktion 0x33). 0x10 allein führt zum Biss-Angriff (Aktion 0x4B) — deshalb griffen unsere „friedlichen“ Tiere an. Der Mod setzt jetzt 0x12 und löscht 0x08.
- **Friedlich heißt im Mod jetzt:** Feindlichkeit 1 + Flag 0x10 (0x08 aus). Genau wie das Huhn: angreifbar, mit Masse, zähmbar, mit Anzeige „LV x Name“.
- **LAB, Feindlichkeit:** Zur Auswahl stehen „Standard“, „1 Monster“, „1 friedlich (wie Huhn)“, „5 Begleiter“ und „6 Passiv“. Die Werte 2, 3 und 4 sind entfernt. 3 (NPC) erzeugte den Vornamen über dem Tier.
- **Mob-Datei:** `feindlichkeit=1` + `friedlich=1`. Alte Dateien eigener Tiere mit 2/3/4 werden beim Laden automatisch so umgedeutet.
- **Name:** Das Namensfeld bleibt leer. Das Spiel zeigt dann wie beim Huhn „LV x Rasse“ aus der Rassen-Namenstabelle.
- **Level:** Tierwelt-Tiere nehmen das Level der nativen Kreaturen in der Umgebung (Median, ±1), sonst das des Spielers.

### Tierdichte: mehr native Tiere (2026-10-16)

Ersetzt die alte Ozean-Variante. Das Spiel verteilt Tiere beim Erzeugen jeder Zone über ein 3 × 3-Raster von Spawn-Punkten (`WorldInfo_generateBiomeContent`, 0x5EAF75..0x5EBBC7). An jedem Punkt laufen dieselben Prüfungen wie sonst: 75 % Chance, Feuchte- und Kältefilter, mindestens 20 Blöcke Abstand, Land und Dichtefeld. Danach wählt das Spiel Rasse, Gruppe und Level wie gewohnt.
- Der Mod vergrößert nur dieses Raster: **Stufe 1** = Spiel (3×3), **2** = 4×4 (Standard), **3** = 5×5, **4** = 6×6.
- Alles andere bleibt nativ. Die zusätzlichen Tiere sind also ganz normale Spieltiere mit passenden Rassen, Gruppen und Leveln.
- **LAB → Netz & Shader → Tierdichte.** Die Einstellung gilt für Zonen, die ab dann neu erzeugt werden, und wird als `tierdichte=N` in `AdventureMod_schalter.cfg` gespeichert.

### Eigene Tiere am nativen Spawn (2026-10-16)

Unsere Tiere kommen jetzt über den Spawn des Spiels. Beim Erzeugen einer Zone legt das Spiel für jedes Tier einen Spawn-Datensatz an (Zone+0x18) und macht daraus später die Kreatur. Am Ende derselben Zonenerzeugung, über den Anker 0x5EDE84, tauscht der Mod bei einem Teil der nativen Tiergruppen das Aussehen gegen ein eigenes Tier:
- **Gleiche Basisrasse:** Das eigene Tier hat dasselbe Skelett und dieselbe Bewegung wie das native, an dessen Stelle es tritt. Ein Wolf wird z. B. zum Grauwolf, Schattenwolf oder Bären.
- **Passender Lebensraum:** Der Mod liest den Boden an der Spawnstelle (Schnee, Sand, Fels, Grasfarbe, Wasser daneben).
- **Nativ bleiben:** Ort, Gruppe, Level, Verhalten, Leine, Entladen und Wiederkehr. Friedliche Tiere bekommen Flag 0x10 (wie das Huhn), Flieger fliegen und Wassertiere schwimmen.
- **Anteil:** LAB → Netz & Shader → „Eigene Tiere am nativen Spawn“: 0 / 25 / 50 (Standard) / 75 / 100 %. Gespeichert als `tierwelt_anteil=N`.
- **Eigener Zonen-Spawn:** Er setzt nur noch Wassertiere und Tiere, deren Basisrasse das Spiel nirgends spawnt.
- Die Teile aller Tiere werden nach dem Laden auf dem Spielthread vorbereitet (4 Tiere je Durchgang, Meldung „…für den nativen Spawn vorbereitet“).


## Biome I / Biome II (Reiter „Biome I / II“, 2026-10-16)

Der Reiter hat zwei Seiten (Knöpfe oben links), die Biomliste links gilt für beide.

- **Biome I: Geografie** — alles wie bisher: Auswahl/Klima, Boden, Gelände, Flüsse, Bergzüge, Ebene, Stadt, Sternmassiv, Minen, Pflanzen, Dungeon-Bauweise, native Orte.
- **Biome II: Mobs** — welche Mobs wo im Biom spawnen:
  - **Regelliste:** *Was* (eigener Mob aus `Strukturen\Mobs` oder native Rasse), *Wo* (überall, Land, Wasser, Wald, Wiese, Savanne, Wüste, Dschungel, Schnee, Gebirge, Sumpf, Küste), *Gewicht*, *Level von/bis* (0 = Level der Gegend) und *Verhalten* (Standard, friedlich, feindlich).
  - „Test: am Spieler spawnen (+40)“ spawnt die Regel aus dem Editor sofort mit Level und Verhalten.
  - **Anteil %:** so viele der nativen Tier- und Fischgruppen werden durch die Liste ersetzt. Ort, Gruppengröße, Entladen und Wiederkehr bleiben nativ.
  - **Überschreibungen:** Anteil eigener Tiere (Tierwelt, gleiche Basisrasse; „wie allgemein“ = Einstellung unter Netz & Shader), Verhalten aller Tiere im Biom und Level der Tiere (nativ, feste Spanne, nativ + Versatz).
  - **Rechte Spalte:** Rassen-Ersatz (Hook 0x5F56C0), eigene Mobs des Spawners mit Schwierigkeit, Dungeon-Besatzung, Tierviertel.
- **Native Welt:** Die erste Zeile der Liste („[Welt] Native Welt“) gilt für alle Gebiete ohne eigenes Biom. Sie hat nur Biome II und wird in `Strukturen\Biome\_nativ.tiere` gespeichert.
- **Vorrang:** Level und Verhalten der Regel gehen vor der Biom-Überschreibung. Danach gilt der native Wert bzw. die .mob-Datei.
- **Wirkung:** Alles gilt für neu erzeugte Zonen. Das Spiel liest geänderte Regeln nach „Speichern + laden“ in etwa 2 s neu (Log: `[Biome II] Spawnregeln bereit`).
- **Ersetzt werden** nur native Tier- und Fischgruppen (Feindlichkeit 1). Lager- und Ortsmonster, Völker und Objekte bleiben.

**Datei (.biom bzw. _nativ.tiere):**
```
spawn_anteil=70
spawn=Tierwelt\hase;wiese;10;0;0;1      # was;wo;gewicht;level_min;level_max;verhalten
spawn=68;gebirge;5;20;30;2
tierwelt_anteil=-1
tier_verhalten=0
tier_level_modus=0
```

**Eigene Tiere am nativen Spawn:**
- Die Ersetzung läuft jetzt unabhängig vom Schalter „Objekte mit der Zone erzeugen“.
- Die Vorbereitung der Tiere geht schneller (3 Tiere pro Bild statt 4 alle 0,7 s).
- Der Lebensraum wird bevorzugt, ist aber kein Muss mehr.
- Alle 15 s steht im Log eine Zeile `[Tierwelt] Zonen … native Tiere … eigene Tiere …`. Zonen, die vor der Vorbereitung entstanden, werden als „vor der Vorbereitung“ gezählt.

### Tiergruppen (2026-10-16)

Die Tiere sind in 15 Gruppen eingeteilt, je 5 pro Medium. Die Gruppe steht in Spalte 7 von `Strukturen\Mobs\Tierwelt\_tierwelt.txt`; der Generator dazu ist `Werkzeuge\Mobs\tiergruppen.py`. Die 26 älteren Tiere aus `Strukturen\Mobs\Tiere` stehen jetzt ebenfalls in der Liste.

| Land | Luft | Wasser |
|---|---|---|
| Raubtiere (13) | Greifvögel & Eulen (10) | Raubfische & Haie (12) |
| Pflanzenfresser & Huftiere (22) | Sing-, Raben- & Buntvögel (14) | Friedfische (13) |
| Kleintiere (11) | Wasser- & Watvögel (11) | Meeressäuger & Riesen (12) |
| Reptilien, Amphibien & Krabbler (7) | Insekten & Fledertiere (13) | Krebse, Kraken & Reptilien (10) |
| Fantasiewesen (11) | Fantasiewesen & Drachen (14) | Fantasiewesen (3) |

**In Biome II:**
- Die Gruppen stehen ganz oben in der Auswahl „Was“.
- Eine Gruppenregel (`spawn=@land/raubtiere;wald;10;0;0;0`) setzt je Spawn **ein** Tier aus der Gruppe. Tiere, deren Lebensraum passt, kommen 6-mal häufiger.
- „Alle 15 Tiergruppen eintragen“ füllt die Liste in einem Schritt (Gewicht 10, Fantasie 2). Danach musst du nur noch Gewichte, Orte, Level oder Verhalten anpassen.
- Der Test-Knopf spawnt bei einer Gruppe ein zufälliges Tier daraus.

## Mob-Editor: Grundmodelle ändern (2026-10-16)

Unter der Teileliste gibt es zwei neue Knöpfe:

- **„Modell im Editor ändern“** holt das Modell des gewählten Teils in den Bau-Editor (Reiter „Bauen“):
  - bei „von der Basisrasse“ das Modell, das das Spiel für diese Rasse wählt,
  - bei „natives Modell“ das Registry-Modell,
  - bei „eigene .cub“ die Datei.
- **„Geändertes übernehmen“** speichert das bearbeitete Modell als `Strukturen\Mobs\Teile\<mob>-<teil>.cub` und stellt das Teil auf diese Datei um. Native Modelle und gemeinsam genutzte Teile bleiben unverändert.
- Danach mit „Speichern“ die .mob sichern.

## Skillfenster: Platz 9 (2026-10-16)

Laut Messung war Knoten 9 trotz Symbol unsichtbar. Das Spiel blendet ihn nach der Knotenschleife ausdrücklich aus (0x48EAF3: Knoten 9 holen, `push 0`, `call 0x411A90`). Der Mod ändert `push 0` zu `push 1` (0x48EAFC).

## Mob-Labor: Körperbau der Rasse (2026-10-16)

Wo welches Körperteil sitzt und wie groß es ist, legt das Spiel pro Rasse fest, und zwar in `creature_generateAppearance` (0x43F7C0). Die Werte landen im Aussehen ab Kreatur+0x78.

**Die Spalte rechts im Reiter „Mobs“:**
- **„Auswahl links lesen“** bzw. **„Basis des eigenen Mobs“** zeigt die Werte der Rasse. Ohne Haken steht der native Wert da, mit Haken die eigene Anpassung.
- Einstellbar sind:
  - Kollision (Breite/Tiefe/Höhe)
  - Skalen von Kopf, Körper, Händen, Füßen, Schwanz, Schulter, Flügeln
  - Positionen von Körper, Kopf, Händen, Füßen, Flügeln
  - Position +0x94: wird nicht gesetzt, Bedeutung offen
- **Haken setzen** = dieses Feld wird ersetzt.
- **„Testen: Rasse spawnen“** setzt die Werte sofort (ohne Datei) und spawnt die Rasse friedlich vor dir.
- **„Speichern (Datei)“** schreibt `Strukturen\Mobs\_koerperbau.txt` (`rasse;feld;wert[,wert,wert]`).
- **„Rasse auf nativ zurücksetzen“** entfernt die Anpassung wieder.

**Wirkung:** Ein Hook auf 0x43F7C0 ersetzt die Felder bei jeder neu entstehenden Kreatur dieser Rasse. Das betrifft:
- native Spawns in neuen Zonen,
- LAB-Spawns,
- eigene Tiere, deren Teile „von der Basisrasse“ kommen. Teile mit eigener Position in der .mob gehen weiterhin vor.

Bereits lebende Kreaturen behalten ihr Aussehen.

## Stand 2026-10-19

**Skillfenster (X):**
- Teleport und Totenbeschwörung haben je eine eigene Zeile unter den anderen Fähigkeiten.
- **Erster Kasten** (nativer Platz 9 bzw. 10, Punkte werden mit dem Charakter gespeichert): Freischalten und Manaverbrauch. Pro Rang kostet die Fähigkeit 10 % weniger Mana, höchstens 60 % weniger.
- **Zweiter Kasten** (vom Mod gezeichnet): Anzahl der Runensteine bzw. der Toten. Linksklick +1, Rechtsklick −1. Die Punkte stehen pro Charakter in `AdventureMod_skillpunkte.txt` und zählen gegen die freien Punkte.
- Beide Plätze sind nur bei offenem Fenster sichtbar. Ein Stub an 0x48EAFC ersetzt das native „Platz 9 ausblenden“ und setzt Sichtbarkeit und Lage jedes Bild.

**Körperbau:**
- Gilt jetzt auch für eigene Rassen (≥ 200), z.B. über „Eigener Mob (rechts)“. Die Anpassung wirkt am nativen Spawn, beim Mod-Spawn und in Biome II.
- Ändert sich der Körperbau, baut Biome II seine Rassen-Vorlagen neu.
- Bereits erzeugte Zonen behalten ihre Tiere.

**Lampe + Schimmerpilze:**
- Der Schimmerpilz (`Zonenmodelle\Schimmerpilz.zmod`, Gegenstand 0x0B/0x34) wächst überall auf Gras.
- An der Werkbank: Lampe + 5/12/25/50 Schimmerpilze ergibt die Lampenstufe 1–4.
- Lichtradius (nativ): Seltenheit × 8 + 10 Blöcke, also 18/26/34/42 statt 10.
- Ablauf: Das Rezept liefert einen eigenen Gegenstand mit `wird_zu=0x18,0x00,0x00,Stufe`. Den tauscht der Mod im Inventar sofort gegen die echte Lampe. Grund: Die Handwerksliste (0x4A161D) zeigt Typ 0x18 nicht an.

**Trank-Wirkungen:**
- Jede aktive Wirkung erscheint links als farbiges Kästchen mit Kürzel, Name, Restzeit und ablaufendem Balken.
- Das gilt auch in der Veröffentlichung (`Effects::DrawHud`).

**Neue Völker:** `Werkzeuge\Mobs\titanen_kull.py` erzeugt `Strukturen\Mobs\Voelker\*.mob` mit eigenen Rassen 380–389.
- **Titanen** (griechische Mythologie, Basis Kyklop, Größe ×2,6–3,2): Lichttitan (Hyperion), Meerestitan (Okeanos), Zeittitan (Kronos), Himmelsträger (Atlas), Feuertitan (Prometheus).
- **Kull** (Eragon, Elite der Urgals, Basis Oger, ×1,6–1,8): Kull, Kull-Häuptling, Kull-Schamane, Kull-Berserker, Kull-Wächter. Graue Haut, gelbe Augen, Widderhörner, Hauer.
- Am nativen Spawn ersetzen sie einen Teil der Kyklopen- bzw. Ogergruppen (gleiche Basisrasse, Anteil wie die Tierwelt). Sie stehen auch in der Tierwelt-Liste und in Biome II unter „Gruppe Land: Fantasiewesen“.

## Friedliche Tiere – Korrektur nach Messung (2026-10-19)

Die Flags entscheiden nicht, ob ein Tier angreift. Gemessen: Kuh 0x31 ist friedlich, Wolf und Borkenkäfer mit ebenfalls 0x31 sind aggressiv.

**Entscheidend ist `creature_isPassive` (0x444680, bei Ghidra „ability_isChanneled“).** Ein Tier ist passiv, wenn beides gilt:
- `(Flags7E & 0x1A00) == 0`
- die Rasse steht in einer festen Liste: Collie, Schäferhund, Alpakas, Schildkröte, Terrier, Katzen, Schwein, Schaf, Hase, Hornisse, Krähe, Huhn, Möwe, Papagei, Pfau, Frosch, Schnabeltier, Maulwurf, Koala, Eule, Pinguin, Pferd, Kuh, Krabben, Saphir-/Zitronenfisch, Seepferdchen.

**Verhalten passiver Tiere:**
- Die Kampf-KI (0x42D171) nimmt einen Spieler nur dann ins Ziel, wenn er das Futter dieser Rasse hält (Zähmen).
- Wird das Tier angegriffen, wehrt es sich (Bedrohung → Aktion 0x4B).

**Eigene Rassen (≥ 200)** stehen nicht in dieser Liste und waren deshalb immer aggressiv. Die Lösung:
- Ein Hook auf 0x444680 entscheidet für eigene Rassen nach `friedlich=1` in der .mob (bzw. alter Feindlichkeit 2/3/4).
- Mod-Marken im unbenutzten Bit 0x4000 („friedlich“) bzw. 0x8000 („feindlich“) von Kreatur+0x7E bzw. Spawn+0x7A überschreiben das für einzelne Kreaturen, z.B. per Biome-II-Regel „friedlich“/„feindlich“. Auch eine Kuh kann so feindlich werden.
- Die frühere Lösung über die Flags 0x02/0x10 ist entfernt. Sie ließ Tiere nur weghüpfen (Aktion 0x33).

„Tiere in der Nähe genau auslesen“ zeigt jetzt „greift von sich aus an: ja/nein“ und bei eigenen Rassen den Namen statt „(?)“.

## Namensschild eigener Rassen (2026-10-19)

**So entsteht das Schild** (0x49ADCF..0x49AE28):
1. `World_getObjectName` (0x59AA60) liefert das Rassenwort, z.B. „Chicken“. Dort trägt der Mod eigene Rassen bereits ein.
2. Danach holt `lookupNestedStringMap` (0x480E00, Sprachtabelle GC+0x314; thiscall(out, wort*, form „singular“ als Wert), ret 0x20) den angezeigten Text.
3. Für unbekannte Wörter liefert die Sprachtabelle "". Deshalb stand bei eigenen Rassen nur „LV 13“: CreatureLevelDisplay.dll (Sprung bei 0x49AE36) setzt lediglich „LV n “ vor den Namen.

**Lösung:** Ein Hook auf 0x480E00 gibt bei leerem Ergebnis das Wort selbst zurück, sofern es ein eigener Rassenname aus den .mob-Dateien ist.

## Skillfenster – Erweiterung mit den Widgets des Spiels (2026-10-19, 2. Stand)

Der Mod zeichnet im Skillfenster nichts mehr selbst. Er arbeitet nur mit den Widgets des Spiels.

**Gefundene Bausteine:**
- **Knoten** GC+0x800844: 11 Klone einer Vorlage, Elternfeld GC+0x800884. Angelegt im GC-ctor 0x4655A5.
- **„+“-Pfeile** GC+0x800838: 6 Stück. Angelegt bei 0x4605F0.
- **Fenster** GC+0x800908: 300 × 460. Gesetzt mit setSize 0x62C570 bei 0x464E50.
- **Widget-Funktionen:**
  - Lage: Knoten+0x38 → Feld +0x48 schreiben, danach `vtbl[1](1)`
  - Zahl: `setText` 0x636AD0
  - sichtbar: 0x411A90
  - Maus darüber: (Knoten+0x40) → 0x6294C0
  - Klonen: 0x6326D0 (Vorlage, Elternwidget)
- **Bisheriger Fehler:** Der Mod schrieb nur die Lage, ohne `vtbl[1](1)` aufzurufen. Deshalb blieb Knoten 9 sichtbar an der alten Stelle.

**Neu:**
- Platz 9 (Teleport) und Platz 10 (Beschwören) stehen als eigene Zeilen unter 4/5.
- Daneben stehen je ein „+“-Pfeil und ein Anzahl-Kasten. Beides sind Klone der Widgets des Spiels.
- Das Fenster ist zwei Zeilen höher.
- Der Pfeil 8 → alter Platz 9 ist ausgeblendet.
- **Punkte:** Der Anzahl-Kasten zählt im nativen Platz mit (Puffer = Freischalten + Anzahl). Dadurch stimmen „Points“, Übernehmen und Abbrechen wie im Original. Klicks gehen über `TreeClickRaw`, also mit derselben Punkteprüfung wie das Spiel. Die Anzahl steht pro Charakter in `AdventureMod_skillpunkte.txt`. Wird der Platz kleiner als die Anzahl (z.B. durch Abbrechen), wird die Anzahl angepasst.
- Die Zahl im Freischalt-Kasten ist Platz − Anzahl. Die Zahl im Anzahl-Kasten ist die Anzahl.
- Der Stub an 0x48EAFC (`TreeFixRaw`) läuft jedes Bild nach der Knotenschleife.

## Dungeon-Baukasten (Reiter „Dungeons“, Stand 2026-10-19)

Eigene Dungeons entstehen beim Erzeugen einer Zone aus Raum-Modellen, genau wie native Dungeons in einer einzigen Zone. Monster, Boss und Vasen werden native Spawns (Zone+0x18), Truhen sind native Truhenobjekte mit nativer Beute.

**Ordner:** `Strukturen\Dungeons\<Name>\dungeon.txt` + `Raeume\*.cub` + je Raum eine `.txt`.

| dungeon.txt | Bedeutung |
|---|---|
| `anteil` | % der geeigneten Zonen (Stadt, native Orte und andere Bauten sind ausgeschlossen) |
| `gebiet` | `ueberall`, `vanilla` oder `biom:<Name>` |
| `boden` | Blocktyp, den der ganze Platz haben muss (0 = egal, 4 Gras, 9 Sand, 10 Schnee) |
| `raster=gx,gy,gz` | Kammern in X/Y und Ebenen; höchstens 240×240 Blöcke Grundfläche |
| `kammer=b,t,h` | Größe jedes Raums; jede .cub muss genau so groß sein |
| `belegung`, `zusatzwege` | % belegter Kammern, % zusätzlicher Querwege |
| `tuer=b,h`, `wand=r,g,b` | Türöffnung (mittig je Seite) und Ersatzfarbe beim Zumauern |
| `bewohner`, `boss` | Liste: Zahl = native Rasse, sonst Mob-Pfad unter `Strukturen\Mobs` ohne `.mob` (z. B. `Voelker\kull-krieger`) |
| `level`, `boss_level` | Zuschlag auf das Zonenlevel |
| `uneben`, `fundament`, `zinnen` | max. Höhenunterschied, Fundament bis zum Boden, Zinnenkranz auf den obersten Kammern |
| `monster_je_marker` | Monster pro Monster-Marker (1–4) |

**Raum-.txt:** `art=raum|eingang|boss|treppe`, `tueren=N,O,S,W,oben,unten`, `gewicht=`, `drehen=0/1`.

**Regeln für die Raum-Modelle:**
- Norden = y+, Osten = x+. Der Boden liegt bei z 0–1, begehbar ist ab z 2.
- Türen liegen mittig in der Wand. Überzählige Türen mauert der Mod zu.
- Der Eingang liegt immer auf Ebene 0, Mitte Südseite. Der Boss sitzt in der Kammer, die am weitesten vom Eingang entfernt ist.
- Treppen sind eine Wendel um die Raummitte: 12 Ringfelder, ein Block pro Feld, eine Umdrehung = Kammerhöhe 12.
  - Treppenräume nicht drehen (`drehen=0`).

**Markerfarben:**

| Marker | Farbe |
|---|---|
| Monster | 254,1,254 |
| Boss | 254,1,128 |
| Truhe | 254,128,1 |
| Vase | 128,1,254 |
| Licht | 254,254,1 |

**Mitgeliefert** (Generator: `Werkzeuge\Dungeons\dungeons_bau.py <Strukturen>`):
- **Tempel:** 3×3 Kammern, 60×60, Sandstein/Gold. Bewohner: Skelette, Untote, Anubis. Boss: Lich oder Minotaurus.
- **Apfelhain:** klein, 2×2 Kammern, Hecken, Apfelbäume, Bienenstöcke, Brunnen. Bewohner: Pflanzenwesen. Boss: Troll.
- **Burg:** 3×3×3 Kammern (72×72×36), Wendeltreppen, Zinnen, Thronsaal. Bewohner: Orks, Goblins, Oger, Kull. Boss: Kull-Häuptling oder Oger.

**Testbau im LAB:** Der Bau entsteht 40 Blöcke vor dem Spieler, mit neuem Zufall pro Klick.
- Sofort da: Mauern, Lichter (als heller Block) und Monster.
- Erst beim Erzeugen einer neuen Zone: Truhen und Vasen. In geladene Zonen setzt der Mod keine Objekte, weil der Objektvektor dabei umziehen würde.

## Dungeon-Baukasten Format 2: Konstruktionselemente (Stand 2026-10-27)

Dieses Format ersetzt das Kammerraster. Ein Dungeon wird aus **Modulen** zusammengesetzt, die über **Anschlüsse** verbunden werden. Jeder Dungeon ist dadurch anders, hält aber eine Mindestgröße ein.

**Ordner:** `Strukturen\Dungeons\<Name>\dungeon.txt` (mit `format=2`) + `Module\*.cub` + je Modul eine `.txt`. Ordner im alten Format werden übersprungen.

| dungeon.txt | Bedeutung |
|---|---|
| `typ=name,breite,hoehe,wand,r,g,b` | Durchgangsart. Burg: `gang` 7 breit und 10 hoch (Oger/Trolle passen), Wand 2 dick. Die Farbe gilt beim Zumauern, wenn keine Wand daneben liegt. |
| `aussen=typ` | Durchgangsart des Eingangs nach draußen. Wird nie zugemauert. Davor bleibt ein Vorplatz frei, auf dem eine Treppe (1 Block pro Stufe, volle Torbreite) bis auf das Gelände entsteht. |
| `module=min,max` | Zahl der Module ohne Boss |
| `flaeche_min=b,t` / `flaeche_max=b,t` | Mindest- und Höchstgröße der Grundfläche (höchstens 240, eine Zone) |
| `hoehe_max` | höchste Modulhöhe (z). Burg: 28 = Ebenen 0, 14, 28 |
| `versuche` | so viele neue Grundrisse, bis die Mindestgröße erreicht ist |
| `monster_anteil` | % der Monster-Marker, die besetzt werden |
| `zinnen=1,r,g,b` | Zinnen auf freien Dachkanten der Module mit `dach=1` |
| `anteil`, `gebiet`, `boden`, `uneben`, `fundament`, `bewohner`, `boss`, `level`, `boss_level`, `monster_je_marker` | wie bisher |

**Modul-.txt:**
- `art=raum|eingang|boss`, `gewicht=`, `max=` (wie oft höchstens), `dach=0/1`
- `anschluss=typ,richtung,u,z`, mehrfach möglich:
  - `richtung`: N, O, S oder W
  - `u`: kleinste x- (bei N/S) bzw. y-Koordinate (bei O/W) der Öffnung
  - `z`: Boden-Oberkante
- Die Öffnung muss im Modul ausgeschnitten sein. Norden = y+, Osten = x+. Der Boden liegt bei z 0–1.

**Generator (`Dungeons.cpp`):**
1. Er beginnt mit dem Eingang (zufällig gedreht).
2. Er hängt an einen zufälligen offenen Anschluss ein passendes Modul: gleicher Typ, gegenüberliegende Richtung, gedreht, ohne Überlappung, innerhalb der Höchstgröße. Treffen dabei zwei offene Anschlüsse genau aufeinander, werden sie verbunden (Rundgänge).
3. Ist die Zielzahl erreicht, kommt der Boss an den offenen Anschluss, der am weitesten vom Eingang entfernt ist.
4. Wird die Mindestgröße nicht erreicht, beginnt ein neuer Versuch.
5. Alle offenen Anschlüsse werden zugemauert.
6. Obere Ebenen stehen auf Fundament.

**Burg (mit Tempelteilen):**
- Module:
  - Torhaus mit Flankentürmen
  - Gänge (gerade, lang, Ecke, T, Kreuz)
  - Treppenhaus
  - Burghof und kleiner Hof (offen, mit Zinnen)
  - Rittersaal (2 Ebenen, Galerie mit Anschlüssen oben)
  - Kaserne, Waffenkammer, Schatzkammer, Bibliothek, Turm
  - Tempelteile: Kapelle, Tempelhalle
  - Boss: Thronsaal oder Allerheiligstes
- 22–32 Module, mindestens 110×110 Blöcke, Ebenenhöhe 14.
- Treppen sind 3 breit, 14 Stufen (1 hoch, 1 tief), über jeder Stufe frei bis zur Decke (mindestens 4).
- Der alte Tempel ist abgeschaltet (`aktiv=0`), weil er jetzt in der Burg steckt.

**Apfelhain:**
- Felder 64×64 rund um `Strukturen\Apfelbaum.cub` (64³), mit Heckenwegen 8 breit.
- Felder:
  - Eingangstor
  - drei Baumfelder
  - Obstwiese
  - Brunnenplatz
  - Scheune mit Kisten
  - Boss: alter Apfelbaum (1,25-fach)
- 6–8 Felder, mindestens 128×128 Blöcke (meist 192×192). Nur jeder zweite Monster-Marker wird besetzt, dafür gibt es mehrere Truhen.

**Prüfen ohne Spiel:** `python dungeons_bau.py <Strukturen> --pruefen --vorschau`
- würfelt 300 Grundrisse mit denselben Regeln wie der Mod
- schreibt `vorschau_1.png` und `vorschau_2.png` in die Dungeon-Ordner (rot = zugemauert)

Die alten Ordner `Raeume` (Burg, Apfelhain, Tempel) werden nicht mehr benutzt und können gelöscht werden.

## Totenbeschwörung: unsichtbare Haustierplätze (Stand 2026-10-27)

- **Ursache:** Das Spiel entlässt in jedem Bild jede Begleiter-Kreatur (Feindlichkeit 5), deren GUID nicht im einen Haustierplatz des Besitzers (`+0x11C8`) steht (`0x60EAB1`). Darum fielen die Beschworenen sofort tot um.
- **Lösung:** 16 unsichtbare Haustierplätze, abgefragt über eine Code-Höhle an `0x60EAC7`.
  - Der Skill füllt so viele Plätze, wie der Anzahl-Kasten erlaubt.
  - Nach der Dauer wird der Platz geleert. Dann entlässt das Spiel die Kreatur selbst.
  - Sind alle Plätze belegt, meldet der Skill das im Chat.
- Das echte Haustier bleibt unberührt.

## Totenbeschwörung: Begleiter-Plätze (Stand 2026-10-27, zweiter Umbau)

**So funktioniert es:**
- Jeder Beschworene ist ein echter Begleiter des Spiels:
  - Feindlichkeit 5, Besitzer (+0x198) = Spieler
  - Begleiter-KI aus `cube_world_init_companion` 0x5EE7A0: kämpft und folgt
- Das Spiel kennt pro Spieler nur einen Haustierplatz (`+0x11C8`) und entlässt jeden anderen Begleiter sofort (0x60EAB1).
- Der Mod führt deshalb 16 eigene Begleiter-Plätze (je Besitzer-GUID, Rasse, Level, Ablaufzeit). Die Höhle an 0x60EAC7 lässt jeden Begleiter in einem dieser Plätze bestehen.
- **Taste (5):** Füllt so viele Plätze, wie der Anzahl-Kasten erlaubt, mit den zuletzt besiegten Gegnern.
- **Leeren:** Ist die Dauer um, wird der Platz geleert. Das Spiel entlässt die Kreatur dann selbst. Tote, verschwundene oder vom Spiel umgestellte Begleiter machen ihren Platz sofort frei.
- **Besiegte Gegner:** Zählen, wenn ihr Leben ≤ 0 ist oder wenn sie verschwinden, nachdem sie mindestens 40 % Leben verloren haben. Umkreis 64 Blöcke.

**LAB → Skills:**
- „Beschwören testen (Skelett)“ ruft ohne besiegte Gegner ein Skelett für 60 s in einen Platz.
- „Plätze leeren“ entlässt alle Beschworenen.
- Die Statusliste zeigt jeden belegten Platz mit Rasse, Level und Restzeit.

**Log:** Jeder Schritt steht mit `[Beschwoeren]` im `AdventureMod_log.txt`:
- Taste, Rang, Plätze
- jeder besiegte Gegner
- jeder Spawn mit Feindlichkeit und Besitzer
- jeder freie Platz mit Grund

**Bekannt:** Ist ein Haustier-Gegenstand ausgerüstet, das Haustier aber gerade weg, übernimmt das Spiel den ersten Begleiter als Haustier (0x60E492ff.).

## F9-Anzeige (Stand 2026-10-27)

[F9] zeigt unten rechts:
- Koordinaten und Zone
- das Biom an deiner Position:
  - eigenes Biom ab Gewicht 0,5
  - sonst das native Klima aus Temperatur 0x5C4DD0, Feuchte 0x5C4800, Lava 0x5EF040 und Ozean 0x5F0720: Grasland/Wald, Wüste, Schneeland, Frostland, Dschungel, Sumpfland, Steppe, Lavagebiet, Ozean
- je angehakter Strukturart die nächsten Ziele mit Entfernung und Himmelsrichtung

**Auswahl:** LAB → Reiter „Dungeons & F9“, Haken je Art. Gespeichert als `f9_*` in `AdventureMod_schalter.cfg`.

| Art | Woher |
|---|---|
| Eigene Dungeons (Burg, Apfelhain, …) | beim Setzen mit der Zone angemeldet |
| Kleine Tempel | dto. |
| Eigene Städte | dto. |
| Native Dörfer, native Dungeons, andere native Orte | aus den Ortstabellen der Regionen (auch unentdeckte, 6000 Blöcke Umkreis) |
| Bergdörfer, eigene Biome | aus dem Weltsamen berechnet |

Gebaute Orte werden je Welt in `AdventureMod_orte_<Seed>.txt` gespeichert. Sie erscheinen, sobald ihre Zone einmal erzeugt wurde.

## Native Biome bearbeiten (Stand 2026-10-27)

Unter LAB → „Biome I / II“ stehen die sechs nativen Biome jetzt ganz oben in der Liste, direkt unter „[Welt] Native Welt“: **Grasland, Schneeland, Frostland, Wüste, Dschungel und Lavagebiet**. Jedes wird mit demselben Editor bearbeitet wie ein eigenes Biom: Boden- und Felsfarbe, Gelände, Pflanzen, Kreaturen, Biome II-Tierregeln, Zonen-Modelle, native Orte, Dungeons usw.

**Dateien:** `Strukturen\Biome\Nativ_<Klasse>.biom` mit `nativ=1..6`. Sie entstehen von selbst, falls sie fehlen.
- Die Vorgaben ändern nichts: Deckkraft 0, kein Fels, kein Gelände, keine Pflanzen, keine Kreaturen, alle Orte erlaubt. Nur was du einstellst, wirkt, und zwar in neu erzeugten Zonen.
- `anteil` = % der Klimazellen dieser Klasse (Vorgabe 100).
- „Aktiv“ aus = das native Biom bleibt unverändert.

**Klasse einer Klimazelle** (RE_Biome.md 1.2/1.3, Schwellen wie die Bodenfarbe 0x5C56E0):

| Klasse | Bedingung |
|---|---|
| Lavagebiet | Sonderbit |
| Schneeland | Temperatur < 0,3, Feuchte ≤ 0,7 |
| Frostland | Temperatur < 0,3, Feuchte > 0,7 |
| Wüste | Temperatur > 0,75, Feuchte < 0,2 |
| Dschungel | Temperatur > 0,75, Feuchte > 0,8 |
| Grasland | sonst (gemäßigt) |

**Regeln:**
- Eigene Biome gehen vor. Die nativen gelten nur für Zellen ohne eigenes Biom.
- Klima und Lava der Zelle bleiben nativ.
- Haben die Tierregeln eines nativen Biomes nichts eingestellt, gilt weiter das Profil „Native Welt“.
- Dungeons und Zonen-Modelle mit Gebiet „vanilla“ zählen bearbeitete native Biome als Vanilla.

Höchstzahl der Biom-Dateien: 31 (vorher 15).

## Boot-Platz: zweiter Spezial-Platz im Inventar (Stand 2026-10-27)

- **Platz:** Im Inventarfenster liegt eine Zeile unter dem Spezial-Platz ein zweiter Platz für das Boot. Es ist ein echtes Ausrüstungsfeld des Spiels (Klon der Vorlage „equipmentboxright“ mit eigenem Feld-Element).
- **Linksklick:** tauscht mit dem Gegenstand am Mauszeiger. Nur Boote werden angenommen; mit leerem Mauszeiger nimmst du das Boot heraus.
- **Rechtsklick:** legt das Boot ins Inventar.
- **[G] im Wasser:** Liegt im Spezial-Platz der Gleitschirm und im Boot-Platz das Boot, tauscht der Mod beide und das Spiel startet das Segeln.
- **An Land:** Eine Sekunde nach dem Aussteigen kommt der Gleitschirm von selbst zurück in den Spezial-Platz.
- **Speicherung:** je Charakter in `AdventureMod_bootplatz.txt` im Spielordner. Ohne den Mod ist ein dort abgelegtes Boot nicht sichtbar. Vorher herausnehmen, wenn du ohne Mod spielen willst.
- **Status:** LAB → Skills (Statusliste).

## Stand 2026-10-27 (3): Beschwörung, Speicher, Burg-Ebenen

### Totenbeschwörung: Ursache gefunden
- **Fehler:** Die Kreaturen-Map des Spiels (EntitesMap) sortiert ihre Schlüssel mit Vorzeichen (Vergleich 0x477D90: jg/jl auf dem hohen Wort).
  - Die GUIDs der Mod-Kreaturen (0xCAFECAFE…) sind dadurch negativ.
  - Die Suche des Mods (`FindCreatureRaw`) verglich ohne Vorzeichen und fand die Beschworenen nie.
  - Der Mod hielt sie deshalb für verschwunden, leerte ihren Platz, und das Spiel entließ sie.
- **Behoben:** Die Suche vergleicht jetzt mit Vorzeichen. Das betrifft alle Mod-Kreaturen (Händler, Taxi, Begleiter …).
- **Neu:** Besiegte Gegner eigener Rassen (z. B. Kull) werden über ihre .mob als Begleiter beschworen. Vorher hieß es „Spawn fehlgeschlagen, Rasse 388“.

### Speicher: Datenbanken statt Textdateien
- **Eigene Datenbank `Save/AdventureMod.db`:** gleiches Format wie die Spieldatenbanken (Tabelle `blobs`), geschrieben mit der SQLite des Spiels. Enthält Schalter, Karte & Leistung, gemerkte Orte (F9), Runensteine, Tempel-Funde und Bergdörfer.
- **Charakter-Datenbank des Spiels `Save/characters.db`:** je Charakter die Schlüssel `AdventureMod:<Name>:skillpunkte` und `AdventureMod:<Name>:bootplatz`. Das Spiel liest dort nur seine Nummern-Schlüssel.
- **Alte Dateien:** `AdventureMod_*.txt/.cfg` werden beim ersten Lesen übernommen und in `*.alt` umbenannt (nicht gelöscht). Die `.alt`-Dateien kannst du danach löschen.
- **Bleiben Dateien:** Logs und Absturzberichte, damit sie auch nach einem Absturz lesbar sind, und die Inhalte unter `Strukturen` (Modelle, .biom, .mob, Dungeons).

### Burg: Grundebene, Treppen, Farben je Biom
1. **Grundebene:** Zuerst entsteht die ganze Grundebene. Treppenräume sind Pflicht (mindestens 2).
2. **Obere Ebenen** entstehen nur durch **Aufstocken**:
   - Über einem Treppenraum kommt `treppenraum_mitte` (Bodenloch, die Treppe kommt an, eine zweite Treppe führt weiter).
   - Darüber kommt `treppenraum_dach`.
   - Von dort werden verbundene Gänge und Räume derselben Ebene genau überbaut. Jedes obere Modul liegt also ganz auf einem Raum darunter.
   - Die Ebenen sind nur über Treppen verbunden. Die Galerie im Rittersaal ist entfallen.
   - Ohne Raum darüber führt die Treppe auf das Dach mit Zinnen.
3. **Treppen:** 3 breit, 14 Stufen, über jeder Stufe mindestens 4 Blöcke frei (nachgemessen).
4. **Farben:** Wände, Böden und Decken sind einfarbig ohne Muster. `biomfarben=1` ersetzt die Schlüsselfarben (`farbe_wand/boden/decke`) je Biom:
   - eigenes Biom → seine Dungeon-Farben (Wand/Boden in der .biom)
   - sonst nach nativer Klimaklasse: Grasland grauer Stein, Schneeland hell, Frostland blaugrau, Wüste Sandstein, Dschungel moosgrün, Lavagebiet Basalt
   - Zinnen und die Eingangstreppe bekommen dieselbe Farbe.
5. **Neue Schlüssel:**
   - `dungeon.txt`: `obere=min,max`, `biomfarben`, `farbe_wand/boden/decke`
   - Modul-`.txt`: `ebene=boden|oben`, `pflicht=n`, `oben=<Modul>`

## Stand 2026-10-27 (4): Karte, Teleport-Taste O, Boot-Platz

**Weltkarte (M):**
- Runensteine erscheinen violett mit ihrem Namen („<> Name“), eigene Markierungen gelb („+ Markierung n“).
- **Mittlere Maustaste** auf der Karte setzt eine Markierung an die Stelle. Ein Mittelklick auf eine vorhandene Markierung entfernt sie.
- Gespeichert je Welt in `Save/AdventureMod.db`.
- Die Höhe der Markierung ist die Höhe des Spielers beim Setzen. Auf der gekippten Karte liegt sie deshalb genau unter dem Zeiger, auch wenn das Gelände dort höher oder tiefer ist.

**Teleport auf P** (statt 4; Nachtrag: P statt O, O bleibt dem Systemmenü):
- O öffnet die Steinliste bzw. teleportiert. 6 = nächster Stein, U = Stein ablegen/aufheben.
- Die Tempelkugel nutzt ebenfalls O statt 4.
- Weil O im Spiel das Systemmenü öffnet, schaltet der Mod diese Belegung ab (Tastentabelle 0x47E95C). Das Systemmenü bleibt über den Menüknopf „O“ erreichbar.

**Boot-Platz:** Er ist jetzt ein Klon des Spezial-Kastens, beschriftet mit „Boot“ und ohne das mitkopierte Bild. Vorher kam versehentlich der linke Waffenplatz samt Waffe mit.

**Infinite-Cubes:** siehe `RE/RE_InfiniteCubes.md`.
- **Nachtrag:** Teleport und Tempelkugel liegen auf **P**. P ist im Spiel unbelegt (`GameController::handleKeyDown` hat keinen Fall für P). Die Sperre für O greift nur noch, wenn ein Skill auf O gelegt wird. Das Systemmenü bleibt also auf O.

## Stand 2026-10-27 (5): Karte, Elfenstadt, Werkstätten

**Karte (M):**
- **Rechte Maustaste** setzt bzw. entfernt eine Markierung. Rechtsklick ist auf der Karte im Spiel unbelegt (Infinite-Cubes `GameController::handleMouseButton`).
- Markierungen und Runensteine sind nur noch **Punkte** ohne Text: gelb für eigene Markierungen, violett für Runensteine.
- **Viertelnamen** stehen auf der Karte: Stadt, Händlerviertel, Alchemieviertel, Tempelviertel, Tierviertel, Arbeitsviertel. Sie erscheinen, sobald die Stadt geplant ist, also wenn Zonen in ihrer Nähe entstanden sind.

**Elfenstadt – Händlerwege:**
- Alle Händlerstraßen sind **10 Blöcke breit** (vorher 5):
  - von jedem Viertel mit Händlern (Handel, Alchemie, Arbeit, Tempel) zum Platz am Mutterbaum
  - von Händler zu Händler (nach Richtung um die Stadtmitte verkettet, bis 380 Blöcke)
  - von jedem Ladenbaum zum nächsten Weg
- Große Bäume halten Abstand zu diesen Straßen.
- Gilt für neu geplante Städte, also in einer neuen Welt oder Gegend.

**Werkstätten (Schmiede, Zimmerei, Weberei):**
- **Ursache der Fehler:** Das native Modell wurde mit Kultur 0x0D..0x0F gestempelt. Dabei entstand es teils nur halb und schleuderte Spieler beim Durchlaufen hoch.
- **Jetzt:** Die Werkstätten sind **Nachbauten** des Originals (`Strukturen\Stadt\Werkstatt_Schmiede/Zimmerei/Weberei.cub`, erzeugt mit `Werkzeuge\Stadt\werkstaetten.py` aus data1.db):
  - Die Markerfarben sind Luft: Türen und Fenster bleiben offen.
  - Die Stationen stehen in der `.txt` daneben (Ofen, Amboss, Säge, Werkbank, Spinnrad, Webstuhl, Anpassungsbank). Die Stadt setzt sie als native Objekte, gedreht mit dem Gebäude.
  - Die Höhe ist wie im Spiel: Ursprung = Bodenoberkante − 7, also Boden der Werkstatt = Gelände.
- Die Modelle sind im LAB anpassbar.

## Stand 2026-10-27 (6): Tiere, Tier-Werkstatt, Flugtier-KI, Fenster im Spielstil

**Tiere richtig zusammengesetzt.** Der Zusammenbau ist 1:1 nach Infinite-Cubes `Animation.cpp` (Ruhehaltung) nachgerechnet und an nativen Tieren geprüft (Boden = Mitte der Kollisionshöhe, Abweichung < 1 Einheit). Alle 178 Tiere (`Mobs\Tiere`, `Mobs\Tierwelt`) sind ausgerichtet: Beine auf dem Boden und leicht im Körper, Vorderbeine vorne, Arme seitlich, Kopf am Körper, Schwanz hinten, Flügel an der Seite. Positionen und Skalen stehen jetzt fest in der `.mob` (Pos = 1).
- Werkzeug: `Projekt\Werkzeuge\Mobs\tierbild.py <Strukturen>\Mobs --ausrichten --schreiben --bilder [--nur name1,name2]` (Bilder davor/danach in `tierbilder\`).
- Neue Köpfe mit kurzem Gesicht und Hals (`kopfbau.py`, Aufruf `koepfe_neu.py`): Giraffe, Rentier, Elch, Reh, Hirsch, Hirschkuh, Antilope, Zebra, Einhorn, Pegasus, alle Drachen, Wyvern, Greif, Phönix, Federschlange (`.mob`-Schlüssel `hals=1`).
- Drachen Faktor 3,0 (vorher 1,4), Wyvern 3,2 (vorher 2,0); Drachen haben Vorderbeine.
- Die Schwanzposition wird jetzt ins Spiel übernommen (Aussehen +0x10C, vorher ignoriert).

**LAB → Reiter „Tier-Werkstatt“.** Links alle `.mob`, Mitte das Tier, wie das Spiel es zeichnet (Ziehen = drehen, Mausrad = Zoom, Gitter = Boden). Rechts Teil wählen, X/Y/Z/Skala mit −/+ verschieben (Schritt einstellbar), Faktor, „Speichern (.mob)“ (ändert nur `teil=`- und `faktor=`-Zeilen), „Testen: 40 Blöcke vor mir spawnen“. Native Teile liest das Spiel nach (dafür im Spiel sein).

**Flugtier-KI (eigene Tiere, Rasse ≥ 200, feindlich).** Steigen seitlich über den Spieler → Sturzflug auf den Spieler (Angriff wie der native Flieger-Biss) → abdrehen → wieder steigen; ab und zu landen und am Boden mit Sprungangriffen kämpfen, danach wieder auffliegen. Kein Schweben mehr über dem Spieler. Kleine Landtiere (Breite < 0,85 Blöcke) greifen mit Sprüngen an (nah: Biss).

**Fenster im Spielstil.** Runenstein benennen: Feld wie die Welterstellung (Rahmen = Spielvorlage „blackwidget“, Eingabe = Spielfeld „edit“). Tippen, Enter = ablegen (leer = Vorschlag), Esc = abbrechen; solange getippt wird, laufen keine Spiel- und Mod-Tasten und der Spieler bleibt stehen. Steinauswahl: gleiches Fenster mit Liste (6 = weiter, P = teleportieren – Tasten aus `Teleport.skill`, Esc = schließen). Fehlen die Vorlagen, kommt das alte Fenster.

## Stand 2026-10-27 (7): Eingabe, Auswahlfenster, weiche Biomübergänge
- **Runenstein-Name eintippbar:** Das geklonte Eingabefeld des Spiels war ohne Textform (plasma: Klon setzt `m_textShape = 0`). Jetzt wird es wie beim Spielstart initialisiert (`Node::initializeWidgets` 0x635700) – Tippen, Rücktaste, Pfeile, Enter funktionieren.
- **Fenster größer:** Auswahl mindestens 560 px breit (breiter bei langen Namen), 34 px je Zeile; Eingabefenster 520 × 160 px.
- **Biomübergänge:** Für die Höhe und die nativen Berge gilt ein eigenes, ~5× breiteres Gewicht (~550 Blöcke Übergang statt ~120; bei 150 Blöcken Höhenunterschied Steigung ≈ 0,4 statt > 2). Zwischen zwei eigenen Biomen werden beide Formen gemischt (vorher Sprung um die halbe Höhendifferenz). Seen und Flüsse nur, wo das Biom ≥ 97 % Anteil hat. Farben, Orte und Mobs behalten die Grenze des Spiels. Wirkt auf neu erzeugte Zonen.

## Stand 2026-10-27 (8): Beine im Gang, Werkstätten, Bergtempel
- **Tierbeine:** Das Spiel schwingt jedes Bein um den Modellursprung (Animation.cpp: `rotate(Schwung)` vor der Verschiebung, dann noch einmal um die Beinmitte) – darum lösten sich lange Beine beim Laufen vom Körper. `tierbild.py --ausrichten` rechnet jetzt den ganzen Schritt nach (Gehamplitude 0,21 wie im Spiel bei 60 Bildern/s) und wählt Beinhöhe + Körperhöhe so, dass die Oberkante immer im Körper bleibt (111 von 113 Lauftieren ohne Lücke, Rest < 1 Einheit). Sichtbare Beinlänge 35–50 % der Bodentiefe wie bei den nativen Tieren; eigene Bein-Modelle werden in der Höhe neu abgetastet (schlank, höchstens 32 % der Körperbreite). Originale bleiben als `*.orig.cub` liegen. Längs stehen die Beine höchstens ~0,8 × Bodentiefe von der Mitte, Zweibeiner unter dem Drehpunkt.
- **Werkstätten Elfendorf:** 1 Block höher (`kWorkZoff` −6 statt −7), Stationen wandern mit. Gilt für neu erzeugte Zonen.
- **Bergtempel** (`Strukturen\Dungeons\Bergtempel`, Generator `Werkzeuge\Dungeons\bergtempel_bau.py`): Baukasten wie die Burg (Grundebene zuerst, 5–12 obere Module nur über Treppenräume), aber `lage=berg`: nur wo das Gelände über dem ganzen Grundriss mindestens `felsdecke=4` Blöcke über dem höchsten Raum liegt (an `berg_anteil=75` % der Messpunkte), Eingang (Felsportal mit Wächterstatuen) am Hangfuß; davor gräbt der Mod einen Stollen ins Freie. Räume: Säulenhalle (2 Ebenen), Wasserhalle mit Kreuzbrücke, Grabkammer, Opferraum, Wachraum, Schriftenkammer, Schatzkammer; Boss im Götzensaal oder Allerheiligsten. Alle Module mit Decke (nur so höhlt das Spiel den Fels aus). Prüfung: 300/300 Grundrisse, 25–42 Module. LAB-Test („Testbau“) sucht den nächsten passenden Berg im Umkreis von 160 Blöcken.

## Stand 2026-10-27 (9): Hochelfenkönig, Teleport-Freischaltung, Partikel
- **Hochelfenkönig:** In der Säulenhalle oben im Mutterbaum steht ein Thron (Podest, roter Läufer zur Hallenmitte, silberne Armlehnen, hohe Lehne mit Goldkrone), der Treppenankunft gegenüber. Der König (`Strukturen\Mobs\hochelfenkoenig.mob`, Kopf = `Strukturen\elfking.cub`, Kopie in `Teile\Elfen`) sitzt darauf wie auf einem Hocker des Spiels (Sitz-Fähigkeit 83, je Bild gehalten), blickt zur Hallenmitte. Der Thron ist Teil des Mutterbaum-Modells → in schon erzeugten Elfenstädten fehlt er, bis die Zone neu entsteht.
- **Sprechblase:** Spricht man ihn an, ersetzt der Mod den Text der nativen Sprechblase durch die Zeilen `sprechblase=` aus der `.mob` (eine Zeile = eine Seite). Dabei wird der Teleport für diesen Charakter freigeschaltet (Charakter-DB `freigeschaltet_hochelfenkoenig`), mit goldenem Funkenregen.
- **Teleport-Skill:** `freischalten=hochelfenkoenig` + `beschreibung=` (Teleport.skill wird beim Laden auf Stand 4 gebracht). Vorher: Chatmeldung „noch verschlossen – sprich zuerst mit dem Hochelfenkönig …“. Im Skillfenster (X) zeigt die Maus über Platz 9/10 die Beschreibung (+ „[noch verschlossen]“).
- **Partikel:** über das Partikelsystem des Spiels (Liste GC+0x800740). Trankwirkung: Funkenstoß beim Einsetzen, solange sie wirkt Funkeln in ihrer Farbe (Regeneration grün, Stärke rot, Schutz blau, Tempo gelb, Federfall weiß, Nachtsicht violett, Mana blau, Ausdauer orange). Beschwörung: schwarzer Stoß mit etwas Violett + aufsteigende dunkle Spirale (1,8 s); beim Verschwinden/Wegschicken schwarze Wolke.

## Stand 2026-10-29: Tiere zurück + eigene Gelenke + Tier-Werkstatt mit Bewegung und Datei-API
- **Beine zurück:** Die Bein-Modelle sind wieder die Originalformen der Generatoren (`Werkzeuge\Mobs\beine_neu.py` schreibt nur Beine neu), Beinskalen wieder die der Basisrasse, Ausrichtung wie vor der Gang-Änderung.
- **Eigene Gelenke je Tier:** Jede `.mob` (113 laufende Tiere) hat `gelenk=<teil>;x;y;z` (Hüfte = Fuß-Teil 3, Schulter = Hand-Teil 2 bei Vierbeinern; Voxel der Originalform `*.orig.cub`) und `gelenk_verlaengerung=n`. Das Bein-Modell ist so aufgefüllt, dass seine Mitte im Gelenk liegt, und oben um n Lagen verlängert (steckt unsichtbar im Körper). **Im Spiel** biegt `TierGelenke.cpp` vier Aufrufe in der Tier-Zeichnung um (0x423059/0x423405 Vorderbeine, 0x423957/0x423B83 Füße → `Matrix::rotate` 0x4241B0): für Rassen mit Gelenken entfällt die Drehung um die Modellmitte, die Beine schwingen nur noch ums Gelenk. Nachgerechnet: in einem ganzen Schritt bleibt bei allen 113 Tieren die Beinoberkante im Körper (vorher 110 mit Lücke).
- **LAB → Tier-Werkstatt:** unten Bewegung wählen (Ruhe, Gehen, Biss, Sturzbiss, Kopfstoß, Stampfen, Hornangriff), „abspielen“ oder Zeit eintippen; rote Punkte = Gelenke. Rechts unter den Teilwerten: Gelenk Y/Z für Füße/Vorderbeine + „anwenden“ (füllt das Bein neu auf, Position bleibt), dann „Speichern“. „Bild -> LabApi\bilder“ speichert das aktuelle Bild.
- **Datei-API** (für Claude/Skripte, läuft solange das Spiel mit LAB läuft): Datei `Strukturen\LabApi\auftrag_<irgendwas>.txt` mit
  `tier=giraffe` · `art=gehen` · `zeiten=0,260,520,780` · `drehung=-35` · `neigung=22` · `groesse=420` · `bild=giraffe_gehen.png`
  → Bild in `Strukturen\LabApi\bilder\`, Zeile in `antwort.txt`, Auftrag wird zu `.erledigt`.
- **Ohne Spiel:** `Werkzeuge\Mobs\tierpose.py <Strukturen>\Mobs <tier> <art> <zeiten> <bild.png>` – dieselbe Rechnung (Quelle Infinite-Cubes `Animation.cpp`: Gehzyklus, Angriffshaltungen mit Ausholen/Schlag/Halten, Monster-Tempo 0,75, Blenden je ms).

## Stand 2026-10-29 (2): Thronsaal mit Tisch, englische Sprechblasen, Tempel am Weltspawn

- **Thronsaal (Mutterbaum, CityModels::MotherTree):** Thron bleibt hinten. Davor ein Tisch (radial r 3.8..5.2, quer ±3, Beine Ht+2, Platte Ht+3 mit Goldrand, zwei Kerzen, rotes Kissen). Das Podest reicht jetzt bis vor den Tisch (r ab 2.5). Der König steht zwischen Tisch und Thron (`CityModels::MotherKingStand`, r = 6.5, Blick zur Hallenmitte).
- **Sprechblase König** (`Strukturen\Mobs\hochelfenkoenig.mob`):
  - `sprechblase=` ist das erste Gespräch und schaltet den Teleport frei.
  - `sprechblase_danach=` gilt für jedes weitere Gespräch ("Use it wisely.").
  - Hat das Spiel weniger Seiten als der Text, wird der Rest an die letzte Seite angehängt.
  - Ob schon ersetzt wurde, prüft der Mod jetzt am Inhalt der ersten Seite, nicht mehr an der Speicheradresse.
- **Tempel am Weltspawn:**
  - Weltspawn = World+0x8000F0, fest (8396928, 8396928). Quelle: Infinite-Cubes World.cpp:527, im Original geprüft bei 0x5A55F8 und 0x593A17.
  - Der Elfen-Tempel (`Strukturen\Stadt\Tempel_Elfen.cub`, ohne Kugel) steht 36 Blöcke nördlich (+y) des Spawns, auf dem höchsten von 5×5 Messpunkten, mit Fundament.
  - In der Mitte steht die Tempelhüterin (`Strukturen\Mobs\tempelhueter.mob`, Text unter `sprechblase=`, Englisch). Sie blickt zum Spawn.
  - Ablauf: `BiomeMod::ZoneSpawnTemple` → `City::SpawnTempleZone`, NPC über `City::Tick` → `KeeperTick`.
- Bereits erzeugte Zonen zeigen Änderungen erst nach einem Neuladen der Welt bzw. der Zone.

## Stand 2026-10-29 (3): Sprechblasen mit Umbruch, Spieltasten nur im Spiel

- **Umbruch in Sprechblasen:**
  - Das Spiel bricht nur zwischen Phrasen um und legt je Wort eine Phrase an (SpeechWidget::drawPhrase).
  - Der Mod zerlegt die Texte jetzt ebenfalls in Wörter, ein Listenknoten je Wort. Die Knoten werden mit dem operator new des Spiels angelegt (0x68D668), damit das Spiel sie selbst wieder freigeben kann.
  - Seiten mit mehr als 92 Zeichen werden automatisch auf mehrere Seiten verteilt. Hat das Spiel weniger Seiten, wird der Seitenvektor vergrößert.
  - In den `.mob`-Dateien darf eine `sprechblase=`-Zeile also beliebig lang sein.
- **Spieltasten nur im laufenden Spiel** (`NativeUi::PlayInput`, je Bild):
  - Gesperrt, solange einer der Menü-Bildschirme sichtbar ist (Welt erstellen, Weltliste, Charakter erstellen, Charakterliste, Verbinden), ein Eingabefeld den Fokus hat oder die Chatzeile offen ist. Das ist dieselbe Prüfung wie in GameController::handleKeyDown 0x47E1B0.
  - Ebenfalls gesperrt, solange die Zone des Spielers noch nicht erzeugt ist, das Spielfenster nicht vorn ist, das LAB vorn ist oder eine eigene Eingabe offen ist.
  - Gilt für alle Skills (Teleport, Beschwören), die Tempel-Zielliste, die Stadt-Kugel [R] und die Strg-Tasten in main.cpp.
  - Nur F9 und F10 gehen weiterhin auch in Menüs.

## Stand 2026-10-29 (4): Runenstein-Name neu gebaut

- Das bisherige Eingabefenster nutzte einen Klon des Weltnamen-Felds und hat die Chatzeile gesperrt. Im Spiel kam dabei kein Zeichen an, sichtbar war nur die leere Chatzeile.
- **Neu (NativeUi):**
  - **Tasten:** Ein eigener Fenster-Hook nimmt WM_CHAR und WM_KEYDOWN, solange getippt wird. Enter legt den Stein ab, Esc bricht ab, die Rücktaste löscht. Das Spiel bekommt diese Tasten nicht, also öffnet sich weder Chat noch Menü.
  - **Spieltasten aus:** Ein DirectInput-Hook auf GetDeviceState leert während der Eingabe die Tastatur des Spiels, dadurch laufen, angreifen und interagieren nicht mit. Der Aufruf im Original liegt bei 0x4C91E9 (push 0x100, call [ecx+0x24]).
  - **Zusätzlich:** Ein Fokus-Feld ist gesetzt, damit moveCharacter (0x4A6B50) sofort zurückkehrt.
  - **Anzeige:** Ein eigener Kasten in der Bildmitte (Goldrand, Titel, Eingabezeile mit blinkendem Strich, Hinweis), gezeichnet in EndScene über `NativeUi::DrawInput`.
- Im Log stehen beim ersten Benennen `[UI] Tastatur-Hook ... ok` und `[UI] DirectInput-Hook ... ok`.

## Stand 2026-10-29 (5): Eingabefenster wieder im Spielstil, Veröffentlichung abgesichert

- Der Runenstein-Name erscheint wieder im Fenster aus den Spielvorlagen: dunkles Feld, Titelzeile, Eingabefeld und Hinweis.
  - Getippt wird über den Fenster-Hook. Der Text steht im Feld, dahinter blinkt ein Strich.
  - Den Ersatzkasten (D3DX) gibt es nur noch, wenn die Spielvorlagen fehlen.
- Die Datei-API ist nur aktiv, wenn `Strukturen\LabApi` existiert. Für Claude-Aufträge den Ordner einmal selbst anlegen.
- Veröffentlichung: siehe VEROEFFENTLICHUNG.md, Abschnitt Sicherheitsprüfung.

## Stand 2026-10-29 (6): Beschwörer, Weg zum König, Tempel und Dungeons

- **Beschworene sind nicht mehr das Haustier:**
  - Das Spiel trägt jede Kreatur mit Feindlichkeit 5 jedes Bild als Haustier des Besitzers ein (+0x11C8, Code bei 0x60DE42..0x60DE76). Dadurch übernahm dein Begleiter Level, Anzeige und Reittier eines Beschworenen.
  - Fix: Ein Patch bei 0x60DE5E überspringt Kreaturen, die in einem Beschwörungs-Platz stehen.
- **Reiten:**
  - Solange der Spieler reitet (Fähigkeit 106), stellt das Spiel jede Kreatur mit seinem Besitzer unter ihn.
  - Fix: Beschworene verlieren während des Reitens ihren Besitzer (+0x198 = 0) und bekommen ihn danach zurück. Sie folgen trotzdem weiter.
  - Läuft ihre Dauer ab, bekommen sie den Besitzer zurück, damit das Spiel sie wie gewohnt entlässt.
- **Große Tiere** werden nicht beschworen. Als groß gilt: Breite ab 2.0 oder Höhe ab 3.6 Blöcken (Mensch 0.8 × 1.8, Oger 1.8 × 3.0 geht noch).
- **Zum Hochelfenkönig (`City::VisitKing`):**
  - Die nächste Elfenstadt wird aus dem Seed berechnet (`BiomeMod::NearestElfCity`, Zellen über SeedScout).
  - Der Spieler kommt zuerst hoch neben den Mutterbaum. Sobald die Stadt geladen ist, landet er im Thronsaal auf dem Läufer vor dem König.
  - Ausgelöst wird das beim 25. gefundenen Tempel und über die Stadttempel-Kugel (erstes Ziel „Hochelfenkoenig“).
- **Kleine Tempel:**
  - Nur noch etwa 30 % der Rasterfelder bekommen einen.
  - Neu verworfen werden Plätze in der Nähe nativer Orte und Plätze, unter deren Grundfläche (alle 3 Blöcke geprüft) Bauwerk, Baum, Laub, Weg oder Wasser liegt.
- **Dungeons:** dieselbe Grundflächen-Prüfung.

## Stand 2026-10-29 (7): König über jede Kugel ab 25, Grundeinstellungen

- **Zum Hochelfenkönig:**
  - Ab 25 gefundenen Tempeln bringt jede Kugel eines kleinen Tempels (R) dich sofort zum König. Das gilt auch für schon bekannte Tempel.
  - Die Suche nach der Elfenstadt läuft in einem eigenen Thread über die ganze Welt (Ringsuche bis 1023 Zellen). So wird die Stadt auch gefunden, wenn Elfenbiome nur 1 % ausmachen.
  - Ablauf: erst hoch neben den Mutterbaum, dann in den Thronsaal vor den König.
  - Im Log stehen die `[Stadt] Weg zum Koenig ...`-Zeilen.
- **Grundeinstellungen (Auslieferung):**
  - Die Vorlagen liegen in `Strukturen\Grundeinstellungen\AdventureMod_schalter.cfg` und `AdventureMod_tweaks.cfg`.
  - Bei einer neuen Installation, solange es die Einstellung noch nicht gibt, übernimmt ModDb die Vorlage in `Save\AdventureMod.db`.
  - Ausgeliefert: `tierdichte=4` (Spawn 6×6, „sehr viel“), `tierwelt_anteil=50`, `vier_gb=1`, Karte & Leistung wie im LAB eingestellt.
- **LAB → Mehrspieler/Shader:**
  - Häkchen „4 GB Adressraum“.
  - Knopf „Als Grundeinstellung speichern“ schreibt die aktuellen Werte in die Vorlagen.
  - Knopf „Grundeinstellungen laden“ setzt die Vorlagen zurück. Tierdichte und Anteil gelten sofort, der Rest ab dem nächsten Start.
- **4 GB automatisch:**
  - Steht `vier_gb=1` und hat die Cube.exe das LAA-Bit noch nicht, startet der Mod einen unsichtbaren Helfer (`rundll32 AdventureMod.dll,LabLaaHelper <pid>`).
  - Der Helfer setzt das Bit nach dem Beenden des Spiels (Sicherung `Cube.exe.vor-4GB.bak`) und schreibt das Ergebnis nach `AdventureMod_4gb.txt`.
  - 4 GB gelten also ab dem zweiten Start.

## Stand 2026-10-29 (8): Ohne D3DX, Schrift des Spiels

- **HUD-Texte:** `NativeUi::HudBegin`/`HudText`/`HudEnd` je Bild. Zeilen sind Klone des Weltnamen-Felds (resource1.dat, 12 px, Kontur), höchstens 64 Zeilen. Links- und Rechtsbündigkeit werden mit 7 px je Zeichen geschätzt.
- **Genutzt von:** F9-Anzeige, Kartenbeschriftungen der Viertel, Skill-Beschreibung im Skillfenster (Umbruch nach etwa 48 Zeichen) und der Ersatz-Steinliste.
- Die Tempelkugel-Liste nutzt jetzt das Listenfenster des Spiels (`NativeUi::ShowList`).
- **Entfernt:** Shader (PostFx → `Projekt\Entfernt`), Skill-Leiste, Effekt-Kästchen, D3DX-Ersatzkasten der Namenseingabe. Ohne Spielvorlagen nimmt Skills das Windows-Fenster.
- **LAB:** Der Reiter „Netz & Shader“ heißt jetzt „Netz & Tiere“.

## Stand 2026-10-29 (9): F9 versetzt, native Skillbeschreibung und Hotbar

- **F9-Anzeige:** 115 px (ca. 3 cm) weiter links.
- **Skillbeschreibung:**
  - Zeigt man im Skillfenster auf Teleport oder Beschwören (Platz 9/10), erscheint sie im Vorschaufeld des Spiels.
  - Technik: Hook auf `writeSkillInfo` 0x4A5710. Er schreibt Name (gold, 12 px), „Taste X – Rang a / b“, die Beschreibung (umgebrochen) und gegebenenfalls „Noch verschlossen“ (rot), mit der Spielschrift (getFont 0x639800, renderText 0x65BC70).
- **Hotbar:**
  - Teleport und Beschwören stehen als echte Knöpfe der Fähigkeitenleiste rechts neben Q (Klone von Knopf 1, Vektor GC+0x8007FC).
  - Jeder Knopf zeigt das Skillfenster-Symbol, die Taste, die Abklingzeit als Abdeckung wie bei den nativen Knöpfen und ist ausgegraut, wenn der Skill verschlossen ist, Mana fehlt oder er noch abklingt.
  - Ein Knopf ist nur sichtbar, wenn der Skill mindestens Rang 1 hat.

## Stand 2026-10-29 (10): Keine feindlichen Wesen in Städten

- **Bei der Zonenerzeugung** (`City::ScrubZoneSpawns`, aufgerufen aus Hook_SunCall nach den Bauten):
  - Entfernt werden alle Spawn-Datensätze mit Feindlichkeit 1, die von sich aus angreifen, innerhalb von Stadtradius + 40 Blöcken. Ob ein Wesen angreift, entscheidet `creature_isPassive` 0x444680 mit den Mod-Marken über `MobLab::PassiveOf`.
  - Die Datensätze werden sauber freigegeben (virtueller Destruktor), die Basisanzahl Zone+0xA0 wird angepasst.
- **Im Spiel** (`CityGuardTick`, jede Sekunde in Städten bis 400 Blöcke um den Spieler):
  - Feindliche Wesen ohne Besitzer, die trotzdem auftauchen (wandernde Gruppen, Tierwelt, aus Nachbarzonen), werden 6000 Blöcke weit weg gesetzt. Das Spiel entlässt sie dort selbst.
  - Friedliche Tiere (Huhn, Gehege-Tiere), NPCs, Haustiere und Beschworene bleiben.
- Im Log zählt die Zeile `[Stadt] Feindliche Wesen ferngehalten`, wie viele Wesen entfernt wurden.
