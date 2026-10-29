#pragma once
//
// PirateAirship.h
// ---------------------------------------------------------------
// Phase 3: das Piratenschiff als NATIVES Airship-Entity.
//
// Cube World Alpha hat ein Airship-System (fliegende Fahrzeug-Entities):
//   - gc->world.AirshipMap  (World+0x0C = gc+0x2f0), map<uint64, void*>
//   - Render-Loop (FUN_004b3xxx) iteriert diese Map und zeichnet airship.cub
//     (Asset-ID 0xa03).
//   - Insert = generisches FUN_00468ad0 (this = &AirshipMap, wie Kreatur-Insert).
//
// Idee: ein Airship-Struct anlegen + in AirshipMap einfuegen -> das Spiel
// rendert/bewegt es. Auf Wasserhoehe = Segelschiff, hoch = fliegendes
// Piratenschiff. Siehe Ozean_Piraten_Plan.md.
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <cstdint>

namespace PirateAirship {

	// [STRG+7] SICHER (read-only): dumpt die AirshipMap (gc+0x2f0) -- Zustand,
	// Zeiger, Anzahl, und die rohen Bytes des ersten gefundenen Airships. Damit
	// verifizieren wir das echte Alpha-Struct-Layout, BEVOR wir selbst spawnen.
	void DumpAirshipMap(cube::GameController* gc);

	// [STRG+8] EXPERIMENTELL: baut ein Airship-Struct (0x78-Layout aus dem
	// coremaze-airship-packet) und fuegt es in die AirshipMap ein. KANN
	// ABSTUERZEN, falls das Alpha-Layout abweicht -> nur auf einer Testwelt!
	void SpawnAirshipNearPlayer(cube::GameController* gc);

	void RegisterWithFramework();
}
