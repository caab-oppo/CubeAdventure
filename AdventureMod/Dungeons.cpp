#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Dungeons.h"
#include "LabStructure.h"
#include "LabBridge.h"
#include "MobLab.h"
#include "BiomeMod.h"
#include "City.h"
#include "Host.h"
#include "Tracking.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <atomic>
#include <map>
#include <cmath>
#include <climits>
#include <cstring>

// Dungeon-Baukasten, Format 2 (2026-10-27): Konstruktionselemente mit Anschluessen.
// Herleitung der Spiel-Adressen: Dungeons.h und Projekt\RE\RE_Dungeons.md (Abschnitt 6/7).
//
// Ein Dungeon = Ordner Strukturen\Dungeons\<Name>\ mit dungeon.txt (format=2) und Module\*.cub + *.txt.
// Jedes Modul hat Anschluesse "anschluss=typ,richtung,u,z": Oeffnung vom Typ (Breite/Hoehe/Wandstaerke aus
// dungeon.txt "typ=name,breite,hoehe,wandstaerke,r,g,b") in der Wand 'richtung' (N/O/S/W), ab u (kleinste x- bzw.
// y-Koordinate der Oeffnung) und ab Hoehe z (Boden-Oberkante). Der Generator setzt ab dem Eingangsmodul Modul an
// Modul (gedreht, Anschluss an Anschluss, keine Ueberlappung), bis die Zielgroesse erreicht ist, haengt den
// Boss-Raum an den tiefsten offenen Anschluss und mauert alle offenen Anschluesse zu. Samen = Zone + Welt
// -> je Ort immer gleich, aber jeder Dungeon anders. Der ganze Bau liegt in EINER Zone.
namespace Dungeons {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	static const uint32_t A_VEC_PUSH = 0x66ADD0;       // std::vector<T*>::push_back, thiscall(vec*, const T**)
	static const uint32_t A_VASE_LOOT = 0x5C9F90;      // cdecl(Spawn+0xF6C, level, zone+0x84)   (Vase 0x5D2670)
	static const uint32_t A_ITEM_GEN = 0x5F7A60;       // thiscall(World, Item* out, level, staerke), ret 0xC (Truhe 0x5D3154)
	static const uint32_t A_ADD_INV = 0x46EBE0;        // thiscall(Inventar*, Item*, platz)
	static const uint32_t A_INV_RESIZE = 0x487380;     // thiscall(Inventar*, n) -- Reiter anlegen (Truhe: 5)

	enum { DIR_N = 0, DIR_E = 1, DIR_S = 2, DIR_W = 3 };
	enum { K_ROOM = 0, K_ENTRY, K_BOSS };
	// Lab::Structure::RotateZ90: (x,y) -> (sy-1-y, x)  =>  Richtung (dx,dy) -> (-dy,dx): N->W, O->N, S->O, W->S
	static int RotDir(int d) { return (d + 3) & 3; }
	static int Opp(int d) { return (d + 2) & 3; }

	struct TypeDef { std::string name; int w = 7, h = 10, t = 2; uint8_t col[3] = { 120, 120, 124 }; };
	struct ConnDef { int type, dir, u, z; };
	struct ModDef {
		std::string file; Lab::Structure s; int kind = K_ROOM; int weight = 10; int maxN = 99; bool roof = false;
		int level = 0;                       // ebene=: 0 ueberall, 1 nur Grundebene, 2 nur obere Ebenen
		int minN = 0;                        // pflicht=: so oft mindestens auf der Grundebene (z. B. Treppenraum)
		std::string aboveName; int above = -1;   // oben=<Modul>: dieses Modul kommt beim Aufstocken darueber (sonst eine Kopie)
		std::vector<ConnDef> conns;
	};
	struct Def {
		std::string name, dir; bool enabled = true;
		float share = 1.0f;                  // % der geeigneten Zonen
		std::string area = "ueberall";       // ueberall | vanilla | biom:<Name>
		int ground = 0;                      // 0 egal, sonst Blocktyp (4 Gras, 9 Sand, 10 Schnee)
		int minMods = 12, maxMods = 24;      // Zahl der Module (ohne Boss)
		int minW = 60, minD = 60;            // Mindestgroesse der Grundflaeche
		int maxW = 236, maxD = 236;          // hoechstens (muss in eine Zone passen)
		int maxZ = 28;                       // hoechstes Modul-z (Ebenen)
		int upMin = 0, upMax = 0;            // obere=min,max: Module auf den oberen Ebenen (nur ueber Treppen erreichbar)
		int colorMode = 0;                   // biomfarben=1: Wand/Boden/Decke nach Biom (Schluesselfarben farbe_wand/boden/decke)
		uint8_t keyWall[3] = { 0, 0, 0 }, keyFloor[3] = { 0, 0, 0 }, keyCeil[3] = { 0, 0, 0 };
		int tries = 16;                      // Versuche, bis die Mindestgroesse erreicht ist
		int monsterPct = 100;                // % der Monster-Marker, die besetzt werden
		int outsideType = -1;                // Typ "aussen" (Eingang nach draussen, nie zugemauert)
		std::vector<TypeDef> types;
		std::vector<std::string> monsters, bosses;
		int lvOff = 0, bossLvOff = 3, uneven = 12, foundation = 1, monsterPer = 1, battlements = 0;
		uint8_t battleCol[3] = { 120, 120, 124 };
		std::vector<ModDef> mods;
		uint32_t salt = 0;
		// lage=berg (2026-10-27): in einen Berg gegraben -- Gelaende ueber dem Grundriss mind. felsdecke Bloecke ueber dem
		// hoechsten Raum (an berg_anteil % der Messpunkte), Eingang am Hangfuss, davor ein Stollen bis ins Freie.
		bool mountain = false; int cover = 4, mountainPct = 75;
	};
	struct PreMob { std::string path; MobLab::CustomMob m; int ids[8]; bool ok = false; };
	static std::vector<Def>* volatile g_defs = nullptr;                       // alte Saetze nie freigeben
	static std::atomic<std::vector<PreMob>*> g_mobs{ nullptr };
	static volatile LONG c_placed = 0, c_spawns = 0, c_chests = 0, c_lights = 0, c_tries = 0, c_layoutFail = 0;

	std::string Dir() { return Lab::StructuresDir() + "\\Dungeons"; }
	static uint32_t Hash(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
		uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ c * 0xC2B2AE35u ^ d * 0x27D4EB2Fu;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h;
	}
	struct Rng { uint32_t s; uint32_t Next() { s = s * 1664525u + 1013904223u; return Hash(s, 0x51u, 0x7u, 0x3u); } int Range(int n) { return n > 0 ? (int)(Next() % (uint32_t)n) : 0; } };

