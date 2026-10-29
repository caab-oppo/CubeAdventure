#include "BlacksmithNpc.h"
#include "Host.h"
#include "MountainVillage.h"
#include "GemForge.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

#pragma intrinsic(_ReturnAddress)

// ===================================================================
// BlacksmithNpc.cpp -- Schmied-NPC in der L-Schmiede jedes Bergdorfs.
//
// Warum ein NPC (Creature) und kein Werkbank-Prop?
//   Die native [R]-Interaktion (GameController_updateSelectedEntity @0x4889e0)
//   arbeitet ueber den ENTITY-Pick-Baum, NICHT ueber injizierte static_object-
//   Props. Deshalb ist die injizierte CustomizationBench nur Deko. Ein echter
//   Creature-Entity IST pickbar -> ueber ihn oeffnen wir die UI.
//
// UI-Oeffnen (Versuch #1):
//   updateSelectedEntity ruft fuer entity+0x140==0x83 (separates Fenster,
//   mutmasslich "Weapon Customization") diese Sequenz auf:
//     p   = *(GC+0x8008f8); s = *(p+0x3c);
//     *(*(s+0x94) + *(s+0x68)*4) = 1;           // UI-Stack: Panel aktivieren
//     w   = *(GC+0x8008fc);
//     *(w+0x160)=-1; *(w+0x164)=-1; *(w+0x168)=1; // Fenster oeffnen
//   Wir bilden genau das nach (alles SEH-gesichert). Falls das falsche/kein
//   Fenster aufgeht, ist die Alternative der Craft-Zweig (0x80..0x82):
//   GC+0x8008d8=<station>; rebuildCraftState@0x4a2300; Fenster 0x8008c4/0x8008bc.
// ===================================================================

namespace BlacksmithNpc {

	// ---- SCHMIED-NPC ENTFERNT ------------------------------------------------
	// Die native CustomizeBench ist inzwischen selbst benutzbar: sie entsteht aus
	// dem Markerblock 0,0,127, bekommt Objekttyp 0x4D und haengt im Ortsindex
	// zone+0xAC -- damit greift die Zielsuche des Spiels und [R] oeffnet sie.
	// Der Schmied-NPC, seine Sprechblase und die Naeherungserkennung sind damit
	// ueberfluessig.
	//
	// DIESE DATEI BLEIBT TROTZDEM IM BUILD: sie haelt den Hook auf 0x588500,
	// aus dem GemForge::OnBenchDrawBegin/End gerufen wird. Ohne ihn werden die
	// Extra-Cubes ueber 32 in der Bench nicht gezeichnet.
	static const bool kSpawnSmith = false;


	static const int64_t kU = 65536;      // Positionseinheiten je Block
	static const int     kNearBlocks = 9; // [K]-Reichweite am Schmied
	static const double  kHouseRadius = 54.0; // == PlanBuild Hausradius
	static const double  kAng0 = 0.7853981634; // Haus k==0 (Schmiede) bei 45 Grad

	struct Post {
		int cx, cy, deckZ;
		int npcX, npcY, npcZ;
		cube::Creature* npcPtr;
		uint64_t npcGuid;
		bool spawned;
	};
	static std::vector<Post> g_posts;
	static int  g_active = -1;
	static bool g_near = false;
	
	// --- Werkbank-Props (Deko-Typ 0x4D) je Dorf -----------------------------
	// Reichweite bewusst klein: man soll AN der Bank stehen, nicht daneben.
	struct Bench { int x, y, z; };
	static std::vector<Bench> g_benches;
	static const int kBenchReach = 4;      // Blockradius um die Werkbank
	static int  g_activeBench = -1;
	static bool g_nearBench = false;

	void OnBenchPlaced(int blockX, int blockY, int blockZ) {
		for (size_t i = 0; i < g_benches.size(); i++)
			if (g_benches[i].x == blockX && g_benches[i].y == blockY) return; // schon bekannt
		Bench b; b.x = blockX; b.y = blockY; b.z = blockZ;
		g_benches.push_back(b);
		std::cout << "[Schmied] Werkbank registriert bei Block(" << blockX << ","
			<< blockY << "," << blockZ << ") -- [R] in " << kBenchReach
			<< " Bloecken Umkreis oeffnet die Customization Bench." << std::endl;
	}

