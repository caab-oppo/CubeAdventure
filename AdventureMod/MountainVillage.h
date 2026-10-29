#pragma once
//
// MountainVillage.h
// ---------------------------------------------------------------
// Baut ein eigenes kleines Bergdorf mit Lufthafen aus Bloecken
// (World::SetBlock). Ausgeloest per STRG+0; baut haeppchenweise
// pro Frame ueber eine Queue, damit kein Frame-Haenger entsteht.
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <string>
#include <vector>
#include <cstdint>

namespace MountainVillage {
	void RegisterWithFramework();
	void BuildAtPlayer(cube::GameController* gc); // plant den Bau an der Spielerposition
	void PersistMarkerAtPlayer(cube::GameController* gc); // STRG+M: Persistenz-Test (0x14-Record + zone dirty)
	void PropTestAtPlayer(cube::GameController* gc);      // STRG+P: nativer Deko-Prop-Render-Test (Weg 1)
	// STRG+H: Voxelscan um den Spieler -> HausBloecke.txt (Hausschale erfassen).
	void ScanBlocksAtPlayer(cube::GameController* gc, int radius, int zDown, int zUp);
	// [STRG+T] Objekt-Tester: setzt je Druck EIN Objekt des naechsten Typs
	// 40 Bloecke in +X mit Nummer davor und haengt es in den Ortsindex.
	void ObjectTester(cube::GameController* gc);
	void ResetBlockScan();                                // STRG+G: Scandatei leeren
	void SpawnTestDwarfAtPlayer(cube::GameController* gc);// STRG+2: EINEN Test-Zwerg spawnen (Flag-Test)
	void AppendTracking(cube::GameController* gc, std::string& overlay); // F9: Doerfer auffindbar
	// Naechstes Bergdorf in Himmelsrichtung (dir 0=Nord 1=Ost 2=Sued 3=West). Blockkoordinaten.
	bool NextVillageInDirection(cube::GameController* gc, int fromBlockX, int fromBlockY,
		int dir, int& outBlockX, int& outBlockY);

	// ---- Filter fuer andere Bausteine (BiomeMod) --------------------------------
	// Liefert der Filter false, entsteht in dieser Zone KEIN Bergdorf. Die Lage aller
	// anderen Doerfer bleibt unveraendert (das Raster wird nicht verschoben).
	// Der Filter wird auf dem Zonen- UND dem Spielthread gerufen -> threadsicher halten.
	typedef bool (*VillageFilter)(cube::GameController* gc, int zx, int zy);
	void SetVillageFilter(VillageFilter f);
	// Rasterpruefung OHNE Filter: liegt in dieser Zone ein Bergdorf-Standort?
	bool IsVillageSiteZone(cube::GameController* gc, int zx, int zy);

	// ---- LAB -------------------------------------------------------------
	// NUR auf dem Spielthread aufrufen (LabBridge::Post). Das LAB-Fenster laeuft
	// in einem eigenen Thread und darf die Welt nie direkt anfassen.
	struct LabBlock { int x, y, z; uint8_t r, g, b; };

	bool LabReadBlock(cube::GameController* gc, int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& t);
	bool LabPutBlock(cube::GameController* gc, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t t);

	// Markerbloecke im Quader (inklusive) zu interaktiven Objekten machen
	// (gruppieren, anlegen, indexieren, Index der Zonen nachziehen).
	int  LabReplaceMarkersBox(cube::GameController* gc, int x0, int y0, int z0, int x1, int y1, int z1);
	// Alle Indexknoten einer Zone wieder auf den aktuellen Objektvektor zeigen lassen.
	int  LabRepointZoneIndex(cube::GameController* gc, int zx, int zy);
	// Objekte ab Vektorplatz fromIdx in den Ortsindex haengen (nach placeStructure mit typ != 0).
	int  LabIndexObjectsFrom(cube::GameController* gc, int zx, int zy, int fromIdx);
	// Anzahl Objekte im Vektor der Zone (-1 = Zone nicht geladen).
	int  LabObjectCount(cube::GameController* gc, int zx, int zy);
	// Beliebige native Objekte anlegen (erst alle anhaengen, dann indexieren, dann Zonenindex nachziehen --
	// wie der Markerersatz). Rueckgabe: angelegte Objekte.
	struct LabObj { int type, x, y, z, orient; float sx, sy, sz; };
	int  LabCreateObjects(cube::GameController* gc, const std::vector<LabObj>& objs);

	// Bauplan eines Mod-Bauwerks erfassen, OHNE die Welt zu aendern.
	// what: 0 = Turm (Bergdorf), 1 = Haus (Blocknachbau), 2 = Schmiede (Blocknachbau).
	// Koordinaten relativ, z = 0 ist die Bodenebene. Objekte kommen als Markerfarben.
	bool LabCapturePlan(cube::GameController* gc, int what, std::vector<LabBlock>& out, std::string& name);
}
