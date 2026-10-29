#pragma once
//
// WanderingMerchant.h
// ---------------------------------------------------------------
// Baustein 1 des AdventureMod: ein wandernder Haendler, der automatisch in
// Spielernaehe spawnt und per nativer [R]-Talk-Interaktion einen Shop mit
// klassen-/levelpassenden Waren (abgeleitet aus der Ausruestung des Spielers)
// oeffnet. Der Katalog wird beim Spawn automatisch gebaut.
//
// Details zu den drei Inventar-Bugfixes (Layout { amount; Item }, Adaptionen
// nullen, getrennte Puffer gegen den Deep-Copy-Use-after-free) stehen in
// Einarbeitung_Haendler_Inventar.md im Hauptordner.
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <cstdint>

namespace AdventureMod {

	// ---------------------------------------------------------------
	// Tracking-API (wiederverwendbar, Basis fuer spaeteres Quest-/NPC-Tracking)
	// ---------------------------------------------------------------

	// Weltposition eines Punktes in mehreren Einheiten.
	struct WorldCoord {
		Vector3<int64_t> raw;              // rohe Positionseinheiten (1 Block = 65536)
		int64_t          blockX, blockY, blockZ;
		int              zoneX, zoneY;     // 1 Zone = 256 Bloecke
	};

	// Ergebnis einer Tracking-Abfrage.
	struct TrackResult {
		bool       found;                  // true, wenn die Entity gefunden wurde
		uint64_t   guid;                   // abgefragte GUID
		WorldCoord pos;                    // Position (nur gueltig wenn found)
		double     distanceBlocks;         // Distanz zum lokalen Spieler in Bloecken (-1 = unbekannt)
	};

	// Verfolgt eine BELIEBIGE Entity per GUID: sucht sie (Spieler-/Haendler-
	// Schnellpfad, sonst gc->world.EntitesMap) und liefert Weltposition +
	// Block-/Zonen-Koordinaten + Distanz zum Spieler. SEH-abgesichert.
	TrackResult TrackEntity(cube::GameController* gc, uint64_t guid);

	// Bequeme Kurzformen.
	TrackResult TrackPlayer(cube::GameController* gc);
	TrackResult TrackMerchant(cube::GameController* gc);

	// GUID des aktiven Haendlers (0 = kein aktiver Haendler).
	uint64_t GetMerchantGuid();

	// ---------------------------------------------------------------
	// Mod-Anbindung / Steuerung
	// ---------------------------------------------------------------

	// Registriert den WanderingMerchant beim AdventureFramework. Einmal aus
	// MainThread() aufrufen, VOR AdventureFramework::InstallZoneGeneratedHook().
	void RegisterWithFramework();

	// Debug-Taste [J]: erzwingt sofort einen neuen Haendler-Spawn (ohne auf
	// Cooldown/Zufalls-Chance zu warten). Baut den Katalog automatisch neu.
	void ForceSpawn(cube::GameController* gc);

	// Baut den Vendor-Katalog aus der aktuell getragenen Spieler-Ausruestung.
	// Wird beim Spawn automatisch aufgerufen; oeffentlich, falls man ihn nach
	// einem Ausruestungswechsel manuell neu bauen will.
	bool BuildCustomVendorCatalog(cube::GameController* gc);

	// Spawnt UNSEREN Haendler (voller Vendor + Katalog) an einer festen Weltposition
	// (z.B. im Bergdorf). Raeumt einen evtl. vorhandenen Haendler weg (nur einer aktiv).
	void SpawnMerchantAt(cube::GameController* gc, Vector3<int64_t> pos);   // meldet nur noch einen Posten an

	// Fester Haendlerposten (Bloecke, Fusshoehe). Gesetzt wird im Spieltick am naechsten Posten (< 140 Bloecke).
	void RegisterMerchantPost(int64_t bx, int64_t by, int64_t bz);
}