	static uint32_t RebaseAddr(uint32_t staticAddr) {
		return Host::Re(staticAddr);
	}

	// Wie der Haendler (WanderingMerchant): hostility_flags=3 => mit [R] ansprechbar.
	// char_class bewusst NICHT auf 0x80 (das waere der Vendor-Dienst) -- der Schmied
	// oeffnet stattdessen unser Adaption/Weapon-Customization-Fenster (siehe OnUseKey).
	static void ConfigBlacksmith(cube::Creature* c) {
		__try {
			c->entity_data.hostility_flags = cube::util::Hostility::Talkable;  // [R]-Ziel
			c->entity_data.appearance.movement_flags &= (uint16_t)~0x0080; // Gate-Bit loeschen
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// ---- Schmied in der L-Schmiede (Haus k==0) spawnen -----------------------
	void OnVillageBuilt(cube::GameController* gc, int cx, int cy, int deckZ) {
		if (!gc || !gc->local_player) return;
		for (size_t i = 0; i < g_posts.size(); i++)
			if (g_posts[i].cx == cx && g_posts[i].cy == cy) return; // schon vorhanden

		// Schmiede-Hausmitte (wie PlanBuild): hx=cx+cos45*54, hy=cy+sin45*54.
		int hx = cx + (int)(std::cos(kAng0) * kHouseRadius);
		int hy = cy + (int)(std::sin(kAng0) * kHouseRadius);
		// Werkbank liegt in der offenen Bucht bei ~ (hx+6, hy). Schmied davor.
		Post p; memset(&p, 0, sizeof(p));
		p.cx = cx; p.cy = cy; p.deckZ = deckZ;
		// Vor die OFFENE Esse (Front = -y). Unter dem Bucht-Dach wuerde er beim Spawn
		// aufs Dach gesetzt; darum knapp vor die Dachkante auf freien Boden.
		p.npcX = hx + 6; p.npcY = hy - 6; p.npcZ = deckZ + 2;
		p.spawned = false; p.npcPtr = nullptr; p.npcGuid = 0;

		// NPC (KEIN Haustier): owner_guid = 0 -- sonst macht cube_world_init_companion()
		// den Schmied zum Begleiter und verdraengt das aktive Tier des Spielers.
		// Ansprechbar wird er allein ueber hostility_flags=3 (wie der Haendler).
		Vector3<int64_t> pos((int64_t)p.npcX * kU, (int64_t)p.npcY * kU, (int64_t)p.npcZ * kU);
		cube::Creature* c = cube::util::SpawnCreature(
			pos, cube::Creature::Race::HumanMale, cube::util::Hostility::Talkable, /*level=*/1, /*owner=*/0);
		if (c) {
			ConfigBlacksmith(c);
			p.npcPtr = c; p.npcGuid = (uint64_t)c->GUID; p.spawned = true;
			// std::cout << "[Schmied] NPC gespawnt in der Schmiede bei Block("
				// << p.npcX << "," << p.npcY << "," << p.npcZ << ")." << std::endl;
		} else {
			// std::cout << "[Schmied] WARN: NPC-Spawn fehlgeschlagen." << std::endl;
		}
		g_posts.push_back(p);
	}

	static int NearestPostToPlayer(cube::GameController* gc, int& outD2) {
		outD2 = 0x7fffffff;
		if (!gc || !gc->local_player) return -1;
		int px = (int)(gc->local_player->entity_data.position.X / kU);
		int py = (int)(gc->local_player->entity_data.position.Y / kU);
		int best = -1;
		for (size_t i = 0; i < g_posts.size(); i++) {
			int dx = g_posts[i].npcX - px, dy = g_posts[i].npcY - py;
			int d2 = dx * dx + dy * dy;
			if (d2 < outD2) { outD2 = d2; best = (int)i; }
		}
		return best;
	}

	static int NearestBenchToPlayer(cube::GameController* gc, int& outD2) {
		outD2 = 0x7fffffff;
		if (!gc || !gc->local_player) return -1;
		int px = (int)(gc->local_player->entity_data.position.X / kU);
		int py = (int)(gc->local_player->entity_data.position.Y / kU);
		int pz = (int)(gc->local_player->entity_data.position.Z / kU);
		int best = -1;
		for (size_t i = 0; i < g_benches.size(); i++) {
			int dz = g_benches[i].z - pz;
			if (dz < -4 || dz > 6) continue;              // andere Etage/Dach ignorieren
			int dx = g_benches[i].x - px, dy = g_benches[i].y - py;
			int d2 = dx * dx + dy * dy;
			if (d2 < outD2) { outD2 = d2; best = (int)i; }
		}
		return best;
	}

	// Aktiviert ein UI-Panel ueber den bewaehrten "UI-Stack-Push"-Mechanismus
	// (*(*(widget+0x3c)+0x94) [ *(widget+0x3c)+0x68 ] = 1). Genau so oeffnete der
	// 0x83-Zweig das Identification-Fenster im Test -> funktioniert nachweislich.
	static void PushOpenWidget(uint8_t* g, uint32_t gcOffset) {
		uint32_t widget = *(uint32_t*)(g + gcOffset);
		if (!widget) return;
		uint32_t sub = *(uint32_t*)(widget + 0x3c);
		if (!sub) return;
		uint32_t arr = *(uint32_t*)(sub + 0x94);
		uint32_t idx = *(uint32_t*)(sub + 0x68);
		if (arr) *(uint32_t*)(arr + idx * 4) = 1;
	}

	// ---- Native Weapon-Customization-UI oeffnen -------------------------------
	// Das Fenster ist eine cube::VoxelWidget bei GC+0x8008f4. Es wird vom UI-Loop
	// nur aktualisiert/gezeichnet, solange sein "Aktiv-Flag" gesetzt ist (der ueberall
	// genutzte Push-Mechanismus *(*(w+0x3c)+0x94)[+0x68]=1); dazu gehoert das
	// Interaktions-Panel GC+0x8008dc.
	//
	// KORREKTUR gegenueber der vorherigen Fassung: widget+0x160 ist KEIN "Modus",
	// sondern der Zeiger auf das zu bearbeitende Item. Belege:
	//   - cube::GameController::drawWeaponCustomization @0x588500:
	//       *(u32*)(this+0x174) = *(u32*)(this+0x170);          // Cube-Vorrat leeren
	//       if (*(int*)(this+0x160) == 0) goto ende;            // <-- bei 0 passiert NICHTS
	//       ... *(u8*)(*(int*)(this+0x160) + 0xd)               // Material der Waffe
	//       ... *(int*)(*(int*)(this+0x160) + 0x114)            // adaption_count
	//   - Klick-Handler (GameController.cpp @0x47d7xx):
	//       if (*pItem != 3) goto weiter;                       // muss eine Waffe sein
	//       *(Item**)(GC[0x8008f4] + 0x160) = pItem;            // Inventar-Klick bindet
	// Mit "= 0" konnte die Bench also gar nicht funktionieren. Wir binden hier
	// direkt die angelegte Waffe (GemForge uebernimmt das inkl. Store-Anlage).
	static void OpenCustomizationUI(cube::GameController* gc) {
		if (!gc) return;
		uint8_t* g = (uint8_t*)gc;
		__try {
			PushOpenWidget(g, 0x8008f4);                    // Widget aktiv -> sichtbar
			PushOpenWidget(g, 0x8008dc);                    // zugehoeriges Interaktions-Panel
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			std::cout << "[Schmied] FEHLER: UI-Open (0x8008f4) abgefangen (SEH)." << std::endl;
			return;
		}
		if (GemForge::BindEquippedWeaponToBench(gc)) {
			std::cout << "[Schmied] Weapon-Customization offen -- angelegte Waffe gebunden." << std::endl;
		} else {
			std::cout << "[Schmied] Weapon-Customization offen, aber KEINE Waffe angelegt. "
				<< "Waffe anlegen (oder im Inventar anklicken), dann erscheint das Modell." << std::endl;
		}
	}

	// [R]: nur wenn nah am Schmied -> native Weapon-Customization-UI oeffnen.
	// (Die fruehere B-Injektion oeffnete ein separates, nicht verknuepftes Inventar
	//  -> Absturz beim Editieren. Entfernt; die Customization-UI bringt ihr Panel selbst mit.)
	// [R]: an der Werkbank ODER beim Schmied -> Customization-UI oeffnen.
	// Die Werkbank hat Vorrang, weil man dort naeher dran steht.
	void OnUseKey(cube::GameController* gc) {
		if (g_nearBench && g_activeBench >= 0) { OpenCustomizationUI(gc); return; }
		if (g_near && g_active >= 0)           { OpenCustomizationUI(gc); return; }
		// Kein Log: [R] ist die allgemeine Interaktionstaste und wird staendig
		// gedrueckt -- eine Meldung hier waere reiner Konsolen-Spam.
	}

	bool GetBubble(cube::GameController* gc, std::string& outText,
		int& outBlockX, int& outBlockY, int& outBlockZ) {
		if (g_nearBench && g_activeBench >= 0 && g_activeBench < (int)g_benches.size()) {
			Bench& b = g_benches[g_activeBench];
			outText = "Werkbank -- [R]: Cubes setzen (100 je Sorte, max. 500)";
			outBlockX = b.x; outBlockY = b.y; outBlockZ = b.z + 2;
			return true;
		}
		if (!g_near || g_active < 0 || g_active >= (int)g_posts.size()) return false;
		Post& p = g_posts[g_active];
		outText = "Schmied -- [R]: Waffe anpassen (Customization Bench)";
		outBlockX = p.npcX; outBlockY = p.npcY; outBlockZ = p.npcZ + 2;
		return true;
	}

	void OnWorldReload() {
		g_posts.clear();
		g_benches.clear();
		g_active = -1; g_near = false;
		g_activeBench = -1; g_nearBench = false;
		std::cout << "[Schmied] Weltwechsel -- Schmiede-Posten und Werkbaenke vergessen." << std::endl;
	}

	class SmithMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override {
			if (!gc || !gc->local_player) return;
			if (!g_posts.empty()) {
				int d2 = 0; int idx = NearestPostToPlayer(gc, d2);
				g_active = idx;
				g_near = (idx >= 0 && d2 <= kNearBlocks * kNearBlocks);
			}
			if (!g_benches.empty()) {
				int bd2 = 0; int bidx = NearestBenchToPlayer(gc, bd2);
				g_activeBench = bidx;
				g_nearBench = (bidx >= 0 && bd2 <= kBenchReach * kBenchReach);
			}
		}
	};
	static SmithMod g_mod;

	// ===================================================================
	// BENCH-SCANNER: hookt die VoxelWidget-Update-Funktion (FUN_00588500 @0x588500).
	// Diese laeuft nur, waehrend die native Werkbank-UI ("Weapon Customization" /
	// "Upgrades") offen ist (VoxelWidget, NICHT das NPC-Panel-System). Beim Oeffnen
	// loggt der Hook: self-Pointer, GC-Pointer (self+0x298), Modus (self+0x160:
	// 0 = Weapon Customization), das GC-Offset am dem der Widget-Pointer haengt,
	// und die Aufrufer-Adresse (RVA). Damit kennen wir danach den exakten Oeffnen-Pfad.
	// ===================================================================
	typedef void(__fastcall* Fn_VoxelUpdate)(void* self, void* edx);
	static Fn_VoxelUpdate g_origVoxelUpdate = nullptr;
	static DWORD g_lastBenchLog = 0;

	// STILLGELEGT. Der Scanner hat seinen Zweck erfuellt (Widget haengt an
	// GC+0x8008F4); er lief bei offener Bench alle 1,5 s und wuerde jetzt nur
	// die GemForge-Diagnose zuschuetten. Wird nicht mehr aufgerufen.
	static void ScanBench(void* self, void* caller) {
		(void)self; (void)caller;
		__try {
			// uint8_t* s = (uint8_t*)self;
			// uint32_t selfAddr = (uint32_t)(uintptr_t)self;
			// uint32_t gc = *(uint32_t*)(s + 0x298);   // VoxelWidget+0x298 = GameController
			// uint32_t mode = *(uint32_t*)(s + 0x160); // +0x160 = Item-Zeiger
			// Der 8-MB-Scan diente nur dazu, das GC-Offset des Widgets zu finden
			// (Ergebnis: GC+0x8008F4, laengst bekannt). Er lief bei OFFENER Bench
			// alle 1,5 s -- also genau dann, wenn die GemForge-Diagnose lesbar
			// sein soll. Komplett stillgelegt.
			// int foundOff = -1;
			// if (gc) {
			//     for (uint32_t off = 0; off < 0x810000; off += 4) {
			//         if (*(uint32_t*)(gc + off) == selfAddr) { foundOff = (int)off; break; }
			//     }
			// }
			// uint32_t base = (uint32_t)GetModuleHandleA(NULL);
			// uint32_t callerRva = (uint32_t)(uintptr_t)caller - base + 0x400000;
			// std::cout << "[BenchScan] Werkbank-UI offen: self=0x" << std::hex << selfAddr
			//     << " GC=0x" << gc << " mode(+0x160)=0x" << mode;
			// if (foundOff >= 0) std::cout << " -> GC+0x" << foundOff;
			// else               std::cout << " -> GC-Offset NICHT gefunden";
			// std::cout << " caller=0x" << callerRva << std::dec << std::endl;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[BenchScan] SEH beim Scannen abgefangen." << std::endl;
		}
	}

	// MinHook laesst nur EINEN Hook je Adresse zu -- deshalb klammert dieser
	// bereits vorhandene Hook zusaetzlich die GemForge-Vorschau ein. Nur
	// zwischen Begin und End darf GemForge in die Cube-Schleife der Bench
	// hineinzeichnen (siehe GemForge.cpp, Hook_Upload).
	static void __fastcall Hook_VoxelUpdate(void* self, void* edx) {
		// Scanner-Aufruf entfernt (siehe ScanBench) -- er lief pro Frame und
		// hat die Konsole zugeschuettet.
		// void* caller = _ReturnAddress();
		// DWORD now = GetTickCount();
		// if (self && (now - g_lastBenchLog > 1500)) { g_lastBenchLog = now; ScanBench(self, caller); }
		GemForge::OnBenchDrawBegin(self);
		if (g_origVoxelUpdate) g_origVoxelUpdate(self, edx);
		GemForge::OnBenchDrawEnd();
	}

	// Zweiter Hook: VoxelWidget-Konstruktor (0x587f70). Feuert, wenn die Werkbank-UI
	// erzeugt wird. Aufrufer (RVA) = der Code, der die native Werkbank oeffnet.
	typedef void(__fastcall* Fn_VoxelCtor)(void* self, void* edx, uint32_t p1, uint32_t p2, uint32_t owner);
	static Fn_VoxelCtor g_origVoxelCtor = nullptr;

	static void __fastcall Hook_VoxelCtor(void* self, void* edx, uint32_t p1, uint32_t p2, uint32_t owner) {
		void* caller = _ReturnAddress();
		uint32_t base = (uint32_t)GetModuleHandleA(NULL);
		// std::cout << "[BenchScan] VoxelWidget ERZEUGT: self=0x" << std::hex << (uint32_t)(uintptr_t)self
			// << " owner(GC)=0x" << owner
			// << " caller(RVA)=0x" << ((uint32_t)(uintptr_t)caller - base + 0x400000)
			// << std::dec << std::endl;
		if (g_origVoxelCtor) g_origVoxelCtor(self, edx, p1, p2, owner);
	}

	static void InstallBenchScanner() {
		void* tUpd = (void*)(uintptr_t)RebaseAddr(0x00588500);
		// Rueckgabewert bewusst verworfen: die beiden Meldungen sind stumm
		// geschaltet, das if/else darf aber nicht leer stehen bleiben.
		(void)MH_CreateHook(tUpd, (LPVOID)&Hook_VoxelUpdate,
			reinterpret_cast<LPVOID*>(&g_origVoxelUpdate));
		// std::cout << "[BenchScan] Hook auf VoxelWidget-Update (0x588500) erstellt." << std::endl;
		// std::cout << "[BenchScan] WARN: Update-Hook fehlgeschlagen." << std::endl;

		void* tCtor = (void*)(uintptr_t)RebaseAddr(0x00587f70);
		(void)MH_CreateHook(tCtor, (LPVOID)&Hook_VoxelCtor,
			reinterpret_cast<LPVOID*>(&g_origVoxelCtor));
		// std::cout << "[BenchScan] Hook auf VoxelWidget-Ctor (0x587f70) erstellt." << std::endl;
		// std::cout << "[BenchScan] WARN: Ctor-Hook fehlgeschlagen." << std::endl;
	}

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_mod);
		InstallBenchScanner();  // MH_EnableHook(MH_ALL_HOOKS) in main.cpp aktiviert ihn.
		std::cout << "[Schmied] Bereit -- [R] an der Werkbank oder beim Schmied." << std::endl;
	}
}
