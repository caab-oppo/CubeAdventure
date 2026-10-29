#pragma once
//
// PerfTweaks.h
// ---------------------------------------------------------------
// Karte freilegen, Minimap agiler, Spiel fluessiger -- in zwei Schritten.
//
// Alle Eingriffe sind byte-genau gegen die vorliegende Cube.exe geprueft
// (Disassembly mit capstone, siehe RE_KarteUndLeistung.md). Vor jedem
// Schreiben wird verglichen, ob an der Stelle noch exakt das Original steht;
// sonst wird die Gruppe nicht angefasst und im Protokoll gemeldet. Jede
// Aenderung ist zur Laufzeit umschaltbar und vollstaendig zuruecknehmbar.
//
// SCHRITT 1 -- Karte
//   * Abdunklung unentdeckter Kacheln aus      (0x5FEA03: jne -> jmp)
//   * Kartenkachel-Fenster groesser             (0x46999D.., +-10 -> +-R Kacheln)
//   * Landschaftsfenster groesser               (0x4696C0.., +-3 -> +-L Zonen)
//   * Kartenthread adaptiv: schnell in Bewegung, sparsam im Stand
//                                                (0x469BCF: Sleep(10) -> Sleep(n))
// SCHRITT 2 -- Fluessiger
//   * Exakter Bildtakt statt grobem Sleep       (0x4C9456: call [Sleep] -> PreciseSleep)
//   * minTimeStep live einstellbar              (cfg [0x76B1C8]+0x19C)
//   * Chunk-Streaming adaptiv                   (0x4694FE: Sleep(5) -> Sleep(n))
//   * Messung: Bildzeiten, Ruckler, Kosten je Mod-Baustein
//
#include "cwsdk.h"
#include <string>

namespace PerfTweaks {

	struct Settings {
		// ---- Schritt 1: Karte ----
		bool mapNoDarken     = true;   // unentdeckte Kacheln nicht mehr abdunkeln
		// 16 statt 20: jede Kartenkachel ist ein eigenes Mesh im selben 2-GB-Adressraum
		// (siehe landscapeRadius). +-16 = 1024 Kacheln statt 400 bei Vanilla.
		int  mapRadius       = 16;     // Kartenkacheln um den Spieler (Vanilla 10)
		// VANILLA 3 -- NICHT erhoehen ohne "Large Address Aware": Mit 5 stuerzte das
		// Spiel ab (bad_alloc im Kartenthread, loadLandscapeTile -> VoxelModel_build_mesh
		// -> operator new). Cube.exe hat nur 2 GB Adressraum; (2L+1)^2 Landschafts-Meshes
		// (49 bei 3, 121 bei 5) haben ihn erschoepft. Belegt durch den Absturzbericht
		// vom 2026-10-01 21:50 (Ruecksprung 0x46995E = Aufruf 0x469959 in der
		// Landschaftsschleife).
		int  landscapeRadius = 3;
		bool mapAdaptive     = true;
		int  mapSleepMove    = 4;      // ms Pause des Kartenthreads in Bewegung (Vanilla 10)
		int  mapSleepIdle    = 15;     // ms Pause im Stand
		// ---- Schritt 2: Fluessiger ----
		bool precisePacing   = true;   // exakter Bildtakt
		int  minTimeStep     = -1;     // -1 = Wert aus options.cfg lassen; sonst ms je Bild
		bool streamAdaptive  = true;
		int  streamSleepMove = 1;      // ms Pause des Chunk-Threads in Bewegung (Vanilla 5)
		int  streamSleepIdle = 5;
	};

	void Install();                                // einmal nach InitGlobals
	void Tick(cube::GameController* gc);           // jedes Frame (Spielthread)

	Settings Get();                                // threadsicher
	void Set(const Settings& s);                   // threadsicher; wirkt im naechsten Frame
	void SetEnabled(bool on);                      // alles an/aus (aus = Vanilla-Bytes)
	bool IsEnabled();

	// Mehrzeiliger Statusbericht fuer das LAB (Gruppen, Bildzeiten, Kosten je Baustein).
	std::string Report();
}
