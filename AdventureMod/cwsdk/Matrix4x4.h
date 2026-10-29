#pragma once

#include "Vector3.h"
#include <math.h>

// Quelle: Andoryuuta/cwsdk, unveraendert uebernommen. Wird nur als
// Speicher-Platzhalter fuer GameController/WorldMap-Felder gebraucht, die
// wir selbst nicht auswerten -- Layout (16 floats) muss trotzdem stimmen,
// damit nachfolgende Felder an der richtigen Adresse landen.
struct Matrix4x4 {
	union {
		struct
		{
			float        _11, _12, _13, _14;
			float        _21, _22, _23, _24;
			float        _31, _32, _33, _34;
			float        _41, _42, _43, _44;
		};
		float m[4][4];
	};
};
