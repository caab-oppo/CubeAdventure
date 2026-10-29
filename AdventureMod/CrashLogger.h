#pragma once
//
// CrashLogger.h
// ---------------------------------------------------------------
// Logger + Absturzfaenger, der einen Absturz von Cube World UEBERLEBT.
//
// Warum ein eigener Prozess: Das LAB laeuft als Thread IN Cube.exe. Stuerzt
// das Spiel ab, endet der ganze Prozess -- mit allen Fenstern darin. Was den
// Absturz ueberleben soll, muss ausserhalb leben.
//
// Aufbau:
//   Im Spiel (InitInGame):
//     * jede Konsolenzeile geht sofort per WriteFile nach AdventureMod_log.txt
//       (Betriebssystem-Puffer -> bleibt auch erhalten, wenn der Prozess stirbt)
//     * SetUnhandledExceptionFilter: schreibt AdventureMod_crash_inproc.txt,
//       falls kein Debugger angehaengt ist
//     * startet den externen Logger:  rundll32.exe AdventureMod.dll,LabLoggerMain <pid>
//   Im Logger-Prozess (LabLoggerMain):
//     * eigenes Fenster, zeigt AdventureMod_log.txt live an
//     * haengt sich als DEBUGGER an Cube.exe. Damit sieht er auch Abstuerze ohne
//       Fehlermeldung (Fail-Fast 0xC0000409, Heap-Korruption 0xC0000374,
//       Stapelueberlauf 0xC00000FD, 0xC000041D in Win32-Callbacks ...).
//     * beim Absturz: Bericht (Ausnahme, Ort als Modul+Offset und Ghidra-Adresse,
//       Register, Aufrufstapel, letzte Logzeilen) + Minidump fuer Visual Studio
//     * bleibt offen, wenn Cube World endet; zeigt den Exit-Code mit Bedeutung
//     * "Debugger trennen" gibt den Prozess fuer Visual Studio frei
//
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace CrashLogger {
	// Laeuft diese DLL gerade im Logger-Prozess (rundll32)? Dann darf DllMain den
	// Mod NICHT starten.
	bool IsLoggerHost();
	// 4 GB (LAA) per Grundeinstellung: ist das Bit nicht gesetzt, setzt es ein Helfer nach dem Beenden des Spiels.
	void EnsureLargeAddressAware(bool want);

	// Im Spiel, einmal frueh in MainThread.
	void InitInGame(HMODULE dll);

	// Vom cout-Abzweig (LabBridge) fuer jedes Stueck Text.
	void WriteLog(const char* s, size_t n);

	// Externen Logger (neu) starten bzw. nach vorne holen.
	bool StartLogger();
	bool LoggerRunning();

	// ---- Leistungsmesser fuer den Logger (gemeinsamer Speicher "Local\\CWLabPerf_<pid>") ----
	// Das Spiel schreibt, der Logger liest -- auch wenn das LAB haengt. Alles billig und ohne Sperren.
	enum PerfBeatId { BEAT_GAME = 0, BEAT_LAB = 1, BEAT_ZONE = 2 };
	enum PerfCounterId { PC_RIVER_CALLS = 0, PC_RIVER_MISSES = 1, PC_HEIGHT_CALLS = 2, PC_COUNT = 8 };
	void PerfBeat(int which);                        // Herzschlag eines Threads
	void PerfCount(int which, long n = 1);           // Zaehler (nicht atomar genau, reicht zum Schaetzen)
	void PerfZoneGen(double ms);                     // Dauer einer ganzen Zonenerzeugung (Zonen-Thread)
	void PerfZonePlace(double ms);                   // davon: Mod-Objekte setzen (Hook_SunCall)
	void PerfPublishText(const char* text);          // Leistungsbericht als Text (Spielthread, ~1x je Sekunde)

	// ---- Adressraum (fuer In-Spiel-Waechter und Absturzbericht) ----------
	// Cube.exe ist ohne "Large Address Aware" auf 2 GB begrenzt. Der Absturz vom
	// 2026-10-01 und 2026-10-03 war beide Male std::bad_alloc aus operator new
	// im Kartenthread (0x469959 -> loadLandscapeTile -> VoxelModel_build_mesh).
	struct AddrSpace {
		unsigned freeMB = 0, largestFreeMB = 0, committedMB = 0, reservedMB = 0, imageMB = 0;
		unsigned limitMB = 0;           // hoechste Nutzeradresse + 1, in MB
		bool ok = false;
	};
	AddrSpace QueryAddrSpace(HANDLE process, uintptr_t maxAddr = 0);   // GetCurrentProcess() fuer das Spiel selbst
	// 1 = LAA gesetzt, 0 = nicht, -1 = unlesbar. Liest die Datei (nicht den Speicher).
	int  ExeIsLargeAddressAware(const char* exePath);
	// Setzt/loescht das LAA-Bit in der Datei. Sicherung "<exe>.vor-4GB.bak" beim ersten Mal.
	// Schlaegt fehl, solange das Spiel laeuft (Datei gesperrt).
	bool SetExeLargeAddressAware(const char* exePath, bool on, char* msg, int msgCap);
}

// Einstieg fuer rundll32 (exportiert als "LabLoggerMain").
extern "C" void CALLBACK LabLoggerMain(HWND hwnd, HINSTANCE hinst, LPSTR cmdLine, int nShow);
