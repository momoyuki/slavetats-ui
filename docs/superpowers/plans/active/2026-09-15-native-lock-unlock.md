# Native Lock / Unlock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users lock and unlock a currently applied SlaveTats-managed tattoo from native Slot Actions, while preventing Replace and Remove until it is unlocked.

**Architecture:** Add a typed `SetTattooLocked` request at the Core and runtime boundary, verified through the existing JContainers integer write/readback seam. The native model and coordinator carry a generation-bound lock ticket; the adapter presents the authoritative snapshot and never calls runtime APIs directly.

**Tech Stack:** C++23, CommonLibSSE NG, SKSE task interface, SlaveTatsNG API, JContainers, ImGui, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-15-native-lock-unlock-design.md`

## Global Constraints

- SlaveTatsNG remains the authoritative tattoo runtime.
- External overlays remain read-only and must never receive a lock mutation.
- Revalidate each session-local runtime handle against the explicitly requested actor before mutation.
- Never fall back to the Player if the requested actor cannot be resolved.
- Keep JContainers and SlaveTatsNG calls out of the adapter and workflow model.
- Lock-state changes write verified `locked` integer `1` or `0`, but do not mark `.SlaveTats.updated` or synchronize.
- Keep Domain support and NPC targeting out of this plan.
- Do not commit, push, or deploy without the user's separate explicit approval.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/core/TattooModels.h` | Typed lock request/result and stable lock error code. |
| `src/core/ITattooRuntime.h` | Runtime abstraction for lock-state mutation. |
| `src/core/SlaveTatsService.*` | Dependency and request-shape validation before forwarding. |
| `src/runtime/SlaveTatsRuntime.*` | Actor/handle ownership validation and verified JContainers persistence. |
| `src/native/NativeSlotWorkflowModel.*` | Lock ticket, UI state, and generation-safe completion. |
| `src/native/NativeSlotWorkflowRuntime.*` | One scheduled lock operation using the existing in-flight guard. |
| `src/native/OfficialMenuFrameworkAdapter.*` | Locked presentation, disabled actions, and Lock/Unlock intent. |
| Existing Core/Runtime/Native/Adapter test files | Narrow regression coverage for each touched boundary. |

### Task 1: Add the typed Core lock contract

**Files:**
- Modify: `src/core/TattooModels.h`
- Modify: `src/core/ITattooRuntime.h`
- Modify: `src/core/SlaveTatsService.h`
- Modify: `src/core/SlaveTatsService.cpp`
- Test: `tests/core/SlaveTatsServiceTests.cpp`

**Interfaces:**
- Produces `SetTattooLockedRequest`, `SetTattooLockedSuccess`, `SetTattooLockedResult`, and `ServiceErrorCode::lockFailed`.
- Produces `ITattooRuntime::setTattooLocked(const SetTattooLockedRequest&)` and `SlaveTatsService::setTattooLocked(const SetTattooLockedRequest&)`.
- Later tasks call the service through this exact signature.

- [ ] **Step 1: Write failing service tests**

Extend the existing fake runtime with `lockRequest`, `lockCount`, `lockResult`, and:

```cpp
SetTattooLockedResult setTattooLocked(const SetTattooLockedRequest& request) override {
    lockRequest = request;
    ++lockCount;
    return lockResult;
}
```

Add tests that assert a valid `{.actorFormId = 0x14, .runtimeHandle = 7, .locked = true}` request forwards unchanged once; unavailable API/JContainers, `actorFormId == 0`, and `runtimeHandle == 0` return the established availability/actor/stale-handle errors and leave `lockCount == 0`.

- [ ] **Step 2: Run the focused test and verify failure**

Run: `cmake --build build/debug --target SlaveTatsServiceTests --parallel; & .\build\debug\SlaveTatsServiceTests.exe`

Expected: compilation failure because the lock request and runtime/service methods do not exist.

- [ ] **Step 3: Add the minimal Core contract and forwarding implementation**

Add these values beside the appearance contract:

```cpp
struct SetTattooLockedRequest {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};

struct SetTattooLockedSuccess {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};
```

`SlaveTatsService::setTattooLocked` must apply the same API/JContainers checks as mutation methods, reject zero actor as `actorNotFound`, reject zero handle as `staleTattooHandle`, then call `m_runtime.setTattooLocked(request)` without altering `locked`.

- [ ] **Step 4: Run the focused test and verify success**

Run: `cmake --build build/debug --target SlaveTatsServiceTests --parallel; & .\build\debug\SlaveTatsServiceTests.exe`

Expected: PASS, including existing service tests.

- [ ] **Step 5: Review Task 1 diff and request commit approval**

Run: `git diff --check; git diff -- src/core/TattooModels.h src/core/ITattooRuntime.h src/core/SlaveTatsService.h src/core/SlaveTatsService.cpp tests/core/SlaveTatsServiceTests.cpp`

Proposed commit after user approval: `feat: add tattoo lock service contract`

### Task 2: Persist lock state safely in the runtime

