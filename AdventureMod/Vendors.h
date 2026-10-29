#pragma once
//
// Vendors.h -- Haendler und Stationen der Staedte (LAB -> Reiter "Haendler")
// ---------------------------------------------------------------
// Eine Datei je Rolle in Strukturen\Haendler\<Rolle>.haendler (Text, key=value):
//   rolle         markt (eigener Haendler auf dem Marktplatz / Mutterbaum / Festung / Bergdorf),
//                 alchemist (am Braustand), huette1..huette5 (Haendlerhaeuser im Handelsviertel)
//   name          Anzeige im LAB
//   klasse        Beruf (Kreatur+0x140): 0x80 Haendler, 0x81..0x83 Dorf-NPC, 0x84 Gastwirt
//   native_waren  1 = Waren wie im Dorf (Generatoren 0x5C9B30 / 0x5CA0A0 / 0x5C8390, nur Klasse 0x80..0x83)
//   zutaten       1 = alle Zutaten der geladenen Rezepte
//   traenke       1 = alle Ergebnisse der geladenen Rezepte (eigene Traenke)
//   gegenstaende  1 = alle aktiven eigenen Gegenstaende (LAB -> Gegenstaende)
//   ware          Typ;Unterart;Material;Level;Seltenheit;Anzahl   oder   item:<Kennung>;Anzahl
//   anzahl        Vorgabe-Anzahl je Ware (sonst 20)
// Katalog: EINE Kategorie { int Anzahl; Item 0x118 } je 0x11C nach Kreatur+0x11DC (Spiel-Allokator 0x68D668) --
// das Spiel kopiert sie beim [R]-Ansprechen in den aktiven Laden (WanderingMerchant.cpp, City.cpp).
//
#include "cwsdk.h"
#include <string>
#include <vector>

namespace Vendors {
	struct Ware { int type = 0x0B, sub = 0, mat = 0, level = 1, rarity = 0, count = 20, item = 0; };   // item > 0: eigener Gegenstand
	struct VendorDef {
		std::string role = "markt", name = "Haendler";
		int  cls = 0x80;
		bool nativeWares = false, ingredients = false, potions = false, items = false;
		int  defaultCount = 20;
		std::vector<Ware> wares;
		std::string file;                    // Laufzeit
	};
	const char* RoleHelp();
	std::string Dir();                       // "Strukturen\\Haendler"
	bool Load(const std::string& path, VendorDef& out, std::string& err);
	bool Save(const std::string& path, const VendorDef& d, std::string& err);
	std::vector<std::string> ListFiles();
	void Reload();                           // jeder Thread; legt beim ersten Mal die Vorgaben an
	bool ForRole(const std::string& role, VendorDef* out);
	// Waren als 0x118-Saetze + Anzahlen (ohne native Waren)
	void BuildWares(const VendorDef& d, std::vector<uint8_t>& items, std::vector<int>& counts);
	// Spielthread: Katalog nach Kreatur+0x11DC schreiben (native Waren vorher ueber den Generator, falls gewuenscht).
	// true, wenn etwas geschrieben wurde.
	bool ApplyTo(cube::GameController* gc, cube::Creature* c, const VendorDef& d);
	std::string Status();
}
