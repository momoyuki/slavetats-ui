# SLO Aroused NG Reactive Tattoo Integration Plan

## Status

Future feature. Do not implement until the SKSE Menu Framework migration is complete enough that the native UI, tattoo repository, and texture/resource layers are stable.

This plan is intentionally separate from the SKSE Menu migration so the UI rewrite does not become coupled to gameplay-reactive tattoo behavior.

## Goal

Add an optional arousal-reactive tattoo subsystem inspired by LewdMarks Aroused.

The initial behavior should allow selected SlaveTats tattoos to change visual properties as an actor's arousal changes, with glow/emissive response as the primary target and alpha/color response as optional extensions.

The feature should read arousal from SLO Aroused NG when available and should degrade cleanly when SLO Aroused NG is not installed.

## Reference behavior

LewdMarks Aroused is a small integration that makes LewdMarks glow at high arousal and exposes configuration through MCM.

SlaveTatsNG already supports overlay properties needed for a more general version of this idea. Its native overlay layer supports diffuse color, emissive color, alpha, glow map, emissive multiplier, glossiness, and specular strength.

The current SlaveTatsUI code also already edits applied tattoo data through JContainers and calls `synchronize_tattoos`, which provides a possible path for updating reactive tattoo properties without rebuilding the tattoo system.

## Dependency strategy

### Hard requirements

- SKSE
- SlaveTatsNG
- the completed/native SlaveTatsUI core and SKSE Menu layers

### Optional integration

- SLO Aroused NG

SLO Aroused NG must remain optional. SlaveTatsUI must continue to work normally when it is absent.

Do not make the project require an ESP/master solely for arousal integration.

## Preferred SLO Aroused NG integration

SLO Aroused NG exposes a C++ API specifically for SKSE plugins. Prefer that interface over routing reads through MCM or high-frequency Papyrus calls.

Conceptually:

```text
Actor
  |
  v
SLO Aroused NG C++ API
  |
  v
arousal float
  |
  v
ReactiveTattooService
  |
  v
mapped visual state
  |
  v
SlaveTatsNG applied tattoo
  |
  v
synchronize overlay
```

Expected read path:

```cpp
float arousal = SLA_GetArousal(actor);
```

The exact exported symbols/loading contract must be taken from the supported SLO Aroused NG C++ API at implementation time and resolved dynamically (for example through the documented DLL API mechanism), so SLO remains optional.

SLO arousal values are conventionally 0-100 but are not technically guaranteed to remain inside that range. Clamp the value for UI/effect mapping unless a profile explicitly opts into another range.

## Do not poll every frame

Reactive tattoo updates must not run in the ImGui render loop.

Preferred update triggers, in order of preference:

1. react to SLO Aroused NG's arousal-update notification/event if a safe C++ integration exists,
2. otherwise use a low-frequency scheduler,
3. refresh explicitly when actor/tattoo state changes,
4. never query and synchronize tattoos every rendered frame.

Arousal only needs to be sampled frequently enough for the visual transition to feel responsive.

Initial fallback polling target for profiling: 1-2 seconds for actively tracked actors, with no polling for actors that have no reactive tattoos.

## Reactive tattoo profile

Do not make every tattoo reactive by default.

A user should explicitly enable/reactively configure a tattoo or a rule should target a known source/section.

Suggested model:

```cpp
struct ReactiveTattooProfile {
    bool enabled = false;

    float minArousal = 0.0f;
    float maxArousal = 100.0f;

    bool affectGlow = true;
    bool affectAlpha = false;
    bool affectColor = false;

    int lowEmissiveColor = 0x000000;
    int highEmissiveColor = 0xFFFFFF;

    float lowEmissiveMult = 0.0f;
    float highEmissiveMult = 1.0f;

    float lowAlpha = 1.0f;
    float highAlpha = 1.0f;

    int lowColor = 0xFFFFFF;
    int highColor = 0xFFFFFF;
};
```

The final model may be split into reusable curves/effects, but the MVP should stay small.

## Stable tattoo identity

Reactive configuration must not be keyed only by transient JContainers handles.

Use the stable tattoo identity established by the SKSE Menu migration, expected to include enough of:

```text
source JSON / pack
section
name
texture path
```

Applied slot and actor are runtime state, not tattoo-definition identity.

## Mapping arousal to visual state

Normalize a clamped arousal value into a profile range:

