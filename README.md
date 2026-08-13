# FluffyTail

FluffyTail adds a fluffy tail to the retail Mithra body armor model and selects
the tail colour for each face and hair variant. One x86 DLL contains adapters for
both supported hosts:

- Ashita v4.3 (the Ashita plugin ABI)
- Windower 4.7.9 (the native plugin ABI, interface version `0x04070300`)

The release also contains a 408-file XiPivot overlay. Fixed-model NPC colours
are baked into their DATs; normal Mithra colours are selected by the plugin at
render time.

## Install

Choose the archive for the host you use. Do not install both copies of the
overlay into one host.

### Ashita

Requirements:

- [Ashita v4.3](https://github.com/AshitaXI/Ashita-v4beta)
- The XiPivot `pivot` polplugin, installed separately and enabled in the active
  Ashita boot profile

1. Download `fluffytail.zip` from the
   [latest release](https://github.com/Aeshur/FluffyTail/releases/latest/download/fluffytail.zip)
   and extract it into the Ashita directory. Merge `plugins/` and
   `polplugins/` with the existing directories.
2. Add `FluffyTail` to the `[overlays]` section in your existing
   `Ashita/config/pivot/pivot.ini`, using the next unused number. The package
   does not include or replace this settings file or the XiPivot binary.
3. Ensure the active boot profile enables `pivot`, then load the plugin with
   `/load fluffytail` (or add that command to the profile script).

### Windower

Requirements:

- [Windower 4.7.9](https://windower.net/)
- The XiPivot `XIPivot` addon, installed separately

1. Download `fluffytail-windower.zip` and extract it into the Windower
   directory. Merge `plugins/` and `addons/` with the existing directories.
2. Add the `FluffyTail` overlay to your existing XIPivot configuration. The
   package writes only `addons/XIPivot/data/DATs/FluffyTail/ROM/`; it never
   overwrites XIPivot settings or bundles the XIPivot addon.
3. Load the native plugin with Windower's plugin loader (`//load fluffytail`).

Exit the game before replacing the DLL or DAT files. Keep XiPivot enabled while
the plugin is loaded. Costume bodies that hide the original tail remain
unchanged, and Nanaa Mihgo's unsupported fixed-model rig is not modified.

## Build

The build requires CMake 3.22 or later, Ninja, an x86 MSVC Native Tools prompt,
the Ashita v4 SDK, and the private DAT rig assets used by the overlay tools. Set
`ASHITA4_SDK_PATH` to the SDK directory:

```batch
set "ASHITA4_SDK_PATH=C:\Games\YourAshita\plugins\sdk"
cmake --preset x86-release-win
cmake --build --preset x86-release-win
```

The result is the single dual-host `bin/fluffytail.dll`. Build the neutral
runtime overlay and both deterministic release archives with:

```batch
python tools/build_runtime_overlay.py
python tools/build_packages.py bin/fluffytail.dll work/runtime-overlay work/packages
```

This writes `work/packages/fluffytail.zip` for Ashita and
`work/packages/fluffytail-windower.zip` for Windower. Existing output files
require `--force` or a new output directory. Each archive contains exactly 413
entries: the shared DLL, 408 validated DAT files, the README and provenance
inventory, and the GPLv3 and LGPLv3 notices under `FluffyTail/`.

Run the offline checks before packaging:

```batch
python -m pytest
ruff check tools tests
python tools/verify_windower_abi.py
```

## Validation status

Validation is offline only. The test suite checks DAT chunk structure, overlay
file counts and paths, deterministic ZIP metadata, the x86 PE header, and the
Windower 4.7.9 ABI (`0x04070300`) against pinned official Hook and Config binary
hashes. No in-game rendering or host loader validation has been performed for
this port.

## License

The Ashita CMake and Direct3D scaffold is [GNU GPL v3](LICENSE.GPL.txt). The
Ashita plugin interface scaffold and most FluffyTail C++ work are [GNU LGPL
v3](LICENSE.md). The Windower adapter and dual-host export definition include
GPLv3-derived Nameplate/Windozer material and are covered by [GNU GPL v3](LICENSE.GPL.txt).
The component inventory in [LICENSES.md](LICENSES.md) records exact provenance.
The Python release tools and tests are covered by [GNU LGPL v3](LICENSE.md).
The complete corresponding source for each published DLL is available from the
[FluffyTail source repository](https://github.com/Aeshur/FluffyTail) at the matching
release tag.

*Final Fantasy XI* and its models and textures are property of Square Enix. This
project is an unofficial fan mod and is unaffiliated with or unendorsed by
Square Enix.
