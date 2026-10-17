# Browser Position and Camera Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users place the browser on either side and inspect the target through camera controls compatible with SmoothCam.

**Architecture:** Store the panel side in the shared plugin JSON and pass it to the Menu Framework adapter. Keep camera input in the UI, camera state and SmoothCam ownership on the game thread, and actor identity in the existing workflow.

**Tech Stack:** C++23, SKSE Menu Framework, CommonLibSSE-NG, SmoothCam optional messaging API, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-15-browser-position-and-camera-design.md`

## Global Constraints

- Preserve SlaveTatsNG as the tattoo authority and keep the Actor Target explicit.
- Do not copy SlavetatsUIPzx source or assets.
- Restore camera state and release SmoothCam ownership on every exit path.
- Do not commit without explicit user approval.

## Review Focus

- Missing SmoothCam: the vanilla third-person camera remains usable.
- SmoothCam denies ownership: do not manipulate the camera while it owns it.
- Rapid open and close: stale queued camera work cannot reopen control.
- NPC selection or target switch: camera work releases Player focus and cannot mutate a stale target.
- Changing side: all workflow screens open on the chosen side with the Actor visible.

---

### Task 1: Browser position setting

- [x] Add tests for the left default, right persistence, invalid values, and unrelated JSON preservation.
- [x] Implement `MenuSideSetting` through `PluginConfigFile` and expose it in the Menu Framework launcher.
- [x] Pass the setting to every workflow screen's layout, with geometry tests.
- [x] Run Debug and Release builds, full CTest suites, and `git diff --check`.
- [ ] Validate both sides and every launch path in Skyrim.

The user confirmed Left and Right menu positioning in-game on 2026-10-17.
Validation of every launch path remains open.

### Task 2: Camera ownership contract

- [x] Limit the first camera QA slice to the selected Player; selecting an NPC releases camera control.
- [x] Specify capture/restore fields and state transitions for open, close, refusal, and stale tasks.
- [x] Add an optional SmoothCam interface bridge using its published modder API.
- [x] Test acquire/refuse/release and version-specific return-to-goal behavior with a fake bridge.

### Task 3: Camera movement

- [x] Add bounded mouse-drag and wheel intents outside the panel through the adapter.
- [x] Execute camera changes on the game thread only; keep UI intent separate from runtime writes.
- [x] Test clamping, lifecycle, target switching, and close during queued movement.
- [x] Run Debug and Release builds and complete test suites.
- [ ] Validate with and without SmoothCam in Skyrim.

### Player framing correction (2026-10-16)

Paired user screenshots show distant Player framing while the browser owns the
camera and desirable close Player framing after NPC selection returns control
to SmoothCam. Defer acquisition until the first nonzero camera gesture, and
retain pose refresh only after acquisition. The session regression test fails
against eager acquisition; coordinator lifecycle tests explicitly start gestures.
In-game acceptance remains required: initial Player framing, first drag without
a jump, wheel zoom outside UI, gallery wheel isolation, close/reopen, and NPC
transition. Add explicit framing settings only after this comparison is resolved.

### Rendered-pose handoff and drag direction (2026-10-16)

The user reproduced a camera jump on the first drag and confirmed inverted
vertical dragging (wheel zoom had not yet been tested). Replace the vanilla
camera reconstruction with an orbit of the captured rendered transform, invert
vertical drag, and carry the menu side through the scheduler for lateral framing.
Add pure-pose continuity/direction/mirroring tests, session/coordinator side
propagation tests, and retain ownership/close/unload tests. Review the native
scene-graph integration before deployment. Actual engine continuity, side
placement and near-wall behavior remain in-game acceptance items.
The user subsequently approved a transparent camera control rectangle on the
existing game scene. Add a side-aware frame and input hit region without a new
render target or input window. Cover viewport offsets, left/right placement,
tiny layouts, and drag-start ownership; verify actual border and input in game.

### No-response diagnostic build (2026-10-16)

The user sees the frame but reports no drag/zoom response. The previous unit
suite does not reproduce engine input or rendering. Add bounded CameraTrace
logs for UI frame hit, ImGui window/item capture, mouse IO versus public API,
gesture intent, SmoothCam lease result, camera state, desired/root pose and
rendered NiCamera position. This is instrumentation, not a confirmed behavior
fix. Read the next in-game trace before changing input or camera transforms.
The Windows Documents location is D:/Documents; the current log is
My Games/Skyrim Special Edition/SKSE/SlaveTatsUI.log under that directory.

### Trace-confirmed engine overwrite correction

The 11:11 trace confirms nonzero accepted UI intent, successful SmoothCam control,
and desired NiCamera positions immediately after scene update. The next sample
repeatedly begins from the same engine position, proving an intervening writer.
Reapply after a chained PlayerCamera virtual Update (slot 2), only while active.
Capture rendered NiCamera transforms, not assumed root axes, and normalize its
parent-relative transform on every apply. The actual NiCamera-axis fixture fails
against the old root-axis math and is covered by the pose suite. Keep hook-entry
and rendered-position trace logs until in-game response is confirmed; automated
tests cannot prove that the engine dispatches this vtable slot in this build.

### Reference investigation after failed hook (2026-10-16)

The 11:32 user test still cannot rotate or zoom. CameraTrace confirms wheel and
drag intents and successful ownership, but contains no post-engine hook-entry
messages despite successful installation. The slot-2 hook approach is disproven
as an effective update seam in this session. Do not describe it as a verified fix.
The first-input framing jump also remains unresolved. Investigate upstream
SlavetatsUIPzx state-driven camera updates and SmoothCam ownership before another
implementation/deployment; retain Player-only scope and the approved input frame.

### Native state control implementation (2026-10-16)

Replace the ineffective PlayerCamera slot-2 hook and scene-transform writes with
ThirdPersonState free rotation and zoom controls. Derive the initial world
pitch/yaw from the captured state quaternion, relative to the Player heading;
keep actor heading unchanged. Snapshot every native state field written and
restore it before releasing SmoothCam. While paused, snap actual offsets/zoom
and explicitly update PlayerCamera on the game thread. Keep acquisition lazy,
Player-only input frame, UI isolation, and existing coordinator lifecycle.
Do not reflect the world camera position at acquisition: that caused a framing
jump. Side layout remains supported; seamless SmoothCam distance handoff and
side framing remain in-game checks, not guarantees from helper tests.
Verify helper regression tests, full Debug/Release suites and scoped review
before deployment. Keep bounded native-state/readback traces for the game test.

Review found that view-identity loss previously skipped restoring native controls.
Retain the captured TESCameraState through BSTSmartPointer, restore its controls
independently of current view/root identity, and keep camera target/first-person
restoration guarded. BrowserCameraControlSnapshot is exercised against a fake
native state, including distinct expected/actual offsets and target/current zoom;
replacement state remains unchanged. Reviewer rechecked and reported no further
important issues. In-game rendered continuity and actual drag/zoom remain pending.

Verification: Debug and Release builds succeeded; complete CTest suites passed
46/46 in each configuration. The pure controls/snapshot suite also passed with
MSVC /W4 /WX. git diff --check passed (pre-existing CRLF advisory only). No separate
repository lint/typecheck target is configured; C++ compilation checks types.
Dependency rebuild emitted the existing CommonLib port binary-count advisory;
installation and both builds completed successfully.

Deployed Release to the dedicated MO2 mod after confirming Skyrim/SKSE stopped.
Backup: SlaveTatsUI.dll.backup-20261016-124949.
SHA256: A8A88A7461846D393BFFC2D678EFAB65437A3728C2010E9FCBFA0F35EB33DFB8.
Built/deployed hashes match; DLL size is 3,923,456 bytes. No commit/push performed.
Awaiting in-game acceptance: first gesture continuity, horizontal/vertical drag,
wheel zoom within the frame, gallery wheel isolation, close/restore and both sides.

### 12:56 overhead-jump report

The user reproduced overhead framing after a small gesture. Trace shows a pure
horizontal input (9,0,0) at 12:56:52 requested fixed pitch -0.255, yet subsequent
idle samples moved rendered Z from -1779.65 toward -1259.74. Therefore mouse
sensitivity alone cannot explain this jump. All samples report paused=false.
The trace also proves current native zoom is negative (-0.2), while our helper
clamped it to zero at capture and wrote zero on every non-wheel gesture. This is
a confirmed unwanted input change, not proof of the complete overhead cause.

Preserve native target zoom exactly, including negative values; capture a stable
zoom lower/upper bound that includes that baseline. Only apply clamping to axes
with actual input. Use the native enabled freeRotation inputs directly, rather
than reinterpret world quaternion pitch as native free look. Keep yaw continuous
across pi. Stop copying actual offsets into expected offsets during active moves;
leave those native inputs intact. Retain saved-state restoration and paused
updates. Add bounded target/current zoom, expected/actual offset, Actor pitch,
pitchZoom and collision readback to separate remaining lease/collision/offset
handoff issues. Keep sensitivity unchanged for this diagnostic comparison.
Regression suite includes observed negative values, horizontal-only input,
idle native-angle preservation, continuous yaw and signed zoom limits.

Native-handoff-v2 verification: Debug and Release builds succeeded; complete
CTest passed 46/46 each. Pure helper/snapshot tests passed /W4 /WX. Diff check
passed with the existing unrelated CRLF advisory. Reviewer reported no further
important issues; seamless native ownership transition remains unproven.
Deployed after confirming game stopped; backup SlaveTatsUI.dll.backup-20261016-130359.
SHA256 EAD835C5AED1CD23EBB106C247A611DF3C0891642E274773933048AE295473CC
matches built and deployed DLL (3,929,088 bytes). No commit/push.
Next acceptance: a small horizontal drag followed by idle, vertical drag, wheel,
UI wheel isolation, close/restore. Read native-handoff-v2 capture values and
subsequent offset/collision traces if the view still jumps.

### 13:07 height/distance handoff follow-up

The user confirms rotation now works but the initial view changes after the first
gesture. At 13:07:30 native target/current zoom stay .025; expected/actual offsets
change from (25,-157.67,-24.86) to (30,0,-10). Upstream Camera::UpdateCamera calls
Thirdperson::OnEnd once after API ownership; OnEnd restores INI shoulder offsets.
This source path matches the observed offset replacement. Reassert captured
actual offsets into both expected/actual on active moves, keeping rotation and
zoom independent. If capture occurs during interpolation, using actual for both
prevents restarting an unfinished easing step every task. Restore distinct
original expected/actual values on close. Tests reproduce both equal-offset
observed case and unequal-offset interpolation; narrow suite passed /W4 /WX.
A transient first frame before the next scheduled reassert remains an in-game
acceptance risk; do not claim exact rendered continuity from these tests.

Side framing follow-up: the existing Browser position dropdown places the menu
and opposite control frame, but does not yet compose the camera for that side.
Recommend automatic Actor placement opposite the menu, with optional manual
framing controls later. No new setting or side mirroring in this correction.
Primary source: https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/camera.cpp
Offset cleanup: https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/thirdperson.cpp#L135-L162

V3 verification: complete Debug/Release builds and CTest passed 46/46 each;
regression helper suite also passed /W4 /WX. git diff --check passed (existing
unrelated CRLF advisory). Review closed the unequal-offset interpolation issue;
no further important findings. Deployed after confirming Skyrim/SKSE stopped.
Backup: SlaveTatsUI.dll.backup-20261016-131552.
SHA256: 8910BD4C6794055F6D980DF172BEF80400BDDDDC9A9BBD15336533A2BA1EA140.
Built/deployed hashes match; 3,929,088 bytes. No commit/push.
In-game first-gesture continuity and opposite-side framing remain acceptance
and follow-up items respectively; do not mark the camera milestone complete.

### Side-aware opening frame — 2026-10-17

The user approved placing Player in the available camera-control area opposite
the configured browser side while retaining the captured angle and distance.
Queue a one-time Whole body framing intent for the Player on target selection;
later idle refreshes only reassert the pose. A pointer gesture arriving before
the initial camera task supersedes that framing. Existing Center and Reset View
remain available for manual adjustment. Coordinator and pure-pose tests cover
both menu sides, once-only framing, and mirrored position without changing
distance or angle. In-game framing and SmoothCam handoff still need acceptance.

Ruling: The upstream SlavetatsUIPzx Setup offers independent camera offset,
zoom, FOV and rotation controls. Keep this milestone on side-aware framing and
manual Center/Reset View until the in-game left/right result is known; adding
seven persistent shot controls now would preempt that result. See
`docs/superpowers/research/2026-10-17-slavetatsuipzx-settings.md`.

The user approved deployment for in-game testing. Fresh Debug and Release
builds and full 55-test suites passed. The Release DLL was installed in the
dedicated MO2 mod after backing up the prior DLL; built and deployed SHA-256
match (`5572575DE007F8354BF5B9E2D089463B54FE209FB184CA680DAC127EEAB39273`).
Left/right visual placement passed in-game acceptance on 2026-10-17.
First-frame gesture continuity and close/restore still require in-game acceptance.
