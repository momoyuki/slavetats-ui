# Follower Target Convenience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add an explicit follower-only shortcut over the loaded-Actor snapshot without adding target heuristics or a new runtime target kind.

**Architecture:** Extend the value-only loaded-Actor provider with the copied
teammate flag. The workflow owns picker mode and filters the existing snapshot;
the runtime and SlaveTats service remain unchanged. The adapter adds one
presentation action and reuses the existing picker.

**Tech Stack:** C++23, CommonLibSSE-NG 11.0.0, ImGuiMCP, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-16-follower-target-convenience-design.md`

## Global Constraints

- `RE::Actor::IsPlayerTeammate()` is the only follower predicate.
- Only non-Player Actors with nonzero form IDs and loaded 3D are eligible.
- The existing `explicitNpc` target kind and actor-scoped safety checks remain authoritative.
- No raw Actor pointers cross the provider boundary.

## Review Focus

- A non-teammate must never appear in follower mode.
- Search must filter the copied snapshot without scheduling another scan.
- Closing the picker must preserve the current target and slot snapshot.
- Selecting a follower must query its exact form ID and never Player.
- Existing all-NPC, nearby, and Player flows must remain unchanged.

### Task 1: Provider teammate metadata

**Files:**
- Modify: `src/native/LoadedActorProvider.h/.cpp`
- Test: `tests/native/LoadedActorProviderTests.cpp`

- [x] Add a teammate flag to `LoadedActorCandidate`.
- [x] Write tests proving normalization preserves the flag and production bindings can carry both teammate and non-teammate candidates.
- [x] Set the flag from `RE::Actor::IsPlayerTeammate()` in production enumeration.
- [x] Run focused provider tests in Debug and Release.

### Task 2: Workflow picker mode

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h/.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

- [x] Add `ActorPickerMode { all, followers }` and `chooseFollowerTarget()`.
- [x] Filter follower results from the captured snapshot without a scan per query.
- [x] Preserve close, refresh, stale completion, exact selection, and no-fallback invariants.
- [x] Add model tests for follower filtering, close preservation, and exact query identity.

### Task 3: Adapter entry point

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

- [x] Add a `Followers` button and follower-specific empty/title copy.
- [x] Reuse the existing picker rows, search, refresh, close, and retry controls.
- [x] Add a deterministic presentation assertion without direct runtime access.

### Task 4: Verification

**Files:**
- Modify: `docs/superpowers/plans/active/2026-10-16-follower-target-convenience.md`

- [x] Run Debug and Release builds.
- [x] Run complete Debug and Release CTest suites.
- [x] Run `git diff --check`.
- [ ] Show the scoped diff and proposed Conventional Commit before committing.
