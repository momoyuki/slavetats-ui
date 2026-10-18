# SlavetatsUIPzx settings comparison — 2026-10-17

Research note for vNext.5 only. Source is the upstream repository at commit [`3bca8156`](https://github.com/pethzero/SlavetatsUIPzx/tree/3bca8156dc154dc5904e0e125e123ee378a82989); no external code was copied.

## Upstream user settings

| Setting | Upstream behavior | Source |
| --- | --- | --- |
| Browser hotkey | Configurable DirectInput scan code; `0` disables it. | [README Settings](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#settings), [Setup implementation](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/ui_menu.cpp#L945-L961) |
| Hover preview | On by default, toggled in Setup; hover paints a temporary preview, then restores on exit. | [README Features](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#tattoo-browser), [Setup](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/ui_menu.cpp#L963-L965) |
| Pause world | On by default; can be disabled in Setup. Upstream uses an empty pausing menu while its browser is open. | [README Features](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#tattoo-browser), [Setup](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/ui_menu.cpp#L968-L973) |
| Initial camera shot | Live Setup sliders for offset X/Y/Z, zoom, FOV, rotation Z and pitch X; explicit Save Settings and Reset Defaults. Values are persisted in `SlavetatsUIPzx.ini`; Recenter reapplies them. | [README Settings](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#settings), [Setup camera](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/ui_menu.cpp#L977-L1009), [INI persistence](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L60-L98) |

The current upstream README also describes a fixed left-docked browser, camera rotation and actor turn by mouse, a 12-slot preset manager, and shared preset files. These are features or navigation choices rather than Setup toggles. [README Features](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#features)

## Fit with this project

Our [vNext.5 roadmap](../../../ROADMAP.md#vnext5--visual-tattoo-browser) already includes a configurable Left/Right browser position and Player-first camera control. The current launcher implements a hotkey and browser-side selector in [`OfficialMenuFrameworkAdapter.cpp`](../../../src/native/OfficialMenuFrameworkAdapter.cpp); the Browse screen includes a black/white thumbnail-background toggle and hover preview. The [camera design](../specs/2026-10-15-browser-position-and-camera-design.md) deliberately defers explicit shot placement and distance settings until diagnostics.

**Best next candidate:** a small, optional **initial framing** setting for each menu side (or a single lateral framing amount mirrored by side), plus a clear Reset View action. This is closer to the user's current left/right composition problem than copying seven upstream camera sliders. Preserve the captured camera angle and zoom by default, as agreed with the user; offer advanced FOV/rotation/zoom defaults only if real-game testing shows that framing alone cannot solve it. Upstream's live camera sliders show that configurable framing can help, but its preset shot is a different behavior contract from ours. [Upstream camera controls](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/ui_menu.cpp#L977-L1009)

**Potential later candidate:** a Preview enable/disable switch if users need to avoid transient RaceMenu overlay changes. The upstream README explicitly warns that preview visuals may enter a RaceMenu co-save if the user saves the game while previewing. Our own actor-scoped preview transaction and save/co-save acceptance should be validated first. [Upstream preview warning](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/README.md#gameplay), [our preview design](../specs/2026-10-16-catalog-hover-preview-design.md)

**Defer:** Pause world. It changes game-menu and input ownership and is unrelated to the active visual browser acceptance. Upstream's implementation uses a dedicated pausing menu; it is not a free configuration-only addition. [Upstream pause implementation](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L443-L444)
