#include "Items.h"
#include "Host.h"
#include "LabStructure.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdio>

// Herleitung: Items.h.

namespace Items {
	static const uint32_t A_ADD_INV = 0x46EBE0;                      // GameController_addItemToInventory, thiscall(Kreatur+0x11DC, item*, slot)
	static const uint32_t GC_PLAYER = 0x8006D0, CR_INVTABS = 0x11DC;
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	const char* KindName(int k) {
		static const char* n[K_COUNT] = { "Material / Zutat", "Trank / Speise", "Waffe", "Ruestung", "Schmuck", "frei (Typ von Hand)" };
		return (k >= 0 && k < K_COUNT) ? n[k] : "?";
	}
	void KindDefaults(int k, int* t, int* s, int* m) {
		switch (k) {
			case K_MATERIAL:   *t = 0x0B; *s = 0x00; *m = 0x00; break;   // Klumpen/Nugget-Form
			case K_CONSUMABLE: *t = 0x01; *s = 0x01; *m = 0x00; break;   // Lebenstrank-Form (getrunken)
			case K_WEAPON:     *t = 0x03; *s = 0x00; *m = 0x01; break;   // Schwert, Eisen
			case K_ARMOR:      *t = 0x04; *s = 0x00; *m = 0x01; break;   // Brust, Eisen
			case K_JEWEL:      *t = 0x08; *s = 0x00; *m = 0x0B; break;   // Ring, Gold
			default: break;
		}
	}

	// ---- Waffen (Herleitung: Items.h) ------------------------------------------------
	// Material-Vorschlag: 1 Eisen, 2 Holz, 0x0B Gold (GemForge MaterialName, material_toColor 0x4C7250); zu den
	// Modellen des Spiels: iron-sword/axe/mace/dagger/fist/longsword, wood-bow/crossbow/boomerang/staff/wand,
	// gold-bracelet (LabModelNames.h).
	static const WeaponDef kWeapons[] = {
		{ 0x00, CLS_WARRIOR, false, "Schwert",            1 },
		{ 0x01, CLS_WARRIOR, false, "Axt",                1 },
		{ 0x02, CLS_WARRIOR, false, "Streitkolben",       1 },
		{ 0x0D, CLS_WARRIOR, false, "Schild (Nebenhand)", 1 },
		{ 0x0F, CLS_WARRIOR, true,  "Grossschwert",       1 },
		{ 0x10, CLS_WARRIOR, true,  "Grossaxt",           1 },
		{ 0x11, CLS_WARRIOR, true,  "Grosser Streitkolben", 1 },
		{ 0x06, CLS_RANGER,  true,  "Bogen",              2 },
		{ 0x07, CLS_RANGER,  true,  "Armbrust",           2 },
		{ 0x08, CLS_RANGER,  true,  "Bumerang",           2 },
		{ 0x0C, CLS_MAGE,    false, "Armreif",            0x0B },
		{ 0x0A, CLS_MAGE,    true,  "Stab",               2 },
		{ 0x0B, CLS_MAGE,    true,  "Zauberstab",         2 },
		{ 0x03, CLS_ROGUE,   false, "Dolch",              1 },
		{ 0x04, CLS_ROGUE,   false, "Faust",              1 },
		{ 0x05, CLS_ROGUE,   true,  "Langschwert",        1 },
	};
	const WeaponDef* Weapons(int* n) { if (n) *n = (int)(sizeof(kWeapons) / sizeof(kWeapons[0])); return kWeapons; }
	const char* ClassName(int c) {
		static const char* n[5] = { "?", "Krieger", "Waldlaeufer", "Magier", "Schurke" };
		return (c >= 1 && c <= 4) ? n[c] : n[0];
	}
	bool WeaponOf(int type, int sub, int* cls, bool* two) {
		if (type != 3) return false;
		for (const WeaponDef& w : kWeapons) if (w.sub == sub) { if (cls) *cls = w.cls; if (two) *two = w.twoHand; return true; }
		return false;
	}

