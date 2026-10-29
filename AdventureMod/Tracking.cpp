#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Tracking.h"
#include "ModDb.h"
#include "BiomeMod.h"
#include "BiomeGen.h"
#include "MountainVillage.h"
#include "ModSwitches.h"
#include <windows.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Tracking {
	static const char* kKey[T_COUNT] = { "f9_biom", "f9_dungeons", "f9_tempel", "f9_staedte", "f9_bergdoerfer", "f9_eigene_biome",
	                                     "f9_native_doerfer", "f9_native_dungeons", "f9_native_orte" };
	static const bool kDef[T_COUNT] = { true, true, true, true, false, false, true, true, false };
	static const char* kLabel[T_COUNT] = {
		"Biom an meiner Position", "Eigene Dungeons (Burg, Apfelhain, ...)", "Kleine Tempel", "Eigene Staedte",
		"Bergdoerfer", "Eigene Biome (naechstes Gebiet je Biom)", "Native Doerfer", "Native Dungeons (Burg, Katakomben, Palast, ...)",
		"Andere native Orte (Hoehlen, Portale, Landmarken)" };
	const char* Label(int k) { return (k >= 0 && k < T_COUNT) ? kLabel[k] : "?"; }
	static volatile LONG g_on[T_COUNT];
	static volatile LONG g_onInit = 0;
	static void InitOn() {
		if (InterlockedCompareExchange(&g_onInit, 1, 0) != 0) return;
		for (int k = 0; k < T_COUNT; k++) InterlockedExchange(&g_on[k], ModSwitches::Get(kKey[k], kDef[k]) ? 1 : 0);
	}
	bool On(int k) { InitOn(); return k >= 0 && k < T_COUNT && g_on[k] != 0; }
	void SetOn(int k, bool on) { if (k < 0 || k >= T_COUNT) return; InitOn(); InterlockedExchange(&g_on[k], on ? 1 : 0); ModSwitches::Set(kKey[k], on); }

	// ---- gebaute Orte (je Welt) ---------------------------------------------------------------------------
	struct Place { int kind; std::string name; int x, y, z; };
	static CRITICAL_SECTION g_cs; static volatile LONG g_csInit = 0;
	static std::vector<Place> g_places; static uint32_t g_seed = 0; static bool g_loaded = false;
	static void Cs() { if (g_csInit == 1) return; if (InterlockedCompareExchange(&g_csInit, 2, 0) == 0) { InitializeCriticalSection(&g_cs); InterlockedExchange(&g_csInit, 1); } else while (g_csInit != 1) Sleep(0); }
	struct Lock { Lock() { Cs(); EnterCriticalSection(&g_cs); } ~Lock() { LeaveCriticalSection(&g_cs); } };
	static std::string File(uint32_t seed) { char b[64]; sprintf_s(b, "AdventureMod_orte_%08X.txt", seed); return b; }
	static void LoadLocked(uint32_t seed) {
		g_places.clear(); g_seed = seed; g_loaded = true;
		std::istringstream f(ModDb::Txt(File(seed))); std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::stringstream ss(line); std::string a, n, x, y, z;
			if (!std::getline(ss, a, ';') || !std::getline(ss, n, ';') || !std::getline(ss, x, ';') || !std::getline(ss, y, ';') || !std::getline(ss, z, ';')) continue;
			g_places.push_back(Place{ atoi(a.c_str()), n, atoi(x.c_str()), atoi(y.c_str()), atoi(z.c_str()) });
		}
	}
	void Note(int kind, const std::string& name, int x, int y, int z, uint32_t seed) {
		Lock l;
		if (!g_loaded || seed != g_seed) LoadLocked(seed);
		for (const Place& p : g_places) if (p.kind == kind && std::abs(p.x - x) < 48 && std::abs(p.y - y) < 48) return;
		g_places.push_back(Place{ kind, name, x, y, z });
		ModDb::Writer f(File(seed), true);
		f << kind << ';' << name << ';' << x << ';' << y << ';' << z << '\n';
	}

	// ---- Anzeige ---------------------------------------------------------------------------------------------
	static const char* Compass(int dx, int dy) {
		double a = std::atan2((double)dy, (double)dx) * 180.0 / 3.14159265;   // +X = Ost, +Y = Nord
		const char* n[8] = { "O", "NO", "N", "NW", "W", "SW", "S", "SO" };
		return n[(int)std::floor((a + 22.5 + 360.0) / 45.0) % 8];
	}
	struct Hit { long long d2; std::string line; };
	static void AddNearest(std::string& out, const char* head, std::vector<Hit>& v, size_t n) {
		if (v.empty()) return;
		std::sort(v.begin(), v.end(), [](const Hit& a, const Hit& b) { return a.d2 < b.d2; });
		for (size_t i = 0; i < v.size() && i < n; i++) out += std::string(i == 0 ? head : "") + (i == 0 ? "" : "   ") + v[i].line + "\n";
	}
	static std::string Line(const std::string& name, int x, int y, int px, int py) {
		char b[160]; int dx = x - px, dy = y - py;
		sprintf_s(b, "%s  %.0f Bl %s  (%d, %d)", name.c_str(), std::sqrt((double)dx * dx + (double)dy * dy), Compass(dx, dy), x, y);
		return b;
	}
	static uint32_t WorldSeedRaw(cube::GameController* gc) { __try { return *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
	static bool PlayerRaw(cube::GameController* gc, int* x, int* y, int* z) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false;
			*x = (int)(*(int64_t*)(c + 0x10) >> 16); *y = (int)(*(int64_t*)(c + 0x18) >> 16); *z = (int)(*(int64_t*)(c + 0x20) >> 16);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	std::string Text(cube::GameController* gc) {
		static DWORD last = 0; static std::string cache;
		DWORD now = GetTickCount();
		if (last && now - last < 500) return cache;
		last = now;
		int px, py, pz; if (!gc || !PlayerRaw(gc, &px, &py, &pz)) return cache = "";
		auto fdiv = [](int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); };
		char b[200];
		sprintf_s(b, "X %d   Y %d   Z %d   Zone %d, %d\n", px, py, pz, fdiv(px, 256), fdiv(py, 256));
		std::string out = b;
		if (On(T_BIOME)) out += "Biom: " + BiomeMod::BiomeNameAt(gc, px, py) + "\n";
		// gebaute Orte
		const uint32_t seed = WorldSeedRaw(gc);
		{
			std::vector<Hit> hits[T_COUNT];
			Lock l;
			if (!g_loaded || seed != g_seed) LoadLocked(seed);
			for (const Place& p : g_places) {
				if (p.kind < 0 || p.kind >= T_COUNT || !On(p.kind)) continue;
				long long dx = p.x - px, dy = p.y - py;
				hits[p.kind].push_back(Hit{ dx * dx + dy * dy, Line(p.name, p.x, p.y, px, py) });
			}
			AddNearest(out, "Dungeon: ", hits[T_DUNGEON], 3);
			AddNearest(out, "Tempel: ", hits[T_TEMPLE], 2);
			AddNearest(out, "Stadt: ", hits[T_CITY], 2);
		}
		// native Orte (aus den Regionen, auch unentdeckte)
		if (On(T_NATIVE_TOWN) || On(T_NATIVE_DUNGEON) || On(T_NATIVE_OTHER)) {
			std::vector<BiomeMod::SiteInfo> s; BiomeMod::NativeSites(gc, 6000, s);
			std::vector<Hit> town, dun, other;
			for (const BiomeMod::SiteInfo& e : s) {
				long long dx = e.x - px, dy = e.y - py, d2 = dx * dx + dy * dy;
				if (e.kind == 1) { if (On(T_NATIVE_TOWN)) town.push_back(Hit{ d2, Line("Dorf", e.x, e.y, px, py) }); }
				else if (e.kind == 14 || e.kind == 5) { if (On(T_NATIVE_DUNGEON)) dun.push_back(Hit{ d2, Line(e.kind == 14 ? "Dungeon" : "Ruinen", e.x, e.y, px, py) }); }
				else if (On(T_NATIVE_OTHER)) {
					std::string n = BiomeGen::SiteKindName(e.kind); size_t k = n.find(" ("); if (k != std::string::npos) n.resize(k);
					other.push_back(Hit{ d2, Line(n, e.x, e.y, px, py) });
				}
			}
			AddNearest(out, "Natives Dorf: ", town, 2);
			AddNearest(out, "Nativer Dungeon: ", dun, 2);
			AddNearest(out, "Ort: ", other, 3);
		}
		if (On(T_OWNBIOME)) {
			std::vector<BiomeMod::SiteInfo> s; std::vector<std::string> names;
			BiomeMod::OwnBiomeTargets(gc, s, names);
			std::vector<Hit> v;
			for (size_t i = 0; i < s.size(); i++) { long long dx = s[i].x - px, dy = s[i].y - py; v.push_back(Hit{ dx * dx + dy * dy, Line(names[i], s[i].x, s[i].y, px, py) }); }
			AddNearest(out, "Biom: ", v, 4);
		}
		if (On(T_VILLAGE)) {
			std::string v; MountainVillage::AppendTracking(gc, v);
			std::istringstream is(v); std::string line; int n = 0;                  // nur die ersten Zeilen (Ueberschrift + 2 Ziele)
			while (std::getline(is, line) && n < 3) { out += line + "\n"; n++; }
		}
		return cache = out;
	}
	std::string Status() {
		Lock l;
		int n[T_COUNT] = { 0 }; for (const Place& p : g_places) if (p.kind >= 0 && p.kind < T_COUNT) n[p.kind]++;
		char b[200]; sprintf_s(b, "F9: gemerkt %d Dungeons, %d Tempel, %d Staedte (Welt %08X).", n[T_DUNGEON], n[T_TEMPLE], n[T_CITY], g_seed);
		return b;
	}
}
