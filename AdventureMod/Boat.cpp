#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Boat.h"
#include "Host.h"
#include "ModDb.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <cstring>

namespace Boat {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	// Spieler (Kreatur)
	static const uint32_t P_SPECIAL = 0xF08, P_CURCNT = 0x11E8, P_CURITEM = 0x11EC, P_INV = 0x11DC, P_NAME = 0x1168, P_ACTION = 0x68, P_PHYS = 0x5C;
	static const uint32_t ITEM = 0x118;
	// GameController
	static const uint32_t G_PLAYER = 0x8006D0, G_INVOPEN = 0x8008F0, G_GUI = 0x800710, G_ROOT = 0x800884, G_EQW = 0x800A34, G_EQE = 0x800A28;
	static const uint8_t ACT_SAIL = 0x6B;

	typedef void* (__fastcall* Find_t)(void* root, void* edx, const void* wname);                       // 0x633D70, ret 4
	typedef void* (__fastcall* Clone_t)(void* tmpl, void* edx, void* parent);                           // 0x6326D0, ret 4
	typedef void  (__fastcall* SetChild_t)(void* w, void* edx, const void* wname, void* el, int f);      // 0x631460, ret 0xC
	typedef void* (__fastcall* Make_t)(void* gui, void* edx, int a, int b, int c, int d, const void* ws); // 0x64F4E0, ret 0x14
	typedef void* (__fastcall* SlotCtor_t)(void* el, void* edx, void* gui, void* base, void* gc);        // 0x51C310, ret 0xC
	typedef int   (__fastcall* Icon_t)(void* reg, void* edx, const void* item);                         // 0x4EC400, ret 4
	typedef void  (__fastcall* Sound_t)(void* gc, void* edx, int id, void* pos, float a, float b);      // 0x484350, ret 0x10
	typedef void  (__fastcall* AddInv_t)(void* inv, void* edx, const void* item, int slot);             // 0x46EBE0
	typedef char  (__fastcall* Hover_t)(void* el, void* edx);                                           // 0x6294C0
	typedef void  (__fastcall* SetVis_t)(void* w, void* edx, int v);                                    // 0x411A90
	typedef void  (__fastcall* WCtor_t)(void* ws, void* edx, const wchar_t* s);                         // 0x40EB60
	typedef void  (__fastcall* WDtor_t)(void* ws, void* edx);                                           // 0x593E50
	typedef void* (__cdecl* New_t)(unsigned);                                                           // 0x68D668
	typedef void  (__fastcall* WRefresh_t)(void* obj, void* edx, int f);

	alignas(8) static uint8_t g_boat[ITEM];               // Inhalt des Boot-Platzes
	static uint8_t* g_widget = nullptr; static uint8_t* g_elem = nullptr;
	static volatile LONG g_uiPatched = 0, g_keyPatched = 0, g_made = 0;
	static std::string g_char; static bool g_dirty = false; static bool g_loaded = false;
	static bool g_lWas = false, g_rWas = false;
	static DWORD g_landSince = 0;

	static bool IsBoat(const uint8_t* it) { return it[0] == 0x17 && it[1] == 1; }
	static bool IsGlider(const uint8_t* it) { return it[0] == 0x17 && it[1] == 0; }
	static void Swap(uint8_t* a, uint8_t* b) { uint8_t t[ITEM]; memcpy(t, a, ITEM); memcpy(a, b, ITEM); memcpy(b, t, ITEM); }

