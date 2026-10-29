// BiomeGen.cpp -- siehe BiomeGen.h. Kein Spielzugriff.
#include "BiomeGen.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace BiomeGen {

	// =====================================================================
	// Zufall
	// =====================================================================
	uint32_t Hash(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
		uint32_t h = 0x811C9DC5u;
		uint32_t v[4] = { a, b, c, d };
		for (int i = 0; i < 4; i++) {
			uint32_t k = v[i] * 0xCC9E2D51u; k = (k << 15) | (k >> 17); k *= 0x1B873593u;
			h ^= k; h = (h << 13) | (h >> 19); h = h * 5 + 0xE6546B64u;
		}
		h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
		return h;
	}
	uint32_t Rng::Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	int Rng::Range(int lo, int hi) { if (hi <= lo) return lo; return lo + (int)(Next() % (uint32_t)(hi - lo + 1)); }
	float Rng::Unit() { return (float)(Next() >> 8) / 16777216.0f; }

	static float Lattice(int x, int y, uint32_t seed) { return (float)(Hash((uint32_t)x, (uint32_t)y, seed) >> 8) / 16777215.0f; }
	// double: Weltkoordinaten liegen bei ~8,4 Mio. Als float haette x/L nur ~0.0005 Aufloesung --
	// Gradienten und Abstaende wuerden blockweise springen (das waren die Einschnitte im Gebirge).
	float ValueNoise2(double x, double y, uint32_t seed) {
		double xf = std::floor(x), yf = std::floor(y);
		int x0 = (int)(long long)xf, y0 = (int)(long long)yf;
		float fx = (float)(x - xf), fy = (float)(y - yf);
		fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
		float a = Lattice(x0, y0, seed), b = Lattice(x0 + 1, y0, seed), c = Lattice(x0, y0 + 1, seed), d = Lattice(x0 + 1, y0 + 1, seed);
		return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
	}

	// =====================================================================
	// Definition
	// =====================================================================
	static const char* kPlant[PK_COUNT] = { "Baum", "Busch", "Pilz", "Kristall", "Kaktus", "Halme" };
	const char* PlantKindName(int k) { return (k >= 0 && k < PK_COUNT) ? kPlant[k] : "Datei"; }
	static const char* kClimate[CR_COUNT] = { "alle Landzellen", "gemaessigt", "heiss", "kalt", "feucht", "trocken" };
	const char* ClimateReqName(int c) { return (c >= 0 && c < CR_COUNT) ? kClimate[c] : "?"; }

	// Namen aus der Kartennamen-Tabelle World::ctor_1 (World.cpp:4100ff.): Paar (Typ, Unterart) -> Name,
	// sicher gelesen ueber Portal = 10 (vom Nutzer im Spiel bestaetigt) und gleiches Muster fuer alle Eintraege.
	const char* SiteKindName(int k) {
		switch (k) {
			case 1: return "Dorf (Viertel: Handel, Handwerk, Klasse, Tiere)";
			case 2: return "Berg (Mountain)";        case 3: return "Wald (Forest)";
			case 4: return "See (Lake)";             case 5: return "Ruinen / Grabstaette";
			case 6: return "Schlucht (Canyon)";      case 7: return "Tal (Valley)";
			case 8: return "Krater (Crater)";        case 9: return "Hoehle (Cave)";
			case 10: return "Portal";                case 11: return "Fels (Rock)";
			case 12: return "Baum (Tree)";           case 13: return "Gipfel (Peak)";
			case 14: return "Burg/Katakomben/Palast/Tempel/Pyramide";
			case 15: return "Insel (Island)";        default: return "?";
		}
	}
	uint32_t DungeonSiteBits() {
		const int d[] = { 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15 };
		uint32_t m = 0; for (int k : d) m |= 1u << k; return m;
	}

	std::string BiomesDir() { return Lab::StructuresDir() + "\\Biome"; }

	const char* NativeClassName(int k) {
		static const char* n[kNativeClasses + 1] = { "-", "Grasland", "Schneeland", "Frostland", "Wueste", "Dschungel", "Lavagebiet" };
		return (k >= 0 && k <= kNativeClasses) ? n[k] : "?";
	}
	int NativeClassOf(float T, float H, bool lava) {
		if (lava) return 6;
		if (T < 0.3f) return H > 0.7f ? 3 : 2;
		if (T > 0.75f) { if (H < 0.2f) return 4; if (H > 0.8f) return 5; return 0; }
		return 1;
	}
	BiomeDef NativeDefault(int k) {
		BiomeDef d;
		d.name = std::string("Nativ ") + NativeClassName(k); d.nativeClass = k; d.enabled = true;
		d.share = 100; d.climateReq = CR_ANY; d.setClimate = false;
		d.strength = 0.0f; d.groundType = 0; d.rockOn = false;
		d.terrainOn = false; d.nativeTrees = 100; d.riversNative = true; d.riverWidth = 1.0f; d.riversOwn = false;
		d.lakeShare = 0.0f; d.ridgeHeight = 0.0f; d.snowLine = 0.0f; d.plainOn = false; d.city = 0; d.starOn = false;
		d.slopeTrees = 0; d.mineShare = 0.0f;
		d.races.clear(); d.mobFiles.clear(); d.spawns.clear(); d.wildShare = -1; d.behaveAll = 0; d.levelMode = 0;
		d.plantDensity = 0; for (int i = 0; i < PK_COUNT; i++) d.plantWeight[i] = 0; d.plantFiles.clear();
		d.dungeonChance = 0; d.siteMask = 0xFFFFu; d.bergdorf = true;
		return d;
	}

	static std::string Trim(const std::string& s) {
		size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
	}
	std::vector<int> ParseIntList(const std::string& s) {
		std::vector<int> v; std::stringstream ss(s); std::string t;
		while (std::getline(ss, t, ',')) { t = Trim(t); if (!t.empty()) v.push_back(atoi(t.c_str())); }
		return v;
	}
	std::vector<std::string> ParseNameList(const std::string& s) {
		std::vector<std::string> v; std::stringstream ss(s); std::string t;
		while (std::getline(ss, t, ',')) { t = Trim(t); if (!t.empty()) v.push_back(t); }
		return v;
	}
	std::string JoinInts(const std::vector<int>& v) { std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += std::to_string(v[i]); } return o; }
	std::string JoinNames(const std::vector<std::string>& v) { std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += v[i]; } return o; }

	static RGB ParseRGB(const std::string& s, RGB def) {
		std::vector<int> v = ParseIntList(s);
		if (v.size() != 3) return def;
		RGB c; c.r = (uint8_t)std::min(255, std::max(0, v[0])); c.g = (uint8_t)std::min(255, std::max(0, v[1])); c.b = (uint8_t)std::min(255, std::max(0, v[2]));
		return c;
	}
	static std::string RGBStr(RGB c) { char b[32]; snprintf(b, sizeof(b), "%d,%d,%d", c.r, c.g, c.b); return b; }
	static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	static int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
	bool ParseSpawnRule(const std::string& v, SpawnRule& r) {
		std::vector<std::string> c; std::string cur;
		for (char ch : v) { if (ch == ';') { c.push_back(cur); cur.clear(); } else cur += ch; } c.push_back(cur);
		auto trim = [](std::string t) { while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back(); size_t i = 0; while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++; return t.substr(i); };
		r = SpawnRule(); r.what = trim(c[0]); if (r.what.empty()) return false;
		if (r.what.size() > 4 && r.what.compare(r.what.size() - 4, 4, ".mob") == 0) r.what.resize(r.what.size() - 4);
		if (c.size() > 1) r.where = trim(c[1]);
		if (c.size() > 2) r.weight = Clampi(atoi(c[2].c_str()), 1, 1000);
		if (c.size() > 3) r.lvMin = Clampi(atoi(c[3].c_str()), 0, 9999);
		if (c.size() > 4) r.lvMax = Clampi(atoi(c[4].c_str()), 0, 9999);
		if (c.size() > 5) r.behave = Clampi(atoi(c[5].c_str()), 0, 2);
		if (r.lvMax < r.lvMin) r.lvMax = r.lvMin;
		return true;
	}
	std::string SpawnRuleStr(const SpawnRule& r) {
		return r.what + ";" + r.where + ";" + std::to_string(r.weight) + ";" + std::to_string(r.lvMin) + ";" + std::to_string(r.lvMax) + ";" + std::to_string(r.behave);
	}

	bool LoadBiome(const std::string& path, BiomeDef& b, std::string& err) {
		std::ifstream f(path);
		if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		BiomeDef d; std::string line; bool sawMode = false;
		while (std::getline(f, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			float fv = (float)atof(v.c_str()); int iv = atoi(v.c_str());
			if (k == "name") d.name = v;
			else if (k == "aktiv") d.enabled = iv != 0;
			else if (k == "anteil") d.share = Clampi(iv, 0, 100);
			else if (k == "klima") d.climateReq = Clampi(iv, 0, CR_COUNT - 1);
			else if (k == "klima_setzen") d.setClimate = iv != 0;
			else if (k == "temperatur") d.temp = Clampf(fv, 0, 1);
			else if (k == "feuchte") d.hum = Clampf(fv, 0, 1);
			else if (k == "boden") d.ground = ParseRGB(v, d.ground);
			else if (k == "boden2") d.ground2 = ParseRGB(v, d.ground2);
			else if (k == "bodentyp") d.groundType = (iv == 4 || iv == 9 || iv == 10 || iv == 12) ? iv : 0;
			else if (k == "staerke") d.strength = Clampf(fv, 0, 1);
			else if (k == "fels") d.rock = ParseRGB(v, d.rock);
			else if (k == "fels_an") d.rockOn = iv != 0;
			else if (k == "terrain_an") d.terrainOn = iv != 0;
			else if (k == "grundhoehe") d.baseHeight = Clampf(fv, -60, 200);
			else if (k == "wellen") d.waves = Clampf(fv, 0, 40);
			else if (k == "wellen_groesse") d.waveSize = Clampf(fv, 4, 400);
			else if (k == "huegel_anteil") d.hillShare = Clampf(fv, 0, 1);
			else if (k == "huegel_hoehe") d.hillHeight = Clampf(fv, -80, 200);
			else if (k == "huegel_groesse") d.hillSize = Clampf(fv, 10, 1000);
			else if (k == "glaetten") d.flatten = Clampf(fv, 0, 1);
			else if (k == "berge") d.mountains = Clampf(fv, 0, 1);
			else if (k == "native_baeume") d.nativeTrees = Clampi(iv, 0, 100);
			else if (k == "fluesse_nativ") d.riversNative = iv != 0;
			else if (k == "fluss_breite") d.riverWidth = Clampf(fv, 0.5f, 4.0f);
			else if (k == "fluesse_eigen") d.riversOwn = iv != 0;
			else if (k == "fluss_abstand") d.riverSpacing = Clampf(fv, 80, 5000);
			else if (k == "seen_anteil") d.lakeShare = Clampf(fv, 0, 1);
			else if (k == "seen_groesse") d.lakeSize = Clampf(fv, 12, 400);
			else if (k == "bergzug_hoehe") d.ridgeHeight = Clampf(fv, 0, 400);
			else if (k == "bergzug_breite") d.ridgeWidth = Clampf(fv, 40, 1000);
			else if (k == "bergzug_abstand") d.ridgeSpacing = Clampf(fv, 300, 8000);
			else if (k == "bergzug_zacken") d.ridgeRough = Clampf(fv, 0, 1);
			else if (k == "schneegrenze") d.snowLine = Clampf(fv, 0, 600);
			else if (k == "ebene_an") d.plainOn = iv != 0;
			else if (k == "ebene_radius") d.plainRadius = Clampf(fv, 100, 6000);
			else if (k == "ebene_rand") d.plainRim = Clampf(fv, 20, 2000);
			else if (k == "ebene_hoehe") d.plainHeight = Clampf(fv, 2, 600);
			else if (k == "stadt") d.city = Clampi(iv, 0, 3);
			else if (k == "stadt_radius") d.cityRadius = Clampf(fv, 0, 6000);
			else if (k == "stadt_tiere") d.cityAnimals = ParseIntList(v);
			else if (k == "stern_an") d.starOn = iv != 0;
			else if (k == "stern_arme") d.starArms = Clampi(iv, 3, 16);
			else if (k == "stern_radius") d.starRadius = Clampf(fv, 2000, 9000);
			else if (k == "stern_ring_hoehe") d.starRingHeight = Clampf(fv, 0, 400);
			else if (k == "stern_grat_hoehe") d.starRidgeHeight = Clampf(fv, 20, 400);
			else if (k == "stern_tal_breite") d.starValleyWidth = Clampf(fv, 30, 400);
			else if (k == "stern_see_groesse") d.starLakeSize = Clampf(fv, 40, 500);
			else if (k == "hangbaeume") d.slopeTrees = Clampi(iv, 0, 200);
			else if (k == "hangbaum_groesse") d.slopeTreeSize = Clampf(fv, 0.6f, 1.6f);
			else if (k == "minen_anteil") d.mineShare = Clampf(fv, 0, 1);
			else if (k == "minen_raster") d.mineGrid = Clampf(fv, 200, 4000);
			else if (k == "stollen_breite") d.tunnelWidth = Clampi(iv, 3, 11);
			else if (k == "stollen_hoehe") d.tunnelHeight = Clampi(iv, 3, 11);
			else if (k == "minen_seitengaenge") d.mineGalleries = Clampi(iv, 0, 8);
			else if (k == "minen_schaechte") d.mineShafts = iv != 0;
			else if (k == "minen_holz") d.mineWood = ParseRGB(v, d.mineWood);
			else if (k == "minen_licht") d.mineLight = ParseRGB(v, d.mineLight);
			else if (k == "minen_schiene") d.mineRail = ParseRGB(v, d.mineRail);
			else if (k == "rassen") d.races = ParseIntList(v);
			else if (k == "rassen_anteil") d.raceShare = Clampf(fv, 0, 1);
			else if (k == "mobs") d.mobFiles = ParseNameList(v);
			else if (k == "spawn") { SpawnRule r; if (ParseSpawnRule(v, r)) d.spawns.push_back(r); }
			else if (k == "spawn_anteil") d.spawnShare = Clampi(iv, 0, 100);
			else if (k == "tierwelt_anteil") d.wildShare = Clampi(iv, -1, 100);
			else if (k == "tier_verhalten") d.behaveAll = Clampi(iv, 0, 2);
			else if (k == "tier_level_modus") d.levelMode = Clampi(iv, 0, 2);
			else if (k == "tier_level_min") d.levelMin = (std::max)(1, iv);
			else if (k == "tier_level_max") d.levelMax = (std::max)(1, iv);
			else if (k == "tier_level_versatz") d.levelOffset = Clampi(iv, -200, 200);
			else if (k == "mobs_max") d.mobMax = Clampi(iv, 0, 30);
			else if (k == "mobs_level") d.mobLevel = iv;
			else if (k == "mobs_level_modus") { d.mobLevelMode = Clampi(iv, 0, 1); sawMode = true; }
			else if (k == "mobs_level_min") d.mobLevelMin = (std::max)(1, iv);
			else if (k == "mobs_level_max") d.mobLevelMax = (std::max)(1, iv);
			else if (k == "mobs_level_abweichung") d.mobLevelOffset = (std::max)(0, iv);
			else if (k == "spawner_rassen") d.spawnerRaces = iv != 0;
			else if (k == "pflanzen_dateien_aus") d.plantFilesOff = ParseNameList(v);
			else if (k == "zonenmodelle_an") d.zoneModelsOn = ParseNameList(v);
			else if (k == "zonenmodelle_aus") d.zoneModelsOff = ParseNameList(v);
			else if (k == "pflanzen_dichte") d.plantDensity = Clampi(iv, 0, 300);
			else if (k == "pflanzen_arten") {
				for (int i = 0; i < PK_COUNT; i++) d.plantWeight[i] = 0;
				for (const std::string& t : ParseNameList(v)) {
					size_t c = t.find(':'); std::string n = Trim(t.substr(0, c)); int w = c == std::string::npos ? 1 : atoi(t.c_str() + c + 1);
					for (int i = 0; i < PK_COUNT; i++) if (n == kPlant[i]) d.plantWeight[i] = Clampi(w, 0, 20);
				}
			}
			else if (k == "stamm") d.trunk = ParseRGB(v, d.trunk);
			else if (k == "blatt") d.leaf = ParseRGB(v, d.leaf);
			else if (k == "akzent") d.accent = ParseRGB(v, d.accent);
			else if (k == "pflanzen_groesse") d.plantSize = Clampf(fv, 0.5f, 2.5f);
			else if (k == "pflanzen_dateien") d.plantFiles = ParseNameList(v);
			else if (k == "pflanzen_dateien_gewicht") d.fileWeight = Clampi(iv, 0, 20);
			else if (k == "dungeon_anteil") d.dungeonChance = Clampi(iv, 0, 100);
			else if (k == "dungeon_raeume") d.dungeonRooms = Clampi(iv, 2, 6);
			else if (k == "dungeon_wand") d.wall = ParseRGB(v, d.wall);
			else if (k == "dungeon_boden") d.floor = ParseRGB(v, d.floor);
			else if (k == "dungeon_akzent") d.dAccent = ParseRGB(v, d.dAccent);
			else if (k == "dungeon_dach") d.dungeonRoof = iv != 0;
			else if (k == "dungeon_verfall") d.decay = Clampf(fv, 0, 1);
			else if (k == "dungeon_rassen") d.dungeonRaces = ParseIntList(v);
			else if (k == "dungeon_mobs_dateien") d.dungeonMobFiles = ParseNameList(v);
			else if (k == "dungeon_mobs") d.dungeonMobs = Clampi(iv, 0, 24);
			else if (k == "dungeon_boss") d.dungeonBoss = iv != 0;
			else if (k == "orte_erlaubt") { d.siteMask = 1u; for (int t : ParseIntList(v)) if (t >= 0 && t < 16) d.siteMask |= 1u << t; }
			else if (k == "bergdorf") d.bergdorf = iv != 0;
			else if (k == "nativ") d.nativeClass = Clampi(iv, 0, kNativeClasses);
		}
		if (!sawMode && d.mobLevel > 0) { d.mobLevelMode = 1; d.mobLevelMin = d.mobLevelMax = d.mobLevel; }   // alte Dateien: fester Level
		if (d.mobLevelMax < d.mobLevelMin) std::swap(d.mobLevelMin, d.mobLevelMax);
		d.mobLevel = d.mobLevelMode == 1 ? d.mobLevelMin : -1;
		b = d;
		return true;
	}

	bool SaveBiome(const std::string& path, const BiomeDef& d, std::string& err) {
		std::ofstream f(path);
		if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[64];
		f << "# Eigenes Biom fuer den AdventureMod (Biom-Werkstatt im LAB). Anleitung: BIOM_ANLEITUNG.md\n";
		f << "name=" << d.name << "\naktiv=" << (d.enabled ? 1 : 0) << "\n";
		if (d.nativeClass > 0)
			f << "\n# NATIVES Biom: gilt fuer alle Klimazellen der Klasse '" << NativeClassName(d.nativeClass) << "' (ohne eigenes Biom).\n"
			     "# Auswahl/Klima unten werden dafuer nicht benutzt. Vorgaben aendern nichts -- nur Eingestelltes wirkt.\nnativ=" << d.nativeClass << "\n";
		f << "\n# Auswahl: Anteil in % der passenden Landzellen; klima 0 alle,1 gemaessigt,2 heiss,3 kalt,4 feucht,5 trocken\n";
		f << "anteil=" << d.share << "\nklima=" << d.climateReq << "\nklima_setzen=" << (d.setClimate ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.2f", d.temp); f << "temperatur=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.hum); f << "feuchte=" << b << "\n";
		f << "\n# Boden (bodentyp 0 = nativ, 4 Gras, 9 Sand, 10 Schnee, 12 Lava-Boden)\n";
		f << "boden=" << RGBStr(d.ground) << "\nboden2=" << RGBStr(d.ground2) << "\nbodentyp=" << d.groundType << "\n";
		snprintf(b, sizeof(b), "%.2f", d.strength); f << "staerke=" << b << "\n";
		f << "fels=" << RGBStr(d.rock) << "\nfels_an=" << (d.rockOn ? 1 : 0) << "\n";
		f << "\n# Gelaende (Bloecke; Meeresspiegel 0 -- unter Hoehe 1 entsteht Wasser)\n";
		f << "terrain_an=" << (d.terrainOn ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.1f", d.baseHeight); f << "grundhoehe=" << b << "\n";
		snprintf(b, sizeof(b), "%.1f", d.waves); f << "wellen=" << b << "\n";
		snprintf(b, sizeof(b), "%.1f", d.waveSize); f << "wellen_groesse=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.hillShare); f << "huegel_anteil=" << b << "\n";
		snprintf(b, sizeof(b), "%.1f", d.hillHeight); f << "huegel_hoehe=" << b << "\n";
		snprintf(b, sizeof(b), "%.1f", d.hillSize); f << "huegel_groesse=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.flatten); f << "glaetten=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.mountains); f << "berge=" << b << "\n";
		f << "native_baeume=" << d.nativeTrees << "\n";
		f << "\n# Fluesse und Seen (Wasser auf Gelaendehoehe)\n";
		f << "fluesse_nativ=" << (d.riversNative ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.2f", d.riverWidth); f << "fluss_breite=" << b << "\n";
		f << "fluesse_eigen=" << (d.riversOwn ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.0f", d.riverSpacing); f << "fluss_abstand=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.lakeShare); f << "seen_anteil=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.lakeSize); f << "seen_groesse=" << b << "\n";
		f << "\n# Bergzuege (Hoehe 0 = aus) und Schneegrenze (0 = aus)\n";
		snprintf(b, sizeof(b), "%.0f", d.ridgeHeight); f << "bergzug_hoehe=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.ridgeWidth); f << "bergzug_breite=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.ridgeSpacing); f << "bergzug_abstand=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.ridgeRough); f << "bergzug_zacken=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.snowLine); f << "schneegrenze=" << b << "\n";
		f << "\n# Freie Ebene in der Mitte jeder Biomzelle (Radius, Uebergang, Hoehe in Bloecken)\n";
		f << "ebene_an=" << (d.plainOn ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.0f", d.plainRadius); f << "ebene_radius=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.plainRim); f << "ebene_rand=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.plainHeight); f << "ebene_hoehe=" << b << "\n";
		f << "# Stadt auf der Ebene: 0 keine, 1 Elfenstadt, 2 Zwergenstadt, 3 zufaellig je Zelle; Radius 0 = ganze Ebene\n";
		f << "stadt=" << d.city << "\n";
		snprintf(b, sizeof(b), "%.0f", d.cityRadius); f << "stadt_radius=" << b << "\n";
		f << "# Tierviertel der Stadt: Rassen-IDs in den Gehegen (leer = Schwein 33, Schaf 34, Kuh 100, Huhn 56)\nstadt_tiere=" << JoinInts(d.cityAnimals) << "\n";
		f << "\n# Sternmassiv (Hochebene = Ebene oben; Bergring, sternfoermige Grate und Taeler mit Fluss und See)\n";
		f << "stern_an=" << (d.starOn ? 1 : 0) << "\nstern_arme=" << d.starArms << "\n";
		snprintf(b, sizeof(b), "%.0f", d.starRadius); f << "stern_radius=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.starRingHeight); f << "stern_ring_hoehe=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.starRidgeHeight); f << "stern_grat_hoehe=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.starValleyWidth); f << "stern_tal_breite=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.starLakeSize); f << "stern_see_groesse=" << b << "\n";
		f << "\n# Grosse Hangbaeume (Versuche je Zone, 0 = aus; Groesse 0.6-1.6)\n";
		f << "hangbaeume=" << d.slopeTrees << "\n";
		snprintf(b, sizeof(b), "%.2f", d.slopeTreeSize); f << "hangbaum_groesse=" << b << "\n";
		f << "\n# Minenschaechte durch die Bergzuege (Anteil 0 = aus)\n";
		snprintf(b, sizeof(b), "%.2f", d.mineShare); f << "minen_anteil=" << b << "\n";
		snprintf(b, sizeof(b), "%.0f", d.mineGrid); f << "minen_raster=" << b << "\n";
		f << "stollen_breite=" << d.tunnelWidth << "\nstollen_hoehe=" << d.tunnelHeight << "\n";
		f << "minen_seitengaenge=" << d.mineGalleries << "\nminen_schaechte=" << (d.mineShafts ? 1 : 0) << "\n";
		f << "minen_holz=" << RGBStr(d.mineWood) << "\nminen_licht=" << RGBStr(d.mineLight) << "\nminen_schiene=" << RGBStr(d.mineRail) << "\n";
		f << "\n# Kreaturen: native Rassen-IDs (Mob-Labor), Anteil der ersetzten Landspawns\n";
		f << "rassen=" << JoinInts(d.races) << "\n";
		snprintf(b, sizeof(b), "%.2f", d.raceShare); f << "rassen_anteil=" << b << "\n";
		f << "mobs=" << JoinNames(d.mobFiles) << "\nmobs_max=" << d.mobMax << "\nmobs_level=" << (d.mobLevelMode == 1 ? d.mobLevelMin : -1) << "\n";
		f << "# Schwierigkeit: modus 0 = Spielerlevel +- abweichung, 1 = feste Spanne min..max\n";
		f << "mobs_level_modus=" << d.mobLevelMode << "\nmobs_level_min=" << d.mobLevelMin << "\nmobs_level_max=" << d.mobLevelMax
		  << "\nmobs_level_abweichung=" << d.mobLevelOffset << "\nspawner_rassen=" << (d.spawnerRaces ? 1 : 0) << "\n";
		f << "\n# Biome II: Spawnregeln am nativen Spawn. spawn=was;wo;gewicht;level_min;level_max;verhalten\n";
		f << "#  was: Zahl = native Rasse, sonst eigener Mob (Pfad unter Strukturen\\Mobs ohne .mob)\n";
		f << "#  wo: leer = ueberall, land, wasser, wald, wiese, savanne, wueste, dschungel, schnee, gebirge, sumpf, kueste\n";
		f << "#  level 0 = nativ; verhalten 0 Standard, 1 friedlich, 2 feindlich\n";
		f << "spawn_anteil=" << d.spawnShare << "\n";
		for (const SpawnRule& r : d.spawns) f << "spawn=" << SpawnRuleStr(r) << "\n";
		f << "# Ueberschreibungen: tierwelt_anteil -1 = allgemein; tier_verhalten 0 nativ, 1 alle friedlich, 2 alle feindlich;\n";
		f << "# tier_level_modus 0 nativ, 1 feste Spanne, 2 nativ + versatz\n";
		f << "tierwelt_anteil=" << d.wildShare << "\ntier_verhalten=" << d.behaveAll << "\ntier_level_modus=" << d.levelMode
		  << "\ntier_level_min=" << d.levelMin << "\ntier_level_max=" << d.levelMax << "\ntier_level_versatz=" << d.levelOffset << "\n";
		f << "\n# Pflanzen (Versuche je Zone, Arten mit Gewicht)\n";
		f << "pflanzen_dichte=" << d.plantDensity << "\npflanzen_arten=";
		for (int i = 0; i < PK_COUNT; i++) { if (i) f << ","; f << kPlant[i] << ":" << d.plantWeight[i]; }
		f << "\nstamm=" << RGBStr(d.trunk) << "\nblatt=" << RGBStr(d.leaf) << "\nakzent=" << RGBStr(d.accent) << "\n";
		snprintf(b, sizeof(b), "%.2f", d.plantSize); f << "pflanzen_groesse=" << b << "\n";
		f << "pflanzen_dateien=" << JoinNames(d.plantFiles) << "\npflanzen_dateien_gewicht=" << d.fileWeight << "\n";
		f << "pflanzen_dateien_aus=" << JoinNames(d.plantFilesOff) << "\n";
		f << "# Zonen-Modelle (Strukturen\\Zonenmodelle): an = auch in diesem Biom, aus = hier nicht\n";
		f << "zonenmodelle_an=" << JoinNames(d.zoneModelsOn) << "\nzonenmodelle_aus=" << JoinNames(d.zoneModelsOff) << "\n";
		f << "\n# Dungeons (Anteil in % je Zone)\n";
		f << "dungeon_anteil=" << d.dungeonChance << "\ndungeon_raeume=" << d.dungeonRooms << "\n";
		f << "dungeon_wand=" << RGBStr(d.wall) << "\ndungeon_boden=" << RGBStr(d.floor) << "\ndungeon_akzent=" << RGBStr(d.dAccent) << "\n";
		f << "dungeon_dach=" << (d.dungeonRoof ? 1 : 0) << "\n";
		snprintf(b, sizeof(b), "%.2f", d.decay); f << "dungeon_verfall=" << b << "\n";
		f << "dungeon_rassen=" << JoinInts(d.dungeonRaces) << "\ndungeon_mobs_dateien=" << JoinNames(d.dungeonMobFiles) << "\n";
		f << "dungeon_mobs=" << d.dungeonMobs << "\ndungeon_boss=" << (d.dungeonBoss ? 1 : 0) << "\n";
		f << "\n# Native Orte, die im Biom BLEIBEN (Ortstypen; fehlt einer, wird er abgeschaltet):\n"
		     "#  1 Dorf, 2 Berg, 3 Wald, 4 See, 5 Ruinen, 6 Schlucht, 7 Tal, 8 Krater, 9 Hoehle, 10 Portal,\n"
		     "#  11 Fels, 12 Baum, 13 Gipfel, 14 Burg/Katakomben/Palast/Tempel/Pyramide, 15 Insel (Kartennamen des Spiels)\n";
		std::vector<int> al; for (int t = 1; t < 16; t++) if (d.siteMask & (1u << t)) al.push_back(t);
		f << "orte_erlaubt=" << JoinInts(al) << "\nbergdorf=" << (d.bergdorf ? 1 : 0) << "\n";
		return (bool)f;
	}

	std::vector<std::string> ListBiomeFiles() {
		std::vector<std::string> out;
#ifdef _WIN32
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((BiomesDir() + "\\*.biom").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {        // native Biome zuerst
			bool na = _strnicmp(a.c_str(), "Nativ_", 6) == 0, nb = _strnicmp(b.c_str(), "Nativ_", 6) == 0;
			return na != nb ? na : a < b;
		});
#endif
		return out;
	}

	// =====================================================================
	// Zonen-Modelle
	// =====================================================================
	const char* ZoneModelKindName(int k) { return k == ZM_TREE ? "Baum" : k == ZM_FLOWER ? "Blume" : k == ZM_DUNGEON ? "Dungeon" : k == ZM_PICKUP ? "Sammelbar" : "?"; }
	ZoneModelDef DefaultZoneModel(int kind) {
		ZoneModelDef d; d.kind = Clampi(kind, 0, ZM_COUNT - 1);
		if (d.kind == ZM_TREE)   { d.density = 12; d.slopeMin = 0.0f; d.slopeMax = 1.4f; d.spacing = 12; d.rotate = true; d.uneven = 0; d.foundation = false; }
		if (d.kind == ZM_FLOWER) { d.density = 12; d.slopeMin = 0.0f; d.slopeMax = 0.8f; d.spacing = 2;  d.rotate = true; d.uneven = 0; d.foundation = false; d.smallScale = 0.09f; d.wind = true; }
		if (d.kind == ZM_PICKUP) { d.density = 6;  d.slopeMin = 0.0f; d.slopeMax = 0.8f; d.spacing = 6;  d.rotate = true; d.uneven = 0; d.foundation = false; d.smallScale = 0.1f; d.wind = false; d.itemName = "Neue Pflanze"; }
		if (d.kind == ZM_DUNGEON){ d.density = 5;  d.slopeMin = 0.0f; d.slopeMax = 3.0f; d.spacing = 48; d.rotate = true; d.uneven = 6; d.foundation = true; }
		return d;
	}
	std::string ZoneModelsDir() { return Lab::StructuresDir() + "\\Zonenmodelle"; }
	bool LoadZoneModel(const std::string& path, ZoneModelDef& out, std::string& err) {
		std::ifstream f(path);
		if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		ZoneModelDef d; std::string line; int kind = -1;
		std::vector<std::pair<std::string, std::string>> kv;
		while (std::getline(f, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			if (k == "art") kind = v == "blume" ? ZM_FLOWER : v == "dungeon" ? ZM_DUNGEON : v == "sammelbar" ? ZM_PICKUP : ZM_TREE;
			kv.push_back(std::make_pair(k, v));
		}
		if (kind >= 0) d = DefaultZoneModel(kind);           // Vorgaben je Art, dann die Werte der Datei
		for (const auto& p : kv) {
			const std::string& k = p.first; const std::string& v = p.second;
			float fv = (float)atof(v.c_str()); int iv = atoi(v.c_str());
			if (k == "name") d.name = v;
			else if (k == "aktiv") d.enabled = iv != 0;
			else if (k == "modell") d.cub = v;
			else if (k == "gebiet") d.area = v.empty() ? "ueberall" : v;
			else if (k == "boden") d.ground = (iv == 4 || iv == 9 || iv == 10 || iv == 12) ? iv : 0;
			else if (k == "dichte") d.density = Clampi(iv, 0, 2000);
			else if (k == "klein_faktor") d.smallScale = fv <= 0.0f ? 0.0f : Clampf(fv, 0.01f, 1.0f);
			else if (k == "wind") d.wind = iv != 0;
			else if (k == "gegenstand_name") d.itemName = v;
			else if (k == "gegenstand_id") { int id = (int)strtol(v.c_str(), nullptr, 0); d.itemId = (id >= kPickupFirstId && id < kPickupFirstId + kPickupCount) ? id : 0; }
			else if (k == "hang_min") d.slopeMin = Clampf(fv, 0, 5);
			else if (k == "hang_max") d.slopeMax = Clampf(fv, 0, 5);
			else if (k == "abstand") d.spacing = Clampi(iv, 0, 200);
			else if (k == "hoehe_min") d.hMin = iv;
			else if (k == "hoehe_max") d.hMax = iv;
			else if (k == "z_versatz") d.zOffset = Clampi(iv, -64, 64);
			else if (k == "drehen") d.rotate = iv != 0;
			else if (k == "unebenheit") d.uneven = Clampi(iv, 0, 64);
			else if (k == "fundament") d.foundation = iv != 0;
		}
		out = d;
		return true;
	}
	bool SaveZoneModel(const std::string& path, const ZoneModelDef& d, std::string& err) {
		std::ofstream f(path);
		if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[32];
		f << "# Zonen-Modell fuer den AdventureMod (LAB -> Zonen-Modelle). Wird mit der Zone erzeugt wie die nativen Baeume.\n";
		f << "name=" << d.name << "\naktiv=" << (d.enabled ? 1 : 0) << "\n";
		f << "art=" << (d.kind == ZM_FLOWER ? "blume" : d.kind == ZM_DUNGEON ? "dungeon" : d.kind == ZM_PICKUP ? "sammelbar" : "baum") << "\n";
		if (d.kind == ZM_PICKUP) {
			char ib[16]; snprintf(ib, sizeof(ib), "0x%02X", d.itemId);
			f << "# Sammelbar: eigener Gegenstand (Typ 0x0B, Unterart gegenstand_id -- nicht aendern, sonst passen gesammelte Exemplare nicht mehr)\n";
			f << "gegenstand_name=" << d.itemName << "\ngegenstand_id=" << ib << "\n";
		}
		f << "modell=" << d.cub << "\n";
		f << "# gebiet: ueberall | vanilla | biom:<datei.biom>;  boden: 0 alle, 4 Gras, 9 Sand, 10 Schnee, 12 Lava-Boden\n";
		f << "gebiet=" << d.area << "\nboden=" << d.ground << "\n";
		f << "# dichte: Baum/Blume = Versuche je Zone, Dungeon = % Chance je Zone\n";
		f << "dichte=" << d.density << "\n";
		snprintf(b, sizeof(b), "%.2f", d.slopeMin); f << "hang_min=" << b << "\n";
		snprintf(b, sizeof(b), "%.2f", d.slopeMax); f << "hang_max=" << b << "\n";
		f << "abstand=" << d.spacing << "\nhoehe_min=" << d.hMin << "\nhoehe_max=" << d.hMax << "\n";
		f << "# klein_faktor (nur Blume): 0 = als Bloecke setzen, sonst Modell x Faktor wie die nativen Pflanzen (Schilf 0.09)\n";
		snprintf(b, sizeof(b), "%.3f", d.smallScale); f << "klein_faktor=" << b << "\nwind=" << (d.wind ? 1 : 0) << "\n";
		f << "z_versatz=" << d.zOffset << "\ndrehen=" << (d.rotate ? 1 : 0) << "\nunebenheit=" << d.uneven << "\nfundament=" << (d.foundation ? 1 : 0) << "\n";
		return true;
	}
	std::vector<std::string> ListZoneModelFiles() {
		std::vector<std::string> out;
#ifdef _WIN32
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((ZoneModelsDir() + "\\*.zmod").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		std::sort(out.begin(), out.end());
#endif
		return out;
	}

	BiomeDef ExampleBiome(int which) {
		BiomeDef d;
		if (which == 0) {
			d.name = "Sumpf"; d.share = 12; d.climateReq = CR_TEMPERATE; d.setClimate = true; d.temp = 0.55f; d.hum = 0.92f;
			d.ground = { 70, 92, 42 }; d.ground2 = { 52, 70, 38 }; d.groundType = 0; d.strength = 0.9f; d.rock = { 75, 80, 65 };
			// Hochflaeche (24 ueber dem Meer), flach mit vereinzelten Huegeln; das Wasser kommt
			// aus sehr vielen Seen und Fluessen, die auf Gelaendehoehe Wasser fuehren.
			d.terrainOn = true; d.baseHeight = 24.0f; d.waves = 2.5f; d.waveSize = 30.0f;
			d.hillShare = 0.10f; d.hillHeight = 14.0f; d.hillSize = 100.0f; d.flatten = 0.9f; d.mountains = 0.0f; d.nativeTrees = 25;
			d.riversNative = true; d.riverWidth = 2.0f; d.riversOwn = true; d.riverSpacing = 300.0f;
			d.lakeShare = 0.5f; d.lakeSize = 60.0f;
			d.races = { 68, 75, 61, 62, 25, 88 }; d.raceShare = 0.8f;
			d.plantDensity = 180; int w[PK_COUNT] = { 4, 4, 2, 0, 0, 8 }; memcpy(d.plantWeight, w, sizeof(w));
			d.trunk = { 70, 60, 40 }; d.leaf = { 55, 95, 40 }; d.accent = { 160, 200, 80 };
			d.dungeonChance = 5; d.wall = { 85, 95, 80 }; d.floor = { 50, 55, 45 }; d.dAccent = { 120, 220, 120 }; d.decay = 0.5f;
			d.dungeonRaces = { 96, 17, 37 }; d.siteMask = 0xFFFFu & ~DungeonSiteBits();     // Dorf bleibt, native Dungeons aus
		} else if (which == 1) {
			d.name = "Kristallwueste"; d.share = 10; d.climateReq = CR_HOT; d.setClimate = true; d.temp = 0.9f; d.hum = 0.1f;
			d.ground = { 225, 200, 160 }; d.ground2 = { 205, 175, 140 }; d.groundType = 9; d.strength = 1.0f; d.rock = { 190, 150, 120 };
			d.terrainOn = true; d.baseHeight = 8.0f; d.waves = 5.0f; d.waveSize = 70.0f;           // Duenen
			d.hillShare = 0.08f; d.hillHeight = 30.0f; d.hillSize = 140.0f; d.flatten = 0.75f; d.mountains = 0.3f; d.nativeTrees = 0;
			d.races = { 66, 42, 72, 83, 84 }; d.raceShare = 0.7f;
			d.plantDensity = 30; int w[PK_COUNT] = { 0, 1, 0, 5, 4, 1 }; memcpy(d.plantWeight, w, sizeof(w));
			d.trunk = { 120, 150, 70 }; d.leaf = { 110, 160, 80 }; d.accent = { 120, 220, 255 };
			d.dungeonChance = 8; d.wall = { 210, 190, 150 }; d.floor = { 170, 150, 115 }; d.dAccent = { 90, 200, 255 }; d.decay = 0.35f;
			d.dungeonRaces = { 77, 78, 17 }; d.siteMask = 0xFFFFu & ~((1u << 1) | (1u << 5)); d.bergdorf = false;   // keine Doerfer/Ruinen
		} else if (which == 3) {
			d.name = "Gebirge"; d.share = 10; d.climateReq = CR_ANY; d.setClimate = false;
			d.ground = { 96, 128, 70 }; d.ground2 = { 120, 120, 105 }; d.groundType = 0; d.strength = 0.7f; d.rock = { 125, 122, 118 };
			d.terrainOn = true; d.baseHeight = 30.0f; d.waves = 3.0f; d.waveSize = 45.0f;
			d.hillShare = 0.10f; d.hillHeight = 22.0f; d.hillSize = 140.0f; d.flatten = 1.0f; d.nativeTrees = 70;
			// Berge 0: die Form kommt ganz aus den Bergzuegen. Mit nativen Bergen laege das echte
			// Gelaende bis ~40 Bloecke ueber der Planung, und die Minenausgaenge laegen im Boden.
			d.mountains = 0.0f;
			// sehr hohe, breite, lange Bergzuege mit Schnee ab 150
			d.ridgeHeight = 180.0f; d.ridgeWidth = 260.0f; d.ridgeSpacing = 1400.0f; d.ridgeRough = 0.55f; d.snowLine = 150.0f;
			d.riversNative = true; d.riverWidth = 1.5f; d.riversOwn = false; d.lakeShare = 0.12f; d.lakeSize = 50.0f;
			d.races = { 47, 108, 111, 28, 87, 115 }; d.raceShare = 0.6f;
			d.plantDensity = 50; int w[PK_COUNT] = { 3, 2, 0, 1, 0, 3 }; memcpy(d.plantWeight, w, sizeof(w));
			d.trunk = { 80, 60, 40 }; d.leaf = { 50, 95, 55 }; d.accent = { 200, 200, 230 };
			d.dungeonChance = 3; d.wall = { 110, 105, 100 }; d.floor = { 70, 65, 60 }; d.dAccent = { 230, 180, 80 };
			d.dungeonRaces = { 109, 111, 17 };
			d.mineShare = 0.7f; d.mineGrid = 700.0f; d.tunnelWidth = 5; d.tunnelHeight = 5; d.mineGalleries = 3; d.mineShafts = true;
		} else if (which == 4) {
			// Sternmassiv: Hochebene (350) in der Mitte, Bergring, sternfoermige Grate und Taeler,
			// Fluss am Talgrund endet im See. Gelaendewerte aussen = Vorgaben des Nutzers (2026-10-04).
			d.name = "Sternmassiv"; d.share = 10; d.climateReq = CR_ANY; d.setClimate = false;
			d.ground = { 78, 138, 58 }; d.ground2 = { 60, 118, 46 }; d.groundType = 0; d.strength = 0.9f; d.rock = { 128, 124, 120 };
			d.terrainOn = true; d.baseHeight = 150.0f; d.waves = 1.0f; d.waveSize = 300.0f;
			d.hillShare = 0.70f; d.hillHeight = 152.0f; d.hillSize = 350.0f; d.flatten = 1.0f; d.mountains = 0.75f; d.nativeTrees = 70;
			d.riversNative = true; d.riverWidth = 1.5f; d.riversOwn = true; d.riverSpacing = 600.0f;
			d.lakeShare = 0.12f; d.lakeSize = 50.0f;
			d.plainOn = true; d.plainRadius = 1200.0f; d.plainRim = 250.0f; d.plainHeight = 350.0f;
			d.snowLine = 400.0f;
			d.starOn = true; d.starArms = 8; d.starRadius = 6000.0f; d.starRingHeight = 130.0f; d.starRidgeHeight = 152.0f;
			d.starValleyWidth = 90.0f; d.starLakeSize = 160.0f;
			d.slopeTrees = 40; d.slopeTreeSize = 1.0f;
			d.races = { 47, 108, 111, 28, 87, 115 }; d.raceShare = 0.6f;
			d.plantDensity = 40; int w[PK_COUNT] = { 2, 3, 0, 0, 0, 5 }; memcpy(d.plantWeight, w, sizeof(w));
			d.trunk = { 92, 64, 40 }; d.leaf = { 48, 100, 52 }; d.accent = { 230, 230, 240 };
			d.dungeonChance = 3; d.wall = { 110, 105, 100 }; d.floor = { 70, 65, 60 }; d.dAccent = { 230, 180, 80 };
			d.dungeonRaces = { 109, 111, 17 };
		} else {
			d.name = "Pilzwald"; d.share = 10; d.climateReq = CR_WET; d.setClimate = false;
			d.ground = { 90, 70, 110 }; d.ground2 = { 70, 55, 95 }; d.groundType = 0; d.strength = 0.85f; d.rock = { 95, 85, 105 };
			d.terrainOn = false; d.nativeTrees = 40;
			d.races = { 69, 70, 71, 59, 37, 38 }; d.raceShare = 0.6f;
			d.plantDensity = 55; int w[PK_COUNT] = { 1, 2, 7, 1, 0, 3 }; memcpy(d.plantWeight, w, sizeof(w));
			d.trunk = { 225, 215, 200 }; d.leaf = { 170, 60, 160 }; d.accent = { 250, 240, 120 };
			d.dungeonChance = 6; d.wall = { 110, 90, 120 }; d.floor = { 60, 50, 70 }; d.dAccent = { 250, 120, 220 };
			d.dungeonRaces = { 41, 45, 43 };
		}
		return d;
	}

	// =====================================================================
	// Gelaende
	// =====================================================================
	// =====================================================================
	// Sternmassiv
	// =====================================================================
	StarParams MakeStarParams(const BiomeDef& d) {
		StarParams p;
		p.on = d.starOn; p.arms = std::max(3, std::min(16, d.starArms));
		p.radius = d.starRadius; p.ringH = d.starRingHeight; p.ridgeH = d.starRidgeHeight;
		p.valleyW = d.starValleyWidth; p.lakeSize = d.starLakeSize;
		p.plateauR = d.plainRadius; p.plateauRim = d.plainRim; p.plateauH = d.plainHeight; p.baseH = d.baseHeight;
		p.riverHalf = 6.0f * std::max(0.5f, d.riverWidth);
		return p;
	}
	static float Sstep(float t) { if (t <= 0) return 0; if (t >= 1) return 1; return t * t * (3 - 2 * t); }
	static float U01(uint32_t q) { return (float)(q & 0xFFFF) / 65535.0f; }
	static const double kTau = 6.283185307179586;
	static double Wrap(double a) { a = std::fmod(a, kTau); return a < 0 ? a + kTau : a; }

	// Geometrie (alles in Bloecken, r = Abstand zur Mitte):
	//   r < R_p                    Hochebene (plateauH)
	//   R_p .. rRing               Innenflanke bis zum Ringgrat (rRing)
	//   rRing .. rSrc              Ringgrat geht in die Arme ueber, Talschluesse (Kare)
	//   rSrc .. rL                 Taeler: flacher Grund faellt von der Quellhoehe zur Seehoehe, Fluss
	//   rL                         See am Talende, dahinter steigt der Grund wieder (Becken)
	//   0.85 R .. R                weicher Uebergang in die uebrige Biomform
	// Zwischen zwei Taelern liegt ein Grat; Querprofil glatt (keine kleinen Spitzen), Gipfel und
	// Saettel nur entlang des Grats mit ~650 Bloecken Wellenlaenge.
	void StarAt(const StarParams& p, uint32_t seed, double dx, double dy, float base, StarSample* o) {
		o->h = base; o->mask = 0.0f; o->riverBlocks = 1e9f; o->lake = false; o->lakeX = o->lakeY = 0; o->lakeR = 0; o->floor = false;
		o->hRaw = 0.0f; o->fade = 1.0f;
		if (!p.on) return;
		double r = std::sqrt(dx * dx + dy * dy);
		if (r >= p.radius) return;
		const float R = p.radius;
		const float fadeStart = R * 0.85f;
		const float rRing = p.plateauR + std::max(300.0f, p.plateauRim + 200.0f);
		const float flankIn = rRing - p.plateauR;
		const float flankOut = std::max(500.0f, 0.12f * R);
		const float rSrc = rRing + flankOut;
		const float wF = p.valleyW * 0.5f;
		const float lakeTarget = p.baseH + 4.0f;
		const float Hring0 = p.plateauH + p.ringH;
		float e = Sstep((float)(r - fadeStart) / (R - fadeStart));
		o->mask = 1.0f - e;
		if (r < p.plateauR) { o->h = p.plateauH; o->hRaw = p.plateauH; o->fade = 0.0f; return; }
		double th = Wrap(std::atan2(dy, dx));
		// Ringgrat: Hoehe schwankt entlang des Kreises (Gipfel/Saettel ~450 Bloecke)
		float ringN = ValueNoise2(std::cos(th) * rRing / 450.0 + 500.0, std::sin(th) * rRing / 450.0 + 500.0, seed ^ 0x21A6u);
		float Hring = p.plateauH + p.ringH * (0.75f + 0.5f * ringN);
		if (r < rRing) {
			float hh = p.plateauH + (Hring - p.plateauH) * Sstep((float)(r - p.plateauR) / flankIn);
			o->h = hh * (1 - e) + base * e; o->hRaw = hh; o->fade = e;
			return;
		}
		// Arme: Talachsen a_k(r) = a0 + seitliches Maeandern / r. Die festen Werte je Arm haengen nur
		// von Samen und Einstellungen ab -> je Thread zwischengespeichert (Leistung, 2026-10-06).
		const int N = std::max(3, std::min(16, p.arms));
		struct ArmCache { bool valid; uint32_t seed; StarParams p; double a0[16]; uint32_t q7[16]; float head[16], lR[16], rL[16]; double lakeX[16], lakeY[16]; };
		static thread_local ArmCache ac = { false };
		if (!ac.valid || ac.seed != seed || memcmp(&ac.p, &p, sizeof(StarParams)) != 0) {
			ac.valid = true; ac.seed = seed; memcpy(&ac.p, &p, sizeof(StarParams));
			for (int k = 0; k < N; k++) {
				uint32_t q = Hash(seed, (uint32_t)k, 0x57A2u);
				ac.a0[k] = kTau * k / N + (U01(q) - 0.5) * 0.3 * kTau / N;
				ac.q7[k] = Hash(q, 7);
				ac.head[k] = p.plateauH - 25.0f - 30.0f * U01(q >> 16);
				ac.lR[k] = p.lakeSize * 0.5f * (0.8f + 0.4f * U01(Hash(q, 1)));
				ac.rL[k] = R * 0.82f - ac.lR[k] * 1.2f - 0.06f * R * U01(Hash(q, 2));
				double rl = ac.rL[k];
				float lw = 230.0f * std::min(1.0f, (float)(rl - rRing) / 1500.0f);
				double al = ac.a0[k] + lw * (ValueNoise2(rl / 1100.0, k * 17.3, ac.q7[k]) * 2.0f - 1.0f) / rl;
				ac.lakeX[k] = std::cos(al) * rl; ac.lakeY[k] = std::sin(al) * rl;
			}
		}
		const float* head = ac.head; const float* lR = ac.lR; const float* rL = ac.rL;
		double rr = std::max(r, (double)rRing);
		float latW = 230.0f * std::min(1.0f, (float)(rr - rRing) / 1500.0f);
		auto Ang = [&](int k) { float lat = latW * (ValueNoise2(rr / 1100.0, k * 17.3, ac.q7[k]) * 2.0f - 1.0f); return Wrap(ac.a0[k] + lat / rr); };
		// Grob ueber die Nennwinkel das Nachbartal finden, dann nur die 4 umliegenden Arme genau
		// rechnen (das seitliche Maeandern verschiebt einen Arm hoechstens um ~0.14 rad < halbe Teilung).
		int k0 = 0; { double bst = 1e9; for (int k = 0; k < N; k++) { double dlt = Wrap(th - ac.a0[k]); if (dlt < bst) { bst = dlt; k0 = k; } } }
		double ang[16]; bool have[16] = {};
		for (int j = -1; j <= 2; j++) { int k = ((k0 + j) % N + N) % N; ang[k] = Ang(k); have[k] = true; }
		// linkes Tal k (groesster Winkel <= th), rechtes Tal k+1
		int kL = k0; double best = 1e9;
		for (int k = 0; k < N; k++) if (have[k]) { double dlt = Wrap(th - ang[k]); if (dlt < best) { best = dlt; kL = k; } }
		int kR = (kL + 1) % N;
		if (!have[kR]) { ang[kR] = Ang(kR); have[kR] = true; }
		double gap = Wrap(ang[kR] - ang[kL]); if (gap < 1e-4) gap = kTau / N;
		double uL = best * rr, uR = (gap - best) * rr, half = gap * rr * 0.5;
		int v = uL <= uR ? kL : kR; double u = std::min(uL, uR);
		auto Floor = [&](int k, double rad) -> float {
			if (rad < rSrc) return head[k] + (float)(rSrc - rad) * 0.5f;                 // Talschluss steigt zum Ring
			if (rad <= rL[k]) { float t = (float)((rad - rSrc) / (rL[k] - rSrc)); float g = 1.0f - (1.0f - t) * (1.0f - t); return head[k] + (lakeTarget - head[k]) * g; }
			return lakeTarget + (float)(rad - rL[k]) * 0.12f;                            // Becken hinter dem See
		};
		float FL = Floor(kL, r), FR = Floor(kR, r), Fv = v == kL ? FL : FR;
		// Grathoehe zwischen kL und kR: vom Ring nach aussen auf Grundhoehe + Grathoehe
		float cT = Sstep((float)(r - rSrc) / std::max(1.0f, fadeStart - rSrc));
		float Cbase = Hring0 + (p.baseH + p.ridgeH - Hring0) * cT;
		float Fmid = 0.5f * (FL + FR);
		float cN = 0.8f + 0.4f * ValueNoise2(r / 650.0, kL * 31.7 + 3.0, seed ^ 0xC4E5u);
		float crest = Fmid + (Cbase - Fmid) * cN;
		crest = std::max(crest, std::max(FL, FR) + 20.0f);
		// Querprofil: flacher Talgrund (wF), sanft steigender Hangfuss, dann der eigentliche Grat
		// mit hoechstens Wr halber Breite -- so bleiben die Grate auch aussen (weite Sektoren)
		// deutliche Bergzuege statt breiter Wellen.
		const float Wr = 650.0f;
		double cDist = half - u;                                    // Abstand zum Grat
		double ridgeW = std::min((double)Wr, half - wF);
		// Anteil des Hangfusses waechst stetig mit seiner Breite (sonst Sprung, wo half - wF = Wr)
		float footShare = 0.15f * Sstep((float)((half - wF - ridgeW) / 400.0));
		float prof;
		if (u <= wF) prof = 0.0f;
		else if (cDist >= ridgeW) {                                 // Hangfuss bis footShare
			double footW = std::max(1.0, half - ridgeW - wF);
			prof = footShare * Sstep((float)((u - wF) / footW));
		} else {
			float t = (float)(1.0 - cDist / std::max(1.0, ridgeW));   // 0 Gratfuss .. 1 Grat
			float q = std::pow(t, 1.3f);
			float g = q * q * (3 - 2 * q);
			prof = footShare + (1.0f - footShare) * g;
		}
		float hA = Fv + (crest - Fv) * prof;
		// Ufer: neben dem Flussbett liegt der Talgrund einen halben Block hoeher. Der Wasserspiegel
		// (oberster Block = floor(Talgrund)) faellt entlang des Flusses hoechstens 0.15 je Block --
		// so liegt kein Uferblock tiefer als das Wasser daneben, meist ist er buendig.
		if (r >= rSrc && u >= p.riverHalf) hA += 0.5f * Sstep((float)(r - rSrc) / 20.0f);
		o->floor = u < wF;
		float hh = hA;
		if (r < rSrc) { float b = Sstep((float)(r - rRing) / flankOut); hh = Hring * (1 - b) + hA * b; }
		o->h = hh * (1 - e) + base * e; o->hRaw = hh; o->fade = e;
		// Fluss am Talgrund von rSrc bis in den See
		if (r >= rSrc && r <= rL[v]) o->riverBlocks = (float)u;
		// See am Ende des naechsten Tals
		o->lake = true; o->lakeX = ac.lakeX[v]; o->lakeY = ac.lakeY[v]; o->lakeR = lR[v];
	}

	float PlainMask(float radius, float rim, double dx, double dy) {
		double d = std::sqrt(dx * dx + dy * dy);
		if (d <= radius) return 1.0f;
		if (rim < 1.0f || d >= radius + rim) return 0.0f;
		float s = 1.0f - (float)((d - radius) / rim);
		return s * s * (3.0f - 2.0f * s);
	}
	float TerrainTarget(float base, float waves, float waveSize, float hillShare, float hillHeight, float hillSize, int x, int y) {
		double fx = x, fy = y;
		// Wellen: zwei Oktaven, auf -1..1
		float ws = waveSize < 4 ? 4 : waveSize;
		float w = ValueNoise2(fx / ws, fy / ws, 0x7E11u) * 0.67f + ValueNoise2(fx * 2 / ws, fy * 2 / ws, 0x7E12u) * 0.33f;
		float h = base + (w * 2.0f - 1.0f) * waves;
		// Huegel: Raster der Groesse hillSize; jede Rasterzelle traegt mit der
		// Wahrscheinlichkeit hillShare einen Huegel (zufaellige Mitte, Radius 35-60 %).
		// So ist der Anteil direkt einstellbar und die Huegel stehen vereinzelt.
		if (hillShare > 0.0f && hillHeight != 0.0f) {
			float hs = hillSize < 10 ? 10 : hillSize;
			int cx0 = (int)std::floor(fx / hs), cy0 = (int)std::floor(fy / hs);
			float best = 0.0f;
			for (int cy = cy0 - 1; cy <= cy0 + 1; cy++) for (int cx = cx0 - 1; cx <= cx0 + 1; cx++) {
				uint32_t q = Hash((uint32_t)cx, (uint32_t)cy, 0x4116E1u);
				if ((float)(q & 0xFFFF) / 65536.0f >= hillShare) continue;
				double mx = (cx + 0.2 + 0.6 * (double)((q >> 16) & 0xFF) / 255.0) * hs;
				double my = (cy + 0.2 + 0.6 * (double)((q >> 24) & 0xFF) / 255.0) * hs;
				float r = hs * (0.35f + 0.25f * (float)(Hash(q, 1) & 0xFF) / 255.0f);
				float d = std::sqrt((fx - mx) * (fx - mx) + (fy - my) * (fy - my)) / r;
				if (d >= 1.0f) continue;
				float t = 1.0f - d; t = t * t * (3 - 2 * t);
				// unruhige Kuppe: Wellenlaenge waechst mit der Huegelgroesse, hoechstens ~6 Bloecke Ausschlag.
				// (Frueher fest 7 Bloecke und 25 %: bei hohen Huegeln kleine spitze Zacken an den Haengen.)
				float amp = std::min(0.25f, 6.0f / std::max(1.0f, std::fabs(hillHeight)));
				double rw = std::max(7.0, (double)hs * 0.1);
				t *= 1.0f - amp + amp * ValueNoise2(fx / rw, fy / rw, 0x7E23u);
				if (t > best) best = t;
			}
			h += hillHeight * best;
		}
		return h;
	}

	static float RidgeField(double L, double fx, double fy) {
		return 2.0f * (ValueNoise2(fx / L, fy / L, 0xB3B6u) * 0.85f + ValueNoise2(fx * 2.5f / L, fy * 2.5f / L, 0xB3B7u) * 0.15f) - 1.0f;
	}
	float RidgeTerm(float height, float width, float spacing, float rough, int x, int y, float* mask) {
		if (mask) *mask = 0.0f;
		if (height <= 0.0f || width < 10.0f) return 0.0f;
		float L = spacing < 300 ? 300 : spacing;
		double fx = x, fy = y;
		// Kammlinie = Nulllinie eines groben Rauschens (lang und gewunden), Abstand |f|/|grad f|
		float f = RidgeField(L, fx, fy);
		float gx = (RidgeField(L, fx + 2, fy) - RidgeField(L, fx - 2, fy)) * 0.25f;
		float gy = (RidgeField(L, fx, fy + 2) - RidgeField(L, fx, fy - 2)) * 0.25f;
		// Untergrenze fuer den Gradienten: an Sattel-/Extrempunkten (grad -> 0) wuerde der
		// Abstand sonst springen und senkrechte Waende erzeugen.
		float gmin = 0.9f / L;
		float g = std::sqrt(gx * gx + gy * gy); if (g < gmin) g = gmin;
		float dist = std::fabs(f) / g;
		// Breite schwankt entlang des Zuges (+-25 %)
		float wv = width * (0.75f + 0.5f * ValueNoise2(fx / (L * 0.5f), fy / (L * 0.5f), 0xB3B8u));
		float u = dist / wv;
		if (u >= 1.0f) return 0.0f;
		// breiter Ruecken mit steileren Flanken: smoothstep(1-u), leicht zugespitzt
		float p = 1.0f - u; p = p * p * (3 - 2 * p);
		p = 0.7f * p + 0.3f * p * p;
		if (mask) *mask = p;
		// Gipfel und Saettel entlang des Kamms, Zerklueftung an den Flanken
		float along = 0.55f + 0.45f * ValueNoise2(fx / (width * 1.3f), fy / (width * 1.3f), 0xB3B9u);
		float crag = (ValueNoise2(fx / 28.0f, fy / 28.0f, 0xB3BAu) - 0.5f) * 2.0f;
		float h = height * p * (1.0f - rough * 0.45f + rough * 0.45f * along);
		h += crag * rough * height * 0.08f * p;
		return h;
	}

	// Normale auf den Bergzug (Richtung quer ueber den Kamm)
	static void RidgeNormal(double L, double fx, double fy, float* nx, float* ny) {
		float gx = RidgeField(L, fx + 2, fy) - RidgeField(L, fx - 2, fy);
		float gy = RidgeField(L, fx, fy + 2) - RidgeField(L, fx, fy - 2);
		float g = std::sqrt(gx * gx + gy * gy);
		if (g < 1e-9f) { *nx = 1; *ny = 0; return; }
		*nx = gx / g; *ny = gy / g;
	}
	static float GroundAt(const BiomeDef& d, int x, int y) {
		return TerrainTarget(d.baseHeight, d.waves, d.waveSize, d.hillShare, d.hillHeight, d.hillSize, x, y) +
		       RidgeTerm(d.ridgeHeight, d.ridgeWidth, d.ridgeSpacing, d.ridgeRough, x, y, nullptr);
	}

	void PlanMines(const BiomeDef& d, int x0, int y0, int x1, int y1, std::vector<MineSeg>& out) {
		if (d.mineShare <= 0.0f || d.ridgeHeight <= 0.0f) return;
		float G = d.mineGrid < 200 ? 200 : d.mineGrid;
		float reach = d.ridgeWidth * 1.6f + 80.0f;                   // so weit kann ein Stollen von seiner Rasterzelle reichen
		int gx0 = (int)std::floor((x0 - reach) / G), gx1 = (int)std::floor((x1 + reach) / G);
		int gy0 = (int)std::floor((y0 - reach) / G), gy1 = (int)std::floor((y1 + reach) / G);
		for (int gy = gy0; gy <= gy1; gy++) for (int gx = gx0; gx <= gx1; gx++) {
			uint32_t q = Hash((uint32_t)gx, (uint32_t)gy, 0x313E5u);
			if ((float)(q & 0xFFFF) / 65536.0f >= d.mineShare) continue;
			// Stollenmitte: bis zu 6 Zufallspunkte der Zelle, der erste auf einem Bergzug zaehlt
			float cx = 0, cy = 0; bool found = false;
			for (int k = 0; k < 6 && !found; k++) {
				uint32_t qk = Hash(q, 0xC0u + (uint32_t)k);
				cx = (gx + 0.1f + 0.8f * (float)(qk & 0xFFFF) / 65535.0f) * G;
				cy = (gy + 0.1f + 0.8f * (float)(qk >> 16) / 65535.0f) * G;
				float m = 0; RidgeTerm(d.ridgeHeight, d.ridgeWidth, d.ridgeSpacing, d.ridgeRough, (int)cx, (int)cy, &m);
				found = m >= 0.45f;
			}
			if (!found) continue;                                     // kein Bergzug in dieser Zelle -> keine Mine
			float nx, ny; RidgeNormal(d.ridgeSpacing < 300 ? 300 : d.ridgeSpacing, cx, cy, &nx, &ny);
			// Laenge: so weit, bis beidseits das Gelaende unter dem Kammfuss liegt
			float half = d.ridgeWidth * 0.5f;
			for (int k = 0; k < 40 && half < d.ridgeWidth * 1.6f; k++) {
				float ma = 0, mb = 0;
				RidgeTerm(d.ridgeHeight, d.ridgeWidth, d.ridgeSpacing, d.ridgeRough, (int)(cx + nx * half), (int)(cy + ny * half), &ma);
				RidgeTerm(d.ridgeHeight, d.ridgeWidth, d.ridgeSpacing, d.ridgeRough, (int)(cx - nx * half), (int)(cy - ny * half), &mb);
				if (ma < 0.05f && mb < 0.05f) break;
				half += d.ridgeWidth * 0.05f;
			}
			float ax = cx - nx * half, ay = cy - ny * half, bx = cx + nx * half, by = cy + ny * half;
			// Stollensohle: 3 Bloecke ueber dem tieferen Talboden an den Enden (das Gelaende steigt zum Kamm)
			float za = GroundAt(d, (int)ax, (int)ay), zb = GroundAt(d, (int)bx, (int)by);
			int z = (int)std::floor((za < zb ? za : zb)) + 3;
			if (z < 6) continue;                                      // unter Wasserniveau keine Mine
			MineSeg mainS; mainS.ax = ax; mainS.ay = ay; mainS.bx = bx; mainS.by = by; mainS.z = z;
			mainS.w = d.tunnelWidth; mainS.h = d.tunnelHeight; mainS.kind = 0;
			std::vector<MineSeg> mine; mine.push_back(mainS);
			// Seitengaenge entlang des Kamms, abwechselnd links/rechts, nur im Berginneren
			for (int i = 0; i < d.mineGalleries; i++) {
				float t = (i + 1.0f) / (d.mineGalleries + 1.0f);        // Position auf dem Hauptstollen
				float px = ax + (bx - ax) * t, py = ay + (by - ay) * t;
				float mm = 0; RidgeTerm(d.ridgeHeight, d.ridgeWidth, d.ridgeSpacing, d.ridgeRough, (int)px, (int)py, &mm);
				if (mm < 0.3f) continue;
				float side = (Hash(q, (uint32_t)i) & 1) ? 1.0f : -1.0f;
				float len = 25.0f + 30.0f * (float)(Hash(q, (uint32_t)i, 7) & 0xFF) / 255.0f;
				MineSeg g; g.ax = px; g.ay = py; g.bx = px - ny * side * len; g.by = py + nx * side * len;
				g.z = z; g.w = d.tunnelWidth > 4 ? d.tunnelWidth - 1 : d.tunnelWidth; g.h = d.tunnelHeight > 4 ? d.tunnelHeight - 1 : d.tunnelHeight; g.kind = 1;
				mine.push_back(g);
			}
			if (d.mineShafts) {
				MineSeg sh; sh.ax = sh.bx = cx; sh.ay = sh.by = cy; sh.z = z; sh.w = 3; sh.h = 0; sh.kind = 2;
				mine.push_back(sh);
			}
			for (const MineSeg& sgm : mine) {
				float minx = std::min(sgm.ax, sgm.bx) - sgm.w - 3, maxx = std::max(sgm.ax, sgm.bx) + sgm.w + 3;
				float miny = std::min(sgm.ay, sgm.by) - sgm.w - 3, maxy = std::max(sgm.ay, sgm.by) + sgm.w + 3;
				if (maxx < x0 || minx > x1 || maxy < y0 || miny > y1) continue;
				out.push_back(sgm);
			}
		}
	}

	bool LakeAt(float lakeShare, float lakeSize, int x, int y, float* dn, int* ocx, int* ocy, float* orad) {
		if (lakeShare <= 0.0f || lakeSize < 4.0f) return false;
		float G = lakeSize * 2.2f;                                  // Rasterfeld je moeglichem See
		double fx = x, fy = y;
		int gx0 = (int)std::floor(fx / G), gy0 = (int)std::floor(fy / G);
		bool any = false; float best = 1e9f;
		for (int gy = gy0 - 1; gy <= gy0 + 1; gy++) for (int gx = gx0 - 1; gx <= gx0 + 1; gx++) {
			uint32_t q = Hash((uint32_t)gx, (uint32_t)gy, 0x5EE5EEu);
			if ((float)(q & 0xFFFF) / 65536.0f >= lakeShare) continue;
			float R = lakeSize * 0.5f * (0.7f + 0.5f * (float)(Hash(q, 2) & 0xFF) / 255.0f);
			double mx = (gx + 0.5) * G + ((double)((q >> 16) & 0xFF) / 255.0 - 0.5) * (G - 2 * R);
			double my = (gy + 0.5) * G + ((double)((q >> 24) & 0xFF) / 255.0 - 0.5) * (G - 2 * R);
			// unregelmaessiges Ufer
			float wob = 1.0f + 0.18f * (ValueNoise2(fx / 14.0f, fy / 14.0f, q) - 0.5f) * 2.0f;
			float d = std::sqrt((fx - mx) * (fx - mx) + (fy - my) * (fy - my)) / (R * wob);
			if (d < best) { best = d; *ocx = (int)mx; *ocy = (int)my; if (orad) *orad = R; any = true; }
		}
		if (any) *dn = best;
		return any;
	}

	static float RiverField(double L, double fx, double fy) {
		return 2.0f * (ValueNoise2(fx / L, fy / L, 0xF1055u) * 0.8f + ValueNoise2(fx * 3 / L, fy * 3 / L, 0xF1056u) * 0.2f) - 1.0f;
	}
	float OwnRiverDistance(float spacing, int x, int y) {
		float L = spacing < 80 ? 80 : spacing;
		double fx = x, fy = y;
		// Abstand zur Nulllinie = |f| / |grad f| (gleichmaessige Breite, auch wo das Rauschen flach ist)
		float f = RiverField(L, fx, fy);
		float gx = (RiverField(L, fx + 1, fy) - RiverField(L, fx - 1, fy)) * 0.5f;
		float gy = (RiverField(L, fx, fy + 1) - RiverField(L, fx, fy - 1)) * 0.5f;
		float g = std::sqrt(gx * gx + gy * gy);
		float gmin = 0.9f / L;                                      // kein Sprung an Sattelpunkten
		if (g < gmin) g = gmin;
		float blocks = std::fabs(f) / g;
		return blocks * (0.02f / 6.0f);                             // 6 Bloecke Halbbreite = Schwelle 0.02
	}

	// =====================================================================
	// Pflanzen
	// =====================================================================
	static uint8_t C8(int v) { return (uint8_t)(v < 1 ? 1 : (v > 255 ? 255 : v)); }    // nie (0,0,0) = leer
	static void PutC(Lab::Structure& s, int x, int y, int z, RGB c, int var, Rng& r) {
		if (!s.In(x, y, z)) return;
		int d = var ? r.Range(-var, var) : 0;
		s.Set(x, y, z, C8(c.r + d), C8(c.g + d), C8(c.b + d));
	}
	static RGB Mul(RGB c, float f) { RGB o; o.r = C8((int)(c.r * f)); o.g = C8((int)(c.g * f)); o.b = C8((int)(c.b * f)); return o; }
	static RGB Mix(RGB a, RGB b, float t) { RGB o; o.r = C8((int)(a.r + (b.r - a.r) * t)); o.g = C8((int)(a.g + (b.g - a.g) * t)); o.b = C8((int)(a.b + (b.b - a.b) * t)); return o; }

	int PickPlantKind(const BiomeDef& b, uint32_t h) {
		int total = 0;
		for (int i = 0; i < PK_COUNT; i++) total += std::max(0, b.plantWeight[i]);
		int fw = b.plantFiles.empty() ? 0 : std::max(0, b.fileWeight);
		total += fw;
		if (total <= 0) return PK_GRASS;
		int r = (int)(h % (uint32_t)total);
		for (int i = 0; i < PK_COUNT; i++) { int w = std::max(0, b.plantWeight[i]); if (r < w) return i; r -= w; }
		return -1;
	}

	static void Ellipsoid(Lab::Structure& s, float cx, float cy, float cz, float rx, float ry, float rz, RGB c, int var, float holes, Rng& r) {
		for (int z = (int)(cz - rz) - 1; z <= (int)(cz + rz) + 1; z++)
			for (int y = (int)(cy - ry) - 1; y <= (int)(cy + ry) + 1; y++)
				for (int x = (int)(cx - rx) - 1; x <= (int)(cx + rx) + 1; x++) {
					float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, dz = (z + 0.5f - cz) / rz;
					float d = dx * dx + dy * dy + dz * dz;
					if (d > 1.0f) continue;
					if (d > 0.55f && r.Unit() < holes) continue;                  // lockerer Rand
					PutC(s, x, y, z, c, var, r);
				}
	}

	Lab::Structure MakePlant(const BiomeDef& b, int kind, uint32_t seed) {
		Rng r(Hash(seed, (uint32_t)kind, 0x51A47u));
		float k = b.plantSize;
		Lab::Structure s; s.origin = "biom: pflanze"; s.name = std::string(PlantKindName(kind));
		switch (kind) {
		case PK_TREE: {
			int h = std::max(3, (int)std::lround(r.Range(5, 9) * k));
			float cr = std::max(1.5f, r.Range(20, 40) / 10.0f * k), crz = std::max(1.5f, cr * (0.7f + r.Unit() * 0.4f));
			int thick = k >= 1.4f ? 2 : 1;
			int W = std::min(15, (int)(cr * 2) + 3), H = std::min(24, h + (int)(crz * 2) + 2);
			s.Create(W, W, H);
			int c0 = W / 2 - (thick - 1) / 2;
			RGB tr = b.trunk;
			for (int z = 0; z < h; z++) for (int dy = 0; dy < thick; dy++) for (int dx = 0; dx < thick; dx++) PutC(s, c0 + dx, c0 + dy, z, tr, 8, r);
			// zwei kurze Aeste
			for (int a = 0; a < 2; a++) {
				int dx = (r.Next() & 1) ? 1 : -1, dy = (r.Next() & 1) ? 1 : -1, z = h - 2 - a;
				PutC(s, c0 + dx, c0, z, tr, 8, r); PutC(s, c0, c0 + dy, z + 1, tr, 8, r);
			}
			Ellipsoid(s, W / 2.0f, W / 2.0f, (float)h + crz * 0.6f, cr, cr, crz, b.leaf, 14, 0.35f, r);
			// Fruechte / Bluehten
			for (int i = 0; i < 6; i++) {
				int x = r.Range(0, W - 1), y = r.Range(0, W - 1), z = r.Range(h, H - 1);
				if (s.Solid(x, y, z)) PutC(s, x, y, z, b.accent, 10, r);
			}
			break;
		}
		case PK_BUSH: {
			float rr = std::max(1.0f, r.Range(12, 22) / 10.0f * k);
			int W = std::min(15, (int)(rr * 2) + 2), H = std::min(24, (int)(rr * 1.6f) + 2);
			s.Create(W, W, H);
			Ellipsoid(s, W / 2.0f, W / 2.0f, rr * 0.7f, rr, rr, rr * 0.8f, Mul(b.leaf, 0.9f), 14, 0.25f, r);
			for (int i = 0; i < 3; i++) { int x = r.Range(0, W - 1), y = r.Range(0, W - 1), z = r.Range(0, H - 1); if (s.Solid(x, y, z)) PutC(s, x, y, z, b.accent, 10, r); }
			break;
		}
		case PK_MUSHROOM: {
			int h = std::max(2, (int)std::lround(r.Range(3, 7) * k));
			float cr = std::max(1.5f, r.Range(20, 45) / 10.0f * k);
			int W = std::min(15, (int)(cr * 2) + 3), H = std::min(24, h + 4);
			s.Create(W, W, H);
			int c = W / 2;
			RGB stem = Mix(b.trunk, RGB{ 235, 230, 215 }, 0.5f);
			for (int z = 0; z < h; z++) PutC(s, c, c, z, stem, 6, r);
			if (k > 1.2f) for (int z = 0; z < h; z++) { PutC(s, c + 1, c, z, stem, 6, r); PutC(s, c, c + 1, z, stem, 6, r); PutC(s, c + 1, c + 1, z, stem, 6, r); }
			// Hut: flache Kuppel, Unterseite heller
			for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
				float dx = x + 0.5f - W / 2.0f, dy = y + 0.5f - W / 2.0f, d = std::sqrt(dx * dx + dy * dy) / cr;
				if (d > 1.0f) continue;
				int top = h + (int)std::lround((1.0f - d * d) * 2.0f);
				for (int z = h; z <= top && z < H; z++) {
					bool spot = z == top && r.Unit() < 0.12f;
					PutC(s, x, y, z, spot ? b.accent : (z == h ? Mul(b.leaf, 1.25f) : b.leaf), 10, r);
				}
			}
			break;
		}
		case PK_CRYSTAL: {
			int n = r.Range(3, 5);
			int L = std::max(3, (int)std::lround(r.Range(4, 8) * k));
			int W = std::min(15, L + 5), H = std::min(24, L + 2);
			s.Create(W, W, H);
			float cx = W / 2.0f, cy = W / 2.0f;
			for (int i = 0; i < n; i++) {
				float ang = r.Unit() * 6.2832f, tilt = 0.15f + r.Unit() * 0.45f;
				int len = i == 0 ? L : std::max(2, L - r.Range(1, 3));
				for (int t = 0; t < len; t++) {
					float x = cx + std::cos(ang) * tilt * t, y = cy + std::sin(ang) * tilt * t;
					RGB col = Mix(b.accent, RGB{ 255, 255, 255 }, (float)t / len * 0.45f);
					PutC(s, (int)x, (int)y, t, col, 8, r);
					if (t < len / 2) PutC(s, (int)x + 1, (int)y, t, Mul(col, 0.85f), 8, r);
				}
			}
			break;
		}
		case PK_CACTUS: {
			int h = std::max(3, (int)std::lround(r.Range(3, 6) * k));
			int W = 7, H = std::min(24, h + 1);
			s.Create(W, W, H);
			int c = W / 2;
			RGB g = Mix(b.leaf, b.trunk, 0.25f);
			for (int z = 0; z < h; z++) PutC(s, c, c, z, g, 8, r);
			int arms = r.Range(0, 2);
			for (int a = 0; a < arms; a++) {
				int dir = a == 0 ? 1 : -1, z0 = r.Range(1, std::max(1, h - 2));
				bool alongX = (r.Next() & 1) != 0;
				int ax = alongX ? c + dir * 2 : c, ay = alongX ? c : c + dir * 2;
				PutC(s, alongX ? c + dir : c, alongX ? c : c + dir, z0, g, 8, r);
				for (int z = z0; z < std::min(H, z0 + r.Range(2, 3)); z++) PutC(s, ax, ay, z, g, 8, r);
			}
			if (r.Unit() < 0.5f) PutC(s, c, c, h - 1 < 0 ? 0 : h - 1, b.accent, 10, r);
			break;
		}
		default: {   // PK_GRASS: Halme / Schilf
			s.Create(5, 5, std::max(2, (int)std::lround(4 * k)));
			int n = r.Range(4, 9);
			for (int i = 0; i < n; i++) {
				int x = r.Range(0, 4), y = r.Range(0, 4), h = r.Range(1, s.sz);
				for (int z = 0; z < h; z++) PutC(s, x, y, z, z == h - 1 && r.Unit() < 0.3f ? b.accent : b.leaf, 16, r);
			}
			break;
		}
		}
		return s;
	}

	// =====================================================================
	// Dungeons: Raeume im Raster, verbunden ueber einen zufaelligen Spannbaum
	// =====================================================================
	DungeonPlan MakeDungeon(const BiomeDef& b, uint32_t seed) {
		Rng r(Hash(seed, 0xD06E04u));
		const int n = std::max(2, std::min(6, b.dungeonRooms));
		const int C = 7;                           // Raumraster: 6 innen + 1 Wand
		const int W = n * C + 1;
		const int Hw = 5;                          // Wandhoehe (z = 1..5), Boden z = 0, Dach z = 6
		const int H = Hw + 2;
		DungeonPlan p;
		p.s.Create(W, W, H);
		p.s.name = "dungeon"; p.s.origin = "biom: dungeon";

		// 1. Spannbaum (Tiefensuche) + ein paar Zusatzwege
		std::vector<int> conE(n * n, 0), conN(n * n, 0);      // Durchgang nach +x bzw. +y
		std::vector<int> seen(n * n, 0), stack;
		int start = r.Range(0, n - 1);                         // Eingangsraum: Zeile y = 0
		stack.push_back(start); seen[start] = 1;
		while (!stack.empty()) {
			int c = stack.back(), cx = c % n, cy = c / n;
			int opts[4], k = 0;
			if (cx + 1 < n && !seen[c + 1]) opts[k++] = 0;
			if (cx > 0 && !seen[c - 1]) opts[k++] = 1;
			if (cy + 1 < n && !seen[c + n]) opts[k++] = 2;
			if (cy > 0 && !seen[c - n]) opts[k++] = 3;
			if (!k) { stack.pop_back(); continue; }
			int o = opts[r.Range(0, k - 1)], nx = c;
			if (o == 0) { conE[c] = 1; nx = c + 1; }
			else if (o == 1) { conE[c - 1] = 1; nx = c - 1; }
			else if (o == 2) { conN[c] = 1; nx = c + n; }
			else { conN[c - n] = 1; nx = c - n; }
			seen[nx] = 1; stack.push_back(nx);
		}
		for (int c = 0; c < n * n; c++) {
			if (c % n + 1 < n && r.Unit() < 0.2f) conE[c] = 1;
			if (c / n + 1 < n && r.Unit() < 0.2f) conN[c] = 1;
		}

		// 2. Entfernungen ab dem Eingang -> Bossraum = weitester Raum
		std::vector<int> dist(n * n, -1), q; dist[start] = 0; q.push_back(start);
		for (size_t qi = 0; qi < q.size(); qi++) {
			int c = q[qi], cx = c % n, cy = c / n;
			int nb[4] = { cx + 1 < n && conE[c] ? c + 1 : -1, cx > 0 && conE[c - 1] ? c - 1 : -1,
			              cy + 1 < n && conN[c] ? c + n : -1, cy > 0 && conN[c - n] ? c - n : -1 };
			for (int v : nb) if (v >= 0 && dist[v] < 0) { dist[v] = dist[c] + 1; q.push_back(v); }
		}
		int boss = start; for (int c = 0; c < n * n; c++) if (dist[c] > dist[boss]) boss = c;

		// 3. Boden
		uint32_t ns = Hash(seed, 7);
		for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			float v = ValueNoise2(x * 0.3f, y * 0.3f, ns);
			RGB c = Mix(b.floor, Mul(b.floor, 0.75f), v);
			if (((x / 2) + (y / 2)) % 2 == 0) c = Mul(c, 1.08f);   // Plattenmuster
			PutC(p.s, x, y, 0, c, 4, r);
		}
		// 4. Waende auf den Rasterlinien
		auto wallCol = [&](int x, int y, int z) {
			float v = ValueNoise2(x * 0.25f + z * 0.5f, y * 0.25f, ns + 1);
			RGB c = Mix(b.wall, Mul(b.wall, 0.7f), v);
			if (b.hum > 0.7f && z <= 2 && r.Unit() < 0.25f) c = Mix(c, RGB{ 60, 110, 50 }, 0.6f);   // Moos in feuchten Biomen
			return c;
		};
		for (int z = 1; z <= Hw; z++)
			for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
				bool onX = x % C == 0, onY = y % C == 0;
				if (!onX && !onY) continue;
				// Durchgaenge (2 breit, 3 hoch) in der Mitte der Wand
				if (z <= 3) {
					int lx = x % C, ly = y % C, cx = x / C, cy = y / C;
					if (onX && !onY && x > 0 && x < W - 1 && (ly == 3 || ly == 4)) { int c = cy * n + (cx - 1); if (conE[c]) continue; }
					if (onY && !onX && y > 0 && y < W - 1 && (lx == 3 || lx == 4)) { int c = (cy - 1) * n + cx; if (conN[c]) continue; }
					// Eingang: Suedwand (y = 0) des Startraums
					if (y == 0 && !onX && x / C == start && (lx == 3 || lx == 4)) continue;
				}
				// Verfall: obere Lagen ausduennen
				if (z >= Hw - 1 && r.Unit() < b.decay * 0.5f) continue;
				PutC(p.s, x, y, z, wallCol(x, y, z), 6, r);
			}
		// 5. Fackeln / Akzente an den Waenden (z = 3), Saeulen in grossen Raeumen
		for (int c = 0; c < n * n; c++) {
			int x0 = (c % n) * C, y0 = (c / n) * C;
			PutC(p.s, x0 + 1, y0 + 1, 3, b.dAccent, 10, r);
			PutC(p.s, x0 + C - 1, y0 + C - 1, 3, b.dAccent, 10, r);
			if (c == boss) {
				for (int y = y0 + 2; y <= y0 + C - 2; y++) for (int x = x0 + 2; x <= x0 + C - 2; x++)
					if (x == x0 + 2 || x == x0 + C - 2 || y == y0 + 2 || y == y0 + C - 2) PutC(p.s, x, y, 0, b.dAccent, 12, r);
			}
		}
		// 6. Dach (mit Loechern bei Verfall)
		if (b.dungeonRoof)
			for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
				float v = ValueNoise2(x * 0.2f, y * 0.2f, ns + 2);
				if (v < b.decay * 0.6f) continue;
				PutC(p.s, x, y, Hw + 1, Mul(wallCol(x, y, Hw + 1), 0.85f), 6, r);
			}
		// 7. Raumliste
		for (int c = 0; c < n * n; c++) {
			DungeonRoom room; room.x = (c % n) * C + C / 2; room.y = (c / n) * C + C / 2; room.z = 1; room.boss = (c == boss);
			p.rooms.push_back(room);
		}
		p.entranceX = start * C + C / 2; p.entranceY = 0;
		return p;
	}
	// Grosse Bergfichte: Stamm 2-3 breit, Krone als gestufter Kegel. Jede Saeule ist von ihrem
	// tiefsten bis zum hoechsten Voxel lueckenlos gefuellt (placeStructure macht Luecken zu Luft).
	Lab::Structure MakeBigTree(const BiomeDef& b, uint32_t seed) {
		Rng r(Hash(seed, 0xB16u));
		float k = b.slopeTreeSize;
		int Ht = std::max(16, std::min(64, (int)std::lround(r.Range(32, 46) * k)));
		float Rm = std::max(4.0f, std::min(12.0f, Ht * (0.18f + 0.04f * r.Unit())));
		int W = 2 * (int)std::ceil(Rm) + 3;
		Lab::Structure s; s.origin = "biom: hangbaum"; s.name = "Bergfichte";
		s.Create(W, W, Ht + 2);
		float c = W / 2.0f;                         // Mitte (W ungerade -> Mitte der Mittelsaeule)
		int thick = Ht >= 38 ? 3 : 2;
		int c0 = W / 2 - (thick - 1) / 2;
		int trunkTop = (int)(Ht * 0.9f);
		int zc0 = (int)(Ht * (0.18f + 0.06f * r.Unit()));
		float tierH = 4.5f + r.Unit() * 1.5f;
		RGB needle = Mul(b.leaf, 0.72f), needleLight = Mul(b.leaf, 0.95f), bark = b.trunk;
		auto Rad = [&](int z) -> float {
			if (z < zc0 || z > Ht) return -1.0f;
			float t = (float)(Ht - z) / (float)(Ht - zc0);
			float rad = Rm * std::pow(t, 0.95f);
			float tier = std::fmod((float)(z - zc0), tierH) / tierH;
			return rad * (0.78f + 0.22f * (1.0f - tier));
		};
		for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			float d = std::sqrt((x + 0.5f - c) * (x + 0.5f - c) + (y + 0.5f - c) * (y + 0.5f - c));
			bool isTrunk = x >= c0 && x < c0 + thick && y >= c0 && y < c0 + thick;
			int zlo = INT_MAX, zhi = INT_MIN;
			float jag = (float)(Hash((uint32_t)x, (uint32_t)y, seed) & 0xFF) / 255.0f * 0.8f;   // ausgefranster Rand
			for (int z = 0; z <= Ht; z++) { float rad = Rad(z); if (rad >= 0 && d <= rad - jag) { zlo = std::min(zlo, z); zhi = std::max(zhi, z); } }
			if (isTrunk) {
				for (int z = 0; z <= trunkTop; z++) PutC(s, x, y, z, bark, 6, r);
				if (zhi >= trunkTop) for (int z = trunkTop + 1; z <= zhi; z++) PutC(s, x, y, z, needle, 10, r);
				continue;
			}
			if (zlo > zhi) continue;
			for (int z = zlo; z <= zhi; z++) {
				float rad = Rad(z);
				bool edge = rad < 0 || d > rad - 1.2f;
				PutC(s, x, y, z, (edge && (Hash((uint32_t)x, (uint32_t)y, (uint32_t)z) & 7) == 0) ? needleLight : needle, 10, r);
			}
		}
		// Spitze
		int cx = W / 2;
		PutC(s, cx, cx, Ht, needle, 6, r); PutC(s, cx, cx, Ht + 1, needleLight, 6, r);
		// Saeulen lueckenlos machen (placeStructure setzt Luecken innerhalb einer Saeule auf Luft)
		for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
			int lo = -1, hi = -1;
			for (int z = 0; z < s.sz; z++) if (s.Solid(x, y, z)) { if (lo < 0) lo = z; hi = z; }
			for (int z = lo + 1; lo >= 0 && z < hi; z++) if (!s.Solid(x, y, z)) PutC(s, x, y, z, needle, 6, r);
		}
		return s;
	}

	Lab::Structure MakeMiniFlower(const Lab::Structure& srcIn, int foot, int maxH) {
		Lab::Structure src = srcIn;
		Lab::Structure out;
		if (!src.Valid()) return out;
		src.CropToContent();
		if (!src.Valid()) return out;
		foot = std::max(1, std::min(4, foot)); maxH = std::max(1, std::min(16, maxH));
		int big = std::max(src.sx, src.sy);
		int H = (int)std::lround((double)src.sz * foot / big);
		H = std::max(1, std::min(maxH, H));
		if (src.sz > big && H < 2 && maxH >= 2) H = 2;                    // hohe Blumen: mindestens Stiel + Bluete
		int F = foot;
		// Durchschnittsfarbe des Modells
		double mr = 0, mg = 0, mb = 0; long n = 0;
		for (int z = 0; z < src.sz; z++) for (int y = 0; y < src.sy; y++) for (int x = 0; x < src.sx; x++) {
			uint8_t r, g, b; if (src.Get(x, y, z, r, g, b)) { mr += r; mg += g; mb += b; n++; }
		}
		if (!n) return out;
		mr /= n; mg /= n; mb /= n;
		out.Create(F, F, H); out.name = src.name + "-mini"; out.origin = "mini-blume";
		std::vector<int> cnt((size_t)F * F * H, 0), vol((size_t)F * F * H, 0);
		std::vector<RGB> col((size_t)F * F * H);
		for (int k = 0; k < H; k++) for (int j = 0; j < F; j++) for (int i = 0; i < F; i++) {
			int x0 = i * src.sx / F, x1 = (i + 1) * src.sx / F, y0 = j * src.sy / F, y1 = (j + 1) * src.sy / F;
			int z0 = k * src.sz / H, z1 = (k + 1) * src.sz / H;
			if (x1 <= x0) x1 = x0 + 1; if (y1 <= y0) y1 = y0 + 1; if (z1 <= z0) z1 = z0 + 1;
			std::map<uint32_t, double> hist; int c = 0;
			for (int z = z0; z < z1; z++) for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
				uint8_t r, g, b;
				if (!src.Get(x, y, z, r, g, b)) continue;
				c++;
				uint32_t key = ((uint32_t)(r >> 3) << 10) | ((uint32_t)(g >> 3) << 5) | (uint32_t)(b >> 3);
				double dist = std::sqrt((r - mr) * (r - mr) + (g - mg) * (g - mg) + (b - mb) * (b - mb)) / 255.0;
				hist[key] += 1.0 + 3.0 * dist;                       // auffaellige Farben (Blueten) wiegen mehr
			}
			size_t idx = ((size_t)k * F + j) * F + i;
			cnt[idx] = c; vol[idx] = (x1 - x0) * (y1 - y0) * (z1 - z0);
			if (c) {
				uint32_t bestK = 0; double bw = -1;
				for (const auto& e : hist) if (e.second > bw) { bw = e.second; bestK = e.first; }
				RGB o; o.r = (uint8_t)(((bestK >> 10) & 31) * 8 + 4); o.g = (uint8_t)(((bestK >> 5) & 31) * 8 + 4); o.b = (uint8_t)((bestK & 31) * 8 + 4);
				if ((o.r | o.g | o.b) == 0) o.r = 1;                    // reines Schwarz ist "leer"
				col[idx] = o;
			}
		}
		// 1. Zellen mit genug Inhalt; 2. darunter alles, was etwas enthaelt (Stiel)
		std::vector<char> on((size_t)F * F * H, 0);
		const double fill = 0.12;
		for (size_t q = 0; q < on.size(); q++) on[q] = cnt[q] > 0 && (double)cnt[q] / vol[q] >= fill;
		bool any = false; for (char c : on) if (c) any = true;
		if (!any) for (size_t q = 0; q < on.size(); q++) on[q] = cnt[q] > 0;   // sehr duennes Modell
		for (int j = 0; j < F; j++) for (int i = 0; i < F; i++) {
			int top = -1;
			for (int k = H - 1; k >= 0; k--) if (on[((size_t)k * F + j) * F + i]) { top = k; break; }
			for (int k = 0; k < top; k++) { size_t q = ((size_t)k * F + j) * F + i; if (cnt[q] > 0) on[q] = 1; }
		}
		// Die Blume muss stehen: unterste Lage mindestens eine Zelle
		bool ground = false; for (int j = 0; j < F; j++) for (int i = 0; i < F; i++) if (on[((size_t)0 * F + j) * F + i]) ground = true;
		if (!ground) {
			int bi = 0, bc = -1;
			for (int j = 0; j < F; j++) for (int i = 0; i < F; i++) if (cnt[(size_t)j * F + i] > bc) { bc = cnt[(size_t)j * F + i]; bi = j * F + i; }
			if (bc > 0) on[bi] = 1;
		}
		for (int k = 0; k < H; k++) for (int j = 0; j < F; j++) for (int i = 0; i < F; i++) {
			size_t q = ((size_t)k * F + j) * F + i;
			if (on[q]) out.Set(i, j, k, col[q].r, col[q].g, col[q].b);
		}
		return out;
	}

}
