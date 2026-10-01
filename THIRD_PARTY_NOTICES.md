# Third-Party Notices

SlaveTats UI depends on or interoperates with third-party projects. Those
projects remain governed by their own license terms.

## Build dependencies

- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), version
  3.7.0 — MIT License.
- [nlohmann/json](https://github.com/nlohmann/json) — MIT License.
- [DirectXTex](https://github.com/microsoft/DirectXTex) — MIT License.

The exact dependency revisions used for a build are selected by
`vcpkg-configuration.json` and recorded in the generated vcpkg installation
metadata.

## Vendored and interoperability headers

- `include/SKSEMenuFramework.h` is vendored from
  [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3)
  under LGPL-2.1, as identified in the header.
- `include/SlaveTatsNG_Interface.h` provides interoperability declarations for
  [SlaveTatsNG](https://github.com/nopse0/SlaveTatsNG).
- Files under `include/JContainers/` provide interoperability declarations for
  [JContainers](https://github.com/ryobg/JContainers).

Copies of third-party license texts and notices supplied with those projects
remain authoritative for their respective code.
