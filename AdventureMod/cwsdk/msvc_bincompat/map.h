#pragma once
#include <cstdint>
#include <map>

// Quelle: Andoryuuta/cwsdk, unveraendert uebernommen. Bildet nur das
// Speicher-Layout von MSVC-std::map (VC++11.0/_MSC_VER=1700, Rot-Schwarz-
// Baum-Sentinel-Knoten "head" INLINE im Objekt) nach -- KEINE eigene
// Insert/Find-Logik, die laeuft im Original-Spielcode (siehe cube_util.cpp,
// Aufruf der echten Map-Insert-Funktion bei ImageBase+0x68AD0).
namespace MSVCBinCompat {
	struct RBBST_node
	{
		RBBST_node *left;
		RBBST_node *parent;
		RBBST_node *right;
		char color;
		char isnil;
		__int16 _pad;
	};

	template <typename KT, typename VT>
	struct MyMap_node
	{
		RBBST_node rbbst;
		KT key;
		VT val;
	};

	template <typename KT, typename VT>
	struct map {
		typedef MyMap_node<KT, VT> node_t;
	private:
		node_t head;
		void inorder(node_t* node, std::map<KT, VT>* output)
		{
			if (!node->rbbst.isnil) {
				inorder((node_t*)node->rbbst.left, output);
				output->insert_or_assign(node->key, node->val);
				inorder((node_t*)node->rbbst.right, output);
			}
		}
	public:
		std::map<KT, VT> CopyToSTDMap() {
			auto out = std::map<KT, VT>();
			inorder((node_t*)head.rbbst.parent, &out);
			return out;
		}

		// Iterative BST-Suche nach key (reiner Zeiger-Walk, keine STL-Objekte
		// -- daher gefahrlos innerhalb eines __try aufrufbar). Liefert VT()
		// (z.B. nullptr) wenn der Schluessel nicht existiert. Ordnung wie
		// MSVC-std::map (std::less<KT>).
		VT Find(const KT& key) {
			node_t* node = (node_t*)head.rbbst.parent; // Wurzel
			while (node != nullptr && !node->rbbst.isnil) {
				if (key < node->key)      node = (node_t*)node->rbbst.left;
				else if (node->key < key) node = (node_t*)node->rbbst.right;
				else                      return node->val;
			}
			return VT();
		}
	};
};
