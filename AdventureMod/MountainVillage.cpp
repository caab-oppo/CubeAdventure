#include "MountainVillage.h"
#include "ModDb.h"
#include "Host.h"
#include "SeedScout.h" // Offline-Feature-Grid (Doerfer/Dungeons/Plateaus...) aus dem Seed
#include "WanderingMerchant.h" // unser eigener Haendler (SpawnMerchantAt)
#include "AirshipTaxi.h"       // Turm-Taxi-Vendor je Dorf (OnVillageBuilt)
#include "BlacksmithNpc.h"     // Schmied-NPC + Waffen-Werkbank je Dorf
#include "NativeHouseProbe.h"  // native Haus- und Schmiedemodelle
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>

// ===================================================================
// MountainVillage.cpp -- eigenes Bergdorf mit begehbarem Lufthafen-Turm.
// STRG+0 plant den Bau an der Spielerposition; Bloecke werden haeppchenweise
// pro Frame gesetzt (Queue) und danach per Chunk-needs_update sichtbar gemacht.
//
// PERSISTENZ: Cube World regeneriert Zonen aus dem Seed -> unsere SetBlock-
// Aenderungen gehen beim Neuladen verloren. Deshalb merken wir die Dorf-
// Standorte in einer Datei und bauen sie automatisch neu, sobald der Spieler
// wieder in der Naehe ist und die Struktur fehlt.
// Alle Koordinaten in BLOECKEN (1 Block = 65536 Einheiten).
// ===================================================================

namespace MountainVillage {

	static const double kUnitsPerBlock = 65536.0;

	// ---- Ablaufspur ---------------------------------------------------------
	// Bei einem Absturz ohne Debugger ist die LETZTE Zeile im Log der Beweis,
	// wo es passiert ist. Deshalb jede Zeile sofort rausschreiben (flush) --
	// gepufferte Ausgabe geht beim Absturz verloren und zeigt einen falschen
	// Ort an.
	static void Spur(const char* was) {
		std::cout << "[Spur] " << was << std::endl;
		std::cout.flush();
	}
	static void SpurN(const char* was, int n) {
		std::cout << "[Spur] " << was << " = " << n << std::endl;
		std::cout.flush();
	}

	static const int    kBlocksPerFrame = 3000; // schneller Aufbau (~0.5s statt 5s); Remesh pro Frame
	static const char* kSitesFile = "AdventureMod_villages.txt";

	struct BlockOp { int x, y, z; uint8_t r, g, b, t; };
	static std::vector<BlockOp> g_queue;
	static size_t g_qi = 0;
	static int    g_placed = 0, g_failed = 0;
	static bool   g_building = false;
	static bool   g_instant = false; // Wiederaufbau beim Neuladen: alles in einem Frame
	static int    g_buildCx = 0, g_buildCy = 0; // Zentrum des aktuellen Baus (fuer gezielten Remesh)
	static int    g_buildDeckZ = 0;             // Deck-Hoehe des aktuellen Baus (fuer NPC-Spawn)
	// NPC-Spawn wird ~2s nach Baufertigstellung ausgeloest (sonst sind die Bloecke
	// im Client-Mesh noch nicht "fest" -> Bewohner stecken im Fels).
	//
	// WARTESCHLANGE statt Einzelplatz: frueher lag hier nur EIN offener Auftrag
	// (g_npcSpawnAt/g_npcCx/...). Sobald in den 2 s Wartezeit ein zweites Dorf zu
	// bauen begann -- was der Marker-Scan und das 5x5-Zellenraster staendig tun --
	// wurde der offene Auftrag ueberschrieben und die Bewohner des ersten Dorfes
	// sind nie erschienen. Genau das ist die Ursache der ausbleibenden Spawns.
	struct NpcJob { DWORD due; int cx, cy, deckZ; };
	static std::vector<NpcJob> g_npcJobs;       // alle offenen Spawn-Auftraege

	// Native Deko-Props, die beim aktuellen Bau angehaengt werden (Haus-Innenausstattung).
	// x,y,z = Weltblock; sxy = Horizontalskala (Objekt +0x24), sz = Hoehe (Objekt +0x2C).
	struct DecoReq { int type, x, y, z, orient; float sxy, sz; };
	static std::vector<DecoReq> g_decoReqs;
	// Hauspositionen des aktuellen Baus (fuer Marktstaende an den Aussenwaenden).
	// style/rot werden nur beim nativen Hausbau gebraucht.
	struct HousePos { int hx, hy; int model; int rot; };
	static std::vector<HousePos> g_houses;

	struct Site { int x, y, deckZ; };
	static std::vector<Site> g_sites;
	static bool g_loaded = false;

	// Phase 1: echte native Gebaeudeobjekte aus Siedlungen abfangen (nur lesen).
	static int     g_capCount = 0;          // wie viele Siedlungszonen schon gefangen
	static uint8_t g_capbuf[0x188];
	static uint8_t g_capbuf2[0x188];
	static int     g_capTypes[64];
	static int     g_capTypeN = 0;

	// Phase 2: native Gebaeude (Typen 52-55) rund um den Turm platzieren.
	struct NBuild { int x, y, z, type; };
	static std::vector<NBuild> g_nbuilds;

	// WELTGEN: deterministisch Bergdoerfer beim Zonen-Generieren spawnen.
	static std::vector<std::pair<int, int> > g_pending; // Zonen-Zentren, noch zu bauen
	static const uint32_t kVillageRarity = 1;  // ~1 von N Zonen ist Dorf-Kandidat (nicht auf 1 setzen -> Ueberlast!)
	static const int      kMountainMinZ = 35;  // nur auf hohem Terrain (Berge)
	static const int      kMaxSpread = 70;  // max. Hoehenunterschied im Umkreis (steilere Haenge -> uebersprungen)
	static const int      kFillDepth = 70;  // Fundament talseitig bis so tief hinunter (bis zum Boden, kein Schweben)
	static const int      kClearHeight = 50;  // bergseitig Fels bis so hoch ueber Deck wegschneiden (Hof/Haeuser)
	static const int      kTowerClear = 48;  // im Turmbereich hoeher wegschneiden (Turm ist hoch)

	// ---- Native Haeuser statt Blocknachbau ---------------------------------
	// true  = Haeuser und Schmiede kommen aus den nativen Modellgruppen
	//         (NativeHouseProbe::PlaceBuilding -- fertige Ganzgebaeude).
	// false = alter Blocknachbau (PlanHouse/PlanSmithy).
	// Die Teile werden jetzt ueber ihren Modellnamen gesucht (framework-wall.cub,
	// stone-roof1.cub, ...) statt ueber geratene Registry-Indizes -- der erste
	// Versuch legte neunmal dieselbe Vorlage ins Raster und ergab lose Waende.
	static const bool kUseNativeHouses = true;

	// ---- Lageplan: Wegenetz -------------------------------------------------
	static const int    kPathSpokes = 8;    // sternfoermige Wege vom Turm nach aussen
	static const double kPathHalfWidth = 1.8;  // halbe Wegbreite in Bloecken

	// ---- Lageplan: Objekttypen ----------------------------------------------
	// Belegt (im Spiel bereits sichtbar bestaetigt):
	//   0x15/0x16/0x17 Marktstaende, 0x2F/0x30/0x31 Blumen, 0x32 Laterne/Fackel,
	//   0x18 Fass, 0x19/0x1A Kiste, 0x4D CustomizationBench.
	// Geraten (aus dem Dorfgenerator, noch nicht im Bild verifiziert) -- falls
	// etwas Falsches erscheint, hier eine ID aendern, sonst nirgends.
	static const int kTypeTorch = 0x32; // bestaetigt
	static const int kTypeFlowerA = 0x2f; // bestaetigt
	static const int kTypeFlowerB = 0x30; // bestaetigt
	static const int kTypeFlowerC = 0x31; // bestaetigt
	static const int kTypeStallA = 0x15; // bestaetigt (spawnRandomObject3 wuerfelt 0x15..0x17)
	static const int kTypeStallB = 0x16; // bestaetigt
	static const int kTypeStallC = 0x17; // bestaetigt
	static const int kTypeTree = 0x0c; // GERATEN -- groesste Grundflaeche im Generator (3.0/3.0)
	static const int kTypeBench = 0x13; // GERATEN -- laenglich (2.0/3.0/1.0), passt zu einer Bank
	// Amboss ist 0x48 -- steht schon so in PlanSmithy und ist sichtbar korrekt.
	// Die Nachbarn stammen aus dem Interaktionsschalter in GameController::vfunc_10
	// (@0x488EE0): jeder Typ hat dort eine eigene Aufforderungszeile, ist also eine
	// eigenstaendige Station. Welche welche ist, zeigt der erste Testlauf.
	static const int kTypeAnvil = 0x48; // bestaetigt (bisheriger Amboss der Schmiede)
	static const int kStations[] = { 0x47, 0x49, 0x4a, 0x4b, 0x4c }; // uebrige Stationen
	static const int      kBlend = 25;  // Breite des auslaufenden Rings um die Plattform (Spitzen abtragen)
	static const int      kBlendCap = 50;  // max. Abtrag-Hoehe pro Spalte im Blend-Ring (Volumen begrenzen)
	static std::vector<std::pair<int, int> > g_villages; // gebaute Doerfer (Zentren)
	static void RecordVillage(int cx, int cy); // Vorwaertsdeklaration

