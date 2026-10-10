# Native Saved Tattoo Loadouts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persistent, global, ordered Saved Tattoo Loadouts that capture all four areas and restore exact managed slot placement with actor-scoped Preflight, one-time synchronization, and verified best-effort rollback.

**Architecture:** Define loadout values and batch results in Core, keep JSON persistence in a focused runtime store, and isolate deterministic planning plus forward/rollback orchestration behind `SlaveTatsService` and `ITattooRuntime`. The native workflow schedules capture, Preflight, restore, retry, and store requests while the adapter only presents intent and typed state.

**Tech Stack:** C++23, CommonLibSSE-NG, SlaveTatsNG API, JContainers, nlohmann/json, CMake/CTest, ImGui through SKSE Menu Framework.

**Spec:** `docs/superpowers/specs/2026-10-04-native-saved-tattoo-loadouts-design.md`

## Global Constraints

- One loadout covers Body, Face, Hands, and Feet and restores exact area/slot placement.
- Persist at most twenty globally shared loadouts in JSON array order.
- Persist domain, section, name, and six appearance values; never persist Actor ID, runtime handle, locked state, or glow/bump texture paths.
- External overlays remain read-only, and a target-slot external overlay rejects Preflight.
- Any locked managed tattoo requiring mutation rejects Preflight; an unchanged locked tattoo is permitted.
- Missing tattoos, insufficient slots, stale Actor state, or any blocking conflict reject the whole operation before mutation.
- Multi-slot mutation stays below the UI boundary, synchronizes once, and verifies all four areas.
- Forward or rollback synchronization-only retry never repeats completed writes.
- Rollback is verified best-effort and must distinguish complete from incomplete recovery.
- Never fall back to Player when an explicit Actor Target cannot be resolved.
- Loading a loadout changes neither Favorites nor Recently Used.
- Controlled alternate glow/bump texture selection remains out of scope.

## Review Focus

- A tattoo pack changes between preview and confirmation: execution must reject stale assumptions before writing.
- A locked tattoo already matches identity but differs only in appearance: Preflight must reject because a write is required.
- An external overlay occupies a non-target slot: it must be preserved and must not block restore.
- Forward writes finish but synchronization fails: retry must synchronize and verify without repeating mutation.
- Rollback writes finish but rollback synchronization fails: retry must synchronize and verify the original snapshot without repeating compensation.

---

### Task 1: Persistent Loadout Values and JSON Store

**Files:**
- Create: `src/core/TattooLoadoutModels.h`
- Create: `src/runtime/TattooLoadoutStore.h`
- Create: `src/runtime/TattooLoadoutStore.cpp`
- Create: `tests/runtime/TattooLoadoutStoreTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `core::TattooArea`, `runtime::PluginConfigFile`, and the Appearance Preset name/ordering conventions.
- Produces: `core::TattooLoadoutEntry`, `core::TattooLoadout`, `core::TattooLoadoutList`, `runtime::TattooLoadoutResult`, and `runtime::TattooLoadoutStore::{load,create,overwrite,rename,erase}`.

- [ ] **Step 1: Add the failing store round-trip and validation tests**

Define test fixtures with exact entries across all four areas and assert JSON order, deterministic tattoo ordering, empty-loadout support, case-folded duplicate rejection, duplicate `(area, slot)` rejection, invalid numeric rejection, unsupported-version preservation, the twenty-entry limit, and interleaved preservation of existing root members.

```cpp
const core::TattooLoadout crimson{
    .name = "Crimson Set",
    .entries = {{
        .area = core::TattooArea::body,
        .slot = 3,
        .domain = "default",
        .section = "Marks",
        .name = "Crimson Sigil",
        .color = 0xFFFFFF,
        .alpha = 1.0F,
        .glow = 0xFF0000,
        .emissiveMult = 2.0F,
        .glossiness = 250.0F,
        .specularStrength = 10.0F,
    }},
};
expect(*store.create(crimson) == core::TattooLoadoutList{crimson},
    "expected exact loadout round-trip");
