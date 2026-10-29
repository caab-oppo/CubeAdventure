#pragma once

#include "cube.h"
#include <cstdint>

namespace cube {
	namespace util {

		// ================= hostility_flags (Creature+0x60) ===================
		// NICHT geraten: diese Werte stammen aus Chunk_generateObjects @0x5AF170,
		// dem einzigen Mob-Spawner des Spiels. Dort wird Spawn+0x28 gesetzt und
		// in GameController.cpp:96652 nach Creature+0x60 kopiert.
		// Vollstaendige Herleitung: RE_MobSpawn.md
		namespace Hostility {
			static const uint32_t Player    = 0;  // nur der Spieler selbst
			static const uint32_t Monster   = 1;  // wildes Monster: feindlich ggue. Spieler,
			                                      // untereinander gleiches Team (kein Infighting)
			static const uint32_t Talkable  = 3;  // NPC, mit [R] ansprechbar (Dorfbewohner, Haendler)
			static const uint32_t Companion = 5;  // Begleiter, folgt einem Besitzer
			static const uint32_t Passive   = 6;  // passives Objekt (nativ: Vogelscheuche)
			// Der Wert 2 kommt im Spiel NIRGENDS vor -- er erzeugt eine Kreatur,
			// die weder Monster noch NPC ist. Nicht verwenden.
		}

		// ================= char_class (Creature+0x140) ========================
		// Aus den fuenf Haus-Zweigen in Chunk_generateObjects. Einziger
		// Unterschied zwischen ihnen ist dieses Byte (RE_MobSpawn.md, §5).
		namespace CharClass {
			static const uint8_t Vendor    = 0x80;  // oeffnet Buy/Sell-UI
			static const uint8_t Npc81     = 0x81;
			static const uint8_t Npc82     = 0x82;
			static const uint8_t Npc83     = 0x83;
			static const uint8_t Innkeeper = 0x84;  // belegt: GameController.cpp:34127
			                                        // prueft darauf und baut die UI-Seite u"innkeeper"
			static const uint8_t Npc88     = 0x88;  // nativ am Schiff/Steg
		}

		// Nativer Vorgabewert des Aggro-/Leinenradius (Spawn+0x08 -> Creature+0x1D2C).
		// Die Verfolgungsschleife rechnet mit (radius + 20.0); bleibt das Feld 0,
		// reagiert ein Mob erst auf 20 Bloecke.
		static const float kNativeAggroRadius = 200.0f;

		// Kompletter Spawn-Ablauf (Ctor + EntitesMap-Registrierung + optionaler
		// Grafik-Init + Companion-/Verhalten-Init). NUR die ersten drei Schritte
		// (Ctor 0x3B690, EntitesMap-Insert 0x68AD0, Companion-Init 0x1EE7A0)
		// sind per Live-Test GEGEN DIESES BUILD verifiziert -- siehe
		// AdventureMod/README.md, Tabelle "Verifikationsstatus". Der Grafik-Init-
		// Aufruf (0x3F7C0) ist NICHT verifiziert und deshalb standardmaessig
		// DEAKTIVIERT (siehe SetGraphicsInitEnabled unten). Jeder einzelne native
		// Aufruf ist zusaetzlich SEH-isoliert (siehe cube_util.cpp) -- ein Absturz
		// darin fuehrt zu einer geloggten, abgefangenen Fehlermeldung statt zum
		// kompletten Prozessabsturz.
		cube::Creature* SpawnCreature(Vector3<int64_t> position, cube::Creature::Race race, uint32_t hostility_flags, uint32_t level, uint64_t owner_guid);
		cube::Creature* SpawnMonster(Vector3<int64_t> position, cube::Creature::Race race, uint32_t hostility_flags, uint32_t level);
		void AttachRandomWalk(cube::Creature* creature, long long homeX, long long homeY);

		// Macht eine bereits gespawnte Kreatur zum ECHTEN feindlichen Mob.
		// Grund (2026-07-13, via CubeWorld-Reversal): unser Spawn ruft die
		// Companion-Init (0x5EE7A0) auf, die zwar CombatBehavior anhaengt, aber
		// zusaetzlich CompanionBehavior -> das Spiel behandelt die Kreatur als
		// friedlichen Begleiter. isHostileTo() entscheidet anhand Faktion +0x60
		// (5=Begleiter, 6=passiv).
		// KORRIGIERT (siehe cube_util.cpp und RE_MobSpawn.md): MakeHostile setzt
		// +0x60 = Hostility::Monster (1), NICHT 0 -- 0 ist der SPIELER, und ein
		// Aggro-Bit wird NICHT gesetzt (das machte die Kreatur zum Pseudo-Spieler
		// mit Infighting und Absturz). Der alte Kommentar hier war falsch.
		// SEH-abgesichert. Fuer Fische/Piraten aufrufen, NICHT fuer den Haendler.
		void MakeHostile(cube::Creature* creature);

		// Setzt den Aggro-/Leinenradius (Creature+0x1D2C) in Bloecken. Nativ
		// bekommt jede Kreatur hier 200.0 aus dem Spawn-Bausatz; unsere Spawns
		// gingen bisher ohne diesen Schritt los und reagierten deshalb erst auf
		// die 20 Bloecke, die die Verfolgungsschleife fest addiert. SEH-abgesichert.
		void SetAggroRadius(cube::Creature* creature, float blocks);

		// Diagnose (read-only): listet EntitesMap-Kreaturen -- Offset-Verifikation.
		void DumpNearbyCreatures(cube::GameController* gc);

		// Schaltet den (unverifizierten, siehe oben) Grafik-Init-Aufruf
		// (imageBase+0x3F7C0) in SpawnCreature() ein/aus. Standard: true
		// (an, auf Nutzerwunsch reaktiviert) -- SEH faengt einen Absturz
		// darin weiterhin ab. Bei Problemen mit SetGraphicsInitEnabled(false)
		// wieder deaktivieren.
		void SetGraphicsInitEnabled(bool enabled);
		bool IsGraphicsInitEnabled();
	};
};
