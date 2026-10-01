# Native Catalog Material Badges and Filters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users identify and filter catalog tattoos by Glow, Bump, and Gloss metadata without changing apply behavior or persisted user data.

**Architecture:** Add one header-only repository classifier over `TattooDefinition`, then make `TattooRepository` use it for three AND-composed filter flags. Propagate the flags through the catalog and workflow models; the adapter consumes the same classifier for ordered badges and forwards filter intent only.

**Tech Stack:** C++23, CMake/CTest, CommonLibSSE NG, ImGui/Skyrim Menu Framework.

**Spec:** `docs/superpowers/specs/2026-09-30-native-catalog-material-badges-filters-design.md`

## Global Constraints

- SlaveTatsNG remains authoritative; catalog metadata is discovery and presentation data only.
- Missing metadata and explicit defaults produce no capability.
- Glow means nonzero `glow`, non-empty `glowTexture`, or explicitly non-default `emissiveMult` using a tolerance.
- Bump means a non-empty `bump` path.
- Gloss means positive `glossiness` or positive `specularStrength`.
- Multiple material filters use AND semantics and compose with existing filters.
- Filters are transient; do not change JSON/configuration schemas.
- The adapter must not duplicate classification or access runtime/storage APIs.
- Alternate texture selection, Edit Appearance changes, and broad layout redesign are out of scope.
- Do not mark the roadmap complete before in-game acceptance.
- Do not commit, push, or deploy without separate explicit approval.

## Review Focus

- Emission values within tolerance of `1.0` must not create false Glow results.
- Present-but-empty paths must behave like missing fields.
- Two or three enabled material filters must require every selected capability.
- Catalog refresh must preserve material toggles while retaining existing contextual reset behavior.
- Lower-left badges must stay inside narrow thumbnails without colliding with Favorite or `In Use`.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/repository/TattooMaterialClassification.h` | Pure Glow/Bump/Gloss classification. |
| `src/repository/TattooRepository.*` | Filter contract and AND-composed queries/facets. |
| `src/native/NativeCatalogBrowserModel.*` | Transient filter state and pagination reset. |
| `src/native/NativeSlotWorkflowModel.*` | Thin UI-intent delegation. |
| `src/native/OfficialMenuFrameworkAdapter.*` | Controls, ordered labels, and badge layout. |
| Existing repository/native tests | Stable seam coverage. |

### Task 1: Add shared classification and repository filters

**Files:**
- Create: `src/repository/TattooMaterialClassification.h`
- Modify: `src/repository/TattooRepository.h`
- Modify: `src/repository/TattooRepository.cpp`
- Test: `tests/repository/TattooRepositoryTests.cpp`

**Interfaces:**
- Produces `TattooMaterialClassification { bool glow; bool bump; bool gloss; }`.
- Produces `classifyTattooMaterial(const TattooDefinition&) noexcept`.
- Produces `kTattooMaterialFloatTolerance = 0.0001F`.
- Extends `TattooFilter` with `glowOnly`, `bumpOnly`, and `glossOnly`.

- [ ] **Step 1: Write failing classifier and filter tests**

Include the new header. Build definitions for missing fields, explicit defaults, nonzero glow, glow texture, emission above and within tolerance, bump, glossiness, specular strength, and all-three. Assert the classifier returns the exact three booleans. Query with each flag and with all flags; assert all-three returns only the entry possessing every capability. Assert all flags false preserve existing count/order, and material filters narrow `contextualFacets` together with Applied/Favorites/Recent.

- [ ] **Step 2: Run the focused test and verify failure**

Run: `cmake --build build/debug --target TattooRepositoryTests --parallel; & .\build\debug\TattooRepositoryTests.exe`

Expected: compilation failure because the classifier, tolerance, and filter fields do not exist.

- [ ] **Step 3: Implement the minimal classifier**

Create the header with `<cmath>` and the public types above. Compute Glow from `(glow && *glow != 0)`, a present non-empty glow path, or `fabs(*emissiveMult - 1.0F) > kTattooMaterialFloatTolerance`. Compute Bump from a present non-empty bump path. Compute Gloss from positive glossiness or positive specular strength. Add defaulted equality for testability. Do not cache flags in `TattooDefinition`.

- [ ] **Step 4: Apply filtering to queries and facets**

Add the three booleans to `TattooFilter`. Add `matchesMaterialFilter(definition, filter)` that calls the classifier once and returns `(!glowOnly || material.glow) && (!bumpOnly || material.bump) && (!glossOnly || material.gloss)`. Use it in both `query()` and `contextualFacets()` beside existing identity filters. Preserve sorting, paging, and Recent ordering.

- [ ] **Step 5: Run the test and verify success**

Run: `cmake --build build/debug --target TattooRepositoryTests --parallel; & .\build\debug\TattooRepositoryTests.exe`

Expected: PASS for existing and new default/empty/tolerance/AND cases.

- [ ] **Step 6: Review and request commit approval**

Run: `git diff --check; git diff -- src/repository/TattooMaterialClassification.h src/repository/TattooRepository.h src/repository/TattooRepository.cpp tests/repository/TattooRepositoryTests.cpp`

Proposed commit after approval: `feat: add catalog material classification filters`

### Task 2: Propagate filters through catalog and workflow models

**Files:**
- Modify: `src/native/NativeCatalogBrowserModel.h`
- Modify: `src/native/NativeCatalogBrowserModel.cpp`
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Test: `tests/native/NativeCatalogBrowserModelTests.cpp`
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`

