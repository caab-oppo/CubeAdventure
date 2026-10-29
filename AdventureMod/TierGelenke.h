#pragma once
//
// TierGelenke.h -- eigene Bein-Gelenke fuer eigene Tiere (2026-10-29)
// ---------------------------------------------------------------
// Das Spiel schwingt jedes Bein ZWEIMAL: erst um den Modellursprung (Mitte der Kreatur), dann um die Beinmitte
// (Infinite-Cubes Animation.cpp: Fuesse rotate(feetRotation + Schwung) vor translate(Fussposition), rotate(Schwung)
// nach scale; Vorderbeine der Vierbeiner ebenso mit dem Haende-Rahmen). Die erste Drehung schiebt lange Beine beim
// Laufen aus dem Koerper. Fuer Rassen mit Gelenken (.mob "gelenk=<teil>;x;y;z") faellt sie weg: das Bein-Modell ist
// so aufgefuellt (Projekt\Werkzeuge\Mobs\tierbild.py --gelenke), dass seine Mitte im Gelenk liegt -> es schwingt um
// Huefte/Schulter. Belegte Aufrufe von Matrix::rotate(Winkel, x, y, z) 0x4241B0 in AnimationState::addSprites:
//   0x423059 linkes Vorderbein (Matrix AnimationState+0x32C)   0x423405 rechtes Vorderbein (+0x36C)
//   0x423957 linker Fuss (+0x4AC, Winkel feetRotation+Schwung) 0x423B83 rechter Fuss (+0x4EC)
//   AnimationState+0 = Kreatur (0x423023: mov eax,[edi]); Kreatur+0x64 Rasse, +0xD0 feetRotation.
//
#include <string>

namespace TierGelenke {
	void Install();                       // Aufrufe umbiegen (einmal, nach dem Laden des Spiels)
	void Reload();                        // Rassen mit Gelenken aus Strukturen\Mobs neu einlesen (jeder Thread)
	bool RaceHasJoints(int race);
	std::string Status();
}
