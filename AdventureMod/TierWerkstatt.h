#pragma once
//
// TierWerkstatt.h -- LAB-Reiter "Tier-Werkstatt" (2026-10-27)
// ---------------------------------------------------------------
// Alle eigenen Tiere (.mob unter Strukturen\Mobs) direkt im LAB zusammengesetzt anzeigen, Teile verschieben und
// skalieren und in die .mob zurueckschreiben. Zusammenbau 1:1 wie das Spiel (Infinite-Cubes alpha/src/cube/Animation.cpp,
// Creature::AnimationState::addSprites, Ruhehaltung) -- dieselbe Rechnung wie Projekt\Werkzeuge\Mobs\tierbild.py:
//   Koerper/Kopf/Schwanz im Koerperrahmen RX(chestRotation), Kopf dreht die Neigung zurueck,
//   Haende (Vierbeiner: Vorderbeine) bei (+-x, y+2.1, z) bzw. Zweibeiner (+-x, y+2.1, z-4.1), rechte Hand gespiegelt,
//   Fuesse RX(feetRotation) vor der Verschiebung, Schwanz ab Koerper + Schwanzposition, Wurzel am Modell-Ende,
//   Fluegel T(+-x,y,z) RX(wingsRotation) RY(+-45) RZ(-90) RX(90).
//   Boden: Kreaturposition = Mitte der Kollisionshoehe -> Boden bei -5.6 * size.z / size.x Modell-Einheiten.
// Grundwerte je Basisrasse: TierBasis.inc (aus fillAppearance erzeugt).
//
#include <windows.h>

namespace TierWerkstatt {
	void SetFonts(HFONT normal, HFONT bold);
	void RegisterClasses(HINSTANCE hi);              // "CWLabTiere" (Seite) + "CWLabTierView" (Ansicht)
	LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM wp, LPARAM lp);
	void ApiPoll();                                  // LAB-Thread (Hauptfenster-Timer): Auftraege aus Strukturen\LabApi abarbeiten
}
