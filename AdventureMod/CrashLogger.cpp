// CrashLogger.cpp -- siehe CrashLogger.h.
//
//   Teil A: im Spiel (Logdatei, In-Prozess-Absturzfilter, Logger starten)
//   Teil B: im Logger-Prozess (Fenster, Debugger, Absturzbericht, Minidump)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CrashLogger.h"

#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shell32.lib")
// rundll32 sucht den undekorierten Namen; __stdcall exportiert sonst "_LabLoggerMain@16".
#if defined(_MSC_VER) && !defined(__clang__)
#pragma comment(linker, "/EXPORT:LabLoggerMain=_LabLoggerMain@16")
#endif

namespace CrashLogger {

	static const char* kLogFile  = "AdventureMod_log.txt";
	static const char* kLogPrev  = "AdventureMod_log_vorher.txt";
	static const char* kInProc   = "AdventureMod_crash_inproc.txt";

	// =====================================================================
	// Teil A -- im Spiel
	// =====================================================================
	namespace {
		HMODULE g_dll = nullptr;
		HANDLE  g_log = INVALID_HANDLE_VALUE;
		CRITICAL_SECTION g_cs; bool g_csInit = false;
		char    g_ring[24 * 1024]; size_t g_ringPos = 0; bool g_ringFull = false;   // letzte Logzeilen
		char    g_dir[MAX_PATH] = "";
	}

