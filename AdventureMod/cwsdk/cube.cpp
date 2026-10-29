#include "cube.h"
#include "../Host.h"

#include <iostream>
#include <Windows.h>
#include "msvc_bincompat.h"
#include "cube_funcs.h"
#include "globals.h"

// GameController-Zeiger-Adresse: 1:1 aus cube_util.cpp (Andoryuuta/cwsdk)
// uebernommen. Cross-verifiziert in der Schwester-Session
// (VillageDungeonTraderMod, siehe docs/RE-Notes.md "ProbeGameController"):
// gc->world.local_player, gc->local_player UND die dortige eigene
// GUID==1-Heuristik zeigten alle auf dieselbe Adresse -- starkes Indiz,
// dass 0x36B1C8 fuer diese Alpha-Version stimmt.
namespace cube {
	GameController* BusywaitForGameController() {
		if (Host::IsServer()) return nullptr;
		auto imageBase = (uint32_t)GetModuleHandleA(NULL);
		GameController** gcp = (GameController**)(Host::ReOff(0x36B1C8));
		while (*gcp == nullptr) {
			Sleep(500);
		}
		return *gcp;
	}

	GameController* GetGameController() {
		if (Host::IsServer()) return nullptr;          // Server.exe hat keinen GameController
		auto imageBase = (uint32_t)GetModuleHandleA(NULL);
		GameController* gc = *(GameController**)(Host::ReOff(0x36B1C8));
		return gc;
	}

	void InitAPI() {
		InitGlobals();
	}
};
