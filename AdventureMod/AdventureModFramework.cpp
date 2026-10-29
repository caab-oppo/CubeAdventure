#include "AdventureModFramework.h"
#include "Host.h"
#include "CrashLogger.h"
#include <typeinfo>
#include <windows.h>
#include <iostream>

namespace AdventureFramework {

	static std::vector<IAdventureMod*> g_mods;
	static std::vector<double> g_tickMs;           // ms je Baustein (gleitend)
	static bool g_initialized = false;

	// ---------------------------------------------------------------
	// SICHERHEITS-UMBAU (siehe README.md, "Absturz-Fix"): Der GenerateZone-
	// Hook loest einen Code-Pfad aus (DispatchZoneGenerated -> World::
	// GetZone() -> cube_funcs::world_getzone, imageBase+0x34D10), dessen
	// Adresse NIE gegen dieses Cube.exe-Build verifiziert wurde -- 1:1 aus
	// dem oeffentlichen cwsdk-Repo uebernommen. War deshalb testweise
	// deaktiviert. AUF NUTZERWUNSCH REAKTIVIERT (true) -- der Verdacht ist,
	// dass der vorherige Absturz stattdessen am selbstgebauten Mod-Launcher
	// lag (statt am zur cwsdk gehoerenden) und nicht an dieser Adresse.
	// Die Sicherheitsnetze bleiben trotzdem aktiv: Byte-Signatur-Pruefung
	// vor dem Patchen (VerifyCallsiteBytes()) und SEH um jeden riskanten
	// nativen Aufruf (siehe Hooked_GenerateZone/SafeGetZone unten) -- ein
	// falscher world_getzone-Offset wuerde also weiterhin abgefangen und
	// geloggt statt das Spiel abstuerzen zu lassen. Bei erneuten Abstuerzen:
	// zuerst die Konsolen-Logzeile unmittelbar davor pruefen, dann ggf.
	// wieder auf false setzen.
	// ---------------------------------------------------------------
	static const bool kEnableZoneHook = true;

	// ---------------------------------------------------------------
	// Verifizierte Konstanten (siehe Header + README.md fuer die
	// Byte-Verifikation gegen die lokale Cube.exe).
	// ---------------------------------------------------------------
	static const uintptr_t GENERATE_ZONE_CALLSITE_OFFSET = 0x0006ACD1; // 5-Byte CALL-Instruktion
	static const uintptr_t GENERATE_ZONE_FUNC_OFFSET      = 0x001E4850; // World::GenerateZone(world*, x, y)
	// Erwartete Original-Bytes an der Aufrufstelle (E8 = CALL rel32, Rest =
	// verifizierter rel32-Offset auf GENERATE_ZONE_FUNC_OFFSET). Wird vor
	// dem Patchen geprueft, siehe VerifyCallsiteBytes().
	static const BYTE EXPECTED_CALLSITE_BYTES[5] = { 0xE8, 0x7A, 0x9B, 0x17, 0x00 };

	// Original-GenerateZone, __thiscall: ECX = World*, Stack = x, y.
	typedef void(__thiscall* GenerateZone_t)(void* world_ptr, uint32_t x, uint32_t y);
	static GenerateZone_t Original_GenerateZone = nullptr;
	static bool g_hookInstalled = false;

