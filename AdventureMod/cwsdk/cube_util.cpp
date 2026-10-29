#include "cube_util.h"
#include "../Host.h"

#include <Windows.h>
#include <iostream>

// ---------------------------------------------------------------------
// SICHERHEITS-UMBAU (siehe AdventureMod/README.md, Abschnitt "Absturz-Fix"):
// Jeder native Funktionsaufruf hier ist in eine eigene, winzige Funktion
// OHNE Objekte mit Destruktor isoliert (MSVC C2712 -- __try/__except duerfen
// nicht im selben Frame wie z.B. Vector3<T>-Locals/-Parameter stehen) und
// per SEH (__try/__except) abgesichert: stuerzt der native Aufruf ab, wird
// die Exception abgefangen, geloggt, und der Rest des Mods laeuft normal
// weiter -- statt dass das ganze Cube.exe hart abstuerzt.
//
// Der Grafik-Init-Aufruf (imageBase+0x3F7C0) ist auf Nutzerwunsch wieder
// AKTIVIERT (g_graphicsInitEnabled = true weiter unten), obwohl seine
// Adresse nie unabhaengig gegen dieses Cube.exe-Build verifiziert wurde
// (siehe README.md-Tabelle). Der Verdacht ist, dass der vorherige Absturz
// stattdessen am selbstgebauten Mod-Launcher lag. SEH faengt einen Absturz
// hier weiterhin ab (siehe SafeInitGraphics) -- via SetGraphicsInitEnabled(false)
// laesst er sich jederzeit wieder deaktivieren, falls doch ein Problem auftritt.
// ---------------------------------------------------------------------

namespace {

	bool SafeLockWorld(cube::World* world) {
		__try {
			world->Lock();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: World::Lock() abgestuerzt (SEH abgefangen) -- breche Spawn ab." << std::endl;
			return false;
		}
	}

	// Wichtig: wird IMMER aufgerufen, wenn SafeLockWorld() erfolgreich war,
	// damit die World-CriticalSection nicht dauerhaft gesperrt bleibt.
	void SafeUnlockWorld(cube::World* world) {
		__try {
			world->Unlock();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: World::Unlock() abgestuerzt (SEH abgefangen) -- "
				"World bleibt evtl. dauerhaft gesperrt, ggf. Neustart noetig!" << std::endl;
		}
	}

