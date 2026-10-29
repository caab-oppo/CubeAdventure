#pragma once
//
// Lan.h -- Mehrspieler im lokalen Netz (Stufe 1, 2026-10-13)
// ---------------------------------------------------------------
// Cube World Alpha hat nativen Mehrspieler: Server.exe (Spielordner) + im Spiel "Multiplayer worlds..." ->
// "Connect to server" (Adresse). Stufe 1: das LAB startet/beendet Server.exe auf diesem Rechner und zeigt die
// LAN-Adressen, unter denen die anderen verbinden. GRENZEN (belegt durch die Strings in Server.exe): der Server
// fuehrt eine EIGENE Welt (kein "Save/world_", kein Seed-Eintrag) -- die Einzelspieler-Welt wird (noch) nicht
// geteilt; Stufe 2 (Seed/Welt des Gastgebers an den Server) braucht Reverse Engineering von Server.exe.
// Alle Mitspieler brauchen den Mod mit denselben Strukturen (Biome, Staedte entstehen auf jedem Rechner selbst).
//
#include <string>
#include <vector>
namespace Lan {
	void SetWorldSeed(unsigned seed);      // Spielthread: Seed der geladenen Welt (World+0x800188)
	// Server.exe starten (minimiert). Vorher wird server.cfg = Seed der geladenen Welt geschrieben (Format belegt durch
	// cw-docker: die Datei enthaelt nur die Zahl) -> der Server erzeugt dieselbe Landschaft wie die eigene Welt.
	bool Start(std::string& msg);
	void Stop();                           // nur den selbst gestarteten Server beenden
	bool Running();
	std::vector<std::string> LocalAddresses();
	std::string Status();
}