	// ---- Laden ----------------------------------------------------------------------------------------
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	static std::vector<std::string> Split(const std::string& s, char c) { std::vector<std::string> o; std::string cur; for (char ch : s) { if (ch == c) { o.push_back(Trim(cur)); cur.clear(); } else cur += ch; } o.push_back(Trim(cur)); return o; }
	typedef std::vector<std::pair<std::string, std::string>> Lines;           // Schluessel duerfen mehrfach vorkommen
	static Lines ReadLines(const std::string& path) {
		Lines kv; std::ifstream f(path); std::string line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t e = line.find('='); if (e == std::string::npos) continue;
			kv.push_back(std::make_pair(Trim(line.substr(0, e)), Trim(line.substr(e + 1))));
		}
		return kv;
	}
	static int ParseDir(std::string t) {
		for (char& ch : t) ch = (char)tolower((unsigned char)ch);
		if (t == "n" || t == "nord") return DIR_N;
		if (t == "o" || t == "e" || t == "ost") return DIR_E;
		if (t == "s" || t == "sued") return DIR_S;
		if (t == "w" || t == "west") return DIR_W;
		return -1;
	}
	static int TypeIndex(const Def& d, const std::string& n) { for (size_t i = 0; i < d.types.size(); i++) if (_stricmp(d.types[i].name.c_str(), n.c_str()) == 0) return (int)i; return -1; }
	static bool LoadDef(const std::string& dir, Def& d, std::string& log) {
		Lines kv = ReadLines(dir + "\\dungeon.txt");
		if (kv.empty()) return false;
		d.dir = dir; d.name = dir.substr(dir.find_last_of('\\') + 1);
		int format = 1; std::string outsideName = "aussen";
		for (auto& p : kv) {
			const std::string& k = p.first; const std::string& v = p.second;
			auto I = [&](int def) { return v.empty() ? def : atoi(v.c_str()); };
			auto Pair = [&](int* a, int* b) { std::vector<std::string> q = Split(v, ','); if (q.size() >= 1) *a = atoi(q[0].c_str()); if (q.size() >= 2) *b = atoi(q[1].c_str()); };
			if (k == "format") format = I(1);
			else if (k == "name") d.name = v;
			else if (k == "aktiv") d.enabled = I(1) != 0;
			else if (k == "anteil") d.share = (float)atof(v.c_str());
			else if (k == "gebiet") d.area = v;
			else if (k == "boden") d.ground = I(0);
			else if (k == "module") Pair(&d.minMods, &d.maxMods);
			else if (k == "flaeche_min") Pair(&d.minW, &d.minD);
			else if (k == "flaeche_max") Pair(&d.maxW, &d.maxD);
			else if (k == "hoehe_max") d.maxZ = std::max(0, I(28));
			else if (k == "obere") Pair(&d.upMin, &d.upMax);
			else if (k == "biomfarben") d.colorMode = I(0);
			else if (k == "farbe_wand" || k == "farbe_boden" || k == "farbe_decke") {
				uint8_t* t = k == "farbe_wand" ? d.keyWall : k == "farbe_boden" ? d.keyFloor : d.keyCeil;
				std::vector<std::string> q = Split(v, ','); for (int i = 0; i < 3 && i < (int)q.size(); i++) t[i] = (uint8_t)atoi(q[i].c_str());
			}
			else if (k == "versuche") d.tries = std::max(1, std::min(64, I(16)));
			else if (k == "monster_anteil") d.monsterPct = std::max(0, std::min(100, I(100)));
			else if (k == "typ") {
				std::vector<std::string> q = Split(v, ',');
				TypeDef t; t.name = q[0];
				if (q.size() > 1) t.w = std::max(1, atoi(q[1].c_str()));
				if (q.size() > 2) t.h = std::max(1, atoi(q[2].c_str()));
				if (q.size() > 3) t.t = std::max(1, atoi(q[3].c_str()));
				for (int i = 0; i < 3; i++) if (q.size() > (size_t)(4 + i)) t.col[i] = (uint8_t)std::max(1, std::min(255, atoi(q[4 + i].c_str())));
				d.types.push_back(t);
			}
			else if (k == "aussen") outsideName = v;
			else if (k == "bewohner") { for (const std::string& s : Split(v, ',')) if (!s.empty()) d.monsters.push_back(s); }
			else if (k == "boss") { for (const std::string& s : Split(v, ',')) if (!s.empty()) d.bosses.push_back(s); }
			else if (k == "level") d.lvOff = I(0);
			else if (k == "boss_level") d.bossLvOff = I(3);
			else if (k == "uneben") d.uneven = std::max(0, I(12));
			else if (k == "fundament") d.foundation = I(1) ? 1 : 0;
			else if (k == "lage") d.mountain = _stricmp(v.c_str(), "berg") == 0;
			else if (k == "felsdecke") d.cover = std::max(0, std::min(40, I(4)));
			else if (k == "berg_anteil") d.mountainPct = std::max(10, std::min(100, I(75)));
			else if (k == "monster_je_marker") d.monsterPer = std::max(1, std::min(4, I(1)));
			else if (k == "zinnen") {
				std::vector<std::string> q = Split(v, ',');
				d.battlements = atoi(q[0].c_str()) ? 1 : 0;
				for (int i = 0; i < 3; i++) if (q.size() > (size_t)(1 + i)) d.battleCol[i] = (uint8_t)std::max(1, std::min(255, atoi(q[1 + i].c_str())));
			}
		}
		if (format != 2) { log += d.name + ": altes Format (Kammerraster) -- uebersprungen. "; return false; }
		d.outsideType = TypeIndex(d, outsideName);
		d.maxW = std::min(d.maxW, 240); d.maxD = std::min(d.maxD, 240);
		if (d.maxMods < d.minMods) d.maxMods = d.minMods;
		if (d.upMax < d.upMin) d.upMax = d.upMin;
		WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\Module\\*.cub").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				ModDef m; m.file = fd.cFileName; std::string err, p = dir + "\\Module\\" + m.file;
				if (!Lab::LoadCub(p, m.s, err)) { log += m.file + ": " + err + " "; continue; }
				Lines mk = ReadLines(p.substr(0, p.size() - 4) + ".txt");
				bool bad = false;
				for (auto& q : mk) {
					const std::string& k = q.first; const std::string& v = q.second;
					if (k == "art") m.kind = v == "eingang" ? K_ENTRY : v == "boss" ? K_BOSS : K_ROOM;
					else if (k == "gewicht") m.weight = std::max(1, atoi(v.c_str()));
					else if (k == "max") m.maxN = std::max(0, atoi(v.c_str()));
					else if (k == "ebene") m.level = v == "boden" ? 1 : v == "oben" ? 2 : 0;
					else if (k == "pflicht") m.minN = std::max(0, atoi(v.c_str()));
					else if (k == "oben") m.aboveName = v;
					else if (k == "dach") m.roof = atoi(v.c_str()) != 0;
					else if (k == "anschluss") {
						std::vector<std::string> a = Split(v, ',');
						if (a.size() < 4) { bad = true; continue; }
						ConnDef c; c.type = TypeIndex(d, a[0]); c.dir = ParseDir(a[1]); c.u = atoi(a[2].c_str()); c.z = atoi(a[3].c_str());
						if (c.type < 0 || c.dir < 0) { log += m.file + ": Anschluss '" + v + "' unbekannt. "; bad = true; continue; }
						const TypeDef& t = d.types[c.type];
						int face = (c.dir == DIR_N || c.dir == DIR_S) ? m.s.sx : m.s.sy;
						if (c.u < 0 || c.u + t.w > face || c.z < 0 || c.z + t.h > m.s.sz) { log += m.file + ": Anschluss '" + v + "' liegt ausserhalb. "; bad = true; continue; }
						m.conns.push_back(c);
					}
				}
				if (bad || m.conns.empty()) { log += m.file + ": ohne gueltige Anschluesse -- uebersprungen. "; continue; }
				d.mods.push_back(m);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		for (ModDef& m : d.mods) if (!m.aboveName.empty()) {                     // oben= aufloesen: gleiche Grundflaeche Pflicht
			for (size_t j = 0; j < d.mods.size(); j++) if (_stricmp((d.mods[j].file.substr(0, d.mods[j].file.size() - 4)).c_str(), m.aboveName.c_str()) == 0) m.above = (int)j;
			if (m.above < 0) log += m.file + ": oben=" + m.aboveName + " nicht gefunden. ";
			else if (d.mods[m.above].s.sx != m.s.sx || d.mods[m.above].s.sy != m.s.sy) { log += m.file + ": oben=" + m.aboveName + " hat eine andere Grundflaeche. "; m.above = -1; }
		}
		int nEntry = 0, nBoss = 0; for (const ModDef& m : d.mods) { if (m.kind == K_ENTRY) nEntry++; if (m.kind == K_BOSS) nBoss++; }
		if (!nEntry || !nBoss) { log += d.name + ": braucht mindestens ein Eingangs- und ein Boss-Modul. "; return false; }
		uint32_t s = 2166136261u; for (char c : d.name) s = (s ^ (uint8_t)c) * 16777619u; d.salt = s;
		return true;
	}
	void Reload() {
		std::vector<Def>* v = new std::vector<Def>(); std::string log;
		WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((Dir() + "\\*").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
				Def d; if (LoadDef(Dir() + "\\" + fd.cFileName, d, log)) v->push_back(d);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		g_defs = v; g_mobs.store(nullptr);
		std::cout << "[Dungeons] " << v->size() << " Dungeon(s) geladen (" << Dir() << "). " << log << std::endl;
	}
	std::vector<std::string> Names() { std::vector<std::string> o; std::vector<Def>* v = g_defs; if (v) for (const Def& d : *v) o.push_back(d.name); return o; }

	// ---- eigene Mobs der Bewohnerlisten (Spielthread) ---------------------------------------------------
	static bool IsNum(const std::string& s) { if (s.empty()) return false; for (char c : s) if (c < '0' || c > '9') return false; return true; }
	void Tick(cube::GameController* gc) {
		if (!gc || g_mobs.load()) return;
		std::vector<Def>* v = g_defs; if (!v) return;
		std::vector<PreMob>* pm = new std::vector<PreMob>();
		for (const Def& d : *v) for (const std::vector<std::string>* L : { &d.monsters, &d.bosses }) for (const std::string& w : *L) {
			if (IsNum(w)) continue;
			std::string path = MobLab::MobsDir() + "\\" + w + ".mob";
			bool dup = false; for (const PreMob& p : *pm) if (p.path == path) dup = true;
			if (dup) continue;
			PreMob p; p.path = path; std::string err;
			if (MobLab::LoadMob(path, p.m, err)) { MobLab::PrepareMobParts(gc, p.m, p.ids); if (p.m.ownRace >= 200) MobLab::RegisterRaceName(gc, p.m.ownRace, p.m.raceName.empty() ? p.m.name : p.m.raceName); p.ok = true; }
			else std::cout << "[Dungeons] " << err << std::endl;
			pm->push_back(p);
		}
		g_mobs.store(pm);
	}
	static const PreMob* FindMob(const std::string& w) {
		std::vector<PreMob>* pm = g_mobs.load(); if (!pm) return nullptr;
		std::string path = MobLab::MobsDir() + "\\" + w + ".mob";
		for (const PreMob& p : *pm) if (p.path == path && p.ok) return &p;
		return nullptr;
	}

	// ---- Grundriss aus Modulen ------------------------------------------------------------------------------
	// Anschlusspunkte in doppelten Koordinaten (Blockecken * 2), damit mittige Oeffnungen ganzzahlig bleiben.
	struct RConn { int type, dir, x2, y2, z; };
	static RConn RotConn(const Def& d, const ModDef& m, const ConnDef& c, int rot, int* W, int* D) {
		int sx = m.s.sx, sy = m.s.sy, w = d.types[c.type].w, c2 = 2 * c.u + w, X = 0, Y = 0;
		switch (c.dir) { case DIR_N: X = c2; Y = 2 * sy; break; case DIR_S: X = c2; Y = 0; break; case DIR_E: X = 2 * sx; Y = c2; break; default: X = 0; Y = c2; break; }
		int dir = c.dir;
		for (int k = 0; k < (rot & 3); k++) { int nX = 2 * sy - Y, nY = X; X = nX; Y = nY; std::swap(sx, sy); dir = RotDir(dir); }
		if (W) *W = sx; if (D) *D = sy;
		RConn r; r.type = c.type; r.dir = dir; r.x2 = X; r.y2 = Y; r.z = c.z; return r;
	}
	struct Box { int x0, y0, z0, x1, y1, z1; };                                // halboffen
	static bool Overlap(const Box& a, const Box& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1 && a.z0 < b.z1 && b.z0 < a.z1; }
	struct PMod { int mod, rot, ox, oy, oz, W, D, H, depth; };
	struct OConn { int pm, ci, type, dir, x2, y2, z, state; };                  // state 0 offen, 1 verbunden, 2 aussen, 3 tot
	struct Layout {
		std::vector<PMod> mods; std::vector<OConn> conns; std::vector<Box> keep;
		int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, boss = -1, phase = 0, upper = 0;
		Box BoxOf(int i) const { const PMod& p = mods[i]; return Box{ p.ox, p.oy, p.oz, p.ox + p.W, p.oy + p.D, p.oz + p.H }; }
	};
	// Modul mi (Drehung rot) mit seinem Anschluss ci an den offenen Anschluss 'at' haengen (at < 0: Eingang am Ursprung).
	static bool TryPlace(const Def& d, Layout& L, int mi, int rot, int ci, int at) {
		const ModDef& m = d.mods[mi];
		int W, D; RConn rc = RotConn(d, m, m.conns[ci], rot, &W, &D);
		int ox = 0, oy = 0, oz = 0, depth = 0;
		if (at >= 0) {
			const OConn& t = L.conns[at];
			if (rc.type != t.type || rc.dir != Opp(t.dir)) return false;
			int dx = t.x2 - rc.x2, dy = t.y2 - rc.y2;
			if ((dx & 1) || (dy & 1)) return false;
			ox = dx / 2; oy = dy / 2; oz = t.z - rc.z; depth = L.mods[t.pm].depth + 1;
		}
		if (oz < 0 || oz > d.maxZ) return false;
		if ((m.level == 1 && oz != 0) || (m.level == 2 && oz == 0)) return false;
		if (L.phase == 0 && oz != 0) return false;                             // erst die ganze Grundebene
		Box b{ ox, oy, oz, ox + W, oy + D, oz + m.s.sz };
		for (size_t i = 0; i < L.mods.size(); i++) if (Overlap(b, L.BoxOf((int)i))) return false;
		for (const Box& k : L.keep) if (Overlap(b, k)) return false;
		if (oz > 0) {                                                          // obere Ebene: ganz auf Daechern darunter (Oberkante = oz)
			for (int y = b.y0; y < b.y1; y += (y + 2 < b.y1 ? 2 : 1)) for (int x = b.x0; x < b.x1; x += (x + 2 < b.x1 ? 2 : 1)) {
				bool sup = false;
				for (size_t i = 0; i < L.mods.size() && !sup; i++) { Box q = L.BoxOf((int)i); sup = q.z1 == oz && d.mods[L.mods[i].mod].roof && x >= q.x0 && x < q.x1 && y >= q.y0 && y < q.y1; }
				if (!sup) return false;
			}
		}
		int nx0 = std::min(L.bx0, b.x0), ny0 = std::min(L.by0, b.y0), nx1 = std::max(L.bx1, b.x1), ny1 = std::max(L.by1, b.y1);
		if (L.mods.empty()) { nx0 = b.x0; ny0 = b.y0; nx1 = b.x1; ny1 = b.y1; }
		if (nx1 - nx0 > d.maxW || ny1 - ny0 > d.maxD) return false;
		const int pm = (int)L.mods.size();
		L.mods.push_back(PMod{ mi, rot & 3, ox, oy, oz, W, D, m.s.sz, depth });
		if (oz > 0) L.upper++;
		L.bx0 = nx0; L.by0 = ny0; L.bx1 = nx1; L.by1 = ny1;
		if (at >= 0) L.conns[at].state = 1;
		for (int k = 0; k < (int)m.conns.size(); k++) {
			RConn r = RotConn(d, m, m.conns[k], rot, nullptr, nullptr);
			OConn o{ pm, k, r.type, r.dir, r.x2 + 2 * ox, r.y2 + 2 * oy, r.z + oz, 0 };
			if (k == ci && at >= 0) o.state = 1;
			else for (OConn& e : L.conns)                                        // Ringschluss: trifft genau einen offenen Anschluss
				if (e.state == 0 && e.type == o.type && e.dir == Opp(o.dir) && e.x2 == o.x2 && e.y2 == o.y2 && e.z == o.z) { e.state = 1; o.state = 1; break; }
			L.conns.push_back(o);
		}
		return true;
	}
	// Aufstocken: Modul mi genau ueber das gesetzte Modul g (gleiche Lage, Drehung, Grundflaeche). Anschluesse, die auf
	// Anschluesse der Nachbarn darueber treffen, verbinden sich ueber den Ringschluss in TryPlace-Logik (hier nachgebaut).
	static bool StackOn(const Def& d, Layout& L, int g, int mi) {
		const PMod gp = L.mods[g];
		const ModDef& m = d.mods[mi];
		const int oz = gp.oz + gp.H;
		if (oz > d.maxZ || m.s.sx != d.mods[gp.mod].s.sx || m.s.sy != d.mods[gp.mod].s.sy) return false;
		int W, D; RotConn(d, m, m.conns[0], gp.rot, &W, &D);
		Box b{ gp.ox, gp.oy, oz, gp.ox + W, gp.oy + D, oz + m.s.sz };
		for (size_t i = 0; i < L.mods.size(); i++) if (Overlap(b, L.BoxOf((int)i))) return false;
		const int pm = (int)L.mods.size();
		L.mods.push_back(PMod{ mi, gp.rot, gp.ox, gp.oy, oz, W, D, m.s.sz, gp.depth + 1 });
		L.upper++;
		for (int k = 0; k < (int)m.conns.size(); k++) {
			RConn r = RotConn(d, m, m.conns[k], gp.rot, nullptr, nullptr);
			OConn o{ pm, k, r.type, r.dir, r.x2 + 2 * gp.ox, r.y2 + 2 * gp.oy, r.z + oz, 0 };
			for (OConn& e : L.conns)
				if ((e.state == 0 || e.state == 3) && e.type == o.type && e.dir == Opp(o.dir) && e.x2 == o.x2 && e.y2 == o.y2 && e.z == o.z) { e.state = 1; o.state = 1; break; }
			L.conns.push_back(o);
		}
		return true;
	}
	static bool Stackable(const Def& d, const Layout& L, int i, int levelH) {
		const PMod& p = L.mods[i]; const ModDef& m = d.mods[p.mod];
		return m.kind == K_ROOM && m.roof && m.level != 1 && p.H == levelH;
	}
	struct Cand { int mi, rot, ci, w; };
	static bool AttachAny(const Def& d, Layout& L, int at, int wantKind, std::vector<int>& count, Rng& r) {
		const OConn t = L.conns[at];
		std::vector<Cand> cs; int sum = 0;
		for (int mi = 0; mi < (int)d.mods.size(); mi++) {
			const ModDef& m = d.mods[mi];
			if (m.kind != wantKind || count[mi] >= m.maxN) continue;
			for (int rot = 0; rot < 4; rot++) for (int ci = 0; ci < (int)m.conns.size(); ci++) {
				if (m.conns[ci].type != t.type) continue;
				RConn rc = RotConn(d, m, m.conns[ci], rot, nullptr, nullptr);
				if (rc.dir != Opp(t.dir)) continue;
				cs.push_back(Cand{ mi, rot, ci, m.weight }); sum += m.weight;
			}
		}
		for (int tries = 0; tries < 14 && !cs.empty(); tries++) {
			int q = r.Range(sum), k = 0;
			for (; k < (int)cs.size() - 1; k++) { if (q < cs[k].w) break; q -= cs[k].w; }
			Cand c = cs[k]; sum -= c.w; cs.erase(cs.begin() + k);
			if (TryPlace(d, L, c.mi, c.rot, c.ci, at)) { count[c.mi]++; return true; }
		}
		return false;
	}
	// Ein Versuch: Eingang, Wachstum bis zur Zielzahl, Boss an den tiefsten offenen Anschluss.
	static bool GrowOnce(const Def& d, uint32_t seed, Layout& L) {
		Rng r{ seed };
		std::vector<int> count(d.mods.size(), 0);
		{   // Eingang
			std::vector<int> es; int sum = 0; for (int i = 0; i < (int)d.mods.size(); i++) if (d.mods[i].kind == K_ENTRY) { es.push_back(i); sum += d.mods[i].weight; }
			int q = r.Range(sum), e = es.back(); for (int i : es) { if (q < d.mods[i].weight) { e = i; break; } q -= d.mods[i].weight; }
			if (!TryPlace(d, L, e, r.Range(4), 0, -1)) return false;
			count[e]++;
			for (OConn& o : L.conns) if (o.type == d.outsideType) {             // aussen: offen lassen + Vorplatz freihalten
				o.state = 2;
				const int depth = d.uneven + 8, w = d.types[o.type].w + 6;
				int cx = o.x2 / 2, cy = o.y2 / 2; Box k{};
				switch (o.dir) {
					case DIR_N: k = Box{ cx - w / 2, cy, 0, cx + w / 2 + 1, cy + depth, 999 }; break;
					case DIR_S: k = Box{ cx - w / 2, cy - depth, 0, cx + w / 2 + 1, cy, 999 }; break;
					case DIR_E: k = Box{ cx, cy - w / 2, 0, cx + depth, cy + w / 2 + 1, 999 }; break;
					default:    k = Box{ cx - depth, cy - w / 2, 0, cx, cy + w / 2 + 1, 999 }; break;
				}
				L.keep.push_back(k);
				L.bx0 = std::min(L.bx0, k.x0); L.by0 = std::min(L.by0, k.y0); L.bx1 = std::max(L.bx1, k.x1); L.by1 = std::max(L.by1, k.y1);
			}
		}
		// 1. Grundebene (alle Module bei z = 0); Anschluesse oben (Treppenhaus) bleiben fuer Schritt 3 offen
		const int target = d.minMods + r.Range(d.maxMods - d.minMods + 1);
		L.phase = 0;
		int fails = 0;
		auto OpenAt = [&](bool ground) { std::vector<int> o; for (int i = 0; i < (int)L.conns.size(); i++) if (L.conns[i].state == 0 && ((L.conns[i].z - L.mods[L.conns[i].pm].oz <= 2 && L.mods[L.conns[i].pm].oz == 0) == ground)) o.push_back(i); return o; };
		while ((int)L.mods.size() < target && fails < 400) {
			std::vector<int> open = OpenAt(true);
			if (open.empty()) break;
			int at = open[r.Range((int)open.size())];
			if (!AttachAny(d, L, at, K_ROOM, count, r)) { L.conns[at].state = 3; fails++; }
		}
		// 2. Pflicht-Module (z. B. Treppenhaeuser) an freie oder tote Anschluesse der Grundebene
		for (int mi = 0; mi < (int)d.mods.size(); mi++) {
			const ModDef& m = d.mods[mi];
			for (int guard = 0; count[mi] < m.minN && guard < 60; guard++) {
				std::vector<int> c; for (int i = 0; i < (int)L.conns.size(); i++) if ((L.conns[i].state == 0 || L.conns[i].state == 3) && L.mods[L.conns[i].pm].oz == 0 && L.conns[i].z == L.mods[L.conns[i].pm].oz + 2) c.push_back(i);
				if (c.empty()) break;
				int at = c[r.Range((int)c.size())];
				const OConn t = L.conns[at]; bool ok = false;
				for (int rot = 0; rot < 4 && !ok; rot++) for (int ci = 0; ci < (int)m.conns.size() && !ok; ci++) {
					if (m.conns[ci].type != t.type) continue;
					int st = L.conns[at].state; L.conns[at].state = 0;
					ok = TryPlace(d, L, mi, rot, ci, at);
					if (!ok) L.conns[at].state = st;
				}
				if (ok) count[mi]++;
			}
		}
		// 3. obere Ebenen = Aufstocken: von jedem Treppenraum (Modul mit oben=) aus werden verbundene Module derselben Ebene
		//    genau ueberbaut (gleiche Grundflaeche, liegt also ganz auf). Der Treppenraum bekommt sein oben=-Modul (Treppe kommt
		//    durch das Bodenloch an) -- die Ebenen sind nur ueber Treppen verbunden.
		if (d.upMax > 0) {
			L.phase = 1;
			const int upTarget = d.upMin + r.Range(d.upMax - d.upMin + 1);
			for (int lvl = 0; lvl < 4 && L.upper < upTarget; lvl++) {
				std::vector<int> stairs;
				for (int i = 0; i < (int)L.mods.size(); i++) if (d.mods[L.mods[i].mod].above >= 0 && L.mods[i].oz == lvl * 14) stairs.push_back(i);
				for (int si = 0; si < (int)stairs.size() && L.upper < upTarget; si++) {
					int st = stairs[si];
					const int levelH = L.mods[st].H;
					if (!StackOn(d, L, st, d.mods[L.mods[st].mod].above)) continue;
					// Breitensuche ueber verbundene Nachbarn derselben Ebene
					std::vector<int> q{ st }; std::vector<char> seen(L.mods.size() + 64, 0); seen[st] = 1;
					int perStair = std::max(2, (upTarget - L.upper) / std::max(1, (int)stairs.size() - si) + 1), done = 1;
					for (size_t h = 0; h < q.size() && done < perStair && L.upper < upTarget; h++) {
						int cur = q[h];
						std::vector<int> nb;
						for (const OConn& c : L.conns) {
							if (c.pm != cur || c.state != 1) continue;
							for (const OConn& e : L.conns) if (e.pm != cur && e.state == 1 && e.x2 == c.x2 && e.y2 == c.y2 && e.z == c.z && e.dir == Opp(c.dir)) nb.push_back(e.pm);
						}
						for (int k = (int)nb.size() - 1; k > 0; k--) std::swap(nb[k], nb[r.Range(k + 1)]);
						for (int n : nb) {
							if (n >= (int)seen.size() || seen[n] || L.mods[n].oz != L.mods[st].oz || !Stackable(d, L, n, levelH)) continue;
							seen[n] = 1;
							int mi = d.mods[L.mods[n].mod].above >= 0 ? d.mods[L.mods[n].mod].above : L.mods[n].mod;
							if (StackOn(d, L, n, mi)) { q.push_back(n); done++; if (done >= perStair || L.upper >= upTarget) break; }
						}
					}
				}
			}
		}
		L.phase = 1;
		// Boss: tiefste offene/tote Anschluesse zuerst
		std::vector<int> cand; for (int i = 0; i < (int)L.conns.size(); i++) if (L.conns[i].state == 0 || L.conns[i].state == 3) cand.push_back(i);
		std::stable_sort(cand.begin(), cand.end(), [&](int a, int b) { return L.mods[L.conns[a].pm].depth > L.mods[L.conns[b].pm].depth; });
		for (int at : cand) {
			int st = L.conns[at].state; L.conns[at].state = 0;
			if (AttachAny(d, L, at, K_BOSS, count, r)) { L.boss = (int)L.mods.size() - 1; break; }
			L.conns[at].state = st;
		}
		return L.boss >= 0;
	}
	static bool Generate(const Def& d, uint32_t seed, Layout& out, std::string* why) {
		Layout best; int bestScore = -1;
		for (int a = 0; a < d.tries; a++) {
			Layout L;
			bool boss = GrowOnce(d, Hash(seed, (uint32_t)a, d.salt, 0xB0u), L);
			int W = L.bx1 - L.bx0, D = L.by1 - L.by0, n = (int)L.mods.size() - (boss ? 1 : 0);
			bool big = std::max(W, D) >= std::max(d.minW, d.minD) && std::min(W, D) >= std::min(d.minW, d.minD);
			if (boss && n >= d.minMods && big && L.upper >= d.upMin) { out = L; return true; }
			int score = boss ? n : -1;
			if (score > bestScore) { bestScore = score; best = L; }
		}
		if (why) { char b[160]; sprintf_s(b, "Mindestgroesse in %d Versuchen nicht erreicht (bester: %d Module).", d.tries, bestScore); *why = b; }
		InterlockedIncrement(&c_layoutFail);
		return false;
	}

	// ---- Bauen ---------------------------------------------------------------------------------------------
	struct Mark { int x, y, z, kind; };                                         // 0 Monster 1 Boss 2 Truhe 3 Vase 4 Licht
	static void TakeMarkers(Lab::Structure& s, std::vector<Mark>& out) {
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			uint8_t r, g, b; if (!s.Get(x, y, z, r, g, b)) continue;
			auto is = [&](const uint8_t* m) { return r == m[0] && g == m[1] && b == m[2]; };
			int k = is(kMonster) ? 0 : is(kBoss) ? 1 : is(kChest) ? 2 : is(kVase) ? 3 : is(kLight) ? 4 : -1;
			if (k < 0) continue;
			s.Clear(x, y, z); out.push_back({ x, y, z, k });
		}
	}
	// Offenen Anschluss zumauern: Oeffnung (Breite x Hoehe) ueber die Wandstaerke fuellen, Farbe von der Wand daneben.
	static void Plug(Lab::Structure& s, const TypeDef& t, const RConn& c) {
		const bool ns = c.dir == DIR_N || c.dir == DIR_S;
		const int a0 = ((ns ? c.x2 : c.y2) - t.w) / 2;
		for (int z = c.z; z < c.z + t.h && z < s.sz; z++)
			for (int dp = 0; dp < t.t; dp++) {
				int depthCoord = c.dir == DIR_N ? s.sy - 1 - dp : c.dir == DIR_S ? dp : c.dir == DIR_E ? s.sx - 1 - dp : dp;
				uint8_t col[3] = { t.col[0], t.col[1], t.col[2] };
				{   // Wandfarbe links/rechts der Oeffnung
					uint8_t r, g, b;
					int ax = ns ? a0 - 1 : depthCoord, ay = ns ? depthCoord : a0 - 1, bx = ns ? a0 + t.w : depthCoord, by = ns ? depthCoord : a0 + t.w;
					if (s.Get(ax, ay, z, r, g, b) || s.Get(bx, by, z, r, g, b)) { col[0] = r; col[1] = g; col[2] = b; }
				}
				for (int k = 0; k < t.w; k++) {
					int x = ns ? a0 + k : depthCoord, y = ns ? depthCoord : a0 + k;
					uint8_t r, g, b; if (!s.In(x, y, z) || s.Get(x, y, z, r, g, b)) continue;
					s.Set(x, y, z, col[0], col[1], col[2]);
				}
			}
	}
	// ---- Inhalt: Spawns / Truhe ---------------------------------------------------------------------------
	typedef void(__fastcall* VecPush_t)(void* vec, void* edx, void* const* el);
	static int ZoneLevelRaw(void* zone, int* rare) { __try { *rare = *((uint8_t*)zone + 0x84); return *(int*)((uint8_t*)zone + 0x80); } __except (EXCEPTION_EXECUTE_HANDLER) { *rare = 0; return 1; } }
	static int SpawnCountRaw(void* zone) { __try { uint8_t** b = *(uint8_t***)((uint8_t*)zone + 0x18); uint8_t** e = *(uint8_t***)((uint8_t*)zone + 0x1C); return (b && e >= b) ? (int)(e - b) : -1; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
	static bool PushSpawnRaw(void* zone, uint8_t* sp, int zx, int zy, int idx) {
		__try {
			*(uint64_t*)(sp + 0x48) = ((((uint64_t)(uint32_t)zy << 16) + (uint32_t)zx) << 8) + (uint32_t)idx;   // wie packWorldCoord64 0x5C0120
			((VecPush_t)Re(A_VEC_PUSH))((uint8_t*)zone + 0x18, nullptr, (void* const*)&sp);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void SetSpawnPosRaw(uint8_t* sp, int x, int y, int z, float rot, float leash) {
		__try {
			*(int64_t*)(sp + 0x10) = (int64_t)x * 65536 + 32768; *(int64_t*)(sp + 0x18) = (int64_t)y * 65536 + 32768; *(int64_t*)(sp + 0x20) = (int64_t)z * 65536;
			*(float*)(sp + 0x54) = rot; *(float*)(sp + 0x08) = leash;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void VaseLootRaw(uint8_t* sp, int level, int rare) {
		__try { *(int*)(sp + 0x28) = 6; *(int*)(sp + 0x2C) = 0x8F; *(int*)(sp + 0x34) = level; *(float*)(sp + 0xF58) = 10.0f;
		        ((void(__cdecl*)(void*, int, int))Re(A_VASE_LOOT))(sp + 0xF6C, level, rare); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	typedef void* (__fastcall* ItemGen_t)(const void* world, void* edx, void* out, int level, int strength);
	typedef void(__fastcall* AddInv_t)(void* inv, void* edx, void* item, int slot);
	typedef void(__fastcall* InvResize_t)(void* inv, void* edx, int n);
	static int FillChestRaw(const void* world, void* zone, int level, int rare, uint32_t h) {
		int n = 0;
		__try {
			uint8_t* b = *(uint8_t**)((uint8_t*)zone + 0x0C); uint8_t* e = *(uint8_t**)((uint8_t*)zone + 0x10);
			if (!b || e - b < 0x188) return 0;
			uint8_t* obj = e - 0x188;                                                // eben angehaengte Truhe
			if (*(int*)obj != 0x0A) return 0;
			((InvResize_t)Re(A_INV_RESIZE))(obj + 0x48, nullptr, 5);
			int cnt = 1 + (int)(h % 4u);
			alignas(8) uint8_t it[0x118];
			for (int i = 0; i < cnt; i++) {
				memset(it, 0, sizeof(it));
				void* r = ((ItemGen_t)Re(A_ITEM_GEN))(world, nullptr, it, level, rare + 1);
				((AddInv_t)Re(A_ADD_INV))(obj + 0x48, nullptr, r ? r : it, -1); n++;
			}
			obj[0x30] = 2;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}
	// Bewohner: Zahl = native Rasse, sonst eigener Mob
	static uint8_t* MakeDweller(const std::string& w, int level) {
		if (IsNum(w)) return MobLab::NewSpawnNative(atoi(w.c_str()), 1, level);
		const PreMob* p = FindMob(w);
		if (!p) return nullptr;
		uint8_t* sp = MobLab::NewSpawnNative(p->m.baseRace, 1, level);              // ApplyToSpawn setzt Feindlichkeit/Flags der .mob
		if (sp) { __try { MobLab::ApplyToSpawn(sp, p->m, p->ids, p->m.ownRace); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
		return sp;
	}

	struct Out { const void* world; void* zone; int zx, zy; cube::GameController* gc; };
	static int StampCell(const Out& o, Lab::Structure& s, int x, int y, int z, int fundament) {
		if (o.zone) return LabBridge::StampIntoZone(s, x, y, z, 0, fundament, o.zone);
		return BiomeMod::StampLive(o.gc, s, x, y, z, fundament);
	}
	static void StampLight(const Out& o, int x, int y, int z) {
		Lab::Structure l; l.Create(1, 1, 1); l.Set(0, 0, 0, 255, 226, 150);
		if (o.zone) LabBridge::StampInPlaceFlags(l, x, y, z, 0, 0, 0, o.zone, 0x0D);
		else BiomeMod::StampLive(o.gc, l, x, y, z, 0);                      // LAB-Test: heller Block (Leuchtflag nur bei Zonenerzeugung)
	}
	typedef std::function<bool(int x, int y, int* z)> GroundFn;
	// Farben je Biom (biomfarben=1): eigenes Biom -> seine Dungeon-Farben (Wand/Boden), sonst je nativer Klimaklasse.
	// Einfarbig (kein Muster): jede Schluesselfarbe im Modul wird durch genau eine Farbe ersetzt.
	struct Palette { uint8_t wall[3], floor[3], ceil[3]; };
	static Palette BiomePalette(const void* world, int x, int y) {
		static const uint8_t nat[7][3][3] = {
			{ { 132, 128, 124 }, { 104, 98, 92 }, { 90, 86, 82 } },       // 0 (Ozean/unbekannt): grauer Stein
			{ { 132, 128, 124 }, { 104, 98, 92 }, { 90, 86, 82 } },       // 1 Grasland
			{ { 214, 222, 232 }, { 172, 182, 196 }, { 150, 160, 176 } },  // 2 Schneeland
			{ { 122, 142, 168 }, { 96, 112, 132 }, { 82, 96, 116 } },     // 3 Frostland
			{ { 214, 190, 140 }, { 188, 162, 116 }, { 168, 142, 98 } },   // 4 Wueste
			{ { 98, 120, 86 }, { 82, 96, 70 }, { 70, 84, 60 } },          // 5 Dschungel
			{ { 60, 54, 54 }, { 46, 42, 42 }, { 38, 34, 34 } } };         // 6 Lavagebiet
		Palette p;
		int id = world ? BiomeMod::BiomeIdAt(world, x, y) : 0;
		if (id && !BiomeMod::IsNativeBiome(id)) {
			for (auto& pr : BiomeMod::GetDefinitionsWithIds()) if (pr.first == id) {
				const BiomeGen::BiomeDef& b = pr.second;
				p.wall[0] = b.wall.r; p.wall[1] = b.wall.g; p.wall[2] = b.wall.b;
				p.floor[0] = b.floor.r; p.floor[1] = b.floor.g; p.floor[2] = b.floor.b;
				for (int i = 0; i < 3; i++) p.ceil[i] = (uint8_t)(p.wall[i] * 3 / 4);
				for (int i = 0; i < 3; i++) { if (!p.wall[i]) p.wall[i] = 1; if (!p.floor[i]) p.floor[i] = 1; if (!p.ceil[i]) p.ceil[i] = 1; }
				return p;
			}
		}
		int k = world ? BiomeMod::NativeClassAt(world, x, y) : 0; if (k < 0 || k > 6) k = 0;
		memcpy(p.wall, nat[k][0], 3); memcpy(p.floor, nat[k][1], 3); memcpy(p.ceil, nat[k][2], 3);
		return p;
	}
	static void Recolor(Lab::Structure& s, const Def& d, const Palette& p) {
		for (size_t i = 0; i + 2 < s.rgb.size(); i += 3) {
			uint8_t* c = &s.rgb[i];
			if (c[0] == d.keyWall[0] && c[1] == d.keyWall[1] && c[2] == d.keyWall[2]) memcpy(c, p.wall, 3);
			else if (c[0] == d.keyFloor[0] && c[1] == d.keyFloor[1] && c[2] == d.keyFloor[2]) memcpy(c, p.floor, 3);
			else if (c[0] == d.keyCeil[0] && c[1] == d.keyCeil[1] && c[2] == d.keyCeil[2]) memcpy(c, p.ceil, 3);
		}
	}
	static bool InBox(const Box& b, int x, int y, int z) { return x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1 && z >= b.z0 && z < b.z1; }
	static std::string Build(const Def& d, const Out& o, const Layout& L, int bx, int by, int bz, uint32_t seed, const GroundFn& ground) {
		Rng r{ seed ^ 0xB00Bu };
		const int sx0 = bx - L.bx0, sy0 = by - L.by0;                          // Layout -> Welt
		int rare = 0, level = 1;
		void* lvZone = o.zone ? o.zone : (void*)LabBridge::ZoneAt(o.gc, (bx + 2) >> 8, (by + 2) >> 8);
		if (lvZone) level = ZoneLevelRaw(lvZone, &rare);
		level = std::max(1, level + d.lvOff);
		std::vector<int> order(L.mods.size()); for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return L.mods[a].oz < L.mods[b].oz; });   // unten zuerst (Fundament)
		int stamped = 0, plugs = 0, mons = 0, chests = 0, lights = 0, steps = 0;
		std::vector<Mark> all;
		const bool recolor = d.colorMode != 0 && (d.keyWall[0] | d.keyWall[1] | d.keyWall[2]);
		const Palette pal = BiomePalette(o.world, bx + (L.bx1 - L.bx0) / 2, by + (L.by1 - L.by0) / 2);
		uint8_t wallCol[3] = { d.battleCol[0], d.battleCol[1], d.battleCol[2] };
		if (recolor) memcpy(wallCol, pal.wall, 3);
		for (int i : order) {
			const PMod& p = L.mods[i];
			const ModDef& m = d.mods[p.mod];
			Lab::Structure s = m.s;
			for (int k = 0; k < p.rot; k++) s.RotateZ90();
			for (const OConn& c : L.conns) {
				if (c.pm != i || (c.state != 0 && c.state != 3)) continue;
				RConn rc = RotConn(d, m, m.conns[c.ci], p.rot, nullptr, nullptr);
				Plug(s, d.types[rc.type], rc); plugs++;
			}
			std::vector<Mark> mk; TakeMarkers(s, mk);
			if (recolor) Recolor(s, d, pal);
			const int wx = sx0 + p.ox, wy = sy0 + p.oy, wz = bz + p.oz;
			if (StampCell(o, s, wx, wy, wz, (d.foundation && p.oz == 0) ? 1 : 0) > 0) stamped++;
			for (Mark mm : mk) { mm.x += wx; mm.y += wy; mm.z += wz; if (mm.kind == 1 && i != L.boss) mm.kind = 0; all.push_back(mm); }
			// Zinnen auf freien Dachkanten (nicht unter einem anderen Modul)
			if (d.battlements && m.roof) {
				Lab::Structure zb; zb.Create(p.W, p.D, 2); bool any = false;
				const int top = p.oz + p.H;
				for (int yy = 0; yy < p.D; yy++) for (int xx = 0; xx < p.W; xx++) {
					if (xx != 0 && yy != 0 && xx != p.W - 1 && yy != p.D - 1) continue;
					bool covered = false;
					for (size_t j = 0; j < L.mods.size() && !covered; j++) if ((int)j != i && InBox(L.BoxOf((int)j), p.ox + xx, p.oy + yy, top)) covered = true;
					if (covered) continue;
					zb.Set(xx, yy, 0, wallCol[0], wallCol[1], wallCol[2]); any = true;
					if (((xx + yy) & 1) == 0) zb.Set(xx, yy, 1, wallCol[0], wallCol[1], wallCol[2]);
				}
				if (any) StampCell(o, zb, wx, wy, wz + p.H, 0);
			}
		}
		// Berg: Stollen vom Portal nach draussen, solange das Gelaende ueber dem Torsturz liegt. Jede Scheibe hat Boden,
		// Waende und Decke -> placeStructure hoehlt dazwischen den Fels aus (leere Voxel zwischen unterstem und oberstem
		// gefuelltem Voxel einer Saeule werden Luft, RE_placeStructure.md).
		int tunnel = 0;
		if (d.mountain) for (const OConn& c : L.conns) {
			if (c.state != 2) continue;
			const TypeDef& t = d.types[c.type];
			const bool ns = c.dir == DIR_N || c.dir == DIR_S;
			const int a0 = ((ns ? c.x2 : c.y2) - t.w) / 2 + (ns ? sx0 : sy0);
			const int face = (ns ? c.y2 : c.x2) / 2 + (ns ? sy0 : sx0);
			const int out = (c.dir == DIR_N || c.dir == DIR_E) ? 1 : -1;
			const int F = bz + c.z;
			const int W = t.w + 2 * t.t, H = t.h + 2;                            // Waende t dick, Boden + Decke
			const uint8_t* wc = recolor ? pal.wall : t.col; const uint8_t* fc = recolor ? pal.floor : t.col; const uint8_t* cc = recolor ? pal.ceil : t.col;
			Lab::Structure sl; if (ns) sl.Create(W, 1, H); else sl.Create(1, W, H);
			for (int k = 0; k < W; k++) for (int z = 0; z < H; z++) {
				const bool wall = k < t.t || k >= W - t.t, floor = z == 0, ceil = z == H - 1;
				if (!wall && !floor && !ceil) continue;
				const uint8_t* col = floor ? fc : ceil ? cc : wc;
				if (ns) sl.Set(k, 0, z, col[0], col[1], col[2]); else sl.Set(0, k, z, col[0], col[1], col[2]);
			}
			int open = 0;
			for (int k = 1; k <= 48 && open < 2; k++) {
				const int line = out > 0 ? face + k - 1 : face - k;
				const int mid = a0 + t.w / 2;
				int g; if (!ground(ns ? mid : line, ns ? line : mid, &g)) break;
				if (g < F + t.h - 1) open++;                                        // Gelaende unter dem Sturz: frei (noch 1 Scheibe)
				const int sxp = ns ? a0 - t.t : line, syp = ns ? line : a0 - t.t;
				StampCell(o, sl, sxp, syp, F - 1, 0); tunnel++;
			}
		}
		// Eingangstreppe: vom Boden des Eingangs stufenweise (1 hoch je Block, volle Torbreite) bis auf das Gelaende
		for (const OConn& c : L.conns) {
			if (c.state != 2) continue;
			const TypeDef& t = d.types[c.type];
			const bool ns = c.dir == DIR_N || c.dir == DIR_S;
			const int a0 = ((ns ? c.x2 : c.y2) - t.w) / 2 + (ns ? sx0 : sy0);
			const int face = (ns ? c.y2 : c.x2) / 2 + (ns ? sy0 : sx0);
			const int out = (c.dir == DIR_N || c.dir == DIR_E) ? 1 : -1;
			const int F = bz + c.z;                                              // Standhoehe im Tor
			Lab::Structure row; if (ns) row.Create(t.w, 1, 1); else row.Create(1, t.w, 1);
			const uint8_t* rc = recolor ? pal.floor : t.col;
			for (int k = 0; k < t.w; k++) { if (ns) row.Set(k, 0, 0, rc[0], rc[1], rc[2]); else row.Set(0, k, 0, rc[0], rc[1], rc[2]); }
			for (int k = 1; k <= 40; k++) {
				const int stand = F - k;
				const int px = ns ? a0 + t.w / 2 : (out > 0 ? face + k - 1 : face - k);
				const int py = ns ? (out > 0 ? face + k - 1 : face - k) : a0 + t.w / 2;
				int g; if (!ground(px, py, &g) || g + 1 >= stand + 1) break;     // Gelaende erreicht (Standhoehe g+1)
				const int sxp = ns ? a0 : px, syp = ns ? py : a0;
				StampCell(o, row, sxp, syp, stand - 1, 1); steps++;
			}
		}
		// Inhalt
		void* spZone = o.zone ? o.zone : (void*)LabBridge::ZoneAt(o.gc, (bx + 2) >> 8, (by + 2) >> 8);
		int zx = o.zone ? o.zx : ((bx + 2) >> 8), zy = o.zone ? o.zy : ((by + 2) >> 8);
		int idx = spZone ? SpawnCountRaw(spZone) : -1;
		for (const Mark& m : all) {
			if (m.kind == 4) { StampLight(o, m.x, m.y, m.z); lights++; continue; }
			if (m.kind == 0 && r.Range(100) >= d.monsterPct) continue;             // nur ein Teil der Monster-Marker
			if (!o.zone) {                                                       // LAB-Test in geladener Welt: Kreaturen direkt
				if (m.kind == 2 || m.kind == 3) continue;
				const std::vector<std::string>& Lst = (m.kind == 1 && !d.bosses.empty()) ? d.bosses : d.monsters;
				if (Lst.empty()) continue;
				int lv = m.kind == 1 ? level + d.bossLvOff : level;
				for (int k = 0; k < (m.kind == 0 ? d.monsterPer : 1); k++) {
					const std::string& w = Lst[r.Range((int)Lst.size())];
					int64_t fx = (int64_t)(m.x + k % 2) * 65536 + 32768, fy = (int64_t)(m.y + k / 2) * 65536 + 32768, fz = (int64_t)m.z * 65536;
					uint64_t g = 0; std::string rep;
					if (IsNum(w)) g = MobLab::SpawnRaceAt(o.gc, atoi(w.c_str()), 1, lv, fx, fy, fz, true, false, 1.0f);
					else if (const PreMob* pm = FindMob(w)) g = MobLab::SpawnCustomAt(o.gc, pm->m, fx, fy, fz, rep);
					if (g) mons++;
				}
				continue;
			}
			if (!spZone || idx < 0) continue;
			if (m.kind == 2) {
				if (City::PushObject(spZone, 0x0A, m.x + 0.5f, m.y + 0.5f, m.z, (int)(r.Next() & 3), 1.0f, 0.75f, 0.9f)) { FillChestRaw(o.world, spZone, level, rare, r.Next()); chests++; }
				continue;
			}
			int n = m.kind == 0 ? d.monsterPer : 1;
			for (int k = 0; k < n && idx < 250; k++) {
				uint8_t* sp = nullptr;
				if (m.kind == 3) { sp = MobLab::NewSpawnBare(); if (sp) VaseLootRaw(sp, level, rare); }
				else {
					const std::vector<std::string>& Lst = (m.kind == 1 && !d.bosses.empty()) ? d.bosses : d.monsters;
					if (Lst.empty()) continue;
					sp = MakeDweller(Lst[r.Range((int)Lst.size())], m.kind == 1 ? level + d.bossLvOff : level);
				}
				if (!sp) continue;
				SetSpawnPosRaw(sp, m.x + (k % 2), m.y + (k / 2), m.z, (float)(r.Range(628)) / 100.0f, m.kind == 3 ? 150.0f : 60.0f);
				if (PushSpawnRaw(spZone, sp, zx, zy, idx)) { idx++; mons++; }
			}
		}
		InterlockedIncrement(&c_placed); InterlockedExchangeAdd(&c_spawns, mons); InterlockedExchangeAdd(&c_chests, chests); InterlockedExchangeAdd(&c_lights, lights);
		char b[460]; sprintf_s(b, "%s bei (%d,%d,%d), %d x %d Bloecke: %d Module (%d obere Ebenen, %d gesetzt), %d Anschluesse zugemauert, %d Treppenstufen am Eingang, %d Stollenscheiben, %d Bewohner/Vasen, %d Truhen, %d Lichter, Level %d.",
		                       d.name.c_str(), bx, by, bz, L.bx1 - L.bx0, L.by1 - L.by0, (int)L.mods.size(), L.upper, stamped, plugs, steps, tunnel, mons, chests, lights, level);
		return b;
	}

	// ---- Zonenerzeugung ------------------------------------------------------------------------------------
	static bool AreaOk(const Def& d, const void* world, int x, int y) {
		if (d.area.empty() || d.area == "ueberall") return true;
		int id = BiomeMod::BiomeIdAt(world, x, y);
		if (d.area == "vanilla") return id == 0 || BiomeMod::IsNativeBiome(id);
		if (d.area.compare(0, 5, "biom:") == 0) {
			if (!id) return false;
			for (auto& pr : BiomeMod::GetDefinitionsWithIds()) if (pr.first == id) return _stricmp(pr.second.name.c_str(), d.area.c_str() + 5) == 0;
		}
		return false;
	}
	// lage=berg: passt der Grundriss bei (ox, oy) in einen Berg? -> Bodenhoehe bz (Eingangsboden = Boden vor dem Portal + 1).
	// Messpunkte alle 6 Bloecke: Gelaende >= Oberkante des Moduls darueber + felsdecke (an berg_anteil %), kein Wasser.
	// Vor dem Portal: Hangfuss -- 4 und 10 Bloecke draussen nicht hoeher als 3 Bloecke ueber dem Boden am Portal.
	typedef std::function<bool(int x, int y, int* z, int* type)> GroundTFn;
	static bool MountainSpot(const Def& d, const Layout& L, int ox, int oy, const GroundTFn& g, int* bzOut) {
		const OConn* e = nullptr; for (const OConn& c : L.conns) if (c.state == 2) { e = &c; break; }
		if (!e) return false;
		const int sx0 = ox - L.bx0, sy0 = oy - L.by0;
		const bool ns = e->dir == DIR_N || e->dir == DIR_S;
		const int mid = (ns ? e->x2 : e->y2) / 2 + (ns ? sx0 : sy0);
		const int face = (ns ? e->y2 : e->x2) / 2 + (ns ? sy0 : sx0);
		const int out = (e->dir == DIR_N || e->dir == DIR_E) ? 1 : -1;
		auto At = [&](int k, int* z) { int line = face + out * k; int ty = 0; return g(ns ? mid : line, ns ? line : mid, z, &ty) && (ty & 0x1F) != 2; };
		int g4, g10; if (!At(4, &g4) || !At(10, &g10)) return false;
		const int bz = g4 + 1 - e->z;
		if (g10 > g4 + 3) return false;                                           // kein Hangfuss (Gelaende steigt draussen weiter)
		int n = 0, okN = 0;
		const int W = L.bx1 - L.bx0, D = L.by1 - L.by0;
		for (int yy = 2; yy < D - 1; yy += 6) for (int xx = 2; xx < W - 1; xx += 6) {
			int top = INT_MIN;
			for (size_t i = 0; i < L.mods.size(); i++) { Box b = L.BoxOf((int)i); if (L.bx0 + xx >= b.x0 && L.bx0 + xx < b.x1 && L.by0 + yy >= b.y0 && L.by0 + yy < b.y1) top = std::max(top, b.z1); }
			if (top == INT_MIN) continue;                                         // kein Raum unter diesem Punkt
			int z, ty; n++;
			if (!g(ox + xx, oy + yy, &z, &ty) || (ty & 0x1F) == 2) continue;
			if (z >= bz + top + d.cover) okN++;
		}
		if (n == 0 || okN * 100 < n * d.mountainPct) return false;
		*bzOut = bz; return true;
	}
	bool ZonePlace(const void* world, void* zone, int zx, int zy, uint32_t seed, SurfaceFn surf,
	               const std::function<bool(int, int, int, int)>& free, int* ox0, int* oy0, int* ox1, int* oy1) {
		std::vector<Def>* v = g_defs; if (!v || !zone || !surf) return false;
		for (const Def& d : *v) {
			if (!d.enabled || d.share <= 0) continue;
			if ((float)(Hash((uint32_t)zx, (uint32_t)zy, seed, d.salt) % 10000u) >= d.share * 100.0f) continue;
			InterlockedIncrement(&c_tries);
			const uint32_t lseed = Hash((uint32_t)zx, (uint32_t)zy, seed, d.salt ^ 0x5EEDu);
			Layout L;
			if (!Generate(d, lseed, L, nullptr)) continue;
			const int W = L.bx1 - L.bx0, D = L.by1 - L.by0;
			if (W > 248 || D > 248) continue;
			if (d.mountain) {                                                     // in einen Berg graben
				GroundTFn gt = [&](int x, int y, int* z, int* ty) {
					if ((x >> 8) != zx || (y >> 8) != zy) return false;
					uint8_t t8; bool ok = surf(zone, x, y, z, &t8); *ty = t8; return ok;
				};
				for (int t = 0; t < 40; t++) {
					uint32_t h = Hash((uint32_t)zx, (uint32_t)zy, (uint32_t)t, seed ^ d.salt ^ 0xBE46u);
					int ox = zx * 256 + 4 + (int)(h % (uint32_t)std::max(1, 249 - W)), oy = zy * 256 + 4 + (int)((h >> 12) % (uint32_t)std::max(1, 249 - D));
					if (!AreaOk(d, world, ox + W / 2, oy + D / 2) || !free(ox - 4, oy - 4, ox + W + 4, oy + D + 4)) continue;
					int bz; if (!MountainSpot(d, L, ox, oy, gt, &bz)) continue;
					Out o{ world, zone, zx, zy, nullptr };
					GroundFn g = [&](int x, int y, int* z) { int ty; return gt(x, y, z, &ty); };
					std::string rep = Build(d, o, L, ox, oy, bz, lseed, g);
					static volatile LONG logsB = 0; if (InterlockedIncrement(&logsB) <= 30) std::cout << "[Dungeons] " << rep << std::endl;
					*ox0 = ox; *oy0 = oy; *ox1 = ox + W; *oy1 = oy + D;
					Tracking::Note(Tracking::T_DUNGEON, d.name, ox + W / 2, oy + D / 2, bz, seed);
					return true;
				}
				continue;
			}
			for (int t = 0; t < 10; t++) {
				uint32_t h = Hash((uint32_t)zx, (uint32_t)zy, (uint32_t)t, seed ^ d.salt ^ 0xD0u);
				int ox = zx * 256 + 4 + (int)(h % (uint32_t)std::max(1, 249 - W)), oy = zy * 256 + 4 + (int)((h >> 12) % (uint32_t)std::max(1, 249 - D));
				if (!AreaOk(d, world, ox + W / 2, oy + D / 2) || !free(ox - 4, oy - 4, ox + W + 4, oy + D + 4)) continue;
				int zmin = INT_MAX, zmax = INT_MIN; bool ok = true;
				for (int yy = 0; yy <= D && ok; yy += 8) for (int xx = 0; xx <= W; xx += 8) {
					int z; uint8_t ty;
					if (!surf(zone, ox + std::min(xx, W - 1), oy + std::min(yy, D - 1), &z, &ty)) { ok = false; break; }
					if (d.ground && (ty & 0x1F) != d.ground) { ok = false; break; }
					zmin = std::min(zmin, z); zmax = std::max(zmax, z);
				}
				if (!ok || zmax - zmin > d.uneven) continue;
				Out o{ world, zone, zx, zy, nullptr };
				GroundFn g = [&](int x, int y, int* z) {
					if ((x >> 8) != zx || (y >> 8) != zy) return false;
					uint8_t ty; return surf(zone, x, y, z, &ty);
				};
				std::string rep = Build(d, o, L, ox, oy, zmax + 1, lseed, g);
				static volatile LONG logs = 0; if (InterlockedIncrement(&logs) <= 30) std::cout << "[Dungeons] " << rep << std::endl;
				*ox0 = ox; *oy0 = oy; *ox1 = ox + W; *oy1 = oy + D;
				Tracking::Note(Tracking::T_DUNGEON, d.name, ox + W / 2, oy + D / 2, zmax + 1, seed);
				return true;
			}
		}
		return false;
	}
	static uint32_t GcSeedRaw(cube::GameController* gc) { __try { return *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
	std::string PlaceHere(cube::GameController* gc, const std::string& name, uint32_t seed) {
		std::vector<Def>* v = g_defs; if (!v || !gc || !gc->local_player) return "kein Spiel oder keine Dungeons";
		for (const Def& d : *v) {
			if (_stricmp(d.name.c_str(), name.c_str()) != 0) continue;
			Layout L; std::string why;
			if (!Generate(d, seed, L, &why)) return d.name + ": " + why;
			int px = (int)(gc->local_player->entity_data.position.X >> 16), py = (int)(gc->local_player->entity_data.position.Y >> 16);
			const int W = L.bx1 - L.bx0, D = L.by1 - L.by0;
			int ox = px + 40, oy = py - D / 2;
			GroundFn g = [gc](int x, int y, int* z) {
				cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
				uint8_t t; return zone && BiomeMod::ZoneSurfaceAt(zone, x, y, z, &t);
			};
			if (d.mountain) {                                                     // Test: naechster passender Berg im Umkreis von 160 Bloecken
				GroundTFn gt = [gc](int x, int y, int* z, int* ty) {
					cube::Zone* zone = LabBridge::ZoneAt(gc, x >> 8, y >> 8);
					uint8_t t = 0; bool ok = zone && BiomeMod::ZoneSurfaceAt(zone, x, y, z, &t); *ty = t; return ok;
				};
				int best = INT_MAX, bx = 0, by = 0, bbz = 0;
				for (int dy = -160; dy <= 160; dy += 8) for (int dx = -160; dx <= 160; dx += 8) {
					int cx = px + dx - W / 2, cy = py + dy - D / 2, bz;
					int dist = dx * dx + dy * dy; if (dist >= best || dist < 30 * 30) continue;
					if (MountainSpot(d, L, cx, cy, gt, &bz)) { best = dist; bx = cx; by = cy; bbz = bz; }
				}
				if (best == INT_MAX) return d.name + ": kein passender Berg im Umkreis von 160 Bloecken (Gelaende muss ueber dem ganzen Tempel hoeher sein, Eingang am Hangfuss).";
				Out o{ (const uint8_t*)gc + 0x2E4, nullptr, 0, 0, gc };
				std::string rep = Build(d, o, L, bx, by, bbz, seed, g);
				LabBridge::RemeshArea(gc, bx - 50, by - 50, bx + W + 50, by + D + 50);
				Tracking::Note(Tracking::T_DUNGEON, d.name + " (Test)", bx + W / 2, by + D / 2, bbz, GcSeedRaw(gc));
				return rep;
			}
			int zmax = INT_MIN;
			for (int yy = 0; yy <= D; yy += 8) for (int xx = 0; xx <= W; xx += 8) { int z; if (g(ox + std::min(xx, W - 1), oy + std::min(yy, D - 1), &z)) zmax = std::max(zmax, z); }
			if (zmax == INT_MIN) return "kein Boden am Testplatz";
			Out o{ (const uint8_t*)gc + 0x2E4, nullptr, 0, 0, gc };
			std::string rep = Build(d, o, L, ox, oy, zmax + 1, seed, g);
			LabBridge::RemeshArea(gc, ox, oy, ox + W, oy + D);
			Tracking::Note(Tracking::T_DUNGEON, d.name + " (Test)", ox + W / 2, oy + D / 2, zmax + 1, GcSeedRaw(gc));
			return rep;
		}
		return "Dungeon '" + name + "' nicht gefunden";
	}
	std::string Status() {
		std::vector<Def>* v = g_defs;
		char b[300]; sprintf_s(b, "Dungeons: %d geladen, %ld gesetzt (%ld Zonen gewuerfelt, %ld Grundrisse unter Mindestgroesse), %ld Bewohner/Vasen, %ld Truhen, %ld Lichter.",
		                       v ? (int)v->size() : 0, c_placed, c_tries, c_layoutFail, c_spawns, c_chests, c_lights);
		return b;
	}
}
