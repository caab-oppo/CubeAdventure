# AdventureMod als Server-Mod

**Kurz:** Dieselbe `AdventureMod.dll` läuft im Spiel (Cube.exe) und im Server (Server.exe). Sie erkennt selbst, wo sie geladen wurde. Im Server läuft nur die Welterzeugung (Biome, Städte, Tempel, Zonen-Modelle). Alles mit Bildschirm (LAB, HUD, Skills, Shader, Menüs) bleibt im Client.

## Installation (coremaze Server-Mod-Launcher)

1. Den Launcher von coremaze (`Cube-World-Server-Mod-Launcher`) und seine `ServerCallbackManager.dll` nach dessen Anleitung neben `Server.exe` legen.
2. Neben `Server.exe` einen Ordner `Server_Mods` anlegen und `AdventureMod.dll` hineinkopieren.
3. Der Ordner `Strukturen\` und die Mod-Schalter müssen neben `Server.exe` liegen, also im Spielordner. Der Mod stellt das Arbeitsverzeichnis selbst auf diesen Ordner.
4. Den Server über den Launcher starten. Das Protokoll steht in `AdventureMod_server_log.txt` neben `Server.exe`.

Der Launcher prüft die Größe der Server.exe (1 718 784 Bytes). Unsere Server.exe hat genau diese Größe.

## Was im Server läuft

| Teil | Server | Client |
|---|---|---|
| Biome: Klima, Höhe, Boden, Fels, Flüsse, Bäume, Rassen | ja | ja |
| Städte/Viertel, Felder, Gärten, Gehege (Zonen-Objekte) | ja | ja |
| Kleine Tempel, Zonen-Modelle, Zonen-Teiler, Runensteine | ja | ja |
| LAB, HUD, Skillfenster, Tasten, Chat, Shader | nein | ja |
| Händler-Menüs, Bewohner, Flugtiere, Beschwören | nein | ja |
| Native Bäume als Vorlage für vergrößerte Stadtbäume | nein (kein Spielgrafik-Import) | ja |

## Die Übersetzungslogik (Cube.exe → Server.exe)

Alle Spieladressen im Quelltext sind Cube.exe-Adressen. `Host::Re(adresse)` rechnet sie zur Laufzeit um. Im Client gilt wie bisher Basis + Abstand. Im Server liest `Host::Re` die Tabelle `AdventureMod\AddrMap_Server.inc`.

Die Tabelle wird automatisch erzeugt (nichts von Hand):

1. **Signatur:** Die Instruktionen ab der Adresse werden zur Bytesignatur, wobei Sprungziele und absolute Adressen ausgeblendet werden. Gibt es genau eine Fundstelle in der Server.exe, ist das die Adresse.
2. **Ausbreitung:** Jedes gesicherte Paar wird in beiden Programmen gleichzeitig disassembliert. Solange die Instruktionen gleich sind, entsprechen sich auch ihre Aufrufziele und Datenadressen. Neue Paare werden nur angenommen, wenn ihre ersten 24 Instruktionen ebenfalls übereinstimmen.
3. **Widersprüche** werden verworfen, in beide Richtungen: Zeigt eine Client-Adresse auf zwei Server-Adressen oder zwei Client-Adressen auf eine, wird kein Eintrag erzeugt.

Ergebnis: 125 von 218 Adressen haben ein Server-Gegenstück. Davon sind alle Adressen der Welterzeugung (die 9 Biom-Hooks, Zonen-Anker 0x5EDE84, Fluss-Patch, Objekt-Push, Spaltendaten …). Bei der Welterzeugung hat jede gefundene Adresse im Server denselben Abstand (0xCC220), was die Treffer gegenseitig bestätigt. Die übrigen 93 sind reine Client-Funktionen wie Grafik, UI, Chat und Werkbank. `Host::Re` gibt dafür 0 zurück, und der Mod lässt das Teil im Server aus. Das steht dann einmal im Protokoll.

### Tabelle neu erzeugen (nach neuen Adressen im Quelltext)

```
pip install capstone
python Werkzeuge\RE\adressen_sammeln.py AdventureMod Werkzeuge\RE\adressen_cube.txt
python Werkzeuge\RE\translate2.py Cube.exe Server.exe Werkzeuge\RE\adressen_cube.txt Werkzeuge\RE\adressen_server_ergebnis.txt AdventureMod\AddrMap_Server.inc
```

Danach die DLL neu bauen. `adressen_server_ergebnis.txt` zeigt je Adresse, wie sie gefunden wurde (signatur, ausbreitung, fehlt, widerspruch).

## Regeln für neuen Code

- Spieladressen nur über `Host::Re(0x…)` oder `Host::ReOff(0x…)`, nie über `GetModuleHandleA(NULL) + …`.
- Byte-Prüfungen von `call`/`jmp` im Server über das Sprungziel vergleichen, nicht über die rohen Bytes (Beispiel: `InstallTreeAnchor`).
- Im Server gibt `cube::GetGameController()` den Wert `nullptr` zurück. Code im Weltgenerierungs-Pfad muss damit umgehen.

## Noch offen

- `OnZoneGenerated` (nachträgliches Stempeln: LAB-Strukturen, Biom-Dungeons, Bergdörfer) hängt im Client an einer Aufrufstelle der Hauptschleife. Im Server ist diese Stelle noch nicht verankert (GenerateZone ist übersetzt: 0x5E4850 → 0x518630).
- Nicht im Spiel getestet. Erster Test: Server über den Launcher starten, das Protokoll prüfen ("Biome: bereit"), dann mit dem Client verbinden.
