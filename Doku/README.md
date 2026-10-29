# AdventureMod — zweiter Mod-Ansatz auf Basis von cwsdk

Dieses Projekt ist ein **zweiter, eigenstaendiger Ansatz** neben dem bisherigen
`VillageDungeonTraderMod` (MinHook + `GenericMod`-Loader, siehe
`../Mods/VillageDungeonTraderMod/docs/RE-Notes.md`). Es baut auf einer sehr
viel weiter fortgeschrittenen Rechercheschiene auf, die der Nutzer aus einer
parallelen Session eingebracht hat (`ROADMAP.md`-Auszuege, `AdventureModFramework.*`,
`main.cpp` als Ausgangsmaterial). Diese Datei fasst zusammen, was daraus
gebaut wurde, was verifiziert ist und was noch offen ist — **ehrlich, ohne
etwas als sicherer darzustellen, als es ist.**

## Warum ein zweites Projekt statt eine Erweiterung des ersten?

Die beiden Ansaetze unterscheiden sich strukturell:

| | `VillageDungeonTraderMod` | `AdventureMod` (dieses Projekt) |
|---|---|---|
| Mod-Loader | eigener `GenericMod`/`MakeMod()`-Loader (`../ModLoader/`) | eigenstaendige DLL, injiziert sich selbst über `DllMain` |
| Game-Tick-Hook | eigener Hintergrund-Thread (Hotkey-Polling) | D3D9 `EndScene`-Hook (MinHook) |
| Struct-Layouts | einzelne, von Hand nachgerechnete Offsets in `Mod.cpp` | vollstaendig vendorierter `cwsdk`-Header-Baum (`cwsdk/`) |
| Kreatur-Spawn | eigene, minimal-invasive `SafeRegisterCreatureInWorld()` (nur EntitesMap-Insert + Companion-Init) | vollstaendiges `cube::util::SpawnCreature()` aus `cwsdk/cube_util.cpp` |
| Zonen-Hook | nicht vorhanden | `GenerateZone`-Callsite-Patch (`AdventureModFramework`), fuer spaetere Dorf-/Dungeon-Erkennung |

Beide Projekte greifen auf **dieselbe Cube.exe** zu und ihre Offsets
bestaetigen sich gegenseitig (siehe naechster Abschnitt) — es lohnt sich
nicht, sie zu vereinheitlichen, solange beide noch experimentell sind.
Langfristig ist `AdventureMod` der vielversprechendere Unterbau (vendorierter
SDK-Header-Baum statt Einzel-Offsets, echter Zonen-Hook), `VillageDungeonTraderMod`
bleibt das "Labor" fuer risikoreiche Einzelexperimente (Lab-Modus,
Speicher-Dumps).

## Verifikationsstatus der wichtigsten Adressen

**Wichtig:** Dieses Projekt nutzt `ImageBase 0x00400000` (Standard-PE-
Ladeadresse) als Rechengrundlage fuer alle `imageBase + offset`-Adressen in
`cwsdk/cube_util.cpp`, `cwsdk/cube.cpp`, `cwsdk/cube_funcs.cpp` und
`AdventureModFramework.cpp`. Zur Laufzeit relociert Windows das Modul meist
an eine andere Basis (ASLR) — deshalb wird ueberall `GetModuleHandleA(NULL)`
zur Laufzeit als echte Basis genommen, die Konstanten sind aber alle als
**Offset relativ zu `0x400000`** zu verstehen.