	// ---- Dateien ----------------------------------------------------------------
	std::string ItemsDir() { return Lab::StructuresDir() + "\\Gegenstaende"; }
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	static std::vector<int> Nums(const std::string& v) {
		std::vector<int> r; size_t p = 0;
		while (p <= v.size()) { size_t c = v.find(',', p); std::string t = Trim(v.substr(p, c == std::string::npos ? std::string::npos : c - p)); if (!t.empty()) r.push_back((int)strtol(t.c_str(), nullptr, 0)); if (c == std::string::npos) break; p = c + 1; }
		return r;
	}
	bool Load(const std::string& path, ItemDef& d, std::string& err) {
		std::ifstream f(path); if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		ItemDef r; std::string line;
		while (std::getline(f, line)) {
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			std::vector<int> n = Nums(v);
			if (k == "name") r.name = v;
			else if (k == "aktiv") r.enabled = atoi(v.c_str()) != 0;
			else if (k == "art") r.kind = (std::max)(0, (std::min)((int)K_COUNT - 1, atoi(v.c_str())));
			else if (k == "grundlage" && n.size() >= 2) { r.type = n[0] & 0xFF; r.sub = n[1] & 0xFF; r.mat = n.size() > 2 ? (n[2] & 0xFF) : 0; }
			else if (k == "level") r.level = (std::max)(1, (std::min)(65535, atoi(v.c_str())));
			else if (k == "seltenheit") r.rarity = (std::max)(0, (std::min)(4, atoi(v.c_str())));
			else if (k == "modell") r.modelFile = v;
			else if (k == "kennung") r.id = (std::max)(0, (std::min)(0xFFFF, atoi(v.c_str())));
			else if (k == "wird_zu" && n.size() >= 2) { r.convType = n[0] & 0xFF; r.convSub = n[1] & 0xFF; r.convMat = n.size() > 2 ? (n[2] & 0xFF) : 0; r.convRarity = n.size() > 3 ? (std::max)(0, (std::min)(4, n[3])) : 0; }
		}
		d = r; return true;
	}
	bool Save(const std::string& path, const ItemDef& d, std::string& err) {
		std::ofstream f(path); if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[64];
		f << "# Eigener Gegenstand (LAB -> Gegenstaende)\n";
		f << "name=" << d.name << "\naktiv=" << (d.enabled ? 1 : 0) << "\n";
		f << "# art: 0 Material/Zutat, 1 Trank/Speise, 2 Waffe, 3 Ruestung, 4 Schmuck, 5 frei\nart=" << d.kind << "\n";
		sprintf_s(b, "0x%02X,0x%02X,0x%02X", d.type, d.sub, d.mat);
		f << "# grundlage: nativer Typ, Unterart, Material (Verhalten, Reiter, Werte)\ngrundlage=" << b << "\n";
		f << "level=" << d.level << "\nseltenheit=" << d.rarity << "\n";
		f << "# modell: eigenes .cub (relativ zu Strukturen\\), leer = Aussehen des Spiels\nmodell=" << d.modelFile << "\n";
		f << "kennung=" << d.id << "\n";
		if (d.convType >= 0) { sprintf_s(b, "0x%02X,0x%02X,0x%02X,%d", d.convType, d.convSub, d.convMat, d.convRarity); f << "# wird_zu: im Inventar gegen diesen nativen Gegenstand tauschen (Typ, Unterart, Material, Seltenheit)\nwird_zu=" << b << "\n"; }
		return true;
	}
	std::vector<std::string> ListFiles() {
		std::vector<std::string> out; WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((ItemsDir() + "\\*.item").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
		std::sort(out.begin(), out.end()); return out;
	}
	std::string ModelPath(const std::string& f) {
		if (f.size() > 1 && (f[1] == ':' || f[0] == '\\' || f[0] == '/')) return f;
		return Lab::StructuresDir() + "\\" + f;
	}

	// ---- Laufzeit -----------------------------------------------------------------
	struct RT { ItemDef d; std::wstring wname; Lab::Structure model; };
	static std::vector<RT>* volatile g_set = nullptr;                // alte Saetze werden nie freigegeben
	static volatile LONG g_ver = 1, g_models = 0, g_given = 0;

	void Reload() {
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(ItemsDir());
		std::vector<RT>* s = new std::vector<RT>();
		long models = 0;
		for (const std::string& f : ListFiles()) {
			RT r; std::string e;
			if (!Load(ItemsDir() + "\\" + f, r.d, e)) { std::cout << "[Gegenstaende] " << e << std::endl; continue; }
			r.d.file = f;
			wchar_t w[96] = { 0 }; MultiByteToWideChar(CP_ACP, 0, r.d.name.c_str(), -1, w, 95); r.wname = w;
			if (r.d.id <= 0) std::cout << "[Gegenstaende] " << f << ": ohne Kennung -- im LAB neu speichern." << std::endl;
			if (!r.d.modelFile.empty()) {
				std::string me;
				if (!Lab::LoadCub(ModelPath(r.d.modelFile), r.model, me)) { std::cout << "[Gegenstaende] " << f << ": Modell nicht lesbar (" << me << ")." << std::endl; r.model = Lab::Structure(); }
				else if (r.model.sx > 64 || r.model.sy > 64 || r.model.sz > 64) { std::cout << "[Gegenstaende] " << f << ": Modell groesser als 64 -- nicht benutzt." << std::endl; r.model = Lab::Structure(); }
				else models++;
			}
			s->push_back(r);
		}
		InterlockedExchangePointer((PVOID volatile*)&g_set, s);
		InterlockedExchange(&g_models, models);
		InterlockedIncrement(&g_ver);
		std::cout << "[Gegenstaende] " << s->size() << " eigene Gegenstaende geladen (" << models << " mit eigenem Modell)." << std::endl;
	}
	int NextId() {
		int mx = 0; std::vector<RT>* s = g_set;
		if (s) for (const RT& r : *s) mx = (std::max)(mx, r.d.id);
		return (std::min)(0xFFFF, mx + 1);
	}
	std::vector<ItemDef> All() {
		std::vector<ItemDef> v; std::vector<RT>* s = g_set;
		if (s) for (const RT& r : *s) v.push_back(r.d);
		return v;
	}
	bool ById(int id, ItemDef* out) {
		std::vector<RT>* s = g_set; if (!s || id <= 0) return false;
		for (const RT& r : *s) if (r.d.id == id) { if (out) *out = r.d; return true; }
		return false;
	}
	static const RT* Find(const uint8_t* item) {
		if (!item) return nullptr;
		uint32_t m = *(const uint32_t*)(item + 4);
		if ((m & 0xFFFF0000u) != kItemTag || (m & 0xFFFFu) == 0) return nullptr;
		int id = (int)(m & 0xFFFFu);
		std::vector<RT>* s = g_set; if (!s) return nullptr;
		for (const RT& r : *s) if (r.d.id == id && r.d.type == item[0] && r.d.sub == item[1]) return &r;   // Typ/Unterart muessen passen
		return nullptr;
	}
	bool IdOf(const uint8_t* item, int* id) { const RT* r = Find(item); if (r && id) *id = r->d.id; return r != nullptr; }
	bool Name(const uint8_t* item, std::wstring* out) { const RT* r = Find(item); if (!r) return false; if (out) *out = r->wname; return true; }
	long ModelVersion() { return g_ver; }
	void Models(std::vector<ModelEntry>& out) {
		std::vector<RT>* s = g_set; if (!s) return;
		for (const RT& r : *s) if (r.d.id > 0 && r.model.Valid()) out.push_back(ModelEntry{ r.d.id, r.d.type, r.d.sub, r.model, r.d.modelFile });
	}

	void FillItem(const ItemDef& d, uint8_t* it) {
		memset(it, 0, 0x118);
		it[0] = (uint8_t)d.type; it[1] = (uint8_t)d.sub;
		*(uint32_t*)(it + 4) = d.id > 0 ? Tag(d.id) : 0;
		it[0x0C] = (uint8_t)d.rarity; it[0x0D] = (uint8_t)d.mat;
		*(uint16_t*)(it + 0x10) = (uint16_t)(d.level < 1 ? 1 : d.level);
	}
	// Stapelbare Typen: addItemToInventory nimmt die Anzahl aus Item+0x10 (0x46EBE0, GameController.cpp:25290ff.)
	static bool Stackable(int type, int sub) {
		return type == 0x0C || type == 0x0D || type == 0x15 || type == 0x00 || type == 0x19 || type == 0x14 || type == 0x18 || type == 0x17 ||
		       (type == 0x0B && sub != 0x0E);
	}
	typedef void (__fastcall* AddInv_t)(void* inv, void* edx, uint8_t* item, int slot);
	static bool AddRaw(cube::GameController* gc, uint8_t* it) {
		__try {
			uint8_t* cr = *(uint8_t**)((uint8_t*)gc + GC_PLAYER);
			if (!cr) return false;
			((AddInv_t)Re(A_ADD_INV))(cr + CR_INVTABS, nullptr, it, -1);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	int Give(cube::GameController* gc, const ItemDef& d, int count, std::string& report) {
		if (!gc) { report = "kein Spiel"; return 0; }
		if (d.id <= 0) { report = "Gegenstand hat noch keine Kennung -- erst 'Speichern + laden'."; return 0; }
		count = (std::max)(1, (std::min)(999, count));
		uint8_t it[0x118];
		int done = 0;
		if (Stackable(d.type, d.sub)) {
			FillItem(d, it); *(uint16_t*)(it + 0x10) = (uint16_t)count;
			if (AddRaw(gc, it)) done = count;
		} else {
			for (int i = 0; i < count; i++) { FillItem(d, it); if (AddRaw(gc, it)) done++; }
		}
		InterlockedExchangeAdd(&g_given, done);
		char b[200]; sprintf_s(b, "%d x '%s' ins Inventar gelegt.", done, d.name.c_str());
		report = done ? b : "Ins Inventar legen fehlgeschlagen (SEH).";
		return done;
	}
	// ---- wird_zu: Gegenstaende im Inventar tauschen -------------------------------------------------
	// Inventar: Kreatur+0x11DC = vector<vector<Eintrag 0x11C>> (Reiter); Eintrag +0 Anzahl, +4 Gegenstand (0x118).
	// Leeren wie CombatBehavior 0x42CB20 (Anzahl 0, Typ/Unterart 0).
	static int TakeTaggedRaw(cube::GameController* gc, uint32_t tag, int* level) {
		int n = 0;
		__try {
			uint8_t* cr = *(uint8_t**)((uint8_t*)gc + GC_PLAYER); if (!cr) return 0;
			uint8_t** tb = *(uint8_t***)(cr + CR_INVTABS); uint8_t** te = *(uint8_t***)(cr + CR_INVTABS + 4);
			if (!tb || te < tb || te - tb > 64 * 3) return 0;
			for (uint8_t** t = tb; t < te; t += 3) {                                  // je Reiter: begin, end, cap
				uint8_t* b = t[0]; uint8_t* e = t[1];
				if (!b || e < b || (e - b) / 0x11C > 512) continue;
				for (uint8_t* en = b; en + 0x11C <= e; en += 0x11C) {
					if (*(int*)en <= 0 || *(uint32_t*)(en + 4 + 4) != tag) continue;
					n += *(int*)en; *level = (std::max)(*level, (int)*(uint16_t*)(cr + 0x190));
					*(int*)en = 0; en[4] = 0; en[5] = 0; *(uint32_t*)(en + 8) = 0;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}
	void Tick(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (!gc || now - last < 400) return;
		last = now;
		std::vector<RT>* s = g_set; if (!s) return;
		for (const RT& r : *s) {
			if (!r.d.enabled || r.d.convType < 0 || r.d.id <= 0) continue;
			int lv = 1, n = TakeTaggedRaw(gc, Tag(r.d.id), &lv);
			if (n <= 0) continue;
			uint8_t it[0x118];
			for (int i = 0; i < n; i++) {
				memset(it, 0, sizeof(it));
				it[0] = (uint8_t)r.d.convType; it[1] = (uint8_t)r.d.convSub; it[0x0C] = (uint8_t)r.d.convRarity; it[0x0D] = (uint8_t)r.d.convMat;
				*(uint16_t*)(it + 0x10) = Stackable(r.d.convType, r.d.convSub) ? 1 : (uint16_t)lv;
				AddRaw(gc, it);
			}
			std::cout << "[Gegenstaende] " << n << " x '" << r.d.name << "' in den nativen Gegenstand getauscht." << std::endl;
		}
	}
	std::string Status() {
		std::vector<RT>* s = g_set;
		char b[200]; sprintf_s(b, "Gegenstaende: %d geladen, %ld mit eigenem Modell, %ld ins Inventar gelegt", s ? (int)s->size() : 0, g_models, g_given);
		return b;
	}
}
