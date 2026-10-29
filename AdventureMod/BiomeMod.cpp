// BiomeMod.cpp -- siehe BiomeMod.h und RE_Biome.md.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "BiomeMod.h"
#include "ModDb.h"
#include "Wildlife.h"
#include "Host.h"
#include "Recipes.h"
#include "Items.h"
#include "City.h"
#include "Skills.h"
#include "Temples.h"
#include "ZoneSplit.h"
#include "AdventureModFramework.h"
#include "CrashLogger.h"
#include "ModSwitches.h"
#include "LabBridge.h"
#include "MobLab.h"
#include "MountainVillage.h"
#include "MinHook.h"
#include "SeedScout.h"
#include "Dungeons.h"
#include "Tracking.h"

#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <tuple>

namespace BiomeMod {
	using namespace BiomeGen;

	// =====================================================================
	// 1. Adressen (ImageBase 0x400000, zur Laufzeit verschoben -- Cube.exe hat ASLR)
	// =====================================================================
	static const uint32_t A_CELL    = 0x005D7A70;   // Klimazelle anlegen   __thiscall(World*, cx, cy) -> Cell*, ret 8
	static const uint32_t A_REGION  = 0x005DA280;   // generateRegion       __thiscall(World*, rx, ry),          ret 8
	static const uint32_t A_SURFACE = 0x005C56E0;   // Bodenfarbe + Typ     __thiscall(World*, out[4], x, y, lvl, hum, temp, p7), ret 0x1C
	static const uint32_t A_ROCK    = 0x005C7140;   // Felsfarbe            __thiscall(World*, float out[3], x, y, lvl, p5) -> out, ret 0x14
	static const uint32_t A_RACE    = 0x005F56C0;   // Rasse eines Spawns   __thiscall(World*, x, y, hoehe, sonder) -> Rasse, ret 0x10
	static const uint32_t A_PERLIN  = 0x004C0EF0;   // __cdecl(double, double) -> ST0, rein rechnend
	static const uint32_t A_HEIGHT  = 0x005C5E20;   // Grundhoehe einer Saeule  __thiscall(World*, x, y, p3) -> float ST0, ret 0xC
	                                                //   (13 Aufrufer: Zonenbau 0x5E4C75, Regionen, Karte, Landschaft -> alles konsistent)
	static const uint32_t A_MOUNT   = 0x005EFFA0;   // Berg-/Klippenfaktor     __thiscall(World*, x, y, float) -> float ST0, ret 0xC
	                                                //   (geht in generateBiomeContent mit (n+1.5)*60*m in die Hoehe ein)
	static const uint32_t A_RIVER   = 0x005F9340;   // Flussabstand r            __thiscall(World*, x, y, p3) -> float ST0, ret 0xC
	                                                //   (Fluss/See wo r < 0.02; 6 Aufrufer: Hoehe 0x5C60F1, Boden 0x5C574E, Flussschleife ...)
	static const uint32_t A_TREE    = 0x005DF960;   // Baum bauen              __thiscall(World*, x, y, z, a4, a5, a6, Zone*), ret 0x1C
	static const uint32_t A_TREE_RET_NATIVE = 0x005EA7B3;   // Ruecksprung der normalen Baumschleife in generateBiomeContent
	                                                //   (0x5E7231 = Riesenbaum des Ortstyps 12, 0x5BC200 = Dorf -- bleiben unberuehrt)

	static const uint32_t W_CELLGRID   = 0x4000BC;  // Cell*[1024*1024], Index cx*1024+cy
	static const uint32_t W_REGIONGRID = 0xBC;      // Region*[1024*1024]
	static const uint32_t W_SEED       = 0x800188;
	static const uint32_t W_STARTX     = 0x8000F0, W_STARTY = 0x8000F4;   // float, Bloecke
	static const uint32_t GC_WORLD     = 0x2E4;     // GameController + 0x2E4 = World

	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	// Prologe aus der vorliegenden Cube.exe (capstone). Nur die ersten 6 Byte:
	// 0x5F56C5 ist ein 'push imm32' mit Relokation -- bei ASLR anders im Speicher.
	struct Site { uint32_t addr; uint8_t pro[6]; const char* name; };
	static const Site kSites[] = {
		{ A_CELL,    { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20 }, "Klimazelle 0x5D7A70" },
		{ A_REGION,  { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 }, "Region 0x5DA280" },
		{ A_SURFACE, { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40 }, "Boden 0x5C56E0" },
		{ A_ROCK,    { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x48 }, "Fels 0x5C7140" },
		{ A_RACE,    { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 }, "Rasse 0x5F56C0" },
		{ A_PERLIN,  { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C }, "Perlin 0x4C0EF0" },
		{ A_HEIGHT,  { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0 }, "Hoehe 0x5C5E20" },
		{ A_MOUNT,   { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 }, "Berge 0x5EFFA0" },
		{ A_TREE,    { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x10 }, "Baum 0x5DF960" },
		{ A_RIVER,   { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C }, "Fluss 0x5F9340" },
	};

	static bool ProloguesMatch(uint32_t a, const uint8_t* want) {
		const void* p = (const void*)Re(a);
		if (!p) return false;                                       // im Server nicht vorhanden (Host::Re)
		__try { return memcmp(p, want, 6) == 0; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// =====================================================================
	// 2. Daten fuer die Hooks (POD, veroeffentlicht ueber einen Zeiger)
	// =====================================================================
	const int kMaxBiomes = 31;                        // Biom-ID 1..31 (Byte in der Zelltabelle; 2026-10-27: 15 -> 31 fuer die nativen Biome)
	struct HookDef {
		bool  used, enabled;
		int   share, climateReq;
		bool  setClimate; float temp, hum;
		uint8_t g1[3], g2[3]; int groundType; float strength;
		uint8_t rock[3]; bool rockOn;
		int   races[32]; int nRaces; float raceShare;
		uint32_t siteMask; bool bergdorf;
		bool  terrainOn; float baseHeight, waves, waveSize, hillShare, hillHeight, hillSize, flatten, mountains;
		int   nativeTrees;
		bool  riversNative, riversOwn; float riverWidth, riverSpacing, lakeShare, lakeSize;
		float ridgeHeight, ridgeWidth, ridgeSpacing, ridgeRough, snowLine;
		bool  plainOn; float plainRadius, plainRim, plainHeight;
		int   city; float cityRadius;
		StarParams star;
		int   slopeTrees; float slopeTreeSize;
		int   nativeClass;            // > 0: gilt fuer alle Zellen dieser nativen Klimaklasse (BiomeGen::NativeClassOf)
		uint32_t salt;                // Wuerfel-Salz aus dem Dateinamen (nicht aus dem Platz -> unabhaengig von Reihenfolge/Anzahl der Biome)
	};
	// Von Hand erzwungene Zellen ("Biom HIER erzwingen"), je Weltsamen.
	struct ForcedCell { uint32_t seed; int cx, cy, id; };
	const int kMaxForced = 256;
	struct HookSet { HookDef d[kMaxBiomes]; int nForced; ForcedCell forced[kMaxForced]; };
	static HookSet* volatile g_hs = nullptr;            // alte Saetze werden NIE freigegeben (Hooks lesen ohne Sperre)
	static volatile bool g_on = true;
	static uint8_t g_cell[1024 * 1024];                // Biom-ID je Klimazelle (0 = keins)
	static volatile LONG g_modVersion = 1;             // fuer SeedScout::ModVersion

	typedef double(__cdecl* Perlin_t)(double, double);
	static Perlin_t g_perlin = nullptr;

	// Zaehler (Zonen-Thread schreibt, LAB liest)
	static volatile LONG c_cells = 0, c_sites = 0, c_races = 0, c_zones = 0, c_plants = 0, c_dungeons = 0, c_mobs = 0;

	// Volle Definitionen (Strings, Listen) -- nur unter g_cs
	namespace {
		CRITICAL_SECTION g_cs; bool g_csInit = false;
		struct Guard { Guard() { EnterCriticalSection(&g_cs); } ~Guard() { LeaveCriticalSection(&g_cs); } };
		BiomeDef g_def[kMaxBiomes];
		std::string g_file[kMaxBiomes];                 // Datei je ID (stabil fuer die Sitzung)
		bool g_defUsed[kMaxBiomes] = {};
		int  g_defVersion = 0;
		bool g_hooksOk = false; std::string g_hookReport;
		struct ForcedEntry { uint32_t seed; int cx, cy; std::string file; };
		std::vector<ForcedEntry> g_forcedList;              // Strukturen\Biome\_erzwungen.txt
		Status g_status;
	}

	void ReloadZoneModels();
	static HookDef ToHook(const BiomeDef& b) {
		HookDef h; memset(&h, 0, sizeof(h));
		h.used = true; h.enabled = b.enabled;
		h.share = b.share; h.climateReq = b.climateReq; h.setClimate = b.setClimate; h.temp = b.temp; h.hum = b.hum;
		h.g1[0] = b.ground.r; h.g1[1] = b.ground.g; h.g1[2] = b.ground.b;
		h.g2[0] = b.ground2.r; h.g2[1] = b.ground2.g; h.g2[2] = b.ground2.b;
		h.groundType = b.groundType; h.strength = b.strength;
		h.rock[0] = b.rock.r; h.rock[1] = b.rock.g; h.rock[2] = b.rock.b; h.rockOn = b.rockOn;
		h.nRaces = 0;
		for (int r : b.races) if (r >= 0 && r < MobLab::kRaceCount && h.nRaces < 32) h.races[h.nRaces++] = r;
		h.raceShare = b.raceShare; h.siteMask = b.SiteMask(); h.bergdorf = b.bergdorf; h.nativeClass = b.nativeClass;
		h.terrainOn = b.terrainOn; h.baseHeight = b.baseHeight; h.waves = b.waves; h.waveSize = b.waveSize;
		h.hillShare = b.hillShare; h.hillHeight = b.hillHeight; h.hillSize = b.hillSize;
		h.flatten = b.flatten; h.mountains = b.mountains; h.nativeTrees = b.nativeTrees;
		h.riversNative = b.riversNative; h.riversOwn = b.riversOwn; h.riverWidth = b.riverWidth;
		h.riverSpacing = b.riverSpacing; h.lakeShare = b.lakeShare; h.lakeSize = b.lakeSize;
		h.ridgeHeight = b.ridgeHeight; h.ridgeWidth = b.ridgeWidth; h.ridgeSpacing = b.ridgeSpacing;
		h.ridgeRough = b.ridgeRough; h.snowLine = b.snowLine;
		h.plainOn = b.plainOn; h.plainRadius = b.plainRadius; h.plainRim = b.plainRim; h.plainHeight = b.plainHeight;
		h.city = b.city; h.cityRadius = b.cityRadius;
		h.star = MakeStarParams(b);
		h.slopeTrees = b.slopeTrees; h.slopeTreeSize = b.slopeTreeSize;
		return h;
	}

	// =====================================================================
	// 3. Gewicht W: Nachbau von World_getRegionCellPtr @0x5EF040
	// =====================================================================
	// id/w: Biom und Gewicht; px/py: Mitte (Zelle +0/+4) der naechsten Zelle dieses Bioms --
	// dort liegt die freie Ebene (ebene_an).
	struct Dom { int id; float w; int px, py; };

	static int Div16k(int v) { return (v + ((v >> 31) & 0x3FFF)) >> 14; }   // wie im Original (Rundung zur 0)

	static Dom DominantRaw(const uint8_t* world, int x, int y) {
		Dom r = { 0, 0.0f };
		__try {
			int x0 = Div16k(x - 0x4000), x1 = Div16k(x + 0x4000);
			int y0 = Div16k(y - 0x4000), y1 = Div16k(y + 0x4000);
			if (x0 < 0 || y0 < 0 || x1 > 1023 || y1 > 1023 || x1 - x0 > 2 || y1 - y0 > 2) return r;
			bool any = false;
			for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) if (g_cell[cx * 1024 + cy]) any = true;
			if (!any || !g_perlin) return r;
			const uint8_t* const* grid = (const uint8_t* const*)(world + W_CELLGRID);
			const uint8_t* cells[9]; int ids[9]; int n = 0;
			for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) {
				const uint8_t* c = grid[cx * 1024 + cy];
				if (!c) return r;                                    // Original: fehlt eine Zelle -> 0
				cells[n] = c; ids[n] = g_cell[cx * 1024 + cy]; n++;
			}
			int cpx[9], cpy[9];
			for (int i = 0; i < n; i++) { cpx[i] = *(const int*)(cells[i] + 0); cpy[i] = *(const int*)(cells[i] + 4); }
			// Perlin-Verschiebung exakt wie 0x5EF145..0x5EF21C
			float jx = (float)g_perlin((double)y * 0.0005, 3423.0);
			jx = jx * 3.0f; jx = jx * 256.0f;
			float jy = (float)g_perlin((double)x * 0.0005, 23421.0);
			jy = jy * 3.0f; jy = jy * 256.0f;
			int xj = (int)((float)x + jx), yj = (int)((float)y + jy);
			int d2[9]; int best = INT_MAX;
			for (int i = 0; i < n; i++) {
				int cxp = *(const int*)(cells[i] + 0), cyp = *(const int*)(cells[i] + 4);
				float fx = (float)((int64_t)(cxp - xj) * 65536); fx = fx * 1.5258789e-05f;
				float fy = (float)((int64_t)(cyp - yj) * 65536); fy = fy * 1.5258789e-05f;
				d2[i] = (int)(fy * fy + fx * fx);
				if (d2[i] < best) best = d2[i];
			}
			float total = 0, sum[kMaxBiomes + 1] = {};
			for (int i = 0; i < n; i++) {
				float f = (float)(d2[i] - best) * 5e-07f;
				if (f > 1.0f) f = 1.0f;
				float w = 1.0f - f;
				total += w;
				if (ids[i] > 0 && ids[i] <= kMaxBiomes) sum[ids[i]] += w;
			}
			if (total <= 0.0f) return r;
			int bi = 0; for (int i = 1; i <= kMaxBiomes; i++) if (sum[i] > sum[bi]) bi = i;
			if (bi > 0 && sum[bi] > 0.0f) {
				r.id = bi; r.w = sum[bi] / total;
				int bd = INT_MAX;
				for (int i = 0; i < n; i++) if (ids[i] == bi && d2[i] < bd) { bd = d2[i]; r.px = cpx[i]; r.py = cpy[i]; }
			}
			return r;
		} __except (EXCEPTION_EXECUTE_HANDLER) { Dom z = { 0, 0.0f }; return z; }
	}

