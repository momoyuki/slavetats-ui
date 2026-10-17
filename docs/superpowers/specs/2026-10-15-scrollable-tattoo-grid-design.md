# Scrollable Tattoo Grid Design

## Intent

Make tattoo browsing feel like a visual gallery: up to six thumbnails per row, a stable vertical scroll, readable tattoo names, and the selected Actor visible beside the panel. The user chose this as the first UI improvement after reviewing SlavetatsUIPzx and Fitting Room.

The Outfit Gallery video shared on 2026-10-15 is a visual reference for the overall interaction: a compact scrollable gallery stays beside a large, unobstructed character view while the user moves through visual choices. Our catalog remains a tattoo browser, with six columns at a typical wide viewport as requested. The video does not replace the separate safety design required for tattoo hover preview.

## Scope

- The existing SKSE Menu Framework Browse screen presents all filtered catalog matches in a vertically scrolling grid. At the usual 1920-pixel viewport, six columns fit in the existing left panel; narrow panels use fewer columns so cards remain usable.
- Search and contextual filters keep their current semantics. Changing a filter returns the grid to its top. Opening a tattoo retains the explicit target and existing confirmation flow.
- Only the visible rows and a small margin around them request DDS thumbnails. Offscreen cards retain placeholders until scrolled into view. Missing or broken textures keep their existing status treatment.
- The footer keeps Cancel and Close while removing page-number navigation from this screen.
- Current Slots retains pagination. A user-approved presentation refinement uses
  four columns and twelve slots per page, as described in the FLICK UI spec.

## Hover preview follow-up within this UI effort

The requested hover behavior means showing an uncommitted tattoo on the Actor and restoring the exact prior visual state on pointer exit. It requires a separate transactional design before runtime writes: the current appearance preview changes fields of an already-owned tattoo, while catalog hover may replace a tattoo identity or occupy an empty slot. No direct RaceMenu or JContainers write may be added to the UI. The preview must be actor-scoped, coalesce rapid hovers, prevent saving a transient overlay, and provide a retryable restore path if the Actor unloads or synchronization fails. This work is planned after the gallery is verified.

## Safety and acceptance

- SlaveTatsNG remains authoritative, and the adapter only presents model state and intents.
- No hover in the gallery mutates the Actor until the separate preview transaction is implemented and tested.
- A large pack remains responsive while scrolling; scrolling never loads the whole catalog's textures at once.
- Search, filters, favorites, In Use labels, selection, actor switching, and empty/missing-thumbnail states remain correct.
- Debug and Release builds and CTest pass; in-game QA covers 1920-pixel and narrower viewports, mouse-wheel scrolling, and returning from selection.

## References

- https://github.com/pethzero/SlavetatsUIPzx
- https://www.nexusmods.com/skyrimspecialedition/mods/185342
- https://x.com/SGR01898939/status/2110638649805271047
- `docs/superpowers/specs/2026-10-14-flick-ui-design.md`
- `docs/superpowers/specs/2026-09-20-native-live-actor-preview-design.md`