```

- [ ] **Step 2: Register and run the new test target to verify RED**

Add `TattooLoadoutStoreTests` with `PluginConfigFile.cpp` and `TattooLoadoutStore.cpp` to `CMakeLists.txt`.

Run:

```powershell
cmake --build --preset debug --target TattooLoadoutStoreTests
```

Expected: compile failure because the new model/store interfaces do not exist.

- [ ] **Step 3: Define the persistent model and store API**

```cpp
namespace stui::core {
inline constexpr std::size_t kTattooLoadoutLimit = 20;

struct TattooLoadoutEntry {
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string domain{"default"};
    std::string section;
    std::string name;
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
    std::int32_t glow{};
    float emissiveMult{1.0F};
    float glossiness{};
    float specularStrength{};
    bool operator==(const TattooLoadoutEntry&) const = default;
};

struct TattooLoadout {
    std::string name;
    std::vector<TattooLoadoutEntry> entries;
    bool operator==(const TattooLoadout&) const = default;
};
using TattooLoadoutList = std::vector<TattooLoadout>;
}
```

Expose store methods matching `AppearancePresetStore`, returning
`std::expected<core::TattooLoadoutList, ConfigError>`.

- [ ] **Step 4: Implement versioned JSON parsing and atomic mutations**

Use root member `tattooLoadouts`, schema version `1`, outer array `entries`, and per-loadout array `tattoos`. Reuse the checked `PluginConfigFile::update` transaction. Trim names, fold names with deterministic ASCII lowercase, validate every field, reject the entire payload on one invalid entry, and sort only tattoo entries by fixed area order then slot during serialization.

- [ ] **Step 5: Run focused and config regression tests**

```powershell
cmake --build --preset debug --target TattooLoadoutStoreTests AppearancePresetStoreTests PluginConfigFileTests
ctest --test-dir build\debug -R "TattooLoadoutStoreTests|AppearancePresetStoreTests|PluginConfigFileTests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Review and request approval for the Task 1 commit**

Show the scoped diff and propose:

```text
feat: persist saved tattoo loadouts
```

Commit only after explicit approval.

---

### Task 2: Deterministic Preflight Planner

**Files:**
- Create: `src/core/TattooLoadoutPlanner.h`
- Create: `src/core/TattooLoadoutPlanner.cpp`
- Create: `tests/core/TattooLoadoutPlannerTests.cpp`
- Modify: `src/core/TattooLoadoutModels.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `core::TattooLoadout`, four fresh `core::TattooSlots` snapshots, and available `core::TattooEntry` definitions grouped by domain.
- Produces: `core::TattooLoadoutPlan`, `core::TattooLoadoutPlanAction`, `core::TattooLoadoutBlocker`, and `core::planTattooLoadout(...)`.

- [ ] **Step 1: Write failing plan classification and blocker tests**

Cover add, replace, remove, appearance-only, unchanged, empty loadout, all four areas, missing tattoo, insufficient configured slot count, target external conflict, conflicting locked tattoo, unchanged locked tattoo, unrelated external overlay, duplicate input position, actor mismatch, and the Review Focus case where a locked identity matches but appearance differs.

```cpp
const auto result = core::planTattooLoadout({
    .actorFormId = 0x14,
    .loadout = loadout,
    .slots = snapshots,
    .available = catalog,
});
expect(result && result->actions[0].kind == core::TattooLoadoutActionKind::appearance,
    "expected appearance-only action");
```

- [ ] **Step 2: Register and run the planner test to verify RED**

```powershell
cmake --build --preset debug --target TattooLoadoutPlannerTests
```

Expected: compile failure for missing planner types.

- [ ] **Step 3: Define immutable planning contracts**

```cpp
enum class TattooLoadoutActionKind { add, replace, remove, appearance, unchanged };
enum class TattooLoadoutBlockerCode {
    actorMismatch, invalidSlot, missingTattoo, externalConflict, lockedConflict
};

