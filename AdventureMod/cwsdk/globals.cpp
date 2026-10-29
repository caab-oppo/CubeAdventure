#include <Windows.h>
#include "../Host.h"
#include "globals.h"
#include "cube_funcs.h"

namespace cube {
	float* gravity = nullptr;
	HWND* hwnd = nullptr;
	bool* input_enabled = nullptr;
	void InitGlobals()
	{
		DWORD trash_protections;
		auto base = cube_funcs::instance()->ImageBase;

		gravity = (float*)(Host::ReOff(0x2FCD78));
		VirtualProtect((void*)gravity, 4, PAGE_READWRITE, &trash_protections);

		hwnd = (HWND*)(Host::ReOff(0x36B1C0));
		input_enabled = (bool*)(Host::ReOff(0x366218));
	}
};
