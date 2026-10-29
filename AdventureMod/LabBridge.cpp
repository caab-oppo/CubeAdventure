// LabBridge.cpp -- Bruecke LAB-Fenster <-> Spiel. Siehe LabBridge.h.
//
// Aufbau der Datei:
//   1. Sperre, Protokoll (cout-Abzweig), Auftragswarteschlange
//   2. Kleine SEH-Funktionen fuer jeden nativen Zugriff (nur POD im Rahmen, C2712)
//   3. Import (Registry, Welt), Export
//   4. Setzen ueber placeStructure mit Schattenmodell, Rueckgaengig
//   5. Platzierungsliste je Welt + Neu-Stempeln nach OnZoneGenerated
//
// Quellen fuer alle Adressen/Layouts: RE_EigeneStrukturen.md (Subagent),
// RE_placeStructure.md, RE_Ortsindex.md.
#include "LabBridge.h"
#include "Host.h"
#include "LabModelNames.h"
#include "MountainVillage.h"
#include "NativeHouseProbe.h"
#include "CrashLogger.h"
#include "City.h"
#include "WanderingMerchant.h"

#include <windows.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <deque>
#include <map>
#include <memory>
#include <cstring>
#include <cstdio>
#include <climits>
#include <algorithm>

namespace LabBridge {

	void AddPlacementInternal(const Placement& p);   // Abschnitt 5

	// =====================================================================
	// 1. Sperre, Protokoll, Warteschlange
	// =====================================================================
	namespace {
		struct Lock {
			CRITICAL_SECTION cs;
			Lock()  { InitializeCriticalSection(&cs); }
			~Lock() { DeleteCriticalSection(&cs); }
		};
		Lock& L() { static Lock l; return l; }
		struct Guard {
			Guard()  { EnterCriticalSection(&L().cs); }
			~Guard() { LeaveCriticalSection(&L().cs); }
		};

		std::string g_log;                                     // noch nicht abgeholter Protokolltext
		struct QueuedJob { std::string label; Job fn; };
		std::deque<QueuedJob> g_jobs;
		std::deque<Lab::Structure> g_structOut;
		std::vector<LibEntry> g_libOut; bool g_libReady = false;
		Snapshot g_snap;
		bool g_flagTracking = false, g_flagOcean = false;

		void AppendLog(const char* s, size_t n) {
			CrashLogger::WriteLog(s, n);             // sofort in AdventureMod_log.txt (ueberlebt einen Absturz)
			Guard g;
			g_log.append(s, n);
			// Obergrenze: holt das Fenster lange nichts ab, alte Haelfte verwerfen.
			if (g_log.size() > 512 * 1024) g_log.erase(0, g_log.size() - 256 * 1024);
		}

		// Abzweig fuer std::cout: alles geht weiter an die Konsole UND ins LAB.
		class TeeBuf : public std::streambuf {
		public:
			explicit TeeBuf(std::streambuf* orig) : m_orig(orig) {}
		protected:
			int overflow(int c) override {
				if (c == traits_type::eof()) return traits_type::not_eof(c);
				char ch = (char)c;
				AppendLog(&ch, 1);
				if (m_orig) m_orig->sputc(ch);
				return c;                                   // Ergebnis der Konsole egal: ohne Konsole (Veroeffentlichung)
			}                                               // wuerde cout sonst auf "fehlerhaft" kippen und LAB/Logdatei verstummen
			std::streamsize xsputn(const char* s, std::streamsize n) override {
				AppendLog(s, (size_t)n);
				if (m_orig) m_orig->sputn(s, n);
				return n;
			}
			int sync() override { if (m_orig) m_orig->pubsync(); return 0; }
		private:
			std::streambuf* m_orig;
		};
	}

	void InstallLogTee() {
		static bool done = false;
		if (done) return;
		done = true;
		static TeeBuf tee(std::cout.rdbuf());
		std::cout.rdbuf(&tee);
	}

	void Log(const std::string& line) {
		std::cout << "[LAB] " << line << std::endl;
	}

	std::string TakeLog() {
		Guard g; std::string s; s.swap(g_log); return s;
	}

	void Post(const std::string& label, Job job) {
		Guard g;
		g_jobs.push_back({ label, job });
	}

	int PendingJobs() { Guard g; return (int)g_jobs.size(); }