struct TattooLoadoutBlocker {
    TattooLoadoutBlockerCode code;
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string message;
};

struct TattooLoadoutPlanAction {
    TattooLoadoutActionKind kind;
    TattooArea area;
    std::int32_t slot;
    std::optional<TattooEntry> before;
    std::optional<TattooLoadoutEntry> after;
};

struct TattooLoadoutPlan {
    std::uint32_t actorFormId{};
    std::vector<TattooLoadoutPlanAction> actions;
    std::vector<TattooSlot> preservedExternal;
};

struct TattooLoadoutPlanningInput {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
    std::array<TattooSlots, 4> slots;
    std::vector<TattooEntry> available;
};

using TattooLoadoutPlanResult =
    std::expected<TattooLoadoutPlan, std::vector<TattooLoadoutBlocker>>;

TattooLoadoutPlanResult planTattooLoadout(
    const TattooLoadoutPlanningInput& input);
```

Keep blocker ordering deterministic by area, slot, then blocker code.

- [ ] **Step 4: Implement pure planning without runtime access**

Normalize the four snapshots into `(area, slot)` maps, resolve stored entries by exact domain plus runtime Tattoo Identity `(section, name)`, compare six appearance values with the same finite/range contract, preserve unrelated external slots, and reject before producing an executable plan when blockers exist.

- [ ] **Step 5: Run focused planner and Core regression tests**

```powershell
cmake --build --preset debug --target TattooLoadoutPlannerTests SlaveTatsUICoreTests
ctest --test-dir build\debug -R "TattooLoadoutPlannerTests|SlaveTatsUICoreTests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Review and request approval for the Task 2 commit**

```text
feat: plan exact tattoo loadout restores
```

Commit only after explicit approval.

---

### Task 3: Typed Core Batch Service Contract

**Files:**
- Modify: `src/core/TattooLoadoutModels.h`
- Modify: `src/core/ITattooRuntime.h`
- Modify: `src/core/SlaveTatsService.h`
- Modify: `src/core/SlaveTatsService.cpp`
- Modify: `tests/core/SlaveTatsServiceTests.cpp`

**Interfaces:**
- Consumes: Task 1 persistent values and Task 2 plan/blocker values.
- Produces: typed capture, preview, restore, and retry requests/results exposed by `SlaveTatsService` and `ITattooRuntime`.

- [ ] **Step 1: Write failing service validation and delegation tests**

Assert zero Actor ID, malformed loadout entry, duplicate position, invalid appearance, and unsupported retry mode are rejected without a runtime call. Assert valid capture/preview/restore requests delegate unchanged and preserve explicit Crosshair Target IDs.

```cpp
const auto result = service.previewLoadout({
    .actorFormId = 0,
    .loadout = loadout,
});
expect(!result && result.error().code == ServiceErrorCode::actorNotFound,
    "expected zero actor rejected before runtime");
expect(runtime.previewCalls == 0, "expected no runtime delegation");
```

- [ ] **Step 2: Run Core tests to verify RED**

```powershell
cmake --build --preset debug --target SlaveTatsUICoreTests
```

Expected: compile failure for missing service methods.

- [ ] **Step 3: Add exact typed request and result APIs**

