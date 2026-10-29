// LabWindow.cpp -- das LAB als zweites Fenster. Siehe LabWindow.h.
//
// Aufbau:
//   1. Zustand (nur der Fensterthread fasst ihn an -- keine Sperren noetig)
//   2. Kleine Helfer (Steuerelemente anlegen, Zahlen lesen, Protokoll)
//   3. Spielfunktionen: alle bisherigen LAB-Tasten als Tabelle
//   4. Bibliothek (nativ / eigene / Export / Scans / Mod-Bauwerke)
//   5. Editor-Operationen
//   6. 3D-Ansicht (isometrisch, GDI, Maler-Algorithmus)
//   7. Ebenen-Editor und Palette
//   8. Hauptfenster: Layout, Befehle, Timer
//   9. Thread + Nachrichtenschleife
//
// Alles, was die Welt betrifft, geht ueber LabBridge::Post (Spielthread).
// Dateizugriffe (.cub lesen/schreiben) passieren direkt hier -- das ist
// unabhaengig vom Spiel und blockiert nur dieses Fenster.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <climits>
#include "LabWindow.h"
#include "ModDb.h"
#include "LabBridge.h"
#include "LabStructure.h"
#include "LabModelNames.h"

#include "MountainVillage.h"
#include "NativeHouseProbe.h"
#include "WanderingMerchant.h"
#include "AirshipTaxi.h"
#include "Wildlife.h"
#include "GemForge.h"
#include "PerfTweaks.h"
#include "MobLab.h"
#include "CrashLogger.h"
#include "BiomeGen.h"
#include "BiomeMod.h"
#include "Recipes.h"
#include "Items.h"
#include "Skills.h"
#include "Vendors.h"
#include "Lan.h"
#include "ZoneSplit.h"
#include "BuildingFx.h"
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include "Effects.h"
#include "City.h"
#include "ModSwitches.h"
#include "Dungeons.h"
#include "Tracking.h"
#include "TierWerkstatt.h"
#include <shellapi.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cmath>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

// In main.cpp: schaltet die [F9]-Tracking-Anzeige um.
void AdventureMain_ToggleTracking();

namespace LabWindow {

	// =====================================================================
	// 1. Zustand
	// =====================================================================
	namespace {
		HWND g_hwnd = nullptr;
		HWND g_h3d = nullptr, g_hLayer = nullptr, g_hPal = nullptr;
		HWND g_hLog = nullptr, g_hLib = nullptr, g_hFilter = nullptr, g_hOnlyBig = nullptr;
		HWND g_hStatus = nullptr, g_hInfo = nullptr, g_hHover = nullptr, g_hLayerLbl = nullptr;
		HWND g_hName = nullptr, g_hSX = nullptr, g_hSY = nullptr, g_hSZ = nullptr;
		HWND g_hPX = nullptr, g_hPY = nullptr, g_hPZ = nullptr, g_hRot = nullptr, g_hTyp = nullptr;
		HWND g_hFund = nullptr, g_hMarkers = nullptr, g_hRemember = nullptr, g_hCut = nullptr;
		HWND g_hPlaceList = nullptr;
		HWND g_hToolBtn[4] = { nullptr, nullptr, nullptr, nullptr };
		HWND g_hFnList = nullptr, g_hRGB[3] = { nullptr, nullptr, nullptr };

		// Speichern
		bool g_dirty = false;                         // ungespeicherte Aenderungen
		std::string g_curPath;                        // Datei der offenen Struktur ("" = noch keine eigene)

		// Farbe
		bool g_colIsMarker = false;                   // aktuelle Farbe ist eine Markerfarbe
		int  g_custom[12][3];                         // eigene Farbplaetze (-1 = leer)
		bool g_customLoaded = false;

		// Rechte Spalte: Reiter
		int g_page = 0;
		std::vector<HWND> g_pageCtl[4];
		BuildingFx::Def g_curFx;                      // Gebaeude-Funktionen der offenen Struktur (<name>.funk)
		HFONT g_font = nullptr, g_fontBold = nullptr;
		HANDLE g_thread = nullptr;
		volatile bool g_visible = false;

		// Editor
		Lab::Structure g_cur;
		std::vector<Lab::Structure> g_undo;          // Editor-Rueckgaengig (max. 40)
		int  g_layer = 0;
		int  g_tool = 0;                             // 0 Stift, 1 Radierer, 2 Pipette, 3 Fuellen
		uint8_t g_col[3] = { 150, 150, 162 };
		int  g_viewRot = 0;
		float g_zoom = 1.0f;
		bool g_painting = false;
		std::vector<std::pair<uint32_t, int>> g_structColors;   // haeufigste Farben der Struktur

		// Bibliothek
		enum LibKind { kMod = 0, kOwn, kExport, kScan, kNative };
		struct LibItem { LibKind kind; int index; std::string file; std::string label; int extra; int sx, sy, sz; };
		std::vector<LibItem> g_lib;
		std::vector<int> g_libVisible;                // Listbox-Zeile -> g_lib-Index
		std::vector<Lab::Structure> g_scans;          // aus HausBloecke.txt
		std::vector<LabBridge::LibEntry> g_native;
		bool g_nativeRequested = false;

		size_t g_lastPlaceCount = (size_t)-1;
		std::string g_hoverText;

		// IDs
		enum {
			ID_GAME_FIRST = 1000,                     // 1000..1099: Spielfunktionen
			ID_LIB = 2000, ID_FILTER, ID_ONLYBIG, ID_LIB_OPEN, ID_LIB_RELOAD, ID_LIB_EXPORT, ID_FILE_OPEN,
			ID_NEW, ID_RESIZE, ID_CROP, ID_ROT90, ID_MIRROR, ID_SAVE, ID_EUNDO,
			ID_TOOL0, ID_TOOL1, ID_TOOL2, ID_TOOL3, ID_COLOR,
			ID_LAYER_UP, ID_LAYER_DN, ID_LAYER_CLEAR, ID_LAYER_FILL, ID_LAYER_COPYUP,
			ID_VROT_L, ID_VROT_R, ID_ZOOM_IN, ID_ZOOM_OUT, ID_FIT, ID_CUT,
			ID_TGT_AHEAD, ID_TGT_HERE, ID_PLACE, ID_WUNDO, ID_SCAN_BOX, ID_SCAN_ME, ID_TYP_SUGGEST,
			ID_PLACELIST, ID_PLACE_DEL, ID_PLACE_OPEN, ID_PERF_OPEN,
			ID_FN_LIST, ID_FN_RUN, ID_SAVEAS, ID_RGB_SET, ID_TAB0, ID_TAB1, ID_TAB2, ID_MOB_OPEN, ID_LOGGER, ID_SCALE, ID_BIOME_OPEN, ID_ZMOD_OPEN, ID_RECIPE_OPEN,	ID_MAINTAB0, ID_MAINTAB_LAST = ID_MAINTAB0 + 15,
				ID_TAB3, ID_FX_LIST, ID_FX_ADD, ID_FX_GLOW, ID_FX_DEL, ID_FX_SAVE,
				ID_TIMER = 1
		};
		const UINT WM_APP_TOGGLE = WM_APP + 1;
	}

	// =====================================================================
	// 2. Helfer
	// =====================================================================
	static HWND Ctl(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
		HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
			g_hwnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
		SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
		return c;
	}
	static HWND Btn(const char* t, int x, int y, int w, int h, int id) { return Ctl("BUTTON", t, BS_PUSHBUTTON, x, y, w, h, id); }
	static HWND Chk(const char* t, int x, int y, int w, int h, int id, bool on) {
		HWND c = Ctl("BUTTON", t, BS_AUTOCHECKBOX, x, y, w, h, id);
		SendMessageA(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
		return c;
	}
	static HWND Lbl(const char* t, int x, int y, int w, int h, bool bold = false) {
		HWND c = Ctl("STATIC", t, SS_LEFT, x, y, w, h, -1);
		if (bold) SendMessageA(c, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
		return c;
	}
	static HWND Edt(const char* t, int x, int y, int w, int h, int id = -1) {
		return Ctl("EDIT", t, ES_AUTOHSCROLL, x, y, w, h, id, WS_EX_CLIENTEDGE);
	}
	static HWND Grp(const char* t, int x, int y, int w, int h) { return Ctl("BUTTON", t, BS_GROUPBOX, x, y, w, h, -1); }

	static std::string GetText(HWND h) {
		char buf[512]; buf[0] = 0; GetWindowTextA(h, buf, sizeof(buf)); return buf;
	}
	static int GetInt(HWND h, int def) {
		std::string s = GetText(h);
		if (s.empty()) return def;
		// Ganzzahl: dezimal (auch mit fuehrender 0, auch negativ), "0x.." hexadezimal; Leerzeichen/Punkte als Tausender ignoriert
		std::string t; for (char ch : s) if (ch != ' ' && ch != '.' && ch != '\'') t += ch;
		const char* q = t.c_str(); bool neg = *q == '-'; if (*q == '-' || *q == '+') q++;
		int base = (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) ? 16 : 10;
		char* end = nullptr; long long v = strtoll(q, &end, base);
		if (!end || end == q) return def;
		if (neg) v = -v;
		if (v > INT_MAX) v = INT_MAX; if (v < INT_MIN) v = INT_MIN;
		return (int)v;
	}
	static void SetInt(HWND h, int v) { char b[32]; sprintf_s(b, "%d", v); SetWindowTextA(h, b); }
	static void SetHex(HWND h, int v) { char b[32]; sprintf_s(b, "0x%X", v); SetWindowTextA(h, b); }
	static bool Checked(HWND h) { return SendMessageA(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }

	static void AppendLog(const std::string& raw) {
		if (raw.empty() || !g_hLog) return;
		std::string s; s.reserve(raw.size() + 32);
		for (char c : raw) { if (c == '\n') s += "\r\n"; else if (c != '\r') s += c; }
		int len = GetWindowTextLengthA(g_hLog);
		if (len > 400000) {                                  // alte Haelfte abschneiden
			SendMessageA(g_hLog, EM_SETSEL, 0, 200000);
			SendMessageA(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)"");
			len = GetWindowTextLengthA(g_hLog);
		}
		SendMessageA(g_hLog, EM_SETSEL, len, len);
		SendMessageA(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
	}
	static void Say(const std::string& s) { AppendLog("[LAB] " + s + "\n"); }

	static void Redraw3D()    { if (g_h3d) InvalidateRect(g_h3d, NULL, FALSE); }
	static void RedrawLayer() { if (g_hLayer) InvalidateRect(g_hLayer, NULL, FALSE); }
	static void RedrawPal()   { if (g_hPal) InvalidateRect(g_hPal, NULL, FALSE); }

	// Vorwaertsdeklarationen (Definitionen weiter unten)
	static const char* MarkerName(uint8_t r, uint8_t g, uint8_t b);
	static char MarkerLetter(uint8_t r, uint8_t g, uint8_t b);
	static void SetTool(int t);
	static void SetColor(uint8_t r, uint8_t g, uint8_t b, bool allowMarker);
	static bool SaveCurrent();
	static bool ConfirmDiscard();
	static void UpdateTitle();

	// =====================================================================
	// 3. Spielfunktionen -- ALLE bisherigen LAB-Tasten
	// =====================================================================
	// Quelle: das [L]-Overlay in main.cpp (unveraendert dort weiter nutzbar).
	// Jede Zeile wird ein Knopf; der Klick legt einen Auftrag fuer den
	// Spielthread an. "Kopf" = Zwischenueberschrift ohne Funktion.
	struct GameFn { const char* label; void(*fn)(cube::GameController*); bool header; };
	static void F_Merchant(cube::GameController* gc)    { AdventureMod::ForceSpawn(gc); }
	static void F_Dwarf(cube::GameController* gc)       { MountainVillage::SpawnTestDwarfAtPlayer(gc); }
	static void F_InvDump(cube::GameController* gc)     { AirshipTaxi::DumpInventories(gc); }
	static void F_Inspect(cube::GameController* gc)     { std::cout << "[Auslesen] Kreaturen im Umkreis 12:\n" << MobLab::InspectNearby(gc, 12) << std::endl; }
	static void F_Creatures(cube::GameController* gc)   { cube::util::DumpNearbyCreatures(gc); }
	static void F_Probe(cube::GameController* gc)       { NativeHouseProbe::Probe(gc); }
	static void F_DryRun(cube::GameController* gc)      { NativeHouseProbe::DryRun(gc); }
	static void F_Browse(cube::GameController* gc)      { NativeHouseProbe::BrowseModels(gc); }
	static void F_BrowseReset(cube::GameController*)    { NativeHouseProbe::ResetBrowse(0x839); }
	static void F_RegDump(cube::GameController* gc)     { NativeHouseProbe::DumpRegistry(gc); }
	static void F_Cycle(cube::GameController* gc)       { NativeHouseProbe::CycleArgs(gc); }
	static void F_ObjScan(cube::GameController* gc)     { NativeHouseProbe::ScanArea(gc, 40); }
	static void F_ObjScanReset(cube::GameController*)   { NativeHouseProbe::ResetScan(); }
	static void F_BlockScan(cube::GameController* gc)   { MountainVillage::ScanBlocksAtPlayer(gc, 22, -4, 28); }
	static void F_BlockScanReset(cube::GameController*) { MountainVillage::ResetBlockScan(); }
	static void F_ObjTester(cube::GameController* gc)   { MountainVillage::ObjectTester(gc); }
	static void F_Persist(cube::GameController* gc)     { MountainVillage::PersistMarkerAtPlayer(gc); }
	static void F_Village(cube::GameController* gc)     { MountainVillage::BuildAtPlayer(gc); }
	static void F_Prop(cube::GameController* gc)        { MountainVillage::PropTestAtPlayer(gc); }
	static void F_Bind(cube::GameController* gc)        { GemForge::BindEquippedWeaponToBench(gc); }
	static void F_Extras(cube::GameController* gc)      { GemForge::ClearExtras(gc); }
	static void F_Bench(cube::GameController* gc)       { GemForge::DumpBenchState(gc); }
	static void F_Inv(cube::GameController* gc)         { GemForge::DumpInventory(gc); }
	static void F_Gems(cube::GameController*)           { GemForge::ToggleGemSupply(); }
static void F_Tracking(cube::GameController*)       { AdventureMain_ToggleTracking(); }

	static const GameFn kGameFns[] = {
		{ "NPC / Kreaturen", nullptr, true },
		{ "Haendler spawnen  [S+1]",          F_Merchant, false },
		{ "Test-Zwerg spawnen  [S+2]",        F_Dwarf, false },
		{ "Kreaturen-Dump",                   F_Creatures, false },
		{ "Tiere in der Naehe genau auslesen", F_Inspect, false },
		{ "Welt / Bauten", nullptr, true },
		{ "Bergdorf hier bauen  [S+0]",       F_Village, false },
		{ "Persistenz-Marker  [S+9]",         F_Persist, false },
		{ "Deko-Prop-Test  [S+P]",            F_Prop, false },
		{ "Objekt-Tester  [S+T]",             F_ObjTester, false },
		{ "Modelle / Scans", nullptr, true },
		{ "Hausprobe  [S+7]",                 F_Probe, false },
		{ "Vorpruefung (DryRun)",             F_DryRun, false },
		{ "Modell-Browser  [S+8]",            F_Browse, false },
		{ "Browser auf 0x839  [S+O]",         F_BrowseReset, false },
		{ "Registry-Dump  [S+L]",             F_RegDump, false },
		{ "Args durchschalten  [S+Y]",        F_Cycle, false },
		{ "Objektscan -> HausScan  [S+J]",    F_ObjScan, false },
		{ "HausScan leeren  [S+K]",           F_ObjScanReset, false },
		{ "Blockscan -> HausBloecke  [S+H]",  F_BlockScan, false },
		{ "HausBloecke leeren  [S+G]",        F_BlockScanReset, false },
		{ "Schmiede / Handel", nullptr, true },
		{ "Waffe an Bench binden  [S+B]",     F_Bind, false },
		{ "Extra-Cubes verwerfen  [S+X]",     F_Extras, false },
		{ "Bench-Dump  [S+I]",                F_Bench, false },
		{ "Inventar-Scan  [S+U]",             F_Inv, false },
		{ "Edelsteine->Cubes an/aus  [S+4]",  F_Gems, false },
{ "Inventar-Dump Taxi  [S+3]",        F_InvDump, false },
		{ "Anzeige", nullptr, true },
		{ "Tracking-Anzeige  [F9]",           F_Tracking, false },
	};
	static const int kGameFnCount = (int)(sizeof(kGameFns) / sizeof(kGameFns[0]));

	static void RunGameFn(int i) {
		if (i < 0 || i >= kGameFnCount || !kGameFns[i].fn) return;
		void(*fn)(cube::GameController*) = kGameFns[i].fn;
		std::string label = kGameFns[i].label;
		Say("-> " + label);
		LabBridge::Post(label, [fn](cube::GameController* gc) { fn(gc); });
	}

	// =====================================================================
	// 4. Bibliothek
	// =====================================================================
	static std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

	static void RebuildLibList() {
		if (!g_hLib) return;
		std::string filt = Lower(GetText(g_hFilter));
		bool onlyBig = Checked(g_hOnlyBig);
		SendMessageA(g_hLib, WM_SETREDRAW, FALSE, 0);
		SendMessageA(g_hLib, LB_RESETCONTENT, 0, 0);
		g_libVisible.clear();
		for (size_t i = 0; i < g_lib.size(); i++) {
			const LibItem& it = g_lib[i];
			if (onlyBig && it.kind == kNative && it.sx < 10 && it.sy < 10) continue;
			if (!filt.empty() && Lower(it.label).find(filt) == std::string::npos) continue;
			SendMessageA(g_hLib, LB_ADDSTRING, 0, (LPARAM)it.label.c_str());
			g_libVisible.push_back((int)i);
		}
		SendMessageA(g_hLib, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(g_hLib, NULL, TRUE);
	}

	static std::string DimStr(int x, int y, int z) { char b[48]; sprintf_s(b, "%dx%dx%d", x, y, z); return b; }

	// Alle Quellen neu einlesen (Dateien direkt, native Liste vom Spielthread).
	static void ReloadLibrary() {
		g_lib.clear();
		// Mod-Bauwerke: Bauplaene aus MountainVillage, erfasst ohne die Welt zu aendern.
		const char* mods[] = { "Turm (Bergdorf)", "Haus (Blocknachbau)", "Schmiede (Blocknachbau)" };
		for (int i = 0; i < 3; i++) g_lib.push_back({ kMod, -1, "", std::string("[mod]    ") + mods[i], i, 0, 0, 0 });

		// Eigene + Export (.cub)
		std::vector<std::string> own = Lab::ListCubFiles(Lab::StructuresDir());
		for (const std::string& f : own) g_lib.push_back({ kOwn, -1, Lab::StructuresDir() + "\\" + f, "[eigen]  " + f, 0, 0, 0, 0 });
		std::vector<std::string> ex = Lab::ListCubFiles(Lab::ExportDir());
		for (const std::string& f : ex) g_lib.push_back({ kExport, -1, Lab::ExportDir() + "\\" + f, "[export] " + f, 0, 0, 0, 0 });
		// Automatische Sicherungen (z.B. nach einem Absturz) -- oeffnen wie Export:
		// Speichern geht dann regulaer nach Strukturen\<Name>.cub.
		std::string cityDir = Lab::StructuresDir() + "\\Stadt";
		for (const std::string& f : Lab::ListCubFiles(cityDir)) g_lib.push_back({ kExport, -1, cityDir + "\\" + f, "[stadt]  " + f, 0, 0, 0, 0 });
		std::string autoDir = Lab::StructuresDir() + "\\_autosave";
		for (const std::string& f : Lab::ListCubFiles(autoDir)) g_lib.push_back({ kExport, -1, autoDir + "\\" + f, "[auto]   " + f, 0, 0, 0, 0 });

		// Scans
		g_scans.clear();
		std::string err;
		Lab::ImportBlockScans("HausBloecke.txt", g_scans, err);
		for (size_t i = 0; i < g_scans.size(); i++)
			g_lib.push_back({ kScan, -1, "", "[scan]   " + g_scans[i].name + "  " + DimStr(g_scans[i].sx, g_scans[i].sy, g_scans[i].sz),
				(int)i, g_scans[i].sx, g_scans[i].sy, g_scans[i].sz });

		// Nativ (aus der letzten Registry-Abfrage)
		for (const LabBridge::LibEntry& e : g_native) {
			char b[32]; sprintf_s(b, "[0x%03X]  ", e.index);
			std::string lbl = std::string(b) + e.file + "  " + DimStr(e.sx, e.sy, e.sz);
			if (e.suggestedTyp) { char t[24]; sprintf_s(t, "  typ 0x%X", e.suggestedTyp); lbl += t; }
			g_lib.push_back({ kNative, e.index, e.file, lbl, e.suggestedTyp, e.sx, e.sy, e.sz });
		}
		RebuildLibList();
		std::ostringstream o;
		o << "Bibliothek: 3 Mod-Bauwerke, " << own.size() << " eigene, " << ex.size() << " Export, "
		  << g_scans.size() << " Scans, " << g_native.size() << " native Modelle.";
		Say(o.str());
	}

	static void RequestNative() {
		g_nativeRequested = true;
		LabBridge::Post("Registry lesen", [](cube::GameController* gc) { LabBridge::RequestNativeLibrary(gc); });
	}

	static void SetCurrent(const Lab::Structure& s, bool pushUndo);
	static void ShowMainTab(int k);

	static void OpenLibItem(int row) {
		if (row < 0 || row >= (int)g_libVisible.size()) return;
		if (!ConfirmDiscard()) return;
		const LibItem it = g_lib[g_libVisible[row]];
		std::string err;
		switch (it.kind) {
			case kOwn: case kExport: {
				Lab::Structure s;
				if (!Lab::LoadCub(it.file, s, err)) { Say("Laden fehlgeschlagen: " + err); return; }
				Lab::LoadMeta(it.file, s);
				if (s.origin.empty() || s.origin == "datei") s.origin = (it.kind == kOwn ? "eigen: " : "export: ") + it.file;
				SetCurrent(s, true);
				if (it.kind == kOwn) { g_curPath = it.file; BuildingFx::Load(g_curPath, g_curFx); }   // Strg+S ueberschreibt diese Datei
				break;
			}
			case kScan:
				if (it.extra >= 0 && it.extra < (int)g_scans.size()) SetCurrent(g_scans[it.extra], true);
				break;
			case kNative: {
				int idx = it.index;
				Say("Lade natives Modell " + it.file + " ...");
				LabBridge::Post("Nativ importieren", [idx](cube::GameController* gc) {
					Lab::Structure s; std::string e;
					if (LabBridge::ImportNative(gc, idx, s, e)) LabBridge::PostStructure(s);
					else LabBridge::Log("Import fehlgeschlagen: " + e);
				});
				break;
			}
			case kMod: {
				int what = it.extra;
				Say("Erfasse Bauplan ...");
				LabBridge::Post("Bauplan erfassen", [what](cube::GameController* gc) {
					std::vector<MountainVillage::LabBlock> blocks; std::string name;
					if (!MountainVillage::LabCapturePlan(gc, what, blocks, name)) { LabBridge::Log("Bauplan: " + name); return; }
					int x0 = blocks[0].x, y0 = blocks[0].y, z0 = blocks[0].z, x1 = x0, y1 = y0, z1 = z0;
					for (const auto& b : blocks) {
						x0 = (std::min)(x0, b.x); y0 = (std::min)(y0, b.y); z0 = (std::min)(z0, b.z);
						x1 = (std::max)(x1, b.x); y1 = (std::max)(y1, b.y); z1 = (std::max)(z1, b.z);
					}
					Lab::Structure s; s.Create(x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1);
					for (const auto& b : blocks) s.Set(b.x - x0, b.y - y0, b.z - z0, b.r, b.g, b.b);
					s.name = name; s.origin = "mod: " + name; s.fundament = 1;
					LabBridge::PostStructure(s);
				});
				break;
			}
		}
	}

	// =====================================================================
	// 5. Editor
	// =====================================================================
	static void UpdateStructColors() {
		std::map<uint32_t, int> h;
		for (size_t i = 0; i + 2 < g_cur.rgb.size(); i += 3) {
			uint32_t c = ((uint32_t)g_cur.rgb[i] << 16) | ((uint32_t)g_cur.rgb[i + 1] << 8) | g_cur.rgb[i + 2];
			if (c) h[c]++;
		}
		g_structColors.assign(h.begin(), h.end());
		std::sort(g_structColors.begin(), g_structColors.end(),
			[](const std::pair<uint32_t, int>& a, const std::pair<uint32_t, int>& b) { return a.second > b.second; });
		if (g_structColors.size() > 24) g_structColors.resize(24);
	}

	static void UpdateInfo() {
		if (!g_hInfo) return;
		std::ostringstream o;
		if (g_cur.Valid())
			o << g_cur.sx << " x " << g_cur.sy << " x " << g_cur.sz << "   " << g_cur.CountSolid() << " Voxel   Herkunft: "
			  << (g_cur.origin.empty() ? "-" : g_cur.origin);
		else o << "keine Struktur";
		SetWindowTextA(g_hInfo, o.str().c_str());
		char b[64]; sprintf_s(b, "Ebene z = %d / %d", g_layer, g_cur.Valid() ? g_cur.sz - 1 : 0);
		SetWindowTextA(g_hLayerLbl, b);
	}

	static void AfterChange(bool colors = true) {
		if (g_layer >= g_cur.sz) g_layer = (std::max)(0, g_cur.sz - 1);
		if (colors) UpdateStructColors();
		UpdateInfo(); Redraw3D(); RedrawLayer(); RedrawPal();
	}

	static void PushUndo() {
		if (!g_cur.Valid()) return;
		g_undo.push_back(g_cur);
		if (g_undo.size() > 40) g_undo.erase(g_undo.begin());
		// Jede Bearbeitung ruft vorher PushUndo -> genau hier wird die Struktur "geaendert".
		if (!g_dirty) { g_dirty = true; UpdateTitle(); }
	}

	static void SetCurrent(const Lab::Structure& s, bool pushUndo) {
		if (pushUndo && g_cur.Valid()) { g_undo.push_back(g_cur); if (g_undo.size() > 40) g_undo.erase(g_undo.begin()); }
		g_cur = s;
		g_layer = 0;
		g_curPath.clear();                            // Aufrufer setzt den Pfad, wenn es eine eigene Datei ist
		g_curFx.e.clear();
		g_dirty = false;
		SetWindowTextA(g_hName, g_cur.name.c_str());
		SetInt(g_hSX, g_cur.sx); SetInt(g_hSY, g_cur.sy); SetInt(g_hSZ, g_cur.sz);
		SetHex(g_hTyp, g_cur.typ);
		SendMessageA(g_hFund, BM_SETCHECK, g_cur.fundament ? BST_CHECKED : BST_UNCHECKED, 0);
		g_zoom = 1.0f;
		AfterChange();
		UpdateTitle();
		Say("Geoeffnet: " + g_cur.name + "  (" + DimStr(g_cur.sx, g_cur.sy, g_cur.sz) + ")");
	}

	static void EditorUndo() {
		if (g_undo.empty()) { Say("Editor: nichts zum Zuruecknehmen."); return; }
		g_cur = g_undo.back(); g_undo.pop_back();
		g_dirty = true; UpdateTitle();
		AfterChange();
	}

	// =====================================================================
	// 5b. Speichern
	// =====================================================================
	static std::string BaseName(const std::string& p) {
		size_t s0 = p.find_last_of("\\/"); return s0 == std::string::npos ? p : p.substr(s0 + 1);
	}
	static bool FileExists(const std::string& p) {
		DWORD a = GetFileAttributesA(p.c_str());
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	static void UpdateTitle() {
		if (!g_hwnd) return;
		std::string t = "Cube World LAB  --  ";
		t += g_cur.Valid() ? (g_cur.name.empty() ? std::string("unbenannt") : g_cur.name) : std::string("keine Struktur");
		if (g_dirty) t += "  *  (ungespeichert, Strg+S)";
		t += "      [F10] im Spiel blendet ein/aus";
		SetWindowTextA(g_hwnd, t.c_str());
	}

	static void SelectInLibrary(const std::string& fileName) {
		for (size_t r = 0; r < g_libVisible.size(); r++)
			if (g_lib[g_libVisible[r]].kind == kOwn && BaseName(g_lib[g_libVisible[r]].file) == fileName) {
				SendMessageA(g_hLib, LB_SETCURSEL, (WPARAM)r, 0); return;
			}
	}

	// Schreibt .cub (natives Format) + .txt (Beiwerte) nach 'path'.
	static bool WriteTo(const std::string& path) {
		g_cur.typ = GetInt(g_hTyp, 0);
		g_cur.fundament = Checked(g_hFund) ? 1 : 0;
		if (g_cur.origin.empty()) g_cur.origin = "lab";
		std::string err;
		if (!Lab::SaveCub(path, g_cur, err)) { Say("Speichern fehlgeschlagen: " + err); MessageBoxA(g_hwnd, err.c_str(), "Speichern fehlgeschlagen", MB_ICONERROR); return false; }
		Lab::SaveMeta(path, g_cur);
		g_curPath = path; g_dirty = false;
		{ std::string fe; if (!BuildingFx::Save(path, g_curFx, fe)) Say("Funktionen: " + fe); }
		UpdateTitle();
		Say("Gespeichert: " + path + "   (" + DimStr(g_cur.sx, g_cur.sy, g_cur.sz) + ", " + std::to_string(g_cur.CountSolid()) + " Voxel)");
		ReloadLibrary();
		SelectInLibrary(BaseName(path));
		return true;
	}

	// Strg+S. Ziel: die Datei, aus der die Struktur stammt -- sonst Strukturen\<Name>.cub.
	// Wurde der Name geaendert, entsteht eine NEUE Datei (die alte bleibt).
	static bool SaveCurrent() {
		if (!g_cur.Valid()) { Say("Keine Struktur zum Speichern."); return false; }
		std::string name = Lab::SafeFileName(GetText(g_hName));
		if (name.empty() || name == "struktur") {
			MessageBoxA(g_hwnd, "Bitte oben einen Namen fuer die Struktur eintragen.", "Speichern", MB_ICONINFORMATION);
			SetFocus(g_hName); return false;
		}
		SetWindowTextA(g_hName, name.c_str());
		g_cur.name = name;
		Lab::EnsureDir(Lab::StructuresDir());
		std::string path = g_curPath;
		if (path.empty() || BaseName(path) != name + ".cub") path = Lab::StructuresDir() + "\\" + name + ".cub";
		if (path != g_curPath && FileExists(path)) {
			std::string q = "'" + name + ".cub' gibt es schon in Strukturen\\.\n\nUeberschreiben?";
			if (MessageBoxA(g_hwnd, q.c_str(), "Speichern", MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
		}
		return WriteTo(path);
	}

	static bool SaveCurrentAs() {
		if (!g_cur.Valid()) { Say("Keine Struktur zum Speichern."); return false; }
		Lab::EnsureDir(Lab::StructuresDir());
		char dir[MAX_PATH] = ""; GetFullPathNameA(Lab::StructuresDir().c_str(), MAX_PATH, dir, NULL);
		char file[MAX_PATH] = "";
		std::string def = Lab::SafeFileName(GetText(g_hName)) + ".cub";
		strncpy_s(file, def.c_str(), _TRUNCATE);
		OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hwnd;
		ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0";
		ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = dir;
		ofn.lpstrDefExt = "cub"; ofn.lpstrTitle = "Struktur speichern unter";
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetSaveFileNameA(&ofn)) return false;
		std::string path = file, base = BaseName(path);
		if (base.size() > 4) base.resize(base.size() - 4);
		g_cur.name = base; SetWindowTextA(g_hName, base.c_str());
		bool ok = WriteTo(path);
		char full[MAX_PATH] = ""; GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL);
		if (ok && _strnicmp(full, dir, strlen(dir)) != 0)
			Say("Hinweis: ausserhalb von Strukturen\\ gespeichert -- erscheint nicht in der Bibliothek.");
		return ok;
	}

	// Vor dem Ersetzen der offenen Struktur: ungespeicherte Aenderungen sichern?
	static bool ConfirmDiscard() {
		if (!g_dirty || !g_cur.Valid()) return true;
		std::string q = "Die Struktur '" + (g_cur.name.empty() ? std::string("unbenannt") : g_cur.name) +
			"' hat ungespeicherte Aenderungen.\n\nJetzt speichern?";
		int r = MessageBoxA(g_hwnd, q.c_str(), "Ungespeicherte Aenderungen", MB_YESNOCANCEL | MB_ICONWARNING);
		if (r == IDCANCEL) return false;
		if (r == IDYES) return SaveCurrent();
		return true;
	}

	static void OpenFileDialog() {
		if (!ConfirmDiscard()) return;
		char file[MAX_PATH] = "";
		OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hwnd;
		ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0Blockscans (HausBloecke.txt)\0*.txt\0Alle Dateien\0*.*\0";
		ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;     // Arbeitsordner von Cube.exe NICHT verstellen
		if (!GetOpenFileNameA(&ofn)) return;
		std::string p = file, err;
		if (p.size() > 4 && Lower(p.substr(p.size() - 4)) == ".txt") {
			std::vector<Lab::Structure> v;
			int n = Lab::ImportBlockScans(p, v, err);
			if (n <= 0) { Say("Keine Scans in " + p + " " + err); return; }
			SetCurrent(v.back(), true);
			Say(std::to_string(n) + " Scans gelesen, der letzte ist geoeffnet.");
			return;
		}
		Lab::Structure s;
		if (!Lab::LoadCub(p, s, err)) { Say("Laden fehlgeschlagen: " + err); return; }
		Lab::LoadMeta(p, s);
		SetCurrent(s, true);
		g_curPath = p;                                 // Strg+S schreibt dorthin zurueck
		BuildingFx::Load(g_curPath, g_curFx);
	}

	static void ApplyToolAt(int x, int y) {
		if (!g_cur.In(x, y, g_layer)) return;
		switch (g_tool) {
			case 0: g_cur.Set(x, y, g_layer, g_col[0], g_col[1], g_col[2]); break;
			case 1: g_cur.Clear(x, y, g_layer); break;
			case 2: {
				uint8_t r, g, b;
				if (g_cur.Get(x, y, g_layer, r, g, b)) SetColor(r, g, b, true);   // Marker aufnehmen ist erlaubt
				break;
			}
			case 3: {
				// Flaechenfuellung in der Ebene: alle 4-verbundenen Zellen gleicher Farbe.
				uint8_t r0 = 0, g0 = 0, b0 = 0; g_cur.Get(x, y, g_layer, r0, g0, b0);
				if (r0 == g_col[0] && g0 == g_col[1] && b0 == g_col[2]) return;
				std::vector<std::pair<int, int>> st; st.push_back({ x, y });
				int guard = 0;
				while (!st.empty() && guard++ < 70000) {
					std::pair<int, int> p = st.back(); st.pop_back();
					if (!g_cur.In(p.first, p.second, g_layer)) continue;
					uint8_t r = 0, g = 0, b = 0; g_cur.Get(p.first, p.second, g_layer, r, g, b);
					if (r != r0 || g != g0 || b != b0) continue;
					g_cur.Set(p.first, p.second, g_layer, g_col[0], g_col[1], g_col[2]);
					st.push_back({ p.first + 1, p.second }); st.push_back({ p.first - 1, p.second });
					st.push_back({ p.first, p.second + 1 }); st.push_back({ p.first, p.second - 1 });
				}
				break;
			}
		}
	}

	static void SetTool(int t) {
		g_tool = t;
		for (int i = 0; i < 4; i++) if (g_hToolBtn[i]) SendMessageA(g_hToolBtn[i], BM_SETCHECK, i == t ? BST_CHECKED : BST_UNCHECKED, 0);
	}

	static void ChangeLayer(int d) {
		if (!g_cur.Valid()) return;
		g_layer = (std::max)(0, (std::min)(g_cur.sz - 1, g_layer + d));
		UpdateInfo(); RedrawLayer(); Redraw3D();
	}

	// =====================================================================
	// 6. 3D-Ansicht
	// =====================================================================
	// Isometrie mit Blick aus (+x,+y,+z): sichtbar sind die Oberseite und die
	// +x/+y-Seiten. Bildschirm: X = (rx-ry)*t, Y = (rx+ry)*t/2 - z*t.
	// Drehung in 90-Grad-Schritten um die Hochachse (viewRot), Zeichnen nach
	// Tiefe (rx+ry+z aufsteigend) -- klassischer Maler-Algorithmus.
	static void ViewDims(int& W, int& D) { W = (g_viewRot & 1) ? g_cur.sy : g_cur.sx; D = (g_viewRot & 1) ? g_cur.sx : g_cur.sy; }
	static void ViewToModel(int rx, int ry, int& x, int& y) {
		switch (g_viewRot & 3) {
			case 0: x = rx;                 y = ry;                 break;
			case 1: x = ry;                 y = g_cur.sy - 1 - rx;  break;
			case 2: x = g_cur.sx - 1 - rx;  y = g_cur.sy - 1 - ry;  break;
			default: x = g_cur.sx - 1 - ry; y = rx;                 break;
		}
	}
	static bool SolidView(int rx, int ry, int z, int zMax) {
		if (z > zMax) return false;
		int x, y; ViewToModel(rx, ry, x, y);
		return g_cur.Solid(x, y, z);
	}
	static COLORREF Shade(uint8_t r, uint8_t g, uint8_t b, float f) {
		return RGB((int)(r * f), (int)(g * f), (int)(b * f));
	}

	static void Render3D(HDC dc, int cw, int ch) {
		HBRUSH bg = CreateSolidBrush(RGB(34, 36, 42));
		RECT rc = { 0, 0, cw, ch }; FillRect(dc, &rc, bg); DeleteObject(bg);
		SetBkMode(dc, TRANSPARENT);
		if (!g_cur.Valid()) {
			SetTextColor(dc, RGB(180, 180, 190));
			TextOutA(dc, 12, 12, "Keine Struktur geladen -- links in der Bibliothek waehlen.", 58);
			return;
		}
		int W, D; ViewDims(W, D);
		int zMax = Checked(g_hCut) ? g_layer : g_cur.sz - 1;
		float base = (std::min)((float)cw / (float)(W + D), (float)ch / ((W + D) * 0.5f + g_cur.sz));
		float t = base * 0.9f * g_zoom;
		if (t < 1.0f) t = 1.0f;
		float ox = cw * 0.5f - (W - D) * t * 0.5f;
		float oy = ch * 0.5f - ((W + D) * t * 0.5f - g_cur.sz * t) * 0.5f;
		auto P = [&](float rx, float ry, float z) -> POINT {
			POINT p; p.x = (LONG)(ox + (rx - ry) * t); p.y = (LONG)(oy + (rx + ry) * t * 0.5f - z * t); return p;
		};

		// Bodenraster (Grundflaeche) + Rahmen der aktuellen Ebene
		HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(60, 64, 74));
		HGDIOBJ oldPen = SelectObject(dc, gridPen);
		int step = (W + D > 80) ? 4 : 1;
		for (int i = 0; i <= W; i += step) { POINT a = P((float)i, 0, 0), b = P((float)i, (float)D, 0); MoveToEx(dc, a.x, a.y, NULL); LineTo(dc, b.x, b.y); }
		for (int j = 0; j <= D; j += step) { POINT a = P(0, (float)j, 0), b = P((float)W, (float)j, 0); MoveToEx(dc, a.x, a.y, NULL); LineTo(dc, b.x, b.y); }
		SelectObject(dc, oldPen); DeleteObject(gridPen);

		// Sichtbare Voxel sammeln: mindestens eine der drei sichtbaren Seiten frei.
		struct Vx { int rx, ry, z, key; uint8_t r, g, b; uint8_t faces; };
		std::vector<Vx> vis; vis.reserve(4096);
		for (int z = 0; z <= zMax; z++) for (int ry = 0; ry < D; ry++) for (int rx = 0; rx < W; rx++) {
			int x, y; ViewToModel(rx, ry, x, y);
			uint8_t r, g, b;
			if (!g_cur.Get(x, y, z, r, g, b)) continue;
			uint8_t f = 0;
			if (!SolidView(rx, ry, z + 1, zMax)) f |= 1;          // oben
			if (!SolidView(rx + 1, ry, z, zMax)) f |= 2;          // +rx
			if (!SolidView(rx, ry + 1, z, zMax)) f |= 4;          // +ry
			if (!f) continue;
			vis.push_back({ rx, ry, z, rx + ry + z, r, g, b, f });
		}
		std::sort(vis.begin(), vis.end(), [](const Vx& a, const Vx& b) { return a.key != b.key ? a.key < b.key : a.z < b.z; });

		HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
		HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
		bool outline = t >= 5.0f;
		for (const Vx& v : vis) {
			float X = (float)v.rx, Y = (float)v.ry, Z = (float)v.z;
			bool hl = Checked(g_hCut) && v.z == g_layer;           // aktuelle Ebene hervorheben
			if (v.faces & 1) {
				POINT q[4] = { P(X, Y, Z + 1), P(X + 1, Y, Z + 1), P(X + 1, Y + 1, Z + 1), P(X, Y + 1, Z + 1) };
				COLORREF c = Shade(v.r, v.g, v.b, hl ? 1.0f : 0.97f);
				SetDCBrushColor(dc, c); SetDCPenColor(dc, outline ? Shade(v.r, v.g, v.b, 0.72f) : c);
				Polygon(dc, q, 4);
			}
			if (v.faces & 2) {
				POINT q[4] = { P(X + 1, Y, Z), P(X + 1, Y + 1, Z), P(X + 1, Y + 1, Z + 1), P(X + 1, Y, Z + 1) };
				COLORREF c = Shade(v.r, v.g, v.b, 0.78f);
				SetDCBrushColor(dc, c); SetDCPenColor(dc, outline ? Shade(v.r, v.g, v.b, 0.58f) : c);
				Polygon(dc, q, 4);
			}
			if (v.faces & 4) {
				POINT q[4] = { P(X, Y + 1, Z), P(X + 1, Y + 1, Z), P(X + 1, Y + 1, Z + 1), P(X, Y + 1, Z + 1) };
				COLORREF c = Shade(v.r, v.g, v.b, 0.62f);
				SetDCBrushColor(dc, c); SetDCPenColor(dc, outline ? Shade(v.r, v.g, v.b, 0.45f) : c);
				Polygon(dc, q, 4);
			}
		}
		SelectObject(dc, ob); SelectObject(dc, op);

		// Rahmen der aktuellen Ebene
		HPEN lp = CreatePen(PS_DOT, 1, RGB(255, 210, 60));
		oldPen = SelectObject(dc, lp);
		float lz = (float)g_layer;
		POINT f[5] = { P(0, 0, lz), P((float)W, 0, lz), P((float)W, (float)D, lz), P(0, (float)D, lz), P(0, 0, lz) };
		Polyline(dc, f, 5);
		SelectObject(dc, oldPen); DeleteObject(lp);

		char info[160];
		sprintf_s(info, "%s   %dx%dx%d   Ansicht %d   %u sichtbare Voxel   [Mausrad = Zoom]",
			g_cur.name.c_str(), g_cur.sx, g_cur.sy, g_cur.sz, g_viewRot * 90, (unsigned)vis.size());
		SetTextColor(dc, RGB(200, 200, 210));
		TextOutA(dc, 8, 6, info, (int)strlen(info));
	}

	static void PaintBuffered(HWND h, void(*render)(HDC, int, int)) {
		PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
		RECT rc; GetClientRect(h, &rc);
		int w = rc.right, hh = rc.bottom;
		if (w > 0 && hh > 0) {
			HDC mem = CreateCompatibleDC(dc);
			HBITMAP bmp = CreateCompatibleBitmap(dc, w, hh);
			HGDIOBJ old = SelectObject(mem, bmp);
			HGDIOBJ of = SelectObject(mem, g_font);
			render(mem, w, hh);
			BitBlt(dc, 0, 0, w, hh, mem, 0, 0, SRCCOPY);
			SelectObject(mem, of); SelectObject(mem, old);
			DeleteObject(bmp); DeleteDC(mem);
		}
		EndPaint(h, &ps);
	}

	static LRESULT CALLBACK Proc3D(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_PAINT: PaintBuffered(h, Render3D); return 0;
			case WM_ERASEBKGND: return 1;
			case WM_MOUSEWHEEL: {
				short d = GET_WHEEL_DELTA_WPARAM(wp);
				g_zoom *= (d > 0) ? 1.15f : 1.0f / 1.15f;
				g_zoom = (std::max)(0.2f, (std::min)(g_zoom, 12.0f));
				Redraw3D(); return 0;
			}
			case WM_LBUTTONDOWN: SetFocus(h); return 0;   // Fokus fuer das Mausrad
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7. Ebenen-Editor + Palette
	// =====================================================================
	struct LayerGeo { int cell, ox, oy; };
	static LayerGeo LayerGeometry(int cw, int ch) {
		LayerGeo g; g.cell = 4; g.ox = 0; g.oy = 0;
		if (!g_cur.Valid()) return g;
		int c = (std::min)((cw - 30) / g_cur.sx, (ch - 30) / g_cur.sy);
		g.cell = (std::max)(2, (std::min)(c, 40));
		g.ox = (cw - g.cell * g_cur.sx) / 2;
		g.oy = (ch - g.cell * g_cur.sy) / 2 + 6;
		return g;
	}
	// Bildschirm -> Zelle. +Y zeigt nach OBEN (wie auf einer Karte).
	static bool CellAt(HWND h, int mx, int my, int& x, int& y) {
		RECT rc; GetClientRect(h, &rc);
		LayerGeo g = LayerGeometry(rc.right, rc.bottom);
		if (!g_cur.Valid()) return false;
		x = (mx - g.ox) / g.cell; int row = (my - g.oy) / g.cell;
		if (mx < g.ox || my < g.oy) return false;
		y = g_cur.sy - 1 - row;
		return g_cur.In(x, y, g_layer);
	}

	static void RenderLayer(HDC dc, int cw, int ch) {
		HBRUSH bg = CreateSolidBrush(RGB(28, 30, 36));
		RECT rc = { 0, 0, cw, ch }; FillRect(dc, &rc, bg); DeleteObject(bg);
		SetBkMode(dc, TRANSPARENT);
		if (!g_cur.Valid()) return;
		LayerGeo g = LayerGeometry(cw, ch);
		HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
		HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
		bool lines = g.cell >= 6;
		for (int y = 0; y < g_cur.sy; y++) for (int x = 0; x < g_cur.sx; x++) {
			int sx = g.ox + x * g.cell, sy = g.oy + (g_cur.sy - 1 - y) * g.cell;
			uint8_t r, gg, b;
			COLORREF c;
			if (g_cur.Get(x, y, g_layer, r, gg, b)) c = RGB(r, gg, b);
			else if (g_layer > 0 && g_cur.Get(x, y, g_layer - 1, r, gg, b))
				c = RGB(46 + r / 6, 48 + gg / 6, 56 + b / 6);         // Geist der Ebene darunter
			else c = RGB(44, 46, 54);
			SetDCBrushColor(dc, c);
			SetDCPenColor(dc, lines ? RGB(60, 63, 72) : c);
			Rectangle(dc, sx, sy, sx + g.cell + (lines ? 1 : 0), sy + g.cell + (lines ? 1 : 0));
			// Markerbloecke sichtbar kennzeichnen: weisser Rahmen + Anfangsbuchstabe.
			uint8_t mr, mg, mb;
			if (g_cur.Get(x, y, g_layer, mr, mg, mb)) {
				const char* mn = MarkerName(mr, mg, mb);
				if (mn && g.cell >= 6) {
					HGDIOBJ nb = SelectObject(dc, GetStockObject(NULL_BRUSH));
					SetDCPenColor(dc, RGB(255, 255, 255));
					Rectangle(dc, sx + 1, sy + 1, sx + g.cell, sy + g.cell);
					SelectObject(dc, nb);
					if (g.cell >= 12) {
						SetTextColor(dc, (mr + mg + mb > 380) ? RGB(0, 0, 0) : RGB(255, 255, 255));
						char L[2] = { MarkerLetter(mr, mg, mb), 0 };
						TextOutA(dc, sx + g.cell / 2 - 3, sy + g.cell / 2 - 7, L, 1);
					}
				}
			}
		}
		SelectObject(dc, ob); SelectObject(dc, op);
		char t[120];
		sprintf_s(t, "Ebene z=%d   +X ->   +Y ^   Links: Werkzeug   Rechts: Radieren   Bild auf/ab: Ebene", g_layer);
		SetTextColor(dc, RGB(200, 200, 210));
		TextOutA(dc, 6, 2, t, (int)strlen(t));
	}

	static LRESULT CALLBACK ProcLayer(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_PAINT: PaintBuffered(h, RenderLayer); return 0;
			case WM_ERASEBKGND: return 1;
			case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
				SetFocus(h);
				int x, y;
				if (!CellAt(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y)) return 0;
				PushUndo();
				int saved = g_tool;
				if (m == WM_RBUTTONDOWN) g_tool = 1;
				ApplyToolAt(x, y);
				if (m == WM_RBUTTONDOWN) g_tool = saved;
				g_painting = (g_tool <= 1) || m == WM_RBUTTONDOWN;
				if (g_painting) SetCapture(h);
				RedrawLayer();
				if (!g_painting) { if (g_tool == 2) SetTool(0); AfterChange(); }
				return 0;
			}
			case WM_MOUSEMOVE: {
				int x, y;
				bool in = CellAt(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
				if (in) {
					uint8_t r = 0, g = 0, b = 0; bool s = g_cur.Get(x, y, g_layer, r, g, b);
					char t[96]; sprintf_s(t, "Zelle x=%d y=%d z=%d   %s", x, y, g_layer, s ? "" : "(leer)");
					std::string txt = t;
					if (s) { char c[40]; sprintf_s(c, "RGB %d,%d,%d", r, g, b); txt += c; }
					SetWindowTextA(g_hHover, txt.c_str());
				}
				if (g_painting && in && (wp & (MK_LBUTTON | MK_RBUTTON))) {
					int saved = g_tool;
					if (wp & MK_RBUTTON) g_tool = 1;
					ApplyToolAt(x, y);
					g_tool = saved;
					RedrawLayer();
				}
				return 0;
			}
			case WM_LBUTTONUP: case WM_RBUTTONUP:
				if (g_painting) { g_painting = false; ReleaseCapture(); AfterChange(); }
				return 0;
			case WM_MOUSEWHEEL:
				ChangeLayer(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1);
				return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// Palette
	// =====================================================================
	// Drei Bereiche, raeumlich getrennt:
	//   links  BAUFARBEN  -- 7 x 12 Farben + 12 eigene Plaetze + Farben der Struktur
	//   rechts MARKER     -- eigene Spalte, beschriftet, mit Warnfarbe abgesetzt
	//
	// Markerfarben loest der Mod beim Setzen in OBJEKTE auf (Tuer, Fenster ...).
	// Die Erkennung ist toleranzbehaftet (MountainVillage::ClassifyMarker): je
	// Kanal zaehlt 0..10 als 0, 117..137 als 127 und 245..255 als 255. Eine
	// Baufarbe wie (250,4,6) waere also eine TUER. Deshalb wird jede Baufarbe,
	// die in so ein Band faellt, um wenige Stufen aus dem Band geschoben --
	// sichtbar kaum, aber nie mehr versehentlich ein Marker.
	struct MarkerDef { uint8_t r, g, b; char letter; const char* name; };
	static const MarkerDef kMarkerDefs[] = {
		{ 255, 0, 0,     'T', "Tuer" },
		{ 0, 255, 0,     'F', "Fenster" },
		{ 255, 255, 0,   'L', "Lampe" },
		{ 0, 0, 127,     'C', "Customize-Bench" },
		{ 0, 0, 255,     'S', "Schild" },
		{ 0, 255, 127,   'A', "Amboss" },
		{ 127, 255, 0,   'E', "Esse" },
		{ 127, 127, 0,   'X', "Tisch" },
		{ 255, 127, 127, 'B', "Blume" },
		{ 127, 0, 127,   'N', "Schmied-NPC" },
	};
	static const int kMarkerCount = (int)(sizeof(kMarkerDefs) / sizeof(kMarkerDefs[0]));

	static int MLevel(uint8_t v) { if (v <= 10) return 0; if (v >= 117 && v <= 137) return 1; if (v >= 245) return 2; return -1; }
	static const MarkerDef* MarkerOf(uint8_t r, uint8_t g, uint8_t b) {
		int R = MLevel(r), G = MLevel(g), B = MLevel(b);
		if (R < 0 || G < 0 || B < 0) return nullptr;
		for (int i = 0; i < kMarkerCount; i++)
			if (MLevel(kMarkerDefs[i].r) == R && MLevel(kMarkerDefs[i].g) == G && MLevel(kMarkerDefs[i].b) == B) return &kMarkerDefs[i];
		return nullptr;
	}
	static const char* MarkerName(uint8_t r, uint8_t g, uint8_t b) { const MarkerDef* m = MarkerOf(r, g, b); return m ? m->name : nullptr; }
	static char MarkerLetter(uint8_t r, uint8_t g, uint8_t b) { const MarkerDef* m = MarkerOf(r, g, b); return m ? m->letter : '?'; }

	// Baufarbe aus den Markerbaendern schieben (ein Kanal genuegt).
	static bool MakeSafe(uint8_t& r, uint8_t& g, uint8_t& b) {
		if (!MarkerOf(r, g, b)) return false;
		uint8_t* ch[3] = { &r, &g, &b };
		for (int k = 0; k < 3; k++) {
			int L = MLevel(*ch[k]);
			if (L == 0) { *ch[k] = 14; break; }
			if (L == 1) { *ch[k] = (*ch[k] <= 127) ? 113 : 141; break; }
			if (L == 2) { *ch[k] = 240; break; }
		}
		return true;
	}

	// ---- Baufarben: 7 Reihen x 12 ----------------------------------------
	static uint8_t g_build[7 * 12][3];
	static const char* kBuildRowName[7] = { "Grau", "kraeftig", "mittel", "gedeckt", "hell", "dunkel", "Baustoffe" };
	static void HsvToRgb(float h, float s, float v, uint8_t* o) {
		float c = v * s, hh = h / 60.0f, x = c * (1 - fabsf(fmodf(hh, 2.0f) - 1)), m = v - c, r = 0, g = 0, b = 0;
		if (hh < 1) { r = c; g = x; } else if (hh < 2) { r = x; g = c; } else if (hh < 3) { g = c; b = x; }
		else if (hh < 4) { g = x; b = c; } else if (hh < 5) { r = x; b = c; } else { r = c; b = x; }
		o[0] = (uint8_t)((r + m) * 255 + 0.5f); o[1] = (uint8_t)((g + m) * 255 + 0.5f); o[2] = (uint8_t)((b + m) * 255 + 0.5f);
	}
	static void BuildPalette() {
		static const uint8_t mats[12][3] = {     // Baustoffe aus MountainVillage + Gelaendetoene
			{ 112, 110, 120 }, { 150, 150, 162 }, { 228, 220, 196 }, { 165, 120, 70 }, { 120, 82, 46 }, { 122, 116, 104 },
			{ 168, 64, 48 },   { 92, 140, 60 },   { 70, 90, 140 },   { 200, 170, 110 }, { 84, 100, 52 }, { 150, 96, 66 } };
		const float S[5] = { 0.85f, 0.65f, 0.40f, 0.30f, 0.70f }, V[5] = { 0.92f, 0.72f, 0.70f, 0.95f, 0.40f };
		for (int c = 0; c < 12; c++) {
			uint8_t v = (uint8_t)(20 + c * 21);                         // Graustufen 20..251
			g_build[c][0] = g_build[c][1] = g_build[c][2] = v;
			for (int r = 0; r < 5; r++) HsvToRgb(c * 30.0f, S[r], V[r], g_build[(r + 1) * 12 + c]);
			for (int k = 0; k < 3; k++) g_build[6 * 12 + c][k] = mats[c][k];
		}
		for (int i = 0; i < 7 * 12; i++) MakeSafe(g_build[i][0], g_build[i][1], g_build[i][2]);
	}

	// ---- eigene Farbplaetze (Strukturen\farben.txt) ----------------------
	static std::string CustomFile() { return Lab::StructuresDir() + "\\farben.txt"; }
	static void LoadCustom() {
		for (int i = 0; i < 12; i++) g_custom[i][0] = -1;
		g_customLoaded = true;
		FILE* f = nullptr; if (fopen_s(&f, CustomFile().c_str(), "r") != 0 || !f) return;
		for (int i = 0; i < 12; i++) {
			int r, g, b; if (fscanf_s(f, "%d %d %d", &r, &g, &b) != 3) break;
			g_custom[i][0] = r; g_custom[i][1] = g; g_custom[i][2] = b;
		}
		fclose(f);
	}
	static void SaveCustom() {
		Lab::EnsureDir(Lab::StructuresDir());
		FILE* f = nullptr; if (fopen_s(&f, CustomFile().c_str(), "w") != 0 || !f) return;
		for (int i = 0; i < 12; i++) fprintf(f, "%d %d %d\n", g_custom[i][0], g_custom[i][1], g_custom[i][2]);
		fclose(f);
	}

	// ---- Farbe setzen -----------------------------------------------------
	static void ShowColorInEdits() {
		for (int k = 0; k < 3; k++) if (g_hRGB[k]) SetInt(g_hRGB[k], g_col[k]);
	}
	// allowMarker = false: Baufarbe (wird bei Bedarf aus den Markerbaendern geschoben)
	static void SetColor(uint8_t r, uint8_t g, uint8_t b, bool allowMarker) {
		if (!allowMarker) {
			const MarkerDef* m = MarkerOf(r, g, b);
			if (MakeSafe(r, g, b)) {
				char t[200];
				sprintf_s(t, "Diese Farbe waere als Marker '%s' gelesen worden (beim Setzen ein Objekt). Als Baufarbe auf (%d,%d,%d) verschoben.",
					m ? m->name : "?", r, g, b);
				Say(t);
			}
		}
		g_col[0] = r; g_col[1] = g; g_col[2] = b;
		g_colIsMarker = MarkerOf(r, g, b) != nullptr;
		if (g_tool == 1 || g_tool == 2) SetTool(0);
		ShowColorInEdits();
		RedrawPal();
	}

	// ---- Geometrie der Palette ------------------------------------------
	// Alles relativ zur Palettenflaeche; ein Feld = 20 px, Raster 23 px.
	static const int kSw = 20, kStep = 23, kHdr = 44;
	static const int kGridX = 0, kGridY = kHdr + 16;                       // 7 Reihen Baufarben
	static const int kCustY = kGridY + 7 * kStep + 20;                     // eigene Farben
	static const int kStructY = kCustY + kStep + 20;                       // Farben der Struktur (2 Reihen)
	static const int kMarkX = 12 * kStep + 22;                             // Markerspalte
	static const int kMarkY = kHdr + 16;
	static const int kPalH = kStructY + 2 * kStep + 4;                     // Gesamthoehe

	enum PalHit { PH_NONE, PH_BUILD, PH_CUSTOM, PH_STRUCT, PH_MARKER, PH_CURRENT };
	static PalHit PalHitTest(int mx, int my, int& idx) {
		idx = -1;
		if (mx < 48 && my < 42) return PH_CURRENT;
		auto cell = [&](int x0, int y0, int cols, int rows) -> int {
			if (mx < x0 || my < y0) return -1;
			int c = (mx - x0) / kStep, r = (my - y0) / kStep;
			if (c >= cols || r >= rows || (mx - x0) % kStep >= kSw || (my - y0) % kStep >= kSw) return -1;
			return r * cols + c;
		};
		if ((idx = cell(kGridX, kGridY, 12, 7)) >= 0) return PH_BUILD;
		if ((idx = cell(kGridX, kCustY, 12, 1)) >= 0) return PH_CUSTOM;
		if ((idx = cell(kGridX, kStructY, 12, 2)) >= 0) return (idx < (int)g_structColors.size()) ? PH_STRUCT : PH_NONE;
		if (mx >= kMarkX && my >= kMarkY) {
			int r = (my - kMarkY) / kStep;
			if (r < kMarkerCount && (my - kMarkY) % kStep < kSw) { idx = r; return PH_MARKER; }
		}
		idx = -1; return PH_NONE;
	}

	static void RenderPal(HDC dc, int cw, int ch) {
		RECT rc = { 0, 0, cw, ch }; FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		SetBkMode(dc, TRANSPARENT);
		HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
		HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
		auto sw = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, bool sel) {
			SetDCBrushColor(dc, RGB(r, g, b)); SetDCPenColor(dc, sel ? RGB(255, 255, 255) : RGB(40, 40, 40));
			Rectangle(dc, x, y, x + kSw, y + kSw);
			if (sel) { SetDCPenColor(dc, RGB(0, 0, 0)); HGDIOBJ nb = SelectObject(dc, GetStockObject(NULL_BRUSH)); Rectangle(dc, x - 2, y - 2, x + kSw + 2, y + kSw + 2); SelectObject(dc, nb); }
		};
		auto same = [&](uint8_t r, uint8_t g, uint8_t b) { return r == g_col[0] && g == g_col[1] && b == g_col[2]; };

		// Kopf: aktuelle Farbe
		SetDCBrushColor(dc, RGB(g_col[0], g_col[1], g_col[2])); SetDCPenColor(dc, RGB(0, 0, 0));
		Rectangle(dc, 0, 0, 42, 42);
		char t[160];
		const MarkerDef* cm = MarkerOf(g_col[0], g_col[1], g_col[2]);
		if (cm) {
			SetDCBrushColor(dc, RGB(255, 236, 160)); SetDCPenColor(dc, RGB(200, 120, 0));
			Rectangle(dc, 48, 2, cw - 2, 40);
			SetTextColor(dc, RGB(150, 40, 0));
			sprintf_s(t, "MARKER: %s  -- wird beim Setzen zum Objekt", cm->name);
			TextOutA(dc, 54, 6, t, (int)strlen(t));
			sprintf_s(t, "RGB %d, %d, %d", g_col[0], g_col[1], g_col[2]);
			TextOutA(dc, 54, 22, t, (int)strlen(t));
		} else {
			SetTextColor(dc, RGB(30, 30, 40));
			sprintf_s(t, "Baufarbe  RGB %d, %d, %d", g_col[0], g_col[1], g_col[2]);
			TextOutA(dc, 54, 6, t, (int)strlen(t));
			SetTextColor(dc, RGB(90, 90, 100));
			TextOutA(dc, 54, 22, "Doppelklick aufs Feld: freie Farbwahl", 37);
		}

		// Baufarben
		SetTextColor(dc, RGB(40, 40, 50));
		TextOutA(dc, kGridX, kGridY - 15, "Baufarben", 9);
		for (int i = 0; i < 7 * 12; i++) {
			int x = kGridX + (i % 12) * kStep, y = kGridY + (i / 12) * kStep;
			sw(x, y, g_build[i][0], g_build[i][1], g_build[i][2], same(g_build[i][0], g_build[i][1], g_build[i][2]));
		}
		// Eigene
		TextOutA(dc, kGridX, kCustY - 15, "Eigene Farben (Rechtsklick = ablegen)", 37);
		for (int i = 0; i < 12; i++) {
			int x = kGridX + i * kStep;
			if (g_custom[i][0] < 0) {
				SetDCBrushColor(dc, GetSysColor(COLOR_BTNFACE)); SetDCPenColor(dc, RGB(150, 150, 150));
				Rectangle(dc, x, kCustY, x + kSw, kCustY + kSw);
				SetDCPenColor(dc, RGB(170, 170, 170)); MoveToEx(dc, x + 4, kCustY + 10, NULL); LineTo(dc, x + 16, kCustY + 10);
				MoveToEx(dc, x + 10, kCustY + 4, NULL); LineTo(dc, x + 10, kCustY + 16);
			} else sw(x, kCustY, (uint8_t)g_custom[i][0], (uint8_t)g_custom[i][1], (uint8_t)g_custom[i][2],
				same((uint8_t)g_custom[i][0], (uint8_t)g_custom[i][1], (uint8_t)g_custom[i][2]));
		}
		// Struktur
		TextOutA(dc, kGridX, kStructY - 15, "In dieser Struktur (haeufigste)", 31);
		for (size_t i = 0; i < g_structColors.size() && i < 24; i++) {
			uint32_t c = g_structColors[i].first;
			uint8_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
			int x = kGridX + (int)(i % 12) * kStep, y = kStructY + (int)(i / 12) * kStep;
			sw(x, y, r, g, b, same(r, g, b));
			if (MarkerOf(r, g, b)) {                                   // Marker auch hier kennzeichnen
				SetTextColor(dc, (r + g + b > 380) ? RGB(0, 0, 0) : RGB(255, 255, 255));
				char L[2] = { MarkerOf(r, g, b)->letter, 0 }; TextOutA(dc, x + 6, y + 3, L, 1);
			}
		}

		// Markerspalte -- abgesetzt
		RECT mr = { kMarkX - 8, kMarkY - 20, cw, kMarkY + kMarkerCount * kStep + 2 };
		SetDCBrushColor(dc, RGB(255, 244, 214)); SetDCPenColor(dc, RGB(210, 150, 60));
		Rectangle(dc, mr.left, mr.top, mr.right, mr.bottom);
		SetTextColor(dc, RGB(150, 60, 0));
		TextOutA(dc, kMarkX, kMarkY - 17, "MARKER -> Objekte", 17);
		for (int i = 0; i < kMarkerCount; i++) {
			const MarkerDef& m = kMarkerDefs[i];
			int y = kMarkY + i * kStep;
			sw(kMarkX, y, m.r, m.g, m.b, same(m.r, m.g, m.b));
			SetTextColor(dc, (m.r + m.g + m.b > 380) ? RGB(0, 0, 0) : RGB(255, 255, 255));
			char L[2] = { m.letter, 0 }; TextOutA(dc, kMarkX + 6, y + 3, L, 1);
			SetTextColor(dc, RGB(60, 40, 20));
			TextOutA(dc, kMarkX + kSw + 6, y + 3, m.name, (int)strlen(m.name));
		}
		SelectObject(dc, ob); SelectObject(dc, op);
	}

	static void ChooseCustomColor() {
		static COLORREF custom[16];
		CHOOSECOLORA cc; memset(&cc, 0, sizeof(cc));
		cc.lStructSize = sizeof(cc); cc.hwndOwner = g_hwnd; cc.lpCustColors = custom;
		cc.rgbResult = RGB(g_col[0], g_col[1], g_col[2]); cc.Flags = CC_FULLOPEN | CC_RGBINIT;
		if (ChooseColorA(&cc))
			SetColor(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult), false);
	}

	static LRESULT CALLBACK ProcPal(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_PAINT: PaintBuffered(h, RenderPal); return 0;
			case WM_ERASEBKGND: return 1;
			case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
				int idx; PalHit hit = PalHitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), idx);
				if (m == WM_RBUTTONDOWN) {
					if (hit == PH_CUSTOM) {
						if (g_colIsMarker) { Say("Markerfarben gehoeren in die Marker-Spalte -- nicht als eigene Farbe abgelegt."); return 0; }
						g_custom[idx][0] = g_col[0]; g_custom[idx][1] = g_col[1]; g_custom[idx][2] = g_col[2];
						SaveCustom(); RedrawPal();
					}
					return 0;
				}
				switch (hit) {
					case PH_BUILD:  SetColor(g_build[idx][0], g_build[idx][1], g_build[idx][2], false); break;
					case PH_CUSTOM: if (g_custom[idx][0] >= 0) SetColor((uint8_t)g_custom[idx][0], (uint8_t)g_custom[idx][1], (uint8_t)g_custom[idx][2], false);
					                else Say("Leerer Platz: Rechtsklick legt die aktuelle Farbe hier ab."); break;
					case PH_STRUCT: {
						uint32_t c = g_structColors[idx].first;
						uint8_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
						SetColor(r, g, b, MarkerOf(r, g, b) != nullptr);   // vorhandene Marker duerfen wieder aufgenommen werden
						break;
					}
					case PH_MARKER: {
						const MarkerDef& d = kMarkerDefs[idx];
						SetColor(d.r, d.g, d.b, true);
						Say(std::string("Marker gewaehlt: ") + d.name + " -- wird beim Setzen zum Objekt.");
						break;
					}
					default: break;
				}
				return 0;
			}
			case WM_LBUTTONDBLCLK: {
				int idx; if (PalHitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), idx) == PH_CURRENT) ChooseCustomColor();
				return 0;
			}
			case WM_MOUSEMOVE: {
				int idx; PalHit hit = PalHitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), idx);
				char t[160] = "";
				switch (hit) {
					case PH_BUILD:  sprintf_s(t, "Baufarbe (%s)  RGB %d,%d,%d", kBuildRowName[idx / 12], g_build[idx][0], g_build[idx][1], g_build[idx][2]); break;
					case PH_CUSTOM: if (g_custom[idx][0] >= 0) sprintf_s(t, "Eigene Farbe %d  RGB %d,%d,%d  (Rechtsklick = ueberschreiben)", idx + 1, g_custom[idx][0], g_custom[idx][1], g_custom[idx][2]);
					                else sprintf_s(t, "Eigener Platz %d frei  (Rechtsklick = aktuelle Farbe ablegen)", idx + 1); break;
					case PH_STRUCT: { uint32_t c = g_structColors[idx].first; sprintf_s(t, "aus der Struktur  RGB %d,%d,%d  -- %d Voxel", (c >> 16) & 255, (c >> 8) & 255, c & 255, g_structColors[idx].second); break; }
					case PH_MARKER: sprintf_s(t, "MARKER %s  RGB %d,%d,%d  -- wird beim Setzen zum Objekt", kMarkerDefs[idx].name, kMarkerDefs[idx].r, kMarkerDefs[idx].g, kMarkerDefs[idx].b); break;
					case PH_CURRENT: sprintf_s(t, "Aktuelle Farbe -- Doppelklick: freie Farbwahl"); break;
					default: break;
				}
				if (t[0]) SetWindowTextA(g_hHover, t);
				return 0;
			}
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7b. Unterfenster "Karte & Leistung"  (PerfTweaks)
	// =====================================================================
	namespace {
		HWND g_perf = nullptr;
		HWND p_en, p_dark, p_mapR, p_lsR, p_mapAd, p_mapMove, p_mapIdle, p_pace, p_mts, p_strAd, p_strMove, p_strIdle, p_rep;
		HWND p_swZone, p_swSmall, p_swFence, p_swRes;   // Fehlersuche-Schalter
		enum { PID_APPLY = 3001, PID_VANILLA, PID_RECOMMEND, PID_SW_ZONE, PID_SW_SMALL, PID_SW_FENCE, PID_SW_RES, PID_TIMER = 7 };
	}

	static HWND PCtl(const char* cls, const char* t, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
		HWND c = CreateWindowExA(ex, cls, t, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_perf, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
		SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
		return c;
	}
	static HWND PChk(const char* t, int x, int y, int w) { return PCtl("BUTTON", t, BS_AUTOCHECKBOX, x, y, w, 20, -1); }
	static HWND PEdt(int x, int y) { return PCtl("EDIT", "", ES_AUTOHSCROLL, x, y, 50, 22, -1, WS_EX_CLIENTEDGE); }
	static void PLbl(const char* t, int x, int y, int w, bool bold = false) {
		HWND c = PCtl("STATIC", t, SS_LEFT, x, y, w, 16, -1);
		if (bold) SendMessageA(c, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
	}
	static void PSetChk(HWND h, bool v) { SendMessageA(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

	static void PerfToUi(const PerfTweaks::Settings& s, bool en) {
		PSetChk(p_en, en); PSetChk(p_dark, s.mapNoDarken); PSetChk(p_mapAd, s.mapAdaptive);
		PSetChk(p_pace, s.precisePacing); PSetChk(p_strAd, s.streamAdaptive);
		SetInt(p_mapR, s.mapRadius); SetInt(p_lsR, s.landscapeRadius); SetInt(p_mapMove, s.mapSleepMove); SetInt(p_mapIdle, s.mapSleepIdle);
		SetInt(p_mts, s.minTimeStep); SetInt(p_strMove, s.streamSleepMove); SetInt(p_strIdle, s.streamSleepIdle);
	}
	static PerfTweaks::Settings PerfFromUi() {
		PerfTweaks::Settings s;
		s.mapNoDarken = Checked(p_dark); s.mapAdaptive = Checked(p_mapAd); s.precisePacing = Checked(p_pace); s.streamAdaptive = Checked(p_strAd);
		s.mapRadius = GetInt(p_mapR, 20); s.landscapeRadius = GetInt(p_lsR, 5);
		s.mapSleepMove = GetInt(p_mapMove, 4); s.mapSleepIdle = GetInt(p_mapIdle, 15);
		s.minTimeStep = GetInt(p_mts, -1); s.streamSleepMove = GetInt(p_strMove, 1); s.streamSleepIdle = GetInt(p_strIdle, 5);
		return s;
	}

	static LRESULT CALLBACK PerfProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_perf = h;
				int x = 12, y = 10;
				p_en = PChk("Karte & Leistung aktiv  (aus = alle Bytes wieder Vanilla)", x, y, 420); y += 28;
				PLbl("Schritt 1 -- Karte", x, y, 300, true); y += 20;
				p_dark = PChk("Abdunklung unentdeckter Kacheln aus", x + 10, y, 320); y += 24;
				PLbl("Kartenfenster +- Kacheln (Vanilla 10, max 32; 1 Kachel = 4 Bl.):", x + 10, y + 4, 340); p_mapR = PEdt(x + 360, y); y += 26;
				PLbl("Landschaft +- Zonen (Vanilla 3, max 4 -- mehr = Speicherabsturz):", x + 10, y + 4, 350); p_lsR = PEdt(x + 360, y); y += 26;
				p_mapAd = PChk("Kartenthread adaptiv  (Pause in ms: Bewegung / Stand, Vanilla 10)", x + 10, y, 420); y += 24;
				PLbl("Bewegung:", x + 30, y + 4, 70); p_mapMove = PEdt(x + 100, y); PLbl("Stand:", x + 170, y + 4, 50); p_mapIdle = PEdt(x + 220, y); y += 32;
				PLbl("Schritt 2 -- Fluessiger", x, y, 300, true); y += 20;
				p_pace = PChk("Exakter Bildtakt (statt grobem Sleep im Begrenzer)", x + 10, y, 400); y += 24;
				PLbl("minTimeStep ms je Bild (-1 = options.cfg, 16 = 60 fps, 7 = 144, 0 = frei):", x + 10, y + 4, 400); p_mts = PEdt(x + 420, y); y += 26;
				p_strAd = PChk("Chunk-Streaming adaptiv  (Pause in ms: Bewegung / Stand, Vanilla 5)", x + 10, y, 420); y += 24;
				PLbl("Bewegung:", x + 30, y + 4, 70); p_strMove = PEdt(x + 100, y); PLbl("Stand:", x + 170, y + 4, 50); p_strIdle = PEdt(x + 220, y); y += 32;
				PCtl("BUTTON", "Uebernehmen", BS_PUSHBUTTON, x, y, 110, 26, PID_APPLY);
				PCtl("BUTTON", "Empfohlen", BS_PUSHBUTTON, x + 116, y, 100, 26, PID_RECOMMEND);
				PCtl("BUTTON", "Vanilla-Werte", BS_PUSHBUTTON, x + 222, y, 110, 26, PID_VANILLA);
				y += 34;
				PLbl("Fehlersuche: Bausteine abschalten (wirkt sofort, wird gespeichert)", x, y, 600, true); y += 20;
				p_swZone = PCtl("BUTTON", "Objekte mit der Zone erzeugen (Hangbaeume, Biom-Pflanzen, Zonen-Modelle)", BS_AUTOCHECKBOX, x + 10, y, 600, 20, PID_SW_ZONE); y += 22;
				p_swSmall = PCtl("BUTTON", "Kleine Blumen (verkleinerte Modelle wie Schilf)", BS_AUTOCHECKBOX, x + 10, y, 600, 20, PID_SW_SMALL); y += 22;
				p_swFence = PCtl("BUTTON", "Stadt: Zaeune an Treppen und Haengebruecken (native Objekte)", BS_AUTOCHECKBOX, x + 10, y, 600, 20, PID_SW_FENCE); y += 26;
				PSetChk(p_swFence, City::FencesOn());
				p_swRes = PCtl("BUTTON", "Stadt: Bewohner (grosse Elfen, Haendler auf dem Mutterbaum-Balkon)", BS_AUTOCHECKBOX, x + 10, y, 600, 20, PID_SW_RES); y += 26;
				PSetChk(p_swRes, City::ResidentsOn());
				PSetChk(p_swZone, BiomeMod::ZoneObjectsOn()); PSetChk(p_swSmall, BiomeMod::SmallFlowersOn());
				PLbl("Messung (alle 0,5 s):", x, y, 300, true); y += 18;
				p_rep = PCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 620, 236, -1, WS_EX_CLIENTEDGE);
				PerfToUi(PerfTweaks::Get(), PerfTweaks::IsEnabled());
				SetTimer(h, PID_TIMER, 500, NULL);
				return 0;
			}
			case WM_COMMAND:
				switch (LOWORD(wp)) {
					case PID_APPLY:
						PerfTweaks::Set(PerfFromUi()); PerfTweaks::SetEnabled(Checked(p_en));
						Say("Karte & Leistung uebernommen (wirkt im naechsten Bild, gespeichert in AdventureMod_tweaks.cfg)."); break;
					case PID_RECOMMEND: { PerfTweaks::Settings d; PerfToUi(d, true); break; }
					case PID_SW_ZONE: BiomeMod::SetZoneObjects(Checked(p_swZone)); Say(std::string("Objekte mit der Zone erzeugen: ") + (Checked(p_swZone) ? "AN." : "AUS (gilt fuer neu erzeugte Zonen).")); break;
					case PID_SW_RES: City::SetResidents(Checked(p_swRes)); ModSwitches::Set("stadt_bewohner", Checked(p_swRes)); Say(std::string("Stadt-Bewohner: ") + (Checked(p_swRes) ? "AN." : "AUS (es kommen keine neuen dazu).")); break;
					case PID_SW_FENCE: City::SetFences(Checked(p_swFence)); ModSwitches::Set("stadt_zaeune", Checked(p_swFence)); Say(std::string("Stadt-Zaeune: ") + (Checked(p_swFence) ? "AN." : "AUS (gilt fuer neu erzeugte Zonen).")); break;
					case PID_SW_SMALL: BiomeMod::SetSmallFlowers(Checked(p_swSmall)); Say(std::string("Kleine Blumen: ") + (Checked(p_swSmall) ? "AN." : "AUS (gilt fuer neu erzeugte Zonen).")); break;
					case PID_VANILLA: {
						PerfTweaks::Settings v; v.mapNoDarken = false; v.mapRadius = 10; v.landscapeRadius = 3; v.mapAdaptive = false;
						v.mapSleepMove = 10; v.mapSleepIdle = 10; v.precisePacing = false; v.minTimeStep = -1;
						v.streamAdaptive = false; v.streamSleepMove = 5; v.streamSleepIdle = 5;
						PerfToUi(v, Checked(p_en)); break;
					}
				}
				return 0;
			case WM_TIMER: {
				std::string r = PerfTweaks::Report();
				static std::string last;
				if (r != last) { last = r; SetWindowTextA(p_rep, r.c_str()); }
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}


	// =====================================================================
	// 7c. Unterfenster "Mob-Labor"  (MobLab)
	// =====================================================================
	// Links: alle 156 Rassen. Mitte: Spawnen mit Flags + nativer Ausruestung,
	// Galerie. Rechts: eigener Mob aus .cub-Teilen (inkl. Fluegel).
	namespace {
		HWND g_mobWnd = nullptr;
		HWND m_filter, m_list, m_count, m_level, m_dist, m_hos, m_cls, m_equip, m_wander, m_sizeF;
		HWND m_name, m_baseLbl, m_parts, m_src, m_idx, m_file, m_scale, m_setPos, m_px, m_py, m_pz,	m_size, m_cHos, m_cEquip, m_cWander, m_cLvl, m_cRace, m_cRName;
		HWND m_eq, m_eCat, m_eId, m_eMat, m_eRar, m_eLvl;
		int  g_eqSlot = 2;                         // Brust vorausgewaehlt
		std::vector<int> m_rows;                  // Listenzeile -> Rasse
		MobLab::CustomMob g_mob = MobLab::EmptyMob();
		int g_mobPart = 0;
		int g_galleryNext = 0;
		enum { MID_FILTER = 4001, MID_LIST, MID_SPAWN, MID_GALLERY, MID_GALLERY_NEXT, MID_DESCRIBE,
		       MID_BASE, MID_NEW, MID_LOAD, MID_SAVE, MID_PARTS, MID_PART_APPLY, MID_FILE, MID_FROM_EDITOR, MID_SPAWN_CUSTOM,
									       MID_NATIVE, MID_EQ, MID_EQ_APPLY, MID_EQ_EMPTY, MID_EQ_KEEP, MID_SRC, MID_EDIT_PART,
									       MID_KB_READ, MID_KB_BASE, MID_KB_TEST, MID_KB_SAVE, MID_KB_RESET, MID_TIMER = 9 };
		const int kHosVals[5] = { -1, 1, 101, 5, 6 };   // 101 = friedlich (Feindlichkeit 1 + Flag 0x10, wie natives Huhn)
		const int kClsVals[11] = { -1, 1, 2, 3, 4, 0x80, 0x81, 0x82, 0x83, 0x84, 0x88 };
	}

	static HWND MCtl(const char* cls, const char* t, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
		HWND c = CreateWindowExA(ex, cls, t, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_mobWnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
		SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
		return c;
	}
	static void MLbl(const char* t, int x, int y, int w, bool bold = false) {
		HWND c = MCtl("STATIC", t, SS_LEFT, x, y, w, 16, -1);
		if (bold) SendMessageA(c, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
	}
	static HWND MEdt(const char* t, int x, int y, int w, int id = -1) { return MCtl("EDIT", t, ES_AUTOHSCROLL, x, y, w, 22, id, WS_EX_CLIENTEDGE); }
	static HWND MBtn(const char* t, int x, int y, int w, int id, int h = 24) { return MCtl("BUTTON", t, BS_PUSHBUTTON, x, y, w, h, id); }
	static HWND MChk(const char* t, int x, int y, int w, bool on) { HWND c = MCtl("BUTTON", t, BS_AUTOCHECKBOX, x, y, w, 20, -1); SendMessageA(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); return c; }
	static HWND MCombo(int x, int y, int w, const char* const* items, int n, int sel) {
		HWND c = MCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, 260, -1);
		for (int i = 0; i < n; i++) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
		SendMessageA(c, CB_SETCURSEL, sel, 0);
		return c;
	}
	static int ComboSel(HWND h) { int i = (int)SendMessageA(h, CB_GETCURSEL, 0, 0); return i < 0 ? 0 : i; }
	static float GetFloat(HWND h, float def) { std::string t = GetText(h); return t.empty() ? def : (float)atof(t.c_str()); }
	static void SetFloat(HWND h, float v) { char b[32]; sprintf_s(b, "%.2f", v); SetWindowTextA(h, b); }

	// Eigene Tiere (2026-10-14): jede .mob unter Strukturen\Mobs mit eigener Rasse (>= 200) bekommt ihre Rassennummer
	// als Zeile in der Liste -- Auswahl + SPAWNEN (oder Doppelklick) setzt das Tier direkt; Suche nach Nummer oder Name.
	struct OwnMob { int race; std::string name, file, kind; int hostility; };
	static std::vector<OwnMob> g_ownMobs; static DWORD g_ownScan = 0;
	static void ScanOwnMobs(const std::string& dir, int depth) {
		if (depth > 4) return;
		WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (hf == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) ScanOwnMobs(p, depth + 1); continue; }
			if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".mob")) continue;
			std::ifstream in(p); std::string line; OwnMob o{ -1, "", p, "", 1 };
			bool swim = false; int fly = -1;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				size_t eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
				std::string k = line.substr(0, eq), v = line.substr(eq + 1);
				if (k == "eigene_rasse") o.race = atoi(v.c_str());
				else if (k == "rassen_name") o.name = v;
				else if (k == "name" && o.name.empty()) o.name = v;
				else if (k == "feindlichkeit") o.hostility = atoi(v.c_str());
				else if (k == "friedlich" && atoi(v.c_str())) o.hostility = 101;
				else if (k == "schwimmt") swim = atoi(v.c_str()) != 0;
				else if (k == "fliegt") fly = atoi(v.c_str());
			}
			if (o.race < 200) continue;
			o.kind = swim ? "Wasser" : (fly == 1 ? "Luft" : "Land");
			bool dup = false; for (const OwnMob& q : g_ownMobs) if (q.race == o.race) dup = true;
			if (!dup) g_ownMobs.push_back(o);
		} while (FindNextFileA(hf, &fd));
		FindClose(hf);
	}
	static const OwnMob* OwnByRace(int r) { for (const OwnMob& o : g_ownMobs) if (o.race == r) return &o; return nullptr; }
	static void MobFillList() {
		std::string f = Lower(GetText(m_filter));
		if (g_ownMobs.empty() || GetTickCount() - g_ownScan > 15000) {
			g_ownScan = GetTickCount(); g_ownMobs.clear(); ScanOwnMobs(MobLab::MobsDir(), 0);
			std::sort(g_ownMobs.begin(), g_ownMobs.end(), [](const OwnMob& a, const OwnMob& b) { return a.race < b.race; });
		}
		SendMessageA(m_list, LB_RESETCONTENT, 0, 0); m_rows.clear();
		for (int r = 0; r < MobLab::kRaceCount; r++) {
			char b[96];
			sprintf_s(b, "%3d  %-18s %-9s [%d]", r, MobLab::RaceName(r), MobLab::RaceKind(r), MobLab::DefaultHostility(r));
			if (!f.empty() && Lower(b).find(f) == std::string::npos) continue;
			SendMessageA(m_list, LB_ADDSTRING, 0, (LPARAM)b); m_rows.push_back(r);
		}
		for (const OwnMob& o : g_ownMobs) {                                    // eigene Tiere (Nummer = eigene Rasse)
			char b[128];
			sprintf_s(b, "%3d  %-18.18s %-9s [%s] eigen", o.race, o.name.c_str(), o.kind.c_str(), (o.hostility == 101 || o.hostility == 2 || o.hostility == 3 || o.hostility == 4) ? "friedl." : std::to_string(o.hostility).c_str());
			if (!f.empty() && Lower(b).find(f) == std::string::npos) continue;
			SendMessageA(m_list, LB_ADDSTRING, 0, (LPARAM)b); m_rows.push_back(o.race);
		}
	}
	static int MobSelectedRace() {
		int i = (int)SendMessageA(m_list, LB_GETCURSEL, 0, 0);
		return (i >= 0 && i < (int)m_rows.size()) ? m_rows[i] : -1;
	}

	static std::string PartLine(int i) {
		const MobLab::Part& p = g_mob.parts[i];
		char b[200];
		const char* src = p.source == MobLab::SRC_BASE ? "von der Basisrasse" : p.source == MobLab::SRC_NONE ? "keins" :
			p.source == MobLab::SRC_NATIVE ? "natives Modell" : "eigene .cub";
		std::string detail;
		if (p.source == MobLab::SRC_NATIVE) {
			const char* n = LabModelNames::NameOf(p.nativeIndex);
			char t[64]; sprintf_s(t, "0x%X %s", p.nativeIndex, n ? n : ""); detail = t;
		} else if (p.source == MobLab::SRC_FILE) {
			size_t k = p.file.find_last_of("\\/"); detail = k == std::string::npos ? p.file : p.file.substr(k + 1);
		}
		char sc[24] = ""; if (p.scale > 0) sprintf_s(sc, "  x%.2f", p.scale);
		sprintf_s(b, "%-9s %-16s %s%s", MobLab::PartName(i), src, detail.c_str(), sc);
		return b;
	}
	static void MobRefreshParts() {
		int sel = g_mobPart;
		SendMessageA(m_parts, LB_RESETCONTENT, 0, 0);
		for (int i = 0; i < MobLab::P_COUNT; i++) SendMessageA(m_parts, LB_ADDSTRING, 0, (LPARAM)PartLine(i).c_str());
		SendMessageA(m_parts, LB_SETCURSEL, sel, 0);
		char b[96]; sprintf_s(b, "Basis: %d %s", g_mob.baseRace, MobLab::RaceName(g_mob.baseRace));
		SetWindowTextA(m_baseLbl, b);
	}
	static void MobShowPart() {
		const MobLab::Part& p = g_mob.parts[g_mobPart];
		SendMessageA(m_src, CB_SETCURSEL, p.source, 0);
		SetHex(m_idx, p.nativeIndex < 0 ? 0 : p.nativeIndex);
		SetWindowTextA(m_file, p.file.c_str());
		SetFloat(m_scale, p.scale);
		SendMessageA(m_setPos, BM_SETCHECK, p.setPos ? BST_CHECKED : BST_UNCHECKED, 0);
		SetFloat(m_px, p.pos[0]); SetFloat(m_py, p.pos[1]); SetFloat(m_pz, p.pos[2]);
		EnableWindow(m_setPos, MobLab::PartHasPosition(g_mobPart));
	}
	static void MobToUi() {
		SetWindowTextA(m_name, g_mob.name.c_str());
		SetFloat(m_size, g_mob.sizeFactor);
		int h = 0; for (int i = 0; i < 5; i++) if (kHosVals[i] == (g_mob.peaceful ? 101 : g_mob.hostility)) h = i;
		SetInt(m_cLvl, g_mob.level); SetInt(m_cRace, g_mob.ownRace); SetWindowTextA(m_cRName, g_mob.raceName.c_str());
		SendMessageA(m_cHos, CB_SETCURSEL, h, 0);
		SendMessageA(m_cEquip, BM_SETCHECK, g_mob.equipment ? BST_CHECKED : BST_UNCHECKED, 0);
		SendMessageA(m_cWander, BM_SETCHECK, g_mob.wander ? BST_CHECKED : BST_UNCHECKED, 0);
		MobRefreshParts(); MobShowPart();
	}
	static void MobFromUi() {
		g_mob.name = Lab::SafeFileName(GetText(m_name));
		{ float f = GetFloat(m_size, 1.0f); g_mob.sizeFactor = f < 0.2f ? 0.2f : (f > 10.0f ? 10.0f : f); }
		int h = kHosVals[(std::max)(0, ComboSel(m_cHos))]; g_mob.peaceful = h == 101; g_mob.hostility = h < 0 ? MobLab::DefaultHostility(g_mob.baseRace) : (h == 101 ? 1 : h);
		g_mob.equipment = Checked(m_cEquip); g_mob.wander = Checked(m_cWander);
		g_mob.level = GetInt(m_cLvl, -1); if (g_mob.level < 1) g_mob.level = -1;
		g_mob.ownRace = GetInt(m_cRace, -1); if (g_mob.ownRace < 200) g_mob.ownRace = -1; g_mob.raceName = GetText(m_cRName);
	}
	static void MobApplyPart() {
		MobLab::Part& p = g_mob.parts[g_mobPart];
		p.source = ComboSel(m_src);
		p.nativeIndex = GetInt(m_idx, -1);
		p.file = GetText(m_file);
		p.scale = GetFloat(m_scale, 0);
		p.setPos = Checked(m_setPos) && MobLab::PartHasPosition(g_mobPart);
		p.pos[0] = GetFloat(m_px, 0); p.pos[1] = GetFloat(m_py, 0); p.pos[2] = GetFloat(m_pz, 0);
		if (p.source == MobLab::SRC_FILE && p.file.empty()) Say("Eigene .cub gewaehlt, aber keine Datei -- '...' oder 'aus Editor'.");
		MobRefreshParts();
	}

	// ---- Ausruestung des eigenen Mobs ----
	static std::string EquipLine(int i) {
		const MobLab::EquipItem& e = g_mob.equip[i];
		char b[160];
		if (!e.set) sprintf_s(b, "%-12s (Ausruester / Basis)", MobLab::EquipSlotName(i));
		else if (e.category == 0) sprintf_s(b, "%-12s leer", MobLab::EquipSlotName(i));
		else sprintf_s(b, "%-12s Kat %d  ID %d  Mat %d  Selt %d  Lv %d%s", MobLab::EquipSlotName(i), e.category, e.id, e.material, e.rarity, e.level,
			e.category == 3 ? "  Waffe" : e.category == 4 ? "  Brust" : "");
		return b;
	}
	static void EquipRefresh() {
		SendMessageA(m_eq, LB_RESETCONTENT, 0, 0);
		for (int i = 0; i < MobLab::kEquipSlots; i++) SendMessageA(m_eq, LB_ADDSTRING, 0, (LPARAM)EquipLine(i).c_str());
		SendMessageA(m_eq, LB_SETCURSEL, g_eqSlot, 0);
	}
	static void EquipShow() {
		const MobLab::EquipItem& e = g_mob.equip[g_eqSlot];
		SetInt(m_eCat, e.category); SetInt(m_eId, e.id); SetInt(m_eMat, e.material); SetInt(m_eRar, e.rarity); SetInt(m_eLvl, e.level);
	}

	static MobLab::SpawnOpts MobOpts() {
		MobLab::SpawnOpts o;
		o.race = MobSelectedRace();
		o.count = GetInt(m_count, 1); o.level = GetInt(m_level, -1); o.distance = GetInt(m_dist, 6);
		{ int hv = kHosVals[(std::max)(0, ComboSel(m_hos))]; o.peaceful = hv == 101; o.hostility = hv == 101 ? 1 : hv; }
		o.charClass = kClsVals[ComboSel(m_cls)];
		o.equipment = Checked(m_equip); o.wander = Checked(m_wander);
		float f = GetFloat(m_sizeF, 1.0f); o.sizeFactor = f < 0.2f ? 0.2f : (f > 10.0f ? 10.0f : f);
		return o;
	}

	// ---- Koerperbau der Rasse (2026-10-16): Definition, wo welches Koerperteil liegt ----------------------
	namespace {
		HWND k_race, k_val[MobLab::kBodyFieldCount][3], k_chk[MobLab::kBodyFieldCount];
		int  g_kbRace = -1; bool g_kbIsMob = false; MobLab::CustomMob g_kbMob;
		CRITICAL_SECTION g_kbCs; bool g_kbCsInit = false;
		bool g_kbReady = false, g_kbOk = false; int g_kbReadRace = -1; MobLab::BodyVals g_kbNat, g_kbEff; bool g_kbHasOv = false; MobLab::BodyVals g_kbOv;
	}
	static void KbCs() { if (!g_kbCsInit) { InitializeCriticalSection(&g_kbCs); g_kbCsInit = true; } }
	static void KbRequestMob(const MobLab::CustomMob& m) {
		if (m.ownRace < 200) { Say("Der Mob hat keine eigene Rasse (>= 200)."); return; }
		g_kbRace = m.ownRace; g_kbMob = m; g_kbIsMob = true; KbCs();
		char b[120]; sprintf_s(b, "Eigene Rasse %d %s (lese ...)", m.ownRace, m.name.c_str()); SetWindowTextA(k_race, b);
		MobLab::CustomMob mm = m; int race = m.ownRace;
		LabBridge::Post("Koerperbau lesen", [mm, race](cube::GameController*) {
			MobLab::BodyVals n, e, ov; bool ok = MobLab::ReadBodyValuesMob(mm, n, e); bool has = MobLab::GetBodyDef(race, ov);
			EnterCriticalSection(&g_kbCs); g_kbReady = true; g_kbOk = ok; g_kbReadRace = race; g_kbNat = n; g_kbEff = e; g_kbHasOv = has; g_kbOv = ov; LeaveCriticalSection(&g_kbCs);
		});
	}
	static void KbRequest(int race) {
		if (race >= 200) {                                                 // eigenes Tier: .mob dazu suchen
			const OwnMob* o = OwnByRace(race); MobLab::CustomMob m; std::string err;
			if (!o || !MobLab::LoadMob(o->file, m, err)) { Say("Koerperbau: .mob zur Rasse " + std::to_string(race) + " nicht gefunden."); return; }
			KbRequestMob(m); return;
		}
		if (race < 0 || race >= MobLab::kRaceCount) { Say("Koerperbau: keine gueltige Rasse gewaehlt."); return; }
		g_kbRace = race; g_kbIsMob = false; KbCs();
		char b[96]; sprintf_s(b, "Rasse %d %s (lese ...)", race, MobLab::RaceName(race)); SetWindowTextA(k_race, b);
		LabBridge::Post("Koerperbau lesen", [race](cube::GameController*) {
			MobLab::BodyVals n, e, ov; bool ok = MobLab::ReadBodyValues(race, n, e); bool has = MobLab::GetBodyDef(race, ov);
			EnterCriticalSection(&g_kbCs); g_kbReady = true; g_kbOk = ok; g_kbReadRace = race; g_kbNat = n; g_kbEff = e; g_kbHasOv = has; g_kbOv = ov; LeaveCriticalSection(&g_kbCs);
		});
	}
	static void KbShow(const MobLab::BodyVals& eff, const MobLab::BodyVals& ov) {
		for (int f = 0; f < MobLab::kBodyFieldCount; f++) {
			for (int k = 0; k < 3; k++) if (k < MobLab::kBodyFields[f].n) { char b[24]; sprintf_s(b, "%.2f", eff.val[f][k]); SetWindowTextA(k_val[f][k], b); }
			SendMessageA(k_chk[f], BM_SETCHECK, (ov.mask & (1u << f)) ? BST_CHECKED : BST_UNCHECKED, 0);
		}
	}
	static void KbPoll() {
		if (!g_kbCsInit) return;
		EnterCriticalSection(&g_kbCs);
		bool rd = g_kbReady; g_kbReady = false; bool ok = g_kbOk; int race = g_kbReadRace; MobLab::BodyVals e = g_kbEff, ov = g_kbHasOv ? g_kbOv : MobLab::BodyVals(), n = g_kbNat;
		LeaveCriticalSection(&g_kbCs);
		if (!rd) return;
		if (!ok) { Say("Koerperbau: Rasse " + std::to_string(race) + " konnte nicht gelesen werden (im Spiel?)."); return; }
		KbShow(e, ov);
		char b[120]; sprintf_s(b, "Rasse %d %s%s", race, race >= 200 ? g_kbMob.name.c_str() : MobLab::RaceName(race), ov.mask ? "  (angepasst)" : (race >= 200 ? "  (wie .mob)" : "  (nativ)")); SetWindowTextA(k_race, b);
		char p[220]; sprintf_s(p, "Koerperbau %s gelesen. Ohne Anpassung: Koerper %.2f/%.2f/%.2f, Kopf %.2f/%.2f/%.2f, Haende %.2f/%.2f/%.2f, Fuesse %.2f/%.2f/%.2f.", race >= 200 ? g_kbMob.name.c_str() : MobLab::RaceName(race),
		          n.val[8][0], n.val[8][1], n.val[8][2], n.val[9][0], n.val[9][1], n.val[9][2], n.val[10][0], n.val[10][1], n.val[10][2], n.val[11][0], n.val[11][1], n.val[11][2]);
		Say(p);
	}
	// Felder mit Haken = angepasst (ersetzen den nativen Wert), ohne Haken = nativ
	static MobLab::BodyVals KbFromUi() {
		MobLab::BodyVals v;
		for (int f = 0; f < MobLab::kBodyFieldCount; f++) {
			for (int k = 0; k < MobLab::kBodyFields[f].n; k++) v.val[f][k] = GetFloat(k_val[f][k], 0);
			if (Checked(k_chk[f])) v.mask |= 1u << f;
		}
		return v;
	}
	static void KbCreate(int x, int y) {
		MLbl("Koerperbau der Rasse (Definition)", x, y, 300, true); y += 20;
		MCtl("STATIC", "Wo welches Teil liegt und wie gross es ist -- gilt fuer ALLE neuen Kreaturen dieser Rasse (nativ + eigene Tiere mit 'von der Basisrasse').", SS_LEFT, x, y, 300, 30, -1); y += 34;
		k_race = MCtl("STATIC", "Rasse: (keine gelesen)", SS_LEFT, x, y + 4, 300, 16, -1); y += 22;
		MBtn("Auswahl links lesen", x, y, 148, MID_KB_READ); MBtn("Eigener Mob (rechts)", x + 152, y, 148, MID_KB_BASE); y += 30;
		MLbl("Haken = anpassen", x, y, 120); MLbl("x / Breite", x + 150, y, 50); MLbl("y / Tiefe", x + 202, y, 50); MLbl("z / Hoehe", x + 254, y, 50); y += 18;
		for (int f = 0; f < MobLab::kBodyFieldCount; f++) {
			k_chk[f] = MChk(MobLab::kBodyFields[f].label, x, y, 146, false);
			for (int k = 0; k < 3; k++) k_val[f][k] = k < MobLab::kBodyFields[f].n ? MEdt("0", x + 150 + k * 52, y - 1, 48) : nullptr;
			y += 25;
		}
		y += 4;
		MBtn("Testen: Rasse spawnen", x, y, 148, MID_KB_TEST); MBtn("Speichern (Datei)", x + 152, y, 148, MID_KB_SAVE); y += 28;
		MBtn("Rasse auf nativ zuruecksetzen", x, y, 300, MID_KB_RESET); y += 30;
		MLbl("Positionen in Bloecken relativ zur Kreaturmitte;", x, y, 300); y += 16;
		MLbl("Haende/Fuesse: x = Abstand zur Seite (gespiegelt).", x, y, 300); y += 16;
		MLbl("Testen setzt die Werte sofort (ohne Datei) und spawnt", x, y, 300); y += 16;
		MLbl("die Rasse 6 Bloecke vor dir. Speichern macht es dauerhaft.", x, y, 300); y += 16;
		MLbl("Wirkt auf neu entstehende Kreaturen (neue Zonen).", x, y, 300);
	}

	static LRESULT CALLBACK MobProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_mobWnd = h;
				// ---- links: Rassen ----
				int x = 10, y = 10;
				MLbl("Alle Rassen des Spiels  (Nr  Name  Art  [Standard-Feindlichkeit])", x, y, 300, true); y += 20;
				MLbl("Suche:", x, y + 4, 40); m_filter = MEdt("", x + 42, y, 258, MID_FILTER); y += 26;
				m_list = MCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 300, 560, MID_LIST, WS_EX_CLIENTEDGE);
				HFONT mono = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
				SendMessageA(m_list, WM_SETFONT, (WPARAM)mono, TRUE);

				// ---- Mitte: Spawnen ----
				x = 322; y = 10;
				MLbl("Spawnen (wie das Spiel)", x, y, 290, true); y += 22;
				MLbl("Anzahl:", x, y + 4, 60); m_count = MEdt("1", x + 64, y, 50);
				MLbl("Abstand +X:", x + 130, y + 4, 76); m_dist = MEdt("6", x + 210, y, 50); y += 26;
				MLbl("Level (-1 = wie Spieler):", x, y + 4, 160); m_level = MEdt("-1", x + 164, y, 60); y += 26;
				MLbl("Groesse x (0.2 - 10, 1 = normal):", x, y + 4, 196); m_sizeF = MEdt("1", x + 200, y, 60); y += 28;
				MLbl("Feindlichkeit:", x, y + 4, 90);
				const char* hos[5] = { "Standard der Rasse", "1  Monster (greift an)", "1  friedlich (wie Huhn)", "5  Begleiter", "6  Passiv (Objekt)" };
				m_hos = MCombo(x + 94, y, 196, hos, 5, 0); y += 28;
				MLbl("Klasse / Beruf:", x, y + 4, 90);
				const char* cls[11] = { "Spiel entscheidet", "1  Krieger", "2  Waldlaeufer", "3  Magier", "4  Schurke", "0x80 Haendler",
				                        "0x81 Dorf-NPC", "0x82 Dorf-NPC", "0x83 Dorf-NPC", "0x84 Gastwirt", "0x88 Steg-NPC" };
				m_cls = MCombo(x + 94, y, 196, cls, 11, 0); y += 28;
				m_equip = MChk("native Ausruestung (Waffen/Ruestung je Rasse)", x, y, 290, true); y += 22;
				m_wander = MChk("umherwandern (RandomWalk um den Spawnpunkt)", x, y, 290, true); y += 28;
				MBtn("SPAWNEN", x, y, 290, MID_SPAWN, 30); y += 38;
				MLbl("Galerie: Rassen nebeneinander, friedlich, 4 Bl. Abstand", x, y, 290); y += 18;
				MBtn("12 Rassen ab Auswahl", x, y, 143, MID_GALLERY); MBtn("naechste 12", x + 147, y, 143, MID_GALLERY_NEXT); y += 32;
				MBtn("Letzten Mob auslesen (Teile, Skalen, Positionen)", x, y, 290, MID_DESCRIBE); y += 32;
				MLbl("1 = Monster, friedlich = 1 + Flag 0x10 (wie Huhn),", x, y, 290); y += 16;
				MLbl("5 = Begleiter, 6 = Objekt (gemessen an nativen Tieren).", x, y, 290); y += 16;
				MLbl("Klasse 1-4: der Ausruester wuerfelt so lange,", x, y, 290); y += 16;
				MLbl("bis sie faellt (Sonderrassen haben feste Klassen).", x, y, 290);

				// ---- rechts: eigener Mob ----
				x = 626; y = 10;
				MLbl("Eigener Mob aus .cub-Teilen", x, y, 300, true); y += 22;
				MLbl("Name:", x, y + 4, 40); m_name = MEdt("", x + 44, y, 256); y += 26;
				m_baseLbl = MCtl("STATIC", "Basis:", SS_LEFT, x, y + 4, 170, 16, -1);
				MBtn("Auswahl als Basis", x + 172, y, 128, MID_BASE); y += 28;
				MBtn("Neuer leerer Mob", x, y, 112, MID_NEW); MBtn("Laden ...", x + 116, y, 90, MID_LOAD); MBtn("Speichern", x + 210, y, 90, MID_SAVE); y += 28;
				MBtn("Native Rasse laden (Aussehen + Ausruestung)", x, y, 300, MID_NATIVE); y += 30;
				m_parts = MCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 300, 112, MID_PARTS, WS_EX_CLIENTEDGE);
				SendMessageA(m_parts, WM_SETFONT, (WPARAM)mono, TRUE);
				y += 116;
				MLbl("Quelle:", x, y + 4, 50);
				const char* srcs[4] = { "von der Basisrasse", "keins", "natives Modell (Index)", "eigene .cub-Datei" };
				m_src = MCombo(x + 52, y, 248, srcs, 4, 0); SetWindowLongPtrA(m_src, GWLP_ID, MID_SRC); y += 28;   // Wechsel gilt sofort
				MLbl("Index:", x, y + 4, 40); m_idx = MEdt("0x0", x + 44, y, 70);
				MLbl("(z.B. 0x4D4 elf-head)", x + 120, y + 4, 180); y += 26;
				MLbl("Datei:", x, y + 4, 40); m_file = MEdt("", x + 44, y, 184); MBtn("...", x + 232, y - 1, 30, MID_FILE); MBtn("Ed.", x + 266, y - 1, 34, MID_FROM_EDITOR); y += 28;
				MBtn("Modell im Editor aendern", x, y, 148, MID_EDIT_PART); MBtn("Geaendertes uebernehmen", x + 152, y, 148, MID_FROM_EDITOR); y += 28;
				MLbl("Skala (0 = Basis):", x, y + 4, 110); m_scale = MEdt("0", x + 114, y, 60); y += 26;
				m_setPos = MChk("Position setzen", x, y, 110, false);
				m_px = MEdt("0", x + 114, y - 1, 58); m_py = MEdt("0", x + 176, y - 1, 58); m_pz = MEdt("0", x + 238, y - 1, 62); y += 28;
				MBtn("Teil uebernehmen", x, y, 300, MID_PART_APPLY); y += 32;
				MLbl("Groesse x (1 = wie die Basis):", x, y + 4, 160); m_size = MEdt("1", x + 164, y, 60); y += 26;
				MLbl("Feindlichkeit:", x, y + 4, 90); m_cHos = MCombo(x + 94, y, 206, hos, 5, 2); y += 28;
				MLbl("Level (-1 = wie Spieler):", x, y + 4, 160); m_cLvl = MEdt("-1", x + 164, y, 80); y += 26;
				MLbl("Eigene Rasse (>=200):", x, y + 4, 120); m_cRace = MEdt("-1", x + 124, y, 50); MLbl("Name:", x + 180, y + 4, 34); m_cRName = MEdt("", x + 216, y, 84); y += 26;
				m_cEquip = MChk("Ausruestung", x, y, 110, false); m_cWander = MChk("umherwandern", x + 120, y, 150, true); y += 26;
				MBtn("EIGENEN MOB SPAWNEN", x, y, 300, MID_SPAWN_CUSTOM, 30);

				// ---- 4. Spalte: Ausruestung ----
				x = 940; y = 10;
				MLbl("Ausruestung des eigenen Mobs", x, y, 290, true); y += 20;
				MLbl("13 Plaetze ab Kreatur+0x300 (je 0x118 Byte)", x, y, 290); y += 20;
				m_eq = MCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 290, 214, MID_EQ, WS_EX_CLIENTEDGE);
				SendMessageA(m_eq, WM_SETFONT, (WPARAM)mono, TRUE); y += 220;
				MLbl("Kategorie:", x, y + 4, 70); m_eCat = MEdt("0", x + 72, y, 50);
				MLbl("ID:", x + 134, y + 4, 24); m_eId = MEdt("0", x + 160, y, 50); y += 26;
				MLbl("Material:", x, y + 4, 70); m_eMat = MEdt("0", x + 72, y, 50);
				MLbl("Selt.:", x + 134, y + 4, 34); m_eRar = MEdt("0", x + 170, y, 40); y += 26;
				MLbl("Level:", x, y + 4, 70); m_eLvl = MEdt("1", x + 72, y, 70); y += 30;
				MBtn("Platz uebernehmen", x, y, 290, MID_EQ_APPLY); y += 28;
				MBtn("Platz leeren", x, y, 143, MID_EQ_EMPTY); MBtn("wie Ausruester", x + 147, y, 143, MID_EQ_KEEP); y += 32;
				MLbl("Belegt: Kategorie 3 = Waffe, 4 = Brustruestung", x, y, 290); y += 16;
				MLbl("(Ausruester @0x5C7730). Waffen-IDs je Klasse:", x, y, 290); y += 16;
				MLbl("Krieger 0-2 / 15-17, Waldlaeufer 6-8, u.a.", x, y, 290); y += 16;
				MLbl("Andere Werte: ausprobieren, Wirkung im Spiel.", x, y, 290); y += 16;
				MLbl("Tipp: 'Native Rasse laden' fuellt alle Plaetze", x, y, 290); y += 16;
				MLbl("mit dem, was das Spiel dieser Rasse gibt.", x, y, 290);
				KbCreate(1245, 10);                                         // 5. Spalte: Koerperbau der Rasse
				SetTimer(h, MID_TIMER, 200, NULL);

				MobFillList();
				SendMessageA(m_list, LB_SETCURSEL, 0, 0);
				MobToUi();
				EquipRefresh(); EquipShow();
				return 0;
			}
			case WM_TIMER: {
				KbPoll();
				MobLab::NativeTemplate t;
				if (MobLab::TakeTemplate(t)) {
					if (!t.ok) { Say("Native Vorlage konnte nicht gelesen werden (siehe Protokoll)."); return 0; }
					MobLab::ApplyTemplate(t, g_mob);
					g_mob.name = std::string(MobLab::RaceName(t.race)) + "-bearbeitet";
					MobToUi(); EquipRefresh(); EquipShow();
					char b[160]; sprintf_s(b, "Native Vorlage geladen: %s (Klasse 0x%X). Alle Teile, Skalen, Positionen und die Ausruestung sind jetzt editierbar.", MobLab::RaceName(t.race), t.cls);
					Say(b);
				}
				return 0;
			}
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				switch (id) {
					case MID_FILTER: if (code == EN_CHANGE) MobFillList(); break;
					case MID_LIST: if (code != LBN_DBLCLK) break;
						[[fallthrough]];
					case MID_SPAWN: {
						MobLab::SpawnOpts o = MobOpts();
						if (o.race < 0) { Say("Bitte links eine Rasse waehlen."); break; }
						if (o.race >= 200) {                                        // eigenes Tier: .mob laden und setzen
							const OwnMob* om = OwnByRace(o.race);
							MobLab::CustomMob mm; std::string err;
							if (!om || !MobLab::LoadMob(om->file, mm, err)) { Say("Eigenes Tier " + std::to_string(o.race) + ": " + (om ? err : std::string("nicht gefunden"))); break; }
							if (o.level > 0) mm.level = o.level;
							int n = (std::max)(1, (std::min)(20, o.count)), dist = o.distance;
							Say("Spawne " + std::to_string(n) + "x " + om->name + " (" + std::to_string(o.race) + ") ...");
							LabBridge::Post("Eigenes Tier", [mm, n, dist](cube::GameController* gc) { for (int i = 0; i < n; i++) { std::string r; MobLab::SpawnCustom(gc, mm, dist + i * 3, r); } });
							break;
						}
						Say(std::string("Spawne ") + MobLab::RaceName(o.race) + " ...");
						LabBridge::Post("Mob spawnen", [o](cube::GameController* gc) { std::string r; MobLab::Spawn(gc, o, r); });
						break;
					}
					case MID_GALLERY: case MID_GALLERY_NEXT: {
						int first = id == MID_GALLERY ? (MobSelectedRace() < 0 ? 0 : MobSelectedRace()) : g_galleryNext;
						if (first >= MobLab::kRaceCount) {                          // eigene Tiere: 12 ab der gewaehlten Nummer
							std::vector<MobLab::CustomMob> ms;
							for (const OwnMob& om : g_ownMobs) if (om.race >= first && ms.size() < 12) { MobLab::CustomMob mm; std::string err; if (MobLab::LoadMob(om.file, mm, err)) ms.push_back(mm); }
							g_galleryNext = first + 12;
							if (ms.empty()) { Say("Keine eigenen Tiere ab dieser Nummer."); break; }
							int dist = GetInt(m_dist, 6);
							LabBridge::Post("Galerie eigene Tiere", [ms, dist](cube::GameController* gc) { for (size_t i = 0; i < ms.size(); i++) { std::string r; MobLab::SpawnCustom(gc, ms[i], dist + (int)i * 5, r); } });
							break;
						}
						g_galleryNext = first + 12;
						MobLab::SpawnOpts o = MobOpts();
						LabBridge::Post("Mob-Galerie", [first, o](cube::GameController* gc) { std::string r; MobLab::SpawnGallery(gc, first, 12, o, r); });
						break;
					}
					case MID_DESCRIBE:
						LabBridge::Post("Mob auslesen", [](cube::GameController* gc) { std::cout << "[Mob] " << MobLab::DescribeLast(gc) << std::endl; });
						break;
					case MID_BASE: {
						int r = MobSelectedRace();
						if (r < 0) { Say("Bitte links eine Rasse waehlen."); break; }
						if (r >= 200) {                                             // eigenes Tier: zum Bearbeiten laden
							const OwnMob* om = OwnByRace(r); std::string err; MobLab::CustomMob mm;
							if (om && MobLab::LoadMob(om->file, mm, err)) { g_mob = mm; g_mobPart = 0; MobToUi(); EquipRefresh(); EquipShow(); Say("Eigenes Tier geladen: " + om->name + " (" + om->file + ")"); }
							else Say("Eigenes Tier nicht ladbar.");
							break;
						}
						g_mob.baseRace = r; MobRefreshParts();
						if (g_mob.useAppearance) Say(std::string("Basis: ") + MobLab::RaceName(r) + ". Farben/Aussehen bleiben von der geladenen Vorlage; Teile mit 'von der Basisrasse' nehmen Modell, Skala und Position der NEUEN Basis.");
						else
						Say(std::string("Basis fuer den eigenen Mob: ") + MobLab::RaceName(r) + " -- Skelett, Bewegung und alle Teile mit 'von der Basisrasse'.");
						break;
					}
					case MID_NEW: g_mob = MobLab::EmptyMob(); g_mobPart = 0; MobToUi(); EquipRefresh(); EquipShow(); Say("Neuer leerer Mob: alle Teile 'keins'. Basis HumanMale (humanoid)."); break;
					case MID_NATIVE: {
						int r = MobSelectedRace();
						if (r < 0) { Say("Bitte links eine Rasse waehlen."); break; }
						int cls = kClsVals[ComboSel(m_cls)];
						Say(std::string("Lese native Vorlage fuer ") + MobLab::RaceName(r) + " (Aussehen + Ausruestung, wie das Spiel sie erzeugt) ...");
						LabBridge::Post("Native Vorlage", [r, cls](cube::GameController* gc) { MobLab::RequestTemplate(gc, r, cls); });
						break;
					}
					case MID_EQ: if (code == LBN_SELCHANGE) { g_eqSlot = (int)SendMessageA(m_eq, LB_GETCURSEL, 0, 0); if (g_eqSlot < 0) g_eqSlot = 0; EquipShow(); } break;
					case MID_EQ_APPLY: {
						MobLab::EquipItem& e = g_mob.equip[g_eqSlot];
						e.set = true; e.category = GetInt(m_eCat, 0); e.id = GetInt(m_eId, 0); e.material = GetInt(m_eMat, 0);
						e.rarity = GetInt(m_eRar, 0); e.level = GetInt(m_eLvl, 1);
						EquipRefresh(); break;
					}
					case MID_EQ_EMPTY: { MobLab::EquipItem& e = g_mob.equip[g_eqSlot]; e.set = true; e.category = 0; EquipRefresh(); EquipShow(); break; }
					case MID_EQ_KEEP:  { g_mob.equip[g_eqSlot] = MobLab::EquipItem(); EquipRefresh(); EquipShow(); break; }
					case MID_PARTS: if (code == LBN_SELCHANGE) { g_mobPart = (int)SendMessageA(m_parts, LB_GETCURSEL, 0, 0); if (g_mobPart < 0) g_mobPart = 0; MobShowPart(); } break;
					case MID_PART_APPLY: MobApplyPart(); break;
					case MID_SRC:
						if (code == CBN_SELCHANGE) {                                   // Quelle sofort uebernehmen (frueher erst mit "Teil uebernehmen")
							MobApplyPart();
							if (ComboSel(m_src) == MobLab::SRC_BASE)
								Say(std::string(MobLab::PartName(g_mobPart)) + ": von der Basisrasse " + MobLab::RaceName(g_mob.baseRace) + " (Modell, Skala und Position der Basis).");
						}
						break;
					case MID_FILE: {
						char file[MAX_PATH] = ""; char dir[MAX_PATH] = "";
						GetFullPathNameA(Lab::StructuresDir().c_str(), MAX_PATH, dir, NULL);
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn));
						ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h; ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0";
						ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = dir;
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (GetOpenFileNameA(&ofn)) { SetWindowTextA(m_file, file); SendMessageA(m_src, CB_SETCURSEL, MobLab::SRC_FILE, 0); MobApplyPart(); }
						break;
					}
					case MID_KB_READ: { int r = MobSelectedRace(); if (r < 0) { Say("Links eine Rasse waehlen."); break; } KbRequest(r); break; }
					case MID_KB_BASE: MobFromUi(); if (g_mob.ownRace >= 200) KbRequestMob(g_mob); else KbRequest(g_mob.baseRace); break;
					case MID_KB_TEST: case MID_KB_SAVE: case MID_KB_RESET: {
						if (g_kbRace < 0) { Say("Erst eine Rasse lesen ('Auswahl links lesen')."); break; }
						int race = g_kbRace;
						MobLab::BodyVals v = id == MID_KB_RESET ? MobLab::BodyVals() : KbFromUi();
						MobLab::SetBodyDefLive(race, v);
						if (id == MID_KB_SAVE || id == MID_KB_RESET) {
							std::string err;
							if (!MobLab::SaveBodyDefs(err)) { Say("Koerperbau speichern: " + err); break; }
							Say(std::string("Koerperbau ") + MobLab::RaceName(race) + (id == MID_KB_RESET ? " auf nativ zurueckgesetzt" : " gespeichert") + " (" + MobLab::BodyFilePath() + "). Gilt fuer alle neuen Kreaturen dieser Rasse.");
							if (id == MID_KB_RESET) KbRequest(race);
							break;
						}
						int dist = GetInt(m_dist, 6);
						if (race >= 200 && g_kbIsMob) {                            // eigenes Tier: den Mob selbst spawnen
							MobLab::CustomMob mm = g_kbMob;
							LabBridge::Post("Koerperbau testen", [mm, dist](cube::GameController* gc) { std::string r; MobLab::SpawnCustom(gc, mm, dist, r); LabBridge::Log("Koerperbau-Test: " + r); });
							Say("Werte gesetzt (noch nicht gespeichert) -- Test-Spawn angefordert.");
							break;
						}
						LabBridge::Post("Koerperbau testen", [race, dist](cube::GameController* gc) {
							if (!gc || !gc->local_player) return;
							int64_t x = gc->local_player->entity_data.position.X, y = gc->local_player->entity_data.position.Y, z = gc->local_player->entity_data.position.Z;
							uint64_t g = MobLab::SpawnRaceAt(gc, race, 1, MobLab::PlayerLevel(gc), x + (int64_t)dist * 65536, y, z + 2LL * 65536, false, false, 1.0f);
							if (g) MobLab::SetPeaceful(gc, g);
							LabBridge::Log(std::string("Koerperbau-Test: ") + MobLab::RaceName(race) + (g ? " gespawnt (steht still, friedlich)." : " Spawn fehlgeschlagen."));
						});
						Say("Werte gesetzt (noch nicht gespeichert) -- Test-Spawn angefordert.");
						break;
					}
					case MID_EDIT_PART: {
						// Grundmodell des gewaehlten Teils in den Bau-Editor holen: eigene .cub direkt, natives Modell bzw. das Modell
						// der Basisrasse aus der Registry (Spielthread). Nach dem Bearbeiten: "Geaendertes uebernehmen" speichert es als
						// Teile\<mob>-<teil>.cub und setzt das Teil auf diese Datei (das Original bleibt unveraendert).
						MobApplyPart(); MobFromUi();
						const MobLab::Part p = g_mob.parts[g_mobPart];
						std::string pn = MobLab::PartName(g_mobPart), mobName = g_mob.name.empty() ? std::string("mob") : g_mob.name;
						if (p.source == MobLab::SRC_NONE) { Say(pn + ": Quelle 'keins' -- erst eine Quelle waehlen (z.B. 'von der Basisrasse')."); break; }
						if (!ConfirmDiscard()) break;
						if (p.source == MobLab::SRC_FILE) {
							std::string path = p.file;
							if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES && !g_mob.baseDir.empty()) path = g_mob.baseDir + "\\" + p.file;
							Lab::Structure st; std::string err;
							if (!Lab::LoadCub(path, st, err)) { Say("Teil laden fehlgeschlagen: " + err); break; }
							st.name = mobName + "-" + pn; st.origin = "mob-teil: " + path;
							SetCurrent(st, true); ShowMainTab(0);
							Say(pn + " (" + path + ") im Editor. Aendern, dann im Mob-Editor 'Geaendertes uebernehmen'.");
							break;
						}
						int race = g_mob.baseRace, part = g_mobPart, idx = p.nativeIndex; bool base = p.source == MobLab::SRC_BASE;
						std::string nm = mobName + "-" + pn;
						Say("Hole " + pn + (base ? std::string(" der Basisrasse ") + MobLab::RaceName(race) : std::string(" (natives Modell)")) + " in den Editor ...");
						LabBridge::Post("Mob-Teil bearbeiten", [race, part, idx, base, nm](cube::GameController* gc) {
							int id = base ? MobLab::BasePartModel(race, part) : idx;
							if (id < 0) { LabBridge::Log("Dieses Teil hat bei der Basisrasse kein Modell."); return; }
							Lab::Structure s; std::string e;
							if (!LabBridge::ImportNative(gc, id, s, e)) { LabBridge::Log("Modell 0x" + [](int v) { char b[16]; sprintf_s(b, "%X", v); return std::string(b); }(id) + " laden fehlgeschlagen: " + e); return; }
							s.name = nm; s.origin = "mob-teil: nativ"; LabBridge::PostStructure(s);
							LabBridge::Log("Modell im Editor (Reiter 'Bauen'). Aendern, dann im Mob-Editor 'Geaendertes uebernehmen'.");
						});
						ShowMainTab(0);
						break;
					}
					case MID_FROM_EDITOR: {
						// Die offene Struktur des LAB-Editors als Teil verwenden (z.B. selbst gebaute Fluegel).
						if (!g_cur.Valid()) { Say("Im Editor ist keine Struktur offen."); break; }
						MobFromUi();
						std::string dir = MobLab::MobsDir() + "\\Teile";
						Lab::EnsureDir(dir);
						std::string path = dir + "\\" + g_mob.name + "-" + MobLab::PartName(g_mobPart) + ".cub", err;
						if (!Lab::SaveCub(path, g_cur, err)) { Say("Teil speichern fehlgeschlagen: " + err); break; }
						SetWindowTextA(m_file, path.c_str()); SendMessageA(m_src, CB_SETCURSEL, MobLab::SRC_FILE, 0); MobApplyPart();
						Say("Editor-Struktur '" + g_cur.name + "' als " + MobLab::PartName(g_mobPart) + " uebernommen: " + path);
						break;
					}
					case MID_SAVE: {
						MobFromUi();
						std::string path = MobLab::MobsDir() + "\\" + g_mob.name + ".mob", err;
						if (MobLab::SaveMob(path, g_mob, err)) { g_mob.baseDir = MobLab::MobsDir(); Say("Mob gespeichert: " + path + " (Teile relativ zu diesem Ordner)"); } else Say("Speichern fehlgeschlagen: " + err);
						break;
					}
					case MID_LOAD: {
						char file[MAX_PATH] = ""; char dir[MAX_PATH] = "";
						Lab::EnsureDir(MobLab::MobsDir());
						GetFullPathNameA(MobLab::MobsDir().c_str(), MAX_PATH, dir, NULL);
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn));
						ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h; ofn.lpstrFilter = "Eigene Mobs (*.mob)\0*.mob\0";
						ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = dir;
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (!GetOpenFileNameA(&ofn)) break;
						std::string err; MobLab::CustomMob mm;
						if (MobLab::LoadMob(file, mm, err)) { g_mob = mm; g_mobPart = 0; MobToUi(); EquipRefresh(); EquipShow(); Say("Mob geladen: " + g_mob.name); }
						else Say("Laden fehlgeschlagen: " + err);
						break;
					}
					case MID_SPAWN_CUSTOM: {
						MobFromUi();
						MobLab::CustomMob mm = g_mob; int dist = GetInt(m_dist, 6);
						Say("Spawne eigenen Mob '" + mm.name + "' ...");
						LabBridge::Post("Eigener Mob", [mm, dist](cube::GameController* gc) { std::string r; MobLab::SpawnCustom(gc, mm, dist, r); });
						break;
					}
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}


	// =====================================================================
	// 7d. Unterfenster "Biom-Werkstatt"  (BiomeMod / BiomeGen)
	// =====================================================================
	// Links: alle Biome (Dateien in Strukturen\Biome) + Zustand im Spiel.
	// Mitte: Auswahl/Klima, Boden, Kreaturen.  Rechts daneben: Pflanzen, Dungeons.
	// Ganz rechts: native Orte im Biom ein/aus (Dorf, Ruinen, Portal, Dungeons) + Bergdorf.
	namespace {
		HWND g_bioWnd = nullptr;
		HWND g_bParent = nullptr;                // != null: B-Helfer bauen in dieses Fenster (Zonen-Modelle)
		HWND b_list, b_status, b_name, b_enabled, b_share, b_climate, b_setClim, b_temp, b_hum;
		HWND b_g1[3], b_g2[3], b_rock[3], b_gtype, b_strength, b_rockOn;
		HWND b_races, b_raceShare, b_racePick, b_mobs, b_mobMax, b_mobLvl, b_lvlMode, b_lvlMin, b_lvlMax, b_lvlOff, b_spRaces;
		HWND b_flora, b_pick, b_flW;                        // Listen "Baeume & Pflanzen" / "Sammelbare Pflanzen", Gewicht
		// Zeile einer Abhakliste. kind: 0 prozedurale Art (idx), 1 native Baeume, 2 eigene .cub, 3 Zonen-Modell, 9 freier Platz
		struct FloraRow { int kind; int idx; std::string file; std::string label; bool on; int weight; bool deflt; };
		std::vector<FloraRow> g_flora, g_pick;
		bool g_floraFilling = false;
		int  g_fileWeight = 2;
		HWND b_density, b_pw[BiomeGen::PK_COUNT], b_trunk[3], b_leaf[3], b_acc[3], b_psize, b_pfiles, b_pfw, b_kind;
		HWND b_dChance, b_dRooms, b_wall[3], b_floor[3], b_dacc[3], b_roof, b_decay, b_dRaces, b_dMobs, b_dCount, b_boss;
		HWND b_site[BiomeGen::kSiteKindCount], b_bergdorf, b_allOn;
		HWND b_tOn, b_tBase, b_tWaves, b_tWaveSize, b_tHillShare, b_tHillH, b_tHillSize, b_tFlat, b_tMount, b_tTrees;
		HWND b_rNative, b_rWidth, b_rOwn, b_rSpacing, b_lShare, b_lSize;
		HWND b_gH, b_gW, b_gSp, b_gRough, b_snow;
		HWND b_pOn, b_pRadius, b_pRim, b_pHeight;          // freie Ebene in der Zellmitte
		HWND b_city, b_cityR;                              // Stadt auf der Ebene (City.cpp)
		HWND b_cityAnimals, b_animalPick;                  // Tierviertel
		HWND b_sOn, b_sArms, b_sRad, b_sRing, b_sRidge, b_sValley, b_sLake;   // Sternmassiv
		HWND b_stTrees, b_stTreeSize;                       // grosse Hangbaeume
		HWND b_mShare, b_mGrid, b_mW, b_mH, b_mGal, b_mShaft, b_mWood[3], b_mLight[3], b_mRail[3];
		std::string g_bioFile;                   // aktuell bearbeitete Datei (nur Name)
		enum { BID_LIST = 5001, BID_NEW, BID_EX0, BID_EX1, BID_EX2, BID_EX3, BID_SAVE, BID_DELETE, BID_RELOAD, BID_NEAREST, BID_TELEPORT, BID_FORCE, BID_UNFORCE,
		       BID_ALL, BID_RACE_ADD, BID_MOB_ADD, BID_PFILE_ADD, BID_PLANT_EDIT, BID_PLANT_SET, BID_DUN_EDIT, BID_DUN_SET,
		       BID_DMOB_ADD, BID_SITES_DUN_OFF, BID_SITES_ALL_ON, BID_EX4, BID_CITYEXP,	BID_FLORA, BID_PICK, BID_FLW_SET, BID_LVLMODE, BID_SITES_NEAR, BID_ANIMAL_ADD,
		BID_PAGE1, BID_PAGE2, BID2_RULES, BID2_ADD, BID2_SET, BID2_DEL, BID2_TEST, BID2_LVMODE, BID2_ALLGROUPS,
		BID_COL_FIRST = 5100, BID_TIMER = 11 };
		// Farbknoepfe: Index -> die drei Felder
		std::vector<HWND*> g_colRows;
		// Biome I / Biome II (2026-10-16): Steuerelemente je Seite (0 = immer, 1 = Geografie, 2 = Mobs)
		int  g_bioGroup = 0, g_bioPage = 1, g_b2Y = 10;
		std::vector<HWND> g_bioGrp[3];
		bool g_bioNative = false;                   // bearbeitet wird die native Welt (Strukturen\Biome\_nativ.tiere)
		int  g_bioNC = 0;                           // > 0: bearbeitet wird ein natives Biom (Datei Nativ_<Klasse>.biom, nativ=<Klasse>)
		HWND b_page1, b_page2, b2_title, b2_rules, b2_what, b2_where, b2_weight, b2_lvMin, b2_lvMax, b2_behave, b2_share,
		     b2_wild, b2_behAll, b2_lvMode, b2_lvMin2, b2_lvMax2, b2_lvOff;
		std::vector<BiomeGen::SpawnRule> g_rules2;
		std::vector<std::string> g_whatKeys;        // parallel zur Auswahl "Was"
		bool g_rulesFilling = false;
		const int kWildVals[7] = { -1, 0, 10, 25, 50, 75, 100 };
	}

	static HWND BCtl(const char* cls, const char* t, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
		HWND c = CreateWindowExA(ex, cls, t, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_bParent ? g_bParent : g_bioWnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
		SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
		if (!g_bParent && g_bioGroup >= 0 && g_bioGroup < 3) g_bioGrp[g_bioGroup].push_back(c);
		return c;
	}
	static void BLbl(const char* t, int x, int y, int w, bool bold = false) {
		HWND c = BCtl("STATIC", t, SS_LEFT, x, y, w, 16, -1);
		if (bold) SendMessageA(c, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
	}
	static HWND BEdt(int x, int y, int w, const char* t = "") { return BCtl("EDIT", t, ES_AUTOHSCROLL, x, y, w, 22, -1, WS_EX_CLIENTEDGE); }
	static HWND BChk(const char* t, int x, int y, int w) { return BCtl("BUTTON", t, BS_AUTOCHECKBOX, x, y, w, 20, -1); }
	static HWND BBtn(const char* t, int x, int y, int w, int id, int h = 24) { return BCtl("BUTTON", t, BS_PUSHBUTTON, x, y, w, h, id); }
	static void BSetChk(HWND h, bool v) { SendMessageA(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }
	static float BFloat(HWND h, float def) { std::string t = GetText(h); return t.empty() ? def : (float)atof(t.c_str()); }
	static void BSetF(HWND h, float v) { char b[32]; sprintf_s(b, "%.2f", v); SetWindowTextA(h, b); }

	// Beschriftung + drei RGB-Felder + "..."-Farbwahl
	static void BRgbRow(const char* label, int x, int y, HWND* e) {
		BLbl(label, x, y + 4, 92);
		for (int i = 0; i < 3; i++) e[i] = BEdt(x + 94 + i * 42, y, 38);
		BBtn("...", x + 94 + 3 * 42, y, 28, BID_COL_FIRST + (int)g_colRows.size(), 22);
		g_colRows.push_back(e);
	}
	static void BSetRGB(HWND* e, BiomeGen::RGB c) { SetInt(e[0], c.r); SetInt(e[1], c.g); SetInt(e[2], c.b); }
	static BiomeGen::RGB BGetRGB(HWND* e) {
		auto c = [](HWND h) { int v = GetInt(h, 0); return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
		BiomeGen::RGB r; r.r = c(e[0]); r.g = c(e[1]); r.b = c(e[2]); return r;
	}

	static void ShowMainTab(int k);
	static void Bio2ToUi(const BiomeGen::BiomeDef& d); static void Bio2FromUi(BiomeGen::BiomeDef& d);
	static void BioShowPage(int p); static void BioTitle();
	static bool BioPickFile(const std::string& dir, const char* filter, std::string& outName);
	static const int TAB_ZMOD_FWD = 3;                  // = TAB_ZMOD (Aufzaehlung steht weiter unten, static_assert dort)
	static void BioLvlEnable() {
		bool fixed = ComboSel(b_lvlMode) == 1;
		EnableWindow(b_lvlMin, fixed); EnableWindow(b_lvlMax, fixed); EnableWindow(b_lvlOff, !fixed);
	}
	// ---- Abhaklisten "Baeume & Pflanzen" und "Sammelbare Pflanzen" ------------------------------------------
	// Jede Liste hat unten immer einen freien Platz ("+ ... hinzufuegen"). Zonen-Modelle: Haken = in diesem Biom;
	// weicht er vom Bereich des Zonen-Modells ab, steht das Modell in zonenmodelle_an/_aus der .biom-Datei.
	static bool BioZmDefault(const BiomeGen::ZoneModelDef& z, const std::string& biomFile) {
		if (z.area == "ueberall") return true;
		if (z.area.compare(0, 5, "biom:") == 0) return _stricmp(z.area.c_str() + 5, biomFile.c_str()) == 0;
		return false;                                                       // "vanilla"
	}
	static bool BioInList(const std::vector<std::string>& v, const std::string& f) {
		for (const std::string& n : v) if (_stricmp(n.c_str(), f.c_str()) == 0) return true;
		return false;
	}
	static std::string BioFileName() { return g_bioFile.empty() ? Lab::SafeFileName(GetText(b_name)) + ".biom" : g_bioFile; }
	static void BioListShow(HWND lv, const std::vector<FloraRow>& rows) {
		g_floraFilling = true;
		ListView_DeleteAllItems(lv);
		for (size_t i = 0; i < rows.size(); i++) {
			const FloraRow& r = rows[i];
			static const char* kn[] = { "prozedural", "nativ", ".cub-Datei", "Zonen-Modell" };
			LVITEMA it; memset(&it, 0, sizeof(it)); it.mask = LVIF_TEXT; it.iItem = (int)i;
			std::string a = r.kind == 9 ? "" : kn[r.kind < 4 ? r.kind : 0];
			it.pszText = (LPSTR)a.c_str();
			int n = (int)SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
			LVITEMA sub; memset(&sub, 0, sizeof(sub)); sub.iSubItem = 1; sub.pszText = (LPSTR)r.label.c_str();
			SendMessageA(lv, LVM_SETITEMTEXTA, n, (LPARAM)&sub);
			std::string w = r.kind == 9 ? "" : r.kind == 3 ? "(Dichte)" : r.kind == 1 ? std::to_string(r.weight) + " %" : std::to_string(r.weight);
			sub.iSubItem = 2; sub.pszText = (LPSTR)w.c_str(); SendMessageA(lv, LVM_SETITEMTEXTA, n, (LPARAM)&sub);
			if (r.kind == 9) { ListView_SetItemState(lv, n, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK); }   // freier Platz: kein Haken
			else { ListView_SetCheckState(lv, n, r.on ? TRUE : FALSE); }
		}
		g_floraFilling = false;
	}
	static void BioFloraLoad(const BiomeGen::BiomeDef& d) {
		g_flora.clear(); g_pick.clear();
		for (int i = 0; i < BiomeGen::PK_COUNT; i++)
			g_flora.push_back({ 0, i, "", BiomeGen::PlantKindName(i), d.plantWeight[i] > 0, d.plantWeight[i] > 0 ? d.plantWeight[i] : 2, false });
		g_flora.push_back({ 1, 0, "", "Native Baeume des Spiels (Anteil, der bleibt)", d.nativeTrees > 0, d.nativeTrees > 0 ? d.nativeTrees : 100, false });
		g_fileWeight = d.fileWeight;
		for (const std::string& f : d.plantFiles)    g_flora.push_back({ 2, 0, f, f, true,  d.fileWeight, false });
		for (const std::string& f : d.plantFilesOff) g_flora.push_back({ 2, 0, f, f, false, d.fileWeight, false });
		std::string bf = g_bioFile.empty() ? d.name + ".biom" : g_bioFile;
		for (const std::string& f : BiomeGen::ListZoneModelFiles()) {
			BiomeGen::ZoneModelDef z; std::string err;
			if (!BiomeGen::LoadZoneModel(BiomeGen::ZoneModelsDir() + "\\" + f, z, err) || z.kind == BiomeGen::ZM_DUNGEON) continue;
			bool def = BioZmDefault(z, bf);
			bool on = BioInList(d.zoneModelsOn, f) ? true : BioInList(d.zoneModelsOff, f) ? false : def;
			std::string label = f + "  (" + BiomeGen::ZoneModelKindName(z.kind) + (z.kind == BiomeGen::ZM_PICKUP && !z.itemName.empty() ? ": " + z.itemName : "") +
			                    (z.enabled ? "" : ", Modell aus") + ")";
			FloraRow r{ 3, z.kind, f, label, on, z.density, def };
			(z.kind == BiomeGen::ZM_PICKUP ? g_pick : g_flora).push_back(r);
		}
		g_flora.push_back({ 9, 0, "", "+  Baum / Pflanze hinzufuegen ...", false, 0, false });
		g_pick.push_back({ 9, 0, "", "+  Sammelbare Pflanze hinzufuegen ...", false, 0, false });
		BioListShow(b_flora, g_flora); BioListShow(b_pick, g_pick);
		SetWindowTextA(b_flW, "");
	}
	static void BioFloraStore(BiomeGen::BiomeDef& d) {
		for (int i = 0; i < BiomeGen::PK_COUNT; i++) d.plantWeight[i] = 0;
		d.plantFiles.clear(); d.plantFilesOff.clear(); d.zoneModelsOn.clear(); d.zoneModelsOff.clear();
		d.fileWeight = (std::max)(0, (std::min)(20, g_fileWeight));
		d.nativeTrees = 0;
		for (int pass = 0; pass < 2; pass++)
			for (const FloraRow& r : pass ? g_pick : g_flora) {
				if (r.kind == 0) d.plantWeight[r.idx] = r.on ? (std::max)(1, (std::min)(20, r.weight)) : 0;
				else if (r.kind == 1) d.nativeTrees = r.on ? (std::max)(1, (std::min)(100, r.weight)) : 0;
				else if (r.kind == 2) (r.on ? d.plantFiles : d.plantFilesOff).push_back(r.file);
				else if (r.kind == 3 && r.on != r.deflt) (r.on ? d.zoneModelsOn : d.zoneModelsOff).push_back(r.file);
			}
	}
	static std::vector<FloraRow>* BioRowsOf(HWND lv) { return lv == b_flora ? &g_flora : lv == b_pick ? &g_pick : nullptr; }
	static void BioFloraAdd(HWND lv) {
		HMENU m = CreatePopupMenu();
		if (lv == b_flora) {
			AppendMenuA(m, MF_STRING, 1, "Eigene .cub-Datei waehlen ...");
			AppendMenuA(m, MF_STRING, 2, "Neues Zonen-Modell (Baum / Blume) anlegen -> Reiter 'Zonen-Modelle'");
		} else AppendMenuA(m, MF_STRING, 3, "Neue sammelbare Pflanze anlegen -> Reiter 'Zonen-Modelle' (Art 'Sammelbar')");
		POINT pt; GetCursorPos(&pt);
		int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_bioWnd, NULL);
		DestroyMenu(m);
		if (c == 1) {
			std::string n;
			if (!BioPickFile(Lab::StructuresDir(), "Struktur (*.cub)\0*.cub\0", n)) return;
			for (const FloraRow& r : g_flora) if (r.kind == 2 && _stricmp(r.file.c_str(), n.c_str()) == 0) { Say("Steht schon in der Liste."); return; }
			g_flora.insert(g_flora.end() - 1, FloraRow{ 2, 0, n, n, true, g_fileWeight, false });
			BioListShow(b_flora, g_flora);
			Say("'" + n + "' hinzugefuegt -- 'Speichern + laden' nicht vergessen.");
		} else if (c == 2 || c == 3) {
			ShowMainTab(TAB_ZMOD_FWD);
			Say(c == 2 ? "Zonen-Modell anlegen (Art Baum oder Blume) und speichern. Danach steht es in der Biom-Liste (Biom neu waehlen)."
			           : "Zonen-Modell mit Art 'Sammelbar' anlegen und speichern. Danach steht es unter 'Sammelbare Pflanzen' (Biom neu waehlen).");
		}
	}
	static void BioFloraNotify(NMHDR* nh) {
		std::vector<FloraRow>* rows = BioRowsOf(nh->hwndFrom);
		if (!rows) return;
		if (nh->code == LVN_ITEMCHANGED && !g_floraFilling) {
			NMLISTVIEW* nl = (NMLISTVIEW*)nh;
			if (nl->iItem < 0 || nl->iItem >= (int)rows->size()) return;
			FloraRow& r = (*rows)[nl->iItem];
			if ((nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_STATEIMAGEMASK) && r.kind != 9)
				r.on = ListView_GetCheckState(nh->hwndFrom, nl->iItem) != 0;
			if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED))
				SetWindowTextA(b_flW, (r.kind == 0 || r.kind == 1 || r.kind == 2) ? std::to_string(r.weight).c_str() : "");
		}
		if (nh->code == NM_CLICK || nh->code == NM_DBLCLK) {
			NMITEMACTIVATE* na = (NMITEMACTIVATE*)nh;
			if (na->iItem >= 0 && na->iItem < (int)rows->size() && (*rows)[na->iItem].kind == 9) BioFloraAdd(nh->hwndFrom);
		}
	}
	static void BioFloraSetWeight() {
		int i = ListView_GetNextItem(b_flora, -1, LVNI_SELECTED);
		if (i < 0 || i >= (int)g_flora.size()) { Say("Erst eine Zeile in 'Baeume & Pflanzen' waehlen."); return; }
		FloraRow& r = g_flora[i];
		int w = GetInt(b_flW, r.weight);
		if (r.kind == 0) r.weight = (std::max)(1, (std::min)(20, w));
		else if (r.kind == 1) r.weight = (std::max)(1, (std::min)(100, w));
		else if (r.kind == 2) { g_fileWeight = (std::max)(0, (std::min)(20, w)); for (FloraRow& q : g_flora) if (q.kind == 2) q.weight = g_fileWeight; }
		else { Say("Zonen-Modelle: Dichte im Reiter 'Zonen-Modelle' einstellen."); return; }
		BioListShow(b_flora, g_flora);
		{ ListView_SetItemState(b_flora, i, LVIS_SELECTED, LVIS_SELECTED); }
	}
	static HWND BioMakeList(int x, int y, int w, int h, int id) {
		HWND lv = BCtl(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, x, y, w, h, id, WS_EX_CLIENTEDGE);
		ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
		LVCOLUMNA c; memset(&c, 0, sizeof(c)); c.mask = LVCF_TEXT | LVCF_WIDTH;
		c.cx = 84;  c.pszText = (LPSTR)"Art";     SendMessageA(lv, LVM_INSERTCOLUMNA, 0, (LPARAM)&c);
		c.cx = w - 84 - 70 - 22; c.pszText = (LPSTR)"Name"; SendMessageA(lv, LVM_INSERTCOLUMNA, 1, (LPARAM)&c);
		c.cx = 70;  c.pszText = (LPSTR)"Gewicht"; SendMessageA(lv, LVM_INSERTCOLUMNA, 2, (LPARAM)&c);
		return lv;
	}
	static void BioToUi(const BiomeGen::BiomeDef& d) {
		SetWindowTextA(b_name, d.name.c_str()); BSetChk(b_enabled, d.enabled);
		SetInt(b_share, d.share); SendMessageA(b_climate, CB_SETCURSEL, d.climateReq, 0);
		BSetChk(b_setClim, d.setClimate); BSetF(b_temp, d.temp); BSetF(b_hum, d.hum);
		BSetRGB(b_g1, d.ground); BSetRGB(b_g2, d.ground2); BSetRGB(b_rock, d.rock);
		int gt = d.groundType == 4 ? 1 : d.groundType == 9 ? 2 : d.groundType == 10 ? 3 : d.groundType == 12 ? 4 : 0;
		SendMessageA(b_gtype, CB_SETCURSEL, gt, 0); BSetF(b_strength, d.strength); BSetChk(b_rockOn, d.rockOn);
		SetWindowTextA(b_races, BiomeGen::JoinInts(d.races).c_str()); BSetF(b_raceShare, d.raceShare);
		SetWindowTextA(b_mobs, BiomeGen::JoinNames(d.mobFiles).c_str()); SetInt(b_mobMax, d.mobMax);
		SendMessageA(b_lvlMode, CB_SETCURSEL, d.mobLevelMode, 0); SetInt(b_lvlMin, d.mobLevelMin); SetInt(b_lvlMax, d.mobLevelMax);
		SetInt(b_lvlOff, d.mobLevelOffset); BSetChk(b_spRaces, d.spawnerRaces); BioLvlEnable();
		SetInt(b_density, d.plantDensity);
		BSetRGB(b_trunk, d.trunk); BSetRGB(b_leaf, d.leaf); BSetRGB(b_acc, d.accent);
		BSetF(b_psize, d.plantSize);
		BioFloraLoad(d);
		SetInt(b_dChance, d.dungeonChance); SetInt(b_dRooms, d.dungeonRooms);
		BSetRGB(b_wall, d.wall); BSetRGB(b_floor, d.floor); BSetRGB(b_dacc, d.dAccent);
		BSetChk(b_roof, d.dungeonRoof); BSetF(b_decay, d.decay);
		SetWindowTextA(b_dRaces, BiomeGen::JoinInts(d.dungeonRaces).c_str());
		SetWindowTextA(b_dMobs, BiomeGen::JoinNames(d.dungeonMobFiles).c_str());
		SetInt(b_dCount, d.dungeonMobs); BSetChk(b_boss, d.dungeonBoss);
		for (int i = 0; i < BiomeGen::kSiteKindCount; i++) BSetChk(b_site[i], (d.siteMask & (1u << BiomeGen::kSiteKinds[i])) != 0);
		BSetChk(b_bergdorf, d.bergdorf);
		BSetChk(b_tOn, d.terrainOn); BSetF(b_tBase, d.baseHeight); BSetF(b_tWaves, d.waves); BSetF(b_tWaveSize, d.waveSize);
		BSetF(b_tFlat, d.flatten); BSetF(b_tHillShare, d.hillShare); BSetF(b_tHillH, d.hillHeight); BSetF(b_tHillSize, d.hillSize);
		BSetF(b_tMount, d.mountains);
		BSetChk(b_rNative, d.riversNative); BSetF(b_rWidth, d.riverWidth); BSetChk(b_rOwn, d.riversOwn);
		SetInt(b_rSpacing, (int)d.riverSpacing); BSetF(b_lShare, d.lakeShare); SetInt(b_lSize, (int)d.lakeSize);
		SetInt(b_gH, (int)d.ridgeHeight); SetInt(b_gW, (int)d.ridgeWidth); SetInt(b_gSp, (int)d.ridgeSpacing);
		BSetF(b_gRough, d.ridgeRough); SetInt(b_snow, (int)d.snowLine);
		BSetChk(b_pOn, d.plainOn); SetInt(b_pRadius, (int)d.plainRadius); SetInt(b_pRim, (int)d.plainRim); SetInt(b_pHeight, (int)d.plainHeight);
		SendMessageA(b_city, CB_SETCURSEL, (d.city >= 0 && d.city <= 3) ? d.city : 0, 0); SetInt(b_cityR, (int)d.cityRadius);
		SetWindowTextA(b_cityAnimals, BiomeGen::JoinInts(d.cityAnimals).c_str());
		BSetChk(b_sOn, d.starOn); SetInt(b_sArms, d.starArms); SetInt(b_sRad, (int)d.starRadius); SetInt(b_sRing, (int)d.starRingHeight);
		SetInt(b_sRidge, (int)d.starRidgeHeight); SetInt(b_sValley, (int)d.starValleyWidth); SetInt(b_sLake, (int)d.starLakeSize);
		SetInt(b_stTrees, d.slopeTrees); BSetF(b_stTreeSize, d.slopeTreeSize);
		BSetF(b_mShare, d.mineShare); SetInt(b_mGrid, (int)d.mineGrid); SetInt(b_mW, d.tunnelWidth); SetInt(b_mH, d.tunnelHeight);
		SetInt(b_mGal, d.mineGalleries); BSetChk(b_mShaft, d.mineShafts);
		BSetRGB(b_mWood, d.mineWood); BSetRGB(b_mLight, d.mineLight); BSetRGB(b_mRail, d.mineRail);
		Bio2ToUi(d);
	}
	static BiomeGen::BiomeDef BioFromUi() {
		BiomeGen::BiomeDef d;
		d.name = Lab::SafeFileName(GetText(b_name)); if (d.name.empty()) d.name = "neues-biom";
		d.enabled = Checked(b_enabled);
		d.share = (std::max)(0, (std::min)(100, GetInt(b_share, 10)));
		d.climateReq = ComboSel(b_climate);
		d.setClimate = Checked(b_setClim);
		d.temp = (std::max)(0.0f, (std::min)(1.0f, BFloat(b_temp, 0.5f))); d.hum = (std::max)(0.0f, (std::min)(1.0f, BFloat(b_hum, 0.5f)));
		d.ground = BGetRGB(b_g1); d.ground2 = BGetRGB(b_g2); d.rock = BGetRGB(b_rock);
		const int gts[5] = { 0, 4, 9, 10, 12 }; d.groundType = gts[ComboSel(b_gtype) % 5];
		d.strength = (std::max)(0.0f, (std::min)(1.0f, BFloat(b_strength, 1.0f))); d.rockOn = Checked(b_rockOn);
		d.races = BiomeGen::ParseIntList(GetText(b_races)); d.raceShare = (std::max)(0.0f, (std::min)(1.0f, BFloat(b_raceShare, 0.7f)));
		d.mobFiles = BiomeGen::ParseNameList(GetText(b_mobs));
		d.mobMax = (std::max)(0, (std::min)(30, GetInt(b_mobMax, 6)));
		d.mobLevelMode = ComboSel(b_lvlMode) == 1 ? 1 : 0;
		d.mobLevelMin = (std::max)(1, GetInt(b_lvlMin, 1)); d.mobLevelMax = (std::max)(1, GetInt(b_lvlMax, 10));
		if (d.mobLevelMax < d.mobLevelMin) std::swap(d.mobLevelMin, d.mobLevelMax);
		d.mobLevelOffset = (std::max)(0, GetInt(b_lvlOff, 0));
		d.mobLevel = d.mobLevelMode == 1 ? d.mobLevelMin : -1;
		d.spawnerRaces = Checked(b_spRaces);
		d.plantDensity = (std::max)(0, (std::min)(300, GetInt(b_density, 40)));
		d.trunk = BGetRGB(b_trunk); d.leaf = BGetRGB(b_leaf); d.accent = BGetRGB(b_acc);
		d.plantSize = (std::max)(0.5f, (std::min)(2.5f, BFloat(b_psize, 1.0f)));
		BioFloraStore(d);
		d.dungeonChance = (std::max)(0, (std::min)(100, GetInt(b_dChance, 6))); d.dungeonRooms = (std::max)(2, (std::min)(6, GetInt(b_dRooms, 4)));
		d.wall = BGetRGB(b_wall); d.floor = BGetRGB(b_floor); d.dAccent = BGetRGB(b_dacc);
		d.dungeonRoof = Checked(b_roof); d.decay = (std::max)(0.0f, (std::min)(1.0f, BFloat(b_decay, 0.3f)));
		d.dungeonRaces = BiomeGen::ParseIntList(GetText(b_dRaces));
		d.dungeonMobFiles = BiomeGen::ParseNameList(GetText(b_dMobs));
		d.dungeonMobs = (std::max)(0, (std::min)(24, GetInt(b_dCount, 6))); d.dungeonBoss = Checked(b_boss);
		d.siteMask = 1u;
		for (int i = 0; i < BiomeGen::kSiteKindCount; i++) if (Checked(b_site[i])) d.siteMask |= 1u << BiomeGen::kSiteKinds[i];
		d.bergdorf = Checked(b_bergdorf);
		auto cl = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
		d.terrainOn = Checked(b_tOn);
		d.baseHeight = cl(BFloat(b_tBase, 8), -60, 200); d.waves = cl(BFloat(b_tWaves, 3), 0, 40); d.waveSize = cl(BFloat(b_tWaveSize, 30), 4, 400);
		d.flatten = cl(BFloat(b_tFlat, 0.9f), 0, 1); d.hillShare = cl(BFloat(b_tHillShare, 0.15f), 0, 1);
		d.hillHeight = cl(BFloat(b_tHillH, 18), -80, 200); d.hillSize = cl(BFloat(b_tHillSize, 90), 10, 1000);
		d.mountains = cl(BFloat(b_tMount, 0.2f), 0, 1);
		d.riversNative = Checked(b_rNative); d.riverWidth = cl(BFloat(b_rWidth, 1.0f), 0.5f, 4.0f);
		d.riversOwn = Checked(b_rOwn); d.riverSpacing = cl(BFloat(b_rSpacing, 600), 80, 5000);
		d.lakeShare = cl(BFloat(b_lShare, 0), 0, 1); d.lakeSize = cl(BFloat(b_lSize, 60), 12, 400);
		d.ridgeHeight = cl(BFloat(b_gH, 0), 0, 400); d.ridgeWidth = cl(BFloat(b_gW, 250), 40, 1000);
		d.ridgeSpacing = cl(BFloat(b_gSp, 1400), 300, 8000); d.ridgeRough = cl(BFloat(b_gRough, 0.5f), 0, 1);
		d.snowLine = cl(BFloat(b_snow, 0), 0, 600);
		d.plainOn = Checked(b_pOn); d.plainRadius = cl(BFloat(b_pRadius, 1200), 100, 6000);
		d.plainRim = cl(BFloat(b_pRim, 250), 20, 2000); d.plainHeight = cl(BFloat(b_pHeight, 20), 2, 600);
		{ int cs = (int)SendMessageA(b_city, CB_GETCURSEL, 0, 0); d.city = (cs < 0 || cs > 3) ? 0 : cs; }
		d.cityRadius = cl(BFloat(b_cityR, 0), 0, 6000);
		d.cityAnimals = BiomeGen::ParseIntList(GetText(b_cityAnimals));
		d.starOn = Checked(b_sOn); d.starArms = (std::max)(3, (std::min)(16, GetInt(b_sArms, 8)));
		d.starRadius = cl(BFloat(b_sRad, 6000), 2000, 9000); d.starRingHeight = cl(BFloat(b_sRing, 130), 0, 400);
		d.starRidgeHeight = cl(BFloat(b_sRidge, 152), 20, 400); d.starValleyWidth = cl(BFloat(b_sValley, 90), 30, 400);
		d.starLakeSize = cl(BFloat(b_sLake, 160), 40, 500);
		d.slopeTrees = (std::max)(0, (std::min)(200, GetInt(b_stTrees, 0))); d.slopeTreeSize = cl(BFloat(b_stTreeSize, 1.0f), 0.6f, 1.6f);
		d.mineShare = cl(BFloat(b_mShare, 0), 0, 1); d.mineGrid = cl(BFloat(b_mGrid, 700), 200, 4000);
		d.tunnelWidth = (std::max)(3, (std::min)(11, GetInt(b_mW, 5))); d.tunnelHeight = (std::max)(3, (std::min)(11, GetInt(b_mH, 5)));
		d.mineGalleries = (std::max)(0, (std::min)(8, GetInt(b_mGal, 3))); d.mineShafts = Checked(b_mShaft);
		d.mineWood = BGetRGB(b_mWood); d.mineLight = BGetRGB(b_mLight); d.mineRail = BGetRGB(b_mRail);
		Bio2FromUi(d);
		return d;
	}

	// Listenzeile 0 = native Welt (nur Biome II), danach die *.biom-Dateien
	static int BioSelFile() { int i = (int)SendMessageA(b_list, LB_GETCURSEL, 0, 0); return i >= 1 ? i - 1 : -1; }
	static void BioFillList(const std::string& select) {
		SendMessageA(b_list, LB_RESETCONTENT, 0, 0);
		SendMessageA(b_list, LB_ADDSTRING, 0, (LPARAM)"[Welt] Native Welt (ohne eigenes Biom) -- Mobs");
		std::vector<std::string> files = BiomeGen::ListBiomeFiles();
		int sel = g_bioNative ? 0 : -1;
		for (size_t i = 0; i < files.size(); i++) {
			BiomeGen::BiomeDef d; std::string err;
			std::string line = files[i];
			if (BiomeGen::LoadBiome(BiomeGen::BiomesDir() + "\\" + files[i], d, err)) {
				if (d.nativeClass > 0) line = std::string(d.enabled ? "[nativ an]  " : "[nativ aus] ") + BiomeGen::NativeClassName(d.nativeClass) + "   (natives Biom bearbeiten, " + std::to_string(d.share) + "% der Zellen)";
				else line = std::string(d.enabled ? "[an]  " : "[aus] ") + d.name + "   " + std::to_string(d.share) + "%  " + BiomeGen::ClimateReqName(d.climateReq);
			}
			SendMessageA(b_list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
			if (!g_bioNative && _stricmp(files[i].c_str(), select.c_str()) == 0) sel = (int)i + 1;
		}
		if (sel >= 0) SendMessageA(b_list, LB_SETCURSEL, sel, 0);
	}
	static void BioLoadSelected() {
		int sel = (int)SendMessageA(b_list, LB_GETCURSEL, 0, 0);
		if (sel == 0) {                                                       // native Welt
			BiomeGen::BiomeDef d; std::string err;
			if (!BiomeGen::LoadBiome(Wildlife::NativeProfilePath(), d, err)) { d = BiomeGen::BiomeDef(); d.spawnShare = 40; }
			d.name = "Native Welt"; g_bioNative = true; g_bioNC = 0; g_bioFile.clear(); BioToUi(d); BioTitle(); BioShowPage(2);
			return;
		}
		int i = BioSelFile();
		std::vector<std::string> files = BiomeGen::ListBiomeFiles();
		if (i < 0 || i >= (int)files.size()) return;
		BiomeGen::BiomeDef d; std::string err;
		if (!BiomeGen::LoadBiome(BiomeGen::BiomesDir() + "\\" + files[i], d, err)) { Say("Biom: " + err); return; }
		g_bioNative = false; g_bioNC = d.nativeClass; g_bioFile = files[i]; BioToUi(d); BioTitle();
		if (g_bioNC > 0) Say(std::string("Natives Biom '") + BiomeGen::NativeClassName(g_bioNC) + "': gilt fuer alle Klimazellen dieser Klasse ohne eigenes Biom. "
		                     "Klima/Auswahl werden dafuer nicht benutzt; Vorgaben aendern nichts -- nur was du einstellst, wirkt (neue Zonen).");
	}
	// Speichern + im Spiel neu laden. Umbenennen legt eine neue Datei an.
	static void BioSave() {
		BiomeGen::BiomeDef d = BioFromUi();
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(BiomeGen::BiomesDir());
		if (g_bioNative) {
			d.name = "Native Welt"; std::string err;
			if (!BiomeGen::SaveBiome(Wildlife::NativeProfilePath(), d, err)) { Say("Native Welt speichern: " + err); return; }
			BioFillList("");
			Say("Mobs der nativen Welt gespeichert (" + std::to_string(d.spawns.size()) + " Regeln, Strukturen\\Biome\\_nativ.tiere). Das Spiel liest sie in ~2 s neu; gilt fuer neu erzeugte Zonen.");
			return;
		}
		d.nativeClass = g_bioNC;
		std::string file = g_bioNC > 0 ? std::string("Nativ_") + BiomeGen::NativeClassName(g_bioNC) + ".biom" : d.name + ".biom", err;
		if (!BiomeGen::SaveBiome(BiomeGen::BiomesDir() + "\\" + file, d, err)) { Say("Biom speichern: " + err); return; }
		g_bioFile = file;
		BiomeMod::ReloadDefinitions();
		BioFillList(file);
		Say("Biom '" + d.name + "' gespeichert und geladen" + std::string(d.enabled ? " (AKTIV)" : " (aus)") +
		    ". Gilt fuer alle ab jetzt neu erzeugten Zonen; die Biom-Auswahl fuer neu entdeckte Klimazellen.");
	}
	static void BioRefreshStatus() {
		BiomeMod::Status s = BiomeMod::GetStatus();
		std::ostringstream o;
		o << (BiomeMod::IsEnabled() ? "Biome AN" : "Biome AUS (Vanilla)") << "   Hooks: " << (s.hooksOk ? "ok" : "FEHLER") << "\r\n";
		if (!s.hooksOk && !s.hookReport.empty()) o << s.hookReport << "\r\n";
		o << "Aktive Biome: " << s.biomes << "\r\n";
		o << "Hier: " << (s.hereBiome ? s.hereName : std::string("kein eigenes Biom"));
		if (s.hereBiome) { char b[32]; sprintf_s(b, "  (Gewicht %.2f)", s.hereW); o << b; }
		o << "\r\nZellen mit Biom: " << s.cellsAssigned << "\r\nNative Orte abgeschaltet: " << s.sitesRemoved
		  << "\r\nSpawns umgestellt: " << s.racesReplaced << "   Baeume entfernt: " << s.treesRemoved << "\r\nMinenabschnitte: " << s.minesCarved << "\r\nZonen geschmueckt: " << s.zonesDecorated << "  (wartend " << s.queue << ")"
		  << "\r\nPflanzen: " << s.plantsPlaced << "   Dungeons: " << s.dungeonsPlaced
		  << "\r\nEigene Mobs gespawnt: " << s.mobsSpawned << "  (verfolgt " << s.mobsTracked << ")";
		o << "\r\n" << City::Status();
		std::string t = o.str();
		char cur[1024]; GetWindowTextA(b_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(b_status, t.c_str());
		SetWindowTextA(b_allOn, BiomeMod::IsEnabled() ? "Alle Biome AUS" : "Alle Biome AN");
	}
	static bool BioPickFile(const std::string& dir, const char* filter, std::string& outName) {
		char file[MAX_PATH] = ""; char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
		std::string init = std::string(cwd) + "\\" + dir;
		OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_bioWnd;
		ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = init.c_str();
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameA(&ofn)) return false;
		std::string p = file;
		// Liegt die Datei im erwarteten Ordner, reicht der Name; sonst der volle Pfad.
		if (_strnicmp(p.c_str(), init.c_str(), init.size()) == 0 && p.size() > init.size() + 1) outName = p.substr(init.size() + 1);
		else outName = p;
		return true;
	}
	static void BioAppend(HWND e, const std::string& v) {
		std::string t = GetText(e); if (!t.empty()) t += ","; t += v; SetWindowTextA(e, t.c_str());
	}

	// ---- Biom-Fenster: Bildlauf ------------------------------------------------
	// Der Inhalt waechst mit jeder neuen Einstellung. Statt die Fenstergroesse jedes Mal
	// nachzuziehen, ist das Fenster in der Groesse veraenderbar und rollt senkrecht wie
	// waagrecht (Bildlaufleisten, Mausrad, Umschalt + Mausrad = waagrecht).
	static int g_bioSX = 0, g_bioSY = 0, g_bioCW = 1240, g_bioCH = 800;
	static BOOL CALLBACK BioExtentChild(HWND c, LPARAM lp) {
		if (GetParent(c) != g_bioWnd) return TRUE;
		if (!(GetWindowLongA(c, GWL_STYLE) & WS_VISIBLE)) return TRUE;           // nur die sichtbare Seite zaehlt
		RECT r; GetWindowRect(c, &r); MapWindowPoints(NULL, g_bioWnd, (POINT*)&r, 2);
		int* e = (int*)lp;
		e[0] = (std::max)(e[0], (int)r.right + g_bioSX); e[1] = (std::max)(e[1], (int)r.bottom + g_bioSY);
		return TRUE;
	}
	static void BioMeasure() {
		int e[2] = { 0, 0 };
		EnumChildWindows(g_bioWnd, BioExtentChild, (LPARAM)e);
		g_bioCW = e[0] + 10; g_bioCH = e[1] + 10;
	}
	static void BioScrollTo(HWND h, int nx, int ny) {
		RECT cr; GetClientRect(h, &cr);
		int maxX = (std::max)(0, g_bioCW - (int)cr.right), maxY = (std::max)(0, g_bioCH - (int)cr.bottom);
		nx = (std::max)(0, (std::min)(nx, maxX)); ny = (std::max)(0, (std::min)(ny, maxY));
		int dx = g_bioSX - nx, dy = g_bioSY - ny;
		g_bioSX = nx; g_bioSY = ny;
		if (dx || dy) {                                                           // alle Kinder verschieben, auch die der verborgenen Seite
			for (int g = 0; g < 3; g++) for (HWND c : g_bioGrp[g]) {
				RECT r; GetWindowRect(c, &r); MapWindowPoints(NULL, h, (POINT*)&r, 2);
				SetWindowPos(c, NULL, r.left + dx, r.top + dy, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			InvalidateRect(h, NULL, TRUE);
		}
		SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0; si.nMax = g_bioCW - 1; si.nPage = (UINT)cr.right; si.nPos = g_bioSX; SetScrollInfo(h, SB_HORZ, &si, TRUE);
		si.nMin = 0; si.nMax = g_bioCH - 1; si.nPage = (UINT)cr.bottom; si.nPos = g_bioSY; SetScrollInfo(h, SB_VERT, &si, TRUE);
	}
	static int BioScrollPos(HWND h, int bar, WPARAM wp, int cur, int page) {
		SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL; GetScrollInfo(h, bar, &si);
		switch (LOWORD(wp)) {
			case SB_LINEUP: return cur - 30;            case SB_LINEDOWN: return cur + 30;
			case SB_PAGEUP: return cur - page + 30;     case SB_PAGEDOWN: return cur + page - 30;
			case SB_THUMBTRACK: case SB_THUMBPOSITION: return si.nTrackPos;
			case SB_TOP: return 0;                      case SB_BOTTOM: return INT_MAX / 2;
		}
		return cur;
	}

	// ---- Biome II: Mobs im Biom (2026-10-16) ---------------------------------------------------------------
	// Spawnregeln WAS (native Rasse oder eigener Mob) WO (Lebensraum) mit Gewicht, Level und Verhalten, plus
	// Ueberschreibungen der allgemeinen Tier-Einstellungen. Wirkt am nativen Spawn (Wildlife::OnZoneSpawns).
	static const char* const kWhereNames[BiomeGen::kWhereCount] = { "ueberall", "Land", "Wasser", "Wald", "Wiese", "Savanne", "Wueste", "Dschungel", "Schnee", "Gebirge", "Sumpf", "Kueste" };
	static const char* const kBehaveNames[3] = { "Standard", "friedlich", "feindlich" };
	static int Bio2WhereIdx(const std::string& w) { for (int i = 0; i < BiomeGen::kWhereCount; i++) if (w == BiomeGen::kWhereKeys[i]) return i; return 0; }
	static std::string Bio2MediumName(const std::string& m) { return m == "land" ? "Land" : m == "luft" ? "Luft" : m == "wasser" ? "Wasser" : m; }
	static std::string Bio2WhatLabel(const std::string& key) {
		if (!key.empty() && key[0] == '@') {
			for (const Wildlife::GroupInfo& g : Wildlife::GroupList()) if (g.key == key) return "Gruppe " + Bio2MediumName(g.medium) + ": " + g.name + "  (" + std::to_string(g.count) + " Tiere)";
			return "Gruppe " + key.substr(1) + "  (unbekannt)";
		}
		bool num = !key.empty(); for (char c : key) if (c < '0' || c > '9') num = false;
		if (num) { int r = atoi(key.c_str()); return std::to_string(r) + "  " + (r >= 0 && r < MobLab::kRaceCount ? MobLab::RaceName(r) : "?") + "  (nativ)"; }
		return key;
	}
	static void Bio2ScanMobs(const std::string& dir, const std::string& rel, int depth, std::vector<std::pair<std::string, std::string>>& out) {
		if (depth > 4) return;
		WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (hf == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n, r = rel.empty() ? n : rel + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) Bio2ScanMobs(p, r, depth + 1, out); continue; }
			if (n.size() < 5 || _stricmp(n.c_str() + n.size() - 4, ".mob")) continue;
			std::ifstream in(p); std::string line, name, kind = "Land"; bool peace = false; int base = -1;
			while (std::getline(in, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				size_t eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
				std::string k = line.substr(0, eq), v = line.substr(eq + 1);
				if (k == "rassen_name") name = v; else if (k == "name" && name.empty()) name = v;
				else if (k == "schwimmt" && atoi(v.c_str())) kind = "Wasser"; else if (k == "fliegt" && atoi(v.c_str()) == 1 && kind == "Land") kind = "Luft";
				else if (k == "friedlich" && atoi(v.c_str())) peace = true; else if (k == "basis") base = atoi(v.c_str());
			}
			std::string key = r.substr(0, r.size() - 4);
			out.push_back(std::make_pair(key, key + "   (" + name + ", " + kind + (peace ? ", friedlich" : "") + (base >= 0 ? std::string(", Basis ") + MobLab::RaceName(base) : "") + ")"));
		} while (FindNextFileA(hf, &fd));
		FindClose(hf);
	}
	static void Bio2FillWhat() {
		SendMessageA(b2_what, CB_RESETCONTENT, 0, 0); g_whatKeys.clear();
		for (const Wildlife::GroupInfo& g : Wildlife::GroupList()) {          // zuerst die 15 Tiergruppen (5 je Medium)
			g_whatKeys.push_back(g.key);
			std::string t = "Gruppe " + Bio2MediumName(g.medium) + ":  " + g.name + "  (" + std::to_string(g.count) + " Tiere)";
			SendMessageA(b2_what, CB_ADDSTRING, 0, (LPARAM)t.c_str());
		}
		std::vector<std::pair<std::string, std::string>> mobs; Bio2ScanMobs(MobLab::MobsDir(), "", 0, mobs);
		std::sort(mobs.begin(), mobs.end());
		for (auto& m : mobs) { g_whatKeys.push_back(m.first); SendMessageA(b2_what, CB_ADDSTRING, 0, (LPARAM)("Eigen:  " + m.second).c_str()); }
		for (int r = 0; r < MobLab::kRaceCount; r++) {
			const char* k = MobLab::RaceKind(r);
			if (!strcmp(k, "Objekt") || !strcmp(k, "Pflanze") || !strcmp(k, "Vorkommen")) continue;
			char b[96]; sprintf_s(b, "Nativ:  %d  %s  (%s)", r, MobLab::RaceName(r), k);
			g_whatKeys.push_back(std::to_string(r)); SendMessageA(b2_what, CB_ADDSTRING, 0, (LPARAM)b);
		}
		SendMessageA(b2_what, CB_SETCURSEL, 0, 0);
	}
	static void Bio2LvlEnable() {
		int md = ComboSel(b2_lvMode);
		EnableWindow(b2_lvMin2, md == 1); EnableWindow(b2_lvMax2, md == 1); EnableWindow(b2_lvOff, md == 2);
	}
	static void Bio2Show() {
		g_rulesFilling = true;
		ListView_DeleteAllItems(b2_rules);
		for (size_t i = 0; i < g_rules2.size(); i++) {
			const BiomeGen::SpawnRule& r = g_rules2[i];
			std::string c0 = Bio2WhatLabel(r.what), c1 = kWhereNames[Bio2WhereIdx(r.where)], c2 = std::to_string(r.weight),
			            c3 = r.lvMin > 0 ? (r.lvMax > r.lvMin ? std::to_string(r.lvMin) + " - " + std::to_string(r.lvMax) : std::to_string(r.lvMin)) : "nativ",
			            c4 = kBehaveNames[(r.behave >= 0 && r.behave <= 2) ? r.behave : 0];
			LVITEMA it; memset(&it, 0, sizeof(it)); it.mask = LVIF_TEXT; it.iItem = (int)i; it.pszText = (LPSTR)c0.c_str();
			int n = (int)SendMessageA(b2_rules, LVM_INSERTITEMA, 0, (LPARAM)&it);
			const std::string* cs[4] = { &c1, &c2, &c3, &c4 };
			for (int k = 0; k < 4; k++) { LVITEMA sub; memset(&sub, 0, sizeof(sub)); sub.iSubItem = k + 1; sub.pszText = (LPSTR)cs[k]->c_str(); SendMessageA(b2_rules, LVM_SETITEMTEXTA, n, (LPARAM)&sub); }
		}
		g_rulesFilling = false;
	}
	static void Bio2Edit(const BiomeGen::SpawnRule& r) {
		int wi = -1;
		for (size_t i = 0; i < g_whatKeys.size(); i++) if (_stricmp(g_whatKeys[i].c_str(), r.what.c_str()) == 0) { wi = (int)i; break; }
		if (wi < 0) { g_whatKeys.push_back(r.what); wi = (int)SendMessageA(b2_what, CB_ADDSTRING, 0, (LPARAM)(r.what + "   (Datei fehlt?)").c_str()); }
		SendMessageA(b2_what, CB_SETCURSEL, wi, 0);
		SendMessageA(b2_where, CB_SETCURSEL, Bio2WhereIdx(r.where), 0);
		SetInt(b2_weight, r.weight); SetInt(b2_lvMin, r.lvMin); SetInt(b2_lvMax, r.lvMax);
		SendMessageA(b2_behave, CB_SETCURSEL, (r.behave >= 0 && r.behave <= 2) ? r.behave : 0, 0);
	}
	static void Bio2Notify(NMHDR* nh) {
		if (nh->hwndFrom != b2_rules || nh->code != LVN_ITEMCHANGED || g_rulesFilling) return;
		NMLISTVIEW* nl = (NMLISTVIEW*)nh;
		if (nl->iItem < 0 || nl->iItem >= (int)g_rules2.size()) return;
		if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED)) Bio2Edit(g_rules2[nl->iItem]);
	}
	static bool Bio2FromEditor(BiomeGen::SpawnRule& r) {
		int wi = (int)SendMessageA(b2_what, CB_GETCURSEL, 0, 0);
		if (wi < 0 || wi >= (int)g_whatKeys.size()) { Say("Bei 'Was' einen Mob oder eine Rasse waehlen."); return false; }
		r.what = g_whatKeys[wi];
		r.where = BiomeGen::kWhereKeys[ComboSel(b2_where) % BiomeGen::kWhereCount];
		r.weight = (std::max)(1, (std::min)(1000, GetInt(b2_weight, 10)));
		r.lvMin = (std::max)(0, GetInt(b2_lvMin, 0)); r.lvMax = (std::max)(0, GetInt(b2_lvMax, 0));
		if (r.lvMin == 0) r.lvMax = 0; else if (r.lvMax < r.lvMin) r.lvMax = r.lvMin;
		r.behave = ComboSel(b2_behave) % 3;
		return true;
	}
	static void Bio2Store(bool replace) {
		BiomeGen::SpawnRule r; if (!Bio2FromEditor(r)) return;
		int i = ListView_GetNextItem(b2_rules, -1, LVNI_SELECTED);
		if (replace) {
			if (i < 0 || i >= (int)g_rules2.size()) { Say("Erst eine Regel in der Liste waehlen."); return; }
			g_rules2[i] = r;
		} else { g_rules2.push_back(r); i = (int)g_rules2.size() - 1; }
		Bio2Show();
		ListView_SetItemState(b2_rules, i, LVIS_SELECTED, LVIS_SELECTED); ListView_EnsureVisible(b2_rules, i, FALSE);
		Say(std::string(replace ? "Regel geaendert" : "Regel hinzugefuegt") + ": " + Bio2WhatLabel(r.what) + " -- 'Speichern + laden' nicht vergessen.");
	}
	// Test: die Regel im Editor einmal am Spieler (+40 Bloecke in X) spawnen -- mit Level und Verhalten der Regel.
	static void Bio2Test() {
		BiomeGen::SpawnRule r; if (!Bio2FromEditor(r)) return;
		bool num = !r.what.empty(); for (char c : r.what) if (c < '0' || c > '9') num = false;
		int lv = r.lvMin > 0 ? r.lvMin + (r.lvMax > r.lvMin ? (int)(GetTickCount() % (DWORD)(r.lvMax - r.lvMin + 1)) : 0) : 0;
		int beh = r.behave;
		if (num) {
			int race = atoi(r.what.c_str());
			LabBridge::Post("Biome II Test", [race, lv, beh](cube::GameController* gc) {
				int64_t x, y, z; if (!gc || !gc->local_player) return;
				x = gc->local_player->entity_data.position.X; y = gc->local_player->entity_data.position.Y; z = gc->local_player->entity_data.position.Z;
				int level = lv > 0 ? lv : MobLab::PlayerLevel(gc);
				uint64_t g = MobLab::SpawnRaceAt(gc, race, 1, level, x + 40LL * 65536, y, z + 2LL * 65536, false, true, 1.0f);
				if (g && beh == 1) MobLab::SetPeaceful(gc, g);
				std::cout << "[Biome II] Test: " << MobLab::RaceName(race) << " Level " << level << (beh == 1 ? " friedlich" : beh == 2 ? " feindlich" : "") << (g ? " gespawnt (+40 X)." : " FEHLGESCHLAGEN.") << std::endl;
			});
		} else {
			std::string path = MobLab::MobsDir() + "\\" + r.what + ".mob";
			if (r.what[0] == '@') {                                              // Tiergruppe: ein zufaelliges Tier daraus
				size_t sl = r.what.find('/'); std::string med = r.what.substr(1, sl == std::string::npos ? std::string::npos : sl - 1), grp = sl == std::string::npos ? "" : r.what.substr(sl + 1);
				std::vector<std::string> mem; std::ifstream f(MobLab::MobsDir() + "\\Tierwelt\\_tierwelt.txt"); std::string line;
				while (std::getline(f, line)) {
					if (!line.empty() && line.back() == '\r') line.pop_back();
					if (line.empty() || line[0] == '#') continue;
					std::vector<std::string> c; std::string cur; for (char ch : line) { if (ch == ';') { c.push_back(cur); cur.clear(); } else cur += ch; } c.push_back(cur);
					if (c.size() > 6 && c[1] == med && c[6] == grp) mem.push_back(c[0]);
				}
				if (mem.empty()) { Say("Die Gruppe ist leer."); return; }
				path = MobLab::MobsDir() + "\\Tierwelt\\" + mem[GetTickCount() % mem.size()] + ".mob";
			}
			LabBridge::Post("Biome II Test", [path, lv, beh](cube::GameController* gc) {
				MobLab::CustomMob m; std::string err, rep;
				if (!MobLab::LoadMob(path, m, err)) { std::cout << "[Biome II] " << err << std::endl; return; }
				if (lv > 0) m.level = lv;
				if (beh == 1) { m.peaceful = true; m.hostility = 1; } else if (beh == 2) { m.peaceful = false; m.hostility = 1; }
				MobLab::SpawnCustom(gc, m, 40, rep);
				std::cout << "[Biome II] Test: " << rep << std::endl;
			});
		}
		Say("Test-Spawn angefordert (40 Bloecke vor dir in X). Ergebnis im Logger.");
	}
	static void Bio2ToUi(const BiomeGen::BiomeDef& d) {
		g_rules2 = d.spawns; Bio2Show();
		SetInt(b2_share, d.spawnShare);
		int wi = 0; for (int i = 0; i < 7; i++) if (kWildVals[i] == d.wildShare) wi = i;
		if (d.wildShare >= 0 && kWildVals[wi] != d.wildShare) { wi = 1; for (int i = 1; i < 7; i++) if (std::abs(kWildVals[i] - d.wildShare) < std::abs(kWildVals[wi] - d.wildShare)) wi = i; }
		SendMessageA(b2_wild, CB_SETCURSEL, wi, 0);
		SendMessageA(b2_behAll, CB_SETCURSEL, d.behaveAll, 0);
		SendMessageA(b2_lvMode, CB_SETCURSEL, d.levelMode, 0);
		SetInt(b2_lvMin2, d.levelMin); SetInt(b2_lvMax2, d.levelMax); SetInt(b2_lvOff, d.levelOffset);
		Bio2LvlEnable();
	}
	static void Bio2FromUi(BiomeGen::BiomeDef& d) {
		d.spawns = g_rules2;
		d.spawnShare = (std::max)(0, (std::min)(100, GetInt(b2_share, 70)));
		d.wildShare = kWildVals[ComboSel(b2_wild) % 7];
		d.behaveAll = ComboSel(b2_behAll) % 3;
		d.levelMode = ComboSel(b2_lvMode) % 3;
		d.levelMin = (std::max)(1, GetInt(b2_lvMin2, 1)); d.levelMax = (std::max)(1, GetInt(b2_lvMax2, 10));
		if (d.levelMax < d.levelMin) std::swap(d.levelMin, d.levelMax);
		d.levelOffset = (std::max)(-200, (std::min)(200, GetInt(b2_lvOff, 0)));
	}
	static void BioTitle() {
		std::string t = g_bioNative ? "Mobs: Native Welt  (alle Gebiete ohne eigenes Biom -- Strukturen\\Biome\\_nativ.tiere)"
		                            : (g_bioNC > 0 ? std::string("Mobs im nativen Biom: ") + BiomeGen::NativeClassName(g_bioNC) + "  (" + g_bioFile + ")"
		                                           : "Mobs im Biom: " + GetText(b_name) + (g_bioFile.empty() ? "  (noch nicht gespeichert)" : "  (" + g_bioFile + ")"));
		SetWindowTextA(b2_title, t.c_str());
	}
	static void BioShowPage(int p) {
		BioScrollTo(g_bioWnd, 0, 0);
		g_bioPage = p;
		for (HWND c : g_bioGrp[1]) ShowWindow(c, p == 1 ? SW_SHOW : SW_HIDE);
		for (HWND c : g_bioGrp[2]) ShowWindow(c, p == 2 ? SW_SHOW : SW_HIDE);
		SetWindowTextA(b_page1, p == 1 ? "> Biome I: Geografie <" : "Biome I: Geografie");
		SetWindowTextA(b_page2, p == 2 ? "> Biome II: Mobs <" : "Biome II: Mobs");
		if (p == 2) BioTitle();
		BioMeasure(); BioScrollTo(g_bioWnd, 0, 0);
		InvalidateRect(g_bioWnd, NULL, TRUE);
	}
	static void Bio2Create() {
		g_bioGroup = 2;
		int x = 285, y = 10;
		b2_title = BCtl("STATIC", "Mobs im Biom", SS_LEFT, x, y, 660, 16, -1); SendMessageA(b2_title, WM_SETFONT, (WPARAM)g_fontBold, TRUE); y += 20;
		BCtl("STATIC", "Am nativen Spawn: ein Teil der nativen Tier- und Fischgruppen wird durch eine passende Regel ersetzt (gleiche Stelle, "
		              "Gruppe, Entladen und Wiederkehr wie nativ). Ohne passende Regel: eigene Tiere der Tierwelt mit gleicher Basisrasse.",
		     SS_LEFT, x, y, 660, 30, -1); y += 34;
		b2_rules = BCtl(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, x, y, 660, 250, BID2_RULES, WS_EX_CLIENTEDGE);
		ListView_SetExtendedListViewStyle(b2_rules, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
		{
			LVCOLUMNA c; memset(&c, 0, sizeof(c)); c.mask = LVCF_TEXT | LVCF_WIDTH;
			const char* t[5] = { "Was", "Wo", "Gewicht", "Level", "Verhalten" }; const int w[5] = { 300, 90, 64, 90, 90 };
			for (int i = 0; i < 5; i++) { c.cx = w[i]; c.pszText = (LPSTR)t[i]; SendMessageA(b2_rules, LVM_INSERTCOLUMNA, i, (LPARAM)&c); }
		}
		y += 256;
		BLbl("Was:", x, y + 4, 40); b2_what = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 42, y, 400, 400, -1);
		BLbl("Wo:", x + 452, y + 4, 30); b2_where = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 484, y, 176, 300, -1);
		for (int i = 0; i < BiomeGen::kWhereCount; i++) SendMessageA(b2_where, CB_ADDSTRING, 0, (LPARAM)kWhereNames[i]);
		SendMessageA(b2_where, CB_SETCURSEL, 0, 0); y += 28;
		BLbl("Gewicht:", x, y + 4, 52); b2_weight = BEdt(x + 54, y, 46, "10");
		BLbl("Level von:", x + 112, y + 4, 62); b2_lvMin = BEdt(x + 176, y, 46, "0"); BLbl("bis:", x + 228, y + 4, 24); b2_lvMax = BEdt(x + 254, y, 46, "0");
		BLbl("(0 = nativ)", x + 306, y + 4, 70);
		BLbl("Verhalten:", x + 384, y + 4, 62); b2_behave = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 448, y, 212, 200, -1);
		SendMessageA(b2_behave, CB_ADDSTRING, 0, (LPARAM)"Standard (Biom / Datei)"); SendMessageA(b2_behave, CB_ADDSTRING, 0, (LPARAM)"friedlich (wie das Huhn)");
		SendMessageA(b2_behave, CB_ADDSTRING, 0, (LPARAM)"feindlich"); SendMessageA(b2_behave, CB_SETCURSEL, 0, 0); y += 30;
		BBtn("Hinzufuegen", x, y, 120, BID2_ADD); BBtn("Auswahl aendern", x + 124, y, 130, BID2_SET); BBtn("Entfernen", x + 258, y, 100, BID2_DEL);
		BBtn("Test: am Spieler spawnen (+40)", x + 430, y, 230, BID2_TEST); y += 30;
		BBtn("Alle 15 Tiergruppen eintragen", x, y, 254, BID2_ALLGROUPS);
		BLbl("Gruppe = je Spawn ein passendes Tier der Gruppe (Lebensraum bevorzugt)", x + 262, y + 5, 400); y += 34;
		BLbl("Anteil der nativen Tiergruppen, die durch die Liste ersetzt werden (%):", x, y + 4, 420); b2_share = BEdt(x + 424, y, 46, "70"); y += 34;

		BLbl("Allgemeine Tier-Einstellungen hier ueberschreiben", x, y, 660, true); y += 20;
		BLbl("Eigene Tiere (Tierwelt, gleiche Basisrasse):", x, y + 4, 260);
		b2_wild = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 262, y, 240, 300, -1);
		{ const char* t[7] = { "wie allgemein (Netz & Tiere)", "keine", "10 %", "25 %", "50 %", "75 %", "100 %" }; for (auto q : t) SendMessageA(b2_wild, CB_ADDSTRING, 0, (LPARAM)q); }
		y += 28;
		BLbl("Verhalten aller Tiere im Biom:", x, y + 4, 260);
		b2_behAll = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 262, y, 240, 200, -1);
		{ const char* t[3] = { "nativ / wie in der .mob-Datei", "alle friedlich (wie das Huhn)", "alle feindlich" }; for (auto q : t) SendMessageA(b2_behAll, CB_ADDSTRING, 0, (LPARAM)q); }
		y += 28;
		BLbl("Level der Tiere:", x, y + 4, 260);
		b2_lvMode = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 262, y, 240, 200, BID2_LVMODE);
		{ const char* t[3] = { "nativ (Level der Gegend)", "feste Spanne", "nativ + Versatz" }; for (auto q : t) SendMessageA(b2_lvMode, CB_ADDSTRING, 0, (LPARAM)q); }
		y += 28;
		BLbl("Spanne von:", x + 262, y + 4, 70); b2_lvMin2 = BEdt(x + 334, y, 46, "1"); BLbl("bis:", x + 386, y + 4, 24); b2_lvMax2 = BEdt(x + 412, y, 46, "10");
		BLbl("Versatz:", x + 470, y + 4, 50); b2_lvOff = BEdt(x + 522, y, 46, "0"); y += 32;
		BCtl("STATIC", "Reihenfolge: Level/Verhalten der Regel > Biom-Ueberschreibung > nativ bzw. .mob-Datei. 'Speichern + laden' (links) "
		              "uebernimmt alles; es wirkt auf NEU erzeugte Zonen. Friedlich = wie das native Huhn: greift nie an, huepft weg, "
		              "ist angreifbar und zaehmbar. Dungeon-Besatzung, Spawner und Tierviertel: rechte Spalte.", SS_LEFT, x, y, 660, 48, -1);
		Bio2FillWhat();
		SendMessageA(b2_wild, CB_SETCURSEL, 0, 0); SendMessageA(b2_behAll, CB_SETCURSEL, 0, 0); SendMessageA(b2_lvMode, CB_SETCURSEL, 0, 0);
		Bio2LvlEnable();
	}

	static LRESULT CALLBACK BiomeProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_SIZE: if (g_bioWnd) BioScrollTo(h, g_bioSX, g_bioSY); return 0;
			case WM_VSCROLL: { RECT cr; GetClientRect(h, &cr); BioScrollTo(h, g_bioSX, BioScrollPos(h, SB_VERT, wp, g_bioSY, cr.bottom)); return 0; }
			case WM_HSCROLL: { RECT cr; GetClientRect(h, &cr); BioScrollTo(h, BioScrollPos(h, SB_HORZ, wp, g_bioSX, cr.right), g_bioSY); return 0; }
			case WM_MOUSEWHEEL: {
				int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
				if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) BioScrollTo(h, g_bioSX - steps * 60, g_bioSY);
				else BioScrollTo(h, g_bioSX, g_bioSY - steps * 60);
				return 0;
			}
			case WM_CREATE: {
				g_bioWnd = h; g_colRows.clear();
				for (auto& v : g_bioGrp) v.clear();
				g_bioGroup = 0; g_b2Y = 10;
				// ---- Spalte 1: Seitenwahl, Liste + Zustand ----
				int x = 10, y = 10;
				b_page1 = BBtn("Biome I: Geografie", x, y, 128, BID_PAGE1, 26); b_page2 = BBtn("Biome II: Mobs", x + 132, y, 128, BID_PAGE2, 26); y += 34;
				BLbl("Biome  (Strukturen\\Biome\\*.biom)", x, y, 260, true); y += 20;
				b_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 260, 200, BID_LIST, WS_EX_CLIENTEDGE); y += 204;
				BBtn("Neu", x, y, 60, BID_NEW); BBtn("Speichern + laden", x + 63, y, 130, BID_SAVE); BBtn("Loeschen", x + 196, y, 64, BID_DELETE); y += 28;
				BLbl("Vorlagen:", x, y + 5, 60);
				BBtn("Sumpf", x + 62, y, 48, BID_EX0); BBtn("Wueste", x + 112, y, 48, BID_EX1); BBtn("Pilze", x + 162, y, 46, BID_EX2);
				BBtn("Gebirge", x + 210, y, 50, BID_EX3); y += 28;
				BBtn("Sternmassiv", x + 62, y, 98, BID_EX4); y += 28;
				BBtn("Alle neu einlesen", x, y, 128, BID_RELOAD); b_allOn = BBtn("Alle Biome AUS", x + 132, y, 128, BID_ALL); y += 34;
				BLbl("Im Spiel", x, y, 260, true); y += 18;
				b_status = BCtl("STATIC", "", SS_LEFT, x, y, 260, 176, -1, WS_EX_CLIENTEDGE); y += 180;
				BBtn("Biom HIER erzwingen", x, y, 170, BID_FORCE); BBtn("aufheben", x + 174, y, 86, BID_UNFORCE); y += 28;
				BBtn("Naechstes Biom suchen", x, y, 128, BID_NEAREST); BBtn("Teleport (ungetestet)", x + 132, y, 128, BID_TELEPORT); y += 30;
				BCtl("STATIC", "Aenderungen wirken auf NEU erzeugte Zonen. Welche Klimazelle welches Biom wird, "
				              "entscheidet sich, wenn die Zelle angelegt wird -- oder per 'erzwingen'.", SS_LEFT, x, y, 260, 64, -1);

				// ---- Spalte 2: Auswahl, Boden (Biome I) ----
				g_bioGroup = 1;
				x = 285; y = 10;
				BLbl("Auswahl und Klima", x, y, 300, true); y += 20;
				BLbl("Name:", x, y + 4, 92); b_name = BEdt(x + 94, y, 160); b_enabled = BChk("aktiv", x + 258, y + 1, 60); y += 26;
				BLbl("Anteil %:", x, y + 4, 92); b_share = BEdt(x + 94, y, 46);
				BLbl("der passenden Landzellen", x + 144, y + 4, 170); y += 26;
				BLbl("Nur Zellen:", x, y + 4, 92);
				b_climate = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 94, y, 160, 200, -1);
				for (int i = 0; i < BiomeGen::CR_COUNT; i++) SendMessageA(b_climate, CB_ADDSTRING, 0, (LPARAM)BiomeGen::ClimateReqName(i));
				y += 26;
				b_setClim = BChk("Klima setzen:", x, y + 1, 92);
				BLbl("Temp.", x + 96, y + 4, 36); b_temp = BEdt(x + 134, y, 42); BLbl("Feuchte", x + 182, y + 4, 50); b_hum = BEdt(x + 234, y, 42); y += 30;

				BLbl("Boden und Fels", x, y, 300, true); y += 20;
				BRgbRow("Boden A:", x, y, b_g1); y += 26;
				BRgbRow("Boden B:", x, y, b_g2); y += 26;
				BLbl("Bodentyp:", x, y + 4, 92);
				b_gtype = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 94, y, 200, 200, -1);
				{ const char* t[5] = { "nativ lassen", "4  Gras/Erde", "9  Sand", "10  Schnee", "12  Lava-Boden" }; for (auto s : t) SendMessageA(b_gtype, CB_ADDSTRING, 0, (LPARAM)s); }
				y += 26;
				BLbl("Deckkraft 0-1:", x, y + 4, 92); b_strength = BEdt(x + 94, y, 46); y += 26;
				BRgbRow("Fels:", x, y, b_rock); b_rockOn = BChk("faerben", x + 252, y + 1, 70); y += 32;

				{ int sx = x, sy = y; g_bioGroup = 2; x = 960; y = g_b2Y;          // -> Biome II, rechte Spalte
				BLbl("Rassen-Ersatz (alle nativen Landspawns)", x, y, 320, true); y += 20;
				BLbl("Rassen-IDs:", x, y + 4, 92); b_races = BEdt(x + 94, y, 222); y += 26;
				b_racePick = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 94, y, 180, 300, -1);
				for (int r = 0; r < MobLab::kRaceCount; r++) { char b[64]; sprintf_s(b, "%d  %s", r, MobLab::RaceName(r)); SendMessageA(b_racePick, CB_ADDSTRING, 0, (LPARAM)b); }
				SendMessageA(b_racePick, CB_SETCURSEL, 68, 0);
				BBtn("+", x + 278, y, 38, BID_RACE_ADD, 22); y += 26;
				BLbl("Anteil 0-1:", x, y + 4, 92); b_raceShare = BEdt(x + 94, y, 46);
				BLbl("der nativen Landspawns", x + 144, y + 4, 170); y += 30;
				BLbl("Eigene Mobs (Mob-Labor, Strukturen\\Mobs)", x, y, 320, true); y += 20;
				BLbl(".mob-Dateien:", x, y + 4, 92); b_mobs = BEdt(x + 94, y, 180); BBtn("...", x + 278, y, 38, BID_MOB_ADD, 22); y += 26;
				BLbl("Max. nah:", x, y + 4, 92); b_mobMax = BEdt(x + 94, y, 46); y += 26;
				BLbl("Schwierigkeit:", x, y + 4, 92);
				b_lvlMode = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 94, y, 222, 200, BID_LVLMODE);
				SendMessageA(b_lvlMode, CB_ADDSTRING, 0, (LPARAM)"Spielerlevel (+- Abweichung)"); SendMessageA(b_lvlMode, CB_ADDSTRING, 0, (LPARAM)"Feste Levelspanne");
				y += 26;
				BLbl("Level von:", x, y + 4, 92); b_lvlMin = BEdt(x + 94, y, 46); BLbl("bis:", x + 146, y + 4, 24); b_lvlMax = BEdt(x + 172, y, 46);
				BLbl("+-", x + 226, y + 4, 16); b_lvlOff = BEdt(x + 244, y, 40); y += 26;
				b_spRaces = BChk("Spawner setzt auch die Rassen-IDs oben", x, y, 320); y += 22;
				BCtl("STATIC", "Gilt fuer eigene Mobs, Spawner und Dungeon-Besatzung. Native Spawns: links unter 'Level der Tiere'.", SS_LEFT, x, y, 320, 30, -1); y += 34;
				g_b2Y = y; x = sx; y = sy; g_bioGroup = 1; }

				BLbl("Minenschaechte (in den Bergzuegen)", x, y, 320, true); y += 20;
				BLbl("Anteil 0-1:", x, y + 4, 92); b_mShare = BEdt(x + 94, y, 46); BLbl("Raster:", x + 150, y + 4, 60); b_mGrid = BEdt(x + 212, y, 46); y += 26;
				BLbl("Stollen B x H:", x, y + 4, 92); b_mW = BEdt(x + 94, y, 46); b_mH = BEdt(x + 144, y, 46); y += 26;
				BLbl("Seitengaenge:", x, y + 4, 92); b_mGal = BEdt(x + 94, y, 46); b_mShaft = BChk("senkrechte Schaechte", x + 150, y + 1, 170); y += 26;
				BRgbRow("Holz:", x, y, b_mWood); y += 26;
				BRgbRow("Licht:", x, y, b_mLight); y += 26;
				BRgbRow("Schienen:", x, y, b_mRail); y += 26;

				// ---- Spalte 3: Pflanzen + Dungeons ----
				x = 625; y = 10;
				BLbl("Pflanzen (prozedural, je Zone)", x, y, 300, true); y += 20;
				BLbl("Dichte:", x, y + 4, 92); b_density = BEdt(x + 94, y, 46); BLbl("Groesse x:", x + 150, y + 4, 64); b_psize = BEdt(x + 216, y, 46); y += 26;
				BCtl("STATIC", "Welche Arten, Dateien und Zonen-Modelle: Liste 'Baeume & Pflanzen' (ganz rechts).", SS_LEFT, x, y, 320, 30, -1);
				y += 32;
				BRgbRow("Stamm:", x, y, b_trunk); y += 26;
				BRgbRow("Blatt:", x, y, b_leaf); y += 26;
				BRgbRow("Akzent:", x, y, b_acc); y += 26;
				b_kind = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, 92, 200, -1);
				for (int i = 0; i < BiomeGen::PK_COUNT; i++) SendMessageA(b_kind, CB_ADDSTRING, 0, (LPARAM)BiomeGen::PlantKindName(i));
				SendMessageA(b_kind, CB_SETCURSEL, 0, 0);
				BBtn("-> LAB-Editor", x + 96, y, 104, BID_PLANT_EDIT); BBtn("Test +40", x + 204, y, 112, BID_PLANT_SET); y += 34;

				BLbl("Dungeons (prozedural)", x, y, 300, true); y += 20;
				BLbl("Anteil % Zone:", x, y + 4, 92); b_dChance = BEdt(x + 94, y, 46); BLbl("Raeume:", x + 150, y + 4, 50); b_dRooms = BEdt(x + 202, y, 40);
				BLbl("je Seite", x + 246, y + 4, 60); y += 26;
				BRgbRow("Wand:", x, y, b_wall); y += 26;
				BRgbRow("Boden:", x, y, b_floor); y += 26;
				BRgbRow("Akzent:", x, y, b_dacc); y += 26;
				b_roof = BChk("Dach", x, y + 1, 60); BLbl("Verfall 0-1:", x + 70, y + 4, 70); b_decay = BEdt(x + 142, y, 42);
				b_boss = BChk("Boss", x + 196, y + 1, 60); y += 26;
				{ int sx = x, sy = y; g_bioGroup = 2; x = 960; y = g_b2Y;          // -> Biome II
				BLbl("Dungeon-Besatzung", x, y, 320, true); y += 20;
				BLbl("Rassen-IDs:", x, y + 4, 92); b_dRaces = BEdt(x + 94, y, 222); y += 26;
				BLbl(".mob-Dateien:", x, y + 4, 92); b_dMobs = BEdt(x + 94, y, 180); BBtn("...", x + 278, y, 38, BID_DMOB_ADD, 22); y += 26;
				BLbl("Besatzung:", x, y + 4, 92); b_dCount = BEdt(x + 94, y, 46); y += 34;
				g_b2Y = y; x = sx; y = sy; g_bioGroup = 1; }
				BBtn("Dungeon -> LAB-Editor", x, y, 156, BID_DUN_EDIT); BBtn("Test +40", x + 160, y, 156, BID_DUN_SET); y += 34;

				BLbl("Bergzuege (Gelaende formen muss an sein)", x, y, 320, true); y += 20;
				BLbl("Hoehe:", x, y + 4, 92); b_gH = BEdt(x + 94, y, 46); BLbl("halbe Breite:", x + 150, y + 4, 80); b_gW = BEdt(x + 232, y, 46); y += 26;
				BLbl("Abstand:", x, y + 4, 92); b_gSp = BEdt(x + 94, y, 46); BLbl("Zacken 0-1:", x + 150, y + 4, 80); b_gRough = BEdt(x + 232, y, 46); y += 26;
				BLbl("Schneegrenze:", x, y + 4, 92); b_snow = BEdt(x + 94, y, 46); BLbl("(Hoehe, 0 = aus)", x + 150, y + 4, 120); y += 32;

				BLbl("Grosse Hangbaeume (Bergfichten)", x, y, 320, true); y += 20;
				BLbl("Versuche/Zone:", x, y + 4, 92); b_stTrees = BEdt(x + 94, y, 46); BLbl("Groesse x:", x + 150, y + 4, 80); b_stTreeSize = BEdt(x + 232, y, 46); y += 32;

				BLbl("Sternmassiv (Mitte = Ebene: Radius/Hoehe)", x, y, 320, true); y += 20;
				b_sOn = BChk("Bergring + sternfoermige Grate und Taeler", x, y, 320); y += 22;
				BLbl("Taeler:", x, y + 4, 92); b_sArms = BEdt(x + 94, y, 46); BLbl("Radius:", x + 150, y + 4, 80); b_sRad = BEdt(x + 232, y, 46); y += 26;
				BLbl("Ring-Hoehe:", x, y + 4, 92); b_sRing = BEdt(x + 94, y, 46); BLbl("Grat-Hoehe:", x + 150, y + 4, 80); b_sRidge = BEdt(x + 232, y, 46); y += 26;
				BLbl("Talgrund-Breite:", x, y + 4, 92); b_sValley = BEdt(x + 94, y, 46); BLbl("See-Groesse:", x + 150, y + 4, 80); b_sLake = BEdt(x + 232, y, 46); y += 26;

				// ---- Spalte 4: native Orte ----
				x = 960; y = 10;
				BLbl("Native Orte im Biom", x, y, 270, true); y += 18;
				BCtl("STATIC", "Haken = bleibt. Ohne Haken wird der Ort beim Anlegen der Region geloescht (Typ 0).", SS_LEFT, x, y, 270, 32, -1); y += 34;
				for (int i = 0; i < BiomeGen::kSiteKindCount; i++) {
					char b[80]; sprintf_s(b, "%2d  %s", BiomeGen::kSiteKinds[i], BiomeGen::SiteKindName(BiomeGen::kSiteKinds[i]));
					b_site[i] = BChk(b, x, y, 270); y += 21;
				}
				y += 4;
				BBtn("Alle Dungeons aus", x, y, 133, BID_SITES_DUN_OFF); BBtn("Alles an", x + 137, y, 133, BID_SITES_ALL_ON); y += 28;
				BBtn("Native Orte in der Naehe anzeigen", x, y, 270, BID_SITES_NEAR); y += 32;
				b_bergdorf = BChk("Bergdorf (Mod) im Biom erlauben", x, y, 270); y += 30;

				// ---- Gelaende (unter den Orten) ----
				BLbl("Gelaende", x, y, 270, true); y += 18;
				b_tOn = BChk("Gelaende formen (Hoehe, Wasser, Huegel)", x, y, 270); y += 22;
				BLbl("Grundhoehe:", x, y + 4, 76); b_tBase = BEdt(x + 78, y, 44);
				BLbl("Wellen +-:", x + 132, y + 4, 62); b_tWaves = BEdt(x + 196, y, 44); y += 25;
				BLbl("Wellen-Laenge:", x, y + 4, 76); b_tWaveSize = BEdt(x + 78, y, 44);
				BLbl("Glaetten 0-1:", x + 132, y + 4, 62); b_tFlat = BEdt(x + 196, y, 44); y += 25;
				BLbl("Huegel-Anteil:", x, y + 4, 76); b_tHillShare = BEdt(x + 78, y, 44);
				BLbl("Huegel-Hoehe:", x + 132, y + 4, 62); b_tHillH = BEdt(x + 196, y, 44); y += 25;
				BLbl("Huegel-Groesse:", x, y + 4, 76); b_tHillSize = BEdt(x + 78, y, 44);
				BLbl("Berge 0-1:", x + 132, y + 4, 62);  /* <= 0.1 = ganz aus (sonst Furchen) */ b_tMount = BEdt(x + 196, y, 44); y += 25;
				b_rNative = BChk("native Fluesse", x, y + 1, 110); BLbl("Breite x:", x + 132, y + 4, 62); b_rWidth = BEdt(x + 196, y, 44); y += 25;
				b_rOwn = BChk("eigene Fluesse", x, y + 1, 110); BLbl("Abstand:", x + 132, y + 4, 62); b_rSpacing = BEdt(x + 196, y, 44); y += 25;
				BLbl("Seen-Anteil:", x, y + 4, 76); b_lShare = BEdt(x + 78, y, 44);
				BLbl("Seen-Groesse:", x + 132, y + 4, 62); b_lSize = BEdt(x + 196, y, 44); y += 26;
				BCtl("STATIC", "Wasser folgt dem Gelaende blockweise. Meer: unter Hoehe 1.", SS_LEFT, x, y, 270, 16, -1); y += 20;
				// ---- Freie Ebene in der Mitte jeder Biomzelle ----
				b_pOn = BChk("Freie Ebene in der Mitte (flach, leer)", x, y, 270); y += 22;
				BLbl("Radius:", x, y + 4, 76); b_pRadius = BEdt(x + 78, y, 44);
				BLbl("Hoehe:", x + 132, y + 4, 62); b_pHeight = BEdt(x + 196, y, 44); y += 25;
				BLbl("Rand:", x, y + 4, 76); b_pRim = BEdt(x + 78, y, 44);
				BLbl("(Bloecke, Uebergang)", x + 132, y + 4, 130); y += 27;
				// ---- Stadt auf der Ebene / Hochebene ----
				BLbl("Stadt auf der Ebene:", x, y + 4, 110);
				b_city = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 112, y, 158, 200, -1);
				for (int k = 0; k <= 3; k++) SendMessageA(b_city, CB_ADDSTRING, 0, (LPARAM)City::KindName(k));
				y += 27;
				BLbl("Stadtradius:", x, y + 4, 76); b_cityR = BEdt(x + 78, y, 44);
				BLbl("(0 = ganze Ebene)", x + 132, y + 4, 130); y += 25;
				{ int sx = x, sy = y; g_bioGroup = 2; x = 960; y = g_b2Y;          // -> Biome II
				BLbl("Tierviertel der Stadt (Gehege)", x, y, 320, true); y += 20;
				BLbl("Tiere (Rassen):", x, y + 4, 110); b_cityAnimals = BEdt(x + 112, y, 158); y += 26;
				b_animalPick = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 112, y, 116, 300, -1);
				for (int r = 0; r < MobLab::kRaceCount; r++) if (!strcmp(MobLab::RaceKind(r), "Tier")) { char b[64]; sprintf_s(b, "%d  %s", r, MobLab::RaceName(r)); SendMessageA(b_animalPick, CB_ADDSTRING, 0, (LPARAM)b); }
				SendMessageA(b_animalPick, CB_SETCURSEL, 0, 0);
				BBtn("+", x + 232, y, 38, BID_ANIMAL_ADD, 22); y += 28;
				g_b2Y = y; x = sx; y = sy; g_bioGroup = 1; }
				BBtn("Stadtmodelle als .cub ablegen", x, y, 200, BID_CITYEXP); y += 26;
				BCtl("STATIC", "Die Stadt entsteht beim Erzeugen neuer Zonen. Laternen leuchten nachts, Landeplatz 0x887 mit Luftschiff-Taxi.", SS_LEFT, x, y, 270, 44, -1);

				// ---- Spalte 5: Abhaklisten Baeume & Pflanzen / Sammelbare ----
				x = 1245; y = 10;
				BLbl("Baeume & Pflanzen im Biom", x, y, 360, true); y += 18;
				BCtl("STATIC", "Haken = waechst hier. Unten immer ein freier Platz zum Hinzufuegen (anklicken).", SS_LEFT, x, y, 380, 18, -1); y += 20;
				b_flora = BioMakeList(x, y, 380, 300, BID_FLORA); y += 304;
				BLbl("Gewicht:", x, y + 4, 60); b_flW = BEdt(x + 62, y, 46); BBtn("setzen", x + 112, y, 60, BID_FLW_SET, 22);
				BLbl("(Art 1-20, native % 1-100, .cub gemeinsam)", x + 178, y + 4, 210); y += 32;
				BLbl("Sammelbare Pflanzen im Biom", x, y, 360, true); y += 18;
				b_pick = BioMakeList(x, y, 380, 170, BID_PICK); y += 174;
				BCtl("STATIC", "Sammelbare sind Zonen-Modelle der Art 'Sammelbar' (aufhebbar, Rezeptzutat). "
				               "Wirkt auf neu erzeugte Zonen.", SS_LEFT, x, y, 380, 30, -1);

				Bio2Create();
				g_bioGroup = 0;
				BioFillList("");
				BioToUi(BiomeGen::ExampleBiome(0));
				SetWindowTextA(b_name, "neues-biom"); BSetChk(b_enabled, false);
				BioRefreshStatus();
				g_bioSX = g_bioSY = 0; BioShowPage(1);
				SetTimer(h, BID_TIMER, 500, NULL);
				return 0;
			}
			case WM_TIMER: if (wp == BID_TIMER && IsWindowVisible(h)) BioRefreshStatus(); return 0;
			case WM_NOTIFY: BioFloraNotify((NMHDR*)lp); Bio2Notify((NMHDR*)lp); return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (id >= BID_COL_FIRST && id < BID_COL_FIRST + (int)g_colRows.size()) {
					HWND* e = g_colRows[id - BID_COL_FIRST];
					BiomeGen::RGB c = BGetRGB(e);
					static COLORREF custom[16];
					CHOOSECOLORA cc; memset(&cc, 0, sizeof(cc)); cc.lStructSize = sizeof(cc); cc.hwndOwner = h;
					cc.rgbResult = RGB(c.r, c.g, c.b); cc.lpCustColors = custom; cc.Flags = CC_FULLOPEN | CC_RGBINIT;
					if (ChooseColorA(&cc)) { SetInt(e[0], GetRValue(cc.rgbResult)); SetInt(e[1], GetGValue(cc.rgbResult)); SetInt(e[2], GetBValue(cc.rgbResult)); }
					return 0;
				}
				if (id == BID_LVLMODE && code == CBN_SELCHANGE) { BioLvlEnable(); return 0; }
				if (id == BID2_LVMODE && code == CBN_SELCHANGE) { Bio2LvlEnable(); return 0; }
				switch (id) {
					case BID_PAGE1:
						if (g_bioNative) { Say("Die native Welt hat keine eigene Geografie -- fuer Biome I links ein eigenes Biom waehlen."); break; }
						BioShowPage(1); break;
					case BID_PAGE2: BioShowPage(2); break;
					case BID2_ADD: case BID2_SET: Bio2Store(id == BID2_SET); break;
					case BID2_DEL: {
						int i = ListView_GetNextItem(b2_rules, -1, LVNI_SELECTED);
						if (i < 0 || i >= (int)g_rules2.size()) { Say("Erst eine Regel in der Liste waehlen."); break; }
						g_rules2.erase(g_rules2.begin() + i); Bio2Show(); Say("Regel entfernt -- 'Speichern + laden' nicht vergessen."); break;
					}
					case BID2_TEST: Bio2Test(); break;
					case BID2_ALLGROUPS: {
						int n = 0;
						for (const Wildlife::GroupInfo& g : Wildlife::GroupList()) {
							bool dup = false; for (const BiomeGen::SpawnRule& r : g_rules2) if (r.what == g.key) dup = true;
							if (dup || !g.count) continue;
							BiomeGen::SpawnRule r; r.what = g.key; r.weight = g.key.find("/fantasie") != std::string::npos ? 2 : 10;
							g_rules2.push_back(r); n++;
						}
						Bio2Show(); Say(std::to_string(n) + " Tiergruppen eingetragen (ueberall, Gewicht 10, Fantasie 2) -- Gewichte nach Wunsch anpassen, dann 'Speichern + laden'.");
						break;
					}
					case BID_FLW_SET: BioFloraSetWeight(); break;
					case BID_SITES_NEAR:
						LabBridge::Post("Native Orte", [](cube::GameController* gc) { std::cout << "[Biom] " << BiomeMod::NativeSitesNear(gc, 6000) << std::endl; });
						Say("Liste kommt im Logger (LAB > Logger) und in AdventureMod_log.txt.");
						break;
					case BID_LIST: if (code == LBN_SELCHANGE) BioLoadSelected(); break;
					case BID_CITYEXP: { std::string r; City::ExportModels(r); Say("Stadt: " + r); break; }
					case BID_NEW: { BiomeGen::BiomeDef d; d.enabled = false; g_bioFile.clear(); g_bioNative = false; g_bioNC = 0; BioToUi(d); BioTitle(); Say("Neues Biom -- Name vergeben, Werte setzen, 'Speichern + laden'."); break; }
					case BID_EX0: case BID_EX1: case BID_EX2: case BID_EX3: case BID_EX4: {
						BiomeGen::BiomeDef d = BiomeGen::ExampleBiome(id == BID_EX4 ? 4 : id - BID_EX0); d.enabled = false; g_bioFile.clear(); g_bioNative = false; g_bioNC = 0; BioToUi(d); BioTitle();
						Say("Vorlage '" + d.name + "' geladen (noch nicht gespeichert, 'aktiv' ist aus).");
						break;
					}
					case BID_SAVE: BioSave(); break;
					case BID_DELETE: {
						int i = BioSelFile();
						std::vector<std::string> files = BiomeGen::ListBiomeFiles();
						if (i < 0 || i >= (int)files.size()) { Say("Erst ein eigenes Biom in der Liste waehlen (die native Welt laesst sich nicht loeschen)."); break; }
						std::string q = "Biom-Datei '" + files[i] + "' loeschen?\n\nBereits erzeugte Zonen behalten ihr Aussehen bis zum Neuladen.";
						if (MessageBoxA(h, q.c_str(), "Biom loeschen", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((BiomeGen::BiomesDir() + "\\" + files[i]).c_str());
						BiomeMod::ReloadDefinitions(); BioFillList(""); Say("Geloescht: " + files[i]);
						break;
					}
					case BID_RELOAD: BiomeMod::ReloadDefinitions(); BioFillList(g_bioFile); Say("Biome neu eingelesen."); break;
					case BID_ALL: BiomeMod::SetEnabled(!BiomeMod::IsEnabled()); BioRefreshStatus();
						Say(BiomeMod::IsEnabled() ? "Biome AN." : "Biome AUS -- neu erzeugte Zonen sind wieder Vanilla."); break;
					case BID_UNFORCE:
						LabBridge::Post("Biom aufheben", [](cube::GameController* gc) {
							std::cout << "[Biom] " << BiomeMod::ForceHere(gc, -1, true) << std::endl;
						});
						break;
					case BID_NEAREST: case BID_TELEPORT: case BID_FORCE: {
						int i = BioSelFile();
						if (i < 0) { Say("Erst ein eigenes Biom in der Liste waehlen."); break; }
						// Listenindex -> Index in BiomeMod::GetDefinitions() ueber den Dateinamen
						std::vector<std::string> files = BiomeGen::ListBiomeFiles(), defs = BiomeMod::GetDefinitionFiles();
						int di = -1;
						if (i < (int)files.size()) for (size_t k = 0; k < defs.size(); k++) if (_stricmp(defs[k].c_str(), files[i].c_str()) == 0) di = (int)k;
						if (di < 0) { Say("Dieses Biom ist im Spiel nicht geladen -- 'Speichern + laden'."); break; }
						int what = id == BID_TELEPORT ? 1 : (id == BID_FORCE ? 2 : 0);
						LabBridge::Post(what == 1 ? "Biom-Teleport" : what == 2 ? "Biom erzwingen" : "Biom suchen", [di, what](cube::GameController* gc) {
							std::string r = what == 1 ? BiomeMod::TeleportToNearest(gc, di) : what == 2 ? BiomeMod::ForceHere(gc, di, false) : BiomeMod::FindNearest(gc, di);
							std::cout << "[Biom] " << r << std::endl;
						});
						break;
					}
					case BID_RACE_ADD: BioAppend(b_races, std::to_string(ComboSel(b_racePick))); break;
					case BID_ANIMAL_ADD: {
						char b[64] = ""; int sel = ComboSel(b_animalPick);
						if (sel >= 0) { SendMessageA(b_animalPick, CB_GETLBTEXT, sel, (LPARAM)b); BioAppend(b_cityAnimals, std::to_string(atoi(b))); }
						break;
					}
					case BID_MOB_ADD: case BID_DMOB_ADD: {
						std::string n;
						if (BioPickFile(MobLab::MobsDir(), "Mob (*.mob)\0*.mob\0", n)) BioAppend(id == BID_MOB_ADD ? b_mobs : b_dMobs, n);
						break;
					}
					case BID_PLANT_EDIT: case BID_PLANT_SET: {
						BiomeGen::BiomeDef d = BioFromUi();
						int kind = ComboSel(b_kind);
						uint32_t seed = GetTickCount();
						if (id == BID_PLANT_EDIT) {
							if (!ConfirmDiscard()) break;
							Lab::Structure s = BiomeGen::MakePlant(d, kind, seed);
							s.name = d.name + "-" + BiomeGen::PlantKindName(kind);
							SetCurrent(s, true);
							Say("Pflanze '" + s.name + "' im Editor. Gespeichert als .cub laesst sie sich unter 'Eigene .cub' eintragen.");
						} else {
							LabBridge::Post("Biom-Pflanze", [d, kind, seed](cube::GameController* gc) {
								std::cout << "[Biom] Test-Pflanze: " << BiomeMod::PlacePlantHere(gc, d, kind, seed) << std::endl;
							});
						}
						break;
					}
					case BID_DUN_EDIT: case BID_DUN_SET: {
						BiomeGen::BiomeDef d = BioFromUi();
						uint32_t seed = GetTickCount();
						if (id == BID_DUN_EDIT) {
							if (!ConfirmDiscard()) break;
							BiomeGen::DungeonPlan p = BiomeGen::MakeDungeon(d, seed);
							p.s.name = d.name + "-dungeon"; p.s.fundament = 1;
							SetCurrent(p.s, true);
							Say("Dungeon im Editor (" + DimStr(p.s.sx, p.s.sy, p.s.sz) + ", " + std::to_string(p.rooms.size()) + " Raeume, Eingang Suedseite).");
						} else {
							LabBridge::Post("Biom-Dungeon", [d, seed](cube::GameController* gc) {
								std::cout << "[Biom] Test-Dungeon: " << BiomeMod::PlaceDungeonHere(gc, d, seed) << std::endl;
							});
						}
						break;
					}
					case BID_SITES_DUN_OFF: {
						uint32_t dun = BiomeGen::DungeonSiteBits();
						for (int i = 0; i < BiomeGen::kSiteKindCount; i++) if (dun & (1u << BiomeGen::kSiteKinds[i])) BSetChk(b_site[i], false);
						break;
					}
					case BID_SITES_ALL_ON: for (int i = 0; i < BiomeGen::kSiteKindCount; i++) BSetChk(b_site[i], true); BSetChk(b_bergdorf, true); break;
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}


	// =====================================================================
	// 7e. Zonen-Modelle: eigene .cub als Baum / Blume / Dungeon, erzeugt mit der Zone
	// =====================================================================
	// Die Modelle entstehen im Zonen-Thread mit der Zone, wie die nativen Baeume (BiomeMod,
	// Hook_SunCall). Das Fenster bearbeitet nur die Dateien Strukturen\Zonenmodelle\*.zmod.
	namespace {
		HWND g_zmWnd = nullptr;
		HWND z_small, z_wind;                     // Blume: klein wie die nativen Pflanzen (Faktor), Wind
		HWND z_itemName;                          // Sammelbar: Name des eigenen Gegenstands
		int  g_zmItemId = 0;                      // Sammelbar: feste Unterart der bearbeiteten Datei
		HWND z_list, z_status, z_name, z_on, z_kind, z_cub, z_area, z_ground, z_dens, z_smin, z_smax, z_space, z_zoff, z_hmin, z_hmax, z_uneven, z_rot, z_fund, z_dims;
		std::string g_zmFile;                     // aktuell bearbeitete Datei (nur Name)
		std::vector<std::string> g_zmAreas;       // Eintraege der Gebietsauswahl
		const int kZGround[5] = { 0, 4, 9, 10, 12 };
		enum { ZID_LIST = 6001, ZID_NEW0, ZID_NEW1, ZID_NEW2, ZID_SAVE, ZID_DEL, ZID_RELOAD, ZID_PICK, ZID_TAKE, ZID_TEST, ZID_NEW3, ZID_TIMER = 12 };
	}
	static std::string ZmSafeName(std::string n) {
		for (char& c : n) if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) c = '_';
		return n.empty() ? std::string("modell") : n;
	}
	static void ZmFillAreas(const std::string& select) {
		g_zmAreas.clear();
		SendMessageA(z_area, CB_RESETCONTENT, 0, 0);
		g_zmAreas.push_back("ueberall"); SendMessageA(z_area, CB_ADDSTRING, 0, (LPARAM)"Ueberall in der Welt");
		g_zmAreas.push_back("vanilla");  SendMessageA(z_area, CB_ADDSTRING, 0, (LPARAM)"Nur Vanilla (ohne eigenes Biom)");
		for (const std::string& f : BiomeGen::ListBiomeFiles()) {
			g_zmAreas.push_back("biom:" + f);
			std::string t = "Biom: " + f;
			SendMessageA(z_area, CB_ADDSTRING, 0, (LPARAM)t.c_str());
		}
		int sel = 0;
		for (size_t i = 0; i < g_zmAreas.size(); i++) if (_stricmp(g_zmAreas[i].c_str(), select.c_str()) == 0) sel = (int)i;
		if (sel == 0 && select.compare(0, 5, "biom:") == 0) {   // Biom-Datei fehlt -> trotzdem anzeigen
			g_zmAreas.push_back(select); std::string t = "Biom: " + select.substr(5) + " (fehlt)";
			SendMessageA(z_area, CB_ADDSTRING, 0, (LPARAM)t.c_str()); sel = (int)g_zmAreas.size() - 1;
		}
		SendMessageA(z_area, CB_SETCURSEL, sel, 0);
	}
	static void ZmToUi(const BiomeGen::ZoneModelDef& d) {
		SetWindowTextA(z_name, d.name.c_str()); BSetChk(z_on, d.enabled);
		SendMessageA(z_kind, CB_SETCURSEL, d.kind, 0);
		SetWindowTextA(z_cub, d.cub.c_str());
		ZmFillAreas(d.area);
		int g = 0; for (int i = 0; i < 5; i++) if (kZGround[i] == d.ground) g = i;
		SendMessageA(z_ground, CB_SETCURSEL, g, 0);
		SetInt(z_dens, d.density); BSetF(z_smin, d.slopeMin); BSetF(z_smax, d.slopeMax);
		SetInt(z_space, d.spacing); SetInt(z_zoff, d.zOffset); SetInt(z_hmin, d.hMin); SetInt(z_hmax, d.hMax);
		SetInt(z_uneven, d.uneven); BSetChk(z_rot, d.rotate); BSetChk(z_fund, d.foundation);
		{ char b[32]; sprintf_s(b, "%.3f", d.smallScale); SetWindowTextA(z_small, b); } BSetChk(z_wind, d.wind);
		SetWindowTextA(z_itemName, d.itemName.c_str()); g_zmItemId = d.itemId;
	}
	static BiomeGen::ZoneModelDef ZmFromUi() {
		BiomeGen::ZoneModelDef d;
		d.name = GetText(z_name); if (d.name.empty()) d.name = "modell";
		d.enabled = Checked(z_on);
		int k = (int)SendMessageA(z_kind, CB_GETCURSEL, 0, 0); d.kind = k < 0 ? 0 : k;
		d.cub = GetText(z_cub);
		int a = (int)SendMessageA(z_area, CB_GETCURSEL, 0, 0); d.area = (a >= 0 && a < (int)g_zmAreas.size()) ? g_zmAreas[a] : "ueberall";
		int g = (int)SendMessageA(z_ground, CB_GETCURSEL, 0, 0); d.ground = (g >= 0 && g < 5) ? kZGround[g] : 0;
		auto cl = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
		d.density = (std::max)(0, (std::min)(2000, GetInt(z_dens, 10)));
		d.slopeMin = cl(BFloat(z_smin, 0), 0, 5); d.slopeMax = cl(BFloat(z_smax, 1.4f), 0, 5);
		d.spacing = (std::max)(0, (std::min)(200, GetInt(z_space, 12)));
		d.zOffset = (std::max)(-64, (std::min)(64, GetInt(z_zoff, 0)));
		d.hMin = GetInt(z_hmin, -1000); d.hMax = GetInt(z_hmax, 1000);
		d.uneven = (std::max)(0, (std::min)(64, GetInt(z_uneven, 6)));
		d.rotate = Checked(z_rot); d.foundation = Checked(z_fund);
		{ float f = BFloat(z_small, 0.0f); d.smallScale = f <= 0.0f ? 0.0f : cl(f, 0.01f, 1.0f); }
		d.wind = Checked(z_wind);
		d.itemName = GetText(z_itemName); d.itemId = g_zmItemId;
		return d;
	}
	static void ZmFillList(const std::string& select) {
		SendMessageA(z_list, LB_RESETCONTENT, 0, 0);
		std::vector<std::string> files = BiomeGen::ListZoneModelFiles();
		int sel = -1;
		for (size_t i = 0; i < files.size(); i++) {
			SendMessageA(z_list, LB_ADDSTRING, 0, (LPARAM)files[i].c_str());
			if (_stricmp(files[i].c_str(), select.c_str()) == 0) sel = (int)i;
		}
		if (sel >= 0) SendMessageA(z_list, LB_SETCURSEL, sel, 0);
	}
	static void ZmRefreshStatus() {
		std::ostringstream o;
		std::vector<BiomeMod::ZoneModelInfo> inf = BiomeMod::GetZoneModelInfo();
		o << "Geladen: " << inf.size() << " Zonen-Modelle\r\n";
		for (const auto& i : inf) {
			o << (i.def.enabled ? "  " : "  (aus) ") << i.file << ": " << BiomeGen::ZoneModelKindName(i.def.kind);
			if (i.modelOk) o << " " << i.sx << "x" << i.sy << "x" << i.sz << ", gesetzt " << i.placed;
			if (!i.note.empty()) o << "  -- " << i.note;
			o << "\r\n";
		}
		std::string t = o.str();
		char cur[4096]; GetWindowTextA(z_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(z_status, t.c_str());
	}
	static void ZmShowDims() {
		Lab::Structure s; std::string err;
		if (BiomeMod::LoadZoneModelCub(GetText(z_cub), s, err)) {
			char b[96]; sprintf_s(b, "Modell: %d x %d x %d Bloecke", s.sx, s.sy, s.sz); SetWindowTextA(z_dims, b);
		} else SetWindowTextA(z_dims, ("Modell: " + err).c_str());
	}
	static void ZmLoadSelected() {
		int i = (int)SendMessageA(z_list, LB_GETCURSEL, 0, 0); if (i < 0) return;
		char b[MAX_PATH] = ""; SendMessageA(z_list, LB_GETTEXT, i, (LPARAM)b);
		BiomeGen::ZoneModelDef d; std::string err;
		if (!BiomeGen::LoadZoneModel(BiomeGen::ZoneModelsDir() + "\\" + b, d, err)) { Say(err); return; }
		g_zmFile = b; ZmToUi(d); ZmShowDims();
	}
	static void ZmSave() {
		BiomeGen::ZoneModelDef d = ZmFromUi();
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(BiomeGen::ZoneModelsDir());
		std::string file = ZmSafeName(d.name) + ".zmod", err;
		if (d.kind == BiomeGen::ZM_PICKUP) {
			if (d.itemName.empty()) d.itemName = d.name;
			if (!d.itemId) {                                          // erste freie Unterart 0x30..0x4F vergeben
				bool used[BiomeGen::kPickupCount] = {};
				for (const std::string& f : BiomeGen::ListZoneModelFiles()) {
					if (_stricmp(f.c_str(), g_zmFile.c_str()) == 0) continue;
					BiomeGen::ZoneModelDef o; std::string e2;
					if (BiomeGen::LoadZoneModel(BiomeGen::ZoneModelsDir() + "\\" + f, o, e2) && o.kind == BiomeGen::ZM_PICKUP && o.itemId)
						used[o.itemId - BiomeGen::kPickupFirstId] = true;
				}
				for (int i = 0; i < BiomeGen::kPickupCount && !d.itemId; i++) if (!used[i]) d.itemId = BiomeGen::kPickupFirstId + i;
				if (!d.itemId) { Say("Sammelbar: alle 32 Gegenstands-Nummern sind vergeben."); return; }
				g_zmItemId = d.itemId;
			}
		}
		if (!BiomeGen::SaveZoneModel(BiomeGen::ZoneModelsDir() + "\\" + file, d, err)) { Say(err); return; }
		if (!g_zmFile.empty() && _stricmp(g_zmFile.c_str(), file.c_str()) != 0) DeleteFileA((BiomeGen::ZoneModelsDir() + "\\" + g_zmFile).c_str());
		g_zmFile = file;
		BiomeMod::ReloadZoneModels();
		ZmFillList(file); ZmShowDims(); ZmRefreshStatus();
		Lab::Structure s;
		if (!BiomeMod::LoadZoneModelCub(d.cub, s, err)) Say("Zonen-Modell gespeichert, aber: " + err);
		else Say("Zonen-Modell '" + d.name + "' gespeichert und geladen. Es erscheint in neu erzeugten Zonen.");
	}
	static LRESULT CALLBACK ZoneModelProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_zmWnd = h; g_bParent = h;
				int x = 10, y = 10;
				BLbl("Zonen-Modelle  (Strukturen\\Zonenmodelle\\*.zmod)", x, y, 300, true); y += 20;
				z_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 300, 220, ZID_LIST, WS_EX_CLIENTEDGE); y += 226;
				BBtn("Neu: Baum", x, y, 72, ZID_NEW0); BBtn("Neu: Blume", x + 76, y, 72, ZID_NEW1); BBtn("Neu: Dungeon", x + 152, y, 72, ZID_NEW2);
				BBtn("Sammelbar", x + 228, y, 72, ZID_NEW3); y += 28;
				BBtn("Speichern + laden", x, y, 150, ZID_SAVE); BBtn("Loeschen", x + 154, y, 70, ZID_DEL); BBtn("Neu einlesen", x + 228, y, 72, ZID_RELOAD); y += 32;
				z_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 300, 200, -1, WS_EX_CLIENTEDGE);

				x = 330; y = 10;
				BLbl("Modell und Regeln", x, y, 300, true); y += 22;
				BLbl("Name:", x, y + 4, 110); z_name = BEdt(x + 112, y, 200); z_on = BChk("aktiv", x + 324, y + 1, 70); y += 28;
				BLbl("Art:", x, y + 4, 110);
				z_kind = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 112, y, 140, 200, -1);
				for (int i = 0; i < BiomeGen::ZM_COUNT; i++) SendMessageA(z_kind, CB_ADDSTRING, 0, (LPARAM)BiomeGen::ZoneModelKindName(i));
				y += 28;
				BLbl("Modell (.cub):", x, y + 4, 110); z_cub = BEdt(x + 112, y, 230); BBtn("...", x + 346, y, 30, ZID_PICK, 22); y += 26;
				BBtn("Aktuelles LAB-Modell uebernehmen", x + 112, y, 264, ZID_TAKE); y += 28;
				z_dims = BCtl("STATIC", "Modell: -", SS_LEFT, x + 112, y, 300, 16, -1); y += 22;
				BLbl("Blume klein:", x, y + 4, 110); z_small = BEdt(x + 112, y, 50);
				BLbl("x Modell (0 = Bloecke)", x + 168, y + 4, 130); z_wind = BChk("Wind", x + 300, y + 1, 70); y += 28;
				BLbl("Gegenstand-Name:", x, y + 4, 110); z_itemName = BEdt(x + 112, y, 200);
				BLbl("(nur Sammelbar)", x + 318, y + 4, 100); y += 30;
				BLbl("Gebiet:", x, y + 4, 110);
				z_area = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 112, y, 264, 300, -1); y += 28;
				BLbl("Boden:", x, y + 4, 110);
				z_ground = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 112, y, 160, 200, -1);
				{ const char* gn[5] = { "jeder Naturboden", "Gras", "Sand", "Schnee", "Lava-Boden" }; for (int i = 0; i < 5; i++) SendMessageA(z_ground, CB_ADDSTRING, 0, (LPARAM)gn[i]); }
				y += 28;
				BLbl("Dichte:", x, y + 4, 110); z_dens = BEdt(x + 112, y, 50); BLbl("Baum/Blume: Versuche je Zone; Dungeon: % je Zone", x + 168, y + 4, 300); y += 26;
				BLbl("Hang min / max:", x, y + 4, 110); z_smin = BEdt(x + 112, y, 50); z_smax = BEdt(x + 168, y, 50); BLbl("Bloecke Hoehe je Block", x + 224, y + 4, 200); y += 26;
				BLbl("Abstand:", x, y + 4, 110); z_space = BEdt(x + 112, y, 50); BLbl("z-Versatz:", x + 180, y + 4, 70); z_zoff = BEdt(x + 252, y, 50); y += 26;
				BLbl("Bodenhoehe min/max:", x, y + 4, 110); z_hmin = BEdt(x + 112, y, 50); z_hmax = BEdt(x + 168, y, 50); y += 26;
				BLbl("Unebenheit:", x, y + 4, 110); z_uneven = BEdt(x + 112, y, 50); BLbl("(Dungeon: max. Hoehenunterschied)", x + 168, y + 4, 230); y += 26;
				z_rot = BChk("zufaellig drehen", x + 112, y, 140); z_fund = BChk("Fundament bis zum Boden", x + 256, y, 190); y += 30;
				BBtn("Test: einmal am Spieler (+40 Bloecke)", x + 112, y, 264, ZID_TEST); y += 34;
				BCtl("STATIC",
				     "Die Modelle entstehen zusammen mit der Zone, wie die nativen Baeume -- kein Aufpoppen. "
				     "Blume klein > 0: grosses Modell verkleinert wie Schilf (0.09). Sammelbar: eigener aufhebbarer "
				     "Gegenstand wie die Herzblume; 'Blume klein' = Groesse in der Welt (0.1). Gilt fuer neue Zonen.",
				     SS_LEFT, x, y, 440, 64, -1);
				g_bParent = nullptr;
				ZmFillList("");
				BiomeGen::ZoneModelDef d = BiomeGen::DefaultZoneModel(BiomeGen::ZM_TREE); d.enabled = false;
				ZmToUi(d);
				ZmRefreshStatus();
				SetTimer(h, ZID_TIMER, 1000, NULL);
				return 0;
			}
			case WM_TIMER: if (wp == ZID_TIMER && IsWindowVisible(h)) ZmRefreshStatus(); return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				switch (id) {
					case ZID_LIST: if (code == LBN_SELCHANGE) ZmLoadSelected(); break;
					case ZID_NEW0: case ZID_NEW1: case ZID_NEW2: case ZID_NEW3: {
						BiomeGen::ZoneModelDef d = BiomeGen::DefaultZoneModel(id == ZID_NEW3 ? BiomeGen::ZM_PICKUP : id - ZID_NEW0);
						d.name = std::string("neu-") + BiomeGen::ZoneModelKindName(d.kind); d.cub = ""; g_zmFile.clear();
						ZmToUi(d); SetWindowTextA(z_dims, "Modell: -");
						Say("Neues Zonen-Modell: Name, Modell (.cub oder aktuelles LAB-Modell) und Gebiet waehlen, dann 'Speichern + laden'.");
						break;
					}
					case ZID_SAVE: ZmSave(); break;
					case ZID_DEL: {
						if (g_zmFile.empty()) break;
						std::string f = g_zmFile;
						if (MessageBoxA(h, ("Zonen-Modell '" + f + "' loeschen? (Die .cub bleibt erhalten.)").c_str(), "Zonen-Modelle", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((BiomeGen::ZoneModelsDir() + "\\" + f).c_str()); g_zmFile.clear();
						BiomeMod::ReloadZoneModels(); ZmFillList(""); ZmRefreshStatus(); Say("Zonen-Modell '" + f + "' geloescht.");
						break;
					}
					case ZID_RELOAD: BiomeMod::ReloadZoneModels(); ZmFillList(g_zmFile); ZmFillAreas(ZmFromUi().area); ZmRefreshStatus(); break;
					case ZID_PICK: {
						char file[MAX_PATH] = ""; char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
						std::string dir = std::string(cwd) + "\\" + BiomeGen::ZoneModelsDir(), sdir = std::string(cwd) + "\\" + Lab::StructuresDir();
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
						ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = sdir.c_str();
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (!GetOpenFileNameA(&ofn)) break;
						std::string p = file, name = p;
						if (_strnicmp(p.c_str(), dir.c_str(), dir.size()) == 0 && p.size() > dir.size() + 1) name = p.substr(dir.size() + 1);
						else if (_strnicmp(p.c_str(), sdir.c_str(), sdir.size()) == 0 && p.size() > sdir.size() + 1 && p.find('\\', sdir.size() + 1) == std::string::npos) name = p.substr(sdir.size() + 1);
						SetWindowTextA(z_cub, name.c_str()); ZmShowDims();
						break;
					}
					case ZID_TAKE: {
						if (!g_cur.Valid()) { Say("Im LAB-Editor ist kein Modell offen."); break; }
						Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(BiomeGen::ZoneModelsDir());
						std::string name = ZmSafeName(GetText(z_name)) + ".cub", err;
						if (!Lab::SaveCub(BiomeGen::ZoneModelsDir() + "\\" + name, g_cur, err)) { Say(err); break; }
						SetWindowTextA(z_cub, name.c_str()); ZmShowDims();
						Say("Aktuelles LAB-Modell als Strukturen\\Zonenmodelle\\" + name + " gespeichert. Jetzt 'Speichern + laden'.");
						break;
					}
					case ZID_TEST: {
						BiomeGen::ZoneModelDef d = ZmFromUi();
						Lab::Structure s; std::string err;
						if (!BiomeMod::LoadZoneModelCub(d.cub, s, err)) { Say(err); break; }
						LabBridge::Post("Zonen-Modell testen", [d, s](cube::GameController* gc) {
							std::cout << "[Zonen-Modelle] " << BiomeMod::PlaceZoneModelHere(gc, d, s) << std::endl;
						});
						break;
					}
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7f. Rezepte: eigene Handwerksrezepte (Recipes.cpp)
	// =====================================================================
	namespace {
		HWND g_rzWnd = nullptr;
		HWND r_list, r_status, r_name, r_on, r_learn, r_outSel, r_outT, r_outS, r_outM, r_outLvl, r_outRar, r_station;
		HWND r_ingSel[6], r_ingT[6], r_ingS[6], r_ingM[6], r_ingN[6];
		HWND r_fx, r_fxVal, r_fxUnit, r_fxSec, r_fxHeal;
		HWND r_use, r_model, r_modelInfo;             // Ergebnis: Verbrauch (Anzeige), eigenes Modell
		int  g_rzEffectId = 0;                       // Kennung des geladenen Rezepts (bleibt beim Speichern)
		std::string g_rzFile;
		struct RChoice { std::string name; int t, s, m; int item = 0; };
		std::vector<RChoice> g_rzChoices;            // Auswahl fuer Ergebnis und Zutaten
		enum { RID_LIST = 7001, RID_NEW, RID_SAVE, RID_DEL, RID_RELOAD, RID_OUTSEL, RID_FX, RID_MODEL, RID_MODELCLR, RID_ING0 = 7050, RID_TIMER = 13 };
	}
	static void RzBuildChoices() {
		g_rzChoices.clear();
		g_rzChoices.push_back({ "(von Hand)", -1, -1, -1 });
		// Eigene sammelbare Pflanzen (Zonen-Modelle, Art "Sammelbar") -- immer der aktuelle Stand: die Liste wird
		// beim Aufklappen und beim Wechsel auf diesen Reiter neu gebaut (RzRefreshChoices).
		for (const auto& i : BiomeMod::GetZoneModelInfo())
			if (i.def.kind == BiomeGen::ZM_PICKUP && i.def.itemId) g_rzChoices.push_back({ "Eigen (sammelbar): " + (i.def.itemName.empty() ? i.file : i.def.itemName), 0x0B, i.def.itemId, 0 });
		for (const Items::ItemDef& d : Items::All())                   // eigene Gegenstaende (LAB -> Gegenstaende)
			if (d.enabled && d.id > 0) g_rzChoices.push_back({ "Gegenstand: " + d.name, d.type, d.sub, d.mat, d.id });
		int n = 0; const Recipes::Named* p = Recipes::Presets(&n);
		for (int k = 0; k < n; k++) g_rzChoices.push_back({ p[k].name, p[k].type, p[k].sub, p[k].mat });
	}
	static void RzFillCombo(HWND c) {
		SendMessageA(c, CB_RESETCONTENT, 0, 0);
		for (const RChoice& r : g_rzChoices) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)r.name.c_str());
	}
	static void RzSelectFor(HWND c, int t, int sub, int m, int item = 0) {
		int sel = 0;
		for (size_t i = 1; i < g_rzChoices.size(); i++) if (g_rzChoices[i].t == t && g_rzChoices[i].s == sub && g_rzChoices[i].m == m && g_rzChoices[i].item == item) { sel = (int)i; break; }
		SendMessageA(c, CB_SETCURSEL, sel, 0);
	}
	// Eigener Gegenstand der Zeile: nur wenn die Auswahl einer ist und Typ/Unterart/Material noch dazu passen
	static int RzItemOf(HWND c, HWND t, HWND s, HWND m) {
		int sel = (int)SendMessageA(c, CB_GETCURSEL, 0, 0);
		if (sel <= 0 || sel >= (int)g_rzChoices.size()) return 0;
		const RChoice& r = g_rzChoices[sel];
		if (r.item <= 0 || r.t != GetInt(t, -1) || r.s != GetInt(s, -1) || r.m != GetInt(m, -1)) return 0;
		return r.item;
	}
	// Ergebnis: wie wird es benutzt (aus Typ/Unterart/Wirkung) und welches Modell zeigt es
	static void RzShowUse() {
		int t = GetInt(r_outT, -1), s = GetInt(r_outS, -1);
		int fx = (int)SendMessageA(r_fx, CB_GETCURSEL, 0, 0);
		std::string u = std::string("Verbrauch: ") + Recipes::UseText(t, s);
		if (fx > Recipes::FX_NONE && fx < Recipes::FX_COUNT) u += std::string(" -> Wirkung: ") + Recipes::EffectName(fx);
		std::string mf = GetText(r_model), mi;
		if (mf.empty()) mi = "Aussehen: Modell des Spiels fuer diesen Gegenstand";
		else {
			Lab::Structure s0; std::string err;
			if (Lab::LoadCub(Recipes::ModelPath(mf), s0, err)) {
				char b[160]; sprintf_s(b, "Aussehen: eigenes Modell %dx%dx%d%s", s0.sx, s0.sy, s0.sz, (s0.sx > 64 || s0.sy > 64 || s0.sz > 64) ? " -- ZU GROSS (max 64)" : " (Inventar, Hand, Boden)");
				mi = b;
			} else mi = "Aussehen: Datei nicht lesbar -- " + err;
		}
		char cur[512];
		GetWindowTextA(r_use, cur, sizeof(cur)); if (u != cur) SetWindowTextA(r_use, u.c_str());
		GetWindowTextA(r_modelInfo, cur, sizeof(cur)); if (mi != cur) SetWindowTextA(r_modelInfo, mi.c_str());
	}
	// Auswahllisten neu aufbauen (neue sammelbare Pflanzen), Auswahl aus den Feldern Typ/Unterart/Material wiederherstellen
	static void RzRefreshChoices() {
		if (!g_rzWnd) return;
		int keepOut = RzItemOf(r_outSel, r_outT, r_outS, r_outM), keepIng[6];
		for (int i = 0; i < 6; i++) keepIng[i] = RzItemOf(r_ingSel[i], r_ingT[i], r_ingS[i], r_ingM[i]);
		RzBuildChoices();
		RzFillCombo(r_outSel);
		if (!GetText(r_outT).empty()) RzSelectFor(r_outSel, GetInt(r_outT, -1), GetInt(r_outS, -1), GetInt(r_outM, -1), keepOut); else SendMessageA(r_outSel, CB_SETCURSEL, 0, 0);
		for (int i = 0; i < 6; i++) {
			RzFillCombo(r_ingSel[i]);
			if (!GetText(r_ingT[i]).empty()) RzSelectFor(r_ingSel[i], GetInt(r_ingT[i], -1), GetInt(r_ingS[i], -1), GetInt(r_ingM[i], -1), keepIng[i]);
			else SendMessageA(r_ingSel[i], CB_SETCURSEL, 0, 0);
		}
	}
	static void RzToUi(const Recipes::RecipeDef& d) {
		RzBuildChoices();
		SetWindowTextA(r_name, d.name.c_str()); BSetChk(r_on, d.enabled); BSetChk(r_learn, d.learnAtStart);
		RzFillCombo(r_outSel); RzSelectFor(r_outSel, d.outType, d.outSub, d.outMat, d.outItem);
		SetHex(r_outT, d.outType); SetHex(r_outS, d.outSub); SetHex(r_outM, d.outMat); SetInt(r_outLvl, d.outLevel); SetInt(r_outRar, d.outRarity);
		int st = 0; for (int i = 0; i < 9; i++) if (Recipes::kStations[i] == d.station) st = i;
		SendMessageA(r_station, CB_SETCURSEL, st, 0);
		SendMessageA(r_fx, CB_SETCURSEL, d.effect, 0);
		SetWindowTextA(r_fxUnit, Recipes::EffectUnit(d.effect));
		if (d.effect != Recipes::FX_NONE && d.effect != Recipes::FX_HEAL) { char b[32]; sprintf_s(b, "%g", d.effectValue); SetWindowTextA(r_fxVal, b); SetInt(r_fxSec, d.effectSeconds); }
		else { SetWindowTextA(r_fxVal, ""); SetWindowTextA(r_fxSec, ""); }
		BSetChk(r_fxHeal, d.effectAlsoHeal);
		SetWindowTextA(r_model, d.modelFile.c_str());
		g_rzEffectId = d.effectId;
		for (int i = 0; i < 6; i++) {
			RzFillCombo(r_ingSel[i]);
			if (i < (int)d.ing.size()) {
				RzSelectFor(r_ingSel[i], d.ing[i].type, d.ing[i].sub, d.ing[i].mat, d.ing[i].item);
				SetHex(r_ingT[i], d.ing[i].type); SetHex(r_ingS[i], d.ing[i].sub); SetHex(r_ingM[i], d.ing[i].mat); SetInt(r_ingN[i], d.ing[i].count);
			} else {
				SendMessageA(r_ingSel[i], CB_SETCURSEL, 0, 0);
				SetWindowTextA(r_ingT[i], ""); SetWindowTextA(r_ingS[i], ""); SetWindowTextA(r_ingM[i], ""); SetWindowTextA(r_ingN[i], "");
			}
		}
	}
	static Recipes::RecipeDef RzFromUi() {
		Recipes::RecipeDef d; d.ing.clear();
		d.name = GetText(r_name); if (d.name.empty()) d.name = "rezept";
		d.enabled = Checked(r_on); d.learnAtStart = Checked(r_learn);
		d.outType = GetInt(r_outT, 0x0B) & 0xFF; d.outSub = GetInt(r_outS, 0x30) & 0xFF; d.outMat = GetInt(r_outM, 0) & 0xFF;
		d.outLevel = (std::max)(1, (std::min)(65535, GetInt(r_outLvl, 1)));   // Gegenstandslevel: 16 Bit (Item+0x10) d.outRarity = (std::max)(0, (std::min)(4, GetInt(r_outRar, 0)));
		int st = (int)SendMessageA(r_station, CB_GETCURSEL, 0, 0); d.station = Recipes::kStations[(st < 0 || st > 8) ? 0 : st];
		for (int i = 0; i < 6; i++) {
			if (GetText(r_ingT[i]).empty() || GetText(r_ingS[i]).empty()) continue;
			Recipes::Ingredient g; g.type = GetInt(r_ingT[i], 0x0B) & 0xFF; g.sub = GetInt(r_ingS[i], 0) & 0xFF; g.mat = GetInt(r_ingM[i], 0) & 0xFF;
			g.count = (std::max)(1, (std::min)(999, GetInt(r_ingN[i], 1)));
			g.item = RzItemOf(r_ingSel[i], r_ingT[i], r_ingS[i], r_ingM[i]);
			d.ing.push_back(g);
		}
		int fx = (int)SendMessageA(r_fx, CB_GETCURSEL, 0, 0); d.effect = (fx < 0 || fx >= Recipes::FX_COUNT) ? Recipes::FX_NONE : fx;
		if (Recipes::EffectTagged(d.effect)) {
			std::string v = GetText(r_fxVal);
			d.effectValue = v.empty() ? Recipes::EffectDefaultValue(d.effect) : (float)atof(v.c_str());
			d.effectSeconds = (std::max)(1, (std::min)(3600, GetInt(r_fxSec, 120)));
			d.effectId = g_rzEffectId;
		}
		d.effectAlsoHeal = Checked(r_fxHeal);
		d.modelFile = GetText(r_model);
		d.outItem = RzItemOf(r_outSel, r_outT, r_outS, r_outM);
		if (!d.modelFile.empty()) d.effectId = g_rzEffectId;                // Kennung auch fuer eigenes Modell
		Recipes::NormalizeEffect(d);
		return d;
	}
	static void RzFillList(const std::string& sel) {
		SendMessageA(r_list, LB_RESETCONTENT, 0, 0);
		std::vector<std::string> f = Recipes::ListFiles(); int s = -1;
		for (size_t i = 0; i < f.size(); i++) { SendMessageA(r_list, LB_ADDSTRING, 0, (LPARAM)f[i].c_str()); if (_stricmp(f[i].c_str(), sel.c_str()) == 0) s = (int)i; }
		if (s >= 0) SendMessageA(r_list, LB_SETCURSEL, s, 0);
	}
	static void RzRefreshStatus() {
		std::string t = Recipes::Status() + "\r\n" + Effects::Status() +
		                "\r\n\r\nNach dem Speichern lernt der Spieler das Rezept automatisch (wenn 'sofort bekannt'). "
		                "Dann das Handwerksmenue neu oeffnen. Traenke mit Wirkung stehen im 7. Reiter 'Effekte' (Symbol wie Alchemie), "
		                "eigene Gegenstaende im 6. Reiter, Essen/Traenke unter Kochen/Alchemie, Waffen/Ruestung/Schmuck unter ihren Reitern. "
																		                "Die Wirkung beginnt, wenn der Trank ganz ausgetrunken ist (oben rechts im Spiel steht die Restzeit). "
																		                "Eigenes Modell: eine .cub (hoechstens 64x64x64) ersetzt das Aussehen des Ergebnisses im Inventar, in der Hand "
																		                "und am Boden; das Ergebnis bekommt dafuer eine eigene Kennung (eigener Stapel) und wird neu angelernt.";
		char cur[1024]; GetWindowTextA(r_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(r_status, t.c_str());
	}
	static LRESULT CALLBACK RecipeProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_rzWnd = h; g_bParent = h;
				int x = 10, y = 10;
				BLbl("Rezepte  (Strukturen\\Rezepte\\*.rez)", x, y, 260, true); y += 20;
				r_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 260, 240, RID_LIST, WS_EX_CLIENTEDGE); y += 246;
				BBtn("Neu", x, y, 60, RID_NEW); BBtn("Speichern + laden", x + 64, y, 120, RID_SAVE); BBtn("Loeschen", x + 188, y, 72, RID_DEL); y += 28;
				BBtn("Neu einlesen", x, y, 120, RID_RELOAD); y += 32;
				r_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 260, 170, -1, WS_EX_CLIENTEDGE);

				x = 290; y = 10;
				BLbl("Rezept", x, y, 200, true); y += 22;
				BLbl("Name:", x, y + 4, 70); r_name = BEdt(x + 72, y, 180); r_on = BChk("aktiv", x + 262, y + 1, 60); r_learn = BChk("sofort bekannt", x + 326, y + 1, 120); y += 30;
				BLbl("Ergebnis:", x, y + 4, 70);
				r_outSel = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 72, y, 180, 400, RID_OUTSEL);
				BLbl("Typ/Unterart/Mat.:", x + 262, y + 4, 100); r_outT = BEdt(x + 364, y, 40); r_outS = BEdt(x + 408, y, 40); r_outM = BEdt(x + 452, y, 40); y += 28;
				BLbl("Level:", x + 72, y + 4, 40); r_outLvl = BEdt(x + 114, y, 40); BLbl("Seltenheit 0-4:", x + 168, y + 4, 90); r_outRar = BEdt(x + 260, y, 30); y += 26;
				r_use = BCtl("STATIC", "", SS_LEFT, x + 72, y + 2, 430, 18, -1); y += 22;
				BLbl("Modell:", x, y + 4, 70); r_model = BEdt(x + 72, y, 250);
				BBtn("Waehlen ...", x + 328, y, 80, RID_MODEL); BBtn("Spiel-Modell", x + 412, y, 90, RID_MODELCLR); y += 26;
				r_modelInfo = BCtl("STATIC", "", SS_LEFT, x + 72, y + 2, 430, 18, -1); y += 26;
				BLbl("Station:", x, y + 4, 70);
				r_station = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 72, y, 180, 300, -1);
				for (int i = 0; i < 9; i++) SendMessageA(r_station, CB_ADDSTRING, 0, (LPARAM)Recipes::StationName(Recipes::kStations[i]));
				y += 30;
				BLbl("Wirkung:", x, y + 4, 70);
				r_fx = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 72, y, 260, 300, RID_FX);
				for (int k = 0; k < Recipes::FX_COUNT; k++) SendMessageA(r_fx, CB_ADDSTRING, 0, (LPARAM)Recipes::EffectName(k));
				r_fxHeal = BChk("zusaetzlich heilen", x + 342, y + 1, 150); y += 28;
				BLbl("Wert:", x + 72, y + 4, 34); r_fxVal = BEdt(x + 108, y, 50);
				r_fxUnit = BCtl("STATIC", "", SS_LEFT, x + 164, y + 4, 230, 18, -1);
				BLbl("Dauer (s):", x + 398, y + 4, 56); r_fxSec = BEdt(x + 456, y, 40);
				y += 34;
				BLbl("Zutaten (Auswahl fuellt Typ/Unterart/Material; leere Zeilen zaehlen nicht)", x, y, 480, true); y += 22;
				BLbl("Zutat", x + 72, y, 100); BLbl("Typ   Unter  Mat.", x + 262, y, 140); BLbl("Anzahl", x + 400, y, 60); y += 18;
				for (int i = 0; i < 6; i++) {
					char b[16]; sprintf_s(b, "%d.", i + 1); BLbl(b, x, y + 4, 20);
					r_ingSel[i] = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 72, y, 180, 400, RID_ING0 + i);
					r_ingT[i] = BEdt(x + 262, y, 40); r_ingS[i] = BEdt(x + 306, y, 40); r_ingM[i] = BEdt(x + 350, y, 40); r_ingN[i] = BEdt(x + 400, y, 40);
					y += 28;
				}
				BCtl("STATIC", "Das Spiel kennt ein Rezept ueber seinen ERGEBNIS-Gegenstand. Der Mod liefert Zutaten und Station "
				     "fuer dieses Ergebnis (gilt auch fuer native Gegenstaende -- deren Spielrezept wird dann ersetzt).",
				     SS_LEFT, x, y + 4, 480, 48, -1);
				g_bParent = nullptr;
				RzFillList("");
				Recipes::RecipeDef d; RzToUi(d);
				RzRefreshStatus();
				SetTimer(h, RID_TIMER, 1000, NULL);
				return 0;
			}
			case WM_TIMER: if (wp == RID_TIMER && IsWindowVisible(h)) { RzRefreshStatus(); RzShowUse(); } return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (code == CBN_SELCHANGE && id == RID_FX) {
					int fx = (int)SendMessageA(r_fx, CB_GETCURSEL, 0, 0);
					SetWindowTextA(r_fxUnit, Recipes::EffectUnit(fx));
					if (Recipes::EffectTagged(fx)) {
						char b[32]; sprintf_s(b, "%g", Recipes::EffectDefaultValue(fx)); SetWindowTextA(r_fxVal, b);
						if (GetText(r_fxSec).empty()) SetInt(r_fxSec, 120);
					} else { SetWindowTextA(r_fxVal, ""); SetWindowTextA(r_fxSec, ""); }
					if (fx != Recipes::FX_NONE) {                     // Wirkung nur fuer Trank/Speise (Typ 1)
						int t = GetInt(r_outT, 0), sub = GetInt(r_outS, 0);
						bool ok = t == 1 && (sub == 1 || sub == 2 || sub == 4 || sub == 5 || sub == 6 || sub == 8 || sub == 9);
						if (!ok) { SetHex(r_outT, 1); SetHex(r_outS, 1); SetHex(r_outM, 0); RzSelectFor(r_outSel, 1, 1, 0); }
					}
					return 0;
				}
				if (code == CBN_DROPDOWN && (id == RID_OUTSEL || (id >= RID_ING0 && id < RID_ING0 + 6))) {
					size_t before = g_rzChoices.size();
					RzRefreshChoices();                                         // neue sammelbare Pflanzen sofort in der Liste
					if (g_rzChoices.size() > before) Say("Zutatenliste aktualisiert: neue eigene sammelbare Pflanzen sind dabei.");
					return 0;
				}
				if (code == CBN_SELCHANGE && (id == RID_OUTSEL || (id >= RID_ING0 && id < RID_ING0 + 6))) {
					HWND c = (HWND)lp; int sel = (int)SendMessageA(c, CB_GETCURSEL, 0, 0);
					if (sel > 0 && sel < (int)g_rzChoices.size()) {
						const RChoice& r = g_rzChoices[sel];
						if (id == RID_OUTSEL) { SetHex(r_outT, r.t); SetHex(r_outS, r.s); SetHex(r_outM, r.m); RzShowUse(); }
						else { int i = id - RID_ING0; SetHex(r_ingT[i], r.t); SetHex(r_ingS[i], r.s); SetHex(r_ingM[i], r.m); if (GetText(r_ingN[i]).empty()) SetInt(r_ingN[i], 1); }
					}
					return 0;
				}
				switch (id) {
					case RID_LIST: if (code == LBN_SELCHANGE) {
						int i = (int)SendMessageA(r_list, LB_GETCURSEL, 0, 0); if (i < 0) break;
						char b[MAX_PATH] = ""; SendMessageA(r_list, LB_GETTEXT, i, (LPARAM)b);
						Recipes::RecipeDef d; std::string err;
						if (Recipes::Load(Recipes::RecipesDir() + "\\" + b, d, err)) { g_rzFile = b; RzToUi(d); RzShowUse(); } else Say(err);
					} break;
					case RID_NEW: { Recipes::RecipeDef d; d.ing.clear(); g_rzFile.clear(); g_rzEffectId = 0; RzToUi(d); RzShowUse(); Say("Neues Rezept: Ergebnis, Station und Zutaten waehlen, dann 'Speichern + laden'."); break; }
					case RID_MODEL: {
						char file[MAX_PATH] = ""; char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
						Lab::EnsureDir(Lab::StructuresDir());
						std::string sdir = std::string(cwd) + "\\" + Lab::StructuresDir();
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
						ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = sdir.c_str();
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (!GetOpenFileNameA(&ofn)) break;
						std::string p = file, name = p;                      // unter Strukturen\ -> relativ speichern
						if (_strnicmp(p.c_str(), sdir.c_str(), sdir.size()) == 0 && p.size() > sdir.size() + 1) name = p.substr(sdir.size() + 1);
						SetWindowTextA(r_model, name.c_str()); RzShowUse();
						break;
					}
					case RID_MODELCLR: SetWindowTextA(r_model, ""); RzShowUse(); break;
					case RID_SAVE: {
						Recipes::RecipeDef d = RzFromUi();
						if (d.ing.empty()) { Say("Rezept braucht mindestens eine Zutat."); break; }
						if (Recipes::NeedsTag(d) && d.effectId <= 0) { d.effectId = Recipes::NextEffectId(); g_rzEffectId = d.effectId; }
						Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Recipes::RecipesDir());
						std::string file = ZmSafeName(d.name) + ".rez", err;
						if (!Recipes::Save(Recipes::RecipesDir() + "\\" + file, d, err)) { Say(err); break; }
						if (!g_rzFile.empty() && _stricmp(g_rzFile.c_str(), file.c_str()) != 0) DeleteFileA((Recipes::RecipesDir() + "\\" + g_rzFile).c_str());
						g_rzFile = file; Recipes::Reload(); RzFillList(file); RzRefreshStatus();
						Say("Rezept '" + d.name + "' gespeichert und geladen.");
						break;
					}
					case RID_DEL: {
						if (g_rzFile.empty()) break;
						if (MessageBoxA(h, ("Rezept '" + g_rzFile + "' loeschen?\n(Bereits gelernte Rezepte bleiben im Spielstand, haben dann aber die Zutaten des Spiels.)").c_str(), "Rezepte", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((Recipes::RecipesDir() + "\\" + g_rzFile).c_str()); g_rzFile.clear(); Recipes::Reload(); RzFillList(""); break;
					}
					case RID_RELOAD: Recipes::Reload(); RzFillList(g_rzFile); { Recipes::RecipeDef d = RzFromUi(); RzToUi(d); } break;
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7f2. Gegenstaende: eigene Items (Items.cpp)
	// =====================================================================
	namespace {
		HWND g_itWnd = nullptr;
		HWND i_list, i_status, i_name, i_on, i_kind, i_base, i_T, i_S, i_M, i_lvl, i_rar, i_model, i_modelInfo, i_count;
		HWND i_wcls, i_whand, i_wtype, i_winfo;       // Waffe: Klasse, Einhand/Zweihand, Waffenart
		std::vector<int> g_itWSubs;                  // Unterarten in der Reihenfolge der Waffenart-Liste
		int  g_itId = 0;                             // Kennung des geladenen Gegenstands (bleibt beim Speichern)
		std::string g_itFile;
		enum { IID_LIST = 7201, IID_NEW, IID_SAVE, IID_DEL, IID_RELOAD, IID_KIND, IID_BASE, IID_MODEL, IID_MODELCLR, IID_GIVE, IID_FROMEQ, IID_WCLS, IID_WHAND, IID_WTYPE, IID_TIMER = 14 };
		// "Vom angelegten Gegenstand": Spielthread schreibt, Fenster-Timer liest
		volatile LONG g_itEqReady = 0; int g_itEq[5] = { 0 };
	}
	static void ItShowModel() {
		std::string mf = GetText(i_model), mi;
		if (mf.empty()) mi = "Aussehen: Modell des Spiels fuer Typ/Unterart/Material";
		else {
			Lab::Structure s0; std::string err;
			if (Lab::LoadCub(Items::ModelPath(mf), s0, err)) {
				char b[160]; sprintf_s(b, "Aussehen: eigenes Modell %dx%dx%d%s", s0.sx, s0.sy, s0.sz, (s0.sx > 64 || s0.sy > 64 || s0.sz > 64) ? " -- ZU GROSS (max 64)" : " (Inventar, Hand/Ausruestung, Boden)");
				mi = b;
			} else mi = "Aussehen: Datei nicht lesbar -- " + err;
		}
		int t = GetInt(i_T, -1), s = GetInt(i_S, -1);
		mi += std::string("\r\nBenutzung: ") + Recipes::UseText(t, s);
		char cur[512]; GetWindowTextA(i_modelInfo, cur, sizeof(cur)); if (mi != cur) SetWindowTextA(i_modelInfo, mi.c_str());
	}
	// Waffenart-Liste fuer Klasse (Index 0..3 -> 1..4) und Hand (0 Einhand, 1 Zweihand); waehlt 'sub' falls vorhanden
	static void ItFillWeaponTypes(int sub) {
		int c = (int)SendMessageA(i_wcls, CB_GETCURSEL, 0, 0) + 1, hnd = (int)SendMessageA(i_whand, CB_GETCURSEL, 0, 0);
		SendMessageA(i_wtype, CB_RESETCONTENT, 0, 0); g_itWSubs.clear();
		int n = 0, sel = -1; const Items::WeaponDef* w = Items::Weapons(&n);
		for (int k = 0; k < n; k++) {
			if (w[k].cls != c || (int)w[k].twoHand != hnd) continue;
			char b[80]; sprintf_s(b, "%s  (0x%02X)", w[k].name, w[k].sub);
			SendMessageA(i_wtype, CB_ADDSTRING, 0, (LPARAM)b);
			if (w[k].sub == sub) sel = (int)g_itWSubs.size();
			g_itWSubs.push_back(w[k].sub);
		}
		SendMessageA(i_wtype, CB_SETCURSEL, sel, 0);
	}
	static void ItShowWeapon() {
		int cls = 0; bool two = false; std::string t;
		if (Items::WeaponOf(GetInt(i_T, -1), GetInt(i_S, -1), &cls, &two)) {
			t = std::string("Waffe: ") + Items::ClassName(cls) + ", " + (two ? "Zweihand" : "Einhand") + " (das Spiel legt Klasse und Hand ueber die Unterart fest)";
			SendMessageA(i_wcls, CB_SETCURSEL, cls - 1, 0); SendMessageA(i_whand, CB_SETCURSEL, two ? 1 : 0, 0);
			ItFillWeaponTypes(GetInt(i_S, -1));
		} else t = "Keine Klassenwaffe (Typ 3 mit Waffen-Unterart waehlen)";
		char cur[256]; GetWindowTextA(i_winfo, cur, sizeof(cur)); if (t != cur) SetWindowTextA(i_winfo, t.c_str());
	}
	static void ItToUi(const Items::ItemDef& d) {
		SetWindowTextA(i_name, d.name.c_str()); BSetChk(i_on, d.enabled);
		SendMessageA(i_kind, CB_SETCURSEL, d.kind, 0);
		SetHex(i_T, d.type); SetHex(i_S, d.sub); SetHex(i_M, d.mat); SetInt(i_lvl, d.level); SetInt(i_rar, d.rarity);
		SendMessageA(i_base, CB_SETCURSEL, 0, 0);
		SetWindowTextA(i_model, d.modelFile.c_str());
		g_itId = d.id;
		ItShowModel(); ItShowWeapon();
	}
	static Items::ItemDef ItFromUi() {
		Items::ItemDef d;
		d.name = GetText(i_name); if (d.name.empty()) d.name = "gegenstand";
		d.enabled = Checked(i_on);
		int k = (int)SendMessageA(i_kind, CB_GETCURSEL, 0, 0); d.kind = (k < 0 || k >= Items::K_COUNT) ? Items::K_OTHER : k;
		d.type = GetInt(i_T, 0x0B) & 0xFF; d.sub = GetInt(i_S, 0) & 0xFF; d.mat = GetInt(i_M, 0) & 0xFF;
		d.level = (std::max)(1, (std::min)(65535, GetInt(i_lvl, 1)));   // Gegenstandslevel: 16 Bit (Item+0x10) d.rarity = (std::max)(0, (std::min)(4, GetInt(i_rar, 0)));
		d.modelFile = GetText(i_model);
		d.id = g_itId;
		return d;
	}
	static void ItFillList(const std::string& sel) {
		SendMessageA(i_list, LB_RESETCONTENT, 0, 0);
		std::vector<std::string> f = Items::ListFiles(); int s = -1;
		for (size_t i = 0; i < f.size(); i++) { SendMessageA(i_list, LB_ADDSTRING, 0, (LPARAM)f[i].c_str()); if (_stricmp(f[i].c_str(), sel.c_str()) == 0) s = (int)i; }
		if (s >= 0) SendMessageA(i_list, LB_SETCURSEL, s, 0);
	}
	static void ItRefreshStatus() {
		std::string t = Items::Status() +
			"\r\n\r\nEin eigener Gegenstand ist ein nativer Gegenstand (Typ/Unterart/Material bestimmen Verhalten, Werte und "
			"Reiter im Handwerksmenue) mit eigener Kennung, eigenem Namen und auf Wunsch eigenem Modell (.cub, hoechstens 64x64x64).\r\n"
			"So kommt er ins Spiel:\r\n"
			" - Rezepte: als Ergebnis oder Zutat 'Gegenstand: Name' waehlen (der Reiter liest die Liste beim Aufklappen neu).\r\n"
			" - 'Ins Inventar': legt ihn direkt in dein Inventar (zum Testen oder als Belohnung).\r\n"
			"Waffe: Klasse, Einhand/Zweihand und Waffenart waehlen -- Typ, Unterart und Material werden gesetzt. Schaden und Tempo "
			"rechnet das Spiel aus Unterart, Level und Seltenheit; die Klasse legt das Spiel ueber die Unterart fest.\r\n"
			"Ruestung/Schmuck: Grundlage am besten mit 'Vom angelegten Gegenstand' von einem echten Teil uebernehmen.";
		char cur[1500]; GetWindowTextA(i_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(i_status, t.c_str());
	}
	static LRESULT CALLBACK ItemProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_itWnd = h; g_bParent = h;
				int x = 10, y = 10;
				BLbl("Gegenstaende  (Strukturen\\Gegenstaende\\*.item)", x, y, 280, true); y += 20;
				i_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 260, 240, IID_LIST, WS_EX_CLIENTEDGE); y += 246;
				BBtn("Neu", x, y, 60, IID_NEW); BBtn("Speichern + laden", x + 64, y, 120, IID_SAVE); BBtn("Loeschen", x + 188, y, 72, IID_DEL); y += 28;
				BBtn("Neu einlesen", x, y, 120, IID_RELOAD); y += 32;
				i_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 260, 230, -1, WS_EX_CLIENTEDGE);

				x = 290; y = 10;
				BLbl("Gegenstand", x, y, 200, true); y += 22;
				BLbl("Name:", x, y + 4, 80); i_name = BEdt(x + 82, y, 200); i_on = BChk("aktiv", x + 292, y + 1, 60); y += 30;
				BLbl("Art:", x, y + 4, 80);
				i_kind = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 82, y, 200, 300, IID_KIND);
				for (int k = 0; k < Items::K_COUNT; k++) SendMessageA(i_kind, CB_ADDSTRING, 0, (LPARAM)Items::KindName(k));
				y += 30;
				BLbl("Grundlage:", x, y + 4, 80);
				i_base = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 82, y, 200, 400, IID_BASE);
				SendMessageA(i_base, CB_ADDSTRING, 0, (LPARAM)"(von Hand / Vorschlag der Art)");
				{ int n = 0; const Recipes::Named* p = Recipes::Presets(&n); for (int k = 0; k < n; k++) SendMessageA(i_base, CB_ADDSTRING, 0, (LPARAM)p[k].name); }
				BBtn("Vom angelegten Gegenstand", x + 292, y, 170, IID_FROMEQ); y += 30;
				BLbl("Typ/Unterart/Mat.:", x, y + 4, 100); i_T = BEdt(x + 104, y, 44); i_S = BEdt(x + 152, y, 44); i_M = BEdt(x + 200, y, 44); y += 28;
				BLbl("Waffe:", x, y + 4, 80);
				i_wcls = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 82, y, 110, 200, IID_WCLS);
				for (int c = 1; c <= 4; c++) SendMessageA(i_wcls, CB_ADDSTRING, 0, (LPARAM)Items::ClassName(c));
				i_whand = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 196, y, 90, 200, IID_WHAND);
				SendMessageA(i_whand, CB_ADDSTRING, 0, (LPARAM)"Einhand"); SendMessageA(i_whand, CB_ADDSTRING, 0, (LPARAM)"Zweihand");
				i_wtype = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 290, y, 190, 300, IID_WTYPE);
				SendMessageA(i_wcls, CB_SETCURSEL, 0, 0); SendMessageA(i_whand, CB_SETCURSEL, 0, 0);
				y += 26;
				i_winfo = BCtl("STATIC", "", SS_LEFT, x + 82, y, 420, 18, -1); y += 24;
				BLbl("Level:", x, y + 4, 40); i_lvl = BEdt(x + 44, y, 44); BLbl("Seltenheit 0-4:", x + 100, y + 4, 90); i_rar = BEdt(x + 192, y, 30); y += 30;
				BLbl("Modell:", x, y + 4, 80); i_model = BEdt(x + 82, y, 240);
				BBtn("Waehlen ...", x + 328, y, 80, IID_MODEL); BBtn("Spiel-Modell", x + 412, y, 90, IID_MODELCLR); y += 28;
				i_modelInfo = BCtl("STATIC", "", SS_LEFT, x + 82, y, 420, 34, -1); y += 42;
				BLbl("Anzahl:", x, y + 4, 80); i_count = BEdt(x + 82, y, 50, "1"); BBtn("Ins Inventar legen", x + 140, y, 160, IID_GIVE); y += 34;
				BCtl("STATIC", "Kennung: jeder Gegenstand bekommt beim ersten Speichern eine feste Kennung (eigener Stapel). "
				     "Name und Modell gelten ueberall, wo das Spiel den Gegenstand zeigt.", SS_LEFT, x, y, 480, 36, -1);
				y += 44;
				BCtl("STATIC", "Luftschiff-Taxi: Ware je Richtung steht jetzt im Reiter 'Haendler' (alle Haendler an einer Stelle).", SS_LEFT, x, y, 480, 20, -1);
				g_bParent = nullptr;
				ItFillList("");
				Items::ItemDef d; ItToUi(d);
				ItRefreshStatus();
				SetTimer(h, IID_TIMER, 700, NULL);
				return 0;
			}
			case WM_TIMER:
				if (wp == IID_TIMER && IsWindowVisible(h)) {
					ItRefreshStatus(); ItShowModel();
					if (GetFocus() != i_wcls && GetFocus() != i_whand && GetFocus() != i_wtype) { int c = 0; bool t2 = false; if (Items::WeaponOf(GetInt(i_T, -1), GetInt(i_S, -1), &c, &t2)) { int sub = GetInt(i_S, -1); bool same = false; int cs = (int)SendMessageA(i_wtype, CB_GETCURSEL, 0, 0); if (cs >= 0 && cs < (int)g_itWSubs.size() && g_itWSubs[cs] == sub) same = true; if (!same) ItShowWeapon(); } }
					if (InterlockedExchange(&g_itEqReady, 0) == 1) {
						if (g_itEq[0] == 0) Say("Kein passender Gegenstand angelegt.");
						else { SetHex(i_T, g_itEq[0]); SetHex(i_S, g_itEq[1]); SetHex(i_M, g_itEq[2]); SetInt(i_lvl, g_itEq[3] < 1 ? 1 : g_itEq[3]); SetInt(i_rar, g_itEq[4]); Say("Grundlage vom angelegten Gegenstand uebernommen."); }
					}
				}
				return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (code == CBN_SELCHANGE && id == IID_KIND) {
					int k = (int)SendMessageA(i_kind, CB_GETCURSEL, 0, 0), t = 0x0B, s = 0, mm = 0;
					if (k >= 0 && k < Items::K_OTHER) { Items::KindDefaults(k, &t, &s, &mm); SetHex(i_T, t); SetHex(i_S, s); SetHex(i_M, mm); }
					return 0;
				}
				if (code == CBN_SELCHANGE && (id == IID_WCLS || id == IID_WHAND)) { ItFillWeaponTypes(-1); return 0; }
				if (code == CBN_SELCHANGE && id == IID_WTYPE) {
					int sel = (int)SendMessageA(i_wtype, CB_GETCURSEL, 0, 0), n = 0; const Items::WeaponDef* w = Items::Weapons(&n);
					if (sel < 0 || sel >= (int)g_itWSubs.size()) return 0;
					for (int k = 0; k < n; k++) if (w[k].sub == g_itWSubs[sel]) { SetHex(i_T, 3); SetHex(i_S, w[k].sub); SetHex(i_M, w[k].mat); }
					SendMessageA(i_kind, CB_SETCURSEL, Items::K_WEAPON, 0);
					ItShowWeapon(); ItShowModel();
					return 0;
				}
				if (code == CBN_SELCHANGE && id == IID_BASE) {
					int sel = (int)SendMessageA(i_base, CB_GETCURSEL, 0, 0), n = 0; const Recipes::Named* p = Recipes::Presets(&n);
					if (sel > 0 && sel <= n) { SetHex(i_T, p[sel - 1].type); SetHex(i_S, p[sel - 1].sub); SetHex(i_M, p[sel - 1].mat); }
					return 0;
				}
				switch (id) {
					case IID_LIST: if (code == LBN_SELCHANGE) {
						int i = (int)SendMessageA(i_list, LB_GETCURSEL, 0, 0); if (i < 0) break;
						char b[MAX_PATH] = ""; SendMessageA(i_list, LB_GETTEXT, i, (LPARAM)b);
						Items::ItemDef d; std::string err;
						if (Items::Load(Items::ItemsDir() + "\\" + b, d, err)) { g_itFile = b; ItToUi(d); } else Say(err);
					} break;
					case IID_NEW: { Items::ItemDef d; g_itFile.clear(); g_itId = 0; ItToUi(d); Say("Neuer Gegenstand: Name, Art, Grundlage und Modell waehlen, dann 'Speichern + laden'."); break; }
					case IID_SAVE: {
						Items::ItemDef d = ItFromUi();
						if (d.id <= 0) { d.id = Items::NextId(); g_itId = d.id; }
						Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Items::ItemsDir());
						std::string file = ZmSafeName(d.name) + ".item", err;
						if (!Items::Save(Items::ItemsDir() + "\\" + file, d, err)) { Say(err); break; }
						if (!g_itFile.empty() && _stricmp(g_itFile.c_str(), file.c_str()) != 0) DeleteFileA((Items::ItemsDir() + "\\" + g_itFile).c_str());
						g_itFile = file; Items::Reload(); ItFillList(file); ItRefreshStatus();
						Say("Gegenstand '" + d.name + "' gespeichert (Kennung " + std::to_string(d.id) + "). In Rezepten als 'Gegenstand: " + d.name + "' waehlbar.");
						break;
					}
					case IID_DEL: {
						if (g_itFile.empty()) break;
						if (MessageBoxA(h, ("Gegenstand '" + g_itFile + "' loeschen?\n(Schon vorhandene Exemplare im Spielstand behalten ihre Kennung, zeigen dann aber das Aussehen/den Namen des Spiels.)").c_str(), "Gegenstaende", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((Items::ItemsDir() + "\\" + g_itFile).c_str()); g_itFile.clear(); Items::Reload(); ItFillList(""); break;
					}
					case IID_RELOAD: Items::Reload(); ItFillList(g_itFile); break;
					case IID_MODEL: {
						char file[MAX_PATH] = ""; char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
						Lab::EnsureDir(Lab::StructuresDir());
						std::string sdir = std::string(cwd) + "\\" + Lab::StructuresDir();
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
						ofn.lpstrFilter = "Cube-Modelle (*.cub)\0*.cub\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = sdir.c_str();
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (!GetOpenFileNameA(&ofn)) break;
						std::string p = file, name = p;
						if (_strnicmp(p.c_str(), sdir.c_str(), sdir.size()) == 0 && p.size() > sdir.size() + 1) name = p.substr(sdir.size() + 1);
						SetWindowTextA(i_model, name.c_str()); ItShowModel();
						break;
					}
					case IID_MODELCLR: SetWindowTextA(i_model, ""); ItShowModel(); break;
					case IID_FROMEQ: {
						int kind = (int)SendMessageA(i_kind, CB_GETCURSEL, 0, 0);
						LabBridge::Post("Gegenstand: Grundlage lesen", [kind](cube::GameController* gc) {
							if (!gc || !gc->local_player) return;
							auto& eq = gc->local_player->entity_data.equipment;
							const ::Item* it = kind == Items::K_ARMOR ? &eq.chest : (kind == Items::K_JEWEL ? &eq.necklace : &eq.right_weapon);
							g_itEq[0] = it->category_id; g_itEq[1] = it->item_id; g_itEq[2] = it->material; g_itEq[3] = it->level; g_itEq[4] = it->rarity;
							InterlockedExchange(&g_itEqReady, 1);
						});
						Say(kind == Items::K_ARMOR ? "Lese die angelegte Brustruestung ..." : (kind == Items::K_JEWEL ? "Lese die angelegte Halskette ..." : "Lese die Waffe in der rechten Hand ..."));
						break;
					}
					case IID_GIVE: {
						Items::ItemDef d = ItFromUi();
						if (d.id <= 0 || !Items::ById(d.id, nullptr)) { Say("Erst 'Speichern + laden' -- dann hat der Gegenstand eine Kennung."); break; }
						Items::ById(d.id, &d);                                  // so, wie er geladen ist
						int n = (std::max)(1, (std::min)(999, GetInt(i_count, 1)));
						LabBridge::Post("Gegenstand ins Inventar", [d, n](cube::GameController* gc) { std::string r; Items::Give(gc, d, n, r); std::cout << "[Gegenstaende] " << r << std::endl; });
						Say("Lege " + std::to_string(n) + " x '" + d.name + "' ins Inventar ...");
						break;
					}
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7g. Reiter oben im Hauptfenster: Bauen | Mobs | Biome | Zonen-Modelle | Rezepte | Leistung | Logger
	// =====================================================================
	// 7f3. Skills: eigene Faehigkeiten (Skills.cpp)
	// =====================================================================
	namespace {
		HWND g_skWnd = nullptr;
		HWND s_list, s_status, s_name, s_on, s_kind, s_cls, s_icon,	s_iconPic, s_key, s_key2, s_keyP, s_unlock, s_lpr, s_maxr, s_tslot, s_tppr;
		HWND s_val, s_valR, s_dur, s_durR, s_cd, s_cdR, s_mana, s_help, s_table, s_hud;
		std::string g_skFile;
		HBITMAP g_skBmp = nullptr;
		ULONG_PTR g_gdipTok = 0;
		enum { SID_LIST = 7401, SID_NEW, SID_SAVE, SID_DEL, SID_RELOAD, SID_KIND, SID_ICON, SID_HUD, SID_TESTSUM, SID_FREESUM, SID_TIMER = 15 };
		const int kSkKeys[] = { 0, 'P', 'O', '4', '5', '6', '7', '8', '9', 'U', 'T', 'Y', 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x7A, 0x7B, 'G', 'H', 'J', 'K', 'N', 'V', 'B', 'Z', 'X', 'C' };
		const int kSkKeyN = sizeof(kSkKeys) / sizeof(kSkKeys[0]);
	}
	static void SkShowIcon() {
		std::string f = GetText(s_icon);
		HBITMAP nb = nullptr;
		if (!f.empty()) {
			if (!g_gdipTok) { Gdiplus::GdiplusStartupInput in; Gdiplus::GdiplusStartup(&g_gdipTok, &in, nullptr); }
			std::string p = Skills::IconPath(f);
			wchar_t w[MAX_PATH] = { 0 }; MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, w, MAX_PATH - 1);
			Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromFile(w);
			if (src && src->GetLastStatus() == Gdiplus::Ok) {
				Gdiplus::Bitmap dst(64, 64, PixelFormat32bppARGB);
				{ Gdiplus::Graphics g(&dst); g.Clear(Gdiplus::Color(255, 240, 240, 240)); g.DrawImage(src, 0, 0, 64, 64); }
				dst.GetHBITMAP(Gdiplus::Color(255, 240, 240, 240), &nb);
			}
			delete src;
		}
		HBITMAP old = (HBITMAP)SendMessageA(s_iconPic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)nb);
		if (old && old != nb) DeleteObject(old);
		if (g_skBmp && g_skBmp != nb) DeleteObject(g_skBmp);
		g_skBmp = nb;
	}
	static Skills::SkillDef SkFromUi() {
		Skills::SkillDef d;
		d.name = GetText(s_name); if (d.name.empty()) d.name = "skill";
		d.enabled = Checked(s_on);
		int k = ComboSel(s_kind); d.kind = (k < 0 || k >= Skills::SK_COUNT) ? 0 : k;
		d.cls = (std::max)(0, (std::min)(4, ComboSel(s_cls)));
		d.icon = GetText(s_icon);
		auto KeyOf = [&](HWND c, int def) { int ks = ComboSel(c); return (ks >= 0 && ks < kSkKeyN) ? kSkKeys[ks] : def; };
		d.key = KeyOf(s_key, 0x34); d.key2 = KeyOf(s_key2, 0); d.keyPlace = KeyOf(s_keyP, 0);
		d.unlockLevel = (std::max)(1, GetInt(s_unlock, 1)); d.levelsPerRank = (std::max)(1, GetInt(s_lpr, 10)); d.maxRank = (std::max)(1, (std::min)(20, GetInt(s_maxr, 5)));
		{ int ts = GetInt(s_tslot, -1); d.treeSlot = (ts >= 0 && ts <= 10) ? ts : -1; d.pointsPerRank = (std::max)(1, GetInt(s_tppr, 5)); }
		d.value = BFloat(s_val, 1); d.valuePerRank = BFloat(s_valR, 0); d.duration = (std::max)(0.0f, BFloat(s_dur, 0)); d.durationPerRank = BFloat(s_durR, 0);
		d.cooldown = (std::max)(0.0f, BFloat(s_cd, 5)); d.cooldownPerRank = BFloat(s_cdR, 0); d.mana = (std::max)(0, (std::min)(100, GetInt(s_mana, 0)));
		return d;
	}
	static void SkShowTable() {
		Skills::SkillDef d = SkFromUi();
		std::ostringstream o;
		if (d.treeSlot >= 0) {
			o << "Fortschritt im Skilltree (Taste X, Leiste 'Weitere Faehigkeiten'; Platz " << d.treeSlot << " = Faehigkeit " << d.treeSlot - 5 << "):\r\n";
			for (int r = 1; r <= d.maxRank; r++) {
				char b[200];
				sprintf_s(b, "  Rang %2d ab %4d Punkten:  Wert %.1f   Dauer %.0f s   Abklingzeit %.0f s   Mana %d %%\r\n",
				          r, 1 + (r - 1) * d.pointsPerRank, Skills::ValueAt(d, r), Skills::DurationAt(d, r), Skills::CooldownAt(d, r), d.mana);
				o << b;
			}
			std::string t = o.str(); char cur[4096]; GetWindowTextA(s_table, cur, sizeof(cur));
			if (t != cur) SetWindowTextA(s_table, t.c_str());
			SetWindowTextA(s_help, Skills::KindHelp(d.kind));
			return;
		}
		o << "Fortschritt (Rang folgt dem Spielerlevel):\r\n";
		for (int r = 1; r <= d.maxRank; r++) {
			char b[200];
			sprintf_s(b, "  Rang %2d ab Level %4d:  Wert %.1f   Dauer %.0f s   Abklingzeit %.0f s   Mana %d %%\r\n",
			          r, d.unlockLevel + (r - 1) * d.levelsPerRank, Skills::ValueAt(d, r), Skills::DurationAt(d, r), Skills::CooldownAt(d, r), d.mana);
			o << b;
		}
		std::string t = o.str(); char cur[4096]; GetWindowTextA(s_table, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(s_table, t.c_str());
		SetWindowTextA(s_help, Skills::KindHelp(d.kind));
	}
	static void SkToUi(const Skills::SkillDef& d) {
		SetWindowTextA(s_name, d.name.c_str()); BSetChk(s_on, d.enabled);
		SendMessageA(s_kind, CB_SETCURSEL, d.kind, 0); SendMessageA(s_cls, CB_SETCURSEL, d.cls, 0);
		SetWindowTextA(s_icon, d.icon.c_str());
		auto SetKey = [&](HWND c, int vk) { int ks = 0; for (int i = 0; i < kSkKeyN; i++) if (kSkKeys[i] == vk) ks = i; SendMessageA(c, CB_SETCURSEL, ks, 0); };
		SetKey(s_key, d.key); SetKey(s_key2, d.key2); SetKey(s_keyP, d.keyPlace);
		SetInt(s_unlock, d.unlockLevel); SetInt(s_lpr, d.levelsPerRank); SetInt(s_maxr, d.maxRank);
		SetInt(s_tslot, d.treeSlot); SetInt(s_tppr, d.pointsPerRank);
		BSetF(s_val, d.value); BSetF(s_valR, d.valuePerRank); BSetF(s_dur, d.duration); BSetF(s_durR, d.durationPerRank);
		BSetF(s_cd, d.cooldown); BSetF(s_cdR, d.cooldownPerRank); SetInt(s_mana, d.mana);
		SkShowIcon(); SkShowTable();
	}
	static void SkFillList(const std::string& sel) {
		SendMessageA(s_list, LB_RESETCONTENT, 0, 0);
		std::vector<std::string> f = Skills::ListFiles(); int s = -1;
		for (size_t i = 0; i < f.size(); i++) { SendMessageA(s_list, LB_ADDSTRING, 0, (LPARAM)f[i].c_str()); if (_stricmp(f[i].c_str(), sel.c_str()) == 0) s = (int)i; }
		if (s >= 0) SendMessageA(s_list, LB_SETCURSEL, s, 0);
	}
	static void SkRefreshStatus() {
		std::string t = Skills::Status() +
			"\r\n\r\nTaste im Spiel druecken: der Skill wirkt, wenn deine Klasse passt, der Rang > 0 ist (Spielerlevel), "
			"er nicht mehr abklingt und genug Mana da ist. Hinweise kommen in den Chat.\r\n"
			"Teleport: in Laufrichtung (zuletzt gelaufene Richtung), Landung auf festem Boden.\r\n"
			"Beschwoeren: die zuletzt in deiner Naehe besiegten Gegner kehren als Begleiter zurueck (Begleiter-KI des Spiels).\r\n"
			"Weitere Arten (z. B. klassenspezifische Skills): Art + Handler in Skills.cpp ergaenzen; "
			"jede Art laesst sich hier auf eine Klasse beschraenken.";
		char cur[2048]; GetWindowTextA(s_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(s_status, t.c_str());
	}
	static LRESULT CALLBACK SkillProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_skWnd = h; g_bParent = h;
				int x = 10, y = 10;
				BLbl("Skills  (Strukturen\\Skills\\*.skill)", x, y, 280, true); y += 20;
				s_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 260, 200, SID_LIST, WS_EX_CLIENTEDGE); y += 204;
				BBtn("Neu", x, y, 60, SID_NEW); BBtn("Speichern + laden", x + 64, y, 120, SID_SAVE); BBtn("Loeschen", x + 188, y, 72, SID_DEL); y += 28;
				BBtn("Neu einlesen", x, y, 120, SID_RELOAD); y += 30;
				s_hud = BCtl("BUTTON", "Skill-Leiste im Spiel (unten rechts)", BS_AUTOCHECKBOX, x, y, 260, 20, SID_HUD); y += 26;
				BCtl("BUTTON", "Beschwoeren testen (Skelett)", BS_PUSHBUTTON, x, y, 170, 24, SID_TESTSUM);
				BCtl("BUTTON", "Plaetze leeren", BS_PUSHBUTTON, x + 176, y, 84, 24, SID_FREESUM); y += 30;
				s_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 260, 300, -1, WS_EX_CLIENTEDGE);

				x = 290; y = 10;
				BLbl("Skill", x, y, 200, true); y += 22;
				BLbl("Name:", x, y + 4, 90); s_name = BEdt(x + 92, y, 190); s_on = BChk("aktiv", x + 292, y + 1, 60); y += 28;
				BLbl("Art:", x, y + 4, 90); s_kind = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 92, y, 190, 200, SID_KIND);
				for (int k = 0; k < Skills::SK_COUNT; k++) SendMessageA(s_kind, CB_ADDSTRING, 0, (LPARAM)Skills::KindName(k));
				y += 28;
				BLbl("Klasse:", x, y + 4, 90); s_cls = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 92, y, 190, 200, -1);
				for (int c = 0; c <= 4; c++) SendMessageA(s_cls, CB_ADDSTRING, 0, (LPARAM)Skills::ClassName(c));
				y += 28;
				BLbl("Taste:", x, y + 4, 90); s_key = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 92, y, 56, 300, -1);
				BLbl("2.:", x + 154, y + 4, 20); s_key2 = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 176, y, 56, 300, -1);
				BLbl("Ablegen:", x + 238, y + 4, 52); s_keyP = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 292, y, 56, 300, -1);
				for (HWND kc : { s_key, s_key2, s_keyP })
					for (int i = 0; i < kSkKeyN; i++) { char b[8]; if (kSkKeys[i] == 0) strcpy_s(b, "-"); else if (kSkKeys[i] >= 0x70) sprintf_s(b, "F%d", kSkKeys[i] - 0x6F); else { b[0] = (char)kSkKeys[i]; b[1] = 0; } SendMessageA(kc, CB_ADDSTRING, 0, (LPARAM)b); }
				y += 28;
				BLbl("Bild:", x, y + 4, 90); s_icon = BEdt(x + 92, y, 190); BBtn("Waehlen ...", x + 288, y, 80, SID_ICON, 22);
				s_iconPic = BCtl("STATIC", "", SS_BITMAP | SS_CENTERIMAGE, x + 380, y - 84, 68, 68, -1, WS_EX_CLIENTEDGE);
				y += 32;
				BLbl("Fortschritt", x, y, 200, true); y += 20;
				BLbl("Rang 1 ab Level:", x, y + 4, 90); s_unlock = BEdt(x + 92, y, 50);
				BLbl("Level je Rang:", x + 152, y + 4, 80); s_lpr = BEdt(x + 234, y, 46);
				BLbl("max. Rang:", x + 290, y + 4, 60); s_maxr = BEdt(x + 352, y, 40); y += 26;
				BLbl("Skilltree-Platz:", x, y + 4, 90); s_tslot = BEdt(x + 92, y, 50);
				BLbl("Punkte je Rang:", x + 152, y + 4, 80); s_tppr = BEdt(x + 234, y, 46);
				BLbl("(9/10 = Faehigkeit 4/5, -1 = nach Level)", x + 290, y + 4, 200); y += 30;
				BLbl("Werte je Rang", x, y, 200, true); y += 20;
				BLbl("Wert:", x, y + 4, 90); s_val = BEdt(x + 92, y, 50); BLbl("+ je Rang:", x + 152, y + 4, 60); s_valR = BEdt(x + 214, y, 50); y += 26;
				BLbl("Dauer s:", x, y + 4, 90); s_dur = BEdt(x + 92, y, 50); BLbl("+ je Rang:", x + 152, y + 4, 60); s_durR = BEdt(x + 214, y, 50); y += 26;
				BLbl("Abklingzeit s:", x, y + 4, 90); s_cd = BEdt(x + 92, y, 50); BLbl("- je Rang:", x + 152, y + 4, 60); s_cdR = BEdt(x + 214, y, 50); y += 26;
				BLbl("Mana %:", x, y + 4, 90); s_mana = BEdt(x + 92, y, 50); y += 30;
				s_help = BCtl("STATIC", "", SS_LEFT, x, y, 480, 44, -1); y += 48;
				s_table = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 480, 220, -1, WS_EX_CLIENTEDGE);
				g_bParent = nullptr;
				BSetChk(s_hud, Skills::HudOn());
				SkFillList("");
				std::vector<Skills::SkillDef> all = Skills::All();
				if (!all.empty()) { g_skFile = all[0].file; SkToUi(all[0]); SkFillList(g_skFile); } else SkToUi(Skills::DefaultSkill(0));
				SkRefreshStatus();
				SetTimer(h, SID_TIMER, 700, NULL);
				return 0;
			}
			case WM_TIMER:
				if (wp == SID_TIMER && IsWindowVisible(h)) { SkRefreshStatus(); SkShowTable(); }
				return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (id == SID_KIND && code == CBN_SELCHANGE) { SkShowTable(); return 0; }
				switch (id) {
					case SID_LIST: if (code == LBN_SELCHANGE) {
						int i = (int)SendMessageA(s_list, LB_GETCURSEL, 0, 0); char b[MAX_PATH] = "";
						if (i >= 0) SendMessageA(s_list, LB_GETTEXT, i, (LPARAM)b);
						Skills::SkillDef d; std::string err;
						if (Skills::Load(Skills::SkillsDir() + "\\" + b, d, err)) { g_skFile = b; SkToUi(d); } else Say(err);
					} break;
					case SID_NEW: {
						HMENU mn = CreatePopupMenu();
						for (int k = 0; k < Skills::SK_COUNT; k++) AppendMenuA(mn, MF_STRING, 100 + k, Skills::KindName(k));
						POINT pt; GetCursorPos(&pt);
						int c = TrackPopupMenu(mn, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, h, NULL); DestroyMenu(mn);
						if (c >= 100) { Skills::SkillDef d = Skills::DefaultSkill(c - 100); d.name = "neu-" + d.name; g_skFile.clear(); SkToUi(d); Say("Neuer Skill: Werte setzen, dann 'Speichern + laden'."); }
						break;
					}
					case SID_SAVE: {
						Skills::SkillDef d = SkFromUi();
						Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Skills::SkillsDir());
						std::string file = ZmSafeName(d.name) + ".skill", err;
						if (!Skills::Save(Skills::SkillsDir() + "\\" + file, d, err)) { Say(err); break; }
						if (!g_skFile.empty() && _stricmp(g_skFile.c_str(), file.c_str()) != 0) DeleteFileA((Skills::SkillsDir() + "\\" + g_skFile).c_str());
						g_skFile = file; Skills::Reload(); SkFillList(file); SkRefreshStatus();
						Say("Skill '" + d.name + "' gespeichert und geladen (Taste im Spiel sofort aktiv).");
						break;
					}
					case SID_DEL: {
						if (g_skFile.empty()) break;
						if (MessageBoxA(h, ("Skill '" + g_skFile + "' loeschen?").c_str(), "Skills", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((Skills::SkillsDir() + "\\" + g_skFile).c_str()); g_skFile.clear(); Skills::Reload(); SkFillList(""); break;
					}
					case SID_RELOAD: Skills::Reload(); SkFillList(g_skFile); break;
					case SID_HUD: Skills::SetHud(Checked(s_hud)); break;
					case SID_TESTSUM: Say("-> Beschwoeren testen"); LabBridge::Post("Beschwoeren testen", [](cube::GameController* gc) { Skills::TestSummon(gc, 17); }); break;
					case SID_FREESUM: Say("-> Begleiter-Plaetze leeren"); LabBridge::Post("Plaetze leeren", [](cube::GameController* gc) { Skills::FreeSummons(gc); }); break;
					case SID_ICON: {
						char file[MAX_PATH] = ""; char cwd[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, cwd);
						Lab::EnsureDir(Skills::SkillsDir()); Lab::EnsureDir(Skills::IconsDir());
						std::string idir = std::string(cwd) + "\\" + Skills::IconsDir();
						OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
						ofn.lpstrFilter = "Bilder (*.png;*.bmp;*.jpg)\0*.png;*.bmp;*.jpg;*.jpeg\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = idir.c_str();
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
						if (!GetOpenFileNameA(&ofn)) break;
						std::string p = file, name = p;
						if (_strnicmp(p.c_str(), idir.c_str(), idir.size()) == 0 && p.size() > idir.size() + 1) name = p.substr(idir.size() + 1);
						SetWindowTextA(s_icon, name.c_str()); SkShowIcon();
						break;
					}
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7f4. Haendler: Waren und Beruf der Stadt-Haendler (Vendors.cpp)
	// =====================================================================
	namespace {
		HWND g_vdWnd = nullptr;
		HWND v_list, v_status, v_role, v_name, v_cls, v_native, v_ing, v_pot, v_items, v_cnt, v_wares, v_wT, v_wS, v_wM, v_wL, v_wR, v_wN, v_item;
		std::string g_vdFile;
		Vendors::VendorDef g_vd;
		enum { VID_LIST = 7501, VID_NEW, VID_SAVE, VID_DEL, VID_RELOAD, VID_WARES, VID_WADD, VID_WSET, VID_WDEL, VID_IADD, VID_TAXISAVE, VID_TAXI0 = 7550, VID_TIMER = 16 };
		HWND v_taxi[4] = {}, v_keep = nullptr;        // Luftschiff-Taxi: Ware je Richtung (frueher Reiter Gegenstaende)
		std::vector<int> g_vdTaxiIds;                 // Kennungen in der Reihenfolge der Auswahl (Index 0 = Standard)
		const char* kVdRoles[7] = { "markt", "alchemist", "huette1", "huette2", "huette3", "huette4", "huette5" };
		const int kVdCls[5] = { 0x80, 0x81, 0x82, 0x83, 0x84 };
		std::vector<int> g_vdItemIds;
	}
	static std::string VdWareText(const Vendors::Ware& w) {
		char b[160];
		if (w.item > 0) { Items::ItemDef d; std::string n = Items::ById(w.item, &d) ? d.name : std::string("?"); sprintf_s(b, "eigener Gegenstand %d  %s   x%d", w.item, n.c_str(), w.count); }
		else sprintf_s(b, "Typ 0x%02X  Unterart 0x%02X  Mat 0x%02X  Lv %d  Selt %d   x%d", w.type, w.sub, w.mat, w.level, w.rarity, w.count);
		return b;
	}
	static void VdShowWares() {
		SendMessageA(v_wares, LB_RESETCONTENT, 0, 0);
		for (const Vendors::Ware& w : g_vd.wares) SendMessageA(v_wares, LB_ADDSTRING, 0, (LPARAM)VdWareText(w).c_str());
	}
	static void VdFromUi() {
		int r = ComboSel(v_role); g_vd.role = (r >= 0 && r < 7) ? kVdRoles[r] : "markt";
		g_vd.name = GetText(v_name); if (g_vd.name.empty()) g_vd.name = g_vd.role;
		int c = ComboSel(v_cls); g_vd.cls = (c >= 0 && c < 5) ? kVdCls[c] : 0x80;
		g_vd.nativeWares = Checked(v_native); g_vd.ingredients = Checked(v_ing); g_vd.potions = Checked(v_pot); g_vd.items = Checked(v_items);
		g_vd.defaultCount = (std::max)(1, (std::min)(999, GetInt(v_cnt, 20)));
	}
	static void VdToUi() {
		int r = 0; for (int i = 0; i < 7; i++) if (g_vd.role == kVdRoles[i]) r = i;
		SendMessageA(v_role, CB_SETCURSEL, r, 0); SetWindowTextA(v_name, g_vd.name.c_str());
		int c = 0; for (int i = 0; i < 5; i++) if (kVdCls[i] == g_vd.cls) c = i;
		SendMessageA(v_cls, CB_SETCURSEL, c, 0);
		BSetChk(v_native, g_vd.nativeWares); BSetChk(v_ing, g_vd.ingredients); BSetChk(v_pot, g_vd.potions); BSetChk(v_items, g_vd.items);
		SetInt(v_cnt, g_vd.defaultCount);
		VdShowWares();
	}
	static void VdFillList(const std::string& sel) {
		SendMessageA(v_list, LB_RESETCONTENT, 0, 0);
		std::vector<std::string> f = Vendors::ListFiles(); int s = -1;
		for (size_t i = 0; i < f.size(); i++) { SendMessageA(v_list, LB_ADDSTRING, 0, (LPARAM)f[i].c_str()); if (_stricmp(f[i].c_str(), sel.c_str()) == 0) s = (int)i; }
		if (s >= 0) SendMessageA(v_list, LB_SETCURSEL, s, 0);
	}
	static void VdFillItems() {
		SendMessageA(v_item, CB_RESETCONTENT, 0, 0); g_vdItemIds.clear();
		for (const Items::ItemDef& d : Items::All()) { if (d.id <= 0) continue; char b[128]; sprintf_s(b, "%d  %s", d.id, d.name.c_str()); SendMessageA(v_item, CB_ADDSTRING, 0, (LPARAM)b); g_vdItemIds.push_back(d.id); }
		if (!g_vdItemIds.empty()) SendMessageA(v_item, CB_SETCURSEL, 0, 0);
	}
	static void VdRefreshStatus() {
		VdFromUi();
		std::vector<uint8_t> items; std::vector<int> counts; Vendors::BuildWares(g_vd, items, counts);
		std::string t = Vendors::Status() + "\r\n\r\nDieser Haendler: " + std::to_string(counts.size()) + " eigene Waren" +
			(g_vd.nativeWares ? " + native Waren (Dorf-Generator)" : "") + ".\r\n\r\n" + Vendors::RoleHelp() +
			"\r\n\r\nGilt fuer neu gesetzte Haendler (nach dem Speichern: Gebiet verlassen und wiederkommen, "
			"oder der Haendler wird beim naechsten Zonenladen neu gesetzt).\r\n"
			"Braustand: das native Lagerfeuer (Station 9) im Vorhof des Alchemiehauses -- Rezepte mit Station "
			"'Lagerfeuer / Braustand' (LAB -> Rezepte) werden dort gebraut.";
		char cur[2048]; GetWindowTextA(v_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(v_status, t.c_str());
	}
	static Vendors::Ware VdWareFromUi() {
		Vendors::Ware w; w.type = GetInt(v_wT, 0x0B) & 0xFF; w.sub = GetInt(v_wS, 0) & 0xFF; w.mat = GetInt(v_wM, 0) & 0xFF;
		w.level = (std::max)(1, (std::min)(65535, GetInt(v_wL, 1))); w.rarity = (std::max)(0, (std::min)(4, GetInt(v_wR, 0))); w.count = (std::max)(1, (std::min)(999, GetInt(v_wN, 20)));
		return w;
	}
	// Taxi-Auswahl: Standard + alle aktiven eigenen Gegenstaende; Auswahl aus der Taxi-Einstellung
	static void VdFillTaxi() {
		if (!v_taxi[0]) return;
		int cur[4]; bool keep = false; AirshipTaxi::GetDirectionItems(cur, &keep);
		g_vdTaxiIds.clear(); g_vdTaxiIds.push_back(0);
		std::vector<std::string> names; names.push_back("(Standard: Kopie der angelegten Waffe)");
		for (const Items::ItemDef& d : Items::All()) if (d.enabled && d.id > 0) { g_vdTaxiIds.push_back(d.id); names.push_back(d.name + "  [" + std::to_string(d.id) + "]"); }
		for (int k = 0; k < 4; k++) {
			SendMessageA(v_taxi[k], CB_RESETCONTENT, 0, 0);
			int sel = 0;
			for (size_t i = 0; i < names.size(); i++) { SendMessageA(v_taxi[k], CB_ADDSTRING, 0, (LPARAM)names[i].c_str()); if (g_vdTaxiIds[i] == cur[k]) sel = (int)i; }
			SendMessageA(v_taxi[k], CB_SETCURSEL, sel, 0);
		}
		BSetChk(v_keep, keep);
	}
	static LRESULT CALLBACK VendorProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_vdWnd = h; g_bParent = h;
				int x = 10, y = 10;
				BLbl("Haendler  (Strukturen\\Haendler\\*.haendler)", x, y, 280, true); y += 20;
				v_list = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 260, 160, VID_LIST, WS_EX_CLIENTEDGE); y += 164;
				BBtn("Neu", x, y, 60, VID_NEW); BBtn("Speichern + laden", x + 64, y, 120, VID_SAVE); BBtn("Loeschen", x + 188, y, 72, VID_DEL); y += 28;
				BBtn("Neu einlesen", x, y, 120, VID_RELOAD); y += 32;
				v_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 260, 330, -1, WS_EX_CLIENTEDGE);

				x = 290; y = 10;
				BLbl("Haendler", x, y, 200, true); y += 22;
				BLbl("Rolle:", x, y + 4, 90); v_role = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 92, y, 190, 200, -1);
				for (const char* r : kVdRoles) SendMessageA(v_role, CB_ADDSTRING, 0, (LPARAM)r);
				y += 28;
				BLbl("Name:", x, y + 4, 90); v_name = BEdt(x + 92, y, 250); y += 28;
				BLbl("Beruf:", x, y + 4, 90); v_cls = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 92, y, 190, 200, -1);
				{ const char* c[5] = { "0x80 Haendler", "0x81 Dorf-NPC", "0x82 Dorf-NPC", "0x83 Dorf-NPC", "0x84 Gastwirt" }; for (const char* s : c) SendMessageA(v_cls, CB_ADDSTRING, 0, (LPARAM)s); }
				y += 30;
				BLbl("Waren", x, y, 200, true); y += 20;
				v_native = BChk("native Waren wie im Dorf (nur 0x80..0x83)", x, y, 300); y += 22;
				v_ing = BChk("alle Rezept-Zutaten", x, y, 200); v_pot = BChk("alle eigenen Traenke/Ergebnisse", x + 200, y, 220); y += 22;
				v_items = BChk("alle eigenen Gegenstaende (LAB -> Gegenstaende)", x, y, 320); y += 24;
				BLbl("Anzahl je Ware:", x, y + 4, 100); v_cnt = BEdt(x + 102, y, 50); y += 30;
				BLbl("Feste Waren", x, y, 200, true); y += 20;
				v_wares = BCtl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, x, y, 470, 150, VID_WARES, WS_EX_CLIENTEDGE); y += 154;
				BLbl("Typ:", x, y + 4, 30); v_wT = BEdt(x + 32, y, 46, "0x0B"); BLbl("Unterart:", x + 84, y + 4, 54); v_wS = BEdt(x + 140, y, 46, "0x00");
				BLbl("Mat:", x + 192, y + 4, 28); v_wM = BEdt(x + 222, y, 46, "0x00"); BLbl("Lv:", x + 274, y + 4, 20); v_wL = BEdt(x + 296, y, 50, "1");
				BLbl("Selt:", x + 352, y + 4, 30); v_wR = BEdt(x + 384, y, 28, "0"); BLbl("x", x + 418, y + 4, 10); v_wN = BEdt(x + 430, y, 40, "20"); y += 28;
				BBtn("Ware hinzufuegen", x, y, 150, VID_WADD); BBtn("Auswahl aendern", x + 156, y, 150, VID_WSET); BBtn("Ware entfernen", x + 312, y, 158, VID_WDEL); y += 30;
				BLbl("Eigener Gegenstand:", x, y + 4, 120); v_item = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 122, y, 230, 300, -1);
				BBtn("hinzufuegen", x + 358, y, 112, VID_IADD); y += 30;
				BCtl("STATIC", "Typen: 1 Essen/Trank, 3 Waffe, 4 Brust, 5 Handschuhe, 6 Schuhe, 7 Schulter, 8 Ring, 9 Amulett, "
				               "0x0B Zutat/Material, 0x0C Muenzen, 0x13 Haustierfutter ...", SS_LEFT, x, y, 470, 32, -1);
				// Luftschiff-Taxi-NPC (Landeplaetze in Bergdoerfern und Staedten) -- unter beiden Spalten
				x = 10; y = 600;
				BLbl("Luftschiff-Taxi (NPC am Landeplatz): Ware je Richtung", x, y, 460, true); y += 22;
				{
					static const char* dn[4] = { "Nord:", "Ost:", "Sued:", "West:" };
					for (int k = 0; k < 4; k++) {
						BLbl(dn[k], x, y + 4, 50);
						v_taxi[k] = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x + 52, y, 330, 300, VID_TAXI0 + k);
						y += 28;
					}
				}
				v_keep = BChk("gekaufte Ware nach dem Abflug behalten (sonst ist sie eine Fahrkarte und verschwindet)", x, y, 520); y += 26;
				BBtn("Taxi-Waren uebernehmen", x, y, 200, VID_TAXISAVE); y += 30;
				BCtl("STATIC", "Gilt fuer alle Taxi-NPCs (Bergdoerfer und Staedte); vorhandene bekommen ihren Katalog sofort neu. "
				     "Preis, Name und Modell kommen vom Gegenstand. Markthaendler = Rolle 'markt', Ladenbaum-Haendler = huette1..5, Alchemist = 'alchemist'.",
				     SS_LEFT, x, y, 620, 36, -1);
				g_bParent = nullptr;
				VdFillTaxi();
				Vendors::Reload(); VdFillList(""); VdFillItems();
				std::vector<std::string> f = Vendors::ListFiles();
				if (!f.empty()) { std::string err; if (Vendors::Load(Vendors::Dir() + "\\" + f[0], g_vd, err)) { g_vdFile = f[0]; VdFillList(g_vdFile); } }
				VdToUi(); VdRefreshStatus();
				SetTimer(h, VID_TIMER, 1000, NULL);
				return 0;
			}
			case WM_TIMER:
				if (wp == VID_TIMER && IsWindowVisible(h)) VdRefreshStatus();
				return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (code == CBN_DROPDOWN && id >= VID_TAXI0 && id < VID_TAXI0 + 4) {     // neue Gegenstaende sofort waehlbar
					int keep[4]; for (int k = 0; k < 4; k++) { int s2 = (int)SendMessageA(v_taxi[k], CB_GETCURSEL, 0, 0); keep[k] = (s2 >= 0 && s2 < (int)g_vdTaxiIds.size()) ? g_vdTaxiIds[s2] : 0; }
					VdFillTaxi();
					for (int k = 0; k < 4; k++) for (size_t i = 0; i < g_vdTaxiIds.size(); i++) if (g_vdTaxiIds[i] == keep[k]) SendMessageA(v_taxi[k], CB_SETCURSEL, i, 0);
					return 0;
				}
				switch (id) {
					case VID_TAXISAVE: {
						int ids[4];
						for (int k = 0; k < 4; k++) { int s2 = (int)SendMessageA(v_taxi[k], CB_GETCURSEL, 0, 0); ids[k] = (s2 >= 0 && s2 < (int)g_vdTaxiIds.size()) ? g_vdTaxiIds[s2] : 0; }
						bool ok = AirshipTaxi::SetDirectionItems(ids, Checked(v_keep));
						Say(ok ? "Taxi-Waren gespeichert (Strukturen\\Gegenstaende\\_luftschifftaxi.txt) -- die Taxi-NPCs bekommen den neuen Katalog."
						       : "Taxi-Waren: Datei nicht schreibbar.");
						break;
					}
					case VID_LIST: if (code == LBN_SELCHANGE) {
						int i = (int)SendMessageA(v_list, LB_GETCURSEL, 0, 0); char b[MAX_PATH] = "";
						if (i >= 0) SendMessageA(v_list, LB_GETTEXT, i, (LPARAM)b);
						std::string err; Vendors::VendorDef d;
						if (Vendors::Load(Vendors::Dir() + "\\" + b, d, err)) { g_vdFile = b; g_vd = d; VdToUi(); VdFillItems(); } else Say(err);
					} break;
					case VID_WARES: if (code == LBN_SELCHANGE) {
						int i = (int)SendMessageA(v_wares, LB_GETCURSEL, 0, 0);
						if (i >= 0 && i < (int)g_vd.wares.size()) { const Vendors::Ware& w = g_vd.wares[i]; SetHex(v_wT, w.type); SetHex(v_wS, w.sub); SetHex(v_wM, w.mat); SetInt(v_wL, w.level); SetInt(v_wR, w.rarity); SetInt(v_wN, w.count); }
					} break;
					case VID_WADD: VdFromUi(); g_vd.wares.push_back(VdWareFromUi()); VdShowWares(); break;
					case VID_WSET: { VdFromUi(); int i = (int)SendMessageA(v_wares, LB_GETCURSEL, 0, 0); if (i >= 0 && i < (int)g_vd.wares.size()) { int it = g_vd.wares[i].item; g_vd.wares[i] = VdWareFromUi(); if (it > 0) { g_vd.wares[i].item = it; } } VdShowWares(); break; }
					case VID_WDEL: { VdFromUi(); int i = (int)SendMessageA(v_wares, LB_GETCURSEL, 0, 0); if (i >= 0 && i < (int)g_vd.wares.size()) g_vd.wares.erase(g_vd.wares.begin() + i); VdShowWares(); break; }
					case VID_IADD: {
						VdFromUi(); int i = ComboSel(v_item);
						if (i >= 0 && i < (int)g_vdItemIds.size()) { Vendors::Ware w; w.item = g_vdItemIds[i]; w.count = (std::max)(1, GetInt(v_wN, 20)); g_vd.wares.push_back(w); VdShowWares(); }
						else Say("Keine eigenen Gegenstaende (LAB -> Gegenstaende).");
						break;
					}
					case VID_NEW: { g_vd = Vendors::VendorDef(); g_vd.name = "neuer-haendler"; g_vdFile.clear(); VdToUi(); Say("Neuer Haendler: Rolle waehlen, Waren setzen, 'Speichern + laden'."); break; }
					case VID_SAVE: {
						VdFromUi();
						Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Vendors::Dir());
						std::string file = g_vd.role + ".haendler", err;                               // eine Datei je Rolle
						if (!Vendors::Save(Vendors::Dir() + "\\" + file, g_vd, err)) { Say(err); break; }
						if (!g_vdFile.empty() && _stricmp(g_vdFile.c_str(), file.c_str()) != 0) DeleteFileA((Vendors::Dir() + "\\" + g_vdFile).c_str());
						g_vdFile = file; Vendors::Reload(); VdFillList(file); VdRefreshStatus();
						Say("Haendler '" + g_vd.name + "' (" + g_vd.role + ") gespeichert.");
						break;
					}
					case VID_DEL: {
						if (g_vdFile.empty()) break;
						if (MessageBoxA(h, ("Haendler '" + g_vdFile + "' loeschen? (Beim naechsten Laden wird die Vorgabe neu angelegt.)").c_str(), "Haendler", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
						DeleteFileA((Vendors::Dir() + "\\" + g_vdFile).c_str()); g_vdFile.clear(); Vendors::Reload(); VdFillList(""); break;
					}
					case VID_RELOAD: Vendors::Reload(); VdFillList(g_vdFile); VdFillItems(); break;
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// 7f5. Mehrspieler (Lan.cpp), Tierdichte, Grundeinstellungen (Shader 2026-10-29 entfernt)
	// =====================================================================
	namespace { HWND n_status = nullptr, n_shader = nullptr, n_list = nullptr, n_dens = nullptr, n_share = nullptr, n_laa = nullptr; enum { NID_START = 7601, NID_STOP, NID_SH_ON, NID_SH_SEL, NID_DENS, NID_SHARE, NID_LAA, NID_DEF_SAVE, NID_DEF_LOAD, NID_TIMER = 17 }; }
	// Grundeinstellungen (2026-10-29): Vorlagen Strukturen\Grundeinstellungen\*.cfg -- gelten bei neuer Installation
	// (ModDb::ReadText), werden mit dem Mod ausgeliefert. Speichern = aktuelle Werte als Vorlage, Laden = Vorlage zurueck.
	static const char* kDefFiles[2] = { "AdventureMod_schalter.cfg", "AdventureMod_tweaks.cfg" };
	static std::string DefaultsSave() {
		Lab::EnsureDir(Lab::StructuresDir() + "\\Grundeinstellungen");
		std::string r;
		for (const char* n : kDefFiles) {
			std::string t = ModDb::Txt(n); if (t.empty()) continue;
			std::ofstream f(ModDb::DefaultsPath(n), std::ios::binary | std::ios::trunc); f << t;
			r += std::string(f ? "" : "FEHLER ") + n + " ";
		}
		return r;
	}
	static std::string DefaultsLoad() {
		std::string r;
		for (const char* n : kDefFiles) {
			std::ifstream f(ModDb::DefaultsPath(n), std::ios::binary); if (!f) continue;
			std::ostringstream o; o << f.rdbuf(); ModDb::WriteText(n, o.str()); r += std::string(n) + " ";
		}
		return r;
	}
	static int CfgInt(const char* key, int def) {
		int v = def; std::istringstream cf(ModDb::Txt("AdventureMod_schalter.cfg")); std::string l; const size_t k = strlen(key);
		while (std::getline(cf, l)) if (l.size() > k && l.compare(0, k, key) == 0 && l[k] == '=') v = atoi(l.c_str() + k + 1);
		return v;
	}
	static void SaveCfg(const std::string& key, int v) {                  // Zeile key=N in AdventureMod_schalter.cfg
		std::vector<std::string> lines; { std::istringstream f(ModDb::Txt("AdventureMod_schalter.cfg")); std::string l; while (std::getline(f, l)) if (l.rfind(key + "=", 0) != 0) lines.push_back(l); }
		lines.push_back(key + "=" + std::to_string(v));
		{ ModDb::Writer o("AdventureMod_schalter.cfg"); for (const std::string& l : lines) o << l << "\n"; }
	}
	static void NRefresh() {
		std::string t = Lan::Status() + "\r\n\r\n" + ZoneSplit::Status() + "\r\n\r\n" + Wildlife::Status();
		char cur[4096]; GetWindowTextA(n_status, cur, sizeof(cur));
		if (t != cur) SetWindowTextA(n_status, t.c_str());
	}
	static LRESULT CALLBACK NetProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_bParent = h;
				int x = 10, y = 10;
				BLbl("Mehrspieler im lokalen Netz (Stufe 1)", x, y, 400, true); y += 22;
				BBtn("Server starten", x, y, 150, NID_START); BBtn("Server beenden", x + 156, y, 150, NID_STOP); y += 34;
				BLbl("Tierdichte (nativer Spawn je Zone, gilt fuer neu erzeugte Zonen)", x, y, 460, true); y += 22;
				n_dens = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, 300, 200, NID_DENS);
				{ const char* d[4] = { "1  wie im Spiel (3x3)", "2  mehr (4x4)", "3  viel (5x5)", "4  sehr viel (6x6)" }; for (const char* t : d) SendMessageA(n_dens, CB_ADDSTRING, 0, (LPARAM)t);
				  SendMessageA(n_dens, CB_SETCURSEL, (std::max)(0, BiomeMod::SpawnDensity() - 1), 0); }
				y += 32;
				BLbl("Eigene Tiere am nativen Spawn (Anteil passender Tiergruppen)", x, y, 460, true); y += 22;
				n_share = BCtl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, 300, 200, NID_SHARE);
				{ const char* d[5] = { "0 %  (nur Spieltiere)", "25 %", "50 %", "75 %", "100 %" }; for (const char* t : d) SendMessageA(n_share, CB_ADDSTRING, 0, (LPARAM)t);
				  int cur = 50; { std::istringstream cf(ModDb::Txt("AdventureMod_schalter.cfg")); std::string l; while (std::getline(cf, l)) if (l.rfind("tierwelt_anteil=", 0) == 0) cur = atoi(l.c_str() + 16); }
				  SendMessageA(n_share, CB_SETCURSEL, (std::max)(0, (std::min)(4, cur / 25)), 0); }
				y += 32;
				BLbl("Speicher und Grundeinstellungen (Auslieferung)", x, y, 460, true); y += 22;
				n_laa = BChk("4 GB Adressraum fuer Cube.exe (wird nach dem Beenden gesetzt, gilt ab Neustart)", x, y, 560); SetWindowLongPtrA(n_laa, GWLP_ID, NID_LAA); y += 24;
				BBtn("Als Grundeinstellung speichern", x, y, 220, NID_DEF_SAVE); BBtn("Grundeinstellungen laden", x + 226, y, 200, NID_DEF_LOAD); y += 34;
				n_status = BCtl("EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL, x, y, 620, 330, -1, WS_EX_CLIENTEDGE);
				g_bParent = nullptr;
				{ char exe[MAX_PATH] = ""; GetModuleFileNameA(NULL, exe, MAX_PATH); BSetChk(n_laa, CfgInt("vier_gb", 0) != 0 || CrashLogger::ExeIsLargeAddressAware(exe) == 1); }
				NRefresh(); SetTimer(h, NID_TIMER, 1500, NULL);
				return 0;
			}
			case WM_TIMER: if (wp == NID_TIMER && IsWindowVisible(h)) NRefresh(); return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (id == NID_START) { std::string msg; Lan::Start(msg); Say(msg); NRefresh(); }
				else if (id == NID_STOP) { Lan::Stop(); NRefresh(); }
				else if (id == NID_LAA) {
					const bool on = Checked(n_laa); SaveCfg("vier_gb", on ? 1 : 0);
					if (on) { CrashLogger::EnsureLargeAddressAware(true); Say("4 GB: wird beim Beenden von Cube World in die Cube.exe eingetragen (Sicherung Cube.exe.vor-4GB.bak), gilt ab dem naechsten Start."); }
					else Say("4 GB: Grundeinstellung aus. Ein schon gesetztes Bit bleibt -- zuruecknehmen ueber LAB -> Logger -> 4 GB.");
				}
				else if (id == NID_DEF_SAVE) { std::string r = DefaultsSave(); Say(r.empty() ? "Nichts zu speichern." : "Als Grundeinstellung gespeichert (Strukturen\\Grundeinstellungen): " + r); }
				else if (id == NID_DEF_LOAD) {
					std::string r = DefaultsLoad();
					if (r.empty()) { Say("Keine Grundeinstellungen in Strukturen\\Grundeinstellungen."); return 0; }
					const int lvl = CfgInt("tierdichte", 2), pct = CfgInt("tierwelt_anteil", 50);
					LabBridge::Post("Grundeinstellungen", [lvl, pct](cube::GameController*) { BiomeMod::SetSpawnDensity(lvl); Wildlife::SetShare(pct); });
					SendMessageA(n_dens, CB_SETCURSEL, (std::max)(0, (std::min)(3, lvl - 1)), 0);
					SendMessageA(n_share, CB_SETCURSEL, (std::max)(0, (std::min)(4, pct / 25)), 0);
					BSetChk(n_laa, CfgInt("vier_gb", 0) != 0);
					Say("Grundeinstellungen geladen: " + r + "-- Tierdichte/Anteil sofort, Karte & Leistung und 4 GB ab dem naechsten Start.");
				}
				else if (id == NID_SHARE && code == CBN_SELCHANGE) {
					int pct = ComboSel(n_share) * 25; SaveCfg("tierwelt_anteil", pct);
					LabBridge::Post("Tierwelt-Anteil", [pct](cube::GameController*) { Wildlife::SetShare(pct); });
					Say("Anteil eigener Tiere am nativen Spawn: " + std::to_string(pct) + " % (neu erzeugte Zonen).");
				}
				else if (id == NID_DENS && code == CBN_SELCHANGE) {
					int lvl = ComboSel(n_dens) + 1; SaveCfg("tierdichte", lvl);
					LabBridge::Post("Tierdichte", [lvl](cube::GameController*) { BiomeMod::SetSpawnDensity(lvl); });
					Say("Tierdichte " + std::to_string(lvl) + " gesetzt -- gilt fuer Zonen, die ab jetzt erzeugt werden.");
				}
				return 0;
			}
			case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// =====================================================================
	// Die frueheren Unterfenster sind jetzt Seiten im Hauptfenster. Jede Seite (ausser
	// Biome, das selbst rollt) sitzt in einem "Rahmen" mit Bildlaufleisten; die Seite
	// behaelt ihre feste Groesse, der Rahmen verschiebt sie.
	enum { TAB_BUILD, TAB_MOBS, TAB_BIOME, TAB_ZMOD, TAB_RECIPE, TAB_ITEMS, TAB_SKILLS, TAB_VENDORS, TAB_NET, TAB_PERF, TAB_DUNGEON, TAB_TIERE, TAB_LOGGER, TAB_COUNT };
	static_assert(TAB_ZMOD == TAB_ZMOD_FWD, "TAB_ZMOD_FWD anpassen");
	static const char* kTabNames[TAB_COUNT] = { "Bauen", "Mobs", "Biome I / II", "Zonen-Modelle", "Rezepte", "Gegenstaende", "Skills", "Haendler", "Netz & Tiere", "Karte & Leistung", "Dungeons & F9", "Tier-Werkstatt", "Logger" };
	static int  g_mainTab = TAB_BUILD;
	static HWND g_tabBtn[TAB_COUNT] = {};
	static HWND g_tabPage[TAB_COUNT] = {};        // Rahmen bzw. (Biome) die Seite selbst

	static void HostScroll(HWND h, int sx, int sy) {
		HWND c = (HWND)GetWindowLongPtrA(h, GWLP_USERDATA);
		if (!c) return;
		RECT wr; GetWindowRect(c, &wr);
		const int pw = wr.right - wr.left, ph = wr.bottom - wr.top;
		for (int pass = 0; pass < 2; pass++) {        // Leisten aendern die Clientgroesse -> zweimal
			RECT cr; GetClientRect(h, &cr);
			sx = (std::max)(0, (std::min)(sx, pw - (int)cr.right)); sy = (std::max)(0, (std::min)(sy, ph - (int)cr.bottom));
			SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
			si.nMin = 0; si.nMax = pw - 1; si.nPage = (UINT)cr.right;  si.nPos = sx; SetScrollInfo(h, SB_HORZ, &si, TRUE);
			si.nMin = 0; si.nMax = ph - 1; si.nPage = (UINT)cr.bottom; si.nPos = sy; SetScrollInfo(h, SB_VERT, &si, TRUE);
		}
		SetWindowPos(c, NULL, -sx, -sy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_SIZE: HostScroll(h, GetScrollPos(h, SB_HORZ), GetScrollPos(h, SB_VERT)); return 0;
			case WM_VSCROLL: case WM_HSCROLL: {
				const int bar = m == WM_VSCROLL ? SB_VERT : SB_HORZ;
				SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL; GetScrollInfo(h, bar, &si);
				int p = si.nPos;
				switch (LOWORD(wp)) {
					case SB_LINEUP: p -= 40; break;                 case SB_LINEDOWN: p += 40; break;
					case SB_PAGEUP: p -= (int)si.nPage - 40; break; case SB_PAGEDOWN: p += (int)si.nPage - 40; break;
					case SB_THUMBTRACK: case SB_THUMBPOSITION: p = si.nTrackPos; break;
					case SB_TOP: p = 0; break;                      case SB_BOTTOM: p = si.nMax; break;
				}
				if (bar == SB_VERT) HostScroll(h, GetScrollPos(h, SB_HORZ), p); else HostScroll(h, p, GetScrollPos(h, SB_VERT));
				return 0;
			}
			case WM_MOUSEWHEEL: {
				int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
				if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) HostScroll(h, GetScrollPos(h, SB_HORZ) - steps * 60, GetScrollPos(h, SB_VERT));
				else HostScroll(h, GetScrollPos(h, SB_HORZ), GetScrollPos(h, SB_VERT) - steps * 60);
				return 0;
			}
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// Seite "Dungeons": Raum-Baukasten (Dungeons.cpp) -- Liste, neu laden, Testbau 40 Bloecke vor dem Spieler.
	static HWND g_dgList = NULL, g_dgStatus = NULL;
	static void DgFill() {
		if (!g_dgList) return;
		SendMessageA(g_dgList, LB_RESETCONTENT, 0, 0);
		for (const std::string& n : Dungeons::Names()) SendMessageA(g_dgList, LB_ADDSTRING, 0, (LPARAM)n.c_str());
		SendMessageA(g_dgList, LB_SETCURSEL, 0, 0);
		if (g_dgStatus) SetWindowTextA(g_dgStatus, Dungeons::Status().c_str());
	}
	static LRESULT CALLBACK DungeonProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				HINSTANCE hi = GetModuleHandleA(NULL);
				auto C = [&](const char* cls, const char* txt, DWORD st, int x, int y, int w, int ht, int id) {
					HWND c = CreateWindowExA(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, ht, h, (HMENU)(INT_PTR)id, hi, NULL);
					SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
				};
				HWND t = C("STATIC", "Dungeon-Baukasten (Strukturen\\Dungeons)", 0, 12, 12, 600, 18, 0);
				SendMessageA(t, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
				g_dgList = C("LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 12, 38, 220, 200, 10);
				C("BUTTON", "Neu laden", BS_PUSHBUTTON, 245, 38, 220, 28, 11);
				C("BUTTON", "Testbau 40 Bloecke vor mir", BS_PUSHBUTTON, 245, 72, 220, 28, 12);
				C("BUTTON", "Ordner oeffnen", BS_PUSHBUTTON, 245, 106, 220, 28, 13);
				C("BUTTON", "Status", BS_PUSHBUTTON, 245, 140, 220, 28, 14);
				g_dgStatus = C("STATIC", "", SS_LEFT, 12, 248, 720, 36, 0);
				C("STATIC",
					"Ein Dungeon = Ordner mit dungeon.txt (format=2) + Module\\*.cub (+ .txt je Modul). Module werden ueber passende "
					"Anschluesse (anschluss=typ,richtung,u,z) zufaellig verbunden, bis die Mindestgroesse erreicht ist; offene "
					"Anschluesse werden zugemauert. Entsteht mit der Zone (Anteil in dungeon.txt), immer in EINER Zone.\r\n"
					"Markerfarben: Monster 254,1,254 | Boss 254,1,128 | Truhe 254,128,1 | Vase 128,1,254 | Licht 254,254,1.\r\n"
					"Modul-.txt: art=raum|eingang|boss, gewicht=, max=, dach=0/1, anschluss=gang,S,8,2 (mehrfach).\r\n"
					"Testbau: Mauern, Lichter und Monster sofort; Truhen und Vasen nur bei Zonenerzeugung (neue Welt/Zone).\r\n"
					"Generator + Pruefung + Vorschau: Projekt\\Werkzeuge\\Dungeons\\dungeons_bau.py <Strukturen> --pruefen --vorschau",
					SS_LEFT, 12, 292, 720, 120, 0);
				{
					HWND t2 = C("STATIC", "F9-Anzeige (unten rechts): was angezeigt wird", 0, 12, 420, 600, 18, 0);
					SendMessageA(t2, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
					for (int k = 0; k < Tracking::T_COUNT; k++) {
						HWND cb = C("BUTTON", Tracking::Label(k), BS_AUTOCHECKBOX, 22 + (k % 2) * 360, 444 + (k / 2) * 24, 350, 20, 40 + k);
						SendMessageA(cb, BM_SETCHECK, Tracking::On(k) ? BST_CHECKED : BST_UNCHECKED, 0);
					}
					C("STATIC", "Eigene Dungeons, kleine Tempel und Staedte erscheinen, sobald ihre Zone einmal erzeugt wurde (gespeichert je Welt in "
					            "Save/AdventureMod.db). Native Orte, Bergdoerfer und eigene Biome werden aus dem Weltsamen berechnet.",
					  SS_LEFT, 12, 444 + ((Tracking::T_COUNT + 1) / 2) * 24 + 6, 720, 40, 0);
				}
				DgFill();
				return 0;
			}
			case WM_COMMAND: {
				const int id = LOWORD(wp);
				if (id == 11) { Dungeons::Reload(); DgFill(); Say("Dungeons neu geladen: " + Dungeons::Status()); }
				else if (id == 12) {
					int i = (int)SendMessageA(g_dgList, LB_GETCURSEL, 0, 0);
					char nm[128] = {}; if (i >= 0) SendMessageA(g_dgList, LB_GETTEXT, i, (LPARAM)nm);
					if (!nm[0]) { Say("Kein Dungeon gewaehlt."); return 0; }
					std::string name = nm; uint32_t seed = GetTickCount();
					Say("-> Dungeon " + name + " (Testbau)");
					LabBridge::Post("Dungeon setzen", [name, seed](cube::GameController* gc) {
						Dungeons::Tick(gc);
						std::cout << "[Dungeons] Testbau: " << Dungeons::PlaceHere(gc, name, seed) << std::endl;
					});
				}
				else if (id == 13) ShellExecuteA(NULL, "open", Dungeons::Dir().c_str(), NULL, NULL, SW_SHOWNORMAL);
				else if (id == 14) { DgFill(); Say(Dungeons::Status() + " " + Tracking::Status() + " " + ModDb::Status()); }
				else if (id >= 40 && id < 40 + Tracking::T_COUNT) {
					bool on = SendMessageA((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED;
					Tracking::SetOn(id - 40, on);
					Say(std::string("F9-Anzeige: ") + Tracking::Label(id - 40) + (on ? " AN." : " AUS."));
				}
				return 0;
			}
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// Seite "Logger": der Logger ist ein eigener Prozess (ueberlebt Abstuerze) -- hier nur Start + Erklaerung.
	static LRESULT CALLBACK LogPageProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				HINSTANCE hi = GetModuleHandleA(NULL);
				HWND t = CreateWindowExA(0, "STATIC", "Logger / Absturzfaenger", WS_CHILD | WS_VISIBLE, 12, 12, 600, 18, h, NULL, hi, NULL);
				SendMessageA(t, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
				HWND b = CreateWindowExA(0, "BUTTON", "Logger starten", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 38, 200, 28, h, (HMENU)(INT_PTR)1, hi, NULL);
				SendMessageA(b, WM_SETFONT, (WPARAM)g_font, TRUE);
				HWND d = CreateWindowExA(0, "STATIC",
					"Der Logger laeuft als eigenes Programm in einem eigenen Fenster. Nur so kann er weiterarbeiten, "
					"wenn Cube World abstuerzt oder haengt.\r\n\r\n"
					"Im Logger-Fenster:\r\n"
					"  - Absturzberichte (mit Aufrufstapel und Minidump)\r\n"
					"  - 'Leistung ausgeben' und 'Threads ausgeben' (auch wenn Spiel oder LAB haengen)\r\n"
					"  - '4 GB freischalten' (nur auf deinen Klick)",
					WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 78, 600, 140, h, NULL, hi, NULL);
				SendMessageA(d, WM_SETFONT, (WPARAM)g_font, TRUE);
				return 0;
			}
			case WM_COMMAND:
				if (LOWORD(wp) == 1) {
					if (CrashLogger::StartLogger()) Say("Logger laeuft als eigener Prozess -- er bleibt offen, wenn Cube World abstuerzt.");
					else Say("Logger konnte nicht gestartet werden (siehe Protokoll).");
				}
				return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	static void EnsureTabPage(int k) {
		if (k <= TAB_BUILD || k >= TAB_COUNT || g_tabPage[k]) return;
		HINSTANCE hi = GetModuleHandleA(NULL);
		if (k == TAB_BIOME) {                         // rollt selbst (Bildlaufleisten, Mausrad)
			HWND w = CreateWindowExA(0, "CWLabBiome", "", WS_CHILD | WS_VSCROLL | WS_HSCROLL, 0, 0, 800, 600, g_hwnd, NULL, hi, NULL);
			if (w) BioScrollTo(w, 0, 0);
			g_tabPage[k] = w;
			return;
		}
		const char* cls = nullptr; int w = 0, ht = 0;
		switch (k) {
			case TAB_MOBS:   cls = "CWLabMob";     w = 1560; ht = 680; break;
			case TAB_ZMOD:   cls = "CWLabZMod";    w = 790;  ht = 640; break;
			case TAB_RECIPE: cls = "CWLabRecipe";  w = 790;  ht = 630; break;
			case TAB_ITEMS:  cls = "CWLabItems";   w = 800;  ht = 820; break;
			case TAB_SKILLS: cls = "CWLabSkills";  w = 800;  ht = 640; break;
			case TAB_VENDORS: cls = "CWLabVendors"; w = 800; ht = 870; break;
			case TAB_NET:     cls = "CWLabNet";     w = 660; ht = 500; break;
			case TAB_PERF:   cls = "CWLabPerf";    w = 646;  ht = 780; break;
			case TAB_DUNGEON: cls = "CWLabDungeon"; w = 760; ht = 700; break;
			case TAB_TIERE:  cls = "CWLabTiere";   w = 1180; ht = 700; break;
			case TAB_LOGGER: cls = "CWLabLogPage"; w = 640;  ht = 240; break;
		}
		HWND host = CreateWindowExA(0, "CWLabHost", "", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, g_hwnd, NULL, hi, NULL);
		if (!host) return;
		HWND page = CreateWindowExA(0, cls, "", WS_CHILD | WS_VISIBLE, 0, 0, w, ht, host, NULL, hi, NULL);
		SetWindowLongPtrA(host, GWLP_USERDATA, (LONG_PTR)page);
		g_tabPage[k] = host;
	}

	// =====================================================================
	// 8. Hauptfenster
	// =====================================================================
	static void PlaceTarget(bool ahead) {
		LabBridge::Snapshot s = LabBridge::GetSnapshot();
		if (!s.inGame) { Say("Nicht im Spiel -- keine Spielerposition."); return; }
		int rot = (int)SendMessageA(g_hRot, CB_GETCURSEL, 0, 0); if (rot < 0) rot = 0;
		int W = g_cur.Valid() ? ((rot & 1) ? g_cur.sy : g_cur.sx) : 1;
		int D = g_cur.Valid() ? ((rot & 1) ? g_cur.sx : g_cur.sy) : 1;
		// "40 Bloecke versetzt" -- bewaehrte Regel aus den Testhaeusern (weiter
		// weg rendert das Spiel nicht mehr zuverlaessig). Immer +X wie beim Objekt-Tester.
		int x = ahead ? s.px + 40 : s.px - W / 2;
		int y = s.py - D / 2;
		SetInt(g_hPX, x); SetInt(g_hPY, y); SetInt(g_hPZ, s.pz);
	}

	static void DoPlace() {
		if (!g_cur.Valid()) { Say("Keine Struktur."); return; }
		LabBridge::PlaceOptions o;
		o.x = GetInt(g_hPX, 0); o.y = GetInt(g_hPY, 0); o.z = GetInt(g_hPZ, 0);
		o.rot = (int)SendMessageA(g_hRot, CB_GETCURSEL, 0, 0); if (o.rot < 0) o.rot = 0;
		o.typ = GetInt(g_hTyp, 0);
		o.fundament = Checked(g_hFund) ? 1 : 0;
		o.replaceMarkers = Checked(g_hMarkers);
		o.remember = Checked(g_hRemember);
		o.fx = g_curFx;
		Lab::Structure s = g_cur;
		s.name = Lab::SafeFileName(GetText(g_hName));
		if (o.typ != 0)
			Say("ACHTUNG typ != 0: native Markerlogik zur Laufzeit ist experimentell. Objekte werden nachtraeglich indexiert.");
		LabBridge::Post("Struktur setzen", [s, o](cube::GameController* gc) {
			std::string rep;
			LabBridge::PlaceNative(gc, s, o, rep);
		});
	}

	static void DoScanBox(bool aroundMe) {
		if (!ConfirmDiscard()) return;
		int x, y, z, sx, sy, sz;
		if (aroundMe) {
			LabBridge::Snapshot s = LabBridge::GetSnapshot();
			if (!s.inGame) { Say("Nicht im Spiel."); return; }
			x = s.px - 20; y = s.py - 20; z = s.pz - 4; sx = 41; sy = 41; sz = 36;
		} else {
			x = GetInt(g_hPX, 0); y = GetInt(g_hPY, 0); z = GetInt(g_hPZ, 0);
			sx = GetInt(g_hSX, 32); sy = GetInt(g_hSY, 32); sz = GetInt(g_hSZ, 32);
		}
		if ((long long)sx * sy * sz > 2000000) { Say("Scanquader zu gross (max. 2 Mio. Voxel)."); return; }
		Say("Scanne Welt " + DimStr(sx, sy, sz) + " ab (" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ") ...");
		LabBridge::Post("Welt scannen", [x, y, z, sx, sy, sz](cube::GameController* gc) {
			Lab::Structure s; s.name = "Weltscan";
			if (LabBridge::ScanWorld(gc, x, y, z, sx, sy, sz, s)) LabBridge::PostStructure(s);
		});
	}

	static void RefreshPlacements(bool force) {
		std::vector<LabBridge::Placement> v = LabBridge::GetPlacements();
		if (!force && v.size() == g_lastPlaceCount) return;
		g_lastPlaceCount = v.size();
		SendMessageA(g_hPlaceList, LB_RESETCONTENT, 0, 0);
		for (const auto& p : v) {
			char b[200];
			sprintf_s(b, "%s  (%d,%d,%d) rot %d typ 0x%X%s", p.name.c_str(), p.x, p.y, p.z, p.rot, p.typ, p.fundament ? " fund." : "");
			SendMessageA(g_hPlaceList, LB_ADDSTRING, 0, (LPARAM)b);
		}
	}

	static void OnTimer() {
		TierWerkstatt::ApiPoll();                         // Datei-API der Tier-Werkstatt (Strukturen\LabApi)
		CrashLogger::PerfBeat(CrashLogger::BEAT_LAB);           // Lebenszeichen fuer den Logger
		AppendLog(LabBridge::TakeLog());

		Lab::Structure s;
		while (LabBridge::TakeStructure(s)) SetCurrent(s, true);

		std::vector<LabBridge::LibEntry> lib;
		if (LabBridge::TakeNativeLibrary(lib)) { g_native.swap(lib); ReloadLibrary(); }

		LabBridge::Snapshot sn = LabBridge::GetSnapshot();
		if (sn.inGame && !g_nativeRequested) RequestNative();      // einmal automatisch
		char b[256];
		sprintf_s(b, "%s   Spieler (%d, %d, %d)   Zone (%d, %d)   Seed %08X   Auftraege %d   Leerfarbe %s   Rueckgaengig %s",
			sn.inGame ? "IM SPIEL" : "kein Spiel", sn.px, sn.py, sn.pz, sn.zx, sn.zy, sn.seed,
			LabBridge::PendingJobs(), sn.emptyColorBlack ? "ok" : "ABWEICHEND", sn.undoAvailable ? "bereit" : "-");
		static std::string last;
		if (last != b) { last = b; SetWindowTextA(g_hStatus, b); }

		static int tick = 0;
		if (++tick % 10 == 0) RefreshPlacements(false);

		// Automatische Sicherung alle 60 s, solange es ungespeicherte Aenderungen gibt.
		// Das LAB laeuft im Spielprozess -- stuerzt Cube World ab, ist alles
		// Ungespeicherte weg. Die Sicherung landet in Strukturen\_autosave\ und
		// erscheint in der Bibliothek als [auto].
		if (tick % 600 == 0 && g_dirty && g_cur.Valid()) {
			std::string dir = Lab::StructuresDir() + "\\_autosave";
			Lab::EnsureDir(dir);
			std::string name = Lab::SafeFileName(GetText(g_hName));
			std::string path = dir + "\\" + (name.empty() ? std::string("unbenannt") : name) + ".cub", err;
			if (Lab::SaveCub(path, g_cur, err)) AppendLog("[LAB] Automatisch gesichert: " + path + "\n");
		}
	}

	// =====================================================================
	// Layout -- passt sich der Fenstergroesse an
	// =====================================================================
	// Frueher standen alle Steuerelemente auf festen Pixeln fuer 1510x990.
	// Auf kleineren Bildschirmen oder mit Windows-Skalierung (125 %, 150 %)
	// ragte das Fenster aus dem Bild. Jetzt:
	//   * Startgroesse = hoechstens der Arbeitsbereich des Monitors
	//   * Fenster frei skalierbar, Mindestgroesse 1260 x 700
	//   * Layout() setzt jedes Element aus der aktuellen Clientgroesse
	//   * die rechte Spalte hat Reiter statt alles untereinander
	//   * die Spielfunktionen sind eine Liste statt 30 fester Knoepfe
	enum Ctl_ {
		C_FN_LBL, C_FN_LIST, C_FN_RUN, C_MOBS, C_PERF, C_BIOME, C_LOGGER, C_LOG_LBL, C_LOG,
		C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_EUNDO, C_NAME_LBL, C_NAME,
		C_SIZE_LBL, C_SX, C_SY, C_SZ, C_RESIZE, C_CROP, C_ROT90, C_MIRROR, C_SCALE, C_SCALE_F, C_INFO,
		C_3D, C_VROT_L, C_VROT_R, C_ZOUT, C_ZIN, C_FIT, C_CUT,
		C_LAYER, C_LUP, C_LDN, C_LLBL, C_LCLEAR, C_LFILL, C_LCOPY, C_HOVER,
		C_T0, C_T1, C_T2, C_T3, C_RGB_LBL, C_R, C_G, C_B, C_RGB_SET, C_CHOOSE, C_PAL,
		C_TAB0, C_TAB1, C_TAB2,
		C_FILTER_LBL, C_FILTER, C_ONLYBIG, C_LIB, C_LIB_OPEN, C_LIB_RELOAD, C_FILE_OPEN2, C_LIB_EXPORT,
		C_CORNER_LBL, C_PX, C_PY, C_PZ, C_TGT_AHEAD, C_TGT_HERE, C_ROT_LBL, C_ROT, C_TYP_LBL, C_TYP, C_TYP_SUG,
		C_FUND, C_MARKERS, C_REMEMBER, C_PLACE, C_WUNDO, C_SCANBOX, C_SCANME, C_HINT,
		C_PL_LBL, C_PL_LIST, C_PL_OPEN, C_PL_DEL,
		C_TAB3, C_FX_LBL, C_FX_LIST, C_FX_FN, C_FX_ADD, C_FX_GLOW, C_FX_DEL, C_FX_SAVE, C_FX_INFO,
		C_STATUS, C_ZMOD, C_RECIPE, C_COUNT
	};
	static HWND g_c[C_COUNT];

	// ---- Gebaeude-Funktionen (Seite "Funktionen") -------------------------------------------------------
	static void FxRefresh() {
		HWND lb = g_c[C_FX_LIST]; if (!lb) return;
		int sel = (int)SendMessageA(lb, LB_GETCURSEL, 0, 0);
		SendMessageA(lb, LB_RESETCONTENT, 0, 0);
		for (const BuildingFx::Entry& x : g_curFx.e) {
			int cnt = 0;
			for (size_t i = 0; i + 2 < g_cur.rgb.size(); i += 3) if (g_cur.rgb[i] == x.r && g_cur.rgb[i + 1] == x.g && g_cur.rgb[i + 2] == x.b) cnt++;
			char b[200]; sprintf_s(b, "%3d,%3d,%3d  ->  %s%s   (%d Voxel)", x.r, x.g, x.b, x.fn.empty() ? "" : x.fn.c_str(), x.glow ? (x.fn.empty() ? "LEUCHTET" : " + leuchtet") : "", cnt);
			SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)b);
		}
		if (sel >= 0 && sel < (int)g_curFx.e.size()) SendMessageA(lb, LB_SETCURSEL, sel, 0);
		std::vector<BuildingFx::GlowVoxel> gv; std::vector<BuildingFx::Group> gr;
		BuildingFx::Analyze(g_cur, g_curFx, gv, gr);
		char info[700];
		sprintf_s(info, "Beim Setzen: %d Funktionsgruppen, %d Leuchtbloecke.\r\n"
		          "Funktion: Voxel genau dieser Farbe werden beim SETZEN ein Objekt (Truhe, Bett, Amboss, Lagerfeuer/Braustand ...) "
		          "oder ein Bewohner (Haendler, Alchemist, unser Haendler). Nebeneinander liegende Voxel = ein Objekt.\r\n"
		          "Leuchtet: Voxel dieser Farbe werden Lichtbloecke (Blocktyp 0x0D, erhellen etwa 3 Bloecke um sich).\r\n"
		          "Gespeichert als <name>.funk neben der .cub (auch mit 'Speichern').",
		          (int)gr.size(), (int)gv.size());
		SetWindowTextA(g_c[C_FX_INFO], info);
	}
	static void Entry_FxAdd(bool glow) {
		uint8_t r = g_col[0], g = g_col[1], b = g_col[2];
		if (r == 0 && g == 0 && b == 0) { Say("Schwarz ist die Leerfarbe -- bitte eine andere Pinselfarbe."); return; }
		BuildingFx::Entry* ex = nullptr;
		for (BuildingFx::Entry& x : g_curFx.e) if (x.r == r && x.g == g && x.b == b) ex = &x;
		if (!ex) { BuildingFx::Entry x; x.r = r; x.g = g; x.b = b; g_curFx.e.push_back(x); ex = &g_curFx.e.back(); }
		if (glow) ex->glow = !ex->glow;
		else {
			int sel = (int)SendMessageA(g_c[C_FX_FN], CB_GETCURSEL, 0, 0), n = 0;
			const BuildingFx::Choice* ch = BuildingFx::Choices(&n);
			if (sel >= 0 && sel < n) ex->fn = ch[sel].name;
		}
		if (ex->fn.empty() && !ex->glow) { for (size_t i = 0; i < g_curFx.e.size(); i++) if (&g_curFx.e[i] == ex) { g_curFx.e.erase(g_curFx.e.begin() + i); break; } }
		g_dirty = true; FxRefresh();
	}


	static void Pos(int c, int x, int y, int w, int h) {
		if (g_c[c]) MoveWindow(g_c[c], x, y, (std::max)(w, 1), (std::max)(h, 1), FALSE);
	}

	static void ShowPage(int p) {
		g_page = p;
		for (int k = 0; k < 4; k++) {
			for (HWND h : g_pageCtl[k]) ShowWindow(h, k == p ? SW_SHOW : SW_HIDE);
			SendMessageA(g_c[k < 3 ? C_TAB0 + k : C_TAB3], BM_SETCHECK, k == p ? BST_CHECKED : BST_UNCHECKED, 0);
		}
	}

	static void Layout() {
		if (!g_hwnd) return;
		RECT rc; GetClientRect(g_hwnd, &rc);
		const int cw = rc.right, ch = rc.bottom, M = 8;
		const int statusH = 18, bottom = ch - statusH - 6;
		const int T = M + 32;                       // unter der Reiterleiste

		// ---- Reiterleiste + Seiten der anderen Reiter ----
		{   // Reiter teilen sich die Breite (13 Reiter passen so auch in schmalere Fenster)
			const int tw = (std::max)(70, (std::min)(121, (cw - 2 * M) / TAB_COUNT));
			for (int k = 0; k < TAB_COUNT; k++) if (g_tabBtn[k]) MoveWindow(g_tabBtn[k], M + k * tw, M, tw - 3, 26, FALSE);
		}
		for (int k = 1; k < TAB_COUNT; k++) if (g_tabPage[k]) MoveWindow(g_tabPage[k], 0, T, cw, (std::max)(50, bottom - T), TRUE);

		// ---- links: Spielfunktionen + Protokoll ----
		const int lw = 232;
		int x = M, y = T;
		Pos(C_FN_LBL, x, y, lw, 16); y += 18;
		int fnH = (std::max)(120, (bottom - y - 30 - 22) * 60 / 100);
		Pos(C_FN_LIST, x, y, lw, fnH); y += fnH + 4;
		Pos(C_FN_RUN, x, y, lw, 24); y += 30;      // Mobs, Biome, Leistung ... sind jetzt Reiter oben
		Pos(C_LOG_LBL, x, y, lw, 16); y += 18;
		Pos(C_LOG, x, y, lw, (std::max)(40, bottom - y));

		// ---- rechts: Werkzeug, Farbe, Palette, Reiter ----
		const int rw = 420, rx = cw - M - rw;
		y = T;
		int tw = (rw - 9) / 4;
		for (int k = 0; k < 4; k++) Pos(C_T0 + k, rx + k * (tw + 3), y, tw, 24);
		y += 28;
		Pos(C_RGB_LBL, rx, y + 4, 34, 16);
		Pos(C_R, rx + 36, y, 46, 22); Pos(C_G, rx + 86, y, 46, 22); Pos(C_B, rx + 136, y, 46, 22);
		Pos(C_RGB_SET, rx + 188, y, 96, 22); Pos(C_CHOOSE, rx + 290, y, rw - 290, 22);
		y += 28;
		Pos(C_PAL, rx, y, rw, kPalH); y += kPalH + 6;
		int tbw = (rw - 9) / 4;
		for (int k = 0; k < 4; k++) Pos(k < 3 ? C_TAB0 + k : C_TAB3, rx + k * (tbw + 3), y, tbw, 24);
		y += 30;
		const int py = y, ph = bottom - y;

		// Seite 0: Bibliothek
		Pos(C_FILTER_LBL, rx, py + 4, 40, 16); Pos(C_FILTER, rx + 42, py, rw - 42 - 140, 22); Pos(C_ONLYBIG, rx + rw - 134, py + 1, 134, 20);
		int listH = (std::max)(60, ph - 26 - 58);
		Pos(C_LIB, rx, py + 26, rw, listH);
		int by = py + 26 + listH + 4, bw = (rw - 6) / 3;
		Pos(C_LIB_OPEN, rx, by, bw, 24); Pos(C_LIB_RELOAD, rx + bw + 3, by, bw, 24); Pos(C_FILE_OPEN2, rx + 2 * (bw + 3), by, bw, 24);
		Pos(C_LIB_EXPORT, rx, by + 28, rw, 24);

		// Seite 1: Ins Spiel
		y = py;
		Pos(C_CORNER_LBL, rx, y + 4, 76, 16); Pos(C_PX, rx + 78, y, 70, 22); Pos(C_PY, rx + 152, y, 70, 22); Pos(C_PZ, rx + 226, y, 56, 22); y += 24;
		Pos(C_TGT_AHEAD, rx + 78, y, 170, 22); Pos(C_TGT_HERE, rx + 252, y, 80, 22); y += 26;
		Pos(C_ROT_LBL, rx, y + 4, 60, 16); Pos(C_ROT, rx + 62, y, 70, 120);
		Pos(C_TYP_LBL, rx + 144, y + 4, 28, 16); Pos(C_TYP, rx + 174, y, 56, 22); Pos(C_TYP_SUG, rx + 234, y, 80, 22); y += 26;
		Pos(C_FUND, rx, y, rw, 20); y += 20;
		Pos(C_MARKERS, rx, y, rw, 20); y += 20;
		Pos(C_REMEMBER, rx, y, rw, 20); y += 23;
		int b2 = (rw - 3) / 2;
		Pos(C_PLACE, rx, y, b2, 30); Pos(C_WUNDO, rx + b2 + 3, y, b2, 30); y += 33;
		Pos(C_SCANBOX, rx, y, b2, 26); Pos(C_SCANME, rx + b2 + 3, y, b2, 26); y += 32;
		Pos(C_HINT, rx, y, rw, (std::max)(20, bottom - y));

		// Seite 2: Platzierungen
		Pos(C_PL_LBL, rx, py, rw, 16);
		Pos(C_PL_LIST, rx, py + 18, rw, (std::max)(60, ph - 18 - 30));
		Pos(C_PL_OPEN, rx, bottom - 24, b2, 24); Pos(C_PL_DEL, rx + b2 + 3, bottom - 24, b2, 24);

		// Seite 3: Gebaeude-Funktionen
		Pos(C_FX_LBL, rx, py, rw, 16);
		int fxH = (std::max)(60, ph - 18 - 140);
		Pos(C_FX_LIST, rx, py + 18, rw, fxH);
		int fy = py + 18 + fxH + 4;
		Pos(C_FX_FN, rx, fy, rw, 300); fy += 26;
		Pos(C_FX_ADD, rx, fy, b2, 24); Pos(C_FX_GLOW, rx + b2 + 3, fy, b2, 24); fy += 27;
		Pos(C_FX_DEL, rx, fy, b2, 24); Pos(C_FX_SAVE, rx + b2 + 3, fy, b2, 24); fy += 28;
		Pos(C_FX_INFO, rx, fy, rw, (std::max)(20, bottom - fy));

		// ---- Mitte: Werkzeugleiste, 3D, Ebene ----
		const int mx = M + lw + M, mw = rx - M - mx;
		y = T;
		Pos(C_NEW, mx, y, 58, 24); Pos(C_OPEN, mx + 61, y, 76, 24);
		Pos(C_SAVE, mx + 140, y, 84, 24); Pos(C_SAVEAS, mx + 227, y, 120, 24); Pos(C_EUNDO, mx + 350, y, 92, 24);
		Pos(C_NAME_LBL, mx + 450, y + 5, 40, 16); Pos(C_NAME, mx + 490, y + 1, (std::max)(80, mw - 490), 22);
		y += 28;
		Pos(C_SIZE_LBL, mx, y + 5, 46, 16); Pos(C_SX, mx + 48, y + 1, 44, 22); Pos(C_SY, mx + 95, y + 1, 44, 22); Pos(C_SZ, mx + 142, y + 1, 44, 22);
		Pos(C_RESIZE, mx + 190, y, 58, 24); Pos(C_CROP, mx + 251, y, 74, 24); Pos(C_ROT90, mx + 328, y, 54, 24); Pos(C_MIRROR, mx + 385, y, 60, 24);
		Pos(C_SCALE, mx + 448, y, 78, 24); Pos(C_SCALE_F, mx + 528, y + 1, 44, 22);
		y += 28;
		Pos(C_INFO, mx, y, mw, 16); y += 20;

		int avail = bottom - y - 80;            // 3D-Leiste, Ebenen-Leiste, Hover-Zeile
		int h3 = (std::max)(120, avail * 45 / 100), hl = (std::max)(120, avail - h3);
		Pos(C_3D, mx, y, mw, h3); y += h3 + 3;
		Pos(C_VROT_L, mx, y, 70, 24); Pos(C_VROT_R, mx + 73, y, 70, 24); Pos(C_ZOUT, mx + 150, y, 56, 24); Pos(C_ZIN, mx + 209, y, 56, 24);
		Pos(C_FIT, mx + 268, y, 70, 24); Pos(C_CUT, mx + 346, y + 2, (std::max)(80, mw - 346), 20);
		y += 28;
		Pos(C_LAYER, mx, y, mw, hl); y += hl + 3;
		Pos(C_LUP, mx, y, 66, 24); Pos(C_LDN, mx + 69, y, 66, 24); Pos(C_LLBL, mx + 142, y + 5, 110, 16);
		Pos(C_LCLEAR, mx + 256, y, 90, 24); Pos(C_LFILL, mx + 349, y, 90, 24); Pos(C_LCOPY, mx + 442, y, (std::max)(80, (std::min)(170, mw - 442)), 24);
		y += 28;
		Pos(C_HOVER, mx, y, mw, 16);

		Pos(C_STATUS, M, ch - statusH - 2, cw - 2 * M, statusH);
		InvalidateRect(g_hwnd, NULL, TRUE);
	}

	static void ShowMainTab(int k) {
		if (k < 0 || k >= TAB_COUNT) return;
		g_mainTab = k;
		EnsureTabPage(k);
		for (int t = 0; t < TAB_COUNT; t++) SendMessageA(g_tabBtn[t], BM_SETCHECK, t == k ? BST_CHECKED : BST_UNCHECKED, 0);
		const bool build = k == TAB_BUILD;
		for (int c = 0; c < C_COUNT; c++) if (g_c[c] && c != C_STATUS) ShowWindow(g_c[c], build ? SW_SHOW : SW_HIDE);
		if (build) ShowPage(g_page);                // rechte Spalte: nur die gewaehlte Unterseite
		for (int t = 1; t < TAB_COUNT; t++) if (g_tabPage[t]) ShowWindow(g_tabPage[t], t == k ? SW_SHOW : SW_HIDE);
		Layout();
		if (k == TAB_ZMOD && g_zmWnd) { ZmFillAreas(ZmFromUi().area); ZmRefreshStatus(); }
		if (k == TAB_RECIPE && g_rzWnd) { RzRefreshStatus(); RzRefreshChoices(); }
		if (k == TAB_ITEMS && g_itWnd) ItRefreshStatus();
		if (k == TAB_VENDORS && g_vdWnd) VdFillTaxi();
		if (!g_visible) { g_visible = true; ShowWindow(g_hwnd, SW_SHOW); }
	}

	static void BuildLayout() {
		memset(g_c, 0, sizeof(g_c));
		auto B  = [](int c, const char* t, int id) { g_c[c] = Btn(t, 0, 0, 10, 10, id); };
		auto Lb = [](int c, const char* t, bool bold = false) { g_c[c] = Lbl(t, 0, 0, 10, 10, bold); };
		auto E  = [](int c, const char* t, int id = -1) { g_c[c] = Edt(t, 0, 0, 10, 10, id); };

		// links
		Lb(C_FN_LBL, "Spielfunktionen  (Doppelklick = ausfuehren)", true);
		g_c[C_FN_LIST] = Ctl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, ID_FN_LIST, WS_EX_CLIENTEDGE);
		for (int i = 0; i < kGameFnCount; i++) {
			std::string t = kGameFns[i].header ? std::string("--- ") + kGameFns[i].label + " ---" : std::string("     ") + kGameFns[i].label;
			SendMessageA(g_c[C_FN_LIST], LB_ADDSTRING, 0, (LPARAM)t.c_str());
		}
		B(C_FN_RUN, "Ausfuehren", ID_FN_RUN);
		Lb(C_LOG_LBL, "Protokoll (alles aus der Konsole)", true);
		g_c[C_LOG] = Ctl("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL, 0, 0, 10, 10, -1, WS_EX_CLIENTEDGE);
		SendMessageA(g_c[C_LOG], EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);

		// Mitte: Werkzeugleiste
		B(C_NEW, "Neu", ID_NEW); B(C_OPEN, "Oeffnen ...", ID_FILE_OPEN);
		B(C_SAVE, "Speichern", ID_SAVE); B(C_SAVEAS, "Speichern unter ...", ID_SAVEAS); B(C_EUNDO, "Rueckgaengig", ID_EUNDO);
		Lb(C_NAME_LBL, "Name:"); E(C_NAME, "");
		Lb(C_SIZE_LBL, "X/Y/Z:"); E(C_SX, "16"); E(C_SY, "16"); E(C_SZ, "16");
		B(C_RESIZE, "Groesse", ID_RESIZE); B(C_CROP, "Zuschneiden", ID_CROP); B(C_ROT90, "Drehen", ID_ROT90); B(C_MIRROR, "Spiegeln", ID_MIRROR);
		B(C_SCALE, "Skalieren x", ID_SCALE); E(C_SCALE_F, "2");
		Lb(C_INFO, "keine Struktur");
		g_c[C_3D] = CreateWindowExA(WS_EX_CLIENTEDGE, "CWLab3D", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_hwnd, NULL, GetModuleHandleA(NULL), NULL);
		B(C_VROT_L, "< Drehen", ID_VROT_L); B(C_VROT_R, "Drehen >", ID_VROT_R); B(C_ZOUT, "Zoom -", ID_ZOOM_OUT); B(C_ZIN, "Zoom +", ID_ZOOM_IN); B(C_FIT, "Einpassen", ID_FIT);
		g_c[C_CUT] = Chk("Schnitt bei Ebene (nur bis z)", 0, 0, 10, 10, ID_CUT, false);
		g_c[C_LAYER] = CreateWindowExA(WS_EX_CLIENTEDGE, "CWLabLayer", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_hwnd, NULL, GetModuleHandleA(NULL), NULL);
		B(C_LUP, "Ebene ^", ID_LAYER_UP); B(C_LDN, "Ebene v", ID_LAYER_DN); Lb(C_LLBL, "Ebene z = 0");
		B(C_LCLEAR, "Ebene leeren", ID_LAYER_CLEAR); B(C_LFILL, "Ebene fuellen", ID_LAYER_FILL); B(C_LCOPY, "Ebene nach oben kopieren", ID_LAYER_COPYUP);
		Lb(C_HOVER, "");

		// rechts: Werkzeug + Farbe
		const char* tools[4] = { "Stift (1)", "Radierer (2)", "Pipette (3)", "Fuellen (4)" };
		for (int k = 0; k < 4; k++)
			g_c[C_T0 + k] = Ctl("BUTTON", tools[k], BS_AUTORADIOBUTTON | BS_PUSHLIKE | (k == 0 ? WS_GROUP : 0), 0, 0, 10, 10, ID_TOOL0 + k);
		Lb(C_RGB_LBL, "RGB:"); E(C_R, "150"); E(C_G, "150"); E(C_B, "162");
		B(C_RGB_SET, "Farbe setzen", ID_RGB_SET); B(C_CHOOSE, "Farbwahl ...", ID_COLOR);
		g_c[C_PAL] = CreateWindowExA(0, "CWLabPal", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_hwnd, NULL, GetModuleHandleA(NULL), NULL);
		const char* tabs[3] = { "Bibliothek", "Ins Spiel", "Platzierungen" };
		for (int k = 0; k < 3; k++)
			g_c[C_TAB0 + k] = Ctl("BUTTON", tabs[k], BS_AUTORADIOBUTTON | BS_PUSHLIKE | (k == 0 ? WS_GROUP : 0), 0, 0, 10, 10, ID_TAB0 + k);

		// Seite 0
		Lb(C_FILTER_LBL, "Suche:"); E(C_FILTER, "", ID_FILTER);
		g_c[C_ONLYBIG] = Chk("nur Bauten >= 10", 0, 0, 10, 10, ID_ONLYBIG, true);
		g_c[C_LIB] = Ctl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, ID_LIB, WS_EX_CLIENTEDGE);
		B(C_LIB_OPEN, "Oeffnen", ID_LIB_OPEN); B(C_LIB_RELOAD, "Neu einlesen", ID_LIB_RELOAD); B(C_FILE_OPEN2, "Datei oeffnen ...", ID_FILE_OPEN);
		B(C_LIB_EXPORT, "Alle nativen Bauten als .cub exportieren", ID_LIB_EXPORT);
		g_pageCtl[0] = { g_c[C_FILTER_LBL], g_c[C_FILTER], g_c[C_ONLYBIG], g_c[C_LIB], g_c[C_LIB_OPEN], g_c[C_LIB_RELOAD], g_c[C_FILE_OPEN2], g_c[C_LIB_EXPORT] };

		// Seite 1
		Lb(C_CORNER_LBL, "Ecke X/Y/Z:"); E(C_PX, "0"); E(C_PY, "0"); E(C_PZ, "0");
		B(C_TGT_AHEAD, "40 Bloecke +X vor mir", ID_TGT_AHEAD); B(C_TGT_HERE, "hier", ID_TGT_HERE);
		Lb(C_ROT_LBL, "Drehung:");
		g_c[C_ROT] = Ctl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, -1);
		const char* rots[] = { "0", "90", "180", "270" };
		for (int k = 0; k < 4; k++) SendMessageA(g_c[C_ROT], CB_ADDSTRING, 0, (LPARAM)rots[k]);
		SendMessageA(g_c[C_ROT], CB_SETCURSEL, 0, 0);
		Lb(C_TYP_LBL, "typ:"); E(C_TYP, "0x0"); B(C_TYP_SUG, "Vorschlag", ID_TYP_SUGGEST);
		g_c[C_FUND] = Chk("Fundament bis zum Boden", 0, 0, 10, 10, -1, true);
		g_c[C_MARKERS] = Chk("Markerfarben -> echte Objekte (Tueren, Fenster, Lampen ...)", 0, 0, 10, 10, -1, true);
		g_c[C_REMEMBER] = Chk("In dieser Welt merken (bei jedem Laden neu gesetzt)", 0, 0, 10, 10, -1, true);
		B(C_PLACE, "SETZEN", ID_PLACE); B(C_WUNDO, "Welt: Rueckgaengig", ID_WUNDO);
		B(C_SCANBOX, "Quader scannen (Ecke + X/Y/Z)", ID_SCAN_BOX); B(C_SCANME, "Um mich scannen", ID_SCAN_ME);
		Lb(C_HINT, "typ 0 = Marker ersetzt der Mod (empfohlen).\r\ntyp != 0 = native Markerlogik (experimentell).");
		g_pageCtl[1] = { g_c[C_CORNER_LBL], g_c[C_PX], g_c[C_PY], g_c[C_PZ], g_c[C_TGT_AHEAD], g_c[C_TGT_HERE], g_c[C_ROT_LBL], g_c[C_ROT],
		                 g_c[C_TYP_LBL], g_c[C_TYP], g_c[C_TYP_SUG], g_c[C_FUND], g_c[C_MARKERS], g_c[C_REMEMBER], g_c[C_PLACE],
		                 g_c[C_WUNDO], g_c[C_SCANBOX], g_c[C_SCANME], g_c[C_HINT] };

		// Seite 2
		Lb(C_PL_LBL, "Gemerkte Platzierungen dieser Welt", true);
		g_c[C_PL_LIST] = Ctl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, ID_PLACELIST, WS_EX_CLIENTEDGE);
		B(C_PL_OPEN, "Oeffnen", ID_PLACE_OPEN); B(C_PL_DEL, "Entfernen", ID_PLACE_DEL);
		g_pageCtl[2] = { g_c[C_PL_LBL], g_c[C_PL_LIST], g_c[C_PL_OPEN], g_c[C_PL_DEL] };
		// Seite 3: Gebaeude-Funktionen
		g_c[C_TAB3] = Ctl("BUTTON", "Funktionen", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 10, 10, ID_TAB3);
		Lb(C_FX_LBL, "Funktions- und Leuchtbloecke dieses Gebaeudes", true);
		g_c[C_FX_LIST] = Ctl("LISTBOX", "", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, ID_FX_LIST, WS_EX_CLIENTEDGE);
		g_c[C_FX_FN] = Ctl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, -1);
		{ int n = 0; const BuildingFx::Choice* ch = BuildingFx::Choices(&n); for (int k = 0; k < n; k++) SendMessageA(g_c[C_FX_FN], CB_ADDSTRING, 0, (LPARAM)ch[k].name); SendMessageA(g_c[C_FX_FN], CB_SETCURSEL, 0, 0); }
		B(C_FX_ADD, "Pinselfarbe -> Funktion", ID_FX_ADD); B(C_FX_GLOW, "Pinselfarbe leuchtet (an/aus)", ID_FX_GLOW);
		B(C_FX_DEL, "Eintrag entfernen", ID_FX_DEL); B(C_FX_SAVE, "Funktionen speichern", ID_FX_SAVE);
		Lb(C_FX_INFO, "");
		g_pageCtl[3] = { g_c[C_FX_LBL], g_c[C_FX_LIST], g_c[C_FX_FN], g_c[C_FX_ADD], g_c[C_FX_GLOW], g_c[C_FX_DEL], g_c[C_FX_SAVE], g_c[C_FX_INFO] };

		Lb(C_STATUS, "");

		// Reiterleiste oben (eigene Gruppe, nicht in g_c -- bleibt immer sichtbar)
		for (int k = 0; k < TAB_COUNT; k++)
			g_tabBtn[k] = Ctl("BUTTON", kTabNames[k], BS_AUTORADIOBUTTON | BS_PUSHLIKE | (k == 0 ? WS_GROUP : 0), 0, 0, 10, 10, ID_MAINTAB0 + k);
		SendMessageA(g_tabBtn[0], BM_SETCHECK, BST_CHECKED, 0);

		// Alte Namen auf die neuen Elemente zeigen lassen (der Rest des Codes nutzt sie).
		g_hLog = g_c[C_LOG]; g_h3d = g_c[C_3D]; g_hLayer = g_c[C_LAYER]; g_hPal = g_c[C_PAL];
		g_hLib = g_c[C_LIB]; g_hFilter = g_c[C_FILTER]; g_hOnlyBig = g_c[C_ONLYBIG];
		g_hStatus = g_c[C_STATUS]; g_hInfo = g_c[C_INFO]; g_hHover = g_c[C_HOVER]; g_hLayerLbl = g_c[C_LLBL];
		g_hName = g_c[C_NAME]; g_hSX = g_c[C_SX]; g_hSY = g_c[C_SY]; g_hSZ = g_c[C_SZ];
		g_hPX = g_c[C_PX]; g_hPY = g_c[C_PY]; g_hPZ = g_c[C_PZ]; g_hRot = g_c[C_ROT]; g_hTyp = g_c[C_TYP];
		g_hFund = g_c[C_FUND]; g_hMarkers = g_c[C_MARKERS]; g_hRemember = g_c[C_REMEMBER]; g_hCut = g_c[C_CUT];
		g_hPlaceList = g_c[C_PL_LIST]; g_hFnList = g_c[C_FN_LIST];
		for (int k = 0; k < 4; k++) g_hToolBtn[k] = g_c[C_T0 + k];
		g_hRGB[0] = g_c[C_R]; g_hRGB[1] = g_c[C_G]; g_hRGB[2] = g_c[C_B];

		BuildPalette();
		if (!g_customLoaded) LoadCustom();
		SetTool(0);
		ShowPage(0);
		ShowColorInEdits();
		Layout();
	}

	static void OnCommand(int id, int code) {
		if (id >= ID_GAME_FIRST && id < ID_GAME_FIRST + kGameFnCount) { RunGameFn(id - ID_GAME_FIRST); return; }
		if (id >= ID_MAINTAB0 && id < ID_MAINTAB0 + TAB_COUNT) { ShowMainTab(id - ID_MAINTAB0); return; }
		switch (id) {
			case ID_FN_LIST: if (code != LBN_DBLCLK) break;
				[[fallthrough]];
			case ID_FN_RUN: {
				int i = (int)SendMessageA(g_hFnList, LB_GETCURSEL, 0, 0);
				if (i >= 0 && i < kGameFnCount && !kGameFns[i].header) RunGameFn(i);
				else Say("Bitte eine Funktion (keine Ueberschrift) in der Liste waehlen.");
				break;
			}
			case ID_SAVEAS: SaveCurrentAs(); break;
			case ID_MOB_OPEN: ShowMainTab(TAB_MOBS); break;
			case ID_BIOME_OPEN: ShowMainTab(TAB_BIOME); break;
			case ID_ZMOD_OPEN: ShowMainTab(TAB_ZMOD); break;
			case ID_RECIPE_OPEN: ShowMainTab(TAB_RECIPE); break;
			case ID_LOGGER: ShowMainTab(TAB_LOGGER); break;
			case ID_RGB_SET: {
				auto c = [](HWND h) { int v = GetInt(h, 0); return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
				SetColor(c(g_hRGB[0]), c(g_hRGB[1]), c(g_hRGB[2]), false);
				break;
			}
			case ID_TAB0: ShowPage(0); break;
			case ID_TAB1: ShowPage(1); break;
			case ID_TAB2: ShowPage(2); RefreshPlacements(true); break;
			case ID_TAB3: ShowPage(3); FxRefresh(); break;
			case ID_FX_ADD: case ID_FX_GLOW: {
				Entry_FxAdd(id == ID_FX_GLOW);
				break;
			}
			case ID_FX_DEL: {
				int i = (int)SendMessageA(g_c[C_FX_LIST], LB_GETCURSEL, 0, 0);
				if (i >= 0 && i < (int)g_curFx.e.size()) { g_curFx.e.erase(g_curFx.e.begin() + i); g_dirty = true; FxRefresh(); }
				break;
			}
			case ID_FX_SAVE: {
				if (g_curPath.empty()) { Say("Erst die Struktur speichern (die Funktionen liegen als <name>.funk daneben)."); break; }
				std::string fe;
				if (BuildingFx::Save(g_curPath, g_curFx, fe)) Say("Funktionen gespeichert: " + BuildingFx::PathFor(g_curPath)); else Say(fe);
				break;
			}
			case ID_LIB: if (code == LBN_DBLCLK) OpenLibItem((int)SendMessageA(g_hLib, LB_GETCURSEL, 0, 0)); break;
			case ID_PERF_OPEN: ShowMainTab(TAB_PERF); break;
			case ID_LIB_OPEN: OpenLibItem((int)SendMessageA(g_hLib, LB_GETCURSEL, 0, 0)); break;
			case ID_FILTER: if (code == EN_CHANGE) RebuildLibList(); break;
			case ID_ONLYBIG: RebuildLibList(); break;
			case ID_LIB_RELOAD: ReloadLibrary(); RequestNative(); break;
			case ID_FILE_OPEN: OpenFileDialog(); break;
			case ID_LIB_EXPORT:
				Say("Exportiere alle nativen Bauten (>= 10 Bloecke) nach " + Lab::ExportDir() + " ...");
				LabBridge::Post("Export", [](cube::GameController* gc) { LabBridge::ExportAllNative(gc, 10); });
				break;

			case ID_NEW: {
				if (!ConfirmDiscard()) break;
				Lab::Structure s; s.Create(GetInt(g_hSX, 16), GetInt(g_hSY, 16), GetInt(g_hSZ, 16));
				s.name = "neu"; s.origin = "neu"; s.fundament = 1;
				SetCurrent(s, true);
				Say("Neue leere Struktur. Namen oben eintragen und mit 'Speichern' (Strg+S) sichern.");
				SetFocus(g_hName); SendMessageA(g_hName, EM_SETSEL, 0, -1);
				break;
			}
			case ID_RESIZE: if (g_cur.Valid()) { PushUndo(); g_cur.Resize(GetInt(g_hSX, g_cur.sx), GetInt(g_hSY, g_cur.sy), GetInt(g_hSZ, g_cur.sz)); AfterChange(); } break;
			case ID_CROP:   if (g_cur.Valid()) { PushUndo(); g_cur.CropToContent(); SetInt(g_hSX, g_cur.sx); SetInt(g_hSY, g_cur.sy); SetInt(g_hSZ, g_cur.sz); AfterChange(); } break;
			case ID_ROT90:  if (g_cur.Valid()) { PushUndo(); g_cur.RotateZ90(); SetInt(g_hSX, g_cur.sx); SetInt(g_hSY, g_cur.sy); AfterChange(false); } break;
			case ID_MIRROR: if (g_cur.Valid()) { PushUndo(); g_cur.MirrorX(); AfterChange(false); } break;
			case ID_SCALE: {
				// Ein Faktor fuer alle Achsen ("2", "0.5") oder drei ("2 2 1" = nur Grundflaeche).
				if (!g_cur.Valid()) break;
				float f[3] = { 0, 0, 0 };
				int n = sscanf_s(GetText(g_c[C_SCALE_F]).c_str(), "%f %f %f", &f[0], &f[1], &f[2]);
				if (n == 1) f[1] = f[2] = f[0];
				if (n != 1 && n != 3) { Say("Skalieren: einen Faktor (z.B. 2 oder 0.5) oder drei (X Y Z, z.B. 2 2 1) eintragen."); break; }
				Lab::Structure t = g_cur;
				if (!t.Scale(f[0], f[1], f[2])) { Say("Skalieren nicht moeglich (Ergebnis groesser als 256 x 256 x 512 oder Faktor <= 0)."); break; }
				PushUndo(); g_cur = t;
				SetInt(g_hSX, g_cur.sx); SetInt(g_hSY, g_cur.sy); SetInt(g_hSZ, g_cur.sz);
				AfterChange(false);
				char b[120]; sprintf_s(b, "Skaliert x%.2f / %.2f / %.2f -> %d x %d x %d (naechster Nachbar, Strg+Z macht es rueckgaengig).", f[0], f[1], f[2], g_cur.sx, g_cur.sy, g_cur.sz);
				Say(b);
				break;
			}
			case ID_EUNDO:  EditorUndo(); break;
			case ID_SAVE:   SaveCurrent(); break;

			case ID_TOOL0: SetTool(0); break;
			case ID_TOOL1: SetTool(1); break;
			case ID_TOOL2: SetTool(2); break;
			case ID_TOOL3: SetTool(3); break;
			case ID_COLOR: ChooseCustomColor(); break;

			case ID_LAYER_UP: ChangeLayer(1); break;
			case ID_LAYER_DN: ChangeLayer(-1); break;
			case ID_LAYER_CLEAR:
				if (g_cur.Valid()) { PushUndo(); for (int y = 0; y < g_cur.sy; y++) for (int x = 0; x < g_cur.sx; x++) g_cur.Clear(x, y, g_layer); AfterChange(); }
				break;
			case ID_LAYER_FILL:
				if (g_cur.Valid()) { PushUndo(); for (int y = 0; y < g_cur.sy; y++) for (int x = 0; x < g_cur.sx; x++) g_cur.Set(x, y, g_layer, g_col[0], g_col[1], g_col[2]); AfterChange(); }
				break;
			case ID_LAYER_COPYUP:
				if (g_cur.Valid() && g_layer + 1 < g_cur.sz) {
					PushUndo();
					for (int y = 0; y < g_cur.sy; y++) for (int x = 0; x < g_cur.sx; x++) {
						uint8_t r, g, b;
						if (g_cur.Get(x, y, g_layer, r, g, b)) g_cur.Set(x, y, g_layer + 1, r, g, b); else g_cur.Clear(x, y, g_layer + 1);
					}
					g_layer++; AfterChange();
				}
				break;

			case ID_VROT_L: g_viewRot = (g_viewRot + 3) & 3; Redraw3D(); break;
			case ID_VROT_R: g_viewRot = (g_viewRot + 1) & 3; Redraw3D(); break;
			case ID_ZOOM_IN: g_zoom = (std::min)(g_zoom * 1.25f, 12.0f); Redraw3D(); break;
			case ID_ZOOM_OUT: g_zoom = (std::max)(g_zoom / 1.25f, 0.2f); Redraw3D(); break;
			case ID_FIT: g_zoom = 1.0f; Redraw3D(); break;
			case ID_CUT: Redraw3D(); break;

			case ID_TGT_AHEAD: PlaceTarget(true); break;
			case ID_TGT_HERE: PlaceTarget(false); break;
			case ID_TYP_SUGGEST: {
				// Aus der Herkunft "nativ 0x903" den Vorschlag ableiten.
				int idx = -1;
				if (g_cur.origin.compare(0, 6, "nativ ") == 0) idx = (int)strtol(g_cur.origin.c_str() + 6, nullptr, 0);
				int t = idx >= 0 ? LabBridge::SuggestTyp(idx) : 0;
				SetHex(g_hTyp, t);
				Say(t ? "typ-Vorschlag aus Chunk_generateObjects: 0x" + std::to_string(t) : "Kein typ-Vorschlag (nur fuer Klassen-/Handwerksgebaeude bekannt) -> 0.");
				break;
			}
			case ID_PLACE: DoPlace(); break;
			case ID_WUNDO:
				LabBridge::Post("Rueckgaengig", [](cube::GameController* gc) { std::string r; LabBridge::UndoLastPlacement(gc, r); });
				break;
			case ID_SCAN_BOX: DoScanBox(false); break;
			case ID_SCAN_ME: DoScanBox(true); break;

			case ID_PLACE_DEL: {
				int i = (int)SendMessageA(g_hPlaceList, LB_GETCURSEL, 0, 0);
				if (i >= 0 && LabBridge::RemovePlacement(i)) { Say("Platzierung entfernt (verschwindet beim naechsten Laden der Zone)."); RefreshPlacements(true); }
				break;
			}
			case ID_PLACELIST:
				if (code != LBN_DBLCLK) break;
				[[fallthrough]];                                 // Doppelklick = Oeffnen
			case ID_PLACE_OPEN: {
				int i = (int)SendMessageA(g_hPlaceList, LB_GETCURSEL, 0, 0);
				std::vector<LabBridge::Placement> v = LabBridge::GetPlacements();
				if (i < 0 || i >= (int)v.size()) break;
				if (!ConfirmDiscard()) break;
				std::string path = Lab::StructuresDir() + "\\" + v[i].name + ".cub", err;
				Lab::Structure s;
				if (!Lab::LoadCub(path, s, err)) { Say(err); break; }
				Lab::LoadMeta(path, s);
				SetCurrent(s, true);
				g_curPath = path; BuildingFx::Load(g_curPath, g_curFx);
				SetInt(g_hPX, v[i].x); SetInt(g_hPY, v[i].y); SetInt(g_hPZ, v[i].z);
				SendMessageA(g_hRot, CB_SETCURSEL, v[i].rot & 3, 0);
				break;
			}
		}
	}

	static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: g_hwnd = h; BuildLayout(); SetTimer(h, ID_TIMER, 100, NULL); UpdateTitle(); return 0;
			case WM_SIZE: if (wp != SIZE_MINIMIZED) Layout(); return 0;
			case WM_GETMINMAXINFO: {
				MINMAXINFO* mm = (MINMAXINFO*)lp;
				mm->ptMinTrackSize.x = 1260; mm->ptMinTrackSize.y = 730;   // darunter reicht die Werkzeugleiste nicht
				return 0;
			}
			case WM_COMMAND: OnCommand(LOWORD(wp), HIWORD(wp)); return 0;
			case WM_TIMER: OnTimer(); return 0;
			case WM_CLOSE:
				// Nur ausblenden -- der Zustand bleibt. Ungespeichertes bleibt ebenfalls
				// erhalten (das Fenster wird nicht zerstoert), aber wir erinnern daran.
				if (g_dirty) Say("Hinweis: ungespeicherte Aenderungen an '" + g_cur.name + "' (Strg+S).");
				ShowWindow(h, SW_HIDE); g_visible = false; return 0;
			case WM_APP + 1:
				g_visible = !g_visible;
				ShowWindow(h, g_visible ? SW_SHOW : SW_HIDE);
				if (g_visible) SetForegroundWindow(h);
				return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	// Tasten im Fenster (ausser in Eingabefeldern): Strg+Z, Strg+S, Strg+Umschalt+S, 1-4, Bild auf/ab.
	static bool HandleKey(const MSG& msg) {
		if (msg.message != WM_KEYDOWN) return false;
		if (g_mainTab != TAB_BUILD) return false;            // Editor-Tasten nur im Reiter "Bauen"
		bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		// Speichern geht auch aus Eingabefeldern heraus.
		if (ctrl && msg.wParam == 'S') { if (shift) SaveCurrentAs(); else SaveCurrent(); return true; }
		char cls[16] = ""; GetClassNameA(GetFocus(), cls, sizeof(cls));
		if (_stricmp(cls, "Edit") == 0) {
			// Enter in einem RGB-Feld = Farbe setzen
			if (msg.wParam == VK_RETURN && (GetFocus() == g_hRGB[0] || GetFocus() == g_hRGB[1] || GetFocus() == g_hRGB[2])) {
				OnCommand(ID_RGB_SET, 0); return true;
			}
			return false;
		}
		switch (msg.wParam) {
			case 'Z': if (ctrl) { EditorUndo(); return true; } break;
			case '1': SetTool(0); return true;
			case '2': SetTool(1); return true;
			case '3': SetTool(2); return true;
			case '4': SetTool(3); return true;
			case VK_PRIOR: ChangeLayer(1); return true;
			case VK_NEXT: ChangeLayer(-1); return true;
		}
		return false;
	}

	// =====================================================================
	// 9. Thread
	// =====================================================================
	static DWORD WINAPI Thread(LPVOID) {
		HINSTANCE hi = GetModuleHandleA(NULL);
		g_font = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
		g_fontBold = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

		WNDCLASSEXA wc; memset(&wc, 0, sizeof(wc)); wc.cbSize = sizeof(wc);
		wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpfnWndProc = MainProc; wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.lpszClassName = "CWLabMain";
		wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		RegisterClassExA(&wc);
		wc.hbrBackground = NULL; wc.hIcon = NULL;
		wc.lpfnWndProc = Proc3D;    wc.lpszClassName = "CWLab3D";    RegisterClassExA(&wc);
		wc.lpfnWndProc = ProcLayer; wc.lpszClassName = "CWLabLayer"; wc.hCursor = LoadCursor(NULL, IDC_CROSS); RegisterClassExA(&wc);
		wc.lpfnWndProc = PerfProc; wc.lpszClassName = "CWLabPerf"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); RegisterClassExA(&wc);
		wc.lpfnWndProc = MobProc; wc.lpszClassName = "CWLabMob"; RegisterClassExA(&wc);
		{ INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&ic); }   // Abhaklisten (Biom)
		wc.lpfnWndProc = BiomeProc; wc.lpszClassName = "CWLabBiome"; RegisterClassExA(&wc);
		wc.lpfnWndProc = ZoneModelProc; wc.lpszClassName = "CWLabZMod"; RegisterClassExA(&wc);
		wc.lpfnWndProc = RecipeProc; wc.lpszClassName = "CWLabRecipe"; RegisterClassExA(&wc);
		wc.lpfnWndProc = ItemProc;   wc.lpszClassName = "CWLabItems";  RegisterClassExA(&wc);
		wc.lpfnWndProc = SkillProc;  wc.lpszClassName = "CWLabSkills"; RegisterClassExA(&wc);
		wc.lpfnWndProc = VendorProc; wc.lpszClassName = "CWLabVendors"; RegisterClassExA(&wc);
		wc.lpfnWndProc = NetProc;    wc.lpszClassName = "CWLabNet";     RegisterClassExA(&wc);
		wc.lpfnWndProc = LogPageProc; wc.lpszClassName = "CWLabLogPage"; RegisterClassExA(&wc);
		wc.lpfnWndProc = DungeonProc; wc.lpszClassName = "CWLabDungeon"; RegisterClassExA(&wc);
		TierWerkstatt::SetFonts(g_font, g_fontBold); TierWerkstatt::RegisterClasses(GetModuleHandleA(NULL));
		wc.lpfnWndProc = HostProc; wc.lpszClassName = "CWLabHost"; RegisterClassExA(&wc); wc.hbrBackground = NULL;
		wc.style = CS_DBLCLKS;
		wc.lpfnWndProc = ProcPal;   wc.lpszClassName = "CWLabPal";   wc.hCursor = LoadCursor(NULL, IDC_HAND); RegisterClassExA(&wc);

		// Startgroesse: Wunsch 1520 x 980, aber nie groesser als der Arbeitsbereich
		// (Bildschirm ohne Taskleiste). Mit Windows-Skalierung meldet Windows hier
		// bereits die skalierte Groesse -- das Fenster passt also auch bei 125/150 %.
		RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
		int waW = wa.right - wa.left, waH = wa.bottom - wa.top;
		RECT r = { 0, 0, 1520, 1010 };
		AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
		int ww = (std::min)((int)(r.right - r.left), waW), wh = (std::min)((int)(r.bottom - r.top), waH);
		HWND h = CreateWindowExA(0, "CWLabMain", "Cube World LAB",
			WS_OVERLAPPEDWINDOW, wa.left, wa.top, ww, wh, NULL, NULL, hi, NULL);
		if (!h) return 0;
		g_visible = false;                  // Veroeffentlichung: LAB startet unsichtbar, [F10] blendet es ein
		Say("LAB gestartet. Alle Weltaktionen laufen als Auftrag auf dem Spielthread.");
		ReloadLibrary();

		MSG msg;
		while (GetMessageA(&msg, NULL, 0, 0) > 0) {
			if (HandleKey(msg)) continue;
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
		return 0;
	}

	void Start() {
		if (g_thread) return;
		g_thread = CreateThread(NULL, 0, Thread, NULL, 0, NULL);
	}

	void Toggle() { if (g_hwnd) PostMessageA(g_hwnd, WM_APP_TOGGLE, 0, 0); }
	bool IsVisible() { return g_visible; }

	// Gehoert das Vordergrundfenster zum LAB (Hauptfenster oder ein Dialog davon)?
	// main.cpp liest die Spiel-Hotkeys per GetAsyncKeyState -- das ist GLOBAL.
	// Ohne diese Sperre loeste z.B. Strg+X beim Ausschneiden im Suchfeld des LAB
	// "Extra-Cubes verwerfen" aus und ein "l" im Namensfeld das [L]-Overlay.
	bool IsForeground() {
		HWND f = GetForegroundWindow();
		if (!f || !g_hwnd) return false;
		if (f == g_hwnd) return true;
		HWND owner = GetWindow(f, GW_OWNER);              // Farb-/Dateidialog
		return owner == g_hwnd;
	}
}
