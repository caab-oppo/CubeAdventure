#include "ZoneSplit.h"
#include "LabBridge.h"
#include <windows.h>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>

// Herleitung: ZoneSplit.h.

namespace ZoneSplit {
	static CRITICAL_SECTION& Cs() { static struct S { CRITICAL_SECTION c; S() { InitializeCriticalSection(&c); } } s; return s.c; }
	struct Lock { Lock() { EnterCriticalSection(&Cs()); } ~Lock() { LeaveCriticalSection(&Cs()); } };
	struct Part { uint64_t id; std::shared_ptr<Lab::Structure> cut; int x, y, z, fund, culture; void* applied; };
	static std::map<int64_t, std::vector<Part>> g_parts;                 // Zone -> vorgemerkte Stuecke
	static std::map<uint64_t, bool> g_known;                             // angemeldete Bauwerke
	static volatile LONG c_placed = 0, c_pending = 0, c_late = 0, c_onzone = 0;
	static int FloorDiv(int v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); }
	static int64_t Key(int zx, int zy) { return ((int64_t)zx << 32) ^ (uint32_t)zy; }
	bool Crosses(int ox, int oy, int w, int d) { return FloorDiv(ox) != FloorDiv(ox + w - 1) || FloorDiv(oy) != FloorDiv(oy + d - 1); }

	// Bauwerk in Weltlage drehen (wie placeStructure, RE_placeStructure.md)
	static void Rotate(const Lab::Structure& s, int rot, Lab::Structure& out) {
		rot &= 3;
		int W = (rot & 1) ? s.sy : s.sx, D = (rot & 1) ? s.sx : s.sy;
		out.Create(W, D, s.sz);
		for (int z = 0; z < s.sz; z++) for (int my = 0; my < s.sy; my++) for (int mx = 0; mx < s.sx; mx++) {
			uint8_t r, g, b; if (!s.Get(mx, my, z, r, g, b)) continue;
			int wx, wy;
			switch (rot) {
				case 0: wx = mx; wy = my; break;
				case 1: wx = my; wy = (s.sx - mx) - 1; break;
				case 2: wx = (s.sx - mx) - 1; wy = (s.sy - my) - 1; break;
				default: wx = (s.sy - my) - 1; wy = mx; break;
			}
			out.Set(wx, wy, z, r, g, b);
		}
	}
	static bool Cut(const Lab::Structure& w, int ox, int oy, int zx, int zy, Lab::Structure& out, int* ax, int* ay) {
		int x0 = (std::max)(ox, zx * 256), y0 = (std::max)(oy, zy * 256), x1 = (std::min)(ox + w.sx, zx * 256 + 256), y1 = (std::min)(oy + w.sy, zy * 256 + 256);
		if (x0 >= x1 || y0 >= y1) return false;
		out.Create(x1 - x0, y1 - y0, w.sz); bool any = false;
		for (int z = 0; z < w.sz; z++) for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
			uint8_t r, g, b; if (!w.Get(x - ox, y - oy, z, r, g, b)) continue;
			out.Set(x - x0, y - y0, z, r, g, b); any = true;
		}
		*ax = x0; *ay = y0;
		return any;
	}
	int Place(void* zone, int zx, int zy, const Lab::Structure& s, int ox, int oy, int z, int rot, int fundament, int culture, uint64_t id) {
		int W = (rot & 1) ? s.sy : s.sx, D = (rot & 1) ? s.sx : s.sy;
		if (!Crosses(ox, oy, W, D)) {                                            // ganz in einer Zone: wie bisher
			if (zone && FloorDiv(ox) == zx && FloorDiv(oy) == zy) { LabBridge::StampIntoZoneCulture(s, ox, oy, z, rot, fundament, culture, zone); InterlockedIncrement(&c_placed); }
			return 1;
		}
		Lab::Structure w; Rotate(s, rot, w);
		int n = 0;
		bool first;
		{ Lock l; first = !g_known.count(id); g_known[id] = true; }
		for (int qx = FloorDiv(ox); qx <= FloorDiv(ox + W - 1); qx++) for (int qy = FloorDiv(oy); qy <= FloorDiv(oy + D - 1); qy++) {
			std::shared_ptr<Lab::Structure> c = std::make_shared<Lab::Structure>(); int ax, ay;
			if (!Cut(w, ox, oy, qx, qy, *c, &ax, &ay)) continue;
			n++;
			if (qx == zx && qy == zy) {                                          // diese Zone: sofort
				if (zone) { LabBridge::StampInPlace(*c, ax, ay, z, 0, fundament, culture, zone); InterlockedIncrement(&c_placed); }
				if (first) { Lock l; g_parts[Key(qx, qy)].push_back({ id, c, ax, ay, z, fundament, culture, zone }); }
				continue;
			}
			if (!first) continue;                                               // Nachbarstuecke schon vorgemerkt
			Lock l; g_parts[Key(qx, qy)].push_back({ id, c, ax, ay, z, fundament, culture, nullptr }); InterlockedIncrement(&c_pending);
		}
		return n;
	}
	void OnZone(void* zone, int zx, int zy) {
		if (!zone) return;
		std::vector<Part> todo;
		{
			Lock l; auto it = g_parts.find(Key(zx, zy)); if (it == g_parts.end()) return;
			for (Part& p : it->second) if (p.applied != zone) { p.applied = zone; todo.push_back(p); }
		}
		for (Part& p : todo) { LabBridge::StampInPlace(*p.cut, p.x, p.y, p.z, 0, p.fund, p.culture, zone); InterlockedIncrement(&c_onzone); }
	}
	void Tick(cube::GameController* gc) {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (!gc || now - last < 500) return;
		last = now;
		std::vector<std::pair<int64_t, size_t>> work;
		{ Lock l; for (auto& kv : g_parts) for (size_t i = 0; i < kv.second.size(); i++) work.push_back({ kv.first, i }); }
		int done = 0;
		for (const auto& w : work) {
			int zx = (int)(w.first >> 32), zy = (int)(int32_t)(uint32_t)w.first;
			cube::Zone* z = LabBridge::ZoneAt(gc, zx, zy);
			if (!z) continue;
			Part p;
			{ Lock l; std::vector<Part>& v = g_parts[w.first]; if (w.second >= v.size() || v[w.second].applied == z) continue; v[w.second].applied = z; p = v[w.second]; }
			LabBridge::StampInPlace(*p.cut, p.x, p.y, p.z, 0, p.fund, p.culture, z);
			LabBridge::RemeshArea(gc, p.x, p.y, p.x + p.cut->sx, p.y + p.cut->sy);
			InterlockedIncrement(&c_late);
			if (++done >= 4) break;                                             // je Durchgang hoechstens 4 (kein Ruckeln)
		}
	}
	std::string Status() {
		size_t n; { Lock l; n = g_known.size(); }
		char b[256]; sprintf_s(b, "Zonen-Teiler: %d Bauwerke ueber Zonengrenzen, %ld Stuecke sofort, %ld vorgemerkt, %ld bei der Zonenerzeugung, %ld nachgesetzt (geladene Zonen)",
		                      (int)n, (long)c_placed, (long)c_pending, (long)c_onzone, (long)c_late);
		return b;
	}
}
