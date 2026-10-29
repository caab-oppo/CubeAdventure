#pragma once
//
// OceanContent.h
// ---------------------------------------------------------------
// Baustein 2 des AdventureMod: mehr Leben in den Ozeanen.
//
// Stufenplan (siehe Ozean_Piraten_Plan.md im Hauptordner):
//   Phase 1  Fische in Ozeanen spawnen  <-- HIER
//   Phase 2  eigene, angepasste Kreaturen (Level/Verhalten/Seltenheit)
//   Phase 3  fahrende Piratenschiffe (bewegter "Dungeon" auf See)
//
// BEWUSST voellig getrennt von WanderingMerchant.* -- eigenes IAdventureMod,
// eigener Namespace. So kann der Haendler nicht beschaedigt werden.
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <cstdint>

namespace OceanContent {

	// Registriert das Ozean-Modul beim AdventureFramework. Einmal aus
	// MainThread() aufrufen (nach AdventureMod::RegisterWithFramework()).
	void RegisterWithFramework();

	// --- Test-/Labor-API (aus dem [L]-Menu in main.cpp aufgerufen) ---

	// Spawnt sofort einen Fischschwarm rund um den Spieler (zum Testen -- man
	// sollte dafuer im/ueber Wasser stehen).
	void SpawnFishSchoolNearPlayer(cube::GameController* gc);

	// Periodisches Auto-Spawnen von Fischen in Spielernaehe an/aus. Gedacht,
	// um es einzuschalten, waehrend man ueber den Ozean segelt/schwimmt.
	void ToggleAutoFish();
	bool IsAutoFishEnabled();

	// Diagnose: spawnt eine Reihe Fische (je einer pro hostility-Wert 0..7)
	// nebeneinander, damit man live sieht, welcher Wert schwimmt UND
	// angreifbar ist. Ergebnis dann in kFish[] eintragen.
	void SpawnHostilityTest(cube::GameController* gc);

	// Phase 1b: automatisches Befuellen erkannter Ozean-Zonen (in der
	// Weltgenerierung) an/aus.
	void ToggleOceanGen();
	bool IsOceanGenEnabled();
}
