#include "AirshipTaxi.h"
#include "Host.h"
#include "AirshipSpawner.h"
#include "MountainVillage.h"
#include "MobLab.h"
#include "LabBridge.h"
#include "Items.h"
#include "LabStructure.h"
#include <fstream>
#include <string>
#include <windows.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>

// ===================================================================
// AirshipTaxi.cpp -- Turm-Vendor + Luftschiff-Taxi zwischen Bergdoerfern.
// Ablauf:
//   1) Dorfbau -> OnVillageBuilt: Vendor-NPC aufs Turmdach, 4-Item-Katalog
//      (Nord/Ost/Sued/West), jede Ware mit einem Richtungs-Marker im Item.
//   2) Spieler spricht NPC an -> Shop kopiert unseren Katalog nach GC+0x800c0c.
//      Wir erkennen den Marker -> Schiff dockt am Turm an (AirshipSpawner::DockAt).
//   3) Spieler kauft eine Ware -> Menge dieser Richtung sinkt -> Depart:
//      Zieldorf via MountainVillage::NextVillageInDirection, Abflug ~10s spaeter.
// Katalog-Layout (aus RE_Notizen_Haendler): Slot = { int32 amount; Item item }
// = 0x11C Byte; amount @slot+0, Item @slot+4. Vendor-Vektor an Creature+0x11dc
// (start/end/cap) bzw. aktiver Shop an GC+0x800c0c/0x800c10/0x800c14.
// ===================================================================

namespace AirshipTaxi {

	static const int64_t kU = 65536;          // Positionseinheiten je Block
	static const int     kTowerHeight = 40;   // = PlanTower height -> Dach bei deckZ+40
	static const int32_t kMarkerBase  = 0x5A110000; // "SAIL" | dir in Item.field_8
	static const DWORD   kBoardDelayMs = 10000;     // ~10 s nach Kauf abfliegen
	static const int     kNpcNearBlocks = 34;       // Overlay/Poll-Reichweite

	struct TaxiPost {
		int cx, cy, deckZ;        // Dorfmitte + Deckhoehe (Block)
		int topZ;                 // Turm-Dachhoehe = deckZ + kTowerHeight
		int npcX, npcY, npcZ;     // NPC-Standort (Block)
		int dockX, dockY, dockZ;  // Anlege-Position des Schiffs (ueber dem +X-Steg)
		uint64_t npcGuid;
		cube::Creature* npcPtr;
		bool spawned;
		int race;                 // Rasse des NPC (Stadt: Elf/Zwerg, Bergdorf: Mensch)
		DWORD lastTry;
	};
	static std::vector<TaxiPost> g_posts;

	// Kauf-Poll-Zustand.
	static int  g_prevGc[4]     = { -1,-1,-1,-1 }; // aktiver Shop (nur Diagnose)
	static int  g_prevV[4]      = { -1,-1,-1,-1 }; // Vendor-Quelle (nur Diagnose)
	static int  g_prevPlayer[4] = { -1,-1,-1,-1 }; // Spieler-Inventar (Kauf-Beweis)
	static int  g_pendingDeleteDir = -1;           // gekauftes Marker-Item nach Abflug loeschen
	static int  g_activePost   = -1;   // naechster Turm-Posten zum Spieler
	static bool g_bubbleActive = false;
	static const int kDockNearBlocks = 12; // so nah -> Schiff dockt an (wie "hingehen zum Reden")

	static uint32_t RebaseAddr(uint32_t staticAddr) {
		return Host::Re(staticAddr);
	}
	using Fn_New = void* (__cdecl*)(unsigned int);

	// ---- 4-Item-Richtungskatalog auf einen Vendor schreiben ------------------
	// Baut EINEN eigenen Item-Puffer (amount=1 je Slot) + 1-Kategorie-Puffer und
	// haengt ihn an Creature+0x11dc. Items = Kopie eines getragenen Spieler-Items
	// (garantiert gueltig/bepreist), je Richtung eigene Raritaet + Marker.
	static bool BuildTemplateItem(cube::GameController* gc, ::Item& out) {
		if (!gc || !gc->local_player) return false;
		auto& eq = gc->local_player->entity_data.equipment;
		const ::Item* cand[5] = { &eq.right_weapon, &eq.left_weapon, &eq.chest, &eq.hands, &eq.feet };
		for (int i = 0; i < 5; i++) {
			if (cand[i]->category_id != 0 || cand[i]->item_id != 0) { out = *cand[i]; return true; }
		}
		return false;
	}

