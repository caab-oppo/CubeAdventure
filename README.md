# Cube Adventure – Mod for Cube World Alpha
This mod was createt with Ai.
Cube Adventure expands **Cube World Alpha** (2013, 32-bit) with custom biomes, elf and dwarf cities with districts, a High Elf King, small teleport temples, new animals, skills (teleport via rune stone, summoning), merchants, recipes, and an in-game editor window (the “LAB,” F10 key).

> Players can download the finished package from **[Releases](../../releases)**. This repository contains the source code and assets.

---

## Playing (Release)

1. Download the file `CubeAdventure_<date>.zip` from **Releases**.
2. Copy the contents to the Cube World Alpha folder, where `Cube.exe` is located:
   ```
   Mods\AdventureMod.dll
   Structures\
   README.txt
   ```
3. Launch the game using a **mod launcher** that loads DLLs from the `Mods` folder (e.g., CubeModLauncher). `Cube.exe` alone does not load mods.

**Requirements:** None. No Visual C++ Redistributable or DirectX Runtime required.

**Note on 4 GB:** With the default setting `vier_gb=1`, the mod sets the 4-GB bit (Large Address Aware) in your `Cube.exe` after the first exit. Before doing so, it creates a backup file named `Cube.exe.vor-4GB.bak`. To disable: LAB → “Network & Animals”.

### Keys

| Key | Function |
|---|---|
| F9 | Toggle coordinates / biome / locations |
| U | Place a rune stone (with name); next to a stone: pick it up |
| P | Teleport to the rune stone (if multiple: list; 6 = next) |
| 5 | Summon (the last defeated enemies will fight for you briefly) |
| X | Skill window: Teleport and Summon are in the native tree |
| R | Touch the temple orb |
| RMT | Touch the temple orb |

Teleport is unlocked at the **High Elf King** in the main tree of the Elven City. Once you’ve found **25 temples**, each Temple Orb will take you directly to him.

---

## Build it yourself

**Requirements**
- Windows, **Visual Studio 2022** with “Desktop Development with C++” (Toolset v143, Windows SDK)
- **MinHook**, not included in the repository. See [`MinHook_134_lib/README.md`](MinHook_134_lib/README.md), download at https://github.com/TsudaKageyu/minhook/releases
- Cube World Alpha for testing

**Folder Structure:** The project expects this repository to be located **as a subfolder within the game folder**. The build copies the DLL to `..\Mods\`:
```
<Cube World Alpha>\
    Cube.exe
    Mods\                      <- the build copies AdventureMod.dll here
    Structures\                <- copy the contents of this repository (Structures folder) here
    CubeAdventure\             <- this repository
        AdventureMod.sln
        AdventureMod\          Source code (C++)
        MinHook_134_lib\       MinHook (download yourself)
        ...
```

**Building**
1. Place MinHook in `MinHook_134_lib\`.
2. Open `AdventureMod.sln`, select **Release | Win32** at the top, and build (Ctrl+Shift+B).
3. Copy the `Structures` folder from the repository to the game folder.
4. Play using the Mod Starter.

**Create a release package:** Run `Veroeffentlichen.bat`. The script takes `Mods\AdventureMod.dll` and `Structures\` from the game folder, omits working and world files, checks the DLL (no debug build, no personal paths, no d3dx9) and the texts (no email addresses, keys, or user paths), and creates `Release\CubeAdventure_<Date>.zip`.

---

## Repository Structure

| Folder | Contents |
|---|---|
| `AdventureMod/` | DLL source code (C++17, 32-bit). Entry point `main.cpp`, overview in `Docs/MODULE_OVERVIEW.md` |
| `Structures/` | All content: biomes, city, mobs (including parts and wildlife), skills, recipes, merchants, items, dungeons, zone models, texts, `BasicSettings/` |
| `Docs/` | Guides (LAB, biomes, server), `README.txt` for the release, licenses |
| `RE/` | Notes on the analysis of `Cube.exe` (addresses, processes) and Ghidra scripts |
| `Tools/` | Python scripts that generate content (animals, legs and joints, dungeons, city models). Not required for playing or building |
| `MinHook_134_lib/` | Space for MinHook (not included) |

---

## License

Original code and content: **MIT**, see [`LICENSE`](LICENSE). Third-party components: see [`THIRD_PARTY.md`](THIRD_PARTY.md).

Cube World is a game by Picroma. This project does not contain any game files and is not an official product.
