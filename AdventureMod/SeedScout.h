#pragma once
//
// SeedScout.h -- Offline-Feature-Scout fuer Cube World Alpha (2013, post glider fix).
// ------------------------------------------------------------------------------------
// Berechnet das FEATURE-GRID (Doerfer, Dungeons, Plateaus, Berge, Seen ...) deterministisch
// aus dem Seed, OHNE Chunks zu laden. Portiert aus dem Reverse-Engineering-Projekt
// CubeWorld_Seed_Scout (TestingRat-blip): cw_seed (LCG), cw_noise (Value-Noise),
// cw_region (region_site), cw_gate (chunk_to_region), cw_featuregen (Feature-Grid pass1-4).
//
// HEADER-ONLY -> jeder Mod kann es einfach mit  #include "SeedScout.h"  nutzen:
//
//     uint32_t base = SeedScout::ReadBase(gc);               // Seed-base aus dem Spielspeicher
//     std::vector<SeedScout::Feature> feats;
//     SeedScout::FeaturesNear(base, blockX, blockZ, feats);  // Features rund um (x,z)
//     if (SeedScout::NearType(base, x, z, SeedScout::T_DUNGEON, 400)) { ... }
//     if (SeedScout::NearStructure(base, x, z, 400)) { ... }  // irgendein POI in der Naehe?
//
// Koordinaten sind BLOCK-Koordinaten. Eine Region = 0x4000 (16384) Bloecke; Spawn-Region (512,512).
// HINWEIS: Positionen/Radien sind ein bestmoeglicher Port; die Region-Seed-Formel + der rand-Stream
// sind im Ursprungsprojekt live-verifiziert. Bei Abweichungen ggf. Konstanten nachziehen.
//
#include <vector>
#include <map>
#include <cmath>
#include <cstdint>
#include <windows.h>

namespace SeedScout {

	// ---- Feature-Typen (aus cw_featuregen) ----
	// Ground-truth Feature-Typen (vom Nutzer im lebenden Spiel verifiziert -- die Namen aus dem
	// Ursprungs-Repo waren FALSCH). ALLE Typen sind Strukturen/POIs, es gibt kein Terrain-Feature.
	enum {
		T_EMPTY = 0,
		T_VILLAGE = 1,        // Dorf
		T_MTN_DUNGEON = 2,    // Bergdungeon
		T_FOREST_DUNGEON = 3, // Walddungeon
		T_LAKE_DUNGEON = 4,   // Seedungeon
		T_RUINS = 5,          // Ruinen
		T_CANYON_DUNGEON = 6, // Schluchtdungeon (Canyon)
		T_VALLEY_DUNGEON = 7, // Schluchtdungeon (Valley)
		T_GORGE_DUNGEON = 8,  // Schluchtdungeon
		T_PORTAL = 10,        // Portal
		T_ROCK_DUNGEON = 11,  // Rockdungeon
		T_TREE_DUNGEON = 12,  // Treedungeon
		T_DUNGEON = 14,       // Dungeon (Palast/gross)
		T_GORGE_DUNGEON2 = 15 // Schluchtdungeon
	};

	struct Feature { int type; int x, z; float radius; }; // x,z = Block-Zentrum

	static inline float f32(double v) { return (float)v; }

	// ---- MSVCR110 LCG (PROVEN) ----
	struct Rng { uint32_t s; Rng(uint32_t seed) : s(seed) {} int rnd() { s = s * 214013u + 2531011u; return (int)((s >> 16) & 0x7fff); } };

	// ---- Value-Noise (FUN_004d5d30), byte-exakt-verifiziert ----
	static inline double _corner(int ix, int iz) {
		uint32_t n = (uint32_t)(ix + iz * 57);
		n = n ^ (n << 13);
		uint32_t h = ((n * n * 60493u + 19990303u) * n + 0xD208DD0Du) & 0x7fffffff;
		return 1.0 - (double)h * 9.31322574615478515625e-10; // 2^-30
	}
	static inline float noise2d(double qx, double qz) {
		const double PI = 3.1415927; // float32-pi als double (bytes 0x400921fb5a7ed197)
		int cx = (int)qx, cz = (int)qz; // Trunkierung Richtung 0
		double fx = qx - cx, fz = qz - cz;
		double wx = (1.0 - std::cos(fx * PI)) * 0.5;
		double wz = (1.0 - std::cos(fz * PI)) * 0.5;
		double v00 = _corner(cx, cz), v01 = _corner(cx + 1, cz);
		double v10 = _corner(cx, cz + 1), v11 = _corner(cx + 1, cz + 1);
		double r0 = v00 * (1.0 - wx) + v01 * wx;
		double r1 = v10 * (1.0 - wx) + v11 * wx;
		return (float)(r0 * (1.0 - wz) + r1 * wz);
	}

