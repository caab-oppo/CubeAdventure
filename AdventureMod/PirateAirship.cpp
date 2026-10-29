#include "PirateAirship.h"
#include "Host.h"
#include <windows.h>
#include <iostream>
#include <cstring>

// ===================================================================
// PirateAirship.cpp  (Phase 3: Schiff = natives Airship-Entity)
// -------------------------------------------------------------------
// AirshipMap-Feld: gc+0x2f0 (= World+0x0C). Insert = FUN_00468ad0
// (this = &AirshipMap, wie beim Kreatur-Insert in cube_util.cpp).
// Render-Loop (FUN_004b3xxx) iteriert die Map -> zeichnet airship.cub (0xa03).
// Struct-Vorlage (0x78) aus coremaze test-airship-packet (Release, noch gegen
// Alpha zu verifizieren -> deshalb erst der Dumper).
// ===================================================================

namespace PirateAirship {

	static const int64_t kBlockUnits = 65536;

	static uint32_t AsRebase(uint32_t staticAddr) {
		return Host::Re(staticAddr);
	}
	using Fn_OperatorNew_t = void* (__cdecl*)(unsigned int size);       // 0x0068d668
	using Fn_MapInsert_t   = void** (__thiscall*)(void* mapField, uint64_t* guid); // 0x00468ad0

	// Airship-Struct (0x78 = 120 Byte), Layout aus dem Release-Airship-Packet.
#pragma pack(push, 1)
	struct Airship {
		uint64_t GUID;          // +0x00
		uint32_t b1;            // +0x08
		uint32_t field_c;       // +0x0C
		int64_t  posX, posY, posZ;   // +0x10 (Einheiten, 65536/Block)
		float    velX, velY, velZ;   // +0x28 (Bloecke/Sekunde)
		float    rot;                // +0x34
		int64_t  stX, stY, stZ;      // +0x38 station_pos
		float    pathrot;            // +0x50
		uint32_t field_54;           // +0x54
		int64_t  tgX, tgY, tgZ;      // +0x58 target_pos
		uint8_t  flight_stage;       // +0x70
		uint8_t  u1, u2, u3;         // +0x71..0x73
		uint32_t b2;                 // +0x74
	};
#pragma pack(pop)

	// ---------------------------------------------------------------
	// [STRG+7] SICHER: AirshipMap dumpen (read-only, SEH-abgesichert).
	// ---------------------------------------------------------------
	static void HexDump(const char* label, void* addr, int bytes) {
		// std::cout << "  " << label << " @0x" << std::hex << (uintptr_t)addr << ": ";
		uint8_t* p = (uint8_t*)addr;
		for (int i = 0; i < bytes; i++) {
			// if (p[i] < 0x10) std::cout << "0";
			// std::cout << std::hex << (int)p[i] << " ";
		}
		// std::cout << std::dec << std::endl;
	}

	// Zaehlt/dumpt einen RB-Baum (MSVCBinCompat-Layout: node = left(4)parent(4)
	// right(4)color(1)isnil(1)pad(2)=0x10, key(8)@0x10, value(4)@0x18).
	static int WalkTree(void* nodeVoid, int depth, bool dumpFirst, bool& dumped) {
		if (!nodeVoid || depth > 40) return 0;
		uint8_t* node = (uint8_t*)nodeVoid;
		uint8_t isnil = node[0xd];
		if (isnil) return 0;
		int cnt = 1;
		void* left = *(void**)(node + 0x0);
		void* right = *(void**)(node + 0x8);
		uint64_t key = *(uint64_t*)(node + 0x10);
		void* val = *(void**)(node + 0x18);
		if (dumpFirst && !dumped && val) {
			dumped = true;
			// std::cout << "  -> erstes Airship: key(GUID)=0x" << std::hex << key
				// << " value=0x" << (uintptr_t)val << std::dec << std::endl;
			HexDump("Airship-Bytes[0x00..0x40]", val, 0x40);
			HexDump("Airship-Bytes[0x40..0x80]", (uint8_t*)val + 0x40, 0x40);
		}
		cnt += WalkTree(left, depth + 1, dumpFirst, dumped);
		cnt += WalkTree(right, depth + 1, dumpFirst, dumped);
		return cnt;
	}

