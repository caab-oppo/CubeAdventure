#include "Wildlife.h"
#include "ModDb.h"
#include "MobLab.h"
#include "MountainVillage.h"
#include "LabBridge.h"
#include "ModSwitches.h"
#include "BiomeMod.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <atomic>

namespace Wildlife {
	static void PreloadTick(cube::GameController* gc); static void FlyerScan(cube::GameController* gc); static bool BaseSeen(int r);
	static void RulesTick(cube::GameController* gc); static void LogTick();
	struct Entry { std::string file, medium, group; std::vector<std::string> habs; int weight = 10, gmin = 1, gmax = 2; };
	struct Tracked { uint64_t guid; int medium; };            // 0 Land, 1 Luft, 2 Wasser
	static std::vector<Entry> g_list; static FILETIME g_listTime = {}; static DWORD g_listCheck = 0;
	static std::map<std::string, MobLab::CustomMob> g_mobs;   // Dateipfad -> geladener Mob (baseRace < 0 = Fehler)
	static std::vector<Tracked> g_tracked;
	static bool g_listChanged = false;
	static long c_spawned[3] = {}, c_fail = 0; static std::string g_lastHab;
	static const int kMaxLand = 10, kMaxAir = 6, kMaxWater = 10;

	static std::string Dir() { return MobLab::MobsDir() + "\\Tierwelt"; }
	static std::vector<std::string> Split(const std::string& s, char c) {
		std::vector<std::string> out; std::string cur; for (char ch : s) { if (ch == c) { out.push_back(cur); cur.clear(); } else cur += ch; } out.push_back(cur); return out;
	}
	static void LoadList() {
		DWORD now = GetTickCount();
		if (!g_list.empty() && now - g_listCheck < 10000) return;
		g_listCheck = now;
		std::string p = Dir() + "\\_tierwelt.txt";
		WIN32_FILE_ATTRIBUTE_DATA fa; if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fa)) { g_list.clear(); return; }
		if (!g_list.empty() && CompareFileTime(&fa.ftLastWriteTime, &g_listTime) == 0) return;
		g_listTime = fa.ftLastWriteTime; g_list.clear(); g_mobs.clear();
		std::ifstream f(p); std::string line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> c = Split(line, ';'); if (c.size() < 6) continue;
			Entry e; e.file = c[0]; e.medium = c[1]; e.habs = Split(c[2], ',');
			e.weight = (std::max)(1, atoi(c[3].c_str())); e.gmin = (std::max)(1, atoi(c[4].c_str())); e.gmax = (std::max)(e.gmin, atoi(c[5].c_str()));
			if (c.size() > 6) e.group = c[6];
			g_list.push_back(e);
		}
		std::cout << "[Tierwelt] " << g_list.size() << " Tiere aus " << p << " geladen." << std::endl;
		g_listChanged = true;                                              // Vorbereitung fuer den nativen Spawn neu (alte Liste bleibt gueltig, bis die neue fertig ist)
	}
	static const MobLab::CustomMob* Mob(const std::string& file) {
		std::string p = Dir() + "\\" + file + ".mob";
		auto it = g_mobs.find(p);
		if (it == g_mobs.end()) {
			MobLab::CustomMob m; std::string err;
			if (!MobLab::LoadMob(p, m, err)) { std::cout << "[Tierwelt] " << err << std::endl; m.baseRace = -1; }
			it = g_mobs.insert(std::make_pair(p, m)).first;
		}
		return it->second.baseRace >= 0 ? &it->second : nullptr;
	}

	// ---- Spalte lesen ------------------------------------------------------------------------
	static bool Block(cube::GameController* gc, int x, int y, int z, uint8_t* rgb, uint8_t* t) {
		uint8_t r, g, b, tt;
		if (!MountainVillage::LabReadBlock(gc, x, y, z, r, g, b, tt)) return false;
		if ((tt & 0x1F) == 0) return false;
		if (rgb) { rgb[0] = r; rgb[1] = g; rgb[2] = b; } if (t) *t = tt & 0x1F; return true;
	}
	static bool IsGround(uint8_t t) { return t == 4 || t == 9 || t == 10 || t == 12; }
	struct Column { bool ok = false, water = false, canopy = false; int surface = 0, bottom = 0; uint8_t t = 0, bt = 0; uint8_t rgb[3] = {}; };
	static Column ReadColumn(cube::GameController* gc, int x, int y, int zTop, int zBot) {
		Column c;
		for (int z = zTop; z >= zBot; z--) {
			uint8_t rgb[3], t;
			if (!Block(gc, x, y, z, rgb, &t)) continue;
			if (t == 2) {                                                  // Wasser: Tiefe bis zum Grund
				c.water = true; c.surface = z; int b = z;
				while (b > zBot - 200 && Block(gc, x, y, b - 1, nullptr, &t) && t == 2) b--;
				c.bottom = b - 1; Block(gc, x, y, c.bottom, nullptr, &c.bt); c.ok = true; return c;
			}
			if (!IsGround(t) && z > zBot + 2) { c.canopy = true; continue; }   // Laub/Holz/Haus: weiter nach unten
			c.surface = z; c.t = t; memcpy(c.rgb, rgb, 3); c.ok = true; return c;
		}
		return c;
	}
	static bool WaterNear(cube::GameController* gc, int x, int y, int z) {
		static const int d[8][2] = { {8,0},{-8,0},{0,8},{0,-8},{6,6},{-6,6},{6,-6},{-6,-6} };
		for (auto& o : d) for (int k = -1; k <= 2; k++) { uint8_t t; if (Block(gc, x + o[0], y + o[1], z + k, nullptr, &t) && t == 2) return true; }
		return false;
	}
	static uint32_t Hash(uint32_t a, uint32_t b, uint32_t c) { uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ c * 0xC2B2AE35u; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return h; }
	// Lebensraum einer Landspalte
	static std::string LandHabitat(cube::GameController* gc, const Column& c, int x, int y) {
		const int r = c.rgb[0], g = c.rgb[1], b = c.rgb[2];
		bool wet = WaterNear(gc, x, y, c.surface);
		if (c.t == 10) return "schnee";
		if (c.t == 9) return wet ? "kueste" : "wueste";
		if (std::abs(r - g) < 18 && std::abs(g - b) < 18 && r > 60 && r < 190) return "gebirge";      // grauer Fels
		if (wet && g < 125 && g >= r) return "sumpf";
		if (g > 110 && g > r * 1.5f && b < g * 0.65f) return c.canopy ? "dschungel" : (Hash(x >> 5, y >> 5, 3) % 3 ? "wiese" : "dschungel");
		if (r > 110 && r > g * 0.85f && b < r * 0.7f) return "savanne";                                // gelb/trocken
		if (c.canopy) return "wald";
		return (Hash(x >> 6, y >> 6, 7) & 1) ? "wald" : "wiese";
	}
	static std::string WaterHabitat(const Column& c) {
		int depth = c.surface - c.bottom;
		if (depth >= 24) return "tief";
		if (depth >= 8) return "meer";
		if (c.bt == 9) return "riff";                                     // flach mit Sandgrund
		return "suess";
	}
	static bool Has(const Entry& e, const std::string& h) { for (const std::string& s : e.habs) if (s == h) return true; return false; }
	static const Entry* Pick(const std::string& medium, const std::string& hab, uint32_t rnd, bool fantasy) {
		std::vector<const Entry*> c; int sum = 0;
		for (const Entry& e : g_list) {
			if (e.medium != medium) continue;
			if (medium != "wasser") { const MobLab::CustomMob* mb = Mob(e.file); if (mb && BaseSeen(mb->baseRace)) continue; }   // nativ vorhanden -> kommt ueber den nativen Spawn
			bool ok = fantasy ? Has(e, "fantasie") : (Has(e, hab) || (medium == "wasser" && hab == "tief" && Has(e, "meer")) ||
			                                         (medium == "wasser" && hab == "riff" && Has(e, "kueste")));
			if (ok) { c.push_back(&e); sum += e.weight; }
		}
		if (c.empty()) return nullptr;
		int r = (int)(rnd % (uint32_t)sum);
		for (const Entry* e : c) { if (r < e->weight) return e; r -= e->weight; }
		return c.back();
	}
	static int Count(cube::GameController* gc, int medium, int64_t px, int64_t py) {
		int n = 0;
		for (const Tracked& t : g_tracked) {
			if (t.medium != medium) continue;
			int64_t x, y, z; if (!MobLab::CreaturePos(gc, t.guid, &x, &y, &z)) continue;
			long long dx = (x >> 16) - px, dy = (y >> 16) - py; if (dx * dx + dy * dy < 170LL * 170LL) n++;
		}
		return n;
	}

	static bool PlayerPos(cube::GameController* gc, int64_t* x, int64_t* y, int64_t* z) {
		__try {
			if (!gc->local_player) return false;
			*x = gc->local_player->entity_data.position.X >> 16; *y = gc->local_player->entity_data.position.Y >> 16; *z = gc->local_player->entity_data.position.Z >> 16;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// ---- Natuerlicher Spawn (2026-10-16) ---------------------------------------------------------
	// Wie die nativen Tiere gehoeren unsere Tiere zu ZONEN: Wird eine Zone in Spielernaehe (5 x 5 Zonen) geladen,
	// bekommt sie einmal ihre Tiere -- Orte, Arten und Gruppen sind aus Zone + Weltsamen bestimmt (gleiche Zone =
	// gleiche Tiere). Entfernt sich der Spieler (> 3 Zonen), verschwinden sie mit der Zone und kommen beim
	// Wiederkehren an denselben Stellen zurueck. Kein Zufallsspawn um den Spieler mehr.
	struct ZoneTiere { std::vector<uint64_t> guids; };
	static std::map<int64_t, ZoneTiere> g_zones;
	static int64_t ZKey(int zx, int zy) { return ((int64_t)zx << 32) ^ (uint32_t)zy; }
	static uint32_t SeedRaw(cube::GameController* gc) { __try { return *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
	// Eine Gruppe an einem bestimmten Punkt der Zone; false = Punkt ungeeignet
	static bool SpawnGroupAt(cube::GameController* gc, int x, int y, int zHint, uint32_t h, std::vector<uint64_t>& out) {
		Column c = ReadColumn(gc, x, y, zHint + 90, zHint - 110);
		if (!c.ok) return false;
		std::string medium, hab; int spawnZ;
		if (c.water) {
			if (c.surface - c.bottom < 2) return false;
			medium = "wasser"; hab = WaterHabitat(c);
			spawnZ = c.bottom + 1 + (c.surface - c.bottom - 1) / 2;
		} else {
			bool air = (h >> 7) % 100 < 30;
			medium = air ? "luft" : "land"; hab = LandHabitat(gc, c, x, y);
			spawnZ = c.surface + (air ? 7 : 1);
		}
		bool fantasy = (h >> 21) % 100 < 4;
		const Entry* e = Pick(medium, hab, h >> 3, fantasy);
		if (!e && fantasy) e = Pick(medium, hab, h >> 3, false);
		if (!e) return false;
		const MobLab::CustomMob* m0 = Mob(e->file); if (!m0) { c_fail++; return false; }
		// Level wie die nativen Tiere der Gegend (das Spiel vergibt sie je Region): Median der nativen Kreaturen im
		// Umkreis von 96 Bloecken, sonst Spielerlevel; je Tier +-1.
		MobLab::CustomMob mm = *m0;
		{
			MobLab::CreatureInfo ci[32]; int lv[32], nl = 0;
			int nc = MobLab::ListCreaturesNear(gc, x, y, spawnZ, 96, 64, ci, 32);
			for (int i = 0; i < nc; i++) if ((ci[i].guid >> 32) != 0xCAFECAFEull && ci[i].hostility == 1 && ci[i].race < MobLab::kRaceCount && ci[i].level > 0) lv[nl++] = ci[i].level;
			if (nl) { std::sort(lv, lv + nl); mm.level = (std::max)(1, lv[nl / 2] + (int)(h % 3) - 1); }
		}
		const MobLab::CustomMob* m = &mm;
		int n = e->gmin + (int)((h >> 17) % (uint32_t)(e->gmax - e->gmin + 1));
		int med = medium == "land" ? 0 : (medium == "luft" ? 1 : 2);
		for (int k = 0; k < n; k++) {
			int ox = k ? (int)((Hash(h, k, 1) % 9) - 4) : 0, oy = k ? (int)((Hash(h, k, 2) % 9) - 4) : 0, oz = spawnZ;
			if (k) {
				Column c2 = ReadColumn(gc, x + ox, y + oy, spawnZ + 12, spawnZ - 24);
				if (!c2.ok || c2.water != c.water) continue;
				oz = c.water ? c2.bottom + 1 + (c2.surface - c2.bottom - 1) / 2 : c2.surface + (med == 1 ? 7 : 1);
			}
			const int64_t U = 65536; std::string rep;
			uint64_t g = MobLab::SpawnCustomAt(gc, *m, (int64_t)(x + ox) * U + U / 2, (int64_t)(y + oy) * U + U / 2, (int64_t)oz * U, rep);
			if (g) { out.push_back(g); g_tracked.push_back({ g, med }); c_spawned[med]++; } else c_fail++;
		}
		g_lastHab = medium + "/" + hab + " -> " + e->file + " x" + std::to_string(n);
		return true;
	}
	void Tick(cube::GameController* gc) {
		if (!gc) return;
		RulesTick(gc);                                                      // Biome II: jedes Bild ein paar Schritte
		static bool on = true; static DWORD swCheck = 0; DWORD now = GetTickCount();
		if (now - swCheck > 5000) { swCheck = now; on = ModSwitches::Get("tierwelt", true); }
		if (!on) return;
		LoadList(); if (g_list.empty()) return;
		PreloadTick(gc);                                                    // jedes Bild 3 Tiere, damit die ersten Zonen sie schon bekommen
		static DWORD last = 0;
		if (now - last < 700) return;
		last = now;
		static bool shareRead = false;
		if (!shareRead) { shareRead = true; std::istringstream cf(ModDb::Txt("AdventureMod_schalter.cfg")); std::string l; while (std::getline(cf, l)) if (l.rfind("tierwelt_anteil=", 0) == 0) SetShare(atoi(l.c_str() + 16)); }
		FlyerScan(gc); LogTick();
		int64_t px, py, pz;
		if (!PlayerPos(gc, &px, &py, &pz)) return;
		const int pzx = (int)(px >> 8), pzy = (int)(py >> 8);
		// Zonen ausser Reichweite: Tiere mit der Zone entfernen (kommen beim Wiederkehren gleich zurueck)
		for (auto it = g_zones.begin(); it != g_zones.end(); ) {
			int zx = (int)(it->first >> 32), zy = (int)(int32_t)(uint32_t)it->first;
			if (std::abs(zx - pzx) > 3 || std::abs(zy - pzy) > 3) {
				for (uint64_t g : it->second.guids) MobLab::SinkCreature(gc, g);
				it = g_zones.erase(it);
			} else ++it;
		}
		for (size_t i = 0; i < g_tracked.size(); ) { int64_t x, y, z; if (!MobLab::CreaturePos(gc, g_tracked[i].guid, &x, &y, &z)) g_tracked.erase(g_tracked.begin() + i); else i++; }
		if (g_tracked.size() >= 80) return;
		// naechste noch leere, geladene Zone (innen nach aussen) besiedeln -- eine je Durchgang
		const uint32_t seed = SeedRaw(gc);
		for (int r = 0; r <= 2; r++) for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
			if ((std::max)(std::abs(dx), std::abs(dy)) != r) continue;
			int zx = pzx + dx, zy = pzy + dy;
			if (g_zones.count(ZKey(zx, zy))) continue;
			if (!LabBridge::ZoneAt(gc, zx, zy)) continue;
			ZoneTiere& zt = g_zones[ZKey(zx, zy)];
			uint32_t hz = Hash((uint32_t)zx * 7919u, (uint32_t)zy * 104729u, seed ^ 0x7A11E5u);
			int groups = 1 + (int)(hz % 3);                                   // 1..3 Gruppen je Zone (+1 Wasserversuch)
			for (int k = 0; k <= groups; k++) {
				uint32_t h = Hash(hz, (uint32_t)k, 0x51u);
				int x = zx * 256 + 16 + (int)(h % 224), y = zy * 256 + 16 + (int)((h >> 8) % 224);
				SpawnGroupAt(gc, x, y, (int)pz, h, zt.guids);
			}
			return;
		}
	}

	// ---- Am nativen Spawn (2026-10-16) ----------------------------------------------------------------
	// Das Spiel erzeugt seine Tiere als cube::Spawn-Datensaetze (Zone+0x18, vector<Spawn*>; RE: 0x5EB4D4 push_back,
	// 0x5EDD53 Groesse -> Zone+0xA0) waehrend der Zonenerzeugung und macht daraus spaeter Kreaturen (0x60FFDB..; das
	// Aussehen kopiert 0x459800 als 0xAC Bytes POD ab Spawn+0x74). Unser Anker (Hook_SunCall, Aufruf 0x5EDE84, Zone =
	// edi = [ebp-0x1308]) laeuft am Ende derselben Erzeugung. Dort:
	//   1. Biome II: liegt die Gruppe in einem Biom mit Spawnregeln (bzw. in der nativen Welt mit _nativ.tiere), wird
	//      ein Teil der Gruppen durch eine passende Regel ersetzt (native Rasse oder eigener Mob; Ort, Level, Verhalten).
	//   2. sonst eigene Tiere der Tierwelt mit GLEICHER Basisrasse (Lebensraum bevorzugt).
	// Ort, Gruppe, Entladen und Wiederkehr bleiben nativ. Teile/Vorlagen entstehen vorher auf dem Spielthread.
	struct Sub { MobLab::CustomMob m; int ids[8]; std::string medium; std::vector<std::string> habs; int weight; };
	static std::vector<Sub>* g_build = nullptr; static std::atomic<std::vector<Sub>*> g_subs{ nullptr };
	static size_t g_preIdx = 0;
	static volatile long c_native = 0, c_nativeGroups = 0, c_zoneCalls = 0, c_zoneEarly = 0, c_seen = 0, c_groups = 0, c_noCand = 0, c_rule = 0, c_ruleGroups = 0;
	static std::atomic<uint32_t> g_seenBase[16];                         // Basisrassen, die nativ spawnen (Bit je Rasse < 512)
	static SurfaceFn g_surface = nullptr;
	void SetSurfaceFn(SurfaceFn fn) { g_surface = fn; }
	static bool BaseSeen(int r) { return r >= 0 && r < 512 && (g_seenBase[r >> 5].load() & (1u << (r & 31))) != 0; }
	static bool PrepareSub(cube::GameController* gc, const MobLab::CustomMob& m, Sub& sb) {
		sb.m = m; MobLab::PrepareMobParts(gc, sb.m, sb.ids);
		if (sb.m.ownRace >= 200) MobLab::RegisterRaceName(gc, sb.m.ownRace, sb.m.raceName.empty() ? sb.m.name : sb.m.raceName);
		return true;
	}
	static void PreloadTick(cube::GameController* gc) {
		if (g_list.empty()) return;
		if (g_listChanged) { g_listChanged = false; if (g_subs.load() || g_build) { delete g_build; g_build = new std::vector<Sub>(); g_preIdx = 0; } }
		if (!g_build && g_subs.load()) return;
		if (!g_build) { g_build = new std::vector<Sub>(); g_preIdx = 0; }
		for (int k = 0; k < 3 && g_preIdx < g_list.size(); k++, g_preIdx++) {         // 3 Tiere je Bild (~1 s fuer 150)
			const Entry& e = g_list[g_preIdx];
			const MobLab::CustomMob* m = Mob(e.file); if (!m) continue;
			Sub sb; PrepareSub(gc, *m, sb); sb.medium = e.medium; sb.habs = e.habs; sb.weight = e.weight;
			g_build->push_back(sb);
		}
		if (g_preIdx >= g_list.size()) { g_subs.store(g_build); std::cout << "[Tierwelt] " << g_build->size() << " Tiere fuer den nativen Spawn vorbereitet." << std::endl; g_build = nullptr; }
	}

	// ---- Biome II: Regeln (Spielthread baut, Zonenthread liest; alte Saetze werden nie freigegeben) ----
	// own: eigener Mob (eine Datei) oder Tiergruppe ("@medium/gruppe" -> alle Tiere der Gruppe, Lebensraum bevorzugt)
	struct RuleRt { bool own = false, group = false; int race = -1; std::vector<int> subs; std::string where; int weight = 10, lvMin = 0, lvMax = 0, behave = 0; bool water = false; };
	struct BiomeRt { bool used = false; std::string name; std::vector<RuleRt> rules; int spawnShare = 70, wildShare = -1, behaveAll = 0, levelMode = 0, levelMin = 1, levelMax = 10, levelOffset = 0; };
	struct RuleSet { BiomeRt b[32]; std::vector<Sub> subs; MobLab::RaceTpl tpl[MobLab::kRaceCount]; bool hasTpl[MobLab::kRaceCount] = {}; };
	static std::atomic<RuleSet*> g_rules{ nullptr };
	static RuleSet* g_ruleBuild = nullptr;
	struct PendFile { std::string path; int biome, rule; std::vector<std::string> habs; int weight; };
	static std::vector<PendFile> g_pendFiles; static std::vector<int> g_pendRaces; static size_t g_pendIdx = 0, g_pendRaceIdx = 0;
	static std::map<std::string, int> g_pendSub;                          // Pfad -> Index in RuleSet::subs (jede Datei nur einmal vorbereiten)
	static int g_ruleVersion = -1; static FILETIME g_nativeTime = {}; static DWORD g_ruleCheck = 0;
	std::string NativeProfilePath() { return "Strukturen\\Biome\\_nativ.tiere"; }
	static bool IsNum(const std::string& s) { if (s.empty()) return false; for (char c : s) if (c < '0' || c > '9') return false; return true; }
	static void FillBiome(BiomeRt& br, const BiomeGen::BiomeDef& d, int id) {
		br.used = true; br.name = d.name; br.spawnShare = d.spawnShare; br.wildShare = d.wildShare; br.behaveAll = d.behaveAll;
		br.levelMode = d.levelMode; br.levelMin = d.levelMin; br.levelMax = (std::max)(d.levelMin, d.levelMax); br.levelOffset = d.levelOffset;
		for (const BiomeGen::SpawnRule& r : d.spawns) {
			RuleRt rr; rr.where = r.where; rr.weight = r.weight; rr.lvMin = r.lvMin; rr.lvMax = r.lvMax; rr.behave = r.behave;
			if (IsNum(r.what)) {
				rr.race = atoi(r.what.c_str()); if (rr.race < 0 || rr.race >= MobLab::kRaceCount) continue;
				rr.water = strcmp(MobLab::RaceKind(rr.race), "Wasser") == 0;
				g_pendRaces.push_back(rr.race);
			} else if (r.what[0] == '@') {                                     // Tiergruppe "@land/raubtiere"
				size_t sl = r.what.find('/'); if (sl == std::string::npos) continue;
				std::string med = r.what.substr(1, sl - 1), grp = r.what.substr(sl + 1);
				rr.own = true; rr.group = true; rr.water = med == "wasser";
				int n = 0;
				for (const Entry& e : g_list) if (e.medium == med && e.group == grp) { g_pendFiles.push_back({ Dir() + "\\" + e.file + ".mob", id, (int)br.rules.size(), e.habs, e.weight }); n++; }
				if (!n) { std::cout << "[Biome II] Tiergruppe " << r.what << " ist leer (Strukturen\\Mobs\\Tierwelt\\_tierwelt.txt)." << std::endl; continue; }
			} else {
				rr.own = true;
				g_pendFiles.push_back({ MobLab::MobsDir() + "\\" + r.what + ".mob", id, (int)br.rules.size(), {}, 10 });
			}
			br.rules.push_back(rr);
		}
	}
	// Spielthread: bei geaenderten Biomen/_nativ.tiere neu bauen, schrittweise vorbereiten, dann veroeffentlichen.
	static void RulesTick(cube::GameController* gc) {
		DWORD now = GetTickCount();
		if (!g_ruleBuild && now - g_ruleCheck > 2000) {
			g_ruleCheck = now;
			int ver = BiomeMod::DefinitionsVersion() * 7919 + (int)MobLab::BodyVersion();   // auch bei geaendertem Koerperbau (Vorlagen)
			WIN32_FILE_ATTRIBUTE_DATA fa = {}; FILETIME ft = {};
			if (GetFileAttributesExA(NativeProfilePath().c_str(), GetFileExInfoStandard, &fa)) ft = fa.ftLastWriteTime;
			if (ver != g_ruleVersion || CompareFileTime(&ft, &g_nativeTime) != 0) {
				g_ruleVersion = ver; g_nativeTime = ft;
				g_ruleBuild = new RuleSet(); g_pendFiles.clear(); g_pendRaces.clear(); g_pendSub.clear(); g_pendIdx = g_pendRaceIdx = 0;
				LoadList();                                                    // Tiergruppen kommen aus _tierwelt.txt
				BiomeGen::BiomeDef nd; std::string err;
				if (BiomeGen::LoadBiome(NativeProfilePath(), nd, err)) { nd.name = "Native Welt"; FillBiome(g_ruleBuild->b[0], nd, 0); }
				for (auto& pr : BiomeMod::GetDefinitionsWithIds()) if (pr.first >= 1 && pr.first <= 31 && pr.second.enabled) FillBiome(g_ruleBuild->b[pr.first], pr.second, pr.first);
			}
		}
		if (!g_ruleBuild) return;
		for (int k = 0; k < 3 && g_pendIdx < g_pendFiles.size(); k++, g_pendIdx++) {
			const PendFile& pf = g_pendFiles[g_pendIdx];
			RuleRt& rr = g_ruleBuild->b[pf.biome].rules[pf.rule];
			auto known = g_pendSub.find(pf.path);
			if (known != g_pendSub.end()) { rr.subs.push_back(known->second); continue; }
			MobLab::CustomMob m; std::string err;
			if (!MobLab::LoadMob(pf.path, m, err)) { std::cout << "[Biome II] " << err << std::endl; continue; }
			Sub sb; PrepareSub(gc, m, sb); sb.weight = pf.weight; sb.habs = pf.habs; sb.medium = m.swim == 1 ? "wasser" : (m.fly == 1 ? "luft" : "land");
			if (!rr.group) rr.water = m.swim == 1;
			int idx = (int)g_ruleBuild->subs.size(); g_pendSub[pf.path] = idx; rr.subs.push_back(idx);
			g_pendRaces.push_back(m.baseRace);                             // Vorlage der Basisrasse (falls die native Gruppe eine andere Rasse hat)
			g_ruleBuild->subs.push_back(sb);
		}
		if (g_pendIdx < g_pendFiles.size()) return;
		for (int k = 0; k < 6 && g_pendRaceIdx < g_pendRaces.size(); k++, g_pendRaceIdx++) {
			int r = g_pendRaces[g_pendRaceIdx];
			if (r < 0 || r >= MobLab::kRaceCount || g_ruleBuild->hasTpl[r]) continue;
			g_ruleBuild->hasTpl[r] = MobLab::MakeRaceTemplate(r, g_ruleBuild->tpl[r]);
		}
		if (g_pendRaceIdx < g_pendRaces.size()) return;
		int nb = 0, nr = 0; for (int i = 0; i < 32; i++) if (g_ruleBuild->b[i].used && !g_ruleBuild->b[i].rules.empty()) { nb++; nr += (int)g_ruleBuild->b[i].rules.size(); }
		g_rules.store(g_ruleBuild);
		std::cout << "[Biome II] Spawnregeln bereit: " << nr << " Regeln in " << nb << " Biomen, " << g_ruleBuild->subs.size() << " eigene Mobs." << std::endl;
		g_ruleBuild = nullptr;
	}

	static int g_share = 50;                                              // % der passenden nativen Gruppen, die getauscht werden
	static std::string HabitatAt(void* zone, int x, int y, int zx, int zy) {
		int z; uint8_t t, rgb[3];
		if (!g_surface || !g_surface(zone, x, y, &z, &t, rgb)) return "";
		bool wet = false;
		static const int d[8][2] = { {6,0},{-6,0},{0,6},{0,-6},{4,4},{-4,4},{4,-4},{-4,-4} };
		for (auto& o : d) {
			int xx = x + o[0], yy = y + o[1];
			if ((xx >> 8) != zx || (yy >> 8) != zy) continue;
			int z2; uint8_t t2, c2[3];
			if (!g_surface(zone, xx, yy, &z2, &t2, c2)) { wet = true; break; }
		}
		const int r = rgb[0], g = rgb[1], b = rgb[2];
		if (t == 10) return "schnee";
		if (t == 9) return wet ? "kueste" : "wueste";
		if (std::abs(r - g) < 18 && std::abs(g - b) < 18 && r > 60 && r < 190) return "gebirge";
		if (wet && g < 125 && g >= r) return "sumpf";
		if (g > 110 && g > r * 1.5f && b < g * 0.65f) return "dschungel";
		if (r > 110 && r > g * 0.85f && b < r * 0.7f) return "savanne";
		return (Hash(x >> 6, y >> 6, 7) & 1) ? "wald" : "wiese";
	}
	static bool SubHasBase(const std::vector<Sub>& subs, int race) { for (const Sub& sb : subs) if (sb.m.baseRace == race) return true; return false; }
	static bool SpawnFieldsRaw(uint8_t* sp, int* race, int* hos, int64_t* x, int64_t* y) {
		__try { *race = *(int*)(sp + 0x2C); *hos = *(int*)(sp + 0x28); *x = *(int64_t*)(sp + 0x10) >> 16; *y = *(int64_t*)(sp + 0x18) >> 16; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool ApplySEH(uint8_t* sp, const Sub& sb) { __try { MobLab::ApplyToSpawn(sp, sb.m, sb.ids, sb.m.ownRace); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	static bool ApplyTplSEH(uint8_t* sp, const MobLab::RaceTpl& t) { __try { MobLab::ApplyRaceTemplate(sp, t); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	// Verhalten (0 lassen, 1 friedlich wie das Huhn, 2 feindlich) und Level in den Datensatz
	static void FinishSEH(uint8_t* sp, int behave, int level) {
		__try {
			if (behave == 1) { *(int*)(sp + 0x28) = 1; *(uint16_t*)(sp + 0x7A) = MobLab::PeacefulFlags(*(uint16_t*)(sp + 0x7A)); }
			else if (behave == 2) { *(int*)(sp + 0x28) = 1; *(uint16_t*)(sp + 0x7A) = MobLab::HostileModFlags(*(uint16_t*)(sp + 0x7A)); }
			if (level > 0) *(int*)(sp + 0x34) = level;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static int LevelOfSEH(uint8_t* sp) { __try { return *(int*)(sp + 0x34); } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; } }
	static int SpawnListRaw(void* zone, uint8_t** out, int maxN) {
		__try {
			uint8_t** b = *(uint8_t***)((uint8_t*)zone + 0x18); uint8_t** e = *(uint8_t***)((uint8_t*)zone + 0x1C);
			if (!b || e < b || e - b > 4096) return 0;
			int n = 0; for (uint8_t** p = b; p < e && n < maxN; ++p) if (*p) out[n++] = *p;
			return n;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	static bool WhereOk(const RuleRt& r, bool water, const std::string& hab) {
		if (r.water != water) return false;
		if (r.where.empty()) return true;
		if (r.where == "land") return !water;
		if (r.where == "wasser") return water;
		return r.where == hab;
	}
	void OnZoneSpawns(const void* world, void* zone, int zx, int zy, uint32_t seed) {
		InterlockedIncrement(&c_zoneCalls);
		std::vector<Sub>* subs = g_subs.load();
		RuleSet* rs = g_rules.load();
		if (!zone) return;
		if (!subs && !rs) { InterlockedIncrement(&c_zoneEarly); return; }
		uint8_t* sp[256]; int n = SpawnListRaw(zone, sp, 256);
		if (n <= 0) return;
		struct G { int race; int64_t x, y; std::vector<uint8_t*> members; };
		std::vector<G> groups;
		for (int i = 0; i < n; i++) {
			int race, hos; int64_t x, y;
			if (!SpawnFieldsRaw(sp[i], &race, &hos, &x, &y) || hos != 1 || race < 0 || race >= MobLab::kRaceCount) continue;
			const char* kind = MobLab::RaceKind(race);
			const bool animal = strcmp(kind, "Tier") == 0 || strcmp(kind, "Wasser") == 0;
			// Monster (2026-10-19) nur, wenn ein eigenes Wesen mit GLEICHER Basisrasse bereitsteht -- z.B. Kull (Basis Oger),
			// Titanen (Basis Kyklop). Voelker, Objekte, Pflanzen bleiben immer.
			if (!animal && !(strcmp(kind, "Monster") == 0 && subs && SubHasBase(*subs, race))) continue;
			InterlockedIncrement(&c_seen);
			g_seenBase[race >> 5].fetch_or(1u << (race & 31));
			bool joined = false;
			for (G& g : groups) if (g.race == race && std::llabs(g.x - x) < 14 && std::llabs(g.y - y) < 14) { g.members.push_back(sp[i]); joined = true; break; }
			if (!joined) groups.push_back({ race, x, y, { sp[i] } });
		}
		for (G& g : groups) {
			InterlockedIncrement(&c_groups);
			uint32_t h = Hash((uint32_t)g.x * 31u + (uint32_t)zx, (uint32_t)g.y * 17u + (uint32_t)zy, seed ^ 0xA11Eu);
			const int bid = BiomeMod::BiomeIdAt(world, (int)g.x, (int)g.y);
			const BiomeRt* br = (rs && rs->b[bid].used) ? &rs->b[bid] : nullptr;
			// bearbeitetes natives Biom ohne eigene Tier-Einstellungen: es gilt weiter das Profil "Native Welt" (b[0])
			if (br && bid > 0 && BiomeMod::IsNativeBiome(bid) && br->rules.empty() && br->behaveAll == 0 && br->levelMode == 0 && br->wildShare < 0 && rs->b[0].used) br = &rs->b[0];
			const std::string hab = HabitatAt(zone, (int)g.x, (int)g.y, zx, zy);
			const bool water = strcmp(MobLab::RaceKind(g.race), "Wasser") == 0;   // native Fischrassen spawnen im Wasser, Tiere an Land (hab "" = Boden nicht lesbar)
			int behave = br ? br->behaveAll : 0, lvRule = 0; bool done = false;
			// 1. Biome II-Regeln
			const bool animalGroup = strcmp(MobLab::RaceKind(g.race), "Tier") == 0 || strcmp(MobLab::RaceKind(g.race), "Wasser") == 0;
			if (animalGroup && br && !br->rules.empty() && (int)((h >> 4) % 100) < br->spawnShare) {
				std::vector<const RuleRt*> c; int sum = 0;
				for (const RuleRt& r : br->rules) if (r.weight > 0 && WhereOk(r, water, hab) && (r.own ? !r.subs.empty() : rs->hasTpl[r.race])) { c.push_back(&r); sum += r.weight; }
				if (!c.empty()) {
					int rr = (int)((h >> 11) % (uint32_t)sum); const RuleRt* pick = c.back();
					for (const RuleRt* r : c) { if (rr < r->weight) { pick = r; break; } rr -= r->weight; }
					const Sub* own = nullptr;
					if (pick->own) {                                           // Datei bzw. ein Tier der Gruppe (Lebensraum x6, Wasser/Land passend)
						int ws = 0; std::vector<int> wv;
						for (int si : pick->subs) {
							const Sub& sb = rs->subs[si];
							int w = (sb.medium == "wasser") != water ? 0 : (std::max)(1, sb.weight);
							if (w && !hab.empty()) for (const std::string& hh : sb.habs) if (hh == hab) { w *= 6; break; }
							wv.push_back(w); ws += w;
						}
						if (!ws) { for (size_t i = 0; i < wv.size(); i++) wv[i] = 1; ws = (int)wv.size(); }
						int q = (int)((h >> 15) % (uint32_t)ws);
						for (size_t i = 0; i < wv.size(); i++) { if (q < wv[i]) { own = &rs->subs[pick->subs[i]]; break; } q -= wv[i]; }
						if (!own) own = &rs->subs[pick->subs.back()];
					}
					for (uint8_t* s : g.members) {
						bool ok;
						if (own) {
							const Sub& sb = *own;
							if (sb.m.baseRace != g.race && sb.m.baseRace >= 0 && sb.m.baseRace < MobLab::kRaceCount && rs->hasTpl[sb.m.baseRace]) ApplyTplSEH(s, rs->tpl[sb.m.baseRace]);
							ok = ApplySEH(s, sb);
						} else ok = ApplyTplSEH(s, rs->tpl[pick->race]);
						if (ok) InterlockedIncrement(&c_rule);
					}
					if (pick->behave) behave = pick->behave;
					if (pick->lvMin > 0) lvRule = pick->lvMin + (int)((h >> 19) % (uint32_t)(pick->lvMax - pick->lvMin + 1));
					InterlockedIncrement(&c_ruleGroups); done = true;
				}
			}
			// 2. eigene Tiere der Tierwelt mit gleicher Basisrasse
			const int share = (br && br->wildShare >= 0) ? br->wildShare : g_share;
			if (!done && subs && (int)(h % 100) < share) {
				std::vector<const Sub*> c; std::vector<int> wt; int sum = 0;
				bool fantasy = (h >> 9) % 100 < 5;
				for (const Sub& sb : *subs) {
					if (sb.m.baseRace != g.race) continue;
					if ((sb.medium == "wasser") != water) continue;
					int w = sb.weight;
					bool habOk = false; for (const std::string& hh : sb.habs) if (hh == hab || (fantasy && hh == "fantasie")) habOk = true;
					if (water || habOk) w *= 6;                                   // Lebensraum bevorzugt, aber kein Muss (die native Rasse passt schon zum Biom)
					c.push_back(&sb); wt.push_back(w); sum += w;
				}
				if (c.empty()) InterlockedIncrement(&c_noCand);
				else {
					int r = (int)((h >> 3) % (uint32_t)sum); const Sub* pick = c.back();
					for (size_t i = 0; i < c.size(); i++) { if (r < wt[i]) { pick = c[i]; break; } r -= wt[i]; }
					for (uint8_t* s : g.members) if (ApplySEH(s, *pick)) InterlockedIncrement(&c_native);
					InterlockedIncrement(&c_nativeGroups);
				}
			}
			// 3. Verhalten und Level (Regel > Biom-Ueberschreibung > nativ/Datei)
			if (animalGroup && (behave || lvRule || (br && br->levelMode))) {
				int k = 0;
				for (uint8_t* s : g.members) {
					int lv = lvRule;
					if (!lv && br && br->levelMode == 1) lv = br->levelMin + (int)(Hash(h, (uint32_t)k, 5u) % (uint32_t)(br->levelMax - br->levelMin + 1));
					if (!lv && br && br->levelMode == 2) lv = (std::max)(1, LevelOfSEH(s) + br->levelOffset);
					FinishSEH(s, behave, lv); k++;
				}
			}
		}
	}
	// Flieger der nativen Spawns: das Spiel laesst sie nur im Kampf fliegen -> wie bei eigenen Spawns FlightTick anmelden
	static void FlyerScan(cube::GameController* gc) {
		std::vector<Sub>* subs = g_subs.load(); RuleSet* rs = g_rules.load(); if (!subs && !rs) return;
		static DWORD last = 0; DWORD now = GetTickCount(); if (now - last < 1000) return; last = now;
		int64_t px, py, pz; if (!PlayerPos(gc, &px, &py, &pz)) return;
		MobLab::CreatureInfo ci[48]; int n = MobLab::ListCreaturesNear(gc, px, py, pz, 120, 80, ci, 48);
		for (int i = 0; i < n; i++) {
			if (ci[i].race < 200) continue;
			const Sub* hit = nullptr;
			if (subs) for (const Sub& sb : *subs) if (sb.m.ownRace == ci[i].race) { hit = &sb; break; }
			if (!hit && rs) for (const Sub& sb : rs->subs) if (sb.m.ownRace == ci[i].race) { hit = &sb; break; }
			if (hit && hit->m.fly == 1) MobLab::RegisterFlyerGuid(ci[i].guid, ci[i].x << 16, ci[i].y << 16);
		}
	}
	// Protokoll (Spielthread, alle 15 s bei Aenderung): ob und wie der native Spawn umgebaut wird
	static void LogTick() {
		static DWORD last = 0; static long lastCalls = -1; DWORD now = GetTickCount();
		if (now - last < 15000 || c_zoneCalls == lastCalls) return;
		last = now; lastCalls = c_zoneCalls;
		std::cout << "[Tierwelt] Zonen " << c_zoneCalls << " (davon " << c_zoneEarly << " vor der Vorbereitung), native Tiere " << c_seen
		          << " in " << c_groups << " Gruppen; Biome II: " << c_ruleGroups << " Gruppen/" << c_rule << " Tiere; eigene Tiere: "
		          << c_nativeGroups << " Gruppen/" << c_native << " Tiere; ohne passendes eigenes Tier " << c_noCand << " Gruppen." << std::endl;
	}
	// Tiergruppen fuer das LAB (jeder Thread, liest die Datei selbst): Kennung "@medium/gruppe", Anzeige, Anzahl
	std::vector<GroupInfo> GroupList() {
		std::vector<GroupInfo> out; std::map<std::string, int> cnt; std::map<std::string, std::string> names;
		std::ifstream f(Dir() + "\\_tierwelt.txt"); std::string line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.rfind("# Tiergruppen", 0) == 0) {                     // "# Tiergruppen (...): land: a=A, b=B | luft: ..."
				size_t c0 = line.find("): "); if (c0 == std::string::npos) continue;
				for (const std::string& part : Split(line.substr(c0 + 3), '|')) {
					size_t col = part.find(':'); if (col == std::string::npos) continue;
					std::string med = part.substr(0, col); med.erase(0, med.find_first_not_of(' ')); med.erase(med.find_last_not_of(' ') + 1);
					for (std::string kv : Split(part.substr(col + 1), ',')) {
						size_t eq = kv.find('='); if (eq == std::string::npos) continue;
						std::string k = kv.substr(0, eq); k.erase(0, k.find_first_not_of(' '));
						std::string key = "@" + med + "/" + k;
						names[key] = kv.substr(eq + 1);
						out.push_back({ key, med, kv.substr(eq + 1), 0 });
					}
				}
				continue;
			}
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> c = Split(line, ';');
			if (c.size() > 6 && !c[6].empty()) cnt["@" + c[1] + "/" + c[6]]++;
		}
		for (GroupInfo& g : out) g.count = cnt[g.key];
		return out;
	}
	void SetShare(int pct) { g_share = (std::max)(0, (std::min)(100, pct)); }

	std::string Status() {
		std::ostringstream o;
		o << "Tierwelt: " << g_list.size() << " Tiere in der Liste, gesetzt Land " << c_spawned[0] << ", Luft " << c_spawned[1] << ", Wasser " << c_spawned[2]
		  << ", Fehler " << c_fail << ". Aktiv: " << g_tracked.size() << " (Schwimmer " << MobLab::SwimmerCount() << ", Flieger " << MobLab::FlyerCount() << ")."
		  << " Besiedelte Zonen: " << g_zones.size() << ". Am nativen Spawn: " << c_zoneCalls << " Zonen (" << c_zoneEarly << " zu frueh), " << c_seen << " native Tiere in " << c_groups << " Gruppen; getauscht " << c_native << " Tiere in " << c_nativeGroups << " Gruppen (Anteil " << g_share << " %), ohne passendes Tier " << c_noCand << "; Biome II: " << c_rule << " Tiere in " << c_ruleGroups << " Gruppen" << (g_rules.load() ? "" : " (Regeln noch nicht bereit)") << "." << (g_lastHab.empty() ? "" : " Zuletzt: " + g_lastHab + ".");
		return o.str();
	}
}
