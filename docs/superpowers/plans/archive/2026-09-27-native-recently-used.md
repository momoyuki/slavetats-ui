# Native Recently Used Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent, area-scoped Recently Used history that records only successful Apply/Replace operations and provides a composable newest-first Picker filter.

**Architecture:** Introduce a repository identity and a dedicated `RecentTattooStore` that shares `PluginConfigFile` with Hotkey and Favorites. The workflow copies the selected definition into the Apply ticket, records it only after mutation and synchronization succeed, and serializes history writes through the existing scheduler/completion-mailbox boundary. The catalog model intersects recent membership with existing filters and switches to history order only while Recently Used is active.

**Tech Stack:** Windows C++23, CMake/Ninja/MSVC, nlohmann JSON, Dear ImGui through SKSE Menu Framework, injected runtime scheduler, standalone CTest executables. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-27-native-recently-used-design.md`

**Execution status (2026-09-30):** Implementation, Debug and Release
verification, Release deployment, and in-game acceptance are complete. The
final retention contract keeps the six newest entries per area while accepting
legacy version 1 histories containing up to ten entries. This plan is archived
as the implementation record.

## Global Constraints

- SlaveTatsNG remains authoritative for tattoo mutation and synchronization.
- Native UI and deterministic workflow models perform no filesystem, JContainers, or SlaveTatsNG access.
- Recent identity is exactly `(domain, sourceId, section, name, area)`; area values serialize as `Body`, `Face`, `Hands`, or `Feet`.
- Keep at most six newest entries per area; repeat use promotes without duplication and does not reorder other areas.
- Record only after Apply or Replace and required synchronization succeed. Preview, Cancel, Remove, Edit Appearance, Lock, rejected/stale operations, and failed mutation do not record.
- A synchronization-only retry must never repeat a completed Apply mutation.
- Hotkey, Favorites, and Recently Used share one `PluginConfigFile` transaction boundary and preserve unrelated JSON keys.
- History errors are non-blocking and separate from tattoo-operation and Favorites errors. Retry performs only the failed history write.
- Existing Applied-only, Favorites, external-overlay, actor-generation, and thumbnail safety behavior must remain intact.
- Code, identifiers, comments, documentation, and commit messages remain English.
- Every commit, push, and deployment remains a separate approval gate. Show the scoped diff and exact Conventional Commit message before each commit; never add attribution trailers.

## Review Focus

1. Apply synchronization failure after `add_and_get_tattoo` must retry synchronization only, then record exactly once: Tasks 1 and 4.
2. Multiple successful Apply/Replace completions while history persistence is pending must preserve completion order without losing or duplicating entries: Tasks 4 and 5.
3. Ten-entry eviction must affect only the recorded area, including unavailable-pack identities: Task 2.
4. Combining Recently Used with Favorites, Applied-only, contextual facets, and pagination must retain newest-first order and valid page references: Tasks 3 and 6.
5. Malformed/unsupported history and write failure must preserve the original configuration and leave tattoo success untouched: Tasks 2, 4, and 5.

## Preflight Finding

The current Apply contract has no synchronization-only mode. `SlaveTatsRuntime::applyToSlot` calls `add_and_get_tattoo` and then synchronization; on synchronization failure the workflow returns to Preview, so confirming again can repeat the mutation. Remove and Appearance already preserve synchronization-only retry, but Apply does not. Task 1 is a required safety prerequisite for the approved Recently Used recording boundary, not a separate convenience feature.

---

### Task 1: Add Safe Apply Synchronization-Only Retry

**Files:**
- Modify: `src/core/TattooModels.h`
- Modify: `src/core/SlaveTatsService.cpp`
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Test: `tests/core/SlaveTatsServiceTests.cpp`
- Test: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Produces: `core::ApplyTattooMode::{applyAndSynchronize,synchronizeOnly}` and `ApplyTattooRequest::mode`.
- Produces: workflow retry that retains the selected tattoo/slot and switches only synchronization failures to `synchronizeOnly`.
- Consumed by: Task 4's rule that recent history is emitted once after final Apply success.

- [ ] **Step 1: Add failing Core and runtime tests for the new mode**

Add assertions that `SlaveTatsService` forwards `synchronizeOnly`, and that `SlaveTatsRuntime` skips external-slot lookup, available-tattoo lookup, template mutation, and `add_and_get_tattoo` while still setting `.SlaveTats.updated` and calling `synchronize_tattoos`.

```cpp
ApplyTattooRequest retry = validApplyRequest();
retry.mode = ApplyTattooMode::synchronizeOnly;
const auto result = service.applyToSlot(retry);
expect(result.has_value(), "expected synchronization-only Apply retry forwarded");
expect(runtime.appliedRequest.mode == ApplyTattooMode::synchronizeOnly,
       "expected exact Apply retry mode");
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "SlaveTatsUICoreTests|SlaveTatsRuntimeAppearanceTests" --output-on-failure
```

Expected: compile/test failure because `ApplyTattooMode` and `ApplyTattooRequest::mode` do not exist.

- [ ] **Step 3: Add the minimal typed mode and runtime branch**

Implement this contract and validate all existing fields for a full Apply while requiring only actor/area for synchronization-only retry:

```cpp
enum class ApplyTattooMode { applyAndSynchronize, synchronizeOnly };

