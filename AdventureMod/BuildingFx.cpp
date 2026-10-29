#include "BuildingFx.h"
#include <windows.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <algorithm>

// Herleitung: BuildingFx.h. Skalen (Halbmasse) aus RE_Objekttypen.md 2b bzw. MountainVillage kScales (gemessen).

namespace BuildingFx {
	static const Choice kChoices[] = {
		{ "Truhe",                    K_OBJECT, 0x0A, 1.2f, 0.8f, 0.8f, "" },
		{ "Tisch",                    K_OBJECT, 0x0C, 3.0f, 3.0f, 1.0f, "" },
		{ "Stuhl",                    K_OBJECT, 0x10, 1.0f, 1.0f, 0.5f, "" },
		{ "Bank",                     K_OBJECT, 0x12, 3.0f, 1.0f, 0.4f, "" },
		{ "Bett",                     K_OBJECT, 0x13, 2.0f, 3.0f, 1.0f, "liegen" },
		{ "Nachttisch",               K_OBJECT, 0x14, 1.0f, 1.0f, 1.0f, "" },
		{ "Marktstand",               K_OBJECT, 0x15, 3.5f, 2.0f, 3.0f, "" },
		{ "Fass",                     K_OBJECT, 0x18, 2.0f, 2.0f, 2.0f, "" },
		{ "Kiste",                    K_OBJECT, 0x19, 1.8f, 1.8f, 1.8f, "" },
		{ "Sack",                     K_OBJECT, 0x1B, 1.5f, 1.5f, 1.4f, "" },
		{ "Theke",                    K_OBJECT, 0x1F, 4.0f, 1.0f, 1.0f, "" },
		{ "Regal",                    K_OBJECT, 0x20, 2.0f, 1.0f, 1.57f, "" },
		{ "Zaun",                     K_OBJECT, 0x34, 2.0f, 1.0f, 1.5f, "" },
		{ "Strassenlaterne",          K_OBJECT, 0x32, 1.0f, 1.0f, 4.0f, "leuchtet" },
		{ "Lagerfeuer / Braustand",   K_OBJECT, 0x41, 2.4f, 2.4f, 0.5f, "Station 9: Kochen, Traenke" },
		{ "Ofen (Schmiede)",          K_OBJECT, 0x47, 2.0f, 2.0f, 1.5f, "Station 3" },
		{ "Amboss (Schmiede)",        K_OBJECT, 0x48, 1.5f, 1.0f, 1.0f, "Station 4" },
		{ "Spinnrad (Weberei)",       K_OBJECT, 0x49, 2.0f, 1.5f, 1.5f, "Station 5, Groesse geschaetzt" },
		{ "Webstuhl (Weberei)",       K_OBJECT, 0x4A, 2.0f, 1.5f, 1.5f, "Station 6, Groesse geschaetzt" },
		{ "Saege (Zimmerei)",         K_OBJECT, 0x4B, 2.0f, 1.5f, 1.5f, "Station 7, Groesse geschaetzt" },
		{ "Werkbank (Zimmerei)",      K_OBJECT, 0x4C, 2.0f, 1.5f, 1.5f, "Station 8, Groesse geschaetzt" },
		{ "Anpassungsbank",           K_OBJECT, 0x4D, 2.0f, 1.0f, 1.0f, "Waffen anpassen" },
		{ "Haendler A (0x80)",        K_VENDOR, 0x80, 0, 0, 0, "nativer Dorfhaendler, Waren vom Spiel" },
		{ "Haendler B (0x81)",        K_VENDOR, 0x81, 0, 0, 0, "nativer Dorfhaendler, Waren vom Spiel" },
		{ "Haendler C (0x82)",        K_VENDOR, 0x82, 0, 0, 0, "nativer Dorfhaendler, Waren vom Spiel" },
		{ "Haendler D (0x83)",        K_VENDOR, 0x83, 0, 0, 0, "nativer Dorfhaendler, Waren vom Spiel" },
		{ "Wirt (0x84)",              K_VENDOR, 0x84, 0, 0, 0, "" },
		{ "Alchemist",                K_ALCHEMIST, 0x80, 0, 0, 0, "verkauft alle Rezeptzutaten" },
		{ "Unser Haendler (Posten)",  K_MERCHANT, 0, 0, 0, 0, "fester Posten des eigenen Haendlers" },
	};
	const Choice* Choices(int* n) { if (n) *n = (int)(sizeof(kChoices) / sizeof(kChoices[0])); return kChoices; }
	int ChoiceIndex(const std::string& name) {
		int n = 0; Choices(&n);
		for (int i = 0; i < n; i++) if (_stricmp(kChoices[i].name, name.c_str()) == 0) return i;
		return -1;
	}
	const Entry* Def::Find(uint8_t r, uint8_t g, uint8_t b) const {
		for (const Entry& x : e) if (x.r == r && x.g == g && x.b == b) return &x;
		return nullptr;
	}

