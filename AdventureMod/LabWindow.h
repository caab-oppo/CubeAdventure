#pragma once
//
// LabWindow.h
// ---------------------------------------------------------------
// Das LAB als eigenes Windows-Fenster neben dem Spiel.
//
//   * Laeuft auf einem EIGENEN Thread mit eigener Nachrichtenschleife --
//     das Spiel ruckelt nicht, wenn das Fenster rechnet, und das Fenster
//     friert nicht ein, wenn das Spiel laedt.
//   * Fasst das Spiel nie direkt an: jede Aktion geht als Auftrag ueber
//     LabBridge::Post an den Spielthread (siehe LabBridge.h).
//   * Enthaelt alle bisherigen LAB-Funktionen (die STRG-Tasten aus dem
//     [L]-Overlay) als Knoepfe, dazu Strukturbibliothek, Editor und
//     3D-Vorschau.
//
// Bedienung: [F10] im Spiel blendet das Fenster ein/aus. Das [L]-Overlay
// im Spiel bleibt unveraendert erhalten.
//
namespace LabWindow {
	void Start();          // einmal beim Mod-Start; legt Thread + Fenster an
	void Toggle();         // ein-/ausblenden (threadsicher, per PostMessage)
	bool IsVisible();
	// true, solange das LAB (oder einer seiner Dialoge) den Tastaturfokus hat.
	// main.cpp ignoriert dann alle Spiel-Hotkeys (GetAsyncKeyState ist global).
	bool IsForeground();
}
