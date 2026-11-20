# Advanced Material Runtime Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the typed, validated, and verified runtime contract for SlaveTatsNG 0.8.x editable material appearance fields without changing the native UI yet.

**Architecture:** Extend the existing `TattooEntry` snapshot and `UpdateTattooAppearanceRequest` with glow/material values. Keep all mutation behind `IUpdateTattooAppearanceBackend`: it validates handle ownership, writes every editable value with JContainers readback verification, then marks and synchronizes exactly once. `glowTexture` and `bump` are snapshot metadata only in this PR; the workflow/UI consumes the expanded contract in a later vertical slice.

**Tech Stack:** C++23, CommonLibSSE-NG, SlaveTatsNG, JContainers, CMake/CTest, PowerShell/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-13-native-advanced-appearance-design.md`

## Global Constraints

- SlaveTatsNG remains the authoritative runtime; do not add parallel slot/allocation logic.
- Keep JContainers and SlaveTatsNG access out of Core and native workflow/UI code.
- Revalidate the session-local tattoo handle against the requested actor before every mutation.
- Write and verify `color`, `invertedAlpha`, `glow`, `glossiness`, `specularStrength`, and `emissiveMult` before marking `.SlaveTats.updated` or synchronizing.
- Missing snapshot keys use `glow=0`, `glossiness=0.0F`, `specularStrength=0.0F`, `bump=""`, `glowTexture=""`, and `emissiveMult=1.0F`.
- Reject RGB values outside `0x000000..0xFFFFFF`, non-finite alpha/material values, and negative material values before mutation. Do not invent upper limits for material floats.
- `synchronizeOnly` must perform no membership query or appearance write and must preserve existing retry behavior.
- Do not implement Lock/Unlock, domain selection, NPC targeting, arbitrary texture-path editing, parser work, or native UI in this PR.

---

## File Structure

- Modify: `src/core/TattooModels.h` — typed snapshot and appearance request fields.
- Modify: `src/core/SlaveTatsService.cpp` — request validation at the typed service boundary.
- Modify: `src/runtime/UpdateTattooAppearanceOrchestration.h` — backend write contract.
- Modify: `src/runtime/UpdateTattooAppearanceOrchestration.cpp` — ownership-protected mutation flow.
- Modify: `src/runtime/SlaveTatsRuntime.h` — testable production binding contract.
- Modify: `src/runtime/SlaveTatsRuntime.cpp` — JContainers snapshot readback and verified writes.
- Modify: `tests/core/SlaveTatsServiceTests.cpp` — service validation/forwarding coverage.
- Modify: `tests/runtime/UpdateTattooAppearanceOrchestrationTests.cpp` — operation order and no-write safety coverage.
- Modify: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp` — JContainers binding/readback coverage.

## Task 1: Define the typed advanced-appearance contract

**Files:**
- Modify: `src/core/TattooModels.h`
- Test: `tests/core/SlaveTatsServiceTests.cpp`

**Interfaces:**
- Produces `core::TattooEntry::{glow, glossiness, specularStrength, bump, glowTexture, emissiveMult}`.
- Produces `core::UpdateTattooAppearanceRequest::{glow, glossiness, specularStrength, emissiveMult}`.
- Defaults are `0`, `0.0F`, `0.0F`, `""`, `""`, and `1.0F` respectively.

- [ ] **Step 1: Write failing Core forwarding and validation tests**

Add a valid request with distinct values and assertions that `FakeTattooRuntime::updatedRequest` receives them unchanged. Add invalid request cases for `glow=-1`, `glow=0x1000000`, negative `glossiness`, negative `specularStrength`, negative `emissiveMult`, and NaN/infinity for each float; each must return `updateFailed` with `updateCount == 0`.

