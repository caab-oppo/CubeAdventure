#pragma once
//
// MobLab.h
// ---------------------------------------------------------------
// Mob-Labor: jede Kreatur des Spiels spawnen -- mit den Flags und der
// Ausruestung, die das Spiel selbst vergibt -- und eigene Mobs aus .cub-
// Dateien zusammensetzen (Kopf, Haare, Haende, Fuesse, Koerper, Schwanz,
// Schulter, Fluegel).
//
// Alle Adressen und Konventionen sind gegen die Cube.exe geprueft
// (capstone, Aufrufstellen); Herleitung in RE_Mobs.md.
//
//   Rasse        0..155, Namen = cwsdk-Enum, gegen die Kopfmodelle des
//                Aussehensgenerators @0x43F7C0 abgeglichen (95 = Santa).
//   Aussehen     creature_generateAppearance @0x43F7C0, __cdecl(&rasse, &aussehen, 0)
//   Ausruestung  "Ausruester" @0x5C7730 (im Reversal faelschlich
//                creature_initBehaviorByType): arbeitet auf einem cube::Spawn
//                (0x10F0), wuerfelt die Klasse, setzt Waffen/Ruestung je Rasse.
//                Kopiert wie die native Umwandlung @0x61005E:
//                Spawn+0x120 -> Kreatur+0x300 (13 Plaetze a 0x118, @0x44AF00).
//   Teile        Aussehen-IDs = Registry-Indizes von .cub-Modellen
//                (Kreatur+0x8C Kopf ... +0x9A Fluegel); gezeichnet generisch
//                von Creature_drawModel @0x4128F0 -- auch Fluegel.
//
#include "cwsdk.h"
#include "LabStructure.h"
#include <string>
#include <vector>

namespace MobLab {

	// ---- Rassen -----------------------------------------------------------
	const int kRaceCount = 156;
	const char* RaceName(int race);            // "Bat", "Dragon", ...
	const char* RaceKind(int race);            // "Volk", "Tier", "Monster", "Pflanze", "Vorkommen", "Objekt", "Wasser"
	int  DefaultHostility(int race);           // 6 fuer Pflanzen/Vorkommen/Objekte, sonst 1 (wie im Spielcode)

	// ---- Spawnen ------------------------------------------------------------
	struct SpawnOpts {
		int  race = 0;
		int  count = 1;
		int  level = -1;          // -1 = Level des Spielers
		int  hostility = -1;      // -1 = Standard der Rasse; sonst 1/5/6
		bool peaceful = false;    // friedlich wie die nativen Tiere (Huhn): Feindlichkeit 1 + Aussehensflag 0x10
		int  charClass = -1;      // -1 = Spiel entscheidet; 1..4 Kampfklasse; 0x80.. Beruf
		bool equipment = true;    // native Ausruestung (Ausruester @0x5C7730)
		bool wander = true;       // RandomWalk um den Spawnpunkt
		int  distance = 6;        // Bloecke in +X vor dem Spieler
		float sizeFactor = 1.0f;  // Groesse x: Darstellung, Hitbox und Koerper (Aussehen +0x08/+0x0C/+0x10)
	};
	// NUR auf dem Spielthread (LabBridge::Post).
	int  Spawn(cube::GameController* gc, const SpawnOpts& o, std::string& report);
	// Galerie: 'count' Rassen ab 'firstRace' nebeneinander, 4 Bloecke Abstand.
	int  SpawnGallery(cube::GameController* gc, int firstRace, int count, const SpawnOpts& base, std::string& report);

	// ---- Eigener Mob ----------------------------------------------------------
	enum PartSlot { P_HEAD, P_HAIR, P_HANDS, P_FEET, P_BODY, P_TAIL, P_SHOULDER, P_WINGS, P_COUNT };
	const char* PartName(int slot);
	bool PartHasPosition(int slot);

	enum SourceKind { SRC_BASE = 0, SRC_NONE, SRC_NATIVE, SRC_FILE };

