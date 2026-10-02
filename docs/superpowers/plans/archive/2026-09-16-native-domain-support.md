# Native Domain Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Browse and apply tattoo definitions from every domain discovered in MO2-resolved JSON without weakening slot safety.

**Architecture:** The parser retains a normalized domain per definition. Repository facets expose Domain before Source and Section; the workflow forwards the selected definition domain to the existing runtime re-query and apply path.

**Tech Stack:** C++23, nlohmann/json, CommonLibSSE NG, SKSE, SlaveTatsNG, ImGui, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-16-native-domain-support-design.md`

## Global Constraints

- Missing or empty JSON `domain` is the exact string `default`.
- Empty Picker filter means `All Domains`; never send that text to SlaveTatsNG.
- Discover domains only from MO2-resolved JSON; do not read `.SlaveTatsNG.cache` or add JContainers discovery calls.
- Domain precedes Source and Section contextually; Selected Area stays unchanged.
- Apply forwards the definition's exact domain, then retains runtime re-query, external-slot recheck, and `tattooNotFound` behavior.
- Keep external slots read-only. Do not include Lock/Unlock, NPC, persistence, or thumbnail-background QoL work.
- Do not commit, push, or deploy without separate user approval.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/repository/TattooSourceParser.*` | Parse/normalize JSON domain. |
| `src/repository/TattooRepository.*` | Domain index, filter, deterministic facets. |
| `src/native/NativeCatalogBrowserModel.*` | Domain selection and contextual reconciliation. |
| `src/native/NativeSlotWorkflowModel.cpp` | Exact-domain apply ticket. |
| `src/native/OfficialMenuFrameworkAdapter.*` | Domain selector/badges only. |
| Existing parser/repository/native tests | Boundary regression coverage. |

## Task 1: Retain and filter normalized catalog domains

**Files:**
- Modify: `src/repository/TattooSourceParser.h`
- Modify: `src/repository/TattooSourceParser.cpp`
- Modify: `src/repository/TattooRepository.h`
- Modify: `src/repository/TattooRepository.cpp`
- Test: `tests/repository/TattooSourceParserTests.cpp`
- Test: `tests/repository/TattooRepositoryTests.cpp`

**Interfaces:**
- Produces non-empty `TattooDefinition::domain`.
- Produces `TattooFilter::domain`; empty is all domains.
- Produces deterministic `TattooFacets::domains`.

- [x] **Step 1: Write failing parser tests**

Add entries with explicit, missing, and empty `domain`, then assert:

```cpp
expect(report.definitions[0].domain == "custom", "expected explicit domain preserved");
expect(report.definitions[1].domain == "default", "expected missing domain fallback");
expect(report.definitions[2].domain == "default", "expected empty domain fallback");
```

- [x] **Step 2: Run the focused parser test and verify failure**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target TattooSourceParserTests --parallel 1 && .\build\debug\TattooSourceParserTests.exe'
```

Expected: compilation failure because `TattooDefinition::domain` is absent.

- [x] **Step 3: Implement parser normalization**

Add `std::string domain{"default"};` to `TattooDefinition`. Add optional-string parsing for `domain`: missing/empty assigns `default`; non-string reports `field 'domain' must be a string`; valid non-empty content is retained exactly. Parse it before optional material metadata so invalid entries are rejected together.

- [x] **Step 4: Write failing repository tests**

Extend the test definition helper with domain. Build `default`, `Custom`, and `custom` entries, then assert:

```cpp
expect(repository.facets().domains == std::vector<std::string>{"Custom", "default"}, "expected deduped sorted domains");
expect(repository.query(TattooFilter{.domain = "CUSTOM"}).matchedEntries == 2, "expected folded domain filter");
expect(repository.query(TattooFilter{}).matchedEntries == 3, "expected all domains for empty filter");
```

- [x] **Step 5: Implement index, query, and contextual facets**

Add `domain` to `TattooFilter`, `domains` to `TattooFacets`, and `foldedDomain` to `IndexedDefinition`. Include folded domain in filtering and deterministic ordering before section. Reuse `buildFacetValues` for global domain facets. In `contextualFacets`, retain all `domains`, but limit Sources and Sections by Area plus Domain plus Source.

- [x] **Step 6: Run focused parser/repository tests and verify success**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target TattooSourceParserTests TattooRepositoryTests --parallel 1 && .\build\debug\TattooSourceParserTests.exe && .\build\debug\TattooRepositoryTests.exe'
```

Expected: all focused tests pass.

- [x] **Step 7: Review Task 1 and request commit approval**

Run `git diff --check` and review only the six files above. Proposed commit: `feat: retain tattoo catalog domains`.

## Task 2: Add Domain as the first contextual Picker filter

