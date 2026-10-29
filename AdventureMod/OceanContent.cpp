#include "OceanContent.h"
#include "Host.h"
#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

// ===================================================================
// OceanContent.cpp  (Phase 1: Fische in Ozeanen)
// -------------------------------------------------------------------
// Eigenstaendiges Modul, komplett getrennt vom WanderingMerchant.
// Nutzt cube::util::SpawnMonster() (owner_guid=0 -> wilde Kreatur) mit
// den Fisch-/See-Races aus Creature.h (SapphireFish..Blowfish, Shark,
// Piranha, Mermaid, Merman, Seahorse ...).
//
// Ozean-Erkennung (Phase 1b, noch offen): "Ocean" ist im Spiel schlicht
// Terrain UNTER dem Wasserpegel (Karten-Label bei 0x004e5498 vergleicht
// eine Tile-Elevation gegen einen Schwellwert, Tile via FUN_00487da0).
// Solange das nicht sauber nachgebaut ist, spawnen wir Fische
// SPIELER-RELATIV (rund um den Spieler, auf seiner Z-Ebene) -- der Nutzer
// schaltet das Auto-Spawnen ein, waehrend er im/ueber dem Ozean ist.
// ===================================================================

namespace OceanContent {

	static const int64_t kBlockUnits = 65536;

	// --- Fisch-/See-Kreaturen-Tabelle -------------------------------
	// hostility_flags (LIVE-Test ueber den Hostility-Tester bestaetigt):
	//   2 = aktiv/aggressiv: BEWEGT sich (schwimmt auf den Spieler zu) UND
	//       ist angreifbar. Der EINZIGE Wert mit Eigenbewegung.
	//   1, 3..7 = angreifbar, aber STATISCH (keine Eigenbewegung).
	//   3 = zusaetzlich "gespraechsbereiter NPC".
	// -> Daher ALLE Fische auf 2 (bewegen sich + angreifbar). Wer ruhige,
	//    NICHT angreifende Schwaerme will, braeuchte mod-eigene Wander-
	//    Bewegung (Phase 2) -- ein "friedlich + schwimmend"-Wert existiert nicht.
	struct FishSpecies {
		cube::Creature::Race race;
		const char* name;
		uint32_t hostility;   // WIRKUNGSLOS: SpawnOneFish ruft danach immer MakeHostile(),
		                      // das +0x60 auf Hostility::Monster (1) zieht. Der frueher hier
		                      // notierte Wert 2 kommt im Spiel ohnehin nicht vor (RE_MobSpawn.md §4).
		                      // Feld bleibt stehen, falls einzelne Arten mal friedlich sein sollen.
		int      levelBias;   // Level = Spielerlevel + levelBias (min. 1)
		int      weight;      // relative Spawn-Haeufigkeit
	};

	static const FishSpecies kFish[] = {
		{ cube::Creature::Race::SapphireFish, "Saphirfisch",  2,  0, 20 },
		{ cube::Creature::Race::LemonFish,    "Zitronenfisch",2,  0, 20 },
		{ cube::Creature::Race::Blowfish,     "Kugelfisch",   2,  0, 14 },
		{ cube::Creature::Race::LanternFish,  "Laternenfisch",2,  0, 12 },
		{ cube::Creature::Race::MawFish,      "Schlundfisch", 2,  0, 10 },
		{ cube::Creature::Race::Seahorse,     "Seepferdchen", 2,  0,  8 },
		{ cube::Creature::Race::Piranha,      "Piranha",      2,  1,  8 },  // Raubfisch
		{ cube::Creature::Race::Shark,        "Hai",          2,  3,  4 },  // Raubfisch
	};
	static const int kFishCount = (int)(sizeof(kFish) / sizeof(kFish[0]));

	// "Eigene Kreatur" als Beispiel (Phase 2, Vorgeschmack): ein seltener,
	// deutlich staerkerer Hai als Mini-Boss. Zeigt, wie man ueber die
	// bestehenden Races getunte Sonder-Kreaturen definiert (echte NEUE
	// 3D-Modelle brauchen die noch nicht geknackte .cub/data*.db-Pipeline).
	static const FishSpecies kGiantShark = { cube::Creature::Race::Shark, "Riesenhai (Mini-Boss)", 2, 15, 0 };
	static const int kGiantSharkChancePercent = 8; // 8% Chance je Schwarm