	// ---- Ausruestung (13 Plaetze a 0x118 ab Kreatur+0x300) -------------------
	// Reihenfolge = cwsdk WornEquipment; geprueft an drei Stellen des Ausruesters
	// (Spawn+0x350 -> Brust, +0x7B0 -> Waffe links, +0x8C8 -> Waffe rechts).
	const int kEquipSlots = 13;
	const int kItemSize = 0x118;
	const int kAppearanceSize = 0xAC;          // Kreatur+0x78 .. +0x123
	const char* EquipSlotName(int i);
	struct EquipItem {
		bool set = false;                       // false = so lassen (Ausruester/Basis)
		int  category = 0, id = 0, material = 0, rarity = 0, level = 1;   // Item +0x00 +0x01 +0x0D +0x0C +0x10
		bool hasRaw = false;
		uint8_t raw[kItemSize] = {};           // komplettes natives Item (Modifikator, Anpassungen ...)
	};
	struct Part {
		int  source = SRC_BASE;   // von der Basisrasse / keins / nativer Index / eigene .cub
		int  nativeIndex = -1;
		std::string file;         // Pfad zur .cub
		float scale = 0.0f;       // 0 = von der Basisrasse
		bool  setPos = false;
		float pos[3] = { 0, 0, 0 };
	};
	struct CustomMob {
		std::string name = "neuer-mob";
		int   baseRace = 2;       // HumanMale: humanoides Skelett
		float size = 0.0f;        // (alt) absolute Darstellungsgroesse, 0 = Basis
		float sizeFactor = 1.0f;  // Groesse x der Basis -- Darstellung, Hitbox, Koerper
		Part  parts[P_COUNT];
		int   hostility = 1;
		int   level = -1;
		bool  equipment = false;
		bool  wander = true;
		// Eigene Rasse (2026-10-13): Kennung >= 200 und Name im Spiel. Gespawnt wird als Basisrasse (Skelett, Bewegung,
		// Aussehensgenerator), danach Kreatur+0x64 = eigene Kennung (rasse_setzen). Der Name kommt in die Namenstabelle
		// der Rassen (std::map<int, wstring> bei World+0x800104, World-ctor 0x58EC26; operator[] 0x4E2DF0).
		int   ownRace = -1;
		std::string raceName;
		bool  setRace = true;
		// Fliegen (2026-10-13): -1 = automatisch (Aussehensflag 0x2 der Basisrasse, z. B. Kraehe, Fledermaus, Fliege,
		// Drache), 0 = nie, 1 = immer. Fliegende eigene Mobs haelt FlightTick in 5..14 Bloecken ueber dem Boden.
		int   fly = -1;
		// Schwimmen (2026-10-14): 1 = Wassertier. SwimTick haelt es im Wasser (Hoehe zwischen Grund und Oberflaeche:
		// swimDepth 0 = am Grund, 1 = an der Oberflaeche), es zieht umher und dreht an Ufern um.
		int   swim = 0;
		bool  peaceful = false;   // friedlich=1: Feindlichkeit 1 + Aussehensflag 0x10 (wie natives Huhn, gemessen 2026-10-16)
		bool  autoAlign = true;   // auto_ausrichtung=0: Teile mit symmetrischem Rand nicht verschieben (Tierwelt)
		float swimDepth = 0.5f;
		// Natives Aussehen als Grundlage (aus "Native Rasse laden"): Farben, Haare,
		// Groessen, Flags -- alles, was der Aussehensgenerator setzt.
		bool    useAppearance = false;
		uint8_t appearance[kAppearanceSize] = {};
		EquipItem equip[kEquipSlots];
		std::string baseDir;      // Ordner der geladenen .mob: Teile werden relativ dazu gesucht (LoadMob setzt ihn)
	};