**Interfaces:**
- Consumes the three `TattooFilter` flags.
- Produces `setGlowOnly(bool)`, `setBumpOnly(bool)`, `setGlossOnly(bool)` on both models.
- Produces `glowOnly()`, `bumpOnly()`, `glossOnly()` const noexcept getters on both models.

- [ ] **Step 1: Write failing catalog-model tests**

Extend the test fixture with Glow-only, Bump-only, Gloss-only, all-three, and legacy entries. Enable Glow+Bump and assert only all-three matches, every change resets `pageIndex` to zero, a no-match combination uses the existing empty page, and disabling all restores legacy entries/order. Combine a material toggle with Search, Area, and Favorites. Assert both same-snapshot and replacement-snapshot refresh preserve all three transient toggles.

- [ ] **Step 2: Write failing workflow tests**

Open Picker via the existing workflow fixture, call each workflow setter, and assert getters and visible catalog results. Navigate Picker -> Preview -> Back and assert toggles remain enabled. Assert no service/runtime request or Actor mutation is created.

- [ ] **Step 3: Run focused tests and verify failure**

Run: `cmake --build build/debug --target NativeCatalogBrowserModelTests NativeSlotWorkflowModelTests --parallel; & .\build\debug\NativeCatalogBrowserModelTests.exe; & .\build\debug\NativeSlotWorkflowModelTests.exe`

Expected: compilation failure because the setters/getters are absent.

- [ ] **Step 4: Implement catalog state transitions**

Implement three setters matching `setAppliedOnly`: unchanged values return; changed values update the corresponding `m_filter` field, reset page zero, reconcile contextual filters, and query. Getters return `m_filter` values. In `resetFilter()`, capture all three flags before reconstructing `TattooFilter`, restore them in the initializer, then restore Applied/Favorites/Recent optionals exactly as today.

- [ ] **Step 5: Add thin workflow delegation**

Add six direct delegating methods to `NativeSlotWorkflowModel`; for example `setGlowOnly(value)` calls `m_catalog.setGlowOnly(value)` and `glowOnly()` returns `m_catalog.glowOnly()`. Repeat for Bump and Gloss. Do not add tickets, schedulers, persistence, or actor state.

- [ ] **Step 6: Run focused tests and verify success**

Run: `cmake --build build/debug --target NativeCatalogBrowserModelTests NativeSlotWorkflowModelTests --parallel; & .\build\debug\NativeCatalogBrowserModelTests.exe; & .\build\debug\NativeSlotWorkflowModelTests.exe`

Expected: PASS including existing contextual, pagination, refresh, and Preview navigation coverage.

- [ ] **Step 7: Review and request commit approval**

Run: `git diff --check; git diff -- src/native/NativeCatalogBrowserModel.h src/native/NativeCatalogBrowserModel.cpp src/native/NativeSlotWorkflowModel.h src/native/NativeSlotWorkflowModel.cpp tests/native/NativeCatalogBrowserModelTests.cpp tests/native/NativeSlotWorkflowModelTests.cpp`

Proposed commit after approval: `feat: add native material filter state`

### Task 3: Render controls and ordered thumbnail badges

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes the shared classifier and workflow getters/setters.
- Produces `catalogMaterialBadgeLabels(const TattooDefinition&)` with fixed ordered labels.
- Produces `calculateCatalogMaterialBadgeLayouts(...)` for clipped lower-left badge rectangles.

- [ ] **Step 1: Write failing label and layout tests**

Assert an all-three definition returns exactly `{"Glow", "Bump", "Gloss"}` and a legacy definition returns no labels. Test normal and narrow containers: first badge starts at lower-left margin, later badges advance horizontally, every right edge stays within the right margin, y uses the bottom margin, and a badge that cannot fit is omitted rather than clipped. Assert layouts do not occupy the upper Favorite/`In Use` row.

