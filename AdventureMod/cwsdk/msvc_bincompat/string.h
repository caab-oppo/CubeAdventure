#pragma once
#include <cstdint>
#include <string>

// Quelle: Andoryuuta/cwsdk. NUR das Layout wird von uns gebraucht (Felder
// liegen als Struct-Member in anderen Structs, z.B. GameController::
// command_line_arg_1). Die Methodenkoerper (string.cpp im Original-SDK)
// binden wir bewusst NICHT ein -- unser eigener Mod-Code erzeugt/kopiert nie
// selbst ein MSVCBinCompat::string-Objekt "by value", sondern liest solche
// Felder hoechstens indirekt ueber Zeiger-Arithmetik. Sollte der Linker
// trotzdem einen dieser Methodenkoerper anmahnen, ist das ein Zeichen, dass
// irgendwo versehentlich eine echte string-Operation aufgerufen wird -- in
// dem Fall die fehlenden Methoden aus dem Original-SDK nachtragen.
namespace MSVCBinCompat{
	class string
	{
		union
		{
			char* ptr;
			char sbo[16];
		} str_data;
		uint32_t size;
		uint32_t cap;

	public:
		~string() = default;
		string() = default;
	};
};
