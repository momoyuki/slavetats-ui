# Scrollable Tattoo Grid Design

## Intent

Make tattoo browsing feel like a visual gallery: up to six thumbnails per row, a stable vertical scroll, readable tattoo names, and the selected Actor visible beside the panel. The user chose this as the first UI improvement after reviewing SlavetatsUIPzx and Fitting Room.

The Outfit Gallery video shared on 2026-10-15 is a visual reference for the overall interaction: a compact scrollable gallery stays beside a large, unobstructed character view while the user moves through visual choices. Our catalog remains a tattoo browser, with six columns at a typical wide viewport as requested. The video does not replace the separate safety design required for tattoo hover preview.

## Scope

- The existing SKSE Menu Framework Browse screen presents all filtered catalog matches in a vertically scrolling grid. At the usual 1920-pixel viewport, six columns fit in the existing left panel; narrow panels use fewer columns so cards remain usable.
- Search and contextual filters keep their current semantics. Changing a filter returns the grid to its top. Opening a tattoo retains the explicit target and existing confirmation flow.
- Only the visible rows and a small margin around them request DDS thumbnails. Offscreen cards retain placeholders until scrolled into view. Missing or broken textures keep their existing status treatment.
- Catalog gallery cards fit the visible alpha region of each DDS to the thumbnail
  cell with padding and at most eightfold magnification. An empty or unreadable
  alpha region uses the full texture. Cropping changes presentation UVs only;
  the source DDS and tattoo applied to the Actor remain unchanged. Analysis is
  bounded to 16 million pixels; larger textures use the full view. Other
  Current Slot thumbnails use the same visible-alpha crop and cached texture
  as Browse, so applied tattoos remain readable in their slots. Other
  thumbnail surfaces keep their full-texture view.
  BC3/DXT5 textures are scanned directly from compressed alpha blocks to avoid
  allocating a full decompressed image; common fully transparent blocks are
  skipped before per-pixel inspection.
- BC3/DXT5 textures larger than 192 pixels in either dimension generate a
  bounded RGBA thumbnail during that same alpha scan. The GPU cache keeps the
  thumbnail rather than the source-resolution texture. Alpha-weighted color
  and maximum source alpha preserve narrow marks in the gallery. Non-BC3 and
  small textures retain the existing upload path. This changes only browser
  thumbnail presentation; SlaveTatsNG still reads the original DDS for tattoos.
  Large BC7 textures use the closest existing mip at or above 192 pixels,
  decompress only that mip, and cache a bounded RGBA DDS. If the source has
  too few mips, the selected mip is resized to 192 pixels. This prevents
  full-resolution BC7 upload and alpha analysis during browsing.
  The GPU cache retains up to 64 recently used thumbnails within a 16 MiB
  uploaded-image byte budget and a two-minute idle TTL. Oversized images can
  still be displayed but are not retained by the cache. Visible references
  may temporarily use additional VRAM. Large BC3 thumbnails are cached as
  reduced DDS files; other DDS files up to 2 MiB are cached unchanged under
  the SKSE log directory. The disk cache skips
  source archive reads and resizing on subsequent visits, retains at most
  256 MiB, and evicts least recently used files. Source path plus loose-file
  or archive metadata stamps select the cache entry. Invalid cached images
  fall back to the original DDS. The SKSE launcher has a Clear Thumbnail Cache
  button that removes only files in this dedicated cache directory.
  The launcher also offers Build Thumbnail Cache for all installed catalog
  texture paths, processing one path per scheduled game task and showing
  completed/total/failure counts plus current disk usage. Building creates
  disk DDS files without uploading textures to GPU; Clear is unavailable
  while a build is active.
  The build queue deduplicates texture paths across ASCII case and slash style
  before scheduling game-thread work.
  A prebuild validates existing DDS cache entries and regenerates corrupt ones
  from their source texture instead of counting unreadable files as complete.
  The 192-pixel size is an in-game quality experiment between the responsive
  128-pixel cache and the sharper 256-pixel version. It uses a new cache key
  so older entries cannot be reused. Typical RGBA thumbnails use about 144 KiB
  each, allowing up to 64 entries within the existing 16 MiB GPU budget when
  other sizes permit.
- The footer keeps Cancel and Close while removing page-number navigation from this screen.
- The Browse footer reserves the same space before and after a tattoo is
  selected, so clicking a card does not resize the visible gallery window or
  push footer buttons below the viewport. Thumbnails that remain visible across
  a small scroll retain their ready texture even if the GPU cache evicted it.
- Browse shows the fixed Actor in its target label and omits Actor switching controls.
  Contextual filter options are computed only while the Filters popup is open.
- Current Slots retains pagination. A user-approved presentation refinement uses
  four columns and twelve slots per page, as described in the FLICK UI spec.

## Click preview follow-up within this UI effort

Clicking a catalog card shows an uncommitted tattoo on the Actor; Hover changes only the outline and tooltip. The preview requires a separate transactional design: the current appearance preview changes fields of an already-owned tattoo, while catalog selection may replace a tattoo identity or occupy an empty slot. No direct RaceMenu or JContainers write may be added to the UI. The preview must be actor-scoped, coalesce rapid selections, prevent saving a transient overlay, and provide a retryable restore path if the Actor unloads or synchronization fails.

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
