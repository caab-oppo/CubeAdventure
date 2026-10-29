#include <windows.h>
#include <cstdio>
#include <iostream>
#include <string>

#include "cwsdk.h"
#include <d3d9.h>
#include "MinHook.h"
#include "AdventureModFramework.h"
#include "WanderingMerchant.h"
#include "PirateShip.h"
#include "PirateAirship.h"
#include "AirshipSpawner.h"
#include "Wegfinder.h"
#include "MountainVillage.h"
#include "AirshipTaxi.h"
#include "BlacksmithNpc.h"
#include "GemForge.h"
#include "NativeHouseProbe.h"
#include "LabWindow.h"
#include "LabBridge.h"
#include "PerfTweaks.h"
#include "BiomeMod.h"
#include "Recipes.h"
#include "Items.h"
#include "TierGelenke.h"
#include "NativeUi.h"
#include "Skills.h"
#include "Temples.h"
#include "Dungeons.h"
#include "Tracking.h"
#include "Boat.h"
#include "MapMarkers.h"
#include "ZoneSplit.h"
#include "MobLab.h"
#include "Effects.h"
#include "City.h"
#include "CrashLogger.h"
#include "ModSwitches.h"
#include "Host.h"
#include "Wildlife.h"
#include <fstream>

// ==========================================================================
// AdventureMod -- Einstiegspunkt (aufgeraeumte Fassung, 2026-07-12)
// --------------------------------------------------------------------------
// D3D9-EndScene-Hook fuers Overlay + Tasten, Framework-Dispatch pro Frame.
// Tasten:
//   [R]  natives Talk-/Shop-Menu (Spiel-eigen), am Haendler anvisiert
//   [F9] Tracking-Anzeige (Spieler + Haendler) ein/aus
//   [L]  Labor-/Test-Menue (u.a. Haendler spawnen)
// Der Vendor-Katalog wird beim Spawn automatisch gebaut (frueher [U]).
// ==========================================================================

typedef HRESULT(APIENTRY* EndScene_t)(LPDIRECT3DDEVICE9 pDevice);
typedef HRESULT(APIENTRY* Reset_t)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);

EndScene_t Original_EndScene = nullptr;
Reset_t Original_Reset = nullptr;

static HMODULE g_hModule = nullptr;   // diese DLL (fuer den externen Logger)
// Veroeffentlichungsfassung: keine Konsole, keine Bildschirmtexte ausser der F9-Koordinatenanzeige,
// kein [L]-Testmenue. Fuer die Entwicklung auf true stellen.
static const bool kDevBuild = false;
static bool g_cheats_enabled = false;
static bool g_lab_open = false;

// Vom LAB-Fenster aus aufrufbar (Knopf "Tracking-Anzeige [F9]").
void AdventureMain_ToggleTracking() { g_cheats_enabled = !g_cheats_enabled; }

