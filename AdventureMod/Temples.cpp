#include "Temples.h"
#include "ModDb.h"
#include "Tracking.h"
#include "City.h"
#include "Host.h"
#include "NativeUi.h"
#include "LabStructure.h"
#include "LabBridge.h"
#include "LabWindow.h"
#include "BiomeMod.h"
#include "Lan.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

// Herleitung: Temples.h.

namespace Temples {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	static CRITICAL_SECTION& Cs() { static struct S { CRITICAL_SECTION c; S() { InitializeCriticalSection(&c); } } s; return s.c; }
	struct Lock { Lock() { EnterCriticalSection(&Cs()); } ~Lock() { LeaveCriticalSection(&Cs()); } };

	// ---- Modelle --------------------------------------------------------------------------------
	static const char* kNames[5] = { "Standard", "Dschungel", "Herbst", "Schnee", "Wueste" };
	struct Model { std::string name; Lab::Structure s; LabBridge::PreparedModel* p = nullptr; int ox = 0, oy = 0, oz = 0; };   // Kugel (Modellkoordinaten)
	static std::vector<Model> g_models; static bool g_loaded = false;
	static void LoadModels() {                                                    // unter Lock
		if (g_loaded) return;
		g_loaded = true;
		for (const char* n : kNames) {
			Model m; m.name = n; std::string err;
			std::string p1 = Lab::StructuresDir() + "\\Tempel\\Tempel_" + n + ".cub", p2 = Lab::StructuresDir() + "\\Tempel_" + n + ".cub";
			if (!(Lab::LoadCub(p1, m.s, err) && m.s.Valid()) && !(Lab::LoadCub(p2, m.s, err) && m.s.Valid())) continue;
			if (!m.s.CropToContent()) continue;
			// Kugel: in der Mittelsaeule der zweite zusammenhaengende Abschnitt ueber dem Altar (Luecke dazwischen),
			// sonst die Altaroberkante. (Tempel_Standard: Altar z 0..6, Kugel z 8..16 -> Mitte 12)
			int cx = m.s.sx / 2, cy = m.s.sy / 2, run = 0, r0 = -1, r1 = -1, top = 0;
			for (int z = 0; z < m.s.sz; z++) {
				bool sol = m.s.Solid(cx, cy, z);
				if (sol) { top = z; if (z == 0 || !m.s.Solid(cx, cy, z - 1)) { run++; if (run == 2) r0 = z; } if (run == 2) r1 = z; }
			}
			m.ox = cx; m.oy = cy; m.oz = r0 >= 0 ? (r0 + r1) / 2 : top;
			m.p = LabBridge::Prepare(m.s);
			std::cout << "[Tempel] Kleiner Tempel " << n << " " << m.s.sx << "x" << m.s.sy << "x" << m.s.sz << ", Kugel bei Modellhoehe " << m.oz << "." << std::endl;
			g_models.push_back(std::move(m));
		}
		if (g_models.empty()) std::cout << "[Tempel] Keine kleinen Tempel gefunden (Strukturen\\Tempel_<Art>.cub)." << std::endl;
	}
	int VariantCount() { Lock l; LoadModels(); return (int)g_models.size(); }
	int VariantByName(const char* key) { Lock l; LoadModels(); for (size_t i = 0; i < g_models.size(); i++) if (_stricmp(g_models[i].name.c_str(), key) == 0) return (int)i; return -1; }
	bool ModelSize(int v, int* sx, int* sy, int* sz) { Lock l; LoadModels(); if (v < 0 || v >= (int)g_models.size()) return false; *sx = g_models[v].s.sx; *sy = g_models[v].s.sy; *sz = g_models[v].s.sz; return true; }