	// ---- Eigene Waren je Richtung (LAB -> Gegenstaende -> Luftschiff-Taxi) -------------------
	// Strukturen\Gegenstaende\_luftschifftaxi.txt: nord/ost/sued/west = Kennung eines eigenen Gegenstands
	// (0 = wie bisher: Kopie der angelegten Waffe, Seltenheit = Richtung), ticket_behalten = 0/1.
	// Erkannt wird ein Kauf weiter am Marker in field_8 und zusaetzlich an der Kennung (Items, +4).
	static int  g_dirItem[4] = { 0, 0, 0, 0 };
	static bool g_keepTicket = false;
	static volatile LONG g_cfgVer = 1;
	static LONG g_cfgApplied = 0;
	static bool g_cfgLoaded = false;
	static std::string TaxiCfgPath() { return Items::ItemsDir() + "\\_luftschifftaxi.txt"; }
	static void LoadTaxiCfg() {
		g_cfgLoaded = true;
		std::ifstream f(TaxiCfgPath()); std::string line;
		static const char* key[4] = { "nord", "ost", "sued", "west" };
		while (std::getline(f, line)) {
			size_t eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
			std::string k = line.substr(0, eq); int v = atoi(line.c_str() + eq + 1);
			for (int d = 0; d < 4; d++) if (k == key[d]) g_dirItem[d] = v > 0 ? v : 0;
			if (k == "ticket_behalten") g_keepTicket = v != 0;
		}
	}
	void GetDirectionItems(int ids[4], bool* keepTicket) {
		if (!g_cfgLoaded) LoadTaxiCfg();
		for (int d = 0; d < 4; d++) ids[d] = g_dirItem[d];
		if (keepTicket) *keepTicket = g_keepTicket;
	}
	bool SetDirectionItems(const int ids[4], bool keepTicket) {
		for (int d = 0; d < 4; d++) g_dirItem[d] = ids[d] > 0 ? ids[d] : 0;
		g_keepTicket = keepTicket; g_cfgLoaded = true;
		InterlockedIncrement(&g_cfgVer);                       // Spielthread setzt die Kataloge neu
		Lab::EnsureDir(Lab::StructuresDir()); Lab::EnsureDir(Items::ItemsDir());
		std::ofstream f(TaxiCfgPath());
		if (!f) return false;
		f << "# Luftschiff-Taxi: Ware je Richtung = Kennung eines eigenen Gegenstands (0 = Kopie der angelegten Waffe)\n";
		f << "nord=" << g_dirItem[0] << "\nost=" << g_dirItem[1] << "\nsued=" << g_dirItem[2] << "\nwest=" << g_dirItem[3] << "\n";
		f << "# ticket_behalten: 1 = gekaufte Ware bleibt im Inventar, 0 = wird nach dem Abflug entfernt (Fahrkarte)\n";
		f << "ticket_behalten=" << (g_keepTicket ? 1 : 0) << "\n";
		return true;
	}
	static bool ApplyCatalogRaw(cube::GameController* gc, cube::Creature* vendor, const uint8_t (*custom)[0x118], const bool* has);
	static bool ApplyDirectionCatalog(cube::GameController* gc, cube::Creature* vendor) {
		if (!g_cfgLoaded) LoadTaxiCfg();
		uint8_t custom[4][0x118]; bool has[4] = { false, false, false, false };
		for (int d = 0; d < 4; d++) {
			Items::ItemDef def;
			if (g_dirItem[d] > 0 && Items::ById(g_dirItem[d], &def) && def.enabled) { Items::FillItem(def, custom[d]); has[d] = true; }
		}
		return ApplyCatalogRaw(gc, vendor, custom, has);
	}
	static bool ApplyCatalogRaw(cube::GameController* gc, cube::Creature* vendor, const uint8_t (*custom)[0x118], const bool* has) {
		__try {
			::Item tmpl;
			bool allCustom = has[0] && has[1] && has[2] && has[3];
			if (!BuildTemplateItem(gc, tmpl) && !allCustom) {
				// std::cout << "[AirshipTaxi] Katalog: Spieler traegt keine Ausruestung -> kein Vendor." << std::endl;
				return false;
			}
			uint16_t plvl = (uint16_t)gc->local_player->entity_data.level;

			const size_t stride = sizeof(int32_t) + sizeof(::Item); // 0x11C
			const int N = 4;
			void* itemBuf = ((Fn_New)RebaseAddr(0x0068d668))((unsigned int)(N * stride));
			if (!itemBuf) return false;
			memset(itemBuf, 0, N * stride);
			uint8_t* cur = (uint8_t*)itemBuf;
			for (int dir = 0; dir < N; dir++) {
				::Item it = tmpl;
				if (has[dir]) {                       // eigener Gegenstand (Name/Modell/Preis aus seiner Definition)
					memcpy(&it, custom[dir], sizeof(::Item));
					it.field_8 = kMarkerBase | dir;
					*(int32_t*)cur = 1;
					memcpy(cur + sizeof(int32_t), &it, sizeof(::Item));
					cur += stride;
					continue;
				}
				it.adaption_count = 0; it.adapted = 0;
				memset(it.customization_data, 0, sizeof(it.customization_data));
				it.level = 1;                         // BILLIG -> Kauf klappt sicher
				it.rarity = (uint8_t)(1 + dir);       // 1..4 -> optisch unterscheidbar
				it.field_8 = kMarkerBase | dir;       // Richtungs-Marker (unser Erkennungsfeld)
				(void)plvl;
				*(int32_t*)cur = 1;                   // amount = 1
				memcpy(cur + sizeof(int32_t), &it, sizeof(::Item));
				cur += stride;
			}
			uint32_t itemStart = (uint32_t)(uintptr_t)itemBuf;
			uint32_t itemEnd = itemStart + (uint32_t)(N * stride);

			void* catBuf = ((Fn_New)RebaseAddr(0x0068d668))(0xC);
			if (!catBuf) return false;
			*(uint32_t*)((uint8_t*)catBuf + 0) = itemStart;
			*(uint32_t*)((uint8_t*)catBuf + 4) = itemEnd;
			*(uint32_t*)((uint8_t*)catBuf + 8) = itemEnd;

			uint8_t* cb = (uint8_t*)vendor;
			*(uint32_t*)(cb + 0x11dc) = (uint32_t)(uintptr_t)catBuf;
			*(uint32_t*)(cb + 0x11e0) = (uint32_t)(uintptr_t)catBuf + 0xC;
			*(uint32_t*)(cb + 0x11e4) = (uint32_t)(uintptr_t)catBuf + 0xC;
			// std::cout << "[AirshipTaxi] 4-Richtungs-Katalog am Turm-Vendor gesetzt." << std::endl;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// std::cout << "[AirshipTaxi] FEHLER: Katalog-Aufbau abgefangen (SEH)." << std::endl;
			return false;
		}
	}