**Files:**
- Modify: `src/runtime/SlaveTatsRuntime.h`
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`
- Modify: `CMakeLists.txt` only if the existing appearance test target must be renamed to reflect lock coverage; otherwise leave it unchanged.

**Interfaces:**
- Consumes `SetTattooLockedRequest` from Task 1.
- Produces `SlaveTatsRuntime::setTattooLocked` implementation used by `SlaveTatsService`.
- Uses the existing injected `SlaveTatsAppearanceBindings` and production `jcmini::JMap::setIntAndVerify` seam.

- [ ] **Step 1: Write failing runtime tests**

Add binding-state coverage that calls:

```cpp
const auto result = runtime.setTattooLocked({
    .actorFormId = 0x14,
    .runtimeHandle = state.appliedHandle,
    .locked = true,
});
```

Assert that it writes/readbacks `"locked" == 1`, returns the actor/handle/`true`, does not set the actor updated marker, and does not synchronize. Repeat with `locked = false` expecting `0`. Add stale/foreign-handle and ineffective-readback cases that assert zero writes or `lockFailed`, respectively.

- [ ] **Step 2: Run the focused test and verify failure**

Run: `cmake --build build/debug --target SlaveTatsRuntimeAppearanceTests --parallel; & .\build\debug\SlaveTatsRuntimeAppearanceTests.exe`

Expected: compilation failure because `SlaveTatsRuntime::setTattooLocked` does not exist.

- [ ] **Step 3: Implement the validated verified write**

Add a dedicated runtime pool name, resolve exactly `request.actorFormId`, query current applied handles using the existing actor-scoped query path, and reject a missing handle with `staleTattooHandle` before writing. Use `setTattooInt/getTattooInt` in injected bindings and `jcmini::JMap::setIntAndVerify(runtimeHandle, "locked", request.locked ? 1 : 0)` in production. Return `lockFailed` when write/readback fails.

Do not call `markActorUpdated`, `synchronize`, `remove_tattoo_from_slot`, or `add_and_get_tattoo` in this method. Ensure its pool guard covers all early returns.

- [ ] **Step 4: Run runtime tests and verify success**

Run: `cmake --build build/debug --target SlaveTatsRuntimeAppearanceTests --parallel; & .\build\debug\SlaveTatsRuntimeAppearanceTests.exe`

Expected: PASS, including existing appearance/sync-only tests.

- [ ] **Step 5: Review Task 2 diff and request commit approval**

Run: `git diff --check; git diff -- src/runtime/SlaveTatsRuntime.h src/runtime/SlaveTatsRuntime.cpp tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp CMakeLists.txt`

Proposed commit after user approval: `feat: persist tattoo lock state safely`

### Task 3: Add generation-safe native workflow scheduling

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Test: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`

**Interfaces:**
- Consumes `core::SetTattooLockedRequest` and result from Tasks 1–2.
- Produces `toggleSelectedSlotLock()`, `takeLockRequest()`, `completeLockStateChange()`, and `isLockStateChangeInFlight()` on the model.
- Produces `SlotLockOperation` and a constructor argument on `NativeSlotWorkflowRuntime`.

- [ ] **Step 1: Write failing model and scheduler tests**

Load a Slot Actions target whose `TattooEntry` has `runtimeHandle = 7` and vary `locked`. Assert:

```cpp
expect(model.toggleSelectedSlotLock(), "owned slot should create lock ticket");
const auto ticket = model.takeLockRequest();
expect(ticket && ticket->request.locked, "unlocked slot should request Lock");
```

Add the locked case expecting a ticket whose `request.locked == false`; empty/external/missing-handle cases produce no ticket. Cover successful completion returning to Current Slots with one selected-area refresh, error completion retaining Slot Actions, and stale generation completion changing nothing. In runtime tests, enqueue two pumps and assert the scheduler receives one lock operation; make the scheduler throw and assert model error plus a subsequent operation can pump.

- [ ] **Step 2: Run focused native tests and verify failure**

Run: `cmake --build build/debug --target NativeSlotWorkflowModelTests NativeSlotWorkflowRuntimeTests --parallel; & .\build\debug\NativeSlotWorkflowModelTests.exe; & .\build\debug\NativeSlotWorkflowRuntimeTests.exe`

Expected: compilation failure because the lock ticket/model/runtime operation is absent.

- [ ] **Step 3: Implement ticket, state transitions, and scheduling**

Add `SlotLockTicket { std::uint64_t generation; core::SetTattooLockedRequest request; }`, one pending ticket and one active generation to the model. `toggleSelectedSlotLock()` must derive `!tattoo.locked` only after validating the current Slot Actions target is an owned occupied slot with nonzero handle. It uses the explicit actor ID from the current slot snapshot, not an implicit Player fallback.

`completeLockStateChange()` ignores mismatched generations. Success clears target/action state, changes screen to `currentSlots`, and calls `scheduleSlotQuery(m_selectedArea)`. Failure stores the service error and restores `slotActions`. `isLockStateChangeInFlight()` returns true while either the pending ticket or active generation exists.

Extend the coordinator constructor with `SlotLockOperation`, pump after existing mutation queues, and implement `scheduleLock` with `InFlightGuard`, `lockFailed` fallback errors, and scheduler-throw completion behavior matching Remove/Appearance.

