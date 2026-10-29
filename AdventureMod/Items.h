#pragma once
//
// Items.h -- eigene Gegenstaende (LAB -> Reiter "Gegenstaende")
// ---------------------------------------------------------------
// Ein eigener Gegenstand ist ein nativer Gegenstand (Typ/Unterart/Material bestimmen Verhalten, Reiter
// im Handwerksmenue, Werte), der eine eigene KENNUNG im Modifier (+4) traegt: 'IT' << 16 | id.
// Item_equals (0x42F4A0) vergleicht +4 mit -> eigener Stapel, eigenes Rezept, eigene Zutat.
// Name und Modell kommen ueber die Hooks in BiomeMod (buildObjectName 0x598A50, item_icon_id 0x4EC400):
// das Modell gilt fuer Inventar, Hand/Ausruestung und Boden.
// Dateien: Strukturen\Gegenstaende\*.item (Text, key=value).
// Ins Inventar: GameController_addItemToInventory 0x46EBE0 (thiscall auf Kreatur+0x11DC, Inventar-Reiter-
// Vektor; fuer stapelbare Typen 0x0B/0x0C/0x0D/... liest es die Anzahl aus Item+0x10).
//
#include "cwsdk.h"
#include "LabStructure.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Items {
	enum Kind { K_MATERIAL = 0, K_CONSUMABLE, K_WEAPON, K_ARMOR, K_JEWEL, K_OTHER, K_COUNT };
	const char* KindName(int k);
	void KindDefaults(int k, int* type, int* sub, int* mat);       // Vorschlag beim Wechsel der Art

	// Waffen (Typ 3). Unterart-Namen aus der Namenstabelle World.cpp:2840ff., Klasse aus item_classifyRecursive
	// 0x4C6F20 (Krieger {0,1,2,0xD,0xF,0x10,0x11}, Schurke {3,4,5}, Waldlaeufer {6,7,8,0xE}, Magier {0xA,0xB,0xC}),
	// Zweihand aus Item_get_stack_or_grid_size 0x4C7660 {5,6,7,8,0xA,0xB,0xF,0x10,0x11,0x12}.
	enum { CLS_WARRIOR = 1, CLS_RANGER = 2, CLS_MAGE = 3, CLS_ROGUE = 4 };
	struct WeaponDef { int sub; int cls; bool twoHand; const char* name; int mat; };
	const WeaponDef* Weapons(int* n);
	const char* ClassName(int cls);                                   // 1..4
	bool WeaponOf(int type, int sub, int* cls, bool* twoHand);        // false = keine Klassenwaffe

	const uint32_t kItemTag = 0x49540000u;                          // 'IT'
	inline uint32_t Tag(int id) { return kItemTag | ((uint32_t)id & 0xFFFFu); }

	struct ItemDef {
		std::string name = "neuer-gegenstand";
		bool enabled = true;
		int kind = K_MATERIAL;
		int type = 0x0B, sub = 0x00, mat = 0x00;                    // nativer Grund-Gegenstand
		int level = 1, rarity = 0;
		std::string modelFile;                                        // .cub (relativ zu Strukturen\ oder absolut), leer = natives Aussehen
		int id = 0;                                                   // Kennung 1..65535 (vom LAB vergeben)
		// wird_zu (2026-10-19): landet der Gegenstand im Inventar (z.B. hergestellt), tauscht der Mod ihn gegen diesen
		// nativen Gegenstand (Typ/Unterart/Material/Seltenheit) -- so lassen sich Gegenstaende herstellen, die das
		// Handwerksmenue nicht listet (Lampe Typ 0x18: Liste 0x4A161D kennt nur 1,3-9,0x0B).
		int convType = -1, convSub = 0, convMat = 0, convRarity = 0;
		std::string file;                                             // Dateiname (nur zur Laufzeit)
	};

	std::string ItemsDir();                                           // "Strukturen\\Gegenstaende"
	bool Load(const std::string& path, ItemDef& out, std::string& err);
	bool Save(const std::string& path, const ItemDef& d, std::string& err);
	std::vector<std::string> ListFiles();
	void Reload();                                                    // jeder Thread
	int  NextId();
	std::vector<ItemDef> All();                                       // geladene (aktive und inaktive)
	bool ById(int id, ItemDef* out);

	// Fuer die Hooks / Rezepte
	bool IdOf(const uint8_t* item, int* id);                          // Kennung + passender Typ/Unterart eines geladenen Gegenstands
	bool Name(const uint8_t* item, std::wstring* out);
	struct ModelEntry { int id, type, sub; Lab::Structure s; std::string file; };
	long ModelVersion();
	void Models(std::vector<ModelEntry>& out);
	std::string ModelPath(const std::string& file);

	void FillItem(const ItemDef& d, uint8_t* item);                   // 0x118-Datensatz bauen
	int  Give(cube::GameController* gc, const ItemDef& d, int count, std::string& report);   // NUR Spielthread
	std::string Status();
	void Tick(cube::GameController* gc);                              // Spielthread: "wird_zu" umsetzen
}
