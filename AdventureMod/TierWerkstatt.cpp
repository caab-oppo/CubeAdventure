// TierWerkstatt.cpp -- LAB-Reiter "Tier-Werkstatt". Siehe TierWerkstatt.h.
//
// Fensterthread des LAB: Liste, Ansicht, Felder, Dateien. Native Modelle (Teile "von der Basisrasse" bzw. "nativer
// Index") liest der Spielthread ueber LabBridge::ImportNative und legt sie in einen gesperrten Zwischenspeicher.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "TierWerkstatt.h"
#include "MobLab.h"
#include "LabBridge.h"
#include "LabStructure.h"
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cfenv>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace TierWerkstatt {

	// ---- Grundwerte der nativen Rassen (fillAppearance) --------------------------------------------------------
	struct BaseApp {
		int flags; float size[3]; float chestRot, feetRot, wingsRot, tailRot;
		int model[8]; float scale[8]; float pos[8][3];
	};
	static const BaseApp kBase[] = {
#include "TierBasis.inc"
	};
	static const int kBaseCount = (int)(sizeof(kBase) / sizeof(kBase[0]));

	// ---- 4x4 (Spaltenvektoren, wie numpy M @ v) -----------------------------------------------------------------
	struct M4 { float a[16]; };
	static M4 Id() { M4 m{}; m.a[0] = m.a[5] = m.a[10] = m.a[15] = 1; return m; }
	static M4 Mul(const M4& A, const M4& B) {
		M4 r{};
		for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float s = 0; for (int k = 0; k < 4; k++) s += A.a[i * 4 + k] * B.a[k * 4 + j]; r.a[i * 4 + j] = s; }
		return r;
	}
	static M4 T(float x, float y, float z) { M4 m = Id(); m.a[3] = x; m.a[7] = y; m.a[11] = z; return m; }
	static M4 S(float x, float y, float z) { M4 m = Id(); m.a[0] = x; m.a[5] = y; m.a[10] = z; return m; }
	static M4 R(char ax, float deg) {
		float c = cosf(deg * 3.14159265f / 180.0f), s = sinf(deg * 3.14159265f / 180.0f); M4 m = Id();
		int i = ax == 'x' ? 1 : ax == 'y' ? 2 : 0, j = ax == 'x' ? 2 : ax == 'y' ? 0 : 1;
		m.a[i * 4 + i] = c; m.a[i * 4 + j] = -s; m.a[j * 4 + i] = s; m.a[j * 4 + j] = c; return m;
	}
	static void Ap(const M4& m, float x, float y, float z, float o[3]) {
		for (int i = 0; i < 3; i++) o[i] = m.a[i * 4] * x + m.a[i * 4 + 1] * y + m.a[i * 4 + 2] * z + m.a[i * 4 + 3];
	}
	static M4 operator*(const M4& A, const M4& B) { return Mul(A, B); }

	// ---- Modelle: Oberflaechen-Voxel je Datei / nativem Index ---------------------------------------------------
	struct Vox { int sx = 0, sy = 0, sz = 0; std::vector<int> pts; std::vector<uint32_t> col; std::vector<uint8_t> top; };
	static Vox Surface(const Lab::Structure& s) {
		Vox v; v.sx = s.sx; v.sy = s.sy; v.sz = s.sz;
		for (int z = 0; z < s.sz; z++) for (int y = 0; y < s.sy; y++) for (int x = 0; x < s.sx; x++) {
			uint8_t r, g, b; if (!s.Get(x, y, z, r, g, b)) continue;
			if (s.Solid(x + 1, y, z) && s.Solid(x - 1, y, z) && s.Solid(x, y + 1, z) && s.Solid(x, y - 1, z) && s.Solid(x, y, z + 1) && s.Solid(x, y, z - 1)) continue;
			v.pts.push_back(x); v.pts.push_back(y); v.pts.push_back(z);
			v.col.push_back((uint32_t)r << 16 | (uint32_t)g << 8 | b); v.top.push_back(s.Solid(x, y, z + 1) ? 0 : 1);
		}
		return v;
	}
	static std::map<std::string, Vox> g_files;                 // Fensterthread
	static std::mutex g_natMx;
	static std::map<int, Lab::Structure> g_natIn;               // vom Spielthread
	static std::set<int> g_natAsked, g_natFailed;
	static std::map<int, Vox> g_nat;                            // Fensterthread
	static volatile LONG g_natNew = 0;

	static const Vox* NativeVox(int idx) {
		auto it = g_nat.find(idx); if (it != g_nat.end()) return &it->second;
		{
			std::lock_guard<std::mutex> lk(g_natMx);
			auto in = g_natIn.find(idx);
			if (in != g_natIn.end()) { g_nat[idx] = Surface(in->second); g_natIn.erase(in); return &g_nat[idx]; }
			if (g_natAsked.count(idx)) return nullptr;
			g_natAsked.insert(idx);
		}
		LabBridge::Post("Tier-Werkstatt: Modell lesen", [idx](cube::GameController* gc) {
			Lab::Structure s; std::string e;
			bool ok = LabBridge::ImportNative(gc, idx, s, e);
			std::lock_guard<std::mutex> lk(g_natMx);
			if (ok) g_natIn[idx] = s; else g_natFailed.insert(idx);
			InterlockedIncrement(&g_natNew);
		});
		return nullptr;
	}
	static std::string Resolve(const std::string& file, const std::string& baseDir) {
		if (file.empty()) return file;
		if (file.size() > 1 && (file[1] == ':' || file[0] == '\\')) return file;
		return baseDir.empty() ? file : baseDir + "\\" + file;
	}
	static const Vox* FileVox(const std::string& path) {
		auto it = g_files.find(path); if (it != g_files.end()) return it->second.sx ? &it->second : nullptr;
		Lab::Structure s; std::string e; Vox v;
		if (Lab::LoadCub(path, s, e)) v = Surface(s);
		g_files[path] = v; return v.sx ? &g_files[path] : nullptr;
	}

	// ---- Zustand ------------------------------------------------------------------------------------------------
	static HFONT g_font = nullptr, g_fontBold = nullptr;
	static HWND g_page = nullptr, g_view = nullptr, g_list = nullptr, g_part = nullptr, g_info = nullptr, g_status = nullptr;
	static HWND g_e[4] = {}, g_step = nullptr, g_setPos = nullptr, g_faktor = nullptr, g_dim = nullptr;
	static HWND g_artBox = nullptr, g_playBox = nullptr, g_timeEd = nullptr, g_jy = nullptr, g_jz = nullptr, g_jInfo = nullptr;
	static std::vector<std::string> g_paths;
	static int g_sel = -1, g_partSel = MobLab::P_HEAD;
	static MobLab::CustomMob g_mob; static bool g_have = false, g_dirty = false;
	static float g_yaw = -35.0f, g_pitch = 22.0f, g_zoomMul = 1.0f;
	static const char* kPartNames[8] = { "Kopf", "Haare", "Haende / Vorderbeine", "Fuesse / Hinterbeine", "Koerper", "Schwanz", "Schulter", "Fluegel" };
	enum { ID_LIST = 10, ID_PART, ID_RELOAD, ID_SAVE, ID_SPAWN, ID_APPLY, ID_DIM, ID_LEFT, ID_RIGHT, ID_FRONT, ID_SIDE,
	       ID_ART, ID_PLAY, ID_TIME, ID_JAPPLY, ID_SHOT, ID_MINUS0 = 40, ID_PLUS0 = 50, ID_TIMER = 7, ID_ANIM = 8 };

	static void Status(const std::string& s) { if (g_status) SetWindowTextA(g_status, s.c_str()); }
	static std::string Txt(HWND h) { char b[256] = ""; GetWindowTextA(h, b, 255); return b; }
	static void SetF(HWND h, float v) { char b[32]; sprintf_s(b, "%.2f", v); SetWindowTextA(h, b); }

	static const BaseApp& Base() { int r = g_mob.baseRace; return kBase[(r >= 0 && r < kBaseCount) ? r : 2]; }
	static int DrawRace() { return (g_mob.ownRace >= 200 && g_mob.setRace) ? g_mob.ownRace : g_mob.baseRace; }

	// Wirksame Werte eines Teils (wie MobLab::ApplyToSpawn ueber dem Aussehen der Basisrasse)
	struct Eff { const Vox* v = nullptr; float scale = 1, pos[3] = {}; bool on = false, missing = false; };
	static Eff Effective(int i) {
		Eff e; const MobLab::Part& p = g_mob.parts[i]; const BaseApp& b = Base();
		if (p.source == MobLab::SRC_NONE) return e;
		if (p.source == MobLab::SRC_FILE) e.v = FileVox(Resolve(p.file, g_mob.baseDir));
		else { int idx = p.source == MobLab::SRC_NATIVE ? p.nativeIndex : b.model[i]; if (idx < 0) return e; e.v = NativeVox(idx); }
		e.on = true; e.missing = e.v == nullptr;
		e.scale = p.scale > 0 ? p.scale : b.scale[i];
		bool hasPos = i != MobLab::P_SHOULDER;
		for (int k = 0; k < 3; k++) e.pos[k] = (p.setPos && hasPos) ? p.pos[k] : b.pos[i][k];
		if (i == MobLab::P_HAIR && g_mob.parts[MobLab::P_HEAD].setPos && !p.setPos) for (int k = 0; k < 3; k++) e.pos[k] = g_mob.parts[MobLab::P_HEAD].pos[k];
		return e;
	}
	// ---- Gelenke + Haltungen (2026-10-29) --------------------------------------------------------------------
	// Gelenke: .mob "gelenk=<teil>;x;y;z" (Voxel der Originalform <datei>.orig.cub), "gelenk_verlaengerung=n" (so viele
	// Lagen ist die Oberkante im Koerper verlaengert). Die Bein-.cub ist so aufgefuellt, dass ihre Mitte im Gelenk liegt;
	// mit TierGelenke (Spiel) faellt die Drehung um den Modellursprung weg -> Schwung nur ums Gelenk.
	static bool g_hasJoint[8] = {}; static float g_joint[8][3] = {}; static int g_ext = 0; static std::string g_mobPath;
	static int g_art = 0; static float g_time = 0.0f; static bool g_play = false;
	static const char* kArtNames[7] = { "Ruhe", "Gehen", "Biss", "Sturzbiss", "Kopfstoss", "Stampfen", "Hornangriff" };
	static const char* kArtKeys[7] = { "ruhe", "gehen", "biss", "sturzbiss", "kopfstoss", "stampfen", "hornangriff" };
	static const float kArtMs[7][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 300, 300, 500 }, { 400, 300, 500 }, { 300, 1000, 800 }, { 300, 300, 500 }, { 300, 1000, 800 } };
	static const float kTempo = 0.75f;                                       // Monster: 0.0625 * Seltenheit 0 + 0.75 (CreatureStats.cpp tempo)
	static float ArtDur(int art) { return (kArtMs[art][0] + kArtMs[art][1] + kArtMs[art][2]) / kTempo; }
	static bool JointsOn() { return DrawRace() >= 200 && (g_hasJoint[MobLab::P_FEET] || g_hasJoint[MobLab::P_HANDS]); }
	static void ReadJoints(const std::string& path) {
		for (int i = 0; i < 8; i++) g_hasJoint[i] = false;
		g_ext = 0; g_mobPath = path;
		std::ifstream f(path); std::string l;
		while (std::getline(f, l)) {
			if (!l.empty() && l.back() == '\r') l.pop_back();
			if (l.compare(0, 7, "gelenk=") == 0) {
				int k; float x, y, z; if (sscanf_s(l.c_str() + 7, "%d;%f;%f;%f", &k, &x, &y, &z) == 4 && k >= 0 && k < 8) { g_hasJoint[k] = true; g_joint[k][0] = x; g_joint[k][1] = y; g_joint[k][2] = z; }
			} else if (l.compare(0, 21, "gelenk_verlaengerung=") == 0) g_ext = atoi(l.c_str() + 21);
		}
	}
	// Haltung (Animation.cpp): Ziel je Zeitabschnitt, je Millisekunde value += (ziel - value) * rate
	struct Pose { float head[3], body[3], bodyOff[3], headOff[3], lhr[3], rhr[3], lho[3], rho[3], tail[3], crea[3]; };
	static void Set3(float* d, float a, float b, float c) { d[0] = a; d[1] = b; d[2] = c; }
	static float Targets(int art, float t, Pose& g) {
		memset(&g, 0, sizeof(g));
		const float ant = kArtMs[art][0] / kTempo, st = kArtMs[art][1] / kTempo, dur = ArtDur(art);
		float rate = 0.01f;
		switch (art) {
			case 2:                                                            // Biss (AbilityBite 75 -> AnimationClawStrike)
				if (t < ant) { Set3(g.head, 10, 0, 0); Set3(g.body, -20, 0, 0); Set3(g.bodyOff, 0, -1, 0); Set3(g.headOff, 0, 1, 1); g.lhr[0] = 20; g.rhr[0] = 20; g.lho[2] = 2; g.rho[2] = 2; Set3(g.tail, 30, 0, 10); }
				else if (t < ant + st / 3) { Set3(g.head, -30, 0, 0); Set3(g.body, 50, 0, 0); Set3(g.bodyOff, 0, 0, 1); Set3(g.headOff, 0, -4, 2); g.lhr[0] = 60; g.rhr[0] = 70; Set3(g.tail, -30, 0, 0); }
				else if (t < ant + st * 2 / 3) { Set3(g.head, -30, 0, 0); Set3(g.body, 40, 0, 0); Set3(g.bodyOff, 0, 1, 1); Set3(g.headOff, 0, -2, 2); g.lhr[0] = 70; g.rhr[0] = -30; Set3(g.rho, 0, 3, -2); Set3(g.tail, -30, 0, -10); }
				else if (t < ant + st) { Set3(g.head, -30, 0, 0); Set3(g.body, 10, 0, 0); Set3(g.bodyOff, 0, 1, 1); Set3(g.headOff, 0, -2, 2); g.lhr[0] = -30; g.rhr[0] = -40; Set3(g.lho, 0, 3, -2); Set3(g.tail, -30, 0, -10); }
				else if (t < dur) { Set3(g.head, 10, 0, -10); Set3(g.body, -20, 0, 0); Set3(g.bodyOff, 0, 1, 0); Set3(g.headOff, 0, 1, 1); g.lhr[0] = 20; g.rhr[0] = 20; g.lho[2] = 2; g.rho[2] = 2; Set3(g.tail, 30, 0, 10); }
				break;
			case 3:                                                            // Sturzbiss (AbilityForwardRoll 51 -> AnimationDiveBite)
				if (t < ant) g.crea[0] = -30; else if (t < ant + st) g.crea[0] = 120;
				break;
			case 4:                                                            // Kopfstoss (AnimationHeadbutt)
				if (t < ant + st / 2) { Set3(g.head, -30, 10, 90); Set3(g.body, 0, 0, 60); Set3(g.headOff, -3, -2, 0); }
				else if (t < dur) { Set3(g.head, 30, -10, -30); Set3(g.body, 10, 0, -20); Set3(g.headOff, 3, 0, 2); }
				break;
			case 5:                                                            // Stampfen (AnimationStomp)
				if (t < ant + st / 2) { Set3(g.head, -20, 0, 0); Set3(g.body, 50, 0, 0); Set3(g.bodyOff, 0, -2, 2); Set3(g.headOff, 0, -6, -2); g.lhr[0] = 20; g.rhr[0] = 30; Set3(g.tail, -50, 0, 0); }
				else if (t < dur) { Set3(g.head, -10, 0, 0); Set3(g.headOff, 0, 0, 1); rate = 0.02f; }
				break;
			case 6:                                                            // Hornangriff (AnimationHornCharge)
				if (t < ant + st / 2) { Set3(g.head, -70, 0, 0); Set3(g.body, -10, 0, 0); Set3(g.headOff, 0, -3, -1); g.lhr[0] = 20; g.rhr[0] = 20; }
				else if (t < dur) { Set3(g.head, 50, 0, 0); Set3(g.body, 20, 0, 0); Set3(g.bodyOff, 0, -3, 1); Set3(g.headOff, 0, 0, 2); g.rhr[0] = -20; g.lhr[0] = -20; Set3(g.tail, -10, 0, 0); }
				break;
		}
		return rate;
	}
	static Pose PoseAt(int art, float tms) {
		Pose st; memset(&st, 0, sizeof(st));
		if (art < 2) return st;
		for (float t = 0; t < tms; t += 16.0f) {
			Pose g; float rate = Targets(art, t, g);
			const float share = 1.0f - powf(1.0f - rate, 16.0f);
			float* a = (float*)&st; const float* b = (const float*)&g;
			for (int i = 0; i < (int)(sizeof(Pose) / sizeof(float)); i++) a[i] += share * (b[i] - a[i]);
		}
		return st;
	}
	struct Inst { int part; M4 m; const Vox* v; };
	// Zusammenbau in einer Haltung (art 0 Ruhe, 1 Gehen, 2.. Angriffe; tms = Zeit). Matrizen wie Animation.cpp 2272ff.
	static std::vector<Inst> Assemble(int art = 0, float tms = 0.0f) {
		std::vector<Inst> out; const BaseApp& b = Base();
		Eff E[8]; for (int i = 0; i < 8; i++) E[i] = Effective(i);
		const bool quad = (b.flags & 1) != 0; const float cr = b.chestRot; const bool jo = JointsOn();
		const float A = art == 1 ? 0.21f : 0.0f, ph = tms * 0.006f;
		const float cyc = sinf(ph + 1.5707963f), sw = A * 2.1991148f * cyc * (quad ? 0.75f : 1.0f);
		const float lfs = sw * 57.29578f * 1.5f, rfs = -sw * 57.29578f * 1.5f, swDeg = sw * 57.29578f;
		const float lLift = A * 4.0f * cyc, rLift = A > 0 ? (1.0f - cyc) * A * 4.0f : 0.0f, stride = fabsf(sw), bob = stride * 4.0f;
		const Pose P = PoseAt(art, tms);
		const M4 CRE = R('z', P.crea[2]) * R('y', P.crea[1]) * R('x', P.crea[0]);
		const M4 BODY = CRE * T(0, 0, bob) * T(P.bodyOff[0], P.bodyOff[1], P.bodyOff[2]) * R('z', P.body[2]) * R('y', P.body[1]) * R('x', cr + P.body[0]);
		const M4 HANDS = CRE * T(0, 0, bob) * T(P.bodyOff[0], P.bodyOff[1], P.bodyOff[2]) * R('z', P.body[2]) * R('y', P.body[1]) * R('x', P.body[0]);
		auto center = [](const Vox* v) { return T(-v->sx * 0.5f, -v->sy * 0.5f, -v->sz * 0.5f); };
		using namespace MobLab;
		if (E[P_BODY].v) { const Eff& e = E[P_BODY]; out.push_back({ P_BODY, BODY * T(e.pos[0], e.pos[1], e.pos[2]) * R('z', swDeg * 0.5f) * S(e.scale, e.scale, e.scale) * center(e.v), e.v }); }
		for (int k : { (int)P_HEAD, (int)P_HAIR }) if (E[k].v) {
			const Eff& e = E[k]; const Vox* v = e.v;
			M4 off = T(-v->sx * 0.5f, -v->sy * (DrawRace() == 101 ? 0.2f : 0.5f), -v->sz * 0.5f);
			M4 M = BODY * T(e.pos[0] + P.headOff[0], e.pos[1] + P.headOff[1], e.pos[2] + P.headOff[2] - bob * 0.5f) * T(0, 0, -3) * R('x', -P.body[0] - cr) * R('y', -P.body[1]) *
			       R('z', P.head[2] - P.body[2]) * R('y', P.head[1]) * R('x', P.head[0]) * T(0, 0, 3);
			out.push_back({ k, M * S(e.scale, e.scale, e.scale) * off, v });
		}
		if (E[P_HANDS].v) {
			const Eff& e = E[P_HANDS]; float hx = e.pos[0], s = e.scale;
			for (int side = -1; side <= 1; side += 2) {
				const float* o = side < 0 ? P.lho : P.rho; const float* rr = side < 0 ? P.lhr : P.rhr;
				M4 M;
				if (quad) {
					const float pre = side < 0 ? rfs : lfs, p0 = jo ? 0.0f : pre;
					M = HANDS * R('x', p0) * T(side * hx + o[0], e.pos[1] + 2.1f - A * 5.0f + o[1], e.pos[2] + o[2]) * R('x', pre) * R('x', rr[0]);
				} else M = HANDS * T(side * hx + o[0], e.pos[1] + 2.1f + o[1], e.pos[2] - 4.1f + o[2]) * R('x', rr[0]);
				out.push_back({ P_HANDS, M * S(side < 0 ? s : -s, s, s) * center(e.v), e.v });
			}
		}
		if (E[P_FEET].v) {
			const Eff& e = E[P_FEET]; float s = e.scale, fr = b.feetRot;
			const float a1 = jo ? fr : fr + lfs, a2 = jo ? 0.0f : rfs;
			out.push_back({ P_FEET, CRE * R('x', a1) * T(-e.pos[0], e.pos[1], stride * 3 + e.pos[2] + lLift) * S(s, s, s) * R('x', lfs) * center(e.v), e.v });
			out.push_back({ P_FEET, CRE * R('x', a2) * T(e.pos[0], e.pos[1], stride * 3 + e.pos[2] + rLift) * S(s, s, s) * R('x', fr + rfs) * center(e.v), e.v });
		}
		if (E[P_TAIL].v && E[P_BODY].on) {
			const Eff& e = E[P_TAIL]; const Vox* v = e.v; float s = e.scale * E[P_BODY].scale;
			out.push_back({ P_TAIL, BODY * T(E[P_BODY].pos[0], E[P_BODY].pos[1], E[P_BODY].pos[2]) * T(e.pos[0], e.pos[1], e.pos[2]) * R('x', b.tailRot) * R('z', -swDeg) *
			                         R('z', P.tail[2]) * R('y', P.tail[1]) * R('x', P.tail[0]) * S(s, s, s) * T(-v->sx * 0.5f, (float)-v->sy, -v->sz * 0.5f), v });
		}
		if (E[P_WINGS].v) {
			const Eff& e = E[P_WINGS]; const Vox* v = e.v; float s = e.scale;
			for (int side = -1; side <= 1; side += 2)
				out.push_back({ P_WINGS, HANDS * T(side * e.pos[0], e.pos[1], e.pos[2]) * R('x', b.wingsRot) * R('y', side * 45.0f) * R('z', -90) * R('x', 90) *
				                          S(s, s, s) * T(-v->sx * 0.5f, 0, -v->sz * 0.5f), v });
		}
		return out;
	}

	// ---- Ansicht ------------------------------------------------------------------------------------------------
	static void Project(float x, float y, float z, float& u, float& w, float& d) {
		float cy = cosf(g_yaw * 0.0174533f), sy = sinf(g_yaw * 0.0174533f), cp = cosf(g_pitch * 0.0174533f), sp = sinf(g_pitch * 0.0174533f);
		float x1 = x * cy - y * sy, y1 = x * sy + y * cy;
		u = -x1; w = z * cp - y1 * sp; d = y1 * cp + z * sp;
	}
	// Bild in einen Puffer zeichnen (Ansicht und Datei-API). marks: Gelenke als rote Punkte.
	static void RenderBuf(int W, int H, std::vector<uint32_t>& px, bool marks) {
		px.assign((size_t)W * H, 0x262B36); std::vector<float> zb((size_t)W * H, -1e30f);
		if (!g_have) return;
		std::vector<Inst> parts = Assemble(g_art, g_time); const BaseApp& b = Base();
		const float G = -5.6f * b.size[2] / b.size[0];
		float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
		auto grow = [&](float x, float y, float z) { float u, w, d; Project(x, y, z, u, w, d); lo[0] = std::min(lo[0], u); hi[0] = std::max(hi[0], u); lo[1] = std::min(lo[1], w); hi[1] = std::max(hi[1], w); };
		{   // Einpassung aus der RUHE-Haltung, damit das Bild beim Abspielen nicht springt
			std::vector<Inst> rest = Assemble(0, 0.0f);
			for (const Inst& in : rest) for (int c = 0; c < 8; c++) { float o[3]; Ap(in.m, (c & 1) ? (float)in.v->sx : 0.f, (c & 2) ? (float)in.v->sy : 0.f, (c & 4) ? (float)in.v->sz : 0.f, o); grow(o[0], o[1], o[2]); }
		}
		grow(-12, -18, G); grow(12, 18, G);
		float zoom = std::min((W - 30) / std::max(1.0f, hi[0] - lo[0]), (H - 30) / std::max(1.0f, hi[1] - lo[1])) * g_zoomMul * 0.85f;
		float ox = W * 0.5f - (lo[0] + hi[0]) * 0.5f * zoom, oy = H * 0.5f + (lo[1] + hi[1]) * 0.5f * zoom;
		auto put = [&](int x, int y, float d, uint32_t c) { if (x < 0 || y < 0 || x >= W || y >= H) return; size_t i = (size_t)y * W + x; if (d > zb[i]) { zb[i] = d; px[i] = c; } };
		for (int k = -12; k <= 12; k += 3) for (int t = 0; t <= 200; t++) {
			float u, w, d, f = -18 + 36 * t / 200.0f;
			Project((float)k, f, G, u, w, d); put((int)(ox + u * zoom), (int)(oy - w * zoom), -1e29f, 0x4A5368);
			Project(f * 12 / 18, (float)k * 1.5f, G, u, w, d); put((int)(ox + u * zoom), (int)(oy - w * zoom), -1e29f, 0x4A5368);
		}
		const bool dim = g_dim && SendMessageA(g_dim, BM_GETCHECK, 0, 0) == BST_CHECKED;
		for (const Inst& in : parts) {
			float det = fabsf(in.m.a[0] * (in.m.a[5] * in.m.a[10] - in.m.a[6] * in.m.a[9]) - in.m.a[1] * (in.m.a[4] * in.m.a[10] - in.m.a[6] * in.m.a[8]) + in.m.a[2] * (in.m.a[4] * in.m.a[9] - in.m.a[5] * in.m.a[8]));
			float side = powf(det, 1.0f / 3.0f) * zoom; int half = std::max(0, (int)ceilf(side * 0.62f));
			bool sel = in.part == g_partSel || (g_partSel == MobLab::P_HEAD && in.part == MobLab::P_HAIR);
			const Vox& v = *in.v;
			for (size_t i = 0; i < v.col.size(); i++) {
				float o[3]; Ap(in.m, v.pts[i * 3] + 0.5f, v.pts[i * 3 + 1] + 0.5f, v.pts[i * 3 + 2] + 0.5f, o);
				float u, w, d; Project(o[0], o[1], o[2], u, w, d);
				uint32_t c = v.col[i]; int r = (c >> 16) & 255, g = (c >> 8) & 255, bb = c & 255;
				float f = v.top[i] ? 1.12f : 0.86f;
				if (dim && !sel) { r = (r + 3 * 90) / 4; g = (g + 3 * 94) / 4; bb = (bb + 3 * 104) / 4; }
				r = std::min(255, (int)(r * f)); g = std::min(255, (int)(g * f)); bb = std::min(255, (int)(bb * f));
				int sx = (int)(ox + u * zoom), sy = (int)(oy - w * zoom);
				for (int yy = -half; yy <= half; yy++) for (int xx = -half; xx <= half; xx++) {
					bool edge = half >= 2 && (yy == -half || xx == -half);
					uint32_t cc = edge ? ((uint32_t)(r * 7 / 8) << 16 | (uint32_t)(g * 7 / 8) << 8 | (uint32_t)(bb * 7 / 8)) : ((uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bb);
					put(sx + xx, sy + yy, d, cc);
				}
			}
		}
		if (marks && JointsOn()) for (const Inst& in : parts) {                   // Gelenk = Mitte des aufgefuellten Bein-Modells
			if (in.part != MobLab::P_FEET && in.part != MobLab::P_HANDS) continue;
			float o[3]; Ap(in.m, in.v->sx * 0.5f, in.v->sy * 0.5f, in.v->sz * 0.5f, o);
			float u, w, d; Project(o[0], o[1], o[2], u, w, d);
			int sx = (int)(ox + u * zoom), sy = (int)(oy - w * zoom);
			for (int yy = -3; yy <= 3; yy++) for (int xx = -3; xx <= 3; xx++) if (xx * xx + yy * yy <= 9) put(sx + xx, sy + yy, 1e30f, 0xE03030);
		}
	}
	static void Paint(HWND h, HDC dc) {
		RECT rc; GetClientRect(h, &rc); int W = rc.right, H = rc.bottom; if (W < 8 || H < 8) return;
		std::vector<uint32_t> px; RenderBuf(W, H, px, true);
		BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(dc, 0, 0, W, H, 0, 0, W, H, px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
		SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(200, 206, 220)); SelectObject(dc, g_font);
		char hint[200];
		if (g_have) sprintf_s(hint, "%s  %.0f ms  -- Ziehen = drehen, Mausrad = Zoom, Gitter = Boden, rote Punkte = Gelenke%s", kArtNames[g_art], g_time, JointsOn() ? "" : " (keine)");
		else strcpy_s(hint, "Tier links waehlen");
		TextOutA(dc, 8, 6, hint, (int)strlen(hint));
	}
	static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		static POINT last; static bool drag = false;
		switch (m) {
			case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); Paint(h, dc); EndPaint(h, &ps); return 0; }
			case WM_ERASEBKGND: return 1;
			case WM_LBUTTONDOWN: SetCapture(h); drag = true; last.x = GET_X_LPARAM(lp); last.y = GET_Y_LPARAM(lp); SetFocus(h); return 0;
			case WM_LBUTTONUP: ReleaseCapture(); drag = false; return 0;
			case WM_MOUSEMOVE:
				if (drag) {
					int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
					g_yaw += (x - last.x) * 0.6f; g_pitch = std::max(-10.0f, std::min(85.0f, g_pitch + (y - last.y) * 0.4f));
					last.x = x; last.y = y; InvalidateRect(h, NULL, FALSE);
				}
				return 0;
			case WM_MOUSEWHEEL: g_zoomMul = std::max(0.3f, std::min(4.0f, g_zoomMul * (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.12f : 1 / 1.12f))); InvalidateRect(h, NULL, FALSE); return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}
	static void Redraw() { if (g_view) InvalidateRect(g_view, NULL, FALSE); }

	// ---- Liste / Laden / Speichern -----------------------------------------------------------------------------
	static void Scan(const std::string& dir, int depth) {
		if (depth > 3) return;
		WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA((dir + "\\*").c_str(), &fd);
		if (fh == INVALID_HANDLE_VALUE) return;
		do {
			std::string n = fd.cFileName; if (n == "." || n == "..") continue;
			std::string p = dir + "\\" + n;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (_stricmp(n.c_str(), "Teile") && _stricmp(n.c_str(), "Vorschau")) Scan(p, depth + 1); continue; }
			if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".mob") == 0) g_paths.push_back(p);
		} while (FindNextFileA(fh, &fd));
		FindClose(fh);
	}
	static void FillList() {
		g_paths.clear(); Scan(MobLab::MobsDir(), 0);
		std::sort(g_paths.begin(), g_paths.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
		SendMessageA(g_list, LB_RESETCONTENT, 0, 0);
		std::string root = MobLab::MobsDir() + "\\";
		for (const std::string& p : g_paths) {
			std::string r = p.compare(0, root.size(), root) == 0 ? p.substr(root.size()) : p;
			if (r.size() > 4) r.resize(r.size() - 4);
			SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)r.c_str());
		}
		char b[80]; sprintf_s(b, "%d Tiere/Mobs", (int)g_paths.size()); Status(b);
	}
	static const char* g_mobPartHint(int i);
	static void ShowPart() {
		const MobLab::Part& p = g_mob.parts[g_partSel]; Eff e = Effective(g_partSel);
		for (int k = 0; k < 3; k++) SetF(g_e[k], e.pos[k]);
		SetF(g_e[3], e.scale);
		SendMessageA(g_setPos, BM_SETCHECK, p.setPos ? BST_CHECKED : BST_UNCHECKED, 0);
		const char* src = p.source == MobLab::SRC_BASE ? "von der Basisrasse" : p.source == MobLab::SRC_NONE ? "keins" : p.source == MobLab::SRC_NATIVE ? "natives Modell" : "eigene .cub";
		char b[600];
		sprintf_s(b, "Quelle: %s %s\r\nBasis %d, %s, Groesse %.2f/%.2f/%.2f x Faktor\r\n%s%s", src, p.source == MobLab::SRC_FILE ? p.file.c_str() : "",
		          g_mob.baseRace, (Base().flags & 1) ? "Vierbeiner (Haende = Vorderbeine)" : "Zweibeiner/Flieger", Base().size[0], Base().size[1], Base().size[2],
		          e.missing ? (p.source == MobLab::SRC_FILE ? "Datei fehlt oder ist unlesbar! " : "natives Modell wird gelesen (im Spiel sein) ... ") : "",
		          g_mobPartHint(g_partSel));
		SetWindowTextA(g_info, b);
		if (g_jy && g_jz) {
			const bool leg = g_partSel == MobLab::P_FEET || g_partSel == MobLab::P_HANDS;
			EnableWindow(g_jy, leg); EnableWindow(g_jz, leg);
			if (leg && g_hasJoint[g_partSel]) { SetF(g_jy, g_joint[g_partSel][1]); SetF(g_jz, g_joint[g_partSel][2]); }
			else { SetWindowTextA(g_jy, ""); SetWindowTextA(g_jz, ""); }
			char jb[200]; sprintf_s(jb, leg ? (g_hasJoint[g_partSel] ? "Gelenk (Voxel der Originalform): y vorn, z hoch. Oberkante %d Lagen verlaengert." : "noch kein Gelenk -- Werte eintragen und anwenden (z = Oberkante - 1)") : "Gelenke gibt es fuer Fuesse und (Vierbeiner) Haende.", g_ext);
			SetWindowTextA(g_jInfo, jb);
		}
	}
	static const char* g_mobPartHint(int i) {
		switch (i) {
			case MobLab::P_HANDS: return "x = Abstand zur Mitte (beide Seiten), y = vorn/hinten (+2.1 vom Spiel), z = Hoehe (Zweibeiner -4.1 vom Spiel).";
			case MobLab::P_FEET:  return "x = Abstand zur Mitte (beide Seiten), y = vorn/hinten, z = Hoehe. Unterkante sollte auf dem Gitter stehen.";
			case MobLab::P_TAIL:  return "Position relativ zum Koerper; Wurzel = vorderes Ende des Schwanzmodells.";
			case MobLab::P_WINGS: return "Drehpunkt = Fluegelwurzel; x = Abstand zur Mitte.";
			case MobLab::P_HEAD: case MobLab::P_HAIR: return "Kopf und Haare teilen sich Position und Skala (Kopfmitte).";
		}
		return "Modell-Einheiten (1 Voxel bei Skala 1).";
	}
	static void LoadSel() {
		int i = (int)SendMessageA(g_list, LB_GETCURSEL, 0, 0);
		if (i < 0 || i >= (int)g_paths.size()) return;
		if (g_dirty && g_sel >= 0 && i != g_sel &&
			MessageBoxA(g_page, "Aenderungen am aktuellen Tier verwerfen?", "Tier-Werkstatt", MB_YESNO | MB_ICONQUESTION) != IDYES) {
			SendMessageA(g_list, LB_SETCURSEL, g_sel, 0); return;
		}
		std::string err; MobLab::CustomMob m;
		if (!MobLab::LoadMob(g_paths[i], m, err)) { Status("Laden fehlgeschlagen: " + err); return; }
		g_mob = m; g_have = true; g_sel = i; g_dirty = false; g_files.clear(); ReadJoints(g_paths[i]);
		SetF(g_faktor, g_mob.sizeFactor);
		ShowPart(); Redraw();
		Status("Geladen: " + g_paths[i]);
	}
	static void FromFields() {
		if (!g_have) return;
		MobLab::Part& p = g_mob.parts[g_partSel];
		float v[4]; for (int k = 0; k < 4; k++) v[k] = (float)atof(Txt(g_e[k]).c_str());
		if (v[3] > 0) p.scale = v[3];
		bool wantPos = SendMessageA(g_setPos, BM_GETCHECK, 0, 0) == BST_CHECKED;
		if (g_partSel != MobLab::P_SHOULDER && wantPos) { p.setPos = true; for (int k = 0; k < 3; k++) p.pos[k] = v[k]; }
		else p.setPos = false;
		if (g_partSel == MobLab::P_HEAD) { MobLab::Part& hr = g_mob.parts[MobLab::P_HAIR]; if (hr.source != MobLab::SRC_NONE) { hr.setPos = p.setPos; memcpy(hr.pos, p.pos, 12); hr.scale = p.scale; } }
		float f = (float)atof(Txt(g_faktor).c_str()); if (f > 0.05f) g_mob.sizeFactor = f;
		g_dirty = true; Redraw();
	}
	static void Nudge(int field, int dir) {
		float step = (float)atof(Txt(g_step).c_str()); if (step <= 0) step = 0.5f;
		if (field == 3) step = 0.05f;                      // Skala immer in 0.05-Schritten
		float v = (float)atof(Txt(g_e[field]).c_str()) + dir * step;
		if (field == 3 && v < 0.05f) v = 0.05f;
		SetF(g_e[field], v);
		if (field < 3) SendMessageA(g_setPos, BM_SETCHECK, BST_CHECKED, 0);
		FromFields();
	}
	// .mob zurueckschreiben: NUR die teil=-Zeilen und faktor= ersetzen, alle anderen Zeilen (hals=, Kommentare ...) bleiben.
	static void Save() {
		if (!g_have || g_sel < 0) return;
		FromFields();
		const std::string& path = g_paths[g_sel];
		std::ifstream in(path); std::vector<std::string> lines; std::string l;
		while (std::getline(in, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); }
		in.close();
		bool done[8] = {}, fak = false;
		auto partLine = [](int i) {
			const MobLab::Part& p = g_mob.parts[i]; char b[700];
			sprintf_s(b, "teil=%d;%d;%d;%s;%g;%d;%g;%g;%g", i, p.source, p.nativeIndex, p.file.c_str(), roundf(p.scale * 1000) / 1000, p.setPos ? 1 : 0,
			          roundf(p.pos[0] * 100) / 100, roundf(p.pos[1] * 100) / 100, roundf(p.pos[2] * 100) / 100);
			return std::string(b);
		};
		for (std::string& s : lines) {
			if (s.compare(0, 5, "teil=") == 0) { int i = atoi(s.c_str() + 5); if (i >= 0 && i < 8) { s = partLine(i); done[i] = true; } }
			else if (s.compare(0, 7, "faktor=") == 0) { char b[40]; sprintf_s(b, "faktor=%g", g_mob.sizeFactor); s = b; fak = true; }
		}
		if (!fak) { char b[40]; sprintf_s(b, "faktor=%g", g_mob.sizeFactor); lines.insert(lines.begin(), b); }
		for (int i = 0; i < 8; i++) if (!done[i]) lines.push_back(partLine(i));
		// Gelenke: alte Zeilen raus, aktuelle hinter die teil=-Zeilen
		lines.erase(std::remove_if(lines.begin(), lines.end(), [](const std::string& x) { return x.compare(0, 7, "gelenk=") == 0 || x.compare(0, 21, "gelenk_verlaengerung=") == 0; }), lines.end());
		{
			size_t at = 0; for (size_t i = 0; i < lines.size(); i++) if (lines[i].compare(0, 5, "teil=") == 0) at = i + 1;
			std::vector<std::string> jl;
			for (int k = 0; k < 8; k++) if (g_hasJoint[k]) { char b[96]; sprintf_s(b, "gelenk=%d;%g;%g;%g", k, roundf(g_joint[k][0] * 100) / 100, roundf(g_joint[k][1] * 100) / 100, roundf(g_joint[k][2] * 100) / 100); jl.push_back(b); }
			if (!jl.empty()) { char b[48]; sprintf_s(b, "gelenk_verlaengerung=%d", g_ext); jl.push_back(b); }
			lines.insert(lines.begin() + (std::min)(at, lines.size()), jl.begin(), jl.end());
		}
		std::ofstream out(path, std::ios::binary);
		if (!out) { Status("Nicht schreibbar: " + path); return; }
		for (const std::string& s : lines) out << s << "\n";
		g_dirty = false;
		Status("Gespeichert: " + path + "  (im Spiel: neu spawnen, eigene Tiere lesen ihre .mob beim Spawnen)");
		LabBridge::Log("[Tier-Werkstatt] gespeichert: " + path);
	}

	// ---- Gelenk anwenden (wie tierbild.py gelenke/pad_to_joint) -----------------------------------------------------
	// Originalform <datei>.orig.cub (fehlt sie: aktuelle Datei wird dazu), Oberkante um g_ext Lagen verlaengert, dann
	// so aufgefuellt, dass das Gelenk die Modellmitte ist; die Position wird so verschoben, dass das Bein in Ruhe bleibt.
	static Lab::Structure Extended(const Lab::Structure& b, int n) {
		if (n <= 0) return b;
		int top = -1; for (int z = 0; z < b.sz; z++) for (int y = 0; y < b.sy; y++) for (int x = 0; x < b.sx; x++) if (b.Solid(x, y, z)) top = std::max(top, z);
		if (top < 0) return b;
		Lab::Structure o; o.Create(b.sx, b.sy, std::max(b.sz, top + 1 + n));
		for (int z = 0; z < b.sz; z++) for (int y = 0; y < b.sy; y++) for (int x = 0; x < b.sx; x++) { uint8_t r, g, c; if (b.Get(x, y, z, r, g, c)) o.Set(x, y, z, r, g, c); }
		for (int k = 1; k <= n; k++) for (int y = 0; y < b.sy; y++) for (int x = 0; x < b.sx; x++) { uint8_t r, g, c; if (b.Get(x, y, top, r, g, c)) o.Set(x, y, top + k, r, g, c); }
		return o;
	}
	static void PadAxis(int n, float c, int* m2out, int* off) {                // Python: m2 = ceil(2*half), off = round(m2/2 - c) (halb zu gerade)
		float half = std::max(c, n - c); int m2 = (int)ceilf(2.0f * half - 1e-6f);
		std::fesetround(FE_TONEAREST); int o = (int)std::nearbyint(m2 / 2.0 - c);
		*m2out = std::max(m2, n + o); *off = o;
	}
	static Lab::Structure Pad(const Lab::Structure& v, float jy, float jz, int* oy, int* oz) {
		int ny, nz; PadAxis(v.sy, jy, &ny, oy); PadAxis(v.sz, jz, &nz, oz);
		Lab::Structure o; o.Create(v.sx, ny, nz);
		for (int z = 0; z < v.sz; z++) for (int y = 0; y < v.sy; y++) for (int x = 0; x < v.sx; x++) {
			uint8_t r, g, c; if (!v.Get(x, y, z, r, g, c)) continue;
			int yy = y + *oy, zz = z + *oz; if (yy >= 0 && zz >= 0 && yy < ny && zz < nz) o.Set(x, yy, zz, r, g, c);
		}
		return o;
	}
	static bool ApplyJoint(int k, float jy, float jz, std::string& msg) {
		MobLab::Part& p = g_mob.parts[k];
		if (p.source != MobLab::SRC_FILE) { msg = "Gelenke nur fuer eigene .cub-Beine."; return false; }
		std::string fp = Resolve(p.file, g_mob.baseDir), op = fp.substr(0, fp.size() - 4) + ".orig.cub", err;
		if (GetFileAttributesA(op.c_str()) == INVALID_FILE_ATTRIBUTES && !g_hasJoint[k]) CopyFileA(fp.c_str(), op.c_str(), TRUE);
		Lab::Structure orig, cur;
		if (!Lab::LoadCub(op, orig, err) || !Lab::LoadCub(fp, cur, err)) { msg = "Bein-Modell nicht lesbar: " + err; return false; }
		int oy0 = 0, oz0 = 0;
		if (g_hasJoint[k]) { Lab::Structure bp = Extended(orig, g_ext); Pad(bp, g_joint[k][1], g_joint[k][2], &oy0, &oz0); }
		const float oldC[3] = { cur.sx * 0.5f, cur.sy * 0.5f - oy0, cur.sz * 0.5f - oz0 };
		int oy, oz; Lab::Structure nw = Pad(Extended(orig, g_ext), jy, jz, &oy, &oz);
		const float newC[3] = { nw.sx * 0.5f, nw.sy * 0.5f - oy, nw.sz * 0.5f - oz };
		Eff e = Effective(k);
		if (!p.setPos) { memcpy(p.pos, e.pos, 12); p.setPos = true; }
		if (p.scale <= 0) p.scale = e.scale;
		p.pos[1] += (newC[1] - oldC[1]) * e.scale; p.pos[2] += (newC[2] - oldC[2]) * e.scale;
		if (!Lab::SaveCub(fp, nw, err)) { msg = "nicht schreibbar: " + err; return false; }
		g_files.erase(fp);
		g_hasJoint[k] = true; g_joint[k][0] = orig.sx * 0.5f; g_joint[k][1] = jy; g_joint[k][2] = jz; g_dirty = true;
		char b[200]; sprintf_s(b, "Gelenk %s: y %.1f z %.1f -> Modell %dx%dx%d (speichern nicht vergessen).", k == MobLab::P_FEET ? "Hinterbeine" : "Vorderbeine", jy, jz, nw.sx, nw.sy, nw.sz);
		msg = b; return true;
	}

	// ---- Datei-API (fuer Claude und Skripte) ------------------------------------------------------------------------
	// Ordner Strukturen\LabApi: jede Datei auftrag*.txt ist ein Auftrag (Schluessel=Wert je Zeile):
	//   tier=giraffe          (.mob-Name oder Pfad relativ zu Strukturen\Mobs)
	//   art=ruhe|gehen|biss|sturzbiss|kopfstoss|stampfen|hornangriff
	//   zeiten=0,130,260      (ms; je Zeit ein Bild nebeneinander)   drehung=-35  neigung=22  groesse=420 (Kachel)
	//   bild=giraffe_gehen.png  (in Strukturen\LabApi\bilder)
	// Ergebnis: Bild + Zeile in Strukturen\LabApi\antwort.txt; der Auftrag wird in *.erledigt umbenannt.
	static int EncoderClsid(const WCHAR* fmt, CLSID* c) {
		UINT n = 0, sz = 0; Gdiplus::GetImageEncodersSize(&n, &sz); if (!sz) return -1;
		std::vector<uint8_t> buf(sz); Gdiplus::ImageCodecInfo* ic = (Gdiplus::ImageCodecInfo*)buf.data();
		Gdiplus::GetImageEncoders(n, sz, ic);
		for (UINT i = 0; i < n; i++) if (wcscmp(ic[i].MimeType, fmt) == 0) { *c = ic[i].Clsid; return (int)i; }
		return -1;
	}
	static bool SavePng(const std::string& path, int W, int H, const std::vector<uint32_t>& px) {
		static ULONG_PTR tok = 0; if (!tok) { Gdiplus::GdiplusStartupInput in; Gdiplus::GdiplusStartup(&tok, &in, nullptr); }
		Gdiplus::Bitmap bmp(W, H, W * 4, PixelFormat32bppRGB, (BYTE*)px.data());
		CLSID c; if (EncoderClsid(L"image/png", &c) < 0) return false;
		wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wp, MAX_PATH);
		return bmp.Save(wp, &c, nullptr) == Gdiplus::Ok;
	}
	static std::string ApiDir() { return Lab::StructuresDir() + "\\LabApi"; }
	struct ApiJob { std::string file; DWORD start; };
	static ApiJob g_job; static bool g_jobOn = false;
	static std::string FindMobPath(const std::string& name) {
		std::vector<std::string> keep = g_paths; g_paths.clear(); Scan(MobLab::MobsDir(), 0);
		std::string hit;
		for (const std::string& p : g_paths) {
			std::string base = p.substr(p.find_last_of('\\') + 1); base.resize(base.size() - 4);
			if (_stricmp(base.c_str(), name.c_str()) == 0 || _stricmp(p.c_str(), (MobLab::MobsDir() + "\\" + name).c_str()) == 0) { hit = p; break; }
		}
		g_paths = keep; return hit;
	}
	static void ApiAnswer(const std::string& line) {
		std::ofstream f(ApiDir() + "\\antwort.txt", std::ios::app); f << line << "\n";
		LabBridge::Log("[LAB-API] " + line);
	}
	void ApiPoll() {
		static DWORD last = 0; DWORD now = GetTickCount();
		if (now - last < 400) return;
		last = now;
		if (!g_jobOn) {
			// Veroeffentlichung (2026-10-29): die Datei-API ist nur aktiv, wenn der Ordner Strukturen\\LabApi schon da ist
			// (Entwickler legen ihn selbst an) -- der Mod legt ihn nicht mehr an.
			DWORD at = GetFileAttributesA(ApiDir().c_str());
			if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) return;
			Lab::EnsureDir(ApiDir() + "\\bilder");
			WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((ApiDir() + "\\auftrag*.txt").c_str(), &fd);
			if (h == INVALID_HANDLE_VALUE) return;
			g_job.file = ApiDir() + "\\" + fd.cFileName; g_job.start = now; g_jobOn = true; FindClose(h);
		}
		std::map<std::string, std::string> kv;
		{ std::ifstream f(g_job.file); std::string l; while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); size_t e = l.find('='); if (e != std::string::npos && l[0] != '#') kv[l.substr(0, e)] = l.substr(e + 1); } }
		auto Finish = [&](const std::string& msg) {
			ApiAnswer(g_job.file.substr(g_job.file.find_last_of('\\') + 1) + ": " + msg);
			std::string done = g_job.file.substr(0, g_job.file.size() - 4) + ".erledigt";
			MoveFileExA(g_job.file.c_str(), done.c_str(), MOVEFILE_REPLACE_EXISTING); g_jobOn = false;
		};
		std::string path = FindMobPath(kv["tier"]);
		if (path.empty()) { Finish("Tier '" + kv["tier"] + "' nicht gefunden"); return; }
		// Stand der Seite sichern, Auftragstier laden
		MobLab::CustomMob keepMob = g_mob; bool keepHave = g_have; int keepArt = g_art; float keepT = g_time, keepYaw = g_yaw, keepPitch = g_pitch, keepZoom = g_zoomMul;
		bool keepJ[8]; float keepJv[8][3]; int keepExt = g_ext; std::string keepPath = g_mobPath; memcpy(keepJ, g_hasJoint, sizeof(keepJ)); memcpy(keepJv, g_joint, sizeof(keepJv));
		std::string err; MobLab::CustomMob m;
		if (!MobLab::LoadMob(path, m, err)) { Finish("nicht lesbar: " + err); return; }
		g_mob = m; g_have = true; ReadJoints(path);
		g_art = 0; for (int i = 0; i < 7; i++) if (_stricmp(kv["art"].c_str(), kArtKeys[i]) == 0) g_art = i;
		if (kv.count("drehung")) g_yaw = (float)atof(kv["drehung"].c_str());
		if (kv.count("neigung")) g_pitch = (float)atof(kv["neigung"].c_str());
		g_zoomMul = 1.0f;
		int tile = kv.count("groesse") ? std::max(120, std::min(1200, atoi(kv["groesse"].c_str()))) : 420;
		std::vector<float> times; { std::string z = kv.count("zeiten") ? kv["zeiten"] : "0"; std::stringstream ss(z); std::string t; while (std::getline(ss, t, ',')) times.push_back((float)atof(t.c_str())); }
		if (times.empty()) times.push_back(0);
		bool missing = false; for (int i = 0; i < 8; i++) { Eff e = Effective(i); if (e.on && e.missing) missing = true; }   // native Teile: bis 6 s auf das Spiel warten
		std::string msg;
		if (!(missing && now - g_job.start < 6000)) {
			const int W = tile * (int)times.size(), H = tile;
			std::vector<uint32_t> all((size_t)W * H, 0x262B36), one;
			for (size_t i = 0; i < times.size(); i++) {
				g_time = times[i]; RenderBuf(tile, tile, one, true);
				for (int y = 0; y < tile; y++) memcpy(&all[(size_t)y * W + i * tile], &one[(size_t)y * tile], tile * 4);
			}
			std::string name = kv.count("bild") ? kv["bild"] : kv["tier"] + "_" + kArtKeys[g_art] + ".png";
			{   // nur ein Dateiname im Ordner bilder (kein Pfad, kein "..", Endung .png)
				size_t cut = name.find_last_of("\\/:"); if (cut != std::string::npos) name = name.substr(cut + 1);
				std::string clean; for (char ch : name) if (isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.') clean += ch;
				while (clean.find("..") != std::string::npos) clean.erase(clean.find(".."), 1);
				if (clean.empty() || clean[0] == '.') clean = "bild" + clean;
				if (clean.size() < 4 || _stricmp(clean.c_str() + clean.size() - 4, ".png") != 0) clean += ".png";
				name = clean;
			}
			std::string out = ApiDir() + "\\bilder\\" + name;
			msg = SavePng(out, W, H, all) ? ("Bild " + out + (missing ? " (native Teile fehlen -- im Spiel sein)" : "") + (JointsOn() ? ", Gelenke an" : ", ohne Gelenke")) : "Bild nicht speicherbar";
		}
		g_mob = keepMob; g_have = keepHave; g_art = keepArt; g_time = keepT; g_yaw = keepYaw; g_pitch = keepPitch; g_zoomMul = keepZoom;
		memcpy(g_hasJoint, keepJ, sizeof(keepJ)); memcpy(g_joint, keepJv, sizeof(keepJv)); g_ext = keepExt; g_mobPath = keepPath;
		if (!msg.empty()) Finish(msg);
	}

	// ---- Seite --------------------------------------------------------------------------------------------------
	static HWND C(HWND parent, const char* cls, const char* t, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
		HWND c = CreateWindowExA(ex, cls, t, WS_CHILD | WS_VISIBLE | st, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
		SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
	}
	LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		switch (m) {
			case WM_CREATE: {
				g_page = h;
				HWND t = C(h, "STATIC", "Tier-Werkstatt: eigene Tiere so zusammengesetzt, wie das Spiel sie zeichnet", 0, 12, 10, 700, 18, -1);
				SendMessageA(t, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
				g_list = C(h, "LISTBOX", "", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 12, 36, 230, 560, ID_LIST, WS_EX_CLIENTEDGE);
				C(h, "BUTTON", "Liste neu", BS_PUSHBUTTON, 12, 602, 110, 26, ID_RELOAD);
				g_view = CreateWindowExA(WS_EX_CLIENTEDGE, "CWLabTierView", "", WS_CHILD | WS_VISIBLE, 252, 36, 600, 560, h, NULL, GetModuleHandleA(NULL), NULL);
				C(h, "BUTTON", "<", BS_PUSHBUTTON, 252, 602, 40, 26, ID_LEFT);
				C(h, "BUTTON", ">", BS_PUSHBUTTON, 296, 602, 40, 26, ID_RIGHT);
				C(h, "BUTTON", "Vorne", BS_PUSHBUTTON, 340, 602, 70, 26, ID_FRONT);
				C(h, "BUTTON", "Seite", BS_PUSHBUTTON, 414, 602, 70, 26, ID_SIDE);
				g_dim = C(h, "BUTTON", "gewaehltes Teil hervorheben", BS_AUTOCHECKBOX, 494, 604, 240, 22, ID_DIM);
				int X = 866;
				C(h, "STATIC", "Teil:", 0, X, 38, 40, 18, -1);
				g_part = C(h, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, X + 44, 34, 220, 300, ID_PART);
				for (const char* n : kPartNames) SendMessageA(g_part, CB_ADDSTRING, 0, (LPARAM)n);
				SendMessageA(g_part, CB_SETCURSEL, g_partSel, 0);
				const char* lab[4] = { "X (Seite)", "Y (vorn)", "Z (hoch)", "Skala" };
				for (int k = 0; k < 4; k++) {
					int y = 72 + k * 32;
					C(h, "STATIC", lab[k], 0, X, y + 4, 70, 18, -1);
					C(h, "BUTTON", "-", BS_PUSHBUTTON, X + 74, y, 30, 24, ID_MINUS0 + k);
					g_e[k] = C(h, "EDIT", "0", ES_AUTOHSCROLL, X + 108, y, 80, 24, -1, WS_EX_CLIENTEDGE);
					C(h, "BUTTON", "+", BS_PUSHBUTTON, X + 192, y, 30, 24, ID_PLUS0 + k);
				}
				C(h, "STATIC", "Schritt", 0, X, 208, 70, 18, -1);
				g_step = C(h, "EDIT", "0.5", ES_AUTOHSCROLL, X + 108, 204, 80, 24, -1, WS_EX_CLIENTEDGE);
				g_setPos = C(h, "BUTTON", "eigene Position (sonst Basisrasse)", BS_AUTOCHECKBOX, X, 236, 260, 22, -1);
				C(h, "STATIC", "Faktor", 0, X, 268, 70, 18, -1);
				g_faktor = C(h, "EDIT", "1", ES_AUTOHSCROLL, X + 108, 264, 80, 24, -1, WS_EX_CLIENTEDGE);
				C(h, "BUTTON", "Uebernehmen", BS_PUSHBUTTON, X, 300, 130, 28, ID_APPLY);
				C(h, "BUTTON", "Speichern (.mob)", BS_PUSHBUTTON, X + 136, 300, 130, 28, ID_SAVE);
				C(h, "BUTTON", "Testen: 40 Bloecke vor mir spawnen", BS_PUSHBUTTON, X, 334, 266, 28, ID_SPAWN);
				g_info = C(h, "STATIC", "", SS_LEFT, X, 372, 300, 130, -1);
				C(h, "STATIC", "Werkzeug: Projekt\\Werkzeuge\\Mobs\\tierbild.py (--ausrichten, --gelenke, --gang). "
				               "Datei-API: Strukturen\\LabApi\\auftrag*.txt -> bilder\\*.png (siehe LAB_ANLEITUNG).",
				  SS_LEFT, X, 506, 300, 70, -1);
				g_artBox = C(h, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 252, 636, 150, 300, ID_ART);
				for (const char* n : kArtNames) SendMessageA(g_artBox, CB_ADDSTRING, 0, (LPARAM)n);
				SendMessageA(g_artBox, CB_SETCURSEL, 0, 0);
				g_playBox = C(h, "BUTTON", "abspielen", BS_AUTOCHECKBOX, 410, 638, 90, 22, ID_PLAY);
				C(h, "STATIC", "Zeit ms", 0, 506, 641, 50, 18, -1);
				g_timeEd = C(h, "EDIT", "0", ES_AUTOHSCROLL, 560, 636, 70, 24, ID_TIME, WS_EX_CLIENTEDGE);
				C(h, "BUTTON", "Bild -> LabApi\\bilder", BS_PUSHBUTTON, 640, 635, 170, 26, ID_SHOT);
				C(h, "STATIC", "Gelenk Y", 0, X, 584, 64, 18, -1); g_jy = C(h, "EDIT", "", ES_AUTOHSCROLL, X + 66, 580, 60, 24, -1, WS_EX_CLIENTEDGE);
				C(h, "STATIC", "Z", 0, X + 134, 584, 14, 18, -1); g_jz = C(h, "EDIT", "", ES_AUTOHSCROLL, X + 150, 580, 60, 24, -1, WS_EX_CLIENTEDGE);
				C(h, "BUTTON", "anwenden", BS_PUSHBUTTON, X + 216, 579, 84, 26, ID_JAPPLY);
				g_jInfo = C(h, "STATIC", "", SS_LEFT, X, 608, 300, 34, -1);
				g_status = C(h, "STATIC", "", SS_LEFT | SS_ENDELLIPSIS, 12, 670, 1150, 18, -1);
				FillList();
				SetTimer(h, ID_TIMER, 400, NULL);
				return 0;
			}
			case WM_TIMER:
				if (wp == ID_TIMER && InterlockedExchange(&g_natNew, 0)) { if (g_have) ShowPart(); Redraw(); }
				if (wp == ID_ANIM && g_play && g_have) {
					g_time += 33.0f;
					const float loop = g_art == 1 ? 1047.2f : (g_art >= 2 ? ArtDur(g_art) + 500.0f : 1000.0f);
					if (g_time > loop) g_time = 0.0f;
					SetF(g_timeEd, g_time); Redraw();
				}
				return 0;
			case WM_COMMAND: {
				int id = LOWORD(wp), code = HIWORD(wp);
				if (id == ID_LIST && (code == LBN_SELCHANGE || code == LBN_DBLCLK)) { LoadSel(); return 0; }
				if (id == ID_PART && code == CBN_SELCHANGE) { int s = (int)SendMessageA(g_part, CB_GETCURSEL, 0, 0); if (s >= 0) g_partSel = s; if (g_have) ShowPart(); Redraw(); return 0; }
				if (id >= ID_MINUS0 && id < ID_MINUS0 + 4) { if (g_have) Nudge(id - ID_MINUS0, -1); return 0; }
				if (id >= ID_PLUS0 && id < ID_PLUS0 + 4) { if (g_have) Nudge(id - ID_PLUS0, +1); return 0; }
				switch (id) {
					case ID_RELOAD: FillList(); break;
					case ID_ART: if (code == CBN_SELCHANGE) { int a = (int)SendMessageA(g_artBox, CB_GETCURSEL, 0, 0); g_art = a < 0 ? 0 : a; g_time = 0; SetF(g_timeEd, 0); Redraw(); } break;
					case ID_PLAY: g_play = SendMessageA(g_playBox, BM_GETCHECK, 0, 0) == BST_CHECKED; if (g_play) SetTimer(h, ID_ANIM, 33, NULL); else KillTimer(h, ID_ANIM); break;
					case ID_TIME: if (code == EN_KILLFOCUS) { g_time = (float)atof(Txt(g_timeEd).c_str()); Redraw(); } break;
					case ID_JAPPLY:
						if (g_have && (g_partSel == MobLab::P_FEET || g_partSel == MobLab::P_HANDS)) {
							std::string msg; ApplyJoint(g_partSel, (float)atof(Txt(g_jy).c_str()), (float)atof(Txt(g_jz).c_str()), msg);
							Status(msg); ShowPart(); Redraw();
						}
						break;
					case ID_SHOT:
						if (g_have && g_sel >= 0) {
							std::vector<uint32_t> px; RenderBuf(640, 560, px, true);
							std::string n = g_paths[g_sel].substr(g_paths[g_sel].find_last_of('\\') + 1); n.resize(n.size() - 4);
							Lab::EnsureDir(ApiDir()); Lab::EnsureDir(ApiDir() + "\\bilder");
							std::string out = ApiDir() + "\\bilder\\" + n + "_" + kArtKeys[g_art] + "_" + std::to_string((int)g_time) + ".png";
							Status(SavePng(out, 640, 560, px) ? "Bild: " + out : "Bild nicht speicherbar");
						}
						break;
					case ID_APPLY: FromFields(); ShowPart(); break;
					case ID_SAVE: Save(); break;
					case ID_DIM: Redraw(); break;
					case ID_LEFT: g_yaw -= 30; Redraw(); break;
					case ID_RIGHT: g_yaw += 30; Redraw(); break;
					case ID_FRONT: g_yaw = -35; g_pitch = 22; g_zoomMul = 1; Redraw(); break;
					case ID_SIDE: g_yaw = -90; g_pitch = 0; g_zoomMul = 1; Redraw(); break;
					case ID_SPAWN:
						if (g_have) {
							FromFields(); MobLab::CustomMob mm = g_mob;
							LabBridge::Post("Tier-Werkstatt: testen", [mm](cube::GameController* gc) { std::string r; MobLab::SpawnCustom(gc, mm, 40, r); LabBridge::Log("[Tier-Werkstatt] " + r); });
							Status("Spawn angefordert (ungespeicherter Stand) -- 40 Bloecke vor dir.");
						}
						break;
				}
				return 0;
			}
			case WM_DESTROY: KillTimer(h, ID_TIMER); KillTimer(h, ID_ANIM); g_page = nullptr; return 0;
		}
		return DefWindowProcA(h, m, wp, lp);
	}

	void SetFonts(HFONT normal, HFONT bold) { g_font = normal; g_fontBold = bold; }
	void RegisterClasses(HINSTANCE hi) {
		WNDCLASSEXA wc{}; wc.cbSize = sizeof(wc); wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.lpfnWndProc = PageProc; wc.lpszClassName = "CWLabTiere"; RegisterClassExA(&wc);
		wc.hbrBackground = NULL; wc.lpfnWndProc = ViewProc; wc.lpszClassName = "CWLabTierView"; wc.hCursor = LoadCursor(NULL, IDC_SIZEALL); RegisterClassExA(&wc);
	}
}
