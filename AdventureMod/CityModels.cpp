#include "CityModels.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <climits>

// Herleitung der Marker und Masse: CityModels.h. Alles deterministisch aus (Variante, Samen).

namespace CityModels {
	const C3 kLantern{ 255, 255, 0 }, kDoor{ 255, 0, 0 }, kWindow{ 0, 255, 0 };
	const C3 kBed{ 255, 127, 0 }, kTable{ 127, 127, 0 }, kShelf{ 255, 127, 127 };
	static const float kPi = 3.14159265358979f, kTau = 6.28318530717959f;

	uint32_t Hash(int x, int y, int z, uint32_t s) {
		uint32_t h = s ^ (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^ (uint32_t)z * 0xCB1AB31Fu;
		h ^= h >> 13; h *= 0x85EBCA6Bu; h ^= h >> 16; h *= 0xC2B2AE35u; h ^= h >> 15;
		return h;
	}
	static float U(uint32_t h) { return (float)(h & 0xFFFFFF) / 16777215.0f; }
	static bool MarkLevel(uint8_t v) { return v == 0 || v == 127 || v == 255; }
	C3 Safe(C3 c) {
		if (MarkLevel(c.r) && MarkLevel(c.g) && MarkLevel(c.b)) c.g = (uint8_t)(c.g == 255 ? 254 : (c.g == 0 ? 1 : 126));
		if (!c.r && !c.g && !c.b) c.r = 1;
		return c;
	}
	C3 Vary(C3 c, uint32_t h, int amp) {
		int d = (int)(h % (uint32_t)(2 * amp + 1)) - amp;
		auto cl = [](int v) { return (uint8_t)(v < 1 ? 1 : (v > 254 ? 254 : v)); };
		C3 o{ cl(c.r + d), cl(c.g + d), cl(c.b + d) };
		return Safe(o);
	}
	static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
	static float Noise3(float x, float y, float z, uint32_t s) {
		int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
		float fx = Smooth(x - xi), fy = Smooth(y - yi), fz = Smooth(z - zi);
		float v[8];
		for (int i = 0; i < 8; i++) v[i] = U(Hash(xi + (i & 1), yi + ((i >> 1) & 1), zi + ((i >> 2) & 1), s));
		float a = v[0] + (v[1] - v[0]) * fx, b = v[2] + (v[3] - v[2]) * fx;
		float c = v[4] + (v[5] - v[4]) * fx, d = v[6] + (v[7] - v[6]) * fx;
		float e = a + (b - a) * fy, f = c + (d - c) * fy;
		return e + (f - e) * fz;
	}
	static float Wrap(float a) { a = std::fmod(a, kTau); return a < 0 ? a + kTau : a; }
	static float AngDiff(float a, float b) { float d = Wrap(a - b); return d > kPi ? kTau - d : d; }

	struct Cv {
		Lab::Structure& s;
		explicit Cv(Lab::Structure& st) : s(st) {}
		void P(int x, int y, int z, C3 c) { if (s.In(x, y, z)) s.Set(x, y, z, c.r, c.g, c.b); }
		void PIfEmpty(int x, int y, int z, C3 c) { if (s.In(x, y, z) && !s.Solid(x, y, z)) s.Set(x, y, z, c.r, c.g, c.b); }
		void E(int x, int y, int z) { if (s.In(x, y, z)) s.Clear(x, y, z); }
		bool Has(int x, int y, int z) const { return s.Solid(x, y, z); }
	};

	// =====================================================================
	// Elfen-Baumhaus
	// =====================================================================
	// Stamm mit Wurzeln, Wendeltreppe aussen am Stamm bis in das Haus, runde Plattform mit Gelaender,
	// achteckiges Holzhaus um den Stamm (Tuer Sueden, drei Fenster), Kegeldach, Laubkrone mit Blueten.
	// Laternen: am Treppenlauf, auf jedem zweiten Gelaenderpfosten, neben der Tuer, innen, unter der Krone.
	TreehouseDims TreehouseInfo(int v) {
		v = ((v % 4) + 4) % 4;
		TreehouseDims d;
		d.tr = 3 + v % 3;
		d.Rs = d.tr + 2;                         // Treppe: quadratischer Ring (Chebyshev) Rs und Rs+1, Zaun auf Rs+2
		d.Rh = d.tr + 7 + (v % 2);                // Haus ausserhalb der Treppenringe (Innenmoebel bei Rh-2 >= Rs+3)
		d.Rp = d.Rh + 4;
		d.Rc = d.Rh + 5 + (v / 2) * 2;
		d.platZ = 30 + (v % 2) * 8 + (v / 2) * 6;
		d.half = std::max(d.Rp + 2, d.Rc + 1) + 1;
		return d;
	}
	// Treppe als quadratische Spirale um den Stamm: Stufen paarweise (2 Felder je Hoehe, Steigung 1:2),
	// 2 breit, keine Luecken; jede Seite hat 2*Rs Felder und beginnt an ihrer Ecke. Zaeune (native Objekte
	// 0x34, 2 lang) stehen aussen auf Ring Rs+2, je ein Zaun auf einem Stufenpaar; Ecken: Zaun + Pfosten.
	struct StairCell { int x, y, z; };
	static void StairPath(const TreehouseDims& d, std::vector<StairCell>& path) {
		const int R = d.Rs;
		path.clear();
		for (int i = 0; ; i++) {
			int loopPos = i % (8 * R), side = loopPos / (2 * R), k = loopPos % (2 * R);
			int x, y;
			switch (side) {
				case 0: x = -R + k; y = -R; break;
				case 1: x = R; y = -R + k; break;
				case 2: x = R - k; y = R; break;
				default: x = -R; y = R - k; break;
			}
			int z = i / 2;
			if (z > d.platZ) break;
			path.push_back({ x, y, z });
		}
	}
	// Richtung (Laufrichtung) und Aussennormale einer Seite
	static void SideDir(int side, int* ax, int* ay, int* nx, int* ny) {
		static const int A[4][4] = { { 1, 0, 0, -1 }, { 0, 1, 1, 0 }, { -1, 0, 0, 1 }, { 0, -1, -1, 0 } };
		*ax = A[side][0]; *ay = A[side][1]; *nx = A[side][2]; *ny = A[side][3];
	}
	std::vector<FenceP> TreehouseFences(int v) {
		std::vector<FenceP> out;
		const TreehouseDims d = TreehouseInfo(v);
		const int c = d.half, R = d.Rs;
		std::vector<StairCell> path; StairPath(d, path);
		for (size_t i = 0; i + 1 < path.size(); i += 2) {
			const StairCell& a = path[i];
			if (a.z >= d.platZ - 2) break;                                     // oben fuehrt die Treppe ins Haus
			int loopPos = (int)(i % (8 * R)), side = loopPos / (2 * R), k = loopPos % (2 * R);
			int ax, ay, nx, ny; SideDir(side, &ax, &ay, &nx, &ny);
			// Zellen des Paares auf dem Zaunring: Lauf-Koordinate k, k+1 (relativ zur Ecke), Normale Rs+2
			float fx0 = (float)(c + a.x + nx * 2), fy0 = (float)(c + a.y + ny * 2);
			FenceP f;
			f.z = a.z + 1;
			if (ax != 0) { f.orient = 0; f.x = fx0 + (ax > 0 ? 1.0f : 0.0f); f.y = fy0 + 0.5f; }   // Grenze zwischen den beiden Zellen
			else { f.orient = 1; f.x = fx0 + 0.5f; f.y = fy0 + (ay > 0 ? 1.0f : 0.0f); }
			out.push_back(f);
			if (k == 0) {                                                       // Ecke: die zwei Zellen vor dem Paar
				FenceP g = f;
				if (ax != 0) g.x -= 2.0f * ax; else g.y -= 2.0f * ay;
				out.push_back(g);
			}
		}
		return out;
	}
	// Elfen-Baumhaus: Stamm, quadratische Spiraltreppe (Zaeune aussen, City.cpp), runde Plattform mit vier
	// Brueckenausgaengen, achteckiges Holzhaus (Tuer Sueden, drei Fenster, Moebel), Kegeldach, Laubkrone.
	ShopInfo TreehouseShopInfo(int v) {
		const TreehouseDims d = TreehouseInfo(v);
		ShopInfo i; i.vx = d.half; i.vy = d.half + d.Rs + 6; return i;
	}
	Lab::Structure Treehouse(int v, uint32_t seed, bool shop) {
		v = ((v % 4) + 4) % 4;
		const uint32_t sd = seed * 2654435761u + (uint32_t)v * 977u + 17u;
		const TreehouseDims dm = TreehouseInfo(v);
		const int tr = dm.tr, Rs = dm.Rs;
		const int Rh = dm.Rh;                        // Haus (Achteck, halbe Breite)
		const int Rp = dm.Rp;                        // Plattform
		const int Rc = dm.Rc;                        // Krone
		const int platZ = dm.platZ;
		const int wallH = 6, roofH = 8 + (v % 2) * 2;
		const int roofBase = platZ + wallH + 1, roofTop = roofBase + roofH;
		const int Rcz = (int)(Rc * 0.62f);
		const int crownCz = roofTop + Rcz - 1;
		const int H = crownCz + Rcz + 2;
		const int half = dm.half, S = 2 * half + 1, c = half;
		Lab::Structure st; st.Create(S, S, H);
		st.name = "elfen-baumhaus-" + std::to_string(v + 1); st.origin = "mod: stadt";
		Cv cv(st);
		const C3 bark{ 96, 66, 44 }, bark2{ 80, 56, 38 }, wood{ 168, 124, 78 }, wood2{ 148, 108, 68 }, beam{ 112, 80, 52 };
		// Laubfarben je Variante: sattgruen, fruehlingshell, goldener Herbst, silbrig (Elfenbaum)
		static const C3 kLeaf[4][3] = {
			{ { 62, 134, 66 }, { 82, 158, 74 }, { 48, 114, 62 } },
			{ { 96, 168, 84 }, { 132, 190, 98 }, { 70, 140, 70 } },
			{ { 206, 158, 62 }, { 222, 186, 84 }, { 176, 118, 52 } },
			{ { 148, 186, 172 }, { 186, 214, 204 }, { 110, 156, 146 } } };
		const C3 leaf = kLeaf[v][0], leaf2 = kLeaf[v][1], leaf3 = kLeaf[v][2], thatch{ 196, 162, 98 }, silver{ 214, 220, 228 };
		const C3 blossomA{ 238, 212, 226 }, blossomB{ 236, 208, 122 };
		auto Bark = [&](int x, int y, int z) { return Vary((Hash(x, y, z / 2, sd) & 3) ? bark : bark2, Hash(x, y, z, sd + 1), 6); };
		const float th0 = U(Hash(5, 5, 5, sd)) * kTau;
		auto Cheb = [&](int x, int y) { return std::max(std::abs(x - c), std::abs(y - c)); };

		// Stamm mit leichtem Wurzelfuss (nur innerhalb der Treppe)
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
			for (int z = 0; z <= crownCz; z++) {
				float R = tr + 0.45f - (z > roofTop ? 0.8f : 0.0f) + (z <= 2 ? (3 - z) * 0.5f : 0.0f);
				if (d <= R && Cheb(x, y) < Rs) cv.P(x, y, z, Bark(x, y, z));
			}
		}
		// Treppe (Stufen paarweise, 2 breit, Ecken 2x2) + Loch in der Plattform ueber den letzten Stufen
		std::vector<StairCell> path; StairPath(dm, path);
		std::vector<int> topZ((size_t)S * S, INT_MIN);
		for (size_t i = 0; i < path.size(); i++) {
			const StairCell& a = path[i];
			int R = Rs, loopPos = (int)(i % (8 * R)), side = loopPos / (2 * R), k = loopPos % (2 * R);
			int ax, ay, nx, ny; SideDir(side, &ax, &ay, &nx, &ny);
			// Stufe 3 breit: Innenfeld zum Stamm (keine Spalte), Laufring, Aussenring
			std::vector<std::pair<int, int>> cells = { { a.x - nx, a.y - ny }, { a.x, a.y }, { a.x + nx, a.y + ny } };
			if (k == 0) {                                                        // Ecke: auch die Felder der Vorseite
				int px, py, qx, qy; SideDir((side + 3) % 4, &px, &py, &qx, &qy);
				cells.push_back({ a.x + qx, a.y + qy }); cells.push_back({ a.x + qx + nx, a.y + qy + ny });
				cells.push_back({ a.x - nx - qx, a.y - ny - qy });
			}
			for (auto& ce : cells) {
				int X = c + ce.first, Y = c + ce.second;
				cv.P(X, Y, a.z, Vary(((i / 2) % 2) ? wood : wood2, Hash(X, Y, a.z, sd + 2), 4));
				if (a.z > 0) cv.P(X, Y, a.z - 1, beam);                          // Stufenunterseite (keine Durchsicht)
				topZ[(size_t)Y * S + X] = std::max(topZ[(size_t)Y * S + X], a.z);
			}
		}
		// Eckpfosten (Gelaender an den Ecken geschlossen) mit Laterne auf jeder zweiten Ecke
		for (size_t i = 0; i < path.size(); i++) {
			int R = Rs, loopPos = (int)(i % (8 * R)), side = loopPos / (2 * R), k = loopPos % (2 * R);
			if (k != 0 || path[i].z >= platZ - 2) continue;
			int ax, ay, nx, ny; SideDir(side, &ax, &ay, &nx, &ny);
			int px, py, qx, qy; SideDir((side + 3) % 4, &px, &py, &qx, &qy);
			int X = c + path[i].x + 2 * qx, Y = c + path[i].y + 2 * qy, z = path[i].z;   // Zaunring der Vorseite, an der Ecke
			for (int dz = 0; dz <= 2; dz++) cv.P(X, Y, z + dz, beam);
			int X2 = c + path[i].x + 2 * qx + nx, Y2 = c + path[i].y + 2 * qy + ny;          // und das Feld davor
			for (int dz = 0; dz <= 2; dz++) cv.P(X2, Y2, z + dz, beam);
			if ((i / (2 * R)) % 2 == 0) cv.P(X, Y, z + 3, kLantern);
		}
		// Streben unter der Plattform (ausserhalb des Treppenbereichs)
		for (int j = 0; j < 8; j++) {
			float a = th0 + 0.4f + j * kTau / 8.0f;
			for (int r = Rs + 4; r <= Rp - 1; r++) {
				float t = (float)(r - Rs - 4) / (float)std::max(1, Rp - 1 - Rs - 4);
				int z = platZ - 6 + (int)std::lround(t * 5.0f);
				int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
				if (Cheb(px, py) <= Rs + 2) continue;
				cv.P(px, py, z, beam); cv.P(px, py, z - 1, beam);
			}
		}
		// Plattform: Loch ueber den Stufen, die hoeher als platZ-5 liegen (Kopffreiheit), sonst Dielen
		auto InExit = [&](float dx, float dy) {
			float a = std::atan2(dy, dx);
			for (int k = 0; k < 4; k++) if (AngDiff(a, k * kPi * 0.5f) * Rp <= 1.6f) return true;
			return false;
		};
		const C3 rope{ 186, 160, 112 };
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
			if (d > Rp + 0.3f || d < tr + 0.45f) continue;
			int tz = topZ[(size_t)y * S + x];
			if (tz != INT_MIN && tz >= platZ - 5 && tz < platZ) continue;          // Treppenloch
			if (tz == platZ) continue;                                             // oberste Stufe = Boden
			C3 col = d > Rp - 1.0f ? beam : (((int)d % 2) ? wood : wood2);
			cv.P(x, y, platZ, Vary(col, Hash(x, y, platZ, sd + 3), 4));
			if (d >= Rp - 0.6f && !InExit(dx, dy)) cv.P(x, y, platZ + 2, beam);    // Handlauf (Luecken fuer Haengebruecken)
		}
		// Treppenloch umranden (Gelaender aus Bloecken auf Plattformhoehe, Treppenseite offen)
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			int tz = topZ[(size_t)y * S + x];
			if (tz == INT_MIN || tz < platZ - 5 || tz >= platZ) continue;
			for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
				int X = x + ox, Y = y + oy;
				if (X < 0 || Y < 0 || X >= S || Y >= S) continue;
				int t2 = topZ[(size_t)Y * S + X];
				if (t2 != INT_MIN && t2 >= platZ - 5) continue;
				if (Cheb(X, Y) > Rs + 2 || Cheb(X, Y) < Rs - 1) continue;
				if (cv.Has(X, Y, platZ) && !cv.Has(X, Y, platZ + 1)) cv.P(X, Y, platZ + 1, beam);
			}
		}
		for (int k = 0; k < 18; k++) {
			float a = th0 + k * kTau / 18.0f;
			int px = c + (int)std::lround(std::cos(a) * Rp), py = c + (int)std::lround(std::sin(a) * Rp);
			if (InExit((float)(px - c), (float)(py - c))) continue;
			cv.P(px, py, platZ + 1, beam); cv.P(px, py, platZ + 2, beam);
			if (k % 2 == 0) cv.P(px, py, platZ + 3, kLantern);
		}
		for (int k = 0; k < 4; k++) {                                              // Ankerpfosten an den Ausgaengen
			int ex = k == 0 ? 1 : (k == 2 ? -1 : 0), ey = k == 1 ? 1 : (k == 3 ? -1 : 0);
			for (int sgn = -1; sgn <= 1; sgn += 2) {
				int px = c + ex * Rp - ey * 2 * sgn, py = c + ey * Rp + ex * 2 * sgn;
				for (int dz = 1; dz <= 3; dz++) cv.P(px, py, platZ + dz, rope);
				cv.P(px, py, platZ + 4, silver);
			}
		}
		// Haus: Achteck um den Stamm
		auto InOct = [](int dx, int dy, int R) { return std::max(std::abs(dx), std::abs(dy)) <= R && std::abs(dx) + std::abs(dy) <= (3 * R) / 2; };
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			int dx = x - c, dy = y - c;
			if (!InOct(dx, dy, Rh) || InOct(dx, dy, Rh - 1)) continue;
			bool flat = std::abs(dx) == Rh || std::abs(dy) == Rh;
			for (int z = platZ + 1; z <= platZ + wallH; z++) {
				C3 col = (z == platZ + 1 || z == platZ + wallH) ? beam : (flat ? ((z % 2) ? wood : wood2) : wood2);
				cv.P(x, y, z, Vary(col, Hash(x, y, z, sd + 4), 4));
			}
		}
		for (int dz = 1; dz <= 3; dz++) { cv.P(c - 1, c - Rh, platZ + dz, kDoor); cv.P(c, c - Rh, platZ + dz, kDoor); }
		for (int dz = 3; dz <= 4; dz++) {
			cv.P(c + Rh, c - 1, platZ + dz, kWindow); cv.P(c + Rh, c, platZ + dz, kWindow);
			cv.P(c - 1, c + Rh, platZ + dz, kWindow); cv.P(c, c + Rh, platZ + dz, kWindow);
			cv.P(c - Rh, c - 1, platZ + dz, kWindow); cv.P(c - Rh, c, platZ + dz, kWindow);
		}
		cv.P(c + 2, c - Rh - 1, platZ + 5, beam); cv.P(c + 2, c - Rh - 1, platZ + 4, kLantern);    // Laterne neben der Tuer
		cv.P(c - 3, c - Rh - 1, platZ + 5, beam); cv.P(c - 3, c - Rh - 1, platZ + 4, kLantern);
		const int ri = Rh - 2;                                                     // Innenring (ausserhalb der Treppe)
		cv.P(c + ri, c + 1, platZ + wallH, kLantern);
		cv.P(c - ri, c - 2, platZ + wallH, kLantern);
		cv.P(c - ri, c + 2, platZ + 1, kBed);
		cv.P(c + 1, c + ri, platZ + 1, kTable);
		cv.P(c + ri, c - 3, platZ + 1, kShelf);
		// Kegeldach (unterste Lage = Decke mit Ueberstand)
		for (int z = roofBase; z <= roofTop; z++) {
			float t = (float)(z - roofBase) / roofH, Rr = (Rh + 2.5f) * (1.0f - t * 0.9f);
			for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
				float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
				if (d > Rr || (z != roofBase && d < Rr - 1.7f)) continue;
				uint32_t h = Hash(x, y, z, sd + 5);
				cv.PIfEmpty(x, y, z, Vary((h % 3 == 0) ? leaf2 : thatch, h >> 8, 6));
			}
		}
		// Aeste in die Krone
		for (int j = 0; j < 5; j++) {
			float a = th0 + 0.3f + j * kTau / 5.0f;
			for (int s = 0; s <= (int)(Rc * 0.85f); s++) {
				float r = tr + (float)s; int z = roofTop - 1 + (int)(s * 0.55f);
				int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
				cv.P(px, py, z, Bark(px, py, z));
				if (s < Rc * 0.4f) cv.P(px, py, z + 1, Bark(px, py, z + 1));
			}
		}
		// Krone
		for (int z = crownCz - Rcz; z <= crownCz + Rcz; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c) / Rc, dy = (float)(y - c) / Rc, dz = (float)(z - crownCz) / Rcz;
			float q = dx * dx + dy * dy + dz * dz;
			if (q > 1.0f) continue;
			float n = Noise3(x * 0.22f, y * 0.22f, z * 0.22f, sd + 7);
			if (!((q > 0.45f && n > 0.30f) || n > 0.64f)) continue;
			uint32_t h = Hash(x, y, z, sd + 8);
			C3 col = n > 0.62f ? leaf3 : (n > 0.45f ? leaf : leaf2);
			if (h % 41 == 0) col = blossomA; else if (h % 53 == 0) col = blossomB;
			cv.PIfEmpty(x, y, z, Vary(col, h >> 9, 7));
		}
		for (int j = 0; j < 8; j++) {                                              // Laternen unter der Krone
			float a = th0 + 0.35f + j * kTau / 8.0f, r = Rc * 0.6f;
			int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
			for (int z = crownCz - Rcz; z <= crownCz; z++) {
				if (!cv.Has(px, py, z)) continue;
				if (z - 1 > roofTop + 1 && !cv.Has(px, py, z - 1)) cv.P(px, py, z - 1, kLantern);
				break;
			}
		}
		if (shop) {
			// Laden am Stammfuss (2026-10-13): offener Stand auf der +y-Seite unter der Plattform, ausserhalb des
			// Treppenzauns (Rs+2). Boden, Seitenwaende, Rueckwand, Theke mit Waren vorn, Markisendach, Laternen.
			// Die Treppe laeuft auf dieser Seite erst ab z ~2*Rs -- frei. Haendler steht bei (vx, vy) = (c, c+Rs+6).
			const int y0 = c + Rs + 3, y1 = c + Rs + 9, x0 = c - 6, x1 = c + 6, roofZ = 7;
			static const C3 awn[2] = { { 196, 62, 58 }, { 236, 226, 206 } };
			static const C3 goods[6] = { { 206, 60, 50 }, { 236, 196, 70 }, { 120, 170, 70 }, { 160, 110, 70 }, { 90, 120, 200 }, { 230, 140, 60 } };
			for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) cv.P(x, y, 0, Vary(((x + y) & 1) ? wood : wood2, Hash(x, y, 0, sd + 40), 3));
			for (int z = 1; z < roofZ; z++) {
				for (int y = y0; y <= y1 - 1; y++) { cv.P(x0, y, z, (y == y0 || z == 1) ? beam : Vary(wood2, Hash(x0, y, z, sd + 41), 4)); cv.P(x1, y, z, (y == y0 || z == 1) ? beam : Vary(wood2, Hash(x1, y, z, sd + 41), 4)); }
				for (int x = x0; x <= x1; x++) cv.P(x, y0, z, Vary(wood, Hash(x, y0, z, sd + 42), 4));
				cv.P(x0, y1, z, beam); cv.P(x1, y1, z, beam);
			}
			for (int x = x0 + 1; x < x1; x++) {                                     // Theke vorn
				cv.P(x, y1, 1, beam); cv.P(x, y1, 2, Vary(wood, Hash(x, y1, 2, sd + 43), 3));
				if (x != c && Hash(x, y1, 3, sd) % 3u) cv.P(x, y1, 3, Vary(goods[(x + v) % 6], Hash(x, y1, 3, sd + 44), 8));
			}
			for (int y = y0; y <= y1 + 2; y++) for (int x = x0 - 1; x <= x1 + 1; x++)  // Markise, nach vorn geneigt
				cv.P(x, y, roofZ + (y <= y1 ? 1 : 0) - (y > y1 ? 0 : 0), awn[(x / 2) & 1]);
			cv.P(x0 + 2, y1 - 1, roofZ - 1, kLantern); cv.P(x1 - 2, y1 - 1, roofZ - 1, kLantern);
			cv.P(x0 + 1, y0 + 1, 1, kShelf); cv.P(x1 - 1, y0 + 1, 1, kShelf);
			st.name = "elfen-ladenbaum-" + std::to_string(v + 1);
		}
		return st;
	}

	// =====================================================================
	// Mutterbaum
	// =====================================================================
	// Hohler Riesenstamm (Wand 4 Bloecke) mit Brettwurzeln, vier Torbogen, breite Wendeltreppe (vom
	// offenen Schacht bis an die Innenwand, 2 dick, native Zaeune am Schachtrand) bis zur Halle in 150 Hoehe,
	// Fenster am Treppenlauf, Aussenbalkon, Saeulenhalle, acht Hauptaeste, Riesenkrone.
	// ---- Wendeltreppe des Mutterbaums (gemeinsam fuer Modell und Zaeune) ------------------------------
	// Innen offener Schacht (Radius kMotherRin), die Stufen reichen vom Schacht bis an die Innenwand
	// (oben ~6, unten ~10 breit). Der Schachtrand ist ein 4-zusammenhaengender Zellring; entlang dieses
	// Rings steigt die Treppe je ZWEI Zellen um einen Block (Paare gleicher Hoehe -> 2 lange native Zaeune
	// auf dem Rand ohne Luecken). Jede Saeule bekommt die Hoehe der naechsten Ringzelle (gleicher Winkel ->
	// gleiche Stufe), die Stufen sind 2 Bloecke dick: keine Loecher, auch nicht von unten.
	static const int kMotherS = 117, kMotherC = 58, kMotherHt = 150;
	static const float kMotherRin = 7.0f, kMotherStart = 1.75f;           // Fuss bei ~100 Grad (zwischen den Toren)
	struct MotherPathT { std::vector<std::pair<int, int>> cell; std::vector<float> ang; int start = 0; };
	static const MotherPathT& MotherPath() {
		static MotherPathT P;
		if (!P.cell.empty()) return P;
		const int c = kMotherC; const float r = kMotherRin + 0.45f;             // nicht x.5: sonst Zacken an den Achsen
		auto Push = [&](int x, int y) { if (P.cell.empty() || P.cell.back() != std::make_pair(x, y)) P.cell.push_back(std::make_pair(x, y)); };
		for (int k = 0; k <= 20000; k++) {
			float a = k * kTau / 20000.0f;
			int x = c + (int)std::lround(std::cos(a) * r), y = c + (int)std::lround(std::sin(a) * r);
			if (!P.cell.empty()) {
				int px = P.cell.back().first, py = P.cell.back().second;
				if (px != x && py != y) {                                       // diagonal -> Zwischenzelle (die naeher am Kreis)
					float d1 = std::fabs(std::sqrt((float)((x - c) * (x - c) + (py - c) * (py - c))) - r);
					float d2 = std::fabs(std::sqrt((float)((px - c) * (px - c) + (y - c) * (y - c))) - r);
					if (d1 <= d2) Push(x, py); else Push(px, y);
				}
			}
			Push(x, y);
		}
		while (P.cell.size() > 1 && P.cell.back() == P.cell.front()) P.cell.pop_back();
		// Sicherheitsnetz: Zacken A,B,A entfernen (jede Zelle genau einmal im Ring)
		for (bool ch = true; ch; ) {
			ch = false;
			for (size_t j = 0; j + 2 < P.cell.size(); j++) if (P.cell[j] == P.cell[j + 2]) { P.cell.erase(P.cell.begin() + j + 1, P.cell.begin() + j + 3); ch = true; break; }
		}
		for (auto& q : P.cell) P.ang.push_back(std::atan2((float)(q.second - c), (float)(q.first - c)));
		float best = 9.0f;
		for (size_t j = 0; j < P.ang.size(); j++) { float d = AngDiff(P.ang[j], kMotherStart); if (d < best) { best = d; P.start = (int)j; } }
		return P;
	}
	static int MotherNearest(float a) {                                       // Ringzelle mit dem naechsten Winkel
		const MotherPathT& P = MotherPath(); int bj = 0; float bd = 9.0f;
		for (size_t j = 0; j < P.ang.size(); j++) { float d = AngDiff(a, P.ang[j]); if (d < bd) { bd = d; bj = (int)j; } }
		return bj;
	}
	// Thron des Hochelfenkoenigs in der Saeulenhalle (Hoehe kMotherHt): der Treppenankunft gegenueber, Lehne aussen,
	// Blick zur Hallenmitte. Lokal: r = radial (von der Mitte), t = quer.
	static float MotherThroneAngle() {
		const MotherPathT& MP = MotherPath(); const int N = (int)MP.cell.size();
		return MP.ang[(MP.start + 2 * (kMotherHt - 1)) % N] + kTau * 0.5f;
	}
	static const float kThroneR = 10.0f;                                    // Sitzmitte (radial)
	bool MotherThroneSeat(float* x, float* y, int* z, float* yawDeg) {
		const float a = MotherThroneAngle();
		*x = kMotherC + 0.5f + std::cos(a) * kThroneR; *y = kMotherC + 0.5f + std::sin(a) * kThroneR;
		*z = kMotherHt + 4;                                                 // Stand ueber der Sitzflaeche (Sitz oben = Ht+3)
		// Kreatur blickt nach (-sin yaw, cos yaw) (Animation.cpp: creatureMatrix.rotateZ(drawnRotation[2]), Modell vorn = +y)
		const float fx = -std::cos(a), fy = -std::sin(a);                 // zur Hallenmitte
		*yawDeg = std::atan2(-fx, fy) * 57.29578f;
		return true;
	}
	// Standplatz des Koenigs (2026-10-29): zwischen Thron (hinten) und Tisch (vorn) auf dem Podest, Blick zur Hallenmitte.
	// Podest-Block Ht+1 -> Standflaeche = Oberkante = Modell Ht+2; *z = Ht+1, damit City (wz = zg + z, Boden = wz + 1)
	// genau auf der Podest-Oberkante steht (Modellursprung zg + 1).
	static const float kKingR = 6.5f, kTableR0 = 3.8f, kTableR1 = 5.2f, kTableT = 3.0f;
	bool MotherKingStand(float* x, float* y, int* z, float* yawDeg) {
		const float a = MotherThroneAngle();
		*x = kMotherC + 0.5f + std::cos(a) * kKingR; *y = kMotherC + 0.5f + std::sin(a) * kKingR;
		*z = kMotherHt + 1;
		const float fx = -std::cos(a), fy = -std::sin(a);
		*yawDeg = std::atan2(-fx, fy) * 57.29578f;
		return true;
	}
	std::vector<FenceP> MotherTreeFences() {
		std::vector<FenceP> out;
		const MotherPathT& P = MotherPath(); const int N = (int)P.cell.size();
		for (int i = 0; ; i += 2) {
			int z = 1 + i / 2;
			if (z > kMotherHt - 6) break;                                      // oben fuehrt die Treppe in die Halle
			auto a = P.cell[(P.start + i) % N], b = P.cell[(P.start + i + 1) % N];
			FenceP f; f.z = z + 1;
			if (a.second == b.second) { f.orient = 0; f.x = (float)std::max(a.first, b.first); f.y = a.second + 0.5f; }
			else { f.orient = 1; f.x = a.first + 0.5f; f.y = (float)std::max(a.second, b.second); }
			out.push_back(f);
		}
		return out;
	}
	Lab::Structure MotherTree(uint32_t seed) {
		const uint32_t sd = seed * 747796405u + 2891336453u;
		const int S = 117, c = 58, Ht = 150, H = 236;
		Lab::Structure st; st.Create(S, S, H);
		st.name = "elfen-mutterbaum"; st.origin = "mod: stadt";
		Cv cv(st);
		const C3 bark{ 92, 64, 44 }, bark2{ 76, 54, 38 }, bark3{ 108, 78, 54 }, wood{ 170, 126, 80 }, wood2{ 150, 110, 70 };
		const C3 stone{ 176, 178, 168 }, stone2{ 156, 160, 150 }, silver{ 214, 220, 228 }, gold{ 226, 192, 96 };
		const C3 leaf{ 60, 132, 66 }, leaf2{ 80, 156, 74 }, leaf3{ 46, 112, 62 }, blossom{ 236, 214, 140 }, glow{ 230, 238, 206 };
		auto Bark = [&](int x, int y, int z) { uint32_t h = Hash(x, y, z / 3, sd); return Vary((h & 3) == 0 ? bark2 : ((h & 7) == 1 ? bark3 : bark), Hash(x, y, z, sd + 1), 6); };
		auto Ro = [&](int z) { return 21.0f - 4.0f * std::min(1.0f, (float)z / Ht) + (z < 24 ? (24 - z) * 0.28f : 0.0f); };
		auto Ri = [&](int z) { return Ro(z) - 4.2f; };

		// Treppe vorab: Winkel der Stufe auf Hoehe z (MotherPath)
		const MotherPathT& MP = MotherPath(); const int MN = (int)MP.cell.size();
		std::vector<float> th(Ht + 2, 0.0f);
		for (int z = 0; z <= Ht + 1; z++) th[z] = MP.ang[(MP.start + 2 * std::max(0, z - 1)) % MN];
		// Fenster am Treppenlauf
		struct Win { float a; int z0, z1; };
		std::vector<Win> wins;
		for (int z = 14; z < Ht - 6; z += 21) wins.push_back({ th[z] + 0.12f, z + 2, z + 4 });
		auto InWindow = [&](float a, float d, int z) {
			for (const Win& w : wins) if (z >= w.z0 && z <= w.z1 && AngDiff(a, w.a) * d <= 1.3f) return true;
			return false;
		};
		auto InArch = [&](float dx, float dy, int z) {
			for (int k = 0; k < 4; k++) {
				float ux = k == 0 ? 1.0f : (k == 2 ? -1.0f : 0.0f), uy = k == 1 ? 1.0f : (k == 3 ? -1.0f : 0.0f);
				float along = dx * ux + dy * uy, perp = std::fabs(dx * uy - dy * ux);
				if (along <= 0.0f || perp > 3.5f) continue;
				float top = 9.0f + 2.5f * std::sqrt(std::max(0.0f, 1.0f - (perp / 3.6f) * (perp / 3.6f)));
				if (z >= 1 && z <= top) return true;
			}
			return false;
		};
		// Wurzeln, Stammwand, Boden
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy), a = std::atan2(dy, dx);
			const float R0 = Ro(0);
			if (d > R0 - 1.0f) {
				for (int k = 0; k < 9; k++) {
					float la = 0.35f + k * kTau / 9.0f;
					float da = AngDiff(a, la);
					if (da > 1.2f) continue;
					float along = d * std::cos(da), perp = d * std::sin(da);
					float u = (along - R0) / 26.0f;
					if (u < -0.1f || u > 1.0f) continue;
					u = std::max(0.0f, u);
					float w = 5.0f * (1.0f - u) + 1.5f, hr = 16.0f * std::pow(1.0f - u, 1.6f) + 1.0f;
					if (perp > w) continue;
					for (int z = 0; z <= (int)hr; z++) if (!InArch(dx, dy, z)) cv.P(x, y, z, Bark(x, y, z));
				}
			}
			for (int z = 0; z <= Ht; z++) {
				float ro = Ro(z), ri = Ri(z);
				if (z == 0 && d < ri) { cv.P(x, y, 0, Vary(((int)(d / 3) % 2) ? stone : stone2, Hash(x, y, 0, sd + 2), 4)); continue; }
				if (d < ri || d > ro) continue;
				if (InArch(dx, dy, z) || InWindow(a, d, z)) continue;
				cv.P(x, y, z, Bark(x, y, z));
			}
		}
		// Laternen an den Toren und im Rund am Boden
		for (int k = 0; k < 4; k++) {
			float ux = k == 0 ? 1.0f : (k == 2 ? -1.0f : 0.0f), uy = k == 1 ? 1.0f : (k == 3 ? -1.0f : 0.0f);
			for (int sgn = -1; sgn <= 1; sgn += 2) {
				float r = Ro(10) + 1.0f;
				int px = c + (int)std::lround(ux * r - uy * 5.0f * sgn), py = c + (int)std::lround(uy * r + ux * 5.0f * sgn);
				cv.P(px, py, 11, bark3); cv.P(px, py, 10, kLantern);
			}
		}
		for (int k = 0; k < 8; k++) {                                            // Laternenrund im Schacht (innerhalb der Treppe)
			float a = k * kTau / 8.0f;
			int px = c + (int)std::lround(std::cos(a) * 4.0f), py = c + (int)std::lround(std::sin(a) * 4.0f);
			cv.P(px, py, 1, silver); cv.P(px, py, 2, kLantern);
		}
		// Wendeltreppe: vom Schachtrand bis an die Innenwand, 2 Bloecke dick, Hoehe je Ringzelle
		std::vector<std::vector<int>> stairAt((size_t)S * S);
		std::vector<int> ringIdx((size_t)S * S, -1);                               // Ringzellen: Hoehe aus ihrem eigenen Index
		for (int j = 0; j < MN; j++) ringIdx[(size_t)MP.cell[j].second * S + MP.cell[j].first] = j;
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
			int rj = ringIdx[(size_t)y * S + x];
			if ((rj < 0 && d < kMotherRin + 0.5f) || d > Ri(0)) continue;          // innen offen (Schacht)
			int j = rj >= 0 ? rj : MotherNearest(std::atan2(dy, dx));
			int i0 = ((j - MP.start) % MN + MN) % MN;
			for (int i = i0; ; i += MN) {
				int z = 1 + i / 2;
				if (z > Ht) break;
				if (d >= Ri(z) - 0.2f) continue;                                    // dort ist schon die Wand
				for (int t = 0; t < 2; t++) {
					int zz = z - t;
					if (zz < 1) continue;
					C3 col = t == 0 ? Vary(wood2, Hash(x, y, zz, sd + 3), 5) : Vary(wood, Hash(x, y, zz, sd + 8), 4);
					if (d < kMotherRin + 1.6f && t == 0) col = Vary(bark3, Hash(x, y, zz, sd + 11), 4);   // Kante am Schacht
					cv.P(x, y, zz, col);
				}
				stairAt[(size_t)y * S + x].push_back(z);
			}
		}
		for (int z = 14; z < Ht - 4; z += 14) {                                     // Wandlaternen ueber der Treppe
			float a = th[z]; float r = Ri(z) + 0.4f;
			cv.P(c + (int)std::lround(std::cos(a) * r), c + (int)std::lround(std::sin(a) * r), z + 3, kLantern);
		}
		// Hallenboden mit Treppenloch, Aussenbalkon
		const float RoT = Ro(Ht);
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
			if (d > RoT + 6.5f) continue;
			if (d <= RoT + 0.5f) {
				bool hole = false;
				for (int z : stairAt[(size_t)y * S + x]) if (z >= Ht - 5 && z <= Ht - 1) hole = true;
				if (!hole && !cv.Has(x, y, Ht)) cv.P(x, y, Ht, Vary(((int)(d / 2) % 2) ? wood : wood2, Hash(x, y, Ht, sd + 5), 4));
			} else {
				cv.P(x, y, Ht, Vary(wood2, Hash(x, y, Ht, sd + 6), 4));
				if (d >= RoT + 5.5f) cv.P(x, y, Ht + 2, wood);                      // Handlauf
			}
		}
		for (int k = 0; k < 24; k++) {
			float a = k * kTau / 24.0f, r = RoT + 6.0f;
			int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
			cv.P(px, py, Ht + 1, wood); cv.P(px, py, Ht + 2, wood);
			if (k % 2 == 0) cv.P(px, py, Ht + 3, kLantern);
			for (int t = 0; t <= 7; t++) {                                           // Streben unter dem Balkon
				float rr = RoT - 1.0f + t * 0.95f; int z = Ht - 8 + t;
				if (k % 2) cv.P(c + (int)std::lround(std::cos(a) * rr), c + (int)std::lround(std::sin(a) * rr), z, bark3);
			}
		}
		// Saeulenhalle + Dach
		for (int k = 0; k < 18; k++) {
			float a = k * kTau / 18.0f, r = RoT - 1.5f;
			int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
			for (int z = Ht + 1; z <= Ht + 10; z++) for (int ox = 0; ox <= 1; ox++) for (int oy = 0; oy <= 1; oy++) cv.P(px + ox, py + oy, z, Bark(px, py, z));
		}
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), d = std::sqrt(dx * dx + dy * dy);
			if (d <= RoT + 3.0f) cv.P(x, y, Ht + 11, d > RoT + 1.5f ? gold : Vary(wood2, Hash(x, y, Ht + 11, sd + 7), 4));
		}
		for (int k = 0; k < 12; k++) {
			float a = k * kTau / 12.0f, r = RoT - 6.0f;
			cv.P(c + (int)std::lround(std::cos(a) * r), c + (int)std::lround(std::sin(a) * r), Ht + 10, kLantern);
		}
		cv.P(c, c, Ht + 10, kLantern); cv.P(c + 3, c, Ht + 9, kLantern); cv.P(c - 3, c, Ht + 9, kLantern);
		// Thron des Hochelfenkoenigs: Podest (Gold-Rand, roter Grund), Sitz, Armlehnen, hohe Lehne mit Krone,
		// Laeufer bis zur Hallenmitte
		{
			const float a = MotherThroneAngle(), ux = std::cos(a), uy = std::sin(a);
			const C3 red{ 150, 34, 40 }, red2{ 128, 26, 34 }, white{ 236, 234, 226 };
			for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
				const float dx = x + 0.5f - (c + 0.5f), dy = y + 0.5f - (c + 0.5f);
				const float r = dx * ux + dy * uy, t = -dx * uy + dy * ux, at = std::fabs(t);
				if (r >= kTableR0 - 1.3f && r <= kThroneR + 3.5f && at <= 5.0f) {               // Podest (reicht bis vor den Tisch)
					bool edge = r < kTableR0 - 0.3f || at > 4.0f;
					cv.P(x, y, Ht + 1, edge ? gold : Vary(red, Hash(x, y, Ht + 1, sd + 31), 4));
				}
				if (r >= -6.0f && r < kTableR0 - 1.3f && at <= 1.6f) cv.P(x, y, Ht + 1, at > 1.0f ? gold : red2);   // Laeufer
				if (r >= kTableR0 && r <= kTableR1 && at <= kTableT) {                         // Tisch vor dem Koenig
					const bool rim = r < kTableR0 + 0.5f || r > kTableR1 - 0.5f || at > kTableT - 0.6f;
					const bool leg = (r < kTableR0 + 0.7f || r > kTableR1 - 0.7f) && at > kTableT - 0.9f;
					if (leg) cv.P(x, y, Ht + 2, wood);                                        // Beine an den Ecken
					cv.P(x, y, Ht + 3, rim ? gold : Vary(white, Hash(x, y, Ht + 3, sd + 33), 3));   // Platte mit Decke
				}
				if (r >= kThroneR - 1.5f && r <= kThroneR + 1.5f && at <= 1.6f) {                // Sitz
					cv.P(x, y, Ht + 2, wood); cv.P(x, y, Ht + 3, Vary(red, Hash(x, y, Ht + 3, sd + 32), 3));
				}
				if (r >= kThroneR - 1.5f && r <= kThroneR + 1.5f && at > 1.6f && at <= 2.7f)      // Armlehnen
					for (int z = Ht + 2; z <= Ht + 5; z++) cv.P(x, y, z, z == Ht + 5 ? gold : silver);
				if (r > kThroneR + 1.5f && r <= kThroneR + 2.8f && at <= 2.7f) {                  // Lehne
					const int top = Ht + 9 - (int)(at > 1.6f ? 1 : 0);
					for (int z = Ht + 2; z <= top; z++) cv.P(x, y, z, (z == top || at > 2.0f) ? gold : white);
					if (at <= 2.1f && ((int)std::floor(t + 10.0f) % 2 == 0)) cv.P(x, y, top + 1, gold);   // Kronenzacken
				}
			}
			const int lx = c + (int)std::lround(ux * (kThroneR + 2.0f)), ly = c + (int)std::lround(uy * (kThroneR + 2.0f));
			cv.P(lx, ly, Ht + 10, kLantern);                                            // Licht ueber dem Thron
			for (int sgn = -1; sgn <= 1; sgn += 2) {                                    // Kerzen auf den Tischenden
				const float rr = (kTableR0 + kTableR1) * 0.5f, tt = sgn * (kTableT - 0.8f);
				cv.P(c + (int)std::floor(0.5f + ux * rr - uy * tt), c + (int)std::floor(0.5f + uy * rr + ux * tt), Ht + 4, kLantern);
			}
			{                                                                           // Krone auf einem Kissen in der Tischmitte
				const float rr = (kTableR0 + kTableR1) * 0.5f;
				const int kx = c + (int)std::floor(0.5f + ux * rr), ky = c + (int)std::floor(0.5f + uy * rr);
				cv.P(kx, ky, Ht + 4, red);
			}
		}
		// Hauptaeste
		struct P3 { float x, y, z; };
		std::vector<P3> ends;
		for (int j = 0; j < 8; j++) {
			float a = 0.39f + j * kTau / 8.0f + (U(Hash(j, 1, 2, sd)) - 0.5f) * 0.3f;
			const float L = 40.0f;
			for (float s = 0.0f; s <= L; s += 1.0f) {
				float px = c + std::cos(a) * (12.0f + s * 0.8f), py = c + std::sin(a) * (12.0f + s * 0.8f), pz = Ht + 12.0f + s * 0.6f;
				float tk = 4.5f - 3.0f * s / L;
				int r = (int)std::ceil(tk);
				for (int oz = -r; oz <= r; oz++) for (int oy = -r; oy <= r; oy++) for (int ox = -r; ox <= r; ox++) {
					if (ox * ox + oy * oy + oz * oz > tk * tk) continue;
					int X = (int)std::lround(px) + ox, Y = (int)std::lround(py) + oy, Z = (int)std::lround(pz) + oz;
					cv.P(X, Y, Z, Bark(X, Y, Z));
				}
				if (s == L) ends.push_back({ px, py, pz });
			}
		}
		// Krone: grosses Ellipsoid + Buesche an den Astenden
		const float cz = Ht + 52.0f;
		for (int z = Ht + 14; z < H; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = (float)(x - c), dy = (float)(y - c), dz = (float)z - cz;
			float q = (dx * dx + dy * dy) / (54.0f * 54.0f) + dz * dz / (30.0f * 30.0f);
			float qs = 9.0f;
			for (const P3& e : ends) { float ex = x - e.x, ey = y - e.y, ez = z - (e.z + 4.0f); qs = std::min(qs, (ex * ex + ey * ey + ez * ez) / (15.0f * 15.0f)); }
			float qq = std::min(q, qs);
			if (qq > 1.0f) continue;
			if (z < Ht + 20 && dx * dx + dy * dy < 26.0f * 26.0f) continue;          // Luft ueber dem Hallendach
			float n = Noise3(x * 0.12f, y * 0.12f, z * 0.12f, sd + 9);
			if (!((qq > 0.6f && n > 0.35f) || n > 0.7f)) continue;
			uint32_t h = Hash(x, y, z, sd + 10);
			C3 col = n > 0.6f ? leaf3 : (n > 0.45f ? leaf : leaf2);
			if (h % 33 == 0) col = blossom; else if (h % 97 == 0) col = glow;
			cv.PIfEmpty(x, y, z, Vary(col, h >> 8, 7));
		}
		// Laternen unter der Krone
		for (int j = 0; j < 40; j++) {
			uint32_t h = Hash(j, 77, 3, sd);
			float a = U(h) * kTau, r = 12.0f + U(h >> 7) * 40.0f;
			int px = c + (int)std::lround(std::cos(a) * r), py = c + (int)std::lround(std::sin(a) * r);
			for (int z = Ht + 14; z < H; z++) {
				if (!cv.Has(px, py, z)) continue;
				if (z - 1 > Ht + 12 && !cv.Has(px, py, z - 1)) cv.P(px, py, z - 1, kLantern);
				break;
			}
		}
		return st;
	}

	// =====================================================================
	// Zwergenhaus
	// =====================================================================
	// Steinhaus mit Quadern, dunklem Obergeschoss mit Balken, steilem Schieferdach (First entlang x),
	// Schornstein, Treppe innen. Vorderseite (Tuer, zwei Laternen) bei y = 0 (Vorplatz y = 0..1).
	Lab::Structure DwarfHouse(int v, uint32_t seed) {
		v = ((v % 4) + 4) % 4;
		const uint32_t sd = seed * 2246822519u + (uint32_t)v * 3266489917u + 5u;
		static const int Ws[4] = { 18, 20, 24, 16 }, Ds[4] = { 14, 16, 16, 16 }, Fl[4] = { 2, 2, 2, 3 };
		const int W = Ws[v], D = Ds[v], floors = Fl[v];
		const int y0 = 2, y1 = D + 1;                    // Hauskoerper y0..y1
		const int fh = 6;                                // Geschosshoehe (Boden + 5)
		const int top = fh * floors;                     // oberste Decke
		const int roofH = D / 2 + 2;
		const int H = top + roofH + 5;
		Lab::Structure st; st.Create(W, D + 2, H);
		st.name = "zwergenhaus-" + std::to_string(v + 1); st.origin = "mod: stadt";
		Cv cv(st);
		const C3 granite{ 122, 116, 110 }, granite2{ 104, 100, 96 }, quoin{ 158, 152, 142 }, dark{ 88, 84, 82 }, mortar{ 140, 134, 126 };
		const C3 beam{ 104, 72, 46 }, plank{ 138, 98, 62 }, slate{ 70, 78, 92 }, slate2{ 58, 66, 80 }, ridge{ 50, 54, 64 };
		const C3 iron{ 66, 64, 70 }, pave{ 140, 134, 126 }, pave2{ 124, 118, 110 }, gold{ 214, 172, 64 }, chim{ 96, 86, 82 };
		auto Wall = [&](int x, int y, int z) -> C3 {
			bool corner = (x == 0 || x == W - 1) && (y == y0 || y == y1);
			if (corner) return Vary(((z / 2) % 2) ? quoin : granite, Hash(x, y, z, sd), 4);
			if (z <= fh) return Vary(((x + y + z * 3) % 7 == 0) ? mortar : (((z / 2 + (x + y) / 3) % 2) ? granite : granite2), Hash(x, y, z, sd + 1), 5);
			int zz = (z - 1) % fh;
			if (zz == 0 || zz == fh - 1 || (x % 4 == 0 && (y == y0 || y == y1)) || (y % 4 == 0 && (x == 0 || x == W - 1))) return beam;
			return Vary(dark, Hash(x, y, z, sd + 2), 4);
		};
		// Boden, Vorplatz
		for (int y = 0; y <= y1; y++) for (int x = 0; x < W; x++) {
			if (y < y0) { if (x >= W / 2 - 3 && x <= W / 2 + 2) cv.P(x, y, 0, Vary((x + y) % 2 ? pave : pave2, Hash(x, y, 0, sd + 3), 3)); continue; }
			cv.P(x, y, 0, Vary((x + y) % 2 ? pave : pave2, Hash(x, y, 0, sd + 4), 3));
		}
		// Waende + Decken
		for (int z = 1; z <= top; z++) for (int y = y0; y <= y1; y++) for (int x = 0; x < W; x++) {
			bool edge = x == 0 || x == W - 1 || y == y0 || y == y1;
			if (edge) cv.P(x, y, z, Wall(x, y, z));
			else if (z % fh == 0) cv.P(x, y, z, Vary(plank, Hash(x, y, z, sd + 5), 4));
		}
		// Treppen innen (Geschoss f -> f+1), Loecher in der Decke
		for (int f = 0; f + 1 < floors; f++) {
			bool back = (f % 2) == 0;
			int yS = back ? y1 - 2 : y0 + 1;                 // 2 breit: yS, yS+1
			for (int s = 0; s < fh; s++) {
				int x = back ? W - 3 - s : 2 + s, z = f * fh + 1 + s;
				for (int yy = yS; yy <= yS + 1; yy++) { cv.P(x, yy, z, Vary(plank, Hash(x, yy, z, sd + 6), 4)); for (int zz = z + 1; zz <= f * fh + fh; zz++) if (zz == (f + 1) * fh && s >= 1) cv.E(x, yy, zz); }
			}
		}
		// Tuer, Rahmen, Laternen
		const int dx0 = W / 2 - 1;
		for (int z = 1; z <= 3; z++) { cv.P(dx0, y0, z, kDoor); cv.P(dx0 + 1, y0, z, kDoor); cv.P(dx0 - 1, y0, z, quoin); cv.P(dx0 + 2, y0, z, quoin); }
		for (int x = dx0 - 1; x <= dx0 + 2; x++) cv.P(x, y0, 4, quoin);
		cv.P(dx0 + 3, 1, 5, iron); cv.P(dx0 + 3, 1, 4, kLantern);
		cv.P(dx0 - 2, 1, 5, iron); cv.P(dx0 - 2, 1, 4, kLantern);
		// Fenster je Geschoss
		for (int f = 0; f < floors; f++) {
			int zw = f * fh + 2;
			std::vector<int> xs;
			if (f == 0) { xs.push_back(3); xs.push_back(W - 5); } else { xs.push_back(3); xs.push_back(W / 2 - 1); xs.push_back(W - 5); }
			for (int x : xs) for (int dz = 0; dz <= 1; dz++) for (int ox = 0; ox <= 1; ox++) {
				cv.P(x + ox, y0, zw + dz, kWindow); cv.P(x + ox, y1, zw + dz, kWindow);
			}
			for (int dz = 0; dz <= 1; dz++) for (int oy = 0; oy <= 1; oy++) {
				cv.P(0, y0 + (D / 2) - 1 + oy, zw + dz, kWindow); cv.P(W - 1, y0 + (D / 2) - 1 + oy, zw + dz, kWindow);
			}
			cv.P(W / 2 + (f % 2 ? -3 : 2), y0 + D / 2, f * fh + fh - 1, kLantern);              // Deckenlaterne
		}
		cv.P(W / 2 + 3, y0 + D / 2 + 1, 1, kTable);
		cv.P(2, y1 - 1, 1, kShelf);
		cv.P(3, y1 - 2, fh + 1, kBed);
		// Satteldach mit First entlang x, Giebel aus Stein
		for (int k = 0; k <= roofH; k++) {
			int z = top + 1 + k;
			int ya = y0 - 1 + k, yb = y1 + 1 - k;
			if (ya > yb) break;
			for (int x = -1; x <= W; x++) {
				int X = std::min(W - 1, std::max(0, x));
				for (int y : { ya, yb }) cv.P(X, y, z, Vary(ya >= yb - 1 ? ridge : ((k % 2) ? slate : slate2), Hash(X, y, z, sd + 7), 4));
				if (ya + 1 <= yb - 1 && (X == 0 || X == W - 1)) for (int y = ya + 1; y <= yb - 1; y++) cv.P(X, y, z, Wall(X, std::min(std::max(y, y0), y1), z));
			}
		}
		// Schornstein mit Goldkappe
		int cx = W - 4, cy = y1 - 3;
		for (int z = 1; z <= top + roofH + 2; z++) for (int ox = 0; ox <= 1; ox++) for (int oy = 0; oy <= 1; oy++) {
			bool inside = z > 0 && z <= top && !(cx + ox == 0 || cx + ox == W - 1 || cy + oy == y0 || cy + oy == y1);
			if (inside && z % fh != 0) continue;                                // innen nur durch die Decken
			cv.P(cx + ox, cy + oy, z, Vary(chim, Hash(cx + ox, cy + oy, z, sd + 8), 4));
		}
		for (int ox = -1; ox <= 2; ox++) for (int oy = -1; oy <= 2; oy++) cv.P(cx + ox, cy + oy, top + roofH + 3, dark);
		for (int ox = 0; ox <= 1; ox++) for (int oy = 0; oy <= 1; oy++) cv.P(cx + ox, cy + oy, top + roofH + 4, gold);
		return st;
	}

	// =====================================================================
	// Zwergenfestung
	// =====================================================================
	// Gepflasterter Hof, Ringmauer mit Wehrgang und Zinnen, vier Ecktuerme, Torhaus mit zwei Tuermen und
	// Bannern (Tor bei y = 0), Bergfried 50 x 50 x 70 mit vier Geschossen, Treppenlaeufen, Tischreihen,
	// Fenstern, vier Ecktuermchen mit Spitzdaechern. Laternen auf dem Hof, an Mauern, Tuermen, innen.
	Lab::Structure Fortress(uint32_t seed) {
		const uint32_t sd = seed * 1597334677u + 3812015801u;
		const int S = 140, H = 96;
		Lab::Structure st; st.Create(S, S, H);
		st.name = "zwergenfestung"; st.origin = "mod: stadt";
		Cv cv(st);
		const C3 granite{ 122, 116, 110 }, granite2{ 104, 100, 96 }, quoin{ 158, 152, 142 }, dark{ 84, 80, 80 };
		const C3 pave{ 142, 136, 128 }, pave2{ 120, 114, 108 }, pave3{ 164, 158, 148 }, slate{ 70, 78, 92 }, slate2{ 58, 66, 80 };
		const C3 iron{ 66, 64, 70 }, gold{ 214, 172, 64 }, banner{ 176, 34, 38 }, plank{ 138, 98, 62 }, beam{ 104, 72, 46 };
		auto Stone = [&](int x, int y, int z) { return Vary(((z / 2 + (x + y) / 3) % 2) ? granite : granite2, Hash(x, y, z, sd), 5); };
		auto WallDist = [&](int x, int y) { return std::min(std::min(x, y), std::min(S - 1 - x, S - 1 - y)); };
		// Pflaster
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			int ring = std::max(std::abs(x - 70), std::abs(y - 80)) / 6;
			cv.P(x, y, 0, Vary(ring % 2 ? pave : ((x + y) % 2 ? pave2 : pave3), Hash(x, y, 0, sd + 1), 3));
		}
		// Ringmauer + Zinnen + Torbogen
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			int wd = WallDist(x, y);
			if (wd < 6 || wd > 11) continue;
			bool gate = y >= 6 && y <= 11 && x >= 62 && x <= 77;
			for (int z = 1; z <= 28; z++) {
				if (gate) {
					float px = std::fabs(x - 69.5f);
					float topz = 12.0f + 4.0f * std::sqrt(std::max(0.0f, 1.0f - (px / 8.0f) * (px / 8.0f)));
					if (z <= topz) continue;
				}
				cv.P(x, y, z, z == 28 ? Vary(pave3, Hash(x, y, z, sd + 2), 3) : Stone(x, y, z));
			}
			if (wd == 6 && ((x + y) / 2) % 2 == 0) { cv.P(x, y, 29, quoin); cv.P(x, y, 30, quoin); }
			if (wd == 11 && ((x + y) / 3) % 4 == 0) cv.P(x, y, 29, granite2);
		}
		// Rund-Tuerme: 4 Ecken (r 12, h 44), 2 Torhaustuerme (r 8, h 38)
		struct T { int x, y; float r; int h; };
		const T towers[6] = { { 12, 12, 12.0f, 44 }, { 127, 12, 12.0f, 44 }, { 12, 127, 12.0f, 44 }, { 127, 127, 12.0f, 44 }, { 55, 9, 8.0f, 38 }, { 84, 9, 8.0f, 38 } };
		for (const T& t : towers) {
			int r = (int)std::ceil(t.r);
			for (int y = t.y - r; y <= t.y + r; y++) for (int x = t.x - r; x <= t.x + r; x++) {
				float d = std::sqrt((float)((x - t.x) * (x - t.x) + (y - t.y) * (y - t.y)));
				if (d > t.r) continue;
				for (int z = 1; z <= t.h; z++) cv.P(x, y, z, z == t.h ? Vary(pave3, Hash(x, y, z, sd + 3), 3) : Stone(x, y, z));
				if (d >= t.r - 1.2f && (((int)(std::atan2((float)(y - t.y), (float)(x - t.x)) * t.r)) % 2 == 0)) { cv.P(x, y, t.h + 1, quoin); cv.P(x, y, t.h + 2, quoin); }
			}
			for (int k = 0; k < 4; k++) {                                            // Laternen aussen am Turm
				float a = k * kTau / 4.0f + kPi / 4.0f, rr = t.r + 0.6f;
				int px = t.x + (int)std::lround(std::cos(a) * rr), py = t.y + (int)std::lround(std::sin(a) * rr);
				cv.P(px, py, t.h - 7, iron); cv.P(px, py, t.h - 8, kLantern);
			}
		}
		// Banner an den Torhaustuermen, Laternen am Tor
		for (int bx : { 54, 85 }) for (int z = 18; z <= 32; z++) for (int ox = -2; ox <= 2; ox++) {
			bool trim = std::abs(ox) == 2 || z == 18;
			cv.P(bx + ox, 0, z, trim ? gold : banner);
		}
		for (int lx : { 64, 75 }) { cv.P(lx, 5, 15, iron); cv.P(lx, 5, 14, kLantern); }
		// Bergfried
		const int kx0 = 45, kx1 = 94, ky0 = 56, ky1 = 105, Hk = 70;
		for (int y = ky0; y <= ky1; y++) for (int x = kx0; x <= kx1; x++) {
			int dw = std::min(std::min(x - kx0, kx1 - x), std::min(y - ky0, ky1 - y));
			for (int z = 1; z <= Hk; z++) {
				if (dw <= 2) cv.P(x, y, z, ((x == kx0 || x == kx1) && (y == ky0 || y == ky1)) ? quoin : Stone(x, y, z));
				else if (z % 20 == 0 || z == Hk) cv.P(x, y, z, Vary(z == Hk ? pave3 : plank, Hash(x, y, z, sd + 4), 4));
			}
			if (dw == 0 && ((x + y) / 2) % 2 == 0) { cv.P(x, y, Hk + 1, quoin); cv.P(x, y, Hk + 2, quoin); }
		}
		// Treppenlaeufe im Bergfried (Rueckwand / Vorderwand im Wechsel), Loecher in Boden und Dach
		for (int k = 0; k < 4; k++) {
			int zb = 20 * k, n = (k < 3) ? 20 : 10;
			bool back = (k % 2) == 0;
			int yA = back ? ky1 - 5 : ky0 + 3;
			for (int s = 0; s < n; s++) {
				int x = back ? kx0 + 5 + s : kx1 - 5 - s, z = zb + 1 + s;
				for (int y = yA; y <= yA + 2; y++) {
					cv.P(x, y, z, Vary(plank, Hash(x, y, z, sd + 5), 4));
					if (s >= n - 6) cv.E(x, y, zb + 20 > Hk ? Hk : zb + 20);
				}
			}
		}
		// Tuer + Durchgang, Fenster, Inneres
		for (int z = 1; z <= 3; z++) { cv.P(69, ky0, z, kDoor); cv.P(70, ky0, z, kDoor); for (int y = ky0 + 1; y <= ky0 + 2; y++) { cv.E(69, y, z); cv.E(70, y, z); } }
		for (int x = 67; x <= 72; x++) cv.P(x, ky0, 4, gold);
		cv.P(66, ky0 - 1, 5, iron); cv.P(66, ky0 - 1, 4, kLantern); cv.P(73, ky0 - 1, 5, iron); cv.P(73, ky0 - 1, 4, kLantern);
		for (int f = 0; f < 3; f++) {
			int zw = f * 20 + 8;
			for (int wx : { 52, 86 }) for (int dz = 0; dz <= 1; dz++) for (int ox = 0; ox <= 1; ox++) {
				cv.P(wx + ox, ky0, zw + dz, kWindow); cv.P(wx + ox, ky1, zw + dz, kWindow);
				cv.E(wx + ox, ky0 + 1, zw + dz); cv.E(wx + ox, ky0 + 2, zw + dz); cv.E(wx + ox, ky1 - 1, zw + dz); cv.E(wx + ox, ky1 - 2, zw + dz);
			}
			for (int wy : { 70, 90 }) for (int dz = 0; dz <= 1; dz++) for (int oy = 0; oy <= 1; oy++) {
				cv.P(kx0, wy + oy, zw + dz, kWindow); cv.P(kx1, wy + oy, zw + dz, kWindow);
				cv.E(kx0 + 1, wy + oy, zw + dz); cv.E(kx0 + 2, wy + oy, zw + dz); cv.E(kx1 - 1, wy + oy, zw + dz); cv.E(kx1 - 2, wy + oy, zw + dz);
			}
			for (int ly = 64; ly <= 98; ly += 17) for (int lx = 55; lx <= 85; lx += 15) cv.P(lx, ly, f * 20 + 19, kLantern);
		}
		for (int x = 58; x <= 82; x += 6) { cv.P(x, 76, 1, kTable); cv.P(x, 86, 1, kTable); }   // Tafel in der Halle
		for (int y = 64; y <= 96; y += 8) { cv.P(kx0 + 3, y, 1, kShelf); cv.P(kx1 - 3, y, 1, kShelf); }
		for (int x = 56; x <= 84; x += 7) { cv.P(x, 80, 21, kBed); cv.P(x, 80, 41, kTable); }
		// Ecktuermchen auf dem Bergfried mit Spitzdach
		const int tc[4][2] = { { kx0 + 3, ky0 + 3 }, { kx1 - 3, ky0 + 3 }, { kx0 + 3, ky1 - 3 }, { kx1 - 3, ky1 - 3 } };
		for (const auto& t : tc) {
			for (int y = t[1] - 5; y <= t[1] + 5; y++) for (int x = t[0] - 5; x <= t[0] + 5; x++) {
				float d = std::sqrt((float)((x - t[0]) * (x - t[0]) + (y - t[1]) * (y - t[1])));
				if (d <= 5.0f) for (int z = Hk + 1; z <= Hk + 14; z++) cv.P(x, y, z, Stone(x, y, z));
				for (int z = Hk + 15; z <= Hk + 25; z++) {
					float rr = 6.0f * (1.0f - (float)(z - Hk - 15) / 10.5f);
					if (d <= rr && d >= rr - 1.6f) cv.P(x, y, z, Vary((z % 2) ? slate : slate2, Hash(x, y, z, sd + 6), 3));
				}
			}
			cv.P(t[0], t[1], Hk + 26, gold);
			cv.P(t[0] + 6, t[1], Hk + 10, iron); cv.P(t[0] + 6, t[1], Hk + 9, kLantern);
			cv.P(t[0] - 6, t[1], Hk + 10, iron); cv.P(t[0] - 6, t[1], Hk + 9, kLantern);
		}
		// Hof: Laternenpfosten, Laternen an den Mauerinnenseiten
		auto InTower = [&](int x, int y) { for (const T& t : towers) { float d = std::sqrt((float)((x - t.x) * (x - t.x) + (y - t.y) * (y - t.y))); if (d <= t.r + 2.0f) return true; } return false; };
		for (int y = 20; y < S - 18; y += 22) for (int x = 20; x < S - 18; x += 22) {
			if (x >= kx0 - 4 && x <= kx1 + 4 && y >= ky0 - 4 && y <= ky1 + 4) continue;
			if (InTower(x, y) || InTower(x + 1, y)) continue;
			if (y < 20 && x > 58 && x < 82) continue;
			for (int z = 1; z <= 5; z++) cv.P(x, y, z, z == 1 ? quoin : dark);
			cv.P(x, y, 6, iron); cv.P(x + 1, y, 6, iron); cv.P(x + 1, y, 5, kLantern);
		}
		for (int k = 22; k < S - 22; k += 18) {
			if (!InTower(k, 13)) { cv.P(k, 12, 22, iron); cv.P(k, 13, 22, kLantern); }
			if (!InTower(k, S - 14)) { cv.P(k, S - 13, 22, iron); cv.P(k, S - 14, 22, kLantern); }
			if (!InTower(13, k)) { cv.P(12, k, 22, iron); cv.P(13, k, 22, kLantern); }
			if (!InTower(S - 14, k)) { cv.P(S - 13, k, 22, iron); cv.P(S - 14, k, 22, kLantern); }
		}
		(void)beam;
		return st;
	}
	// =====================================================================
	// Kastanienbaum -- Vorlage: echte Rosskastanie (Aesculus hippocastanum) und Edelkastanie (Castanea sativa)
	// =====================================================================
	// Wuchs der Rosskastanie: kurzer, kraeftiger Stamm (graubraun, im Alter rissig und leicht gedreht),
	// in etwa 4-5 m Hoehe Aufteilung in 4-6 starke, steil aufsteigende Hauptaeste; breite, hoch gewoelbte
	// Kuppelkrone (Breite etwa 3/4 der Hoehe), untere Aeste weit ausladend, ihre Spitzen haengen und biegen
	// wieder auf. Laub dicht in Buescheln (grosse handfoermige Blaetter, dunkelgruen, unten dunkler).
	// Mai: aufrechte weisse Bluetenkerzen mit gelb/rosa Fleck; Spaetsommer: stachelige gruene Fruechte;
	// Herbst: gelb-orange-braunes Laub, aufgeplatzte Fruechte mit braunen Kastanien.
	// Edelkastanie: dickerer, tief gefurchter, spiralig gedrehter Stamm, hellere glaenzende Blaetter,
	// im Juni lange gelblich-weisse Kaetzchen.
	// Massstab: 1 Block ~ 0.4 m -> 24 m Baum ~ 60 Bloecke, Krone ~ 46 Bloecke breit.
	// Varianten: 0 Rosskastanie Bluete, 1 Rosskastanie Sommer (Fruechte), 2 Rosskastanie Herbst, 3 Edelkastanie.
	const char* ChestnutName(int v) {
		static const char* n[4] = { "Kastanie_1_Bluete", "Kastanie_2_Sommer", "Kastanie_3_Herbst", "Kastanie_4_Edelkastanie" };
		return n[(v % 4 + 4) % 4];
	}
	Lab::Structure Chestnut(int variant, uint32_t seed) {
		const int v = (variant % 4 + 4) % 4;
		const bool edel = v == 3;
		const uint32_t sd = Hash(v, 77, 13, seed);
		const int H = 60 + (int)(U(Hash(1, 0, 0, sd)) * 4.0f);           // Kronenspitze
		const float R = edel ? 22.0f : 23.0f;                                // Kronenradius
		const int zb = edel ? 13 : 15;                                       // Kronenansatz
		const int zt = edel ? 10 : 12;                                       // Stammteilung
		const float r0 = edel ? 3.2f : 2.6f;                                 // Stammradius
		const int W = 2 * (int)std::ceil(R + 4.0f) + 1, SZ = H + 6;
		const float cx = W * 0.5f, cy = W * 0.5f;
		enum { M_NONE = 0, M_BARK, M_LEAF, M_FLOWER, M_FLOWERSPOT, M_FRUIT, M_NUT, M_CATKIN, M_ROOT };
		std::vector<uint8_t> mat((size_t)W * W * SZ, 0);
		auto I = [&](int x, int y, int z) { return ((size_t)z * W + y) * W + x; };
		auto In = [&](int x, int y, int z) { return x >= 0 && y >= 0 && z >= 0 && x < W && y < W && z < SZ; };
		auto Get = [&](int x, int y, int z) -> uint8_t { return In(x, y, z) ? mat[I(x, y, z)] : 0; };
		auto Set = [&](int x, int y, int z, uint8_t m) { if (In(x, y, z)) mat[I(x, y, z)] = m; };
		struct V3 { float x, y, z; };
		// Kapsel (Segment mit Radius ra -> rb) mit Material m; Rinde ueberschreibt Laub
		auto Capsule = [&](V3 a, V3 b, float ra, float rb, uint8_t m) {
			float mr = std::max(ra, rb) + 1.0f;
			int x0 = (int)std::floor(std::min(a.x, b.x) - mr), x1 = (int)std::ceil(std::max(a.x, b.x) + mr);
			int y0 = (int)std::floor(std::min(a.y, b.y) - mr), y1 = (int)std::ceil(std::max(a.y, b.y) + mr);
			int z0 = (int)std::floor(std::min(a.z, b.z) - mr), z1 = (int)std::ceil(std::max(a.z, b.z) + mr);
			float vx = b.x - a.x, vy = b.y - a.y, vz = b.z - a.z, l2 = vx * vx + vy * vy + vz * vz;
			for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
				float px = x + 0.5f - a.x, py = y + 0.5f - a.y, pz = z + 0.5f - a.z;
				float t = l2 > 0 ? (px * vx + py * vy + pz * vz) / l2 : 0.0f; t = std::max(0.0f, std::min(1.0f, t));
				float dx = px - vx * t, dy = py - vy * t, dz = pz - vz * t;
				float r = ra + (rb - ra) * t;
				if (dx * dx + dy * dy + dz * dz <= r * r) Set(x, y, z, m);
			}
		};
		auto Bez = [](V3 a, V3 c, V3 b, float t) { float u = 1 - t; return V3{ u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y, u * u * a.z + 2 * u * t * c.z + t * t * b.z }; };
		auto Curve = [&](V3 a, V3 c, V3 b, float ra, float rb, int steps) {
			V3 p = a;
			for (int i = 1; i <= steps; i++) {
				float t = (float)i / steps; V3 q = Bez(a, c, b, t);
				Capsule(p, q, ra + (rb - ra) * (t - 1.0f / steps), ra + (rb - ra) * t, M_BARK);
				p = q;
			}
		};
		// ---- Stamm mit Wurzelanlauf und Oberflaechenwurzeln
		float lean = (U(Hash(2, 0, 0, sd)) - 0.5f) * 2.0f, la = U(Hash(3, 0, 0, sd)) * kTau;
		V3 base{ cx, cy, 0.0f }, top{ cx + std::cos(la) * lean, cy + std::sin(la) * lean, (float)zt };
		Capsule(base, top, r0, r0 * 0.85f, M_BARK);
		for (int z = 0; z < 4; z++) Capsule(V3{ cx, cy, (float)z }, V3{ cx, cy, (float)z + 0.9f }, r0 + (4 - z) * 0.55f, r0 + (3.5f - z) * 0.55f, M_BARK);
		int nRoots = 5;
		for (int k = 0; k < nRoots; k++) {
			float a = k * kTau / nRoots + U(Hash(k, 4, 0, sd)) * 0.8f, L = r0 + 4.0f + U(Hash(k, 5, 0, sd)) * 3.0f;
			Capsule(V3{ cx, cy, 1.0f }, V3{ cx + std::cos(a) * L, cy + std::sin(a) * L, 0.5f }, 1.6f, 0.6f, M_ROOT);
		}
		// ---- Hauptaeste: steil aufsteigend, enden im oberen Teil der Krone
		const float zc = zb + (H - zb) * 0.42f, Rz = H - zc, Rzl = zc - zb + 2.0f;
		std::vector<V3> clusters; std::vector<float> clR;
		int nLimbs = edel ? 6 : 5;
		float a0 = U(Hash(6, 0, 0, sd)) * kTau;
		for (int k = 0; k < nLimbs; k++) {
			float az = a0 + k * kTau / nLimbs + (U(Hash(k, 7, 0, sd)) - 0.5f) * 0.6f;
			float rr = R * (0.45f + 0.25f * U(Hash(k, 8, 0, sd))), ez = zc + Rz * (0.35f + 0.35f * U(Hash(k, 9, 0, sd)));
			V3 E{ cx + std::cos(az) * rr, cy + std::sin(az) * rr, ez };
			V3 C{ top.x + (E.x - top.x) * 0.3f, top.y + (E.y - top.y) * 0.3f, top.z + (E.z - top.z) * 0.65f };
			Curve(top, C, E, edel ? 2.2f : 1.9f, 0.8f, 14);
			clusters.push_back(E); clR.push_back(6.0f);
			// Seitenaeste: unten weit ausladend und haengend (Spitze biegt wieder auf), oben steiler
			for (int j = 0; j < 3; j++) {
				float t = 0.4f + 0.22f * j;
				V3 S = Bez(top, C, E, t);
				float bz = az + (U(Hash(k, j, 10, sd)) - 0.5f) * 1.6f;
				float out = R * (0.55f + 0.35f * U(Hash(k, j, 11, sd))) - std::sqrt((S.x - cx) * (S.x - cx) + (S.y - cy) * (S.y - cy)) * 0.3f;
				out = std::max(6.0f, out * (j == 0 ? 1.0f : 0.7f));
				float dz = j == 0 ? -3.0f - 3.0f * U(Hash(k, j, 12, sd)) : 2.0f + 4.0f * j * U(Hash(k, j, 12, sd));
				V3 E2{ S.x + std::cos(bz) * out, S.y + std::sin(bz) * out, S.z + dz };
				V3 C2{ S.x + std::cos(bz) * out * 0.55f, S.y + std::sin(bz) * out * 0.55f, j == 0 ? S.z + dz * 1.3f : S.z + dz * 0.4f };
				if (j == 0) E2.z += 2.0f;                                        // Spitze biegt wieder auf
				Curve(S, C2, E2, 0.95f, 0.55f, 10);
				clusters.push_back(E2); clR.push_back(j == 0 ? 6.5f : 5.5f);
				clusters.push_back(Bez(S, C2, E2, 0.6f)); clR.push_back(4.5f);
			}
		}
		// Kronenhuelle: Buschel gleichmaessig auf der Kuppel (Fibonacci-Kugel), damit die Krone dicht ist
		const int nShell = 90;
		for (int i = 0; i < nShell; i++) {
			float zz = 1.0f - (i + 0.5f) / nShell * 1.35f;                        // +1 .. -0.35 (wenig unter dem Aequator)
			float rad = std::sqrt(std::max(0.0f, 1.0f - zz * zz)), ph = i * 2.39996323f + U(Hash(i, 14, 0, sd));
			float sx = std::cos(ph) * rad, sy = std::sin(ph) * rad;
			float k = 0.74f + 0.1f * U(Hash(i, 15, 0, sd));
			clusters.push_back(V3{ cx + sx * R * k, cy + sy * R * k, zz >= 0 ? zc + zz * Rz * k : zc + zz * Rzl * k });
			clR.push_back(5.0f + 2.0f * U(Hash(i, 16, 0, sd)));
		}
		// ---- Laub: Buschel mit Rauschen, nur in einer Schale der Kuppel (innen sieht man die Aeste)
		auto Env = [&](float x, float y, float z) {                           // 1 = Kronenrand
			float dx = (x - cx) / R, dy = (y - cy) / R, dz = z >= zc ? (z - zc) / Rz : (z - zc) / Rzl;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		};
		for (size_t ci = 0; ci < clusters.size(); ci++) {
			const V3& P = clusters[ci]; float rc = clR[ci];
			for (int z = (int)(P.z - rc) - 1; z <= (int)(P.z + rc) + 1; z++)
				for (int y = (int)(P.y - rc) - 1; y <= (int)(P.y + rc) + 1; y++)
					for (int x = (int)(P.x - rc) - 1; x <= (int)(P.x + rc) + 1; x++) {
						if (!In(x, y, z) || z < zb - 3) continue;
						float px = x + 0.5f, py = y + 0.5f, pz = z + 0.5f;
						float dx = (px - P.x) / rc, dy = (py - P.y) / rc, dz = (pz - P.z) / (rc * 0.78f);
						float n = Noise3(px / 3.2f, py / 3.2f, pz / 3.2f, sd + 21) - 0.5f;
						if (dx * dx + dy * dy + dz * dz > 1.0f + n * 0.7f) continue;
						float e = Env(px, py, pz) + (Noise3(px / 5.0f, py / 5.0f, pz / 5.0f, sd + 22) - 0.5f) * 0.18f;
						if (e > 1.0f || e < 0.5f) continue;
						if (Get(x, y, z) == M_NONE) Set(x, y, z, M_LEAF);
					}
		}
		// Zweigspitzen, die aus der Krone ragen, kappen (die Krone schliesst die Aeste ein)
		for (int z = zb; z < SZ; z++) for (int y = 0; y < W; y++) for (int x = 0; x < W; x++)
			if (Get(x, y, z) == M_BARK && Env(x + 0.5f, y + 0.5f, z + 0.5f) > 0.97f) Set(x, y, z, M_NONE);
		// Einzelne lose Laubvoxel entfernen (kein "Staub" an der Krone)
		for (int z = 0; z < SZ; z++) for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			if (Get(x, y, z) != M_LEAF) continue;
			int nb = 0;
			for (int d = 0; d < 6; d++) { static const int D[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} }; if (Get(x + D[d][0], y + D[d][1], z + D[d][2])) nb++; }
			if (nb <= 1) Set(x, y, z, M_NONE);
		}
		// ---- Jahreszeit: Bluetenkerzen, Fruechte, Kaetzchen (auf der Kronenoberflaeche)
		for (int z = SZ - 2; z >= 0; z--) for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			if (Get(x, y, z) != M_LEAF) continue;
			uint32_t h = Hash(x, y, z, sd + 31);
			bool upOpen = Get(x, y, z + 1) == M_NONE, downOpen = z > 0 && Get(x, y, z - 1) == M_NONE;
			bool sideOpen = !Get(x + 1, y, z) || !Get(x - 1, y, z) || !Get(x, y + 1, z) || !Get(x, y - 1, z);
			if (v == 0 && upOpen && z > zc - 4 && (h & 1023) < 70) {             // Kerze 3-5 hoch, aufrecht
				int hgt = 3 + (int)((h >> 10) % 3);
				bool ok = true; for (int k = 1; k <= hgt; k++) if (Get(x, y, z + k)) ok = false;
				if (ok) { for (int k = 1; k <= hgt; k++) Set(x, y, z + k, M_FLOWER); Set(x, y, z + 1 + (int)((h >> 14) % (uint32_t)std::max(1, hgt - 1)), M_FLOWERSPOT); }
			} else if ((v == 1 || v == 2) && (upOpen || sideOpen) && (h & 1023) < (v == 1 ? 28u : 22u)) {
				Set(x, y, z, v == 1 ? M_FRUIT : (((h >> 10) & 1) ? M_NUT : M_FRUIT));
			} else if (v == 3 && (downOpen || sideOpen) && Env(x + 0.5f, y + 0.5f, z + 0.5f) > 0.8f && (h & 1023) < 60) {
				int L = 3 + (int)((h >> 10) % 3);                                      // Kaetzchen haengen
				for (int k = 1; k <= L; k++) { if (Get(x, y, z - k)) break; Set(x, y, z - k, M_CATKIN); }
			}
		}
		// ---- Farben
		C3 leaf[4];
		if (v == 2)      { leaf[0] = { 150, 74, 30 }; leaf[1] = { 182, 104, 36 }; leaf[2] = { 206, 140, 46 }; leaf[3] = { 222, 178, 70 }; }
		else if (edel)   { leaf[0] = { 64, 108, 38 }; leaf[1] = { 86, 132, 46 }; leaf[2] = { 110, 158, 60 }; leaf[3] = { 138, 182, 78 }; }
		else             { leaf[0] = { 40, 78, 32 }; leaf[1] = { 54, 98, 40 }; leaf[2] = { 72, 122, 50 }; leaf[3] = { 98, 148, 62 }; }
		const C3 barkA = edel ? C3{ 98, 78, 60 } : C3{ 108, 94, 82 }, barkB = edel ? C3{ 82, 64, 50 } : C3{ 92, 80, 70 }, furrow = edel ? C3{ 60, 46, 36 } : C3{ 72, 62, 54 };
		Lab::Structure s; s.Create(W, W, SZ);
		s.name = ChestnutName(v); s.origin = "mod: kastanie";
		for (int z = 0; z < SZ; z++) for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			uint8_t m = Get(x, y, z); if (!m) continue;
			uint32_t h = Hash(x, y, z, sd + 41);
			C3 c;
			if (m == M_BARK || m == M_ROOT) {
				float ang = std::atan2(y + 0.5f - cy, x + 0.5f - cx);
				float twist = edel ? std::sin(ang * 7.0f + z * 0.55f) : std::sin(ang * 5.0f + z * 0.32f);
				c = twist > (edel ? 0.35f : 0.6f) ? furrow : (((h >> 3) & 1) ? barkA : barkB);
				if (m == M_ROOT) c = Vary(barkB, h, 6);
				c = Vary(c, h, 5);
			} else if (m == M_LEAF) {
				bool up = !Get(x, y, z + 1), dn = z > 0 && !Get(x, y, z - 1);
				float t = (float)(z - zb) / (float)(H - zb);
				float f = t * 1.6f + (Noise3(x / 4.0f, y / 4.0f, z / 4.0f, sd + 51) - 0.5f) * 2.2f + (up ? 1.0f : 0.0f) - (dn ? 0.9f : 0.0f);
				int k = f < 0.2f ? 0 : (f < 0.9f ? 1 : (f < 1.6f ? 2 : 3));
				if (v == 2 && (h % 9u) == 0) k = 3 - k;                                 // Herbst: gesprenkelt
				c = Vary(leaf[k], h, 6);
			} else if (m == M_FLOWER) c = Vary(C3{ 242, 238, 228 }, h, 4);
			else if (m == M_FLOWERSPOT) c = ((h >> 4) & 1) ? C3{ 232, 168, 176 } : C3{ 236, 204, 92 };
			else if (m == M_FRUIT) c = Vary(v == 2 ? C3{ 150, 148, 70 } : C3{ 128, 160, 66 }, h, 6);
			else if (m == M_NUT) c = Vary(C3{ 112, 58, 30 }, h, 5);
			else c = Vary(C3{ 224, 212, 138 }, h, 6);                               // Kaetzchen
			c = Safe(c);
			s.Set(x, y, z, c.r, c.g, c.b);
		}
		return s;
	}

	// =====================================================================
	// Stadtviertel (2026-10-13): Haendlerhuette, Alchemistenhaus mit Braustand, Tempel, Kugel
	// style 0 = Elfen (Holz, Laub, Silber), 1 = Zwerge (Granit, Schiefer, Gold). Vorderseite bei y = 0.
	// =====================================================================
	struct Style { C3 floor, floor2, wall, wall2, post, roof, roof2, trim; };
	static Style StyleOf(int style) {
		if (style == 1) return { { 140, 134, 126 }, { 124, 118, 110 }, { 122, 116, 110 }, { 104, 100, 96 }, { 104, 72, 46 }, { 70, 78, 92 }, { 58, 66, 80 }, { 214, 172, 64 } };
		return { { 150, 112, 72 }, { 132, 96, 60 }, { 176, 142, 96 }, { 156, 122, 80 }, { 110, 80, 52 }, { 70, 132, 66 }, { 92, 156, 80 }, { 214, 220, 228 } };
	}
	// Haendlerhaus (Stand 3, 2026-10-13): 19 x 16 x 17 -- vorn offener Verkaufsstand (Theke, Markise), hinten ein
	// Laden mit Tuer, Fenstern, Regalen und Satteldach. Haendler steht hinter der Theke (vx, vy).
	HutInfo MarketHutInfo() { HutInfo h; h.w = 19; h.d = 16; h.vx = 9; h.vy = 4; return h; }
	Lab::Structure MarketHut(int style, int variant, uint32_t seed) {
		const HutInfo hi = MarketHutInfo();
		const int W = hi.w, D = hi.d, H = 17, yr = 6;                      // Laden ab y = 6
		const uint32_t sd = seed * 2654435761u + (uint32_t)variant * 97u + (uint32_t)style * 7919u;
		static const C3 kAwn[4][2] = { { { 196, 62, 58 }, { 236, 226, 206 } }, { { 58, 112, 186 }, { 236, 226, 206 } },
		                               { { 210, 168, 52 }, { 120, 70, 40 } },   { { 82, 150, 74 }, { 236, 226, 206 } } };
		static const C3 kGoods[6] = { { 206, 60, 50 }, { 236, 196, 70 }, { 120, 170, 70 }, { 160, 110, 70 }, { 90, 120, 200 }, { 230, 140, 60 } };
		const Style st = StyleOf(style);
		Lab::Structure m; m.Create(W, D, H);
		m.name = std::string(style ? "zwergen" : "elfen") + "-haendlerhaus-" + std::to_string(variant + 1); m.origin = "mod: stadt";
		Cv cv(m);
		for (int y = 0; y < D; y++) for (int x = 0; x < W; x++) cv.P(x, y, 0, Vary(((x + y) & 1) ? st.floor : st.floor2, Hash(x, y, 0, sd), 3));
		// Verkaufsstand: Pfosten, Theke mit Waren, Markise (gestreift, nach vorn geneigt)
		for (int z = 1; z <= 7; z++) for (int x : { 0, W / 2, W - 1 }) cv.P(x, 0, z, st.post);
		for (int x = 2; x < W - 2; x++) {
			cv.P(x, 2, 1, Vary(st.post, Hash(x, 2, 1, sd + 2), 3)); cv.P(x, 2, 2, Vary(st.wall2, Hash(x, 2, 2, sd + 3), 3));
			if (x != W / 2 && Hash(x, 2, 3, sd) % 3u) cv.P(x, 2, 3, Vary(kGoods[(x + variant) % 6], Hash(x, 3, 3, sd), 8));
		}
		for (int y = 0; y < yr; y++) { int z = 8 + (yr - 1 - y) / 3; for (int x = 0; x < W; x++) cv.P(x, y, z, kAwn[variant & 3][(x / 2) & 1]); }
		cv.P(4, 1, 7, kLantern); cv.P(W - 5, 1, 7, kLantern);
		// Laden: Waende mit Eckpfosten, Tuer (Mitte vorn), Fenster, Regale innen
		for (int z = 1; z <= 9; z++) for (int y = yr; y < D; y++) for (int x = 0; x < W; x++) {
			bool edge = x == 0 || x == W - 1 || y == yr || y == D - 1;
			if (!edge) continue;
			bool post = (x == 0 || x == W - 1) && (y == yr || y == D - 1);
			cv.P(x, y, z, post ? st.post : Vary((z % 3 == 0) ? st.wall2 : st.wall, Hash(x, y, z, sd + 1), 4));
		}
		for (int z = 1; z <= 3; z++) { cv.P(W / 2, yr, z, kDoor); cv.P(W / 2 + 1, yr, z, kDoor); }
		for (int x : { 3, W - 5 }) for (int dz = 0; dz < 2; dz++) { cv.P(x, yr, 4 + dz, kWindow); cv.P(x + 1, yr, 4 + dz, kWindow); cv.P(x, D - 1, 4 + dz, kWindow); cv.P(x + 1, D - 1, 4 + dz, kWindow); }
		for (int dz = 0; dz < 2; dz++) { cv.P(0, yr + 4, 4 + dz, kWindow); cv.P(0, yr + 5, 4 + dz, kWindow); cv.P(W - 1, yr + 4, 4 + dz, kWindow); cv.P(W - 1, yr + 5, 4 + dz, kWindow); }
		cv.P(2, D - 2, 1, kShelf); cv.P(W - 3, D - 2, 1, kShelf); cv.P(W / 2, D - 3, 1, kTable);
		for (int y = yr; y < D; y++) for (int x = 0; x < W; x++) cv.P(x, y, 10, Vary(st.wall2, Hash(x, y, 10, sd + 5), 3));   // Decke
		// Satteldach (First entlang x), Giebel
		for (int k = 0; k <= (D - yr) / 2 + 1; k++) {
			int z = 11 + k;
			for (int x = -0; x < W; x++) {
				int ya = yr - 1 + k, yb = D - k;
				if (ya > yb) continue;
				C3 rc = Vary(((x + k) & 1) ? st.roof : st.roof2, Hash(x, k, z, sd + 6), 4);
				cv.P(x, ya, z, rc); cv.P(x, yb, z, rc);
				if (x == 0 || x == W - 1) for (int y = ya + 1; y < yb; y++) cv.P(x, y, z, Vary(st.wall, Hash(x, y, z, sd + 7), 4));
			}
		}
		return m;
	}

	static const C3 kBrew{ 96, 214, 120 }, kBrew2{ 146, 236, 166 };
	bool IsGlowColor(uint8_t r, uint8_t g, uint8_t b) {
		return (r == kBrew.r && g == kBrew.g && b == kBrew.b) || (r == kBrew2.r && g == kBrew2.g && b == kBrew2.b);
	}
	AlchemyInfo AlchemyHouseInfo() { AlchemyInfo a; a.w = 17; a.d = 16; a.brewX = 4; a.brewY = 3; a.npcX = 11; a.npcY = 3; return a; }
	Lab::Structure AlchemyHouse(int style, uint32_t seed) {
		const AlchemyInfo ai = AlchemyHouseInfo();
		const int W = ai.w, D = ai.d, y0 = 7, H = 22;
		const uint32_t sd = seed * 40503u + 977u + (uint32_t)style * 131u;
		const Style st = StyleOf(style);
		const C3 iron{ 58, 56, 62 }, iron2{ 78, 76, 84 }, brew = kBrew, brew2 = kBrew2, bottle{ 120, 190, 220 }, herb{ 96, 150, 70 };
		Lab::Structure m; m.Create(W, D, H);
		m.name = std::string(style ? "zwergen" : "elfen") + "-alchemistenhaus"; m.origin = "mod: stadt";
		Cv cv(m);
		for (int y = 0; y < D; y++) for (int x = 0; x < W; x++) cv.P(x, y, 0, Vary(((x * 3 + y) % 4) ? st.floor : st.floor2, Hash(x, y, 0, sd), 3));
		// Haus y0..D-1, 2 Geschosse a 5
		const int top = 10;
		for (int z = 1; z <= top; z++) for (int y = y0; y < D; y++) for (int x = 0; x < W; x++) {
			bool edge = x == 0 || x == W - 1 || y == y0 || y == D - 1;
			if (edge) cv.P(x, y, z, Vary((z == 5 || z == top || ((x == 0 || x == W - 1) && (y == y0 || y == D - 1))) ? st.post : ((z % 2) ? st.wall : st.wall2), Hash(x, y, z, sd + 1), 4));
			else if (z == 5) cv.P(x, y, z, Vary(st.floor2, Hash(x, y, z, sd + 2), 3));
		}
		for (int z = 1; z <= 3; z++) { cv.P(W / 2, y0, z, kDoor); cv.P(W / 2 + 1, y0, z, kDoor); }
		for (int f = 0; f < 2; f++) for (int x : { 3, W - 5 }) for (int dz = 0; dz <= 1; dz++) for (int ox = 0; ox <= 1; ox++) { cv.P(x + ox, y0, 2 + f * 5 + dz, kWindow); cv.P(x + ox, D - 1, 2 + f * 5 + dz, kWindow); }
		for (int y = y0 + 3; y <= D - 4; y += 3) for (int z = 2; z <= 3; z++) { cv.P(0, y, z, kWindow); cv.P(W - 1, y, z, kWindow); }
		cv.P(2, D - 2, 1, kShelf); cv.P(5, D - 2, 1, kShelf); cv.P(W - 3, D - 2, 1, kShelf); cv.P(W - 3, y0 + 2, 1, kTable);
		cv.P(3, y0 + 2, 6, kBed); cv.P(W / 2, (y0 + D) / 2, 4, kLantern); cv.P(W / 2, (y0 + D) / 2, 9, kLantern);
		// Treppe ins Obergeschoss (innen, an der rechten Wand)
		for (int s2 = 0; s2 < 5; s2++) { int x = W - 2 - s2 / 2, y = D - 2 - s2, z = 1 + s2; cv.P(x, y, z, st.post); cv.E(x, y, 5); cv.E(x, y + 0, 5); }
		// Dach: Kegelstumpf / Walmdach mit Kraeuterbuendeln, Schornstein mit gruenem Rauchloch
		for (int k = 0; k <= 7; k++) {
			int z = top + 1 + k;
			for (int y = y0 - 1 + k; y <= D - k; y++) for (int x = -1 + k; x <= W - k; x++) {
				bool edge = y == y0 - 1 + k || y == D - k || x == -1 + k || x == W - k;
				if (edge && m.In(x, y, z)) cv.P(x, y, z, Vary((k % 2) ? st.roof : st.roof2, Hash(x, y, z, sd + 3), 5));
			}
		}
		for (int z = 1; z <= top + 9; z++) { cv.P(2, D - 3, z, Vary(iron2, Hash(2, D - 3, z, sd + 4), 4)); cv.P(3, D - 3, z, Vary(iron2, Hash(3, D - 3, z, sd + 4), 4)); }
		cv.P(2, D - 3, top + 10, brew); cv.P(3, D - 3, top + 10, brew2);
		for (int x = 2; x < W - 2; x += 3) cv.P(x, y0 - 1, top, herb);                                       // Kraeuter unter der Traufe
		// Braustand im Vorhof: natives Lagerfeuer (Objekt 0x41, Station 9) frei sichtbar bei z 1; darueber an einem
		// Querbalken (z 7) ein kleiner Kessel (z 5..6) -- nichts verdeckt das Feuer (Stand 2026-10-13: vorher lag eine
		// Eisenplatte ueber dem Feuer, man sah nur Bloecke).
		const int bx = ai.brewX, by = ai.brewY;
		for (int z = 1; z <= 7; z++) for (int ox : { -2, 2 }) cv.P(bx + ox, by, z, st.post);
		for (int ox = -2; ox <= 2; ox++) cv.P(bx + ox, by, 8, st.post);
		cv.P(bx, by, 7, iron2);                                                                           // Kette
		for (int ox = -1; ox <= 1; ox++) cv.P(bx + ox, by, 5, iron);
		cv.P(bx, by - 1, 5, iron); cv.P(bx, by + 1, 5, iron2); cv.P(bx, by, 6, brew); cv.P(bx - 1, by, 6, iron); cv.P(bx + 1, by, 6, iron2);
		cv.P(bx + 3, by - 2, 1, bottle); cv.P(bx + 3, by - 1, 1, bottle); cv.P(bx - 3, by + 1, 1, herb);
		cv.P(bx + 2, by + 3, 1, kTable);
		return m;
	}

	TempleInfo TempleInfoOf() { TempleInfo t; t.s = 41; t.orbZ = 12; return t; }
	Lab::Structure Temple(int style, uint32_t seed) {
		const TempleInfo ti = TempleInfoOf();
		const int S = ti.s, c = S / 2, H = 40;
		const uint32_t sd = seed * 69069u + 4099u + (uint32_t)style * 17u;
		const C3 marble{ 228, 226, 218 }, marble2{ 210, 208, 200 }, gold{ 222, 186, 84 }, vine{ 86, 150, 72 }, vine2{ 120, 176, 90 };
		const C3 gran{ 132, 126, 120 }, gran2{ 112, 108, 104 }, ironC{ 72, 70, 78 };
		const C3 A = style ? gran : marble, B = style ? gran2 : marble2, R1 = style ? C3{ 70, 78, 92 } : C3{ 90, 150, 140 }, R2 = style ? C3{ 58, 66, 80 } : C3{ 110, 170, 156 };
		Lab::Structure m; m.Create(S, S, H);
		m.name = std::string(style ? "zwergen" : "elfen") + "-tempel"; m.origin = "mod: stadt";
		Cv cv(m);
		// Sockel in 3 Stufen (rundum begehbar), Boden auf z 3
		for (int z = 0; z <= 2; z++) for (int y = z; y < S - z; y++) for (int x = z; x < S - z; x++)
			cv.P(x, y, z, Vary(((x + y) & 1) ? A : B, Hash(x, y, z, sd), 3));
		for (int y = 3; y < S - 3; y++) for (int x = 3; x < S - 3; x++) {
			float r = std::sqrt((x + 0.5f - c - 0.5f) * (x + 0.5f - c - 0.5f) + (y + 0.5f - c - 0.5f) * (y + 0.5f - c - 0.5f));
			int ring = (int)(r / 3.0f);
			cv.P(x, y, 3, (r < 4.5f) ? gold : Vary((ring & 1) ? A : B, Hash(x, y, 3, sd + 1), 3));
		}
		// 12 Saeulen (Radius 15), Gebaelk, Kuppel mit Oeffnung
		const float RC = 15.0f;
		for (int k = 0; k < 12; k++) {
			float a = k * kTau / 12.0f; int px = c + (int)std::lround(std::cos(a) * RC), py = c + (int)std::lround(std::sin(a) * RC);
			for (int z = 4; z <= 19; z++) for (int ox = 0; ox <= 1; ox++) for (int oy = 0; oy <= 1; oy++) {
				C3 col = (z == 4 || z == 19) ? gold : Vary(((z + k) % 5 == 0) ? B : A, Hash(px + ox, py + oy, z, sd + 2), 3);
				if (!style && z > 6 && z < 18 && ((z + ox + oy + k) % 6 == 0)) col = (z & 1) ? vine : vine2;      // Elfen: Ranken
				cv.P(px + ox, py + oy, z, col);
			}
			if (k % 3 == 0) cv.P(px + (std::cos(a) > 0 ? -1 : 2), py, 15, kLantern);
		}
		for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float r = std::sqrt((x - c) * (x - c) + (float)(y - c) * (y - c));
			if (r >= RC - 2.5f && r <= RC + 2.5f) { cv.P(x, y, 20, Vary(A, Hash(x, y, 20, sd + 3), 3)); cv.P(x, y, 21, (r > RC + 1.5f) ? gold : Vary(B, Hash(x, y, 21, sd + 3), 3)); }
		}
		for (int z = 22; z < H; z++) {                                                                      // Kuppel (Halbkugel), Oeffnung r < 3
			float zz = (float)(z - 21);
			for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
				float r = std::sqrt((x - c) * (x - c) + (float)(y - c) * (y - c) + zz * zz * 1.15f);
				float rh = std::sqrt((x - c) * (x - c) + (float)(y - c) * (y - c));
				if (r <= RC + 1.5f && r >= RC - 0.5f && rh >= 3.0f) cv.P(x, y, z, Vary(((x + y + z) % 4) ? R1 : R2, Hash(x, y, z, sd + 4), 4));
			}
		}
		if (style) for (int k = 0; k < 4; k++) { float a = k * kTau / 4.0f + kTau / 8.0f; cv.P(c + (int)(std::cos(a) * 9), c + (int)(std::sin(a) * 9), 4, ironC); }
		// Altarring um die Kugel (Kugel selbst ist ein eigenes Teil, schwebt bei orbZ)
		for (int k = 0; k < 16; k++) { float a = k * kTau / 16.0f; cv.P(c + (int)std::lround(std::cos(a) * 5.5f), c + (int)std::lround(std::sin(a) * 5.5f), 4, gold); }
		return m;
	}
	Lab::Structure Orb(uint32_t seed) {
		const int S = 7; Lab::Structure m; m.Create(S, S, S);
		m.name = "tempel-kugel"; m.origin = "mod: stadt";
		Cv cv(m);
		for (int z = 0; z < S; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
			float dx = x - 3.0f, dy = y - 3.0f, dz = z - 3.0f, r = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (r > 3.2f) continue;
			float t = (dz + 3.0f) / 6.0f;
			C3 col{ (uint8_t)(110 + 90 * t), (uint8_t)(200 + 30 * t), 250 };
			if (r < 1.6f) col = { 250, 250, 240 };
			cv.P(x, y, z, Vary(col, Hash(x, y, z, seed + 77), 4));
		}
		return m;
	}

	// ---- Tempelstatuen (2026-10-13) -------------------------------------------------------------
	// 15 x 15 x 34: dreistufiger Sockel (z 0..3), Figur ~26 hoch, Vorderseite y = 0.
	// variant 0 Hueterin (hebt eine goldene Kugel), 1 Waechter (Schwert / Zwerge: Hammer vor sich),
	// 2 Weiser (Kapuze, Stab, Buch). style 0 Elfen (Marmor, Gold, Moos), 1 Zwerge (Granit, Bronze).
	Lab::Structure Statue(int variant, int style, uint32_t seed) {
		const int S = 15, H = 34; const float cx = 7.0f, cy = 7.5f;
		const uint32_t sd = seed * 2654435761u + 911u + (uint32_t)variant * 31u + (uint32_t)style * 7u;
		const C3 st1 = style ? C3{ 134, 128, 122 } : C3{ 226, 224, 216 }, st2 = style ? C3{ 116, 112, 108 } : C3{ 206, 204, 196 };
		const C3 metal = style ? C3{ 176, 124, 66 } : C3{ 222, 186, 84 }, moss{ 92, 140, 74 }, dark = style ? C3{ 92, 88, 86 } : C3{ 176, 172, 164 };
		Lab::Structure m; m.Create(S, S, H);
		m.name = std::string("statue-") + (style ? "zwerge-" : "elfen-") + std::to_string(variant + 1); m.origin = "mod: stadt";
		Cv cv(m);
		auto Stone = [&](int x, int y, int z) { uint32_t h = Hash(x, y, z, sd); C3 c = (h % 5 == 0) ? st2 : st1; if (!style && z < 9 && h % 23 == 0) c = moss; cv.P(x, y, z, Vary(c, h >> 4, 3)); };
		// Sockel
		for (int z = 0; z <= 3; z++) { int in = z < 2 ? 0 : (z == 2 ? 1 : 2); for (int y = in; y < S - in; y++) for (int x = in; x < S - in; x++) cv.P(x, y, z, Vary((z == 3 && (x == in || y == in || x == S - 1 - in || y == S - 1 - in)) ? metal : dark, Hash(x, y, z, sd + 1), 3)); }
		auto Ell = [&](float ex, float ey, float ez, float rx, float ry, float rz, C3* col) {
			for (int z = (int)(ez - rz) - 1; z <= (int)(ez + rz) + 1; z++) for (int y = (int)(ey - ry) - 1; y <= (int)(ey + ry) + 1; y++) for (int x = (int)(ex - rx) - 1; x <= (int)(ex + rx) + 1; x++) {
				float dx = (x + 0.5f - ex) / rx, dy = (y + 0.5f - ey) / ry, dz = (z + 0.5f - ez) / rz;
				if (dx * dx + dy * dy + dz * dz > 1.0f) continue;
				if (col) cv.P(x, y, z, Vary(*col, Hash(x, y, z, sd + 2), 3)); else Stone(x, y, z);
			}
		};
		auto Bar = [&](float x0, float y0, float z0, float x1, float y1, float z1, float r, C3* col) {   // Zylinder zwischen zwei Punkten
			int n = (int)(std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0)) * 2.0f) + 1;
			for (int i = 0; i <= n; i++) { float t = (float)i / n; Ell(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t, r, r, r, col); }
		};
		// Gewand/Koerper: Kegel z 4..17 (Saum breit), Rumpf z 17..22, Schultern, Hals, Kopf
		for (int z = 4; z <= 17; z++) {
			float t = (z - 4) / 13.0f, rx = 4.6f - 1.9f * t, ry = 3.6f - 1.4f * t;
			for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
				float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
				if (dx * dx + dy * dy <= 1.0f) Stone(x, y, z);
			}
		}
		Ell(cx, cy, 19.5f, 2.9f, 2.0f, 3.0f, nullptr);                      // Rumpf
		Ell(cx, cy, 21.8f, 3.9f, 2.1f, 1.2f, nullptr);                      // Schultern
		Ell(cx, cy, 23.0f, 1.0f, 1.0f, 1.0f, nullptr);                      // Hals
		Ell(cx, cy, 25.3f, 2.1f, 2.1f, 2.3f, nullptr);                      // Kopf
		if (style) Ell(cx, cy - 1.6f, 23.3f, 1.8f, 1.2f, 1.8f, nullptr);   // Zwerge: Bart
		C3 met = metal;
		switch (variant % 3) {
			case 0: {                                                         // Hueterin: rechte Hand hebt die Kugel, linke am Gewand
				Bar(cx + 3.4f, cy, 21.5f, cx + 4.4f, cy - 0.5f, 27.5f, 0.9f, nullptr);
				Ell(cx + 4.5f, cy - 0.5f, 30.0f, 1.8f, 1.8f, 1.8f, &met);
				Bar(cx - 3.4f, cy, 21.5f, cx - 3.6f, cy - 1.0f, 15.5f, 0.9f, nullptr);
				for (int x = (int)cx - 2; x <= (int)cx + 2; x++) cv.P(x, (int)cy, 27, met);          // Diadem
				break;
			}
			case 1: {                                                         // Waechter: Haende auf dem Knauf, Klinge/Stiel vor dem Gewand
				Bar(cx - 3.4f, cy, 21.5f, cx - 0.8f, cy - 3.2f, 17.0f, 0.9f, nullptr);
				Bar(cx + 3.4f, cy, 21.5f, cx + 0.8f, cy - 3.2f, 17.0f, 0.9f, nullptr);
				int bx = (int)cx, by = (int)(cy - 4.0f);
				if (!style) {
					for (int z = 4; z <= 16; z++) { cv.P(bx, by, z, z < 6 ? met : C3{ 196, 200, 208 }); }
					for (int x = bx - 2; x <= bx + 2; x++) cv.P(x, by, 16, met);
					cv.P(bx, by, 17, met); cv.P(bx, by, 18, met);
				} else {
					for (int z = 4; z <= 17; z++) cv.P(bx, by, z, C3{ 120, 86, 56 });
					for (int z = 4; z <= 7; z++) for (int x = bx - 2; x <= bx + 2; x++) for (int y = by - 1; y <= by + 1; y++) cv.P(x, y, z, Vary(C3{ 104, 104, 112 }, Hash(x, y, z, sd + 5), 4));
				}
				for (int y = (int)cy - 2; y <= (int)cy + 2; y++) for (int x = (int)cx - 2; x <= (int)cx + 2; x++) cv.P(x, y, 28, met);   // Helmkamm
				for (int z = 27; z <= 30; z++) cv.P((int)cx, (int)cy, z, met);
				break;
			}
			default: {                                                        // Weiser: Kapuze, Stab rechts, Buch links
				for (int z = 24; z <= 28; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
					float dx = x + 0.5f - cx, dy = y + 0.5f - (cy + 0.4f), r = std::sqrt(dx * dx + dy * dy);
					float R = 2.9f - (z - 24) * 0.35f;
					if (r <= R && r >= R - 1.2f && !(dy < -1.0f && z <= 26 && std::fabs(dx) < 1.4f)) Stone(x, y, z);
				}
				Bar(cx + 3.4f, cy, 21.5f, cx + 4.2f, cy - 1.5f, 16.5f, 0.9f, nullptr);
				for (int z = 4; z <= 31; z++) cv.P((int)(cx + 4.5f), (int)(cy - 1.5f), z, z >= 29 ? met : C3{ 132, 98, 66 });
				Bar(cx - 3.4f, cy, 21.5f, cx - 2.0f, cy - 2.6f, 17.0f, 0.9f, nullptr);
				for (int z = 16; z <= 18; z++) for (int x = (int)cx - 3; x <= (int)cx - 1; x++) cv.P(x, (int)(cy - 3.6f), z, z == 17 ? C3{ 236, 228, 200 } : C3{ 120, 50, 44 });
				break;
			}
		}
		return m;
	}
}
