#pragma once
//
// BlacksmithNpc.h
// ---------------------------------------------------------------
// Schmied-NPC in der L-Schmiede jedes Bergdorfs. Steht in der offenen
// Esse-Bucht neben der Waffen-Werkbank. Geht der Spieler nah heran und
// drueckt [K], wird die native "Weapon Customization"-UI geoeffnet
// (Nachbildung des 0x83-Zweigs aus GameController_updateSelectedEntity).
//
// HINWEIS: Das Oeffnen der UI ist ein iterativer RE-Versuch (Offsets aus
// dem Decompile abgeleitet, nicht im Spiel verifiziert). Siehe OpenCustomizationUI.
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <string>

namespace BlacksmithNpc {
	void RegisterWithFramework();

	// Vom Dorfbau aufgerufen: spawnt den Schmied in der L-Schmiede (Haus k==0).
	// cx,cy = Dorfmitte (Block), deckZ = Deckhoehe.
	void OnVillageBuilt(cube::GameController* gc, int cx, int cy, int deckZ);

	// Vom Dorfbau beim Platzieren der Deko aufgerufen, einmal je gesetztem
	// CustomizationBench-Prop (Deko-Typ 0x4D). Blockkoordinaten.
	//
	// Warum ueberhaupt eigene Naeherungserkennung? Die native [R]-Abfrage
	// (GameController_updateSelectedEntity @0x4889E0) laeuft ueber den
	// ENTITY-Pick-Baum. Unsere Deko-Props sind zone_static_objects und
	// tauchen dort nicht auf -- ohne diese Registrierung waere die Werkbank
	// reine Dekoration.
	void OnBenchPlaced(int blockX, int blockY, int blockZ);

	// Overlay-Sprechblase (main.cpp): Text + Weltposition, wenn nah am Schmied.
	bool GetBubble(cube::GameController* gc, std::string& outText,
		int& outBlockX, int& outBlockY, int& outBlockZ);

	// [K] gedrueckt: wenn nah am Schmied -> Waffen-Customization-UI oeffnen.
	void OnUseKey(cube::GameController* gc);

	// Bei Weltwechsel/Reload: Posten vergessen, damit der Schmied neu spawnt.
	void OnWorldReload();
}
