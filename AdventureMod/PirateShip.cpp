#include "PirateShip.h"
#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

// ===================================================================
// PirateShip.cpp  (v2: fahrender Piraten-Verband = mobiler Doungon)
// -------------------------------------------------------------------
// Robuster Weg (Entscheidung 2026-07-13): das "Schiff" ist ein Verband
// aus normalen Kreaturen (unser bewaehrter cube::util::SpawnMonster-Pfad),
// die als Deck-Formation ueber die See DRIFTEN. Kein Eingriff in native
// Render-Maps -> kein Render-Crash (der native AirshipMap-Insert wurde
// verworfen, siehe Ozean_Piraten_Plan.md).
//
// Fahrlogik: der Verband steuert langsam auf den Spieler zu und stoppt
// beim "Entern" (Spieler nah) -> dann uebernimmt die Kreatur-KI den Kampf.
// Positionen werden pro Tick SEH-sicher gesetzt (wie beim Haendler-Despawn).
//
// NAECHSTE SCHRITTE: eigenes .cub-Schiffsmodell/-textur als sichtbares Deck
// unter den Verband legen (Tooling: voided/cubeworld-tools, DatZach-Assets).
// ===================================================================

namespace PirateShip {

	static const int64_t kBlockUnits = 65536;
	static const double  kPi = 3.14159265358979;

	// Fahr-Parameter
	static const double kSpeedBlocksPerSec = 5.0;   // Reisegeschwindigkeit
	static const double kTurnRadPerSec     = 0.6;   // wie schnell der Bug einschwenkt
	static const double kBoardingBlocks    = 14.0;  // naeher => "Entern" (Verband stoppt)
	static const double kDisengageBlocks   = 200.0; // weiter weg => Verband inaktiv

	static uint32_t PlayerLevel(cube::GameController* gc) {
		if (gc && gc->local_player) {
			int lv = gc->local_player->entity_data.level;
			return (uint32_t)(lv > 0 ? lv : 1);
		}
		return 1;
	}

	struct PirateKind { cube::Creature::Race race; const char* name; int levelBias; };

	// Aggressive Piraten brauchen FEINDLICHE Rassen (NomadMale/-Female sind
	// friedliche NPCs). hostility=2 = aktiv/beweglich/greift an.
	static const PirateKind kCrew[] = {
		{ cube::Creature::Race::Bandit,   "Pirat",          0 },
		{ cube::Creature::Race::Bandit,   "Freibeuter",     0 },
		{ cube::Creature::Race::Skeleton, "Skelett-Pirat", -1 },
		{ cube::Creature::Race::Zombie,   "Untoter Pirat", -1 },
	};
	static const int kCrewCount = (int)(sizeof(kCrew) / sizeof(kCrew[0]));
	static const PirateKind kCaptain = { cube::Creature::Race::Bandit, "Piraten-Kapitaen (Boss)", 18 };
	// 1 = wildes Monster. Vorher stand hier 2 -- ein Wert, den das Spiel nie
	// verwendet (RE_MobSpawn.md, §4). Wirkungsgleich, weil MakeHostile danach
	// ohnehin 1 schreibt, aber jetzt steht von Anfang an das Richtige da.
	static const uint32_t kPirateHostility = cube::util::Hostility::Monster;

	// --- Verbands-Zustand (das "Schiff") ---
	struct ShipMember { uint64_t guid; double localX; double localY; }; // Schiffs-lokale Deck-Koordinaten (Bloecke)
	static ShipMember g_members[24];
	static int     g_memberCount = 0;
	static bool    g_shipActive  = false;
	static int64_t g_centerX = 0, g_centerY = 0, g_shipZ = 0; // Weltzentrum + Wasser-Z
	static double  g_heading = 0.0;   // Fahrtrichtung (rad)
	static DWORD   g_lastMoveTick = 0;
	static uint64_t g_captainGuid = 0;

	static double Frand() { return (double)std::rand() / (double)RAND_MAX; }