struct ApplyTattooRequest {
    // existing fields
    ApplyTattooMode mode{ApplyTattooMode::applyAndSynchronize};
};
```

In `SlaveTatsRuntime::applyToSlot`, place external-slot checks, catalog lookup, appearance guard, and `add_and_get_tattoo` inside `applyAndSynchronize`; both modes set the updated flag and synchronize. Mark synchronization failure as `MutationSideEffect::mayHaveOccurred` for a full Apply and `none` for a synchronization-only retry.

- [ ] **Step 4: Add failing workflow tests for retry behavior**

Test that a synchronization failure returns to Preview with the same slot and tattoo, the next `confirmApply()` emits a newer generation with `mode == synchronizeOnly`, a non-synchronization failure remains `applyAndSynchronize`, and success clears retry state.

```cpp
model.completeApply(first->generation, std::unexpected(ServiceError{
    ServiceErrorCode::synchronizeFailed,
    "apply sync failed",
    MutationSideEffect::mayHaveOccurred,
}));
expect(model.confirmApply(), "expected synchronization retry accepted");
const auto retry = model.takeApplyRequest();
expect(retry && retry->request.mode == ApplyTattooMode::synchronizeOnly,
       "expected Apply mutation not to repeat");
```

- [ ] **Step 5: Implement workflow retry state and run the focused suite**

Add `m_applyRequiresSynchronizationOnly`, set it only for `synchronizeFailed`/post-mutation failure, copy it into the next request mode, and clear it after success, Actor invalidation, or a newly selected tattoo. Do not change Remove or Appearance retry behavior.

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "SlaveTatsUICoreTests|SlaveTatsRuntimeAppearanceTests|NativeSlotWorkflowModelTests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Review the diff and propose, but do not run without explicit approval:

```text
fix: retry apply synchronization without repeating mutation
```

### Task 2: Add Recent Identity and Persistent Store

**Files:**
- Create: `src/repository/RecentTattooIdentity.h`
- Create: `src/runtime/RecentTattooStore.h`
- Create: `src/runtime/RecentTattooStore.cpp`
- Create: `tests/runtime/RecentTattooStoreTests.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/runtime/HotkeyBindingTests.cpp`
- Test: `tests/runtime/FavoriteStoreTests.cpp`

**Interfaces:**
- Consumes: existing `runtime::PluginConfigFile` and `runtime::ConfigError`.
- Produces: `RecentTattooIdentity`, `RecentTattooList`, `RecentTattooResult`, `RecentTattooStore::load()`, and `RecentTattooStore::record(identity)`.
- Consumed by: Tasks 3–5.

```cpp
namespace stui::repository {
struct RecentTattooIdentity {
    std::string domain, sourceId, section, name;
    core::TattooArea area{core::TattooArea::body};
    bool operator==(const RecentTattooIdentity&) const = default;
};
RecentTattooIdentity recentTattooIdentity(
    const TattooDefinition& tattoo,
    core::TattooArea area);
}