- [ ] **Step 2: Run the Core test executable and verify the new cases fail**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\SlaveTatsServiceTests.exe`

Expected: FAIL because the request has no advanced fields and validation/forwarding does not exist.

- [ ] **Step 3: Add the model fields with stable defaults**

Extend `TattooEntry` exactly as follows:

```cpp
std::int32_t glow{0};
float glossiness{0.0F};
float specularStrength{0.0F};
std::string bump;
std::string glowTexture;
float emissiveMult{1.0F};
```

Extend `UpdateTattooAppearanceRequest` with `glow`, `glossiness`, `specularStrength`, and `emissiveMult`, using the same defaults. Retain `color`, visible `alpha`, and `mode` unchanged.

- [ ] **Step 4: Validate the new editable request values in `SlaveTatsService::updateAppearance`**

After existing color/alpha checks and only for `updateAndSynchronize`, reject invalid glow RGB and any non-finite or negative material float. Use `std::isfinite`; include `<cmath>`. Leave `synchronizeOnly` independent of appearance-value validation so it can retry a partial success with stale editor values.

- [ ] **Step 5: Run the Core test executable and verify it passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\SlaveTatsServiceTests.exe`

Expected: PASS, including exact forwarding and every invalid-value no-runtime-call case.

## Task 2: Make orchestration mutate the complete editable value set

**Files:**
- Modify: `src/runtime/UpdateTattooAppearanceOrchestration.h`
- Modify: `src/runtime/UpdateTattooAppearanceOrchestration.cpp`
- Test: `tests/runtime/UpdateTattooAppearanceOrchestrationTests.cpp`

**Interfaces:**
- Replaces `IUpdateTattooAppearanceBackend::writeAppearance(handle, color, invertedAlpha)` with `writeAppearance(handle, const core::UpdateTattooAppearanceRequest&)`.
- Consumes visible `request.alpha` and converts it once inside the backend to SlaveTats inverted alpha.
- Produces one verified logical write call only after actor/handle membership succeeds.

- [ ] **Step 1: Write failing orchestration tests for the expanded backend call**

Change the fake backend to capture the full request. Give `validRequest()` distinct values:

```cpp
.glow = 0x102030,
.glossiness = 2.5F,
.specularStrength = 1.25F,
.emissiveMult = 3.0F,
```

Assert that a valid update invokes exactly one write with those values, and that stale-handle and `synchronizeOnly` paths invoke zero writes.

- [ ] **Step 2: Run the orchestration test executable and verify it fails**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\UpdateTattooAppearanceOrchestrationTests.exe`

Expected: FAIL because the backend interface only accepts color and inverted alpha.

- [ ] **Step 3: Update the backend interface and orchestration call site**

Use this interface:

```cpp
[[nodiscard]] virtual bool writeAppearance(
    std::int32_t runtimeHandle,
    const core::UpdateTattooAppearanceRequest& request) = 0;
```

Keep all existing actor resolution, membership, updated-marker, synchronization, error code, and operation-order behavior. Call `writeAppearance(request.runtimeHandle, request)` only in `updateAndSynchronize` after membership validation.

- [ ] **Step 4: Run the orchestration test executable and verify it passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\UpdateTattooAppearanceOrchestrationTests.exe`

Expected: PASS, including one logical write for success and zero writes for stale/sync-only paths.

## Task 3: Implement verified JContainers writes for material fields

**Files:**
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Modify: `src/runtime/SlaveTatsRuntime.h`
- Test: `tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`

**Interfaces:**
- Implements `SlaveTatsAppearanceBackend::writeAppearance(handle, request)`.
- `SlaveTatsAppearanceBindings` continues to expose typed integer/float get/set delegates for deterministic runtime tests.
- Writes exact keys: `color`, `invertedAlpha`, `glow`, `glossiness`, `specularStrength`, and `emissiveMult`.

- [ ] **Step 1: Write failing binding tests for every key and readback failure order**

Replace the single-key `BindingState` storage with key-aware maps keyed by `(handle, key)`. Assert success writes exact values for all six keys; alpha must write `0.65F` for visible alpha `0.35F`. Add one readback-failure case for each new material key and assert the operation returns `updateFailed`, does not mark updated, and does not synchronize. Assert earlier keys may already be stored, documenting sequential partial-storage behavior.

