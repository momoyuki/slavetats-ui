# Native Appearance Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persistent, ordered, named appearance presets that load into Edit Appearance for preview without changing tattoo identity, textures, lock state, or footer behavior.

**Architecture:** Add a typed `AppearancePreset` and dedicated `AppearancePresetStore` over the shared `PluginConfigFile`. Route load and mutations through `NativeSlotWorkflowModel` tickets and `NativeSlotWorkflowRuntime` scheduler/mailbox operations; the adapter only renders intent and copies a selected preset into the existing edit session.

**Tech Stack:** C++23, nlohmann/json, CMake/CTest, SKSE/CommonLibSSE-NG, SKSE Menu Framework ImGui bindings.

**Spec:** `docs/superpowers/specs/2026-09-29-native-appearance-presets-design.md`

**Execution status (2026-09-29):** Tasks 1–5 are implemented in commits
`239fdb3`, `5caaa33`, `874f440`, `427cd4f`, and `d411ea7`. Debug and Release
builds and all 35 tests pass. Task 6 documentation is in progress; deployment
and in-game acceptance remain pending.

## Global Constraints

- Persist in the existing absolute `SlaveTatsUI.json` through the shared `PluginConfigFile` only.
- Store exactly color, alpha, glow, emissive multiplier, glossiness, and specular strength.
- Preserve tattoo identity, Actor, area, slot, runtime handle, lock, glow texture, and bump texture.
- Limit the list to 20 entries; folded names are unique; create appends while overwrite and rename retain index.
- JSON array order is presentation order. Never sort or rewrite on load.
- Loading is preview-only; tattoo `Save` and `Cancel` retain their existing contracts.
- Preset errors and Retry are independent of appearance, Favorites, and Recently Used operations.
- Do not implement import/export, in-game reordering, thumbnails, texture paths, or loadouts.

## Review Focus

- A name containing only whitespace must fail without creating or modifying the configuration file; Task 1 tests it.
- `NaN`, infinity, negative alpha, and values above the current Edit Appearance ranges must fail without partial persistence; Task 1 tests them.
- Hand-edited JSON order must survive load, overwrite, rename, delete, and restart; Task 1 tests every mutation.
- A preset completion delivered after a newer request ID must not replace the committed list or error; Task 3 tests it.
- Loading a preset while live preview is pending must supersede the pending values without losing the original restore snapshot; Task 2 tests Load, Save, Cancel, and Close paths.

---

### Task 1: Typed Preset Store and Shared-File Persistence

**Files:**
- Create: `src/runtime/AppearancePresetStore.h`
- Create: `src/runtime/AppearancePresetStore.cpp`
- Create: `tests/runtime/AppearancePresetStoreTests.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/runtime/HotkeyBindingTests.cpp`
- Test: `tests/runtime/FavoriteStoreTests.cpp`
- Test: `tests/runtime/RecentTattooStoreTests.cpp`

**Interfaces:**
- Consumes: `PluginConfigFile::read()` and `PluginConfigFile::update(const ConfigEdit&)`.
- Produces: `AppearancePreset`, `AppearancePresetList`, `AppearancePresetResult`, `AppearancePresetStore::load/create/overwrite/rename/erase`, and `kAppearancePresetLimit`.

- [ ] **Step 1: Register the new test target before creating production files**

Add `AppearancePresetStoreTests` to `CMakeLists.txt` with `PluginConfigFile.cpp`, the new store source, and `nlohmann_json::nlohmann_json`, following `RecentTattooStoreTests`.

- [ ] **Step 2: Write failing store contract tests**

Use this public shape in the tests:

```cpp
AppearancePreset preset(std::string name, float alpha = 1.0F) {
    return AppearancePreset{
        .name = std::move(name), .color = 0xFFFFFF, .alpha = alpha,
        .glow = 0x102030, .emissiveMult = 2.0F,
        .glossiness = 250.0F, .specularStrength = 10.0F,
    };
}

expect(store.create(preset("First"))->at(0).name == "First", "create appends");
expect(store.create(preset("Second"))->at(1).name == "Second", "order preserved");
expect(store.overwrite(preset("first", 0.5F))->at(0).alpha == 0.5F,
    "folded overwrite retains index");
expect(store.rename("FIRST", "Renamed")->at(0).name == "Renamed",
    "rename retains index");
expect(store.erase("renamed")->front().name == "Second",
    "delete preserves remaining order");
```

