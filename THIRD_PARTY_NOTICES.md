# Third-Party Notices

SlaveTats UI depends on or interoperates with third-party projects. Those
projects remain governed by their own license terms.

## Build dependencies

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), version
  11.0.0 — GPL-3.0-or-later. The overlay port installs the upstream
  `COPYING.txt` license notice.
- [nlohmann/json](https://github.com/nlohmann/json) — MIT License.
- [DirectXTex](https://github.com/microsoft/DirectXTex) — MIT License.

The exact dependency revisions used for a build are selected by
`vcpkg-configuration.json` and recorded in the generated vcpkg installation
metadata.

## Vendored and interoperability headers

- `include/SKSEMenuFramework.h` is vendored from
  [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)
  under LGPL-2.1, as identified in the header.
- `include/FLICK/FUCK_API.h` is vendored from
  [FLICK API revision `22b5362`](https://github.com/Fuzzlesz/FUCK_API/tree/22b5362aa3d8c3d01c267339724aa895cba124b0)
  under GPL-3.0. The optional FLICK host is loaded dynamically.
- `include/FLICK/imgui/` contains Dear ImGui 1.92.8 public type headers under
  MIT License; see the included `LICENSE.txt`. No separate ImGui
  implementation is linked.
- `include/SlaveTatsNG_Interface.h` provides interoperability declarations for
  [SlaveTatsNG](https://github.com/nopse0/SlaveTatsNG).
- Files under `include/JContainers/` provide interoperability declarations for
  [JContainers](https://github.com/ryobg/JContainers).

Copies of third-party license texts and notices supplied with those projects
remain authoritative for their respective code.

## Simplified Chinese UI wording

The initial Simplified Chinese catalog adapts UI wording from
[MonetND/slavetats-ui](https://github.com/MonetND/slavetats-ui), revision
`073bfedc9a96b9a381e4d3b76e30f2d16a1ff0fd`, under this project's GPL-3.0-or-later
license. The localization loader, language selector and Korean/Japanese catalogs
are new work; no Chinese-fork runtime changes are incorporated.
