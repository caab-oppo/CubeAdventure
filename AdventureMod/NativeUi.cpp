// NativeUi.cpp -- Fenster im Stil des Spiels. Siehe NativeUi.h (alle Adressen und Belege dort).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "NativeUi.h"
#include "Host.h"
#include "LabBridge.h"
#include "LabWindow.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <algorithm>

namespace NativeUi {
	static uint32_t Re(uint32_t a) { return Host::Re(a); }
	static const uint32_t G_ENGINE = 0x800710, G_ROOT = 0x800884, G_BLACK = 0x8008C8, G_WORLDNAME = 0x8009BC, G_CHAT = 0x800A14;
	static const uint32_t G_W = 0x11C, G_H = 0x120;                     // Fenstergroesse (resize 0x482FA3/0x482F8A)
	static const uint32_t E_FOCUS = 0xCC, W_ENABLED = 0x15C, CHAT_ACTIVE = 0x180;

	typedef void* (__fastcall* Instance_t)(void* tmpl, void* edx, void* parent);                    // 0x636040
	typedef void* (__fastcall* Clone_t)(void* tmpl, void* edx, void* parent);                       // 0x6326D0
	typedef void* (__fastcall* VClone_t)(void* obj, void* edx);                                     // vtbl[2]
	typedef void  (__fastcall* SetObj_t)(void* node, void* edx, void* obj);                         // 0x636B70 / 0x6368E0
	typedef void  (__fastcall* Resize_t)(void* w, void* edx, float x, float y, int f);              // 0x62C570
	typedef void  (__fastcall* MoveTo_t)(void* w, void* edx, float x, float y, int f);              // 0x62A650
	typedef float (__fastcall* Dim_t)(void* w, void* edx);                                          // 0x62F600 / 0x6291D0
	typedef void  (__fastcall* SetVis_t)(void* n, void* edx, int v);                                // 0x411A90
	typedef void  (__fastcall* SetText3_t)(void* n, void* edx, const void* name, const void* text, int rec);   // 0x636A00
	typedef void* (__fastcall* GetText_t)(void* n, void* edx, void* out, const void* name, int rec);           // 0x635550
	typedef void  (__fastcall* WCtor_t)(void* ws, void* edx, const wchar_t* s);                     // 0x40EB60
	typedef void  (__fastcall* WDtor_t)(void* ws, void* edx);                                       // 0x593E50
	typedef void  (__fastcall* TRefresh_t)(void* t, void* edx, int f);                             // Transformation vtbl[1]
	typedef void  (__fastcall* InitW_t)(void* n, void* edx);                                        // 0x635700 Node::initializeWidgets

