#pragma once
//
// AirshipTaxi.h
// ---------------------------------------------------------------
// Bergdorf-Luftschiff-Taxi: auf jedem Dorfturm steht ein Vendor-NPC.
// Wird er angesprochen (Shop geht auf), dockt ein Luftschiff am Turm an.
// Sein Inventar hat 4 Waren = naechstes Dorf in Nord/Ost/Sued/West.
// Kauf einer Ware -> Schiff fliegt ~10s spaeter in die Richtung los
// (via AirshipSpawner::DockAt/Depart, Zieldorf via MountainVillage-Grid).
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <string>

namespace AirshipTaxi {
	void RegisterWithFramework();

	// Vom Dorfbau aufgerufen: registriert einen Turm-Taxiposten + spawnt den
	// Vendor-NPC auf dem Turmdach. cx,cy = Dorfmitte (Block), deckZ = Deckhoehe.
	void OnVillageBuilt(cube::GameController* gc, int cx, int cy, int deckZ);

	// Landeplatz einer Stadt (natives Modell 0x887): Taxi-NPC auf dem Deck, Anlegepunkt neben dem Deck.
	// race: Rasse des Taxi-NPC (Elfenstadt ElfMale, Zwergenstadt DwarfMale, sonst HumanMale).
	bool AddLandingPost(cube::GameController* gc, int cityX, int cityY, int npcX, int npcY, int npcZ, int dockX, int dockY, int dockZ, int race = 2);

	// Fuer das Overlay (main.cpp): liefert Text + Weltposition (Blockkoordinaten)
	// der aktiven Turm-Sprechblase. Rueckgabe false = nichts anzeigen.
	bool GetBubble(cube::GameController* gc, std::string& outText,
		int& outBlockX, int& outBlockY, int& outBlockZ);

	// STRG+3: Roh-Dump von Vendor- + Spieler-Inventar (Diagnose Kauf-Erkennung).
	void DumpInventories(cube::GameController* gc);

	// Bei Weltwechsel/Reload aufrufen: vergisst die Turm-Posten, damit der Taxi-Vendor
	// beim naechsten Dorf-(Neu)bau frisch spawnt.
	void OnWorldReload();

	// Waren je Richtung (0 Nord, 1 Ost, 2 Sued, 3 West): Kennung eines eigenen Gegenstands (Items) oder 0 =
	// Kopie der angelegten Waffe wie bisher. keepTicket: gekaufte Ware bleibt im Inventar. Gespeichert in
	// Strukturen\Gegenstaende\_luftschifftaxi.txt; vorhandene Taxi-NPCs bekommen den Katalog neu.
	void GetDirectionItems(int ids[4], bool* keepTicket);
	bool SetDirectionItems(const int ids[4], bool keepTicket);
}