	// ---- Region-Site (natives Dorf pro Region) ----
	struct Site { int x, z; int wet; float temp, humid; int elev; };

	// ---- Anbindung an die eigenen Biome (definiert in BiomeMod.cpp) ----
	// Der Biom-Mod aendert beim Anlegen einer Klimazelle Klima und Lava-Bit (Hook 0x5D7A70) und
	// schaltet je Biom native Orte ab (Hook 0x5DA280). Damit der Rechner hier dieselbe Welt
	// vorhersagt, laeuft jede berechnete Zelle durch ModAdjustSite und jedes Feature durch
	// ModFeatureAllowed. ModVersion aendert sich, sobald Biome/Schalter/erzwungene Zellen
	// sich aendern -- dann werden die Caches verworfen.
	//   Achtung Namen: temp = Zelle+0x0C, humid = Zelle+0x10 (so liest es dieser Port).
	uint32_t ModVersion();
	void ModAdjustSite(uint32_t base, int regX, int regZ, Site& st);
	bool ModFeatureAllowed(uint32_t base, int x, int z, int type);

	static inline uint32_t region_seed(uint32_t base, int regX, int regZ) {
		return (uint32_t)regX + 0x108au + (uint32_t)regZ * 0x400u + base * 3u;
	}
	static inline float _f32chain(int r, float mul, float div, bool hasAdd, float add) {
		float v = f32((double)f32((double)f32((double)r) * (double)mul) / (double)div);
		return hasAdd ? f32((double)v + (double)add) : v;
	}
	static inline float _dbl_f32(int r, double mul, double div, double add) {
		return f32((double)r * mul / div + add);
	}
	static inline Site region_site_calc(uint32_t base, int regX, int regZ, bool modAdjust = true) {
		Site st; st.wet = 0; st.temp = 0; st.humid = 0; st.elev = 0;
		Rng rng(region_seed(base, regX, regZ));
		bool isSpawn = (regX == 0x200 && regZ == 0x200);
		bool coin1 = (rng.rnd() & 1) == 0;
		bool posFromSpawn = false;
		if (coin1 || isSpawn) {
			st.temp = _f32chain(rng.rnd(), 0.4f, 32767.0f, true, 0.3f);
			st.humid = _dbl_f32(rng.rnd(), 0.4, 32767.0, 0.4000000059604645);
			if (isSpawn) posFromSpawn = true;
			else if (rng.rnd() % 10 == 0) {
				st.wet = 1;
				st.humid = _f32chain(rng.rnd(), 0.2f, 32767.0f, true, 0.8f);
				st.temp = _f32chain(rng.rnd(), 0.5f, 32767.0f, false, 0.0f);
			}
		} else {
			bool c2 = (rng.rnd() & 1) == 0;
			st.temp = _f32chain(rng.rnd(), 0.1f, 32767.0f, !c2, 0.9f);
			bool c3 = (rng.rnd() & 1) == 0;
			st.humid = _f32chain(rng.rnd(), 0.1f, 32767.0f, !c3, 0.9f);
		}
		int x, z;
		if (posFromSpawn) { x = 8396928; z = 8396928; }
		else {
			z = regZ * 0x4000 + (rng.rnd() % 0x3c00) + 0x200; // erst Z, dann X
			x = regX * 0x4000 + 0x200 + (rng.rnd() % 0x3c00);
		}
		st.x = (int)(((uint32_t)x & 0xfffff800u) + 0x400u); // Snap auf 0x800-Raster + 0x400
		st.z = (int)(((uint32_t)z & 0xfffff800u) + 0x400u);
		double o1 = noise2d(regX * 1.4 + (double)base, regZ * 1.4 + (double)base + 843.0);
		float e1 = f32(((double)o1 + 1.0) * 100.0 - 70.0);
		double o2 = noise2d(regX * 4.0 + (double)base, regZ * 4.0 + (double)base + 843.0);
		st.elev = (int)f32((double)e1 + (double)o2 * 30.0);
		if (st.elev < 1) {
			st.wet = 0;
			if (isSpawn) st.elev = rng.rnd() % 0x32 + 0x14;
			else {
				st.elev = (st.elev - 100 < -100) ? -100 : st.elev - 100;
				if ((rng.rnd() & 1) == 0) {
					st.temp = _f32chain(rng.rnd(), 0.1f, 32767.0f, true, 0.9f);
					st.humid = _f32chain(rng.rnd(), 0.1f, 32767.0f, true, 0.9f);
				} else {
					st.temp = _f32chain(rng.rnd(), 0.4f, 32767.0f, true, 0.3f);
					st.humid = _dbl_f32(rng.rnd(), 0.4, 32767.0, 0.4000000059604645);
				}
			}
		}
		if (modAdjust) ModAdjustSite(base, regX, regZ, st);   // eigene Biome (Klima, Lava-Bit)
		return st;
	}

