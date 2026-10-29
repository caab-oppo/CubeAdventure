// LabStructure.cpp -- Datenmodell + Dateiformat des LAB. Siehe LabStructure.h.
//
// Diese Datei kennt das Spiel NICHT (keine cwsdk-Includes, kein __try). Sie ist
// damit auch ausserhalb von Cube.exe testbar und sicher aus jedem Thread nutzbar.
#include "LabStructure.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

namespace Lab {

	// Obergrenzen: verhindern, dass eine kaputte Datei oder ein Skalierfaktor
	// Gigabytes anfordert (256/256/512 wie in RE_EigeneStrukturen.md).
	static const int kMaxXY = 256, kMaxZ = 512;

	// =====================================================================
	// Structure
	// =====================================================================
	bool Structure::Get(int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b) const {
		if (!In(x, y, z) || !Valid()) return false;
		size_t i = Idx(x, y, z);
		r = rgb[i]; g = rgb[i + 1]; b = rgb[i + 2];
		return (r | g | b) != 0;                       // (0,0,0) = leer, wie nativ
	}

	bool Structure::Solid(int x, int y, int z) const {
		uint8_t r, g, b; return Get(x, y, z, r, g, b);
	}

	void Structure::Set(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b) {
		if (!In(x, y, z) || !Valid()) return;
		// Reines Schwarz ist im nativen Format "leer". Wer Schwarz malen will,
		// bekommt das dunkelste darstellbare Grau statt eines Lochs.
		if ((r | g | b) == 0) { r = g = b = 1; }
		size_t i = Idx(x, y, z);
		rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
	}

	void Structure::Clear(int x, int y, int z) {
		if (!In(x, y, z) || !Valid()) return;
		size_t i = Idx(x, y, z);
		rgb[i] = rgb[i + 1] = rgb[i + 2] = 0;
	}

	void Structure::Create(int nsx, int nsy, int nsz) {
		nsx = std::max(nsx, 1); nsy = std::max(nsy, 1); nsz = std::max(nsz, 1);
		sx = nsx; sy = nsy; sz = nsz;
		rgb.assign((size_t)sx * sy * sz * 3, 0);
	}

	void Structure::Resize(int nsx, int nsy, int nsz) {
		nsx = std::max(nsx, 1); nsy = std::max(nsy, 1); nsz = std::max(nsz, 1);
		Structure n; n.Create(nsx, nsy, nsz);
		for (int z = 0; z < std::min(sz, nsz); z++)
			for (int y = 0; y < std::min(sy, nsy); y++)
				for (int x = 0; x < std::min(sx, nsx); x++) {
					size_t a = Idx(x, y, z), b = n.Idx(x, y, z);
					n.rgb[b] = rgb[a]; n.rgb[b + 1] = rgb[a + 1]; n.rgb[b + 2] = rgb[a + 2];
				}
		sx = nsx; sy = nsy; sz = nsz; rgb.swap(n.rgb);
	}

	void Structure::Shift(int dx, int dy, int dz) {
		if (!Valid()) return;
		std::vector<uint8_t> n(rgb.size(), 0);
		for (int z = 0; z < sz; z++) for (int y = 0; y < sy; y++) for (int x = 0; x < sx; x++) {
			int tx = x + dx, ty = y + dy, tz = z + dz;
			if (!In(tx, ty, tz)) continue;
			size_t a = Idx(x, y, z), b = Idx(tx, ty, tz);
			n[b] = rgb[a]; n[b + 1] = rgb[a + 1]; n[b + 2] = rgb[a + 2];
		}
		rgb.swap(n);
	}

	void Structure::RotateZ90() {
		if (!Valid()) return;
		// (x,y) -> (sy-1-y, x): neue Breite = alte Tiefe.
		Structure n; n.Create(sy, sx, sz);
		for (int z = 0; z < sz; z++) for (int y = 0; y < sy; y++) for (int x = 0; x < sx; x++) {
			size_t a = Idx(x, y, z), b = n.Idx(sy - 1 - y, x, z);
			n.rgb[b] = rgb[a]; n.rgb[b + 1] = rgb[a + 1]; n.rgb[b + 2] = rgb[a + 2];
		}
		sx = n.sx; sy = n.sy; rgb.swap(n.rgb);
	}

