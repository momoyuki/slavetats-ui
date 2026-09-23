# Native Live Actor Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preview supported appearance edits on the selected loaded Actor after 1,000 ms of inactivity, while Save commits without duplicate writes and Cancel or Close restores the original appearance before leaving.

**Architecture:** Extend the existing copyable Edit Appearance session into a deterministic preview transaction. The workflow model owns debounce eligibility, latest-wins coalescing, operation purpose, retry mode, and exit intent; the runtime coordinator injects a monotonic clock and schedules the existing typed appearance operation; the adapter only renders state and sends intent.

**Tech Stack:** C++23, `std::chrono::steady_clock`, CommonLibSSE NG, SKSE task interface, Dear ImGui through SKSE Menu Framework, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-20-native-live-actor-preview-design.md`

## Global Constraints

- Debounce is exactly 1,000 milliseconds from the latest observed local edit.
- Preview supports diffuse color, alpha, glow color, emissive multiplier, glossiness, and specular strength.
- `glowTexture` and `bump` remain read-only metadata.
- At most one appearance operation may be in flight; later edits are latest-wins.
- Save flushes the latest pending value immediately and never rewrites a value already previewed successfully.
- Cancel and Close restore the exact original appearance before leaving when any preview write succeeded.
- A synchronization-only retry never repeats a completed appearance write.
- Every operation retains exact Actor form ID, target generation, area, slot, runtime handle, and texture identity.
- Never fall back to Player, retain Actor pointers, automatically load an Actor, or bypass the scheduler.
- External slots remain read-only and stale handles remain rejected before writes.
- Do not commit, push, or deploy without explicit approval; deployment requires fresh approval after the final feature commit.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/native/NativeSlotWorkflowModel.*` | Preview transaction, debounce state, latest-wins, Save/Cancel/Close intent, stale completion handling. |
| `src/native/NativeSlotWorkflowRuntime.*` | Injected monotonic clock and scheduled execution through the existing appearance operation. |
| `src/native/OfficialMenuFrameworkAdapter.*` | Status/retry presentation and intent-only Edit Appearance controls. |
| `src/native/NativeMenu.*` | Existing close boundary; no preview state or runtime access. |
| `src/main.cpp` | Production steady-clock composition and deferred-close callback composition. |
| `tests/native/*` | Deterministic model, coordinator, menu-lifecycle, and adapter regressions. |

## Task 1: Add the deterministic Live Preview transaction to the workflow model

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Produce `enum class AppearanceOperationPurpose { preview, commit, restore };`.
- Produce `enum class LivePreviewStatus { clean, pending, updating, applied, restoring, previewError, restoreError };`.
- Extend `SlotAppearanceTicket` with `AppearanceOperationPurpose purpose`.
- Produce `void advanceLivePreview(std::chrono::steady_clock::time_point now)`.
- Produce `bool requestEditAppearanceClose()` and `bool takeMenuCloseRequest()`.
- Produce `bool retryLivePreviewOperation()`.
- Produce `LivePreviewStatus livePreviewStatus() const noexcept`.
- Preserve existing `confirmAppearanceUpdate()`, `cancelEditAppearance()`, and `completeAppearanceUpdate(...)` entry points.

- [ ] **Step 1: Write failing debounce and latest-wins tests**

Create a loaded Player edit session, change every editable field, and drive an injected logical time:

```cpp
const auto startedAt = std::chrono::steady_clock::time_point{};
model.advanceLivePreview(startedAt);
model.setEditedAppearance(0x112233, 0.5F, 0x445566, 0.25F, 0.75F, 2.0F);
model.advanceLivePreview(startedAt + 999ms);
expect(!model.takeAppearanceRequest(), "expected no preview before 1000ms");
model.advanceLivePreview(startedAt + 1000ms);
const auto preview = model.takeAppearanceRequest();
expect(preview && preview->purpose == AppearanceOperationPurpose::preview,
    "expected preview at exact debounce deadline");
```

While that ticket is active, edit twice and assert no second ticket exists. Complete the first preview, advance from the last edit time, and assert only the newest appearance is emitted. Assert identical edits do not create another ticket.