- [ ] **Step 4: Run focused native tests and verify success**

Run: `cmake --build build/debug --target NativeSlotWorkflowModelTests NativeSlotWorkflowRuntimeTests --parallel; & .\build\debug\NativeSlotWorkflowModelTests.exe; & .\build\debug\NativeSlotWorkflowRuntimeTests.exe`

Expected: PASS, including all existing workflow transitions.

- [ ] **Step 5: Review Task 3 diff and request commit approval**

Run: `git diff --check; git diff -- src/native/NativeSlotWorkflowModel.h src/native/NativeSlotWorkflowModel.cpp src/native/NativeSlotWorkflowRuntime.h src/native/NativeSlotWorkflowRuntime.cpp tests/native/NativeSlotWorkflowModelTests.cpp tests/native/NativeSlotWorkflowRuntimeTests.cpp`

Proposed commit after user approval: `feat: schedule native tattoo lock actions`

### Task 4: Present and gate Lock / Unlock in native UI

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes `NativeSlotWorkflowModel::toggleSelectedSlotLock()` and Slot Snapshot `TattooEntry.locked` from Task 3.
- Produces no runtime-facing interface; this layer renders and forwards intent only.

- [ ] **Step 1: Write failing adapter tests**

Use existing recording ImGui bindings to render a locked owned Slot Actions state. Assert emitted controls contain `Locked`, `Unlock`, and disabled Replace/Remove, while Edit Appearance is enabled. Render the unlocked state and assert it contains `Lock` and enabled Replace/Remove. Invoke the recorded Lock/Unlock button callback and assert only the model ticket is created; no service/runtime callback is directly called from the adapter.

- [ ] **Step 2: Run the focused adapter test and verify failure**

Run: `cmake --build build/debug --target OfficialMenuFrameworkAdapterTests --parallel; & .\build\debug\OfficialMenuFrameworkAdapterTests.exe`

Expected: assertion failure because the adapter does not yet render the lock state or action.

- [ ] **Step 3: Implement presentation-only controls**

In Slot Actions, derive `locked` from the selected owned Slot Snapshot. Render the `Locked` badge when true. Wrap Replace and Remove in the existing disabled-control presentation when locked and show the exact helper text `Unlock to replace or remove.` Keep Edit Appearance independent of `locked`. Render an immediate `Lock` or `Unlock` button that invokes `workflow.toggleSelectedSlotLock()` exactly once; disable it while the workflow has a pending/active lock action.

Do not add direct JContainers, SlaveTatsNG, actor lookup, scheduler, or optimistic snapshot mutation to the adapter.

- [ ] **Step 4: Run adapter tests and verify success**

Run: `cmake --build build/debug --target OfficialMenuFrameworkAdapterTests --parallel; & .\build\debug\OfficialMenuFrameworkAdapterTests.exe`

Expected: PASS, including existing layout and Close behavior tests.

- [ ] **Step 5: Review Task 4 diff and request commit approval**

Run: `git diff --check; git diff -- src/native/OfficialMenuFrameworkAdapter.cpp tests/native/OfficialMenuFrameworkAdapterTests.cpp`

Proposed commit after user approval: `feat: add native lock and unlock controls`

### Task 5: Integrate, document, and validate the milestone slice

**Files:**
- Modify: `ROADMAP.md` only after all implementation and in-game acceptance pass.
- Modify: `docs/superpowers/plans/active/2026-09-15-native-lock-unlock.md` to check completed tasks during execution.
- Move: `docs/superpowers/plans/active/2026-09-15-native-lock-unlock.md` to `docs/superpowers/plans/archive/2026-09-15-native-lock-unlock.md` only after the complete plan passes acceptance.

**Interfaces:**
- Consumes all completed feature slices.
- Produces a verified Lock / Unlock completion record; Domain remains unchecked in vNext.2.

- [ ] **Step 1: Build every Debug test target in the MSVC environment**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --parallel && ctest --test-dir build\debug --output-on-failure'
```

Expected: all Debug CTest tests PASS.

- [ ] **Step 2: Build and run Release tests in the MSVC environment**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\release --parallel && ctest --test-dir build\release --output-on-failure'
```

Expected: all Release CTest tests PASS.

- [ ] **Step 3: Inspect final change scope**

Run: `git diff --check; git status --short; git diff --check HEAD`

Expected: no whitespace errors; only Lock/Unlock implementation, tests, roadmap/plan records, and no secrets.

- [ ] **Step 4: Perform in-game acceptance after separate deployment approval**

Verify the eight acceptance steps in `docs/superpowers/specs/2026-09-15-native-lock-unlock-design.md`. Do not deploy before the user separately authorizes deployment. Record failures as defects; do not check the roadmap Lock/Unlock items until acceptance passes.

- [ ] **Step 5: Update records and request final commit approval**

After verified acceptance, check only the four Lock/Unlock boxes in vNext.2 and leave all Domain boxes unchecked. Archive this plan. Show the final scoped diff and request separate approval for each proposed Conventional Commit; do not push.
