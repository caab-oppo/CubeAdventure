// MobLab.cpp -- siehe MobLab.h und RE_Mobs.md.
//
// Aufbau:
//   1. Rassentabelle
//   2. Native Funktionen (SEH-Kleinfunktionen, nur POD im Rahmen -- C2712)
//   3. Native Ausruestung ueber einen temporaeren cube::Spawn
//   4. Eigene Modelle als echte cube::Sprite in die Registry
//   5. Spawnen (Rasse / Galerie / eigener Mob)
//   6. Mob-Dateien
#include "MobLab.h"
#include "Host.h"
#include "NativeHouseProbe.h"

#include <windows.h>
#include <iostream>
#include <sstream>
#include <map>
#include <atomic>
#include "MinHook.h"
#include <climits>
#include "MountainVillage.h"
#include <fstream>
#include <cstring>
#include <cstdio>

namespace MobLab {

	// =====================================================================
	// 1. Rassen
	// =====================================================================
	// Namen = cwsdk-Enum cube::Creature::Race. Fuer die Alpha geprueft: Fuer jede
	// Rasse mit zuordenbarem Kopfmodell setzt der Aussehensgenerator @0x43F7C0 genau
	// das passende Modell (0 elf-head-m01, 17 skeleton-head, 94 werewolf-head,
	// 101 dragon-head, 148 mermaid-head ...). Im Enum fehlten 95 (santa-head) und 129.
	static const char* kRace[kRaceCount] = {
		"ElfMale","ElfFemale","HumanMale","HumanFemale","GoblinMale","GoblinFemale","Bullterrier","LizardmanMale",
		"LizardmanFemale","DwarfMale","DwarfFemale","OrcMale","OrcFemale","FrogmanMale","FrogmanFemale","UndeadMale",
		"UndeadFemale","Skeleton","OldMan","Collie","ShepherdDog","SkullBull","Alpaca","BrownAlpaca",
		"Egg","Turtle","Terrier","ScottishTerrier","Wolf","Panther","Cat","BrownCat",
		"WhiteCat","Pig","Sheep","Bunny","Porcupine","GreenSlime","PinkSlime","YellowSlime",
		"BlueSlime","Frightener","SandHorror","Wizard","Bandit","Witch","Ogre","Rockling",
		"Gnoll","PolarGnoll","Monkey","Gnobold","Insectoid","Hornet","InsectGuard","Crow",
		"Chicken","Seagull","Parrot","Bat","Fly","Midge","Mosquito","PlainRunner",
		"LeafRunner","SnowRunner","DesertRunner","Peacock","Frog","PlantCreature","RadishCreature","Onionling",
		"DesertOnionling","Devourer","Duckbill","Crocodile","SpikeCreature","Anubis","Horus","Jester",
		"Spectrino","Djinn","Minotaur","NomadMale","NomadFemale","Imp","Spitter","Mole",
		"Biter","Koala","Squirrel","Raccoon","Owl","Penguin","Werewolf","Santa",
		"Zombie","Vampire","Horse","Camel","Cow","Dragon","BarkBeetle","FireBeetle",
		"SnoutBeetle","LemonBeetle","Crab","SeaCrab","Troll","DarkTroll","HellDemon","Golem",
		"EmberGolem","SnowGolem","Yeti","Cyclops","Mammoth","Lich","RuneGiant","Saurian",
		"Bush","SnowBush","SnowBerryBush","CottonPlant","Scrub","CobwebScrub","FireScrub","Ginseng",
		"Cactus","(unbekannt 129)","ThornTree","GoldDeposit","IronDeposit","SilverDeposit","SandstoneDeposit","EmeraldDeposit",
		"SapphireDeposit","RubyDeposit","DiamondDeposit","IceCrystalDeposit","Scarecrow","Aim","Dummy","Vase",
		"Bomb","SapphireFish","LemonFish","Seahorse","Mermaid","Merman","Shark","Bumblebee",
		"LanternFish","MawFish","Piranha","Blowfish",
	};
	const char* RaceName(int r) { return (r >= 0 && r < kRaceCount) ? kRace[r] : "?"; }

	const char* RaceKind(int r) {
		if (r <= 16 || r == 83 || r == 84) return "Volk";
		if (r == 18 || r == 95) return "NPC";
		if (r == 24 || (r >= 140 && r <= 144)) return "Objekt";
		if (r >= 120 && r <= 130) return "Pflanze";
		if (r >= 131 && r <= 139) return "Vorkommen";
		if (r >= 145 && r <= 155 && r != 151) return "Wasser";
		static const int animals[] = { 6, 19, 20, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 50, 55, 56, 57, 58, 59,
			60, 61, 62, 63, 64, 65, 66, 67, 68, 74, 75, 87, 89, 90, 91, 92, 93, 98, 99, 100, 102, 103, 104, 105, 106, 107, 151 };
		for (int a : animals) if (a == r) return "Tier";
		return "Monster";
	}

	// Belegt an den 35 nativen Spawn-Stellen (Spawn+0x28): 6 fuer Vogelscheuche
	// (0x8C), Vase (0x8F), Eisenvorkommen (0x84); 1 fuer Fledermaus (0x3B),
	// Seekrabbe (0x6B) und alle Lagermonster. Pflanzen/Vorkommen/Objekte -> 6.
	int DefaultHostility(int r) {
		const char* k = RaceKind(r);
		if (!strcmp(k, "Pflanze") || !strcmp(k, "Vorkommen") || !strcmp(k, "Objekt")) return 6;
		return 1;
	}

	// =====================================================================
	// 2. Native Funktionen
	// =====================================================================
	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint32_t Re(uint32_t a) { return Host::Re(a); }

	static const uint32_t kFnGameNew       = 0x0068D668;  // operator new (MSVCR110-Thunk), __cdecl(size)
	static const uint32_t kFnSpawnCtor     = 0x005ADB20;  // cube::Spawn::ctor_0, __thiscall(), ret
	static const uint32_t kFnOutfitter     = 0x005C7730;  // Ausruester, ret 8: (Spawn*, 0); ECX ungenutzt
	static const uint32_t kFnCopyEquip     = 0x0044AF00;  // 13 Items kopieren, __thiscall(ziel, quelle), ret 4
	static const uint32_t kFnSpriteCtor    = 0x004E6A20;  // cube::Sprite::ctor_0, __thiscall(dev, 0), ret 8
	static const uint32_t kFnSetData       = 0x004E7650;  // VoxelModel_set_data, __thiscall(x,y,z,rgb,strict), ret 0x14
	static const uint32_t kDatEmptyColor   = 0x0076B340;  // Leerfarbe der Modelle
	static const uint32_t kFnGenAppearance = 0x0043F7C0;  // creature_generateAppearance, __cdecl(&rasse, &aussehen, seed)

	typedef void* (__cdecl* Fn_New)(unsigned);
	typedef void  (__fastcall* Fn_This0)(void* self, void* edx);
	typedef void  (__fastcall* Fn_This1)(void* self, void* edx, void* a);
	typedef void  (__fastcall* Fn_This1i)(void* self, void* edx, int a);
	typedef void  (__fastcall* Fn_This2)(void* self, void* edx, void* a, int b);
	typedef void  (__fastcall* Fn_SetData)(void* self, void* edx, int x, int y, int z, const void* rgb, int strict);
	typedef void  (__cdecl* Fn_GenAppearance)(void* race, void* appearance, void* seed);

	// Friedlich wie das native Huhn (Flags7E 0x32), RE CombatBehavior::vfunc_0 0x42CB20: Ist im Aussehensflag 0x02 UND
	// 0x10 gesetzt, waehlt die KI keinen Angriff, sondern huepft vom Ziel weg (Aktion 0x33) -- auch wenn das Tier selbst
	// angegriffen wird. 0x10 allein heisst nur "unbewaffnet" und fuehrt zum Biss-Angriff 'K' (0x4B). 0x08 = Nahkaempfer.
	// KORREKTUR 2026-10-19 (gemessen: Kuh 0x31 friedlich, Wolf 0x31 aggressiv -- die Flags entscheiden NICHT):
	// Ob ein wildes Tier von sich aus angreift, entscheidet CombatBehavior 0x42D171 ueber creature_isPassive 0x444680
	// (Ghidra-Name "ability_isChanneled"): passiv = (Flags7E & 0x1A00) == 0 UND Rasse in einer festen Liste (Collie, Hunde,
	// Alpakas, Schildkroete, Katzen, Schwein, Schaf, Hase, Hornisse, Kraehe, Huhn, Moewe, Papagei, Pfau, Frosch, Schnabeltier,
	// Maulwurf, Koala, Eule, Pinguin, Pferd, Kuh, Krabben, Fische 0x91-0x93). Passive Tiere greifen nur Spieler mit ihrem
	// Futter in der Hand an (Zaehmen) und wehren sich, wenn man sie angreift. Eigene Rassen (>= 200) stehen nicht in der
	// Liste -> immer aggressiv. Der Mod haengt sich an 0x444680 und merkt "friedlich"/"feindlich" im unbenutzten Flag
	// 0x4000/0x8000 (Kreatur+0x7E bzw. Spawn+0x7A; im Spiel nirgends abgefragt).
	static const uint16_t kModPeace = 0x4000, kModHostile = 0x8000;
	static inline uint16_t PeaceFlags(uint16_t af) { return (uint16_t)((af | kModPeace) & ~kModHostile); }
	static inline uint16_t HostileFlags(uint16_t af) { return (uint16_t)((af | kModHostile) & ~kModPeace); }
	static volatile uint8_t g_peaceRace[512];                                   // eigene Rassen mit friedlich=1 (aus den .mob)
	static bool LookupRaceNameRaw(cube::GameController* gc, int race, char* out, size_t cap);
	static void ApplyBodyRaw(int race, uint8_t* app);
	std::string RaceNameInGame(cube::GameController* gc, int race);

