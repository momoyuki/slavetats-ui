# Native Actor Targeting Design

## Context

The native slot-first workflow currently manages only the Player. Core service
and runtime requests already carry an explicit `actorFormId`, and
`SlaveTatsRuntime` resolves that exact actor before querying or mutating.
However, `NativeSlotWorkflowModel` still hard-codes the Player form ID for slot
queries, apply requests, and refreshes.

Roadmap vNext.3 extends the same workflow to a deliberately selected loaded
Actor without weakening actor-scoped handle validation, external-slot
read-only behavior, or synchronization-only retry semantics.

## Goals

- Preserve Player targeting as the default and existing behavior.
- Add an explicit `Crosshair Target` mode for a loaded Actor.
- Display the active Actor name and form ID on every workflow screen.
- Keep the selected Actor fixed for the session until the user deliberately
  changes or refreshes it.
- Route crosshair resolution through a typed runtime boundary rather than the
  UI adapter or workflow model.
- Scope every query and mutation to the active Actor form ID.
- Clear actor-specific workflow state when the target changes.
- Reject stale completions from a previous Actor generation.
- Never silently fall back to the Player.

## Non-goals

- No arbitrary NPC list, follower picker, or search UI.
- No target persistence across native-menu sessions.
- No automatic retargeting as the crosshair moves.
- No mutation of unloaded Actors or references without loaded 3D.
- No automatic Player fallback after crosshair resolution failure.
- No change to external overlay ownership.
- No live actor preview, camera control, or freeze behavior.

## User Behavior

Every workflow screen presents a target selector:

```text
[Player] [Crosshair Target]    Lydia [0x000A2C8E]    [Refresh Target]
```

Player remains selected by default. Selecting `Crosshair Target` resolves the
Actor currently held by Skyrim's crosshair pick data. The chosen Actor remains
fixed even if the crosshair later moves. `Refresh Target` deliberately resolves
the current crosshair again.

While resolving, the UI shows `Resolving target...` and disables slot and
mutation controls. If no valid loaded Actor is available, the UI remains in
Crosshair mode, shows `No valid crosshair Actor`, exposes no active Actor, and
does not query or mutate the Player.

The target line displays the Actor's display name plus an eight-digit uppercase
hexadecimal form ID. If Skyrim supplies no display name, present `Unnamed Actor`
with the form ID. The name is presentation metadata only; form ID is identity.

Target selector and refresh actions are disabled while a mutation is in flight.
Changing target clears Current Slot state, selected slot, preview, edit session,
pagination, and actor-scoped errors before querying the new Actor.

## Architecture

```text
OfficialMenuFrameworkAdapter
  presents Player / Crosshair intent and target status
              |
              v
NativeSlotWorkflowModel
  owns copyable ActorTarget state and generation-scoped tickets
              |
              v
NativeSlotWorkflowRuntime
  schedules target resolution and all tattoo operations
              |
              +--> ActorTargetProvider
              |      resolves CrosshairPickData::targetActor
              |
              +--> SlaveTatsService --> ITattooRuntime --> SlaveTatsRuntime
```

The adapter expresses intent only. It must not access `RE::CrosshairPickData`,
resolve Skyrim forms, retain Actor pointers, or call SlaveTatsNG/JContainers.

`ActorTargetProvider` is a small injected callable or interface at the native
runtime boundary. Its production implementation reads
`RE::CrosshairPickData::targetActor`, resolves the handle to `RE::Actor`, checks
for nonzero form ID and loaded 3D, and copies the display name and form ID into
a value result. It does not persist a raw Actor pointer.

## Target Contracts

The workflow uses copyable value types:

```cpp
enum class ActorTargetKind {
    player,
    crosshair,
};

struct ActorTarget {
    ActorTargetKind kind{ActorTargetKind::player};
    std::uint32_t formId{0x14};
    std::string displayName{"Player"};
};
```

Crosshair resolution produces `std::expected<ActorTarget, ServiceError>` through
a generation-tagged ticket. A successful crosshair result must have kind
`crosshair`, a nonzero form ID, and a copied non-empty presentation name after
fallback normalization. Resolution errors use a dedicated stable error code or
the existing actor-not-found category with a clear crosshair-specific message.

The Player target is constructed locally as the stable form ID `0x14`; it does
not require crosshair resolution. This is an explicit selection, not fallback.

## Workflow State and Transitions

`NativeSlotWorkflowModel` owns one active `ActorTarget` or an empty target while
Crosshair resolution has failed. It also owns a monotonically increasing target
generation.

Selecting Player:

1. Reject the change if a mutation is in flight.
2. Advance the target generation.
3. Clear all actor-scoped workflow and per-area slot caches.
4. Set the explicit Player target.
5. Query the selected area using Player form ID.

Selecting or refreshing Crosshair Target:

1. Reject the action if a mutation is in flight.
2. Advance the target generation.
3. Clear all actor-scoped workflow and per-area slot caches.
4. Enter resolving state with no active Actor.
5. Emit one target-resolution ticket.
6. On matching success, store the returned value and query the selected area.
7. On matching failure, remain in Crosshair mode with no active Actor and show
   the error.

