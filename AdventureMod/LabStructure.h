#pragma once
//
// LabStructure.h
// ---------------------------------------------------------------
// Datenmodell einer Struktur im LAB -- bewusst IDENTISCH zum nativen
// Modellformat des Spiels, damit nichts umgerechnet werden muss:
//
//   .cub-Datei  = int32 sizeX, int32 sizeY, int32 sizeZ,
//                 danach sizeX*sizeY*sizeZ*3 Byte Farbe (R,G,B).
//   Reihenfolge = x laeuft am schnellsten, dann y, dann z (Hoehe).
//   Leer        = (0,0,0).
//
// Genau so liegt ein Modell auch im Speicher (Modellobjekt +0x30 = RGB-Zeiger,
// +0x44/+0x48/+0x4C = Groesse). Beleg: RE_EigeneStrukturen.md, Abschnitte 1.5,
// 1.6 und 2.1; WorldInfo.cpp:5393-5420 (Indexformel im placeStructure-Rumpf).
//
// Neben jeder .cub-Datei kann eine gleichnamige .txt mit Beiwerten liegen
// (typ, fundament, herkunft). Die stehen absichtlich NICHT im .cub, damit
// die Datei byte-gleich zum nativen Format bleibt.
//
#include <cstdint>
#include <string>
#include <vector>

namespace Lab {

	struct Structure {
		std::string name;      // Anzeigename / Dateiname ohne ".cub"
		std::string origin;    // Herkunft: "nativ 0x903", "scan", "datei", "mod: turm", ...
		int sx = 0, sy = 0, sz = 0;
		std::vector<uint8_t> rgb;   // sx*sy*sz*3, natives Layout

		// Beiwerte fuer placeStructure (siehe RE_placeStructure.md):
		//   typ        = param_5, Kultur-/Gebaeudetyp. 0 = Markerlogik AUS
		//                (Markerfarben bleiben Bloecke und werden vom Mod ersetzt).
		//   fundament  = param_7, verlaengert die Struktur nach unten bis zum Boden.
		int typ = 0;
		int fundament = 0;

		bool   Valid() const { return sx > 0 && sy > 0 && sz > 0 && rgb.size() == (size_t)sx * sy * sz * 3; }
		bool   In(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz; }
		size_t Idx(int x, int y, int z) const { return (((size_t)sy * z + y) * sx + x) * 3; }

		// false = leer (oder ausserhalb)
		bool Get(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const;
		bool Solid(int x, int y, int z) const;
		void Set(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b);
		void Clear(int x, int y, int z);

		void Create(int nsx, int nsy, int nsz);          // leere Struktur
		void Resize(int nsx, int nsy, int nsz);          // Inhalt bleibt an (0,0,0) verankert
		void Shift(int dx, int dy, int dz);              // Inhalt verschieben (was rausfaellt, ist weg)
		void RotateZ90();                                // 90 Grad um die Hochachse (gegen den Uhrzeigersinn von oben)
		void MirrorX();
		bool CropToContent();                            // auf die Huelle der gefuellten Voxel zuschneiden
		// Skalieren je Achse (naechster Nachbar): neue Groesse = runde(alt * f), mind. 1.
		// f = 2 verdoppelt jede Kante (jedes Voxel wird 2x2x2), f = 0.5 halbiert.
		bool Scale(float fx, float fy, float fz);
		int  CountSolid() const;
	};

	// ---- Dateien ----------------------------------------------------------
	// Alle Pfade relativ zum Arbeitsordner von Cube.exe (dort liegen auch
	// HausBloecke.txt, HausScan.txt usw.).
	std::string StructuresDir();                 // "Strukturen"
	std::string ExportDir();                     // "Strukturen\\Export"   (native Modelle als .cub)
	bool EnsureDir(const std::string& dir);

	bool LoadCub(const std::string& path, Structure& out, std::string& err);
	bool SaveCub(const std::string& path, const Structure& s, std::string& err);
	void LoadMeta(const std::string& cubPath, Structure& s);   // liest <name>.txt neben der .cub
	bool SaveMeta(const std::string& cubPath, const Structure& s);

	// Liest alle Scans aus HausBloecke.txt (Format von MountainVillage::ScanBlocksAtPlayer).
	int  ImportBlockScans(const std::string& path, std::vector<Structure>& out, std::string& err);

	// Dateinamen-tauglicher Name (keine \ / : * ? " < > |).
	std::string SafeFileName(const std::string& name);

	// Alle .cub-Dateien eines Ordners (nur Dateinamen).
	std::vector<std::string> ListCubFiles(const std::string& dir);
}