namespace stui::runtime {
inline constexpr std::size_t kRecentTattooLimitPerArea = 6;
using RecentTattooList = std::vector<repository::RecentTattooIdentity>;
using RecentTattooResult = std::expected<RecentTattooList, ConfigError>;
class RecentTattooStore {
public:
    explicit RecentTattooStore(std::shared_ptr<PluginConfigFile> file);
    [[nodiscard]] RecentTattooResult load();
    [[nodiscard]] RecentTattooResult record(
        const repository::RecentTattooIdentity& identity);
};
}
```

- [ ] **Step 1: Write failing persistence and validation tests**

Cover missing file/key without creation, save/reopen, canonical area strings, Unicode, exact five-field distinctions, duplicate promotion, newest-first order, six-item per-area eviction, legacy ten-item compatibility, preservation of other-area order, unavailable identities, malformed roots, invalid/empty fields, unsupported versions, duplicate canonicalization, and overflow rejection.

```cpp
for (int index = 0; index < 11; ++index) {
    expect(store.record(recent("Body " + std::to_string(index), TattooArea::body)),
           "expected Body history update");
}
expect(store.record(recent("Face 0", TattooArea::face)),
       "expected independent Face history update");
const auto loaded = store.load();
expect(countArea(*loaded, TattooArea::body) == 6 &&
       countArea(*loaded, TattooArea::face) == 1,
       "expected six entries per area without cross-area eviction");
```

- [ ] **Step 2: Add interleaving tests against the shared file**

Use new store instances over the same `PluginConfigFile`: change Hotkey, add Favorite, record Recent, then repeat in another order. Assert every setting and an unknown root key survive. Verify an unsupported `recentlyUsed` payload is preserved by Hotkey/Favorite updates while Recent operations fail.

- [ ] **Step 3: Run the new target and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "RecentTattooStoreTests|HotkeyBindingTests|FavoriteStoreTests" --output-on-failure
```

Expected: configuration/build failure until the new files and target exist.

- [ ] **Step 4: Implement schema parsing and atomic record updates**

Use `PluginConfigFile::read()` for load and `PluginConfigFile::update()` for record. Re-read on every record, remove the exact duplicate, insert at the front, erase only the seventh-and-later entries of the same area, serialize newest-first, and return the committed canonical list only after checked replacement succeeds. Accept legacy version 1 histories with up to ten unique entries per area by exposing the six newest and canonicalizing on the next successful record. Do not add a second mutex around the shared document.

- [ ] **Step 5: Register sources and run persistence tests**

Add the header/source to the plugin and a `RecentTattooStoreTests` executable linked to nlohmann JSON. Run the Step 3 command; expected all selected tests pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Proposed message:

```text
feat: persist area-scoped recently used tattoos
```

### Task 3: Add Recent Filtering, Ordering, and Browser State

**Files:**
- Modify: `src/repository/TattooRepository.h`
- Modify: `src/repository/TattooRepository.cpp`
- Modify: `src/native/NativeCatalogBrowserModel.h`
- Modify: `src/native/NativeCatalogBrowserModel.cpp`
- Test: `tests/repository/TattooRepositoryTests.cpp`
- Test: `tests/native/NativeCatalogBrowserModelTests.cpp`

**Interfaces:**
- Consumes: Task 2's `RecentTattooIdentity` and ordered `RecentTattooList`.
- Produces: optional recent restriction in `TattooFilter`; browser recent state and newest-first query behavior.
- Consumed by: Tasks 4 and 6.

```cpp
struct TattooFilter {
    // existing fields
    std::optional<std::vector<RecentTattooIdentity>> recentIdentities;
};

void setRecentlyUsedOnly(bool enabled);
void setRecentTattooIdentities(std::vector<RecentTattooIdentity> identities);
[[nodiscard]] bool recentlyUsedOnly() const noexcept;
```

- [ ] **Step 1: Write failing repository tests for membership and ordering**

Build a catalog whose natural order differs from history order. Assert inactive history preserves catalog order; active empty history matches none; active history selects exact domain/source/section/name/area and yields newest-first results. Cover same-name/different-source/domain, mismatched area, missing-pack identities, and intersections with search, domain, source, section, Applied-only, and Favorites-only before pagination.

- [ ] **Step 2: Write failing browser tests for state transitions**

Test toggle reset to page one, Selected Area reset, committed history preserving/clamping the requested page, catalog replacement retaining history/toggle, recent ordering across two pages, contextual facets, selected-label preservation, and session reset/navigation behavior.