	std::string PathFor(const std::string& cub) {
		size_t d = cub.find_last_of('.'), s = cub.find_last_of("\\/");
		return (d != std::string::npos && (s == std::string::npos || d > s) ? cub.substr(0, d) : cub) + ".funk";
	}
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	static bool ParseRGB(const std::string& v, uint8_t* r, uint8_t* g, uint8_t* b) {
		int a1 = -1, a2 = -1, a3 = -1;
		if (sscanf_s(v.c_str(), "%d,%d,%d", &a1, &a2, &a3) != 3) return false;
		if (a1 < 0 || a1 > 255 || a2 < 0 || a2 > 255 || a3 < 0 || a3 > 255) return false;
		*r = (uint8_t)a1; *g = (uint8_t)a2; *b = (uint8_t)a3; return true;
	}
	bool Load(const std::string& cub, Def& out) {
		out.e.clear();
		std::ifstream f(PathFor(cub)); if (!f) return false;
		std::string line;
		while (std::getline(f, line)) {
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			uint8_t r, g, b;
			if (k == "funktion") {
				size_t sc = v.find(';'); if (sc == std::string::npos) continue;
				if (!ParseRGB(v.substr(0, sc), &r, &g, &b)) continue;
				std::string fn = Trim(v.substr(sc + 1));
				if (ChoiceIndex(fn) < 0) continue;
				Entry* ex = nullptr; for (Entry& x : out.e) if (x.r == r && x.g == g && x.b == b) ex = &x;
				if (ex) ex->fn = fn; else { Entry x; x.r = r; x.g = g; x.b = b; x.fn = fn; out.e.push_back(x); }
			} else if (k == "leuchtend") {
				if (!ParseRGB(v, &r, &g, &b)) continue;
				Entry* ex = nullptr; for (Entry& x : out.e) if (x.r == r && x.g == g && x.b == b) ex = &x;
				if (ex) ex->glow = true; else { Entry x; x.r = r; x.g = g; x.b = b; x.glow = true; out.e.push_back(x); }
			}
		}
		return true;
	}
	bool Save(const std::string& cub, const Def& d, std::string& err) {
		std::string p = PathFor(cub);
		if (!d.Any()) { DeleteFileA(p.c_str()); return true; }
		std::ofstream f(p); if (!f) { err = "Datei nicht schreibbar: " + p; return false; }
		f << "# Gebaeude-Funktionen (LAB -> Bauen -> Funktionen)\n"
		     "# funktion=R,G,B;Name  -> Voxel dieser Farbe werden beim Setzen zur Funktion (Objekt oder Bewohner)\n"
		     "# leuchtend=R,G,B      -> Voxel dieser Farbe werden Lichtbloecke (Blocktyp 0x0D)\n";
		for (const Entry& x : d.e) {
			if (!x.fn.empty()) f << "funktion=" << (int)x.r << "," << (int)x.g << "," << (int)x.b << ";" << x.fn << "\n";
			if (x.glow) f << "leuchtend=" << (int)x.r << "," << (int)x.g << "," << (int)x.b << "\n";
		}
		return true;
	}

	void Analyze(const Lab::Structure& s, const Def& d, std::vector<GlowVoxel>& glow, std::vector<Group>& groups) {
		glow.clear(); groups.clear();
		if (!s.Valid() || !d.Any()) return;
		std::vector<int> fnOf(d.e.size(), -1);
		for (size_t i = 0; i < d.e.size(); i++) if (!d.e[i].fn.empty()) fnOf[i] = ChoiceIndex(d.e[i].fn);
		std::vector<int> mark((size_t)s.sx * s.sy * s.sz, -1);                     // Eintrag je Voxel (Funktion)
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			uint8_t r, g, b;
			if (!s.Get(x, y, z, r, g, b)) continue;
			for (size_t i = 0; i < d.e.size(); i++) {
				const Entry& e = d.e[i];
				if (e.r != r || e.g != g || e.b != b) continue;
				if (e.glow && fnOf[i] < 0) glow.push_back({ x, y, z, r, g, b });
				if (fnOf[i] >= 0) mark[((size_t)z * s.sy + y) * s.sx + x] = (int)i;
				break;
			}
		}
		// Gruppen: 6er-Nachbarschaft gleicher Eintrag (Flutfuellung)
		std::vector<int> stack;
		for (size_t start = 0; start < mark.size(); start++) {
			if (mark[start] < 0) continue;
			int ent = mark[start];
			int minx = INT_MAX, maxx = INT_MIN, miny = INT_MAX, maxy = INT_MIN, minz = INT_MAX, n = 0;
			stack.clear(); stack.push_back((int)start); mark[start] = -2;
			while (!stack.empty()) {
				int i = stack.back(); stack.pop_back(); n++;
				int x = i % s.sx, y = (i / s.sx) % s.sy, z = i / (s.sx * s.sy);
				minx = (std::min)(minx, x); maxx = (std::max)(maxx, x); miny = (std::min)(miny, y); maxy = (std::max)(maxy, y); minz = (std::min)(minz, z);
				const int nb[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				for (const auto& o : nb) {
					int X = x + o[0], Y = y + o[1], Z = z + o[2];
					if (!s.In(X, Y, Z)) continue;
					size_t j = ((size_t)Z * s.sy + Y) * s.sx + X;
					if (mark[j] == ent) { mark[j] = -2; stack.push_back((int)j); }
				}
			}
			groups.push_back({ fnOf[ent], (minx + maxx + 1) / 2, (miny + maxy + 1) / 2, minz, n });
			// Leuchtende Funktionsfarbe: Funktionsvoxel werden ohnehin entfernt -> kein Lichtblock
		}
	}

	void ModelToWorld(int rot, int sx, int sy, int mx, int my, int ox, int oy, int* wx, int* wy) {
		switch (rot & 3) {
			case 0: *wx = ox + mx; *wy = oy + my; break;
			case 1: *wx = ox + my; *wy = oy + (sx - mx) - 1; break;
			case 2: *wx = ox + (sx - mx) - 1; *wy = oy + (sy - my) - 1; break;
			default: *wx = ox + (sy - my) - 1; *wy = oy + mx; break;
		}
	}
}