Also test missing file/no write, exact round-trip, Unicode preservation, 20/21 limit, blank and folded duplicate names, malformed entries, duplicate JSON names, unsupported version, unknown-member preservation, non-finite values, and current appearance bounds.

- [ ] **Step 3: Run the new target and capture RED**

Run:

```powershell
& .\build.ps1 -Config debug
```

Expected: CMake or compile failure because `AppearancePresetStore` does not exist.

- [ ] **Step 4: Implement the typed API and schema**

Create:

```cpp
inline constexpr std::size_t kAppearancePresetLimit = 20;

struct AppearancePreset {
    std::string name;
    std::uint32_t color{};
    float alpha{1.0F};
    std::uint32_t glow{};
    float emissiveMult{1.0F};
    float glossiness{};
    float specularStrength{};
    bool operator==(const AppearancePreset&) const = default;
};

using AppearancePresetList = std::vector<AppearancePreset>;
using AppearancePresetResult = std::expected<AppearancePresetList, ConfigError>;

class AppearancePresetStore {
public:
    explicit AppearancePresetStore(std::shared_ptr<PluginConfigFile> file);
    [[nodiscard]] AppearancePresetResult load();
    [[nodiscard]] AppearancePresetResult create(AppearancePreset preset);
    [[nodiscard]] AppearancePresetResult overwrite(AppearancePreset preset);
    [[nodiscard]] AppearancePresetResult rename(std::string_view oldName, std::string newName);
    [[nodiscard]] AppearancePresetResult erase(std::string_view name);
};
```

Parse `appearancePresets.version == 1` and preserve array order. Reuse Core appearance constants or a shared validation helper rather than duplicating different numeric limits.

- [ ] **Step 5: Add interleaved shared-file tests**

Perform Hotkey → Favorite → Recent → Preset and the reverse order using new store instances over one `PluginConfigFile`. Assert every supported value and an unknown root key survive. Assert an unsupported preset payload survives non-preset writers unchanged.

- [ ] **Step 6: Run focused and full Debug tests**

```powershell
ctest --test-dir build/debug -R "AppearancePresetStoreTests|PluginConfigFileTests|HotkeyBindingTests|FavoriteStoreTests|RecentTattooStoreTests" --output-on-failure
ctest --test-dir build/debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit after the normal approval gate**

```text
feat: persist ordered appearance presets
```

---

### Task 2: Edit-Session Preset Loading

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Consumes: `runtime::AppearancePreset` and the existing `EditAppearanceSession::edited` values.
- Produces: `loadAppearancePreset(const AppearancePreset&)`, preset list/selection accessors, and deterministic preview-only state.

- [ ] **Step 1: Write failing edit-session tests**

```cpp
expect(model.beginEditAppearance(), "edit session starts");
const auto originalGlowTexture = model.editAppearance()->glowTexture;
const auto originalBump = model.editAppearance()->bump;
expect(model.loadAppearancePreset(preset("Warm Glow")), "preset loads");
expect(model.editAppearance()->edited.color == 0xFFFFFF &&
       model.editAppearance()->edited.glow == 0x102030,
       "six values copied");
expect(model.editAppearance()->glowTexture == originalGlowTexture &&
       model.editAppearance()->bump == originalBump,
       "texture metadata preserved");
```

Test rejection outside Edit Appearance, pending-preview replacement, Load → Cancel restore, Load → Save request contents, lock/identity/Actor/slot preservation, and deferred Close restoration.

- [ ] **Step 2: Run focused test and capture RED**

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug -R NativeSlotWorkflowModelTests --output-on-failure
```

Expected: compile failure for missing preset workflow API.

- [ ] **Step 3: Implement minimal session loading**

Add:

```cpp
[[nodiscard]] bool loadAppearancePreset(const runtime::AppearancePreset& preset);
[[nodiscard]] const runtime::AppearancePresetList& appearancePresets() const noexcept;
[[nodiscard]] std::optional<std::size_t> selectedAppearancePreset() const noexcept;
void selectAppearancePreset(std::optional<std::size_t> index);
```

