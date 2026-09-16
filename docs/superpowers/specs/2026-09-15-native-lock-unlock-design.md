# Native Lock / Unlock Design

## Context

`TattooEntry` already exposes the SlaveTatsNG `locked` field in native slot
snapshots. The native UI currently offers Replace and Remove for every
SlaveTats-managed occupied slot, including a locked tattoo. SlaveTatsNG then
rejects the later mutation. The result is a late, unclear failure and does not
offer a way to change the lock state without returning to MCM.

This design completes the Lock / Unlock part of roadmap vNext.2. It uses the
same persistent JContainers field that SlaveTatsNG MCM uses, while preserving
the existing native workflow boundaries and stale-handle guarantees.

## Goals

- Show whether a SlaveTats-managed tattoo is locked.
- Lock and unlock an owned, currently applied tattoo from Slot Actions.
- Disable Replace and Remove before they can issue a known-invalid request.
- Keep Edit Appearance available while locked.
- Verify the JContainers write by reading back the exact `locked` value.
- Reject stale, foreign, empty, and external targets before any write.
- Refresh only the selected area after a successful lock-state change.
- Keep all SlaveTatsNG and JContainers access behind `ITattooRuntime`.

## Non-Goals

- No domain selector or domain discovery.
- No NPC targeting.
- No confirmation dialog for Lock or Unlock.
- No force override of a lock during Replace or Remove.
- No mutation of external overlays.
- No actor `updated` marker or tattoo synchronization for lock-state changes.
- No retry-synchronization state: lock changes storage policy, not visible
  overlay appearance.

## User Behavior

For an occupied SlaveTats-managed Current Slot:

| Current state | Available actions |
| --- | --- |
| Unlocked | Replace, Remove, Edit Appearance, Lock |
| Locked | Edit Appearance, Unlock |

The Slot Actions view displays a `Locked` badge when applicable. For a locked
tattoo, Replace and Remove remain visible but disabled with the helper text
`Unlock to replace or remove.` This communicates why those actions are
unavailable without hiding capability.

Lock and Unlock take effect immediately after click. While the operation is
in flight, the action is disabled. A successful result returns to Current Slots
and refreshes the selected area. A failure keeps Slot Actions open, preserves
the selected slot, shows the error, and permits a deliberate retry.

Edit Appearance remains available because locking controls replacement/removal
policy only; it must not silently change material-edit behavior.

## Architecture

```text
OfficialMenuFrameworkAdapter
  presents Lock / Unlock intent and disabled actions
              |
              v
NativeSlotWorkflowModel
  owns selected slot, ticket generation, and UI transitions
              |
              v
NativeSlotWorkflowRuntime
  schedules one typed lock-state operation
              |
              v
SlaveTatsService
  validates availability and request shape
              |
              v
ITattooRuntime / SlaveTatsRuntime
  resolves actor, validates current ownership, writes JMap locked field
```

The UI adapter and workflow model must not directly call JContainers or
SlaveTatsNG. The new operation must share the coordinator's existing
single-in-flight protection and generation-based stale-completion handling.

## Core Contract

Add a typed operation rather than reusing appearance update or a UI-only flag:

```cpp
struct SetTattooLockedRequest {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};

struct SetTattooLockedSuccess {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};
    bool locked{};
};

using SetTattooLockedResult =
    std::expected<SetTattooLockedSuccess, ServiceError>;
```

`ITattooRuntime` and `SlaveTatsService` gain `setTattooLocked`. The service
rejects unavailable SlaveTatsNG/JContainers, zero actor ID, and zero runtime
handle before forwarding. Use a dedicated `lockFailed` service error for a
write/readback failure. Reuse `staleTattooHandle` for a handle that is absent,
foreign, no longer SlaveTats-managed, or otherwise not mutable.

Do not overload `UpdateTattooAppearanceRequest`: the lock field has unrelated
validation, persistence, synchronization, and retry semantics.

## Runtime Behavior

`SlaveTatsRuntime::setTattooLocked` must:

1. Resolve exactly `request.actorFormId`; never fall back to Player.
2. Query the actor's current SlaveTats tattoos and find `runtimeHandle`.
3. Confirm the found tattoo belongs to the requested actor and remains a
   SlaveTats-managed current slot.
4. Write `locked` as integer `1` or `0` through the existing verified integer
   JContainers seam.
5. Read back the same value before reporting success.
6. Return the actor, handle, and resulting boolean on success.

It must perform zero writes when validation fails. It must not set
`.SlaveTats.updated`, call `synchronize_tattoos`, or interact with external
overlay slots. Lock state is consumed by SlaveTatsNG mutation policy; it does
not change the rendered overlay.

