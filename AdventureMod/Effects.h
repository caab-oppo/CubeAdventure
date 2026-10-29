#pragma once
//
// Effects.h -- Wirkungen eigener Traenke (Rezepte mit "wirkung") + Reiter "Effekte" im Handwerksmenue
// ----------------------------------------------------------------------------------------------
// Belegt (RE 2026-10-06, Cube.exe, ImageBase 0x400000):
//  Trinken/Essen
//   * useItem 0x4A2780: Typ 1 -> Kopie nach Kreatur+0x1E8, Aktion +0x68 = 0x50 (Unterart 1/2, trinken)
//     sonst 0x51 (essen), Zeit +0x6C = 0 (int ms). Der Gegenstand wird dabei sofort verbraucht.
//   * Dauer = GameController_itemCooldown 0x4C6B80: Typ 1 Unterart 1 -> 3000 ms, sonst 10000 ms.
//   * Heilwert item_computeValue 0x4C6E10 (thiscall(item), float): Aufrufe 0x6189C1 (Heilen beim
//     Trinken), 0x611214 (Heilen anderer Kreaturen), 0x4A53C9/0x4A5420 (Tooltip "+x HP").
//  Spielerwerte (Kreatur): +0x16C Leben, +0x170 Mana (0..1), +0x1194 Ausdauer (0..1),
//     +0x34/+0x38/+0x3C Geschwindigkeit (z nach oben; Schwerkraft 30/s^2 bei 0x61AC2B),
//     max. Leben 0x444DB0 (thiscall, float; Klemme bei 0x618A7B).
//  Staerke: Waffenschaden 0x43DC60, Faehigkeiten 0x446E80, Zauberkraft 0x43E190 (je thiscall, float)
//  Schutz: Ruestung 0x43CFF0, Widerstand 0x4467A0 (thiscall, float; Abzug in 0x596D30)
//  Schnelligkeit: Laufgeschwindigkeit [ebp-0x2B60] in updateSubsystems; bei 0x619DCC
//     ("push 0xC; mov ecx,esi; call list_findByTypeByte") wird sie fuer esi=Spieler multipliziert.
//  Nachtsicht: Licht-/Himmelsfarben 0x59D640 (thiscall(GC, 7 Arg.), ret 0x1C) lesen die Tageszeit
//     einmal bei 0x59D65B (movd xmm0,[ecx+0x80015C]); dort liest der Mod eine eigene Zeit.
//  Handwerksreiter: Kategorien GC+0x800ADC (vector<vector<Zeile 0x11C>>), gefuellt von
//     computeCraftPrices 0x4A19D0 (thiscall(GC, kat, vector<Rezept 0x128>*), ret 8; waechst mit kat);
//     Reiter-Knoten setzt InventoryWidget 0x4C6140 (thiscall(widget, std::list<Node*>*), ret 4),
//     gebaut in 0x4A1E50 (Klon 0x6326D0, Symbol Modell+0x7F8[+0x7CC] = GC+0x8006C0->+0x4C,
//     list push_back 0x4860B0, Modell auffrischen 0x64AC00). Reiter k zeigt Kategorie k.
//
#include "cwsdk.h"
#include <string>
#include <d3d9.h>

namespace Effects {
	void Install();                               // nach MH_Initialize, vor MH_EnableHook
	void Tick(cube::GameController* gc);          // Spielthread, jedes Bild
	std::string HudText();                        // aktive Wirkungen (leer = keine)
	std::string Status();

	// ---- Partikel (2026-10-27) ---- natives Partikelsystem des Spiels (nur Spielthread) -----------------------------
	// GameController::m_particles = std::list<Particle> bei GC+0x800740 (Aufruf 0x4B2111), push_back 0x4869D0
	// (thiscall(list*, const Particle*), Knoten 0xC0 = 8 + 0xB8). Particle (Infinite-Cubes GameController.h, Offsets
	// am Original 0x4B1F21..0x4B2122 geprueft): +0 Typ (0 Wuerfel, 1 Feuer, 2 Leuchten, 3 ortsfest, 4 Blatt), +4 Alter,
	// +8 Position (3 x int64, Block*65536), +0x20 Startposition, +0x38 Drehung, +0x44 Farbe RGBA, +0x54 Geschwindigkeit,
	// +0x60 Drall, +0x6C Matrix (Einheit), +0xAC Groesse, +0xB0 prallt an Bloecken, +0xB1 ohne Bewegung, +0xB4 Lebenszeit ms.
	// x/y/z in Bloecken.
	void Burst(cube::GameController* gc, float x, float y, float z, float r, float g, float b, int count,
	           float size = 0.3f, float spread = 3.0f, float up = 4.0f, int type = 2, int lifeMs = 1200);
	// Zeitlich laufende Quelle (in Tick abgearbeitet). mode 0 = Funkeln ringsum, 1 = aufsteigende Spirale (Beschwoerung)
	void AddEmitter(float x, float y, float z, float r, float g, float b, int ms, int perSecond, int mode, float radius = 1.2f);
}