Copy only six edited values, call the same dirty/live-preview scheduling path used by manual control edits, and never replace the captured original snapshot.

- [ ] **Step 4: Run focused and full Debug tests**

Expected: `NativeSlotWorkflowModelTests` and the full Debug suite pass.

- [ ] **Step 5: Commit after approval**

```text
feat: preview saved appearance presets
```

---

### Task 3: Preset Requests, Confirmation, and Retry State

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Consumes: Task 1 preset result/list types and Task 2 edit-session loading.
- Produces: `AppearancePresetRequestKind`, `AppearancePresetTicket`, request/complete/retry APIs, confirmation state, pending/error state.

- [ ] **Step 1: Write failing request-state tests**

Define expectations around:

```cpp
enum class AppearancePresetRequestKind { load, create, overwrite, rename, erase };

struct AppearancePresetTicket {
    std::uint64_t requestId{};
    AppearancePresetRequestKind kind{AppearancePresetRequestKind::load};
    std::optional<runtime::AppearancePreset> preset;
    std::string existingName;
};
```

Test initialization load, create at limit, explicit overwrite confirmation, rename collision without overwrite, explicit delete confirmation, no optimistic list change, success publication, separate error, Retry with a new ID, and stale/out-of-order completion rejection.

- [ ] **Step 2: Run focused test and capture RED**

Expected: compile failure for missing ticket and request APIs.

- [ ] **Step 3: Implement deterministic workflow state**

Add APIs:

```cpp
void initializeAppearancePresets();
[[nodiscard]] bool requestCreateAppearancePreset(std::string name);
[[nodiscard]] bool confirmAppearancePresetOverwrite();
[[nodiscard]] bool requestRenameAppearancePreset(std::string newName);
[[nodiscard]] bool requestDeleteAppearancePreset();
[[nodiscard]] bool confirmAppearancePresetDelete();
[[nodiscard]] bool retryAppearancePreset();
[[nodiscard]] std::optional<AppearancePresetTicket> takeAppearancePresetRequest();
void completeAppearancePreset(std::uint64_t requestId,
    runtime::AppearancePresetResult result);
```

Keep overwrite/delete confirmation local until confirmed. Capture current edited values only when creating the ticket. Persistence state survives Actor/screen changes; edit-session selection may reset when the session ends.

- [ ] **Step 4: Run focused and full Debug tests**

Expected: all pass.

- [ ] **Step 5: Commit after approval**

```text
feat: coordinate appearance preset actions
```

---

### Task 4: Runtime Scheduling and Production Wiring

**Files:**
- Modify: `src/native/NativeSlotWorkflowRuntime.h`
- Modify: `src/native/NativeSlotWorkflowRuntime.cpp`
- Modify: `src/main.cpp`
- Test: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`

**Interfaces:**
- Consumes: `AppearancePresetTicket` and `AppearancePresetStore` methods.
- Produces: asynchronous preset operation callbacks and a completion mailbox drained on presentation pump.

- [ ] **Step 1: Write failing runtime tests**

Inject one operation:

```cpp
using AppearancePresetOperation = std::function<runtime::AppearancePresetResult(
    const AppearancePresetTicket&)>;
