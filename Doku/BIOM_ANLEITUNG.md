# Biom-Werkstatt: eigene Biome anlegen, registrieren und füllen

**Kurz:** Im LAB öffnet **„Biome …“** die Biom-Werkstatt. Dort legst du ein Biom an: Farben, Klima, Kreaturen, eigene Mobs, Pflanzen und Dungeons. Für jedes Biom schaltest du einzeln, ob native Dörfer, Ruinen, Portale, Dungeons und das Bergdorf dort entstehen dürfen. **„Speichern + laden“** registriert das Biom sofort. Es erscheint in allen Gebieten, die du danach neu entdeckst. Bereits besuchte Gebiete bleiben, wie sie sind.

Technischer Hintergrund und alle Adressen: `RE_Biome.md`. Code: `BiomeMod.cpp` (Spiel) und `BiomeGen.cpp` (Generatoren).

---

## 1. Schnellstart

1. Im LAB **„Biome …“** öffnen.
2. Links eine Vorlage wählen (**Sumpf**, **Kristallwüste**, **Pilzwald**) oder **„Neu“** klicken.
3. **„aktiv“** anhaken. Zum Testen den **Anteil** hoch setzen, z. B. 100 %. Dann wird jede passende Landzelle zu diesem Biom.
4. **„Speichern + laden“** klicken.
5. Neues Gebiet erkunden. Eine Klimazelle ist 16384 Blöcke groß, du musst also weit laufen oder fliegen. **„Nächstes Biom suchen“** nennt Richtung und Entfernung.

Pflanzen und Dungeons kannst du **vorher** ansehen:

- **„→ LAB-Editor“** zeigt sie im Editor.
- **„Test +40“** setzt sie 40 Blöcke vor dich, wie alle Testplatzierungen.

---

### Biom sofort hier testen: „Biom HIER erzwingen“

**Gleicher Ort, gleiches Biom (seit 2026-10-07):** Ob eine Zelle ein Biom bekommt, würfelt jedes Biom mit einem Salz aus seinem **Dateinamen** (vorher: Platz in der alphabetischen Liste aller Biome). Eine neue, gelöschte oder umbenannte Biomdatei verschiebt also die anderen Biome nicht mehr. Gewinnen mehrere Biome dieselbe Zelle, entscheidet ein zweiter Wurf – ebenfalls unabhängig von Reihenfolge und Anzahl. Einmalig liegen die Biome bestehender Welten dadurch an anderen Orten als vor diesem Stand; danach bleiben sie fest, solange Name, Anteil, Klima und „aktiv“ eines Bioms gleich bleiben.

**Der schnellste Weg, ein Biom zu sehen:** Am Ort, an dem du stehst, „Biom HIER erzwingen“ drücken. Das funktioniert auch in der Startzelle, und ohne den Anteil zu ändern.

1. Biom in der Liste wählen. Es muss **aktiv** und gespeichert sein.
2. **„Biom HIER erzwingen“** klicken. Die Klimazelle, die an deiner Stelle das Biom bestimmt, wird fest auf dieses Biom gesetzt. Das ist gespeichert je Welt in `Strukturen\Biome\_erzwungen.txt`.
3. Die Zonen um dich sind schon gebaut und ändern sich erst, wenn das Spiel sie neu erzeugt. Dafür gibt es zwei Wege:
   - Welt verlassen und neu betreten: Dann ist die ganze Zelle von etwa 16384 × 16384 Blöcken das Biom.
   - Gut 1000 Blöcke weggehen und zurückkommen.
4. **„aufheben“** an derselben Stelle gibt die Zelle wieder frei.

## 2. Das Fenster

| Spalte | Inhalt |
|---|---|
| **1 Biome** | Liste der `.biom`-Dateien (`[an]`/`[aus]`, Anteil, Klima). Knöpfe: Neu, Speichern + laden, Löschen, Vorlagen, „Alle Biome AUS“ (Vanilla), Zustand im Spiel, Suche, Teleport |
| **2 Auswahl, Boden, Kreaturen** | Name, Anteil %, passende Zellen, Klima setzen; Bodenfarben A/B, Bodentyp, Deckkraft, Felsfarbe; Rassen der nativen Spawns; eigene `.mob`-Mobs |
| **3 Pflanzen, Dungeons** | Dichte, Gewicht je Art (Baum, Busch, Pilz, Kristall, Kaktus, Halme), Farben, Größe, eigene `.cub`; Dungeon-Anteil, Räume, Farben, Dach, Verfall, Besatzung, Boss |
| **4 Native Orte** | je Ortstyp ein Haken: bleibt im Biom oder nicht. Dazu das Bergdorf |

**„…“** neben einer RGB-Zeile öffnet die Farbwahl. **„+“** hängt die gewählte Rasse an die Liste an.

---

## 3. Was im Spiel passiert

| Schritt | Wann | Wie |
|---|---|---|
| **Registrieren** | wenn eine Klimazelle zum ersten Mal angelegt wird | Hook auf `0x5D7A70`. Die Zelle bekommt ein Biom, wenn sie passt (Land, gewähltes Klima) und der Würfel aus Zelle und Weltsamen unter dem Anteil liegt. Dieselbe Welt ergibt immer dieselben Biome. |
| **Grenzen** | bei jeder Abfrage | Nachbau der nativen Lava-Gewichtung (`0x5EF040`), mit derselben Verschiebung. Der Übergang ist etwa 120 Blöcke breit. Die Grenze verläuft wellig, weil sie um bis zu ±768 Blöcke verschoben wird. |
| **Boden und Fels** | Zonenbau | Hooks `0x5C56E0` und `0x5C7140` mischen deine Farben ein. Ab Gewicht 0,5 wird der Bodentyp gesetzt. |
| **Kreaturen** | Zonenbau | Hook `0x5F56C0`: Ein Anteil der Landspawns bekommt eine Rasse aus deiner Liste. Ausrüstung und Flags vergibt danach das Spiel selbst. |
| **Native Orte** | wenn eine Region angelegt wird | Hook `0x5DA280`: Ausgeschaltete Ortstypen bekommen im Biom Typ 0. Das Spiel baut dort dann nichts. |
| **Bergdorf** | Bergdorf-Raster | Ist es abgeschaltet, entsteht in Zonen dieses Bioms kein Bergdorf. Das Raster selbst wird nicht verschoben. |
| **Pflanzen und Dungeons** | nach jeder Zonengenerierung, auf dem Spielthread, eine Zone nach der anderen, höchstens etwa 3 ms je Bild, im Umkreis von 900 Blöcken | Per `placeStructure` mit Schattenmodell, wie die LAB-Platzierung. Bei jedem Neuladen der Zone entstehen sie neu und gleich. |
| **Eigene Mobs** | alle 1 s in Spielernähe | bis zu „Max. nah“ Kreaturen aus deinen `.mob`-Dateien, 20–40 Blöcke um dich, nur im Biom |
| **Dungeon-Besatzung** | wenn du einem Dungeon näher als 56 Blöcke kommst | Rassen und `.mob`-Dateien auf die Räume verteilt. Der Boss ist 1,8-mal so groß und steht im entferntesten Raum. Einmal je Sitzung. |

