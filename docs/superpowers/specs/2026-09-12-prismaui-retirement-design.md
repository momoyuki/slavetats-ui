# PrismaUI Retirement Design

## Context

SlaveTatsUI now provides the player-facing slot workflow through SKSE Menu
Framework: current-slot browsing, catalog selection, thumbnails, apply, replace,
remove, color editing, alpha editing, refresh, and synchronization. PrismaUI is
still initialized by `Bridge`, owns the shared `SlaveTatsRuntime` and
`SlaveTatsService`, receives the configurable hotkey, and contributes browser
assets, serializers, tests, build inputs, and deployment requirements.

This change makes the native menu the only frontend and removes PrismaUI as a
runtime and packaging dependency. It is intentionally a compatibility-breaking
release change.

## Goals

- Make SKSE Menu Framework the only SlaveTatsUI frontend.
- Open and close the native workflow with the configured hotkey.
- Preserve the existing native player slot, catalog, thumbnail, mutation,
  appearance, and synchronization behavior.
- Give the runtime and service a UI-independent process-lifetime owner.
- Remove all shipped PrismaUI code, browser assets, tests, build inputs,
  documentation, and deployment steps.
- Fail clearly when SKSE Menu Framework is unavailable instead of silently
  falling back to PrismaUI.

## Non-Goals

- Supporting nearby NPC selection; the native workflow remains player-only.
- Preserving Prisma JSON request or response compatibility.
- Migrating old PrismaUI browser state or its generated thumbnail cache.
- Deleting PrismaUI from a user's MO2 installation; other mods may still use it.
- Deploying to MO2 or performing in-game acceptance without separate approval.

## Architecture

Introduce `ApplicationRuntime` as the process-lifetime composition boundary for
SlaveTats integration. It owns one `SlaveTatsRuntime` and one
`SlaveTatsService`, exposes the service to native workflow callbacks, and
forwards SlaveTatsNG version/interface and JContainers bindings to the runtime.
It contains no presentation, JSON, texture, hotkey, or Menu Framework logic.

`main.cpp` remains the plugin composition root. It constructs the application
runtime before the native workflow runtime, injects the shared service into the
native callbacks, owns the registered `NativeMenu`, and routes the configured
hotkey to that menu. This removes the last reason for `Bridge` to exist.

The ownership flow becomes:

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
                            |
                            v
             NativeMenu / Menu Framework
```

## Lifecycle and Input

Plugin load registers the native menu and records whether registration
succeeded. `kDataLoaded` continues to build the catalog and thumbnail runtime,
then starts the native slot workflow. It no longer loads PrismaUI or creates a
browser view.

The configured hotkey calls the native menu launch path. Launch resets or
refreshes the player workflow using the same behavior as the Menu Framework
section item, then opens the registered window. Pressing the hotkey while the
window is open closes it. If the native menu is unavailable, the key does
nothing and one actionable startup error is logged.

New opening requests from either the configured hotkey or the Menu Framework
section item are ignored while Skyrim's Console or RaceSex Menu is open. The
matching hotkey remains consumed. This guard does not auto-close a SlaveTatsUI
window that was already open and does not block its normal close path.

To avoid duplicating launch behavior, `NativeMenu` receives a public `toggle`
operation which uses its existing launch callback when opening. Unit tests cover
open, close, launch rejection, and unavailable registration behavior before the
production hotkey is rerouted.

## Removal Scope

Remove these production and compatibility artifacts:

- `include/PrismaUI_API.h`
- `src/Bridge.h` and `src/Bridge.cpp`
- `src/adapters/PrismaSlotSerializer.*`
- `src/adapters/PrismaTattooSerializer.*`
- their serializer tests and CMake test targets
- `view/index.html` and the empty `view` directory
- Prisma-only CPU decode, Base64 transport, and persistent `.rgba` cache code
  currently contained in `Bridge`

Update CMake metadata and source lists so none of these files compile. Update
README, development, deployment, and migration documentation to describe the
native-only architecture, SKSE Menu Framework requirement, DLL-only package,
native GPU thumbnail behavior, and the fact that PrismaUI can be removed from
this mod's dependency list.

Historical design and implementation records under `docs/superpowers` remain
unchanged except for this decision record; references in those files describe
the state and constraints at the time they were written.

## Error Handling

- Missing or incompatible SKSE Menu Framework is a frontend-unavailable error,
  logged with the existing registration error name. Plugin loading remains
  non-fatal so the log can diagnose the installation.
- Missing SlaveTatsNG or JContainers continues to surface through typed service
  errors in the native workflow.
- A rejected or throwing launch callback leaves the native window closed and
  records `callbackFailed` where appropriate.
- Catalog or thumbnail failures retain the existing placeholder and logging
  behavior and do not prevent the menu from opening.

## Testing

Implementation follows red-green-refactor:

1. Extend `NativeMenuTests` with failing tests for hotkey-style toggle behavior.
2. Add focused `ApplicationRuntimeTests` proving interface/version/JContainers
   forwarding and shared-service availability without a UI dependency.
3. Implement the minimum runtime owner and native toggle path.
4. Remove Prisma sources, assets, and test targets only after the native path
   builds and its focused tests pass.
5. Run all Debug and Release CTest suites and build the plugin in both
   configurations.
6. Search production/build/deployment files for case-insensitive `Prisma` and
   `.rgba`; only historical records may retain matches.

In-game acceptance after separately approved deployment:

- the configured hotkey opens and closes the native menu;
- the Menu Framework section item still opens it;
- neither entry point opens the native menu over Console or RaceMenu;
- current slots refresh for the player;
- catalog search, filters, pagination, and thumbnails work;
- apply, replace, remove, edit appearance, and sync work;
- startup logs contain no PrismaUI load or fallback messages;
- the packaged mod contains only the DLL/config-related files and does not
  require a `PrismaUI/views` directory.

## Release and Rollback

The change should be one dedicated retirement commit after tests and review.
Rollback is the inverse commit; no persisted tattoo data format changes. Users
may delete the old `PrismaUI/views/SlaveTatsUI` directory and legacy thumbnail
cache manually, but the plugin will neither require nor modify them.
