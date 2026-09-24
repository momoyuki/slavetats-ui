# Native Favorites Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` when implementation is explicitly authorized. Steps use checkbox syntax for tracking. The user requested this document only; do not begin implementation, commit, or deploy from this planning turn.

**Goal:** Add persistent, shared Favorites with independent star actions on Picker cards and a composable Favorites-only filter.

**Status:** Draft for review; implementation not started. Keep outside `plans/active/` until approved for execution.

**Architecture:** A separate runtime `FavoriteStore` owns favorite persistence through a shared JSON transaction writer also used by `HotkeyBinding`. The workflow queues explicit desired-state requests; the runtime scheduler executes storage operations, and the presentation thread consumes copied completions. The repository filters by full favorite identities before pagination without reading files.

**Tech Stack:** Existing Windows C++, CMake/Ninja/MSVC, nlohmann JSON, native ImGui adapter, injected scheduler, standalone CTest executables. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-24-native-favorites-design.md` (approved; implementation deferred). The Design Contract below summarizes the conversational decisions. Read `ROADMAP.md`, `CONTEXT.md`, and the domain/slot-first specs for surrounding contracts. This plan does not authorize execution.

## Design Contract

- Milestone: vNext.4 Workflow Quality of Life.
- Favorites survive game restarts and are shared across MO2 profiles using the same configuration location. They are independent of Actor Target and savegame.
- Store favorites in the existing `SlaveTatsUI.json`, preserving `hotkey` and unknown keys. Keep `FavoriteStore` separate from `HotkeyBinding`.
- Favorite identity consists of `domain`, `sourceId`, `section`, and `name`. This is distinct from the Applied-only `TattooIdentity` of section/name. Use normalized catalog source IDs, not physical MO2 paths, row indices, runtime handles, or Actor IDs. Preserve exact domain, section, and name spelling for identity matching.
- Exact duplicates of all four fields share a star. Different source/domain values do not share a star. A changed source ID is a new identity; do not guess migrations.
- Draw an independent star action on every Picker card. Clicking the star must not also select the card, enter Preview, or apply a tattoo.
- Persist immediately through scheduled work. Publish changed membership only after successful persistence. Preserve existing membership on failure and show a recoverable error.
- Favorites-only defaults off, survives ordinary Picker/Preview navigation, and intersects all existing filters, including Applied-only and Selected Area, before pagination.
- Keep missing-pack identities on disk. Hide unavailable catalog entries; restore their star automatically when the same identity returns.
- No arbitrary favorite-count limit, sorting mode, favorite management screen, presets, history, or footer redesign in this slice.

## Global Constraints

- No filesystem access from render callbacks or deterministic workflow models.
- SlaveTatsNG remains authoritative for tattoo mutations; Favorites do not call SlaveTatsNG or JContainers.
- External-overlay ownership, actor generation validation, and synchronization-only retry remain unchanged.
- All JSON writers for this file must share one serialized read/modify/write transaction boundary.
- Malformed existing JSON must not be silently replaced. A failed write must preserve the original file and committed UI state.
- Code and documentation remain English; user-facing discussion remains Thai.
- Commit only after showing the scoped diff and exact message and receiving explicit approval. Deployment is deferred by the user and requires separate authorization.
- Applied-only commit `a6e2d6c` passed prior automated checks but still awaits in-game acceptance. Do not report it or Favorites as fully accepted based solely on automated tests.

## Review Focus

1. Interleaved Hotkey/Favorites saves must preserve both values and unrelated JSON keys: Task 1.
2. A corrupt file, unsupported favorites schema, or failed replacement must preserve recoverable data and the previous membership: Tasks 1–2.
3. Same-named tattoos across sources/domains and a missing/reinstalled pack must retain the correct stars: Tasks 2–3.
4. Closing the menu or changing Actor while a save is pending must not lose a successful global favorite update or revive old slot state: Task 4.
5. Removing the last favorite on a later page must clamp pagination, preserve active filters, and avoid invalidating references during card rendering: Tasks 3–5.

## Current Evidence and Execution Preflight

`src/main.cpp` builds the current config path from `setupLog().parent_path()`. It is not derived from the DLL directory. Its fallback uses the game's `Data/SKSE/Plugins` location, and an exception can leave `pluginDir` empty. The implementation must not promise profile sharing merely because profiles use the same DLL.

`HotkeyBinding::save()` currently performs an independent read/modify/truncating write. A FavoriteStore mutex alone cannot prevent lost updates from this second writer. A small shared transaction helper is a necessary prerequisite, not an unrelated configuration refactor.

- [ ] Recheck branch/HEAD, `git status`, repository instructions, and open issues/PRs for overlapping configuration or catalog work. If remote status is unavailable, record that limitation.
- [ ] Confirm the normal resolved configuration path is outside profile-local/virtualized storage and log the absolute path. Use the same path for both stores. Reject an empty/relative path for Favorites rather than creating a file in the process working directory. Profile sharing must be verified manually before acceptance; if the existing path is profile-specific, report the conflict before migrating any user data.
- [ ] Read the current catalog query/facet code, workflow request/completion pattern, adapter card input handling, hotkey persistence tests, and CMake targets before editing.
- [ ] After execution approval, move this plan into `docs/superpowers/plans/active/` and update internal links. Do not move it during planning.

## Task 1: Serialize and Safely Replace Shared Configuration

**Files:** Create `src/runtime/PluginConfigFile.h`, `src/runtime/PluginConfigFile.cpp`, `tests/runtime/PluginConfigFileTests.cpp`. Modify `src/runtime/HotkeyBinding.h`, `src/runtime/HotkeyBinding.cpp`, `tests/runtime/HotkeyBindingTests.cpp`, `src/main.cpp`, `CMakeLists.txt`.

**Interfaces:** In `stui::runtime`, introduce the following shared types and operations. HotkeyBinding and FavoriteStore receive the same `std::shared_ptr<PluginConfigFile>` in production. Keep the path-based HotkeyBinding constructor as a delegating compatibility convenience where existing tests need it.

```cpp
struct ConfigError { std::string message; };
using ConfigEdit = std::function<std::expected<void, ConfigError>(nlohmann::json&)>;
class PluginConfigFile {
public:
    explicit PluginConfigFile(std::filesystem::path path);
    std::expected<nlohmann::json, ConfigError> read() const;
    std::expected<void, ConfigError> update(const ConfigEdit& edit);
};
```

- [ ] Add real temporary-directory tests: missing file reads as an empty object without creating it; legacy `hotkey` and an unknown nested key survive a favorites update; changing hotkey afterwards preserves favorites; malformed/unreadable JSON is reported without overwriting its bytes.
- [ ] Exercise competing updates through the same helper, assert the final file contains both changes, and test serialization failure/write failure/replacement failure using a narrow injected filesystem-operation seam where deterministic OS failures are impractical.
- [ ] Build and run the new test target, verify failure due to missing behavior, then implement the helper. Hold one mutex over read, edit, temporary-file write, flush/close checks, and replacement. Write a uniquely named sibling temporary file, then use checked Windows replacement/move semantics. Never truncate the destination before success. Clean up only the temporary file owned by this attempt on failure.
- [ ] Make `HotkeyBinding` route persistence through this helper; retain current hotkey parsing and rollback semantics. Do not overwrite malformed files to recover a hotkey. Avoid holding inconsistent lock order across stores; acquire store-local state lock before the shared file transaction lock everywhere.
- [ ] Re-read disk inside every transaction rather than writing a cached whole document. Cross-process simultaneous game instances are not supported in this slice; sequential MO2-profile launches are the acceptance target.
- [ ] Run `PluginConfigFileTests` and `HotkeyBindingTests`. Proposed commit after review/approval: `refactor: serialize shared plugin configuration writes`.

Representative test behavior (inside a temporary-directory fixture):

```cpp
auto file = std::make_shared<PluginConfigFile>(configPath);
expect(file->update([](auto& json) -> std::expected<void, ConfigError> {
    json["hotkey"] = 67;
    json["custom"] = "keep";
    return {};
}).has_value(), "expected initial config save");
expect(file->update([](auto& json) -> std::expected<void, ConfigError> {
    json["favorites"] = {{"version", 1}, {"entries", nlohmann::json::array()}};
    return {};
}).has_value(), "expected favorites save");
const auto loaded = file->read();
expect(loaded && loaded->at("hotkey") == 67 && loaded->at("custom") == "keep",
       "expected unrelated config values preserved");