```cpp
struct CaptureTattooLoadoutRequest { std::uint32_t actorFormId{}; };
struct PreviewTattooLoadoutRequest {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
};
struct RestoreTattooLoadoutRequest {
    std::uint32_t actorFormId{};
    TattooLoadout loadout;
};

using TattooLoadoutRetryId = std::uint64_t;

struct TattooLoadoutCaptureValue {
    std::uint32_t actorFormId{};
    std::vector<TattooLoadoutEntry> entries;
};

struct TattooLoadoutPreviewValue {
    std::uint32_t actorFormId{};
    TattooLoadoutPlan plan;
};

struct TattooLoadoutAffectedSlot {
    TattooArea area{TattooArea::body};
    std::int32_t slot{-1};
    std::string message;
};

struct RetryTattooLoadoutSynchronizationRequest {
    std::uint32_t actorFormId{};
    TattooLoadoutRetryId retryId{};
};

enum class TattooLoadoutRestoreState {
    success,
    preflightRejected,
    forwardSynchronizationPending,
    rollbackSynchronizationPending,
    rollbackComplete,
    rollbackIncomplete
};

struct TattooLoadoutRestoreResultValue {
    std::uint32_t actorFormId{};
    TattooLoadoutRestoreState state{TattooLoadoutRestoreState::success};
    std::vector<TattooLoadoutBlocker> blockers;
    std::vector<TattooLoadoutAffectedSlot> affectedSlots;
    bool synchronizationRetryAvailable{};
    std::optional<TattooLoadoutRetryId> retryId;
};

using CaptureTattooLoadoutResult =
    std::expected<TattooLoadoutCaptureValue, ServiceError>;
using PreviewTattooLoadoutResult =
    std::expected<TattooLoadoutPreviewValue, ServiceError>;
using RestoreTattooLoadoutResult =
    std::expected<TattooLoadoutRestoreResultValue, ServiceError>;
```

Expose `captureLoadout`, `previewLoadout`, `restoreLoadout`, and
`retryLoadoutSynchronization` on the service/runtime boundary. The retry request carries only an opaque session-local `TattooLoadoutRetryId` plus the explicit Actor ID; no raw Actor pointer is exposed.

- [ ] **Step 4: Implement shared request validation and delegation**

Factor one Core-only validator for loadout entries so store, service tests, and runtime assumptions use identical field rules without introducing a dependency from Core to runtime persistence.

- [ ] **Step 5: Run Core tests**

```powershell
cmake --build --preset debug --target SlaveTatsUICoreTests
ctest --test-dir build\debug -R SlaveTatsUICoreTests --output-on-failure
```

Expected: pass.

- [ ] **Step 6: Review and request approval for the Task 3 commit**

```text
feat: add tattoo loadout service contracts
```

Commit only after explicit approval.

---

### Task 4: Isolated Restore and Rollback Orchestration

**Files:**
- Create: `src/runtime/TattooLoadoutOrchestration.h`
- Create: `src/runtime/TattooLoadoutOrchestration.cpp`
- Create: `tests/runtime/TattooLoadoutOrchestrationTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Core Loadout Plan and typed result contracts.
- Produces: `runtime::ITattooLoadoutBackend`, `runtime::TattooLoadoutRetryRegistry`, and `runtime::executeTattooLoadout(...)`.

- [ ] **Step 1: Write failing orchestration tests with a deterministic fake backend**

Exercise successful writes with one synchronize, stale revalidation before mutation, failure before any write, failure after each action index, forward sync failure, final verification failure, complete rollback, rollback write failure, rollback sync failure, and both synchronization-only retry paths.

```cpp
backend.failForwardSync = true;
const auto result = runtime::executeTattooLoadout(request, backend, retries);
expect(result->state == core::TattooLoadoutRestoreState::forwardSynchronizationPending,
    "forward sync failure must expose synchronization-only retry");
expect(backend.forwardWrites == expectedWrites, "writes must run once");
expect(result->retryId.has_value(), "expected opaque retry context");
```

Add explicit Review Focus assertions that forward-sync retry and rollback-sync retry do not increment write counters.

- [ ] **Step 2: Register and run orchestration tests to verify RED**

```powershell
cmake --build --preset debug --target TattooLoadoutOrchestrationTests
```

Expected: compile failure for missing orchestration interfaces.

- [ ] **Step 3: Define a narrow backend seam and retry registry**

```cpp
namespace stui::core {
struct TattooLoadoutRollbackSnapshot {
    std::uint32_t actorFormId{};
    std::array<TattooSlots, 4> areas;
};

struct TattooLoadoutVerification {
    bool matches{};
    std::vector<TattooLoadoutAffectedSlot> affectedSlots;
};
}