```cpp
model.setRecentTattooIdentities(recentOrder);
model.setRecentlyUsedOnly(true);
expect(model.page().entries[0].name == "Newest" &&
       model.page().entries[1].name == "Older",
       "expected history order to replace catalog order");
```

- [ ] **Step 3: Run focused tests and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "TattooRepositoryTests|NativeCatalogBrowserModelTests" --output-on-failure
```

Expected: compile failure for missing recent filter/browser API.

- [ ] **Step 4: Implement filtering before pagination and conditional ordering**

Create an order lookup from the area-filtered recent vector only when `recentIdentities` is present. Intersect all predicates first, stable-sort surviving definitions by recent position, then compute matched count/pages. Preserve existing ordering when the optional restriction is absent. Update contextual facets using the same eligible-set logic.

- [ ] **Step 5: Implement browser state and run focused tests**

Retain the full ordered history independently of the toggle. Set `filter.recentIdentities` to absent when off and the selected-area subset when on. Re-query after committed history updates without resetting the page, then rely on query clamping. Run Step 3; expected all selected tests pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Proposed message:

```text
feat: filter catalog by recently used tattoos
```

### Task 4: Record Successful Use in Workflow State

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Consumes: Task 1's Apply retry mode, Task 2's identity/result types, and Task 3's browser setters.
- Produces: copied identity on `SlotApplyTicket`; serialized `RecentTattooTicket` queue; retry/error API.
- Consumed by: Task 5 runtime and Task 6 adapter.

```cpp
struct SlotApplyTicket {
    std::uint64_t generation{};
    core::ApplyTattooRequest request;
    repository::RecentTattooIdentity recentIdentity;
};

struct RecentTattooTicket {
    std::uint64_t requestId{};
    enum class Kind { load, record } kind{Kind::record};
    std::optional<repository::RecentTattooIdentity> identity;
};

void initializeRecentTattoos();
[[nodiscard]] std::optional<RecentTattooTicket> takeRecentTattooRequest();
void completeRecentTattoo(std::uint64_t requestId, runtime::RecentTattooResult result);
[[nodiscard]] bool retryRecentTattoo();
[[nodiscard]] bool recentlyUsedPending() const noexcept;
[[nodiscard]] const runtime::ConfigError* recentlyUsedError() const noexcept;
void setRecentlyUsedOnly(bool value);
[[nodiscard]] bool recentlyUsedOnly() const noexcept;
```

- [ ] **Step 1: Write failing tests for recording boundary**

Assert Preview/Cancel and failed full Apply enqueue nothing; successful Add and Replace enqueue the exact copied identity/area; a sync failure enqueues nothing; successful synchronization-only retry enqueues once. Change catalog, Actor, Area, and navigation before completion and verify the copied identity remains correct without restoring stale UI state.

- [ ] **Step 2: Write failing tests for load, queue, failure, and retry**

Start with `initializeRecentTattoos()`: assert it emits one load ticket, disables a trustworthy Recent-only empty result until completion, publishes a successful load, and retries a failed load as another load rather than a write. Then complete several successful Applies while a history record ticket is active. Assert different identities preserve completion order, adjacent duplicates coalesce, one ticket is active, success publishes committed history to the browser, failure leaves browser history unchanged, and Retry uses a fresh request ID for the failed identity before queued entries.

```cpp
model.completeRecentTattoo(ticket->requestId, std::unexpected(ConfigError{
    .message = "history write failed"}));
expect(model.error() == nullptr && model.recentlyUsedError(),
       "expected non-blocking history error separate from tattoo errors");
expect(model.retryRecentTattoo(), "expected history-only retry");
```

- [ ] **Step 3: Run the model test and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "NativeSlotWorkflowModelTests" --output-on-failure
```

Expected: compile failure for missing tickets and history state.

- [ ] **Step 4: Implement copied Apply context and serial history queue**

Construct `recentIdentity` from the selected `TattooDefinition` plus `m_selectedArea` in `confirmApply()`. Retain the active ticket context after `takeApplyRequest()`. Enqueue only in the accepted successful `completeApply()` branch. Keep an explicit load state, a `std::deque<RecentTattooIdentity>`, and active/failed tickets; coalesce only an adjacent duplicate. Do not bind history request IDs to Actor Target Generation.

- [ ] **Step 5: Implement completion/retry publication and run tests**

