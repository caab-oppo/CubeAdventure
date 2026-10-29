#pragma once
//
// ModSwitches.h -- einfache Ein/Aus-Schalter fuer die Fehlersuche, gespeichert in
// AdventureMod_schalter.cfg (Arbeitsordner von Cube.exe), Zeilen "name=0/1".
// Jeder Thread darf lesen und schreiben.
//
#include <string>
namespace ModSwitches {
	bool Get(const char* name, bool def);
	void Set(const char* name, bool on);       // sofort gespeichert
}
