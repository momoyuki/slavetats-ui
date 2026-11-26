# Plan: configurable tattoos per row

This is a plan only. It does not change the game. Implementation waits until the owner says to start, on the local development machine.

Read this from the top. Phase 1 is the player setting for how many tattoo cards sit on one row. Phase 2 is the thumbnail-resolution and cache-size work. The owner answered the Phase 2 direction on 2026-11-26. Those answers are in [Handoff](#handoff-for-the-local-development-machine). They override the older "ask first" recommendations later in this file. Still do not write the feature until the owner says start.

## Handoff for the local development machine

Cloud session `bc-6de1b84b-4800-5e13-940b-8aac3d8f8352` stopped after the plan and these decisions. No self-hosted worker was connected, so this branch is the handoff. Continue from draft PR https://github.com/momoyuki/slavetats-ui/pull/27 on branch `cursor/configurable-tattoos-per-row-plan-8352`.

Do not implement yet. The next session should read this file, confirm the decisions below, and wait for the owner to say start. When they do, implement on this branch. Do not open a second plan.

### Owner decisions, 2026-11-26

1. **Match the resolution the game is actually rendering**, when that size can be read. It can. `OfficialMenuFrameworkAdapter::renderFoundation` already reads `ImGui::GetMainViewport()->Size`, and `calculateFoundationLayout` turns that into the browser panel (about 46.6% of the width). Card pixels are that content width divided by the per-row count. This is the game's backbuffer, not a second Windows DPI scale. Keep `dpiScale` at 1 unless a later in-game test shows the swapchain is stretched onto a denser panel and the cards are still soft at a matching tier.

2. **The thumbnail tier stays adjustable.** Auto-select the smallest of 192, 256, 384, and 512 that is at least the current card size. The launcher must still let the player move that tier up or down. Recompute the automatic tier when the viewport size or the per-row count changes, and only when the browser is opened again (`ImGuiCond_Appearing` already applies the panel size then). A manual choice sticks until the player sets it back to automatic.

3. **The disk-cache limit is automatic.** Do not ask the player to pick a MiB value. Set the limit from the active tier and the number of installed tattoo textures. The existing Clear Thumbnail Cache button remains the reset. Large libraries may use more disk. The owner accepts that, because the source tattoos are already large, and wants the work spent on a sensible thumbnail size instead.

4. **Smooth, low-friction browsing wins over maximum sharpness.** If the tier that covers the card cannot keep about one screen of pictures in the current 16 MiB GPU cache, step down one tier instead of growing the files. 384-pixel pictures fit about 28 in that budget, which covers a 4-per-row screen. 512-pixel pictures fit about 16 and are the wrong default when smoothness matters. 1440p with 4 per row is about a 300-pixel card, so 384 is the automatic tier there and about 1,500 square RGBA files need about 844 MiB, which the existing 1024 MiB choice can hold. A 512-pixel library of 1,500 files needs about 1,500 MiB and does not fit today's maximum. In that case step the tier down. Extend `ThumbnailCacheSetting::limitsMiB` only if a tier the player explicitly chose still cannot be stored. Apply the limit through the existing `requestDiskCacheLimit` path, off the render thread.

5. **Keep the current stutter rules.** One background worker job at a time. Do not decode on the render thread. Do not rebuild a failed thumbnail on every frame. A tier change uses a new cache key (`thumbnail-v8:` and `STUITH8` both move). Schedule the existing clear and the existing background build. Do not reuse a smaller DDS as the new tier.

Phase 1 (the per-row setting, square cards, saved integer) and Phase 2 (automatic tier from the live viewport, manual override, automatic disk limit) are one design now. Phase 1 must still not change the 192-pixel files by itself if it is ever split out. The owner asked for them together so the larger cards stay sharp enough without hitching.

### What to do when the owner says start

1. Re-read this file and the code references before editing.
2. Implement the per-row setting and the thumbnail tier together, with the disk limit derived from the tier.
3. Leave Current Slots at four columns. The shared thumbnail cache will still get the new tier, which the owner accepted by making the cache automatic for the whole thumbnail pipeline.
4. Add the tests named in Phase 1, plus tests that the tier follows viewport width and per-row count, that a manual tier is kept, and that an illegal or missing config falls back without deleting other JSON keys.
5. Do not merge the draft PR until the owner has reviewed an in-game pass at 1080p, 1440p, and 4K.

The owner is learning this codebase. Each step says what to change and why that step exists. Do the steps in order. Do not start with the slider: the setting has to exist before the screen can save it.

## What the player wants

Release 1.8.0 (`CMakeLists.txt` and `vcpkg.json`) draws the Tattoo Browser gallery with about six cards on a row. On a large monitor, for example 27 inches, those pictures are too small to recognize a tattoo. A community edit of `1.8.0-beta2` that showed four cards on a row was preferred. The count should be a saved setting, not a number baked into the layout.

Suggested setting for Phase 1:

- integer
- default **4**
- allowed range **3 through 8**, inclusive
- shown in the existing launcher settings screen
- saved in the same JSON file as the other settings

## Two phases

| Phase | What it does | When |
| --- | --- | --- |
| Phase 1 | Let the player choose 3–8 cards per row. Card width comes from the panel width divided by that count. Thumbnail files stay 192 pixels. The disk-cache limit stays whatever the player already saved (default 256 MiB). | First implementation, after this plan is accepted. |
| Phase 2 | Choose a thumbnail tier from the live game resolution and the per-row count, let the player override it, and set the disk-cache limit automatically. | Decided in direction on 2026-11-26. See [Handoff](#handoff-for-the-local-development-machine). Implement only when the owner says start, together with Phase 1. |

Phase 1 will make cards larger than the 192-pixel picture stored on disk. ImGui will stretch that picture. Stretching a small picture into a big card looks soft. That softness is expected until Phase 2. Do not "fix" it in Phase 1 by changing the cache.

## What Phase 1 must not change

- Thumbnail pixel size. Today every generated thumbnail fits in 192 pixels. See [where 192 is hardcoded](#where-192-pixels-is-hardcoded).
- Disk-cache limit choices: 128, 256, 512, or 1024 MiB, default 256. Class `ThumbnailCacheSetting`.
- GPU cache: 64 pictures, 16 MiB, forgotten after 2 minutes idle.
- The Current Slots grid. That screen is a fixed page of slot cards, four columns, not the scrolling gallery. `BeginTable("CurrentSlotCards", 4, ...)` in `renderCurrentSlots`.
- SlaveTatsNG, JContainers, apply/remove/appearance, and which actor is targeted.

## How the gallery works today

The Tattoo Browser is an ImGui window drawn by `OfficialMenuFrameworkAdapter::renderFoundation` in `src/native/OfficialMenuFrameworkAdapter.cpp`. After the other screens return, the fall-through path begins the window `"Tattoo Browser##SlaveTatsUI"`.

### The panel is not the whole monitor

`calculateFoundationLayout` sizes the browser panel:

- 20 pixels of margin on each side
- width = 46.6% of the game window, but never less than 360 pixels, and never wider than the window minus the margins
- height = the game window height minus the margins

The character view keeps the rest of the screen. "Available width" in this plan means the width inside this panel (`ImGui::GetContentRegionAvail().x` at the grid), not the full monitor width.

Rough panel widths (the real content width is a little smaller because of window padding):

| Game resolution | Window width | Panel width |
| --- | ---: | ---: |
| 1080p | 1920 | about 895 |
| 1440p | 2560 | about 1193 |
| 4K | 3840 | about 1789 |

The size is applied with `ImGuiCond_Appearing`. Closing and reopening the browser is what picks up a new resolution. Measure in game; do not treat the table above as exact.

### How many cards, and how wide

`catalogColumnCount` does **not** store a column count. It derives one:

```cpp
// src/native/OfficialMenuFrameworkAdapter.cpp
constexpr float minimumCardWidth = 140.0F;
return clamp(availableWidth / 140, 1, 6);
```

A wide panel therefore stops at 6 columns. Each column then shares the width equally because the table is created with `ImGuiTableFlags_SizingStretchSame`. On a 4K panel the six columns are wider than 140 pixels, but the picture does not grow with them. See the next section.

`thumbnailGridSupportsSixCardsPerRow` in `tests/native/OfficialMenuFrameworkAdapterTests.cpp` locks this in: 900 pixels → 6 columns, 300 → 2, 100 → 1.

### Why six columns still looks small

The row height is a fixed 160 pixels:

```cpp
constexpr float galleryRowHeight = 160.0F;
```

The thumbnail box is that 160 minus the name line and the table's vertical cell padding. `fitCatalogThumbnail` scales the picture with the **smaller** of "fit the width" and "fit the height", and it keeps the picture's own shape. It does not stretch.

So once the cell is wider than the thumbnail box is tall, extra column width only adds empty space beside the picture. The picture stays on the order of 120–140 pixels tall. On a 27-inch 1440p screen that is roughly an inch of tattoo. Fewer columns will not make the tattoo easier to see unless the row gets taller too.

That is why Phase 1 must set the row height from the card width, not only pass a new column count.

### What one card contains

Inside each cell, in this order:

1. A child window for the picture (`catalogCardWidgetId("Thumbnail", ...)`), height `gridLayout.thumbnailHeight`.
2. Badges drawn on top of the picture (Glow / Bump / Gloss, domain, in-use dot).
3. A second child for the name, one text line tall. Long names are shortened by `fitCatalogCardName`.

`ImGuiListClipper` skips rows that are off screen. It is given `gridLayout.rowHeight`, so every row must be the same height.

`catalogVisibleCardRange` turns the scroll position into the tattoos that should load pictures. It includes the rows on screen plus one row above and one row below. The browse path passes that range to `thumbnails.synchronize`, then `thumbnails.pump()`.

Those three numbers — column count, row height, and the visible range — must be computed from the same inputs. If the clipper and the loader disagree, the wrong pictures load and empty boxes scroll into view.

### Where settings live

There is no INI file and no SkyUI MCM page for this mod's own options. The launcher section (`OfficialMenuFrameworkAdapter::renderLauncher`) is the settings screen: language, hotkey, browser side, camera, and the thumbnail disk-cache limit.

Those values are saved in one JSON object, `SlaveTatsUI.json`, through `PluginConfigFile` (`src/runtime/PluginConfigFile.h`). `main.cpp` builds the path next to the SKSE log, usually `Data/SKSE/Plugins/SlaveTatsUI.json`. With Mod Organizer 2 this is often under that profile's overwrite folder, not next to the DLL. The log line `configuration path is '...'` is the file that matters.

`PluginConfigFile::update` reads the whole object, changes the keys the caller sets, and writes the object back. Other keys (favorites, loadouts, hotkey, and anything unknown) stay. A missing file is an empty object, not an error.

Copy `ThumbnailCacheSetting` (`src/runtime/ThumbnailCacheSetting.h`) for the new integer:

- `load()` starts from the default
- a missing key is success and keeps the default
- a present but illegal value returns false and keeps the default
- `select()` refuses an illegal value and does not write
- a failed write leaves the previous in-memory value alone

`main.cpp` already logs a warning when `load()` fails for the other settings. Do the same here.

### Where the words on screen come from

Player-facing sentences live in:

- `resources/translations/en.tsv`
- `resources/translations/ja.tsv`
- `resources/translations/ko.tsv`
- `resources/translations/zh-CN.tsv`

They are loaded from `Data/SKSE/Plugins/SlaveTatsUI/translations` by `TranslationCatalog`. A line is `English source`, then a tab, then the translation. The English sentence **is** the key. There is no separate id like `settings.tattoosPerRow`.

`tr("Tattoos per row")` looks up that English sentence. English itself returns the source and does not read `en.tsv`. Japanese, Korean, and Chinese fall back to the English sentence if their line is missing. Still add all four lines. A broken line (no tab, two tabs, or a `{` placeholder that does not match) makes `TranslationCatalog::load` return false, and `main.cpp` warns that translations are invalid.

`localizedUi::BeginCombo` and `SliderFloat` in `src/native/LocalizedImGui.h` translate the label and keep the English sentence as the ImGui id (`###...`). That way switching language does not reset the widget. There is no `SliderInt` wrapper yet. Add one beside `SliderFloat` if the control is a slider. A dropdown of the six numbers can use `BeginCombo` instead and needs no new wrapper.

## Phase 1 behavior

### The setting

New runtime type, suggested name `TattoosPerRowSetting`, suggested JSON key `tattoosPerRow`.

| Rule | Value |
| --- | --- |
| Type | JSON integer |
| Default when the key is absent | 4 |
| Smallest | 3 |
| Largest | 8 |
| Reject | missing file is fine; reject non-integers, `true`, `null`, `"4"`, `4.0`, and integers outside 3–8 |

Why a runtime class, not a variable in the adapter: the adapter draws. It must not read or write JSON itself. The same split is already used for the hotkey, the menu side, and the disk-cache limit.

Why the default is 4 even though today's gallery tends toward 6: nothing in the JSON records "6". The 6 comes from the formula. Players who have a config file today will see 4 the first time they run Phase 1, which is the density people preferred. They can move the control to 6 if they want more columns. Choosing 6 will still not look identical to 1.8, because rows become taller (next section).

Do not write the default into the file during `load()`. Writing on startup can touch a file that also holds favorites and loadouts, and the other settings only write when the player changes them.

### Card width

Stop deriving the column count from "at least 140 pixels, and at most 6".

The player's number **is** the column count, as long as the available width is a real positive number. If the width is zero, negative, or not a number, use one column and a zero size so the math cannot divide by zero.

```text
columnCount = tattoosPerRow          // already limited to 3..8
cardWidth   = availableWidth / columnCount
```

Create the table with that column count and keep `ImGuiTableFlags_SizingStretchSame`. ImGui then divides the panel across the columns. Do not call `SetColumnWidth` with a fixed width. The division above is for the row height and for tests. Inside a cell, keep using `GetContentRegionAvail()` so the picture fits the real cell, including the few pixels a border takes.

Keep the current table flags as well:

- `BordersInner` — the line between cards is the gap
- `NoPadInnerX` — no extra horizontal padding, so the simple division stays honest
- `ScrollY` — vertical scrolling only

Add `ImGuiTableFlags_NoSavedSettings`, or put the count in the table id (`"TattooCards##4"`). ImGui remembers column layout under the id `"TattooCards"`. Changing the count from 4 to 6 while that memory exists can restore the old columns and break the grid. The player does not resize these columns, so there is nothing worth saving.

### Aspect ratio

Make the **picture box** square with the card width, and let the tattoo drawing keep its own shape inside that box.

```text
thumbnailHeight = cardWidth
rowHeight       = thumbnailHeight + nameLine + verticalPadding
```

`nameLine` is the same value the browse path already calls `metadataHeight` (one text line, plus the gap between the picture and the name). `verticalPadding` is `CellPadding.y * 2`, which the browse path already computes.

Why square cards: four columns with the old 160-pixel row only adds empty space on the left and right. The tattoo stays small. A square picture box is what makes "fewer per row" mean "bigger tattoo".

Why not stretch the tattoo to fill the square: `fitCatalogThumbnail` already letterboxes. A wide tattoo stays wide. A tall one stays tall. Stretching would distort the design.

The name stays under the picture, one line, shortened if the card is narrow. Badges stay inside the picture and already disappear when they do not fit (`calculateCatalogMaterialBadgeLayouts`). On 8 columns they will drop off sooner. That is acceptable.

Very narrow panel: the panel can be 360 pixels wide. Eight columns is then about 45 pixels each. Names will not fit and the cards will be hard to click. Phase 1 should still honor 8, because a silent change to fewer columns looks like a broken setting. The manual test covers this. A minimum card width can be a follow-up if that case is unusable.

Very tall card: 4K with 3 columns is on the order of 600 pixels square. Several rows still fit in a 4K-tall panel. No extra cap is proposed for Phase 1. If a short window makes one row taller than the grid, the existing vertical scroll still moves it; check that in the manual test.

### Spacing and scrolling

- No new gap value. The inner border is the spacing.
- No horizontal scrollbar. Columns shrink to the panel.
- `ImGuiListClipper` gets the new `rowHeight`.
- `catalogVisibleCardRange` gets the same `rowHeight` and the same column count.
- Changing a filter already jumps back to the top (`scrollRevision`). Keep that.
- When the saved count changes while the browser is open, set scroll to 0 once. Scroll is stored in pixels. A taller row would otherwise land on a different tattoo.
- The launcher and the browser can both be open. `renderFoundation` must read the setting every frame, the same way it reads the thumbnail background. Pass a `TattoosPerRowSetting*` into `renderFoundation` and `renderLauncher` from `main.cpp`. Do not use a new global.

Changing the count does not rebuild thumbnails. Phase 1 must not call the cache builder. That is what keeps this setting from causing a hitch.

### Backward compatibility

| Existing `SlaveTatsUI.json` | Result |
| --- | --- |
| No `tattoosPerRow` key | Use 4. Leave the file unchanged until the player moves the control. |
| `"tattoosPerRow": 4` (or any integer 3–8) | Use that integer. |
| Illegal value | Use 4, return false, log a warning. Do not delete other keys. The bad value stays until the player picks a legal one, which overwrites only this key. |
| Other keys (favorites, loadouts, hotkey, `menuSide`, `thumbnailDiskCacheLimitMiB`, ...) | Untouched. |

This matches `ThumbnailCacheSetting`: a bad value does not wipe the file, and it does not throw.

Players updating from 1.8 will see four columns instead of about six even though their file has no column count. That is the intended default, not a migration bug. Say so in the release notes when Phase 1 ships.

### New sentences

Add these English source lines to all four `.tsv` files. Translate the Japanese, Korean, and Chinese columns. The English column repeats the source, which is what `en.tsv` already does.

| English source (the key) | Where it is used |
| --- | --- |
| `Tattoos per row` | Slider or dropdown label |
| `Could not save the tattoos per row setting.` | Shown if `select()` fails, same pattern as the hotkey and the browser position |

Optional third line, recommended so the softness is not a surprise:

`Fewer tattoos per row makes each picture larger. Pictures are still stored at 192 pixels, so very large cards can look soft.`

Do not put `##` in these sentences. The translation loader rejects that.

Put the control in `renderLauncher` with the other browser preferences, after the Browser position combo and before the camera sliders. The player should see it before Open Tattoo Browser.

### Phase 1 steps

Do these in order. After each step the project should still build. Tests that describe the old "up to six columns" rule will fail once the formula changes; update them in the same step that changes the formula, not later.

1. **Add `TattoosPerRowSetting`.** New files `src/runtime/TattoosPerRowSetting.h` and `.cpp`, modeled on `ThumbnailCacheSetting`. Why: the number needs one place that validates and saves it. The UI should not parse JSON.

2. **Register the `.cpp` in `CMakeLists.txt` `sources`.** Why: CMake does not pick up a new file by itself. Without this line the DLL will not contain the class.

3. **Construct and `load()` it in `main.cpp` next to the other settings.** Pass the pointer into `renderLauncher` and `renderFoundation`. Why: the launcher and the browser are two different callbacks. A local variable in one is invisible to the other.

4. **Add the control in `renderLauncher`.** If it is a slider, add `STUI_LOCALIZED_WIDGET(SliderInt)` next to `SliderFloat` in `LocalizedImGui.h`. Save on change. Show the failure sentence when `select()` returns false. Why: this is the screen players already use for settings. A slider of 3–8 matches "an integer in a range". A dropdown of the six numbers is also fine and needs no new wrapper.

5. **Replace the layout math.** Add a pure function beside `catalogColumnCount`, for example `catalogGridMetrics(availableWidth, tattoosPerRow, nameLine, verticalPadding)`, returning column count, card width, thumbnail height, and row height. Use it in the browse path instead of the hard-coded `160` and instead of `catalogColumnCount(width)`. Why: the tests can call a pure function without opening ImGui. The adapter file is large and its tests need a fake menu framework; keep the new math as easy to call as `catalogColumnCount` is today.

6. **Feed that result to the table, the clipper, and `catalogVisibleCardRange`.** Add `NoSavedSettings` (or a count-specific table id). Reset scroll when the count changes. Why: those three must agree, or scrolling loads the wrong pictures.

7. **Update `thumbnailGridSupportsSixCardsPerRow`.** A 900-pixel width no longer means six columns. Assert the player's count, the divided width, and the square row height. Keep `galleryRequestsOnlyNearbyRows`: `catalogVisibleCardRange` still takes the column count as an argument, so that test can keep passing 6. Why: the old test would fail for a good reason. Leaving it red hides new mistakes.

8. **Add `TattoosPerRowSettingTests`, wired like `ThumbnailCacheSettingTests`.** Cover: missing key → 4; save and reload 3, 4, and 8; reject 2, 9, 0, floats, strings, and a broken file; a failed save keeps the previous value; other JSON keys survive; a null config stays at 4 and cannot save. Why: this is the same contract the cache-limit tests already guard, and this file shares the disk with favorites and loadouts.

9. **Add the four translation lines.** Why: Korean, Japanese, and Chinese would otherwise show English. A mismatched placeholder would mark the whole translation load as failed.

10. **Run the adapter tests and the new setting tests in Debug, then the repo's usual Debug and Release test runs before asking to merge.** `git diff --check` the diff. Why: the layout change is in a file with a lot of UI tests. Running only the new test would miss a broken call to `catalogColumnCount`.

### Phase 1 manual test

Close and reopen the Tattoo Browser after each resolution change. The panel size is applied when the window appears.

| Check | Pass looks like |
| --- | --- |
| 1080p, 1440p, and 4K | The panel stays on one side. Cards stay inside it. No sideways scrollbar. |
| 3, 4, 6, and 8 per row at each of those resolutions | The row really has that many cards. The pictures get bigger as the count goes down, not just wider with empty sides. |
| Default on a config that has no `tattoosPerRow` | 4 cards. The JSON file gains the key only after the control is moved. |
| Set 6, restart the game | Still 6. Favorites, loadouts, hotkey, and the disk-cache limit are unchanged. |
| Put `"tattoosPerRow": 99` in the JSON, start the game | Browser uses 4. The log warns. Other keys remain. |
| Scroll a large pack at 3 and at 8 | The wheel moves by whole rows. Pictures load as they approach, not all at once. Filter changes return to the top. |
| Change the count while the browser is open | The grid updates on the next frame and starts at the top. |
| Narrow the game window, or use 8 on the smallest panel | Cards get very small but the count stays 8. Clicking a card still selects it. |
| Current Slots | Still four columns. This setting does not change that screen. |
| Look at a large card | It may look soft. That is the 192-pixel picture being stretched. Do not change the cache during this test. |

## Phase 2 — thumbnail resolution and cache size

**Status: direction decided on 2026-11-26. Do not implement until the owner says start.** The decisions are in the [Handoff](#handoff-for-the-local-development-machine). This section is the investigation those decisions sit on. Where it still says "recommendation, not a decision", the handoff wins.

Phase 1 answers "how many cards". Phase 2 answers "how many pixels are stored for each card, and how much disk and GPU memory that costs". They have to be chosen together. A bigger card with a 192-pixel picture looks soft. A sharper picture of every tattoo can miss the disk limit, and scrolling then has to build pictures on the fly. That is the stutter we have to avoid.

Nothing in this section is approved. The tables are so the owner can pick. They use a library of about 1,500 tattoos, the size called out for this plan. Real packs differ. Cropped tattoos are smaller than these numbers because the cache stores the visible part, not a full square, and the longest side is what is limited.

### What the cache does today

Two caches, on purpose:

| Cache | Where | Budget today | What it holds |
| --- | --- | --- | --- |
| Disk | `SlaveTatsUI/ThumbnailCache` next to the SKSE log. Class `ThumbnailDiskCache`. | 128 / 256 / 512 / 1024 MiB, default 256. Key `thumbnailDiskCacheLimitMiB`. | A DDS file per tattoo, so the next visit skips the heavy read and resize. |
| GPU | `TextureCache` inside `D3D11NativeThumbnailSource`, created in `main.cpp`. | 64 pictures, 16 MiB of uploaded bytes, dropped after 2 minutes unused. | The picture actually on the video card. |

Startup logs `capacity=64, vramCacheBudget=16MiB, requestedDiskCacheBudget=...`. The disk limit is applied by `requestDiskCacheLimit`, which runs off the render path.

#### Where 192 pixels is hardcoded

All of these have to move together. Changing one of them and leaving the others will either shrink the picture back to 192 or refuse to show it.

| Place | What it does |
| --- | --- |
| `makeBc3Thumbnail` in `src/textures/D3D11TextureUploader.cpp` | `maxThumbnailSize = 192`. Sources already within 192 are not resized. |
| `makeGenericThumbnail` in the same file | Same 192 cap for the other DDS formats, including BC7. |
| `makeCachedThumbnailDds` | If the source is already within 192 and under 2 MiB, the original bytes are stored. Otherwise it stores the resized RGBA image. |
| `uploadDdsTexture` | If the file is wider or taller than 192 and the resize step did not run, upload fails. |
| The "already framed" check in `uploadDdsTexture` | A cached file is trusted only if it carries the tag below **and** both sides are within 192. |

The tag is the eight bytes `STUITH8` written into the DDS header (`kFramedThumbnailTag`). The disk filename is a hash that starts with the text `thumbnail-v8:` (`ThumbnailDiskCache::entryPath`). The pixel size is **not** part of that hash.

So a 512-pixel file, fed through today's uploader, is not shown at 512. `makeGenericThumbnail` sees that it is larger than 192 and scales it back down. And a new size that reused the `thumbnail-v8:` prefix would collide with the old files.

There is also a per-file cap, `kMaxEntryBytes` = 2 MiB, in `ThumbnailDiskCache`. `store` and `load` both ignore a bigger file. A square RGBA 512-pixel image is about 1 MiB plus a small header, so it fits. A square RGBA image much above 512 would be dropped on the floor with no error the player can see. Phase 2 has to raise this cap if it ever goes past that.

Generated thumbnails are uncompressed RGBA (`R8G8B8A8`), 4 bytes per pixel. That is why the size grows with the area of the picture, not with the length of one side.

#### Build, failure, and eviction today

These are the behaviors Phase 2 has to keep.

**Lazy load while browsing.** The grid asks only for the visible rows plus one row of margin (`catalogVisibleCardRange`). `NativeThumbnailController` then keeps at most 96 distinct paths (`kMaximumVisiblePaths`). `NativeThumbnailRuntime::pump` starts **one** load per call, and the browse screen calls `pump` once per frame. The work runs on `NativeThumbnailWorker` (one thread, queue of 16), which `main.cpp` turns on with `background = true`. Archive reads still go through the SKSE task interface, because those must stay on the engine's terms. Render lookups use a try-lock. If the worker holds the lock, render does not wait; it sees "not in memory yet".

**Build button.** `renderLauncher` calls `startDiskCacheBuild` with every catalog texture path (`NativeCatalogBrowserModel::diskCacheBuildPaths`). `pumpDiskCacheBuild` prepares **one** path at a time, on the same worker, and only while that launcher screen is open. The UI tells the player to keep the menu open. Build, clear, and limit changes refuse to run at the same time.

**A good disk file is reused.** `prepareDiskCache` returns immediately when the cached DDS decodes. It does not resize that tattoo again.

**A bad disk file is rebuilt once, not in a loop.** If the cached bytes do not decode, `prepareDiskCache` reads the original tattoo and writes a new file. If that also fails, the build counts a failure and moves on. It does not retry that path until the player presses Build again.

**Browsing does not retry a failed picture.** `missing` and `broken` are stored on the controller for the current catalog epoch and are not queued again (`m_negativeFailures`, `queueIfEligibleLocked`). `cancelled` is queued again. `deviceUnavailable` is retried only through `retryDeviceUnavailable`. A full worker queue is the exception to be careful about: `pump` catches the worker's "queue unavailable" error and completes the picture as `broken`, which then sticks until the epoch changes. Steady browsing only has one load in flight, so this is rare. Phase 2 must not turn a busy queue into a permanent broken image, and it must not clear the negative list every frame. Clearing it every frame would rebuild failures in a loop and stutter.

**Disk eviction is least-recently-used.** `load` updates the file's last-write time, so a picture that was just shown looks newest. `prune` sorts by that time and deletes the oldest `.dds` files. It runs when a store pushes the folder over the limit, and when the player lowers the limit. If the limit is at least 1 MiB, it does not stop at the limit. It keeps deleting down to **75%** of the limit (`maxBytes * 3 / 4`). That hysteresis avoids deleting on every single new file. Files that are never opened again, including leftovers from an old thumbnail size, are the first to go — but only after the folder is over the limit. Under the limit they sit there and still count toward `bytes()`.

**GPU eviction is also least-recently-used.** `TextureCache::get` moves a hit to the front. When the cache is over 64 pictures or over 16 MiB, it drops from the back. A single picture bigger than 16 MiB is shown and not stored. Idle pictures older than 2 minutes are dropped by `pruneExpired`, at most once a second, on the worker.

**Changing resolution is not "reuse the old file".** Old files are a different picture. Showing them at a new card size is the blur Phase 2 exists to remove. They also cannot share `thumbnail-v8:` with a larger image.

### Displayed size, and the tier that matches it

The card's pixel size in the game's own framebuffer is:

```text
displayedCardPixels = (availableContentWidth / tattoosPerRow) * dpiScale
```

`availableContentWidth` is the grid width inside the browser panel, the same width Phase 1 divides by.

`dpiScale` is **1 in this codebase**. Nothing reads Windows DPI or an ImGui DPI scale. ImGui draws in the game's backbuffer pixels. Fullscreen at the monitor's resolution already means one backbuffer pixel is one physical pixel, so multiplying by Windows DPI on top would ask for a bigger texture than the screen can show. A window that Windows then stretches is the only case where a second scale would matter. That is an open question, not something Phase 1 should guess.

The picture box from Phase 1 is square with the card, so the stored thumbnail's long side should be at least `displayedCardPixels`. Smaller than that, ImGui scales up and the card looks soft. Larger than that, the extra pixels cost memory and are scaled down.

Approximate longest card side, using the panel width as a stand-in (real cards are slightly smaller):

| Resolution | 8 per row | 6 per row | 4 per row | 3 per row |
| --- | ---: | ---: | ---: | ---: |
| 1080p | 112 | 149 | 224 | 298 |
| 1440p (typical 27 inch) | 149 | 199 | 298 | 398 |
| 4K | 224 | 298 | 447 | 596 |

A fixed 192 misses every 4-per-row case in that table. It is enough for 8-per-row at 1080p and 1440p, which is the opposite of what the larger cards need.

Suggested tiers are the ones the owner named: **192, 256, 384, 512**. Pick the smallest tier that is still greater than or equal to the card. If the card is larger than 512, 512 is the closest this list goes.

| Situation | Card | Smallest listed tier that covers it |
| --- | ---: | --- |
| 1080p, 4 per row | ~224 | 256 |
| 1440p, 4 per row (the 27-inch case) | ~298 | 384 (256 is a mild 1.16× scale-up) |
| 4K, 4 per row | ~447 | 512 |
| 4K, 3 per row | ~596 | none; 512 is still a small scale-up |

One global tier has to cover the **largest** card the player can currently ask for, not the average card. A player who can select 3 per row on a 4K window needs a tier near 512 even if they usually play at 1440p. A tier chosen only from the current window will look soft after they raise the resolution, until the cache is rebuilt for the new size.

### Memory and disk cost

Square RGBA upper bound, 4 bytes per pixel, header ignored (about 128 bytes, under 0.1%). Area grows with the square of the side, so 512 is about **7.1×** the bytes of 192 (`(512/192)²`), not 512/192.

| Tier | Bytes per square thumbnail | Versus 192 | 1,500 tattoos | Fit in 256 MiB | Fit in 512 MiB | Fit in 1024 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 192 | 144 KiB | 1× | 211 MiB | 1,820 (library fits, ~45 MiB spare) | yes | yes |
| 256 | 256 KiB | 1.8× | 375 MiB | 1,024 (library does **not** fit) | 2,048 (fits, ~137 MiB spare) | yes |
| 384 | 576 KiB | 4× | 844 MiB | 455 | 910 | 1,820 (fits, ~180 MiB spare) |
| 512 | 1 MiB | 7.1× | 1,500 MiB | 256 | 512 | 1,024 (library does **not** fit) |

Read the "fit" columns as "how many square thumbnails the cap can hold", and compare with 1,500.

The 75% prune target matters only after the folder goes **over** the cap:

- 192-pixel × 1,500 is 211 MiB, which is under the default 256 MiB, so prune does not run and all 1,500 stay.
- 256-pixel × 1,500 is 375 MiB. On a 256 MiB cap the build crosses the cap and prune keeps about 192 MiB, roughly **768** files, about half the library. On a 512 MiB cap, 375 is under the cap, so all 1,500 stay.
- 384-pixel × 1,500 fits in 1024 MiB without pruning.
- 512-pixel × 1,500 does not fit in 1024 MiB. Prune would keep about 768 MiB, roughly **768** files. Several hundred tattoos would be rebuilt the next time they scroll into view.

GPU budget at the same sizes, against the current 16 MiB and 64-picture caps:

| Tier | Bytes each | Pictures that fit in 16 MiB | What actually limits |
| --- | ---: | ---: | --- |
| 192 | 144 KiB | ~113 | the 64-picture cap (about 9 MiB used) |
| 256 | 256 KiB | 64 | both caps meet at 64 |
| 384 | 576 KiB | ~28 | the 16 MiB cap |
| 512 | 1 MiB | 16 | the 16 MiB cap |

One screen of a 4-per-row grid is on the order of 12–20 pictures, plus the one-row margin. At 192 and 256 the GPU cache can hold several screens. At 512 it holds about one screen. Fast scrolling then uploads again even when the disk file is already there. Upload is still paced at one picture per frame, so it shows up as placeholders catching up, not as "decode the whole pack on the UI thread". A 1 MiB upload is still about seven times the bandwidth of a 192-pixel upload. Phase 2 should decide whether the 16 MiB / 64 cap grows with the tier. Raising it blindly raises VRAM use while the player is browsing.

`kMaximumVisiblePaths` (96) is a separate ceiling. Eight columns and a tall 4K grid can ask for more than 96 unique pictures. The extras stay empty until they fall inside the 96. That cap is fine at 192. It should be rechecked if a screen of 512-pixel pictures is already enough to fill the GPU budget.

### Should the cache limit be chosen automatically?

**Owner decision:** yes. Set `thumbnailDiskCacheLimitMiB` from the active tier and the installed texture count. Do not ask the player to confirm a MiB number. Clear Thumbnail Cache stays. The note below is the earlier recommendation and is kept so the disk cost is still visible. It does not override the handoff. If the needed size is above 1024 MiB, step the thumbnail tier down for smoothness before extending the legal limits.

**Earlier recommendation, superseded:** do not silently rewrite `thumbnailDiskCacheLimitMiB`.

The limit is already a player setting. Applying it deletes oldest files on a worker (`setMaxBytes` → `prune`). Raising it automatically can fill a disk the player did not offer. Lowering it deletes cache files they may have spent a long build creating.

What is safe to do, if the owner wants it later:

- From the current per-row count, the current backbuffer size, and the tier that covers that card, compute the MiB needed for 1,500 square thumbnails.
- Show that number next to the existing limit control: "this size needs about N MiB for 1,500 tattoos; the current limit holds about M."
- Offer a button that selects the smallest existing limit (256 / 512 / 1024) which holds that library. The player confirms. The existing `requestDiskCacheLimit` path saves it and prunes off the render thread.
- If the need is above 1024 MiB, say so. Do not invent a new limit in the UI until the owner extends `ThumbnailCacheSetting::limitsMiB`. Today's validator only accepts those four numbers. 512-pixel × 1,500 needs about 1,500 MiB, which no current choice can hold.

Deriving the limit from per-row alone is not enough. Per-row changes the card size only together with the window width. The same "4 per row" is ~224 pixels at 1080p and ~447 pixels at 4K.

### Avoiding stutter

Phase 2 should keep the current threading and add rules for a size change. The expensive work is reading the original DDS (especially a large BC7), resizing it, and uploading. The disk cache exists so browsing does the upload only.

Do these. Do not do the opposites.

| Do | Why |
| --- | --- |
| Keep generation and upload on `NativeThumbnailWorker`, one browse load per frame. | `main.cpp` already refuses a single-threaded D3D device and otherwise renders placeholders. Moving a 512-pixel decode back onto the render callback is the stutter Phase 2 is trying not to reintroduce. The archive noted that scrolling still hitching after a finished disk build is why this worker exists. |
| Keep the launcher build at one tattoo per pump, and tell the player to let it finish. | A tier change makes every old file a miss. Building them while they scroll is a long series of source decodes. The existing Build button is the background path. Do not start 1,500 decodes because the slider moved. |
| On a tier change, change the cache key (`thumbnail-v8:` and the `STUITH8` tag, or include the tier in both). | Otherwise the loader will find the old 192-pixel file and either show it soft or scale it down again. |
| Leave old files for LRU, or schedule one clear through `requestDiskCacheClear` (already off the render thread). Do not delete from the browse callback. | Old files still count toward the byte budget. If the folder is under the limit they are never pruned, and the new larger files can be the ones that get deleted once the cap is hit. A scheduled clear is the predictable option. LRU-only is acceptable if the player is told the first session will rebuild. |
| Treat a miss as one worker job. Keep `missing` / `broken` sticky for the epoch. | Rebuilding a failed tattoo every frame, on the thread that is drawing, is the failure mode to avoid. The controller already does the sticky part. |
| If the worker queue is full, complete as `cancelled` (retried) rather than `broken` (sticky). | Today `pump` marks a rejected submit as `broken`. That is rare with one in-flight load. A larger tier plus a build running at the same time makes a full queue more plausible. A busy queue is not a corrupt tattoo. |
| Do not hold `m_mutex` for the whole source decode if the tier gets more expensive. | `load` takes that lock before `makeCachedThumbnailDds`. Render uses try-lock, so it will not freeze, but every lookup fails until the decode ends. At 192 this has been acceptable. At a larger output the source decode is still the slow part (the original tattoo can be up to 16 million pixels; bigger than that is refused and shows no thumbnail, which the owner already accepted). Phase 2 should measure before splitting the lock. Do not split it "just in case" in the same change as the tier. |
| Pace GPU upload. Revisit the 16 MiB cap if a screen of cards does not fit. | Disk hits still upload. Sixteen 512-pixel pictures fill the whole GPU budget. The rest of the visible grid re-uploads as the player scrolls. |

What not to do: do not rebuild the library synchronously inside `renderFoundation`. Do not delete the cache on the render thread. Do not reuse a smaller DDS as if it were the new tier. Do not bump only one of the 192 constants.

Current Slots and external-slot thumbnails go through this same source. A higher tier makes those pictures sharper too, and they spend the same disk and GPU budgets. Phase 2 is not "the browser only", even though Phase 1's column count is.

### Phase 2 questions, with the owner's answers

Answered on 2026-11-26. The handoff is the source of truth. The old recommendation text under each number stays as design notes.

1. **Which card is the thumbnail supposed to match?** Owner: the card on the resolution the game is rendering now, and the player can still override the tier. The largest possible card (3 per row on 4K, about 600 pixels) is not the stored size for everyone. Detail: 4K with 3 per row is about 600 pixels, which this tier list cannot fully cover. 1440p with 4 per row is about 300 pixels, so 384. Match the current backbuffer and the current per-row count, and rebuild when either changes, instead of always storing 512.

2. **Is `dpiScale` ever anything but 1?** Recommendation: match backbuffer pixels only, unless a real setup is found where the swapchain is stretched onto a higher-DPI panel and the cards look soft even at a matching tier.

3. **One tier for the whole library, or several sizes on disk?** Recommendation: one tier. Two sizes would double the key space and the prune logic. The player does not browse at two resolutions at once.

4. **May Phase 2 raise the disk limit by itself?** Owner: yes. Set it automatically from the tier and the library size. Clear Thumbnail Cache is the player's reset. If the result is above 1024 MiB, step the tier down so scrolling stays smooth before extending `ThumbnailCacheSetting::limitsMiB`. The old note was to show the estimate and wait for a click. That confirm step is not what the owner asked for.

5. **Is 1,500 the library to plan for?** Recommendation: measure the owner's catalog (`diskCacheBuildPaths` size, and the real bytes of a 192-pixel cache folder) before locking a tier. Cropped tattoos will be under the square upper bound. A 3,000-tattoo list does not fit the 384-pixel row of the table inside 1024 MiB.

6. **On a tier or resolution change, clear the folder or let LRU delete old files?** Recommendation: schedule the existing clear, then the existing background build, both off the render thread. Tell the player the first browse after a size change is the slow one if they skip the build. Do not reuse the old bytes.

7. **Does the GPU budget grow with the tier?** Recommendation: keep 64 / 16 MiB for 192 and 256. Revisit before shipping 384 or 512, because 512 only keeps about 16 pictures and a single screen may not fit. Do not raise it in the same unmeasured step as the disk tier.

8. **Keep writing uncompressed RGBA?** Recommendation: yes for the first cut of Phase 2. BC compression would shrink a 512-pixel file from about 1 MiB toward 256 KiB, which would let 1,500 of them fit in 512 MiB, but encoding is its own CPU cost and the current pipeline is RGBA so that alpha-weighted color and the framed header stay simple. Do not switch formats while also changing the size.

9. **Is a screen full of placeholders during the first scroll after a tier change acceptable**, as long as the frame time stays even? Recommendation: yes, if the build button is the path to a warm cache and scrolling never does more than one worker job per frame. No, if a miss is allowed to decode on the render thread.

10. **Confirm Current Slots may get the sharper thumbnails too.** They share the cache. Recommendation: yes. A second, smaller cache just for slots would miss the disk budget and complicate eviction.

## Phase 1 risks

- **Soft pictures.** Bigger cards stretch a 192-pixel image. Phase 1 ships that on purpose. The optional launcher sentence warns about it. Phase 2 is the fix, later.
- **The default changes the look for everyone.** Existing files do not store a column count, so everyone gets 4 until they move the control. Players who liked the old density pick 6, and even then the rows are taller than in 1.8.
- **Eight columns on a 360-pixel panel** makes cards about 45 pixels wide. The setting is honored anyway so it does not look ignored.
- **ImGui table memory** under the id `TattooCards` can fight a new column count. `NoSavedSettings` or a new id avoids that.
- **Scroll position is in pixels.** Changing the count or the window size changes which tattoo sits at the top. Resetting scroll on a count change is the predictable behavior. A resolution change already wants the browser reopened because the panel size is applied when the window appears.
- **The footer must stay on screen.** `calculatePinnedFooterY` already pins it. A taller row must not push Close below the window. The manual test is the check. There is no unit test that opens the real window.
- **This file is the plan the owner asked to review.** The repo's usual active plans live under `docs/superpowers/plans/active/`. This draft does not move or edit those, and it does not edit `ROADMAP.md`. Phase 1 does not change the specs that still describe a six-column, 192-pixel gallery. Those specs describe the code until an implementation is accepted.

## Phase 1 open questions

1. Is the range exactly 3–8, and is 4 the default? This plan assumes yes.
2. Square picture boxes, as written, or keep the 160-pixel row and only change the column count? The second option does not make the tattoo larger. This plan assumes square boxes.
3. Honor 8 columns even when the card would be tiny? This plan assumes yes.
4. Should Current Slots ever use the same count? This plan leaves that screen at four columns.
5. Show the one-line softness warning in the launcher? This plan recommends yes.