namespace stui::runtime {
enum class TattooLoadoutRetryPhase { forwardSync, rollbackSync };

struct TattooLoadoutRetryContext {
    core::TattooLoadoutRetryId id{};
    std::uint32_t actorFormId{};
    TattooLoadoutRetryPhase phase{TattooLoadoutRetryPhase::forwardSync};
    core::TattooLoadoutPlan forwardPlan;
    core::TattooLoadoutRollbackSnapshot rollbackSnapshot;
};

class TattooLoadoutRetryRegistry {
public:
    core::TattooLoadoutRetryId store(TattooLoadoutRetryContext context);
    std::optional<TattooLoadoutRetryContext> take(
        core::TattooLoadoutRetryId id,
        std::uint32_t actorFormId);
    void erase(core::TattooLoadoutRetryId id) noexcept;
};

class ITattooLoadoutBackend {
public:
    virtual ~ITattooLoadoutBackend() = default;
    virtual core::TattooLoadoutPlanResult preflight(
        const core::PreviewTattooLoadoutRequest&) = 0;
    virtual std::expected<core::TattooLoadoutRollbackSnapshot, core::ServiceError>
        captureRollback(const core::TattooLoadoutPlan&) = 0;
    virtual std::expected<void, core::ServiceError> applyAction(
        const core::TattooLoadoutPlanAction&) = 0;
    virtual bool synchronize(std::uint32_t actorFormId) = 0;
    virtual core::TattooLoadoutVerification verifyForward(
        const core::TattooLoadoutPlan&) = 0;
    virtual std::expected<void, core::ServiceError> applyRollback(
        const core::TattooLoadoutRollbackSnapshot&) = 0;
    virtual core::TattooLoadoutVerification verifyRollback(
        const core::TattooLoadoutRollbackSnapshot&) = 0;
};
}
```

The retry registry stores opaque contexts keyed by monotonically increasing IDs. Each context copies Actor ID, phase (`forwardSync` or `rollbackSync`), and expected verification state; it stores no raw Actor pointer. Consuming a successful retry erases the context. Actor mismatch rejects and erases it.

- [ ] **Step 4: Implement the explicit state machine**

Use the order `re-preflight -> rollback snapshot -> forward actions -> forward sync -> forward verify`. A forward action or verification failure after possible side effects runs `rollback writes -> rollback sync -> rollback verify`. A forward synchronization failure does not repeat writes or begin rollback; it returns transient `forwardSynchronizationPending` with a synchronization-only retry context. A rollback synchronization failure returns transient `rollbackSynchronizationPending`. Successful retry synchronizes and verifies the stored expected state, then returns one of the four terminal spec outcomes. Create a retry context only when all writes for that phase are proven complete.

- [ ] **Step 5: Run focused orchestration tests**

```powershell
cmake --build --preset debug --target TattooLoadoutOrchestrationTests
ctest --test-dir build\debug -R TattooLoadoutOrchestrationTests --output-on-failure
```

Expected: pass with write/synchronize counters proving no repeated mutation.

- [ ] **Step 6: Review and request approval for the Task 4 commit**

```text
feat: orchestrate tattoo loadout rollback
```

Commit only after explicit approval.

---

### Task 5: SlaveTatsRuntime Batch Backend

**Files:**
- Modify: `src/runtime/SlaveTatsRuntime.h`
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `src/runtime/ApplicationRuntime.h`
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`
- Modify: `tests/runtime/ApplicationRuntimeTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 `ITattooRuntime` methods and Task 4 orchestration/backend interfaces.
- Produces: production capture, Preflight, restore, verification, rollback, and retry behavior using SlaveTatsNG/JContainers only inside `SlaveTatsRuntime`.

- [ ] **Step 1: Add failing runtime tests for four-area capture and exact batch restore**

Extend runtime test bindings so fake SlaveTatsNG/JContainers state can expose configured slots, external slots, catalog definitions, mutation counters, synchronization failures, and actor unload. Assert exact Actor IDs and no Player fallback.

```cpp
const auto result = runtime.restoreLoadout({.actorFormId = 0x1234, .loadout = loadout});
expect(result && result->state == core::TattooLoadoutRestoreState::success,
    "expected exact restore");