Ohne aktives Biom ist alles bitgleich Vanilla, denn jeder Hook ruft zuerst das Original.

---

## 4. Native Orte (Spalte 4)

> **Wichtig:** Diese Haken betreffen nur die Orte des Spiels.
>
> - **Eigene Dungeons des Biom-Mods:** Sie werden über „Dungeons → Anteil % Zone“ gesteuert, 0 = keine. Die Sumpf-Vorlage hat 5 %.
> - **Bergdorf:** Es hat seinen eigenen Haken.
> - **Wann es wirkt:** Änderungen gelten sofort auch für bereits angelegte Regionen. Schon gebaute Zonen ändern sich aber erst, wenn das Spiel sie neu erzeugt: weggehen und zurückkommen, oder die Welt neu betreten.

| Typ | Ort |
|---|---|
| 1 | Dorf (Stadt mit Vierteln) |
| 5 | Ruinen |
| 10 | Portal |
| 14 | großer Dungeon / Palast |
| 2, 3, 4, 6, 7, 8, 11, 12, 15 | Berg-, Wald-, See-, Schlucht-, Tal-, Fels- und Baum-Dungeons |
| 9, 13 | im Spiel nicht beobachtet |

Die Namen stammen aus `SeedScout.h` und wurden im Spiel geprüft. **„Alle Dungeons aus“** nimmt alle Haken außer Dorf, Ruinen und Portal.

---

## 4b. Gelände (Spalte 4 unten)

**Mit „Gelände formen“ verändert das Biom die Form der Landschaft selbst, nicht nur die Farbe der obersten Blöcke.** Es macht das Land flach, legt Wasserflächen an und setzt Hügel. Native Bäume lassen sich ausdünnen.

| Feld | Wirkung |
|---|---|
| Grundhöhe | mittlere Höhe in Blöcken. **Meeresspiegel ist 0: Alles unter Höhe 1 füllt das Spiel mit Wasser.** |
| Wellen ± / Wellen-Länge | kleine Unebenheiten (Höhe, Wellenlänge in Blöcken). Grundhöhe 1 mit Wellen 3 ergibt etwa die Hälfte Wasser, also Tümpel und Inseln. |
| Glätten 0–1 | wie stark die native Höhe durch die Biomform ersetzt wird (1 = ganz) |
| Hügel-Anteil / -Höhe / -Größe | Anteil der Rasterfelder mit einem Hügel, Hügelhöhe, Rastergröße in Blöcken. Die Hügel stehen einzeln. |
| Berge 0–1 | 0 = native Berge und Klippen verschwinden, 1 = sie bleiben. **Bis 0,1 gilt ganz aus.** Native Berge bringen ein Netz schmaler Felsrinnen mit; ein kleiner Rest davon zeigt sich als 1 Block tiefe Furchen. Die Sumpf-Vorlage hat jetzt 0. |
| Native Bäume % | wie viele der normalen Bäume im Biom bleiben (0 = keine). Riesenbäume von Orten und Dorfbäume bleiben unberührt. |
| native Flüsse / Breite × | Die Flussläufe des Spiels bleiben (an/aus) und werden um den Faktor 0,5–4 breiter. 1 = normal, etwa 12 Blöcke. |
| eigene Flüsse / Abstand | zusätzliche gewundene Flüsse mit dem angegebenen mittleren Abstand in Blöcken |
| Seen-Anteil / Seen-Größe | Anteil der Rasterfelder mit See (0–1) und Seedurchmesser in Blöcken. Der Seeboden wird eingeebnet. Der Wasserspiegel liegt auf der Höhe des niedrigsten Uferblocks, kein Ufer liegt tiefer als das Wasser. |

**Flüsse (Stand 2026-10-06):** Der Wasserspiegel fällt blockweise mit dem Gelände (1-Block-Stufen). Kein Wasser gibt es an Steilstufen über 1,5 Blöcke und dort, wo ein Fluss schräg über einen Hang liefe. Dort bleibt das Flussbett trocken. Geprüft wird nur, wo das Spiel Wasser schreibt, das kostet kaum Rechenzeit.


**Freie Ebene in der Mitte:** Mit dem Haken „Freie Ebene in der Mitte“ bekommt jede Biomzelle um ihre Mitte eine ganz flache, leere Fläche.

| Feld | Wirkung |
|---|---|
| Radius | Radius der flachen Fläche in Blöcken (100–6000, Vorgabe 1200). Eine Biomzelle ist etwa 16 000 Blöcke breit. |
| Höhe | Höhe der Ebene über dem Meer (2–200) |
| Rand | Breite des weichen Übergangs ins übrige Biomgelände (20–2000) |

- Auf der Ebene und ihrem Rand gibt es keine Flüsse, Seen, Bergzüge, nativen Berge, Bäume, eigenen Pflanzen, eigenen Dungeons, Minen, nativen Orte und kein Bergdorf. Kreaturen spawnen weiter.
- Die Ebene wirkt auch ohne „Gelände formen“.
- „Teleport zum nächsten“ setzt dich genau über die Mitte einer Biomzelle, also über die Ebene.
- Grenzt die Zelle an ein anderes Biom, läuft die Ebene dort mit dem Biomgewicht aus.

