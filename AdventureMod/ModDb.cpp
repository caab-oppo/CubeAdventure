#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ModDb.h"
#include "Host.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace ModDb {
	typedef int (__cdecl* Open_t)(const char*, void**);
	typedef int (__cdecl* Exec_t)(void*, const char*, void*, void*, char**);
	typedef int (__cdecl* Prep_t)(void*, const char*, int, void**, const char**);
	typedef int (__cdecl* BindT_t)(void*, int, const char*, int, void*);
	typedef int (__cdecl* BindB_t)(void*, int, const void*, int, void*);
	typedef int (__cdecl* Step_t)(void*);
	typedef int (__cdecl* Fin_t)(void*);
	typedef const void* (__cdecl* ColB_t)(void*, int);
	typedef int (__cdecl* ColN_t)(void*, int);
	struct Api { Open_t open; Exec_t exec; Prep_t prep; BindT_t bt; BindB_t bb; Step_t step; Fin_t fin; ColB_t cb; ColN_t cn; bool ok; };
	static Api g_api = {};
	static void* g_db = nullptr; static volatile LONG g_state = 0;   // 0 neu, 1 offen, -1 Dateien
	static CRITICAL_SECTION g_cs; static volatile LONG g_csInit = 0;
	static const char* kPath = "Save/AdventureMod.db";
	static volatile LONG c_reads = 0, c_writes = 0, c_imports = 0, c_charW = 0;

	static void Cs() { if (g_csInit == 1) return; if (InterlockedCompareExchange(&g_csInit, 2, 0) == 0) { InitializeCriticalSection(&g_cs); InterlockedExchange(&g_csInit, 1); } else while (g_csInit != 1) Sleep(0); }
	struct Lock { Lock() { Cs(); EnterCriticalSection(&g_cs); } ~Lock() { LeaveCriticalSection(&g_cs); } };
	static bool LoadApi() {
		if (g_api.ok) return true;
		const uint32_t a[9] = { 0x525590, 0x520760, 0x526480, 0x526B60, 0x5267A0, 0x527300, 0x528070, 0x527590, 0x527610 };
		for (uint32_t x : a) if (!Host::Has(x)) return false;
		g_api.open = (Open_t)Host::Re(a[0]); g_api.exec = (Exec_t)Host::Re(a[1]); g_api.prep = (Prep_t)Host::Re(a[2]);
		g_api.bt = (BindT_t)Host::Re(a[3]); g_api.bb = (BindB_t)Host::Re(a[4]); g_api.step = (Step_t)Host::Re(a[5]);
		g_api.fin = (Fin_t)Host::Re(a[6]); g_api.cb = (ColB_t)Host::Re(a[7]); g_api.cn = (ColN_t)Host::Re(a[8]);
		g_api.ok = true; return true;
	}
	// ---- rohe SQLite-Aufrufe (SEH, ohne Objekte) ----------------------------------------------------------------
	static int OpenRaw(void** db) {
		__try {
			CreateDirectoryA("Save", nullptr);
			if (g_api.open(kPath, db) != 0 || !*db) return 0;
			g_api.exec(*db, "CREATE TABLE IF NOT EXISTS blobs(key TEXT PRIMARY KEY, value BLOB);", nullptr, nullptr, nullptr);
			return 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	// Lesen: Ergebnis in malloc-Puffer (*out, *n); 1 = gefunden, 0 = nicht da, -1 = Fehler
	static int GetRaw(void* db, const char* key, char** out, int* n) {
		*out = nullptr; *n = 0;
		__try {
			void* st = nullptr;
			if (g_api.prep(db, "SELECT value FROM blobs WHERE key = ?", -1, &st, nullptr) != 0 || !st) return -1;
			g_api.bt(st, 1, key, -1, nullptr);
			int rc = g_api.step(st), res = 0;
			if (rc == 100) {
				int len = g_api.cn(st, 0); const void* p = g_api.cb(st, 0);
				char* b = (char*)malloc((size_t)len + 1);
				if (b) { if (len > 0 && p) memcpy(b, p, (size_t)len); b[len] = 0; *out = b; *n = len; res = 1; }
			} else if (rc != 101) res = -1;
			g_api.fin(st);
			return res;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	static int PutRaw(void* db, const char* key, const char* data, int n) {
		__try {
			void* st = nullptr;
			if (g_api.prep(db, "INSERT OR REPLACE INTO blobs(key, value) VALUES(?, ?)", -1, &st, nullptr) != 0 || !st) return 0;
			g_api.bt(st, 1, key, -1, nullptr);
			g_api.bb(st, 2, data, n, nullptr);
			int rc = g_api.step(st);
			g_api.fin(st);
			return rc == 101 ? 1 : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	static bool Ready() {
		if (g_state == 1) return true;
		if (g_state < 0) return false;
		if (!LoadApi()) { InterlockedExchange(&g_state, -1); std::cout << "[ModDb] SQLite des Spiels nicht gefunden (Server?) -- Dateien wie bisher." << std::endl; return false; }
		void* db = nullptr;
		if (!OpenRaw(&db)) { InterlockedExchange(&g_state, -1); std::cout << "[ModDb] Save/AdventureMod.db nicht zu oeffnen -- Dateien wie bisher." << std::endl; return false; }
		g_db = db; InterlockedExchange(&g_state, 1);
		std::cout << "[ModDb] Eigene Datenbank Save/AdventureMod.db offen." << std::endl;
		return true;
	}
	static bool FileRead(const std::string& name, std::string& out) {
		std::ifstream f(name, std::ios::binary); if (!f) return false;
		std::ostringstream s; s << f.rdbuf(); out = s.str(); return true;
	}
	// Grundeinstellungen (2026-10-29): Gibt es eine .cfg noch nicht (neue Installation), gilt die mitgelieferte Vorlage
	// Strukturen\Grundeinstellungen\<Name> und wird in die Datenbank uebernommen. Das LAB schreibt die Vorlagen
	// ("Als Grundeinstellung speichern") und kann zu ihnen zurueck ("Grundeinstellungen laden").
	static bool DefaultRead(const std::string& name, std::string& out) {
		if (name.size() < 4 || _stricmp(name.c_str() + name.size() - 4, ".cfg") != 0) return false;
		return FileRead(DefaultsPath(name), out);
	}
	std::string DefaultsPath(const std::string& name) { return "Strukturen\\Grundeinstellungen\\" + name; }
	bool ReadText(const std::string& name, std::string& out) {
		Lock l;
		if (!Ready()) return FileRead(name, out) || DefaultRead(name, out);
		char* b = nullptr; int n = 0;
		int r = GetRaw(g_db, name.c_str(), &b, &n);
		InterlockedIncrement(&c_reads);
		if (r == 1) { out.assign(b, (size_t)n); free(b); return true; }
		if (b) free(b);
		// alte Datei uebernehmen (einmalig), dann umbenennen
		std::string old;
		if (!FileRead(name, old)) {
			if (!DefaultRead(name, old)) return false;
			PutRaw(g_db, name.c_str(), old.data(), (int)old.size());
			std::cout << "[ModDb] " << name << ": Grundeinstellung aus " << DefaultsPath(name) << " uebernommen." << std::endl;
			out = old; return true;
		}
		if (PutRaw(g_db, name.c_str(), old.data(), (int)old.size())) {
			MoveFileExA(name.c_str(), (name + ".alt").c_str(), MOVEFILE_REPLACE_EXISTING);
			InterlockedIncrement(&c_imports);
			std::cout << "[ModDb] " << name << " in die Datenbank uebernommen (Datei -> " << name << ".alt)." << std::endl;
		}
		out = old; return true;
	}
	void WriteText(const std::string& name, const std::string& content) {
		Lock l;
		if (!Ready()) { std::ofstream f(name, std::ios::binary | std::ios::trunc); f << content; return; }
		PutRaw(g_db, name.c_str(), content.data(), (int)content.size());
		InterlockedIncrement(&c_writes);
	}
	void AppendText(const std::string& name, const std::string& line) {
		std::string cur; ReadText(name, cur);
		if (!cur.empty() && cur.back() != '\n') cur += '\n';
		cur += line; if (cur.empty() || cur.back() != '\n') cur += '\n';
		WriteText(name, cur);
	}
	// ---- Charakter-Datenbank des Spiels -------------------------------------------------------------------------
	static void* CharDbRaw(cube::GameController* gc) { __try { return *(void**)((uint8_t*)gc + 0x100100C); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
	static void NameRaw(cube::GameController* gc, char* n) { __try { uint8_t* p = *(uint8_t**)((uint8_t*)gc + 0x8006D0); if (p) memcpy(n, p + 0x1168, 16); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	std::string CharName(cube::GameController* gc) { char n[17] = {}; if (gc) NameRaw(gc, n); return n; }
	bool CharRead(cube::GameController* gc, const std::string& field, std::string& out) {
		if (!gc || !LoadApi()) return false;
		void* db = CharDbRaw(gc); std::string who = CharName(gc);
		if (!db || who.empty()) return false;
		std::string key = "AdventureMod:" + who + ":" + field;
		char* b = nullptr; int n = 0;
		int r = GetRaw(db, key.c_str(), &b, &n);
		if (r == 1) { out.assign(b, (size_t)n); free(b); return true; }
		if (b) free(b);
		return false;
	}
	bool CharWrite(cube::GameController* gc, const std::string& field, const std::string& content) {
		if (!gc || !LoadApi()) return false;
		void* db = CharDbRaw(gc); std::string who = CharName(gc);
		if (!db || who.empty()) return false;
		std::string key = "AdventureMod:" + who + ":" + field;
		bool ok = PutRaw(db, key.c_str(), content.data(), (int)content.size()) != 0;
		if (ok) InterlockedIncrement(&c_charW);
		return ok;
	}
	std::string Status() {
		char b[200]; sprintf_s(b, "Speicher: %s, %ld gelesen, %ld geschrieben, %ld Dateien uebernommen, %ld Charakter-Eintraege geschrieben.",
		                       g_state == 1 ? "Save/AdventureMod.db" : g_state < 0 ? "Dateien (keine Datenbank)" : "noch nicht geoeffnet", c_reads, c_writes, c_imports, c_charW);
		return b;
	}
}