	// ---- Fundorte und Funde ------------------------------------------------------------------------
	struct Site { int x, y, z, v; };                                       // Kugel (Welt)
	static std::map<int64_t, Site> g_sites;                                // Schluessel: Kugel x/y
	static uint32_t g_seed = 0; static bool g_foundLoaded = false;
	static std::vector<std::pair<int, int>> g_found;
	static int64_t Key(int x, int y) { return ((int64_t)x << 32) ^ (uint32_t)y; }
	static std::string FoundFile(uint32_t seed) { char b[64]; sprintf_s(b, "AdventureMod_tempel_%08X.txt", seed); return b; }
	static void LoadFoundLocked(uint32_t seed) {
		g_found.clear(); g_seed = seed; g_foundLoaded = true;
		std::istringstream f(ModDb::Txt(FoundFile(seed))); std::string line;
		while (std::getline(f, line)) { int x, y; if (line.empty() || line[0] == '#') continue; if (sscanf_s(line.c_str(), "%d %d", &x, &y) == 2) g_found.push_back({ x, y }); }
	}
	static void SaveFoundLocked() {
		ModDb::Writer f(FoundFile(g_seed));
		f << "# Gefundene kleine Tempel dieser Welt (Kugel x y)\n";
		for (const auto& p : g_found) f << p.first << " " << p.second << "\n";
	}
	bool Stamp(void* zone, int v, int ox, int oy, int z, uint32_t worldSeed) {
		LabBridge::PreparedModel* p = nullptr; int mx = 0, my = 0, mz = 0;
		{ Lock l; LoadModels(); if (v < 0 || v >= (int)g_models.size()) return false; p = g_models[v].p; mx = g_models[v].ox; my = g_models[v].oy; mz = g_models[v].oz; }
		if (!p || LabBridge::StampPrepared(p, ox, oy, z, 0, 1, 0, zone) <= 0) return false;
		Lock l;
		if (worldSeed != g_seed) { g_sites.clear(); LoadFoundLocked(worldSeed); }
		Site s{ ox + mx, oy + my, z + mz, v };
		g_sites[Key(s.x, s.y)] = s;
		Tracking::Note(Tracking::T_TEMPLE, "Tempel", s.x, s.y, s.z, worldSeed);
		// Natives [R]-Ziel: Runenstein (Typ 0x2D, "[R] Teleport") in der Altarsaeule unter der Kugel (Boden + 1),
		// ohne Partner -> kein nativer Sprung; die Aktion (Fund zaehlen) macht Tick wie bisher.
		City::PushObject(zone, 0x2D, s.x + 0.5f, s.y + 0.5f, z + 1, 0, 1.4f, 1.0f, 1.5f);
		return true;
	}
	int Found() { Lock l; return (int)g_found.size(); }

