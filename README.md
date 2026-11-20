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

Choose **SlaveTatsUI > Tattoo Browser** in SKSE Menu Framework. With FLICK
installed, choose the same tool in its sidebar. In the launcher, set the
browser position to Left or Right and press **Open Tattoo Browser**. A hotkey
can be configured in the launcher; it defaults to **None**.
The hotkey list includes punctuation keys `, . / ; ' [ ] - =` and
`BACKSLASH` (`\`). These bindings use physical keyboard scan codes.

Choose the Player or use **Choose NPC** to select a nearby loaded Actor, then
manage that Actor Target's current slots:

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
- the Browse gallery previews a tattoo only when clicked; hover shows a tooltip
  without changing the Actor, and Save Tattoo commits the selected draft;
- Refresh reloads current slot state and Sync reapplies visual updates.

The scrolling gallery requests thumbnails near visible rows. The launcher can
prebuild reduced DDS thumbnails for large textures into a bounded disk cache,
show build progress and disk usage, and clear that cache. The GPU cache is also
bounded; missing textures render a placeholder rather than blocking the menu.
**Thumbnail disk cache limit** selects 128, 256, 512, or 1024 MiB of disk
storage; **Reset to default** restores 256 MiB. The setting persists between
game sessions. Reducing the limit evicts older cached files when needed;
it does not change the GPU cache budget or the installed tattoo textures.

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

## UI Language

Open the **SlaveTatsUI > Tattoo Browser** launcher and choose **Language**.
English (`en`), Simplified Chinese (`zh-CN`), Korean (`ko`) and Japanese (`ja`)
are included. The choice is saved immediately in the existing `SlaveTatsUI.json`
configuration beside the log and takes effect on the next launcher frame.
Other configuration fields are preserved. A failed save keeps the previous
language and displays an error. The registered Menu Framework / FLICK entry
remains **SlaveTatsUI > Tattoo Browser** in every language.

**Fonts:** In SKSE Menu Framework's own settings, enable the Chinese, Korean
and Japanese glyph ranges you need and select a font containing those glyphs.
A translation cannot supply glyphs missing from a font. Follow the framework's
font setup/reload instructions; SlaveTats UI does not overwrite its global
settings or rebuild its shared font atlas. The built-in language names use ASCII English so you can switch back even if a font is missing.

The release includes these files under the game's `Data` directory:

```text
SKSE/Plugins/SlaveTatsUI/translations/en.tsv
SKSE/Plugins/SlaveTatsUI/translations/zh-CN.tsv
SKSE/Plugins/SlaveTatsUI/translations/ko.tsv
SKSE/Plugins/SlaveTatsUI/translations/ja.tsv
```

Missing files, missing keys and rejected entries use the original English text.
An unknown configured language also falls back to English. The catalogs load
once when the plugin starts; restart Skyrim after editing them. `en.tsv` is the
English source/reference catalog; English fallback is built into the code.

Catalogs are UTF-8, one **English source text**, a literal tab, and one
**translation** per line. Lines beginning with `#` are comments. Preserve spaces,
all printf placeholders (`%s`, `%d`, `%zu`, `%.1f`, etc.) and all C++ format
fields (`{}`, `{:.0f}`, etc.) in their original order. Use `\n` for a newline,
`\t` for a tab, `\s` for a space (especially at the end of a field), and `\\`
for a literal backslash. Empty translations, invalid
UTF-8, duplicate keys, changed placeholders and `##` widget-ID delimiters are
rejected and reported in the startup log; valid rows remain usable.

Language affects presentation only. Actor names, tattoo/pack names, domains,
paths and user-named presets/loadouts keep their original contents. Runtime
and dependency error details/logs may remain in English. Korean and Japanese
wording is an initial translation and welcomes native-speaker review.

### Add another language without rebuilding

1. Copy `en.tsv` into the same translations directory and rename it with a locale
   code, for example `fr.tsv`, `de.tsv`, `it.tsv`, `pl.tsv`, `ru.tsv` or `es.tsv`.
2. Set the first line to `# language-name: French` (or the desired display name).
   Translate the right-hand column only; leave English keys and placeholders intact.
3. Restart Skyrim. The new language appears automatically in **Language**, and its
   filename code is used for persistence. Incomplete translations fall back per key.

No C++ changes, DLL build or central language registry are required. A translation
mod can ship only `Data/SKSE/Plugins/SlaveTatsUI/translations/fr.tsv`, for example.
Contributors can submit new or corrected TSV files in `resources/translations/`;
release packaging discovers additional catalogs automatically. Filenames must start
with an ASCII letter, contain only ASCII letters/digits/`-`/`_`, and have at most
32 characters before `.tsv`. The English recovery option always remains available.
French/German/Italian/Polish/Russian/Spanish are examples of supported additions;
translations for those languages are not included yet. Enable their glyph ranges
and choose an appropriate font in the framework when needed.

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
