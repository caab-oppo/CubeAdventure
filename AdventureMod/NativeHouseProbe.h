#pragma once
//
// NativeHouseProbe.h
// ---------------------------------------------------------------
// MACHBARKEITSPROBE (Hotkey STRG+7): Kann der Mod ein natives Hausteil
// setzen, oder muessen die Haeuser aus Bloecken nachgebaut werden?
//
// Befund aus dem Reversal-Baum:
//   Ein natives Haus ist KEIN Fertigteil. Es ist ein House-Objekt
//   (0x74 Byte, Raster 3x3x4 Zellen a 12 Byte, ctor @ 0x5AEB40), dessen
//   Zellen im Generator einzeln gefuellt werden:
//       VoxelChunk_cellAt(x,y,z)[0] = 2   // Fundament
//                                  = 1   // Etage
//                                  = 3   // Dach
//   Anschliessend laeuft der Generator ueber die Zellen und stanzt je Zelle
//   EIN Modell in die Welt:
//       WorldInfo_placeStructure(tmpl, pos, rot, color, kind, zone, flag, pal, x)
//   Rastermasse aus dem Generator: 0xD = 13 Bloecke je Zelle in X/Y,
//   7 Bloecke je Stockwerk in Z.
//
//   Die Modellvorlagen kommen aus einer Registry, die GameController::ctor_0
//   ueber loadStructureBlob @ 0x4809A0 fuellt. Der Generator holt sie mit
//       vectorU32_at(index)      // @ 0x4120C0, thiscall, ECX = Registry
//   Hausstile belegen die Indizes 0x881..0x8EE; ein Stil braucht ~15 Vorlagen
//   (Wand, Ecke, Tuer, Fenster, Dach, ...).
//
// Offene Frage, die genau diese Probe beantwortet:
//   Funktioniert placeStructure AUSSERHALB der Zonengenerierung? Der Generator
//   ruft es auf, solange der Chunk noch nicht vermescht ist. Wenn die Funktion
//   das voraussetzt, ist der native Weg fuer den Mod tot und wir bauen nach.
//
// WARNUNG: Der Aufruf laeuft im EndScene-Hook, also in einem Win32-Callback.
// Ein dort geschlucktes 0xC0000005 beendet den Prozess trotzdem (0xC000041D).
// Deshalb wird VOR dem Aufruf alles geprueft, was pruefbar ist, und bei der
// kleinsten Unstimmigkeit NICHT gerufen.
//
#include "cwsdk.h"

namespace NativeHouseProbe {

	// [STRG+7] Einmalige Probe vor dem Spieler. Gibt jeden Schritt auf der
	// Konsole aus, damit im Fehlerfall klar ist, woran es lag.
	void Probe(cube::GameController* gc);

	// Nur die Vorpruefungen (Registry, Prolog, Argumentmodell) ohne Aufruf.
	// Laeuft ohne jedes Risiko und sagt schon fast alles.
	void DryRun(cube::GameController* gc);

