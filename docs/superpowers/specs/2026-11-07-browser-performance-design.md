# Browser Performance Design

Date: 2026-11-07
Status: User authorized implementation after the source audit.
Milestone: vNext.5 performance follow-up; independent of localization PR #23.

## Intent
Reduce unnecessary render/game-thread work across the existing browser without weakening Actor Target, stale-handle, read-only External, preview rollback, or synchronization-only retry contracts. No dependencies or new user-facing settings.

## Decisions
- Cache catalog contextual facets until relevant filter/catalog changes. Reuse one query result for pagination and scroll; suppress no-op filter updates.
- Use a single owned thumbnail worker for disk/CPU preparation and GPU resource creation where D3D11 permits. Engine archive reads remain scheduled through SKSE. Publish bounded, cancellable work; never block render on disk/decoding locks. Worker lifetime must end before its source is destroyed. No detached threads.
- All supported 2D thumbnails, including fallback formats, must fit 192 pixels. Reject unsupported or excessive source allocations before full decode. The user explicitly accepted no thumbnail for sources above 16M pixels (including 8K); original tattoo application is unchanged. Preserve alpha-weighted cropping/clarity and source tattoo files. Bound decode scratch space explicitly; preserve 64-entry/16 MiB cache budgets.
- Keep preview ownership/slot validation. The user approved removing automatic 500 ms overwrite recovery on 2026-11-07; explicit selection/edits still paint, and save/restore revalidate. Coalesce edits and avoid resetting texture properties for scalar-only edits. Do not reduce correctness checks to stale cached actor state.
- Preserve per-frame camera ownership enforcement when needed, but avoid scheduling work for unsupported NPC/no-camera targets. Idle Player camera enforcement stays until measured safe to relax.
- Move cache maintenance off render/game paths where safe, keep hysteretic pruning, and avoid per-message flushing of routine diagnostics.
- Config writes remain atomic; unchanged preferences do not write again. Startup catalog discovery remains engine-safe and event-scoped. Add evidence/measurement seams rather than claiming FPS gains from unit tests.

## Threading and memory boundaries
- D3D11 device-only creation follows Microsoft guidance: https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro . No immediate-context calls are moved to workers. Single-threaded devices show placeholders.
- Reuse the existing MIT-licensed Microsoft DirectXTex dependency; no external implementation is copied and no new dependency is added. Reference: https://github.com/microsoft/DirectXTex .
- Raw DDS reads and input are capped at 128 MiB (including headers, so normal 4K RGBA is accepted). Header preflight rejects arrays/cubes/volume, more than 16M source pixels and source mip allocations above 128 MiB. BC7 conversion uses at most 64 MiB RGBA plus small output/accumulators; transient totals include the bounded input/source chain.
- Archive tasks have a finite wait. Queue delay is retryable cancellation, not a permanently broken image. Once an engine read has started, wait for that transfer instead of discarding late bytes. Expired queued tasks do not touch engine resources.
- Startup filesystem JSON scan/indexing uses an owned worker and atomically publishes the complete catalog. Menu refresh observes completion without blocking startup; initial UI can briefly show no catalog.
- Render cache lookups use try-lock; cache usage is an atomic estimate. Worker maintenance can serialize disk operations without stalling render.

## Approved feature tradeoff
The recommendation accepted by the user removes idle catalog Preview polling. An external mod overwrite is no longer automatically repainted while idle; click/select or change a draft value to repaint. Keep 192 px thumbnails and the separate one-second Edit Appearance preview. Actor/slot validation, stale rejection, Undo/Close restore and synchronization-only retry are unchanged.

## Verification
Test-first regression coverage at affected seams. Debug and Release DLL/CTest, all PowerShell release suites, diff/secret review, independent final review. In-game frametime/VRAM, SmoothCam, rapid scroll/close/load and preview restore remain explicit acceptance requirements. Do not deploy or merge without further authorization.