On success, replace browser history with the store's committed list, clear error/failed state, then expose the next queued ticket. On failure, preserve committed browser state and queue, expose a separate error, and wait for explicit Retry. Ignore duplicate/out-of-order request IDs. Run Step 3; expected pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Proposed message:

```text
feat: record successful tattoo use in workflow state
```

### Task 5: Schedule History Persistence and Wire Production Storage

**Files:**
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp` (constructor fixtures only if required)

**Interfaces:**
- Consumes: Task 4's ticket/model API and Task 2's store.
- Produces: `RecentTattooLoadOperation`, `RecentTattooRecordOperation`, scheduled storage, completion mailbox, initial-load publication, and production store lifetime.
- Consumed by: Task 6 presentation.

```cpp
using RecentTattooLoadOperation = std::function<runtime::RecentTattooResult()>;
using RecentTattooRecordOperation = std::function<runtime::RecentTattooResult(
    const repository::RecentTattooIdentity&)>;
```

- [ ] **Step 1: Write failing runtime tests**

Test deferred execution, one in-flight task, completion publication only on the next presentation-thread `pump()`, operation exception, scheduler rejection, duplicate/stale completion, close/reopen, Actor/Area changes, and FIFO processing after failure/retry. Assert Apply success remains visible even when history storage fails.

- [ ] **Step 2: Run runtime tests and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "NativeSlotWorkflowRuntimeTests" --output-on-failure
```

Expected: compile failure until the recent load/record operations and scheduling exist.

- [ ] **Step 3: Add scheduler and mailbox handling**

Extend the runtime constructor with optional recent load and record operations after the Favorite operation. Dispatch according to `RecentTattooTicket::Kind`, requiring no identity for load and an identity for record. Drain both completion mailboxes before advancing presentation state or taking references. Schedule Recent after Actor/query/mutation work and before releasing `m_inFlight`; convert a missing/mismatched operation, exceptions, and rejection to `ConfigError` completions.

```cpp
void NativeSlotWorkflowRuntime::drainRecentTattooCompletions() {
    // swap under mutex, complete on presentation thread
}
```

- [ ] **Step 4: Wire one shared production store and safe initial load**

Create `g_recentTattooStore` from the existing `g_pluginConfig`, pass both `load()` and `record()` operations into the runtime, and call `g_nativeSlotWorkflow.initializeRecentTattoos()` after production storage is configured. Let the normal scheduler/mailbox path publish the initial load or its failure so the filter can show unavailable + Retry; do not publish an empty successful list. Keep the resolved absolute path logging unchanged and do not introduce migration/copy behavior.

- [ ] **Step 5: Run runtime and integration-facing tests**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "RecentTattooStoreTests|NativeSlotWorkflowModelTests|NativeSlotWorkflowRuntimeTests|OfficialMenuFrameworkAdapterTests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Proposed message:

```text
feat: schedule recently used persistence
```

### Task 6: Add Picker Filter, Empty States, and Recovery UI

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`
- Test: `tests/native/NativeThumbnailRuntimeTests.cpp` if current-page collection coverage requires the runtime seam

**Interfaces:**
- Consumes: Task 3 browser toggle/order and Tasks 4–5 pending/error/retry state.
- Produces: `Recently used only` overlay control, separate error/Retry presentation, and expanded empty-state classification.

- [ ] **Step 1: Write failing presentation-helper tests**

Extend `CatalogBrowserEmptyState` and `classifyCatalogBrowserEmptyState` with `recentlyUsedOnly` plus unavailable state. Test exact precedence and messages for Recent alone, Favorite+Recent, Applied+Recent, all three, empty catalog, generic filters, and load failure. Test that Retry dispatches `retryRecentTattoo()` rather than Apply.

```cpp
expect(catalogBrowserEmptyMessage(
    classifyCatalogBrowserEmptyState(true, emptyPage, true, true, true)) ==
    "No favorite applied recently used tattoos match the current filters.",
    "expected three-way empty state");
```

- [ ] **Step 2: Run adapter tests and verify RED**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "OfficialMenuFrameworkAdapterTests" --output-on-failure
```

Expected: compile/test failure for missing recent UI state.

- [ ] **Step 3: Add the compact overlay control and recovery presentation**

