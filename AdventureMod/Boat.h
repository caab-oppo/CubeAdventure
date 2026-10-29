#pragma once
//
// Boat.h -- zweiter Spezial-Platz fuer das Boot (Stand 2026-10-27)
// ---------------------------------------------------------------
// Ausruestung des Spielers: Spezial-Platz = Kreatur+0xF08 (Kategorie 0x17; Unterart +0xF09: 0 Gleitschirm, 1 Boot).
// Das Spiel kennt nur EINEN Spezial-Platz. Der Mod legt im Inventarfenster einen zweiten Platz an ("Boot"), als
// echtes Ausruestungsfeld des Spiels (Klon der Vorlage "equipmentboxright" mit eigenem Feld-Element, wie die zwoelf
// nativen in GC+0x800A34/0x800A28, GC-ctor 0x460A20..0x460B3D). Inhalt: ein Item im Mod (je Charakter in der
// Charakter-Datenbank des Spiels, Save/characters.db). Linksklick tauscht mit dem Gegenstand am Mauszeiger (nur Boote), Rechtsklick legt das
// Boot ins Inventar.
// [G] (Spezial-Taste, Fall 5 der Tastentabelle 0x47E92C -> 0x47E83C): im Wasser (Kreatur+0x5C & 2) wird vorher
// automatisch das Boot in den Spezial-Platz getauscht (der Gleitschirm wandert in den Boot-Platz) -- das Spiel startet
// dann das Segeln (Aktion 0x6B). Zurueck an Land kommt der Gleitschirm von selbst wieder in den Spezial-Platz.
//
#include "cwsdk.h"
#include <string>

namespace Boat {
	void Tick(cube::GameController* gc);      // Spielthread, jedes Bild: Patches, Laden/Speichern, Ruecktausch an Land
	std::string Status();
}