Die Vorlage **Sumpf** ist eine Hochfläche auf Höhe 24 mit Wellen ±2,5, vereinzelten Hügeln (Anteil 0,10, Höhe 14) und kaum Bergen (0,05). Das Wasser kommt aus **sehr vielen Seen und Flüssen**:

- native Flüsse, doppelt so breit;
- eigene Flüsse etwa alle 300 Blöcke;
- Seen-Anteil 0,5, Durchmesser 60.

Nachgerechnet sind etwa 7 % der Fläche See und 8 % eigene Flüsse. Dazu kommen **180 Pflanzenversuche je Zone** (Baum 4, Busch 4, Pilz 2, Halme 8) und 25 % der nativen Bäume. Eine schon gespeicherte `Sumpf.biom` hat die neuen Werte noch nicht. Links **„Sumpf“** (Vorlage) laden, „aktiv“ anhaken und speichern.

Technisch sind das drei Hooks:

- `0x5C5E20`: Grundhöhe einer Säule. Dieselbe Funktion nutzen auch Karte und Landschaft, sie stimmen also überein.
- `0x5EFFA0`: Berg- und Klippenfaktor.
- `0x5DF960`: Baum bauen. Ausgedünnt wird nur die normale Baumschleife, erkannt am Rücksprung `0x5EA7B3`.

## 4c. Bergzüge, Schneegrenze und Minenschächte

**Vorlage „Gebirge“** (links, Knopf „Gebirge“):

| Bereich | Werte |
|---|---|
| Bergzüge | etwa 180 Blöcke über dem Gelände, Spitzen bis etwa 230, etwa 500 Blöcke breit, Abstand etwa 1400 |
| Schnee | Schneegrenze bei 150 |
| Wasser | Flüsse und Seen nur in den Tälern |
| Minen | Minenschächte quer durch die Kämme |

| Feld (Spalte 3 unten) | Wirkung |
|---|---|
| Höhe / halbe Breite | Kammhöhe über dem Gelände und Abstand Kamm–Fuß in Blöcken |
| Abstand | mittlerer Abstand der Bergzüge. Die Züge folgen langen, gewundenen Linien. |
| Zacken 0–1 | 0 = glatter Rücken, 1 = Gipfel und Sättel im Wechsel, zerklüftete Flanken |
| Schneegrenze | ab dieser Höhe Schneefarbe und Schneeboden, mit unregelmäßigem Rand (0 = aus) |

| Feld (Spalte 2 unten) | Wirkung |
|---|---|
| Minen: Anteil / Raster | In jedem Rasterfeld mit einem Bergzug entsteht mit dieser Wahrscheinlichkeit eine Mine. |
| Stollen B × H | Querschnitt des Hauptstollens (Seitengänge 1 kleiner) |
| Seitengänge / senkrechte Schächte | Abzweige entlang des Kamms. Der Schacht führt aus der Stollenmitte senkrecht bis an die Oberfläche und hat oben einen Holzkranz. |
| Holz / Licht / Schienen | Farben der Stützen (alle 6 Blöcke), der Laternenblöcke (alle 12 Blöcke) und der Schienen auf Holzschwellen |

**Wie eine Mine liegt:**

- Der Hauptstollen läuft quer durch den Kamm, von Talseite zu Talseite. Die Sohle liegt 3 Blöcke über dem tieferen Talboden.
- Der Mod gräbt nur dort, wo über dem Stollen Fels ist. Wo der Stollen ins Freie tritt, entsteht deshalb von selbst das Mundloch.
- Gegraben wird nach dem Zonenbau auf dem Spielthread, mit Zeitbudget und unter der Weltsperre, so wie das Bergdorf baut.
- Für dieselbe Welt liegen die Minen immer an derselben Stelle.

**Voraussetzung:** Gelände formen ist an, die Bergzughöhe ist größer als 0, und **Berge ist höchstens 0,1**. Mit nativen Bergen läge das echte Gelände bis etwa 40 Blöcke über der Planung, dann liegen die Mundlöcher im Boden.

**Grenzen:** Die Laternen sind farbige Blöcke und leuchten nicht. Ein Mundloch kann blind enden, wenn ein Fluss oder See den Talboden verändert.

## 4e. Sternmassiv und große Hangbäume (Spalte 3 unten)

Das Sternmassiv baut um die Mitte jeder Biomzelle eine Hochebene mit Bergring. Vom Ring gehen sternförmig Grate und Täler ab. Jedes Tal führt am flachen Talgrund einen durchgehenden Fluss, der in einem See am Talende endet. Vorlage: Knopf **„Sternmassiv“** unter den Vorlagen.

| Feld | Wirkung | Vorlage |
|---|---|---|
| Täler | Anzahl der Täler (3–16); zwischen zwei Tälern liegt je ein Grat | 8 |
| Radius | Außenradius des Massivs; ab 85 % geht es weich in die übrige Biomform über | 6000 |
| Ring-Höhe | Ringgipfel über der Hochebene | 130 |
| Grat-Höhe | Grathöhe über der Grundhöhe am Außenende (innen gehen die Grate in den Ring über) | 152 |
| Talgrund-Breite | flacher Talboden, volle Breite | 90 |
| See-Größe | Seedurchmesser am Talende | 160 |

