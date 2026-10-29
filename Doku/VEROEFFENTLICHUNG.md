# AdventureMod veröffentlichen – Checkliste (Stand 2026-10-07)

> **Seit 2026-10-13 (Umzug nach `Cube-Adventure-Test`):** Das Paket baut `Projekt\Veroeffentlichen.bat`. Es landet als `Projekt\Veroeffentlichung\CubeAdventure_<Datum>.zip` und enthält `Mods\AdventureMod.dll`, den **einen** `Strukturen`-Ordner (ohne weltbezogene Dateien), `LIESMICH.txt` und `Lizenzen\`. Ordnerübersicht: `Projekt\README.md`.

## Was die Veröffentlichungsfassung im Spiel anders macht

`main.cpp`: `kDevBuild = false` (für die Entwicklung auf `true` stellen).

- Kein Konsolenfenster mehr. Alle Ausgaben gehen weiter ins LAB-Protokoll (Reiter „Logger“) und nach `AdventureMod_log.txt`.
- Keine Bildschirmtexte mehr. Abgeschaltet sind das Tracking, die Trank-Restzeit, die Taxi-Sprechblase und der GemForge-Zähler. Hinweise wie Trank-Start/-Ende und der Händler stehen weiter im Spiel-Chat.
- **F9** blendet nur noch die Koordinaten ein: unten rechts, rechtsbündig, „X … Y … Z … Zone …“.
- **L**-Testmenü samt seinen STRG-Tasten ist aus. Damit ist auch STRG+4 (Edelstein-Cubes) nicht mehr erreichbar.
- Das **LAB** startet unsichtbar und erscheint erst mit **F10**.
- Der **Debugger** (externer Logger, hängt sich an Cube.exe) startet nicht mehr von selbst, sondern nur über LAB → Logger → „Logger starten“.

## Bauen

- Visual Studio 2022, Konfiguration **Release | Win32** (x86, Cube World ist 32 Bit).
- Benötigt: MinHook (`MinHook_134_lib`), Legacy DirectX SDK (Juni 2010) für `d3dx9`.

## Was ausgeliefert wird

| Datei / Ordner | Zweck |
|---|---|
| `AdventureMod.dll` (Release, Win32) | der Mod |
| Loader/Injektor, mit dem du die DLL in Cube.exe lädst | der Mod injiziert sich nicht selbst in einen fremden Prozess. Liefere entweder deinen Injektor mit oder beschreibe, welchen man nimmt. |
| `Strukturen\Biome\*.biom` | eigene Biome (ohne aktive Datei läuft alles wie Vanilla) |
| `Strukturen\Zonenmodelle\*.zmod` + zugehörige `.cub` | Zonen-Modelle, sammelbare Pflanzen |
| `Strukturen\Rezepte\*.rez` (+ eigene Modelle) | Rezepte |
| `Strukturen\Stadt\Kastanie_*.cub` | Kastanien der Elfenstadt (fehlen sie, werden sie erzeugt) |
| `Strukturen\Mobs\*.mob` + `Strukturen\Mobs\Teile\...` | Elfen und untote Zwerge (Bewohner der Elfenstadt). Die Teile werden relativ zur `.mob` gesucht. |
| `LIESMICH.txt` (kurz) | Installation, Tasten (F9, F10, R am Händler), bekannte Grenzen, Deinstallation |
| Lizenztexte (siehe unten) | Pflicht |

**Nicht** ausliefern:

- `Cube.exe`, `data*.db` und andere Spieldateien
- Sicherungen (`Sicherungen\`), `Entfernt\`, Logdateien (`AdventureMod_log*.txt`, `*_crash*.txt`, Minidumps)
- `AdventureMod_schalter.cfg` (entsteht beim Spieler neu)
- `Projekt\Werkzeuge\`, die RE-Notizen (`RE_*.md`), Testdateien, `.vscode\`

## Voraussetzungen beim Spieler

- **Microsoft Visual C++ Redistributable (x86)** für VS 2015–2022. Das Release baut mit `/MD`. Alternativ auf `/MT` umstellen, dann muss MinHook ebenfalls mit `/MT` gebaut sein.
- **DirectX End-User Runtime (Juni 2010)** wegen `d3dx9_43.dll` (Schrift der F9-Anzeige).
- Cube World Alpha, gleiche `Cube.exe`. Die Hooks prüfen die Funktionsanfänge; weicht die Exe ab, schalten sich die betroffenen Teile ab und melden das im Protokoll.
- Optional „4 GB“ (Large Address Aware) über den Logger. Nur auf ausdrücklichen Klick des Spielers.

## Lizenzen und Rechte – vor der Veröffentlichung klären

(Keine Rechtsberatung, nur was im Projekt auffällt.)

- **CubicExpansion:** `PotionCrafting` (aus CubicExpansion, dort GPL v3) ist seit 2026-10-07 **entfernt** – die eigenen Tränke (Rezepte + Effects) ersetzen es. Der Mod enthält damit keinen Code mehr aus CubicExpansion (die alten Dateien liegen nur noch in `Entfernt\` und der Sicherung, nicht mitliefern).
- **MinHook:** BSD-2-Clause, den Lizenztext beilegen.
- **cwsdk** (Andoryuuta): Lizenz des Originals prüfen und beilegen. Im Projekt liegt keine Lizenzdatei.
- **Spielinhalte:** Die Elfen-/Zwergenköpfe und -körper sind umgefärbte native Modelle von Cube World. Der native Landeplatz 0x887 und die Schmiede werden zur Laufzeit aus dem Spiel gelesen, das ist unkritisch. Umgefärbte Originalmodelle mitzuliefern, berührt dagegen die Rechte am Spiel. Sicherer ist es, sie vom Spieler beim ersten Start aus seiner `data1.db` erzeugen zu lassen oder eigene Köpfe zu bauen.

## Vor dem Hochladen testen

1. Neue Welt starten: kein Konsolenfenster, kein Text auf dem Bild, das LAB ist zu.
2. F9: Koordinaten unten rechts. F10: LAB auf/zu.
3. Elfenstadt besuchen:
   - Bewohner laufen auf Wegen und Brücken.
   - Der Händler steht auf dem Aussichtsbalkon des Mutterbaums.
   - Der Taxi-Elf steht oben auf dem Landeplatz.
   - Die Prüfung meldet „0 weiterhin fehlerhaft“.
4. LAB → Logger → „Logger starten“ funktioniert.

## Nachtrag 2026-10-13: neue Dateien zum Ausliefern

| Datei / Ordner | Zweck |
|---|---|
| `Strukturen\Skills\*.skill` + `Strukturen\Skills\Bilder\*.png` | Skills Teleport/Beschwören (fehlen sie, legt der Mod die Skill-Dateien neu an, die Bilder nicht) |
| `Strukturen\Mobs\Tiere\*.mob` + `Strukturen\Mobs\Teile\Tiere\*.cub` | 26 neue Tiere (umgefärbte native Teile → gleicher Rechtehinweis wie bei Elfen/Zwergen) |
| `Strukturen\Stadt\Tempel_Elfen.cub`, `Tempel_Zwerge.cub`, `Tempel_Kugel.cub` | Tempel der Städte (fehlen sie, werden sie erzeugt) |
| `*.funk` neben eigenen Gebäuden | Gebäude-Funktionen (nur wenn ihr Gebäude mitliefert) |

Nicht ausliefern: `Projekt\Werkzeuge\Mobs\animals.py`, `Werkzeuge_Mobs\cubdb.py`.

Zusätzlich vor dem Hochladen testen:
- Elfen- und Zwergenstadt: Viertel, Händler mit Waren, Alchemist, Braustand (Trank mit Station „Lagerfeuer / Braustand“ brauen), Tempel-Kugel mit [R], Tiere in den Gehegen.
- Runensteine (U ablegen/aufheben, 6 Liste/porten, 4 teleportieren), Beschwören (5), Skill-Leiste.
- LAB öffnen und eine Datei öffnen: kein Absturz mehr nach dem Dateidialog (xaudio2_7 ist festgenagelt).

## Sicherheitsprüfung (Stand 2026-10-29)

- **Quelltext:** Keine API-Schlüssel, Tokens, Passwörter, E-Mail-Adressen oder Benutzernamen gefunden. Netzwerk wird nur für die Anzeige der eigenen LAN-Adresse (Lan.cpp) genutzt, es wird nichts gesendet.
- **Pfade:**
  - Alle Mod-Dateien sind relativ zum Spielordner angelegt: `Strukturen\`, `AdventureMod_*.txt/.cfg`, `Save\AdventureMod.db`.
  - Neu: Der Mod setzt beim Start den Arbeitsordner auf den Ordner der laufenden `Cube.exe` (`ChdirToGameDir` in main.cpp). Der Server macht das schon lange so.
  - Es gibt keine festen Pfade.
- **DLL:**
  - Eine Debug-DLL enthält den vollen PDB-Pfad (`C:\Users\<Name>\...`) und die Include-Pfade von Visual Studio.
  - Release setzt jetzt `/PDBALTPATH:%_PDB%`, dann steht in der DLL nur noch der Dateiname.
  - **Immer Release|Win32 veröffentlichen.**
- **Veroeffentlichen.ps1 bricht ab, wenn:**
  - die DLL `\Users\`, den Windows-Benutzernamen, `\Desktop\`, `\Downloads\`, eine Debug-PDB oder Debug-Laufzeit-DLLs enthält,
  - eine Text-, `.mob`- oder `.cfg`-Datei im Paket einen Benutzerpfad, Laufwerkspfad, eine E-Mail-Adresse oder Wörter wie key/secret/password/token enthält.
- **Nicht im Paket:** `Strukturen\LabApi` (Datei-API, nur Entwicklung), `*.orig.cub`, `*.alt/.bak/.tmp/.log/.erledigt` sowie die schon vorher ausgeschlossenen Dateien.
- **LAB-Datei-API:** Sie läuft nur noch, wenn der Ordner `Strukturen\LabApi` existiert. Der Mod legt ihn nicht mehr selbst an. Bildnamen werden auf einen reinen Dateinamen `.png` im Ordner `bilder` beschränkt (kein `..`, kein Pfad).

## Grundeinstellungen (Stand 2026-10-29)

- `Strukturen\Grundeinstellungen\*.cfg` kommen mit ins Paket und gelten bei neuen Spielern.
- Vor dem Veröffentlichen im LAB „Als Grundeinstellung speichern“ drücken, wenn die eigenen Werte die Vorgabe sein sollen.
- **Hinweis für die LIESMICH:** Mit `vier_gb=1` setzt der Mod nach dem ersten Beenden in der `Cube.exe` des Spielers das 4-GB-Bit (Large Address Aware).
  - Vorher legt er die Sicherung `Cube.exe.vor-4GB.bak` an.
  - Zurücknehmen: LAB → Logger → 4 GB, oder die Sicherung zurückkopieren.

## Laufzeit-Bibliotheken (Stand 2026-10-29)

- **Release baut jetzt mit `/MT`.** Die C++-Laufzeit steckt in der DLL, Spieler brauchen kein VC++ Redistributable.
- **Debug-DLLs nie mitgeben.** `MSVCP140D.dll`, `VCRUNTIME140D.dll` und `ucrtbased.dll` sind die Debug-Laufzeit von Visual Studio. Microsoft erlaubt nicht, sie weiterzugeben, und auf Rechnern ohne Visual Studio fehlen sie. Sie erscheinen nur, wenn „Debug“ gebaut wurde.
- **Bleibt nötig:** `d3dx9_43.dll` aus der DirectX End-User Runtime (Juni 2010). Für den Spieler ist das ein Hinweis in der LIESMICH, mitgeliefert wird nichts.

## Ohne D3DX (Stand 2026-10-29)

- **Kein `d3dx9_43.dll` mehr.** Alle Mod-Texte (F9, Kartenbeschriftungen, Skill-Beschreibung, Listen) nutzen die Schrift des Spiels (`NativeUi::Hud*`, Klone des Weltnamen-Felds).
- **Entfernt:**
  - Shader (F8). Der Code liegt in `Projekt\Entfernt\PostFx.*`, `Strukturen\Shader` wird nicht mehr ausgeliefert.
  - Skill-Leiste und Effekt-Kästchen.
- **Spieler brauchen außer Cube World und einem Mod-Starter nichts.**
- **Zum Bauen** ist kein DirectX SDK mehr nötig. `d3d9.h` und `d3d9.lib` kommen aus dem Windows SDK.
- `Veroeffentlichen.ps1` bricht ab, falls die DLL doch `d3dx9_43.dll` verlangt.
