#pragma once
#include <cstdint>

// Nicht vollstaendig aufgeloest im Original-SDK (Unterfelder als rohe
// Byte-Bloecke) -- fuer unser Projekt aktuell nicht direkt genutzt, cube::World
// haelt nur "Region*"-ZEIGER (main_regions/unk_regions[1024][1024]), keine
// Region-Werte -- deshalb reicht ein reiner Byte-Platzhalter in der
// bestaetigten Gesamtgroesse (0x15A28), OHNE einzelne (geratene) Unterfelder.
//
// HINWEIS: eine fruehere Version dieser Datei versuchte, die Groesse aus
// einer angenommenen Feldaufteilung (Header + 4096x16-Byte-Subfelder +
// 4096 Zone-Zeiger + 64x1354-Byte-Subfelder) zusammenzusetzen -- das ergab
// beim Nachrechnen KEINE ganzzahlige Loesung und war schlicht falsch
// (fuehrte zum C2338 assert_size-Build-Fehler). Da wir Region-Felder
// ohnehin nirgends lesen, ist der reine Byte-Platzhalter unten korrekt UND
// sicherer als eine geratene, falsche Feldaufteilung.
namespace cube {
	struct Zone;

	struct Region
	{
		uint8_t raw[0x15A28];
	};
};