- **Hochebene:** Radius, Höhe und Rand kommen aus „Freie Ebene in der Mitte“ (Vorlage 1200 / 350 / 250). Ist die Ebene an, ist die Hochebene frei.
- **Keine kleinen Spitzen:** Im Massiv sind die nativen Berge ganz aus. Das Querprofil ist glatt, Gipfel und Sättel wechseln nur entlang der Grate (etwa 650 Blöcke Wellenlänge).
- **Flüsse:** Der Talgrund fällt höchstens 0,15 Blöcke je Block. Das Wasser fällt in 1-Block-Stufen bis in den See. Daneben liegt das Ufer einen halben Block höher, also nie tiefer als das Wasser.
- **Seen:** Der Wasserspiegel liegt auf dem niedrigsten Uferblock (wie bei allen eigenen Seen).
- **Was im Massiv wegfällt:** native Flüsse, eigene Rasterflüsse und Rasterseen. Außerhalb gelten sie weiter.
- **Farben:** In den 40 Blöcken unter der Schneegrenze geht der Boden in die Felsfarbe über, darüber liegt Schnee. Das Felsband gilt für alle Biome mit Schneegrenze und „Fels an“. Vorlage: Schnee ab 400, Hochebene (350) bleibt grün, Ring und innere Grate sind weiß.
- Gelände formen muss an sein.

**Große Hangbäume:** Bergfichten, 17–23 Blöcke breit und 39–47 hoch (bei Größe 1).

| Feld | Wirkung |
|---|---|
| Versuche/Zone | Versuche je Zone (0 = aus) |
| Größe x | 0,6–1,6 |

- **Wo sie wachsen:** nur an Hängen mit Steigung 0,2 bis 1,4, mindestens 35 Blöcke unter der Schneegrenze, nicht auf der freien Ebene, mindestens 14 Blöcke voneinander entfernt.
- **Wie sie stehen:** Der Stamm steht auf dem tiefsten Bodenblock unter ihm, damit er am Hang nicht schwebt.
- **Wann sie entstehen:** zusammen mit der Zone, wie die nativen Bäume, noch bevor das Spiel die Zone anzeigt, beleuchtet oder auf die Karte bringt. Es gibt kein Nachladen und kein Aufpoppen.
- **Eigene Pflanzen:** Auch die Pflanzen aus der Pflanzen-Spalte (Baum, Busch, Pilz, Kristall, Kaktus, Gras und „Eigene .cub“) entstehen jetzt mit der Zone. Nachträglich gesetzt werden nur noch eigene Dungeons und Minen.
- **Farben:** Stamm- und Blattfarbe des Bioms (Nadeln dunkler).

**Hügel (alle Biome):** Die Unruhe auf den Hügelkuppen hat jetzt eine Wellenlänge von 10 % der Hügelgröße und höchstens etwa 6 Blöcke Ausschlag. Vorher waren es fest 7 Blöcke und 25 % der Hügelhöhe; bei 152 hohen Hügeln gab das kleine spitze Zacken an den Hängen.

**Ungeprüft:** Gipfel über etwa 400 Blöcke habe ich im Spiel nicht getestet. Die Vorlage erreicht bis zu etwa 520.

## 4d. Seed-Rechner (SeedScout) und [F9]-Anzeige

**Der Seed-Rechner des Mods berücksichtigt die eigenen Biome jetzt immer.** `SeedScout.h` berechnet Klimazellen und native Orte direkt aus dem Weltsamen, ohne dass das Gebiet geladen sein muss. Davon hängt ab, wo Bergdörfer entstehen, welche nativen Orte gemieden werden und was die [F9]-Liste zeigt. Für jede berechnete Zelle läuft dieselbe Biom-Entscheidung wie im Spiel:

- erzwungene Zellen;
- Anteil und Klima-Auswahl;
- überschriebenes Klima, das sich auf die Ortsplanung des Spiels auswirkt;
- gelöschtes Lava-Bit.

**Was das ändert:**

- **Native Orte:** Orte, die ein Biom abschaltet, fehlen auch in der Berechnung. Die Bergdorf-Planung meidet sie nicht mehr, und [F9] zeigt sie nicht mehr an.
- **Caches:** Ändert sich ein Biom, ein Schalter oder eine erzwungene Zelle, verwirft der Rechner seine Zwischenspeicher.
- **[F9]-Anzeige:** Sie zeigt zusätzlich „Biom hier“ sowie das **nächste Gebiet jedes aktiven Bioms** mit Koordinaten, Entfernung und Richtung. Das sind berechnete Klimazellen-Mitten, auch in nie besuchtem Land.
- **„Nächstes Biom suchen“** in der Werkstatt rechnet ebenso. Es findet Biome im Umkreis von 24 Klimazellen, auch unerforschte. Bisher kannte es nur schon angelegte Zellen.

**Vorsicht:** Der Teleport nutzt dieselbe Suche. Ein Sprung in weit entferntes, ungeladenes Land braucht viel Speicher. Ohne „4 GB“ ist ein Absturz wahrscheinlich.

## 5. Die Datei `Strukturen\Biome\<name>.biom`

Die Datei ist reiner Text mit `schlüssel=wert` und kann auch von Hand bearbeitet werden. Danach in der Werkstatt **„Alle neu einlesen“** klicken.

