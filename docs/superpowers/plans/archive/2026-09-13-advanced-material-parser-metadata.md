# Advanced Material Parser Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve valid SlaveTatsNG 0.8.x material metadata from local tattoo JSON files without changing apply, runtime, or native UI behavior.

**Architecture:** Extend `repository::TattooDefinition` with optional local-pack metadata and parse it at the existing `readOptionalFields` seam. The parser treats each tattoo JSON entry atomically: a malformed optional material field creates an indexed parse issue and skips that entry, while valid sibling entries still load. `TattooRepository` remains a value-preserving catalog layer.

**Tech Stack:** C++23, nlohmann-json, CMake/CTest, PowerShell/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-13-native-advanced-appearance-design.md`

## Global Constraints

- SlaveTatsNG remains authoritative for apply/runtime behavior; do not copy catalog metadata into actor mutations.
- Only `glow`, `glowTexture`, `emissiveMult`, `glossiness`, `specularStrength`, and `bump` are in scope.
- Preserve missing advanced keys as absent optional metadata; legacy packs remain valid.
- `glowTexture` and `bump` accept strings only; no path normalization, validation, or texture selection.
- `emissiveMult`, `glossiness`, and `specularStrength` accept finite, non-negative JSON numbers only; do not add undocumented upper limits.
- An invalid optional field emits an indexed `TattooParseIssue` and skips only that malformed entry; valid siblings remain available.
- Do not modify native UI, workflow, `SlaveTatsRuntime`, apply behavior, Lock/Unlock, domains, or actor targeting.

---

## File Structure

- Modify: `src/repository/TattooSourceParser.h` — optional parser-model fields.
- Modify: `src/repository/TattooSourceParser.cpp` — typed optional JSON readers.
- Modify: `tests/repository/TattooSourceParserTests.cpp` — parse/default/error isolation coverage.
- Modify: `tests/repository/TattooRepositoryTests.cpp` — catalog value-preservation regression.

## Task 1: Parse typed advanced material metadata

**Files:**
- Modify: `src/repository/TattooSourceParser.h`
- Modify: `src/repository/TattooSourceParser.cpp`
- Test: `tests/repository/TattooSourceParserTests.cpp`

**Interfaces:**
- Produces `TattooDefinition::glowTexture`, `bump` as `std::optional<std::string>`.
- Produces `TattooDefinition::emissiveMult`, `glossiness`, `specularStrength` as `std::optional<float>`.
- Reuses `TattooParseIssue{.entryIndex, .message}` for malformed entries.

- [ ] **Step 1: Write failing parser tests for populated and legacy entries**

Extend `parserPreservesSourceAndOptionalMetadata()` with a valid entry containing:

```json
"glowTexture":"Pack\\mark_g.dds",
"bump":"Pack\\mark_n.dds",
"emissiveMult":3.75,
"glossiness":2.5,
"specularStrength":1.25
```

Assert every optional contains the exact source value. Add a legacy entry with none of the five new keys and assert all five optionals are disengaged with no issue.

- [ ] **Step 2: Run the parser executable and verify the new assertions fail**

Run: `.\build.ps1 -Config debug; & .\build\debug\TattooSourceParserTests.exe`

Expected: FAIL because the model/parser does not yet expose or preserve the five new values.

- [ ] **Step 3: Add optional fields and minimal readers**

Add these members to `TattooDefinition`:

```cpp
std::optional<std::string> glowTexture;
std::optional<float> emissiveMult;
std::optional<float> glossiness;
std::optional<float> specularStrength;
std::optional<std::string> bump;
```

In `readOptionalFields`, read `glowTexture` and `bump` only with `is_string()`. Read material floats only with `is_number()`, convert to `float`, then require `std::isfinite(value) && value >= 0.0F`. On failure, set a field-specific error such as `field 'emissiveMult' must be a finite non-negative number` and return `false`.

- [ ] **Step 4: Add failing malformed-field isolation tests, then implement no extra behavior**

Use one malformed entry plus one valid sibling for each category:

```json
"glowTexture":17
"bump":false
"emissiveMult":-0.1
"glossiness":"high"
"specularStrength":null
```

Assert exactly one valid definition remains, exactly one issue exists, and `issue.entryIndex == 0`. Do not make malformed optional fields silently disappear.

- [ ] **Step 5: Run the parser executable and verify it passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\TattooSourceParserTests.exe`

Expected: PASS for populated values, legacy omission, and every malformed-entry isolation case.

## Task 2: Preserve advanced metadata through the repository catalog

**Files:**
- Modify: `tests/repository/TattooRepositoryTests.cpp`

**Interfaces:**
- Consumes populated `TattooDefinition` advanced optionals from Task 1.
- Verifies `TattooRepository::query()` returns catalog entries without losing or normalizing metadata.

- [ ] **Step 1: Write a failing repository preservation test**

In `ordersDefinitionsDeterministicallyAndPreservesMetadata()`, set distinct advanced values on the `Alpha` definition and assert the queried entry preserves all five optionals exactly, including both texture paths.


- [ ] **Step 2: Run the repository executable and verify the existing value-copy path passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\TattooRepositoryTests.exe`

Expected: PASS. Task 1 has already added value members, so the existing copy/sort/query implementation should preserve them without a repository source change.

- [ ] **Step 3: Keep the repository implementation unchanged unless the test exposes a copy loss**

`TattooRepository` owns sorted copies of `TattooDefinition`; do not add filters, badges, or catalog editing. If Task 1's fields are value members, the existing copy/sort/query path should preserve them without source changes.

- [ ] **Step 4: Run the repository executable and verify it passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\TattooRepositoryTests.exe`

Expected: PASS, proving deterministic ordering and advanced metadata preservation.

## Task 3: Full regression and PR preparation

**Files:**
- Modify: only Task 1-2 files and this active plan.

- [ ] **Step 1: Run complete Debug validation**

Run: `.\build.ps1 -Config debug; ctest --test-dir build\debug --output-on-failure`

Expected: build succeeds and every Debug test passes.

- [ ] **Step 2: Run complete Release validation**

Run: `.\build.ps1 -Config release; ctest --test-dir build\release --output-on-failure`

Expected: build succeeds and every Release test passes.

- [ ] **Step 3: Review scope and whitespace**

Run: `git diff --check; git diff -- src/repository/TattooSourceParser.h src/repository/TattooSourceParser.cpp tests/repository/TattooSourceParserTests.cpp tests/repository/TattooRepositoryTests.cpp docs/superpowers/plans/active/2026-09-13-advanced-material-parser-metadata.md`

Expected: no whitespace errors, no runtime/UI/apply changes, and no secret-like values.

- [ ] **Step 4: Present the reviewed diff for commit approval**

Proposed commit:

```text
feat: preserve advanced material parser metadata
```

Do not commit or push until the user explicitly approves the reviewed diff and exact message.
