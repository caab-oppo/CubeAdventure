#pragma once
//
// MapMarkers.h -- Marker auf der Weltkarte (M), Stand 2026-10-27
// ---------------------------------------------------------------
// Zeichnet auf der Karte Punkte: Runensteine (violett) und eigene Markierungen (gelb). RECHTE Maustaste auf der Karte setzt
// eine Markierung an der Stelle (Ebene in Spielerhoehe); Rechtsklick auf eine vorhandene entfernt sie. Rechtsklick ist auf
// der Karte nativ unbelegt (Infinite-Cubes GameController::handleMouseButton: rechts nur Baumodus/Verbesserung). Gespeichert je Welt in
// der eigenen Datenbank (Save/AdventureMod.db, "AdventureMod_kartenmarker_<Seed>.txt").
// Projektion wie MapOverlayWidget::draw (Infinite-Cubes alpha/src/cube/widgets/MapOverlayWidget.cpp):
//   p = Ziel - Spieler - GameController::m_mapOffset (Bloecke), v = WorldMap::m_view * p (v.z > 0),
//   q = WorldMap::m_projection * v, Bild = (q.x * W/2 + W/2, -q.y * H/2 + H/2).
// Lage im GameController (Cube.exe, aus Infinite-Cubes GameController.h + Querverweisen belegt): m_mapOpen +0x8006E4,
// m_worldMap +0x800D44 (m_projection +4, m_view +0x44, Matrix 4x4 float spaltenweise), m_mapOffset +0x1000E4C.
//
#include "cwsdk.h"

struct IDirect3DDevice9;

namespace MapMarkers {
	void Draw(cube::GameController* gc, IDirect3DDevice9* dev, int w, int h);   // Present-Hook (Spielthread)
	// zusaetzlich: Namen der Stadtviertel (City::MapLabels) als Text
}
