#pragma once
//
// Wegfinder.h
// ---------------------------------------------------------------
// Ein universelles Navigationssystem fuer Entitaeten in Cube World.
// Liest den World-Seed live aus dem Speicher und berechnet reale
// Dorfkoordinaten fuer eine dynamische Wegpunkt-Navigation.
//

#include "cwsdk.h"
#include <vector>
#include <cstdint>
#include <windows.h>

namespace Wegfinder {

	// Definiert ein einzelnes Ziel auf der Route
	struct Waypoint {
		int64_t x, y, z;
		int waitTimeMs;
	};

	// Speichert den Status der aktuellen Wegfindung (pro Kreatur/Schiff)
	struct AIState {
		int targetWaypointIndex = 0;
		bool isWaiting = false;
		DWORD waitStartTime = 0;
		double heading = 0.0; // Fahrtrichtung in Radiant (wie PirateShip)
	};

	// Scannt das 3x3 Regionen-Grid um den Spieler, sortiert per nearest-neighbor
	std::vector<Waypoint> GenerateVillageRoute(cube::GameController* gc, int64_t flightHeightUnits, int waitTimeMs);

	// Berechnet den naechsten Schritt und aktualisiert Position, Geschwindigkeit und Rotation
	void TickMovement(AIState& ai, const std::vector<Waypoint>& route,
		int64_t& posX, int64_t& posY, int64_t& posZ,
		float& velX, float& velY, float& velZ, float& rot,
		double speedBps, double turnRadPerSec, unsigned int dt);
}
