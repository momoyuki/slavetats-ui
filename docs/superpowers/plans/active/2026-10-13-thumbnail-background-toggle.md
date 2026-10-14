# Thumbnail Background Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users switch the background behind tattoo thumbnails between black and white without changing tattoos or thumbnail cache entries.

**Architecture:** Keep one background choice in the native menu presentation session. Pass it through the existing screen renderers and draw a solid rectangle behind each fitted DDS image before the existing tinted image call. No workflow, service, runtime, or cache state changes.

**Tech Stack:** C++23, CommonLibSSE-NG, SKSE Menu Framework/ImGui, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-16-thumbnail-background-toggle-design.md` (implementation Draft PR #12).

## Global Constraints

- Exactly two modes: Black and White; Black is the initial mode.
- One UI-session choice survives navigation among Current Slots, Picker, Preview, and Edit Appearance.
- Reuse existing DDS textures, tint, alpha, badges, and layout; no cache invalidation.
- No SlaveTatsNG, JContainers, service, workflow mutation, or synchronization call.

## Task 1: Test and Render the Background

**Files:** `src/native/OfficialMenuFrameworkAdapter.h`, `src/native/OfficialMenuFrameworkAdapter.cpp`, `tests/native/OfficialMenuFrameworkAdapterTests.cpp`.

- [x] Add an adapter test for Black/White fill colors and a mode toggle that alternates in both directions.
- [x] Build the focused adapter target and confirm the new test fails because the API is missing.
- [x] Add `ThumbnailBackgroundMode` and a small presentation-only choice/color helper; keep the mode out of tattoo and runtime models.
- [x] Pass the selected mode through Current Slots, Slot Actions, Picker, Preview, and Edit Appearance; draw behind each ready fitted DDS image before the current image call.
- [x] Run the focused adapter test until it passes.

## Task 2: Expose the Picker Control and Verify

**Files:** `src/native/OfficialMenuFrameworkAdapter.cpp`, `tests/native/OfficialMenuFrameworkAdapterTests.cpp`.

- [x] Add a single Black/White button beside Picker filters, labeled with the current mode and using the same presentation-session mode passed to all thumbnail surfaces.
- [x] Confirm switching the mode does not call thumbnail reload, service, runtime, or synchronization paths; test the mode helper at the narrow stable seam.
- [x] Run full Debug and Release builds and CTest suites, release policy scripts, and `git diff --check`.
- [ ] Review the scoped diff and obtain explicit approval before committing or pushing.

## Task 3: In-Game Acceptance and PR

- [x] Confirm both backgrounds on dark and light tattoos, immediate change, navigation persistence, and unchanged tint/alpha, actor appearance, and thumbnail cache behavior with the original dropdown (user in-game QA, 2026-10-13).
- [ ] Confirm the single-button interaction in game after the updated DLL is deployed.
- [x] Open focused implementation Draft PR #12 and close the superseded docs-only Draft PR #9.
- [ ] Return to the active vNext.4 material-texture acceptance work in `ROADMAP.md`.