	// =====================================================================
	// Leistungsmesser: gemeinsamer Speicher (beide Prozesse)
	// =====================================================================
	struct PerfShm {
		uint32_t magic;                              // 'CWPF'
		volatile LONG beat[4];                       // Herzschlaege: Spiel, LAB, Zonen
		volatile LONG textSeq; DWORD textTick;       // Textbericht: Folgenummer, GetTickCount beim Schreiben
		volatile LONG zones, places;
		float zoneLastMs, zoneMaxMs, zoneSumMs, placeLastMs, placeMaxMs, placeSumMs;
		volatile LONG cnt[PC_COUNT];
		char text[8192];
	};
	static const uint32_t kPerfMagic = 0x46505743;   // "CWPF"
	static void PerfName(DWORD pid, char* out, int cap) { sprintf_s(out, cap, "Local\\CWLabPerf_%u", pid); }
	static PerfShm* g_perf = nullptr;                // im Spiel
	static void PerfCreate() {
		char n[64]; PerfName(GetCurrentProcessId(), n, sizeof(n));
		HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(PerfShm), n);
		if (!m) return;
		g_perf = (PerfShm*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PerfShm));
		if (g_perf) { memset(g_perf, 0, sizeof(PerfShm)); g_perf->magic = kPerfMagic; }
	}
	void PerfBeat(int w) { if (g_perf && w >= 0 && w < 4) InterlockedIncrement(&g_perf->beat[w]); }
	void PerfCount(int w, long n) { if (g_perf && w >= 0 && w < PC_COUNT) g_perf->cnt[w] += n; }
	void PerfZoneGen(double ms) {
		if (!g_perf) return;
		InterlockedIncrement(&g_perf->zones);
		g_perf->zoneLastMs = (float)ms; g_perf->zoneSumMs += (float)ms; if (ms > g_perf->zoneMaxMs) g_perf->zoneMaxMs = (float)ms;
	}
	void PerfZonePlace(double ms) {
		if (!g_perf) return;
		InterlockedIncrement(&g_perf->places);
		g_perf->placeLastMs = (float)ms; g_perf->placeSumMs += (float)ms; if (ms > g_perf->placeMaxMs) g_perf->placeMaxMs = (float)ms;
	}
	void PerfPublishText(const char* t) {
		if (!g_perf || !t) return;
		size_t n = strlen(t); if (n >= sizeof(g_perf->text)) n = sizeof(g_perf->text) - 1;
		InterlockedIncrement(&g_perf->textSeq);              // ungerade = wird geschrieben
		memcpy(g_perf->text, t, n); g_perf->text[n] = 0;
		g_perf->textTick = GetTickCount();
		InterlockedIncrement(&g_perf->textSeq);
	}

	// =====================================================================
	// Adressraum + Large-Address-Aware (beide Prozesse)
	// =====================================================================
	AddrSpace QueryAddrSpace(HANDLE p, uintptr_t maxAddr) {
		AddrSpace a;
		SYSTEM_INFO si; GetSystemInfo(&si);
		unsigned long long fr = 0, cm = 0, rs = 0, im = 0, big = 0;
		uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
		// Fuer einen FREMDEN Prozess kann die eigene Obergrenze (rundll32 aus SysWOW64)
		// hoeher sein als die des Spiels -- der Aufrufer gibt dann die Grenze des
		// Spiels mit (0x7FFEFFFF ohne LAA), sonst zaehlte 2..4 GB als "frei".
		uintptr_t top = maxAddr ? maxAddr : (uintptr_t)si.lpMaximumApplicationAddress;
		MEMORY_BASIC_INFORMATION mbi;
		int guard = 0;
		while (guard++ < 200000) {
			SIZE_T r = VirtualQueryEx(p, (LPCVOID)addr, &mbi, sizeof(mbi));
			if (r == 0) break;
			unsigned long long sz = mbi.RegionSize;
			if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize > top + 1) sz = top + 1 - (uintptr_t)mbi.BaseAddress;
			if (mbi.State == MEM_FREE) { fr += sz; if (sz > big) big = sz; }
			else if (mbi.State == MEM_RESERVE) rs += sz;
			else if (mbi.State == MEM_COMMIT) { cm += sz; if (mbi.Type == MEM_IMAGE) im += sz; }
			uintptr_t next = (uintptr_t)mbi.BaseAddress + (uintptr_t)mbi.RegionSize;
			if (next <= addr) break;
			addr = next;
			if (addr > top) break;
		}
		a.freeMB = (unsigned)(fr >> 20); a.largestFreeMB = (unsigned)(big >> 20);
		a.committedMB = (unsigned)(cm >> 20); a.reservedMB = (unsigned)(rs >> 20); a.imageMB = (unsigned)(im >> 20);
		a.limitMB = (unsigned)(((unsigned long long)top + 1) >> 20);
		a.ok = guard > 1;
		return a;
	}

	// PE: e_lfanew @0x3C, FileHeader.Characteristics @ pe+0x16, Bit 0x20 = LARGE_ADDRESS_AWARE.
	static bool ReadCharacteristics(HANDLE f, DWORD* peOff, WORD* ch) {
		DWORD r = 0; unsigned char mz[0x40];
		SetFilePointer(f, 0, NULL, FILE_BEGIN);
		if (!ReadFile(f, mz, sizeof(mz), &r, NULL) || r != sizeof(mz) || mz[0] != 'M' || mz[1] != 'Z') return false;
		DWORD pe = *(DWORD*)(mz + 0x3C);
		unsigned char hdr[0x18];
		if (SetFilePointer(f, pe, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
		if (!ReadFile(f, hdr, sizeof(hdr), &r, NULL) || r != sizeof(hdr) || memcmp(hdr, "PE\0\0", 4) != 0) return false;
		if (*(WORD*)(hdr + 4) != IMAGE_FILE_MACHINE_I386) return false;
		*peOff = pe; *ch = *(WORD*)(hdr + 0x16);
		return true;
	}

	int ExeIsLargeAddressAware(const char* path) {
		if (!path || !*path) return -1;
		HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE) return -1;
		DWORD pe = 0; WORD ch = 0; bool ok = ReadCharacteristics(f, &pe, &ch);
		CloseHandle(f);
		return ok ? ((ch & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 1 : 0) : -1;
	}

	bool SetExeLargeAddressAware(const char* path, bool on, char* msg, int cap) {
		auto say = [&](const char* s) { if (msg && cap > 0) { strncpy_s(msg, cap, s, _TRUNCATE); } };
		if (!path || !*path) { say("Pfad der Cube.exe unbekannt."); return false; }
		HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE) {
			DWORD e = GetLastError();
			say(e == ERROR_SHARING_VIOLATION ? "Cube.exe ist noch in Benutzung -- erst das Spiel beenden."
			                                 : "Cube.exe laesst sich nicht zum Schreiben oeffnen (Rechte?).");
			return false;
		}
		DWORD pe = 0; WORD ch = 0;
		if (!ReadCharacteristics(f, &pe, &ch)) { CloseHandle(f); say("Cube.exe hat kein erwartetes 32-Bit-PE-Format -- nichts geaendert."); return false; }
		bool is = (ch & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
		if (is == on) { CloseHandle(f); say(on ? "4 GB war schon eingeschaltet." : "4 GB war schon aus."); return true; }
		CloseHandle(f);
		// Sicherung nur beim ersten Mal (Original bleibt das Original).
		std::string bak = std::string(path) + ".vor-4GB.bak";
		if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileA(path, bak.c_str(), TRUE)) {
			say("Sicherung Cube.exe.vor-4GB.bak konnte nicht angelegt werden -- nichts geaendert."); return false;
		}
		f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE) { say("Cube.exe nach der Sicherung nicht mehr schreibbar."); return false; }
		if (!ReadCharacteristics(f, &pe, &ch)) { CloseHandle(f); say("Kopfdaten unlesbar -- nichts geaendert."); return false; }
		WORD nch = on ? (WORD)(ch | IMAGE_FILE_LARGE_ADDRESS_AWARE) : (WORD)(ch & ~IMAGE_FILE_LARGE_ADDRESS_AWARE);
		DWORD w = 0;
		bool ok = SetFilePointer(f, pe + 0x16, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && WriteFile(f, &nch, 2, &w, NULL) && w == 2;
		// Die PE-Pruefsumme der Cube.exe ist 0 (nicht benutzt); der Lader prueft sie fuer
		// normale Programme ohnehin nicht. Daher bleibt sie unveraendert.
		CloseHandle(f);
		say(ok ? (on ? "4 GB eingeschaltet. Gilt ab dem naechsten Spielstart. Sicherung: Cube.exe.vor-4GB.bak"
		             : "4 GB ausgeschaltet (Vanilla-Kopf). Gilt ab dem naechsten Spielstart.")
		       : "Schreiben fehlgeschlagen -- Cube.exe aus der Sicherung wiederherstellen.");
		return ok;
	}

	bool IsLoggerHost() {
		char p[MAX_PATH] = ""; GetModuleFileNameA(NULL, p, MAX_PATH);
		size_t n = strlen(p);
		return n >= 12 && _stricmp(p + n - 12, "rundll32.exe") == 0;
	}

	void WriteLog(const char* s, size_t n) {
		if (!g_csInit || !s || !n) return;
		EnterCriticalSection(&g_cs);
		if (g_log != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(g_log, s, (DWORD)n, &w, NULL); }   // OS-Puffer: ueberlebt den Absturz
		for (size_t i = 0; i < n; i++) {
			g_ring[g_ringPos++] = s[i];
			if (g_ringPos == sizeof(g_ring)) { g_ringPos = 0; g_ringFull = true; }
		}
		LeaveCriticalSection(&g_cs);
	}

	// ---- In-Prozess-Absturzfilter -------------------------------------------
	// Laeuft nur, wenn KEIN Debugger haengt (sonst bekommt der Logger die
	// Ausnahme). Keine Heap-Nutzung: der Heap kann gerade kaputt sein.
	static void ModuleOf(DWORD addr, char* out, int cap) {
		HMODULE m = nullptr; char p[MAX_PATH] = "";
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) && m) {
			GetModuleFileNameA(m, p, MAX_PATH);
			const char* b = strrchr(p, '\\'); b = b ? b + 1 : p;
			DWORD off = addr - (DWORD)m;
			if (_stricmp(b, "Cube.exe") == 0) wsprintfA(out, "%s+0x%X (Ghidra 0x%X)", b, off, off + 0x400000);
			else wsprintfA(out, "%s+0x%X", b, off);
		} else wsprintfA(out, "0x%08X (kein Modul)", addr);
		(void)cap;
	}

	static void WriteStr(HANDLE f, const char* s) { DWORD w; WriteFile(f, s, (DWORD)strlen(s), &w, NULL); }

	static LONG WINAPI InProcFilter(EXCEPTION_POINTERS* ep) {
		static volatile LONG once = 0;
		if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
		char path[MAX_PATH]; wsprintfA(path, "%s\\%s", g_dir, kInProc);
		HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
		if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
		char b[512], m[300];
		EXCEPTION_RECORD* er = ep->ExceptionRecord; CONTEXT* c = ep->ContextRecord;
		SYSTEMTIME t; GetLocalTime(&t);
		wsprintfA(b, "=== Cube World Absturz (im Spiel erfasst)  %04d-%02d-%02d %02d:%02d:%02d ===\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); WriteStr(f, b);
		ModuleOf((DWORD)er->ExceptionAddress, m, sizeof(m));
		wsprintfA(b, "Ausnahme 0x%08X bei %s\r\n", er->ExceptionCode, m); WriteStr(f, b);
		if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
			wsprintfA(b, "Zugriff: %s an 0x%08X\r\n", er->ExceptionInformation[0] == 0 ? "Lesen" : er->ExceptionInformation[0] == 1 ? "Schreiben" : "Ausfuehren", (DWORD)er->ExceptionInformation[1]);
			WriteStr(f, b);
		}
		wsprintfA(b, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X\r\nEBP=%08X ESP=%08X EIP=%08X\r\n",
			c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, c->Eip); WriteStr(f, b);
		WriteStr(f, "Ruecksprungadressen auf dem Stapel (Rohsuche):\r\n");
		int found = 0;
		for (int i = 0; i < 2048 && found < 40; i++) {
			DWORD v = 0;
			__try { v = ((DWORD*)c->Esp)[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
			MEMORY_BASIC_INFORMATION mbi;
			if (!VirtualQuery((LPCVOID)v, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) continue;
			if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;
			ModuleOf(v, m, sizeof(m)); wsprintfA(b, "  [ESP+0x%03X] %s\r\n", i * 4, m); WriteStr(f, b); found++;
		}
		WriteStr(f, "\r\nLetzte Logzeilen:\r\n");
		DWORD w;
		if (g_ringFull) WriteFile(f, g_ring + g_ringPos, (DWORD)(sizeof(g_ring) - g_ringPos), &w, NULL);
		WriteFile(f, g_ring, (DWORD)g_ringPos, &w, NULL);
		CloseHandle(f);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	bool LoggerRunning() {
		char name[64]; wsprintfA(name, "Local\\CWLabLogger_%u", GetCurrentProcessId());
		HANDLE h = OpenMutexA(SYNCHRONIZE, FALSE, name);
		if (h) { CloseHandle(h); return true; }
		return false;
	}

	bool StartLogger() {
		if (LoggerRunning()) {
			HWND w = FindWindowA("CWLabLogger", NULL);
			if (w) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
			return true;
		}
		char sys[MAX_PATH] = "";
		if (!GetSystemWow64DirectoryA(sys, MAX_PATH)) GetSystemDirectoryA(sys, MAX_PATH);   // 64-Bit-Windows: 32-Bit-rundll32
		char dll[MAX_PATH] = ""; GetModuleFileNameA(g_dll, dll, MAX_PATH);
		char cmd[3 * MAX_PATH];
		wsprintfA(cmd, "\"%s\\rundll32.exe\" \"%s\",LabLoggerMain %u", sys, dll, GetCurrentProcessId());
		STARTUPINFOA si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
		PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
		if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, g_dir[0] ? g_dir : NULL, &si, &pi)) {
			char b[400]; wsprintfA(b, "[Logger] konnte nicht gestartet werden (Fehler %u): %s\n", GetLastError(), cmd);
			WriteLog(b, strlen(b));
			return false;
		}
		CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
		return true;
	}

	void InitInGame(HMODULE dll) {
		g_dll = dll;
		if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
		GetCurrentDirectoryA(MAX_PATH, g_dir);
		char a[MAX_PATH], b[MAX_PATH];
		wsprintfA(a, "%s\\%s", g_dir, kLogFile); wsprintfA(b, "%s\\%s", g_dir, kLogPrev);
		MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING);
		g_log = CreateFileA(a, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		SYSTEMTIME t; GetLocalTime(&t);
		char h[200]; wsprintfA(h, "=== AdventureMod-Log  %04d-%02d-%02d %02d:%02d:%02d  PID %u ===\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
		WriteLog(h, strlen(h));
		SetUnhandledExceptionFilter(InProcFilter);
		PerfCreate();
		// Externer Logger/Debugger startet NICHT mehr von selbst -- nur ueber LAB -> Logger ("Logger starten").
	}

	// =====================================================================
	// Teil B -- Logger-Prozess
	// =====================================================================
	namespace L {
		DWORD  pid = 0;
		HANDLE hProc = nullptr;
		HWND   wnd = nullptr, status = nullptr, log = nullptr, btnDbg = nullptr, chkFirst = nullptr, info = nullptr, btnLaa = nullptr;
		HWND   btnPerf = nullptr, btnThreads = nullptr, chkPerfAuto = nullptr;
		PerfShm* perf = nullptr;                 // Leistungsmesser des Spiels (nur lesen)
		LONG   beatSeen[4] = { 0, 0, 0, 0 }; DWORD beatTick[4] = { 0, 0, 0, 0 };
		DWORD  perfAutoLast = 0;
		volatile LONG dumpBusy = 0;
		CRITICAL_SECTION symCs;                  // dbghelp ist nicht threadsicher
		std::string exePath;                     // Pfad der laufenden Cube.exe (fuer den 4-GB-Schalter)
		HFONT  font = nullptr, fontBig = nullptr;
		CRITICAL_SECTION cs;
		std::string pending;                     // Text vom Debug-Thread fuers Fenster
		std::string tail;                        // letzte ~16 KB Log (fuer den Bericht)
		std::string statusText = "Verbinde ...";
		bool   statusBad = false;
		volatile bool attached = false, wantDetach = false, exited = false, showFirst = false;
		HANDLE dbgThread = nullptr;
		LONGLONG logPos = 0;
		std::string lastReport;
		struct Mod { DWORD base, size; std::string name; };
		std::vector<Mod> mods;                   // nur Debug-Thread
		std::map<DWORD, HANDLE> threads;         // nur Debug-Thread
		DWORD mainBase = 0;
		enum { IDB_DBG = 101, IDB_DIR, IDB_REPORT, IDB_CLEAR, IDB_FIRST, IDB_PREV, IDB_LAA, IDB_PERF, IDB_THREADS, IDB_PERFAUTO, IDT_TAIL = 1, WM_LOGGER_POKE = WM_APP + 7 };
	}

	static void Post(const std::string& s) {
		EnterCriticalSection(&L::cs); L::pending += s; LeaveCriticalSection(&L::cs);
		if (L::wnd) PostMessageA(L::wnd, L::WM_LOGGER_POKE, 0, 0);
	}
	static void SetStatus(const std::string& s, bool bad) {
		EnterCriticalSection(&L::cs); L::statusText = s; L::statusBad = bad; LeaveCriticalSection(&L::cs);
		if (L::wnd) PostMessageA(L::wnd, L::WM_LOGGER_POKE, 0, 0);
	}

	static const char* CodeName(DWORD c) {
		switch (c) {
			case 0x00000000: return "normal beendet";
			case 0xC0000005: return "Zugriffsverletzung (Speicher)";
			case 0xC0000409: return "Fail-Fast / Stapelpuffer-Ueberlauf (Spiel bricht SOFORT ab, ohne Meldung)";
			case 0xC0000374: return "Heap beschaedigt (Speicher wurde vorher zerschrieben)";
			case 0xC00000FD: return "Stapelueberlauf (Endlosrekursion?)";
			case 0xC000041D: return "Ausnahme in einem Win32-Callback (z.B. im EndScene-Hook) -- Prozess beendet";
			case 0xC0000094: return "Ganzzahl-Division durch 0";
			case 0xC000001D: return "Ungueltiger Befehl (Sprung ins Leere?)";
			case 0xC0000096: return "Privilegierter Befehl (Sprung ins Leere?)";
			case 0xC0000017: return "Kein Speicher mehr";
			case 0xE06D7363: return "C++-Ausnahme (nicht abgefangen)";
			case 0x80000003: return "Haltepunkt (int3)";
			case 0x40000015: return "abort() / fataler Programmabbruch";
			case 0xC000013A: return "Fenster geschlossen / Strg+C";
			case 0xC0000420: return "Assertion fehlgeschlagen";
			default: return "unbekannt";
		}
	}

	static std::string Hex(DWORD v) { char b[16]; sprintf_s(b, "0x%08X", v); return b; }

	static std::string Where(DWORD a) {
		for (const L::Mod& m : L::mods)
			if (a >= m.base && a < m.base + m.size) {
				char b[200];
				if (m.base == L::mainBase) sprintf_s(b, "%s+0x%X (Ghidra 0x%X)", m.name.c_str(), a - m.base, a - m.base + 0x400000);
				else sprintf_s(b, "%s+0x%X", m.name.c_str(), a - m.base);
				return b;
			}
		return Hex(a) + " (kein Modul)";
	}

	static DWORD ImageSize(DWORD base) {
		DWORD lfanew = 0, size = 0; SIZE_T r;
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(base + 0x3C), &lfanew, 4, &r)) return 0x100000;
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(base + lfanew + 0x50), &size, 4, &r)) return 0x100000;
		return size ? size : 0x100000;
	}

	static void AddModule(DWORD base, HANDLE file, const char* fallback) {
		char p[MAX_PATH] = "";
		if (file && file != INVALID_HANDLE_VALUE) GetFinalPathNameByHandleA(file, p, MAX_PATH, 0);
		const char* n = strrchr(p, '\\'); n = (n && n[1]) ? n + 1 : (p[0] ? p : fallback);
		L::mods.push_back({ base, ImageSize(base), n });
	}

	static std::string Timestamp(bool file) {
		SYSTEMTIME t; GetLocalTime(&t); char b[40];
		if (file) sprintf_s(b, "%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		else sprintf_s(b, "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		return b;
	}

	// LAA-Bit der LAUFENDEN Cube.exe (aus dem Speicher, nicht aus der Datei --
	// die Datei kann seither umgeschaltet worden sein).
	static int TargetLAA() {
		if (!L::mainBase || !L::hProc) return -1;
		DWORD lfanew = 0; WORD ch = 0; SIZE_T r;
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(L::mainBase + 0x3C), &lfanew, 4, &r)) return -1;
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(L::mainBase + lfanew + 0x16), &ch, 2, &r)) return -1;
		return (ch & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 1 : 0;
	}

	// MSVC-C++-Ausnahme 0xE06D7363: Info[0] = 0x19930520, Info[2] = ThrowInfo*.
	// 32 Bit: absolute Zeiger. ThrowInfo+0x0C -> CatchableTypeArray {n, CatchableType*[n]},
	// CatchableType+0x04 -> TypeDescriptor, dessen Name ab +0x08 (".?AVbad_alloc@std@@").
	static std::string CxxTypeName(const EXCEPTION_RECORD& er) {
		if (er.ExceptionCode != 0xE06D7363 || er.NumberParameters < 3 || er.ExceptionInformation[0] != 0x19930520) return "";
		SIZE_T r; DWORD ti = (DWORD)er.ExceptionInformation[2], cta = 0, ct0 = 0, td = 0;
		if (!ti || !ReadProcessMemory(L::hProc, (LPCVOID)(ti + 0x0C), &cta, 4, &r) || !cta) return "";
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(cta + 4), &ct0, 4, &r) || !ct0) return "";
		if (!ReadProcessMemory(L::hProc, (LPCVOID)(ct0 + 4), &td, 4, &r) || !td) return "";
		char name[96] = ""; ReadProcessMemory(L::hProc, (LPCVOID)(td + 8), name, sizeof(name) - 1, &r);
		name[sizeof(name) - 1] = 0;
		return name;
	}

	static std::string AddrSpaceLine(bool* low) {
		int laa = TargetLAA();
		CrashLogger::AddrSpace a = QueryAddrSpace(L::hProc, laa == 1 ? (uintptr_t)0xFFFEFFFF : (uintptr_t)0x7FFEFFFF);
		char b[300];
		sprintf_s(b, "Adressraum: Grenze %u MB (%s),  belegt %u MB,  reserviert %u MB,  frei %u MB,  groesster freier Block %u MB",
			a.limitMB, laa == 1 ? "4 GB / LAA" : (laa == 0 ? "2 GB, NICHT LAA" : "?"), a.committedMB, a.reservedMB, a.freeMB, a.largestFreeMB);
		if (low) *low = a.ok && (a.largestFreeMB < 64 || a.freeMB < 192);
		return b;
	}

	// Zweite Chance = niemand im Spiel faengt die Ausnahme -> Absturz.
	static void CrashReport(const DEBUG_EVENT& ev) {
		const EXCEPTION_RECORD& er = ev.u.Exception.ExceptionRecord;
		HANDLE th = L::threads.count(ev.dwThreadId) ? L::threads[ev.dwThreadId] : OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
		CONTEXT ctx; memset(&ctx, 0, sizeof(ctx)); ctx.ContextFlags = CONTEXT_FULL;
		bool haveCtx = th && GetThreadContext(th, &ctx);
		std::string ts = Timestamp(true);
		std::string rep;
		char b[400];
		rep += "=== Cube World ABSTURZ  " + Timestamp(false) + "  (vom LAB-Logger als Debugger erfasst) ===\r\n";
		sprintf_s(b, "Ausnahme:  0x%08X  %s\r\n", er.ExceptionCode, CodeName(er.ExceptionCode)); rep += b;
		rep += "Ort:       " + Where((DWORD)er.ExceptionAddress) + "   Thread " + std::to_string(ev.dwThreadId) + "\r\n";
		std::string cxx = CxxTypeName(er);
		if (!cxx.empty()) rep += "C++-Typ:   " + cxx + "\r\n";
		bool low = false;
		rep += AddrSpaceLine(&low) + "\r\n";
		if (cxx.find("bad_alloc") != std::string::npos || low) {
			rep += "Deutung:   SPEICHER ERSCHOEPFT -- operator new fand keinen freien Block mehr.";
			rep += (TargetLAA() == 1) ? "\r\n" : "  Abhilfe: Knopf \"4 GB freischalten\" (nach Spielende).\r\n";
		}
		if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
			sprintf_s(b, "Zugriff:   %s an 0x%08X\r\n", er.ExceptionInformation[0] == 0 ? "Lesen" : er.ExceptionInformation[0] == 1 ? "Schreiben" : "Ausfuehren",
				(DWORD)er.ExceptionInformation[1]); rep += b;
		}
		if (haveCtx) {
			sprintf_s(b, "Register:  EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X\r\n           EBP=%08X ESP=%08X EIP=%08X\r\n",
				ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp, ctx.Eip); rep += b;
			// Aufrufstapel ueber dbghelp
			rep += "Aufrufstapel:\r\n";
			EnterCriticalSection(&L::symCs);
			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
			SymInitialize(L::hProc, NULL, TRUE);
			STACKFRAME64 f; memset(&f, 0, sizeof(f));
			f.AddrPC.Offset = ctx.Eip; f.AddrPC.Mode = AddrModeFlat;
			f.AddrFrame.Offset = ctx.Ebp; f.AddrFrame.Mode = AddrModeFlat;
			f.AddrStack.Offset = ctx.Esp; f.AddrStack.Mode = AddrModeFlat;
			CONTEXT c2 = ctx;
			for (int i = 0; i < 40; i++) {
				if (!StackWalk64(IMAGE_FILE_MACHINE_I386, L::hProc, th, &f, &c2, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
				if (!f.AddrPC.Offset) break;
				sprintf_s(b, "  #%-2d %s\r\n", i, Where((DWORD)f.AddrPC.Offset).c_str()); rep += b;
			}
			LeaveCriticalSection(&L::symCs);
			// Rohsuche: Ruecksprungadressen auf dem Stapel (hilft, wenn der Rahmen fehlt)
			rep += "Stapel-Rohsuche (Adressen in Modulen):\r\n";
			std::vector<DWORD> st(2048); SIZE_T r = 0;
			ReadProcessMemory(L::hProc, (LPCVOID)ctx.Esp, st.data(), st.size() * 4, &r);
			int found = 0;
			for (size_t i = 0; i < r / 4 && found < 30; i++) {
				std::string w = Where(st[i]);
				if (w.find("kein Modul") != std::string::npos) continue;
				sprintf_s(b, "  [ESP+0x%03X] %s\r\n", (unsigned)(i * 4), w.c_str()); rep += b; found++;
			}
		}
		rep += "Module:\r\n";
		for (const L::Mod& m : L::mods) {
			if (m.name.find("AdventureMod") == std::string::npos && m.base != L::mainBase && m.name.find("d3d") == std::string::npos) continue;
			sprintf_s(b, "  %08X-%08X  %s\r\n", m.base, m.base + m.size, m.name.c_str()); rep += b;
		}
		// Minidump fuer Visual Studio (Datei > Oeffnen > die .dmp, dann "Nur systemeigen debuggen")
		std::string dmp = "AdventureMod_crash_" + ts + ".dmp";
		HANDLE df = CreateFileA(dmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		if (df != INVALID_HANDLE_VALUE) {
			EXCEPTION_RECORD erc = er; CONTEXT cc = ctx;
			EXCEPTION_POINTERS ep = { &erc, &cc };
			MINIDUMP_EXCEPTION_INFORMATION mei; mei.ThreadId = ev.dwThreadId; mei.ExceptionPointers = &ep; mei.ClientPointers = FALSE;
			BOOL ok = MiniDumpWriteDump(L::hProc, L::pid, df, (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
				MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData), haveCtx ? &mei : NULL, NULL, NULL);
			CloseHandle(df);
			rep += ok ? ("Minidump: " + dmp + "  (in Visual Studio oeffnen)\r\n") : "Minidump: FEHLGESCHLAGEN\r\n";
		}
		EnterCriticalSection(&L::cs); std::string t = L::tail; LeaveCriticalSection(&L::cs);
		if (t.size() > 6000) t = t.substr(t.size() - 6000);
		rep += "\r\n--- Letzte Logzeilen ---\r\n" + t + "\r\n";
		std::string txt = "AdventureMod_crash_" + ts + ".txt";
		FILE* fp = nullptr; if (fopen_s(&fp, txt.c_str(), "wb") == 0 && fp) { fwrite(rep.data(), 1, rep.size(), fp); fclose(fp); }
		L::lastReport = txt;
		Post("\r\n################ ABSTURZ ################\r\n" + rep.substr(0, rep.find("Module:")) + "Bericht: " + txt + "\r\n#########################################\r\n");
		SetStatus("ABGESTUERZT: " + Hex(er.ExceptionCode) + " " + CodeName(er.ExceptionCode) + "  bei " + Where((DWORD)er.ExceptionAddress), true);
	}

	static DWORD WINAPI DebugThread(LPVOID) {
		if (!DebugActiveProcess(L::pid)) {
			Post("[Logger] Debugger konnte nicht angehaengt werden (Fehler " + std::to_string(GetLastError()) +
			     "). Haengt schon Visual Studio? Absturzfaenger inaktiv, Log und Exit-Code laufen weiter.\r\n");
			L::attached = false; return 0;
		}
		DebugSetProcessKillOnExit(FALSE);        // Logger beenden darf das Spiel NICHT beenden
		L::attached = true;
		Post("[Logger] Absturzfaenger aktiv (als Debugger angehaengt).\r\n");
		bool initialBp = false;
		DWORD lastFirst = 0; int firstCount = 0;
		for (;;) {
			if (L::wantDetach) {
				DebugActiveProcessStop(L::pid); L::attached = false; L::wantDetach = false;
				Post("[Logger] Debugger getrennt -- Visual Studio kann jetzt angehaengt werden.\r\n");
				return 0;
			}
			DEBUG_EVENT ev;
			if (!WaitForDebugEvent(&ev, 200)) continue;
			DWORD cont = DBG_CONTINUE;
			switch (ev.dwDebugEventCode) {
				case CREATE_PROCESS_DEBUG_EVENT:
					L::mainBase = (DWORD)ev.u.CreateProcessInfo.lpBaseOfImage;
					AddModule(L::mainBase, ev.u.CreateProcessInfo.hFile, "Cube.exe");
					if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
					L::threads[ev.dwThreadId] = ev.u.CreateProcessInfo.hThread;
					break;
				case CREATE_THREAD_DEBUG_EVENT: L::threads[ev.dwThreadId] = ev.u.CreateThread.hThread; break;
				case EXIT_THREAD_DEBUG_EVENT: L::threads.erase(ev.dwThreadId); break;
				case LOAD_DLL_DEBUG_EVENT:
					AddModule((DWORD)ev.u.LoadDll.lpBaseOfDll, ev.u.LoadDll.hFile, "?.dll");
					if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
					break;
				case UNLOAD_DLL_DEBUG_EVENT:
					for (size_t i = 0; i < L::mods.size(); i++) if (L::mods[i].base == (DWORD)ev.u.UnloadDll.lpBaseOfDll) { L::mods.erase(L::mods.begin() + i); break; }
					break;
				case EXCEPTION_DEBUG_EVENT: {
					const EXCEPTION_RECORD& er = ev.u.Exception.ExceptionRecord;
					if (!initialBp && er.ExceptionCode == EXCEPTION_BREAKPOINT) { initialBp = true; break; }   // Anhaenge-Haltepunkt
					if (ev.u.Exception.dwFirstChance) {
						cont = DBG_EXCEPTION_NOT_HANDLED;   // das Spiel / der Mod darf selbst behandeln
						if (L::showFirst && er.ExceptionCode != 0x406D1388) {
							DWORD now = GetTickCount();
							if (now - lastFirst > 1000) { lastFirst = now; firstCount = 0; }
							if (++firstCount <= 10)
								Post("[Erste Chance] " + Hex(er.ExceptionCode) + " " + CodeName(er.ExceptionCode) + " bei " + Where((DWORD)er.ExceptionAddress) + "\r\n");
						}
						break;
					}
					CrashReport(ev);
					cont = DBG_EXCEPTION_NOT_HANDLED;
					break;
				}
				case EXIT_PROCESS_DEBUG_EVENT: {
					DWORD code = ev.u.ExitProcess.dwExitCode;
					Post("[Logger] Cube World beendet, Exit-Code " + Hex(code) + "  " + CodeName(code) + "\r\n");
					if (code != 0 && !L::statusBad) SetStatus("Beendet mit " + Hex(code) + "  " + CodeName(code), true);
					else if (code == 0) SetStatus("Cube World normal beendet.", false);
					L::exited = true; L::attached = false;
					ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
					return 0;
				}
				default: break;
			}
			ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
		}
	}

	// ---- Leistungsmesser lesen --------------------------------------------------------
	static PerfShm* PerfView() {
		if (L::perf) return L::perf;
		char n[64]; PerfName(L::pid, n, sizeof(n));
		HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, n);
		if (!m) return nullptr;
		PerfShm* p = (PerfShm*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(PerfShm));
		if (p && p->magic != kPerfMagic) { UnmapViewOfFile(p); p = nullptr; }
		L::perf = p;
		return p;
	}
	// Herzschlaege beobachten (Logger-Timer, alle 200 ms): wann hat sich jeder zuletzt bewegt?
	static void PerfWatchBeats() {
		PerfShm* p = PerfView(); if (!p) return;
		DWORD now = GetTickCount();
		for (int i = 0; i < 3; i++) {
			LONG b = p->beat[i];
			if (b != L::beatSeen[i] || !L::beatTick[i]) { L::beatSeen[i] = b; L::beatTick[i] = now; }
		}
	}
	static std::string BeatLine(int i, const char* name, const char* stuckHint) {
		char b[220];
		DWORD age = L::beatTick[i] ? GetTickCount() - L::beatTick[i] : 0;
		if (!L::beatSeen[i]) sprintf_s(b, "  %-12s noch kein Lebenszeichen\r\n", name);
		else if (age < 1500) sprintf_s(b, "  %-12s laeuft (Zaehler %ld)\r\n", name, L::beatSeen[i]);
		else sprintf_s(b, "  %-12s STEHT seit %.1f s  %s\r\n", name, age / 1000.0, stuckHint);
		return b;
	}
	static std::string PerfSnapshot() {
		std::string o = "\r\n======== Leistung " + Timestamp(false) + " ========\r\n";
		PerfShm* p = PerfView();
		if (!p) return o + "Kein Leistungsmesser gefunden (Spiel mit aelterem Mod oder noch nicht gestartet).\r\n";
		PerfWatchBeats();
		o += "Threads (Herzschlag):\r\n";
		o += BeatLine(0, "Spielthread", "-> Spiel haengt (Bild steht). 'Threads ausgeben' zeigt wo.");
		o += BeatLine(1, "LAB", "-> LAB-Fenster haengt. 'Threads ausgeben' zeigt wo.");
		o += BeatLine(2, "Zonen", "(steht auch, wenn gerade keine Zone erzeugt wird)");
		char b[300];
		LONG z = p->zones, pl = p->places;
		sprintf_s(b, "Zonenerzeugung: %ld Zonen, zuletzt %.1f ms, Mittel %.1f ms, Max %.1f ms\r\n", z, p->zoneLastMs, z ? p->zoneSumMs / z : 0.0f, p->zoneMaxMs); o += b;
		sprintf_s(b, "  davon Mod-Objekte (Baeume/Pflanzen/Modelle): zuletzt %.1f ms, Mittel %.1f ms, Max %.1f ms\r\n", p->placeLastMs, pl ? p->placeSumMs / pl : 0.0f, p->placeMaxMs); o += b;
		LONG rc = p->cnt[PC_RIVER_CALLS], rm = p->cnt[PC_RIVER_MISSES], hc = p->cnt[PC_HEIGHT_CALLS];
		sprintf_s(b, "Gelaende-Hooks bisher: Hoehe %ld Aufrufe, Fluss %ld Aufrufe (davon neu gerechnet %ld = %.0f %%)\r\n", hc, rc, rm, rc ? 100.0 * rm / rc : 0.0); o += b;
		// Text des Spiels (ungerade Folgenummer = wird gerade geschrieben -> kurz warten)
		std::string txt;
		for (int tries = 0; tries < 5; tries++) {
			LONG s1 = p->textSeq; if (s1 & 1) { Sleep(2); continue; }
			char buf[sizeof(p->text)]; memcpy(buf, (const void*)p->text, sizeof(buf)); buf[sizeof(buf) - 1] = 0;
			if (p->textSeq == s1) { txt = buf; break; }
		}
		DWORD age = p->textTick ? GetTickCount() - p->textTick : 0;
		sprintf_s(b, "Bericht des Spielthreads (vor %.1f s):\r\n", age / 1000.0); o += b;
		std::string t2; for (size_t i = 0; i < txt.size(); i++) { if (txt[i] == '\n' && (i == 0 || txt[i - 1] != '\r')) t2 += "\r\n"; else t2 += txt[i]; }
		o += t2.empty() ? std::string("  (noch keiner)\r\n") : t2;
		o += "========================================\r\n";
		return o;
	}
	static void SaveToFile(const char* file, const std::string& s) {
		FILE* fp = nullptr; if (fopen_s(&fp, file, "ab") == 0 && fp) { fwrite(s.data(), 1, s.size(), fp); fclose(fp); }
	}

	// ---- Alle Threads: CPU-Last und Aufrufstapel (zeigt, wo etwas haengt oder rechnet) ----------
	struct DumpMod { DWORD base, size; std::string name; };
	static std::string WhereIn(const std::vector<DumpMod>& mods, DWORD a) {
		for (const DumpMod& m : mods) if (a >= m.base && a < m.base + m.size) {
			char b[200];
			if (_stricmp(m.name.c_str(), "Cube.exe") == 0) sprintf_s(b, "%s+0x%X (Ghidra 0x%X)", m.name.c_str(), a - m.base, a - m.base + 0x400000);
			else sprintf_s(b, "%s+0x%X", m.name.c_str(), a - m.base);
			return b;
		}
		return Hex(a);
	}
	typedef LONG (NTAPI* NtQIT_t)(HANDLE, int, PVOID, ULONG, PULONG);
	static DWORD WINAPI ThreadDumpWorker(LPVOID) {
		std::string o = "\r\n======== Threads von Cube World " + Timestamp(false) + " ========\r\n";
		std::vector<DumpMod> mods;
		HANDLE ms = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, L::pid);
		if (ms != INVALID_HANDLE_VALUE) {
			MODULEENTRY32 me; me.dwSize = sizeof(me);
			if (Module32First(ms, &me)) do { mods.push_back({ (DWORD)(uintptr_t)me.modBaseAddr, me.modBaseSize, me.szModule }); } while (Module32Next(ms, &me));
			CloseHandle(ms);
		}
		std::vector<DWORD> tids;
		HANDLE ts = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (ts != INVALID_HANDLE_VALUE) {
			THREADENTRY32 te; te.dwSize = sizeof(te);
			if (Thread32First(ts, &te)) do { if (te.th32OwnerProcessID == L::pid) tids.push_back(te.th32ThreadID); } while (Thread32Next(ts, &te));
			CloseHandle(ts);
		}
		NtQIT_t ntq = (NtQIT_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
		// CPU-Zeit ueber 1 s messen
		struct T { DWORD id; HANDLE h; ULONGLONG cpu0, cpu1; DWORD start; };
		std::vector<T> th;
		for (DWORD id : tids) {
			HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
			if (!h) continue;
			T t; t.id = id; t.h = h; t.cpu0 = t.cpu1 = 0; t.start = 0;
			FILETIME c, e, k, u; if (GetThreadTimes(h, &c, &e, &k, &u)) t.cpu0 = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
			if (ntq) { DWORD sa = 0; ntq(h, 9 /*ThreadQuerySetWin32StartAddress*/, &sa, sizeof(sa), NULL); t.start = sa; }
			th.push_back(t);
		}
		Sleep(1000);
		for (T& t : th) { FILETIME c, e, k, u; if (GetThreadTimes(t.h, &c, &e, &k, &u)) t.cpu1 = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime); }
		std::sort(th.begin(), th.end(), [](const T& a, const T& b) { return (a.cpu1 - a.cpu0) > (b.cpu1 - b.cpu0); });
		EnterCriticalSection(&L::symCs);
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		static bool symInit = false; if (!symInit) { SymInitialize(L::hProc, NULL, TRUE); symInit = true; }
		char b[300];
		int shown = 0;
		for (T& t : th) {
			if (shown >= 14) { CloseHandle(t.h); continue; }
			double cpu = (double)(t.cpu1 - t.cpu0) / 10000.0 / 10.0;     // 100-ns -> % von 1 s
			std::string start = t.start ? WhereIn(mods, t.start) : std::string("?");
			bool interesting = cpu >= 2.0 || start.find("Cube.exe") != std::string::npos || start.find("AdventureMod") != std::string::npos;
			std::string body;                                          // Stapel erst sammeln, dann entscheiden
			sprintf_s(b, "\r\nThread %u  CPU %.0f %%  Start %s\r\n", t.id, cpu, start.c_str());
			std::string head = b;
			std::string& o0 = o; std::string save = o0; o0.clear();
			if (SuspendThread(t.h) != (DWORD)-1) {
				CONTEXT ctx; memset(&ctx, 0, sizeof(ctx)); ctx.ContextFlags = CONTEXT_FULL;
				if (GetThreadContext(t.h, &ctx)) {
					STACKFRAME64 f; memset(&f, 0, sizeof(f));
					f.AddrPC.Offset = ctx.Eip; f.AddrPC.Mode = AddrModeFlat;
					f.AddrFrame.Offset = ctx.Ebp; f.AddrFrame.Mode = AddrModeFlat;
					f.AddrStack.Offset = ctx.Esp; f.AddrStack.Mode = AddrModeFlat;
					CONTEXT c2 = ctx;
					for (int i = 0; i < 16; i++) {
						if (!StackWalk64(IMAGE_FILE_MACHINE_I386, L::hProc, t.h, &f, &c2, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
						if (!f.AddrPC.Offset) break;
						sprintf_s(b, "    #%-2d %s\r\n", i, WhereIn(mods, (DWORD)f.AddrPC.Offset).c_str()); o += b;
					}
					// Rohsuche: Ruecksprungadressen in Cube.exe / AdventureMod.dll (falls der Rahmen fehlt)
					std::vector<DWORD> st(1024); SIZE_T r = 0;
					ReadProcessMemory(L::hProc, (LPCVOID)(uintptr_t)ctx.Esp, st.data(), st.size() * 4, &r);
					int found = 0;
					for (size_t i = 0; i < r / 4 && found < 8; i++) {
						std::string w = WhereIn(mods, st[i]);
						if (w.find("Cube.exe") == std::string::npos && w.find("AdventureMod") == std::string::npos) continue;
						sprintf_s(b, "    [ESP+0x%03X] %s\r\n", (unsigned)(i * 4), w.c_str()); o += b; found++;
					}
				}
				ResumeThread(t.h);
			}
			body = o0; o0 = save;
			// Auch Spiel-Threads, die ueber msvcr110 gestartet wurden: zeigen, wenn ihr Stapel durch Cube.exe/den Mod laeuft
			if (interesting || body.find("Cube.exe") != std::string::npos || body.find("AdventureMod") != std::string::npos) { o += head + body; ++shown; }
			CloseHandle(t.h);
		}
		LeaveCriticalSection(&L::symCs);
		o += "\r\n(Threads mit Start in Cube.exe/AdventureMod.dll oder >= 2 % CPU, nach CPU-Last sortiert)\r\n========================================\r\n";
		SaveToFile("AdventureMod_leistung.txt", o);
		Post(o + "[Logger] Auch gespeichert in AdventureMod_leistung.txt\r\n");
		InterlockedExchange(&L::dumpBusy, 0);
		return 0;
	}

	static void AppendToEdit(const std::string& raw) {
		if (raw.empty()) return;
		std::string s; s.reserve(raw.size() + 64);
		for (size_t i = 0; i < raw.size(); i++) {
			char c = raw[i];
			if (c == '\n' && (i == 0 || raw[i - 1] != '\r')) s += "\r\n"; else s += c;
		}
		int len = GetWindowTextLengthA(L::log);
		if (len > 1500000) { SendMessageA(L::log, EM_SETSEL, 0, 700000); SendMessageA(L::log, EM_REPLACESEL, FALSE, (LPARAM)""); len = GetWindowTextLengthA(L::log); }
		SendMessageA(L::log, EM_SETSEL, len, len);
		SendMessageA(L::log, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
	}

	static void TailLog() {
		HANDLE f = CreateFileA(kLogFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE) return;
		LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
		if (sz.QuadPart < L::logPos) L::logPos = 0;                  // neue Datei (Spiel neu gestartet)
		if (sz.QuadPart > L::logPos) {
			LONGLONG n = sz.QuadPart - L::logPos; if (n > 4 * 1024 * 1024) { L::logPos = sz.QuadPart - 4 * 1024 * 1024; n = 4 * 1024 * 1024; }
			std::string buf((size_t)n, '\0');
			LARGE_INTEGER p; p.QuadPart = L::logPos; SetFilePointerEx(f, p, NULL, FILE_BEGIN);
			DWORD r = 0; ReadFile(f, &buf[0], (DWORD)n, &r, NULL); buf.resize(r);
			L::logPos += r;
			EnterCriticalSection(&L::cs);
			L::tail += buf; if (L::tail.size() > 16384) L::tail.erase(0, L::tail.size() - 16384);
			LeaveCriticalSection(&L::cs);
			AppendToEdit(buf);
		}
		CloseHandle(f);
	}

	static void Layout() {
		RECT rc; GetClientRect(L::wnd, &rc);
		int w = rc.right, h = rc.bottom;
		MoveWindow(L::status, 8, 6, w - 16, 26, TRUE);
		MoveWindow(L::info, 8, 34, w - 16, 16, TRUE);
		int bw = 150;
		MoveWindow(L::btnDbg, 8, 54, bw, 26, TRUE);
		MoveWindow(GetDlgItem(L::wnd, L::IDB_DIR), 8 + (bw + 4), 54, 110, 26, TRUE);
		MoveWindow(GetDlgItem(L::wnd, L::IDB_REPORT), 8 + (bw + 4) + 114, 54, 130, 26, TRUE);
		MoveWindow(GetDlgItem(L::wnd, L::IDB_PREV), 8 + (bw + 4) + 248, 54, 130, 26, TRUE);
		MoveWindow(GetDlgItem(L::wnd, L::IDB_CLEAR), 8 + (bw + 4) + 382, 54, 100, 26, TRUE);
		MoveWindow(L::chkFirst, 8 + (bw + 4) + 488, 57, 220, 20, TRUE);
		MoveWindow(L::btnLaa, (std::max)(8 + (bw + 4) + 712, (int)w - 8 - 230), 54, 230, 26, TRUE);
		MoveWindow(L::btnPerf, 8, 84, bw, 26, TRUE);
		MoveWindow(L::btnThreads, 8 + (bw + 4), 84, 244, 26, TRUE);
		MoveWindow(L::chkPerfAuto, 8 + (bw + 4) + 252, 87, 220, 20, TRUE);
		MoveWindow(L::log, 8, 116, w - 16, h - 124, TRUE);
	}

	static LRESULT CALLBACK LoggerProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				L::wnd = h;
				auto mk = [&](const char* cls, const char* t, DWORD st, int id, DWORD ex = 0) {
					HWND c = CreateWindowExA(ex, cls, t, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, NULL, NULL);
					SendMessageA(c, WM_SETFONT, (WPARAM)L::font, TRUE); return c;
				};
				L::status = mk("STATIC", "Verbinde ...", SS_LEFT, -1); SendMessageA(L::status, WM_SETFONT, (WPARAM)L::fontBig, TRUE);
				char d[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, d);
				std::string inf = std::string("Eigener Prozess -- bleibt offen, wenn Cube World abstuerzt.  Logdatei: ") + d + "\\" + kLogFile;
				L::info = mk("STATIC", inf.c_str(), SS_LEFT | SS_PATHELLIPSIS, -1);
				L::btnDbg = mk("BUTTON", "Debugger trennen", BS_PUSHBUTTON, L::IDB_DBG);
				mk("BUTTON", "Ordner oeffnen", BS_PUSHBUTTON, L::IDB_DIR);
				mk("BUTTON", "Letzten Bericht", BS_PUSHBUTTON, L::IDB_REPORT);
				mk("BUTTON", "Log der Vorrunde", BS_PUSHBUTTON, L::IDB_PREV);
				mk("BUTTON", "Ansicht leeren", BS_PUSHBUTTON, L::IDB_CLEAR);
				L::chkFirst = mk("BUTTON", "Erste-Chance-Ausnahmen zeigen", BS_AUTOCHECKBOX, L::IDB_FIRST);
				L::btnLaa = mk("BUTTON", "4 GB ...", BS_PUSHBUTTON, L::IDB_LAA);
				L::btnPerf = mk("BUTTON", "Leistung ausgeben", BS_PUSHBUTTON, L::IDB_PERF);
				L::btnThreads = mk("BUTTON", "Threads ausgeben (Haenger / Last)", BS_PUSHBUTTON, L::IDB_THREADS);
				L::chkPerfAuto = mk("BUTTON", "Leistung alle 10 s ausgeben", BS_AUTOCHECKBOX, L::IDB_PERFAUTO);
				L::log = mk("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL, -1, WS_EX_CLIENTEDGE);
				SendMessageA(L::log, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);
				HFONT mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
				SendMessageA(L::log, WM_SETFONT, (WPARAM)mono, TRUE);
				Layout();
				SetTimer(h, L::IDT_TAIL, 200, NULL);
				return 0;
			}
			case WM_SIZE: Layout(); return 0;
			case WM_CTLCOLORSTATIC:
				if ((HWND)lp == L::status) {
					HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
					EnterCriticalSection(&L::cs); bool bad = L::statusBad; LeaveCriticalSection(&L::cs);
					SetTextColor(dc, bad ? RGB(200, 0, 0) : RGB(0, 120, 0));
					return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
				}
				break;
			case WM_COMMAND:
				switch (LOWORD(wp)) {
					case L::IDB_DBG:
						if (L::attached) { L::wantDetach = true; }
						else if (!L::exited) {
							if (L::dbgThread) { WaitForSingleObject(L::dbgThread, 1000); CloseHandle(L::dbgThread); }
							L::dbgThread = CreateThread(NULL, 0, DebugThread, NULL, 0, NULL);
						}
						break;
					case L::IDB_DIR: { char d[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, d); ShellExecuteA(h, "open", d, NULL, NULL, SW_SHOWNORMAL); break; }
					case L::IDB_REPORT: {
						std::string r = L::lastReport;
						if (r.empty()) {   // sonst den neuesten Bericht im Ordner nehmen
							WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA("AdventureMod_crash_*.txt", &fd); FILETIME best = { 0, 0 };
							if (fh != INVALID_HANDLE_VALUE) { do { if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) { best = fd.ftLastWriteTime; r = fd.cFileName; } } while (FindNextFileA(fh, &fd)); FindClose(fh); }
							if (r.empty() && GetFileAttributesA(kInProc) != INVALID_FILE_ATTRIBUTES) r = kInProc;
						}
						if (r.empty()) MessageBoxA(h, "Noch kein Absturzbericht vorhanden.", "LAB-Logger", MB_ICONINFORMATION);
						else ShellExecuteA(h, "open", "notepad.exe", r.c_str(), NULL, SW_SHOWNORMAL);
						break;
					}
					case L::IDB_PREV: ShellExecuteA(h, "open", "notepad.exe", kLogPrev, NULL, SW_SHOWNORMAL); break;
					case L::IDB_CLEAR: SetWindowTextA(L::log, ""); break;
					case L::IDB_LAA: {
						int cur = ExeIsLargeAddressAware(L::exePath.c_str());
						if (cur < 0) { MessageBoxA(h, ("Cube.exe nicht lesbar:\n" + L::exePath).c_str(), "4 GB", MB_ICONERROR); break; }
						if (!L::exited) { MessageBoxA(h, "Erst Cube World beenden -- die laufende Cube.exe ist gesperrt.\n"
							"Der Logger bleibt danach offen; dann hier erneut klicken.", "4 GB", MB_ICONINFORMATION); break; }
						std::string q = cur ? std::string("4 GB (Large Address Aware) wieder AUSSCHALTEN?\n\nDann gilt wieder die Vanilla-Grenze von 2 GB.")
							: std::string("Cube.exe auf 4 GB Adressraum erweitern (Large Address Aware)?\n\n"
							  "Was passiert: In der Datei wird ein einziges Bit im PE-Kopf gesetzt.\n"
							  "Vorher wird Cube.exe.vor-4GB.bak angelegt. Rueckgaengig: hier erneut klicken.\n\n"
							  "Warum: Beide Abstuerze (01.10. und 03.10.) waren std::bad_alloc -- 2 GB voll.\n"
							  "Risiko: Spielcode, der Zeiger als Vorzeichenzahl prueft, ist nicht ausgeschlossen.\n\n") + L::exePath;
						if (MessageBoxA(h, q.c_str(), "4 GB", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						char msg[256] = "";
						bool ok = SetExeLargeAddressAware(L::exePath.c_str(), !cur, msg, sizeof(msg));
						AppendToEdit(std::string("[Logger] ") + msg + "\r\n");
						MessageBoxA(h, msg, "4 GB", ok ? MB_ICONINFORMATION : MB_ICONERROR);
						break;
					}
					case L::IDB_FIRST: L::showFirst = SendMessageA(L::chkFirst, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
					case L::IDB_PERF: { std::string s = PerfSnapshot(); SaveToFile("AdventureMod_leistung.txt", s); AppendToEdit(s + "[Logger] Auch gespeichert in AdventureMod_leistung.txt\r\n"); break; }
					case L::IDB_THREADS:
						if (L::exited) { AppendToEdit("[Logger] Cube World laeuft nicht.\r\n"); break; }
						if (InterlockedExchange(&L::dumpBusy, 1)) break;
						AppendToEdit("[Logger] Messe 1 s lang die CPU-Last aller Threads und lese ihre Aufrufstapel ...\r\n");
						{ HANDLE t = CreateThread(NULL, 0, ThreadDumpWorker, NULL, 0, NULL); if (t) CloseHandle(t); else InterlockedExchange(&L::dumpBusy, 0); }
						break;
				}
				return 0;
			case WM_TIMER:
			case L::WM_LOGGER_POKE: {
				TailLog();
				PerfWatchBeats();
				if (!L::exited && SendMessageA(L::chkPerfAuto, BM_GETCHECK, 0, 0) == BST_CHECKED && GetTickCount() - L::perfAutoLast > 10000) {
					L::perfAutoLast = GetTickCount();
					std::string s = PerfSnapshot(); SaveToFile("AdventureMod_leistung.txt", s); AppendToEdit(s);
				}
				std::string p, st; bool bad;
				EnterCriticalSection(&L::cs); p.swap(L::pending); st = L::statusText; bad = L::statusBad; LeaveCriticalSection(&L::cs);
				AppendToEdit(p);
				// Ohne Debugger: Prozessende ueber das Handle erkennen
				if (!L::attached && !L::exited && L::hProc && WaitForSingleObject(L::hProc, 0) == WAIT_OBJECT_0) {
					DWORD code = 0; GetExitCodeProcess(L::hProc, &code); L::exited = true;
					AppendToEdit("[Logger] Cube World beendet, Exit-Code " + Hex(code) + "  " + CodeName(code) + "\r\n");
					st = (code == 0 || code == 0xC000013A) ? std::string("Cube World beendet.") : ("Beendet mit " + Hex(code) + "  " + CodeName(code));
					bad = !(code == 0 || code == 0xC000013A);
					EnterCriticalSection(&L::cs); L::statusText = st; L::statusBad = bad; LeaveCriticalSection(&L::cs);
				}
				if (!L::exited && !bad) st = std::string("Cube World laeuft (PID ") + std::to_string(L::pid) + ")  --  " + (L::attached ? "Absturzfaenger aktiv" : "nur Log (Debugger getrennt)");
				char cur[512]; GetWindowTextA(L::status, cur, sizeof(cur));
				if (st != cur) { SetWindowTextA(L::status, st.c_str()); InvalidateRect(L::status, NULL, TRUE); }
				SetWindowTextA(L::btnDbg, L::attached ? "Debugger trennen" : (L::exited ? "(Spiel beendet)" : "Debugger anhaengen"));
				{
					static DWORD lastLaa = 0; DWORD now = GetTickCount();
					if (now - lastLaa > 2000) {
						lastLaa = now;
						int f = ExeIsLargeAddressAware(L::exePath.c_str());
						const char* t = f == 1 ? "4 GB: AN (Datei) -- ausschalten" : f == 0 ? "4 GB freischalten (2 GB voll?)" : "4 GB: Cube.exe ?";
						char cur2[96]; GetWindowTextA(L::btnLaa, cur2, sizeof(cur2));
						if (strcmp(cur2, t) != 0) SetWindowTextA(L::btnLaa, t);
					}
				}
				return 0;
			}
			case WM_CLOSE:
				if (L::attached) { L::wantDetach = true; if (L::dbgThread) WaitForSingleObject(L::dbgThread, 1500); }
				DestroyWindow(h); return 0;
			case WM_DESTROY: PostQuitMessage(0); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	static void LoggerMain(DWORD pid) {
		InitializeCriticalSection(&L::cs);
		InitializeCriticalSection(&L::symCs);
		L::pid = pid;
		char mx[64]; sprintf_s(mx, "Local\\CWLabLogger_%u", pid);
		HANDLE mutex = CreateMutexA(NULL, TRUE, mx);
		if (GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return; }   // laeuft schon
		L::hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE | PROCESS_DUP_HANDLE, FALSE, pid);
		if (L::hProc) { char ep[MAX_PATH] = ""; DWORD n = MAX_PATH; if (QueryFullProcessImageNameA(L::hProc, 0, ep, &n)) L::exePath = ep; }
		L::font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
		L::fontBig = CreateFontA(-17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
		WNDCLASSEXA wc; memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = LoggerProc; wc.hInstance = GetModuleHandleA(NULL); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.lpszClassName = "CWLabLogger"; wc.hIcon = LoadIcon(NULL, IDI_WARNING);
		RegisterClassExA(&wc);
		RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
		int ww = (std::min)(1100, (int)(wa.right - wa.left)), wh = (std::min)(600, (int)(wa.bottom - wa.top));
		char title[96]; sprintf_s(title, "Cube World LAB -- Logger & Absturzfaenger  (PID %u)", pid);
		HWND h = CreateWindowExA(0, "CWLabLogger", title, WS_OVERLAPPEDWINDOW, wa.right - ww, wa.bottom - wh, ww, wh, NULL, NULL, wc.hInstance, NULL);
		ShowWindow(h, SW_SHOWNOACTIVATE);
		L::dbgThread = CreateThread(NULL, 0, DebugThread, NULL, 0, NULL);
		MSG msg;
		while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
		if (mutex) CloseHandle(mutex);
	}
}

// 4 GB (Large Address Aware) automatisch (2026-10-29): Die laufende Cube.exe ist gesperrt. Deshalb startet das Spiel
// diesen kleinen Helfer (rundll32 ... ,LabLaaHelper <pid>, kein Fenster): Er wartet, bis Cube World beendet ist, setzt
// dann das LAA-Bit (SetExeLargeAddressAware, Sicherung Cube.exe.vor-4GB.bak) und schreibt das Ergebnis nach
// AdventureMod_4gb.txt. Gilt ab dem naechsten Start.
#pragma comment(linker, "/EXPORT:LabLaaHelper=_LabLaaHelper@16")
extern "C" void CALLBACK LabLaaHelper(HWND, HINSTANCE, LPSTR cmdLine, int) {
	DWORD pid = cmdLine ? (DWORD)strtoul(cmdLine, nullptr, 10) : 0;
	if (!pid) return;
	HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h) return;
	char exe[MAX_PATH] = ""; DWORD n = MAX_PATH;
	if (!QueryFullProcessImageNameA(h, 0, exe, &n)) { CloseHandle(h); return; }
	WaitForSingleObject(h, INFINITE); CloseHandle(h);
	char msg[256] = ""; bool ok = false;
	for (int i = 0; i < 20 && !ok; i++) { Sleep(1000); ok = CrashLogger::SetExeLargeAddressAware(exe, true, msg, sizeof(msg)); }
	std::string note = std::string(exe); size_t cut = note.find_last_of('\\'); note = (cut == std::string::npos ? std::string() : note.substr(0, cut + 1)) + "AdventureMod_4gb.txt";
	HANDLE f = CreateFileA(note.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, msg, (DWORD)strlen(msg), &w, NULL); CloseHandle(f); }
}
namespace CrashLogger {
	// Im Spiel: soll 4 GB an sein und ist die Cube.exe noch ohne LAA-Bit -> Helfer fuer nach dem Beenden starten.
	void EnsureLargeAddressAware(bool want) {
		static volatile LONG done = 0; if (InterlockedExchange(&done, 1)) return;
		char exe[MAX_PATH] = ""; GetModuleFileNameA(NULL, exe, MAX_PATH);
		int cur = ExeIsLargeAddressAware(exe);
		char b[300];
		if (cur == 1) { { const char* t = "[4GB] Cube.exe hat 4 GB Adressraum (Large Address Aware).\n"; WriteLog(t, strlen(t)); } return; }
		if (!want || cur < 0) return;
		char sys[MAX_PATH] = "";
		if (!GetSystemWow64DirectoryA(sys, MAX_PATH)) GetSystemDirectoryA(sys, MAX_PATH);
		char dll[MAX_PATH] = ""; GetModuleFileNameA(g_dll, dll, MAX_PATH);
		char cmd[3 * MAX_PATH]; wsprintfA(cmd, "\"%s\\rundll32.exe\" \"%s\",LabLaaHelper %u", sys, dll, GetCurrentProcessId());
		STARTUPINFOA si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
		PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
		bool ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dir[0] ? g_dir : NULL, &si, &pi) != 0;
		if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
		wsprintfA(b, "[4GB] Grundeinstellung 4 GB: %s\n", ok ? "wird nach dem Beenden von Cube World eingeschaltet (gilt ab dem naechsten Start)." : "Helfer nicht startbar.");
		WriteLog(b, strlen(b));
	}
}
extern "C" void CALLBACK LabLoggerMain(HWND, HINSTANCE, LPSTR cmdLine, int) {
	DWORD pid = cmdLine ? (DWORD)strtoul(cmdLine, nullptr, 10) : 0;
	if (!pid) { MessageBoxA(NULL, "LabLoggerMain: keine Prozess-ID uebergeben.", "LAB-Logger", MB_ICONERROR); return; }
	CrashLogger::LoggerMain(pid);
}
