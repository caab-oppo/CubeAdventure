#pragma once
//
// Recipes.h -- eigene Handwerksrezepte (LAB -> "Rezepte ...")
// ---------------------------------------------------------------
// Belegt (RE 2026-10-06, Cube.exe):
//  * Rezeptdatenbank @0x59CFF0, stdcall(const Item* ergebnis, Rezept* out), ret 8, liefert 1.
//    Das Spiel kennt nur Rezepte, deren ERGEBNIS-Gegenstand der Spieler gelernt hat; zu jedem
//    gelernten Ergebnis fragt es hier Zutaten und Station ab:
//      out+0x000  Gegenstand (0x118, Kopie des Ergebnisses, Item_copyStruct 0x42C5E0)
//      out+0x118  Station: 0 keine, 1 Wasser, 3 Ofen, 4 Amboss, 5 Spinnrad, 6 Webstuhl,
//                 7 Saege, 8 Werkbank, 9 Lagerfeuer (Pruefung/Texte 0x42F910..)
//      out+0x11C  std::vector<Zutat 0x11C> -- Zutat = Gegenstand (Typ +0, Unterart +1,
//                 Material +0xD, +0x10 = 1) + Anzahl (+0x118); anhaengen mit 0x5A0D80
//                 (thiscall(vec*, const Zutat*), ret 4)
//    Aufrufer: Handwerksliste 0x4A1604 (sortiert nach Ergebnistyp: 3 Waffen, 4-7 Ruestung,
//    8-9 Schmuck, 1 Kochen/Alchemie, 0x0B Material) und Auswahl/Herstellen 0x47BBA7.
//  * Gelernte Rezepte: Kreatur+0x1D28 -> +0x14 (std::list<Item>); schon bekannt? 0x444A90,
//    anhaengen 0x44D460 (wie PotionCrafting).
// Der Mod leitet 0x59CFF0 fuer die eigenen Ergebnisse um und lernt sie dem Spieler an.
//
#include "cwsdk.h"
#include "LabStructure.h"
#include <string>
#include <vector>

namespace Recipes {
	// Wirkungen beim Trinken/Essen (Effects.cpp). FX_HEAL = normaler Trank (heilt wie bisher).
	enum EffectKind { FX_NONE = 0, FX_HEAL, FX_REGEN, FX_STRENGTH, FX_PROTECT, FX_SPEED, FX_SLOWFALL,
	                  FX_NIGHT, FX_MANA, FX_STAMINA, FX_COUNT };
	const char* EffectName(int k);                // Anzeige
	const char* EffectKey(int k);                 // Dateiwert
	const char* EffectUnit(int k);                // Bedeutung des Werts
	float EffectDefaultValue(int k);
	inline bool EffectTagged(int k) { return k >= FX_REGEN && k < FX_COUNT; }
	// Effekt-Traenke tragen ihre Kennung im Modifier (+4) des Gegenstands: 'EF' << 16 | id.
	// Item_equals (0x42F4A0) vergleicht +4 mit -> eigene Stapel, eigenes Rezept.
	const uint32_t kEffectTag = 0x45460000u;
	inline uint32_t EffectModifier(int id) { return kEffectTag | ((uint32_t)id & 0xFFFFu); }

	struct Ingredient { int type = 0x0B, sub = 0x16, mat = 0, count = 1; int item = 0; };   // item: eigener Gegenstand (Items, Kennung) oder 0
	struct RecipeDef {
		std::string name = "neues-rezept";
		bool enabled = true;
		int  outType = 0x0B, outSub = 0x30, outMat = 0, outLevel = 1, outRarity = 0;
		int  station = 0;
		bool learnAtStart = true;
		std::vector<Ingredient> ing;              // hoechstens 6
		int   effect = FX_NONE;                   // EffectKind
		float effectValue = 0.0f;                 // Bedeutung: EffectUnit()
		int   effectSeconds = 120;
		bool  effectAlsoHeal = false;             // zusaetzlich die normale Trank-Heilung
		int   effectId = 0;                       // Kennung 1..65535 (vom LAB vergeben): Effekt-Traenke UND Ergebnisse mit eigenem Modell
		std::string modelFile;                    // eigenes optisches Modell (.cub, relativ zu Strukturen\ oder absolut), leer = nativ
		int outItem = 0;                          // Ergebnis ist ein eigener Gegenstand (Items, Kennung) -- Name/Modell von dort
	};
	// Braucht das Ergebnis eine Kennung im Modifier (+4)? Wirkung mit Kennung oder eigenes Modell.
	inline bool NeedsTag(const RecipeDef& r) { return EffectTagged(r.effect) || !r.modelFile.empty(); }
	// Wie wird das Ergebnis benutzt? (useItem 0x4A2780: Typ 1 Unterart 1/2 trinken 0x50, 4/5/6/8/9 essen 0x51)
	const char* UseText(int type, int sub);
	struct EffectInfo { int kind = FX_NONE; float value = 0; int seconds = 0; bool alsoHeal = false; std::wstring name; };
	struct Named { const char* name; int type, sub, mat; };
	const Named* Presets(int* count);             // native Zutaten/Ergebnisse mit Namen
	const char* StationName(int code);            // 0..9
	extern const int kStations[9];                // gueltige Stationscodes in Anzeigereihenfolge

	std::string RecipesDir();                     // "Strukturen\\Rezepte"
	bool Load(const std::string& path, RecipeDef& out, std::string& err);
	bool Save(const std::string& path, const RecipeDef& d, std::string& err);
	std::vector<std::string> ListFiles();

	void Install();                               // nach MH_Initialize, vor MH_EnableHook
	void Reload();                                // jeder Thread
	void Tick(cube::GameController* gc);          // Spielthread: Rezepte anlernen
	std::string Status();

	// Effekt-Traenke
	bool EffectIdOf(const uint8_t* item, int* id);            // Typ 1 + Kennung im Modifier?
	bool EffectById(int id, EffectInfo* out);                 // aus den geladenen Rezepten
	bool ItemName(const uint8_t* item, std::wstring* out);    // Name fuer Effekt-Traenke
	int  NextEffectId();                                      // fuer das LAB (frei, > alle geladenen)
	bool IsEffectRecipeItem(const uint8_t* item);             // fuer den Reiter "Effekte"
	void NormalizeEffect(RecipeDef& r);                       // Wirkung -> Ergebnis Typ 1, trinkbare Unterart

	// Eigene Modelle (Gegenstandsmodell 0x4EC400: Symbol im Inventar, in der Hand, am Boden)
	bool TagOf(const uint8_t* item, int* id);                // Kennung im Modifier (jeder Typ)
	struct ModelEntry { int id, type, sub; Lab::Structure s; std::string file; };
	long ModelVersion();                                      // aendert sich bei jedem Neuladen
	void Models(std::vector<ModelEntry>& out);                // alle geladenen Ergebnis-Modelle (Kopie)
	std::string ModelPath(const std::string& file);           // relativ -> Strukturen\\datei
}