```text
t = clamp((arousal - minArousal) / (maxArousal - minArousal), 0, 1)
```

Then derive properties from `t`.

MVP mapping:

- emissive multiplier: linear interpolation,
- alpha: optional linear interpolation,
- color/emissive color: optional RGB interpolation,
- glow-map swapping: defer unless needed by real tattoo packs.

Example default inspired by LewdMarks Aroused:

```text
Arousal 0-40   -> no glow
Arousal 40-70  -> gradually increasing glow
Arousal 70-100 -> strong glow
```

The exact defaults should be configurable and reviewed after in-game testing.

## Thresholds and hysteresis

If an effect uses discrete thresholds, do not let it rapidly toggle when arousal oscillates around the boundary.

Use hysteresis, for example:

```text
turn ON at 70
turn OFF at 65
```

Continuous interpolation normally avoids this problem, but threshold-based rules may still be useful for enable/disable or texture swapping.

## Preserve the user's baseline tattoo settings

Reactive effects are an overlay on top of the tattoo's normal state.

Before a reactive profile modifies an applied tattoo, preserve the relevant baseline properties:

- diffuse color,
- alpha,
- emissive/glow properties,
- any other field the implementation changes.

When any of these occur:

- reactive behavior is disabled,
- SLO Aroused NG disappears/unloads,
- the profile is removed,
- the actor is no longer tracked,

restore the baseline rather than resetting to arbitrary defaults.

Do not overwrite unrelated tattoo properties.

## SlaveTatsNG integration constraints

The currently vendored `SlaveTatsNG_Interface.h` exposes tattoo query/add/remove/synchronize operations but does not expose a dedicated public function for updating emissive/glow fields.

SlaveTatsNG's internal overlay API does support:

```text
diffuse color
emissive color
glossiness
alpha
specular strength
glow map
emissive multiplier
```

Before implementation, choose the least fragile update path:

1. preferred: use/extend a supported SlaveTatsNG public API for applied-tattoo visual properties,
2. acceptable if confirmed stable: edit the applied tattoo JContainer fields and call `synchronize_tattoos`, consistent with the project's current color/alpha update path,
3. avoid directly duplicating NiOverride state management inside SlaveTatsUI unless SlaveTatsNG cannot provide a stable integration surface.

If SlaveTatsNG needs an interface extension, keep that work isolated and version-gated.

## Actor scope

MVP recommendation:

- Player: supported first.
- Actor currently selected in the SKSE Menu: supported after Player path is stable.
- Nearby NPC background tracking: deferred until performance/profile behavior is verified.

Do not scan every loaded actor for arousal by default.

An actor should only enter the reactive update set when they actually have at least one reactive tattoo applied.

## SKSE Menu UX

Add the feature only after the native tattoo editor exists.

Suggested tattoo editor section:

```text
Reactive / Arousal
[x] React to SLO Aroused NG

Arousal range
Min [ 40 ]   Max [ 100 ]

[x] Glow
Glow strength   0.0 -------- 1.5

[ ] Alpha
[ ] Color

Current arousal: 73.4
Preview response: 0.56
```

Useful controls:

- enable per tattoo,
- min/max arousal,
- low/high glow strength,
- optional glow/emissive color,
- optional alpha/color response,
- live display of selected actor's current arousal,
- Reset to tattoo baseline.

Do not require MCM for normal configuration once the SKSE Menu version exists.

## Configuration persistence

Reactive profiles belong to SlaveTatsUI, not to third-party tattoo-pack JSON files.

Do not modify installed tattoo-pack JSONs.

Store user rules separately, keyed by stable tattoo identity.

Possible future configuration shape:

```json
{
  "profiles": {
    "SomePack.json|LewdMarks|Mark01|textures/...dds": {
      "enabled": true,
      "minArousal": 40.0,
      "maxArousal": 100.0,
      "glow": {
        "enabled": true,
        "lowMultiplier": 0.0,
        "highMultiplier": 1.5,
        "color": "#FF4080"
      }
    }
  }
}
```

Exact persistence format should follow whatever settings/storage layer the native UI adopts.

## Performance requirements

- zero arousal work for actors with no reactive tattoos,
- no per-frame SLO reads,
- no per-frame `synchronize_tattoos`,
- cache the last sampled arousal and last applied visual state,
- skip synchronization when the mapped visual state has not changed enough to matter,
- coalesce multiple reactive tattoo changes for the same actor into one synchronization pass,
- avoid repeated JContainers full-library queries during arousal updates.