Changing Body/Face/Hands/Feet retains the active Actor but continues to use the
existing per-area cache within that Actor generation. Target changes invalidate
all four area caches and reset page indices.

## Actor-Scoped Requests

Replace every workflow-level Player constant used for tattoo operations with
the active target form ID. This applies to:

- slot queries and refreshes;
- apply and replace;
- remove;
- Edit Appearance and synchronization-only retry;
- Lock / Unlock.

Before issuing a mutation, the model must verify that the current Slot Snapshot
belongs to the active Actor. A missing target, mismatched snapshot actor, stale
runtime handle, external slot, or in-flight target change creates no mutation
ticket.

Core service and runtime layers continue resolving exactly the requested form
ID. Actor lookup failure stops the operation. Existing handle membership checks
remain actor-scoped, and no operation may substitute the Player.

## Concurrency and Stale Completion Safety

Target resolution, slot query, and mutation tickets carry sufficient generation
context to prove they belong to the active target. A completion from an older
target must not populate caches, change screens, expose errors, schedule a
refresh, or mutate current UI state.

The existing scheduler's single-in-flight mutation protection remains. Target
selection and refresh are disabled while apply, remove, appearance update,
synchronization retry, or Lock / Unlock is pending or active.

Slot query completion must additionally confirm its returned `actorFormId`
matches the active target before accepting the snapshot.

## Error Handling

- Missing crosshair singleton/handle: `No valid crosshair Actor`; no slot query.
- Crosshair points to a non-Actor: same failure; no Player fallback.
- Actor has zero form ID or no loaded 3D: reject before slot query.
- Empty Actor display name: normalize presentation to `Unnamed Actor`.
- Actor unloads after selection: runtime lookup/query/mutation returns an Actor
  error; retain the selected identity for deliberate refresh or reselection.
- Snapshot actor mismatch: reject as stale workflow state; no mutation.
- Scheduler exception during target resolution: clear resolving state and expose
  a stable target-resolution error.
- Obsolete completion: ignore without changing the current target or error.

## Presentation

The target selector and target identity are shared header content across Current
Slots, Picker, Preview, Slot Actions, Remove confirmation, and Edit Appearance.
The Current Slots title uses `Current Tattoos - <Actor Name>` rather than a
hard-coded Player label.

When no Crosshair Actor is active, slot content uses an explicit empty/error
state rather than displaying Player data. `Refresh Target` is available in
Crosshair mode only. Close and existing page/navigation placement remain
unchanged.

## Testing

### Target Provider

- resolves `targetActor` to an Actor and copies exact form ID/display name;
- rejects missing singleton/handle, non-Actor, zero form ID, and unloaded 3D;
- normalizes an empty display name;
- retains no raw Actor pointer in its result.

### Workflow Model

- starts with the explicit Player target and preserves current behavior;
- Crosshair selection clears all per-area caches and actor-scoped state;
- successful resolution queries the exact crosshair Actor form ID;
- failed resolution produces no Player query or fallback;
- Player selection after Crosshair failure queries only `0x14`;
- target switching is rejected during mutation;
- every query/apply/remove/edit/retry/lock request carries the active Actor ID;
- mismatched Slot Snapshot actor creates no mutation ticket;
- stale resolution, query, and mutation completions cannot affect a newer target.

### Runtime Coordinator

- schedules one target-resolution operation at a time;
- converts provider and scheduler failures into typed completion results;
- preserves existing single-in-flight mutation behavior.

### Adapter

- presents Player/Crosshair selection and active target identity;
- presents resolving, missing-target, and error states;
- disables selector/refresh during mutation;
- changes the Current Slots title without moving Close/navigation controls.

### Regression

- Core and SlaveTatsRuntime actor-scoped validation remains green;
- external slots stay read-only;
- synchronization-only retry retains the original Actor;
- full Debug and Release suites pass.

## In-Game Acceptance

1. Open the menu with Player selected and verify existing behavior is unchanged.
2. Aim at a loaded NPC, select Crosshair Target, and verify name/FormID and slots.
3. Apply, replace, remove, edit appearance, Lock, and Unlock on that NPC.
4. Confirm Player slots remain unchanged.
5. Aim at a different NPC and Refresh Target; verify all slots/state change to
   the new Actor with no stale data.
6. Select Crosshair with no Actor or a non-Actor target; verify error and no
   Player data/fallback.
7. Cause the selected Actor to unload, then attempt an operation; verify a clear
   failure and no mutation of Player or another Actor.
8. Switch target around slow queries and confirm old completions never populate
   the new target.
9. Confirm external slots remain read-only for Player and NPC targets.

## Implementation Boundaries

- `src/native/NativeSlotWorkflowModel.*`
- `src/native/NativeSlotWorkflowRuntime.*`
- a focused native Actor target provider seam and production implementation;
- `src/native/OfficialMenuFrameworkAdapter.*`
- application composition only for dependency injection;
- target-provider, workflow, coordinator, adapter, and regression tests.

Do not add direct Skyrim, JContainers, or SlaveTatsNG access to the adapter or
workflow model. Do not expand this slice into follower lists, target persistence,
camera/freeze controls, or unloaded-actor support.
