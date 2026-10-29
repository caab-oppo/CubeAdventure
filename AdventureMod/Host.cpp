#include "Host.h"
#include <windows.h>
#include <cstring>
#include <iostream>

namespace Host {
	struct Pair { uint32_t c, s; };
	static const Pair kMap[] = {
#include "AddrMap_Server.inc"
	};
	static const int kN = (int)(sizeof(kMap) / sizeof(kMap[0]));

	static int g_server = -1;                       // -1 = noch nicht bestimmt
	static uint32_t g_base = 0;

	static bool DetectServer() {
		char path[MAX_PATH] = {};
		GetModuleFileNameA(NULL, path, MAX_PATH);
		const char* name = strrchr(path, '\\'); name = name ? name + 1 : path;
		if (_stricmp(name, "Server.exe") == 0) return true;
		if (_stricmp(name, "Cube.exe") == 0) return false;
		// Umbenannte Programme: an der Groesse des Codeabschnitts erkennen (Cube 0x2FAABF, Server 0x156665)
		uint8_t* b = (uint8_t*)GetModuleHandleA(NULL);
		__try {
			IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
			IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
			return sec->Misc.VirtualSize == 0x156665;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	bool IsServer() {
		if (g_server < 0) g_server = DetectServer() ? 1 : 0;
		return g_server == 1;
	}
	uint32_t Base() {
		if (!g_base) g_base = (uint32_t)GetModuleHandleA(NULL);
		return g_base;
	}
	static uint32_t Lookup(uint32_t a) {
		int lo = 0, hi = kN - 1;                     // Tabelle ist aufsteigend sortiert
		while (lo <= hi) {
			int m = (lo + hi) / 2;
			if (kMap[m].c == a) return kMap[m].s;
			if (kMap[m].c < a) lo = m + 1; else hi = m - 1;
		}
		return 0;
	}
	static volatile LONG g_missCount = 0;
	static uint32_t g_missSeen[64];
	static void NoteMiss(uint32_t a) {
		LONG n = g_missCount;
		for (LONG i = 0; i < n && i < 64; i++) if (g_missSeen[i] == a) return;
		LONG k = InterlockedIncrement(&g_missCount) - 1;
		if (k < 64) g_missSeen[k] = a;
		std::cout << "[Host] Cube.exe-Adresse 0x" << std::hex << a << std::dec
		          << " hat im Server kein Gegenstueck -- Funktion bleibt dort aus." << std::endl;
	}
	uint32_t Re(uint32_t a) {
		if (!IsServer()) return Base() + (a - 0x400000u);
		uint32_t s = Lookup(a);
		if (!s) { NoteMiss(a); return 0; }
		return Base() + (s - 0x400000u);
	}
	bool Has(uint32_t a) { return !IsServer() || Lookup(a) != 0; }
	int MappedCount() { int n = 0; for (int i = 0; i < kN; i++) if (kMap[i].s) n++; return n; }
	int TableSize() { return kN; }
	const char* Name() { return IsServer() ? "Server" : "Client"; }
}
