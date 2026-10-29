#include "GemForge.h"
#include "Host.h"
#include "MinHook.h"
#include <windows.h>
#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <cmath>

// ===================================================================
// GemForge.cpp -- 100 Cubes an der nativen Customization Bench.
//
// Warum die vorherige Fassung nicht funktionieren KONNTE (Kurzfassung,
// ausfuehrlich in RE_Notizen_GemForge.md):
//
//  1) ToggleRender() war an KEINE Taste gebunden. STRG+V rief
//     ApplyCubesToWeapon() auf, der Render-Hook wurde nie installiert.
//  2) Die Material-IDs {0x0b,0x0d,0x0e,0x0f,0x10} sind falsch:
//     material_toColor (0x4C7250) kennt nur 1,2,5,7,0x0b,0x0c und
//     0x80..0x83. 0x0d/0x0e/0x0f/0x10 fallen alle in den default-Zweig
//     -> einheitliches Grau statt Edelsteinfarben. Die Gem-Materialien
//     sind 0x80 (rot), 0x81 (gruen), 0x82 (blau), 0x83 (weiss) -- genau
//     die Glow-Zweige, die auch CombatBehavior::spawnHitParticles
//     ueber "if (material > 0x7f)" fuer Funken auswertet.
//  3) ApplyCubesToWeapon schrieb bis zu 32 Cubes ins Item, obwohl das
//     Spiel je Waffentyp nur 32 ODER 16 erlaubt (Item_get_stack_or_grid_size
//     @0x4C7660). Ueber 16 Cubes auf einem Kurzschwert = Zustand, den das
//     Spiel selbst nie erzeugt.
//  4) Die Bench wurde mit "*(u32*)(widget+0x160) = 0" geoeffnet. +0x160 ist
//     KEIN Modus, sondern der Zeiger auf das zu bearbeitende Item.
//     drawWeaponCustomization (0x588500) steigt bei 0 sofort aus.
//  5) Die Aufrufkonvention des Render-Hooks war geraten (__stdcall).
//
// Diese Fassung: nichts geraten. Die Aufrufkonvention wird an den
// AUFRUFSTELLEN im Maschinencode nachgesehen (siehe AnalyzeConvention).
// Ist das Ergebnis nicht eindeutig, wird der betroffene Hook NICHT
// gesetzt -- lieber eine fehlende Funktion als ein zerlegter Stack.
// ===================================================================

namespace GemForge {

	// ------------------------------------------------------------------
	// NOTBREMSEN. Falls es doch noch kracht: hier einzeln auf false setzen
	// und neu bauen, um den schuldigen Hook einzugrenzen. Reihenfolge des
	// Verdachts von oben nach unten.
	// ------------------------------------------------------------------
	static const bool kEnableRenderHook  = true; // 0x471B60 -- laeuft ab Spielstart staendig
	static const bool kEnableBenchHook   = true; // 0x588250 -- nur bei offener Bench
	static const bool kEnableDamageHook  = true; // 0x4C7F60 -- risikofrei (keine Stack-Argumente)
	static const bool kEnableAddCubeHook = true; // 0x4C69B0 -- Auffangnetz beim Cube-Eintrag
	static const bool kEnableGemSupply   = true; // Edelsteine im Bench-Vorrat + Auswahl-Merker
	static const bool kEnableGemColorHook= true; // 0x4C7250 -- Edelsteinfarben
	static const bool kEnableUniformHook = true; // 0x448280 -- Farbe nach der Injektion zurueck
	static const bool kEnableCounterHook = true; // 0x4C7660 -- Zaehler "Upgrades n/m"

	// ------------------------------------------------------------------
	// Bench-Vorschau. Signatur inzwischen per Decompile BELEGT:
	//   void __thiscall FUN_004482a0(void* this, float* p1, float* p2, float* p3)
	// -> this in ECX, drei Stack-Argumente, "ret 0xC". Genau das Modell, das
	// Hook_Upload benutzt. Die Funktion schreibt nirgends durch ihre
	// Parameter, sie liest nur je 16 Floats.
	//
	// Der frueher beobachtete Absturz (Schreibzugriff an 0xC100000C) kam
	// deshalb NICHT von der Konvention, sondern aus der Reentranz: die
	// Injektion lief mitten im Aufruf, also zwischen "Uniform setzen" und
	// "Mesh zeichnen" -- und danach lief die Spielsequenz mit den von uns
	// ueberschriebenen Uniformen weiter. Behoben, siehe Hook_Upload.
	// ------------------------------------------------------------------
	static const bool kEnablePreviewHook = true;

	// ------------------------------------------------------------------
	// Konstanten aus dem Reversal-Baum
	// ------------------------------------------------------------------
	static const uint32_t kAddr_TryPlaceCube    = 0x00588250; // VoxelWidget::tryPlaceCube
	static const uint32_t kAddr_TryPlaceCubeEnd = 0x00588500; // naechste Funktion (drawWeaponCustomization)
	static const uint32_t kAddr_AddAdaptionCube = 0x004C69B0; // Item::addAdaptionCube
	static const uint32_t kAddr_AddAdaptionEnd  = 0x004C6A90; // naechste Funktion (item_computeBlockValue)
	static const uint32_t kAddr_RenderCubes     = 0x00471B60; // item_renderAdaptionCubes
	static const uint32_t kAddr_RenderCubesEnd  = 0x00471D50; // naechste Funktion (drawOrientedModelOrTrail)
	static const uint32_t kAddr_WeaponDamage    = 0x004C7F60; // item_weaponDamage
	static const uint32_t kAddr_ToggleSelection  = 0x0058CE40; // VoxelWidget::toggleSelection
	static const uint32_t kAddr_StackSize        = 0x004C7660; // Item_get_stack_or_grid_size
	static const uint32_t kAddr_StackSizeEnd     = 0x004C76A0; // naechste Funktion
	static const uint32_t kAddr_SetUniform30     = 0x00448280; // render_setUniform30
	static const uint32_t kAddr_SetUniform30End  = 0x004482A0; // naechste Funktion
	static const uint32_t kAddr_MaterialColor    = 0x004C7250; // material_toColor
	static const uint32_t kAddr_MaterialColorEnd = 0x004C7660; // naechste Funktion (get_stack_or_grid_size)
	static const uint32_t kAddr_UploadMatrices  = 0x004482A0; // render_uploadModelViewMatrices
	static const uint32_t kAddr_UploadMatricesEnd = 0x00448F10; // naechste Funktion (uploadBoneMatrices)

	// Item-Offsets (0x118 Byte gesamt)
	static const int kIt_Category   = 0x00;
	static const int kIt_ItemId     = 0x01;
	static const int kIt_Modifier   = 0x04;
	static const int kIt_Field8     = 0x08;
	static const int kIt_Rarity     = 0x0C;
	static const int kIt_Material   = 0x0D;
	static const int kIt_Level      = 0x10;
	static const int kIt_Adaptions  = 0x14;
	static const int kIt_Count      = 0x114;
	static const int kIt_Size       = 0x118;

	// VoxelWidget-Offsets (belegt ueber ctor 0x587F70 + drawWeaponCustomization 0x588500)
	static const int kW_Item        = 0x160; // Item* -- das bearbeitete Item
	static const int kW_CubeVecBeg  = 0x170; // vector<{int count; Item item;}> (stride 0x11C)
	static const int kW_CubeVecEnd  = 0x174;
	static const int kW_SelCube     = 0x17C; // Index in den Cube-Vorrat, -1 = keiner
	static const int kW_HitValid    = 0x2A8; // Raycast hat ein Voxel getroffen
	static const int kW_HitX        = 0x29C;
	static const int kW_HitY        = 0x2A0;
	static const int kW_HitZ        = 0x2A4;
	static const int kW_HoverCube   = 0x2AC; // Index eines bereits gesetzten Cubes, -1 = keiner
	static const int kW_VecStride   = 0x11C;
	// "Cube in der Hand" -- vom Hover-Zweig in tryPlaceCube benutzt
	static const int kW_PickActive  = 0x3C8; // bool: ein Cube ist aufgenommen
	static const int kW_PickX       = 0x3CC;
	static const int kW_PickY       = 0x3D0;
	static const int kW_PickZ       = 0x3D4;
	static const int kW_PickMat     = 0x3D8;
	static const int kW_PickLvl     = 0x3DC;

	// ------------------------------------------------------------------
	// Layout-Pruefungen zur Compile-Zeit (schlagen fehl, falls cwsdk driftet)
	// ------------------------------------------------------------------
	static_assert(sizeof(::Item) == kIt_Size, "Item muss 0x118 Byte gross sein");
	static_assert(sizeof(::ItemAdaption) == 8, "ItemAdaption muss 8 Byte gross sein");
	static_assert(offsetof(::Item, customization_data) == kIt_Adaptions, "customization_data @0x14");
	static_assert(offsetof(::Item, adaption_count) == kIt_Count, "adaption_count @0x114");

	// ------------------------------------------------------------------
	// Parallel-Store
	// ------------------------------------------------------------------
	struct Entry {
		uint64_t key;
		int      count;                          // Anzahl EXTRA-Cubes (ueber die nativen hinaus)
		uint8_t  cubes[kMaxCubesTotal * 8];      // gleiches Format wie ItemAdaption
	};
	static const int kMaxTrackedWeapons = 64;     // 64 * ~4 KB = ~256 KB statisch
	static Entry g_store[kMaxTrackedWeapons];
	static int   g_storeCount = 0;

	static cube::GameController* g_gc = nullptr;
	// != 0, solange drawWeaponCustomization laeuft (frueh deklariert, weil
	// mehrere Hooks daran erkennen, ob sie in der Bench sind).
	static void* g_benchWidget = nullptr;

	// ==================================================================
	// Persistenz: der Parallel-Store liegt sonst nur im Speicher und waere
	// nach jedem Spielstart leer. Geschrieben wird eine schlichte Binaerdatei
	// neben Cube.exe. Schluessel ist derselbe Item-Hash wie zur Laufzeit --
	// er besteht nur aus Feldern, die das Spiel nicht mehr veraendert
	// (Kategorie, item_id, modifier, field_8, rarity, material, level).
	// ==================================================================
	static const uint32_t kSaveMagic   = 0x47524647u;   // "GFRG"
	static const uint32_t kSaveVersion = 1;
	static bool  g_storeDirty = false;
	static DWORD g_lastSaveTick = 0;

	static void MarkStoreDirty() { g_storeDirty = true; }

	static bool StorePath(char* out, size_t n) {
		if (!GetModuleFileNameA(NULL, out, (DWORD)n)) return false;
		size_t len = strlen(out);
		while (len > 0 && out[len - 1] != '\\' && out[len - 1] != '/') len--;
		out[len] = '\0';
		return strncat_s(out, n, "GemForge.dat", _TRUNCATE) == 0;
	}

	static void SaveStore() {
		char path[MAX_PATH];
		if (!StorePath(path, sizeof(path))) return;
		FILE* f = nullptr;
		if (fopen_s(&f, path, "wb") != 0 || !f) return;
		uint32_t used = 0;
		for (int i = 0; i < g_storeCount; i++) if (g_store[i].count > 0) used++;
		fwrite(&kSaveMagic, 4, 1, f);
		fwrite(&kSaveVersion, 4, 1, f);
		fwrite(&used, 4, 1, f);
		for (int i = 0; i < g_storeCount; i++) {
			if (g_store[i].count <= 0) continue;
			int32_t cnt = g_store[i].count;
			fwrite(&g_store[i].key, 8, 1, f);
			fwrite(&cnt, 4, 1, f);
			fwrite(g_store[i].cubes, 8, (size_t)cnt, f);
		}
		fclose(f);
		g_storeDirty = false;
	}

	static void LoadStore() {
		char path[MAX_PATH];
		if (!StorePath(path, sizeof(path))) return;
		FILE* f = nullptr;
		if (fopen_s(&f, path, "rb") != 0 || !f) return;
		uint32_t magic = 0, ver = 0, used = 0;
		bool ok = fread(&magic, 4, 1, f) == 1 && fread(&ver, 4, 1, f) == 1
			   && fread(&used, 4, 1, f) == 1 && magic == kSaveMagic && ver == kSaveVersion;
		int loaded = 0;
		if (ok) {
			for (uint32_t i = 0; i < used && g_storeCount < kMaxTrackedWeapons; i++) {
				uint64_t key = 0; int32_t cnt = 0;
				if (fread(&key, 8, 1, f) != 1 || fread(&cnt, 4, 1, f) != 1) break;
				if (cnt <= 0 || cnt > kMaxCubesTotal) break;         // Datei beschaedigt
				Entry& e = g_store[g_storeCount];
				if (fread(e.cubes, 8, (size_t)cnt, f) != (size_t)cnt) break;
				e.key = key; e.count = cnt;
				g_storeCount++; loaded++;
			}
		}
		fclose(f);
		std::cout << "[GemForge] Gespeicherte Cubes geladen: " << loaded << " Waffe(n)"
			<< (ok ? "" : " -- Datei unlesbar oder aus einer anderen Version") << std::endl;
	}

	static uint32_t RebaseAddr(uint32_t a) {
		return Host::Re(a);
	}

	// ==================================================================
	// Aufrufkonvention bestimmen -- an den AUFRUFSTELLEN, nicht geraten.
	//
	// Der Reversal-Baum haelt fuer Spielfunktionen KEINE Konvention fest
	// (nur fuer erkannte Bibliotheksfunktionen steht dort __cdecl/__stdcall).
	// Ein Scan im Funktionsrumpf nach "C2 imm16" ist eine Heuristik und kann
	// zufaellig anschlagen -- bei 6 Stack-Argumenten kostet ein Fehlgriff
	// 24 Byte Stack pro Aufruf, und 0x471B60 feuert ab Spielstart staendig.
	//
	// Eindeutig ist dagegen der Aufrufer: raeumt ER den Stack ab (cdecl),
	// steht direkt hinter dem CALL ein "add esp, n" bzw. "lea esp,[esp+n]".
	// Steht das nirgends und der Rumpf endet auf "ret n", raeumt der Callee.
	// ==================================================================
	enum Convention { CONV_UNKNOWN = 0, CONV_CALLER_CLEANS, CONV_CALLEE_CLEANS };