	// Dumpt eine INLINE std::map { _Myhead(ptr)@+0, _Mysize@+4 }, ihren
	// Head-Sentinel und zaehlt die Knoten. Read-only.
	static void DumpOneMap(const char* name, uint8_t* field) {
		uint32_t myhead = *(uint32_t*)(field + 0);
		uint32_t mysize = *(uint32_t*)(field + 4);
		// std::cout << "\n  --- " << name << " @0x" << std::hex << (uintptr_t)field << std::dec
			// << "  _Myhead=0x" << std::hex << myhead << "  _Mysize=" << std::dec << mysize << " ---" << std::endl;
		HexDump("Feld (0x10)", field, 0x10);
		if (myhead < 0x10000 || myhead == 0xffffffff) { return; }   // (Konsolenausgabe entfernt)
		uint8_t* head = (uint8_t*)myhead;
		HexDump("Head-Sentinel (0x20)", head, 0x20);
		void* left = *(void**)(head + 0x0);
		void* parent = *(void**)(head + 0x4); // = root
		void* right = *(void**)(head + 0x8);
		uint8_t isnil = head[0xd];
		// std::cout << "  Head: left=0x" << std::hex << (uintptr_t)left << " parent(root)=0x" << (uintptr_t)parent
			// << " right=0x" << (uintptr_t)right << std::dec << " isnil=" << (int)isnil
			// << (parent == head ? "  (root==head -> LEER)" : "") << std::endl;
		bool d = false;
		int cnt = WalkTree(parent, 0, true, d);
		// std::cout << "  -> " << cnt << " Knoten im Baum." << std::endl;
	}

	void DumpAirshipMap(cube::GameController* gc) {
		if (!gc) { return; }   // (Konsolenausgabe entfernt)
		__try {
			// std::cout << "\n=== [PirateAirship] Map-Vergleich (EntitesMap vs AirshipMap) ===" << std::endl;
			// EntitesMap = World+0x04 = gc+0x2e8 (bekannt funktionierend, per cube_util befuellt).
			DumpOneMap("EntitesMap  (gc+0x2e8, funktioniert)", (uint8_t*)gc + 0x2e8);
			// AirshipMap = World+0x0C = gc+0x2f0 (Ziel, aktuell leer).
			DumpOneMap("AirshipMap  (gc+0x2f0, Ziel)", (uint8_t*)gc + 0x2f0);
			// std::cout << "\n  Vergleich: Haben beide Head-Sentinels dieselbe Struktur (isnil=1,\n"
				// "  left/parent/right = self bei leerer Map)? Dann ist der Insert-Pfad identisch.\n"
				// "==================================================\n" << std::endl;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[PirateAirship] FEHLER: Map-Dump abgestuerzt (SEH abgefangen)." << std::endl;
		}
	}

	// ---------------------------------------------------------------
	// [STRG+8] EXPERIMENTELL: Airship bauen + in AirshipMap einfuegen.
	// ---------------------------------------------------------------
	static uint64_t g_nextGuid = 0x5000AD00000ULL;

	void SpawnAirshipNearPlayer(cube::GameController* gc) {
		(void)gc;
		// STILLGELEGT (2026-07-13): Der native AirshipMap-Insert hat den Render-
		// Loop des Spiels zweimal gecrasht (Alpha-Airship-Struct != coremaze-
		// Release-Layout, Absturz ausserhalb unserer SEH). Wir gehen jetzt den
		// robusten Weg: fahrender Kreaturen-Verband in PirateShip.cpp (STRG+5).
		// std::cout << "[PirateAirship] Nativer Airship-Spawn ist stillgelegt (crashte den\n"
			// "  Render-Loop). Nutze STRG+5: fahrendes Piratenschiff (Kreaturen-Verband).\n"
			// "  STRG+7 (Map-Dump) bleibt als reines Analyse-Werkzeug erhalten." << std::endl;
	}

	// --- Framework-Anbindung (v1: keine Auto-Logik) ---
	class PirateAirshipMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override { (void)gc; }
	};
	static PirateAirshipMod g_mod;

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_mod);
		// std::cout << "[PirateAirship] Initialisiert (Phase 3: Airship-Entity, experimentell)." << std::endl;
	}
}
