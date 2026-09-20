# Native Actor Targeting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the native slot-first workflow from the Player to an explicitly selected loaded Crosshair Actor without cross-actor state or mutation leakage.

**Architecture:** A focused native target provider resolves Skyrim crosshair state into a copyable `ActorTarget` value. The workflow model owns target selection and target-generation invalidation; the existing scheduler carries resolution and tattoo operations, while Core and `SlaveTatsRuntime` continue authorizing every operation by exact actor form ID.

**Tech Stack:** C++23, CommonLibSSE NG, SKSE task interface, Dear ImGui through SKSE Menu Framework, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-17-native-actor-targeting-design.md`

## Global Constraints

- Player remains the default explicit target with form ID `0x14`.
- Crosshair resolution accepts only a nonzero `RE::Actor` with loaded 3D.
- Store copied form ID and display name only; never retain a raw Actor pointer in workflow state.
- Never silently fall back to Player after Crosshair selection or failure.
- Every slot query and mutation uses the active target form ID.
- Reject a mutation when its Slot Snapshot actor does not match the active target.
- A target change clears all four area caches, page indices, selection, preview, edit session, and actor-scoped errors.
- Target switching and refresh are unavailable while a mutation is pending or active.
- Ignore resolution, query, and mutation completions from an obsolete target generation.
- Preserve external-slot read-only, stale-handle validation, scheduler boundaries, and synchronization-only retry.
- Do not add NPC lists, follower selection, persistence, automatic retargeting, camera/freeze controls, or unloaded-actor support.
- Do not commit, push, or deploy without the required explicit approvals; deployment requires a fresh approval after its commit.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/native/ActorTarget.h` | Copyable target identity, target kind, and typed resolution result. |
| `src/native/ActorTargetProvider.h/.cpp` | Testable Crosshair provider seam plus CommonLibSSE production resolver. |
| `src/native/NativeSlotWorkflowModel.*` | Active target, resolution tickets, cache invalidation, actor-scoped request generation. |
| `src/native/NativeSlotWorkflowRuntime.*` | Scheduled target resolution and stale-safe completion delivery. |
| `src/native/OfficialMenuFrameworkAdapter.*` | Shared target selector/header and target-state presentation. |
| `src/runtime/SlaveTatsRuntime.*` | Re-resolve and reject an unloaded requested Actor before tattoo work. |
| `src/main.cpp` | Inject the production Crosshair resolver into the coordinator. |
| `CMakeLists.txt` | Compile provider code and register its focused test target. |
| `tests/native/*` | Provider, model, coordinator, and adapter regression coverage. |

## Task 1: Add the copyable Actor target and Crosshair provider

**Files:**
- Create: `src/native/ActorTarget.h`
- Create: `src/native/ActorTargetProvider.h`
- Create: `src/native/ActorTargetProvider.cpp`
- Create: `tests/native/ActorTargetProviderTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ActorTargetKind`, `ActorTarget`, `ActorTargetResult`.
- Produces: `ActorTargetProviderBindings` and two `resolveCrosshairActorTarget` overloads.
- Consumes: `RE::CrosshairPickData::targetActor`, `RE::Actor::GetFormID`, `GetDisplayFullName`, and `Is3DLoaded` only in the production provider.

- [ ] **Step 1: Write failing provider tests and register the target**

Define the expected public contract in tests:

```cpp
using stui::native::ActorTarget;
using stui::native::ActorTargetKind;
using stui::native::ActorTargetProviderBindings;

ActorTargetProviderBindings validBindings() {
    return {
        .resolveCrosshairActor = [] { return reinterpret_cast<void*>(0x1); },
        .formId = [](void*) { return 0x1234U; },
        .is3DLoaded = [](void*) { return true; },
        .displayName = [](void*) { return std::string{"Lydia"}; },
    };
}
```