	// .text-Bereich des Moduls aus den PE-Headern holen.
	static bool GetCodeRange(uint8_t** outBegin, size_t* outSize) {
		bool ok = false;
		__try {
			uint8_t* base = (uint8_t*)GetModuleHandleA(NULL);
			if (!base) return false;
			if (*(uint16_t*)base != 0x5A4D) return false;               // "MZ"
			uint32_t peOff = *(uint32_t*)(base + 0x3C);
			uint8_t* nt = base + peOff;
			if (*(uint32_t*)nt != 0x00004550) return false;             // "PE\0\0"
			uint16_t nSections = *(uint16_t*)(nt + 6);
			uint16_t optSize = *(uint16_t*)(nt + 0x14);
			uint8_t* sec = nt + 0x18 + optSize;
			for (uint16_t i = 0; i < nSections; i++, sec += 0x28) {
				uint32_t chars = *(uint32_t*)(sec + 0x24);
				if (chars & 0x00000020) {                               // IMAGE_SCN_CNT_CODE
					*outBegin = base + *(uint32_t*)(sec + 0x0C);        // VirtualAddress
					*outSize  = (size_t)*(uint32_t*)(sec + 0x08);       // VirtualSize
					ok = true;
					break;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	// Zaehlt die CALL-Stellen auf targetRva und schaut, was direkt dahinter steht.
	static void ScanCallSites(uint32_t targetRva, uint8_t argBytes,
							  int* outSites, int* outCallerCleans) {
		*outSites = 0; *outCallerCleans = 0;
		__try {
			uint8_t* code = nullptr; size_t codeSize = 0;
			if (!GetCodeRange(&code, &codeSize)) return;
			uint8_t* target = (uint8_t*)(uintptr_t)RebaseAddr(targetRva);
			for (size_t i = 0; i + 12 < codeSize; i++) {   // +12: q[0..5] bleibt in Grenzen
				if (code[i] != 0xE8) continue;                          // CALL rel32
				int32_t rel = *(int32_t*)(code + i + 1);
				if (code + i + 5 + rel != target) continue;
				(*outSites)++;
				const uint8_t* q = code + i + 5;
				// add esp, imm8   /   lea esp,[esp+imm8]   /   add esp, imm32
				if ((q[0] == 0x83 && q[1] == 0xC4 && q[2] == argBytes) ||
					(q[0] == 0x8D && q[1] == 0x64 && q[2] == 0x24 && q[3] == argBytes) ||
					(q[0] == 0x81 && q[1] == 0xC4 && *(uint32_t*)(q + 2) == argBytes)) {
					(*outCallerCleans)++;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { }
	}

	// Endet die Funktion auf "ret imm16"? Rueckwaerts ab endRva ueber die
	// MSVC-Ausrichtungsbytes (0xCC / 0x90) laufen und die letzte echte
	// Instruktion ansehen -- viel verlaesslicher als ein Scan im Rumpf.
	static bool TailIsRetImm(uint32_t startRva, uint32_t endRva, uint8_t imm) {
		bool ok = false;
		__try {
			const uint8_t* p = (const uint8_t*)(uintptr_t)RebaseAddr(startRva);
			size_t len = (size_t)(endRva - startRva);
			size_t i = len;
			while (i > 0 && (p[i - 1] == 0xCC || p[i - 1] == 0x90)) i--;
			if (i >= 3 && p[i - 3] == 0xC2 && p[i - 2] == imm && p[i - 1] == 0x00) ok = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	// Endet die Funktion auf einem schlichten "ret" (0xC3)? Gegenstueck zu
	// TailIsRetImm fuer Funktionen ohne Stack-Argumente.
	static bool TailIsPlainRet(uint32_t startRva, uint32_t endRva) {
		bool ok = false;
		__try {
			const uint8_t* p = (const uint8_t*)(uintptr_t)RebaseAddr(startRva);
			size_t len = (size_t)(endRva - startRva);
			size_t i = len;
			while (i > 0 && (p[i - 1] == 0xCC || p[i - 1] == 0x90)) i--;
			if (i >= 1 && p[i - 1] == 0xC3) ok = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	// Liefert die tatsaechliche Konstante des abschliessenden RET:
	//   >= 0  "ret imm16"  -- der Callee raeumt genau so viele Bytes ab
	//   -1    "ret"        -- der Callee raeumt nichts ab
	//   -2    keines von beidem erkennbar
	//
	// Damit laesst sich das ARGUMENTMODELL bestimmen, nicht nur die
	// Aufraeumrichtung. Bei vier logischen Parametern gilt:
	//   ret 0x10 -> alle vier auf dem Stack        (stdcall)
	//   ret 0x0C -> erster in ECX, drei am Stack   (thiscall)
	//   ret 0x08 -> erster ECX, zweiter EDX        (fastcall)
	//   ret      -> alle vier am Stack, Aufrufer raeumt ab (cdecl)
	static int TailRetImmValue(uint32_t startRva, uint32_t endRva) {
		int result = -2;
		__try {
			const uint8_t* p = (const uint8_t*)(uintptr_t)RebaseAddr(startRva);
			size_t len = (size_t)(endRva - startRva);
			size_t i = len;
			while (i > 0 && (p[i - 1] == 0xCC || p[i - 1] == 0x90)) i--;
			if (i >= 1 && p[i - 1] == 0xC3) result = -1;
			else if (i >= 3 && p[i - 3] == 0xC2) result = (int)p[i - 2] | ((int)p[i - 1] << 8);
		} __except (EXCEPTION_EXECUTE_HANDLER) { result = -2; }
		return result;
	}

	// ------------------------------------------------------------------
	// Argumentmodell bestimmen, ohne sich auf das Funktionsende zu verlassen.
	//
	// Das Funktionsende taugt bei switch-lastigen Funktionen nicht: MSVC legt
	// die SPRUNGTABELLE als Daten unmittelbar hinter den Code, der
	// Rueckwaertslauf landet dann in der Tabelle statt im Epilog. Genau das
	// passiert bei material_toColor (0x4C7250) -- gelesen wurde "-2".
	//
	// Belastbarer sind die Aufrufstellen: raeumt der Aufrufer ab, steht direkt
	// hinter dem CALL ein "add esp,N" bzw. "lea esp,[esp+N]", und N ist genau
	// die Zahl der Stack-Bytes. Findet sich das an keiner Stelle, raeumt die
	// Funktion selbst ab, und die Byte-Zahl steht in ihrem "ret imm16" --
	// dafuer wird der Rumpf durchsucht und der haeufigste Wert genommen.
	//
	// outBytes = Stack-Bytes, Rueckgabe = wer abraeumt.
	static Convention DetectArgModel(uint32_t startRva, uint32_t endRva,
									 int* outBytes, int* outSites, int* outCallerCleans) {
		*outBytes = -1; *outSites = 0; *outCallerCleans = 0;
		int cleanBytes = -1;
		__try {
			uint8_t* code = nullptr; size_t codeSize = 0;
			if (!GetCodeRange(&code, &codeSize)) return CONV_UNKNOWN;
			uint8_t* target = (uint8_t*)(uintptr_t)RebaseAddr(startRva);
			for (size_t i = 0; i + 12 < codeSize; i++) {
				if (code[i] != 0xE8) continue;
				int32_t rel = *(int32_t*)(code + i + 1);
				if (code + i + 5 + rel != target) continue;
				(*outSites)++;
				const uint8_t* q = code + i + 5;
				int n = -1;
				if      (q[0] == 0x83 && q[1] == 0xC4) n = q[2];                    // add esp,imm8
				else if (q[0] == 0x8D && q[1] == 0x64 && q[2] == 0x24) n = q[3];    // lea esp,[esp+imm8]
				else if (q[0] == 0x81 && q[1] == 0xC4) n = (int)*(uint32_t*)(q + 2);// add esp,imm32
				if (n >= 0) { (*outCallerCleans)++; if (cleanBytes < 0) cleanBytes = n; }
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { return CONV_UNKNOWN; }

		if (*outCallerCleans > 0 && cleanBytes >= 0) { *outBytes = cleanBytes; return CONV_CALLER_CLEANS; }
		if (*outSites == 0) return CONV_UNKNOWN;

		// Niemand raeumt am Aufrufer ab -> die Funktion tut es selbst.
		// Haeufigstes "ret imm16" im Rumpf gewinnt; einzelne Treffer koennen
		// aus der Sprungtabelle stammen, die Mehrheit nicht.
		int best = -1, bestN = 0;
		__try {
			const uint8_t* p = (const uint8_t*)(uintptr_t)RebaseAddr(startRva);
			size_t len = (size_t)(endRva - startRva);
			static const int kCand[] = { 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20 };
			for (int k = 0; k < (int)(sizeof(kCand) / sizeof(kCand[0])); k++) {
				int n = 0;
				for (size_t i = 0; i + 2 < len; i++)
					if (p[i] == 0xC2 && p[i + 1] == (uint8_t)kCand[k] && p[i + 2] == 0x00) n++;
				if (n > bestN) { bestN = n; best = kCand[k]; }
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { best = -1; }
		if (best >= 0) { *outBytes = best; return CONV_CALLEE_CLEANS; }
		return CONV_UNKNOWN;
	}

	static Convention AnalyzeConvention(const char* name, uint32_t startRva, uint32_t endRva,
										uint8_t argBytes) {
		int sites = 0, callerCleans = 0;
		ScanCallSites(startRva, argBytes, &sites, &callerCleans);
		bool tailRetImm = TailIsRetImm(startRva, endRva, argBytes);

		std::cout << "[GemForge] Konventions-Analyse " << name << ": "
			<< sites << " Aufrufstelle(n), davon " << callerCleans
			<< " mit 'add esp," << (int)argBytes << "'; Rumpf endet auf 'ret "
			<< (int)argBytes << "': " << (tailRetImm ? "ja" : "nein") << std::endl;

		// Entscheidungsregel, nach Beweiskraft geordnet:
		//
		// 1. Endet die Funktion auf "ret imm16", raeumt sie die Argumente
		//    SELBST ab. Das ist keine Heuristik, sondern die Semantik der
		//    Instruktion -- der Aufrufer kann dieselben Bytes nicht noch
		//    einmal abraeumen. Der Aufrufstellen-Scan darf das nicht
		//    ueberstimmen; er kann durch zufaellige Bytefolgen
		//    falsch-positiv werden, das Funktionsende nicht.
		// 2. Schlichtes "ret" + mindestens eine Aufrufstelle mit
		//    "add esp,n"  ->  der Aufrufer raeumt ab.
		// 3. Alles andere: Finger weg. Insbesondere "ret" ohne passendes
		//    Aufraeumen heisst, dass die Argumentzahl nicht stimmt.
		if (tailRetImm)                                  return CONV_CALLEE_CLEANS;
		if (sites == 0)                                  return CONV_UNKNOWN;
		if (TailIsPlainRet(startRva, endRva) && callerCleans > 0) return CONV_CALLER_CLEANS;
		return CONV_UNKNOWN;
	}

	// Identitaet einer Waffe ueber ihre unveraenderlichen Felder. Ueberlebt
	// Kopien zwischen Inventar und Ausruestung (das Spiel kopiert die
	// Item-Struktur byteweise, Item_copyStruct @0x42C5E0).
	static uint64_t ItemKey(const uint8_t* it) {
		static const int kBytes[] = { 0x00, 0x01, 0x04, 0x05, 0x06, 0x07,
									  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
									  0x10, 0x11 };
		uint64_t h = 1469598103934665603ULL;
		for (int i = 0; i < (int)(sizeof(kBytes) / sizeof(kBytes[0])); i++) {
			h ^= (uint64_t)it[kBytes[i]];
			h *= 1099511628211ULL;
		}
		return h;
	}

	static Entry* FindEntry(uint64_t key) {
		for (int i = 0; i < g_storeCount; i++)
			if (g_store[i].key == key) return &g_store[i];
		return nullptr;
	}

	static Entry* FindOrCreateEntry(uint64_t key) {
		Entry* e = FindEntry(key);
		if (e) return e;
		if (g_storeCount >= kMaxTrackedWeapons) return nullptr;
		e = &g_store[g_storeCount++];
		e->key = key; e->count = 0;
		memset(e->cubes, 0, sizeof(e->cubes));
		return e;
	}

	// Item_get_stack_or_grid_size @0x4C7660 -- wie viele Cubes das Spiel
	// selbst maximal in dieses Item eintraegt.
	static int NativeCap(const uint8_t* it) {
		if (it[kIt_Category] != 0x03) return 16;
		uint8_t s = it[kIt_ItemId];
		if (s == 0x05 || s == 0x06 || s == 0x07 || s == 0x08 || s == 0x0A ||
			s == 0x0B || s == 0x0F || s == 0x10 || s == 0x11 || s == 0x12) return 32;
		return 16;
	}

	// Wie viele Cubes DIESES Materials sitzen schon auf der Waffe?
	// Gezaehlt werden die nativen (im Item) plus die im Parallel-Store --
	// sonst koennte man das Limit durch Ab-/Anlegen umgehen.
	static int CountMaterial(const uint8_t* it, const Entry* e, uint8_t material) {
		int n = *(const int*)(it + kIt_Count);
		if (n < 0) n = 0;
		if (n > 32) n = 32;
		int total = 0;
		for (int i = 0; i < n; i++)
			if (it[kIt_Adaptions + i * 8 + 3] == material) total++;
		if (e) {
			for (int i = 0; i < e->count; i++)
				if (e->cubes[i * 8 + 3] == material) total++;
		}
		return total;
	}

	static int TotalCubes(const uint8_t* it, const Entry* e) {
		int n = *(const int*)(it + kIt_Count);
		if (n < 0) n = 0;
		if (n > 32) n = 32;
		return n + (e ? e->count : 0);
	}

	static bool Occupied(const uint8_t* it, const Entry* e, uint8_t x, uint8_t y, uint8_t z) {
		int n = *(const int*)(it + kIt_Count);
		if (n < 0) n = 0;
		if (n > 32) n = 32;
		for (int i = 0; i < n; i++) {
			const uint8_t* c = it + kIt_Adaptions + i * 8;
			if (c[0] == x && c[1] == y && c[2] == z) return true;
		}
		if (e) {
			for (int i = 0; i < e->count; i++) {
				const uint8_t* c = e->cubes + i * 8;
				if (c[0] == x && c[1] == y && c[2] == z) return true;
			}
		}
		return false;
	}

	// ==================================================================
	// Hook 1: VoxelWidget::tryPlaceCube @0x588250  (thiscall)
	//
	//   if (!hitValid || !item) { ...; return; }
	//   idx = widget->selectedCube;
	//   if (idx < 0 || idx >= vecSize) { ...Hover-/Preview-Zweig...; return; }
	//   else if (item->adaption_count < 0x20) { addAdaptionCube(...); Cube verbrauchen; }
	//
	// Das "< 0x20" ist das Gate. Wir haengen +0x160 waehrend des Aufrufs auf
	// ein Scratch-Item mit count = 0 um -> das Gate ist offen, das Spiel
	// verbraucht den Cube ganz normal aus dem Inventar, und der eigentliche
	// Eintrag wird von Hook 2 abgefangen.
	// ==================================================================
	// Zwei Varianten: mit callee-cleanup fuer die beiden Stack-Argumente
	// (material, level -- "ret 8") oder ohne (Aufrufer raeumt -- "ret").
	typedef void(__fastcall* Fn_TryPlace2)(void* self, void* edx, uint32_t material, int level);
	typedef void(__fastcall* Fn_TryPlace0)(void* self, void* edx);
	static void* g_origTryPlace = nullptr;
	static bool  g_tryPlaceCleansArgs = false;

	static bool     g_capturedViaHook = false;   // Cube kam ueber den addAdaptionCube-Hook
	static int      g_lastCaptured    = 0;       // in diesem Klick uebernommene Cubes

	// Nur true, wenn der Bench-Hook mit EINDEUTIG bestimmter Konvention steht.
	// Sonst wird das 32er-Gate gar nicht erst geoeffnet.
	static bool g_extraCubesEnabled = false;

	static int g_matFullMaterial = -1;   // >=0: dieses Material hat sein 100er-Limit erreicht

	// ------------------------------------------------------------------
	// Diagnose. Solange nicht bestaetigt ist, dass ueber 32 Cubes ankommen,
	// protokolliert jeder Klick in der Bench, WARUM umgeleitet wurde oder
	// nicht. Nach der Fehlersuche auf false setzen.
	// ------------------------------------------------------------------
	static const bool kVerboseBench = true;
	static int  g_diagCalls = 0;
	static const int kDiagMaxCalls = 40;

	// Grund, warum PrePlace nicht umgeleitet hat (fuer die Diagnosezeile).
	static const char* g_skipReason = "";
	// Werte fuer die Diagnosezeile.
	static int g_dHit = 0, g_dIdx = 0, g_dVecN = 0, g_dNat = 0, g_dCap = 0;
	static int g_dExtra = 0, g_dHover = 0, g_dMat = 0, g_dMatCount = 0;
	static int g_dX = 0, g_dY = 0, g_dZ = 0;

	// ------------------------------------------------------------------
	// Aufnehmen und Verschieben von Extra-Cubes.
	//
	// Nativ laeuft das im Hover-Zweig von tryPlaceCube:
	//   kein Vorrats-Cube gewaehlt + Klick auf einen vorhandenen Cube
	//     -> widget+0x3C8 = 1, +0x3CC/3D0/3D4 = Position,
	//        +0x3D8 = Material, +0x3DC = Level      ("aufgenommen")
	//   naechster Klick, 0x3C8 gesetzt
	//     -> removeHitEntry(alte Pos) + addAdaptionCube(neue Pos)  ("abgelegt")
	//
	// Der Suchlauf geht nur ueber item->adaption_count, findet unsere Extras
	// also nie. Beides wird darum hier nachgebildet: das Aufnehmen in
	// PostPlace (nachdem das Original nichts gefunden hat), das Ablegen ueber
	// dieselbe Umleitung wie beim normalen Setzen.
	static bool g_carryingExtra = false;
	static bool g_evicted = false;   // in diesem Klick wurde ein Platz freigemacht

	// Index des Extra-Cubes an (x,y,z), sonst -1.
	static int FindExtraAt(const Entry* e, uint8_t x, uint8_t y, uint8_t z) {
		if (!e) return -1;
		for (int i = 0; i < e->count; i++) {
			const uint8_t* c = e->cubes + i * 8;
			if (c[0] == x && c[1] == y && c[2] == z) return i;
		}
		return -1;
	}
	static void RemoveExtraAt(Entry* e, int idx) {
		if (!e || idx < 0 || idx >= e->count) return;
		for (int i = idx; i < e->count - 1; i++)
			memcpy(e->cubes + i * 8, e->cubes + (i + 1) * 8, 8);
		e->count--;
	}

	// ------------------------------------------------------------------
	// Platz schaffen statt umleiten.
	//
	// Der Platzierungs-Raycast in drawWeaponCustomization testet das Volumen
	// AUSSCHLIESSLICH gegen die Cubes im Item:
	//     if (0 < *(int*)(*(int*)(this+0x160) + 0x114)) { ... Vergleich ... }
	// Cubes im Parallel-Store sind fuer ihn also Luft -- man kann nicht an
	// ihnen anbauen, und aufnehmen/entfernen geht ebenfalls nicht.
	//
	// Deshalb wird die Ablage umgedreht: das Item behaelt IMMER die neuesten
	// 32 bzw. 16 Cubes, der jeweils aelteste wandert beim Setzen in den Store.
	// Damit laeuft alles, was der Spieler anfasst, wieder komplett nativ --
	// Volumen, Anbauen, Verschieben, Entfernen. Der Store haelt nur noch das,
	// was ohnehin verdeckt darunter liegt, und wird von uns gezeichnet.
	static bool EvictOldestNative(uint8_t* item, Entry* e) {
		int n = *(int*)(item + kIt_Count);
		if (n <= 0 || !e || e->count >= kMaxCubesTotal) return false;
		memcpy(e->cubes + e->count * 8, item + kIt_Adaptions, 8);   // Index 0 = aeltester
		e->count++;
		for (int i = 0; i < n - 1; i++)
			memcpy(item + kIt_Adaptions + i * 8, item + kIt_Adaptions + (i + 1) * 8, 8);
		memset(item + kIt_Adaptions + (n - 1) * 8, 0, 8);
		*(int*)(item + kIt_Count) = n - 1;
		MarkStoreDirty();
		return true;
	}

	// Gegenstueck zum Verdraengen: sobald im Item wieder Platz ist, wandern
	// die zuletzt verdraengten Cubes zurueck -- so lange, bis das Item erneut
	// voll ist oder der Store leer.
	//
	// Gebraucht wird das, weil Loeschen im Spiel so laeuft: Cube aufnehmen
	// (widget+0x3C8 = 1), Cursor vom Modell wegziehen, dann greift in
	// tryPlaceCube
	//     if ((block == 0) && (*(char*)(self + 0x3c8) != '\0')) {
	//         GameState_removeHitEntry(self+0x3CC, +0x3D0, +0x3D4);
	//         *(u8*)(self + 0x3c8) = 0;
	//     }
	// und adaption_count sinkt. Ohne Nachladen bliebe die Luecke bestehen und
	// die alten Cubes waeren dauerhaft im Store gefangen.
	//
	// Reihenfolge: EvictOldestNative haengt den jeweils aeltesten hinten an,
	// hier wird also vom Ende genommen und vorne wieder eingefuegt -- das ist
	// exakt die Umkehrung, die urspruengliche Anordnung bleibt erhalten.
	static int RefillFromStore(uint8_t* item, Entry* e) {
		int moved = 0;
		if (!e) return 0;
		int cap = NativeCap(item);
		if (cap > 32) cap = 32;
		while (e->count > 0) {
			int n = *(int*)(item + kIt_Count);
			if (n < 0) n = 0;
			if (n >= cap) break;
			for (int i = n; i > 0; i--)
				memcpy(item + kIt_Adaptions + i * 8, item + kIt_Adaptions + (i - 1) * 8, 8);
			memcpy(item + kIt_Adaptions, e->cubes + (e->count - 1) * 8, 8);
			e->count--;
			*(int*)(item + kIt_Count) = n + 1;
			moved++;
		}
		if (moved) MarkStoreDirty();
		return moved;
	}

	static void RefillBenchItem(void* widget) {
		if (!g_extraCubesEnabled || g_carryingExtra) return;   // nicht waehrend des Tragens
		__try {
			uint8_t* w = (uint8_t*)widget;
			if (*(uint8_t*)(w + kW_PickActive) != 0) return;   // Spiel traegt einen Cube
			uint8_t* item = *(uint8_t**)(w + kW_Item);
			if (!item || item[kIt_Category] != 0x03) return;
			if (*(int*)(item + kIt_Count) >= NativeCap(item)) return;   // schon voll
			Entry* e = FindEntry(ItemKey(item));
			if (!e || e->count <= 0) return;
			RefillFromStore(item, e);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	static void PrePlace(void* self) {
		g_matFullMaterial = -1;
		g_capturedViaHook = false;
		g_lastCaptured = 0;
		g_skipReason = "";
		if (!g_extraCubesEnabled) { g_skipReason = "Hook inaktiv"; return; }
		__try {
			uint8_t* w = (uint8_t*)self;
			uint8_t* item = *(uint8_t**)(w + kW_Item);
			if (!item) { g_skipReason = "kein Item an widget+0x160"; return; }

			// Diagnosewerte einsammeln, BEVOR irgendein Zweig aussteigt.
			g_dHit   = *(uint8_t*)(w + kW_HitValid);
			g_dIdx   = *(int*)(w + kW_SelCube);
			int vecBytes = *(int*)(w + kW_CubeVecEnd) - *(int*)(w + kW_CubeVecBeg);
			g_dVecN  = (vecBytes > 0) ? (vecBytes / kW_VecStride) : 0;
			g_dNat   = *(int*)(item + kIt_Count);
			g_dCap   = NativeCap(item);
			g_dHover = *(int*)(w + kW_HoverCube);
			g_dX = *(int*)(w + kW_HitX); g_dY = *(int*)(w + kW_HitY); g_dZ = *(int*)(w + kW_HitZ);
			g_dMat = -1; g_dMatCount = -1;

			uint64_t key = ItemKey(item);
			Entry* e = FindOrCreateEntry(key);
			g_dExtra = e ? e->count : -1;

			if (g_dHit == 0) { g_skipReason = "kein Trefferpunkt (widget+0x2A8 == 0)"; return; }

			// Wird ueberhaupt gesetzt? Entweder ein Cube aus dem Vorrat, oder ein
			// zuvor aufgenommener wird abgelegt.
			bool carrying = (*(uint8_t*)(w + kW_PickActive) != 0);
			bool fromSupply = (g_dIdx >= 0 && g_dIdx < g_dVecN);
			if (!carrying && !fromSupply) { g_skipReason = "kein Cube gewaehlt oder in der Hand"; return; }

			if (g_dNat < g_dCap) { g_skipReason = "nativ ist noch Platz -- Original macht es selbst"; return; }
			if (!e) { g_skipReason = "Store voll (mehr als 64 Waffen)"; return; }
			if (TotalCubes(item, e) >= kMaxCubesTotal) { g_skipReason = "Gesamtbudget erreicht"; return; }

			// Material-Grenze nur beim Setzen aus dem Vorrat; ein bereits
			// gesetzter Cube, der nur verschoben wird, zaehlt nicht neu.
			if (fromSupply) {
				uint8_t* elem = (uint8_t*)(*(int*)(w + kW_CubeVecBeg)) + (size_t)g_dIdx * kW_VecStride;
				uint8_t mat = elem[4 + kIt_Material];
				g_dMat = mat;
				g_dMatCount = CountMaterial(item, e, mat);
				if (g_dMatCount >= kMaxPerMaterial) {
					g_matFullMaterial = mat;
					g_skipReason = "Grenze dieser Sorte erreicht";
					return;
				}
			}

			uint8_t hx = (uint8_t)g_dX, hy = (uint8_t)g_dY, hz = (uint8_t)g_dZ;
			if (Occupied(item, e, hx, hy, hz)) { g_skipReason = "Zielvoxel schon belegt"; return; }

			// Einen Platz freimachen -- danach setzt das Original ganz normal.
			if (!EvictOldestNative(item, e)) { g_skipReason = "Verdraengen fehlgeschlagen"; return; }
			g_evicted = true;
			g_skipReason = "Platz geschaffen, Original setzt nativ";
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_skipReason = "SEH in PrePlace"; }
	}

	// Nach dem Original: falls das Spiel im Hover-Zweig nichts gefunden hat
	// (0x3C8 immer noch 0), pruefen ob an der Stelle ein EXTRA-Cube liegt --
	// und ihn dann genauso "in die Hand nehmen" wie das Spiel es mit nativen
	// Cubes tut.
	static void TryPickUpExtra(void* self) {
		if (!g_extraCubesEnabled || g_carryingExtra) return;
		__try {
			uint8_t* w = (uint8_t*)self;
			if (*(uint8_t*)(w + kW_PickActive) != 0) return;   // Spiel hat schon einen
			if (*(uint8_t*)(w + kW_HitValid) == 0) return;
			int idx = *(int*)(w + kW_SelCube);
			int vecBytes = *(int*)(w + kW_CubeVecEnd) - *(int*)(w + kW_CubeVecBeg);
			int n = (vecBytes > 0) ? (vecBytes / kW_VecStride) : 0;
			if (idx >= 0 && idx < n) return;                   // Vorrats-Cube gewaehlt -> Setzen
			uint8_t* item = *(uint8_t**)(w + kW_Item);
			if (!item) return;
			Entry* e = FindEntry(ItemKey(item));
			if (!e || e->count <= 0) return;

			uint8_t hx = (uint8_t)*(int*)(w + kW_HitX);
			uint8_t hy = (uint8_t)*(int*)(w + kW_HitY);
			uint8_t hz = (uint8_t)*(int*)(w + kW_HitZ);
			int found = FindExtraAt(e, hx, hy, hz);
			if (found < 0) return;

			const uint8_t* c = e->cubes + found * 8;
			*(int*)(w + kW_PickX)   = (int8_t)c[0];
			*(int*)(w + kW_PickY)   = (int8_t)c[1];
			*(int*)(w + kW_PickZ)   = (int8_t)c[2];
			*(int*)(w + kW_PickMat) = c[3];
			*(int*)(w + kW_PickLvl) = *(const uint16_t*)(c + 4);
			*(uint8_t*)(w + kW_PickActive) = 1;
			RemoveExtraAt(e, found);
			g_carryingExtra = true;
			MarkStoreDirty();
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// Nach dem Original: hat es wirklich gesetzt? Der Zaehler muss wieder auf
	// dem Cap stehen. Wenn nicht, den freigemachten Platz zurueckgeben, damit
	// kein Cube im Store verschwindet.
	static int PostPlace() {
		if (!g_evicted) return 0;
		g_evicted = false;
		int placed = 0;
		__try {
			uint8_t* w = (uint8_t*)g_benchWidget;
			uint8_t* item = w ? *(uint8_t**)(w + kW_Item) : nullptr;
			if (!item) return 0;
			int nat = *(int*)(item + kIt_Count);
			if (nat >= NativeCap(item)) {
				placed = 1;                     // Cube sitzt nativ, Verdraengung war korrekt
				g_carryingExtra = false;
				MarkStoreDirty();
			} else {
				// Nichts gesetzt -> den verdraengten Cube zurueckholen.
				Entry* e = FindEntry(ItemKey(item));
				if (e && e->count > 0 && nat < 32) {
					for (int i = nat; i > 0; i--)
						memcpy(item + kIt_Adaptions + i * 8, item + kIt_Adaptions + (i - 1) * 8, 8);
					memcpy(item + kIt_Adaptions, e->cubes + (e->count - 1) * 8, 8);
					e->count--;
					*(int*)(item + kIt_Count) = nat + 1;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		g_lastCaptured = placed;
		return placed;
	}

	static bool g_captureMissWarned = false;

	static void DiagLine(bool placed, int captured) {
		if (!kVerboseBench || g_diagCalls > kDiagMaxCalls) return;
		std::cout << "[GemForge][Diag " << g_diagCalls << "] "
			<< (placed ? "PLATZ GESCHAFFEN" : "uebersprungen")
			<< " | hit=" << g_dHit
			<< " sel=" << g_dIdx << "/" << g_dVecN
			<< " nativ=" << g_dNat << "/" << g_dCap
			<< " extra=" << g_dExtra
			<< " hover(0x2AC)=" << g_dHover
			<< " ziel=(" << g_dX << "," << g_dY << "," << g_dZ << ")"
			<< " mat=0x" << std::hex << (g_dMat < 0 ? 0 : g_dMat) << std::dec
			<< " davon=" << g_dMatCount;
		if (placed) std::cout << " | nativ gesetzt: " << captured;
		else        std::cout << " | Grund: " << g_skipReason;
		std::cout << std::endl;
		if (g_diagCalls == kDiagMaxCalls)
			std::cout << "[GemForge][Diag] weitere Zeilen werden unterdrueckt." << std::endl;
	}

	static void ReportPlacement() {
		if (g_matFullMaterial >= 0) {
			std::cout << "[GemForge] Material 0x" << std::hex << g_matFullMaterial << std::dec
				<< " hat sein Limit von " << (int)kMaxPerMaterial
				<< " Cubes erreicht -- andere Sorte waehlen." << std::endl;
			g_matFullMaterial = -1;
			return;
		}
		if (g_lastCaptured > 0) {
			g_lastCaptured = 0;
		} else if (!g_captureMissWarned) {
			g_captureMissWarned = true;
			std::cout << "[GemForge] Hinweis: es wurde ein Platz freigemacht, aber nichts "
				<< "gesetzt -- der verdraengte Cube wurde zurueckgeholt." << std::endl;
		}
	}

	static void __fastcall Hook_TryPlaceCube2(void* self, void* edx, uint32_t material, int level) {
		if (!g_origTryPlace) return;
		PrePlace(self);
		bool did = g_evicted;
		((Fn_TryPlace2)g_origTryPlace)(self, edx, material, level);
		int captured = PostPlace();
		if (!did) TryPickUpExtra(self);      // Aufnehmen erst pruefen, wenn nichts gesetzt wurde
		g_diagCalls++;
		DiagLine(did, captured);
		if (did || g_matFullMaterial >= 0) ReportPlacement();
	}
	static void __fastcall Hook_TryPlaceCube0(void* self, void* edx) {
		if (!g_origTryPlace) return;
		PrePlace(self);
		bool did = g_evicted;
		((Fn_TryPlace0)g_origTryPlace)(self, edx);
		int captured = PostPlace();
		if (!did) TryPickUpExtra(self);      // Aufnehmen erst pruefen, wenn nichts gesetzt wurde
		g_diagCalls++;
		DiagLine(did, captured);
		if (did || g_matFullMaterial >= 0) ReportPlacement();
	}

	// ==================================================================
	// Hook 2: item_renderAdaptionCubes @0x471B60
	//
	//   for (i = 0; i < item->adaption_count; i++) {
	//       m = matrix; m.translate(c.x, c.y, c.z);
	//       material_toColor(col, c.material, tint, glow);
	//       setUniform30(col); uploadModelViewMatrices(m); drawCubeMesh();
	//   }
	//
	// Die Funktion prueft NICHT gegen 32 -- sie laeuft genau count-mal.
	// Wir rufen sie also nach dem Original noch einmal je Batch mit einem
	// eigenen Item-Puffer auf (Kopfdaten der echten Waffe + unsere Cubes).
	//
	// Aufrufkonvention: 6 Stack-Argumente. __stdcall wuerde mit "ret 0x18"
	// enden, __cdecl mit "ret". Wird zur Laufzeit anhand der Funktionsbytes
	// entschieden, statt geraten -- ein Fehlgriff hier zerlegt den Stack.
	// ==================================================================
	// ------------------------------------------------------------------
	// ACHTUNG -- Ursache des Absturzes 0xC0000005 bei 0x448FFA:
	//
	// 0x471B60 ist eine METHODE des Renderer-Objekts: ECX (this) muss beim
	// Eintritt schon stimmen. Die Funktion setzt ECX fuer ihre eigenen
	// Aufrufe NICHT selbst, sondern erbt es vom Aufrufer. Beleg:
	//
	//   void render_setVec3Uniform58(undefined4 value) {   // @0x448FE0
	//       int self;                                       // <- freies Register (ECX)
	//       (**(code**)(**(int**)(self + 0x1c) + 0x1b4))(...);
	//   }
	//
	// Bei `self == 0` liest das genau an 0x1C -- exakt die gemeldete Adresse.
	// Ein normaler C-Detour (__cdecl/__stdcall) verwirft ECX/EDX, bevor das
	// Original laeuft. Deshalb: __fastcall-Detour, dann sind ECX und EDX
	// ganz normale Parameter und werden unveraendert weitergereicht.
	//
	// Signatur: __fastcall(ecx, edx, item, matrix, p3, p4, p5, p6)
	//   -> ECX/EDX in Registern, 6 Argumente auf dem Stack, "ret 0x18".
	//   Das passt zur Analyse (6 Aufrufstellen, keine mit "add esp,24",
	//   Rumpf endet auf "ret 24") -- also Callee-Cleanup von 24 Byte.
	// ------------------------------------------------------------------
	typedef void(__fastcall* Fn_RenderT)(void* ecx, void* edx, void* item, float* mat,
										 uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6);
	static void* g_origRenderRaw = nullptr;
	static bool  g_renderReady = false;

	static void CallOrigRender(void* ecx, void* edx, void* item, float* mat,
							   uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) {
		if (!g_origRenderRaw) return;
		((Fn_RenderT)g_origRenderRaw)(ecx, edx, item, mat, p3, p4, p5, p6);
	}

	static void RenderExtras(void* ecx, void* edx, uint8_t* it, float* mat,
							 uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) {
		__try {
			if (!it || it[kIt_Category] != 0x03) return;
			Entry* e = FindEntry(ItemKey(it));
			if (!e || e->count <= 0) return;
			uint8_t fake[kIt_Size + 8];
			memcpy(fake, it, kIt_Size);
			int off = 0;
			while (off < e->count) {
				int n = e->count - off;
				if (n > 32) n = 32;
				memcpy(fake + kIt_Adaptions, e->cubes + off * 8, (size_t)n * 8);
				*(int*)(fake + kIt_Count) = n;
				// Gleiches ECX/EDX wie das Original -> gleicher Renderer-Zustand.
				CallOrigRender(ecx, edx, fake, mat, p3, p4, p5, p6);
				off += n;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// Farb-Override nur waehrend des Zeichnens von Adaptions-Cubes.
	//
	// material_toColor ist NICHT nur fuer Cubes zustaendig -- sie faerbt auch
	// GameController_avgEquippedColor @0x4C6CC0 (GameController.cpp:55825)
	// und die Treffer-Funken in CombatBehavior::spawnHitParticles
	// (game_misc.cpp:18534). Ein globales Umfaerben der Zutaten-Materialien
	// 0x0D..0x10 schlaegt dort ebenfalls durch -- das waren die
	// "komischen Sachen" ausserhalb der Bench.
	static bool g_inCubeRender = false;

	// ECX/EDX, mit denen das SPIEL 0x471B60 aufruft. Fuer die Bench-Vorschau
	// unverzichtbar -- siehe die lange Begruendung bei InjectBenchExtras.
	static void* g_renderEcx = nullptr;
	static void* g_renderEdx = nullptr;

	static void __fastcall Hook_Render(void* ecx, void* edx, void* item, float* mat,
									   uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) {
		g_renderEcx = ecx;      // nur echte Spielaufrufe landen hier -- unsere
		g_renderEdx = edx;      // eigene Aufrufe gehen an die Trampoline.
		bool outer = !g_inCubeRender;
		g_inCubeRender = true;                  // Farb-Hook nur hier drin aktiv
		CallOrigRender(ecx, edx, item, mat, p3, p4, p5, p6);
		RenderExtras(ecx, edx, (uint8_t*)item, mat, p3, p4, p5, p6);
		if (outer) g_inCubeRender = false;
	}

	// ==================================================================
	// Hook: render_setUniform30 @0x448280   void __thiscall(self, value)
	//
	// Nur zum MITSCHNEIDEN der zuletzt gesetzten Farbe. Hintergrund: die
	// Cube-Schleife der Bench arbeitet je Cube in dieser Reihenfolge
	//     material_toColor -> setUniform30 -> uploadModelViewMatrices -> draw_mesh
	// Unsere Injektion haengt im Upload-Hook, also NACH setUniform30 und VOR
	// draw_mesh. Sie ueberschreibt damit das Farb-Uniform, und der unmittelbar
	// danach gezeichnete Spiel-Cube bekommt die Farbe unseres letzten
	// Extra-Cubes -- genau das Bild "die Cubes aendern ihre Art".
	// Mit dem Mitschnitt laesst sich die Farbe nach der Injektion exakt
	// wiederherstellen.
	// ==================================================================
	typedef void(__fastcall* Fn_SetU30)(void* ecx, void* edx, uint32_t value);
	static Fn_SetU30 g_origSetU30 = nullptr;
	static void*     g_u30Ecx = nullptr;
	static uint32_t  g_u30Val = 0;

	static void __fastcall Hook_SetU30(void* ecx, void* edx, uint32_t value) {
		g_u30Ecx = ecx; g_u30Val = value;
		if (g_origSetU30) g_origSetU30(ecx, edx, value);
	}

	// ==================================================================
	// Hook: Item_get_stack_or_grid_size @0x4C7660   int __thiscall(Item*)
	//
	// Einziger Aufrufer im ganzen Binary ist die Beschriftung der Bench:
	//     cap   = Item_get_stack_or_grid_size(item);        // 0x4C7660
	//     count = *(int*)(item + 0x114);
	//     ... L"Upgrades " << count << "/" << cap
	//
	// Beide Werte kennen unsere Extras nicht. Der Hook liefert deshalb das
	// Gesamtbudget als Obergrenze und hebt adaption_count kurz auf
	// nativ+extra an -- zurueckgesetzt wird in OnBenchDrawEnd, also noch im
	// selben Frame. Dazwischen laeuft nur die Textausgabe.
	// ==================================================================
	static void* g_origStackSize = nullptr;
	static uint8_t*     g_countBumpItem = nullptr;
	static int          g_countBumpSaved = 0;

	static void RestoreCountBump() {
		if (!g_countBumpItem) return;
		__try { *(int*)(g_countBumpItem + kIt_Count) = g_countBumpSaved; }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		g_countBumpItem = nullptr;
	}

	static uint32_t StackSizeCommon(void* item, uint32_t cap);

	// ohne Stack-Argument (thiscall, "ret")
	static uint32_t __fastcall Hook_StackSize0(void* item, void* edx) {
		typedef uint32_t(__fastcall* Fn0)(void*, void*);
		uint32_t cap = g_origStackSize ? ((Fn0)g_origStackSize)(item, edx) : 16u;
		return StackSizeCommon(item, cap);
	}
	// mit einem Stack-Argument (thiscall, "ret 4")
	static uint32_t __fastcall Hook_StackSize1(void* item, void* edx, uint32_t a1) {
		typedef uint32_t(__fastcall* Fn1)(void*, void*, uint32_t);
		uint32_t cap = g_origStackSize ? ((Fn1)g_origStackSize)(item, edx, a1) : 16u;
		return StackSizeCommon(item, cap);
	}

	static uint32_t StackSizeCommon(void* item, uint32_t cap) {
		if (!g_benchWidget || g_countBumpItem) return cap;   // nur die Bench-Beschriftung
		__try {
			uint8_t* it = (uint8_t*)item;
			if (it[kIt_Category] != 0x03) return cap;
			Entry* e = FindEntry(ItemKey(it));
			if (!e || e->count <= 0) return cap;
			g_countBumpItem  = it;
			g_countBumpSaved = *(int*)(it + kIt_Count);
			*(int*)(it + kIt_Count) = g_countBumpSaved + e->count;
			cap = (uint32_t)kMaxCubesTotal;
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_countBumpItem = nullptr; }
		return cap;
	}

	// ==================================================================
	// Edelstein-Zutaten als zusaetzliche Vorrats-Eintraege der Bench.
	//
	// Die Zutaten bleiben unveraendert im Inventar -- angeboten wird eine
	// KOPIE als Vorrats-Eintrag. Dadurch:
	//   - gesetzt wird das Material des Edelsteins selbst (0x0D..0x10, 0x0B),
	//   - der native Verbrauch findet den echten Inventar-Slot wieder, weil
	//     tryPlaceCube ihn ueber category/item_id/material + item_rarityScaled
	//     mit dem Vorrats-Eintrag vergleicht (identische Bytes -> Treffer),
	//   - jenseits der 32 greift der bereits vorhandene Parallel-Store.
	//
	// Kein Effekt, nur Schaden (ueber adaption_count) und Optik.
	// ==================================================================
	struct Gem { uint8_t material; float r, g, b; const char* name; };
	static const Gem kGems[] = {
		{ 0x0B, 1.00f, 0.78f, 0.25f, "Gold"     },   // 0x0B kennt material_toColor bereits
		{ 0x0D, 0.15f, 0.90f, 0.35f, "Emerald"  },
		{ 0x0E, 0.20f, 0.45f, 1.00f, "Sapphire" },
		{ 0x0F, 1.00f, 0.15f, 0.20f, "Ruby"     },
		{ 0x10, 0.75f, 0.95f, 1.00f, "Diamond"  },
		{ 0x09, 0.35f, 1.00f, 0.85f, "Schimmerpilz" },   // nur fuer Lampen (Farbe der gesetzten Cubes)
	};
	static const int kGemCount = (int)(sizeof(kGems) / sizeof(kGems[0]));

	static const Gem* FindGem(int material) {
		for (int i = 0; i < kGemCount; i++) if (kGems[i].material == material) return &kGems[i];
		return nullptr;
	}

	// --- eigener Vorrats-Puffer -----------------------------------------
	// Der Spiel-Vektor waechst nur ueber den Spiel-Allokator. Damit wir
	// anhaengen koennen, ohne je eine Reallokation auszuloesen, bekommt der
	// Vektor waehrend geoeffneter Bench unseren Puffer mit grosszuegiger
	// Kapazitaet. Beim Schliessen wird das Original zurueckgeschrieben --
	// sonst wuerde der Widget-Destruktor operator_delete auf unseren
	// statischen Puffer aufrufen.
	static const int kSupplyCap = 64;
	static uint8_t   g_supplyBuf[kSupplyCap * kW_VecStride];
	static bool      g_supplySwapped = false;
	static uint32_t  g_savedBegin = 0, g_savedEnd = 0, g_savedCap = 0;
	static void*     g_swapWidget = nullptr;
	static DWORD     g_lastBenchTick = 0;
	static bool      g_gemsAppended = false;
	static bool      g_gemSupplyOn = true;
	static bool      g_gemLogged = false;

	static void InstallSupplyBuffer(void* widget) {
		if (g_supplySwapped || !g_gemSupplyOn) return;
		__try {
			uint8_t* w = (uint8_t*)widget;
			g_savedBegin = *(uint32_t*)(w + kW_CubeVecBeg);
			g_savedEnd   = *(uint32_t*)(w + kW_CubeVecEnd);
			g_savedCap   = *(uint32_t*)(w + 0x178);
			uint32_t base = (uint32_t)(uintptr_t)g_supplyBuf;
			*(uint32_t*)(w + kW_CubeVecBeg) = base;
			*(uint32_t*)(w + kW_CubeVecEnd) = base;
			*(uint32_t*)(w + 0x178)         = base + (uint32_t)(kSupplyCap * kW_VecStride);
			g_swapWidget = widget;
			g_supplySwapped = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_supplySwapped = false; }
	}

	static void RestoreSupplyBuffer() {
		if (!g_supplySwapped) return;
		__try {
			uint8_t* w = (uint8_t*)g_swapWidget;
			*(uint32_t*)(w + kW_CubeVecBeg) = g_savedBegin;
			*(uint32_t*)(w + kW_CubeVecEnd) = g_savedEnd;
			*(uint32_t*)(w + 0x178)         = g_savedCap;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		g_supplySwapped = false;
		g_swapWidget = nullptr;
	}

	// Haengt je Edelstein-Sorte einen Vorrats-Eintrag an (Kopie des echten
	// Inventar-Items). Wird einmal je Bench-Frame aufgerufen, nachdem das
	// Spiel den Vorrat gefuellt hat.
	static int AppendGemEntries(void* widget) {
		int added = 0;
		__try {
			if (!g_supplySwapped || !g_gemSupplyOn) return 0;
			uint8_t* w = (uint8_t*)widget;
			uint32_t end = *(uint32_t*)(w + kW_CubeVecEnd);
			uint32_t cap = *(uint32_t*)(w + 0x178);
			if (!g_gc || !g_gc->local_player) return 0;
			uint8_t* pl = (uint8_t*)g_gc->local_player;
			uint32_t oBeg = *(uint32_t*)(pl + 0x11DC);
			uint32_t oEnd = *(uint32_t*)(pl + 0x11E0);
			if (!oBeg || oEnd <= oBeg) return 0;
			// Lampe (Kategorie 0x18, 2026-10-19): statt Material-/Geisterwuerfeln nur native Schimmerpilze
			// (0x0B / 0x13, Material 9 -- Hoehlenfund, auch Trankzutat). Jeder gesetzte Pilz vergroessert den Lichtradius.
			const uint8_t* bound = *(const uint8_t**)(w + kW_Item);
			const bool lamp = bound && bound[kIt_Category] == 0x18;
			if (lamp) { end = *(uint32_t*)(w + kW_CubeVecBeg); }
			int cats = (int)((oEnd - oBeg) / 0x0C);
			for (int b = 0; b < cats && b < 64; b++) {
				uint8_t* bucket = (uint8_t*)(uintptr_t)(oBeg + (uint32_t)b * 0x0C);
				uint32_t sBeg = *(uint32_t*)(bucket + 0), sEnd = *(uint32_t*)(bucket + 4);
				if (!sBeg || sEnd <= sBeg) continue;
				int slots = (int)((sEnd - sBeg) / kW_VecStride);
				for (int s = 0; s < slots; s++) {
					if (end + (uint32_t)kW_VecStride > cap) return added;   // niemals reallozieren
					uint8_t* slot = (uint8_t*)(uintptr_t)(sBeg + (uint32_t)s * kW_VecStride);
					int cnt = *(int*)slot;
					uint8_t* it = slot + 4;
					if (cnt <= 0) continue;
					if (lamp) { if (it[kIt_Category] != 0x0B || it[kIt_ItemId] != 0x13) continue; }
					else {
						if (it[kIt_Category] != 0x0B || it[kIt_ItemId] != 0x00) continue;
						if (!FindGem(it[kIt_Material]) || it[kIt_Material] == 0x09) continue;
					}
					memcpy((void*)(uintptr_t)end, slot, kW_VecStride);      // 1:1-Kopie
					end += (uint32_t)kW_VecStride;
					added++;
				}
			}
			*(uint32_t*)(w + kW_CubeVecEnd) = end;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return added;
	}

	// ------------------------------------------------------------------
	// Warum die angehaengten Edelsteine sonst nicht auswaehlbar sind.
	//
	// drawWeaponCustomization klemmt die Auswahl direkt nach dem Fuellen
	// des Vorrats ab -- also BEVOR wir anhaengen:
	//
	//   if ((*(int*)(this+0x174) - *(int*)(this+0x170)) / 0x11C <= this->sel)
	//       this->sel = (...)/0x11C - 1;          // = letzter NATIVER Eintrag
	//
	// Klickt man einen Edelstein an, setzt ToggleSelection @0x58CE40 zwar
	// this->sel korrekt -- im naechsten Frame steht der Index aber wieder
	// ausserhalb des dann noch nativen Vorrats und wird zurueckgeklemmt.
	// Das sieht exakt so aus, als liesse sich der Stein nicht anwaehlen.
	//
	// Loesung: den Klick-Wunsch beim Klick mitschreiben (ToggleSelection ist
	// winzig, thiscall, ohne Argumente -> "ret", risikolos zu hooken) und ihn
	// NACH dem Anhaengen wieder eintragen.
	// ------------------------------------------------------------------
	typedef void(__fastcall* Fn_ToggleSel)(void* ecx, void* edx);
	static Fn_ToggleSel g_origToggleSel = nullptr;
	static int g_desiredSel = -1;

	static void __fastcall Hook_ToggleSel(void* ecx, void* edx) {
		if (g_origToggleSel) g_origToggleSel(ecx, edx);
		__try {
			g_desiredSel = *(int*)((uint8_t*)ecx + kW_SelCube);
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_desiredSel = -1; }
	}

	static void RestoreSelection(void* widget) {
		if (g_desiredSel < 0) return;
		__try {
			uint8_t* w = (uint8_t*)widget;
			int bytes = *(int*)(w + kW_CubeVecEnd) - *(int*)(w + kW_CubeVecBeg);
			int total = (bytes > 0) ? (bytes / kW_VecStride) : 0;
			if (g_desiredSel < total) *(int*)(w + kW_SelCube) = g_desiredSel;
			else                      g_desiredSel = -1;   // Eintrag ist weg (aufgebraucht)
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	void ToggleGemSupply() {
		g_gemSupplyOn = !g_gemSupplyOn;
		if (!g_gemSupplyOn) RestoreSupplyBuffer();
		std::cout << "[GemForge] Edelstein-Angebot in der Bench: "
			<< (g_gemSupplyOn ? "AN" : "AUS") << std::endl;
	}

	// ==================================================================
	// Extra-Cubes auch in der BENCH-VORSCHAU zeichnen.
	//
	// Problem: drawWeaponCustomization (0x588500) benutzt NICHT 0x471B60,
	// sondern zeichnet die Cubes in einer eigenen Schleife:
	//
	//   for (i = 0; i < widget->item->adaption_count; i++) {
	//       c = widget->item + 0x14 + i*8;
	//       m = basis;                                  // 16 floats kopiert
	//       m[12+j] += c.x*m[0+j] + c.y*m[4+j] + c.z*m[8+j];   // Zeile 3 = Translation
	//       material_toColor(col, c.material, {1,1,1,1}, 0);
	//       render_setUniform30(col);
	//       render_uploadModelViewMatrices(m, projA, projB);   // 0x4482A0
	//       GameController_draw_mesh();
	//   }
	//
	// Der Zaehler sitzt fest bei +0x114, direkt hinter Eintrag 31 -- ein
	// groesserer Puffer ist strukturell unmoeglich (siehe RE-Notizen 1.6).
	// Die Schleife kann also nie mehr als 32 zeichnen.
	//
	// Loesung: 0x4482A0 mitlesen. Der Aufruf liefert alles, was 0x471B60
	// braucht -- ECX (Renderer-this, gleiche Klasse wie setUniform30), die
	// Modellmatrix und beide Projektions-Parameter. Aus der Matrix eines
	// bekannten Cubes laesst sich die BASISMATRIX exakt zurueckrechnen:
	//
	//   basis[12+j] = m[12+j] - (x*m[0+j] + y*m[4+j] + z*m[8+j])
	//
	// Damit wird 0x471B60 mit unseren Extra-Cubes und der Basismatrix
	// aufgerufen -- identische Rechnung wie im Original, also exakt dieselbe
	// Position wie in der Welt.
	//
	// Erkennung der Cube-Schleife ohne weiteren Hook: zwei aufeinander
	// folgende Aufrufe, deren Matrixzeilen 0..2 identisch sind und deren
	// Translationsdifferenz genau der Differenz von Cube 0 zu Cube 1
	// entspricht. Das ist selbstpruefend -- eine zufaellige Uebereinstimmung
	// muesste alle vier Komponenten treffen.
	// ==================================================================
	typedef void(__fastcall* Fn_Upload)(void* ecx, void* edx, float* mat, uint32_t a, uint32_t b);
	static Fn_Upload g_origUpload = nullptr;

	static bool   g_benchInjected = false;    // in diesem Durchlauf schon gezeichnet
	static bool   g_injecting     = false;    // Reentranz-Sperre (0x471B60 ruft 0x4482A0)
	static bool   g_previewLogged = false;

	// --- Erkennung der Cube-Schleife ----------------------------------
	// Entscheidender Befund: drawWeaponCustomization ruft
	// render_uploadModelViewMatrices an DREI Stellen auf, und die erste
	// liefert die Basismatrix unveraendert:
	//
	//   80531:  render_uploadModelViewMatrices(afStack_490, ...)   // Waffenmodell
	//   80583:  render_uploadModelViewMatrices(&fStack_558, ...)   // Vorschau-Cube
	//   80697:  render_uploadModelViewMatrices(&fStack_558, ...)   // je vorhandener Cube
	//
	// afStack_490 ist genau die Matrix, aus der die Cube-Schleife ihre
	// Kopien zieht. Die Basis muss also gar nicht zurueckgerechnet werden --
	// sie ist der ERSTE Aufruf einer Kette mit gleicher Basis.
	//
	// Der frueher benutzte Ansatz "Aufruf k gehoert zu Cube k" konnte nicht
	// funktionieren: vor dem ersten Cube stehen bereits ein bis zwei Aufrufe
	// (Modell, optionaler Vorschau-Cube), die Zuordnung war also um 1..2
	// verschoben -- und genau diese Verschiebung deckten die Kandidaten
	// k und k+1 nicht ab.
	//
	// Bestaetigt wird jetzt umgekehrt: passt ein spaeterer Aufruf der Kette
	// zu "Basis verschoben um Cube i" fuer irgendein i, zaehlt das als Beleg.
	// Ab drei Belegen gilt die Basis als sicher.
	// Umgesetzt als KANDIDATENLISTE, nicht als Kette: eine Kette bricht,
	// sobald ein Cube hervorgehoben wird (Basis x1.1), und dabei ginge der
	// bereits gefundene Basis-Kandidat verloren. Die Kandidaten leben deshalb
	// ueber den ganzen Bench-Frame.
	static const int kMaxCands = 8;
	struct BaseCand { float m[16]; int hits; };
	static BaseCand g_cands[kMaxCands];
	static int      g_candN = 0;
	// Diagnose fuer den Fall, dass es doch nicht greift
	static int    g_dbgCalls = 0, g_dbgBestHits = 0;
	static bool   g_dbgLogged = false;

	static void ResetRun() {
		g_candN = 0;
		g_dbgCalls = 0; g_dbgBestHits = 0;
	}

	// Stimmen die Basiszeilen (0..2) zweier Matrizen ueberein?
	static bool SameBasis(const float* a, const float* b) {
		for (int i = 0; i < 12; i++)
			if (fabsf(a[i] - b[i]) > 1e-4f) return false;
		return true;
	}

	// Toleranz relativ zur Groesse -- die Translationsanteile koennen je nach
	// Kameraabstand deutlich groesser als 1 sein.
	static bool Same16(const float* a, const float* b) {
		for (int i = 0; i < 16; i++)
			if (fabsf(a[i] - b[i]) > 1e-3f * (1.0f + fabsf(a[i]))) return false;
		return true;
	}

	// Basis um Cube idx verschieben -- exakt die Rechnung der Bench-Schleife:
	//   m = basis; m[12+j] += x*m[0+j] + y*m[4+j] + z*m[8+j]
	//
	// WICHTIG: die Koordinaten sind SIGNED. Beide Zeichenpfade lesen sie ueber
	// char*:  renderAdaptionCubes  "posX = (float)(int)pGlyph[-1];"
	//         Bench-Schleife       "fVar25 = (float)(int)*pcVar18;"
	// Als uint8_t gelesen wuerde aus z = -1 der Wert 255 -- die Matrix laege
	// dann um ein Vielfaches von 256 daneben.
	static void TranslateBase(const float* base, const uint8_t* item, int idx, float* out) {
		const int8_t* c = (const int8_t*)(item + kIt_Adaptions + idx * 8);
		float x = (float)c[0], y = (float)c[1], z = (float)c[2];
		memcpy(out, base, 16 * sizeof(float));
		for (int j = 0; j < 4; j++)
			out[12 + j] = base[12 + j] + x * base[j] + y * base[4 + j] + z * base[8 + j];
	}

	// Klammern um cube::GameController::drawWeaponCustomization (0x588500).
	// Aufgerufen aus dem bereits vorhandenen Hook in BlacksmithNpc, weil
	// MinHook nur einen Hook je Adresse zulaesst.
	void OnBenchDrawBegin(void* voxelWidget) {
		g_benchWidget   = voxelWidget;
		g_benchInjected = false;
		ResetRun();
		g_gemsAppended  = false;
		g_lastBenchTick = GetTickCount();
		InstallSupplyBuffer(voxelWidget);   // muss VOR dem Original stehen
		RefillBenchItem(voxelWidget);       // Luecken aus dem Store auffuellen
	}

	void OnBenchDrawEnd() {
		// Einmalige Diagnose, falls die Basis nicht bestaetigt werden konnte:
		// zeigt, ob der Hook ueberhaupt greift, wie lang die laengste Kette
		// mit gleicher Basis war und wie viele Belege zusammenkamen.
		if (!g_dbgLogged && g_dbgCalls > 0 && !g_benchInjected) {
			g_dbgLogged = true;
			std::cout << "[GemForge] Bench-Vorschau NICHT bestaetigt: " << g_dbgCalls
				<< " Upload-Aufrufe, " << g_candN << " Basis-Kandidaten"
				<< ", beste Belegzahl " << g_dbgBestHits << " (noetig: 3)." << std::endl;
		}
		RestoreCountBump();                 // Zaehler-Anhebung fuer die Beschriftung zurueck
		g_benchWidget   = nullptr;
		g_lastBenchTick = GetTickCount();
		ResetRun();
	}

	// ACHTUNG bei ECX: 0x471B60 ruft zwei Objekte an, nicht eins.
	//
	//   render_setUniform30 / uploadModelViewMatrices  -> self+0x1c, +0x24 ...
	//        = Renderer/Shader-Objekt
	//   GameController_draw_mesh @0x4E6DF0             -> self+0x34 Vertexbuffer,
	//        +0x38 Indexbuffer, +0x3C/+0x40 Zaehler, +0x58 D3D-Device
	//        = MESH-Objekt
	//
	// Frueher wurde hier das ECX von render_uploadModelViewMatrices
	// weitergereicht, also der RENDERER. draw_mesh liest dann
	// *(int*)(renderer + 0x34), findet 0 und kehrt STILL zurueck:
	//
	//   if (*(int *)(self + 0x34) == 0) { if (*(char *)(self + 0x56) != 0) {
	//       *(undefined1 *)(self + 0x54) = 1; return; } }
	//
	// Kein Absturz, kein Dreieck -- exakt das beobachtete Bild: Injektion
	// meldet Erfolg, sichtbar ist nichts. In der Welt lief es, weil dort das
	// ECX des Spiels unveraendert durchgereicht wird.
	//
	// Deshalb: das vom SPIEL benutzte ECX/EDX verwenden (in Hook_Render
	// mitgeschnitten), nicht das des Matrix-Uploads.
	static void InjectBenchExtras(void* ecx, void* edx, uint8_t* item, Entry* e,
								  float* baseMat, uint32_t a, uint32_t b) {
		static float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (g_renderEcx) { ecx = g_renderEcx; edx = g_renderEdx; }
		g_injecting = true;
		__try {
			uint8_t fake[kIt_Size + 8];
			memcpy(fake, item, kIt_Size);
			int off = 0;
			while (off < e->count) {
				int n = e->count - off;
				if (n > 32) n = 32;
				memcpy(fake + kIt_Adaptions, e->cubes + off * 8, (size_t)n * 8);
				*(int*)(fake + kIt_Count) = n;
				CallOrigRender(ecx, edx, fake, baseMat, a, b,
							   (uint32_t)(uintptr_t)&tint[0], 0u);   // glow = 0.0f
				off += n;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		g_injecting = false;
	}

	static void TryInjectBenchExtras(void* ecx, void* edx, float* mat, uint32_t a, uint32_t b) {
		__try {
			uint8_t* w = (uint8_t*)g_benchWidget;
			uint8_t* item = *(uint8_t**)(w + kW_Item);
			if (!item || item[kIt_Category] != 0x03) return;
			Entry* e = FindEntry(ItemKey(item));
			if (!e || e->count <= 0) return;
			int nat = *(int*)(item + kIt_Count);
			if (nat < 3) return;                  // drei Belege brauchen drei Cubes
			if (nat > 32) nat = 32;
			g_dbgCalls++;

			// Passt dieser Aufruf zu einem bekannten Kandidaten, verschoben um
			// einen der vorhandenen Cubes? SameBasis ist dabei nur ein schneller
			// Vorfilter -- ein hervorgehobener Cube (Basis x1.1) faellt so raus,
			// ohne den Kandidaten zu beschaedigen.
			for (int ci = 0; ci < g_candN; ci++) {
				BaseCand& cand = g_cands[ci];
				if (!SameBasis(cand.m, mat)) continue;
				bool fits = false;
				for (int i = 0; i < nat && !fits; i++) {
					float t[16];
					TranslateBase(cand.m, item, i, t);
					if (Same16(t, mat)) fits = true;
				}
				if (!fits) continue;
				cand.hits++;
				if (cand.hits > g_dbgBestHits) g_dbgBestHits = cand.hits;
				if (cand.hits < 3) return;        // erst ab drei Belegen zeichnen

				float base[16];
				memcpy(base, cand.m, sizeof(base));

				// Zustand des laufenden Spiel-Cubes sichern -- die Injektion
				// ueberschreibt Matrix UND Farbe, und das Spiel zeichnet
				// unmittelbar danach mit beidem weiter.
				void*    savedEcx = g_u30Ecx;
				uint32_t savedCol = g_u30Val;

				InjectBenchExtras(ecx, edx, item, e, base, a, b);
				g_benchInjected = true;

				if (g_origUpload) g_origUpload(ecx, edx, mat, a, b);          // Matrix zurueck
				if (g_origSetU30 && savedCol)
					g_origSetU30(savedEcx, nullptr, savedCol);                // Farbe zurueck

				if (!g_previewLogged) {
					g_previewLogged = true;
					std::cout << "[GemForge] Bench-Vorschau: Basismatrix bestaetigt ("
						<< cand.hits << " Belege) -- " << e->count
						<< " Extra-Cubes werden mitgezeichnet." << std::endl;
					if (!g_renderEcx)
						std::cout << "[GemForge] WARNUNG: kein Mesh-ECX vom Spiel mitgeschnitten "
							<< "(0x471B60 wurde noch nie vom Spiel aufgerufen) -- die Cubes "
							<< "bleiben unsichtbar." << std::endl;
				}
				return;
			}

			// Kein Treffer -> dieser Aufruf koennte selbst die Basis sein.
			// (drawWeaponCustomization zeichnet das Waffenmodell mit genau der
			//  Matrix, aus der die Cube-Schleife ihre Kopien zieht.)
			if (g_candN < kMaxCands) {
				memcpy(g_cands[g_candN].m, mat, 16 * sizeof(float));
				g_cands[g_candN].hits = 0;
				g_candN++;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	static void __fastcall Hook_Upload(void* ecx, void* edx, float* mat, uint32_t a, uint32_t b) {
		if (g_origUpload) g_origUpload(ecx, edx, mat, a, b);
		// Sehr heisse Funktion (jedes Mesh im Spiel) -- ausserhalb der Bench
		// nach wenigen Vergleichen wieder raus.
		if (!g_benchWidget || g_injecting) return;
		// Erster Aufruf im Bench-Frame: liegt garantiert NACH dem Fuellen des
		// Vorrats und VOR der Zutatenliste -> hier die Edelsteine anhaengen.
		if (!g_gemsAppended) {
			g_gemsAppended = true;
			int n = AppendGemEntries(g_benchWidget);
			RestoreSelection(g_benchWidget);   // gegen den Auswahl-Clamp, siehe dort
			if (n > 0 && !g_gemLogged) {
				g_gemLogged = true;
				std::cout << "[GemForge] " << n << " Edelstein-Sorte(n) im Bench-Vorrat "
					<< "ergaenzt (Zutaten bleiben unveraendert)." << std::endl;
			}
		}
		if (g_benchInjected || !mat) return;
		TryInjectBenchExtras(ecx, edx, mat, a, b);
	}

	// ==================================================================
	// Hook: material_toColor @0x4C7250
	//   float* material_toColor(float* out, u32 material, float* src, float level)
	// Vier Stack-Argumente, kein ECX (Ghidra loest alle vier als Stack-
	// Parameter auf). Die Edelstein-Materialien 0x0D..0x10 kennt die
	// Funktion nicht -- sie fallen in den default-Zweig und werden grau.
	// Der Hook faengt genau diese vier ab und schreibt die Steinfarbe.
	// Alles andere geht unveraendert ans Original.
	// ==================================================================
	// Vier Auspraegungen desselben Aufrufs -- welche gilt, sagt die
	// RET-Konstante am Funktionsende (siehe TailRetImmValue).
	typedef float*(__stdcall* Fn_MC_Std )(float*, uint32_t, float*, uint32_t);
	typedef float*(__cdecl*   Fn_MC_C   )(float*, uint32_t, float*, uint32_t);
	typedef float*(__fastcall* Fn_MC_This)(float*, void*, uint32_t, float*, uint32_t);
	typedef float*(__fastcall* Fn_MC_Fast)(float*, uint32_t, float*, uint32_t);
	static void* g_origMatColor = nullptr;
	static int   g_matColorModel = -1;   // = TailRetImmValue
	static const char* g_matColorName = "nicht gesetzt";
	static long  g_colorGateCalls = 0;   // wie oft der Hook ueberhaupt lief
	static long  g_colorGateHits  = 0;   // davon: Edelstein umgefaerbt

	// Faerbt nur die Edelstein-Materialien, und nur waehrend wirklich
	// Adaptions-Cubes gezeichnet werden. material_toColor bedient auch
	// GameController_avgEquippedColor und die Treffer-Funken -- ein globales
	// Umfaerben schlaegt dort durch.
	static bool GemColorGate(float* out, uint32_t m, const float* src) {
		g_colorGateCalls++;
		if (!g_inCubeRender && !g_benchWidget) return false;
		if (!out || !src) return false;
		const Gem* g = FindGem((int)m);
		if (!g || m == 0x0B) return false;      // 0x0B faerbt das Spiel selbst
		g_colorGateHits++;
		out[0] = src[0] * g->r;
		out[1] = src[1] * g->g;
		out[2] = src[2] * g->b;
		out[3] = src[3];
		return true;
	}


	static float* __stdcall Hook_MC_Std(float* out, uint32_t m, float* src, uint32_t lvl) {
		if (GemColorGate(out, m, src)) return out;
		return g_origMatColor ? ((Fn_MC_Std)g_origMatColor)(out, m, src, lvl) : out;
	}
	static float* __cdecl Hook_MC_C(float* out, uint32_t m, float* src, uint32_t lvl) {
		if (GemColorGate(out, m, src)) return out;
		return g_origMatColor ? ((Fn_MC_C)g_origMatColor)(out, m, src, lvl) : out;
	}
	static float* __fastcall Hook_MC_This(float* out, void* edx, uint32_t m, float* src, uint32_t lvl) {
		if (GemColorGate(out, m, src)) return out;
		return g_origMatColor ? ((Fn_MC_This)g_origMatColor)(out, edx, m, src, lvl) : out;
	}
	static float* __fastcall Hook_MC_Fast(float* out, uint32_t m, float* src, uint32_t lvl) {
		if (GemColorGate(out, m, src)) return out;
		return g_origMatColor ? ((Fn_MC_Fast)g_origMatColor)(out, m, src, lvl) : out;
	}


	// ==================================================================
	// Hook 4: item_weaponDamage @0x4C7F60  (thiscall, Rueckgabe FLOAT)
	//
	//   powerBonus = adaption_count * 0.1
	//   dmg = math_pow2Mul(level + powerBonus, rarity) * {2|4|8}
	//   math_pow2Mul(x, r) = 2^(3*(1 - 1/((x-1)*0.05 + 1))) * 2^(r*0.25)
	//
	// Der Waffentyp-Faktor und 2^(r*0.25) sind von x unabhaengig, also ist
	//   faktor = f(level + 0.1*(nativ+extra)) / f(level + 0.1*nativ)
	// mit f(x) = 2^(3*(1 - 1/((x-1)*0.05 + 1))) EXAKT der Wert, den das
	// Spiel selbst berechnet haette. Kein erfundener Multiplikator, und die
	// native Saettigung bei Faktor 8 bleibt erhalten.
	// ==================================================================
	typedef float(__fastcall* Fn_WeaponDamage)(void* item, void* edx);
	static Fn_WeaponDamage g_origWeaponDamage = nullptr;

	static double PowerCurve(double x) {
		double d = (x - 1.0) * 0.05 + 1.0;
		if (d < 1e-6) d = 1e-6;
		return pow(2.0, 3.0 * (1.0 - 1.0 / d));
	}

	static double ExtraDamageScale(const uint8_t* it) {
		double scale = 1.0;
		__try {
			if (!it || it[kIt_Category] != 0x03) return 1.0;
			Entry* e = FindEntry(ItemKey(it));
			if (!e || e->count <= 0) return 1.0;
			double level = (double)*(const int16_t*)(it + kIt_Level);
			double nat = (double)*(const int*)(it + kIt_Count);
			double f0 = PowerCurve(level + 0.1 * nat);
			double f1 = PowerCurve(level + 0.1 * (nat + (double)e->count));
			if (f0 > 1e-9) scale = f1 / f0;
		} __except (EXCEPTION_EXECUTE_HANDLER) { scale = 1.0; }
		return scale;
	}

	static float __fastcall Hook_WeaponDamage(void* item, void* edx) {
		if (!g_origWeaponDamage) return 0.0f;
		float base = g_origWeaponDamage(item, edx);
		double s = ExtraDamageScale((const uint8_t*)item);
		return (float)((double)base * s);
	}

	// ==================================================================
	// Oeffentliche API
	// ==================================================================
	static bool BindImpl(void* gcRaw, int* outNative, int* outCap, int* outExtra, int* outType) {
		bool ok = false;
		__try {
			cube::GameController* gc = (cube::GameController*)gcRaw;
			uint8_t* g = (uint8_t*)gc;
			uint32_t widget = *(uint32_t*)(g + 0x8008F4);
			if (!widget) return false;
			uint8_t* w = (uint8_t*)&gc->local_player->entity_data.equipment.right_weapon;
			if (w[kIt_Category] != 0x03) {
				*(uint32_t*)(widget + kW_Item) = 0;
				return false;
			}
			*(uint32_t*)(widget + kW_Item) = (uint32_t)(uintptr_t)w;
			*(int*)(widget + kW_SelCube)   = -1;
			*(int*)(widget + kW_HoverCube) = -1;
			Entry* e = FindOrCreateEntry(ItemKey(w));
			*outNative = *(int*)(w + kIt_Count);
			*outCap    = NativeCap(w);
			*outExtra  = e ? e->count : 0;
			*outType   = (int)w[kIt_ItemId];
			ok = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	// Schimmerpilz-Cubes auf einem Gegenstand (nativ + Parallel-Store) -- fuer den Lampen-Lichtradius
	int CountCubesOfMaterial(const uint8_t* item, uint8_t material) {
		__try { return CountMaterial(item, FindEntry(ItemKey(item)), material); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	bool BindEquippedWeaponToBench(cube::GameController* gc) {
		if (!gc || !gc->local_player) return false;
		g_diagCalls = 0;             // Diagnosezeilen fuer die neue Sitzung freigeben
		int nat = 0, cap = 0, ext = 0, type = 0;
		if (!BindImpl(gc, &nat, &cap, &ext, &type)) {
			std::cout << "[GemForge] Bench: keine Waffe in der rechten Hand -- nichts gebunden." << std::endl;
			return false;
		}
		std::cout << "[GemForge] Bench an Waffe Typ " << type << " gebunden -- nativ "
			<< nat << "/" << cap << ", extra " << ext << ", gesamt "
			<< (nat + ext) << "/" << (int)kMaxCubesTotal
			<< " (je Material bis " << (int)kMaxPerMaterial << ")." << std::endl;
		return true;
	}

	static int ClearImpl(void* gcRaw) {
		int removed = -1;
		__try {
			cube::GameController* gc = (cube::GameController*)gcRaw;
			uint8_t* w = (uint8_t*)&gc->local_player->entity_data.equipment.right_weapon;
			if (w[kIt_Category] != 0x03) return -1;
			Entry* e = FindEntry(ItemKey(w));
			if (!e) return 0;
			removed = e->count;
			e->count = 0;
			g_storeDirty = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { removed = -1; }
		return removed;
	}

	// ------------------------------------------------------------------
	// Material-Klartext. IDs aus material_toColor @0x4C7250; die Zuordnung
	// der vier Glow-Zweige zu Edelsteinen ist per Bench-Dump BESTAETIGT
	// (Vorrat zeigte 0x82 und 0x83 fuer Sapphire- und Diamond-Cubes).
	// ------------------------------------------------------------------
	const char* MaterialName(int m) {
		switch (m) {
			case 0x01: return "Eisen/Stein (grau)";
			case 0x02: return "Holz";
			case 0x05: return "dunkles Metall";
			case 0x07: return "helles Metall";
			case 0x0B: return "Gold";
			case 0x0C: return "Silber";
			// Ab 0x80 die vier Spirit-Farben. Belegt aus drawWeaponCustomization
			// @0x588500: die Vorrats-Schleife laeuft "for (m = 0x80; m < 0x84; m++)".
			case 0x80: return "SPIRIT rot (Ruby)";
			case 0x81: return "SPIRIT gruen (Emerald)";
			case 0x82: return "SPIRIT blau (Sapphire)";
			case 0x83: return "SPIRIT weiss (Diamond)";
			// Diese Werte tauchen als ZUTATEN-Material auf (Kategorie 0x0B, id 0x00).
			// Als Adaptions-Material kennt material_toColor sie NICHT -> grauer
			// default-Zweig. Genau hier lag der Fehler der ersten GemForge-Fassung.
			case 0x0D: return "Edelstein-Zutat (nicht setzbar)";
			case 0x0E: return "Edelstein-Zutat (nicht setzbar)";
			case 0x0F: return "Edelstein-Zutat (nicht setzbar)";
			case 0x10: return "Edelstein-Zutat (nicht setzbar)";
			default:   return "unbekannt -> grau (default-Zweig)";
		}
	}

	// Cube-Sorte anhand der item_id. Beleg (drawWeaponCustomization @0x588500):
	//   Materialwuerfel-Vorlage: local_378._0_2_ = 0xA0B  -> cat 0x0B, id 0x0A
	//   Spirit-Vorlage:          local_13c[0]   = 0xE0B  -> cat 0x0B, id 0x0E
	//   und beim Vorschau-Cube:  id = (material > 0x7F) ? 0x0A + 4 : 0x0A
	static const char* CubeKind(int cat, int id) {
		if (cat != 0x0B) return "";
		if (id == 0x0A) return "Materialwuerfel (muss zum Waffenmaterial passen)";
		if (id == 0x0E) return "SPIRIT-CUBE (an der Bench setzbar)";
		if (id == 0x00) return "Zutat/Edelstein -- NICHT an der Bench setzbar";
		return "Kategorie 0x0B, unbekannte id";
	}

	// ------------------------------------------------------------------
	// [STRG+U] -- Inventar-Scan.
	// Rohdaten werden SEH-gesichert in ein POD-Array gelesen, ausgegeben
	// wird danach ausserhalb des __try (std::cout hat Destruktoren).
	// ------------------------------------------------------------------
	struct InvSlot { int cat, id, material, rarity, level, count, adaptions; };
	struct InvDump { int ok, categories, shown, truncated; InvSlot s[128]; };

	static void InventoryImpl(void* gcRaw, InvDump* d) {
		memset(d, 0, sizeof(*d));
		__try {
			cube::GameController* gc = (cube::GameController*)gcRaw;
			if (!gc->local_player) return;
			uint8_t* pl = (uint8_t*)gc->local_player;
			uint32_t outerBeg = *(uint32_t*)(pl + 0x11DC);
			uint32_t outerEnd = *(uint32_t*)(pl + 0x11E0);
			if (!outerBeg || outerEnd <= outerBeg) { d->ok = 1; return; }
			d->categories = (int)((outerEnd - outerBeg) / 0x0C);
			for (int b = 0; b < d->categories && b < 64; b++) {
				uint8_t* bucket = (uint8_t*)(uintptr_t)(outerBeg + (uint32_t)b * 0x0C);
				uint32_t sBeg = *(uint32_t*)(bucket + 0);
				uint32_t sEnd = *(uint32_t*)(bucket + 4);
				if (!sBeg || sEnd <= sBeg) continue;
				int slots = (int)((sEnd - sBeg) / 0x11C);
				for (int s = 0; s < slots; s++) {
					if (d->shown >= 128) { d->truncated = 1; break; }
					uint8_t* slot = (uint8_t*)(uintptr_t)(sBeg + (uint32_t)s * 0x11C);
					uint8_t* it = slot + 4;                      // Item beginnt nach der Anzahl
					if (it[kIt_Category] == 0 && it[kIt_ItemId] == 0) continue;  // leerer Slot
					InvSlot& o = d->s[d->shown++];
					o.count     = *(int*)slot;
					o.cat       = it[kIt_Category];
					o.id        = it[kIt_ItemId];
					o.rarity    = it[kIt_Rarity];
					o.material  = it[kIt_Material];
					o.level     = *(int16_t*)(it + kIt_Level);
					o.adaptions = *(int*)(it + kIt_Count);
				}
				if (d->truncated) break;
			}
			d->ok = 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { d->ok = 0; }
	}

	void DumpInventory(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		static InvDump d;                       // 128 * 28 Byte -- nicht auf den Stack
		InventoryImpl(gc, &d);
		if (!d.ok) { std::cout << "[GemForge][Inv] Inventar nicht lesbar (SEH)." << std::endl; return; }
		std::cout << "[GemForge][Inv] Spieler+0x11DC: " << d.categories
			<< " Kategorien, " << d.shown << " belegte Slots"
			<< (d.truncated ? " (gekuerzt)" : "") << std::endl;
		std::cout << "[GemForge][Inv]   kat  id   mat   rar  lvl  anz  cubes  Bedeutung" << std::endl;
		for (int i = 0; i < d.shown; i++) {
			const InvSlot& o = d.s[i];
			char buf[200];
			_snprintf_s(buf, sizeof(buf), _TRUNCATE,
				"  0x%02X 0x%02X 0x%02X  %3d %4d %4d %5d  %s%s%s",
				o.cat, o.id, o.material, o.rarity, o.level, o.count, o.adaptions,
				(o.cat == 0x03) ? "Waffe, Basismaterial " : "",
				(o.cat == 0x0B || o.cat == 0x03) ? MaterialName(o.material) : "",
				(o.cat == 0x0B) ? CubeKind(o.cat, o.id) : "");
			std::cout << "[GemForge][Inv]" << buf << std::endl;
		}
		std::cout << "[GemForge][Inv] Die Bench bietet NUR an: id 0x0A mit dem Basismaterial der "
			<< "Waffe, und id 0x0E (Spirit) mit Material 0x80..0x83 im Levelfenster "
			<< "[Waffenlevel-10 .. Waffenlevel]. Zutaten (id 0x00) tauchen dort nie auf."
			<< std::endl;
	}

	// ------------------------------------------------------------------
	// [STRG+I] -- Bench-Zustand auslesen statt raten.
	// ------------------------------------------------------------------
	struct BenchDump {
		int  ok;
		int  supplyCount, selected, hoverSupply, hitValid, hx, hy, hz;
		int  nativeCount, nativeCap, extras, itemId, weaponMaterial;
		int  supMat[16], supLvl[16], supCnt[16], supShown;
		int  cubeMat[32], cubeShown;
	};

	static void DumpImpl(void* gcRaw, BenchDump* d) {
		memset(d, 0, sizeof(*d));
		__try {
			cube::GameController* gc = (cube::GameController*)gcRaw;
			uint8_t* g = (uint8_t*)gc;
			uint32_t widget = *(uint32_t*)(g + 0x8008F4);
			if (!widget) return;
			uint8_t* w = (uint8_t*)(uintptr_t)widget;
			uint8_t* item = *(uint8_t**)(w + kW_Item);
			if (!item) { d->ok = -1; return; }            // Bench haelt kein Item

			d->itemId         = item[kIt_ItemId];
			d->weaponMaterial = item[kIt_Material];
			d->nativeCount    = *(int*)(item + kIt_Count);
			d->nativeCap      = NativeCap(item);
			Entry* e = FindEntry(ItemKey(item));
			d->extras = e ? e->count : 0;

			d->selected    = *(int*)(w + kW_SelCube);
			d->hoverSupply = *(int*)(w + kW_HoverCube);
			d->hitValid    = *(uint8_t*)(w + kW_HitValid);
			d->hx = *(int*)(w + kW_HitX);
			d->hy = *(int*)(w + kW_HitY);
			d->hz = *(int*)(w + kW_HitZ);

			int vecBytes = *(int*)(w + kW_CubeVecEnd) - *(int*)(w + kW_CubeVecBeg);
			d->supplyCount = (vecBytes > 0) ? (vecBytes / kW_VecStride) : 0;
			uint8_t* beg = (uint8_t*)(uintptr_t)(*(uint32_t*)(w + kW_CubeVecBeg));
			for (int i = 0; i < d->supplyCount && i < 16 && beg; i++) {
				uint8_t* el = beg + (size_t)i * kW_VecStride;
				d->supCnt[i] = *(int*)el;                       // Stueckzahl im Inventar
				d->supMat[i] = el[4 + kIt_Material];
				d->supLvl[i] = *(int16_t*)(el + 4 + kIt_Level);
				d->supShown++;
			}
			int n = d->nativeCount; if (n < 0) n = 0; if (n > 32) n = 32;
			for (int i = 0; i < n; i++) { d->cubeMat[i] = item[kIt_Adaptions + i * 8 + 3]; d->cubeShown++; }
			d->ok = 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { d->ok = 0; }
	}

	void DumpBenchState(cube::GameController* gc) {
		if (!gc) return;
		BenchDump d;
		DumpImpl(gc, &d);
		if (d.ok == 0) { std::cout << "[GemForge][Dump] Bench nicht lesbar (SEH oder kein Widget)." << std::endl; return; }
		if (d.ok < 0)  { std::cout << "[GemForge][Dump] Bench haelt KEIN Item (widget+0x160 == 0) -- "
									  "erst [R] an der Werkbank oder [STRG+B]." << std::endl; return; }
		std::cout << "[GemForge][Dump] Waffe id=" << d.itemId
			<< " material=0x" << std::hex << d.weaponMaterial << std::dec
			<< " | nativ " << d.nativeCount << "/" << d.nativeCap
			<< " extra " << d.extras << std::endl;
		std::cout << "[GemForge][Dump] Auswahl=" << d.selected << "/" << d.supplyCount
			<< " (Zutatenliste-Hover=" << d.hoverSupply << ")"
			<< " Treffer=" << d.hitValid
			<< " Ziel=(" << d.hx << "," << d.hy << "," << d.hz << ")" << std::endl;
		if (d.supShown == 0)
			std::cout << "[GemForge][Dump] Cube-Vorrat ist LEER -- ohne passende Cubes im "
				<< "Inventar kann nichts gesetzt werden." << std::endl;
		for (int i = 0; i < d.supShown; i++)
			std::cout << "[GemForge][Dump]   Vorrat[" << i << "] Material=0x" << std::hex
				<< d.supMat[i] << std::dec << " (" << MaterialName(d.supMat[i]) << ")"
				<< " Level=" << d.supLvl[i]
				<< " Anzahl=" << d.supCnt[i] << (i == d.selected ? "   <-- ausgewaehlt" : "") << std::endl;
		if (d.selected < 0)
			std::cout << "[GemForge][Dump] KEIN Cube ausgewaehlt -- erst einen Eintrag in der "
				<< "Zutatenliste anklicken, sonst setzt tryPlaceCube nichts." << std::endl;
		// Materialien der Waffe als Haeufigkeit statt als lange Zahlenreihe.
		int ids[8], cnt[8], used = 0;
		for (int i = 0; i < d.cubeShown; i++) {
			int k = 0;
			for (; k < used; k++) if (ids[k] == d.cubeMat[i]) { cnt[k]++; break; }
			if (k == used && used < 8) { ids[used] = d.cubeMat[i]; cnt[used] = 1; used++; }
		}
		std::cout << "[GemForge][Dump] Farb-Hook: " << (g_origMatColor ? "gesetzt" : "NICHT gesetzt")
			<< " (" << g_matColorName << ", ret=" << g_matColorModel << ")"
			<< " -- Aufrufe " << g_colorGateCalls << ", davon umgefaerbt " << g_colorGateHits
			<< std::endl;
		std::cout << "[GemForge][Dump] Materialien auf der Waffe:";
		for (int k = 0; k < used; k++)
			std::cout << " 0x" << std::hex << ids[k] << std::dec << " x" << cnt[k]
				<< " (" << MaterialName(ids[k]) << ")";
		std::cout << std::endl;
	}

	void ClearExtras(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		int n = ClearImpl(gc);
		if (n < 0) std::cout << "[GemForge] Keine Waffe angelegt." << std::endl;
		else       std::cout << "[GemForge] " << n << " Extra-Cubes entfernt." << std::endl;
	}

	static bool InfoImpl(void* gcRaw, char* buf, size_t bufLen) {
		bool ok = false;
		__try {
			cube::GameController* gc = (cube::GameController*)gcRaw;
			uint8_t* w = (uint8_t*)&gc->local_player->entity_data.equipment.right_weapon;
			if (w[kIt_Category] != 0x03) return false;
			Entry* e = FindEntry(ItemKey(w));
			int nat = *(int*)(w + kIt_Count);
			int ext = e ? e->count : 0;

			// Materialien NICHT raten, sondern zaehlen was wirklich drauf ist.
			// Die frueheren festen Labels (Ruby/Emerald/...) standen auf
			// Material-IDs, die aus material_toColor (0x4C7250) abgeleitet
			// waren -- die tatsaechlich vergebenen IDs koennen andere sein.
			int ids[8]; int cnt[8]; int used = 0;
			int n = nat; if (n < 0) n = 0; if (n > 32) n = 32;
			for (int i = 0; i < n + ext; i++) {
				int m = (i < n) ? w[kIt_Adaptions + i * 8 + 3]
								: (e ? e->cubes[(i - n) * 8 + 3] : 0);
				int k = 0;
				for (; k < used; k++) if (ids[k] == m) { cnt[k]++; break; }
				if (k == used && used < 8) { ids[used] = m; cnt[used] = 1; used++; }
			}
			char mats[128]; mats[0] = '\0';
			for (int k = 0; k < used; k++) {
				char part[24];
				_snprintf_s(part, sizeof(part), _TRUNCATE, " 0x%02X:%d", ids[k], cnt[k]);
				strncat_s(mats, sizeof(mats), part, _TRUNCATE);
			}
			_snprintf_s(buf, bufLen, _TRUNCATE,
				"Waffe %d | nativ %d/%d + extra %d = %d/%d (je Material max %d) | Materialien:%s",
				(int)w[kIt_ItemId], nat, NativeCap(w), ext, nat + ext,
				(int)kMaxCubesTotal, (int)kMaxPerMaterial, mats);
			ok = true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	bool GetEquippedInfo(cube::GameController* gc, std::string& outText) {
		if (!gc || !gc->local_player) return false;
		char buf[256];
		if (!InfoImpl(gc, buf, sizeof(buf))) return false;
		outText = buf;
		return true;
	}

	// ------------------------------------------------------------------
	class GemMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override {
			g_gc = gc;
			// Bench zu? Dann den eigenen Vorrats-Puffer wieder abhaengen --
			// sonst wuerde der Widget-Destruktor operator_delete darauf
			// aufrufen. drawWeaponCustomization laeuft jeden Frame, solange
			// die Bench offen ist; 300 ms Ruhe heisst also: geschlossen.
			if (g_supplySwapped && (GetTickCount() - g_lastBenchTick) > 300)
				RestoreSupplyBuffer();
			// Geaenderte Cubes hoechstens alle zwei Sekunden auf Platte
			// schreiben -- das Setzen selbst soll nicht auf Datei-IO warten.
			DWORD now = GetTickCount();
			if (g_storeDirty && (now - g_lastSaveTick) > 2000) {
				g_lastSaveTick = now;
				SaveStore();
			}
		}
	};
	static GemMod g_mod;

	static bool MkHook(uint32_t addr, void* detour, void** orig, const char* name) {
		if (MH_CreateHook((void*)(uintptr_t)RebaseAddr(addr), detour, orig) == MH_OK) {
			std::cout << "[GemForge] Hook " << name << " erstellt." << std::endl;
			return true;
		}
		std::cout << "[GemForge] WARN: Hook " << name << " fehlgeschlagen." << std::endl;
		return false;
	}

	void RegisterWithFramework() {
		memset(g_store, 0, sizeof(g_store));
		g_storeCount = 0;
		LoadStore();
		AdventureFramework::RegisterMod(&g_mod);

		// --- 1) Stat-Hook: keine Stack-Argumente, Konvention risikofrei ---
		// thiscall(void) und __fastcall(ecx, edx) enden beide auf "ret".
		if (kEnableDamageHook) {
			MkHook(kAddr_WeaponDamage, (void*)&Hook_WeaponDamage, (void**)&g_origWeaponDamage,
				   "item_weaponDamage (0x4C7F60)");
		}

		// --- 2) Render-Hook: thiscall + 6 Stack-Argumente (0x18 Byte) ---
		// Nur bei Callee-Cleanup einsetzbar -- dafuer passt der __fastcall-Detour
		// exakt (ECX/EDX durchgereicht, "ret 0x18"). Kaeme hier Caller-Cleanup
		// heraus, waere die Annahme falsch und der Hook bleibt aus.
		if (kEnableRenderHook) {
			Convention c = AnalyzeConvention("item_renderAdaptionCubes (0x471B60)",
											 kAddr_RenderCubes, kAddr_RenderCubesEnd, 0x18);
			if (c == CONV_CALLEE_CLEANS) {
				g_renderReady = MkHook(kAddr_RenderCubes, (void*)&Hook_Render,
									   (void**)&g_origRenderRaw,
									   "item_renderAdaptionCubes (0x471B60)");
				std::cout << "[GemForge] Render-Hook: thiscall + 6 Stack-Argumente, "
					<< "ECX/EDX werden durchgereicht." << std::endl;
			} else {
				std::cout << "[GemForge] Render-Hook NICHT gesetzt (Konvention nicht wie erwartet). "
					<< "Die Extra-Cubes bleiben unsichtbar, das Spiel bleibt stabil." << std::endl;
			}
		}

		// --- 3) Bench-Placement: 2 Stack-Argumente (8 Byte) ---
		if (kEnableBenchHook) {
			Convention c = AnalyzeConvention("VoxelWidget::tryPlaceCube (0x588250)",
											 kAddr_TryPlaceCube, kAddr_TryPlaceCubeEnd, 0x08);
			if (c == CONV_UNKNOWN) {
				std::cout << "[GemForge] Bench-Hook NICHT gesetzt: Konvention nicht eindeutig. "
					<< "Es bleibt bei den nativen 32/16 Cubes." << std::endl;
			} else {
				g_tryPlaceCleansArgs = (c == CONV_CALLEE_CLEANS);
				void* d = g_tryPlaceCleansArgs ? (void*)&Hook_TryPlaceCube2 : (void*)&Hook_TryPlaceCube0;
				g_extraCubesEnabled = MkHook(kAddr_TryPlaceCube, d, (void**)&g_origTryPlace,
											 "VoxelWidget::tryPlaceCube (0x588250)");
				std::cout << "[GemForge] tryPlaceCube-Konvention: "
					<< (g_tryPlaceCleansArgs ? "callee raeumt ab (thiscall, 2 Argumente)"
											 : "Aufrufer raeumt ab") << std::endl;
			}
		}

		// --- 3b) Bench-Vorschau: render_uploadModelViewMatrices, 3 Argumente (0x0C) ---
		// Nur sinnvoll, wenn der Render-Hook steht (wir rufen 0x471B60 auf).
		if (kEnablePreviewHook && g_renderReady) {
			Convention c = AnalyzeConvention("render_uploadModelViewMatrices (0x4482A0)",
											 kAddr_UploadMatrices, kAddr_UploadMatricesEnd, 0x0C);
			if (c == CONV_CALLEE_CLEANS) {
				MkHook(kAddr_UploadMatrices, (void*)&Hook_Upload, (void**)&g_origUpload,
					   "render_uploadModelViewMatrices (0x4482A0)");
			} else {
				std::cout << "[GemForge] Vorschau-Hook NICHT gesetzt (Konvention nicht wie "
					<< "erwartet) -- Extras bleiben in der Bench unsichtbar, in der Welt "
					<< "aber sichtbar." << std::endl;
			}
		}

		// --- 3f) Bench-Zaehler: Item_get_stack_or_grid_size ---
		// Per Disassembly BELEGT (Nutzer-Dump):
		//   undefined4 __fastcall FUN_004c7660(char* param_1)   // ECX
		//   004c7660  CMP byte ptr [ECX],0x3    ...
		//   004c7695  C3  RET
		//   004c769b  C3  RET
		//   004c769c  CC CC CC CC   align
		// Zwei schlichte RET, keine Stack-Argumente -> die 0-Argument-Variante
		// ist korrekt. Geprueft wird trotzdem, damit eine abweichende
		// Cube.exe nicht stillschweigend falsch behandelt wird.
		if (kEnableCounterHook) {
			bool cleans4 = TailIsRetImm(kAddr_StackSize, kAddr_StackSizeEnd, 0x04);
			bool plainRet = TailIsPlainRet(kAddr_StackSize, kAddr_StackSizeEnd);
			if (cleans4 && !plainRet) {
				MkHook(kAddr_StackSize, (void*)&Hook_StackSize1, (void**)&g_origStackSize,
					   "Item_get_stack_or_grid_size (0x4C7660, ret 4)");
			} else if (plainRet && !cleans4) {
				MkHook(kAddr_StackSize, (void*)&Hook_StackSize0, (void**)&g_origStackSize,
					   "Item_get_stack_or_grid_size (0x4C7660, ret)");
			} else {
				std::cout << "[GemForge] Zaehler-Hook NICHT gesetzt (Konvention nicht eindeutig) "
					<< "-- die Bench zeigt weiter nur die nativen Cubes." << std::endl;
			}
		}

		// --- 3e) Farb-Uniform mitschneiden: render_setUniform30, 1 Argument (4) ---
		if (kEnableUniformHook) {
			Convention c = AnalyzeConvention("render_setUniform30 (0x448280)",
											 kAddr_SetUniform30, kAddr_SetUniform30End, 0x04);
			if (c == CONV_CALLEE_CLEANS) {
				MkHook(kAddr_SetUniform30, (void*)&Hook_SetU30, (void**)&g_origSetU30,
					   "render_setUniform30 (0x448280)");
			} else {
				std::cout << "[GemForge] setUniform30-Hook NICHT gesetzt -- ein Spiel-Cube kann "
					<< "in der Bench kurz die Farbe eines Extra-Cubes tragen." << std::endl;
			}
		}

		// --- 3d) Auswahl-Merker: VoxelWidget::toggleSelection ---
		// Keine Stack-Argumente -> thiscall und __fastcall enden beide auf
		// "ret", die Konvention ist hier risikofrei.
		if (kEnableGemSupply) {
			MkHook(kAddr_ToggleSelection, (void*)&Hook_ToggleSel, (void**)&g_origToggleSel,
				   "VoxelWidget::toggleSelection (0x58CE40)");
		}

		// --- 3c) Edelsteinfarben: material_toColor, 4 Stack-Argumente (0x10) ---
		if (kEnableGemColorHook) {
			// Modell an den Aufrufstellen ablesen, nicht am Funktionsende --
			// dort liegt bei dieser switch-Funktion die Sprungtabelle.
			int bytes = -1, sites = 0, cleans = 0;
			Convention c = DetectArgModel(kAddr_MaterialColor, kAddr_MaterialColorEnd,
										  &bytes, &sites, &cleans);
			std::cout << "[GemForge] material_toColor (0x4C7250): " << sites
				<< " Aufrufstelle(n), davon " << cleans << " mit 'add esp'; "
				<< (c == CONV_CALLER_CLEANS ? "Aufrufer raeumt ab"
				  : c == CONV_CALLEE_CLEANS ? "Funktion raeumt selbst ab" : "unklar")
				<< ", Stack-Bytes " << bytes << std::endl;

			void* d = nullptr; const char* name = "";
			if (c == CONV_CALLER_CLEANS && bytes == 0x10) { d = (void*)&Hook_MC_C;    name = "cdecl, 4 Stack-Argumente"; }
			else if (c == CONV_CALLEE_CLEANS) {
				if      (bytes == 0x10) { d = (void*)&Hook_MC_Std;  name = "stdcall, 4 Stack-Argumente"; }
				else if (bytes == 0x0C) { d = (void*)&Hook_MC_This; name = "thiscall, ECX + 3 Stack"; }
				else if (bytes == 0x08) { d = (void*)&Hook_MC_Fast; name = "fastcall, ECX/EDX + 2 Stack"; }
			}
			g_matColorModel = bytes;
			if (d) {
				MkHook(kAddr_MaterialColor, d, (void**)&g_origMatColor, "material_toColor (0x4C7250)");
				g_matColorName = name;
				std::cout << "[GemForge] Edelsteinfarben aktiv (" << name << ")." << std::endl;
			} else {
				std::cout << "[GemForge] Farb-Hook NICHT gesetzt -- Argumentmodell nicht "
					<< "eindeutig. Edelstein-Cubes bleiben grau." << std::endl;
			}
		}

		// Der frueher noetige Auffangnetz-Hook auf Item::addAdaptionCube
		// (0x4C69B0) entfaellt: es wird nicht mehr umgeleitet, sondern vor dem
		// Setzen ein Platz im Item freigemacht. Das Spiel traegt den Cube damit
		// selbst ein -- inklusive Volumen fuer den Raycast.

		// Kompakte Uebersicht -- ein Screenshot davon genuegt zur Fehlersuche.
		std::cout << "+---------------- GemForge Hook-Status ----------------" << std::endl;
		std::cout << "| Render   0x471B60  " << (g_renderReady      ? "ok" : "AUS") << std::endl;
		std::cout << "| Bench    0x588250  " << (g_extraCubesEnabled? "ok" : "AUS") << std::endl;
		std::cout << "| Vorschau 0x4482A0  " << (g_origUpload       ? "ok" : "AUS") << std::endl;
		std::cout << "| Zaehler  0x4C7660  " << (g_origStackSize    ? "ok" : "AUS") << std::endl;
		std::cout << "| Uniform  0x448280  " << (g_origSetU30       ? "ok" : "AUS") << std::endl;
		std::cout << "| Auswahl  0x58CE40  " << (g_origToggleSel    ? "ok" : "AUS") << std::endl;
		std::cout << "| FARBEN   0x4C7250  " << (g_origMatColor     ? "ok" : "AUS")
			<< "   Modell: " << g_matColorName
			<< "  (ret=" << g_matColorModel << ")" << std::endl;
		std::cout << "+-----------------------------------------------------" << std::endl;

		if (g_extraCubesEnabled) {
			std::cout << "[GemForge] Bereit -- " << (int)kMaxPerMaterial
				<< " Cubes je Material (max. " << (int)kMaxCubesTotal
				<< " gesamt) je Waffe an der nativen Customization Bench"
				<< (g_renderReady ? "" : " (WARNUNG: Render-Hook fehlt, Extras bleiben unsichtbar)")
				<< "." << std::endl;
		} else {
			std::cout << "[GemForge] Eingeschraenkt aktiv: Bench-Bindung und Schadensformel laufen, "
				<< "aber die 32er-Grenze bleibt bestehen (siehe Meldungen oben)." << std::endl;
		}
	}

	// ==================================================================
	// Lampe an der Customization Bench (2026-10-19)
	//  * Binden: der Klick-Handler bindet nur Kategorie 3 (Waffe) an die Bench
	//    (0x47CF9B Inventar: cmp byte [ecx],3 / 0x47D9B5 Ausruestung: cmp dl,3; beide jne 0x47D7C6).
	//    Code-Hoehlen lassen zusaetzlich Kategorie 0x18 (Lampe) zu.
	//  * Vorrat: AppendGemEntries zeigt fuer Lampen nur native Schimmerpilze (0x0B/0x13, Material 9).
	//  * Lichtradius: Lampenlicht 0x4AF86B rechnet "Seltenheit(+0xDFC) * 8 + 10" (Lampe = Kreatur+0xDF0).
	//    Ersetzt durch LampRadiusC: dazu je gesetztem Schimmerpilz +1.5 Bloecke (hoechstens +60).
	// ==================================================================
	static int __cdecl LampRadiusC(uint8_t* creature) {
		int r = 10;
		__try {
			const uint8_t* lamp = creature + 0xDF0;
			r = lamp[0x0C] * 8 + 10;
			int n = CountCubesOfMaterial(lamp, 0x09);
			int bonus = (int)(n * 1.5f + 0.5f); if (bonus > 60) bonus = 60;
			r += bonus;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return r;
	}
	static uint8_t* NewCave(int n) { return (uint8_t*)VirtualAlloc(nullptr, (size_t)n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); }
	static void Rel32(uint8_t* at, uint32_t target) { *(int32_t*)at = (int32_t)(target - ((uint32_t)(uintptr_t)at + 4)); }
	static bool PatchSite(uint32_t site, const uint8_t* orig, int n, const uint8_t* repl) {
		uint8_t* p = (uint8_t*)(uintptr_t)RebaseAddr(site);
		__try { if (memcmp(p, orig, n) != 0) return false; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		DWORD old; if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return false;
		memcpy(p, repl, n); DWORD d2; VirtualProtect(p, n, old, &d2); FlushInstructionCache(GetCurrentProcess(), p, n);
		return true;
	}
	// Bindungs-Hoehle: cmp <op>,3 / je ok / cmp <op>,0x18 / je ok / jmp nein / ok: jmp ja
	static bool BindCave(uint32_t site, const uint8_t* orig9, const uint8_t* cmp3, const uint8_t* cmp18, uint32_t no, uint32_t yes) {
		uint8_t* c = NewCave(32); if (!c) return false;
		int k = 0;
		memcpy(c + k, cmp3, 3); k += 3; c[k++] = 0x74; c[k++] = 0x0A;
		memcpy(c + k, cmp18, 3); k += 3; c[k++] = 0x74; c[k++] = 0x05;
		c[k++] = 0xE9; Rel32(c + k, RebaseAddr(no)); k += 4;
		c[k++] = 0xE9; Rel32(c + k, RebaseAddr(yes)); k += 4;
		uint8_t repl[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
		*(int32_t*)(repl + 1) = (int32_t)((uint32_t)(uintptr_t)c - (RebaseAddr(site) + 5));
		return PatchSite(site, orig9, 9, repl);
	}
	void InstallLampPatches() {
		static bool done = false; if (done) return; done = true;
		static const uint8_t o1[9] = { 0x80, 0x39, 0x03, 0x0F, 0x85, 0x22, 0x08, 0x00, 0x00 };
		static const uint8_t a1[3] = { 0x80, 0x39, 0x03 }, b1[3] = { 0x80, 0x39, 0x18 };
		static const uint8_t o2[9] = { 0x80, 0xFA, 0x03, 0x0F, 0x85, 0x08, 0xFE, 0xFF, 0xFF };
		static const uint8_t a2[3] = { 0x80, 0xFA, 0x03 }, b2[3] = { 0x80, 0xFA, 0x18 };
		bool k1 = BindCave(0x47CF9B, o1, a1, b1, 0x47D7C6, 0x47CFA4);
		bool k2 = BindCave(0x47D9B5, o2, a2, b2, 0x47D7C6, 0x47D9BE);
		// Lichtradius
		static const uint8_t o3[14] = { 0x0F, 0xB6, 0x80, 0xFC, 0x0D, 0x00, 0x00, 0x8D, 0x04, 0xC5, 0x0A, 0x00, 0x00, 0x00 };
		bool k3 = false;
		uint8_t* c = NewCave(32);
		if (c) {
			int k = 0;
			c[k++] = 0x51; c[k++] = 0x52; c[k++] = 0x50;                                  // push ecx, edx, eax(Kreatur)
			c[k++] = 0xE8; Rel32(c + k, (uint32_t)(uintptr_t)&LampRadiusC); k += 4;
			c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x04;                                  // add esp,4
			c[k++] = 0x5A; c[k++] = 0x59; c[k++] = 0xC3;                                  // pop edx, pop ecx, ret
			uint8_t repl[14]; memset(repl, 0x90, sizeof(repl));
			repl[0] = 0xE8; *(int32_t*)(repl + 1) = (int32_t)((uint32_t)(uintptr_t)c - (RebaseAddr(0x4AF86B) + 5));
			k3 = PatchSite(0x4AF86B, o3, 14, repl);
		}
		std::cout << "[Lampe] Bench-Bindung Inventar " << (k1 ? "ok" : "FEHLT") << ", Ausruestung " << (k2 ? "ok" : "FEHLT")
		          << ", Lichtradius " << (k3 ? "ok" : "FEHLT") << " (je Schimmerpilz +1.5 Bloecke, max +60)." << std::endl;
	}
}