```

## Task 2: Persistent Favorite Identity and Store

**Files:** Create `src/repository/FavoriteIdentity.h`, `src/runtime/FavoriteStore.h`, `src/runtime/FavoriteStore.cpp`, `tests/runtime/FavoriteStoreTests.cpp`; modify `CMakeLists.txt`.

**Interfaces:** Keep identity values transport-independent; store methods use Task 1's error type.

```cpp
// stui::repository
struct FavoriteIdentity {
    std::string domain, sourceId, section, name;
    bool operator==(const FavoriteIdentity&) const = default;
};
FavoriteIdentity favoriteIdentity(const TattooDefinition& tattoo);
// stui::runtime
using FavoriteList = std::vector<repository::FavoriteIdentity>;
using FavoriteResult = std::expected<FavoriteList, ConfigError>;
class FavoriteStore {
public:
    explicit FavoriteStore(std::shared_ptr<PluginConfigFile> file);
    FavoriteResult load();
    FavoriteResult setFavorite(const repository::FavoriteIdentity& identity, bool enabled);
};
```

Persist under this exact shape, preserving unknown root properties:

```json
{"hotkey":67,"favorites":{"version":1,"entries":[{"domain":"default","sourceId":"textures/actors/character/slavetats/marks.json","section":"Marks","name":"Rose"}]}}
```

- [ ] Test missing favorites in a valid legacy config as an empty list; missing config also yields empty Favorites without creating it. Test save/reload across new store instances, idempotent enable/disable, deduplication, stable serialized ordering, Unicode names, and exact four-field matching.
- [ ] Test invalid field types, absent required fields, empty identity fields, invalid root JSON, and unknown schema versions. Fail the complete favorites load/update and preserve original bytes rather than silently discarding entries. Retain extra members of a supported `favorites` object when updating its entries.
- [ ] Test a saved identity that is absent from a later catalog stays in storage and loads again unchanged. Persistence never prunes according to the installed catalog.
- [ ] Verify RED, implement parsing and desired-state updates inside `PluginConfigFile::update`, and return the newly committed list only after file replacement succeeds. Use fresh on-disk favorites for each update. Normalize source IDs using the scanner's canonical form; use exact domain/section/name comparisons and do not change Applied-only identity semantics.
- [ ] Verify failed updates return an error rather than a candidate list; test the unchanged file and subsequent reload, not only a mocked return code.
- [ ] Run `FavoriteStoreTests`, `PluginConfigFileTests`, and `HotkeyBindingTests`. Proposed commit: `feat: persist shared tattoo favorites`.

Representative fixture assertion:

```cpp
FavoriteStore store(file);
const repository::FavoriteIdentity rose{"default", sourceId, "Marks", "Rose"};
expect(store.setFavorite(rose, true).has_value(), "expected successful save");
FavoriteStore reopened(file);
const auto restored = reopened.load();
expect(restored && *restored == FavoriteList{rose}, "expected restart persistence");
```

## Task 3: Favorites Filtering and Browser State

**Files:** Modify `src/repository/TattooRepository.h`, `src/repository/TattooRepository.cpp`, `src/native/NativeCatalogBrowserModel.h`, `src/native/NativeCatalogBrowserModel.cpp`, `tests/repository/TattooRepositoryTests.cpp`, `tests/native/NativeCatalogBrowserModelTests.cpp`.

**Interfaces:** Add `std::optional<std::vector<FavoriteIdentity>> favoriteIdentities` to `TattooFilter`. Absent means inactive; present empty means no favorites. Add browser methods:

```cpp
void setFavoritesOnly(bool enabled);
void setFavoriteIdentities(std::vector<repository::FavoriteIdentity> identities);
bool favoritesOnly() const noexcept;
bool isFavorite(const repository::TattooDefinition& tattoo) const;
```

- [ ] Test interleaved favorite/nonfavorite catalog entries across at least three original pages: favorites must fill a six-entry page and produce correct total/matched/page counts. Add same-name/different-source and different-domain fixtures and intersection with Applied-only, area, search, source, domain, and section.
- [ ] Test null versus empty membership, missing-pack entries, catalog replacement, unchanged membership preserving the page, and removal of the final entry on the last page clamping to the preceding page.
- [ ] Verify RED, then add exact favorite membership checking to query and contextual facets before pagination. Implement filter-toggle reset to page zero. On committed membership changes, preserve the requested page and clamp through the query; do not reset to page zero unnecessarily.
- [ ] Preserve explicit search/domain/source/section when a star is removed, including when results become empty. Reconcile dependent filters for explicit domain/source/area changes using the established hierarchy. Derive selected combo labels from stored selections even when the narrowed option list has no matching value; do not silently display All while filtering by another value.
- [ ] Keep the full favorite set for star presentation even when Favorites-only is disabled. Retain toggle and memberships on catalog replacement while respecting existing catalog-refresh behavior for other filters.
- [ ] Run repository/browser tests. Proposed commit: `feat: filter catalog by saved favorites`.

Representative browser assertions after installing a seven-favorite fixture:

```cpp
model.setFavoritesOnly(true);
expect(model.page().entries.size() == 6 && model.page().pageCount == 2,
       "expected full first favorites page");