	// ---- Native Vorlage: Aussehen + Ausruestung einer Rasse ------------------
	// Genau wie das Spiel: Spawn anlegen, Aussehen erzeugen (@0x43F7C0), DANN
	// Ausruester (@0x5C7730) -- der liest die Aussehensflags (z.B. 0x18 =
	// unbewaffnet beim Drachen).
	struct NativeTemplate {
		bool ok = false;
		int  race = 0, cls = 0;
		uint8_t appearance[kAppearanceSize] = {};
		uint8_t equipment[kEquipSlots * kItemSize] = {};
	};
	void RequestTemplate(cube::GameController* gc, int race, int wantClass);   // Spielthread
	bool TakeTemplate(NativeTemplate& out);                                     // Fensterthread
	void ApplyTemplate(const NativeTemplate& t, CustomMob& m);                   // Fensterthread
	CustomMob EmptyMob();         // alle Teile "keins" -- nur was du einsetzt, ist zu sehen

	bool SaveMob(const std::string& path, const CustomMob& m, std::string& err);
	bool LoadMob(const std::string& path, CustomMob& m, std::string& err);
	std::string MobsDir();        // "Strukturen\\Mobs"

	// NUR Spielthread. Setzt eigene .cub-Teile als echte Spielmodelle in freie
	// Registry-Plaetze (cube::Sprite mit Mesh) und spawnt den Mob.
	int  SpawnCustom(cube::GameController* gc, const CustomMob& m, int distance, std::string& report);