Assert that a valid Actor produces `{crosshair, 0x1234, "Lydia"}`; missing Actor,
zero form ID, and unloaded 3D return `actorNotFound`; an empty display name
becomes `Unnamed Actor`. Register `ActorTargetProviderTests` with
`src/native/ActorTargetProvider.cpp` and link `CommonLibSSE::CommonLibSSE`.

- [ ] **Step 2: Run the focused provider test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target ActorTargetProviderTests --parallel 1'
```

Expected: configure/build failure because the new headers and implementation do not exist.

- [ ] **Step 3: Implement the value contract and binding-driven resolver**

Add:

```cpp
enum class ActorTargetKind { player, crosshair };

struct ActorTarget {
    ActorTargetKind kind{ActorTargetKind::player};
    std::uint32_t formId{0x14};
    std::string displayName{"Player"};
};

using ActorTargetResult = std::expected<ActorTarget, core::ServiceError>;

struct ActorTargetProviderBindings {
    std::function<void*()> resolveCrosshairActor;
    std::function<std::uint32_t(void*)> formId;
    std::function<bool(void*)> is3DLoaded;
    std::function<std::string(void*)> displayName;
};

ActorTargetResult resolveCrosshairActorTarget(
    const ActorTargetProviderBindings& bindings);
ActorTargetResult resolveCrosshairActorTarget();
```

The binding-driven resolver validates every callable, Actor handle, form ID,
and loaded-3D result before returning a copied value. The production overload
uses `RE::CrosshairPickData::GetSingleton()->targetActor.get()`, copies the
Actor fields, and releases the temporary smart pointer on return.

- [ ] **Step 4: Run the focused provider test and verify GREEN**

Run the Step 2 build, then:

```powershell
& .\build\debug\ActorTargetProviderTests.exe
```

Expected: every provider case passes.

- [ ] **Step 5: Review Task 1 and request commit approval**

Run `git diff --check` and review only the five Task 1 files. Proposed commit:
`feat: add crosshair actor target provider`.

---

## Task 2: Make workflow state explicitly actor-scoped

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Consumes: `ActorTarget`, `ActorTargetKind`, `ActorTargetResult` from Task 1.
- Produces: `ActorTargetResolutionTicket { std::uint64_t generation; }`.
- Produces: `bool selectPlayerTarget()`, `bool selectCrosshairTarget()`, and
  `bool refreshCrosshairTarget()`.
- Produces: `std::optional<ActorTargetResolutionTicket> takeActorTargetRequest()`
  and `void completeActorTargetResolution(std::uint64_t, ActorTargetResult)`.
- Produces read-only presentation state:
  `ActorTargetKind selectedTargetKind() const`,
  `const ActorTarget* actorTarget() const`, and
  `bool isActorTargetResolutionInFlight() const`.

- [ ] **Step 1: Write failing target-selection and isolation tests**

Cover these literal behaviors:

```cpp
expect(model.actorTarget() && model.actorTarget()->formId == 0x14,
    "expected explicit Player target at startup");
expect(model.selectCrosshairTarget(), "expected Crosshair selection accepted");
expect(!model.actorTarget() && model.isActorTargetResolutionInFlight(),
    "expected no active Actor while resolving Crosshair");
```

Complete resolution with `{crosshair, 0x1234, "Lydia"}` and assert the next
slot query uses `0x1234`. Populate BODY and FACE caches first, change target,
then assert both are cleared and page indices reset. Complete an obsolete
Player query after Crosshair selection and assert it changes no state.

Add mutation assertions proving apply, remove, appearance update,
synchronization-only retry, and Lock use the active `0x1234`, not `0x14`.
Provide a Slot Snapshot with a different `actorFormId` and assert no mutation
ticket is produced. Assert target switching returns false while any mutation is
pending/active.

- [ ] **Step 2: Run the focused model test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowModelTests --parallel 1'
```

Expected: compilation failure because target state and selection methods are absent.

- [ ] **Step 3: Implement target state, invalidation, and resolution tickets**