	// Spawn wie das Spiel vorbereiten (0x5D7990..0x5D79C9): Rasse/Feindlichkeit/
	// Level setzen, AUSSEHEN erzeugen (setzt u.a. die Flags in Aussehen+6 =
	// Spawn+0x7A, z.B. Drache |0x33 -> 0x10 = unbewaffnet), DANN der Ausruester.
	// Frueher fehlte der Aussehensschritt und die Flags wurden auf 0 gesetzt --
	// darum bekam ein Drache ein Schwert ("Dragon ... Klasse 0x1 Krieger").
	static uint8_t* PrepareSpawnRaw(int race, int hostility, int wantClass, int level, int* outClass) {
		uint8_t* sp = (uint8_t*)((Fn_New)Re(kFnGameNew))(0x10F0);
		if (!sp) return nullptr;
		memset(sp, 0, 0x10F0);
		((Fn_This0)Re(kFnSpawnCtor))(sp, nullptr);
		*(int*)(sp + 0x28) = hostility;
		*(int*)(sp + 0x2C) = race;
		*(int*)(sp + 0x34) = level;
		((Fn_GenAppearance)Re(kFnGenAppearance))(sp + 0x2C, sp + 0x74, nullptr);
		sp[0x30] = (uint8_t)(wantClass >= 0 ? wantClass : 0);
		int tries = 0;
		do {
			((Fn_This2)Re(kFnOutfitter))(sp, nullptr, sp, 0);
			tries++;
		} while (wantClass >= 1 && wantClass <= 4 && sp[0x30] != (uint8_t)wantClass && tries < 32);
		if (outClass) *outClass = sp[0x30];
		return sp;
	}
	static void FreeSpawnRaw(uint8_t* sp) {
		void** vt = *(void***)sp;
		((Fn_This1i)vt[0])(sp, nullptr, 1);
	}
	// Dungeons: fertiger nativer Spawn-Datensatz (wie 0x5D7990) bzw. nur Konstruktor (Vase 0x5D257E)
	uint8_t* NewSpawnNative(int race, int hostility, int level) {
		__try { return PrepareSpawnRaw(race, hostility, -1, level, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	uint8_t* NewSpawnBare() {
		__try {
			uint8_t* sp = (uint8_t*)((Fn_New)Re(kFnGameNew))(0x10F0);
			if (!sp) return nullptr;
			memset(sp, 0, 0x10F0);
			((Fn_This0)Re(kFnSpawnCtor))(sp, nullptr);
			return sp;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	// ---- Ausruestung --------------------------------------------------------
	// Baut einen temporaeren cube::Spawn wie das Spiel (game_misc 0x5D79B4..0x5D79C9),
	// laesst den Ausruester laufen und kopiert das Ergebnis wie die native Umwandlung
	// Spawn->Kreatur (0x61002B..0x61017F). Nur POD im Rahmen.
	static bool NativeOutfit(uint8_t* cre, int race, int hostility, int wantClass, int level, int* outClass) {
		__try {
			// Fuer gewoehnliche Rassen wuerfelt der Ausruester die Klasse selbst
			// (rand()%4 + 1) -- wer eine bestimmte Kampfklasse will, bekommt so
			// viele Wuerfe, bis sie faellt (hoechstens 32). Sonderrassen und
			// unbewaffnete (Flag 0x18) bleiben wie im Spiel.
			int cls = 0;
			uint8_t* sp = PrepareSpawnRaw(race, hostility, wantClass, level, &cls);
			if (!sp) return false;
			((Fn_This1)Re(kFnCopyEquip))(cre + 0x300, nullptr, sp + 0x120);     // Ausruestung
			cre[0x140] = sp[0x30];                                              // Klasse / Beruf
			cre[0x141] = sp[0x31];                                              // Spezialisierung
			memcpy(cre + 0x178, sp + 0xF58, 0x14);                              // Wertefaktoren
			*(float*)(cre + 0x1D2C) = *(float*)(sp + 0x08);                     // Aggro-Radius
			if (outClass) *outClass = cls;
			FreeSpawnRaw(sp);                     // virtueller Destruktor, ret 4, Flag 1 = delete
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool WriteCreature(uint8_t* cre, int off, const void* src, int n) {
		__try { memcpy(cre + off, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool ReadCreature(const uint8_t* cre, int off, void* dst, int n) {
		__try { memcpy(dst, cre + off, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool ReadPlayer(cube::GameController* gc, int64_t* x, int64_t* y, int64_t* z, int* level) {
		__try {
			if (!gc || !gc->local_player) return false;
			uint8_t* p = (uint8_t*)gc->local_player;
			*x = *(int64_t*)(p + 0x10); *y = *(int64_t*)(p + 0x18); *z = *(int64_t*)(p + 0x20);
			*level = *(int*)(p + 0x190);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// =====================================================================
	// 4. Eigene Modelle als cube::Sprite in die Registry
	// =====================================================================
	// Registry = SpriteManager: Vektor<Sprite*> bei +4/+8/+0xC, Device bei +0x10.
	// Freie Plaetze sind Nullzeiger (der Vektor wird beim Start auf 0xA09 Plaetze
	// gebracht, nicht alle sind belegt). Gibt es keinen, wird der Vektor um 64
	// Plaetze vergroessert. Der alte Puffer wird NICHT freigegeben: Liest ein
	// anderer Thread gerade daraus, bleibt sein Zeiger gueltig.
	static int FindOrMakeFreeSlot(uint32_t reg, const std::vector<int>* taken) {
		uint8_t** vb = (uint8_t**)(reg + 4);
		uint32_t* begin = (uint32_t*)vb[0];
		uint32_t* end = (uint32_t*)vb[1];
		uint32_t* cap = (uint32_t*)vb[2];
		int n = (int)(end - begin);
		for (int i = n - 1; i >= 0x10; i--) {
			if (begin[i] != 0) continue;
			bool used = false;
			for (int t : *taken) if (t == i) { used = true; break; }
			if (!used) return i;
		}
		// vergroessern
		int ncap = n + 64;
		uint32_t* nb = (uint32_t*)((Fn_New)Re(kFnGameNew))((unsigned)(ncap * 4));
		if (!nb) return -1;
		memcpy(nb, begin, n * 4);
		memset(nb + n, 0, 64 * 4);
		vb[0] = (uint8_t*)nb; vb[1] = (uint8_t*)(nb + ncap); vb[2] = (uint8_t*)(nb + ncap);
		(void)cap;
		return n + 63;
	}

	static int InstallSpriteRaw(uint32_t reg, int slot, int sx, int sy, int sz, const uint8_t* rgb, void* existing) {
		__try {
			void* dev = *(void**)(reg + 0x10);
			if (!dev) return -1;
			uint8_t* sp = (uint8_t*)existing;
			if (!sp) {
				sp = (uint8_t*)((Fn_New)Re(kFnGameNew))(0x60);
				if (!sp) return -1;
				memset(sp, 0, 0x60);
				((Fn_This2)Re(kFnSpriteCtor))(sp, nullptr, dev, 0);
			}
			// strict = 1: nur DAT_0076B340 ist durchsichtig; Mesh wird gebaut (+0x56 == 0).
			((Fn_SetData)Re(kFnSetData))(sp, nullptr, sx, sy, sz, rgb, 1);
			uint32_t* begin = *(uint32_t**)(reg + 4);
			begin[slot] = (uint32_t)sp;
			return slot;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	static void* SlotPtrSEH(uint32_t reg, int slot) {
		__try { return (void*)(*(uint32_t**)(reg + 4))[slot]; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	static int FreeSlotSEH(uint32_t reg, const std::vector<int>* taken) {
		__try { return FindOrMakeFreeSlot(reg, taken); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	static bool ReadEmptyColor(uint8_t* c) {
		__try { const uint8_t* p = (const uint8_t*)Re(kDatEmptyColor); c[0] = p[0]; c[1] = p[1]; c[2] = p[2]; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { c[0] = c[1] = c[2] = 0; return false; }
	}

	namespace {
		struct Injected { std::string path; FILETIME mtime; int index; void* sprite; };
		std::vector<Injected> g_inj;
		uint64_t g_lastGuid = 0;            // zuletzt gespawnte Kreatur (Suche ueber die Entity-Map)
		CRITICAL_SECTION g_tplCs; bool g_tplCsInit = false;
		NativeTemplate g_tpl; bool g_tplReady = false;
	}

	static FILETIME MTime(const std::string& p) {
		FILETIME t = { 0, 0 };
		WIN32_FILE_ATTRIBUTE_DATA d;
		if (GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &d)) t = d.ftLastWriteTime;
		return t;
	}

	// Teil-Datei finden: wie angegeben (relativ zu Cube.exe oder absolut); sonst unter Strukturen\Mobs und
	// Strukturen\Mobs\Teile mit dem Pfad ab "Mobs\"; sonst irgendwo unter Strukturen\Mobs mit demselben
	// Dateinamen. (Die mitgelieferten .mob zeigten auf Strukturen\Mobs\Elfen, die Teile lagen aber unter
	// ...\Teile\Elfen -> "Datei nicht lesbar", der Mob blieb leer.)
	static bool FileExists(const std::string& p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
	static bool FindByName(const std::string& dir, const std::string& name, std::string& out, int depth) {
		if (depth > 4) return false;
		std::string cand = dir + "\\" + name;
		if (FileExists(cand)) { out = cand; return true; }
		WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return false;
		bool found = false;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
			if (FindByName(dir + "\\" + fd.cFileName, name, out, depth + 1)) { found = true; break; }
		} while (FindNextFileA(h, &fd));
		FindClose(h);
		return found;
	}
	static std::string ResolvePartPath(const std::string& path);
	static bool IsAbsolute(const std::string& p) { return p.size() > 1 && (p[1] == ':' || (p[0] == '\\' && p[1] == '\\')); }
	static std::string DirOf(const std::string& f) { size_t s = f.find_last_of("\\/"); return s == std::string::npos ? std::string(".") : f.substr(0, s); }
	// Dynamisch: zuerst relativ zum Ordner der .mob-Datei (dort liegen ihre Modell-Ordner), dann wie angegeben,
	// dann nach Dateinamen unter dem .mob-Ordner, zuletzt unter Strukturen\Mobs.
	static std::string ResolvePartPath(const std::string& pathIn, const std::string& baseDir) {
		std::string path = pathIn; for (char& ch : path) if (ch == '/') ch = '\\';
		if (path.empty()) return path;
		if (!IsAbsolute(path) && !baseDir.empty()) {
			if (FileExists(baseDir + "\\" + path)) return baseDir + "\\" + path;
			std::string low = path; for (char& ch : low) ch = (char)tolower((unsigned char)ch);
			size_t k = low.rfind("mobs\\");                                  // alte Angabe "Strukturen\Mobs\..." -> ab dort relativ
			if (k != std::string::npos && FileExists(baseDir + "\\" + path.substr(k + 5))) return baseDir + "\\" + path.substr(k + 5);
		}
		if (FileExists(path)) return path;
		size_t s0 = path.find_last_of('\\');
		std::string name0 = s0 == std::string::npos ? path : path.substr(s0 + 1), out0;
		if (!baseDir.empty() && FindByName(baseDir, name0, out0, 0)) return out0;
		return ResolvePartPath(path);
	}
	static std::string ResolvePartPath(const std::string& path) {
		if (path.empty() || FileExists(path)) return path;
		std::string p = path; for (char& ch : p) if (ch == '/') ch = '\\';
		std::string low = p; for (char& ch : low) ch = (char)tolower((unsigned char)ch);
		size_t k = low.rfind("mobs\\");
		if (k != std::string::npos) {
			std::string rest = p.substr(k + 5);
			for (const std::string& base : { MobsDir() + "\\", MobsDir() + "\\Teile\\" }) if (FileExists(base + rest)) return base + rest;
		}
		size_t s = p.find_last_of('\\');
		std::string name = s == std::string::npos ? p : p.substr(s + 1), out;
		if (FindByName(MobsDir(), name, out, 0)) return out;
		return path;
	}

	// .cub -> Registry-Index. Gleiche Datei = gleicher Index; geaenderte Datei
	// (neu gespeichert im LAB-Editor) wird in denselben Sprite neu geladen.
	static int InjectFile(cube::GameController* gc, const std::string& pathIn, const std::string& baseDir, std::string& err) {
		const std::string path = ResolvePartPath(pathIn, baseDir);
		if (path != pathIn) std::cout << "[Mob] Teil " << pathIn << " gefunden als " << path << std::endl;
		uint32_t reg = NativeHouseProbe::RegistryBase(gc);
		if (!reg) { err = "Registry nicht gefunden"; return -1; }
		FILETIME mt = MTime(path);
		Injected* hit = nullptr;
		for (Injected& j : g_inj) if (_stricmp(j.path.c_str(), path.c_str()) == 0) { hit = &j; break; }
		if (hit && CompareFileTime(&hit->mtime, &mt) == 0) return hit->index;

		Lab::Structure s;
		if (!Lab::LoadCub(path, s, err)) return -1;
		uint8_t e[3]; ReadEmptyColor(e);
		if (e[0] | e[1] | e[2])                               // Leerfarbe des Spiels ist nicht schwarz
			for (size_t i = 0; i + 2 < s.rgb.size(); i += 3)
				if ((s.rgb[i] | s.rgb[i + 1] | s.rgb[i + 2]) == 0) { s.rgb[i] = e[0]; s.rgb[i + 1] = e[1]; s.rgb[i + 2] = e[2]; }

		int slot = hit ? hit->index : -1;
		if (slot < 0) {
			std::vector<int> taken; for (Injected& j : g_inj) taken.push_back(j.index);
			slot = FreeSlotSEH(reg, &taken);
			if (slot < 0) { err = "kein freier Registry-Platz"; return -1; }
		}
		int r = InstallSpriteRaw(reg, slot, s.sx, s.sy, s.sz, s.rgb.data(), hit ? hit->sprite : nullptr);
		if (r < 0) { err = "Modell konnte nicht angelegt werden (SEH)"; return -1; }
		void* sp = SlotPtrSEH(reg, slot);
		if (hit) { hit->mtime = mt; }
		else g_inj.push_back({ path, mt, slot, sp });
		std::cout << "[Mob] Modell " << path << " (" << s.sx << "x" << s.sy << "x" << s.sz << ") -> Registry 0x"
		          << std::hex << slot << std::dec << (hit ? " (neu geladen)" : "") << std::endl;
		return slot;
	}

	// =====================================================================
	// 4b. Kreatur ueber GUID finden, native Vorlage
	// =====================================================================
	// Entity-Map = std::map<uint64 GUID, Creature*> bei World+4 (dort, wohin der
	// Map-Insert @0x468AD0 schreibt). cwsdk fuehrt das Feld als Zeiger -- sein Wert
	// ist der Kopfknoten (_Myhead). Knoten: +0 links, +4 Eltern, +8 rechts,
	// +0xD isnil, +0x10 Schluessel (GUID), +0x18 Creature*. Kopf->Eltern = Wurzel.
	// Frueher hielt DescribeLast einen Rohzeiger -- die Kreatur war beim Auslesen
	// schon ersetzt/verschwunden und es kam Muell heraus.
	static uint8_t* FindCreatureRaw(cube::GameController* gc, uint64_t guid) {
		__try {
			uint8_t* head = *(uint8_t**)((uint8_t*)&gc->world + 4);
			if (!head) return nullptr;
			uint8_t* n = *(uint8_t**)(head + 4);
			for (int guard = 0; n && !n[0xD] && guard < 64; guard++) {
				// Schluessel der EntitesMap sind VORZEICHENBEHAFTET (Vergleich 0x477D90: jg/jl auf dem hohen Wort). Mod-GUIDs
				// 0xCAFECAFE........ sind negativ -- mit unsigned-Vergleich wurden sie nie gefunden (Beschwoerung, 2026-10-27).
				int64_t k = *(int64_t*)(n + 0x10);
				if (k == (int64_t)guid) return *(uint8_t**)(n + 0x18);
				n = ((int64_t)guid < k) ? *(uint8_t**)(n + 0) : *(uint8_t**)(n + 8);
			}
			return nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	static bool TemplateRaw(int race, int wantClass, int level, uint8_t* app, uint8_t* equip, int* cls) {
		__try {
			uint8_t* sp = PrepareSpawnRaw(race, DefaultHostility(race), wantClass, level, cls);
			if (!sp) return false;
			memcpy(app, sp + 0x74, kAppearanceSize);
			memcpy(equip, sp + 0x120, kEquipSlots * kItemSize);
			FreeSpawnRaw(sp);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	void RequestTemplate(cube::GameController* gc, int race, int wantClass) {
		if (!g_tplCsInit) { InitializeCriticalSection(&g_tplCs); g_tplCsInit = true; }
		int64_t x, y, z; int plevel = 1; ReadPlayer(gc, &x, &y, &z, &plevel);
		NativeTemplate t; t.race = race;
		t.ok = TemplateRaw(race, wantClass, plevel > 0 ? plevel : 1, t.appearance, t.equipment, &t.cls);
		std::cout << "[Mob] Native Vorlage " << RaceName(race) << (t.ok ? " gelesen" : " FEHLGESCHLAGEN") << "." << std::endl;
		EnterCriticalSection(&g_tplCs); g_tpl = t; g_tplReady = true; LeaveCriticalSection(&g_tplCs);
	}
	bool TakeTemplate(NativeTemplate& out) {
		if (!g_tplCsInit) return false;
		bool r = false;
		EnterCriticalSection(&g_tplCs);
		if (g_tplReady) { out = g_tpl; g_tplReady = false; r = true; }
		LeaveCriticalSection(&g_tplCs);
		return r;
	}

	static const char* kSlot[kEquipSlots] = { "Platz 0 (?)", "Amulett", "Brust", "Fuesse", "Haende", "Schultern",
		"Waffe links", "Waffe rechts", "Ring links", "Ring rechts", "Licht", "Spezial", "Haustier" };
	const char* EquipSlotName(int i) { return (i >= 0 && i < kEquipSlots) ? kSlot[i] : "?"; }

	// Aussehens-Offsets RELATIV zum Aussehen (= Kreatur-Offset - 0x78)
	static const int kAIdOff[8]    = { 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22 };
	static const int kAScaleOff[8] = { 0x24, 0x24, 0x2C, 0x30, 0x28, 0x3C, 0x40, 0x44 };
	static const int kAPosOff[8]   = { 0x70, 0x70, 0x7C, 0x88, 0x64, -1, -1, 0xA0 };

	void ApplyTemplate(const NativeTemplate& t, CustomMob& m) {
		m.baseRace = t.race;
		m.useAppearance = true;
		memcpy(m.appearance, t.appearance, kAppearanceSize);
		for (int i = 0; i < P_COUNT; i++) {
			Part& p = m.parts[i];
			int16_t id; memcpy(&id, t.appearance + kAIdOff[i], 2);
			p.source = id < 0 ? SRC_NONE : SRC_NATIVE;
			p.nativeIndex = id; p.file.clear();
			memcpy(&p.scale, t.appearance + kAScaleOff[i], 4);
			p.setPos = kAPosOff[i] >= 0;
			if (p.setPos) memcpy(p.pos, t.appearance + kAPosOff[i], 12);
		}
		float f = 1.0f; m.sizeFactor = f;
		for (int i = 0; i < kEquipSlots; i++) {
			EquipItem& e = m.equip[i];
			const uint8_t* it = t.equipment + i * kItemSize;
			e.set = true; e.hasRaw = true; memcpy(e.raw, it, kItemSize);
			e.category = it[0]; e.id = it[1]; e.rarity = it[0x0C]; e.material = it[0x0D];
			uint16_t lv; memcpy(&lv, it + 0x10, 2); e.level = lv;
		}
		m.equipment = false;   // die geladene Ausruestung ist jetzt explizit gesetzt
	}

	// =====================================================================
	// 5. Spawnen
	// =====================================================================
	static cube::Creature* SpawnOne(cube::GameController* gc, int race, int hostility, int level,
	                                int64_t x, int64_t y, int64_t z, const SpawnOpts& o, int* outClass, uint64_t owner = 0) {
		Vector3<int64_t> pos(x, y, z);
		// owner != 0: Begleiter. cube_world_init_companion 0x5EE7A0 baut dann Sequential{CombatBehavior(20),
		// CompanionBehavior(owner)} -- kaempft und folgt dem Besitzer (wie ein Haustier).
		cube::Creature* c = cube::util::SpawnCreature(pos, (cube::Creature::Race)race, (uint32_t)hostility, (uint32_t)level, owner);
		if (!c) return nullptr;
		if (o.equipment) NativeOutfit((uint8_t*)c, race, hostility, o.charClass, level, outClass);
		else if (o.charClass >= 0) { uint8_t cc = (uint8_t)o.charClass; WriteCreature((uint8_t*)c, 0x140, &cc, 1); }
		uint8_t h = (uint8_t)hostility; WriteCreature((uint8_t*)c, 0x60, &h, 1);   // nach dem Ausruester sicher setzen
		// Friedlich wie die nativen Tiere (gemessen am Huhn, LAB 'Tiere genau auslesen'): Feindlichkeit 1, Aussehensflags
		// 0x0032 -- Bit 0x10 gesetzt ("unbewaffnet": Angriffsaktion 'K' statt Waffe, GameController.cpp:15317), Bit 0x08 aus.
		// Feindliche Laeufer haben 0x0028 (0x08 statt 0x10). Rasse, Kampf-KI und Baum sind sonst identisch.
		if (o.peaceful) { uint16_t af = 0; ReadCreature((uint8_t*)c, 0x7E, &af, 2); af = PeaceFlags(af); WriteCreature((uint8_t*)c, 0x7E, &af, 2); }
		// Groesse: Aussehen +0x08/+0x0C/+0x10 = Kreatur +0x80/+0x84/+0x88. Der
		// Aussehensgenerator setzt sie je Rasse (z.B. 0.96/0.96/2.16 fuer Menschen,
		// @0x43F7C0: item+8/+0xC/+0x10). Alle drei gleich skalieren, damit Bild,
		// Trefferflaeche und Koerperhoehe zusammenpassen.
		if (o.sizeFactor > 0.0f && (o.sizeFactor < 0.999f || o.sizeFactor > 1.001f)) {
			float sz[3];
			if (ReadCreature((uint8_t*)c, 0x80, sz, 12)) {
				for (int k = 0; k < 3; k++) sz[k] *= o.sizeFactor;
				WriteCreature((uint8_t*)c, 0x80, sz, 12);
			}
		}
		if (hostility != 6 && o.wander && !owner) cube::util::AttachRandomWalk(c, x, y);   // Begleiter behalten die Begleiter-KI
		cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);
		uint64_t guid = 0; ReadCreature((uint8_t*)c, 0x08, &guid, 8); g_lastGuid = guid;
		return c;
	}

	static const char* ClassName(int c) {
		switch (c) {
			case 1: return "Krieger"; case 2: return "Waldlaeufer"; case 3: return "Magier"; case 4: return "Schurke";
			case 0x80: return "Haendler"; case 0x84: return "Gastwirt"; case 0x88: return "Steg-NPC";
			default: return c >= 0x80 ? "Beruf" : (c == 0 ? "keine" : "?");
		}
	}

	int Spawn(cube::GameController* gc, const SpawnOpts& o, std::string& report) {
		int64_t px, py, pz; int plevel = 1;
		if (!ReadPlayer(gc, &px, &py, &pz, &plevel)) { report = "kein Spieler"; return 0; }
		int race = (o.race >= 0 && o.race < kRaceCount) ? o.race : 0;
		int hos = o.hostility >= 0 ? o.hostility : DefaultHostility(race);
		int lvl = o.level > 0 ? o.level : (plevel > 0 ? plevel : 1);
		const int64_t U = 65536;
		int n = o.count < 1 ? 1 : (o.count > 30 ? 30 : o.count), ok = 0, cls = -1;
		for (int i = 0; i < n; i++) {
			int64_t yoff = (int64_t)(i - n / 2) * 2 * U;
			if (SpawnOne(gc, race, hos, lvl, px + (int64_t)o.distance * U, py + yoff, pz + U, o, &cls)) ok++;
		}
		std::ostringstream r;
		r << ok << "x " << RaceName(race) << " (" << race << ", " << RaceKind(race) << ")  Groesse x" << o.sizeFactor << ", Feindlichkeit " << hos
		  << ", Level " << lvl << (o.equipment ? ", native Ausruestung" : ", ohne Ausruestung");
		if (o.equipment && cls >= 0) { char b[32]; sprintf_s(b, ", Klasse 0x%X ", cls); r << b << ClassName(cls); }
		report = r.str();
		std::cout << "[Mob] " << report << std::endl;
		return ok;
	}

	int SpawnGallery(cube::GameController* gc, int first, int count, const SpawnOpts& base, std::string& report) {
		int64_t px, py, pz; int plevel = 1;
		if (!ReadPlayer(gc, &px, &py, &pz, &plevel)) { report = "kein Spieler"; return 0; }
		const int64_t U = 65536;
		int ok = 0;
		std::ostringstream r;
		for (int i = 0; i < count && first + i < kRaceCount; i++) {
			int race = first + i;
			SpawnOpts o = base; o.race = race; o.wander = false;      // Galerie: stehen bleiben
			int hos = DefaultHostility(race) == 6 ? 6 : (base.hostility >= 0 ? base.hostility : 3);  // 3: greift nicht an
			int lvl = base.level > 0 ? base.level : (plevel > 0 ? plevel : 1);
			int cls = -1;
			if (SpawnOne(gc, race, hos, lvl, px + (int64_t)base.distance * U, py + (int64_t)(i - count / 2) * 4 * U, pz + U, o, &cls)) {
				ok++;
				std::cout << "[Mob-Galerie] " << (i + 1) << ". " << race << " " << RaceName(race) << "  (" << RaceKind(race) << ")" << std::endl;
			}
		}
		r << ok << " Rassen ab " << first << " (" << RaceName(first) << ") in einer Reihe, " << base.distance
		  << " Bloecke +X, 4 Bloecke Abstand. Galerie-Mobs sind friedlich (3) und bleiben stehen.";
		report = r.str();
		std::cout << "[Mob] " << report << std::endl;
		return ok;
	}

	// ---- eigener Mob ---------------------------------------------------------
	static const char* kPartName[P_COUNT] = { "Kopf", "Haare", "Haende", "Fuesse", "Koerper", "Schwanz", "Schulter", "Fluegel" };
	const char* PartName(int s) { return (s >= 0 && s < P_COUNT) ? kPartName[s] : "?"; }
	// Kreatur-Offsets (Aussehen ab +0x78, gegen @0x43F7C0 und Creature_drawModel geprueft)
	static const int kIdOff[P_COUNT]    = { 0x8C, 0x8E, 0x90, 0x92, 0x94, 0x96, 0x98, 0x9A };
	static const int kScaleOff[P_COUNT] = { 0x9C, 0x9C, 0xA4, 0xA8, 0xA0, 0xB4, 0xB8, 0xBC };   // Haare teilen die Kopfskala
	// Positionen ab +0xDC: Koerper, Kopf, Haende, Fuesse, Schwanz (+0x10C), Fluegel (+0x118) -- Reihenfolge wie Infinite-Cubes
	// Creature.h Appearance (chestPosition..wingsPosition), passt zu den belegten Nachbarn +0x100 (Fuesse) und +0x118 (Fluegel).
	// 2026-10-27: Schwanz freigeschaltet (vorher -1); Schulter hat keine eigene Position.
	static const int kPosOff[P_COUNT]   = { 0xE8, 0xE8, 0xF4, 0x100, 0xDC, 0x10C, -1, 0x118 };
	bool PartHasPosition(int s) { return s >= 0 && s < P_COUNT && kPosOff[s] >= 0; }

	// Groesse eines Registry-Modells (Sprite +0x44/+0x48/+0x4C), auch fuer eingespielte eigene Teile
	static bool ModelDims(cube::GameController* gc, int index, int* sx, int* sy, int* sz) {
		if (index < 0) return false;
		void* m = NativeHouseProbe::RegistryModel(gc, index);
		if (!m) return false;
		__try { *sx = *(int*)((uint8_t*)m + 0x44); *sy = *(int*)((uint8_t*)m + 0x48); *sz = *(int*)((uint8_t*)m + 0x4C); return *sz > 0 && *sz < 512; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static int ScaleIdx(int part) { return (kScaleOff[part] - 0x9C) / 4; }        // in float[9] ab +0x9C
	static int PosIdx(int part) { return kPosOff[part] < 0 ? -1 : (kPosOff[part] - 0xDC) / 12; }   // in float[6][3] ab +0xDC

	CustomMob EmptyMob() {
		CustomMob m;
		for (int i = 0; i < P_COUNT; i++) m.parts[i].source = SRC_NONE;
		return m;
	}

	static bool ReadPos(uint8_t* c, int64_t* x, int64_t* y, int64_t* z);
	// ---- Fliegen + Angriffs-KI (2026-10-27) ------------------------------------------------------------------
	// Vorher hielt FlightTick jeden Flieger staendig 5..14 Bloecke ueber dem Boden -> im Kampf schwebten sie ueber dem
	// Spieler und verharrten. Jetzt eigene Kampf-KI fuer eigene feindliche Tiere (Rasse >= 200, Feindlichkeit 1, ohne
	// Friedlich-Marke 0x4000):
	//   Flieger:  STEIGEN (seitlich ueber den Spieler, 9..12 Bloecke) -> STURZFLUG (schnell auf die Brust des Spielers,
	//             Angriff ForwardRoll) -> ABDREHEN (weg und hoch) -> wieder STEIGEN; ab und zu LANDEN und am Boden mit
	//             Sprungangriffen kaempfen (Flugflag 0x2 aus, Schwerkraft), danach wieder auffliegen.
	//   Kleine Landtiere (Breite < 0.85 Bloecke): Sprungangriffe aus 1.5..12 Bloecken.
	// Belege (Infinite-Cubes alpha, nur Logik; Offsets aus dem eigenen Kreatur-Layout):
	//   EntityData: position +0x10, rotation +0x28, velocity +0x34, ..., hostilityType +0x60, race +0x64,
	//   currentAbility +0x68 (uchar), timeSinceAbility +0x6C (int ms), appearance +0x78 (Flags +0x7E, size +0x80).
	//   CombatBehavior.cpp: Flieger mit Biss (Flags 2|4) greifen mit currentAbility = AbilityForwardRoll (51),
	//   timeSinceAbility = 0 und velocity = Richtung * 10 an; Golem-Sprung: velocity = (Ziel - Position) * 2, velocity.z = 15.
	//   WorldUpdate.cpp:1743: ForwardRoll/Bite (75) sind Nahkampftreffer (Treffer nach der Ausholzeit).
	enum { FS_PATROL, FS_CLIMB, FS_DIVE, FS_RECOVER, FS_GROUND };
	struct Flyer { uint64_t guid; int64_t hx, hy; float phase; int ground; DWORD nextGround; int state; DWORD stateT; DWORD nextLeap; float side; };
	static std::vector<Flyer> g_flyers;
	struct Leaper { uint64_t guid; int ground; DWORD nextGround, nextLeap; };
	static std::vector<Leaper> g_leapers;
	static void RegisterFlyer(uint64_t guid, int64_t x, int64_t y) {
		if (!guid) return;
		for (const Flyer& f : g_flyers) if (f.guid == guid) return;
		if (g_flyers.size() >= 64) g_flyers.erase(g_flyers.begin());
		Flyer f{}; f.guid = guid; f.hx = x; f.hy = y; f.phase = (float)(guid % 628) / 100.0f; f.ground = INT_MIN; f.state = FS_PATROL;
		f.side = (guid & 1) ? 1.0f : -1.0f; g_flyers.push_back(f);
	}
	static void RegisterLeaper(uint64_t guid) {
		if (!guid) return;
		for (const Leaper& l : g_leapers) if (l.guid == guid) return;
		if (g_leapers.size() >= 128) g_leapers.erase(g_leapers.begin());
		g_leapers.push_back({ guid, INT_MIN, 0, 0 });
	}
	static bool FlyStepRaw(uint8_t* c, float targetZ, float wantVx, float wantVy, bool steer) {
		__try {
			float z = (float)(*(int64_t*)(c + 0x20)) / 65536.0f;
			float vz = (targetZ - z) * 1.6f;
			if (vz > 6.0f) vz = 6.0f; if (vz < -4.0f) vz = -4.0f;
			*(float*)(c + 0x3C) = vz;
			if (steer) {                                                         // sanft in Richtung Heimat zurueck
				*(float*)(c + 0x34) = *(float*)(c + 0x34) * 0.9f + wantVx * 0.1f;
				*(float*)(c + 0x38) = *(float*)(c + 0x38) * 0.9f + wantVy * 0.1f;
			}
			uint16_t af = *(uint16_t*)(c + 0x7E); if (!(af & 2)) *(uint16_t*)(c + 0x7E) = (uint16_t)(af | 2);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Zustand einer Kreatur fuer die KI: feindlich? lebt? Breite (+0x80), Hoehe (+0x88), Flags, Geschwindigkeit z.
	// Position = Mitte der Kollisionshoehe (CombatBehavior.cpp:574 zieht size.z/2 fuer den Fusspunkt ab).
	static bool AiInfoRaw(uint8_t* c, bool* hostile, float* width, uint16_t* af, float* vz, int* race, float* height) {
		__try {
			*af = *(uint16_t*)(c + 0x7E); *race = *(int*)(c + 0x64); *width = *(float*)(c + 0x80); *height = *(float*)(c + 0x88); *vz = *(float*)(c + 0x3C);
			*hostile = c[0x60] == 1 && !(*af & kModPeace) && *(float*)(c + 0x16C) > 0.0f;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Geschwindigkeit setzen; fly: 1 = Flugflag 0x2 setzen, 0 = loeschen (Schwerkraft), -1 = lassen. ability >= 0: Angriff starten.
	static bool AiDriveRaw(uint8_t* c, float vx, float vy, float vz, int fly, int ability) {
		__try {
			*(float*)(c + 0x34) = vx; *(float*)(c + 0x38) = vy; *(float*)(c + 0x3C) = vz;
			uint16_t af = *(uint16_t*)(c + 0x7E);
			if (fly == 1) af |= 2; else if (fly == 0) af &= (uint16_t)~2;
			*(uint16_t*)(c + 0x7E) = af;
			if (ability >= 0) { c[0x68] = (uint8_t)ability; *(int*)(c + 0x6C) = 0; }
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static int GroundBelow(cube::GameController* gc, int x, int y, int z) {
		for (int k = 0; k < 48; k++) { uint8_t r, g, b, t; if (MountainVillage::LabReadBlock(gc, x, y, z - k, r, g, b, t) && (t & 0x1F) != 0) return z - k; }
		return z - 48;
	}
	static uint32_t AiRand(uint64_t guid, DWORD now) { uint32_t h = (uint32_t)(guid ^ (guid >> 32)) * 2654435761u ^ (now / 97) * 40503u; h ^= h >> 13; return h * 2246822519u; }
	// Sprungangriff am Boden. Rueckgabe true = gesprungen. dx/dy/dz in Bloecken (Spieler - Tier).
	static bool TryLeap(uint8_t* c, float dx, float dy, float dz, bool onGround, DWORD now, DWORD* nextLeap, uint64_t guid) {
		float dh = std::sqrt(dx * dx + dy * dy);
		if (!onGround || (int)(now - *nextLeap) < 0 || dh < 1.2f || dh > 12.0f || dz > 6.0f) return false;
		float k = 2.0f;                                                          // wie der Golem-Sprung: Abstand * 2
		float vx = dx * k, vy = dy * k, hs = std::sqrt(vx * vx + vy * vy);
		if (hs > 16.0f) { vx *= 16.0f / hs; vy *= 16.0f / hs; }
		float vz = (std::min)(14.0f, (std::max)(7.0f, 5.0f + dh * 0.8f + (std::max)(0.0f, dz) * 1.5f));
		AiDriveRaw(c, vx, vy, vz, -1, dh < 4.0f ? 75 : 51);                   // nah: Biss, weiter: Sprung-Rolle
		*nextLeap = now + 1100 + AiRand(guid, now) % 800;
		return true;
	}
	void FlightTick(cube::GameController* gc) {
		if (!gc) return;
		DWORD now = GetTickCount();
		const float t = now / 1000.0f;
		int64_t px = 0, py = 0, pz = 0; bool havePlayer = gc->local_player && ReadPos((uint8_t*)gc->local_player, &px, &py, &pz);
		const float pX = px / 65536.0f, pY = py / 65536.0f, pZ = pz / 65536.0f;
		// eigene feindliche Tiere in Spielernaehe aufnehmen (auch die mit der Zone entstandenen)
		static DWORD lastScan = 0;
		if (havePlayer && now - lastScan > 1000) {
			lastScan = now;
			CreatureInfo ci[64]; int n = ListCreaturesNear(gc, px >> 16, py >> 16, pz >> 16, 48, 32, ci, 64);
			for (int i = 0; i < n; i++) {
				if (ci[i].race < 200 || ci[i].hostility != 1) continue;
				uint8_t* c = FindCreatureRaw(gc, ci[i].guid); if (!c) continue;
				bool hos; float w, vz, hh; uint16_t af; int race;
				if (!AiInfoRaw(c, &hos, &w, &af, &vz, &race, &hh) || !hos || (af & 0x100)) continue;   // 0x100 = Wassertier
				if (af & 2) RegisterFlyer(ci[i].guid, ci[i].x << 16, ci[i].y << 16);
				else if (w > 0.0f && w < 0.85f) RegisterLeaper(ci[i].guid);
			}
		}
		for (size_t i = 0; i < g_flyers.size(); ) {
			Flyer& f = g_flyers[i];
			uint8_t* c = FindCreatureRaw(gc, f.guid);
			if (!c) { g_flyers.erase(g_flyers.begin() + i); continue; }
			int64_t x, y, z; if (!ReadPos(c, &x, &y, &z)) { i++; continue; }
			int bx = (int)(x >> 16), by = (int)(y >> 16), bz = (int)(z >> 16);
			if (f.ground == INT_MIN || (int)(now - f.nextGround) >= 0) { f.ground = GroundBelow(gc, bx, by, bz + 2); f.nextGround = now + 300; }
			bool hos = false; float w = 1, vzNow = 0, hgt = 2; uint16_t af = 0; int race = 0;
			AiInfoRaw(c, &hos, &w, &af, &vzNow, &race, &hgt);
			const float X = x / 65536.0f, Y = y / 65536.0f, Z = z / 65536.0f;
			float dx = pX - X, dy = pY - Y, dz = pZ - Z, dh = std::sqrt(dx * dx + dy * dy) + 0.001f;
			bool engage = havePlayer && hos && dh < 30.0f && std::fabs(dz) < 24.0f;
			if (!engage) {
				if (f.state != FS_PATROL) { f.state = FS_PATROL; f.stateT = now; }
				float alt = 9.0f + 4.5f * std::sin(t * 0.35f + f.phase) + 1.5f * std::sin(t * 1.3f + f.phase * 2.0f);
				float hx = (float)((f.hx >> 16) - bx), hy = (float)((f.hy >> 16) - by), hd = std::sqrt(hx * hx + hy * hy) + 0.01f;
				FlyStepRaw(c, (float)f.ground + 1.0f + alt, hx / hd * 4.0f, hy / hd * 4.0f, hd > 24.0f);
				i++; continue;
			}
			if (f.state == FS_PATROL) { f.state = FS_CLIMB; f.stateT = now; }
			const DWORD el = now - f.stateT;
			const float sz = (std::max)(0.6f, w);                                   // groessere Tiere: weiter weg, hoeher
			switch (f.state) {
				case FS_CLIMB: {                                                  // seitlich ueber den Spieler
					float ax = -dx / dh, ay = -dy / dh;                                // vom Spieler weg
					float ox = ax * std::cos(0.6f * f.side) - ay * std::sin(0.6f * f.side), oy = ax * std::sin(0.6f * f.side) + ay * std::cos(0.6f * f.side);
					float tx = pX + ox * (8.0f + 3.0f * sz), ty = pY + oy * (8.0f + 3.0f * sz), tz = (std::max)(pZ, (float)f.ground) + 9.0f + 2.5f * sz;
					float ex = tx - X, ey = ty - Y, ez = tz - Z, ed = std::sqrt(ex * ex + ey * ey + ez * ez) + 0.001f;
					float sp = (std::min)(14.0f, 3.0f + ed * 2.0f);
					AiDriveRaw(c, ex / ed * sp, ey / ed * sp, ez / ed * sp, 1, -1);
					if (ed < 2.5f || el > 2600) { f.state = FS_DIVE; f.stateT = now; AiDriveRaw(c, dx / dh * 10, dy / dh * 10, -6, 1, 51); }
					break;
				}
				case FS_DIVE: {                                                   // Sturzflug auf die Brust des Spielers
					float ex = dx, ey = dy, ez = dz + 0.2f, ed = std::sqrt(ex * ex + ey * ey + ez * ez) + 0.001f;
					AiDriveRaw(c, ex / ed * 24.0f, ey / ed * 24.0f, ez / ed * 24.0f, 1, -1);
					if (ed < 1.6f + 0.5f * sz || Z < pZ - 0.5f || el > 1400) { f.state = FS_RECOVER; f.stateT = now; f.side = -f.side; }
					break;
				}
				case FS_RECOVER: {                                                // abdrehen: weg und hoch
					AiDriveRaw(c, -dx / dh * 11.0f + dy / dh * 4.0f * f.side, -dy / dh * 11.0f - dx / dh * 4.0f * f.side, 10.0f, 1, -1);
					if (el > 700) {
						bool land = AiRand(f.guid, now) % 100 < 35;
						f.state = land ? FS_GROUND : FS_CLIMB; f.stateT = now; f.nextLeap = now + 600;
					}
					break;
				}
				case FS_GROUND: {                                                 // landen, am Boden springen, dann wieder auf
					bool onGround = Z - hgt * 0.5f - (float)(f.ground + 1) < 0.5f && std::fabs(vzNow) < 1.0f;
					if (el < 3000 && !onGround) AiDriveRaw(c, dx / dh * 4.0f, dy / dh * 4.0f, (std::min)(vzNow, -8.0f), 0, -1);   // herunter
					else TryLeap(c, dx, dy, dz, onGround, now, &f.nextLeap, f.guid);
					if (el > 6500) { f.state = FS_CLIMB; f.stateT = now; AiDriveRaw(c, 0, 0, 10.0f, 1, -1); }
					break;
				}
			}
			i++;
		}
		// kleine Landtiere: Sprungangriffe
		for (size_t i = 0; i < g_leapers.size(); ) {
			Leaper& l = g_leapers[i];
			uint8_t* c = FindCreatureRaw(gc, l.guid);
			if (!c) { g_leapers.erase(g_leapers.begin() + i); continue; }
			int64_t x, y, z; if (!havePlayer || !ReadPos(c, &x, &y, &z)) { i++; continue; }
			bool hos = false; float w = 1, vzNow = 0, hgt = 1; uint16_t af = 0; int race = 0;
			if (!AiInfoRaw(c, &hos, &w, &af, &vzNow, &race, &hgt) || !hos) { i++; continue; }
			int bx = (int)(x >> 16), by = (int)(y >> 16), bz = (int)(z >> 16);
			if (l.ground == INT_MIN || (int)(now - l.nextGround) >= 0) { l.ground = GroundBelow(gc, bx, by, bz + 1); l.nextGround = now + 250; }
			const float X = x / 65536.0f, Y = y / 65536.0f, Z = z / 65536.0f;
			bool onGround = Z - hgt * 0.5f - (float)(l.ground + 1) < 0.5f && std::fabs(vzNow) < 1.0f;
			TryLeap(c, pX - X, pY - Y, pZ - Z, onGround, now, &l.nextLeap, l.guid);
			i++;
		}
	}
	int FlyerCount() { return (int)g_flyers.size(); }
	// ---- Schwimmen (2026-10-14) -----------------------------------------------------------------
	// Wassertiere bewegt der Mod selbst: Positionsschreiben je Bild (wie OceanContent -- Geschwindigkeiten
	// verwirft die native KI bei Feindlichkeit 1/3), Hoehe zwischen Grund und Wasseroberflaeche, an Ufern umdrehen.
	// Nah am Spieler (7 Bloecke) und feindlich -> nichts tun, der native Kampf uebernimmt.
	struct Swimmer { uint64_t guid; int64_t hx, hy; float heading, depth, phase; int surface, bottom; bool inWater, hostile; DWORD nextScan, last; };
	static std::vector<Swimmer> g_swimmers;
	int SwimmerCount() { return (int)g_swimmers.size(); }
	static void RegisterSwimmer(uint64_t guid, int64_t x, int64_t y, float depth, bool hostile) {
		if (!guid) return;
		for (const Swimmer& s : g_swimmers) if (s.guid == guid) return;
		if (g_swimmers.size() >= 96) g_swimmers.erase(g_swimmers.begin());
		g_swimmers.push_back({ guid, x, y, (float)(guid % 628) / 100.0f, depth, (float)(guid % 314) / 50.0f, INT_MIN, INT_MIN, false, hostile, 0, GetTickCount() });
	}
	static bool IsWater(cube::GameController* gc, int x, int y, int z) {
		uint8_t r, g, b, t; return MountainVillage::LabReadBlock(gc, x, y, z, r, g, b, t) && (t & 0x1F) == 2;
	}
	// Wassersaeule an (x, y) um z: Oberflaeche (oberster Wasserblock) und Grund (erster Block darunter, der kein Wasser ist)
	static bool WaterColumn(cube::GameController* gc, int x, int y, int z, int* surface, int* bottom) {
		int w = INT_MIN;
		for (int k = 0; k <= 3 && w == INT_MIN; k++) { if (IsWater(gc, x, y, z - k)) w = z - k; else if (IsWater(gc, x, y, z + k)) w = z + k; }
		if (w == INT_MIN) return false;
		int s = w; for (int k = 0; k < 96 && IsWater(gc, x, y, s + 1); k++) s++;
		int b = w; for (int k = 0; k < 160 && IsWater(gc, x, y, b - 1); k++) b--;
		*surface = s; *bottom = b - 1;                                      // bottom = fester Grund
		return true;
	}
	static bool SwimMoveRaw(uint8_t* c, int64_t nx, int64_t ny, int64_t nz) {
		__try {
			*(int64_t*)(c + 0x10) = nx; *(int64_t*)(c + 0x18) = ny; *(int64_t*)(c + 0x20) = nz;
			*(float*)(c + 0x34) = 0.0f; *(float*)(c + 0x38) = 0.0f; *(float*)(c + 0x3C) = 0.0f;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void SwimTick(cube::GameController* gc) {                               // nur Liste pflegen (Bewegung macht das Spiel)
		if (!gc || g_swimmers.empty()) return;
		static DWORD last = 0; DWORD now = GetTickCount(); if (now - last < 2000) return; last = now;
		for (size_t i = 0; i < g_swimmers.size(); ) { if (!FindCreatureRaw(gc, g_swimmers[i].guid)) g_swimmers.erase(g_swimmers.begin() + i); else i++; }
	}
	// Gezaehmte eigene Tiere: Das Haustier-Item merkt nur Basisrasse + Namen; beim Rufen baut das Spiel das native
	// Aussehen. PetSkinTick erkennt eigene Begleiter am Namen und setzt ihre Teile/Groesse wieder ein.
	static std::vector<CustomMob> g_ownIdx; static bool g_ownIdxBuilt = false;
	static void BuildOwnIdx(const std::string& dir, int depth) {
		if (depth > 4) return;
		WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (hf == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) BuildOwnIdx(p, depth + 1); continue; }
			if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".mob")) continue;
			CustomMob m; std::string err;
			if (LoadMob(p, m, err) && m.ownRace >= 200) g_ownIdx.push_back(m);
		} while (FindNextFileA(hf, &fd));
		FindClose(hf);
	}
	static bool PetInfoRaw(uint8_t* c, uint64_t player, char* name16, int* race, float* size) {
		__try {
			if (c[0x60] != 5) return false;
			if (*(uint64_t*)(c + 0x198) != player) return false;
			memcpy(name16, c + 0x1168, 16); name16[15] = 0; *race = *(int*)(c + 0x64); memcpy(size, c + 0x80, 12);
			return name16[0] != 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void PetSkinTick(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (!gc || !gc->local_player || now - last < 1000) return;
		last = now;
		if (!g_ownIdxBuilt) { g_ownIdxBuilt = true; BuildOwnIdx(MobsDir(), 0); }
		if (g_ownIdx.empty()) return;
		static std::vector<uint64_t> done;
		uint64_t player = 0; ReadCreature((uint8_t*)gc->local_player, 0x08, &player, 8);
		int64_t px, py, pz; if (!ReadPos((uint8_t*)gc->local_player, &px, &py, &pz)) return;
		CreatureInfo ci[24];
		int n = ListCreaturesNear(gc, px >> 16, py >> 16, pz >> 16, 48, 24, ci, 24);
		for (int i = 0; i < n; i++) {
			if (ci[i].hostility != 5) continue;
			if (std::find(done.begin(), done.end(), ci[i].guid) != done.end()) continue;
			uint8_t* c = FindCreatureRaw(gc, ci[i].guid); if (!c) continue;
			char nm[16]; int race; float sz[3];
			if (!PetInfoRaw(c, player, nm, &race, sz)) continue;
			const CustomMob* m = nullptr;
			for (const CustomMob& o : g_ownIdx) {
				std::string dn = o.raceName.empty() ? o.name : o.raceName;
				if (o.baseRace == race && strncmp(dn.c_str(), nm, 15) == 0) { m = &o; break; }
			}
			done.push_back(ci[i].guid); if (done.size() > 256) done.erase(done.begin());
			if (!m) continue;
			for (int k = 0; k < P_COUNT; k++) {
				const Part& p = m->parts[k];
				if (p.source == SRC_NONE) { int16_t v = -1; WriteCreature(c, kIdOff[k], &v, 2); }
				else if (p.source == SRC_NATIVE) { int16_t v = (int16_t)p.nativeIndex; WriteCreature(c, kIdOff[k], &v, 2); }
				else if (p.source == SRC_FILE) { std::string err; int idx = InjectFile(gc, p.file, m->baseDir, err); if (idx >= 0) { int16_t v = (int16_t)idx; WriteCreature(c, kIdOff[k], &v, 2); } }
				if (p.scale > 0.0f) WriteCreature(c, kScaleOff[k], &p.scale, 4);
			}
			if (m->sizeFactor > 0.0f && (m->sizeFactor < 0.999f || m->sizeFactor > 1.001f)) { for (int k = 0; k < 3; k++) sz[k] *= m->sizeFactor; WriteCreature(c, 0x80, sz, 12); }
			std::cout << "[Mob] Begleiter '" << nm << "': Aussehen des eigenen Tiers wieder eingesetzt." << std::endl;
		}
	}
	// ---- Teile vorbereiten / auf einen nativen Spawn-Datensatz schreiben (2026-10-16) -------------------
	// Spielthread: eigene .cub-Teile in die Modell-Registry (InjectFile) -> ids[] (-2 = Basis behalten, -1 = keins).
	void PrepareMobParts(cube::GameController* gc, const CustomMob& m, int ids[8]) {
		for (int i = 0; i < P_COUNT && i < 8; i++) {
			const Part& p = m.parts[i]; ids[i] = -2;
			if (p.source == SRC_NONE) ids[i] = -1;
			else if (p.source == SRC_NATIVE) ids[i] = p.nativeIndex;
			else if (p.source == SRC_FILE) { std::string err; int idx = InjectFile(gc, p.file, m.baseDir, err); ids[i] = idx >= 0 ? idx : -2; }
		}
	}
	// Beliebiger Thread (Zonenerzeugung): Aussehen in einem nativen cube::Spawn (Aussehen ab +0x74, also Kreatur-Offset - 4;
	// Rasse +0x2C) ueberschreiben. Die Umwandlung Spawn -> Kreatur (GameController.cpp:96641ff.) kopiert alles mit.
	void ApplyToSpawn(uint8_t* sp, const CustomMob& m, const int ids[8], int ownRace) {
		for (int i = 0; i < P_COUNT && i < 8; i++) {
			if (ids[i] != -2) *(int16_t*)(sp + kIdOff[i] - 4) = (int16_t)ids[i];
			if (m.parts[i].scale > 0.0f) *(float*)(sp + kScaleOff[i] - 4) = m.parts[i].scale;
			if (m.parts[i].setPos && kPosOff[i] >= 0) memcpy(sp + kPosOff[i] - 4, m.parts[i].pos, 12);
		}
		if (m.sizeFactor > 0.0f && (m.sizeFactor < 0.999f || m.sizeFactor > 1.001f))
			for (int k = 0; k < 3; k++) *(float*)(sp + 0x7C + 4 * k) *= m.sizeFactor;
		uint16_t af = *(uint16_t*)(sp + 0x7A);
		if (m.fly == 1) af |= 2;
		if (m.swim == 1) af = (uint16_t)((af & ~2) | 0x100);
		if (m.peaceful) af = PeaceFlags(af);                                  // friedlich: Marke fuer den Hook auf 0x444680
		*(uint16_t*)(sp + 0x7A) = af;
		if (ownRace >= 200) { *(int*)(sp + 0x2C) = ownRace; ApplyBodyRaw(ownRace, sp + 0x74); }   // Koerperbau der eigenen Rasse
	}
	void RegisterFlyerGuid(uint64_t guid, int64_t x, int64_t y) { RegisterFlyer(guid, x, y); }
	uint16_t PeacefulFlags(uint16_t af) { return PeaceFlags(af); }
	uint16_t HostileModFlags(uint16_t af) { return HostileFlags(af); }
	static bool MakeRaceTemplateRaw(int race, RaceTpl& out) {
		__try {
			uint8_t* sp = PrepareSpawnRaw(race, 1, -1, 1, nullptr);
			if (!sp) return false;
			out.race = race; out.cls[0] = sp[0x30]; out.cls[1] = sp[0x31];
			memcpy(out.app, sp + 0x74, 0xAC); memcpy(out.fac, sp + 0xF58, 0x14);
			FreeSpawnRaw(sp);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool MakeRaceTemplate(int race, RaceTpl& out) { return race >= 0 && race < kRaceCount && MakeRaceTemplateRaw(race, out); }
	// Modellnummer (Registry-Index), die das Spiel fuer ein Teil der Rasse waehlt (Aussehen wie bei einem nativen Spawn;
	// Aussehen beginnt bei Kreatur+0x78 bzw. Spawn+0x74 -> Teil-Kennung bei kIdOff - 0x78). -1 = kein Modell.
	int BasePartModel(int race, int part) {
		if (part < 0 || part >= P_COUNT) return -1;
		RaceTpl t; if (!MakeRaceTemplate(race, t)) return -1;
		int16_t id = *(int16_t*)(t.app + (kIdOff[part] - 0x78));
		return id;
	}
	// ---- Koerperbau je Rasse (2026-10-16) ----------------------------------------------------------------
	// Wo welches Koerperteil liegt und wie gross es ist, legt creature_generateAppearance (0x43F7C0, cdecl(&rasse,
	// &aussehen, seed)) je Rasse im Aussehen fest (Aussehen = Spawn+0x74 = Kreatur+0x78). Ein Hook ueberschreibt danach
	// die Felder aus Strukturen\Mobs\_koerperbau.txt -- fuer ALLE Kreaturen dieser Rasse, die danach entstehen
	// (nativer Spawn, Mod-Spawns, eigene Mobs mit Teilen "von der Basisrasse"). Bereits lebende behalten ihr Aussehen.
	const BodyField kBodyFields[kBodyFieldCount] = {
		{ "kollision",       "Kollision (Breite/Tiefe/Hoehe)", 0x08, 3 },
		{ "skala_kopf",      "Skala Kopf (+Haare)",            0x24, 1 },
		{ "skala_koerper",   "Skala Koerper",                  0x28, 1 },
		{ "skala_haende",    "Skala Haende",                   0x2C, 1 },
		{ "skala_fuesse",    "Skala Fuesse",                   0x30, 1 },
		{ "skala_schwanz",   "Skala Schwanz",                  0x3C, 1 },
		{ "skala_schulter",  "Skala Schulter",                 0x40, 1 },
		{ "skala_fluegel",   "Skala Fluegel",                  0x44, 1 },
		{ "pos_koerper",     "Position Koerper",               0x64, 3 },
		{ "pos_kopf",        "Position Kopf",                  0x70, 3 },
		{ "pos_haende",      "Position Haende",                0x7C, 3 },
		{ "pos_fuesse",      "Position Fuesse",                0x88, 3 },
		{ "pos_94",          "Position +0x94 (unbelegt)",      0x94, 3 },
		{ "pos_fluegel",     "Position Fluegel",               0xA0, 3 },
	};
	struct BodyTable { bool used[kBodyMax]; BodyVals v[kBodyMax]; };
	static std::atomic<BodyTable*> g_body{ nullptr };                 // alte Tabellen werden nie freigegeben (Zonenthread liest)
	static volatile LONG g_bodyBypass = 0;
	static volatile LONG g_bodyVersion = 0;
	long BodyVersion() { return g_bodyVersion; }                            // >0: Hook greift nicht (native Werte lesen)
	typedef void(__cdecl* GenApp_t)(void* race, void* app, void* seed);
	static GenApp_t o_GenApp = nullptr;
	static void ApplyBodyRaw(int race, uint8_t* app) {
		BodyTable* t = g_body.load();
		if (!t || race < 0 || race >= kBodyMax || !t->used[race] || g_bodyBypass > 0) return;
		const BodyVals& b = t->v[race];
		__try {
			for (int f = 0; f < kBodyFieldCount; f++)
				if (b.mask & (1u << f)) memcpy(app + kBodyFields[f].off, b.val[f], 4 * kBodyFields[f].n);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void __cdecl Hook_GenApp(void* race, void* app, void* seed) {
		o_GenApp(race, app, seed);
		int r = -1; __try { r = *(int*)race; } __except (EXCEPTION_EXECUTE_HANDLER) {}
		ApplyBodyRaw(r, (uint8_t*)app);
	}
	std::string BodyFilePath() { return MobsDir() + "\\_koerperbau.txt"; }
	static BodyTable* ParseBodyFile() {
		BodyTable* t = new BodyTable(); memset(t, 0, sizeof(*t));
		std::ifstream f(BodyFilePath()); std::string line;
		while (std::getline(f, line)) {                                  // rasse;feld;wert[,wert,wert]
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			size_t a = line.find(';'), b = a == std::string::npos ? a : line.find(';', a + 1);
			if (b == std::string::npos) continue;
			int race = atoi(line.substr(0, a).c_str()); std::string key = line.substr(a + 1, b - a - 1), v = line.substr(b + 1);
			if (race < 0 || race >= kBodyMax) continue;
			for (int fi = 0; fi < kBodyFieldCount; fi++) if (key == kBodyFields[fi].key) {
				float x[3] = {}; int n = sscanf_s(v.c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]);
				if (n < kBodyFields[fi].n) break;
				memcpy(t->v[race].val[fi], x, sizeof(x)); t->v[race].mask |= 1u << fi; t->used[race] = true;
			}
		}
		return t;
	}
	void ReloadBodyDefs() {
		BodyTable* t = ParseBodyFile(); g_body.store(t);
		int n = 0; for (int r = 0; r < kBodyMax; r++) if (t->used[r]) n++;
		InterlockedIncrement(&g_bodyVersion);
		std::cout << "[Mob] Koerperbau: " << n << " Rassen angepasst (" << BodyFilePath() << ")." << std::endl;
	}
	bool GetBodyDef(int race, BodyVals& out) {
		BodyTable* t = g_body.load(); out = BodyVals();
		if (!t || race < 0 || race >= kBodyMax || !t->used[race]) return false;
		out = t->v[race]; return true;
	}
	// Testweise (ohne Datei) fuer eine Rasse setzen; mask 0 = Anpassung aufheben
	void SetBodyDefLive(int race, const BodyVals& v) {
		if (race < 0 || race >= kBodyMax) return;
		BodyTable* old = g_body.load(); BodyTable* t = new BodyTable();
		if (old) *t = *old; else memset(t, 0, sizeof(*t));
		t->v[race] = v; t->used[race] = v.mask != 0;
		g_body.store(t); InterlockedIncrement(&g_bodyVersion);
	}
	bool SaveBodyDefs(std::string& err) {
		BodyTable* t = g_body.load();
		std::ofstream f(BodyFilePath(), std::ios::trunc);
		if (!f) { err = "Datei nicht schreibbar: " + BodyFilePath(); return false; }
		f << "# Koerperbau je Rasse (LAB > Mobs > Koerperbau). rasse;feld;wert[,wert,wert]\n"
		     "# Felder: Aussehen (Kreatur+0x78) wie creature_generateAppearance 0x43F7C0. Nur gelistete Felder werden ersetzt.\n";
		if (t) for (int r = 0; r < kBodyMax; r++) if (t->used[r]) {
			f << "# " << r << " " << (r < kRaceCount ? RaceName(r) : "eigene Rasse") << "\n";
			for (int fi = 0; fi < kBodyFieldCount; fi++) if (t->v[r].mask & (1u << fi)) {
				char b[96]; const float* x = t->v[r].val[fi];
				if (kBodyFields[fi].n == 3) sprintf_s(b, "%.3f,%.3f,%.3f", x[0], x[1], x[2]); else sprintf_s(b, "%.3f", x[0]);
				f << r << ";" << kBodyFields[fi].key << ";" << b << "\n";
			}
		}
		return true;
	}
	// Spielthread: Werte, die das Spiel fuer die Rasse erzeugt -- nativ (ohne Anpassung) und wirksam (mit)
	bool ReadBodyValues(int race, BodyVals& native, BodyVals& effective) {
		RaceTpl t;
		InterlockedIncrement(&g_bodyBypass); bool ok = MakeRaceTemplate(race, t); InterlockedDecrement(&g_bodyBypass);
		if (!ok) return false;
		native = BodyVals(); native.mask = (1u << kBodyFieldCount) - 1;
		for (int f = 0; f < kBodyFieldCount; f++) memcpy(native.val[f], t.app + kBodyFields[f].off, 4 * kBodyFields[f].n);
		effective = native; BodyVals ov;
		if (GetBodyDef(race, ov)) for (int f = 0; f < kBodyFieldCount; f++) if (ov.mask & (1u << f)) memcpy(effective.val[f], ov.val[f], sizeof(ov.val[f]));
		return true;
	}
	// Eigene Rasse: Basisrasse wie das Spiel, darauf die Teile/Skalen/Positionen des Mobs (wie am nativen Spawn)
	bool ReadBodyValuesMob(const CustomMob& m, BodyVals& native, BodyVals& effective) {
		RaceTpl t;
		InterlockedIncrement(&g_bodyBypass); bool ok = MakeRaceTemplate(m.baseRace, t); InterlockedDecrement(&g_bodyBypass);
		if (!ok) return false;
		std::vector<uint8_t> buf(0x10F0, 0); memcpy(&buf[0x74], t.app, 0xAC);
		int ids[8]; for (int& v : ids) v = -2;
		ApplyToSpawn(&buf[0], m, ids, -1);
		native = BodyVals(); native.mask = (1u << kBodyFieldCount) - 1;
		for (int f = 0; f < kBodyFieldCount; f++) memcpy(native.val[f], &buf[0x74 + kBodyFields[f].off], 4 * kBodyFields[f].n);
		effective = native; BodyVals ov;
		if (m.ownRace >= 200 && GetBodyDef(m.ownRace, ov)) for (int f = 0; f < kBodyFieldCount; f++) if (ov.mask & (1u << f)) memcpy(effective.val[f], ov.val[f], sizeof(ov.val[f]));
		return true;
	}
	typedef uint32_t(__fastcall* IsPassive_t)(uint8_t* c, void* edx);
	static IsPassive_t o_IsPassive = nullptr;
	static uint32_t __fastcall Hook_IsPassive(uint8_t* c, void* edx) {
		uint32_t r = o_IsPassive(c, edx);
		__try {
			uint16_t f = *(uint16_t*)(c + 0x7E); int race = *(int*)(c + 0x64);
			if (f & 0x1A00) return r;                                        // wie nativ: diese Flags sind nie passiv
			if (f & kModHostile) return r & ~0xFFu;                          // Mod: feindlich (auch Kuh & Co.)
			if (f & kModPeace) return (r & ~0xFFu) | 1u;                     // Mod: friedlich
			if (race >= 200 && race < 512) return (r & ~0xFFu) | (g_peaceRace[race] ? 1u : 0u);   // eigene Rasse laut .mob
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return r;
	}
	// ---- Namensschild: Uebersetzung (2026-10-19) ---------------------------------------------------------------
	// Das Schild ueber einer Kreatur (0x49ADCF..0x49AE28) holt erst den Rassennamen (World_getObjectName 0x59AA60 ->
	// z.B. "Chicken"), dann die ANZEIGE aus der Sprachtabelle: lookupNestedStringMap 0x480E00 = thiscall(GC+0x314,
	// wstring* out, const wstring* wort, wstring form ("singular", als Wert)), ret 0x20. Fehlt das Wort in der Tabelle,
	// kommt "" zurueck -- deshalb stand bei eigenen Rassen nur "LV 13" (CreatureLevelDisplay setzt bloss "LV n " davor).
	// Der Hook liefert fuer eigene Rassennamen das Wort selbst, wenn die Tabelle nichts hat.
	struct WStr { uint8_t b[24]; };                                    // MSVC wstring: Puffer[8 wchar]/Zeiger, Laenge +0x10, Kapazitaet +0x14
	typedef void* (__fastcall* Lookup_t)(void* self, void* edx, void* out, const WStr* word, WStr form);
	static Lookup_t o_Lookup = nullptr;
	static std::atomic<std::vector<std::wstring>*> g_ownNames{ nullptr };   // alte Listen werden nie freigegeben
	static bool WStrRead(const void* s, wchar_t* out, size_t cap) {
		__try {
			const uint8_t* p = (const uint8_t*)s; uint32_t len = *(const uint32_t*)(p + 0x10), c = *(const uint32_t*)(p + 0x14);
			if (len >= cap) return false;
			const wchar_t* w = c >= 8 ? *(const wchar_t* const*)p : (const wchar_t*)p;
			memcpy(out, w, len * 2); out[len] = 0; return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool WStrEmpty(const void* s) { __try { return *(const uint32_t*)((const uint8_t*)s + 0x10) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	typedef void* (__fastcall* WAssign_t)(void* str, void* edx, const wchar_t* s, size_t n);
	static void WStrAssignSEH(void* out, const wchar_t* w, size_t n) { __try { ((WAssign_t)Re(0x40F7A0))(out, nullptr, w, n); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void* __fastcall Hook_Lookup(void* self, void* edx, void* out, const WStr* word, WStr form) {
		wchar_t w[64]; bool haveWord = word && WStrRead(word, w, 64);
		void* r = o_Lookup(self, edx, out, word, form);                   // uebernimmt 'form' (gibt ihn frei)
		if (haveWord && out && WStrEmpty(out)) {
			std::vector<std::wstring>* names = g_ownNames.load();
			if (names) for (const std::wstring& n : *names) if (n == w) { WStrAssignSEH(out, w, wcslen(w)); break; }
		}
		return r;
	}
	bool InstallBodyHook() {
		{
			uint8_t* a = (uint8_t*)Re(0x480E00); static const uint8_t pro[6] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
			bool ok = a && memcmp(a, pro, 6) == 0 && MH_CreateHook((LPVOID)a, (LPVOID)&Hook_Lookup, (LPVOID*)&o_Lookup) == MH_OK;
			std::cout << "[Mob] Namensschild-Hook (Sprachtabelle 0x480E00): " << (ok ? "ok" : "FEHLER") << std::endl;
		}
		{   // friedliche Tiere (0x444680): Prolog pruefen (movzx eax, word [ecx+0x7E])
			uint8_t* a = (uint8_t*)Re(0x444680); static const uint8_t pro[4] = { 0x0F, 0xB7, 0x41, 0x7E };
			bool ok = a && memcmp(a, pro, 4) == 0 && MH_CreateHook((LPVOID)a, (LPVOID)&Hook_IsPassive, (LPVOID*)&o_IsPassive) == MH_OK;
			std::cout << "[Mob] Friedlich-Hook (creature_isPassive 0x444680): " << (ok ? "ok" : "FEHLER") << std::endl;
		}
		ReloadBodyDefs();
		uint32_t a = Re(kFnGenAppearance); if (!a) return false;
		bool ok = MH_CreateHook((LPVOID)a, (LPVOID)&Hook_GenApp, (LPVOID*)&o_GenApp) == MH_OK;
		std::cout << "[Mob] Koerperbau-Hook (0x43F7C0): " << (ok ? "ok" : "FEHLER") << std::endl;
		return ok;
	}
	void ApplyRaceTemplate(uint8_t* sp, const RaceTpl& t) {
		*(int*)(sp + 0x2C) = t.race; sp[0x30] = t.cls[0]; sp[0x31] = t.cls[1];
		memcpy(sp + 0x74, t.app, 0xAC); memcpy(sp + 0xF58, t.fac, 0x14);
	}
	// ---- Native Kreaturen auslesen (2026-10-16) -----------------------------------------------------
	// Fuer friedliche/zaehmbare native Tiere: alle Felder, die Verhalten, Angreifbarkeit, Zaehmen und Namen bestimmen.
	// Verhaltensbaum: Knoten-vtables (CombatBehavior 0x6FD310, CompanionBehavior 0x6FD868, LookAtPlayer 0x702D60,
	// RandomInteraction 0x7039D0, RandomWalk 0x7039E8, Sequential 0x703A00, SpawnLocation 0x703B58, WalkPath 0x71A770).
	static const char* BehName(uint32_t vtStatic) {
		switch (vtStatic) {
			case 0x6FD310: return "Combat"; case 0x6FD868: return "Companion"; case 0x702D60: return "LookAtPlayer";
			case 0x7039D0: return "RandomInteraction"; case 0x7039E8: return "RandomWalk"; case 0x703A00: return "Sequential";
			case 0x703B58: return "SpawnLocation"; case 0x71A770: return "WalkPath"; default: return "?";
		}
	}
	static void DescribeTreeRaw(uint32_t beh, char* out, size_t cap, int depth) {
		__try {
			if (!beh || depth > 3) return;
			uint32_t vt = *(uint32_t*)beh; uint32_t vs = vt - Host::Base() + 0x400000;
			char b[96]; sprintf_s(b, "%s(0x%06X", BehName(vs), vs); strcat_s(out, cap, b);
			if (vs == 0x6FD310) { sprintf_s(b, " r=%.1f", *(float*)(beh + 8)); strcat_s(out, cap, b); }
			if (vs == 0x703A00) {                                           // Sequential: Liste ab +4, Knoten +8 = Behavior*
				uint32_t head = *(uint32_t*)(beh + 4), node = head ? *(uint32_t*)head : 0; int g = 0;
				strcat_s(out, cap, " {");
				while (node && node != head && g++ < 12) { DescribeTreeRaw(*(uint32_t*)(node + 8), out, cap, depth + 1); strcat_s(out, cap, " "); node = *(uint32_t*)node; }
				strcat_s(out, cap, "}");
			}
			strcat_s(out, cap, ")");
		} __except (EXCEPTION_EXECUTE_HANDLER) { strcat_s(out, cap, "<Fehler>"); }
	}
	static bool InspectRaw(uint8_t* c, char* out, size_t cap) {
		__try {
			char nm[17] = {}; memcpy(nm, c + 0x1168, 16);
			uint8_t buffs[16]; int nb = 0;
			uint32_t lh = *(uint32_t*)(c + 0x1178);
			if (lh) { uint32_t nd = *(uint32_t*)lh; int g = 0; while (nd && nd != lh && g++ < 16) { if (nb < 16) buffs[nb++] = *(uint8_t*)(nd + 8); nd = *(uint32_t*)nd; } }
			sprintf_s(out, cap, "Rasse %d (%s), Feindlichkeit %d, Klasse 0x%02X/%d, Level %d, Besitzer %llu, Flags7E 0x%04X, Flags124 0x%04X, "
			          "Leine(+1D2C) %.0f, Name '%s', HP %.0f, Aktion 0x%02X, Groesse %.2f/%.2f/%.2f, Buffs:",
			          *(int*)(c + 0x64), RaceName(*(int*)(c + 0x64)), c[0x60], c[0x140], c[0x141], *(int*)(c + 0x190),
			          (unsigned long long)*(uint64_t*)(c + 0x198), *(uint16_t*)(c + 0x7E), *(uint16_t*)(c + 0x124),
			          *(float*)(c + 0x1D2C), nm, *(float*)(c + 0x16C), c[0x68], *(float*)(c + 0x80), *(float*)(c + 0x84), *(float*)(c + 0x88));
			for (int i = 0; i < nb; i++) { char b[8]; sprintf_s(b, " %d", buffs[i]); strcat_s(out, cap, b); }
			strcat_s(out, cap, "; Baum: ");
			DescribeTreeRaw(*(uint32_t*)(c + 0x13E4), out, cap, 0);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Greift ein Wesen mit diesen Aussehen-Flags (Kreatur +0x7E / Spawn +0x7A) und dieser Rasse von sich aus an?
	// creature_isPassive 0x444680 (mit unserem Hook) liest nur +0x7E und +0x64 -> mit einem Ersatz-Datensatz fragen.
	bool PassiveOf(uint16_t flags, int race) {
		alignas(8) uint8_t fake[0x100] = {};
		*(uint16_t*)(fake + 0x7E) = flags; *(int*)(fake + 0x64) = race;
		__try { return (((IsPassive_t)Re(0x444680))(fake, nullptr) & 0xFF) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool IsPassiveNow(uint8_t* c) { if (!c) return false; __try { return (((IsPassive_t)Re(0x444680))(c, nullptr) & 0xFF) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	std::string InspectNearby(cube::GameController* gc, int radius) {
		std::ostringstream o;
		int64_t px, py, pz; if (!gc || !gc->local_player || !ReadPos((uint8_t*)gc->local_player, &px, &py, &pz)) return "kein Spieler";
		CreatureInfo ci[16];
		int n = ListCreaturesNear(gc, px >> 16, py >> 16, pz >> 16, radius, radius, ci, 16);
		int shown = 0;
		for (int i = 0; i < n; i++) {
			uint8_t* c = FindCreatureRaw(gc, ci[i].guid);
			if (!c || c == (uint8_t*)gc->local_player) continue;
			char b[1400] = {};
			if (!InspectRaw(c, b, sizeof(b))) continue;
			std::string rn = RaceNameInGame(gc, ci[i].race);
			if (ci[i].race >= kRaceCount) { std::string tag = std::string("Rasse ") + std::to_string(ci[i].race) + " (?)"; size_t k = std::string(b).find(tag);
				if (k != std::string::npos) { std::string t(b); t.replace(k, tag.size(), "Rasse " + std::to_string(ci[i].race) + " (" + rn + ", eigene)"); strcpy_s(b, t.c_str()); } }
			{ uint8_t* cc = FindCreatureRaw(gc, ci[i].guid); uint16_t f7 = 0; if (cc) ReadCreature(cc, 0x7E, &f7, 2);
			  char pb[96]; sprintf_s(pb, "; greift von sich aus an: %s", IsPassiveNow(cc) ? "nein (friedlich)" : "ja"); strcat_s(b, pb); }
			o << ((ci[i].guid >> 32) == 0xCAFECAFEull ? "[MOD]    " : "[NATIV]  ") << "GUID " << ci[i].guid << ": " << b
			  << "; Rassenname im Spiel: '" << rn << "'" << (rn.empty() ? " (FEHLT -> Schild ohne Namen)" : "") << "\n";
			shown++;
		}
		if (!shown) o << "Keine Kreatur im Umkreis von " << radius << " Bloecken.";
		return o.str();
	}
	// ---- Namensanzeige (2026-10-16) ---------------------------------------------------------------
	// Die Namensschilder baut das Spiel aus der EntitesMap (World+4), aber nur fuer Kreaturen, deren Anzeigeposition
	// Kreatur+0x1350 (int64 x/y/z, sonst vom Netz-Abgleich gesetzt) naeher als 3600 Einheiten am Spieler liegt
	// (Schleife 0x490920..0x4909B7). Vom Mod gesetzte Kreaturen (GUID 0xCAFECAFE........) bekommen diese Position
	// nie -- also kein Schild. ModCreatureTick fuehrt +0x1350 jedes Bild auf die echte Position (+0x10) nach.
	static int ModCreaturesRaw(uint8_t* worldEnt) {
		int n = 0;
		__try {
			uint8_t* head = *(uint8_t**)(worldEnt + 4);
			if (!head) return 0;
			uint8_t* stack[96]; int sp = 0, visited = 0;
			uint8_t* nd = *(uint8_t**)(head + 4);
			while ((nd && !nd[0xD]) || sp > 0) {
				while (nd && !nd[0xD]) { if (sp >= 96) return n; stack[sp++] = nd; nd = *(uint8_t**)(nd + 0); }
				nd = stack[--sp];
				if (++visited > 8192) break;
				uint64_t guid = *(uint64_t*)(nd + 0x10);
				uint8_t* c = *(uint8_t**)(nd + 0x18);
				if (c && (guid >> 32) == 0xCAFECAFEull) { memcpy(c + 0x1350, c + 0x10, 24); n++; }
				nd = *(uint8_t**)(nd + 8);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}
	static bool SetPeacefulRaw(uint8_t* c) {
		__try { c[0x60] = 1; uint16_t af = *(uint16_t*)(c + 0x7E); *(uint16_t*)(c + 0x7E) = PeaceFlags(af); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool SetPeaceful(cube::GameController* gc, uint64_t guid) { uint8_t* c = FindCreatureRaw(gc, guid); return c && SetPeacefulRaw(c); }
	// Alle eigenen Rassen (eigene_rasse= in jeder .mob unter Strukturen\Mobs) alle 10 s in der Namenstabelle sicherstellen --
	// so steht der Rassenname auch bei Tieren, die nie ueber den Mod-Spawn liefen (nativer Spawn, Gruppen, geladene Welt).
	static void ScanRaceNames(const std::string& dir, int depth, std::map<int, std::string>& out, std::map<int, bool>& peace) {
		if (depth > 4) return;
		WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (hf == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) ScanRaceNames(p, depth + 1, out, peace); continue; }
			if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".mob")) continue;
			std::ifstream in(p); std::string line, rn, nm; int race = -1; bool calm = false; int hos = 1;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				size_t eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
				std::string k = line.substr(0, eq), v = line.substr(eq + 1);
				if (k == "eigene_rasse") race = atoi(v.c_str()); else if (k == "rassen_name") rn = v; else if (k == "name") nm = v;
				else if (k == "friedlich") calm = atoi(v.c_str()) != 0; else if (k == "feindlichkeit") hos = atoi(v.c_str());
			}
			if (race >= 200 && !out.count(race)) { out[race] = rn.empty() ? nm : rn; peace[race] = calm || hos == 2 || hos == 3 || hos == 4; }
		} while (FindNextFileA(hf, &fd));
		FindClose(hf);
	}
	static void RaceNameTick(cube::GameController* gc) {
		static std::map<int, std::string> names; static DWORD lastScan = 0, lastReg = 0; static bool logged = false;
		DWORD now = GetTickCount();
		if (names.empty() || now - lastScan > 60000) {
			lastScan = now; names.clear(); std::map<int, bool> peace; ScanRaceNames(MobsDir(), 0, names, peace);
			for (int r = 200; r < 512; r++) g_peaceRace[r] = 0;
			std::vector<std::wstring>* nv = new std::vector<std::wstring>();
			for (auto& kv : names) { wchar_t w[64] = { 0 }; if (MultiByteToWideChar(CP_ACP, 0, kv.second.c_str(), -1, w, 63) > 1) nv->push_back(w); }
			g_ownNames.store(nv);
			for (auto& kv : peace) if (kv.first >= 200 && kv.first < 512) g_peaceRace[kv.first] = kv.second ? 1 : 0;
		}
		if (now - lastReg < 10000) return;
		lastReg = now;
		int fixed = 0;
		for (auto& kv : names) { char cur[64]; if (!LookupRaceNameRaw(gc, kv.first, cur, sizeof(cur)) || kv.second != cur) { RegisterRaceName(gc, kv.first, kv.second); fixed++; } }
		if (!logged || fixed) { std::cout << "[Mob] Rassennamen: " << names.size() << " eigene Rassen, " << fixed << " (neu) eingetragen." << std::endl; logged = true; }
	}
	void ModCreatureTick(cube::GameController* gc) { if (gc) { ModCreaturesRaw((uint8_t*)&gc->world); RaceNameTick(gc); } }
	// ---- Masse (2026-10-14) ------------------------------------------------------------------------
	// Das Spiel laesst Spieler und Kreatur nur kollidieren, wenn World_areEntitiesHostile (0x5988A0) sie als
	// Gegner sieht (Feindlichkeit 1 bzw. 6); friedliche Tiere (3) waren durchlaessig. Fuer alle eigenen Tiere
	// schiebt SolidTick den Spieler aus der Huelle (Kreatur +0x80/+0x84 Breite/Tiefe, +0x88 Hoehe in Bloecken,
	// Position = Fussmitte) und laesst ihn auf grossen Tieren stehen.
	static std::vector<uint64_t> g_solids;
	static void RegisterSolid(uint64_t guid) {
		if (!guid) return;
		for (uint64_t g : g_solids) if (g == guid) return;
		if (g_solids.size() >= 160) g_solids.erase(g_solids.begin());
		g_solids.push_back(guid);
	}
	static bool SolidPushRaw(uint8_t* pl, uint8_t* c) {
		__try {
			if (*(uint64_t*)(c + 0x198) != 0) return false;               // gezaehmt/Begleiter: keine Masse
			int64_t cx = *(int64_t*)(c + 0x10), cy = *(int64_t*)(c + 0x18), cz = *(int64_t*)(c + 0x20);
			float r = (std::max)(*(float*)(c + 0x80), *(float*)(c + 0x84)) * 0.5f, ht = *(float*)(c + 0x88);
			if (!(r > 0.05f && r < 80.0f && ht > 0.05f && ht < 120.0f)) return false;
			int64_t px = *(int64_t*)(pl + 0x10), py = *(int64_t*)(pl + 0x18), pz = *(int64_t*)(pl + 0x20);
			float dx = (float)(px - cx) / 65536.0f, dy = (float)(py - cy) / 65536.0f, dz = (float)(pz - cz) / 65536.0f;
			const float rp = 0.45f, hp = 1.8f;                            // Spieler: Radius, Hoehe
			float rr = r + rp; float d2 = dx * dx + dy * dy;
			if (d2 >= rr * rr || dz >= ht || dz + hp <= 0.0f) return false;
			float* vel = (float*)(pl + 0x34);
			if (dz > ht - 0.9f && vel[2] <= 0.5f && d2 < r * r) {         // von oben: auf dem Tier stehen
				*(int64_t*)(pl + 0x20) = cz + (int64_t)(ht * 65536.0f); vel[2] = 0.0f;
				return true;
			}
			float d = std::sqrt(d2);
			float nx = d > 0.001f ? dx / d : 1.0f, ny = d > 0.001f ? dy / d : 0.0f;
			*(int64_t*)(pl + 0x10) = cx + (int64_t)(nx * rr * 65536.0f);
			*(int64_t*)(pl + 0x18) = cy + (int64_t)(ny * rr * 65536.0f);
			float vn = vel[0] * nx + vel[1] * ny;                            // Geschwindigkeit in das Tier hinein streichen
			if (vn < 0.0f) { vel[0] -= vn * nx; vel[1] -= vn * ny; }
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void SolidTick(cube::GameController* gc) {
		if (!gc || !gc->local_player || g_solids.empty()) return;
		uint8_t* pl = (uint8_t*)gc->local_player;
		for (size_t i = 0; i < g_solids.size(); ) {
			uint8_t* c = FindCreatureRaw(gc, g_solids[i]);
			if (!c) { g_solids.erase(g_solids.begin() + i); continue; }
			if (c != pl) SolidPushRaw(pl, c);
			i++;
		}
	}
	// Name einer eigenen Rasse in die native Namenstabelle (std::map<int, wstring> World+0x800104) eintragen.
	typedef void* (__fastcall* MapIdx_t)(void* map, void* edx, const int* key);
	typedef void* (__fastcall* WAssignN_t)(void* str, void* edx, const wchar_t* s, size_t n);
	static bool RegisterRaceNameRaw(cube::GameController* gc, int race, const wchar_t* w, size_t n) {
		__try {
			void* map = (uint8_t*)gc + 0x2E4 + 0x800104;
			void* str = ((MapIdx_t)Re(0x4E2DF0))(map, nullptr, &race);
			if (!str) return false;
			((WAssignN_t)Re(0x40F7A0))(str, nullptr, w, n);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Rassenname lesen wie World_getObjectName 0x59AA60 (std::map<int, wstring>, Kopf bei World+0x800104; Knoten: Schluessel +0x10,
	// wstring +0x14: Puffer[8 wchar] bzw. Zeiger, Laenge +0x24, Kapazitaet +0x28). Leer = kein Eintrag -> leeres Namensschild.
	static bool LookupRaceNameRaw(cube::GameController* gc, int race, char* out, size_t cap) {
		out[0] = 0;
		__try {
			uint8_t* head = *(uint8_t**)((uint8_t*)gc + 0x2E4 + 0x800104); if (!head) return false;
			uint8_t* n = *(uint8_t**)(head + 4); int guard = 0;
			while (n && n != head && !n[0xD] && guard++ < 64) {
				int k = *(int*)(n + 0x10);
				if (k == race) {
					uint32_t len = *(uint32_t*)(n + 0x24), capw = *(uint32_t*)(n + 0x28);
					const wchar_t* w = capw >= 8 ? *(const wchar_t**)(n + 0x14) : (const wchar_t*)(n + 0x14);
					if (len > 60) len = 60;
					WideCharToMultiByte(CP_ACP, 0, w, (int)len, out, (int)cap - 1, NULL, NULL); out[(std::min)((size_t)len, cap - 1)] = 0;
					return true;
				}
				n = k < race ? *(uint8_t**)(n + 8) : *(uint8_t**)(n + 0);
			}
			return false;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	std::string RaceNameInGame(cube::GameController* gc, int race) { char b[64]; return LookupRaceNameRaw(gc, race, b, sizeof(b)) ? std::string(b) : std::string(); }
	void RegisterRaceName(cube::GameController* gc, int race, const std::string& name) {
		if (name.empty()) return;
		char cur[64]; if (LookupRaceNameRaw(gc, race, cur, sizeof(cur)) && name == cur) return;   // steht schon (Tabelle selbst pruefen, kein Merker)
		wchar_t w[64] = { 0 }; int n = MultiByteToWideChar(CP_ACP, 0, name.c_str(), -1, w, 63);
		if (n <= 1) return;
		RegisterRaceNameRaw(gc, race, w, (size_t)(n - 1));
	}
	uint64_t SpawnCustomAt(cube::GameController* gc, const CustomMob& m, int64_t x, int64_t y, int64_t z, std::string& report, uint64_t owner) {
		// 1. Teile vorbereiten (eigene .cub -> Registry)
		int ids[P_COUNT];
		std::ostringstream r;
		for (int i = 0; i < P_COUNT; i++) {
			const Part& p = m.parts[i];
			ids[i] = -2;                                           // -2 = Basis behalten
			if (p.source == SRC_NONE) ids[i] = -1;
			else if (p.source == SRC_NATIVE) ids[i] = p.nativeIndex;
			else if (p.source == SRC_FILE) {
				std::string err;
				int idx = InjectFile(gc, p.file, m.baseDir, err);
				if (idx < 0) { r << PartName(i) << ": " << err << " -- Teil der Basisrasse bleibt; "; ids[i] = -2; }   // nicht leer lassen
				else ids[i] = idx;
			}
		}
		// 2. Kreatur der Basisrasse
		// Friedliche eigene Tiere (Datei: Feindlichkeit 2/3) spawnen wie die NATIVEN friedlichen Tiere: Feindlichkeit 5
		// OHNE Besitzer (Chunk_generateObjects 0x5BA4F5: "mov dword [spawn+0x28], 5", Rasse aus der Tierliste).
		// CombatBehavior behandelt 5 ohne Besitzer als wildes Tier, das den Spieler nicht angreift, aber zum Fressen
		// kommt, wenn er das passende Futter (Item 0x14, Unterart = Rasse) hochhaelt -> Zaehmen wie nativ.
		// owner != 0: als Begleiter (Feindlichkeit 5, Besitzer, Begleiter-KI 0x5EE7A0) -- Totenbeschwoerung eigener Rassen
		const bool peaceful = m.peaceful && !owner;
		const int hosEff = owner ? 5 : (peaceful ? 1 : m.hostility);
		SpawnOpts o; o.race = m.baseRace; o.hostility = hosEff; o.level = m.level; o.peaceful = peaceful;
		o.equipment = m.equipment; o.wander = owner ? false : m.wander; o.sizeFactor = m.sizeFactor;
		int lvl = m.level > 0 ? m.level : PlayerLevel(gc);
		cube::Creature* c = SpawnOne(gc, m.baseRace, hosEff, lvl, x, y, z, o, nullptr, owner);
		if (!c) { report = "Spawn fehlgeschlagen"; return 0; }
		uint8_t* cre = (uint8_t*)c;
		// Werte der BASISRASSE merken, bevor eine Vorlage sie ueberschreibt: "von der Basisrasse" heisst dann
		// wirklich die gewaehlte Basis (frueher kam bei geladener Vorlage das Teil der Vorlage-Rasse).
		int16_t bId[P_COUNT] = {}; float bSc[9] = {}; float bPos[6][3] = {};
		bool haveBase = ReadCreature(cre, 0x8C, bId, sizeof(bId)) && ReadCreature(cre, 0x9C, bSc, sizeof(bSc)) && ReadCreature(cre, 0xDC, bPos, sizeof(bPos));
		// 3a. Natives Aussehen der Vorlage als Grundlage (Farben, Haare, Flags ...)
		if (m.useAppearance) WriteCreature(cre, 0x78, m.appearance, kAppearanceSize);
		// 3b. Ausruestung: gesetzte Plaetze ueberschreiben (Kategorie 0 = leer)
		int eq = 0;
		for (int i = 0; i < kEquipSlots; i++) {
			const EquipItem& e = m.equip[i];
			if (!e.set) continue;
			uint8_t it[kItemSize];
			if (e.hasRaw) memcpy(it, e.raw, kItemSize); else memset(it, 0, kItemSize);
			if (e.category == 0) memset(it, 0, kItemSize);
			else {
				it[0] = (uint8_t)e.category; it[1] = (uint8_t)e.id; it[0x0C] = (uint8_t)e.rarity; it[0x0D] = (uint8_t)e.material;
				uint16_t lv = (uint16_t)(e.level < 1 ? 1 : e.level); memcpy(it + 0x10, &lv, 2);
			}
			if (WriteCreature(cre, 0x300 + i * kItemSize, it, kItemSize)) eq++;
		}
		if (eq) r << eq << " Ausruestungsplaetze gesetzt. ";
		// Zustand vor den Teilen (Modelle/Skalen/Positionen, auf die sich eigene Teile beziehen)
		int16_t pId[P_COUNT] = {}; float pSc[9] = {}; float pPos[6][3] = {}; float pSize[3] = {};
		bool havePrev = ReadCreature(cre, 0x8C, pId, sizeof(pId)) && ReadCreature(cre, 0x9C, pSc, sizeof(pSc)) &&
		                ReadCreature(cre, 0xDC, pPos, sizeof(pPos)) && ReadCreature(cre, 0x80, pSize, 12);
		// 3c. Teile ueberschreiben
		for (int i = 0; i < P_COUNT; i++) {
			const Part& p = m.parts[i];
			if (p.source == SRC_BASE && haveBase && m.useAppearance) {          // Basisrasse statt Vorlage
				WriteCreature(cre, kIdOff[i], &bId[i], 2);
				bool sharedOk = i != P_HAIR || m.parts[P_HEAD].source == SRC_BASE;   // Haare teilen Skala/Position mit dem Kopf
				if (sharedOk && p.scale <= 0.0f) WriteCreature(cre, kScaleOff[i], &bSc[ScaleIdx(i)], 4);
				if (sharedOk && !p.setPos && PosIdx(i) >= 0) WriteCreature(cre, kPosOff[i], bPos[PosIdx(i)], 12);
			}
			if (ids[i] != -2) { int16_t v = (int16_t)ids[i]; WriteCreature(cre, kIdOff[i], &v, 2); }
			if (p.scale > 0.0f) WriteCreature(cre, kScaleOff[i], &p.scale, 4);
			if (p.setPos && kPosOff[i] >= 0) WriteCreature(cre, kPosOff[i], p.pos, 12);
		}
		// 3d. Auto-Ausrichtung: Ein eigener Koerper/Kopf mit anderer Hoehe als das Teil davor sitzt sonst falsch --
		// das Spiel zeichnet jedes Teil um seine Mitte, ein laengerer Koerper waechst also nach oben UND unten
		// in Kopf und Beine. Masseinheit aus der vorhandenen Anordnung abgeleitet (Kopf sitzt auf dem Koerper):
		// k = (Kopf.z - Koerper.z) / ((hKoerper*sKoerper + hKopf*sKopf) / 2). Dann Koerper um die halbe
		// Mehrhoehe hoch, Kopf und Haende um die ganze, Fuesse bleiben; Koerperhoehe (Kollision) waechst mit.
		// Nur fuer Teile ohne eigene Position.
		if (havePrev && m.autoAlign && (m.parts[P_BODY].source == SRC_FILE || m.parts[P_HEAD].source == SRC_FILE)) {
			int sx, sy, hbN = 0, hhN = 0, hbF = 0, hhF = 0, t;
			bool okB = ModelDims(gc, pId[P_BODY], &sx, &sy, &hbN), okH = ModelDims(gc, pId[P_HEAD], &sx, &sy, &hhN);
			int newB = m.parts[P_BODY].source == SRC_FILE ? ids[P_BODY] : pId[P_BODY];
			int newH = m.parts[P_HEAD].source == SRC_FILE ? ids[P_HEAD] : pId[P_HEAD];
			bool okB2 = ModelDims(gc, newB, &sx, &sy, &hbF), okH2 = ModelDims(gc, newH, &sx, &sy, &hhF);
			float sb = m.parts[P_BODY].scale > 0 ? m.parts[P_BODY].scale : pSc[ScaleIdx(P_BODY)];
			float sh = m.parts[P_HEAD].scale > 0 ? m.parts[P_HEAD].scale : pSc[ScaleIdx(P_HEAD)];
			float dzNative = pPos[PosIdx(P_HEAD)][2] - pPos[PosIdx(P_BODY)][2];
			float denom = (hbN * sb + hhN * sh) * 0.5f;
			(void)t;
			if (okB && okH && okB2 && okH2 && denom > 0.01f && (hbF != hbN || hhF != hhN)) {
				float k = dzNative / denom;
				if (k > 0.01f && k < 5.0f) {
					float dB = k * (hbF - hbN) * sb, dH = k * (hhF - hhN) * sh;
					float body[3], head[3], hands[3];
					memcpy(body, pPos[PosIdx(P_BODY)], 12); memcpy(head, pPos[PosIdx(P_HEAD)], 12); memcpy(hands, pPos[PosIdx(P_HANDS)], 12);
					body[2] += dB * 0.5f; head[2] += dB + dH * 0.5f; hands[2] += dB;
					if (!m.parts[P_BODY].setPos) WriteCreature(cre, kPosOff[P_BODY], body, 12);
					if (!m.parts[P_HEAD].setPos) WriteCreature(cre, kPosOff[P_HEAD], head, 12);
					if (!m.parts[P_HANDS].setPos) WriteCreature(cre, kPosOff[P_HANDS], hands, 12);
					// Kollisionshoehe (+0x88) bleibt: das Spiel haelt Darstellung (+0x80) zu Hoehe (+0x88) immer 1 : 2.25
					// (Aussehensgenerator 0x43F7C0); nur +0x88 zu aendern hob den Mob vom Boden ab (~1/3 Block).
					float ratio = (hbF * sb + hhF * sh) / (hbN * sb + hhN * sh);
					char b[220];
					sprintf_s(b, "Auto-Ausrichtung: Koerper %d->%d, Kopf %d->%d Voxel, k=%.3f, Koerper z %+.2f, Kopf z %+.2f, Haende z %+.2f (Gesamthoehe x%.2f, Kollision unveraendert). ",
					          hbN, hbF, hhN, hhF, k, dB * 0.5f, dB + dH * 0.5f, dB, ratio);
					r << b;
				} else r << "Auto-Ausrichtung uebersprungen (Masseinheit k=" << k << " unplausibel). ";
			}
		}
		if (m.size > 0.0f) WriteCreature(cre, 0x80, &m.size, 4);      // Aussehen +0x08: Darstellungsgroesse
		// 4a. Fliegen: Aussehensflag 0x2 (Kreatur+0x7E; creature_generateAppearance 0x43F7C0 setzt es fuer Kraehe,
		//     Moewe, Papagei, Fledermaus, Fliege, Muecke, Drache ...). Nativ fliegen diese nur im Kampf auf
		//     (CombatBehavior: Flag 0x2 + 0x10) -- FlightTick haelt eigene Flieger dauerhaft in der Luft.
		{
			uint16_t af = 0; ReadCreature(cre, 0x7E, &af, 2);
			bool flies = m.fly == 1 || (m.fly < 0 && (af & 2) != 0);
			if (flies) { af |= 2; WriteCreature(cre, 0x7E, &af, 2); uint64_t gg = 0; ReadCreature(cre, 0x08, &gg, 8); RegisterFlyer(gg, x, y); }
			if (m.swim == 1) {                                          // Wassertier wie die nativen Fische (Aussehen 0x96 Hai:
				// Flags 0x131; 0x100 = Wassertier: alles ausser Wasser zaehlt als fest, GameController.cpp:100948) --
				// kein Flugflag, keine Positionssteuerung; die native RandomWalk-KI laesst es im Wasser schwimmen.
				af = (uint16_t)((af & ~2) | 0x100); if (peaceful) af = PeaceFlags(af); WriteCreature(cre, 0x7E, &af, 2);
				uint64_t gg = 0; ReadCreature(cre, 0x08, &gg, 8);
				RegisterSwimmer(gg, x, y, (std::max)(0.0f, (std::min)(1.0f, m.swimDepth)), m.hostility == 1);
			}
		}
		if (m.ownRace >= 200) ApplyBodyRaw(m.ownRace, cre + 0x78);          // Koerperbau der eigenen Rasse (LAB > Mobs)
		// 4b. Masse fuer eigene Tiere (Begleiter ausgenommen)
		if (m.ownRace >= 200 && hosEff != 5) { uint64_t gg = 0; ReadCreature(cre, 0x08, &gg, 8); RegisterSolid(gg); }
		// 4. eigene Rasse: Name eintragen, Kennung setzen
		// Name wie nativ (Namensschild 0x49A887..0x49A929): Ist das Namensfeld (+0x1168) LEER, zeigt das Spiel bei
		// Tieren den Rassennamen aus der Rassen-Namenstabelle (World+0x800104) -- so wie bei jedem nativen Tier.
		// Ein gesetztes Namensfeld erscheint dagegen als Eigenname (wie bei Spielern/Haustieren). Also: Namensfeld leer
		// lassen, eigene Rassenkennung setzen und den Namen in der Rassen-Tabelle eintragen.
		if (m.ownRace >= 200 && m.setRace) {
			std::string nm = m.raceName.empty() ? m.name : m.raceName;
			RegisterRaceName(gc, m.ownRace, nm);
			int rr = m.ownRace; WriteCreature(cre, 0x64, &rr, 4);
			char z16[16] = {}; WriteCreature(cre, 0x1168, z16, 16);
			r << "Eigene Rasse " << m.ownRace << " (" << nm << "). ";
		}
		(void)peaceful;
		r << "Eigener Mob '" << m.name << "' auf Basis " << RaceName(m.baseRace) << " gespawnt.";
		report = r.str();
		uint64_t guid = 0; ReadCreature(cre, 0x08, &guid, 8);
		return guid ? guid : 1;
	}

	int SpawnCustom(cube::GameController* gc, const CustomMob& m, int distance, std::string& report) {
		int64_t px, py, pz; int plevel = 1;
		if (!ReadPlayer(gc, &px, &py, &pz, &plevel)) { report = "kein Spieler"; return 0; }
		const int64_t U = 65536;
		uint64_t g = SpawnCustomAt(gc, m, px + (int64_t)distance * U, py, pz + U, report);
		if (g) std::cout << "[Mob] " << report << std::endl;
		return g ? 1 : 0;
	}

	uint64_t SpawnRaceAt(cube::GameController* gc, int race, int hostility, int level, int64_t x, int64_t y, int64_t z,
	                     bool equipment, bool wander, float sizeFactor) {
		if (race < 0 || race >= kRaceCount) return 0;
		SpawnOpts o; o.race = race; o.equipment = equipment; o.wander = wander; o.sizeFactor = sizeFactor;
		int hos = hostility >= 0 ? hostility : DefaultHostility(race);
		int lvl = level > 0 ? level : PlayerLevel(gc);
		cube::Creature* c = SpawnOne(gc, race, hos, lvl, x, y, z, o, nullptr);
		if (!c) return 0;
		uint64_t guid = 0; ReadCreature((uint8_t*)c, 0x08, &guid, 8);
		return guid ? guid : 1;
	}

	// Eigene Rasse (>= 200) als Begleiter: die .mob mit eigene_rasse=<race> suchen (Strukturen\Mobs, Unterordner), laden, spawnen.
	static void FindMobFiles(const std::string& dir, int depth, int race, std::string& out) {
		if (depth > 4 || !out.empty()) return;
		WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (hf == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) FindMobFiles(p, depth + 1, race, out); continue; }
			if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".mob")) continue;
			std::ifstream in(p); std::string line;
			while (std::getline(in, line)) if (line.rfind("eigene_rasse=", 0) == 0) { if (atoi(line.c_str() + 13) == race) out = p; break; }
		} while (out.empty() && FindNextFileA(hf, &fd));
		FindClose(hf);
	}
	uint64_t SpawnOwnCompanionAt(cube::GameController* gc, int race, int level, int64_t x, int64_t y, int64_t z, uint64_t owner) {
		static std::map<int, std::string> cache;
		if (race < 200 || !owner) return 0;
		std::string path = cache[race];
		if (path.empty()) { FindMobFiles(MobsDir(), 0, race, path); cache[race] = path; }
		if (path.empty()) return 0;
		CustomMob m; std::string err;
		if (!LoadMob(path, m, err)) return 0;
		if (level > 0) m.level = level;
		std::string rep;
		return SpawnCustomAt(gc, m, x, y, z, rep, owner);
	}
	uint64_t SpawnCompanionAt(cube::GameController* gc, int race, int level, int64_t x, int64_t y, int64_t z, uint64_t owner, bool equipment) {
		if (race < 0 || race >= kRaceCount || !owner) return 0;
		SpawnOpts o; o.race = race; o.equipment = equipment; o.wander = false;
		cube::Creature* c = SpawnOne(gc, race, 5, level > 0 ? level : PlayerLevel(gc), x, y, z, o, nullptr, owner);
		if (!c) return 0;
		uint64_t guid = 0; ReadCreature((uint8_t*)c, 0x08, &guid, 8);
		return guid ? guid : 1;
	}

	// Kreaturen in der Naehe (Entity-Map in-order, hoechstens 8192 Knoten). Nur Spielthread.
	int ListCreaturesNear(cube::GameController* gc, int64_t bx, int64_t by, int64_t bz, int rXY, int rZ, CreatureInfo* out, int maxOut) {
		if (!gc || !out || maxOut <= 0) return 0;
		int n = 0;
		__try {
			uint8_t* head = *(uint8_t**)((uint8_t*)&gc->world + 4);
			if (!head) return 0;
			uint8_t* stack[96]; int sp = 0, visited = 0;
			uint8_t* nd = *(uint8_t**)(head + 4);
			while ((nd && !nd[0xD]) || sp > 0) {
				while (nd && !nd[0xD]) { if (sp >= 96) return n; stack[sp++] = nd; nd = *(uint8_t**)(nd + 0); }
				nd = stack[--sp];
				if (++visited > 8192) break;
				uint8_t* c = *(uint8_t**)(nd + 0x18);
				if (c) {
					int64_t x = *(int64_t*)(c + 0x10) >> 16, y = *(int64_t*)(c + 0x18) >> 16, z = *(int64_t*)(c + 0x20) >> 16;
					if (x - bx <= rXY && bx - x <= rXY && y - by <= rXY && by - y <= rXY && z - bz <= rZ && bz - z <= rZ) {
						CreatureInfo& i = out[n++];
						i.guid = *(uint64_t*)(nd + 0x10); i.race = *(int*)(c + 0x64); i.hostility = c[0x60]; i.cls = c[0x140];
						i.level = *(int*)(c + 0x190); i.hp = *(float*)(c + 0x16C); i.x = x; i.y = y; i.z = z;
						i.sx = *(float*)(c + 0x80); i.sz = *(float*)(c + 0x88);
						if (n >= maxOut) return n;
					}
				}
				nd = *(uint8_t**)(nd + 8);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}

	int PlayerLevel(cube::GameController* gc) {
		int64_t px, py, pz; int plevel = 1;
		if (!ReadPlayer(gc, &px, &py, &pz, &plevel)) return 1;
		return plevel > 0 ? plevel : 1;
	}

	static bool ReadPos(uint8_t* c, int64_t* x, int64_t* y, int64_t* z) {
		__try { *x = *(int64_t*)(c + 0x10); *y = *(int64_t*)(c + 0x18); *z = *(int64_t*)(c + 0x20); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool CreaturePos(cube::GameController* gc, uint64_t guid, int64_t* x, int64_t* y, int64_t* z) {
		if (!gc || guid <= 1) return false;
		uint8_t* c = FindCreatureRaw(gc, guid);
		return c && ReadPos(c, x, y, z);
	}

	// Alle Kreaturen der EntitesMap (in-order, expliziter Stapel, hoechstens 8192 Knoten) nach einer passenden
	// Kreatur in der Naehe von (bx,by,bz) [Bloecke] durchsuchen: |dx|,|dy| <= rXY, |dz| <= rZ, optional Klasse
	// (+0x140, -1 = egal) und Feindlichkeit (+0x60, -1 = egal). Liefert die Anzahl Treffer; guids[0..maxGuids) = ihre
	// Schluessel (GUIDs). Damit erkennen Taxi und Haendler einen schon stehenden NPC, auch wenn er unter seiner
	// alten GUID nicht mehr gefunden wird -- vorher wurde dann jedes Mal ein weiterer gesetzt.
	int CountCreaturesNear(cube::GameController* gc, int64_t bx, int64_t by, int64_t bz, int rXY, int rZ, int wantClass, int wantHostility, uint64_t* guids, int maxGuids) {
		if (!gc) return 0;
		int hits = 0;
		__try {
			uint8_t* head = *(uint8_t**)((uint8_t*)&gc->world + 4);
			if (!head) return 0;
			uint8_t* stack[96]; int sp = 0, visited = 0;
			uint8_t* n = *(uint8_t**)(head + 4);
			while ((n && !n[0xD]) || sp > 0) {
				while (n && !n[0xD]) { if (sp >= 96) return hits; stack[sp++] = n; n = *(uint8_t**)(n + 0); }
				n = stack[--sp];
				if (++visited > 8192) break;
				uint8_t* c = *(uint8_t**)(n + 0x18);
				if (c) {
					int64_t x = *(int64_t*)(c + 0x10) >> 16, y = *(int64_t*)(c + 0x18) >> 16, z = *(int64_t*)(c + 0x20) >> 16;
					if (x - bx <= rXY && bx - x <= rXY && y - by <= rXY && by - y <= rXY && z - bz <= rZ && bz - z <= rZ
						&& (wantClass < 0 || c[0x140] == (uint8_t)wantClass) && (wantHostility < 0 || c[0x60] == (uint8_t)wantHostility)) {
						if (guids && hits < maxGuids) guids[hits] = *(uint64_t*)(n + 0x10);
						hits++;
					}
				}
				n = *(uint8_t**)(n + 8);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return hits;
	}

	cube::Creature* CreatureByGuid(cube::GameController* gc, uint64_t guid) {
		if (!gc || guid <= 1) return nullptr;
		return (cube::Creature*)FindCreatureRaw(gc, guid);
	}

	// Kreatur per GUID sicher wegraeumen (tief unter die Karte) -- NUR wenn sie noch in der EntitesMap steht.
	bool SinkCreature(cube::GameController* gc, uint64_t guid) {
		if (!gc || guid <= 1) return false;
		uint8_t* c = FindCreatureRaw(gc, guid);
		if (!c) return false;
		__try { *(int64_t*)(c + 0x20) -= (int64_t)100000 * 65536; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	std::string DescribeLast(cube::GameController* gc) {
		if (!g_lastGuid) return "noch nichts gespawnt";
		uint8_t* g_last = FindCreatureRaw(gc, g_lastGuid);
		if (!g_last) return "der zuletzt gespawnte Mob existiert nicht mehr (tot oder entladen)";
		std::ostringstream o;
		int16_t id[P_COUNT]; float sc[9]; float pos[6][3]; int race = 0; uint8_t cls = 0, hos = 0; int lvl = 0; float size = 0;
		if (!ReadCreature(g_last, 0x8C, id, sizeof(id)) || !ReadCreature(g_last, 0x9C, sc, sizeof(sc)) ||
		    !ReadCreature(g_last, 0xDC, pos, sizeof(pos)) || !ReadCreature(g_last, 0x64, &race, 4) ||
		    !ReadCreature(g_last, 0x140, &cls, 1) || !ReadCreature(g_last, 0x60, &hos, 1) ||
		    !ReadCreature(g_last, 0x190, &lvl, 4) || !ReadCreature(g_last, 0x80, &size, 4))
			return "Kreatur nicht mehr lesbar";
		char b[160];
		sprintf_s(b, "Letzter Mob: Rasse %d %s, Feindlichkeit %d, Klasse 0x%X, Level %d, Groesse %.2f\r\n", race, RaceName(race), hos, cls, lvl, size);
		o << b;
		static const char* sn[9] = { "Kopf", "Koerper", "Haende", "Fuesse", "?", "Waffe", "Schwanz", "Schulter", "Fluegel" };
		for (int i = 0; i < P_COUNT; i++) { sprintf_s(b, "  %-9s ID %d%s\r\n", PartName(i), id[i], id[i] < 0 ? " (keins)" : ""); o << b; }
		for (int i = 0; i < 9; i++) { sprintf_s(b, "  Skala %-9s %.2f\r\n", sn[i], sc[i]); o << b; }
		static const char* pn[6] = { "Koerper", "Kopf", "Haende", "Fuesse", "?", "Fluegel" };
		for (int i = 0; i < 6; i++) { sprintf_s(b, "  Position %-8s %.2f %.2f %.2f\r\n", pn[i], pos[i][0], pos[i][1], pos[i][2]); o << b; }
		return o.str();
	}

	// =====================================================================
	// 6. Mob-Dateien  (Strukturen\Mobs\<name>.mob, Text)
	// =====================================================================
	std::string MobsDir() { return Lab::StructuresDir() + "\\Mobs"; }

	// Teil-Pfad relativ zum Ordner der .mob schreiben, wenn die Datei darunter liegt (so bleibt der Ordner
	// samt Modellen verschiebbar).
	static std::string RelTo(const std::string& file, const std::string& dir) {
		char a[MAX_PATH] = "", b[MAX_PATH] = "";
		if (file.empty() || !GetFullPathNameA(file.c_str(), MAX_PATH, a, NULL) || !GetFullPathNameA(dir.c_str(), MAX_PATH, b, NULL)) return file;
		std::string fa = a, db = b; if (!db.empty() && db.back() != '\\') db += '\\';
		if (fa.size() > db.size() && _strnicmp(fa.c_str(), db.c_str(), db.size()) == 0) return fa.substr(db.size());
		return file;
	}
	bool SaveMob(const std::string& path, const CustomMob& m, std::string& err) {
		Lab::EnsureDir(MobsDir());
		std::ofstream f(path);
		if (!f) { err = "nicht schreibbar: " + path; return false; }
		f << "# Eigener Mob (LAB). teil=Nr;Quelle;Index;Datei;Skala;Pos(0/1);x;y;z\n";
		f << "# Quelle: 0 = von der Basisrasse, 1 = keins, 2 = nativer Registry-Index, 3 = eigene .cub\n";
		f << "name=" << m.name << "\nbasis=" << m.baseRace << "\ngroesse=" << m.size << "\nfaktor=" << m.sizeFactor << "\nfeindlichkeit=" << m.hostility
				  << "\nlevel=" << m.level << "\nausruestung=" << (m.equipment ? 1 : 0) << "\nwandern=" << (m.wander ? 1 : 0) << "\n";
				if (m.fly >= 0) f << "# fliegt: 1 = immer, 0 = nie (ohne Zeile: automatisch nach Basisrasse)\nfliegt=" << m.fly << "\n";
				if (!m.autoAlign) f << "auto_ausrichtung=0\n";
				if (m.peaceful) f << "# friedlich: wie natives Huhn (Feindlichkeit 1 + Aussehensflags 0x02|0x10)\nfriedlich=1\n";
				if (m.swim) f << "# schwimmt: 1 = Wassertier; schwimmtiefe 0 = Grund .. 1 = Oberflaeche\nschwimmt=1\nschwimmtiefe=" << m.swimDepth << "\n";
				if (m.ownRace >= 200) f << "# eigene Rasse: Kennung (>= 200) und Name im Spiel\neigene_rasse=" << m.ownRace << "\nrassen_name=" << m.raceName << "\nrasse_setzen=" << (m.setRace ? 1 : 0) << "\n";
		auto hex = [](const uint8_t* d, int n) { std::string s; char b[3]; for (int i = 0; i < n; i++) { sprintf_s(b, "%02X", d[i]); s += b; } return s; };
		if (m.useAppearance) f << "aussehen=" << hex(m.appearance, kAppearanceSize) << "\n";
		for (int i = 0; i < kEquipSlots; i++) {
			const EquipItem& e = m.equip[i];
			if (!e.set) continue;
			f << "ausruestung=" << i << ";" << e.category << ";" << e.id << ";" << e.material << ";" << e.rarity << ";" << e.level << ";"
			  << (e.hasRaw ? hex(e.raw, kItemSize) : std::string("-")) << "\n";
		}
		for (int i = 0; i < P_COUNT; i++) {
			const Part& p = m.parts[i];
			std::string pf = p.source == SRC_FILE ? RelTo(ResolvePartPath(p.file, m.baseDir), DirOf(path)) : p.file;
			f << "teil=" << i << ";" << p.source << ";" << p.nativeIndex << ";" << pf << ";" << p.scale << ";"
			  << (p.setPos ? 1 : 0) << ";" << p.pos[0] << ";" << p.pos[1] << ";" << p.pos[2] << "\n";
		}
		return (bool)f;
	}

	static void FixHostility(CustomMob& m) {           // alte Dateien: 2/3/4 bei eigenen Tieren = friedlich (1 + Flag 0x10)
		if (m.ownRace >= 200 && (m.hostility == 2 || m.hostility == 3 || m.hostility == 4)) { m.hostility = 1; m.peaceful = true; }
	}
	bool LoadMob(const std::string& path, CustomMob& m, std::string& err) {
		std::ifstream f(path);
		if (!f) { err = "nicht lesbar: " + path; return false; }
		m = EmptyMob();
		m.baseDir = DirOf(path);                                   // Modell-Ordner liegen neben der .mob
		std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find('='); if (eq == std::string::npos) continue;
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "name") m.name = v;
			else if (k == "basis") m.baseRace = atoi(v.c_str());
			else if (k == "groesse") m.size = (float)atof(v.c_str());
			else if (k == "faktor") m.sizeFactor = (float)atof(v.c_str());
			else if (k == "feindlichkeit") m.hostility = atoi(v.c_str());
			else if (k == "level") m.level = atoi(v.c_str());
			else if (k == "ausruestung") m.equipment = atoi(v.c_str()) != 0;
			else if (k == "wandern") m.wander = atoi(v.c_str()) != 0;
			else if (k == "eigene_rasse") m.ownRace = atoi(v.c_str());
			else if (k == "rassen_name") m.raceName = v;
			else if (k == "rasse_setzen") m.setRace = atoi(v.c_str()) != 0;
			else if (k == "fliegt") m.fly = atoi(v.c_str());
			else if (k == "schwimmt") m.swim = atoi(v.c_str());
			else if (k == "friedlich") m.peaceful = atoi(v.c_str()) != 0;
			else if (k == "auto_ausrichtung") m.autoAlign = atoi(v.c_str()) != 0;
			else if (k == "schwimmtiefe") m.swimDepth = (float)atof(v.c_str());
			else if (k == "aussehen" && v.size() >= (size_t)kAppearanceSize * 2) {
				for (int i = 0; i < kAppearanceSize; i++) m.appearance[i] = (uint8_t)strtoul(v.substr(i * 2, 2).c_str(), nullptr, 16);
				m.useAppearance = true;
			}
			else if (k == "ausruestung") {
				std::vector<std::string> t; std::string cur;
				for (char ch : v) { if (ch == ';') { t.push_back(cur); cur.clear(); } else cur += ch; }
				t.push_back(cur);
				if (t.size() < 7) continue;
				int i = atoi(t[0].c_str()); if (i < 0 || i >= kEquipSlots) continue;
				EquipItem& e = m.equip[i];
				e.set = true; e.category = atoi(t[1].c_str()); e.id = atoi(t[2].c_str()); e.material = atoi(t[3].c_str());
				e.rarity = atoi(t[4].c_str()); e.level = atoi(t[5].c_str());
				e.hasRaw = t[6].size() >= (size_t)kItemSize * 2;
				if (e.hasRaw) for (int j = 0; j < kItemSize; j++) e.raw[j] = (uint8_t)strtoul(t[6].substr(j * 2, 2).c_str(), nullptr, 16);
			}
			else if (k == "teil") {
				std::vector<std::string> t; std::string cur;
				for (char ch : v) { if (ch == ';') { t.push_back(cur); cur.clear(); } else cur += ch; }
				t.push_back(cur);
				if (t.size() < 9) continue;
				int i = atoi(t[0].c_str()); if (i < 0 || i >= P_COUNT) continue;
				Part& p = m.parts[i];
				p.source = atoi(t[1].c_str()); p.nativeIndex = (int)strtol(t[2].c_str(), nullptr, 0); p.file = t[3];
				p.scale = (float)atof(t[4].c_str()); p.setPos = atoi(t[5].c_str()) != 0;
				p.pos[0] = (float)atof(t[6].c_str()); p.pos[1] = (float)atof(t[7].c_str()); p.pos[2] = (float)atof(t[8].c_str());
			}
		}
		FixHostility(m);
		return true;
	}
}
