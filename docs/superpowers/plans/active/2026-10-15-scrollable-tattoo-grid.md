# Scrollable Tattoo Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the six-item Browse page with a responsive, vertically scrolling thumbnail gallery.

**Architecture:** Keep repository filtering in `NativeCatalogBrowserModel`, expose an all-matches view for Browse, and render visible rows through the Menu Framework adapter. Keep thumbnail requests bounded to rows near the viewport. Current Slots and runtime mutation paths stay unchanged.

**Tech Stack:** C++23, SKSE Menu Framework ImGui API, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-15-scrollable-tattoo-grid-design.md`

## Global Constraints

- Preserve actor-scoped workflow and SlaveTatsNG authority.
- Do not add direct JContainers, RaceMenu, or SlaveTatsNG writes to the adapter.
- No new dependencies or copied reference-mod assets.
- Do not commit without the user's explicit approval.

## 2026-10-17 disk thumbnail cache follow-up

The user requested persistent thumbnail files for repeated long gallery scrolls
and a cache clear button in the SKSE launcher. The implementation creates
reduced DDS files for large BC3 source textures and copies other DDS files up
to 2 MiB, checks loose file or BSA
metadata before reuse, and caps disk ownership at 256 MiB. GPU cache ownership
is reduced to 16 entries / 16 MiB. Invalid cache data falls back to the source.
Tests cover disk reuse after GPU eviction, explicit clear, loose source changes,
and preserving unrelated files in the dedicated cache folder. Full in-game
scroll and cache clear acceptance remains required after build/test verification.

## Review Focus

- Thousands of matches: only near-viewport textures are requested.
- Narrow panel: cards remain clickable with fewer than six columns.
- Filter change: stale scroll position and thumbnail requests do not mislabel new results.
- Target change: In Use status follows the selected Actor.
- Missing texture: other cards remain interactive.

---

### Task 1: Catalog data for scrolling

**Files:** `src/native/NativeCatalogBrowserModel.{h,cpp}`, `tests/native/NativeCatalogBrowserModelTests.cpp`.

- [x] Write a failing test that all filtered matches are available to the gallery while the existing paged view still works.
- [x] Implement a cached all-matches query, refreshed on every filter change.
- [x] Run the model test and full relevant CTest suite.

### Task 2: Responsive visible-row layout and thumbnail window

**Files:** `src/native/OfficialMenuFrameworkAdapter.{h,cpp}`, `src/native/NativeThumbnailController.cpp`, corresponding native tests.

- [x] Write failing tests for six columns at desktop width, fewer on narrow width, and bounded visible thumbnail paths.
- [x] Implement the layout helpers and bounded thumbnail synchronization.
- [x] Run the adapter and thumbnail tests.

### Task 3: Browse renderer and verification

**Files:** `src/native/OfficialMenuFrameworkAdapter.cpp`, adapter tests, this plan.

- [x] Render the all-matches grid with vertical scrolling, visible names, and row clipping; keep selection and badges.
- [x] Replace Browse pagination controls with match count; reset scroll after filter changes.
- [x] Run Debug and Release builds, full CTest suites, and `git diff --check`.
- [x] Record in-game acceptance items for the user; do not claim game validation from unit tests.

In-game acceptance still needed: verify six columns at the usual viewport, fewer columns
on a narrow panel, continuous wheel scrolling through a large pack, long-name tooltips,
filter changes resetting the viewport, and returning from a selected tattoo without
changing the explicit Actor target. These checks cannot be performed by CTest.

### Task 4: Hover-preview design and implementation

Before hover preview, the user approved a Current Slots presentation refinement:
four columns and twelve slots per page, one Actor title, one-shot area tab
synchronization, no Add/Edit text, a red external-slot indicator with a Read Only
tooltip, and temporarily hidden Lock/Favorite icon buttons. See the FLICK UI spec.
In-game acceptance must verify all four area tabs, per-area pagination, red-dot
placement, external-slot protection, and normal card clicks after hiding icons.
The target header now has Player and Choose NPC only. The unified NPC picker
includes Followers within 2048 units; Choose NPC toggles the list open/closed.
Verify opening, closing without retargeting, nearby NPC/Follower selection,
Refresh/Search, Player return, and rejection of stale scan completions.

- [ ] Design the actor-scoped transient transaction and save/co-save protection as a separate spec and plan.
  Proposed interaction/runtime contract and primary-source research are now in
  `docs/superpowers/specs/2026-10-16-catalog-hover-preview-design.md` and
  `docs/superpowers/research/2026-10-16-catalog-hover-preview-references.md`.
  User confirmed hover previews immediately, click pins/enables Save, and
  Undo/Close cancel. The proposed full design awaits review/approval; this item
  is not marked complete and no preview implementation was deployed.
- [ ] Implement through workflow, scheduler, service, and runtime only after the safety contract is settled.
- [ ] Test rapid hover, pointer exit, close, target switch, unloaded Actor, stale completion, save/rollback, and sync-only retry.
