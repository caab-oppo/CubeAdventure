#include "Effects.h"
#include "Host.h"
#include "Recipes.h"
#include "MinHook.h"
#include <windows.h>
#include <d3d9.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>

// Herleitung aller Adressen: Effects.h.
//
// Diese Werte liest der Maschinencode direkt (Stub bei 0x619DCC, Lesestelle 0x59D65B) -- deshalb
// ausserhalb des Namensraums und als einfache Typen.
static void*    g_fxSpeedPlayer = nullptr;     // Kreatur, deren Laufgeschwindigkeit multipliziert wird
static float    g_fxSpeedMul = 1.0f;
static uint32_t g_fxFindByType = 0;            // list_findByTypeByte 0x443F10 (rebased)
static int      g_fxLightTime = 43200000;      // Tageszeit fuer die Lichtfarben (ms, 12:00)

// Ersetzt bei 0x619DCC: push 0xC / mov ecx,esi / call 0x443F10 (9 Byte) -> call SpeedStub + 4x nop.
// esi = aktuelle Kreatur, [ebp-0x2B60] = ihre Laufgeschwindigkeit (float). xmm0 ist hinter einem
// Aufruf ohnehin frei (der Originalcode rief hier eine Funktion auf).
static __declspec(naked) void SpeedStub() {
	__asm {
		mov eax, g_fxSpeedPlayer
		test eax, eax
		jz orig
		cmp esi, eax
		jne orig
		movss xmm0, dword ptr [ebp - 0x2B60]
		mulss xmm0, dword ptr [g_fxSpeedMul]
		movss dword ptr [ebp - 0x2B60], xmm0
	orig:
		push 0x0C
		mov ecx, esi
		call dword ptr [g_fxFindByType]
		ret
	}
}

namespace Effects {
	using namespace Recipes;
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	static const uint32_t A_WDMG = 0x43DC60, A_APOW = 0x446E80, A_SPELL = 0x43E190, A_ARMOR = 0x43CFF0, A_SPIRIT = 0x4467A0;
	static const uint32_t A_MAXHP = 0x444DB0, A_HEALVAL = 0x4C6E10, A_FINDTYPE = 0x443F10;
	static const uint32_t A_LIGHT = 0x59D640, A_LIGHT_READ = 0x59D65B, A_SPEED_SITE = 0x619DCC;
	static const uint32_t A_PRICES = 0x4A19D0, A_SETTABS = 0x4C6140, A_CLONE = 0x6326D0, A_LISTPUSH = 0x4860B0, A_MDLREFRESH = 0x64AC00;
	static const uint32_t kHealSites[4] = { 0x6189C1, 0x611214, 0x4A53C9, 0x4A5420 };
	static const uint32_t GC_PLAYER = 0x8006D0, GC_TIME = 0x80015C, GC_CRAFTW = 0x800958, GC_ICON_ALCH = 0x8006C0;
	static const uint32_t CR_ACTION = 0x68, CR_ATIME = 0x6C, CR_VZ = 0x3C, CR_HP = 0x16C, CR_MP = 0x170, CR_STAMINA = 0x1194, CR_USEITEM = 0x1E8;
	static const uint32_t W_GAME = 0x164, W_TABS_SIZE = 0x180;