	// SEH um einen Auftrag. Rahmen enthaelt nur Zeiger (C2712). Faengt auch
	// C++-Ausnahmen (0xE06D7363) -- ein kaputter Auftrag kostet nur sich selbst.
	static void CallJob(Job* j, cube::GameController* gc) { (*j)(gc); }
	static bool RunJobSEH(Job* j, cube::GameController* gc) {
		__try { CallJob(j, gc); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	void PostStructure(const Lab::Structure& s) { Guard g; g_structOut.push_back(s); }
	bool TakeStructure(Lab::Structure& out) {
		Guard g; if (g_structOut.empty()) return false;
		out = g_structOut.front(); g_structOut.pop_front(); return true;
	}
	bool TakeNativeLibrary(std::vector<LibEntry>& out) {
		Guard g; if (!g_libReady) return false;
		out.swap(g_libOut); g_libOut.clear(); g_libReady = false; return true;
	}
	Snapshot GetSnapshot() { Guard g; return g_snap; }
	void SetOverlayFlags(bool tracking, bool oceanGen) { Guard g; g_flagTracking = tracking; g_flagOcean = oceanGen; }

	// =====================================================================
	// 2. SEH-Kleinfunktionen (nur POD im Rahmen)
	// =====================================================================
	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	static const uint32_t kFnPlaceStructure = 0x005F0CE0;   // __stdcall, 9 Args
	static const uint32_t kDatEmptyColor    = 0x0076B340;   // 3 Byte Leerfarbe der Modelle

	static int FloorDiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

	static bool ReadEmptyColor(uint8_t* c) {
		__try { const uint8_t* p = (const uint8_t*)Re(kDatEmptyColor); c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { c[0] = c[1] = c[2] = 0; return false; }
	}

	static bool ReadModelDims(void* m, int* sx, int* sy, int* sz) {
		__try {
			if (!m) return false;
			*sx = *(int*)((uint8_t*)m + 0x44);
			*sy = *(int*)((uint8_t*)m + 0x48);
			*sz = *(int*)((uint8_t*)m + 0x4C);
			return *sx > 0 && *sy > 0 && *sz > 0 && *sx <= 256 && *sy <= 256 && *sz <= 512
				&& *(void**)((uint8_t*)m + 0x30) != nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool CopyModelRGB(void* m, uint8_t* dst, size_t n) {
		__try {
			const uint8_t* src = *(const uint8_t**)((uint8_t*)m + 0x30);
			if (!src) return false;
			memcpy(dst, src, n);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static cube::Zone* SafeZone(cube::GameController* gc, int zx, int zy) {
		__try { return gc->world.GetZone(zx, zy); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	static uint32_t ReadSeed(cube::GameController* gc) {
		// Wie MountainVillage::ZoneHash: GC + World(0x2E4) + 0x800188.
		__try { return *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	static bool ReadPlayerBlock(cube::GameController* gc, int* x, int* y, int* z) {
		__try {
			if (!gc || !gc->local_player) return false;
			// >> 16 statt / 65536: rundet auch bei negativen Koordinaten ab.
			*x = (int)(gc->local_player->entity_data.position.X >> 16);
			*y = (int)(gc->local_player->entity_data.position.Y >> 16);
			*z = (int)(gc->local_player->entity_data.position.Z >> 16);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// NUR die Chunks im Quader (+ Rand) neu vermaschen. Frueher wurden hier ALLE
	// geladenen Chunks markiert -- genau das, wovor MountainVillage.cpp warnt
	// ("riesiger Spike -> schwarze Karte / Absturz"): der Mesh-Thread arbeitet
	// dann sekundenlang alles neu ab, und echte Aenderungen warten dahinter.
	// Chunk = 32x32 Bloecke, Lage x@+0x18, y@+0x1C (wie UpdateVillageChunks).
	static int RemeshBox(cube::GameController* gc, int x0, int y0, int x1, int y1) {
		__try {
			uint32_t n = gc->chunk_count_to_be_sqaured;
			if (n == 0 || n > 128) return 0;
			cube::Chunk* ch = gc->chunks;
			if (!ch) return 0;
			const int m = 16;                                   // Rand: Nachbarflaechen mitnehmen
			int cxlo = FloorDiv(x0 - m, 32), cxhi = FloorDiv(x1 + m, 32);
			int cylo = FloorDiv(y0 - m, 32), cyhi = FloorDiv(y1 + m, 32);
			int hit = 0;
			for (uint32_t i = 0; i < n * n; i++) {
				uint8_t* c = (uint8_t*)&ch[i];
				int cx = *(int*)(c + 0x18), cy = *(int*)(c + 0x1C);
				if (cx >= cxlo && cx <= cxhi && cy >= cylo && cy <= cyhi) { ch[i].needs_update = 1; hit++; }
			}
			return hit;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	// WorldInfo_placeStructure @0x5F0CE0 (RE_placeStructure.md):
	//   (Model*, int origin[3] UNTERE ECKE, uint rot, byte blockFlags, int typ,
	//    Chunk* zone, char fundament, int crop[4], Recorder* rec)
	// blockFlags 6 und crop {0,0,0,0} wie in allen Vanilla-Aufrufen; rec = 0.
	// KEINE Weltsperre drumherum -- placeStructure greift selbst auf die Zone zu
	// (Kommentar in MountainVillage::PlaceVillageDeco).
	typedef void(__stdcall* Fn_PlaceStructure)(void* model, int* origin, unsigned rot,
		unsigned char blockFlags, int typ, void* zone, char fundament, int* crop, void* recorder);

	// blockFlags = Blocktyp der gesetzten Voxel (wird mit | 0x40 geschrieben): 6 wie Vanilla, 0x0D = Lichtblock.
	static bool CallPlaceSEHF(void* model, int ox, int oy, int oz, int rot, int typ, void* zone, int fundament, unsigned char blockFlags) {
		int origin[3] = { ox, oy, oz };
		int crop[4] = { 0, 0, 0, 0 };
		Fn_PlaceStructure f = (Fn_PlaceStructure)Re(kFnPlaceStructure);
		__try { f(model, origin, (unsigned)(rot & 3), blockFlags, typ, zone, (char)(fundament ? 1 : 0), crop, nullptr); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool CallPlaceSEH(void* model, int ox, int oy, int oz, int rot, int typ, void* zone, int fundament) {
		return CallPlaceSEHF(model, ox, oy, oz, rot, typ, zone, fundament, 6);
	}

	// =====================================================================
	// Schattenmodell
	// =====================================================================
	// placeStructure liest vom Modell NUR +0x30 (RGB-Zeiger) und +0x44/+0x48/
	// +0x4C (Groesse) -- BELEGT in RE_EigeneStrukturen.md 2.1 (WorldInfo.cpp:
	// 5099-7143, game_misc.cpp:46751-46997, GameController.cpp:42657-42671).
	// Ein genullter 0x60-Block mit diesen vier Feldern verhaelt sich also exakt
	// wie ein natives cube::Sprite. +0x55 = 1 (strict) und +0x56 = 1 (kein Mesh)
	// zur Sicherheit, falls doch einmal eine Modellfunktion darauf losgeht.
	struct Shadow {
		alignas(8) uint8_t mem[0x60];
		std::vector<uint8_t> rgb;
	};

	static void FillShadow(Shadow& m, const Lab::Structure& s) {
		m.rgb = s.rgb;
		uint8_t e[3]; ReadEmptyColor(e);
		if (e[0] | e[1] | e[2]) {
			// Das Spiel haelt eine andere Leerfarbe als (0,0,0) -- unsere Luecken
			// muessen dann genau diese Farbe tragen, sonst werden sie schwarz gesetzt
			// (Risiko R5 im Bericht des Subagenten).
			for (size_t i = 0; i + 2 < m.rgb.size(); i += 3)
				if ((m.rgb[i] | m.rgb[i + 1] | m.rgb[i + 2]) == 0) { m.rgb[i] = e[0]; m.rgb[i + 1] = e[1]; m.rgb[i + 2] = e[2]; }
		}
		memset(m.mem, 0, sizeof(m.mem));
		*(uint8_t**)(m.mem + 0x30) = m.rgb.data();
		*(int*)(m.mem + 0x44) = s.sx;
		*(int*)(m.mem + 0x48) = s.sy;
		*(int*)(m.mem + 0x4C) = s.sz;
		m.mem[0x55] = 1;
		m.mem[0x56] = 1;
	}

	// =====================================================================
	// 3. Import / Export
	// =====================================================================
	int SuggestTyp(int idx) {
		// Grundstueckstyp -> Modell -> typ aus Chunk_generateObjects
		// (game_misc.cpp:28654-28683; RE_EigeneStrukturen.md Anhang A).
		if (idx >= 0x8FF && idx <= 0x902) return 9 + (idx - 0x8FF);   // Krieger..Magier
		if (idx == 0x903) return 0x0D;                                  // Schmiede
		if (idx == 0x904) return 0x0E;                                  // Zimmerei
		if (idx == 0x905) return 0x0F;                                  // Weberei
		return 0;
	}

	bool ImportNative(cube::GameController* gc, int index, Lab::Structure& out, std::string& err) {
		void* m = NativeHouseProbe::RegistryModel(gc, index);
		if (!m) { err = "kein Modell an diesem Index"; return false; }
		int sx = 0, sy = 0, sz = 0;
		if (!ReadModelDims(m, &sx, &sy, &sz)) { err = "Modell unlesbar oder ohne Voxel"; return false; }
		out.Create(sx, sy, sz);
		if (!CopyModelRGB(m, out.rgb.data(), out.rgb.size())) { err = "RGB-Daten unlesbar"; return false; }

		// Leerfarbe des Spiels -> (0,0,0) wie im .cub-Format.
		uint8_t e[3]; ReadEmptyColor(e);
		if (e[0] | e[1] | e[2]) {
			for (size_t i = 0; i + 2 < out.rgb.size(); i += 3) {
				bool isEmpty = out.rgb[i] == e[0] && out.rgb[i + 1] == e[1] && out.rgb[i + 2] == e[2];
				bool isBlack = (out.rgb[i] | out.rgb[i + 1] | out.rgb[i + 2]) == 0;
				if (isEmpty) { out.rgb[i] = out.rgb[i + 1] = out.rgb[i + 2] = 0; }
				else if (isBlack) { out.rgb[i] = out.rgb[i + 1] = out.rgb[i + 2] = 1; }
			}
		}
		const char* file = LabModelNames::NameOf(index);
		std::string n = file ? file : "";
		if (n.size() > 4 && n.substr(n.size() - 4) == ".cub") n.resize(n.size() - 4);
		char buf[32]; sprintf_s(buf, "0x%03X", index);
		out.name = n.empty() ? std::string("modell_") + buf : n;
		out.origin = std::string("nativ ") + buf;
		out.typ = SuggestTyp(index);
		out.fundament = 1;
		return true;
	}

	void RequestNativeLibrary(cube::GameController* gc) {
		std::vector<LibEntry> v;
		v.reserve(LabModelNames::kCount);
		for (int i = 0; i < LabModelNames::kCount; i++) {
			const LabModelNames::Entry& e = LabModelNames::kEntries[i];
			LibEntry le; le.index = e.index; le.file = e.file; le.suggestedTyp = SuggestTyp(e.index);
			void* m = NativeHouseProbe::RegistryModel(gc, e.index);
			if (!m || !ReadModelDims(m, &le.sx, &le.sy, &le.sz)) continue;
			v.push_back(le);
		}
		std::cout << "[LAB] Registry gelesen: " << v.size() << " von " << LabModelNames::kCount
			<< " benannten Modellen lesbar." << std::endl;
		Guard g; g_libOut.swap(v); g_libReady = true;
	}

	bool ScanWorld(cube::GameController* gc, int x0, int y0, int z0, int sx, int sy, int sz, Lab::Structure& out) {
		if (sx < 1 || sy < 1 || sz < 1 || sx > 256 || sy > 256 || sz > 512) return false;
		out.Create(sx, sy, sz);
		int n = 0;
		for (int z = 0; z < sz; z++) for (int y = 0; y < sy; y++) for (int x = 0; x < sx; x++) {
			uint8_t r, g, b, t;
			if (!MountainVillage::LabReadBlock(gc, x0 + x, y0 + y, z0 + z, r, g, b, t)) continue;
			out.Set(x, y, z, r, g, b); n++;
		}
		std::ostringstream o; o << "scan " << x0 << "," << y0 << "," << z0;
		out.origin = o.str();
		if (out.name.empty()) out.name = "Scan";
		std::cout << "[LAB] Weltscan " << sx << "x" << sy << "x" << sz << ": " << n << " Bloecke." << std::endl;
		return true;
	}

	int ExportAllNative(cube::GameController* gc, int minDim) {
		Lab::EnsureDir(Lab::ExportDir());
		int saved = 0, failed = 0;
		for (int i = 0; i < LabModelNames::kCount; i++) {
			const LabModelNames::Entry& e = LabModelNames::kEntries[i];
			void* m = NativeHouseProbe::RegistryModel(gc, e.index);
			int sx = 0, sy = 0, sz = 0;
			if (!m || !ReadModelDims(m, &sx, &sy, &sz)) continue;
			if (sx < minDim && sy < minDim) continue;
			Lab::Structure s; std::string err;
			if (!ImportNative(gc, e.index, s, err)) { failed++; continue; }
			std::string path = Lab::ExportDir() + "\\" + e.file;
			if (Lab::SaveCub(path, s, err)) { Lab::SaveMeta(path, s); saved++; }
			else failed++;
		}
		std::cout << "[LAB] Export: " << saved << " native Modelle (>= " << minDim
			<< " Bloecke) nach " << Lab::ExportDir() << ", " << failed << " Fehler." << std::endl;
		return saved;
	}

	// =====================================================================
	// 4. Setzen + Rueckgaengig
	// =====================================================================
	namespace {
		struct UndoState {
			bool valid = false;
			int x0 = 0, y0 = 0, z0 = 0;
			Lab::Structure blocks;          // vorheriger Inhalt
			std::vector<uint8_t> types;     // Blocktyp je Voxel (0 = Luft)
		};
		UndoState g_undo;
	}

	// Kern: setzt s mit Ecke (x,y,z) in ALLE Zonen, die die Grundflaeche beruehrt.
	// placeStructure verwirft Bloecke ausserhalb der uebergebenen Zone und kehrt
	// wortlos zurueck, wenn die Grundflaeche die Zone gar nicht schneidet
	// (WorldInfo.cpp:5307-5340) -- deshalb die Schleife, wie in PlaceBuilding.
	static int StampIntoZones(cube::GameController* gc, Shadow& m, const Lab::Structure& s,
	                          int x, int y, int z, int rot, int typ, int fundament,
	                          int onlyZx, int onlyZy, std::string& report) {
		int w = (rot & 1) ? s.sy : s.sx;
		int d = (rot & 1) ? s.sx : s.sy;
		int zx0 = FloorDiv(x, 256), zx1 = FloorDiv(x + w - 1, 256);
		int zy0 = FloorDiv(y, 256), zy1 = FloorDiv(y + d - 1, 256);
		int zonesOk = 0, zonesMissing = 0, objsNew = 0, objsIndexed = 0, repointed = 0;
		for (int zx = zx0; zx <= zx1; zx++) for (int zy = zy0; zy <= zy1; zy++) {
			if (onlyZx != INT_MIN && (zx != onlyZx || zy != onlyZy)) continue;
			cube::Zone* zone = SafeZone(gc, zx, zy);
			if (!zone) { zonesMissing++; continue; }
			int n0 = MountainVillage::LabObjectCount(gc, zx, zy);
			if (!CallPlaceSEH(m.mem, x, y, z, rot, typ, zone, fundament)) {
				std::cout << "[LAB] placeStructure in Zone (" << zx << "," << zy << ") abgestuerzt (SEH)." << std::endl;
				continue;
			}
			zonesOk++;
			int n1 = MountainVillage::LabObjectCount(gc, zx, zy);
			if (n1 > n0 && n0 >= 0) {
				// Native Markerlogik (typ != 0) hat Objekte angelegt -- das Spiel
				// haengt sie zur Laufzeit NICHT in den Ortsindex. Wir schon, sonst
				// sind sie weder fest noch benutzbar (INTERAKTION_ERKLAERT.md).
				objsNew += n1 - n0;
				int k = MountainVillage::LabIndexObjectsFrom(gc, zx, zy, n0);
				if (k > 0) objsIndexed += k;
			}
			// Der Vektor kann umgezogen sein -> alle Knoten der Zone nachziehen.
			int f = MountainVillage::LabRepointZoneIndex(gc, zx, zy);
			if (f > 0) repointed += f;
		}
		std::ostringstream o;
		o << "Zonen gesetzt " << zonesOk << ", nicht geladen " << zonesMissing;
		if (typ != 0) o << ", native Objekte " << objsNew << " (indexiert " << objsIndexed << ")";
		if (repointed) o << ", Indexknoten nachgezogen " << repointed;
		report = o.str();
		return zonesOk;
	}

	int StampStructure(cube::GameController* gc, const Lab::Structure& s, int x, int y, int z,
	                   int rot, int fundament, int onlyZx, int onlyZy) {
		if (!gc || !s.Valid()) return 0;
		std::unique_ptr<Shadow> m(new Shadow());
		FillShadow(*m, s);
		int w = (rot & 1) ? s.sy : s.sx, d = (rot & 1) ? s.sx : s.sy;
		int zx0 = FloorDiv(x, 256), zx1 = FloorDiv(x + w - 1, 256);
		int zy0 = FloorDiv(y, 256), zy1 = FloorDiv(y + d - 1, 256);
		int ok = 0;
		for (int zx = zx0; zx <= zx1; zx++) for (int zy = zy0; zy <= zy1; zy++) {
			if (onlyZx != INT_MIN && (zx != onlyZx || zy != onlyZy)) continue;
			cube::Zone* zone = SafeZone(gc, zx, zy);
			if (!zone) continue;
			if (CallPlaceSEH(m->mem, x, y, z, rot, 0, zone, fundament)) ok++;
		}
		return ok;
	}
	// ---- Vorbereitete Modelle (Staedte): Schattenmodell einmal bauen, beliebig oft stempeln ----
	struct PreparedModel { Shadow m; int sx = 0, sy = 0, sz = 0; };
	PreparedModel* Prepare(const Lab::Structure& s) {
		if (!s.Valid()) return nullptr;
		PreparedModel* p = new PreparedModel();
		FillShadow(p->m, s);
		p->sx = s.sx; p->sy = s.sy; p->sz = s.sz;
		return p;
	}
	void PreparedDims(const PreparedModel* p, int* sx, int* sy, int* sz) { *sx = p ? p->sx : 0; *sy = p ? p->sy : 0; *sz = p ? p->sz : 0; }
	int StampPrepared(PreparedModel* p, int x, int y, int z, int rot, int fundament, int culture, void* zone) {
		if (!p || !zone) return 0;
		return CallPlaceSEH(p->m.mem, x, y, z, rot, culture, zone, fundament) ? 1 : 0;
	}
	int StampPreparedFlags(PreparedModel* p, int x, int y, int z, int rot, int fundament, int culture, void* zone, int blockFlags) {
		if (!p || !zone) return 0;
		return CallPlaceSEHF(p->m.mem, x, y, z, rot, culture, zone, fundament, (unsigned char)blockFlags) ? 1 : 0;
	}
	void* NativeModelPtr(cube::GameController* gc, int index) {
		void* m = NativeHouseProbe::RegistryModel(gc, index);
		int sx, sy, sz;
		return (m && ReadModelDims(m, &sx, &sy, &sz)) ? m : nullptr;
	}
	bool NativeModelDims(void* m, int* sx, int* sy, int* sz) { return ReadModelDims(m, sx, sy, sz); }
	int StampModelPtr(void* model, int x, int y, int z, int rot, int fundament, int culture, void* zone) {
		if (!model || !zone) return 0;
		return CallPlaceSEH(model, x, y, z, rot, culture, zone, fundament) ? 1 : 0;
	}
	// Ohne Kopie: das Schattenmodell zeigt direkt auf s.rgb (Leerfarbe wird darin ersetzt, s ist danach "verbraucht").
	int StampInPlace(Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone) {
		if (!zone || !s.Valid()) return 0;
		uint8_t e[3]; ReadEmptyColor(e);
		if (e[0] | e[1] | e[2])
			for (size_t i = 0; i + 2 < s.rgb.size(); i += 3)
				if ((s.rgb[i] | s.rgb[i + 1] | s.rgb[i + 2]) == 0) { s.rgb[i] = e[0]; s.rgb[i + 1] = e[1]; s.rgb[i + 2] = e[2]; }
		alignas(8) uint8_t mem[0x60]; memset(mem, 0, sizeof(mem));
		*(uint8_t**)(mem + 0x30) = s.rgb.data();
		*(int*)(mem + 0x44) = s.sx; *(int*)(mem + 0x48) = s.sy; *(int*)(mem + 0x4C) = s.sz;
		mem[0x55] = 1; mem[0x56] = 1;
		return CallPlaceSEH(mem, x, y, z, rot, culture, zone, fundament) ? 1 : 0;
	}
	int StampInPlaceFlags(Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone, int blockFlags) {
		if (!zone || !s.Valid()) return 0;
		uint8_t e[3]; ReadEmptyColor(e);
		if (e[0] | e[1] | e[2])
			for (size_t i = 0; i + 2 < s.rgb.size(); i += 3)
				if ((s.rgb[i] | s.rgb[i + 1] | s.rgb[i + 2]) == 0) { s.rgb[i] = e[0]; s.rgb[i + 1] = e[1]; s.rgb[i + 2] = e[2]; }
		alignas(8) uint8_t mem[0x60]; memset(mem, 0, sizeof(mem));
		*(uint8_t**)(mem + 0x30) = s.rgb.data();
		*(int*)(mem + 0x44) = s.sx; *(int*)(mem + 0x48) = s.sy; *(int*)(mem + 0x4C) = s.sz;
		mem[0x55] = 1; mem[0x56] = 1;
		return CallPlaceSEHF(mem, x, y, z, rot, culture, zone, fundament, (unsigned char)blockFlags) ? 1 : 0;
	}
	int StampIntoZoneCulture(const Lab::Structure& s, int x, int y, int z, int rot, int fundament, int culture, void* zone) {
		if (!zone || !s.Valid()) return 0;
		std::unique_ptr<Shadow> m(new Shadow());
		FillShadow(*m, s);
		return CallPlaceSEH(m->mem, x, y, z, rot, culture, zone, fundament) ? 1 : 0;
	}
	int StampIntoZone(const Lab::Structure& s, int x, int y, int z, int rot, int fundament, void* zone) {
		if (!zone || !s.Valid()) return 0;
		std::unique_ptr<Shadow> m(new Shadow());
		FillShadow(*m, s);
		return CallPlaceSEH(m->mem, x, y, z, rot, 0, zone, fundament) ? 1 : 0;
	}
	void RemeshArea(cube::GameController* gc, int x0, int y0, int x1, int y1) { RemeshBox(gc, x0, y0, x1, y1); }
	cube::Zone* ZoneAt(cube::GameController* gc, int zx, int zy) { return gc ? SafeZone(gc, zx, zy) : nullptr; }

	// ---- Gebaeude-Funktionen (BuildingFx) nach dem Stempeln: Leuchtbloecke (Blocktyp 0x0D), Funktionsbloecke ->
	//      native Objekte bzw. Bewohner-Posten. onlyZx/onlyZy: nur Bloecke/Objekte in dieser Zone (Neu-Stempeln).
	static std::string ApplyFx(cube::GameController* gc, const Lab::Structure& s, const BuildingFx::Def& fx, int ox, int oy, int oz, int rot, int onlyZx, int onlyZy) {
		if (!fx.Any() || !s.Valid()) return "";
		std::vector<BuildingFx::GlowVoxel> glow; std::vector<BuildingFx::Group> groups;
		BuildingFx::Analyze(s, fx, glow, groups);
		auto InZone = [&](int wx, int wy) { return onlyZx == INT_MIN || (FloorDiv(wx, 256) == onlyZx && FloorDiv(wy, 256) == onlyZy); };
		int nGlow = 0, nCleared = 0;
		for (const BuildingFx::GlowVoxel& v : glow) {
			int wx, wy; BuildingFx::ModelToWorld(rot, s.sx, s.sy, v.x, v.y, ox, oy, &wx, &wy);
			if (InZone(wx, wy) && MountainVillage::LabPutBlock(gc, wx, wy, oz + v.z, v.r, v.g, v.b, 0x0D | 0x40)) nGlow++;
		}
		// Funktionsvoxel raeumen (sie werden Objekt/Bewohner)
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			uint8_t r, g, b; if (!s.Get(x, y, z, r, g, b)) continue;
			const BuildingFx::Entry* e = fx.Find(r, g, b);
			if (!e || e->fn.empty()) continue;
			int wx, wy; BuildingFx::ModelToWorld(rot, s.sx, s.sy, x, y, ox, oy, &wx, &wy);
			if (InZone(wx, wy) && MountainVillage::LabPutBlock(gc, wx, wy, oz + z, 0, 0, 0, 0)) nCleared++;
		}
		int n = 0; const BuildingFx::Choice* ch = BuildingFx::Choices(&n);
		std::vector<MountainVillage::LabObj> objs; int nPosts = 0;
		for (const BuildingFx::Group& g : groups) {
			if (g.choice < 0 || g.choice >= n) continue;
			int wx, wy; BuildingFx::ModelToWorld(rot, s.sx, s.sy, g.x, g.y, ox, oy, &wx, &wy);
			if (!InZone(wx, wy)) continue;
			const BuildingFx::Choice& c = ch[g.choice];
			if (c.kind == BuildingFx::K_OBJECT) objs.push_back({ c.param, wx, wy, oz + g.z, rot & 3, c.sx, c.sy, c.sz });
			else if (c.kind == BuildingFx::K_VENDOR) { City::AddExternalPost(0, wx, wy, oz + g.z, c.param); nPosts++; }
			else if (c.kind == BuildingFx::K_ALCHEMIST) { City::AddExternalPost(1, wx, wy, oz + g.z, 0x80); nPosts++; }
			else if (c.kind == BuildingFx::K_MERCHANT) { AdventureMod::RegisterMerchantPost(wx, wy, oz + g.z); nPosts++; }
		}
		int made = MountainVillage::LabCreateObjects(gc, objs);
		std::ostringstream o;
		o << ", Funktionen: " << made << " Objekte, " << nPosts << " Bewohner-Posten, " << nGlow << " Leuchtbloecke";
		return o.str();
	}
	static std::map<std::string, BuildingFx::Def> g_fxCache;                       // Name -> .funk (Neu-Stempeln)

	bool PlaceNative(cube::GameController* gc, const Lab::Structure& s, const PlaceOptions& o, std::string& report) {
		if (!gc || !s.Valid()) { report = "keine gueltige Struktur"; return false; }
		int w = (o.rot & 1) ? s.sy : s.sx;
		int d = (o.rot & 1) ? s.sx : s.sy;

		// Vorher-Zustand fuer Rueckgaengig sichern (nur der Quader der Struktur;
		// ein Fundament nach unten ist nicht erfasst).
		g_undo.valid = false;
		g_undo.x0 = o.x; g_undo.y0 = o.y; g_undo.z0 = o.z;
		g_undo.blocks.Create(w, d, s.sz);
		g_undo.types.assign((size_t)w * d * s.sz, 0);
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < d; y++) for (int x = 0; x < w; x++) {
			uint8_t r, g, b, t;
			if (!MountainVillage::LabReadBlock(gc, o.x + x, o.y + y, o.z + z, r, g, b, t)) continue;
			g_undo.blocks.Set(x, y, z, r, g, b);
			g_undo.types[((size_t)d * z + y) * w + x] = t;
		}
		g_undo.valid = true;

		std::unique_ptr<Shadow> m(new Shadow());
		FillShadow(*m, s);
		std::string zr;
		int ok = StampIntoZones(gc, *m, s, o.x, o.y, o.z, o.rot, o.typ, o.fundament, INT_MIN, INT_MIN, zr);

		int objs = 0;
		if (o.replaceMarkers && o.typ == 0)
			objs = MountainVillage::LabReplaceMarkersBox(gc, o.x, o.y, o.z, o.x + w - 1, o.y + d - 1, o.z + s.sz - 1);
		std::string fxr = ApplyFx(gc, s, o.fx, o.x, o.y, o.z, o.rot, INT_MIN, INT_MIN);
		RemeshBox(gc, o.x, o.y, o.x + w - 1, o.y + d - 1);

		std::ostringstream r;
		r << "'" << s.name << "' " << s.sx << "x" << s.sy << "x" << s.sz << " bei (" << o.x << "," << o.y << "," << o.z
		  << ") Drehung " << o.rot << ", typ 0x" << std::hex << o.typ << std::dec << ": " << zr;
		if (o.replaceMarkers && o.typ == 0) r << ", Marker->Objekte " << objs;
		r << fxr;
		report = r.str();

		if (ok > 0 && o.remember) {
			// Datei sichern (die Liste verweist per Name darauf) und eintragen.
			Lab::EnsureDir(Lab::StructuresDir());
			std::string name = Lab::SafeFileName(s.name);
			std::string path = Lab::StructuresDir() + "\\" + name + ".cub";
			std::string err;
			if (!Lab::SaveCub(path, s, err)) { report += "  [MERKEN FEHLGESCHLAGEN: " + err + "]"; }
			else {
				Lab::Structure meta = s; meta.typ = o.typ; meta.fundament = o.fundament;
				Lab::SaveMeta(path, meta);
				std::string fe; BuildingFx::Save(path, o.fx, fe);               // Funktionen gehoeren zur gemerkten Datei
				g_fxCache[name] = o.fx;
				Placement p; p.name = name; p.x = o.x; p.y = o.y; p.z = o.z; p.rot = o.rot;
				p.typ = o.typ; p.fundament = o.fundament; p.markers = o.replaceMarkers ? 1 : 0;
				AddPlacementInternal(p);
				report += "  [gemerkt]";
			}
		}
		std::cout << "[LAB] Gesetzt: " << report << std::endl;
		return ok > 0;
	}

	bool UndoLastPlacement(cube::GameController* gc, std::string& report) {
		if (!g_undo.valid) { report = "nichts zum Zuruecknehmen"; return false; }
		const Lab::Structure& b = g_undo.blocks;
		int n = 0;
		for (int z = 0; z < b.sz; z++) for (int y = 0; y < b.sy; y++) for (int x = 0; x < b.sx; x++) {
			uint8_t r = 0, g = 0, bl = 0;
			uint8_t t = g_undo.types[((size_t)b.sy * z + y) * b.sx + x];
			if (b.Get(x, y, z, r, g, bl) && t != 0)
				MountainVillage::LabPutBlock(gc, g_undo.x0 + x, g_undo.y0 + y, g_undo.z0 + z, r, g, bl, t);
			else
				MountainVillage::LabPutBlock(gc, g_undo.x0 + x, g_undo.y0 + y, g_undo.z0 + z, 0, 0, 0, 0);
			n++;
		}
		RemeshBox(gc, g_undo.x0, g_undo.y0, g_undo.x0 + b.sx - 1, g_undo.y0 + b.sy - 1);
		g_undo.valid = false;
		std::ostringstream o; o << n << " Voxel zurueckgesetzt (Objekte wie Tueren bleiben bis zum Neuladen der Zone).";
		report = o.str();
		std::cout << "[LAB] Rueckgaengig: " << report << std::endl;
		return true;
	}

	// =====================================================================
	// 5. Platzierungsliste + Neu-Stempeln
	// =====================================================================
	// Weg (b)+(d) aus RE_EigeneStrukturen.md: Nichts wird in den Spielstand
	// geschrieben. Stattdessen merkt sich der Mod je Welt, WAS WO steht, und
	// stempelt es bei jeder Zonengenerierung neu -- genau wie das Spiel seine
	// eigenen Gebaeude jedes Mal aus dem Seed neu erzeugt. Damit ist die
	// Struktur nach einem Neustart wieder da und die Markerobjekte entstehen
	// ebenfalls neu. Kein Risiko fuer den Spielstand.
	namespace {
		std::vector<Placement> g_place;
		uint32_t g_placeSeed = 0;
		bool g_placeLoaded = false;
		struct PendingZone { int zx, zy; int tries; DWORD due; };
		std::vector<PendingZone> g_pendingZones;               // aus OnZoneGenerated (beliebiger Thread)
		std::map<std::string, std::unique_ptr<Shadow>> g_shadowCache;
		std::map<std::string, Lab::Structure> g_structCache;
	}

	static std::string PlacementFile(uint32_t seed) {
		char buf[64]; sprintf_s(buf, "\\platzierungen_%08X.txt", seed);
		return Lab::StructuresDir() + buf;
	}

	static void SavePlacementsLocked() {
		Lab::EnsureDir(Lab::StructuresDir());
		std::ofstream f(PlacementFile(g_placeSeed));
		if (!f) return;
		f << "# LAB-Platzierungen dieser Welt (Seed " << std::hex << g_placeSeed << std::dec << ")\n";
		f << "# name x y z drehung typ fundament marker   -- x/y/z = UNTERE ECKE in Bloecken\n";
		for (const Placement& p : g_place)
			f << p.name << " " << p.x << " " << p.y << " " << p.z << " " << p.rot << " "
			  << p.typ << " " << p.fundament << " " << p.markers << "\n";
	}

	static void LoadPlacementsLocked(uint32_t seed) {
		g_place.clear(); g_placeSeed = seed; g_placeLoaded = true;
		std::ifstream f(PlacementFile(seed));
		if (!f) return;
		std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream is(line);
			Placement p;
			if (is >> p.name >> p.x >> p.y >> p.z >> p.rot >> p.typ >> p.fundament) {
				if (!(is >> p.markers)) p.markers = 1;
				g_place.push_back(p);
			}
		}
	}

	void AddPlacementInternal(const Placement& p) {
		Guard g;
		g_place.push_back(p);
		g_structCache.erase(p.name);          // Datei kann sich geaendert haben
		g_shadowCache.erase(p.name);
		SavePlacementsLocked();
	}

	std::vector<Placement> GetPlacements() { Guard g; return g_place; }

	bool RemovePlacement(int i) {
		Guard g;
		if (i < 0 || i >= (int)g_place.size()) return false;
		g_place.erase(g_place.begin() + i);
		SavePlacementsLocked();
		return true;
	}

	// Strukturdatei + Schattenmodell aus dem Zwischenspeicher (Spielthread).
	static Shadow* ShadowFor(const std::string& name, const Lab::Structure** sOut) {
		auto it = g_shadowCache.find(name);
		if (it == g_shadowCache.end()) {
			Lab::Structure s; std::string err;
			std::string path = Lab::StructuresDir() + "\\" + name + ".cub";
			if (!Lab::LoadCub(path, s, err)) {
				std::cout << "[LAB] Platzierung '" << name << "': " << err << std::endl;
				return nullptr;
			}
			Lab::LoadMeta(path, s);
			std::unique_ptr<Shadow> m(new Shadow());
			FillShadow(*m, s);
			g_structCache[name] = s;
			it = g_shadowCache.emplace(name, std::move(m)).first;
		}
		*sOut = &g_structCache[name];
		return it->second.get();
	}

	// Spielthread: gemeldete Zonen abarbeiten.
	static void ProcessPendingZones(cube::GameController* gc) {
		std::vector<PendingZone> work;
		std::vector<Placement> places;
		{
			Guard g;
			if (g_pendingZones.empty()) return;
			DWORD now = GetTickCount();
			for (size_t i = 0; i < g_pendingZones.size();) {
				if ((int)(now - g_pendingZones[i].due) >= 0) { work.push_back(g_pendingZones[i]); g_pendingZones.erase(g_pendingZones.begin() + i); }
				else i++;
			}
			places = g_place;
		}
		if (work.empty() || places.empty()) return;

		bool any = false;
		for (const PendingZone& pz : work) {
			if (!SafeZone(gc, pz.zx, pz.zy)) {
				// Noch nicht im Weltgitter -- spaeter nochmal (max. 10 Versuche).
				if (pz.tries < 10) { Guard g; g_pendingZones.push_back({ pz.zx, pz.zy, pz.tries + 1, GetTickCount() + 500 }); }
				continue;
			}
			for (const Placement& p : places) {
				const Lab::Structure* s = nullptr;
				Shadow* m = ShadowFor(p.name, &s);
				if (!m || !s) continue;
				int w = (p.rot & 1) ? s->sy : s->sx, d = (p.rot & 1) ? s->sx : s->sy;
				if (pz.zx < FloorDiv(p.x, 256) || pz.zx > FloorDiv(p.x + w - 1, 256)) continue;
				if (pz.zy < FloorDiv(p.y, 256) || pz.zy > FloorDiv(p.y + d - 1, 256)) continue;
				std::string zr;
				StampIntoZones(gc, *m, *s, p.x, p.y, p.z, p.rot, p.typ, p.fundament, pz.zx, pz.zy, zr);
				if (p.markers && p.typ == 0) {
					// Nur den Teil des Quaders, der in DIESER Zone liegt.
					int x0 = (std::max)(p.x, pz.zx * 256), x1 = (std::min)(p.x + w - 1, pz.zx * 256 + 255);
					int y0 = (std::max)(p.y, pz.zy * 256), y1 = (std::min)(p.y + d - 1, pz.zy * 256 + 255);
					MountainVillage::LabReplaceMarkersBox(gc, x0, y0, p.z, x1, y1, p.z + s->sz - 1);
				}
				{
					auto fit = g_fxCache.find(p.name);
					if (fit == g_fxCache.end()) { BuildingFx::Def d; BuildingFx::Load(Lab::StructuresDir() + "\\" + p.name + ".cub", d); fit = g_fxCache.emplace(p.name, d).first; }
					zr += ApplyFx(gc, *s, fit->second, p.x, p.y, p.z, p.rot, pz.zx, pz.zy);
				}
				RemeshBox(gc, p.x, p.y, p.x + w - 1, p.y + d - 1);
				std::cout << "[LAB] Neu gestempelt: '" << p.name << "' in Zone (" << pz.zx << "," << pz.zy << ") -- " << zr << std::endl;
				any = true;
			}
		}
		(void)any;
	}

	class LabMod : public AdventureFramework::IAdventureMod {
	public:
		void OnZoneGenerated(cube::GameController*, cube::World*, int zx, int zy, cube::Zone*) override {
			// Kann auf einem Generator-Thread laufen -> hier NICHTS an der Welt
			// tun, nur vormerken. Gestempelt wird im naechsten Frame auf dem
			// Spielthread (ProcessPendingZones), mit kurzer Verzoegerung, damit die
			// Zone sicher im Weltgitter haengt.
			Guard g;
			if (g_place.empty()) return;
			g_pendingZones.push_back({ zx, zy, 0, GetTickCount() + 300 });
		}
	};
	static LabMod g_labMod;

	void RegisterWithFramework() { AdventureFramework::RegisterMod(&g_labMod); }

	// =====================================================================
	// Drain: jedes Frame vom Spielthread
	// =====================================================================
	void Drain(cube::GameController* gc) {
		if (!gc) return;

		// Zustand fuer das Fenster
		Snapshot s;
		s.inGame = ReadPlayerBlock(gc, &s.px, &s.py, &s.pz);
		s.zx = FloorDiv(s.px, 256); s.zy = FloorDiv(s.py, 256);
		s.seed = ReadSeed(gc);
		uint8_t e[3]; ReadEmptyColor(e); s.emptyColorBlack = (e[0] | e[1] | e[2]) == 0;
		s.undoAvailable = g_undo.valid;
		{
			Guard g;
			s.ovl_tracking = g_flagTracking; s.oceanGen = g_flagOcean;
			s.registryCount = LabModelNames::kCount;
			g_snap = s;
			// Weltwechsel -> Platzierungsliste der neuen Welt laden.
			if (s.seed != 0 && (!g_placeLoaded || s.seed != g_placeSeed)) {
				LoadPlacementsLocked(s.seed);
				g_shadowCache.clear(); g_structCache.clear();
				if (!g_place.empty())
					std::cout << "[LAB] Welt " << std::hex << s.seed << std::dec << ": " << g_place.size()
					          << " gemerkte Platzierungen geladen." << std::endl;
			}
		}

		ProcessPendingZones(gc);

		// Auftraege: so viele wie in ~12 ms passen, mindestens einer.
		DWORD start = GetTickCount();
		for (int n = 0; n < 64; n++) {
			QueuedJob j;
			{
				Guard g;
				if (g_jobs.empty()) break;
				j = std::move(g_jobs.front());
				g_jobs.pop_front();
			}
			if (!RunJobSEH(&j.fn, gc))
				std::cout << "[LAB] Auftrag '" << j.label << "' abgestuerzt (SEH abgefangen)." << std::endl;
			if (GetTickCount() - start > 12) break;
		}
	}
}
