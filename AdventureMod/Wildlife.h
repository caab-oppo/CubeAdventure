#pragma once
// ============================================================================
// Tierwelt (2026-10-14, natuerlicher Spawn 2026-10-16): 150 eigene Tiere (Strukturen\Mobs\Tierwelt, erzeugt mit
// Werkzeuge\Mobs\tierwelt.py) gehoeren wie die nativen Tiere zu Zonen (5 x 5 um den Spieler, je Zone fest
// aus Zone + Weltsamen) und werden nach Lebensraum gewaehlt:
//   Land/Luft: Boden der Spalte (Schnee, Sand, Fels, Gras-Farbe, Baumkronen,
//              Wasser in der Naehe) -> wald, wiese, savanne, wueste, dschungel,
//              schnee, gebirge, sumpf, kueste
//   Wasser:    Tiefe und Grund der Wassersaeule -> suess, riff, meer, tief
//   "fantasie": selten ueberall.
// Liste: Strukturen\Mobs\Tierwelt\_tierwelt.txt (datei;medium;lebensraeume;
// gewicht;gruppe_min;gruppe_max). Schalter "tierwelt" (LAB). Nur Spielthread.
// ============================================================================
#include <string>
#include <vector>
#include <cstdint>
#include "cwsdk.h"

namespace Wildlife {
	void Tick(cube::GameController* gc);
	// Am nativen Spawn: BiomeMod ruft OnZoneSpawns am Ende jeder Zonenerzeugung (beliebiger Thread).
	typedef bool (*SurfaceFn)(void* zone, int x, int y, int* zOut, uint8_t* tOut, uint8_t* rgb);
	void SetSurfaceFn(SurfaceFn fn);
	void OnZoneSpawns(const void* world, void* zone, int zx, int zy, uint32_t seed);
	void SetShare(int pct);
	// Biome II: Spawnregeln/Ueberschreibungen fuer die NATIVE Welt (Zonen ohne eigenes Biom); gleiches Format wie *.biom
	std::string NativeProfilePath();       // "Strukturen\\Biome\\_nativ.tiere"   // Anteil der passenden nativen Tiergruppen, die eigene Tiere werden (0..100)
	std::string Status();
	// Tiergruppen (5 je Medium, Spalte 7 in _tierwelt.txt). Biome II: Regel "@land/raubtiere" = ein Tier der Gruppe.
	struct GroupInfo { std::string key, medium, name; int count; };
	std::vector<GroupInfo> GroupList();
}
