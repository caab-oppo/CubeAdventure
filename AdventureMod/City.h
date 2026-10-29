#pragma once
//
// City.h -- Staedte auf der Hochebene des Sternmassivs (bzw. jeder Biom-"freien Ebene")
// --------------------------------------------------------------------------------------
// Ablauf (alles waehrend der Zonenerzeugung, wie native Doerfer):
//   BiomeMod::Hook_SunCall (Aufruf 0x5EDE84, VOR dem Ortsindex und vor der Veroeffentlichung der Zone)
//   -> City::StampZone: Stadtplan (deterministisch aus Weltsamen + Zellmitte) und fuer diese Zone
//      1. eine Wegeschicht (Wege, Plaetze, Mauern, Tuerme, Tore, Rampen, Laternenpfosten) als
//         256 x 256 x h-Struktur, gestempelt auf die Bodenoberkante,
//      2. alle Gebaeude, deren Grundflaeche die Zone beruehrt (placeStructure verwirft Bloecke
//         ausserhalb der Zone; jede Zone stempelt ihren Teil -- RE_placeStructure.md).
//   Gestempelt wird mit Kultur 1: Markerfarben werden native Objekte (Tueren, Fenster, Laternen,
//   Betten, Tische, Regale) und landen ueber den nativen Ortsindex-Aufbau sofort im Index.
//   Native Modelle: Landeplatz 0x887 (landingplace.cub, 32 x 32 x 80, Deck z 60) und in der
//   Zwergenstadt Schmieden 0x903 mit Kultur 0x0D (Ofen, Amboss, Anpassungsbank).
//   Luftschiffe: City::Tick meldet jeden erzeugten Landeplatz beim Taxi an (AirshipTaxi).
//
#include "cwsdk.h"
#include "LabStructure.h"
#include <vector>
#include <string>
#include <cstdint>

namespace City {
	enum Kind { NONE = 0, ELF = 1, DWARF = 2, RANDOM = 3 };
	const char* KindName(int k);
	struct Site {
		int kind;                 // ELF / DWARF (RANDOM ist hier schon aufgeloest)
		int cx, cy;               // Mitte der Hochebene (Bloecke)
		float radius;             // Stadtradius (Bloecke)
		uint32_t seed;            // Weltsamen
		int biome = 0;            // Biom-ID (1..15) der Zelle -- liefert die Baeume fuer die Feldmitten
	};
	int  ResolveKind(int kind, uint32_t seed, int cx, int cy);
	// Stadtplan bauen (falls noch nicht da) und JEDER beruehrten Zone einen Bauauftrag geben (Puffer:
	// Stadtkreis + 30 und alle Bauwerkshuellen). Generierungsthread.
	void Register(void* gc, const Site& s);
	// Staedte mit Bauauftrag fuer diese Zone (unabhaengig vom Biom am Zonenmittelpunkt).
	int  ZonePlans(int zx, int zy, Site* out, int maxN);
	// Tempel am Weltspawn (2026-10-29): Zonen-Thread. Stempelt den Elfen-Tempel (ohne Kugel) 36 Bloecke noerdlich des
	// Weltspawns, wenn diese Zone ihn enthaelt; Rueckgabe true + Grundflaeche. Den Tempelhueter setzt City::Tick.
	typedef bool (*SurfFn)(void* zone, int x, int y, int* z, uint8_t* t);
	// Zonen-Thread: feindliche Spawn-Datensaetze in Zonen mit Stadt-Auftrag entfernen (nach der Erzeugung). Anzahl.
	int ScrubZoneSpawns(void* zone, int zx, int zy);
	// Spieler zum naechsten Hochelfenkoenig bringen (Seed-Berechnung, dann Thronsaal sobald geladen). Spielthread.
	bool VisitKing(cube::GameController* gc, std::string& msg);
	bool SpawnTempleZone(void* gc, void* zone, int zx, int zy, int spawnX, int spawnY, SurfFn surf, int* x0, int* y0, int* x1, int* y1);
	// Baeume eines Bioms (BiomeMod): Zonen-Modell-Baeume, Hangbaeume, Pflanzenbaeume, Pflanzendateien.
	// Rueckgabe true = das Biom hat native Baeume -> der native Baum 0x832 kommt vergroessert dazu.
	typedef bool (*TreeProvider)(int biome, std::vector<Lab::Structure>& out);
	void SetTreeProvider(TreeProvider f);
	void SetPlantProvider(TreeProvider f);               // kleine Pflanzen des Bioms (Pflanzgaerten in den Vierteln)
	// Tiere im Tierviertel (BiomeMod: Biom-Einstellung "Tiere in der Stadt", Rassen-IDs). Leer = Schwein, Schaf, Kuh, Huhn.
	typedef bool (*AnimalProvider)(int biome, std::vector<int>& races);
	void SetAnimalProvider(AnimalProvider f);
	const char* DistrictName(int k);
	struct MapLabel { int x, y, z; std::string name; };   // z = Bodenhoehe der Stadt (INT_MIN = unbekannt)
	int MapLabels(std::vector<MapLabel>& out);          // Stadt + Viertel aller geplanten Staedte (Weltkoordinaten)                    // 0 Arbeit, 1 Handel, 2 Alchemie, 3 Tempel, 4 Tiere
	// Bewohner-Posten aus Gebaeude-Funktionen (LAB): kind 0 nativer Haendler (cls 0x80..0x84), 1 Alchemist.
	// Gesetzt wie die Viertel-Bewohner (< 120 Bloecke, Zone geladen, keine Doppelten). Jeder Thread.
	void AddExternalPost(int kind, int x, int y, int z, int cls);
	// Generierungsthread, waehrend der Zonenerzeugung. groundZ = oberster Bodenblock der Hochebene.
	void StampZone(void* gc, void* zone, int zx, int zy, const Site& s, int groundZ);
	// Beruehrt die Zone die Stadt (inkl. Mauer)?
	bool ZoneTouches(const Site& s, int zx, int zy);
	void Tick(cube::GameController* gc);                // Spielthread: Landeplaetze beim Taxi anmelden
	void NoteAbort();
	// Natives 0x188-Objekt waehrend der Zonenerzeugung anhaengen (Mitte wx/wy, Fuss wz, Halbmasse). SEH-geschuetzt.
	bool PushObject(void* zone, int type, float wx, float wy, int wz, int orient, float sx, float sy, float sz);
	void SetFences(bool on); bool FencesOn();          // Schalter "stadt_zaeune" (native Zaeune an Treppen/Bruecken)
	void SetResidents(bool on); bool ResidentsOn();    // Schalter "stadt_bewohner" (Elfen + Haendler auf dem Balkon)
	// groundZ = INT_MIN: Zone ohne Messpunkt -> Bodenhoehe der Stadt aus einer frueheren Zone verwenden
	int  ExportModels(std::string& report);             // Strukturen\Stadt\*.cub (zum Ansehen im LAB)
	std::string Status();
}