	// ---- Spielthread -------------------------------------------------------------------------------
	typedef void* (__fastcall* WAssign_t)(void* str, void* edx, const wchar_t* s);
	typedef void  (__fastcall* WFree_t)(void* str, void* edx);
	typedef void  (__fastcall* ChatPrint_t)(void* chat, void* edx, void* wstr, void* color);
	static void ChatRaw(cube::GameController* gc, const wchar_t* text) {
		__try {
			if (!gc || !gc->ChatWidget) return;
			alignas(4) uint8_t w[0x18]; memset(w, 0, sizeof(w));
			((WAssign_t)Re(0x40EB60))(w, nullptr, text);
			float col[4] = { 1.0f, 0.85f, 0.45f, 1.0f };
			((ChatPrint_t)Re(0x43AB30))((void*)gc->ChatWidget, nullptr, w, col);
			((WFree_t)Re(0x593E50))(w, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void Chat(cube::GameController* gc, const std::string& s) { wchar_t w[256] = { 0 }; MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w, 255); ChatRaw(gc, w); }
	static bool PlayerRaw(cube::GameController* gc, int* x, int* y, int* z) {
		__try { uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false; *x = (int)(*(int64_t*)(c + 0x10) >> 16); *y = (int)(*(int64_t*)(c + 0x18) >> 16); *z = (int)(*(int64_t*)(c + 0x20) >> 16); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	typedef float (__fastcall* MaxHp_t)(void* c, void* edx);
	static bool BlessRaw(cube::GameController* gc) {
		__try { uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false; float mx = ((MaxHp_t)Re(0x444DB0))(c, nullptr); if (mx > 0.0f) *(float*)(c + 0x16C) = mx; *(float*)(c + 0x170) = 1.0f; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool Focus() {
		HWND fg = GetForegroundWindow(); DWORD pid = 0; if (!fg) return false;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId() && !LabWindow::IsForeground() && NativeUi::PlayInputCached();   // nur im Spiel (kein Chat/Menue)
	}
	static std::map<int, bool> g_down;
	static bool Pressed(int vk, bool focus) { bool d = focus && (GetAsyncKeyState(vk) & 0x8000) != 0; bool& w = g_down[vk]; bool e = d && !w; w = d; return e; }

	// Zielliste (eigene Biome)
	static bool g_listOpen = false, g_listShownHidden = true; static int g_sel = 0; static DWORD g_until = 0;
	static std::vector<std::string> g_dest; static std::vector<int> g_destIdx;
	bool ListOpen() { return g_listOpen; }
	static void OpenList(cube::GameController* gc) {
		g_dest.clear(); g_destIdx.clear();
		g_dest.push_back("Hochelfenkoenig"); g_destIdx.push_back(-1);                 // 2026-10-29: immer zuerst
		std::vector<BiomeGen::BiomeDef> defs = BiomeMod::GetDefinitions();
		for (size_t i = 0; i < defs.size(); i++) if (defs[i].enabled) { g_dest.push_back(defs[i].name); g_destIdx.push_back((int)i); }

		g_listOpen = true; g_sel = (std::min)(g_sel, (int)g_dest.size() - 1); g_until = GetTickCount() + 20000;
		Chat(gc, "Die Kugel oeffnet die Wege der Tempel: 6 = naechstes Ziel, P = teleportieren.");
	}
	void CityOrb(cube::GameController* gc) {
		int n = Found();
		if (n < kNeeded) { Chat(gc, "Die Kugel ruht. Finde kleine Tempel und beruehre ihre Kugeln: " + std::to_string(n) + " von " + std::to_string(kNeeded) + "."); return; }
		OpenList(gc);
	}
	// eigene Funktion: __try geht nicht in Funktionen mit Objekten, die abgebaut werden muessen (C2712)
	static uint32_t SeedRaw(cube::GameController* gc) {
		__try { return *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	void Tick(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		int px, py, pz; if (!PlayerRaw(gc, &px, &py, &pz)) return;
		uint32_t seed = SeedRaw(gc);
		Lan::SetWorldSeed(seed);
		{ Lock l; if (seed && (!g_foundLoaded || seed != g_seed)) { g_sites.clear(); LoadFoundLocked(seed); } }
		bool focus = Focus();
		DWORD now = GetTickCount();
		if (g_listOpen) {                                                          // 2026-10-29: Fenster im Spielstil statt D3DX-Text
			std::vector<std::string> rows = g_dest;
			NativeUi::ShowList(gc, "Tempelkugel", rows, g_sel, "6 = naechstes Ziel      P = teleportieren");
		}
		else if (!g_listShownHidden) { NativeUi::HideList(gc); g_listShownHidden = true; }
		if (g_listOpen) {
			g_listShownHidden = false;
			if ((int)(now - g_until) >= 0) g_listOpen = false;
			else if (Pressed('6', focus)) { g_sel = (g_sel + 1) % (int)g_dest.size(); g_until = now + 20000; }
			else if (Pressed('P', focus) && g_sel < (int)g_destIdx.size()) {
				if (g_destIdx[g_sel] < 0) { std::string r; City::VisitKing(gc, r); Chat(gc, r); }
				else {
					std::string r = BiomeMod::TeleportToNearest(gc, g_destIdx[g_sel]);
					Chat(gc, "Teleport nach " + g_dest[g_sel] + ": " + r);
				}
				g_listOpen = false;
			}
		}
		if (!Pressed('R', focus)) return;
		// kleiner Tempel in Reichweite?
		Site hit{}; bool any = false;
		{
			Lock l;
			for (const auto& kv : g_sites) {
				const Site& s = kv.second;
				if (std::abs(s.x - px) <= 6 && std::abs(s.y - py) <= 6 && std::abs(s.z - pz) <= 8) { hit = s; any = true; break; }
			}
		}
		if (!any) return;
		static DWORD lastBless = 0;
		bool fresh = false; int n = 0;
		{
			Lock l;
			bool known = false; for (const auto& f : g_found) if (f.first == hit.x && f.second == hit.y) known = true;
			if (!known) { g_found.push_back({ hit.x, hit.y }); SaveFoundLocked(); fresh = true; }
			n = (int)g_found.size();
		}
		if (n >= kNeeded) {                                                       // 2026-10-29: ab dem 25. Tempel bringt JEDE Kugel zum Koenig
			if (fresh) Chat(gc, "Tempel gefunden! " + std::to_string(n) + " von " + std::to_string(kNeeded) + ".");
			BlessRaw(gc); lastBless = now;
			std::string r; City::VisitKing(gc, r); Chat(gc, r);
			return;
		}
		if (fresh) {
			BlessRaw(gc); lastBless = now;
			Chat(gc, "Tempel gefunden! " + std::to_string(n) + " von " + std::to_string(kNeeded) +
			         (n >= kNeeded ? " -- die Kugeln der Stadttempel bringen dich jetzt in die Biome." : "."));

		} else {
			if (now - lastBless >= 60000 && BlessRaw(gc)) { lastBless = now; Chat(gc, "Die Kugel segnet dich (" + std::to_string(n) + "/" + std::to_string(kNeeded) + " Tempel)."); }
			else Chat(gc, "Diesen Tempel kennst du schon (" + std::to_string(n) + "/" + std::to_string(kNeeded) + ").");
		}
	}
	std::string Status() {
		Lock l; LoadModels();
		std::string r = "Kleine Tempel: " + std::to_string(g_models.size()) + " Modelle, " + std::to_string(g_sites.size()) + " in geladenen Zonen gesetzt, " +
		                std::to_string(g_found.size()) + " von " + std::to_string(kNeeded) + " gefunden.";
		int px = 0; (void)px;
		return r;
	}
}
