#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MapMarkers.h"
#include "NativeUi.h"
#include "Skills.h"
#include "ModDb.h"
#include "City.h"
#include <climits>
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <cstring>

namespace MapMarkers {
	static const uint32_t G_PLAYER = 0x8006D0, G_MAPOPEN = 0x8006E4, G_PROJ = 0x800D48, G_VIEW = 0x800D88, G_MAPOFF = 0x1000E4C, G_SEED = 0x2E4 + 0x800188;
	struct Mark { int x, y, z; std::string name; };
	static std::vector<Mark> g_marks; static uint32_t g_seed = 0; static bool g_loaded = false;
	static bool g_mWas = false;

	static std::string File(uint32_t seed) { char b[64]; sprintf_s(b, "AdventureMod_kartenmarker_%08X.txt", seed); return b; }
	static void Load(uint32_t seed) {
		g_marks.clear(); g_seed = seed; g_loaded = true;
		std::istringstream f(ModDb::Txt(File(seed))); std::string line;
		while (std::getline(f, line)) {
			Mark m; int n = 0;
			if (sscanf_s(line.c_str(), "%d %d %d %n", &m.x, &m.y, &m.z, &n) < 3) continue;
			m.name = line.substr((size_t)n); g_marks.push_back(m);
		}
	}
	static void Save() {
		std::ostringstream o;
		for (const Mark& m : g_marks) o << m.x << " " << m.y << " " << m.z << " " << m.name << "\n";
		ModDb::WriteText(File(g_seed), o.str());
	}
	struct View { float P[16], V[16]; float off[3]; int64_t pos[3]; };
	static bool ReadRaw(cube::GameController* gc, View* v, uint32_t* seed, bool* open) {
		__try {
			uint8_t* G = (uint8_t*)gc;
			*open = G[G_MAPOPEN] != 0;
			uint8_t* p = *(uint8_t**)(G + G_PLAYER); if (!p) return false;
			memcpy(v->P, G + G_PROJ, 64); memcpy(v->V, G + G_VIEW, 64); memcpy(v->off, G + G_MAPOFF, 12);
			memcpy(v->pos, p + 0x10, 24); *seed = *(uint32_t*)(G + G_SEED);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static float E(const float* m, int r, int c) { return m[c * 4 + r]; }           // spaltenweise wie plasma::Matrix
	static bool Mul(const float* m, const float* in, float* out) {                      // mit Perspektivdivision
		float w = E(m, 3, 0) * in[0] + E(m, 3, 1) * in[1] + E(m, 3, 2) * in[2] + E(m, 3, 3);
		if (std::fabs(w) < 1e-12f) return false;
		for (int r = 0; r < 3; r++) out[r] = (E(m, r, 0) * in[0] + E(m, r, 1) * in[1] + E(m, r, 2) * in[2] + E(m, r, 3)) / w;
		return true;
	}
	static bool Project(const View& v, double wx, double wy, double wz, int W, int H, float* sx, float* sy) {
		float p[3] = { (float)(wx - v.pos[0] / 65536.0 - v.off[0]), (float)(wy - v.pos[1] / 65536.0 - v.off[1]), (float)(wz - v.pos[2] / 65536.0) };
		float a[3], q[3];
		if (!Mul(v.V, p, a) || a[2] <= 0.0f || !Mul(v.P, a, q)) return false;
		if (q[0] < -1.0f || q[0] > 1.0f || q[1] < -1.0f || q[1] > 1.0f) return false;
		*sx = q[0] * W * 0.5f + W * 0.5f; *sy = -q[1] * H * 0.5f + H * 0.5f;
		return true;
	}
	// Bildpunkt -> Weltpunkt auf der Ebene z = Spielerhoehe: C = P * V; (C0 - nx C3).p = 0 und (C1 - ny C3).p = 0.
	static bool Unproject(const View& v, float sx, float sy, int W, int H, double* wx, double* wy) {
		float C[4][4];
		for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float s = 0; for (int k = 0; k < 4; k++) s += E(v.P, r, k) * E(v.V, k, c); C[r][c] = s; }
		float nx = (sx - W * 0.5f) / (W * 0.5f), ny = -(sy - H * 0.5f) / (H * 0.5f);
		float a1 = C[0][0] - nx * C[3][0], b1 = C[0][1] - nx * C[3][1], c1 = (C[0][2] - nx * C[3][2]) * 0.0f + (C[0][3] - nx * C[3][3]);
		float a2 = C[1][0] - ny * C[3][0], b2 = C[1][1] - ny * C[3][1], c2 = (C[1][2] - ny * C[3][2]) * 0.0f + (C[1][3] - ny * C[3][3]);
		float det = a1 * b2 - a2 * b1;
		if (std::fabs(det) < 1e-12f) return false;
		float x = (-c1 * b2 + c2 * b1) / det, y = (-a1 * c2 + a2 * c1) / det;
		*wx = x + v.pos[0] / 65536.0 + v.off[0]; *wy = y + v.pos[1] / 65536.0 + v.off[1];
		return true;
	}
	// Punkt: schwarzer Rand 10 x 10, innen 6 x 6 in der Farbe (Clear auf Rechtecke des Ziels, kein Text)
	static void Dot(IDirect3DDevice9* dev, float x, float y, D3DCOLOR c) {
		D3DRECT o = { (LONG)x - 5, (LONG)y - 5, (LONG)x + 5, (LONG)y + 5 }, i = { (LONG)x - 3, (LONG)y - 3, (LONG)x + 3, (LONG)y + 3 };
		dev->Clear(1, &o, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
		dev->Clear(1, &i, D3DCLEAR_TARGET, c, 1.0f, 0);
	}
	static bool GameFocus() {
		HWND fg = GetForegroundWindow(); DWORD pid = 0; if (!fg) return false;
		GetWindowThreadProcessId(fg, &pid); return pid == GetCurrentProcessId();
	}
	void Draw(cube::GameController* gc, IDirect3DDevice9* dev, int W, int H) {
		if (!gc || !dev) return;
		View v; uint32_t seed = 0; bool open = false;
		if (!ReadRaw(gc, &v, &seed, &open)) return;
		if (!g_loaded || seed != g_seed) Load(seed);
		const bool m = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		const bool click = open && m && !g_mWas && GameFocus();
		g_mWas = m;
		if (!open) return;
		// Rechtsklick: vorhandene Markierung unter dem Zeiger entfernen, sonst neue setzen
		if (click) {
			POINT cp; GetCursorPos(&cp); HWND hw = GetForegroundWindow(); ScreenToClient(hw, &cp);
			RECT cr; GetClientRect(hw, &cr);
			float mx = (float)cp.x * W / (float)std::max(1L, cr.right), my = (float)cp.y * H / (float)std::max(1L, cr.bottom);
			int hit = -1; float best = 14.0f * 14.0f;
			for (int i = 0; i < (int)g_marks.size(); i++) {
				float sx, sy; if (!Project(v, g_marks[i].x + 0.5, g_marks[i].y + 0.5, g_marks[i].z, W, H, &sx, &sy)) continue;
				float d = (sx - mx) * (sx - mx) + (sy - my) * (sy - my); if (d < best) { best = d; hit = i; }
			}
			if (hit >= 0) g_marks.erase(g_marks.begin() + hit);
			else {
				double wx, wy;
				if (Unproject(v, mx, my, W, H, &wx, &wy)) {
					int n = 1; for (const Mark& k : g_marks) if (k.name.rfind("Markierung ", 0) == 0) n = std::max(n, atoi(k.name.c_str() + 11) + 1);
					g_marks.push_back(Mark{ (int)std::floor(wx), (int)std::floor(wy), (int)(v.pos[2] >> 16), "Markierung " + std::to_string(n) });
				}
			}
			Save();
		}
		// Viertelnamen der Staedte (Text wie die Kartennamen des Spiels: weiss mit Schatten)
		{   // 2026-10-29: Schrift des Spiels (NativeUi::HudText) statt D3DX
			static std::vector<City::MapLabel> labels; static DWORD last = 0;
			if (GetTickCount() - last > 2000) { last = GetTickCount(); labels.clear(); City::MapLabels(labels); }
			for (const City::MapLabel& lb : labels) {
				float sx, sy; double z = lb.z == INT_MIN ? v.pos[2] / 65536.0 : lb.z;
				if (!Project(v, lb.x + 0.5, lb.y + 0.5, z, W, H, &sx, &sy)) continue;
				NativeUi::HudText(gc, lb.name, sx, sy, 1);
			}
		}
		// Runensteine (violett) und eigene Markierungen (gelb)
		for (const Skills::Stone& st : Skills::Stones()) {
			float sx, sy; if (Project(v, st.x + 0.5, st.y + 0.5, st.z, W, H, &sx, &sy)) Dot(dev, sx, sy, D3DCOLOR_ARGB(255, 190, 110, 255));
		}
		for (const Mark& k : g_marks) {
			float sx, sy; if (Project(v, k.x + 0.5, k.y + 0.5, k.z, W, H, &sx, &sy)) Dot(dev, sx, sy, D3DCOLOR_ARGB(255, 255, 220, 60));
		}
	}
}