	// -----------------------------------------------------------------
	// SEH-isolierte Helfer (siehe cube_util.cpp fuer dasselbe Muster und
	// die Begruendung: MSVC C2712 verbietet __try/__except im selben Frame
	// wie Objekte mit Destruktor, deshalb eigene, "schlanke" Funktionen).
	// -----------------------------------------------------------------
	static bool SafeCallOriginalGenerateZone(void* world_ptr, uint32_t x, uint32_t y) {
		__try {
			Original_GenerateZone(world_ptr, x, y);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AdventureModFramework] FEHLER: Original-GenerateZone (+0x1E4850) abgestuerzt "
				// "(SEH abgefangen) -- Zone evtl. nicht korrekt generiert." << std::endl;
			return false;
		}
	}

	static cube::Zone* SafeGetZone(cube::World* world, int x, int y) {
		__try {
			return world->GetZone(x, y);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AdventureModFramework] FEHLER: World::GetZone() (nutzt intern "
				// "cube_funcs::world_getzone, +0x34D10, UNVERIFIZIERT) abgestuerzt (SEH abgefangen)." << std::endl;
			return nullptr;
		}
	}

	static bool SafeLockWorld(cube::World* world) {
		__try {
			world->Lock();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AdventureModFramework] FEHLER: World::Lock() abgestuerzt (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	static void SafeUnlockWorld(cube::World* world) {
		__try {
			world->Unlock();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AdventureModFramework] FEHLER: World::Unlock() abgestuerzt (SEH abgefangen) -- "
				// "World bleibt evtl. dauerhaft gesperrt!" << std::endl;
		}
	}

	static void DispatchZoneGenerated(void* world_ptr, uint32_t x, uint32_t y) {
		cube::GameController* gc = cube::GetGameController();
		if (!gc) return;

		cube::World* world = (cube::World*)world_ptr;

		if (!SafeLockWorld(world)) return;
		cube::Zone* zone = SafeGetZone(world, (int)x, (int)y);
		SafeUnlockWorld(world);

		for (IAdventureMod* mod : g_mods) {
			mod->OnZoneGenerated(gc, world, (int)x, (int)y, zone);
		}
	}

	// -----------------------------------------------------------------
	// Interposer-Funktion fuer die GenerateZone-Aufrufstelle.
	//
	// Technik: Der Original-Callsite ruft GenerateZone per __thiscall auf
	// (this/World* in ECX, x und y auf dem Stack). Deklariert man die
	// eigene Interposer-Funktion als __fastcall mit einem ungenutzten
	// zweiten Parameter, landet ECX (World*) im ersten Parameter und der
	// Stack-Rest (x, y) exakt wie bei __thiscall in den folgenden
	// Parametern - eine seit Jahren etablierte Technik, um thiscall-
	// Aufrufstellen ohne Inline-Assembler abzufangen.
	// -----------------------------------------------------------------
	static void __fastcall Hooked_GenerateZone(void* world_ptr, void* /*unused_edx*/, uint32_t x, uint32_t y) {
		// Erst die echte Originalfunktion laufen lassen - die Zone existiert
		// erst danach wirklich. SEH-isoliert (siehe oben): ein Absturz hier
		// wird abgefangen statt den ganzen Prozess mitzureissen.
		LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
		CrashLogger::PerfBeat(CrashLogger::BEAT_ZONE);
		bool ok = SafeCallOriginalGenerateZone(world_ptr, x, y);
		QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
		CrashLogger::PerfZoneGen((double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart);   // fuer den Logger
		if (!ok) {
			return;
		}

		DispatchZoneGenerated(world_ptr, x, y);
	}

	static void WriteCallPatch(BYTE* location, void* newFunction) {
		DWORD oldProtect;
		VirtualProtect(location, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
		location[0] = 0xE8; // CALL rel32
		*((DWORD*)(location + 1)) = (DWORD)((uintptr_t)newFunction - (uintptr_t)location - 5);
		VirtualProtect(location, 5, oldProtect, &oldProtect);
	}

	// Prueft, ob an der Aufrufstelle wirklich noch die erwarteten,
	// per Hex-Dump verifizierten Original-Bytes stehen, BEVOR gepatcht
	// wird. Schuetzt vor blindem Patchen bei einer abweichenden
	// Cube.exe-Version/einem anderen Patch-Level (siehe README.md).
	static bool VerifyCallsiteBytes(BYTE* location) {
		__try {
			for (int i = 0; i < 5; i++) {
				if (location[i] != EXPECTED_CALLSITE_BYTES[i]) {
					return false;
				}
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AdventureModFramework] FEHLER: Aufrufstelle nicht lesbar (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	void RegisterMod(IAdventureMod* mod) {
		if (mod) g_mods.push_back(mod);
	}

	void InstallZoneGeneratedHook() {
		if (!kEnableZoneHook) {
			// std::cout << "[AdventureModFramework] GenerateZone-Hook ist DEAKTIVIERT (kEnableZoneHook=false, "
				// "siehe README.md \"Absturz-Fix\" -- world_getzone-Offset unverifiziert). "
				// "OnZoneGenerated() wird fuer keinen Mod-Baustein feuern." << std::endl;
			return;
		}

		if (Host::IsServer()) return;   // Aufrufstelle liegt in der Client-Hauptschleife; im Server gibt es sie nicht
		BYTE* callsite = (BYTE*)Host::ReOff(GENERATE_ZONE_CALLSITE_OFFSET);

		if (!VerifyCallsiteBytes(callsite)) {
			// std::cout << "[AdventureModFramework] FEHLER: Bytes an der GenerateZone-Aufrufstelle (0x"
				// << std::hex << (imageBase + GENERATE_ZONE_CALLSITE_OFFSET) << std::dec
				// << ") stimmen NICHT mit den erwarteten E8 7A 9B 17 00 ueberein -- "
				// "Hook wird NICHT installiert (andere Cube.exe-Version/Patch-Level?)." << std::endl;
			return;
		}

		Original_GenerateZone = (GenerateZone_t)Host::ReOff(GENERATE_ZONE_FUNC_OFFSET);
		WriteCallPatch(callsite, &Hooked_GenerateZone);
		g_hookInstalled = true;

		// std::cout << "[AdventureModFramework] GenerateZone-Hook installiert (Callsite 0x"
			// << std::hex << (imageBase + GENERATE_ZONE_CALLSITE_OFFSET) << std::dec << ")." << std::endl;
	}

	static volatile int g_currentMod = -1;

	void DispatchGameTick(cube::GameController* gc) {
		if (!gc) return;

		if (!g_initialized) {
			g_initialized = true;
			for (IAdventureMod* mod : g_mods) {
				mod->OnInitialize(gc);
			}
		}

		// Jeden Baustein einzeln messen (Hochleistungszaehler). Kostet ~2 x 20 ns
		// je Baustein und Bild -- vernachlaessigbar, zeigt aber genau, wer bremst.
		static LARGE_INTEGER freq = { 0 };
		if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
		if (g_tickMs.size() != g_mods.size()) g_tickMs.assign(g_mods.size(), 0.0);
		for (size_t i = 0; i < g_mods.size(); i++) {
			LARGE_INTEGER t0, t1;
			QueryPerformanceCounter(&t0);
			g_currentMod = (int)i;                          // fuer die Fehlermeldung, falls dieser Baustein abstuerzt
			g_mods[i]->OnGameTick(gc);
			QueryPerformanceCounter(&t1);
			double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
			g_tickMs[i] = g_tickMs[i] * 0.95 + ms * 0.05;    // gleitender Mittelwert ~20 Bilder
		}
		g_currentMod = -1;
	}

	const char* CurrentModName() {
		static std::string n;
		int i = g_currentMod;
		if (i < 0 || i >= (int)g_mods.size()) return "(kein Baustein / Initialisierung)";
		const char* raw = typeid(*g_mods[i]).name();
		n = raw ? raw : "?";
		if (n.compare(0, 6, "class ") == 0) n = n.substr(6);
		return n.c_str();
	}

	void GetTickTimings(std::vector<std::pair<std::string, double>>& out) {
		out.clear();
		for (size_t i = 0; i < g_mods.size() && i < g_tickMs.size(); i++) {
			const IAdventureMod& m = *g_mods[i];
			const char* n = typeid(m).name();                // "class MountainVillage::MountainVillageMod"
			std::string name = n ? n : "?";
			if (name.compare(0, 6, "class ") == 0) name = name.substr(6);
			out.push_back(std::make_pair(name, g_tickMs[i]));
		}
	}
}