	// ---- Bausteine (SEH, keine Objekte mit Destruktor) ---------------------------------------------------------
	static void InitWidgets(uint8_t* n) { if (n) ((InitW_t)Re(0x635700))(n, nullptr); }
	static uint8_t* MakePanelRaw(uint8_t* G) {
		uint8_t* tmpl = *(uint8_t**)(G + G_BLACK); void* root = *(void**)(G + G_ROOT);
		if (!tmpl || !root) return nullptr;
		uint8_t* p = (uint8_t*)((Instance_t)Re(0x636040))(tmpl, nullptr, root);
		if (!p) return nullptr;
		void* t = *(void**)(p + 0x38); void* d = *(void**)(p + 0x3C);
		if (t) { void* tc = ((VClone_t)(*(void***)t)[2])(t, nullptr); ((SetObj_t)Re(0x636B70))(p, nullptr, tc); }
		if (d) { void* dc = ((VClone_t)(*(void***)d)[2])(d, nullptr); ((SetObj_t)Re(0x6368E0))(p, nullptr, dc); }
		InitWidgets(p);
		return p;
	}
	// Klone brauchen initialize(): Edit(const Edit&, Node*) setzt m_textShape = 0 (Infinite-Cubes plasma Edit.cpp:32),
	// erst Edit::initialize holt die Textform des Knotens -- ohne sie verwirft characterEntered jedes Zeichen.
	// Das Spiel ruft dafuer einmal root->initializeWidgets() (GameController.cpp:1962); Original 0x635700
	// (Knoten sichtbar -> widget vtbl[7] initialize, dann alle Kinder; 0x635712..0x63574D).
	static uint8_t* MakeRowRaw(uint8_t* G) {
		uint8_t* tmpl = *(uint8_t**)(G + G_WORLDNAME); void* root = *(void**)(G + G_ROOT);
		if (!tmpl || !root) return nullptr;
		uint8_t* r = (uint8_t*)((Clone_t)Re(0x6326D0))(tmpl, nullptr, root);
		InitWidgets(r);
		return r;
	}
	static bool PanelPlaceRaw(uint8_t* p, float w, float h, float x, float y) {
		__try {
			void* wd = *(void**)(p + 0x40); if (!wd) return false;
			((Resize_t)Re(0x62C570))(wd, nullptr, w, h, 1);
			((MoveTo_t)Re(0x62A650))(wd, nullptr, x, y, 1);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static bool NodePosRaw(uint8_t* n, float x, float y) {
		__try {
			uint8_t* t = *(uint8_t**)(n + 0x38); if (!t) return false;
			int i = *(int*)(t + 0x68); float* a = *(float**)(t + 0x94); if (!a || i < 0 || i > 4096) return false;
			a[i * 2] = x; a[i * 2 + 1] = y;
			((TRefresh_t)(*(void***)t)[1])(t, nullptr, 1);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	static void VisRaw(uint8_t* n, int v) { __try { if (n) ((SetVis_t)Re(0x411A90))(n, nullptr, v); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void SetTextRaw(uint8_t* n, const wchar_t* name, const wchar_t* text) {
		alignas(8) uint8_t a[32] = {}, b[32] = {};
		__try {
			((WCtor_t)Re(0x40EB60))(a, nullptr, name); ((WCtor_t)Re(0x40EB60))(b, nullptr, text);
			((SetText3_t)Re(0x636A00))(n, nullptr, a, b, 1);
			((WDtor_t)Re(0x593E50))(a, nullptr); ((WDtor_t)Re(0x593E50))(b, nullptr);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	static bool GetTextRaw(uint8_t* n, const wchar_t* name, wchar_t* out, int cap) {
		alignas(8) uint8_t a[32] = {}, r[32] = {};
		out[0] = 0;
		__try {
			((WCtor_t)Re(0x40EB60))(a, nullptr, name);
			((GetText_t)Re(0x635550))(n, nullptr, r, a, 1);
			size_t len = *(size_t*)(r + 0x10), c = *(size_t*)(r + 0x14);
			const wchar_t* p = c >= 8 ? *(const wchar_t**)r : (const wchar_t*)r;
			int k = 0; for (; p && k < cap - 1 && k < (int)len; k++) out[k] = p[k]; out[k] = 0;
			((WDtor_t)Re(0x593E50))(a, nullptr); ((WDtor_t)Re(0x593E50))(r, nullptr);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Kind mit Namen (Name std::wstring +0x0C; Kinder std::list +0x2C: Knoten next +0, Wert +8)
	static uint8_t* FindChildRaw(uint8_t* n, const wchar_t* want, int depth) {
		__try {
			uint8_t* ws = n + 0x0C; size_t len = *(size_t*)(ws + 0x10), cap = *(size_t*)(ws + 0x14);
			const wchar_t* p = cap >= 8 ? *(const wchar_t**)ws : (const wchar_t*)ws;
			if (p && len == wcslen(want) && wcsncmp(p, want, len) == 0) return n;
			if (depth > 5) return nullptr;
			uint8_t* head = *(uint8_t**)(n + 0x2C); if (!head) return nullptr;
			int k = 0;
			for (uint8_t* nd = *(uint8_t**)head; nd && nd != head && k < 32; nd = *(uint8_t**)nd, k++)
				if (uint8_t* r = FindChildRaw(*(uint8_t**)(nd + 8), want, depth + 1)) return r;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}
	static bool ScreenRaw(uint8_t* G, float* w, float* h) { __try { *w = (float)*(int*)(G + G_W); *h = (float)*(int*)(G + G_H); return *w > 100 && *h > 100; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
	static void* FocusRaw(uint8_t* G) { __try { uint8_t* e = *(uint8_t**)(G + G_ENGINE); return e ? *(void**)(e + E_FOCUS) : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
	static void SetFocusRaw(uint8_t* G, void* w) { __try { uint8_t* e = *(uint8_t**)(G + G_ENGINE); if (e) *(void**)(e + E_FOCUS) = w; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static void ChatLockRaw(uint8_t* G, bool on) { __try { uint8_t* c = *(uint8_t**)(G + G_CHAT); if (c) c[CHAT_ACTIVE] = on ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	static bool EnabledRaw(uint8_t* w) { __try { return w && w[W_ENABLED] != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

	static void Wide(const std::string& s, wchar_t* out, int cap) {
		int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, cap);
		if (n <= 0) { int k = 0; for (; k < cap - 1 && k < (int)s.size(); k++) out[k] = (unsigned char)s[k]; out[k] = 0; }
	}

	// ---- Eingabefenster -------------------------------------------------------------------------------------------
	static uint8_t* g_tPanel = nullptr; static uint8_t* g_tTitle = nullptr; static uint8_t* g_tHint = nullptr; static uint8_t* g_tEdit = nullptr;
	static volatile LONG g_typing = 0; static bool g_tMade = false;
	static const float kTW = 520.0f, kTH = 160.0f;

	bool Typing() { return g_typing != 0; }

	// ---- Spieltasten nur im Spiel (2026-10-29) ----------------------------------------------------------------
	// Wie GameController::handleKeyDown 0x47E1B0 (Infinite-Cubes GameController.cpp:4386): keine Taste, solange einer
	// der fuenf Menue-Bildschirme sichtbar ist (GC+0x800880/0x800890/0x800888/0x800894/0x80088C: Charakter erstellen,
	// Welt erstellen, Charakterliste, Verbinden, Weltliste; sichtbar = Node+0x3C -> [+0x94][+0x68 * 4] != 0), ein Widget den
	// Fokus hat (Engine GC+0x800710, +0xCC; 0x6531E0) oder die Chatzeile offen ist (ChatWidget GC+0x800A14, +0x180).
	// Dazu: Spielfenster vorn, kein LAB, keine eigene Eingabe, Spieler da und seine Zone geladen (sonst Welterzeugung).
	static const uint32_t G_SCREENS[5] = { 0x800880, 0x800890, 0x800888, 0x800894, 0x80088C };
	static volatile LONG g_play = 0; static volatile DWORD g_playAt = 0;
	static bool ShownRaw(uint8_t* node) {
		if (!node) return false;
		uint8_t* t = *(uint8_t**)(node + 0x3C); if (!t) return false;
		int i = *(int*)(t + 0x68); int* a = *(int**)(t + 0x94);
		return a && i >= 0 && i < 64 && a[i] != 0;
	}
	static int PlayRaw(uint8_t* G, int* zx, int* zy) {
		__try {
			for (uint32_t off : G_SCREENS) if (ShownRaw(*(uint8_t**)(G + off))) return 0;
			uint8_t* e = *(uint8_t**)(G + G_ENGINE); if (!e || *(void**)(e + E_FOCUS)) return 0;
			uint8_t* c = *(uint8_t**)(G + G_CHAT); if (c && c[CHAT_ACTIVE]) return 0;
			return 1;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}
	static bool PlayerZoneRaw(cube::GameController* gc, int* zx, int* zy) {
		__try {
			uint8_t* p = (uint8_t*)gc->local_player; if (!p) return false;
			int64_t x = *(int64_t*)(p + 0x10), y = *(int64_t*)(p + 0x18);
			*zx = (int)((x >> 16) >> 8); *zy = (int)((y >> 16) >> 8); return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool PlayInput(cube::GameController* gc) {
		bool ok = false;
		HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
		if (gc && pid == GetCurrentProcessId() && !LabWindow::IsForeground() && !Typing()) {
			int zx = 0, zy = 0;
			ok = PlayRaw((uint8_t*)gc, &zx, &zy) == 1 && PlayerZoneRaw(gc, &zx, &zy) && LabBridge::ZoneAt(gc, zx, zy) != nullptr;
		}
		InterlockedExchange(&g_play, ok ? 1 : 0); g_playAt = GetTickCount();
		return ok;
	}
	bool PlayInputCached() { return g_play != 0 && GetTickCount() - g_playAt < 500; }
	static void MakeTextRaw(uint8_t* G);

	// ---- Eingabefenster (2026-10-29, neu) ---------------------------------------------------------------------------
	// Vorher tippte das plasma-Edit (Klon des Weltnamen-Felds) -- im Spiel kam kein Zeichen an, sichtbar war nur die
	// gesperrte Chatzeile. Jetzt sauber ohne fremde Widgets:
	//  * Tasten: eigener Fenster-Hook (Unterklasse des Spielfensters). Solange getippt wird, gehen WM_CHAR/WM_KEYDOWN NUR
	//    an uns (Infinite-Cubes main.cpp WndProc: WM_CHAR -> Engine::characterEntered + onKeyTyped, WM_KEYDOWN ->
	//    keyPressed + handleKeyDown) -> kein Chat, kein Menue, keine Spieltaste.
	//  * Laufen: GameController::moveCharacter 0x4A6B50 kehrt sofort zurueck, solange Engine+0xCC (Fokus-Widget) gesetzt
	//    ist (0x4A6B6F..0x4A6B7C; Infinite-Cubes GameControllerMovement.cpp:44). Wir setzen dort das (unsichtbare) Feld
	//    unseres Zeilenklons -- die Chatzeile bleibt ZU.
	//  * Anzeige: Fenster aus den Spielvorlagen (Feld + Titel + Eingabezeile + Hinweis).
	static WNDPROC g_oldProc = nullptr; static HWND g_hwnd = nullptr;
	static wchar_t g_tBuf[48] = { 0 }; static int g_tLen = 0; static volatile LONG g_tDone = 0;   // 1 Enter, -1 Esc
	static wchar_t g_tTitleW[128] = { 0 }, g_tHintW[160] = { 0 };
	static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
		if (g_typing && g_tDone == 0) {
			if (m == WM_CHAR) {
				wchar_t c = (wchar_t)wp;
				if (c == 8) { if (g_tLen > 0) g_tBuf[--g_tLen] = 0; }
				else if (c >= 32 && g_tLen < 40) { g_tBuf[g_tLen++] = c; g_tBuf[g_tLen] = 0; }
				return 0;
			}
			if (m == WM_KEYDOWN) {
				if (wp == VK_RETURN) InterlockedExchange(&g_tDone, 1);
				else if (wp == VK_ESCAPE) InterlockedExchange(&g_tDone, -1);
				return 0;                                                    // keine Spieltaste waehrend der Eingabe
			}
			if (m == WM_KEYUP) return 0;
		}
		return CallWindowProcW(g_oldProc, h, m, wp, lp);
	}
	// Tastatur fuer das Spiel leeren, solange getippt wird: das Spiel liest W/A/S/D/E/R/Q/1..4 je Bild per DirectInput
	// (Infinite-Cubes main.cpp:381ff.: keyboard->GetDeviceState(256, keys) -> m_inputs; Original 0x4C91E9: push 0x100,
	// call [ecx+0x24] = IDirectInputDevice8W::GetDeviceState, IID_IDirectInput8W im Programm). Die Geraete-vtbl ist fuer
	// alle Tastatur/Maus-Geraete von dinput8 dieselbe -> Platz 9 umbiegen; nur Abfragen mit 256 Bytes (Tastatur) werden
	// waehrend der Eingabe geleert, die Maus bleibt unberuehrt.
	typedef HRESULT (WINAPI* DI8Create_t)(HINSTANCE, DWORD, const GUID&, void**, void*);
	typedef HRESULT (__stdcall* DICreateDev_t)(void* self, const GUID& g, void** dev, void* outer);
	typedef ULONG (__stdcall* Release_t)(void* self);
	typedef HRESULT (__stdcall* GetState_t)(void* self, DWORD cb, void* data);
	static GetState_t g_origGetState = nullptr; static bool g_diTried = false;
	static HRESULT __stdcall HookGetState(void* self, DWORD cb, void* data) {
		HRESULT hr = g_origGetState(self, cb, data);
		if (g_typing && cb == 256 && data && SUCCEEDED(hr)) memset(data, 0, 256);
		return hr;
	}
	static void HookDInput() {
		if (g_diTried) return;
		g_diTried = true;
		static const GUID kIID_DI8W = { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
		static const GUID kSysKeyboard = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
		HMODULE m = GetModuleHandleA("dinput8.dll"); if (!m) m = LoadLibraryA("dinput8.dll");
		DI8Create_t cr = m ? (DI8Create_t)GetProcAddress(m, "DirectInput8Create") : nullptr;
		void* di = nullptr; void* dev = nullptr;
		if (!cr || FAILED(cr(GetModuleHandleA(NULL), 0x0800, kIID_DI8W, &di, nullptr)) || !di) { std::cout << "[UI] DirectInput-Hook: kein DirectInput8." << std::endl; return; }
		void** dvt = *(void***)di;
		if (SUCCEEDED(((DICreateDev_t)dvt[3])(di, kSysKeyboard, &dev, nullptr)) && dev) {
			void** vt = *(void***)dev; DWORD old;
			if (VirtualProtect(&vt[9], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
				if (vt[9] != (void*)&HookGetState) { g_origGetState = (GetState_t)vt[9]; vt[9] = (void*)&HookGetState; }
				DWORD o2; VirtualProtect(&vt[9], sizeof(void*), old, &o2);
			}
			((Release_t)vt[2])(dev);
		}
		((Release_t)dvt[2])(di);
		std::cout << "[UI] DirectInput-Hook (Tastatur ruht beim Tippen): " << (g_origGetState ? "ok" : "FEHLT") << std::endl;
	}
	static bool HookWindow() {
		if (g_oldProc) return true;
		HWND h = GetActiveWindow(); if (!h) h = GetFocus(); if (!h) h = GetForegroundWindow();
		DWORD pid = 0; if (h) GetWindowThreadProcessId(h, &pid);
		if (!h || pid != GetCurrentProcessId()) return false;
		g_hwnd = h;
		g_oldProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&InputProc);
		std::cout << "[UI] Tastatur-Hook fuer das Eingabefenster: " << (g_oldProc ? "ok" : "FEHLT") << std::endl;
		return g_oldProc != nullptr;
	}
	// Anzeige im Stil des Spiels: Feld = m_blackWidget-Instanz, Titelzeile + Eingabefeld = Klon des Weltnamen-Felds
	// (wie bisher, NUR Anzeige). Der getippte Text kommt aus InputProc und wird per setText in die Textform "edit" des
	// Klons geschrieben (dasselbe setText 0x636A00 wie die Zeilen der Runensteinliste). Fehlen die Vorlagen, zeichnet
	// DrawInput einen Ersatzkasten.
	static bool g_tNative = false, g_tFocus = false; static wchar_t g_tShown[64] = { 0 };
	static void* EditWidget() { void* ew = nullptr; __try { ew = g_tEdit ? *(void**)(g_tEdit + 0x40) : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) {} return ew; }
	static void ShowTyped() {
		if (!g_tNative) return;
		wchar_t w[64]; const bool caret = (GetTickCount() / 500) % 2 == 0;
		swprintf_s(w, L"%s%s", g_tBuf, caret ? L"_" : L" ");
		if (wcscmp(w, g_tShown) == 0) return;
		wcscpy_s(g_tShown, w);
		SetTextRaw(g_tTitle, L"edit", w);
	}
	bool AskText(cube::GameController* gc, const std::string& title, const std::string& hint) {
		uint8_t* G = (uint8_t*)gc; if (!G || g_typing) return false;
		if (!HookWindow()) return false;
		HookDInput();
		if (!g_tMade) {
			g_tMade = true;
			MakeTextRaw(G);
			if (g_tHint) { uint8_t* e = FindChildRaw(g_tHint, L"edit", 0); if (e) VisRaw(e, 0); }
			if (g_tTitle) g_tEdit = FindChildRaw(g_tTitle, L"edit", 0);
			std::cout << "[UI] Eingabefenster aus Spielvorlagen: " << (g_tPanel && g_tTitle && g_tEdit ? "ok" : "FEHLT (Ersatzkasten)") << std::endl;
		}
		Wide(title, g_tTitleW, 128); Wide(hint, g_tHintW, 160);
		g_tLen = 0; g_tBuf[0] = 0; g_tShown[0] = 0; InterlockedExchange(&g_tDone, 0);
		g_tNative = false;
		float W, H;
		if (g_tPanel && g_tTitle && g_tEdit && ScreenRaw(G, &W, &H)) {
			const float x0 = (W - kTW) * 0.5f, y0 = (H - kTH) * 0.5f;
			PanelPlaceRaw(g_tPanel, kTW, kTH, x0, y0);
			NodePosRaw(g_tTitle, W * 0.5f, y0 + 44.0f);
			NodePosRaw(g_tHint, W * 0.5f, y0 + kTH - 28.0f);
			SetTextRaw(g_tTitle, L"worldName", g_tTitleW);
			SetTextRaw(g_tHint, L"worldName", g_tHintW);
			VisRaw(g_tPanel, 1); VisRaw(g_tTitle, 1); VisRaw(g_tHint, 1);
			g_tNative = true;
			ShowTyped();
		}
		if (!g_tNative) return false;                                          // ohne Spielvorlagen: Skills nimmt das Windows-Fenster
		// Laufen: DirectInput-Hook leert die Tastatur. Nur wenn der fehlt, sperrt das Fokus-Feld moveCharacter.
		g_tFocus = false;
		if (!g_origGetState) { void* ew = EditWidget(); if (ew) { SetFocusRaw(G, ew); g_tFocus = true; } }
		ChatLockRaw(G, false);
		InterlockedExchange(&g_typing, 1);
		return true;
	}
	static void CloseText(uint8_t* G) {
		void* ew = EditWidget();
		if (ew && FocusRaw(G) == ew) SetFocusRaw(G, nullptr);
		if (g_tNative) { SetTextRaw(g_tTitle, L"edit", L""); VisRaw(g_tPanel, 0); VisRaw(g_tTitle, 0); VisRaw(g_tHint, 0); }
		g_tNative = false; g_tFocus = false;
		InterlockedExchange(&g_typing, 0);
	}
	int PollText(cube::GameController* gc, std::string& out) {
		uint8_t* G = (uint8_t*)gc; if (!G || !g_typing) return -2;
		const LONG d = g_tDone;
		if (d == -1) { CloseText(G); return -1; }
		if (d == 1) {
			char b[192] = ""; WideCharToMultiByte(CP_UTF8, 0, g_tBuf, -1, b, sizeof(b), nullptr, nullptr);
			out = b;
			while (!out.empty() && out.back() == ' ') out.pop_back();
			while (!out.empty() && out[0] == ' ') out.erase(0, 1);
			CloseText(G); return 1;
		}
		ShowTyped();
		if (g_tFocus) { void* ew = EditWidget(); if (ew && FocusRaw(G) != ew) SetFocusRaw(G, ew); }
		return 0;
	}

	// ---- Knoten-Hilfen fuer andere Bausteine (2026-10-29) ----------------------------------------------------------
	void* NodeClone(cube::GameController* gc, void* tmpl) {
		uint8_t* G = (uint8_t*)gc; if (!G || !tmpl) return nullptr;
		__try { void* root = *(void**)(G + G_ROOT); return root ? ((Clone_t)Re(0x6326D0))(tmpl, nullptr, root) : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	void* NodeChild(void* node, const wchar_t* name) { return node ? FindChildRaw((uint8_t*)node, name, 0) : nullptr; }
	void  NodeText(void* node, const wchar_t* name, const wchar_t* text) { if (node) SetTextRaw((uint8_t*)node, name, text); }
	void  NodeVisible(void* node, bool on) { VisRaw((uint8_t*)node, on ? 1 : 0); }
	bool  NodeSetPos(void* node, float x, float y) { return node && NodePosRaw((uint8_t*)node, x, y); }
	bool  NodeGetPos(void* node, float* x, float* y) {
		__try {
			uint8_t* t = *(uint8_t**)((uint8_t*)node + 0x38); if (!t) return false;
			int i = *(int*)(t + 0x68); float* a = *(float**)(t + 0x94); if (!a || i < 0 || i > 4096) return false;
			*x = a[i * 2]; *y = a[i * 2 + 1]; return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// ---- HUD-Schrift des Spiels (2026-10-29, Weg A) --------------------------------------------------------------
	// Statt D3DX: Zeilen sind Klone des Weltnamen-Felds (wie die Zeilen der Runensteinliste; Textform "worldName",
	// resource1.dat 12 px mit schwarzer Kontur, mittig gesetzt). Je Bild: HudBegin -> HudText ... -> HudEnd; nicht
	// benutzte Zeilen werden ausgeblendet. Alle Knoten haengen am Spielbildschirm (GC+0x800884) und verschwinden mit ihm
	// in den Menues. Breite fuer links/rechts: geschaetzt 7 px je Zeichen.
	static const int kHudMax = 64; static const float kHudCharW = 7.0f, kHudLineH = 18.0f;
	static uint8_t* g_hud[kHudMax] = {}; static wchar_t g_hudTxt[kHudMax][96] = {}; static float g_hudPos[kHudMax][2] = {};
	static bool g_hudVis[kHudMax] = {}; static int g_hudUsed = 0, g_hudMade = 0;
	void HudBegin(cube::GameController*) { g_hudUsed = 0; }
	static void HudLine(uint8_t* G, const wchar_t* w, float x, float y, int align) {
		if (g_hudUsed >= kHudMax) return;
		const int i = g_hudUsed++;
		if (i >= g_hudMade) {
			uint8_t* r = nullptr; __try { r = MakeRowRaw(G); } __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
			if (!r) { g_hudUsed--; return; }
			uint8_t* e = FindChildRaw(r, L"edit", 0); if (e) VisRaw(e, 0);
			g_hud[i] = r; g_hudMade = i + 1; g_hudTxt[i][0] = 0; g_hudVis[i] = false;
		}
		const float wpx = (float)wcslen(w) * kHudCharW;
		const float cx = align == 0 ? x + wpx * 0.5f : align == 2 ? x - wpx * 0.5f : x;
		if (wcscmp(g_hudTxt[i], w) != 0) { wcsncpy_s(g_hudTxt[i], w, _TRUNCATE); SetTextRaw(g_hud[i], L"worldName", g_hudTxt[i]); }
		if (g_hudPos[i][0] != cx || g_hudPos[i][1] != y) { g_hudPos[i][0] = cx; g_hudPos[i][1] = y; NodePosRaw(g_hud[i], cx, y); }
		if (!g_hudVis[i]) { VisRaw(g_hud[i], 1); g_hudVis[i] = true; }
	}
	void HudText(cube::GameController* gc, const std::string& text, float x, float y, int align) {
		uint8_t* G = (uint8_t*)gc; if (!G || text.empty()) return;
		size_t a = 0; float yy = y;
		while (a <= text.size()) {
			size_t b = text.find('\n', a); if (b == std::string::npos) b = text.size();
			if (b > a) { wchar_t w[96]; Wide(text.substr(a, b - a), w, 96); HudLine(G, w, x, yy, align); }
			yy += kHudLineH; a = b + 1;
		}
	}
	void HudEnd(cube::GameController*) {
		for (int i = g_hudUsed; i < g_hudMade; i++) if (g_hudVis[i]) { VisRaw(g_hud[i], 0); g_hudVis[i] = false; }
	}
	void HudHideAll() { g_hudUsed = 0; HudEnd(nullptr); }
	int HudLines(const std::string& text) { int n = 0; size_t a = 0; while (a <= text.size()) { size_t b = text.find('\n', a); if (b == std::string::npos) b = text.size(); if (b > a) n++; a = b + 1; } return n; }

	// ---- Auswahlliste ---------------------------------------------------------------------------------------------
	static const int kRows = 12;
	static uint8_t* g_lPanel = nullptr; static uint8_t* g_lTitle = nullptr; static uint8_t* g_lHint = nullptr; static uint8_t* g_lRow[kRows] = {};
	static bool g_lMade = false, g_lShown = false;

	static void MakeListRaw(uint8_t* G) {
		__try {
			g_lPanel = MakePanelRaw(G); g_lTitle = MakeRowRaw(G); g_lHint = MakeRowRaw(G);
			for (int i = 0; i < kRows; i++) g_lRow[i] = MakeRowRaw(G);
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_lPanel = nullptr; }
	}
	void MakeTextRaw(uint8_t* G) {
		__try {
			g_tPanel = MakePanelRaw(G);
			g_tTitle = MakeRowRaw(G);                                            // Beschriftung + Eingabefeld
			g_tHint = MakeRowRaw(G);                                             // Hinweiszeile (ohne Feld)
		} __except (EXCEPTION_EXECUTE_HANDLER) { g_tPanel = g_tTitle = g_tHint = nullptr; }
	}
	bool ShowList(cube::GameController* gc, const std::string& title, const std::vector<std::string>& rows, int sel, const std::string& hint) {
		uint8_t* G = (uint8_t*)gc; if (!G) return false;
		if (!g_lMade) {
			g_lMade = true;
			MakeListRaw(G);
			uint8_t* all[kRows + 2] = { g_lTitle, g_lHint };
			for (int i = 0; i < kRows; i++) all[i + 2] = g_lRow[i];
			for (uint8_t* n : all) if (n) { uint8_t* e = FindChildRaw(n, L"edit", 0); if (e) VisRaw(e, 0); }
			std::cout << "[UI] Auswahlliste aus Spielvorlagen: " << (g_lPanel && g_lTitle ? "ok" : "FEHLT") << std::endl;
		}
		if (!g_lPanel || !g_lTitle || !g_lHint) return false;
		float W, H; if (!ScreenRaw(G, &W, &H)) return false;
		int n = (int)rows.size(); if (n > kRows) n = kRows;
		int first = 0; if (sel >= kRows) first = sel - kRows + 1;
		// 2026-10-27 (2): groesser -- Zeilen 34 px, Feld mind. 560 px breit (breiter bei langen Namen, ~9 px je Zeichen
		// der 12-px-Schrift + Rand), oben Platz fuer den Titel, unten fuer den Hinweis.
		size_t longest = hint.size(); for (const std::string& r : rows) longest = (std::max)(longest, r.size() + 8);
		const float rowH = 34.0f, pw = (std::min)(W - 40.0f, (std::max)(560.0f, 9.0f * (float)longest + 80.0f));
		const float ph = 110.0f + rowH * (float)n;
		const float x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f;
		PanelPlaceRaw(g_lPanel, pw, ph, x0, y0);
		wchar_t w[160];
		Wide(title, w, 160); SetTextRaw(g_lTitle, L"worldName", w); NodePosRaw(g_lTitle, W * 0.5f, y0 + 34.0f);
		for (int i = 0; i < kRows; i++) {
			if (!g_lRow[i]) continue;
			if (i >= n) { VisRaw(g_lRow[i], 0); continue; }
			int k = first + i; if (k >= (int)rows.size()) { VisRaw(g_lRow[i], 0); continue; }
			std::string s = (k == sel ? "> " : "") + std::to_string(k + 1) + ". " + rows[k] + (k == sel ? " <" : "");
			Wide(s, w, 160); SetTextRaw(g_lRow[i], L"worldName", w);
			NodePosRaw(g_lRow[i], W * 0.5f, y0 + 76.0f + rowH * (float)i);
			VisRaw(g_lRow[i], 1);
		}
		Wide(hint, w, 160); SetTextRaw(g_lHint, L"worldName", w); NodePosRaw(g_lHint, W * 0.5f, y0 + ph - 26.0f);
		VisRaw(g_lPanel, 1); VisRaw(g_lTitle, 1); VisRaw(g_lHint, 1);
		g_lShown = true;
		return true;
	}
	void HideList(cube::GameController* gc) {
		(void)gc;
		if (!g_lShown) return;
		VisRaw(g_lPanel, 0); VisRaw(g_lTitle, 0); VisRaw(g_lHint, 0);
		for (int i = 0; i < kRows; i++) VisRaw(g_lRow[i], 0);
		g_lShown = false;
	}
}
