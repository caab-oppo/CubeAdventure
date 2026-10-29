#pragma once
// ============================================================================
// Dungeons.h -- Dungeon-Baukasten (2026-10-19, Weg D aus RE_Dungeons.md)
//
// FORMAT 2 (2026-10-27): Konstruktionselemente. Ein Dungeon ist ein Ordner Strukturen\Dungeons\<Name>\ mit
//   dungeon.txt        format=2, typ=name,breite,hoehe,wandstaerke,r,g,b (Durchgangsarten), module=min,max,
//                      flaeche_min/max, hoehe_max, bewohner, boss, monster_anteil, zinnen, uneben ...
//   Module\*.cub       Bauteile beliebiger Groesse
//   Module\*.txt       art (raum|eingang|boss), gewicht, max, dach, anschluss=typ,richtung,u,z (mehrfach)
// Der Generator setzt ab dem Eingang Modul an Modul (Anschluss an passenden Anschluss, gedreht, ohne Ueberlappung),
// bis die Zielgroesse erreicht ist (sonst neuer Versuch), haengt den Boss an den tiefsten offenen Anschluss und
// mauert alle offenen Anschluesse zu. Obere Ebenen ruhen auf Fundament. Vor dem Eingang entsteht eine Treppe bis
// auf das Gelaende. Markerfarben im Modul werden zu Inhalt: Monster/Boss/Vase als natives cube::Spawn (Zone+0x18,
// GUID wie das Spiel 0x5C0120), Truhe als Objekt 0x0A mit Beute wie nativ (0x5F7A60 + 0x46EBE0), Licht als
// Leuchtblock (0x0D). Der ganze Dungeon liegt in EINER Zone und entsteht mit ihr (Hook_SunCall -> ZonePlaceAll).
// ============================================================================
#include "cwsdk.h"
#include <string>
#include <vector>
#include <functional>

namespace Dungeons {
	// Markerfarben in Raum-Modellen (werden beim Setzen zu Luft + Inhalt)
	const uint8_t kMonster[3] = { 254, 1, 254 }, kBoss[3] = { 254, 1, 128 }, kChest[3] = { 254, 128, 1 },
	              kVase[3] = { 128, 1, 254 }, kLight[3] = { 254, 254, 1 };

	void Reload();                                    // jeder Thread
	void Tick(cube::GameController* gc);              // Spielthread: eigene Mobs der Bewohnerlisten vorbereiten
	// Zonen-Thread (BiomeMod::ZonePlaceAll): einen Dungeon in die Zone setzen. surf = Bodenhoehe/Typ einer Spalte,
	// free = Rechteck frei (keine Orte/anderen Bauten). true = gesetzt, Rechteck in out*.
	typedef bool (*SurfaceFn)(void* zone, int x, int y, int* z, uint8_t* t);
	bool ZonePlace(const void* world, void* zone, int zx, int zy, uint32_t seed, SurfaceFn surf,
	               const std::function<bool(int x0, int y0, int x1, int y1)>& free, int* ox0, int* oy0, int* ox1, int* oy1);
	// LAB: Dungeon 'name' 40 Bloecke vor dem Spieler (Spielthread)
	std::string PlaceHere(cube::GameController* gc, const std::string& name, uint32_t seed);
	std::vector<std::string> Names();
	std::string Status();
	std::string Dir();                                // "Strukturen\\Dungeons"
}
