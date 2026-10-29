#pragma once
//
// Tracking.h -- F9-Anzeige (Stand 2026-10-27)
// ---------------------------------------------------------------
// [F9] zeigt unten rechts: Koordinaten + Zone, das Biom an der Spielerposition und je angehakter Strukturart die
// naechsten Ziele (Entfernung + Himmelsrichtung). Was angezeigt wird, waehlt man im LAB (Reiter "Dungeons & F9"),
// gespeichert als Schalter "f9_<art>" in AdventureMod_schalter.cfg.
// Eigene Bauten, die erst mit ihrer Zone entstehen (Dungeons, kleine Tempel, Staedte), meldet der Mod beim Setzen hier
// an (Note); sie werden je Welt in AdventureMod_orte_<Seed>.txt gespeichert und erscheinen, sobald ihre Zone einmal
// erzeugt wurde. Native Orte, Bergdoerfer und eigene Biome werden aus dem Weltsamen berechnet (auch unentdeckte).
//
#include "cwsdk.h"
#include <string>

namespace Tracking {
	enum Kind { T_BIOME = 0, T_DUNGEON, T_TEMPLE, T_CITY, T_VILLAGE, T_OWNBIOME, T_NATIVE_TOWN, T_NATIVE_DUNGEON, T_NATIVE_OTHER, T_COUNT };
	const char* Label(int kind);                     // Text fuer den LAB-Haken
	bool On(int kind);
	void SetOn(int kind, bool on);
	// beliebiger Thread: gebauten Ort anmelden (doppelte im Umkreis von 48 Bloecken werden zusammengefasst)
	void Note(int kind, const std::string& name, int x, int y, int z, uint32_t worldSeed);
	// Spielthread: Zeilen fuer die Anzeige (zwischengespeichert, alle 0,5 s neu)
	std::string Text(cube::GameController* gc);
	std::string Status();
}