	void Structure::MirrorX() {
		if (!Valid()) return;
		for (int z = 0; z < sz; z++) for (int y = 0; y < sy; y++) for (int x = 0; x < sx / 2; x++) {
			size_t a = Idx(x, y, z), b = Idx(sx - 1 - x, y, z);
			for (int k = 0; k < 3; k++) std::swap(rgb[a + k], rgb[b + k]);
		}
	}

	bool Structure::CropToContent() {
		if (!Valid()) return false;
		int x0 = sx, y0 = sy, z0 = sz, x1 = -1, y1 = -1, z1 = -1;
		for (int z = 0; z < sz; z++) for (int y = 0; y < sy; y++) for (int x = 0; x < sx; x++) {
			if (!Solid(x, y, z)) continue;
			x0 = std::min(x0, x); y0 = std::min(y0, y); z0 = std::min(z0, z);
			x1 = std::max(x1, x); y1 = std::max(y1, y); z1 = std::max(z1, z);
		}
		if (x1 < 0) return false;                       // komplett leer
		Structure n; n.Create(x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1);
		for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
			size_t a = Idx(x, y, z), b = n.Idx(x - x0, y - y0, z - z0);
			n.rgb[b] = rgb[a]; n.rgb[b + 1] = rgb[a + 1]; n.rgb[b + 2] = rgb[a + 2];
		}
		sx = n.sx; sy = n.sy; sz = n.sz; rgb.swap(n.rgb);
		return true;
	}

	bool Structure::Scale(float fx, float fy, float fz) {
		if (!Valid() || fx <= 0 || fy <= 0 || fz <= 0) return false;
		auto dim = [](int a, float f) { int v = (int)(a * f + 0.5f); return v < 1 ? 1 : v; };
		int nx = dim(sx, fx), ny = dim(sy, fy), nz = dim(sz, fz);
		if (nx > kMaxXY || ny > kMaxXY || nz > kMaxZ) return false;
		Structure n; n.Create(nx, ny, nz);
		for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) {
			// Mittelpunkt der neuen Zelle zurueck in alte Koordinaten
			int ox = (int)((x + 0.5f) * sx / nx), oy = (int)((y + 0.5f) * sy / ny), oz = (int)((z + 0.5f) * sz / nz);
			if (ox >= sx) ox = sx - 1; if (oy >= sy) oy = sy - 1; if (oz >= sz) oz = sz - 1;
			size_t a = Idx(ox, oy, oz), b = n.Idx(x, y, z);
			n.rgb[b] = rgb[a]; n.rgb[b + 1] = rgb[a + 1]; n.rgb[b + 2] = rgb[a + 2];
		}
		sx = nx; sy = ny; sz = nz; rgb.swap(n.rgb);
		return true;
	}

	int Structure::CountSolid() const {
		if (!Valid()) return 0;
		int n = 0;
		for (size_t i = 0; i < rgb.size(); i += 3) if ((rgb[i] | rgb[i + 1] | rgb[i + 2]) != 0) n++;
		return n;
	}

	// =====================================================================
	// Ordner
	// =====================================================================
	// DER EINZIGE Strukturen-Ordner: <Spielordner>\Strukturen (Arbeitsordner von Cube.exe). ALLE Unterordner
	// (Biome, Zonenmodelle, Rezepte, Gegenstaende, Mobs, Skills, Stadt, Export) werden hieraus gebildet.
	std::string StructuresDir() { return "Strukturen"; }
	std::string ExportDir() { return StructuresDir() + "\\Export"; }

	bool EnsureDir(const std::string& dir) {
#ifdef _WIN32
		// Jede Ebene einzeln anlegen ("Strukturen", dann "Strukturen\\Export").
		std::string cur;
		for (size_t i = 0; i <= dir.size(); i++) {
			if (i == dir.size() || dir[i] == '\\' || dir[i] == '/') {
				if (!cur.empty()) CreateDirectoryA(cur.c_str(), NULL);
			}
			if (i < dir.size()) cur += dir[i];
		}
		DWORD a = GetFileAttributesA(dir.c_str());
		return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
		std::string d = dir; std::replace(d.begin(), d.end(), '\\', '/');
		std::string cur;
		for (size_t i = 0; i <= d.size(); i++) {
			if (i == d.size() || d[i] == '/') { if (!cur.empty()) mkdir(cur.c_str(), 0755); }
			if (i < d.size()) cur += d[i];
		}
		return true;
#endif
	}

	std::vector<std::string> ListCubFiles(const std::string& dir) {
		std::vector<std::string> out;
#ifdef _WIN32
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\*.cub").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return out;
		do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); }
		while (FindNextFileA(h, &fd));
		FindClose(h);
