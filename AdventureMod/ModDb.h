#pragma once
//
// ModDb.h -- Speicher des Mods (Stand 2026-10-27): keine verstreuten .txt/.cfg mehr.
// ---------------------------------------------------------------
//  * Eigene Datenbank  Save\AdventureMod.db  (SQLite des Spiels, Tabelle blobs(key TEXT PRIMARY KEY, value BLOB)
//    wie die Spieldatenbanken). Schalter, Leistung, Orte, Runensteine, Tempel-Funde, Bergdoerfer ... als "Textdateien"
//    unter ihrem alten Namen (ReadText/WriteText). Jeder Thread.
//  * Charakter-Datenbank des Spiels  Save\characters.db  (offen im GameController +0x1001008, sqlite3* bei +0x100100C):
//    je Charakter eigene Schluessel "AdventureMod:<Name>:<Feld>" neben den Spielschluesseln "num", "0", "1", ...
//    (das Spiel liest nur seine Nummern). NUR Spielthread.
// Alte Dateien werden beim ersten Lesen uebernommen und in <Name>.alt umbenannt (nicht geloescht).
// Benutzt die SQLite-Funktionen in Cube.exe (3.7.15.2): open 0x525590, close 0x520620, exec 0x520760,
// prepare_v2 0x526480, bind_text 0x526B60, bind_blob 0x5267A0, step 0x527300, finalize 0x528070,
// column_blob 0x527590, column_bytes 0x527610 (Belege: Datenbankklasse 0x4497B0..0x449B6D). Server.exe ohne diese
// Adressen: Dateien wie bisher.
//
#include "cwsdk.h"
#include <string>
#include <sstream>

namespace ModDb {
	bool ReadText(const std::string& name, std::string& out);      // false = gibt es nicht
	void WriteText(const std::string& name, const std::string& content);
	void AppendText(const std::string& name, const std::string& line);   // Zeile anhaengen ('\n' wird ergaenzt)
	// Charakter-Datenbank (Spielthread). Name = Charaktername (+0x1168).
	bool CharRead(cube::GameController* gc, const std::string& field, std::string& out);
	bool CharWrite(cube::GameController* gc, const std::string& field, const std::string& content);
	std::string CharName(cube::GameController* gc);
	std::string Status();
	// Ersatz fuer std::ifstream/std::ofstream auf den alten Dateinamen
	std::string DefaultsPath(const std::string& name);         // Strukturen\\Grundeinstellungen\\<Name> (Vorlage fuer neue Installationen)
	inline std::string Txt(const std::string& name) { std::string s; ReadText(name, s); return s; }
	struct Writer : std::ostringstream {
		std::string n; bool app;
		explicit Writer(const std::string& name, bool append = false) : n(name), app(append) {}
		~Writer() { if (app) { std::string cur = Txt(n); if (!cur.empty() && cur.back() != '\n') cur += '\n'; WriteText(n, cur + str()); } else WriteText(n, str()); }
	};
}
