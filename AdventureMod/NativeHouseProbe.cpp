#include "NativeHouseProbe.h"
#include "Host.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <cmath>

namespace NativeHouseProbe {

	// ---- Adressen (statisch, ImageBase 0x400000) ----------------------------
	static const uint32_t kFnPlaceStructure = 0x005F0CE0; // WorldInfo_placeStructure
	static const uint32_t kFnVectorU32At    = 0x004120C0; // vectorU32_at (thiscall)
	static const uint32_t kFnHouseCtor      = 0x005AEB40; // cube::House::ctor_0

	// Modell-Indizes eines Hausstils (aus Chunk_generateObjects @ 0x5AF170).
	// 0x8D0 ist die erste Vorlage von Stil 0 -- laut Decompile eine Wand.
	static const uint32_t kModelFirst = 0x8D0;
	static const uint32_t kModelLast  = 0x8EE;

	// ---- Die vier unsicheren Argumente von placeStructure -------------------
	// BEFUND: placeStructure erzeugt Tueren, Fenster und Moebel SELBST. Ab
	// Zeile ~1264 der Funktion vergleicht sie Blockfarben (setColor3b), legt
	// dann per Struct_InitDefaults + std_vector_pushBack_0x188 ein Objekt an und
	// loescht den Markerblock mit setColor3b(0,0,0). Die bunten Bloecke in den
	// Haeusern sind genau diese Marker -- der Umbau laeuft bei uns nicht.
	//
	// Eines dieser Argumente schaltet ihn ab. Bekannte Werte aus dem Spielcode:
	//   Dorfgenerator (game_misc.cpp:28971): kind 6, mode 0, flag 1
	//   andere Stelle  (game_misc.cpp:45867): kind 0x28, mode 0, flag 0
	// Mit STRG+U durchschalten und schauen, wann die Marker verschwinden.
	// STAND: mode 1 und 2 erzeugen sichtbare Tueren -- der Markerumbau laeuft
	// damit. Offen ist noch Volumen und Interaktion: man laeuft durch die Tueren
	// hindurch. Die Objekte entstehen also, sind aber nicht als Hindernis oder
	// Benutzbares registriert.
	static unsigned char g_argKind = 6;   // param_4  (geht in setColor4b(..|0x40))
	// AUS DEM CODE GELESEN (WorldInfo.cpp:6363 ff.), nicht geraten:
	//
	//   if (color_equals3(...)) {          // Markerfarbe erkannt
	//     if (zone == 0xd) {               // param_5 MUSS 0x0D sein
	//       Struct_InitDefaults();
	//       local_308[0] = 0x4d;           // Objekt anlegen
	//       std_vector_pushBack_0x188(...);
	//       if (param_9 != 0) {            // KORREKTUR: param_9 ist ein ZEIGER
	//         ...                          // auf eine Protokoll-Liste, kein Flag.
	//       }                              // Vanilla uebergibt hier immer 0.
	//     }
	//   }
	//
	// KORREKTUR nach genauerer Analyse: extra loescht NICHTS, es ist ein
	// Listenzeiger. Der Markerumbau haengt allein an mode. Und mode ist im
	// Generator 1:1 an den Modellindex gekoppelt (game_misc.cpp:28656-28742):
	// Modell 0x903 gehoert zu mode 0x0D. Fuer die Dorfhaeuser 0x87E-0x880 ist
	// der zugehoerige mode-Wert NICHT bekannt -- siehe RE_placeStructure.md.
	// GEMESSEN, nicht hergeleitet: mode 0 setzt ALLE Gebaeude zuverlaessig.
	// mode 1 -> 1 von 4.  mode 0x0D / 0x0E -> 0 von 4 UND 0 Baeume.
	// Der Markerumbau im Spiel ist damit fuer uns unerreichbar; wir ersetzen
	// die Marker selbst (MountainVillage::ReplaceMarkers).
	static int  g_argMode = 0;            // param_5  MUSS 0 bleiben
	static char g_argFlag = 1;            // param_7
	// param_9 ist ZEIGER auf eine std::list, KEIN Flag. Der Code dereferenziert
	// ihn an drei Stellen (WorldInfo.cpp:5659, :5816, :5964), z.B.
	//     local_6a4 = *(_Container_base0**)(param_9 + 0x18);
	// Der Wert 1 ergab damit einen Lesezugriff auf 0x19 -> Zugriffsverletzung,
	// vom __except geschluckt, PlaceModel meldete nur "false". ALLE 22 nativen
	// Aufrufstellen uebergeben 0. Das war die Ursache der Fehlschlaege.
	static int  g_argExtra = 0;           // param_9  MUSS 0 (Nullzeiger) sein
	static bool g_lastUsedFallback = false;  // fuer die Konsolenmeldung

	// Rastermasse des nativen Hauses.
	static const int kCellBlocks  = 13; // 0xD Bloecke je Hauszelle in X/Y
	static const int kFloorBlocks = 7;  // Bloecke je Stockwerk

	static uint32_t Base() { return (uint32_t)GetModuleHandleA(NULL); }
	static uint32_t Re(uint32_t staticAddr) { return Host::Re(staticAddr); }

	// ---- Codebereich aus den PE-Headern ------------------------------------
	static bool GetCodeRange(uint32_t& lo, uint32_t& hi) {
		__try {
			uint32_t b = Base();
			IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)b;
			if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
			IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
			lo = b + nt->OptionalHeader.BaseOfCode;
			hi = lo + nt->OptionalHeader.SizeOfCode;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool Readable(const void* p, size_t n) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
		if (mbi.State != MEM_COMMIT) return false;
		DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
			| PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
		if ((mbi.Protect & ok) == 0) return false;
		if (mbi.Protect & PAGE_GUARD) return false;
		return (uint8_t*)p + n <= (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
	}

	// ---- Prolog- und Konventionspruefung -----------------------------------
	// Nichts wird gerufen, was nicht vorher so aussieht wie erwartet. Das ist
	// dieselbe Regel wie im Rest des Mods -- sie hat mehrfach den Unterschied
	// zwischen "laeuft" und "Zugriffsverletzung" gemacht.
	struct FnVerdict { bool ok; const char* why; };

	// Nur der Prolog. KEIN Ruecksprung-Scan mehr: ein linearer Byte-Scan nach
	// C3/C2 laeuft ohne Instruktionslaengen-Dekoder zwangslaeufig in Datenbytes
	// und liefert Unsinn (der erste Versuch meldete "raeumt 38283 Byte ab").
	static FnVerdict VerifyPrologue(uint32_t staticAddr) {
		FnVerdict v; v.ok = false; v.why = "";
		uint32_t lo = 0, hi = 0;
		if (!GetCodeRange(lo, hi)) { v.why = "Codebereich nicht lesbar"; return v; }
		uint32_t a = Re(staticAddr);
		if (a < lo || a >= hi) { v.why = "Adresse ausserhalb .text"; return v; }
		const uint8_t* p = (const uint8_t*)a;
		if (!Readable(p, 16)) { v.why = "Prolog nicht lesbar"; return v; }
		if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC) { v.ok = true; v.why = "push ebp; mov ebp,esp"; return v; }
		v.why = "kein 55 8B EC Prolog";
		return v;
	}

	// ---- Aufrufstellen-Analyse: die belastbare Methode ----------------------
	// Wer raeumt den Stack ab? Steht hinter dem CALL ein "add esp,N", raeumt der
	// AUFRUFER ab -> __cdecl, und N verraet zugleich die Argumentgroesse.
	// Fehlt es durchgaengig, raeumt die Funktion selbst ab -> __stdcall/__thiscall.
	// Das ist unabhaengig von Instruktionslaengen und damit verlaesslich.
	struct CallSites { int total; int withCleanup; int bytes; };