| Schlüssel | Bedeutung |
|---|---|
| `aktiv` | 1/0 |
| `anteil` | 0..100, % der passenden Landzellen |
| `klima` | 0 alle, 1 gemäßigt, 2 heiß, 3 kalt, 4 feucht, 5 trocken |
| `klima_setzen`, `temperatur`, `feuchte` | Klima der Zelle überschreiben (0..1). Regen, Nebel und nativer Bewuchs folgen dann diesen Werten. |
| `boden`, `boden2`, `bodentyp`, `staerke` | Bodenfarben (R,G,B), Typ (0 nativ, 4 Gras, 9 Sand, 10 Schnee, 12 Lava-Boden), Deckkraft |
| `fels`, `fels_an` | Felsfarbe |
| `rassen`, `rassen_anteil` | Rassen-IDs (siehe Mob-Labor), Anteil der ersetzten Landspawns |
| `mobs`, `mobs_max`, `mobs_level` | eigene `.mob`-Dateien aus `Strukturen\Mobs`; Höchstzahl in der Nähe; `mobs_level` nur noch für ältere Fassungen |
| `mobs_level_modus`, `mobs_level_min`, `mobs_level_max`, `mobs_level_abweichung`, `spawner_rassen` | Schwierigkeit: 0 = Spielerlevel ± Abweichung, 1 = feste Spanne; Spawner nimmt auch die Rassen-IDs |
| `pflanzen_dichte`, `pflanzen_arten` | Versuche je Zone; Arten mit Gewicht, z. B. `Baum:3,Busch:2,Pilz:1,Kristall:0,Kaktus:0,Halme:4` |
| `stamm`, `blatt`, `akzent`, `pflanzen_groesse` | Farben und Größe der Pflanzen |
| `pflanzen_dateien`, `pflanzen_dateien_gewicht`, `pflanzen_dateien_aus` | eigene `.cub` aus `Strukturen` (höchstens 64×64), ihr Gewicht; abgehakte bleiben gelistet |
| `zonenmodelle_an`, `zonenmodelle_aus` | Zonen-Modelle (Baum, Blume, Sammelbar), die in diesem Biom abweichend vom Bereich des Modells an bzw. aus sind |
| `dungeon_anteil`, `dungeon_raeume` | % je Zone; Räume je Seite (2..6, also 15×15 bis 43×43 Blöcke) |
| `dungeon_wand`, `dungeon_boden`, `dungeon_akzent` | Farben (Akzent = Fackeln, Bossraum) |
| `dungeon_dach`, `dungeon_verfall` | Dach an/aus; 0 = heil, 1 = Ruine |
| `dungeon_rassen`, `dungeon_mobs_dateien`, `dungeon_mobs`, `dungeon_boss` | Besatzung |
| `orte_erlaubt` | Liste der Ortstypen, die **bleiben** |
| `terrain_an`, `grundhoehe`, `wellen`, `wellen_groesse`, `glaetten` | Gelände formen (siehe 4b) |
| `huegel_anteil`, `huegel_hoehe`, `huegel_groesse`, `berge` | Hügel und Berge |
| `native_baeume` | 0..100 % der normalen Bäume bleiben |
| `bergzug_hoehe`, `bergzug_breite`, `bergzug_abstand`, `bergzug_zacken`, `schneegrenze` | Bergzüge und Schnee (siehe 4c) |
| `minen_anteil`, `minen_raster`, `stollen_breite`, `stollen_hoehe`, `minen_seitengaenge`, `minen_schaechte`, `minen_holz`, `minen_licht`, `minen_schiene` | Minenschächte (siehe 4c) |
| `fluesse_nativ`, `fluss_breite`, `fluesse_eigen`, `fluss_abstand`, `seen_anteil`, `seen_groesse` | Flüsse und Seen (siehe 4b) |
| `ebene_an`, `ebene_radius`, `ebene_rand`, `ebene_hoehe` | freie Ebene in der Mitte (siehe 4b) |
| `stern_an`, `stern_arme`, `stern_radius`, `stern_ring_hoehe`, `stern_grat_hoehe`, `stern_tal_breite`, `stern_see_groesse` | Sternmassiv (siehe 4e) |
| `hangbaeume`, `hangbaum_groesse` | große Hangbäume (siehe 4e) |
| `bergdorf` | 1 = Bergdorf im Biom erlaubt |

Es sind höchstens 15 Biome gleichzeitig möglich.

---

## 6. Regeln, die das Ergebnis bestimmen

- **Neu entdeckt:**
  - Die Biom-Wahl einer Klimazelle fällt beim ersten Anlegen. Wer den Anteil ändert, ändert nur Zellen, die danach angelegt werden.
  - Farben, Kreaturen, Pflanzen, Dungeons und Orte gelten für jede danach **neu erzeugte Zone**. Zonen werden auch beim erneuten Betreten neu erzeugt.
- **Startzelle:** Die Zelle, in der das Spiel neue Figuren startet, bekommt nie ein Biom. Ozean auch nicht.
- **Pflanzen** wachsen nur auf Gras, Sand, Schnee oder Lava-Boden. Sie wachsen nicht im Wasser, nicht unter Überhängen und nicht in Zonen mit nativer Siedlung (mehr als 12 Objekte) oder Bergdorf.
- **Dungeons** brauchen eine Fläche mit höchstens 24 Blöcken Höhenunterschied. Der Boden liegt auf dem höchsten Punkt, darunter füllt das Fundament bis zum Gelände. Bergdorf-Zonen und ihre Nachbarn bleiben frei.
- **Abgebaute Pflanzen** wachsen beim Neuladen der Zone wieder, weil sie aus dem Weltsamen berechnet werden.

---

## 7. Ungeprüft

- Nichts davon ist im Spiel gelaufen. Geprüft sind:
  - alle Adressen und `ret`-Werte gegen die Cube.exe;
  - die Generatoren: Pflanzen, Dungeons (jeder Raum vom Eingang erreichbar, genau ein Bossraum), Laden und Speichern, Wiederholbarkeit;
  - das Kompilieren.
- **Bodentyp 12 („Lava-Boden“)**: Der Name ist aus der Farbe abgeleitet. Ob das Spiel diesen Typ sonst noch besonders behandelt, ist offen.
- **Teleport** setzt nur die Spielerposition, 300 Blöcke hoch. Ungetestet.
- **Mehrspieler**: Ein Server ohne Mod erzeugt Vanilla-Spawns und Vanilla-Orte.
- Eigene Mobs werden zur Laufzeit gespawnt, nicht als Zonen-Spawns. Sie verschwinden nicht von selbst, wenn du weggehst. Die Obergrenze liegt bei 48 verfolgten Kreaturen.
- **Zusätzliche Last:** Jeder Pflanzenversuch braucht eine Gewichtsabfrage und eine Bodensuche. Bei Dichte 300 in vielen Zonen kann das Bildzeit kosten. Die Messung unter „Karte & Leistung“ zeigt den Baustein `BiomeModule`.

## Stadt auf der Ebene / Hochebene

Im Biom-Reiter unter der freien Ebene: **Stadt auf der Ebene** = keine / Elfenstadt / Zwergenstadt / zufällig je Zelle, **Stadtradius** (0 = ganze Ebene minus 30 Blöcke). Braucht „Freie Ebene in der Mitte“ (beim Sternmassiv ist das die Hochebene). Die Stadt entsteht beim **Erzeugen neuer Zonen** – schon besuchte Gegenden bleiben, wie sie sind (neue Welt oder weit weg fliegen). Schalter „Zonenobjekte“ (Karte & Leistung) muss an sein.