- [ ] **Step 2: Run the focused model target and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowModelTests --parallel 1'
```

Expected: compilation fails because preview purpose, status, and clock advancement do not exist.

- [ ] **Step 3: Implement copyable preview transaction state**

Extend `AppearanceEditSession` with copied transaction fields: optional `lastPreviewed`, edit revision, observed revision, optional latest-edit time, status, exit intent, active purpose, and retry mode. Keep time observation deterministic: `setEditedAppearance` increments the revision; `advanceLivePreview(now)` observes a new revision and starts/restarts the one-second deadline at `now`.

Queue a preview only when the revision is unchanged for at least 1,000 ms, the edited value differs from `lastPreviewed`, the Actor/snapshot identity remains valid, and no appearance operation is pending or active.

- [ ] **Step 4: Write failing Save, Cancel, Close, and retry tests**

Cover these transitions:

- Save with a pending preview emits an immediate `commit` ticket.
- Save after a successful identical preview emits no second write and schedules Slot Snapshot refresh.
- Cancel before a successful preview exits immediately.
- Cancel after a successful preview emits `restore` with the exact original values.
- Close after preview emits restore, does not expose a menu-close request until restore succeeds, then returns one consumable close request.
- Cancel/Close during an in-flight preview waits; failed preview write exits safely, while success or synchronization failure proceeds to restore.
- Preview/restore synchronization failure retries with `synchronizeOnly` and the same purpose.
- Actor-not-found during a synchronization-only retry preserves synchronization-only mode.
- stale completion cannot change the current session or close request.

- [ ] **Step 5: Implement transaction-aware Save, Cancel, Close, and completion handling**

Keep one private queue helper that copies exact session identity and appearance into a `SlotAppearanceTicket`. Record operation purpose before handing the ticket to runtime. On completion, use the recorded purpose rather than the latest error code to decide the next transition. Preserve synchronization-only mode after any later error until that logical operation succeeds.

Block Actor selection, area navigation, slot navigation, and other mutations whenever the transaction has pending, active, commit, restore, or deferred-close work.

- [ ] **Step 6: Run focused GREEN verification**

Build and run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowModelTests --parallel 1 && .\build\debug\NativeSlotWorkflowModelTests.exe'
```

Expected: all existing and new model cases pass with no warnings.

- [ ] **Step 7: Review Task 1 and request commit approval**

Run `git diff --check`; review only the three Task 1 files. Proposed commit: `feat: add live appearance preview transaction`.

## Task 2: Drive debounce and appearance work through the runtime coordinator

**Files:**
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Modify: `src/main.cpp`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp` to pass a deterministic clock in runtime fixtures

**Interfaces:**
- Consume `advanceLivePreview(time_point)` and purpose-tagged appearance tickets from Task 1.
- Produce `using LivePreviewClock = std::function<std::chrono::steady_clock::time_point()>;`.
- Add `LivePreviewClock` to `NativeSlotWorkflowRuntime` construction.

- [ ] **Step 1: Write failing deterministic-clock coordinator tests**

Inject a mutable time point and assert repeated `pump()` calls before 1,000 ms schedule nothing; the first pump at exactly 1,000 ms schedules one appearance operation. Assert a second pump while scheduled work is pending does not duplicate it.

Add cases for latest-wins after an in-flight completion, immediate Save flush, scheduler exception, appearance-operation exception, synchronization-only retry, and stale completion after target/session replacement.

- [ ] **Step 2: Run the focused runtime target and verify RED**

Run the configured MSVC build for `NativeSlotWorkflowRuntimeTests`. Expected: compile failure because the coordinator has no clock dependency.

- [ ] **Step 3: Inject and use the monotonic clock**

At the beginning of `pump()`, call `m_model.advanceLivePreview(m_livePreviewClock())`, then consume target, query, and mutation tickets through the existing ordering and `InFlightGuard`. Do not add a second scheduler or bypass `m_appearanceOperation`.

Production composition passes:

```cpp
[] { return std::chrono::steady_clock::now(); }
```

Every test constructor passes a deterministic clock lambda.

- [ ] **Step 4: Run Debug and Release focused verification**

Build and run `NativeSlotWorkflowRuntimeTests`, `OfficialMenuFrameworkAdapterTests`, and `SlaveTatsUI` in Debug and Release with `--parallel 1`. Expected: tests pass and plugin links without warnings.

- [ ] **Step 5: Review Task 2 and request commit approval**

Run `git diff --check`; review only Task 2 files. Proposed commit: `feat: schedule debounced live appearance preview`.

## Task 3: Present Live Preview state and deferred Close in the native UI

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consume `LivePreviewStatus`, `retryLivePreviewOperation()`, `requestEditAppearanceClose()`, and `takeMenuCloseRequest()`.
- Produce `std::string_view livePreviewStatusLabel(LivePreviewStatus)`.
- Produce a pure presentation value describing Save, Cancel, Close, and retry visibility/enabled state.

- [ ] **Step 1: Write failing presentation tests**

Assert exact labels:

```cpp
expect(livePreviewStatusLabel(LivePreviewStatus::pending) == "Preview pending...", ...);
expect(livePreviewStatusLabel(LivePreviewStatus::updating) == "Updating preview...", ...);
expect(livePreviewStatusLabel(LivePreviewStatus::applied) == "Preview applied", ...);
expect(livePreviewStatusLabel(LivePreviewStatus::restoring) ==
    "Restoring original appearance...", ...);