#else
		std::string d = dir; std::replace(d.begin(), d.end(), '\\', '/');
		DIR* dp = opendir(d.c_str());
		if (!dp) return out;
		while (dirent* e = readdir(dp)) {
			std::string n = e->d_name;
			if (n.size() > 4 && n.substr(n.size() - 4) == ".cub") out.push_back(n);
		}
		closedir(dp);
#endif
		std::sort(out.begin(), out.end());
		return out;
	}

	static std::string FixPath(const std::string& p) {
#ifdef _WIN32
		return p;
#else
		std::string d = p; std::replace(d.begin(), d.end(), '\\', '/'); return d;
#endif
	}

	std::string SafeFileName(const std::string& name) {
		std::string s;
		for (char c : name) {
			if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32) s += '_';
			else s += c;
		}
		if (s.empty()) s = "struktur";
		return s;
	}

	// =====================================================================
	// .cub
	// =====================================================================
	// Obergrenzen: Die groessten nativen Modelle liegen bei ~40x40 Grundflaeche,
	// Schiffe/Doerfer koennen hoeher sein. 256/256/512 wie in der Skizze des
	// Subagenten -- verhindert, dass eine kaputte Datei Gigabytes anfordert.

	bool LoadCub(const std::string& path, Structure& out, std::string& err) {
		FILE* f = fopen(FixPath(path).c_str(), "rb");
		if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		int32_t d[3] = { 0, 0, 0 };
		bool ok = false;
		if (fread(d, 4, 3, f) != 3) err = "Kopf (3x int32) fehlt";
		else if (d[0] <= 0 || d[1] <= 0 || d[2] <= 0 || d[0] > kMaxXY || d[1] > kMaxXY || d[2] > kMaxZ) {
			std::ostringstream o; o << "unplausible Groesse " << d[0] << "x" << d[1] << "x" << d[2]; err = o.str();
		} else {
			size_t n = (size_t)d[0] * d[1] * d[2] * 3;
			out.sx = d[0]; out.sy = d[1]; out.sz = d[2];
			out.rgb.assign(n, 0);
			if (fread(out.rgb.data(), 1, n, f) != n) err = "Datei zu kurz";
			else ok = true;
		}
		fclose(f);
		if (!ok) { out.sx = out.sy = out.sz = 0; out.rgb.clear(); return false; }
		// Name = Dateiname ohne Ordner und Endung
		std::string base = path;
		size_t s = base.find_last_of("\\/"); if (s != std::string::npos) base = base.substr(s + 1);
		if (base.size() > 4 && base.substr(base.size() - 4) == ".cub") base.resize(base.size() - 4);
		out.name = base;
		if (out.origin.empty()) out.origin = "datei";
		return true;
	}

	bool SaveCub(const std::string& path, const Structure& s, std::string& err) {
		if (!s.Valid()) { err = "Struktur ist leer/ungueltig"; return false; }
		FILE* f = fopen(FixPath(path).c_str(), "wb");
		if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		int32_t d[3] = { s.sx, s.sy, s.sz };
		bool ok = fwrite(d, 4, 3, f) == 3 && fwrite(s.rgb.data(), 1, s.rgb.size(), f) == s.rgb.size();
		fclose(f);
		if (!ok) err = "Schreibfehler";
		return ok;
	}

	static std::string MetaPathFor(const std::string& cubPath) {
		std::string p = cubPath;
		if (p.size() > 4 && p.substr(p.size() - 4) == ".cub") p.resize(p.size() - 4);
		return p + ".txt";
	}

	void LoadMeta(const std::string& cubPath, Structure& s) {
		std::ifstream f(FixPath(MetaPathFor(cubPath)));
		if (!f) return;
		std::string line;
		while (std::getline(f, line)) {
			size_t eq = line.find('=');
			if (eq == std::string::npos || line[0] == '#') continue;
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "typ")       s.typ = (int)strtol(v.c_str(), nullptr, 0);
			else if (k == "fundament") s.fundament = atoi(v.c_str());
			else if (k == "herkunft")  s.origin = v;
		}
	}

	bool SaveMeta(const std::string& cubPath, const Structure& s) {
		std::ofstream f(FixPath(MetaPathFor(cubPath)));
		if (!f) return false;
		f << "# Beiwerte zu " << s.name << ".cub (LAB). Nicht Teil des nativen Formats.\n";
		f << "# typ = placeStructure param_5 (0 = Markerlogik aus, Marker ersetzt der Mod)\n";
		f << "typ=0x" << std::hex << s.typ << std::dec << "\n";
		f << "fundament=" << s.fundament << "\n";
		f << "herkunft=" << s.origin << "\n";
		return (bool)f;
	}

	// =====================================================================
	// HausBloecke.txt
	// =====================================================================
	// Format (MountainVillage::ScanBlocksAtPlayer):
	//   === Blockscan N  Spieler Block(px,py,pz)  Radius r  z a..b ===
	//   dx<TAB>dy<TAB>dz<TAB>R<TAB>G<TAB>B<TAB>Typ
	//   ...
	//   # Kommentarzeilen
	int ImportBlockScans(const std::string& path, std::vector<Structure>& out, std::string& err) {
		std::ifstream f(FixPath(path));
		if (!f) { err = "keine " + path; return 0; }
		struct V { int x, y, z; uint8_t r, g, b; };
		std::vector<V> cur; std::string title; int found = 0;

		auto flush = [&]() {
			if (cur.empty()) return;
			int x0 = cur[0].x, y0 = cur[0].y, z0 = cur[0].z, x1 = x0, y1 = y0, z1 = z0;
			for (const V& v : cur) {
				x0 = std::min(x0, v.x); y0 = std::min(y0, v.y); z0 = std::min(z0, v.z);
				x1 = std::max(x1, v.x); y1 = std::max(y1, v.y); z1 = std::max(z1, v.z);
			}
			Structure s; s.Create(x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1);
			if (s.sx > kMaxXY || s.sy > kMaxXY || s.sz > kMaxZ) { cur.clear(); return; }
			for (const V& v : cur) s.Set(v.x - x0, v.y - y0, v.z - z0, v.r, v.g, v.b);
			s.name = title.empty() ? "Scan" : title;
			s.origin = "scan (HausBloecke.txt)";
			out.push_back(s); found++;
			cur.clear();
		};

		std::string line;
		while (std::getline(f, line)) {
			if (line.compare(0, 3, "===") == 0) {
				flush();
				int n = 0;
				if (sscanf(line.c_str(), "=== Blockscan %d", &n) == 1) { std::ostringstream o; o << "Scan " << n; title = o.str(); }
				else title = "Scan";
				continue;
			}
			if (line.empty() || line[0] == '#' || line[0] == 'd') continue;   // Kommentar / Kopfzeile "dx dy ..."
			int x, y, z, r, g, b, t;
			if (sscanf(line.c_str(), "%d %d %d %d %d %d %d", &x, &y, &z, &r, &g, &b, &t) == 7)
				cur.push_back({ x, y, z, (uint8_t)r, (uint8_t)g, (uint8_t)b });
		}
		flush();
		return found;
	}
}