	// Gecachte region_site (rein deterministisch; in generate_region sehr oft aufgerufen).
	static inline const Site& region_site(uint32_t base, int regX, int regZ) {
		static std::map<uint64_t, Site> cache;
		static uint32_t ver = 0xFFFFFFFFu;
		if (ModVersion() != ver) { cache.clear(); ver = ModVersion(); }   // Biome geaendert
		uint64_t key = ((uint64_t)base << 22) | ((uint64_t)(regX & 0x7ff) << 11) | (uint64_t)(regZ & 0x7ff);
		std::map<uint64_t, Site>::iterator it = cache.find(key);
		if (it != cache.end()) return it->second;
		cache[key] = region_site_calc(base, regX, regZ);
		return cache[key];
	}

	// ---- chunk_to_region (FUN_004febd0): naechstes region site zur x768-gewarpten Query ----
	static inline int _rblk(int coord, int off) { int v = coord + off; v = v + ((v >> 31) & 0x3fff); return v >> 14; }
	static inline void _warp(int worldX, int worldZ, int& qx, int& qz) {
		float wX = f32((double)f32((double)f32((double)noise2d((double)worldZ * 0.0005, 3423.0)) * 3.0) * 256.0);
		float wZ = f32((double)f32((double)f32((double)noise2d((double)worldX * 0.0005, 23421.0)) * 3.0) * 256.0);
		qx = (int)f32((double)worldX + (double)wX);
		qz = (int)f32((double)worldZ + (double)wZ);
	}
	static inline int _d2(const Site& s, int qx, int qz) {
		float dxf = (float)(s.x - qx), dzf = (float)(s.z - qz);
		return (int)f32((double)f32((double)dxf * (double)dxf) + (double)f32((double)dzf * (double)dzf));
	}
	static inline bool _inb(int rx, int rz) { return rx >= 0 && rz >= 0 && rx <= 0x3ff && rz <= 0x3ff; }
	static inline bool chunk_to_region(uint32_t base, int worldX, int worldZ, int& outRx, int& outRz) {
		int rlx = _rblk(worldX, -0x4000), rlz = _rblk(worldZ, -0x4000);
		int rhx = _rblk(worldX, +0x4000), rhz = _rblk(worldZ, +0x4000);
		int qx, qz; _warp(worldX, worldZ, qx, qz);
		bool found = false; int bestD = 0, brx = 0, brz = 0;
		for (int rx = rlx; rx <= rhx; rx++) for (int rz = rlz; rz <= rhz; rz++) {
			if (!_inb(rx, rz)) continue;
			int d = _d2(region_site(base, rx, rz), qx, qz);
			if (!found || d < bestD) { found = true; bestD = d; brx = rx; brz = rz; }
		}
		outRx = brx; outRz = brz; return found;
	}

	// ---- Feature-Grid Generator (cw_featuregen pass1-4) ----
	static inline int _tier(int regX, int regZ) {
		if (regX == 0x200 && regZ == 0x200) return 1;
		float d = f32((double)f32((double)(0x200 - regZ) * (double)(0x200 - regZ)) + (double)f32((double)(0x200 - regX) * (double)(0x200 - regX)));
		d = f32(std::sqrt((double)d));
		return 2 - (int)f32((double)d * -0.75);
	}
	static inline int _sub_count(int idx) { double x = idx / 64.0; return (int)((1.0 / (1.0 - x) - 1.0) * 20.0 + 1.0); }
	static inline void _subswitch(Rng& rng, int cnt) { if (cnt >= 5) rng.rnd(); }
	static inline float _free(int radius) {
		return f32((double)f32(2048.0) - (double)f32((double)f32((double)f32((double)radius) + (double)f32(256.0)) * (double)f32(2.0)));
	}
	static inline long long _rad16(int radius) { return (long long)f32((double)f32((double)radius) * (double)f32(65536.0)); }
	static inline long long _jit16(int rj, float scFr) {
		return (long long)f32((double)f32((double)f32((double)f32((double)rj) * (double)scFr) / (double)f32(32767.0)) * (double)f32(65536.0));
	}