	// ---- Speichern je Charakter: Charakter-Datenbank des Spiels ("AdventureMod:<Name>:bootplatz", 0x118 Bytes hex) ----
	static cube::GameController* g_gc = nullptr;
	static std::string Hex() { static const char* hx = "0123456789ABCDEF"; std::string h; for (int i = 0; i < (int)ITEM; i++) { h += hx[g_boat[i] >> 4]; h += hx[g_boat[i] & 15]; } return h; }
	static void FromHex(const std::string& h) {
		memset(g_boat, 0, ITEM); if (h.size() < ITEM * 2) return;
		auto v = [](char c) { return c >= 'A' ? c - 'A' + 10 : c - '0'; };
		for (int i = 0; i < (int)ITEM; i++) g_boat[i] = (uint8_t)(v(h[2 * i]) * 16 + v(h[2 * i + 1]));
	}
	static void Save() {
		if (g_char.empty() || !g_gc || ModDb::CharName(g_gc) != g_char) return;
		if (ModDb::CharWrite(g_gc, "bootplatz", Hex())) g_dirty = false;
	}
	static void Load(cube::GameController* gc, const std::string& who) {
		g_gc = gc;
		std::string h;
		if (ModDb::CharRead(gc, "bootplatz", h)) FromHex(h);
		else {                                                                // alte Datei: Zeilen "Name=hex"
			memset(g_boat, 0, ITEM);
			std::istringstream f(ModDb::Txt("AdventureMod_bootplatz.txt")); std::string l;
			while (std::getline(f, l)) { size_t e = l.find('='); if (e != std::string::npos && l.substr(0, e) == who) FromHex(l.substr(e + 1)); }
			g_dirty = true;
		}
		g_char = who; g_loaded = true;
		std::cout << "[Boot] Boot-Platz fuer '" << who << "': " << (IsBoat(g_boat) ? "Boot" : IsGlider(g_boat) ? "Gleitschirm" : g_boat[0] ? "Gegenstand" : "leer") << std::endl;
	}
	static bool NameRaw(cube::GameController* gc, char* out) {
		__try {
			uint8_t* p = *(uint8_t**)((uint8_t*)gc + G_PLAYER); if (!p) return false;
			memcpy(out, p + P_NAME, 16); out[16] = 0; return out[0] != 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- [G]: vor der nativen Spezial-Taste (Hoehle an 0x47E83C) -----------------------------------------------
	static void __cdecl SpecialKeyC(uint8_t* gc) {
		__try {
			uint8_t* p = *(uint8_t**)(gc + G_PLAYER); if (!p) return;
			uint8_t* sp = p + P_SPECIAL;
			if (p[P_ACTION] == ACT_SAIL) return;                                 // segelt: das Spiel steigt ab
			const bool swim = (p[P_PHYS] & 2) != 0;
			if (swim && !IsBoat(sp) && IsBoat(g_boat)) { Swap(sp, g_boat); g_dirty = true; }                 // Boot nehmen
			else if (!swim && IsBoat(sp) && IsGlider(g_boat)) { Swap(sp, g_boat); g_dirty = true; }          // an Land: Gleitschirm
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void InstallKeyPatch() {
		if (g_keyPatched) return;
		static const uint8_t orig[6] = { 0x8B, 0xB7, 0xD0, 0x06, 0x80, 0x00 };   // mov esi,[edi+0x8006D0]
		uint8_t* site = (uint8_t*)(uintptr_t)Re(0x47E83C);
		bool same = false; __try { same = site && memcmp(site, orig, 6) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (!same) { InterlockedExchange(&g_keyPatched, -1); std::cout << "[Boot] 0x47E83C weicht ab -- G waehlt das Boot nicht automatisch." << std::endl; return; }
		uint8_t* c = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); if (!c) return;
		int k = 0;
		c[k++] = 0x60; c[k++] = 0x57;                                            // pushad; push edi (GameController)
		c[k++] = 0xE8; *(int32_t*)(c + k) = (int32_t)((uintptr_t)&SpecialKeyC - ((uintptr_t)c + k + 4)); k += 4;
		c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x04; c[k++] = 0x61;               // add esp,4; popad
		memcpy(c + k, orig, 6); k += 6;                                          // mov esi,[edi+0x8006D0]
		c[k++] = 0xE9; *(int32_t*)(c + k) = (int32_t)(Re(0x47E842) - ((uint32_t)(uintptr_t)c + k + 4)); k += 4;
		DWORD old; VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old);
		site[0] = 0xE9; *(int32_t*)(site + 1) = (int32_t)((uintptr_t)c - ((uintptr_t)site + 5)); site[5] = 0x90;
		DWORD d2; VirtualProtect(site, 6, old, &d2); FlushInstructionCache(GetCurrentProcess(), site, 6);
		InterlockedExchange(&g_keyPatched, 1);
		std::cout << "[Boot] G im Wasser waehlt das Boot (Hoehle an 0x47E83C)." << std::endl;
	}

	// ---- Inventarfenster: zweiter Spezial-Platz (Aufruf aus der Ausruestungs-Aktualisierung, 0x48CBBA) ---------
	static float* WPos(uint8_t* w) { uint8_t* obj = *(uint8_t**)(w + 0x38); if (!obj) return nullptr; uint8_t* f = obj + 0x48; uint8_t* arr = *(uint8_t**)(f + 0x4C); int i = *(int*)(f + 0x20); return (arr && i >= 0 && i < 4096) ? (float*)(arr + i * 8) : nullptr; }
	static void WSetPos(uint8_t* w, float x, float y) { float* p = WPos(w); if (!p) return; p[0] = x; p[1] = y; uint8_t* obj = *(uint8_t**)(w + 0x38); void** vt = *(void***)obj; ((WRefresh_t)vt[1])(obj, nullptr, 1); }
	// Knoten (plasma::Node, Infinite-Cubes alpha/plasma): Name (std::wstring) +0x0C, Kinder std::list +0x2C,
	// m_transformation +0x38, m_display +0x3C, m_widget +0x40, m_flags +0xC8 (Bit 2 = NODE_HIDDEN).
	static bool NodeNameIs(uint8_t* n, const wchar_t* want) {
		uint8_t* ws = n + 0x0C; size_t len = *(size_t*)(ws + 0x10), cap = *(size_t*)(ws + 0x14);
		const wchar_t* p = cap >= 8 ? *(const wchar_t**)ws : (const wchar_t*)ws;
		return p && len == wcslen(want) && wcsncmp(p, want, len) == 0;
	}
	static int Kids(uint8_t* n, uint8_t** out, int maxN) {
		uint8_t* head = *(uint8_t**)(n + 0x2C); if (!head) return 0;
		int k = 0; for (uint8_t* nd = *(uint8_t**)head; nd && nd != head && k < maxN; nd = *(uint8_t**)nd) out[k++] = *(uint8_t**)(nd + 8);
		return k;
	}
	static uint8_t* FindNode(uint8_t* n, const wchar_t* name, int depth) {
		if (NodeNameIs(n, name)) return n;
		if (depth > 6) return nullptr;
		uint8_t* k[32]; int c = Kids(n, k, 32);
		for (int i = 0; i < c; i++) if (uint8_t* r = FindNode(k[i], name, depth + 1)) return r;
		return nullptr;
	}
	typedef void (__fastcall* SetText_t)(void* node, void* edx, const void* ws);                        // 0x636AD0 Node::setText
	// Wie GameController-ctor (Infinite-Cubes GameController.cpp: equipmentboxleft/right -> clone -> moveIntoWidget
	// "background" -> SpriteWidget). Die Vorlagen loescht das Spiel danach; darum Klon des Spezial-Kastens
	// (m_equipmentBoxes[10] = GC+0x800A34) -- dessen alter Feld-Knoten (Kopie des Spezial-Bildes) wird ausgeblendet.
	static void MakeSlotRaw(uint8_t* G) {
		alignas(4) uint8_t s2[32] = {}, s3[32] = {}, s4[32] = {};
		void* gui = *(void**)(G + G_GUI); void* root = *(void**)(G + G_ROOT);
		uint8_t** wb = *(uint8_t***)(G + G_EQW);
		uint8_t* tmpl = wb ? wb[10] : nullptr;
		if (!tmpl) return;
		((WCtor_t)Re(0x40EB60))(s2, nullptr, L"");
		void* base = ((Make_t)Re(0x64F4E0))(gui, nullptr, 0, 0, 0, 0, s2);
		((WDtor_t)Re(0x593E50))(s2, nullptr);
		uint8_t* el = (uint8_t*)((New_t)Re(0x68D668))(0x16C);
		if (!el) return;
		el = (uint8_t*)((SlotCtor_t)Re(0x51C310))(el, nullptr, gui, base, G);
		uint8_t* w = (uint8_t*)((Clone_t)Re(0x6326D0))(tmpl, nullptr, root);
		if (!w || !el) return;
		uint8_t* bg = FindNode(w, L"background", 0);
		if (bg) { uint8_t* k[16]; int c = Kids(bg, k, 16); for (int i = 0; i < c; i++) *(uint32_t*)(k[i] + 0xC8) |= 4u; }   // kopiertes Spezial-Bild aus
		((WCtor_t)Re(0x40EB60))(s3, nullptr, L"background");
		((SetChild_t)Re(0x631460))(w, nullptr, s3, el, 1);
		((WDtor_t)Re(0x593E50))(s3, nullptr);
		((WCtor_t)Re(0x40EB60))(s4, nullptr, L"Boot");
		((SetText_t)Re(0x636AD0))(w, nullptr, s4);
		((WDtor_t)Re(0x593E50))(s4, nullptr);
		g_widget = w; g_elem = el;
	}
	static void UiRaw(uint8_t* G) {
		uint8_t* p = *(uint8_t**)(G + G_PLAYER); if (!p) return;
		uint8_t** wb = *(uint8_t***)(G + G_EQW); uint8_t** we = *(uint8_t***)(G + G_EQW + 4);
		if (!wb || we - wb < 12) return;
		if (!g_made) { InterlockedExchange(&g_made, 1); MakeSlotRaw(G); std::cout << "[Boot] Boot-Platz im Inventar " << (g_widget ? "angelegt" : "NICHT angelegt") << "." << std::endl; }
		if (!g_widget || !g_elem) return;
		const int open = *(G + G_INVOPEN) != 0 ? 1 : 0;
		((SetVis_t)Re(0x411A90))(g_widget, nullptr, open);
		if (!open) return;
		// Lage wie GameController::resize (Spalten 400 px auseinander, 56 px je Zeile): Spezial = Index 10 (linke Spalte,
		// Zeile 5) -> Boot-Platz eine Zeile darunter (Zeile 6), gleiche Spalte. Aus den Nachbarn abgeleitet (Pivot gleich).
		float* p10 = WPos(wb[10]); float* p8 = WPos(wb[8]);
		if (p10 && p8) WSetPos(g_widget, p10[0], p10[1] + (p10[1] - p8[1]));
		*(int*)(g_elem + 0x160) = ((Icon_t)Re(0x4EC400))(G + 0x300, nullptr, g_boat);
		*(uint8_t**)(g_elem + 0x164) = g_boat;
		// Klicks
		bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		bool hov = ((Hover_t)Re(0x6294C0))(g_elem, nullptr) != 0;
		if (hov && l && !g_lWas) {
			int& cnt = *(int*)(p + P_CURCNT); uint8_t* cur = p + P_CURITEM;
			bool ok = (cnt == 0 && g_boat[0]) || (cnt == 1 && IsBoat(cur));
			if (ok) {
				Swap(g_boat, cur);
				cnt = cur[0] ? 1 : 0; if (!cur[0]) memset(cur, 0, ITEM);
				g_dirty = true;
				((Sound_t)Re(0x484350))(G, nullptr, cnt ? 0x57 : 0x58, G + 0x140, 1.0f, 1.0f);
			}
		}
		if (hov && r && !g_rWas && g_boat[0] && *(int*)(p + P_CURCNT) == 0) {
			((AddInv_t)Re(0x46EBE0))(p + P_INV, nullptr, g_boat, -1);
			memset(g_boat, 0, ITEM); g_dirty = true;
			((Sound_t)Re(0x484350))(G, nullptr, 0x58, G + 0x140, 1.0f, 1.0f);
		}
		g_lWas = l; g_rWas = r;
	}
	static void __cdecl UiC(uint8_t* G) { __try { UiRaw(G); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void InstallUiPatch() {
		if (g_uiPatched) return;
		static const uint8_t orig[7] = { 0x8B, 0x07, 0x8B, 0xCF, 0x8B, 0x40, 0x04 };   // mov eax,[edi]; mov ecx,edi; mov eax,[eax+4]
		uint8_t* site = (uint8_t*)(uintptr_t)Re(0x48CBBA);
		bool same = false; __try { same = site && memcmp(site, orig, 7) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (!same) { InterlockedExchange(&g_uiPatched, -1); std::cout << "[Boot] 0x48CBBA weicht ab -- kein Boot-Platz im Inventar." << std::endl; return; }
		uint8_t* c = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); if (!c) return;
		int k = 0;
		c[k++] = 0x60; c[k++] = 0x57;                                            // pushad; push edi (GameController)
		c[k++] = 0xE8; *(int32_t*)(c + k) = (int32_t)((uintptr_t)&UiC - ((uintptr_t)c + k + 4)); k += 4;
		c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x04; c[k++] = 0x61;               // add esp,4; popad
		memcpy(c + k, orig, 7); k += 7;                                          // ersetzte Befehle
		c[k++] = 0xC3;                                                           // ret -> 0x48CBBF (2 x nop, call eax)
		DWORD old; VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &old);
		site[0] = 0xE8; *(int32_t*)(site + 1) = (int32_t)((uintptr_t)c - ((uintptr_t)site + 5)); site[5] = 0x90; site[6] = 0x90;
		DWORD d2; VirtualProtect(site, 7, old, &d2); FlushInstructionCache(GetCurrentProcess(), site, 7);
		InterlockedExchange(&g_uiPatched, 1);
		std::cout << "[Boot] Inventarfenster: zweiter Spezial-Platz (Boot) eingehaengt (0x48CBBA)." << std::endl;
	}

	// ---- Spielthread ---------------------------------------------------------------------------------------------
	static void TickRaw(uint8_t* G, bool* landSwap) {
		uint8_t* p = *(uint8_t**)(G + G_PLAYER); if (!p) return;
		if (g_widget && *(G + G_INVOPEN) == 0) ((SetVis_t)Re(0x411A90))(g_widget, nullptr, 0);
		uint8_t* sp = p + P_SPECIAL;
		const bool swim = (p[P_PHYS] & 2) != 0, sail = p[P_ACTION] == ACT_SAIL;
		// an Land: Gleitschirm zurueck in den Spezial-Platz (1 s nach dem Aussteigen)
		if (!swim && !sail && IsBoat(sp) && IsGlider(g_boat)) {
			if (!g_landSince) g_landSince = GetTickCount();
			else if (GetTickCount() - g_landSince > 1000) { Swap(sp, g_boat); g_dirty = true; *landSwap = true; g_landSince = 0; }
		} else g_landSince = 0;
	}
	static bool TickSeh(uint8_t* G, bool* landSwap) { __try { TickRaw(G, landSwap); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	void Tick(cube::GameController* gc) {
		if (!gc) return;
		InstallKeyPatch(); InstallUiPatch();
		char nm[17]; if (NameRaw(gc, nm) && (!g_loaded || g_char != nm)) { if (g_dirty) Save(); Load(gc, nm); }
		bool landSwap = false; TickSeh((uint8_t*)gc, &landSwap);
		if (g_dirty && g_loaded) Save();
	}
	std::string Status() {
		char b[200]; sprintf_s(b, "Boot-Platz: %s (Taste G %s, Inventar %s)", IsBoat(g_boat) ? "Boot" : IsGlider(g_boat) ? "Gleitschirm (getauscht)" : g_boat[0] ? "Gegenstand" : "leer",
		                       g_keyPatched == 1 ? "aktiv" : "FEHLT", g_uiPatched == 1 ? (g_widget ? "aktiv" : "wartet") : "FEHLT");
		return b;
	}
}