	// ---- Nativer Hausbau ----------------------------------------------------
	// Die Hausbausaetze stehen als Modelldateinamen im Binary, je Stil dieselben
	// 13 Teile: -base, -floor, -floor-stairs, -wall, -wall-window, -wall-door,
	// -wall-indoor, -wall-balcony, -wall-lamp, -roof1, -roof2, -roof3, -arc.
	// Gesucht wird ueber den NAMEN, nicht ueber geratene Registry-Indizes.
	// ABER: es gibt fertige GANZGEBAEUDE, die den Bausatz ueberfluessig machen.
	// Bestaetigt im Spiel: Index 0x903 ist die komplette Schmiede, 32x31 Bloecke.
	// Die uebrigen Indizes ergeben sich aus der Reihenfolge der Namen im Binary
	// (die Zeichenketten liegen in genau dieser Folge in .rdata) und sind damit
	// hergeleitet, nicht gemessen -- stimmt eines nicht, hier die Zahl aendern.
	// Belegt durch den Registry-Auszug: um die bestaetigte Schmiede (0x903, 32x31)
	// liegen SIEBEN grosse Gebaeude in Folge -- und genau sieben Klassen-/Handwerks-
	// haeuser stehen im Binary (warrior, ranger, rogue, mage, smithy,
	// carpentersshop, weavingmill). Die Groessen passen durchweg:
	//
	//   0x8FF 32x32   0x900 32x40   0x901 32x32   0x902 22x22
	//   0x903 32x31 <- Schmiede, im Spiel bestaetigt
	//   0x904 32x40   0x905 32x32
	//
	// Davor liegen zehn Eintraege fuer die zehn uebrigen Ortsnamen
	// (4x stilt, 4x frame-, stone-well, landingplace) -- 0x8F5..0x8FE.
	// IM SPIEL BESTAETIGT (siehe MODELL_INDEX.md). Diese sind gesehen, nicht
	// hergeleitet -- die frueheren Herleitungen aus der .rdata-Reihenfolge waren
	// falsch (0x8FE ist ein Wachturm, kein Landeplatz).
	namespace Native {
		static const int Tree      = 0x832;  // 15x15  Baum
		static const int Temple1   = 0x84C;  // 32x32
		static const int Temple2   = 0x850;  // 32x32
		static const int House1    = 0x87E;  // 33x33  Wohnhaus
		static const int House2    = 0x87F;  // 33x33  Wohnhaus
		static const int House3    = 0x880;  // 33x33  Wohnhaus
		static const int Well      = 0x882;  // 10x10  Brunnen
		static const int Stilt1    = 0x884;  // 32x32  Staenderbau mit Steg
		static const int Stilt2    = 0x885;  // 32x32
		static const int Stilt3    = 0x886;  // 20x30
		static const int DockTower = 0x887;  // 32x32  Turm mit Andockstelle
		static const int Stairs    = 0x889;  // 14x14  Treppe
		static const int WatchTower= 0x8FE;  // 20x20  Wachturm
		static const int Gateway   = 0x8FF;  // 32x32  Durchgang
		static const int Smithy    = 0x903;  // 32x31  Schmiede
		static const int Torches   = 0x9E2;  //  5x5   Standfackeln
		static const int Ship      = 0x9F4;  // 16x26  Schiff
	}

	// VERALTET -- aus der .rdata-Reihenfolge hergeleitet und nachweislich
	// falsch. Bleibt nur stehen, damit alte Verweise noch bauen.
	namespace Model {
		static const int StiltInn1     = 0x8F5;  // 14x14
		static const int StiltHut2     = 0x8F6;  // 20x20
		static const int StiltHut3     = 0x8F7;  // 18x18
		static const int StiltPath     = 0x8F8;  // 16x16
		static const int FrameHouse01  = 0x8F9;  // 16x16  Wohnhaus
		static const int FrameInn      = 0x8FA;  // 16x16  Gasthaus
		static const int FrameShop     = 0x8FB;  // 14x14  Laden
		static const int StoneWell     = 0x8FD;  //  6x6   Brunnen
		static const int LandingPlace  = 0x8FE;  // 20x20
		static const int Warrior       = 0x8FF;  // 32x32  grosses Wohnhaus
		static const int Ranger        = 0x900;  // 32x40
		static const int Rogue         = 0x901;  // 32x32
		static const int Mage          = 0x902;  // 22x22
		static const int Smithy        = 0x903;  // 32x31  BESTAETIGT
		static const int Carpenters    = 0x904;  // 32x40
		static const int WeavingMill   = 0x905;  // 32x32
	}

	// Setzt EIN fertiges Gebaeude, zentriert auf Block (cx,cy), Sockel bei cz.
	// rot = Drehung 0..3 (im Spiel -90 Grad je Schritt).
	// Rueckgabe: true, wenn gesetzt.
	// mode steuert, WELCHE Objekte aus den Markern entstehen -- aus dem Code
	// gelesen (WorldInfo.cpp, Zweige "zone == 0xd" / "zone == 0xe"):
	//   MarkerSmithy (0x0D): 0x4D CustomizationBench, 0x48 Amboss, 0x47, 0x49
	//   MarkerHouse  (0x0E): 0x4C, 0x4B, 0x4A
	// Die uebrige Einrichtung (Tisch 0x13, Stuhl 0x14, Truhe 0x20-0x22,
	// 0x0C, 0x10, 0x38+, Blumen 0x2F-0x31) entsteht in BEIDEN Modi.
	static const int MarkerSmithy = 0x0D;
	static const int MarkerHouse  = 0x0E;
	bool PlaceBuilding(cube::GameController* gc, int modelIndex, int cx, int cy, int cz,
	                   int rot, int mode = 0);

