# SlaveTats UI Deployment Guide

This guide covers development deployment through Mod Organizer 2. Build
instructions are in [DEVELOPMENT.md](DEVELOPMENT.md#building). GitHub release
publication is a separate process documented in [RELEASING.md](RELEASING.md).

## Prerequisites

- Skyrim and SKSE are not running.
- The Debug or Release DLL exists under `build\<config>\SlaveTatsUI.dll`.
- The MO2 profile has SKSE64, SKSE Menu Framework 3.x, SlaveTatsNG,
  JContainers SE, and at least one texture pack enabled.
- A dedicated empty MO2 mod exists for SlaveTats UI.

## Mod Layout

```text
<MO2 mods dir>\SlaveTatsUI\
└── SKSE\Plugins\SlaveTatsUI.dll
```

## Deploy

```powershell
$mod = "D:\Modding\SKYRIM-MOD\mods\SlaveTatsUI"
$config = "debug"

New-Item "$mod\SKSE\Plugins" -ItemType Directory -Force | Out-Null
Copy-Item "build\$config\SlaveTatsUI.dll" "$mod\SKSE\Plugins\SlaveTatsUI.dll" -Force
```

Never copy directly into Skyrim's physical `Data` directory. Deploy only to the
dedicated MO2 mod so enable, disable, conflict, and uninstall operations remain
recoverable.

## Verify

1. Compare the built and deployed DLL hashes with `Get-FileHash`.
2. Launch Skyrim through SKSE in MO2.
3. Inspect `SlaveTatsUI.log` for native menu registration and dependency errors.
4. Press the configured hotkey and confirm it opens and closes the native menu.
5. Verify current slots, catalog navigation, thumbnails, and one non-destructive
   catalog interaction on the Player.
6. Select a loaded Crosshair Target, confirm its identity is shown, and verify
   its slots load without changing the Player.

An old browser-view directory or legacy thumbnail cache may be removed manually
from this mod after backing it up. The plugin neither reads nor deletes those
paths.
