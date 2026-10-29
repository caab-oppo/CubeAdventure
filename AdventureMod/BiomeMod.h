#pragma once
//
// BiomeMod.h
// ---------------------------------------------------------------
// Eigene Biome: registrieren, aufbauen, mit Pflanzen, Dungeons und Mobs fuellen.
// Grundlage: RE_Biome.md (alle Adressen dort belegt).
//
// REGISTRIEREN (Weltgenerierung, Zonen-Thread):
//   Hook 0x5D7A70  Klimazelle neu  -> Zelle bekommt ein Biom (eigener Hash aus
//                  Zelle + Weltsamen, KEIN rand()); optional Klima ueberschreiben.
//   Gewicht W      Nachbau von 0x5EF040 (gleiche Perlin-Verschiebung 0x4C0EF0,
//                  gleiche Formel) -> weiche Grenzen wie beim nativen Lava-Biom.
// AUFBAUEN:
//   Hook 0x5C56E0  Bodenfarbe + Blocktyp     Hook 0x5C7140  Felsfarbe
//   Hook 0x5F56C0  Rasse der nativen Spawns  Hook 0x5DA280  native Orte je Biom aus
//   OnZoneGenerated -> (Spielthread) Pflanzen + Dungeons per placeStructure
//   Spielthread     -> eigene .mob-Kreaturen in Spielernaehe, Dungeon-Besatzung
//   MountainVillage -> Bergdorf je Biom abschaltbar (Filter)
//
// Alle Hooks rufen zuerst das Original und mischen danach; ohne Biom (W = 0)
// bleibt jedes Ergebnis bitgleich Vanilla.
//
#include "cwsdk.h"
#include "BiomeGen.h"
#include <string>
#include <vector>

namespace BiomeMod {

	// Einmal nach cube::InitGlobals() und MH_Initialize(), VOR MH_EnableHook(MH_ALL_HOOKS).
	void Install();
	void RegisterWithFramework();

	// ---- Definitionen (Strukturen\Biome\*.biom) -- jeder Thread --------------
	// Laedt alle Dateien neu; wirkt fuer alle ab jetzt erzeugten Zonen/Zellen.
	void ReloadDefinitions();
	bool SetSpawnDensity(int level);   // native Spawndichte: 1 = Spiel (3x3 je Zone), 2 = 4x4, 3 = 5x5, 4 = 6x6
	int  SpawnDensity();
	std::vector<BiomeGen::BiomeDef> GetDefinitions();
	std::vector<std::string> GetDefinitionFiles();      // gleiche Reihenfolge wie GetDefinitions
	void SetEnabled(bool on);                          // alles an/aus (aus = Vanilla)
	int  DefinitionsVersion();                         // steigt bei jedem ReloadDefinitions
	std::vector<std::pair<int, BiomeGen::BiomeDef>> GetDefinitionsWithIds();   // (Biom-ID 1..15, Definition)
	int  BiomeIdAt(const void* world, int x, int y);   // jeder Thread (auch Zonenerzeugung): Biom-ID am Block, 0 = nativ
	bool IsNativeBiome(int id);
	int  NativeClassAt(const void* world, int x, int y);   // native Klimaklasse am Block (1..6, 0 = Ozean/keine), jeder Thread                         // Datei bearbeitet ein natives Biom (nativ=1..6)
	// Dungeons.cpp: geladene Welt mit Welt-/Blocksperre stempeln (Spielthread, rot 0) / Bodenhoehe einer Saeule (false = Wasser)
	int  StampLive(cube::GameController* gc, const Lab::Structure& s, int x, int y, int z, int fundament);
	bool ZoneSurfaceAt(void* zone, int x, int y, int* z, uint8_t* t);
	bool IsEnabled();

	// ---- Zustand fuer das LAB ---------------------------------------------------
	struct Status {
		bool hooksOk = false;
		std::string hookReport;
		int  biomes = 0;
		int  hereBiome = 0;            // 0 = keins, sonst Index+1
		float hereW = 0;
		std::string hereName;
		long cellsAssigned = 0, sitesRemoved = 0, racesReplaced = 0, treesRemoved = 0;
		long zonesDecorated = 0, plantsPlaced = 0, dungeonsPlaced = 0, mobsSpawned = 0, queue = 0;
		int  mobsTracked = 0;
		long minesCarved = 0;          // gegrabene Minenabschnitte (je Zone gezaehlt)
	};
	Status GetStatus();

