#pragma once
//
// AdventureModFramework.h
// ---------------------------------------------------------------
// Eigener, schlanker "Mod-Launcher im Kleinen" fuer genau die lokale
// Cube World Alpha Version. Siehe README.md fuer die vollstaendige
// Versions-/Offset-Verifikation.
//
// Hintergrund: Der oeffentliche CWSDK-Mod-Launcher-Fork
// (github.com/gijsgroenewegen/Cube-World-Mod-Launcher) bringt zwar
// bequeme Callbacks wie OnZoneGenerated/OnGameTick mit, ist aber gegen
// eine ANDERE Cube-World-Version gebaut (andere CRC, andere Offsets).
// Diese Datei bietet das gleiche Komfort-Prinzip (Mods registrieren
// sich einmal und bekommen fertige Callbacks), aber mit Hook-Adressen,
// die gegen die tatsaechlich vorliegende Cube.exe verifiziert wurden.
//
// Verifizierte Adressen (ImageBase 0x400000):
//   - GenerateZone-Aufrufstelle:  0x0046ACD1  (5 Byte CALL, E8 7A 9B 17 00)
//   - GenerateZone selbst:        0x005E4850  (World::GenerateZone(world*, x, y))
//   Quelle der Offsets: ChrisMiuchiz' "Multithreaded Terrain Generation Mod"
//   (github.com/coremaze/Cube-World-Multithreaded-Terrain-Generation-Mod),
//   gegengeprueft per Hex-Dump der lokalen Cube.exe (siehe README.md).
//
#include "cwsdk.h"
#include <vector>
#include <string>
#include <utility>
#include <cstdint>

namespace AdventureFramework {

	// Basisklasse fuer einen einzelnen "Adventure Mod"-Baustein
	// (z.B. WanderingMerchant, spaeter VillageGenerator, DungeonGenerator, ...).
	// Ein Mod ueberschreibt nur die Callbacks, die er tatsaechlich braucht.
	class IAdventureMod {
	public:
		virtual ~IAdventureMod() {}

		// Einmalig beim allerersten Tick aufgerufen (Globals/GameController
		// stehen zu diesem Zeitpunkt bereits zur Verfuegung).
		virtual void OnInitialize(cube::GameController* gc) {}

		// Einmal pro gerendertem Frame (aktuell ueber den bereits vorhandenen
		// D3D9 EndScene-Hook in main.cpp).
		virtual void OnGameTick(cube::GameController* gc) {}

		// Feuert direkt NACHDEM eine Zone fertig generiert wurde (Hook auf die
		// GenerateZone-Aufrufstelle, siehe oben). world_ptr ist der rohe
		// this-Zeiger aus dem Spiel (als cube::World* gecastet nutzbar),
		// zone_x/zone_y sind die Zonenkoordinaten, zone ist das Ergebnis von
		// world->GetZone(zone_x, zone_y) direkt nach der Generierung.
		//
		// Das ist der zentrale Ansatzpunkt fuer die spaetere Dorf-/Dungeon-
		// Erkennung bzw. -Injektion (siehe README.md, "Naechste Schritte").
		virtual void OnZoneGenerated(cube::GameController* gc, cube::World* world, int zone_x, int zone_y, cube::Zone* zone) {}
	};

	// Registriert einen Mod-Baustein beim Framework. Sollte VOR
	// InstallHooks() aufgerufen werden (z.B. ganz am Anfang von MainThread
	// in main.cpp). Das Framework uebernimmt NICHT den Besitz (kein delete) -
	// der Aufrufer ist fuer die Lebensdauer verantwortlich (in der Praxis:
	// statische/globale Instanzen).
	void RegisterMod(IAdventureMod* mod);

	// Installiert den GenerateZone-Callsite-Hook (fuer OnZoneGenerated).
	// Muss einmal aufgerufen werden, NACHDEM cube::InitGlobals() gelaufen ist.
	// Braucht KEIN MinHook, da hier direkt die 5-Byte-CALL-Instruktion an der
	// Aufrufstelle umgeschrieben wird (klassische Technik, siehe ChrisMiuchiz'
	// Terrain-Mods).
	//
	// SICHERHEITS-HINWEIS (siehe README.md, "Absturz-Fix"): Diese Funktion
	// prueft VOR dem Patchen die 5 Bytes an der Aufrufstelle (muss exakt
	// E8 7A 9B 17 00 sein) und bricht klar geloggt ab, falls sie nicht
	// passen -- schuetzt vor blindem Patchen bei einer abweichenden
	// Cube.exe-Version. WICHTIGER: der Code-Pfad, der durch diesen Hook
	// ausgeloest wird (DispatchZoneGenerated -> World::GetZone()), nutzt
	// intern cube_funcs::world_getzone (imageBase+0x34D10) -- eine Adresse,
	// die NIE gegen dieses Build verifiziert wurde (1:1 aus dem oeffentlichen
	// cwsdk-Repo uebernommen, das vermutlich eine andere Cube-World-Version
	// referenziert). Ein falscher Offset dort wuerde bei JEDER neu
	// generierten Zone ausgefuehrt -- ein sehr plausibler Kandidat fuer
	// haeufige/schnelle Abstuerze. Der Hook war deshalb testweise
	// deaktiviert, ist aber auf Nutzerwunsch wieder AKTIV (siehe
	// kEnableZoneHook in AdventureModFramework.cpp) -- der Verdacht ist,
	// dass der vorherige Absturz stattdessen am selbstgebauten Mod-Launcher
	// lag. Byte-Signatur-Pruefung vor dem Patchen und SEH um jeden riskanten
	// Aufruf bleiben als Sicherheitsnetz aktiv, siehe README.md "Absturz-Fix".
	void InstallZoneGeneratedHook();

	// Muss einmal pro Frame aus dem bestehenden EndScene-Hook aufgerufen
	// werden. Kuemmert sich um OnInitialize (einmalig) und OnGameTick
	// (jeden Frame) fuer alle registrierten Mods.
	void DispatchGameTick(cube::GameController* gc);

	// Gemessene Zeit je Mod-Baustein in OnGameTick (gleitender Mittelwert, ms).
	// Name = Klassenname (RTTI). Fuer die Leistungsanzeige im LAB.
	void GetTickTimings(std::vector<std::pair<std::string, double>>& out);

	// Name des Bausteins, dessen OnGameTick gerade laeuft (fuer Absturzmeldungen).
	const char* CurrentModName();
}
