#pragma once
#include <cstdint>
#include "msvc_bincompat.h"
#include "Vector3.h"
#include "cube.h"

// Singleton mit allen "kleinen Helfer"-Funktionszeigern, die World-
// Methoden intern brauchen (GetZone/SetBlock/GetBlock). Adressen 1:1 aus
// Andoryuuta/cwsdk uebernommen -- NICHT eigenstaendig fuer diese Alpha-
// Version verifiziert (anders als GameController-Zeiger/CreatureCtor/
// Map-Insert, siehe README.md "Verifikationsstatus"). Falls World::GetZone
// abstuerzt, ist world_getzone der erste Verdaechtige.
class cube_funcs
{
	typedef void(__thiscall* chatwidget_print_t)(uint32_t, MSVCBinCompat::wstring*, Color*);
	typedef void(__thiscall* vec3_int64_fom_vec3_float_t)(Vector3<int64_t>*, Vector3<float>*);
	typedef cube::Zone*(__thiscall* world_getzone_t)(cube::World*, int zone_x, int zone_y);
	typedef BlockColor*(__thiscall* world_getblock_t)(cube::World*, int x, int y, int z, cube::Zone*);
	typedef char(__thiscall* world_setblock_t)(cube::World*, int x, int y, int z, BlockColor*, cube::Zone*);
	typedef MSVCBinCompat::wstring*(__thiscall *speech_get_localization_from_dict_t)(cube::Speech*, MSVCBinCompat::wstring* out, MSVCBinCompat::wstring* key, MSVCBinCompat::wstring word_form);
	typedef MSVCBinCompat::wstring*(__stdcall *generate_rare_creature_name_t)(MSVCBinCompat::wstring*, uint32_t GUID_low, uint32_t race);

public:
	static cube_funcs* instance();
	cube_funcs(cube_funcs const&) = delete;
	void operator=(cube_funcs const&) = delete;

private:
	static cube_funcs* _instance;
	cube_funcs();

public:
	uint32_t ImageBase;
	chatwidget_print_t chatwidget_print;
	vec3_int64_fom_vec3_float_t vec3_int64_fom_vec3_float;
	world_getzone_t world_getzone;
	world_getblock_t world_getblock;
	world_setblock_t world_setblock;
	speech_get_localization_from_dict_t speech_get_localization_from_dict;
	generate_rare_creature_name_t generate_rare_creature_name;
};