// ALLE Spiel-Hotkeys laufen hierueber. GetAsyncKeyState ist GLOBAL -- es meldet
// Tasten auch dann, wenn ein anderes Fenster den Fokus hat. Solange das LAB-
// Fenster vorne ist, gehoeren die Tasten ihm: sonst loeste z.B. Strg+X beim
// Ausschneiden im LAB-Suchfeld "Extra-Cubes verwerfen" aus.
static bool KeyAny(int vk) {                       // F9/F10: auch in Menues (nur nicht im LAB / beim Tippen)
	if (LabWindow::IsForeground()) return false;
	if (NativeUi::Typing()) return false;            // Runenstein-Name wird getippt (Eingabefenster im Spielstil)
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
// 2026-10-29: Spieltasten nur im laufenden Spiel -- nicht in Menue-Bildschirmen (Welt erstellen ...), nicht bei offener
// Chatzeile, nicht solange die Spielerzone noch erzeugt wird (NativeUi::PlayInput, je Bild in Hooked_EndScene).
static bool Key(int vk) {
	if (!NativeUi::PlayInputCached()) return false;
	return KeyAny(vk);
}

// ---------------------------------------------------------------------
// SEH-Isolation (MSVC C2712: __try darf nicht im selben Frame wie Objekte
// mit Destruktor stehen -- Hooked_EndScene hat ein std::string-Local).
// ---------------------------------------------------------------------
// LAB-Auftraege (Fenster-Thread -> Spielthread). Eigene SEH-Huelle wie oben.
static void SafeLabDrain(cube::GameController* gc) {
	__try {
		LabBridge::Drain(gc);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::cout << "[LAB] FEHLER: Drain abgestuerzt (SEH abgefangen)." << std::endl;
	}
}

// Leistungsbericht fuer den Logger (gemeinsamer Speicher), etwa einmal je Sekunde.
static void PublishPerfText() {
	static DWORD last = 0; DWORD now = GetTickCount();
	if (now - last < 1000) return;
	last = now;
	std::string t = PerfTweaks::Report();
	BiomeMod::Status s = BiomeMod::GetStatus();
	char b[400];
	sprintf_s(b, "Biome: %s, %d aktiv, hier %s | Zonen geschmueckt %ld (wartend %ld), Pflanzen %ld, Dungeons %ld, Minen %ld | eigene Mobs verfolgt %d\r\n",
	          BiomeMod::IsEnabled() ? "AN" : "AUS", s.biomes, s.hereBiome ? s.hereName.c_str() : "-", s.zonesDecorated, s.queue,
	          s.plantsPlaced, s.dungeonsPlaced, s.minesCarved, s.mobsTracked);
	t += b;
	CrashLogger::PerfPublishText(t.c_str());
}
static void SafePublishPerf() {
	__try { PublishPerfText(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SafeRecipesTick(cube::GameController* gc) {
	__try { Recipes::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Effects::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Items::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { City::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Skills::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Temples::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Dungeons::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Boat::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { ZoneSplit::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MobLab::FlightTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MobLab::SwimTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MobLab::SolidTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MobLab::ModCreatureTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MobLab::PetSkinTick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { Wildlife::Tick(gc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// Skill-Leiste (unten rechts). Eigene SEH-Huelle: Hooked_EndScene hat Objekte mit Destruktor.
static void TrackingInto(cube::GameController* gc, std::string* out) { *out = Tracking::Text(gc); }
static bool TrackingTextSeh(cube::GameController* gc, std::string* out) {
	__try { TrackingInto(gc, out); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }   // C2712: kein Objekt hier
}
static std::string SafeTrackingText(cube::GameController* gc) { std::string s; if (!TrackingTextSeh(gc, &s)) s.clear(); return s; }
// 2026-10-29: ohne D3DX -- Schrift = Textknoten des Spiels (NativeUi::Hud*), keine Skill-Leiste/Effekt-Kaestchen mehr.
static void SafeSkillsHud(LPDIRECT3DDEVICE9 dev, int w, int h) {
	cube::GameController* gc = cube::GetGameController();
	__try { Skills::DrawHud(gc, w, h); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { MapMarkers::Draw(gc, dev, w, h); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void SafeHud(cube::GameController* gc, int step) {
	__try { if (step == 0) NativeUi::HudBegin(gc); else if (step == 1) NativeUi::HudEnd(gc); else NativeUi::HudHideAll(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SafePerfTick(cube::GameController* gc) {
	SafeRecipesTick(gc);
	CrashLogger::PerfBeat(CrashLogger::BEAT_GAME);         // Lebenszeichen des Spielthreads fuer den Logger
	SafePublishPerf();
	__try {
		PerfTweaks::Tick(gc);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::cout << "[Leistung] FEHLER: Tick abgestuerzt (SEH abgefangen)." << std::endl;
	}
}

static void SafeDispatchGameTick(cube::GameController* gc) {
	__try {
		AdventureFramework::DispatchGameTick(gc);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::cout << "[AdventureMod] FEHLER: DispatchGameTick() abgestuerzt (SEH abgefangen) in Baustein '"
			<< AdventureFramework::CurrentModName() << "' -- Spiel laeuft weiter." << std::endl;
	}
}

HRESULT APIENTRY Hooked_Reset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
	HRESULT hr = Original_Reset(pDevice, pPresentationParameters);
	return hr;
}

// --- UNSERE HAUPTFALLE (Hooked EndScene) ---
HRESULT APIENTRY Hooked_EndScene(LPDIRECT3DDEVICE9 pDevice) {

	auto gc = cube::GetGameController();
	if (gc && gc->local_player) {
		NativeUi::PlayInput(gc);                                  // Spieltasten erlaubt? (Menues, Chat, Welterzeugung)

		D3DVIEWPORT9 vp; pDevice->GetViewport(&vp);
		SafeHud(gc, 0);                                           // Schrift des Spiels: Zeilen dieses Bildes beginnen

		// --- ADVENTURE-MOD-FRAMEWORK (OnInitialize einmalig + OnGameTick) ---
		SafeDispatchGameTick(gc);

		// --- LAB: Auftraege aus dem zweiten Fenster auf DIESEM Thread ausfuehren ---
		LabBridge::SetOverlayFlags(g_cheats_enabled, false);
		SafeLabDrain(gc);

		// --- Karte & Leistung: adaptive Pausen, Patches nachfuehren, Messung ---
		SafePerfTick(gc);

		// --- [F10]: LAB-Fenster ein/aus ---
		static bool f10_pressed = false;
		if (KeyAny(VK_F10)) {
			if (!f10_pressed) { f10_pressed = true; LabWindow::Toggle(); }
		}
		else { f10_pressed = false; }

		// --- [F9]: Tracking-Anzeige ein/aus ---
		static bool f9_pressed = false;
		if (KeyAny(VK_F9)) {
			if (!f9_pressed) {
				f9_pressed = true;
				g_cheats_enabled = !g_cheats_enabled;
				// std::cout << "[Tracking] Anzeige " << (g_cheats_enabled ? "AN" : "AUS") << std::endl;
			}
		}
		else { f9_pressed = false; }

		// --- [F9]: unten rechts, rechtsbuendig (Veroeffentlichung): Koordinaten, Biom, angehakte Strukturen (Tracking.cpp) ---
		if (!kDevBuild && g_cheats_enabled) {
			std::string t = SafeTrackingText(gc);
			if (!t.empty()) {
				const int lines = NativeUi::HudLines(t);
				NativeUi::HudText(gc, t, (float)vp.Width - 129.0f, (float)vp.Height - 12.0f - 18.0f * (float)lines, 2);   // ~3 cm weiter links (115 px)
			}
		}
		SafeSkillsHud(pDevice, (int)vp.Width, (int)vp.Height);
		if (kDevBuild && g_cheats_enabled) {
			AdventureMod::TrackResult player = AdventureMod::TrackPlayer(gc);
			AdventureMod::TrackResult merchant = AdventureMod::TrackMerchant(gc);

			char linebuf[256];
			std::string overlay = "--- Tracking (F9 zum Ausschalten) ---\n";

			if (player.found) {
				sprintf_s(linebuf, "Spieler   Block (%lld, %lld, %lld)   Zone (%d, %d)\n",
					(long long)player.pos.blockX, (long long)player.pos.blockY, (long long)player.pos.blockZ,
					player.pos.zoneX, player.pos.zoneY);
				overlay += linebuf;
			}

			if (merchant.found) {
				sprintf_s(linebuf, "Haendler  Block (%lld, %lld, %lld)   Zone (%d, %d)   Entfernung %.1f Bloecke\n",
					(long long)merchant.pos.blockX, (long long)merchant.pos.blockY, (long long)merchant.pos.blockZ,
					merchant.pos.zoneX, merchant.pos.zoneY, merchant.distanceBlocks);
				overlay += linebuf;
			}
			else {
				overlay += "Haendler  kein aktiver Haendler\n";
			}

			// Piraten-Kapitaen (Enterkommando), falls einer gespawnt wurde.
			uint64_t capGuid = PirateShip::GetCaptainGuid();
			if (capGuid != 0) {
				AdventureMod::TrackResult cap = AdventureMod::TrackEntity(gc, capGuid);
				if (cap.found) {
					sprintf_s(linebuf, "Kapitaen  Block (%lld, %lld, %lld)   Zone (%d, %d)   Entfernung %.1f Bloecke\n",
						(long long)cap.pos.blockX, (long long)cap.pos.blockY, (long long)cap.pos.blockZ,
						cap.pos.zoneX, cap.pos.zoneY, cap.distanceBlocks);
					overlay += linebuf;
				}
			}

			// Bergdoerfer (auffindbar machen)
			MountainVillage::AppendTracking(gc, overlay);
			overlay += BiomeMod::TrackingText(gc);             // eigene Biome (Seed-Rechner)

			NativeUi::HudText(gc, overlay, 10.0f, 20.0f, 0);
		}

		// --- [L]: Labor-/Test-Menue ein/aus ---
		static bool l_pressed = false;
		if (kDevBuild && Key(0x4C)) { // 'L' (Veroeffentlichung: aus)
			if (!l_pressed) {
				l_pressed = true;
				g_lab_open = !g_lab_open;
			}
		}
		else { l_pressed = false; }

		if (kDevBuild && g_lab_open) {
			char labbuf[512]; // gross genug fuer alle Menuezeilen (sprintf_s bricht sonst hart ab)
			sprintf_s(labbuf,
				"=== LABOR-MENUE (L schliesst) ===\n"
				"[STRG+1] Haendler spawnen\n"
				"[STRG+2] Test-Zwerg bei mir spawnen (Flag-Test)\n"
				"[STRG+3] Inventar-Dump (Vendor + Spieler, Kauf-Diagnose)\n"
				"[STRG+9] Persistenz-Marker setzen (Test der Welt-DB)\n"
				"[STRG+0] Bergflughafen bauen (eigenes Dorf, an deiner Position)\n"
				"[STRG+P] Native-Deko-Prop-Test\n"
				"[F10]    LAB-Fenster ein/aus (alles hier + Struktur-Editor)");
			NativeUi::HudText(gc, labbuf, 10.0f, 390.0f, 0);

			// [1] Haendler spawnen
			static bool lab1_pressed = false;
			if (Key(VK_CONTROL) && Key('1')) {
				if (!lab1_pressed) {
					lab1_pressed = true;
					AdventureMod::ForceSpawn(gc);
				}
			}
			else { lab1_pressed = false; }

			// [2] Test-Zwerg an der Spielerposition spawnen (Flag-Test in MakeVillager)
				static bool lab2_pressed = false;
				if (Key(VK_CONTROL) && Key('2')) {
					if (!lab2_pressed) { lab2_pressed = true; MountainVillage::SpawnTestDwarfAtPlayer(gc); }
				}
				else { lab2_pressed = false; }

				// [3] Inventar-Dump (Kauf-Diagnose fuer das Luftschiff-Taxi)
				static bool lab3_pressed = false;
				if (Key(VK_CONTROL) && Key('3')) {
					if (!lab3_pressed) { lab3_pressed = true; AirshipTaxi::DumpInventories(gc); }
				}
				else { lab3_pressed = false; }

				// [6] frueher Ozean-Weltgen (OceanContent) -- entfernt 2026-10-16, Tiere kommen ueber die Tierwelt

				// [7] Machbarkeitsprobe: laesst sich ein natives Hausteil setzen?
				// Reine Vorpruefung, ruft noch nichts im Spiel auf.
				static bool lab7_pressed = false;
				if (Key(VK_CONTROL) && Key('7')) {
					if (!lab7_pressed) { lab7_pressed = true; NativeHouseProbe::Probe(gc); }
				}
				else { lab7_pressed = false; }

				// [8] Modell-Browser: naechste zehn Modelle als Reihe vor den Spieler.
				static bool lab8_pressed = false;
				if (Key(VK_CONTROL) && Key('8')) {
					if (!lab8_pressed) { lab8_pressed = true; NativeHouseProbe::BrowseModels(gc); }
				}
				else { lab8_pressed = false; }

				// [J] SCANNER: Bauten im Umkreis erfassen -> HausScan.txt.
				// Radius 40 erfasst genau ein Haus; mehrfach druecken haengt an.
				static bool labJ_pressed = false;
				if (Key(VK_CONTROL) && Key('J')) {
					if (!labJ_pressed) { labJ_pressed = true; NativeHouseProbe::ScanArea(gc, 40); }
				}
				else { labJ_pressed = false; }

				// [Y] placeStructure-Argumente durchschalten + Testhaus setzen.
				static bool labY_pressed = false;
				if (Key(VK_CONTROL) && Key('Y')) {
					if (!labY_pressed) { labY_pressed = true; NativeHouseProbe::CycleArgs(gc); }
				}
				else { labY_pressed = false; }

				// [L] Registry als Textdatei ausgeben (ModelRegistry.txt).
				static bool labL_pressed = false;
				if (Key(VK_CONTROL) && Key('L')) {
					if (!labL_pressed) { labL_pressed = true; NativeHouseProbe::DumpRegistry(gc); }
				}
				else { labL_pressed = false; }

				// [O] Modell-Browser auf einen bestimmten Index zuruecksetzen.
				static bool labO_pressed = false;
				if (Key(VK_CONTROL) && Key('O')) {
					if (!labO_pressed) { labO_pressed = true; NativeHouseProbe::ResetBrowse(0x839); }
				}
				else { labO_pressed = false; }

				// [T] Objekt-Tester: naechster Objekttyp, 40 Bloecke in +X.
				static bool labT_pressed = false;
				if (Key(VK_CONTROL) && Key('T')) {
					if (!labT_pressed) { labT_pressed = true; MountainVillage::ObjectTester(gc); }
				}
				else { labT_pressed = false; }

				// [H] BLOCKSCAN: Hausschale als Voxel erfassen -> HausBloecke.txt.
				// Radius 22, z -4..+28 deckt ein zweigeschossiges Haus ab.
				static bool labH_pressed = false;
				if (Key(VK_CONTROL) && Key('H')) {
					if (!labH_pressed) { labH_pressed = true; MountainVillage::ScanBlocksAtPlayer(gc, 22, -4, 28); }
				}
				else { labH_pressed = false; }

				// [G] Blockscan-Datei leeren.
				static bool labG_pressed = false;
				if (Key(VK_CONTROL) && Key('G')) {
					if (!labG_pressed) { labG_pressed = true; MountainVillage::ResetBlockScan(); }
				}
				else { labG_pressed = false; }

				// [K] Scan-Datei leeren (neu anfangen).
				static bool labK_pressed = false;
				if (Key(VK_CONTROL) && Key('K')) {
					if (!labK_pressed) { labK_pressed = true; NativeHouseProbe::ResetScan(); }
				}
				else { labK_pressed = false; }

			// [9] Persistenz-Marker-Test: setzt EINEN Block persistent (0x14-Record + zone dirty). BACKUP!
			static bool lab9_pressed = false;
			if (Key(VK_CONTROL) && Key('9')) {
				if (!lab9_pressed) { lab9_pressed = true; MountainVillage::PersistMarkerAtPlayer(gc); }
			}
			else { lab9_pressed = false; }

			// [0] Bergflughafen / eigenes Dorf an der Spielerposition bauen
			static bool lab0_pressed = false;
			if (Key(VK_CONTROL) && Key('0')) {
				if (!lab0_pressed) { lab0_pressed = true; MountainVillage::BuildAtPlayer(gc); }
			}
			else { lab0_pressed = false; }

			// [P] Weg-1-Test: native Deko-Props an die Zone anhaengen -> rendern sie?
			static bool labP_pressed = false;
			if (Key(VK_CONTROL) && Key('P')) {
				if (!labP_pressed) { labP_pressed = true; MountainVillage::PropTestAtPlayer(gc); }
			}
			else { labP_pressed = false; }

			// [R] Schmied: nahe dem Schmied -> Waffen-Customization-UI oeffnen (native Interakt-Taste).
			static bool labR_pressed = false;
			if (Key('R')) {
				if (!labR_pressed) { labR_pressed = true; /* Schmied entfernt -- [R] ist wieder das native Menue */ }
			}
			else { labR_pressed = false; }

			// GemForge: Cubes werden an der NATIVEN Customization Bench gesetzt
			// (Schmied -> [R] -> Cube im Vorrat waehlen -> auf die Waffe klicken).
			// Hier nur zwei Hilfstasten.
			{
				bool ctrl = Key(VK_CONTROL) != 0;
				// STRG+B: angelegte Waffe (erneut) an die offene Bench binden.
				static bool gfB_pressed = false;
				if (ctrl && Key('B')) {
					if (!gfB_pressed) { gfB_pressed = true; GemForge::BindEquippedWeaponToBench(gc); }
				}
				else gfB_pressed = false;
				// STRG+X: Extra-Cubes (>nativ) der angelegten Waffe verwerfen.
				static bool gfX_pressed = false;
				if (ctrl && Key('X')) {
					if (!gfX_pressed) { gfX_pressed = true; GemForge::ClearExtras(gc); }
				}
				else gfX_pressed = false;
				// STRG+I: Bench-Zustand auslesen (Vorrat, Auswahl, Trefferpunkt, Material-IDs).
				static bool gfI_pressed = false;
				if (ctrl && Key('I')) {
					if (!gfI_pressed) { gfI_pressed = true; GemForge::DumpBenchState(gc); }
				}
				else gfI_pressed = false;
				// STRG+U: kompletter Inventar-Scan (Kategorien, Material-IDs, Level).
				static bool gfU_pressed = false;
				if (ctrl && Key('U')) {
					if (!gfU_pressed) { gfU_pressed = true; GemForge::DumpInventory(gc); }
				}
				else gfU_pressed = false;
// STRG+4: Edelstein-Zutaten in setzbare Cubes umwandeln.
				static bool gf4_pressed = false;
				if (ctrl && Key('4')) {
					if (!gf4_pressed) { gf4_pressed = true; GemForge::ToggleGemSupply(); }
				}
				else gf4_pressed = false;
			}
		}

		// --- Luftschiff-Taxi-Sprechblase + GemForge-Zaehler -- nur Entwicklungsfassung ---
		if (kDevBuild) {
			std::string btext; int bbx = 0, bby = 0, bbz = 0;
			if (AirshipTaxi::GetBubble(gc, btext, bbx, bby, bbz)) {
				NativeUi::HudText(gc, btext, (float)vp.Width * 0.5f, 105.0f, 1);
			}
			// GemForge-Overlay: Cube-Zaehler der angelegten Waffe.
			std::string gtext;
			if (GemForge::GetEquippedInfo(gc, gtext)) {
				NativeUi::HudText(gc, gtext, (float)vp.Width * 0.5f, 170.0f, 1);
			}
		}

		SafeHud(gc, 1);                                           // ungenutzte Zeilen ausblenden
	} // Ende "if (gc && gc->local_player)"
	else SafeHud(nullptr, 2);

	return Original_EndScene(pDevice);
}

// --- D3D HELPER ---
void GetD3D9Addresses(void** endScene, void** reset) {
	WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DummyClass", NULL };
	RegisterClassExA(&wc);
	HWND hWnd = CreateWindowA("DummyClass", "", WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);
	LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D) { return; }

	D3DPRESENT_PARAMETERS d3dpp = {};
	d3dpp.Windowed = TRUE; d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD; d3dpp.hDeviceWindow = hWnd;
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; d3dpp.BackBufferCount = 1; d3dpp.BackBufferWidth = 1; d3dpp.BackBufferHeight = 1;

	LPDIRECT3DDEVICE9 pDevice = nullptr;
	HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
	if (FAILED(hr) || !pDevice) { hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice); }
	if (FAILED(hr) || !pDevice) { return; }

	void** pVTable = *reinterpret_cast<void***>(pDevice);
	*endScene = pVTable[42]; *reset = pVTable[16];

	pDevice->Release(); pD3D->Release(); DestroyWindow(hWnd); UnregisterClassA("DummyClass", wc.hInstance);
}

// XAudio2 2.7 (DirectX SDK Juni 2010) meldet in DllCanUnloadNow faelschlich "entladbar". Ruft irgendein
// Teil des Prozesses CoFreeUnusedLibraries auf -- das tun z. B. die Windows-Dateidialoge des LAB
// (GetOpenFileName/GetSaveFileName) --, wird xaudio2_7.dll entladen, waehrend ihr Audio-Thread noch laeuft.
// Folge: "Ausfuehren an 0x5EBFA5E0 (kein Modul)" -- in beiden Minidumps vom 2026-10-08 liegt die Adresse
// in der ENTLADENEN xaudio2_7.dll (0x5EBC0000..0x5EC4E000). Abhilfe (bekannter Workaround): die DLL selbst
// laden und fuer die Prozessdauer festnageln.
static void PinXAudio27() {
	HMODULE h = LoadLibraryA("xaudio2_7.dll");
	HMODULE pinned = nullptr;
	BOOL ok = h && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, "xaudio2_7.dll", &pinned);
	std::cout << "[Audio] xaudio2_7.dll " << (ok ? "festgenagelt (Schutz gegen vorzeitiges Entladen)." : "nicht gefunden -- kein Schutz noetig/moeglich.") << std::endl;
}

// --- START ---
// Alle Mod-Dateien (Strukturen\, AdventureMod_*.txt/.cfg, Save\AdventureMod.db) liegen relativ zum Spielordner.
// Damit das auch gilt, wenn Cube.exe ueber eine Verknuepfung mit anderem "Ausfuehren in" startet: Arbeitsordner =
// Ordner der laufenden Cube.exe (dynamisch, kein fester Pfad).
static void ChdirToGameDir() {
	char p[MAX_PATH] = ""; if (!GetModuleFileNameA(NULL, p, MAX_PATH)) return;
	char* s = strrchr(p, '\\'); if (s) { *s = 0; SetCurrentDirectoryA(p); }
}
DWORD WINAPI MainThread(LPVOID lpReserved) {
	ChdirToGameDir();                                          // vor Log und allen Dateien
	if (kDevBuild) { AllocConsole(); FILE* fDummy; freopen_s(&fDummy, "CONOUT$", "w", stdout); }   // Veroeffentlichung: kein Konsolenfenster

	// Logdatei + Absturzfilter + externer Logger (eigener Prozess, ueberlebt
	// einen Absturz von Cube World). VOR dem cout-Abzweig, damit von Anfang an
	// alles in AdventureMod_log.txt landet.
	CrashLogger::InitInGame(g_hModule);
	// Ab hier geht jede Konsolenzeile zusaetzlich ins LAB-Protokoll und in die Logdatei.
	LabBridge::InstallLogTee();

	std::cout << "========================================" << std::endl;
	std::cout << " AdventureMod gestartet! " << std::endl;
	std::cout << "========================================" << std::endl;
	PinXAudio27();
	Sleep(2000);
	CrashLogger::EnsureLargeAddressAware(ModSwitches::Get("vier_gb", false));   // Grundeinstellung 4 GB (Strukturen\\Grundeinstellungen)
	cube::InitGlobals(); MH_Initialize();
	// Karte & Leistung: Patch-Stellen gegen die Cube.exe pruefen, Einstellungen laden.
	// Angewendet wird erst im ersten Spiel-Frame (PerfTweaks::Tick).
	PerfTweaks::Install();
	// Eigene Biome (RE_Biome.md): 5 MinHook-Hooks in die Weltgenerierung, aktiv
	// erst mit MH_EnableHook unten. Ohne aktive .biom-Datei bitgleich Vanilla.
	BiomeMod::Install();
	MobLab::InstallBodyHook();                  // Koerperbau je Rasse (LAB > Mobs)
	GemForge::InstallLampPatches();             // Lampe an der Bench + Schimmerpilze -> Lichtradius
	Items::Reload();                            // eigene Gegenstaende (LAB -> Gegenstaende), vor den Rezepten
	Dungeons::Reload();                         // Dungeon-Baukasten (Strukturen\\Dungeons)
	Skills::Reload();                           // eigene Skills (LAB -> Skills); legt Teleport/Beschwoeren an
	Recipes::Install();                         // eigene Handwerksrezepte (LAB -> Rezepte)
	Effects::Install();                         // Wirkungen der Effekt-Traenke + Reiter "Effekte"
	TierGelenke::Install();                     // eigene Bein-Gelenke (Beine schwingen um Huefte/Schulter statt um die Modellmitte)

	// Adventure-Mod-Bausteine beim Framework anmelden, BEVOR die Framework-Hooks
	// installiert werden. Weitere Mods (VillageGenerator, ...) kommen hier als
	// weitere "Register...()"-Aufrufe dazu.
	AdventureMod::RegisterWithFramework();
	PirateShip::RegisterWithFramework();
	PirateAirship::RegisterWithFramework();
	AirshipSpawner::RegisterWithFramework();
	MountainVillage::RegisterWithFramework();
	AirshipTaxi::RegisterWithFramework();
	// Schmied-NPC entfernt. Die Registrierung bleibt NUR wegen des Hooks auf
	// 0x588500, aus dem GemForge::OnBenchDrawBegin/End gerufen wird -- ohne den
	// werden die Extra-Cubes in der Bench nicht gezeichnet.
	BlacksmithNpc::RegisterWithFramework();
	GemForge::RegisterWithFramework();
	// LAB: Neu-Stempeln gemerkter Strukturen nach jeder Zonengenerierung.
	LabBridge::RegisterWithFramework();
	// Biome: Pflanzen/Dungeons nach jeder Zonengenerierung, Mobs in Spielernaehe.
	BiomeMod::RegisterWithFramework();

	void* pEndScene = nullptr; void* pReset = nullptr;
	GetD3D9Addresses(&pEndScene, &pReset);

	if (pEndScene && pReset) {
		MH_CreateHook(pEndScene, &Hooked_EndScene, reinterpret_cast<LPVOID*>(&Original_EndScene));
		MH_CreateHook(pReset, &Hooked_Reset, reinterpret_cast<LPVOID*>(&Original_Reset));
		MH_EnableHook(MH_ALL_HOOKS);

		// Callback-Hook fuer "Zone fertig generiert" (OnZoneGenerated).
		AdventureFramework::InstallZoneGeneratedHook();

		std::cout << "[DEBUG] System bereit!" << std::endl;

		// Zweites Fenster: das LAB. Eigener Thread, fasst das Spiel nur ueber
		// LabBridge::Post an. [F10] im Spiel blendet es ein/aus.
		LabWindow::Start();
		std::cout << " - LAB-Fenster bereit (unsichtbar, [F10] ein/aus)." << std::endl;
		// Hinweise der uebrigen Module stumm -- die Konsole soll waehrend der
		// GemForge-Fehlersuche lesbar bleiben.
		// std::cout << " - Wandernder Haendler spawnt automatisch ~alle 90 Minuten (Katalog wird beim Spawn gebaut)" << std::endl;
		// std::cout << " - [R] beim Haendler anvisiert: natives Talk-/Shop-Menu" << std::endl;
		// std::cout << " - [F9] Tracking-Anzeige (Spieler- und Haendlerposition) ein-/ausschalten" << std::endl;
		// std::cout << " - [L] Labor-/Test-Menue oeffnen (u.a. Haendler spawnen)" << std::endl;
		// std::cout << " - Ozean-Modul aktiv: mehr Fische ([L]-Menue [2] Schwarm, [3] Auto-Fische)" << std::endl;
		std::cout << " - GemForge: In der Dorf-Schmiede an die WERKBANK stellen und [R] druecken" << std::endl;
		std::cout << "   (alternativ [R] beim Schmied). Cube im Vorrat waehlen, auf die Waffe klicken." << std::endl;
		std::cout << "   100 Cubes je Material (Ruby/Emerald/Sapphire/Diamond/Gold), max. 500 je Waffe." << std::endl;
		std::cout << "   Cubes muessen im Inventar liegen; Material und Level muessen zur Waffe passen." << std::endl;
		std::cout << "   [STRG+4] Edelstein-Zutaten in setzbare Cubes umwandeln (Waffe anlegen!)" << std::endl;
std::cout << "   [STRG+B] binden, [STRG+X] Extras loeschen, [STRG+I] Bench-Dump, [STRG+U] Inventar" << std::endl;
	}
	else {
		std::cout << "[FEHLER] D3D9 EndScene/Reset-Adressen konnten nicht ermittelt werden." << std::endl;
	}
	return TRUE;
}

// ============================================================================
// SERVER-ZWEIG: dieselbe DLL in Server.exe (coremaze Server-Mod-Launcher,
// Ordner Server_Mods). Dort gibt es kein Fenster, kein D3D, keinen
// GameController und keine Spielerfigur -- nur die Welterzeugung. Deshalb
// laufen hier ausschliesslich die Weltgenerierungs-Teile des Mods, alle
// Spieladressen ueber Host::Re (Cube.exe -> Server.exe, AddrMap_Server.inc).
// ============================================================================
static std::ofstream g_serverLog;
static void ServerChdirToExe() {
	char p[MAX_PATH] = ""; GetModuleFileNameA(NULL, p, MAX_PATH);
	char* s = strrchr(p, '\\'); if (s) { *s = 0; SetCurrentDirectoryA(p); }   // Strukturen\, .biom, Schalter neben Server.exe
}
DWORD WINAPI ServerMainThread(LPVOID) {
	ServerChdirToExe();
	g_serverLog.open("AdventureMod_server_log.txt", std::ios::out | std::ios::trunc);
	if (g_serverLog) std::cout.rdbuf(g_serverLog.rdbuf());
	std::cout << "========================================" << std::endl;
	std::cout << " AdventureMod -- SERVER-Modus (Server.exe)" << std::endl;
	std::cout << "========================================" << std::endl;
	std::cout << "[Host] Adresstabelle: " << Host::MappedCount() << " von " << Host::TableSize()
	          << " Cube.exe-Adressen haben ein Server-Gegenstueck." << std::endl;
	if (MH_Initialize() != MH_OK) { std::cout << "[FEHLER] MinHook startet nicht -- Mod bleibt aus." << std::endl; return 0; }
	// Weltgenerierung: Klima/Hoehe/Boden/Fels/Fluss/Baum/Rasse-Hooks, Zonen-Anker (Staedte, Tempel,
	// Zonen-Modelle, Zonen-Teiler, Runensteine). Fehlt eine Adresse im Server, prueft Install das selbst.
	BiomeMod::Install();
	MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
	std::cout << "[Server] Hooks aktiv: " << (st == MH_OK ? "ja" : "NEIN") << ". Biome: "
	          << (BiomeMod::GetStatus().hooksOk ? "bereit" : BiomeMod::GetStatus().hookReport) << std::endl;
	std::cout << "[Server] Nicht im Server (nur Client): LAB, HUD, Skills, Chat, Shader, Haendler-Menues, Flugtiere." << std::endl;
	std::cout.flush();
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	// Im Logger-Prozess (rundll32 ... ,LabLoggerMain) den Mod NICHT starten --
	// dort laeuft nur der Logger.
	if (ul_reason_for_call == DLL_PROCESS_ATTACH && CrashLogger::IsLoggerHost()) { DisableThreadLibraryCalls(hModule); return TRUE; }
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) g_hModule = hModule;
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		// Selbsterkennung: Server.exe -> nur Welterzeugung, Cube.exe -> voller Mod (wie bisher).
		CreateThread(NULL, 0, Host::IsServer() ? ServerMainThread : MainThread, 0, 0, NULL);
	}
	return TRUE;
}