	// SEH-sicher: Kreatur per GUID finden (Schnellpfad Kapitaen, sonst EntitesMap).
	static cube::Creature* FindByGuid(cube::GameController* gc, uint64_t guid) {
		if (!gc) return nullptr;
		__try {
			auto* m = gc->world.EntitesMap;
			if (!m) return nullptr;
			return m->Find(guid);
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	// SEH-sicher: Position setzen (nur POD im Scope -> C2712-frei).
	static bool SafeWritePos(cube::Creature* c, int64_t x, int64_t y, int64_t z) {
		if (!c) return false;
		__try {
			c->entity_data.position.X = x;
			c->entity_data.position.Y = y;
			c->entity_data.position.Z = z;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool SafeReadPos(cube::Creature* c, int64_t* x, int64_t* y, int64_t* z) {
		if (!c) return false;
		__try { *x = c->entity_data.position.X; *y = c->entity_data.position.Y; *z = c->entity_data.position.Z; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static void AddMember(uint64_t guid, double lx, double ly) {
		if (g_memberCount >= (int)(sizeof(g_members) / sizeof(g_members[0]))) return;
		g_members[g_memberCount].guid = guid;
		g_members[g_memberCount].localX = lx;
		g_members[g_memberCount].localY = ly;
		g_memberCount++;
	}

	void SpawnPirateShipNearPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) {
			// std::cout << "[PirateShip] Kein Spieler -- Schiff uebersprungen." << std::endl;
			return;
		}
		Vector3<int64_t> p = gc->local_player->entity_data.position;
		uint32_t plvl = PlayerLevel(gc);

		// Verband zuruecksetzen.
		g_memberCount = 0;
		g_captainGuid = 0;

		// Schiff ~20 Bloecke vor dem Spieler, auf Wasseroberflaeche (Spieler-Z).
		g_centerX = p.X + (int64_t)20 * kBlockUnits;
		g_centerY = p.Y;
		g_shipZ   = p.Z;
		g_heading = std::atan2((double)(p.Y - g_centerY), (double)(p.X - g_centerX)); // Bug Richtung Spieler
		g_lastMoveTick = GetTickCount();

		// std::cout << "[PirateShip] Ein Piratenschiff naehert sich bei Block ("
			// << (long long)(g_centerX / kBlockUnits) << ", " << (long long)(g_centerY / kBlockUnits) << ")!" << std::endl;

		// Kapitaen am Heck-Ruder (lokal hinten mittig).
		int capLevel = (int)plvl + kCaptain.levelBias; if (capLevel < 1) capLevel = 1;
		Vector3<int64_t> capPos(g_centerX + (int64_t)(-3.0 * kBlockUnits), g_centerY, g_shipZ);
		cube::Creature* cap = cube::util::SpawnMonster(capPos, kCaptain.race, kPirateHostility, (uint32_t)capLevel);
		if (cap) { g_captainGuid = (uint64_t)cap->GUID; AddMember(g_captainGuid, -3.0, 0.0);
			          cube::util::SetAggroRadius(cap, cube::util::kNativeAggroRadius); }
		// std::cout << "[PirateShip]   " << kCaptain.name << " (Lvl " << capLevel << ")"
			// << (cap ? "" : " -- Spawn fehlgeschlagen") << std::endl;

		// Crew als Deck-Formation: 2 Reihen (backbord/steuerbord) entlang der
		// Schiffslaenge (lokal X = vorne/hinten, lokal Y = quer).
		int crew = 8;
		int placed = 0;
		for (int i = 0; i < crew; i++) {
			const PirateKind& k = kCrew[std::rand() % kCrewCount];
			double lx = -2.0 + (double)(i / 2) * 1.6;          // -2 .. +4 in Schritten
			double ly = (i % 2 == 0) ? -1.6 : 1.6;             // Backbord/Steuerbord
			Vector3<int64_t> pos(g_centerX + (int64_t)(lx * kBlockUnits),
			                     g_centerY + (int64_t)(ly * kBlockUnits), g_shipZ);
			int lvl = (int)plvl + k.levelBias; if (lvl < 1) lvl = 1;
			cube::Creature* c = cube::util::SpawnMonster(pos, k.race, kPirateHostility, (uint32_t)lvl);
			if (c) { AddMember((uint64_t)c->GUID, lx, ly);
			         cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius); placed++; }
		}
		g_shipActive = (g_memberCount > 0);
		// std::cout << "[PirateShip] Verband: 1 Kapitaen + " << placed << " Crew, faehrt auf dich zu. Wehr dich!" << std::endl;
	}

	// Fahrt des ganzen Verbands (pro Tick, SEH-sicher). Steuert auf den Spieler
	// zu; stoppt beim Entern; wird inaktiv, wenn alle tot oder zu weit weg.
	static void DriveShip(cube::GameController* gc) {
		if (!g_shipActive || g_memberCount == 0 || !gc || !gc->local_player) return;

		DWORD now = GetTickCount();
		double dt = (double)(now - g_lastMoveTick) / 1000.0;
		g_lastMoveTick = now;
		if (dt <= 0.0 || dt > 0.5) dt = 0.05; // Ausreisser/Frame-Sprung glaetten

		int64_t px, py, pz;
		if (!SafeReadPos(gc->local_player, &px, &py, &pz)) return;

		double dx = (double)(px - g_centerX) / (double)kBlockUnits;
		double dy = (double)(py - g_centerY) / (double)kBlockUnits;
		double distToPlayer = std::sqrt(dx * dx + dy * dy);
		if (distToPlayer > kDisengageBlocks) { g_shipActive = false; return; }

		// Bug sanft Richtung Spieler drehen.
		double target = std::atan2(dy, dx);
		double diff = target - g_heading;
		while (diff > kPi)  diff -= 2.0 * kPi;
		while (diff < -kPi) diff += 2.0 * kPi;
		double maxTurn = kTurnRadPerSec * dt;
		if (diff >  maxTurn) diff =  maxTurn;
		if (diff < -maxTurn) diff = -maxTurn;
		g_heading += diff;

		bool sailing = (distToPlayer > kBoardingBlocks);
		if (sailing) {
			double adv = kSpeedBlocksPerSec * dt; // Bloecke diesen Tick
			g_centerX += (int64_t)(std::cos(g_heading) * adv * (double)kBlockUnits);
			g_centerY += (int64_t)(std::sin(g_heading) * adv * (double)kBlockUnits);
		}

		double ch = std::cos(g_heading), sh = std::sin(g_heading);
		int alive = 0;
		for (int i = 0; i < g_memberCount; i++) {
			cube::Creature* c = FindByGuid(gc, g_members[i].guid);
			if (!c) continue;
			alive++;
			if (sailing) {
				// lokale Deck-Koordinate in die Welt drehen.
				double lx = g_members[i].localX, ly = g_members[i].localY;
				int64_t wx = g_centerX + (int64_t)((lx * ch - ly * sh) * (double)kBlockUnits);
				int64_t wy = g_centerY + (int64_t)((lx * sh + ly * ch) * (double)kBlockUnits);
				SafeWritePos(c, wx, wy, g_shipZ);
			}
		}
		if (alive == 0) { g_shipActive = false; g_memberCount = 0; }
	}

	class PirateShipMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override { DriveShip(gc); }
	};
	static PirateShipMod g_pirateShipMod;

	uint64_t GetCaptainGuid() { return g_captainGuid; }

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_pirateShipMod);
		// std::cout << "[PirateShip] Initialisiert (v2: fahrender Verband)." << std::endl;
	}
}