	// --- Farben (Type 1 = fester Block, Type 0 = Luft) ---
	static void P(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t t) {
		BlockOp o; o.x = x; o.y = y; o.z = z; o.r = r; o.g = g; o.b = b; o.t = t;
		g_queue.push_back(o);
	}
	static void Stone(int x, int y, int z) { P(x, y, z, 112, 110, 120, 1); }
	static void Deck(int x, int y, int z) { P(x, y, z, 165, 120, 70, 1); }
	static void DeckEdge(int x, int y, int z) { P(x, y, z, 120, 82, 46, 1); }
	static void Rail(int x, int y, int z) { P(x, y, z, 92, 62, 36, 1); }
	static void Wall(int x, int y, int z) { P(x, y, z, 228, 220, 196, 1); } // Creme
	static void Roof(int x, int y, int z) { P(x, y, z, 52, 64, 82, 1); }   // Dunkelblau
	static void Beam(int x, int y, int z) { P(x, y, z, 92, 68, 44, 1); }   // Balken/Pfosten
	static void Pane(int x, int y, int z) { P(x, y, z, 150, 190, 210, 1); } // Fensterglas
	static void Lantern(int x, int y, int z) { P(x, y, z, 255, 225, 140, 1); }// Laterne
	static void Floor(int x, int y, int z) { P(x, y, z, 140, 100, 60, 1); }
	static void TWall(int x, int y, int z) { P(x, y, z, 150, 150, 162, 1); }
	static void Step(int x, int y, int z) { P(x, y, z, 150, 100, 55, 1); } // Holz
	static void Frame(int x, int y, int z) { P(x, y, z, 90, 60, 35, 1); }
	static void Post(int x, int y, int z) { P(x, y, z, 130, 130, 140, 1); } // Mittelpfosten
	static void Air(int x, int y, int z) { P(x, y, z, 0, 0, 0, 0); }
	static void Path(int x, int y, int z) { P(x, y, z, 122, 116, 104, 1); } // Pflaster/Weg
	static void Water(int x, int y, int z) { P(x, y, z, 70, 130, 200, 1); }  // Brunnenwasser
	static void Flower(int x, int y, int z) { P(x, y, z, 210, 90, 90, 1); }   // Blumen/Beet
	static void Leaf(int x, int y, int z) { P(x, y, z, 74, 140, 70, 1); }   // Laub
	// Blumenkasten: Holzrand mit farbigem Beet obenauf.
	static void Planter(int x, int y, int z) { Beam(x, y, z); Flower(x, y, z + 1); }
	// Kleiner Baum: Stamm + Laubkrone.
	static void Tree(int x, int y, int z) {
		for (int k = 0; k < 4; k++) Beam(x, y, z + k);
		for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) { Leaf(x + dx, y + dy, z + 4); Leaf(x + dx, y + dy, z + 5); }
		Leaf(x, y, z + 6);
	}
	// Zierbrunnen (5x5) mit Steinrand und Wasser.
	static void Well(int cx, int cy, int z) {
		for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) {
			bool rim = (dx == -2 || dx == 2 || dy == -2 || dy == 2);
			if (rim) { Stone(cx + dx, cy + dy, z); Stone(cx + dx, cy + dy, z + 1); }
			else Water(cx + dx, cy + dy, z);
		}
		// vier Eckpfosten + Dach
		for (int s = 0; s < 2; s++) for (int dx = -2; dx <= 2; dx += 4) for (int dy = -2; dy <= 2; dy += 4) Beam(cx + dx, cy + dy, z + 2 + s);
		for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) Roof(cx + dx, cy + dy, z + 4);
	}
	// Stufe/Plattform mit garantierter Kopffreiheit: 3 Bloecke Luft darueber.
	static void StepH(int x, int y, int z) { Step(x, y, z); Air(x, y, z + 1); Air(x, y, z + 2); Air(x, y, z + 3); Air(x, y, z + 4); }
	static void LandH(int x, int y, int z) { Deck(x, y, z); Air(x, y, z + 1); Air(x, y, z + 2); Air(x, y, z + 3); Air(x, y, z + 4); }

	// --- Terrain lesen / Block setzen ---
	static int SurfaceZ(cube::World* w, int x, int y, int topZ, int botZ) {
		__try {
			for (int z = topZ; z >= botZ; z--) {
				BlockColor* c = w->GetBlock(x, y, z, (cube::Zone*)nullptr);
				if (c && c->Type != 0) return z;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return -1000000;
	}
	static bool PutBlock(cube::World* w, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t t) {
		__try { BlockColor col(r, g, b, t); w->SetBlock(x, y, z, &col, (cube::Zone*)nullptr); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool IsSolidAt(cube::World* w, int x, int y, int z) {
		__try { BlockColor* c = w->GetBlock(x, y, z, (cube::Zone*)nullptr); return c && c->Type != 0; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Tuer (2 breit, 3 hoch) mit Rahmen + Vordach; (tx,ty)=Wandtangente, (nx,ny)=aussen.
	static void DoorPorch(int wx, int wy, int fz, int tx, int ty, int nx, int ny) {
		for (int i = 0; i < 2; i++) for (int z = 0; z < 3; z++) Air(wx + tx * i, wy + ty * i, fz + z);
		for (int z = 0; z < 3; z++) { Frame(wx - tx, wy - ty, fz + z); Frame(wx + tx * 2, wy + ty * 2, fz + z); }
		Frame(wx, wy, fz + 3); Frame(wx + tx, wy + ty, fz + 3);
		// Vordach (2 Bloecke raus) + zwei Pfosten
		for (int o = 1; o <= 2; o++) for (int i = -1; i <= 2; i++) Roof(wx + tx * i + nx * o, wy + ty * i + ny * o, fz + 4);
		for (int z = 0; z < 4; z++) { Beam(wx - tx + nx * 2, wy - ty + ny * 2, fz + z); Beam(wx + tx * 2 + nx * 2, wy + ty * 2 + ny * 2, fz + z); }
	}

	// --- Moebel/Deko (aus den Referenzbildern) ---
	static void Vase(int x, int y, int z) { P(x, y, z, 235, 236, 240, 1); }  // weisse Vase
	static void FurnTable(int x, int y, int z) {           // 3x2 Tisch: 4 Beine + Holzplatte
		Beam(x, y, z); Beam(x + 2, y, z); Beam(x, y + 1, z); Beam(x + 2, y + 1, z);
		for (int dx = 0; dx < 3; dx++) for (int dy = 0; dy < 2; dy++) Step(x + dx, y + dy, z + 1);
	}
	static void FurnBench(int x, int y, int z, int len, bool horiz) {         // niedrige Bank
		for (int i = 0; i < len; i++) {
			int bx = horiz ? x + i : x, by = horiz ? y : y + i;
			if (i == 0 || i == len - 1) Beam(bx, by, z);
			Step(bx, by, z + 1);
		}
	}
	static void FurnWardrobe(int x, int y, int z) {                          // 2x1x3 Schrank
		for (int dz = 0; dz < 3; dz++) { Frame(x, y, z + dz); Frame(x + 1, y, z + dz); }
		Lantern(x, y, z + 1); // heller Griff-Akzent
	}
	static void FurnPlant(int x, int y, int z) { Vase(x, y, z); Leaf(x, y, z + 1); } // Pflanze in Vase
	static void FurnShelf(int x, int y, int z, int len, bool horiz) {        // Regal mit bunten Waren
		for (int lvl = 0; lvl < 2; lvl++) {
			int zz = z + 1 + lvl * 2;
			for (int i = 0; i < len; i++) {
				int bx = horiz ? x + i : x, by = horiz ? y : y + i;
				Step(bx, by, zz);
				if (i == 1) P(bx, by, zz + 1, (uint8_t)(80 + lvl * 90), 150, (uint8_t)(200 - lvl * 90), 1);
			}
		}
	}
	static void WallLamp(int x, int y, int z, int nx, int ny) {              // Wandlaterne (nx,ny = ins Zimmer)
		Beam(x, y, z);
		Lantern(x + nx, y + ny, z);
	}

	// --- Haus im Referenz-Stil: Creme-Waende, dunkelblaues Satteldach mit Ueberstand,
	// Tuer mit Vordach (zur gewuenschten Seite face 0=-y,1=+x,2=+y,3=-x), Fenster. ---
	// === L-SCHMIEDE nach Bildvorlage: geschlossener Hauptraum + OFFENE Esse-Bucht ===
	static void PlanSmithy(int cx, int cy, int fz, int face) {
		const int mw = 15, md = 18, h = 10;   // Hauptraum
		const int bw = 11, bd = 12, bh = 7;   // offene Bucht (rechts, niedriger)
		int bx = cx + mw - 1;                 // teilt die Ostwand des Hauptraums
		int by = cy + md - bd;                // buendig zur Rueckseite; Front offen
		(void)face;
		// Fundament der gesamten L-Flaeche
		for (int x = 0; x < mw; x++) for (int y = 0; y < md; y++) Stone(cx + x, cy + y, fz - 1);
		for (int x = 0; x < bw; x++) for (int y = 0; y < bd; y++) Stone(bx + x, by + y, fz - 1);
		// Hauptraum-Waende
		for (int z = 0; z < h; z++) {
			for (int x = 0; x < mw; x++) { Wall(cx + x, cy, fz + z); Wall(cx + x, cy + md - 1, fz + z); }
			for (int y = 0; y < md; y++) { Wall(cx, cy + y, fz + z); Wall(cx + mw - 1, cy + y, fz + z); }
		}
		// Hauptraum-Satteldach (First entlang x) + Giebelfuellung
		for (int y = -1; y <= md; y++) {
			int yc = y < 0 ? 0 : (y > md - 1 ? md - 1 : y);
			int lvl = (yc < md - 1 - yc) ? yc : (md - 1 - yc);
			for (int x = -1; x <= mw; x++) Roof(cx + x, cy + y, fz + h + lvl);
			if (y >= 0 && y <= md - 1) for (int z = 0; z < lvl; z++) { Wall(cx, cy + y, fz + h + z); Wall(cx + mw - 1, cy + y, fz + h + z); }
		}
		// Fenster + Tuer + Vordach an der Vorderseite (-y) des Hauptraums
		for (int k = -1; k <= 1; k += 2) { int xx = cx + mw / 2 + k * 3; Air(xx, cy, fz + 3); Air(xx, cy, fz + 4); Frame(xx, cy, fz + 2); Frame(xx, cy, fz + 5); }
		DoorPorch(cx + mw / 2 - 1, cy, fz, 1, 0, 0, -1);
		// Schornstein des Hauptraums (Stein) durchs Dach
		for (int z = 0; z <= h + 5; z++) { Stone(cx + 2, cy + md - 3, fz + z); Stone(cx + 3, cy + md - 3, fz + z); }

		// === Offene Esse-Bucht (rechts angebaut) ===
		// Boden auf fz-1 (Fundament-Ebene) -> alles darueber (fz+) sitzt OBEN AUF,
		// steckt also nicht mehr im Boden. Gleiche Lauf-Ebene wie der Hauptraum.
		for (int x = 0; x < bw; x++) for (int y = -1; y < bd; y++) Deck(bx + x, by + y, fz - 1);
		for (int z = 0; z < bh; z++) for (int x = 0; x < bw; x++) Wall(bx + x, by + bd - 1, fz + z); // Rueckwand
		// Pultdach: vorne bh, hinten bh+3 (leicht geneigt), mit Ueberstand
		for (int x = -1; x <= bw; x++) for (int y = -1; y <= bd; y++) {
			int yy = y < 0 ? 0 : (y > bd - 1 ? bd - 1 : y);
			int rise = (yy * 3) / (bd - 1);
			Roof(bx + x, by + y, fz + bh + rise);
		}
		// Pfosten: vom Boden (fz) DURCHGEHEND bis unter das Pultdach an ihrer y-Position
		// (kein Schweben, keine Luecke -> Balken gehen bis zur Decke).
		{
			int postXY[5][2] = { { bx, by }, { bx + bw / 2, by }, { bx + bw - 1, by },
				{ bx + bw - 1, by + bd / 2 }, { bx + bw - 1, by + bd - 1 } };
			for (int i = 0; i < 5; i++) {
				int py = postXY[i][1];
				int yy = py - by; if (yy < 0) yy = 0; if (yy > bd - 1) yy = bd - 1;
				int top = bh + (yy * 3) / (bd - 1);          // Dachhoehe an dieser Stelle
				for (int z = 0; z <= top; z++) Beam(postXY[i][0], py, fz + z);
			}
		}

		// === Esse: Steinkuppel mit Bogen + Glut + Schornstein an der Rueckwand ===
		int fxc = bx + bw / 2, fyc = by + bd - 3;
		for (int x = -1; x <= 1; x++) for (int y = 0; y < 2; y++) { Stone(fxc + x, fyc + y, fz); Stone(fxc + x, fyc + y, fz + 1); }
		P(fxc, fyc, fz + 1, 245, 130, 35, 1); Air(fxc, fyc, fz + 2);          // Bogenoeffnung + Glut
		for (int z = fz + 2; z <= fz + bh + 2; z++) for (int x = -1; x <= 1; x++) Stone(fxc + x, fyc + 1, z);
		for (int z = fz + 2; z <= fz + bh + 6; z++) Stone(fxc, fyc + 1, z);   // Schornstein durchs Dach
		Air(fxc, fyc + 1, fz + bh + 6);

		// === Stationen wie in der nativen Schmiede ===
		// In den nativen Doerfern stehen die Handwerksstationen als eigene
		// 0x188-Objekte um die Esse herum, nicht als Blockmoebel. Genau das machen
		// wir hier: Amboss und Loeschfass direkt an der Esse, die Waffen-Werkbank
		// frei zugaenglich vorne, die uebrigen Stationen an der Seitenwand.
		g_decoReqs.push_back({ kTypeAnvil, fxc - 3, by + bd - 3, fz, 0, 1.5f, 1.0f }); // Amboss
		g_decoReqs.push_back({ 0x18,       fxc + 2, by + bd - 3, fz, 0, 1.0f, 1.0f }); // Fass (Loeschwasser)
		g_decoReqs.push_back({ 0x4d,       bx + bw / 2, by + 3,  fz, 0, 2.0f, 1.0f }); // CustomizationBench
		// Uebrige native Stationen an der linken Innenwand aufreihen. Steht eine
		// davon falsch oder doppelt, genuegt es, sie aus kStations zu nehmen.
		{
			const int n = (int)(sizeof(kStations) / sizeof(kStations[0]));
			for (int i = 0; i < n; i++) {
				int sxp = bx + 2;
				int syp = by + 5 + i * 3;
				if (syp > by + bd - 4) break;             // nicht in die Esse hineinbauen
				g_decoReqs.push_back({ kStations[i], sxp, syp, fz, 0, 1.5f, 1.0f });
			}
		}
	}

	static void PlanHouse(int cx, int cy, int fz, int w, int d, int h, int face, bool smithy = false) {
		for (int x = 0; x < w; x++) for (int y = 0; y < d; y++) Stone(cx + x, cy + y, fz - 1);
		for (int z = 0; z < h; z++) {
			for (int x = 0; x < w; x++) { Wall(cx + x, cy, fz + z); Wall(cx + x, cy + d - 1, fz + z); }
			for (int y = 0; y < d; y++) { Wall(cx, cy + y, fz + z); Wall(cx + w - 1, cy + y, fz + z); }
		}
		// Satteldach (First entlang x), Ueberstand auf allen Seiten
		for (int y = -1; y <= d; y++) {
			int yc = y < 0 ? 0 : (y > d - 1 ? d - 1 : y);
			int lvl = (yc < d - 1 - yc) ? yc : (d - 1 - yc);
			for (int x = -1; x <= w; x++) Roof(cx + x, cy + y, fz + h + lvl);
			if (y >= 0 && y <= d - 1) for (int z = 0; z < lvl; z++) { Wall(cx, cy + y, fz + h + z); Wall(cx + w - 1, cy + y, fz + h + z); }
		}
		// Fenster (OFFEN, mit Luft) an allen vier Seiten, mit Holzrahmen, bis zu zwei Reihen.
		int winx[3] = { cx + w / 4, cx + w / 2, cx + (3 * w) / 4 };
		int winy[3] = { cy + d / 4, cy + d / 2, cy + (3 * d) / 4 };
		int rows[2] = { fz + 2, fz + 6 };
		int nrows = (h >= 8) ? 2 : 1;
		for (int r = 0; r < nrows; r++) {
			int pz = rows[r];
			// Laengsseiten (an y=0 und y=d-1)
			for (int k = 0; k < 3; k++) {
				int xx = winx[k];
				Air(xx, cy, pz); Air(xx, cy, pz + 1);
				Air(xx, cy + d - 1, pz); Air(xx, cy + d - 1, pz + 1);
				Frame(xx - 1, cy, pz - 1); Frame(xx + 1, cy, pz - 1); Frame(xx, cy, pz + 2);
				Frame(xx - 1, cy + d - 1, pz - 1); Frame(xx + 1, cy + d - 1, pz - 1); Frame(xx, cy + d - 1, pz + 2);
			}
			// Giebelseiten (an x=0 und x=w-1)
			for (int k = 0; k < 3; k++) {
				int yy = winy[k];
				Air(cx, yy, pz); Air(cx, yy, pz + 1);
				Air(cx + w - 1, yy, pz); Air(cx + w - 1, yy, pz + 1);
			}
		}
		// Tuer + Vordach auf der gewuenschten Seite
		if (face == 0)      DoorPorch(cx + w / 2 - 1, cy, fz, 1, 0, 0, -1);
		else if (face == 2) DoorPorch(cx + w / 2 - 1, cy + d - 1, fz, 1, 0, 0, 1);
		else if (face == 1) DoorPorch(cx + w - 1, cy + d / 2 - 1, fz, 0, 1, 1, 0);
		else                DoorPorch(cx, cy + d / 2 - 1, fz, 0, 1, -1, 0);

		// --- Innenausstattung: NATIVE Deko-Props (keine Block-Moebel), sauber platziert ---
		// z = fz (Boden liegt bei fz-1). Werden in PlaceVillageDeco angehaengt.
		if (smithy) {
			// SCHMIEDE mit eigenem Design (Bild-Vorlage): gemauerte STEINESSE + SCHORNSTEIN.
			int hx0 = cx + 2, hy0 = cy + d - 4; // Herd hinten-links
			for (int x = 0; x < 3; x++) for (int y = 0; y < 2; y++) { Stone(hx0 + x, hy0 + y, fz); Stone(hx0 + x, hy0 + y, fz + 1); }
			P(hx0 + 1, hy0, fz + 1, 245, 130, 35, 1);   // Glut/Feuer in der Esse
			Air(hx0 + 1, hy0, fz + 2);                  // Essenoeffnung
			for (int z = fz + 2; z <= fz + 4; z++) for (int x = 0; x < 3; x++) Stone(hx0 + x, hy0 + 1, z); // Rueckwand
			// Schornstein (2x2 Stein) durch das Dach nach oben.
			for (int z = fz + 2; z <= fz + h + 7; z++) for (int x = 0; x < 2; x++) for (int y = 0; y < 2; y++) Stone(hx0 + x, hy0 + y, z);
			for (int x = 0; x < 2; x++) for (int y = 0; y < 2; y++) Air(hx0 + x, hy0 + y, fz + h + 7); // Rauchabzug offen
			// Waffen-Werkbank (CustomizationBench) mittig + Amboss + Saegebank + Wasserfass.
			g_decoReqs.push_back({ 0x4d, cx + w / 2 - 1, cy + d / 2 - 1, fz, 0, 2.0f, 1.0f }); // CustomizationBench
			g_decoReqs.push_back({ 0x48, cx + 6,         cy + d - 3,     fz, 0, 1.5f, 1.0f }); // Anvil (Amboss)
			g_decoReqs.push_back({ 0x4b, cx + w - 3,     cy + 2,         fz, 0, 1.5f, 1.0f }); // SawBench
			g_decoReqs.push_back({ 0x18, cx + w - 4,     cy + d - 3,     fz, 0, 1.0f, 1.0f }); // Barrel (Loeschwasser)
		}
		else {
			// Wohnhaus: Tisch 4 breit/1 hoch mittig + Stuehle, Schrank + Regal (von der Wand weg), Bank, Vase.
			g_decoReqs.push_back({ 0x0c, cx + w / 2 - 1, cy + d / 2 - 1, fz, 0, 4.0f, 1.0f }); // Tisch (mittig)
			g_decoReqs.push_back({ 0x0f, cx + w / 2 - 2, cy + d / 2 - 1, fz, 0, 1.0f, 1.0f }); // Stuhl
			g_decoReqs.push_back({ 0x0f, cx + w / 2,     cy + d / 2 - 1, fz, 0, 1.0f, 1.0f }); // Stuhl
			g_decoReqs.push_back({ 0x1d, cx + 2,         cy + d - 3,     fz, 0, 2.0f, 3.0f }); // Schrank
			g_decoReqs.push_back({ 0x12, cx + 3,         cy + 2,         fz, 0, 2.0f, 1.0f }); // Bank
			g_decoReqs.push_back({ 0x20, cx + w - 3,     cy + d - 3,     fz, 0, 1.5f, 2.0f }); // Regal
			g_decoReqs.push_back({ 0x38, cx + w - 3,     cy + 2,         fz, 0, 1.0f, 1.0f }); // Vase
		}
	}

	// --- Strassenlaterne ---
	static void PlanLamp(int x, int y, int z) {
		for (int k = 0; k < 5; k++) Beam(x, y, z + k);
		Lantern(x, y, z + 5);
		Beam(x, y, z + 6);
	}

	// --- Achteck-Test ---
	static bool inOct(int dx, int dy) { const int R = 16, K = 26; int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy; return ax <= R && ay <= R && ax + ay <= K; }

	// --- Achteckiger, hohler Turm (32 breit, 40 hoch) mit 3-breiter Holz-
	// Wendeltreppe um einen Mittelpfosten, Plattform an jeder Ecke,
	// Bogentor unten, begehbarem Dach mit Reling + Luke oben. ---
	static void PlanTower(cube::World* w, int cx, int cy, int deckZ) {
		const int R = 16, height = 40;
		int topZ = deckZ + height;
		for (int dx = -R; dx <= R; dx++) for (int dy = -R; dy <= R; dy++) {
			if (!inOct(dx, dy)) continue;
			int x = cx + dx, y = cy + dy;
			bool wall = !(inOct(dx + 1, dy) && inOct(dx - 1, dy) && inOct(dx, dy + 1) && inOct(dx, dy - 1));
			Floor(x, y, deckZ);
			if (wall) {
				int s = SurfaceZ(w, x, y, deckZ + 2, deckZ - kFillDepth);
				if (s <= -999999 || s < deckZ - kFillDepth) s = deckZ - kFillDepth;
				for (int z = s + 1; z < deckZ; z++) Stone(x, y, z);         // Fundament bis Boden
				for (int z = deckZ + 1; z <= topZ; z++) TWall(x, y, z);     // Wand ueberschreibt Fels
				Rail(x, y, topZ + 1);
			}
			else {
				// Turminneres vom Fels befreien; die Treppe wird spaeter darueber gesetzt.
				for (int z = deckZ + 1; z < topZ; z++) Air(x, y, z);
			}
			Floor(x, y, topZ);
		}
		// Bogentor in der Vorderwand (dy = -R)
		for (int dz = 0; dz < 9; dz++) {
			int zz = deckZ + 1 + dz;
			int hw = 2; if (dz >= 7) hw = 0; else if (dz >= 5) hw = 1;
			for (int dxx = -hw; dxx <= hw; dxx++) Air(cx + dxx, cy - R, zz);
		}
		for (int dz = 0; dz < 8; dz++) { Frame(cx - 3, cy - R, deckZ + 1 + dz); Frame(cx + 3, cy - R, deckZ + 1 + dz); }
		Frame(cx - 2, cy - R, deckZ + 9); Frame(cx + 2, cy - R, deckZ + 9); Frame(cx - 1, cy - R, deckZ + 10); Frame(cx, cy - R, deckZ + 10); Frame(cx + 1, cy - R, deckZ + 10);

		// --- Wendeltreppe: Mittelpfosten (3x3) + 3-breite Stufen + Eck-Plattform ---
		const int Rs = 4;
		int scx = cx, scy = cy - 4;
		for (int z = deckZ + 1; z <= topZ; z++)
			for (int pdx = -1; pdx <= 1; pdx++) for (int pdy = -1; pdy <= 1; pdy++) Post(scx + pdx, scy + pdy, z);
		int cxs[4] = { -Rs, Rs, Rs, -Rs }, cys[4] = { -Rs, -Rs, Rs, Rs };
		int h = 0, lastx = scx - Rs, lasty = scy - Rs; bool done = false;
		for (int side = 0; side < 40 && !done; side++) {
			int i = side % 4;
			int c0x = cxs[i], c0y = cys[i], c1x = cxs[(i + 1) % 4], c1y = cys[(i + 1) % 4];
			int zc = deckZ + 1 + h;
			int sx = (c0x < 0) ? 1 : -1, sy = (c0y < 0) ? 1 : -1; // Plattform zur Mitte
			for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) LandH(scx + c0x + a * sx, scy + c0y + b * sy, zc);
			lastx = scx + c0x; lasty = scy + c0y;
			if (zc >= topZ - 1) { done = true; break; }
			if (i == 0) { int sc = 0; for (int x = c0x + 1; x <= c1x - 1; x++) { int z = deckZ + 1 + h; StepH(scx + x, scy - Rs, z); StepH(scx + x, scy - Rs + 1, z); StepH(scx + x, scy - Rs + 2, z); lastx = scx + x; lasty = scy - Rs; if (++sc % 2 == 0) h++; if (deckZ + 1 + h >= topZ - 1) { done = true; break; } } }
			else if (i == 1) { int sc = 0; for (int y = c0y + 1; y <= c1y - 1; y++) { int z = deckZ + 1 + h; StepH(scx + Rs, scy + y, z); StepH(scx + Rs - 1, scy + y, z); StepH(scx + Rs - 2, scy + y, z); lastx = scx + Rs; lasty = scy + y; if (++sc % 2 == 0) h++; if (deckZ + 1 + h >= topZ - 1) { done = true; break; } } }
			else if (i == 2) { int sc = 0; for (int x = c0x - 1; x >= c1x + 1; x--) { int z = deckZ + 1 + h; StepH(scx + x, scy + Rs, z); StepH(scx + x, scy + Rs - 1, z); StepH(scx + x, scy + Rs - 2, z); lastx = scx + x; lasty = scy + Rs; if (++sc % 2 == 0) h++; if (deckZ + 1 + h >= topZ - 1) { done = true; break; } } }
			else { int sc = 0; for (int y = c0y - 1; y >= c1y + 1; y--) { int z = deckZ + 1 + h; StepH(scx - Rs, scy + y, z); StepH(scx - Rs + 1, scy + y, z); StepH(scx - Rs + 2, scy + y, z); lastx = scx - Rs; lasty = scy + y; if (++sc % 2 == 0) h++; if (deckZ + 1 + h >= topZ - 1) { done = true; break; } } }
		}

		// --- Deko: Zinnen auf der Dachreling ---
		for (int dx = -R; dx <= R; dx++) for (int dy = -R; dy <= R; dy++) {
			if (!inOct(dx, dy)) continue;
			bool w2 = !(inOct(dx + 1, dy) && inOct(dx - 1, dy) && inOct(dx, dy + 1) && inOct(dx, dy - 1));
			if (w2 && ((dx + dy) & 1) == 0) Rail(cx + dx, cy + dy, topZ + 2);
		}
		// --- Deko: schmale Fenster (Nord + West), zwei Hoehen ---
		for (int wz = deckZ + 14; wz <= deckZ + 16; wz++) { Air(cx, cy + R, wz); Air(cx - R, cy, wz); }
		for (int wz = deckZ + 26; wz <= deckZ + 28; wz++) { Air(cx, cy + R, wz); Air(cx - R, cy, wz); }
		// --- Deko: Banner (rot) neben dem Tor ---
		for (int bz = deckZ + 2; bz <= deckZ + 7; bz++) { P(cx - 5, cy - R, bz, 175, 45, 45, 1); P(cx + 5, cy - R, bz, 175, 45, 45, 1); }

		// --- Laufplanke (+x-Seite) zum Anlegen des Luftschiffs ---
		int gy = cy;
		for (int dyy = -2; dyy <= 2; dyy++) { Air(cx + R, gy + dyy, topZ + 1); Air(cx + R, gy + dyy, topZ + 2); } // Reling oeffnen
		for (int gi = 1; gi <= 12; gi++) {
			int gx = cx + R + gi;
			for (int dyy = -2; dyy <= 2; dyy++) Deck(gx, gy + dyy, topZ);      // 5 breit, 3 begehbar
			Rail(gx, gy - 2, topZ + 1); Rail(gx, gy + 2, topZ + 1);           // Gelaender
		}
		int ex = cx + R + 13;                                                 // Dock-Plattform (5x7)
		for (int a = 0; a < 5; a++) for (int b = -3; b <= 3; b++) Deck(ex + a, gy + b, topZ);
		for (int a = 0; a < 5; a++) { Rail(ex + a, gy - 3, topZ + 1); Rail(ex + a, gy + 3, topZ + 1); } // Laengsseiten
		for (int b = -3; b <= 3; b++) Rail(ex + 4, gy + b, topZ + 1);         // Fernseite (Nahseite offen)
		for (int z = topZ + 1; z <= topZ + 5; z++) TWall(ex + 2, gy, z);      // Anlegemast
		P(ex + 2, gy, topZ + 6, 255, 90, 40, 1);                             // Signalfeuer
	}

	// --- Kompletten Bau planen ---
	static void ScanFootprint(cube::World* w, int px, int py, int pz, int& maxS, int& minS, int& avgS); // Def. weiter unten
	// ================= NATIVES HAUS AUS DEM SCAN ============================
	// Erfasst mit STRG+J in einem nativen Dorf, hier auf die Hausmitte umgerechnet.
	// Belegt durch die Messung: Zellmitten liegen 13 Bloecke auseinander, Waende
	// sitzen 6-7 Bloecke davon versetzt auf der Zellkante, ein Stockwerk ist
	// 7 Bloecke hoch. Drehung 0 = Wand auf der Y-Kante, 1 = auf der X-Kante.
	// Dieses Haus ist 2x2 Zellen mit zwei Geschossen.
	struct TplObj { int dx, dy, dz, type, rot; float sx, sy, sz; };

	static const TplObj kHouseA[] = {
		// --- Aussenwaende, Erdgeschoss ---
		{ -6, -13, 0, 0x01, 0, 2.0f, 1.0f, 3.0f },   // Tuerseite (-Y)
		{ -13, -6, 0, 0x01, 1, 2.0f, 1.0f, 3.0f },
		{  7, -13, 1, 0x03, 0, 2.0f, 1.0f, 2.0f },
		{ 13,  -6, 1, 0x03, 1, 2.0f, 1.0f, 2.0f },
		{ -6,  13, 1, 0x03, 0, 2.0f, 1.0f, 2.0f },
		{ -13,  7, 1, 0x03, 1, 2.0f, 1.0f, 2.0f },
		{  7,  13, 1, 0x03, 0, 2.0f, 1.0f, 2.0f },
		{ 13,   7, 1, 0x03, 1, 2.0f, 1.0f, 2.0f },
		// --- Aussenwaende, Obergeschoss (+7 Bloecke) ---
		{ -6,  13, 8, 0x03, 0, 2.0f, 1.0f, 2.0f },
		{ -13,  7, 8, 0x03, 1, 2.0f, 1.0f, 2.0f },
		// --- Einrichtung Erdgeschoss ---
		{ -12, -9, 0, 0x12, 1, 3.0f, 1.0f, 0.4f },
		{ -11, -4, 0, 0x0a, 1, 1.2f, 0.8f, 0.8f },
		{  -9,-12, 0, 0x1e, 2, 3.0f, 1.0f, 1.0f },
		{  -6, -6, 0, 0x0c, 3, 3.0f, 3.0f, 1.0f },
		{  12, -9, 0, 0x1e, 3, 3.0f, 1.0f, 1.0f },
		{   4,-12, 0, 0x1e, 2, 3.0f, 1.0f, 1.0f },
		{  12,  4, 0, 0x14, 3, 1.0f, 1.0f, 1.0f },
		{   4, 12, 0, 0x14, 0, 1.0f, 1.0f, 1.0f },
		{   9, 12, 0, 0x22, 0, 2.0f, 1.0f, 1.57f },
		{   4,  4, 0, 0x13, 2, 2.0f, 3.0f, 1.0f },   // Vierergruppe (Tisch/Stuehle)
		{  10,  4, 0, 0x13, 0, 2.0f, 3.0f, 1.0f },
		{   4, 10, 0, 0x13, 0, 2.0f, 3.0f, 1.0f },
		{  10, 10, 0, 0x13, 3, 2.0f, 3.0f, 1.0f },
		// --- Einrichtung Obergeschoss ---
		{ -12,  4, 7, 0x21, 1, 2.0f, 1.0f, 1.57f },
		{ -12,  9, 7, 0x3c, 1, 1.0f, 1.0f, 1.0f },
		{  -1,  4, 7, 0x12, 3, 3.0f, 1.0f, 0.4f },
		{  -1,  9, 7, 0x1e, 3, 3.0f, 1.0f, 1.0f },
		{  -9,  2, 7, 0x10, 2, 1.0f, 1.0f, 0.5f },
		{  -9, 11, 7, 0x10, 0, 1.0f, 1.0f, 0.5f },
		{  -4, 12, 7, 0x1e, 0, 3.0f, 1.0f, 1.0f },
	};

	static void PlanBuild(cube::GameController* gc, int px, int py, int deckZ) {
		cube::World* w = &gc->world;
		g_queue.clear(); g_qi = 0; g_placed = 0; g_failed = 0;
		g_decoReqs.clear(); // Haus-Deko-Requests dieses Baus sammeln
		g_buildCx = px; g_buildCy = py; g_buildDeckZ = deckZ; // fuer Remesh + NPC-Spawn
		PlanTower(w, px, py, deckZ);
		// Grosse flache Ebene mit gewundenen Wegen (Pflaster) und Reling-Rand
		const int PR = 80;
		// Terrainbereich der Flaeche -> Scan/Fuellung/Schnitt genau so weit wie noetig (guenstig
		// auf flachem Terrain, passt sich aber steilem voll an). Cap gegen extreme Klippen.
		const int kFitMax = 140;
		int fmaxS, fminS, favgS; ScanFootprint(w, px, py, deckZ, fmaxS, fminS, favgS);
		int scanTop = fmaxS + 3; if (scanTop > deckZ + kFitMax) scanTop = deckZ + kFitMax;
		int scanBot = fminS - 3; if (scanBot < deckZ - kFitMax) scanBot = deckZ - kFitMax;
		for (int dx = -PR; dx <= PR; dx++) for (int dy = -PR; dy <= PR; dy++) {
			int rr = dx * dx + dy * dy;
			if (rr > PR * PR) continue;
			if (inOct(dx, dy)) continue; // Turmbereich auslassen
			int x = px + dx, y = py + dy;
			// Terrasse: Nahe Turm hoeher freischneiden (Turm ist hoch), sonst Hof-/Haushoehe.
			// Fundament (Talseite) bzw. Wegschnitt (Bergseite) bis zum ECHTEN Boden/Fels -- passt
			// sich jedem Hang an (kein fester Cap, nur der Terrainbereich oben).
			int s = SurfaceZ(w, x, y, scanTop, scanBot);
			if (s <= -999999) s = scanBot;
			if (s < deckZ) { for (int z = s + 1; z < deckZ; z++) Stone(x, y, z); }
			else if (s > deckZ) { for (int z = deckZ + 1; z <= s; z++) Air(x, y, z); }
			bool edge = rr > (PR - 1) * (PR - 1);
			// WEGENETZ nach Lageplan: sternfoermig vom Turm nach aussen, gewunden.
			// Frueher waren es nur zwei Diagonalkreuze; der Plan zeigt einen Stern
			// aus kPathSpokes Wegen, die alle bis zum Rand durchlaufen.
			double rdist = std::sqrt((double)rr);
			double wind = 3.0 * std::sin(rdist * 0.16);
			bool path = false;
			if (rdist > 1.0) {
				for (int s = 0; s < kPathSpokes; s++) {
					double a = (2.0 * 3.14159265358979 * s) / kPathSpokes + 0.3926990817; // 22.5 Grad versetzt
					double cs = std::cos(a), sn = std::sin(a);
					double along = cs * dx + sn * dy;          // Laenge entlang der Speiche
					double perp = -sn * dx + cs * dy;          // Abstand quer dazu
					if (along > 0.0 && std::fabs(perp - wind) <= kPathHalfWidth) { path = true; break; }
				}
			}
			if (edge) { DeckEdge(x, y, deckZ); Rail(x, y, deckZ + 1); }
			else if (path) Path(x, y, deckZ);
			else Floor(x, y, deckZ);
		}
		// Organische Einbettung: rund um die Plattform die Bergspitzen abtragen, nach aussen
		// als 1:1-Rampe auslaufend -> kein harter Zylinder, das Dorf waechst in den Hang.
		for (int dx = -(PR + kBlend); dx <= PR + kBlend; dx++)
			for (int dy = -(PR + kBlend); dy <= PR + kBlend; dy++) {
				int rr = dx * dx + dy * dy;
				if (rr <= PR * PR) continue;                       // Plattform ist schon behandelt
				if (rr > (PR + kBlend) * (PR + kBlend)) continue;
				int x = px + dx, y = py + dy;
				int r = (int)(std::sqrt((double)rr));
				int allowed = deckZ + (r - PR);                    // erlaubte Hoehe steigt nach aussen (Rampe)
				int s = SurfaceZ(w, x, y, allowed + kBlendCap, allowed + 1);
				if (s > allowed) {                                 // Spitze ueber der Rampe -> abtragen
					int to = s; if (to > allowed + kBlendCap) to = allowed + kBlendCap;
					for (int z = allowed + 1; z <= to; z++) Air(x, y, z);
				}
			}
		// VIER grosse Haeuser an den Diagonalen (45/135/225/315 Grad); der Steg auf +X
		// bleibt frei. Haus 0 (NO, +x+y) ist die SCHMIEDE (CustomizationBench + Ofen + Amboss).
		g_nbuilds.clear();
		g_houses.clear();
		for (int k = 0; k < 4; k++) {
			double ang = 0.7853981634 + k * 1.5707963268; // 45 + k*90 Grad
			double cs = std::cos(ang), sn = std::sin(ang);
			int hx = px + (int)(cs * 54.0), hy = py + (int)(sn * 54.0);
			// Tuer zeigt zur Dorfmitte (dominante Achse Richtung Zentrum).
			int face = (cs < 0 ? -cs : cs) > (sn < 0 ? -sn : sn) ? (cs > 0 ? 3 : 1) : (sn > 0 ? 0 : 2);
			// Drehung so, dass die Tuerseite zur Dorfmitte zeigt (face 0..3).
			// Fertige Ganzgebaeude: k==0 die Schmiede (im Spiel bestaetigt, 32x31),
			// dazu drei grosse Wohnhaeuser aus derselben Gruppe (32x32/32x32/22x22).
			// Alle vier Gebaeude sind jetzt native Ganzmodelle (im Spiel bestaetigt):
			// Schmiede plus drei VERSCHIEDENE Wohnhaeuser.
			int model = (k == 0) ? NativeHouseProbe::Native::Smithy
				: (k == 1 ? NativeHouseProbe::Native::House1
					: (k == 2 ? NativeHouseProbe::Native::House2
						: NativeHouseProbe::Native::House3));
			if (!kUseNativeHouses) {
				if (k == 0) PlanSmithy(hx - 13, hy - 9, deckZ + 1, face);          // L-Schmiede mit offener Esse
				else        PlanHouse(hx - 11, hy - 9, deckZ + 1, 22, 18, 11, face); // groessere Wohnhaeuser
			}
			else if (k == 0) {
				// Native Schmiede: die Stationen sind Objekte, keine Blockmoebel.
				// Sie werden hier direkt um die Hausmitte gelegt, weil PlanSmithy
				// (das sie sonst anmeldet) beim nativen Bau nicht mehr laeuft.
				g_decoReqs.push_back({ kTypeAnvil, hx - 5, hy,     deckZ + 1, 0, 1.5f, 1.0f }); // Amboss
				g_decoReqs.push_back({ 0x18,       hx + 5, hy,     deckZ + 1, 0, 1.0f, 1.0f }); // Loeschfass
				g_decoReqs.push_back({ 0x4d,       hx,     hy - 8, deckZ + 1, 0, 2.0f, 1.0f }); // CustomizationBench
				const int ns = (int)(sizeof(kStations) / sizeof(kStations[0]));
				for (int i = 0; i < ns; i++)
					g_decoReqs.push_back({ kStations[i], hx - 12 + i * 6, hy + 9, deckZ + 1, 0, 1.5f, 1.0f });
			}
			g_houses.push_back({ hx, hy, model, face });
		}
		// (Kleindeko -- Laternen an den Gaengen, Blumenkaesten + Marktstaende an den
		//  Haeusern -- wird nativ in PlaceVillageDeco gesetzt.)
		g_building = true;
	}

	// Scannt die ganze Plattformflaeche (Radius 70): hoechstes, tiefstes UND mittleres Terrain.
	static void ScanFootprint(cube::World* w, int px, int py, int pz, int& maxS, int& minS, int& avgS) {
		maxS = -1000000; minS = 1000000; long long sum = 0; int cnt = 0;
		for (int dx = -70; dx <= 70; dx += 4) for (int dy = -70; dy <= 70; dy += 4) {
			if (dx * dx + dy * dy > 70 * 70) continue;
			int s = SurfaceZ(w, px + dx, py + dy, pz + 90, pz - 160);
			if (s > -999999) { if (s > maxS) maxS = s; if (s < minS) minS = s; sum += s; cnt++; }
		}
		if (cnt == 0) { maxS = minS = avgS = pz; return; }
		avgS = (int)(sum / cnt);
	}
	// Deck-Hoehe = mittleres Terrain (+1): Terrasse mittig im Hang -> bergseitig wegschneiden,
	// talseitig Fundament. So ragt das Dorf seitlich aus dem Berg heraus statt zu schweben.
	static int ComputeDeckZ(cube::World* w, int px, int py, int pz) {
		int maxS, minS, avgS; ScanFootprint(w, px, py, pz, maxS, minS, avgS);
		return avgS + 1;
	}

	// --- Persistenz ---
	static void LoadSites() {
		g_sites.clear();
		std::istringstream f(ModDb::Txt(kSitesFile));
		int x, y, z;
		while (f >> x >> y >> z) { Site s; s.x = x; s.y = y; s.deckZ = z; g_sites.push_back(s); }
		// std::cout << "[Bergdorf] " << g_sites.size() << " gespeicherte Dorf-Standorte geladen." << std::endl;
	}
	static void SaveSites() {
		ModDb::Writer f(kSitesFile);
		for (size_t i = 0; i < g_sites.size(); i++) f << g_sites[i].x << " " << g_sites[i].y << " " << g_sites[i].deckZ << "\n";
	}
	static void AddSite(int x, int y, int deckZ) {
		for (size_t i = 0; i < g_sites.size(); i++)
			if (std::abs(g_sites[i].x - x) < 20 && std::abs(g_sites[i].y - y) < 20) { g_sites[i].deckZ = deckZ; SaveSites(); return; }
		Site s; s.x = x; s.y = y; s.deckZ = deckZ; g_sites.push_back(s); SaveSites();
	}

	// --- STRG+0: Bau an der Spielerposition ---
	void BuildAtPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		if (g_building) { return; }   // (Konsolenausgabe entfernt)
		cube::World* w = &gc->world;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);
		// 50 Bloecke in Blickrichtung -> Turm spawnt nicht im Spieler
		float yaw = gc->local_player->entity_data.rotation.Z; // Yaw = Z-Komponente
		int bx = px + (int)(std::cos(yaw) * 50.0f);
		int by = py + (int)(std::sin(yaw) * 50.0f);
		int deckZ = ComputeDeckZ(w, bx, by, pz);
		PlanBuild(gc, bx, by, deckZ);
		RecordVillage(bx, by);
		// std::cout << "[Bergdorf] (Test) Lufthafen geplant: " << g_queue.size()
			// << " Bloecke, Deck auf Z=" << deckZ << " bei Block(" << bx << ", " << by << "), 50 Bloecke vor dir." << std::endl;
	}

	// --- Chunks sichtbar machen ---
	// NUR die Chunks im Dorf-Bereich neu meshen (wie der Building-Mod via UpdateChunk),
	// statt ALLE geladenen Chunks zu flaggen. Alles gleichzeitig neu zu meshen erzeugt
	// einen riesigen Spike -> schwarze Karte / Absturz. Chunk = 32x32 Bloecke;
	// Layout wie im Building-Mod: x@+0x18, y@+0x1C, needs_update@+0x74.
	static void UpdateVillageChunks(cube::GameController* gc, int bx, int by) {
		__try {
			uint32_t n = gc->chunk_count_to_be_sqaured;
			if (n == 0 || n > 128) return;
			uint32_t total = n * n;
			cube::Chunk* ch = gc->chunks;
			if (!ch) return;
			int cxlo = (bx - 96) / 32, cxhi = (bx + 96) / 32;
			int cylo = (by - 96) / 32, cyhi = (by + 96) / 32;
			int hit = 0;
			for (uint32_t i = 0; i < total; i++) {
				uint8_t* c = (uint8_t*)&ch[i];
				int cx = *(int*)(c + 0x18), cy = *(int*)(c + 0x1C);
				if (cx >= cxlo && cx <= cxhi && cy >= cylo && cy <= cyhi) { ch[i].needs_update = 1; hit++; }
			}
			// std::cout << "[Bergdorf] Remesh nur Dorf-Chunks: " << hit << " Stueck." << std::endl;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}   // (Konsolenausgabe entfernt)
	}
	// // Alte Variante (alle Chunks) -- NICHT mehr benutzen (Spike/Crash), nur als Fallback.
	static void RemeshLoadedChunks(cube::GameController* gc) {
		__try {
			uint32_t n = gc->chunk_count_to_be_sqaured;
			if (n == 0 || n > 128) { return; }
			uint32_t total = n * n;
			cube::Chunk* ch = gc->chunks;
			if (!ch) return;
			for (uint32_t i = 0; i < total; i++) ch[i].needs_update = 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// Liest static_objects einer Zone roh aus (keine C++-Objekte -> __try ok).
	// Nur Siedlungen (viele Objekte). Fuellt g_capbuf/g_capbuf2 + Typenliste.
	static bool CaptureSettlement(cube::Zone* zone, int* outN) {
		__try {
			uint8_t** raw = (uint8_t**)&zone->static_objects; // {start,end,cap}
			uint8_t* start = raw[0]; uint8_t* end = raw[1];
			if (!start || !end || end <= start) return false;
			int n = (int)((end - start) / 0x188);
			*outN = n;
			if (n < 6) return false; // nur groessere Ansammlungen (Siedlung/Wald)
			memcpy(g_capbuf, start, 0x188);
			memcpy(g_capbuf2, start + 0x188, 0x188);
			g_capTypeN = n < 64 ? n : 64;
			for (int i = 0; i < g_capTypeN; i++) g_capTypes[i] = *(int*)(start + i * 0x188);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Prueft, ob eine Zone ein natives Gebaeude(teil) enthaelt (static_object Typ 52-55).
	static bool ZoneHasNativeBuilding(cube::Zone* zone) {
		__try {
			uint8_t** raw = (uint8_t**)&zone->static_objects; // {start,end,cap}
			uint8_t* start = raw[0]; uint8_t* end = raw[1];
			if (!start || !end || end <= start) return false;
			int n = (int)((end - start) / 0x188);
			if (n < 0) return false;
			if (n > 8192) n = 8192; // Sicherheitsgrenze
			for (int i = 0; i < n; i++) {
				int type = *(int*)(start + (size_t)i * 0x188);
				if (type >= 52 && type <= 55) return true; // natives Gebaeude gefunden
			}
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// True, wenn in der Ziel-Zone ODER einer der 8 Nachbarzonen eine native Struktur liegt.
	// So spawnt unser Dorf nicht IN oder direkt BEI Siedlungen/Staedten.
	static cube::Zone* SafeGetZone(cube::World* w, int zx, int zy); // Vorwaertsdeklaration (Def. weiter unten)
	static bool NearNativeStructure(cube::World* w, int cx, int cy) {
		int zx = cx / 256, zy = cy / 256;
		for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) {
			cube::Zone* z = SafeGetZone(w, zx + a, zy + b);
			if (z && ZoneHasNativeBuilding(z)) return true;
		}
		return false;
	}

	// Abstand, den unser Dorf zu nativen Strukturen (Doerfer/Dungeons/Bauwerke) haelt.
	static const int kNativeAvoid = 400;  // zusaetzlich zum Feature-Radius
	// Chance, dass auf einem erkannten Plateau ein Dorf entsteht (1 von N).
	static const int kPlateauChance = 3;
	// Persistenter Dorf-Marker (Sentinel-Farbe, tief vergraben) in der Welt-DB.
	static const uint8_t kMarkerR = 254, kMarkerG = 1, kMarkerB = 253, kMarkerT = 1;
	static std::vector<std::pair<int, int> > g_scannedZones; // diese Session auf Marker geprueft
	static std::vector<std::pair<int, int> > g_npcSpawned;   // Doerfer mit gespawnten NPCs (Session)

	// ---- Phase 2: natives Gebaeudeobjekt bauen + korrekt in die Zone anhaengen ----
	using Fn_NewMV = void* (__cdecl*)(unsigned int);
	static uint32_t ImageBaseMV() { return (uint32_t)GetModuleHandleA(NULL); }

	// Deterministischer Hash je Zone (Seed aus dem Speicher) -> gleiche Doerfer an gleichen Stellen.
	static uint32_t ZoneHash(cube::GameController* gc, int zx, int zy) {
		uint32_t seed = 0;
		__try { seed = *(uint32_t*)((uint8_t*)gc + 0x2E4 + 0x800188); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		uint32_t h = (uint32_t)(zx * 73856093) ^ (uint32_t)(zy * 19349663) ^ (seed * 83492791u);
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return h;
	}
	// Ganzzahl-Division mit Abrunden (auch fuer negative Zonen korrekt).
	static int FloorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
	// RASTER: pro Zelle (kCellZones x kCellZones Zonen) genau EIN Dorf, deterministisch aus
	// dem Seed gewaehlt (nahe Zellmitte + kleiner Jitter). Garantiert Mindestabstand
	// (kCellZones - 2*kJitterZones) * 256 Bloecke zwischen benachbarten Doerfern.
	static const int kCellZones = 44;  // 44 Zonen = 11264 Bloecke Zellgroesse
	static const int kJitterZones = 2;   // +-2 Zonen Versatz -> Mindestabstand (44-4)*256 = 10240 Bloecke
	static volatile VillageFilter g_villageFilter = nullptr;   // BiomeMod: Bergdorf je Biom aus
	static bool IsVillageZoneRaw(cube::GameController* gc, int zx, int zy) {
		int cgx = FloorDiv(zx, kCellZones), cgy = FloorDiv(zy, kCellZones);
		uint32_t h = ZoneHash(gc, cgx * 2 + 12345, cgy * 2 - 6789); // Hash der Zelle
		int jx = (int)(h % (2 * kJitterZones + 1)) - kJitterZones;
		int jy = (int)((h >> 8) % (2 * kJitterZones + 1)) - kJitterZones;
		int chosenX = cgx * kCellZones + kCellZones / 2 + jx;
		int chosenY = cgy * kCellZones + kCellZones / 2 + jy;
		return (zx == chosenX && zy == chosenY);
	}
	static bool VillageAllowed(cube::GameController* gc, int zx, int zy) {
		VillageFilter f = g_villageFilter;
		return !f || f(gc, zx, zy);
	}
	static bool IsVillageZone(cube::GameController* gc, int zx, int zy) {
		return IsVillageZoneRaw(gc, zx, zy) && VillageAllowed(gc, zx, zy);
	}
	// Liefert die deterministisch gewaehlte Dorf-Zone der Rasterzelle (cgx,cgy).
	static void VillageZoneForCell(cube::GameController* gc, int cgx, int cgy, int& vzx, int& vzy) {
		uint32_t h = ZoneHash(gc, cgx * 2 + 12345, cgy * 2 - 6789);
		int jx = (int)(h % (2 * kJitterZones + 1)) - kJitterZones;
		int jy = (int)((h >> 8) % (2 * kJitterZones + 1)) - kJitterZones;
		vzx = cgx * kCellZones + kCellZones / 2 + jx;
		vzy = cgy * kCellZones + kCellZones / 2 + jy;
	}

	// Naechstes Bergdorf in einer Himmelsrichtung von einer Blockposition.
	// dir: 0=Nord(+Y) 1=Ost(+X) 2=Sued(-Y) 3=West(-X). Liefert Dorf-Mittelblock.
	// Da jede Rasterzelle GENAU ein Dorf hat, ist das Nachbardorf das Dorf der
	// benachbarten Zelle in der jeweiligen Richtung. Rueckgabe true bei Erfolg.
	static bool NextVillageInDirectionImpl(cube::GameController* gc, int fromBlockX, int fromBlockY,
		int dir, int& outBlockX, int& outBlockY) {
		if (!gc) return false;
		int zx = FloorDiv(fromBlockX, 256), zy = FloorDiv(fromBlockY, 256);
		int cgx = FloorDiv(zx, kCellZones), cgy = FloorDiv(zy, kCellZones);
		switch (dir) {
		case 0: cgy += 1; break; // Nord = +Y
		case 1: cgx += 1; break; // Ost  = +X
		case 2: cgy -= 1; break; // Sued = -Y
		case 3: cgx -= 1; break; // West = -X
		default: return false;
		}
		int vzx, vzy; VillageZoneForCell(gc, cgx, cgy, vzx, vzy);
		outBlockX = vzx * 256 + 128;
		outBlockY = vzy * 256 + 128;
		return true;
	}
	// Zonen-Hoehe aus dem Seed OHNE Chunk-Laden (FUN_00487da0, tile+0x18; Meeresspiegel=0).
	using Fn_WorldTile = void* (__thiscall*)(void*, int, int);
	static bool TileElev(cube::GameController* gc, int zx, int zy, int& outElev) {
		__try {
			void* worldCtx = (void*)((uint8_t*)gc + 0x2e4);
			void* tile = ((Fn_WorldTile)(Host::ReOff(0x87da0)))(worldCtx, zx, zy);
			if (!tile) return false;
			outElev = *(int*)((uint8_t*)tile + 0x18);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void RecordVillage(int cx, int cy) {
		for (size_t i = 0; i < g_villages.size(); i++) {
			int dx = g_villages[i].first - cx, dy = g_villages[i].second - cy;
			if (dx * dx + dy * dy < 100 * 100) return;
		}
		g_villages.push_back(std::make_pair(cx, cy));
	}
	static const char* Compass(int dx, int dy) {
		static const char* dirs[8] = { "O", "NO", "N", "NW", "W", "SW", "S", "SO" };
		double a = std::atan2((double)dy, (double)dx);
		int idx = (int)std::floor((a + 3.14159265 / 8.0) / (3.14159265 / 4.0));
		idx = ((idx % 8) + 8) % 8;
		return dirs[idx];
	}
	static cube::Zone* SafeGetZone(cube::World* w, int zx, int zy) {
		__try { return w->GetZone(zx, zy); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	// Weltsperre +0x8000C0 UND Blocksperre +0x8000D8, in der Reihenfolge des Chunk-Threads
	// (0x46AF84, 0x46AF91). Der Kartenthread erzeugt Kacheln nur unter +0x8000D8 (0x469B62..
	// 0x469B82); ohne sie konnte er Saeulen lesen, die gerade geschrieben werden (schwarze Minimap).
	static void SafeLockWorld(cube::World* w) {
		__try { w->Lock(); EnterCriticalSection((LPCRITICAL_SECTION)&w->block_op_critical_section); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static void SafeUnlockWorld(cube::World* w) {
		__try { LeaveCriticalSection((LPCRITICAL_SECTION)&w->block_op_critical_section); w->Unlock(); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// ================= PERSISTENZ-TEST: 0x14-Block-Record in die Weltdatenbank =================
	// Aus RE (RE_Notizen_Persistenz.md): Zone speichert beim Entladen, wenn zone+0x75 (dirty) gesetzt.
	// Serializer schreibt IMMER die 0x14-Records aus dem Vektor zone+0x68/+0x6c. Ein Record ist
	// 20 Byte: [x:4][y:4][z:4][Farbe rgba:4][id:4]; beim Laden via SetBlock angewandt (id<0 -> immer).
	static void BuildRecord14(uint8_t* rec, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t t, int id) {
		memset(rec, 0, 0x14);
		*(int*)(rec + 0x00) = x;
		*(int*)(rec + 0x04) = y;
		*(int*)(rec + 0x08) = z;
		rec[0x0C] = r; rec[0x0D] = g; rec[0x0E] = b; rec[0x0F] = t;
		*(int*)(rec + 0x10) = id;
	}
	// Haengt einen 0x14-Record an den Vektor zone+0x68 an (freie Kapazitaet ODER Realloc via Spiel-new).
	static bool AppendZoneRecord14(cube::Zone* zone, const uint8_t* rec) {
		__try {
			uint8_t** v = (uint8_t**)((uint8_t*)zone + 0x68); // {start,end,cap}
			uint8_t* start = v[0]; uint8_t* end = v[1]; uint8_t* cap = v[2];
			const int esz = 0x14;
			if (end && cap && end + esz <= cap) { memcpy(end, rec, esz); v[1] = end + esz; return true; }
			int existing = (start && end && end > start) ? (int)((end - start) / esz) : 0;
			if (existing < 0 || existing > 200000) return false; // Sicherheitsgrenze
			int total = existing + 1;
			Fn_NewMV opNew = (Fn_NewMV)(Host::ReOff(0x28D668)); // spieleigenes operator_new (0x68D668)
			uint8_t* nb = (uint8_t*)opNew((unsigned)(total * esz));
			if (!nb) return false;
			if (existing && start) memcpy(nb, start, (size_t)existing * esz);
			memcpy(nb + (size_t)existing * esz, rec, esz);
			v[0] = nb; v[1] = nb + total * esz; v[2] = nb + total * esz;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Sucht in zone+0x68 nach unserem Dorf-Marker (Sentinel-Farbe); gibt true + Zentrum (cx,cy).
	static bool ScanZoneMarker(cube::Zone* zone, int& outCx, int& outCy) {
		__try {
			uint8_t** v = (uint8_t**)((uint8_t*)zone + 0x68);
			uint8_t* start = v[0]; uint8_t* end = v[1];
			if (!start || !end || end <= start) return false;
			int n = (int)((end - start) / 0x14);
			if (n < 0 || n > 200000) return false;
			for (int i = 0; i < n; i++) {
				uint8_t* rec = start + i * 0x14;
				if (rec[0x0C] == kMarkerR && rec[0x0D] == kMarkerG && rec[0x0E] == kMarkerB && rec[0x0F] == kMarkerT) {
					outCx = *(int*)(rec + 0x00); outCy = *(int*)(rec + 0x04); return true;
				}
			}
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Setzt EINEN persistenten Dorf-Marker (0x14-Record + zone dirty) -- nur, wenn noch keiner da ist.
	static void PlaceVillageMarker(cube::GameController* gc, int cx, int cy, int deckZ) {
		cube::World* w = &gc->world;
		cube::Zone* zone = SafeGetZone(w, cx / 256, cy / 256);
		if (!zone) return;
		int ex, ey; if (ScanZoneMarker(zone, ex, ey)) return; // schon vorhanden -> nicht doppeln
		int mz = deckZ - 45; // tief vergraben -> unsichtbar
		uint8_t rec[0x14];
		BuildRecord14(rec, cx, cy, mz, kMarkerR, kMarkerG, kMarkerB, kMarkerT, -1);
		if (AppendZoneRecord14(zone, rec)) {
			__try { *(uint8_t*)((uint8_t*)zone + 0x75) = 1; }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			PutBlock(w, cx, cy, mz, kMarkerR, kMarkerG, kMarkerB, kMarkerT);
			// std::cout << "[Bergdorf] Persistenz-Marker gesetzt (Zone " << (cx / 256) << "," << (cy / 256) << ")." << std::endl;
		}
	}

	// Macht eine Kreatur zum Dorfbewohner: Faktion +0x60 (5=freundlich, 6=passiv) + "talkable".
	// Nur POD-Schreibzugriffe -> __try ok.
	// KORRIGIERT: hier standen zwei Schreibzugriffe, die DASSELBE Byte trafen --
	//   *((uint8_t*)c + 0x60) = 5;            // "Faktion"
	//   c->entity_data.hostility_flags = 3;   // "talkable"
	// entity_data.hostility_flags LIEGT bei +0x60. Nachrechnen an cwsdk/cube/Creature.h:
	//   +0x00 vftable(4) +0x04 field_4(4) +0x08 GUID(8)            -> 0x10
	//   +0x10 position Vector3<int64_t> = 24 Byte                  -> 0x28
	//   +0x28 rotation / +0x34 velocity / +0x40 acceleration (je 12)
	//   +0x4C retreat_force(12) +0x58 head_rotation(4)             -> 0x5C
	//   +0x5C physics_flags, field_4D, field_4E, field_4F          -> 0x60
	// Die zweite Zeile ueberschrieb die erste; die 5 hat nie gewirkt. Dass es
	// trotzdem funktionierte, war Zufall: 3 ist genau der Wert, den das Spiel
	// seinen eigenen Dorfbewohnern gibt (RE_MobSpawn.md, §4 und §7.1).
	static void MakeVillager(cube::Creature* c) {
		if (!c) return;
		__try {
			c->entity_data.hostility_flags = cube::util::Hostility::Talkable;  // 3 -> [R]-Ziel
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		// Aggro-/Leinenradius wie nativ -- unsere Spawns hatten hier 0.
		cube::util::SetAggroRadius(c, cube::util::kNativeAggroRadius);
	}

	// Spawnt Dorfbewohner (Zwerge) + einen Haendler im Dorf. NPCs sind Entities und
	// persistieren nicht wie Bloecke -> sie werden je Session einmal pro Dorf gespawnt
	// (Dedup ueber g_npcSpawned). SpawnCreature ist intern SEH-isoliert (kein __try noetig).
	static void SpawnVillagers(cube::GameController* gc, int cx, int cy, int deckZ) {
		for (size_t i = 0; i < g_npcSpawned.size(); i++)
			if (g_npcSpawned[i].first == cx && g_npcSpawned[i].second == cy) return;
		g_npcSpawned.push_back(std::make_pair(cx, cy));
		const int64_t U = (int64_t)kUnitsPerBlock;
		// Zwerge (Dorfbewohner): wie im OceanMod -- SpawnMonster (wild, kein Begleiter) +
		// AttachRandomWalk (native Wander-KI, ersetzt die CompanionBehavior). KEIN MakeHostile
		// -> friedlich. hostility_flags=0.
		static const int spots[6][2] = { {26, 4}, {-26, -4}, {6, 28}, {-6, -28}, {22, -20}, {-22, 20} };
		int nOk = 0;
		for (int i = 0; i < 6; i++) {
			Vector3<int64_t> pos((int64_t)(cx + spots[i][0]) * U, (int64_t)(cy + spots[i][1]) * U, (int64_t)(deckZ + 3) * U);
			cube::Creature* c = cube::util::SpawnMonster(pos,
				(i % 2) ? cube::Creature::Race::DwarfFemale : cube::Creature::Race::DwarfMale,
				cube::util::Hostility::Talkable, /*level=*/250);   // 0 war SPIELER -- MakeVillager korrigierte es nur nachtraeglich
			if (c) { MakeVillager(c); cube::util::AttachRandomWalk(c, pos.X, pos.Y); nOk++; } // passiv + wandert (kein Companion)
		}
		std::cout << "[Bergdorf] Zwerge: " << nOk << "/6 gespawnt." << std::endl;
		// Unser eigener Haendler (WanderingMerchant, voller Katalog) in der Dorfmitte vor dem Turm.
		Vector3<int64_t> mpos((int64_t)cx * U, (int64_t)(cy - 26) * U, (int64_t)(deckZ + 3) * U);
		AdventureMod::SpawnMerchantAt(gc, mpos);
		// Luftschiff-Taxi: Vendor-NPC auf dem Turmdach registrieren/spawnen.
		AirshipTaxi::OnVillageBuilt(gc, cx, cy, deckZ);
		// Schmied-NPC in der L-Schmiede (Haus k==0) registrieren/spawnen.
		// Schmied-NPC entfernt -- die native CustomizeBench ist ueber [R]
		// benutzbar, sobald das Objekt im Ortsindex haengt.
		// BlacksmithNpc::OnVillageBuilt(gc, cx, cy, deckZ);
		// std::cout << "[Bergdorf] Zwerge + Haendler gespawnt bei Block(" << cx << "," << cy << ")." << std::endl;
	}

	// Baut ein korrektes 0x188-Gebaeudeobjekt (Layout aus dem City-Dump).
	static void FillBuilding(uint8_t* o, int type, int bx, int by, int bz, int orient) {
		memset(o, 0, 0x188);
		*(int*)(o + 0x00) = type;
		*(long long*)(o + 0x08) = (long long)bx * 65536;
		*(long long*)(o + 0x10) = (long long)by * 65536;
		*(long long*)(o + 0x18) = (long long)bz * 65536;
		*(int*)(o + 0x20) = orient;
		*(float*)(o + 0x24) = 2.0f;   // vis_scale
		*(float*)(o + 0x28) = 1.0f;   // phys_scale
		*(float*)(o + 0x2C) = 1.5f;   // height
		*(int*)(o + 0x30) = 1;        // field_30
		*(int*)(o + 0x68) = 1;        // field_68
		*(uint32_t*)(o + 0x178) = 0xFFFFFFFF;
		*(uint32_t*)(o + 0x17C) = 0xFFFFFFFF;
		*(uint32_t*)(o + 0x180) = 0xFFFFFFFF;
	}
	// Wie FillBuilding, aber als DEKO-PROP mit einstellbarer Groesse:
	// sxy -> +0x24 (Horizontalskala, Breite/Tiefe), sz -> +0x2C (Hoehe).
	static void FillProp(uint8_t* o, int type, int x, int y, int z, int orient, float sxy, float sz) {
		FillBuilding(o, type, x, y, z, orient);
		*(float*)(o + 0x24) = sxy; // Horizontalskala
		*(float*)(o + 0x2C) = sz;  // Hoehe
	}
	// Wie FillProp, aber mit ALLEN DREI Skalen. Der Hausscan zeigt bei +0x28
	// Werte von 0.8 bis 3.0 -- die feste 1.0 aus FillBuilding verformt native
	// Bauteile, deshalb hier getrennt setzbar.
	static void FillProp3(uint8_t* o, int type, int x, int y, int z, int orient,
		float sx, float sy, float sz) {
		FillBuilding(o, type, x, y, z, orient);
		*(float*)(o + 0x24) = sx;
		*(float*)(o + 0x28) = sy;
		*(float*)(o + 0x2C) = sz;
	}
	// Haengt ein Objekt an den static_objects-Vektor an (freie Kapazitaet ODER Realloc).
	static bool AppendBuilding(cube::Zone* zone, const uint8_t* src) {
		__try {
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t* start = raw[0]; uint8_t* end = raw[1]; uint8_t* cap = raw[2];
			const int esz = 0x188;
			if (end && cap && end + esz <= cap) { memcpy(end, src, esz); raw[1] = end + esz; return true; }
			int existing = (start && end && end > start) ? (int)((end - start) / esz) : 0;
			int total = existing + 1;

			// KAPAZITAET VERDOPPELN statt exakt zu passen.
			// Frueher wurde raw[2] auf raw[1] gesetzt -- der Puffer war damit nach
			// JEDEM Anhaengen sofort wieder voll und wurde beim naechsten Mal
			// komplett neu alloziert. Der alte Puffer wird dabei NICHT freigegeben
			// (wir kennen das passende operator delete des Spiels nicht), also
			// summierten sich die Zwischenpuffer:
			//     1600 Anhaengungen  ->  rund 479 MB je Dorf
			//     vier Doerfer       ->  rund 1,9 GB
			// Ein 32-Bit-Prozess hat etwa 2 GB. Genau das war die Meldung
			// "nicht genuegend Speicherressourcen".
			// Mit Verdopplung sinkt die Zahl der Neuallokationen von 1600 auf ~11
			// und der Verbrauch auf wenige MB.
			int capCount = (existing < 16) ? 32 : existing * 2;
			if (capCount < total) capCount = total;

			Fn_NewMV opNew = (Fn_NewMV)(Host::ReOff(0x28D668));
			uint8_t* nb = (uint8_t*)opNew((unsigned)(capCount * esz));
			if (!nb) return false;
			if (existing && start) memcpy(nb, start, existing * esz);
			memcpy(nb + existing * esz, src, esz);
			raw[0] = nb;
			raw[1] = nb + total * esz;        // Ende der belegten Elemente
			raw[2] = nb + capCount * esz;     // Ende der KAPAZITAET
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void PlaceNativeBuildings(cube::GameController* gc) {
		if (g_nbuilds.empty()) return;
		cube::World* w = &gc->world;
		SafeLockWorld(w);
		int ok = 0;
		for (size_t i = 0; i < g_nbuilds.size(); i++) {
			NBuild& b = g_nbuilds[i];
			uint8_t obj[0x188];
			FillBuilding(obj, b.type, b.x, b.y, b.z, (int)(b.type % 4));
			int zx = b.x / 256, zy = b.y / 256;
			cube::Zone* zone = SafeGetZone(w, zx, zy);
			bool r = zone && AppendBuilding(zone, obj);
			// std::cout << "[Bergdorf] NativGebaeude #" << i << " type=" << b.type
				// << " Block(" << b.x << "," << b.y << "," << b.z << ") Zone(" << zx << "," << zy << ") -> "
				// << (r ? "angehaengt" : "FEHLGESCHLAGEN") << std::endl;
			if (r) ok++;
		}
		SafeUnlockWorld(w);
		RemeshLoadedChunks(gc);
		// std::cout << "[Bergdorf] Native Gebaeude: " << ok << "/" << g_nbuilds.size() << " angehaengt." << std::endl;
	}

	// ---- Native Deko-Props im Dorf platzieren (Weg 1 -- bewiesen renderfaehig) ----
	// Props sind static_objects (FillProp + AppendBuilding an die jeweilige Zone).
	// Groessen: sxy = Horizontalskala (+0x24), sz = Hoehe (+0x2C). Positionen relativ
	// zur Dorfmitte, auf der Deckflaeche (deckZ+1). Zusaetzlich die Haus-Innen-Deko
	// aus g_decoReqs (absolute Weltkoordinaten). Danach einmal Remesh.
	struct DecoItem { int type; int dx; int dy; int orient; float sxy; float sz; };
	static bool AppendProp(cube::World* w, int type, int x, int y, int z, int orient, float sxy, float sz) {
		uint8_t obj[0x188];
		FillProp(obj, type, x, y, z, orient, sxy, sz);
		cube::Zone* zone = SafeGetZone(w, FloorDiv(x, 256), FloorDiv(y, 256));
		return zone && AppendBuilding(zone, obj);
	}
	// Dreht (x,y) um r * 90 Grad. Probe: eine Y-Kanten-Wand (0,13) muss bei
	// r=1 auf der X-Kante landen -> (-13,0). Genau das liefert (x,y)->(-y,x).
	static void RotXY(int r, int& x, int& y) {
		for (int i = 0; i < (r & 3); i++) { int t = x; x = -y; y = t; }
	}

	// Setzt eine Hausvorlage zentriert auf (cx,cy), Sockel cz, um rot gedreht.
	static int PlaceTemplate(cube::World* w, const TplObj* tpl, int count,
		int cx, int cy, int cz, int rot) {
		int ok = 0;
		for (int i = 0; i < count; i++) {
			int dx = tpl[i].dx, dy = tpl[i].dy;
			RotXY(rot, dx, dy);
			// Skalen mitdrehen: bei ungerader Drehung tauschen X- und Y-Achse.
			float sx = tpl[i].sx, sy = tpl[i].sy;
			if (rot & 1) { float t = sx; sx = sy; sy = t; }
			uint8_t obj[0x188];
			FillProp3(obj, tpl[i].type, cx + dx, cy + dy, cz + tpl[i].dz,
				(tpl[i].rot + rot) & 3, sx, sy, tpl[i].sz);
			cube::Zone* zone = SafeGetZone(w, FloorDiv(cx + dx, 256), FloorDiv(cy + dy, 256));
			if (zone && AppendBuilding(zone, obj)) ok++;
		}
		return ok;
	}

	// ================= BLOCKSCANNER ========================================
	// Die Hausschale (Waende, Dach, Boden) besteht aus VOXELBLOECKEN, nicht aus
	// static_objects -- der Objektscan lieferte nur Moebel und Wandlampen.
	// Dieser Scanner liest den Quader um den Spieler Block fuer Block aus und
	// schreibt jeden gesetzten Block relativ zur Spielerposition nach
	// HausBloecke.txt. Daraus laesst sich ein Haus exakt nachbauen.
	static int g_blockScanNo = 0;

	// Ein einzelner Block, SEH-isoliert (kein __try neben C++-Objekten).
	static bool ReadBlock(cube::World* w, int x, int y, int z,
		uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& t) {
		__try {
			BlockColor* c = w->GetBlock(x, y, z, (cube::Zone*)nullptr);
			if (!c || c->Type == 0) return false;
			r = c->Red; g = c->Green; b = c->Blue; t = c->Type;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	void ScanBlocksAtPlayer(cube::GameController* gc, int radius, int zDown, int zUp) {
		if (!gc || !gc->local_player) { std::cout << "[BlockScan] Kein Spieler." << std::endl; return; }
		cube::World* w = &gc->world;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);

		std::ofstream f("HausBloecke.txt", std::ios::app);
		if (!f) { std::cout << "[BlockScan] Datei nicht schreibbar." << std::endl; return; }

		g_blockScanNo++;
		f << "\n=== Blockscan " << g_blockScanNo << "  Spieler Block(" << px << "," << py << "," << pz
			<< ")  Radius " << radius << "  z " << zDown << ".." << zUp << " ===\n";
		f << "dx\tdy\tdz\tR\tG\tB\tTyp\n";

		// Farbhistogramm: zeigt sofort, aus wie vielen Materialien das Haus besteht.
		struct Mat { uint8_t r, g, b, t; int n; };
		Mat mats[64]; int nMats = 0;

		int count = 0;
		for (int dx = -radius; dx <= radius; dx++) {
			for (int dy = -radius; dy <= radius; dy++) {
				for (int dz = zDown; dz <= zUp; dz++) {
					uint8_t r, g, b, t;
					if (!ReadBlock(w, px + dx, py + dy, pz + dz, r, g, b, t)) continue;
					f << dx << "\t" << dy << "\t" << dz << "\t"
						<< (int)r << "\t" << (int)g << "\t" << (int)b << "\t" << (int)t << "\n";
					count++;
					int k = 0;
					for (; k < nMats; k++)
						if (mats[k].r == r && mats[k].g == g && mats[k].b == b && mats[k].t == t) break;
					if (k < nMats) mats[k].n++;
					else if (nMats < 64) { mats[nMats].r = r; mats[nMats].g = g; mats[nMats].b = b; mats[nMats].t = t; mats[nMats].n = 1; nMats++; }
				}
			}
		}

		f << "# " << count << " Bloecke, " << nMats << " Materialien\n";
		f << "# Materialliste (R,G,B,Typ = Anzahl):\n";
		for (int k = 0; k < nMats; k++)
			f << "#   " << (int)mats[k].r << "," << (int)mats[k].g << "," << (int)mats[k].b
			<< "," << (int)mats[k].t << " = " << mats[k].n << "\n";
		f.close();

		std::cout << "[BlockScan " << g_blockScanNo << "] " << count << " Bloecke, "
			<< nMats << " Materialien -> HausBloecke.txt" << std::endl;
		if (count == 0)
			std::cout << "[BlockScan] Nichts gefunden -- stehst du im Haus?" << std::endl;
	}

	void ResetBlockScan() {
		std::ofstream f("HausBloecke.txt", std::ios::trunc);
		f << "# HausBloecke -- Voxelscans erfasster Bauten\n";
		f.close();
		g_blockScanNo = 0;
		std::cout << "[BlockScan] HausBloecke.txt zurueckgesetzt." << std::endl;
	}

	// ================= MARKERERSATZ ========================================
	// placeStructure stanzt das Modell samt farbiger Platzhalter. Der native
	// Umbau in Objekte laeuft nur waehrend der Zonengenerierung (mode 0x0D/0x0E
	// setzt bei uns gar kein Gebaeude mehr). Also machen wir es selbst:
	// Markerblock erkennen -> passendes natives Objekt anlegen -> Block loeschen.
	//
	// Die Zuordnung stammt aus WorldInfo_placeStructure (siehe RE_placeStructure.md).
	// Die Objekttypen sind BELEGT, die genauen RGB-Werte hat Ghidra grossenteils
	// verworfen -- deshalb wird nach dominanten Kanaelen klassifiziert, nicht nach
	// exakten Werten. Sitzt ein Objekt falsch, ist es hier eine Zeile.
	// NOTSCHALTER. Steht jetzt auf true, weil die beiden Absturzursachen belegt
	// gefunden und behoben sind:
	//   1. AppendBuilding setzte die Kapazitaet gleich dem Ende -> jede Anhaengung
	//      allozierte neu, ohne freizugeben. 1600 Objekte = 479 MB je Dorf,
	//      knapp 2 GB bei vier. Das war die Meldung "nicht genuegend
	//      Speicherressourcen". Jetzt Kapazitaetsverdopplung: 1,5 MB.
	//   2. IndexInsert nahm einen 12-Byte-Listenkopf an; er hat 8 Byte. Das
	//      "size++" schrieb in den _Myhead-Zeiger der NAECHSTEN Indexzelle --
	//      stille Speicherkorruption. Jetzt wird die Spielfunktion
	//      WorldInfo_mapInsertEntry @0x5F4B50 benutzt.
	// Bei Problemen hier auf false: Haeuser stehen mit bunten Markern, Spiel laeuft.
	static const bool kReplaceMarkers = true;

	// ---- ALTE EIGENE DEKO ----------------------------------------------------
	// Alles, was wir vor dem nativen Hausbau selbst gesetzt haben: Faesser und
	// Kisten am Turm, Marktstaende an den Hauswaenden, Baenke, Blumen im
	// Aussenring, und vor allem die HANDWERKSSTATIONEN (Amboss, Werkbank,
	// Saegebank, CustomizationBench), die wir per g_decoReqs angemeldet haben.
	//
	// Die nativen Haeuser bringen ihre Einrichtung inzwischen selbst mit -- ueber
	// die Markerbloecke. Unsere alte Deko steht seitdem doppelt und im Weg.
	// false = wird nicht mehr gesetzt. Der Code bleibt stehen, nichts geloescht.
	static const bool kLegacyDeco = false;

	// ================= OBJEKT-TESTER =======================================
	// Zwei offene Fragen auf einmal:
	//   1. Welcher Objekttyp zeigt WELCHES Modell? (bisher geraten -> falsche Modelle)
	//   2. Wird ein Objekt benutzbar, wenn es im Ortsindex zone+0xAC haengt?
	//
	// Belegt aus RE_Interaktion.md: Physik und Zielsuche lesen AUSSCHLIESSLICH
	// den Ortsindex zone+0xAC[cellX][cellY] (Zellen zu 8x8 Bloecken), niemals
	// den static_objects-Vektor. Ohne Indexeintrag ist ein Objekt reine Optik.
	//
	//   getTileCell32 @0x42F640  thiscall auf World:
	//       void* getTileCell32(uint cellX, uint cellY, Zone* zone)
	//   Listenknoten: { next, prev, StaticObject* obj, int zoneX, zoneY, index }

	static const uint32_t kFnGetTileCell32 = 0x0042F640;

	// Liefert die Indexzelle fuer einen Weltblock, oder nullptr.
	static void* TileCell(cube::World* w, int blockX, int blockY, cube::Zone* zone) {
		typedef void* (__thiscall* Fn)(void*, unsigned, unsigned, void*);
		__try {
			Fn f = (Fn)(Host::Re(kFnGetTileCell32));
			return f(w, (unsigned)(blockX >> 3), (unsigned)(blockY >> 3), zone);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	// Haengt ein Objekt in den Ortsindex -- ueber die SPIELFUNKTION, nicht von Hand.
	// WorldInfo_mapInsertEntry @0x5F4B50 ist woertlich std::list::push_back:
	//     __thiscall(std::list<ObjRef>* this, const ObjRef* value)
	// ObjRef = { StaticObject* obj; int zoneX, zoneY, index; }  (16 Byte)
	//
	// MEIN FRUEHERER FEHLER: ich nahm einen 12-Byte-Kopf {proxy, head, size} an.
	// Der Kopf hat 8 Byte {_Myhead, _Mysize} -- mein "size++" schrieb damit in den
	// _Myhead-Zeiger der NAECHSTEN Indexzelle. Stille Speicherkorruption.
	static const uint32_t kFnMapInsertEntry = 0x005F4B50;

	static bool IndexInsert(void* cell, void* obj, int zoneX, int zoneY, int index) {
		typedef void* (__thiscall* Fn)(void*, const void*);
		__try {
			if (!cell || !obj) return false;
			uint32_t ref[4];
			ref[0] = (uint32_t)(uintptr_t)obj;
			ref[1] = (uint32_t)zoneX;
			ref[2] = (uint32_t)zoneY;
			ref[3] = (uint32_t)index;
			Fn f = (Fn)(Host::Re(kFnMapInsertEntry));
			f(cell, ref);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- Index nach einer Reallokation wieder auf den Vektor zeigen lassen ----
	// ObjRef = { obj*, zoneX, zoneY, index }. Der Rohzeiger obj veraltet, sobald
	// zone->static_objects umzieht -- und das tut der Vektor bei JEDEM Anhaengen,
	// das die Kapazitaet sprengt (AppendBuilding, aber auch placeStructure mit
	// Markerlogik). Betroffen sind dann nicht nur UNSERE Knoten, sondern auch die
	// der NATIVEN Objekte der Zone (Truhen, Tueren ...), die das Spiel bei der
	// Generierung eingehaengt hat. Die Kollisionsschleife laeuft ueber genau
	// diese Zeiger -> Sprung auf Muell (der Absturz "Ausfuehren an 0x00004800").
	//
	// Abhilfe: Der Handle (zoneX, zoneY, index) steht redundant im Knoten --
	// genau dafuer. Wir gehen alle 1024 Zellen der Zone ab und setzen obj neu
	// auf begin + index*0x188. Laufzeit: ein paar hundert Knoten, vernachlaessigbar.
	// Layout belegt in RE_Ortsindex.md (Kopf 8 Byte {_Myhead,_Mysize}, Knoten
	// {_Next,_Prev,Wert}, Wert ab +8; zone+0xAC = Zeiger auf ListHead[1024]).
	static int RepointZoneIndexRaw(cube::Zone* zone, int zoneX, int zoneY) {
		__try {
			if (!zone) return 0;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t* begin = raw[0];
			int count = (begin && raw[1] > begin) ? (int)((raw[1] - begin) / 0x188) : 0;
			uint8_t* heads = *(uint8_t**)((uint8_t*)zone + 0xAC);
			if (!heads) return 0;
			int fixed = 0;
			for (int c = 0; c < 1024; c++) {
				uint8_t* head = *(uint8_t**)(heads + c * 8);      // _Myhead (Sentinel)
				if (!head) continue;
				uint8_t* node = *(uint8_t**)head;                 // _Myhead->_Next = begin()
				int guard = 0;
				while (node && node != head && guard++ < 4096) {
					uint32_t* v = (uint32_t*)(node + 8);          // ObjRef
					int idx = (int)v[3];
					if ((int)v[1] == zoneX && (int)v[2] == zoneY && idx >= 0 && idx < count) {
						uint32_t want = (uint32_t)(uintptr_t)(begin + (size_t)idx * 0x188);
						if (v[0] != want) { v[0] = want; fixed++; }
					}
					node = *(uint8_t**)node;                      // _Next
				}
			}
			return fixed;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	// Haengt alle Objekte ab Vektorplatz fromIdx in den Ortsindex. Gedacht fuer
	// Objekte, die placeStructure MIT Markerlogik (typ != 0) zur Laufzeit angelegt
	// hat: das Spiel haengt sie nur in den Vektor -- den Index baut es sonst
	// ausschliesslich waehrend der Zonengenerierung (WorldInfo.cpp:5048-5080).
	// Position aus dem Record: +0x08/+0x10 int64 Fixed16 (Bloecke * 65536).
	static int IndexObjectsFromRaw(cube::World* w, cube::Zone* zone, int zoneX, int zoneY, int fromIdx) {
		__try {
			if (!zone) return 0;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t* begin = raw[0];
			int count = (begin && raw[1] > begin) ? (int)((raw[1] - begin) / 0x188) : 0;
			int done = 0;
			for (int i = (fromIdx < 0 ? 0 : fromIdx); i < count; i++) {
				uint8_t* o = begin + (size_t)i * 0x188;
				int bx = (int)(*(int64_t*)(o + 0x08) >> 16);
				int by = (int)(*(int64_t*)(o + 0x10) >> 16);
				void* cell = TileCell(w, bx, by, zone);
				if (cell && IndexInsert(cell, o, zoneX, zoneY, i)) done++;
			}
			return done;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	// Ziffernbild 3x5 fuer 0-9 und A-F (wie im Modell-Browser).
	static const unsigned char kDig[16][5] = {
		{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
		{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},{7,5,7,5,5},{6,5,6,5,6},
		{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4},
	};
	static void DrawNum(cube::World* w, int x, int y, int z, int value) {
		int d[3], n = 0;
		if (value == 0) d[n++] = 0;
		while (value > 0 && n < 3) { d[n++] = value & 0xF; value >>= 4; }
		int cur = 0;
		for (int i = n - 1; i >= 0; i--) {
			for (int row = 0; row < 5; row++)
				for (int col = 0; col < 3; col++)
					if (kDig[d[i]][row] & (4 >> col))
						PutBlock(w, x + cur + col, y, z + (4 - row), 255, 230, 40, 1);
			cur += 4;
		}
	}

	// [STRG+T] Setzt EIN Objekt des naechsten Typs 40 Bloecke in +X, mit Nummer
	// davor, und haengt es in den Ortsindex. Damit laesst sich Modell UND
	// Benutzbarkeit je Typ ablesen.
	void ObjectTester(cube::GameController* gc) {
		if (!gc || !gc->local_player) { std::cout << "[Tester] Kein Spieler." << std::endl; return; }
		static int typ = 0x00;
		typ++;
		if (typ > 0x50) typ = 0x01;

		cube::World* w = &gc->world;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);
		int bx = px + 40, by = py, bz = pz;   // IMMER 40 Bloecke in +X

		SafeLockWorld(w);
		cube::Zone* zone = SafeGetZone(w, FloorDiv(bx, 256), FloorDiv(by, 256));
		bool placed = false, indexed = false;
		int idx = -1;
		if (zone) {
			// Objekt anlegen und seinen Index im Vektor merken.
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t obj[0x188];
			FillProp3(obj, typ, bx, by, bz, 0, 1.0f, 1.0f, 1.0f);
			*(int*)(obj + 0x30) = 1;              // Zustand "zu"/aktiv
			placed = AppendBuilding(zone, obj);
			if (placed) {
				uint8_t* s = raw[0]; uint8_t* e = raw[1];
				idx = (int)((e - s) / 0x188) - 1;
				void* optr = s + (size_t)idx * 0x188;
				void* cell = TileCell(w, bx, by, zone);
				indexed = IndexInsert(cell, optr, FloorDiv(bx, 256), FloorDiv(by, 256), idx);
			}
		}
		DrawNum(w, px + 20, py, pz + 1, typ);     // Nummer auf halber Strecke
		SafeUnlockWorld(w);
		RemeshLoadedChunks(gc);

		std::cout << "[Tester] Typ 0x" << std::hex << typ << std::dec
			<< "  Block(" << bx << "," << by << "," << bz << ")"
			<< "  Objekt " << (placed ? "gesetzt" : "FEHLGESCHLAGEN")
			<< ", Index " << (indexed ? "eingehaengt" : "NICHT eingehaengt")
			<< " (Vektorplatz " << idx << ")" << std::endl;
		std::cout.flush();
	}

	// ---- Markerfarbe -> Objekttyp -------------------------------------------
	// GEMESSEN mit dem Blockscanner in einem gesetzten Haus (HausBloecke.txt).
	// Die Marker benutzen je Kanal NUR die Werte 0, 127 und 255 -- meine frueheren
	// Schwellen ">=247 oder <=8" haben deshalb alle 127er-Marker verfehlt, und
	// genau die blieben als bunte Bloecke stehen.
	//
	// Aus der Materialliste (Einzelstuecke = Marker, alles andere Baumaterial):
	//   255,255,0   Lampe          -> 0x32 street-light01
	//   0,0,127     CustomizeBench -> 0x4D customization-bench
	//   0,255,127   Amboss         -> 0x48 anvil
	//   127,0,127   Schmied        -> KEIN Objekt (NPC-Standort)
	//   127,127,0   Tisch/Stuhl    -> 0x0C table
	//   255,127,127 Blumenkasten   -> 0x2F flower-box01
	//   255,0,0     Tuer           -> 0x01 door
	//   0,255,0     Fenster        -> 0x03 window
	//   0,0,255     Schild         -> 0x2E (heller als die Bench)
	//   127,255,0   Ofen/Esse      -> 0x47 furnace
	// Objekttypen aus der Modelltabelle im Binary (RE_Objekttypen.md).
	static const int kTypDoor = 0x01;   // door.cub
	static const int kTypWindow = 0x03;   // window.cub
	static const int kTypTable = 0x0C;   // table.cub
	static const int kTypFlower = 0x2F;   // flower-box01.cub
	static const int kTypLamp = 0x32;   // street-light01.cub
	static const int kTypSign = 0x2E;   // Schild
	static const int kTypFurnace = 0x47;   // furnace.cub  (Schmiedeesse)
	static const int kTypAnvil = 0x48;   // anvil.cub
	static const int kTypBench = 0x4D;   // customization-bench.cub

	// Rundet einen Kanal auf die Markerstufen 0 / 127 / 255. Rueckgabe -1, wenn
	// der Wert zu keiner Stufe passt -- dann ist es Baumaterial.
	static int MarkerLevel(uint8_t v) {
		if (v <= 10) return 0;
		if (v >= 117 && v <= 137) return 1;
		if (v >= 245) return 2;
		return -1;
	}

	// ---- Native Skalen je Objekttyp -----------------------------------------
	// GEMESSEN im Objektscan eines echten Dorfhauses (HausScan.txt). Bisher habe
	// ich pauschal 1.0/1.0/1.0 gesetzt -- daher die falschen Proportionen.
	// Die drei Werte gehen nach +0x24 / +0x28 / +0x2C und sind zugleich die
	// Kollisionsbox.
	struct TypScale { int typ; float sx, sy, sz; };
	static const TypScale kScales[] = {
		{ 0x01, 2.0f, 1.0f, 3.0f },   // Tuer      -- schmal, hoch
		{ 0x03, 2.0f, 1.0f, 2.0f },   // Fenster
		{ 0x0A, 1.2f, 0.8f, 0.8f },   // Truhe
		{ 0x0C, 3.0f, 3.0f, 1.0f },   // Tisch
		{ 0x10, 1.0f, 1.0f, 0.5f },   // Stuhl
		{ 0x12, 3.0f, 1.0f, 0.4f },   // Bank
		{ 0x13, 2.0f, 3.0f, 1.0f },   // Bett
		{ 0x1C, 3.0f, 3.0f, 2.5f },
		{ 0x1D, 2.5f, 1.0f, 3.0f },
		{ 0x1E, 3.0f, 1.0f, 1.0f },
		{ 0x20, 2.0f, 1.0f, 1.57f },  // Truhe/Regal
		{ 0x21, 2.0f, 1.0f, 1.57f },
		{ 0x22, 2.0f, 1.0f, 1.57f },
		{ 0x2F, 1.0f, 1.0f, 1.0f },   // Blumenkasten
		{ 0x32, 1.0f, 1.0f, 4.0f },   // Laterne   -- schmal, hoch
		{ 0x47, 2.0f, 2.0f, 1.5f },   // Ofen / Esse
		{ 0x48, 1.5f, 1.0f, 1.0f },   // Amboss
		{ 0x4D, 2.0f, 1.0f, 1.0f },   // CustomizationBench
	};
	static void ScaleFor(int typ, float& sx, float& sy, float& sz) {
		sx = sy = sz = 1.0f;                 // Vorgabe, falls der Typ fehlt
		for (int i = 0; i < (int)(sizeof(kScales) / sizeof(kScales[0])); i++)
			if (kScales[i].typ == typ) { sx = kScales[i].sx; sy = kScales[i].sy; sz = kScales[i].sz; return; }
	}

	static int ClassifyMarker(uint8_t r, uint8_t g, uint8_t b) {
		int R = MarkerLevel(r), G = MarkerLevel(g), B = MarkerLevel(b);
		if (R < 0 || G < 0 || B < 0) return -1;      // Baumaterial
		int code = R * 100 + G * 10 + B;
		switch (code) {
		case 200: return kTypDoor;     // 255,0,0
		case 20:  return kTypWindow;   // 0,255,0
		case 220: return kTypLamp;     // 255,255,0
			// Stufe 1 = 127 (dunkel), Stufe 2 = 255 (hell) -- nicht verwechseln.
		case 1:   return kTypBench;    // 0,0,127  DUNKELblau -> CustomizeBench
		case 2:   return kTypSign;     // 0,0,255  HELLblau   -> Schild
		case 21:  return kTypAnvil;    // 0,255,127 hellgruen
		case 120: return kTypFurnace;  // 127,255,0 Esse
		case 110: return kTypTable;    // 127,127,0 oliv
		case 211: return kTypFlower;   // 255,127,127
		case 101: return -1;           // 127,0,127 Schmied -> NPC, kein Objekt
		default:  return -1;           // Schwarz/Weiss/Grau und Unbekanntes
		}
	}

	// ================= MARKERERSATZ =========================================
	// ZWEI Fehler, die hier zusammen behoben werden:
	//
	// 1. Eine Tuer ist 3x2 Bloecke gross, ein Fenster 2x2. Ich habe pro BLOCK
	//    ein Objekt erzeugt -- also sechs Tueren uebereinander statt einer.
	//    Jetzt werden benachbarte Marker gleicher Farbe zu EINER Gruppe
	//    zusammengefasst und ein Objekt in deren Mitte gesetzt.
	//
	// 2. ObjRef.obj im Ortsindex ist ein ROHZEIGER in den Objektvektor. Wuchs
	//    der Vektor, zog er um, und alle bereits eingehaengten Knoten zeigten
	//    ins Leere -- darueber laeuft die Kollisionsschleife des Spiels. Das war
	//    der Absturz "Ausfuehren an 0x00004800".
	//    Deshalb jetzt zweistufig, genau wie WorldInfo_generateBiomeContent:
	//    erst ALLE Objekte anlegen, DANACH den Index bauen. Nach der letzten
	//    Anhaengung liegt der Vektor fest, die Zeiger bleiben gueltig.

	static const int kMaxMarkers = 256;      // je Haus; ein Haus hat ~20-50 Gruppen

	struct MarkerBlock { int x, y, z, typ; int group; };
	struct MarkerGroup { int typ; int cx, cy, cz; int idx; };  // idx = Platz im Vektor

	// Legt EIN Objekt an, OHNE Indexeintrag. Gibt den Vektorplatz zurueck (-1 = Fehler).
	static int AppendObjectOnly(cube::World* w, int x, int y, int z, int typ) {
		__try {
			float sx, sy, sz;
			ScaleFor(typ, sx, sy, sz);
			cube::Zone* zone = SafeGetZone(w, FloorDiv(x, 256), FloorDiv(y, 256));
			if (!zone) return -1;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t obj[0x188];
			FillProp3(obj, typ, x, y, z, 0, sx, sy, sz);
			*(int*)(obj + 0x30) = 1;                 // Zustand "zu"/aktiv
			if (!AppendBuilding(zone, obj)) return -1;
			return (int)((raw[1] - raw[0]) / 0x188) - 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	// Haengt ein bereits angelegtes Objekt in den Ortsindex. Erst NACH allen
	// Anhaengungen aufrufen, sonst sind die Zeiger veraltet.
	static bool IndexOne(cube::World* w, int x, int y, int z, int idx) {
		__try {
			cube::Zone* zone = SafeGetZone(w, FloorDiv(x, 256), FloorDiv(y, 256));
			if (!zone) return false;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			int count = (int)((raw[1] - raw[0]) / 0x188);
			if (idx < 0 || idx >= count) return false;
			void* optr = raw[0] + (size_t)idx * 0x188;
			void* cell = TileCell(w, x, y, zone);
			return IndexInsert(cell, optr, FloorDiv(x, 256), FloorDiv(y, 256), idx);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Arbeitet auf einem Quader [bx0..bx1] x [by0..by1] x [bz0..bz1] (inklusive).
	// Frueher fest "Radius um ein Hauszentrum, z -8..+36" -- das LAB setzt aber
	// beliebig grosse Strukturen und braucht den genauen Quader.
	static int ReplaceMarkersBox(cube::World* w, int bx0, int by0, int bz0, int bx1, int by1, int bz1) {
		// ---- Schritt 1: alle Markerbloecke einsammeln ----
		static MarkerBlock mb[kMaxMarkers];
		int n = 0;
		for (int x = bx0; x <= bx1 && n < kMaxMarkers; x++)
			for (int y = by0; y <= by1 && n < kMaxMarkers; y++)
				for (int z = bz0; z <= bz1 && n < kMaxMarkers; z++) {
					uint8_t r, g, b, t;
					if (!ReadBlock(w, x, y, z, r, g, b, t)) continue;
					int typ = ClassifyMarker(r, g, b);
					if (typ < 0) continue;
					mb[n].x = x; mb[n].y = y; mb[n].z = z; mb[n].typ = typ; mb[n].group = -1;
					n++;
				}
		if (n == 0) return 0;

		// ---- Schritt 2: benachbarte Marker gleicher Art gruppieren ----
		// Eine Tuer (3x2) wird so zu EINER Gruppe. Nachbarschaft grosszuegig
		// (Abstand <= 2 in jeder Achse), damit auch diagonale Ecken zusammenfinden.
		static MarkerGroup grp[kMaxMarkers];
		int ng = 0;
		for (int i = 0; i < n; i++) {
			if (mb[i].group >= 0) continue;
			int gi = ng++;
			mb[i].group = gi;
			// Breitensuche ueber die Liste (klein genug fuer O(n^2))
			bool grew = true;
			while (grew) {
				grew = false;
				for (int a = 0; a < n; a++) {
					if (mb[a].group != gi) continue;
					for (int b2 = 0; b2 < n; b2++) {
						if (mb[b2].group >= 0 || mb[b2].typ != mb[a].typ) continue;
						if (std::abs(mb[b2].x - mb[a].x) <= 2 &&
							std::abs(mb[b2].y - mb[a].y) <= 2 &&
							std::abs(mb[b2].z - mb[a].z) <= 2) {
							mb[b2].group = gi; grew = true;
						}
					}
				}
			}
			// ---- Ankerpunkt der Gruppe ----
			// Frueher: ganzzahliger Durchschnitt ueber alle Bloecke. Das war in
			// ZWEI Richtungen falsch:
			//   X/Y: bei gerader Breite (Tuer 2 breit, x=10..11) ergibt 21/2 = 10,
			//        also einen halben Block zu weit links/oben. Deshalb wird die
			//        Spanne aufgerundet: (min+max+1)/2 -> 11.
			//   Z:   ein Objekt haengt an seiner UNTERKANTE, nicht an der Mitte.
			//        Bei einer 3 Bloecke hohen Tuer (z=20..22) lag es auf 21 und
			//        damit einen Block zu hoch. Jetzt das Minimum -> 20.
			int minx = 0, maxx = 0, miny = 0, maxy = 0, minz = 0;
			bool first = true;
			for (int a = 0; a < n; a++) {
				if (mb[a].group != gi) continue;
				if (first) { minx = maxx = mb[a].x; miny = maxy = mb[a].y; minz = mb[a].z; first = false; }
				else {
					if (mb[a].x < minx) minx = mb[a].x;
					if (mb[a].x > maxx) maxx = mb[a].x;
					if (mb[a].y < miny) miny = mb[a].y;
					if (mb[a].y > maxy) maxy = mb[a].y;
					if (mb[a].z < minz) minz = mb[a].z;
				}
			}
			grp[gi].typ = mb[i].typ;
			grp[gi].cx = (minx + maxx + 1) / 2;   // aufgerundet -> nach rechts
			grp[gi].cy = (miny + maxy + 1) / 2;
			grp[gi].cz = minz;                    // Unterkante -> nach unten
			grp[gi].idx = -1;
		}

		// ---- Schritt 3: je Gruppe EIN Objekt anlegen (noch ohne Index) ----
		int made = 0;
		for (int i = 0; i < ng; i++) {
			grp[i].idx = AppendObjectOnly(w, grp[i].cx, grp[i].cy, grp[i].cz, grp[i].typ);
			if (grp[i].idx >= 0) made++;
		}

		// ---- Schritt 4: JETZT den Index bauen. Der Vektor waechst nicht mehr,
		//      die Zeiger bleiben gueltig. Genau die Reihenfolge des Originals.
		int indexed = 0;
		for (int i = 0; i < ng; i++)
			if (grp[i].idx >= 0 && IndexOne(w, grp[i].cx, grp[i].cy, grp[i].cz, grp[i].idx)) indexed++;

		// ---- Schritt 5: alle Markerbloecke raeumen ----
		for (int a = 0; a < n; a++) PutBlock(w, mb[a].x, mb[a].y, mb[a].z, 0, 0, 0, 0);

		// ---- Schritt 6: Index der betroffenen Zonen nachziehen ----
		// AppendBuilding kann den Vektor verlegt haben. Unsere eigenen Knoten
		// sind zwar erst danach eingehaengt, die NATIVEN Knoten der Zone aber
		// zeigen noch in den alten Puffer. RepointZoneIndexRaw behebt das.
		int repointed = 0;
		for (int zx = FloorDiv(bx0, 256); zx <= FloorDiv(bx1, 256); zx++)
			for (int zy = FloorDiv(by0, 256); zy <= FloorDiv(by1, 256); zy++) {
				int f = RepointZoneIndexRaw(SafeGetZone(w, zx, zy), zx, zy);
				if (f > 0) repointed += f;
			}

		std::cout << "[Marker] Quader(" << bx0 << "," << by0 << ")-(" << bx1 << "," << by1 << "): "
			<< n << " Bloecke -> " << ng << " Gruppen, "
			<< made << " Objekte, " << indexed << " im Index, "
			<< repointed << " Knoten nachgezogen"
			<< (n >= kMaxMarkers ? "   [GRENZE erreicht]" : "") << std::endl;
		std::cout.flush();
		return made;
	}

	// Alte Schnittstelle: Radius um ein Hauszentrum, z von -8 bis +36.
	static int ReplaceMarkers(cube::World* w, int cx, int cy, int cz, int radius) {
		return ReplaceMarkersBox(w, cx - radius, cy - radius, cz - 8, cx + radius, cy + radius, cz + 36);
	}


	static void PlaceVillageDeco(cube::GameController* gc, int cx, int cy, int deckZ) {
		Spur("PlaceVillageDeco BEGINN");
		cube::World* w = &gc->world;

		// ---- NATIVE HAEUSER + SCHMIEDE ----
		// BEWUSST VOR SafeLockWorld: placeStructure greift selbst auf die Zone zu
		// und wuerde sich an unserer Sperre verklemmen.
		if (kUseNativeHouses) {
			if (!NativeHouseProbe::Available(gc)) {
				std::cout << "[Bergdorf] Native Modelle nicht verfuegbar -- Haeuser fehlen. "
					<< "kUseNativeHouses auf false stellen fuer den Blocknachbau." << std::endl;
			}
			else {
				// Alle vier Gebaeude als native Ganzmodelle. Laeuft VOR der
				// Weltsperre, weil placeStructure selbst auf die Zone zugreift.
				int nb = 0;
				for (size_t i = 0; i < g_houses.size(); i++) {
					// Der Markermodus bestimmt, WAS aus den Platzhaltern wird --
					// aus dem Code gelesen: 0x0D liefert CustomizationBench (0x4D)
					// und Amboss (0x48), 0x0E die uebrigen Stationen. Die
					// Einrichtung (Tisch, Stuhl, Truhe, Blumen) kommt in beiden.
					// mode MUSS 0 sein -- 0x0D/0x0E setzten gar nichts mehr.
					int mmode = 0;
					// Z-VERSATZ: der Generator setzt Gebaeude auf
					// "Terrainoberkante - 7" (game_misc.cpp:28755), der Mod nahm
					// deckZ + 1. Daher standen die Haeuser auf Rampen -- das
					// Modell bringt seinen Sockel selbst mit.
					bool okB = NativeHouseProbe::PlaceBuilding(gc, g_houses[i].model,
						g_houses[i].hx, g_houses[i].hy, deckZ - 7, g_houses[i].rot, mmode);
					if (okB) { nb++; RemeshLoadedChunks(gc); }   // je Gebaeude neu vermeschen
					std::cout << "[Bergdorf] Gebaeude " << i << " (0x" << std::hex
						<< g_houses[i].model << ", Marker 0x" << mmode << std::dec << "): "
						<< (okB ? "gesetzt" : "FEHLGESCHLAGEN") << std::endl;
				}

				// BAEUME im Aussenring -- ebenfalls Registry-Modelle, also hier
				// und nicht unten bei der Objekt-Deko. Wege bleiben frei.
				int nt = 0;
				{
					const double R = 74.0; const int slots = 36;
					for (int k = 0; k < slots; k++) {
						double a = (2.0 * 3.14159265358979 * k) / slots;
						bool onPath = false;
						for (int s = 0; s < kPathSpokes && !onPath; s++) {
							double pa = (2.0 * 3.14159265358979 * s) / kPathSpokes + 0.3926990817;
							double d = a - pa;
							while (d > 3.14159265358979) d -= 2.0 * 3.14159265358979;
							while (d < -3.14159265358979) d += 2.0 * 3.14159265358979;
							if (std::fabs(d) < 0.14) onPath = true;
						}
						if (onPath) continue;
						// Haussektoren freilassen: ein 33 Bloecke breites Haus bei
						// Radius 54 belegt rund +-18 Grad, ein Baum ist nochmal 15
						// breit -- ohne das waechst er in die Hauswand.
						bool atHouse = false;
						for (int h = 0; h < 4 && !atHouse; h++) {
							double ha = 0.7853981634 + h * 1.5707963268;
							double d = a - ha;
							while (d > 3.14159265358979) d -= 2.0 * 3.14159265358979;
							while (d < -3.14159265358979) d += 2.0 * 3.14159265358979;
							if (std::fabs(d) < 0.40) atHouse = true;   // ~23 Grad
						}
						if (atHouse) continue;
						int tx = cx + (int)(std::cos(a) * R), ty = cy + (int)(std::sin(a) * R);
						if (NativeHouseProbe::PlaceBuilding(gc, NativeHouseProbe::Native::Tree,
							tx, ty, deckZ + 1, k % 4)) nt++;
					}
				}
				std::cout << "[Bergdorf] " << nb << "/" << g_houses.size()
					<< " Gebaeude, " << nt << " Baeume gesetzt." << std::endl;
			}
		}

		Spur("Weltsperre nehmen");
		SafeLockWorld(w);
		int ok = 0;

		// (Haeuser und Baeume sind native Registry-Modelle und stehen weiter oben,
		//  vor der Weltsperre. Hier folgt nur noch die Objekt-Deko.)

		// ---- MARKERERSATZ an den vier Gebaeuden ----
		// Unter der Sperre, weil AppendProp den Zonenvektor erweitert.
		if (kUseNativeHouses && kReplaceMarkers) {
			int mk = 0;
			for (size_t i = 0; i < g_houses.size(); i++) {
				SpurN("Markerersatz Haus", (int)i);
				mk += ReplaceMarkers(w, g_houses[i].hx, g_houses[i].hy, deckZ - 7, 22);
			}
			SpurN("Markerersatz fertig, Objekte", mk);
		}
		else if (kUseNativeHouses) {
			Spur("Markerersatz per Notschalter AUS");
		}

		// Kleines Lager + Blumenkaesten-Reihe am Turm-Eingang (Front = -y).
		static const DecoItem tdeco[] = {
			{ 0x18, -6, -22, 0, 1.0f, 1.0f }, { 0x18, 6, -22, 0, 1.0f, 1.0f }, // Barrel
			{ 0x19, -2, -22, 0, 1.0f, 1.0f }, { 0x1a, 2, -22, 0, 1.0f, 1.0f }, // Crate
			{ 0x2f, -4, -18, 0, 1.0f, 1.0f }, { 0x30, -2, -18, 0, 1.0f, 1.0f },
			{ 0x31, 2, -18, 0, 1.0f, 1.0f }, { 0x2f, 4, -18, 0, 1.0f, 1.0f },
		};
		if (kLegacyDeco)
			for (int i = 0; i < (int)(sizeof(tdeco) / sizeof(tdeco[0])); i++)
				if (AppendProp(w, tdeco[i].type, cx + tdeco[i].dx, cy + tdeco[i].dy, deckZ + 1,
					tdeco[i].orient, tdeco[i].sxy, tdeco[i].sz)) ok++;

		if (kLegacyDeco) {
			// ---- FACKELN: beidseitig an ALLEN Wegen, vom Turm bis zum Rand ----
			// Der Lageplan setzt sie ueber das ganze Dorf verteilt, nicht nur auf
			// dem Stueck zwischen Turm und Haus. Sie folgen derselben Schlangenlinie
			// wie das Wegenetz in PlanBuild, stehen also wirklich am Weg.
			for (int s = 0; s < kPathSpokes; s++) {
				double a = (2.0 * 3.14159265358979 * s) / kPathSpokes + 0.3926990817;
				double cs = std::cos(a), sn = std::sin(a);
				double pxp = -sn, pyp = cs;
				for (int r = 20; r <= 74; r += 9) {
					double wind = 3.0 * std::sin((double)r * 0.16);
					for (int side = -1; side <= 1; side += 2) {
						double offs = kPathHalfWidth + 2.0;
						int lx = cx + (int)(cs * r + pxp * (offs * side + wind));
						int ly = cy + (int)(sn * r + pyp * (offs * side + wind));
						if (AppendProp(w, kTypeTorch, lx, ly, deckZ + 1, 0, 1.0f, 4.0f)) ok++;
					}
				}
			}

			// ---- MARKTSTAENDE: Reihe an der zur MITTE zeigenden Hauswand ----
			// Im Lageplan liegen die violetten Striche zwischen Haus und Turm,
			// nicht auf der Aussenseite. Drei Staende je Haus, laengs der Wand.
			{
				int mtypes[3] = { kTypeStallA, kTypeStallB, kTypeStallC };
				for (size_t i = 0; i < g_houses.size(); i++) {
					int ddx = g_houses[i].hx - cx, ddy = g_houses[i].hy - cy;
					int sx = 0, sy = 0;
					if (std::abs(ddx) >= std::abs(ddy)) sx = (ddx > 0 ? 1 : -1); else sy = (ddy > 0 ? 1 : -1);
					// Innenseite = Richtung Mitte; Laengsachse steht senkrecht dazu.
					// Natives Haus ist 3x13 = 39 Bloecke breit (halb 20), der
					// Blocknachbau nur 22x18 -- Abstand entsprechend waehlen.
					int hw = kUseNativeHouses ? 22 : 13, hd = kUseNativeHouses ? 22 : 11;
					int ix = g_houses[i].hx - sx * hw, iy = g_houses[i].hy - sy * hd;
					int tx = sy, ty = sx;
					for (int k = -1; k <= 1; k++) {
						if (AppendProp(w, mtypes[(i + k + 3) % 3], ix + tx * k * 6, iy + ty * k * 6,
							deckZ + 1, 0, 2.0f, 2.0f)) ok++;
					}
					// Blumenkaesten flankieren die Standreihe.
					if (AppendProp(w, kTypeFlowerA, ix + tx * 10, iy + ty * 10, deckZ + 1, 0, 1.0f, 1.0f)) ok++;
					if (AppendProp(w, kTypeFlowerB, ix - tx * 10, iy - ty * 10, deckZ + 1, 0, 1.0f, 1.0f)) ok++;
				}
			}

			// ---- BAENKE: innen, zwischen Turm und Haeusern ----
			// Blaue Striche im Plan: kurze Baenke, quer zur Blickrichtung Turm.
			for (int s = 0; s < kPathSpokes; s++) {
				if (s % 2) continue;                       // nur jede zweite Speiche
				double a = (2.0 * 3.14159265358979 * s) / kPathSpokes + 0.3926990817;
				double cs = std::cos(a), sn = std::sin(a);
				double pxp = -sn, pyp = cs;
				int r = 30;
				double wind = 3.0 * std::sin((double)r * 0.16);
				for (int side = -1; side <= 1; side += 2) {
					int bx = cx + (int)(cs * r + pxp * (6.0 * side + wind));
					int by = cy + (int)(sn * r + pyp * (6.0 * side + wind));
					int orient = (std::fabs(cs) > std::fabs(sn)) ? 1 : 0;
					if (AppendProp(w, kTypeBench, bx, by, deckZ + 1, orient, 2.0f, 1.0f)) ok++;
				}
			}

			// ---- AUSSENRING: Baeume und Blumen im Wechsel ----
			// Ersetzt den frueheren Zaunkranz. Im Plan bildet der Ring aus Gruen
			// und Rosa den Dorfrand; Wege duerfen nicht zugestellt werden.
			{
				const double R = 74.0; const int slots = 72;
				for (int k = 0; k < slots; k++) {
					double a = (2.0 * 3.14159265358979 * k) / slots;
					double cs = std::cos(a), sn = std::sin(a);
					// Liegt der Platz auf einem Weg? Dann freilassen.
					bool onPath = false;
					for (int s = 0; s < kPathSpokes && !onPath; s++) {
						double pa = (2.0 * 3.14159265358979 * s) / kPathSpokes + 0.3926990817;
						double d = a - pa;
						while (d > 3.14159265358979) d -= 2.0 * 3.14159265358979;
						while (d < -3.14159265358979) d += 2.0 * 3.14159265358979;
						if (std::fabs(d) < 0.10) onPath = true;
					}
					if (onPath) continue;
					// Nur noch Blumen -- die Baeume sind native Modelle und stehen
					// bereits oben. Sie liegen etwas weiter innen als der Baumkranz.
					int fx = cx + (int)(cs * (R - 6.0)), fy = cy + (int)(sn * (R - 6.0));
					int ft[3] = { kTypeFlowerA, kTypeFlowerB, kTypeFlowerC };
					if (AppendProp(w, ft[k % 3], fx, fy, deckZ + 1, 0, 1.0f, 1.0f)) ok++;
				}
			}


		}  // Ende kLegacyDeco (Fackeln, Staende, Baenke, Aussenring)

		  // Haus-Innenausstattung (native Props statt Block-Moebel).
		int hok = 0;
		for (size_t i = 0; kLegacyDeco && i < g_decoReqs.size(); i++) {
			const DecoReq& r = g_decoReqs[i];
			if (AppendProp(w, r.type, r.x, r.y, r.z, r.orient, r.sxy, r.sz)) {
				hok++;
				// Werkbank (CustomizationBench, Deko-Typ 0x4D) fuer die
				// Naeherungserkennung merken -- nur wenn sie wirklich gesetzt
				// wurde, sonst zeigt das Overlay eine Bank an, die es nicht gibt.
				// (Naeherungserkennung entfernt)
			}
		}
		Spur("Weltsperre freigeben");
		SafeUnlockWorld(w);
		Spur("Remesh");
		RemeshLoadedChunks(gc);
		Spur("PlaceVillageDeco ENDE");
		// std::cout << "[Bergdorf] Deko-Props: Dorf/Staende/Laternen " << ok
			// << ", Haus-Innendeko " << hok << "/" << g_decoReqs.size() << " platziert (+ Remesh)." << std::endl;
	}

	class MountainVillageMod : public AdventureFramework::IAdventureMod {
	public:
		// Phase 1: beim Betreten einer nativen Siedlung ein echtes Objekt abfangen + dumpen.
		void OnZoneGenerated(cube::GameController* gc, cube::World* world, int zx, int zy, cube::Zone* zone) override {
			// WELTGEN: Raster-Auswahl -> pro Zelle genau ein Dorf, Mindestabstand ~10240 Bloecke.
			if (IsVillageZone(gc, zx, zy)) {
				int cx = zx * 256 + 128, cy = zy * 256 + 128;
				bool known = false;
				for (size_t i = 0; i < g_pending.size(); i++) if (g_pending[i].first == cx && g_pending[i].second == cy) { known = true; break; }
				if (!known) g_pending.push_back(std::make_pair(cx, cy));
			}
			if (g_capCount >= 3 || !zone) return;
			int n = 0;
			if (!CaptureSettlement(zone, &n)) return;
			g_capCount++;
			// std::cout << "[Capture] SIEDLUNG #" << g_capCount << " Zone(" << zx << "," << zy << ") n=" << n << " Objekte. Typen:";
			// for (int i = 0; i < g_capTypeN; i++) std::cout << " " << g_capTypes[i];
			// std::cout << std::endl;
			// Hexdump der eingefangenen Siedlungsobjekte -- Konsolenausgabe ist
			// auskommentiert, deshalb entfaellt auch das Formatieren (sonst
			// liefe die sprintf-Schleife ohne Wirkung).
			// for (int obj = 0; obj < 2; obj++) {
			//     uint8_t* b = (obj == 0) ? g_capbuf : g_capbuf2;
			//     std::cout << "[Capture] Objekt " << obj << " type=" << *(int*)b << ":" << std::endl;
			//     char line[160];
			//     for (int i = 0; i < 0x188; i += 16) {
			//         int p = sprintf(line, "  +%03X:", i);
			//         for (int j = 0; j < 16 && i + j < 0x188; j++) p += sprintf(line + p, " %02X", b[i + j]);
			//         std::cout << line << std::endl;
			//     }
			// }
		}
		// Hilfsfunktion ohne C++-Objekte (vermeidet Fehler C2712)
		static bool CheckGameLoadingSafe(cube::GameController* gc) {
			__try {
				return (*(uint8_t*)((uint8_t*)gc + 0x800585) != 0);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
		void OnGameTick(cube::GameController* gc) override {
			if (!gc || !gc->local_player) return;
			cube::World* w = &gc->world;

			// WELTWECHSEL/RELOAD erkennen (Lade-Flag GC+0x800585). Auf steigender Flanke den
			// Session-Zustand zuruecksetzen -> Doerfer werden neu gescannt/gebaut und NPCs
			// (inkl. Taxi-Vendor) spawnen bei JEDEM Laden neu.
			static bool s_wasLoading = false;

			// HIER IST DIE ÄNDERUNG: Aufruf der Hilfsfunktion!
			bool loading = CheckGameLoadingSafe(gc);

			if (loading && !s_wasLoading) {
				g_scannedZones.clear(); g_npcSpawned.clear();
				g_villages.clear(); g_pending.clear();
				g_building = false; g_npcJobs.clear();
				AirshipTaxi::OnWorldReload();
				// BlacksmithNpc::OnWorldReload();  -- Schmied entfernt
				// std::cout << "[Bergdorf] Weltwechsel -- Session zurueckgesetzt (Doerfer/NPCs neu)." << std::endl;
			}
			s_wasLoading = loading;

			// Verzoegerter NPC-Spawn + Deko (~2s nach Baufertigstellung -> Mesh ist gesetzt).
			// BEWUSST VOR dem Bau-Block: laeuft gerade ein weiterer Dorfbau, kehrt der
			// Tick unten per return zurueck. Stuende die Abarbeitung dahinter, wuerden
			// faellige Auftraege waehrend eines Baus nie drankommen.
			for (size_t i = 0; i < g_npcJobs.size(); ) {
				if (GetTickCount() < g_npcJobs[i].due) { ++i; continue; }
				NpcJob j = g_npcJobs[i];
				g_npcJobs.erase(g_npcJobs.begin() + i);
				PlaceVillageDeco(gc, j.cx, j.cy, j.deckZ); // native Deko-Props (Weg 1)
				SpawnVillagers(gc, j.cx, j.cy, j.deckZ);   // Zwerge + Haendler + Turm-Vendor
				std::cout << "[Bergdorf] NPCs gespawnt bei Block(" << j.cx << "," << j.cy
					<< "), offene Auftraege: " << g_npcJobs.size() << std::endl;
			}

			// Bau-Queue haeppchenweise abarbeiten (pro Frame nur kBlocksPerFrame Bloecke),
			// danach NUR die Dorf-Chunks neu meshen (nicht alle) -> kein Spike/Crash.
			if (g_building) {
				int n = 0;
				int budget = g_instant ? (int)g_queue.size() : kBlocksPerFrame;
				for (; g_qi < g_queue.size() && n < budget; ++g_qi, ++n) {
					BlockOp& o = g_queue[g_qi];
					if (PutBlock(w, o.x, o.y, o.z, o.r, o.g, o.b, o.t)) g_placed++; else g_failed++;
				}
				if (g_qi >= g_queue.size()) {
					g_building = false; g_instant = false;
					// Remesh NUR EINMAL am Ende (nicht pro Frame) -> kein Datenrennen mit dem
					// Chunk-Mesher (das war die Ursache der Korruption).
					UpdateVillageChunks(gc, g_buildCx, g_buildCy);
					std::cout << "[Bergdorf] Bau fertig: " << g_placed << " gesetzt, " << g_failed << " fehlgeschlagen -> NPC-Auftrag in 2s." << std::endl;
					// NPCs erst ~2s spaeter spawnen (Mesh muss sich setzen, sonst stecken sie im Fels).
					// Anhaengen statt ueberschreiben -> mehrere Doerfer gehen nicht verloren.
					NpcJob j; j.due = GetTickCount() + 2000;
					j.cx = g_buildCx; j.cy = g_buildCy; j.deckZ = g_buildDeckZ;
					g_npcJobs.push_back(j);
				}
				return;
			}

			// PERSISTENZ: geladene Zonen um den Spieler nach persistenten Dorf-Markern scannen
			// (einmal je Zone/Session). Marker gefunden -> Dorf an der gespeicherten Stelle bauen.
			{
				int mpx = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
				int mpy = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
				int pzx = mpx / 256, pzy = mpy / 256;
				for (int a = -1; a <= 1 && !g_building; a++) for (int b = -1; b <= 1 && !g_building; b++) {
					int zx = pzx + a, zy = pzy + b;
					bool scanned = false;
					for (size_t i = 0; i < g_scannedZones.size(); i++)
						if (g_scannedZones[i].first == zx && g_scannedZones[i].second == zy) { scanned = true; break; }
					if (scanned) continue;
					cube::Zone* zone = SafeGetZone(w, zx, zy);
					if (!zone) continue; // noch nicht geladen -> spaeter erneut versuchen
					g_scannedZones.push_back(std::make_pair(zx, zy));
					int mcx, mcy;
					if (ScanZoneMarker(zone, mcx, mcy)) {
						bool built = false;
						for (size_t i = 0; i < g_villages.size(); i++)
							if (g_villages[i].first == mcx && g_villages[i].second == mcy) { built = true; break; }
						// Biom schaltet das Bergdorf hier ab -> auch gespeicherte Marker-Doerfer nicht bauen
						// (der Marker bleibt; ohne Biom entsteht das Dorf wieder).
						if (!built && !VillageAllowed(gc, FloorDiv(mcx, 256), FloorDiv(mcy, 256))) built = true;
						if (!built) {
							int surf = SurfaceZ(w, mcx, mcy, 500, -80);
							if (surf > -999999) {
								int deckZ = ComputeDeckZ(w, mcx, mcy, surf);
								std::cout << "[Bergdorf] Marker-Dorf erkannt bei Block(" << mcx << "," << mcy << ") -> baue." << std::endl;
								PlanBuild(gc, mcx, mcy, deckZ);
								RecordVillage(mcx, mcy);
							}
						}
					}
				}
				if (g_building) return; // ein Marker-Dorf wird gerade gebaut
			}

			int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
			int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
			// PROAKTIV: berechnete Dorf-Standorte in der Naehe (wie der Tracker) in die Queue
			// aufnehmen -- unabhaengig vom OnZoneGenerated-Timing -> zuverlaessiges Spawnen an
			// den berechneten Stellen, egal aus welcher Richtung/Geschwindigkeit man kommt.
			{
				int pcx = FloorDiv(px / 256, kCellZones), pcy = FloorDiv(py / 256, kCellZones);
				for (int a = -2; a <= 2; a++) for (int b = -2; b <= 2; b++) {
					int vzx, vzy; VillageZoneForCell(gc, pcx + a, pcy + b, vzx, vzy);
					if (!VillageAllowed(gc, vzx, vzy)) continue;           // im Biom abgeschaltet
					int vcx = vzx * 256 + 128, vcy = vzy * 256 + 128;
					long long ddx = (long long)vcx - px, ddy = (long long)vcy - py;
					if (ddx * ddx + ddy * ddy > 600LL * 600LL) continue; // erst wenn in der Naehe
					bool skip = false;
					for (size_t i = 0; i < g_villages.size(); i++) { long long qx = g_villages[i].first - vcx, qy = g_villages[i].second - vcy; if (qx * qx + qy * qy < 200LL * 200LL) { skip = true; break; } }
					if (skip) continue;
					for (size_t i = 0; i < g_pending.size(); i++) if (g_pending[i].first == vcx && g_pending[i].second == vcy) { skip = true; break; }
					if (skip) continue;
					g_pending.push_back(std::make_pair(vcx, vcy));
				}
			}
			// WELTGEN-Doerfer: naechsten Kandidaten bauen, sobald der Spieler nah + Flaeche geladen ist.
			if (!g_pending.empty()) {
				// Sehr weit entfernte Kandidaten verwerfen (Spieler ist weg; kommt bei Regen erneut).
				for (size_t i = 0; i < g_pending.size(); ) {
					long long dx = (long long)g_pending[i].first - px, dy = (long long)g_pending[i].second - py;
					if (dx * dx + dy * dy > 700LL * 700LL) g_pending.erase(g_pending.begin() + i);
					else i++;
				}
				// SICHERHEIT: nur bauen, wenn der Spieler NAH ist (Zone stabil geladen) und
				// mit Rate-Limit. Weiter entfernt liegt die Zone am Streaming-Rand -> SetBlock
				// dorthin korrumpiert die Karte (schwarze Karte). Reichweite konservativ.
				static DWORD lastBuild = 0;
				if (!g_pending.empty() && GetTickCount() - lastBuild > 5000) {
					int best = -1; long long bestd = 1LL << 60;
					for (size_t i = 0; i < g_pending.size(); i++) {
						long long dx = (long long)g_pending[i].first - px, dy = (long long)g_pending[i].second - py;
						long long d2 = dx * dx + dy * dy;
						if (d2 < 250LL * 250LL && d2 < bestd) { bestd = d2; best = (int)i; }
					}
					if (best >= 0) {
						int cx = g_pending[best].first, cy = g_pending[best].second;
						int surf = SurfaceZ(w, cx, cy, 500, -80);
						// Footprint-Randproben: die GANZE Flaeche (Radius 68) muss stabil geladen
						// sein. Sonst wuerde SetBlock in Streaming-Rand-Zonen schreiben -> schwarze
						// Karte. Ist noch nicht alles geladen, bleibt das Dorf in der Queue.
						bool loaded = (surf > -999999);
						for (int k = 0; k < 8 && loaded; k++) {
							double a = k * 0.7853981634;
							int ex = cx + (int)(std::cos(a) * 88.0), ey = cy + (int)(std::sin(a) * 88.0);
							if (SurfaceZ(w, ex, ey, 500, -80) <= -999999) loaded = false;
						}
						if (loaded) {
							uint32_t base = SeedScout::ReadBase(gc);
							// DECKUNGSGLEICH MIT DEM TRACKER: Dorf an jedem Land-Rasterpunkt (kein Ozean/Struktur);
							// KEINE Berg-Hoehen-Pflicht -- die Terrasse passt sich an jedes Terrain an.
							if (SeedScout::region_site(base, cx / 0x4000, cy / 0x4000).elev < 1) {
								g_pending.erase(g_pending.begin() + best); // Ozean -> kein Dorf (wie im Tracker)
							}
							else {
								int maxS, minS, avgS; ScanFootprint(w, cx, cy, surf, maxS, minS, avgS);
								// KEINE "zu steil"-Sperre mehr -- auch steiles Terrain wird bebaut; die Terrasse
									// gruendet/schneidet sich bis zum echten Boden/Fels (siehe PlanBuild).
								if (SeedScout::NearStructure(base, cx, cy, kNativeAvoid) || NearNativeStructure(w, cx, cy)) {
									// In/bei nativer Struktur (Dorf/Dungeon/Bauwerk, aus dem Seed berechnet)
									// oder static_objects-Struktur -> hier KEIN eigenes Dorf.
									g_pending.erase(g_pending.begin() + best);
									// std::cout << "[Bergdorf] Native Struktur in der Naehe -> uebersprungen." << std::endl;
								}
								else {
									g_pending.erase(g_pending.begin() + best);
									lastBuild = GetTickCount();
									int deckZ = avgS + 1; // Terrasse auf mittlerer Hoehe
									// std::cout << "[Bergdorf] Weltgen-Bergdorf entsteht bei Block(" << cx << ", " << cy << "), Deck Z=" << deckZ
										// << " (Spread " << (maxS - minS) << ")." << std::endl;
									PlanBuild(gc, cx, cy, deckZ);
									RecordVillage(cx, cy);
									PlaceVillageMarker(gc, cx, cy, deckZ); // persistenter Trigger fuer kuenftige Ladevorgaenge
								}
							}
						}
						// sonst: noch nicht komplett geladen -> in der Queue lassen, spaeter erneut
					}
				}
			}
		}
	};
	static MountainVillageMod g_mod;

	// Name eines Feature-Typs (Ground-truth aus dem Spiel).
	static const char* StructName(int t) {
		switch (t) {
		case 1: return "Dorf";            case 2: return "Bergdungeon";  case 3: return "Walddungeon";
		case 4: return "Seedungeon";      case 5: return "Ruine";        case 6: return "Schluchtdungeon";
		case 7: return "Taldungeon";      case 8: return "Schluchtdungeon"; case 10: return "Portal";
		case 11: return "Felsdungeon";    case 12: return "Baumdungeon"; case 14: return "Dungeon";
		case 15: return "Schluchtdungeon"; default: return "Struktur";
		}
	}

	// F9-Tracker: eigene (persistente) Bergdoerfer + naechste native Strukturen zum Meiden.
	void AppendTracking(cube::GameController* gc, std::string& overlay) {
		if (!gc || !gc->local_player) return;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		char buf[192];

		uint32_t base = SeedScout::ReadBase(gc);

		// --- Bergdorf-Standorte (deterministisch aus Seed + Raster) -- fuehrt dich hin ---
		// Zeigt auch noch nicht gebaute Doerfer. Ozean- und Struktur-blockierte Zellen werden
		// gefiltert; bereits gebaute sind markiert.
		struct DV { int x, y; bool built; };
		std::vector<DV> dv;
		int pzx = px / 256, pzy = py / 256;
		int pcx = FloorDiv(pzx, kCellZones), pcy = FloorDiv(pzy, kCellZones);
		for (int a = -2; a <= 2; a++) for (int b = -2; b <= 2; b++) {
			int vzx, vzy; VillageZoneForCell(gc, pcx + a, pcy + b, vzx, vzy);
			if (!VillageAllowed(gc, vzx, vzy)) continue;                   // im Biom abgeschaltet
			int vx = vzx * 256 + 128, vy = vzy * 256 + 128;
			if (SeedScout::region_site(base, vx / 0x4000, vy / 0x4000).elev < 1) continue; // Ozean raus
			if (SeedScout::NearStructure(base, vx, vy, kNativeAvoid)) continue;             // Struktur blockiert
			bool built = false;
			for (size_t i = 0; i < g_villages.size(); i++) {
				long long ddx = g_villages[i].first - vx, ddy = g_villages[i].second - vy;
				if (ddx * ddx + ddy * ddy < 200LL * 200LL) { built = true; break; }
			}
			DV d; d.x = vx; d.y = vy; d.built = built; dv.push_back(d);
		}
		std::vector<std::pair<long long, int> > vorder;
		for (size_t i = 0; i < dv.size(); i++) {
			long long dx = dv[i].x - px, dy = dv[i].y - py;
			vorder.push_back(std::make_pair(dx * dx + dy * dy, (int)i));
		}
		std::sort(vorder.begin(), vorder.end());
		if (dv.empty()) {
			overlay += "Bergdoerfer: keine in der Naehe berechnet\n";
		}
		else {
			overlay += "Bergdoerfer (Ziele):\n";
			for (size_t i = 0; i < vorder.size() && i < 6; i++) {
				DV& d = dv[vorder[i].second];
				double dist = std::sqrt((double)vorder[i].first);
				sprintf(buf, "  %s (%d, %d)  %.0f Bl  %s\n", d.built ? "Dorf [gebaut]" : "Dorf         ",
					d.x, d.y, dist, Compass(d.x - px, d.y - py));
				overlay += buf;
			}
		}

		// --- Naechste native Strukturen (zum Meiden), nach Entfernung ---
		std::vector<SeedScout::Feature> fs; SeedScout::FeaturesNear(base, px, py, fs);
		std::vector<std::pair<long long, int> > so;
		for (size_t i = 0; i < fs.size(); i++) {
			long long dx = fs[i].x - px, dz = fs[i].z - py;
			so.push_back(std::make_pair(dx * dx + dz * dz, (int)i));
		}
		std::sort(so.begin(), so.end());
		if (!so.empty()) {
			overlay += "Native Strukturen in der Naehe (meiden):\n";
			for (size_t i = 0; i < so.size() && i < 4; i++) {
				SeedScout::Feature& ft = fs[so[i].second];
				double dist = std::sqrt((double)so[i].first);
				sprintf(buf, "  %-15s (%d, %d)  %.0f Bl  %s\n", StructName(ft.type), ft.x, ft.z, dist, Compass(ft.x - px, ft.z - py));
				overlay += buf;
			}
		}
	}

	// PERSISTENZ-TEST (STRG+M): setzt EINEN Marker-Block persistent (0x14-Record + zone dirty).
	// Nach Reload muss der Marker (Magenta) noch da sein -> DB-Weg funktioniert. BACKUP der Weltdatei!
	void PersistMarkerAtPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		cube::World* w = &gc->world;
		int bx = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int by = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int bz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);
		int mz = bz + 3; // Marker knapp ueber dem Spieler
		int zx = bx / 256, zy = by / 256;
		cube::Zone* zone = SafeGetZone(w, zx, zy);
		if (!zone) { return; }   // (Konsolenausgabe entfernt)
		// // Diagnose: Zone-Offsets loggen (zur Verifikation, ob GetZone das Save-Objekt liefert)
		int zoX = -1, zoY = -1; int dirty = -1; long long nrec = -1;
		__try {
			zoX = *(int*)((uint8_t*)zone + 0x60);
			zoY = *(int*)((uint8_t*)zone + 0x64);
			dirty = *(uint8_t*)((uint8_t*)zone + 0x75);
			uint8_t** v = (uint8_t**)((uint8_t*)zone + 0x68);
			if (v[0] && v[1] && v[1] > v[0]) nrec = (long long)((v[1] - v[0]) / 0x14);
			else nrec = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		// std::cout << "[Marker] Zone(" << zx << "," << zy << ")  +0x60=" << zoX << "  +0x64=" << zoY
			// << "  dirty(+0x75)=" << dirty << "  0x14-Records=" << nrec << std::endl;
		// Marker-Record anhaengen + Zone dirty setzen
		uint8_t rec[0x14];
		BuildRecord14(rec, bx, by, mz, 255, 0, 255, 1, -1); // Magenta, id=-1 (wird beim Laden immer angewandt)
		bool ok = AppendZoneRecord14(zone, rec);
		if (ok) { __try { *(uint8_t*)((uint8_t*)zone + 0x75) = 1; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
		PutBlock(w, bx, by, mz, 255, 0, 255, 1); // in-memory, sofort sichtbar
		UpdateVillageChunks(gc, bx, by);
		// std::cout << "[Marker] Magenta-Marker bei Block(" << bx << "," << by << "," << mz << ") "
			// << (ok ? "gesetzt + Zone dirty. >>> BACKUP der Save/world_*.db, dann Welt neu laden. <<<"
				   // : "FEHLGESCHLAGEN (Record-Anhaengen).") << std::endl;
	}

	// WEG-1-TEST: haengt native Deko-Props (Table/Cupboard/Vase/FlowerBox/StreetLight) an die
	// static_objects der Zone an + erzwingt Remesh. Wenn sie RENDERN, geht der native Weg.
	void PropTestAtPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		cube::World* w = &gc->world;
		int bx = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int by = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int bz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);
		cube::Zone* zone = SafeGetZone(w, bx / 256, by / 256);
		if (!zone) { return; }   // (Konsolenausgabe entfernt)
		static const int types[5] = { 0x0c, 0x1d, 0x38, 0x2f, 0x32 };
		static const char* names[5] = { "Table", "Cupboard", "Vase1", "FlowerBox1", "StreetLight" };
		SafeLockWorld(w);
		for (int i = 0; i < 5; i++) {
			uint8_t obj[0x188];
			FillBuilding(obj, types[i], bx + 3 + i * 2, by + 3, bz, 0);
			*(float*)(obj + 0x24) = 1.0f; // vis_scale 1.0 (Prop, nicht Gebaeude)
			bool r = AppendBuilding(zone, obj);
			// std::cout << "[PropTest] " << names[i] << " (0x" << std::hex << types[i] << std::dec
				// << ") bei Block(" << (bx + 3 + i * 2) << "," << (by + 3) << "," << bz << ") -> "
				// << (r ? "angehaengt" : "FEHLGESCHLAGEN") << std::endl;
		}
		SafeUnlockWorld(w);
		RemeshLoadedChunks(gc);
		// std::cout << "[PropTest] 5 native Props angehaengt + Remesh. Schau, ob eines rendert." << std::endl;
	}

	// STRG+2: EINEN Test-Zwerg an der Spielerposition spawnen (ohne Dedup) -- zum schnellen
	// Ausprobieren der Flags in MakeVillager (Faktion +0x60, hostility_flags).
	void SpawnTestDwarfAtPlayer(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		const int64_t U = (int64_t)kUnitsPerBlock;
		int64_t bx = gc->local_player->entity_data.position.X;
		int64_t by = gc->local_player->entity_data.position.Y;
		int64_t bz = gc->local_player->entity_data.position.Z;
		Vector3<int64_t> pos(bx + 3 * U, by, bz + 2 * U); // 3 Bloecke neben, 2 ueber dem Spieler
		cube::Creature* c = cube::util::SpawnMonster(pos, cube::Creature::Race::DwarfMale, cube::util::Hostility::Talkable, /*level=*/1);
		if (c) { MakeVillager(c); cube::util::AttachRandomWalk(c, pos.X, pos.Y); }
		// std::cout << "[Bergdorf] Test-Zwerg gespawnt (" << (c ? "ok" : "NULL") << ") -- Flags in MakeVillager anpassen." << std::endl;
	}

	// Public: naechstes Dorf in Himmelsrichtung (fuer AirshipTaxi). dir 0=N 1=O 2=S 3=W.
	bool NextVillageInDirection(cube::GameController* gc, int fromBlockX, int fromBlockY,
		int dir, int& outBlockX, int& outBlockY) {
		return NextVillageInDirectionImpl(gc, fromBlockX, fromBlockY, dir, outBlockX, outBlockY);
	}

	// =====================================================================
	// LAB-SCHNITTSTELLE  (siehe MountainVillage.h, Abschnitt "LAB")
	// Alles hier laeuft auf dem SPIELTHREAD -- das LAB-Fenster ruft es nie
	// direkt, sondern ueber LabBridge::Post (Auftragswarteschlange).
	// =====================================================================
	void SetVillageFilter(VillageFilter f) { g_villageFilter = f; }
	bool IsVillageSiteZone(cube::GameController* gc, int zx, int zy) { return gc && IsVillageZoneRaw(gc, zx, zy); }

	bool LabReadBlock(cube::GameController* gc, int x, int y, int z, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& t) {
		if (!gc) return false;
		return ReadBlock(&gc->world, x, y, z, r, g, b, t);
	}
	bool LabPutBlock(cube::GameController* gc, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t t) {
		if (!gc) return false;
		return PutBlock(&gc->world, x, y, z, r, g, b, t);
	}
	static int AppendObjectRaw(cube::World* w, const LabObj& o) {
		__try {
			cube::Zone* zone = SafeGetZone(w, FloorDiv(o.x, 256), FloorDiv(o.y, 256));
			if (!zone) return -1;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t obj[0x188];
			FillProp3(obj, o.type, o.x, o.y, o.z, o.orient & 3, o.sx, o.sy, o.sz);
			*(int*)(obj + 0x30) = 1;
			if (!AppendBuilding(zone, obj)) return -1;
			return (int)((raw[1] - raw[0]) / 0x188) - 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	int LabCreateObjects(cube::GameController* gc, const std::vector<LabObj>& objs) {
		if (!gc || objs.empty()) return 0;
		cube::World* w = &gc->world;
		std::vector<int> idx(objs.size(), -1);
		int made = 0;
		for (size_t i = 0; i < objs.size(); i++) { idx[i] = AppendObjectRaw(w, objs[i]); if (idx[i] >= 0) made++; }
		for (size_t i = 0; i < objs.size(); i++) if (idx[i] >= 0) IndexOne(w, objs[i].x, objs[i].y, objs[i].z, idx[i]);
		std::vector<std::pair<int, int>> zones;
		for (const LabObj& o : objs) {
			std::pair<int, int> z(FloorDiv(o.x, 256), FloorDiv(o.y, 256));
			if (std::find(zones.begin(), zones.end(), z) == zones.end()) zones.push_back(z);
		}
		for (const auto& z : zones) RepointZoneIndexRaw(SafeGetZone(w, z.first, z.second), z.first, z.second);
		return made;
	}
	int LabReplaceMarkersBox(cube::GameController* gc, int x0, int y0, int z0, int x1, int y1, int z1) {
		if (!gc) return 0;
		return ReplaceMarkersBox(&gc->world, x0, y0, z0, x1, y1, z1);
	}
	int LabRepointZoneIndex(cube::GameController* gc, int zx, int zy) {
		if (!gc) return 0;
		return RepointZoneIndexRaw(SafeGetZone(&gc->world, zx, zy), zx, zy);
	}
	int LabIndexObjectsFrom(cube::GameController* gc, int zx, int zy, int fromIdx) {
		if (!gc) return 0;
		return IndexObjectsFromRaw(&gc->world, SafeGetZone(&gc->world, zx, zy), zx, zy, fromIdx);
	}
	static int ObjectCountRaw(cube::Zone* zone) {
		__try {
			if (!zone) return -1;
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			return (raw[0] && raw[1] > raw[0]) ? (int)((raw[1] - raw[0]) / 0x188) : 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}
	int LabObjectCount(cube::GameController* gc, int zx, int zy) {
		if (!gc) return -1;
		return ObjectCountRaw(SafeGetZone(&gc->world, zx, zy));
	}

	// Objekttyp -> Markerfarbe (Umkehrung von ClassifyMarker). So landen die
	// Objekte unserer Bauplaene als Marker in der erfassten Struktur und werden
	// beim Setzen durch ReplaceMarkers wieder zu echten Objekten.
	static bool MarkerColorFor(int type, uint8_t& r, uint8_t& g, uint8_t& b) {
		switch (type) {
		case 0x01: r = 255; g = 0;   b = 0;   return true;   // Tuer
		case 0x03: r = 0;   g = 255; b = 0;   return true;   // Fenster
		case 0x32: r = 255; g = 255; b = 0;   return true;   // Lampe
		case 0x4D: r = 0;   g = 0;   b = 127; return true;   // CustomizeBench
		case 0x2E: r = 0;   g = 0;   b = 255; return true;   // Schild
		case 0x48: r = 0;   g = 255; b = 127; return true;   // Amboss
		case 0x47: r = 127; g = 255; b = 0;   return true;   // Esse
		case 0x0C: r = 127; g = 127; b = 0;   return true;   // Tisch
		case 0x2F: case 0x30: case 0x31:
			r = 255; g = 127; b = 127; return true;   // Blumen
		default: return false;
		}
	}

	bool LabCapturePlan(cube::GameController* gc, int what, std::vector<LabBlock>& out, std::string& name) {
		out.clear();
		if (!gc || !gc->local_player) { name = "kein Spieler"; return false; }
		if (g_building) { name = "Dorfbau laeuft gerade -- spaeter erneut"; return false; }
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)kUnitsPerBlock);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)kUnitsPerBlock);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)kUnitsPerBlock);

		// Laufende Warteschlangen sichern -- die Plan-Funktionen schreiben in
		// g_queue / g_decoReqs. Danach unveraendert zurueck. Die Welt wird NICHT
		// angefasst: P() reiht nur ein, gesetzt wird erst vom Bau-Tick.
		std::vector<BlockOp> savedQ; savedQ.swap(g_queue);
		std::vector<DecoReq> savedD; savedD.swap(g_decoReqs);
		size_t savedQi = g_qi;

		int zMin = pz;   // alles darunter ist gelaendeabhaengiges Fundament
		switch (what) {
		case 0: name = "Turm (Bergdorf)";            PlanTower(&gc->world, px, py, pz);        zMin = pz;     break;
		case 1: name = "Haus (Blocknachbau)";        PlanHouse(px, py, pz, 22, 18, 11, 0);    zMin = pz - 1; break;
		case 2: name = "Schmiede (Blocknachbau)";    PlanSmithy(px, py, pz, 0);               zMin = pz - 1; break;
		default: name = "unbekannt"; break;
		}

		for (size_t i = 0; i < g_queue.size(); i++) {
			const BlockOp& o = g_queue[i];
			if (o.t == 0 || o.z < zMin) continue;          // Luft-Schnitte und Fundament weglassen
			LabBlock b; b.x = o.x - px; b.y = o.y - py; b.z = o.z - zMin; b.r = o.r; b.g = o.g; b.b = o.b;
			out.push_back(b);
		}
		int markers = 0;
		for (size_t i = 0; i < g_decoReqs.size(); i++) {
			const DecoReq& d = g_decoReqs[i];
			LabBlock b;
			if (!MarkerColorFor(d.type, b.r, b.g, b.b)) continue;
			b.x = d.x - px; b.y = d.y - py; b.z = d.z - zMin;
			out.push_back(b); markers++;
		}

		g_queue.swap(savedQ); g_decoReqs.swap(savedD); g_qi = savedQi;
		std::cout << "[LAB] Bauplan '" << name << "' erfasst: " << out.size() << " Bloecke ("
			<< markers << " davon Marker)" << std::endl;
		return !out.empty();
	}

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_mod);
		// std::cout << "[Bergdorf] Bereit -- Bergdoerfer entstehen automatisch in den Bergen (Weltgen). STRG+0 = Testbau, STRG+M = Persistenz-Marker." << std::endl;
	}
}