- [ ] **Step 2: Write failing interaction coverage**

Render Picker with the existing `CatalogFilters` popup seam. Assert `Glow only`, `Bump only`, and `Gloss only` controls appear after Applied/Favorites/Recent. Trigger each checkbox and assert only the matching workflow getter changes and no runtime operation is emitted.

- [ ] **Step 3: Run adapter tests and verify failure**

Run: `cmake --build build/debug --target OfficialMenuFrameworkAdapterTests --parallel; & .\build\debug\OfficialMenuFrameworkAdapterTests.exe`

Expected: compilation or assertion failure because helpers and controls are absent.

- [ ] **Step 4: Implement presentation helpers**

`catalogMaterialBadgeLabels()` calls the classifier once and appends `Glow`, `Bump`, `Gloss` in that order. Add `CatalogMaterialBadgeLayout { string_view label; CatalogBadgeLayout bounds; }`. Its layout function accepts label widths, container width/height, text height, padding, margin, and spacing; positions badges from the left margin at `max(0, containerHeight - margin - badgeHeight)` and stops before a badge would exceed the right margin. Keep the existing top-right `calculateCatalogBadgeLayout()` unchanged for `In Use`.

- [ ] **Step 5: Render controls and badges**

Add three checkbox rows after Recent inside the existing popup. Each copies the workflow getter into a local Boolean and calls only the corresponding workflow setter on change. For each thumbnail, get ordered labels, measure them with `CalcTextSize`, calculate layouts, and draw the same compact dark background/white text treatment used by `In Use`. Preserve upper-left Favorite, upper-right `In Use`, domain presentation, and click behavior.

- [ ] **Step 6: Run adapter tests and verify success**

Run: `cmake --build build/debug --target OfficialMenuFrameworkAdapterTests --parallel; & .\build\debug\OfficialMenuFrameworkAdapterTests.exe`

Expected: PASS including existing Favorite icon, filter overlay, `In Use`, thumbnail, footer, and constrained-layout tests.

- [ ] **Step 7: Review and request commit approval**

Run: `git diff --check; git diff -- src/native/OfficialMenuFrameworkAdapter.h src/native/OfficialMenuFrameworkAdapter.cpp tests/native/OfficialMenuFrameworkAdapterTests.cpp`

Proposed commit after approval: `feat: show catalog material badges`

### Task 4: Integrate, validate, and record acceptance

**Files:**
- Modify: `ROADMAP.md` only after automated and in-game acceptance.
- Update this active plan while tracking execution.
- Move this plan to `docs/superpowers/plans/archive/` only after acceptance.

**Interfaces:**
- Consumes Tasks 1-3.
- Produces verified completion of the vNext.4 material badges/filter checkbox.

- [ ] **Step 1: Run focused Debug verification in sequence**

Build targets: `cmake --build build/debug --target TattooRepositoryTests NativeCatalogBrowserModelTests NativeSlotWorkflowModelTests OfficialMenuFrameworkAdapterTests --parallel`.

Then run the four matching `.exe` files one at a time. Stop after any build/test failure; do not execute stale binaries. Expected: all pass.

- [ ] **Step 2: Run complete Debug validation**

Run: `.\build.ps1 -Config debug` and only after success run `ctest --test-dir build/debug --output-on-failure`.

Expected: build succeeds and every Debug test passes.

- [ ] **Step 3: Run complete Release validation**

Run: `.\build.ps1 -Config release` and only after success run `ctest --test-dir build/release --output-on-failure`.

Expected: build succeeds and every Release test passes.

- [ ] **Step 4: Inspect final scope and secrets**

Run: `git diff --check; git status --short; git diff --stat; git diff`.

Expected: only classifier, filters, workflow delegation, adapter presentation, tests, and approved docs; no credentials, binaries, deployment artifacts, or unrelated refactors.

- [ ] **Step 5: Request separate deployment approval and test in game**

After approved commits, show the verified Release DLL path/hash and ask separately before deployment. Execute all seven In-Game Acceptance checks in the spec, including combinations with Applied/Favorites/Search and constrained layout. Record failures without checking the roadmap.

- [ ] **Step 6: Update records after acceptance**

After user confirmation, check only `Glow / Bump / Gloss catalog badges and filters` in vNext.4, add a dated execution-status note, and move this plan to archive. Leave Saved tattoo sets/loadouts and alternate texture selection unchecked.

- [ ] **Step 7: Show documentation diff and request approval**

Run `git diff --check` and show the scoped ROADMAP/spec/plan diff.

Proposed commit after approval: `docs: accept catalog material filters`