Use a configurable/constant epsilon for continuous values so tiny float changes do not trigger overlay rebuilds.

Example:

```text
if abs(newEmissiveMult - oldEmissiveMult) < 0.02:
    skip update
```

## Failure behavior

If SLO Aroused NG is unavailable:

- SlaveTatsUI still loads,
- normal tattoo browsing/editing works,
- reactive controls show integration unavailable,
- existing reactive tattoos fall back to/restored baseline state where safely possible,
- no error spam every update tick.

If an actor/tattoo disappears during an update, drop the tracking entry safely.

## Implementation dependency

This feature is blocked on the SKSE Menu migration work.

Do not begin the implementation PR until these pieces are stable enough to reuse:

- UI-independent SlaveTats service,
- stable tattoo data model/identity,
- tattoo repository,
- native SKSE Menu tattoo editor,
- applied tattoo update/synchronize path.

The arousal system should consume those abstractions rather than adding new logic back into `Bridge.cpp`.

## Suggested future architecture

```text
SLO Aroused NG
      |
      v
ArousalProvider
      |
      v
ReactiveTattooService
      |
      +--> ReactiveProfileStore
      |
      +--> SlaveTatsService
                |
                v
        applied tattoo state
                |
                v
        synchronize_tattoos
```

Suggested files:

```text
src/integrations/SLOArousedProvider.h
src/integrations/SLOArousedProvider.cpp
src/reactive/ReactiveTattooService.h
src/reactive/ReactiveTattooService.cpp
src/reactive/ReactiveTattooProfile.h
src/reactive/ReactiveProfileStore.h
src/reactive/ReactiveProfileStore.cpp
```

## Proposed implementation sequence

### Phase 1 — Optional SLO provider

- dynamically detect SLO Aroused NG,
- resolve supported C++ API,
- read selected actor arousal,
- expose `IsAvailable()` and `GetArousal(actor)` behind a small interface.

### Phase 2 — Reactive profile storage

- profile keyed by stable tattoo identity,
- persist independently of tattoo packs,
- no runtime visual changes yet.

### Phase 3 — Player glow MVP

- Player only,
- one or more explicitly enabled tattoos,
- arousal -> emissive/glow intensity,
- baseline preservation/restoration,
- coalesced SlaveTats synchronization.

### Phase 4 — Native menu editor

- configure range/glow in SKSE Menu,
- display live arousal,
- enable/disable/reset.

### Phase 5 — Additional actors and optional properties

After profiling:

- selected NPC support,
- alpha mapping,
- color mapping,
- presets/rules by source pack or section,
- optional threshold/curve presets.

## Testing checklist

- SlaveTatsUI starts without SLO Aroused NG installed.
- SLO availability is detected after game data is ready.
- Player arousal reads correctly.
- Values below 0 / above 100 are safely mapped.
- Non-reactive tattoos are never modified.
- Glow increases across the configured range.
- No rapid threshold flicker.
- Disabling a profile restores baseline tattoo state.
- Removing SLO integration does not corrupt tattoo state.
- Multiple reactive tattoos on one actor synchronize in one pass.
- No update/sync work happens every render frame.
- Save/reload preserves reactive configuration.
- Removing a tattoo removes/detaches its runtime tracking safely.
- Selected NPC support does not create a global loaded-actor scan.

## Open design questions

1. Which exact applied-tattoo JContainer fields does current SlaveTatsNG synchronize for emissive color, glow map, and emissive multiplier?
2. Should we request/use a formal SlaveTatsNG public interface extension rather than mutate those fields directly?
3. Should the default behavior use continuous glow interpolation or a LewdMarks-style high-arousal threshold?
4. Should profiles be configured per tattoo definition, per applied instance, or support both?
5. Should pack/section rules be supported in the first release or only per-tattoo profiles?
6. How should user overrides interact with a currently active reactive state when color/alpha is manually edited?
7. What update cadence is visually smooth without causing unnecessary overlay synchronization?

## Decision

Plan this as a separate post-SKSE-Menu feature rather than expanding the current UI migration scope.

Use SLO Aroused NG as an optional arousal provider, keep SlaveTatsNG responsible for overlay application, and make reactive behavior opt-in and profile-driven.

The first implementation should reproduce the useful core idea of LewdMarks Aroused — stronger visual glow at higher arousal — before adding broader rule engines or complex effects.