// TierGelenke.cpp -- siehe TierGelenke.h
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TierGelenke.h"
#include "Host.h"
#include "MobLab.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdlib>

namespace TierGelenke {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	static const uint32_t A_ROTATE = 0x4241B0;
	static volatile uint8_t g_race[1024];                 // 1 = Rasse hat Gelenke
	static volatile LONG g_count = 0, g_patched = 0, g_calls = 0;
	typedef void (__fastcall* Rot_t)(void* m, void* edx, float a, float x, float y, float z);

	bool RaceHasJoints(int race) { return race >= 0 && race < 1024 && g_race[race] != 0; }

	// Kreatur aus der Matrix (Matrix liegt bei AnimationState + off, AnimationState+0 = Kreatur)
	static uint8_t* CreatureOf(void* m, uint32_t off) {
		__try { uint8_t* c = *(uint8_t**)((uint8_t*)m - off); return c; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static bool JointsRaw(uint8_t* c, float* feetRot) {
		__try { int r = *(int*)(c + 0x64); if (!RaceHasJoints(r)) return false; if (feetRot) *feetRot = *(float*)(c + 0xD0); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void __fastcall RotHandL(void* m, void* edx, float a, float x, float y, float z) {
		uint8_t* c = CreatureOf(m, 0x32C); if (c && JointsRaw(c, nullptr)) { a = 0.0f; InterlockedIncrement(&g_calls); }
		((Rot_t)Re(A_ROTATE))(m, edx, a, x, y, z);
	}
	static void __fastcall RotHandR(void* m, void* edx, float a, float x, float y, float z) {
		uint8_t* c = CreatureOf(m, 0x36C); if (c && JointsRaw(c, nullptr)) a = 0.0f;
		((Rot_t)Re(A_ROTATE))(m, edx, a, x, y, z);
	}
	static void __fastcall RotFootL(void* m, void* edx, float a, float x, float y, float z) {
		float fr = 0; uint8_t* c = CreatureOf(m, 0x4AC); if (c && JointsRaw(c, &fr)) a = fr;      // nur feetRotation bleibt
		((Rot_t)Re(A_ROTATE))(m, edx, a, x, y, z);
	}
	static void __fastcall RotFootR(void* m, void* edx, float a, float x, float y, float z) {
		uint8_t* c = CreatureOf(m, 0x4EC); if (c && JointsRaw(c, nullptr)) a = 0.0f;
		((Rot_t)Re(A_ROTATE))(m, edx, a, x, y, z);
	}

	static int ScanDir(const std::string& dir, int depth) {
		if (depth > 4) return 0;
		int n = 0; WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return 0;
		do {
			std::string nm = fd.cFileName; if (nm == "." || nm == "..") continue;
			std::string p = dir + "\\" + nm;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(nm.c_str(), "Teile") && _stricmp(nm.c_str(), "Vorschau")) n += ScanDir(p, depth + 1); continue; }
			if (nm.size() < 5 || _stricmp(nm.c_str() + nm.size() - 4, ".mob")) continue;
			std::ifstream f(p); std::string l; int race = -1; bool setRace = true, joints = false;
			while (std::getline(f, l)) {
				if (!l.empty() && l.back() == '\r') l.pop_back();
				if (l.compare(0, 13, "eigene_rasse=") == 0) race = atoi(l.c_str() + 13);
				else if (l.compare(0, 13, "rasse_setzen=") == 0) setRace = atoi(l.c_str() + 13) != 0;
				else if (l.compare(0, 7, "gelenk=") == 0) joints = true;
			}
			if (joints && setRace && race >= 200 && race < 1024) { g_race[race] = 1; n++; }
		} while (FindNextFileA(h, &fd));
		FindClose(h);
		return n;
	}
	void Reload() {
		for (int i = 0; i < 1024; i++) g_race[i] = 0;
		int n = ScanDir(MobLab::MobsDir(), 0);
		InterlockedExchange(&g_count, n);
		std::cout << "[Gelenke] " << n << " eigene Rassen mit Bein-Gelenken." << std::endl;
	}
	static bool PatchCall(uint32_t site, void* target) {
		uint8_t* p = (uint8_t*)(uintptr_t)Re(site);
		__try {
			if (p[0] != 0xE8) return false;
			uint32_t dst = (uint32_t)(uintptr_t)p + 5 + *(int32_t*)(p + 1);
			if (dst != Re(A_ROTATE)) return false;
			DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
			*(int32_t*)(p + 1) = (int32_t)((uintptr_t)target - ((uintptr_t)p + 5));
			DWORD o2; VirtualProtect(p, 5, old, &o2); FlushInstructionCache(GetCurrentProcess(), p, 5);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void Install() {
		if (g_patched) return;
		Reload();
		int ok = 0;
		ok += PatchCall(0x423059, (void*)&RotHandL);
		ok += PatchCall(0x423405, (void*)&RotHandR);
		ok += PatchCall(0x423957, (void*)&RotFootL);
		ok += PatchCall(0x423B83, (void*)&RotFootR);
		InterlockedExchange(&g_patched, ok);
		std::cout << "[Gelenke] " << ok << "/4 Bein-Drehungen umgebogen" << (ok == 4 ? "." : " -- Stellen weichen ab, Gelenke wirken nicht voll.") << std::endl;
	}
	std::string Status() {
		char b[160]; sprintf_s(b, "Gelenke: %ld Rassen, %ld/4 Aufrufe umgebogen, %ld Vorderbein-Aufrufe mit Gelenk.", g_count, g_patched, g_calls);
		return b;
	}
}
