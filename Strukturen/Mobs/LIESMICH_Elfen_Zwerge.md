# Neue Elfen und untote Zwerge (2026-10-07)

Je 5 Varianten weiblich und männlich, als Mob-Labor-Dateien (`*.mob`) mit eigenen Kopf- und Körpermodellen.
Den Ordner `Strukturen` in den Ordner von `Cube.exe` legen (Teile stehen in der .mob RELATIV zu ihrem Ordner (`Teile\Elfen\...`); der Mod sucht sie neben der .mob, dann nach Dateinamen in allen Unterordnern der .mob, zuletzt unter `Strukturen\Mobs` -- der Ordner samt Modellen ist frei verschiebbar), dann im LAB unter **Mobs → Laden** eine `.mob` wählen und spawnen.

**Kopfformen unverändert:** Bei allen 20 Köpfen ist exakt dieselbe Voxelbelegung wie im nativen Kopf (geprüft). Geändert sind nur Farben sichtbarer Voxel; verdeckte Füllfarben im Inneren (Türkis/Lila/Grün) bleiben unverändert.

| Datei | Basis | Kopf | Körper | Aussehen |
|---|---|---|---|---|
| elf-w1..w5 | ElfFemale (1), Größe ×1.2 | elf-head-f01/f02 | dein langer Körper (body1_lang) | Haut dunkler: Honig, Bronze, Dämmerung, Oliv, Tiefbraun; Augen Smaragd, Violett, Bernstein, Eisblau, Gold; Robe Waldgrün, Mitternacht, Wein, Moosgrau, Tiefseegrün |
| elf-m1..m5 | ElfMale (0), Größe ×1.2 | elf-head-m02/m03/m04 | body1_lang | wie oben, Roben dunkler |
| zwerg-untot-w1..w5 | DwarfFemale (10) | dwarf-head-f03/f04 | dwarf-body-female | Haut verwest (grüngrau, Asche, Frost, Mumie, Moder), Schädelplatten, Augenhöhlen, glimmende Iris (grün, blau, rot, violett, gelb), Brauenbogen, Jochbeine, Nasenloch, Zahnreihe; Körper mit Brustbein, Rippen, Wirbelsäule, Schlüsselbeinen, Kleid als Grablumpen |
| zwerg-untot-m1..m5 | DwarfMale (9) | dwarf-head-m04/m05 | dwarf-body | dasselbe, Hose als Lumpen |

Hände, Füße, Haare/Bart kommen von der Basisrasse. Feindlichkeit: Elfen 3 (greifen nicht an), untote Zwerge 1 (Standard, feindlich) – im Mob-Labor änderbar.

**Langer Elfenkörper (Stand 2026-10-07):** Das Spiel zeichnet jedes Teil um seine Mitte. Der 25 statt 10 Blöcke hohe Körper wuchs deshalb nach oben in den Kopf und nach unten in die Beine – das war der Hauptgrund, warum die Elfen nicht gut aussahen. Beim Spawnen richtet der Mod Körper, Kopf und Hände jetzt automatisch aus: Die Maßeinheit wird aus der Anordnung der Basisrasse abgeleitet (Kopf sitzt auf dem Körper), dann Körper um die halbe Mehrhöhe nach oben, Kopf und Hände um die ganze, Füße bleiben; die Kollisionshöhe wächst mit. Das Protokoll zeigt die Werte („Auto-Ausrichtung: Koerper 10->25 …“). Teile mit gesetzter Position werden nicht angefasst – zum Feinjustieren „Letzten Mob auslesen“ und die Position im Mob-Labor setzen.
Vorschau: `Vorschau\*.png`. Erzeugt mit `Werkzeuge_Mobs\gen.py` (Python, nur Umfärben).
