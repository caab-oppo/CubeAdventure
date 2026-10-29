#include "AirshipSpawner.h"
#include "Host.h"
#include "Wegfinder.h"
#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>

// ===================================================================
// AirshipSpawner.cpp -- Piratenschiff, das zwischen Doerfern pendelt.
// Ablauf: EINSTEIGEN (neben Spieler absinken) -> FAHRT (Wegfinder-Route).
//
// WICHTIG (2026-07-24): tree_populate (0x208FF0) crasht in der Alpha IMMER.
// Loesung: Schiff EINMAL einfuegen (Objekt lebt trotz Crash), Objekt-Zeiger
// merken und jede Runde unser 0x78-Paket DIREKT auf das lebende Objekt
// schreiben. Kein g_broken mehr -> die Bewegung laeuft ueber unseren Code.
// Alle Koordinaten im Log in BLOECKEN.
// ===================================================================

namespace AirshipSpawner {

	static const double kUnitsPerBlock = 65536.0;
	static const int    kNumAirships   = 1;
	static const int    kCrewPerShip   = 5;

	// Fahrt
	static const double kSpeedBps          = 13.0;  // Max-Reisetempo (Bloecke/Sekunde) -- Drossel senkt es beim Laden
	static const double kTurnRadPerSec     = 0.6;   // Einlenken
	static const double kFlightHeightBlocks= 35.0;  // Flughoehe UEBER Spieler (relativ)

	// Einstiegs-Phase
	static const double kSpawnHeightBlocks = 25.0;  // Spawn-Hoehe ueber Spieler
	static const double kBoardHeightBlocks = 5.0;   // Absink-Ziel (Deck ~ Spielerhoehe)
	static const double kBoardSideBlocks   = 5.0;   // seitlicher Versatz zum Spieler
	static const DWORD  kBoardHoldMs       = 15000; // Einstiegsfenster
	static const double kVertBps           = 8.0;   // Sink-Tempo

	// Terrain-Following (gegen Clipping in Berge)
	static const int    kGroundClearBlk = 34;    // Freihoehe ueber Boden (Rumpf haengt runter -> mehr)
	static const int    kScanUpBlk      = 80;    // Gipfel bis so weit UEBER dem Schiff suchen
	static const int    kScanDownBlk    = 100;   // Boden bis so weit UNTER dem Schiff suchen
	static const int    kScanStep       = 5;     // Abtast-Schrittweite (Bloecke)
	static const double kFollowMaxBps   = 22.0;  // max Steig-/Sinktempo (weiches Folgen)
	static const double kRiseTau        = 0.40;  // schnell auf Gipfelhoehe (kleine tau)
	static const double kFallTau        = 2.50;  // langsam sinken -> glatter Bogen (blaue Linie) (je h�her der Wert desto flacher der Bogen)

#pragma pack(push, 1)
	struct Airship {
		uint64_t GUID;
		uint32_t b1;
		uint32_t field_c;
		int64_t  posX, posY, posZ;
		float    velX, velY, velZ;
		float    rot;
		int64_t  stX, stY, stZ;
		float    pathrot;
		uint32_t field_54;
		int64_t  tgX, tgY, tgZ;
		uint8_t  flight_stage;
		uint8_t  u1, u2, u3;
		uint32_t b2;
	};
#pragma pack(pop)
	typedef char assert_airship_size[(sizeof(Airship) == 0x78) ? 1 : -1];

	static Airship g_airships[kNumAirships];
	static void*   g_shipObj[kNumAirships];   // lebender Objekt-Zeiger in der AirshipMap
	static bool    g_shipReady[kNumAirships];  // schon eingefuegt?
	static bool  g_enabled  = false;
	static bool  g_spawned  = false;
	static bool  g_broken   = false;
	static DWORD g_lastTick = 0;
	static bool  g_verified = false;

	// Einstiegs-Phase
	enum ShipPhase { PHASE_BOARDING, PHASE_CRUISING };
	static ShipPhase g_phase = PHASE_BOARDING;
	static int64_t   g_boardX = 0, g_boardY = 0, g_boardZ = 0;
	static DWORD     g_holdStart = 0;
	static unsigned long g_tickCount = 0;
	static double g_avgFrameMs = 30.0;      // geglaettete Framezeit -> Lade-Last
	static double g_throttle   = 1.0;
	static int64_t g_targetZ[kNumAirships];     // geglaettete Zielhoehe
	static int64_t g_rawTargetZ[kNumAirships];  // rohe (sprunghafte) Zielhoehe aus der Abtastung
	static DWORD   g_lastGroundMs = 0;

