#include "Skills.h"
#include "NativeUi.h"
#include "Effects.h"
#include "ModDb.h"
#include "Boat.h"
#include "Host.h"
#include "MobLab.h"
#include "LabStructure.h"
#include "LabWindow.h"
#include "ModSwitches.h"
#include "MountainVillage.h"
#include "LabBridge.h"
#include "City.h"
#include "Temples.h"
#include "ZoneSplit.h"
#include "MinHook.h"
#include <windows.h>
#include <d3d9.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <deque>
#include <cmath>
#include <cstring>
#include <cstdio>

// Herleitung: Skills.h.

namespace Skills {
	static const uint32_t CR_HP = 0x16C, CR_MP = 0x170, CR_CLASS = 0x140, CR_LEVEL = 0x190, CR_GUID = 0x08;
	static const uint32_t CR_POS = 0x10, CR_VEL = 0x34;
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	const char* KindName(int k) {
		static const char* n[SK_COUNT] = { "Teleport", "Beschwoeren" };
		return (k >= 0 && k < SK_COUNT) ? n[k] : "?";
	}
	const char* KindHelp(int k) {
		switch (k) {
			case SK_TELEPORT: return "Wert = Anzahl Runensteine (Rang 1: wert, je Rang + wert_je_rang). U legt einen benannten Runenstein ab "
			                         "(U neben einem Stein hebt ihn auf), 6 portet hin; bei mehreren zeigt 6 die Liste (6 = weiter, P = teleportieren). "
			                         "Abklingzeit und Mana gelten fuer den Sprung. Dauer: ohne Bedeutung.";
			case SK_SUMMON:   return "Wert = Anzahl Begleiter (abgerundet), Dauer = Sekunden bis sie vergehen. Beschworen werden die "
			                         "zuletzt in deiner Naehe besiegten Gegner (Rasse und Level, hoechstens dein Level), neueste zuerst; "
			                         "jeder besiegte Gegner kommt einmal zurueck. Dein eigenes Haustier bleibt unberuehrt.";
		}
		return "";
	}
	const char* ClassName(int c) {
		static const char* n[5] = { "alle Klassen", "Krieger", "Waldlaeufer", "Magier", "Schurke" };
		return (c >= 0 && c <= 4) ? n[c] : "?";
	}
	static const char* kTeleDesc = "Runensteine ablegen (U) und zu ihnen reisen (P, Liste mit 6). Erst nutzbar, nachdem du mit dem "
	                               "Hochelfenkoenig auf dem Mutterbaum der Elfenstadt gesprochen hast.";
	SkillDef DefaultSkill(int kind) {
		SkillDef d; d.kind = kind;
		if (kind == SK_SUMMON) {
			d.name = "Beschwoeren"; d.icon = "beschwoeren.png"; d.key = 0x35;            // Taste 5
			d.treeSlot = 10; d.pointsPerRank = 5;                                          // Skilltree: Faehigkeit 5
			d.unlockLevel = 5; d.levelsPerRank = 10; d.maxRank = 5;
			d.value = 1.0f; d.valuePerRank = 0.5f; d.duration = 30.0f; d.durationPerRank = 10.0f;
			d.cooldown = 60.0f; d.cooldownPerRank = 5.0f; d.mana = 40;
		} else {
			d.name = "Teleport"; d.icon = "teleport.png"; d.key = 0x50; d.key2 = 0x36; d.keyPlace = 0x55;   // Tasten P, 6, U (P statt 4: 4 wurde versehentlich gedrueckt; P ist nativ frei)
			d.treeSlot = 9; d.pointsPerRank = 5;                                           // Skilltree: Faehigkeit 4
			d.unlockLevel = 1; d.levelsPerRank = 10; d.maxRank = 5;
			d.value = 1.0f; d.valuePerRank = 1.0f; d.duration = 0.0f; d.durationPerRank = 0.0f;       // 1 Stein, je Rang +1
			d.cooldown = 10.0f; d.cooldownPerRank = 1.0f; d.mana = 25;
			d.unlockBy = "hochelfenkoenig";
			d.desc = kTeleDesc;
		}
		return d;
	}

