# FastTravelPlus - Fast Travel from the Map

A mod for CONTROL Resonant: open the fast travel menu from the map or with a hotkey, wherever you are, instead of walking to a fast travel door.

Download and install instructions are on Nexus Mods (search for FastTravelPlus in the CONTROL Resonant section). This repository is the full source.

## Features

- A Fast Travel entry in the map screen's action bar.
- Keyboard and controller hotkeys, set on the MODS page by pressing the key or button (Xbox and PlayStation controllers, with or without Steam Input).
- The game's own fast travel menu, destinations and trip.
- Mission check: in a mission it asks first, then abandons the mission the game's own way before you travel.

## Requirements (to play)

- [f2g DLL Mod Loader (crloader)](https://www.nexusmods.com/controlresonant/mods/9)
- [Mod Settings Menu](https://www.nexusmods.com/controlresonant/mods/35) 1.7.1 or later

## Building

Requires Windows and the Visual Studio 2022 Build Tools (C++ workload). Run `build.bat`; it builds `build\fasttravelplus.dll`. To install your build, put it in `crmods\FastTravelPlus` in the game folder together with the files in `mod\`.

## How it works

`fasttravelplus.dll` is loaded by [f2g DLL Mod Loader (crloader)](https://www.nexusmods.com/controlresonant/mods/9) from `crmods\FastTravelPlus`. At start-up it reads the game executable from disk, finds the game functions it needs by byte signatures, and installs a few inline hooks inside the game process only. `FastTravelPlus.js` is appended to the game's UI bundle when the game loads it, and talks to the DLL through a `coui://` endpoint. There is no network code; the mod writes only its own log and settings files in its own folder.

## Antivirus false positives

Some antivirus tools, including Windows Defender (`Trojan:Win32/Wacatac.C!ml`), may flag the DLL because it is unsigned and hooks into the game. This is a false positive and has been reported to Microsoft. The full source is here, so you can check it or build the DLL yourself with `build.bat`.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.

## Credits

- **fame2gin** for f2g DLL Mod Loader.
- **kkyleeb21** for Mod Settings Menu, and for MapFusion, which showed how to add scripts to the game's UI.

## License

MIT, see [LICENSE](LICENSE). CONTROL Resonant is a game by Remedy Entertainment; this project isn't affiliated with or endorsed by Remedy.
