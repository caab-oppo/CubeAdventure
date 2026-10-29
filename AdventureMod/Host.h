#pragma once
// ============================================================================
// Host: EIN Mod, zwei Programme.
//
// Die AdventureMod.dll laeuft unveraendert in Cube.exe (Client) UND in
// Server.exe (ueber den Server-Mod-Launcher von coremaze, Ordner Server_Mods).
// Alle Spieladressen im Quelltext sind Cube.exe-Adressen (Ghidra, Basis
// 0x400000). Host::Re() rechnet sie auf das laufende Programm um:
//   Client: Basis + (a - 0x400000)                  (wie bisher)
//   Server: Tabelle AddrMap_Server.inc (Cube -> Server), 0 = fehlt im Server
// Die Tabelle erzeugt Werkzeuge/RE/translate2.py aus Cube.exe + Server.exe
// (Bytesignatur + Gleichschritt-Ausbreitung), nichts davon ist von Hand.
// ============================================================================
#include <cstdint>

namespace Host {
	bool IsServer();                    // laeuft in Server.exe?
	uint32_t Base();                    // Ladeadresse des Hauptprogramms
	uint32_t Re(uint32_t cubeVa);       // Cube.exe-Adresse -> Laufzeitadresse (0 = im Server nicht vorhanden)
	inline uint32_t ReOff(uint32_t cubeRva) { return Re(0x400000u + cubeRva); }   // fuer "imageBase + 0x..."-Stellen
	bool Has(uint32_t cubeVa);          // gibt es die Adresse im laufenden Programm?
	int MappedCount();                  // Eintraege mit Server-Gegenstueck
	int TableSize();
	const char* Name();                 // "Client" / "Server"
}