	static CallSites ScanCallSites(uint32_t staticAddr) {
		CallSites r; r.total = 0; r.withCleanup = 0; r.bytes = -1;
		uint32_t lo = 0, hi = 0;
		if (!GetCodeRange(lo, hi)) return r;
		uint32_t target = Re(staticAddr);
		for (uint32_t a = lo; a + 8 < hi; a++) {
			const uint8_t* p = (const uint8_t*)a;
			if (*p != 0xE8) continue;                       // call rel32
			int32_t rel = *(const int32_t*)(p + 1);
			uint32_t dst = a + 5 + (uint32_t)rel;
			if (dst != target) continue;
			r.total++;
			const uint8_t* q = p + 5;                       // direkt hinter dem CALL
			if (q[0] == 0x83 && q[1] == 0xC4) {             // add esp, imm8
				r.withCleanup++; if (r.bytes < 0) r.bytes = q[2];
			} else if (q[0] == 0x81 && q[1] == 0xC4) {      // add esp, imm32
				r.withCleanup++; if (r.bytes < 0) r.bytes = *(const int32_t*)(q + 2);
			} else if (q[0] == 0x8D && q[1] == 0x64 && q[2] == 0x24) { // lea esp,[esp+imm8]
				r.withCleanup++; if (r.bytes < 0) r.bytes = q[3];
			}
			if (r.total >= 64) break;                       // genug Belege
		}
		return r;
	}

	// ---- Modell-Registry im GameController suchen ---------------------------
	// vectorU32_at liest self+4 (begin) und self+8 (end). Gesucht ist also ein
	// Tripel, dessen Elementzahl mindestens bis 0x8EE reicht und dessen
	// Eintraege wie Zeiger aussehen.
	static uint32_t g_registry = 0;

