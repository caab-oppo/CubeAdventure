#pragma once
//
// ZoneSplit.h -- Bauwerke ueber Zonengrenzen hinweg (2026-10-13)
// ---------------------------------------------------------------
// placeStructure (0x5F0CE0) baut nur in die Zone, die gerade erzeugt wird -- ein Bauwerk ueber einer Zonengrenze
// endete bisher an der Grenze. ZoneSplit teilt es in die Stuecke je Zone (in Weltlage gedreht und zugeschnitten):
//   * das Stueck der Zone, die gerade erzeugt wird, sofort,
//   * die Stuecke der Nachbarzonen vorgemerkt: wird eine davon (neu) erzeugt -> OnZone setzt es mit;
//     ist sie schon geladen -> Tick setzt es auf dem Spielthread nach und vermascht neu.
// Jedes Bauwerk hat eine Kennung (id): doppelt angemeldet wird es nur einmal.
//
#include "cwsdk.h"
#include "LabStructure.h"
#include <string>

namespace ZoneSplit {
	// Zonenthread (waehrend der Erzeugung von zone zx/zy). rot wie placeStructure (0..3), culture = param_5.
	// Rueckgabe: Zahl der Zonen, ueber die das Bauwerk reicht.
	int  Place(void* zone, int zx, int zy, const Lab::Structure& s, int ox, int oy, int z, int rot, int fundament, int culture, uint64_t id);
	bool Crosses(int ox, int oy, int w, int d);                 // reicht das Rechteck ueber eine Zonengrenze?
	void OnZone(void* zone, int zx, int zy);                     // Zonenthread: vorgemerkte Stuecke dieser Zone setzen
	void Tick(cube::GameController* gc);                         // Spielthread: Stuecke in geladene Zonen nachsetzen
	std::string Status();
}
