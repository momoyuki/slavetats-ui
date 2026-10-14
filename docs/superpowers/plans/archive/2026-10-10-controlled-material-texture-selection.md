# Controlled Material Texture Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let pack authors declare safe glow and bump texture choices that users can preview and save through Edit Appearance.

**Architecture:** Parse ordered option allowlists into catalog definitions, resolve them once by exact runtime identity when an edit session opens, and carry selected paths through the existing appearance model/runtime boundary. SlaveTatsNG remains authoritative for applied tattoo storage and synchronization; the adapter only renders choices and emits workflow intent.

**Tech Stack:** C++23, CMake, nlohmann/json, CommonLibSSE-NG, JContainers, SKSE Menu Framework/ImGui, PowerShell build tooling.

**Spec:** `docs/superpowers/specs/2026-10-10-controlled-material-texture-selection-design.md`

## Global Constraints

- Only pack-declared `glowTextureOptions` and `bumpOptions` are selectable.
- No filesystem/BSA scanning, filename inference, file picker, or free-form path input.
- Preserve exact path bytes and existing singular `glowTexture`/`bump` semantics.
- Resolve options by exact `(domain, section, name, area)` identity; missing or ambiguous matches stay read-only.
- Revalidate the actor-scoped runtime handle before every mutation.
- External overlay slots remain read-only.
- Synchronization-only retry performs no appearance writes.
- Appearance presets remain numeric and loadouts preserve concrete paths without storing option lists.

## Review Focus

- Duplicate runtime identities from multiple effective catalog sources must disable selection rather than choose by load order; cover in Task 2.
- An applied path absent from the allowlist must remain restorable without becoming selectable; cover in Task 4.
- A failed string readback after earlier numeric writes must report a possible mutation; cover in Task 3.
- Rapid texture changes during an in-flight preview must retain latest-wins behavior; cover in Task 4.
- Labels containing control characters or ImGui `##` delimiters must never reach interactive widget IDs; cover in Tasks 1 and 5.

---

### Task 1: Parse Pack-Declared Texture Options

**Files:**
- Modify: `src/repository/TattooSourceParser.h`
- Modify: `src/repository/TattooSourceParser.cpp`
- Modify: `tests/repository/TattooSourceParserTests.cpp`

**Interfaces:**
- Produces: `repository::TattooTextureOption { std::string label; std::string path; }`.
- Produces: `TattooDefinition::glowTextureOptions` and `TattooDefinition::bumpOptions` as ordered vectors.

- [x] **Step 1: Add failing parser tests** for valid ordered objects, trimmed labels, missing arrays, every malformed shape, forbidden label content, empty/NUL paths, and ASCII-case-insensitive duplicate paths.
- [x] **Step 2: Run `ctest --test-dir build/debug -R TattooSourceParserTests --output-on-failure`** and confirm the new cases fail because option fields are not parsed.
- [x] **Step 3: Define `TattooTextureOption` and the two vectors** in `TattooSourceParser.h`, including defaulted equality for deterministic assertions.
- [x] **Step 4: Implement `readTextureOptions(const Json&, std::string_view, std::vector<TattooTextureOption>&, std::string&) -> bool`** in `TattooSourceParser.cpp` with the exact validation and ordering from the spec.
- [x] **Step 5: Run the focused parser test** and confirm it passes.
- [x] **Step 6: Commit** with `feat: parse material texture options` after showing the scoped diff and receiving explicit approval.

### Task 2: Resolve Effective Options by Runtime Identity

**Files:**
- Modify: `src/repository/TattooRepository.h`
- Modify: `src/repository/TattooRepository.cpp`
- Modify: `tests/repository/TattooRepositoryTests.cpp`

**Interfaces:**
- Consumes: parsed `TattooTextureOption` vectors from Task 1.
- Produces: `TattooMaterialTextureOptions { std::vector<TattooTextureOption> glow; std::vector<TattooTextureOption> bump; }`.
- Produces: `TattooRepository::materialTextureOptions(std::string_view domain, std::string_view section, std::string_view name, std::string_view area) const -> std::optional<TattooMaterialTextureOptions>`.

- [x] **Step 1: Add failing repository tests** for a unique exact match, no match, case-different identity, ambiguous duplicates, prepended singular defaults, empty defaults, and declared default deduplication without reordering.
- [x] **Step 2: Run `ctest --test-dir build/debug -R TattooRepositoryTests --output-on-failure`** and confirm the lookup API is missing.
- [x] **Step 3: Add the result type and exact-identity lookup**; return `nullopt` unless exactly one definition matches all four identity fields.
- [x] **Step 4: Build effective lists** by prepending non-empty singular fields as `Pack default` and deduplicating paths with ASCII case-insensitive comparison.
- [x] **Step 5: Run the focused repository test** and confirm it passes.
- [x] **Step 6: Commit** with `feat: resolve tattoo texture options` after scoped approval.

### Task 3: Extend the Typed Appearance Mutation Boundary

