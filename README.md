<h1 align="center">FluffyTail</h1>

<p align="center">
Fluffy tails calibrated to each Mithra face and hair variant.<br>
An Ashita v4.3 plugin and XiPivot overlay for retail Final Fantasy XI.
</p>

<p align="center">
<a href="LICENSE.GPL.txt"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3"></a>
<a href="LICENSE.md"><img src="https://img.shields.io/badge/License-LGPLv3-blue.svg" alt="License: LGPLv3"></a>
<a href="https://github.com/Aeshur/FluffyTail/actions/workflows/quality.yml"><img src="https://github.com/Aeshur/FluffyTail/actions/workflows/quality.yml/badge.svg" alt="Checks"></a>
</p>

## About

FluffyTail combines an Ashita v4.3 plugin with the bundled XiPivot `pivot`
polplugin. It matches each normal Mithra tail to the character's face and hair
variant. Supported NPCs with fixed models receive colours baked into their DATs.

## Install

Requirements:

- [Ashita v4.3](https://github.com/AshitaXI/Ashita-v4beta)
- The bundled `pivot` polplugin enabled in the active Ashita boot profile

1. Download the
   [latest release](https://github.com/Aeshur/FluffyTail/releases/latest/download/fluffytail.zip)
   and extract it into the Ashita directory. Merge the included `plugins/` and
   `polplugins/` folders with the existing folders.
2. Open the active profile in `Ashita/config/boot/` and ensure it contains:

   ```ini
   [ashita.polplugins]
   pivot = 1
   ```

3. Open `Ashita/config/pivot/pivot.ini` and add `FluffyTail` under `[overlays]`
   using the next unused number. For example:

   ```ini
   [overlays]
   0=another-overlay
   1=FluffyTail
   ```

4. Start FFXI and run `/load fluffytail`.

To load the plugin automatically, add `/load fluffytail` to the script used by the
active Ashita boot profile.

When updating an existing installation, exit FFXI before replacing the DLL or DATs.

## Commands

`/fluffytail inspect` prints the current target's entity type, race, actor pointer,
and armor model IDs. It requires a current target and is intended for diagnosing
unsupported Mithra with fixed models.

## Troubleshooting

Keep the XiPivot overlay enabled while the plugin is loaded. With the plugin off,
normal Mithra tails use the neutral fallback colour while supported NPCs with fixed
models retain colours baked to match their hair.

## Known limitations

- Costume bodies that hide the original tail remain unchanged.
- Nanaa Mihgo's special fixed model uses an unsupported tail-body rig and is not
  modified.

## Build

The build requires Ninja, CMake 3.22 or later, the Ashita v4 SDK, and the x86
MSVC build environment. Set `ASHITA4_SDK_PATH` to the SDK directory:

```batch
set "ASHITA4_SDK_PATH=C:\Games\YourAshita\plugins\sdk"
```

From an x86 Native Tools Command Prompt, run:

```batch
cmake --preset x86-release-win
cmake --build --preset x86-release-win
```

A successful configure and build writes `bin/fluffytail.dll`.

## License

<a href="LICENSE.GPL.txt"><img src="https://www.gnu.org/graphics/gplv3-127x51.png" alt="GNU GPLv3 logo"></a>
<a href="LICENSE.md"><img src="https://www.gnu.org/graphics/lgplv3-147x51.png" alt="GNU LGPLv3 logo"></a>

The Ashita CMake and Direct3D scaffold uses
[GNU GPL version 3](LICENSE.GPL.txt). The plugin interface scaffold and original
C++ work created by this project use [GNU LGPL version 3](LICENSE.md). The
[component license inventory](LICENSES.md) records exact provenance and affected
files. No license is currently stated for the Python release tools and tests.

*Final Fantasy XI* and its models and textures are property of Square Enix.
This project is an unofficial fan mod. It is unaffiliated with and unendorsed by
Square Enix.