	// ---- Fuer andere Bausteine (BiomeMod) -- NUR Spielthread ------------------
	// Positionen in Festkomma (Block * 65536). Rueckgabe: GUID (0 = fehlgeschlagen).
	uint64_t SpawnRaceAt(cube::GameController* gc, int race, int hostility, int level,
	                     int64_t x, int64_t y, int64_t z, bool equipment, bool wander, float sizeFactor);
	uint64_t SpawnCustomAt(cube::GameController* gc, const CustomMob& m, int64_t x, int64_t y, int64_t z, std::string& report, uint64_t owner = 0);
	uint64_t SpawnOwnCompanionAt(cube::GameController* gc, int ownRace, int level, int64_t x, int64_t y, int64_t z, uint64_t owner);   // eigene Rasse als Begleiter
	// Lebt die Kreatur noch (Entity-Map)? Dann Position in Festkomma.
	bool CreaturePos(cube::GameController* gc, uint64_t guid, int64_t* x, int64_t* y, int64_t* z);
	int  CountCreaturesNear(cube::GameController* gc, int64_t bx, int64_t by, int64_t bz, int rXY, int rZ, int wantClass, int wantHostility, uint64_t* guids, int maxGuids);
	cube::Creature* CreatureByGuid(cube::GameController* gc, uint64_t guid);   // nullptr, wenn nicht (mehr) in der EntitesMap
	bool SinkCreature(cube::GameController* gc, uint64_t guid);   // nur wenn per GUID noch in der EntitesMap
	int  PlayerLevel(cube::GameController* gc);
	// Spielthread: fliegende eigene Mobs in der Luft halten (je Bild aufrufen)
	void FlightTick(cube::GameController* gc);
	void SwimTick(cube::GameController* gc);   // Wassertiere (schwimmt=1) im Wasser halten
	void SolidTick(cube::GameController* gc);
	void ModCreatureTick(cube::GameController* gc);
	bool PassiveOf(uint16_t flags, int race);     // friedlich? (creature_isPassive 0x444680 mit Mod-Marken; jeder Thread)
	bool SetPeaceful(cube::GameController* gc, uint64_t guid);   // vorhandene Kreatur friedlich machen (Feindlichkeit 1 + Flag 0x10) // Mod-Kreaturen: Anzeigeposition +0x1350 nachfuehren (Namensschilder)
	void PetSkinTick(cube::GameController* gc);
	std::string InspectNearby(cube::GameController* gc, int radius);   // alle Kreaturen im Umkreis: Verhalten, Flags, Baum (LAB/Log) // gezaehmte eigene Tiere: Aussehen wieder einsetzen  // eigene Tiere haben Masse: Spieler wird aus ihrer Huelle geschoben, kann auf grossen stehen
	int  SwimmerCount();
	int  FlyerCount();
	// Name einer eigenen Rasse (>= 200) in die native Namenstabelle eintragen (Spielthread)
	void RegisterRaceName(cube::GameController* gc, int race, const std::string& name);
	void PrepareMobParts(cube::GameController* gc, const CustomMob& m, int ids[8]);   // Spielthread: Teile in die Registry
	void ApplyToSpawn(uint8_t* spawn, const CustomMob& m, const int ids[8], int ownRace); // nativen Spawn-Datensatz umbauen
	uint8_t* NewSpawnNative(int race, int hostility, int level);   // cube::Spawn wie das Spiel (ctor + Aussehen + Ausruester)
	uint8_t* NewSpawnBare();                                       // nur ctor (Vase)
	// Biome II: Vorlage einer nativen Rasse (Spielthread erzeugt sie wie das Spiel; jeder Thread kann sie in einen
	// nativen Spawn-Datensatz kopieren -- nur POD: Aussehen 0xAC Bytes ab +0x74, Klasse +0x30/31, Wertefaktoren +0xF58).
	struct RaceTpl { int race = -1; uint8_t cls[2] = {}; uint8_t app[0xAC] = {}; uint8_t fac[0x14] = {}; };
	bool MakeRaceTemplate(int race, RaceTpl& out);                  // Spielthread
	void ApplyRaceTemplate(uint8_t* spawn, const RaceTpl& t);       // Rasse + Aussehen setzen (Aufrufer faengt Ausnahmen)
	uint16_t PeacefulFlags(uint16_t af);                           // friedlich: Mod-Marke 0x4000 (Hook creature_isPassive 0x444680)
	uint16_t HostileModFlags(uint16_t af);                         // feindlich: Mod-Marke 0x8000 (auch Kuh & Co.)
	int  BasePartModel(int race, int part);
	// Koerperbau je Rasse (Hook auf creature_generateAppearance, Datei Strukturen\Mobs\_koerperbau.txt)
	struct BodyField { const char* key; const char* label; int off; int n; };   // off: im Aussehen (Kreatur+0x78)
	const int kBodyFieldCount = 14;
	const int kBodyMax = 512;                                         // native Rassen + eigene (>= 200)
	extern const BodyField kBodyFields[kBodyFieldCount];
	struct BodyVals { uint32_t mask = 0; float val[kBodyFieldCount][3] = {}; };
	bool InstallBodyHook();                                           // vor MH_EnableHook(MH_ALL_HOOKS)
	void ReloadBodyDefs();
	bool GetBodyDef(int race, BodyVals& out);
	void SetBodyDefLive(int race, const BodyVals& v);                 // sofort fuer neue Kreaturen (ohne Datei); mask 0 = aufheben
	bool SaveBodyDefs(std::string& err);                              // aktuelle Tabelle in die Datei
	bool ReadBodyValues(int race, BodyVals& native, BodyVals& effective);   // Spielthread
	bool ReadBodyValuesMob(const CustomMob& m, BodyVals& native, BodyVals& effective);   // Spielthread, eigene Rasse
	long BodyVersion();
	std::string BodyFilePath();                         // Spielthread: Registry-Index des Teils der Basisrasse (-1 = keins)                           // friedlich wie das Huhn: |0x12, &~0x08
	void RegisterFlyerGuid(uint64_t guid, int64_t x, int64_t y);
	// Begleiter des Besitzers (Feindlichkeit 5, native Begleiter-KI: kaempft + folgt). Nur Spielthread.
	uint64_t SpawnCompanionAt(cube::GameController* gc, int race, int level, int64_t x, int64_t y, int64_t z, uint64_t owner, bool equipment);
	struct CreatureInfo { uint64_t guid; int race, hostility, cls, level; float hp; int64_t x, y, z; float sx = 0, sz = 0; };   // sx/sz: Aussehen-Groesse +0x80/+0x88 (Bloecke)   // x/y/z in Bloecken
	int  ListCreaturesNear(cube::GameController* gc, int64_t bx, int64_t by, int64_t bz, int rXY, int rZ, CreatureInfo* out, int maxOut);

	// Liest das Aussehen der zuletzt gespawnten Kreatur zurueck (Teile-IDs,
	// Skalen, Positionen) -- um eigene Teile an echte Werte anzupassen.
	std::string DescribeLast(cube::GameController* gc);
}