	// --- Auto-Spawn-Zustand -----------------------------------------
	static bool  g_autoFish = false;
	static DWORD g_lastFishTick = 0;
	static const DWORD kAutoFishIntervalMs = 1200; // alle ~12s ein kleiner Schwarm (wenn an)

	static double Frand() { return (double)std::rand() / (double)RAND_MAX; }

	static const FishSpecies& PickWeighted() {
		int total = 0;
		for (int i = 0; i < kFishCount; i++) total += kFish[i].weight;
		int r = std::rand() % (total > 0 ? total : 1);
		for (int i = 0; i < kFishCount; i++) {
			r -= kFish[i].weight;
			if (r < 0) return kFish[i];
		}
		return kFish[0];
	}

	static uint32_t PlayerLevel(cube::GameController* gc) {
		if (gc && gc->local_player) {
			int lv = gc->local_player->entity_data.level;
			return (uint32_t)(lv > 0 ? lv : 1);
		}
		return 1;
	}

	// --- Sanftes Umherschwimmen (mod-getrieben, absturzsicher) -----------
	// Unsere companion-basierten Fische haben keine native Wander-KI -> im
	// Ruhezustand stehen sie still. Wir bewegen sie deshalb selbst sanft um
	// ihren Spawn-Punkt. Ist der Spieler nah (Kampf), lassen wir los, damit
	// die Combat-KI angreifen kann. Positions-Schreibzugriffe SEH-sicher.
	struct TrackedFish { uint64_t guid; int64_t homeX, homeY, z; float heading; bool active; };
	static TrackedFish g_fish[96];
	static int   g_fishCount = 0;
	static DWORD g_lastWanderTick = 0;
	static const double kFishSpeed      = 2.2;  // Bloecke/Sekunde (gemaechlich)
	static const double kFishTurn       = 1.4;  // rad/Sekunde
	static const double kFishHomeRadius = 100.0; // wandert ~50 Bloecke um den Spawn
	static const double kFishCombatDist = 6.0;  // Spieler naeher -> Combat uebernimmt