	static std::vector<Wegfinder::Waypoint> g_flightRoute;
	static Wegfinder::AIState g_shipAI[kNumAirships];

	// --- Bergdorf-Taxi (gerichteter Einzelflug) -------------------------------
	static bool    g_taxiMode   = false;  // laeuft ein Taxi-Flug (DockAt/Depart)?
	static int64_t g_taxiTgtX   = 0, g_taxiTgtY = 0;
	static DWORD   g_boardHoldMs = kBoardHoldMs; // Boarding-Fenster (Piraten-Route)
	static bool    g_noCrew     = false;  // Taxi: keine feindliche Crew
	static bool    g_taxiArrived = false; // Zieldorf erreicht -> Despawn
	static bool    g_taxiHasTarget = false; // Ziel gekauft -> Abflug geplant
	static DWORD   g_departAt   = 0;      // Zeitpunkt (GetTickCount) fuer den Abflug

	struct CrewMember { uint64_t guid; double offX, offY, offZ; };
	static CrewMember g_crew[kNumAirships][kCrewPerShip];
	static int        g_crewCount[kNumAirships];

	static cube::Creature* FindByGuid(cube::GameController* gc, uint64_t guid) {
		if (!gc) return nullptr;
		__try { auto* m = gc->world.EntitesMap; return m ? m->Find(guid) : nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static bool WritePos(cube::Creature* c, int64_t x, int64_t y, int64_t z) {
		if (!c) return false;
		__try { c->entity_data.position.X = x; c->entity_data.position.Y = y; c->entity_data.position.Z = z; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static uint32_t PlayerLevel(cube::GameController* gc) {
		if (gc && gc->local_player) { int lv = gc->local_player->entity_data.level; return (uint32_t)(lv > 0 ? lv : 1); }
		return 1;
	}
	static uint32_t ImageBase() { return (uint32_t)GetModuleHandleA(NULL); }
	static float  D2R(float d) { return d * 0.017453292519943295f; }

	using Fn_ValueAt   = void* (__thiscall*)(void*, const void*);
	using Fn_Subscript = void** (__thiscall*)(void*, const void*);
	using Fn_New       = void* (__cdecl*)(unsigned int);
	using Fn_Ctor      = void* (__thiscall*)(void*);
	using Fn_TreePop   = void* (__thiscall*)(void*, const void*);

	static bool MapValid(void* mapField) {
		__try {
			uint32_t mapObj = *(uint32_t*)mapField;
			if (mapObj < 0x10000 || mapObj == 0xffffffff) return false;
			uint32_t root = *(uint32_t*)(mapObj + 4);
			if (root < 0x10000 || root == 0xffffffff) return false;
			volatile unsigned char probe = *(unsigned char*)(root + 0xd);
			(void)probe;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static int CountNodes(void* node, int depth) {
		if (!node || depth > 64) return 0;
		uint8_t* n = (uint8_t*)node;
		if (n[0xd]) return 0;
		void* l = *(void**)(n + 0);
		void* r = *(void**)(n + 8);
		return 1 + CountNodes(l, depth + 1) + CountNodes(r, depth + 1);
	}

	// Einmalig einfuegen. Alte Reihenfolge: erst *slot=obj (Schiff erscheint),
	// dann Kern selbst kopieren, dann tree_populate best-effort (Crash egal).
	// Rueckgabe: obj-Zeiger (oder 0). rc via out.
	static void* InsertShip(void* mapField, const Airship* pkt, uint32_t base, int* rcOut) {
		Fn_ValueAt   mapGet = (Fn_ValueAt)  (Host::ReOff(0x2F000));
		Fn_Subscript mapSub = (Fn_Subscript)(Host::ReOff(0x68AD0));
		Fn_New       opNew  = (Fn_New)      (Host::ReOff(0x28D668));
		Fn_Ctor      ctor   = (Fn_Ctor)     (Host::ReOff(0x66510));
		void* existing = 0;
		__try {
			void* mapObj = *(void**)mapField;
			uint64_t searchKey = pkt->GUID;
			existing = mapObj ? mapGet(mapObj, &searchKey) : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { *rcOut = 3; return 0; }
		if (existing != 0) { *rcOut = 10; return existing; }

		void* obj = 0; void** slot = 0;
		__try {
			obj = opNew(0xA0);
			if (obj == 0) { *rcOut = 1; return 0; }
			obj = ctor(obj);
			uint64_t searchKey = pkt->GUID;
			slot = mapSub(mapField, &searchKey);
			if (slot == 0) { *rcOut = 2; return 0; }
			*slot = obj;                       // Schiff ist jetzt in der Map (erscheint)
			memcpy(obj, pkt, 0x78);            // Kern selbst setzen (Position/Ziel/GUID)
		} __except (EXCEPTION_EXECUTE_HANDLER) { *rcOut = 3; return obj; }

		// tree_populate nur als Bonus (setzt Render-Zusatzfelder). Crash wird verkraftet.
		uint8_t src[0xA0];
		memset(src, 0, sizeof(src));
		memcpy(src, pkt, 0x78);
		__try { ((Fn_TreePop)(Host::ReOff(0x208FF0)))(obj, src); *rcOut = 0; }
		__except (EXCEPTION_EXECUTE_HANDLER) { *rcOut = 4; }
		return obj;                            // Objekt lebt in JEDEM Fall
	}

	// Unser 0x78-Kern direkt auf das lebende Objekt schreiben (Bewegung -> Render).
	static void PushCore(void* obj, const Airship* pkt) {
		if (!obj) return;
		__try { memcpy(obj, pkt, 0x78); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	static bool SafeEnter(LPCRITICAL_SECTION cs) {
		__try { EnterCriticalSection(cs); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void SafeLeave(LPCRITICAL_SECTION cs) {
		__try { LeaveCriticalSection(cs); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	static long long B(int64_t units) { return (long long)(units / (int64_t)kUnitsPerBlock); }

	// Roh-Sonde: Block-Type an (bx,by,z). -1=Crash, -2=nullptr, sonst Type (0=Luft).
	static int ProbeType(cube::World* w, int bx, int by, int z) {
		__try { BlockColor* c = w->GetBlock(bx, by, z, (cube::Zone*)nullptr); return c ? (int)c->Type : -2; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	// Hoechster fester Block in der Saeule (bx,by) zwischen topZ..botZ. Sonst sehr klein.
	static int SampleSurfaceZ(cube::World* w, int bx, int by, int topZ, int botZ, int step) {
		__try {
			for (int z = topZ; z >= botZ; z -= step) {
				BlockColor* c = w->GetBlock(bx, by, z, (cube::Zone*)nullptr);
				if (c && c->Type != 0) return z;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return -2000000000;
	}
	// Zielhoehe = hoechster Boden (hier + voraus) + Freiraum. Kein Boden -> Hoehe halten.
	static int64_t TerrainFollowZ(cube::GameController* gc, int64_t posX, int64_t posY, int64_t posZ, double heading, int64_t fallbackZ) {
		cube::World* w = &gc->world;
		int shipZb = (int)(posZ / 65536);
		int bx = (int)(posX / 65536), by = (int)(posY / 65536);
		int top = shipZb + kScanUpBlk, bot = shipZb - kScanDownBlk;
		double ch = std::cos(heading), sh = std::sin(heading);
		// Flaechen-Abtaster: entlang der Flugrichtung UND seitlich (Schiff ist breit).
		// pf = vorwaerts, pl = seitlich (senkrecht zur Fahrtrichtung), in Bloecken.
		static const double pf[10] = { 0, 0,  0, 15, 15, 34, 34, 34, 58, 82 };
		static const double pl[10] = { 0, 13,-13, 11,-11, 13,-13, 0, 0,  0 };
		int maxG = -2000000000; int found = 0;
		for (int k = 0; k < 10; k++) {
			int cx = bx + (int)(ch * pf[k] - sh * pl[k]);
			int cy = by + (int)(sh * pf[k] + ch * pl[k]);
			int g = SampleSurfaceZ(w, cx, cy, top, bot, kScanStep);
			if (g > -1000000000) { found++; if (g > maxG) maxG = g; }
		}
		static DWORD s_tflog = 0; DWORD now = GetTickCount();
		if (now - s_tflog > 2000) {
			s_tflog = now;
		//	std::cout << "[Terrain] Block(" << bx << "," << by << ",z=" << shipZb << ") heading=" << (int)(heading * 57.3)
		//		<< "grad Treffer=" << found << "/10 maxBoden=" << (maxG <= -1000000000 ? -999 : maxG)
		//		<< " -> zielH=" << (maxG <= -1000000000 ? -999 : maxG + kGroundClearBlk) << std::endl;
		}
		if (maxG <= -1000000000) return fallbackZ;
		return (int64_t)(maxG + kGroundClearBlk) * 65536LL;
	}

	static void SpawnAround(cube::GameController* gc) {
		uint64_t startGuid = 0x50000000000ULL;
		uint32_t plvl = PlayerLevel(gc);
		int64_t px = gc->local_player->entity_data.position.X;
		int64_t py = gc->local_player->entity_data.position.Y;
		int64_t pz = gc->local_player->entity_data.position.Z;
		int64_t flightZ = pz + (int64_t)(kFlightHeightBlocks * kUnitsPerBlock);

		// std::cout << "[Airship] Spieler bei Block (" << B(px) << ", " << B(py) << ", " << B(pz) << ")." << std::endl;

		g_flightRoute = Wegfinder::GenerateVillageRoute(gc, flightZ, 15000);
		if (g_flightRoute.empty()) { return; }   // (Konsolenausgabe entfernt)
		// std::cout << "[Airship] Route mit " << g_flightRoute.size() << " Doerfern:" << std::endl;
		for (size_t w = 0; w < g_flightRoute.size(); w++) {
			// std::cout << "   Dorf " << w << ": Block (" << B(g_flightRoute[w].x) << ", " << B(g_flightRoute[w].y)
				// << ", " << B(g_flightRoute[w].z) << ")" << std::endl;
		}

		for (int i = 0; i < kNumAirships; i++) {
			Airship a; memset(&a, 0, sizeof(a));
			a.GUID = startGuid + (uint64_t)i;

			g_boardX = px + (int64_t)(kBoardSideBlocks * kUnitsPerBlock);
			g_boardY = py;
			g_boardZ = pz + (int64_t)(kBoardHeightBlocks * kUnitsPerBlock);
			g_phase  = PHASE_BOARDING;
			g_holdStart = 0;

			a.posX = g_boardX;
			a.posY = g_boardY;
			a.posZ = pz + (int64_t)(kSpawnHeightBlocks * kUnitsPerBlock);

			g_shipAI[i] = Wegfinder::AIState{};
			g_shipAI[i].targetWaypointIndex = 0;
			double dxB = (double)(g_flightRoute[0].x - a.posX) / kUnitsPerBlock;
			double dyB = (double)(g_flightRoute[0].y - a.posY) / kUnitsPerBlock;
			g_shipAI[i].heading = std::atan2(dyB, dxB);

			a.velX = a.velY = a.velZ = 0.0f;
			a.rot = (float)(g_shipAI[i].heading * 57.29577951308232) - 90.0f;
			a.pathrot = a.rot;
			a.flight_stage = 3;
			// Ziel = eigene Position -> native Engine steuert NICHT gegen (wir schreiben pos selbst).
			a.stX = a.tgX = a.posX; a.stY = a.tgY = a.posY; a.stZ = a.tgZ = a.posZ;
			g_airships[i] = a;
			g_shipObj[i]  = 0;
			g_shipReady[i] = false;
			g_targetZ[i]  = flightZ;
			g_rawTargetZ[i] = flightZ;

			g_crewCount[i] = 0;
			for (int j = 0; !g_noCrew && j < kCrewPerShip; j++) {
				double ox = (j % 2 == 0) ? -1.8 : 1.8;
				double oy = -3.0 + (double)(j / 2) * 2.5;
				double oz = 1.5;
				Vector3<int64_t> cpos(a.posX + (int64_t)(ox * kUnitsPerBlock),
					a.posY + (int64_t)(oy * kUnitsPerBlock),
					a.posZ + (int64_t)(oz * kUnitsPerBlock));
				cube::Creature* c = cube::util::SpawnMonster(cpos, cube::Creature::Race::Bandit, cube::util::Hostility::Monster, plvl);
				if (c) {
					cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);  // nativ 200
					g_crew[i][j].guid = (uint64_t)c->GUID;
					g_crew[i][j].offX = ox; g_crew[i][j].offY = oy; g_crew[i][j].offZ = oz;
					g_crewCount[i]++;
				}
			}
		}
		// std::cout << "[Airship] Schiff sinkt neben dir zum Einsteigen ab. Spring aufs Deck!" << std::endl;
	}

	static void TickAll(cube::GameController* gc, unsigned int dt) {
		g_tickCount++;
		// Adaptive Drossel: ruckelt das Spiel (hohe Framezeit = laedt), fliegt das Schiff langsamer.
		g_avgFrameMs = g_avgFrameMs * 0.9 + (double)dt * 0.1;
		g_throttle = 30.0 / g_avgFrameMs;
		if (g_throttle > 1.0) g_throttle = 1.0;
		if (g_throttle < 0.25) g_throttle = 0.25;
		double effSpeed = kSpeedBps * g_throttle;
		double dts = (double)dt / 1000.0;
		for (int i = 0; i < kNumAirships; i++) {
			Airship* a = &g_airships[i];

			if (g_phase == PHASE_BOARDING) {
				// Horizontal zum Dock/Steg EINSCHWENKEN (glatt gleiten + banken), nicht snappen.
				int64_t ddx = g_boardX - a->posX, ddy = g_boardY - a->posY;
				double dh = std::sqrt((double)ddx * (double)ddx + (double)ddy * (double)ddy);
				int64_t hstep = (int64_t)(kSpeedBps * 0.6 * kUnitsPerBlock * dts);
				if (hstep < 1) hstep = 1;
				if (dh > (double)hstep) {
					a->posX += (int64_t)((double)ddx / dh * (double)hstep);
					a->posY += (int64_t)((double)ddy / dh * (double)hstep);
					a->rot = (float)(std::atan2((double)ddy, (double)ddx) * 57.29577951308232) - 90.0f;
				} else { a->posX = g_boardX; a->posY = g_boardY; }
				// Vertikal ans Dock-Level annaehern.
				int64_t vstep = (int64_t)(kVertBps * kUnitsPerBlock * dts);
				if (vstep < 1) vstep = 1;
				if (a->posZ - g_boardZ > vstep) a->posZ -= vstep;
				else if (g_boardZ - a->posZ > vstep) a->posZ += vstep;
				else a->posZ = g_boardZ;
				bool docked = (dh <= (double)hstep) && (a->posZ == g_boardZ);
				if (docked) {
					if (g_taxiMode) {
						if (g_taxiHasTarget && GetTickCount() >= g_departAt) {
							g_phase = PHASE_CRUISING;
							// std::cout << "[Airship-Taxi] Ablegen -- Kurs auf das Zieldorf!" << std::endl;
						}
					} else if (g_holdStart == 0) {
						g_holdStart = GetTickCount();
						// std::cout << "[Airship] Einsteigen! Schiff legt in " << (g_boardHoldMs / 1000) << " s ab." << std::endl;
					} else if (GetTickCount() - g_holdStart > g_boardHoldMs) {
						g_phase = PHASE_CRUISING;
						// std::cout << "[Airship] Ablegen -- Kurs auf das naechste Dorf!" << std::endl;
					}
				}
				a->velX = a->velY = a->velZ = 0.0f;
			} else {
				Wegfinder::TickMovement(g_shipAI[i], g_flightRoute,
					a->posX, a->posY, a->posZ,
					a->velX, a->velY, a->velZ, a->rot,
					effSpeed, kTurnRadPerSec, dt);
				// Terrain-Following: Rohziel abtasten, dann WEICH annaehern -> kein Huepfen.
				DWORD nowG = GetTickCount();
				if (nowG - g_lastGroundMs > 200) {
					g_lastGroundMs = nowG;
					g_rawTargetZ[i] = TerrainFollowZ(gc, a->posX, a->posY, a->posZ, g_shipAI[i].heading, g_rawTargetZ[i]);
				}
				double cdts = dts; if (cdts > 0.1) cdts = 0.1;
				// Zielhoehe asymmetrisch glaetten: schnell HOCH (Gipfel freimachen),
				// sehr langsam RUNTER -> glatter Bogen wie die blaue Linie, keine Taeler.
				double dRaw = (double)(g_rawTargetZ[i] - g_targetZ[i]);
				double tau = (dRaw > 0.0) ? kRiseTau : kFallTau;
				double aT = 1.0 - std::exp(-cdts / tau);
				g_targetZ[i] += (int64_t)(dRaw * aT);
				// Hoehe weich ans Ziel fuehren, Tempolimit gegen Spruenge.
				double aP = 1.0 - std::exp(-cdts / 0.45);
				int64_t dz = (int64_t)((double)(g_targetZ[i] - a->posZ) * aP);
				int64_t cap = (int64_t)(kFollowMaxBps * kUnitsPerBlock * cdts);
				if (cap < 1) cap = 1;
				if (dz > cap) dz = cap;
				if (dz < -cap) dz = -cap;
				a->posZ += dz;
			}
			// Taxi: bei Ankunft am Zieldorf ANLEGEN (sanft absinken + schweben, KEIN Despawn).
			if (g_taxiMode && g_phase == PHASE_CRUISING) {
				double adx = (double)(g_taxiTgtX - a->posX) / kUnitsPerBlock;
				double ady = (double)(g_taxiTgtY - a->posY) / kUnitsPerBlock;
				if (adx * adx + ady * ady < 40.0 * 40.0) {
					// Ziel: der Steg auf der +X-Seite des Zieldorf-Turms (wie beim Start-Dock).
					int64_t sx = g_taxiTgtX + (int64_t)31 * (int64_t)kUnitsPerBlock;
					int64_t sy = g_taxiTgtY;
					int bx = (int)(sx / 65536), by = (int)(sy / 65536);
					int shipZb = (int)(a->posZ / 65536);
					int surf = SampleSurfaceZ(&gc->world, bx, by, shipZb + 50, shipZb - 150, 4);
					int64_t dockZ = (surf > -1000000000) ? (int64_t)(surf + 6) * 65536LL
						: (a->posZ - (int64_t)20 * (int64_t)kUnitsPerBlock);
					g_boardX = sx; g_boardY = sy; g_boardZ = dockZ;
					g_phase = PHASE_BOARDING;      // schwenkt jetzt zum Steg ein (horizontal + absinken)
					g_taxiHasTarget = false; g_departAt = 0; g_holdStart = 0;
					g_flightRoute.clear();
					// std::cout << "[Airship-Taxi] Zieldorf erreicht -- schwenkt zum Steg ein." << std::endl;
				}
			}
			a->pathrot = a->rot;
			a->flight_stage = 3;
			// Ziel = eigene Position halten -> Engine steuert nicht gegen, WIR bewegen pos.
			a->stX = a->tgX = a->posX; a->stY = a->tgY = a->posY; a->stZ = a->tgZ = a->posZ;

			// Besatzung auf dem Deck mitfuehren.
			double ch = std::cos(D2R(a->rot + 90.0f)), sh = std::sin(D2R(a->rot + 90.0f));
			for (int j = 0; j < g_crewCount[i]; j++) {
				cube::Creature* c = FindByGuid(gc, g_crew[i][j].guid);
				if (!c) continue;
				double ox = g_crew[i][j].offX, oy = g_crew[i][j].offY;
				int64_t wx = a->posX + (int64_t)((ox * ch - oy * sh) * kUnitsPerBlock);
				int64_t wy = a->posY + (int64_t)((ox * sh + oy * ch) * kUnitsPerBlock);
				int64_t wz = a->posZ + (int64_t)(g_crew[i][j].offZ * kUnitsPerBlock);
				WritePos(c, wx, wy, wz);
			}

			// --- Koordinaten-Log alle 2 Sekunden ---
			static DWORD s_lastLog = 0;
			DWORD nowL = GetTickCount();
			if (nowL - s_lastLog > 2000) {
				s_lastLog = nowL;
				const char* ph = (g_phase == PHASE_BOARDING) ? "BOARDING" : "FAHRT";
				int wpi = g_shipAI[i].targetWaypointIndex;
				long long tX = 0, tY = 0, tZ = 0, distB = 0;
				bool waiting = g_shipAI[i].isWaiting;
				if (!g_flightRoute.empty() && wpi >= 0 && wpi < (int)g_flightRoute.size()) {
					tX = B(g_flightRoute[wpi].x); tY = B(g_flightRoute[wpi].y); tZ = B(g_flightRoute[wpi].z);
					double ddx = (double)(g_flightRoute[wpi].x - a->posX) / kUnitsPerBlock;
					double ddy = (double)(g_flightRoute[wpi].y - a->posY) / kUnitsPerBlock;
					distB = (long long)std::sqrt(ddx * ddx + ddy * ddy);
				}
				// std::cout << "[Airship] #" << g_tickCount << " " << ph
					// << " Pos=(" << B(a->posX) << "," << B(a->posY) << "," << B(a->posZ) << ")"
					// << " -> Dorf " << wpi << "(" << tX << "," << tY << "," << tZ << ")"
					// << " Dist=" << distB << "b warte=" << (waiting ? "JA" : "nein")
					// << " rot=" << (int)a->rot << " obj=" << (g_shipObj[i] ? "ja" : "NEIN")
					// << " tempo=" << (int)(effSpeed) << "bps(" << (int)(g_throttle*100) << "%) frame=" << (int)g_avgFrameMs << "ms"
					// << " zielH=" << B(g_targetZ[i]) << "b"
					// << std::endl;
			}
		}
	}

	static void Publish(cube::GameController* gc) {
		if (g_broken) return;
		void* mapField = (void*)&gc->world.AirshipMap;
		if (!MapValid(mapField)) { g_broken = true; return; }   // (Konsolenausgabe entfernt)
		LPCRITICAL_SECTION cs = (LPCRITICAL_SECTION)((uint8_t*)gc + 0x2E4 + 0x8000C0);
		if (!SafeEnter(cs)) { g_broken = true; return; }   // (Konsolenausgabe entfernt)
		uint32_t base = ImageBase();
		for (int i = 0; i < kNumAirships; i++) {
			if (!g_shipReady[i]) {
				int rc = 9;
				g_shipObj[i] = InsertShip(mapField, &g_airships[i], base, &rc);
				g_shipReady[i] = true;
				if (!g_verified) {
					g_verified = true;
					// Log ausserhalb des CS waere sauberer, aber einmalig ok.
					// std::cout << "[Airship] Insert rc=" << rc
						// << (rc == 0 ? " (voll ok)" : rc == 4 ? " (tree_populate crashte -- Schiff lebt trotzdem)"
							// : rc == 10 ? " (schon vorhanden)" : " (Objekt evtl. teilweise)")
						// << " obj=" << (g_shipObj[i] ? "ja" : "NEIN") << std::endl;
				}
			}
			// Jede Runde: unser bewegtes Paket direkt aufs lebende Objekt schreiben.
			PushCore(g_shipObj[i], &g_airships[i]);
		}
		SafeLeave(cs);
	}

	class AirshipMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override {
			if (!g_enabled || g_broken || !gc || !gc->local_player) return;
			// Beim Laden nicht neu spawnen -- ausser im Taxi-Modus (sonst wuerde der
			// Flug abbrechen und SpawnAround am Spieler eine Piraten-Route starten).
			if (*(uint8_t*)((uint8_t*)gc + 0x800585) != 0) { if (!g_taxiMode) g_spawned = false; return; }
			DWORD now = GetTickCount();
			unsigned int dt = (g_lastTick != 0) ? (unsigned int)(now - g_lastTick) : 50u;
			g_lastTick = now;
			if (dt > 1000) dt = 50;
			if (!g_spawned && !g_taxiMode) {
				int64_t px = gc->local_player->entity_data.position.X;
				int64_t py = gc->local_player->entity_data.position.Y;
				if (px == 0 && py == 0) return;
				SpawnAround(gc);
				g_spawned = true;
			}
			TickAll(gc, dt);
			Publish(gc);
			// (Kein Despawn mehr: das Schiff legt am Zieldorf an und bleibt dort schweben.
			//  Kauft der Spieler dort erneut eine Richtung, fliegt DASSELBE Schiff weiter.)
		}
	};
	static AirshipMod g_mod;

	bool IsEnabled() { return g_enabled; }

	void Toggle() {
		g_enabled = !g_enabled;
		g_spawned = false; g_broken = false; g_lastTick = 0; g_verified = false;
		g_phase = PHASE_BOARDING; g_holdStart = 0;
		g_flightRoute.clear();
		for (int i = 0; i < kNumAirships; i++) {
			g_shipAI[i] = Wegfinder::AIState{}; g_crewCount[i] = 0;
			g_shipObj[i] = 0; g_shipReady[i] = false;
		}
		// std::cout << "[Airship-Labor] STRG+8 -- Status: " << (g_enabled ? "AN" : "AUS") << std::endl;
	}

	bool IsBusy()     { return g_enabled && g_taxiMode; }
	bool IsDocked()   { return g_enabled && g_taxiMode && !g_taxiHasTarget; }
	bool IsCruising() { return g_enabled && g_taxiMode && g_phase == PHASE_CRUISING; }

	// Phase 1: Schiff erscheint am Turm und schwebt (beim Ansprechen des NPC).
	// Alle Koordinaten in POSITIONSEINHEITEN (1 Block = 65536). dockZ = Deckhoehe.
	void DockAt(cube::GameController* gc, int64_t dockX, int64_t dockY, int64_t dockZ) {
		if (!gc || !gc->local_player) return;
		if (g_enabled && g_taxiMode) return; // schon ein Schiff aktiv

		g_enabled = true; g_broken = false; g_lastTick = 0; g_verified = false;
		g_phase = PHASE_BOARDING; g_holdStart = 0;
		g_taxiMode = true; g_noCrew = true; g_taxiArrived = false;
		g_taxiHasTarget = false; g_departAt = 0;
		g_taxiTgtX = 0; g_taxiTgtY = 0;
		g_flightRoute.clear();

		int i = 0;
		Airship a; memset(&a, 0, sizeof(a));
		a.GUID = 0x50000000000ULL + (uint64_t)i;
		g_boardX = dockX;
		g_boardY = dockY;
		g_boardZ = dockZ + (int64_t)(kBoardHeightBlocks * kUnitsPerBlock);
		a.posX = dockX;
		a.posY = dockY;
		a.posZ = dockZ + (int64_t)(kSpawnHeightBlocks * kUnitsPerBlock);

		g_shipAI[i] = Wegfinder::AIState{};
		g_shipAI[i].targetWaypointIndex = 0;
		g_shipAI[i].heading = 0.0;
		a.velX = a.velY = a.velZ = 0.0f;
		a.rot = 0.0f; a.pathrot = 0.0f; a.flight_stage = 3;
		a.stX = a.tgX = a.posX; a.stY = a.tgY = a.posY; a.stZ = a.tgZ = a.posZ;
		g_airships[i] = a;
		g_shipObj[i] = 0;
		g_shipReady[i] = false;
		g_targetZ[i] = dockZ + (int64_t)(kFlightHeightBlocks * kUnitsPerBlock);
		g_rawTargetZ[i] = g_targetZ[i];
		g_crewCount[i] = 0;
		g_spawned = true; // OnGameTick ueberspringt SpawnAround

		// std::cout << "[Airship-Taxi] Schiff dockt am Turm an bei Block (" << B(dockX) << ", " << B(dockY)
			// << ") -- wartet auf Richtungskauf." << std::endl;
	}

	// Phase 2: Ziel setzen + nach delayMs ablegen (beim Kauf einer Richtung).
	void Depart(cube::GameController* gc, int64_t tgtX, int64_t tgtY, unsigned long delayMs) {
		if (!g_enabled || !g_taxiMode) { return; }   // (Konsolenausgabe entfernt)
		if (g_taxiHasTarget) return; // schon ein Ziel gesetzt
// 
		int64_t flightZ = g_targetZ[0];
		g_flightRoute.clear();
		Wegfinder::Waypoint wp; wp.x = tgtX; wp.y = tgtY; wp.z = flightZ; wp.waitTimeMs = 0;
		g_flightRoute.push_back(wp);
		g_taxiTgtX = tgtX; g_taxiTgtY = tgtY;

		int i = 0;
		double dxB = (double)(tgtX - g_airships[i].posX) / kUnitsPerBlock;
		double dyB = (double)(tgtY - g_airships[i].posY) / kUnitsPerBlock;
		g_shipAI[i].heading = std::atan2(dyB, dxB);
		g_shipAI[i].targetWaypointIndex = 0;

		g_taxiHasTarget = true;
		g_departAt = GetTickCount() + (DWORD)delayMs;
		// std::cout << "[Airship-Taxi] Richtung gekauft -- Abflug in " << (delayMs / 1000)
			// << "s zum Dorf bei Block (" << B(tgtX) << ", " << B(tgtY) << ")." << std::endl;
	}

	// Convenience: andocken + sofort Ziel setzen (Test/Einzelaufruf).
	void LaunchTo(cube::GameController* gc,
		int64_t dockX, int64_t dockY, int64_t dockZ,
		int64_t tgtX, int64_t tgtY, unsigned long boardHoldMs) {
		DockAt(gc, dockX, dockY, dockZ);
		Depart(gc, tgtX, tgtY, boardHoldMs);
	}

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_mod);
		// std::cout << "[Airship] Wegfinder + Einstieg bereit (STRG+8)." << std::endl;
	}
}