	struct Cell { int type; long long x16, z16; float radius; int col, row; };

	// Erzeugt die 64 Feature-Zellen einer Region (Typ + Position + Radius).
	static inline void generate_region(uint32_t base, int regX, int regZ, Cell cells[64]) {
		for (int i = 0; i < 64; i++) { cells[i].type = 0; cells[i].x16 = 0; cells[i].z16 = 0; cells[i].radius = 0; cells[i].col = 0; cells[i].row = 0; }
		Rng rng(base + (uint32_t)regZ * 0x400u + (uint32_t)regX); // feature_seed
		Site site = region_site(base, regX, regZ);
		int rbX = regX * 0x4000, rbZ = regZ * 0x4000;

		// -- Pass 1: Region-Felder --
		int t = _tier(regX, regZ);
		if (t > 4) rng.rnd();                 // field[4]
		rng.rnd();                            // field[5] = rand()&3
		rng.rnd();                            // local_2ac = rand()%10000
		if (site.humid > f32(0.81)) rng.rnd();

		// -- Pass 2: Kandidaten (8x8 Tiles) --
		int own_col = (site.x >> 11) & 7, own_row = (site.z >> 11) & 7;
		struct Cand { float score; int col, row, cellidx; };
		std::vector<Cand> cands; cands.reserve(64);
		for (int col = 0; col < 8; col++) for (int row = 0; row < 8; row++) {
			bool is_own = (col == own_col && row == own_row);
			if (is_own) { // COPY-Pfad: eigenes Site-Tile = Dorf (Typ 1)
				int cr = rng.rnd();
				int ci = row + col * 8;
				cells[ci].type = T_VILLAGE;
				cells[ci].x16 = (long long)site.x << 16; cells[ci].z16 = (long long)site.z << 16;
				cells[ci].radius = f32((double)(cr % 200 + 0x200)); cells[ci].col = col; cells[ci].row = row;
				continue;
			}
			int tcx = rbX + col * 0x800 + 0x400, tcz = rbZ + row * 0x800 + 0x400;
			int nrx, nrz; chunk_to_region(base, tcx, tcz, nrx, nrz);
			if (!(nrx == regX && nrz == regZ)) continue; // Nachbar-Site naeher -> keine Draws
			int radius = ((int)((uint32_t)rng.rnd() & 0xff)) + 0x200;
			float scFr = _free(radius); long long scR16 = _rad16(radius);
			int rj1 = rng.rnd(); long long jz16 = _jit16(rj1, scFr);
			long long Z16 = ((long long)(rbZ + row * 0x800) << 16) + scR16 + 0x1000000 + jz16;
			int fZ = (int)(Z16 >> 16);
			int rj2 = rng.rnd(); long long jx16 = _jit16(rj2, scFr);
			long long X16 = ((long long)(rbX + col * 0x800) << 16) + scR16 + 0x1000000 + jx16;
			int fX = (int)(X16 >> 16);
			int ci = row + col * 8;
			cells[ci].x16 = X16; cells[ci].z16 = Z16; cells[ci].radius = f32((double)radius);
			cells[ci].type = 0; cells[ci].col = col; cells[ci].row = row;
			int nrx2, nrz2; chunk_to_region(base, fX, fZ, nrx2, nrz2);
			if (nrx2 == regX && nrz2 == regZ) {
				int sr = rng.rnd();
				float dd = f32((double)f32((double)(own_col - col) * (double)(own_col - col)) + (double)f32((double)(own_row - row) * (double)(own_row - row)));
				float dist = f32(std::sqrt((double)dd));
				float score = f32((double)f32((double)f32((double)(sr * 4) / 32767.0) + (double)dist) - 2.0);
				Cand c; c.score = score; c.col = col; c.row = row; c.cellidx = ci; cands.push_back(c);
			}
		}

		// -- Pass 3: Kandidaten-Verarbeitung (gerade Indizes, sortiert nach score aufsteigend) --
		std::vector<Cand> srt = cands;
		for (size_t a = 0; a + 1 < srt.size(); a++) for (size_t b = 0; b + 1 < srt.size() - a; b++)
			if (srt[b].score > srt[b + 1].score) { Cand tmp = srt[b]; srt[b] = srt[b + 1]; srt[b + 1] = tmp; }
		bool elevneg = site.elev < 0;
		int left = (int)srt.size(); int k = 0;
		for (int idx = 0; idx < 0x40; idx++) {
			if (left == 0) break;
			if (idx & 1) continue;
			left--;
			int cnt = _sub_count(idx);
			Cell* cell = (k < (int)srt.size() && srt[k].cellidx < 64) ? &cells[srt[k].cellidx] : nullptr;
			if ((idx >> 1) & 1) { // DUNGEON (Typ 14)
				rng.rnd(); rng.rnd(); rng.rnd(); _subswitch(rng, cnt);
				if (cell) {
					cell->type = T_DUNGEON; cell->radius = f32(150.0);
					cell->x16 = (long long)((((cell->x16 >> 16) & ~0xFF) + 0x80)) << 16;
					cell->z16 = (long long)((((cell->z16 >> 16) & ~0xFF) + 0x80)) << 16;
				}
			} else {
				int sel = rng.rnd() & 7; int ty = 0;
				if (sel == 0) ty = 2;
				else if (sel == 1) { ty = 3; rng.rnd(); }
				else if (sel == 2) ty = elevneg ? 0xf : 4;
				else if (sel == 3) { // Typ 5: reposition (2 jitter rands)
					ty = 5; int nr = ((int)((uint32_t)rng.rnd() & 0xff)) + 0x100;
					if (cell) {
						float scFr = _free(nr); long long scR16 = _rad16(nr);
						int rj1 = rng.rnd(); long long jz16 = _jit16(rj1, scFr);
						cell->z16 = ((long long)(rbZ + cell->row * 0x800) << 16) + scR16 + 0x1000000 + jz16;
						int rj2 = rng.rnd(); long long jx16 = _jit16(rj2, scFr);
						cell->x16 = ((long long)(rbX + cell->col * 0x800) << 16) + scR16 + 0x1000000 + jx16;
						cell->radius = f32((double)nr);
					} else { rng.rnd(); rng.rnd(); }
				}
				else if (sel == 4) ty = elevneg ? 0xf : 6;
				else if (sel == 5) ty = elevneg ? 0xf : 7;
				else if (sel == 6) ty = 0xb;
				else ty = 0xc;
				_subswitch(rng, cnt); rng.rnd();
				if (cell) {
					cell->type = ty;
					if (ty == 0xb || ty == 0xc) {
						cell->radius = f32(128.0);
						cell->x16 = (long long)((((cell->x16 >> 16) & ~0xFF) + 0x80)) << 16;
						cell->z16 = (long long)((((cell->z16 >> 16) & ~0xFF) + 0x80)) << 16;
					}
				}
			}
			k++;
		}
		int processed = (int)srt.size() < 32 ? (int)srt.size() : 32;

		// -- Pass 4: Seen (Typ 10), aus den uebrigen Kandidaten --
		std::vector<Cand> leftlist;
		for (size_t i = processed; i < srt.size(); i++) leftlist.push_back(srt[i]);
		int lleft = (int)srt.size() - processed;
		for (int it = 0; it < 5; it++) {
			if (lleft == 0) break;
			int pick = rng.rnd() % lleft;
			rng.rnd();
			if (pick < (int)leftlist.size()) {
				int ci = leftlist[pick].cellidx;
				if (ci >= 0 && ci < 64) cells[ci].type = 10; // "Lake"-Pass des Generators -> Typ 10 (Portal)
				leftlist.erase(leftlist.begin() + pick);
			}
			lleft--;
		}
	}

