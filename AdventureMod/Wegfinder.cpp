#include "Wegfinder.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>

namespace Wegfinder {

	static const double kUnitsPerBlock = 65536.0;
	static const double PI = 3.14159265358979;
	static const double kMaxClimbBps = 8.0;
	static const bool kDebugNav = true;

	class CW_LCG {
		uint32_t state;
	public:
		CW_LCG(uint32_t seed) : state(seed) {}
		uint32_t rand() {
			state = (state * 214013 + 2531011);
			return (state >> 16) & 0x7fff;
		}
	};

	static void SortRouteNearestNeighbor(std::vector<Waypoint>& route, int64_t startX, int64_t startY) {
		if (route.size() <= 1) return;

		std::vector<Waypoint> sorted;
		sorted.reserve(route.size());
		std::vector<bool> used(route.size(), false);

		int64_t curX = startX;
		int64_t curY = startY;

		for (size_t step = 0; step < route.size(); step++) {
			int best = -1;
			// FIX: Nutze 1e30 (eine echte mathematisch gigantische Zahl)
			double bestDist = 1e30;

			for (size_t i = 0; i < route.size(); i++) {
				if (used[i]) continue;

				// FIX: Rechne hier in Bloecke um, um einen mathematischen Overflow zu vermeiden!
				double dx = (double)(route[i].x - curX) / kUnitsPerBlock;
				double dy = (double)(route[i].y - curY) / kUnitsPerBlock;
				double d = dx * dx + dy * dy;

				if (d < bestDist) {
					bestDist = d;
					best = (int)i;
				}
			}
			if (best < 0) break;
			used[best] = true;
			sorted.push_back(route[best]);
			curX = route[best].x;
			curY = route[best].y;
		}

		route.swap(sorted);
	}

	std::vector<Waypoint> GenerateVillageRoute(cube::GameController* gc, int64_t flightHeightUnits, int waitTimeMs) {
		std::vector<Waypoint> route;
		if (!gc || !gc->local_player) return route;

		uint8_t* worldObj = (uint8_t*)gc + 0x2E4;
		uint32_t base = *(uint32_t*)(worldObj + 0x800188);

		int64_t px = gc->local_player->entity_data.position.X;
		int64_t py = gc->local_player->entity_data.position.Y;
		int curRx = (int)((px / 65536LL) / 16384LL);
		int curRy = (int)((py / 65536LL) / 16384LL);

		if (curRx <= 0 || curRy <= 0) { curRx = 512; curRy = 512; }

		for (int dx = -1; dx <= 1; dx++) {
			for (int dy = -1; dy <= 1; dy++) {
				int rx = curRx + dx;
				int rz = curRy + dy;

				uint32_t seed = (rx + 0x108a + rz * 0x400 + base * 3) & 0xffffffff;
				CW_LCG rng(seed);

				bool is_spawn = (rx == 512 && rz == 512);
				bool coin1 = (rng.rand() & 1) == 0;
				bool pos_from_spawn = false;

				if (coin1 || is_spawn) {
					rng.rand(); rng.rand();
					if (is_spawn) pos_from_spawn = true;
					else if (rng.rand() % 10 == 0) { rng.rand(); rng.rand(); }
				}
				else {
					if ((rng.rand() & 1) == 0) {} rng.rand();
					if ((rng.rand() & 1) == 0) {} rng.rand();
				}

				int64_t siteX, siteZ;
				if (pos_from_spawn) {
					siteX = 8396928; siteZ = 8396928;
				}
				else {
					siteZ = rz * 0x4000 + rng.rand() % 0x3c00 + 0x200;
					siteX = rx * 0x4000 + 0x200 + rng.rand() % 0x3c00;
				}

				siteX = (siteX & 0xfffff800) + 0x400;
				siteZ = (siteZ & 0xfffff800) + 0x400;

				Waypoint wp;
				wp.x = siteX * 65536LL;
				wp.y = siteZ * 65536LL;
				wp.z = flightHeightUnits;
				wp.waitTimeMs = waitTimeMs;

				route.push_back(wp);
			}
		}

		SortRouteNearestNeighbor(route, px, py);

		if (kDebugNav) {
			// std::cout << "[Wegfinder] Route mit " << route.size() << " Waypoints (NN-sortiert) generiert." << std::endl;
		}

		return route;
	}