	// Reine SEH-Helfer: KEINE C++-Objekte in diesen Funktionen, sonst C2712
	// ("__try in Funktionen, die Objekt-Entladung brauchen").
	static bool ReadPair(const uint8_t* p, uint32_t& begin, uint32_t& end) {
		__try {
			if (!Readable(p, 12)) return false;
			begin = *(uint32_t*)(p + 4);
			end   = *(uint32_t*)(p + 8);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Wie viele der Hausvorlagen 0x8D0..0x8EE sehen wie gueltige Zeiger aus?
	static int ScorePlausible(uint32_t begin) {
		__try {
			uint32_t* v = (uint32_t*)begin;
			int good = 0;
			for (uint32_t i = kModelFirst; i <= kModelLast; i++) {
				uint32_t e = v[i];
				if (e >= 0x10000 && (e & 3) == 0 && Readable((void*)e, 4)) good++;
			}
			return good;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	static uint32_t ScanForRegistry(cube::GameController* gc, int& outCount, int& outScore) {
		uint8_t* g = (uint8_t*)gc;
		const uint32_t kScan = 0x810000; // GameController ist riesig (vgl. GC+0x8006D0)
		const int kNeed = (int)(kModelLast - kModelFirst) - 2; // fast alle muessen passen
		for (uint32_t off = 0; off + 12 < kScan; off += 4) {
			uint32_t begin = 0, end = 0;
			if (!ReadPair(g + off, begin, end)) continue;
			if (begin == 0 || end <= begin) continue;
			uint32_t bytes = end - begin;
			if (bytes & 3) continue;
			uint32_t count = bytes >> 2;
			if (count <= kModelLast || count > 0x20000) continue; // muss 0x8EE abdecken
			if (!Readable((void*)begin, (kModelLast + 1) * 4)) continue;
			int score = ScorePlausible(begin);
			if (score >= kNeed) { outCount = (int)count; outScore = score; return off; }
		}
		return 0xFFFFFFFF;
	}

	static uint32_t FindModelRegistry(cube::GameController* gc) {
		if (!gc) return 0;
		if (g_registry) return g_registry;
		int count = 0, score = 0;
		uint32_t off = ScanForRegistry(gc, count, score);
		if (off == 0xFFFFFFFF) {
			std::cout << "[HausProbe] Keine Modell-Registry im GameController gefunden." << std::endl;
			return 0;
		}
		g_registry = (uint32_t)((uint8_t*)gc + off);
		std::cout << "[HausProbe] Registry bei GC+0x" << std::hex << off << std::dec
			<< " (" << count << " Eintraege, " << score << " von "
			<< (kModelLast - kModelFirst + 1) << " Hausvorlagen plausibel)" << std::endl;
		return g_registry;
	}

	// vectorU32_at ist thiscall: ECX = Registry, Rueckgabe in EAX.
	static uint32_t RegistryAt(uint32_t registry, uint32_t index) {
		typedef uint32_t(__fastcall* Fn)(uint32_t ecx, uint32_t edx, uint32_t idx);
		Fn f = (Fn)Re(kFnVectorU32At);
		__try { return f(registry, 0, index); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	// ---- Vorpruefungen ------------------------------------------------------
	void DryRun(cube::GameController* gc) {
		std::cout << "=== [HausProbe] Vorpruefung ===" << std::endl;
		std::cout << "  Modulbasis: 0x" << std::hex << Base() << std::dec << std::endl;

		struct Item { uint32_t addr; const char* name; int expectArgBytes; };
		static const Item items[] = {
			{ kFnPlaceStructure, "WorldInfo_placeStructure @0x5F0CE0", 36 }, // 9 Argumente
			{ kFnVectorU32At,    "vectorU32_at            @0x4120C0",  4 }, // thiscall, 1 Argument
			{ kFnHouseCtor,      "House::ctor_0           @0x5AEB40", 12 }, // thiscall, 3 Argumente
		};
		for (int i = 0; i < 3; i++) {
			FnVerdict v = VerifyPrologue(items[i].addr);
			CallSites c = ScanCallSites(items[i].addr);
			std::cout << "  " << items[i].name << ": Prolog "
				<< (v.ok ? "OK" : "ABGELEHNT") << " (" << v.why << ")" << std::endl;
			std::cout << "      Aufrufstellen: " << c.total;
			if (c.total == 0) {
				std::cout << " -- keine gefunden, Konvention unbestimmt" << std::endl;
			} else if (c.withCleanup == 0) {
				std::cout << ", keine mit 'add esp' -> Funktion raeumt selbst ab"
					<< " (__stdcall/__thiscall)" << std::endl;
			} else {
				std::cout << ", davon " << c.withCleanup << " mit 'add esp," << c.bytes
					<< "' -> __cdecl, " << (c.bytes / 4) << " Argumente"
					<< " (erwartet " << (items[i].expectArgBytes / 4) << ")" << std::endl;
			}
		}

		uint32_t reg = FindModelRegistry(gc);
		if (reg) {
			std::cout << "  Hausvorlagen 0x8D0..0x8D4: ";
			for (uint32_t i = kModelFirst; i < kModelFirst + 5; i++)
				std::cout << "0x" << std::hex << RegistryAt(reg, i) << std::dec << " ";
			std::cout << std::endl;
		}
		std::cout << "  Raster: " << kCellBlocks << " Bloecke je Zelle, "
			<< kFloorBlocks << " je Stockwerk, Haus 3x3 Zellen = "
			<< (3 * kCellBlocks) << "x" << (3 * kCellBlocks) << " Bloecke." << std::endl;
		std::cout << "=== Ende Vorpruefung ===" << std::endl;
	}

	// ---- Der scharfe Aufruf -------------------------------------------------
	// Belegt durch die Aufrufstellen-Analyse: placeStructure raeumt selbst ab.
	// Als freie Funktion (Global::) mit 9 Argumenten heisst das __stdcall.
	typedef void(__stdcall* Fn_PlaceStructure)(
		void* tmpl,      // Modellvorlage aus der Registry
		int* origin,     // int[3], Blockkoordinaten
		unsigned rot,    // Drehung 0..3 (im Code: -90 Grad je Schritt)
		unsigned char color,
		int kind,
		void* zone,      // cube::Zone*
		char flag,
		int* pal,        // param_8: int[4] Beschnittraender, KEIN Farbpuffer
		int extra);

	static bool CallPlaceStructure(void* tmpl, int* origin, void* zone, int* pal) {
		Fn_PlaceStructure f = (Fn_PlaceStructure)Re(kFnPlaceStructure);
		__try {
			// Argumentwerte wie im Dorfgenerator (game_misc.cpp:28971):
			// rot 0, color 6, kind 0, flag 0, extra 0.
			f(tmpl, origin, 0, 6, 0, zone, 0, pal, 0);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static int FloorDivP(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

	// Eigene Funktion, weil __try nicht mit C++-Objekten im selben Rumpf darf.
	static cube::Zone* SafeZone(cube::GameController* gc, int bx, int by) {
		__try { return gc->world.GetZone(FloorDivP(bx, 256), FloorDivP(by, 256)); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	// ---- Nativer Hausbau ----------------------------------------------------
	// Rezeptur wortgleich aus dem Dorfgenerator (game_misc.cpp:28971 / 29009):
	//     tmpl = vectorU32_at(index);
	//     w = tmpl->field_0x44;  h = tmpl->field_0x48;
	//     if (rotation & 1) swap(w, h);          // ungerade Drehung tauscht die Achsen
	//     pos = { baseX - w/2, baseY - h/2, z }; // Modelle werden ZENTRIERT gesetzt
	//     placeStructure(tmpl, pos, rotation, 6, 0, zone, 1, farbe, 0);
	// Wichtig: param_7 ist an diesen Stellen 1, nicht 0.

	static bool ReadModelSize(void* tmpl, int& w, int& h) {
		__try {
			if (!Readable(tmpl, 0x50)) return false;
			w = *(int*)((uint8_t*)tmpl + 0x44);
			h = *(int*)((uint8_t*)tmpl + 0x48);
			// Grobe Plausibilitaet: ein Bauteil ist keine 0 und keine 4096 Bloecke breit.
			if (w <= 0 || h <= 0 || w > 512 || h > 512) return false;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool PlaceModel(uint32_t reg, void* zone, int modelIndex,
	                       int bx, int by, int bz, int rot, int mode) {
		void* tmpl = (void*)RegistryAt(reg, (uint32_t)modelIndex);
		if (!tmpl) return false;
		int w = 0, h = 0;
		if (!ReadModelSize(tmpl, w, h)) return false;
		if (rot & 1) { int t = w; w = h; h = t; }
		int origin[3] = { bx - w / 2, by - h / 2, bz };
		// param_8 ist KEIN Farbpuffer, sondern int[4] Beschnittraender auf die
		// Modell-Schleifengrenzen (WorldInfo.cpp:5359). Vanilla immer {0,0,0,0}.
		int clip[4] = { 0, 0, 0, 0 };
		Fn_PlaceStructure f = (Fn_PlaceStructure)Re(kFnPlaceStructure);
		__try {
			f(tmpl, origin, (unsigned)(rot & 3), g_argKind, mode, zone, g_argFlag, clip, g_argExtra);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Verfuegbar heisst: die Registry steht UND das bestaetigte Schmiedemodell
	// (0x903) hat Gebaeudegroesse. Der Namenstest taugt dafuer NICHT -- die
	// Modellobjekte fuehren den Dateinamen offenbar nicht in den ersten Feldern,
	// im Spiel kam "namenlos" zurueck. Die Groesse ist die belastbare Probe:
	// 32x31 kann kein Deko-Objekt sein.
	// ---- Modell ueber seine NAMENSADRESSE finden -----------------------------
	// Die Modellnamen liegen als Zeichenketten im Binary, ihre Adressen sind aus
	// dem Disassembly ablesbar, z.B.
	//     007115d4  "framework-wall-lamp.cub"
	// Jedes geladene Modell haelt einen Zeiger auf seinen Namen. Wir suchen also
	// den Registry-Eintrag, in dem GENAU diese Adresse steht -- das ist exakt und
	// braucht kein Raten ueber Reihenfolgen oder Groessen.
	//
	// Warum nicht ueber den Namenstext? Ein frueherer Versuch las den Namen aus
	// den ersten 0x20 Feldern und bekam "namenlos" zurueck -- der Zeiger sitzt
	// weiter hinten. Hier wird deshalb ein groesserer Bereich durchsucht UND auf
	// eine bekannte Adresse verglichen statt auf Text.
	static int FindModelByNameAddr(uint32_t reg, uint32_t staticStrAddr) {
		uint32_t want = Re(staticStrAddr);
		for (uint32_t i = 0; i < 2600; i++) {
			void* t = (void*)RegistryAt(reg, i);
			if (!t) continue;
			__try {
				if (!Readable(t, 0x200)) continue;
				uint32_t* f = (uint32_t*)t;
				for (int k = 0; k < 0x80; k++)
					if (f[k] == want) return (int)i;
			} __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
		}
		return -1;
	}

	// framework-wall-lamp.cub -- Adresse aus dem Disassembly.
	static const uint32_t kStrFrameworkWallLamp = 0x007115D4;

	// KORRIGIERT: Die Modellobjekte (cube::Sprite, 0x60 Byte) haben KEIN
	// Namensfeld -- FindModelByNameAddr konnte deshalb nie treffen
	// (RE_EigeneStrukturen.md, 1.3/1.6). Der Index steht fest im Lader
	// GameController_load_game_assets @0x4EE230:
	//     string_ctorAppend(..., "framework-wall-lamp.cub"); vector_at_stride4(0x895);
	// Vollstaendige Zuordnung Index -> Datei: LabModelNames.h.
	static const int kModelFrameworkWallLamp = 0x895;
	int FindWallLampModel(cube::GameController* gc) {
		static int cached = -2;
		if (cached != -2) return cached;
		uint32_t reg = FindModelRegistry(gc);
		cached = (reg && RegistryAt(reg, kModelFrameworkWallLamp)) ? kModelFrameworkWallLamp : -1;
		std::cout << "[Lampe] framework-wall-lamp.cub -> Registry-Index "
			<< (cached >= 0 ? "0x" : "") << std::hex << (cached >= 0 ? cached : 0)
			<< std::dec << (cached >= 0 ? "" : " NICHT GEFUNDEN") << std::endl;
		return cached;
	}

	// LAB: Adresse des SpriteManagers (Registry). Vektor<Sprite*> bei +0x04/+0x08/+0x0C,
	// IDirect3DDevice9* bei +0x10 (RE_EigeneStrukturen.md 1.6). 0 = nicht gefunden.
	uint32_t RegistryBase(cube::GameController* gc) { return gc ? FindModelRegistry(gc) : 0; }

	// LAB: Modellobjekt zu einem Registry-Index (nullptr = nicht vorhanden).
	void* RegistryModel(cube::GameController* gc, int index) {
		if (!gc || index < 0) return nullptr;
		uint32_t reg = FindModelRegistry(gc);
		return reg ? (void*)RegistryAt(reg, (uint32_t)index) : nullptr;
	}

	bool Available(cube::GameController* gc) {
		if (!gc) return false;
		uint32_t reg = FindModelRegistry(gc);
		if (!reg) return false;
		void* t = (void*)RegistryAt(reg, 0x903);
		int w = 0, h = 0;
		if (!t || !ReadModelSize(t, w, h)) return false;
		return (w >= 20 && h >= 20);
	}

	// ---- Modelle ueber ihren NAMEN finden -----------------------------------
	// Die Modelldateinamen ("framework-wall.cub", "stone-roof1.cub", ...) stehen
	// im Klartext im Binary, und jedes geladene Modell haelt einen Zeiger darauf.
	// Damit brauchen wir keine hartkodierten Registry-Indizes mehr -- die waren
	// geraten und lieferten Dungeonteile statt Hauswaenden.

	// Sucht in den ersten Feldern des Modellobjekts einen Zeiger auf einen
	// String, der auf ".cub" endet. Gibt ihn zurueck oder nullptr.
	static const char* ModelNameOf(void* tmpl) {
		__try {
			if (!Readable(tmpl, 0x80)) return nullptr;
			uint32_t* f = (uint32_t*)tmpl;
			for (int i = 0; i < 0x20; i++) {
				uint32_t p = f[i];
				if (p < 0x10000) continue;
				if (!Readable((void*)p, 64)) continue;
				const char* s = (const char*)p;
				int n = 0;
				while (n < 60 && s[n] >= 0x20 && s[n] < 0x7F) n++;
				if (n < 5 || s[n] != '\0') continue;
				if (s[n-4] == '.' && s[n-3] == 'c' && s[n-2] == 'u' && s[n-1] == 'b') return s;
			}
			return nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}

	static bool NameEquals(const char* a, const char* b) {
		if (!a || !b) return false;
		while (*a && *b) { if (*a != *b) return false; a++; b++; }
		return *a == *b;
	}

	// Linearer Durchlauf durch die Registry, bis der Name passt. Ergebnis wird
	// gepuffert, damit der Dorfbau nicht 2569 Eintraege je Teil durchsucht.
	struct NameCache { char name[48]; int index; };
	static NameCache g_nameCache[32];
	static int g_nameCacheCount = 0;

	static int FindModelIndex(uint32_t reg, const char* name) {
		for (int i = 0; i < g_nameCacheCount; i++)
			if (NameEquals(g_nameCache[i].name, name)) return g_nameCache[i].index;
		for (uint32_t i = 0; i < 2600; i++) {
			void* t = (void*)RegistryAt(reg, i);
			if (!t) continue;
			const char* n = ModelNameOf(t);
			if (!NameEquals(n, name)) continue;
			if (g_nameCacheCount < 32) {
				int k = 0; while (k < 47 && name[k]) { g_nameCache[g_nameCacheCount].name[k] = name[k]; k++; }
				g_nameCache[g_nameCacheCount].name[k] = '\0';
				g_nameCache[g_nameCacheCount].index = (int)i;
				g_nameCacheCount++;
			}
			return (int)i;
		}
		return -1;
	}

	// ---- Ganzgebaeude setzen -------------------------------------------------
	// Viel einfacher als der 13-Teile-Bausatz: die Registry enthaelt fertige
	// Gebaeude (building-smithy, frame-house01, ...). Ein Aufruf, ein Haus.
	bool UsedFallback() { return g_lastUsedFallback; }

	bool PlaceBuilding(cube::GameController* gc, int modelIndex, int cx, int cy, int cz, int rot, int mode) {
		if (!gc) return false;
		uint32_t reg = FindModelRegistry(gc);
		if (!reg) return false;
		// ZONENGRENZEN -- Ursache der angeschnittenen Haeuser.
		// placeStructure schreibt NUR Bloecke, die in der uebergebenen Zone
		// liegen; alles ausserhalb verwirft es still (WorldInfo.cpp:5306-5340).
		// Ein 33 Bloecke breites Haus kann bis zu vier Zonen beruehren
		// (Zone = 256 Bloecke). Wurde nur die Zone der Hausmitte uebergeben,
		// fehlte der ueberstehende Teil -- und mit ihm dessen Markerbloecke,
		// also auch die Stationen. Genau das erklaert "mal ganz, mal halb".
		//
		// Deshalb: Grundflaeche aus dem Modell holen und den Aufruf fuer JEDE
		// beruehrte Zone wiederholen. Jeder Aufruf schreibt seinen Anteil.
		int mw = 0, mh = 0;
		{
			void* t0 = (void*)RegistryAt(reg, (uint32_t)modelIndex);
			if (!t0 || !ReadModelSize(t0, mw, mh)) return false;
			if (rot & 1) { int t = mw; mw = mh; mh = t; }
		}
		const int zx0 = FloorDivP(cx - mw / 2 - 1, 256), zx1 = FloorDivP(cx + mw / 2 + 1, 256);
		const int zy0 = FloorDivP(cy - mh / 2 - 1, 256), zy1 = FloorDivP(cy + mh / 2 + 1, 256);

		int parts = 0, zonesSeen = 0;
		for (int zx = zx0; zx <= zx1; zx++) {
			for (int zy = zy0; zy <= zy1; zy++) {
				cube::Zone* zp = SafeZone(gc, zx * 256 + 1, zy * 256 + 1);
				if (!zp) continue;                 // Zone nicht geladen
				zonesSeen++;
				if (PlaceModel(reg, zp, modelIndex, cx, cy, cz, rot, mode)) parts++;
			}
		}
		if (parts == 0) {
			// Markermodus ausgefallen -> zurueck auf mode 0, dieselbe Zonenschleife.
			for (int zx = zx0; zx <= zx1; zx++)
				for (int zy = zy0; zy <= zy1; zy++) {
					cube::Zone* zp = SafeZone(gc, zx * 256 + 1, zy * 256 + 1);
					if (zp && PlaceModel(reg, zp, modelIndex, cx, cy, cz, rot, 0)) parts++;
				}
			if (parts == 0) return false;
			MakeObjectsUsable(gc, cx, cy, cz, 26);
			MakeMarkersUsable(gc, cx, cy, cz, 20);
			g_lastUsedFallback = true;
			return true;
		}
		// IMMER melden, nicht nur im Fehlerfall. "Manchmal abgeschnitten" laesst
		// sich sonst nicht zuordnen: liegt es am SETZEN (Zone fehlt / Aufruf
		// verworfen) oder am VERMESHEN (Bloecke da, Mesh nicht aktualisiert)?
		// Die Zeile trennt beides.
		int zonesWanted = (zx1 - zx0 + 1) * (zy1 - zy0 + 1);
		std::cout << "[Haus] 0x" << std::hex << modelIndex << std::dec
			<< "  Groesse " << mw << "x" << mh
			<< "  Mitte(" << cx << "," << cy << "," << cz << ")"
			<< "  Zonen " << zonesSeen << "/" << zonesWanted
			<< "  Teile gesetzt " << parts;
		if (zonesSeen < zonesWanted)
			std::cout << "   >>> ZONE FEHLT, Rest unvermeidbar weg";
		else if (parts < zonesSeen)
			std::cout << "   >>> Aufruf verworfen in " << (zonesSeen - parts) << " Zone(n)";
		std::cout << std::endl;
		std::cout.flush();
		g_lastUsedFallback = false;
		// Blocktyp an den neu entstandenen Objekten nachziehen -> Tueren werden
		// fest und benutzbar. Radius grosszuegig, das groesste Modell ist 35x35.
		// Der Markerumbau laeuft jetzt nativ (mode 0x0D + extra 1).
		// Die beiden Notbehelfe bleiben als Reserve, falls einzelne
		// Marker stehenbleiben -- dann hier wieder einkommentieren.
		// MakeObjectsUsable(gc, cx, cy, cz, 26);
		// MakeMarkersUsable(gc, cx, cy, cz, 20);
		return true;
	}

	// ---- Abschlussschritt: funktionale Blocktypen -----------------------------
	// BEFUND (isInteractableTileType @ 0x47F9F0): benutzbar ist ein Block, wenn
	// sein Type-Byte in {0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x15} liegt. Tueren und
	// Truhen sind also BLOECKE mit Sondertyp -- das Modell ist nur die Optik.
	// placeStructure legt das Sichtobjekt an, setzt aber offenbar den Typ nicht.
	static bool IsInteractableType(unsigned char t) {
		return t == 0x01 || t == 0x0A || t == 0x0B || t == 0x0C || t == 0x0D || t == 0x15;
	}

	static bool ReadBlockNH(cube::World* w, int x, int y, int z,
	                        unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& t) {
		__try {
			BlockColor* c = w->GetBlock(x, y, z, (cube::Zone*)nullptr);
			if (!c || c->Type == 0) return false;
			r = c->Red; g = c->Green; b = c->Blue; t = c->Type;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Zaehlt die Blocktypen im Grundriss eines gerade gesetzten Gebaeudes.
	// Damit ist sofort sichtbar, ob placeStructure funktionale Typen schreibt.
	void InspectBuilding(cube::GameController* gc, int cx, int cy, int cz, int w, int h) {
		if (!gc) return;
		cube::World* wo = &gc->world;
		int counts[256]; for (int i = 0; i < 256; i++) counts[i] = 0;
		int total = 0, inter = 0;
		int hw = w / 2 + 2, hh = h / 2 + 2;
		for (int dx = -hw; dx <= hw; dx++)
			for (int dy = -hh; dy <= hh; dy++)
				for (int dz = -2; dz <= 24; dz++) {
					unsigned char r, g, b, t;
					if (!ReadBlockNH(wo, cx + dx, cy + dy, cz + dz, r, g, b, t)) continue;
					counts[t]++; total++;
					if (IsInteractableType(t)) inter++;
				}
		std::cout << "[Bau] " << total << " Bloecke, davon " << inter
			<< " mit benutzbarem Typ. Typverteilung:" << std::endl;
		for (int i = 1; i < 256; i++) {
			if (!counts[i]) continue;
			std::cout << "   Typ 0x" << std::hex << i << std::dec << " = " << counts[i]
				<< (IsInteractableType((unsigned char)i) ? "   <-- benutzbar" : "") << std::endl;
		}
		if (inter == 0)
			std::cout << "[Bau] KEIN benutzbarer Blocktyp gesetzt -- das ist die Ursache "
				<< "fuer Tueren ohne Volumen." << std::endl;
	}

	bool ModelSize(cube::GameController* gc, int modelIndex, int& w, int& h) {
		w = 0; h = 0;
		if (!gc) return false;
		uint32_t reg = FindModelRegistry(gc);
		if (!reg) return false;
		void* t = (void*)RegistryAt(reg, (uint32_t)modelIndex);
		return t && ReadModelSize(t, w, h);
	}

	// ---- Neu vermeschen ------------------------------------------------------
	// placeStructure schreibt in die Chunkdaten, NICHT ins sichtbare Mesh.
	// Ohne das hier bleibt alles unsichtbar -- im Dorf fiel das nicht auf, weil
	// PlaceVillageDeco ohnehin am Ende remesht. Gleiche Logik wie
	// MountainVillage::RemeshLoadedChunks.
	// Vermescht ALLE geladenen Chunks neu. Wichtig nach placeStructure: die
	// Funktion schreibt Bloecke, ruehrt das Mesh aber nicht an. Bleibt ein
	// Hausteil unsichtbar, obwohl die Bloecke da sind, fehlt genau das hier.
	static void RemeshAll(cube::GameController* gc) {
		__try {
			uint32_t n = gc->chunk_count_to_be_sqaured;
			if (n == 0 || n > 128) return;
			uint32_t total = n * n;
			cube::Chunk* ch = gc->chunks;
			if (!ch) return;
			// Nur Chunks im Umkreis von 192 Bloecken um den Spieler (Browser und
			// Testhaeuser stehen 40 Bloecke vor ihm). ALLE zu markieren erzeugt
			// einen Mesh-Spike -- siehe Warnung in MountainVillage.cpp.
			int px = 0, py = 0;
			if (gc->local_player) {
				px = (int)(gc->local_player->entity_data.position.X >> 16);
				py = (int)(gc->local_player->entity_data.position.Y >> 16);
			}
			int cxlo = (px - 192) >> 5, cxhi = (px + 192) >> 5, cylo = (py - 192) >> 5, cyhi = (py + 192) >> 5;
			for (uint32_t i = 0; i < total; i++) {
				uint8_t* c = (uint8_t*)&ch[i];
				int cx = *(int*)(c + 0x18), cy = *(int*)(c + 0x1C);
				if (cx >= cxlo && cx <= cxhi && cy >= cylo && cy <= cyhi) ch[i].needs_update = 1;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// ---- Modell-Browser ------------------------------------------------------
	// Setzt eine Reihe aufeinanderfolgender Modelle vor den Spieler, mit Abstand
	// und Indexausgabe. So sieht man in einem Durchgang, welcher Index Wand,
	// Ecke, Tuer oder Dach ist -- statt es aus 800 Zeilen Pseudo-C zu raten.
	static int g_browseBase = 0x839;  // ab hier ist noch nichts durchgesehen
	static int g_browseMinDim = 10;   // kleinere Modelle sind Items, nicht Bauten

	// ---- Ganze Registry in eine Datei schreiben ------------------------------
	// Einmal STRG+8 statt 257-mal durchblaettern. Die Groesse allein trennt schon
	// sauber: Ganzgebaeude sind 20..40 Bloecke, Bauteile ~13, Deko < 10.
	void DumpRegistry(cube::GameController* gc) {
		uint32_t reg = FindModelRegistry(gc);
		if (!reg) { std::cout << "[Modelle] Keine Registry." << std::endl; return; }

		std::ofstream f("ModelRegistry.txt");
		if (!f) { std::cout << "[Modelle] Datei nicht schreibbar." << std::endl; return; }
		f << "Index  Groesse   Kategorie      Name\n";
		f << "------------------------------------------------------\n";

		int total = 0, big = 0, named = 0;
		for (uint32_t i = 0; i < 2600; i++) {
			void* t = (void*)RegistryAt(reg, i);
			if (!t) continue;
			int w = 0, h = 0;
			if (!ReadModelSize(t, w, h)) continue;
			total++;
			const char* kat = "Deko";
			if (w >= 20 || h >= 20)      { kat = "GEBAEUDE"; big++; }
			else if (w >= 10 || h >= 10)   kat = "Bauteil";
			const char* nm = ModelNameOf(t);
			if (nm) named++;
			f << "0x" << std::hex << i << std::dec
			  << "\t" << w << "x" << h
			  << "\t" << kat
			  << "\t" << (nm ? nm : "-") << "\n";
		}
		f.close();
		std::cout << "[Modelle] ModelRegistry.txt geschrieben: " << total
			<< " Eintraege, davon " << big << " gebaeudegross, "
			<< named << " mit lesbarem Namen." << std::endl;
		std::cout << "[Modelle] Die Datei liegt neben Cube.exe." << std::endl;
	}

	// ---- SCANNER: bestehende Bauten erfassen --------------------------------
	// Ein zone_static_object ist 0x188 Byte. Belegung (aus FillBuilding und dem
	// City-Dump, hier nur gelesen):
	//     +0x00 int    Typ / Modellverweis
	//     +0x08 int64  X in Fixed16 (Bloecke * 65536)
	//     +0x10 int64  Y
	//     +0x18 int64  Z
	//     +0x20 int    Drehung
	//     +0x24 float  Skala X   +0x28 Skala Y   +0x2C Hoehe
	static const int kObjStride = 0x188;

	// Reine SEH-Leser, damit kein __try neben C++-Objekten steht.
	struct ObjRec { int type; int bx, by, bz; int rot; float sx, sy, sz; };

	static bool ReadZoneObjects(cube::Zone* zone, uint8_t*& outStart, int& outCount) {
		__try {
			uint8_t** raw = (uint8_t**)&zone->static_objects;
			uint8_t* s = raw[0]; uint8_t* e = raw[1];
			if (!s || !e || e <= s) return false;
			int n = (int)((e - s) / kObjStride);
			if (n < 0 || n > 20000) return false;
			outStart = s; outCount = n;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static bool ReadObj(uint8_t* base, int i, ObjRec& o) {
		__try {
			uint8_t* p = base + (size_t)i * kObjStride;
			if (!Readable(p, kObjStride)) return false;
			o.type = *(int*)(p + 0x00);
			long long x = *(long long*)(p + 0x08);
			long long y = *(long long*)(p + 0x10);
			long long z = *(long long*)(p + 0x18);
			o.bx = (int)(x / 65536); o.by = (int)(y / 65536); o.bz = (int)(z / 65536);
			o.rot = *(int*)(p + 0x20);
			o.sx = *(float*)(p + 0x24);
			o.sy = *(float*)(p + 0x28);
			o.sz = *(float*)(p + 0x2C);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static int g_scanNo = 0;

	void ResetScan() {
		std::ofstream f("HausScan.txt", std::ios::trunc);
		f << "# HausScan -- Stuecklisten erfasster Bauten\n";
		f.close();
		g_scanNo = 0;
		std::cout << "[Scan] HausScan.txt zurueckgesetzt." << std::endl;
	}

	void ScanArea(cube::GameController* gc, int radiusBlocks) {
		if (!gc || !gc->local_player) { std::cout << "[Scan] Kein Spieler." << std::endl; return; }
		const double U = 65536.0;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)U);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)U);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)U);

		std::ofstream f("HausScan.txt", std::ios::app);
		if (!f) { std::cout << "[Scan] Datei nicht schreibbar." << std::endl; return; }

		g_scanNo++;
		f << "\n=== Scan " << g_scanNo << "  Spieler auf Block(" << px << "," << py << "," << pz
		  << ")  Radius " << radiusBlocks << " ===\n";
		f << "dx\tdy\tdz\tTyp\tDreh\tSkalaX\tSkalaY\tHoehe\tModellgroesse\n";

		uint32_t reg = FindModelRegistry(gc);
		int found = 0;

		// Zonen im Umkreis abklappern (Zone = 256 Bloecke).
		int zr = radiusBlocks / 256 + 1;
		int pzx = FloorDivP(px, 256), pzy = FloorDivP(py, 256);
		for (int a = -zr; a <= zr; a++) {
			for (int b = -zr; b <= zr; b++) {
				cube::Zone* z = SafeZone(gc, (pzx + a) * 256 + 1, (pzy + b) * 256 + 1);
				if (!z) continue;
				uint8_t* base = nullptr; int n = 0;
				if (!ReadZoneObjects(z, base, n)) continue;
				for (int i = 0; i < n; i++) {
					ObjRec o;
					if (!ReadObj(base, i, o)) continue;
					int dx = o.bx - px, dy = o.by - py, dz = o.bz - pz;
					if (dx * dx + dy * dy > radiusBlocks * radiusBlocks) continue;
					// Groesse des referenzierten Modells dazuschreiben -- damit
					// laesst sich pruefen, ob Typ und Registry-Index dieselbe
					// Nummerierung benutzen.
					int mw = 0, mh = 0;
					if (reg) {
						void* t = (void*)RegistryAt(reg, (uint32_t)o.type);
						if (t) ReadModelSize(t, mw, mh);
					}
					f << dx << "\t" << dy << "\t" << dz
					  << "\t0x" << std::hex << o.type << std::dec
					  << "\t" << o.rot
					  << "\t" << o.sx << "\t" << o.sy << "\t" << o.sz
					  << "\t" << mw << "x" << mh << "\n";
					found++;
				}
			}
		}
		f << "# " << found << " Objekte erfasst.\n";
		f.close();
		std::cout << "[Scan " << g_scanNo << "] " << found
			<< " Objekte im Umkreis " << radiusBlocks << " erfasst -> HausScan.txt" << std::endl;
		if (found == 0)
			std::cout << "[Scan] Nichts gefunden. Stehst du in einem nativen Dorf?" << std::endl;
	}

	// ---- Beschriftung aus Bloecken ------------------------------------------
	// 3x5-Ziffernsatz fuer 0-9 und A-F, je Zeile drei Bits (links = hoechstes).
	static const unsigned char kGlyph[16][5] = {
		{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
		{5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
		{7,5,7,5,7}, {7,5,7,1,7}, {7,5,7,5,5}, {6,5,6,5,6},
		{7,4,4,4,7}, {6,5,5,5,6}, {7,4,7,4,7}, {7,4,7,4,4},
	};

	static bool SetBlockNH(cube::World* w, int x, int y, int z,
	                       unsigned char r, unsigned char g, unsigned char b, unsigned char t) {
		__try {
			BlockColor col(r, g, b, t);
			w->SetBlock(x, y, z, &col, (cube::Zone*)nullptr);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- Der fehlende Schritt: Blocktyp an den Objektpositionen setzen ------
	// BELEGT: Kollision und Benutzbarkeit haengen am BLOCKTYP, nicht am Objekt.
	//   isInteractableTileType @ 0x47F9F0 : typ in {1, 0x0A, 0x0B, 0x0C, 0x0D, 0x15}
	//   interactSpecialObject @ 0x488030 : typ in {1, 2, 3, 0x0A, 0x10, 0x12, 0x13, 0x45}
	//     -- alles andere beantwortet das Spiel mit "There is nothing special."
	// placeStructure legt das SICHTOBJEKT an und loescht den Markerblock; den
	// funktionalen Typ setzt im nativen Ablauf ein Schritt, der bei uns fehlt.
	// Deshalb sieht man die Tuer und laeuft hindurch.
	//
	// Wir holen das nach: nach dem Setzen eines Gebaeudes werden die Bloecke an
	// den Positionen der neu entstandenen Objekte auf einen benutzbaren Typ
	// gesetzt, Farbe bleibt erhalten.
	static const unsigned char kTypeInteractive = 1;   // in BEIDEN Listen enthalten

	// Setzt an allen Objektpositionen im Umkreis den Blocktyp. Rueckgabe: Anzahl.
	// Ist der Block ein Markerblock? Marker sind knallige Reinfarben
	// (Magenta, Gruen, Rot, Blau, Gelb, Cyan) -- Baumaterial ist immer
	// gedeckt. Ein Kanal hoch UND ein Kanal niedrig trennt beides sauber.
	static bool IsMarkerColor(unsigned char r, unsigned char g, unsigned char b) {
		unsigned char hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
		unsigned char lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
		return hi >= 200 && lo <= 60;
	}

	// Sucht im Grundriss des Gebaeudes die Markerbloecke und macht sie benutzbar.
	// Ersetzt den Schritt, den placeStructure mit mode 1 selbst taete -- nur ohne
	// dessen Ausnahmen.
	int MakeMarkersUsable(cube::GameController* gc, int cx, int cy, int cz, int radius) {
		if (!gc) return 0;
		cube::World* w = &gc->world;
		int fixed = 0;
		for (int dx = -radius; dx <= radius; dx++) {
			for (int dy = -radius; dy <= radius; dy++) {
				for (int dz = -2; dz <= 34; dz++) {
					unsigned char r, g, b, t;
					if (!ReadBlockNH(w, cx + dx, cy + dy, cz + dz, r, g, b, t)) continue;
					if (!IsMarkerColor(r, g, b)) continue;
					if (t == kTypeInteractive) continue;
					if (SetBlockNH(w, cx + dx, cy + dy, cz + dz, r, g, b, kTypeInteractive))
						fixed++;
				}
			}
		}
		return fixed;
	}

	int MakeObjectsUsable(cube::GameController* gc, int cx, int cy, int cz, int radius) {
		if (!gc) return 0;
		cube::World* w = &gc->world;
		int fixed = 0;
		int zr = radius / 256 + 1;
		int pzx = FloorDivP(cx, 256), pzy = FloorDivP(cy, 256);
		for (int a = -zr; a <= zr; a++) {
			for (int b = -zr; b <= zr; b++) {
				cube::Zone* z = SafeZone(gc, (pzx + a) * 256 + 1, (pzy + b) * 256 + 1);
				if (!z) continue;
				uint8_t* base = nullptr; int n = 0;
				if (!ReadZoneObjects(z, base, n)) continue;
				for (int i = 0; i < n; i++) {
					ObjRec o;
					if (!ReadObj(base, i, o)) continue;
					int dx = o.bx - cx, dy = o.by - cy, dz = o.bz - cz;
					if (dx * dx + dy * dy > radius * radius) continue;
					if (dz < -4 || dz > 40) continue;
					// Der Block an der Objektposition bekommt den benutzbaren Typ.
					// Farbe bleibt, sonst faellt die Tuer optisch aus dem Rahmen.
					unsigned char cr, cg, cb, ct;
					if (!ReadBlockNH(w, o.bx, o.by, o.bz, cr, cg, cb, ct)) continue;
					if (ct == kTypeInteractive) continue;   // schon gesetzt
					if (SetBlockNH(w, o.bx, o.by, o.bz, cr, cg, cb, kTypeInteractive))
						fixed++;
				}
			}
		}
		return fixed;
	}


	// Zeichnet eine Ziffer stehend: Breite laeuft in +X, Hoehe in +Z.
	static void DrawGlyph(cube::World* w, int gx, int gy, int gz, int nibble) {
		if (nibble < 0 || nibble > 15) return;
		for (int row = 0; row < 5; row++) {
			unsigned char bits = kGlyph[nibble][row];
			for (int col = 0; col < 3; col++) {
				if (!(bits & (4 >> col))) continue;
				// Zeile 0 ist oben -> von oben nach unten zeichnen.
				SetBlockNH(w, gx + col, gy, gz + (4 - row), 255, 230, 40, 1);
			}
		}
	}

	// Schreibt den Index als Hexzahl vor das Modell. Rueckgabe: Breite in Bloecken.
	static int DrawHexLabel(cube::World* w, int x, int y, int z, int value) {
		int digits[4]; int n = 0;
		if (value == 0) { digits[n++] = 0; }
		while (value > 0 && n < 4) { digits[n++] = value & 0xF; value >>= 4; }
		int cursor = 0;
		for (int i = n - 1; i >= 0; i--) {       // hoechstwertige Ziffer zuerst
			DrawGlyph(w, x + cursor, y, z, digits[i]);
			cursor += 4;                          // 3 breit + 1 Luecke
		}
		return cursor;
	}

	void BrowseModels(cube::GameController* gc) {
		if (!gc || !gc->local_player) { std::cout << "[Modelle] Kein Spieler." << std::endl; return; }
		uint32_t reg = FindModelRegistry(gc);
		if (!reg) { std::cout << "[Modelle] Keine Registry." << std::endl; return; }
		cube::World* w = &gc->world;

		const double U = 65536.0;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)U);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)U);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)U);

		// 3x3-Raster statt langer Reihe: 16 Modelle mal bis zu 45 Bloecken
		// Abstand waeren ueber 700 Bloecke -- alles jenseits von ~150 rendert
		// nicht mehr. Neun Stueck bleiben sicher im Blickfeld.
		const int kCount = 9;       // Modelle je Durchgang (3x3)
		const int kMaxIdx = 2600;

		// Erst sammeln, dann setzen: der Abstand muss sich nach dem groessten
		// Modell des Durchgangs richten, sonst verdeckt ein 35x35-Teil die Nachbarn.
		int idxs[9]; int ws[9]; int hs[9]; int n = 0;
		int scan = g_browseBase;
		int maxDim = 8;
		for (; scan < kMaxIdx && n < kCount; scan++) {
			void* t = (void*)RegistryAt(reg, (uint32_t)scan);
			int mw = 0, mh = 0;
			if (!t || !ReadModelSize(t, mw, mh)) continue;
			// Items (Waffen, Essen, Ruestung) sind klein und wuerden die Reihe
			// mit tausenden Eintraegen fluten -- die interessieren hier nicht.
			if (mw < g_browseMinDim && mh < g_browseMinDim) continue;
			idxs[n] = scan; ws[n] = mw; hs[n] = mh; n++;
			if (mw > maxDim) maxDim = mw;
			if (mh > maxDim) maxDim = mh;
		}

		if (n == 0) {
			std::cout << "[Modelle] Ab 0x" << std::hex << g_browseBase << std::dec
				<< " nichts mehr gefunden. Mit STRG+O von vorn." << std::endl;
			return;
		}

		const int gap = maxDim + 10;
		std::cout << "=== [Modelle] 0x" << std::hex << idxs[0] << " .. 0x" << idxs[n-1]
			<< std::dec << "  (" << n << " Stueck, Abstand " << gap << ") ===" << std::endl;

		int placed = 0;
		for (int i = 0; i < n; i++) {
			int bx = px + 24 + (i % 3) * gap;   // 3 Spalten nach +X
			int by = py + (i / 3) * gap;        // 3 Reihen nach +Y
			cube::Zone* zc = SafeZone(gc, bx, by);
			bool set = zc && PlaceModel(reg, zc, idxs[i], bx, by, pz, 0, g_argMode);

			// BESCHRIFTUNG: Hexindex als gelbe Blockziffern, stehend, auf der
			// -Y-Seite -- man laeuft die Reihe ab und liest sie an.
			int labelY = by - (hs[i] / 2) - 3;
			DrawHexLabel(w, bx - 6, labelY, pz + 1, idxs[i]);

			if (set) placed++;
			std::cout << "  0x" << std::hex << idxs[i] << std::dec
				<< "\tSpalte " << (i % 3 + 1) << "/Reihe " << (i / 3 + 1)
				<< "\t" << ws[i] << "x" << hs[i]
				<< (set ? "\tgesetzt" : "\tFEHLGESCHLAGEN") << std::endl;
		}

		RemeshAll(gc);
		g_browseBase = scan;
		std::cout << "  " << placed << "/" << n << " gesetzt. Naechster Druck ab 0x"
			<< std::hex << g_browseBase << std::dec
			<< "   (Filter: Kantenlaenge >= " << g_browseMinDim << ")" << std::endl;
	}

	void ResetBrowse(int base) { g_browseBase = base; }

	// Schaltet die Argumentkombination weiter und setzt EIN Testhaus (0x87E)
	// vor den Spieler. Verschwinden die bunten Marker und stehen stattdessen
	// Tueren und Fenster, ist die Kombination gefunden.
	void CycleArgs(cube::GameController* gc) {
		// mode 1 und 2 erzeugen Tueren (bestaetigt), aber keine mit Volumen.
		// Jetzt der systematische Durchlauf: ALLE mode-Werte, die der Code prueft
		// (1,2,3,4,5,6,7,9,10,0xB), jeweils mit flag 0 und 1, dazu am Ende vier
		// Varianten mit anderem kind und extra.
		struct Combo { unsigned char kind; int mode; char flag; int extra; };
		static const int kModes[10] = { 1, 2, 3, 4, 5, 6, 7, 9, 10, 11 };
		static Combo combos[24];
		static int nCombos = 0;
		if (nCombos == 0) {
			for (int m = 0; m < 10; m++) {
				for (int fl = 0; fl < 2; fl++) {
					combos[nCombos].kind = 6; combos[nCombos].mode = kModes[m];
					combos[nCombos].flag = (char)fl; combos[nCombos].extra = 0;
					nCombos++;
				}
			}
			// Vier Sonderfaelle: anderes kind, extra gesetzt.
			combos[nCombos].kind = 0x28; combos[nCombos].mode = 1; combos[nCombos].flag = 1; combos[nCombos].extra = 0; nCombos++;
			combos[nCombos].kind = 0x28; combos[nCombos].mode = 2; combos[nCombos].flag = 1; combos[nCombos].extra = 0; nCombos++;
			combos[nCombos].kind = 6;    combos[nCombos].mode = 1; combos[nCombos].flag = 1; combos[nCombos].extra = 1; nCombos++;
			combos[nCombos].kind = 0;    combos[nCombos].mode = 1; combos[nCombos].flag = 1; combos[nCombos].extra = 0; nCombos++;
		}

		static int sel = -1;
		sel = (sel + 1) % nCombos;
		g_argKind = combos[sel].kind;
		g_argMode = combos[sel].mode;
		g_argFlag = combos[sel].flag;
		g_argExtra = combos[sel].extra;

		std::cout << "[Args] " << (sel + 1) << "/" << nCombos
			<< "  kind=0x" << std::hex << (int)g_argKind << std::dec
			<< "  mode=" << g_argMode << "  flag=" << (int)g_argFlag
			<< "  extra=" << g_argExtra << std::endl;

		if (!gc || !gc->local_player) return;
		const double U = 65536.0;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)U);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)U);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)U);

		// IMMER 40 Bloecke in Blickrichtung -- nicht nach aussen gestaffelt.
		// Die frueheren Kombinationen 6-8 landeten bei +240 bis +280 Bloecken und
		// damit ausserhalb der Renderweite (~150), waren also gar nicht sichtbar.
		// So steht jedes Testhaus im Blickfeld; fuer den naechsten Versuch
		// einfach ein Stueck weitergehen.
		float yaw = gc->local_player->entity_data.rotation.Z;
		int bx = px + (int)(std::cos((double)yaw) * 40.0);
		int by = py + (int)(std::sin((double)yaw) * 40.0);
		bool okH = PlaceBuilding(gc, Native::House1, bx, by, pz, 0);

		// NUMMER DER KOMBINATION als gelbe Blockziffern davor -- auf halber
		// Strecke zwischen Spieler und Haus, damit sie nicht in der Wand steckt.
		int lx = px + (int)(std::cos((double)yaw) * 22.0);
		int ly = py + (int)(std::sin((double)yaw) * 22.0);
		DrawHexLabel(&gc->world, lx, ly, pz + 1, sel + 1);

		if (okH) {
			std::cout << "[Args] Testhaus Nr. " << (sel + 1) << " 40 Bloecke voraus bei Block("
				<< bx << "," << by << ") gesetzt." << std::endl;
			// Sofort messen, welche Blocktypen dabei entstanden sind.
			int mw = 0, mh = 0;
			ModelSize(gc, Native::House1, mw, mh);
			InspectBuilding(gc, bx, by, pz, mw, mh);
		}
		RemeshAll(gc);
	}

	void Probe(cube::GameController* gc) {
		if (!gc || !gc->local_player) { std::cout << "[HausProbe] Kein Spieler." << std::endl; return; }
		DryRun(gc);

		uint32_t reg = FindModelRegistry(gc);
		if (!reg) { std::cout << "[HausProbe] ABBRUCH: keine Registry." << std::endl; return; }
		void* tmpl = (void*)RegistryAt(reg, kModelFirst);
		if (!tmpl) { std::cout << "[HausProbe] ABBRUCH: Vorlage 0x8D0 ist leer." << std::endl; return; }

		// Zielpunkt: 25 Bloecke vor dem Spieler, damit nichts in ihm landet.
		const double U = 65536.0;
		int px = (int)(gc->local_player->entity_data.position.X / (int64_t)U);
		int py = (int)(gc->local_player->entity_data.position.Y / (int64_t)U);
		int pz = (int)(gc->local_player->entity_data.position.Z / (int64_t)U);
		float yaw = gc->local_player->entity_data.rotation.Z;
		int tx = px + (int)(std::cos((double)yaw) * 25.0);
		int ty = py + (int)(std::sin((double)yaw) * 25.0);

		cube::Zone* zone = SafeZone(gc, tx, ty);
		if (!zone) { std::cout << "[HausProbe] ABBRUCH: Zone nicht geladen." << std::endl; return; }

		int origin[3] = { tx, ty, pz };
		// param_8 ist KEIN Farbpuffer, sondern int[4] Beschnittraender auf die
		// Modell-Schleifengrenzen (WorldInfo.cpp:5359). Vanilla immer {0,0,0,0}.
		int clip[4] = { 0, 0, 0, 0 };

		std::cout << "[HausProbe] >>> RUFE placeStructure: Vorlage 0x" << std::hex
			<< (uint32_t)(uintptr_t)tmpl << ", Zone 0x" << (uint32_t)(uintptr_t)zone
			<< std::dec << ", Block(" << tx << "," << ty << "," << pz << ")" << std::endl;
		std::cout.flush();   // MUSS raus, bevor der Aufruf laeuft -- stuerzt das
		                     // Spiel ab, ist diese Zeile die letzte im Log und
		                     // beweist, dass es genau der Aufruf war.

		bool ok = CallPlaceStructure(tmpl, origin, zone, clip);
		RemeshAll(gc);

		std::cout << "[HausProbe] <<< zurueck, " << (ok ? "ohne Ausnahme." : "MIT Ausnahme (abgefangen).")
			<< std::endl;
		std::cout << "[HausProbe] Wenn nichts zu sehen ist: Gebiet einmal verlassen "
			<< "und neu laden -- placeStructure schreibt in die Zone, nicht ins Mesh."
			<< std::endl;
	}
}
