// PerfTweaks.cpp -- siehe PerfTweaks.h und RE_KarteUndLeistung.md.
//
// Aufbau:
//   1. Patch-Werkzeug (Original pruefen, schreiben, zuruecknehmen)
//   2. Die Stellen -- jede mit Originalbytes aus der Cube.exe
//   3. PreciseSleep: exakter Bildtakt
//   4. Adaptive Pausen (Bewegungserkennung)
//   5. Messung + Bericht
//   6. Einstellungen (Datei AdventureMod_tweaks.cfg)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PerfTweaks.h"
#include "ModDb.h"
#include "Host.h"
#include "CrashLogger.h"
#include "AdventureModFramework.h"

#include <windows.h>
#include <intrin.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace PerfTweaks {

	// =====================================================================
	// 1. Patch-Werkzeug
	// =====================================================================
	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint8_t* At(uint32_t staticAddr) { return (uint8_t*)Host::Re(staticAddr); }

	static bool ReadBytes(uint32_t a, uint8_t* out, int n) {
		__try { memcpy(out, At(a), n); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool WriteBytes(uint32_t a, const uint8_t* src, int n) {
		uint8_t* p = At(a);
		DWORD old = 0;
		if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
		bool ok = true;
		__try { memcpy(p, src, n); }
		__except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		DWORD dummy; VirtualProtect(p, n, old, &dummy);
		FlushInstructionCache(GetCurrentProcess(), p, n);
		return ok;
	}

	// Eine Stelle = eine Instruktion. orig = Bytes in der Cube.exe (capstone-geprueft).
	struct Site {
		const char* name;
		uint32_t addr;
		uint8_t  orig[9];
		int      len;
		bool     verified;     // beim Start Original vorgefunden?
	};

	// =====================================================================
	// 2. Stellen  (alle in .text eindeutig, Kontext-Vorkommen = 1)
	// =====================================================================
	// Kartenthread World_streamChunksAroundPlayer @0x469590 (eigener Thread,
	// Endlosschleife mit Sleep). Er sucht in einem Fenster von +-10 Kartenkacheln
	// um den Spieler je Durchgang 10-mal die naechstgelegene Kachel ohne Kartendaten
	// und erzeugt sie (streamTileChunk @0x603A00). Eine Kachel = 1/64 Zone.
	enum {
		S_MAP_X0, S_MAP_X1, S_MAP_Y0, S_MAP_Y1, S_MAP_Y1B,      // Fenster +-10
		S_MAP_SLEEP,                                            // Sleep(10)
		S_LS_X0, S_LS_X1, S_LS_Y0, S_LS_Y1, S_LS2_Y0, S_LS2_Y1, // Landschaft +-3 Zonen
		S_MAP_DARKEN,                                           // Abdunklung x0.6
		S_STREAM_SLEEP,                                         // Chunk-Thread Sleep(5)
		S_FRAME_SLEEP,                                          // Bildbegrenzer Sleep
		S_COUNT
	};
	static Site g_sites[S_COUNT] = {
		{ "Kartenfenster X-Start  lea edx,[eax-10]", 0x46999D, { 0x8D, 0x50, 0xF6 }, 3, false },
		{ "Kartenfenster X-Ende   lea ecx,[eax+10]", 0x4699A0, { 0x8D, 0x48, 0x0A }, 3, false },
		{ "Kartenfenster Y-Start  lea ebx,[ecx-10]", 0x4699CC, { 0x8D, 0x59, 0xF6 }, 3, false },
		{ "Kartenfenster Y-Ende   add ecx,10",       0x4699D2, { 0x83, 0xC1, 0x0A }, 3, false },
		{ "Kartenfenster Y-Ende   lea ecx,[edx+10]", 0x469B39, { 0x8D, 0x4A, 0x0A }, 3, false },
		{ "Kartenthread           push 10 (Sleep)",  0x469BCF, { 0x6A, 0x0A }, 2, false },
		{ "Landschaft X-Start     lea esi,[eax-3]",  0x4696C0, { 0x8D, 0x70, 0xFD }, 3, false },
		{ "Landschaft X-Ende      add eax,3",        0x4696C5, { 0x83, 0xC0, 0x03 }, 3, false },
		{ "Landschaft Y-Start     lea esi,[eax-3]",  0x4696E5, { 0x8D, 0x70, 0xFD }, 3, false },
		{ "Landschaft Y-Ende      lea edx,[eax+3]",  0x4696E8, { 0x8D, 0x50, 0x03 }, 3, false },
		{ "Landschaft2 Y-Start    lea edx,[eax-3]",  0x469849, { 0x8D, 0x50, 0xFD }, 3, false },
		{ "Landschaft2 Y-Ende     lea ecx,[eax+3]",  0x46984C, { 0x8D, 0x48, 0x03 }, 3, false },
		{ "Abdunklung             jne +0x4A",        0x5FEA03, { 0x75, 0x4A }, 2, false },
		{ "Chunk-Thread           push 5 (Sleep)",   0x4694FE, { 0x6A, 0x05 }, 2, false },
		{ "Bildbegrenzer          call [Sleep]",     0x4C9456, { 0xFF, 0x15, 0x84, 0xC0, 0x6F, 0x00 }, 6, false },
	};

	// Gruppen: nur wenn ALLE Stellen einer Gruppe stimmen, wird sie benutzt.
	static bool g_grpMapWindow = false, g_grpMapSleep = false, g_grpLandscape = false,
	            g_grpDarken = false, g_grpStream = false, g_grpFrame = false;

	// ABSTURZSCHUTZ 2026-10-03: Dreimal derselbe Absturz (01.10. 21:50, 03.10. 20:30
	// und 21:10 nach ~3 min Spielzeit): std::bad_alloc im KARTENTHREAD
	// (0x469959 -> loadLandscapeTile -> VoxelModel_build_mesh -> operator new).
	// Der Kartenthread ist der einzige Ort, an dem dieser Baustein etwas am
	// Speicherverbrauch aendert (groesseres Kachelfenster, schnellerer Takt).
	// Deshalb bleiben alle drei Kartenthread-Gruppen hart AUS -- die Bytes dort
	// werden nie angefasst, der Kartenthread laeuft exakt wie Vanilla.
	static const bool kMapThreadOff = true;

	static bool VerifySite(Site& s) {
		uint8_t cur[16];
		s.verified = ReadBytes(s.addr, cur, s.len) && memcmp(cur, s.orig, s.len) == 0;
		return s.verified;
	}
	static bool Restore(int i) { return g_sites[i].verified && WriteBytes(g_sites[i].addr, g_sites[i].orig, g_sites[i].len); }

	// Imm8 einer Instruktion setzen (letztes Byte), Rest unveraendert.
	static bool SetImm8(int i, int8_t v) {
		Site& s = g_sites[i];
		if (!s.verified) return false;
		uint8_t b[16]; memcpy(b, s.orig, s.len);
		b[s.len - 1] = (uint8_t)v;
		return WriteBytes(s.addr, b, s.len);
	}

	// =====================================================================
	// 3. PreciseSleep -- exakter Bildtakt
	// =====================================================================
	// Original (Hauptschleife @0x4C9437):
	//     verstrichen = timeGetTime() - Bildstart;
	//     if (verstrichen < cfg->minTimeStep) Sleep(cfg->minTimeStep - verstrichen);
	// timeGetTime und Sleep rechnen in ganzen Millisekunden; Sleep(n) kehrt nach
	// n..n+1 ms zurueck. Die Bildabstaende springen dadurch unregelmaessig (bei
	// minTimeStep 10 zwischen ~10 und ~12 ms) -- sichtbar als feines Ruckeln.
	// PreciseSleep haelt stattdessen einen festen Takt mit dem Hochleistungszaehler:
	// grob schlafen bis ~2 ms vor dem Ziel, den Rest aktiv abwarten.
	// Signatur wie Sleep (__stdcall, 1 Argument) -- das "push ecx" davor bleibt.
	static LARGE_INTEGER g_qpf;
	static LONGLONG g_next = 0;
	static const uint32_t kCfgPtr = 0x0076B1C8;     // Zeiger auf die Optionen
	static const uint32_t kOffMinTimeStep = 0x19C;  // int, ms (options.cfg "minTimeStep")

	static int ReadMinTimeStep() {
		__try { uint8_t* cfg = *(uint8_t**)At(kCfgPtr); return cfg ? *(int*)(cfg + kOffMinTimeStep) : -1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	static bool WriteMinTimeStep(int v) {
		__try { uint8_t* cfg = *(uint8_t**)At(kCfgPtr); if (!cfg) return false; *(int*)(cfg + kOffMinTimeStep) = v; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static void __stdcall PreciseSleep(DWORD ms) {
		if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		int step = ReadMinTimeStep();
		if (step <= 0) step = (int)ms;
		LONGLONG period = g_qpf.QuadPart * step / 1000;
		// Neu einrasten, wenn der Takt verloren ging (langes Bild, Ladepause, erster Aufruf).
		if (g_next == 0 || now.QuadPart > g_next + period || g_next - now.QuadPart > 2 * period)
			g_next = now.QuadPart + g_qpf.QuadPart * (LONGLONG)ms / 1000;
		for (;;) {
			QueryPerformanceCounter(&now);
			LONGLONG left = g_next - now.QuadPart;
			if (left <= 0) break;
			double leftMs = (double)left * 1000.0 / (double)g_qpf.QuadPart;
			if (leftMs > 2.0) Sleep(1);          // timeBeginPeriod(1) ist vom Spiel gesetzt
			else _mm_pause();                     // letzte ~2 ms aktiv
		}
		g_next += period;
	}

	static bool InstallPreciseSleep(bool on) {
		if (!g_grpFrame) return false;
		if (!on) { g_next = 0; return Restore(S_FRAME_SLEEP); }
		// call rel32 auf PreciseSleep + nop  (6 Byte wie das Original)
		uint8_t b[6];
		uint32_t from = (uint32_t)At(g_sites[S_FRAME_SLEEP].addr);
		int32_t rel = (int32_t)((uint32_t)&PreciseSleep - (from + 5));
		b[0] = 0xE8; memcpy(b + 1, &rel, 4); b[5] = 0x90;
		return WriteBytes(g_sites[S_FRAME_SLEEP].addr, b, 6);
	}

	// =====================================================================
	// Zustand, Einstellungen
	// =====================================================================
	namespace {
		CRITICAL_SECTION g_cs; bool g_csInit = false;
		struct Guard { Guard() { EnterCriticalSection(&g_cs); } ~Guard() { LeaveCriticalSection(&g_cs); } };
		Settings g_set, g_pending; bool g_hasPending = false;
		bool g_enabled = true, g_pendingEnabled = true;
		bool g_installed = false;
		int  g_origMinTimeStep = -1;
		// Was gerade wirklich in der Exe steht
		int  g_curMapR = 10, g_curLsR = 3, g_curMapSleep = 10, g_curStreamSleep = 5;
		bool g_curDarkenOff = false, g_curPacing = false;
		// Adressraum-Waechter (eigener Thread, alle 3 s): ohne LAA hat Cube.exe 2 GB.
		// Beide Abstuerze (01.10./03.10.) waren bad_alloc im Kartenthread.
		CrashLogger::AddrSpace g_as; bool g_asLow = false; int g_selfLAA = -1;
		volatile bool g_asThread = false;
	}

	static int SelfLAA() {
		__try {
			uint8_t* b = (uint8_t*)GetModuleHandleA(NULL);
			uint32_t pe = *(uint32_t*)(b + 0x3C);
			return (*(uint16_t*)(b + pe + 0x16) & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 1 : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	// Knapp = groesster freier Block < 96 MB oder frei < 256 MB. Frei wieder ab
	// 160 / 400 MB (Hysterese, damit es nicht flattert). Die Werte liegen ueber
	// dem, was ein einzelnes Landschafts-Mesh braucht, aber deutlich vor dem Ende.
	static DWORD WINAPI AddrSpaceThread(LPVOID) {
		for (;;) {
			CrashLogger::AddrSpace a = CrashLogger::QueryAddrSpace(GetCurrentProcess());
			bool wasLow, low;
			{ Guard g; wasLow = g_asLow;
			  low = wasLow ? (a.largestFreeMB < 160 || a.freeMB < 400) : (a.largestFreeMB < 96 || a.freeMB < 256);
			  g_as = a; g_asLow = a.ok && low; }
			if (a.ok && low != wasLow)
				std::cout << (low ? "[Leistung] ADRESSRAUM KNAPP: frei " : "[Leistung] Adressraum wieder ausreichend: frei ")
				          << a.freeMB << " MB, groesster Block " << a.largestFreeMB << " MB von " << a.limitMB << " MB."
				          << (low ? " Kartenfenster vorsorglich auf Vanilla +-10. Abhilfe: Logger > \"4 GB freischalten\"." : "")
				          << std::endl;
			Sleep(3000);
		}
	}

	static const char* kCfgFile = "AdventureMod_tweaks.cfg";

	static void SaveSettings(const Settings& s, bool enabled) {
		ModDb::Writer f(kCfgFile);
		if (!f) return;
		f << "# AdventureMod: Karte & Leistung (PerfTweaks). Vom LAB geschrieben.\n";
		f << "version=2\n";
		f << "enabled=" << (enabled ? 1 : 0) << "\n";
		f << "mapNoDarken=" << s.mapNoDarken << "\nmapRadius=" << s.mapRadius << "\nlandscapeRadius=" << s.landscapeRadius
		  << "\nmapAdaptive=" << s.mapAdaptive << "\nmapSleepMove=" << s.mapSleepMove << "\nmapSleepIdle=" << s.mapSleepIdle
		  << "\nprecisePacing=" << s.precisePacing << "\nminTimeStep=" << s.minTimeStep
		  << "\nstreamAdaptive=" << s.streamAdaptive << "\nstreamSleepMove=" << s.streamSleepMove << "\nstreamSleepIdle=" << s.streamSleepIdle << "\n";
	}

	static void LoadSettings(Settings& s, bool& enabled) {
		std::istringstream f(ModDb::Txt(kCfgFile));
		if (!f) return;
		std::string line; int version = 1;
		while (std::getline(f, line)) {
			size_t eq = line.find('=');
			if (eq == std::string::npos || line[0] == '#') continue;
			std::string k = line.substr(0, eq); int v = atoi(line.c_str() + eq + 1);
			if (k == "version") version = v;
			else if (k == "enabled") enabled = v != 0;
			else if (k == "mapNoDarken") s.mapNoDarken = v != 0;
			else if (k == "mapRadius") s.mapRadius = v;
			else if (k == "landscapeRadius") s.landscapeRadius = v;
			else if (k == "mapAdaptive") s.mapAdaptive = v != 0;
			else if (k == "mapSleepMove") s.mapSleepMove = v;
			else if (k == "mapSleepIdle") s.mapSleepIdle = v;
			else if (k == "precisePacing") s.precisePacing = v != 0;
			else if (k == "minTimeStep") s.minTimeStep = v;
			else if (k == "streamAdaptive") s.streamAdaptive = v != 0;
			else if (k == "streamSleepMove") s.streamSleepMove = v;
			else if (k == "streamSleepIdle") s.streamSleepIdle = v;
		}
		// Version 1 hatte Landschaft +-5 als Standard -> Speicherabsturz. Zuruecksetzen.
		if (version < 2 && s.landscapeRadius > 3) {
			s.landscapeRadius = 3;
			std::cout << "[Leistung] Landschaftsfenster aus alter Einstellung (+-5) auf Vanilla +-3 zurueckgesetzt "
			             "(verursachte Speicherabsturz, siehe PerfTweaks.h)." << std::endl;
		}
	}

	static Settings Clamp(Settings s) {
		auto c = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
		// Obergrenze 32 Kacheln = 128 Bloecke. Reicht das Fenster in Zonen ohne
		// Kachelfeld (nicht geladen), gilt dort jede Kachel als "fehlt"
		// (hash_or_index_compute liefert 0 -> Kandidat), streamTileChunk kann sie
		// aber nicht fuellen. Die Suche nimmt dann immer wieder diese naechste,
		// unfuellbare Kachel und verhungert weiter entfernte, fuellbare.
		s.mapRadius       = c(s.mapRadius, 4, 32);
		s.landscapeRadius = c(s.landscapeRadius, 1, 4);    // > 3 nur mit Large-Address-Aware (siehe PerfTweaks.h)
		s.mapSleepMove    = c(s.mapSleepMove, 1, 50);
		s.mapSleepIdle    = c(s.mapSleepIdle, 1, 100);
		s.streamSleepMove = c(s.streamSleepMove, 1, 20);
		s.streamSleepIdle = c(s.streamSleepIdle, 1, 50);
		if (s.minTimeStep != -1) s.minTimeStep = c(s.minTimeStep, 0, 50);
		return s;
	}

	// =====================================================================
	// 4. Anwenden (nur Spielthread)
	// =====================================================================
	static void ApplyMapRadius(int r) {
		if (!g_grpMapWindow || r == g_curMapR) return;
		SetImm8(S_MAP_X0, (int8_t)-r); SetImm8(S_MAP_X1, (int8_t)r);
		SetImm8(S_MAP_Y0, (int8_t)-r); SetImm8(S_MAP_Y1, (int8_t)r); SetImm8(S_MAP_Y1B, (int8_t)r);
		g_curMapR = r;
	}
	static void ApplyLandscape(int r) {
		if (!g_grpLandscape || r == g_curLsR) return;
		SetImm8(S_LS_X0, (int8_t)-r); SetImm8(S_LS_X1, (int8_t)r);
		SetImm8(S_LS_Y0, (int8_t)-r); SetImm8(S_LS_Y1, (int8_t)r);
		SetImm8(S_LS2_Y0, (int8_t)-r); SetImm8(S_LS2_Y1, (int8_t)r);
		g_curLsR = r;
	}
	static void ApplyMapSleep(int ms)    { if (g_grpMapSleep && ms != g_curMapSleep && SetImm8(S_MAP_SLEEP, (int8_t)ms)) g_curMapSleep = ms; }
	static void ApplyStreamSleep(int ms) { if (g_grpStream && ms != g_curStreamSleep && SetImm8(S_STREAM_SLEEP, (int8_t)ms)) g_curStreamSleep = ms; }
	static void ApplyDarken(bool off) {
		if (!g_grpDarken || off == g_curDarkenOff) return;
		if (off) { uint8_t jmp[2] = { 0xEB, 0x4A }; WriteBytes(g_sites[S_MAP_DARKEN].addr, jmp, 2); }
		else Restore(S_MAP_DARKEN);
		g_curDarkenOff = off;
	}
	static void ApplyPacing(bool on) { if (on != g_curPacing && InstallPreciseSleep(on)) g_curPacing = on; }

	static void RevertAllNow() {
		ApplyMapRadius(10); ApplyLandscape(3); ApplyMapSleep(10); ApplyStreamSleep(5);
		ApplyDarken(false); ApplyPacing(false);
		if (g_origMinTimeStep >= 0) WriteMinTimeStep(g_origMinTimeStep);
	}

	void Install() {
		if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
		QueryPerformanceFrequency(&g_qpf);
		// Cube.exe hat ASLR (DllCharacteristics 0x8140, geladen z.B. bei 0x260000):
		// 'call [Sleep]' enthaelt die ABSOLUTE IAT-Adresse 0x6FC084, die der Lader
		// verschiebt. Deshalb stimmte die Stelle nie ("passt NICHT"). Erwartet wird
		// jetzt die verschobene Adresse -- so wird auch korrekt zurueckgeschrieben.
		{ uint32_t iat = Host::Re(0x006FC084); memcpy(g_sites[S_FRAME_SLEEP].orig + 2, &iat, 4); }
		int bad = 0;
		for (int i = 0; i < S_COUNT; i++) if (!VerifySite(g_sites[i])) {
			bad++;
			std::cout << "[Leistung] Stelle passt NICHT zur Cube.exe: " << g_sites[i].name
			          << " @0x" << std::hex << g_sites[i].addr << std::dec << " -- Gruppe bleibt aus." << std::endl;
		}
		auto all = [](std::initializer_list<int> l) { for (int i : l) if (!g_sites[i].verified) return false; return true; };
		g_grpMapWindow = all({ S_MAP_X0, S_MAP_X1, S_MAP_Y0, S_MAP_Y1, S_MAP_Y1B });
		g_grpMapSleep  = all({ S_MAP_SLEEP });
		g_grpLandscape = all({ S_LS_X0, S_LS_X1, S_LS_Y0, S_LS_Y1, S_LS2_Y0, S_LS2_Y1 });
		if (kMapThreadOff) {
			g_grpMapWindow = g_grpMapSleep = g_grpLandscape = false;
			std::cout << "[Leistung] Kartennachladung des Mods AUS (Absturzschutz): Kartenfenster, Kartenthread-Takt "
			             "und Landschaftsfenster bleiben Vanilla." << std::endl;
		}
		g_grpDarken    = all({ S_MAP_DARKEN });
		g_grpStream    = all({ S_STREAM_SLEEP });
		g_grpFrame     = all({ S_FRAME_SLEEP });

		Settings s; bool en = true;
		LoadSettings(s, en);
		{ Guard g; g_set = Clamp(s); g_pending = g_set; g_hasPending = true; g_pendingEnabled = en; }
		g_installed = true;
		g_selfLAA = SelfLAA();
		if (!g_asThread) { g_asThread = true; HANDLE t = CreateThread(NULL, 0, AddrSpaceThread, NULL, 0, NULL); if (t) CloseHandle(t); }
		std::cout << "[Leistung] Cube.exe Adressraum: " << (g_selfLAA == 1 ? "4 GB (Large Address Aware)" : g_selfLAA == 0 ? "2 GB (NICHT Large Address Aware)" : "?")
		          << std::endl;
		std::cout << "[Leistung] Karte & Leistung bereit: " << (S_COUNT - bad) << "/" << S_COUNT
		          << " Stellen geprueft. " << (en ? "AKTIV" : "aus") << " (LAB > Karte & Leistung)." << std::endl;
	}

	Settings Get() { Guard g; return g_hasPending ? g_pending : g_set; }
	void Set(const Settings& s) { Guard g; g_pending = Clamp(s); g_hasPending = true; }
	void SetEnabled(bool on) { Guard g; g_pendingEnabled = on; g_hasPending = true; }
	bool IsEnabled() { Guard g; return g_hasPending ? g_pendingEnabled : g_enabled; }

	// =====================================================================
	// 5. Messung
	// =====================================================================
	namespace {
		const int kHist = 600;                  // ~6-10 s Bildzeiten
		float g_frameMs[kHist]; int g_frameN = 0, g_frameI = 0;
		LONGLONG g_lastFrame = 0;
		// Bewegung
		int g_lastPx = 0, g_lastPy = 0; bool g_havePos = false;
		DWORD g_burstUntil = 0, g_posCheckAt = 0;
		bool g_moving = false;
	}

	static void RecordFrame() {
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		if (g_lastFrame) {
			float ms = (float)((double)(now.QuadPart - g_lastFrame) * 1000.0 / (double)g_qpf.QuadPart);
			g_frameMs[g_frameI] = ms; g_frameI = (g_frameI + 1) % kHist; if (g_frameN < kHist) g_frameN++;
		}
		g_lastFrame = now.QuadPart;
	}

	static bool ReadPlayerBlock(cube::GameController* gc, int* x, int* y) {
		__try {
			if (!gc || !gc->local_player) return false;
			*x = (int)(gc->local_player->entity_data.position.X >> 16);
			*y = (int)(gc->local_player->entity_data.position.Y >> 16);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Bewegung: alle 250 ms Position vergleichen. >= 2 Bloecke (8 Bl./s, also
	// zuegiges Laufen, Reiten, Gleiten) oder ein Sprung > 64 Bloecke (Teleport,
	// Luftschiff) startet bzw. verlaengert eine 3-s-"Schubphase".
	static void UpdateMotion(cube::GameController* gc) {
		DWORD now = GetTickCount();
		if ((int)(now - g_posCheckAt) < 250) { g_moving = (int)(g_burstUntil - now) > 0; return; }
		g_posCheckAt = now;
		int x, y;
		if (ReadPlayerBlock(gc, &x, &y)) {
			if (g_havePos) {
				int dx = x - g_lastPx, dy = y - g_lastPy;
				int d2 = dx * dx + dy * dy;
				if (d2 >= 4) g_burstUntil = now + 3000;
			}
			g_lastPx = x; g_lastPy = y; g_havePos = true;
		}
		g_moving = (int)(g_burstUntil - now) > 0;
	}

	void Tick(cube::GameController* gc) {
		if (!g_installed) return;
		RecordFrame();

		Settings s; bool en; bool changed = false;
		{
			Guard g;
			if (g_hasPending) { g_set = g_pending; g_enabled = g_pendingEnabled; g_hasPending = false; changed = true; }
			s = g_set; en = g_enabled;
		}
		if (changed) SaveSettings(s, en);

		if (g_origMinTimeStep < 0) g_origMinTimeStep = ReadMinTimeStep();

		if (!en) { if (changed) { RevertAllNow(); std::cout << "[Leistung] Alles auf Vanilla zurueckgesetzt." << std::endl; } return; }

		UpdateMotion(gc);

		// Schritt 1 -- Karte
		ApplyDarken(s.mapNoDarken);
		bool asLow; { Guard g; asLow = g_asLow; }
		ApplyMapRadius(asLow ? (std::min)(s.mapRadius, 10) : s.mapRadius);
		ApplyLandscape(s.landscapeRadius);
		ApplyMapSleep(s.mapAdaptive ? (g_moving ? s.mapSleepMove : s.mapSleepIdle) : 10);

		// Schritt 2 -- Fluessiger
		ApplyPacing(s.precisePacing);
		ApplyStreamSleep(s.streamAdaptive ? (g_moving ? s.streamSleepMove : s.streamSleepIdle) : 5);
		if (changed) {
			if (s.minTimeStep >= 0) WriteMinTimeStep(s.minTimeStep);
			else if (g_origMinTimeStep >= 0) WriteMinTimeStep(g_origMinTimeStep);
		}
	}

	std::string Report() {
		std::ostringstream o;
		Settings s; bool en;
		{ Guard g; s = g_set; en = g_enabled; }
		o << (en ? "AKTIV" : "AUS (Vanilla)") << (g_moving ? "   [in Bewegung: schnelle Pausen]" : "   [Stand: sparsame Pausen]") << "\r\n";
		const char* mapOff = kMapThreadOff ? "AUS (Absturzschutz)" : "FEHLT";
		o << "Gruppen:  Kartenfenster " << (g_grpMapWindow ? "ok" : mapOff) << ",  Kartenthread " << (g_grpMapSleep ? "ok" : mapOff)
		  << ",  Landschaft " << (g_grpLandscape ? "ok" : mapOff) << ",  Abdunklung " << (g_grpDarken ? "ok" : "FEHLT")
		  << ",  Chunk-Thread " << (g_grpStream ? "ok" : "FEHLT") << ",  Bildtakt " << (g_grpFrame ? "ok" : "FEHLT") << "\r\n";
		o << "Jetzt in der Exe:  Kartenfenster +-" << g_curMapR << " Kacheln (+-" << g_curMapR * 4 << " Bl.),  Landschaft +-" << g_curLsR
		  << " Zonen,  Kartenpause " << g_curMapSleep << " ms,  Chunkpause " << g_curStreamSleep << " ms,  Abdunklung "
		  << (g_curDarkenOff ? "aus" : "an") << ",  exakter Takt " << (g_curPacing ? "an" : "aus") << "\r\n";
		// Kosten des Kartenthreads relativ zu Vanilla: Pruefungen je Durchgang ~ (2R)^2, je ms Pause
		double cost = (double)(4 * g_curMapR * g_curMapR) / (double)(g_curMapSleep + 1) / (400.0 / 11.0);
		char b[160];
		sprintf_s(b, "Kartenthread-Last ca. %.1fx Vanilla (Suche je Durchgang ~ Radius^2 / Pause)\r\n", cost);
		o << b;
		{
			CrashLogger::AddrSpace a; bool low; { Guard g; a = g_as; low = g_asLow; }
			sprintf_s(b, "Adressraum: %s,  belegt %u MB,  frei %u MB,  groesster Block %u MB%s\r\n",
				g_selfLAA == 1 ? "4 GB (LAA)" : "2 GB (nicht LAA)", a.committedMB, a.freeMB, a.largestFreeMB,
				low ? "   KNAPP -> Kartenfenster auf +-10 begrenzt" : "");
			o << b;
		}
		int mts = ReadMinTimeStep();
		sprintf_s(b, "minTimeStep %d ms (Ziel %s fps)\r\n", mts, mts > 0 ? std::to_string(1000 / mts).c_str() : "unbegrenzt");
		o << b;

		// Bildzeiten
		if (g_frameN > 30) {
			std::vector<float> v(g_frameMs, g_frameMs + g_frameN);
			std::sort(v.begin(), v.end());
			double sum = 0; for (float f : v) sum += f;
			double avg = sum / v.size(), med = v[v.size() / 2], p99 = v[(size_t)(v.size() * 0.99)], mx = v.back();
			int spikes = 0; for (float f : v) if (f > 2.0 * med && f > 20.0) spikes++;
			sprintf_s(b, "Bilder: %.1f fps,  Median %.1f ms,  99%% %.1f ms,  Max %.1f ms,  Ruckler (>2x Median) %d von %d\r\n",
				1000.0 / avg, med, p99, mx, spikes, (int)v.size());
			o << b;
		}
		// Kosten je Mod-Baustein
		std::vector<std::pair<std::string, double>> t;
		AdventureFramework::GetTickTimings(t);
		std::sort(t.begin(), t.end(), [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& c) { return a.second > c.second; });
		o << "Mod-Bausteine je Bild (gleitender Mittelwert):\r\n";
		for (size_t i = 0; i < t.size() && i < 8; i++) {
			sprintf_s(b, "   %6.3f ms   %s\r\n", t[i].second, t[i].first.c_str());
			o << b;
		}
		return o.str();
	}
}
