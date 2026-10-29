#pragma once
//
// cwsdk.h -- eigener, schlanker Aggregator-Header fuer unser Projekt.
//
// Der Original-cwsdk-Aggregator (Andoryuuta/cwsdk/cwsdk.h) inkludiert
// zusaetzlich "memory_management.h" und "plasma.h", deren Inhalte im
// oeffentlichen Repo nicht (mehr) auflösbar waren. Wir brauchen sie nicht:
//   - memory_management.h: vermutlich eigene operator-new/delete-Ueberladungen
//     fuer Kompatibilitaet -- unser Code nutzt bereits Standard-::operator new,
//     was in den Live-Tests (VillageDungeonTraderMod) nachweislich funktioniert.
//   - plasma.h: volles UI-Node-System -- wir brauchen nur den Zeiger-TYP
//     plasma::Node (siehe cwsdk/plasma/Node.h, eigener Minimal-Stub), keine
//     Node-Methoden.
//
#include "cube.h"
#include "cube_util.h"
#include "globals.h"
