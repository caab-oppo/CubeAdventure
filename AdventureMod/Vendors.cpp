#include "Vendors.h"
#include "Host.h"
#include "LabStructure.h"
#include "Items.h"
#include "Recipes.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>

// Herleitung: Vendors.h.

namespace Vendors {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	const char* RoleHelp() {
		return "Rollen: markt = eigener Haendler (Marktplatz, Mutterbaum, Festung, Bergdorf), alchemist = am Braustand, "
		       "huette1..huette5 = Haendlerhaeuser im Handelsviertel.";
	}
	std::string Dir() { return Lab::StructuresDir() + "\\Haendler"; }
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	static int Num(const std::string& v) { std::string t = Trim(v); return (int)strtol(t.c_str(), nullptr, (t.size() > 1 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) ? 16 : 10); }
	bool Load(const std::string& path, VendorDef& d, std::string& err) {
		std::ifstream f(path); if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		VendorDef r; std::string line;
		while (std::getline(f, line)) {
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			if (k == "rolle") r.role = v;
			else if (k == "name") r.name = v;
			else if (k == "klasse") r.cls = Num(v) & 0xFF;
			else if (k == "native_waren") r.nativeWares = Num(v) != 0;
			else if (k == "zutaten") r.ingredients = Num(v) != 0;
			else if (k == "traenke") r.potions = Num(v) != 0;
			else if (k == "gegenstaende") r.items = Num(v) != 0;
			else if (k == "anzahl") r.defaultCount = (std::max)(1, (std::min)(999, Num(v)));
			else if (k == "ware") {
				Ware w; w.count = r.defaultCount;
				if (v.compare(0, 5, "item:") == 0) {
					size_t sc = v.find(';'); w.item = Num(v.substr(5, sc == std::string::npos ? std::string::npos : sc - 5));
					if (sc != std::string::npos) w.count = Num(v.substr(sc + 1));
				} else {
					int t[6] = { 0x0B, 0, 0, 1, 0, r.defaultCount }; int n = 0; size_t p = 0;
					while (n < 6 && p <= v.size()) { size_t q = v.find(';', p); t[n++] = Num(v.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
					w.type = t[0]; w.sub = t[1]; w.mat = t[2]; w.level = (std::max)(1, t[3]); w.rarity = (std::max)(0, (std::min)(4, t[4])); w.count = t[5];
				}
				w.count = (std::max)(1, (std::min)(999, w.count));
				if ((int)r.wares.size() < 120) r.wares.push_back(w);
			}
		}
		d = r; return true;
	}
	bool Save(const std::string& path, const VendorDef& d, std::string& err) {
		std::ofstream f(path); if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[96];
		f << "# Haendler (LAB -> Haendler). " << RoleHelp() << "\n";
		f << "rolle=" << d.role << "\nname=" << d.name << "\n";
		sprintf_s(b, "0x%02X", d.cls); f << "# klasse: 0x80 Haendler, 0x81..0x83 Dorf-NPC, 0x84 Gastwirt\nklasse=" << b << "\n";
		f << "# native_waren: Waren wie im Dorf (nur Klasse 0x80..0x83)\nnative_waren=" << (d.nativeWares ? 1 : 0) << "\n";
		f << "zutaten=" << (d.ingredients ? 1 : 0) << "\ntraenke=" << (d.potions ? 1 : 0) << "\ngegenstaende=" << (d.items ? 1 : 0) << "\nanzahl=" << d.defaultCount << "\n";
		f << "# ware: Typ;Unterart;Material;Level;Seltenheit;Anzahl  oder  item:<Kennung>;Anzahl\n";
		for (const Ware& w : d.wares) {
			if (w.item > 0) f << "ware=item:" << w.item << ";" << w.count << "\n";
			else { sprintf_s(b, "0x%02X;0x%02X;0x%02X;%d;%d;%d", w.type, w.sub, w.mat, w.level, w.rarity, w.count); f << "ware=" << b << "\n"; }
		}
		return true;
	}
	std::vector<std::string> ListFiles() {
		std::vector<std::string> out; WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((Dir() + "\\*.haendler").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
		std::sort(out.begin(), out.end()); return out;
	}
	static std::vector<VendorDef>* volatile g_set = nullptr;           // alte Saetze werden nie freigegeben
	static VendorDef Default(const std::string& role) {
		VendorDef d; d.role = role;
		if (role == "markt") { d.name = "Markthaendler"; d.cls = 0x80; d.potions = true; d.items = true; d.ingredients = true; }
		else if (role == "alchemist") { d.name = "Alchemist"; d.cls = 0x80; d.ingredients = true; d.potions = true; }
		else {
			int k = role.size() > 5 ? atoi(role.c_str() + 5) : 1; if (k < 1 || k > 5) k = 1;
			static const int cls[5] = { 0x80, 0x81, 0x82, 0x83, 0x84 };
			static const char* nm[5] = { "Haendlerhaus 1 (Waffen/Ruestung)", "Haendlerhaus 2", "Haendlerhaus 3", "Haendlerhaus 4", "Haendlerhaus 5 (Gastwirt)" };
			d.name = nm[k - 1]; d.cls = cls[k - 1]; d.nativeWares = k <= 4;
			if (k == 5) { d.potions = true; d.wares.push_back({ 0x01, 0x01, 0, 1, 0, 20, 0 }); d.wares.push_back({ 0x01, 0x02, 0, 1, 0, 20, 0 }); }   // Traenke, Essen
		}
		return d;
	}
	void Reload() {
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Dir());
		static const char* roles[7] = { "markt", "alchemist", "huette1", "huette2", "huette3", "huette4", "huette5" };
		for (const char* r : roles) {
			std::string p = Dir() + "\\" + r + ".haendler";
			if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
			std::string e; if (Save(p, Default(r), e)) std::cout << "[Haendler] " << p << " angelegt (LAB -> Haendler)." << std::endl;
		}
		std::vector<VendorDef>* s = new std::vector<VendorDef>();
		for (const std::string& f : ListFiles()) { VendorDef d; std::string e; if (Load(Dir() + "\\" + f, d, e)) { d.file = f; s->push_back(d); } }
		InterlockedExchangePointer((PVOID volatile*)&g_set, s);
	}
	bool ForRole(const std::string& role, VendorDef* out) {
		if (!g_set) Reload();
		std::vector<VendorDef>* s = g_set; if (!s) return false;
		for (const VendorDef& d : *s) if (_stricmp(d.role.c_str(), role.c_str()) == 0) { *out = d; return true; }
		*out = Default(role); return true;
	}
	static void Add(std::vector<uint8_t>& items, std::vector<int>& counts, const uint8_t* it, int n) {
		for (size_t i = 0; i < counts.size(); i++) if (memcmp(&items[i * 0x118], it, 0x14) == 0) return;   // keine Doppelten
		items.insert(items.end(), it, it + 0x118); counts.push_back(n);
	}
	void BuildWares(const VendorDef& d, std::vector<uint8_t>& items, std::vector<int>& counts) {
		uint8_t it[0x118];
		for (const Ware& w : d.wares) {
			Items::ItemDef idf;
			if (w.item > 0) { if (!Items::ById(w.item, &idf)) continue; Items::FillItem(idf, it); }
			else { memset(it, 0, sizeof(it)); it[0] = (uint8_t)w.type; it[1] = (uint8_t)w.sub; it[0x0C] = (uint8_t)w.rarity; it[0x0D] = (uint8_t)w.mat; *(uint16_t*)(it + 0x10) = (uint16_t)(std::max)(1, (std::min)(65535, w.level)); }
			Add(items, counts, it, w.count);
		}
		if (d.items) for (const Items::ItemDef& idf : Items::All()) { if (!idf.enabled) continue; Items::FillItem(idf, it); Add(items, counts, it, d.defaultCount); }
		if (d.ingredients || d.potions) for (const std::string& f : Recipes::ListFiles()) {
			Recipes::RecipeDef r; std::string err;
			if (!Recipes::Load(Recipes::RecipesDir() + "\\" + f, r, err) || !r.enabled) continue;
			if (d.ingredients) for (const Recipes::Ingredient& ig : r.ing) {
				Items::ItemDef idf;
				if (ig.item > 0 && Items::ById(ig.item, &idf)) Items::FillItem(idf, it);
				else { memset(it, 0, sizeof(it)); it[0] = (uint8_t)ig.type; it[1] = (uint8_t)ig.sub; it[0x0D] = (uint8_t)ig.mat; *(uint16_t*)(it + 0x10) = 1; }
				Add(items, counts, it, d.defaultCount);
			}
			if (d.potions) {
				Items::ItemDef idf;
				if (r.outItem > 0 && Items::ById(r.outItem, &idf)) Items::FillItem(idf, it);
				else {
					memset(it, 0, sizeof(it)); it[0] = (uint8_t)r.outType; it[1] = (uint8_t)r.outSub; it[0x0C] = (uint8_t)r.outRarity; it[0x0D] = (uint8_t)r.outMat;
					*(uint16_t*)(it + 0x10) = (uint16_t)(std::max)(1, r.outLevel);
					if (Recipes::NeedsTag(r) && r.effectId > 0) *(uint32_t*)(it + 4) = Recipes::EffectModifier(r.effectId);
				}
				Add(items, counts, it, d.defaultCount);
			}
		}
		if (items.size() > 0x118 * 200) { items.resize(0x118 * 200); counts.resize(200); }
	}
	typedef void* (__cdecl* GameNew_t)(unsigned);
	typedef void (__fastcall* VendGen_t)(void* world, void* edx, void* spawn, int a, int b);
	static bool NativeWaresRaw(cube::GameController* gc, uint8_t* c, int cls) {
		uint32_t fn = cls == 0x80 ? 0x5C9B30 : (cls == 0x82 ? 0x5C8390 : 0x5CA0A0);
		__try { ((VendGen_t)Re(fn))((uint8_t*)gc + 0x2E4, nullptr, c + (0x11DC - 0xF6C), 1, 0x14); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// eigene Waren als ZUSAETZLICHE Kategorie anhaengen (native bleiben erhalten) bzw. als einzige Kategorie
	static bool WriteRaw(uint8_t* c, const std::vector<uint8_t>& items, const std::vector<int>& counts, bool append) {
		__try {
			int n = (int)counts.size(); if (n <= 0) return false;
			uint8_t* buf = (uint8_t*)((GameNew_t)Re(0x68D668))((unsigned)(n * 0x11C));
			if (!buf) return false;
			for (int i = 0; i < n; i++) { *(int32_t*)(buf + i * 0x11C) = counts[i]; memcpy(buf + i * 0x11C + 4, &items[(size_t)i * 0x118], 0x118); }
			uint32_t oldB = *(uint32_t*)(c + 0x11DC), oldE = *(uint32_t*)(c + 0x11E0);
			int oldN = (append && oldB && oldE > oldB) ? (int)((oldE - oldB) / 0xC) : 0;
			uint8_t* cat = (uint8_t*)((GameNew_t)Re(0x68D668))((unsigned)((oldN + 1) * 0xC));
			if (!cat) return false;
			if (oldN) memcpy(cat, (void*)(uintptr_t)oldB, (size_t)oldN * 0xC);          // alte Kategorien (Zeiger) uebernehmen
			uint8_t* e = cat + oldN * 0xC;
			*(uint32_t*)(e + 0) = (uint32_t)(uintptr_t)buf; *(uint32_t*)(e + 4) = (uint32_t)(uintptr_t)(buf + n * 0x11C); *(uint32_t*)(e + 8) = *(uint32_t*)(e + 4);
			*(uint32_t*)(c + 0x11DC) = (uint32_t)(uintptr_t)cat; *(uint32_t*)(c + 0x11E0) = (uint32_t)(uintptr_t)(cat + (oldN + 1) * 0xC); *(uint32_t*)(c + 0x11E4) = *(uint32_t*)(c + 0x11E0);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static volatile LONG c_applied = 0;
	bool ApplyTo(cube::GameController* gc, cube::Creature* cr, const VendorDef& d) {
		if (!gc || !cr) return false;
		uint8_t* c = (uint8_t*)cr;
		bool native = d.nativeWares && d.cls >= 0x80 && d.cls <= 0x83 && NativeWaresRaw(gc, c, d.cls);
		std::vector<uint8_t> items; std::vector<int> counts;
		BuildWares(d, items, counts);
		bool own = !counts.empty() && WriteRaw(c, items, counts, native);
		if (native || own) InterlockedIncrement(&c_applied);
		return native || own;
	}
	std::string Status() {
		std::vector<VendorDef>* s = g_set;
		return "Haendler: " + std::to_string(s ? (int)s->size() : 0) + " Dateien (Strukturen\\Haendler), Kataloge gesetzt: " + std::to_string((long)c_applied);
	}
}
