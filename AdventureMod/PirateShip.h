#pragma once
//
// PirateShip.h
// ---------------------------------------------------------------
// Baustein 3 des AdventureMod: Piraten auf See (Vorstufe zum fahrenden
// "Dungeon"-Schiff). Siehe Ozean_Piraten_Plan.md.
//
// v1 (dieser Stand): ein Enter-/Ueberfallkommando -- Piraten-Crew + ein
// Kapitaen-Boss erscheinen auf der Wasseroberflaeche (Z=0) und greifen den
// Spieler an. Noch OHNE Schiffsmodell und OHNE autonomes Segeln -- das sind
// die naechsten Schritte (Boot-Entity/boat2.cub + mod-getriebene Fahrt).
//
// BEWUSST voellig getrennt von WanderingMerchant.* und OceanContent.*
// (eigenes IAdventureMod, eigener Namespace).
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <cstdint>

namespace PirateShip {

	// Registriert das Piraten-Modul beim AdventureFramework (aus MainThread).
	void RegisterWithFramework();

	// --- Test-/Labor-API ---
	// Spawnt ein Piraten-Enterkommando (Kapitaen + Crew) auf der Wasser-
	// oberflaeche vor dem Spieler. Am besten auf offener See ausloesen.
	void SpawnPirateShipNearPlayer(cube::GameController* gc);

	// GUID des zuletzt gespawnten Kapitaens (0 = keiner) -- fuers Tracking.
	uint64_t GetCaptainGuid();
}
