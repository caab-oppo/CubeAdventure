#include "Lan.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#pragma comment(lib, "ws2_32.lib")

// Herleitung: Lan.h.

namespace Lan {
	static PROCESS_INFORMATION g_pi = {};
	static volatile LONG g_seed = 0;
	void SetWorldSeed(unsigned seed) { if (seed) InterlockedExchange(&g_seed, (LONG)seed); }
	bool Running() {
		if (!g_pi.hProcess) return false;
		DWORD code = 0;
		if (GetExitCodeProcess(g_pi.hProcess, &code) && code == STILL_ACTIVE) return true;
		CloseHandle(g_pi.hProcess); CloseHandle(g_pi.hThread); g_pi = {};
		return false;
	}
	bool Start(std::string& msg) {
		if (Running()) { msg = "Server laeuft bereits."; return true; }
		char dir[MAX_PATH] = ""; GetCurrentDirectoryA(MAX_PATH, dir);
		std::string exe = std::string(dir) + "\\Server.exe";
		if (GetFileAttributesA(exe.c_str()) == INVALID_FILE_ATTRIBUTES) { msg = "Server.exe nicht im Spielordner gefunden (" + exe + ")."; return false; }
		std::string seedNote;
		if (g_seed) {                                                              // server.cfg = Seed der eigenen Welt
			std::string cfg = std::string(dir) + "\\server.cfg", bak = cfg + ".vor_mod";
			if (GetFileAttributesA(cfg.c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileA(cfg.c_str(), bak.c_str(), TRUE);
			HANDLE f = CreateFileA(cfg.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f != INVALID_HANDLE_VALUE) { std::string v = std::to_string((unsigned)g_seed) + "\n"; DWORD w; WriteFile(f, v.data(), (DWORD)v.size(), &w, nullptr); CloseHandle(f); seedNote = " Seed " + std::to_string((unsigned)g_seed) + " (wie deine Welt)."; }
		} else seedNote = " (Seed der Welt unbekannt -- erst eine Welt laden.)";
		STARTUPINFOA si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWMINNOACTIVE;
		std::string cmd = "\"" + exe + "\"";
		if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir, &si, &g_pi)) { msg = "Server.exe liess sich nicht starten (Fehler " + std::to_string(GetLastError()) + ")."; g_pi = {}; return false; }
		msg = "Server gestartet." + seedNote + " Im Spiel: Hauptmenue -> Multiplayer worlds... -> Connect to server -> 127.0.0.1 (du) bzw. deine LAN-Adresse (Mitspieler).";
		std::cout << "[LAN] " << msg << std::endl;
		return true;
	}
	void Stop() {
		if (!Running()) return;
		TerminateProcess(g_pi.hProcess, 0);
		CloseHandle(g_pi.hProcess); CloseHandle(g_pi.hThread); g_pi = {};
		std::cout << "[LAN] Server beendet." << std::endl;
	}
	std::vector<std::string> LocalAddresses() {
		std::vector<std::string> out;
		WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return out;
		char host[256] = "";
		if (gethostname(host, sizeof(host)) == 0) {
			addrinfo hints = {}; hints.ai_family = AF_INET; addrinfo* res = nullptr;
			if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
				for (addrinfo* p = res; p; p = p->ai_next) {
					char b[64] = ""; sockaddr_in* a = (sockaddr_in*)p->ai_addr;
					inet_ntop(AF_INET, &a->sin_addr, b, sizeof(b));
					if (strncmp(b, "127.", 4) != 0) out.push_back(b);
				}
				freeaddrinfo(res);
			}
		}
		WSACleanup();
		return out;
	}
	std::string Status() {
		std::string r = std::string("LAN-Server: ") + (Running() ? "laeuft (von hier gestartet)" : "aus") + "\r\nDeine Adressen im lokalen Netz:";
		for (const std::string& a : LocalAddresses()) r += " " + a;
		r += "\r\n\r\nSo spielt ihr zusammen:\r\n 1. 'Server starten'.\r\n 2. Du: Hauptmenue -> Multiplayer worlds... -> Connect to server -> 127.0.0.1\r\n"
		     " 3. Mitspieler (gleiches Netz, gleicher Mod + Strukturen-Ordner): dieselbe Schaltflaeche mit deiner Adresse oben.\r\n"
		     " 4. Windows-Firewall: Server.exe beim ersten Start erlauben (privates Netzwerk).\r\n\r\n"
		     "Der Server bekommt den Seed deiner Welt (server.cfg) -> gleiche Landschaft. Jeder Spieler bringt seinen eigenen "
		     "Charakter mit (Inventar, Rezepte, Level liegen beim Spieler in Save\\characters.db -- so ist Cube World gebaut). "
		     "Biome/Staedte/Tempel des Mods erzeugt jeder Rechner selbst aus Seed + Strukturen-Ordner. "
		     "Eigene Mod-Kreaturen (Haendler, Tiere) erscheinen nur bei dem Spieler, dessen Mod sie gesetzt hat.";
		return r;
	}
}
