#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "City.h"
#include "Host.h"
#include "NativeUi.h"
#include "CityModels.h"
#include "LabBridge.h"
#include "LabStructure.h"
#include "AirshipTaxi.h"
#include "MobLab.h"
#include "WanderingMerchant.h"
#include "Recipes.h"
#include "Items.h"
#include "Vendors.h"
#include "Temples.h"
#include "Skills.h"
#include "Effects.h"
#include "BiomeMod.h"
#include <fstream>
#include <array>
#include <set>
#include <windows.h>
#include <cmath>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <climits>

// Herleitung: City.h. Masse in Bloecken, Winkel im Bogenmass (0 = +x, gegen den Uhrzeigersinn).

namespace City {
	using CityModels::C3; using CityModels::Hash; using CityModels::Vary;
	static const float kPi = 3.14159265358979f, kTau = 6.28318530717959f;
	static float U(uint32_t h) { return (float)(h & 0xFFFFFF) / 16777215.0f; }
	static float Wrap(float a) { a = std::fmod(a, kTau); return a < 0 ? a + kTau : a; }
	static float AngDiff(float a, float b) { float d = Wrap(a - b); return d > kPi ? kTau - d : d; }
	static float SignedAng(float a, float b) { float d = Wrap(a - b); return d > kPi ? d - kTau : d; }
	static int Rnd(float v) { return (int)std::floor(v + 0.5f); }

	const char* KindName(int k) {
		switch (k) { case ELF: return "Elfenstadt"; case DWARF: return "Zwergenstadt"; case RANDOM: return "zufaellig je Zelle"; default: return "keine"; }
	}
	int ResolveKind(int kind, uint32_t seed, int cx, int cy) {
		if (kind == RANDOM) return (Hash(cx, cy, 7, seed) & 1) ? ELF : DWARF;
		return (kind == ELF || kind == DWARF) ? kind : NONE;
	}

	static CRITICAL_SECTION& Cs() { static struct S { CRITICAL_SECTION c; S() { InitializeCriticalSection(&c); } } s; return s.c; }
	struct Lock { Lock() { EnterCriticalSection(&Cs()); } ~Lock() { LeaveCriticalSection(&Cs()); } };

	// ---- Modelle ------------------------------------------------------------------------
	// M_WELL: nativer Brunnen 0x882 (building-stone-well.cub, 10 x 10 x 10). Das Dorf setzt ihn mit Kultur 8
	// (RE_placeStructure.md, Zeile 28792) -- dann wird die blaue Markerfarbe Wasser.
	enum { M_TH0 = 0, M_MOTHER = 4, M_DH0 = 5, M_FORT = 9, M_LANDING = 10, M_SMITHY = 11, M_WELL = 12,
	       M_HUT_E = 13, M_HUT_D = 17, M_ALCH_E = 21, M_ALCH_D = 22, M_TEMPLE_E = 23, M_TEMPLE_D = 24, M_ORB = 25,
	       M_STATUE_E = 26, M_STATUE_D = 29, M_THS0 = 32, M_CARPENTER = 36, M_WEAVER = 37, M_COUNT = 38 };   // M_THS0..+3: Baumhaus mit Laden am Stammfuss              // Statuen: je Volk 3 (Strukturen\Stadt\Statue_*.cub)
	static int g_orbZ[2] = { 12, 12 };                          // Hoehe der Kugelmitte im Tempel (Elfen/Zwerge)
	const int kTreeIdBase = 100;          // Piece.id >= 100: Plan-eigener grosser Baum (Plan::treeM[id - 100])
	// prof[z] = groesster waagerechter Abstand eines Voxels von der Modellmitte in Hoehe z (-1 = leer):
	// damit pruefen Baum, Baumhaus und Bruecke ihren Freiraum Schicht fuer Schicht statt mit Kisten.
	// probe: sichtbare Oberflaechenvoxel ohne Markerfarben (x | y << 10 | z << 20) fuer die Pruefschleife;
	// pb: dasselbe Modell OHNE Marker -- zum Nachsetzen (sonst entstuenden Tueren/Betten/Laternen doppelt).
	// src: das Modell selbst (fuer den Zuschnitt, wenn ein Bauwerk doch ueber eine Zonengrenze reicht).
	// glow: Lichtbloecke (Blocktyp 0x0D) je Hoehe als einlagiges Modell (placeStructure hoehlt sonst Saeulen aus);
	// p/pb enthalten sie dann NICHT (ein schon gesetzter Block mit Flag 0x40 wuerde nicht ueberschrieben).
	struct MInfo { LabBridge::PreparedModel* p = nullptr; LabBridge::PreparedModel* pb = nullptr; void* native = nullptr; int sx = 0, sy = 0, sz = 0; std::vector<float> prof; std::vector<uint32_t> probe; Lab::Structure src;
	               bool allGlow = false; std::vector<std::pair<int, LabBridge::PreparedModel*>> glow; };
	static bool GlowVoxel(const MInfo& m, uint8_t r, uint8_t g, uint8_t b) { return m.allGlow || CityModels::IsGlowColor(r, g, b); }
	static MInfo g_m[M_COUNT];
	// Werkstaetten (2026-10-19): Modell und Kultur gehoeren zusammen (Chunk_generateObjects game_misc.cpp:28654-28742):
	// 0x903 Schmiede + 0x0D, 0x904 Zimmerei + 0x0E, 0x905 Weberei + 0x0F. Frueher stand fuer alle drei das
	// Schmiede-Modell -- mit 0x0E/0x0F deutet placeStructure dessen Markerfarben falsch: Stationen fehlten ("nur halb").
	static int WorkModel(int culture) { return culture == 0x0E ? M_CARPENTER : (culture == 0x0F ? M_WEAVER : M_SMITHY); }
	// 2026-10-27: Werkstaetten sind NACHBAUTEN der nativen Modelle (Strukturen\Stadt\Werkstatt_*.cub, Werkzeuge\Stadt\
	// werkstaetten.py): Markerfarben sind Luft, die Stationen (Ofen, Amboss, Saege, Werkbank, Spinnrad, Webstuhl,
	// Anpassungsbank) stehen in der .txt daneben und kommen als native Objekte dazu. Vorher wurde das native Modell mit
	// Kultur 0x0D..0x0F gestempelt; ueber Zonengrenzen entstand es nur halb und schleuderte Spieler hoch.
	// Hoehe wie das Spiel (World::placeSprite-Aufruf in House.cpp, Infinite-Cubes): Ursprung = Bodenoberkante - 7.
	static bool IsNativeModel(int id) { return id == M_LANDING || id == M_WELL; }
	struct Station { int type, x, y, z; float sx, sy, sz; };
	static std::vector<Station> g_work[3];                                  // 0 Schmiede, 1 Zimmerei, 2 Weberei
	static int WorkIndex(int id) { return id == M_SMITHY ? 0 : id == M_CARPENTER ? 1 : id == M_WEAVER ? 2 : -1; }
	static const int kWorkZoff = -6;                                         // 2026-10-27: 1 Block hoeher als nativ (-7) -- schauten zu tief im Boden
	static bool g_firTried = false;
	// Native Baeume, die in der Stadt NICHT stehen sollen (passen nicht ins Bild) -> dort Kastanien:
	// 0x832 fir-tree, 0x833 thorn-tree, 0x840 tree-leaves, 0x99E christmas-tree. Erkannt am Namen und am Inhalt
	// (auch ganzzahlig vergroessert), falls ein Zonen-Modell daraus gemacht wurde.
	static const int kNoTrees[4] = { 0x832, 0x833, 0x840, 0x99E };
	static const char* kNoTreeNames[8] = { "fir-tree", "thorn-tree", "tree-leaves", "christmas-tree", "0x832", "0x833", "0x840", "0x99e" };
	static std::vector<Lab::Structure> g_noTrees;
	static std::vector<Lab::Structure> g_chestnuts;             // Strukturen\Stadt\Kastanie_*.cub (im LAB anpassbar)
	static bool g_chestTried = false;
	// Kastanien: Datei fehlt -> aus CityModels::Chestnut erzeugen und ablegen; danach IMMER die Datei nehmen,
	// damit Aenderungen im LAB (Bauen -> Bibliothek -> Strukturen\Stadt) in neu geplanten Staedten gelten.
	static void LoadChestnuts() {
		std::string dir = Lab::StructuresDir() + "\\Stadt";
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(dir);
		for (int v = 0; v < CityModels::kChestnutVariants; v++) {
			std::string path = dir + "\\" + CityModels::ChestnutName(v) + ".cub", err;
			Lab::Structure s;
			if (Lab::LoadCub(path, s, err) && s.Valid()) { s.name = CityModels::ChestnutName(v); g_chestnuts.push_back(s); continue; }
			s = CityModels::Chestnut(v, 0x5EED17u);                 // = kModelSeed
			std::string e2;
			if (Lab::SaveCub(path, s, e2)) std::cout << "[Stadt] " << path << " angelegt (im LAB anpassbar)." << std::endl;
			g_chestnuts.push_back(s);
		}
	}
	static TreeProvider g_treeProvider = nullptr;
	void SetTreeProvider(TreeProvider f) { g_treeProvider = f; }
	static TreeProvider g_plantProvider = nullptr;                // Pflanzen des Bioms fuer die Pflanzgaerten
	void SetPlantProvider(TreeProvider f) { g_plantProvider = f; }
	static std::vector<CityModels::FenceP> g_thFences[4];     // Treppenzaeune je Baumhaus-Variante (Modellkoordinaten)
	static std::vector<CityModels::FenceP> g_motherFences;    // Gelaender der Wendeltreppe im Mutterbaum
	static bool g_built = false;
	static const uint32_t kModelSeed = 0x5EED17u;
	// Tempel + Kugel: Strukturen\\Stadt\\Tempel_Elfen.cub / Tempel_Zwerge.cub / Tempel_Kugel.cub -- fehlt eine Datei,
	// wird sie erzeugt und abgelegt; danach gilt IMMER die Datei (im LAB anpassbar oder durch ein eigenes Layout ersetzbar).
	// Hoehe der Kugel: 9 Bloecke ueber dem Tempelboden in der Modellmitte (oberster fester Block unter der halben Hoehe).
	static void Put(int id, const Lab::Structure& s);
	static void EnsureTempleFiles() {                                           // nur Dateien (jeder Thread)
		std::string dir = Lab::StructuresDir() + "\\Stadt";
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(dir);
		const char* names[3] = { "Tempel_Elfen", "Tempel_Zwerge", "Tempel_Kugel" };
		for (int k = 0; k < 3; k++) {
			std::string path = dir + "\\" + names[k] + ".cub", e2;
			if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
			Lab::Structure s = k < 2 ? CityModels::Temple(k, kModelSeed) : CityModels::Orb(kModelSeed);
			s.name = names[k];
			if (Lab::SaveCub(path, s, e2)) std::cout << "[Stadt] " << path << " angelegt (im LAB anpassbar)." << std::endl;
		}
	}
	static void LoadTemples() {
		std::string dir = Lab::StructuresDir() + "\\Stadt";
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(dir);
		const char* names[3] = { "Tempel_Elfen", "Tempel_Zwerge", "Tempel_Kugel" };
		for (int k = 0; k < 3; k++) {
			std::string path = dir + "\\" + names[k] + ".cub", err;
			Lab::Structure s;
			if (!(Lab::LoadCub(path, s, err) && s.Valid())) {
				s = k < 2 ? CityModels::Temple(k, kModelSeed) : CityModels::Orb(kModelSeed);
				std::string e2; if (Lab::SaveCub(path, s, e2)) std::cout << "[Stadt] " << path << " angelegt (im LAB anpassbar)." << std::endl;
			}
			s.name = names[k];
			Put(k == 0 ? M_TEMPLE_E : k == 1 ? M_TEMPLE_D : M_ORB, s);
			if (k < 2) {
				int cx = s.sx / 2, cy = s.sy / 2, fl = 0;
				for (int z = 0; z < s.sz / 2; z++) if (s.Solid(cx, cy, z)) fl = z;
				g_orbZ[k] = std::min(fl + 9, std::max(4, s.sz - 5));
			}
		}
		// Statuen am Tempel: Statue_Elfen_1..3 / Statue_Zwerge_1..3 (fehlt eine Datei: erzeugen und ablegen)
		for (int st = 0; st < 2; st++) for (int v = 0; v < 3; v++) {
			std::string nm = std::string(st ? "Statue_Zwerge_" : "Statue_Elfen_") + std::to_string(v + 1);
			std::string path = dir + "\\" + nm + ".cub", err;
			Lab::Structure s;
			if (!(Lab::LoadCub(path, s, err) && s.Valid())) {
				s = CityModels::Statue(v, st, kModelSeed);
				std::string e2; if (Lab::SaveCub(path, s, e2)) std::cout << "[Stadt] " << path << " angelegt (im LAB anpassbar)." << std::endl;
			}
			s.name = nm;
			Put((st ? M_STATUE_D : M_STATUE_E) + v, s);
		}
	}
	static volatile LONG c_zones = 0, c_pieces = 0, c_lanterns = 0, c_fail = 0;

