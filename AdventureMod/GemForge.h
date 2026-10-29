#pragma once
//
// GemForge.h
// ---------------------------------------------------------------
// Erweitert die NATIVE Customization Bench ("Weapon Customization") von
// 32 auf 100 Cubes je Waffe -- Bedienung, Materialregeln und Level-Regeln
// bleiben exakt die des Spiels (siehe cubeworld.fandom.com/wiki/Customization_Bench).
//
// Funktionsprinzip (alle Adressen relativ zu ImageBase 0x400000, belegt gegen
// den Reversal-Baum CubeWorld-Reversal-master-new, siehe RE_Notizen_GemForge.md):
//
//   Item-Layout (0x118 Byte):
//     +0x00 category_id (3 = Waffe)   +0x01 item_id     +0x04 modifier/Seed
//     +0x0C rarity      +0x0D material                  +0x10 level (u16)
//     +0x14 ItemAdaption[32] (je 8 Byte: x,y,z,material,u16 level,u16 pad)
//     +0x114 adaption_count
//   -> Das Array ist STRUKTURELL auf 32 Eintraege begrenzt. Mehr kann nicht
//      im Item liegen, ohne das Nachbar-Item in WornEquipment zu ueberschreiben.
//      Darum: erste <=32 Cubes bleiben nativ (werden gespeichert & gesynct),
//      alles darueber lebt in einem Parallel-Store nur im Mod.
//
//   Gehookte Funktionen:
//     0x588250  VoxelWidget::tryPlaceCube(u8 waffenMaterial, int waffenLevel)
//               thiscall, ECX = VoxelWidget. Wird beim Klick in der Bench
//               aufgerufen. Enthaelt das Gate "if (item->adaption_count < 0x20)".
//     0x4C69B0  Item::addAdaptionCube(x, y, z, material, level)
//               thiscall, ECX = Item. Traegt einen Cube ins Item ein
//               (Cap 32 bzw. 16 je Waffentyp, siehe 0x4C7660).
//     0x471B60  item_renderAdaptionCubes(Item*, Matrix4*, u32, u32, float* tint, float glow)
//               Zeichnet fuer jeden Eintrag einen Cube, versetzt um (x,y,z).
//               Aufrufkonvention wird zur Laufzeit erkannt (siehe .cpp).
//     0x4C7F60  item_weaponDamage()  thiscall, ECX = Item, Rueckgabe FLOAT.
//               Native Formel: pow2Mul(level + adaption_count*0.1, rarity).
//
#include "cwsdk.h"
#include "AdventureModFramework.h"
#include <string>

namespace GemForge {

	// Budget je Waffe: 100 Cubes JE MATERIAL (Ruby, Emerald, Sapphire,
	// Diamond, Gold, ...), zusammen hoechstens 500. Gezaehlt wird jeweils
	// nativ + Parallel-Store.
	enum { kMaxPerMaterial = 100, kMaxCubesTotal = 500 };

	// Materialkennungen aus material_toColor (0x4C7250) -- nur fuer die Anzeige.
	enum { MAT_GOLD = 0x0B, MAT_RUBY = 0x80, MAT_EMERALD = 0x81,
		   MAT_SAPPHIRE = 0x82, MAT_DIAMOND = 0x83 };

	// Installiert alle Hooks. MH_EnableHook(MH_ALL_HOOKS) passiert in main.cpp.
	void RegisterWithFramework();

	// Bindet die aktuell angelegte Waffe an das native Bench-Widget
	// (VoxelWidget + 0x160). Ohne das steigt drawWeaponCustomization sofort
	// aus ("if (*(int*)(self+0x160) == 0) goto ende") und die Bench bleibt leer.
	// Rueckgabe: true, wenn eine Waffe gebunden werden konnte.
	bool BindEquippedWeaponToBench(cube::GameController* gc);
	int  CountCubesOfMaterial(const uint8_t* item, uint8_t material);   // nativ + Parallel-Store
	void InstallLampPatches();                                          // Lampe an der Bench + Lichtradius (Lampe.cpp)

	// Loescht die Extra-Cubes (>32) der aktuell angelegten Waffe.
	void ClearExtras(cube::GameController* gc);

	// [STRG+I]: Zustand der offenen Bench auf die Konsole. Zeigt den
	// Cube-Vorrat mit den ECHTEN Material-IDs, welcher Eintrag ausgewaehlt
	// ist, den Trefferpunkt des Raycasts und die Cubes der Waffe. Damit
	// laesst sich ohne Raten feststellen, warum ueber 32 hinaus nichts
	// gesetzt wird.
	void DumpBenchState(cube::GameController* gc);

	// [STRG+U]: kompletter Inventar-Scan des Spielers.
	//
	// Layout (belegt aus drawWeaponCustomization @0x588500, dort wird der
	// Cube-Vorrat genau so zusammengesucht):
	//   Creature + 0x11DC = Zeiger auf Anfang der Kategorien-Liste
	//   Creature + 0x11E0 = Zeiger auf Ende          -> (Ende-Anfang)/0x0C Kategorien
	//   Kategorie (0x0C Byte) = { Slot* begin; Slot* end; Slot* cap; }
	//   Slot (0x11C Byte)     = { int32 anzahl; Item item; }   // Item = 0x118
	// Ausgegeben wird jeder Slot mit Kategorie, ID, Material, Rarity, Level.
	// Cubes (Kategorie 0x0B) werden markiert -- damit sind die Material-IDs
	// der Gems direkt ablesbar statt geraten.
	void DumpInventory(cube::GameController* gc);

	// Klartextname zu einer Material-ID (aus material_toColor @0x4C7250).
	const char* MaterialName(int material);

	// [STRG+4]: Edelstein-Angebot in der Bench an/aus (Standard: an).
	//
	// Die Zutaten werden dabei NICHT veraendert. Sie werden dem Vorrat der
	// Bench als zusaetzliche Eintraege beigelegt -- mit ihrem eigenen
	// Material (0x0B Gold, 0x0D/0x0E/0x0F/0x10 Emerald/Sapphire/Ruby/Diamond).
	// Gesetzt wird also genau der Edelstein, nicht ein Ersatzwuerfel.
	//
	// Timing: drawWeaponCustomization @0x588500 leert den Vorrat am Anfang,
	// fuellt ihn und BENUTZT ihn noch im selben Aufruf (Auswahl, Icons,
	// Hover). Angehaengt wird deshalb waehrenddessen -- beim ersten
	// render_uploadModelViewMatrices-Aufruf des Bench-Frames, der garantiert
	// nach dem Fuellen und vor der Zutatenliste liegt.
	void ToggleGemSupply();

	// Klammern um cube::GameController::drawWeaponCustomization (0x588500).
	// Werden aus dem bereits vorhandenen Hook in BlacksmithNpc aufgerufen
	// (MinHook laesst nur einen Hook je Adresse zu). Nur dazwischen darf die
	// Vorschau-Injektion greifen -- siehe GemForge.cpp, Hook_Upload.
	void OnBenchDrawBegin(void* voxelWidget);
	void OnBenchDrawEnd();

	// Overlay-Text: "Waffe 12 | nativ 32/32 + extra 41 = 73/100".
	bool GetEquippedInfo(cube::GameController* gc, std::string& outText);
}