	bool SafeCallCtor(cube::Creature* creature, uint64_t* guid, uint32_t imageBase) {
		typedef cube::Creature* (__thiscall* ctor_t)(cube::Creature*, uint64_t*);
		auto fn = (ctor_t)(Host::ReOff(0x3B690));
		__try {
			fn(creature, guid);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: CreatureCtor (+0x3B690) abgestuerzt (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	// Gibt nullptr zurueck, wenn entweder der Aufruf selbst abstuerzt ODER
	// die native Funktion (regulaer) einen Nullzeiger liefert.
	cube::Creature** SafeInsertIntoMap(void* entitesMap, uint64_t* guid, uint32_t imageBase) {
		typedef cube::Creature** (__thiscall* insert_t)(void*, uint64_t*);
		auto fn = (insert_t)(Host::ReOff(0x68AD0));
		__try {
			return fn(entitesMap, guid);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: EntitesMap-Insert (+0x68AD0) abgestuerzt (SEH abgefangen)." << std::endl;
			return nullptr;
		}
	}

	// NICHT verifizierte Adresse -- siehe Kommentar oben. Wird nur
	// aufgerufen, wenn cube::util::IsGraphicsInitEnabled() true ist.
	bool SafeInitGraphics(uint32_t* racePtr, void* appearance, uint32_t imageBase) {
		typedef int(__cdecl* fn_t)(uint32_t*, void*, uint32_t);
		auto fn = (fn_t)(Host::ReOff(0x3F7C0));
		__try {
			fn(racePtr, appearance, 0);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: init_creature_graphics_by_race (+0x3F7C0, UNVERIFIZIERT) "
				"abgestuerzt (SEH abgefangen) -- Kreatur besteht trotzdem weiter, Aussehen evtl. falsch/leer." << std::endl;
			return false;
		}
	}

	bool SafeInitCompanion(cube::World* world, uint64_t ownerGuid, cube::Creature* c, uint32_t imageBase) {
		typedef int(__thiscall* fn_t)(cube::World*, uint64_t, cube::Creature*);
		auto fn = (fn_t)(Host::ReOff(0x1EE7A0));
		__try {
			fn(world, ownerGuid, c);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SpawnCreature] FEHLER: cube_world_init_companion (+0x1EE7A0) abgestuerzt "
				"(SEH abgefangen) -- Kreatur besteht weiter, hat aber evtl. keine Verhaltens-KI." << std::endl;
			return false;
		}
	}

	// Auf Nutzerwunsch reaktiviert (siehe README.md, "Absturz-Fix" -- der
	// Verdacht ist, dass der vorherige Absturz am selbstgebauten Mod-
	// Launcher lag statt an dieser unverifizierten Adresse). SEH bleibt
	// aktiv (SafeInitGraphics oben), ein falscher Offset wird also weiterhin
	// abgefangen und geloggt statt das Spiel abstuerzen zu lassen.
	// KORRIGIERT (2026-07-13 via Live-Dump): +0x60 -> 0=SPIELER, 1=normale
	// Kreatur (ALLE echten Tiere/Monster: Ogre/Hai/Piranha = 1), 5=Begleiter,
	// 6=passiv. Unsere Spawns hatten +0x60=2 -> weder feindlich noch Kreatur.
	// Fix: +0x60=1. isHostileTo: Kreatur(1) vs Spieler(0) = feindlich; Kreatur(1)
	// vs Kreatur(1) = gleiches Team = NICHT feindlich. KEIN Aggro-Bit (das machte
	// sie zu Pseudo-Spielern + Infighting + Crash). Nur POD -> __try erlaubt.
	bool SafeMakeHostile(cube::Creature* c) {
		__try {
			uint8_t* base = (uint8_t*)c;
			base[0x60] = 1;            // Faktion 1 = normale Kreatur: feindlich ggue.
			                           // Spieler(0), gleiches Team (kein Infighting), kein Spieler-Code.
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[MakeHostile] FEHLER: Schreibzugriff abgestuerzt (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	// Aggro-/Leinenradius (Creature+0x1D2C). Nativ aus dem Spawn-Bausatz
	// (Spawn+0x08, Vorgabe 200.0; GameController.cpp:96687 kopiert es herueber).
	// Die Verfolgungsschleife vergleicht (radius + 20.0)^2 gegen den Abstand --
	// bleibt das Feld 0, reagiert der Mob erst auf 20 Bloecke. Nur POD -> __try ok.
	bool SafeSetAggroRadius(cube::Creature* c, float blocks) {
		__try {
			*(float*)((uint8_t*)c + 0x1D2C) = blocks;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[SetAggroRadius] FEHLER: Schreibzugriff abgestuerzt (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	// ---- Diagnose: Kreatur-Felder dumpen (read-only, SEH) ---------------
	void DumpOneCreatureFields(cube::Creature* c, cube::Creature* player) {
		__try {
			uint8_t* b = (uint8_t*)c;
			uint64_t guid = *(uint64_t*)(b + 0x08);
			uint8_t faction = b[0x60];
			uint32_t race64 = *(uint32_t*)(b + 0x64);
			uint8_t b2c = b[0x2c];
			uint8_t b30 = b[0x30];
			void* tree = *(void**)(b + 0x13e4);
			uint32_t abB = *(uint32_t*)(b + 0x13e8);
			uint32_t abE = *(uint32_t*)(b + 0x13ec);
			int abN = (abE >= abB && (abE - abB) < 0x1000) ? (int)((abE - abB) / 4) : -1;
			const char* tag = (c == player) ? "SPIELER" : (((guid >> 32) == 0xCAFECAFEULL) ? "UNSER  " : "ECHT   ");
			std::cout << "  [" << tag << "] c=0x" << std::hex << (uintptr_t)c
				<< " race(+0x64)=" << std::dec << race64
				<< " faction(+0x60)=" << (int)faction
				<< " +0x2c=" << (int)b2c << " +0x30=" << (int)b30
				<< " tree(+0x13e4)=0x" << std::hex << (uintptr_t)tree << std::dec
				<< " abilities=" << abN << std::endl;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "  [FEHLER beim Lesen einer Kreatur]" << std::endl;
		}
	}

	void WalkCreatureTree(void* node, cube::Creature* player, int depth, int* count, int maxN) {
		if (!node || depth > 60 || *count >= maxN) return;
		uint8_t* n = (uint8_t*)node;
		uint8_t isnil = 1; void* left = 0; void* right = 0; cube::Creature* c = 0;
		__try {
			isnil = n[0xd];
			if (!isnil) { left = *(void**)(n + 0); right = *(void**)(n + 8); c = *(cube::Creature**)(n + 0x18); }
		} __except (EXCEPTION_EXECUTE_HANDLER) { return; }
		if (isnil) return;
		if (c) { DumpOneCreatureFields(c, player); (*count)++; }
		WalkCreatureTree(left, player, depth + 1, count, maxN);
		WalkCreatureTree(right, player, depth + 1, count, maxN);
	}

	// ---- Native Wander-KI anhaengen (RandomWalkBehavior) ----------------
	// Unsere Fische/Mobs bekommen von der Companion-Init (0x5EE7A0) den Baum
	// Sequential[ CombatBehavior, CompanionBehavior ]. CompanionBehavior folgt
	// einem OWNER-Entity (haben wir nicht) -> nutzlos. Wir ERSETZEN diesen
	// Knoten durch eine RandomWalkBehavior (nur EIN Zeiger-Ueberschreiben im
	// Baum, KEINE Listen-Chirurgie) und setzen den Heimat-Anker Creature+0x1c0/
	// +0x1c8 (int64 X/Y) = Spawn-Position. Die spieleigene KI laesst die Kreatur
	// dann um ihren Spawn wandern (Velocity via Spiel-Physik -> haftet, anders
	// als mod-getriebene Positions-Schreibzugriffe). Reihenfolge im Baum bleibt
	// [Combat, RandomWalk]: nah am Spieler greift Combat an, sonst wird gewandert.
	// Alles SEH-abgesichert; ein Fehler wird abgefangen statt Cube.exe zu crashen.
	bool SafeAttachRandomWalk(cube::Creature* creature, long long homeX, long long homeY, uint32_t imageBase) {
		typedef void (__thiscall* rwctor_t)(void*);
		typedef void* (__cdecl* opnew_t)(unsigned int);
		__try {
			uint8_t* b = (uint8_t*)creature;
			// 1) Heimat-Anker setzen (int64 X @ +0x1c0, int64 Y @ +0x1c8).
			*(int64_t*)(b + 0x1c0) = homeX;
			*(int64_t*)(b + 0x1c8) = homeY;
			// 2) RandomWalkBehavior anlegen (8 Byte) + nativer Ctor setzt vtable+timer.
			void* rw = ((opnew_t)(Host::ReOff(0x28D668)))(8u);
			if (!rw) return false;
			((rwctor_t)(Host::ReOff(0xDBF10)))(rw);
			// 3) Im Verhaltensbaum (Creature+0x13e4 = SequentialBehavior) den LETZTEN
			//    Knoten (CompanionBehavior) durch unsere RandomWalk ersetzen.
			uint32_t tree = *(uint32_t*)(b + 0x13e4);
			if (!tree) return false;
			uint32_t head = *(uint32_t*)(tree + 4);   // Listen-Sentinel
			if (!head) return false;
			uint32_t node = *(uint32_t*)head;         // erster echter Knoten (Combat)
			if (node == head) return false;           // leere Liste
			int guard = 0;
			while (*(uint32_t*)node != head && guard++ < 16) node = *(uint32_t*)node;
			// node = letzter echter Knoten; dessen Behavior-Zeiger (node+8) ersetzen.
			*(uint32_t*)(node + 8) = (uint32_t)rw;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[AttachRandomWalk] FEHLER: abgestuerzt (SEH abgefangen) -- Kreatur ohne Wander-KI." << std::endl;
			return false;
		}
	}

	bool g_graphicsInitEnabled = true;
}

namespace cube {
	namespace util {

		void SetGraphicsInitEnabled(bool enabled) {
			g_graphicsInitEnabled = enabled;
			std::cout << "[cube::util] Grafik-Init-Aufruf (+0x3F7C0, unverifiziert) ist jetzt "
				<< (enabled ? "AKTIVIERT" : "DEAKTIVIERT") << "." << std::endl;
		}

		bool IsGraphicsInitEnabled() {
			return g_graphicsInitEnabled;
		}

		void MakeHostile(cube::Creature* creature) {
			if (!creature) return;
			SafeMakeHostile(creature); // Faktion +0x60=1 -> feindliche normale Kreatur
		}

		void SetAggroRadius(cube::Creature* creature, float blocks) {
			if (!creature) return;
			SafeSetAggroRadius(creature, blocks);
		}

		void AttachRandomWalk(cube::Creature* creature, long long homeX, long long homeY) {
			if (!creature) return;
			SafeAttachRandomWalk(creature, homeX, homeY, (uint32_t)GetModuleHandleA(NULL));
		}

		void DumpNearbyCreatures(cube::GameController* gc) {
			if (!gc) return;
			std::cout << "\n=== [DumpCreatures] Kreaturen in EntitesMap (ECHT vs UNSER vs SPIELER) ===" << std::endl;
			__try {
				auto* m = gc->world.EntitesMap;
				if (!m) { std::cout << "  keine EntitesMap." << std::endl; }
				else {
					void* root = *(void**)((uint8_t*)m + 4);
					int count = 0;
					WalkCreatureTree(root, gc->local_player, 0, &count, 40);
					std::cout << "  (" << count << " Kreaturen gedumpt, max 40)" << std::endl;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				std::cout << "  FEHLER: Dump abgestuerzt (SEH abgefangen)." << std::endl;
			}
			std::cout << "  Vergleich: hat ein ECHTes Monster tree!=0 und abilities>0, unsere aber\n"
				"  tree==0/abilities<=0? Und stimmt race bei +0x64?\n"
				"===================================================================\n" << std::endl;
		}

		cube::Creature* SpawnCreature(Vector3<int64_t> position, cube::Creature::Race race, uint32_t hostility_flags, uint32_t level, uint64_t owner_guid) {
			auto imageBase = (uint32_t)GetModuleHandleA(NULL);
			auto gc = cube::GetGameController();
			if (!gc) {
				std::cout << "[SpawnCreature] FEHLER: GetGameController() lieferte nullptr, breche ab." << std::endl;
				return nullptr;
			}

			if (!SafeLockWorld(&gc->world)) {
				return nullptr;
			}

			static uint64_t guid = 0xCAFECAFE00000000;
			guid++;
			uint64_t thisGuid = guid;
			std::cout << "[SpawnCreature] Starte Spawn, GUID=" << thisGuid << std::endl;

			// Speicher vom SPIEL holen (operator new 0x68D668 = msvcr110 malloc), nicht vom DLL-Heap: das Spiel loescht
			// Kreaturen beim Entladen/ausserhalb der Leine selbst (Creature::vfunc_0 -> operator_delete). Mit "new" aus
			// der DLL landete das free im falschen Heap -> Absturz in ntdll (Dump 2026-10-16 20:46, Cube.exe+0x20E3D3).
			typedef void* (__cdecl* GameNew_t)(unsigned int);
			void* mem = ((GameNew_t)Host::ReOff(0x28D668))((unsigned int)sizeof(cube::Creature));
			if (!mem) { SafeUnlockWorld(&gc->world); return nullptr; }
			memset(mem, 0, sizeof(cube::Creature));
			cube::Creature* creature = (cube::Creature*)mem;
			cube::Creature* result = nullptr;

			std::cout << "[SpawnCreature] Rufe CreatureCtor (+0x3B690) auf..." << std::endl;
			if (!SafeCallCtor(creature, &thisGuid, imageBase)) {
				// Spiel-Speicher: nicht mit delete freigeben (anderer Heap) -- selten, bleibt liegen
				SafeUnlockWorld(&gc->world);
				return nullptr;
			}

			std::cout << "[SpawnCreature] Rufe EntitesMap-Insert (+0x68AD0) auf..." << std::endl;
			cube::Creature** slot = SafeInsertIntoMap(&gc->world.EntitesMap, &thisGuid, imageBase);
			if (!slot) {
				std::cout << "[SpawnCreature] FEHLER: kein gueltiger EntitesMap-Slot erhalten, breche ab "
					"(Kreatur bleibt unregistriert/unsichtbar, aber Prozess laeuft weiter)." << std::endl;
				SafeUnlockWorld(&gc->world);
				return nullptr;
			}
			*slot = creature;

			creature->entity_data.position = position;
			creature->entity_data.race = (uint32_t)race;
			creature->entity_data.hostility_flags = hostility_flags;
			creature->entity_data.level = level;

			if (g_graphicsInitEnabled) {
				std::cout << "[SpawnCreature] Rufe init_creature_graphics_by_race (+0x3F7C0, UNVERIFIZIERT) auf..." << std::endl;
				SafeInitGraphics(&creature->entity_data.race, &creature->entity_data.appearance, imageBase);
			} else {
				std::cout << "[SpawnCreature] Grafik-Init uebersprungen (standardmaessig deaktiviert, siehe README.md)." << std::endl;
			}

			std::cout << "[SpawnCreature] Rufe cube_world_init_companion (+0x1EE7A0) auf..." << std::endl;
			SafeInitCompanion(&gc->world, owner_guid, creature, imageBase);

			std::cout << "[SpawnCreature] Spawn abgeschlossen, GUID=" << thisGuid << std::endl;
			result = creature;

			SafeUnlockWorld(&gc->world);
			return result;
		}

		cube::Creature* SpawnMonster(Vector3<int64_t> position, cube::Creature::Race race, uint32_t hostility_flags, uint32_t level) {
			auto creature = SpawnCreature(position, race, hostility_flags, level, 0);
			return creature;
		}
	}
}