Add a `Recently Used` table row beside the existing Applied/Favorites rows inside `CatalogFilters`. Keep the popup overlay and existing grid geometry unchanged. Render `Recently Used: <message>` separately from Favorites and tattoo errors, with `Retry recent history` disabled while pending.

- [ ] **Step 4: Update empty states and thumbnail ordering**

Pass all three toggle values into classification. Consume runtime completions and filter intent before reading `model.page()` and collecting thumbnail paths. Verify only visible newest-first rows request thumbnails and no stale page/card reference survives a completion.

- [ ] **Step 5: Run adapter, browser, and thumbnail tests**

Run:

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R "OfficialMenuFrameworkAdapterTests|NativeCatalogBrowserModelTests|NativeThumbnailRuntimeTests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Prepare the scoped commit gate**

Proposed message:

```text
feat: add recently used picker filter
```

### Task 7: Documentation, Full Verification, and Manual Acceptance

**Files:**
- Modify: `CONTEXT.md`
- Modify: `ROADMAP.md`
- Modify: `docs/superpowers/specs/2026-09-27-native-recently-used-design.md`
- Modify: this plan during execution tracking
- Move after acceptance: this plan to `docs/superpowers/plans/archive/2026-09-27-native-recently-used.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: recoverable project context and acceptance evidence. No production API.

- [x] **Step 1: Update terminology and implementation status without claiming acceptance**

Add `Recently Used` to `CONTEXT.md` with the exact five-field identity, per-area six-item retention, newest-first ordering, and successful Apply/Replace boundary. Update the Roadmap line to `implemented; in-game acceptance pending` only after code exists and automated checks pass. Keep unresolved Favorites acceptance text unchanged.

- [x] **Step 2: Run full Debug verification**

```powershell
& .\build.ps1 -Config debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir build/debug --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Expected: build succeeds and every Debug test passes.

- [x] **Step 3: Run full Release verification**

```powershell
& .\build.ps1 -Config release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir build/release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
git diff --check
```

Expected: build succeeds, every Release test passes, and `git diff --check` reports no errors. Report dependency-scan warnings separately; do not describe warnings as test failures.

- [x] **Step 4: Perform scoped safety review**

Review the complete diff for accidental actor/runtime changes, direct UI filesystem access, Apply retry mutation duplication, cross-area eviction, stale completion state revival, unsupported-schema overwrites, unrelated refactors, secrets, and attribution trailers. Confirm no dedicated lint command exists before reporting compiler/tests as the available static validation.

- [x] **Step 5: Present the commit gate**

Show the full scoped diff summary, all test results, known limitations, and exact final Conventional Commit message(s). Do not commit until explicit approval. If Tasks 1–6 were approved as separate commits during execution, propose only the documentation/status commit:

```text
docs: document recently used workflow
```

- [ ] **Step 6: Deploy only after fresh deployment approval**

Follow `DEPLOY.md`; copy only the verified Release DLL to the configured MO2 mod, compare SHA-256 hashes, and report the exact destination. Do not deploy to the game `Data` directory.

- [ ] **Step 7: Complete in-game acceptance**

Verify Add and Replace recording, duplicate promotion, six entries per Body/Face/Hands/Feet, restart persistence, identical logged path and history across two sequential MO2 profiles, all filter intersections, pagination/thumbnails, Filters overlay geometry, synchronization-only Apply retry without duplication, and recoverable history save Retry without repeating Apply.

- [ ] **Step 8: Close the milestone slice only after acceptance**

When intended acceptance passes, mark Recently Used complete in `ROADMAP.md`, set the spec status to accepted, move this plan to `docs/superpowers/plans/archive/`, run `git diff --check`, and request documentation commit approval. If any environmental check is blocked, leave the Roadmap item unchecked and record the exact limitation.

## Planning Review Record

The plan covers every approved spec section: identity, per-area retention, persistence, shared-file transactions, successful-use boundary, Apply synchronization-only safety, queued async writes, retry isolation, newest-first filtering, three-way intersections, empty states, thumbnails, documentation, and manual acceptance. The discovered Apply retry gap is owned by Task 1 and tested at Core, Runtime, and Workflow seams. Interfaces use one consistent `RecentTattooIdentity` and `RecentTattooResult` vocabulary. No placeholders remain. Implementation and commits were approved after verification; deployment and in-game acceptance remain pending.
