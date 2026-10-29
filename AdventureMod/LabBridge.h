#pragma once
//
// LabBridge.h
// ---------------------------------------------------------------
// Die Bruecke zwischen dem LAB-Fenster (eigener Thread) und dem Spiel.
//
// GRUNDREGEL: Das LAB-Fenster fasst das Spiel NIE direkt an. Jede Aktion
// wird als Auftrag in eine Warteschlange gelegt (Post) und vom EndScene-
// Hook -- also dem Thread, auf dem auch alle anderen Mod-Bausteine laufen --
// abgearbeitet (Drain). Ergebnisse gehen auf demselben Weg zurueck.
//
// Warum so streng: Die Welt (Zonen, Objektvektoren, Ortsindex, Chunks) ist
// nicht threadsicher. Ein zweiter Thread, der parallel Bloecke setzt oder den
// Objektvektor verlegt, waehrend die Kollisionsschleife darueber laeuft, ist
// genau die Sorte Absturz, die wir mehrfach gejagt haben (0x00004800).
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include "LabStructure.h"
#include "BuildingFx.h"

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace LabBridge {

	// ---- Auftraege: Fenster -> Spiel -----------------------------------------
	typedef std::function<void(cube::GameController*)> Job;
	void Post(const std::string& label, Job job);
	void Drain(cube::GameController* gc);        // aus Hooked_EndScene, jedes Frame
	int  PendingJobs();

	// ---- Protokoll: alles, was nach std::cout geht, landet auch im LAB --------
	void InstallLogTee();                        // einmal beim Start
	void Log(const std::string& line);           // Zeile ins LAB-Protokoll (+ Konsole)
	std::string TakeLog();                       // neuer Text seit dem letzten Aufruf

	// ---- Ergebnisse: Spiel -> Fenster -----------------------------------------
	struct LibEntry {
		int index = -1;                          // Registry-Index (nativ) oder -1
		int sx = 0, sy = 0, sz = 0;
		std::string file;                        // "building-smithy.cub"
		int suggestedTyp = 0;                    // Vorschlag fuer placeStructure param_5
	};
	void PostStructure(const Lab::Structure& s); // fertiges Import-/Scanergebnis
	bool TakeStructure(Lab::Structure& out);
	bool TakeNativeLibrary(std::vector<LibEntry>& out);

	struct Placement {
		std::string name;                        // Dateiname ohne .cub in "Strukturen\"
		int x = 0, y = 0, z = 0;                 // UNTERE ECKE in Bloecken (nativ, wie placeStructure)
		int rot = 0, typ = 0, fundament = 0;
		int markers = 1;                         // 1 = Markerfarben durch den Mod ersetzen (bei typ 0)
	};
	std::vector<Placement> GetPlacements();      // Kopie, fuer die Anzeige

	// ---- Zustand (vom Spielthread jedes Frame aktualisiert) -------------------
	struct Snapshot {
		bool inGame = false;
		int px = 0, py = 0, pz = 0;              // Spieler in Bloecken
		int zx = 0, zy = 0;                      // Zone des Spielers
		uint32_t seed = 0;
		int registryCount = 0;
		bool emptyColorBlack = true;             // DAT_0076b340 == (0,0,0)?
		bool undoAvailable = false;
		bool ovl_tracking = false;               // [F9]-Anzeige (wird von main.cpp gespiegelt)
		bool oceanGen = false;
	};
	Snapshot GetSnapshot();

	// ---- Spieloperationen (NUR auf dem Spielthread, also aus einem Job) --------
	// Natives Modell aus der Registry kopieren.
	bool ImportNative(cube::GameController* gc, int index, Lab::Structure& out, std::string& err);
	// Registry-Liste (Index, Groesse, Dateiname) fuer die Bibliothek.
	void RequestNativeLibrary(cube::GameController* gc);
	// Quader aus der Welt lesen (untere Ecke + Groesse).
	bool ScanWorld(cube::GameController* gc, int x0, int y0, int z0, int sx, int sy, int sz, Lab::Structure& out);

	struct PlaceOptions {
		int x = 0, y = 0, z = 0;                 // untere Ecke
		int rot = 0;                             // 0..3
		int typ = 0;                             // 0 = Markerlogik aus (empfohlen)
		int fundament = 0;
		bool replaceMarkers = true;              // Markerfarben -> Objekte (bei typ 0)
		bool remember = false;                   // in die Platzierungsliste der Welt
		BuildingFx::Def fx;                      // Gebaeude-Funktionen: Funktions- und Leuchtbloecke (<name>.funk)
	};
	// Setzt die Struktur ueber WorldInfo_placeStructure mit einem Schattenmodell
	// -- also exakt so, wie das Spiel seine eigenen Gebaeude setzt.
	bool PlaceNative(cube::GameController* gc, const Lab::Structure& s, const PlaceOptions& o, std::string& report);
	// Letzte Platzierung zuruecknehmen (Bloecke; Objekte bleiben bis zum Neuladen).
	bool UndoLastPlacement(cube::GameController* gc, std::string& report);
	// Alle nativen Modelle ab einer Mindestgroesse als .cub nach Strukturen\Export.
	int  ExportAllNative(cube::GameController* gc, int minDim);
	// Platzierung aus der Liste entfernen (Bloecke verschwinden beim Neuladen der Zone).
	bool RemovePlacement(int listIndex);

	// Vorschlag fuer placeStructure param_5 je Registry-Index
	// (game_misc.cpp:28654-28683, siehe RE_EigeneStrukturen.md Anhang A).
	int  SuggestTyp(int registryIndex);

	// ---- Fuer andere Mod-Bausteine (BiomeMod) -- NUR Spielthread ----------------
	// Leichtes Stempeln: placeStructure mit Schattenmodell, typ 0, ohne Undo, ohne
	// Liste, ohne Index-Nachzug (typ 0 legt keine Objekte an, der Objektvektor
	// zieht also nicht um). onlyZx == INT_MIN -> alle beruehrten Zonen.
	// Rueckgabe: Zahl der Zonen, in die gesetzt wurde.
	// Direkt in eine bestimmte Zone (z. B. waehrend der Zonenerzeugung, noch nicht veroeffentlicht).
	int  StampIntoZone(const Lab::Structure& s, int x, int y, int z, int rot, int fundament, void* zone);
	// Staedte: Modell einmal vorbereiten (wird nie freigegeben), dann direkt in die Zone stempeln.
	// culture = placeStructure param_5: 0 = Marker bleiben Bloecke, 1 = Marker werden native Objekte
	// (Tuer rot, Fenster gruen, Laterne gelb, Bett orange, Tisch oliv, Regal lachs -- MARKER_TABELLE.md),
	// 7 = Tueren als grosse Tore, 0x0D = Schmiede (Ofen, Amboss, Anpassungsbank).
	int  StampInPlace(Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone);
	int  StampIntoZoneCulture(const Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone);
	struct PreparedModel;
	PreparedModel* Prepare(const Lab::Structure& s);
	void PreparedDims(const PreparedModel* p, int* sx, int* sy, int* sz);
	int  StampPrepared(PreparedModel* p, int x, int y, int z, int rot, int fundament, int culture, void* zone);
	// Wie oben, mit eigenem Blocktyp (0x0D = Lichtblock). Hinweis: placeStructure hoehlt LEERE Zellen zwischen dem
	// untersten und obersten Voxel jeder Saeule aus -> Lichtbloecke nur als einlagige Modelle (sz = 1) stempeln.
	int  StampPreparedFlags(PreparedModel* p, int x, int y, int z, int rot, int fundament, int culture, void* zone, int blockFlags);
	int  StampInPlaceFlags(Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone, int blockFlags);
	void* NativeModelPtr(cube::GameController* gc, int index);              // Registry-Modell (z. B. 0x887 Landeplatz)
	bool NativeModelDims(void* model, int* sx, int* sy, int* sz);
	int  StampModelPtr(void* model, int x, int y, int z, int rot, int fundament, int culture, void* zone);
	int  StampStructure(cube::GameController* gc, const Lab::Structure& s, int x, int y, int z,
	                    int rot, int fundament, int onlyZx, int onlyZy);
	// Nur die geladenen Chunks im Rechteck (+16 Rand) neu vermaschen.
	void RemeshArea(cube::GameController* gc, int x0, int y0, int x1, int y1);
	cube::Zone* ZoneAt(cube::GameController* gc, int zx, int zy);   // SEH-geschuetzt, 0 = nicht geladen

	// Beim Framework anmelden (OnZoneGenerated fuer das Neu-Stempeln).
	void RegisterWithFramework();
	// Von main.cpp jedes Frame: Spiegelwerte fuer die Anzeige.
	void SetOverlayFlags(bool tracking, bool oceanGen);
}
