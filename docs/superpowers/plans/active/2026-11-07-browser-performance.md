# Browser Performance Implementation Plan

Design: docs/superpowers/specs/2026-11-07-browser-performance-design.md
User authorized implementing the audit findings and opening a PR. No commit/push until scoped diff/message approval per AGENTS.md.

## Tasks
1. Catalog: NativeCatalogBrowserModel and tests. Cache facets by actual query changes, no-op setters, derive paged view from scroll query. Verify invalidation/favorites/recent/material filters.
2. Bounded DDS: D3D11TextureUploader/DdsDecoder and tests. Header limits before decode, generic 2D fallback downsample, bounded BC7 scratch and cache version. Verify BC3/BC7/RGBA, unsupported images, transparent content, small textures.
3. Thumbnail scheduling: worker ownership, cancellable queued engine archive read, nonblocking render/cache metadata access and tests. Do not call BSResource on the worker. Cache maintenance uses worker. Verify shutdown/reset/cancel/single-inflight/stale completion.
4. Preview/camera/config/logging: preserve full validation on explicit actions; remove approved 500 ms idle overwrite recovery; delta material writes, skip no-camera target tasks, deduplicate unchanged settings and routine diagnostics. Test scalar-vs-texture edits, retries, camera close/target/lifecycle.
5. Integration: document roadmap and specs, benchmark/measurement procedure, Debug/Release and release-tool tests sequentially; independent review and fix findings. Show diff/proposed commit, request explicit commit/push authorization, then open focused PR after approval.

## Ledger
- Base: main 66bde8a. Localization PR #23 remains unchanged.
- Scope ruling: optimize existing paths without speculative replacement of engine/synchronization behavior. Raw FPS gains require game measurements.

- Tasks 1-4 implemented with failing regressions observed before fixes. Full integration validation running.
- Review fixes: stop predicate under worker mutex (lost wake); transient archive timeout remains retryable; per-frame UI owns facet strings through filter mutation; async scan exceptions complete with error.
- Ruling: raw DDS input cap is 128 MiB, not 64 MiB, to include headers/mips for normal 4K RGBA; decoded source-chain cap remains 128 MiB. One worker bounds simultaneous scratch allocations.
- User accepted recommended tradeoff: remove 500 ms automatic Preview recovery only; preserve 192px thumbnails and Edit Appearance live preview.
- Synthetic /O2 catalog benchmark (100 warmed idle calls, 1000 sources/200 sections): 10k definitions 1015.98ms before / <0.01ms after; 20k 1742.89ms before / <0.01ms after. Checksums match, timer-resolution floor; not an FPS result.

- User explicitly accepted no thumbnails above 16M pixels (for example 8K); actual tattoo Apply is unchanged.
- Final review found manual same-card recovery needed after removing timer; explicit reselect requests repaint, retaining idempotent material writes. Regression and integration recheck pending.

## Final validation — 2026-11-07

- Latest frozen source: MSVC Debug and Release DLL builds passed. CTest 59/59 each, no skipped tests.
- PowerShell CommonLibDependency, PackageRelease, ReleaseGuards, ReleaseMetadata and WorkflowPolicy suites: 5/5 passed.
- git diff --check passed. Native compiler is the type-check gate; no standalone linter target exists.
- Final independent review P2 same-card recovery fixed RED/GREEN and scoped re-review accepted; no remaining actionable finding.
- Implementation tasks 1-4 complete. Task5 awaiting explicit commit/push approval per AGENTS.md, PR creation and in-game acceptance. No deployment, merge or public release performed.
