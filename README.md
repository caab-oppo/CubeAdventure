# Cube Adventure – Mod für Cube World Alpha

Cube Adventure erweitert **Cube World Alpha** (2013, 32 Bit) um eigene Biome, Elfen- und Zwergenstädte mit Vierteln, einen Hochelfenkönig, kleine Teleport-Tempel, neue Tiere, Skills (Teleport per Runenstein, Beschwören), Händler, Rezepte und ein In-Game-Editor-Fenster (das „LAB“, Taste F10).

> Spieler laden das fertige Paket unter **[Releases](../../releases)** herunter. Dieses Repository enthält den Quellcode und die Inhalte.

---

## Spielen (Release)

1. Unter **Releases** die Datei `CubeAdventure_<Datum>.zip` herunterladen.
2. Den Inhalt in den Cube-World-Alpha-Ordner kopieren, dorthin, wo `Cube.exe` liegt:
   ```
   Mods\AdventureMod.dll
   Strukturen\
   LIESMICH.txt
   ```
3. Das Spiel über einen **Mod-Starter** starten, der DLLs aus dem Ordner `Mods` lädt (z. B. CubeModLauncher). `Cube.exe` allein lädt keine Mods.

**Voraussetzungen:** keine weiteren. Kein Visual C++ Redistributable und keine DirectX-Runtime nötig.

**Hinweis 4 GB:** Mit der Grundeinstellung `vier_gb=1` setzt der Mod nach dem ersten Beenden in deiner `Cube.exe` das 4-GB-Bit (Large Address Aware). Vorher legt er die Sicherung `Cube.exe.vor-4GB.bak` an. Abschalten: LAB → „Netz & Tiere“.

### Tasten

| Taste | Wirkung |
|---|---|
| F9 | Koordinaten / Biom / Orte ein- und ausblenden |
| F10 | LAB-Fenster (Editor) |
| U | Runenstein ablegen (mit Namen), neben einem Stein: aufheben |
| P | zum Runenstein teleportieren (bei mehreren: Liste, 6 = weiter) |
| 5 | Beschwören (die zuletzt besiegten Gegner kämpfen kurz für dich) |
| X | Skillfenster: Teleport und Beschwören stehen im nativen Baum |
| R | Tempelkugel berühren |
| RMT | Marker setzen/löschen |

Der Teleport wird beim **Hochelfenkönig** im Mutterbaum der Elfenstadt freigeschaltet. Ab **25 gefundenen Tempeln** bringt dich jede Tempelkugel direkt zu ihm.

---

## Selbst bauen

**Benötigt**
- Windows, **Visual Studio 2022** mit „Desktopentwicklung mit C++“ (Toolset v143, Windows SDK)
- **MinHook**, nicht im Repository. Siehe [`MinHook_134_lib/LIESMICH.md`](MinHook_134_lib/LIESMICH.md), Download unter https://github.com/TsudaKageyu/minhook/releases
- Cube World Alpha zum Testen

**Ordnerlage:** Das Projekt erwartet, dass dieses Repository **als Unterordner im Spielordner** liegt. Der Build kopiert die DLL nach `..\Mods\`:
```
<Cube World Alpha>\
    Cube.exe
    Mods\                      <- hierhin kopiert der Build AdventureMod.dll
    Strukturen\                <- Inhalt aus diesem Repository (Ordner Strukturen) hierher kopieren
    CubeAdventure\             <- dieses Repository
        AdventureMod.sln
        AdventureMod\          Quellcode (C++)
        MinHook_134_lib\       MinHook (selbst herunterladen)
        ...
```

**Bauen**
1. MinHook nach `MinHook_134_lib\` legen.
2. `AdventureMod.sln` öffnen, oben **Release | Win32** wählen und erstellen (Strg+Umschalt+B).
3. Den Ordner `Strukturen` aus dem Repository in den Spielordner kopieren.
4. Mit dem Mod-Starter spielen.

**Release-Paket erzeugen:** `Veroeffentlichen.bat` ausführen. Das Skript nimmt `Mods\AdventureMod.dll` und `Strukturen\` aus dem Spielordner, lässt Arbeits- und Weltdateien weg, prüft die DLL (kein Debug-Build, keine persönlichen Pfade, kein d3dx9) und die Texte (keine E-Mail-Adressen, Schlüssel oder Benutzerpfade) und schreibt `Veroeffentlichung\CubeAdventure_<Datum>.zip`.

---

## Aufbau des Repositorys

| Ordner | Inhalt |
|---|---|
| `AdventureMod/` | Quellcode der DLL (C++17, 32 Bit). Einstieg `main.cpp`, Übersicht in `Doku/MODUL_UEBERSICHT.md` |
| `Strukturen/` | alle Inhalte: Biome, Stadt, Mobs mit Teilen und Tierwelt, Skills, Rezepte, Händler, Gegenstände, Dungeons, Zonenmodelle, Texte, `Grundeinstellungen/` |
| `Doku/` | Anleitungen (LAB, Biome, Server), `LIESMICH.txt` für das Release, Lizenzen |
| `RE/` | Notizen zur Analyse der `Cube.exe` (Adressen, Abläufe) und Ghidra-Skripte |
| `Werkzeuge/` | Python-Skripte, die Inhalte erzeugen (Tiere, Beine und Gelenke, Dungeons, Stadtmodelle). Zum Spielen und Bauen nicht nötig |
| `MinHook_134_lib/` | Platz für MinHook (nicht enthalten) |

---

## Lizenz

Eigener Code und eigene Inhalte: **MIT**, siehe [`LICENSE`](LICENSE). Fremde Bestandteile: siehe [`THIRD_PARTY.md`](THIRD_PARTY.md).

Cube World ist ein Spiel von Picroma. Dieses Projekt enthält keine Spieldateien und ist kein offizielles Produkt.
