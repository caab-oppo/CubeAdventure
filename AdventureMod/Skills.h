#pragma once
//
// Skills.h -- eigene Faehigkeiten (LAB -> Reiter "Skills")
// ---------------------------------------------------------------
// Ein Skill ist eine Datei Strukturen\Skills\*.skill (Text, key=value):
//   art      was er tut (Teleport, Beschwoeren, ... -- neue Arten: Kind + Handler in Skills.cpp)
//   klasse   0 = alle Klassen, 1 Krieger, 2 Waldlaeufer, 3 Magier, 4 Schurke (Kreatur+0x140)
//   bild     Symbol (.png/.bmp/.jpg, relativ zu Strukturen\Skills\Bilder oder absolut)
//   taste    virtueller Tastencode (Vorgabe: Teleport 4, Beschwoeren 5)
//   taste2   zweite Taste (Teleport: 6 = Steinliste / naechster Stein), taste_ablegen (Teleport: U = Runenstein ablegen/aufheben)
//   Fortschritt: ab_level, level_je_rang, max_rang -> der Rang folgt dem Spielerlevel
//   Werte je Rang: wert + wert_je_rang, dauer + dauer_je_rang, abklingzeit - abklingzeit_je_rang, mana (% des Manas)
//
// Spielwerte (belegt, Effects.h): Kreatur +0x16C Leben, +0x170 Mana (0..1), +0x140 Klasse, +0x190 Level,
// +0x10/+0x18/+0x1C.. Position (Festkomma, Block = 65536), +0x34/+0x38 Geschwindigkeit.
// Beschwoeren: die zuletzt besiegten Gegner (Rasse + Level, neueste zuerst) kommen als Begleiter zurueck.
//   cube_world_init_companion 0x5EE7A0 schreibt die GUID des Begleiters in den Besitzer (+0x11C8/+0x11CC, EIN Platz
//   fuer das Haustier) -- der Platz wird vorher gesichert und danach zurueckgeschrieben, sonst ersetzt der Skill das
//   eigene Haustier (Fehlerbild: "respawnt nur den eigenen Begleiter").
// Teleport: Runensteine (Objekttyp 0x2D, Modell runestone.cub 0x9DD, 11x8x12). U legt einen benannten Stein ab
//   (U neben einem Stein hebt ihn wieder auf), 6 portet zum Stein bzw. zeigt bei mehreren die Liste (6 = weiter,
//   4 = teleportieren). Anzahl Steine = Wert des Rangs. Gespeichert je Welt in AdventureMod_runensteine_<Seed>.txt,
//   bei jeder Zonenerzeugung neu gesetzt (ZoneObjects). +0x178 = -1: der native Runenstein-Sprung (gleiche Zone,
//   GameController.cpp:96928) greift nicht.
//
#include "cwsdk.h"
#include <string>
#include <vector>


namespace Skills {
	enum Kind { SK_TELEPORT = 0, SK_SUMMON, SK_COUNT };
	const char* KindName(int k);              // "Teleport", "Beschwoeren"
	const char* KindHelp(int k);              // was wert/dauer bei dieser Art bedeuten
	const char* ClassName(int c);             // 0 "alle Klassen", 1..4

	struct SkillDef {
		std::string name = "neuer-skill";
		bool  enabled = true;
		int   kind = SK_TELEPORT;
		int   cls = 0;
		std::string icon;                     // Bilddatei
		int   key = 0x34;                     // '4'
		int   key2 = 0;                       // zweite Taste (Teleport: '6')
		int   keyPlace = 0;                   // Teleport: Runenstein ablegen/aufheben ('U')
		int   unlockLevel = 1, levelsPerRank = 10, maxRank = 5;
		// Skilltree (Taste X): Platz im nativen Rangfeld Kreatur+0x1138 (9 = Faehigkeit 4, 10 = Faehigkeit 5; im nativen
		// Fenster unsichtbar, aber mitgezaehlt und mitgespeichert). -1 = Rang folgt dem Level (ab_level/level_je_rang).
		int   treeSlot = -1, pointsPerRank = 5;
		float value = 1.0f, valuePerRank = 1.0f;        // Teleport: Anzahl Runensteine; Beschwoeren: Anzahl
		float duration = 0.0f, durationPerRank = 0.0f;  // Sekunden (Beschwoeren: Lebensdauer)
		float cooldown = 8.0f, cooldownPerRank = 1.0f;  // Sekunden (je Rang kuerzer, mindestens 1 s)
		int   mana = 20;                                // % des Manas
		std::string desc;                               // beschreibung= (Skillfenster X: Maus ueber dem Platz)
		std::string unlockBy;                           // freischalten=<npc>: erst nach dem Gespraech (z. B. hochelfenkoenig)
		std::string file;                               // Dateiname (Laufzeit)
	};
	SkillDef DefaultSkill(int kind);

	std::string SkillsDir();                  // "Strukturen\\Skills"
	std::string IconsDir();                   // "Strukturen\\Skills\\Bilder"
	std::string IconPath(const std::string& icon);
	bool Load(const std::string& path, SkillDef& out, std::string& err);
	bool Save(const std::string& path, const SkillDef& d, std::string& err);
	std::vector<std::string> ListFiles();
	void Reload();                            // jeder Thread; legt beim ersten Mal die zwei Vorgaben an
	std::vector<SkillDef> All();

	int RankFor(const SkillDef& d, int level);          // 0 = noch nicht freigeschaltet (nur Level-Skills; Baum-Skills: Punkte)
	int RankForPoints(const SkillDef& d, int points);   // Baum-Skills: Rang aus investierten Punkten
	float ValueAt(const SkillDef& d, int rank);
	float DurationAt(const SkillDef& d, int rank);
	float CooldownAt(const SkillDef& d, int rank);

	// Spielthread
	void Tick(cube::GameController* gc);
	// Freischaltung durch ein Gespraech (City: Hochelfenwkoenig). Je Charakter in der Charakter-DB
	// ("AdventureMod:<Name>:freigeschaltet_<npc>"). Spielthread.
	void UnlockByNpc(cube::GameController* gc, const std::string& npc);
	bool IsUnlockedByNpc(cube::GameController* gc, const std::string& npc);
	void DrawHud(cube::GameController* gc, int w, int h);   // EndScene: Beschreibung im Skillfenster, Ersatz-Steinliste (Schrift des Spiels)

	bool HudOn();  void SetHud(bool on);       // Skill-Leiste unten rechts (ModSwitch "skill_leiste")
	struct Snapshot { bool inGame = false; int level = 0, cls = 0; std::string kills; };
	Snapshot GetSnapshot();                   // fuer das LAB
	std::string Status();
	// Totenbeschwoerung: eigene Begleiter-Plaetze (Spielthread)
	void TestSummon(cube::GameController* gc, int race);    // ohne besiegte Gegner, z. B. Skelett 17
	void FreeSummons(cube::GameController* gc);             // alle Plaetze leeren (das Spiel entlaesst die Kreaturen)

	// Runensteine (Teleport)
	struct Stone { std::string name; int x, y, z; };
	std::vector<Stone> Stones();                 // der aktuellen Welt
	// Zonenerzeugung (beliebiger Thread): Runensteine dieser Zone als native Objekte anlegen. world = GC+0x2E4.
	void ZoneObjects(const void* world, void* zone, int zx, int zy);
}
