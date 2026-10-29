#pragma once
//
// NativeUi.h -- Fenster im Stil des Spiels aus den eigenen plasma-Vorlagen (2026-10-27)
// ---------------------------------------------------------------
// Genau wie GameController-ctor/resize (Infinite-Cubes alpha/src/cube/GameController.cpp, Adressen am Original geprueft):
//   Feld   = m_blackWidget (GC+0x8008C8)->instance(m_gameScreen GC+0x800884) 0x636040, eigene Transformation/Anzeige
//            (transformation() 0x411C70 / display() 0x40F8E0, je vtbl[2] = clone; setTransformation 0x636B70,
//            setDisplay 0x6368E0), widget() 0x411CF0 -> resizeTo 0x62C570, moveTo 0x62A650, viewWidth 0x62F600,
//            getViewHeight 0x6291D0, setVisible 0x411A90  (Ablauf 1:1 wie das Systemmenue 0x464A00..0x464A81, 0x482F92..)
//   Zeile  = Klon des Weltnamen-Felds m_worldNameNode (GC+0x8009BC; Beschriftung 12 px mit Kontur + Eingabefeld "edit")
//            clone 0x6326D0; Text setText(name, text, rekursiv) 0x636A00 / getText 0x635550 (wie 0x45D652 / 0x4826B9);
//            Lage: Transformation +0x68 Index, +0x94 Feld (x,y), danach vtbl[1](1) (wie resize 0x4831D0..0x4831FA).
//   Eingabe: Engine = GC+0x800710, Fokus-Widget Engine+0xCC (characterEntered 0x652710 / keyPressed 0x652730 geben
//            Zeichen und Tasten an das fokussierte Widget; plasma::Edit fuegt ein, Ruecktaste, Pfeile; Enter loescht
//            den Fokus). Solange ein Widget den Fokus hat, ignoriert GameController::handleKeyDown alle Spieltasten und
//            onKeyTyped (0x482A00) schreibt nichts in den Chat. Laufen sperrt das Spiel nur bei offener Chatzeile
//            (ChatWidget GC+0x800A14, +0x180 aktiv) -- die setzen wir fuer die Dauer der Eingabe mit.
//
#include "cwsdk.h"
#include <string>
#include <vector>


namespace NativeUi {
	// ---- Eingabefenster (Benennen) --------------------------------------------------------- nur Spielthread
	bool AskText(cube::GameController* gc, const std::string& title, const std::string& hint);   // false = Vorlagen fehlen
	// 0 = offen, 1 = bestaetigt (out = Text, evtl. leer), -1 = abgebrochen, -2 = nicht offen
	int  PollText(cube::GameController* gc, std::string& out);
	bool Typing();                                  // jeder Thread: Eingabe offen -> Mod-Tasten ruhen
	// Spieltasten erlaubt? Nur im laufenden Spiel: kein Menue-Bildschirm (Welt/Charakter erstellen ...), kein Fokus-Widget,
	// Chatzeile zu, Spielfenster vorn, kein LAB, Spielerzone geladen. PlayInput: Spielthread (rechnet + merkt sich),
	// PlayInputCached: letzter Wert (jeder Thread; aelter als 0,5 s = nein).
	bool PlayInput(cube::GameController* gc);
	bool PlayInputCached();

	// ---- Knoten des Spiels (plasma::Node) ------------------------------------------------------- nur Spielthread
	void* NodeClone(cube::GameController* gc, void* tmpl);              // Klon (0x6326D0) unter dem Spielbildschirm
	void* NodeChild(void* node, const wchar_t* name);                   // Kind mit Namen (rekursiv)
	void  NodeText(void* node, const wchar_t* name, const wchar_t* text);   // setText(name, text, rekursiv) 0x636A00
	void  NodeVisible(void* node, bool on);                             // 0x411A90
	bool  NodeSetPos(void* node, float x, float y);                     // Verschiebung der Transformation
	bool  NodeGetPos(void* node, float* x, float* y);

	// ---- HUD-Schrift des Spiels (statt D3DX) ------------------------------------------------------ nur Spielthread
	// Je Bild: HudBegin, beliebig viele HudText (mehrzeilig mit '\n', align 0 links / 1 mittig / 2 rechts ab x; y = erste
	// Zeile, 18 px Zeilenabstand), HudEnd blendet ungenutzte Zeilen aus. HudHideAll ausserhalb des Spiels.
	void HudBegin(cube::GameController* gc);
	void HudText(cube::GameController* gc, const std::string& text, float x, float y, int align);
	void HudEnd(cube::GameController* gc);
	void HudHideAll();
	int  HudLines(const std::string& text);

	// ---- Auswahlliste (Runensteine) -------------------------------------------------------- nur Spielthread
	bool ShowList(cube::GameController* gc, const std::string& title, const std::vector<std::string>& rows, int sel, const std::string& hint);
	void HideList(cube::GameController* gc);
}
