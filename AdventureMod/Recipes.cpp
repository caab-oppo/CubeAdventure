#include "Recipes.h"
#include "Host.h"
#include "LabStructure.h"
#include "Items.h"
#include "MinHook.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace Recipes {
	static const uint32_t A_RECIPE_GET = 0x59CFF0, A_ITEM_COPY = 0x42C5E0, A_VEC_PUSH_ING = 0x5A0D80;
	static const uint32_t A_IS_KNOWN = 0x444A90, A_PUSH_CRAFT = 0x44D460;
	static const uint32_t GC_PLAYER = 0x8006D0, CR_CRAFTDATA = 0x1D28, CD_LIST = 0x14;
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	const int kStations[9] = { 0, 1, 3, 4, 5, 6, 7, 8, 9 };
	const char* EffectName(int k) {
		switch (k) {
			case FX_NONE: return "keine (normaler Gegenstand)"; case FX_HEAL: return "Heilen beim Trinken (wie bisher)";
			case FX_REGEN: return "Regeneration"; case FX_STRENGTH: return "Staerke (mehr Schaden)";
			case FX_PROTECT: return "Schutz (Ruestung/Widerstand)"; case FX_SPEED: return "Schnelligkeit";
			case FX_SLOWFALL: return "Langsames Fallen"; case FX_NIGHT: return "Nachtsicht";
			case FX_MANA: return "Mana-Fluss"; case FX_STAMINA: return "Ausdauer (nie erschoepft)";
			default: return "?";
		}
	}
	const char* EffectKey(int k) {
		static const char* keys[FX_COUNT] = { "keine", "heilen", "regeneration", "staerke", "schutz", "schnelligkeit",
		                                      "langsam_fallen", "nachtsicht", "mana", "ausdauer" };
		return (k >= 0 && k < FX_COUNT) ? keys[k] : "keine";
	}
	const char* EffectUnit(int k) {
		switch (k) {
			case FX_REGEN: return "% der max. Lebenspunkte je Sekunde"; case FX_STRENGTH: return "% mehr Schaden";
			case FX_PROTECT: return "% mehr Ruestung und Widerstand"; case FX_SPEED: return "% schneller laufen";
			case FX_SLOWFALL: return "hoechste Fallgeschwindigkeit (Bloecke/s)"; case FX_NIGHT: return "% Helligkeit (100 = wie mittags)";
			case FX_MANA: return "% Mana je Sekunde"; case FX_STAMINA: return "(kein Wert noetig)";
			default: return "";
		}
	}
	float EffectDefaultValue(int k) {
		switch (k) {
			case FX_REGEN: return 2.0f; case FX_STRENGTH: return 50.0f; case FX_PROTECT: return 50.0f; case FX_SPEED: return 40.0f;
			case FX_SLOWFALL: return 5.0f; case FX_NIGHT: return 80.0f; case FX_MANA: return 10.0f; default: return 0.0f;
		}
	}
	const char* StationName(int c) {
		switch (c) {
			case 0: return "keine"; case 1: return "Wasser"; case 3: return "Ofen"; case 4: return "Amboss";
			case 5: return "Spinnrad"; case 6: return "Webstuhl"; case 7: return "Saege"; case 8: return "Werkbank";
			case 9: return "Lagerfeuer / Braustand"; default: return "?";
		}
	}
	// Namen aus item_icon_id @0x4EC400 (Modell je Typ/Unterart/Material)
	// Typ/Unterart/Material wie das Spiel sie vergibt (belegt): Glasflasche 0x0C mit Material Glas 0x18
	// (Startinventar game_misc.cpp:34762, Haendler GameController.cpp:57419, Kaktustrank-Rezept 0x59CFF0),
	// Schimmerpilz 0x13 mit Material 9 (Hoehlenfund WorldInfo.cpp:2064), Papageienfeder Material 0x13 (Papagei),
	// Pflanzenfaser 0x15 (Pflanze), Holz 2, Baumwolle 0x1B (Kreaturen-Beute game_misc.cpp:18976ff.).
	// Edelsteine tragen dort zusaetzlich eine Seltenheit (Smaragd 1 .. Diamant 4) -- setzt BuildRaw.
	static const Named kPresets[] = {
		{ "Herzblume", 0x0B, 0x16, 0 }, { "Frost-Herzblume", 0x0B, 0x18, 0 }, { "Seelenblume", 0x0B, 0x19, 0 },
		{ "Pilz", 0x0B, 0x0F, 0 }, { "Schimmerpilz", 0x0B, 0x13, 0x09 }, { "Kuerbis", 0x0B, 0x10, 0 },
		{ "Ananas", 0x0B, 0x11, 0 }, { "Rettich", 0x0B, 0x12, 0 }, { "Ginseng", 0x0B, 0x14, 0 },
		{ "Zwiebel", 0x0B, 0x15, 0 }, { "Kaktusfeige", 0x0B, 0x17, 0 }, { "Schneebeere", 0x0B, 0x1B, 0 },
		{ "Wasserflasche", 0x0B, 0x1A, 0 }, { "Glasflasche", 0x0B, 0x0C, 0x18 }, { "Holz", 0x0B, 0x01, 0x02 },
		{ "Eisennugget", 0x0B, 0x00, 1 }, { "Silbernugget", 0x0B, 0x00, 0x0C }, { "Goldnugget", 0x0B, 0x00, 0x0B },
		{ "Smaragd", 0x0B, 0x00, 0x0D }, { "Saphir", 0x0B, 0x00, 0x0E }, { "Rubin", 0x0B, 0x00, 0x0F },
		{ "Diamant", 0x0B, 0x00, 0x10 }, { "Sandstein", 0x0B, 0x00, 0x11 }, { "Papageienfeder", 0x0B, 0x02, 0x13 },
		{ "Pflanzenfaser", 0x0B, 0x05, 0x15 }, { "Spinnweben", 0x0B, 0x06, 0 }, { "Mammuthaar", 0x0B, 0x07, 0 },
		{ "Baumwolle", 0x0B, 0x0B, 0x1B }, { "Eisenwuerfel", 0x0B, 0x0A, 1 }, { "Holzwuerfel", 0x0B, 0x0A, 2 },
		{ "Lebenstrank (Trank, trinken 3 s)", 0x01, 0x01, 0 }, { "Kaktustrank (Trank, trinken 10 s)", 0x01, 0x02, 0 },
		{ "Ginsengsuppe (essen 10 s)", 0x01, 0x04, 0 }, { "Schneebeerensaft (essen 10 s)", 0x01, 0x05, 0 },
		{ "Pilzspiess (essen 10 s)", 0x01, 0x06, 0 }, { "Ananasscheibe (essen 10 s)", 0x01, 0x08, 0 },
		{ "Kuerbismuffin (essen 10 s)", 0x01, 0x09, 0 },
	};
	const Named* Presets(int* n) { *n = (int)(sizeof(kPresets) / sizeof(kPresets[0])); return kPresets; }

	// ---- Dateien ----------------------------------------------------------------
	std::string RecipesDir() { return Lab::StructuresDir() + "\\Rezepte"; }
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	static std::vector<int> Nums(const std::string& v) {
		std::vector<int> r; size_t p = 0;
		while (p <= v.size()) { size_t c = v.find(',', p); std::string t = Trim(v.substr(p, c == std::string::npos ? std::string::npos : c - p)); if (!t.empty()) r.push_back((int)strtol(t.c_str(), nullptr, 0)); if (c == std::string::npos) break; p = c + 1; }
		return r;
	}
	// Trank-/Speise-Formen, die das Spiel trinken (0x50) oder essen (0x51) laesst (useItem 0x4A2780;
	// Unterart 7 = Bombe ist ausgeschlossen).
	static bool DrinkableSub(int s) { return s == 1 || s == 2 || s == 4 || s == 5 || s == 6 || s == 8 || s == 9; }
	void NormalizeEffect(RecipeDef& r) {
		if (r.effect < FX_NONE || r.effect >= FX_COUNT) r.effect = FX_NONE;
		if (r.effect != FX_NONE) {                  // Wirkungen gibt es nur fuer trinkbare/essbare Gegenstaende
			r.outType = 1;
			if (!DrinkableSub(r.outSub)) r.outSub = 1;
			r.outMat = 0;
		}
		if (!NeedsTag(r)) r.effectId = 0;
	}
	const char* UseText(int type, int sub) {
		if (type == 1) {
			if (sub == 1 || sub == 2) return "wird getrunken (Trank)";
			if (sub == 4 || sub == 5 || sub == 6 || sub == 8 || sub == 9) return "wird gegessen (Speise)";
			if (sub == 7) return "Bombe: wird nicht getrunken/gegessen";
			return "Typ 1, Unterart wird vom Spiel nicht benutzt";
		}
		if (type == 3) return "Waffe: wird ausgeruestet, nicht verbraucht";
		if (type >= 4 && type <= 7) return "Ruestung: wird getragen, nicht verbraucht";
		if (type == 8 || type == 9) return "Schmuck: wird getragen, nicht verbraucht";
		if (type == 0x0B) return "Material/Zutat: wird nicht verbraucht (nur zum Herstellen)";
		return "wird nicht verbraucht";
	}
	bool Load(const std::string& path, RecipeDef& d, std::string& err) {
		std::ifstream f(path); if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		RecipeDef r; r.ing.clear(); std::string line;
		while (std::getline(f, line)) {
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			std::vector<int> n = Nums(v);
			if (k == "name") r.name = v;
			else if (k == "aktiv") r.enabled = atoi(v.c_str()) != 0;
			else if (k == "ergebnis" && n.size() >= 2) { r.outType = n[0] & 0xFF; r.outSub = n[1] & 0xFF; r.outMat = n.size() > 2 ? (n[2] & 0xFF) : 0; }
			else if (k == "ergebnis_level") r.outLevel = (std::max)(1, (std::min)(65535, atoi(v.c_str())));
			else if (k == "ergebnis_seltenheit") r.outRarity = (std::max)(0, (std::min)(4, atoi(v.c_str())));
			else if (k == "station") r.station = atoi(v.c_str());
			else if (k == "sofort_bekannt") r.learnAtStart = atoi(v.c_str()) != 0;
			else if (k.compare(0, 5, "zutat") == 0 && n.size() >= 4 && r.ing.size() < 6) { Ingredient g; g.type = n[0] & 0xFF; g.sub = n[1] & 0xFF; g.mat = n[2] & 0xFF; g.count = (std::max)(1, (std::min)(999, n[3])); g.item = n.size() > 4 ? (std::max)(0, n[4]) : 0; r.ing.push_back(g); }
			else if (k == "ergebnis_gegenstand") r.outItem = (std::max)(0, atoi(v.c_str()));
			else if (k == "wirkung") { r.effect = FX_NONE; for (int e = 0; e < FX_COUNT; e++) if (v == EffectKey(e)) r.effect = e; }
			else if (k == "wirkung_wert") r.effectValue = (float)atof(v.c_str());
			else if (k == "wirkung_dauer") r.effectSeconds = (std::max)(1, (std::min)(3600, atoi(v.c_str())));
			else if (k == "wirkung_auch_heilen") r.effectAlsoHeal = atoi(v.c_str()) != 0;
			else if (k == "effekt_id") r.effectId = (std::max)(0, (std::min)(0xFFFF, atoi(v.c_str())));
			else if (k == "modell") r.modelFile = v;
		}
		// Alte Dateien: Zutaten mit Material 0, die es im Spiel so nicht gibt -> richtiges Material (siehe kPresets)
		auto Fix = [](int type, int sub, int& mat) {
			if (type != 0x0B || mat != 0) return false;
			static const int kFix[][2] = { { 0x0C, 0x18 }, { 0x13, 0x09 }, { 0x02, 0x13 }, { 0x05, 0x15 }, { 0x01, 0x02 }, { 0x0B, 0x1B } };
			for (auto& f : kFix) if (f[0] == sub) { mat = f[1]; return true; }
			return false;
		};
		int fixed = 0;
		for (Ingredient& g : r.ing) if (g.item == 0 && Fix(g.type, g.sub, g.mat)) fixed++;
		if (r.outItem == 0 && Fix(r.outType, r.outSub, r.outMat)) fixed++;
		if (fixed) std::cout << "[Rezepte] " << path << ": " << fixed << " Zutat(en) auf das Material des Spiels korrigiert (z. B. Glasflasche = Glas, Schimmerpilz = Material 9)." << std::endl;
		NormalizeEffect(r);
		d = r; return true;
	}
	bool Save(const std::string& path, const RecipeDef& d, std::string& err) {
		std::ofstream f(path); if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[96];
		f << "# Eigenes Rezept (LAB -> Rezepte). Typ/Unterart/Material als Zahlen, z. B. Herzblume 0x0B,0x16,0\n";
		f << "name=" << d.name << "\naktiv=" << (d.enabled ? 1 : 0) << "\n";
		sprintf_s(b, "0x%02X,0x%02X,0x%02X", d.outType, d.outSub, d.outMat); f << "ergebnis=" << b << "\n";
		f << "ergebnis_level=" << d.outLevel << "\nergebnis_seltenheit=" << d.outRarity << "\n";
		f << "# station: 0 keine, 1 Wasser, 3 Ofen, 4 Amboss, 5 Spinnrad, 6 Webstuhl, 7 Saege, 8 Werkbank, 9 Lagerfeuer\n";
		f << "station=" << d.station << "\nsofort_bekannt=" << (d.learnAtStart ? 1 : 0) << "\n";
		for (size_t i = 0; i < d.ing.size(); i++) {
			if (d.ing[i].item > 0) sprintf_s(b, "zutat%d=0x%02X,0x%02X,0x%02X,%d,%d", (int)i + 1, d.ing[i].type, d.ing[i].sub, d.ing[i].mat, d.ing[i].count, d.ing[i].item);
			else sprintf_s(b, "zutat%d=0x%02X,0x%02X,0x%02X,%d", (int)i + 1, d.ing[i].type, d.ing[i].sub, d.ing[i].mat, d.ing[i].count);
			f << b << "\n";
		}
		f << "# ergebnis_gegenstand: Kennung eines eigenen Gegenstands (LAB -> Gegenstaende), 0 = nativer Gegenstand\n";
		f << "ergebnis_gegenstand=" << d.outItem << "\n";
		f << "# wirkung: keine, heilen, regeneration, staerke, schutz, schnelligkeit, langsam_fallen, nachtsicht, mana, ausdauer\n";
		f << "wirkung=" << EffectKey(d.effect) << "\n";
		if (d.effect != FX_NONE) {
			sprintf_s(b, "%.2f", d.effectValue);
			f << "wirkung_wert=" << b << "\nwirkung_dauer=" << d.effectSeconds << "\nwirkung_auch_heilen=" << (d.effectAlsoHeal ? 1 : 0) << "\n";
		}
		f << "# modell: eigenes optisches Modell des Ergebnisses (.cub, relativ zu Strukturen\\), leer = Modell des Spiels\n";
		f << "modell=" << d.modelFile << "\n";
		if (d.effectId > 0) f << "effekt_id=" << d.effectId << "\n";
		return true;
	}
	std::vector<std::string> ListFiles() {
		std::vector<std::string> out; WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((RecipesDir() + "\\*.rez").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
		std::sort(out.begin(), out.end()); return out;
	}

	// ---- Laufzeit ---------------------------------------------------------------
	struct RT { RecipeDef d; std::string file; std::wstring wname; Lab::Structure model; };
	static std::vector<RT>* volatile g_set = nullptr;        // alte Saetze werden nie freigegeben
	static volatile LONG g_ver = 1, g_learned = 0, g_served = 0, g_models = 0;
	static bool g_hookOk = false;

	typedef char (__stdcall* RecipeGet_t)(const uint8_t* result, uint8_t* out);
	typedef void* (__fastcall* ItemCopy_t)(void* dst, void* edx, const void* src);
	typedef void  (__fastcall* VecPush_t)(void* vec, void* edx, const void* rec);
	typedef char  (__fastcall* IsKnown_t)(void* creature, void* edx, const void* item);
	typedef void  (__fastcall* PushCraft_t)(void* list, void* edx, const void* item);
	static RecipeGet_t o_RecipeGet = nullptr;

	bool EffectIdOf(const uint8_t* item, int* id) {
		if (!item || item[0] != 1) return false;
		uint32_t m = *(const uint32_t*)(item + 4);
		if ((m & 0xFFFF0000u) != kEffectTag) return false;
		if (id) *id = (int)(m & 0xFFFFu);
		return true;
	}
	bool TagOf(const uint8_t* item, int* id) {
		if (!item) return false;
		uint32_t m = *(const uint32_t*)(item + 4);
		if ((m & 0xFFFF0000u) != kEffectTag || (m & 0xFFFFu) == 0) return false;
		if (id) *id = (int)(m & 0xFFFFu);
		return true;
	}
	// Rezepte ohne Kennung passen nur auf Gegenstaende OHNE Kennung, Rezepte mit Kennung nur auf ihre Kennung.
	static const RT* Find(const uint8_t* item) {
		std::vector<RT>* s = g_set; if (!s || !item) return nullptr;
		int id = 0; bool tagged = TagOf(item, &id);
		uint32_t mod = *(const uint32_t*)(item + 4);
		bool itemTagged = (mod & 0xFFFF0000u) == Items::kItemTag && (mod & 0xFFFFu) != 0;
		// Seltenheit (2026-10-19): Rezepte mit Seltenheit > 0 (z.B. verbesserte Lampen) gelten nur fuer genau diese
		// Seltenheit -- so stehen mehrere Stufen desselben Gegenstands nebeneinander, und das native Rezept (Seltenheit 0) bleibt.
		for (const RT& r : *s) {
			if (!r.d.enabled || r.d.outType != item[0] || r.d.outSub != item[1] || r.d.outMat != item[0x0D]) continue;
			if (r.d.outRarity > 0 && r.d.outRarity != item[0x0C]) continue;
			if (r.d.outItem > 0) { if (mod == Items::Tag(r.d.outItem)) return &r; continue; }   // eigener Gegenstand als Ergebnis
			if (itemTagged) continue;
			bool rt = NeedsTag(r.d) && r.d.effectId > 0;
			if (rt ? (tagged && id == r.d.effectId) : !tagged) return &r;
		}
		return nullptr;
	}
	static const RT* FindEffectId(int id) {
		std::vector<RT>* s = g_set; if (!s || id <= 0) return nullptr;
		for (const RT& r : *s) if (EffectTagged(r.d.effect) && r.d.effectId == id) return &r;
		return nullptr;
	}
	bool EffectById(int id, EffectInfo* out) {
		const RT* r = FindEffectId(id); if (!r) return false;
		if (out) { out->kind = r->d.effect; out->value = r->d.effectValue; out->seconds = r->d.effectSeconds; out->alsoHeal = r->d.effectAlsoHeal; out->name = r->wname; }
		return true;
	}
	// Rezept zu einer Kennung, passend zu Typ/Unterart des Gegenstands (schuetzt vor zufaellig gleichen Modifiern)
	static const RT* FindTag(const uint8_t* item, int id) {
		std::vector<RT>* s = g_set; if (!s || id <= 0) return nullptr;
		for (const RT& r : *s) if (NeedsTag(r.d) && r.d.effectId == id && r.d.outType == item[0] && r.d.outSub == item[1]) return &r;
		return nullptr;
	}
	bool ItemName(const uint8_t* item, std::wstring* out) {
		int id = 0; if (!TagOf(item, &id)) return false;
		const RT* r = FindTag(item, id);
		if (r) { if (out) *out = r->wname; return true; }
		if (item[0] != 1) return false;                                   // fremder Modifier -> Name des Spiels
		if (out) *out = L"Unbekannter Effekttrank";
		return true;
	}
	bool IsEffectRecipeItem(const uint8_t* item) { int id = 0; return EffectIdOf(item, &id) && FindEffectId(id) != nullptr; }
	long ModelVersion() { return g_ver; }
	void Models(std::vector<ModelEntry>& out) {
		std::vector<RT>* s = g_set; if (!s) return;
		for (const RT& r : *s) if (r.d.enabled && r.d.outItem == 0 && r.d.effectId > 0 && r.model.Valid()) out.push_back(ModelEntry{ r.d.effectId, r.d.outType, r.d.outSub, r.model, r.d.modelFile });
	}
	int NextEffectId() {
		int mx = 0; std::vector<RT>* s = g_set;
		if (s) for (const RT& r : *s) mx = (std::max)(mx, r.d.effectId);
		return (std::min)(0xFFFF, mx + 1);
	}
	static bool BuildRaw(const RecipeDef* d, const uint8_t* result, uint8_t* out) {
		__try {
			((ItemCopy_t)Re(A_ITEM_COPY))(out, nullptr, result);
			*(int*)(out + 0x118) = d->station;
			for (size_t i = 0; i < d->ing.size(); i++) {
				alignas(8) uint8_t rec[0x11C]; memset(rec, 0, sizeof(rec));
				rec[0] = (uint8_t)d->ing[i].type; rec[1] = (uint8_t)d->ing[i].sub; rec[0x0D] = (uint8_t)d->ing[i].mat;
				if (rec[0] == 0x0B && rec[1] == 0x00 && rec[0x0D] >= 0x0D && rec[0x0D] <= 0x10) rec[0x0C] = (uint8_t)(rec[0x0D] - 0x0C);   // Edelstein: Seltenheit wie im Spiel
				*(uint16_t*)(rec + 0x10) = 1;
				if (d->ing[i].item > 0) *(uint32_t*)(rec + 4) = Items::Tag(d->ing[i].item);   // eigener Gegenstand als Zutat
				*(int*)(rec + 0x118) = d->ing[i].count;
				((VecPush_t)Re(A_VEC_PUSH_ING))(out + 0x11C, nullptr, rec);
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static char __stdcall Hook_RecipeGet(const uint8_t* result, uint8_t* out) {
		const RT* r = Find(result);
		if (r && BuildRaw(&r->d, result, out)) { InterlockedIncrement(&g_served); return 1; }
		return o_RecipeGet(result, out);
	}

	static int LearnRaw(uint8_t* gc, const RecipeDef* d) {
		__try {
			uint8_t* creature = *(uint8_t**)(gc + GC_PLAYER); if (!creature) return -1;
			uint8_t* craftData = *(uint8_t**)(creature + CR_CRAFTDATA); if (!craftData) return -1;
			::Item rec; memset(&rec, 0, sizeof(rec));
			rec.category_id = (uint8_t)d->outType; rec.item_id = (uint8_t)d->outSub; rec.material = (uint8_t)d->outMat;
			rec.level = (uint16_t)d->outLevel; rec.rarity = (uint8_t)d->outRarity;
			if (d->outItem > 0) *(uint32_t*)((uint8_t*)&rec + 4) = Items::Tag(d->outItem);
			else if (NeedsTag(*d) && d->effectId > 0) *(uint32_t*)((uint8_t*)&rec + 4) = EffectModifier(d->effectId);
			if (((IsKnown_t)Re(A_IS_KNOWN))(creature, nullptr, &rec)) return 0;
			((PushCraft_t)Re(A_PUSH_CRAFT))(craftData + CD_LIST, nullptr, &rec);
			return 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	static void* PlayerRaw(uint8_t* gc) { __try { return *(void**)(gc + GC_PLAYER); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
	void Tick(cube::GameController* gc) {
		static DWORD last = 0; static LONG lastVer = 0; static void* lastPlayer = nullptr;
		if (!gc || !g_hookOk) return;
		DWORD now = GetTickCount();
		void* player = PlayerRaw((uint8_t*)gc);
		if (!player) return;
		if (lastVer == g_ver && player == lastPlayer && now - last < 5000) return;
		last = now; lastVer = g_ver; lastPlayer = player;
		std::vector<RT>* s = g_set; if (!s) return;
		int n = 0;
		for (const RT& r : *s) if (r.d.enabled && r.d.learnAtStart && LearnRaw((uint8_t*)gc, &r.d) == 1) n++;
		if (n) { InterlockedExchangeAdd(&g_learned, n); std::cout << "[Rezepte] " << n << " eigene Rezepte gelernt (Handwerksmenue neu oeffnen)." << std::endl; }
	}
	std::string ModelPath(const std::string& f) {
		if (f.size() > 1 && (f[1] == ':' || f[0] == '\\' || f[0] == '/')) return f;
		return Lab::StructuresDir() + "\\" + f;
	}
	void Reload() {
		InterlockedExchange(&g_models, 0);
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(RecipesDir());
		std::vector<RT>* s = new std::vector<RT>();
		for (const std::string& f : ListFiles()) {
			RT r; std::string e; r.file = f;
			if (!Load(RecipesDir() + "\\" + f, r.d, e)) { std::cout << "[Rezepte] " << e << std::endl; continue; }
			wchar_t w[96] = { 0 }; MultiByteToWideChar(CP_ACP, 0, r.d.name.c_str(), -1, w, 95); r.wname = w;
			if (NeedsTag(r.d) && r.d.effectId <= 0) std::cout << "[Rezepte] " << f << ": Wirkung/Modell ohne effekt_id -- im LAB neu speichern." << std::endl;
			if (!r.d.modelFile.empty()) {
				std::string mp = ModelPath(r.d.modelFile), me;
				if (!Lab::LoadCub(mp, r.model, me)) { std::cout << "[Rezepte] " << f << ": Modell nicht lesbar (" << me << ")." << std::endl; r.model = Lab::Structure(); }
				else if (r.model.sx > 64 || r.model.sy > 64 || r.model.sz > 64) { std::cout << "[Rezepte] " << f << ": Modell groesser als 64 -- nicht benutzt." << std::endl; r.model = Lab::Structure(); }
				else InterlockedIncrement(&g_models);
			}
			s->push_back(r);
		}
		InterlockedExchangePointer((PVOID volatile*)&g_set, s);
		InterlockedIncrement(&g_ver);
		std::cout << "[Rezepte] " << s->size() << " eigene Rezepte geladen (Strukturen\\Rezepte)." << std::endl;
	}
	static bool PrologueOk() {
		static const uint8_t pro[6] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x20 };
		__try { return memcmp((const void*)Re(A_RECIPE_GET), pro, 6) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void Install() {
		g_hookOk = PrologueOk() && MH_CreateHook((LPVOID)Re(A_RECIPE_GET), (LPVOID)&Hook_RecipeGet, (LPVOID*)&o_RecipeGet) == MH_OK;
		std::cout << "[Rezepte] Rezeptdatenbank 0x59CFF0 " << (g_hookOk ? "umgeleitet." : "NICHT umgeleitet (Bytes weichen ab).") << std::endl;
		Reload();
	}
	std::string Status() {
		std::vector<RT>* s = g_set;
		char b[240]; sprintf_s(b, "Rezepte: %d geladen, %ld mit eigenem Modell, Hook %s, angelernt %ld, vom Spiel abgefragt %ld", s ? (int)s->size() : 0, g_models, g_hookOk ? "ok" : "FEHLT", g_learned, g_served);
		return b;
	}
}
