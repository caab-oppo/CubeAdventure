#pragma once
#include <cstdint>
#include <string>

// Quelle: Andoryuuta/cwsdk. Siehe Kommentar in string.h -- gleiche
// Begruendung, warum nur das Layout (nicht die Methodenkoerper) uebernommen
// wird.
namespace MSVCBinCompat {
	class wstring
	{
		union
		{
			wchar_t* ptr;
			wchar_t sbo[8];
		} str_data;
		uint32_t size;
		uint32_t cap;

	public:
		~wstring() = default;
		wstring() = default;
	};
}