	static cube::Creature* FishFindByGuid(cube::GameController* gc, uint64_t guid) {
		if (!gc) return nullptr;
		__try { auto* m = gc->world.EntitesMap; return m ? m->Find(guid) : nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static bool FishReadPos(cube::Creature* c, int64_t* x, int64_t* y, int64_t* z) {
		if (!c) return false;
		__try { *x = c->entity_data.position.X; *y = c->entity_data.position.Y; *z = c->entity_data.position.Z; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool FishWritePos(cube::Creature* c, int64_t x, int64_t y, int64_t z) {
		if (!c) return false;
		__try { c->entity_data.position.X = x; c->entity_data.position.Y = y; c->entity_data.position.Z = z; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void RegisterFish(cube::Creature* c, Vector3<int64_t> pos) {
		if (!c) return;
		int slot = -1;
		for (int i = 0; i < g_fishCount; i++) if (!g_fish[i].active) { slot = i; break; }
		if (slot < 0) { if (g_fishCount >= 96) return; slot = g_fishCount++; }
		g_fish[slot].guid = (uint64_t)c->GUID;
		g_fish[slot].homeX = pos.X; g_fish[slot].homeY = pos.Y; g_fish[slot].z = pos.Z;
		g_fish[slot].heading = (float)(Frand() * 2.0 * 3.14159265358979);
		g_fish[slot].active = true;
	}

	static void FishWander(cube::GameController* gc) {
		if (g_fishCount == 0 || !gc || !gc->local_player) return;
		DWORD now = GetTickCount();
		double dt = (double)(now - g_lastWanderTick) / 1000.0;
		g_lastWanderTick = now;
		if (dt <= 0.0 || dt > 0.5) dt = 0.06;
		const double PI = 3.14159265358979;
		int64_t px, py, pz;
		if (!FishReadPos(gc->local_player, &px, &py, &pz)) return;
		int active = 0, wandered = 0, skipped = 0, dead = 0;
		static uint64_t s_watch = 0; static int64_t s_watchPrevX = 0; bool watchThis = false;
		int64_t watchFx = 0, watchNx = 0, watchRx = 0;
		for (int i = 0; i < g_fishCount; i++) {
			if (!g_fish[i].active) continue;
			active++;
			cube::Creature* c = FishFindByGuid(gc, g_fish[i].guid);
			if (!c) { g_fish[i].active = false; dead++; continue; }
			int64_t fx, fy, fz;
			if (!FishReadPos(c, &fx, &fy, &fz)) continue;
			double dpx = (double)(px - fx) / (double)kBlockUnits;
			double dpy = (double)(py - fy) / (double)kBlockUnits;
			if (std::sqrt(dpx * dpx + dpy * dpy) < kFishCombatDist) { skipped++; continue; }
			double hx = (double)(g_fish[i].homeX - fx) / (double)kBlockUnits;
			double hy = (double)(g_fish[i].homeY - fy) / (double)kBlockUnits;
			double distH = std::sqrt(hx * hx + hy * hy);
			double h = g_fish[i].heading + (Frand() - 0.5) * kFishTurn * dt;
			if (distH > kFishHomeRadius) {
				double toHome = std::atan2(hy, hx);
				double diff = toHome - h; while (diff > PI) diff -= 2 * PI; while (diff < -PI) diff += 2 * PI;
				double mx = kFishTurn * dt * 2.0; if (diff > mx) diff = mx; if (diff < -mx) diff = -mx;
				h += diff;
			}
			g_fish[i].heading = (float)h;
			double adv = kFishSpeed * dt; if (adv < 0.25) adv = 0.25; // Mindestschritt fuer Sichtbarkeit
			int64_t nx = fx + (int64_t)(std::cos(h) * adv * (double)kBlockUnits);
			int64_t ny = fy + (int64_t)(std::sin(h) * adv * (double)kBlockUnits);
			FishWritePos(c, nx, ny, g_fish[i].z);
			wandered++;
			if (s_watch == 0) s_watch = g_fish[i].guid;
			if (g_fish[i].guid == s_watch) {
				int64_t rx, ry, rz; FishReadPos(c, &rx, &ry, &rz);
				watchThis = true; watchFx = fx; watchNx = nx; watchRx = rx;
			}
		}
		static DWORD s_lastLog = 0;
		if (now - s_lastLog > 1500) {
			s_lastLog = now;
			// std::cout << "[FishWander] aktiv=" << active << " gewandert=" << wandered
				// << " uebersprungen(Kampf)=" << skipped << " weg=" << dead << std::endl;
			if (watchThis) {
				int64_t moved = watchNx - watchFx;
				int64_t stuck = watchFx - s_watchPrevX;
				// std::cout << "  Beobachtungs-Fisch: fx=" << (long long)(watchFx / kBlockUnits)
					// << " geschrieben nx=" << (long long)(watchNx / kBlockUnits)
					// << " readback=" << (long long)(watchRx / kBlockUnits)
					// << " | seit letztem Log tatsaechlich bewegt=" << (long long)(stuck / (kBlockUnits/100)) << " /100Block"
					// << (watchRx == watchNx ? " (Schreibzugriff HAFTET)" : " (Schreibzugriff verworfen!)") << std::endl;
				s_watchPrevX = watchFx;
			}
		}
	}

	// Spawnt EINEN Fisch einer Art rund um ein Zentrum (Radius in Bloecken).
	static void SpawnOneFish(const FishSpecies& sp, Vector3<int64_t> center, uint32_t playerLevel,
		double minR, double maxR) {
		double ang = Frand() * 2.0 * 3.14159265358979;
		double dist = minR + Frand() * (maxR - minR);
		int64_t ox = (int64_t)(std::cos(ang) * dist * (double)kBlockUnits);
		int64_t oy = (int64_t)(std::sin(ang) * dist * (double)kBlockUnits);
		// leichte Tiefenstreuung, damit die Fische nicht alle auf einer Ebene sind
		int64_t oz = (int64_t)((Frand() - 0.5) * 2.0 * (double)kBlockUnits);

		Vector3<int64_t> pos(center.X + ox, center.Y + oy, center.Z + oz);

		int level = (int)playerLevel + sp.levelBias;
		if (level < 1) level = 1;

		// SpawnMonster: owner_guid=0 -> wilde Kreatur (kein Begleiter). Die
		// Funktion ist intern SEH-abgesichert (siehe cube_util.cpp).
		cube::Creature* c = cube::util::SpawnMonster(pos, sp.race, sp.hostility, (uint32_t)level);
		if (c) {
			// ECHT feindlich machen (Faktion +0x60=0, Aggro-Bit +0x124|0x20).
			// Ohne das behandelt das Spiel den Spawn als friedlichen Begleiter
			// (Companion-Init 0x5EE7A0). Siehe cube_util.cpp / MakeHostile.
			cube::util::MakeHostile(c);
			// Native Wander-KI anhaengen: Fisch schwimmt um seinen Spawn (Spiel-KI,
			// haftet -- ersetzt die nutzlose CompanionBehavior durch RandomWalk).
			cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);  // nativ 200; wir hatten 0
			cube::util::AttachRandomWalk(c, pos.X, pos.Y);
			// std::cout << "[OceanContent] " << sp.name << " (Lvl " << level << ") gespawnt bei Block ("
				// << (long long)(pos.X / kBlockUnits) << ", " << (long long)(pos.Y / kBlockUnits)
				// << ", " << (long long)(pos.Z / kBlockUnits) << ")." << std::endl;
		}
	}

	// Spawnt einen ganzen Schwarm um ein Zentrum.
	static void SpawnFishSchool(cube::GameController* gc, Vector3<int64_t> center, int count) {
		if (!gc) return;
		// Fische IMMER in der Tiefe spawnen: mindestens 15 Bloecke unter der
		// Oberflaeche, oder auf Spielertiefe, wenn er tiefer taucht. So schwimmen
		// sie auch, wenn der Spieler an der Oberflaeche (Z=0) steht.
		int64_t maxZ = (int64_t)-15 * kBlockUnits;
		if (center.Z > maxZ) center.Z = maxZ;
		uint32_t plvl = PlayerLevel(gc);

		// ART-REIN: eine Art fuer den ganzen Schwarm (die Arten wechseln von
		// Schwarm zu Schwarm durch, weil pro Schwarm neu gewichtet gewaehlt wird).
		const FishSpecies& sp = PickWeighted();
		for (int i = 0; i < count; i++) {
			SpawnOneFish(sp, center, plvl, 8.0, 100.0);
		}

		// Seltener Mini-Boss-Hai (Beispiel fuer "eigene Kreatur").
		if ((std::rand() % 100) < kGiantSharkChancePercent) {
			SpawnOneFish(kGiantShark, center, plvl, 10.0, 100.0);
			// std::cout << "[OceanContent] Ein RIESENHAI ist aufgetaucht!" << std::endl;
		}
	}

	void SpawnFishSchoolNearPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) {
			// std::cout << "[OceanContent] Kein Spieler -- Fischschwarm uebersprungen." << std::endl;
			return;
		}
		Vector3<int64_t> p = gc->local_player->entity_data.position;
		int count = 6 + (std::rand() % 5); // 6..10 Fische
		// std::cout << "[OceanContent] Spawne Fischschwarm (" << count << ") beim Spieler." << std::endl;
		SpawnFishSchool(gc, p, count);
	}

	void ToggleAutoFish() {
		g_autoFish = !g_autoFish;
		g_lastFishTick = 0; // beim Einschalten sofort einen Schwarm
		// std::cout << "[OceanContent] Auto-Fische " << (g_autoFish ? "AN" : "AUS")
			// << (g_autoFish ? " (spawnt periodisch in Spielernaehe -- im Ozean einschalten)" : "") << std::endl;
	}

	bool IsAutoFishEnabled() { return g_autoFish; }

	// A/B-Test fuer den MakeHostile-Fix (2026-07-13): spawnt WEST 4 KONTROLL-
	// Banditen OHNE MakeHostile (sollten friedlich bleiben = alte Companion-
	// Init) und OST 4 mit MakeHostile (sollten DICH ANGREIFEN). So sieht man
	// in EINEM Test, ob der Fix (Faktion +0x60=0 + Aggro-Bit +0x124|0x20) wirkt.
	void SpawnHostilityTest(cube::GameController* gc) {
		if (!gc || !gc->local_player) {
			// std::cout << "[OceanContent] Kein Spieler -- Test uebersprungen." << std::endl;
			return;
		}
		Vector3<int64_t> p = gc->local_player->entity_data.position;
		uint32_t plvl = PlayerLevel(gc);
		// std::cout << "[OceanContent] MAKEHOSTILE-TEST: WEST(-X)=4 Kontrolle (friedlich erwartet), "
			// "OST(+X)=4 feindlich (sollten angreifen)." << std::endl;
		for (int i = 1; i <= 4; i++) {
			int64_t off = (int64_t)i * 4;
			// Kontrolle nach Westen (ohne MakeHostile).
			// Die 2 ist hier ABSICHT und bleibt: sie ist der Kontrollwert des A/B-Tests
			// ("weder Monster noch NPC"), gegen den die Ostseite verglichen wird.
			Vector3<int64_t> west(p.X - off * kBlockUnits, p.Y, p.Z);
			cube::util::SpawnMonster(west, cube::Creature::Race::Bandit, 2, plvl);
			// Feindlich nach Osten (mit MakeHostile).
			Vector3<int64_t> east(p.X + off * kBlockUnits, p.Y, p.Z);
			cube::Creature* c = cube::util::SpawnMonster(east, cube::Creature::Race::Bandit, 2, plvl);
			if (c) cube::util::MakeHostile(c);
		}
		// std::cout << "[OceanContent]   Fertig: Greifen die OESTLICHEN (rechts) dich an und die "
			// "WESTLICHEN (links) nicht, wirkt der Fix." << std::endl;
	}

	// ===============================================================
	// Phase 1b: echte Ozean-Erkennung in der Weltgenerierung
	// ---------------------------------------------------------------
	// Wasserpegel = Z 0 (Bloecke). "Ocean" ist im Spiel Tile-Elevation < 0
	// (bewiesen aus dem Karten-Label-Code 0x004e5498: XOR EAX,EAX;
	// CMP [tile+0x18], 0; JGE -> kein Ozean). Tile via FUN_00487da0(worldCtx,
	// chunkX, chunkY); tile+0x18 = Elevation (int).
	//
	// SICHER: OnZoneGenerated reiht die Zone nur in eine Queue ein; das
	// eigentliche Spawnen passiert DEFERRED im OnGameTick (nicht re-entrant
	// zur laufenden Weltgenerierung).
	// ===============================================================
	using Fn_WorldTile_t = void* (__thiscall*)(void* worldCtx, int chunkX, int chunkY); // FUN_00487da0

	static uint32_t OcRebase(uint32_t staticAddr) {
		return Host::Re(staticAddr);
	}

	static bool GetTileElevation(cube::GameController* gc, int cx, int cy, int& outElev) {
		__try {
			void* worldCtx = (void*)((uint8_t*)gc + 0x2e4);
			void* tile = ((Fn_WorldTile_t)OcRebase(0x00487da0))(worldCtx, cx, cy);
			if (!tile) return false;
			outElev = *(int*)((uint8_t*)tile + 0x18);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	static const int kZoneBlocks = 256;             // 1 Zone = 256 Bloecke (block/256 = zone)
	static const int kOceanZoneChancePercent = 35;  // nicht jede Ozean-Zone befuellen
	static bool g_oceanGenEnabled = true;           // Phase-1b Auto-Populate an/aus

	// Kleine Ringpuffer-Queue neu generierter Zonen (OnZoneGenerated -> OnGameTick).
	static const int kMaxQueued = 48;
	static int  g_qX[kMaxQueued];
	static int  g_qY[kMaxQueued];
	static int  g_qHead = 0, g_qTail = 0;

	static void EnqueueZone(int x, int y) {
		int next = (g_qTail + 1) % kMaxQueued;
		if (next == g_qHead) return; // voll -> aeltestes verwerfen (einfach ignorieren)
		g_qX[g_qTail] = x; g_qY[g_qTail] = y; g_qTail = next;
	}

	// DIAGNOSE: verarbeitet EINE eingereihte Zone und loggt (die ersten paar)
	// die gemessene Elevation -- so sehen wir, ob OnZoneGenerated feuert und
	// welche Werte FUN_00487da0 fuer Zonen-Koordinaten liefert. Spawnt bewusst
	// NICHTS (das echte Spawnen macht die robuste ProcessOceanNearPlayer()).
	static int g_zoneDiagCount = 0;
	static void DiagnoseOceanQueue(cube::GameController* gc) {
		if (!gc) return;
		if (g_qHead == g_qTail) return; // leer
		int zx = g_qX[g_qHead], zy = g_qY[g_qHead];
		g_qHead = (g_qHead + 1) % kMaxQueued;
		if (g_zoneDiagCount >= 8) return;
		g_zoneDiagCount++;
		int elev = -99999;
		bool ok = GetTileElevation(gc, zx, zy, elev);
		// std::cout << "[OceanContent][DIAG] Zone (" << zx << "," << zy
			// << ") FUN_00487da0 ok=" << (ok ? 1 : 0) << " elev=" << elev
			// << ((ok && elev < 0) ? "  (< 0 = Ozean)" : "") << std::endl;
	}

	// ROBUSTE Ozean-Erkennung ueber die SPIELER-Position (ohne Koordinaten-
	// Unsicherheit): Wasseroberflaeche = Z 0. Ist der Spieler auf/unter dem
	// Wasserpegel (Z <= kOceanSurfaceZ), gilt er als "im Ozean" -> periodisch
	// einen kleinen Schwarm in seiner Naehe spawnen.
	static const int64_t kOceanSurfaceZ = 2; // Bloecke Toleranz um die Oberflaeche
	static DWORD g_lastOceanFishTick = 0;
	static const DWORD kOceanFishIntervalMs = 9000;

	static void ProcessOceanNearPlayer(cube::GameController* gc) {
		if (!g_oceanGenEnabled || !gc || !gc->local_player) return;
		int64_t pzBlocks = gc->local_player->entity_data.position.Z / kBlockUnits;
		if (pzBlocks > kOceanSurfaceZ) return; // Spieler ueber Wasser (Land)

		DWORD now = GetTickCount();
		if (g_lastOceanFishTick != 0 && now - g_lastOceanFishTick < kOceanFishIntervalMs) return;
		g_lastOceanFishTick = now;

		Vector3<int64_t> p = gc->local_player->entity_data.position;
		int count = 3 + (std::rand() % 3); // 3..5
		// std::cout << "[OceanContent] Ozean (Spieler-Z=" << pzBlocks << ") -> " << count << " Fische." << std::endl;
		SpawnFishSchool(gc, p, count);
	}

	void ToggleOceanGen() {
		g_oceanGenEnabled = !g_oceanGenEnabled;
		// std::cout << "[OceanContent] Ozean-Weltgen (Fische in Ozean-Zonen) "
			// << (g_oceanGenEnabled ? "AN" : "AUS") << std::endl;
	}
	bool IsOceanGenEnabled() { return g_oceanGenEnabled; }

	// --- Framework-Anbindung ----------------------------------------
	class OceanContentMod : public AdventureFramework::IAdventureMod {
	public:
		void OnZoneGenerated(cube::GameController* gc, cube::World* world, int zone_x, int zone_y, cube::Zone* zone) override {
			(void)gc; (void)world; (void)zone;
			EnqueueZone(zone_x, zone_y); // deferred -> DiagnoseOceanQueue im Tick
		}
		void OnGameTick(cube::GameController* gc) override {
			DiagnoseOceanQueue(gc);       // Diagnose: loggt Zonen-Elevation
			ProcessOceanNearPlayer(gc);   // Phase 1b: Ozean beleben (Spieler-Z-basiert)
			if (!g_autoFish || !gc || !gc->local_player) return;
			DWORD now = GetTickCount();
			if (g_lastFishTick != 0 && now - g_lastFishTick < kAutoFishIntervalMs) return;
			g_lastFishTick = now;

			// Kleiner Schwarm etwas vor/neben dem Spieler.
			Vector3<int64_t> p = gc->local_player->entity_data.position;
			int count = 10 + (std::rand() % 3); // 3..5
			SpawnFishSchool(gc, p, count);
		}
	};

	static OceanContentMod g_oceanMod;

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_oceanMod);
		// std::cout << "[OceanContent] Initialisiert (Ozean-Fische + MakeHostile-Fix)." << std::endl;
	}
}