```

Test deferred load/create/overwrite/rename/delete, result publication only on the next `pump()`, exception conversion, scheduler rejection, missing operation, in-flight release, and stale completion delivery to the model.

- [ ] **Step 2: Run runtime test and capture RED**

Expected: compile failure because the runtime lacks the operation and mailbox.

- [ ] **Step 3: Implement scheduling and mailbox delivery**

Drain preset completions before taking model references. Schedule preset work after Actor/query/mutation and existing configuration work. Convert failure to `ConfigError` without touching appearance operation state.

- [ ] **Step 4: Wire the shared store in `main.cpp`**

Create `g_appearancePresetStore` from `g_pluginConfig`, dispatch ticket kinds to exact store methods, and call `initializeAppearancePresets()` once after storage configuration. Missing storage returns an unavailable `ConfigError` through the same asynchronous path.

- [ ] **Step 5: Run focused and full Debug tests**

Expected: all pass.

- [ ] **Step 6: Commit after approval**

```text
feat: schedule appearance preset persistence
```

---

### Task 5: Edit Appearance Preset UI

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes: Task 2/3 workflow accessors and intents.
- Produces: the `Appearance Preset` section, naming/manage popups, deterministic labels, and footer-preserving layout.

- [ ] **Step 1: Write failing pure presentation tests**

Add helpers that can be tested without ImGui state:

```cpp
enum class AppearancePresetUiState { unavailable, empty, ready, limitReached, pending };
std::string_view appearancePresetStatusMessage(AppearancePresetUiState state) noexcept;
bool canCreateAppearancePreset(std::size_t count, bool pending) noexcept;
```

Test empty/error/pending/limit states, stored-order labels, overwrite/delete confirmation copy, rename collision, and that Edit Appearance grid/footer calculations are unchanged.

- [ ] **Step 2: Run adapter tests and capture RED**

Expected: compile failure for missing UI helpers.

- [ ] **Step 3: Render the section above Basic**

Render Dropdown, `Load`, `Save Preset`, and `Manage`. Use an ImGui resize-callback-backed `std::string` for naming so the UI does not silently truncate persisted names. Disable mutation controls during pending state. Keep `Cancel / Save / Close` in the existing footer path.

- [ ] **Step 4: Implement popups and separate error/Retry**

Create explicit Overwrite and Delete confirmations containing the preset name. Render `Appearance Presets: <message>` and `Retry` independently from appearance/Favorites/Recent errors.

- [ ] **Step 5: Run focused and full Debug tests**

Expected: all pass.

- [ ] **Step 6: Commit after approval**

```text
feat: add appearance preset controls
```

---

### Task 6: Documentation, Release Verification, and Acceptance Gate

**Files:**
- Modify: `CONTEXT.md`
- Modify: `ROADMAP.md`
- Modify: `docs/superpowers/specs/2026-09-29-native-appearance-presets-design.md`
- Modify: this plan during execution tracking
- Move after acceptance: this plan to `docs/superpowers/plans/archive/2026-09-29-native-appearance-presets.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: recoverable terminology, verification evidence, and manual acceptance instructions.

- [ ] **Step 1: Update implementation status without claiming acceptance**

Add canonical `Appearance Preset` terminology to `CONTEXT.md`. Change the Roadmap item to `implemented; in-game acceptance pending` only after Tasks 1–5 and automated checks pass.

- [ ] **Step 2: Run full Debug and Release verification**

```powershell
& .\build.ps1 -Config debug
ctest --test-dir build/debug --output-on-failure
& .\build.ps1 -Config release
ctest --test-dir build/release --output-on-failure
git diff --check
```

Expected: both builds and every test pass; dependency-scan warnings are reported separately.

- [ ] **Step 3: Review the complete diff**

Check for UI filesystem access, duplicated appearance mutation, lost texture metadata, target-generation coupling, optimistic persistence, JSON reordering, unsupported-schema overwrite, secrets, attribution trailers, and unrelated refactors. Confirm whether the repository has a dedicated lint command.

- [ ] **Step 4: Present commit and deployment gates**

Show the scoped diff, verification results, known limitations, and exact remaining commit message. Do not commit, deploy, merge, or mark accepted without explicit approval.

- [ ] **Step 5: Deploy and execute manual acceptance only after approval**

Follow `DEPLOY.md`, verify Release DLL hashes, then execute every Manual Acceptance item from the spec, including JSON reorder and two sequential MO2 profiles sharing a resolved configuration path.

- [ ] **Step 6: Close the slice after acceptance**

Mark the Roadmap checkbox complete, set the spec status to accepted, archive this plan, run `git diff --check`, and request documentation commit approval.

```text
docs: document appearance preset workflow
```

## Planning Review Record

The plan covers every approved spec contract: exact six-field scope, global shared persistence, 20-entry limit, folded unique names, creation-order JSON semantics, preview-only loading, original-snapshot restoration, explicit overwrite/delete confirmation, rename collision rejection, scheduler/mailbox delivery, separate errors and Retry, stable footer placement, full automated verification, and manual acceptance. Each public type is introduced before use, every production task begins with a failing test, and no CommonLibSSE migration, import/export, reordering UI, texture editing, or loadout behavior is included.
