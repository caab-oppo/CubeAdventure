#include "ModSwitches.h"
#include "ModDb.h"
#include <windows.h>
#include <fstream>
#include <map>

namespace ModSwitches {
	static const char* kFile = "AdventureMod_schalter.cfg";
	namespace {
		CRITICAL_SECTION g_cs; volatile LONG g_init = 0; bool g_loaded = false;
		std::map<std::string, std::string> g_val;     // roh (auch Zahlen anderer Bausteine, z. B. tierwelt_anteil=50)
	}
	static void Init() {
		if (InterlockedCompareExchange(&g_init, 1, 0) == 0) { InitializeCriticalSection(&g_cs); InterlockedExchange(&g_init, 2); }
		while (g_init != 2) Sleep(0);
	}
	static DWORD g_loadedAt = 0;
	static void LoadLocked(bool force = false) {
		DWORD now = GetTickCount();
		if (g_loaded && !force && now - g_loadedAt < 2000) return;    // hoechstens alle 2 s neu (das LAB schreibt Zahlenwerte mit hinein)
		g_loaded = true; g_loadedAt = now;
		g_val.clear();
		std::istringstream f(ModDb::Txt(kFile)); std::string line;
		while (std::getline(f, line)) {
			size_t eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
			{ std::string v = line.substr(eq + 1); if (!v.empty() && v.back() == '\r') v.pop_back(); g_val[line.substr(0, eq)] = v; }
		}
	}
	bool Get(const char* name, bool def) {
		Init(); EnterCriticalSection(&g_cs); LoadLocked();
		auto it = g_val.find(name); bool r = it == g_val.end() ? def : atoi(it->second.c_str()) != 0;
		LeaveCriticalSection(&g_cs); return r;
	}
	void Set(const char* name, bool on) {
		Init(); EnterCriticalSection(&g_cs); LoadLocked(true);
		g_val[name] = on ? "1" : "0";
		ModDb::Writer f(kFile);
		f << "# AdventureMod: Schalter fuer die Fehlersuche (LAB -> Leistung ...). 1 = an, 0 = aus\n";
		for (const auto& p : g_val) f << p.first << "=" << p.second << "\n";
		LeaveCriticalSection(&g_cs);
	}
}
