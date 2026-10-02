# Thumbnail Background Toggle Design

## Context

Tattoo thumbnails are currently rendered against a fixed preview background. This can make a dark tattoo difficult to inspect on a dark background and a light tattoo difficult to inspect on a light background.

User feedback requested a simple way to switch the background behind tattoo thumbnails between black and white so the tattoo texture remains readable regardless of its dominant color.

This is a presentation-only quality-of-life feature. It must not change tattoo data, SlaveTatsNG state, thumbnail texture content, or actor overlays.

## Goal

Allow the user to toggle the background drawn behind tattoo thumbnails between **Black** and **White**.

The selected mode should apply consistently to tattoo thumbnail surfaces in the native UI where the shared thumbnail/card renderer is used.

## Scope

- Add two thumbnail background modes: `Black` and `White`.
- Provide a small native UI control that switches the current mode.
- Treat the selection as a global UI preference rather than a per-tattoo value.
- Apply the chosen background before drawing the tattoo thumbnail texture.
- Preserve existing tattoo tint/alpha preview behavior.
- Keep the feature entirely inside the UI/presentation layer unless persistence is implemented through an existing UI-settings mechanism.

## Non-Goals

- No checkerboard mode in the initial change.
- No split black/white preview in the initial change.
- No automatic contrast detection.
- No skin-tone backgrounds.
- No per-tattoo background preference.
- No mutation of tattoo color, alpha, glow, material state, or pack metadata.
- No SlaveTatsNG or JContainers changes.
- No synchronization request when the background changes.

## UX

Expose a compact control near the tattoo browser/thumbnail presentation controls, for example:

```text
Thumbnail BG: [ Black | White ]
```

An icon toggle is also acceptable if the current state is unambiguous and a tooltip names the resulting background.

Changing the mode should update visible thumbnails immediately on the next render frame. It must not reload or regenerate DDS thumbnail textures solely because the background changed.

## State

The background mode is UI state, not tattoo state.

Recommended model:

```cpp
enum class ThumbnailBackgroundMode {
    black,
    white,
};
```

Use one selected value for the relevant native UI session rather than storing a value on `TattooEntry` or any runtime tattoo model.

### Persistence

Minimum requirement: preserve the selected mode while the native menu remains alive and across navigation between Current Slots, Picker, Preview, and Edit Appearance.

Optional follow-up: persist the preference across game launches if the project already has an appropriate settings/configuration store. Do not introduce a new persistence subsystem solely for this toggle.

## Rendering

The renderer should:

1. compute the thumbnail rectangle as it does today;
2. draw a solid black or white rectangle behind the thumbnail according to the selected mode;
3. draw the tattoo thumbnail texture using the existing UV, tint, and alpha behavior;
4. draw existing card borders, labels, badges, hover state, and selection state unchanged.

The background must not be baked into the cached thumbnail texture. Switching the mode should therefore not invalidate or duplicate the D3D11 thumbnail cache.

## Architecture

This feature belongs in the native presentation/workflow UI boundary.

Do not add fields for thumbnail background mode to:

- `core::TattooEntry`;
- SlaveTats service requests;
- `ITattooRuntime`;
- `SlaveTatsRuntime`;
- JContainers state;
- SlaveTatsNG tattoo templates.

If multiple thumbnail surfaces already share a rendering helper, prefer threading the background mode through that helper rather than duplicating draw logic.

## Acceptance Criteria

1. A user can switch tattoo thumbnail backgrounds between black and white from the native UI.
2. The change is visible immediately without applying, refreshing, or synchronizing tattoos.
3. Dark tattoos remain visible when the white background is selected.
4. Light tattoos remain visible when the black background is selected.
5. Switching background does not change tattoo color, alpha, glow, material values, slot state, or actor appearance.
6. Switching background does not cause a SlaveTatsNG/JContainers call.
7. Switching background does not reload or duplicate cached DDS thumbnail textures solely due to the background change.
8. The selected mode survives normal navigation within the menu session.
9. Existing hover, selection, labels, badges, tint, and alpha rendering continue to work.

## Suggested Tests

- state defaults deterministically to the chosen project default;
- toggling changes only the background-mode state;
- navigation does not reset the mode unexpectedly;
- thumbnail rendering helper chooses black/white fill correctly;
- tint and visible alpha remain unchanged between background modes;
- thumbnail cache key/count is unchanged by the background mode;
- no runtime/service mutation request is emitted when toggled.

## Scheduling

This is a small standalone QoL change and does not belong to the functional scope of vNext.2 Lock + Domain.

It may be implemented opportunistically after a safe vNext.2 checkpoint and before vNext.3, without changing the meaning or completion criteria of either milestone.

If implementation work is already touching the shared thumbnail renderer, it is reasonable to include this change at that point as long as the diff remains isolated and independently testable.
