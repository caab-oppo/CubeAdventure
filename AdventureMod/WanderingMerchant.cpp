#include "WanderingMerchant.h"
#include "Host.h"
#include "MobLab.h"
#include "Vendors.h"
#include <windows.h>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

// ===================================================================
// WanderingMerchant.cpp  (aufgeraeumte Fassung, 2026-07-12)
// -------------------------------------------------------------------
// Enthaelt nur noch den funktionierenden Kern:
//   - Auto-Spawn eines wandernden Haendlers (char_class 0x80, native
//     [R]-Talk-Interaktion, +0x1b0/+0x1b4/+0x1b8 Katalog-IDs)
//   - Vendor-Katalog aus der Spieler-Ausruestung (Layout { amount; Item },
//     getrennte Puffer fuer GC+0x800c0c und Kreatur+0x11dc -- siehe
//     Einarbeitung_Haendler_Inventar.md im Hauptordner fuer die 3 Bugfixes)
//   - Zone-Hook (Gebaeude-Anker) + Schild-Experiment
//   - GUID-Tracking-API (TrackEntity/TrackPlayer/TrackMerchant)
// Die frueheren Experimente (native Item-Konstruktion FUN_00595010,
// Session-wares-Injektion, Byte-Vergleichs-Debugtasten K/L/M/N/H/P) sind
// entfernt -- sie waren tote Sackgassen (siehe RE_Notizen_Haendler.md).
// ===================================================================

namespace AdventureMod {

	// 1 Block = 65536 Positionseinheiten (Vector3<int64_t>).
	static const int64_t kBlockUnits = 65536;

	// ---------------------------------------------------------------
	// Haendler-Zustand -- bewusst NICHT in der cube::Creature-Struktur
	// gespeichert, sondern separat im Mod ueber Creature*+GUID verknuepft.
	// ---------------------------------------------------------------
	static bool             g_hasMerchant = false;
	static cube::Creature*  g_merchantPtr = nullptr;
	static uint64_t         g_merchantGuid = 0;
	static bool SafeReadPosition(cube::Creature* c, int64_t* x, int64_t* y, int64_t* z);   // unten

	static const int64_t kZoneSizeBlocks = 256; // zoneCoord = blockCoord / 256 (empirisch)
	static int ComputeZoneCoord(int64_t posUnits) {
		return (int)(posUnits / (kBlockUnits * kZoneSizeBlocks));
	}

	// ---------------------------------------------------------------
	// Adressierung: Ghidra-Adressen sind relativ zu ImageBase 0x400000.
	// Zur Laufzeit reloziert Cube.exe -> immer ueber RebaseAddr() rechnen.
	// ---------------------------------------------------------------
	using Fn_OperatorNew_t = void* (__cdecl*)(unsigned int size); // game-eigenes operator new (0x0068d668)

	static uint32_t RebaseAddr(uint32_t staticAddr) {
		return Host::Re(staticAddr);
	}

