#pragma once
//
// Temples.h -- kleine Tempel in der Welt, Tempel-Funde, Teleport ins Biom (Stand 2026-10-13)
// ---------------------------------------------------------------
// Modelle: Strukturen\Tempel_<Art>.cub (Standard, Dschungel, Herbst, Schnee, Wueste, Pastell; vom Nutzer gebaut,
// 64 x 64 x 64, Kugel/Altar in der Modellmitte). BiomeMod setzt sie bei der Zonenerzeugung: je 768er-Rasterfeld
// hoechstens einer, auf ebenem Land, nie in einer Stadt; die Art folgt dem Boden (Schnee, Sand, Herbstlaub, Dschungel,
// Pilz-/Pastellbiom, sonst Standard).
// Fund: [R] an der Kugel eines kleinen Tempels -> gefunden (je Welt gespeichert in AdventureMod_tempel_<Seed>.txt).
// Ab 25 Funden oeffnet [R] an der Kugel eines Stadttempels (Elfen und Zwerge) die Zielliste der eigenen Biome:
// 6 = naechstes Ziel, 4 = teleportieren (BiomeMod::TeleportToNearest: ueber die Mitte der naechsten Zelle).
//
#include "cwsdk.h"
#include <string>


namespace Temples {
	const int kNeeded = 25;
	// Zonenerzeugung (beliebiger Thread)
	int  VariantCount();                                  // geladene Modelle
	int  VariantByName(const char* key);                  // "Schnee", "Wueste", ... -> Index, -1 = fehlt
	bool ModelSize(int v, int* sx, int* sy, int* sz);
	// Modell in die Zone stempeln (untere Ecke ox/oy, Fuss z) und als Fundort anmelden
	bool Stamp(void* zone, int v, int ox, int oy, int z, uint32_t worldSeed);
	// Spielthread
	void Tick(cube::GameController* gc);
	void CityOrb(cube::GameController* gc);               // [R] an der Kugel eines Stadttempels (City.cpp)
	bool ListOpen();                                      // Zielliste offen (Skills lassen 4/6 dann in Ruhe)
	int  Found();
	std::string Status();
}