	static void ConfigVendorSafe(cube::Creature* c) {
		__try {
			c->entity_data.hostility_flags = cube::util::Hostility::Talkable;  // ansprechbar
			c->entity_data.char_class = (char)cube::util::CharClass::Vendor;   // echter Vendor
			c->entity_data.appearance.movement_flags &= (uint16_t)~0x0080;
			*(uint32_t*)((uint8_t*)c + 0x1b0) = 0x8024;         // Katalog-Sentinels
			*(uint32_t*)((uint8_t*)c + 0x1b4) = 0x8023;
			*(uint32_t*)((uint8_t*)c + 0x1b8) = 4;              // 4 Waren
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// ---- Vendor-NPC auf dem Turmdach spawnen ---------------------------------
	void OnVillageBuilt(cube::GameController* gc, int cx, int cy, int deckZ) {
		if (!gc || !gc->local_player) return;
		for (size_t i = 0; i < g_posts.size(); i++)
			if (g_posts[i].cx == cx && g_posts[i].cy == cy) return; // schon vorhanden

		TaxiPost p; memset(&p, 0, sizeof(p));
		p.cx = cx; p.cy = cy; p.deckZ = deckZ;
		p.topZ = deckZ + kTowerHeight;
		// NPC an der Turmbasis nahe dem Eingang (deckZ hat GARANTIERT Boden -> PlanTower
		// setzt Floor(x,y,deckZ) fuer jede Turmzelle). Gut sichtbar/erreichbar.
		p.npcX = cx; p.npcY = cy - 12; p.npcZ = deckZ + 1;
		// Anlege-Position: ueber der Dock-Plattform / dem Anlegemast des +X-Stegs
		// (PlanTower: Steg auf +X, Plattform cx+29, Mast bei cx+31,cy, Deck auf topZ).
		// ~31 Bloecke aus der Turmmitte, und der Schwebepunkt liegt ~5 Bloecke hoeher
		// (DockAt schwebt auf dockZ+5 -> topZ+10, klar ueber Mast + Signalfeuer).
		p.dockX = cx + 31; p.dockY = cy; p.dockZ = p.topZ + 5;
		p.spawned = false; p.npcPtr = nullptr; p.npcGuid = 0; p.race = (int)cube::Creature::Race::HumanMale; p.lastTry = 0;

		Vector3<int64_t> pos((int64_t)p.npcX * kU, (int64_t)p.npcY * kU, (int64_t)p.npcZ * kU);
		uint64_t here[1] = {};
		cube::Creature* c = nullptr;
		if (MobLab::CountCreaturesNear(gc, p.npcX, p.npcY, p.npcZ, 4, 6, 0x80, 3, here, 1) > 0) {   // steht schon einer da
			p.npcGuid = here[0]; p.npcPtr = MobLab::CreatureByGuid(gc, here[0]); p.spawned = p.npcPtr != nullptr;
			g_posts.push_back(p);
			return;
		}
		c = cube::util::SpawnCreature(
			pos, cube::Creature::Race::HumanMale, cube::util::Hostility::Talkable, /*level=*/1, /*owner=*/0);
		if (c) {
			ConfigVendorSafe(c);
			ApplyDirectionCatalog(gc, c);
			p.npcPtr = c; p.npcGuid = (uint64_t)c->GUID; p.spawned = true;
			// std::cout << "[AirshipTaxi] Turm-Taxi-NPC gespawnt bei Block(" << p.npcX << "," << p.npcY << "," << p.npcZ << ")." << std::endl;
		} else {
			// std::cout << "[AirshipTaxi] WARN: Turm-Taxi-NPC-Spawn fehlgeschlagen." << std::endl;
		}
		g_posts.push_back(p);
	}

	// Landeplatz einer Stadt (City.cpp, natives Modell 0x887): NPC auf dem Deck, Schiff schwebt neben dem Deck.
	// cx, cy = Stadtmitte (fuer die Zielsuche in Nord/Ost/Sued/West). Doppelte Meldungen werden ignoriert.
	bool AddLandingPost(cube::GameController* gc, int cx, int cy, int npcX, int npcY, int npcZ, int dockX, int dockY, int dockZ, int race) {
		if (!gc || !gc->local_player) return false;
		for (size_t i = 0; i < g_posts.size(); i++)
			if (g_posts[i].npcX == npcX && g_posts[i].npcY == npcY) return false;
		TaxiPost p; memset(&p, 0, sizeof(p));
		p.cx = cx; p.cy = cy; p.deckZ = npcZ; p.topZ = npcZ;
		p.npcX = npcX; p.npcY = npcY; p.npcZ = npcZ;
		p.dockX = dockX; p.dockY = dockY; p.dockZ = dockZ;
		p.race = race; p.lastTry = 0;
		// NICHT sofort spawnen: der Landeplatz wird beim ERZEUGEN seiner Zone gemeldet -- oft weit weg vom
		// Spieler, die Zone ist dann noch nicht geladen und der NPC fiel ins Leere. EnsureNpcs setzt ihn,
		// sobald der Spieler naeher als 160 Bloecke ist und die Zone geladen ist (und erneut, falls er fehlt).
		p.spawned = false; p.npcPtr = nullptr; p.npcGuid = 0;
		g_posts.push_back(p);
		return true;
	}
	static void EnsureNpcs(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 2000 || !gc || !gc->local_player) return;
		last = now;
		int px = (int)(gc->local_player->entity_data.position.X / kU), py = (int)(gc->local_player->entity_data.position.Y / kU);
		for (TaxiPost& p : g_posts) {
			long long dx = p.npcX - px, dy = p.npcY - py;
			if (dx * dx + dy * dy > 160LL * 160LL) continue;
			// Wer steht schon am Posten? (Vendor 0x80, ansprechbar, +-4 Bloecke). Frueher wurde nur die GUID
			// geprueft -- fand die Suche den NPC unter seiner GUID nicht, kam alle 8 s ein weiterer dazu.
			uint64_t here[8] = {};
			int n = MobLab::CountCreaturesNear(gc, p.npcX, p.npcY, p.npcZ, 4, 6, 0x80, 3, here, 8);
			if (n > 0) {
				bool mine = false;
				for (int i = 0; i < n && i < 8; i++) if (here[i] == p.npcGuid) mine = true;
				if (!mine) { p.npcGuid = here[0]; p.npcPtr = MobLab::CreatureByGuid(gc, here[0]); p.spawned = p.npcPtr != nullptr; }
				for (int i = 0; i < n && i < 8; i++)                                          // Doppelte wegraeumen
					if (here[i] != p.npcGuid && MobLab::SinkCreature(gc, here[i]))
						std::cout << "[AirshipTaxi] Doppelten Taxi-NPC bei (" << p.npcX << "," << p.npcY << ") entfernt." << std::endl;
				continue;
			}
			if (p.spawned && p.npcGuid) { int64_t x, y, z; if (MobLab::CreaturePos(gc, p.npcGuid, &x, &y, &z)) continue; }   // lebt, ist nur weggelaufen
			if (now - p.lastTry < 30000) continue;                                         // hoechstens alle 30 s neu setzen
			p.lastTry = now;
			if (!LabBridge::ZoneAt(gc, p.npcX >> 8, p.npcY >> 8)) continue;          // Zone noch nicht geladen
			Vector3<int64_t> pos((int64_t)p.npcX * kU + kU / 2, (int64_t)p.npcY * kU + kU / 2, (int64_t)p.npcZ * kU);
			cube::Creature* c = cube::util::SpawnCreature(pos, (cube::Creature::Race)p.race, cube::util::Hostility::Talkable, 1, 0);
			if (!c) continue;
			ConfigVendorSafe(c);
			ApplyDirectionCatalog(gc, c);
			p.npcPtr = c; p.npcGuid = (uint64_t)c->GUID; p.spawned = true;
			std::cout << "[AirshipTaxi] Taxi-NPC bei (" << p.npcX << "," << p.npcY << "," << p.npcZ << ") gesetzt." << std::endl;
		}
	}

	// Erkennt eines unserer 4 Richtungs-Items -> Richtung 0..3, sonst -1.
	// Primaer ueber den field_8-Marker; falls der beim Kauf genullt wird (bei cat=3
	// im Spieler-Inventar beobachtet), ueber die SIGNATUR: cat=3,id=5,mat=1,lvl=1,
	// rarity=1..4 (Richtung = rarity-1). Echte Spieler-Items sind hochstufig -> kein Konflikt.
	static int MarkerDir(const ::Item* it) {
		if (((uint32_t)it->field_8 & 0xFFFF0000) == kMarkerBase) return (int)(it->field_8 & 3);
		uint32_t mod = (uint32_t)it->modifier;                        // eigener Gegenstand als Ware (Kennung, auch ohne Marker)
		if ((mod & 0xFFFF0000u) == Items::kItemTag)
			for (int d = 0; d < 4; d++) if (g_dirItem[d] > 0 && (int)(mod & 0xFFFFu) == g_dirItem[d]) return d;
		if (it->category_id == 3 && it->item_id == 5 && it->material == 1 && it->level == 1
			&& it->rarity >= 1 && it->rarity <= 4) return (int)it->rarity - 1;
		return -1;
	}
	// Zaehlt unsere Richtungs-Items im Katalog/Inventar (Kategorie-Vektor [catStart,catEnd),
	// je 0xC; Items = { int32 amount; Item } = 0x11C). cnt[dir] = Anzahl. true = irgendetwas gefunden.
	static bool ReadMarkerCounts(uint32_t catStart, uint32_t catEnd, int cnt[4]) {
		for (int k = 0; k < 4; k++) cnt[k] = 0;
		bool ours = false;
		__try {
			if (catStart == 0 || catEnd <= catStart) return false;
			uint32_t ccount = (catEnd - catStart) / 0xC;
			if (ccount == 0 || ccount > 16) return false;
			const uint32_t stride = (uint32_t)(sizeof(int32_t) + sizeof(::Item)); // 0x11C
			for (uint32_t c = 0; c < ccount; c++) {
				uint8_t* cat = (uint8_t*)(uintptr_t)(catStart + c * 0xC);
				uint32_t itemStart = *(uint32_t*)(cat + 0);
				uint32_t itemEnd   = *(uint32_t*)(cat + 4);
				if (itemStart == 0 || itemEnd <= itemStart) continue;
				uint32_t icount = (itemEnd - itemStart) / stride;
				if (icount > 60000) continue; // Sicherheitsgrenze (echte Inventare < ~3000)
				for (uint32_t i = 0; i < icount; i++) {
					::Item* it = (::Item*)(uintptr_t)(itemStart + i * stride + 4);
					int dir = MarkerDir(it);
					if (dir >= 0) { cnt[dir]++; ours = true; }
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		return ours;
	}
	// Aktiver Shop (GC+0x800c0c) bzw. Kreatur-Inventar (Creature+0x11dc) -- Kategorie-Vektoren.
	static bool ReadShopAmounts(cube::GameController* gc, int cnt[4]) {
		uint32_t start = 0, end = 0;
		__try { uint8_t* gb = (uint8_t*)gc; start = *(uint32_t*)(gb + 0x800c0c); end = *(uint32_t*)(gb + 0x800c10); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		return ReadMarkerCounts(start, end, cnt);
	}
	static bool ReadVendorAmounts(cube::Creature* v, int cnt[4]) {
		if (!v) { for (int k = 0; k < 4; k++) cnt[k] = 0; return false; }
		uint32_t start = 0, end = 0;
		__try { uint8_t* cb = (uint8_t*)v; start = *(uint32_t*)(cb + 0x11dc); end = *(uint32_t*)(cb + 0x11e0); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		return ReadMarkerCounts(start, end, cnt);
	}
	// Neutralisiert ALLE Marker-Items im Spieler-Inventar: Slot an Ort NULLEN
	// (amount=0 + Item leeren -> cat=0/id=0 = leerer Slot, wie es sie nativ gibt).
	// KEIN Vektor-Umbau (memmove/Ende verschieben) -> kein Dangling-Pointer-Crash.
	static int DeletePlayerMarkerItems(cube::Creature* player) {
		if (!player) return 0;
		int removed = 0;
		__try {
			uint8_t* cb = (uint8_t*)player;
			uint32_t catStart = *(uint32_t*)(cb + 0x11dc);
			uint32_t catEnd   = *(uint32_t*)(cb + 0x11e0);
			if (catStart == 0 || catEnd <= catStart) return 0;
			uint32_t ccount = (catEnd - catStart) / 0xC;
			if (ccount == 0 || ccount > 16) return 0;
			const uint32_t stride = (uint32_t)(sizeof(int32_t) + sizeof(::Item)); // 0x11C
			for (uint32_t c = 0; c < ccount; c++) {
				uint8_t* cat = (uint8_t*)(uintptr_t)(catStart + c * 0xC);
				uint32_t itemStart = *(uint32_t*)(cat + 0);
				uint32_t itemEnd   = *(uint32_t*)(cat + 4);
				if (itemStart == 0 || itemEnd <= itemStart) continue;
				uint32_t icount = (itemEnd - itemStart) / stride;
				if (icount > 60000) continue;
				for (uint32_t i = 0; i < icount; i++) {
					uint8_t* slot = (uint8_t*)(uintptr_t)(itemStart + i * stride);
					::Item* it = (::Item*)(slot + 4);
					if (MarkerDir(it) >= 0) {
						*(int32_t*)slot = 0;            // amount = 0 (leerer Slot)
						memset(it, 0, sizeof(::Item));  // Item leeren -> nicht mehr erkannt
						removed++;
					}
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { return removed; }
		return removed;
	}

	static int NearestPostToPlayer(cube::GameController* gc, int& outDistBlocks) {
		outDistBlocks = 1 << 30;
		if (!gc || !gc->local_player) return -1;
		int px = (int)(gc->local_player->entity_data.position.X / kU);
		int py = (int)(gc->local_player->entity_data.position.Y / kU);
		int best = -1;
		for (size_t i = 0; i < g_posts.size(); i++) {
			long long dx = g_posts[i].npcX - px, dy = g_posts[i].npcY - py;
			int d = (int)std::min<long long>(1LL << 30, dx * dx + dy * dy);
			if (d < outDistBlocks) { outDistBlocks = d; best = (int)i; }
		}
		return best;
	}

	// ---- Tick: Andocken beim Ansprechen, Depart beim Kauf ---------------------
	class TaxiMod : public AdventureFramework::IAdventureMod {
	public:
		void OnGameTick(cube::GameController* gc) override {
			if (!gc || !gc->local_player || g_posts.empty()) return;
			EnsureNpcs(gc);
			if (g_cfgApplied != g_cfgVer) {                              // LAB hat die Waren geaendert -> Kataloge neu
				g_cfgApplied = g_cfgVer;
				for (TaxiPost& p : g_posts) {
					int64_t x, y, z;
					if (p.spawned && p.npcPtr && MobLab::CreaturePos(gc, p.npcGuid, &x, &y, &z)) ApplyDirectionCatalog(gc, p.npcPtr);
				}
			}

			int d2 = 0; int postIdx = NearestPostToPlayer(gc, d2);
			g_bubbleActive = (postIdx >= 0 && d2 <= kNpcNearBlocks * kNpcNearBlocks);
			g_activePost = postIdx;

			// Andocken per NAEHE (zuverlaessig, unabhaengig von der Shop-Erkennung):
			// gehst du zum NPC, erscheint das Schiff am Turmdach.
			if (postIdx >= 0 && d2 <= kDockNearBlocks * kDockNearBlocks && !AirshipSpawner::IsBusy()) {
				TaxiPost& p = g_posts[postIdx];
				AirshipSpawner::DockAt(gc, (int64_t)p.dockX * kU, (int64_t)p.dockY * kU, (int64_t)p.dockZ * kU);
			}

			// KAUF-BEWEIS ueber das SPIELER-INVENTAR (Player+0x11dc, gleiche Tab-Struktur):
			// das gekaufte Marker-Item taucht dort auf -> zuverlaessig, egal wie der Vendor
			// intern zaehlt. Shop/Vendor werden nur noch zur Diagnose gelesen.
			cube::Creature* vptr = (postIdx >= 0) ? g_posts[postIdx].npcPtr : nullptr;
			int gcAmt[4]; bool gcOurs = ReadShopAmounts(gc, gcAmt);
			int vAmt[4];  bool vOurs  = ReadVendorAmounts(vptr, vAmt);
			int pAmt[4];  bool pOurs  = ReadVendorAmounts(gc->local_player, pAmt);

			static DWORD s_diag = 0; DWORD now = GetTickCount();
			if ((gcOurs || vOurs || pOurs) && now - s_diag > 1500) {
				s_diag = now;
				//std::cout << "[AirshipTaxi-DIAG] Shop=" << gcAmt[0] << "," << gcAmt[1] << "," << gcAmt[2] << "," << gcAmt[3]
					//<< " Vendor=" << vAmt[0] << "," << vAmt[1] << "," << vAmt[2] << "," << vAmt[3]
					//<< " Spieler=" << pAmt[0] << "," << pAmt[1] << "," << pAmt[2] << "," << pAmt[3] << std::endl;
			}

			// Kauf = Anzahl unserer Richtungs-Items im Spieler-Inventar ist gestiegen.
			int bought = -1;
			static bool s_playerBaselined = false;
			if (s_playerBaselined) {
				for (int dir = 0; dir < 4; dir++)
					if (pAmt[dir] > g_prevPlayer[dir]) bought = dir;
			}
			for (int k = 0; k < 4; k++) g_prevPlayer[k] = pAmt[k];
			s_playerBaselined = true;

			if (bought >= 0 && postIdx >= 0 && AirshipSpawner::IsDocked()) {
				TaxiPost& p = g_posts[postIdx];
				int tx, ty;
				if (MountainVillage::NextVillageInDirection(gc, p.cx, p.cy, bought, tx, ty)) {
					AirshipSpawner::Depart(gc, (int64_t)tx * kU, (int64_t)ty * kU, kBoardDelayMs);
					g_pendingDeleteDir = bought;
					const char* dn[4] = { "Norden","Osten","Sueden","Westen" };
					// std::cout << "[AirshipTaxi] Kauf erkannt (Inventar): Richtung " << dn[bought]
						// << " -> Zieldorf Block(" << tx << "," << ty << ")." << std::endl;
				}
			}

			// Nach dem Ablegen (Schiff fliegt) das gekaufte Marker-Item wieder entfernen.
			if (g_pendingDeleteDir >= 0 && AirshipSpawner::IsCruising()) {
				int del = g_keepTicket ? 0 : DeletePlayerMarkerItems(gc->local_player);   // eigene Ware darf bleiben
				g_pendingDeleteDir = -1;
				for (int k = 0; k < 4; k++) g_prevPlayer[k] = -1;
				// std::cout << "[AirshipTaxi] " << del << " Richtungs-Token nach Abflug aus Inventar geloescht." << std::endl;
			}
		}
	};
	static TaxiMod g_mod;

	bool GetBubble(cube::GameController* gc, std::string& outText,
		int& outBlockX, int& outBlockY, int& outBlockZ) {
		if (!g_bubbleActive || g_activePost < 0 || g_activePost >= (int)g_posts.size()) return false;
		TaxiPost& p = g_posts[g_activePost];
		outText = "Luftschiff-Taxi -- kauf dir eine Richtung!  (1=Nord 2=Ost 3=Sued 4=West)";
		outBlockX = p.npcX; outBlockY = p.npcY; outBlockZ = p.npcZ + 2;
		return true;
	}

	// ----Diagnose: Roh - Dump eines Kreatur - Inventars(Player oder Vendor) ------ -
	static void DumpCreatureInv(const char* who, cube::Creature* c) {
		if (!c) { return; }   // (Konsolenausgabe entfernt)
		__try {
			uint8_t* cb = (uint8_t*)c;
			uint32_t catStart = *(uint32_t*)(cb + 0x11dc);
			uint32_t catEnd   = *(uint32_t*)(cb + 0x11e0);
			// std::cout << "  " << who << " +0x11dc cat[0x" << std::hex << catStart << "..0x" << catEnd << "]" << std::dec;
			if (catStart == 0 || catEnd <= catStart) { return; }   // (Konsolenausgabe entfernt)
			uint32_t ccount = (catEnd - catStart) / 0xC;
			// std::cout << " tabs=" << ccount << std::endl;
			const uint32_t stride = (uint32_t)(sizeof(int32_t) + sizeof(::Item));
			for (uint32_t t = 0; t < ccount && t < 16; t++) {
				uint8_t* cat = (uint8_t*)(uintptr_t)(catStart + t * 0xC);
				uint32_t is = *(uint32_t*)(cat + 0), ie = *(uint32_t*)(cat + 4);
				uint32_t ic = (is && ie > is) ? (ie - is) / stride : 0;
				// std::cout << "    Tab" << t << " items=" << ic << " [0x" << std::hex << is << "..0x" << ie << "]" << std::dec << std::endl;
				// Erste 2 + LETZTE 12 (Kauf landet hinten) + alles mit unserem Marker/lvl<=2.
				for (uint32_t i = 0; i < ic; i++) {
					bool head = (i < 2);
					bool tail = (ic > 12 && i >= ic - 12);
					uint8_t* slot = (uint8_t*)(uintptr_t)(is + i * stride);
					::Item* it = (::Item*)(slot + 4);
					bool cand = (((uint32_t)it->field_8 & 0xFFFF0000) == kMarkerBase) || (it->level <= 2);
					if (!head && !tail && !cand) continue;
					// std::cout << "      #" << i << (cand ? "*" : " ") << " amt=" << *(int32_t*)slot
						// << " cat=" << (int)it->category_id << " id=" << (int)it->item_id
						// << " mat=" << (int)it->material << " rar=" << (int)it->rarity
						// << " lvl=" << (int)it->level << " f8=0x" << std::hex << (uint32_t)it->field_8 << std::dec << std::endl;
					// if (i == 1 && ic > 14) std::cout << "      ..." << std::endl;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { }   // (Konsolenausgabe entfernt)
	}
	// // STRG+3: Vendor- und Spieler-Inventar roh dumpen (nach einem Kauf druecken).
	void DumpInventories(cube::GameController* gc) {
		if (!gc || !gc->local_player) return;
		cube::Creature* vptr = (g_activePost >= 0 && g_activePost < (int)g_posts.size()) ? g_posts[g_activePost].npcPtr : nullptr;
		//std::cout << "=== [AirshipTaxi-DUMP] (Marker-Basis=0x" << std::hex << kMarkerBase << std::dec << ") ===" << std::endl;
		DumpCreatureInv("VENDOR ", vptr);
		DumpCreatureInv("SPIELER", gc->local_player);
		//std::cout << "=== [AirshipTaxi-DUMP] Ende ===" << std::endl;
	}

	void OnWorldReload() {
		g_posts.clear();
		g_activePost = -1; g_bubbleActive = false; g_pendingDeleteDir = -1;
		for (int k = 0; k < 4; k++) { g_prevGc[k] = g_prevV[k] = g_prevPlayer[k] = -1; }
		// std::cout << "[AirshipTaxi] Weltwechsel -- Turm-Posten vergessen (Vendor spawnt neu)." << std::endl;
	}

	void RegisterWithFramework() {
		AdventureFramework::RegisterMod(&g_mod);
		// std::cout << "[AirshipTaxi] Bereit -- Turm-Vendor + Luftschiff-Taxi zwischen Bergdoerfern." << std::endl;
	}
}