**Elfenstadt:** in der Mitte der hohle Mutterbaum (117 × 117, 236 hoch; vier Torbögen, Wendeltreppe innen mit Geländer, Absätze in 50 und 100 Höhe, Halle mit Balkon in 150 Höhe, riesige Krone). Darum ein Laternenplatz, sechs Alleen, Ringwege und Ringe aus Baumhäusern in vier Farben (grün, hellgrün, Herbstgold, silbrig): Wendeltreppe am Stamm, Haus mit Tür, Fenstern, Bett, Tisch, Regal, Plattform mit Geländer, Krone.

**Elfenstadt (Stand 3):** Baumhäuser wie ein Wald verteilt (Mindestabstand 60, nicht gedreht, Tür immer Süden). **Aufgang:** quadratische Spiraltreppe um den Stamm, 3 breit, je zwei Felder eine Stufe, ohne Lücken; außen **native Zäune** (Objekt 0x34, mit Volumen) und an jeder Ecke Pfosten, die Treppe endet im Haus. **Hängebrücken:** zwei gerade Läufe mit 5×5-Eckplattform auf einer Stütze, Planken 5 breit, auf beiden Rändern native Zäune, leichter Durchhang, Laterne an der Eckplattform; höchstens zwei Brücken je Baumhaus. **Wald:** kleine und mittlere Bäume und Riesenpilze, Stamm/Stiel immer mindestens 5 Blöcke frei (Laub/Hut erst ab Höhe 6), keine Einzelblöcke am Boden. Schalter „Stadt: Zäune …“ unter Karte & Leistung (Zäune kosten Bildrate, falls nötig ausschalten).

**Elfenstadt (Stand 4, 2026-10-07):** Die Baumhäuser stehen jetzt in **Reihen und Spalten** eines 64er-Rasters (Versatz je Spalte und je Reihe, nie je Haus; jedes 8. Feld bleibt Lichtung). Dadurch liegen gegenüberliegende Plattform-Ausgänge auf einer Linie: **jede Hängebrücke ist ein einziger gerader Lauf, ohne Ecke, ohne Pfeiler** (Planken 5 breit, native Zäune auf beiden Rändern, Durchhang, Laterne unter der Deckmitte; jede 5. Verbindung bleibt frei, eine fehlende Nachbarhütte darf überspannt werden). **In der Mitte jedes Rasterfelds** zwischen vier Häusern steht ein **vergrößerter Baum aus dem Biom**: alle Zonen-Modelle der Art „Baum“ (für dieses Biom oder „überall“), die Hangbäume (falls an), der Pflanzen-Baum (Gewicht > 0), eigene Pflanzendateien ab 12 Blöcken Höhe und – wenn das Biom native Bäume hat – der native Baum 0x832. Vergrößert ganzzahlig auf etwa 66 Blöcke (höchstens 64 breit, 110 hoch), passt er nicht, kommt dieselbe Art eine Stufe kleiner oder die nächste Art. Freiraum wird Schicht für Schicht geprüft (Baum gegen Baumhaus, Mutterbaum, Brückendeck mit Kopffreiheit). Die Konsole listet beim Planen alle verwendeten Bäume.

**Abgeschnittene Bäume (behoben, zwei Ursachen):** (1) Die Bodenschicht der Elfenstadt ist nur 21 Blöcke hoch – die Kronen der mittleren Waldbäume reichen bis 27 und wurden oben flach abgeschnitten. Was höher reicht, geht jetzt in die Brückenschicht darüber. (2) Jede Zone entschied selbst über das Biom an ihrem Mittelpunkt, ob sie Stadt bekommt; am Stadtrand konnte eine Nachbarzone so leer bleiben (Bäume/Häuser endeten an der Zonengrenze, man sah ins Innere). Jetzt bekommt **jede Zone, die der Stadtplan berührt, einen Bauauftrag** (Stadtkreis + 30 Blöcke Puffer und die Hülle jedes Bauwerks/jeder Brücke); erzeugt das Spiel später eine dieser Zonen, baut sie ihren Teil ausdrücklich. Der Plan entsteht schon, wenn eine Zone bis 512 Blöcke vor der Stadt liegt. Die Bodenhöhe misst die erste Zone (mit Ausweichpunkten, falls dort Wasser ist) und gilt dann für alle. Zustand unter Karte & Leistung: „Bauaufträge für N Zonen“, „ohne Bodenhöhe“ (sollte 0 sein).

**Prüfschleife (seit 2026-10-07):** Nachdem eine Zone gebaut ist, liest der Mod jedes Bauwerk der Zone Block für Block zurück (sichtbare Oberfläche ohne Markerfarben, über `world_getColumnData` 0x42F7E0). Fehlen Blöcke, wird das Bauwerk ohne Marker noch einmal gesetzt (Türen, Betten, Laternen entstehen also nicht doppelt) und erneut geprüft. Zustand unter Karte & Leistung: „Prüfung: … geprüft, … mit Lücken -> … nachgesetzt ok, … weiterhin fehlerhaft“. Die ersten 20 Fälle stehen mit Bauwerk, Lage, fehlender Anzahl und Modellhöhe im Protokoll – damit lässt sich die eigentliche Ursache finden, falls etwas „weiterhin fehlerhaft“ bleibt.

**Bewohner (2026-10-07):** In der Elfenstadt laufen die großen Elfen aus `Strukturen\Mobs\elf-w1..5.mob` / `elf-m1..5.mob` (friedlich) auf Trampelpfaden und Hängebrücken – bis zu 18 im Umkreis von 160 Blöcken um den Spieler (Kreaturen gibt es nur in geladenen Zonen), insgesamt höchstens 40. Fehlen die `.mob`-Dateien, kommen native Elfen ×1,2. Der **eigene Händler** steht auf dem Aussichtsbalkon des Mutterbaums (Höhe 150; nähert man sich dem Mutterbaum, wird er dorthin geholt). Der **Richtungshändler** (Luftschiff-Taxi) steht als Elf oben auf dem Landeplatz (in der Zwergenstadt als Zwerg).