	// ================= Oeffentliche API =================

	// Seed-`base` aus dem Spielspeicher lesen (world[0x800188]).
	static inline uint32_t ReadBase(void* gameController) {
		uint32_t base = 0;
		__try { base = *(uint32_t*)((uint8_t*)gameController + 0x2E4 + 0x800188); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return base;
	}

	// Alle Features EINER Region als Block-Koordinaten-Liste (nur nicht-leere Zellen).
	// Ergebnis wird pro Region gecacht (generate_region ist teuer).
	static inline const std::vector<Feature>& RegionFeaturesCached(uint32_t base, int regX, int regZ) {
		static std::map<uint64_t, std::vector<Feature> > cache;
		static const std::vector<Feature> empty;
		static uint32_t ver = 0xFFFFFFFFu;
		if (ModVersion() != ver) { cache.clear(); ver = ModVersion(); }   // Biome geaendert
		if (!_inb(regX, regZ)) return empty;
		uint64_t key = ((uint64_t)base << 22) | ((uint64_t)(regX & 0x7ff) << 11) | (uint64_t)(regZ & 0x7ff);
		std::map<uint64_t, std::vector<Feature> >::iterator it = cache.find(key);
		if (it != cache.end()) return it->second;
		std::vector<Feature> v;
		Cell cells[64]; generate_region(base, regX, regZ, cells);
		for (int i = 0; i < 64; i++) if (cells[i].type != 0) {
			Feature f; f.type = cells[i].type; f.x = (int)(cells[i].x16 >> 16); f.z = (int)(cells[i].z16 >> 16); f.radius = cells[i].radius;
			if (!ModFeatureAllowed(base, f.x, f.z, f.type)) continue;   // im eigenen Biom abgeschaltet
			v.push_back(f);
		}
		cache[key] = v;
		return cache[key];
	}
	static inline void RegionFeatures(uint32_t base, int regX, int regZ, std::vector<Feature>& out) {
		const std::vector<Feature>& v = RegionFeaturesCached(base, regX, regZ);
		for (size_t i = 0; i < v.size(); i++) out.push_back(v[i]);
	}

	// Features rund um (x,z): eigene Region + 8 Nachbarn.
	static inline void FeaturesNear(uint32_t base, int x, int z, std::vector<Feature>& out) {
		int rgX = x / 0x4000, rgZ = z / 0x4000;
		for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) RegionFeatures(base, rgX + a, rgZ + b, out);
	}