	// ---- Spielthread-Auftraege (aus dem LAB per LabBridge::Post) ----------------
	// Naechste bereits angelegte Zelle mit Biom 'index' (0-basiert) suchen.
	std::string FindNearest(cube::GameController* gc, int index);
	// Native Orte (Ortstabelle der Regionen) im Umkreis: Typ, Entfernung, Richtung, Zustand (aktiv /
	// vom Biom abgeschaltet / freie Ebene). Nur Spielthread.
	std::string NativeSitesNear(cube::GameController* gc, int radiusBlocks);
	// Die Klimazelle, die am Spieler das Biom bestimmt, auf Biom 'index' festlegen
	// (clear = Festlegung aufheben). Gespeichert je Weltsamen in Strukturen\Biome\_erzwungen.txt.
	std::string ForceHere(cube::GameController* gc, int index, bool clear);
	// UNGETESTET: Spieler 300 Bloecke hoch ueber die Mitte der naechsten Biomzelle setzen.
	std::string TeleportToNearest(cube::GameController* gc, int index);
	// Naechste Elfenstadt (Mitte = Mutterbaum/Hochelfenkoenig), aus dem Seed berechnet (SeedScout). Spielthread.
	bool NearestElfCity(cube::GameController* gc, int px, int py, int* cx, int* cy);
	// Dasselbe ohne Spiel (jeder Thread): Ringsuche bis maxRing Zellen (1023 = ganze Welt). base = WorldBase(gc).
	bool NearestElfCityFrom(uint32_t base, int px, int py, int maxRing, int* cx, int* cy);
	uint32_t WorldBase(cube::GameController* gc);
	// Testweise am Spieler (+40 Bloecke in X, wie alle Testplatzierungen):
	std::string PlacePlantHere(cube::GameController* gc, const BiomeGen::BiomeDef& def, int kind, uint32_t seed);
	std::string PlaceDungeonHere(cube::GameController* gc, const BiomeGen::BiomeDef& def, uint32_t seed);

	// Zeilen fuer die [F9]-Anzeige: Biom hier + naechste eigene Biome (berechnet, auch unerforscht).
	std::string TrackingText(cube::GameController* gc);
	// F9-Anzeige (Tracking.cpp), Spielthread
	struct SiteInfo { int kind, x, y; };
	std::string BiomeNameAt(cube::GameController* gc, int x, int y);              // eigenes Biom oder natives Klima
	int NativeSites(cube::GameController* gc, int radius, std::vector<SiteInfo>& out);   // native Orte (kind 1..15)
	int OwnBiomeTargets(cube::GameController* gc, std::vector<SiteInfo>& out, std::vector<std::string>& names);

	// ---- Zonen-Modelle (eigene .cub, mit der Zone erzeugt wie die nativen Baeume) ----
	struct ZoneModelInfo {
		std::string file; BiomeGen::ZoneModelDef def;
		bool modelOk = false; int sx = 0, sy = 0, sz = 0; long placed = 0; std::string note;
	};
	// Fehlersuche (LAB -> Leistung): Objekte mit der Zone erzeugen (Hangbaeume, Biom-Pflanzen,
	// Zonen-Modelle) bzw. nur die kleinen Blumen. Gespeichert ("zonenobjekte", "kleine_blumen").
	void SetZoneObjects(bool on); bool ZoneObjectsOn();
	void SetSmallFlowers(bool on); bool SmallFlowersOn();
	void ReloadZoneModels();                                    // Strukturen\Zonenmodelle\*.zmod neu einlesen (jeder Thread)
	std::vector<ZoneModelInfo> GetZoneModelInfo();
	// .cub eines Zonen-Modells laden (Name in Strukturen\Zonenmodelle, sonst Strukturen, sonst Pfad)
	bool LoadZoneModelCub(const std::string& cub, Lab::Structure& out, std::string& err);
	// Spielthread: einmal am Spieler (+40 Bloecke in X) nach den Regeln der Art setzen
	std::string PlaceZoneModelHere(cube::GameController* gc, const BiomeGen::ZoneModelDef& def, const Lab::Structure& s);

	// Intern, fuer den Seed-Rechner SeedScout (SeedScout::ModAdjustSite/ModFeatureAllowed/ModVersion)
	long ModVersionInternal();
	void AdjustSiteInternal(uint32_t base, int cx, int cy, int elev, float* t10, float* hC, int* wet);
	bool FeatureAllowedInternal(uint32_t base, int x, int z, int kind);
}