expect(state.synchronizeCalls == 1, "expected one batch synchronization");
expect(state.playerMutationCalls == 0, "expected no Player fallback");
```

- [ ] **Step 2: Run runtime targets to verify RED**

```powershell
cmake --build --preset debug --target SlaveTatsRuntimeAppearanceTests ApplicationRuntimeTests
```

Expected: compile failure until the new virtual methods are implemented.

- [ ] **Step 3: Implement fresh four-area capture and Preflight inputs**

Resolve the explicit loaded Actor once per scheduled operation, query configured/external/managed slots for all areas, query each required domain's available tattoos, and feed copied values into the pure planner. Never retain the `RE::Actor*` beyond the operation.

- [ ] **Step 4: Implement no-sync mutation primitives and verified appearance writes**

Extract internal helpers from existing single-slot apply/remove/update paths without changing their public behavior. Batch helpers mutate one planned slot without calling synchronize; validate session-local handles against the requested Actor immediately before appearance or rollback writes.

- [ ] **Step 5: Implement rollback snapshot and verification adapters**

Capture every mutable field required to reconstruct original managed slots, including runtime texture metadata used only for recovery. Preserve external slots by excluding them from writes and verifying their occupancy after synchronization. Translate incomplete verification into sorted affected area/slot values.

- [ ] **Step 6: Run runtime, Core, and existing mutation regressions**

```powershell
cmake --build --preset debug --target SlaveTatsRuntimeAppearanceTests ApplicationRuntimeTests SlaveTatsUICoreTests UpdateTattooAppearanceOrchestrationTests
ctest --test-dir build\debug -R "SlaveTatsRuntimeAppearanceTests|ApplicationRuntimeTests|SlaveTatsUICoreTests|UpdateTattooAppearanceOrchestrationTests|SlaveTatsApplyUpgradePolicyTests" --output-on-failure
```

Expected: pass, including existing single-slot synchronization-only behavior.

- [ ] **Step 7: Review and request approval for the Task 5 commit**

```text
feat: restore tattoo loadouts atomically
```

Commit only after explicit approval.

---

### Task 6: Workflow Model and Scheduler Integration

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`

**Interfaces:**
- Consumes: `TattooLoadoutStore` result APIs and Task 3 service requests/results.
- Produces: loadout selection/management state, capture summary, Preflight preview, confirmation, progress, rollback result, and generation-scoped scheduler tickets.

- [ ] **Step 1: Write failing workflow state-transition tests**

Cover initialization, ordered selection, create/overwrite/rename/delete, limit state, capture across four areas, Preflight preview counts, blocker presentation state, confirmation, restore success, rollback complete/incomplete, synchronization retry, Selected Area preservation, all-cache refresh, target change invalidation, stale completion rejection, and single-in-flight exclusion.

```cpp
expect(model.requestLoadoutPreview(), "expected preview request");
model.completeLoadoutPreview(oldGeneration, preview);
expect(!model.loadoutPreview(), "stale preview must not affect a newer target");
```

Assert no workflow transition enqueues a `RecentTattooTicket` or Favorite request.

- [ ] **Step 2: Run native model/runtime tests to verify RED**

```powershell
cmake --build --preset debug --target NativeSlotWorkflowModelTests NativeSlotWorkflowRuntimeTests
```

Expected: compile failure for missing loadout tickets/state.

- [ ] **Step 3: Add explicit request and screen state types**

