# SlaveTats UI Development

SlaveTats UI is a native C++ SKSE plugin. SKSE Menu Framework owns window
registration and rendering; the plugin owns a typed, UI-independent tattoo
service and a bounded D3D11 thumbnail path.

## Architecture

```text
SKSE / SlaveTatsNG / JContainers messages
                 |
                 v
        ApplicationRuntime
       /                  \
SlaveTatsRuntime     SlaveTatsService
                            |
                            v
               NativeSlotWorkflowRuntime
                 /                    \
 NativeSlotWorkflowModel     NativeThumbnailRuntime
                 \                    /
                  OfficialMenuFrameworkAdapter
                              |
                         NativeMenu
```

- `ApplicationRuntime` owns one concrete runtime and service for the process.
- `SlaveTatsService` validates transport-independent queries and mutations.
- `SlaveTatsRuntime` binds SlaveTatsNG and JContainers to Skyrim operations.
- `NativeSlotWorkflowModel` owns Actor Target-scoped slot, picker, preview, and
  edit state.
- `NativeSlotWorkflowRuntime` schedules game-thread queries and mutations.
- `NativeThumbnailRuntime` resolves only requested page textures and publishes
  bounded D3D11 shader-resource views.
- `OfficialMenuFrameworkAdapter` renders the model without owning domain state.
- `NativeMenu` registers the blocking window and handles launch/toggle behavior.

## Source Layout

```text
include/                         External API headers
src/core/                        Typed models, runtime port, and service
src/runtime/                     Skyrim adapters and application ownership
src/repository/                  Tattoo discovery, parsing, and querying
src/textures/                    DDS resolution, decoding, upload, and cache
src/native/                      Workflow, thumbnails, and Menu Framework adapter
tests/                           Unit and integration-style executable tests
docs/superpowers/specs/          Approved design records
docs/superpowers/plans/          Implementation plans
```

## Building

Requirements:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.21 or newer and Ninja
- vcpkg with `VCPKG_ROOT` set
- dependencies declared in `vcpkg.json`

Use the repository script because it imports the MSVC environment required by
CommonLibSSE and D3D11 headers:

```powershell
.\build.ps1 -Config debug
.\build.ps1 -Config release
```

Outputs:

```text
build\debug\SlaveTatsUI.dll
build\release\SlaveTatsUI.dll
```

## Testing

The build script configures and builds all test executables. Run the complete
suite for both configurations before proposing a commit:

```powershell
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/release --output-on-failure
```

For a focused cycle, build through an MSVC-initialized shell or rerun
`build.ps1`, then use `ctest -R '<test-name>'`.

Tests are small C++ executables registered with CTest. New behavior follows
red-green-refactor: add a behavior test, observe the expected failure, implement
the minimum change, and rerun focused plus relevant full suites.

GitHub Actions repeats the complete Debug and Release verification on pull
requests targeting `main` and pushes to `main`. A green local run does not
replace the hosted check. Release automation and versioning are documented in
[RELEASING.md](RELEASING.md).

## Runtime Lifecycle

1. Plugin load initializes logging and registers the native menu.
2. SKSE, SlaveTatsNG, and JContainers listeners are registered.
3. `kDataLoaded` scans effective loose/BSA tattoo JSON sources and initializes
   the native D3D11 thumbnail runtime.
4. The configured hotkey or Menu Framework section item launches the selected
   Actor Target workflow and opens the native window.
5. External work is scheduled through the SKSE task interface; presentation
   observes model state on later frames.

## Thumbnail Policy

Search and filtering operate on metadata and never decode textures. The current
catalog or slot page requests only visible thumbnails. Texture resolution checks
loose files before the BSA resource reader, then DirectXTex decodes DDS bytes and
the D3D11 uploader creates shader-resource views. The cache is bounded to 12
entries with a two-minute idle lifetime. Failures produce placeholders and a
stage-specific log entry.

## Contribution Gates

- Keep core models and services free of SKSE Menu Framework and D3D11 types.
- Keep game-thread work out of render callbacks.
- Add tests for every new behavior and run Debug plus Release suites.
- Run `git diff --check` and inspect the full diff for secrets and unrelated
  changes.
- Show the diff and proposed Conventional Commit message before committing.
