#pragma once
//
// BuildingFx.h -- Gebaeude-Funktionen: Funktionsbloecke und Leuchtbloecke je Gebaeude (LAB -> Bauen -> "Funktionen")
// ------------------------------------------------------------------------------------------------------------------
// Datei <name>.funk neben <name>.cub (Text):
//   funktion=R,G,B;<Funktionsname>     Voxel genau dieser Farbe werden beim Setzen zur Funktion:
//                                      natives Objekt (Truhe, Bett, Amboss, Lagerfeuer/Braustand ...) oder
//                                      Bewohner (native Haendler 0x80..0x84, Alchemist, unser Haendler).
//                                      Benachbarte Voxel derselben Farbe = EIN Objekt (Mitte, Unterkante).
//   leuchtend=R,G,B                    Voxel dieser Farbe werden Lichtbloecke: Blocktyp 0x0D.
// Lichtblock (belegt, RE_Interaktion.md 7a): Blocktyp 0x0D ist fest und sichtbar und zaehlt bei der Beleuchtung
// der Nachbarflaechen mit voller Helligkeit 255 (Terrain_sampleBlockBrightness 0x59FEA0, Mesher ChunkBuffer:
// Gewicht (3 - Abstand)/3) -- er leuchtet also seine Umgebung im Umkreis von etwa 3 Bloecken an.
//
#include "LabStructure.h"
#include <array>
#include <string>
#include <vector>

namespace BuildingFx {
	enum Kind { K_OBJECT = 0, K_VENDOR, K_ALCHEMIST, K_MERCHANT };
	struct Choice { const char* name; int kind; int param; float sx, sy, sz; const char* note; };
	const Choice* Choices(int* n);
	int ChoiceIndex(const std::string& name);                // -1 = unbekannt

	struct Entry { uint8_t r = 0, g = 0, b = 0; std::string fn; bool glow = false; };   // fn leer = nur leuchtend
	struct Def {
		std::vector<Entry> e;
		bool Any() const { return !e.empty(); }
		const Entry* Find(uint8_t r, uint8_t g, uint8_t b) const;
	};
	std::string PathFor(const std::string& cubPath);         // <name>.funk
	bool Load(const std::string& cubPath, Def& out);          // false = keine Datei (out leer)
	bool Save(const std::string& cubPath, const Def& d, std::string& err);   // leer -> Datei loeschen

	// Funktionsgruppe in Modellkoordinaten (Mitte x/y aufgerundet wie die Marker, z = Unterkante)
	struct Group { int choice; int x, y, z, voxels; };
	// glow: Modellkoordinaten aller Leuchtvoxel (mit Farbe); groups: Funktionsgruppen.
	struct GlowVoxel { int x, y, z; uint8_t r, g, b; };
	void Analyze(const Lab::Structure& s, const Def& d, std::vector<GlowVoxel>& glow, std::vector<Group>& groups);
	// Modell -> Welt (gleiche Drehung wie placeStructure, RE_placeStructure.md)
	void ModelToWorld(int rot, int sx, int sy, int mx, int my, int ox, int oy, int* wx, int* wy);
}
