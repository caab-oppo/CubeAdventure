#pragma once

// Minimal-Stub (nicht Teil des Original-cwsdk-Downloads -- die echten
// plasma::Node-Feldlayouts wurden dort nie oeffentlich aufgeloest). Wir
// brauchen nur den TYP, weil GameController mehrere "plasma::Node*"-Zeiger
// als Member hat (UI-Baum-Knoten) -- wir dereferenzieren sie nirgends,
// daher reicht eine Vorwaertsdeklaration ohne echte Feldliste.
namespace plasma {
	struct Node;
}