**Brunnen:** Native Brunnen (0x882, wie im Dorf mit Kultur 8 → echtes Wasser). In der Elfenstadt steht an jeder Kreuzung Allee × Ringweg einer auf einem kleinen gepflasterten Platz (Radius 9). Passt er nicht ganz in eine Zone, wird er ein Stück entlang des Rings versetzt. In der Zwergenstadt stehen vier auf dem Platz um die Festung.

**Taxi-NPC und Bewohner (Korrektur):** Der Taxi-NPC wird nicht mehr beim Erzeugen der Zone gesetzt (oft weit weg, Zone noch nicht geladen → er fiel ins Leere). Er erscheint, sobald man näher als 160 Blöcke ist, und wird neu gesetzt, falls er fehlt. Die Elfen-Bewohner suchen alle `elf-*.mob` unter `Strukturen\Mobs` (auch in Unterordnern). Findet der Mod keine, meldet das Protokoll den gesuchten Ordner und es kommen native Elfen; nach 60 s wird erneut gesucht.

**Mutterbaum-Treppe (2026-10-07):** Breite Wendeltreppe vom offenen Schacht in der Mitte (Radius 7) bis an die Innenwand – oben etwa 6, unten etwa 10 Blöcke breit, Stufen 2 Blöcke dick (keine Löcher, auch nicht von unten), je zwei Felder am Schachtrand ein Block Steigung, Kopffreiheit mindestens 26 Blöcke. Am Schachtrand lückenlos **native Zäune** (144 Stück, mit Volumen). Die alten Absätze bei 50/100 sind entfallen (sie lagen quer über der neuen Treppe); Wandlaternen alle 14 Stufen, Laternenrund im Schacht am Boden.

**Zonengrenzen (Ursache gefunden, 2026-10-07):** Die Prüfschleife hat gezeigt: Jedes Bauwerk, dessen untere Ecke in der Nachbarzone liegt, fehlte in der geprüften Zone zu 100 % – auch nach erneutem Setzen. `placeStructure` baut in eine Zone nur Bauwerke, deren untere Ecke in dieser Zone liegt. Deshalb: (1) Die Baumhäuser stehen auf einem an den Zonen ausgerichteten Raster (in jeder Zone bei 32, 96, 160, 224 ± 8) und liegen damit immer ganz in einer Zone; die großen Bäume stehen in den Feldmitten bei 64, 128, 192 – die Feldmitten auf der Zonengrenze bleiben frei. Kein Baumhaus und kein großer Baum überschneidet eine Zonengrenze. (2) Was trotzdem über eine Grenze reicht (Mutterbaum – die Stadtmitte liegt immer auf einer Zonenecke –, Festung, Zwergenhäuser, Landeplatz), wird je Zone zugeschnitten und mit der Ecke in der Zone gesetzt. Zustand: „… Teile an Zonengrenzen zugeschnitten“.

**Native Bäume in der Stadt:** 0x832 (fir-tree), 0x833 (thorn-tree), 0x840 (tree-leaves) und 0x99E (christmas-tree) passen nicht ins Bild und werden in der Stadt nicht verwendet – auch nicht, wenn ein Zonen-Modell des Bioms daraus besteht (erkannt am Namen und am Inhalt, auch vergrößert). An ihre Stelle treten die Kastanien. Die Konsole meldet jeden ersetzten Baum.