model.setPageNumber(2);
model.setFavoriteIdentities(firstSixFavorites);
expect(model.page().pageIndex == 0 && model.page().matchedEntries == 6,
       "expected last-page removal to clamp safely");
```

## Task 4: Scheduled Save Requests and Completion Publication

**Files:** Modify `src/native/NativeSlotWorkflowModel.h`, `src/native/NativeSlotWorkflowModel.cpp`, `src/native/NativeSlotWorkflowRuntime.h`, `src/native/NativeSlotWorkflowRuntime.cpp`, `src/main.cpp`, `tests/native/NativeSlotWorkflowModelTests.cpp`, `tests/native/NativeSlotWorkflowRuntimeTests.cpp`. Update constructor call sites in `tests/native/OfficialMenuFrameworkAdapterTests.cpp` if needed.

**Interfaces:** Define in the native boundary:

```cpp
struct FavoriteTicket {
    std::uint64_t requestId;
    repository::FavoriteIdentity identity;
    bool enabled;
};
using FavoriteOperation = std::function<runtime::FavoriteResult(
    const repository::FavoriteIdentity&, bool)>;
// Model
bool requestFavorite(const repository::TattooDefinition& tattoo, bool enabled);
std::optional<FavoriteTicket> takeFavoriteRequest();
void completeFavorite(std::uint64_t requestId, runtime::FavoriteResult result);
bool favoritePending() const noexcept;
const runtime::ConfigError* favoriteError() const noexcept;
```

- [ ] Test request creation while in Picker, duplicate input suppression, unchanged membership until success, error preservation, retry with a fresh request ID, and absence of apply/remove/appearance requests when favoriting.
- [ ] Test an Actor change and close/reopen while a save is queued: successful membership is global and must survive; the completion must not change the new screen, Actor, target slot, or preview. Use a separate favorite request sequence, not Target Generation. Ignore repeated/out-of-order completions by favorite request ID.
- [ ] Test deferred scheduler execution, scheduling rejection/throw, storage exceptions, and one in-flight request. Verify exceptions release pending state and publish an actionable error. Do not assume schedule invocation means storage succeeded.
- [ ] Verify RED, wire `FavoriteOperation` to `FavoriteStore::setFavorite` in main, and queue storage through the existing injected scheduler. Keep favorite persistence errors separate from tattoo-operation errors. Disable further star writes while one favorite request is pending.
- [ ] Publish immutable favorite completions into a mutex-protected mailbox, consumed from runtime `pump()` on the presentation thread. Do not mutate catalog vectors from the scheduled storage task while the adapter is iterating them. Keep existing actor-operation behavior outside this focused change.
- [ ] Load Favorites once during initialization, before exposing star actions; publish the loaded list to the catalog model. If loading fails, retain the file, expose a load error and explicit scheduled retry, and disable star writes until load succeeds. Preserve global favorite pending/completion state during ordinary menu resets.
- [ ] Run workflow model/runtime tests and affected adapter tests. Proposed commit: `feat: schedule favorite saves through native workflow`.

Representative model assertions using a Picker fixture and a captured ticket:

```cpp
expect(model.requestFavorite(tattoo, true), "expected favorite request");
const auto ticket = model.takeFavoriteRequest();
expect(ticket && !catalog.isFavorite(tattoo), "expected pending save without optimistic star");
expect(!model.takeApplyRequest(), "expected favorite to leave tattoo application untouched");
model.completeFavorite(ticket->requestId, runtime::FavoriteList{repository::favoriteIdentity(tattoo)});
expect(catalog.isFavorite(tattoo) && !model.favoritePending(), "expected committed star");
```

## Task 5: Picker Star Interaction and Empty States

**Files:** Modify `src/native/OfficialMenuFrameworkAdapter.h`, `src/native/OfficialMenuFrameworkAdapter.cpp`, `tests/native/OfficialMenuFrameworkAdapterTests.cpp`.

**Interfaces:** Adapter star click sends `requestFavorite(tattoo, !catalog.isFavorite(tattoo))`; Favorites-only sends `catalog.setFavoritesOnly(enabled)`. Extend `classifyCatalogBrowserEmptyState` with an optional `bool favoritesOnly = false` after its Applied-only argument.

- [ ] Test presentation action dispatch so a star click consumes the card action and cannot also select Preview. Use independent hit targets and per-card unique IDs even for duplicate names; exercise actual action-routing behavior rather than source-text assertions.
- [ ] Verify RED, add a bounded star hit target to the card metadata region. Draw an outline/filled star with existing draw primitives if the shipped font lacks the glyph; show Add to favorites/Remove from favorites tooltips and Saving while pending. Preserve thumbnail dimensions and the existing footer layout.
- [ ] Add Favorites-only next to Applied-only in Filters; preserve selected context labels. Render separate persistence error/retry UI without replacing tattoo mutation errors. During loading failure, communicate unavailable Favorites instead of claiming the catalog is empty.
- [ ] Define empty-state precedence: unavailable Favorites with Favorites-only active shows the load error and Retry first; otherwise absent/empty catalog precedes filter messages; both filters active => `No favorite applied tattoos match the current filters.`; Favorites-only => `No favorite tattoos match the current filters.`; Applied-only => existing text; otherwise existing generic message. Test classification and usable guidance.
- [ ] Consume completions and user filter intent before collecting visible thumbnail paths and taking references to the page. Star persistence completion on the next pump must not invalidate the current frame's card references. Confirm only visible page textures are requested after filtering.
- [ ] Run adapter, browser, workflow, and thumbnail tests. Proposed commit: `feat: add favorite stars and picker filter`.

## Task 6: Verification, Documentation, and Deferred Acceptance

**Files:** Update `CONTEXT.md`, `ROADMAP.md`, this plan, and `docs/superpowers/specs/2026-09-24-native-favorites-design.md` when approved implementation requires documentation changes. Record the verified config location, schema, sharing boundary, missing-pack behavior, and recovery guidance.

- [ ] Run repository-native Debug and Release builds and complete suites. Check every command's exit status; do not run tests against stale binaries after a failed build.

```powershell
& .\build.ps1 -Config debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir build/debug --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& .\build.ps1 -Config release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir build/release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
git diff --check
```

- [ ] Register all new sources and test targets in CMake, linking nlohmann JSON where needed. Compiler checks provide C++ type validation; run repository lint tooling if configured, otherwise explicitly report no dedicated linter. Report vcpkg dependency-scan warnings separately from test outcomes.
- [ ] Review the scoped diff for unrelated actor/appearance changes, credential-like data, ownership violations, and storage failure behavior. Present exact commit messages and diffs before asking for commit approval. Do not push or deploy automatically.
- [ ] Keep Favorites unchecked in the Roadmap until intended validation passes; annotate implemented/automated validation versus pending in-game acceptance. Record Applied-only's pending in-game acceptance accurately alongside it.
- [ ] After separate deployment approval, verify stars, filter intersections, pagination, duplicate-name identities, error/retry, and thumbnail consistency in game. Restart to verify persistence. Change profiles and verify the logged absolute configuration path and same stars. Disable/re-enable a pack and verify restoration. Confirm Hotkey and Favorites survive alternating saves and a restart.
- [ ] Complete the deferred Applied-only acceptance in the same session: Actor/Area switches, empty results, external exclusions, Back/Preview, and Filters layout.
- [ ] Only after acceptance, update roadmap status and archive the completed plan under `docs/superpowers/plans/archive/`. If deployment remains deferred, leave the manual checklist open and report that limitation.

## Planning Review Record

The plan covers persistence, shared-file writes, full identity matching, missing packs, filtering, scheduled state transitions, and independent star interaction. Profile sharing is tied to an observed configuration path rather than DLL location. Storage failures, configuration-writer races, global completion lifetime, and render-time invalidation have explicit task owners and test cases. No production files, tests, build settings, or deployed artifacts are changed by writing this plan.
