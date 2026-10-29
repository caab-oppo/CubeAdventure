# MinHook (nicht im Repository)

Der Mod bindet **MinHook** (Hooks für x86, BSD-2-Lizenz) beim Bauen fest in die DLL ein. Die Bibliothek liegt nicht im Repository, du lädst sie selbst herunter:

1. Auf **https://github.com/TsudaKageyu/minhook/releases** das Paket mit den fertigen Bibliotheken holen (`MinHook_<Version>_lib.zip`, gebaut mit Version 1.3.4).
2. Den Inhalt so in diesen Ordner legen:

```
MinHook_134_lib\
    include\MinHook.h
    lib\libMinHook.x86.lib
```

Eine andere Version oder einen anderen Ort trägst du in `AdventureMod\AdventureMod.vcxproj` (Eigenschaften `MinHookIncludeDir` / `MinHookLibDir`) oder als Benutzermakro in Visual Studio ein.

Projektseite und Lizenz: https://github.com/TsudaKageyu/minhook