| Adresse (rel. `0x400000`) | Bedeutung | Status |
|---|---|---|
| `+0x3B690` (`CreatureCtor`) | `cube::Creature`-Konstruktor | **Verifiziert** (Ghidra-Decompile + Live-Test in beiden Projekten; `VillageDungeonTraderMod` fand denselben Offset unabhaengig per eigenem Ghidra-Dump) |
| `+0x68AD0` (Map-Insert) | Fuegt `Creature*` in `World.EntitesMap` ein | **Verifiziert per Live-Test** (`VillageDungeonTraderMod`-Log: "Kandidat #1 erfolgreich in World.EntitesMap eingetragen", kein Absturz an dieser Stelle) |
| `+0x1EE7A0` (Companion-Init) | Verhaltens-/Bewegungs-Init | **Verifiziert per Live-Test** (behebt reproduzierbaren Absturz bei fehlendem Aufruf, siehe `VillageDungeonTraderMod/docs/RE-Notes.md`) |
| `+0x3F7C0` (Grafik-Init nach Rasse) | Modell/Aussehen setzen | Aus `cube_util.cpp` uebernommen, **nicht eigenstaendig verifiziert** |
| `+0x36B1C8` (GameController-Zeiger) | `*(GameController**)` | **Verifiziert** (`ProbeGameController()` in `VillageDungeonTraderMod`: Fenstergroesse plausibel, `World.local_player`/`GameController.local_player`/eigene GUID==1-Heuristik zeigen alle auf dieselbe Adresse) |
| `+0x6ACD1` (GenerateZone-Callsite) | `CALL`-Instruktion auf `GenerateZone` | **Verifiziert per Hex-Dump** (Byte-Folge `E8 7A 9B 17 00` an dieser Adresse bestaetigt, Ziel `0x5E4850` passt zu ChrisMiuchiz' bekannten Offsets) |
| `+0x1E4850` (`GenerateZone`) | `World::GenerateZone(world*, x, y)` | Wie oben, per Hex-Dump verifiziert |
| `+0x60` (`hostility_flags`) | NPC-Interaktionszustand (`3`="[R] Talk", `5`="[R] Ride") | **Verifiziert per Live-Test** ("[R] Talk" erschien korrekt und war druckbar) |
| `+0x140` (`char_class`) | bei NPCs als Sentinel `0x84`="innkeeper" zweckentfremdet | **Verifiziert per Live-Test** (siehe voriger Punkt) |
| `+0x7e` (Bit `0x80` von `appearance.movement_flags`) | "Shop-Gate": blockiert `FUN_004882e0` komplett, wenn gesetzt | Aus Decompile hergeleitet, **Wirkung noch nicht abschliessend per Test bestaetigt** |
| `+0x80092C` (`vendor_session_pool`) | Pool fuer aktive Haendler-Sitzungen | Aus Decompile + `cwsdk/GameController.h`-Namensabgleich hergeleitet, **Kern-Verhalten ("kein neuer Slot wird angelegt") noch nicht mit `[P]`-Debug-Ausgabe live bestaetigt** |
| `+0x800A70` (`interacting_creature_guid`) | GUID der Kreatur, mit der gerade interagiert wird | Aus `cwsdk/GameController.h` uebernommen, ungeprueft |

**Struct-Groessen-Bugfix (behoben):** Beim ersten echten Build schlugen die
`assert_size(cube::Creature, 0x1E60)`- und `assert_size(cube::Region, 0x15A28)`-
Pruefungen in `cwsdk/cube.h` fehl (`C2338`). Ursachen und Fixes:
- `cube/Creature.h` hatte ein ueberzaehliges `int field_1E5C;` nach
  `other_stuff[2900]` -- `other_stuff` endet bereits exakt auf der erwarteten
  Groesse (nachgerechnet: `0x130C + 2900 = 0x1E60`); das Extra-Feld ergab
  `0x1E68` statt `0x1E60`. Entfernt.
- `cube/Region.h` hatte eine geratene Unterfeld-Aufteilung
  (`Region_SubField1[4096]` + `Zone*[4096]` + `Region_SubField2[64]`), deren
  Kommentar-Formel beim Nachrechnen KEINE ganze Zahl ergab -- schlicht falsch.
  Da `cube::World` nur `Region*`-Zeiger haelt (nie `Region`-Werte) und wir
  keine Region-Felder lesen, wurde die Struct durch einen reinen
  `uint8_t raw[0x15A28]`-Platzhalter ersetzt: garantiert korrekte Groesse,
  ohne erfundene Feldaufteilung vorzutaeuschen.

**Kurz gesagt:** Die *Grundmechanik* (Kreatur spawnen, sichtbar/tickt sie,
[R]-Talk-Prompt erscheint) ist durch echte Ingame-Tests abgesichert. Der
*native Shop-UI-Teil* (oeffnet sich nach [R]-Druck wirklich ein Kaufmenue)
ist es **nicht** — siehe naechster Abschnitt.

## Absturz-Fix (Sicherheits-Umbau)

Ein erster echter Test stuerzte Cube.exe sofort ab ("Zugriffsverletzung beim
Ausfuehren an Position 0x6A8B20DD" -- ein "execute"-Fehler an einer weit
ausserhalb des Cube.exe-Adressraums liegenden Adresse, klassisches Symptom
fuer einen Sprung in eine falsch berechnete/unverifizierte Funktionsadresse).

**Wahrscheinlichste Ursache:** `cwsdk/cube_funcs.cpp` nutzt mehrere Adressen
(`world_getzone` +0x34D10, `world_getblock` +0x2F7E0, `world_setblock`
+0x4E7A0, `chatwidget_print` +0x3AB30 u.a.), die **1:1 unveraendert aus dem
oeffentlichen Andoryuuta/cwsdk-Repo uebernommen** und **nie gegen dieses
lokale Cube.exe-Build verifiziert** wurden (im Gegensatz zu den wenigen
Adressen in der Tabelle oben, die per Ghidra+Live-Test bestaetigt sind).
`world_getzone` wurde bei JEDER neu generierten Zone ueber den neuen
`GenerateZone`-Hook aufgerufen -- also sehr haeufig waehrend des normalen
Spielens. Ein falscher Offset dort passt genau zum beobachteten Absturzbild.

**Durchgefuehrte Aenderungen (verteidigungsorientiert, nicht "beweisend
korrekt" -- es fehlt weiterhin eine unabhaengige Verifikation der
cube_funcs-Adressen):**

1. **SEH-Absicherung (`__try`/`__except`) um praktisch jeden nativen
   Funktionsaufruf**, jeweils in eigene, objektfreie Hilfsfunktionen isoliert
   (MSVC C2712: `__try` darf nicht im selben Frame wie Objekte mit
   Destruktor stehen, z.B. `Vector3<T>` oder `std::string`):
   - `cube_util.cpp`: `CreatureCtor`, `EntitesMap`-Insert, Grafik-Init,
     Companion-Init, sowie `World::Lock()`/`Unlock()` (Unlock wird
     garantiert aufgerufen, auch wenn ein vorheriger Schritt fehlschlaegt --
     verhindert ein dauerhaft gesperrtes World-Lock).
   - `AdventureModFramework.cpp`: Original-`GenerateZone`-Aufruf,
     `World::GetZone()`.
   - `WanderingMerchant.cpp`: der rohe Pointer-Walk im `[P]`-Vendor-Pool-Dump.
   - `main.cpp`: der komplette `DispatchGameTick()`-Aufruf (deckt u.a. den
     Haendler-Spawn ab).
   Ein Absturz in einem dieser Bausteine wird jetzt abgefangen, geloggt
   (Konsole zeigt genau, WELCHER native Aufruf fehlschlug) und der Rest des
   Mods/Spiels laeuft weiter, statt dass Cube.exe komplett stirbt.
2. **Byte-Signatur-Pruefung vor dem CALL-Patch** (`VerifyCallsiteBytes()`):
   prueft vor dem Patchen der `GenerateZone`-Aufrufstelle, ob dort wirklich
   noch `E8 7A 9B 17 00` steht -- verhindert blindes Patchen bei einer
   abweichenden Version.

**Status der Feature-Flags:** `GenerateZone`-Hook (`kEnableZoneHook`) und
Grafik-Init (`g_graphicsInitEnabled`) waren zwischenzeitlich testweise
DEAKTIVIERT, sind aber auf Nutzerwunsch wieder AKTIV -- der Verdacht ist,
dass der urspruengliche Absturz am selbstgebauten Mod-Launcher lag (statt
am zur cwsdk bereitgestellten Launcher) und nicht an den unverifizierten
Adressen selbst. Die SEH-Sicherheitsnetze aus Punkt 1 bleiben in jedem Fall
aktiv -- sollte doch einer der unverifizierten Offsets (`world_getzone`,
Grafik-Init) falsch sein, wird das jetzt abgefangen und geloggt statt das
Spiel abstuerzen zu lassen.

**Wie weiter vorgehen, falls es noch crasht:** Poste die Konsolen-Ausgabe
unmittelbar VOR dem Crash -- die `[SpawnCreature] Rufe ... auf...`- und
`[AdventureModFramework] ...`-Logzeilen zeigen exakt, welcher native Aufruf
zuletzt lief. Falls es tatsaechlich wieder an `world_getzone`/Grafik-Init
liegt (SEH sollte das eigentlich abfangen, ausser der Schaden entsteht schon
VOR dem Rueckkehren aus dem nativen Aufruf, z.B. durch Speicherkorruption),
in `AdventureModFramework.cpp` `kEnableZoneHook = false` bzw. in
`cube_util.cpp` `g_graphicsInitEnabled = false` setzen und neu bauen, um die
beiden Verdaechtigen wieder einzeln auszuschliessen.

## Pet-Konflikt (behoben, Verhalten noch nicht final bestaetigt)

Erster erfolgreicher Lauf mit reaktiviertem Zonen-Hook + Grafik-Init (siehe
"Absturz-Fix" oben) zeigte KEINEN Absturz mehr, aber einen neuen Seiteneffekt:
der gespawnte Haendler ersetzte das bereits aktive Begleiter-Tier des
Spielers ("konnte man vorher mit interagieren"). Ursache: `owner_guid` wurde
bisher auf die Spieler-GUID gesetzt (siehe alter Kommentar im Code), und
`cube_world_init_companion()` (+0x1EE7A0) verwaltet offenbar **pro Spieler
nur einen einzigen aktiven Begleiter-Slot** -- ein neu gespawnter "Begleiter"
mit derselben Besitzer-GUID verdraengt den vorherigen daraus.

Zusaetzlicher Befund aus demselben Testlauf: der `[P]`-Debug-Dump zeigte
`char_class=0x2` statt der von uns gesetzten `0x84` ("innkeeper"-Sentinel) --
die native Companion-/Tick-Logik scheint dieses Feld laufend auf einen
eigenen Wert zurueckzusetzen. Das bedeutet, `char_class=0x84` ist bestenfalls
fuer einen kurzen Moment nach dem Spawn gesetzt, wird aber danach ueberschrieben
-- ein moeglicher Mitgrund, warum `FUN_004882e0` nie einen Vendor-Pool-Slot
fuer unseren Haendler anlegt (siehe naechster Abschnitt): der `innkeeper`-
Zweig dort greift nur bei `char_class == 0x84` zum Zeitpunkt des `[R]`-Drucks.

**Fix (auf Nutzerwunsch):** `owner_guid` beim Haendler-Spawn auf `0` (kein
Besitzer) statt Spieler-GUID gesetzt (`WanderingMerchant.cpp`, `TrySpawn()`).
Das verhindert die Verdraengung des Spieler-Tiers aus dem Begleiter-Slot.
Bekanntes Risiko laut cwsdk-Kommentar: herrenlose Kreaturen (`owner_guid=0`)
koennten aggressiv werden ("attacks anyone") -- wir setzen aber weiterhin
sofort danach `hostility_flags=3` ("talkable"), was das moeglicherweise schon
unterbindet.

**LIVE BESTAETIGT (naechster Testlauf):** Tier bleibt jetzt unangetastet, der
Haendler spawnt als eigenstaendige (menschliche) Person statt das Tier zu
ersetzen. `char_class` blieb diesmal korrekt auf `0x84` stehen (wurde vorher
mit `owner_guid=Spieler` von der Companion-Logik auf `0x2` zurueckgesetzt --
ein weiterer Hinweis, dass die Companion-/Begleiter-Zuordnung dieses Feld
mit-verwaltet). Aggressionsverhalten wurde vom Nutzer noch nicht explizit
rueckgemeldet -- falls der Haendler doch angreift, bitte melden.

## Schild-Experiment (Nutzer-Hypothese, ungetestet)

Nutzer-Idee: vielleicht braucht die native `[R]`-Interaktion (`FUN_00488ee0`)
nahe `zone_static_object`-"Schilder" in der Zone, bevor sie `FUN_004882e0`
aufruft und einen Vendor-Pool-Slot claimt -- unser bisheriger Decompile von
`FUN_004882e0` SELBST zeigt zwar keinen solchen Check, aber der aeussere
Dispatch `FUN_00488ee0` (77KB, siehe "Bekannter offener Punkt" unten) wurde
noch nicht vollstaendig gesehen und koennte das trotzdem tun.

**Umsetzung** (`WanderingMerchant.cpp`, `TrySpawnShopSigns()`,
`kEnableShopSignExperiment = true`): Nach jedem erfolgreichen Haendler-Spawn
werden zwei `zone_static_object`-Eintraege links/rechts vom Haendler in
dessen Zone eingefuegt, mit zwei verschiedenen Kandidaten-`type`-Werten
(`50` und `57`) -- deckt zwei Hypothesen in einem Testlauf ab. **Welcher
`type`-Wert wirklich ein Schild ist, ist unbekannt/geraten** (aus den
geloggten Zonen-Typ-Statistiken gewaehlt: Werte, die nur in grossen/
dorfartigen Zonen mit wenigen Vorkommen auftauchen, anders als die
ueberall vorkommenden `0/65/66/69`-Clutter-Typen oder die massenhaften
`52-55`-Gebaeudeteile-Typen). In `kShopSignTypeCandidates[]` leicht
anpassbar/erweiterbar fuer weitere Test-Iterationen.

**Sicherheitsaspekt (wichtig):** `zone->static_objects` ist ein
`MSVCBinCompat::vector<zone_static_object>`. Dessen `push_back()`/
`reserve()` wuerden bei Bedarf reallozieren -- dabei wuerde UNSER (falscher)
`new`/`delete` auf einem vom SPIEL allozierten Speicherblock aufgerufen
werden (Cross-Allocator-Heap-Korruption, siehe "Absturz-Fix" oben, gleiche
Gefahrenklasse). Deshalb wird hier **nicht** `push_back()` benutzt, sondern
eine eigene `TryAppendStaticObjectNoRealloc()`, die NUR anhaengt, wenn im
Vektor bereits ungenutzte Kapazitaet vorhanden ist (roher Start/End/Cap-
Zeiger-Vergleich, kein `new`/`delete`). Ist die Zone bereits "voll" (End ==
Cap, in der Praxis vermutlich der Regelfall bei fertig generierten Zonen),
wird nichts geschrieben -- ein sicherer Fehlschlag statt eines Risikos, aber
auch eine reale Einschraenkung: **die Hypothese laesst sich mit diesem Ansatz
evtl. gar nicht testen, falls Zonen grundsaetzlich keine Kapazitaetsreserve
haben.** Die Konsole zeigt in jedem Fall klar an, ob ein Schild platziert
wurde oder mangels Kapazitaet uebersprungen werden musste.

**Noch nicht live getestet.** Bitte nach dem naechsten Haendler-Spawn beide
Konsolenzeilen ("Schild #0 ... platziert" bzw. "keine freie Vektor-
Kapazitaet") sowie das Verhalten von `[R]`/`[P]` danach beobachten und
zurueckmelden.

## Bekannter offener Punkt: natives Shop-UI

`FUN_004882e0` (die Funktion, die `[R] Talk` letztlich ausloest) sucht einen
freien Eintrag im `vendor_session_pool`, legt aber **nachweislich nie selbst
einen neuen an** — sie nutzt nur, was bereits im Pool existiert. Wenn dieser
Pool zu Spielbeginn leer ist (oder nur an andere Kreaturen gebundene Eintraege
enthaelt), bekommt unser Haendler nie einen Slot zugewiesen, und es oeffnet
sich kein Menue — trotz korrekt angezeigtem "[R] Talk"-Prompt. Das ist der
Stand, an dem die Recherche (siehe `ROADMAP.md`-Auszuege oben in diesem
Ordner, falls du sie behalten willst) zuletzt stand.

**Auf Nutzerwunsch wurde der eigene Text-Overlay-Shop (vormals Zahlentasten
`[1][2][3]`) entfernt** — es soll ausschliesslich das native `[R]`-Menu
genutzt werden. Das bedeutet aktuell konkret: Solange der Vendor-Pool-Slot
nicht zustande kommt, zeigt das Spiel zwar "[R] Talk" an, aber es gibt
**keine Moeglichkeit, tatsaechlich zu handeln** — das ist eine bewusste
Konsequenz dieser Entscheidung, kein uebersehener Bug. Siehe naechsten
Abschnitt fuer den Weg zu einem funktionierenden nativen Menu.

**Naechster Schritt, falls das weiterverfolgt werden soll:** Mit `[P]`
(`DebugDumpVendorPool()`) ingame pruefen, ob unser Haendler ueberhaupt einen
Pool-Slot bekommt. Falls nicht: nach der (noch unbekannten) Funktion suchen,
die neue Eintraege in `vendor_session_pool` einfuegt — vermutlich Teil der
Dorf-/WorldStructure-Generierung, die noch nicht lokalisiert ist.

**Update nach Live-Test (alle dokumentierten Vorbedingungen erfuellt, Menu
oeffnet trotzdem nicht):** Ein Testlauf mit `hostility_flags=3`,
`char_class=0x84` und geloeschtem `movement_flags`-Gate-Bit -- also allen
Bedingungen, unter denen `FUN_004882e0` laut Decompile (Abschnitt A des
Ghidra-Skripts) einen freien Pool-Slot claimen sollte -- zeigte trotzdem
`backref=0x0` in allen 4 Slots nach [R]-Druck. Zwei moegliche Erklaerungen:
(a) `[P]` wurde gedrueckt, bevor `[R]` ueberhaupt in der Naehe des Haendlers
gedrueckt wurde, oder (b) `FUN_00488ee0` (die Funktion, die `[R]`-Tastendruck
dispatcht und `FUN_004882e0` aufruft) hat eine weitere, bisher unbekannte
Bedingung, die den Aufruf verhindert. `FUN_00488ee0` konnte im letzten
Ghidra-Lauf nicht dekompiliert werden ("Decompile fehlgeschlagen", vermutlich
Timeout bei 77332 Bytes Funktionsgroesse) -- deshalb wurde
`ghidra_scripts/VendorMenuResearch.java` um drei neue Abschnitte erweitert
(siehe "Ghidra-Rechercheskript" unten): gezielte Disassembly-Fenster um die
beiden bekannten `FUN_004882e0`-Aufrufstellen, ein erneuter Decompile-Versuch
mit 300s statt 60s Timeout, und eine grobe Suche nach `key_R`-Verwendung als
unabhaengige Gegenprobe.

**Ghidra-Rechercheskript:** `ghidra_scripts/VendorMenuResearch.java` automatisiert
einen ersten Rechercheschritt dazu (siehe eigener Abschnitt "Ghidra-Rechercheskript"
unten) — es dekompiliert die bekannten Funktionen, listet zusaetzliche Aufrufer
von `FUN_004882e0`, sucht UI-verdaechtige Strings (`buy`, `shop`, `vendor`, ...)
und deren Xrefs, und listet Funktionen, die die Vendor-Slot-Offsets `0x160`
(Waren-Inventar) bzw. `0x188` (Backref) als Immediate-Operand referenzieren.

## Ghidra-Rechercheskript

`ghidra_scripts/VendorMenuResearch.java` — folgt dem im `ROADMAP.md`
beschriebenen Muster von `AdventureModResearch.java` (mehrere klar getrennte
Abschnitte statt eines monolithischen Scans). Ausfuehrung: Skriptordner
`AdventureMod/ghidra_scripts` im Ghidra Script Manager hinzufuegen, dann
`VendorMenuResearch.java` auf die geladene/analysierte `Cube.exe` ausfuehren
(oder headless via `analyzeHeadless ... -postScript VendorMenuResearch.java`).

Was es tut:
- **Abschnitt A:** Dekompiliert `FUN_004882e0`, `FUN_00488ee0`, `FUN_004e4a20`
  und `FUN_004e4bd0` in Textform (fuer schnelles Nachlesen ohne Ghidra-GUI).
- **Abschnitt B:** Listet alle Aufrufer (Xrefs) von `FUN_004882e0` — der
  bekannte ist `FUN_00488ee0`; zusaetzliche, bisher undokumentierte Aufrufer
  waeren der interessanteste Fund (moeglicher zweiter Pfad, der tatsaechlich
  einen neuen Vendor-Slot anlegt).
- **Abschnitt C:** Durchsucht alle als String erkannten Daten im Programm nach
  Hinweisen (`buy`, `sell`, `vendor`, `shop`, `trade`, `merchant`, `wares`,
  `innkeeper`, `buy-back`, `icon-vendor`, `shop-sign`) und listet fuer jeden
  Treffer die Funktionen, die diesen String referenzieren — potenzielle
  UI-Zeichen-Funktionen.
- **Abschnitt D:** Sucht Instruktionen mit Immediate-Operand `0x160`
  (Waren-Inventar-Feld eines Vendor-Slots, siehe `ROADMAP.md`) bzw. `0x188`
  (Backref-Feld) und markiert Funktionen, die BEIDE referenzieren, als
  starke Kandidaten.
- **Abschnitt E (neu):** Disassembliert ein Fenster (50 Instruktionen davor,
  10 danach) um jede der beiden bekannten `FUN_004882e0`-Aufrufstellen
  innerhalb von `FUN_00488ee0` (`0x4906d5`, `0x496a4f`) — zeigt die
  Bedingungsspruenge unmittelbar vor jedem `CALL`, um die zusaetzliche(n)
  Bedingung(en) zu finden, die den Aufruf trotz erfuellter
  hostility_flags/char_class/movement_flags-Vorbedingungen blockieren
  koennten (z.B. Entfernungspruefung, Cooldown, UI-Zustand).
- **Abschnitt F (neu):** Wiederholt den Decompile-Versuch fuer `FUN_00488ee0`
  mit 300 statt 60 Sekunden Timeout (die Funktion ist mit 77332 Bytes sehr
  gross, ein 60s-Timeout kann knapp scheitern).
- **Abschnitt G (neu):** Grobe, bewusst unscharfe Suche nach Instruktionen,
  die den `key_R`-Struct-Offset (`GameController+0xF`, aus `input.key_R`
  hergeleitet) referenzieren — unabhaengige Gegenprobe, ob wirklich nur
  `FUN_00488ee0` auf R-Tastendruck reagiert oder noch eine weitere Funktion
  beteiligt ist.

- **Abschnitt H (neu, wichtigster Fund bisher):** Dekompiliert `FUN_0042c6c0`
  und `FUN_0042f000` -- die beiden Funktionen, die laut Abschnitt E VOR dem
  Aufruf von `FUN_004882e0` an Aufrufstelle #1 (`0x4906d5`) gaten: Zuerst
  liefert `FUN_0042c6c0` ein "aktuelles Interaktionsziel"-Objekt (in ESI).
  Ist dessen Feld `ESI+0x18` gleich `0`, wird der komplette Talk-Zweig
  uebersprungen -- `FUN_004882e0` wird gar nicht erst aufgerufen. Ist
  `ESI+0x25` (Interaktionstyp: `1`=Talk, `2`=?, `5`=Ride) nicht `1`, ebenso.
  Danach wird `ESI+0x8` (vermutlich eine GUID) per `FUN_0042f000` gegen
  `&gc->world` nachgeschlagen -- schlaegt DIESER Lookup fehl, wird
  `FUN_004882e0` ebenfalls nicht erreicht. Das ist der aktuell
  wahrscheinlichste Grund, warum unser Haendler trotz korrekter
  `hostility_flags`/`char_class`/`movement_flags` nie einen Vendor-Pool-Slot
  bekommt: **wir erreichen `FUN_004882e0` auf diesem Codepfad vermutlich gar
  nicht erst**, weil dieser vorgelagerte Lookup fuer unsere raw-gespawnte
  Kreatur fehlschlaegt (moeglicherweise fehlt ihr eine Registrierung, die
  echte, vom Spiel selbst generierte NPCs automatisch bekommen). Das
  `interacting_creature_guid`-Feld (`[P]`-Dump) beweist NICHT, dass dieser
  Lookup erfolgreich ist -- das koennte ein unabhaengiger Mechanismus sein
  (z.B. "was wird gerade anvisiert", unabhaengig vom Interaktions-Dispatch).

- **Abschnitt I (neu):** Sucht Xrefs auf das globale Blockier-Flag bei
  `0x76b138` (siehe Abschnitt E-Fund an Aufrufstelle #2) -- Ziel: wer setzt
  dieses Byte auf ungleich 0 und blockiert damit die gesamte Talk-Logik?

  **Update nach tatsaechlichem Decompile:** `FUN_0042c6c0(p)` ist nur
  `*p + 8` (reine Zeiger-Arithmetik, kein eigenes Objekt) -- `ESI` zeigt
  also auf ein Feld innerhalb einer Struktur, deren Adresse VOR unserem
  150-Instruktionen-Fenster in die lokale Variable `[ESP+0x174]` von
  `FUN_00488ee0` geschrieben wird (vermutlich das Ergebnis eines
  Raycasts/einer Zielauswahl). `FUN_0042f000(this, guidPtr)` ist im Kern
  ein **`EntitesMap.find(guid)`** (`this+4` = `World.EntitesMap`,
  `FUN_00477d90` = klassische RB-Tree-Suche, `param_1[6]` = Value-Feld
  `Creature*` im gefundenen Knoten) -- **dieselbe Map**, in die unser
  Haendler bereits nachweislich erfolgreich eingetragen wird. Der Lookup
  SELBST sollte also fuer unseren Haendler funktionieren. Der Verdacht
  verschiebt sich damit auf die Stelle VOR diesem Fenster: befuellt das
  Spiel `[ESP+0x174]` (vermutlich per Raycast/Zielauswahl) fuer unseren
  rohen Haendler korrekt mit seiner GUID, oder fehlt ihm dafuer etwas
  (z.B. Kollisions-/Hitbox-Setup, das echte, vom Spiel generierte NPCs
  automatisch bekommen)? Deshalb wurde das Disassembly-Fenster in
  Abschnitt E von 50 auf 150 Instruktionen vor der Aufrufstelle vergroessert.

  **Update nach dem 150-Instruktionen-Fenster (wichtigster Fund bisher):**
  Aufrufstelle #1 zeigte weiterhin nur vorgelagerte Setup-Aufrufe (Text-
  Formatierung/Logging), keine erkennbare "Zielauswahl". Aufrufstelle #2
  (`0x496a4f`) war dafuer sehr ergiebig -- dieser Pfad laeuft ueber
  `gc->interacting_creature_guid` (per `FUN_0042f000` gegen `&gc->world`
  nachgeschlagen), das laut `[P]`-Dump bei uns bereits korrekt auf den
  Haendler zeigt. Drei weitere Gates wurden identifiziert:
  1. **`GameController+0xF` = `input.key_R`** (exakt unser eigenes
     Struct-Feld!) -- der Aufruf feuert nur, wenn `R` GENAU in diesem Frame
     als gedrueckt erkannt wird.
  2. **Globales Flag bei absoluter Adresse `0x76b138`** -- ist es ungleich
     0, wird die GESAMTE Talk-Logik uebersprungen. Herkunft/Bedeutung noch
     unbekannt.
  3. **`GameController+0x800940`** (jetzt als `cached_vendor_session` in
     `GameController.h` benannt) -- ein gecachter "aktuell aktive Vendor-
     Session"-Zeiger. Ist er beim `[R]`-Druck noch gesetzt/gueltig, wird er
     in DIESEM Frame nur GELEERT -- `FUN_004882e0` wird dann NICHT
     aufgerufen! Erst im naechsten Frame (Cache jetzt leer) wuerde der
     eigentliche Claim-Aufruf passieren.

  Alle drei sind jetzt direkt live sichtbar: `[P]` (`DebugDumpVendorPool()`)
  zeigt zusaetzlich `input.key_R`, das globale Blockier-Flag und
  `cached_vendor_session`. **Praktischer Test-Tipp:** `[R]` gedrueckt HALTEN
  und waehrend R noch gehalten wird `[P]` druecken, um `key_R=JA` zu sehen --
  vorher zeigte `[P]` es vermutlich als "nein", weil zwischen Loslassen von
  `[R]` und Druecken von `[P]` bereits wieder ein neuer, R-loser Frame lief.

Ausgabe erscheint in der Ghidra-Konsole UND wird zusaetzlich als
`vendor_menu_research_output.txt` neben dem Skript abgelegt.

**Ehrlicher Hinweis:** Das Skript *findet Kandidaten*, es *beweist nichts*.
Abschnitt D insbesondere kann falsch-positive Treffer liefern, da `0x160`/`0x188`
kleine, generische Offsets sind, die auch in voellig unabhaengigen Strukturen
vorkommen koennen. Jeder Treffer muss von Hand in der Ghidra-Decompile-Ansicht
geprueft werden.

## Architekturentscheidung: kein eigener Bewegungs-Code

Fruehere Versionen dieses Ansatzes gaben dem Haendler jeden Frame selbst eine
neue Position ("Wander"-Logik). Das kollidierte mit dem nativen
"Companion"-Verhaltenssystem, das `cube_world_init_companion()` (aufgerufen
am Ende von `SpawnCreature()`) der Kreatur ebenfalls mitgibt — beide haben
sich gegenseitig ueberschrieben ("Teleport-Bug"). **Fix:** Die Kreatur
bekommt ihre Position nur einmal beim Spawnen; danach bewegt sie sich
ausschliesslich ueber ihre eigene, vom Spiel mitgelieferte Verhaltens-KI
(dieselbe, die auch bei wilden Tieren laeuft). Kein eigener Tick-Code mehr in
`WanderingMerchant.cpp` schreibt in `entity_data.position`.

## Aufbau

```
AdventureMod/
  cwsdk/                    -- vendorierter cwsdk-Header-/Source-Baum (Andoryuuta/cwsdk)
    cube/                   -- Creature.h, World.h(+.cpp), GameController.h, Zone.h, ...
    msvc_bincompat/         -- MSVC-ABI-kompatible string/wstring/map/vector-Layouts
    plasma/Node.h           -- Minimal-Stub (nur Zeiger-Typ, keine echten Felder)
    cube.h/.cpp             -- Aggregator + GetGameController()/InitAPI()
    cube_util.h/.cpp        -- cube::util::SpawnCreature()/SpawnMonster()
    cube_funcs.h/.cpp       -- kleinere Helfer-Funktionszeiger (World::GetZone() usw.)
    globals.h/.cpp          -- gravity/hwnd/input_enabled
  framework.h               -- minimaler PCH-Stub
  AdventureModFramework.h/.cpp -- IAdventureMod-Basisklasse, GenerateZone-Hook
  WanderingMerchant.h/.cpp  -- Baustein 1: wandernder Haendler
  main.cpp                  -- DllMain, D3D9-EndScene-Hook, Tastenkuerzel
  ghidra_scripts/
    VendorMenuResearch.java -- Recherche-Skript fuer die native Vendor-Menue-UI
  scripts/
    build.bat / clean.bat   -- MSBuild-Wrapper (findet MSBuild via vswhere)
  .vscode/
    tasks.json / c_cpp_properties.json / launch.json / extensions.json / settings.json
  AdventureMod.sln          -- eigenstaendige Solution nur fuer dieses Projekt
  AdventureMod.vcxproj
```

## Build-Voraussetzungen

- Visual Studio 2022, Werkzeugsatz v143, Zielplattform **Win32 (x86)** — Cube.exe ist 32-Bit.
- MinHook (bereits im Solutions-Ordner unter `MinHook_134_lib/`, gleiche Kopie wie `VillageDungeonTraderMod`).
- **Legacy DirectX SDK (Juni 2010)** fuer `d3dx9.h`/`d3dx9.lib` (Text-Overlay-Schrift). Ist NICHT Teil des aktuellen Windows-SDK mehr. Nach Installation liegt `d3dx9.h` typischerweise unter `%DXSDK_DIR%Include`; das Projekt liest diese Umgebungsvariable automatisch (siehe `AdventureMod.vcxproj`, `DXSDKIncludeDir`/`DXSDKLibDir`). Falls die Variable fehlt, per Benutzermakro in den Projekteigenschaften setzen.
- `AdventureMod.sln` ist eine **eigenstaendige Solution nur fuer dieses Projekt** — einfach doppelklicken/oeffnen, kein manuelles Hinzufuegen zu `../CubeWorldAlphaMegamod.sln` noetig. (Die beiden Solutions sind unabhaengig voneinander; `AdventureMod` wurde bewusst NICHT in die Haupt-Solution eingetragen, um deren bestehende Projekt-GUIDs/Konfiguration nicht ungetestet anzufassen.)

## VS Code Projekt-Setup

Der Ordner `.vscode/` enthaelt eine fertige VS-Code-Konfiguration, damit du
nicht manuell eine "Developer Command Prompt" oeffnen oder die `.vcxproj`
per Hand in Visual Studio bauen musst:

- **`tasks.json`** — vier Build-Tasks (`Strg+Shift+B` fuer den Standard-Task
  "AdventureMod: Build (Debug|Win32)"): Build Debug, Build Release, Clean
  Debug, Clean Release. Rufen `scripts/build.bat`/`scripts/clean.bat` auf.
- **`scripts/build.bat`** / **`scripts/clean.bat`** — finden `MSBuild.exe`
  selbststaendig ueber `vswhere.exe` (wird von jedem VS2022-Installer,
  auch den kostenlosen "Build Tools fuer Visual Studio 2022", mitinstalliert)
  und bauen/reinigen `AdventureMod.sln` fuer `Win32`. Funktionieren aus
  jedem Terminal heraus, nicht nur aus einer Developer-Shell.
- **`c_cpp_properties.json`** — IntelliSense-Konfiguration (Include-Pfade
  fuer `cwsdk/`, `MinHook_134_lib/include`, `%DXSDK_DIR%Include`; passende
  Defines/Standard/`windows-msvc-x86`), damit Autovervollstaendigung und
  Fehlermarkierungen im Editor korrekt funktionieren.
- **`launch.json`** — Debugger-Konfiguration "An Cube.exe anhaengen"
  (`cppvsdbg`, `request: attach`). Nach dem Injizieren der gebauten DLL in
  ein laufendes `Cube.exe` kannst du damit per `F5` einen Breakpoint-Debugger
  anhaengen (Prozessauswahl-Dialog erscheint automatisch). Setzt PDB-Symbole
  voraus, die `AdventureMod.vcxproj` bereits erzeugt (`GenerateDebugInformation`).
- **`extensions.json`** — empfiehlt die Erweiterung `ms-vscode.cpptools`
  (liefert IntelliSense UND den `cppvsdbg`-Debugger; ohne sie funktionieren
  weder Tasks-Fehlermarkierung noch `launch.json`).

**Voraussetzung:** Visual Studio 2022 mit der Workload "Desktopentwicklung
mit C++" (oder die eigenstaendigen "Build Tools fuer Visual Studio 2022")
muss installiert sein — VS Code selbst bringt keinen C++-Compiler mit, nutzt
hier aber MSBuild/`cl.exe` aus einer vorhandenen VS-Installation. `DXSDK_DIR`
muss weiterhin als Umgebungsvariable gesetzt sein (siehe "Build-Voraussetzungen"
oben) — sonst schlaegt der Build mit einem `d3dx9.h`-Fehler fehl.

**Nutzung:** Ordner `AdventureMod/` in VS Code oeffnen (nicht die gesamte
Solution) → beim ersten Start `ms-vscode.cpptools` installieren, falls VS
Code das nicht automatisch vorschlaegt → `Strg+Shift+B` zum Bauen.

## Injektion

**Wichtig:** Diese DLL ist **kein** `GenericMod` (kein `MakeMod()`-Export) und
wird deshalb vom bestehenden `../ModLoader/` NICHT erkannt/geladen. Sie
injiziert sich beim Laden selbst per `DllMain` → eigener Thread → D3D9-Hook.
Zum Einschleusen also einen normalen DLL-Injector benutzen (z.B. den bereits
vorhandenen Drittanbieter-Weg aus der urspruenglichen `BUILD_INSTRUCTIONS.md`,
oder — falls du den Quellcode von `AdventureModLauncher.exe` noch hast/mir
gibst — dessen eigenen `VirtualAllocEx`/`CreateRemoteThread`-Loader).

## Bedienung (Stand dieses Builds)

- Wandernder Haendler spawnt automatisch in Spielernaehe (alle ~20s, 20% Chance, normalerweise Ring 20-45 Bloecke um den Spieler). Wurde in einer generierten Zone ein `zone_static_object` mit `type==0x84` gesehen und liegt es hoechstens 80 Bloecke vom Spieler entfernt, spawnt der Haendler stattdessen bevorzugt dort (Ring 5-15 Bloecke) — siehe naechster Punkt fuer den ehrlichen Stand dieser Erkennung.
- `[R]` sollte "Talk" anzeigen, wenn du den Haendler anvisierst (nativer Interaktions-Hinweis) — ob sich dabei auch ein natives Shop-Menu oeffnet, ist der oben beschriebene offene Punkt. **Kein eigenes Text-Overlay/Kauf-Fallback mehr** (auf Nutzerwunsch entfernt) — solange das native Menu nicht oeffnet, ist der Haendler aktuell nur sichtbar, nicht nutzbar.
- `[F9]` schaltet eine Koordinaten-Debug-Anzeige (Spieler- und Haendlerposition) ein/aus.
- `[U]` dumpt das Spieler-Inventar direkt aus dem SDK (Rohdaten + gefilterte Ansicht).
- `[P]` dumpt den nativen Vendor-Session-Pool (siehe "Bekannter offener Punkt" oben).
- Jede neu generierte Zone wird mit einer Haeufigkeitstabelle ihrer `zone_static_object::type`-Werte geloggt — Rohmaterial fuer die weiterhin offene Dorf-/Gebaeude-Erkennung. Der zugrundeliegende `GenerateZone`-Hook ist wieder aktiv (siehe "Absturz-Fix" oben), aber SEH-abgesichert.

## Bekannte Einschraenkungen (bewusst dokumentiert)

1. **Keine echte Dorf-Erkennung.** Der Spawn-Ring um den Spieler ist ein Platzhalter. Der Gebaeude-Anker-Mechanismus (`g_lastInnkeeperPos`, `type==0x84`) ist aus den bereitgestellten Daten uebernommen, greift aber laut bisherigen Live-Tests praktisch nie (siehe "Bekannter offener Punkt" oben) — er ist im Code aktiv, aber kein verlaesslicher Ersatz fuer echte Dorf-Erkennung.
2. **Kein Handel moeglich, solange der native Vendor-Pool-Slot fehlt** (siehe "Bekannter offener Punkt"). Es gibt bewusst KEIN eigenes Text-Overlay/Kauf-Fallback mehr.
3. **Kein sauberes Despawnen.** Es gibt noch keine verifizierte "Kreatur entfernen"-Funktion — der Haendler wird nach 15 Minuten stattdessen weit nach unten verschoben (bleibt technisch im `EntitesMap`).
4. **Grafik-Init-Adresse (`+0x3F7C0`) ungeprueft**, siehe Tabelle oben — falls sich das Aussehen des Haendlers falsch/wie der Spieler zeigt, ist das der erste Verdaechtige.

## Naechste Schritte

1. **Prioritaet, da es jetzt der einzige Weg zum Handeln ist:** `[P]` ingame druecken, sobald ein Haendler aktiv ist, und die Vendor-Pool-Ausgabe auswerten (siehe "Bekannter offener Punkt"). Falls dort steht, dass kein Slot fuer unseren Haendler existiert, oeffnet sich das native Menu mit hoher Wahrscheinlichkeit nie -- dann muss zuerst die Pool-Slot-Erzeugung geloest werden, bevor der Haendler ueberhaupt nutzbar ist.
2. Falls der Pool tatsaechlich nie einen Slot fuer unsere Kreatur anlegt: die Funktion suchen, die neue Pool-Eintraege erzeugt (vermutlich Teil der noch nicht lokalisierten Dorf-/WorldStructure-Generierung).
3. `OnZoneGenerated`-Log ueber mehrere echte Doerfer sammeln, um doch noch ein verlaessliches `zone_static_object::type` fuer Haendler-/Gasthaus-Gebaeude zu finden (die fruehere `0x84`-Hypothese dafuer ist widerlegt).
4. Mittelfristig: eigene `AdventureModLauncher.exe` (falls Quellcode vorhanden/nachreichbar) bauen, damit diese DLL nicht mehr auf einen Drittanbieter-Injector angewiesen ist.
