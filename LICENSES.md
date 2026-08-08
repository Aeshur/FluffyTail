# Component licenses

| Component | Source and provenance | Affected files | License text |
| --- | --- | --- | --- |
| Ashita CMake and Direct3D scaffold | [AshitaXI/screenshot-src at `2d8812f`](https://github.com/AshitaXI/screenshot-src/tree/2d8812fd98f652f457e273cf74bad31a4bcc3ec9) | `CMakeLists.txt`, `cmake/FindAshitaSDK.cmake`, `src/defines.hpp` | [GNU GPL v3](LICENSE.GPL.txt) |
| Ashita plugin interface scaffold | [AshitaXI/exampleplugin at `b67726f`](https://github.com/AshitaXI/exampleplugin/tree/b67726f02fb55e6d2aef94a402ef6875384116eb) | `src/fluffytail.cpp`, `src/fluffytail.hpp`, `src/exports.def` | [GNU LGPL v3](LICENSE.md) |
| Ashita v4 SDK | Distributed separately with Ashita and selected by `ASHITA4_SDK_PATH` at build time | External headers and libraries, not included in this repository | [GNU LGPL v3](LICENSE.md) |
| FluffyTail C++ implementation | Modifications to the Ashita plugin scaffold authored in this repository | FluffyTail code in `src/fluffytail.cpp`, `src/fluffytail.hpp`, and `src/exports.def` | [GNU LGPL v3](LICENSE.md), as stated in the source headers |
| Release tooling and tests | Python tools and synthetic tests authored in this repository | `tools/`, `tests/`, and `pyproject.toml` | No license is currently stated for these files |

The release DAT overlay contains modified Final Fantasy XI model data. Final Fantasy
XI and its models and textures are property of Square Enix. See the disclaimer in
[README.md](README.md).