	// ---- Dateien ------------------------------------------------------------------
	std::string SkillsDir() { return Lab::StructuresDir() + "\\Skills"; }
	std::string IconsDir() { return SkillsDir() + "\\Bilder"; }
	std::string IconPath(const std::string& f) {
		if (f.empty()) return "";
		if (f.size() > 1 && (f[1] == ':' || f[0] == '\\' || f[0] == '/')) return f;
		return IconsDir() + "\\" + f;
	}
	static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
	bool Load(const std::string& path, SkillDef& d, std::string& err) {
		std::ifstream f(path); if (!f) { err = "Datei nicht lesbar: " + path; return false; }
		SkillDef r; std::string line; int version = 1;
		while (std::getline(f, line)) {
			line = Trim(line); if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
			int iv = (int)strtol(v.c_str(), nullptr, 0); float fv = (float)atof(v.c_str());
			if (k == "name") r.name = v;
			else if (k == "aktiv") r.enabled = iv != 0;
			else if (k == "art") { for (int i = 0; i < SK_COUNT; i++) if (_stricmp(v.c_str(), KindName(i)) == 0) r.kind = i; }
			else if (k == "klasse") r.cls = (std::max)(0, (std::min)(4, iv));
			else if (k == "bild") r.icon = v;
			else if (k == "taste") r.key = iv & 0xFF;
			else if (k == "taste2") r.key2 = iv & 0xFF;
			else if (k == "taste_ablegen") r.keyPlace = iv & 0xFF;
			else if (k == "version") version = iv;
			else if (k == "baum_platz") r.treeSlot = (iv >= 0 && iv <= 10) ? iv : -1;
			else if (k == "punkte_je_rang") r.pointsPerRank = (std::max)(1, iv);
			else if (k == "ab_level") r.unlockLevel = (std::max)(1, iv);
			else if (k == "level_je_rang") r.levelsPerRank = (std::max)(1, iv);
			else if (k == "max_rang") r.maxRank = (std::max)(1, (std::min)(20, iv));
			else if (k == "wert") r.value = fv;
			else if (k == "wert_je_rang") r.valuePerRank = fv;
			else if (k == "dauer") r.duration = (std::max)(0.0f, fv);
			else if (k == "dauer_je_rang") r.durationPerRank = fv;
			else if (k == "abklingzeit") r.cooldown = (std::max)(0.0f, fv);
			else if (k == "abklingzeit_je_rang") r.cooldownPerRank = fv;
			else if (k == "mana") r.mana = (std::max)(0, (std::min)(100, iv));
			else if (k == "beschreibung") r.desc = v;
			else if (k == "freischalten") r.unlockBy = v;
		}
		if (version < 4 && r.kind == SK_TELEPORT) {                        // Stand 4 (2026-10-27): Teleport erst nach dem Hochelfenkoenig
			if (r.unlockBy.empty()) r.unlockBy = "hochelfenkoenig";
			if (r.desc.empty()) r.desc = kTeleDesc;
			f.close(); std::string e3; Save(path, r, e3);
			std::cout << "[Skills] " << path << ": Teleport wird erst durch den Hochelfenkoenig freigeschaltet (Stand 4)." << std::endl;
		}
		if (version < 3) {                                                // Stand 2026-10-13: Tasten 4/5, Teleport = Runensteine, Skilltree
			if (version < 2) {
				if (r.kind == SK_TELEPORT) { r.key = 0x50; r.key2 = 0x36; r.keyPlace = 0x55; r.value = 1.0f; r.valuePerRank = 1.0f; r.cooldown = 10.0f; }
				else if (r.kind == SK_SUMMON) r.key = 0x35;
			}
			if (r.kind == SK_TELEPORT) r.treeSlot = 9; else if (r.kind == SK_SUMMON) r.treeSlot = 10;
			f.close();
			std::string e2; Save(path, r, e2);
			std::cout << "[Skills] " << path << " auf Stand 2 gebracht (Tasten 4/5/6/U, Runensteine)." << std::endl;
		}
		d = r; return true;
	}
	bool Save(const std::string& path, const SkillDef& d, std::string& err) {
		std::ofstream f(path); if (!f) { err = "Datei nicht schreibbar: " + path; return false; }
		char b[64];
		f << "# Eigener Skill (LAB -> Skills)\nversion=4\nname=" << d.name << "\naktiv=" << (d.enabled ? 1 : 0) << "\n";
		f << "# art: Teleport, Beschwoeren\nart=" << KindName(d.kind) << "\n";
		f << "# klasse: 0 alle, 1 Krieger, 2 Waldlaeufer, 3 Magier, 4 Schurke\nklasse=" << d.cls << "\n";
		f << "# bild: relativ zu Strukturen\\Skills\\Bilder (png, bmp, jpg)\nbild=" << d.icon << "\n";
		sprintf_s(b, "0x%02X", d.key); f << "# taste: virtueller Tastencode (0x34 = 4, 0x35 = 5, 0x36 = 6, 0x50 = P, 0x55 = U, 0x74 = F5 ...)\ntaste=" << b << "\n";
		sprintf_s(b, "0x%02X", d.key2); f << "# taste2: zweite Taste (Teleport: Steinliste / naechster Stein), 0 = keine\ntaste2=" << b << "\n";
		sprintf_s(b, "0x%02X", d.keyPlace); f << "# taste_ablegen: Teleport -- Runenstein ablegen bzw. daneben aufheben, 0 = keine\ntaste_ablegen=" << b << "\n";
		f << "# Fortschritt: Rang 1 ab Level ab_level, jeder weitere Rang nach level_je_rang Leveln, hoechstens max_rang\n";
		f << "ab_level=" << d.unlockLevel << "\nlevel_je_rang=" << d.levelsPerRank << "\nmax_rang=" << d.maxRank << "\n";
		f << "# baum_platz: Skilltree (X) -- 9 = Faehigkeit 4, 10 = Faehigkeit 5 (Punkte aus dem nativen Baum), -1 = Rang folgt dem Level\n";
		f << "baum_platz=" << d.treeSlot << "\npunkte_je_rang=" << d.pointsPerRank << "\n";
		f << "# Werte: Rang r -> wert + (r-1) * wert_je_rang usw.; abklingzeit wird je Rang um abklingzeit_je_rang kuerzer\n";
		sprintf_s(b, "%.2f", d.value); f << "wert=" << b << "\n";
		sprintf_s(b, "%.2f", d.valuePerRank); f << "wert_je_rang=" << b << "\n";
		sprintf_s(b, "%.1f", d.duration); f << "dauer=" << b << "\n";
		sprintf_s(b, "%.1f", d.durationPerRank); f << "dauer_je_rang=" << b << "\n";
		sprintf_s(b, "%.1f", d.cooldown); f << "abklingzeit=" << b << "\n";
		sprintf_s(b, "%.1f", d.cooldownPerRank); f << "abklingzeit_je_rang=" << b << "\n";
		f << "# mana: Kosten in % des Manas\nmana=" << d.mana << "\n";
		f << "# beschreibung: Text im Skillfenster (X), wenn die Maus ueber dem Platz steht\nbeschreibung=" << d.desc << "\n";
		f << "# freischalten: erst nach einem Gespraech nutzbar (hochelfenkoenig = Hochelfenkoenig auf dem Mutterbaum), leer = sofort\nfreischalten=" << d.unlockBy << "\n";
		return true;
	}
	std::vector<std::string> ListFiles() {
		std::vector<std::string> out; WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((SkillsDir() + "\\*.skill").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
		std::sort(out.begin(), out.end()); return out;
	}

	static std::vector<SkillDef>* volatile g_set = nullptr;       // alte Saetze werden nie freigegeben
	void Reload() {
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(SkillsDir()); Lab::EnsureDir(IconsDir());
		std::vector<std::string> files = ListFiles();
		if (files.empty()) {                                          // erste Benutzung: die zwei Vorgaben anlegen
			std::string e;
			Save(SkillsDir() + "\\Teleport.skill", DefaultSkill(SK_TELEPORT), e);
			Save(SkillsDir() + "\\Beschwoeren.skill", DefaultSkill(SK_SUMMON), e);
			files = ListFiles();
		}
		std::vector<SkillDef>* s = new std::vector<SkillDef>();
		for (const std::string& f : files) {
			SkillDef d; std::string e;
			if (!Load(SkillsDir() + "\\" + f, d, e)) { std::cout << "[Skills] " << e << std::endl; continue; }
			d.file = f; s->push_back(d);
		}
		InterlockedExchangePointer((PVOID volatile*)&g_set, s);
		std::cout << "[Skills] " << s->size() << " Skills geladen (Strukturen\\Skills)." << std::endl;
	}
	std::vector<SkillDef> All() { std::vector<SkillDef>* s = g_set; return s ? *s : std::vector<SkillDef>(); }

	int RankFor(const SkillDef& d, int level) {
		if (level < d.unlockLevel) return 0;
		return (std::min)(d.maxRank, 1 + (level - d.unlockLevel) / (std::max)(1, d.levelsPerRank));
	}
	int RankForPoints(const SkillDef& d, int points) {
		if (points <= 0) return 0;
		return (std::min)(d.maxRank, 1 + (points - 1) / (std::max)(1, d.pointsPerRank));
	}
	float ValueAt(const SkillDef& d, int r) { return d.value + (float)(std::max)(0, r - 1) * d.valuePerRank; }
	float DurationAt(const SkillDef& d, int r) { return (std::max)(0.0f, d.duration + (float)(std::max)(0, r - 1) * d.durationPerRank); }
	float CooldownAt(const SkillDef& d, int r) { return (std::max)(1.0f, d.cooldown - (float)(std::max)(0, r - 1) * d.cooldownPerRank); }

	// ---- Zustand (Spielthread) -----------------------------------------------------
	static std::map<std::string, DWORD> g_readyAt;                     // Datei -> Zeitpunkt, ab dem wieder bereit
	struct Kill { int race, level; bool big; };
	// Grosse Tiere werden nicht beschworen (2026-10-29): Groesse aus dem Aussehen (+0x80 Breite, +0x88 Hoehe, Bloecke).
	// Mensch 0.8 x 1.8, Oger 1.8 x 3.0 (geht noch); ab 2.0 Breite oder 3.6 Hoehe gilt ein Tier als gross
	// (Infinite-Cubes fillAppearance: 2.4 x 6.0, 2.8 x 4.0, 4.8 x 10.8 ...).
	static bool IsBig(float sx, float sz) { return sx >= 2.0f || sz >= 3.6f; }
	static std::deque<Kill> g_kills;                                   // zuletzt besiegt (vorn = neuester)
	struct Watch { int race, level; DWORD seen; float hp0, hpLast; bool big; };
	static std::map<uint64_t, Watch> g_watch;                          // lebende Monster in der Naehe
	struct Summon { uint64_t guid; DWORD end; };
	static std::vector<Summon> g_summons;
	static float g_dirX = 1.0f, g_dirY = 0.0f;                          // letzte Laufrichtung
	static std::map<int, bool> g_keyDown;
	static CRITICAL_SECTION g_snapCs; static bool g_snapInit = false;
	static Snapshot g_snap;
	static volatile LONG g_hudOn = -1;

	bool HudOn() { if (g_hudOn < 0) InterlockedExchange(&g_hudOn, ModSwitches::Get("skill_leiste", true) ? 1 : 0); return g_hudOn != 0; }
	void SetHud(bool on) { InterlockedExchange(&g_hudOn, on ? 1 : 0); ModSwitches::Set("skill_leiste", on); }

	Snapshot GetSnapshot() {
		if (!g_snapInit) return Snapshot();
		EnterCriticalSection(&g_snapCs); Snapshot s = g_snap; LeaveCriticalSection(&g_snapCs); return s;
	}

	struct PlayerRaw { uint64_t guid; int64_t x, y, z; float vx, vy; float mp; int cls, level; };
	static bool ReadPlayerRaw(cube::GameController* gc, PlayerRaw* p) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false;
			p->guid = *(uint64_t*)(c + CR_GUID);
			p->x = *(int64_t*)(c + CR_POS); p->y = *(int64_t*)(c + CR_POS + 8); p->z = *(int64_t*)(c + CR_POS + 16);
			p->vx = *(float*)(c + CR_VEL); p->vy = *(float*)(c + CR_VEL + 4);
			p->mp = *(float*)(c + CR_MP); p->cls = c[CR_CLASS]; p->level = *(int*)(c + CR_LEVEL);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool WriteMana(cube::GameController* gc, float mp) {
		__try { *(float*)((uint8_t*)gc->local_player + CR_MP) = mp; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool MovePlayer(cube::GameController* gc, int64_t x, int64_t y, int64_t z) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player;
			*(int64_t*)(c + CR_POS) = x; *(int64_t*)(c + CR_POS + 8) = y; *(int64_t*)(c + CR_POS + 16) = z;
			*(float*)(c + CR_VEL) = 0.0f; *(float*)(c + CR_VEL + 4) = 0.0f; *(float*)(c + CR_VEL + 8) = 0.0f;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	typedef void* (__fastcall* WAssign_t)(void* str, void* edx, const wchar_t* s);
	typedef void  (__fastcall* WFree_t)(void* str, void* edx);
	typedef void  (__fastcall* ChatPrint_t)(void* chat, void* edx, void* wstr, void* color);
	static void ChatRaw(cube::GameController* gc, const wchar_t* text) {      // wie Effects.cpp (0x43AB30)
		__try {
			if (!gc || !gc->ChatWidget) return;
			alignas(4) uint8_t w[0x18]; memset(w, 0, sizeof(w));
			((WAssign_t)Re(0x40EB60))(w, nullptr, text);
			float col[4] = { 0.75f, 0.6f, 1.0f, 1.0f };
			((ChatPrint_t)Re(0x43AB30))((void*)gc->ChatWidget, nullptr, w, col);
			((WFree_t)Re(0x593E50))(w, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void Chat(cube::GameController* gc, const std::string& s) {
		wchar_t w[256] = { 0 }; MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w, 255); ChatRaw(gc, w);
	}

	// Fester Boden mit 3 Bloecken Luft darueber, von oben (z0 + 24) nach unten (z0 - 48). false = nichts gefunden.
	static bool LandingZ(cube::GameController* gc, int x, int y, int z0, int* out) {
		for (int z = z0 + 24; z >= z0 - 48; z--) {
			uint8_t r, g, b, t;
			if (!MountainVillage::LabReadBlock(gc, x, y, z, r, g, b, t)) continue;
			if ((t & 0x1F) == 2) return false;                                    // Wasser
			bool free3 = true;
			for (int k = 1; k <= 3 && free3; k++) { uint8_t r2, g2, b2, t2; if (MountainVillage::LabReadBlock(gc, x, y, z + k, r2, g2, b2, t2)) free3 = false; }
			if (free3) { *out = z + 1; return true; }
		}
		return false;
	}

	// ---- Runensteine (Teleport) ---------------------------------------------------------
	// Je Welt (Seed GC+0x2E4+0x800188) eine Datei im Spielordner: "x y z Name". Die Zonenerzeugung (beliebiger
	// Thread) setzt die Steine jedes Mal neu als native Objekte (Typ 0x2D); abgelegt/aufgehoben wird auf dem
	// Spielthread (LabCreateObjects bzw. Skala 0 + Neuvermaschen).
	static CRITICAL_SECTION& StoneCs() { static struct S { CRITICAL_SECTION c; S() { InitializeCriticalSection(&c); } } s; return s.c; }
	struct StoneLock { StoneLock() { EnterCriticalSection(&StoneCs()); } ~StoneLock() { LeaveCriticalSection(&StoneCs()); } };
	static std::vector<Stone> g_stones; static uint32_t g_stoneSeed = 0; static bool g_stonesLoaded = false;
	static const int kStoneType = 0x2D;                                   // Runenstein (runestone.cub 0x9DD, 11 x 8 x 12)
	static const float kStoneSx = 1.4f, kStoneSy = 1.0f, kStoneSz = 1.5f;  // Halbmasse wie Zaun 16x3x12 -> 2/1/1.5 (Breite/8)
	static std::string StoneFile(uint32_t seed) { char b[64]; sprintf_s(b, "AdventureMod_runensteine_%08X.txt", seed); return b; }
	static void LoadStonesLocked(uint32_t seed) {
		g_stones.clear(); g_stoneSeed = seed; g_stonesLoaded = true;
		std::istringstream f(ModDb::Txt(StoneFile(seed))); std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			Stone st; int n = 0;
			if (sscanf_s(line.c_str(), "%d %d %d %n", &st.x, &st.y, &st.z, &n) < 3) continue;
			st.name = Trim(line.substr((size_t)n));
			if (st.name.empty()) st.name = "Runenstein";
			g_stones.push_back(st);
		}
	}
	static void SaveStonesLocked() {
		ModDb::Writer f(StoneFile(g_stoneSeed));
		f << "# Runensteine dieser Welt (Skill Teleport): x y z Name\n";
		for (const Stone& st : g_stones) f << st.x << " " << st.y << " " << st.z << " " << st.name << "\n";
	}
	static void EnsureStones(uint32_t seed) { if (seed && (!g_stonesLoaded || seed != g_stoneSeed)) LoadStonesLocked(seed); }
	std::vector<Stone> Stones() { StoneLock l; return g_stones; }
	static uint32_t SeedRaw(const void* world) { __try { return *(const uint32_t*)((const uint8_t*)world + 0x800188); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
	void ZoneObjects(const void* world, void* zone, int zx, int zy) {
		if (!world || !zone) return;
		uint32_t seed = SeedRaw(world);
		std::vector<Stone> here;
		{ StoneLock l; EnsureStones(seed); for (const Stone& st : g_stones) if ((st.x >> 8) == zx && (st.y >> 8) == zy) here.push_back(st); }
		for (const Stone& st : here) City::PushObject(zone, kStoneType, st.x + 0.5f, st.y + 0.5f, st.z, 0, kStoneSx, kStoneSy, kStoneSz);
	}
	// Aufheben: Objekt Typ 0x2D an (x, y, z) in der geladenen Zone unsichtbar und durchlaessig machen (Skala 0).
	static bool HideStoneRaw(cube::Zone* zone, int x, int y, int z) {
		__try {
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			for (uint8_t* o = raw[0]; o && o + 0x188 <= raw[1]; o += 0x188) {
				if (*(int*)o != kStoneType) continue;
				int ox = (int)(*(int64_t*)(o + 0x08) >> 16), oy = (int)(*(int64_t*)(o + 0x10) >> 16), oz = (int)(*(int64_t*)(o + 0x18) >> 16);
				if (std::abs(ox - x) <= 1 && std::abs(oy - y) <= 1 && std::abs(oz - z) <= 2) {
					*(float*)(o + 0x24) = 0.0f; *(float*)(o + 0x28) = 0.0f; *(float*)(o + 0x2C) = 0.0f;
					return true;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return false;
	}

	// ---- Namensfenster (eigener Thread, damit das Spiel weiterlaeuft) -------------------------
	struct NameReq { bool done = false, ok = false; std::string name; int x = 0, y = 0, z = 0; };
	static NameReq g_nameReq; static volatile LONG g_nameOpen = 0;
	static CRITICAL_SECTION& NameCs() { static struct S { CRITICAL_SECTION c; S() { InitializeCriticalSection(&c); } } s; return s.c; }
	struct NameArgs { HWND game; std::string def; int x, y, z; };
	static LRESULT CALLBACK NameProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		if (m == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
			char buf[64] = ""; GetWindowTextA(GetDlgItem(h, 100), buf, sizeof(buf));
			NameArgs* a = (NameArgs*)GetWindowLongPtrA(h, GWLP_USERDATA);
			EnterCriticalSection(&NameCs());
			g_nameReq.done = true; g_nameReq.ok = LOWORD(wp) == IDOK; g_nameReq.name = Trim(buf);
			if (a) { g_nameReq.x = a->x; g_nameReq.y = a->y; g_nameReq.z = a->z; }
			LeaveCriticalSection(&NameCs());
			DestroyWindow(h); return 0;
		}
		if (m == WM_CLOSE) { SendMessageA(h, WM_COMMAND, IDCANCEL, 0); return 0; }
		if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
		return DefWindowProcA(h, m, wp, lp);
	}
	static DWORD WINAPI NameThread(LPVOID p) {
		NameArgs* a = (NameArgs*)p;
		WNDCLASSA wc = {}; wc.lpfnWndProc = NameProc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "AdvRunensteinName";
		wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		RegisterClassA(&wc);
		RECT gr = { 0, 0, 800, 600 }; if (a->game) GetWindowRect(a->game, &gr);
		int W = 340, H = 130, X = (gr.left + gr.right - W) / 2, Y = (gr.top + gr.bottom - H) / 2;
		HWND h = CreateWindowExA(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, wc.lpszClassName, "Runenstein benennen", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		                         X, Y, W, H, NULL, NULL, wc.hInstance, NULL);
		if (!h) { InterlockedExchange(&g_nameOpen, 0); delete a; return 0; }
		SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)a);
		HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		HWND l = CreateWindowA("STATIC", "Name des Runensteins (Enter = ablegen, Esc = abbrechen):", WS_CHILD | WS_VISIBLE, 10, 8, 320, 18, h, NULL, NULL, NULL);
		HWND ed = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", a->def.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 10, 30, 314, 22, h, (HMENU)100, NULL, NULL);
		HWND ok = CreateWindowA("BUTTON", "Ablegen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 154, 62, 82, 26, h, (HMENU)IDOK, NULL, NULL);
		HWND ca = CreateWindowA("BUTTON", "Abbrechen", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 242, 62, 82, 26, h, (HMENU)IDCANCEL, NULL, NULL);
		for (HWND c : { l, ed, ok, ca }) SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
		SendMessageA(ed, EM_SETLIMITTEXT, 40, 0); SendMessageA(ed, EM_SETSEL, 0, -1);
		SetForegroundWindow(h); SetFocus(ed);
		MSG msg;
		while (GetMessageA(&msg, NULL, 0, 0) > 0) { if (!IsDialogMessageA(h, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
		if (a->game) SetForegroundWindow(a->game);
		delete a;
		InterlockedExchange(&g_nameOpen, 0);
		return 0;
	}
	// 2026-10-27: zuerst das Eingabefenster im Stil des Spiels (NativeUi, plasma-Vorlagen); nur wenn die Vorlagen fehlen,
	// das Windows-Fenster von frueher.
	struct NativeAsk { bool on = false; std::string def; int x = 0, y = 0, z = 0; };
	static NativeAsk g_nativeAsk;
	static void AskStoneName(cube::GameController* gc, const std::string& def, int x, int y, int z) {
		if (g_nameOpen || g_nativeAsk.on || NativeUi::Typing()) return;
		if (gc && NativeUi::AskText(gc, "Name des Runensteins  (leer = " + def + ")", "Enter = ablegen      Esc = abbrechen")) {
			g_nativeAsk.on = true; g_nativeAsk.def = def; g_nativeAsk.x = x; g_nativeAsk.y = y; g_nativeAsk.z = z;
			return;
		}
		if (InterlockedCompareExchange(&g_nameOpen, 1, 0) != 0) return;
		NameArgs* a = new NameArgs{ GetForegroundWindow(), def, x, y, z };
		HANDLE t = CreateThread(NULL, 0, NameThread, a, 0, NULL);
		if (t) CloseHandle(t); else { delete a; InterlockedExchange(&g_nameOpen, 0); }
	}

	// ---- Skilltree (Taste X) ------------------------------------------------------------------------
	// Belegt (GameController.cpp:28758..28860, 29620..29642; SkillWidget 0x4DD750/0x4DD810):
	//   GC+0x8008F1  Skillfenster offen (Byte)          GC+0x80090C  SkillWidget*
	//   Widget+0x160 Bearbeitungspuffer int[11] (wie Kreatur+0x1138), +0x18C Spezialisierung
	//   Punkte gesamt = (Kreatur+0x1164 / 4) + Level*2 - 2; Linksklick +1 nur, wenn Summe(Puffer) < gesamt
	//   "Uebernehmen" kopiert den Puffer nach Kreatur+0x1138 (kostet Gold bei Ruecknahme), "Abbrechen" umgekehrt.
	// Die Plaetze 9/10 (Faehigkeit 4/5) zeichnet das native Fenster nicht -- unsere Leiste daneben setzt sie im
	// Puffer; das native Uebernehmen speichert sie mit dem Charakter.
	static int g_treePts[11] = {}; static bool g_treeOpen = false;
	struct TreeBtn { RECT r; int slot, delta; };
	static std::vector<TreeBtn> g_treeBtns; static POINT g_mouse = { -1, -1 }; static bool g_mouseValid = false;
	static bool ReadTreeRaw(cube::GameController* gc, int* pts, bool* open) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; if (!c) return false;
			for (int i = 0; i < 11; i++) pts[i] = *(int*)(c + 0x1138 + i * 4);
			*open = *((uint8_t*)gc + 0x8008F1) != 0;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Puffer des Fensters aendern (wie ein Klick auf eine native Fertigkeit)
	static bool TreeClickRaw(cube::GameController* gc, int slot, int delta, int* bufVal, int* left) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; uint8_t* wdg = *(uint8_t**)((uint8_t*)gc + 0x80090C);
			if (!c || !wdg || slot < 0 || slot > 10) return false;
			int* buf = (int*)(wdg + 0x160);
			int sum = 0; for (int i = 0; i < 11; i++) sum += buf[i];
			int b = *(int*)(c + 0x1164), total = ((b + ((b >> 31) & 3)) >> 2) + *(int*)(c + 0x190) * 2 - 2;
			if (delta > 0 && sum >= total) { *left = 0; return false; }
			if (delta < 0 && buf[slot] <= 0) return false;
			buf[slot] += delta > 0 ? 1 : -1;
			*bufVal = buf[slot]; *left = total - sum - (delta > 0 ? 1 : -1);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool TreeBufRaw(cube::GameController* gc, int* buf, int* total) {
		__try {
			uint8_t* c = (uint8_t*)gc->local_player; uint8_t* wdg = *(uint8_t**)((uint8_t*)gc + 0x80090C);
			if (!c || !wdg) return false;
			for (int i = 0; i < 11; i++) buf[i] = ((int*)(wdg + 0x160))[i];
			int b = *(int*)(c + 0x1164); *total = ((b + ((b >> 31) & 3)) >> 2) + *(int*)(c + 0x190) * 2 - 2;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static int g_treeBuf[11] = {}; static int g_treeTotal = 0;
	// Natives Skillfenster: Symbole je Fertigkeit stehen in std::map<int, Sprite*> bei GC+0x800828 (Schluessel =
	// Platz 0..10; GameController-ctor 0x45F9A8: map[1] = LoadIcon("pet-riding.png") ...). Das Fenster zeigt einen
	// Platz nur, wenn dort ein Symbol steht (Aufbau 0x48E76A: Suche, sonst ausgeblendet 0x48EA4F) -- 9/10 fehlen nativ.
	// LoadIcon 0x486A20 = thiscall(GC, const std::string* pfad, Archiv*), ret 8; Archiv 0 -> Datei von der Platte
	// (Texturverwaltung GC+0x800710, 0x658C40). Map-Zugriff operator[] 0x468910 = thiscall(map*, const int*), ret 4.
	typedef void* (__fastcall* LoadIcon_t)(void* gc, void* edx, const void* path, void* archive);
	typedef void** (__fastcall* MapIdx_t)(void* map, void* edx, const int* key);
	static bool RegisterTreeIconRaw(cube::GameController* gc, int slot, const char* path) {
		__try {
			size_t n = strlen(path); if (n == 0 || n > 250) return false;
			static char buf[2][260]; char* b = buf[slot == 10 ? 1 : 0]; memcpy(b, path, n + 1);
			alignas(4) uint8_t s[24]; memset(s, 0, sizeof(s));            // std::string (MSVC): Puffer/Zeiger, Laenge +0x10, Kapazitaet +0x14
			if (n < 16) { memcpy(s, path, n + 1); *(uint32_t*)(s + 0x14) = 15; }
			else { *(char**)s = b; *(uint32_t*)(s + 0x14) = (uint32_t)n; }
			*(uint32_t*)(s + 0x10) = (uint32_t)n;
			void* spr = ((LoadIcon_t)Re(0x486A20))(gc, nullptr, s, nullptr);
			if (!spr) return false;
			void** v = ((MapIdx_t)Re(0x468910))((uint8_t*)gc + 0x800828, nullptr, &slot);
			if (!v) return false;
			*v = spr;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Native Anzeige der Plaetze 9/10 (2026-10-14, Cube.exe nachgelesen):
	//  * Aufbau 0x48E4A0: Knoten i (Vektor GC+0x800844, 11 Stueck) sichtbar = (i != 10) -> "setne cl" @0x48E4AF
	//    wird "mov cl,1" (alle 11 sichtbar).
	//  * Platz 9 ist nativ Kind von Platz 8: gedimmt (0x48E693) und nicht klickbar (0x47B96F), solange Platz 8 < 5
	//    Punkte hat; Platz 8 laesst sich nicht abbauen, solange 9 Punkte hat (0x47BA1F). Die drei "jge/jle" werden "jmp".
	//  * Knoten 10 positioniert das Spiel nie (Positionsblock 0x48DBA6 setzt 6,7,8,9,0..5) -> wir setzen ihn jedes
	//    Bild hinter Knoten 9 (gleicher Abstand wie 8 -> 9). Position: Knoten+0x38 -> Obj, Obj+0x48 = Feld
	//    (+0x4C Array, +0x20 Index, je 8 Byte float x,y), wie 0x4288B0.
	struct CodePatch { uint32_t a; uint8_t orig[3], neu[3]; int n; };
	static const CodePatch kTreePatches[] = {
		{ 0x48E4AF, { 0x0F, 0x95, 0xC1 }, { 0xB1, 0x01, 0x90 }, 3 },   // setne cl -> mov cl,1 ; nop
		{ 0x48E69A, { 0x7D, 0x4B, 0 }, { 0xEB, 0x4B, 0 }, 2 },          // Platz 9 nicht dimmen
		{ 0x47B976, { 0x7D, 0x3C, 0 }, { 0xEB, 0x3C, 0 }, 2 },          // Platz 9 klickbar
		{ 0x47BA26, { 0x7E, 0x31, 0 }, { 0xEB, 0x31, 0 }, 2 },          // Platz 8 abbaubar

	};
	static uint32_t TreePatchMismatch() {                                    // 0 = alle Stellen wie erwartet
		for (const CodePatch& c : kTreePatches) {
			uint8_t* p = (uint8_t*)Re(c.a);
			if (!p) return c.a;
			__try { if (memcmp(p, c.orig, c.n) != 0) return c.a; } __except (EXCEPTION_EXECUTE_HANDLER) { return c.a; }
		}
		return 0;
	}
	static int g_treePatched = -1;                                         // -1 noch nicht, 0 abweichend, 1 gesetzt
	static void InstallTreePatches() {
		if (g_treePatched >= 0) return;
		g_treePatched = 0;
		uint32_t bad = TreePatchMismatch();
		if (bad) { std::cout << "[Skills] Skillfenster-Stelle 0x" << std::hex << bad << std::dec << " weicht ab -- Plaetze 9/10 bleiben nativ verborgen." << std::endl; return; }
		for (const CodePatch& c : kTreePatches) {
			uint8_t* p = (uint8_t*)Re(c.a); DWORD old;
			if (!VirtualProtect(p, c.n, PAGE_EXECUTE_READWRITE, &old)) continue;
			memcpy(p, c.neu, c.n);
			DWORD d2; VirtualProtect(p, c.n, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, c.n);
		}
		g_treePatched = 1;
		std::cout << "[Skills] Skillfenster: Plaetze 9/10 (Faehigkeit 4/5) nativ sichtbar und klickbar." << std::endl;
	}
	static float* NodePosRaw(uint8_t* node) {
		uint8_t* obj = *(uint8_t**)(node + 0x38); if (!obj) return nullptr;
		uint8_t* fld = obj + 0x48; uint8_t* arr = *(uint8_t**)(fld + 0x4C); int idx = *(int*)(fld + 0x20);
		if (!arr || idx < 0 || idx > 4096) return nullptr;
		return (float*)(arr + idx * 8);
	}
	static void PlaceNode10Raw(cube::GameController* gc) {
		__try {
			uint8_t** b = *(uint8_t***)((uint8_t*)gc + 0x800844); uint8_t** e = *(uint8_t***)((uint8_t*)gc + 0x800848);
			if (!b || e - b < 11 || !b[8] || !b[9] || !b[10]) return;
			float* p8 = NodePosRaw(b[8]); float* p9 = NodePosRaw(b[9]); float* p10 = NodePosRaw(b[10]);
			if (!p8 || !p9 || !p10) return;
			p10[0] = p9[0] + (p9[0] - p8[0]); p10[1] = p9[1] + (p9[1] - p8[1]);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	// Diagnose (2026-10-16): beim Oeffnen des Fensters (X) je Knoten Sichtbarkeit (Knoten+0x3C -> Obj, Obj+0x94[Obj+0x68],
	// wie 0x411A90), Position (wie NodePosRaw) und Symbol (map GC+0x800828, ohne Einfuegen gelesen) protokollieren.
	static void* TreeIconLookupRaw(cube::GameController* gc, int key) {
		uint8_t* head = *(uint8_t**)((uint8_t*)gc + 0x800828); if (!head) return nullptr;
		uint8_t* n = *(uint8_t**)(head + 4); int guard = 0;
		while (n && n != head && !n[0xD] && guard++ < 64) {
			int k = *(int*)(n + 0x10);
			if (k == key) return *(void**)(n + 0x14);
			n = k < key ? *(uint8_t**)(n + 8) : *(uint8_t**)(n + 0);
		}
		return nullptr;
	}
	static int TreeDiagRaw(cube::GameController* gc, char* out, size_t cap) {
		__try {
			uint8_t** b = *(uint8_t***)((uint8_t*)gc + 0x800844); uint8_t** e = *(uint8_t***)((uint8_t*)gc + 0x800848);
			if (!b || e <= b) { sprintf_s(out, cap, "Knotenvektor leer"); return 0; }
			int n = (int)(e - b); size_t at = 0;
			at += sprintf_s(out + at, cap - at, "%d Knoten:", n);
			for (int i = 0; i < n && i < 11; i++) {
				uint8_t* nd = b[i]; int vis = -9; float x = -1, y = -1;
				if (nd) {
					uint8_t* o = *(uint8_t**)(nd + 0x3C);
					if (o && *(int**)(o + 0x94)) vis = (*(int**)(o + 0x94))[*(int*)(o + 0x68)];
					float* ps = NodePosRaw(nd); if (ps) { x = ps[0]; y = ps[1]; }
				}
				void* ic = TreeIconLookupRaw(gc, i);
				at += sprintf_s(out + at, cap - at, " [%d sichtbar=%d pos=%.0f,%.0f symbol=%p]", i, vis, x, y, ic);
				if (at + 80 > cap) break;
			}
			return n;
		} __except (EXCEPTION_EXECUTE_HANDLER) { sprintf_s(out, cap, "Ausnahme beim Lesen"); return -1; }
	}
	// ---- Skillfenster-Umbau (2026-10-19) -----------------------------------------------------------------
	// Teleport (Platz 9) und Beschwoeren (Platz 10) stehen je in einer eigenen Zeile unter den Plaetzen 0..5:
	//   [Freischalten / Manaverbrauch] (nativer Knoten, Punkte im nativen Puffer, mit dem Charakter gespeichert)
	//   + [Anzahl Steine / Tote]       (vom Mod gezeichnet, Punkte in AdventureMod_skillpunkte.txt je Charakter)
	// Nach der Knotenschleife blendet das Spiel Knoten 9 jedes Bild aus (0x48EAF3..0x48EB03, auch bei geschlossenem
	// Fenster). Dort ruft jetzt ein Stub TreeFixC: Sichtbarkeit 9/10 = Fenster offen, Position unter Knoten 4.
	static cube::GameController* volatile g_gcStub = nullptr;
	static int g_extraPts[11] = {}; static std::string g_extraChar; static bool g_extraLoaded = false; static volatile bool g_extraDirty = false;
	static float g_boxX[11], g_boxY[11]; static bool g_boxOk[11];                      // Lage der Anzahl-Kaesten (GUI = Bildpunkte)
	typedef void(__fastcall* SetVis_t)(void* node, void* edx, int vis);
	// ---- Skillfenster als echte Erweiterung des Originals (2026-10-19) ----------------------------------------
	// Alles mit den Widgets des Spiels (kein eigenes Zeichnen):
	//  * Knoten = GC+0x800844 (11 Widgets, Klone einer Vorlage, Eltern = Fensterfeld GC+0x800884; GC-ctor 0x4655A5..)
	//    Lage: Knoten+0x38 -> Obj, Obj+0x48 Feld (0x4288B0), danach Obj->vtbl[1](1) (wie 0x48DE1A..0x48DE2A).
	//    Zahl: setText 0x636AD0 (wie 0x48E738). Sichtbar 0x411A90. Maus darueber: (Knoten+0x40) -> 0x6294C0.
	//  * Pfeile "+" = GC+0x800838 (6 Klone, 0x4605F0).  Fenster GC+0x800908, Groesse 300 x 460 (GC-ctor 0x464E50,
	//    setSize 0x62C570 auf Knoten+0x40).
	// Teleport = Knoten 9, Beschwoeren = Knoten 10: je eine neue Zeile unter 4/5 (Zeilenabstand 60, Spalte 55).
	// Der zweite Kasten (Anzahl) ist ein KLON des Knotens (gleiches Symbol, gleicher Rahmen) mit eigenem Pfeil-Klon.
	// Punkte: der Anzahl-Kasten zaehlt IM nativen Platz mit (Puffer 9/10 = Freischalten + Anzahl) -- so stimmen
	// "Points" oben, Uebernehmen, Abbrechen und Zuruecksetzen wie im Original; die Anzahl steht zusaetzlich je Charakter
	// in AdventureMod_skillpunkte.txt (Freischalten = Puffer - Anzahl).
	typedef void* (__fastcall* WClone_t)(void* tmpl, void* edx, void* parent);
	typedef void  (__fastcall* WText_t)(void* w, void* edx, const void* wstr);
	typedef void  (__fastcall* WCtor_t)(void* wstr, void* edx, const wchar_t* s);
	typedef void  (__fastcall* WDtor_t)(void* wstr, void* edx);
	typedef char  (__fastcall* WHover_t)(void* obj, void* edx);
	typedef void  (__fastcall* WRefresh_t)(void* obj, void* edx, int f);
	typedef void  (__fastcall* WSize_t)(void* obj, void* edx, float w, float h, int f);
	static uint8_t* g_cnt[11] = {}; static uint8_t* g_cntArrow[11] = {}; static bool g_clonesTried = false, g_sized = false; static volatile bool g_treeLog = false;
	static bool g_lWas = false, g_rWas = false; static int g_clickSlot = -1, g_clickDelta = 0;
	static float* WPos(uint8_t* w) { uint8_t* obj = *(uint8_t**)(w + 0x38); if (!obj) return nullptr; uint8_t* f = obj + 0x48; uint8_t* arr = *(uint8_t**)(f + 0x4C); int i = *(int*)(f + 0x20); return (arr && i >= 0 && i < 4096) ? (float*)(arr + i * 8) : nullptr; }
	static void WSetPos(uint8_t* w, float x, float y) {
		float* p = WPos(w); if (!p) return; p[0] = x; p[1] = y;
		uint8_t* obj = *(uint8_t**)(w + 0x38); void** vt = *(void***)obj; ((WRefresh_t)vt[1])(obj, nullptr, 1);
	}
	static void WSetVis(uint8_t* w, int v) { ((SetVis_t)Re(0x411A90))(w, nullptr, v); }
	static void WSetNum(uint8_t* w, int n) {
		wchar_t t[16]; swprintf_s(t, L"%d", n);
		alignas(4) uint8_t s[32] = {}; ((WCtor_t)Re(0x40EB60))(s, nullptr, t);
		((WText_t)Re(0x636AD0))(w, nullptr, s); ((WDtor_t)Re(0x593E50))(s, nullptr);
	}
	static bool WHovered(uint8_t* w) { uint8_t* o = *(uint8_t**)(w + 0x40); return o && ((WHover_t)Re(0x6294C0))(o, nullptr) != 0; }
	// Kinder eines Widgets: std::list<Widget*> bei +0x2C (Knoten: next +0, Wert +8; Klonen 0x632763 geht sie so durch)
	static int WChildren(uint8_t* w, uint8_t** out, int maxN) {
		uint8_t* head = *(uint8_t**)(w + 0x2C); if (!head) return 0;
		int n = 0; for (uint8_t* nd = *(uint8_t**)head; nd && nd != head && n < maxN; nd = *(uint8_t**)nd) out[n++] = *(uint8_t**)(nd + 8);
		return n;
	}
	static int WVis(uint8_t* w) { uint8_t* o = *(uint8_t**)(w + 0x3C); if (!o) return -1; int* a = *(int**)(o + 0x94); return a ? a[*(int*)(o + 0x68)] : -1; }
	// Hoehe/Breite eines Elements (Rechteck +0x50..+0x7C; 0x627CE0 = Hoehe, 0x627D50 = Breite -- SkillWidget 0x4DF165 setzt
	// "Learn"/"Cancel" auf Hoehe - 0x6FCD74)
	typedef float (__fastcall* WDim_t)(void* el, void* edx);
	static int g_arrowChild = -2;                                          // Index des "+"-Kinds im Knoten (-1 = keins gefunden)
	static float g_skillH0 = 0, g_skillW0 = 0;
	static volatile int g_hoverSlot = -1;
	static void TreeFixRaw(cube::GameController* gc) {
		__try {
			uint8_t* G = (uint8_t*)gc;
			uint8_t** b = *(uint8_t***)(G + 0x800844); uint8_t** e = *(uint8_t***)(G + 0x800848);
			uint8_t** ab = *(uint8_t***)(G + 0x800838); uint8_t** ae = *(uint8_t***)(G + 0x80083C);
			if (!b || e - b < 11 || !b[4] || !b[5] || !b[8] || !b[9] || !b[10]) return;
			const int open = *(G + 0x8008F1) != 0 ? 1 : 0;
			uint8_t* wdg = *(uint8_t**)(G + 0x80090C); int* buf = wdg ? (int*)(wdg + 0x160) : nullptr;
			for (int i = 9; i <= 10; i++) { WSetVis(b[i], open); if (g_cnt[i]) WSetVis(g_cnt[i], open); if (g_cntArrow[i]) WSetVis(g_cntArrow[i], open); }
			if (!open || !buf) { g_sized = false; g_hoverSlot = -1; return; }
			float* p4 = WPos(b[4]); float* p5 = WPos(b[5]); float* p8 = WPos(b[8]);
			if (!p4 || !p5 || !p8) return;
			const float x0 = p4[0], x1 = p5[0], dy = 60.0f;
			// Fenster und SkillWidget (zeichnet "Points", "Learn", "Cancel" relativ zu seiner Hoehe) um zwei Zeilen hoeher
			if (!g_sized) {
				uint8_t* win = *(uint8_t**)(G + 0x800908); uint8_t* wo = win ? *(uint8_t**)(win + 0x40) : nullptr;
				if (wo) ((WSize_t)Re(0x62C570))(wo, nullptr, 300.0f, 460.0f + 2 * dy, 1);
				if (g_skillH0 <= 0) { g_skillH0 = ((WDim_t)Re(0x627CE0))(wdg, nullptr); g_skillW0 = ((WDim_t)Re(0x627D50))(wdg, nullptr); }
				if (g_skillH0 > 0) ((WSize_t)Re(0x62C570))(wdg, nullptr, g_skillW0, g_skillH0 + 2 * dy, 1);
				g_sized = true;
			}
			// "+"-Kind im Knoten finden: bei Knoten 1 sichtbar, bei Knoten 0 (Zeilenanfang) nicht
			if (g_arrowChild == -2) {
				uint8_t* c0[16]; uint8_t* c1[16]; int n0 = WChildren(b[0], c0, 16), n1 = WChildren(b[1], c1, 16);
				g_arrowChild = -1;
				for (int k = 0; k < n0 && k < n1; k++) if (WVis(c1[k]) == 1 && WVis(c0[k]) == 0) { g_arrowChild = k; break; }
				g_treeLog = true;
			}
			// Pfeil zwischen 4 und 5 (Vorlage + Versatz) und Pfeil rechts von 8 (fuehrte zum alten Platz 9 -> aus)
			uint8_t* arrow45 = nullptr; float adx = 0, ady = 0;
			for (uint8_t** a = ab; a && a < ae; ++a) {
				float* pa = *a ? WPos(*a) : nullptr; if (!pa) continue;
				if (std::fabs(pa[1] - p4[1]) < 30.0f && pa[0] > x0 && pa[0] < x1) { arrow45 = *a; adx = pa[0] - x0; ady = pa[1] - p4[1]; }
				if (std::fabs(pa[1] - p8[1]) < 30.0f && pa[0] > p8[0]) WSetVis(*a, 0);
			}
			// Klone einmal anlegen (nach der Knotenschleife: Symbol 9/10 ist schon gesetzt)
			if (!g_clonesTried) {
				g_clonesTried = true;
				void* parent = *(void**)(G + 0x800884);
				for (int i = 9; i <= 10; i++) {
					g_cnt[i] = (uint8_t*)((WClone_t)Re(0x6326D0))(b[i], nullptr, parent);
					if (arrow45 && g_arrowChild < 0) g_cntArrow[i] = (uint8_t*)((WClone_t)Re(0x6326D0))(arrow45, nullptr, parent);
				}
			}
			for (int i = 9; i <= 10; i++) {
				const float y = p4[1] + dy * (float)(i - 8);
				WSetPos(b[i], x0, y);
				if (g_arrowChild >= 0) {                                          // Zeilenanfang: kein "+", Anzahl-Kasten: "+"
					uint8_t* ch[16]; int n = WChildren(b[i], ch, 16); if (g_arrowChild < n) WSetVis(ch[g_arrowChild], 0);
					if (g_cnt[i]) { n = WChildren(g_cnt[i], ch, 16); if (g_arrowChild < n) WSetVis(ch[g_arrowChild], 1); }
				}
				for (uint8_t** a = ab; a && a < ae; ++a) {                         // freie "+"-Pfeile vor dem Zeilenanfang aus
					float* pa = *a ? WPos(*a) : nullptr;
					if (pa && std::fabs(pa[1] - y) < 30.0f && pa[0] < x0 + 4.0f) WSetVis(*a, 0);
				}
				int cnt = g_extraPts[i]; if (cnt > buf[i]) { cnt = buf[i]; g_extraPts[i] = cnt; g_extraDirty = true; }
				WSetNum(b[i], buf[i] - cnt);                                              // Freischalten / Mana
				if (g_cnt[i]) { WSetPos(g_cnt[i], x1, y); WSetVis(g_cnt[i], 1); WSetNum(g_cnt[i], cnt); }   // Anzahl
				if (g_cntArrow[i]) { WSetPos(g_cntArrow[i], x0 + adx, y + ady); WSetVis(g_cntArrow[i], 1); }
			}
			// Klick auf einen Anzahl-Kasten (Links +1, Rechts -1) -- verarbeitet im Spielthread (Tick)
			bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			{   // Maus ueber Platz 9/10 (Symbol oder Anzahl) -> Beschreibung (DrawHud)
				int hv = -1;
				for (int i = 9; i <= 10; i++) if ((b[i] && WHovered(b[i])) || (g_cnt[i] && WHovered(g_cnt[i]))) hv = i;
				g_hoverSlot = open ? hv : -1;
			}
			for (int i = 9; i <= 10; i++) if (g_cnt[i] && WHovered(g_cnt[i])) {
				if (l && !g_lWas) { g_clickSlot = i; g_clickDelta = +1; }
				if (r && !g_rWas) { g_clickSlot = i; g_clickDelta = -1; }
			}
			g_lWas = l; g_rWas = r;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void __cdecl TreeFixC() {
		cube::GameController* gc = g_gcStub; if (!gc) return;
		TreeFixRaw(gc);
	}
	static bool g_stubOk = false;
	static void InstallTreeStub(cube::GameController* gc) {
		if (g_stubOk || !gc) return;
		g_gcStub = gc;
		uint8_t* site = (uint8_t*)Re(0x48EAFC);
		static const uint8_t orig[7] = { 0x6A, 0x00, 0xE8, 0x8D, 0x2F, 0xF8, 0xFF };   // push 0; call 0x411A90
		__try { if (!site || memcmp(site, orig, 7) != 0) { g_stubOk = true; std::cout << "[Skills] Stelle 0x48EAFC weicht ab -- Platz 9/10 bleiben, wo sie sind." << std::endl; return; } }
		__except (EXCEPTION_EXECUTE_HANDLER) { g_stubOk = true; return; }
		uint8_t* stub = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!stub) { g_stubOk = true; return; }
		int k = 0;
		stub[k++] = 0x60;                                                                  // pushad
		stub[k++] = 0xE8; *(int32_t*)(stub + k) = (int32_t)((uintptr_t)&TreeFixC - ((uintptr_t)stub + k + 4)); k += 4;
		stub[k++] = 0x61;                                                                  // popad
		stub[k++] = 0xC3;                                                                  // ret
		DWORD old; VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &old);
		site[0] = 0xE8; *(int32_t*)(site + 1) = (int32_t)((uintptr_t)stub - ((uintptr_t)site + 5)); site[5] = 0x90; site[6] = 0x90;
		DWORD d2; VirtualProtect(site, 7, old, &d2); FlushInstructionCache(GetCurrentProcess(), site, 7);
		g_stubOk = true;
		std::cout << "[Skills] Skillfenster: Teleport/Beschwoeren in eigenen Zeilen, nur bei offenem Fenster sichtbar." << std::endl;
	}
	static void CharNameCopy(cube::GameController* gc, char* n) {          // SEH ohne Objekte (C2712)
		__try { if (gc->local_player) memcpy(n, (uint8_t*)gc->local_player + 0x1168, 16); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static std::string CharNameRaw(cube::GameController* gc) { char n[17] = {}; CharNameCopy(gc, n); return n; }
	// Anzahl-Punkte je Charakter: in der Charakter-Datenbank des Spiels (Save/characters.db, Schluessel
	// "AdventureMod:<Name>:skillpunkte", Zeilen "platz;punkte"). Alte Datei AdventureMod_skillpunkte.txt wird uebernommen.
	static cube::GameController* g_extraGc = nullptr;
	static void LoadExtra(cube::GameController* gc, const std::string& who) {
		for (int& v : g_extraPts) v = 0;
		g_extraGc = gc;
		std::string txt;
		if (ModDb::CharRead(gc, "skillpunkte", txt)) {
			std::istringstream f(txt); std::string line;
			while (std::getline(f, line)) { size_t a2 = line.find(';'); if (a2 == std::string::npos) continue; int slot = atoi(line.c_str()); if (slot >= 0 && slot < 11) g_extraPts[slot] = (std::max)(0, atoi(line.c_str() + a2 + 1)); }
		} else {                                                            // alte Datei (alle Charaktere): Zeilen "Name;Platz;Punkte"
			std::istringstream f(ModDb::Txt("AdventureMod_skillpunkte.txt")); std::string line;
			while (std::getline(f, line)) {
				size_t a2 = line.find(';'), b2 = a2 == std::string::npos ? a2 : line.find(';', a2 + 1); if (b2 == std::string::npos) continue;
				if (line.substr(0, a2) != who) continue;
				int slot = atoi(line.substr(a2 + 1, b2 - a2 - 1).c_str()); if (slot >= 0 && slot < 11) g_extraPts[slot] = (std::max)(0, atoi(line.substr(b2 + 1).c_str()));
			}
			g_extraDirty = true;                                             // gleich in die Charakter-Datenbank
		}
		g_extraChar = who; g_extraLoaded = true;
	}
	static void SaveExtra() {
		if (!g_extraGc || g_extraChar.empty() || ModDb::CharName(g_extraGc) != g_extraChar) return;
		std::ostringstream o;
		for (int i = 0; i < 11; i++) if (g_extraPts[i] > 0) o << i << ";" << g_extraPts[i] << "\n";
		ModDb::CharWrite(g_extraGc, "skillpunkte", o.str());
	}
	static int ExtraSum() { int s = 0; for (int v : g_extraPts) s += v; return s; }
	static int CountRank(const SkillDef& d, int rankFallback) {           // Rang der Anzahl: 1 + Punkte/Punkte-je-Rang
		if (d.treeSlot < 0 || d.treeSlot > 10) return rankFallback;
		return (std::min)(d.maxRank, 1 + g_extraPts[d.treeSlot] / (std::max)(1, d.pointsPerRank));
	}
	static bool g_treeIconsOk = false;
	static void RegisterTreeIcons(cube::GameController* gc) {
		static DWORD lastTry = 0; DWORD now = GetTickCount();
		if (g_treeIconsOk || now - lastTry < 5000) return;
		lastTry = now;
		std::vector<SkillDef>* set = g_set; if (!set) return;
		int ok = 0, want = 0;
		for (const SkillDef& d : *set) {
			if (!d.enabled || d.treeSlot < 9 || d.treeSlot > 10) continue;
			want++;
			char full[MAX_PATH] = ""; std::string rel = IconPath(d.icon);
			if (rel.empty() || !GetFullPathNameA(rel.c_str(), MAX_PATH, full, nullptr)) continue;
			if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) continue;
			if (RegisterTreeIconRaw(gc, d.treeSlot, full)) ok++;
		}
		g_treeIconsOk = want > 0 && ok == want;
		std::cout << "[Skills] Skillfenster: " << ok << " von " << want << " Symbolen fuer Faehigkeit 4/5 eingetragen"
		          << (g_treeIconsOk ? " -- erscheinen nativ im Fenster (X)." : " -- Zusatzleiste bleibt aktiv.") << std::endl;
	}
	static int RankOf(const SkillDef& d, int level) {                       // Freischalten = Platz - Anzahl (Anzahl zaehlt im Platz mit)
		if (d.treeSlot < 0) return RankFor(d, level);
		int pts = g_treePts[d.treeSlot]; if (d.treeSlot >= 9 && d.treeSlot <= 10) pts -= (std::min)(pts, g_extraPts[d.treeSlot]);
		return RankForPoints(d, pts);
	}

	// Steinliste (Spielthread; die Leiste zeichnet sie)
	static bool g_listOpen = false; static int g_listSel = 0; static DWORD g_listUntil = 0; static bool g_listNativeOk = false;

	// ---- Arten ---------------------------------------------------------------------
	static int MaxStones(const SkillDef& d, int rank) { return (std::max)(1, (int)std::floor(ValueAt(d, CountRank(d, rank)) + 0.001f)); }
	static bool PortToStone(cube::GameController* gc, const SkillDef& d, const Stone& st, std::string& msg) {
		int tz;
		const int64_t U = 65536;
		for (int k = 0; k < 8; k++) {                                       // neben den Stein (nicht hinein)
			int tx = st.x + (k == 0 ? 2 : k == 1 ? -2 : k == 2 ? 0 : k == 3 ? 0 : (k & 1) ? 3 : -3), ty = st.y + (k == 2 ? 2 : k == 3 ? -2 : (k >= 4 ? ((k & 2) ? 3 : -3) : 0));
			if (!LandingZ(gc, tx, ty, st.z, &tz)) continue;
			if (!MovePlayer(gc, (int64_t)tx * U + U / 2, (int64_t)ty * U + U / 2, (int64_t)tz * U)) break;
			msg = d.name + ": " + st.name + "."; return true;
		}
		if (MovePlayer(gc, (int64_t)st.x * U + U / 2, (int64_t)(st.y + 2) * U + U / 2, (int64_t)(st.z + 2) * U)) { msg = d.name + ": " + st.name + " (Boden nicht geladen)."; return true; }
		msg = d.name + ": Teleport fehlgeschlagen."; return false;
	}
	static bool PlaceOrPick(cube::GameController* gc, const SkillDef& d, int rank, const PlayerRaw& p, std::string& msg) {
		int px = (int)(p.x >> 16), py = (int)(p.y >> 16), pz = (int)(p.z >> 16);
		// neben einem eigenen Stein: aufheben
		Stone hit; bool found = false;
		{
			StoneLock l;
			for (size_t i = 0; i < g_stones.size(); i++) {
				const Stone& st = g_stones[i];
				if (std::abs(st.x - px) <= 4 && std::abs(st.y - py) <= 4 && std::abs(st.z - pz) <= 4) { hit = st; found = true; g_stones.erase(g_stones.begin() + i); SaveStonesLocked(); break; }
			}
		}
		if (found) {
			cube::Zone* z = LabBridge::ZoneAt(gc, hit.x >> 8, hit.y >> 8);
			if (z) HideStoneRaw(z, hit.x, hit.y, hit.z);
			LabBridge::RemeshArea(gc, hit.x - 8, hit.y - 8, hit.x + 8, hit.y + 8);
			msg = d.name + ": Runenstein \"" + hit.name + "\" aufgehoben."; return true;
		}
		int have; { StoneLock l; have = (int)g_stones.size(); }
		int maxN = MaxStones(d, rank);
		if (have >= maxN) { msg = d.name + ": hoechstens " + std::to_string(maxN) + " Runenstein(e) auf Rang " + std::to_string(rank) + " -- U neben einem Stein hebt ihn auf."; return false; }
		// 2 Bloecke vor dem Spieler auf den Boden
		int tx = px + (int)std::lround(g_dirX * 2.0f), ty = py + (int)std::lround(g_dirY * 2.0f), tz;
		if (!LandingZ(gc, tx, ty, pz, &tz)) { tx = px; ty = py; tz = pz; }
		AskStoneName(gc, "Runenstein " + std::to_string(have + 1), tx, ty, tz);
		return false;                                                          // abgelegt wird nach der Eingabe
	}
	static void FinishPlacement(cube::GameController* gc, const SkillDef* d) {
		if (g_nativeAsk.on) {                                                    // Eingabefenster im Spielstil
			std::string txt; int st = NativeUi::PollText(gc, txt);
			if (st == 1 || st == -1 || st == -2) {
				EnterCriticalSection(&NameCs());
				g_nameReq.done = true; g_nameReq.ok = st == 1; g_nameReq.name = txt.empty() ? g_nativeAsk.def : txt;
				g_nameReq.x = g_nativeAsk.x; g_nameReq.y = g_nativeAsk.y; g_nameReq.z = g_nativeAsk.z;
				LeaveCriticalSection(&NameCs());
				g_nativeAsk.on = false;
			}
		}
		NameReq r;
		EnterCriticalSection(&NameCs()); r = g_nameReq; g_nameReq.done = false; LeaveCriticalSection(&NameCs());
		if (!r.done) return;
		std::string nm = d ? d->name : std::string("Teleport");
		if (!r.ok) { Chat(gc, nm + ": Ablegen abgebrochen."); return; }
		Stone st{ r.name.empty() ? std::string("Runenstein") : r.name, r.x, r.y, r.z };
		std::vector<MountainVillage::LabObj> objs(1);
		objs[0] = { kStoneType, st.x, st.y, st.z, 0, kStoneSx, kStoneSy, kStoneSz };
		int made = MountainVillage::LabCreateObjects(gc, objs);
		LabBridge::RemeshArea(gc, st.x - 8, st.y - 8, st.x + 8, st.y + 8);
		{ StoneLock l; g_stones.push_back(st); SaveStonesLocked(); }
		Chat(gc, nm + ": Runenstein \"" + st.name + "\" abgelegt" + (made > 0 ? "." : " (erscheint, sobald die Zone neu geladen wird)."));
	}
	static bool DoTeleportTo(cube::GameController* gc, const SkillDef& d, int idx, std::string& msg) {
		Stone st; { StoneLock l; if (idx < 0 || idx >= (int)g_stones.size()) { msg = d.name + ": kein Runenstein."; return false; } st = g_stones[idx]; }
		return PortToStone(gc, d, st, msg);
	}
	// ---- Unsichtbare Haustier-Plaetze (2026-10-27) ------------------------------------------------------------------
	// Warum die Beschworenen sofort starben: GameController_updateSubsystems prueft je Bild jede Kreatur mit Feindlichkeit 5
	// (0x60EAB1..0x60EB0B): Besitzer = Kreatur +0x198 (Lookup 0x42F000); ist Besitzer+0x11C8 (sein EINER Haustierplatz)
	// nicht ihre GUID, wird sie entlassen: Leben 0, Feindlichkeit 3, +0x6C = 5000, Besitzer 0. Der Spieler hat nur diesen
	// einen Platz (das echte Haustier) -> jeder weitere Begleiter fiel im ersten Bild tot um.
	// Abhilfe: Hoehle an 0x60EAC7 (22 Bytes: Vergleich +0x11C8/+0x11CC). Stimmt der native Platz nicht, fragt sie zusaetzlich
	// die Mod-Plaetze (kGhostSlots, je Besitzer-GUID). Die Beschwoerung FUELLT einen Platz; ist die Dauer um, wird er GELEERT
	// -> das Spiel entlaesst die Kreatur selbst (nativer Ablauf wie beim Wegschicken eines Haustiers).
	static const int kGhostSlots = 16;
	struct GhostSlot { uint64_t owner, pet; DWORD end; int race, level; };     // ein Begleiter-Platz (Feindlichkeit 5, Besitzer = Spieler)
	static volatile GhostSlot g_ghost[kGhostSlots];
	static volatile LONG g_ghostPatched = 0;                               // 1 ok, -1 Bytes passen nicht
	static int __cdecl GhostPetC(const uint8_t* owner, const uint8_t* pet) {
		__try {
			const uint64_t og = *(const uint64_t*)(owner + 8), pg = *(const uint64_t*)(pet + 8);
			for (int i = 0; i < kGhostSlots; i++) if (g_ghost[i].pet == pg && g_ghost[i].owner == og && pg) return 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return 0;
	}
	// Taste O oeffnet nativ das Systemmenue (GameController::handleKeyDown, Infinite-Cubes GameController.cpp:4506;
	// Cube.exe: Tastentabelle 0x47E95C[Taste - 9], 'O' -> Fall 7 = 0x47E905 toggleSystemWidget). Liegt ein Skill auf O,
	// wird der Eintrag auf "nichts" (Fall 0x0B) gestellt -- das Systemmenue bleibt ueber den Menueknopf "O" erreichbar.
	static volatile LONG g_oPatched = 0;
	static void InstallOKeyPatch() {
		if (g_oPatched) return;
		uint8_t* p = (uint8_t*)(uintptr_t)Re(0x47E95C + ('O' - 9));
		bool ok = false; __try { ok = p && *p == 0x07; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (!ok) { InterlockedExchange(&g_oPatched, -1); std::cout << "[Skills] Tastentabelle fuer O weicht ab -- Systemmenue bleibt auf O." << std::endl; return; }
		DWORD old; VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old); *p = 0x0B; DWORD d2; VirtualProtect(p, 1, old, &d2);
		InterlockedExchange(&g_oPatched, 1);
		std::cout << "[Skills] Taste O gehoert jetzt dem Teleport (Systemmenue nur noch ueber den Menueknopf)." << std::endl;
	}
	static void InstallGhostPetPatch() {
		if (g_ghostPatched) return;
		static const uint8_t orig[22] = { 0x8B,0x90,0xC8,0x11,0x00,0x00, 0x3B,0x57,0x08, 0x75,0x0B, 0x8B,0x80,0xCC,0x11,0x00,0x00, 0x3B,0x47,0x0C, 0x74,0x38 };
		const uint32_t site = Re(0x60EAC7), keep = Re(0x60EB15), drop = Re(0x60EADD);
		uint8_t* p = (uint8_t*)(uintptr_t)site;
		bool same = false; __try { same = memcmp(p, orig, 22) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (!site || !same) { InterlockedExchange(&g_ghostPatched, -1); std::cout << "[Skills] Haustier-Plaetze: Bytes an 0x60EAC7 passen nicht -- Beschwoeren bleibt aus." << std::endl; return; }
		auto rel = [](uint8_t* at, uint32_t to) { *(int32_t*)at = (int32_t)(to - ((uint32_t)(uintptr_t)at + 4)); };
		int k = 0;
		uint8_t* c = (uint8_t*)VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!c) return;
		k = 0;
		c[k++] = 0x50;                                                     // push eax            (Besitzer)
		c[k++] = 0x8B; c[k++] = 0x90; *(uint32_t*)(c + k) = 0x11C8; k += 4; // mov edx,[eax+11C8]
		c[k++] = 0x3B; c[k++] = 0x57; c[k++] = 0x08;                       // cmp edx,[edi+8]
		c[k++] = 0x75; c[k++] = 0x0D;                                      // jne mod (+13 -> 25)
		c[k++] = 0x8B; c[k++] = 0x90; *(uint32_t*)(c + k) = 0x11CC; k += 4; // mov edx,[eax+11CC]
		c[k++] = 0x3B; c[k++] = 0x57; c[k++] = 0x0C;                       // cmp edx,[edi+C]
		c[k++] = 0x75; c[k++] = 0x02;                                      // jne mod
		c[k++] = 0xEB; c[k++] = 0x11;                                      // jmp ok (+17 -> 42)
		// mod: (k == 25)
		c[k++] = 0x57;                                                     // push edi            (Haustier)
		c[k++] = 0xFF; c[k++] = 0x74; c[k++] = 0x24; c[k++] = 0x04;        // push [esp+4]        (Besitzer)
		c[k++] = 0xE8; rel(c + k, (uint32_t)(uintptr_t)&GhostPetC); k += 4; // call GhostPetC
		c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x08;                       // add esp,8
		c[k++] = 0x85; c[k++] = 0xC0;                                      // test eax,eax
		c[k++] = 0x74; c[k++] = 0x06;                                      // jz entlassen
		// ok: (k == 42)
		c[k++] = 0x58;                                                     // pop eax
		c[k++] = 0xE9; rel(c + k, keep); k += 4;                           // jmp 0x60EB15 (behalten)
		// entlassen: (k == 48)
		c[k++] = 0x58;                                                     // pop eax
		c[k++] = 0xE9; rel(c + k, drop); k += 4;                           // jmp 0x60EADD (natives Entlassen)
		uint8_t repl[22]; memset(repl, 0x90, sizeof(repl));
		repl[0] = 0xE9; *(int32_t*)(repl + 1) = (int32_t)((uint32_t)(uintptr_t)c - (site + 5));
		DWORD old; if (!VirtualProtect(p, 22, PAGE_EXECUTE_READWRITE, &old)) return;
		memcpy(p, repl, 22); DWORD d2; VirtualProtect(p, 22, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, 22);
		InterlockedExchange(&g_ghostPatched, 1);
		std::cout << "[Skills] Unsichtbare Haustier-Plaetze aktiv (" << kGhostSlots << ", Hoehle an 0x60EAC7)." << std::endl;
	}
	// ---- Beschworene sind NICHT das Haustier (2026-10-29) ------------------------------------------------------------
	// 1) Begleiterlevel: GameController_updateSubsystems verknuepft je Bild JEDE Kreatur mit Feindlichkeit 5 mit ihrem
	//    Besitzer: Besitzer+0x11C8/+0x11CC = GUID der Kreatur (0x60DE42..0x60DE76; Infinite-Cubes WorldUpdate.cpp:777
	//    "links each pet to its owner"). Mit Beschworenen gewann der zuletzt durchlaufene -> der Haustierplatz zeigte auf
	//    einen Beschworenen (Level/Anzeige/Reittier = seine). Hoehle an 0x60DE5E (12 Bytes: mov ecx,[ebp-0x2B18];
	//    push edi; call 0x42F000): steht die Kreatur (esi) in einem Mod-Platz, wird sie uebersprungen (-> 0x60DE7C).
	// 2) Reiten: reitet der Besitzer (Faehigkeit +0x68 = 106, AbilityRidePet; Original z.B. 0x619306 cmp [esi+0x68],0x6A),
	//    setzt das Spiel JEDE Kreatur mit Feindlichkeit 5 und diesem Besitzer (+0x198) unter ihn (WorldUpdate.cpp:4689,
	//    5213). Solange der Spieler reitet, nehmen wir den Beschworenen den Besitzer (+0x198 = 0) und geben ihn danach
	//    zurueck. Ohne Besitzer ueberspringt auch die Entlassungs-Pruefung sie (0x60EAC5: Lookup 0 -> behalten); ihre
	//    Begleiter-KI folgt weiter (CompanionBehavior hat die Besitzer-GUID selbst).
	static int __cdecl GhostIsSummon(const uint8_t* pet) {
		__try {
			const uint64_t pg = *(const uint64_t*)(pet + 8); if (!pg) return 0;
			for (int i = 0; i < kGhostSlots; i++) if (g_ghost[i].pet == pg) return 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return 0;
	}
	static volatile LONG g_linkPatched = 0;
	static void InstallLinkPatch() {
		if (g_linkPatched) return;
		const uint32_t site = Re(0x60DE5E), look = Re(0x42F000), back = Re(0x60DE6A), skip = Re(0x60DE7C);
		uint8_t* p = (uint8_t*)(uintptr_t)site;
		static const uint8_t head[7] = { 0x8B, 0x8D, 0xE8, 0xD4, 0xFF, 0xFF, 0x57 };
		bool ok = false;
		__try { ok = memcmp(p, head, 7) == 0 && p[7] == 0xE8 && (uint32_t)((uintptr_t)p + 12 + *(int32_t*)(p + 8)) == look; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (!site || !ok) { InterlockedExchange(&g_linkPatched, -1); std::cout << "[Skills] Haustier-Verknuepfung: Bytes an 0x60DE5E passen nicht." << std::endl; return; }
		uint8_t* c = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!c) return;
		auto rel = [](uint8_t* at, uint32_t to) { *(int32_t*)at = (int32_t)(to - ((uint32_t)(uintptr_t)at + 4)); };
		int k = 0;
		c[k++] = 0x56;                                                     // push esi (Kreatur)
		c[k++] = 0xE8; rel(c + k, (uint32_t)(uintptr_t)&GhostIsSummon); k += 4;
		c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x04;                       // add esp,4
		c[k++] = 0x85; c[k++] = 0xC0;                                      // test eax,eax
		c[k++] = 0x75; c[k++] = 0x11;                                      // jnz skip (+17)
		memcpy(c + k, head, 7); k += 7;                                    // mov ecx,[ebp-2B18]; push edi
		c[k++] = 0xE8; rel(c + k, look); k += 4;                           // call 0x42F000
		c[k++] = 0xE9; rel(c + k, back); k += 4;                           // jmp 0x60DE6A
		c[k++] = 0xE9; rel(c + k, skip); k += 4;                           // skip: jmp 0x60DE7C
		uint8_t repl[12]; memset(repl, 0x90, sizeof(repl));
		repl[0] = 0xE9; *(int32_t*)(repl + 1) = (int32_t)((uint32_t)(uintptr_t)c - (site + 5));
		DWORD old; if (!VirtualProtect(p, 12, PAGE_EXECUTE_READWRITE, &old)) return;
		memcpy(p, repl, 12); DWORD d2; VirtualProtect(p, 12, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, 12);
		InterlockedExchange(&g_linkPatched, 1);
		std::cout << "[Skills] Beschworene zaehlen nicht mehr als Haustier (Hoehle an 0x60DE5E)." << std::endl;
	}
	static bool g_detached[kGhostSlots] = {};
	static void SetOwnerRaw(cube::GameController* gc, uint64_t guid, uint64_t owner) {
		uint8_t* c = (uint8_t*)MobLab::CreatureByGuid(gc, guid); if (!c) return;
		__try { *(uint64_t*)(c + 0x198) = owner; } __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static bool PlayerRidesRaw(cube::GameController* gc) {
		__try { uint8_t* c = (uint8_t*)gc->local_player; return c && c[0x68] == 0x6A; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void RideTick(cube::GameController* gc) {
		const bool ride = PlayerRidesRaw(gc);
		for (int i = 0; i < kGhostSlots; i++) {
			if (!g_ghost[i].pet) { g_detached[i] = false; continue; }
			if (ride && !g_detached[i]) { SetOwnerRaw(gc, g_ghost[i].pet, 0); g_detached[i] = true; }
			else if (!ride && g_detached[i]) { SetOwnerRaw(gc, g_ghost[i].pet, g_ghost[i].owner); g_detached[i] = false; }
		}
	}
	static int GhostFree() { int n = 0; for (int i = 0; i < kGhostSlots; i++) if (!g_ghost[i].pet) n++; return n; }
	static int GhostUsed(uint64_t owner) { int n = 0; for (int i = 0; i < kGhostSlots; i++) if (g_ghost[i].pet && g_ghost[i].owner == owner) n++; return n; }
	static int GhostFill(uint64_t owner, uint64_t pet, DWORD end, int race, int level) {
		for (int i = 0; i < kGhostSlots; i++) if (!g_ghost[i].pet) { g_ghost[i].owner = owner; g_ghost[i].end = end; g_ghost[i].race = race; g_ghost[i].level = level; g_ghost[i].pet = pet; return i; }
		return -1;
	}
	// Zustand eines Beschworenen: Feindlichkeit +0x60, Besitzer +0x198, Leben +0x16C
	static bool PetStateRaw(cube::GameController* gc, uint64_t guid, int* hos, uint64_t* owner, float* hp) {
		uint8_t* c = (uint8_t*)MobLab::CreatureByGuid(gc, guid);
		if (!c) return false;
		__try { *hos = c[0x60]; *owner = *(uint64_t*)(c + 0x198); *hp = *(float*)(c + 0x16C); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void Log(const std::string& s) { std::cout << "[Beschwoeren] " << s << std::endl; }
	static bool SpawnSummonRaw(cube::GameController* gc, int race, int level, int64_t x, int64_t y, int64_t z, uint64_t owner, uint64_t* out);
	static std::string RN(int race) { const char* n = MobLab::RaceName(race); return n ? n : "?"; }
	// Einen Beschworenen als Begleiter erzeugen und in einen freien Platz legen. -1 = fehlgeschlagen.
	static int SummonInto(cube::GameController* gc, const PlayerRaw& p, int race, int level, float secs, float angle) {
		if (GhostFree() <= 0) { Log("kein freier Platz"); return -1; }
		const int64_t U = 65536;
		int64_t x = p.x + (int64_t)(std::cos(angle) * 3.0f * U), y = p.y + (int64_t)(std::sin(angle) * 3.0f * U), z = p.z + U;
		uint64_t g = 0;
		if (!SpawnSummonRaw(gc, race, level, x, y, z, p.guid, &g) || g <= 1) { Log("Spawn fehlgeschlagen, Rasse " + std::to_string(race)); return -1; }
		DWORD end = GetTickCount() + (DWORD)(secs * 1000.0f);
		int slot = GhostFill(p.guid, g, end, race, level);                   // im selben Bild wie der Spawn -> keine Entlassung
		{   // Auftauchen: schwarzer Partikelstoss aus dem Boden + aufsteigende dunkle Spirale (1.8 s)
			const float fx = x / 65536.0f, fy = y / 65536.0f, fz = z / 65536.0f - 1.0f;
			Effects::Burst(gc, fx, fy, fz, 0.04f, 0.03f, 0.06f, 45, 0.4f, 2.5f, 6.0f, 2, 1500);
			Effects::Burst(gc, fx, fy, fz, 0.35f, 0.1f, 0.5f, 12, 0.25f, 1.5f, 4.0f, 2, 1200);       // ein wenig Violett
			Effects::AddEmitter(fx, fy, fz, 0.05f, 0.04f, 0.08f, 1800, 40, 1, 1.3f);
		}
		int hos = -1; uint64_t own = 0; float hp = 0;
		PetStateRaw(gc, g, &hos, &own, &hp);
		char b[200]; sprintf_s(b, "Platz %d: %s Lv %d, GUID %llu, Feindlichkeit %d, Besitzer %s, Leben %.0f, %.0f s",
		                       slot, RN(race).c_str(), level, (unsigned long long)g, hos, own == p.guid ? "Spieler" : "FALSCH", hp, secs);
		Log(b);
		if (slot >= 0) g_summons.push_back({ g, end });
		return slot;
	}
	static void GhostEmpty(int i) { g_ghost[i].pet = 0; g_ghost[i].owner = 0; g_ghost[i].end = 0; }
	static void GhostRelease(cube::GameController* gc, int i) {        // Besitzer zurueck, damit das Spiel ihn entlaesst
		if (g_detached[i] && g_ghost[i].pet) SetOwnerRaw(gc, g_ghost[i].pet, g_ghost[i].owner);
		g_detached[i] = false; GhostEmpty(i);
	}

	// Beschwoeren: die zuletzt besiegten Gegner (neueste zuerst) als Begleiter -- das eigene Haustier bleibt.
	static bool SpawnSummonRaw(cube::GameController* gc, int race, int level, int64_t x, int64_t y, int64_t z, uint64_t owner, uint64_t* out) {
		uint8_t* me = (uint8_t*)gc->local_player;
		uint64_t petSlot = 0; bool saved = false;
		__try { petSlot = *(uint64_t*)(me + 0x11C8); saved = true; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		*out = race >= MobLab::kRaceCount ? MobLab::SpawnOwnCompanionAt(gc, race, level, x, y, z, owner)   // eigene Rasse (.mob)
		                                  : MobLab::SpawnCompanionAt(gc, race, level, x, y, z, owner, true);
		if (saved) { __try { *(uint64_t*)(me + 0x11C8) = petSlot; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
		return *out != 0;
	}
	static bool DoSummon(cube::GameController* gc, const SkillDef& d, int rank, const PlayerRaw& p, std::string& msg) {
		InstallGhostPetPatch();
		if (g_ghostPatched != 1) { msg = d.name + ": Begleiter-Plaetze nicht verfuegbar (Spielversion?)."; Log(msg); return false; }
		int slots = (std::min)(kGhostSlots, (std::max)(1, (int)std::floor(ValueAt(d, CountRank(d, rank)) + 0.001f)));
		int n = slots - GhostUsed(p.guid);
		char hb[160]; sprintf_s(hb, "Taste: Rang %d, Plaetze %d (belegt %d), besiegte Gegner gemerkt %d", rank, slots, GhostUsed(p.guid), (int)g_kills.size()); Log(hb);
		if (n <= 0) { msg = d.name + ": alle " + std::to_string(slots) + " Plaetze belegt -- warte, bis Beschworene vergehen."; return false; }
		if (g_kills.empty()) { msg = d.name + ": noch keine besiegten Gegner in deiner Naehe (Gegner im Umkreis von 64 Bloecken besiegen)."; return false; }
		float secs = DurationAt(d, rank); if (secs < 5.0f) secs = 5.0f;
		int ok = 0, big = 0; std::string names;
		for (int i = 0; i < n && !g_kills.empty(); i++) {
			Kill k = g_kills.front(); g_kills.pop_front();                       // jeder Besiegte kommt einmal zurueck
			if (k.big) { big++; i--; Log("zu gross, nicht beschworen: " + RN(k.race)); continue; }
			int lv = (std::min)(k.level > 0 ? k.level : p.level, p.level);
			if (SummonInto(gc, p, k.race, lv, secs, 6.2832f * (float)i / (float)n) >= 0) {
				ok++; if (!names.empty()) names += ", "; names += MobLab::RaceName(k.race);
			}
		}
		if (ok == 0 && big > 0) { msg = d.name + ": grosse Tiere lassen sich nicht beschwoeren."; return false; }
		char b[128]; sprintf_s(b, ": %d Begleiter fuer %.0f s, Plaetze %d/%d (", ok, secs, GhostUsed(p.guid), slots); msg = d.name + b + names + ").";
		return ok > 0;
	}
	void TestSummon(cube::GameController* gc, int race) {
		if (!gc || !gc->local_player) return;
		InstallGhostPetPatch();
		PlayerRaw p; if (!ReadPlayerRaw(gc, &p)) return;
		if (g_ghostPatched != 1) { Chat(gc, "Beschwoeren-Test: Begleiter-Plaetze nicht verfuegbar."); return; }
		int slot = SummonInto(gc, p, race, p.level > 0 ? p.level : 1, 60.0f, 0.0f);
		Chat(gc, slot >= 0 ? "Beschwoeren-Test: " + RN(race) + " in Platz " + std::to_string(slot) + " fuer 60 s (Feindlichkeit 5, Besitzer du)."
		                   : std::string("Beschwoeren-Test fehlgeschlagen (siehe Log)."));
	}
	void FreeSummons(cube::GameController* gc) {
		int n = 0; for (int i = 0; i < kGhostSlots; i++) if (g_ghost[i].pet) {
			int64_t cx, cy, cz;
			if (MobLab::CreaturePos(gc, g_ghost[i].pet, &cx, &cy, &cz))                // schwarze Wolke beim Wegschicken
				Effects::Burst(gc, cx / 65536.0f, cy / 65536.0f, cz / 65536.0f, 0.03f, 0.03f, 0.05f, 50, 0.45f, 3.0f, 3.5f, 2, 1600);
			GhostRelease(gc, i); n++;
		}
		Log("alle Plaetze geleert (" + std::to_string(n) + ")");
		if (gc) Chat(gc, "Beschwoeren: " + std::to_string(n) + " Begleiter entlassen.");
	}

	static const char* KeyName(int vk, char* buf);
	static bool GameHasFocus() {
		HWND fg = GetForegroundWindow(); DWORD pid = 0;
		if (!fg) return false;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId() && !LabWindow::IsForeground() && !g_nameOpen && !NativeUi::Typing()
		       && NativeUi::PlayInputCached();                                    // nur im Spiel: kein Menue, kein Chat, Zone geladen
	}
	static bool Pressed(int vk, bool focus) {                               // Flanke (gedrueckt seit dem letzten Tick)
		if (vk <= 0) return false;
		bool down = focus && (GetAsyncKeyState(vk) & 0x8000) != 0;
		bool& was = g_keyDown[vk];
		bool edge = down && !was; was = down; return edge;
	}
	// Manaverbrauch: je Rang im Freischalt-Kasten 10 % weniger (hoechstens -60 %)
	static float ManaCost(const SkillDef& d, int level) {
		int r = RankOf(d, level); float f = 1.0f - 0.1f * (float)(std::max)(0, r - 1); if (f < 0.4f) f = 0.4f;
		return (float)d.mana / 100.0f * f;
	}
	// ---- Freischaltung durch ein Gespraech (je Charakter, Charakter-DB) -----------------------------------
	static std::string g_unlockChar; static std::map<std::string, bool> g_unlock;
	bool IsUnlockedByNpc(cube::GameController* gc, const std::string& npc) {
		std::string who = ModDb::CharName(gc);
		if (who != g_unlockChar) { g_unlockChar = who; g_unlock.clear(); }
		auto it = g_unlock.find(npc);
		if (it != g_unlock.end()) return it->second;
		std::string v; bool on = ModDb::CharRead(gc, "freigeschaltet_" + npc, v) && v == "1";
		g_unlock[npc] = on; return on;
	}
	void UnlockByNpc(cube::GameController* gc, const std::string& npc) {
		if (IsUnlockedByNpc(gc, npc)) return;
		g_unlock[npc] = true;
		ModDb::CharWrite(gc, "freigeschaltet_" + npc, "1");
		std::vector<SkillDef>* set = g_set;
		if (set) for (const SkillDef& d : *set) if (_stricmp(d.unlockBy.c_str(), npc.c_str()) == 0) Chat(gc, d.name + " ist jetzt freigeschaltet.");
	}
	static bool Ready(cube::GameController* gc, const SkillDef& d, const PlayerRaw& p, int* rank) {
		if (d.cls != 0 && d.cls != p.cls) { Chat(gc, d.name + ": nur fuer " + ClassName(d.cls) + "."); return false; }
		if (!d.unlockBy.empty() && !IsUnlockedByNpc(gc, d.unlockBy)) {
			Chat(gc, d.name + ": noch verschlossen -- sprich zuerst mit dem Hochelfenkoenig auf dem Mutterbaum der Elfenstadt.");
			return false;
		}
		*rank = RankOf(d, p.level);
		if (*rank <= 0) {
			if (d.treeSlot >= 0) Chat(gc, d.name + ": noch keine Punkte -- im Skillfenster (X) in der Leiste 'Weitere Faehigkeiten' verteilen.");
			else Chat(gc, d.name + ": ab Level " + std::to_string(d.unlockLevel) + ".");
			return false;
		}
		return true;
	}
	static bool Afford(cube::GameController* gc, const SkillDef& d, const PlayerRaw& p, DWORD now) {
		DWORD& ready = g_readyAt[d.file];
		if ((int)(ready - now) > 0) { Chat(gc, d.name + ": noch " + std::to_string((ready - now + 999) / 1000) + " s."); return false; }
		if (p.mp + 0.0001f < ManaCost(d, p.level)) { Chat(gc, d.name + ": nicht genug Mana."); return false; }
		return true;
	}
	static void Spend(cube::GameController* gc, const SkillDef& d, int rank, const PlayerRaw& p, DWORD now) {
		WriteMana(gc, p.mp - ManaCost(d, p.level));
		g_readyAt[d.file] = now + (DWORD)(CooldownAt(d, rank) * 1000.0f);
	}

	static void InstallDescHook();
	static void HotbarTick(cube::GameController* gc, const PlayerRaw& p, DWORD now);
	void Tick(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		if (!g_snapInit) { InitializeCriticalSection(&g_snapCs); g_snapInit = true; }
		if (!g_set) Reload();
		PlayerRaw p; if (!ReadPlayerRaw(gc, &p)) return;
		DWORD now = GetTickCount();
		{ StoneLock l; EnsureStones(SeedRaw((const uint8_t*)gc + 0x2E4)); }
		// Laufrichtung merken (Geschwindigkeit waagrecht)
		float sp = std::sqrt(p.vx * p.vx + p.vy * p.vy);
		if (sp > 0.5f) { g_dirX = p.vx / sp; g_dirY = p.vy / sp; }
		// Besiegte Gegner (Monster, Feindlichkeit 1, im Umkreis von 48 Bloecken: Leben <= 0)
		static DWORD lastScan = 0;
		if (now - lastScan >= 250) {
			lastScan = now;
			static MobLab::CreatureInfo buf[256];
			int n = MobLab::ListCreaturesNear(gc, p.x >> 16, p.y >> 16, p.z >> 16, 64, 40, buf, 256);
			for (int i = 0; i < n; i++) {
				const MobLab::CreatureInfo& c = buf[i];
				if (c.guid == p.guid || c.hostility != 1) continue;
				auto it = g_watch.find(c.guid);
				if (c.hp <= 0.0f) {
					if (it != g_watch.end()) {
						g_kills.push_front({ it->second.race, it->second.level, it->second.big });
						if (g_kills.size() > 12) g_kills.pop_back();
						Log("besiegt: " + RN(it->second.race) + " Lv " + std::to_string(it->second.level));
						g_watch.erase(it);
					}
				} else if (it == g_watch.end()) g_watch[c.guid] = { c.race, c.level, now, c.hp, c.hp, IsBig(c.sx, c.sz) };
				else { it->second.seen = now; it->second.hpLast = c.hp; }
			}
			// Verschwunden, nachdem er deutlich Leben verloren hatte -> ebenfalls besiegt (Leichen werden oft sofort entfernt)
			for (auto it = g_watch.begin(); it != g_watch.end(); ) {
				DWORD age = now - it->second.seen;
				if (age > 600 && it->second.hpLast < it->second.hp0 * 0.6f) {
					g_kills.push_front({ it->second.race, it->second.level, it->second.big });
					if (g_kills.size() > 12) g_kills.pop_back();
					Log("besiegt (verschwunden): " + RN(it->second.race) + " Lv " + std::to_string(it->second.level));
					it = g_watch.erase(it);
				}
				else if (age > 5000) it = g_watch.erase(it);
				else ++it;
			}
			// Begleiter-Plaetze pruefen: Dauer um -> leeren (das Spiel entlaesst die Kreatur selbst, 0x60EADD);
			// tot, verschwunden oder vom Spiel umgestellt -> Platz frei + Grund ins Log
			for (int i = 0; i < kGhostSlots; i++) {
				if (!g_ghost[i].pet) continue;
				int hos = -1; uint64_t own = 0; float hp = 0;
				bool alive = PetStateRaw(gc, g_ghost[i].pet, &hos, &own, &hp);
				std::string why;
				if (!alive) why = "verschwunden (entladen oder entfernt)";
				else if (hp <= 0.0f) why = "gestorben";
				else if (hos != 5 || (own != g_ghost[i].owner && !(own == 0 && g_detached[i]))) { char b[96]; sprintf_s(b, "vom Spiel entlassen (Feindlichkeit %d, Besitzer %s)", hos, own ? "anderer" : "keiner"); why = b; }
				else if ((int)(now - g_ghost[i].end) >= 0) why = "Dauer um";
				if (why.empty()) continue;
				Log("Platz " + std::to_string(i) + " (" + RN(g_ghost[i].race) + ") frei: " + why);
				{   // Verschwinden: schwarze Wolke an der Stelle des Beschworenen
					int64_t cx, cy, cz;
					if (MobLab::CreaturePos(gc, g_ghost[i].pet, &cx, &cy, &cz)) {
						const float fx = cx / 65536.0f, fy = cy / 65536.0f, fz = cz / 65536.0f;
						Effects::Burst(gc, fx, fy, fz, 0.03f, 0.03f, 0.05f, 50, 0.45f, 3.0f, 3.5f, 2, 1600);
						Effects::AddEmitter(fx, fy, fz - 1.0f, 0.06f, 0.05f, 0.09f, 900, 30, 0, 1.0f);
					}
				}
				GhostRelease(gc, i);
			}
			for (size_t i = 0; i < g_summons.size(); ) {
				bool inSlot = false; for (int k = 0; k < kGhostSlots; k++) if (g_ghost[k].pet == g_summons[i].guid) inSlot = true;
				if (!inSlot) g_summons.erase(g_summons.begin() + i); else i++;
			}
			// Snapshot fuers LAB
			std::string ks;
			for (const Kill& k : g_kills) { if (!ks.empty()) ks += ", "; ks += MobLab::RaceName(k.race); }
			EnterCriticalSection(&g_snapCs); g_snap.inGame = true; g_snap.level = p.level; g_snap.cls = p.cls; g_snap.kills = ks; LeaveCriticalSection(&g_snapCs);
		}
		{ bool op = false; if (ReadTreeRaw(gc, g_treePts, &op)) g_treeOpen = op; else g_treeOpen = false; }
		RegisterTreeIcons(gc);
		InstallTreePatches();
		InstallGhostPetPatch();
		if (g_ghostPatched == 1) { InstallLinkPatch(); RideTick(gc); }   // Beschworene: kein Haustierplatz, nicht mitreiten
		InstallDescHook();                                               // Beschreibung 9/10 im nativen Vorschaufeld
		HotbarTick(gc, p, now);                                          // Teleport/Beschwoeren als Knoepfe der nativen Leiste
		if (g_treePatched == 1) InstallTreeStub(gc);
		{ std::string who = CharNameRaw(gc); if (!who.empty() && (!g_extraLoaded || who != g_extraChar)) LoadExtra(gc, who); }
		{
			static bool was = false; static int logs = 0; static DWORD openAt = 0;
			if (g_treeOpen && !was) openAt = GetTickCount();
			if (g_treeOpen && openAt && GetTickCount() - openAt > 300 && logs < 3) {     // nach dem ersten Aufbau
				char buf[1400]; TreeDiagRaw(gc, buf, sizeof(buf)); logs++; openAt = 0;
				std::cout << "[Skills] Fenster offen (Patches " << g_treePatched << ", Symbole " << (g_treeIconsOk ? "ok" : "fehlen") << "): " << buf << std::endl;
			}
			was = g_treeOpen;
		}
		if (g_clickSlot >= 9) {                                                   // Anzahl-Kasten: Punkt im nativen Platz + Anzahl
			int sl = g_clickSlot, dl = g_clickDelta; g_clickSlot = -1;
			int v = 0, left = 0;
			if (dl > 0) { if (TreeClickRaw(gc, sl, +1, &v, &left)) { g_extraPts[sl]++; SaveExtra(); } else Chat(gc, "Keine freien Fertigkeitspunkte."); }
			else if (g_extraPts[sl] > 0 && TreeClickRaw(gc, sl, -1, &v, &left)) { g_extraPts[sl]--; SaveExtra(); }
		}
		if (g_extraDirty) { g_extraDirty = false; SaveExtra(); }
		if (g_treeLog) { g_treeLog = false; std::cout << "[Skills] Skillfenster erweitert: '+'-Kind im Knoten = " << g_arrowChild << ", SkillWidget " << g_skillW0 << " x " << g_skillH0 << " -> +120." << std::endl; }
		if (g_treeOpen) {
			TreeBufRaw(gc, g_treeBuf, &g_treeTotal);
			static bool lWas = false, rWas = false;
			bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
			if (g_mouseValid && ((l && !lWas) || (r && !rWas)))
				for (const TreeBtn& b : g_treeBtns)
					if (g_mouse.x >= b.r.left && g_mouse.x < b.r.right && g_mouse.y >= b.r.top && g_mouse.y < b.r.bottom) {
						int v = 0, left = 0, dl = (r && !rWas) ? -1 : b.delta;
if (!TreeClickRaw(gc, b.slot, dl, &v, &left) && dl > 0) Chat(gc, "Keine freien Fertigkeitspunkte.");
						break;
					}
			lWas = l; rWas = r;
		}
		std::vector<SkillDef>* set = g_set; if (!set) return;
		const SkillDef* tele = nullptr;
		for (const SkillDef& d : *set) if (d.enabled && d.kind == SK_TELEPORT) { tele = &d; break; }
		FinishPlacement(gc, tele);                                               // Name eingegeben -> Stein ablegen
		if (g_listOpen && (int)(now - g_listUntil) >= 0) g_listOpen = false;
		bool focus = GameHasFocus();
		if (g_listOpen && focus && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) g_listOpen = false;   // Esc schliesst die Liste
		// Steinliste als Fenster im Stil des Spiels (NativeUi); scheitern die Vorlagen, zeichnet DrawStoneList wie bisher
		if (g_listOpen && tele) {
			std::vector<Stone> st = Stones(); std::vector<std::string> rows;
			for (const Stone& x : st) rows.push_back(x.name);
			char kb1[8], kb2[8];
			std::string hint = std::string(KeyName(tele->key2 ? tele->key2 : '6', kb2)) + " = weiter      " + KeyName(tele->key ? tele->key : 'P', kb1) +
			                   " = teleportieren      Esc = schliessen";
			g_listNativeOk = !rows.empty() && NativeUi::ShowList(gc, "Runensteine", rows, (std::min)(g_listSel, (int)rows.size() - 1), hint);
		} else { NativeUi::HideList(gc); g_listNativeOk = false; }
		for (const SkillDef& d : *set) {
			if (!d.enabled) continue;
			std::string msg; int rank = 0;
			if (d.kind == SK_TELEPORT) {
				if (d.key == 'O' || d.key2 == 'O' || d.keyPlace == 'O') InstallOKeyPatch();
				const bool free = !Temples::ListOpen();                                // Tempel-Zielliste nutzt 4/6
				bool place = Pressed(d.keyPlace, focus), k1 = Pressed(d.key, focus) && free, k2 = Pressed(d.key2, focus) && free;
				if (place && Ready(gc, d, p, &rank)) { PlaceOrPick(gc, d, rank, p, msg); }
				if ((k1 || k2) && Ready(gc, d, p, &rank)) {
					int nStones; { StoneLock l; nStones = (int)g_stones.size(); }
					if (nStones == 0) msg = d.name + ": noch kein Runenstein -- mit " + std::string(1, (char)(d.keyPlace ? d.keyPlace : 'U')) + " ablegen.";
					else if (nStones == 1 || (g_listOpen && k1)) {                      // ein Stein: sofort; Liste offen: 4 = los
						int idx = nStones == 1 ? 0 : (std::min)(g_listSel, nStones - 1);
						if (Afford(gc, d, p, now) && DoTeleportTo(gc, d, idx, msg)) { Spend(gc, d, rank, p, now); g_listOpen = false; }
					} else if (!g_listOpen) { g_listOpen = true; g_listSel = (std::min)(g_listSel, nStones - 1); g_listUntil = now + 10000; }
					else { g_listSel = (g_listSel + 1) % nStones; g_listUntil = now + 10000; }   // Liste offen: 6 = naechster
				}
			} else {
				if (!Pressed(d.key, focus) && !Pressed(d.key2, focus)) continue;
				if (!Ready(gc, d, p, &rank) || !Afford(gc, d, p, now)) continue;
				if (d.kind == SK_SUMMON && DoSummon(gc, d, rank, p, msg)) Spend(gc, d, rank, p, now);
				else if (d.kind != SK_SUMMON) msg = "unbekannte Skill-Art";
			}
			if (!msg.empty()) Chat(gc, msg);
			ReadPlayerRaw(gc, &p);
		}
	}

	// ---- Native Skill-Beschreibung (2026-10-29) ---------------------------------------------------------------------
	// Zeigt man im Skillfenster auf einen Platz, setzt das Spiel PreviewWidget+0x168 = Platz und ruft im Vorschaufeld
	// GameController::writeSkillInfo(skill, level, x, y, scale, width) = 0x4A5710 (thiscall, ret 0x18; Aufruf 0x4D5404,
	// Infinite-Cubes GameController.cpp:10735). Fuer 9/10 kennt das Spiel keinen Text -> unser Hook schreibt dort Name,
	// Rang und Beschreibung mit derselben Schrift: Schrift = FontEngine([GC+0x800710]+0x34)->getFont(L"resource1.dat")
	// 0x639800; ScalableFont::renderText 0x65BC70 (thiscall: Text, x, y, Groesse, Kontur, 0, 0, 0, Farbe, Konturfarbe,
	// dritte Farbe, 0, 1), zweimal wie im Original 0x4A6088/0x4A6134: erst Kontur 3 weiss/schwarz, dann Fuellung.
	typedef void (__fastcall* WriteSkill_t)(void* gc, void* edx, int skill, int level, int x, int y, float scale, int width);
	typedef void* (__fastcall* GetFont_t)(void* fe, void* edx, const void* ws);
	typedef void (__fastcall* RenderText_t)(void* font, void* edx, const void* ws, float x, float y, float size, float stroke,
	                                        float a, float b, float c, const float* col, const float* scol, const float* third, int z, int flag);
	static WriteSkill_t o_WriteSkill = nullptr; static volatile LONG g_descHook = 0;
	static void RenderLineRaw(void* font, const wchar_t* t, float x, float y, float size, const float* col) {
		alignas(4) uint8_t ws[32] = {};
		static const float white[4] = { 1, 1, 1, 1 }, black[4] = { 0, 0, 0, 1 }, zero[4] = { 0, 0, 0, 0 };
		__try {
			((WCtor_t)Re(0x40EB60))(ws, nullptr, t);
			((RenderText_t)Re(0x65BC70))(font, nullptr, ws, x, y, size, 3.0f, 0, 0, 0, white, black, zero, 0, 1);
			((RenderText_t)Re(0x65BC70))(font, nullptr, ws, x, y, size, 0.0f, 0, 0, 0, col, zero, zero, 0, 1);
			((WDtor_t)Re(0x593E50))(ws, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void* GameFontRaw(void* gc) {
		alignas(4) uint8_t ws[32] = {}; void* f = nullptr;
		__try {
			uint8_t* eng = *(uint8_t**)((uint8_t*)gc + 0x800710); void* fe = eng ? *(void**)(eng + 0x34) : nullptr;
			if (!fe) return nullptr;
			((WCtor_t)Re(0x40EB60))(ws, nullptr, L"resource1.dat");
			f = ((GetFont_t)Re(0x639800))(fe, nullptr, ws);
			((WDtor_t)Re(0x593E50))(ws, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) { f = nullptr; }
		return f;
	}
	static bool OurSkillText(int slot, std::vector<std::pair<std::wstring, int>>& lines) {     // int: 0 normal, 1 Titel, 2 Hinweis
		std::vector<SkillDef>* set = g_set; if (!set) return false;
		const SkillDef* d = nullptr; for (const SkillDef& x : *set) if (x.enabled && x.treeSlot == slot) d = &x;
		if (!d) return false;
		auto W = [](const std::string& s) { wchar_t w[256]; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, 256); if (n <= 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w, 256); return std::wstring(w); };
		lines.push_back({ W(d->name), 1 });
		char kb[8], b[128]; int pts = g_treePts[slot]; if (slot >= 9 && slot <= 10) pts -= (std::min)(pts, g_extraPts[slot]);
		sprintf_s(b, "Taste %s  -  Rang %d / %d", KeyName(d->key, kb), RankForPoints(*d, pts), d->maxRank); lines.push_back({ W(b), 0 });
		std::string line, word; const std::string src = d->desc + " ";
		for (char ch : src) {
			if (ch == ' ' || ch == '\n') {
				if (!line.empty() && line.size() + 1 + word.size() > 44) { lines.push_back({ W(line), 0 }); line.clear(); }
				if (!word.empty()) { if (!line.empty()) line += ' '; line += word; word.clear(); }
				if (ch == '\n' && !line.empty()) { lines.push_back({ W(line), 0 }); line.clear(); }
			} else word += ch;
		}
		if (!line.empty()) lines.push_back({ W(line), 0 });
		if (!d->unlockBy.empty() && g_gcStub && !IsUnlockedByNpc(g_gcStub, d->unlockBy)) lines.push_back({ L"Noch verschlossen - sprich mit dem Hochelfenkoenig.", 2 });
		return true;
	}
	static void __fastcall Hook_WriteSkill(void* gc, void* edx, int skill, int level, int x, int y, float scale, int width) {
		if (skill == 9 || skill == 10) {
			std::vector<std::pair<std::wstring, int>> lines;
			void* font = OurSkillText(skill, lines) ? GameFontRaw(gc) : nullptr;
			if (font) {
				static const float white[4] = { 1, 1, 1, 1 }, gold[4] = { 1.0f, 0.86f, 0.45f, 1 }, red[4] = { 1.0f, 0.45f, 0.4f, 1 };
				float yy = (float)y;
				for (size_t i = 0; i < lines.size(); i++) {
					const float size = lines[i].second == 1 ? 12.0f : 10.0f;
					RenderLineRaw(font, lines[i].first.c_str(), (float)x, yy, size, lines[i].second == 1 ? gold : lines[i].second == 2 ? red : white);
					yy += lines[i].second == 1 ? 18.0f : 14.0f;
				}
				return;
			}
		}
		o_WriteSkill(gc, edx, skill, level, x, y, scale, width);
	}
	static void InstallDescHook() {
		if (g_descHook) return;
		uint8_t* p = (uint8_t*)(uintptr_t)Re(0x4A5710);
		static const uint8_t pro[6] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
		bool ok = false; __try { ok = p && memcmp(p, pro, 6) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		if (ok && MH_CreateHook(p, (LPVOID)&Hook_WriteSkill, (LPVOID*)&o_WriteSkill) == MH_OK && MH_EnableHook(p) == MH_OK) {
			InterlockedExchange(&g_descHook, 1);
			std::cout << "[Skills] Skillbeschreibung nativ im Vorschaufeld (Hook writeSkillInfo 0x4A5710)." << std::endl;
		} else { InterlockedExchange(&g_descHook, -1); std::cout << "[Skills] writeSkillInfo 0x4A5710 passt nicht -- keine Beschreibung fuer 9/10." << std::endl; }
	}

	// ---- Native Hotbar (2026-10-29) ----------------------------------------------------------------------------------
	// Faehigkeitenleiste des Spiels: Knoten-Vektor GC+0x8007FC (m_abilityButtons, 7 Stueck), Abklingzeit-Abdeckungen
	// GC+0x800808 (m_cooldowns), Symbole map GC+0x800820 (Faehigkeiten) / GC+0x800828 (Skillfenster, unsere 9/10).
	// Je Knopf (Schleife 0x48BDC0..0x48C0FA): Symbol = Texture+0x4C (0x47B5F0) in SmoothMeshShape+0x7AC, Saettigung
	// (0/1) in +0x544 (Attribut-Setzen 0x467F10: Feld +0x4C, Index +0x20), Form = Knoten+0x34 (0x411320) -> 0x488B90,
	// danach updateDrawingTextures 0x64AC00. Abdeckung: Transformation (Knoten+0x38) +0xA0 Verformung (0x42F590):
	// Einheit 0x423E70, scale(1, Rest) 0x434AD0, danach Transformation->vtbl[1](1). Lage: wie 0x4831D0 (48 px Abstand).
	// Wir klonen den Knopf "1" (Index 2) je eigenem Skill und setzen ihn rechts neben den Schnellgegenstand (Q).
	typedef int  (__fastcall* TexId_t)(void* tex, void* edx);
	typedef void* (__cdecl* Smooth_t)(void* shape);
	typedef void (__fastcall* AttrSet_t)(void* attr, void* edx, const void* v);
	typedef void (__fastcall* ShapeUpd_t)(void* shape, void* edx);
	typedef void* (__fastcall* AttrVal_t)(void* attr, void* edx);
	typedef void (__fastcall* MatId_t)(void* m, void* edx);
	typedef void (__fastcall* MatScale_t)(void* m, void* edx, float x, float y);
	typedef void (__fastcall* TRef_t)(void* t, void* edx, int f);
	struct BarBtn { void* node = nullptr; void* cool = nullptr; int slot = -1; int key = -1; bool vis = false; float left = -1; int sat = -1; int tex = -1; };
	static BarBtn g_bar[2]; static bool g_barTried = false;
	static void* BarTemplateRaw(uint8_t* G, int idx, int* count) {
		__try { void** b = *(void***)(G + 0x8007FC); void** e = *(void***)(G + 0x800800); *count = (int)(e - b); return (b && idx < *count) ? b[idx] : nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	static int SkillTexRaw(uint8_t* G, int slot) {
		__try {
			void** v = ((MapIdx_t)Re(0x468910))(G + 0x800828, nullptr, &slot);
			return (v && *v) ? ((TexId_t)Re(0x47B5F0))(*v, nullptr) : -1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	static void BtnLookRaw(void* node, int tex, float sat) {
		__try {
			void* sh = ((Smooth_t)Re(0x488B90))(*(void**)((uint8_t*)node + 0x34)); if (!sh) return;
			((AttrSet_t)Re(0x467F10))((uint8_t*)sh + 0x7AC, nullptr, &tex);
			((AttrSet_t)Re(0x467F10))((uint8_t*)sh + 0x544, nullptr, &sat);
			((ShapeUpd_t)Re(0x64AC00))(sh, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void CoolRaw(void* cool, float left) {
		__try {
			void* t = *(void**)((uint8_t*)cool + 0x38); if (!t) return;
			void* m = ((AttrVal_t)Re(0x42F590))((uint8_t*)t + 0xA0, nullptr);
			((MatId_t)Re(0x423E70))(m, nullptr);
			((MatScale_t)Re(0x434AD0))(m, nullptr, 1.0f, left);
			((TRef_t)(*(void***)t)[1])(t, nullptr, 1);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void HotbarTick(cube::GameController* gc, const PlayerRaw& p, DWORD now) {
		uint8_t* G = (uint8_t*)gc; std::vector<SkillDef>* set = g_set; if (!set) return;
		int n = 0; void* tmpl = BarTemplateRaw(G, 2, &n); void* last = BarTemplateRaw(G, 5, &n);            // Knopf 1 / Knopf 4 (gleiche Vorlage)
		if (!tmpl || !last || n < 7) return;
		// Skills mit Platz im Skillfenster (9/10), in fester Reihenfolge
		const SkillDef* defs[2] = { nullptr, nullptr };
		for (const SkillDef& d : *set) if (d.enabled && d.treeSlot >= 9 && d.treeSlot <= 10) defs[d.treeSlot - 9] = &d;
		if (!g_barTried) {
			g_barTried = true;
			for (int k = 0; k < 2; k++) {
				if (!defs[k]) continue;
				g_bar[k].node = NativeUi::NodeClone(gc, tmpl);
				g_bar[k].cool = g_bar[k].node ? NativeUi::NodeChild(g_bar[k].node, L"cooldown") : nullptr;
				g_bar[k].slot = defs[k]->treeSlot;
			}
			std::cout << "[Skills] Hotbar: " << (g_bar[0].node ? 1 : 0) + (g_bar[1].node ? 1 : 0) << " eigene Knoepfe (Klone von Knopf 1, Vektor GC+0x8007FC)." << std::endl;
		}
		float lx, ly; if (!NativeUi::NodeGetPos(last, &lx, &ly)) return;
		int pos = 0;
		for (int k = 0; k < 2; k++) {
			BarBtn& b = g_bar[k]; if (!b.node) continue;
			const SkillDef* d = defs[k];
			const int rank = d ? RankOf(*d, p.level) : 0;
			const bool show = d && rank > 0;
			if (show != b.vis) { NativeUi::NodeVisible(b.node, show); b.vis = show; }
			if (!show) continue;
			pos++;
			NativeUi::NodeSetPos(b.node, lx + 48.0f * (1 + pos), ly);                // hinter Q (Knopf 4 + 48: Schnellgegenstand)
			if (b.key != d->key) { char kb[8]; KeyName(d->key, kb); wchar_t w[8]; MultiByteToWideChar(CP_ACP, 0, kb, -1, w, 8); NativeUi::NodeText(b.node, L"info", w); b.key = d->key; }
			const int tex = SkillTexRaw(G, b.slot);
			auto it = g_readyAt.find(d->file);
			const float total = CooldownAt(*d, rank) * 1000.0f;
			float left = (it != g_readyAt.end() && (int)(it->second - now) > 0 && total > 0) ? (float)(int)(it->second - now) / total : 0.0f;
			if (left > 1.0f) left = 1.0f;
			const bool locked = !d->unlockBy.empty() && !IsUnlockedByNpc(gc, d->unlockBy);
			const int sat = (!locked && left <= 0.0f && p.mp + 0.0001f >= ManaCost(*d, p.level)) ? 1 : 0;
			if (tex >= 0 && (tex != b.tex || sat != b.sat)) { BtnLookRaw(b.node, tex, sat ? 1.0f : 0.0f); b.tex = tex; b.sat = sat; }
			if (b.cool && left != b.left) {
				if (left > 0.0f) { NativeUi::NodeVisible(b.cool, true); CoolRaw(b.cool, left); } else NativeUi::NodeVisible(b.cool, false);
				b.left = left;
			}
		}
	}

	// ---- Anzeige (2026-10-29): ohne D3DX ----------------------------------------------------------------------------
	// Skill-Leiste, Symbole und Zusatzleiste sind entfallen (Skillfenster X zeigt die Faehigkeiten nativ). Uebrig:
	// Beschreibung beim Zeigen auf Platz 9/10 im Skillfenster und die Ersatz-Steinliste -- beide als Schrift des Spiels
	// (NativeUi::HudText).
	static const char* KeyName(int vk, char* buf) {
		if (vk >= 0x70 && vk <= 0x87) { sprintf_s(buf, 8, "F%d", vk - 0x6F); return buf; }
		if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
		sprintf_s(buf, 8, "%02X", vk); return buf;
	}
	// Runensteinliste nur, wenn das Spielfenster (NativeUi::ShowList) nicht gebaut werden konnte -- mittig rechts
	static void DrawStoneList(cube::GameController* gc, int w, int h) {
		if (!g_listOpen || g_listNativeOk) return;
		std::vector<Stone> st = Stones();
		if (st.empty()) return;
		std::string t = "Runensteine:  6 = weiter,  P = teleportieren\n";
		for (size_t i = 0; i < st.size(); i++) {
			char b[96]; sprintf_s(b, "%s %d. %s\n", (int)i == g_listSel ? ">" : "  ", (int)i + 1, st[i].name.c_str()); t += b;
		}
		NativeUi::HudText(gc, t, (float)(w - 330), (float)(h / 2 - (int)st.size() * 9 - 20), 0);
	}
	// Beschreibung eines Skills im nativen Skillfenster (Maus ueber Platz 9/10)
	static void DrawSkillDesc(cube::GameController* gc) {
		int slot = g_hoverSlot; std::vector<SkillDef>* set = g_set;
		if (slot < 9 || !set) return;
		const SkillDef* d = nullptr; for (const SkillDef& x : *set) if (x.enabled && x.treeSlot == slot) d = &x;
		if (!d || d->desc.empty()) return;
		HWND hw = GetActiveWindow(); POINT m;
		if (!hw || !GetCursorPos(&m) || !ScreenToClient(hw, &m)) return;
		std::string t = d->name + "\n";
		{   // Zeilen umbrechen (~48 Zeichen), die Schrift des Spiels bricht nicht selbst um
			std::string line, word; const std::string src = d->desc + " ";
			for (char ch : src) {
				if (ch == ' ' || ch == '\n') {
					if (!line.empty() && line.size() + 1 + word.size() > 48) { t += line + "\n"; line.clear(); }
					if (!word.empty()) { if (!line.empty()) line += ' '; line += word; word.clear(); }
					if (ch == '\n' && !line.empty()) { t += line + "\n"; line.clear(); }
				} else word += ch;
			}
			if (!line.empty()) t += line + "\n";
		}
		if (!d->unlockBy.empty() && g_gcStub && !IsUnlockedByNpc(g_gcStub, d->unlockBy)) t += "[noch verschlossen]";
		NativeUi::HudText(gc, t, (float)(m.x + 18), (float)(m.y + 20), 0);
	}
	void DrawHud(cube::GameController* gc, int w, int h) {
		if (!gc) return;
		DrawStoneList(gc, w, h);                                        // Beschreibung 9/10: nativ (Hook_WriteSkill)
	}

	std::string Status() {
		std::vector<SkillDef>* s = g_set; Snapshot sn = GetSnapshot();
		char b[256]; sprintf_s(b, "Skills: %d geladen; Spieler Level %d, Klasse %s", s ? (int)s->size() : 0, sn.level, ClassName(sn.cls));
		std::string r = std::string(b) + "\r\nZuletzt besiegt (fuer Beschwoeren): " + (sn.kills.empty() ? "-" : sn.kills);
		r += std::string("\r\nBegleiter-Plaetze (Hoehle 0x60EAC7 ") + (g_ghostPatched == 1 ? "aktiv" : g_ghostPatched < 0 ? "FEHLT" : "noch nicht") + "):";
		DWORD nowS = GetTickCount(); int used = 0;
		for (int i = 0; i < kGhostSlots; i++) {
			if (!g_ghost[i].pet) continue; used++;
			char sb[160]; sprintf_s(sb, "\r\n  Platz %2d: %s Lv %d, noch %d s", i, RN(g_ghost[i].race).c_str(), g_ghost[i].level, (int)((int)(g_ghost[i].end - nowS) / 1000));
			r += sb;
		}
		if (!used) r += " alle frei";
		std::vector<Stone> st = Stones();
		r += "\r\n" + Temples::Status();
		r += "\r\n" + Boat::Status();
		r += "\r\n" + ZoneSplit::Status();
		r += "\r\nRunensteine dieser Welt: " + std::to_string(st.size());
		for (const Stone& s1 : st) r += "\r\n  " + s1.name + " (" + std::to_string(s1.x) + "," + std::to_string(s1.y) + "," + std::to_string(s1.z) + ")";
		return r;
	}
}
