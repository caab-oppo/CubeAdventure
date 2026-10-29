#pragma once

// Quelle: Andoryuuta/cwsdk, unveraendert uebernommen (bis auf den
// tatsaechlich benutzten Teil: die IDirect3DDevice9_VTable-Struct ist im
// SDK selbst nur experimentell und wird von uns NICHT genutzt -- wir
// ermitteln EndScene/Reset ueber eine echte D3D9-Dummy-Device-Vtable in
// main.cpp, siehe GetD3D9Addresses()). Hier nur der <d3d9.h>-Include
// wichtig, den Sprite.h/SpriteManager.h fuer LPDIRECT3DDEVICE9 brauchen.
#include <d3d9.h>