	// Liegt (x,z) naeher als 'margin' Bloecke an einem Feature vom Typ 'type' (dessen Radius zaehlt mit)?
	static inline bool NearType(uint32_t base, int x, int z, int type, int margin) {
		std::vector<Feature> fs; FeaturesNear(base, x, z, fs);
		for (size_t i = 0; i < fs.size(); i++) {
			if (fs[i].type != type) continue;
			long long dx = fs[i].x - x, dz = fs[i].z - z;
			long long rr = (long long)(fs[i].radius) + margin;
			if (dx * dx + dz * dz < rr * rr) return true;
		}
		return false;
	}

	// Liegt (x,z) naeher als 'margin' an IRGENDEINER nativen Struktur (Dorf/Dungeon/Bauwerk)?
	// Terrain-Features (Berg 6/7, Plateau 4, See 10) zaehlen NICHT als "Struktur".
	static inline bool NearStructure(uint32_t base, int x, int z, int margin) {
		std::vector<Feature> fs; FeaturesNear(base, x, z, fs);
		for (size_t i = 0; i < fs.size(); i++) {
			// Ground-truth (Nutzer): JEDER Feature-Typ ist eine Struktur/POI -- Dorf, diverse
			// Dungeons, Ruinen, Portal, Palast. Es gibt KEIN Terrain-Feature. Also alle meiden.
			(void)fs[i].type;
			long long dx = fs[i].x - x, dz = fs[i].z - z;
			long long rr = (long long)(fs[i].radius) + margin;
			if (dx * dx + dz * dz < rr * rr) return true;
		}
		return false;
	}

	// Naechstes Feature vom Typ 'type' im Umkreis von 'regRadius' Regionen um (x,z).
	// Rueckgabe true + 'out' gesetzt, wenn eins gefunden. Ergebnisse sind gecacht.
	static inline bool NearestOfType(uint32_t base, int x, int z, int type, int regRadius, Feature& out) {
		int rgX = x / 0x4000, rgZ = z / 0x4000;
		bool found = false; long long best = 0; Feature bf;
		for (int a = -regRadius; a <= regRadius; a++) for (int b = -regRadius; b <= regRadius; b++) {
			const std::vector<Feature>& v = RegionFeaturesCached(base, rgX + a, rgZ + b);
			for (size_t i = 0; i < v.size(); i++) {
				if (v[i].type != type) continue;
				long long dx = v[i].x - x, dz = v[i].z - z;
				long long d2 = dx * dx + dz * dz;
				if (!found || d2 < best) { found = true; best = d2; bf = v[i]; }
			}
		}
		if (found) out = bf;
		return found;
	}

	// (Kein Plateau-Feature: Plateaus sind Terrain, nicht Teil des Feature-Grids.)
}