```cpp
enum class TattooLoadoutRequestKind { load, create, overwrite, rename, erase };
enum class TattooLoadoutScreen {
    none, captureSummary, preview, restoring, result
};

enum class TattooLoadoutActorOperation {
    capture, preview, restore, retrySynchronization
};

struct TattooLoadoutStoreTicket {
    std::uint64_t requestId{};
    TattooLoadoutRequestKind kind{TattooLoadoutRequestKind::load};
    std::optional<core::TattooLoadout> loadout;
    std::string existingName;
};

struct TattooLoadoutActorTicket {
    std::uint64_t targetGeneration{};
    std::uint32_t actorFormId{};
    TattooLoadoutActorOperation operation;
    std::optional<core::TattooLoadout> loadout;
    std::optional<core::TattooLoadoutRetryId> retryId;
};
```

Keep store request IDs independent from Actor Target Generation. Actor tickets require both matching generation and explicit form ID.

- [ ] **Step 4: Implement deterministic model transitions**

Keep the current Selected Area before loadout screens, invalidate actor-scoped previews/retries on target change, disable all tattoo mutations while restoring, accept committed store lists only from the active request ID, and queue refreshes for all four areas after success or any rollback outcome.

- [ ] **Step 5: Implement scheduler operations and completion mailboxes**

Inject store, capture, preview, restore, and retry callables into `NativeSlotWorkflowRuntime`. Convert exceptions and scheduler rejection into typed completions. Retain opaque retry contexts in the runtime layer and expose only retry availability/intent to the adapter.

- [ ] **Step 6: Run workflow and existing feature regressions**

```powershell
cmake --build --preset debug --target NativeSlotWorkflowModelTests NativeSlotWorkflowRuntimeTests NativeCatalogBrowserModelTests
ctest --test-dir build\debug -R "NativeSlotWorkflowModelTests|NativeSlotWorkflowRuntimeTests|NativeCatalogBrowserModelTests" --output-on-failure
```

Expected: pass.

- [ ] **Step 7: Review and request approval for the Task 6 commit**

```text
feat: coordinate tattoo loadout workflows
```

Commit only after explicit approval.

---

### Task 7: Native UI and Production Composition

Implementation prerequisite discovered during UI integration: carry typed blockers
in `TattooLoadoutPreviewValue` and pass the approved plan through the workflow
ticket and service restore request. Compare fresh planning state (including resolved
pack metadata, excluding catalog session handles) before any mutation. These
narrow Core/runtime changes complete the existing blocked-preview and stale-plan
contracts; they do not introduce another roadmap feature.

Result acknowledgement remains blocked until every area snapshot has loaded.
If one area query fails, finish querying the other areas and offer a read-only
four-area refresh retry without repeating the completed restore or rollback.

Environment prerequisite on the restored Windows host: retain a configured
`VCPKG_ROOT` when importing the MSVC environment in `build.ps1`. Review this
build-script fix separately from the Loadouts feature commit.

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Modify: `src/main.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 6 model state and tickets, `TattooLoadoutStore`, and `SlaveTatsService` batch methods.
- Produces: Current Slots Loadouts controls, capture/name/manage views, grouped Preflight preview, blocked reasons, progress, typed result presentation, and fully wired production dependencies.

- [ ] **Step 1: Write failing adapter presentation tests**

Add helpers/tests for empty state, ordered Dropdown labels, limit-disabled creation, per-area capture counts, grouped action counts, external `Preserved`, blockers with area/slot/reason, progress without Cancel, success, rollback complete, rollback incomplete, stable footer roles, and scrollable content above an anchored footer.

```cpp
const auto summary = native::summarizeLoadoutPlan(plan);
expect(summary.body.add == 1 && summary.face.remove == 1,
    "expected grouped loadout summary");
expect(native::loadoutResultLabel(incomplete) == "Rollback incomplete",
    "expected explicit incomplete recovery label");