	static int SehLogFilter(unsigned int code, struct _EXCEPTION_POINTERS* ep) {
		uint32_t imageBase = (uint32_t)GetModuleHandleA(NULL);
		uint32_t crashAddr = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
		uint32_t staticAddr = crashAddr - imageBase + 0x400000;
		// std::cout << "  [SEH] Code=0x" << std::hex << code
			// << " Adresse=0x" << crashAddr
			// << " (statisch/Ghidra-Adresse=0x" << staticAddr << ")" << std::dec << std::endl;
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// ---------------------------------------------------------------
	// Chat-Ausgabe (Spawn-Meldung). Native chatwidget_print @0x0043ab30
	// (__thiscall(this=ChatWidget*, wstring*, Color*) -- Arg-Reihenfolge per
	// Ghidra-Aufrufer bestaetigt). Die Adresse ist aus dem cwsdk uebernommen
	// und nur bedingt verifiziert -> KOMPLETT SEH-gekapselt: schlimmstenfalls
	// erscheint keine Meldung, aber es kann NICHT abstuerzen.
	// ---------------------------------------------------------------
	using Fn_WstrAssign_t = void* (__thiscall*)(void* this_, const wchar_t* str); // FUN_0040eb60
	using Fn_WstrFree_t   = void  (__fastcall*)(void* container);                 // FUN_00593e50
	using Fn_ChatPrint_t  = void  (__thiscall*)(void* chatWidget, void* wstr, void* color); // FUN_0043ab30

	static void PostChatMessage(cube::GameController* gc, const wchar_t* text) {
		__try {
			if (!gc || !gc->ChatWidget) return;
			uint8_t wstr[0x18];
			memset(wstr, 0, sizeof(wstr));
			((Fn_WstrAssign_t)RebaseAddr(0x0040eb60))(wstr, text);
			Color col(1.0f, 0.85f, 0.2f, 1.0f); // gold/gelb
			((Fn_ChatPrint_t)RebaseAddr(0x0043ab30))((void*)gc->ChatWidget, wstr, &col);
			((Fn_WstrFree_t)RebaseAddr(0x00593e50))(wstr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Chat-Ausgabe nicht verfuegbar (Adresse abweichend) -- still ignorieren.
		}
	}

	// ===============================================================
	// Vendor-Katalog
	// ===============================================================
	// Uebernimmt ein getragenes Spieler-Item 1:1 in den Katalog, mit drei
	// Anpassungen:
	//   1. Voxel-Adaptionen nullen (adaption_count/adapted/customization_data)
	//      -- sonst laeuft die native Namens-/Tooltip-Schleife FUN_00598a50
	//      ueber den Puffer hinaus -> Absturz (siehe Doku, Crash-Ursache 2).
	//   2. Powerlevel = Charakterlevel des Spielers.
	//   3. Rarity eine Stufe ueber der aktuell getragenen (max 5).
	// Name/Preis/Icon haengen NUR an category_id/item_id/rarity/material/level,
	// daher sind diese Anpassungen gefahrlos.
	static bool CopyPlayerSlotItem(const Item& playerSlot, uint16_t playerLevel, Item& out) {
		if (playerSlot.category_id == 0 && playerSlot.item_id == 0) return false;
		out = playerSlot; // 1:1-Kopie
		out.adaption_count = 0;
		out.adapted = 0;
		memset(out.customization_data, 0, sizeof(out.customization_data));
		out.level = playerLevel;
		out.rarity = (uint8_t)(playerSlot.rarity < 5 ? playerSlot.rarity + 1 : 5);
		// material NICHT ueberschreiben -- das echte Material des Spieler-Items
		// bleibt erhalten (es bestimmt Modell/Look; frueher faelschlich auf
		// rarity gesetzt -> Rarity 5 wurde zu Material 5 = Obsidian).
		return true;
	}

	// Legt EINEN in sich geschlossenen Vendor-Katalog an (eigener Item-Puffer +
	// eigener 1-Kategorie-Puffer) und liefert dessen Kategorie-Vektor
	// (catStart/catEnd). Slot-Layout: { int32 amount; Item item; } = 0x11C
	// (amount ZUERST, Item ab +4 -- bewiesen aus FUN_004c2050, Vendor-Modus).
	static bool AllocOneCatalog(const Item* itemsData, size_t itemCount,
		uint32_t& outCatStart, uint32_t& outCatEnd) {
		const size_t kItemStride = sizeof(int32_t) + sizeof(Item); // amount + Item = 0x11C
		size_t itemBytes = itemCount * kItemStride;

		void* itemBuffer = ((Fn_OperatorNew_t)RebaseAddr(0x0068d668))((unsigned int)itemBytes);
		if (!itemBuffer) return false;
		memset(itemBuffer, 0, itemBytes);
		uint8_t* cursor = (uint8_t*)itemBuffer;
		for (size_t i = 0; i < itemCount; i++) {
			*(int32_t*)cursor = 1;                                          // amount = 1 (Offset 0)
			memcpy(cursor + sizeof(int32_t), &itemsData[i], sizeof(Item));  // Item ab +4
			cursor += kItemStride;
		}
		uint32_t itemStart = (uint32_t)(uintptr_t)itemBuffer;
		uint32_t itemEnd = itemStart + (uint32_t)itemBytes;

		void* catBuffer = ((Fn_OperatorNew_t)RebaseAddr(0x0068d668))(0xC);
		if (!catBuffer) return false;
		*(uint32_t*)((uint8_t*)catBuffer + 0) = itemStart;
		*(uint32_t*)((uint8_t*)catBuffer + 4) = itemEnd;
		*(uint32_t*)((uint8_t*)catBuffer + 8) = itemEnd; // feld3 (== itemArrayEnd beobachtet)

		outCatStart = (uint32_t)(uintptr_t)catBuffer;
		outCatEnd = outCatStart + 0xC;
		return true;
	}

	// Schreibt den Katalog nach Kreatur+0x11dc (eigener Puffer, Spiel-Allokator).
	// POD-only + __try (kein C++-Objekt mit Destruktor im Scope, C2712).
	static bool ApplyCustomVendorCatalog(cube::GameController* gc, const Item* itemsData, size_t itemCount) {
		__try {
			// Puffer 1 -> Kreatur+0x11dc: die QUELLE, aus der FUN_004a2300 beim
			// [R]-Oeffnen nach GC+0x800c0c kopiert (Source of Truth).
			uint32_t srcCatStart = 0, srcCatEnd = 0;
			if (g_hasMerchant && g_merchantPtr) {
				if (!AllocOneCatalog(itemsData, itemCount, srcCatStart, srcCatEnd)) {
					// std::cout << "  FEHLER: operator_new (Kreatur-Katalog) lieferte NULL." << std::endl;
					return false;
				}
				uint8_t* creatureBase = (uint8_t*)g_merchantPtr;
				*(uint32_t*)(creatureBase + 0x11dc) = srcCatStart;
				*(uint32_t*)(creatureBase + 0x11e0) = srcCatEnd;
				*(uint32_t*)(creatureBase + 0x11e4) = srcCatEnd; // Cap
			}

			// GC+0x800c0c (aktiver Laden) wird NICHT mehr beschrieben: war gerade ein anderer Laden offen, zeigte
			// dessen Fenster danach auf unseren Puffer. Das Spiel kopiert Kreatur+0x11dc beim [R]-Oeffnen selbst.
			(void)gc;
			return true;
		} __except (SehLogFilter(GetExceptionCode(), GetExceptionInformation())) {
			// std::cout << "  FEHLER: Custom-Katalog-Aufbau abgestuerzt (SEH abgefangen)." << std::endl;
			return false;
		}
	}

	bool BuildCustomVendorCatalog(cube::GameController* gc) {
		if (!gc->local_player) return false;

		auto& eq = gc->local_player->entity_data.equipment;
		uint16_t playerLevel = (uint16_t)gc->local_player->entity_data.level;

		std::vector<Item> items;
		Item tmp;

		auto addSlot = [&](const Item& playerSlot) {
			if (CopyPlayerSlotItem(playerSlot, playerLevel, tmp)) {
				items.push_back(tmp);
			}
		};

		addSlot(eq.right_weapon);
		addSlot(eq.left_weapon);
		addSlot(eq.left_ring);
		addSlot(eq.right_ring);
		addSlot(eq.necklace);
		addSlot(eq.shoulder);
		addSlot(eq.chest);
		addSlot(eq.hands);
		addSlot(eq.feet);
		addSlot(eq.light);

		if (items.empty()) {
			// std::cout << "[WanderingMerchant] Katalog: Spieler traegt keine Ausruestung, uebersprungen." << std::endl;
			return false;
		}

		return ApplyCustomVendorCatalog(gc, items.data(), items.size());
	}

	// ===============================================================
	// Spawn -- NUR an festen Haendlerposten (2026-10-13)
	// ---------------------------------------------------------------
	// Posten melden: Bergdorf (vor dem Turm), Elfenstadt (Aussichtsbalkon des Mutterbaums),
	// Zwergenstadt (Marktplatz). Es gibt EINEN Haendler; er steht am Posten, der dem Spieler am
	// naechsten ist (< 140 Bloecke). Kein Zufalls-Spawn mehr neben dem Spieler, keine Lebenszeit,
	// keine Schilder in fremden Zonen-Vektoren.
	// Absturz-Ursachen der alten Fassung (behoben):
	//  - g_merchantPtr wurde ohne Pruefung beschrieben (Wegraeumen beim Neusetzen/Ablauf). Hatte das Spiel
	//    die Kreatur inzwischen entfernt, schrieb das in freigegebenen Speicher. Jetzt nur noch ueber die
	//    GUID in der EntitesMap (MobLab::SinkCreature).
	//  - Der Katalog wurde zusaetzlich nach GC+0x800c0c (aktiver Laden) geschrieben -- auch wenn gerade ein
	//    ANDERER Laden offen war. Jetzt nur noch Kreatur+0x11dc; das Spiel kopiert ihn beim [R]-Oeffnen selbst.
	// ===============================================================
	struct MerchantPost { int64_t x, y, z; };          // Bloecke (Fuesse)
	static std::vector<MerchantPost> g_posts;
	static DWORD g_lastPostTick = 0, g_lastSpawnTry = 0;
	static bool  g_forceRespawn = false;

	void RegisterMerchantPost(int64_t bx, int64_t by, int64_t bz) {
		for (MerchantPost& p : g_posts)
			if (std::llabs(p.x - bx) < 8 && std::llabs(p.y - by) < 8) { p.z = bz; return; }
		g_posts.push_back({ bx, by, bz });
		std::cout << "[Haendler] Fester Posten bei (" << bx << "," << by << "," << bz << ") angemeldet ("
		          << g_posts.size() << " Posten)." << std::endl;
	}

	static bool ConfigVillageMerchantSafe(cube::Creature* m) {
		__try {
			m->entity_data.hostility_flags = cube::util::Hostility::Talkable;  // [R]-Ziel
			m->entity_data.char_class = (char)cube::util::CharClass::Vendor;   // echter Vendor
			m->entity_data.appearance.movement_flags &= (uint16_t)~0x0080;
			*(uint32_t*)((uint8_t*)m + 0x1b0) = 0x8024;         // Katalog-Sentinels
			*(uint32_t*)((uint8_t*)m + 0x1b4) = 0x8023;
			*(uint32_t*)((uint8_t*)m + 0x1b8) = 6;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Waren: LAB -> Haendler, Rolle "markt" (Strukturen\Haendler\markt.haendler). Leer -> wie frueher die
	// getragene Ausruestung des Spielers.
	// __try und Objekte mit Destruktor (VendorDef) nicht in derselben Funktion (C2712) -> getrennt
	static bool BuildPlayerCatalogSafe(cube::GameController* gc) {
		__try { BuildCustomVendorCatalog(gc); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool BuildCatalogSafe(cube::GameController* gc) {
		Vendors::VendorDef d;
		if (g_merchantPtr && Vendors::ForRole("markt", &d) && Vendors::ApplyTo(gc, g_merchantPtr, d)) return true;
		return BuildPlayerCatalogSafe(gc);
	}

	static void SpawnAtPost(cube::GameController* gc, const MerchantPost& p) {
		if (g_hasMerchant) MobLab::SinkCreature(gc, g_merchantGuid);    // nur wenn er per GUID noch existiert
		g_hasMerchant = false; g_merchantPtr = nullptr; g_merchantGuid = 0;
		Vector3<int64_t> pos(p.x * kBlockUnits + kBlockUnits / 2, p.y * kBlockUnits + kBlockUnits / 2, p.z * kBlockUnits);
		cube::Creature* merchant = cube::util::SpawnCreature(
			pos, cube::Creature::Race::HumanMale, cube::util::Hostility::Talkable, /*level=*/1, /*owner_guid=*/0);
		if (!merchant) return;
		ConfigVillageMerchantSafe(merchant);
		g_merchantPtr = merchant;                         // gueltig: gerade erzeugt (nur fuer den Katalog direkt hiernach)
		g_merchantGuid = (uint64_t)merchant->GUID;
		g_hasMerchant = true;
		BuildCatalogSafe(gc);
		g_merchantPtr = nullptr;                          // danach NIE mehr direkt benutzen -- immer ueber die GUID
		std::cout << "[Haendler] Am Posten (" << p.x << "," << p.y << "," << p.z << ") gesetzt." << std::endl;
	}

	static void Update(cube::GameController* gc) {
		if (!gc || !gc->local_player || g_posts.empty()) return;
		DWORD now = GetTickCount();
		if (now - g_lastPostTick < 2000) return;
		g_lastPostTick = now;
		int64_t px, py, pz;
		if (!SafeReadPosition(gc->local_player, &px, &py, &pz)) return;
		px /= kBlockUnits; py /= kBlockUnits; pz /= kBlockUnits;
		int best = -1; long long bestD = 140LL * 140LL;
		for (size_t i = 0; i < g_posts.size(); i++) {
			long long dx = g_posts[i].x - px, dy = g_posts[i].y - py, d = dx * dx + dy * dy;
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		if (best < 0) { g_forceRespawn = false; return; }
		const MerchantPost& p = g_posts[best];
		// Wer steht schon am Posten (Vendor 0x80, ansprechbar, +-4 Bloecke)?
		uint64_t here[8] = {};
		int n = MobLab::CountCreaturesNear(gc, p.x, p.y, p.z, 4, 6, 0x80, 3, here, 8);
		if (n > 0 && !g_forceRespawn) {
			bool mine = false;
			for (int i = 0; i < n && i < 8; i++) if (g_hasMerchant && here[i] == g_merchantGuid) mine = true;
			if (!mine) { g_merchantGuid = here[0]; g_hasMerchant = true; }
			for (int i = 0; i < n && i < 8; i++)
				if (here[i] != g_merchantGuid && MobLab::SinkCreature(gc, here[i]))
					std::cout << "[Haendler] Doppelten Haendler am Posten entfernt." << std::endl;
			return;
		}
		// Unser Haendler lebt noch woanders und der Spieler steht bei ihm (< 60 Bloecke) -> nicht wegholen.
		if (g_hasMerchant && !g_forceRespawn) {
			int64_t mx, my, mz;
			if (MobLab::CreaturePos(gc, g_merchantGuid, &mx, &my, &mz)) {
				double dx = (double)(mx / kBlockUnits - px), dy = (double)(my / kBlockUnits - py);
				if (dx * dx + dy * dy < 60.0 * 60.0) return;
			}
		}
		if (!g_forceRespawn && now - g_lastSpawnTry < 20000) return;   // hoechstens alle 20 s
		cube::Zone* z = nullptr;
		__try { z = gc->world.GetZone((int)(p.x >> 8), (int)(p.y >> 8)); } __except (EXCEPTION_EXECUTE_HANDLER) { z = nullptr; }
		if (!z) return;                                              // Zone noch nicht geladen
		g_lastSpawnTry = now;
		g_forceRespawn = false;
		SpawnAtPost(gc, p);
	}

	// LAB-Knopf: Haendler am naechsten festen Posten neu setzen (nur in < 140 Bloecken).
	void ForceSpawn(cube::GameController* gc) {
		(void)gc;
		g_forceRespawn = true;
		g_lastPostTick = 0;
		if (g_posts.empty()) std::cout << "[Haendler] Kein fester Posten bekannt (Bergdorf, Elfen- oder Zwergenstadt besuchen)." << std::endl;
	}

	// Alte Schnittstelle: meldet nur noch den Posten an, gesetzt wird im Tick.
	void SpawnMerchantAt(cube::GameController* gc, Vector3<int64_t> pos) {
		(void)gc;
		RegisterMerchantPost(pos.X / kBlockUnits, pos.Y / kBlockUnits, pos.Z / kBlockUnits);
	}

	// ===============================================================
	// GUID-Tracking-API (Basis fuer spaeteres Quest-/NPC-Tracking)
	// ===============================================================
	static WorldCoord MakeWorldCoord(const Vector3<int64_t>& raw) {
		WorldCoord c;
		c.raw = raw;
		c.blockX = raw.X / kBlockUnits;
		c.blockY = raw.Y / kBlockUnits;
		c.blockZ = raw.Z / kBlockUnits;
		c.zoneX = ComputeZoneCoord(raw.X);
		c.zoneY = ComputeZoneCoord(raw.Y);
		return c;
	}

	// Sucht eine Kreatur per GUID. Schnellpfade fuer Spieler und eigenen
	// Haendler; sonst iterative BST-Suche in gc->world.EntitesMap
	// (MSVCBinCompat::map<uint64_t, Creature*>). SEH-abgesichert, da die
	// Map zur Laufzeit vom Spiel veraendert werden kann.
	static cube::Creature* FindCreatureByGuid(cube::GameController* gc, uint64_t guid) {
		if (!gc) return nullptr;
		if (gc->local_player && (uint64_t)gc->local_player->GUID == guid) return gc->local_player;
		__try {
			auto* entMap = gc->world.EntitesMap;
			if (!entMap) return nullptr;
			return entMap->Find(guid);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	// Liest die Position einer Kreatur SEH-sicher in rohe int64-Felder.
	// BEWUSST separat: __try darf nicht in einer Funktion mit Objekten stehen,
	// die einen Destruktor haben (Vector3<> hat einen -> C2712). Hier nur
	// POD-Zeiger/Ints im Scope, daher __try erlaubt.
	static bool SafeReadPosition(cube::Creature* c, int64_t* x, int64_t* y, int64_t* z) {
		__try {
			*x = c->entity_data.position.X;
			*y = c->entity_data.position.Y;
			*z = c->entity_data.position.Z;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Kernfunktion: verfolgt eine beliebige Entity per GUID und liefert
	// Weltposition (roh + Block + Zone) sowie die Distanz zum Spieler.
	TrackResult TrackEntity(cube::GameController* gc, uint64_t guid) {
		TrackResult r;
		r.found = false;
		r.guid = guid;
		r.distanceBlocks = -1.0;
		memset(&r.pos, 0, sizeof(r.pos));

		cube::Creature* c = FindCreatureByGuid(gc, guid);
		if (!c) return r;

		int64_t ex, ey, ez;
		if (!SafeReadPosition(c, &ex, &ey, &ez)) return r;
		r.pos = MakeWorldCoord(Vector3<int64_t>(ex, ey, ez));
		r.found = true;

		if (gc && gc->local_player) {
			int64_t px, py, pz;
			if (SafeReadPosition(gc->local_player, &px, &py, &pz)) {
				double dx = (double)(ex - px) / (double)kBlockUnits;
				double dy = (double)(ey - py) / (double)kBlockUnits;
				double dz = (double)(ez - pz) / (double)kBlockUnits;
				r.distanceBlocks = std::sqrt(dx * dx + dy * dy + dz * dz);
			}
		}
		return r;
	}

	TrackResult TrackPlayer(cube::GameController* gc) {
		TrackResult r;
		r.found = false; r.guid = 0; r.distanceBlocks = 0.0;
		memset(&r.pos, 0, sizeof(r.pos));
		if (!gc || !gc->local_player) return r;
		return TrackEntity(gc, (uint64_t)gc->local_player->GUID);
	}

	TrackResult TrackMerchant(cube::GameController* gc) {
		TrackResult r;
		r.found = false; r.guid = 0; r.distanceBlocks = -1.0;
		memset(&r.pos, 0, sizeof(r.pos));
		if (!g_hasMerchant) return r;
		return TrackEntity(gc, g_merchantGuid);
	}

	uint64_t GetMerchantGuid() {
		return (g_hasMerchant ? g_merchantGuid : 0);
	}

	// ===============================================================
	// Framework-Anbindung
	// ===============================================================
	class WanderingMerchantMod : public AdventureFramework::IAdventureMod {
	public:
		void OnInitialize(cube::GameController* gc) override {
			(void)gc;   // kein Zufalls-Spawn mehr -- nur feste Posten (Update)
		}
		void OnGameTick(cube::GameController* gc) override {
			Update(gc);
		}
		void OnZoneGenerated(cube::GameController* gc, cube::World* world, int zone_x, int zone_y, cube::Zone* zone) override {
			(void)gc; (void)world; (void)zone_x; (void)zone_y; (void)zone;   // frueher Zonen-Instrumentierung (entfernt)
		}
	};

	static WanderingMerchantMod g_wanderingMerchantMod;

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_wanderingMerchantMod);
		// std::cout << "[WanderingMerchant] Initialisiert." << std::endl;
	}

}
