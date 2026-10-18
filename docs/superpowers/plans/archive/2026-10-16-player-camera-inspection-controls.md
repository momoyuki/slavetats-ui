# Player Camera Inspection Controls Implementation Plan

> 2026-10-18 acceptance: The user reported no issues with the first camera
> gesture on Left and Right with SmoothCam. This records the requested final
> in-game pass without asserting that every exploratory case below was rerun.

> **For agentic workers:** Use executing-plans inline, with a fresh final review.

**Goal:** Preserve opening-view continuity while adding bone-aware focus, pan,
zoom, Center/Reset and unobtrusive camera instructions.
**Architecture:** UI emits copied input/commands through the existing coordinator;
only the native game-thread port reads actor/camera geometry or owns SmoothCam.
**Tech Stack:** Existing C++23, CommonLibSSE-NG, SKSE Menu Framework, CTest.
**Spec:** docs/superpowers/specs/2026-10-15-browser-position-and-camera-design.md

## Global constraints

Player-only camera scope; never implicit Player fallback for an NPC workflow.
No tattoo runtime mutations. No new package dependency. No commit/push without
separate explicit approval. Existing worktree reused; camera test deploy approval
persists, but deploy only after verifying Skyrim/SKSE is stopped and backing up DLL.

## Review focus

- First mouse or keyboard action starts from the actual opening/rendered view.
- Text inputs, popups, dragged menu and off-frame pointer never steer camera.
- Actor heights, missing bones and invalid bounds never produce invalid pivots.
- Center differs from Reset and side layout cannot change Actor identity.
- Close/unload/state replacement releases exactly the resources owned by session.

### Task 1: Focus geometry and input contracts

Files: src/native/BrowserCameraPose.{h,cpp},
src/native/OfficialMenuFrameworkAdapter.{h,cpp}, corresponding existing tests.
- [x] Add failing tests for bone selection and bounds fallbacks across scales.
- [x] Implement pure focus selection, finite geometry validation and no fixed height.
- [x] Add typed motion/commands: orbit, pan, zoom, focus/Center and Reset.
- [x] Test Alt gesture/keyboard routing, UI exclusion and compact hint selection.

### Task 2: Coalesced command lifecycle

Files: BrowserCameraSession.{h,cpp}, BrowserCameraCoordinator.{h,cpp}, tests.
- [x] Add failing tests for opening-view capture without ownership, pan coalescing,
  reset/focus barriers, target switch and late queued close.
- [x] Carry typed motion through coordinator/session; retain latest command and
  clear pending gestures at target/side/close boundaries.
- [x] Keep idle open read-only until a real gesture/explicit camera command.

### Task 3: Native rendered-pose handoff and camera control

Files: SkyrimBrowserCameraPort.{h,cpp}, BrowserCameraPose tests, source research.
- [x] Verify a supported native camera update path from primary source before
  replacing the insufficient third-person-offset handoff.
- [x] Seed native control from actual rendered pose; do not rebuild a different
  vanilla distance. Preserve opening pose separately for Reset.
- [x] Resolve skeleton landmarks on the game thread and pan/zoom around focus.
- [x] Restore captured camera state/ownership on all exits; reject foreign state.
- [x] Keep bounded trace data to validate actual engine pose and remaining mod
  interactions. Do not claim rendered continuity from helper tests.

### Task 4: UI and verification

Files: OfficialMenuFrameworkAdapter.cpp, src/main.cpp, adapter tests, this plan.
- [x] Add compact focus/Center/Reset controls and small contextual instructions.
- [x] Wire all commands through coordinator; no engine reads in render callback.
- [x] Run full Debug/Release builds and CTest plus diff check and final review.
- [x] Deploy verified Release with backup/hash if game stopped; record candidate.
- [ ] In-game acceptance: initial/first move, Alt-pan, keyboard isolation, zoom,
  both menu sides, each focus preset, varying actor scale, Reset and close.

## Ledger

User approved the interaction in chat. Catalog hover-preview design remains a
separate request; this approval concerns the camera recommendations. No camera
mod is yet identified as the cause. Native third-person offset/zoom preservation
alone did not retain rendered framing in the user's 13:27/13:28 test.


### Candidate deployed 2026-10-16 13:59 +07

Native FreeCamera now uses rendered center-ray orientation and actual world position.
Fresh review found a replacement-view restore guard gap; the guard now checks
state, target, root and rendered camera before changing active state. Debug and
Release builds succeeded; CTest passed 46/46 in each configuration. Diff check
passed (existing line-ending advisory only). No repository lint/typecheck command
is configured separately from C++ compilation. No commit or push was made.

Game processes were absent before deployment. Release DLL copied into MO2 after
backup SlaveTatsUI.dll.backup-20261016-135903; size 3,946,496 bytes; SHA256
EED4965B6BF8989A8F4E23ADD9A674FC7F8413F4C085CF0C393688F21A110227.

In-game continuity, native input suppression, near-wall behavior, both menu sides,
focus presets and close/Reset remain pending user acceptance. Unit tests do not
prove rendering continuity or compatibility with installed camera mods.