- [ ] **Step 2: Run the runtime appearance executable and verify it fails**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\SlaveTatsRuntimeAppearanceTests.exe`

Expected: FAIL because only color and inverted alpha are written and verified.

- [ ] **Step 3: Implement the verified write sequence**

For binding mode, call `setTattooInt`/`getTattooInt` for `color` and `glow`, then `setTattooFloat`/`getTattooFloat` for `invertedAlpha`, `glossiness`, `specularStrength`, and `emissiveMult`. For production mode, use `jcmini::JMap::setIntAndVerify` and `setFltAndVerify` with the same keys and order. Return `false` immediately on the first failed verification.

Convert visible alpha using `toSlaveTatsInvertedAlpha(request.alpha)` only in this backend. Do not write `glowTexture` or `bump`.

- [ ] **Step 4: Run the runtime appearance executable and verify it passes**

Run: `.\build.ps1 -Config debug; & .\build\debug\tests\SlaveTatsRuntimeAppearanceTests.exe`

Expected: PASS, including exact-key writes, each failure boundary, stale no-write behavior, and sync-only no-write behavior.

## Task 4: Read advanced fields into runtime snapshots

**Files:**
- Modify: `src/runtime/SlaveTatsRuntime.cpp`
- Test: `tests/runtime/SlaveTatsRuntimeTests.cpp` or the existing focused query test file selected by `CMakeLists.txt`

**Interfaces:**
- `SlaveTatsRuntime::queryAvailable()` and `querySlots()` populate all six `TattooEntry` fields.
- Reads keys `glow`, `glossiness`, `specularStrength`, `bump`, `glowTexture`, and `emissiveMult` with documented defaults.

- [ ] **Step 1: Locate the existing query test target and add failing snapshot assertions**

Use `rg -n "queryAvailable|querySlots|glowTexture|invertedAlpha" tests CMakeLists.txt` to identify the target that exercises real JContainers query extraction. Seed a tattoo with distinct advanced values, call both query methods, and assert every field is preserved. Add a separate missing-key fixture and assert the documented defaults.

- [ ] **Step 2: Run the selected query test target and verify it fails**

Run the exact executable registered for the selected source in `CMakeLists.txt` after building with `.\build.ps1 -Config debug`.

Expected: FAIL because snapshot construction currently reads only color, lock, and alpha.

- [ ] **Step 3: Populate snapshots with field-specific defaults**

In both `queryAvailable()` and `querySlots()`, read:

```cpp
.glow = jcmini::JMap::getInt(handle, "glow", 0),
.glossiness = jcmini::JMap::getFlt(handle, "glossiness", 0.0F),
.specularStrength = jcmini::JMap::getFlt(handle, "specularStrength", 0.0F),
.bump = jcmini::JMap::getStr(handle, "bump"),
.glowTexture = jcmini::JMap::getStr(handle, "glowTexture"),
.emissiveMult = jcmini::JMap::getFlt(handle, "emissiveMult", 1.0F),
```

Do not derive either texture path from the diffuse texture and do not normalize or discard supplied material values.

- [ ] **Step 4: Run the selected query target and verify it passes**

Run the same target from Step 2.

Expected: PASS for populated and missing-key snapshots.

## Task 5: Full regression and PR preparation

**Files:**
- Modify: all files from Tasks 1-4 only.

- [ ] **Step 1: Run Debug build and complete Debug test suite**

Run: `.\build.ps1 -Config debug; ctest --test-dir build\debug --output-on-failure`

Expected: zero build failures and all Debug tests passing.

- [ ] **Step 2: Run Release build and complete Release test suite**

Run: `.\build.ps1 -Config release; ctest --test-dir build\release --output-on-failure`

Expected: zero build failures and all Release tests passing.

- [ ] **Step 3: Review the staged scope and whitespace**

Run: `git diff --check; git diff -- src/core/TattooModels.h src/core/SlaveTatsService.cpp src/runtime/UpdateTattooAppearanceOrchestration.h src/runtime/UpdateTattooAppearanceOrchestration.cpp src/runtime/SlaveTatsRuntime.h src/runtime/SlaveTatsRuntime.cpp tests/core/SlaveTatsServiceTests.cpp tests/runtime/UpdateTattooAppearanceOrchestrationTests.cpp tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp`

Expected: no whitespace errors, no changes outside the listed runtime foundation scope, and no secret-like values.

- [ ] **Step 4: Present diff and commit proposal for approval**

Proposed commit:

```text
feat: add advanced material runtime support
```

Do not commit or push until the user explicitly approves the reviewed diff and exact message.
