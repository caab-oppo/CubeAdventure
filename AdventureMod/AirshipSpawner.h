#pragma once
//
// AirshipSpawner.h
// ---------------------------------------------------------------
// Luftschiffe im Singleplayer: schreibt Airships direkt in die native
// Airship-Map des Clients (gc->world.AirshipMap, GC+0x2F0).
//
#include "cwsdk.h"
#include "AdventureModFramework.h"

namespace AirshipSpawner {
	void RegisterWithFramework();
	void Toggle();
	bool IsEnabled();

	// Gerichteter Einzelflug (Bergdorf-Taxi): Schiff erscheint am Dock (Turm),
	// haelt boardHoldMs (Spieler kann aufspringen), fliegt dann zum Zieldorf und
	// despawnt bei Ankunft. Keine feindliche Crew. dockZ = Deckhoehe am Turm.
	void LaunchTo(cube::GameController* gc,
		int64_t dockX, int64_t dockY, int64_t dockZ,
		int64_t tgtX, int64_t tgtY, unsigned long boardHoldMs);

	// Zwei-Phasen-Taxi:
	//  DockAt  -> Schiff erscheint am Turm und schwebt (beim Ansprechen des NPC).
	//  Depart  -> Ziel setzen + nach delayMs ablegen (beim Kauf einer Richtung).
	// Koordinaten in Positionseinheiten (1 Block = 65536).
	void DockAt(cube::GameController* gc, int64_t dockX, int64_t dockY, int64_t dockZ);
	void Depart(cube::GameController* gc, int64_t tgtX, int64_t tgtY, unsigned long delayMs);
	bool IsBusy();      // laeuft gerade ein Taxi-Flug (angedockt oder unterwegs)?
	bool IsDocked();    // Schiff ist angedockt und wartet auf einen Kauf?
	bool IsCruising();  // Schiff hat abgelegt und fliegt zum Zieldorf?
}