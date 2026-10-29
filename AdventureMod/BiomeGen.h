#pragma once
//
// BiomeGen.h
// ---------------------------------------------------------------
// Reiner Rechenteil der Biom-Werkstatt -- KEIN Spielzugriff, deshalb auch
// ausserhalb des Spiels testbar:
//
//   * BiomeDef        Definition eines eigenen Bioms (Datei Strukturen\Biome\*.biom)
//   * MakePlant       prozedurale Pflanze  -> Lab::Structure
//   * MakeDungeon     prozeduraler Dungeon -> Lab::Structure + Raumliste
//   * Hash/Rng        deterministischer Zufall (gleiche Welt = gleiche Pflanzen)
//
// Warum deterministisch: Cube World erzeugt Zonen bei jedem Betreten neu. Alles,
// was der Mod hineinsetzt, muss beim naechsten Mal exakt gleich aussehen --
// also nur aus (Zone, Index, Weltsamen) berechnet, nie aus Zeit oder rand().
//
#include "LabStructure.h"
#include <cstdint>
#include <string>
#include <vector>

namespace BiomeGen {

	// ---- Zufall ---------------------------------------------------------------
	uint32_t Hash(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
	struct Rng {
		uint32_t s;
		explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
		uint32_t Next();                       // xorshift32
		int   Range(int lo, int hi);           // [lo, hi]
		float Unit();                          // [0, 1)
	};
	// Glatter Wertrausch-Wert in [0,1] (fuer Farbabwandlungen; unabhaengig vom Spiel).
	float ValueNoise2(double x, double y, uint32_t seed);

	// ---- Definition -------------------------------------------------------------
	enum PlantKind { PK_TREE, PK_BUSH, PK_MUSHROOM, PK_CRYSTAL, PK_CACTUS, PK_GRASS, PK_COUNT };
	const char* PlantKindName(int k);          // "Baum", "Busch", ...

	enum ClimateReq { CR_ANY, CR_TEMPERATE, CR_HOT, CR_COLD, CR_WET, CR_DRY, CR_COUNT };
	const char* ClimateReqName(int c);

	struct RGB { uint8_t r = 0, g = 0, b = 0; };

	// Biome II (2026-10-16): eine Spawnregel -- WAS spawnt WO im Biom mit welchem Level und Verhalten.
	// Wirkt am nativen Spawn (Zonenerzeugung): ein Teil der nativen Tier-/Fischgruppen wird durch eine passende Regel ersetzt.
	struct SpawnRule {
		std::string what;            // Zahl = native Rasse (Mob-Labor-ID), sonst eigener Mob: Pfad unter Strukturen\Mobs ohne .mob (z.B. Tierwelt\hase)
		std::string where;           // "" ueberall (Land bzw. Wasser je nach Tier), land, wasser, wald, wiese, savanne, wueste, dschungel, schnee, gebirge, sumpf, kueste
		int weight = 10;             // Gewicht gegen die anderen passenden Regeln
		int lvMin = 0, lvMax = 0;    // 0 = Level wie nativ an dieser Stelle (bzw. Biom-Spanne)
		int behave = 0;              // 0 Standard (Biom/Datei), 1 friedlich, 2 feindlich
	};
	const char* const kWhereKeys[] = { "", "land", "wasser", "wald", "wiese", "savanne", "wueste", "dschungel", "schnee", "gebirge", "sumpf", "kueste" };
	const int kWhereCount = 12;

	struct BiomeDef {
		std::string name = "neues-biom";
		bool  enabled = true;
		// -- Auswahl / Klima
		int   share = 10;            // % der passenden Landzellen (0..100)
		int   climateReq = CR_ANY;   // welche nativen Zellen in Frage kommen
		bool  setClimate = false;    // Temperatur/Feuchte der Zelle ueberschreiben
		float temp = 0.5f, hum = 0.5f;
		// -- Boden / Fels
		RGB   ground{ 70, 140, 60 }, ground2{ 50, 110, 45 };
		int   groundType = 0;        // 0 = nativ lassen; 4 Gras, 9 Sand, 10 Schnee, 12 Lava-Boden (Namen VERMUTET)
		float strength = 1.0f;       // 0..1 Deckkraft der Biomfarbe
		RGB   rock{ 110, 105, 100 };
		bool  rockOn = true;
		// -- Gelaende (Hook 0x5C5E20 Grundhoehe, 0x5EFFA0 Bergfaktor, 0x5DF960 native Baeume)
		//    Hoehen in Bloecken, Meeresspiegel = 0: alles unter Hoehe 1 fuellt das Spiel mit Wasser.
		bool  terrainOn = false;
		float baseHeight = 8.0f;     // Grundhoehe
		float waves = 3.0f;          // Wellen: +- Bloecke kleiner Unebenheiten
		float waveSize = 30.0f;      // Wellenlaenge in Bloecken
		float hillShare = 0.15f;     // 0..1 Anteil der Flaeche mit Huegeln
		float hillHeight = 18.0f;    // Huegelhoehe in Bloecken
		float hillSize = 90.0f;      // Huegelgroesse (Wellenlaenge) in Bloecken
		float flatten = 0.9f;        // 0 = natives Gelaende, 1 = ganz durch die Biomform ersetzt
		float mountains = 0.2f;      // 0 = Berge/Klippen weg, 1 = native Berge bleiben
		int   nativeTrees = 100;     // % der nativen Baeume, die im Biom bleiben
		// -- Fluesse und Seen (Hook 0x5F9340 "Flussabstand" r; Fluss/See wo r < 0.02,
		//    Wasser auf Gelaendehoehe abgestuft auf 5 Bloecke -- RE_Biome.md 6f)
		bool  riversNative = true;   // native Flusslaeufe im Biom behalten
		float riverWidth = 1.0f;     // Breite x (0.5 .. 4)
		bool  riversOwn = false;     // zusaetzliche eigene Fluesse
		float riverSpacing = 600.0f; // mittlerer Abstand der eigenen Fluesse in Bloecken
		float lakeShare = 0.0f;      // 0..1 Anteil der Rasterfelder mit See
		float lakeSize = 60.0f;      // Seedurchmesser in Bloecken
		// -- Bergzuege: lange Kaemme entlang gewundener Linien (0 Hoehe = aus)
		float ridgeHeight = 0.0f;    // Kammhoehe ueber dem Gelaende in Bloecken
		float ridgeWidth = 250.0f;   // halbe Breite (Kamm bis Fuss) in Bloecken
		float ridgeSpacing = 1400.0f;// mittlerer Abstand der Bergzuege in Bloecken
		float ridgeRough = 0.5f;     // 0 = glatter Kamm, 1 = stark zerklueftet (Gipfel/Saettel)
		float snowLine = 0.0f;       // Schneegrenze (Oberflaechenhoehe in Bloecken), 0 = aus
		// -- Freie Ebene in der Mitte jeder Biomzelle (Zellmitte = Klimazelle +0/+4)
		//    Flach auf plainHeight, ohne Fluesse, Seen, Bergzuege, Baeume, Pflanzen, Dungeons, Orte, Minen.
		bool  plainOn = false;
		float plainRadius = 1200.0f; // Radius der ganz flachen Flaeche in Bloecken
		float plainRim = 250.0f;     // Breite des Uebergangs ins Biomgelaende
		float plainHeight = 20.0f;   // Hoehe der Ebene (Meeresspiegel 0)
		// -- Stadt auf der Ebene/Hochebene (City.cpp): 0 keine, 1 Elfenstadt, 2 Zwergenstadt, 3 zufaellig je Zelle
		int   city = 0;
		float cityRadius = 0.0f;     // 0 = ganze Ebene (Ebenenradius - 30)
		std::vector<int> cityAnimals;            // Tierviertel: Rassen-IDs in den Gehegen (leer = Schwein, Schaf, Kuh, Huhn)
		// -- Sternmassiv: um die Mitte (Hochebene = Ebene oben: Radius/Hoehe/Rand) ein Bergring,
		//    von dem sternfoermig Grate und Taeler abgehen. Jedes Tal fuehrt einen Fluss am
		//    Talgrund, der in einem See endet. Innen keine nativen Berge, Fluesse oder Rasterseen.
		bool  starOn = false;
		int   starArms = 8;          // Anzahl Taeler (3..16)
		float starRadius = 6000.0f;  // Aussenradius des Massivs in Bloecken
		float starRingHeight = 130.0f; // Ring-Gipfel ueber der Hochebene
		float starRidgeHeight = 152.0f;// Grathoehe ueber der Grundhoehe am Aussenende
		float starValleyWidth = 90.0f; // flacher Talgrund (volle Breite)
		float starLakeSize = 160.0f; // Seedurchmesser am Talende
		// -- Grosse Hangbaeume (Bergfichten), nur an Haengen unter der Schneegrenze
		int   slopeTrees = 0;        // Versuche je Zone (0 = aus)
		float slopeTreeSize = 1.0f;  // 0.6 .. 1.6
		// -- Minenschaechte durch die Bergzuege
		float mineShare = 0.0f;      // 0..1 Anteil der Minen-Rasterfelder (auf einem Bergzug) mit Mine
		float mineGrid = 700.0f;     // Rastergroesse in Bloecken
		int   tunnelWidth = 5, tunnelHeight = 5;
		int   mineGalleries = 3;     // Seitengaenge je Hauptstollen
		bool  mineShafts = true;     // senkrechter Schacht von der Stollenmitte bis an die Oberflaeche
		RGB   mineWood{ 115, 82, 48 }, mineLight{ 255, 214, 120 }, mineRail{ 95, 95, 105 };
		// -- Kreaturen (native Spawns, Hook auf 0x5F56C0)
		std::vector<int> races;      // Rassen-IDs
		float raceShare = 0.7f;      // Anteil der nativen Landspawns, der ersetzt wird
		// -- eigene Mobs aus dem Mob-Labor (Laufzeit-Spawner)
		std::vector<std::string> mobFiles;   // Dateinamen in Strukturen\Mobs
		int   mobMax = 6;            // gleichzeitig in Spielernaehe
		int   mobLevel = -1;         // -1 = Spielerlevel (alt; wird aus Modus/Spanne abgeleitet)
		// -- Schwierigkeit der Biom-Mobs (eigene Mobs, Spawner, Dungeon-Besatzung)
		int   mobLevelMode = 0;      // 0 = Spielerlevel (+- Abweichung), 1 = feste Spanne
		int   mobLevelMin = 1, mobLevelMax = 10;
		int   mobLevelOffset = 0;    // Modus 0: Spielerlevel +- Abweichung
		bool  spawnerRaces = false;  // Spawner setzt auch die Rassen-IDs (nicht nur .mob-Dateien)
		// -- Biome II: Spawnregeln am nativen Spawn + Ueberschreibungen der allgemeinen Tier-Einstellungen
		std::vector<SpawnRule> spawns;
		int   spawnShare = 70;       // % der nativen Tier-/Fischgruppen im Biom, die durch die Regeln ersetzt werden
		int   wildShare = -1;        // eigene Tiere am nativen Spawn (gleiche Basisrasse): -1 = allgemeine Einstellung, sonst %
		int   behaveAll = 0;         // Verhalten aller Tiere im Biom: 0 nativ/Datei, 1 alle friedlich, 2 alle feindlich
		int   levelMode = 0;         // Level der Tiere: 0 nativ, 1 feste Spanne levelMin..levelMax, 2 nativ + levelOffset
		int   levelMin = 1, levelMax = 10, levelOffset = 0;
		// -- Pflanzen
		int   plantDensity = 40;     // Versuche je Zone (256x256)
		int   plantWeight[PK_COUNT] = { 3, 2, 1, 0, 0, 4 };
		RGB   trunk{ 95, 65, 35 }, leaf{ 60, 130, 45 }, accent{ 210, 70, 150 };
		float plantSize = 1.0f;      // 0.5..2.5
		std::vector<std::string> plantFiles; // eigene .cub (Strukturen\...), AKTIV
		std::vector<std::string> plantFilesOff; // eigene .cub, in der LAB-Liste abgehakt (bleiben gelistet)
		// Zonen-Modelle (Baum/Blume/Sammelbar) je Biom: an = auch hier, aus = hier nicht (ueberstimmt "ueberall").
		// Nicht gelistet: es gilt der Bereich des Zonen-Modells.
		std::vector<std::string> zoneModelsOn, zoneModelsOff;
		int   fileWeight = 2;        // Gewicht der Dateien gegen die prozeduralen Arten
		// -- Dungeons
		int   dungeonChance = 6;     // % je Zone
		int   dungeonRooms = 4;      // Raeume je Seite (2..6)
		RGB   wall{ 95, 95, 105 }, floor{ 60, 55, 50 }, dAccent{ 220, 160, 40 };
		bool  dungeonRoof = true;
		float decay = 0.3f;          // 0 = heil, 1 = Ruine
		std::vector<int> dungeonRaces;               // native Rassen im Dungeon
		std::vector<std::string> dungeonMobFiles;    // eigene Mobs im Dungeon
		int   dungeonMobs = 6;
		bool  dungeonBoss = true;
		// -- native Orte im Biom (siehe RE_Biome.md 6b). Bit k = Ortstyp k bleibt.
		//    Typen laut SeedScout.h (vom Nutzer im Spiel verifiziert): 1 Dorf, 5 Ruinen,
		//    10 Portal, 14 grosser Dungeon/Palast, 2/3/4/6/7/8/11/12/15 Berg-/Wald-/See-/
		//    Schlucht-/Tal-/Fels-/Baum-Dungeons; 9 und 13 im Spiel nicht beobachtet.
		uint32_t siteMask = 0xFFFFu;
		bool  bergdorf = true;       // Bergdorf des Mods (MountainVillage)

		uint32_t SiteMask() const { return siteMask | 1u; }     // Typ 0 ("nichts") immer
		// -- Natives Biom bearbeiten (2026-10-27): 0 = eigenes Biom (eigene Klimazellen); 1..kNativeClasses = diese
		//    Datei gilt fuer alle Klimazellen dieser nativen Klasse (ohne eigenes Biom). Auswahl/Klima werden dann ignoriert.
		int   nativeClass = 0;
	};
	// Native Klimaklassen einer Klimazelle (RE_Biome.md 1.2/1.3: gemaessigt T 0,4-0,8 / F 0,3-0,7; extrem T und F je
	// 0-0,1 oder 0,9-1; Lava = Sonderbit). Schwellen wie Bodenfarbe 0x5C56E0 (Schnee T < 0,3, Sand T > 0,75 und F < 0,2).
	const int kNativeClasses = 6;
	const char* NativeClassName(int k);                   // 1 Grasland, 2 Schneeland, 3 Frostland, 4 Wueste, 5 Dschungel, 6 Lavagebiet
	int NativeClassOf(float temp, float hum, bool lava);  // 0 = keine (z. B. heiss + mittelfeucht)
	BiomeDef NativeDefault(int k);                        // neutrale Vorgaben: aendert nichts, bis man etwas einstellt
	// Ortstypen-Namen und Gruppen fuer die Oberflaeche
	const char* SiteKindName(int kind);
	const int kSiteKinds[] = { 1, 5, 10, 14, 2, 3, 4, 6, 7, 8, 11, 12, 15, 9, 13 };
	const int kSiteKindCount = 15;
	uint32_t DungeonSiteBits();          // alle Dungeon-Typen (2,3,4,6,7,8,9,11,12,13,14,15)

	std::string BiomesDir();                                     // "Strukturen\\Biome"
	bool LoadBiome(const std::string& path, BiomeDef& out, std::string& err);
	bool SaveBiome(const std::string& path, const BiomeDef& b, std::string& err);
	std::vector<std::string> ListBiomeFiles();                  // Dateinamen *.biom
	BiomeDef ExampleBiome(int which);                            // 0 Sumpf, 1 Kristallwueste, 2 Pilzwald, 3 Gebirge, 4 Sternmassiv

	// Zahlenlisten "1,2,3" und Namenslisten "a.mob,b.mob"
	std::vector<int> ParseIntList(const std::string& s);
	std::vector<std::string> ParseNameList(const std::string& s);
	std::string JoinInts(const std::vector<int>& v);
	std::string JoinNames(const std::vector<std::string>& v);
	bool ParseSpawnRule(const std::string& v, SpawnRule& r);    // "was;wo;gewicht;lvmin;lvmax;verhalten"
	std::string SpawnRuleStr(const SpawnRule& r);

	// ---- Gelaende ------------------------------------------------------------
	// Zielhoehe der Biomform an (x, y) in Bloecken: Grundhoehe + Wellen + Huegel.
	// Rein rechnend (fuer den Hook und fuer Tests).
	float TerrainTarget(float base, float waves, float waveSize, float hillShare, float hillHeight, float hillSize, int x, int y);
	// Bergzuege: Hoehe ueber dem Gelaende, *mask = 0..1 (1 = auf dem Kamm, 0 = ausserhalb).
	float RidgeTerm(float height, float width, float spacing, float rough, int x, int y, float* mask);
	// ---- Minen -----------------------------------------------------------------
	// Ein Stollenabschnitt (Mittellinie a->b auf Bodenhoehe z) oder ein senkrechter Schacht
	// (kind 2: Mitte a, von z nach oben bis zur Oberflaeche -- die Oberflaeche ermittelt das Spiel).
	struct MineSeg { float ax, ay, bx, by; int z, w, h, kind; };     // kind 0 Hauptstollen, 1 Seitengang, 2 Schacht
	// Alle Abschnitte, deren Huelle das Rechteck [x0,x1]x[y0,y1] beruehrt. Deterministisch.
	void PlanMines(const BiomeDef& d, int x0, int y0, int x1, int y1, std::vector<MineSeg>& out);

	// ---- Sternmassiv (rein rechnend) ------------------------------------------------
	struct StarParams {
		bool on; int arms;
		float radius, ringH, ridgeH, valleyW, lakeSize;
		float plateauR, plateauRim, plateauH, baseH;
		float riverHalf;     // halbe Flussbreite in Bloecken (6 * Flussbreite x)
	};
	StarParams MakeStarParams(const BiomeDef& d);
	struct StarSample {
		float h;            // Zielhoehe (aussen weich auf 'base')
		float mask;         // 1 = im Massiv, 0 = ausserhalb
		float riverBlocks;  // Abstand zur Talmitte in Bloecken, wo ein Fluss laeuft (sonst 1e9)
		bool  lake;         // Talende-See des naechsten Tals
		double lakeX, lakeY; float lakeR;   // relativ zur Mitte
		bool  floor;        // auf dem flachen Talgrund
		float hRaw, fade;   // h = hRaw * (1 - fade) + base * fade  (zum Zwischenspeichern ohne 'base')
	};
	// dx, dy = Abstand zur Zellmitte, base = Biomform an dieser Stelle (fuer den Uebergang aussen).
	void StarAt(const StarParams& p, uint32_t seed, double dx, double dy, float base, StarSample* out);
	// Grosse Bergfichte fuer Haenge (Saeulen ohne Luecken: placeStructure hoehlt sonst den Hang aus).
	Lab::Structure MakeBigTree(const BiomeDef& b, uint32_t seed);
	// Mini-Blume: ein grosses Blumenmodell auf foot x foot Bloecke Grundflaeche (1 oder 2) und
	// hoechstens maxH Bloecke Hoehe verkleinern. Jede Zielzelle bekommt die praegende Farbe ihres
	// Quellbereichs (Bluetenfarben zaehlen staerker als die Durchschnittsfarbe); Stiele bleiben
	// stehen: unter einer gefuellten Zelle wird jede Zelle gefuellt, in der das Modell etwas hat.
	Lab::Structure MakeMiniFlower(const Lab::Structure& src, int foot, int maxH);

	// Freie Ebene: 1 = innerhalb radius, weich (smoothstep) auf 0 bei radius + rim. dx, dy = Abstand zur Zellmitte.
	float PlainMask(float radius, float rim, double dx, double dy);

	// Naechster eigener See: dn = Abstand / Radius (< 1 = im See), Mitte (cx, cy). false = kein See in der Naehe.
	// rad (optional) = Grundradius R des Sees in Bloecken (Ufer liegt bei R * 0.82 .. R * 1.18).
	bool  LakeAt(float lakeShare, float lakeSize, int x, int y, float* dn, int* cx, int* cy, float* rad = nullptr);
	// Eigene Flusslaeufe: Flussabstand im Mass des Spiels (Fluss wo < 0.02 bei Breite 1, etwa 12 Bloecke breit).
	float OwnRiverDistance(float spacing, int x, int y);

	// ---- Zonen-Modelle: eigene .cub-Modelle, die mit der Zone erzeugt werden ------------
	// (wie die nativen Baeume, siehe BiomeMod Hook_SunCall). Datei Strukturen\Zonenmodelle\*.zmod
	// ZM_PICKUP: aufhebbarer EIGENER Gegenstand (wie Herzblume/Pilz): Gegenstandstyp 0x0B (Material),
	// eigene Unterart kPickupFirstId.. ; Modell, Name und Weltgroesse kommen aus dem Zonen-Modell.
	enum ZoneModelKind { ZM_TREE, ZM_FLOWER, ZM_DUNGEON, ZM_PICKUP, ZM_COUNT };
	const int kPickupFirstId = 0x30, kPickupCount = 32;          // Unterarten 0x30..0x4F
	const char* ZoneModelKindName(int k);                    // "Baum", "Blume", "Dungeon"
	struct ZoneModelDef {
		std::string name = "neues-modell";
		bool  enabled = true;
		int   kind = ZM_TREE;
		std::string cub;                 // .cub: Dateiname (in Strukturen\Zonenmodelle) oder Pfad
		std::string area = "ueberall";   // "ueberall", "vanilla" (kein eigenes Biom) oder "biom:<datei.biom>"
		int   ground = 0;                // Boden: 0 alle, 4 Gras, 9 Sand, 10 Schnee, 12 Lava-Boden
		int   density = 10;              // Baum/Blume: Versuche je Zone; Dungeon: % Chance je Zone
		float slopeMin = 0.0f, slopeMax = 1.4f;   // Steigung (Bloecke je Block)
		int   spacing = 12;              // Mindestabstand zu anderen eigenen Objekten
		int   hMin = -1000, hMax = 1000; // erlaubte Bodenhoehe
		int   zOffset = 0;               // Hoehenversatz (negativ = tiefer einsetzen)
		bool  rotate = true;             // zufaellig drehen (90-Grad-Schritte)
		int   uneven = 6;                // Dungeon: groesster Hoehenunterschied der Grundflaeche
		bool  foundation = false;        // Fundament nach unten bis zum Boden
		// Nur Blume: wie die nativen Pflanzen als verkleinertes Modell zeichnen statt als Bloecke.
		// 0 = als Bloecke setzen; z. B. 0.09 = wie Schilf/Blumen des Spiels (Modell x 0.09).
		float smallScale = 0.0f;
		bool  wind = true;               // im Wind wiegen (wie Schilf)
		// Nur Sammelbar: Name des Gegenstands und feste Unterart (0 = beim Speichern vergeben).
		// Bei Sammelbar ist smallScale die Groesse in der Welt (Herzblume 0.1).
		std::string itemName;
		int   itemId = 0;
	};
	ZoneModelDef DefaultZoneModel(int kind);
	std::string ZoneModelsDir();                                 // "Strukturen\Zonenmodelle"
	bool LoadZoneModel(const std::string& path, ZoneModelDef& out, std::string& err);
	bool SaveZoneModel(const std::string& path, const ZoneModelDef& d, std::string& err);
	std::vector<std::string> ListZoneModelFiles();               // *.zmod

	// ---- Pflanzen -------------------------------------------------------------
	// Grundflaeche zentriert, Stamm/Fuss bei z = 0. Gross hoechstens 15x15x24.
	Lab::Structure MakePlant(const BiomeDef& b, int kind, uint32_t seed);
	// Gewichtete Wahl einer Art (-1 = Datei aus plantFiles).
	int PickPlantKind(const BiomeDef& b, uint32_t h);

	// ---- Dungeons -------------------------------------------------------------
	struct DungeonRoom { int x, y, z; bool boss; };        // lokale Mitte, z = erste Luftschicht
	struct DungeonPlan {
		Lab::Structure s;
		std::vector<DungeonRoom> rooms;
		int entranceX = 0, entranceY = 0;                  // lokal, Mitte der Tuer aussen
	};
	DungeonPlan MakeDungeon(const BiomeDef& b, uint32_t seed);
}