The runtime must clean its JContainer pool on every return path, including
actor lookup, stale validation, write, and readback failures.

## Workflow Model and Scheduler

Add `SlotLockTicket`, pending/active lock ticket state, and a typed operation
alias in `NativeSlotWorkflowRuntime`. `toggleSelectedSlotLock()` derives the
desired state from the selected Slot Snapshot and is valid only when:

- the screen is Slot Actions;
- the selected slot exists and is SlaveTats-managed;
- its tattoo metadata exists with a nonzero runtime handle; and
- no conflicting mutation is in flight.

The ticket contains explicit Player actor form ID, area, slot, runtime handle,
and desired lock state. Area and slot remain workflow context for completion
and refresh; the runtime mutation authorizes by actor plus handle.

On success, clear the active ticket, return to Current Slots, and invalidate
only the selected area's snapshot before scheduling its refresh. On failure,
clear the active ticket, retain Slot Actions and selection, and expose the
service error. Completion for an obsolete generation must be ignored.

## UI Presentation

Slot cards show `Locked` for a locked SlaveTats-managed tattoo. Slot Actions:

```text
Replace                 [enabled only when unlocked]
Remove                  [enabled only when unlocked]
Edit Appearance         [always enabled for owned occupied tattoos]
Lock / Unlock           [immediate action]
```

The action label follows the latest Slot Snapshot. Do not optimistically flip
the cached lock bit: use the verified runtime completion and area refresh as
the source of truth.

## Error Handling

- Missing API/JContainers: report existing availability errors; no runtime call.
- Zero/unknown actor or zero handle: reject before mutation.
- Stale/foreign/non-owned handle: report `staleTattooHandle`; zero writes.
- External or empty selected slot: model creates no ticket.
- Failed JContainers write/readback: report `lockFailed`; no UI state flip.
- Scheduler exception: convert to `lockFailed`, clear in-flight state, keep the
  user on Slot Actions.
- A newer selection, refresh, or close invalidates older completions.

## Testing

### Core Service

- forwards valid lock and unlock requests unchanged;
- rejects unavailable runtime dependencies, zero actor ID, and zero handle;
- performs no runtime call for rejected requests.

### Runtime

- locking writes verified integer `1`; unlocking writes verified integer `0`;
- a successful operation returns the requested actor, handle, and state;
- stale, absent, foreign, or external handles produce zero writes;
- write/readback failure returns `lockFailed`;
- neither outcome sets `updated` nor calls synchronization;
- all JContainer pools are cleaned on success and every failure path.

### Native Workflow and Scheduler

- unlocked owned slot produces a Lock ticket; locked produces Unlock;
- empty/external/missing-handle slots produce no ticket;
- completion success refreshes only the selected area and returns to slots;
- failure retains Slot Actions and error for retry;
- duplicate clicks during in-flight work issue one operation;
- stale completion cannot change a later selection;
- scheduler exception clears in-flight state and reports a stable error.

### Adapter

- locked badge renders only for locked SlaveTats-managed slots;
- Replace and Remove are disabled only while locked;
- Edit Appearance remains enabled while locked;
- Lock/Unlock label and disabled-in-flight state match workflow presentation.

## In-Game Acceptance

1. Open an unlocked owned tattoo and lock it.
2. Reopen Slot Actions: `Locked` appears; Replace and Remove are unavailable;
   Edit Appearance still opens.
3. Unlock it and confirm Replace and Remove become available again.
4. Open SlaveTatsNG MCM and confirm its lock state agrees with the native UI.
5. Lock in MCM, refresh native Current Slots, and confirm native restrictions
   update from the authoritative snapshot.
6. Attempt rapid repeated Lock/Unlock clicks; only one mutation is applied.
7. Confirm external slots remain read-only and never receive a lock action.
8. Confirm locking/unlocking does not visibly reapply, flicker, or synchronize
   the tattoo.

## Implementation Boundaries

- `src/core/TattooModels.h`
- `src/core/ITattooRuntime.h`
- `src/core/SlaveTatsService.*`
- `src/runtime/SlaveTatsRuntime.*`
- `src/native/NativeSlotWorkflowModel.*`
- `src/native/NativeSlotWorkflowRuntime.*`
- `src/native/OfficialMenuFrameworkAdapter.*`
- Core, Runtime, Native Workflow/Runtime, and Adapter tests

Keep domain work out of this change. It remains the separate second slice of
vNext.2 after Lock / Unlock is complete and verified.
