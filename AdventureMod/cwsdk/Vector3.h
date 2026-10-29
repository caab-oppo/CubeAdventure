#pragma once
#include <cstdint>

// Quelle: Andoryuuta/cwsdk (https://github.com/Andoryuuta/cwsdk), unveraendert
// uebernommen. Vector3<int64_t> wird fuer Weltkoordinaten benutzt (siehe
// Creature::entity_data.position) -- 1 Block = 65536 Positionseinheiten,
// siehe WanderingMerchant.cpp.
template <typename T>
class Vector3
{
public:
	T X = 0;
	T Y = 0;
	T Z = 0;

	Vector3() {};
	Vector3(T x, T y, T z) : X(x), Y(y), Z(z) {};
	~Vector3() {};
};