**Kastanien (Laubbäume der Elfenstadt):** Statt des vergrößerten Pflanzen-Laubbaums stehen in den Feldmitten Kastanien nach echter Vorlage (Rosskastanie: kurzer kräftiger Stamm mit Wurzelanlauf, 5 steil aufsteigende Hauptäste, unten ausladende hängende Seitenäste, breite gewölbte Kuppelkrone aus dichten Laubbüscheln; Edelkastanie: dickerer, spiralig gefurchter Stamm, helleres Laub, hängende Kätzchen). Dateien in `Strukturen\Stadt\`: `Kastanie_1_Bluete` (weiße Blütenkerzen), `Kastanie_2_Sommer` (grüne Früchte), `Kastanie_3_Herbst` (Herbstlaub, Kastanien), `Kastanie_4_Edelkastanie`, je etwa 55 × 55 × 67. **Im LAB anpassbar:** Bauen → Bibliothek → `Strukturen\Stadt` öffnen, ändern, unter demselben Namen speichern. Die Stadt nimmt immer die Datei (fehlt sie, wird sie neu angelegt; „Stadtmodelle als .cub ablegen“ überschreibt sie nie). Je Stadt eine Jahreszeit der Rosskastanie (fest) plus die Edelkastanie, jeweils auch in 0,8-facher Größe. Änderungen gelten für Städte, die nach dem Neustart geplant werden.

**Zwergenstadt:** in der Mitte die Festung (140 × 140: Ringmauer mit Wehrgang, vier Ecktürme, Torhaus mit Bannern, Bergfried mit vier Geschossen, Treppen, Tafel, Betten). Ringstraße, Ringgassen, acht Radialstraßen, Steinhäuser in vier Formen (Tür zur Straße), vier native Schmieden (Ofen, Amboss, Anpassungsbank), große Stadtmauer mit Zinnen, Türmen, vier Toren und Rampen auf den Wehrgang.

**Licht:** überall native Laternen (gelbe Marker → Laterne 0x0D, nachts leuchtend): an Wegen, Plattformen, Treppen, unter den Kronen, an Mauern und Türmen.

**Landeplatz 0x887:** jede Stadt hat den nativen Landeplatzturm (Deck in 60 Höhe). Sobald er erzeugt ist, steht oben ein Taxi-NPC; wer zu ihm geht, bekommt ein Luftschiff an das Deck (wie an den Bergdorf-Türmen).

**Ansehen/Ändern:** Knopf „Stadtmodelle als .cub ablegen“ schreibt alle Gebäude nach `Strukturen\Stadt\` – im LAB unter Bauen → Bibliothek öffnen.


## Bäume, Pflanzen, Sammelbare und Schwierigkeit (2026-10-13)

Ganz rechts in der Biom-Werkstatt stehen zwei **Abhaklisten**:

- **Bäume & Pflanzen:** die sechs prozeduralen Arten, „Native Bäume des Spiels“, eigene `.cub`-Dateien und alle Zonen-Modelle der Art Baum oder Blume. **Haken = wächst in diesem Biom.**
  - **Gewicht:** Zeile wählen, Wert eintragen, „setzen“. Arten nehmen 1–20, native Bäume den Anteil in % (1–100), `.cub`-Dateien ein gemeinsames Gewicht.
  - Die **letzte Zeile ist immer frei** („+ Baum / Pflanze hinzufügen …“). Ein Klick darauf bietet zwei Wege: eine `.cub`-Datei wählen oder im Reiter „Zonen-Modelle“ ein neues Modell anlegen.
- **Sammelbare Pflanzen:** alle Zonen-Modelle der Art „Sammelbar“. Haken = sie wachsen hier. Die freie letzte Zeile führt zum Anlegen einer neuen sammelbaren Pflanze.

Zonen-Modelle haben einen eigenen Bereich („überall“, „vanilla“, „biom:…“). Entspricht ein Haken diesem Bereich, wird nichts extra gespeichert. Weicht er ab, steht das Modell in `zonenmodelle_an` bzw. `zonenmodelle_aus` dieses Bioms und gilt dort vorrangig.

**Schwierigkeit** (Spalte 2, „Eigene Mobs“):
- **Spielerlevel (± Abweichung):** Jeder Spawn bekommt den Spielerlevel plus/minus einen Zufallswert bis zur Abweichung.
- **Feste Levelspanne:** Jeder Spawn bekommt einen Level von … bis ….
- Die Schwierigkeit gilt für eigene `.mob`-Mobs, den Spawner und die Dungeon-Besatzung. Mit „Spawner setzt auch die Rassen-IDs“ spawnt der Spawner zusätzlich die Rassen aus „Kreaturen“, ebenfalls mit diesem Level.
- **Native Spawns behalten den Level des Gebiets.** Den setzt das Spiel erst beim Umwandeln in eine Kreatur (`GameController_updateSubsystems`, Spawn+0x34 ← Gebietsstufe), darauf hat der Mod keinen sauberen Zugriff.

## Stadtviertel (2026-10-13)

Elfen- und Zwergenstadt haben jetzt Viertel. Jedes Viertel ist ein gepflasterter Platz mit eigenem Muster.
- **Elfen:** fünf Plätze zwischen den Alleen auf Radius 215 um den Mutterbaum. Das sechste Feld bleibt Wohngebiet. Auf den Plätzen stehen keine Baumhäuser, keine großen Bäume und keine Hängebrücken.
- **Zwerge:** vier Plätze im zweiten Ringband zwischen den Radialstraßen. Die Werkstätten im ersten Band bilden das Arbeitsviertel.

| Viertel | Inhalt |
|---|---|
| **Arbeitsviertel** | Native Werkstatt 0x903 dreimal mit verschiedenen Kulturen: 0x0D Schmied (Ofen, Amboss, Anpassungsbank), 0x0E Zimmerer (Säge, Werkbank), 0x0F Weber (Spinnrad, Webstuhl). Dazu ein Lagerfeuer zum Kochen. Zwerge: die vier Werkstätten im ersten Band (Schmied, Zimmerer, Weber, Schmied). |
| **Händlerviertel** | Fünf Händlerhütten mit den nativen Dorfhändlern (Klasse 0x80, 0x81, 0x82, 0x83 und Wirt 0x84). Ihre Waren kommen aus denselben Generatoren wie im Dorf. Dazu Marktstände (nur Elfen) und unser eigener Händler auf dem Platz. |
| **Alchemieviertel** | Alchemistenhaus mit Vorhof und **Braustand**: Kessel auf vier Pfosten, darunter ein natives Lagerfeuer (Objekt 0x41). Der Alchemist verkauft alle Zutaten der geladenen Rezepte. |
| **Tempelviertel** | Großer Tempel mit schwebender, leuchtender **Kugel**. **[R]** in ihrer Nähe = Segen (Leben und Mana voll, danach 60 s Pause). |
| **Tierviertel** | Zwei eingezäunte Gehege (native Zäune, Heuboden) mit je 3 friedlichen Tieren (passiv, laufen umher). |

**Braustand und Tränke:** Rezepte mit der Station **„Lagerfeuer / Braustand“** (Code 9) lassen sich am Braustand brauen. Das Spiel prüft dafür ein Lagerfeuer-Objekt im Umkreis von 4 Blöcken, genau das steht unter dem Kessel. Damit eure Tränke nur dort entstehen: im Reiter „Rezepte“ diese Station wählen.

**Tiere je Biom:** Biom-Werkstatt, Spalte 4, „Tiere (Tierviertel)“: Tier in der Liste wählen und **„+“**, oder Rassen-IDs von Hand eintragen. Leer = Schwein, Schaf, Kuh, Huhn. Gespeichert als `stadt_tiere` in der `.biom`.

**Tempel im LAB:** Die Modelle stehen als `Strukturen\Stadt\Tempel_Elfen.cub`, `Tempel_Zwerge.cub` und `Tempel_Kugel.cub` bereit. Fehlen sie, legt das Spiel sie an. Danach gilt immer die Datei.
- Ändern: Bauen → Bibliothek → `Strukturen\Stadt` öffnen, bearbeiten, unter demselben Namen speichern.
- Eigenes Layout: einfach unter diesem Namen ablegen.
- Die Kugel schwebt 9 Blöcke über dem Tempelboden in der Modellmitte.

Die Platzmitten stehen im LAB unter Biome → „Im Spiel“ (Status): „Haendlerviertel (x, y)“ usw. Die Bewohner der Viertel lassen sich mit dem Schalter „Stadt-Bewohner“ (Karte & Leistung) abschalten.
