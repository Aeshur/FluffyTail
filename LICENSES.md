# Component licenses

| Component | Source and provenance | Affected files | License text |
| --- | --- | --- | --- |
| Ashita CMake and Direct3D scaffold | [AshitaXI/screenshot-src at `2d8812f`](https://github.com/AshitaXI/screenshot-src/tree/2d8812fd98f652f457e273cf74bad31a4bcc3ec9) | `CMakeLists.txt`, `cmake/FindAshitaSDK.cmake`, `src/defines.hpp` | [GNU GPL v3](LICENSE.GPL.txt) |
| Ashita plugin interface scaffold | [AshitaXI/exampleplugin at `b67726f`](https://github.com/AshitaXI/exampleplugin/tree/b67726f02fb55e6d2aef94a402ef6875384116eb) | `src/fluffytail.cpp`, `src/fluffytail.hpp` | [GNU LGPL v3](LICENSE.md) |
| Ashita v4 SDK | Distributed separately with Ashita and selected by `ASHITA4_SDK_PATH` at build time | External headers and libraries, not included in this repository | [GNU LGPL v3](LICENSE.md) |
| Windower native plugin ABI declarations | [Shirk/Nameplate Windozer declarations](https://github.com/Shirk/Nameplate), including `Windozer.h` and `Windozer.cpp`; adapted for Windower 4.7.9 | `src/windower.hpp`, `src/windower.cpp` | [GNU GPL v3](LICENSE.GPL.txt), as stated in the source headers |
| Dual-host export definition | Ashita and Windower export names authored from their respective host contracts; the Windower entries derive from the GPLv3 Windozer declarations | `src/exports.def` | [GNU GPL v3](LICENSE.GPL.txt) |
| FluffyTail C++ implementation | Modifications to the Ashita plugin scaffold and shared runtime authored in this repository | `src/fluffytail.cpp`, `src/fluffytail.hpp`, `src/hook_utils.cpp`, `src/hook_utils.hpp`, `src/render_runtime.cpp`, `src/render_runtime.hpp`, and `src/tail_policy.hpp` | [GNU LGPL v3](LICENSE.md), as stated in the source headers |
| Release tooling and tests | Python tools and synthetic tests authored in this repository | `tools/`, `tests/`, and `pyproject.toml` | [GNU LGPL v3](LICENSE.md) |

The release DAT overlay contains modified Final Fantasy XI model data. Final Fantasy
XI and its models and textures are property of Square Enix. See the disclaimer in
[README.md](README.md).