	static void MakeInfo(MInfo& m, const Lab::Structure& s, bool allGlow = false) {
		m.allGlow = allGlow; m.glow.clear();
		Lab::Structure base = s; bool anyGlow = false;
		for (int z = 0; z < s.sz; z++) {
			Lab::Structure layer; bool any = false;
			for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
				uint8_t r, g, b; if (!s.Get(x, y, z, r, g, b) || !GlowVoxel(m, r, g, b)) continue;
				if (!any) { layer.Create(s.sx, s.sy, 1); any = true; }
				layer.Set(x, y, 0, r, g, b); base.Clear(x, y, z); anyGlow = true;
			}
			if (any) m.glow.push_back(std::make_pair(z, LabBridge::Prepare(layer)));
		}
		m.p = LabBridge::Prepare(anyGlow ? base : s); m.sx = s.sx; m.sy = s.sy; m.sz = s.sz; m.src = s;
		m.prof.assign(s.sz, -1.0f);
		const float mx = s.sx * 0.5f, my = s.sy * 0.5f;
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			if (!s.Solid(x, y, z)) continue;
			float dx = std::fabs(x + 0.5f - mx) + 0.5f, dy = std::fabs(y + 0.5f - my) + 0.5f;     // Voxelecke
			m.prof[z] = std::max(m.prof[z], std::sqrt(dx * dx + dy * dy));
		}
		auto Marker = [&](int x, int y, int z) {
			uint8_t r, g, b; s.Get(x, y, z, r, g, b);
			auto L = [](uint8_t v) { return v == 0 || v == 127 || v == 255; };
			return L(r) && L(g) && L(b);
		};
		bool anyMarker = false;
		m.probe.clear();
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			if (!s.Solid(x, y, z)) continue;
			if (Marker(x, y, z)) { anyMarker = true; continue; }
			bool open = !s.Solid(x + 1, y, z) || !s.Solid(x - 1, y, z) || !s.Solid(x, y + 1, z) || !s.Solid(x, y - 1, z) || !s.Solid(x, y, z + 1) || (z > 0 && !s.Solid(x, y, z - 1));
			if (open) m.probe.push_back((uint32_t)x | ((uint32_t)y << 10) | ((uint32_t)z << 20));
		}
		if (anyMarker) {
			Lab::Structure c = anyGlow ? base : s;
			for (int z = 0; z < c.sz; z++) for (int y = 0; y < c.sy; y++) for (int x = 0; x < c.sx; x++) if (c.Solid(x, y, z) && Marker(x, y, z)) c.Clear(x, y, z);
			m.pb = LabBridge::Prepare(c);
		} else m.pb = m.p;
	}
	static void Put(int id, const Lab::Structure& s) { MakeInfo(g_m[id], s, id == M_ORB); }
	static float ProfM(const MInfo& m, int z) { return (z >= 0 && z < (int)m.prof.size()) ? m.prof[z] : -1.0f; }
	static float Prof(int id, int z) { return ProfM(g_m[id], z); }
	static void EnsureModels(void* gc) {
		Lock l;
		if (!g_built) {
			for (int v = 0; v < 4; v++) { Put(M_TH0 + v, CityModels::Treehouse(v, kModelSeed)); g_thFences[v] = CityModels::TreehouseFences(v); Put(M_THS0 + v, CityModels::Treehouse(v, kModelSeed, true)); }
			Put(M_MOTHER, CityModels::MotherTree(kModelSeed)); g_motherFences = CityModels::MotherTreeFences();
			for (int v = 0; v < 4; v++) Put(M_DH0 + v, CityModels::DwarfHouse(v, kModelSeed));
			Put(M_FORT, CityModels::Fortress(kModelSeed));
			for (int st = 0; st < 2; st++) {                                                     // Viertel (2026-10-13)
				for (int v = 0; v < 4; v++) Put((st ? M_HUT_D : M_HUT_E) + v, CityModels::MarketHut(st, v, kModelSeed));
				Put(st ? M_ALCH_D : M_ALCH_E, CityModels::AlchemyHouse(st, kModelSeed));
			}
			LoadTemples();
			{   // Werkstaetten (Nachbau der nativen Modelle)
				const char* names[3] = { "Werkstatt_Schmiede", "Werkstatt_Zimmerei", "Werkstatt_Weberei" };
				const int ids[3] = { M_SMITHY, M_CARPENTER, M_WEAVER };
				for (int k = 0; k < 3; k++) {
					std::string path = Lab::StructuresDir() + "\\Stadt\\" + names[k] + ".cub", err;
					Lab::Structure ws;
					if (!Lab::LoadCub(path, ws, err) || !ws.Valid()) { std::cout << "[Stadt] " << path << " fehlt -- Werkzeuge\\Stadt\\werkstaetten.py ausfuehren." << std::endl; continue; }
					Put(ids[k], ws);
					g_work[k].clear();
					std::ifstream tf(path.substr(0, path.size() - 4) + ".txt"); std::string line;
					while (std::getline(tf, line)) {
						Station st{}; if (sscanf_s(line.c_str(), "station=%d,%d,%d,%d,%f,%f,%f", &st.type, &st.x, &st.y, &st.z, &st.sx, &st.sy, &st.sz) == 7) g_work[k].push_back(st);
					}
				}
			}
			g_m[M_LANDING].sx = 32; g_m[M_LANDING].sy = 32; g_m[M_LANDING].sz = 80;      // landingplace.cub (data1.db)
			g_m[M_WELL].sx = 10; g_m[M_WELL].sy = 10; g_m[M_WELL].sz = 10;              // building-stone-well.cub
			g_built = true;
			std::cout << "[Stadt] Gebaeudemodelle gebaut (4 Baumhaeuser, Mutterbaum, 4 Zwergenhaeuser, Festung)." << std::endl;
		}
		if (!g_chestTried) { g_chestTried = true; LoadChestnuts(); }
		if (gc && !g_firTried) {                                                 // vergroesserte native Baeume
			g_firTried = true;
			for (int id : kNoTrees) {
				Lab::Structure t; std::string err;
				if (LabBridge::ImportNative((cube::GameController*)gc, id, t, err) && t.CropToContent()) g_noTrees.push_back(t);
			}
		}
		if (gc && !g_m[M_LANDING].native) {
			for (int id : { M_LANDING, M_WELL }) {
				const int reg = id == M_LANDING ? 0x887 : 0x882;
				void* m = LabBridge::NativeModelPtr((cube::GameController*)gc, reg);
				int sx, sy, sz;
				if (m && LabBridge::NativeModelDims(m, &sx, &sy, &sz)) {
					g_m[id].native = m; g_m[id].sx = sx; g_m[id].sy = sy; g_m[id].sz = sz;
					std::string err; LabBridge::ImportNative((cube::GameController*)gc, reg, g_m[id].src, err);   // fuer den Zuschnitt
				}
			}
		}
	}

	// ---- Stadtplan -----------------------------------------------------------------------
	struct Piece { int id, x, y, zoff, rot, culture, w, d; };
	// Elfen: Baumhaus (Mitte relativ zur Stadtmitte), Pfad (quadratische Kurve), Haengebruecke (Plattform zu Plattform)
	struct ETree { float x, y; int v, platZ, Rp; };                      // Mitte ganzzahlig (Baumhaeuser werden nie gedreht)
	struct EPath { float x0, y0, cxp, cyp, x1, y1; bool lamp; };
	// Haengebruecke: Zellen relativ zur Stadtmitte, z wie Modellhoehe (0 = Bodenoberkante + 1)
	struct BCell { int x, y, z; };
	struct EBridge {
		std::vector<BCell> deck, lamps;                                     // Planken 5 breit (gerade), Laternen unter dem Deck
		std::vector<CityModels::FenceP> fences;                              // Zaeune (x/y relativ zur Stadtmitte)
		int bx0, by0, bx1, by1;                                              // Huelle
	};
	struct EBig { float x, y; int id, rot; };                              // vergroesserter nativer Baum (Mitte relativ zur Stadtmitte)
	struct ERing { float r, ph1, ph2; };
	// Stadtviertel (2026-10-13, organisch): unregelmaessige Flaeche (Ellipse ra/rb, Drehung psi, gewellter Rand),
	// Platz in der Mitte, Bauwerke per Platzsuche (immer ganz in EINER Zone), Gehege, Felder, Wege, NPC-Posten.
	// D_FARM = Hof (Felder + Gehege) zwischen den Haeusern.
	enum { D_WORK = 0, D_TRADE, D_ALCH, D_TEMPLE, D_ANIMAL, D_FARM, D_COUNT };
	enum { NP_VENDOR = 0, NP_ALCH, NP_ANIMAL, NP_MERCHANT, NP_ORB, NP_SCARECROW };
	struct EDist {
		int kind; float x, y, r;                                              // Mitte relativ zur Stadtmitte, r = groesste Ausdehnung
		float ra = 0, rb = 0, psi = 0, ph1 = 0, ph2 = 0;                      // Halbachsen, Drehung, Randwellen
		float lim0 = 0, lim1 = 0;                                             // Zwerge: Ringband (Abstand zur Stadtmitte), 0 = frei
		float plaza = 0;                                                      // Platzradius in der Mitte
	};
	// Feld (relativ, inklusive) wie das native Feld (Zonenzelle Typ 6, game_misc.cpp:24703): Furchen je 2 Reihen,
	// auf jeder zweiten Doppelreihe native Pflanzen-Props (Liste Zone+4). crop: Index in kCrops.
	struct FieldR { int x0, y0, x1, y1, crop; bool alongX; };
	// Pflanzgarten (relativ, inklusive): Beet mit Steinrand; Pflanzen des Bioms/der Mod (Bloecke) + native Blumen-Props.
	struct GardenR { int x0, y0, x1, y1, style; };                        // style 0 Blumen, 1 Kraeuter (Mod-Pflanzen), 2 gemischt
	struct NpcP { int kind, x, y, z, a, n; };                                // z: ueber dem Boden (0 = Boden + 1); a: Klasse/Rasse; n: Anzahl
	struct ObjP { int type; float x, y; int z, orient; float sx, sy, sz; };  // natives 0x188-Objekt (relativ), z wie NpcP
	struct PenR { int x0, y0, x1, y1; };                                     // Gehege (relativ, inklusive)
	struct Plan {
		int kind = NONE, cx = 0, cy = 0; float R = 0; uint32_t seed = 0;
		std::vector<Piece> pieces;
		float av0 = 0; float avPh[6] = {}, avPh2[6] = {}; std::vector<ERing> erings; float lAx = 0, lAy = 0;   // Elfen
		std::vector<ETree> trees; std::vector<EPath> trails; std::vector<EBridge> bridges;
		std::map<int64_t, std::vector<int>> treeGrid;                       // 64er-Raster der Baumhaeuser
		std::map<int64_t, std::vector<int>> bridgeGrid;                     // 64er-Raster: Bruecken, deren Huelle das Feld beruehrt
		std::vector<EBig> bigs; std::map<int64_t, std::vector<int>> bigGrid; // grosse Baeume (EBig.id = Index in treeM)
		std::vector<MInfo> treeM; std::vector<std::string> treeNames;      // vergroesserte Baeume des Bioms (+ nativer 0x832)
		std::vector<BCell> spawnPts;                                        // Bewohner: Wege (z = 0) und Brueckendecks (z = Deck + 1), relativ zur Mitte
		std::vector<std::pair<int, int>> wells;                             // Brunnenmitten (relativ), Elfen: mit Brunnenplatz
		std::vector<std::vector<int>> treeGroups;                           // je Baumart: Indizes in treeM, groesster zuerst
		int biome = 0;
		float Rw = 0; std::vector<float> rings, ringHalf; std::vector<float> towerA;   // Zwerge
		int landing = -1;
		std::vector<EDist> dists; std::vector<NpcP> npcs; std::vector<ObjP> objs;   // Viertel
		std::vector<CityModels::FenceP> penFences; std::vector<PenR> pens;
		std::vector<FieldR> fields; std::vector<EPath> dpaths;              // Felder, Viertelwege (relativ)
		std::vector<EPath> roads;                                           // Ladenstrassen (5 breit) zu den Haendlern (relativ)
		std::vector<std::array<float, 4>> lampPosts;                        // Laternenpfosten (relativ x, y, Armrichtung ax, ay)
		std::vector<GardenR> gardens; std::vector<MInfo> gardenM;           // Pflanzgaerten + Pflanzen des Bioms
		std::vector<int> dpieces;                                           // Indizes der Viertel-Bauwerke in pieces
		volatile LONG zg = LONG_MIN;          // Bodenoberkante der Stadt: EINMAL gemessen, fuer alle Zonen gleich
	};
	static int64_t GKey(int gx, int gy) { return ((int64_t)gx << 32) ^ (uint32_t)gy; }
	static std::map<uint64_t, Plan*> g_plans;
	static uint64_t PlanKey(const Site& s) {
		uint64_t k = (uint64_t)s.seed * 0x9E3779B97F4A7C15ull;
		k ^= ((uint64_t)(uint32_t)s.cx << 1) * 0xC2B2AE3D27D4EB4Full;
		k ^= ((uint64_t)(uint32_t)s.cy << 3) * 0x165667B19E3779F9ull;
		k ^= (uint64_t)s.kind * 0x27D4EB2F165667C5ull ^ (uint64_t)(int)s.radius * 0x94D049BB133111EBull;
		return k;
	}
	static int FloorDiv256(int v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); }
	// Liegt das Bauwerk ganz in EINER Zone? (placeStructure baut nur, wenn die untere Ecke in der Zone liegt --
	// belegt durch die Pruefschleife: Teile mit Ecke in der Nachbarzone fehlten zu 100 %.)
	static bool FitsZone(const Piece& p) {
		return FloorDiv256(p.x) == FloorDiv256(p.x + p.w - 1) && FloorDiv256(p.y) == FloorDiv256(p.y + p.d - 1);
	}
	static bool Overlap(const Piece& a, const Piece& b, int m) {
		return a.x - m < b.x + b.w && b.x - m < a.x + a.w && a.y - m < b.y + b.d && b.y - m < a.y + a.d;
	}
	struct Plan;
	static void AddStations(Plan& p, const Piece& pc);
	static Piece MakePiece(int id, int cx, int cy, int rot, int culture, int zoff) {
		const MInfo& m = g_m[id];
		int w = (rot & 1) ? m.sy : m.sx, d = (rot & 1) ? m.sx : m.sy;
		Piece p{ id, cx - w / 2, cy - d / 2, zoff, rot, culture, w, d };
		return p;
	}
	// Stationen eines Werkstatt-Nachbaus als native Objekte (wie placeSprite: Position = Voxel + 0.5, Drehung -rot)
	static void AddStations(Plan& p, const Piece& pc) {
		const int wi = WorkIndex(pc.id); if (wi < 0) return;
		const MInfo& m = g_m[pc.id]; const int sx = m.src.Valid() ? m.src.sx : m.sx, sy = m.src.Valid() ? m.src.sy : m.sy;
		for (const Station& st : g_work[wi]) {
			int wx, wy;                                                          // Drehung wie StampPiece/placeStructure
			switch (pc.rot & 3) {
				case 0: wx = pc.x + st.x; wy = pc.y + st.y; break;
				case 1: wx = pc.x + st.y; wy = pc.y + (sx - st.x) - 1; break;
				case 2: wx = pc.x + (sx - st.x) - 1; wy = pc.y + (sy - st.y) - 1; break;
				default: wx = pc.x + (sy - st.y) - 1; wy = pc.y + st.x; break;
			}
			p.objs.push_back({ st.type, wx + 0.5f - p.cx, wy + 0.5f - p.cy, 1 + pc.zoff + st.z, (4 - (pc.rot & 3)) & 3, st.sx, st.sy, st.sz });
		}
	}
	// kleinster und groesster Abstand eines Rechtecks zur Mitte
	static void RectDist(const Piece& p, float cx, float cy, float* dmin, float* dmax) {
		float nx = std::max((float)p.x, std::min(cx, (float)(p.x + p.w))), ny = std::max((float)p.y, std::min(cy, (float)(p.y + p.d)));
		*dmin = std::sqrt((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy));
		float fx = std::max(std::fabs(p.x - cx), std::fabs(p.x + p.w - cx)), fy = std::max(std::fabs(p.y - cy), std::fabs(p.y + p.d - cy));
		*dmax = std::sqrt(fx * fx + fy * fy);
	}
	// beruehrt das Rechteck (+m) einen Strahl ab der Mitte in Richtung a?
	static bool RectNearRay(const Piece& p, float cx, float cy, float a, float m) {
		float ux = std::cos(a), uy = std::sin(a);
		float qmin = 1e9f, qmax = -1e9f, tmax = -1e9f;
		float xs[2] = { (float)p.x - cx, (float)(p.x + p.w) - cx }, ys[2] = { (float)p.y - cy, (float)(p.y + p.d) - cy };
		for (float x : xs) for (float y : ys) { float t = x * ux + y * uy, q = -x * uy + y * ux; qmin = std::min(qmin, q); qmax = std::max(qmax, q); tmax = std::max(tmax, t); }
		return tmax > 0.0f && qmin <= m && qmax >= -m;
	}

	// ---- Elfen: Wegenetz ----------------------------------------------------------------------
	// Sechs geschwungene Alleen (seitliche Auslenkung zweier Sinuswellen), geschwungene Ringwege,
	// Trampelpfade zwischen benachbarten Baumhaeusern und vom Baumhaus zum naechsten Weg.
	static float AvOffset(const Plan& p, int k, float r) { return 10.0f * std::sin(r / 41.0f + p.avPh[k]) + 5.0f * std::sin(r / 17.0f + p.avPh2[k]); }
	static float RingR(const ERing& e, float th) { return e.r + 8.0f * std::sin(3.0f * th + e.ph1) + 4.0f * std::sin(7.0f * th + e.ph2); }
	// Abstand zum naechsten Allee-/Ringweg (Mitte), r/th = Polarkoordinaten relativ zur Stadtmitte
	static float MainPathDist(const Plan& p, float dx, float dy) {
		float r = std::sqrt(dx * dx + dy * dy), th = std::atan2(dy, dx), best = 1e9f;
		if (r >= 66.0f && r <= p.R - 6.0f)
			for (int k = 0; k < 6; k++) {
				float a = p.av0 + k * kTau / 6.0f;
				if (AngDiff(th, a) > 1.2f) continue;
				best = std::min(best, std::fabs(r * SignedAng(th, a) - AvOffset(p, k, r)));
			}
		for (const ERing& e : p.erings) best = std::min(best, std::fabs(r - RingR(e, th)));
		return best;
	}
	static void NearestMainPathPoint(const Plan& p, float dx, float dy, float* ox, float* oy, float* dist) {
		float r = std::sqrt(dx * dx + dy * dy), th = std::atan2(dy, dx);
		*dist = 1e9f;
		for (int k = 0; k < 6; k++) {
			float a = p.av0 + k * kTau / 6.0f + AvOffset(p, k, r) / std::max(r, 1.0f);
			float px = std::cos(a) * r, py = std::sin(a) * r, d = std::sqrt((px - dx) * (px - dx) + (py - dy) * (py - dy));
			if (d < *dist) { *dist = d; *ox = px; *oy = py; }
		}
		for (const ERing& e : p.erings) {
			float rr = RingR(e, th), px = std::cos(th) * rr, py = std::sin(th) * rr, d = std::fabs(r - rr);
			if (d < *dist) { *dist = d; *ox = px; *oy = py; }
		}
	}
	static float SegDist(float px, float py, float ax, float ay, float bx, float by) {
		float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
		float t = l2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / l2 : 0.0f;
		t = std::max(0.0f, std::min(1.0f, t));
		float ex = ax + vx * t - px, ey = ay + vy * t - py;
		return std::sqrt(ex * ex + ey * ey);
	}

	// ---- Elfen: Haengebruecken (gerade, ohne Stuetzen) ------------------------------------------
	// Die Baumhaeuser stehen in Reihen und Spalten eines 64er-Rasters. Versetzt wird je SPALTE in x und
	// je REIHE in y (nie je Haus): Nachbarn einer Reihe haben dieselbe y-Mitte, Nachbarn einer Spalte
	// dieselbe x-Mitte. Plattform-Ausgaenge liegen in +x/+y/-x/-y (Baumhaeuser ungedreht) -> jede Bruecke
	// verbindet zwei gegenueberliegende Ausgaenge mit EINEM geraden Lauf. Keine Ecke, keine Stuetze.
	// Planken 5 breit, auf den Randreihen native Zaeune (2 lang, je Stufenpaar gleiche Hoehe).
	// Hoehe paarweise (2 Felder), Unterschied zwischen Paaren hoechstens 1, leichter Durchhang.
	// Lauf von (x0,y0) bis (x1,y1) (eine Koordinate gleich), Hoehe zs -> ze
	static bool AddLeg(EBridge& b, int x0, int y0, int x1, int y1, int zs, int ze) {
		int ax = (x1 > x0) - (x1 < x0), ay = (y1 > y0) - (y1 < y0);
		int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
		if (n < 2) return false;
		int pairs = (n + 1) / 2;
		float sag = std::min(2.0f, n * 0.05f);
		std::vector<int> Z(pairs);
		for (int jj = 0; jj < pairs; jj++) {
			float t = pairs > 1 ? (float)jj / (pairs - 1) : 0.0f;
			Z[jj] = Rnd(zs + (ze - zs) * t - sag * 4.0f * t * (1.0f - t));
			if (jj > 0 && std::abs(Z[jj] - Z[jj - 1]) > 1) return false;
		}
		if (std::abs(Z[0] - zs) > 1 || std::abs(Z[pairs - 1] - ze) > 1) return false;
		int px = -ay, py = ax;                                            // Querrichtung
		for (int ii = 0; ii < n; ii++) {
			int x = x0 + ax * ii, y = y0 + ay * ii, z = Z[ii / 2];
			for (int w = -2; w <= 2; w++) b.deck.push_back({ x + px * w, y + py * w, z });
		}
		for (int jj = 0; jj < pairs; jj++) {
			int ia = 2 * jj, ib = 2 * jj + 1;
			if (ib >= n) { ia = n - 2; ib = n - 1; }
			int xa = x0 + ax * ia, ya = y0 + ay * ia, xb = x0 + ax * ib, yb = y0 + ay * ib;
			int z = std::max(Z[ia / 2], Z[ib / 2]) + 1;
			for (int sgn = -1; sgn <= 1; sgn += 2) {
				CityModels::FenceP f;
				f.z = z;
				if (ax != 0) { f.orient = 0; f.x = (float)std::max(xa, xb); f.y = ya + py * 2 * sgn + 0.5f; }
				else { f.orient = 1; f.x = xa + px * 2 * sgn + 0.5f; f.y = (float)std::max(ya, yb); }
				b.fences.push_back(f);
			}
		}
		// Laterne unter der Deckmitte (haengt unter den Planken, leuchtet nach unten in den Wald)
		if (n >= 12) { int m = n / 2; b.lamps.push_back({ x0 + ax * m, y0 + ay * m, Z[m / 2] - 1 }); }
		return true;
	}
	static bool BridgeClear(const Plan& p, const EBridge& b, int i, int j, float lcx, float lcy) {
		for (const BCell& c : b.deck) {
			if ((float)c.x * c.x + (float)c.y * c.y < 70.0f * 70.0f) return false;
			if (std::fabs(c.x - lcx) < 20 && std::fabs(c.y - lcy) < 20) return false;
		}
		int g0x = (int)std::floor(b.bx0 / 64.0f) - 1, g1x = (int)std::floor(b.bx1 / 64.0f) + 1;
		int g0y = (int)std::floor(b.by0 / 64.0f) - 1, g1y = (int)std::floor(b.by1 / 64.0f) + 1;
		for (int gx = g0x; gx <= g1x; gx++) for (int gy = g0y; gy <= g1y; gy++) {
			auto it = p.treeGrid.find(GKey(gx, gy));
			if (it == p.treeGrid.end()) continue;
			for (int q : it->second) {
				if (q == i || q == j) continue;
				const ETree& T = p.trees[q]; float lim = T.Rp + 3.0f;
				for (const BCell& c : b.deck) if ((c.x - T.x) * (c.x - T.x) + (c.y - T.y) * (c.y - T.y) < lim * lim) return false;
			}
		}
		for (const EBridge& o : p.bridges) {                               // keine Kreuzung (ueber eine Rasterluecke hinweg)
			if (o.bx1 < b.bx0 - 3 || o.bx0 > b.bx1 + 3 || o.by1 < b.by0 - 3 || o.by0 > b.by1 + 3) continue;
			for (const BCell& c : b.deck) for (const BCell& d : o.deck) if (std::abs(c.x - d.x) <= 2 && std::abs(c.y - d.y) <= 2) return false;
		}
		return true;
	}
	// Gerade Bruecke von A (Ausgang +axis) nach B (Ausgang -axis); A und B liegen auf derselben Rasterlinie.
	static bool MakeBridge(Plan& p, int i, int j, int axis, float lcx, float lcy) {
		const ETree& A = p.trees[i]; const ETree& B = p.trees[j];
		int Ax = (int)A.x, Ay = (int)A.y, Bx = (int)B.x, By = (int)B.y;
		EBridge b;
		bool ok;
		if (axis == 0) { if (Ay != By) return false; ok = AddLeg(b, Ax + A.Rp + 1, Ay, Bx - B.Rp - 1, By, A.platZ, B.platZ); }
		else           { if (Ax != Bx) return false; ok = AddLeg(b, Ax, Ay + A.Rp + 1, Bx, By - B.Rp - 1, A.platZ, B.platZ); }
		if (!ok || (int)b.deck.size() / 5 > 100) return false;
		b.bx0 = b.by0 = INT_MAX; b.bx1 = b.by1 = INT_MIN;
		for (const BCell& c : b.deck) { b.bx0 = std::min(b.bx0, c.x); b.bx1 = std::max(b.bx1, c.x); b.by0 = std::min(b.by0, c.y); b.by1 = std::max(b.by1, c.y); }
		if (!BridgeClear(p, b, i, j, lcx, lcy)) return false;
		int idx = (int)p.bridges.size();
		for (int gx = (int)std::floor(b.bx0 / 64.0f); gx <= (int)std::floor(b.bx1 / 64.0f); gx++)
			for (int gy = (int)std::floor(b.by0 / 64.0f); gy <= (int)std::floor(b.by1 / 64.0f); gy++) p.bridgeGrid[GKey(gx, gy)].push_back(idx);
		p.bridges.push_back(std::move(b));
		return true;
	}
	// ---- Elfen: vergroesserte native Baeume in der Mitte zwischen den Baumhaeusern --------------------
	// Freiraum Schicht fuer Schicht (Profil = groesster Radius je Hoehe): Baum gegen Baumhaus, Mutterbaum,
	// Brueckendeck (+ Kopffreiheit 4 und Zaun) -- nichts darf sich beruehren.
	static bool BigClear(const Plan& p, const MInfo& m, float x, float y) {
		for (int ax = (int)std::floor(x / 64.0f) - 2; ax <= (int)std::floor(x / 64.0f) + 2; ax++)
			for (int ay = (int)std::floor(y / 64.0f) - 2; ay <= (int)std::floor(y / 64.0f) + 2; ay++) {
				auto it = p.treeGrid.find(GKey(ax, ay));
				if (it == p.treeGrid.end()) continue;
				for (int q : it->second) {
					const ETree& T = p.trees[q];
					float d = std::sqrt((T.x - x) * (T.x - x) + (T.y - y) * (T.y - y));
					if (d > 120.0f) continue;
					for (int z = 0; z < m.sz; z++) { float a = ProfM(m, z), b = Prof(M_TH0 + T.v, z); if (a >= 0 && b >= 0 && d < a + b + 1.5f) return false; }
				}
				auto ib = p.bridgeGrid.find(GKey(ax, ay));
				if (ib == p.bridgeGrid.end()) continue;
				for (int bi : ib->second) {
					const EBridge& B = p.bridges[bi];
					for (const BCell& c : B.deck) {
						float d = std::sqrt((c.x - x) * (c.x - x) + (c.y - y) * (c.y - y));
						if (d > 40.0f) continue;
						for (int z = c.z - 1; z <= c.z + 4; z++) { float a = ProfM(m, z); if (a >= 0 && d < a + 1.0f) return false; }
					}
				}
			}
		float d0 = std::sqrt(x * x + y * y);                                  // Mutterbaum in der Mitte
		for (int z = 0; z < m.sz; z++) { float a = ProfM(m, z), b = Prof(M_MOTHER, z); if (a >= 0 && b >= 0 && d0 < a + b + 2.0f) return false; }
		return true;
	}
	static float TrailDist(const Plan& p, float x, float y, float lim) {
		float best = 1e9f;
		for (int pass = 0; pass < 2; pass++) for (const EPath& t : pass ? p.roads : p.trails) {
			float bx0 = std::min(std::min(t.x0, t.x1), t.cxp), bx1 = std::max(std::max(t.x0, t.x1), t.cxp);
			float by0 = std::min(std::min(t.y0, t.y1), t.cyp), by1 = std::max(std::max(t.y0, t.y1), t.cyp);
			if (x < bx0 - lim || x > bx1 + lim || y < by0 - lim || y > by1 + lim) continue;
			for (int k = 0; k <= 24; k++) {
				float u = k / 24.0f, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				best = std::min(best, std::sqrt((px - x) * (px - x) + (py - y) * (py - y)));
			}
		}
		return best;
	}

	// Baumsatz des Bioms: Liefer-Funktion aus BiomeMod (Zonen-Modell-Baeume, Hangbaeume, Pflanzenbaeume,
	// Pflanzendateien) + nativer Baum 0x832. Ganzzahlig vergroessert (Voxel bleiben wuerfelig) auf etwa
	// 66 Bloecke Hoehe, hoechstens 64 breit / 110 hoch; dazu je Art eine Stufe kleiner (passt oefter).
	static bool IsNoTree(const Lab::Structure& t) {
		std::string n = t.name + " " + t.origin;
		for (char& ch : n) ch = (char)tolower((unsigned char)ch);
		for (const char* k : kNoTreeNames) if (n.find(k) != std::string::npos) return true;
		for (const Lab::Structure& b : g_noTrees) {
			for (int k = 1; k <= 5; k++) {
				if (t.sx != b.sx * k || t.sy != b.sy * k || t.sz != b.sz * k) continue;
				bool same = true;
				for (int z = 0; z < t.sz && same; z += 2) for (int y = 0; y < t.sy && same; y += 2) for (int x = 0; x < t.sx && same; x += 2) {
					uint8_t r1, g1, b1, r2, g2, b2;
					bool s1 = t.Get(x, y, z, r1, g1, b1), s2 = b.Get(x / k, y / k, z / k, r2, g2, b2);
					if (s1 != s2 || (s1 && (r1 != r2 || g1 != g2 || b1 != b2))) same = false;
				}
				if (same) return true;
			}
		}
		return false;
	}
	static void BuildTreeSet(Plan& p) {
		std::vector<Lab::Structure> src;
		bool withNative = g_treeProvider ? g_treeProvider(p.biome, src) : true;   // true = Biom hat native Baeume
		// Laubbaeume: Kastanien aus den Dateien. Je Stadt EINE Jahreszeit der Rosskastanie (Bluete/Sommer/Herbst,
		// fest aus Samen + Stadtmitte) und die Edelkastanie -- keine bluehenden neben herbstlichen Baeumen.
		if (g_chestnuts.size() >= 4) {
			int season = (int)(Hash(p.cx, p.cy, 61, p.seed) % 3u);
			src.push_back(g_chestnuts[season]); src.push_back(g_chestnuts[3]);
		} else for (const Lab::Structure& c : g_chestnuts) src.push_back(c);
		(void)withNative;                                                       // native Baeume -> Kastanien (siehe kNoTrees)
		for (const Lab::Structure& s0 : src) {
			Lab::Structure t = s0;
			if (!t.Valid() || !t.CropToContent() || t.sz < 6) continue;
			if (IsNoTree(t)) { std::cout << "[Stadt]   " << t.name << ": nativer Baum (0x832/0x833/0x840/0x99E) -- in der Stadt durch Kastanien ersetzt." << std::endl; continue; }
			int f = std::max(1, std::min(5, Rnd(66.0f / t.sz)));
			while (f > 1 && (t.sx * f > 64 || t.sy * f > 64 || t.sz * f > 110)) f--;
			std::vector<float> sizes; sizes.push_back((float)f);
			if (f > 1) sizes.push_back((float)(f - 1)); else sizes.push_back(0.8f);   // eine Stufe kleiner (passt oefter)
			std::vector<int> grp;
			for (float ff : sizes) {
				Lab::Structure u = t;
				if (ff != 1.0f && !u.Scale(ff, ff, ff)) continue;
				if (u.sz < 24 || u.sx > 64 || u.sy > 64) continue;                     // zu klein fuer die Mitte / zu breit
				MInfo m; MakeInfo(m, u);
				if (!m.p) continue;
				grp.push_back((int)p.treeM.size());
				p.treeM.push_back(std::move(m));
				char fb[16]; snprintf(fb, sizeof(fb), " x%g", ff);
				p.treeNames.push_back(t.name + fb);
			}
			if (!grp.empty()) p.treeGroups.push_back(grp);
		}
	}

	// Brunnen setzen: ganz in einer Zone, ohne Ueberschneidung mit reservierten Bauwerken (Abstand 4).
	static bool AddWell(Plan& p, float x, float y, const std::vector<Piece>& avoid) {
		Piece w = MakePiece(M_WELL, p.cx + Rnd(x), p.cy + Rnd(y), 0, 8, 0);
		if (!FitsZone(w)) return false;
		for (const Piece& q : avoid) if (Overlap(w, q, 4)) return false;
		for (const Piece& q : p.pieces) if (Overlap(w, q, 4)) return false;
		p.pieces.push_back(w); p.wells.push_back(std::make_pair(Rnd(x), Rnd(y)));
		return true;
	}
	static bool NearWell(const Plan& p, float x, float y, float lim) {
		for (const auto& w : p.wells) if (std::fabs(w.first - x) < lim && std::fabs(w.second - y) < lim) return true;
		return false;
	}

	// ---- Stadtviertel (2026-10-13, Stand 3: gross, im Wald, native Felder/Deko) -----------------------
	// Ein Viertel ist ein grosses Gebiet (Elfen: Radius ~140) MITTEN im Wald: es raeumt keine Lichtung, sondern
	// setzt seine Bauwerke, Gehege, Felder und Gaerten mit Abstand (Elfen 12, Zwerge 6) zwischen die Baumhaeuser.
	// Baumhaeuser, grosse Baeume und Bruecken weichen nur diesen Teilen aus (ElementHit). Jedes Bauwerk liegt
	// GANZ in einer Zone (FitsZone). Felder wie das native Feld (Furchen + Pflanzen-Props), Gaerten mit Pflanzen
	// des Bioms und nativen Blumen, Deko nur aus nativen Objekten (Fass, Kiste, Sack, Bank, Strassenlaterne,
	// Marktstand, Lagerfeuer). Wege von der Viertelmitte zu jeder Tuer, jedem Gatter, Feld und Garten.
	const char* DistrictName(int k) {
		static const char* n[D_COUNT] = { "Arbeitsviertel", "Haendlerviertel", "Alchemieviertel", "Tempelviertel", "Tierviertel", "Hof" };
		return (k >= 0 && k < D_COUNT) ? n[k] : "?";
	}
	static AnimalProvider g_animalProvider = nullptr;
	void SetAnimalProvider(AnimalProvider f) { g_animalProvider = f; }
	// Native Feldpflanzen (Props, Liste Zone+4; Plaetze aus GameController-ctor, Groessen aus dem Feldgenerator 0x5B1C..)
	struct Crop { int slot; float scale; int flags; bool rotate; const char* name; };
	static const Crop kCrops[6] = {
		{ 0x1D, 0.20f, 4, true,  "Weizen" },   { 0x1E, 0.20f, 4, true,  "Mais" },      { 0x17, 0.10f, 0, true, "Kuerbis" },
		{ 0x19, 0.20f, 4, false, "Sonnenblume" }, { 0x1A, 0.20f, 0, true, "Bohnen" },   { 0x18, 0.15f, 0, true, "Ananas" } };
	static const int kCropWeights[6] = { 3, 2, 2, 1, 1, 1 };
	// Native Blumen-Props fuer die Gaerten: flowers2, flowers, echinacea2, tulips-colorful, cornflower, desert-flower01/02
	struct Flower { int slot; float scale; };
	static const Flower kFlowers[7] = { { 0x00, 0.09f }, { 0x01, 0.09f }, { 0x0B, 0.07f }, { 0x14, 0.12f }, { 0x15, 0.09f }, { 0x1B, 0.08f }, { 0x1C, 0.08f } };
	// Native Objekte (Typ, Halbmasse = Modellmass / 8 wie beim Zaun 16x3x12 -> 2/1/1.5)
	enum { O_BARREL = 0x18, O_CRATE = 0x19, O_SACK = 0x1B, O_BENCH = 0x12, O_LIGHT = 0x33, O_CAMPFIRE = 0x41 };
	static void ObjSize(int type, float* sx, float* sy, float* sz) {
		switch (type) {
			case O_BARREL: *sx = 1.6f; *sy = 1.6f; *sz = 1.75f; break;      // barrel.cub 13x13x14
			case O_CRATE:  *sx = 1.25f; *sy = 1.25f; *sz = 1.25f; break;    // crate.cub 10x10x10
			case O_SACK:   *sx = 1.6f; *sy = 1.6f; *sz = 1.4f; break;       // sack.cub 13x13x11
			case O_BENCH:  *sx = 1.6f; *sy = 0.6f; *sz = 0.5f; break;       // bench.cub 13x5x4
			case O_LIGHT:  *sx = 0.6f; *sy = 0.6f; *sz = 3.1f; break;       // street-light01.cub 5x5x25
			default:       *sx = 2.4f; *sy = 2.4f; *sz = 0.5f; break;       // Lagerfeuer (wie bisher)
		}
	}
	// Vorderseite (Modell y = 0) zeigt in Richtung (fx, fy) -- wie die Zwergenhaeuser (Drehung wie placeStructure)
	static int RotFacing(float fx, float fy) { return std::fabs(fx) > std::fabs(fy) ? (fx > 0 ? 3 : 1) : (fy > 0 ? 2 : 0); }
	// Modellpunkt (mx, my) -> Welt, gleiche Drehung wie StampPiece/placeStructure
	static void ModelToWorld(const Piece& pc, int msx, int msy, int mx, int my, int* wx, int* wy) {
		switch (pc.rot & 3) {
			case 0: *wx = pc.x + mx; *wy = pc.y + my; break;
			case 1: *wx = pc.x + my; *wy = pc.y + (msx - mx) - 1; break;
			case 2: *wx = pc.x + (msx - mx) - 1; *wy = pc.y + (msy - my) - 1; break;
			default: *wx = pc.x + (msy - my) - 1; *wy = pc.y + mx; break;
		}
	}
	// Rand des Gebiets in Richtung th (Abstand zur Mitte)
	static float DistEdge(const EDist& d, float th) {
		float c = std::cos(th - d.psi), s = std::sin(th - d.psi);
		float e = 1.0f / std::sqrt(c * c / (d.ra * d.ra) + s * s / (d.rb * d.rb));
		return e * (1.0f + 0.11f * std::sin(2.0f * th + d.ph1) + 0.06f * std::sin(5.0f * th + d.ph2));
	}
	static EDist MakeDist(int kind, float x, float y, float ra, float rb, float psi, float plaza, uint32_t h) {
		EDist d; d.kind = kind; d.x = x; d.y = y; d.ra = ra; d.rb = rb; d.psi = psi; d.plaza = plaza;
		d.ph1 = U(h) * kTau; d.ph2 = U(h * 2654435761u + 77u) * kTau;
		d.r = std::max(ra, rb) * 1.18f;
		return d;
	}
	static bool InBlob(const EDist& d, float x, float y, float m) {
		float dx = x - d.x, dy = y - d.y, q = dx * dx + dy * dy;
		if (q > d.r * d.r) return false;
		float e = DistEdge(d, std::atan2(dy, dx)) - m;
		if (e <= 0.0f || q > e * e) return false;
		if (d.lim1 > 0.0f) { float rc = std::sqrt(x * x + y * y); if (rc < d.lim0 + m || rc > d.lim1 - m) return false; }
		return true;
	}
	static bool InPlaza(const EDist& d, float x, float y) {
		if (d.plaza <= 0.0f) return false;
		float dx = x - d.x, dy = y - d.y, pr = d.plaza * (1.0f + 0.08f * std::sin(3.0f * std::atan2(dy, dx) + d.ph2));
		return dx * dx + dy * dy <= pr * pr;
	}
	static bool RectInBlob(const EDist& d, float x0, float y0, float x1, float y1, float m) {
		for (int i = 0; i <= 4; i++) {
			float t = i / 4.0f, xi = x0 + (x1 - x0) * t, yi = y0 + (y1 - y0) * t;
			if (!InBlob(d, xi, y0, m) || !InBlob(d, xi, y1, m) || !InBlob(d, x0, yi, m) || !InBlob(d, x1, yi, m)) return false;
		}
		return true;
	}
	// kleinster Abstand Punkt -> Rechteck [x0,x1) x [y0,y1)
	static float RectGap(float x0, float y0, float x1, float y1, float px, float py) {
		float dx = std::max(std::max(x0 - px, 0.0f), px - x1), dy = std::max(std::max(y0 - py, 0.0f), py - y1);
		return std::sqrt(dx * dx + dy * dy);
	}
	// Rechteck (relativ, exklusiv) frei von Bauwerken, Gehegen, Feldern, Gaerten und Plaetzen (Abstand m)?
	static bool ElemFree(const Plan& p, float x0, float y0, float x1, float y1, float m) {
		auto Hit = [&](float a0, float b0, float a1, float b1) { return x0 - m < a1 && a0 - m < x1 && y0 - m < b1 && b0 - m < y1; };
		for (const Piece& q : p.pieces) if (Hit((float)(q.x - p.cx), (float)(q.y - p.cy), (float)(q.x - p.cx + q.w), (float)(q.y - p.cy + q.d))) return false;
		for (const PenR& g : p.pens) if (Hit((float)g.x0, (float)g.y0, g.x1 + 1.0f, g.y1 + 1.0f)) return false;
		for (const FieldR& f : p.fields) if (Hit((float)f.x0, (float)f.y0, f.x1 + 1.0f, f.y1 + 1.0f)) return false;
		for (const GardenR& g : p.gardens) if (Hit((float)g.x0, (float)g.y0, g.x1 + 1.0f, g.y1 + 1.0f)) return false;
		for (const EDist& d : p.dists) if (d.plaza > 0.0f) { float nx = std::max(x0, std::min(d.x, x1)), ny = std::max(y0, std::min(d.y, y1)), pr = d.plaza * 1.1f + m; if ((nx - d.x) * (nx - d.x) + (ny - d.y) * (ny - d.y) < pr * pr) return false; }
		return true;
	}
	// Elfen: Rechteck nicht auf Allee/Ringweg/Mittelplatz, nicht an einem Brunnen
	static bool NearWell(const Plan& p, float x, float y, float lim);
	static bool PathOK(const Plan& p, float x0, float y0, float x1, float y1) {
		if (p.kind != ELF) return true;
		for (int j = 0; j <= 2; j++) for (int i = 0; i <= 2; i++) {
			float x = x0 + (x1 - x0) * i * 0.5f, y = y0 + (y1 - y0) * j * 0.5f;
			if (MainPathDist(p, x, y) < 5.0f || x * x + y * y < 84.0f * 84.0f) return false;
		}
		return !NearWell(p, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, std::max(x1 - x0, y1 - y0) * 0.5f + 12.0f);
	}
	// Liegt (x, y) (relativ) naeher als m an einem Viertel-Teil? buildingsOnly: nur Bauwerke und Plaetze (Bruecken)
	static bool ElementHit(const Plan& p, float x, float y, float m, bool buildingsOnly = false) {
		for (int i : p.dpieces) { const Piece& q = p.pieces[i]; float qx = (float)(q.x - p.cx), qy = (float)(q.y - p.cy); if (RectGap(qx, qy, qx + q.w, qy + q.d, x, y) < m) return true; }
		for (const EDist& d : p.dists) if (d.plaza > 0.0f) { float dx = x - d.x, dy = y - d.y; if (dx * dx + dy * dy < (d.plaza + m) * (d.plaza + m)) return true; }
		if (buildingsOnly) return false;
		for (const PenR& g : p.pens) if (RectGap((float)g.x0, (float)g.y0, g.x1 + 1.0f, g.y1 + 1.0f, x, y) < m) return true;
		for (const FieldR& f : p.fields) if (RectGap((float)f.x0, (float)f.y0, f.x1 + 1.0f, f.y1 + 1.0f, x, y) < m) return true;
		for (const GardenR& g : p.gardens) if (RectGap((float)g.x0, (float)g.y0, g.x1 + 1.0f, g.y1 + 1.0f, x, y) < m) return true;
		return false;
	}
	// Gehege (relativ, inklusive) mit Zaun; Tor (2 frei) auf der Seite zu (gx, gy). Liefert den Punkt vor dem Tor.
	static void AddPen(Plan& p, int x0, int y0, int x1, int y1, float gx, float gy, float* ox, float* oy) {
		p.pens.push_back({ x0, y0, x1, y1 });
		int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
		bool gateY = std::fabs(gy) >= std::fabs(gx);
		int gateEdge = gateY ? (gy > 0 ? y1 : y0) : (gx > 0 ? x1 : x0);
		for (int x = x0 + 1; x < x1; x += 2) for (int y : { y0, y1 }) {
			if (gateY && y == gateEdge && std::abs(x - mx) <= 1) continue;
			p.penFences.push_back({ (float)x + 0.5f, (float)y + 0.5f, 0, 0 });
		}
		for (int y = y0 + 1; y < y1; y += 2) for (int x : { x0, x1 }) {
			if (!gateY && x == gateEdge && std::abs(y - my) <= 1) continue;
			p.penFences.push_back({ (float)x + 0.5f, (float)y + 0.5f, 0, 1 });
		}
		if (gateY) { *ox = mx + 0.5f; *oy = gateEdge + (gy > 0 ? 2.5f : -1.5f); }
		else { *ox = gateEdge + (gx > 0 ? 2.5f : -1.5f); *oy = my + 0.5f; }
	}
	// Bauwerk im Gebiet setzen: Entfernung f0..f1 (Anteil am Rand), Vorderseite zur Mitte, ganz in einer Zone, Abstand gap.
	static bool PlaceBuilding(Plan& p, const EDist& d, int id, int culture, int zoff, float f0, float f1, int salt, float gap, Piece* out) {
		const uint32_t ds = p.seed ^ (uint32_t)Rnd(d.x * 31.0f + d.y);
		for (int a = 0; a < 320; a++) {
			float th = U(Hash(a, salt, 311, ds)) * kTau, f = f0 + (f1 - f0) * U(Hash(a, salt, 312, ds));
			float e = DistEdge(d, th) * f, x = d.x + std::cos(th) * e, y = d.y + std::sin(th) * e;
			Piece pc = MakePiece(id, p.cx + Rnd(x), p.cy + Rnd(y), RotFacing(d.x - x, d.y - y), culture, zoff);
			if (!FitsZone(pc)) continue;
			float x0 = (float)(pc.x - p.cx), y0 = (float)(pc.y - p.cy), x1 = x0 + pc.w, y1 = y0 + pc.d;
			if (!RectInBlob(d, x0, y0, x1, y1, 2.0f) || !ElemFree(p, x0, y0, x1, y1, gap) || !PathOK(p, x0, y0, x1, y1)) continue;
			p.dpieces.push_back((int)p.pieces.size()); p.pieces.push_back(pc); *out = pc;
			return true;
		}
		return false;
	}
	// Rechteck W x D (zufaellig gedreht) im Gebiet; Ergebnis relativ, inklusive
	static bool FindRect(const Plan& p, const EDist& d, int W, int D, float f0, float f1, int salt, float gap, int* rx0, int* ry0, int* rx1, int* ry1) {
		const uint32_t ds = p.seed ^ (uint32_t)Rnd(d.x * 17.0f + d.y);
		for (int a = 0; a < 260; a++) {
			float th = U(Hash(a, salt, 321, ds)) * kTau, f = f0 + (f1 - f0) * U(Hash(a, salt, 322, ds));
			bool t = (Hash(a, salt, 323, ds) & 1) != 0;
			int w = t ? D : W, dd = t ? W : D;
			if (a >= 160) { w = w * 4 / 5; dd = dd * 4 / 5; }                     // passt es nicht, etwas kleiner
			float e = DistEdge(d, th) * f;
			int x0 = Rnd(d.x + std::cos(th) * e) - w / 2, y0 = Rnd(d.y + std::sin(th) * e) - dd / 2, x1 = x0 + w - 1, y1 = y0 + dd - 1;
			if (!RectInBlob(d, (float)x0, (float)y0, x1 + 1.0f, y1 + 1.0f, 2.0f)) continue;
			if (!ElemFree(p, (float)x0, (float)y0, x1 + 1.0f, y1 + 1.0f, gap) || !PathOK(p, (float)x0, (float)y0, x1 + 1.0f, y1 + 1.0f)) continue;
			*rx0 = x0; *ry0 = y0; *rx1 = x1; *ry1 = y1;
			return true;
		}
		return false;
	}
	static void AddDPath(Plan& p, float x0, float y0, float x1, float y1, int salt, bool lamp) {
		float L = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
		if (L < 3.0f) return;
		float nx = -(y1 - y0) / L, ny = (x1 - x0) / L, bend = (U(Hash(Rnd(x1), Rnd(y1), 340 + salt, p.seed)) - 0.5f) * 0.3f * L;
		p.dpaths.push_back({ x0, y0, (x0 + x1) * 0.5f + nx * bend, (y0 + y1) * 0.5f + ny * bend, x1, y1, lamp && L > 18.0f });
	}
	static int PickCrop(uint32_t h) { int sum = 0; for (int w : kCropWeights) sum += w; int r = (int)(h % (uint32_t)sum); for (int i = 0; i < 6; i++) { if (r < kCropWeights[i]) return i; r -= kCropWeights[i]; } return 0; }
	// Viertel fuellen. dwarf: kleinere Masse (Ringband), Abstand 6 statt 12.
	static void FillDistrict(Plan& p, const EDist& d, bool dwarf) {
		const int st = dwarf ? 1 : 0;
		const float gap = dwarf ? 6.0f : 12.0f;
		const int ax = p.cx + Rnd(d.x), ay = p.cy + Rnd(d.y);
		const int salt = (int)(Hash(Rnd(d.x), Rnd(d.y), 330, p.seed) & 0xFFFF);
		auto Obj = [&](int type, float wx, float wy, int orient) { float sx, sy, sz; ObjSize(type, &sx, &sy, &sz); p.objs.push_back({ type, wx - p.cx, wy - p.cy, 1, orient, sx, sy, sz }); };
		std::vector<int> races;
		if (g_animalProvider) g_animalProvider(p.biome, races);
		if (races.empty()) races = { 33, 34, 100, 56 };                                         // Schwein, Schaf, Kuh, Huhn
		int raceI = salt;
		std::vector<std::pair<float, float>> targets;                                          // Wegziele (relativ)
		auto Door = [&](const Piece& pc) {
			const MInfo& m = g_m[pc.id]; int wx, wy;
			ModelToWorld(pc, m.sx, m.sy, m.sx / 2, -3, &wx, &wy);
			targets.push_back({ (float)(wx - p.cx), (float)(wy - p.cy) });
		};
		auto Clutter = [&](const Piece& pc, int n, int k) {                                    // Faesser/Kisten/Saecke seitlich vor dem Bauwerk
			static const int kinds[3] = { O_BARREL, O_CRATE, O_SACK };
			const MInfo& m = g_m[pc.id];
			for (int i = 0; i < n; i++) {
				uint32_t h = Hash(k, i, 370, p.seed ^ (uint32_t)pc.x);
				int mx = (i & 1) ? m.sx + 1 : -2, my = 1 + 2 * (i / 2);
				int wx, wy; ModelToWorld(pc, m.sx, m.sy, mx, my, &wx, &wy);
				Obj(kinds[h % 3], wx + 0.5f, wy + 0.5f, (int)((h >> 4) & 1));
			}
		};
		auto Pen = [&](int wMin, int wMax, int dMin, int dMax, float f0, float f1, int k) {
			uint32_t h = Hash(k, salt, 350, p.seed);
			int W = wMin + (int)(h % (uint32_t)(wMax - wMin + 1)), D = dMin + (int)((h >> 8) % (uint32_t)(dMax - dMin + 1));
			int x0, y0, x1, y1;
			if (!FindRect(p, d, W, D, f0, f1, salt + 100 + k, gap, &x0, &y0, &x1, &y1)) return false;
			float cx0 = (x0 + x1) * 0.5f, cy0 = (y0 + y1) * 0.5f, gx, gy;
			AddPen(p, x0, y0, x1, y1, d.x - cx0, d.y - cy0, &gx, &gy);
			p.npcs.push_back({ NP_ANIMAL, Rnd(cx0), Rnd(cy0), 1, races[(size_t)(raceI++) % races.size()], 3 });   // 3 Tiere je Gehege
			targets.push_back({ gx, gy });
			Obj(O_SACK, p.cx + gx + (std::fabs(gy - cy0) > std::fabs(gx - cx0) ? 3.0f : 0.0f), p.cy + gy + (std::fabs(gy - cy0) > std::fabs(gx - cx0) ? 0.0f : 3.0f), 0);
			return true;
		};
		auto Field = [&](int wMin, int wMax, int dMin, int dMax, float f0, float f1, int k, int crop) {
			uint32_t h = Hash(k, salt, 360, p.seed);
			int W = wMin + (int)(h % (uint32_t)(wMax - wMin + 1)), D = dMin + (int)((h >> 8) % (uint32_t)(dMax - dMin + 1));
			int x0, y0, x1, y1;
			if (!FindRect(p, d, W, D, f0, f1, salt + 200 + k, gap, &x0, &y0, &x1, &y1)) return false;
			p.fields.push_back({ x0, y0, x1, y1, crop, ((h >> 16) & 1) != 0 });
			float cx0 = (x0 + x1) * 0.5f, cy0 = (y0 + y1) * 0.5f, gx = d.x - cx0, gy = d.y - cy0;
			if (std::fabs(gx) * (y1 - y0 + 1) > std::fabs(gy) * (x1 - x0 + 1)) targets.push_back({ gx > 0 ? x1 + 2.5f : x0 - 1.5f, cy0 });
			else targets.push_back({ cx0, gy > 0 ? y1 + 2.5f : y0 - 1.5f });
			if ((x1 - x0 + 1) * (y1 - y0 + 1) >= 300) p.npcs.push_back({ NP_SCARECROW, Rnd(cx0), Rnd(cy0), 1, 0x8C, 1 });   // native Vogelscheuche
			return true;
		};
		auto Garden = [&](int wMin, int wMax, int dMin, int dMax, float f0, float f1, int k, int style) {
			uint32_t h = Hash(k, salt, 365, p.seed);
			int W = wMin + (int)(h % (uint32_t)(wMax - wMin + 1)), D = dMin + (int)((h >> 8) % (uint32_t)(dMax - dMin + 1));
			int x0, y0, x1, y1;
			if (!FindRect(p, d, W, D, f0, f1, salt + 300 + k, gap * 0.75f, &x0, &y0, &x1, &y1)) return false;
			p.gardens.push_back({ x0, y0, x1, y1, style });
			float cx0 = (x0 + x1) * 0.5f, cy0 = (y0 + y1) * 0.5f, gx = d.x - cx0, gy = d.y - cy0;
			if (std::fabs(gx) * (y1 - y0 + 1) > std::fabs(gy) * (x1 - x0 + 1)) targets.push_back({ gx > 0 ? x1 + 2.5f : x0 - 1.5f, cy0 });
			else targets.push_back({ cx0, gy > 0 ? y1 + 2.5f : y0 - 1.5f });
			return true;
		};
		const int crop0 = PickCrop(Hash(salt, 1, 361, p.seed));
		Piece pc;
		switch (d.kind) {
			case D_WORK: {                                                                     // Schmied, Zimmerer, Weber (native Schmiede 0x903)
				static const int cult[3] = { 0x0D, 0x0E, 0x0F };
				for (int k = 0; k < 3; k++) if (PlaceBuilding(p, d, WorkModel(cult[k]), 0, kWorkZoff, 0.25f, 0.8f, salt + k, gap, &pc)) { AddStations(p, pc); Door(pc); Clutter(pc, 3, k); }
				Obj(O_CAMPFIRE, (float)ax, (float)ay, 0);                                          // Lagerfeuer (Kochen)
				Pen(dwarf ? 12 : 18, dwarf ? 16 : 24, dwarf ? 9 : 13, dwarf ? 12 : 17, 0.45f, 0.95f, 0);
				Field(dwarf ? 14 : 24, dwarf ? 20 : 34, dwarf ? 10 : 16, dwarf ? 14 : 24, 0.45f, 0.95f, 0, crop0);
				break;
			}
			case D_TRADE: {
				const CityModels::HutInfo hi = CityModels::MarketHutInfo();
				static const int cls[5] = { 0x80, 0x81, 0x82, 0x83, 0x84 };
				const float a0 = U(Hash(salt, 2, 362, p.seed)) * kTau;
				for (int k = 0; k < 3; k++) {                                                    // Marktstaende 0x15..0x17 am Platzrand
					float a = a0 + k * kTau / 3.0f, rr = d.plaza - 5.0f;
					p.objs.push_back({ 0x15 + k, std::cos(a) * rr + d.x, std::sin(a) * rr + d.y, 1, RotFacing(-std::cos(a), -std::sin(a)) & 1, 3.5f, 2.0f, 3.0f });
					float b = a + kTau / 6.0f, rb2 = d.plaza - 2.5f;                               // Baenke dazwischen
					Obj(O_BENCH, p.cx + d.x + std::cos(b) * rb2, p.cy + d.y + std::sin(b) * rb2, RotFacing(std::cos(b), std::sin(b)) & 1);
				}
				p.npcs.push_back({ NP_MERCHANT, ax - p.cx, ay - p.cy, 1, 0, 1 });                // unser Haendler auf dem Platz
				for (int k = 0; k < 5; k++) {
					if (!PlaceBuilding(p, d, (dwarf ? M_HUT_D : M_HUT_E) + (k & 3), 1, 0, 0.35f, 0.9f, salt + k, gap * 0.75f, &pc)) continue;
					int vx, vy; ModelToWorld(pc, hi.w, hi.d, hi.vx, hi.vy, &vx, &vy);
					p.npcs.push_back({ NP_VENDOR, vx - p.cx, vy - p.cy, 1, cls[k], 1 });
					Door(pc); Clutter(pc, 2, k);
				}
				for (int k = 0; k < 2; k++) Garden(dwarf ? 10 : 16, dwarf ? 14 : 24, dwarf ? 8 : 10, dwarf ? 10 : 15, 0.55f, 0.95f, k, 0);
				break;
			}
			case D_ALCH: {
				const CityModels::AlchemyInfo ai = CityModels::AlchemyHouseInfo();
				if (PlaceBuilding(p, d, dwarf ? M_ALCH_D : M_ALCH_E, 1, 0, 0.1f, 0.45f, salt, gap, &pc)) {
					int bx, by, nx, ny;
					ModelToWorld(pc, ai.w, ai.d, ai.brewX, ai.brewY, &bx, &by);
					ModelToWorld(pc, ai.w, ai.d, ai.npcX, ai.npcY, &nx, &ny);
					p.objs.push_back({ O_CAMPFIRE, bx + 0.5f - p.cx, by + 0.5f - p.cy, 1, 0, 2.4f, 2.4f, 0.5f });   // Braustand = Lagerfeuer-Station (9)
					p.npcs.push_back({ NP_ALCH, nx - p.cx, ny - p.cy, 1, 0x80, 1 });
					Door(pc); Clutter(pc, 2, 7);
				}
				for (int k = 0; k < 4; k++) Garden(dwarf ? 10 : 16, dwarf ? 14 : 24, dwarf ? 8 : 12, dwarf ? 10 : 17, 0.4f, 0.95f, k, k == 0 ? 0 : (k == 1 ? 2 : 1));
				Field(dwarf ? 12 : 18, dwarf ? 16 : 26, dwarf ? 9 : 12, dwarf ? 12 : 18, 0.5f, 0.95f, 0, (salt & 1) ? 3 : 4);   // Sonnenblumen oder Bohnen
				break;
			}
			case D_TEMPLE: {
				const int tid = dwarf ? M_TEMPLE_D : M_TEMPLE_E;
				Piece tp = MakePiece(tid, ax, ay, 0, 1, 0);                                    // Mitte (Lage bei der Planung geprueft)
				p.dpieces.push_back((int)p.pieces.size()); p.pieces.push_back(tp);
				int oz = g_orbZ[st] - g_m[M_ORB].sz / 2;
				p.pieces.push_back(MakePiece(M_ORB, ax, ay, 0, 0, oz));
				p.npcs.push_back({ NP_ORB, ax - p.cx, ay - p.cy, g_orbZ[st], 0, 0 });
				// natives Ziel fuer [R] (2026-10-14): Runenstein (Typ 0x2D -> "[R] Teleport") am Boden unter der Kugel.
				// Die Zielsuche (GameController.cpp:40297ff.) nimmt nur Objekte im Ortsindex nahe am Spieler; der Stein
				// hat keinen Partner (+0x178 = -1) -> kein nativer Sprung, die Kugel-Aktion macht PostsTick.
				p.objs.push_back({ 0x2D, ax + 0.5f - p.cx, ay + 0.5f - p.cy, 1, 0, 1.4f, 1.0f, 1.5f });
				// drei Statuen vor dem Tempel (Richtung Stadtmitte), Vorderseite zum Tempel; ganz in einer Zone
				const float toC = std::atan2(-d.y, -d.x), sr = tp.w * 0.5f + 12.0f;
				for (int k = 0; k < 3; k++) {
					const int sid = (dwarf ? M_STATUE_D : M_STATUE_E) + k;
					for (int t = 0; t < 80; t++) {
						float a = toC + (k - 1) * 0.62f + ((t & 1) ? 1.0f : -1.0f) * (float)(((t % 16) + 1) / 2) * 0.05f, rr = sr + (float)(t / 16) * 3.0f;
						float sx = d.x + std::cos(a) * rr, sy = d.y + std::sin(a) * rr;
						Piece s = MakePiece(sid, p.cx + Rnd(sx), p.cy + Rnd(sy), RotFacing(-std::cos(a), -std::sin(a)), 0, 0);
						if (!FitsZone(s)) continue;
						bool clash = false; for (const Piece& q : p.pieces) if (Overlap(s, q, 2)) clash = true;
						if (clash) continue;
						p.dpieces.push_back((int)p.pieces.size()); p.pieces.push_back(s);
						break;
					}
				}
				for (int k = 0; k < 4; k++) Garden(dwarf ? 10 : 16, dwarf ? 14 : 22, dwarf ? 8 : 10, dwarf ? 10 : 14, 0.6f, 0.95f, k, (k & 1) ? 2 : 0);
				break;
			}
			case D_ANIMAL: {
				AddWell(p, d.x, d.y, std::vector<Piece>());                                       // Traenke in der Mitte (wenn er in eine Zone passt)
				int n = 4 + (int)(Hash(salt, 3, 364, p.seed) % 3u);
				for (int k = 0; k < n; k++) Pen(dwarf ? 13 : 20, dwarf ? 18 : 30, dwarf ? 10 : 16, dwarf ? 13 : 22, 0.2f, 0.95f, k);
				for (int k = 0; k < 2; k++) Field(dwarf ? 14 : 22, dwarf ? 20 : 34, dwarf ? 10 : 16, dwarf ? 14 : 24, 0.4f, 0.95f, k, PickCrop(Hash(salt, k, 366, p.seed)));
				break;
			}
			case D_FARM: {
				if (Hash(salt, 4, 365, p.seed) & 1) AddWell(p, d.x, d.y, std::vector<Piece>());
				int nf = 2 + (int)(Hash(salt, 5, 365, p.seed) % 3u), np = 1 + (int)(Hash(salt, 6, 365, p.seed) % 2u);
				for (int k = 0; k < nf; k++) Field(dwarf ? 16 : 28, dwarf ? 24 : 46, dwarf ? 12 : 20, dwarf ? 16 : 32, 0.15f, 0.95f, k, PickCrop(Hash(salt, k, 367, p.seed)));
				for (int k = 0; k < np; k++) Pen(dwarf ? 13 : 18, dwarf ? 17 : 26, dwarf ? 10 : 14, dwarf ? 12 : 18, 0.2f, 0.95f, k);
				if (Hash(salt, 7, 365, p.seed) % 3u == 0) Garden(dwarf ? 10 : 16, dwarf ? 14 : 22, 8, dwarf ? 10 : 14, 0.3f, 0.9f, 0, 2);
				break;
			}
		}
		for (size_t i = 0; i < targets.size(); i++) AddDPath(p, d.x, d.y, targets[i].first, targets[i].second, (int)i, true);
	}
	// Ladenstrasse (2026-10-14): vom Ladeneingang (Haendlerposten) zur naechsten Allee/zum Ring bzw. zum Platz,
	// 5 Bloecke breit im Alleenbelag, Laternenpfosten im Wechsel alle 14 Bloecke -- die Haendler sind so vom
	// Wegenetz aus zu finden.
	static void AddShopRoad(Plan& p, float vx, float vy, int salt) {
		float ox, oy, dist; NearestMainPathPoint(p, vx, vy, &ox, &oy, &dist);
		float r = std::sqrt(vx * vx + vy * vy);
		if (r - 66.0f < dist) { ox = vx / r * 66.0f; oy = vy / r * 66.0f; dist = r - 66.0f; }   // Platz ist naeher
		if (dist < 3.0f) return;
		float nx = -(oy - vy) / dist, ny = (ox - vx) / dist, bend = (U(Hash(Rnd(vx), Rnd(vy), 380 + salt, p.seed)) - 0.5f) * 0.25f * dist;
		EPath t{ vx, vy, (vx + ox) * 0.5f + nx * bend, (vy + oy) * 0.5f + ny * bend, ox, oy, true };
		p.roads.push_back(t);
		int n = std::max(1, (int)(dist / 14.0f));
		for (int k = 0; k < n; k++) {
			float u = (k + 0.5f) / n, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
			float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
			float tx = 2 * (1 - u) * (t.cxp - t.x0) + 2 * u * (t.x1 - t.cxp), ty = 2 * (1 - u) * (t.cyp - t.y0) + 2 * u * (t.y1 - t.cyp), tl = std::sqrt(tx * tx + ty * ty) + 0.001f;
			float side = (k & 1) ? 1.0f : -1.0f, lx = px - ty / tl * side * 4.0f, ly = py + tx / tl * side * 4.0f;
			if (u * dist < 12.0f || ElementHit(p, lx, ly, 1.5f)) continue;      // nicht in Stamm/Laden
			p.lampPosts.push_back({ lx, ly, px - lx, py - ly });
		}
	}
	// Laternenpfosten an den Viertelwegen -- nach allen Vierteln (Wege stehen dann fest)
	static void PathLights(Plan& p) {
		for (size_t i = 0; i < p.dpaths.size(); i++) {
			const EPath& t = p.dpaths[i];
			if (!t.lamp) continue;
			float L = std::sqrt((t.x1 - t.x0) * (t.x1 - t.x0) + (t.y1 - t.y0) * (t.y1 - t.y0));
			int n = std::max(1, (int)(L / 26.0f));
			for (int k = 0; k < n; k++) {
				float u = (k + 0.6f) / (n + 0.2f), a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				float tx = 2 * (1 - u) * (t.cxp - t.x0) + 2 * u * (t.x1 - t.cxp), ty = 2 * (1 - u) * (t.cyp - t.y0) + 2 * u * (t.y1 - t.cyp), tl = std::sqrt(tx * tx + ty * ty) + 0.001f;
				float side = ((i + k) & 1) ? 1.0f : -1.0f, lx = px - ty / tl * side * 3.0f, ly = py + tx / tl * side * 3.0f;
				if (!ElemFree(p, lx - 0.5f, ly - 0.5f, lx + 0.5f, ly + 0.5f, 1.5f)) continue;
				// Laternenpfosten wie an den Alleen (Bloecke, Arm zur Wegmitte) statt der nativen Bodenleuchte 0x33
				p.lampPosts.push_back({ lx, ly, px - lx, py - ly });
			}
		}
	}
	// Pflanzen des Bioms fuer die Gaerten (einmal je Plan)
	static void BuildGardenSet(Plan& p) {
		if (!p.gardenM.empty() || !g_plantProvider) return;
		std::vector<Lab::Structure> src;
		g_plantProvider(p.biome, src);
		for (const Lab::Structure& s : src) { MInfo m; MakeInfo(m, s); if (m.p) p.gardenM.push_back(std::move(m)); }
	}

	// Haendlerwege (2026-10-27): 10 Bloecke breit -- von jedem Viertel mit Haendlern (Handel, Alchemie, Arbeit, Tempel)
	// zum Platz am Mutterbaum und von Haendler zu Haendler (nach Winkel um die Stadtmitte verkettet). Grosse Baeume
	// halten danach Abstand (RoadDist).
	static float RoadDist(const Plan& p, float x, float y) {
		float best = 1e9f;
		for (const EPath& t : p.roads) {
			for (int k = 0; k <= 16; k++) {
				float u = k / 16.0f, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				best = std::min(best, std::sqrt((px - x) * (px - x) + (py - y) * (py - y)));
			}
		}
		return best;
	}
	static void TraderRoads(Plan& p) {
		auto Road = [&](float x0, float y0, float x1, float y1) { p.roads.push_back({ x0, y0, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, x1, y1, true }); };
		for (const EDist& d : p.dists) {
			if (d.kind != D_TRADE && d.kind != D_ALCH && d.kind != D_WORK && d.kind != D_TEMPLE) continue;
			float r = std::sqrt(d.x * d.x + d.y * d.y); if (r < 80.0f) continue;
			Road(d.x, d.y, d.x / r * 66.0f, d.y / r * 66.0f);
		}
		std::vector<std::pair<float, std::pair<float, float>>> v;
		for (const NpcP& n : p.npcs) if (n.kind == NP_VENDOR || n.kind == NP_MERCHANT || n.kind == NP_ALCH) v.push_back({ std::atan2((float)n.y, (float)n.x), { (float)n.x, (float)n.y } });
		std::sort(v.begin(), v.end());
		for (size_t i = 0; i + 1 < v.size() + (v.size() > 2 ? 1 : 0); i++) {
			const auto& a = v[i].second; const auto& b = v[(i + 1) % v.size()].second;
			float dx = b.first - a.first, dy = b.second - a.second;
			if (dx * dx + dy * dy < 9.0f || dx * dx + dy * dy > 380.0f * 380.0f) continue;   // zu nah (gleicher Platz) / zu weit (ueber den Platz)
			Road(a.first, a.second, b.first, b.second);
		}
		std::cout << "[Stadt] Haendlerwege: " << p.roads.size() << " Strassen, 10 Bloecke breit." << std::endl;
	}
	static void BuildElf(Plan& p) {
		const int cx = p.cx, cy = p.cy; const float R = p.R;
		p.av0 = U(Hash(cx, cy, 11, p.seed)) * kTau;
		for (int k = 0; k < 6; k++) { p.avPh[k] = U(Hash(k, 1, 12, p.seed)) * kTau; p.avPh2[k] = U(Hash(k, 2, 12, p.seed)) * kTau; }
		for (float rr = 300.0f; rr <= R - 40.0f; rr += 240.0f) p.erings.push_back({ rr, U(Hash((int)rr, 3, 13, p.seed)) * kTau, U(Hash((int)rr, 4, 13, p.seed)) * kTau });
		p.pieces.push_back(MakePiece(M_MOTHER, cx, cy, 0, 1, 0));
		// Landeplatz neben der ersten Allee
		// (ganz in einer Zone: entlang der Allee nach innen/aussen versetzt, bis er passt)
		float rL0 = std::min(R * 0.55f, 380.0f), rL = rL0, aA = 0.0f, aL = 0.0f, rr = 0.0f;
		Piece land{};
		for (int t = 0; t < 25; t++) {
			rL = rL0 + ((t & 1) ? 1.0f : -1.0f) * (float)((t + 1) / 2) * 6.0f;
			aA = p.av0 + AvOffset(p, 0, rL) / rL;
			aL = aA + std::atan2(30.0f, rL); rr = std::sqrt(rL * rL + 900.0f);
			land = MakePiece(M_LANDING, cx + Rnd(std::cos(aL) * rr), cy + Rnd(std::sin(aL) * rr), 0, 1, -2);   // 2 Bloecke im Boden
			if (FitsZone(land)) break;
		}
		p.lAx = cx + std::cos(aA) * rL; p.lAy = cy + std::sin(aA) * rL;
		p.landing = (int)p.pieces.size(); p.pieces.push_back(land);
		const float lcx = land.x + land.w * 0.5f - cx, lcy = land.y + land.d * 0.5f - cy;
		// Brunnen auf kleinen Plaetzen an jeder Kreuzung Allee x Ringweg (passt er nicht in eine Zone, ein Stueck
		// entlang des Rings versetzt)
		for (const ERing& er : p.erings) for (int k = 0; k < 6; k++) {
			float a = p.av0 + k * kTau / 6.0f, r = er.r, th = a;
			for (int it = 0; it < 3; it++) { th = a + AvOffset(p, k, r) / r; r = RingR(er, th); }
			for (float off : { 0.0f, 14.0f, -14.0f, 28.0f, -28.0f }) {
				float t2 = th + off / r, r2 = RingR(er, t2);
				if (AddWell(p, std::cos(t2) * r2, std::sin(t2) * r2, std::vector<Piece>())) break;
			}
		}
		// Viertel (2026-10-13, Stand 3): grosse Gebiete MITTEN im Wald -- Handel, Alchemie, Tempel, Tiere, Arbeit in je
		// einem Feld zwischen den Alleen (das sechste bleibt Wohnen), weiter draussen Hoefe. Es wird nichts geraeumt:
		// Baumhaeuser, grosse Baeume und Bruecken weichen nur den Teilen der Viertel aus (ElementHit).
		{
			auto FarFromOthers = [&](const EDist& d, float gap) {
				for (const EDist& o : p.dists) { float dx = o.x - d.x, dy = o.y - d.y; if (dx * dx + dy * dy < (o.r + d.r + gap) * (o.r + d.r + gap)) return false; }
				return true;
			};
			static const int order[5] = { D_TRADE, D_ALCH, D_TEMPLE, D_ANIMAL, D_WORK };
			const float rdMain = std::max(240.0f, std::min(420.0f, R - 175.0f));
			const float raMain = std::min(122.0f, (rdMain - 95.0f) / 1.18f);
			if (raMain >= 70.0f) for (int k = 0; k < 5; k++) {
				const int kind = order[k];
				const float plaza = kind == D_TRADE ? 22.0f : (kind == D_TEMPLE ? 30.0f : 0.0f);
				for (int t = 0; t < 40; t++) {
					float ra = raMain * (0.92f + 0.12f * U(Hash(k, t, 335, p.seed))), rb = ra * (0.85f + 0.15f * U(Hash(k, t, 336, p.seed)));
					float a = p.av0 + (k + 0.5f) * kTau / 6.0f + (U(Hash(k, t, 332, p.seed)) - 0.5f) * 0.14f;
					float rd = rdMain + (U(Hash(k, t, 333, p.seed)) - 0.5f) * 40.0f;
					EDist d = MakeDist(kind, std::cos(a) * rd, std::sin(a) * rd, ra, rb, U(Hash(k, t, 334, p.seed)) * kPi, plaza, Hash(k, t, 337, p.seed));
					if (!FarFromOthers(d, 6.0f)) continue;
					if (plaza > 0.0f && MainPathDist(p, d.x, d.y) < plaza + 6.0f) continue;        // Platz nicht auf einer Allee
					if (kind == D_TEMPLE && !FitsZone(MakePiece(M_TEMPLE_E, cx + Rnd(d.x), cy + Rnd(d.y), 0, 1, 0))) continue;   // Tempel ganz in einer Zone
					p.dists.push_back(d);
					break;
				}
			}
			// Hoefe weiter draussen
			if (R > rdMain + 260.0f) {
				const int want = std::min(16, (int)(R * R / 70000.0f));
				for (int t = 0, got = 0; t < want * 16 && got < want; t++) {
					float a = U(Hash(t, 7, 337, p.seed ^ (uint32_t)cy)) * kTau, rd = rdMain + 190.0f + U(Hash(t, 8, 337, p.seed)) * (R - rdMain - 300.0f);
					float ra = 70.0f + U(Hash(t, 9, 337, p.seed)) * 26.0f, rb = ra * (0.75f + 0.25f * U(Hash(t, 10, 337, p.seed)));
					EDist d = MakeDist(D_FARM, std::cos(a) * rd, std::sin(a) * rd, ra, rb, U(Hash(t, 11, 337, p.seed)) * kPi, 0.0f, Hash(t, 12, 337, p.seed));
					if (rd + d.r > R - 20.0f || !FarFromOthers(d, 30.0f)) continue;
					p.dists.push_back(d); got++;
				}
			}
			for (const EDist& d : p.dists) FillDistrict(p, d, false);
			for (const EDist& d : p.dists) {                                      // Weg von der Viertelmitte zum naechsten Stadtweg
				float ox, oy, dist; NearestMainPathPoint(p, d.x, d.y, &ox, &oy, &dist);
				if (dist < 220.0f) AddDPath(p, d.x, d.y, ox, oy, 9, true);
			}
			PathLights(p);
		}
		// Baumhaeuser: 64er-Raster, Versatz je Spalte (x) und je Reihe (y), jedes 8. Feld bleibt Lichtung
		// Raster an den Zonen ausgerichtet (Zone = 256 x 256): Spalten/Reihen liegen in jeder Zone bei 32, 96,
		// 160, 224 (+- 8). Ein Baumhaus (halbe Breite <= 22) liegt damit IMMER ganz in einer Zone -- keine
		// Ueberschneidung einer Zonengrenze. Die Feldmitten liegen bei 0, 64, 128, 192: die bei 0 (= Zonengrenze)
		// bleiben frei, die anderen tragen die grossen Baeume (FitsZone prueft jedes Bauwerk).
		const int G = 64, n = (int)(R / G) + 2;
		const int xoff = (((32 - cx) % G) + G) % G, yoff = (((32 - cy) % G) + G) % G;
		std::vector<int> jx(2 * n + 4), jy(2 * n + 4);
		for (int k = -n - 1; k <= n + 2; k++) {
			jx[k + n + 1] = xoff + Rnd((U(Hash(k, 0, 22, p.seed ^ (uint32_t)cx)) - 0.5f) * 16.0f);
			jy[k + n + 1] = yoff + Rnd((U(Hash(0, k, 23, p.seed ^ (uint32_t)cy)) - 0.5f) * 16.0f);
		}
		auto JX = [&](int g) { return jx[g + n + 1]; };
		auto JY = [&](int g) { return jy[g + n + 1]; };
		std::map<int64_t, int> cell;                                          // (Spalte, Reihe) -> Baumhaus
		std::vector<std::pair<int, int>> gpos;
		for (int gy = -n; gy <= n; gy++) for (int gx = -n; gx <= n; gx++) {
			uint32_t h = Hash(gx, gy, 21, p.seed ^ (uint32_t)cx);
			if ((h >> 27) % 8u == 0) continue;                                 // Lichtung
			float x = (float)(gx * G + JX(gx)), y = (float)(gy * G + JY(gy));
			float r = std::sqrt(x * x + y * y);
			int v = (int)((h >> 20) & 3);
			CityModels::TreehouseDims dm = CityModels::TreehouseInfo(v);
			if (r < 140.0f || r > R - dm.half - 6.0f) continue;
			if (MainPathDist(p, x, y) < dm.half + 4.0f) continue;
			if (std::fabs(x - lcx) < 16 + dm.half + 6 && std::fabs(y - lcy) < 16 + dm.half + 6) continue;
			if (SegDist(x, y, p.lAx - cx, p.lAy - cy, lcx, lcy) < dm.half + 4.0f) continue;
			Piece hp = MakePiece(M_TH0 + v, cx + (int)x, cy + (int)y, 0, 1, 0);
			if (!FitsZone(hp)) continue;                                        // nie ueber eine Zonengrenze
			if (NearWell(p, x, y, (float)dm.half + 14.0f)) continue;              // Brunnenplatz frei lassen
			if (ElementHit(p, x, y, (float)dm.half + 8.0f)) continue;            // Viertel-Teile (Bauwerk, Feld, Gehege, Garten, Platz) frei lassen
			int cgx = (int)std::floor(x / 64.0f), cgy = (int)std::floor(y / 64.0f);
			cell[GKey(gx, gy)] = (int)p.trees.size();
			gpos.push_back({ gx, gy });
			p.treeGrid[GKey(cgx, cgy)].push_back((int)p.trees.size());
			p.trees.push_back({ x, y, v, dm.platZ, dm.Rp });
			p.pieces.push_back(hp);
		}
		// Ladenbaeume (2026-10-13): die Baumhaeuser nahe der Stadtmitte bekommen unten einen Laden; dort stehen die
		// fuenf Haendler (Rollen huette1..5, LAB -> Haendler) und der Markthaendler -- leicht zu finden, an den Wegen.
		{
			std::vector<std::pair<float, int>> cand;                              // (Abstand zur Mitte, Index in pieces)
			for (int i = 0; i < (int)p.pieces.size(); i++) {
				const Piece& q = p.pieces[i];
				if (q.id < M_TH0 || q.id >= M_TH0 + 4) continue;
				float qx = q.x + q.w * 0.5f - cx, qy = q.y + q.d * 0.5f - cy, r = std::sqrt(qx * qx + qy * qy);
				if (r >= 120.0f && r <= 420.0f) cand.push_back({ r, i });
			}
			std::sort(cand.begin(), cand.end());
			std::vector<float> used;
			int k = 0;
			for (const auto& c : cand) {
				if (k >= 6) break;
				Piece& q = p.pieces[c.second];
				float qx = q.x + q.w * 0.5f - cx, qy = q.y + q.d * 0.5f - cy, a = std::atan2(qy, qx);
				bool close = false; for (float u : used) if (AngDiff(a, u) < 0.6f) close = true;   // ueber die Stadt verteilt
				if (close) continue;
				used.push_back(a);
				int v = q.id - M_TH0;
				q.id = M_THS0 + v;
				CityModels::ShopInfo si = CityModels::TreehouseShopInfo(v);
				int vx = q.x + si.vx - cx, vy = q.y + si.vy - cy;
				if (k < 5) p.npcs.push_back({ NP_VENDOR, vx, vy, 1, 0x80 + k, 1 });
				else p.npcs.push_back({ NP_MERCHANT, vx, vy, 1, 0, 1 });
				AddShopRoad(p, (float)vx, (float)vy, k);
				k++;
			}
			if (k) std::cout << "[Stadt] " << k << " Ladenbaeume mit Haendlern (Abstand " << (int)cand.front().first << ".." << (int)cand[std::min((size_t)k, cand.size()) - 1].first << " zur Mitte)." << std::endl;
		}
		// Gerade Bruecken zum naechsten Haus in +x (Reihe) und +y (Spalte); fehlt das direkte Nachbarhaus
		// (Lichtung/Weg), darf die Bruecke ein Feld ueberspannen. Jede 5. Verbindung bleibt frei.
		for (int i = 0; i < (int)p.trees.size(); i++) {
			int gx = gpos[i].first, gy = gpos[i].second;
			for (int axis = 0; axis < 2; axis++) {
				if (Hash(gx, gy, 40 + axis, p.seed) % 5u == 0) continue;
				for (int step = 1; step <= 2; step++) {
					auto it = cell.find(axis == 0 ? GKey(gx + step, gy) : GKey(gx, gy + step));
					if (it == cell.end()) continue;
					bool overPlaza = false;                                       // Bruecke nicht ueber Viertel-Bauwerke / Plaetze
					{
						const ETree& A = p.trees[i]; const ETree& B = p.trees[it->second];
						float L = std::sqrt((B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y));
						for (float u = 0.0f; u <= L && !overPlaza; u += 3.0f) if (ElementHit(p, A.x + (B.x - A.x) * u / L, A.y + (B.y - A.y) * u / L, 4.0f, true)) overPlaza = true;
					}
					if (!overPlaza) MakeBridge(p, i, it->second, axis, lcx, lcy);
					break;                                                        // naechstes Haus gefunden (ob Bruecke oder nicht)
				}
			}
		}
		// Trampelpfade: zu bis zu 3 naechsten Baumhaeusern (<= 100) und zum naechsten Haupt-/Ringweg
		std::map<int64_t, bool> seen;
		for (int i = 0; i < (int)p.trees.size(); i++) {
			const ETree& A = p.trees[i];
			std::vector<std::pair<float, int>> nb;
			int cgx = (int)std::floor(A.x / 64.0f), cgy = (int)std::floor(A.y / 64.0f);
			for (int ax = cgx - 2; ax <= cgx + 2; ax++) for (int ay = cgy - 2; ay <= cgy + 2; ay++) {
				auto it = p.treeGrid.find(GKey(ax, ay));
				if (it == p.treeGrid.end()) continue;
				for (int q : it->second) if (q != i) { float ex = p.trees[q].x - A.x, ey = p.trees[q].y - A.y, d = std::sqrt(ex * ex + ey * ey); if (d <= 100.0f) nb.push_back({ d, q }); }
			}
			std::sort(nb.begin(), nb.end());
			for (size_t k = 0; k < nb.size() && k < 3; k++) {
				int j = nb[k].second; int64_t key = GKey(std::min(i, j), std::max(i, j));
				if (seen.count(key)) continue;
				seen[key] = true;
				const ETree& B = p.trees[j];
				uint32_t h = Hash(i, j, 31, p.seed);
				float L = nb[k].first, mx = (A.x + B.x) * 0.5f, my = (A.y + B.y) * 0.5f, nx = -(B.y - A.y) / L, ny = (B.x - A.x) / L, bend = (U(h) - 0.5f) * 0.6f * L;
				p.trails.push_back({ A.x, A.y, mx + nx * bend, my + ny * bend, B.x, B.y, L > 40.0f });
			}
			float ox, oy, d;
			NearestMainPathPoint(p, A.x, A.y, &ox, &oy, &d);
			if (d <= 120.0f) {
				uint32_t h = Hash(i, 0, 32, p.seed);
				float mx = (A.x + ox) * 0.5f, my = (A.y + oy) * 0.5f, L = std::max(d, 1.0f), nx = -(oy - A.y) / L, ny = (ox - A.x) / L, bend = (U(h) - 0.5f) * 0.5f * L;
				p.trails.push_back({ A.x, A.y, mx + nx * bend, my + ny * bend, ox, oy, false });
			}
		}
		TraderRoads(p);
		// Grosse Baeume in der Mitte jedes Rasterfelds zwischen vier Haeusern: alle Baeume des Bioms
		// (BuildTreeSet), vergroessert. Je Feld ein Baum (Wahl per Zufall), passt er nicht, der naechst-
		// kleinere derselben Art, dann die naechste Art -- Freiraum Schicht fuer Schicht (BigClear).
		BuildTreeSet(p);
		for (int gy = -n; gy < n; gy++) for (int gx = -n; gx < n; gx++) {
			if (p.treeGroups.empty()) break;
			float x = gx * (float)G + G * 0.5f + (JX(gx) + JX(gx + 1)) * 0.5f, y = gy * (float)G + G * 0.5f + (JY(gy) + JY(gy + 1)) * 0.5f;
			x = (float)Rnd(x); y = (float)Rnd(y);
			uint32_t h = Hash(gx, gy, 24, p.seed);
			if (h % 7u == 0) continue;                                          // nicht in jedes Feld
			float r = std::sqrt(x * x + y * y);
			bool done = false;
			const int ng = (int)p.treeGroups.size(), g0 = (int)((h >> 12) % (uint32_t)ng);
			for (int k = 0; k < ng && !done; k++) for (int id : p.treeGroups[(g0 + k) % ng]) {
				const MInfo& m = p.treeM[id];
				float half = std::max(m.sx, m.sy) * 0.5f, root = std::max(2.0f, ProfM(m, 0));
				if (r < 70.0f + half || r > R - half - 4.0f) continue;
				if (std::fabs(x - lcx) < 16 + half + 4 && std::fabs(y - lcy) < 16 + half + 4) continue;
				if (SegDist(x, y, p.lAx - cx, p.lAy - cy, lcx, lcy) < half + 4.0f) continue;
				if (MainPathDist(p, x, y) < root + 4.0f) continue;                // Stamm/Wurzeln nicht auf Wegen
				if (RoadDist(p, x, y) < half + 6.0f) continue;                   // nicht auf die Haendlerwege
				if (TrailDist(p, x, y, root + 4.0f) < root + 3.0f) continue;
				if (!BigClear(p, m, x, y)) continue;
				if (NearWell(p, x, y, half + 12.0f)) continue;
				if (ElementHit(p, x, y, half + 4.0f)) continue;
				int rot = (int)((h >> 8) & 3), w = (rot & 1) ? m.sy : m.sx, d = (rot & 1) ? m.sx : m.sy;
				if (!FitsZone(Piece{ 0, cx + (int)x - w / 2, cy + (int)y - d / 2, 0, rot, 0, w, d })) continue;   // nie ueber eine Zonengrenze
				int bi = (int)p.bigs.size();
				p.bigs.push_back({ x, y, id, rot });
				p.bigGrid[GKey((int)std::floor(x / 64.0f), (int)std::floor(y / 64.0f))].push_back(bi);
				// Kultur 0: Baumfarben bleiben Bloecke (eine zufaellige Markerfarbe in einem Biom-Baum wuerde sonst ein Objekt)
				p.pieces.push_back(Piece{ kTreeIdBase + id, cx + (int)x - w / 2, cy + (int)y - d / 2, 0, rot, 0, w, d });
				done = true; break;
			}
		}
	}

	static void BuildDwarf(Plan& p) {
		const int cx = p.cx, cy = p.cy; const float R = p.R;
		p.Rw = R - 10.0f;
		p.rings.push_back(110.0f); p.ringHalf.push_back(4.0f);
		for (float r = 185.0f; r < p.Rw - 45.0f; r += 75.0f) { p.rings.push_back(r); p.ringHalf.push_back(3.5f); }
		p.rings.push_back(p.Rw - 13.0f); p.ringHalf.push_back(3.5f);                  // Gasse an der Mauer
		// Mauertuerme (nicht naeher als 30 an einem Tor)
		int nT = std::max(8, Rnd(kTau * p.Rw / 120.0f));
		for (int i = 0; i < nT; i++) {
			float a = (i + 0.5f) * kTau / nT; bool nearGate = false;
			for (int g = 0; g < 4; g++) if (AngDiff(a, g * kTau / 4.0f) * p.Rw < 30.0f) nearGate = true;
			if (!nearGate) p.towerA.push_back(a);
		}
		std::vector<Piece> reserved;
		Piece fort = MakePiece(M_FORT, cx, cy, 0, 1, 0);
		p.pieces.push_back(fort); reserved.push_back(fort);
		// vier Brunnen auf dem Platz um die Festung (Platz bis r 106, Festung bis ~70)
		for (int k = 0; k < 4; k++) {
			for (float da : { 0.0f, 0.12f, -0.12f, 0.24f, -0.24f }) {
				float a = kPi / 4.0f + k * kPi / 2.0f + da;
				if (AddWell(p, std::cos(a) * 88.0f, std::sin(a) * 88.0f, reserved)) { reserved.push_back(p.pieces.back()); break; }
			}
		}
		// Schmieden im ersten Ring (Kultur 0x0D: Ofen, Amboss, Anpassungsbank), Boden z 6 = Bodenoberkante.
		// Ganz in einer Zone: entlang des Rings / quer dazu versetzt, bis sie passt (nicht an die Radialstrassen).
		auto InBand = [&](const Piece& pc, float inner, float outer) {
			float dmin, dmax; RectDist(pc, (float)cx, (float)cy, &dmin, &dmax);
			if (dmin < inner || dmax > outer) return false;
			for (int k = 0; k < 8; k++) if (RectNearRay(pc, (float)cx, (float)cy, k * kPi / 4.0f, 5.0f)) return false;
			for (const Piece& q : reserved) if (Overlap(pc, q, 4)) return false;
			return true;
		};
		if (p.rings.size() >= 3) {
			float inner = p.rings[0] + p.ringHalf[0] + 2.0f, outer = p.rings[1] - p.ringHalf[1] - 2.0f, rs = (inner + outer) * 0.5f;
			static const int cult[4] = { 0x0D, 0x0E, 0x0F, 0x0D };                         // Arbeitsviertel: Schmied, Zimmerer, Weber, Schmied
			for (int k = 0; k < 4; k++) {
				for (int t = 0; t < 33; t++) {
					float sh = ((t & 1) ? 1.0f : -1.0f) * (float)(((t % 11) + 1) / 2) * 6.0f, rr = rs + (float)(t / 11 == 1 ? 5 : (t / 11 == 2 ? -5 : 0));
					float a = kPi / 8.0f + k * kPi / 2.0f + sh / rs;
					Piece s = MakePiece(WorkModel(cult[k]), cx + Rnd(std::cos(a) * rr), cy + Rnd(std::sin(a) * rr), 0, 0, kWorkZoff);
					if (!FitsZone(s) || !InBand(s, inner, outer)) continue;
					p.pieces.push_back(s); reserved.push_back(s); AddStations(p, s);
					break;
				}
			}
		}
		// Landeplatz im aeussersten Ring, der breit genug ist (zwischen zwei Radialstrassen), ganz in einer Zone
		for (int i = (int)p.rings.size() - 2; i >= 0 && p.landing < 0; i--) {
			float ra = p.rings[i] + p.ringHalf[i] + 2.0f, rb = p.rings[i + 1] - p.ringHalf[i + 1] - 2.0f;
			if (rb - ra < 50.0f) continue;
			float rm = (ra + rb) * 0.5f;
			for (int t = 0; t < 15; t++) {
				float a = kPi / 8.0f + ((t & 1) ? 1.0f : -1.0f) * (float)((t + 1) / 2) * 6.0f / rm;
				Piece l = MakePiece(M_LANDING, cx + Rnd(std::cos(a) * rm), cy + Rnd(std::sin(a) * rm), 0, 1, -2);   // 2 Bloecke im Boden
				if (!FitsZone(l)) continue;
				bool clash = false; for (const Piece& q : reserved) if (Overlap(l, q, 4)) clash = true;
				if (clash) continue;
				p.landing = (int)p.pieces.size(); p.pieces.push_back(l); reserved.push_back(l);
				break;
			}
		}
		// Viertel (2026-10-13, organisch) im zweiten Ringband: Handel, Alchemie, Tempel, Tiere -- laenglich entlang des
		// Bands (Ellipse, gewellter Rand, auf das Band beschnitten). In den aeusseren Baendern Hoefe (Felder + Gehege)
		// zwischen den Haeusern. Die Werkstaetten im ersten Band sind das Arbeitsviertel.
		auto DistBox = [&](const EDist& d) {                                      // umschliessendes Rechteck (Weltlage)
			float c = std::cos(d.psi), s = std::sin(d.psi), A = d.ra * 1.18f, B = d.rb * 1.18f;
			float hx = std::sqrt(A * A * c * c + B * B * s * s), hy = std::sqrt(A * A * s * s + B * B * c * c);
			return Piece{ 0, cx + Rnd(d.x - hx), cy + Rnd(d.y - hy), 0, 0, 0, Rnd(2 * hx), Rnd(2 * hy) };
		};
		auto ClearOfRadials = [&](float a, float rm, float halfT) {
			for (int k = 0; k < 8; k++) if (AngDiff(a, k * kPi / 4.0f) * rm < halfT + 8.0f) return false;
			return true;
		};
		if (p.rings.size() >= 3) {
			float ra = p.rings[1] + p.ringHalf[1] + 2.0f, rb = p.rings[2] - p.ringHalf[2] - 2.0f;
			float rm = (ra + rb) * 0.5f, half = (rb - ra) * 0.5f;
			static const int order[4] = { D_TRADE, D_ALCH, D_TEMPLE, D_ANIMAL };
			if (half >= 24.0f) for (int k = 0; k < 4; k++) {
				const int kind = order[k];
				const float plaza = kind == D_TRADE ? 14.0f : (kind == D_TEMPLE ? 26.0f : 0.0f);
				for (int t = 0; t < 40; t++) {
					float aT = std::min(78.0f, rm * kPi / 4.0f - 14.0f) * (t < 20 ? 1.0f : 0.85f);
					float a = kPi / 8.0f + kPi / 4.0f + k * kPi / 2.0f + ((t & 1) ? 1.0f : -1.0f) * (float)(t % 20 / 2) * 4.0f / rm;
					float rr = rm + (kind == D_TEMPLE ? (float)((t % 5) - 2) * 2.0f : 0.0f);
					EDist d = MakeDist(kind, std::cos(a) * rr, std::sin(a) * rr, aT, half + 4.0f, a + kPi / 2.0f, plaza, Hash(k, t, 338, p.seed));
					d.lim0 = ra; d.lim1 = rb;
					if (!ClearOfRadials(a, rm, aT * 1.1f)) continue;
					Piece box = DistBox(d);
					bool clash = false; for (const Piece& q : reserved) if (Overlap(box, q, 2)) clash = true;
					if (clash) continue;
					if (kind == D_TEMPLE) {
						Piece tp = MakePiece(M_TEMPLE_D, cx + Rnd(d.x), cy + Rnd(d.y), 0, 1, 0);
						float dmin, dmax; RectDist(tp, (float)cx, (float)cy, &dmin, &dmax);
						if (!FitsZone(tp) || dmin < ra || dmax > rb) continue;
					}
					p.dists.push_back(d); reserved.push_back(box);
					break;
				}
			}
			// Hoefe in den aeusseren Baendern (ab dem dritten), je Band einige, nie an Radialstrassen oder Toren
			for (size_t i = 2; i + 1 < p.rings.size(); i++) {
				float fa = p.rings[i] + p.ringHalf[i] + 2.0f, fb = p.rings[i + 1] - p.ringHalf[i + 1] - 2.0f;
				if (fb - fa < 40.0f) continue;
				float fm = (fa + fb) * 0.5f, fh = (fb - fa) * 0.5f;
				int want = std::max(1, std::min(6, (int)(kTau * fm / 380.0f)));
				for (int t = 0, got = 0; t < want * 10 && got < want; t++) {
					float a = U(Hash((int)i, t, 339, p.seed)) * kTau, aT = 48.0f + U(Hash((int)i, t, 340, p.seed)) * 30.0f;
					EDist d = MakeDist(D_FARM, std::cos(a) * fm, std::sin(a) * fm, aT, fh + 4.0f, a + kPi / 2.0f, 0.0f, Hash((int)i, t, 341, p.seed));
					d.lim0 = fa; d.lim1 = fb;
					if (!ClearOfRadials(a, fm, aT * 1.1f)) continue;
					bool nearGate = false; for (int g = 0; g < 4; g++) if (fm > p.Rw - 70.0f && AngDiff(a, g * kTau / 4.0f) * fm < aT + 60.0f) nearGate = true;
					if (nearGate) continue;
					Piece box = DistBox(d);
					bool clash = false; for (const Piece& q : reserved) if (Overlap(box, q, 6)) clash = true;
					if (clash) continue;
					p.dists.push_back(d); reserved.push_back(box); got++;
				}
			}
			for (const EDist& d : p.dists) {
				FillDistrict(p, d, true);
				float th = std::atan2(d.y, d.x), r0 = std::sqrt(d.x * d.x + d.y * d.y);  // Wege zur inneren und aeusseren Ringstrasse
				AddDPath(p, d.x, d.y, std::cos(th) * (d.lim0 - 1.0f), std::sin(th) * (d.lim0 - 1.0f), 9, false);
				AddDPath(p, d.x, d.y, std::cos(th) * (d.lim1 + 1.0f), std::sin(th) * (d.lim1 + 1.0f), 10, false);
				(void)r0;
			}
			PathLights(p);
		}
		// Haeuser in zwei Reihen je Ring, Vorderseite zur Strasse. Ganz in einer Zone: passt ein Haus nicht, wird es
		// entlang der Reihe um bis zu 12 Bloecke versetzt (sonst entfaellt es).
		std::vector<Piece> houses;
		std::map<int64_t, std::vector<int>> grid;                       // 64er-Raster fuer die Ueberlappungspruefung
		static const float kShift[9] = { 0.0f, 3.0f, -3.0f, 6.0f, -6.0f, 9.0f, -9.0f, 12.0f, -12.0f };
		for (size_t i = 0; i + 1 < p.rings.size(); i++) {
			float inner = p.rings[i] + p.ringHalf[i] + 2.0f, outer = p.rings[i + 1] - p.ringHalf[i + 1] - 2.0f;
			if (outer - inner < 18.0f) continue;
			struct Row { float r; float facing; };
			std::vector<Row> rows; rows.push_back({ inner + 9.5f, -1.0f });
			if (outer - inner >= 40.0f) rows.push_back({ outer - 9.5f, 1.0f });
			for (size_t ri = 0; ri < rows.size(); ri++) {
				const Row& row = rows[ri];
				int n = (int)(kTau * row.r / 27.0f);
				for (int j = 0; j < n; j++) {
					uint32_t h = Hash((int)i, (int)ri * 100000 + j, 9, p.seed ^ (uint32_t)cy);
					float th0 = (j + 0.5f) * kTau / n + (U(h) - 0.5f) * 0.02f;
					bool nearRad = false;
					for (int k = 0; k < 8; k++) if (AngDiff(th0, k * kPi / 4.0f) * row.r < 18.0f) nearRad = true;
					if (nearRad) continue;
					float fx = std::cos(th0) * row.facing, fy = std::sin(th0) * row.facing;
					int rot = std::fabs(fx) > std::fabs(fy) ? (fx > 0 ? 3 : 1) : (fy > 0 ? 2 : 0);
					int id = M_DH0 + (int)((h >> 8) & 3);
					for (int si = 0; si < 9; si++) {
						float th = th0 + kShift[si] / row.r;
						Piece pc = MakePiece(id, cx + Rnd(std::cos(th) * row.r), cy + Rnd(std::sin(th) * row.r), rot, 1, 0);
						float dmin, dmax; RectDist(pc, (float)cx, (float)cy, &dmin, &dmax);
						// schraeg zur Strasse stehende Haeuser so weit verschieben, dass die Ecke an der Strasse anliegt
						float shift = row.facing < 0 ? std::max(0.0f, inner - dmin + 0.5f) : -std::max(0.0f, dmax - outer + 0.5f);
						if (shift != 0.0f) {
							pc = MakePiece(id, cx + Rnd(std::cos(th) * (row.r + shift)), cy + Rnd(std::sin(th) * (row.r + shift)), rot, 1, 0);
							RectDist(pc, (float)cx, (float)cy, &dmin, &dmax);
						}
						if (dmin < inner || dmax > outer || !FitsZone(pc)) continue;
						bool bad = false;
						for (int k = 0; k < 8 && !bad; k++) if (RectNearRay(pc, (float)cx, (float)cy, k * kPi / 4.0f, 5.0f)) bad = true;
						for (const Piece& q : reserved) if (!bad && Overlap(pc, q, 4)) bad = true;
						int gx0 = (pc.x - cx) >> 6, gy0 = (pc.y - cy) >> 6;
						for (int gx = gx0 - 1; gx <= gx0 + 1 && !bad; gx++) for (int gy = gy0 - 1; gy <= gy0 + 1 && !bad; gy++) {
							auto it = grid.find(((int64_t)gx << 32) ^ (uint32_t)gy);
							if (it != grid.end()) for (int q : it->second) if (Overlap(pc, houses[q], 2)) { bad = true; break; }
						}
						if (bad) continue;
						grid[((int64_t)gx0 << 32) ^ (uint32_t)gy0].push_back((int)houses.size());
						houses.push_back(pc);
						break;
					}
				}
			}
		}
		p.pieces.insert(p.pieces.end(), houses.begin(), houses.end());
	}

	// ---- Zonen-Bauauftraege ----------------------------------------------------------------------
	// Sobald ein Stadtplan steht, bekommt JEDE Zone, die er beruehrt, einen Bauauftrag: Stadtkreis + 30
	// Puffer, dazu die Huelle jedes Bauwerks und jeder Bruecke (+2). Eine Zone, die spaeter erzeugt wird,
	// baut ihren Teil dann ausdruecklich -- unabhaengig davon, welches Biom an ihrem Mittelpunkt liegt.
	// (Frueher entschied jede Zone selbst ueber das Biom an ihrer Mitte; am Stadtrand konnte eine
	// Nachbarzone so leer bleiben: Baeume und Haeuser endeten an der Zonengrenze, man sah ins Innere.)
	static std::map<int64_t, std::vector<Plan*>> g_orders;
	static std::map<const Plan*, Site> g_planSites;
	static volatile LONG c_orderZones = 0, c_noGround = 0;
	static void RegisterOrders(Plan* p) {                                       // unter Lock
		std::map<int64_t, bool> z;
		auto Box = [&](int x0, int y0, int x1, int y1) {                        // Weltkoordinaten, x1/y1 inklusive
			for (int zx = FloorDiv256(x0); zx <= FloorDiv256(x1); zx++) for (int zy = FloorDiv256(y0); zy <= FloorDiv256(y1); zy++) z[GKey(zx, zy)] = true;
		};
		const int r = (int)p->R + 30;
		for (int zx = FloorDiv256(p->cx - r); zx <= FloorDiv256(p->cx + r); zx++)
			for (int zy = FloorDiv256(p->cy - r); zy <= FloorDiv256(p->cy + r); zy++) {
				float nx = (float)std::max(zx * 256, std::min(p->cx, zx * 256 + 255)), ny = (float)std::max(zy * 256, std::min(p->cy, zy * 256 + 255));
				if ((nx - p->cx) * (nx - p->cx) + (ny - p->cy) * (ny - p->cy) <= (float)r * r) z[GKey(zx, zy)] = true;
			}
		for (const Piece& pc : p->pieces) Box(pc.x - 2, pc.y - 2, pc.x + pc.w + 1, pc.y + pc.d + 1);
		for (const EBridge& b : p->bridges) Box(p->cx + b.bx0 - 3, p->cy + b.by0 - 3, p->cx + b.bx1 + 3, p->cy + b.by1 + 3);
		for (const auto& e : z) {
			std::vector<Plan*>& v = g_orders[e.first];
			if (std::find(v.begin(), v.end(), p) == v.end()) { v.push_back(p); InterlockedIncrement(&c_orderZones); }
		}
	}
	static const Plan* GetPlan(const Site& s) {
		uint64_t k = PlanKey(s);
		Lock l;
		auto it = g_plans.find(k);
		if (it != g_plans.end()) return it->second;
		Plan* p = new Plan();
		p->kind = s.kind; p->cx = s.cx; p->cy = s.cy; p->R = s.radius; p->seed = s.seed; p->biome = s.biome;
		if (s.kind == ELF) BuildElf(*p); else if (s.kind == DWARF) BuildDwarf(*p);
		if (!p->gardens.empty()) BuildGardenSet(*p);
		g_plans[k] = p;
		g_planSites[p] = s;
		RegisterOrders(p);
		std::cout << "[Stadt] " << KindName(s.kind) << " bei (" << s.cx << "," << s.cy << "), Radius " << (int)s.radius
		          << ": " << p->pieces.size() << " Bauwerke geplant";
		if (s.kind == ELF) std::cout << " (" << p->trees.size() << " Baumhaeuser, " << p->bridges.size() << " gerade Bruecken, " << p->bigs.size() << " grosse Baeume aus " << p->treeGroups.size() << " Baumarten des Bioms)";
		std::cout << "." << std::endl;
		for (size_t i = 0; i < p->treeNames.size(); i++) std::cout << "[Stadt]   Baum " << i + 1 << ": " << p->treeNames[i] << " (" << p->treeM[i].sx << "x" << p->treeM[i].sy << "x" << p->treeM[i].sz << ")" << std::endl;
		return p;
	}

	void Register(void* gc, const Site& s) {
		if ((s.kind != ELF && s.kind != DWARF) || s.radius < 150.0f) return;
		EnsureModels(gc);
		GetPlan(s);
	}
	// Kartennamen der Viertel (Weltkarte, MapMarkers.cpp): je geplanter Stadt die Stadt selbst und ihre Viertel
	int MapLabels(std::vector<MapLabel>& out) {
		Lock l;
		for (auto& kv : g_plans) {
			const Plan* p = kv.second; if (!p) continue;
			const int z = p->zg == LONG_MIN ? INT_MIN : (int)p->zg + 1;
			out.push_back({ p->cx, p->cy, z, KindName(p->kind) });
			for (const EDist& d : p->dists) {
				if (d.kind == D_FARM) continue;
				std::string n = DistrictName(d.kind);
				if (d.kind == D_TRADE) n = "Haendlerviertel";
				out.push_back({ p->cx + Rnd(d.x), p->cy + Rnd(d.y), z, n });
			}
		}
		return (int)out.size();
	}
	int ZonePlans(int zx, int zy, Site* out, int maxN) {
		Lock l;
		auto it = g_orders.find(GKey(zx, zy));
		if (it == g_orders.end()) return 0;
		int n = 0;
		for (Plan* p : it->second) { if (n >= maxN) break; auto s = g_planSites.find(p); if (s != g_planSites.end()) out[n++] = s->second; }
		return n;
	}

	bool ZoneTouches(const Site& s, int zx, int zy) {
		float nx = (float)std::max(zx * 256, std::min(s.cx, zx * 256 + 255)), ny = (float)std::max(zy * 256, std::min(s.cy, zy * 256 + 255));
		float d = std::sqrt((nx - s.cx) * (nx - s.cx) + (ny - s.cy) * (ny - s.cy));
		return d <= s.radius + 14.0f;
	}

	// ---- Wegeschicht -------------------------------------------------------------------
	// Layer-Index z 0 = oberster Bodenblock (wird umgefaerbt), z >= 1 darueber.
	struct Layer {
		Lab::Structure L; int x0, y0; bool any = false; long lanterns = 0;
		// Bodenschicht der Elfen ist nur 21 hoch (darueber beginnt die Brueckenschicht). Was hoeher reicht
		// (Kronen der Waldbaeume bis z ~27), geht in die obere Schicht -- frueher fiel es weg: Kronen oben flach.
		Layer* up = nullptr; int upOff = 0;
		void Set(int wx, int wy, int z, C3 c) {
			if (up && z >= L.sz) { up->Set(wx, wy, z - upOff, c); return; }
			int lx = wx - x0, ly = wy - y0; if (L.In(lx, ly, z)) { L.Set(lx, ly, z, c.r, c.g, c.b); any = true; }
		}
		bool In(int wx, int wy) const { return wx >= x0 && wx < x0 + 256 && wy >= y0 && wy < y0 + 256; }
	};
	// Laternenpfosten: Pfosten (pole) z 1..h, Arm am Kopf eine Zelle Richtung (ax, ay), Laterne darunter.
	static void Post(Layer& L, float px, float py, float ax, float ay, int h, C3 pole, C3 arm, C3 cap) {
		int x = Rnd(px), y = Rnd(py);
		int dx = std::fabs(ax) >= std::fabs(ay) ? (ax > 0 ? 1 : -1) : 0, dy = dx ? 0 : (ay > 0 ? 1 : -1);
		if (L.In(x, y)) { for (int z = 1; z <= h; z++) L.Set(x, y, z, pole); L.Set(x, y, h + 1, cap); }
		if (L.In(x + dx, y + dy)) { L.Set(x + dx, y + dy, h + 1, arm); L.Set(x + dx, y + dy, h, CityModels::kLantern); L.lanterns++; }
	}

	// ---- Elfen: Boden (Maerchenwald), Wege, Dekor --------------------------------------------
	// Wegemaske fuer Zone + 14 Rand (damit Dekor an Zonengrenzen in beiden Zonen gleich entschieden wird).
	static const int kM = 28, kMW = 256 + 2 * kM;      // Rand >= Suchrand Dekor (16) + Freiraum (6) + Reserve
	struct EMask { std::vector<uint8_t> m; int x0, y0; uint8_t At(int wx, int wy) const { int lx = wx - x0 + kM, ly = wy - y0 + kM; return (lx < 0 || ly < 0 || lx >= kMW || ly >= kMW) ? 0 : m[(size_t)ly * kMW + lx]; } };
	// ---- Viertel: Bodenmaske und Farben ---------------------------------------------------------------
	// Codes: 20 Gehege, 40+Art Platz, 50 Viertelweg, 60 Feld, 61 Gartenbeet, 62 Gartenrand, 63 unter einem Bauwerk
	// (nur Sperre fuer Walddeko). Ueberschrieben wird nur Freies (0) bzw. Wege nur auf Freiem -- Alleen bleiben.
	static void RasterDistricts(const Plan& p, EMask& M) {
		const int bx = M.x0 - kM - p.cx, by = M.y0 - kM - p.cy;             // Maskenursprung relativ zur Stadtmitte
		auto Cell = [&](int x, int y) -> uint8_t* { int lx = x - bx, ly = y - by; return (lx < 0 || ly < 0 || lx >= kMW || ly >= kMW) ? nullptr : &M.m[(size_t)ly * kMW + lx]; };
		for (const EDist& d : p.dists) {
			if (d.plaza <= 0.0f) continue;
			int r = (int)std::ceil(d.plaza * 1.1f) + 1;
			for (int y = Rnd(d.y) - r; y <= Rnd(d.y) + r; y++) for (int x = Rnd(d.x) - r; x <= Rnd(d.x) + r; x++) {
				if (!InPlaza(d, x + 0.5f, y + 0.5f)) continue;
				uint8_t* v = Cell(x, y); if (v && (*v == 0 || *v == 3)) *v = (uint8_t)(40 + d.kind);
			}
		}
		for (int i : p.dpieces) {
			const Piece& q = p.pieces[i];
			for (int y = q.y - p.cy - 1; y <= q.y - p.cy + q.d; y++) for (int x = q.x - p.cx - 1; x <= q.x - p.cx + q.w; x++) { uint8_t* v = Cell(x, y); if (v && (*v == 0 || *v == 3)) *v = 63; }
		}
		for (const PenR& g : p.pens) for (int y = g.y0; y <= g.y1; y++) for (int x = g.x0; x <= g.x1; x++) { uint8_t* v = Cell(x, y); if (v && (*v == 0 || *v == 3 || *v == 63)) *v = 20; }
		for (const FieldR& f : p.fields) for (int y = f.y0; y <= f.y1; y++) for (int x = f.x0; x <= f.x1; x++) { uint8_t* v = Cell(x, y); if (v && (*v == 0 || *v == 3 || *v == 63)) *v = 60; }
		for (const GardenR& g : p.gardens) for (int y = g.y0; y <= g.y1; y++) for (int x = g.x0; x <= g.x1; x++) {
			uint8_t* v = Cell(x, y); if (!v || !(*v == 0 || *v == 3 || *v == 63)) continue;
			*v = (x == g.x0 || x == g.x1 || y == g.y0 || y == g.y1) ? 62 : 61;
		}
		const float mx0 = (float)bx - 3, my0 = (float)by - 3, mx1 = mx0 + kMW + 6, my1 = my0 + kMW + 6;
		for (const EPath& t : p.dpaths) {
			if (std::max(std::max(t.x0, t.x1), t.cxp) < mx0 || std::min(std::min(t.x0, t.x1), t.cxp) > mx1) continue;
			if (std::max(std::max(t.y0, t.y1), t.cyp) < my0 || std::min(std::min(t.y0, t.y1), t.cyp) > my1) continue;
			float L = std::sqrt((t.x1 - t.x0) * (t.x1 - t.x0) + (t.y1 - t.y0) * (t.y1 - t.y0));
			int steps = std::max(2, (int)(L * 1.6f));
			for (int k = 0; k <= steps; k++) {
				float u = (float)k / steps, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
					if (ox && oy) continue;
					uint8_t* v = Cell((int)std::floor(px) + ox, (int)std::floor(py) + oy);
					if (v && (*v == 0 || *v == 3)) *v = 50;
				}
			}
		}
	}
	static const FieldR* FieldAt(const Plan& p, int x, int y) {               // relativ
		for (const FieldR& f : p.fields) if (x >= f.x0 && x <= f.x1 && y >= f.y0 && y <= f.y1) return &f;
		return nullptr;
	}
	static C3 PlazaColor(int kind, int wx, int wy, bool dwarf);
	static bool DistrictColor(const Plan& p, uint8_t m, int wx, int wy, bool dwarf, C3* out) {
		uint32_t h = Hash(wx, wy, 78, p.seed);
		if (m >= 40 && m < 40 + D_COUNT) { *out = PlazaColor(m - 40, wx, wy, dwarf); return true; }
		if (m == 50) {
			if (dwarf) *out = (h % 3) ? C3{ 140, 132, 122 } : C3{ 122, 116, 108 };
			else *out = (h % 7 == 0) ? C3{ 170, 162, 142 } : ((h & 1) ? C3{ 142, 118, 84 } : C3{ 128, 108, 78 });
			return true;
		}
		if (m == 20) { *out = (h % 3) ? C3{ 120, 142, 70 } : C3{ 138, 116, 72 }; return true; }       // Weide (Gras/Erde) im Gehege
		if (m == 60) {                                                                                  // Feld wie nativ: Doppelfurchen
			const FieldR* f = FieldAt(p, wx - p.cx, wy - p.cy);
			int row = f ? ((f->alongX ? (wy - p.cy - f->y0) : (wx - p.cx - f->x0)) >> 1) & 1 : 0;
			*out = row ? C3{ 126, 94, 58 } : C3{ 104, 76, 48 };
			return true;
		}
		if (m == 61) { *out = (h % 5 == 0) ? C3{ 96, 72, 50 } : C3{ 82, 60, 42 }; return true; }       // Gartenerde
		if (m == 62) { *out = dwarf ? C3{ 128, 122, 116 } : C3{ 162, 158, 148 }; return true; }       // Beetrand (Stein)
		return false;
	}
	static void BuildDistrictMask(const Plan& p, int x0, int y0, EMask& M) {
		M.x0 = x0; M.y0 = y0; M.m.assign((size_t)kMW * kMW, 0);
		RasterDistricts(p, M);
	}
	// 1 Allee, 2 Ringweg, 3 Trampelpfad, 4 Platz
	static void BuildMask(const Plan& p, int x0, int y0, EMask& M) {
		M.x0 = x0; M.y0 = y0; M.m.assign((size_t)kMW * kMW, 0);
		for (int ly = 0; ly < kMW; ly++) for (int lx = 0; lx < kMW; lx++) {
			float dx = (float)(x0 - kM + lx - p.cx), dy = (float)(y0 - kM + ly - p.cy), r = std::sqrt(dx * dx + dy * dy);
			if (r > p.R + 2.0f) continue;
			uint8_t v = 0;
			if (r <= 70.0f) v = 4;
			else {
				float th = std::atan2(dy, dx);
				for (int k = 0; k < 6 && !v; k++) {
					float a = p.av0 + k * kTau / 6.0f;
					if (r <= p.R - 6.0f && AngDiff(th, a) < 1.2f && std::fabs(r * SignedAng(th, a) - AvOffset(p, k, r)) <= 3.0f) v = 1;
				}
				for (const ERing& e : p.erings) if (!v && std::fabs(r - RingR(e, th)) <= 2.0f) v = 2;
			}
			M.m[(size_t)ly * kMW + lx] = v;
		}
		// Viertel: Wiese/Platz/Gehege/Felder/Wege (RasterDistricts)
		RasterDistricts(p, M);
		// Brunnenplaetze (Radius 9) -> Wert 5
		for (const auto& w : p.wells) {
			int lx0 = w.first + p.cx - (x0 - kM), ly0 = w.second + p.cy - (y0 - kM);
			for (int oy = -9; oy <= 9; oy++) for (int ox = -9; ox <= 9; ox++) {
				if (ox * ox + oy * oy > 81) continue;
				int lx = lx0 + ox, ly = ly0 + oy;
				if (lx >= 0 && ly >= 0 && lx < kMW && ly < kMW) M.m[(size_t)ly * kMW + lx] = 5;
			}
		}
		// Trampelpfade als Kurven rastern
		const float mx0 = (float)(x0 - kM - p.cx), my0 = (float)(y0 - kM - p.cy), mx1 = mx0 + kMW, my1 = my0 + kMW;
		for (const EPath& t : p.roads) {                                     // Haendlerstrassen: 10 breit, Alleenbelag (2026-10-27)
			float bx0 = std::min(std::min(t.x0, t.x1), t.cxp) - 7, bx1 = std::max(std::max(t.x0, t.x1), t.cxp) + 7;
			float by0 = std::min(std::min(t.y0, t.y1), t.cyp) - 7, by1 = std::max(std::max(t.y0, t.y1), t.cyp) + 7;
			if (bx1 < mx0 || bx0 > mx1 || by1 < my0 || by0 > my1) continue;
			float L = std::sqrt((t.x1 - t.x0) * (t.x1 - t.x0) + (t.y1 - t.y0) * (t.y1 - t.y0));
			int steps = std::max(2, (int)(L * 2.0f));
			for (int k = 0; k <= steps; k++) {
				float u = (float)k / steps, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				for (int oy = -5; oy <= 5; oy++) for (int ox = -5; ox <= 5; ox++) {
					if (ox * ox + oy * oy > 26) continue;                         // Scheibe Radius 5 -> 10 Bloecke breit
					int lx = (int)std::floor(px - mx0) + ox, ly = (int)std::floor(py - my0) + oy;
					if (lx < 0 || ly < 0 || lx >= kMW || ly >= kMW) continue;
					uint8_t& v = M.m[(size_t)ly * kMW + lx];
					if (!v || v == 3 || v >= 20) v = 1;
				}
			}
		}
		for (const EPath& t : p.trails) {
			float bx0 = std::min(std::min(t.x0, t.x1), t.cxp) - 3, bx1 = std::max(std::max(t.x0, t.x1), t.cxp) + 3;
			float by0 = std::min(std::min(t.y0, t.y1), t.cyp) - 3, by1 = std::max(std::max(t.y0, t.y1), t.cyp) + 3;
			if (bx1 < mx0 || bx0 > mx1 || by1 < my0 || by0 > my1) continue;
			float L = std::sqrt((t.x1 - t.x0) * (t.x1 - t.x0) + (t.y1 - t.y0) * (t.y1 - t.y0));
			int steps = std::max(2, (int)(L * 1.6f));
			for (int k = 0; k <= steps; k++) {
				float u = (float)k / steps, a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float px = a * t.x0 + b * t.cxp + c * t.x1, py = a * t.y0 + b * t.cyp + c * t.y1;
				if ((px - t.x0) * (px - t.x0) + (py - t.y0) * (py - t.y0) < 49.0f) continue;          // Wurzeln am Stamm frei
				for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
					if (ox && oy) continue;
					int lx = (int)std::floor(px - mx0) + ox, ly = (int)std::floor(py - my0) + oy;
					if (lx < 0 || ly < 0 || lx >= kMW || ly >= kMW) continue;
					uint8_t& v = M.m[(size_t)ly * kMW + lx];
					if (!v) v = 3;
				}
			}
		}
	}
	static float NearTreeDist(const Plan& p, float x, float y) {
		float best = 1e9f; int cgx = (int)std::floor(x / 64.0f), cgy = (int)std::floor(y / 64.0f);
		for (int ax = cgx - 1; ax <= cgx + 1; ax++) for (int ay = cgy - 1; ay <= cgy + 1; ay++) {
			auto it = p.treeGrid.find(GKey(ax, ay));
			if (it != p.treeGrid.end()) for (int q : it->second) { float ex = p.trees[q].x - x, ey = p.trees[q].y - y; best = std::min(best, std::sqrt(ex * ex + ey * ey)); }
		}
		return best;
	}
	// Wald zwischen den Baumhaeusern: kleine und mittlere Baeume, Riesenpilze -- direkt in die Bodenschicht.
	// Stamm/Stiel immer mindestens 5 Bloecke frei (Laub, Aeste, Hut erst ab z 6). Keine Einzelbloecke am Boden.
	// Entscheidung je 9er-Rasterfeld nur aus Weltkoordinaten + Wegemaske (Zone + 28 Rand) -> in jeder Zone gleich.
	// Dekor (Hoehe top, Radius rad um die Mitte) gegen grosse Baeume (Profil bis Hoehe top) und Bruecken
	// (Deck in Reichweite der Krone): keine Ueberschneidung.
	static bool DecorClear(const Plan& p, float x, float y, int top, float rad) {
		int cgx = (int)std::floor(x / 64.0f), cgy = (int)std::floor(y / 64.0f);
		for (int ax = cgx - 1; ax <= cgx + 1; ax++) for (int ay = cgy - 1; ay <= cgy + 1; ay++) {
			auto it = p.bigGrid.find(GKey(ax, ay));
			if (it != p.bigGrid.end()) for (int bi : it->second) {
				const EBig& B = p.bigs[bi];
				float d = std::sqrt((B.x - x) * (B.x - x) + (B.y - y) * (B.y - y));
				for (int z = 0; z <= top; z++) { float a = ProfM(p.treeM[B.id], z); if (a >= 0 && d < a + rad + 1.5f) return false; }
			}
			auto ib = p.bridgeGrid.find(GKey(ax, ay));
			if (ib != p.bridgeGrid.end()) for (int bi : ib->second) {
				const EBridge& B = p.bridges[bi];
				if (x < B.bx0 - rad - 2 || x > B.bx1 + rad + 2 || y < B.by0 - rad - 2 || y > B.by1 + rad + 2) continue;
				for (const BCell& c : B.deck) if (c.z - 2 <= top && std::fabs(c.x - x) <= rad + 1.5f && std::fabs(c.y - y) <= rad + 1.5f) return false;
			}
		}
		return true;
	}
	static void ElfDecor(const Plan& p, Layer& L, const EMask& M) {
		// Kronen: ueberwiegend Gruentoene, selten Bluete, Birke, Herbst
		static const C3 kCanopy[8] = { { 70, 150, 72 }, { 58, 130, 64 }, { 92, 168, 80 }, { 64, 124, 58 }, { 80, 146, 70 },
		                               { 226, 176, 206 }, { 214, 220, 196 }, { 206, 150, 70 } };
		const C3 bark{ 96, 70, 50 }, birch{ 222, 218, 206 }, stemC{ 230, 224, 206 };
		const C3 capRed{ 198, 46, 48 }, capBlue{ 70, 150, 220 }, capGold{ 226, 176, 62 }, dot{ 246, 242, 232 };
		const int G = 9;
		int g0x = (int)std::floor((L.x0 - 16.0f) / G), g1x = (int)std::floor((L.x0 + 272.0f) / G);
		int g0y = (int)std::floor((L.y0 - 16.0f) / G), g1y = (int)std::floor((L.y0 + 272.0f) / G);
		for (int gy = g0y; gy <= g1y; gy++) for (int gx = g0x; gx <= g1x; gx++) {
			uint32_t h = Hash(gx, gy, 41, p.seed);
			int wx = gx * G + 1 + (int)(h % (G - 2)), wy = gy * G + 1 + (int)((h >> 4) % (G - 2));
			float dx = (float)(wx - p.cx), dy = (float)(wy - p.cy), r = std::sqrt(dx * dx + dy * dy);
			if (r < 78.0f || r > p.R - 8.0f) continue;
			int kind = (int)((h >> 8) % 100u);
			if (kind >= 36) continue;
			if (p.landing >= 0) { const Piece& lp = p.pieces[p.landing]; if (wx > lp.x - 8 && wx < lp.x + lp.w + 8 && wy > lp.y - 8 && wy < lp.y + lp.d + 8) continue; }
			int clear = kind < 10 ? 6 : 3;
			bool onPath = false;
			for (int oy = -clear; oy <= clear && !onPath; oy++) for (int ox = -clear; ox <= clear && !onPath; ox++) if (M.At(wx + ox, wy + oy)) onPath = true;
			if (onPath || NearTreeDist(p, dx, dy) < (kind < 10 ? 24.0f : 14.0f)) continue;
			if (!DecorClear(p, dx, dy, kind < 10 ? 28 : 11, kind < 10 ? 6.0f : 4.0f)) continue;
			uint32_t h2 = Hash(gx, gy, 42, p.seed);
			// Natuerlicher Stamm (2026-10-13): leicht schief (Richtung/Neigung je Baum), unten dicker mit Wurzelanlauf,
			// nach oben duenner. Liefert die Kronenmitte (Stammspitze) zurueck.
			auto Trunk = [&](int x0, int y0, int Ht, float lean, int thick, C3 tc, uint32_t ht, int* tx, int* ty) {
				float a = U(ht) * kTau, lx = std::cos(a) * lean, ly = std::sin(a) * lean;
				for (int z = 1; z <= Ht; z++) {
					float t = (float)z / Ht, off = t * t;                                       // Neigung nimmt nach oben zu
					int cx0 = x0 + Rnd(lx * off), cy0 = y0 + Rnd(ly * off);
					float r = thick > 1 ? (z <= 2 ? 1.6f : (t < 0.4f ? 1.0f : (t < 0.7f ? 0.6f : 0.0f))) : (z <= 1 ? 1.0f : 0.0f);
					int R = (int)std::ceil(r);
					for (int oy = -R; oy <= R; oy++) for (int ox = -R; ox <= R; ox++) {
						if (ox * ox + oy * oy > r * r + 0.01f) continue;
						L.Set(cx0 + ox, cy0 + oy, z, Vary(tc, Hash(cx0 + ox, cy0 + oy, z, p.seed), 5));
					}
					if (z == Ht) { *tx = cx0; *ty = cy0; }
				}
				for (int k = 0; k < 3 + thick; k++) {                                         // Wurzeln
					float ra = a + kPi + (k - 1.5f) * 1.3f + (U(ht >> (k + 2)) - 0.5f);
					for (int s = 1; s <= 2 + thick; s++) L.Set(x0 + Rnd(std::cos(ra) * s), y0 + Rnd(std::sin(ra) * s), 1, Vary(tc, Hash(x0, y0, 60 + k, p.seed), 6));
				}
			};
			if (kind < 10) {                                            // mittlerer Waldbaum: Stamm bis z 7 frei, mehrere Laubballen
				int ci = (int)((h2 >> 8) % 8);
				C3 trunk = (ci == 6) ? birch : bark, cc = kCanopy[ci];
				int H = 16 + (int)(h2 % 8), topX = wx, topY = wy;
				Trunk(wx, wy, H - 2, 1.0f + 1.6f * U(h2 >> 3), 2, trunk, h2 >> 5, &topX, &topY);
				for (int b = 0; b < 4; b++) {
					uint32_t hb = Hash(wx, wy, 50 + b, p.seed);
					int br = 3 + (int)((hb >> 9) % 2);
					int bx = topX + (int)(hb % 5) - 2, by = topY + (int)((hb >> 3) % 5) - 2;
					int bz = std::max(H - 3 + (int)((hb >> 6) % 4), 6 + br);        // unterste Blattlage >= z 6
					for (int oz = -br; oz <= br; oz++) for (int oy = -br; oy <= br; oy++) for (int ox = -br; ox <= br; ox++) {
						float q = (float)(ox * ox + oy * oy) / (br * br + 0.5f) + (float)(oz * oz) / (br * br * 0.55f + 0.5f);
						if (q > 1.0f || bz + oz < 6) continue;
						uint32_t hv = Hash(bx + ox, by + oy, bz + oz, p.seed + 17);
						if (q > 0.6f && (hv & 7) == 0) continue;
						L.Set(bx + ox, by + oy, bz + oz, Vary(cc, hv >> 5, 8));
					}
				}
				if ((h2 >> 14) % 3 == 0) { L.Set(topX + 1, topY, 5, CityModels::kLantern); L.lanterns++; }    // haengt unter dem Laub
			} else if (kind < 28) {                                     // kleiner Baum: Stamm z 1..5 frei, Krone ab z 6
				int ci = (int)((h2 >> 8) % 8), cr = 2 + (int)((h2 >> 4) % 2);
				C3 trunk = (ci == 6) ? birch : bark, cc = kCanopy[ci];
				int cz = 6 + cr, topX = wx, topY = wy;                      // unterste Kronenlage = 6
				Trunk(wx, wy, cz, 0.6f + 1.2f * U(h2 >> 6), 1, trunk, h2 >> 7, &topX, &topY);
				for (int oz = -cr; oz <= cr; oz++) for (int oy = -cr; oy <= cr; oy++) for (int ox = -cr; ox <= cr; ox++) {
					float q = (float)(ox * ox + oy * oy) / (cr * cr + 0.5f) + (float)(oz * oz) / (cr * cr * 0.7f + 0.5f);
					if (q > 1.0f || cz + oz < 6) continue;
					uint32_t hv = Hash(topX + ox, topY + oy, cz + oz, p.seed + 7);
					if (q > 0.55f && (hv & 7) == 0) continue;
					L.Set(topX + ox, topY + oy, cz + oz, Vary(cc, hv >> 5, 8));
				}
			} else {                                                    // Riesenpilz: Stiel z 1..5 (+), Hut ab z 6
				int H = 5 + (int)(h2 % 3), cr = 2 + (int)((h2 >> 4) % 2), ci = (int)((h2 >> 8) % 3);
				C3 cap = ci == 0 ? capRed : (ci == 1 ? capBlue : capGold);
				for (int z = 1; z <= H; z++) L.Set(wx, wy, z, stemC);
				for (int oz = 0; oz <= cr; oz++) {
					float rr = cr + 0.6f - oz * (cr + 0.6f) / (cr + 1.0f) * 0.8f;
					for (int oy = -cr - 1; oy <= cr + 1; oy++) for (int ox = -cr - 1; ox <= cr + 1; ox++) {
						if (ox * ox + oy * oy > rr * rr) continue;
						uint32_t hv = Hash(wx + ox, wy + oy, H + oz, p.seed + 9);
						L.Set(wx + ox, wy + oy, H + 1 + oz, (ci == 0 && hv % 5 == 0) ? dot : Vary(cap, hv >> 3, 6));
					}
				}
				if ((h2 >> 12) % 3 == 0) { L.Set(wx + 1, wy, H, CityModels::kLantern); L.lanterns++; }   // leuchtet unter dem Hut
			}
		}
	}

	// Pflaster der Viertelplaetze: je Viertel eigenes Muster (Arbeit Kopfstein, Handel Schachbrett hell/rot,
	// Alchemie Gruen/Violett, Tempel Ringe hell/Gold, Tiere Erde/Gras)
	static C3 PlazaColor(int kind, int wx, int wy, bool dwarf) {
		uint32_t h = Hash(wx, wy, 77, 0x9A2Au);
		switch (kind) {
			case D_WORK:   return (h % 3) ? C3{ 120, 112, 104 } : C3{ 100, 94, 88 };
			case D_TRADE:  return (((wx >> 1) + (wy >> 1)) & 1) ? (dwarf ? C3{ 150, 140, 128 } : C3{ 204, 196, 176 }) : C3{ 168, 84, 64 };
			case D_ALCH:   return ((wx + wy) % 5 == 0) ? C3{ 120, 86, 150 } : ((h & 1) ? C3{ 86, 128, 92 } : C3{ 72, 112, 80 });
			case D_TEMPLE: return (((wx * 7 + wy * 3) >> 2) % 4 == 0) ? C3{ 214, 180, 90 } : (dwarf ? C3{ 138, 132, 126 } : C3{ 224, 222, 214 });
			default:       return (h % 4) ? C3{ 112, 92, 64 } : C3{ 92, 130, 64 };
		}
	}
	static void LayerElf(const Plan& p, Layer& L) {
		const C3 tileA{ 196, 198, 186 }, tileB{ 172, 176, 164 }, trim{ 226, 192, 96 }, mossP{ 124, 150, 98 }, mossP2{ 110, 136, 88 };
		const C3 ringC{ 168, 174, 156 }, ringC2{ 152, 160, 142 }, trail{ 134, 114, 84 }, trail2{ 118, 104, 76 }, trailM{ 104, 124, 78 };
		static const C3 kFloor[5] = { { 70, 118, 60 }, { 60, 106, 56 }, { 84, 130, 68 }, { 66, 116, 104 }, { 96, 124, 60 } };
		const C3 silver{ 214, 220, 228 }, gold{ 226, 192, 96 }, wood{ 120, 90, 60 };
		EMask M; BuildMask(p, L.x0, L.y0, M);
		for (int ly = 0; ly < 256; ly++) for (int lx = 0; lx < 256; lx++) {
			int wx = L.x0 + lx, wy = L.y0 + ly;
			float dx = (float)(wx - p.cx), dy = (float)(wy - p.cy), r = std::sqrt(dx * dx + dy * dy);
			if (r > p.R + 2.0f) continue;
			uint32_t h = Hash(wx, wy, 0, p.seed);
			uint8_t m = M.At(wx, wy);
			C3 col;
			if (m == 4) {
				if (r <= 66.0f) { int ring = (int)(r / 5.0f), spoke = (int)(Wrap(std::atan2(dy, dx)) * 16.0f / kTau); col = ((ring + spoke) % 2) ? tileA : tileB; if (r > 60.0f) col = tileB; }
				else col = trim;
			} else if (m == 1) col = (h & 3) ? mossP : mossP2;
			else if (m == 2) col = (h & 1) ? ringC : ringC2;
			else if (m == 3) col = (h % 5 == 0) ? trailM : ((h & 1) ? trail : trail2);
			else if (m == 5) col = ((wx + wy) & 1) ? tileA : tileB;                    // Brunnenplatz
			else if (m >= 20 && DistrictColor(p, m, wx, wy, false, &col)) {}            // Viertel
			else {
				// Waldboden: Moosflecken aus zwei Rauschgroessen
				float n = std::sin(wx * 0.071f + std::sin(wy * 0.053f) * 2.0f) + std::sin(wy * 0.067f + std::sin(wx * 0.049f) * 2.0f);
				int idx = n > 1.1f ? 3 : (n > 0.4f ? 2 : (n > -0.5f ? 0 : (n > -1.2f ? 1 : 4)));
				col = kFloor[idx];
			}
			L.Set(wx, wy, 0, Vary(col, h >> 4, 6));
		}
		ElfDecor(p, L, M);
		// Laternen: Platzring, Alleen, Ringwege, Pfadmitte
		auto Near = [&](float x, float y) { return x >= L.x0 - 3 && x < L.x0 + 259 && y >= L.y0 - 3 && y < L.y0 + 259; };
		for (int k = 0; k < 24; k++) {
			float a = k * kTau / 24.0f, x = p.cx + std::cos(a) * 62.0f, y = p.cy + std::sin(a) * 62.0f;
			if (Near(x, y)) Post(L, x, y, -std::cos(a), -std::sin(a), 6, silver, silver, gold);
		}
		for (int k = 0; k < 6; k++) {
			float a = p.av0 + k * kTau / 6.0f;
			for (int j = 0; 80.0f + j * 18.0f <= p.R - 10.0f; j++) {
				float t = 80.0f + j * 18.0f, side = (j % 2) ? 1.0f : -1.0f;
				float ac = a + AvOffset(p, k, t) / t, ux = std::cos(ac), uy = std::sin(ac), nx = -uy, ny = ux;
				float x = p.cx + ux * t + nx * side * 5.0f, y = p.cy + uy * t + ny * side * 5.0f;
				if (Near(x, y) && !M.At(Rnd(x), Rnd(y))) Post(L, x, y, -nx * side, -ny * side, 6, silver, silver, gold);
			}
		}
		for (const ERing& e : p.erings) {
			int n = (int)(kTau * e.r / 30.0f);
			for (int i = 0; i < n; i++) {
				float a = (i + 0.5f) * kTau / n, rr = RingR(e, a) + 3.5f, x = p.cx + std::cos(a) * rr, y = p.cy + std::sin(a) * rr;
				if (Near(x, y) && !M.At(Rnd(x), Rnd(y))) Post(L, x, y, -std::cos(a), -std::sin(a), 6, silver, silver, gold);
			}
		}
		for (const EPath& t : p.trails) {
			if (!t.lamp) continue;
			float x = p.cx + 0.25f * t.x0 + 0.5f * t.cxp + 0.25f * t.x1 + 2.0f, y = p.cy + 0.25f * t.y0 + 0.5f * t.cyp + 0.25f * t.y1;
			if (Near(x, y) && !M.At(Rnd(x), Rnd(y))) Post(L, x, y, -1.0f, 0.0f, 3, wood, wood, gold);
		}
		for (const auto& lp : p.lampPosts) {                                  // Viertelwege + Ladenstrassen
			float x = p.cx + lp[0], y = p.cy + lp[1];
			if (Near(x, y)) Post(L, x, y, lp[2], lp[3], 6, silver, silver, gold);
		}
	}
	// Haengebruecken: Planken und Laternen ab Modell-z 20 in der Brueckenschicht (Weltlage Bodenoberkante + 21),
	// darunter in der Bodenschicht (z 1..20) -- beide Schichten schliessen lueckenlos aneinander.
	static const int kSkyBase = 20, kSkyH = 34;      // Modell-z 20 = Schicht-z 0
	static void LayerBridges(const Plan& p, Layer& S, Layer& G) {
		const C3 plank{ 152, 112, 72 }, plank2{ 132, 96, 60 };
		auto Put = [&](int x, int y, int mz, C3 c) {                      // mz = Modellhoehe (0 = Bodenoberkante + 1)
			int wx = p.cx + x, wy = p.cy + y;
			if (mz >= kSkyBase) S.Set(wx, wy, mz - kSkyBase, c); else if (mz >= 0) G.Set(wx, wy, mz + 1, c);
		};
		for (const EBridge& b : p.bridges) {
			if (p.cx + b.bx1 + 3 < S.x0 || p.cx + b.bx0 - 3 > S.x0 + 256 || p.cy + b.by1 + 3 < S.y0 || p.cy + b.by0 - 3 > S.y0 + 256) continue;
			for (const BCell& c : b.deck) Put(c.x, c.y, c.z, ((c.x + c.y) & 1) ? plank : plank2);
			for (const BCell& c : b.lamps) {                                   // Laterne unter der Deckmitte, Haken darueber ist das Deck
				int wx = p.cx + c.x, wy = p.cy + c.y;
				if (S.In(wx, wy)) { Put(c.x, c.y, c.z, CityModels::kLantern); S.lanterns++; }
			}
		}
	}

	static void LayerDwarf(const Plan& p, Layer& L) {
		const C3 cob1{ 132, 126, 118 }, cob2{ 118, 112, 106 }, cob3{ 146, 140, 130 }, curb{ 96, 92, 90 };
		const C3 plaza1{ 152, 146, 136 }, plaza2{ 136, 130, 122 }, granite{ 122, 116, 110 }, granite2{ 104, 100, 96 };
		const C3 walk{ 164, 158, 148 }, quoin{ 158, 152, 142 }, iron{ 66, 64, 70 }, gold{ 214, 172, 64 }, dark{ 84, 80, 80 };
		const float Rw = p.Rw;
		const int wallH = 26;
		struct Tw { float x, y, r; int h; };
		std::vector<Tw> tw;
		for (float a : p.towerA) tw.push_back({ p.cx + std::cos(a) * Rw, p.cy + std::sin(a) * Rw, 9.5f, 36 });
		for (int g = 0; g < 4; g++) for (int sgn = -1; sgn <= 1; sgn += 2) {
			float a = g * kTau / 4.0f + sgn * 13.0f / Rw;
			tw.push_back({ p.cx + std::cos(a) * Rw, p.cy + std::sin(a) * Rw, 7.5f, 34 });
		}
		static thread_local std::vector<uint8_t> occ;                          // Strasse/Mauer/Turm/Rampe belegt
		occ.assign(256 * 256, 0);
		for (int ly = 0; ly < 256; ly++) for (int lx = 0; lx < 256; lx++) {
			int wx = L.x0 + lx, wy = L.y0 + ly;
			float dx = (float)(wx - p.cx), dy = (float)(wy - p.cy), r = std::sqrt(dx * dx + dy * dy);
			if (r > Rw + 12.0f) continue;
			uint8_t& oc = occ[(size_t)ly * 256 + lx];
			float th = Wrap(std::atan2(dy, dx)), s = th * Rw;
			uint32_t h = Hash(wx, wy, 0, p.seed);
			auto Brick = [&](int z) { return Vary(((z / 2 + (int)(s / 3.0f)) % 2) ? granite : granite2, Hash(wx, wy, z, p.seed + 1), 5); };
			// Tuerme
			bool tower = false;
			for (const Tw& t : tw) {
				float d = std::sqrt((wx - t.x) * (wx - t.x) + (wy - t.y) * (wy - t.y));
				if (d > t.r) continue;
				tower = true;
				for (int z = 0; z <= t.h; z++) L.Set(wx, wy, z, z == t.h ? Vary(walk, h, 3) : Brick(z));
				if (d >= t.r - 1.2f && ((int)(Wrap(std::atan2(wy - t.y, wx - t.x)) * t.r)) % 2 == 0) { L.Set(wx, wy, t.h + 1, quoin); L.Set(wx, wy, t.h + 2, quoin); }
				break;
			}
			if (tower) { oc = 1; continue; }
			float gds = 1e9f; int gIdx = -1;
			for (int g = 0; g < 4; g++) { float d = AngDiff(th, g * kTau / 4.0f) * Rw; if (d < gds) { gds = d; gIdx = g; } }
			// Mauer mit Toren
			if (std::fabs(r - Rw) <= 3.5f) {
				int cut = gds <= 5.5f ? (int)(13.0f + 3.0f * std::sqrt(std::max(0.0f, 1.0f - (gds / 5.5f) * (gds / 5.5f)))) : -1;
				for (int z = 0; z <= wallH; z++) {
					if (cut >= 0 && z >= 1 && z <= cut) continue;
					C3 c = (z == wallH) ? Vary(walk, h, 3) : Brick(z);
					if (z == 0 && cut >= 0) c = Vary((h & 1) ? cob1 : cob2, h >> 3, 4);
					L.Set(wx, wy, z, c);
				}
				if (r >= Rw + 2.5f && ((int)(s / 2.0f)) % 2 == 0) { L.Set(wx, wy, wallH + 1, quoin); L.Set(wx, wy, wallH + 2, quoin); }
				if (r <= Rw - 2.5f && ((int)(s / 3.0f)) % 3 == 0) L.Set(wx, wy, wallH + 1, granite2);
				oc = 1; continue;
			}
			// Rampen innen neben jedem Tor (+s-Seite), 26 Stufen bis zum Wehrgang
			if (r >= Rw - 8.0f && r < Rw - 3.5f && gIdx >= 0) {
				float srel = SignedAng(th, gIdx * kTau / 4.0f) * Rw;
				if (srel >= 22.0f && srel <= 48.0f) {
					int top = std::min(wallH, (int)(srel - 22.0f) + 1);
					for (int z = 0; z <= top; z++) L.Set(wx, wy, z, z == top ? Vary(walk, h, 3) : Brick(z));
					oc = 1; continue;
				}
			}
			// Strassen und Platz
			bool paint = false; C3 col = cob1;
			if (r <= 106.0f) { col = ((((int)(r / 6.0f)) + (int)(th * 12.0f / kPi)) % 2) ? plaza1 : plaza2; paint = true; }
			for (size_t i = 0; i < p.rings.size() && !paint; i++) {
				float q = std::fabs(r - p.rings[i]);
				if (q <= p.ringHalf[i]) { col = q > p.ringHalf[i] - 0.7f ? curb : ((h % 3 == 0) ? cob3 : ((h & 1) ? cob1 : cob2)); paint = true; }
			}
			for (int k = 0; k < 8 && !paint; k++) {
				float a = k * kPi / 4.0f, ux = std::cos(a), uy = std::sin(a);
				float t = dx * ux + dy * uy, q = std::fabs(-dx * uy + dy * ux);
				if (t >= 100.0f && t <= Rw && q <= 3.5f) { col = q > 2.8f ? curb : ((h % 3 == 0) ? cob3 : ((h & 1) ? cob1 : cob2)); paint = true; }
			}
			if (paint) { L.Set(wx, wy, 0, Vary(col, h >> 5, 4)); oc = 1; }
		}
		// Viertel und Hoefe (2026-10-13, organisch): Wiese, Platz, Gehege, Felder, Wege -- nie ueber Strassen/Mauern
		EMask DM; BuildDistrictMask(p, L.x0, L.y0, DM);
		for (int ly = 0; ly < 256; ly++) for (int lx = 0; lx < 256; lx++) {
			if (occ[(size_t)ly * 256 + lx]) continue;
			int wx = L.x0 + lx, wy = L.y0 + ly; uint8_t m = DM.At(wx, wy); C3 col;
			if (m >= 20 && DistrictColor(p, m, wx, wy, true, &col)) L.Set(wx, wy, 0, Vary(col, Hash(wx, wy, 5, p.seed), 4));
		}
		// Laternen: Strassen, Mauer innen, Tuerme
		auto Near = [&](float x, float y) { return x >= L.x0 - 3 && x < L.x0 + 259 && y >= L.y0 - 3 && y < L.y0 + 259; };
		for (const auto& lp : p.lampPosts) {                                  // Viertelwege (frueher native Bodenleuchte)
			float x = p.cx + lp[0], y = p.cy + lp[1];
			if (Near(x, y)) Post(L, x, y, lp[2], lp[3], 5, quoin, iron, dark);
		}
		for (size_t i = 0; i < p.rings.size(); i++) {
			float rr = p.rings[i]; int n = (int)(kTau * rr / 26.0f);
			for (int j = 0; j < n; j++) {
				float a = (j + 0.5f) * kTau / n; bool nearRad = false;
				for (int k = 0; k < 8; k++) if (AngDiff(a, k * kPi / 4.0f) * rr < 8.0f) nearRad = true;
				if (nearRad) continue;
				float side = (j % 2) ? 1.0f : -1.0f, off = p.ringHalf[i] + 2.0f;
				float x = p.cx + std::cos(a) * (rr + side * off), y = p.cy + std::sin(a) * (rr + side * off);
				if (Near(x, y)) Post(L, x, y, -std::cos(a) * side, -std::sin(a) * side, 5, quoin, iron, dark);
			}
		}
		for (int k = 0; k < 8; k++) {
			float a = k * kPi / 4.0f, ux = std::cos(a), uy = std::sin(a), nx = -uy, ny = ux;
			for (int j = 0; 120.0f + j * 26.0f <= Rw - 20.0f; j++) {
				float t = 120.0f + j * 26.0f, side = (j % 2) ? 1.0f : -1.0f;
				bool nearRing = false; for (float rr : p.rings) if (std::fabs(t - rr) < 8.0f) nearRing = true;
				if (nearRing) continue;
				float x = p.cx + ux * t + nx * side * 5.5f, y = p.cy + uy * t + ny * side * 5.5f;
				if (Near(x, y)) Post(L, x, y, -nx * side, -ny * side, 5, quoin, iron, dark);
			}
		}
		{
			int n = (int)(kTau * Rw / 20.0f);
			for (int j = 0; j < n; j++) {
				float a = (j + 0.5f) * kTau / n; bool skip = false;
				for (int g = 0; g < 4; g++) if (AngDiff(a, g * kTau / 4.0f) * Rw < 45.0f) skip = true;
				for (float ta : p.towerA) if (AngDiff(a, ta) * Rw < 12.0f) skip = true;
				if (skip) continue;
				int x = Rnd(p.cx + std::cos(a) * (Rw - 4.2f)), y = Rnd(p.cy + std::sin(a) * (Rw - 4.2f));
				if (L.In(x, y)) { L.Set(x, y, 23, iron); L.Set(x, y, 22, CityModels::kLantern); L.lanterns++; }
			}
		}
		for (const Tw& t : tw) for (int k = 0; k < 4; k++) {
			float a = std::atan2(t.y - p.cy, t.x - p.cx) + k * kPi / 2.0f;
			int x = Rnd(t.x + std::cos(a) * (t.r + 0.8f)), y = Rnd(t.y + std::sin(a) * (t.r + 0.8f));
			if (L.In(x, y)) { L.Set(x, y, t.h - 7, iron); L.Set(x, y, t.h - 8, CityModels::kLantern); L.lanterns++; }
		}
	}

	// ---- Zaeune als native Objekte (Volumen/Kollision) ---------------------------------------------
	// Wie die Zaeune im Dorfgenerator (Chunk_generateObjects 0x5B214A..0x5B2206): 0x188-Satz mit
	// Struct_InitDefaults 0x58EAA0 vorbelegen, Typ 0x34 (fence01.cub 16x3x12), Position (int64, 1 Block
	// = 0x10000) = Mitte des 2 Bloecke langen Zauns, Fuss = erster Luftblock, Drehung +0x20 (0 entlang x,
	// 1 entlang y), Groesse +0x24/+0x28/+0x2C = 2 / 1 / 1.5 (Zeichnen: Skala = +0x24 / Modellbreite;
	// Kollisionsquader aus denselben Werten, bei ungerader Drehung x/y getauscht, GameController.cpp:101265ff.),
	// anhaengen mit std_vector_pushBack_0x188 0x5A0E00 an Zone+0x0C, Temp mit Member_Destroy 0x593E80 freigeben.
	// Waehrend der Zonenerzeugung (vor dem Ortsindex) -> der native Indexaufbau nimmt sie mit.
	static volatile LONG c_fences = 0, c_abort = 0, c_objs = 0;
	typedef void (__fastcall* ObjInit_t)(void* rec, void* edx);
	typedef void (__fastcall* ObjPush_t)(void* vec, void* edx, const void* rec);
	static uint32_t ReA(uint32_t a) { return Host::Re(a); }
	// Beliebiges natives 0x188-Objekt (Typ, Mitte, Fuss z, Ausrichtung, Halbmasse) -- wie die Zaeune.
	static bool PushObjRaw(void* zone, int type, float wx, float wy, int wz, int orient, float sx, float sy, float sz) {
		__try {
			alignas(8) uint8_t rec[0x188]; memset(rec, 0, sizeof(rec));
			((ObjInit_t)ReA(0x58EAA0))(rec, nullptr);
			*(int*)(rec + 0x00) = type;
			*(int64_t*)(rec + 0x08) = (int64_t)std::llround((double)wx * 65536.0);
			*(int64_t*)(rec + 0x10) = (int64_t)std::llround((double)wy * 65536.0);
			*(int64_t*)(rec + 0x18) = (int64_t)wz << 16;
			*(int*)(rec + 0x20) = orient;
			*(float*)(rec + 0x24) = sx; *(float*)(rec + 0x28) = sy; *(float*)(rec + 0x2C) = sz;
			((ObjPush_t)ReA(0x5A0E00))((uint8_t*)zone + 0x0C, nullptr, rec);
			((ObjInit_t)ReA(0x593E80))(rec, nullptr);                           // Member_Destroy (Temp)
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool PushFenceRaw(void* zone, float wx, float wy, int wz, int orient) { return PushObjRaw(zone, 0x34, wx, wy, wz, orient, 2.0f, 1.0f, 1.5f); }
	bool PushObject(void* zone, int type, float wx, float wy, int wz, int orient, float sx, float sy, float sz) { return zone && PushObjRaw(zone, type, wx, wy, wz, orient, sx, sy, sz); }
	// <PROPS>
	// Native Pflanzen-Props (Felder, Gaerten): Eintrag 0x40 Byte in der Liste Zone+4 -- wie der Feldgenerator
	// (game_misc.cpp:24880ff., 0x5B1EC1..0x5B1FD3): Vorgaben 0x58E9F0, +0x00 Platz, +0x08/+0x10/+0x18 Position
	// (Blockmitte, 1 Block = 0x10000), +0x20 Groesse, +0x24 Drehung (Grad), +0x38 Flags (4 = wiegt im Wind),
	// anhaengen 0x4862E0 (thiscall(list*, const rec*)). Licht setzt der Sonnenlauf danach fuer alle Eintraege.
	static volatile LONG c_props = 0;
	typedef void* (__fastcall* PropInit_t)(void* rec, void* edx);
	typedef void  (__fastcall* PropPush_t)(void* list, void* edx, const void* rec);
	static bool PushPropRaw(void* zone, int slot, int x, int y, int z, float scale, float rotDeg, int flags) {
		__try {
			alignas(8) uint8_t rec[0x40]; memset(rec, 0, sizeof(rec));
			((PropInit_t)ReA(0x58E9F0))(rec, nullptr);
			*(int*)(rec + 0x00) = slot;
			*(int64_t*)(rec + 0x08) = ((int64_t)x << 16) + 0x8000;
			*(int64_t*)(rec + 0x10) = ((int64_t)y << 16) + 0x8000;
			*(int64_t*)(rec + 0x18) = (int64_t)z << 16;
			*(float*)(rec + 0x20) = scale;
			*(float*)(rec + 0x24) = rotDeg;
			*(int*)(rec + 0x38) = flags;
			((PropPush_t)ReA(0x4862E0))((uint8_t*)zone + 4, nullptr, rec);
			InterlockedIncrement(&c_props);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// </PROPS>
	static volatile LONG g_fencesOn = 1;
	void SetFences(bool on) { InterlockedExchange(&g_fencesOn, on ? 1 : 0); }
	static volatile LONG g_residentsOn = 1;
	void SetResidents(bool on) { InterlockedExchange(&g_residentsOn, on ? 1 : 0); }
	bool ResidentsOn() { return g_residentsOn != 0; }
	bool FencesOn() { return g_fencesOn != 0; }
	static void PushFence(void* zone, int x0, int y0, float wx, float wy, int wz, int orient) {
		if (!g_fencesOn) return;
		if (wx < x0 || wx >= x0 + 256 || wy < y0 || wy >= y0 + 256) return;    // gehoert der Zone, in der seine Mitte liegt
		if (PushFenceRaw(zone, wx, wy, wz, orient)) InterlockedIncrement(&c_fences); else InterlockedIncrement(&c_fail);
	}

	// ---- Landeplaetze -> Taxi ---------------------------------------------------------
	struct Land { int cx, cy, npcX, npcY, npcZ, dockX, dockY, dockZ, race; };
	static std::vector<Land> g_landPending;
	static std::vector<Land> g_landKnown;

	// ---- Pruefschleife -----------------------------------------------------------------------------
	// Nach dem Bauen einer Zone wird jedes Bauwerk Voxel fuer Voxel (sichtbare Oberflaeche, ohne Marker)
	// in der Zone nachgelesen (world_getColumnData 0x42F7E0, Typ != 0 = fest). Fehlen Bloecke, wird das
	// Bauwerk ohne Marker noch einmal gesetzt und erneut geprueft. Gezaehlt und (erste 20 Faelle) protokolliert.
	// <ZONEVOXEL>
	typedef uint8_t* (__stdcall* ColData_t)(int x, int y, int z, void* zone);
	static int ZoneVoxelRaw(void* zone, int x, int y, int z) {                  // 1 fest, 0 Luft, -1 unlesbar
		__try {
			uint8_t* v = ((ColData_t)(Host::Re(0x42F7E0)))(x, y, z, zone);
			if (!v) return -1;
			return (v[3] & 0x1F) ? 1 : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	// </ZONEVOXEL>
	static volatile LONG c_chkPieces = 0, c_chkBad = 0, c_chkFixed = 0, c_chkStill = 0, c_chkLog = 0, c_cropped = 0;
	static bool IsMarkerC(uint8_t r, uint8_t g, uint8_t b) { auto L = [](uint8_t v) { return v == 0 || v == 127 || v == 255; }; return L(r) && L(g) && L(b); }
	// Bauwerk in DIESE Zone setzen. Liegt es ganz in der Zone: wie bisher. Reicht es ueber die Grenze (Mutterbaum,
	// Festung, Zwergenhaeuser, Landeplatz): den Teil in dieser Zone in Weltlage ausschneiden und mit der unteren Ecke
	// IN der Zone setzen -- placeStructure baut sonst in der Nachbarzone gar nichts (Pruefschleife, 2026-10-07).
	static int StampPiece(const MInfo& m, const Piece& pc, int bz, void* zone, int x0, int y0, bool blocksOnly) {
		bool inside = pc.x >= x0 && pc.y >= y0 && pc.x + pc.w <= x0 + 256 && pc.y + pc.d <= y0 + 256;
		if (inside || !m.src.Valid()) {
			if (m.native && !m.src.Valid()) return LabBridge::StampModelPtr(m.native, pc.x, pc.y, bz, pc.rot, 1, pc.culture, zone);
			if (m.native && inside) return LabBridge::StampModelPtr(m.native, pc.x, pc.y, bz, pc.rot, 1, pc.culture, zone);
			if (inside) {
				int ok = LabBridge::StampPrepared(blocksOnly && m.pb ? m.pb : m.p, pc.x, pc.y, bz, pc.rot, 1, blocksOnly ? 0 : pc.culture, zone);
				for (const auto& gl : m.glow) LabBridge::StampPreparedFlags(gl.second, pc.x, pc.y, bz + gl.first, pc.rot, 0, 0, zone, 0x0D);   // Lichtbloecke
				return ok;
			}
		}
		const Lab::Structure& s = m.src;
		int ax0 = std::max(pc.x, x0), ay0 = std::max(pc.y, y0), ax1 = std::min(pc.x + pc.w, x0 + 256), ay1 = std::min(pc.y + pc.d, y0 + 256);
		if (ax0 >= ax1 || ay0 >= ay1) return 0;
		static thread_local Lab::Structure* cut = nullptr;
		if (!cut) cut = new Lab::Structure();
		cut->Create(ax1 - ax0, ay1 - ay0, s.sz);
		bool any = false;
		std::vector<std::pair<int, int>> glowAt;                                   // (Index im Schnitt, mz) der Lichtbloecke
		for (int mz = 0; mz < s.sz; mz++) for (int my = 0; my < s.sy; my++) for (int mx = 0; mx < s.sx; mx++) {
			uint8_t r, g, b;
			if (!s.Get(mx, my, mz, r, g, b)) continue;
			if (blocksOnly && IsMarkerC(r, g, b)) continue;
			if (!m.glow.empty() && GlowVoxel(m, r, g, b)) {
				int wx, wy;
				switch (pc.rot & 3) {
					case 0: wx = pc.x + mx; wy = pc.y + my; break;
					case 1: wx = pc.x + my; wy = pc.y + (s.sx - mx) - 1; break;
					case 2: wx = pc.x + (s.sx - mx) - 1; wy = pc.y + (s.sy - my) - 1; break;
					default: wx = pc.x + (s.sy - my) - 1; wy = pc.y + mx; break;
				}
				if (wx >= ax0 && wx < ax1 && wy >= ay0 && wy < ay1) glowAt.push_back(std::make_pair((wy - ay0) * (ax1 - ax0) + (wx - ax0), mz));
				(void)r;
				continue;
			}
			int wx, wy;                                                          // Drehung wie placeStructure
			switch (pc.rot & 3) {
				case 0: wx = pc.x + mx; wy = pc.y + my; break;
				case 1: wx = pc.x + my; wy = pc.y + (s.sx - mx) - 1; break;
				case 2: wx = pc.x + (s.sx - mx) - 1; wy = pc.y + (s.sy - my) - 1; break;
				default: wx = pc.x + (s.sy - my) - 1; wy = pc.y + mx; break;
			}
			if (wx < ax0 || wx >= ax1 || wy < ay0 || wy >= ay1) continue;
			cut->Set(wx - ax0, wy - ay0, mz, r, g, b); any = true;
		}
		int ok = 1;
		if (any) { InterlockedIncrement(&c_cropped); ok = LabBridge::StampInPlace(*cut, ax0, ay0, bz, 0, 1, blocksOnly ? 0 : pc.culture, zone); }
		if (!glowAt.empty()) {                                                    // Lichtbloecke: je Hoehe eine Lage
			std::sort(glowAt.begin(), glowAt.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.second < b.second; });
			const int W = ax1 - ax0, D = ay1 - ay0;
			for (size_t i = 0; i < glowAt.size(); ) {
				int z = glowAt[i].second;
				Lab::Structure lay; lay.Create(W, D, 1);
				for (; i < glowAt.size() && glowAt[i].second == z; i++) {
					int lx = glowAt[i].first % W, ly = glowAt[i].first / W;
					// Farbe aus dem Modell: dieselbe Rueckrechnung waere aufwendig -- Lichtbloecke tragen die Farbe des Modells,
					// daher hier aus src neu lesen ueber die Umkehrdrehung.
					int wx = ax0 + lx, wy = ay0 + ly, mx = 0, my = 0;
					switch (pc.rot & 3) {
						case 0: mx = wx - pc.x; my = wy - pc.y; break;
						case 1: my = wx - pc.x; mx = s.sx - 1 - (wy - pc.y); break;
						case 2: mx = s.sx - 1 - (wx - pc.x); my = s.sy - 1 - (wy - pc.y); break;
						default: my = s.sy - 1 - (wx - pc.x); mx = wy - pc.y; break;
					}
					uint8_t r = 255, g = 255, b = 255; s.Get(mx, my, z, r, g, b);
					lay.Set(lx, ly, 0, r, g, b);
				}
				LabBridge::StampInPlaceFlags(lay, ax0, ay0, bz + z, 0, 0, 0, zone, 0x0D);
			}
		}
		return ok;
	}
	struct CheckRes { int total = 0, missing = 0, zlo = INT_MAX, zhi = INT_MIN; };
	static CheckRes CheckPiece(const MInfo& m, const Piece& pc, int bz, void* zone, int x0, int y0) {
		CheckRes r;
		for (uint32_t q : m.probe) {
			int mx = (int)(q & 1023), my = (int)((q >> 10) & 1023), mz = (int)(q >> 20);
			int wx, wy;                                                          // Drehung wie placeStructure (RE_placeStructure.md)
			switch (pc.rot & 3) {
				case 0: wx = pc.x + mx; wy = pc.y + my; break;
				case 1: wx = pc.x + my; wy = pc.y + (m.sx - mx) - 1; break;
				case 2: wx = pc.x + (m.sx - mx) - 1; wy = pc.y + (m.sy - my) - 1; break;
				default: wx = pc.x + (m.sy - my) - 1; wy = pc.y + mx; break;
			}
			if (wx < x0 || wx >= x0 + 256 || wy < y0 || wy >= y0 + 256) continue;
			int v = ZoneVoxelRaw(zone, wx, wy, bz + mz);
			if (v < 0) continue;
			r.total++;
			if (v == 0) { r.missing++; r.zlo = std::min(r.zlo, mz); r.zhi = std::max(r.zhi, mz); }
		}
		return r;
	}
	static std::string PieceName(const Plan& p, const Piece& pc) {
		if (pc.id >= kTreeIdBase) { int i = pc.id - kTreeIdBase; return "Baum " + (i < (int)p.treeNames.size() ? p.treeNames[i] : std::string("?")); }
		if (pc.id >= M_TH0 && pc.id < M_TH0 + 4) return "Baumhaus " + std::to_string(pc.id - M_TH0 + 1);
		if (pc.id >= M_THS0 && pc.id < M_THS0 + 4) return "Ladenbaum " + std::to_string(pc.id - M_THS0 + 1);
		if (pc.id == M_MOTHER) return "Mutterbaum";
		if (pc.id >= M_DH0 && pc.id < M_DH0 + 4) return "Zwergenhaus " + std::to_string(pc.id - M_DH0 + 1);
		if (pc.id == M_FORT) return "Festung";
		if (pc.id >= M_STATUE_E && pc.id < M_STATUE_E + 6) return "Statue";
		return "Bauwerk";
	}
	static void CheckZone(const Plan& p, void* zone, int zx, int zy, int zg) {
		const int x0 = zx * 256, y0 = zy * 256;
		std::vector<std::pair<const Piece*, CheckRes>> bad;
		for (const Piece& pc : p.pieces) {
			if (pc.x + pc.w <= x0 || pc.x >= x0 + 256 || pc.y + pc.d <= y0 || pc.y >= y0 + 256) continue;
			if (IsNativeModel(pc.id) && g_m[pc.id].probe.empty()) continue;   // natives Modell ohne Abbild
			const MInfo& m = pc.id >= kTreeIdBase ? p.treeM[pc.id - kTreeIdBase] : g_m[pc.id];
			CheckRes r = CheckPiece(m, pc, zg + 1 + pc.zoff, zone, x0, y0);
			InterlockedIncrement(&c_chkPieces);
			if (r.total > 0 && r.missing > std::max(3, r.total / 500)) bad.push_back(std::make_pair(&pc, r));
		}
		for (auto& b : bad) {                                                     // nachsetzen ...
			const Piece& pc = *b.first;
			const MInfo& m = pc.id >= kTreeIdBase ? p.treeM[pc.id - kTreeIdBase] : g_m[pc.id];
			InterlockedIncrement(&c_chkBad);
			StampPiece(m, pc, zg + 1 + pc.zoff, zone, x0, y0, true);
		}
		for (auto& b : bad) {                                                     // ... und noch einmal pruefen
			const Piece& pc = *b.first;
			const MInfo& m = pc.id >= kTreeIdBase ? p.treeM[pc.id - kTreeIdBase] : g_m[pc.id];
			CheckRes r2 = CheckPiece(m, pc, zg + 1 + pc.zoff, zone, x0, y0);
			bool fixed = r2.missing <= std::max(3, r2.total / 500);
			InterlockedIncrement(fixed ? &c_chkFixed : &c_chkStill);
			if (InterlockedIncrement(&c_chkLog) <= 20)
				std::cout << "[Stadt] Pruefung Zone (" << zx << "," << zy << "): " << PieceName(p, pc) << " bei (" << pc.x << "," << pc.y << "): "
				          << b.second.missing << " von " << b.second.total << " Bloecken fehlten (Modellhoehe " << b.second.zlo << ".." << b.second.zhi
				          << ") -> nachgesetzt, jetzt fehlen " << r2.missing << (fixed ? " (ok)" : " (WEITERHIN FEHLERHAFT)") << "." << std::endl;
		}
	}

	void StampZone(void* gc, void* zone, int zx, int zy, const Site& s, int zg) {
		if ((s.kind != ELF && s.kind != DWARF) || !zone || s.radius < 150.0f) return;
		EnsureModels(gc);
		Plan* p = (Plan*)GetPlan(s);
		// Bodenhoehe einmal je Stadt festlegen: so liegen die Teile eines Bauwerks in allen Zonen gleich hoch
		// (vorher mass jede Zone selbst -- 1 Block Unterschied ergab abgeschnittene/versetzte Baeume).
		if (zg != INT_MIN) InterlockedCompareExchange(&p->zg, (LONG)zg, LONG_MIN);
		if (p->zg == LONG_MIN) { InterlockedIncrement(&c_noGround); return; }   // Boden noch unbekannt (Zone ohne Messpunkt)
		zg = (int)p->zg;
		// 1) Bodenschicht (+ bei Elfen die Brueckenschicht darueber). Puffer je Thread wiederverwendet,
		//    gestempelt ohne Kopie -- so bleibt der Adressraum ruhig.
		static thread_local Layer* tlG = nullptr; static thread_local Layer* tlS = nullptr;
		if (!tlG) { tlG = new Layer(); tlS = new Layer(); }
		Layer& L = *tlG; L.x0 = zx * 256; L.y0 = zy * 256; L.any = false; L.lanterns = 0;
		L.L.Create(256, 256, s.kind == DWARF ? 40 : kSkyBase + 1);
		Layer& S = *tlS; S.x0 = L.x0; S.y0 = L.y0; S.any = false; S.lanterns = 0;
		L.up = nullptr; L.upOff = 0;
		if (s.kind == ELF) { S.L.Create(256, 256, kSkyH); L.up = &S; L.upOff = kSkyBase + 1; LayerElf(*p, L); LayerBridges(*p, S, L); }
		else LayerDwarf(*p, L);
		if (L.any && LabBridge::StampInPlace(L.L, L.x0, L.y0, zg, 0, 0, 1, zone) <= 0) InterlockedIncrement(&c_fail);
		if (s.kind == ELF && S.any && LabBridge::StampInPlace(S.L, S.x0, S.y0, zg + 1 + kSkyBase, 0, 0, 1, zone) <= 0) InterlockedIncrement(&c_fail);
		InterlockedExchangeAdd(&c_lanterns, L.lanterns + S.lanterns);
		// 2) Gebaeude, deren Grundflaeche die Zone beruehrt
		const int x0 = zx * 256, y0 = zy * 256;
		for (const Piece& pc : p->pieces) {
			if (pc.x + pc.w <= x0 || pc.x >= x0 + 256 || pc.y + pc.d <= y0 || pc.y >= y0 + 256) continue;
			const MInfo& m = pc.id >= kTreeIdBase ? p->treeM[pc.id - kTreeIdBase] : g_m[pc.id];
			int bz = zg + 1 + pc.zoff, ok = 0;
			if (IsNativeModel(pc.id) && !m.native) ok = 0;
			else ok = StampPiece(m, pc, bz, zone, x0, y0, false);
			if (ok > 0) InterlockedIncrement(&c_pieces); else InterlockedIncrement(&c_fail);
			const int thv = (pc.id >= M_TH0 && pc.id < M_TH0 + 4) ? pc.id - M_TH0 : ((pc.id >= M_THS0 && pc.id < M_THS0 + 4) ? pc.id - M_THS0 : -1);
			if (thv >= 0 && ok > 0)                                              // Treppengelaender (native Zaeune)
				for (const CityModels::FenceP& f : g_thFences[thv]) PushFence(zone, x0, y0, pc.x + f.x, pc.y + f.y, zg + 1 + f.z, f.orient);
			if (pc.id == M_MOTHER && ok > 0)                                     // Gelaender der Wendeltreppe (Mutterbaum nie gedreht)
				for (const CityModels::FenceP& f : g_motherFences) PushFence(zone, x0, y0, pc.x + f.x, pc.y + f.y, zg + 1 + f.z, f.orient);
			if (pc.id == M_LANDING && ok > 0) {
				int mx = pc.x + pc.w / 2, my = pc.y + pc.d / 2;
				if (mx >= x0 && mx < x0 + 256 && my >= y0 && my < y0 + 256) {
					// Deck = Modell-z 60 (landingplace.cub, gemessen); NPC steht darauf, Schiff schwebt oestlich daneben.
					Land l{ s.cx, s.cy, pc.x + 12, pc.y + 16, bz + 61, pc.x + 16 + 26, pc.y + 16, bz + 61, s.kind == ELF ? 0 : 9 };   // Taxi-NPC: ElfMale / DwarfMale
					Lock lk; g_landPending.push_back(l);
				}
			}
		}
		// 2b) Pruefschleife: fehlende Bloecke nachsetzen (nach ALLEN Bauwerken der Zone -- spaetere koennen fruehere beschaedigen)
		CheckZone(*p, zone, zx, zy, zg);
		// 2c) Viertel: native Objekte (Lagerfeuer = Kochstelle/Braustand, Marktstaende) und Gehegezaeune -- je Zone die,
		//     deren Mitte in ihr liegt
		for (const ObjP& o : p->objs) {
			float wx = p->cx + o.x, wy = p->cy + o.y;
			if (wx < x0 || wx >= x0 + 256 || wy < y0 || wy >= y0 + 256) continue;
			if (PushObjRaw(zone, o.type, wx, wy, zg + o.z, o.orient, o.sx, o.sy, o.sz)) InterlockedIncrement(&c_objs); else InterlockedIncrement(&c_fail);
		}
		for (const CityModels::FenceP& f : p->penFences) PushFence(zone, x0, y0, p->cx + f.x, p->cy + f.y, zg + 1 + f.z, f.orient);
		// 2d) Felder: native Pflanzen auf jeder zweiten Doppelreihe (wie das native Feld, dichter);
		//     Gaerten: Pflanzen des Bioms (Bloecke, je 4 Felder eine) und native Blumen dazwischen
		for (const FieldR& f : p->fields) {
			int fx0 = p->cx + f.x0, fy0 = p->cy + f.y0, fx1 = p->cx + f.x1, fy1 = p->cy + f.y1;
			if (fx1 < x0 || fx0 >= x0 + 256 || fy1 < y0 || fy0 >= y0 + 256) continue;
			const Crop& c = kCrops[f.crop % 6];
			for (int wy = std::max(fy0 + 1, y0); wy <= std::min(fy1 - 1, y0 + 255); wy++)
				for (int wx = std::max(fx0 + 1, x0); wx <= std::min(fx1 - 1, x0 + 255); wx++) {
					int row = ((f.alongX ? (wy - fy0) : (wx - fx0)) >> 1) & 1;
					if (!row) continue;
					uint32_t h = Hash(wx, wy, 90, p->seed);
					if (h % 100u >= 45u) continue;
					if (f.crop == 0 && (h >> 8) % 10u == 0) { PushPropRaw(zone, 0x15, wx, wy, zg + 1, 0.05f, U(h >> 3) * 360.0f, 4); continue; }   // Kornblume im Weizen
					PushPropRaw(zone, c.slot, wx, wy, zg + 1, c.scale * (0.9f + 0.2f * U(h >> 5)), c.rotate ? U(h >> 2) * 360.0f : 0.0f, c.flags);
				}
		}
		for (const GardenR& g : p->gardens) {
			int gx0 = p->cx + g.x0, gy0 = p->cy + g.y0, gx1 = p->cx + g.x1, gy1 = p->cy + g.y1;
			if (gx1 < x0 || gx0 >= x0 + 256 || gy1 < y0 || gy0 >= y0 + 256) continue;
			const bool mod = g.style != 0 && !p->gardenM.empty();
			for (int wy = std::max(gy0 + 1, y0); wy <= std::min(gy1 - 1, y0 + 255); wy++)
				for (int wx = std::max(gx0 + 1, x0); wx <= std::min(gx1 - 1, x0 + 255); wx++) {
					uint32_t h = Hash(wx, wy, 91, p->seed);
					int lx = wx - gx0, ly = wy - gy0;
					if (mod && (lx % 4) == 2 && (ly % 4) == 2 && (g.style == 1 || (h & 1))) {             // Pflanze des Bioms (Bloecke)
						const MInfo& m = p->gardenM[(h >> 4) % p->gardenM.size()];
						int ox = wx - m.sx / 2, oy = wy - m.sy / 2;
						if (ox >= x0 && oy >= y0 && ox + m.sx <= x0 + 256 && oy + m.sy <= y0 + 256 && ox > gx0 && oy > gy0 && ox + m.sx <= gx1 && oy + m.sy <= gy1)
							LabBridge::StampPrepared(m.p, ox, oy, zg + 1, 0, 0, 0, zone);
						continue;
					}
					if (mod && std::abs((lx % 4) - 2) <= 1 && std::abs((ly % 4) - 2) <= 1) continue;      // um die Pflanze frei
					if (h % 100u >= 55u) continue;
					const Flower& fl = kFlowers[(h >> 8) % 7u];
					PushPropRaw(zone, fl.slot, wx, wy, zg + 1, fl.scale * (0.85f + 0.3f * U(h >> 3)), U(h >> 2) * 360.0f, 4);
				}
		}
		// 3) Brueckengelaender (native Zaeune) -- jede Zone legt die Zaeune an, deren Mitte in ihr liegt
		if (s.kind == ELF)
			for (const EBridge& b : p->bridges) {
				if (p->cx + b.bx1 + 3 < x0 || p->cx + b.bx0 - 3 > x0 + 256 || p->cy + b.by1 + 3 < y0 || p->cy + b.by0 - 3 > y0 + 256) continue;
				for (const CityModels::FenceP& f : b.fences) PushFence(zone, x0, y0, p->cx + f.x, p->cy + f.y, zg + 1 + f.z, f.orient);
			}
		InterlockedIncrement(&c_zones);
	}
	void NoteAbort() {
		LONG n = InterlockedIncrement(&c_abort);
		if (n == 1) std::cout << "[Stadt] WARNUNG: Zonenaufbau abgebrochen (Ausnahme abgefangen) -- dort fehlen Teile der Stadt." << std::endl;
	}

	// ---- Bewohner der Elfenstadt (Spielthread) -----------------------------------------------------
	// Grosse Elfen aus Strukturen\Mobs\elf-w1..5 / elf-m1..5 (.mob; fehlt eine, native Elfen x1.2), friedlich
	// (Feindlichkeit 3), auf Wegen und Hängebruecken; bis zu kElves im Umkreis von 160 Bloecken um den Spieler.
	// Der eigene Haendler steht auf dem Aussichtsbalkon des Mutterbaums (Hoehe 150), der Taxi-NPC (Richtungs-
	// haendler) als Elf oben auf dem Landeplatz.
	static const int kElves = 18;
	static std::vector<uint64_t> g_elves;
	static std::vector<MobLab::CustomMob> g_elfMobs; static bool g_elfMobsTried = false;
	static void BuildSpawnPoints(Plan& p) {
		if (!p.spawnPts.empty() || p.kind != ELF) return;
		for (size_t i = 0; i < p.trails.size(); i += 2) {                       // Trampelpfade (zwischen den Haeusern)
			const EPath& t = p.trails[i];
			for (float u : { 0.35f, 0.65f }) {
				float a = (1 - u) * (1 - u), b = 2 * u * (1 - u), c = u * u;
				float x = a * t.x0 + b * t.cxp + c * t.x1, y = a * t.y0 + b * t.cyp + c * t.y1;
				if (NearTreeDist(p, x, y) < 22.0f) continue;                     // nicht in Stamm/Treppe
				p.spawnPts.push_back({ Rnd(x), Rnd(y), 0 });
			}
		}
		for (const EPath& t : p.dpaths) {                                      // Viertelwege (Hoefe, Maerkte)
			float x = 0.25f * t.x0 + 0.5f * t.cxp + 0.25f * t.x1, y = 0.25f * t.y0 + 0.5f * t.cyp + 0.25f * t.y1;
			if (!ElementHit(p, x, y, 2.0f, true)) p.spawnPts.push_back({ Rnd(x), Rnd(y), 0 });
		}
		for (const EBridge& b : p.bridges) {                                   // Bruecken: Mitte des Decks
			if (b.deck.size() < 25) continue;
			const BCell& c = b.deck[(b.deck.size() / 10) * 5 + 2];              // mittlere Reihe
			p.spawnPts.push_back({ c.x, c.y, c.z + 1 });
		}
	}
	// Alle "elf-*.mob" unter Strukturen\Mobs (auch in Unterordnern). Fehlen sie, alle 60 s erneut suchen
	// (Dateien koennen waehrend des Spiels hineinkopiert werden).
	static void FindMobFiles(const std::string& dir, const char* prefix, std::vector<std::string>& out, int depth) {
		if (depth > 4) return;
		WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName;
			if (n == "." || n == "..") continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { FindMobFiles(dir + "\\" + n, prefix, out, depth + 1); continue; }
			std::string low = n; for (char& ch : low) ch = (char)tolower((unsigned char)ch);
			if (low.size() > 4 && low.compare(0, strlen(prefix), prefix) == 0 && low.compare(low.size() - 4, 4, ".mob") == 0) out.push_back(dir + "\\" + n);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	static void LoadElfMobs() {
		static DWORD lastTry = 0; DWORD now = GetTickCount();
		if (g_elfMobsTried && (!g_elfMobs.empty() || now - lastTry < 60000)) return;
		g_elfMobsTried = true; lastTry = now;
		std::vector<std::string> files; FindMobFiles(MobLab::MobsDir(), "elf-", files, 0);
		std::sort(files.begin(), files.end());
		for (const std::string& f : files) {
			MobLab::CustomMob m; std::string err;
			if (MobLab::LoadMob(f, m, err)) { m.hostility = 3; m.wander = true; g_elfMobs.push_back(m); }
			else std::cout << "[Stadt] Bewohner: " << f << " nicht lesbar (" << err << ")." << std::endl;
		}
		char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
		std::cout << "[Stadt] Bewohner: " << g_elfMobs.size() << " Elfen-Mobs (elf-*.mob) aus " << cwd << "\\" << MobLab::MobsDir()
		          << (g_elfMobs.empty() ? " -- keine gefunden, es kommen native Elfen x1.2 (neue Suche in 60 s)." : ".") << std::endl;
	}
	// <SPIELTHREAD-VIERTEL>
	// ---- Viertel-Bewohner (Spielthread) ---------------------------------------------------------------
	// Native Haendler (Klasse 0x80..0x84) an den Haendlerhuetten, Alchemist am Braustand, Tiere in den Gehegen,
	// Kugel im Tempel ([R] in der Naehe = Segen). Gesetzt, sobald der Spieler < 120 Bloecke entfernt und die Zone
	// geladen ist; steht schon einer am Posten, wird er uebernommen (keine Doppelten).
	// Waren der nativen Haendler: dieselben Generatoren wie im Dorf (Chunk_generateObjects): 0x80 -> 0x5C9B30,
	// 0x81/0x83 -> 0x5CA0A0, 0x82 -> 0x5C8390; thiscall(World = GC+0x2E4, Spawn*, 1, 0x14), ret 0xC. Sie fuellen
	// ausschliesslich Spawn+0xF6C (= Inventar-Reiter, gleiche Struktur wie Kreatur+0x11DC; Disassembly 2026-10-13)
	// -> mit Spawn* = Kreatur + 0x11DC - 0xF6C landen die Waren direkt im Inventar der Kreatur.
	struct LivePost { uint64_t key; int kind, x, y, z, a, n, race0; };
	static std::map<uint64_t, std::vector<uint64_t>> g_postGuids;
	static std::map<uint64_t, DWORD> g_postTry;
	static volatile LONG c_vendors = 0, c_animals = 0, c_bless = 0;
	typedef void (__fastcall* VendGen_t)(void* world, void* edx, void* spawn, int a, int b);
	static bool NativeWaresRaw(cube::GameController* gc, uint8_t* c, int cls) {
		uint32_t fn = cls == 0x80 ? 0x5C9B30 : (cls == 0x82 ? 0x5C8390 : 0x5CA0A0);
		__try { ((VendGen_t)ReA(fn))((uint8_t*)gc + 0x2E4, nullptr, c + (0x11DC - 0xF6C), 1, 0x14); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool ConfigVendorRaw(cube::Creature* c, int cls) {
		__try {
			c->entity_data.hostility_flags = 3;
			c->entity_data.char_class = (char)cls;
			c->entity_data.appearance.movement_flags &= (uint16_t)~0x0080;
			*(uint32_t*)((uint8_t*)c + 0x1b0) = 0x8024; *(uint32_t*)((uint8_t*)c + 0x1b4) = 0x8023; *(uint32_t*)((uint8_t*)c + 0x1b8) = 6;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Katalog { int Anzahl; Item } je 0x11C in EINER Kategorie nach Kreatur+0x11DC (Spiel-Allokator 0x68D668).
	typedef void* (__cdecl* GameNew_t)(unsigned);
	static bool WriteCatalogRaw(uint8_t* c, const uint8_t* items, int n, int amount) {
		__try {
			if (n <= 0) return false;
			uint8_t* buf = (uint8_t*)((GameNew_t)ReA(0x68D668))((unsigned)(n * 0x11C));
			uint8_t* cat = (uint8_t*)((GameNew_t)ReA(0x68D668))(0xC);
			if (!buf || !cat) return false;
			for (int i = 0; i < n; i++) { *(int32_t*)(buf + i * 0x11C) = amount; memcpy(buf + i * 0x11C + 4, items + i * 0x118, 0x118); }
			*(uint32_t*)(cat + 0) = (uint32_t)(uintptr_t)buf; *(uint32_t*)(cat + 4) = (uint32_t)(uintptr_t)(buf + n * 0x11C); *(uint32_t*)(cat + 8) = *(uint32_t*)(cat + 4);
			*(uint32_t*)(c + 0x11DC) = (uint32_t)(uintptr_t)cat; *(uint32_t*)(c + 0x11E0) = (uint32_t)(uintptr_t)cat + 0xC; *(uint32_t*)(c + 0x11E4) = (uint32_t)(uintptr_t)cat + 0xC;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Trankzutaten des Alchemisten: alle Zutaten der geladenen Rezepte (Strukturen\Rezepte), ohne Doppelte.
	static std::vector<uint8_t> AlchemyWares() {
		std::vector<uint8_t> out; std::vector<std::array<int, 4>> seen;
		for (const std::string& f : Recipes::ListFiles()) {
			Recipes::RecipeDef r; std::string err;
			if (!Recipes::Load(Recipes::RecipesDir() + "\\" + f, r, err) || !r.enabled) continue;
			for (const Recipes::Ingredient& ig : r.ing) {
				std::array<int, 4> k = { ig.type, ig.sub, ig.mat, ig.item };
				if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
				seen.push_back(k);
				uint8_t it[0x118]; memset(it, 0, sizeof(it));
				Items::ItemDef d;
				if (ig.item > 0 && Items::ById(ig.item, &d)) Items::FillItem(d, it);
				else { it[0] = (uint8_t)ig.type; it[1] = (uint8_t)ig.sub; it[0x0D] = (uint8_t)ig.mat; *(uint16_t*)(it + 0x10) = 1; }
				out.insert(out.end(), it, it + 0x118);
				if (out.size() >= 0x118 * 40) return out;
			}
		}
		return out;
	}
	typedef void* (__fastcall* WAssignC_t)(void* str, void* edx, const wchar_t* s);
	typedef void  (__fastcall* WFreeC_t)(void* str, void* edx);
	typedef void  (__fastcall* ChatPrintC_t)(void* chat, void* edx, void* wstr, void* color);
	static void CityChat(cube::GameController* gc, const wchar_t* text) {
		__try {
			if (!gc || !gc->ChatWidget) return;
			alignas(4) uint8_t w[0x18]; memset(w, 0, sizeof(w));
			((WAssignC_t)ReA(0x40EB60))(w, nullptr, text);
			float col[4] = { 1.0f, 0.9f, 0.55f, 1.0f };
			((ChatPrintC_t)ReA(0x43AB30))((void*)gc->ChatWidget, nullptr, w, col);
			((WFreeC_t)ReA(0x593E50))(w, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	// Segen der Kugel: Leben voll (max. Leben 0x444DB0, thiscall), Mana voll.
	typedef float (__fastcall* MaxHp_t)(void* c, void* edx);
	static bool BlessRaw(cube::GameController* gc) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false;
			float mx = ((MaxHp_t)ReA(0x444DB0))(c, nullptr);
			if (mx > 0.0f) *(float*)(c + 0x16C) = mx;
			*(float*)(c + 0x170) = 1.0f;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static std::vector<LivePost> g_extPosts;                                   // aus Gebaeude-Funktionen (unter Lock)
	void AddExternalPost(int kind, int x, int y, int z, int cls) {
		Lock l;
		uint64_t key = 0xE7E7000000000000ull ^ ((uint64_t)(uint32_t)x << 24) ^ ((uint64_t)(uint32_t)y << 4) ^ (uint64_t)(uint32_t)z ^ ((uint64_t)cls << 56);
		for (const LivePost& q : g_extPosts) if (q.key == key) return;
		g_extPosts.push_back({ key, kind == 1 ? NP_ALCH : NP_VENDOR, x, y, z, cls, 1, 2 });     // Mensch (HumanMale/-Female)
	}
	static std::set<uint64_t> g_walked;                                        // Tiere mit Wander-KI (Spielthread)
	static void PostsTick(cube::GameController* gc, const std::vector<LivePost>& posts, int64_t px, int64_t py, int64_t pz) {
		DWORD now = GetTickCount();
		bool spawned = false;                                                       // hoechstens eine Kreatur je Durchgang
		for (const LivePost& q : posts) {
			long long dx = q.x - px, dy = q.y - py;
			if (q.kind == NP_ORB) {                                                     // Kugel: [R] in der Naehe
				static bool rWas = false; static DWORD lastBless = 0;
				bool isNear = dx * dx + dy * dy <= 6 * 6 && std::llabs(q.z - pz) <= 16;
				HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
				bool rDown = isNear && pid == GetCurrentProcessId() && NativeUi::PlayInputCached() && (GetAsyncKeyState('R') & 0x8000) != 0;
				if (rDown && !rWas) {
					// ab 25 gefundenen kleinen Tempeln: Zielliste der Biome (Temples.cpp), sonst Segen + Fortschritt
					if (Temples::Found() >= Temples::kNeeded) Temples::CityOrb(gc);
					else {
						if (now - lastBless >= 60000 && BlessRaw(gc)) { lastBless = now; InterlockedIncrement(&c_bless); }
						Temples::CityOrb(gc);
					}
				}
				if (isNear) rWas = rDown;
				continue;
			}
			if (dx * dx + dy * dy > 120LL * 120LL) continue;
			std::vector<uint64_t>& gs = g_postGuids[q.key];
			for (size_t i = 0; i < gs.size(); ) { int64_t x, y, z; if (!MobLab::CreaturePos(gc, gs[i], &x, &y, &z)) gs.erase(gs.begin() + i); else i++; }
			int want = q.kind == NP_ANIMAL ? q.n : 1;
			if (q.kind == NP_ANIMAL || q.kind == NP_SCARECROW) {                        // Tiere/Vogelscheuche: vorhandene uebernehmen, Ueberzaehlige entfernen
				MobLab::CreatureInfo ci[16];
				int nc = MobLab::ListCreaturesNear(gc, q.x, q.y, q.z, q.kind == NP_SCARECROW ? 3 : 16, 10, ci, 16);
				for (int i = 0; i < nc; i++) {
					if (ci[i].race != q.a || ci[i].hostility == 1 || ci[i].hostility == 5 || ci[i].hp <= 0.0f) continue;
					if (std::find(gs.begin(), gs.end(), ci[i].guid) != gs.end()) continue;
					if ((int)gs.size() < want) gs.push_back(ci[i].guid);
					else MobLab::SinkCreature(gc, ci[i].guid);                           // keine Vermehrung
				}
				while ((int)gs.size() > want) { MobLab::SinkCreature(gc, gs.back()); gs.pop_back(); }
				if (q.kind == NP_ANIMAL) {                                              // Umherwandern (auch fuer uebernommene Tiere), einmal je Tier
					for (uint64_t g : gs) {
						if (g_walked.count(g)) continue;
						cube::Creature* c = MobLab::CreatureByGuid(gc, g);
						if (!c) continue;
						cube::util::AttachRandomWalk(c, (int64_t)q.x * 65536, (int64_t)q.y * 65536);
						g_walked.insert(g);
					}
					if (g_walked.size() > 4096) g_walked.clear();
				}
			}
			if ((int)gs.size() >= want) continue;
			if (q.kind != NP_ANIMAL && q.kind != NP_MERCHANT && q.kind != NP_SCARECROW) {   // steht schon einer da? -> uebernehmen
				uint64_t here[4] = {};
				int n = MobLab::CountCreaturesNear(gc, q.x, q.y, q.z, 2, 4, q.a, 3, here, 4);
				if (n > 0) { gs.push_back(here[0]); continue; }
			}
			if (spawned || (int)(now - g_postTry[q.key]) < 15000) continue;
			if (!LabBridge::ZoneAt(gc, q.x >> 8, q.y >> 8)) continue;
			g_postTry[q.key] = now;
			const int64_t U = 65536;
			if (q.kind == NP_MERCHANT) continue;
			if (q.kind == NP_SCARECROW) {                                               // native Vogelscheuche (wie im Feld: Rasse 0x8C, Feindlichkeit 6)
				uint64_t g = MobLab::SpawnRaceAt(gc, 0x8C, 6, -1, (int64_t)q.x * U + U / 2, (int64_t)q.y * U + U / 2, (int64_t)q.z * U, false, false, 1.0f);
				if (g) gs.push_back(g);
				spawned = true;
				continue;
			}
			if (q.kind == NP_ANIMAL) {
				uint32_t h = Hash((int)now, q.x, q.y, (uint32_t)gs.size());
				int ox = (int)(h % 7) - 3, oy = (int)((h >> 4) % 5) - 2;
				int64_t fx = (int64_t)(q.x + ox) * U + U / 2, fy = (int64_t)(q.y + oy) * U + U / 2, fz = (int64_t)q.z * U + U / 4;
				uint64_t g = MobLab::SpawnRaceAt(gc, q.a, 1, -1, fx, fy, fz, false, false, 1.0f);   // friedlich wie natives Huhn:
				if (g) MobLab::SetPeaceful(gc, g);                                        // Feindlichkeit 1 + Flag 0x10
				cube::Creature* c = g ? MobLab::CreatureByGuid(gc, g) : nullptr;
				if (c) { cube::util::AttachRandomWalk(c, (int64_t)q.x * U, (int64_t)q.y * U); g_walked.insert(g); gs.push_back(g); InterlockedIncrement(&c_animals); }
				spawned = true;
				continue;
			}
			// Haendler / Alchemist
			int race = q.race0 + (int)(Hash(q.x, q.y, q.a, 3) & 1);
			int64_t fx = (int64_t)q.x * U + U / 2, fy = (int64_t)q.y * U + U / 2, fz = (int64_t)q.z * U + U / 4;
			uint64_t g = MobLab::SpawnRaceAt(gc, race, 3, -1, fx, fy, fz, false, false, 1.0f);
			cube::Creature* c = g ? MobLab::CreatureByGuid(gc, g) : nullptr;
			spawned = true;
			if (!c) { if (g) gs.push_back(g); std::cout << "[Stadt] Haendlerposten (" << q.x << "," << q.y << "," << q.z << "): Spawn ohne Kreatur (GUID " << g << ")." << std::endl; continue; }
			// Waren und Beruf: LAB -> Haendler (alchemist, huette1..huette5 nach Klasse 0x80..0x84)
			Vendors::VendorDef vd;
			std::string role = q.kind == NP_ALCH ? std::string("alchemist") : "huette" + std::to_string((q.a >= 0x80 && q.a <= 0x84) ? q.a - 0x7F : 1);
			Vendors::ForRole(role, &vd);
			ConfigVendorRaw(c, vd.cls);
			if (!Vendors::ApplyTo(gc, c, vd) && q.kind == NP_ALCH) { std::vector<uint8_t> w = AlchemyWares(); WriteCatalogRaw((uint8_t*)c, w.data(), (int)(w.size() / 0x118), 20); }
			gs.push_back(g); InterlockedIncrement(&c_vendors);
			std::cout << "[Stadt] " << (q.kind == NP_ALCH ? "Alchemist" : "Haendler") << " '" << role << "' bei (" << q.x << "," << q.y << "," << q.z
			          << ") gesetzt (Spieler " << std::llabs(dx) + std::llabs(dy) << " Bloecke entfernt)." << std::endl;
		}
	}

	// </SPIELTHREAD-VIERTEL>
	static void PopulateTick(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 1500) return;
		last = now;
		int64_t px, py, pz;
		{ AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (!me.found) return; px = me.pos.blockX; py = me.pos.blockY; pz = me.pos.blockZ; }
		(void)pz;
		for (size_t i = 0; i < g_elves.size(); ) { int64_t x, y, z; if (!MobLab::CreaturePos(gc, g_elves[i], &x, &y, &z)) g_elves.erase(g_elves.begin() + i); else i++; }
		Plan* city = nullptr;
		std::vector<std::array<int64_t, 3>> posts;                              // feste Haendlerposten (Elfen- UND Zwergenstadt)
		std::vector<LivePost> live;                                             // Viertel-Bewohner
		{
			Lock l;
			for (auto& kv : g_plans) {
				Plan* p = kv.second;
				if (p->zg == LONG_MIN) continue;
				double dx = (double)(px - p->cx), dy = (double)(py - p->cy);
				if (dx * dx + dy * dy >= (double)(p->R + 150) * (p->R + 150)) continue;
				bool trade = false;
				for (size_t i = 0; i < p->npcs.size(); i++) {
					const NpcP& n = p->npcs[i];
					int wx = p->cx + n.x, wy = p->cy + n.y, wz = (int)p->zg + 1 + n.z;
					if (n.kind == NP_MERCHANT) { posts.push_back({ wx, wy, wz }); trade = true; continue; }
					live.push_back({ (uint64_t)kv.first * 1315423911ull + i, n.kind, wx, wy, wz, n.a, n.n, p->kind == ELF ? 0 : 9 });
				}
				if (trade) { if (p->kind == ELF && !city) city = p; continue; }
				if (p->kind == ELF) {
					// Aussichtsbalkon des Mutterbaums (Balkon r 17.5..23.5 bei Modellhoehe 150)
					const float a = 0.30f, r = 20.5f;
					posts.push_back({ p->cx + (int64_t)std::lround(std::cos(a) * r), p->cy + (int64_t)std::lround(std::sin(a) * r), (int64_t)p->zg + 1 + 150 + 1 });
					if (!city) city = p;
				} else {
					// Zwergenstadt: Platz vor der Festung (Platz bis r 106, Festung bis ~70, Brunnen bei r 88 auf den Diagonalen)
					const float a = kPi / 8.0f, r = 96.0f;
					posts.push_back({ p->cx + (int64_t)std::lround(std::cos(a) * r), p->cy + (int64_t)std::lround(std::sin(a) * r), (int64_t)p->zg + 1 });
				}
			}
			if (city) BuildSpawnPoints(*city);
			live.insert(live.end(), g_extPosts.begin(), g_extPosts.end());
		}
		for (const auto& q : posts) AdventureMod::RegisterMerchantPost(q[0], q[1], q[2]);   // idempotent; gesetzt wird im Haendler-Tick
		if (g_residentsOn) PostsTick(gc, live, px, py, pz);
		if (!city || !g_residentsOn) return;                                     // Bewohner-Schalter (LAB) betrifft nur die Elfen
		const int zg = (int)city->zg;
		// grosse Elfen
		int nearCount = 0;
		for (uint64_t g : g_elves) { int64_t x, y, z; if (MobLab::CreaturePos(gc, g, &x, &y, &z)) { long long dx = (x >> 16) - px, dy = (y >> 16) - py; if (dx * dx + dy * dy < 160LL * 160LL) nearCount++; } }
		if (nearCount >= kElves || g_elves.size() >= 40 || city->spawnPts.empty()) return;
		LoadElfMobs();
		uint32_t h = Hash((int)(now / 1500), (int)px, (int)py, city->seed);
		for (int attempt = 0; attempt < 12; attempt++) {
			const BCell& sp = city->spawnPts[(h = Hash((int)h, attempt, 91, city->seed)) % city->spawnPts.size()];
			int wx = city->cx + sp.x, wy = city->cy + sp.y;
			long long dx = wx - px, dy = wy - py, d2 = dx * dx + dy * dy;
			if (d2 < 30LL * 30LL || d2 > 150LL * 150LL) continue;               // nicht vor der Nase, nicht zu weit
			if (!LabBridge::ZoneAt(gc, wx >> 8, wy >> 8)) continue;
			int64_t fx = (int64_t)wx * 65536 + 32768, fy = (int64_t)wy * 65536 + 32768, fz = (int64_t)(zg + 1 + sp.z) * 65536 + 16384;
			uint64_t g = 0;
			if (!g_elfMobs.empty()) { std::string rep; g = MobLab::SpawnCustomAt(gc, g_elfMobs[(h >> 8) % g_elfMobs.size()], fx, fy, fz, rep); }
			else g = MobLab::SpawnRaceAt(gc, (h >> 9) & 1, 3, -1, fx, fy, fz, false, true, 1.2f);
			if (g) g_elves.push_back(g);
			break;
		}
	}


	// ---- Keine feindlichen Wesen in den Staedten (2026-10-29) ---------------------------------------------------------
	// 1) Zonenerzeugung: Das Spiel legt seine Tiere/Monster als Spawn-Datensaetze an (Zone+0x18/+0x1C vector<Spawn*>,
	//    Basisanzahl Zone+0xA0 = Groesse nach 0x5EDD53). Im Anker Hook_SunCall (0x5EDE84, danach) werfen wir in Zonen mit
	//    Stadt-Auftrag jeden Datensatz mit Feindlichkeit 1 (+0x28), der von sich aus angreift (Flags +0x7A, Rasse +0x2C ->
	//    creature_isPassive 0x444680), innerhalb Stadtradius + 40 hinaus (virtueller Destruktor mit delete, wie MobLab).
	// 2) Im Spiel: was trotzdem auftaucht (wandernde Gruppen World::populateZone, Tierwelt, Nachbarzonen), wird je Sekunde
	//    weit weg gesetzt -- das Spiel entlaesst Wesen ohne Spieler in der Naehe dann selbst (World::update, Abstand).
	typedef void (__fastcall* SpDtor_t)(void* sp, void* edx, int flag);
	struct Circle { float x, y, r; };
	static int ScrubRaw(void* zone, const Circle* c, int nc) {
		int removed = 0;
		__try {
			uint8_t** b = *(uint8_t***)((uint8_t*)zone + 0x18); uint8_t** e = *(uint8_t***)((uint8_t*)zone + 0x1C);
			if (!b || e < b || e - b > 4096) return 0;
			const int base = *(int*)((uint8_t*)zone + 0xA0), n0 = (int)(e - b);
			uint8_t** w = b;
			for (uint8_t** p = b; p < e; ++p) {
				uint8_t* sp = *p; bool drop = false;
				if (sp && *(int*)(sp + 0x28) == 1) {
					const float x = (float)(*(int64_t*)(sp + 0x10) >> 16), y = (float)(*(int64_t*)(sp + 0x18) >> 16);
					bool in = false; for (int i = 0; i < nc && !in; i++) { float dx = x - c[i].x, dy = y - c[i].y; in = dx * dx + dy * dy <= c[i].r * c[i].r; }
					if (in && !MobLab::PassiveOf(*(uint16_t*)(sp + 0x7A), *(int*)(sp + 0x2C))) drop = true;
				}
				if (drop) { ((SpDtor_t)(*(void***)sp)[0])(sp, nullptr, 1); removed++; }
				else *w++ = sp;
			}
			*(uint8_t***)((uint8_t*)zone + 0x1C) = w;
			if (base == n0) *(int*)((uint8_t*)zone + 0xA0) = (int)(w - b);
			else if (base > (int)(w - b)) *(int*)((uint8_t*)zone + 0xA0) = (int)(w - b);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return removed;
	}
	static volatile LONG c_scrubbed = 0, c_guarded = 0;
	int ScrubZoneSpawns(void* zone, int zx, int zy) {
		if (!zone) return 0;
		Site s[9]; int n = ZonePlans(zx, zy, s, 9);
		if (n <= 0) return 0;
		Circle c[9]; for (int i = 0; i < n; i++) c[i] = { (float)s[i].cx, (float)s[i].cy, s[i].radius + 40.0f };
		int r = ScrubRaw(zone, c, n);
		if (r > 0) InterlockedExchangeAdd(&c_scrubbed, r);
		return r;
	}
	static bool GuardMoveRaw(uint8_t* c) {
		__try { *(int64_t*)(c + 0x10) += (int64_t)6000 << 16; *(int64_t*)(c + 0x20) = (int64_t)900 << 16; *(float*)(c + 0x34) = 0; *(float*)(c + 0x38) = 0; *(float*)(c + 0x3C) = 0; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool GuardInfoRaw(uint8_t* c, uint16_t* flags, int* race, int* hos, uint64_t* owner) {
		__try { *flags = *(uint16_t*)(c + 0x7E); *race = *(int*)(c + 0x64); *hos = c[0x60]; *owner = *(uint64_t*)(c + 0x198); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void CityGuardTick(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 1000) return;
		last = now;
		AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (!me.found) return;
		std::vector<Circle> cs;
		{
			Lock l;
			for (auto& kv : g_plans) {
				Plan* p = kv.second; if (p->zg == LONG_MIN) continue;
				double dx = (double)(me.pos.blockX - p->cx), dy = (double)(me.pos.blockY - p->cy);
				if (dx * dx + dy * dy < (double)(p->R + 400) * (p->R + 400)) cs.push_back({ (float)p->cx, (float)p->cy, p->R + 40.0f });
			}
		}
		for (const Circle& c : cs) {
			static MobLab::CreatureInfo buf[256];
			const int rr = (int)c.r;
			int n = MobLab::ListCreaturesNear(gc, (int64_t)c.x, (int64_t)c.y, me.pos.blockZ, rr, 400, buf, 256);
			for (int i = 0; i < n; i++) {
				const MobLab::CreatureInfo& ci = buf[i];
				if (ci.hostility != 1 || ci.hp <= 0.0f) continue;
				const float dx = (float)ci.x - c.x, dy = (float)ci.y - c.y; if (dx * dx + dy * dy > c.r * c.r) continue;
				uint8_t* cr = (uint8_t*)MobLab::CreatureByGuid(gc, ci.guid); if (!cr) continue;
				uint16_t fl; int race, hos; uint64_t own;
				if (!GuardInfoRaw(cr, &fl, &race, &hos, &own) || own != 0) continue;
				if (MobLab::PassiveOf(fl, race)) continue;                               // friedliche Tiere (Huhn, Stadt-Tiere) bleiben
				if (GuardMoveRaw(cr)) InterlockedIncrement(&c_guarded);
			}
		}
		static LONG lastLog = 0;
		if (c_guarded + c_scrubbed - lastLog >= 20) { lastLog = c_guarded + c_scrubbed; std::cout << "[Stadt] Feindliche Wesen ferngehalten: " << c_scrubbed << " Spawns bei der Zonenerzeugung entfernt, " << c_guarded << " im Spiel versetzt." << std::endl; }
	}

	// ---- Tempel am Weltspawn (2026-10-29) --------------------------------------------------------------
	// Weltspawn: World+0x8000F0 (float x, y, z), im World-ctor fest (8396928, 8396928) -- Infinite-Cubes World.cpp:527,
	// im Original 0x5A55F8/0x5A5602 (mov [esi+0x8000F0/F4], 0x4B002080) und 0x593A17 (Neustart). Die Spawnzone bekommt
	// nativ keine Tiere (World::populateZone). Der Elfen-Tempel (Strukturen\Stadt\Tempel_Elfen.cub, OHNE Kugel) steht
	// kSpawnTempleOff Bloecke noerdlich (+y) davon; in seiner Mitte steht der Tempelhueter (Strukturen\Mobs\tempelhueter.mob).
	static const int kSpawnTempleOff = 36;
	static volatile LONG g_stX = LONG_MIN, g_stY = LONG_MIN, g_stZ = LONG_MIN;      // Tempelmitte + Standflaeche (Welt)
	bool SpawnTempleZone(void* gc, void* zone, int zx, int zy, int spawnX, int spawnY, SurfFn surf, int* bx0, int* by0, int* bx1, int* by1) {
		if (!zone || !surf) return false;
		const int cx0 = spawnX, cy0 = spawnY + kSpawnTempleOff;
		if (FloorDiv256(cx0) != zx || FloorDiv256(cy0) != zy) return false;
		EnsureModels(gc);
		const MInfo& m = g_m[M_TEMPLE_E];
		if (!m.src.Valid() || m.sx <= 0 || m.sy <= 0 || m.sx > 240 || m.sy > 240) return false;
		const int X0 = zx * 256, Y0 = zy * 256;
		int ox = std::max(X0 + 2, std::min(cx0 - m.sx / 2, X0 + 254 - m.sx));            // ganz in der Zone
		int oy = std::max(Y0 + 2, std::min(cy0 - m.sy / 2, Y0 + 254 - m.sy));
		int zmax = INT_MIN, n = 0;
		for (int j = 0; j <= 4; j++) for (int i = 0; i <= 4; i++) {                        // 5 x 5 Messpunkte, hoechster zaehlt
			int z; uint8_t t;
			if (surf(zone, ox + (m.sx - 1) * i / 4, oy + (m.sy - 1) * j / 4, &z, &t)) { zmax = std::max(zmax, z); n++; }
		}
		if (n < 5) return false;                                                           // kaum Land (Wasser) -> kein Tempel
		const Piece pc = MakePiece(M_TEMPLE_E, ox + m.sx / 2, oy + m.sy / 2, 0, 1, 0);
		const int bz = zmax + 1;                                                           // Sockel mit Fundament nach unten
		if (StampPiece(m, pc, bz, zone, X0, Y0, false) <= 0) { InterlockedIncrement(&c_fail); return false; }
		int fl = 0; const int mx = m.src.sx / 2, my = m.src.sy / 2;
		for (int z = 0; z < m.src.sz / 2; z++) if (m.src.Solid(mx, my, z)) fl = z;          // Tempelboden in der Mitte
		InterlockedExchange(&g_stX, pc.x + m.sx / 2); InterlockedExchange(&g_stY, pc.y + m.sy / 2);
		InterlockedExchange(&g_stZ, bz + fl + 1);
		if (bx0) { *bx0 = pc.x; *by0 = pc.y; *bx1 = pc.x + pc.w; *by1 = pc.y + pc.d; }
		std::cout << "[Weltspawn] Elfen-Tempel bei (" << g_stX << "," << g_stY << "," << g_stZ << "), Spawn (" << spawnX << "," << spawnY << ")." << std::endl;
		return true;
	}

	// ---- Hochelfenkoenig (2026-10-27) -----------------------------------------------------------------------
	// Steht (2026-10-29) zwischen Thron und Tisch in der Halle oben im Mutterbaum (CityModels::MotherKingStand). Mob: Strukturen\Mobs\
	// hochelfenkoenig.mob (Kopf = Strukturen\elfking.cub), Sprechblase: dort die Zeilen "sprechblase=".
	// Sitzen wie auf einem nativen Hocker (Infinite-Cubes WorldUpdate.cpp:655: Position = Sitzflaeche + halbe Hoehe,
	// Geschwindigkeit 0, rotation.z = Blickrichtung, currentAbility = AbilitySit 83) -- je Bild neu gesetzt.
	// Eigener Text: Spricht man ihn an, belegt das Spiel eine Sprechblase (GameController::talkTo; Vektor
	// m_speechWidgets GC+0x80092C, SpeechWidget-ctor 0x4E5C90: m_dialogs +0x160/+0x164, m_readTime +0x16C,
	// m_dialogIndex +0x170, m_textUncovered +0x174, m_creature +0x188; Dialog = std::list<Phrase> (8 Bytes: Kopf,
	// Anzahl, SpeechWidget::draw 0x4E5FC2: Index*8), Listenknoten: next +0, prev +4, Phrase +8 (std::wstring text,
	// Farbe)). Diese Seiten werden durch unseren Text ersetzt; dabei wird der Teleport freigeschaltet.
	static uint64_t g_king = 0; static DWORD g_kingTry = 0; static MobLab::CustomMob g_kingMob; static bool g_kingLoaded = false;
	static std::vector<std::wstring> g_kingText, g_kingTextAfter;
	typedef void(__fastcall* KWCtor_t)(void* ws, void* edx, const wchar_t* s);
	typedef void(__fastcall* KWDtor_t)(void* ws, void* edx);

	// Seiten "<key>Text" aus einer .mob lesen (UTF-8, eine Zeile = eine Seite)
	static void ReadSpeech(const std::string& path, const char* key, std::vector<std::wstring>& out) {
		out.clear(); const size_t kl = strlen(key);
		std::ifstream f(path); std::string l;
		while (std::getline(f, l)) {
			if (!l.empty() && l.back() == '\r') l.pop_back();
			if (l.compare(0, kl, key) != 0) continue;
			std::string t = l.substr(kl); wchar_t w[512];
			int n = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, w, 512);
			if (n <= 0) { int k = 0; for (; k < 511 && k < (int)t.size(); k++) w[k] = (unsigned char)t[k]; w[k] = 0; }
			if (w[0]) out.push_back(w);
		}
	}
	static void LoadKing() {
		if (g_kingLoaded) return;
		g_kingLoaded = true;
		std::string path = MobLab::MobsDir() + "\\hochelfenkoenig.mob", err;
		if (!MobLab::LoadMob(path, g_kingMob, err)) { std::cout << "[Stadt] Hochelfenkoenig: " << err << std::endl; g_kingMob = MobLab::CustomMob(); g_kingMob.name.clear(); return; }
		g_kingMob.hostility = 3; g_kingMob.wander = false; // 3 = Friendly NPC (ansprechbar)
		ReadSpeech(path, "sprechblase=", g_kingText);
		ReadSpeech(path, "sprechblase_danach=", g_kingTextAfter);
		std::cout << "[Stadt] Hochelfenkoenig geladen (" << g_kingText.size() << " Seiten erstes Gespraech, " << g_kingTextAfter.size() << " danach)." << std::endl;
	}

	static bool KingHoldRaw(uint8_t* c, int64_t x, int64_t y, int64_t floorZ, float yaw) {
		__try {
			float hgt = *(float*)(c + 0x88);
			if (hgt < 1.0f || hgt > 10.0f) hgt = 2.2f;
			*(int64_t*)(c + 0x10) = x;
			*(int64_t*)(c + 0x18) = y;
			*(int64_t*)(c + 0x20) = floorZ + (int64_t)(hgt * 0.5f * 65536.0f) + 16384;
			*(float*)(c + 0x34) = 0; *(float*)(c + 0x38) = 0; *(float*)(c + 0x3C) = 0;
			*(float*)(c + 0x30) = yaw;
			c[0x5C] |= 1;                                      // Physics-Flag: auf dem Boden (verhindert Fall-Animation)
			c[0x60] = 3;                                       // Hostility = 3 (Friendly NPC, zwingend fuer talkTo)
			if (*(int*)(c + 0x64) < 0 || *(int*)(c + 0x64) > 16) *(int*)(c + 0x64) = 0; // Humanoide Rasse (Elf) sichern
			if (c[0x68] == 83) c[0x68] = 0;                    // Stehen (Idle = 0 statt AbilitySit = 83)
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static void SetWs(uint8_t* ws, const wchar_t* t) { ((KWDtor_t)ReA(0x593E50))(ws, nullptr); ((KWCtor_t)ReA(0x40EB60))(ws, nullptr, t); }

	// Sprechblase des Koenigs suchen und seine Seiten einsetzen. Rueckgabe true = gerade ersetzt (= angesprochen).
	// std::wstring (MSVC, ctor 0x40EB60): Puffer/Zeiger +0, Laenge +0x10, Kapazitaet +0x14 (> 7 -> Zeiger)
	static const wchar_t* WsPtr(uint8_t* ws) { return *(uint32_t*)(ws + 0x14) > 7 ? *(const wchar_t**)ws : (const wchar_t*)ws; }
	// Umbruch (2026-10-29): SpeechWidget::drawPhrase (Infinite-Cubes widgets/SpeechWidget.cpp) bricht nur ZWISCHEN
	// Phrasen um -- das Spiel legt je Wort eine Phrase an. Also: jede Seite in Woerter zerlegen, ein Listenknoten je Wort.
	// Knoten std::list<Phrase> = 0x30 Bytes (next +0, prev +4, Text-wstring +8, Farbe +0x20; _Buynode 0x46D850:
	// push 0x30, operator new), angelegt mit dem operator new DES SPIELS (Thunk 0x68D668 -> MSVCR110 ??2, delete 0x68D66E),
	// damit das Spiel sie spaeter selbst freigeben kann. Seiten: std::vector<Dialog> +0x160 first, +0x164 last, +0x168 end
	// (SpeechWidget-ctor 0x4E5D2C..0x4E5D40), Dialog = { Kopf, Anzahl } (8 Bytes). Zu lange Seiten teilt NpcSpeech auf.
	typedef void* (__cdecl* GNew_t)(size_t);
	typedef void (__cdecl* GDel_t)(void*);
	static const int kPageChars = 92;                       // ~4 Zeilen zu 230 - 30 px bei Schrift 14
	static bool SameW(const wchar_t* a, const std::wstring* b) { return b && !b->empty() && wcscmp(a, b->c_str()) == 0; }
	// Sprechblase der Kreatur suchen; nullptr, wenn keine offen ist oder ihre ersten zwei Woerter schon unsere sind
	// (Vergleich am Inhalt: ein neuer Dialog kann denselben Speicher bekommen).
	static uint8_t* SpeechFindRaw(uint8_t* G, uint8_t* npc, const std::wstring* a1, const std::wstring* a2, const std::wstring* b1, const std::wstring* b2) {
		__try {
			uint8_t** wb = *(uint8_t***)(G + 0x80092C); uint8_t** we = *(uint8_t***)(G + 0x800930);
			if (!wb || we < wb || we - wb > 16) return nullptr;
			for (uint8_t** it = wb; it < we; ++it) {
				uint8_t* w = *it; if (!w || *(uint8_t**)(w + 0x188) != npc) continue;
				uint8_t* b = *(uint8_t**)(w + 0x160); uint8_t* e = *(uint8_t**)(w + 0x164);
				if (!b || e <= b) continue;
				uint8_t* head = *(uint8_t**)b; uint8_t* n1 = head ? *(uint8_t**)head : nullptr;
				if (n1 && n1 != head) {
					uint8_t* n2 = *(uint8_t**)n1; const wchar_t* t1 = WsPtr(n1 + 8);
					const wchar_t* t2 = (n2 && n2 != head) ? WsPtr(n2 + 8) : L"";
					if ((SameW(t1, a1) && (!a2 || a2->empty() || SameW(t2, a2))) || (SameW(t1, b1) && (!b2 || b2->empty() || SameW(t2, b2)))) return nullptr;
				}
				return w;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}
	static uint8_t* NewNodeRaw(const wchar_t* t, const uint8_t* color) {
		uint8_t* nd = (uint8_t*)((GNew_t)ReA(0x68D668))(0x30); if (!nd) return nullptr;
		memset(nd, 0, 0x30);
		((KWCtor_t)ReA(0x40EB60))(nd + 8, nullptr, t);
		memcpy(nd + 0x20, color, 16);
		return nd;
	}
	static void LinkBackRaw(uint8_t* dlg, uint8_t* nd) {                  // ans Ende der Liste, Anzahl + 1
		uint8_t* head = *(uint8_t**)dlg; uint8_t* last = *(uint8_t**)(head + 4);
		*(uint8_t**)nd = head; *(uint8_t**)(nd + 4) = last; *(uint8_t**)last = nd; *(uint8_t**)(head + 4) = nd;
		*(uint32_t*)(dlg + 4) += 1;
	}
	// words: alle Woerter hintereinander, start[p] = erstes Wort der Seite p, start[P] = Ende
	static bool SpeechWriteRaw(uint8_t* w, const wchar_t* const* words, const int* start, int P) {
		__try {
			uint8_t* b = *(uint8_t**)(w + 0x160); uint8_t* e = *(uint8_t**)(w + 0x164);
			int n = (int)((e - b) / 8); if (n <= 0 || P <= 0) return false;
			uint8_t color[16]; const float white[4] = { 1, 1, 1, 1 }; memcpy(color, white, 16);
			{ uint8_t* h = *(uint8_t**)b; uint8_t* f = h ? *(uint8_t**)h : nullptr; if (f && f != h) memcpy(color, f + 0x20, 16); }
			if (P > n) {                                                        // mehr Seiten als das Spiel: Vektor neu (Spiel-Speicher)
				uint8_t* nb = (uint8_t*)((GNew_t)ReA(0x68D668))((size_t)P * 8); if (!nb) return false;
				memcpy(nb, b, (size_t)n * 8);
				for (int i = n; i < P; i++) {
					uint8_t* s = (uint8_t*)((GNew_t)ReA(0x68D668))(0x30); if (!s) return false;
					memset(s, 0, 0x30); *(uint8_t**)s = s; *(uint8_t**)(s + 4) = s;   // leerer Kopf (zeigt auf sich)
					*(uint8_t**)(nb + i * 8) = s; *(uint32_t*)(nb + i * 8 + 4) = 0;
				}
				((GDel_t)ReA(0x68D66E))(b);
				b = nb; *(uint8_t**)(w + 0x160) = nb; *(uint8_t**)(w + 0x164) = nb + P * 8; *(uint8_t**)(w + 0x168) = nb + P * 8;
			} else if (P < n) *(uint8_t**)(w + 0x164) = b + P * 8;               // weitere Seiten des Spiels weg (Speicher bleibt liegen)
			for (int p = 0; p < P; p++) {
				uint8_t* dlg = b + p * 8; uint8_t* head = *(uint8_t**)dlg; if (!head) continue;
				int k = start[p];
				for (uint8_t* nd = *(uint8_t**)head; nd && nd != head; nd = *(uint8_t**)nd)   // vorhandene Knoten belegen, Rest leer
					SetWs(nd + 8, k < start[p + 1] ? words[k++] : L"");
				for (; k < start[p + 1]; k++) { uint8_t* nd = NewNodeRaw(words[k], color); if (!nd) break; LinkBackRaw(dlg, nd); }
			}
			*(int*)(w + 0x16C) = 0; *(int*)(w + 0x170) = 0; *(int*)(w + 0x174) = 0;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Text in Seiten (hoechstens kPageChars Zeichen) und Woerter zerlegen
	static void SplitPages(const std::vector<std::wstring>& text, std::vector<std::wstring>& words, std::vector<int>& start) {
		words.clear(); start.clear();
		for (const std::wstring& pg : text) {
			std::vector<std::wstring> ws; std::wstring cur;
			for (wchar_t ch : pg) { if (ch == L' ' || ch == L'\t') { if (!cur.empty()) ws.push_back(cur); cur.clear(); } else cur += ch; }
			if (!cur.empty()) ws.push_back(cur);
			if (ws.empty()) continue;
			int len = 0; start.push_back((int)words.size());
			for (const std::wstring& x : ws) {
				if (len > 0 && len + 1 + (int)x.size() > kPageChars) { start.push_back((int)words.size()); len = 0; }
				words.push_back(x); len += (len ? 1 : 0) + (int)x.size();
			}
		}
		start.push_back((int)words.size());
	}
	static void FirstTwo(const std::vector<std::wstring>& text, std::wstring& a, std::wstring& b) {
		std::vector<std::wstring> w; std::vector<int> st; SplitPages(text, w, st);
		a = w.size() > 0 ? w[0] : L""; b = w.size() > 1 ? w[1] : L"";
	}
	// Seiten einsetzen; true = gerade ersetzt (= eben angesprochen). other = zweiter eigener Text derselben Figur.
	static bool NpcSpeech(uint8_t* G, uint8_t* npc, const std::vector<std::wstring>& text, const std::vector<std::wstring>* other) {
		if (text.empty()) return false;
		std::wstring a1, a2, b1, b2; FirstTwo(text, a1, a2); if (other) FirstTwo(*other, b1, b2);
		uint8_t* w = SpeechFindRaw(G, npc, &a1, &a2, other ? &b1 : nullptr, other ? &b2 : nullptr);
		if (!w) return false;
		std::vector<std::wstring> words; std::vector<int> start; SplitPages(text, words, start);
		const int P = (int)start.size() - 1; if (P <= 0) return false;
		std::vector<const wchar_t*> ptr; for (auto& x : words) ptr.push_back(x.c_str());
		return SpeechWriteRaw(w, ptr.data(), start.data(), P);
	}

	// ---- Zum Hochelfenkoenig (2026-10-29) --------------------------------------------------------------------------
	// Ausgeloest durch die Kugel eines kleinen Tempels ab dem 25. Fund (Temples.cpp) und die Stadttempel-Kugel.
	// 1) Suche (eigener Thread, die Elfenstadt kann weit weg sein -- Elfenbiome z. B. nur 1 %): naechste Elfenstadt aus
	//    dem Seed (BiomeMod::NearestElfCityFrom, Ringsuche ueber die ganze Welt; Ergebnis je Seed+Startzelle gemerkt).
	// 2) Sprung hoch neben den Mutterbaum (90 Bloecke, Hoehe >= 450), damit das Spiel die Stadt-Zonen erzeugt.
	// 3) Sobald der Stadtplan die Bodenhoehe kennt und die Zone des Koenigs geladen ist: in den Thronsaal auf den Laeufer,
	//    4.5 Bloecke vor den Koenig (Standflaeche wie MotherKingStand: zg + z + 1). Spaetestens nach 120 s Ende.
	static volatile LONG g_visitState = 0;            // 0 aus, 1 Suche laeuft, 2 Suche fertig, 3 wartet auf die Stadt
	static volatile LONG g_visitFound = 0; static int g_visitX = 0, g_visitY = 0, g_visitPX = 0, g_visitPY = 0; static uint32_t g_visitBase = 0;
	static DWORD g_visitAt = 0;
	static bool SetPlayerRaw(cube::GameController* gc, int64_t x, int64_t y, int64_t z) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false;
			*(int64_t*)(c + 0x10) = x; *(int64_t*)(c + 0x18) = y; *(int64_t*)(c + 0x20) = z;
			*(float*)(c + 0x34) = 0; *(float*)(c + 0x38) = 0; *(float*)(c + 0x3C) = 0;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static DWORD WINAPI VisitSearch(LPVOID) {
		int cx = 0, cy = 0;
		bool ok = BiomeMod::NearestElfCityFrom(g_visitBase, g_visitPX, g_visitPY, 1023, &cx, &cy);
		g_visitX = cx; g_visitY = cy; InterlockedExchange(&g_visitFound, ok ? 1 : 0);
		InterlockedExchange(&g_visitState, 2);
		return 0;
	}
	bool VisitKing(cube::GameController* gc, std::string& msg) {
		if (!gc || !gc->local_player) { msg = "kein Spieler"; return false; }
		if (g_visitState != 0) { msg = "Der Weg zum Hochelfenkoenig ist schon offen."; return true; }
		AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (!me.found) { msg = "kein Spieler"; return false; }
		g_visitBase = BiomeMod::WorldBase(gc); g_visitPX = (int)me.pos.blockX; g_visitPY = (int)me.pos.blockY; g_visitAt = GetTickCount();
		InterlockedExchange(&g_visitState, 1);
		HANDLE t = CreateThread(NULL, 0, VisitSearch, NULL, 0, NULL);
		if (!t) { InterlockedExchange(&g_visitState, 0); msg = "Suche nach der Elfenstadt nicht startbar."; return false; }
		CloseHandle(t);
		msg = "Die Kugeln rufen dich zum Hochelfenkoenig ...";
		std::cout << "[Stadt] Weg zum Koenig: Suche der naechsten Elfenstadt gestartet (Spieler " << g_visitPX << ", " << g_visitPY << ")." << std::endl;
		return true;
	}
	static void VisitTick(cube::GameController* gc) {
		const LONG st = g_visitState;
		if (st == 0 || st == 1) return;
		const int64_t U = 65536;
		if (st == 2) {                                                               // Suche fertig -> erster Sprung
			if (!g_visitFound) {
				InterlockedExchange(&g_visitState, 0);
				CityChat(gc, L"Keine Elfenstadt in dieser Welt gefunden (Biom mit Elfenstadt aktiv?).");
				std::cout << "[Stadt] Weg zum Koenig: keine Elfenstadt gefunden." << std::endl;
				return;
			}
			int64_t z = 450; { AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (me.found && me.pos.blockZ > z) z = me.pos.blockZ; }
			SetPlayerRaw(gc, (int64_t)(g_visitX + 90) * U, (int64_t)g_visitY * U, z * U);
			g_visitAt = GetTickCount(); InterlockedExchange(&g_visitState, 3);
			std::cout << "[Stadt] Weg zum Koenig: Elfenstadt bei (" << g_visitX << ", " << g_visitY << "), warte auf die Stadt." << std::endl;
			return;
		}
		if (GetTickCount() - g_visitAt > 120000) {
			InterlockedExchange(&g_visitState, 0);
			CityChat(gc, L"Die Elfenstadt laedt nicht -- folge dem Mutterbaum zu Fuss.");
			std::cout << "[Stadt] Weg zum Koenig: Stadt nach 120 s nicht geladen." << std::endl;
			return;
		}
		long zg = LONG_MIN;
		{ Lock l; for (auto& kv : g_plans) { Plan* p = kv.second; if (p->kind == ELF && p->cx == g_visitX && p->cy == g_visitY) { zg = p->zg; break; } } }
		if (zg == LONG_MIN) return;
		float sx, sy, yaw; int sz; CityModels::MotherKingStand(&sx, &sy, &sz, &yaw);
		const float mid = 58.5f, f = 2.0f / 6.5f;                                    // Koenig r = 6.5 -> Spieler r = 2.0 (Laeufer)
		const float lx = mid + (sx - mid) * f, ly = mid + (sy - mid) * f;
		const int64_t wx = (int64_t)((g_visitX - 58 + lx) * U), wy = (int64_t)((g_visitY - 58 + ly) * U);
		if (!LabBridge::ZoneAt(gc, (int)((wx / U) >> 8), (int)((wy / U) >> 8))) return;
		const int64_t floorZ = (int64_t)(zg + sz) * U + U;                            // Oberkante Laeufer/Podest (Ht+1)
		SetPlayerRaw(gc, wx, wy, floorZ + 2 * U);
		InterlockedExchange(&g_visitState, 0);
		Effects::Burst(gc, (float)wx / U, (float)wy / U, (float)floorZ / U + 1.0f, 0.95f, 0.85f, 0.45f, 40);
		std::cout << "[Stadt] Im Thronsaal vor dem Hochelfenkoenig angekommen." << std::endl;
	}
	static void KingTick(cube::GameController* gc) {
		int64_t px, py, pz;
		{ AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (!me.found) return; px = me.pos.blockX; py = me.pos.blockY; pz = me.pos.blockZ; }
		(void)pz;
		int64_t cx = 0, cy = 0; long zg = LONG_MIN;
		{
			Lock l; double best = 1e18;
			for (auto& kv : g_plans) {
				Plan* p = kv.second; if (p->kind != ELF || p->zg == LONG_MIN) continue;
				double dx = (double)(px - p->cx), dy = (double)(py - p->cy), d = dx * dx + dy * dy;
				if (d < best && d < 260.0 * 260.0) { best = d; cx = p->cx; cy = p->cy; zg = p->zg; }
			}
		}
		if (zg == LONG_MIN) return;
		float sx, sy, yaw; int sz; CityModels::MotherKingStand(&sx, &sy, &sz, &yaw);   // steht am Tisch vor dem Thron
		// Modell-Ursprung: Mitte - 58 (MakePiece: cx - w/2, w = 117), Hoehe zg + 1 (wie der Aussichtsbalkon)
		const int64_t U = 65536;
		const int64_t wx = (int64_t)((cx - 58 + sx) * U), wy = (int64_t)((cy - 58 + sy) * U), wz = (int64_t)(zg  + sz) * U;
		LoadKing();
		if (g_kingMob.name.empty()) return;
		uint8_t* c = g_king ? (uint8_t*)MobLab::CreatureByGuid(gc, g_king) : nullptr;
		if (!c) {
			g_king = 0; DWORD now = GetTickCount();
			if (now - g_kingTry < 3000 || !LabBridge::ZoneAt(gc, (int)((wx / U) >> 8), (int)((wy / U) >> 8))) return;
			g_kingTry = now;
			std::string rep; g_king = MobLab::SpawnCustomAt(gc, g_kingMob, wx, wy, wz + U + 16384, rep);
			if (g_king) std::cout << "[Stadt] Hochelfenkoenig am Tisch vor dem Thron (" << (wx / U) << "," << (wy / U) << "," << (wz / U) << ")." << std::endl;
			return;
		}
		KingHoldRaw(c, wx, wy, wz + U, yaw); // wz + U = Oberkante des Podests
		// Erstes Gespraech: Begruessung + Teleport; danach nur noch "sprechblase_danach="
		const bool known = Skills::IsUnlockedByNpc(gc, "hochelfenkoenig");
		const std::vector<std::wstring>* txt = (known && !g_kingTextAfter.empty()) ? &g_kingTextAfter : &g_kingText;
		const std::vector<std::wstring>* oth = (txt == &g_kingText) ? &g_kingTextAfter : &g_kingText;
		if (NpcSpeech((uint8_t*)gc, c, *txt, oth) && !known) {
			Skills::UnlockByNpc(gc, "hochelfenkoenig");
			Effects::Burst(gc, (float)wx / U, (float)wy / U, (float)wz / U + 2.0f, 0.95f, 0.85f, 0.45f, 40);   // goldener Funkenregen
		}
	}

	// Tempelhueter am Weltspawn: steht in der Tempelmitte, erzaehlt die Geschichte des Landes (sprechblase= der .mob)
	static uint64_t g_keeper = 0; static DWORD g_keeperTry = 0; static MobLab::CustomMob g_keeperMob; static bool g_keeperLoaded = false;
	static std::vector<std::wstring> g_keeperText;
	static void LoadKeeper() {
		if (g_keeperLoaded) return;
		g_keeperLoaded = true;
		std::string path = MobLab::MobsDir() + "\\tempelhueter.mob", err;
		if (!MobLab::LoadMob(path, g_keeperMob, err)) { std::cout << "[Weltspawn] Tempelhueter: " << err << std::endl; g_keeperMob = MobLab::CustomMob(); g_keeperMob.name.clear(); return; }
		g_keeperMob.hostility = 3; g_keeperMob.wander = false;
		ReadSpeech(path, "sprechblase=", g_keeperText);
		std::cout << "[Weltspawn] Tempelhueter geladen (" << g_keeperText.size() << " Seiten Text)." << std::endl;
	}
	static void KeeperTick(cube::GameController* gc) {
		const LONG tx = g_stX, ty = g_stY, tz = g_stZ;
		if (tz == LONG_MIN) return;
		int64_t px, py;
		{ AdventureMod::TrackResult me = AdventureMod::TrackPlayer(gc); if (!me.found) return; px = me.pos.blockX; py = me.pos.blockY; }
		if (std::llabs(px - tx) > 300 || std::llabs(py - ty) > 300) return;
		LoadKeeper();
		if (g_keeperMob.name.empty()) return;
		const int64_t U = 65536;
		const int64_t wx = (int64_t)tx * U + U / 2, wy = (int64_t)ty * U + U / 2, floorZ = (int64_t)tz * U;
		const float yaw = 180.0f;                                                          // Blick nach -y = zum Weltspawn
		uint8_t* c = g_keeper ? (uint8_t*)MobLab::CreatureByGuid(gc, g_keeper) : nullptr;
		if (!c) {
			g_keeper = 0; DWORD now = GetTickCount();
			if (now - g_keeperTry < 3000 || !LabBridge::ZoneAt(gc, (int)(tx >> 8), (int)(ty >> 8))) return;
			g_keeperTry = now;
			std::string rep; g_keeper = MobLab::SpawnCustomAt(gc, g_keeperMob, wx, wy, floorZ + U + 16384, rep);
			if (g_keeper) std::cout << "[Weltspawn] Tempelhueter in der Tempelmitte (" << tx << "," << ty << "," << tz << ")." << std::endl;
			return;
		}
		KingHoldRaw(c, wx, wy, floorZ, yaw);
		NpcSpeech((uint8_t*)gc, c, g_keeperText, nullptr);
	}
	static void KingSEH(cube::GameController* gc) { __try { CityGuardTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {} __try { VisitTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {} __try { KingTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {} __try { KeeperTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	// <SEH>
	static void PopulateSEH(cube::GameController* gc) { __try { PopulateTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	// </SEH>
	void Tick(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
std::vector<Land> work;
		{ Lock l; work.swap(g_landPending); }
		for (const Land& d : work) {
			bool known = false;
			{ Lock l; for (const Land& k : g_landKnown) if (k.npcX == d.npcX && k.npcY == d.npcY) known = true; }
			if (AirshipTaxi::AddLandingPost(gc, d.cx, d.cy, d.npcX, d.npcY, d.npcZ, d.dockX, d.dockY, d.dockZ, d.race) || known) {
				if (!known) {
					Lock l; g_landKnown.push_back(d);
					std::cout << "[Stadt] Landeplatz bei (" << d.npcX << "," << d.npcY << "," << d.npcZ << ") beim Luftschiff-Taxi angemeldet." << std::endl;
				}
			}
		}
		PopulateSEH(gc);                                                  // Haendlerposten + Bewohner NACH den Landeplaetzen, eigene Absturzhuelle
		KingSEH(gc);                                                      // Hochelfenkoenig auf dem Thron (je Bild)
	}

	int ExportModels(std::string& report) {
		Lab::EnsureDir(Lab::StructuresDir());
		std::string dir = Lab::StructuresDir() + "\\Stadt";
		Lab::EnsureDir(dir);
		std::vector<Lab::Structure> v;
		for (int i = 0; i < 4; i++) v.push_back(CityModels::Treehouse(i, kModelSeed));
		v.push_back(CityModels::MotherTree(kModelSeed));
		for (int i = 0; i < 4; i++) v.push_back(CityModels::DwarfHouse(i, kModelSeed));
		v.push_back(CityModels::Fortress(kModelSeed));
		EnsureTempleFiles();                                                      // legt Tempel_Elfen/_Zwerge/_Kugel an, falls sie fehlen
		int n = 0; std::ostringstream o;
		for (const Lab::Structure& s : v) {
			std::string err;
			if (Lab::SaveCub(dir + "\\" + s.name + ".cub", s, err)) n++; else o << err << "; ";
		}
		for (int st = 0; st < 2; st++) {                                          // Viertel-Gebaeude (Tempel/Kugel: LoadTemples)
			for (int k = 0; k < 4; k++) v.push_back(CityModels::MarketHut(st, k, kModelSeed));
			v.push_back(CityModels::AlchemyHouse(st, kModelSeed));
		}
		for (int i = 0; i < CityModels::kChestnutVariants; i++) {               // Kastanien: nur anlegen, nie ueberschreiben
			std::string path = dir + "\\" + CityModels::ChestnutName(i) + ".cub", err;
			if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
			if (Lab::SaveCub(path, CityModels::Chestnut(i, kModelSeed), err)) n++; else o << err << "; ";
		}
		report = std::to_string(n) + " Modelle nach " + dir + (o.str().empty() ? "" : " (Fehler: " + o.str() + ")");
		return n;
	}

	std::string Status() {
		size_t plans, lands, dists = 0; { Lock l; plans = g_plans.size(); lands = g_landKnown.size(); for (auto& kv : g_plans) dists += kv.second->dists.size(); }
		char b[700];
		sprintf_s(b, "Staedte: %d geplant, Bauauftraege fuer %ld Zonen, %ld Zonen bebaut, %ld Bauwerke, %ld Laternen (Wege), %ld Zaeune, %ld Fehler, %ld Abbrueche, %ld ohne Bodenhoehe, %d Landeplaetze am Taxi. "
												          "Pruefung: %ld Bauwerksteile geprueft, %ld mit Luecken -> %ld nachgesetzt ok, %ld weiterhin fehlerhaft; %ld Teile an Zonengrenzen zugeschnitten",
													(int)plans, c_orderZones, c_zones, c_pieces, c_lanterns, c_fences, c_fail, c_abort, c_noGround, (int)lands, c_chkPieces, c_chkBad, c_chkFixed, c_chkStill, c_cropped);
		std::string r = b;
		int cross = 0, fields = 0, pens = 0, gardens = 0;                                      // Bauwerke ueber einer Zonengrenze (ohne Mutterbaum/Festung)
		{ Lock l; for (auto& kv : g_plans) { for (const Piece& pc : kv.second->pieces) if (pc.id != M_MOTHER && pc.id != M_FORT && !FitsZone(pc)) cross++; fields += (int)kv.second->fields.size(); pens += (int)kv.second->pens.size(); gardens += (int)kv.second->gardens.size(); } }
		char v2[600]; sprintf_s(v2, "\r\nViertel: %d Gebiete/Hoefe, %d Felder, %d Gehege, %d Gaerten, %ld native Pflanzen-Props, %ld native Objekte (Lagerfeuer/Braustand, Marktstaende, Faesser, Laternen), %ld Haendler/Alchemisten, %ld Tiere, %ld Segen der Kugel; "
		                       "%d Bauwerke ueber einer Zonengrenze (ohne Mutterbaum/Festung)",
		                       (int)dists, fields, pens, gardens, c_props, c_objs, c_vendors, c_animals, c_bless, cross);
		r += v2;
		{
			Lock l; int shown = 0;
			for (auto& kv : g_plans) {
				if (kv.second->dists.empty() || shown >= 3) continue;
				shown++;
				std::ostringstream o; o << "\r\n  " << KindName(kv.second->kind) << " (" << kv.second->cx << "," << kv.second->cy << "):";
				for (const EDist& d : kv.second->dists) o << " " << DistrictName(d.kind) << " (" << kv.second->cx + Rnd(d.x) << "," << kv.second->cy + Rnd(d.y) << ")";
				o << "\r\n    Haendlerposten:";
				for (const NpcP& n : kv.second->npcs) if (n.kind == NP_VENDOR || n.kind == NP_MERCHANT || n.kind == NP_ALCH)
					o << " " << (n.kind == NP_MERCHANT ? "Markt" : (n.kind == NP_ALCH ? "Alchemist" : "Haendler")) << "(" << kv.second->cx + n.x << "," << kv.second->cy + n.y << ")";
				r += o.str();
			}
		}
		return r;
	}
}