	// ---- 3b. Breites Hoehengewicht (2026-10-27) -----------------------------------------------------
	// Die Biomgrenze des Spiels (Gewicht oben, Abfall 5e-7 je Abstand^2-Einheit) mischt nur ~125 Bloecke zu jeder
	// Seite. Fuer Farben/Orte bleibt sie; fuer die HOEHE war das bei hohen Biomen (Sternmassiv, Bergzuege, hohe
	// Grundhoehe) und an nativen Bergen zu steil, und zwischen ZWEI eigenen Biomen sprang die Hoehe sogar um die
	// halbe Differenz (jede Seite mischte nur ihr Ziel mit der nativen Hoehe). Jetzt: gleiche Zellen, gleiche
	// Perlin-Verschiebung, aber Abfall x kWideScale (~5x breiter, ~550 Bloecke gesamt), jede Zelle weich
	// (smoothstep), und die Hoehe = Summe aller beteiligten Biome (und der nativen Hoehe) nach Anteil.
	static const float kWideScale = 0.2f;                 // ~550 Bloecke Uebergang (Steigung bei 150 Bloecken Hoehenunterschied ~0.4)
	struct Wide { int n; int id[4]; float a[4]; int px[4], py[4]; float a0; };   // a0 = Anteil natives Gelaende
	static Wide WideRaw(const uint8_t* world, int x, int y) {
		Wide r; r.n = 0; r.a0 = 1.0f;
		__try {
			int x0 = Div16k(x - 0x4000), x1 = Div16k(x + 0x4000);
			int y0 = Div16k(y - 0x4000), y1 = Div16k(y + 0x4000);
			if (x0 < 0 || y0 < 0 || x1 > 1023 || y1 > 1023 || x1 - x0 > 2 || y1 - y0 > 2) return r;
			bool any = false;
			for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) if (g_cell[cx * 1024 + cy]) any = true;
			if (!any || !g_perlin) return r;
			const uint8_t* const* grid = (const uint8_t* const*)(world + W_CELLGRID);
			int ids[9], cpx[9], cpy[9], d2[9], n = 0, best = INT_MAX;
			float jx = (float)g_perlin((double)y * 0.0005, 3423.0); jx = jx * 3.0f; jx = jx * 256.0f;
			float jy = (float)g_perlin((double)x * 0.0005, 23421.0); jy = jy * 3.0f; jy = jy * 256.0f;
			int xj = (int)((float)x + jx), yj = (int)((float)y + jy);
			for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) {
				const uint8_t* c = grid[cx * 1024 + cy];
				if (!c) return r;
				ids[n] = g_cell[cx * 1024 + cy]; cpx[n] = *(const int*)(c + 0); cpy[n] = *(const int*)(c + 4);
				float fx = (float)((int64_t)(cpx[n] - xj) * 65536); fx = fx * 1.5258789e-05f;
				float fy = (float)((int64_t)(cpy[n] - yj) * 65536); fy = fy * 1.5258789e-05f;
				d2[n] = (int)(fy * fy + fx * fx); if (d2[n] < best) best = d2[n];
				n++;
			}
			float total = 0, nat = 0, sum[kMaxBiomes + 1] = {}; int bd[kMaxBiomes + 1], bpx[kMaxBiomes + 1] = {}, bpy[kMaxBiomes + 1] = {};
			for (int i = 0; i <= kMaxBiomes; i++) bd[i] = INT_MAX;
			for (int i = 0; i < n; i++) {
				float f = (float)(d2[i] - best) * (5e-07f * kWideScale);
				if (f > 1.0f) f = 1.0f;
				float w = 1.0f - f; w = w * w * (3.0f - 2.0f * w);
				total += w;
				int id = ids[i];
				if (id > 0 && id <= kMaxBiomes) { sum[id] += w; if (d2[i] < bd[id]) { bd[id] = d2[i]; bpx[id] = cpx[i]; bpy[id] = cpy[i]; } }
				else nat += w;
			}
			if (total <= 0.0f) return r;
			r.a0 = nat / total;
			for (int k = 0; k < 4; k++) {                                   // die (hoechstens 4) staerksten Biome
				int bi = 0; for (int i = 1; i <= kMaxBiomes; i++) if (sum[i] > sum[bi]) bi = i;
				if (bi == 0 || sum[bi] <= 0.0f) break;
				r.id[r.n] = bi; r.a[r.n] = sum[bi] / total; r.px[r.n] = bpx[bi]; r.py[r.n] = bpy[bi]; r.n++;
				sum[bi] = 0.0f;
			}
			float s = r.a0; for (int k = 0; k < r.n; k++) s += r.a[k];
			if (s > 0.0f) { r.a0 /= s; for (int k = 0; k < r.n; k++) r.a[k] /= s; }
			return r;
		} __except (EXCEPTION_EXECUTE_HANDLER) { Wide z; z.n = 0; z.a0 = 1.0f; return z; }
	}

	// Pro Thread die letzte Saeule merken: der Felshook kommt je Voxel einer Saeule.
	// ---- Saeulen-Speicher je Thread (Leistung, 2026-10-06) ----------------------------------
	// Die Zonenerzeugung fragt je Saeule Hoehe (~2x), Flussabstand (~5x), Bergfaktor, Boden und
	// Fels ab -- frueher rechnete jeder Hook Biomgewicht, Sternmassiv und See jedes Mal neu
	// (gemessen: 1.2 s je Zone). Jetzt einmal je Saeule. Direkt abgebildet ueber (x&255, y&255):
	// eine ganze Zone passt hinein, alle Durchgaenge einer Zone treffen. Gueltig nur fuer dieselbe
	// Welt, denselben Samen, denselben Biomsatz (g_hs), dieselbe Version und An/Aus.
	struct LakeQ { float dn; int lx, ly; float R; };
	struct ColInfo {
		int x, y; const void* w; uint32_t stamp;
		uint8_t has;                       // 1 Dom, 2 Stern, 4 See, 8 Hoehe, 16 Fluss, 32 Fluss verschachtelt, 64 breites Gewicht
		bool starIn, lakeOk;
		Dom d;
		Wide wd;
		float sMask, sRiver, sHRaw, sFade, sLakeX, sLakeY, sLakeR; bool sLake;
		LakeQ lq;
		float height, riverF, riverN;
	};
	static uint32_t WorldSeedRaw(const uint8_t* world);
	static uint32_t ColStamp(const void* w);
	static ColInfo& Col(const void* w, int x, int y) {
		static thread_local ColInfo* t = nullptr;
		if (!t) { t = new ColInfo[65536]; for (int i = 0; i < 65536; i++) { t[i].w = nullptr; t[i].has = 0; t[i].x = INT_MIN; } }
		ColInfo& c = t[(x & 255) | ((y & 255) << 8)];
		uint32_t st = ColStamp(w);
		if (c.x != x || c.y != y || c.w != w || c.stamp != st) { c.x = x; c.y = y; c.w = w; c.stamp = st; c.has = 0; }
		return c;
	}
	int BiomeIdAt(const void* world, int x, int y) {          // jeder Thread; 0 = kein eigenes Biom (Gewicht < 0.5)
		HookSet* hs = g_hs; if (!g_on || !hs || !world) return 0;
		Dom d = DominantRaw((const uint8_t*)world, x, y);
		if (!d.id || d.w < 0.5f) return 0;
		const HookDef& h = hs->d[d.id - 1];
		return (h.used && h.enabled) ? d.id : 0;
	}
	bool IsNativeBiome(int id) { HookSet* hs = g_hs; return hs && id >= 1 && id <= kMaxBiomes && hs->d[id - 1].used && hs->d[id - 1].nativeClass > 0; }
	static Dom Dominant(const uint8_t* world, int x, int y) {
		ColInfo& c = Col(world, x, y);
		if (!(c.has & 1)) { c.d = DominantRaw(world, x, y); c.has |= 1; }
		return c.d;
	}
	static float Smooth(float w) { if (w <= 0) return 0; if (w >= 1) return 1; return w * w * (3 - 2 * w); }
	static Wide WideAt(const uint8_t* world, int x, int y) {
		ColInfo& c = Col(world, x, y);
		if (!(c.has & 64)) { c.wd = WideRaw(world, x, y); c.has |= 64; }
		return c.wd;
	}
	// Anteil des Bioms id an der Saeule (breites Gewicht); Seen/Fluesse nur, wo er praktisch 1 ist
	static float WideShare(const Wide& wd, int id) { for (int k = 0; k < wd.n; k++) if (wd.id[k] == id) return wd.a[k]; return 0.0f; }
	static const float kDeepShare = 0.97f;
	static volatile LONG g_regionEpoch = 0;          // jede neue/nachgefilterte Region: Saeulen-Speicher ungueltig
	static uint32_t ColStamp(const void* w) {
		// Die Hooks bekommen immer die gueltige Welt des Spiels -> Samen direkt lesen (ohne SEH-Rahmen,
		// das ist hier der haeufigste Aufruf ueberhaupt).
		uint32_t seed = *(const uint32_t*)((const uint8_t*)w + W_SEED);
		return seed * 0x9E3779B1u ^ (uint32_t)(uintptr_t)g_hs * 0x85EBCA77u ^ ((uint32_t)g_modVersion << 1) ^ ((uint32_t)g_regionEpoch << 13) ^ (g_on ? 1u : 0u);
	}

	// ---- Freie Ebene in der Zellmitte -------------------------------------------
	// Maske 0..1 (1 = flache Ebene, weich auf 0 am Ende des Rands), schon mit dem Biomgewicht.
	static float PlainAt(const HookDef& b, const Dom& d, int x, int y) {
		if (!b.plainOn || !d.id) return 0.0f;
		return PlainMask(b.plainRadius, b.plainRim, (double)x - d.px, (double)y - d.py) * Smooth(d.w);
	}
	// Liegt (x, y) +- margin im Bereich der Ebene samt Rand? (fuer Orte, Baeume, Pflanzen, Dungeons, Minen)
	static bool InPlain(bool on, float radius, float rim, const Dom& d, int x, int y, float margin) {
		if (!on || !d.id) return false;
		double dx = (double)x - d.px, dy = (double)y - d.py, r = radius + rim + margin;
		return dx * dx + dy * dy < r * r;
	}

	// =====================================================================
	// 4. Hooks
	// =====================================================================
	typedef void*  (__fastcall* Cell_t)(void* w, void* edx, int cx, int cy);
	typedef void   (__fastcall* Region_t)(void* w, void* edx, int rx, int ry);
	typedef void*  (__fastcall* Surface_t)(void* w, void* edx, uint8_t* out, int x, int y, int lvl, uint32_t hum, uint32_t temp, uint32_t p7);
	typedef float* (__fastcall* Rock_t)(void* w, void* edx, float* out, int x, int y, int lvl, uint32_t p5);
	typedef int    (__fastcall* Race_t)(void* w, void* edx, int x, int y, int height, int special);
	static Cell_t o_Cell = nullptr; static Region_t o_Region = nullptr; static Surface_t o_Surface = nullptr;
	static Rock_t o_Rock = nullptr; static Race_t o_Race = nullptr;

	// ---- 4a. Klimazelle: Biom zuweisen -----------------------------------------
	static bool CellSlotEmpty(const uint8_t* world, int cx, int cy) {
		__try { return ((const uint8_t* const*)(world + W_CELLGRID))[cx * 1024 + cy] == nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool Eligible(int req, float T, float H, bool lava) {
		switch (req) {
			case CR_TEMPERATE: return !lava && T >= 0.35f && T <= 0.85f && H >= 0.25f && H <= 0.75f;
			case CR_HOT:  return !lava && T >= 0.8f;
			case CR_COLD: return T <= 0.2f;
			case CR_WET:  return H >= 0.8f;
			case CR_DRY:  return !lava && H <= 0.2f;
			default:      return true;
		}
	}
	// Reine Entscheidung (auch fuer den Seed-Rechner SeedScout): welches Biom bekommt eine
	// frisch angelegte Zelle? T = Zelle+0x10, H = Zelle+0x0C (werden ggf. ueberschrieben),
	// lava = Zelle+0x08. Rueckgabe: Biom-ID 1..15 oder 0.
	static int DecideCell(const HookSet* hs, uint32_t seed, int cx, int cy, int elev, bool isStart, float* T, float* H, bool* lava) {
		if (!g_on || !hs || elev < 1) return 0;
		int id = 0;
		// 1. von Hand erzwungen? (gilt auch fuer die Startzelle)
		for (int k = 0; k < hs->nForced && !id; k++) {
			const ForcedCell& f = hs->forced[k];
			if (f.seed != seed || f.cx != cx || f.cy != cy || f.id < 1 || f.id > kMaxBiomes) continue;
			const HookDef& h = hs->d[f.id - 1];
			if (!h.used || !h.enabled) break;
			id = f.id;
		}
		// 2. sonst wuerfeln: Land, nicht die Startzelle. Jedes Biom wuerfelt mit seinem eigenen Salz
		//    (Dateiname); gewinnen mehrere, entscheidet ein zweiter Wurf -- beides unabhaengig davon,
		//    welche anderen Biome es gibt und in welcher Reihenfolge sie geladen wurden.
		if (!id && !isStart) {
			uint32_t bestKey = 0xFFFFFFFFu;
			for (int i = 0; i < kMaxBiomes; i++) {
				const HookDef& h = hs->d[i];
				if (!h.used || !h.enabled || h.share <= 0 || h.nativeClass > 0) continue;
				if (!Eligible(h.climateReq, *T, *H, *lava)) continue;
				if (Hash((uint32_t)cx, (uint32_t)cy, seed, 0xB10E0000u ^ h.salt) % 100u >= (uint32_t)h.share) continue;
				uint32_t key = Hash((uint32_t)cx, (uint32_t)cy, seed, 0x5E1EC700u ^ h.salt);
				if (!id || key < bestKey || (key == bestKey && h.salt < hs->d[id - 1].salt)) { bestKey = key; id = i + 1; }
			}
		}
		if (id) {
			const HookDef& h = hs->d[id - 1];
			if (h.setClimate) { *T = h.temp; *H = h.hum; }
			*lava = false;                                                   // nicht zugleich Lava
			return id;
		}
		// 3. kein eigenes Biom: gilt eine Datei fuer die native Klimaklasse dieser Zelle? (Klima/Lava bleiben nativ)
		const int nc = BiomeGen::NativeClassOf(*T, *H, *lava);
		if (nc > 0)
			for (int i = 0; i < kMaxBiomes; i++) {
				const HookDef& h = hs->d[i];
				if (!h.used || !h.enabled || h.nativeClass != nc || h.share <= 0) continue;
				if (h.share < 100 && Hash((uint32_t)cx, (uint32_t)cy, seed, 0xA71F0000u ^ h.salt) % 100u >= (uint32_t)h.share) continue;
				return i + 1;
			}
		return 0;
	}
	static void AssignCellRaw(uint8_t* world, uint8_t* c, int cx, int cy) {
		__try {
			int elev = *(int*)(c + 0x18);
			int sx = (int)*(float*)(world + W_STARTX) / 16384, sy = (int)*(float*)(world + W_STARTY) / 16384;
			uint32_t seed = *(uint32_t*)(world + W_SEED);
			float T = *(float*)(c + 0x10), H = *(float*)(c + 0x0C);
			bool lava = c[8] == 1;
			int id = DecideCell(g_hs, seed, cx, cy, elev, cx == sx && cy == sy, &T, &H, &lava);
			if (id && g_hs && g_hs->d[id - 1].nativeClass == 0) { *(float*)(c + 0x10) = T; *(float*)(c + 0x0C) = H; c[8] = 0; }
			g_cell[cx * 1024 + cy] = (uint8_t)id;
			if (id) InterlockedIncrement(&c_cells);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void* __fastcall Hook_Cell(void* w, void* edx, int cx, int cy) {
		bool fresh = cx >= 0 && cy >= 0 && cx < 1024 && cy < 1024 && CellSlotEmpty((uint8_t*)w, cx, cy);
		void* c = o_Cell(w, edx, cx, cy);
		if (fresh && c) AssignCellRaw((uint8_t*)w, (uint8_t*)c, cx, cy);
		return c;
	}

	// ---- 4b. Region: native Orte je Biom abschalten ------------------------------
	static bool RegionSlotEmpty(const uint8_t* world, int rx, int ry) {
		__try { return ((const uint8_t* const*)(world + W_REGIONGRID))[rx * 1024 + ry] == nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Orte werden je Region einmal angelegt und bleiben dann im Speicher -- auch ueber
	// "Welt verlassen / neu betreten" hinweg. Ein Biom, das erst danach erzwungen oder
	// geaendert wird, muss deshalb auch BESTEHENDE Regionen nachfiltern. Damit ein spaeter
	// wieder erlaubter Ort zurueckkommt, wird der Originaltyp gemerkt.
	struct SiteRaw { int kind, x, y, id; };
	static bool ReadSiteRaw(uint8_t* world, int rx, int ry, int i, SiteRaw* o) {
		__try {
			uint8_t* reg = ((uint8_t**)(world + W_REGIONGRID))[rx * 1024 + ry];
			if (!reg) return false;
			uint8_t* e = reg + 0x14018 + i * 0x68;
			o->kind = *(int*)(e + 0x18);
			o->x = (int)(*(int64_t*)(e + 0x00) >> 16); o->y = (int)(*(int64_t*)(e + 0x08) >> 16);
			o->id = *(int*)(e + 0x20);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool WriteSiteRaw(uint8_t* world, int rx, int ry, int i, int kind) {
		__try {
			uint8_t* reg = ((uint8_t**)(world + W_REGIONGRID))[rx * 1024 + ry];
			if (!reg) return false;
			*(int*)(reg + 0x14018 + i * 0x68 + 0x18) = kind;              // Typ 0 = nichts (WorldInfo.cpp:3740)
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Zonen-Datensaetze der Region (Region+0x18 + idx*0x10, idx = zx*64 + zy, 64x64 Zonen).
	// Byte +0 markiert Zonen, in denen gebaut wird -- UNABHAENGIG vom Ortseintrag (generateRegion):
	//   1 = Stadtviertel (Byte +1 = 1..4: Handels-/Handwerks-/Klassen-/Tierviertel)  0x5DBF5D..0x5DBFBD
	//   3 = Zone eines grossen Dungeons (Ortstyp 14)                                   0x5DAE3F
	//   4 = Portal (Ortstyp 10)                                                         0x5DB5A2
	static bool ReadRecordsRaw(uint8_t* world, int rx, int ry, uint8_t* kinds) {
		__try {
			uint8_t* reg = ((uint8_t**)(world + W_REGIONGRID))[rx * 1024 + ry];
			if (!reg) return false;
			for (int k = 0; k < 4096; k++) kinds[k] = reg[0x18 + k * 0x10];
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool WriteRecordRaw(uint8_t* world, int rx, int ry, int idx, uint8_t kind) {
		__try {
			uint8_t* reg = ((uint8_t**)(world + W_REGIONGRID))[rx * 1024 + ry];
			if (!reg) return false;
			reg[0x18 + idx * 0x10] = kind;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool SiteAllowed(const uint8_t* world, const SiteRaw& sr, int kind) {
		HookSet* hs = g_hs;
		if (!g_on || !hs || kind <= 0 || kind > 15) return true;
		Dom d = DominantRaw(world, sr.x, sr.y);
		if (!d.id || d.w < 0.5f) return true;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled) return true;
		if (InPlain(h.plainOn, h.plainRadius, h.plainRim, d, sr.x, sr.y, 160.0f)) return false;   // freie Ebene
		return (h.siteMask & (1u << kind)) != 0;
	}
	namespace {
		CRITICAL_SECTION g_scs; bool g_scsInit = false;
		std::map<uint64_t, int> g_siteOrig;                           // (Welt, rx, ry, i) -> Originaltyp
		volatile LONG g_refilter = 1;                                 // 1 = alle Regionen nachfiltern (Spielthread)
		volatile LONG c_sitesNow = 0;                                 // derzeit abgeschaltete Orte
	}
	// slot 0..63 = Ortseintrag, 64 + idx = Zonen-Datensatz
	static uint64_t SiteKey(const uint8_t* world, int rx, int ry, int slot) {
		return ((uint64_t)(uintptr_t)world << 32) ^ ((uint64_t)rx << 23) ^ ((uint64_t)ry << 13) ^ (uint64_t)slot;
	}
	static int RecordToSiteKind(int rec) { return rec == 1 ? 1 : rec == 3 ? 14 : rec == 4 ? 10 : 0; }
	static void ApplySites(uint8_t* world, int rx, int ry) {
		if (!g_scsInit) return;
		for (int i = 0; i < 64; i++) {
			SiteRaw sr;
			if (!ReadSiteRaw(world, rx, ry, i, &sr)) return;
			uint64_t key = SiteKey(world, rx, ry, i);
			EnterCriticalSection(&g_scs);
			auto it = g_siteOrig.find(key);
			int orig = it != g_siteOrig.end() ? it->second : sr.kind;
			bool saved = it != g_siteOrig.end();
			LeaveCriticalSection(&g_scs);
			if (orig <= 0 || orig > 15) continue;
			bool allowed = SiteAllowed(world, sr, orig);
			if (!allowed && sr.kind != 0) {
				EnterCriticalSection(&g_scs); g_siteOrig[key] = orig; LeaveCriticalSection(&g_scs);
				if (WriteSiteRaw(world, rx, ry, i, 0)) { InterlockedIncrement(&c_sites); InterlockedIncrement(&c_sitesNow); }
			} else if (allowed && saved && sr.kind == 0) {
				if (WriteSiteRaw(world, rx, ry, i, orig)) InterlockedDecrement(&c_sitesNow);
				EnterCriticalSection(&g_scs); g_siteOrig.erase(key); LeaveCriticalSection(&g_scs);
			}
		}
		// Zonen-Datensaetze (Stadtviertel, Dungeon-Zonen, Portale)
		static thread_local uint8_t recs[4096];
		if (!ReadRecordsRaw(world, rx, ry, recs)) return;
		for (int idx = 0; idx < 4096; idx++) {
			uint64_t key = SiteKey(world, rx, ry, 64 + idx);
			int cur = recs[idx];
			int orig = cur;
			bool saved = false;
			if (cur == 0) {                                                // evtl. von uns geloescht?
				EnterCriticalSection(&g_scs);
				auto it = g_siteOrig.find(key);
				if (it != g_siteOrig.end()) { orig = it->second; saved = true; }
				LeaveCriticalSection(&g_scs);
			}
			int sk = RecordToSiteKind(orig);
			if (!sk) continue;
			SiteRaw zr; zr.kind = sk; zr.id = 0;
			zr.x = rx * 16384 + (idx / 64) * 256 + 128; zr.y = ry * 16384 + (idx % 64) * 256 + 128;
			bool allowed = SiteAllowed(world, zr, sk);
			if (!allowed && cur != 0) {
				EnterCriticalSection(&g_scs); g_siteOrig[key] = cur; LeaveCriticalSection(&g_scs);
				if (WriteRecordRaw(world, rx, ry, idx, 0)) { InterlockedIncrement(&c_sites); InterlockedIncrement(&c_sitesNow); }
			} else if (allowed && saved) {
				if (WriteRecordRaw(world, rx, ry, idx, (uint8_t)orig)) InterlockedDecrement(&c_sitesNow);
				EnterCriticalSection(&g_scs); g_siteOrig.erase(key); LeaveCriticalSection(&g_scs);
			}
		}
	}
	// Alle angelegten Regionen (Raster 1024x1024) nachfiltern. Nur Spielthread.
	static int ListRegionsRaw(const uint8_t* world, int* out, int cap) {
		int n = 0;
		__try {
			const uint8_t* const* grid = (const uint8_t* const*)(world + W_REGIONGRID);
			for (int k = 0; k < 1024 * 1024 && n < cap; k++) if (grid[k]) out[n++] = k;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}
	static int RefilterAll(uint8_t* world) {
		static int list[8192];
		int n = ListRegionsRaw(world, list, 8192);
		for (int k = 0; k < n; k++) ApplySites(world, list[k] / 1024, list[k] % 1024);
		InterlockedIncrement(&g_regionEpoch);
		return n;
	}

	static void __fastcall Hook_Region(void* w, void* edx, int rx, int ry) {
		bool fresh = rx >= 0 && ry >= 0 && rx < 1024 && ry < 1024 && RegionSlotEmpty((uint8_t*)w, rx, ry);
		o_Region(w, edx, rx, ry);
		if (fresh) ApplySites((uint8_t*)w, rx, ry);
		if (fresh) InterlockedIncrement(&g_regionEpoch);           // Hoehen koennen sich durch die Orte aendern
	}

	// ---- 4c. Boden ------------------------------------------------------------
	static void* __fastcall Hook_Surface(void* w, void* edx, uint8_t* out, int x, int y, int lvl, uint32_t hum, uint32_t temp, uint32_t p7) {
		void* r = o_Surface(w, edx, out, x, y, lvl, hum, temp, p7);
		HookSet* hs = g_hs;
		if (!g_on || !hs || !out) return r;
		Dom d = Dominant((const uint8_t*)w, x, y);
		if (!d.id) return r;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled) return r;
		float t = Smooth(d.w) * h.strength;
		float n = ValueNoise2(x * 0.06, y * 0.06, 0xB10Eu) * 0.6f + ValueNoise2(x * 0.015, y * 0.015, 0xB10Fu) * 0.4f;
		for (int i = 0; i < 3; i++) {
			float col = h.g1[i] + (h.g2[i] - h.g1[i]) * n;
			float v = out[i] * (1.0f - t) + col * t;
			out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
		}
		if (h.groundType && d.w > 0.5f) out[3] = (uint8_t)((out[3] & 0xE0) | (h.groundType & 0x1F));
		// Schneegrenze: lvl = Oberflaechenhoehe der Saeule (Aufruf 0x5E532E uebergibt z)
		if (h.snowLine > 0.0f && d.w > 0.5f) {
			float jitter = (ValueNoise2(x * 0.05, y * 0.05, 0x5A0Bu) - 0.5f) * 16.0f;
			// Felsband: in den 40 Bloecken unter der Schneegrenze geht der Boden in die Felsfarbe ueber
			float kr = ((float)lvl - (h.snowLine - 40.0f + jitter)) / 40.0f;
			if (kr > 0.0f && h.rockOn) {
				if (kr > 1.0f) kr = 1.0f;
				kr *= 0.85f;
				for (int i = 0; i < 3; i++) { float v = out[i] * (1.0f - kr) + h.rock[i] * kr; out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
			}
			float k = ((float)lvl - (h.snowLine + jitter)) / 10.0f;
			if (k > 0.0f) {
				if (k > 1.0f) k = 1.0f;
				const float snow[3] = { 236.0f, 240.0f, 246.0f };
				for (int i = 0; i < 3; i++) { float v = out[i] * (1.0f - k) + snow[i] * k; out[i] = (uint8_t)(v > 255 ? 255 : v); }
				if (k > 0.5f) out[3] = (uint8_t)((out[3] & 0xE0) | 10);       // Typ 10 = Schnee (wie 0x5C56E0)
			}
		}
		return r;
	}

	// ---- 4d. Fels -------------------------------------------------------------
	static float* __fastcall Hook_Rock(void* w, void* edx, float* out, int x, int y, int lvl, uint32_t p5) {
		float* r = o_Rock(w, edx, out, x, y, lvl, p5);
		HookSet* hs = g_hs;
		if (!g_on || !hs || !out) return r;
		Dom d = Dominant((const uint8_t*)w, x, y);
		if (!d.id) return r;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled || !h.rockOn) return r;
		float t = Smooth(d.w) * h.strength * 0.85f;
		float n = 0.85f + 0.3f * ValueNoise2(x * 0.1 + lvl * 0.37, y * 0.1, 0xF015u);
		for (int i = 0; i < 3; i++) {
			float v = out[i] * (1.0f - t) + h.rock[i] * n * t;
			out[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
		}
		return r;
	}

	// ---- 4e. Rasse der nativen Spawns -------------------------------------------
	static int __fastcall Hook_Race(void* w, void* edx, int x, int y, int height, int special) {
		int race = o_Race(w, edx, x, y, height, special);
		HookSet* hs = g_hs;
		if (!g_on || !hs || (special & 0xFF) || height < 0) return race;   // Voelker-/Wasser-Tabellen bleiben
		Dom d = Dominant((const uint8_t*)w, x, y);
		if (!d.id || d.w < 0.5f) return race;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled || h.nRaces <= 0) return race;
		uint32_t q = Hash((uint32_t)x, (uint32_t)y, (uint32_t)height, 0x7ACE0000u + (uint32_t)d.id);
		if ((float)(q & 0xFFFF) / 65536.0f >= h.raceShare) return race;
		InterlockedIncrement(&c_races);
		return h.races[(q >> 16) % (uint32_t)h.nRaces];
	}

	// ---- 4g. Gelaende ---------------------------------------------------------
	// Grundhoehe: zum Ziel der Biomform ziehen. Meeresspiegel 0: generateBiomeContent
	// fuellt alles unter Hoehe 1 mit Wasser (WorldInfo.cpp:1958, Typbyte 2).
	typedef float (__fastcall* Height_t)(void* w, void* edx, int x, int y, uint32_t p3);
	typedef float (__fastcall* Mount_t)(void* w, void* edx, int x, int y, uint32_t base);
	typedef void  (__fastcall* Tree_t)(void* w, void* edx, int x, int y, int z, int a4, int a5, int a6, void* zone);
	static Height_t o_Height = nullptr; static Mount_t o_Mount = nullptr; static Tree_t o_Tree = nullptr;
	static volatile LONG c_trees = 0, c_mines = 0;

	static float __fastcall Hook_River(void* w, void* edx, int x, int y, uint32_t p3);
	static float RidgeAt(const HookDef& b, int x, int y, float* mask) {
		if (b.ridgeHeight <= 0.0f) { if (mask) *mask = 0.0f; return 0.0f; }
		return RidgeTerm(b.ridgeHeight, b.ridgeWidth, b.ridgeSpacing, b.ridgeRough, x, y, mask);
	}
	// Seen nur in Taelern: liegt die Seemitte auf einem Bergzug, gibt es dort keinen See.
	static bool LakeHere(const HookDef& b, int x, int y, float* dn, int* lx, int* ly, float* rad = nullptr) {
		if (b.lakeShare <= 0.0f || !LakeAt(b.lakeShare, b.lakeSize, x, y, dn, lx, ly, rad)) return false;
		float m = 0.0f; RidgeAt(b, *lx, *ly, &m);
		return m < 0.03f;
	}
	// ---- Sternmassiv ------------------------------------------------------------------
	static uint32_t StarSeed(const Dom& d) { return Hash((uint32_t)d.px, (uint32_t)d.py, 0x57A55EEDu); }
	static float BaseTarget(const HookDef& b, int x, int y) {
		return TerrainTarget(b.baseHeight, b.waves, b.waveSize, b.hillShare, b.hillHeight, b.hillSize, x, y) + RidgeAt(b, x, y, nullptr);
	}
	// Stern an (x, y)? sm = Massiv-Maske mal Biomgewicht. base nur fuer ss->h noetig (sonst 0 uebergeben).
	static bool StarHere(const void* w, const HookDef& b, const Dom& d, int x, int y, float base, StarSample* ss, float* sm) {
		*sm = 0.0f;
		if (!b.star.on || !d.id) return false;
		ColInfo& c = Col(w, x, y);
		if (!(c.has & 2)) {
			StarSample t; StarAt(b.star, StarSeed(d), (double)x - d.px, (double)y - d.py, 0.0f, &t);
			ColInfo& c2 = Col(w, x, y);
			c2.sMask = t.mask; c2.sRiver = t.riverBlocks; c2.sHRaw = t.hRaw; c2.sFade = t.fade;
			c2.sLake = t.lake; c2.sLakeX = (float)t.lakeX; c2.sLakeY = (float)t.lakeY; c2.sLakeR = t.lakeR; c2.has |= 2;
		}
		ColInfo& c3 = Col(w, x, y);
		ss->mask = c3.sMask; ss->riverBlocks = c3.sRiver; ss->hRaw = c3.sHRaw; ss->fade = c3.sFade;
		ss->h = c3.sHRaw * (1.0f - c3.sFade) + base * c3.sFade;
		ss->lake = c3.sLake; ss->lakeX = c3.sLakeX; ss->lakeY = c3.sLakeY; ss->lakeR = c3.sLakeR; ss->floor = false;
		*sm = ss->mask * Smooth(d.w);
		return ss->mask > 0.0f;
	}
	// Biomform ohne Seen/Fluesse/Ebene (Grundlage fuer den Seespiegel)
	// Nicht ueber den Saeulen-Speicher: die Probepunkte rechnen mit dem Biom der SEE-Saeule (d).
	static float FormTarget(const HookDef& b, const Dom& d, int x, int y) {
		float base = BaseTarget(b, x, y);
		if (b.star.on && d.id) {
			StarSample ss; StarAt(b.star, StarSeed(d), (double)x - d.px, (double)y - d.py, base, &ss);
			if (ss.mask > 0.0f) return base + (ss.h - base) * Smooth(d.w);
		}
		return base;
	}
	// Ein See an (x, y): im Sternmassiv nur die Talend-Seen, sonst die Rasterseen.
	static bool LakeQueryRaw(const HookDef& b, const Dom& d, int x, int y, float sm, const StarSample* ss, LakeQ* q);
	// See an der Saeule -- je Saeule einmal gerechnet (haengt nur von der Saeule ab)
	static bool LakeQuery(const void* w, const HookDef& b, const Dom& d, int x, int y, float sm, const StarSample* ss, LakeQ* q) {
		ColInfo& c = Col(w, x, y);
		if (!(c.has & 4)) {
			LakeQ t; bool ok = LakeQueryRaw(b, d, x, y, sm, ss, &t);
			ColInfo& c2 = Col(w, x, y); c2.lakeOk = ok; if (ok) c2.lq = t; c2.has |= 4;
		}
		ColInfo& c3 = Col(w, x, y);
		if (c3.lakeOk) *q = c3.lq;
		return c3.lakeOk;
	}
	static bool LakeQueryRaw(const HookDef& b, const Dom& d, int x, int y, float sm, const StarSample* ss, LakeQ* q) {
		if (ss && sm > 0.02f) {
			if (!ss->lake || ss->lakeR <= 0.0f) return false;
			double lx = d.px + ss->lakeX, ly = d.py + ss->lakeY;
			float wob = 1.0f + 0.12f * (ValueNoise2(x / 14.0, y / 14.0, 0x5EE6u) - 0.5f) * 2.0f;   // unregelmaessiges Ufer
			q->dn = (float)(std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly)) / (ss->lakeR * wob));
			q->lx = (int)std::floor(lx); q->ly = (int)std::floor(ly); q->R = ss->lakeR;
			return true;
		}
		if (LakeHere(b, x, y, &q->dn, &q->lx, &q->ly, &q->R) && !InPlain(b.plainOn, b.plainRadius, b.plainRim, d, q->lx, q->ly, b.lakeSize)) return true;
		return false;
	}

	// Wasserspiegel eines eigenen Sees (mit Stufen-Patch): der niedrigste Punkt der Biomform
	// (Wellen + Huegel + Bergzuege) im Uferring 0.8 R .. 1.9 R, abgerundet. Damit liegt jedes
	// Ufer auf oder ueber dem obersten Wasserblock; der tiefste Uferblock ist mit ihm buendig.
	// Je Thread fuer die letzten Seen gemerkt (der Satz g_hs wechselt bei jedem Neuladen).
	static float LakeLevel(const HookDef& b, const Dom& d, int lx, int ly, float R) {
		const int id = d.id;
		struct E { int lx, ly, id; const void* hs; float level; };
		static thread_local E cache[16]; static thread_local int next = 0;
		const void* hs = g_hs;
		for (const E& e : cache) if (e.hs == hs && e.id == id && e.lx == lx && e.ly == ly) return e.level;
		float mn = 1e9f;
		const float rings[] = { 0.8f, 1.0f, 1.2f, 1.45f, 1.7f, 1.9f };
		for (float rf : rings) for (int k = 0; k < 40; k++) {
			double a = k * (6.283185307179586 / 40.0);
			int sx = lx + (int)std::lround(std::cos(a) * R * rf), sy = ly + (int)std::lround(std::sin(a) * R * rf);
			float v = FormTarget(b, d, sx, sy);
			if (v < mn) mn = v;
		}
		float level = std::floor(mn);
		if (level < 2.0f) level = 2.0f;                                    // darunter Meer-Logik
		E& e = cache[next]; next = (next + 1) & 15;
		e.lx = lx; e.ly = ly; e.id = id; e.hs = hs; e.level = level;
		return level;
	}
	// Flussschleife: Aufruf der Hoehe bei 0x5E7EFE (Ruecksprung 0x5E7F03). Ist der Stufen-Patch
	// aktiv (s. InstallRiverPatch), bekommt NUR dieser Aufruf floor(h) + 0.5: Spiegel = floor(h),
	// Bruchteil q = 0.5 -> jede Flusssaeule voll (4 Bloecke), Wasserspiegel 1-Block-genau.
	static const uint32_t A_RIVER_HEIGHT_RET = 0x005E7F03;
	static volatile bool g_riverPatch = false;
	static float HeightCore(void* w, void* edx, int x, int y, uint32_t p3);
	static float __fastcall Hook_Height(void* w, void* edx, int x, int y, uint32_t p3) {
		uint32_t ret = (uint32_t)_ReturnAddress();
		CrashLogger::PerfCount(CrashLogger::PC_HEIGHT_CALLS);
		float h;
		{
			ColInfo& c = Col(w, x, y);
			if (c.has & 8) h = c.height;
			else { h = HeightCore(w, edx, x, y, p3); ColInfo& c2 = Col(w, x, y); c2.height = h; c2.has |= 8; }
		}
		if (g_riverPatch && ret == Re(A_RIVER_HEIGHT_RET)) return std::floor(h < 0 ? 0 : h) + 0.5f;
		return h;
	}
	// Hoehe EINES Bioms an der Saeule, als ob es dort ganz gaelte (Anteil 1). dom = das nach der Spielgrenze
	// herrschende Biom (Saeulen-Speicher fuer Stern/See); deep = so tief im Biom, dass Seen und Fluesse gelten.
	static float BiomeHeight(void* w, void* edx, const HookDef& b, const Dom& d, bool dom, bool deep, int x, int y, float h) {
		Dom d1 = d; d1.w = 1.0f;
		float pm = PlainAt(b, d1, x, y);                                  // freie Ebene (auch ohne "Gelaende formen")
		if (!b.terrainOn) return pm > 0.0f ? h + (b.plainHeight - h) * pm : h;
		float t = b.flatten;
		float target = BaseTarget(b, x, y);                               // Wellen, Huegel, Bergzuege
		// Sternmassiv: ersetzt die Biomform im Massiv ganz (aussen weicher Uebergang des Massivs selbst)
		StarSample ss; float sm = 0.0f; bool star = false;
		if (b.star.on) {
			if (dom) star = StarHere(w, b, d1, x, y, target, &ss, &sm);
			else { StarAt(b.star, StarSeed(d), (double)x - d.px, (double)y - d.py, target, &ss); sm = ss.mask; star = ss.mask > 0.0f; }
		}
		if (star) { target = ss.h; if (sm > t) t = sm; }
		// Seen. Die Flussschleife des Spiels (generateBiomeContent, WorldInfo.cpp:2697ff.)
		// fuellt Wasser NICHT bis zur Gelaendehoehe h, sondern:
		//     Spiegel = (h/5)*5,  Bruchteil q = (h - Spiegel)/5,  Profil f = q*2 bzw. 1-(q-0.5)*4,
		//     Wasser von (Spiegel - 5f + 2) bis Spiegel  -> volle 4 Bloecke Wasser nur bei q = 0.5,
		//     bei q nahe 0 gar keins; darueber wird bis h + 2..9 Luft geschnitten.
		// Mit beliebigem h entstanden deshalb trockene oder halb gefuellte Loecher bis 5 Bloecke
		// unter dem Gelaende. Jetzt: im See h = Spiegel + 2.5 (q = 0.5, voller Wasserkoerper),
		// Spiegel = Seemitte auf 5 Bloecke gerundet; das Ufer laeuft von Spiegel + 1 weich ins Gelaende.
		float dn, lr = 0.0f; int lx, ly;
		float lakeFloor = -1e9f;                                          // Ufer nie unter den Wasserspiegel
		// Nur tief im Biom (breiter Anteil >= 0.97): am Rand wuerde die Mischung mit der nativen Hoehe den
		// Wasserspiegel verschieben. Rasterseen nahe der Ebene entfallen ganz; im Sternmassiv
		// gibt es nur die Seen an den Talenden.
		LakeQ lq;
		bool lakeHit = deep && LakeQuery(w, b, d, x, y, sm, star ? &ss : nullptr, &lq) && lq.dn < 1.6f;
		if (lakeHit) { dn = lq.dn; lx = lq.lx; ly = lq.ly; lr = lq.R; }
		if (lakeHit) {
			float s = 0.0f;
			if (dn >= 1.0f) { s = (dn - 1.0f) / 0.6f; if (s > 1.0f) s = 1.0f; s = s * s * (3 - 2 * s); }
			float lakeH, shoreH;
			if (g_riverPatch) {
				// Mit Stufen-Patch: oberster Wasserblock = floor(h) der Seesaeule (h = Spiegel + 0.5).
				// Spiegel = niedrigster Uferblock: Minimum der Biomform auf dem Uferring (LakeLevel).
				// Das Ufer beginnt auf Spiegel + 0.5 (oberster Block = Spiegel, buendig mit dem Wasser)
				// und steigt dann ins Gelaende -- es liegt nirgends tiefer als das Wasser.
				float level = LakeLevel(b, d, lx, ly, lr);
				lakeH = level + 0.5f; shoreH = level + 0.5f;
				lakeFloor = level + 0.5f;
			} else {
				// Ohne Patch (5er-Stufen): Spiegel auf 5 gerundet, h = Spiegel + 2.5 -> q = 0.5
				float mid = TerrainTarget(b.baseHeight, b.waves, b.waveSize, 0.0f, 0.0f, b.hillSize, lx, ly);
				float level = std::floor(mid / 5.0f + 0.5f) * 5.0f;
				if (level < 5.0f) level = 5.0f;
				lakeH = level + 2.5f; shoreH = level + 1.0f;
			}
			float base = dn >= 1.0f ? shoreH : lakeH;
			target = base + (target - base) * s;
			float tk = 1.0f - s;                                          // im See exakt
			if (tk > t) t = tk;
		}
		// Fluesse: dieselbe Regel. Im Flussbett h = Stufe + 2.5 (Stufe = Gelaende auf 5 abgerundet),
		// damit jeder Abschnitt voll Wasser fuehrt; die Ufer laufen auf Stufe + 1 aus. Wo die Stufe
		// springt, entstehen Wasserfaelle wie bei den nativen Fluessen.
		if (deep && !g_riverPatch) {                           // mit Stufen-Patch unnoetig (Wasser folgt dem Gelaende)
			float rr = Hook_River(w, edx, x, y, 0);                       // p3 = Zone, 0 erlaubt (0x5F95DC -> getChunkCell256)
			if (rr < 0.035f) {
				float lv = std::floor(target / 5.0f) * 5.0f;
				if (lv >= 5.0f) {
					if (rr < 0.02f) { target = lv + 2.5f; t = 1.0f; }
					else {
						float k = 1.0f - (rr - 0.02f) / 0.015f; k = k * k * (3 - 2 * k);
						target = target + (lv + 1.0f - target) * k;
						if (k > t) t = k;
					}
				}
			}
		}
		// Freie Ebene zuletzt: ersetzt Wellen, Huegel, Bergzuege und Seen ganz
		if (pm > 0.0f) { target = target + (b.plainHeight - target) * pm; if (pm > t) t = pm; }
		float res = h + (target - h) * t;
		if (res < lakeFloor) res = lakeFloor;                             // Ufer: native Resthoehe darf nicht darunter ziehen
		return res;
	}
	// Grundhoehe: Summe der Biomhoehen nach breitem Anteil + natives Gelaende nach seinem Anteil (WideRaw).
	static float HeightCore(void* w, void* edx, int x, int y, uint32_t p3) {
		float h = o_Height(w, edx, x, y, p3);
		HookSet* hs = g_hs;
		if (!g_on || !hs) return h;
		Wide wd = WideAt((const uint8_t*)w, x, y);
		if (wd.n == 0) return h;
		Dom d = Dominant((const uint8_t*)w, x, y);
		float res = wd.a0 * h;
		for (int k = 0; k < wd.n; k++) {
			const HookDef& b = hs->d[wd.id[k] - 1];
			float R = h;
			if (b.used && b.enabled) {
				bool dom = d.id == wd.id[k];
				Dom dd = d; if (!dom) { dd.id = wd.id[k]; dd.w = 1.0f; dd.px = wd.px[k]; dd.py = wd.py[k]; }
				R = BiomeHeight(w, edx, b, dd, dom, dom && wd.a[k] >= kDeepShare, x, y, h);
			}
			res += wd.a[k] * R;
		}
		return res;
	}

	// Flussabstand r: Fluss und See wo r < 0.02. Das Spiel fuellt dort Wasser auf die
	// Gelaendehoehe (abgestuft auf 5 Bloecke) -- also auch hoch ueber dem Meer.
	typedef float (__fastcall* River_t)(void* w, void* edx, int x, int y, uint32_t p3);
	static River_t o_River = nullptr;
	static float __fastcall Hook_Height(void* w, void* edx, int x, int y, uint32_t p3);
	static float HeightCore(void* w, void* edx, int x, int y, uint32_t p3);
	static const uint32_t A_RIVER_LOOP_RET = 0x005E7E8A;
	static const bool kRiverTweaks = true;                // 1-Block-Stufen (Wasser faellt blockweise mit dem Gelaende)
	static const bool kRiverCrossCheck = true;            // Hangpruefung "schraeg ueber den Hang" (nur in der Flussschleife)   // Flussschleife: call 0x5F9340 @0x5E7E85 (WorldInfo.cpp:2697ff.)
	// Flussabstand einer Saeule (Biome, Sternmassiv, Seen, Ebene) -- je Saeule einmal gerechnet.
	static float RiverCore(void* w, void* edx, int x, int y, uint32_t p3, float* widthOut);
	static float RiverCore(void* w, void* edx, int x, int y, uint32_t p3, float* widthOut) {
		CrashLogger::PerfCount(CrashLogger::PC_RIVER_CALLS);
		{
			ColInfo& c = Col(w, x, y);
			if (c.has & 16) { if (widthOut) *widthOut = c.riverN; return c.riverF; }
		}
		CrashLogger::PerfCount(CrashLogger::PC_RIVER_MISSES);
		float r = o_River(w, edx, x, y, p3);
		float res = r;
		float rwidth = 1.0f;                                              // Flussbreite x (Vanilla 1)
		do {
		HookSet* hs = g_hs;
		if (!g_on || !hs) break;
		Dom d = Dominant((const uint8_t*)w, x, y);
		if (!d.id) break;
		const HookDef& b = hs->d[d.id - 1];
		if (!b.used || !b.enabled) break;
		float pm = PlainAt(b, d, x, y);
		if (!b.terrainOn) { if (pm > 0.0f) res = r + (1.0f - r) * std::min(1.0f, pm * 4.0f); break; }
		float width = b.riverWidth < 0.5f ? 0.5f : b.riverWidth;
		rwidth = Smooth(d.w) > 0.5f ? width : 1.0f;
		float rb = b.riversNative ? r / width : r + 1.0f;               // 1.0 = weit weg von jedem Fluss
		if (b.riversOwn) { float o = OwnRiverDistance(b.riverSpacing, x, y) / width; if (o < rb) rb = o; }
		float t = Smooth(d.w);
		res = r + (rb - r) * t;
		// Keine Fluesse auf Bergzuegen (sonst Treppen-Rinnen quer ueber den Kamm)
		if (b.ridgeHeight > 0.0f) {
			float m = 0.0f; RidgeAt(b, x, y, &m);
			if (m > 0.02f) { float sup = 0.02f + m * 2.0f; if (res < sup) res = res + (sup - res) * (m > 0.1f ? 1.0f : m / 0.1f); }
		}
		// Sternmassiv: fremde Fluesse aus, eigener Fluss am Talgrund (bis in den Talend-See)
		StarSample ss; float sm = 0.0f;
		bool star = StarHere(w, b, d, x, y, 0.0f, &ss, &sm);
		if (star) {
			res = res + (1.0f - res) * std::min(1.0f, sm * 4.0f);
			if (ss.riverBlocks < 1e8f && sm > 0.5f) { float rs = ss.riverBlocks * (0.02f / 6.0f) / width; if (rs < res) res = rs; }
		}
		// Seen ungemischt (sonst haebe die native Entfernung den Seerand ueber 0.02)
		LakeQ lq;
		if (WideShare(WideAt((const uint8_t*)w, x, y), d.id) >= kDeepShare && LakeQuery(w, b, d, x, y, sm, star ? &ss : nullptr, &lq)) {   // wie die Hoehe
			float dn = lq.dn;
			float lr = dn < 1.0f ? dn * 0.015f : 0.015f + (dn - 1.0f) * 0.2f;   // nur das Innere (dn < 1) ist Wasser
			if (lr < res) res = lr;
		}
		// Freie Ebene: kein Wasser (Fluesse enden am aeusseren Rand)
		if (pm > 0.0f) res = res + (1.0f - res) * std::min(1.0f, pm * 4.0f);
		} while (0);
		{
			ColInfo& c = Col(w, x, y);
			c.riverF = res; c.riverN = rwidth; c.has |= 16;              // riverN = Flussbreite der Saeule
		}
		if (widthOut) *widthOut = rwidth;
		return res;
	}
	// Hoehe einer Saeule aus dem Saeulen-Speicher (wie Hook_Height, ohne Zaehler)
	static float HeightAt(void* w, int x, int y) {
		{ ColInfo& c = Col(w, x, y); if (c.has & 8) return c.height; }
		float h = HeightCore(w, nullptr, x, y, 0);
		ColInfo& c = Col(w, x, y); c.height = h; c.has |= 8;
		return h;
	}
	// ---- Wasser am Hang: NUR in der Flussschleife entschieden ---------------------------------
	// Frueher pruefte jeder Aufruf des Flussabstands die Nachbarn (auch die Hoehenfunktion ruft ihn
	// bei jeder Abfrage) -- viel zu teuer. Jetzt nur der eine Aufruf, nach dem die Flussschleife
	// Wasser schreibt (Ruecksprung 0x5E7E8A), und nur wo dort Wasser hinkaeme (r <= 0.02).
	// Die Hoehen und Flussabstaende der Nachbarn liegen zu diesem Zeitpunkt schon im Saeulen-
	// Speicher (die Zone hat ihre Hoehen vorher berechnet) -> praktisch kostenlos.
	//  * steil: mehr als 1.5 Bloecke zum Nachbarn -> kein Wasser (Wasserwand)
	//  * quer am Hang: Steigung quer zum Fluss (Querrichtung = Richtung, in der r waechst) mal
	//    Flussbreite (12 * Breite) groesser als 1.5 Bloecke -> kein Wasser (Wasserflaeche am Berg)
	// Das Flussbett, das die Hoehenfunktion graebt, bleibt dort trocken stehen (Bachbett).
	static bool RiverWaterOk(void* w, int x, int y, float width) {
		float h0 = HeightAt(w, x, y);
		float hxp = HeightAt(w, x + 1, y), hxm = HeightAt(w, x - 1, y), hyp = HeightAt(w, x, y + 1), hym = HeightAt(w, x, y - 1);
		if (!g_riverPatch) {                                               // 5er-Stufen (Patch fehlt): Stufenkante = Damm
			auto S = [](float h) { return (int)(h < 0 ? 0 : h) / 5; };
			int s0 = S(h0);
			return S(hxp) == s0 && S(hxm) == s0 && S(hyp) == s0 && S(hym) == s0;
		}
		float step = std::max(std::max(std::fabs(hxp - h0), std::fabs(h0 - hxm)), std::max(std::fabs(hyp - h0), std::fabs(h0 - hym)));
		if (step > 1.5f) return false;
		if (!kRiverCrossCheck) return true;                                // nur Steilstufen, keine Querpruefung
		float gx = (hxp - hxm) * 0.5f, gy = (hyp - hym) * 0.5f;
		if (gx * gx + gy * gy <= 0.08f * 0.08f) return true;               // flach: immer
		float r0 = RiverCore(w, nullptr, x, y, 0, nullptr);
		float nx = RiverCore(w, nullptr, x + 1, y, 0, nullptr) - r0, ny = RiverCore(w, nullptr, x, y + 1, 0, nullptr) - r0;
		float nl = std::sqrt(nx * nx + ny * ny);
		if (nl < 1e-7f) return true;
		float cross = std::fabs(gx * nx + gy * ny) / nl;
		return cross <= 1.5f / (12.0f * std::max(0.5f, width));
	}
	static float __fastcall Hook_River(void* w, void* edx, int x, int y, uint32_t p3) {
		uint32_t ret = (uint32_t)_ReturnAddress();
		float width = 1.0f;
		float res = RiverCore(w, edx, x, y, p3, &width);
		if (kRiverTweaks && res <= 0.02f && ret == Re(A_RIVER_LOOP_RET) && !RiverWaterOk(w, x, y, width)) return 0.025f;
		return res;
	}
	// Bergfaktor m: geht als (n+1.5)*60*m und ueber den Klippenterm in die Hoehe ein --
	// ohne Daempfung blieben im flachen Biom die nativen Berge stehen.
	static float __fastcall Hook_Mount(void* w, void* edx, int x, int y, uint32_t base) {
		float m = o_Mount(w, edx, x, y, base);
		HookSet* hs = g_hs;
		if (!g_on || !hs || m <= 0.0f) return m;
		// Wie die Hoehe: Anteile nach dem breiten Gewicht (WideRaw) -> native Berge laufen ueber ~400 Bloecke aus.
		Wide wd = WideAt((const uint8_t*)w, x, y);
		if (wd.n == 0) return m;
		Dom d = Dominant((const uint8_t*)w, x, y);
		float f = wd.a0;
		for (int k = 0; k < wd.n; k++) {
			const HookDef& b = hs->d[wd.id[k] - 1];
			float fi = 1.0f;
			if (b.used && b.enabled) {
				bool dom = d.id == wd.id[k];
				Dom dd = d; if (!dom) { dd.id = wd.id[k]; dd.px = wd.px[k]; dd.py = wd.py[k]; }
				dd.w = 1.0f;
				float pm = PlainAt(b, dd, x, y);                              // freie Ebene: Berge/Klippen/Furchen weg
				if (!b.terrainOn) fi = 1.0f - pm;
				else {
					// m steckt in generateBiomeContent auch im Rinnen-Term  8*m*(A+B)  (WorldInfo.cpp:1666-1726):
					// bei kleinem Rest-m blieben 1 Block tiefe Furchen im Raster -> unter 0.1 ganz aus.
					float keep = b.mountains <= 0.1f ? 0.0f : b.mountains;
					StarSample ss; float sm = 0.0f; bool star = false;    // Sternmassiv: keine nativen Berge (Zacken)
					if (b.star.on) {
						if (dom) star = StarHere(w, b, dd, x, y, 0.0f, &ss, &sm);
						else { StarAt(b.star, StarSeed(dd), (double)x - dd.px, (double)y - dd.py, 0.0f, &ss); sm = ss.mask; star = sm > 0.0f; }
					}
					float lk = 1.0f;                                          // im See und am Ufer: Hoehe = Grundhoehe
					LakeQ lq;
					if (dom && wd.a[k] >= kDeepShare && LakeQuery(w, b, d, x, y, sm, star ? &ss : nullptr, &lq) && lq.dn < 1.6f) {
						lk = lq.dn <= 1.0f ? 0.0f : (lq.dn - 1.0f) / 0.6f; lk = lk * lk * (3 - 2 * lk);
					}
					fi = keep * (1.0f - pm) * (1.0f - sm) * lk;
				}
			}
			f += wd.a[k] * fi;
		}
		return m * f;
	}
	// Native Baeume: nur die normale Baumschleife (Ruecksprung 0x5EA7B3) wird ausgeduennt.
	static void __fastcall Hook_Tree(void* w, void* edx, int x, int y, int z, int a4, int a5, int a6, void* zone) {
		HookSet* hs = g_hs;
		if (g_on && hs && (uint32_t)_ReturnAddress() == Re(A_TREE_RET_NATIVE)) {
			Dom d = Dominant((const uint8_t*)w, x, y);
			if (d.id && d.w >= 0.5f) {
				const HookDef& b = hs->d[d.id - 1];
				bool plain = b.used && b.enabled && InPlain(b.plainOn, b.plainRadius, b.plainRim, d, x, y, 8.0f);
				if (plain || (b.used && b.enabled && b.nativeTrees < 100 &&
				    (int)(Hash((uint32_t)x, (uint32_t)y, 0x7EEu) % 100u) >= b.nativeTrees)) {
					InterlockedIncrement(&c_trees);
					return;                                            // dieser Baum entfaellt
				}
			}
		}
		o_Tree(w, edx, x, y, z, a4, a5, a6, zone);
	}

	// ---- 4f. Bergdorf-Filter (MountainVillage) ---------------------------------
	static bool VillageFilterFn(cube::GameController* gc, int zx, int zy) {
		HookSet* hs = g_hs;
		if (!g_on || !hs || !gc) return true;
		Dom d = DominantRaw((const uint8_t*)gc + GC_WORLD, zx * 256 + 128, zy * 256 + 128);
		if (!d.id || d.w < 0.5f) return true;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled) return true;
		if (InPlain(h.plainOn, h.plainRadius, h.plainRim, d, zx * 256 + 128, zy * 256 + 128, 400.0f)) return false;
		return h.bergdorf;
	}

	// =====================================================================
	// 5. Definitionen laden
	// =====================================================================
	static std::string ForcedPath() { return BiomesDir() + "\\_erzwungen.txt"; }
	static void LoadForced() {                    // unter g_cs
		g_forcedList.clear();
		std::ifstream f(ForcedPath()); std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream ss(line); ForcedEntry e; long long sd = 0;
			if (!(ss >> sd >> e.cx >> e.cy)) continue;
			std::getline(ss, e.file);                       // Rest der Zeile (Dateiname darf Leerzeichen haben)
			size_t b = e.file.find_first_not_of(" \t"), z = e.file.find_last_not_of(" \t\r");
			if (b == std::string::npos) continue;
			e.file = e.file.substr(b, z - b + 1); e.seed = (uint32_t)sd; g_forcedList.push_back(e);
		}
	}
	static void SaveForced() {                    // unter g_cs
		std::ofstream f(ForcedPath());
		f << "# Von Hand erzwungene Biomzellen: weltsamen zelle_x zelle_y biom-datei (Biom-Werkstatt > Biom HIER erzwingen)\n";
		for (const ForcedEntry& e : g_forcedList) f << e.seed << " " << e.cx << " " << e.cy << " " << e.file << "\n";
	}

	// Salz je Biom: FNV-1a ueber den kleingeschriebenen Dateinamen. Frueher war es der Platz (0..14) --
	// der haengt von der alphabetischen Reihenfolge ALLER Biomdateien ab: eine neue/umbenannte Datei
	// verschob die Plaetze, und beim naechsten Start lagen die Biome derselben Welt an anderen Orten.
	static uint32_t NameSalt(const std::string& file) {
		uint32_t h = 2166136261u;
		for (char ch : file) { unsigned char c = (unsigned char)ch; if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a'); h = (h ^ c) * 16777619u; }
		return h;
	}
	static void Publish() {                       // unter g_cs
		HookSet* hs = new HookSet; memset(hs, 0, sizeof(HookSet));
		for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) { hs->d[i] = ToHook(g_def[i]); hs->d[i].salt = NameSalt(g_file[i]); }
		for (const ForcedEntry& e : g_forcedList) {
			if (hs->nForced >= kMaxForced) break;
			int id = 0;
			for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i] && _stricmp(g_file[i].c_str(), e.file.c_str()) == 0) id = i + 1;
			if (!id) continue;
			ForcedCell& f = hs->forced[hs->nForced++]; f.seed = e.seed; f.cx = e.cx; f.cy = e.cy; f.id = id;
		}
		g_hs = hs;                                // alter Satz bleibt stehen (Hooks koennten ihn gerade lesen)
		g_defVersion++;
		InterlockedIncrement(&g_modVersion);      // Seed-Rechner (SeedScout) verwirft seine Caches
	}

	void ReloadDefinitions() {
		if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(BiomesDir());
		std::vector<std::string> files = ListBiomeFiles();
		if (files.empty()) {                      // Beispiele anlegen -- AUS, bis man sie einschaltet
			for (int k = 0; k < 3; k++) {
				BiomeDef d = ExampleBiome(k); d.enabled = false; std::string err;
				SaveBiome(BiomesDir() + "\\" + d.name + ".biom", d, err);
			}
			files = ListBiomeFiles();
		}
		{   // native Biome: je Klimaklasse eine Datei (neutrale Vorgaben -- aendert nichts, bis man etwas einstellt)
			bool made = false;
			for (int k = 1; k <= BiomeGen::kNativeClasses; k++) {
				std::string fn = std::string("Nativ_") + BiomeGen::NativeClassName(k) + ".biom";
				bool have = false; for (const std::string& f : files) if (_stricmp(f.c_str(), fn.c_str()) == 0) have = true;
				if (have) continue;
				std::string err; if (SaveBiome(BiomesDir() + "\\" + fn, BiomeGen::NativeDefault(k), err)) made = true;
			}
			if (made) files = ListBiomeFiles();
		}
		Guard g;
		for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) g_def[i].enabled = false;   // entfernte Dateien -> aus
		int loaded = 0;
		for (const std::string& f : files) {
			BiomeDef d; std::string err;
			if (!LoadBiome(BiomesDir() + "\\" + f, d, err)) { std::cout << "[Biom] " << err << std::endl; continue; }
			int slot = -1;
			for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i] && _stricmp(g_file[i].c_str(), f.c_str()) == 0) slot = i;
			if (slot < 0) for (int i = 0; i < kMaxBiomes; i++) if (!g_defUsed[i]) { slot = i; break; }
			if (slot < 0) { std::cout << "[Biom] Mehr als " << kMaxBiomes << " Biome -- '" << f << "' uebersprungen." << std::endl; continue; }
			g_def[slot] = d; g_file[slot] = f; g_defUsed[slot] = true; loaded++;
		}
		LoadForced();
		Publish();
		InterlockedExchange(&g_refilter, 1);                     // bestehende Regionen mit den neuen Schaltern nachfiltern
		int on = 0; for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i] && g_def[i].enabled) on++;
		std::cout << "[Biom] " << loaded << " Biom-Dateien geladen, " << on << " aktiv (Strukturen\\Biome)." << std::endl;
		ReloadZoneModels();                                        // Biom-Zuordnung der Zonen-Modelle auffrischen
	}

	std::vector<BiomeDef> GetDefinitions() {
		std::vector<BiomeDef> v; if (!g_csInit) return v;
		Guard g; for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) v.push_back(g_def[i]);
		return v;
	}
	std::vector<std::string> GetDefinitionFiles() {
		std::vector<std::string> v; if (!g_csInit) return v;
		Guard g; for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) v.push_back(g_file[i]);
		return v;
	}
	void SetEnabled(bool on) { g_on = on; InterlockedExchange(&g_refilter, 1); InterlockedIncrement(&g_modVersion); }
	bool IsEnabled() { return g_on; }
	int DefinitionsVersion() { if (!g_csInit) return 0; Guard g; return g_defVersion; }
	std::vector<std::pair<int, BiomeDef>> GetDefinitionsWithIds() {
		std::vector<std::pair<int, BiomeDef>> v; if (!g_csInit) return v;
		Guard g; for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) v.push_back(std::make_pair(i + 1, g_def[i]));
		return v;
	}

	static bool DefById(int id, BiomeDef& out) {   // id 1..15
		if (id < 1 || id > kMaxBiomes || !g_csInit) return false;
		Guard g; if (!g_defUsed[id - 1]) return false;
		out = g_def[id - 1]; return out.enabled;
	}
	static int DefIdByListIndex(int index) {        // Index in GetDefinitions() -> ID
		if (!g_csInit) return 0;
		Guard g; int k = 0;
		for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i]) { if (k == index) return i + 1; k++; }
		return 0;
	}

	// =====================================================================
	// 6. Zonen schmuecken: Pflanzen + Dungeons (Spielthread)
	// =====================================================================
	namespace {
		CRITICAL_SECTION g_qcs; bool g_qcsInit = false;
		std::deque<std::pair<int, int>> g_queue;          // (zx, zy), Zonen-Thread schreibt
		struct PlantKey { int id, kind, var, ver; bool operator<(const PlantKey& o) const { return std::tie(id, kind, var, ver) < std::tie(o.id, o.kind, o.var, o.ver); } };
		std::map<PlantKey, Lab::Structure> g_plantCache;
		std::map<std::string, Lab::Structure> g_fileCache;
		struct Dungeon { int zx, zy, id; int cx, cy, cz; std::vector<DungeonRoom> rooms; int ox, oy, oz; bool populated; };
		std::vector<Dungeon> g_dungeons;                  // diese Sitzung
		struct Tracked { uint64_t guid; int id; int dungeon; };
		std::vector<Tracked> g_tracked;
		std::map<std::string, MobLab::CustomMob> g_mobCache;
		uint32_t g_lastSeed = 0;
	}

	static uint32_t SeedOf(cube::GameController* gc) {
		__try { return *(uint32_t*)((uint8_t*)gc + GC_WORLD + W_SEED); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	static bool ColumnHeightRaw(void* zone, int x, int y, int* h) {
		// world_getChunkCell256 @0x4347A0: Zone+0xA8 + ((x&255) + (y&255)*256) * 0x20;
		// +0x10 + +0x1C = Oberflaeche, wie in generateBiomeContent (WorldInfo.cpp:2393, :3270).
		__try {
			uint8_t* base = *(uint8_t**)((uint8_t*)zone + 0xA8);
			if (!base) return false;
			uint8_t* c = base + ((x & 255) + (y & 255) * 256) * 0x20;
			*h = *(int*)(c + 0x10) + *(int*)(c + 0x1C);
			return *h > -4096 && *h < 4096;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Oberflaechentypen aus 0x5C56E0 (RE_Biome.md Abschnitt 2): 4 Gras/Erde, 9 Sand, 10 Schnee, 12 Lava-Boden.
	// Alles andere (Holz, Mauern, Fels-Typen) gilt nicht als Pflanz-/Baugrund.
	static bool IsGround(uint8_t t) { t &= 0x1F; return t == 4 || t == 9 || t == 10 || t == 12; }

	// =====================================================================
	// Grosse Hangbaeume -- verankert in der Zonenerzeugung wie die nativen Baeume
	// =====================================================================
	// generateBiomeContent (0x5E4850) ruft kurz vor dem Ende VoxelChunk_propagateSunlight
	// @0x59A0E0 auf (Aufruf 0x5EDE84: thiscall(World, x0, y0, x0+256, y0+256, 0, Zone), ret 0x18).
	// Erst danach traegt es die Zone in die Region ein (Region+0x10018+i*4 = Zone, WorldInfo.cpp
	// Ende der Funktion). Wer an dieser Aufrufstelle baut, baut also wie die nativen Baeume:
	// im Zonen-Thread, in die noch unveroeffentlichte Zone, VOR dem Sonnenlicht -- keine Sperren,
	// kein Nachladen, kein Aufpoppen, kein Eingriff in Karte oder Chunk-Thread.
	static const uint32_t A_SUN = 0x0059A0E0, A_SUN_CALL = 0x005EDE84;
	static const uint8_t kSunCallOrig[5] = { 0xE8, 0x57, 0xC2, 0xFA, 0xFF };
	typedef void (__fastcall* Sun_t)(void* w, void* edx, int x0, int y0, int x1, int y1, int margin, void* zone);
	typedef uint8_t* (__stdcall* ColData_t)(int x, int y, int z, void* zone);   // world_getColumnData @0x42F7E0, ret 0x10
	static const uint32_t A_COLDATA = 0x0042F7E0;
	static bool g_treeAnchor = false;
	static volatile bool g_zoneObjOn = true, g_smallOn = true;   // Schalter (LAB -> Leistung)
	static volatile bool g_splitOn = true;                       // Zonen-Teiler (Schalter "zonen_teiler")
	static volatile LONG c_bigTrees = 0;
	namespace {
		CRITICAL_SECTION g_tcs; bool g_tcsInit = false;
		struct TreeKey { int id, var, ver; bool operator<(const TreeKey& o) const { return std::tie(id, var, ver) < std::tie(o.id, o.var, o.ver); } };
		std::map<TreeKey, Lab::Structure> g_treeCache;               // Knoten bleiben stabil
	}
	static const Lab::Structure* BigTreeFor(int id, uint32_t h) {
		if (!g_tcsInit) return nullptr;
		TreeKey k{ id, (int)(h % 6u), g_defVersion };
		EnterCriticalSection(&g_tcs);
		auto it = g_treeCache.find(k);
		if (it == g_treeCache.end()) {
			BiomeDef def;
			Lab::Structure st;
			if (DefById(id, def)) st = MakeBigTree(def, Hash((uint32_t)id, 100u, (uint32_t)k.var));
			it = g_treeCache.insert(std::make_pair(k, st)).first;
		}
		const Lab::Structure* r = it->second.Valid() ? &it->second : nullptr;
		LeaveCriticalSection(&g_tcs);
		return r;
	}
	// Oberste feste Lage einer Saeule direkt aus der Zone (wie die native Baumschleife,
	// WorldInfo.cpp:3600ff.: ab Saeulenoberkante abwaerts bis Typ != 0). Wasser -> false.
	static bool ZoneSurface(void* zone, int x, int y, int* zOut, uint8_t* tOut) {
		int top;
		if (!ColumnHeightRaw(zone, x, y, &top)) return false;
		__try {
			ColData_t col = (ColData_t)Re(A_COLDATA);
			for (int z = top - 1; z > top - 400; z--) {
				uint8_t* v = col(x, y, z, zone);
				if (!v) return false;
				uint8_t t = v[3] & 0x1F;
				if (t == 0) continue;
				if (t == 2) return false;                                   // Wasser
				*zOut = z; *tOut = t; return true;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return false;
	}
	static uint32_t WorldSeedRaw(const uint8_t* world) {
		__try { return *(const uint32_t*)(world + W_SEED); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	// Gesetzte Objekte einer Zone (Abstandspruefung ueber alle Arten)
	// Baeume/Blumen untereinander: Abstand der Mitten. Ist ein Dungeon beteiligt: Grundflaechen
	// (um den Abstand erweitert) duerfen sich nicht ueberlappen.
	struct Placed { int x, y, half, spacing; int x0, y0, x1, y1; bool box; };
	static bool Conflicts(const std::vector<Placed>& pl, int x, int y, int half, int spacing, bool box = false, int bx0 = 0, int by0 = 0, int bx1 = 0, int by1 = 0) {
		for (const Placed& p : pl) {
			int sp = std::max(spacing, p.spacing);
			if (box || p.box) {
				int ax0 = box ? bx0 : x - half, ay0 = box ? by0 : y - half, ax1 = box ? bx1 : x + half, ay1 = box ? by1 : y + half;
				if (ax0 - sp < p.x1 && ax1 + sp > p.x0 && ay0 - sp < p.y1 && ay1 + sp > p.y0) return true;
				continue;
			}
			float need = (float)std::max(sp, (int)(0.7f * (half + p.half)));
			float dx = (float)(p.x - x), dy = (float)(p.y - y);
			if (dx * dx + dy * dy < need * need) return true;
		}
		return false;
	}
	// Sperrflaechen der Zone: Stadtviertel/Dungeon-/Portalzone, native Orte, Bergdorf-Zonen.
	struct ZoneBlock { bool zoneBlocked = false; std::vector<std::pair<int, int>> sites; };
	static int RecordKindRaw(const uint8_t* world, int zx, int zy) {
		__try {
			const uint8_t* reg = ((const uint8_t* const*)(world + W_REGIONGRID))[(zx >> 6) * 1024 + (zy >> 6)];
			if (!reg) return 0;
			return reg[0x18 + ((zx & 63) * 64 + (zy & 63)) * 0x10];
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	static void ReadZoneBlock(const uint8_t* world, int zx, int zy, ZoneBlock& zb) {
		zb.zoneBlocked = RecordKindRaw(world, zx, zy) != 0;
		cube::GameController* gc = cube::GetGameController();
		for (int a = -1; a <= 1 && gc && !zb.zoneBlocked; a++) for (int b = -1; b <= 1; b++)
			if (MountainVillage::IsVillageSiteZone(gc, zx + a, zy + b)) { zb.zoneBlocked = true; break; }
		int rx = zx >> 6, ry = zy >> 6;
		for (int i = 0; i < 64; i++) {
			SiteRaw sr;
			if (!ReadSiteRaw((uint8_t*)world, rx, ry, i, &sr)) break;
			if (sr.kind <= 0 || sr.kind > 15) continue;
			if (std::abs(sr.x - (zx * 256 + 128)) < 128 + 120 && std::abs(sr.y - (zy * 256 + 128)) < 128 + 120) zb.sites.push_back(std::make_pair(sr.x, sr.y));
		}
	}
	static bool NearSite(const ZoneBlock& zb, int x, int y, int half) {
		for (const auto& p : zb.sites) { int dx = p.first - x, dy = p.second - y, r = 90 + half; if (dx * dx + dy * dy < r * r) return true; }
		return false;
	}
	static void ZoneBigTrees(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk) {
		HookSet* hs = g_hs;
		if (!g_on || !hs || !zone) return;
		uint32_t seed = WorldSeedRaw(world);
		int x0 = zx * 256, y0 = zy * 256;
		// schnell: kommt in der Zone ueberhaupt ein Biom mit Hangbaeumen vor?
		const int pts[5][2] = { { 128, 128 }, { 16, 16 }, { 240, 16 }, { 16, 240 }, { 240, 240 } };
		bool any = false;
		for (int i = 0; i < 5 && !any; i++) { Dom d = DominantRaw(world, x0 + pts[i][0], y0 + pts[i][1]); if (d.id && hs->d[d.id - 1].used && hs->d[d.id - 1].enabled && hs->d[d.id - 1].slopeTrees > 0) any = true; }
		if (!any) return;
		if (blk.zoneBlocked) return;
		for (int ti = 0; ti < 200; ti++) {
			uint32_t h = Hash((uint32_t)zx, (uint32_t)zy, (uint32_t)ti, seed ^ 0x7B16u);
			int x = x0 + 14 + (int)(h % 228u), y = y0 + 14 + (int)((h >> 8) % 228u);
			Dom d = DominantRaw(world, x, y);
			if (!d.id || d.w < 0.6f) continue;
			const HookDef& b = hs->d[d.id - 1];
			if (!b.used || !b.enabled || ti >= b.slopeTrees) continue;
			if (InPlain(b.plainOn, b.plainRadius, b.plainRim, d, x, y, 12.0f)) continue;
			if (Conflicts(pl, x, y, 10, 14) || NearSite(blk, x, y, 10)) continue;
			int zc, ze, zw, zn, zs; uint8_t t;
			if (!ZoneSurface(zone, x, y, &zc, &t) || !IsGround(t)) continue;
			if (!ZoneSurface(zone, x + 3, y, &ze, &t) || !ZoneSurface(zone, x - 3, y, &zw, &t) ||
			    !ZoneSurface(zone, x, y + 3, &zn, &t) || !ZoneSurface(zone, x, y - 3, &zs, &t)) continue;
			float slope = (float)std::max(std::abs(ze - zw), std::abs(zn - zs)) / 6.0f;
			if (slope < 0.2f || slope > 1.4f) continue;
			if (b.snowLine > 0.0f && zc > b.snowLine - 35.0f) continue;
			const Lab::Structure* st = BigTreeFor(d.id, Hash(h, 2));
			if (!st) continue;
			int ox = x - st->sx / 2, oy = y - st->sy / 2;
			if (ox < x0 || oy < y0 || ox + st->sx > x0 + 256 || oy + st->sy > y0 + 256) continue;
			// Stammfuss auf den tiefsten Bodenblock unter dem Stamm (am Hang sonst schwebend)
			int zb = zc;
			for (int a = -1; a <= 1; a += 2) for (int c = -1; c <= 1; c += 2) { int zz; uint8_t tt; if (ZoneSurface(zone, x + a, y + c, &zz, &tt)) zb = std::min(zb, zz); }
			if (LabBridge::StampIntoZone(*st, ox, oy, zb, 0, 0, zone) > 0) { { int hf = std::max(st->sx, st->sy) / 2; pl.push_back(Placed{ x, y, hf, 14, x - hf, y - hf, x + hf, y + hf, false }); } InterlockedIncrement(&c_bigTrees); }
		}
	}
	// ---- Zonen-Modelle (LAB -> Zonen-Modelle) ----------------------------------------------
	// ---- Kleine Pflanzen wie die nativen (Schilf, Seerosen, Unterwasserpflanzen) -------------------
	// Das Spiel zeichnet kleine Pflanzen nicht als Bloecke, sondern als verkleinertes Modell:
	//  * Tabelle der Deko-Pflanzen: std::vector<Sprite*> bei GameController+0x800718, 64 Plaetze
	//    (GameController-ctor 0x462545..0x462BE0: Platz 0 flowers2.cub, 1 flowers.cub, 2 grass.cub,
	//    5 underwater-plant, 0x16 reed, 0x1F/0x20 water-lily ...). Frei: 0x12, 0x3B..0x3F.
	//  * Je Pflanze ein Eintrag (0x40 Byte) in der Liste Zone+4 (std::list, Knoten 0x48):
	//      +0x00 int Platz   +0x08/+0x10/+0x18 int64 x/y/z (1 Block = 0x10000)   +0x20 float Groesse
	//      +0x24 float Drehung (Grad)   +0x28 float Licht (setzt generateBiomeContent am Ende fuer
	//      alle Eintraege)   +0x2C..+0x34 Farbton (1,1,1)   +0x38 Flags (4 = im Wind wiegen, 1 = unbeleuchtet)
	//    Anlegen: Vorgaben 0x58E9F0 (thiscall), Anhaengen 0x4862E0 (thiscall(list*, const rec*), ret 4).
	//    Beispiel Schilf: 0x5EC0B7 Groesse 0.09, Platz 0x16, Flags 4.
	//  * Zeichnen: 0x4BD160 nimmt Tabelle[Platz] und zeichnet das Modell mit Groesse/Drehung/Licht.
	//    Sprite::draw 0x4E6DF0 braucht fertige Puffer (+0x34/+0x38); Sprite::build_mesh 0x4E7870
	//    baut sie aus +0x30 (RGB), +0x44..+0x4C (Groesse), +0x55, Geraet +0x58.
	static const uint32_t A_VEG_TABLE = 0x800718;                    // GameController-Offset
	static const uint32_t A_SPRITE_BUILD = 0x4E7870, A_VEGREC_INIT = 0x58E9F0, A_VEGLIST_PUSH = 0x4862E0;
	static const uint32_t GC_D3DDEV = 0x134;                          // GameController+0x134 = IDirect3DDevice9*
	static const int kVegSlots[6] = { 0x12, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F };
	namespace {
		struct VegSlot { std::string key; Lab::Structure s; uint8_t* sprite = nullptr; bool pending = false; bool failed = false; std::string note; };
		VegSlot g_veg[6];
		CRITICAL_SECTION g_vcs; bool g_vcsInit = false;
	}
	typedef void* (__fastcall* VegInit_t)(void* rec, void* edx);
	typedef void  (__fastcall* VegPush_t)(void* list, void* edx, const void* rec);
	typedef void  (__fastcall* SpriteBuild_t)(void* sprite, void* edx);
	static bool PushVegetationRaw(void* zone, int slot, int x, int y, int z, float scale, float rotDeg, bool wind, float light) {
		__try {
			alignas(8) uint8_t rec[0x40]; memset(rec, 0, sizeof(rec));
			((VegInit_t)Re(A_VEGREC_INIT))(rec, nullptr);
			*(int*)(rec + 0x00) = slot;
			*(int64_t*)(rec + 0x08) = ((int64_t)x << 16) + 0x8000;      // Blockmitte (wie das Spiel: +0.5)
			*(int64_t*)(rec + 0x10) = ((int64_t)y << 16) + 0x8000;
			*(int64_t*)(rec + 0x18) = (int64_t)z << 16;
			*(float*)(rec + 0x20) = scale;
			*(float*)(rec + 0x24) = rotDeg;
			*(float*)(rec + 0x28) = light;
			if (wind) *(int*)(rec + 0x38) |= 4;
			((VegPush_t)Re(A_VEGLIST_PUSH))((uint8_t*)zone + 4, nullptr, rec);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Platz fuer ein Modell vergeben (LAB-/Spielthread). key = Datei + Modell + Groesse.
	static int VegSlotFor(const std::string& key, const Lab::Structure& s, std::string& note) {
		if (!g_vcsInit) { InitializeCriticalSection(&g_vcs); g_vcsInit = true; }
		EnterCriticalSection(&g_vcs);
		int idx = -1;
		for (int i = 0; i < 6 && idx < 0; i++) if (g_veg[i].key == key) idx = i;
		if (idx < 0) for (int i = 0; i < 6 && idx < 0; i++) if (g_veg[i].key.empty()) idx = i;
		if (idx >= 0 && g_veg[idx].key != key) {
			g_veg[idx].key = key; g_veg[idx].s = s; g_veg[idx].pending = true; g_veg[idx].failed = false; g_veg[idx].note.clear();
		}
		if (idx < 0) note = "keine freie Pflanzen-Nummer (hoechstens 6 kleine Modelle)";
		LeaveCriticalSection(&g_vcs);
		return idx < 0 ? -1 : kVegSlots[idx];
	}
	static void VegReleaseUnused(const std::vector<std::string>& used) {
		if (!g_vcsInit) return;
		EnterCriticalSection(&g_vcs);
		for (VegSlot& v : g_veg) {
			bool u = false; for (const std::string& k : used) if (k == v.key) u = true;
			if (!u) { v.key.clear(); v.pending = false; }                 // Sprite bleibt stehen (wird evtl. noch gezeichnet)
		}
		LeaveCriticalSection(&g_vcs);
	}
	static bool BuildSpriteRaw(uint8_t* gc, uint8_t* spr, const uint8_t* tmpl, uint8_t* rgb, int sx, int sy, int sz, int slot) {
		__try {
			memcpy(spr, tmpl, 0x60);                                       // Vorlage: flowers.cub
			*(uint8_t**)(spr + 0x30) = rgb;
			*(uint32_t*)(spr + 0x34) = 0; *(uint32_t*)(spr + 0x38) = 0;
			*(uint32_t*)(spr + 0x3C) = 0; *(uint32_t*)(spr + 0x40) = 0;
			*(int*)(spr + 0x44) = sx; *(int*)(spr + 0x48) = sy; *(int*)(spr + 0x4C) = sz;
			*(uint32_t*)(spr + 0x50) = 0;
			spr[0x54] = 0; spr[0x56] = 0;                                  // nicht "faul": Puffer bleiben
			void* dev = *(void**)(gc + GC_D3DDEV);
			if (!dev) return false;
			*(void**)(spr + 0x58) = dev;
			((SpriteBuild_t)Re(A_SPRITE_BUILD))(spr, nullptr);
			if (!*(uint32_t*)(spr + 0x34) || !*(uint32_t*)(spr + 0x38)) return false;
			if (slot < 0) return true;                                     // Gegenstandsmodell: nicht in die Pflanzentabelle
			uint8_t** table = *(uint8_t***)(gc + A_VEG_TABLE);
			uint8_t** tend  = *(uint8_t***)(gc + A_VEG_TABLE + 4);
			if (!table || tend - table <= slot) return false;
			table[slot] = spr;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static const uint8_t* VegTemplateRaw(uint8_t* gc) {
		__try {
			uint8_t** table = *(uint8_t***)(gc + A_VEG_TABLE);
			return table ? table[1] : nullptr;                            // flowers.cub
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static bool VegSlotFreeRaw(uint8_t* gc, int slot) {
		__try { uint8_t** table = *(uint8_t***)(gc + A_VEG_TABLE); return table && table[slot] == nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void ReadEmptyColorRaw(uint8_t* e) {
		__try { memcpy(e, (const void*)Re(0x76B340), 3); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // Leerfarbe DAT_0076b340
	}
	static std::string VegNote(int slot) {
		if (!g_vcsInit) return "";
		std::string r;
		EnterCriticalSection(&g_vcs);
		for (int i = 0; i < 6; i++) if (kVegSlots[i] == slot) r = g_veg[i].pending ? "wartet auf das Spiel" : g_veg[i].note;
		LeaveCriticalSection(&g_vcs);
		return r;
	}
	// ---- Eigene aufhebbare Gegenstaende (wie Herzblume, Pilz, Kuerbis) -------------------------
	// Das Spiel legt solche Pflanzen als Gegenstand am Boden ab: Eintrag 0x148 Byte in Zone+0x30
	// (std::vector; Vorgaben 0x44A970 thiscall, Anhaengen 0x44D590 thiscall(vec*, rec*) ret 4):
	//   +0x000 Gegenstand (0x118): +0 Typ 0x0B (Material), +1 Unterart (0x0F Pilz, 0x10 Kuerbis,
	//          0x11 Ananas, 0x16 Herzblume, 0x18 Frost-Herzblume, 0x19 Seelenblume ...)
	//   +0x118/+0x120/+0x128 int64 x/y/z   +0x130 Drehung (Grad)   +0x134 Groesse (Herzblume 0.1)
	//   +0x138 Byte 2
	// Belegt: Wildpflanzen generateBiomeContent 0x5ECB75..0x5ED176, Kuerbis-/Ananasfelder
	// Chunk_generateObjects 0x5B1DB3..0x5B1EAD. Modell eines Gegenstands: item_icon_id @0x4EC400
	// (thiscall(this, item), ret 4, liefert Sprite*), Name: buildObjectName @0x598A50
	// (thiscall(this, out wstring*, item), ret 8, liefert out). Eigene Gegenstaende bekommen die
	// Unterarten 0x30..0x4F des Typs 0x0B; beide Funktionen werden fuer genau diese umgeleitet.
	static const uint32_t A_ITEM_ICON = 0x4EC400, A_OBJ_NAME = 0x598A50, A_WSTR_ASSIGN = 0x40EB60;
	static const uint32_t A_DROP_RESET = 0x44A970, A_DROP_PUSH = 0x44D590;
	namespace {
		struct ItemSlot { std::string key; Lab::Structure s; uint8_t* sprite = nullptr; bool pending = false, failed = false; wchar_t name[64] = { 0 }; std::string note; bool used = false; };
		ItemSlot g_items[kPickupCount];
	}
	typedef void* (__fastcall* ItemIcon_t)(void* self, void* edx, const uint8_t* item);
	typedef void* (__fastcall* ObjName_t)(void* self, void* edx, void* out, const uint8_t* item);
	typedef void* (__fastcall* WAssign_t)(void* str, void* edx, const wchar_t* s);
	typedef void* (__fastcall* DropReset_t)(void* rec, void* edx);
	typedef void  (__fastcall* DropPush_t)(void* vec, void* edx, const void* rec);
	static ItemIcon_t o_ItemIcon = nullptr; static ObjName_t o_ObjName = nullptr;
	static int OwnItemIndex(const uint8_t* item) {
		if (!item || item[0] != 0x0B) return -1;
		int i = (int)item[1] - kPickupFirstId;
		return (i >= 0 && i < kPickupCount) ? i : -1;
	}
	// Eigene Modelle fuer Rezept-Ergebnisse (LAB -> Rezepte -> "Modell"): Kennung im Modifier (+4) und
	// passender Typ/Unterart -> eigenes Sprite. item_icon_id liefert das Modell fuer Inventar, Hand,
	// Ausruestung und Boden (36 Aufrufer, GameController.cpp) -- alle sehen dasselbe Modell.
	struct RecSpr { int id, type, sub; uint8_t* sprite; };
	static std::vector<RecSpr>* volatile g_recSpr = nullptr;      // alte Saetze werden nie freigegeben
	static long g_recSprVer = -1;
	static std::vector<RecSpr>* volatile g_itemSpr = nullptr;     // eigene Gegenstaende (Items.cpp)
	static long g_itemSprVer = -1;
	static void* RecipeSprite(const uint8_t* item) {
		int id;
		if (Items::IdOf(item, &id)) {
			std::vector<RecSpr>* v = g_itemSpr;
			if (v) for (const RecSpr& e : *v) if (e.id == id && e.type == item[0] && e.sub == item[1]) return e.sprite;
			return nullptr;
		}
		if (!Recipes::TagOf(item, &id)) return nullptr;
		std::vector<RecSpr>* v = g_recSpr;
		if (v) for (const RecSpr& e : *v) if (e.id == id && e.type == item[0] && e.sub == item[1]) return e.sprite;
		return nullptr;
	}
	static void* __fastcall Hook_ItemIcon(void* self, void* edx, const uint8_t* item) {
		if (void* rs = RecipeSprite(item)) return rs;
		int i = OwnItemIndex(item);
		if (i >= 0) {
			if (g_items[i].sprite) return g_items[i].sprite;
			// Modell (noch) nicht da -- z. B. Datei geloescht: nie 0 liefern (das Spiel prueft das nicht),
			// sondern das Herzblumen-Modell zeigen.
			uint8_t tmp[0x118]; memcpy(tmp, item, sizeof(tmp)); tmp[1] = 0x16;
			return o_ItemIcon(self, edx, tmp);
		}
		return o_ItemIcon(self, edx, item);
	}
	static void* __fastcall Hook_ObjName(void* self, void* edx, void* out, const uint8_t* item) {
		std::wstring fxName;                                                  // eigene Gegenstaende, Effekt-Traenke (Rezepte)
		if (Items::Name(item, &fxName) || Recipes::ItemName(item, &fxName)) { ((WAssign_t)Re(A_WSTR_ASSIGN))(out, nullptr, fxName.c_str()); return out; }
		int i = OwnItemIndex(item);
		if (i >= 0) {
			const wchar_t* n = (g_items[i].used && g_items[i].name[0]) ? g_items[i].name : L"Eigene Pflanze";
			((WAssign_t)Re(A_WSTR_ASSIGN))(out, nullptr, n);             // baut den wstring wie das Spiel
			return out;
		}
		return o_ObjName(self, edx, out, item);
	}
	static bool PushItemDropRaw(void* zone, int subtype, int x, int y, int z, float scale, float rotDeg) {
		__try {
			alignas(8) uint8_t rec[0x148]; memset(rec, 0, sizeof(rec));
			((DropReset_t)Re(A_DROP_RESET))(rec, nullptr);
			rec[0] = 0x0B; rec[1] = (uint8_t)subtype;
			*(int64_t*)(rec + 0x118) = ((int64_t)x << 16) + 0x8000;
			*(int64_t*)(rec + 0x120) = ((int64_t)y << 16) + 0x8000;
			*(int64_t*)(rec + 0x128) = (int64_t)z << 16;
			*(float*)(rec + 0x130) = rotDeg;
			*(float*)(rec + 0x134) = scale;
			rec[0x138] = 2;
			((DropPush_t)Re(A_DROP_PUSH))((uint8_t*)zone + 0x30, nullptr, rec);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Gegenstand anmelden (LAB-/Spielthread): Name sofort, Modell im naechsten Bild.
	static void ItemRegister(int id, const std::string& key, const Lab::Structure& s, const std::string& name) {
		if (!g_vcsInit) { InitializeCriticalSection(&g_vcs); g_vcsInit = true; }
		int i = id - kPickupFirstId; if (i < 0 || i >= kPickupCount) return;
		EnterCriticalSection(&g_vcs);
		ItemSlot& it = g_items[i];
		wchar_t w[64] = { 0 }; MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, w, 63);
		memcpy(it.name, w, sizeof(w));
		if (it.key != key) { it.key = key; it.s = s; it.pending = true; it.failed = false; it.note.clear(); }
		it.used = true;
		LeaveCriticalSection(&g_vcs);
	}
	static std::string ItemNote(int id) {
		int i = id - kPickupFirstId; if (!g_vcsInit || i < 0 || i >= kPickupCount) return "";
		EnterCriticalSection(&g_vcs);
		std::string r = g_items[i].pending ? "wartet auf das Spiel" : g_items[i].note;
		LeaveCriticalSection(&g_vcs);
		return r;
	}
	static void BuildPendingItemSprites(uint8_t* gc) {
		for (int i = 0; i < kPickupCount; i++) {
			ItemSlot& v = g_items[i];
			if (!v.pending || !v.s.Valid()) continue;
			v.pending = false;
			const uint8_t* tmpl = VegTemplateRaw(gc);
			if (!tmpl) { v.pending = true; continue; }
			uint8_t e[3] = { 0, 0, 0 }; ReadEmptyColorRaw(e);
			uint8_t* rgb = new uint8_t[v.s.rgb.size()];
			memcpy(rgb, v.s.rgb.data(), v.s.rgb.size());
			if (e[0] | e[1] | e[2])
				for (size_t k = 0; k + 2 < v.s.rgb.size(); k += 3) if ((rgb[k] | rgb[k + 1] | rgb[k + 2]) == 0) { rgb[k] = e[0]; rgb[k + 1] = e[1]; rgb[k + 2] = e[2]; }
			uint8_t* spr = new uint8_t[0x60];                             // nie freigeben
			if (BuildSpriteRaw(gc, spr, tmpl, rgb, v.s.sx, v.s.sy, v.s.sz, -1)) {
				v.sprite = spr; v.note = "bereit";
				std::cout << "[Zonen-Modelle] Gegenstand 0x0B/0x" << std::hex << (kPickupFirstId + i) << std::dec << " (" << v.s.sx << "x" << v.s.sy << "x" << v.s.sz << ") angemeldet." << std::endl;
			} else { v.failed = true; v.note = "Modell konnte nicht gebaut werden"; }
		}
	}
	static void BuildRecipeSprites(uint8_t* gc) {
		long ver = Recipes::ModelVersion();
		if (ver == g_recSprVer) return;
		const uint8_t* tmpl = VegTemplateRaw(gc);
		if (!tmpl) return;                                                    // Spiel noch nicht bereit
		g_recSprVer = ver;
		std::vector<Recipes::ModelEntry> ms; Recipes::Models(ms);
		std::vector<RecSpr>* nv = new std::vector<RecSpr>();
		uint8_t e[3] = { 0, 0, 0 }; ReadEmptyColorRaw(e);
		for (const Recipes::ModelEntry& m : ms) {
			uint8_t* rgb = new uint8_t[m.s.rgb.size()];                       // nie freigeben (Sprite zeigt darauf)
			memcpy(rgb, m.s.rgb.data(), m.s.rgb.size());
			if (e[0] | e[1] | e[2])
				for (size_t k = 0; k + 2 < m.s.rgb.size(); k += 3) if ((rgb[k] | rgb[k + 1] | rgb[k + 2]) == 0) { rgb[k] = e[0]; rgb[k + 1] = e[1]; rgb[k + 2] = e[2]; }
			uint8_t* spr = new uint8_t[0x60];
			if (BuildSpriteRaw(gc, spr, tmpl, rgb, m.s.sx, m.s.sy, m.s.sz, -1)) {
				nv->push_back(RecSpr{ m.id, m.type, m.sub, spr });
				std::cout << "[Rezepte] Eigenes Modell " << m.file << " (" << m.s.sx << "x" << m.s.sy << "x" << m.s.sz << ") fuer Kennung " << m.id << " bereit." << std::endl;
			} else std::cout << "[Rezepte] Eigenes Modell " << m.file << " konnte nicht gebaut werden." << std::endl;
		}
		InterlockedExchangePointer((PVOID volatile*)&g_recSpr, nv);
	}
	static void BuildItemSprites(uint8_t* gc) {
		long ver = Items::ModelVersion();
		if (ver == g_itemSprVer) return;
		const uint8_t* tmpl = VegTemplateRaw(gc);
		if (!tmpl) return;
		g_itemSprVer = ver;
		std::vector<Items::ModelEntry> ms; Items::Models(ms);
		std::vector<RecSpr>* nv = new std::vector<RecSpr>();
		uint8_t e[3] = { 0, 0, 0 }; ReadEmptyColorRaw(e);
		for (const Items::ModelEntry& m : ms) {
			uint8_t* rgb = new uint8_t[m.s.rgb.size()];
			memcpy(rgb, m.s.rgb.data(), m.s.rgb.size());
			if (e[0] | e[1] | e[2])
				for (size_t k = 0; k + 2 < m.s.rgb.size(); k += 3) if ((rgb[k] | rgb[k + 1] | rgb[k + 2]) == 0) { rgb[k] = e[0]; rgb[k + 1] = e[1]; rgb[k + 2] = e[2]; }
			uint8_t* spr = new uint8_t[0x60];
			if (BuildSpriteRaw(gc, spr, tmpl, rgb, m.s.sx, m.s.sy, m.s.sz, -1)) {
				nv->push_back(RecSpr{ m.id, m.type, m.sub, spr });
				std::cout << "[Gegenstaende] Modell " << m.file << " (" << m.s.sx << "x" << m.s.sy << "x" << m.s.sz << ") fuer Kennung " << m.id << " bereit." << std::endl;
			} else std::cout << "[Gegenstaende] Modell " << m.file << " konnte nicht gebaut werden." << std::endl;
		}
		InterlockedExchangePointer((PVOID volatile*)&g_itemSpr, nv);
	}
	// Spielthread (EndScene): ausstehende Modelle bauen und in die Tabelle haengen.
	static void BuildPendingSprites(cube::GameController* gcp) {
		if (gcp) { BuildRecipeSprites((uint8_t*)gcp); BuildItemSprites((uint8_t*)gcp); }
		if (!g_vcsInit || !gcp) return;
		uint8_t* gc = (uint8_t*)gcp;
		EnterCriticalSection(&g_vcs);
		BuildPendingItemSprites(gc);
		if (!g_smallOn) { LeaveCriticalSection(&g_vcs); return; }
		for (int i = 0; i < 6; i++) {
			VegSlot& v = g_veg[i];
			if (!v.pending || !v.s.Valid()) continue;
			v.pending = false;
			int slot = kVegSlots[i];
			if (!v.sprite && !VegSlotFreeRaw(gc, slot)) { v.failed = true; v.note = "Tabellenplatz belegt"; continue; }
			const uint8_t* tmpl = VegTemplateRaw(gc);
			if (!tmpl) { v.pending = true; continue; }                     // Spiel noch nicht bereit
			// RGB im nativen Layout; Luecken in der Leerfarbe des Spiels
			uint8_t e[3] = { 0, 0, 0 };
			ReadEmptyColorRaw(e);
			uint8_t* rgb = new uint8_t[v.s.rgb.size()];
			memcpy(rgb, v.s.rgb.data(), v.s.rgb.size());
			if (e[0] | e[1] | e[2])
				for (size_t k = 0; k + 2 < v.s.rgb.size(); k += 3) if ((rgb[k] | rgb[k + 1] | rgb[k + 2]) == 0) { rgb[k] = e[0]; rgb[k + 1] = e[1]; rgb[k + 2] = e[2]; }
			uint8_t* spr = new uint8_t[0x60];                             // nie freigeben: wird evtl. noch gezeichnet
			if (BuildSpriteRaw(gc, spr, tmpl, rgb, v.s.sx, v.s.sy, v.s.sz, slot)) {
				v.sprite = spr; v.failed = false; v.note = "bereit";
				std::cout << "[Zonen-Modelle] Kleines Modell " << v.s.sx << "x" << v.s.sy << "x" << v.s.sz << " als Pflanze Nr. 0x" << std::hex << slot << std::dec << " angemeldet." << std::endl;
			} else { v.failed = true; v.note = "Modell konnte nicht gebaut werden"; std::cout << "[Zonen-Modelle] Kleines Modell: Bau fehlgeschlagen (Platz 0x" << std::hex << slot << std::dec << ")." << std::endl; }
		}
		LeaveCriticalSection(&g_vcs);
	}

	struct ZModelRT {
		ZoneModelDef def; std::string file; Lab::Structure s;
		int vegSlot = -1;                // >= 0: kleine Pflanze (Tabellenplatz), sonst Bloecke
		int biomeId = 0;                 // 0 ueberall, -1 nur Vanilla, >0 Biom-ID, -2 Biom nicht gefunden
		uint32_t biomeOn = 0, biomeOff = 0;   // Bit (1 << Biom-ID): Biom hakt das Modell an / ab (LAB-Liste, ueberstimmt den Bereich)
		uint32_t salt = 0;
		volatile LONG placed = 0;
		std::string note;
	};
	static std::vector<ZModelRT*>* volatile g_zm = nullptr;     // alte Saetze werden nie freigegeben
	static bool ZoneModelAreaOk(const ZModelRT& m, const Dom& d) {
		if (d.id && d.w >= 0.6f) {
			if (m.biomeOff & (1u << d.id)) return false;
			if (m.biomeOn & (1u << d.id)) return true;
		}
		if (m.biomeId == -2) return false;
		if (m.biomeId == -1) return !d.id || d.w < 0.4f || IsNativeBiome(d.id);   // bearbeitete native Biome zaehlen als Vanilla
		if (m.biomeId > 0) return d.id == m.biomeId && d.w >= 0.6f;
		return true;
	}
	static void ZoneModelsPlace(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk) {
		std::vector<ZModelRT*>* set = g_zm;
		HookSet* hs = g_hs;
		if (!g_on || !set || set->empty() || !zone || blk.zoneBlocked) return;
		uint32_t seed = WorldSeedRaw(world);
		int x0 = zx * 256, y0 = zy * 256;
		// Kleine Pflanzen zeichnet das Spiel einzeln (je Eintrag ein Zeichenaufruf, 0x4BD160) ->
		// je Zone hoechstens 40 je Modell und 80 insgesamt, sonst bricht die Bildrate ein.
		int smallTotal = 0;
		// Reihenfolge: Dungeons (brauchen Platz), Baeume, Blumen
		for (int kind : { (int)ZM_DUNGEON, (int)ZM_TREE, (int)ZM_FLOWER, (int)ZM_PICKUP }) for (ZModelRT* m : *set) {
			const ZoneModelDef& d = m->def;
			if (!d.enabled || d.kind != kind || !m->s.Valid() || d.density <= 0) continue;
			int tries = d.density;
			if (kind == ZM_DUNGEON) {
				if (Hash((uint32_t)zx, (uint32_t)zy, seed, m->salt ^ 0xD0u) % 100u >= (uint32_t)d.density) continue;
				tries = 6;                                         // bis zu 6 Stellen in der Zone probieren, 1 Dungeon
			}
			int done = 0;
			for (int ti = 0; ti < tries; ti++) {
				if (kind == ZM_DUNGEON && done) break;
				uint32_t h = Hash((uint32_t)zx, (uint32_t)zy, (uint32_t)ti, seed ^ m->salt);
				int rot = d.rotate ? (int)(Hash(h, 5) & 3) : 0;
				int W = (rot & 1) ? m->s.sy : m->s.sx, D = (rot & 1) ? m->s.sx : m->s.sy;
				const bool isSmall = (kind == ZM_FLOWER && m->vegSlot >= 0) || kind == ZM_PICKUP;
				if (isSmall && kind == ZM_FLOWER && !g_smallOn) break;     // Schalter: kleine Blumen aus
				if (kind == ZM_PICKUP && (d.itemId < kPickupFirstId || d.itemId >= kPickupFirstId + kPickupCount)) break;
				if (isSmall) { W = 1; D = 1; }
				if (isSmall && (done >= 40 || smallTotal >= 80)) break;
				if (W > 250 || D > 250) break;
				int ox = x0 + 2 + (int)(h % (uint32_t)std::max(1, 252 - W)), oy = y0 + 2 + (int)((h >> 12) % (uint32_t)std::max(1, 252 - D));
				// Zonen-Teiler (2026-10-13): Mitte irgendwo in der Zone, das Modell darf ueber die Grenze reichen --
				// ZoneSplit setzt die Stuecke in den Nachbarzonen mit (sonst haeuften sich Modelle nie an Zonengrenzen).
				const bool split = g_splitOn && !isSmall && kind != ZM_DUNGEON && kind != ZM_PICKUP;
				if (split) { ox = x0 + 4 + (int)(h % 248u) - W / 2; oy = y0 + 4 + (int)((h >> 12) % 248u) - D / 2; }
				int cx = ox + W / 2, cy = oy + D / 2, half = std::max(W, D) / 2;
				Dom dm = DominantRaw(world, cx, cy);
				if (!ZoneModelAreaOk(*m, dm)) continue;
				if (dm.id && hs) { const HookDef& b = hs->d[dm.id - 1]; if (b.used && b.enabled && InPlain(b.plainOn, b.plainRadius, b.plainRim, dm, cx, cy, (float)half)) continue; }
				bool isBox = kind == ZM_DUNGEON;
				if (Conflicts(pl, cx, cy, half, d.spacing, isBox, ox, oy, ox + W, oy + D) || NearSite(blk, cx, cy, half)) continue;
				int zc; uint8_t t;
				if (!ZoneSurface(zone, cx, cy, &zc, &t)) continue;
				if (d.ground ? (t & 0x1F) != d.ground : !IsGround(t)) continue;
				if (zc < d.hMin || zc > d.hMax) continue;
				int ze, zw, zn, zs; uint8_t tt;
				if (!ZoneSurface(zone, cx + 3, cy, &ze, &tt) || !ZoneSurface(zone, cx - 3, cy, &zw, &tt) ||
				    !ZoneSurface(zone, cx, cy + 3, &zn, &tt) || !ZoneSurface(zone, cx, cy - 3, &zs, &tt)) continue;
				float slope = (float)std::max(std::abs(ze - zw), std::abs(zn - zs)) / 6.0f;
				if (slope < d.slopeMin || slope > d.slopeMax) continue;
				if (kind == ZM_PICKUP) {                                   // wie Herzblume: Gegenstand am Boden (Zone+0x30)
					float sc = d.smallScale > 0.0f ? d.smallScale : 0.1f;
					float rd = d.rotate ? (float)(Hash(h, 8) % 360u) : 0.0f;
					if (PushItemDropRaw(zone, d.itemId, cx, cy, zc + 1 + d.zOffset, sc, rd)) {
						pl.push_back(Placed{ cx, cy, 0, d.spacing, cx, cy, cx + 1, cy + 1, false });
						InterlockedIncrement(&m->placed); done++; smallTotal++;
					}
					continue;
				}
				if (isSmall) {                                             // wie Schilf: Eintrag in die Pflanzenliste der Zone
					float sc = d.smallScale * (0.85f + 0.3f * (float)(Hash(h, 7) & 0xFF) / 255.0f);
					float rd = d.rotate ? (float)(Hash(h, 8) % 360u) : 0.0f;
					if (PushVegetationRaw(zone, m->vegSlot, cx, cy, zc + 1 + d.zOffset, sc, rd, d.wind, 0.0f)) {
						pl.push_back(Placed{ cx, cy, 0, d.spacing, cx, cy, cx + 1, cy + 1, false });
						InterlockedIncrement(&m->placed); done++; smallTotal++;
					}
					continue;
				}
				int z;
				if (kind == ZM_TREE) {
					int zb = zc;                                           // Stammfuss auf den tiefsten Bodenblock
					for (int a = -1; a <= 1; a += 2) for (int c = -1; c <= 1; c += 2) { int zz; if (ZoneSurface(zone, cx + a, cy + c, &zz, &tt)) zb = std::min(zb, zz); }
					z = zb + d.zOffset;
				} else if (kind == ZM_FLOWER) {
					z = zc + 1 + d.zOffset;
				} else {
					int zmin = INT_MAX, zmax = INT_MIN; bool ok = true;
					for (int yy = 0; yy <= D && ok; yy += 4) for (int xx = 0; xx <= W; xx += 4) {
						int sxp = ox + std::min(xx, W - 1), syp = oy + std::min(yy, D - 1);
						if (split && (sxp < x0 || sxp >= x0 + 256 || syp < y0 || syp >= y0 + 256)) continue;   // Nachbarzone: nicht messbar
						int zz; if (!ZoneSurface(zone, sxp, syp, &zz, &tt)) { ok = false; break; }
						zmin = std::min(zmin, zz); zmax = std::max(zmax, zz);
					}
					if (!ok || zmax - zmin > d.uneven) continue;
					z = zmax + 1 + d.zOffset;
				}
				int placed = 0;
				if (split && ZoneSplit::Crosses(ox, oy, W, D)) {
					uint64_t id = ((uint64_t)Hash((uint32_t)ox, (uint32_t)oy, (uint32_t)z, m->salt) << 32) ^ (uint64_t)Hash((uint32_t)rot, m->salt, seed);
					placed = ZoneSplit::Place(zone, zx, zy, m->s, ox, oy, z, rot, d.foundation ? 1 : 0, 0, id);
				} else placed = LabBridge::StampIntoZone(m->s, ox, oy, z, rot, d.foundation ? 1 : 0, zone);
				if (placed > 0) {
					pl.push_back(Placed{ cx, cy, half, d.spacing, ox, oy, ox + W, oy + D, isBox });
					InterlockedIncrement(&m->placed); done++;
				}
			}
		}
	}
	static void ZoneBiomePlants(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk);
	// ---- Baeume eines Bioms fuer die Stadt (Feldmitten zwischen den Baumhaeusern) ---------------------
	// Alles, was im Biom als Baum waechst: Zonen-Modelle der Art "Baum" (fuer dieses Biom oder ueberall),
	// die Hangbaeume (falls an), der prozedurale Pflanzenbaum (Gewicht > 0) und eigene Pflanzendateien,
	// die mindestens 12 Bloecke hoch sind (niedrigere sind Blumen/Bueschel). City vergroessert sie.
	static const Lab::Structure* PlantStructureRaw(const BiomeDef& def, int id, int kind, uint32_t h);
	static bool CityTreesFor(int biome, std::vector<Lab::Structure>& out) {
		BiomeDef def;
		if (!DefById(biome, def)) return true;
		std::vector<ZModelRT*>* set = g_zm;
		if (set) for (ZModelRT* m : *set) {
			if (!m || m->def.kind != ZM_TREE || m->vegSlot >= 0 || !m->s.Valid()) continue;
			if (m->biomeOff & (1u << biome)) continue;
			if (!(m->biomeOn & (1u << biome)) && m->biomeId != biome && m->biomeId != 0) continue;
			Lab::Structure c = m->s; c.name = "Zonenmodell " + m->file; out.push_back(c);
		}
		if (def.slopeTrees > 0) for (int v = 0; v < 2; v++) { Lab::Structure c = MakeBigTree(def, Hash((uint32_t)biome, 100u, (uint32_t)v)); c.name = "Hangbaum"; out.push_back(c); }
		// Der prozedurale Pflanzen-Laubbaum wirkt vergroessert grob -- als Laubbaum nimmt City die Kastanien
		// aus Strukturen\Stadt\Kastanie_*.cub (im LAB anpassbar).
if (g_tcsInit) for (size_t i = 0; i < def.plantFiles.size(); i++) {
			EnterCriticalSection(&g_tcs);
			const Lab::Structure* f = PlantStructureRaw(def, biome, -1, (uint32_t)i);
			Lab::Structure c; if (f) c = *f;
			LeaveCriticalSection(&g_tcs);
			if (c.Valid() && c.sz >= 12) { c.name = "Pflanzendatei " + def.plantFiles[i]; out.push_back(c); }
		}
		return def.nativeTrees > 0;                                       // native Baeume im Biom -> City nimmt 0x832 dazu
	}
	// Kleine Pflanzen des Bioms fuer die Pflanzgaerten der Stadtviertel: prozedurale Bueschel/Pilze/Kristalle/Kakteen/
	// Graeser (Gewicht > 0) und eigene Pflanzendateien bis 9 x 9 x 14.
	static bool CityPlantsFor(int biome, std::vector<Lab::Structure>& out) {
		BiomeDef def;
		if (!DefById(biome, def) || !g_tcsInit) return false;
		EnterCriticalSection(&g_tcs);
		for (int k = BiomeGen::PK_BUSH; k < BiomeGen::PK_COUNT; k++) {
			if (def.plantWeight[k] <= 0) continue;
			for (uint32_t v = 0; v < 3; v++) { const Lab::Structure* f = PlantStructureRaw(def, biome, k, v); if (f && f->Valid()) out.push_back(*f); }
		}
		for (size_t i = 0; i < def.plantFiles.size(); i++) { const Lab::Structure* f = PlantStructureRaw(def, biome, -1, (uint32_t)i); if (f && f->Valid()) out.push_back(*f); }
		LeaveCriticalSection(&g_tcs);
		for (size_t i = 0; i < out.size(); ) {
			Lab::Structure c = out[i];
			if (!c.CropToContent() || c.sx > 9 || c.sy > 9 || c.sz > 14 || c.sz < 2) out.erase(out.begin() + i); else { out[i] = c; i++; }
		}
		return !out.empty();
	}
	static bool CityAnimalsFor(int biome, std::vector<int>& out) {
		BiomeDef def;
		if (!DefById(biome, def)) return false;
		for (int r : def.cityAnimals) if (r >= 0 && r < MobLab::kRaceCount) out.push_back(r);
		return !out.empty();
	}
	// ---- Stadt auf der Ebene (City.cpp) ------------------------------------------------------
	// Mitte = Zellmitte (Zelle +0/+4) jeder Klimazelle mit Stadt-Biom -- genau dort liegt die Ebene
	// (PlainAt nimmt Dom.px/py = Mitte der naechsten Zelle dieses Bioms). Frueher entschied das Biom
	// am Zonenmittelpunkt (DominantRaw), ob die Zone ueberhaupt Stadt bekommt: am Stadtrand konnte
	// die Mitte einer Nachbarzone schon zu einer anderen Zelle zaehlen -> dort fehlte die Haelfte der
	// Bauwerke (abgeschnitten). Jetzt: alle Zellen um die Zone pruefen, jede beruehrte Stadt stempeln.
	static const uint8_t* CellPtrRaw(const uint8_t* world, int cx, int cy) {
		if (cx < 0 || cy < 0 || cx > 1023 || cy > 1023) return nullptr;
		__try { return ((const uint8_t* const*)(world + W_CELLGRID))[cx * 1024 + cy]; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static bool CellCenterRaw(const uint8_t* c, int* x, int* y) {
		__try { *x = *(const int*)c; *y = *(const int*)(c + 4); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static int CitySites(const uint8_t* world, int zx, int zy, City::Site* out, int maxN) {
		HookSet* hs = g_hs;
		if (!g_on || !hs) return 0;
		const int x = zx * 256 + 128, y = zy * 256 + 128;
		int x0 = Div16k(x - 0x4000), x1 = Div16k(x + 0x4000), y0 = Div16k(y - 0x4000), y1 = Div16k(y + 0x4000);
		uint32_t seed = WorldSeedRaw(world);
		int n = 0;
		for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) {
			if (cx < 0 || cy < 0 || cx > 1023 || cy > 1023 || n >= maxN) continue;
			int id = g_cell[cx * 1024 + cy];
			if (id < 1 || id > kMaxBiomes) continue;
			const HookDef& b = hs->d[id - 1];
			if (!b.used || !b.enabled || !b.plainOn || b.city <= 0 || b.plainRadius < 200.0f) continue;
			const uint8_t* c = CellPtrRaw(world, cx, cy);
			int px, py;
			if (!c || !CellCenterRaw(c, &px, &py)) continue;
			City::Site s;
			s.kind = City::ResolveKind(b.city, seed, px, py);
			if (s.kind == City::NONE) continue;
			s.cx = px; s.cy = py; s.seed = seed; s.biome = id;
			float R = b.cityRadius > 0.0f ? b.cityRadius : b.plainRadius - 30.0f;
			s.radius = std::max(150.0f, std::min(R, b.plainRadius - 20.0f));
			// Puffer: Plan schon anlegen, wenn die Zone bis 512 Bloecke vor der Stadt liegt
			City::Site wide = s; wide.radius = s.radius + 512.0f;
			if (!City::ZoneTouches(wide, zx, zy)) continue;
			out[n++] = s;
		}
		return n;
	}
	static void ZoneCity(const uint8_t* world, void* zone, int zx, int zy) {
		if (!zone) return;
		void* gc = Host::IsServer() ? nullptr : (void*)(world - GC_WORLD);   // Server: kein GameController um die Welt herum
		City::Site sites[9];
		int n = CitySites(world, zx, zy, sites, 9);
		for (int i = 0; i < n; i++) City::Register(gc, sites[i]);          // Plan + Bauauftraege fuer alle beruehrten Zonen
		n = City::ZonePlans(zx, zy, sites, 9);                             // hat DIESE Zone einen Auftrag?
		for (int i = 0; i < n; i++) {
			const City::Site& s = sites[i];
			// Bodenhoehe: Punkt der Zone, der der Stadtmitte am naechsten liegt; ist dort Wasser o. ae.,
			// weitere Punkte Richtung Zonenmitte. Nichts gefunden -> City nimmt die schon gemessene Hoehe.
			int zg = INT_MIN; uint8_t t;
			for (int k = 0; k <= 8 && zg == INT_MIN; k++) {
				int nx = std::max(zx * 256, std::min(s.cx, zx * 256 + 255)), ny = std::max(zy * 256, std::min(s.cy, zy * 256 + 255));
				int gx = nx + (zx * 256 + 128 - nx) * k / 8, gy = ny + (zy * 256 + 128 - ny) * k / 8;
				float dx = (float)(gx - s.cx), dy = (float)(gy - s.cy);
				if (k > 0 && dx * dx + dy * dy > s.radius * s.radius) continue;          // nur innerhalb der (flachen) Stadt messen
				int z;
				if (ZoneSurface(zone, gx, gy, &z, &t)) zg = z;
			}
			City::StampZone(gc, zone, zx, zy, s, zg);
			Tracking::Note(Tracking::T_CITY, City::KindName(s.kind), s.cx, s.cy, zg == INT_MIN ? 0 : zg, s.seed);
		}
	}
	static void ZoneCitySEH(const uint8_t* world, void* zone, int zx, int zy) {
		__try { ZoneCity(world, zone, zx, zy); } __except (EXCEPTION_EXECUTE_HANDLER) { City::NoteAbort(); }
	}
	// ---- Kleine Tempel (Temples.cpp) --------------------------------------------------------------
	// Je 768er-Rasterfeld (3 x 3 Zonen) hoechstens einer: Zone im Feld und Platz in der Zone aus Weltsamen + Feld
	// (deterministisch, kein rand()); ganz in der Zone, ebener Boden (Hoehenunterschied <= 6), kein Wasser, keine
	// Stadt, kein nativer Ort. Die Art folgt dem Boden: Schnee (Typ 10), Sand (9), eigenes Biom (Name: Pilz/Pastell,
	// Sumpf/Dschungel, Kristall/Wueste, Schnee/Kalt/Frost), Herbstlaub (rot > gruen), sattes Gruen = Dschungel, sonst Standard.
	static bool ZoneSurfaceRGB(void* zone, int x, int y, int* zOut, uint8_t* tOut, uint8_t* rgb) {
		int top;
		if (!ColumnHeightRaw(zone, x, y, &top)) return false;
		__try {
			ColData_t col = (ColData_t)Re(A_COLDATA);
			for (int z = top - 1; z > top - 400; z--) {
				uint8_t* v = col(x, y, z, zone);
				if (!v) return false;
				uint8_t t = v[3] & 0x1F;
				if (t == 0) continue;
				if (t == 2) return false;
				*zOut = z; *tOut = t; rgb[0] = v[0]; rgb[1] = v[1]; rgb[2] = v[2]; return true;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return false;
	}
	static volatile LONG c_temples = 0, c_templeBusy = 0;
	// Grundflaeche frei (2026-10-29)? Der Hook laeuft am Ende von generateBiomeContent (Aufruf 0x5EDE84) -- native Haeuser,
	// Baeume, Wege und alle schon gesetzten Mod-Bauten (placeStructure -> Blocktyp 6) stehen da bereits in der Zone.
	// Jede step-te Spalte: oberster fester Block darf kein Bauwerk (6), Stamm (7), Laub (8) oder Weg (11) sein und
	// kein Wasser (ZoneSurface = false). Blocktypen: Infinite-Cubes include/cube/Block.h.
	static bool FootprintClear(void* zone, int zx, int zy, int x0, int y0, int x1, int y1, int step) {
		x0 = std::max(x0, zx * 256); y0 = std::max(y0, zy * 256); x1 = std::min(x1, zx * 256 + 256); y1 = std::min(y1, zy * 256 + 256);
		if (x0 >= x1 || y0 >= y1) return false;
		for (int y = y0; y < y1; y += step) for (int x = x0; x < x1; x += step) {
			int z; uint8_t t;
			if (!ZoneSurface(zone, x, y, &z, &t)) return false;
			t &= 0x1F; if (t == 6 || t == 7 || t == 8 || t == 11) return false;
		}
		return true;
	}
	static void ZoneSmallTemples(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk) {
		if (!g_on || !zone || blk.zoneBlocked) return;
		const int nV = Temples::VariantCount(); if (nV <= 0) return;
		{ City::Site cs[2]; if (City::ZonePlans(zx, zy, cs, 2) > 0) return; }        // keine Stadt
		const uint32_t seed = WorldSeedRaw(world);
		const int G = 3;                                                              // Zonen je Rasterfeld
		const int gx = zx >= 0 ? zx / G : -((-zx + G - 1) / G), gy = zy >= 0 ? zy / G : -((-zy + G - 1) / G);
		const uint32_t h = Hash((uint32_t)gx, (uint32_t)gy, 0x7E3B1E5u, seed);
		if (zx != gx * G + (int)(h % G) || zy != gy * G + (int)((h >> 4) % G)) return;
		if (Hash((uint32_t)gx, (uint32_t)gy, 0x3070u, seed) % 100u >= 30u) return;   // 2026-10-29: nur ~30 % der Rasterfelder
		const int x0 = zx * 256, y0 = zy * 256;
		for (int k = 0; k < 6; k++) {
			uint32_t hk = Hash(h, (uint32_t)k, 0x51u);
			int sx = 48, sy = 48, sz = 24;
			int vPre = (int)(hk % (uint32_t)nV);
			Temples::ModelSize(vPre, &sx, &sy, &sz);
			int ox = x0 + 6 + (int)(hk % (uint32_t)std::max(1, 256 - 12 - sx)), oy = y0 + 6 + (int)((hk >> 9) % (uint32_t)std::max(1, 256 - 12 - sy));
			int zmin = INT_MAX, zmax = INT_MIN; bool ok = true; uint8_t t = 0, rgb[3] = { 0, 0, 0 };
			for (int j = 0; j <= 2 && ok; j++) for (int i = 0; i <= 2 && ok; i++) {
				int zz; uint8_t tt, c3[3];
				if (!ZoneSurfaceRGB(zone, ox + (sx - 1) * i / 2, oy + (sy - 1) * j / 2, &zz, &tt, c3)) { ok = false; break; }
				zmin = std::min(zmin, zz); zmax = std::max(zmax, zz);
				if (i == 1 && j == 1) { t = tt; rgb[0] = c3[0]; rgb[1] = c3[1]; rgb[2] = c3[2]; }
			}
			if (!ok || zmax - zmin > 6) continue;
			int cx = ox + sx / 2, cy = oy + sy / 2;
			if (Conflicts(pl, cx, cy, std::max(sx, sy) / 2, 10, true, ox, oy, ox + sx, oy + sy)) continue;
			// Art nach Boden / Biom
			const char* key = "Standard";
			Dom dm = Dominant(world, cx, cy);
			std::string bn;
			if (dm.id && dm.w >= 0.5f) { BiomeDef def; if (DefById(dm.id, def)) { bn = def.name; for (char& ch : bn) ch = (char)tolower((unsigned char)ch); } }
			if (t == 10 || bn.find("schnee") != std::string::npos || bn.find("kalt") != std::string::npos || bn.find("frost") != std::string::npos) key = "Schnee";
			else if (t == 9 || bn.find("wueste") != std::string::npos || bn.find("kristall") != std::string::npos) key = "Wueste";
			else if (bn.find("pilz") != std::string::npos || bn.find("pastell") != std::string::npos || (rgb[2] > rgb[1] && rgb[0] > rgb[1])) key = "Pastell";
			else if (bn.find("sumpf") != std::string::npos || bn.find("dschungel") != std::string::npos || (rgb[1] > rgb[0] + 70 && rgb[1] > 110)) key = "Dschungel";
			else if (rgb[0] > rgb[1] + 10) key = "Herbst";
			int v = Temples::VariantByName(key); if (v < 0) v = Temples::VariantByName("Standard"); if (v < 0) v = vPre;
			Temples::ModelSize(v, &sx, &sy, &sz);
			if (ox + sx > x0 + 256 || oy + sy > y0 + 256) continue;
			if (NearSite(blk, ox + sx / 2, oy + sy / 2, std::max(sx, sy) / 2) ||                // native Orte (Doerfer, Dungeons)
			    !FootprintClear(zone, zx, zy, ox - 2, oy - 2, ox + sx + 2, oy + sy + 2, 3)) {    // nichts Gebautes / kein Baum darunter
				InterlockedIncrement(&c_templeBusy); continue;
			}
			if (!Temples::Stamp(zone, v, ox, oy, (zmin + zmax) / 2 + 1, seed)) continue;
			pl.push_back(Placed{ cx, cy, std::max(sx, sy) / 2, 10, ox, oy, ox + sx, oy + sy, true });
			InterlockedIncrement(&c_temples);
			{ LONG n = c_temples; if (n <= 5 || n % 25 == 0) std::cout << "[Tempel] " << n << " kleine Tempel gesetzt, " << c_templeBusy << " Plaetze wegen Bebauung/Baeumen/Orten verworfen." << std::endl; }
			return;
		}
	}
	// Tempel am Weltspawn (2026-10-29): Weltspawn = World+0x8000F0 (float x, y, z; Infinite-Cubes World.cpp:527,
	// im Original 0x5A55F8: mov [esi+0x8000F0], 0x4B002080 = 8396928.0). City stempelt den Elfen-Tempel davor.
	static const uint32_t W_SPAWN = 0x8000F0;
	static void ZoneSpawnTemple(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl) {
		if (!zone) return;
		const float sx = *(const float*)(world + W_SPAWN), sy = *(const float*)(world + W_SPAWN + 4);
		if (!(sx > 0.0f && sx < 2.0e9f && sy > 0.0f && sy < 2.0e9f)) return;
		const int spx = (int)sx, spy = (int)sy;
		if (std::abs((spx >> 8) - zx) > 1 || std::abs((spy >> 8) - zy) > 1) return;       // nur Zonen um den Spawn
		{ City::Site cs[2]; if (City::ZonePlans(zx, zy, cs, 2) > 0) return; }              // keine Stadt
		void* gc = Host::IsServer() ? nullptr : (void*)(world - GC_WORLD);
		int x0, y0, x1, y1;
		if (City::SpawnTempleZone(gc, zone, zx, zy, spx, spy, &ZoneSurface, &x0, &y0, &x1, &y1))
			pl.push_back(Placed{ (x0 + x1) / 2, (y0 + y1) / 2, std::max(x1 - x0, y1 - y0) / 2, 10, x0, y0, x1, y1, true });
	}
	static void ZoneSpawnTempleSEH(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl) {
		__try { ZoneSpawnTemple(world, zone, zx, zy, pl); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	// Dungeon-Baukasten (Dungeons.cpp): vor den Zonenmodellen, damit Baeume/Pflanzen ausweichen
	static void ZoneDungeons(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk) {
		if (!g_on || !zone || blk.zoneBlocked) return;
		{ City::Site cs[2]; if (City::ZonePlans(zx, zy, cs, 2) > 0) return; }        // keine Stadt
		int x0, y0, x1, y1;
		auto freeFn = [&](int a0, int b0, int a1, int b1) {
			int cx = (a0 + a1) / 2, cy = (b0 + b1) / 2, half = std::max(a1 - a0, b1 - b0) / 2;
			return !Conflicts(pl, cx, cy, half, 10, true, a0, b0, a1, b1) && !NearSite(blk, cx, cy, half) &&
			       FootprintClear(zone, zx, zy, a0, b0, a1, b1, 3);                    // 2026-10-29: nichts Bestehendes ueberbauen
		};
		if (Dungeons::ZonePlace(world, zone, zx, zy, WorldSeedRaw(world), &ZoneSurface, freeFn, &x0, &y0, &x1, &y1))
			pl.push_back(Placed{ (x0 + x1) / 2, (y0 + y1) / 2, std::max(x1 - x0, y1 - y0) / 2, 10, x0, y0, x1, y1, true });
	}
	static void ZonePlaceAll(const uint8_t* world, void* zone, int zx, int zy) {
		ZoneSplit::OnZone(zone, zx, zy);                                   // Stuecke von Bauwerken der Nachbarzonen
		ZoneCitySEH(world, zone, zx, zy);
		Skills::ZoneObjects(world, zone, zx, zy);                           // Runensteine (Teleport)
		std::vector<Placed> pl;
		ZoneBlock blk; ReadZoneBlock(world, zx, zy, blk);
		ZoneSpawnTempleSEH(world, zone, zx, zy, pl);                        // Elfen-Tempel + Tempelhueter am Weltspawn
		ZoneSmallTemples(world, zone, zx, zy, pl, blk);                     // kleine Tempel zuerst (die anderen weichen aus)
		ZoneDungeons(world, zone, zx, zy, pl, blk);                         // eigene Dungeons (Raum-Baukasten)
		ZoneModelsPlace(world, zone, zx, zy, pl, blk);
		ZoneBigTrees(world, zone, zx, zy, pl, blk);
		ZoneBiomePlants(world, zone, zx, zy, pl, blk);
	}
	static void ZonePlaceAllSEH(const uint8_t* world, void* zone, int zx, int zy) {
		__try { ZonePlaceAll(world, zone, zx, zy); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void ScrubCitySEH(void* zone, int zx, int zy) { __try { City::ScrubZoneSpawns(zone, zx, zy); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void __fastcall Hook_SunCall(void* w, void* edx, int x0, int y0, int x1, int y1, int margin, void* zone) {
		if (x1 - x0 == 256 && y1 - y0 == 256)                                   // Tiere am nativen Spawn: unabhaengig vom Schalter "zonenobjekte"
			Wildlife::OnZoneSpawns(w, zone, x0 >> 8, y0 >> 8, WorldSeedRaw((const uint8_t*)w));
		if (g_zoneObjOn && x1 - x0 == 256 && y1 - y0 == 256) {
			LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
			ZonePlaceAllSEH((const uint8_t*)w, zone, x0 >> 8, y0 >> 8);
			QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
			CrashLogger::PerfZonePlace((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart);
		}
		if (x1 - x0 == 256 && y1 - y0 == 256) ScrubCitySEH(zone, x0 >> 8, y0 >> 8);   // 2026-10-29: keine feindlichen Spawns in Staedten
		((Sun_t)Re(A_SUN))(w, edx, x0, y0, x1, y1, margin, zone);
	}
	static bool InstallTreeAnchor() {
		uint8_t* p = (uint8_t*)Re(A_SUN_CALL);
		if (!p || !Re(A_SUN)) return false;
		// Pruefung unabhaengig vom Programm: an der Stelle steht "call A_SUN" (im Client sind das
		// genau die Bytes kSunCallOrig, im Server zeigt der rel32 auf die uebersetzte Adresse).
		__try {
			if (p[0] != 0xE8) return false;
			uint32_t target = (uint32_t)(uintptr_t)p + 5 + *(int32_t*)(p + 1);
			if (target != Re(A_SUN)) return false;
			if (!Host::IsServer() && memcmp(p, kSunCallOrig, 5) != 0) return false;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		DWORD old;
		if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
		p[0] = 0xE8; *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&Hook_SunCall - ((uintptr_t)p + 5));
		DWORD d2; VirtualProtect(p, 5, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, 5);
		return true;
	}

	// Oberste feste Lage; false bei Wasser, Ueberhang, Baum, Haus oder nicht geladen.
	static bool FindSurface(cube::GameController* gc, cube::Zone* zone, int x, int y, int* zOut, uint8_t* typeOut, bool natural) {
		int h = 0;
		int top = 700, bottom = -64;                                       // Sternmassiv: Gipfel bis ~520
		if (zone && ColumnHeightRaw(zone, x, y, &h)) { top = h + 4; bottom = h - 48; }
		for (int pass = 0; pass < 2; pass++) {
			for (int z = top; z >= bottom; z--) {
				uint8_t r, g, b, t;
				if (!MountainVillage::LabReadBlock(gc, x, y, z, r, g, b, t)) continue;
				if ((t & 0x1F) == 2) return false;                         // Wasser
				if (natural && !IsGround(t)) return false;                 // Holz, Mauer, Dach ...
				for (int k = 1; k <= 3; k++) { uint8_t r2, g2, b2, t2; if (MountainVillage::LabReadBlock(gc, x, y, z + k, r2, g2, b2, t2)) return false; }
				*zOut = z; *typeOut = t; return true;
			}
			if (top == 700) break;
			top = 700; bottom = -64;                                       // Saeulenwert passte nicht -> voll suchen
		}
		return false;
	}

	// Pflanzen-Cache: Spielthread (Test) und Zonen-Thread (Erzeugung) -> unter g_tcs.
	// Eintraege werden nie entfernt, die Zeiger bleiben gueltig.
	static const Lab::Structure* PlantStructureRaw(const BiomeDef& def, int id, int kind, uint32_t h);
	static const Lab::Structure* PlantStructure(const BiomeDef& def, int id, int kind, uint32_t h) {
		if (!g_tcsInit) return nullptr;
		EnterCriticalSection(&g_tcs);
		const Lab::Structure* r = PlantStructureRaw(def, id, kind, h);
		LeaveCriticalSection(&g_tcs);
		return r;
	}
	static const Lab::Structure* PlantStructureRaw(const BiomeDef& def, int id, int kind, uint32_t h) {
		if (kind < 0) {
			if (def.plantFiles.empty()) return nullptr;
			std::string name = def.plantFiles[h % def.plantFiles.size()];
			std::string path = (name.find('\\') == std::string::npos && name.find('/') == std::string::npos) ? Lab::StructuresDir() + "\\" + name : name;
			if (path.size() < 4 || _stricmp(path.c_str() + path.size() - 4, ".cub") != 0) path += ".cub";
			auto it = g_fileCache.find(path);
			if (it == g_fileCache.end()) {
				Lab::Structure s; std::string err;
				if (!Lab::LoadCub(path, s, err) || s.sx > 64 || s.sy > 64) { std::cout << "[Biom] Pflanzendatei: " << (err.empty() ? "zu gross (max 64x64)" : err) << std::endl; s = Lab::Structure(); }
				it = g_fileCache.insert(std::make_pair(path, s)).first;
			}
			return it->second.Valid() ? &it->second : nullptr;
		}
		PlantKey k{ id, kind, (int)(h % 8u), g_defVersion };
		auto it = g_plantCache.find(k);
		if (it == g_plantCache.end()) it = g_plantCache.insert(std::make_pair(k, MakePlant(def, kind, Hash((uint32_t)id, (uint32_t)kind, (uint32_t)k.var)))).first;
		return &it->second;
	}

	// Eigene Pflanzen der Biome (Pflanzen-Spalte + "Eigene .cub") -- im Zonen-Thread mit der Zone
	// erzeugt (frueher nachtraeglich auf dem Spielthread). Gleiche Positionen wie vorher.
	static void ZoneBiomePlants(const uint8_t* world, void* zone, int zx, int zy, std::vector<Placed>& pl, const ZoneBlock& blk) {
		HookSet* hs = g_hs;
		if (!g_on || !hs || !zone || blk.zoneBlocked) return;
		uint32_t seed = WorldSeedRaw(world);
		int x0 = zx * 256, y0 = zy * 256;
		std::map<int, BiomeDef> defs;                                   // je Biom einmal kopieren
		auto Def = [&](int id) -> const BiomeDef* {
			auto it = defs.find(id);
			if (it == defs.end()) { BiomeDef d; if (!DefById(id, d)) d.plantDensity = -1; it = defs.insert(std::make_pair(id, d)).first; }
			return it->second.plantDensity > 0 ? &it->second : nullptr;
		};
		for (int i = 0; i < 300; i++) {
			uint32_t h = Hash((uint32_t)zx, (uint32_t)zy, (uint32_t)i, seed ^ 0x9A17u);
			int x = x0 + 4 + (int)(h % 248u), y = y0 + 4 + (int)((h >> 8) % 248u);
			Dom d = DominantRaw(world, x, y);
			if (!d.id || d.w < 0.55f) continue;
			const HookDef& b = hs->d[d.id - 1];
			if (!b.used || !b.enabled) continue;
			const BiomeDef* def = Def(d.id);
			if (!def || i >= def->plantDensity) continue;
			if (InPlain(def->plainOn, def->plainRadius, def->plainRim, d, x, y, 8.0f)) continue;   // freie Ebene
			int kind = PickPlantKind(*def, Hash(h, 1));
			const Lab::Structure* st = PlantStructure(*def, d.id, kind, Hash(h, 2));
			if (!st) continue;
			int ox = x - st->sx / 2, oy = y - st->sy / 2;
			if (ox < x0 || oy < y0 || ox + st->sx > x0 + 256 || oy + st->sy > y0 + 256) continue;   // ganz in der Zone
			int half = std::max(st->sx, st->sy) / 2;
			if (Conflicts(pl, x, y, half, 2) || NearSite(blk, x, y, half)) continue;
			int z; uint8_t t;
			if (!ZoneSurface(zone, x, y, &z, &t) || !IsGround(t)) continue;
			int rot = (kind >= 0 && st->sx == st->sy) ? (int)(Hash(h, 3) & 3) : 0;    // nur quadratische drehen
			if (LabBridge::StampIntoZone(*st, ox, oy, z + 1, rot, 0, zone) > 0) {
				pl.push_back(Placed{ x, y, half, 2, ox, oy, ox + st->sx, oy + st->sy, false });
				InterlockedIncrement(&c_plants);
			}
		}
	}

	struct Box { int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN; void Add(int a, int b, int c, int d) { x0 = std::min(x0, a); y0 = std::min(y0, b); x1 = std::max(x1, c); y1 = std::max(y1, d); } bool Empty() const { return x0 > x1; } };

	// Eine Zone wird ueber mehrere Bilder verteilt bearbeitet (Zeitbudget je Bild),
	// damit 300 Pflanzenversuche keinen Ruckler erzeugen.
	namespace {
		struct MineOp { int x, y, z; uint8_t r, g, b, t; };
		struct ZoneJob {
			int zx = 0, zy = 0, i = 0;
			bool active = false, prepared = false, skipPlants = false, town = false;
			bool dungeonDone = false, minesPlanned = false;
			cube::Zone* zone = nullptr;
			Box box; int plants = 0;
			std::map<int, BiomeDef> defs;
			std::vector<MineOp> ops; size_t opPos = 0; int mineSegs = 0;
			std::vector<std::pair<int, int>> trees;                    // gesetzte Hangbaeume (Abstand)
		};
		ZoneJob g_job;
		LARGE_INTEGER g_qpf = { 0 };
	}
	static double NowMs() {
		LARGE_INTEGER t; QueryPerformanceCounter(&t);
		if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
		return (double)t.QuadPart * 1000.0 / (double)g_qpf.QuadPart;
	}
	static const BiomeDef* JobDef(ZoneJob& j, int id) {
		auto it = j.defs.find(id);
		if (it == j.defs.end()) { BiomeDef d; if (!DefById(id, d)) d.plantDensity = -1; it = j.defs.insert(std::make_pair(id, d)).first; }
		return it->second.plantDensity >= 0 ? &it->second : nullptr;
	}

	// ---- Schreibschutz gegen Karten- und Chunk-Thread (schwarze Minimap) -------------
	// Der Kartenthread World_streamChunksAroundPlayer haelt beim Erzeugen jeder Kartenkachel
	// aus den Zonendaten die Sperre World+0x8000D8 (0x469B62 -> streamTileChunk 0x469B75 ->
	// Freigabe 0x469B82). Der Chunk-Thread nimmt erst World+0x8000C0, dann +0x8000D8
	// (0x46AF84, 0x46AF91). Schreibt der Mod ohne diese Sperre in eine Zone, kann die Kachel
	// aus halb geschriebenen Saeulen entstehen. Deshalb: beide Sperren, in derselben Reihenfolge.
	static void LockWorldSEH(cube::World* w)   { __try { w->Lock(); }   __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void UnlockWorldSEH(cube::World* w) { __try { w->Unlock(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void LockBlocksSEH(cube::World* w)   { __try { EnterCriticalSection((LPCRITICAL_SECTION)&w->block_op_critical_section); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void UnlockBlocksSEH(cube::World* w) { __try { LeaveCriticalSection((LPCRITICAL_SECTION)&w->block_op_critical_section); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	struct WorldWriteLock {
		cube::World* w;
		explicit WorldWriteLock(cube::GameController* gc) : w(&gc->world) { LockWorldSEH(w); LockBlocksSEH(w); }
		~WorldWriteLock() { UnlockBlocksSEH(w); UnlockWorldSEH(w); }
	};
	static int StampLocked(cube::GameController* gc, const Lab::Structure& s, int x, int y, int z, int rot, int fundament, int zx, int zy) {
		WorldWriteLock lk(gc);
		return LabBridge::StampStructure(gc, s, x, y, z, rot, fundament, zx, zy);
	}
	int StampLive(cube::GameController* gc, const Lab::Structure& s, int x, int y, int z, int fundament) { return StampLocked(gc, s, x, y, z, 0, fundament, INT_MIN, INT_MIN); }
	bool ZoneSurfaceAt(void* zone, int x, int y, int* z, uint8_t* t) { return ZoneSurface(zone, x, y, z, t); }
	// Zone sicher geladen und nicht am Streaming-Rand? (Lehre aus dem Bergdorf, MountainVillage.cpp:
	// "SetBlock in Streaming-Rand-Zonen -> schwarze Karte"): die Zone und alle 8 Nachbarn geladen.
	static bool ZoneSettled(cube::GameController* gc, int zx, int zy) {
		for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) if (!LabBridge::ZoneAt(gc, zx + a, zy + b)) return false;
		return true;
	}
	static const long long kDecoRange = 320;      // Zonenmitte hoechstens so weit vom Spieler (Bergdorf: 250)

	static void PlaceZoneDungeon(cube::GameController* gc, ZoneJob& j, uint32_t seed) {
		const uint8_t* world = (const uint8_t*)gc + GC_WORLD;
		int zx = j.zx, zy = j.zy, x0 = zx * 256, y0 = zy * 256;
		Dom dc = DominantRaw(world, x0 + 128, y0 + 128);
		BiomeDef ddef;
		bool nearVillage = false;
		for (int a = -1; a <= 1 && !nearVillage; a++) for (int b = -1; b <= 1; b++) if (MountainVillage::IsVillageSiteZone(gc, zx + a, zy + b)) { nearVillage = true; break; }
		if (!dc.id || dc.w < 0.75f || j.town || nearVillage || !DefById(dc.id, ddef) || ddef.dungeonChance <= 0) return;
		if (Hash((uint32_t)zx, (uint32_t)zy, seed, 0xD1u) % 100u >= (uint32_t)ddef.dungeonChance) return;
		bool known = false; for (const Dungeon& g : g_dungeons) if (g.zx == zx && g.zy == zy) known = true;
		DungeonPlan p = MakeDungeon(ddef, Hash((uint32_t)zx, (uint32_t)zy, seed, 0xD2u));
		int W = p.s.sx, D = p.s.sy;
		uint32_t hp = Hash((uint32_t)zx, (uint32_t)zy, seed, 0xD3u);
		int ox = x0 + 8 + (int)(hp % (uint32_t)std::max(1, 240 - W)), oy = y0 + 8 + (int)((hp >> 12) % (uint32_t)std::max(1, 240 - D));
		int zmin = INT_MAX, zmax = INT_MIN;
		for (int yy = 0; yy <= D; yy += 4) for (int xx = 0; xx <= W; xx += 4) {
			int z; uint8_t t;
			if (!FindSurface(gc, j.zone, ox + std::min(xx, W - 1), oy + std::min(yy, D - 1), &z, &t, true)) return;
			zmin = std::min(zmin, z); zmax = std::max(zmax, z);
		}
		if (zmax - zmin > 24) return;
		if (InPlain(ddef.plainOn, ddef.plainRadius, ddef.plainRim, dc, ox + W / 2, oy + D / 2, (float)std::max(W, D))) return;   // freie Ebene
		if (StampLocked(gc, p.s, ox, oy, zmax + 1, 0, 1, zx, zy) <= 0) return;
		j.box.Add(ox, oy, ox + W, oy + D);
		InterlockedIncrement(&c_dungeons);
		if (!known) {
			Dungeon g; g.zx = zx; g.zy = zy; g.id = dc.id; g.ox = ox; g.oy = oy; g.oz = zmax + 1;
			g.cx = ox + W / 2; g.cy = oy + D / 2; g.cz = zmax + 2; g.rooms = p.rooms; g.populated = false;
			g_dungeons.push_back(g);
			std::cout << "[Biom] Dungeon '" << ddef.name << "' in Zone (" << zx << "," << zy << ") bei Block ("
			          << g.cx << "," << g.cy << "," << g.cz << "), " << p.rooms.size() << " Raeume." << std::endl;
		}
	}

	// ---- Minen ------------------------------------------------------------------
	// Bloecke setzt World::SetBlock (0x44E7A0) -- dieselbe Funktion, mit der der Zonenbau und
	// das Bergdorf (MountainVillage, inkl. Luft fuer Terrassen) bauen; dort wie hier unter der
	// Weltsperre auf dem Spielthread. Luft (Typ 0) ausserhalb des gespeicherten Saeulenbereichs
	// ist ein No-op, im Berg wird die Saeule nach unten erweitert (tilemap_writeGlyphColumn).
	static bool SolidAt(cube::GameController* gc, int x, int y, int z) {
		uint8_t r, g, b, t; return MountainVillage::LabReadBlock(gc, x, y, z, r, g, b, t) && (t & 0x1F) != 2;
	}
	static void PlanZoneMines(cube::GameController* gc, ZoneJob& j) {
		const uint8_t* world = (const uint8_t*)gc + GC_WORLD;
		int x0 = j.zx * 256, y0 = j.zy * 256, x1 = x0 + 255, y1 = y0 + 255;
		Dom dc = DominantRaw(world, x0 + 128, y0 + 128);
		BiomeDef def;
		if (!dc.id || dc.w < 0.5f || !DefById(dc.id, def) || def.mineShare <= 0.0f || !def.terrainOn || def.ridgeHeight <= 0.0f) return;
		if (InPlain(def.plainOn, def.plainRadius, def.plainRim, dc, x0 + 128, y0 + 128, 182.0f)) return;   // freie Ebene: keine Minen
		std::vector<MineSeg> segs; PlanMines(def, x0, y0, x1, y1, segs);
		if (segs.empty()) return;
		auto push = [&](int x, int y, int z, const RGB& c, uint8_t t) {
			if (x < x0 || x > x1 || y < y0 || y > y1) return;
			MineOp o; o.x = x; o.y = y; o.z = z; o.r = c.r; o.g = c.g; o.b = c.b; o.t = t; j.ops.push_back(o);
			j.box.Add(x, y, x + 1, y + 1);
		};
		const RGB air{ 0, 0, 0 };
		for (const MineSeg& sg : segs) {
			if (sg.kind == 2) {
				// Senkrechter Schacht 3x3 von der Stollendecke bis ueber die Oberflaeche, Holzkranz oben
				int cx = (int)sg.ax, cy = (int)sg.ay, top = sg.z;
				for (int z = sg.z + 1; z < sg.z + 360; z++) {
					if (!SolidAt(gc, cx, cy, z) && !SolidAt(gc, cx, cy, z + 1) && !SolidAt(gc, cx, cy, z + 2)) { top = z; break; }
					top = z;
				}
				for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
					for (int z = sg.z + 1; z <= top; z++) push(cx + dx, cy + dy, z, air, 0);
				for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
					if (dx == -2 || dx == 2 || dy == -2 || dy == 2) { push(cx + dx, cy + dy, top - 1, def.mineWood, 1); push(cx + dx, cy + dy, top, def.mineWood, 1); }
				j.mineSegs++;
				continue;
			}
			float dx = sg.bx - sg.ax, dy = sg.by - sg.ay, len = std::sqrt(dx * dx + dy * dy);
			if (len < 1.0f) continue;
			float ux = dx / len, uy = dy / len, half = sg.w * 0.5f;
			int bx0 = std::max(x0, (int)std::floor(std::min(sg.ax, sg.bx) - half - 1)), bx1 = std::min(x1, (int)std::ceil(std::max(sg.ax, sg.bx) + half + 1));
			int by0 = std::max(y0, (int)std::floor(std::min(sg.ay, sg.by) - half - 1)), by1 = std::min(y1, (int)std::ceil(std::max(sg.ay, sg.by) + half + 1));
			int before = (int)j.ops.size();
			for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++) {
				float px = x + 0.5f - sg.ax, py = y + 0.5f - sg.ay;
				float t = px * ux + py * uy, d = std::fabs(px * uy - py * ux);
				if (t < 0.0f || t > len || d > half) continue;
				if (!SolidAt(gc, x, y, sg.z + sg.h + 1)) continue;          // keine Decke = Freiluft (Talseite) -> nicht graben
				int ti = (int)t;
				bool wall = d > half - 1.0f;                                   // aeusserste Spalte: Stuetzen/Laternen
				bool support = ti % 6 == 0;
				for (int z = sg.z + 1; z <= sg.z + sg.h; z++) {
					if (support && wall) push(x, y, z, (z == sg.z + 3 && ti % 12 == 0) ? def.mineLight : def.mineWood, 1);
					else if (support && z == sg.z + sg.h) push(x, y, z, def.mineWood, 1);   // Deckenbalken
					else push(x, y, z, air, 0);
				}
				// Sohle: zwei Schienen auf Holzschwellen (nur im Hauptstollen)
				if (sg.kind == 0) {
					if (d >= 0.6f && d <= 1.6f) push(x, y, sg.z, def.mineRail, 1);
					else if (d < 0.6f && ti % 2 == 0) push(x, y, sg.z, def.mineWood, 1);
				}
			}
			if ((int)j.ops.size() > before) j.mineSegs++;
		}
	}

	// true = Zone fertig
	static bool StepZone(cube::GameController* gc, ZoneJob& j, double budgetMs) {
		double t0 = NowMs();
		uint32_t seed = SeedOf(gc);
		const uint8_t* world = (const uint8_t*)gc + GC_WORLD;
		if (!j.prepared) {
			j.zone = LabBridge::ZoneAt(gc, j.zx, j.zy);
			if (!j.zone) return true;                                   // nicht (mehr) geladen
			j.skipPlants = MountainVillage::IsVillageSiteZone(gc, j.zx, j.zy);
			j.town = MountainVillage::LabObjectCount(gc, j.zx, j.zy) > 12;   // native Siedlung (0x188-Objekte)
			j.prepared = true;
		}
		j.zone = LabBridge::ZoneAt(gc, j.zx, j.zy);                     // kann zwischen zwei Bildern entladen sein
		if (!j.zone) return true;
		int x0 = j.zx * 256, y0 = j.zy * 256;
		// Pflanzen und Hangbaeume entstehen jetzt mit der Zone (ZonePlaceAll, Zonen-Thread).
		if (!j.dungeonDone) { PlaceZoneDungeon(gc, j, seed); j.dungeonDone = true; if (NowMs() - t0 > budgetMs) return false; }
		// Minen: Abschnitte planen, dann Bloecke in Haeppchen unter der Weltsperre setzen
		if (!j.minesPlanned) { PlanZoneMines(gc, j); j.minesPlanned = true; if (NowMs() - t0 > budgetMs) return false; }
		if (j.opPos < j.ops.size()) {
			cube::World* wld = &gc->world;
			LockWorldSEH(wld); LockBlocksSEH(wld);
			while (j.opPos < j.ops.size()) {
				const MineOp& o = j.ops[j.opPos++];
				MountainVillage::LabPutBlock(gc, o.x, o.y, o.z, o.r, o.g, o.b, o.t);
				if ((j.opPos & 255) == 0 && NowMs() - t0 > budgetMs) break;
			}
			UnlockBlocksSEH(wld); UnlockWorldSEH(wld);
			if (j.opPos < j.ops.size()) return false;
		}
		if (!j.box.Empty()) LabBridge::RemeshArea(gc, j.box.x0, j.box.y0, j.box.x1, j.box.y1);
		if (j.plants) InterlockedExchangeAdd(&c_plants, j.plants);
		if (j.mineSegs) InterlockedExchangeAdd(&c_mines, j.mineSegs);
		InterlockedIncrement(&c_zones);
		return true;
	}

	// =====================================================================
	// 7. Eigene Mobs in Spielernaehe, Dungeon-Besatzung (Spielthread)
	// =====================================================================
	static const MobLab::CustomMob* MobFromFile(const std::string& name) {
		std::string path = (name.find('\\') == std::string::npos && name.find('/') == std::string::npos) ? MobLab::MobsDir() + "\\" + name : name;
		if (path.size() < 4 || _stricmp(path.c_str() + path.size() - 4, ".mob") != 0) path += ".mob";
		// relativer Pfad mit Unterordner (z. B. "Tiere\\nashorn.mob" aus der Dateiauswahl): zuerst unter Strukturen\\Mobs suchen
		if (!(path.size() > 1 && (path[1] == ':' || path[0] == '\\')) && GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			std::string alt = MobLab::MobsDir() + "\\" + path;
			if (GetFileAttributesA(alt.c_str()) != INVALID_FILE_ATTRIBUTES) path = alt;
		}
		auto it = g_mobCache.find(path);
		if (it == g_mobCache.end()) {
			MobLab::CustomMob m; std::string err;
			if (!MobLab::LoadMob(path, m, err)) { std::cout << "[Biom] Mob-Datei: " << err << std::endl; m.baseRace = -1; }
			it = g_mobCache.insert(std::make_pair(path, m)).first;
		}
		return it->second.baseRace >= 0 ? &it->second : nullptr;
	}

	static bool PlayerBlock(cube::GameController* gc, int* x, int* y, int* z) {
		__try {
			if (!gc || !gc->local_player) return false;
			*x = (int)(gc->local_player->entity_data.position.X >> 16);
			*y = (int)(gc->local_player->entity_data.position.Y >> 16);
			*z = (int)(gc->local_player->entity_data.position.Z >> 16);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static int PlayerLevelRaw(cube::GameController* gc) {
		__try { return gc && gc->local_player ? (int)gc->local_player->entity_data.level : 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
	}
	// Schwierigkeit (LAB: Biom > Eigene Mobs): Modus 0 = Spielerlevel +- Abweichung, 1 = feste Spanne. Deterministisch je Spawn (h).
	static int BiomeMobLevel(cube::GameController* gc, const BiomeDef& def, uint32_t h) {
		if (def.mobLevelMode == 1) {
			int64_t lo = (std::max)(1, def.mobLevelMin), hi = (std::max)((int64_t)def.mobLevelMax, lo);
			return (int)(lo + (int64_t)(h % (uint64_t)(hi - lo + 1)));
		}
		int64_t p = (std::max)(1, PlayerLevelRaw(gc)), o = (std::max)(0, def.mobLevelOffset);
		int64_t v = p - o + (int64_t)(h % (uint64_t)(2 * o + 1));
		return (int)(std::max)((int64_t)1, (std::min)(v, (int64_t)INT_MAX));
	}
	static uint64_t SpawnEntry(cube::GameController* gc, const BiomeDef& def, bool useFile, const std::string& file, int race,
	                           int x, int y, int z, float size, bool wander) {
		const int64_t U = 65536;
		int64_t fx = (int64_t)x * U + U / 2, fy = (int64_t)y * U + U / 2, fz = (int64_t)z * U;
		int lvl = BiomeMobLevel(gc, def, Hash((uint32_t)x, (uint32_t)y, (uint32_t)z, GetTickCount()));
		if (useFile) {
			const MobLab::CustomMob* m = MobFromFile(file);
			if (!m) return 0;
			MobLab::CustomMob c = *m;
			c.level = lvl;                                                 // Biom-Schwierigkeit gilt auch fuer .mob-Dateien
			if (size > 1.01f) c.sizeFactor *= size;
			std::string rep;
			return MobLab::SpawnCustomAt(gc, c, fx, fy, fz, rep);
		}
		return MobLab::SpawnRaceAt(gc, race, 1, lvl, fx, fy, fz, true, wander, size);
	}

	static void PruneTracked(cube::GameController* gc) {
		for (size_t i = 0; i < g_tracked.size(); ) {
			int64_t x, y, z;
			if (!MobLab::CreaturePos(gc, g_tracked[i].guid, &x, &y, &z)) g_tracked.erase(g_tracked.begin() + i);
			else i++;
		}
	}

	static void SpawnerTick(cube::GameController* gc, int px, int py, const Dom& here) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 1000) return;
		last = now;
		PruneTracked(gc);
		if (!here.id || here.w < 0.5f) return;
		BiomeDef def; if (!DefById(here.id, def) || def.mobMax <= 0) return;
		const bool useRaces = def.spawnerRaces && !def.races.empty();
		if (def.mobFiles.empty() && !useRaces) return;
		int nearCount = 0;
		for (const Tracked& t : g_tracked) {
			if (t.id != here.id || t.dungeon >= 0) continue;
			int64_t x, y, z; if (!MobLab::CreaturePos(gc, t.guid, &x, &y, &z)) continue;
			long long dx = (x >> 16) - px, dy = (y >> 16) - py;
			if (dx * dx + dy * dy < 160LL * 160LL) nearCount++;
		}
		if (nearCount >= def.mobMax || g_tracked.size() >= 48) return;
		Rng r(Hash(now, (uint32_t)px, (uint32_t)py));
		const uint8_t* world = (const uint8_t*)gc + GC_WORLD;
		for (int attempt = 0; attempt < 6; attempt++) {
			float a = r.Unit() * 6.2832f; int dist = r.Range(20, 40);
			int x = px + (int)(std::cos(a) * dist), y = py + (int)(std::sin(a) * dist);
			Dom d = DominantRaw(world, x, y);
			if (d.id != here.id || d.w < 0.5f) continue;
			cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
			if (!zone) continue;
			int z; uint8_t t;
			if (!FindSurface(gc, zone, x, y, &z, &t, false)) continue;
			size_t pool = def.mobFiles.size() + (useRaces ? def.races.size() : 0), pick = r.Next() % pool;
			uint64_t g = pick < def.mobFiles.size()
				? SpawnEntry(gc, def, true, def.mobFiles[pick], -1, x, y, z + 1, 1.0f, true)
				: SpawnEntry(gc, def, false, std::string(), def.races[pick - def.mobFiles.size()], x, y, z + 1, 1.0f, true);
			if (g) { g_tracked.push_back({ g, here.id, -1 }); InterlockedIncrement(&c_mobs); }
			break;
		}
	}

	static void DungeonTick(cube::GameController* gc, int px, int py) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 500) return;
		last = now;
		for (size_t di = 0; di < g_dungeons.size(); di++) {
			Dungeon& g = g_dungeons[di];
			if (g.populated) continue;
			long long dx = g.cx - px, dy = g.cy - py;
			if (dx * dx + dy * dy > 56LL * 56LL) continue;
			g.populated = true;
			BiomeDef def; if (!DefById(g.id, def)) continue;
			std::vector<int> races = def.dungeonRaces.empty() ? def.races : def.dungeonRaces;
			const std::vector<std::string>& files = def.dungeonMobFiles;
			if (races.empty() && files.empty()) continue;
			Rng r(Hash((uint32_t)g.zx, (uint32_t)g.zy, 0xB055u));
			int spawned = 0;
			for (int i = 0; i < def.dungeonMobs && !g.rooms.empty(); i++) {
				const DungeonRoom& room = g.rooms[(i + 1) % g.rooms.size()];
				if (room.boss && def.dungeonBoss) continue;                 // Bossraum bekommt nur den Boss
				bool useFile = !files.empty() && (races.empty() || (i % 2) == 1);
				int race = races.empty() ? -1 : races[r.Next() % races.size()];
				std::string f = files.empty() ? std::string() : files[r.Next() % files.size()];
				uint64_t gd = SpawnEntry(gc, def, useFile, f, race, g.ox + room.x + r.Range(-1, 1), g.oy + room.y + r.Range(-1, 1), g.oz + room.z, 1.0f, true);
				if (gd) { g_tracked.push_back({ gd, g.id, (int)di }); spawned++; InterlockedIncrement(&c_mobs); }
			}
			if (def.dungeonBoss) {
				for (const DungeonRoom& room : g.rooms) if (room.boss) {
					bool useFile = races.empty();
					uint64_t gd = SpawnEntry(gc, def, useFile, files.empty() ? std::string() : files[0], races.empty() ? -1 : races[0],
					                         g.ox + room.x, g.oy + room.y, g.oz + room.z, 1.8f, false);
					if (gd) { g_tracked.push_back({ gd, g.id, (int)di }); spawned++; InterlockedIncrement(&c_mobs); }
				}
			}
			std::cout << "[Biom] Dungeon bei (" << g.cx << "," << g.cy << ") besetzt: " << spawned << " Kreaturen"
			          << (def.dungeonBoss ? " (inkl. Boss x1.8)" : "") << "." << std::endl;
		}
	}

	// =====================================================================
	// 8. Framework
	// =====================================================================
	class BiomeModule : public AdventureFramework::IAdventureMod {
	public:
		void OnZoneGenerated(cube::GameController* gc, cube::World* world, int zx, int zy, cube::Zone* zone) override {
			// Zonen-Thread: nur pruefen + vormerken. Gesetzt wird auf dem Spielthread
			// (wie LabBridge beim Neu-Stempeln) -- die Welt ist nicht threadsicher.
			if (!g_on || !g_hs || !world || !g_qcsInit) return;
			const uint8_t* w = (const uint8_t*)world;
			int x0 = zx * 256, y0 = zy * 256;
			const int pts[5][2] = { { 128, 128 }, { 16, 16 }, { 240, 16 }, { 16, 240 }, { 240, 240 } };
			bool any = false;
			for (int i = 0; i < 5 && !any; i++) { Dom d = DominantRaw(w, x0 + pts[i][0], y0 + pts[i][1]); if (d.id && d.w > 0.3f) any = true; }
			if (!any) return;
			EnterCriticalSection(&g_qcs);
			bool dup = false; for (auto& q : g_queue) if (q.first == zx && q.second == zy) { dup = true; break; }
			if (!dup) { g_queue.push_back(std::make_pair(zx, zy)); if (g_queue.size() > 512) g_queue.pop_front(); }
			LeaveCriticalSection(&g_qcs);
		}

		void OnGameTick(cube::GameController* gc) override {
			if (!gc || !gc->local_player) return;
			uint32_t seed = SeedOf(gc);
			if (seed != g_lastSeed) {                 // andere Welt: Sitzungsdaten verwerfen
				g_lastSeed = seed; g_dungeons.clear(); g_tracked.clear(); g_job = ZoneJob();
				EnterCriticalSection(&g_qcs); g_queue.clear(); LeaveCriticalSection(&g_qcs);
			}
			int px, py, pz;
			if (!PlayerBlock(gc, &px, &py, &pz)) return;
			const uint8_t* world = (const uint8_t*)gc + GC_WORLD;
			static DWORD lastStatus = 0; static Dom here = { 0, 0 };
			DWORD now = GetTickCount();
			if (now - lastStatus > 250) {
				lastStatus = now;
				here = DominantRaw(world, px, py);
				Status s; s.hooksOk = g_hooksOk; s.hereBiome = here.id; s.hereW = here.w;
				BiomeDef d; if (here.id && DefById(here.id, d)) s.hereName = d.name; else if (here.id) s.hereName = "(aus)";
				EnterCriticalSection(&g_qcs); s.queue = (long)g_queue.size(); LeaveCriticalSection(&g_qcs);
				s.mobsTracked = (int)g_tracked.size();
				Guard g; s.hookReport = g_hookReport; s.biomes = 0;
				for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i] && g_def[i].enabled) s.biomes++;
				g_status = s;
			}
			// Schalter/Biome geaendert -> alle bestehenden Regionen nachfiltern (auch bei AUS: stellt wieder her)
			if (InterlockedExchange(&g_refilter, 0)) {
				LONG before = c_sitesNow;
				int nreg = RefilterAll((uint8_t*)gc + GC_WORLD);
				if (nreg) std::cout << "[Biom] Native Orte nachgefiltert: " << nreg << " Regionen, jetzt " << c_sitesNow
				                    << " Orte abgeschaltet (vorher " << before << "). Bereits gebaute Zonen aendern sich erst, "
				                       "wenn sie neu erzeugt werden (weggehen und zurueck)." << std::endl;
			}
			if (!g_on) return;

			// Eine Zone nach der anderen, je Bild hoechstens ~3 ms. Nur nahe Zonen, die samt
			// Nachbarn stabil geladen sind (nicht am Streaming-Rand -> sonst schwarze Minimap);
			// die naechste zuerst. Weit entfernte Auftraege verwerfen (kommen beim Neuladen wieder).
			if (!g_job.active) {
				std::pair<int, int> job(INT_MIN, INT_MIN);
				std::vector<std::pair<int, int>> cand;
				EnterCriticalSection(&g_qcs);
				for (size_t i = 0; i < g_queue.size(); i++) {
					long long dx = (long long)g_queue[i].first * 256 + 128 - px, dy = (long long)g_queue[i].second * 256 + 128 - py;
					long long d2 = dx * dx + dy * dy;
					if (d2 > 1600LL * 1600LL) { g_queue.erase(g_queue.begin() + i); i--; continue; }
					if (d2 < kDecoRange * kDecoRange) cand.push_back(g_queue[i]);
				}
				LeaveCriticalSection(&g_qcs);
				long long bestD = LLONG_MAX;
				for (const auto& c : cand) {
					long long dx = (long long)c.first * 256 + 128 - px, dy = (long long)c.second * 256 + 128 - py;
					if (dx * dx + dy * dy < bestD && ZoneSettled(gc, c.first, c.second)) { bestD = dx * dx + dy * dy; job = c; }
				}
				if (job.first != INT_MIN) {
					EnterCriticalSection(&g_qcs);
					for (size_t i = 0; i < g_queue.size(); i++) if (g_queue[i] == job) { g_queue.erase(g_queue.begin() + i); break; }
					LeaveCriticalSection(&g_qcs);
					g_job = ZoneJob(); g_job.zx = job.first; g_job.zy = job.second; g_job.active = true;
				}
			}
			if (g_job.active) {
				// Unterwegs weggelaufen oder Nachbarn entladen: anhalten und spaeter fortsetzen
				long long dx = (long long)g_job.zx * 256 + 128 - px, dy = (long long)g_job.zy * 256 + 128 - py;
				bool ok = dx * dx + dy * dy < (kDecoRange + 160) * (kDecoRange + 160) && ZoneSettled(gc, g_job.zx, g_job.zy);
				if (!ok) {
					// zurueck in die Warteschlange (wird spaeter von vorn bearbeitet; alles ist
					// deterministisch, doppelt gesetzte Bloecke sind dieselben)
					if (g_job.box.x0 <= g_job.box.x1) LabBridge::RemeshArea(gc, g_job.box.x0, g_job.box.y0, g_job.box.x1, g_job.box.y1);
					if (dx * dx + dy * dy <= 1600LL * 1600LL && LabBridge::ZoneAt(gc, g_job.zx, g_job.zy)) {
						EnterCriticalSection(&g_qcs); g_queue.push_back(std::make_pair(g_job.zx, g_job.zy)); LeaveCriticalSection(&g_qcs);
					}
					g_job.active = false;
				} else if (StepZone(gc, g_job, 3.0)) g_job.active = false;
			}

			BuildPendingSprites(gc);                                     // kleine Pflanzenmodelle (D3D-Puffer) bauen
			SpawnerTick(gc, px, py, here);
			DungeonTick(gc, px, py);
		}
	};
	static BiomeModule g_module;

	void RegisterWithFramework() {
		if (!g_qcsInit) { InitializeCriticalSection(&g_qcs); g_qcsInit = true; }
		if (!g_scsInit) { InitializeCriticalSection(&g_scs); g_scsInit = true; }
		AdventureFramework::RegisterMod(&g_module);
	}

	// ---- Stufen-Patch der Flussschleife (generateBiomeContent) -----------------------
	// Original: Spiegel = (int)h / 5 * 5 und q = (h - Spiegel) / 5  -> 5-Block-Stufen.
	// Patch:    Spiegel = (int)h          und q = (h - Spiegel)      -> 1-Block-Stufen.
	// Die Profil-Konstante 5.0 (Wassertiefe 5f, Bett) bleibt -- xmm4 wird weiter gebraucht.
	struct BytePatch { uint32_t addr; uint8_t orig[5]; int len; };
	static const BytePatch kRiverPatch[] = {
		{ 0x5E7F30, { 0xB8, 0x67, 0x66, 0x66, 0x66 }, 5 },   // mov eax, 0x66666667
		{ 0x5E7F35, { 0xF7, 0xE9 }, 2 },                       // imul ecx
		{ 0x5E7F3F, { 0xD1, 0xFA }, 2 },                       // sar edx, 1
		{ 0x5E7F41, { 0x8B, 0xC2 }, 2 },                       // mov eax, edx
		{ 0x5E7F43, { 0xC1, 0xE8, 0x1F }, 3 },                 // shr eax, 31
		{ 0x5E7F46, { 0x03, 0xC2 }, 2 },                       // add eax, edx
		{ 0x5E7F4B, { 0x8D, 0x0C, 0x80 }, 3 },                 // lea ecx, [eax+eax*4]  -> ecx bleibt (int)h
	};
	static const uint32_t kRiverDivAddr = 0x5E7F6F;           // divss xmm2, xmm4 (/5.0) -> 4 Byte NOP
	static const uint8_t  kRiverDivOrig[4] = { 0xF3, 0x0F, 0x5E, 0xD4 };
	static bool ReadCode(uint32_t a, uint8_t* out, int n) {
		__try { memcpy(out, (const void*)Re(a), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool WriteCode(uint32_t a, const uint8_t* src, int n) {
		uint8_t* p = (uint8_t*)Re(a); DWORD old = 0;
		if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
		memcpy(p, src, n);
		DWORD d2; VirtualProtect(p, n, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, n);
		return true;
	}
	static bool InstallRiverPatch() {
		uint8_t cur[8];
		for (const BytePatch& bp : kRiverPatch) if (!ReadCode(bp.addr, cur, bp.len) || memcmp(cur, bp.orig, bp.len) != 0) return false;
		if (!ReadCode(kRiverDivAddr, cur, 4) || memcmp(cur, kRiverDivOrig, 4) != 0) return false;
		const uint8_t nops[8] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
		for (const BytePatch& bp : kRiverPatch) WriteCode(bp.addr, nops, bp.len);
		const uint8_t nop4[4] = { 0x0F, 0x1F, 0x40, 0x00 };   // nop dword [eax+0]
		WriteCode(kRiverDivAddr, nop4, 4);
		return true;
	}

	// ---- Native Spawndichte (2026-10-16) -----------------------------------------------------------
	// WorldInfo_generateBiomeContent (0x5E4850) legt je Zone ein 3 x 3-Raster von Spawn-Kandidaten an (Zellen 0x55 =
	// 85 Bloecke, Versatz 24 + rand%10; 0x5EAF75..0x5EBBC7). Je Kandidat: 75 % weiter, Feuchte/Kaelte-Filter, kein
	// Spawn naeher als 20 Bloecke, Landpruefung, Dichtefeld (WorldInfo_rotateAndPlace >= 1) -> Spawn + Gruppe.
	// Mehr Tiere wie im Spiel selbst: Raster vergroessern (N x N, Schritt 256/N). Alles andere bleibt nativ
	// (Filter, Rassenwahl, Gruppen, Level). Stufe 1 = nativ (3x3), 2 = 4x4, 3 = 5x5, 4 = 6x6.
	struct GridPatch { uint32_t a; uint8_t orig; };
	static const GridPatch kStep[2] = { { 0x5EAFC2, 0x55 }, { 0x5EAFDC, 0x55 } };   // imul esi/edi, ..., 0x55
	static const GridPatch kCount[2] = { { 0x5EBBB0, 0x03 }, { 0x5EBBC6, 0x03 } };  // cmp eax, 3 (beide Schleifen)
	static int g_density = 1;
	static bool SpawnGridOk() {
		uint8_t b[2][3];
		if (!ReadCode(0x5EAFC0, b[0], 3) || !ReadCode(0x5EAFDA, b[1], 3)) return false;
		if (b[0][0] != 0x6B || b[0][1] != 0xF6 || b[1][0] != 0x6B || b[1][1] != 0xFF) return false;   // imul esi,esi / edi,edi
		uint8_t c[2][3];
		if (!ReadCode(0x5EBBAE, c[0], 3) || !ReadCode(0x5EBBC4, c[1], 3)) return false;
		return c[0][0] == 0x83 && c[0][1] == 0xF8 && c[1][0] == 0x83 && c[1][1] == 0xF8;            // cmp eax, imm8
	}
	bool SetSpawnDensity(int level) {
		level = (std::max)(1, (std::min)(4, level));
		if (Host::IsServer() && !Host::Has(0x5EAFC2)) return false;
		if (!SpawnGridOk()) { std::cout << "[Biom] Spawndichte: Stelle 0x5EAFC0 weicht ab -- bleibt nativ." << std::endl; return false; }
		const uint8_t n = (uint8_t)(level + 2), step = (uint8_t)(256 / n);
		for (const GridPatch& p : kStep) WriteCode(p.a, &step, 1);
		for (const GridPatch& p : kCount) WriteCode(p.a, &n, 1);
		g_density = level;
		std::cout << "[Biom] Spawndichte Stufe " << level << ": " << (int)n << "x" << (int)n << " Kandidaten je Zone (nativ 3x3), Schritt " << (int)step << " Bloecke." << std::endl;
		return true;
	}
	int SpawnDensity() { return g_density; }
	static int ReadDensitySetting() {
		std::istringstream f(ModDb::Txt("AdventureMod_schalter.cfg")); std::string line; int v = 2;
		while (std::getline(f, line)) if (line.rfind("tierdichte=", 0) == 0) v = atoi(line.c_str() + 11);
		return v;
	}
	void Install() {
		if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
		if (!g_qcsInit) { InitializeCriticalSection(&g_qcs); g_qcsInit = true; }
		if (!g_scsInit) { InitializeCriticalSection(&g_scs); g_scsInit = true; }
		std::ostringstream rep; int ok = 0, bad = 0;
		for (const Site& s : kSites) if (!ProloguesMatch(s.addr, s.pro)) { bad++; rep << s.name << " passt NICHT; "; }
		if (bad) {
			std::cout << "[Biom] Cube.exe weicht ab -- Biome AUS. " << rep.str() << std::endl;
			Guard g; g_hookReport = rep.str(); g_hooksOk = false;
			return;
		}
		g_perlin = (Perlin_t)Re(A_PERLIN);
		struct H { uint32_t a; void* det; void** orig; const char* n; } hs[] = {
			{ A_CELL,    (void*)&Hook_Cell,    (void**)&o_Cell,    "Klimazelle" },
			{ A_REGION,  (void*)&Hook_Region,  (void**)&o_Region,  "Region" },
			{ A_SURFACE, (void*)&Hook_Surface, (void**)&o_Surface, "Boden" },
			{ A_ROCK,    (void*)&Hook_Rock,    (void**)&o_Rock,    "Fels" },
			{ A_RACE,    (void*)&Hook_Race,    (void**)&o_Race,    "Rasse" },
			{ A_HEIGHT,  (void*)&Hook_Height,  (void**)&o_Height,  "Hoehe" },
			{ A_MOUNT,   (void*)&Hook_Mount,   (void**)&o_Mount,   "Berge" },
			{ A_TREE,    (void*)&Hook_Tree,    (void**)&o_Tree,    "Baum" },
			{ A_RIVER,   (void*)&Hook_River,   (void**)&o_River,   "Fluss" },
		};
		for (const H& h : hs) {
			MH_STATUS st = MH_CreateHook((LPVOID)Re(h.a), h.det, h.orig);
			if (st == MH_OK) ok++; else { bad++; rep << h.n << " MinHook " << (int)st << "; "; }
		}
		// Stufen-Patch nur, wenn der Hoehen-Hook steht (er liefert der Flussschleife floor(h)+0.5).
		if (!g_tcsInit) { InitializeCriticalSection(&g_tcs); g_tcsInit = true; }
		// Eigene Gegenstaende: Modell und Name (Prologe aus der vorliegenden Cube.exe)
		{
			static const uint8_t proIcon[6] = { 0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1 };
			static const uint8_t proName[6] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
			bool okI = ProloguesMatch(A_ITEM_ICON, proIcon) && MH_CreateHook((LPVOID)Re(A_ITEM_ICON), (LPVOID)&Hook_ItemIcon, (LPVOID*)&o_ItemIcon) == MH_OK;
			bool okN = ProloguesMatch(A_OBJ_NAME, proName) && MH_CreateHook((LPVOID)Re(A_OBJ_NAME), (LPVOID)&Hook_ObjName, (LPVOID*)&o_ObjName) == MH_OK;
			std::cout << "[Zonen-Modelle] Eigene Gegenstaende: Modell-Hook " << (okI ? "ok" : "FEHLT") << ", Namen-Hook " << (okN ? "ok" : "FEHLT") << "." << std::endl;
		}
		g_zoneObjOn = ModSwitches::Get("zonenobjekte", true);
		g_smallOn = ModSwitches::Get("kleine_blumen", true);
		g_splitOn = ModSwitches::Get("zonen_teiler", true);
		City::SetFences(ModSwitches::Get("stadt_zaeune", true));
		City::SetResidents(ModSwitches::Get("stadt_bewohner", true));
		City::SetTreeProvider(&CityTreesFor);
		City::SetAnimalProvider(&CityAnimalsFor);
		City::SetPlantProvider(&CityPlantsFor);
		g_treeAnchor = InstallTreeAnchor();          // Zonen-Modelle laedt ReloadDefinitions (braucht die Biome)
		std::cout << (g_treeAnchor ? "[Biom] Hangbaeume: in der Zonenerzeugung verankert (Aufruf 0x5EDE84)."
		                           : "[Biom] Hangbaeume: Aufrufstelle 0x5EDE84 weicht ab -- keine Hangbaeume.") << std::endl;
		// 1-Block-Stufen an (Stand vor der Hangpruefung); die Hangpruefung selbst ist aus (kRiverCrossCheck).
		if (kRiverTweaks && o_Height && InstallRiverPatch()) { g_riverPatch = true; std::cout << "[Biom] Fluesse: Wasserspiegel folgt dem Gelaende blockweise (Stufen-Patch 0x5E7F30 aktiv), Hangpruefung " << (kRiverCrossCheck ? "an." : "aus.") << std::endl; }
		else std::cout << "[Biom] Fluesse: Stufen-Patch NICHT gesetzt -- 5er-Stufen wie im Spiel." << std::endl;
		ReloadDefinitions();
		SetSpawnDensity(ReadDensitySetting());
		Wildlife::SetSurfaceFn(&ZoneSurfaceRGB);                                  // mehr native Tiere (Schalter tierdichte=1..4)
		MountainVillage::SetVillageFilter(&VillageFilterFn);
		{ Guard g; g_hooksOk = bad == 0; g_hookReport = bad ? rep.str() : "alle 9 Hooks gesetzt"; }
		std::cout << "[Biom] Biome bereit: " << ok << "/9 Hooks" << (bad ? (" -- " + rep.str()) : std::string()) << std::endl;
	}

	Status GetStatus() {
		Status s;
		if (g_csInit) { Guard g; s = g_status; s.hooksOk = g_hooksOk; s.hookReport = g_hookReport; }
		s.cellsAssigned = c_cells; s.sitesRemoved = c_sitesNow; s.racesReplaced = c_races; s.treesRemoved = c_trees; s.minesCarved = c_mines;
		s.zonesDecorated = c_zones; s.plantsPlaced = c_plants + c_bigTrees; s.dungeonsPlaced = c_dungeons; s.mobsSpawned = c_mobs;
		return s;
	}

	// =====================================================================
	// 9. LAB-Auftraege (Spielthread)
	// =====================================================================
	static const char* Compass(int dx, int dy) {
		double a = std::atan2((double)dy, (double)dx) * 180.0 / 3.14159265;   // +X = Ost, +Y = Nord
		const char* n[8] = { "Ost", "Nordost", "Nord", "Nordwest", "West", "Suedwest", "Sued", "Suedost" };
		int i = (int)std::floor((a + 22.5 + 360.0) / 45.0) % 8;
		return n[i];
	}
	static bool CellCenter(const uint8_t* world, int cx, int cy, int* x, int* y) {
		__try {
			const uint8_t* c = ((const uint8_t* const*)(world + W_CELLGRID))[cx * 1024 + cy];
			if (!c) return false;
			*x = *(const int*)c; *y = *(const int*)(c + 4); return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// =====================================================================
	// 9b. Rechnen ohne geladene Welt (fuer den Seed-Rechner SeedScout, Suche, Anzeige)
	// =====================================================================
	// SeedScout::region_site_calc bildet den Zellerzeuger 0x5D7A70 nach (Lage, Klima, Hoehe).
	// Darauf dieselbe Entscheidung wie im Hook -> Biom jeder Zelle, auch unerforscht.
	struct OffCell { int x, y, id; };
	namespace { CRITICAL_SECTION g_ocs; bool g_ocsInit = false; std::map<uint64_t, OffCell> g_off; LONG g_offVer = -1; }
	static OffCell OfflineCell(uint32_t base, int cx, int cy) {
		if (!g_ocsInit) { InitializeCriticalSection(&g_ocs); g_ocsInit = true; }
		uint64_t key = ((uint64_t)base << 22) | ((uint64_t)(cx & 0x7FF) << 11) | (uint64_t)(cy & 0x7FF);
		EnterCriticalSection(&g_ocs);
		if (g_offVer != g_modVersion) { g_off.clear(); g_offVer = g_modVersion; }
		auto it = g_off.find(key);
		if (it != g_off.end()) { OffCell r = it->second; LeaveCriticalSection(&g_ocs); return r; }
		LeaveCriticalSection(&g_ocs);
		SeedScout::Site st = SeedScout::region_site_calc(base, cx, cy, false);   // roh, ohne Biom-Anpassung
		float T = st.humid, H = st.temp; bool lava = st.wet != 0;                 // humid = +0x10, temp = +0x0C
		OffCell c; c.x = st.x; c.y = st.z;
		c.id = DecideCell(g_hs, base, cx, cy, st.elev, cx == 0x200 && cy == 0x200, &T, &H, &lava);
		EnterCriticalSection(&g_ocs); if (g_off.size() > 200000) g_off.clear(); g_off[key] = c; LeaveCriticalSection(&g_ocs);
		return c;
	}
	// Wie DominantRaw, aber mit berechneten statt geladenen Zellen.
	static Dom OfflineDominant(uint32_t base, int x, int y) {
		Dom r = { 0, 0.0f };
		int x0 = Div16k(x - 0x4000), x1 = Div16k(x + 0x4000), y0 = Div16k(y - 0x4000), y1 = Div16k(y + 0x4000);
		if (x0 < 0 || y0 < 0 || x1 > 1023 || y1 > 1023 || x1 - x0 > 2 || y1 - y0 > 2) return r;
		OffCell cells[9]; int n = 0; bool any = false;
		for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) { cells[n] = OfflineCell(base, cx, cy); if (cells[n].id) any = true; n++; }
		if (!any) return r;
		int xj, yj; SeedScout::_warp(x, y, xj, yj);                                  // gleiche Verschiebung wie 0x5EF040
		int d2[9]; int best = INT_MAX;
		for (int i = 0; i < n; i++) {
			float fx = (float)((int64_t)(cells[i].x - xj) * 65536); fx = fx * 1.5258789e-05f;
			float fy = (float)((int64_t)(cells[i].y - yj) * 65536); fy = fy * 1.5258789e-05f;
			d2[i] = (int)(fy * fy + fx * fx); if (d2[i] < best) best = d2[i];
		}
		float total = 0, sum[kMaxBiomes + 1] = {};
		for (int i = 0; i < n; i++) {
			float f = (float)(d2[i] - best) * 5e-07f; if (f > 1.0f) f = 1.0f;
			float w = 1.0f - f; total += w;
			if (cells[i].id > 0 && cells[i].id <= kMaxBiomes) sum[cells[i].id] += w;
		}
		if (total <= 0.0f) return r;
		int bi = 0; for (int i = 1; i <= kMaxBiomes; i++) if (sum[i] > sum[bi]) bi = i;
		if (bi > 0 && sum[bi] > 0.0f) {
			r.id = bi; r.w = sum[bi] / total;
			int bd = INT_MAX;
			for (int i = 0; i < n; i++) if (cells[i].id == bi && d2[i] < bd) { bd = d2[i]; r.px = cells[i].x; r.py = cells[i].y; }
		}
		return r;
	}

	static bool Nearest(cube::GameController* gc, int index, int px, int py, int* bx, int* by) {
		int id = DefIdByListIndex(index); if (!id) return false;
		uint32_t base = SeedOf(gc);
		int pcx = px / 16384, pcy = py / 16384;
		long long best = LLONG_MAX; bool found = false;
		// Berechnet statt nur angelegt: findet auch Biome in noch nie besuchten Gebieten.
		for (int cx = std::max(0, pcx - 24); cx <= std::min(1023, pcx + 24); cx++)
			for (int cy = std::max(0, pcy - 24); cy <= std::min(1023, pcy + 24); cy++) {
				OffCell c = OfflineCell(base, cx, cy);
				if (c.id != id) continue;
				long long dx = c.x - px, dy = c.y - py, d2 = dx * dx + dy * dy;
				if (d2 < best) { best = d2; *bx = c.x; *by = c.y; found = true; }
			}
		return found;
	}

	// Naechste ELFENSTADT (2026-10-29): berechnet aus dem Seed (SeedScout::region_site_calc -> OfflineCell), also auch in
	// nie besuchten Gebieten. Bedingungen wie CitySites (Zonenerzeugung): Biom mit freier Ebene (plainOn, plainRadius >= 200)
	// und Stadt (city > 0), Art per City::ResolveKind == ELF; Stadtmitte = Zellmitte (= Mutterbaum mit dem Koenig).
	// Suche in Ringen um die Zelle des Spielers (Elfenbiome koennen selten sein, z. B. 1 %): ist im Ring r eine Stadt
	// gefunden, werden noch die Ringe bis 1.5 r + 2 geprueft (ein spaeterer Ring kann naeher liegen), dann Schluss.
	// Jeder Thread (OfflineCell hat eine eigene Sperre).
	static bool ElfCityCell(HookSet* hs, uint32_t base, int x, int y, int* cx, int* cy) {
		if (x < 0 || y < 0 || x > 1023 || y > 1023) return false;
		OffCell c = OfflineCell(base, x, y);
		if (c.id < 1 || c.id > kMaxBiomes) return false;
		const HookDef& b = hs->d[c.id - 1];
		if (!b.used || !b.enabled || !b.plainOn || b.city <= 0 || b.plainRadius < 200.0f) return false;
		if (City::ResolveKind(b.city, base, c.x, c.y) != City::ELF) return false;
		*cx = c.x; *cy = c.y; return true;
	}
	bool NearestElfCityFrom(uint32_t base, int px, int py, int maxRing, int* cx, int* cy) {
		HookSet* hs = g_hs; if (!g_on || !hs) return false;
		const int pcx = px / 16384, pcy = py / 16384;
		long long best = LLONG_MAX; int stopAt = maxRing;
		for (int r = 0; r <= stopAt; r++) {
			for (int k = -r; k <= r; k++) {
				const int pts[4][2] = { { pcx + k, pcy - r }, { pcx + k, pcy + r }, { pcx - r, pcy + k }, { pcx + r, pcy + k } };
				for (int q = 0; q < 4; q++) {
					if (r == 0 && q > 0) break;
					if ((q >= 2) && (k == -r || k == r)) continue;                    // Ecken nur einmal
					int x, y; if (!ElfCityCell(hs, base, pts[q][0], pts[q][1], &x, &y)) continue;
					long long dx = x - px, dy = y - py, d2 = dx * dx + dy * dy;
					if (d2 < best) { best = d2; *cx = x; *cy = y; }
				}
			}
			if (best != LLONG_MAX && stopAt == maxRing) stopAt = (std::min)(maxRing, r + r / 2 + 2);
		}
		return best != LLONG_MAX;
	}
	uint32_t WorldBase(cube::GameController* gc) { return SeedOf(gc); }
	bool NearestElfCity(cube::GameController* gc, int px, int py, int* cx, int* cy) {
		return NearestElfCityFrom(SeedOf(gc), px, py, 32, cx, cy);
	}
	std::string TrackingText(cube::GameController* gc) {
		static DWORD last = 0; static std::string cache;
		DWORD now = GetTickCount();
		if (now - last < 2000 && last) return cache;
		last = now;
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return cache = "";
		uint32_t base = SeedOf(gc);
		std::vector<BiomeDef> defs = GetDefinitions();
		std::ostringstream o;
		Dom here = OfflineDominant(base, px, py);
		BiomeDef hd;
		if (here.id && DefById(here.id, hd)) { char b[96]; sprintf_s(b, "Biom hier: %s (Gewicht %.2f)\n", hd.name.c_str(), here.w); o << b; }
		bool header = false;
		for (size_t k = 0; k < defs.size(); k++) {
			if (!defs[k].enabled) continue;
			int bx, by;
			if (!Nearest(gc, (int)k, px, py, &bx, &by)) continue;
			if (!header) { o << "Eigene Biome (berechnet, Mitte der Klimazelle):\n"; header = true; }
			int dx = bx - px, dy = by - py;
			char b[160]; sprintf_s(b, "  %-15s (%d, %d)  %.0f Bl  %s\n", defs[k].name.c_str(), bx, by, std::sqrt((double)dx * dx + (double)dy * dy), Compass(dx, dy));
			o << b;
		}
		return cache = o.str();
	}
	std::string NativeSitesNear(cube::GameController* gc, int radius) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		uint8_t* world = (uint8_t*)gc + GC_WORLD;
		struct E { long long d2; int kind, x, y; std::string state; };
		std::vector<E> out; int regions = 0;
		int rx0 = Div16k(px - radius), rx1 = Div16k(px + radius), ry0 = Div16k(py - radius), ry1 = Div16k(py + radius);
		for (int rx = rx0; rx <= rx1; rx++) for (int ry = ry0; ry <= ry1; ry++) {
			if (rx < 0 || ry < 0 || rx > 1023 || ry > 1023) continue;
			bool any = false;
			for (int i = 0; i < 64; i++) {
				SiteRaw sr; if (!ReadSiteRaw(world, rx, ry, i, &sr)) break;
				any = true;
				int orig = sr.kind; bool off = false;
				if (g_scsInit) {
					EnterCriticalSection(&g_scs);
					auto it = g_siteOrig.find(SiteKey(world, rx, ry, i));
					if (it != g_siteOrig.end()) { orig = it->second; off = true; }
					LeaveCriticalSection(&g_scs);
				}
				if (orig <= 0 || orig > 15) continue;
				long long dx = sr.x - px, dy = sr.y - py, d2 = dx * dx + dy * dy;
				if (d2 > (long long)radius * radius) continue;
				std::string st = "aktiv";
				if (off) {
					Dom d = DominantRaw(world, sr.x, sr.y);
					std::string bn = "?";
					if (d.id && g_csInit) { Guard g; if (g_defUsed[d.id - 1]) bn = g_def[d.id - 1].name; }
					HookSet* hs = g_hs;
					bool plain = hs && d.id && InPlain(hs->d[d.id - 1].plainOn, hs->d[d.id - 1].plainRadius, hs->d[d.id - 1].plainRim, d, sr.x, sr.y, 160.0f);
					st = plain ? "ABGESCHALTET (freie Ebene/Stadt von '" + bn + "')" : "ABGESCHALTET (Biom '" + bn + "', Haken unter 'Native Orte' fehlt)";
				}
				out.push_back({ d2, orig, sr.x, sr.y, st });
			}
			if (any) regions++;
		}
		std::sort(out.begin(), out.end(), [](const E& a, const E& b) { return a.d2 < b.d2; });
		std::ostringstream o;
		o << "Native Orte im Umkreis von " << radius << " Bloecken (" << regions << " Regionen geladen):\n";
		int caves = 0;
		for (const E& e : out) {
			char b[256];
			sprintf_s(b, "  %2d %-34s %6.0f Bl %-3s (%d, %d)  %s\n", e.kind, SiteKindName(e.kind), std::sqrt((double)e.d2), Compass(e.x - px, e.y - py), e.x, e.y, e.state.c_str());
			o << b;
			if (e.kind == 9) caves++;
		}
		if (out.empty()) o << "  keine (Regionen evtl. noch nicht angelegt)\n";
		o << "Hoehlen (Typ 9) darunter: " << caves << ".";
		return o.str();
	}
	// ---- F9-Anzeige (Tracking.cpp) ---------------------------------------------------------------------------
	// Natives Klima an der Spielerposition: Temperatur 0x5C4DD0 / Feuchte 0x5C4800 / Lava-Gewicht 0x5EF040 /
	// Ozean-Gewicht 0x5F0720, je __thiscall(World*, x, y) -> float (ST0), ret 8 (RE_Biome.md 1.3/1.4).
	typedef float(__fastcall* Climate_t)(void* world, void* edx, int x, int y);
	static bool ClimateRaw(void* world, int x, int y, float* T, float* H, float* lava, float* sea) {
		__try {
			*T = ((Climate_t)Re(0x5C4DD0))(world, nullptr, x, y);
			*H = ((Climate_t)Re(0x5C4800))(world, nullptr, x, y);
			*lava = ((Climate_t)Re(0x5EF040))(world, nullptr, x, y);
			*sea = ((Climate_t)Re(0x5F0720))(world, nullptr, x, y);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	int NativeClassAt(const void* world, int x, int y) {
		float T = 0, H = 0, lava = 0, sea = 0;
		if (!world || !ClimateRaw((void*)world, x, y, &T, &H, &lava, &sea) || sea >= 0.5f) return 0;
		return BiomeGen::NativeClassOf(T, H, lava >= 0.5f);
	}
	std::string BiomeNameAt(cube::GameController* gc, int x, int y) {
		if (!gc) return "?";
		Dom here = OfflineDominant(SeedOf(gc), x, y);
		BiomeDef hd;
		if (g_on && here.id && here.w >= 0.5f && DefById(here.id, hd) && hd.nativeClass == 0) return hd.name + " (eigenes Biom)";
		float T = 0, H = 0, lava = 0, sea = 0;
		if (!ClimateRaw((uint8_t*)gc + GC_WORLD, x, y, &T, &H, &lava, &sea)) return "?";
		// Schwellen wie das Spiel: Bodenfarbe 0x5C56E0 (Schnee T < 0,3; Sand T > 0,75 und F < 0,2),
		// Kreaturentabelle 0x5F56C0 (Lava ab Gewicht 0,1, kalt T < 0,2, Wueste T >= 0,8 und F < 0,2)
		const char* n;
		if (sea >= 0.5f) n = "Ozean";
		else { int k = BiomeGen::NativeClassOf(T, H, lava >= 0.5f); n = k > 0 ? BiomeGen::NativeClassName(k) : "Heisses Grasland"; if (k == 1) n = "Grasland / Wald"; }
		std::string edited = (g_on && here.id && here.w >= 0.5f && DefById(here.id, hd) && hd.nativeClass > 0) ? ", bearbeitet: " + hd.name : "";
		char b[160]; sprintf_s(b, "%s (Temp %.2f, Feuchte %.2f%s)", n, T, H, edited.c_str());
		return b;
	}
	int NativeSites(cube::GameController* gc, int radius, std::vector<SiteInfo>& out) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return 0;
		uint8_t* world = (uint8_t*)gc + GC_WORLD;
		int rx0 = Div16k(px - radius), rx1 = Div16k(px + radius), ry0 = Div16k(py - radius), ry1 = Div16k(py + radius);
		for (int rx = rx0; rx <= rx1; rx++) for (int ry = ry0; ry <= ry1; ry++) {
			if (rx < 0 || ry < 0 || rx > 1023 || ry > 1023) continue;
			for (int i = 0; i < 64; i++) {
				SiteRaw sr; if (!ReadSiteRaw(world, rx, ry, i, &sr)) break;
				int orig = sr.kind; bool off = false;
				if (g_scsInit) {
					EnterCriticalSection(&g_scs);
					auto it = g_siteOrig.find(SiteKey(world, rx, ry, i));
					if (it != g_siteOrig.end()) { orig = it->second; off = true; }
					LeaveCriticalSection(&g_scs);
				}
				if (orig <= 0 || orig > 15 || off) continue;                    // abgeschaltete Orte nicht anzeigen
				long long dx = sr.x - px, dy = sr.y - py;
				if (dx * dx + dy * dy > (long long)radius * radius) continue;
				out.push_back(SiteInfo{ orig, sr.x, sr.y });
			}
		}
		return (int)out.size();
	}
	int OwnBiomeTargets(cube::GameController* gc, std::vector<SiteInfo>& out, std::vector<std::string>& names) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return 0;
		std::vector<BiomeDef> defs = GetDefinitions();
		for (size_t k = 0; k < defs.size(); k++) {
			if (!defs[k].enabled || defs[k].nativeClass > 0) continue;
			int bx, by;
			if (!Nearest(gc, (int)k, px, py, &bx, &by)) continue;
			out.push_back(SiteInfo{ (int)k, bx, by }); names.push_back(defs[k].name);
		}
		return (int)out.size();
	}
	std::string FindNearest(cube::GameController* gc, int index) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		int bx, by;
		if (!Nearest(gc, index, px, py, &bx, &by))
			return "Kein Gebiet dieses Bioms im Umkreis von 24 Klimazellen (~390 000 Bloecke) berechnet. Anteil erhoehen, Klima-Auswahl pruefen oder 'Biom HIER erzwingen'.";
		std::ostringstream o;
		int dx = bx - px, dy = by - py;
		o << "Naechste Biom-Mitte: Block (" << bx << ", " << by << "), " << (int)std::sqrt((double)dx * dx + (double)dy * dy)
		  << " Bloecke Richtung " << Compass(dx, dy) << ".";
		return o.str();
	}
	// Zelle, deren (verschobene) Mitte dem Block am naechsten liegt -- dieselbe
	// Voronoi-Regel wie DominantRaw, also genau die Zelle, die hier das Biom bestimmt.
	static bool NearestCellRaw(const uint8_t* world, int x, int y, int* ocx, int* ocy, uint8_t** ocell) {
		__try {
			if (!g_perlin) return false;
			int x0 = Div16k(x - 0x4000), x1 = Div16k(x + 0x4000), y0 = Div16k(y - 0x4000), y1 = Div16k(y + 0x4000);
			if (x0 < 0 || y0 < 0 || x1 > 1023 || y1 > 1023) return false;
			float jx = (float)g_perlin((double)y * 0.0005, 3423.0); jx = jx * 3.0f; jx = jx * 256.0f;
			float jy = (float)g_perlin((double)x * 0.0005, 23421.0); jy = jy * 3.0f; jy = jy * 256.0f;
			int xj = (int)((float)x + jx), yj = (int)((float)y + jy);
			uint8_t* const* grid = (uint8_t* const*)(world + W_CELLGRID);
			long long best = LLONG_MAX; bool found = false;
			for (int cx = x0; cx <= x1; cx++) for (int cy = y0; cy <= y1; cy++) {
				uint8_t* c = grid[cx * 1024 + cy]; if (!c) continue;
				long long dx = *(int*)c - xj, dy = *(int*)(c + 4) - yj, d2 = dx * dx + dy * dy;
				if (d2 < best) { best = d2; *ocx = cx; *ocy = cy; *ocell = c; found = true; }
			}
			return found;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void ApplyClimateRaw(uint8_t* c, bool set, float t, float hm) {
		__try { if (set) { *(float*)(c + 0x10) = t; *(float*)(c + 0x0C) = hm; } c[8] = 0; }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static bool IsLandRaw(const uint8_t* c) {
		__try { return *(const int*)(c + 0x18) >= 1; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	std::string ForceHere(cube::GameController* gc, int index, bool clear) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		uint8_t* world = (uint8_t*)gc + GC_WORLD;
		int cx, cy; uint8_t* cell = nullptr;
		if (!NearestCellRaw(world, px, py, &cx, &cy, &cell)) return "Klimazelle hier nicht gefunden.";
		uint32_t seed = SeedOf(gc);
		std::ostringstream o;
		if (clear) {
			size_t n0, n1;
			{
				Guard g;
				n0 = g_forcedList.size();
				for (size_t i = 0; i < g_forcedList.size(); )
					if (g_forcedList[i].seed == seed && g_forcedList[i].cx == cx && g_forcedList[i].cy == cy) g_forcedList.erase(g_forcedList.begin() + i); else i++;
				n1 = g_forcedList.size();
				SaveForced(); Publish();
			}
			g_cell[cx * 1024 + cy] = 0;
			InterlockedExchange(&g_refilter, 1);
			o << "Zelle (" << cx << "," << cy << ") " << (n0 != n1 ? "wieder frei." : "war nicht erzwungen.")
			  << " Gilt fuer neu erzeugte Zonen; beim naechsten Laden der Welt wird sie neu ausgewuerfelt.";
			return o.str();
		}
		int id = DefIdByListIndex(index);
		if (!id) return "Biom nicht geladen.";
		BiomeDef d;
		if (!DefById(id, d)) return "Biom ist AUS -- erst 'aktiv' anhaken und speichern.";
		if (!IsLandRaw(cell)) return "Diese Zelle ist Ozean -- Biome gibt es nur an Land.";
		{
			Guard g;
			for (size_t i = 0; i < g_forcedList.size(); )
				if (g_forcedList[i].seed == seed && g_forcedList[i].cx == cx && g_forcedList[i].cy == cy) g_forcedList.erase(g_forcedList.begin() + i); else i++;
			ForcedEntry e; e.seed = seed; e.cx = cx; e.cy = cy; e.file = g_file[id - 1];
			g_forcedList.push_back(e);
			SaveForced(); Publish();
		}
		ApplyClimateRaw(cell, d.setClimate, d.temp, d.hum);
		g_cell[cx * 1024 + cy] = (uint8_t)id;
		InterlockedExchange(&g_refilter, 1);
		int ccx = 0, ccy = 0; CellCenter(world, cx, cy, &ccx, &ccy);
		o << "Biom '" << d.name << "' fuer Klimazelle (" << cx << "," << cy << ") erzwungen, Mitte bei Block (" << ccx << ", " << ccy
		  << "). Bereits geladene Zonen bleiben, bis sie neu erzeugt werden: ~1000 Bloecke weggehen und zurueck, "
		     "oder Welt verlassen und neu betreten (gespeichert in Strukturen\\Biome\\_erzwungen.txt).";
		return o.str();
	}

	static bool SetPlayerPos(cube::GameController* gc, int64_t x, int64_t y, int64_t z) {
		__try { gc->local_player->entity_data.position.X = x; gc->local_player->entity_data.position.Y = y; gc->local_player->entity_data.position.Z = z; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	std::string TeleportToNearest(cube::GameController* gc, int index) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		int bx, by; if (!Nearest(gc, index, px, py, &bx, &by)) return "Keine Biomzelle bekannt.";
		const int64_t U = 65536;
		int z = std::max(pz, 300);
		if (!SetPlayerPos(gc, (int64_t)bx * U, (int64_t)by * U, (int64_t)z * U)) return "Teleport fehlgeschlagen (SEH).";
		std::ostringstream o; o << "Teleport (UNGETESTET) nach Block (" << bx << ", " << by << ", " << z << ") -- gleiten/fallen lassen.";
		return o.str();
	}
	std::string PlacePlantHere(cube::GameController* gc, const BiomeDef& def, int kind, uint32_t seed) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		int x = px + 40, y = py;                                  // Testplatz 40 Bloecke versetzt
		Lab::Structure s = kind >= 0 ? MakePlant(def, kind, seed) : Lab::Structure();
		if (!s.Valid()) return "keine Pflanze";
		int z; uint8_t t;
		cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
		if (!FindSurface(gc, zone, x, y, &z, &t, false)) z = pz;
		int ox = x - s.sx / 2, oy = y - s.sy / 2;
		int n = LabBridge::StampStructure(gc, s, ox, oy, z + 1, 0, 0, INT_MIN, INT_MIN);
		LabBridge::RemeshArea(gc, ox, oy, ox + s.sx, oy + s.sy);
		std::ostringstream o; o << PlantKindName(kind) << " " << s.sx << "x" << s.sy << "x" << s.sz << " bei (" << ox << "," << oy << "," << z + 1 << "), Zonen " << n;
		return o.str();
	}
	std::string PlaceDungeonHere(cube::GameController* gc, const BiomeDef& def, uint32_t seed) {
		int px, py, pz; if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		DungeonPlan p = MakeDungeon(def, seed);
		int ox = px + 40, oy = py - p.s.sy / 2;                     // 40 Bloecke versetzt
		int zmax = INT_MIN;
		for (int yy = 0; yy < p.s.sy; yy += 4) for (int xx = 0; xx < p.s.sx; xx += 4) {
			int z; uint8_t t;
			cube::Zone* zone = LabBridge::ZoneAt(gc, (ox + xx) >> 8, (oy + yy) >> 8);
			if (FindSurface(gc, zone, ox + xx, oy + yy, &z, &t, false)) zmax = std::max(zmax, z);
		}
		if (zmax == INT_MIN) zmax = pz - 1;
		int n = LabBridge::StampStructure(gc, p.s, ox, oy, zmax + 1, 0, 1, INT_MIN, INT_MIN);
		LabBridge::RemeshArea(gc, ox, oy, ox + p.s.sx, oy + p.s.sy);
		std::ostringstream o; o << "Dungeon " << p.s.sx << "x" << p.s.sy << "x" << p.s.sz << " (" << p.rooms.size() << " Raeume) bei ("
		  << ox << "," << oy << "," << zmax + 1 << "), Zonen " << n << ". Eingang Suedseite bei x=" << ox + p.entranceX << ".";
		return o.str();
	}

	// ---- fuer SeedScout (siehe Ende der Datei) ----
	long ModVersionInternal() { return g_modVersion; }
	void AdjustSiteInternal(uint32_t base, int cx, int cy, int elev, float* t10, float* hC, int* wet) {
		bool lava = *wet != 0;
		int id = DecideCell(g_hs, base, cx, cy, elev, cx == 0x200 && cy == 0x200, t10, hC, &lava);
		if (id) *wet = 0;
	}
	// =====================================================================
	// Zonen-Modelle: Laden, Status, Test
	// =====================================================================
	bool LoadZoneModelCub(const std::string& cub, Lab::Structure& out, std::string& err) {
		if (cub.empty()) { err = "kein Modell (.cub) eingetragen"; return false; }
		std::string name = cub;
		if (name.size() < 4 || _stricmp(name.c_str() + name.size() - 4, ".cub") != 0) name += ".cub";
		bool bare = name.find('\\') == std::string::npos && name.find('/') == std::string::npos;
		std::vector<std::string> tries;
		if (bare) { tries.push_back(ZoneModelsDir() + "\\" + name); tries.push_back(Lab::StructuresDir() + "\\" + name); }
		else tries.push_back(name);
		for (const std::string& p : tries) {
			if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
			if (!Lab::LoadCub(p, out, err)) return false;
			if (out.sx > 250 || out.sy > 250) { err = "Modell zu gross (hoechstens 250 x 250, eine Zone)"; out = Lab::Structure(); return false; }
			return true;
		}
		err = "Datei nicht gefunden: " + name;
		return false;
	}
	void ReloadZoneModels() {
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(ZoneModelsDir());
		std::vector<ZModelRT*>* set = new std::vector<ZModelRT*>();
		std::vector<ZModelRT*>* old = g_zm;
		std::vector<std::string> usedKeys;
		int ok = 0;
		for (const std::string& f : ListZoneModelFiles()) {
			ZModelRT* m = new ZModelRT();
			std::string err;
			m->file = f;
			if (old) for (ZModelRT* o : *old) if (o->file == f) m->placed = o->placed;   // Zaehler uebernehmen
			if (!LoadZoneModel(ZoneModelsDir() + "\\" + f, m->def, err)) { m->note = err; set->push_back(m); continue; }
			if (!LoadZoneModelCub(m->def.cub, m->s, err)) m->note = err;
			if (m->def.kind == ZM_PICKUP && m->s.Valid()) {
				if (m->def.itemId >= kPickupFirstId && m->def.itemId < kPickupFirstId + kPickupCount) {
					char kb[64]; sprintf_s(kb, "|%dx%dx%d|%u", m->s.sx, m->s.sy, m->s.sz, (unsigned)m->s.CountSolid());
					ItemRegister(m->def.itemId, f + "|" + m->def.cub + kb, m->s, m->def.itemName);
				} else m->note = "keine Gegenstands-ID (im LAB speichern)";
			}
			if (m->def.kind == ZM_FLOWER && m->def.smallScale > 0.0f && m->s.Valid()) {
				char kb[64]; sprintf_s(kb, "|%dx%dx%d|%u", m->s.sx, m->s.sy, m->s.sz, (unsigned)m->s.CountSolid());
				std::string key = f + "|" + m->def.cub + kb, n2;
				if (m->def.enabled) { m->vegSlot = VegSlotFor(key, m->s, n2); usedKeys.push_back(key); }
				if (!n2.empty()) m->note = n2;
			}
			uint32_t fh = 2166136261u; for (char c : f) fh = (fh ^ (uint8_t)tolower((uint8_t)c)) * 16777619u;
			m->salt = Hash(0x2A0D3u, fh);
			const std::string& a = m->def.area;
			if (a == "vanilla") m->biomeId = -1;
			else if (a.compare(0, 5, "biom:") == 0) {
				std::string bf = a.substr(5); m->biomeId = -2;
				if (g_csInit) { Guard g; for (int i = 0; i < kMaxBiomes; i++) if (g_defUsed[i] && _stricmp(g_file[i].c_str(), bf.c_str()) == 0) m->biomeId = i + 1; }
				if (m->biomeId == -2 && m->note.empty()) m->note = "Biom '" + bf + "' nicht gefunden";
			} else m->biomeId = 0;
			if (g_csInit) {                                             // Biom-Listen (LAB: Baeume & Pflanzen / Sammelbare)
				Guard g;
				for (int i = 0; i < kMaxBiomes; i++) {
					if (!g_defUsed[i]) continue;
					for (const std::string& n : g_def[i].zoneModelsOn)  if (_stricmp(n.c_str(), f.c_str()) == 0) m->biomeOn  |= 1u << (i + 1);
					for (const std::string& n : g_def[i].zoneModelsOff) if (_stricmp(n.c_str(), f.c_str()) == 0) m->biomeOff |= 1u << (i + 1);
				}
			}
			if (m->def.enabled && m->s.Valid() && (m->biomeId != -2 || m->biomeOn)) ok++;
			set->push_back(m);
		}
		VegReleaseUnused(usedKeys);
		InterlockedExchangePointer((PVOID volatile*)&g_zm, set);
		std::cout << "[Zonen-Modelle] " << set->size() << " Dateien, " << ok << " aktiv (Strukturen\\Zonenmodelle). "
		             "Sie entstehen in neu erzeugten Zonen." << std::endl;
	}
	std::vector<ZoneModelInfo> GetZoneModelInfo() {
		std::vector<ZoneModelInfo> out;
		std::vector<ZModelRT*>* set = g_zm;
		if (!set) return out;
		for (ZModelRT* m : *set) {
			ZoneModelInfo i; i.file = m->file; i.def = m->def; i.modelOk = m->s.Valid();
			i.sx = m->s.sx; i.sy = m->s.sy; i.sz = m->s.sz; i.placed = m->placed; i.note = m->note;
			if (m->def.kind == ZM_PICKUP && m->def.itemId) {
				char b[96]; sprintf_s(b, "Gegenstand 0x0B/0x%02X '%s': ", m->def.itemId, m->def.itemName.c_str());
				i.note = b + ItemNote(m->def.itemId) + (m->note.empty() ? "" : " / " + m->note);
			}
			if (m->vegSlot >= 0) {
				char b[96]; sprintf_s(b, "klein x%.3f, Pflanze Nr. 0x%X: ", m->def.smallScale, m->vegSlot);
				i.note = b + VegNote(m->vegSlot) + (m->note.empty() ? "" : " / " + m->note);
			}
			out.push_back(i);
		}
		return out;
	}
	std::string PlaceZoneModelHere(cube::GameController* gc, const ZoneModelDef& d, const Lab::Structure& s) {
		if (!s.Valid()) return "kein gueltiges Modell";
		int px, py, pz;
		if (!PlayerBlock(gc, &px, &py, &pz)) return "kein Spieler";
		if (d.kind == ZM_PICKUP) {
			if (d.itemId < kPickupFirstId || d.itemId >= kPickupFirstId + kPickupCount) return "Gegenstand: erst 'Speichern + laden' (vergibt die ID).";
			BuildPendingSprites(gc);
			int n = 0;
			for (int k = 0; k < 5; k++) {
				int x = px + 38 + k * 2, y = py;
				cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
				int zc; uint8_t t;
				if (!zone || !FindSurface(gc, zone, x, y, &zc, &t, false)) continue;
				if (PushItemDropRaw(zone, d.itemId, x, y, zc + 1 + d.zOffset, d.smallScale > 0.0f ? d.smallScale : 0.1f, (float)(k * 72))) n++;
			}
			char b[220]; sprintf_s(b, "%d x '%s' (Gegenstand 0x0B/0x%02X) 40 Bloecke neben dir abgelegt -- aufheben wie eine Herzblume. %s", n, d.itemName.c_str(), d.itemId, ItemNote(d.itemId).c_str());
			return b;
		}
		if (d.kind == ZM_FLOWER && d.smallScale > 0.0f) {
			// kleine Pflanze: braucht den Tabellenplatz des gespeicherten Modells
			int slot = -1;
			std::vector<ZModelRT*>* set = g_zm;
			if (set) for (ZModelRT* m : *set) if (m->vegSlot >= 0 && _stricmp(m->def.cub.c_str(), d.cub.c_str()) == 0) slot = m->vegSlot;
			if (slot < 0) return "kleine Blume: erst 'Speichern + laden' (aktiv), dann testen.";
			BuildPendingSprites(gc);
			int n = 0;
			for (int k = 0; k < 16; k++) {
				int x = px + 38 + (k % 4) * 2, y = py - 3 + (k / 4) * 2;
				cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
				int zc; uint8_t t;
				if (!zone || !FindSurface(gc, zone, x, y, &zc, &t, false)) continue;
				uint32_t h = Hash((uint32_t)x, (uint32_t)y, 0x7E57u);
				if (PushVegetationRaw(zone, slot, x, y, zc + 1 + d.zOffset, d.smallScale * (0.85f + 0.3f * (float)(h & 0xFF) / 255.0f),
				                      (float)(h % 360u), d.wind, 220.0f)) n++;
			}
			char b[200]; sprintf_s(b, "%d kleine '%s' (x%.3f, Pflanze Nr. 0x%X) 40 Bloecke neben dir gesetzt. %s", n, d.name.c_str(), d.smallScale, slot, VegNote(slot).c_str());
			return b;
		}
		int W = s.sx, D = s.sy;
		int cx = px + 40, cy = py;                                   // Testplatz 40 Bloecke versetzt
		int ox = cx - W / 2, oy = cy - D / 2;
		int zc; uint8_t t;
		if (!FindSurface(gc, LabBridge::ZoneAt(gc, cx >> 8, cy >> 8), cx, cy, &zc, &t, false)) return "kein Boden am Testplatz (Wasser?)";
		int z = (d.kind == ZM_TREE ? zc : zc + 1) + d.zOffset;
		if (d.kind == ZM_DUNGEON) {
			int zmax = zc;
			for (int yy = 0; yy <= D; yy += 4) for (int xx = 0; xx <= W; xx += 4) {
				int bx = ox + std::min(xx, W - 1), by = oy + std::min(yy, D - 1), zz; uint8_t tt;
				if (FindSurface(gc, LabBridge::ZoneAt(gc, bx >> 8, by >> 8), bx, by, &zz, &tt, false)) zmax = std::max(zmax, zz);
			}
			z = zmax + 1 + d.zOffset;
		}
		int n = StampLocked(gc, s, ox, oy, z, 0, d.foundation ? 1 : 0, INT_MIN, INT_MIN);
		LabBridge::RemeshArea(gc, ox, oy, ox + W, oy + D);
		char b[200]; sprintf_s(b, "%s '%s' bei (%d,%d,%d) gesetzt (%d Zonen).", ZoneModelKindName(d.kind), d.name.c_str(), cx, cy, z, n);
		return b;
	}

	void SetZoneObjects(bool on) { g_zoneObjOn = on; ModSwitches::Set("zonenobjekte", on); }
	bool ZoneObjectsOn() { return g_zoneObjOn; }
	void SetSmallFlowers(bool on) { g_smallOn = on; ModSwitches::Set("kleine_blumen", on); }
	bool SmallFlowersOn() { return g_smallOn; }

	bool FeatureAllowedInternal(uint32_t base, int x, int z, int kind) {
		HookSet* hs = g_hs;
		if (!g_on || !hs || kind <= 0 || kind > 15) return true;
		Dom d = OfflineDominant(base, x, z);
		if (!d.id || d.w < 0.5f) return true;
		const HookDef& h = hs->d[d.id - 1];
		if (!h.used || !h.enabled) return true;
		if (InPlain(h.plainOn, h.plainRadius, h.plainRim, d, x, z, 160.0f)) return false;   // freie Ebene
		return (h.siteMask & (1u << kind)) != 0;
	}
}

// =========================================================================
// Anbindung des Seed-Rechners (SeedScout.h) an die eigenen Biome
// =========================================================================
namespace SeedScout {
	uint32_t ModVersion() { return (uint32_t)BiomeMod::ModVersionInternal(); }
	void ModAdjustSite(uint32_t base, int regX, int regZ, Site& st) { BiomeMod::AdjustSiteInternal(base, regX, regZ, st.elev, &st.humid, &st.temp, &st.wet); }
	bool ModFeatureAllowed(uint32_t base, int x, int z, int type) { return BiomeMod::FeatureAllowedInternal(base, x, z, type); }
}