Add an explicit Player target, selected target kind, pending/active target
resolution state, and a target generation. Centralize target changes in a
private helper that clears all `AreaState` slots/page indices plus selected
slot, preview, edit session, pending target-specific query, and error state.

`selectCrosshairTarget()` and `refreshCrosshairTarget()` clear active target and
emit one resolution ticket. `completeActorTargetResolution()` accepts only the
matching generation, stores a valid Crosshair target, and schedules the selected
area query. Failure leaves Crosshair selected with no active Actor and exposes
the provider error. `selectPlayerTarget()` constructs `{player, 0x14, "Player"}`
and schedules the exact Player query.

- [ ] **Step 4: Replace workflow Player constants and enforce snapshot identity**

Use `m_actorTarget->formId` in query/apply/refresh paths. Existing remove,
appearance, and lock paths may use `TattooSlots::actorFormId` only after checking
it equals the active target form ID. Synchronization-only retry retains the
actor captured by the edit session and must also match the active target.

`completeSlotQuery()` accepts a successful snapshot only when its actor form ID
matches the active target and ticket generation. Target changes invalidate the
active query generation so old completions are ignored.

- [ ] **Step 5: Run the focused model test and verify GREEN**

Run the Step 2 command followed by:

```powershell
& .\build\debug\NativeSlotWorkflowModelTests.exe
```

Expected: all model tests pass, including unchanged Player behavior.

- [ ] **Step 6: Review Task 2 and request commit approval**

Run `git diff --check` and review only the three Task 2 files. Proposed commit:
`feat: scope native workflow to actor targets`.

---

## Task 3: Schedule Crosshair resolution and compose the production provider

**Files:**
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp` only for constructor fixtures if required

**Interfaces:**
- Consumes: `ActorTargetResult resolveCrosshairActorTarget()` from Task 1.
- Consumes: target-resolution ticket methods from Task 2.
- Produces: `ActorTargetOperation = std::function<ActorTargetResult()>` injected into `NativeSlotWorkflowRuntime`.

- [ ] **Step 1: Write failing coordinator tests**

Extend the runtime fixture with a target resolver count and result. Assert:

```cpp
fixture.model.selectCrosshairTarget();
fixture.runtime.pump();
fixture.runScheduledTask();
expect(fixture.targetResolutionCount == 1,
    "expected one scheduled Crosshair resolution");
expect(fixture.model.actorTarget() && fixture.model.actorTarget()->formId == 0x1234,
    "expected resolved Crosshair target delivered to model");
```

Also assert resolver exception and scheduler exception clear in-flight state,
produce `actorNotFound`, and schedule no Player query. Add a stale completion
case where Player is reselected before the scheduled Crosshair task finishes.

- [ ] **Step 2: Run the focused coordinator test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowRuntimeTests --parallel 1'
```

Expected: compilation failure because the coordinator lacks the target operation.

- [ ] **Step 3: Implement scheduled target resolution**

Add `ActorTargetOperation` to the constructor and member state. In `pump()`,
consume target-resolution tickets before slot queries. `scheduleActorTarget`
uses the same `NativeSlotScheduler` and `InFlightGuard`, converts exceptions to:

```cpp
core::ServiceError{
    .code = core::ServiceErrorCode::actorNotFound,
    .message = "Failed to resolve crosshair Actor.",
}
```

Deliver every result through `completeActorTargetResolution(generation, result)`;
the model owns stale rejection.

- [ ] **Step 4: Inject the production provider in application composition**

Add `src/native/ActorTargetProvider.cpp` to the plugin sources. In `src/main.cpp`,
pass this operation before the slot query operation:

```cpp
[] { return native::resolveCrosshairActorTarget(); },
```

Update every test constructor with an explicit deterministic target operation;
never let a unit test read live Skyrim crosshair state.

