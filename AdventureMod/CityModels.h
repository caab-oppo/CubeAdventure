#pragma once
//
// CityModels.h -- Gebaeudemodelle der Staedte auf der Hochebene (rein rechnend, ohne Spielzugriff)
// ------------------------------------------------------------------------------------------------
// Alle Modelle sind Lab::Structure im nativen .cub-Layout. Markerfarben (nur Kanalwerte 0/127/255)
// verwandelt placeStructure mit Kultur 1 waehrend der Zonenerzeugung in native Objekte
// (MARKER_TABELLE.md, RE_Objekttypen.md 6a):
//   (255,0,0)   Tuer 2 breit x 3 hoch        (0,255,0)   Fenster 2 x 2
//   (255,255,0) Laterne (Prop 0x0D, lantern02.cub, leuchtet: Flag 1 "unbeleuchtet")
//   (255,127,0) Bett + Nachttisch            (127,127,0) Tisch + Stuhl     (255,127,127) Regal
// Baustoffe haben deshalb immer mindestens einen Kanal ausserhalb von 0/127/255.
// Ein Modellvoxel z = 0 steht auf dem obersten Bodenblock + 1 (Ausnahme: Wegeschicht, siehe City.cpp).
//
#include "LabStructure.h"
#include <cstdint>
#include <vector>

namespace CityModels {
	// Elfenstadt
	struct TreehouseDims { int tr, Rs, Rh, Rp, Rc, platZ, half; }; // Stamm, Treppenring, Haus, Plattform, Krone, Plattformhoehe, halbe Breite
	// Zaun (natives Objekt 0x34..0x37, 2 Bloecke lang): Mitte x/y in Bloecken (Modellkoordinaten), Fuss z, 0 = entlang x, 1 = entlang y
	struct FenceP { float x, y; int z; int orient; };
	std::vector<FenceP> TreehouseFences(int variant);         // Treppengelaender (Baumhaus wird nie gedreht)
	TreehouseDims TreehouseInfo(int variant);
	// shop: Laden am Stammfuss (+y-Seite unter der Plattform); Haendler steht bei TreehouseShopInfo (Modellkoordinaten)
	Lab::Structure Treehouse(int variant, uint32_t seed, bool shop = false);   // variant 0..3, ca. 33..41 breit, 60..85 hoch
	struct ShopInfo { int vx, vy; };
	ShopInfo TreehouseShopInfo(int variant);
	// Plattform-Ausgaenge (Gelaenderluecken) liegen in +x, +y, -x, -y bei Radius Rp.
	Lab::Structure MotherTree(uint32_t seed);                 // 117 x 117 x ~236, hohl, Wendeltreppe innen
	std::vector<FenceP> MotherTreeFences();
	// Thron des Hochelfenkoenigs: Sitzplatz in Modellkoordinaten (x, y Mitte, z = Standhoehe ueber dem Sitz) und Blickwinkel
	bool MotherThroneSeat(float* x, float* y, int* z, float* yawDeg);                   // Gelaender der Wendeltreppe (Schachtrand), Modellkoordinaten
	bool MotherKingStand(float* x, float* y, int* z, float* yawDeg);                    // Koenig steht zwischen Thron und Tisch (z wie oben: Standflaeche = zg + z + 1)
	// Zwergenstadt
	Lab::Structure DwarfHouse(int variant, uint32_t seed);    // variant 0..3, Vorderseite (Tuer) bei y = 0
	Lab::Structure Fortress(uint32_t seed);                   // 140 x 140 x ~95, Tor bei y = 0
	// Kastanienbaum (Vorlage echte Ross-/Edelkastanie), ca. 55 x 55 x 66: 0 Bluete, 1 Sommer, 2 Herbst, 3 Edelkastanie.
	// City legt ihn als Strukturen\Stadt\<Name>.cub ab (nur wenn die Datei fehlt) und nimmt immer die Datei -> im LAB anpassbar.
	Lab::Structure Chestnut(int variant, uint32_t seed);
	const char* ChestnutName(int variant);
	const int kChestnutVariants = 4;
	const int kTreehouseVariants = 4, kDwarfHouseVariants = 4;

	// Stadtviertel (2026-10-13). style 0 = Elfen, 1 = Zwerge; Vorderseite bei y = 0.
	struct HutInfo { int w, d, vx, vy; };                    // Groesse, Haendlerplatz hinter der Theke (Modell)
	HutInfo MarketHutInfo();
	Lab::Structure MarketHut(int style, int variant, uint32_t seed);     // 19 x 16 x 17: Stand mit Markise vorn, Laden hinten
	struct AlchemyInfo { int w, d, brewX, brewY, npcX, npcY; };       // Braustand-Mitte (Lagerfeuer-Objekt darunter), Alchemist
	AlchemyInfo AlchemyHouseInfo();
	Lab::Structure AlchemyHouse(int style, uint32_t seed);   // 17 x 16 x 22, Haus hinten, Vorhof mit Braustand
	struct TempleInfo { int s, orbZ; };                      // Kantenlaenge, Hoehe der Kugelmitte ueber dem Fuss
	TempleInfo TempleInfoOf();
	Lab::Structure Temple(int style, uint32_t seed);          // 41 x 41 x 40, Saeulenring, Kuppel mit Oeffnung
	Lab::Structure Orb(uint32_t seed);                        // 7 x 7 x 7, leuchtende Kugel (schwebt im Tempel)
	Lab::Structure Statue(int variant, int style, uint32_t seed);   // 15 x 15 x 34: 0 Hueterin, 1 Waechter, 2 Weiser (Tempelplatz)
	// Lichtbloecke (Blocktyp 0x0D) in den Stadtmodellen: Trankfluessigkeit im Braustand/Schornstein (die Kugel ganz).
	bool IsGlowColor(uint8_t r, uint8_t g, uint8_t b);

	// Hilfen fuer die Wegeschicht (City.cpp)
	struct C3 { uint8_t r, g, b; };
	C3 Safe(C3 c);                                            // nie eine Markerfarbe, nie (0,0,0)
	C3 Vary(C3 c, uint32_t h, int amp);
	uint32_t Hash(int x, int y, int z, uint32_t s);
	extern const C3 kLantern, kDoor, kWindow, kBed, kTable, kShelf;
}