	// Zaehlt die Blocktypen im Grundriss eines gesetzten Gebaeudes und meldet,
	// ob benutzbare Typen (0x01,0x0A,0x0B,0x0C,0x0D,0x15) dabei sind.
	void InspectBuilding(cube::GameController* gc, int cx, int cy, int cz, int w, int h);

	// Setzt an allen Objektpositionen im Umkreis den benutzbaren Blocktyp.
	// Wird von PlaceBuilding automatisch aufgerufen; einzeln nutzbar, um
	// bereits gesetzte Gebaeude nachtraeglich zu reparieren.
	int MakeObjectsUsable(cube::GameController* gc, int cx, int cy, int cz, int radius);

	// True, wenn das zuletzt gesetzte Gebaeude ueber den Rueckfall lief.
	bool UsedFallback();

	// Macht die bunten Markerbloecke im Grundriss benutzbar (Typ 1).
	int MakeMarkersUsable(cube::GameController* gc, int cx, int cy, int cz, int radius);

	// Groesse eines Modells in Bloecken (fuer Abstandsberechnung im Dorf).
	bool ModelSize(cube::GameController* gc, int modelIndex, int& w, int& h);

	// [STRG+8] Schreibt die komplette Registry als Textdatei neben Cube.exe:
	// Index, Groesse, Kategorie (GEBAEUDE/Bauteil/Deko) und Name, soweit lesbar.
	void DumpRegistry(cube::GameController* gc);

	// ---- SCANNER: bestehende Bauten erfassen --------------------------------
	// [STRG+J] Liest alle zone_static_object-Records im Umkreis um den Spieler
	// und schreibt sie als Stueckliste nach HausScan.txt:
	//     Typ, Position RELATIV zum Spieler (Bloecke), Drehung, Skalen.
	// In einem nativen Dorf ist das genau der Bauplan eines Hauses -- damit
	// laesst sich der Zusammenbau eins zu eins nachbauen, statt ihn zu raten.
	// Mehrfaches Druecken haengt einen neuen Abschnitt an, damit man mehrere
	// Haeuser nacheinander erfassen kann.
	void ScanArea(cube::GameController* gc, int radiusBlocks);

	// [STRG+J zweimal kurz] Setzt die Datei zurueck (neuer Scan von vorn).
	void ResetScan();

	// True, sobald Registry und Vorlagen einmal erfolgreich geprueft wurden.
	bool Available(cube::GameController* gc);

	// Registry-Index von framework-wall-lamp.cub (fest 0x895, aus dem Lader
	// @0x4EE230 abgelesen -- die Namenssuche konnte nie treffen).
	// Rueckgabe -1, wenn nicht gefunden.
	int FindWallLampModel(cube::GameController* gc);

	// LAB: Modellobjekt (cube::Sprite*) zu einem Registry-Index, sonst nullptr.
	// Layout: +0x30 RGB-Zeiger, +0x44/+0x48/+0x4C Groesse X/Y/Z.
	void* RegistryModel(cube::GameController* gc, int index);
	// LAB: Adresse des SpriteManagers (Registry), 0 = nicht gefunden.
	uint32_t RegistryBase(cube::GameController* gc);

	// [STRG+8] Setzt die naechsten zehn Modelle als beschriftete Reihe vor den
	// Spieler (Richtung +X, 22 Bloecke Abstand). Jeder weitere Druck zeigt die
	// naechsten zehn. So finden wir empirisch, welcher Index welches Bauteil ist.
	void BrowseModels(cube::GameController* gc);

	// [STRG+Y] Naechste Argumentkombination fuer placeStructure und ein
	// Testhaus damit setzen -- sucht den Schalter, der Tueren/Fenster erzeugt.
	void CycleArgs(cube::GameController* gc);

	// Startindex des Browsers neu setzen.
	void ResetBrowse(int base);
}
