# Browser Performance Validation — 2026-11-07

Candidate branch: codex/performance-improvements, based on main 66bde8a.
This branch does not include the separate unmerged localization PR #23.

## Scope

Owned thumbnail/background catalog workers; scheduled engine archive reads;
render-safe cache lookup; bounded 192px DDS output; facet caching/single query;
delta SKEE property updates; inactive NPC camera refresh suppression;
unchanged configuration write suppression; routine diagnostic log reduction.

Approved feature tradeoff: remove automatic 500ms catalog Preview repaint.
Keep the separate one-second Edit Appearance preview, 192px quality, actor/slot
validation, External read-only behavior, explicit Save, Undo/Close restore and
synchronization-only retry. No actual-game acceptance is claimed here.

## Synthetic measurement

MSVC optimized /O2 /MD, one warm-up followed by 100 identical idle facet reads.
Synthetic catalogs contain 1000 sources and 200 sections. Identical checksums.

| Definitions | Original main | Candidate |
|---|---:|---:|
| 10000 | 1015.98 ms | <0.01 ms |
| 20000 | 1742.89 ms | <0.01 ms |

The candidate is at timer-resolution floor. This measures only model facet reads;
UI snapshot copying, rendering, game tasks, texture work and FPS are excluded.
No ratio or FPS uplift should be inferred.

## Automated evidence

The implementation plan records RED/GREEN regression work. Final complete MSVC
Debug/Release DLL builds passed; CTest passed 59/59 in each configuration with no
skipped tests. All five PowerShell release-tool suites passed. Independent review
and scoped re-review have no outstanding findings. A repeat optimized benchmark
run measured 784.735 ms / 1215.38 ms before (10k / 20k), versus <0.01 ms after;
CPU load/timing varies, and this remains a synthetic model-only measurement. There is no standalone lint/typecheck target in this
native C++ repository; compiler type checks and git diff --check are the available
repository checks. No skipped tests should be treated as passing acceptance.

## In-game comparison

Use the same save, position, resolution, ENB/Community Shaders and SmoothCam
settings for baseline and candidate. Record frametime rather than only average
FPS; repeat each sequence three times. Retain the current user configuration.

1. Startup: catalog eventually becomes available; opening the menu before the
   scan finishes must not freeze or show a partly constructed catalog.
2. Cold thumbnails: open Current Slots, then Browse, scroll several new rows.
   Record frame spikes and thumbnail latency. First cache use after v8 rebuilds.
3. Warm thumbnails: repeat the same rows; compare frametime with cold path.
4. Rapid scroll, search and filters: many packs, Favorites/Recent/Applied/material
   filters; return to prior rows and verify labels/results and no stuck Loading.
5. Preview: click several tattoos; edit Ink/Glow/Glossy; leave selection idle
   30 seconds, then Undo/Close. No periodic repaint is expected. An external
   overwrite may stay visible until explicit selection/edit; Save still validates.
6. Edit Appearance: edit and stop for a second; verify visual update and cancel
   restore. Confirm glow/bump transitions and final Save.
7. Camera: Left/Right Player orbit/pan/zoom and close/restore, SmoothCam handoff,
   then NPC/Player switching; no NPC camera ownership should remain.
8. Lifecycle: close/reopen during thumbnail loading, quicksave/autosave/load,
   normal quit while work is queued; no hang, crash or stale task visual updates.
9. Memory: compare process private bytes and GPU memory after repeated scrolling.
   16MiB is retained thumbnail cache budget, not a cap on all engine resources or
   temporary CPU decode storage. Single-worker DDS limits bound transient work.
10. Cache maintenance: build/clear/change limit from launcher and verify progress,
    responsiveness, eventual completion, and unrelated mod files remain intact.

The test ZIP is a local review artifact, not a public release. Do not deploy over
the PR #23 test DLL without explicitly choosing which candidate is under test.