```

Assert retry labels are exactly `Retry Preview`, `Retry Preview Sync`, `Retry Restore`, and `Retry Restore Sync`. Assert target controls, navigation, and mutation actions are disabled while the transaction owns work.

- [ ] **Step 2: Run the adapter target and verify RED**

Build `OfficialMenuFrameworkAdapterTests`. Expected: compile failure because Live Preview presentation helpers are absent.

- [ ] **Step 3: Implement status, actions, and deferred Close**

Render the status under appearance controls without moving the existing footer. Save calls the existing model Save intent, Cancel calls transaction-aware cancel, and Close calls `requestEditAppearanceClose()` rather than closing the menu directly while an Edit Appearance session exists.

After each coordinator pump, composition consumes `takeMenuCloseRequest()` and invokes the existing menu close callback exactly once. The adapter never owns timers, Actor pointers, runtime operations, or rollback values.

- [ ] **Step 4: Test frame safety and exact intent routing**

Use pure helper/orchestration seams to prove that accepted Save/Cancel/Close/retry input emits only the matching model intent, duplicate input is disabled during in-flight work, and actor-scoped references are not used after a transition invalidates them.

- [ ] **Step 5: Run Debug and Release adapter/plugin verification**

Build and run `OfficialMenuFrameworkAdapterTests` plus `SlaveTatsUI` in both configurations. Expected: all cases pass and existing footer/Close alignment tests remain green.

- [ ] **Step 6: Review Task 3 and request commit approval**

Run `git diff --check`; review only the four Task 3 files. Proposed commit: `feat: add live preview controls and safe rollback`.

## Task 4: Validate runtime safety and regression behavior

**Files:**
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes all Live Preview contracts from Tasks 1–3.
- Produces no new production API unless a failing integration test proves one is required.

- [ ] **Step 1: Add the cross-layer regression matrix**

Cover Player and NPC exact IDs, Actor unload during Preview and Restore, target generation replacement, stale handle rejection, external-slot read-only, glow-to-non-glow restoration, emissive multiplier zero and values greater than one, Save after successful Preview, Close after synchronization failure, and no repeat write during synchronization-only retry.

- [ ] **Step 2: Run focused targets and repair only proven gaps**

Run the model, coordinator, adapter, and runtime appearance targets. Trace any failure to its owning layer and make only the smallest production correction inside the existing architecture. Do not add alternate overlay paths, new persistence, or unrelated QoL work.

- [ ] **Step 3: Run full Debug verification**

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --parallel 1 && ctest --test-dir build\debug --output-on-failure'
```

- [ ] **Step 4: Run full Release verification**

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\release --parallel 1 && ctest --test-dir build\release --output-on-failure'
```

- [ ] **Step 5: Audit final boundaries**

Run `git diff --check`, inspect `git status --short`, and confirm timer/debounce state exists only in model/coordinator; adapter has no clock/runtime access; runtime retains loaded-Actor/stale-handle validation; no Player fallback or external-slot writes were added.

- [ ] **Step 6: Review Task 4 and request commit approval**

If Task 4 adds test or production changes, propose the focused commit `test: cover live actor preview integration`. If it produces no diff, record verification evidence without creating an empty commit.

## Task 5: Deploy and complete in-game acceptance

**Files:**
- Modify: `ROADMAP.md` only after acceptance.
- Move: this plan from `docs/superpowers/plans/active/` to `docs/superpowers/plans/archive/` only after acceptance.

- [ ] **Step 1: Request fresh deployment approval**

Report current HEAD, clean status, Debug/Release totals, Release DLL path, and SHA-256. Do not deploy before explicit approval.

- [ ] **Step 2: Back up and deploy the verified Release DLL**

Copy the existing installed DLL to a timestamped `.bak-*`, deploy the verified artifact, and confirm source/deployed SHA-256 equality.

- [ ] **Step 3: Complete in-game acceptance**

Verify Player and Crosshair NPC; exact one-second inactivity behavior; rapid latest-wins edits; every supported field; Save without visible duplicate update; Cancel and Close restoration; Preview/Restore sync-only retry; NPC unload and retry; target/navigation blocking; glow-to-non-glow; Lock and external-slot regressions.

- [ ] **Step 4: Close the slice after user acceptance**

Check the Live Actor Preview roadmap item, archive this plan, run `git diff --check`, and request approval for `docs: complete native live actor preview`.