- [ ] **Step 5: Run coordinator and application-adapter targets**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowRuntimeTests OfficialMenuFrameworkAdapterTests SlaveTatsUI --parallel 1 && .\build\debug\NativeSlotWorkflowRuntimeTests.exe && .\build\debug\OfficialMenuFrameworkAdapterTests.exe'
```

Expected: coordinator and adapter tests pass and the plugin links.

- [ ] **Step 6: Review Task 3 and request commit approval**

Run `git diff --check` and review the six Task 3 files. Proposed commit:
`feat: schedule crosshair actor resolution`.

---

## Task 4: Revalidate loaded Actor state at mutation time

**Files:**
- Modify: `src/runtime/SlaveTatsRuntime.h`
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`

**Interfaces:**
- Extends: `SlaveTatsAppearanceBindings` with `std::function<bool(void*)> isActor3DLoaded`.
- Produces: one private loaded-Actor resolution path used by slot query, apply,
  remove, appearance update, and Lock / Unlock.

- [ ] **Step 1: Write failing unloaded-Actor runtime tests**

Extend the runtime fixture with `actorLoaded`. Resolve the requested form ID to
the fixture Actor while returning false from `isActor3DLoaded`. Assert:

```cpp
expectError(runtime.querySlots(0x1234, TattooArea::body),
    ServiceErrorCode::actorNotFound, "Actor not found");
expectError(runtime.updateAppearance(validAppearanceRequest(0x1234)),
    ServiceErrorCode::actorNotFound, "Actor not found");
expectError(runtime.setTattooLocked(validLockRequest(0x1234)),
    ServiceErrorCode::actorNotFound, "Actor not found");
expect(state.queryCount == 0 && state.updatedWriteCount == 0,
    "expected unloaded Actor rejection before query or mutation");
```

Extend the existing fake SlaveTatsNG API state with `applyCount`, `removeCount`,
and `synchronizeCount`. Send otherwise-valid apply and remove requests for
`0x1234`, then assert `actorNotFound` and all three counters remain zero. This
proves the shared loaded-Actor guard runs before the API mutation paths.

- [ ] **Step 2: Run the focused runtime test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target SlaveTatsRuntimeAppearanceTests --parallel 1 && .\build\debug\SlaveTatsRuntimeAppearanceTests.exe'
```

Expected: the new cases fail because resolved Actors are not checked for loaded 3D.

- [ ] **Step 3: Implement one loaded-Actor guard**

Centralize Actor resolution so production uses
`RE::TESForm::LookupByID<RE::Actor>(formId)` plus `Is3DLoaded()`, while tests use
`resolveActor` plus `isActor3DLoaded`. Missing resolver/check bindings, null Actor,
zero form ID, or unloaded 3D returns `actorNotFound` before API/JContainers work.

Route `querySlots`, `applyToSlot`, `removeFromSlot`, appearance backend
resolution, and `setTattooLocked` through that policy. Do not add fallback,
automatic load, polling, or retained Actor pointers.

- [ ] **Step 4: Run the focused runtime test and verify GREEN**

Run the Step 2 command. Expected: all runtime appearance tests pass and every
unloaded case reports zero mutation.

- [ ] **Step 5: Review Task 4 and request commit approval**

Run `git diff --check` and review only the three Task 4 files. Proposed commit:
`fix: reject unloaded actor targets`.

---

## Task 5: Present and control Actor targets on every workflow screen

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes: target model intents and presentation state from Task 2.
- Produces: `std::string formatActorTargetIdentity(const ActorTarget&)`.
- Produces: `std::string_view actorTargetStatusLabel(bool, const ActorTarget*)`.
- Produces: `ActorTargetControlPresentation actorTargetControlPresentation(
  ActorTargetKind, bool resolving, bool mutationInFlight)` where the value
  declares Player/Crosshair enabled state and Refresh visibility/enabled state.
- Produces: one shared target header renderer.

- [ ] **Step 1: Write failing adapter-helper tests**

Define pure helpers and assert hand-derived output:

```cpp
expect(formatActorTargetIdentity(ActorTarget{
           .kind = ActorTargetKind::crosshair,
           .formId = 0xA2C8E,
           .displayName = "Lydia",
       }) == "Lydia [0x000A2C8E]",
    "expected name and fixed-width uppercase form ID");