	// ---- Partikel (siehe Effects.h) -------------------------------------------------
	typedef void (__fastcall* PPush_t)(void* list, void* edx, const void* particle);
	static uint32_t g_prng = 0x1234567u;
	static float Rnd() { g_prng = g_prng * 1664525u + 1013904223u; return (float)((g_prng >> 8) & 0xFFFF) / 65535.0f; }
	static bool PushParticleRaw(uint8_t* G, const uint8_t* pt) {
		__try { ((PPush_t)Re(0x4869D0))(G + 0x800740, nullptr, pt); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void Burst(cube::GameController* gc, float x, float y, float z, float r, float g, float b, int count, float size, float spread, float up, int type, int lifeMs) {
		if (!gc) return;
		alignas(8) uint8_t pt[0xB8];
		for (int i = 0; i < count && i < 200; i++) {
			memset(pt, 0, sizeof(pt));
			*(int*)(pt + 0x00) = type;
			const float jx = (Rnd() - 0.5f) * 0.6f, jy = (Rnd() - 0.5f) * 0.6f, jz = (Rnd() - 0.5f) * 0.6f;
			int64_t* pos = (int64_t*)(pt + 0x08); int64_t* st = (int64_t*)(pt + 0x20);
			pos[0] = st[0] = (int64_t)((x + jx) * 65536.0f); pos[1] = st[1] = (int64_t)((y + jy) * 65536.0f); pos[2] = st[2] = (int64_t)((z + jz) * 65536.0f);
			float* col = (float*)(pt + 0x44); col[0] = r; col[1] = g; col[2] = b; col[3] = 1.0f;
			float* v = (float*)(pt + 0x54);                                  // wie die Pakete des Spiels: Grundrichtung + Streuung*2*(rnd-0.5)
			v[0] = spread * 2.0f * (Rnd() - 0.5f); v[1] = spread * 2.0f * (Rnd() - 0.5f); v[2] = up + spread * 2.0f * (Rnd() - 0.5f);
			float* sp = (float*)(pt + 0x60); sp[0] = 100.0f * (Rnd() - 0.5f); sp[1] = 100.0f * (Rnd() - 0.5f); sp[2] = 100.0f * (Rnd() - 0.5f);
			float* m = (float*)(pt + 0x6C); m[0] = m[5] = m[10] = m[15] = 1.0f;
			*(float*)(pt + 0xAC) = size * (0.7f + 0.6f * Rnd());
			*(int*)(pt + 0xB4) = (int)(lifeMs * (0.7f + 0.6f * Rnd()));
			if (!PushParticleRaw((uint8_t*)gc, pt)) return;
		}
	}
	struct Emitter { float x, y, z, r, g, b, radius; DWORD start, end; int perSec, mode; float acc; };
	static std::vector<Emitter> g_emit;
	void AddEmitter(float x, float y, float z, float r, float g, float b, int ms, int perSecond, int mode, float radius) {
		if (g_emit.size() > 64) g_emit.erase(g_emit.begin());
		DWORD now = GetTickCount();
		g_emit.push_back({ x, y, z, r, g, b, radius, now, now + (DWORD)ms, perSecond, mode, 0.0f });
	}
	static void EmitTick(cube::GameController* gc, float dt) {
		DWORD now = GetTickCount();
		for (size_t i = 0; i < g_emit.size(); ) {
			Emitter& e = g_emit[i];
			if ((int)(now - e.end) >= 0) { g_emit.erase(g_emit.begin() + i); continue; }
			e.acc += dt * e.perSec;
			const float t = (float)(now - e.start) / 1000.0f;
			while (e.acc >= 1.0f) {
				e.acc -= 1.0f;
				if (e.mode == 1) {                                               // Spirale steigt auf, dunkler Rauch
					const float a = t * 7.0f + Rnd() * 0.8f, rr = e.radius * (1.0f - 0.3f * Rnd());
					Burst(gc, e.x + std::cos(a) * rr, e.y + std::sin(a) * rr, e.z + 0.2f + Rnd() * 0.6f, e.r, e.g, e.b, 1, 0.35f, 0.4f, 5.0f + Rnd() * 2.0f, 2, 1400);
				} else {                                                          // Funkeln um eine Stelle
					const float a = Rnd() * 6.2832f, rr = e.radius * std::sqrt(Rnd());
					Burst(gc, e.x + std::cos(a) * rr, e.y + std::sin(a) * rr, e.z + Rnd() * 1.8f, e.r, e.g, e.b, 1, 0.2f, 0.5f, 1.5f, 2, 900);
				}
			}
			i++;
		}
	}
	// Farbe je Trankwirkung (Partikel um den Spieler, solange sie wirkt)
	static void FxColor(int k, float* c) {
		static const float t[FX_COUNT][3] = { { 1, 1, 1 }, { 1.0f, 0.3f, 0.35f }, { 0.35f, 1.0f, 0.45f }, { 1.0f, 0.35f, 0.2f }, { 0.45f, 0.65f, 1.0f },
		                                      { 1.0f, 0.92f, 0.3f }, { 0.92f, 0.96f, 1.0f }, { 0.65f, 0.45f, 1.0f }, { 0.3f, 0.5f, 1.0f }, { 1.0f, 0.65f, 0.25f } };
		c[0] = t[k][0]; c[1] = t[k][1]; c[2] = t[k][2];
	}
	static bool PlayerPosRaw(uint8_t* p, float* x, float* y, float* z) {
		__try { *x = (float)(*(int64_t*)(p + 0x10)) / 65536.0f; *y = (float)(*(int64_t*)(p + 0x18)) / 65536.0f; *z = (float)(*(int64_t*)(p + 0x20)) / 65536.0f; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- Zustand (nur Spielthread) -------------------------------------------------
	struct Active { DWORD end = 0; float value = 0; std::wstring name; int total = 0; };   // total: Dauer in ms (Balken)
	static Active g_act[FX_COUNT];
	static void*  g_target = nullptr;            // Spieler, solange Staerke/Schutz wirken
	static float  g_dmgMul = 1.0f, g_armMul = 1.0f, g_night = 0.0f;
	static bool   g_hStat = false, g_hHeal = false, g_hSpeed = false, g_hNight = false, g_hTab = false;
	static volatile LONG g_tabAdded = 0, g_started = 0;

	// ---- Staerke / Schutz -----------------------------------------------------------
	typedef float (__fastcall* StatF_t)(void* self, void* edx);
	static StatF_t o_WDmg = nullptr, o_APow = nullptr, o_Spell = nullptr, o_Armor = nullptr, o_Spirit = nullptr;
	static float __fastcall H_WDmg(void* s, void* e)   { float r = o_WDmg(s, e);   return (s && s == g_target) ? r * g_dmgMul : r; }
	static float __fastcall H_APow(void* s, void* e)   { float r = o_APow(s, e);   return (s && s == g_target) ? r * g_dmgMul : r; }
	static float __fastcall H_Spell(void* s, void* e)  { float r = o_Spell(s, e);  return (s && s == g_target) ? r * g_dmgMul : r; }
	static float __fastcall H_Armor(void* s, void* e)  { float r = o_Armor(s, e);  return (s && s == g_target) ? r * g_armMul : r; }
	static float __fastcall H_Spirit(void* s, void* e) { float r = o_Spirit(s, e); return (s && s == g_target) ? r * g_armMul : r; }

	// ---- Heilwert: Effekt-Traenke heilen nur mit "auch heilen" ----------------------
	typedef float (__fastcall* Heal_t)(const uint8_t* item, void* edx);
	static float __fastcall H_HealValue(const uint8_t* item, void* edx) {
		int id = 0; EffectInfo fi;
		if (EffectIdOf(item, &id) && EffectById(id, &fi) && !fi.alsoHeal) return 0.0f;
		return ((Heal_t)Re(A_HEALVAL))(item, edx);
	}

	// ---- Nachtsicht: eigene Tageszeit nur fuer die Lichtfarben ---------------------
	typedef void (__fastcall* Light_t)(void* gc, void* edx, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7);
	static Light_t o_Light = nullptr;
	static void __fastcall H_Light(void* gc, void* edx, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6, void* a7) {
		int real = *(int*)((uint8_t*)gc + GC_TIME);
		int t = real;
		if (g_night > 0.0f) t = real + (int)((43200000.0 - (double)real) * (double)g_night);   // Richtung 12:00
		g_fxLightTime = t;
		o_Light(gc, edx, a1, a2, a3, a4, a5, a6, a7);
	}

	// ---- Reiter "Effekte" ------------------------------------------------------------
	typedef void* (__fastcall* Clone_t)(void* node, void* edx, int parent);
	typedef void  (__fastcall* ListPush_t)(void* list, void* edx, void* const* val);
	typedef void  (__fastcall* MdlRefresh_t)(void* mdl, void* edx);
	typedef void  (__fastcall* SetTabs_t)(void* w, void* edx, int* list);
	typedef void  (__fastcall* Prices_t)(void* gc, void* edx, int cat, int* vec);
	static SetTabs_t o_SetTabs = nullptr; static Prices_t o_Prices = nullptr;

	static bool AddEffectTabRaw(uint8_t* w, int* list) {
		__try {
			uint8_t* gc = *(uint8_t**)(w + W_GAME);
			if (!gc || *(uint8_t**)(gc + GC_CRAFTW) != w) return false;    // nur das Handwerksfenster
			if (!list || list[1] != 6) return false;                          // Reiter 0..5 wie im Original
			int* head = (int*)list[0];
			void* last = (void*)((int*)head[1])[2];                          // letzter Reiter (Material)
			void* node = ((Clone_t)Re(A_CLONE))(last, nullptr, 0);
			if (!node) return false;
			uint8_t* mdl = *(uint8_t**)((uint8_t*)node + 0x34);
			if (!mdl || *(int*)(mdl + 0x48) != 2) return false;
			uint8_t* icon = *(uint8_t**)(gc + GC_ICON_ALCH);                 // Symbol wie Alchemie
			if (!icon) return false;
			(*(int**)(mdl + 0x7F8))[*(int*)(mdl + 0x7CC)] = *(int*)(icon + 0x4C);
			((ListPush_t)Re(A_LISTPUSH))(list, nullptr, &node);
			((MdlRefresh_t)Re(A_MDLREFRESH))(mdl, nullptr);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void __fastcall H_SetTabs(void* w, void* edx, int* list) {
		if (AddEffectTabRaw((uint8_t*)w, list)) {
			InterlockedExchange(&g_tabAdded, 1);
			std::cout << "[Effekte] Handwerksmenue: Reiter 'Effekte' angelegt (7. Reiter)." << std::endl;
		}
		o_SetTabs(w, edx, list);
	}
	// Die Reiter baut das Spiel nur einmal im GameController-Konstruktor (0x459C40 -> 0x4A1E50), meist
	// bevor der Mod seine Hooks scharf hat. Dann baut der Mod sie einmal neu: 0x4A1E50 klont frische
	// Reiter (Symbole je Klasse), H_SetTabs haengt "Effekte" an, setTabs ersetzt die alten Knoten.
	typedef void (__fastcall* BuildTabs_t)(void* gc, void* edx);
	static const uint32_t A_BUILDTABS = 0x4A1E50;
	static int TabCountRaw(uint8_t* gc) {
		__try { uint8_t* w = *(uint8_t**)(gc + GC_CRAFTW); return w ? *(int*)(w + W_TABS_SIZE) : -1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	static bool RebuildTabsRaw(uint8_t* gc, uint8_t* player) {
		__try {
			uint8_t cls = player[0x140];
			if (cls < 1 || cls > 4) return false;                              // ohne Klasse baut das Spiel nur 4 Reiter
			((BuildTabs_t)Re(A_BUILDTABS))(gc, nullptr);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool SevenTabsRaw(uint8_t* gc) {
		__try { uint8_t* w = *(uint8_t**)(gc + GC_CRAFTW); return w && *(int*)(w + W_TABS_SIZE) == 7; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Rezeptsaetze (0x128: Gegenstand, Station, Zutatenvektor) werden nur gelesen -- flache Kopien
	// in eigenen Puffern reichen, die Originale gibt loadInventoryItems 0x4A14C0 danach selbst frei.
	static std::vector<uint8_t> g_effRecs;
	static void __fastcall H_Prices(void* gc, void* edx, int cat, int* vec) {
		if (cat < 0 || cat > 5 || !vec || !SevenTabsRaw((uint8_t*)gc)) { o_Prices(gc, edx, cat, vec); return; }
		if (cat == 0) g_effRecs.clear();
		const uint8_t* b = (const uint8_t*)vec[0]; const uint8_t* e = (const uint8_t*)vec[1];
		std::vector<uint8_t> keep; bool moved = false;
		for (const uint8_t* r = b; r && r + 0x128 <= e; r += 0x128) {
			if (IsEffectRecipeItem(r)) { g_effRecs.insert(g_effRecs.end(), r, r + 0x128); moved = true; }
			else keep.insert(keep.end(), r, r + 0x128);
		}
		if (!moved) o_Prices(gc, edx, cat, vec);
		else { int fk[3] = { (int)keep.data(), (int)(keep.data() + keep.size()), (int)(keep.data() + keep.size()) }; o_Prices(gc, edx, cat, fk); }
		if (cat == 5) {
			int fk[3] = { (int)g_effRecs.data(), (int)(g_effRecs.data() + g_effRecs.size()), (int)(g_effRecs.data() + g_effRecs.size()) };
			o_Prices(gc, edx, 6, fk);
		}
	}

	// ---- Patches ----------------------------------------------------------------------
	static bool WriteBytes(uint8_t* p, const uint8_t* b, size_t n) {
		DWORD old;
		if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
		memcpy(p, b, n);
		DWORD d2; VirtualProtect(p, n, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, n);
		return true;
	}
	static bool PatchCall(uint32_t site, uint32_t expect, void* fn) {
		uint8_t* p = (uint8_t*)Re(site);
		__try {
			if (p[0] != 0xE8) return false;
			if ((uint32_t)p + 5 + *(int32_t*)(p + 1) != Re(expect)) return false;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		uint8_t b[5] = { 0xE8 }; *(int32_t*)(b + 1) = (int32_t)((uint32_t)fn - ((uint32_t)p + 5));
		return WriteBytes(p, b, 5);
	}
	static bool BytesAre(uint32_t a, const uint8_t* b, size_t n) {
		__try { return memcmp((const void*)Re(a), b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool Hook(uint32_t a, const uint8_t* pro, size_t n, void* det, void** orig) {
		return BytesAre(a, pro, n) && MH_CreateHook((LPVOID)Re(a), det, (LPVOID*)orig) == MH_OK;
	}

	void Install() {
		static const uint8_t pFrame[6] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
		static const uint8_t pSpell[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 };
		static const uint8_t pArm[6]   = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10 };
		static const uint8_t pLight[9] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00 };
		static const uint8_t pPrice[9] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x4C, 0x01, 0x00, 0x00 };
		std::ostringstream rep;
		bool a = Hook(A_WDMG, pFrame, 6, (void*)&H_WDmg, (void**)&o_WDmg);
		bool b = Hook(A_APOW, pFrame, 6, (void*)&H_APow, (void**)&o_APow);
		bool c = Hook(A_SPELL, pSpell, 6, (void*)&H_Spell, (void**)&o_Spell);
		bool d = Hook(A_ARMOR, pArm, 6, (void*)&H_Armor, (void**)&o_Armor);
		bool e = Hook(A_SPIRIT, pArm, 6, (void*)&H_Spirit, (void**)&o_Spirit);
		g_hStat = a && b && c && d && e;
		rep << "Staerke/Schutz " << (g_hStat ? "ok" : "FEHLT") << ", ";

		// Heilwert: alle vier Aufrufstellen muessen passen, sonst keine anfassen
		bool sitesOk = true;
		for (uint32_t s : kHealSites) {
			uint8_t* p = (uint8_t*)Re(s);
			if (!BytesAre(s, (const uint8_t*)"\xE8", 1) || (uint32_t)p + 5 + *(int32_t*)(p + 1) != Re(A_HEALVAL)) sitesOk = false;
		}
		if (sitesOk) { g_hHeal = true; for (uint32_t s : kHealSites) g_hHeal = PatchCall(s, A_HEALVAL, (void*)&H_HealValue) && g_hHeal; }
		rep << "Heilwert " << (g_hHeal ? "ok" : "FEHLT") << ", ";

		// Schnelligkeit
		static const uint8_t speedOrig[9] = { 0x6A, 0x0C, 0x8B, 0xCE, 0xE8, 0x3B, 0xA1, 0xE2, 0xFF };
		if (BytesAre(A_SPEED_SITE, speedOrig, 9)) {
			g_fxFindByType = Re(A_FINDTYPE);
			uint8_t* p = (uint8_t*)Re(A_SPEED_SITE);
			uint8_t nb[9] = { 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
			*(int32_t*)(nb + 1) = (int32_t)((uint32_t)&SpeedStub - ((uint32_t)p + 5));
			g_hSpeed = WriteBytes(p, nb, 9);
		}
		rep << "Schnelligkeit " << (g_hSpeed ? "ok" : "FEHLT") << ", ";

		// Nachtsicht: Hook + Lesestelle (nur beides zusammen)
		static const uint8_t readOrig[8] = { 0x66, 0x0F, 0x6E, 0x81, 0x5C, 0x01, 0x80, 0x00 };
		if (BytesAre(A_LIGHT_READ, readOrig, 8) && Hook(A_LIGHT, pLight, 9, (void*)&H_Light, (void**)&o_Light)) {
			uint8_t nb[8] = { 0x66, 0x0F, 0x6E, 0x05, 0, 0, 0, 0 };                // movd xmm0, [&g_fxLightTime]
			*(uint32_t*)(nb + 4) = (uint32_t)&g_fxLightTime;
			g_hNight = WriteBytes((uint8_t*)Re(A_LIGHT_READ), nb, 8);
			if (!g_hNight) MH_RemoveHook((LPVOID)Re(A_LIGHT));
		}
		rep << "Nachtsicht " << (g_hNight ? "ok" : "FEHLT") << ", ";

		// Reiter "Effekte"
		g_hTab = Hook(A_PRICES, pPrice, 9, (void*)&H_Prices, (void**)&o_Prices);
		if (g_hTab && !Hook(A_SETTABS, pFrame, 6, (void*)&H_SetTabs, (void**)&o_SetTabs)) { MH_RemoveHook((LPVOID)Re(A_PRICES)); g_hTab = false; }
		rep << "Reiter " << (g_hTab ? "ok" : "FEHLT");
		std::cout << "[Effekte] " << rep.str() << std::endl;
	}

	// ---- Spielthread ------------------------------------------------------------------
	static uint8_t* PlayerRaw(cube::GameController* gc) { __try { return *(uint8_t**)((uint8_t*)gc + GC_PLAYER); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
	struct DrinkSnap { bool ok; uint8_t action; int timer; uint8_t item[16]; };
	static DrinkSnap SnapRaw(uint8_t* p) {
		DrinkSnap s; memset(&s, 0, sizeof(s));
		__try { s.action = p[CR_ACTION]; s.timer = *(int*)(p + CR_ATIME); memcpy(s.item, p + CR_USEITEM, 16); s.ok = true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { s.ok = false; }
		return s;
	}
	static int CooldownOf(const uint8_t* item) { return (item[0] == 1 && item[1] == 1) ? 3000 : 10000; }   // 0x4C6B80
	typedef float (__fastcall* MaxHp_t)(void* c, void* edx);
	struct ApplyIn { float regen, mana, fall; bool stamina; float dt; };
	static void ApplyRaw(uint8_t* p, const ApplyIn& a) {
		__try {
			float* hp = (float*)(p + CR_HP);
			if (a.regen > 0 && *hp > 0.0f) {
				float mx = ((MaxHp_t)Re(A_MAXHP))(p, nullptr);
				if (*hp < mx) *hp = (std::min)(mx, *hp + mx * a.regen / 100.0f * a.dt);
			}
			if (a.mana > 0) { float* mp = (float*)(p + CR_MP); *mp = (std::min)(1.0f, *mp + a.mana / 100.0f * a.dt); }
			if (a.stamina) *(float*)(p + CR_STAMINA) = 1.0f;
			if (a.fall > 0) { float* vz = (float*)(p + CR_VZ); if (*vz < -a.fall) *vz = -a.fall; }
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	typedef void* (__fastcall* WAssign_t)(void* str, void* edx, const wchar_t* s);
	typedef void  (__fastcall* WFree_t)(void* str, void* edx);
	typedef void  (__fastcall* ChatPrint_t)(void* chat, void* edx, void* wstr, void* color);
	static void ChatRaw(cube::GameController* gc, const wchar_t* text) {
		__try {
			if (!gc || !gc->ChatWidget) return;
			alignas(4) uint8_t w[0x18]; memset(w, 0, sizeof(w));
			((WAssign_t)Re(0x40EB60))(w, nullptr, text);
			float col[4] = { 0.55f, 0.85f, 1.0f, 1.0f };
			((ChatPrint_t)Re(0x43AB30))((void*)gc->ChatWidget, nullptr, w, col);
			((WFree_t)Re(0x593E50))(w, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	static void Start(cube::GameController* gc, int id) {
		EffectInfo fi;
		if (!EffectById(id, &fi) || !EffectTagged(fi.kind)) return;
		Active& a = g_act[fi.kind];
		DWORD now = GetTickCount();
		a.end = now + (DWORD)fi.seconds * 1000u;
		a.value = fi.value; a.name = fi.name; a.total = fi.seconds * 1000;
		{   // Wirkung beginnt: Funkenstoss in der Farbe der Wirkung
			uint8_t* p = PlayerRaw(gc); float x, y, z, c[3]; FxColor(fi.kind, c);
			if (p && PlayerPosRaw(p, &x, &y, &z)) Burst(gc, x, y, z, c[0], c[1], c[2], 36, 0.3f, 3.0f, 5.0f, 2, 1300);
		}
		InterlockedIncrement(&g_started);
		wchar_t msg[200]; char nm[64]; WideCharToMultiByte(CP_ACP, 0, fi.name.c_str(), -1, nm, sizeof(nm), nullptr, nullptr);
		swprintf_s(msg, L"%ls wirkt %d s.", fi.name.c_str(), fi.seconds);
		ChatRaw(gc, msg);
		std::cout << "[Effekte] " << nm << ": " << EffectName(fi.kind) << " " << fi.value << " fuer " << fi.seconds << " s" << std::endl;
	}

	void Tick(cube::GameController* gc) {
		static uint8_t* lastP = nullptr; static DWORD lastT = 0;
		static bool watching = false, fired = false; static int watchId = 0, watchTimer = 0, watchDur = 0;
		if (!gc) return;
		uint8_t* p = PlayerRaw(gc);
		DWORD now = GetTickCount();
		float dt = lastT ? (float)(std::min)((DWORD)250, now - lastT) / 1000.0f : 0.0f;
		lastT = now;
		if (p != lastP) {                                    // anderer Spieler / neu geladen
			for (Active& a : g_act) a.end = 0;
			watching = false; lastP = p;
		}
		if (!p) { g_target = nullptr; g_fxSpeedPlayer = nullptr; g_night = 0; return; }

		// Reiter "Effekte" nachruesten (einmal je Spieler, nur wenn das Spiel 6 Reiter hat)
		static uint8_t* tabTriedFor = nullptr;
		if (g_hTab && tabTriedFor != p && TabCountRaw((uint8_t*)gc) == 6) {
			tabTriedFor = p;
			bool ok = RebuildTabsRaw((uint8_t*)gc, p);
			std::cout << "[Effekte] Handwerksreiter neu aufgebaut: " << (ok ? "ja" : "nein") << ", jetzt " << TabCountRaw((uint8_t*)gc) << " Reiter." << std::endl;
		}

		// Trinken/Essen eines Effekt-Tranks beobachten; Wirkung erst, wenn es zu Ende gefuehrt wurde.
		DrinkSnap s = SnapRaw(p);
		if (s.ok) {
			int id = 0;
			bool drinking = (s.action == 0x50 || s.action == 0x51) && EffectIdOf(s.item, &id);
			if (drinking) {
				if (!watching || id != watchId || s.timer < watchTimer) {           // neuer Schluck
					if (watching && !fired && watchTimer >= watchDur - 300) Start(gc, watchId);
					watching = true; fired = false; watchId = id; watchDur = CooldownOf(s.item);
				}
				watchTimer = s.timer;
				if (!fired && watchTimer >= watchDur) { Start(gc, watchId); fired = true; }   // Dauer erreicht
			} else if (watching) {                                                  // Aktion beendet
				if (!fired && watchTimer >= watchDur - 300) Start(gc, watchId);
				watching = false;
			}
		}

		// Ablauf + Werte fuer die Hooks
		ApplyIn in = { 0, 0, 0, false, dt };
		float dmg = 1.0f, arm = 1.0f, spd = 1.0f, night = 0.0f;
		for (int k = FX_REGEN; k < FX_COUNT; k++) {
			Active& a = g_act[k];
			if (!a.end) continue;
			if ((int)(a.end - now) <= 0) {
				a.end = 0;
				wchar_t msg[160]; swprintf_s(msg, L"%ls ist abgeklungen.", a.name.c_str()); ChatRaw(gc, msg);
				continue;
			}
			switch (k) {
				case FX_REGEN:    in.regen = a.value; break;
				case FX_STRENGTH: dmg = 1.0f + a.value / 100.0f; break;
				case FX_PROTECT:  arm = 1.0f + a.value / 100.0f; break;
				case FX_SPEED:    spd = 1.0f + a.value / 100.0f; break;
				case FX_SLOWFALL: in.fall = (std::max)(0.5f, a.value); break;
				case FX_NIGHT:    night = (std::max)(0.0f, (std::min)(1.0f, a.value / 100.0f)); break;
				case FX_MANA:     in.mana = a.value; break;
				case FX_STAMINA:  in.stamina = true; break;
			}
		}
		// Partikel: solange eine Wirkung laeuft, funkelt es in ihrer Farbe um den Spieler (~6 je Sekunde und Wirkung)
		{
			static float acc[FX_COUNT] = {};
			float px, py, pz;
			if (PlayerPosRaw(p, &px, &py, &pz)) for (int k = FX_REGEN; k < FX_COUNT; k++) {
				if (!g_act[k].end) { acc[k] = 0; continue; }
				acc[k] += dt * 6.0f;
				float c[3]; FxColor(k, c);
				while (acc[k] >= 1.0f) {
					acc[k] -= 1.0f;
					const float a = Rnd() * 6.2832f, rr = 0.6f + Rnd() * 0.8f;
					Burst(gc, px + std::cos(a) * rr, py + std::sin(a) * rr, pz - 0.8f + Rnd() * 1.8f, c[0], c[1], c[2], 1, 0.18f, 0.3f, 1.2f, 2, 1000);
				}
			}
		}
		EmitTick(gc, dt);
		g_dmgMul = (std::max)(0.0f, dmg); g_armMul = (std::max)(0.0f, arm);
		g_target = (dmg != 1.0f || arm != 1.0f) ? p : nullptr;
		g_fxSpeedMul = (std::max)(0.05f, spd);
		g_fxSpeedPlayer = (spd != 1.0f) ? p : nullptr;
		g_night = night;
		if (in.regen > 0 || in.mana > 0 || in.fall > 0 || in.stamina) ApplyRaw(p, in);
	}

	std::string HudText() {
		std::string t; DWORD now = GetTickCount();
		for (int k = FX_REGEN; k < FX_COUNT; k++) {
			const Active& a = g_act[k];
			if (!a.end || (int)(a.end - now) <= 0) continue;
			int s = (int)(a.end - now) / 1000;
			char b[160], nm[64]; WideCharToMultiByte(CP_ACP, 0, a.name.c_str(), -1, nm, sizeof(nm), nullptr, nullptr);
			const char* kn = EffectName(k); std::string shortName(kn, strcspn(kn, "("));
			while (!shortName.empty() && shortName.back() == ' ') shortName.pop_back();
			sprintf_s(b, "%s  %d:%02d  (%s)\n", shortName.c_str(), s / 60, s % 60, nm);
			t += b;
		}
		return t;
	}
	// Wirkungs-Anzeige (2026-10-19): je aktiver Wirkung ein Kaestchen in der Farbe der Wirkung mit Kuerzel, darunter ein
	// ablaufender Balken und die Restzeit -- wie ein Zustand des Spiels links am Bildrand (auch in der Veroeffentlichung).
	struct FxStyle { const char* tag; D3DCOLOR col; };
	static FxStyle Style(int k) {
		switch (k) {
			case FX_REGEN:    return { "+",  D3DCOLOR_ARGB(255,  70, 200,  90) };
			case FX_STRENGTH: return { "ST", D3DCOLOR_ARGB(255, 210,  60,  50) };
			case FX_PROTECT:  return { "SC", D3DCOLOR_ARGB(255, 120, 140, 170) };
			case FX_SPEED:    return { ">>", D3DCOLOR_ARGB(255, 230, 200,  50) };
			case FX_SLOWFALL: return { "v",  D3DCOLOR_ARGB(255, 140, 210, 240) };
			case FX_NIGHT:    return { "N",  D3DCOLOR_ARGB(255, 140,  90, 210) };
			case FX_MANA:     return { "M",  D3DCOLOR_ARGB(255,  60, 110, 230) };
			case FX_STAMINA:  return { "A",  D3DCOLOR_ARGB(255, 240, 150,  40) };
			default:          return { "?",  D3DCOLOR_ARGB(255, 200, 200, 200) };
		}
	}
	std::string Status() {
		char b[220];
		sprintf_s(b, "Effekte: Staerke/Schutz %s, Heilwert %s, Schnelligkeit %s, Nachtsicht %s, Reiter %s%s, gestartet %ld",
			g_hStat ? "ok" : "FEHLT", g_hHeal ? "ok" : "FEHLT", g_hSpeed ? "ok" : "FEHLT", g_hNight ? "ok" : "FEHLT",
			g_hTab ? "ok" : "FEHLT", g_tabAdded ? " (angelegt)" : " (noch nicht angelegt)", g_started);
		return b;
	}
}
