# Advanced Material Native Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose editable SlaveTatsNG material appearance values in Edit Appearance through the existing workflow/runtime scheduler boundary.

**Architecture:** Replace the color/alpha-only edit value with a copyable `TattooAppearance` shared by the workflow session and update ticket. The native adapter only collects/presents values; the workflow model owns dirty state, Save, failure retention, and synchronization-only retry. Texture metadata is read-only.

**Tech Stack:** C++23, SKSE Menu Framework v3, ImGuiMCP, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-13-native-advanced-appearance-design.md`

## Global Constraints

- Do not add JContainers or SlaveTatsNG calls outside `SlaveTatsRuntime`.
- Save creates one request only after user action; editing controls create no mutation.
- Preserve stale-handle and synchronization-only retry behavior.
- Editable: color, alpha, glow, glossiness, specularStrength, emissiveMult.
- Read-only: glowTexture and bump; display `None` for empty values.
- UI may offer practical sliders but must not impose undocumented runtime maxima; numeric input accepts finite non-negative values.
- Do not add Lock/Unlock, domain, NPC, presets, texture-path editing, or live preview.

## File Structure

- Modify: `src/native/NativeSlotWorkflowModel.h/.cpp` — rich edit state and request construction.
- Modify: `tests/native/NativeSlotWorkflowModelTests.cpp` — session/dirty/save/retry coverage.
- Modify: `src/native/OfficialMenuFrameworkAdapter.h/.cpp` — material control/frame presentation.
- Modify: `tests/native/OfficialMenuFrameworkAdapterTests.cpp` — conversion, metadata, save presentation.

## Task 1: Rich workflow appearance state

**Files:** `src/native/NativeSlotWorkflowModel.h`, `src/native/NativeSlotWorkflowModel.cpp`, `tests/native/NativeSlotWorkflowModelTests.cpp`

- [ ] **Step 1: Write failing model tests**

Add a slot snapshot with distinct `glow=0x102030`, `glossiness=2.5F`, `specularStrength=1.25F`, `emissiveMult=3.0F`, glow texture, and bump. Assert edit session copies all values; changing any editable field makes Save enabled; changing it back disables Save; Save ticket forwards all six editable values; sync failure retains edited values and Retry Sync sends no second write.

- [ ] **Step 2: Run focused model test (RED)**

Run: `./build.ps1 -Config debug; & ./build/debug/NativeSlotWorkflowModelTests.exe`

Expected: FAIL because the session only stores color/alpha.

- [ ] **Step 3: Implement `TattooAppearance`**

Replace `PreviewTattooAppearance` edit-session use with:

```cpp
struct TattooAppearance {
    std::int32_t color{0xFFFFFF}; float alpha{1.0F};
    std::int32_t glow{}; float glossiness{};
    float specularStrength{}; float emissiveMult{1.0F};
    bool operator==(const TattooAppearance&) const = default;
};
```

Copy snapshot fields in `beginEditAppearance`. Add `glowTexture` and `bump` to the session. Expand `setEditedAppearance` to accept all editable fields; clamp RGB/alpha only, reject non-finite/negative material values by retaining prior material values. Build full request in `confirmAppearanceUpdate`. Keep synchronize-only request creation unchanged.

- [ ] **Step 4: Run focused model test (GREEN)**

Run: `./build.ps1 -Config debug; & ./build/debug/NativeSlotWorkflowModelTests.exe`

Expected: PASS.

## Task 2: Native Material/Emission presentation

**Files:** `src/native/OfficialMenuFrameworkAdapter.h`, `src/native/OfficialMenuFrameworkAdapter.cpp`, `tests/native/OfficialMenuFrameworkAdapterTests.cpp`

- [ ] **Step 1: Write failing adapter helper tests**

Add tests for glow RGB round-trip using existing `tattooColorComponents`/`tattooColorValue`, `None` presentation for empty metadata, material dirty/save enablement, and retry-sync control disablement.

- [ ] **Step 2: Run focused adapter test (RED)**

Run: `./build.ps1 -Config debug; & ./build/debug/OfficialMenuFrameworkAdapterTests.exe`

Expected: FAIL because frame interaction/presentation has no material fields or metadata.

- [ ] **Step 3: Extend frame contracts and render controls**

Extend `EditAppearanceFrameInteraction` with glow/material values and `AppearanceThumbnailPresentation` only for diffuse tint. Render Basic Color/Alpha plus Material / Emission: Glow Color picker, slider plus numeric input for three material floats, and read-only `Glow Texture`/`Bump Texture`. Convert empty metadata to `None`. Pass every edited value to `workflow.setEditedAppearance`; do not synchronize while controls move.

- [ ] **Step 4: Run focused adapter test (GREEN)**

Run: `./build.ps1 -Config debug; & ./build/debug/OfficialMenuFrameworkAdapterTests.exe`

Expected: PASS.

## Task 3: Regression, in-game acceptance, commit gate

- [ ] **Step 1: Debug and Release validation**

Run: `./build.ps1 -Config debug; ctest --test-dir build/debug --output-on-failure; ./build.ps1 -Config release; ctest --test-dir build/release --output-on-failure`

Expected: all tests pass.

- [ ] **Step 2: In-game acceptance**

Deploy only after explicit user approval. Verify Basic/Material controls, `None` metadata, Save, stale handle rejection, and Retry Sync no-rewrite behavior against a SlaveTatsNG 0.8.x tattoo.

- [ ] **Step 3: Diff/commit gate**

Run: `git diff --check`

Proposed commit:

```text
feat: add material appearance editor controls
```

Do not commit/push/deploy without explicit approval.
