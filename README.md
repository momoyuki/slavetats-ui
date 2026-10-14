# SlaveTats UI

A native SKSE Menu Framework interface for SlaveTatsNG. Browse installed tattoo
packs, inspect the Player or a deliberately selected nearby Actor, and
apply, replace, edit, or remove tattoos without using MCM.

> Building from source or contributing? See [DEVELOPMENT.md](DEVELOPMENT.md) and
> [DEPLOY.md](DEPLOY.md). Maintainers should also read
> [RELEASING.md](RELEASING.md).

## License

SlaveTats UI is copyright 2026 mskmktx and is licensed under the
[GNU General Public License v3.0 or later](LICENSE). Distributions of compiled
binaries must provide recipients access to the corresponding source under the
same license terms.

Third-party components remain subject to their respective licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution.

## Requirements

Install and enable all of these before loading SlaveTats UI:

| Mod | Notes |
|-----|-------|
| [SKSE64](https://skse.silverlock.org/) | Must match the installed Skyrim SE/AE version |
| [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) | Version 3.x |
| [SlaveTatsNG](https://github.com/nopse0/SlaveTatsNG/tree/master) | Provides the tattoo runtime API |
| [JContainers SE](https://www.nexusmods.com/skyrimspecialedition/mods/16495) | Provides SlaveTats data storage |
| One or more SlaveTats texture packs | Loose files and BSA archives are supported |

[FLICK](https://www.nexusmods.com/skyrimspecialedition/mods/181603) v1.6.0 or
newer is optional. When installed, its **SlaveTatsUI > Tattoo Browser** tool
opens the same native browser. FLICK does not replace SKSE Menu Framework.

PrismaUI is not required by SlaveTats UI. It may remain installed if another
mod uses it.

## Installation

Install the release archive with Mod Organizer 2 and enable it. The runtime
plugin uses this path inside the package:

```text
SlaveTatsUI\
└── SKSE\Plugins\SlaveTatsUI.dll
```

Launch Skyrim through SKSE in MO2.

## Usage

Press **F8** by default or choose **SlaveTatsUI > Tattoo Browser** in SKSE Menu
Framework. With FLICK installed, choose **SlaveTatsUI > Tattoo Browser** in its
sidebar and press **Open Tattoo Browser**. The configured hotkey toggles the
native window. The browser opens as a left-side panel so the Actor Target stays
visible beside it.

Choose the Player or one of the nearby loaded Actors, then manage that Actor Target's
current slots:

- choose an empty slot to browse and apply a tattoo;
- choose an occupied SlaveTats slot to edit its appearance, lock or unlock it,
  replace it, or remove it;
- external overlay slots remain visible but read-only;
- search, domain/source/section/area, Applied, Favorites, Glow, Bump, and Gloss
  filters narrow the catalog;
- Favorites, six-entry area-scoped Recently Used lists, and ordered Appearance
  Presets speed up repeated work;
- appearance controls provide debounced live preview with safe Save,
  Cancel/Close restoration, and synchronization-only retry;
- Refresh reloads current slot state and Sync reapplies visual updates.

Only current-page thumbnails are requested. Loose and BSA-backed DDS textures
are decoded and uploaded to a bounded D3D11 cache; missing textures render a
placeholder rather than blocking the menu.

## Pack-Declared Material Texture Choices

Pack authors may add `glowTextureOptions` and `bumpOptions` to a tattoo entry in
the pack's JSON array. These fields are a **SlaveTats UI extension**, not part
of the upstream SlaveTatsNG schema. Existing `glowTexture` and `bump` fields
remain the pack defaults.

```json
[
  {
    "name": "Mark",
    "section": "Marks",
    "texture": "Pack\\mark.dds",
    "area": "Body",
    "glowTexture": "Pack\\mark_g.dds",
    "bump": "Pack\\mark_n.dds",
    "glowTextureOptions": [
      { "label": "Soft", "path": "Pack\\soft_g.dds" },
      { "label": "Sharp", "path": "Pack\\sharp_g.dds" }
    ],
    "bumpOptions": [
      { "label": "Smooth", "path": "Pack\\smooth_n.dds" },
      { "label": "Raised", "path": "Pack\\raised_n.dds" }
    ]
  }
]
```

Each option must be an object containing only a non-empty `label` and `path`.
Labels are trimmed and may not contain control characters or `##`. Paths retain
their exact bytes and may not contain NUL. Paths must be unique within each
list, ignoring ASCII case. A malformed list rejects that tattoo entry and
reports a catalog parse issue, while valid entries remain available.
Non-empty singular defaults appear as `Pack default` unless already listed.
The editor shows selectors only when it can match one catalog tattoo
unambiguously. Older packs without option arrays keep their existing read-only
texture display.

## Changing the Hotkey

The hotkey defaults to `None`. Open the `SlaveTatsUI` section in SKSE Menu and
choose a keyboard key from the `Hotkey` dropdown. Choose `None` to disable the
hotkey.

The setting is saved immediately to `SlaveTatsUI.json` beside the plugin log.
Existing named or raw DIK scancode values remain supported.

## Compatibility

- Skyrim SE 1.5.97 and AE 1.6.x are supported when the DLL is built against a
  matching SKSE/CommonLibSSE-NG setup.
- Actor Targets are the Player (FormID `0x14`) or one of up to ten nearby,
  loaded Actors discovered when the browser opens. Nearby Actors are ordered by
  distance and a failed scan never falls back silently to another Actor.
- Texture packs must follow
  `textures\actors\character\slavetats\<section>\*.dds`.

## Troubleshooting

**The UI does not open**

- Confirm SKSE Menu Framework 3.x is installed and enabled.
- Check `SlaveTatsUI.log` for `native menu unavailable` and its named reason.
- Confirm the configured hotkey is not claimed by another mod.

**JContainers or SlaveTatsNG is unavailable**

- Confirm both dependencies match the current Skyrim runtime and load through
  SKSE.
- Inspect `SlaveTatsUI.log` for interface-version or initialization errors.

**A thumbnail is missing**

- Confirm the tattoo pack is enabled and its DDS path follows the standard
  layout.
- Check the log for the texture path and failure stage (`resolve` or `upload`).

**Apply, edit, remove, or sync has no visible effect**

- Test on the selected loaded Actor Target in first- or third-person view.
- Use Refresh to reload slot state and Sync to request visual synchronization.

## Log Location

The primary log is normally written to:

```text
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\SlaveTatsUI.log
```

If that directory is unavailable, the plugin falls back to
`Data\SKSE\Plugins\SlaveTatsUI.log`.