expect(actorTargetStatusLabel(true, nullptr) == "Resolving target...",
    "expected explicit resolving state");
expect(actorTargetStatusLabel(false, nullptr) == "No valid crosshair Actor",
    "expected explicit missing-target state");
```

Add control-state tests proving Player/Crosshair selection and Refresh are
disabled during mutation, and Refresh is visible only in Crosshair mode.

- [ ] **Step 2: Run the focused adapter test and verify RED**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target OfficialMenuFrameworkAdapterTests --parallel 1'
```

Expected: compilation failure because target presentation helpers are absent.

- [ ] **Step 3: Implement pure presentation helpers**

Format form IDs as eight uppercase hexadecimal digits. Derive selector disabled
state from pending/active mutation state, not from Actor kind. Use exact labels
`Player`, `Crosshair Target`, `Refresh Target`, `Resolving target...`,
`No valid crosshair Actor`, and fallback name `Unnamed Actor`.

- [ ] **Step 4: Add one shared target header to every workflow renderer**

Render the selector, identity/status, and conditional Refresh control through a
single helper called by Current Slots, Picker, Preview, Slot Actions, Remove
confirmation, and Edit Appearance. Button callbacks call only workflow-model
intent methods. Do not access `RE::CrosshairPickData` or runtime services.

Disable slot grids and mutation actions when no active target or while resolving.
Change the Current Slots title to `Current Tattoos - <Actor Name>`. Preserve all
Close and navigation placement.

- [ ] **Step 5: Run the focused adapter test and verify GREEN**

Run the Step 2 command followed by:

```powershell
& .\build\debug\OfficialMenuFrameworkAdapterTests.exe
```

Expected: all adapter tests pass.

- [ ] **Step 6: Review Task 5 and request commit approval**

Run `git diff --check` and review only the three Task 5 files. Proposed commit:
`feat: add native actor target controls`.

---

## Task 6: Validate the Actor Targeting slice

**Files:**
- Modify: `ROADMAP.md` only after automated and in-game acceptance.
- Move: this plan from `docs/superpowers/plans/active/` to `docs/superpowers/plans/archive/` only after acceptance.

- [ ] **Step 1: Run full Debug verification**

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --parallel 1 && ctest --test-dir build\debug --output-on-failure'
```

Expected: every Debug CTest test passes.

- [ ] **Step 2: Run full Release verification**

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\release --parallel 1 && ctest --test-dir build\release --output-on-failure'
```

Expected: every Release CTest test passes.

- [ ] **Step 3: Inspect final scope and architecture boundaries**

Run:

```powershell
git diff --check
git status --short
git diff -- src tests CMakeLists.txt docs ROADMAP.md CONTEXT.md
rg -n "CrosshairPickData|targetActor" src/native
```

Expected: Skyrim crosshair access appears only in `ActorTargetProvider.cpp`;
adapter/model contain no direct Skyrim, JContainers, or SlaveTatsNG calls; no
unrelated QoL or camera/freeze work is present.

- [ ] **Step 4: Request separate deployment approval and complete in-game acceptance**

Deploy only after the feature commit and a fresh approval. Verify every
in-game case from the spec: unchanged Player flow; loaded Crosshair NPC query;
exact NPC apply/replace/remove/edit/retry/lock; target refresh; non-Actor and
missing target; Actor unload; no Player fallback; stale-state isolation; and
external-slot read-only behavior.

- [ ] **Step 5: Record vNext.3 completion after user acceptance**

Check only the roadmap boxes proven by implementation and in-game acceptance,
set vNext.3 status to `Complete`, archive this plan, run `git diff --check`, and
request approval for `docs: complete native actor targeting milestone`.
