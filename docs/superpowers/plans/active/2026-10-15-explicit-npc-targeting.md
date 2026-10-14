# Explicit NPC Targeting Implementation Plan

> For agentic workers: execute this plan task by task and keep the architecture
> boundaries in the design intact.

**Goal:** Add a deterministic loaded-NPC picker backed by one session snapshot,
without weakening existing Player or nearby Actor safety guarantees.

**Spec:** `docs/superpowers/specs/2026-10-15-explicit-npc-targeting-design.md`

## Task 1: Loaded Actor provider

- [x] Add pure provider tests for filtering, name ordering, form-ID tie-break,
  duplicate removal, empty-name normalization, and the 64-entry cap.
- [x] Implement a value-only `LoadedActorProvider` over the process-list boundary.
- [x] Run the focused provider tests in Debug and Release.

## Task 2: Snapshot workflow state

- [x] Add snapshot, filter, picker visibility, and generation state to the model.
- [x] Add scan/refresh/selection tickets to the runtime coordinator.
- [x] Prove stale scan and query completions cannot update a newer target.
- [x] Preserve Player and nearby target behavior and no-fallback safety.

## Task 3: Picker presentation

- [x] Add pure helpers for deterministic labels, search results, and stable IDs.
- [x] Render `Choose NPC`, the captured list, name filtering, refresh, and close.
- [x] Disable target changes during scan and target transition.
- [x] Keep adapter code free of runtime, JContainers, and Actor pointer access.

## Task 4: Verification and acceptance

- [x] Run Debug/Release builds and complete both CTest suites.
- [x] Run release policy checks and `git diff --check`.
- [ ] Complete and record all seven in-game checks. The user confirmed checks
  1–4 and 7 on 2026-10-15 and accepted deferring checks 5–6 until after merge.
  Those checks cover an unloaded NPC without Player fallback and stale Refresh
  completion without overwriting a newer selection. Automated coverage exists,
  but the two in-game checks remain open.
- [x] Show the scoped diff and proposed Conventional Commit subject before commit.

## Merge record

- PR #17 merged into `main` on 2026-10-15 after Debug and Release builds,
  complete CTest suites (41/41 in each), release policy checks, and hosted CI.
- Keep this plan active until in-game checks 5–6 are reported and recorded.