	void TickMovement(AIState& ai, const std::vector<Waypoint>& route,
		int64_t& posX, int64_t& posY, int64_t& posZ,
		float& velX, float& velY, float& velZ, float& rot,
		double speedBps, double turnRadPerSec, unsigned int dt) {

		if (route.empty()) return;

		double dts = (double)dt / 1000.0;
		if (dts <= 0.0 || dts > 0.25) dts = 0.05; // nur extreme Ruckler deckeln -> sonst gleichmaessig

		if (ai.targetWaypointIndex < 0 || ai.targetWaypointIndex >= (int)route.size()) {
			ai.targetWaypointIndex = 0;
		}

		Waypoint target = route[ai.targetWaypointIndex];

		double dxBlocks = (double)(target.x - posX) / kUnitsPerBlock;
		double dyBlocks = (double)(target.y - posY) / kUnitsPerBlock;
		double distBlocks = std::sqrt(dxBlocks * dxBlocks + dyBlocks * dyBlocks);

		const double arrivalThresholdBlocks = 40.0;

		if (distBlocks < arrivalThresholdBlocks) {
			if (!ai.isWaiting) {
				ai.isWaiting = true;
				ai.waitStartTime = GetTickCount();
				velX = 0.0f; velY = 0.0f; velZ = 0.0f;
			}
			else if (GetTickCount() - ai.waitStartTime > (DWORD)target.waitTimeMs) {
				ai.isWaiting = false;
				ai.targetWaypointIndex = (ai.targetWaypointIndex + 1) % (int)route.size();
				target = route[ai.targetWaypointIndex];
				dxBlocks = (double)(target.x - posX) / kUnitsPerBlock;
				dyBlocks = (double)(target.y - posY) / kUnitsPerBlock;
			}
		}

		if (!ai.isWaiting) {
			double targetAngle = std::atan2(dyBlocks, dxBlocks);
			double diff = targetAngle - ai.heading;
			while (diff > PI) diff -= 2.0 * PI;
			while (diff < -PI) diff += 2.0 * PI;

			double maxTurn = turnRadPerSec * dts;
			if (diff > maxTurn) diff = maxTurn;
			if (diff < -maxTurn) diff = -maxTurn;
			ai.heading += diff;

			double advBlocks = speedBps * dts;
			const double kMaxStepBlocks = 6.0; // nur echte Spruenge deckeln (kein Teleport in ungeladenes Gebiet)
			if (advBlocks > kMaxStepBlocks) advBlocks = kMaxStepBlocks;
			posX += (int64_t)(std::cos(ai.heading) * advBlocks * kUnitsPerBlock);
			posY += (int64_t)(std::sin(ai.heading) * advBlocks * kUnitsPerBlock);

			velX = (float)(std::cos(ai.heading) * speedBps);
			velY = (float)(std::sin(ai.heading) * speedBps);
			rot = (float)(ai.heading * 57.29577951308232) - 90.0f;
		}

		double dzBlocks = (double)(target.z - posZ) / kUnitsPerBlock;
		if (ai.isWaiting) {
			velZ = 0.0f;
		}
		else if (std::abs(dzBlocks) > 0.5) {
			double climbVel = dzBlocks * 2.0;
			if (climbVel > kMaxClimbBps) climbVel = kMaxClimbBps;
			if (climbVel < -kMaxClimbBps) climbVel = -kMaxClimbBps;
			velZ = (float)climbVel;
			posZ += (int64_t)(climbVel * kUnitsPerBlock * dts);
		}
		else {
			velZ = 0.0f;
			posZ = target.z;
		}

		static DWORD s_lastDebug = 0;
		if (kDebugNav) {
			DWORD now = GetTickCount();
			if (now - s_lastDebug > 2000) {
				s_lastDebug = now;
				// std::cout << "[Wegfinder] WP=" << ai.targetWaypointIndex
					// << " dist=" << distBlocks << " blk heading=" << ai.heading
					// << " velZ=" << velZ << std::endl;
			}
		}
	}
}