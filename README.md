<h1 align="center">FluffyTail</h1>

<p align="center">
Fluffy Mithra tails with colours matched to each face.<br>
Supports Ashita 4 and Windower 4.
</p>

## About

FluffyTail combines a native x86 plugin with a XiPivot overlay. Fixed NPC
colours are stored in the DATs. Player colours are selected while rendering.

## Ashita

Requires [Ashita 4](https://github.com/AshitaXI/Ashita-v4beta) and its XiPivot
`pivot` polplugin.

1. Extract `fluffytail.zip` into the Ashita directory.
2. Add `FluffyTail` to `[overlays]` in `Ashita/config/pivot/pivot.ini` and enable
   `pivot` in the active boot profile.
3. Run `/load fluffytail`.

## Windower

Requires [Windower 4](https://windower.net/) and the
[XIPivot](https://github.com/HealsCodes/XIPivot) addon.

1. Extract `fluffytail-windower.zip` into the Windower directory.
2. Add `FluffyTail` to XIPivot's overlay list and load XIPivot.
3. Run `//load fluffytail`.

Packages are available from [Releases](https://github.com/Aeshur/FluffyTail/releases).

## Build

Requires CMake, Ninja, x86 MSVC, the Ashita 4 SDK, and the private DAT rig
assets.

```batch
set "ASHITA4_SDK_PATH=C:\Games\YourAshita\plugins\sdk"
cmake --preset x86-release-win
cmake --build --preset x86-release-win
python tools/build_runtime_overlay.py
python tools/build_packages.py bin/fluffytail.dll work/runtime-overlay work/packages
```

## License

FluffyTail contains GPLv3 and LGPLv3 components. See [LICENSES.md](LICENSES.md),
[LICENSE.GPL.txt](LICENSE.GPL.txt), and [LICENSE.md](LICENSE.md). Complete source
for each release is available from the matching tag in this repository.

Final Fantasy XI and its assets are property of Square Enix. FluffyTail is an
unofficial fan project and is not endorsed by Square Enix.
