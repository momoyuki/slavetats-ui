# Nearby Actor Targeting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace live Crosshair Target resolution with a one-time nearest-first nearby Actor snapshot containing at most ten user-selectable entries.

**Architecture:** `NearbyActorProvider` scans CommonLibSSE-NG's loaded Actor process list on the game thread and returns copied value candidates. The workflow model owns the session snapshot and selected Actor; the adapter renders selection intents and the existing service/runtime continues to own every tattoo query and mutation.

**Tech Stack:** C++23, CommonLibSSE-NG v11.0.0, SKSE task scheduler, SKSE Menu Framework, CTest.

**Spec:** `docs/superpowers/specs/2026-10-14-nearby-actor-targeting-design.md`

## Global Constraints

- Scan only after a menu session opens; never scan from a render callback.
- Return at most 10 unique non-Player Actors within 2048 Skyrim units.
- Sort by ascending distance, then ascending form ID.
- Copy display name, form ID, and distance; retain no raw Actor pointer.
- Keep SlaveTatsNG authoritative for tattoo state and mutations.
- Never silently fall back to Player after a nearby Actor failure.
- Preserve target-generation and stale-handle validation.

## Review Focus

- Duplicate Actor handles across ProcessLists must produce one candidate.
- Exactly ten candidates must be returned, with the nearest ten retained.
- Equal-distance candidates must have deterministic form-ID ordering.
- A stale scan completion must not replace a newer session snapshot.
- An unloaded selected Actor must fail explicitly without querying Player.

### Task 1: Nearby Actor provider

**Files:**
- Create: `src/native/NearbyActorProvider.h`
- Create: `src/native/NearbyActorProvider.cpp`
- Create: `tests/native/NearbyActorProviderTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `NearbyActorCandidate`, `NearbyActorScanResult`,
  `scanNearbyActors(const NearbyActorProviderBindings&)`, and the production
  `scanNearbyActors()` overload.

- [x] Write failing pure-provider tests for filtering, distance ordering,
  form-ID tie-break, duplicate removal, and the ten-entry cap.
- [x] Run `ctest --test-dir build/debug -R NearbyActorProviderTests` and verify
  the new target fails before the implementation exists.
- [x] Implement production discovery through `RE::ProcessLists::ForAllActors`,
  Player position, loaded-3D checks, and value-only normalization.
- [x] Run the focused provider test in Debug and verify it passes. Release remains
  part of Task 4 verification.

### Task 2: Workflow scan lifecycle

**Files:**
- Modify: `src/native/ActorTarget.h`
- Modify: `src/native/NativeSlotWorkflowModel.h/.cpp`
- Modify: `src/native/NativeSlotWorkflowRuntime.h/.cpp`
- Modify: `src/main.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`

**Interfaces:**
- Replaces Crosshair target operations with `NearbyActorScanOperation` and
  model methods `takeNearbyActorScanRequest`, `completeNearbyActorScan`,
  `nearbyActors`, and `selectNearbyActor(std::size_t)`.

- [x] Add failing tests proving reset requests one scan, render-independent
  state has no repeated scans, and stale scan completions are ignored.
- [x] Add failing tests proving selected candidate form IDs reach slot queries
  and target changes invalidate all actor-scoped state.
- [x] Implement generation-tagged scan ticket delivery and session snapshot
  storage with Player remaining the default target.
- [x] Compose the provider into `main.cpp` without adding runtime/storage calls
  to the UI adapter.
- [x] Run focused native model/runtime tests and verify they pass.
- [x] Preserve a failed discovery as a retryable state without queuing scans
  during render.

### Task 3: Target selector presentation

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h/.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Replace Crosshair radio/refresh presentation with deterministic nearby-list
  helpers and a selection intent that calls the workflow model.

- [x] Add failing adapter helper tests for scanning/empty status, populated
  selector labels, selected-target presentation, and mutation-disabled states.
- [x] Render Player plus up to ten nearby entries with name, form ID, and
  distance; preserve existing breadcrumbs and footer controls.
- [x] Keep external slots read-only and all mutation controls disabled while a
  scan or target transition is active.
- [x] Render an explicit `Retry Nearby Scan` action only after discovery
  failure.
- [x] Run focused adapter tests and verify they pass.

### Task 4: Records and verification

**Files:**
- Modify: `ROADMAP.md`
- Modify: `docs/superpowers/specs/2026-09-17-native-actor-targeting-design.md`
- Modify: `docs/superpowers/plans/active/2026-10-14-flick-ui.md`

- [x] Record that Nearby Actor selection supersedes the Crosshair Target
  presentation and retains the actor safety invariants.
- [x] Run Debug and Release builds and complete CTest suites (with TEMP/TMP
  redirected to a writable worktree-local directory in this sandbox).
- [x] Run `git diff --check` and inspect the scoped diff for unrelated changes.
- [x] Preserve nearby-scan failures independently from Player slot-query
  completion and pause Player selection while the nearby scan is active.
- [ ] Deploy only after explicit approval and record in-game QA for Player,
  nearby list ordering/cap, selected Actor operations, unload failure, and no
  fallback.
- [ ] Show the diff and proposed Conventional Commit subject before commit.