```

- [ ] **Step 2: Run adapter tests to verify RED**

```powershell
cmake --build --preset debug --target OfficialMenuFrameworkAdapterTests
```

Expected: compile failure for missing presentation helpers.

- [ ] **Step 3: Implement Loadouts controls and dedicated screens**

Render `Load`, `Save Current`, and `Manage` on Current Slots. Keep `Back` left, contextual actions in the middle, and `Cancel`/`Close` bottom-right. Use a child content region above a fixed footer. Disable `Apply Loadout` for blockers and hide Cancel only after restore mutation begins.

- [ ] **Step 4: Wire persistence and batch operations in `main.cpp`**

Create one `TattooLoadoutStore` from the existing shared `PluginConfigFile`. Inject switch-based store operations and service capture/preview/restore/retry lambdas into `NativeSlotWorkflowRuntime`. Do not call JContainers or SlaveTatsNG from composition lambdas beyond the service boundary.

- [ ] **Step 5: Run adapter and composition regressions**

```powershell
cmake --build --preset debug --target OfficialMenuFrameworkAdapterTests NativeSlotWorkflowModelTests NativeSlotWorkflowRuntimeTests ApplicationRuntimeTests
ctest --test-dir build\debug -R "OfficialMenuFrameworkAdapterTests|NativeSlotWorkflowModelTests|NativeSlotWorkflowRuntimeTests|ApplicationRuntimeTests" --output-on-failure
```

Expected: pass and existing footer/Edit Appearance tests remain unchanged.

- [ ] **Step 6: Review and request approval for the Task 7 commit**

```text
feat: add native tattoo loadout controls
```

Commit only after explicit approval.

---

### Task 8: Full Verification and In-Game Acceptance Handoff

**Files:**
- Modify after acceptance only: `ROADMAP.md`
- Move after acceptance only: `docs/superpowers/plans/active/2026-10-05-native-saved-tattoo-loadouts.md` to `docs/superpowers/plans/archive/2026-10-05-native-saved-tattoo-loadouts.md`

**Interfaces:**
- Consumes: all completed implementation tasks.
- Produces: verified Debug/Release artifacts and a bounded manual acceptance checklist; roadmap/archive changes occur only after in-game acceptance.

- [ ] **Step 1: Run the complete Debug build and suite**

```powershell
.\build.ps1 -Config debug
ctest --test-dir build\debug --output-on-failure
```

Expected: build succeeds and every Debug test passes.

- [ ] **Step 2: Run the complete Release build and suite**

```powershell
.\build.ps1 -Config release
ctest --test-dir build\release --output-on-failure
```

Expected: build succeeds and every Release test passes.

- [ ] **Step 3: Run repository hygiene checks**

```powershell
git diff --check
git status --short
```

Scan the scoped diff for secrets, generated files, unrelated edits, accidental release/version changes, and any modification to the already committed Hotkey fix.

- [ ] **Step 4: Request separate approval before deployment**

Show the exact Release DLL path/hash and proposed MO2 destination. Confirm Skyrim and SKSE are closed, back up the installed DLL, copy only to the dedicated MO2 mod, and compare SHA-256 hashes. Do not deploy without explicit approval.

- [ ] **Step 5: Execute the manual acceptance checklist**

Verify Player and Crosshair Target capture/restore, empty/full loadouts, JSON order, restart persistence, missing pack, insufficient slot count, external target conflict, unrelated external preservation, locked conflict, unchanged locked slot, forward sync retry, rollback-complete and rollback-incomplete presentation, Favorites/Recently Used preservation, and stable footers.

- [ ] **Step 6: Close documentation only after accepted in-game results**

Change only the Saved Tattoo Sets / Loadouts checkbox in `ROADMAP.md` to checked, add a concise acceptance record to the spec, and move this completed plan to archive. Keep controlled alternate glow/bump texture selection unchecked.

- [ ] **Step 7: Review and request approval for the documentation commit**

```text
docs: complete saved tattoo loadouts
```

Commit only after explicit approval. Push, merge, deployment, release, and tag creation each remain separate approval gates.