**Files:**
- Modify: `src/core/TattooModels.h`
- Modify: `src/core/SlaveTatsService.cpp`
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `tests/core/SlaveTatsServiceTests.cpp`
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`
- Modify: `tests/runtime/UpdateTattooAppearanceOrchestrationTests.cpp`

**Interfaces:**
- Produces: `UpdateTattooAppearanceRequest::glowTexture` and `UpdateTattooAppearanceRequest::bump`.
- Consumes: existing `SlaveTatsAppearanceBindings::setTattooString/getTattooString` and JContainers string accessors.

- [x] **Step 1: Add failing service tests** proving exact strings are forwarded in update mode and ignored by validation in synchronization-only mode.
- [x] **Step 2: Add failing runtime tests** for successful writes/readbacks, stale-handle zero writes, one-field readback failure, JContainers fallback, and sync-only zero writes.
- [x] **Step 3: Run the three focused test executables** and confirm the string expectations fail.
- [x] **Step 4: Add both strings to `UpdateTattooAppearanceRequest`** without changing existing defaults or mode semantics.
- [x] **Step 5: Extend `SlaveTatsAppearanceBackend::writeAppearance`** to write and verify `glowTexture` and `bump` after numeric fields through bindings and fallback paths.
- [x] **Step 6: Confirm orchestration reports `mayHaveOccurred` on string failure** and retains zero writes during sync-only retry.
- [x] **Step 7: Run focused Core and Runtime tests** and confirm they pass.
- [x] **Step 8: Commit** with `feat: update applied material textures` after scoped approval.

### Task 4: Integrate Texture Choices with Live Appearance State

**Files:**
- Modify: `src/native/NativeSlotWorkflowModel.h`
- Modify: `src/native/NativeSlotWorkflowModel.cpp`
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp`
- Modify: `tests/native/NativeSlotWorkflowRuntimeTests.cpp`

**Interfaces:**
- Consumes: `TattooRepository::materialTextureOptions(...)` from Task 2.
- Produces: `TattooAppearance::glowTexture` and `TattooAppearance::bump`.
- Produces: immutable session option lists and `selectGlowTexture(std::size_t) -> bool` / `selectBumpTexture(std::size_t) -> bool` intents.

- [x] **Step 1: Add failing model tests** for unique/missing/ambiguous catalog matches, allowlist-only selection, unlisted original restoration, string dirty state, preset isolation, debounce, latest-wins, Save, Cancel/Close, and stale completion.
- [x] **Step 2: Add failing runtime-coordinator tests** proving tickets carry exact selected/restored paths and sync-only retry preserves them without a write.
- [x] **Step 3: Run focused workflow tests** and confirm the new state and intents are absent.
- [x] **Step 4: Move both paths into `TattooAppearance`** and capture effective option lists when `beginEditAppearance()` opens the session.
- [x] **Step 5: Implement the two index-based selection intents** so only captured options can change edited paths and every real change increments `editRevision`.
- [x] **Step 6: Include both strings in preview, commit, and restore tickets** while leaving appearance presets numeric-only.
- [x] **Step 7: Run focused workflow tests** and confirm they pass.
- [x] **Step 8: Commit** with `feat: select material textures in appearance state` after scoped approval.

### Task 5: Render Controlled Selectors in Edit Appearance

**Files:**
- Modify: `src/native/OfficialMenuFrameworkAdapter.h`
- Modify: `src/native/OfficialMenuFrameworkAdapter.cpp`
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

**Interfaces:**
- Consumes: captured option lists and selection intents from Task 4.
- Produces: pure presentation helpers for selector visibility, current labels, and stable widget IDs.

- [x] **Step 1: Add failing adapter-helper tests** for hidden empty selectors, declared labels, `Pack default`, unlisted current display, exact supporting paths, and stable IDs independent of pack labels.
- [x] **Step 2: Run `ctest --test-dir build/debug -R OfficialMenuFrameworkAdapterTests --output-on-failure`** and confirm the helpers are missing.
- [x] **Step 3: Implement pure selector presentation helpers**; never concatenate an untrusted label into the hidden ImGui identifier.
- [x] **Step 4: Render Glow Texture and Bump Texture combos** beside the existing read-only metadata and call only workflow selection intents.
- [x] **Step 5: Run the focused adapter test** and confirm it passes.
- [x] **Step 6: Commit** with `feat: add controlled material texture selectors` after scoped approval.

### Task 6: Document, Verify, Deploy, and Accept

**Files:**
- Modify: `README.md`
- Modify: `ROADMAP.md` only after in-game acceptance
- Modify: `docs/superpowers/specs/2026-10-10-controlled-material-texture-selection-design.md` with an acceptance record after validation
- Move after completion: `docs/superpowers/plans/active/2026-10-10-controlled-material-texture-selection.md` to `docs/superpowers/plans/archive/`

**Interfaces:**
- Consumes: the completed feature from Tasks 1–5.
- Produces: pack-author documentation and verified release evidence.

- [x] **Step 1: Add the extension JSON example and validation rules** to `README.md` without presenting the fields as upstream SlaveTatsNG schema.
- [x] **Step 2: Build Debug and run all CTest tests**; require 100% pass.
- [x] **Step 3: Build Release and run all CTest tests**; require 100% pass.
- [x] **Step 4: Run release policy scripts and `git diff --check`**, then review the scoped diff and scan it for secrets/unrelated changes.
- [x] **Step 5: Deploy only the verified Release DLL** to the dedicated MO2 mod, preserve the previous DLL, and compare SHA-256 hashes.
- [x] **Step 6: Execute all ten in-game acceptance checks from the spec** on Player and a selected nearby Actor and inspect `SlaveTatsUI.log` after exit.
- [x] **Step 7: Mark the roadmap item complete and record acceptance evidence** after the user confirmed the in-game behavior.
- [x] **Step 8: Commit documentation completion** with `docs: complete material texture selection` after showing the final diff and receiving explicit approval.