**Files:**
- Modify: `src/native/NativeCatalogBrowserModel.h`
- Modify: `src/native/NativeCatalogBrowserModel.cpp`
- Test: `tests/native/NativeCatalogBrowserModelTests.cpp`

**Interfaces:**
- Consumes `TattooFilter::domain` and `TattooFacets::domains`.
- Produces `void setDomain(std::string value)`; empty means All Domains.

- [x] **Step 1: Write failing browser-model tests**

Use a catalog whose custom and default entries have non-overlapping Source/Section values. Select custom Source/Section then call `setDomain("default")`:

```cpp
expect(model.filter().domain == "default" && model.filter().pageIndex == 0, "expected Domain change to reset pagination");
expect(model.filter().sourceId.empty() && model.filter().section.empty(), "expected incompatible filters cleared");
model.setDomain("");
expect(model.page().matchedEntries == 3, "expected empty domain to restore All Domains");
```

Replace the snapshot with one lacking the chosen domain and assert `refresh()` clears it.

- [x] **Step 2: Run the focused browser test and verify failure**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeCatalogBrowserModelTests --parallel 1 && .\build\debug\NativeCatalogBrowserModelTests.exe'
```

Expected: compilation failure because `setDomain` is absent.

- [x] **Step 3: Implement selection and reconciliation**

Set `m_filter.domain`, reset page index, reconcile, then query. In `reconcileContextualFilters`, validate a non-empty Domain against the folded `contextualFacets().domains`; clear it before validating Source and Section. Preserve Area.

- [x] **Step 4: Run focused browser test and verify success**

Run the Step 2 command. Expected: every browser-model test passes.

- [x] **Step 5: Review Task 2 and request commit approval**

Run `git diff --check` and review the three files above. Proposed commit: `feat: add contextual domain filtering`.

## Task 3: Forward and present exact domains

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Modify: `src/native/OfficialMenuFrameworkAdapter.h` only for presentation helpers
- Test: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Test: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes selected `TattooDefinition::domain` and `setDomain`.
- Produces exact-domain apply tickets plus presentation-only labels/badges.

- [x] **Step 1: Write failing workflow/adapter tests**

Set the selected picker tattoo to `.domain = "custom"`, then assert:

```cpp
expect(ticket->request.domain == "custom", "expected exact selected domain forwarded");
```

Add pure adapter-helper tests for `All Domains`, concrete option labels, card/Slot Action domain labels, and fallback to `default` from an empty applied snapshot domain. Assert helpers do not call model/runtime operations.

- [x] **Step 2: Run focused native tests and verify failure**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --target NativeSlotWorkflowModelTests OfficialMenuFrameworkAdapterTests --parallel 1 && .\build\debug\NativeSlotWorkflowModelTests.exe && .\build\debug\OfficialMenuFrameworkAdapterTests.exe'
```

Expected: workflow assertion fails because current Apply sends `default`.

- [x] **Step 3: Implement workflow and UI changes**

In `confirmApply()`, use `.domain = m_previewTattoo->domain`. In Picker filters, render `Domain` before `Source`, use `All Domains` index zero, and call `setDomain(index == 0 ? "" : contextualFacets.domains[index - 1])`. Add a compact visible domain badge to each card and applied Slot Actions. Do not alter thumbnail cache keys/uploads or add runtime/service calls to the adapter.

- [x] **Step 4: Run focused native tests and verify success**

Run the Step 2 command. Expected: all focused tests pass.

- [x] **Step 5: Review Task 3 and request commit approval**

Run `git diff --check` and review the five files above. Proposed commit: `feat: apply tattoos with selected domain`.

## Task 4: Validate the Domain slice

**Files:**
- Modify: `ROADMAP.md` only after automated and in-game acceptance.
- Modify: this plan during execution.

- [x] **Step 1: Run full Debug verification**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\debug --parallel 1 && ctest --test-dir build\debug --output-on-failure'
```

Expected: every Debug CTest test passes.

- [x] **Step 2: Run full Release verification**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\release --parallel 1 && ctest --test-dir build\release --output-on-failure'
```

Expected: every Release CTest test passes.

- [x] **Step 3: Inspect final scope**

Run `git diff --check`, `git status --short`, and `git diff -- src tests docs ROADMAP.md`. Expected: only Domain work, no secrets, no direct cache/JContainers discovery, and no unrelated QoL change.

- [x] **Step 4: Deploy only after separate approval and complete in-game acceptance**

Verify: All Domains default; custom/default options; contextual Source/Section narrowing; custom-domain apply; Slot Actions domain label; missing-domain legacy JSON as default; external slots still read-only; no cache/JContainers discovery error in log.

- [x] **Step 5: Record vNext.2 completion after user acceptance**

Check the four Domain boxes and vNext.2 status in `ROADMAP.md`, archive this plan after its feature record commits, and only then begin PR #9 QoL work.
