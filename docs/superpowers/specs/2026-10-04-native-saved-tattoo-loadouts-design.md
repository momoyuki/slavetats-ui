# Native Saved Tattoo Loadouts Design

**Status:** Accepted.

**Roadmap milestone:** vNext.4 -- Workflow Quality of Life

## Goal

Let users save the complete SlaveTats-managed appearance of one Actor Target
across Body, Face, Hands, and Feet, then restore that named loadout to any
explicitly selected loaded Actor. A restore must remain actor-scoped, preserve
external overlays, respect locked tattoos, and avoid leaving an unexplained
partial result when a multi-slot operation fails.

Loadouts are global configuration shared by every savegame and Actor Target
that resolves the same `SlaveTatsUI.json` path. They are visual templates, not
ownership records for a specific Actor.

## Scope

The MVP supports creating, loading, overwriting, renaming, and deleting up to
twenty named loadouts. The JSON entry order is the presentation order.

Each stored tattoo contains:

- area and exact slot index;
- SlaveTats domain, section, and name;
- diffuse color and visible alpha;
- glow color and emissive multiplier; and
- glossiness and specular strength.

One loadout covers all four supported areas. Loading uses exact-restore
semantics: every mutable SlaveTats-managed slot must match the stored loadout,
including removing managed tattoos that are absent from it.

## Non-goals

- Storing an Actor form ID, runtime tattoo handle, or savegame identity.
- Storing or restoring locked state.
- Storing arbitrary glow or bump texture paths.
- Editing external overlays or using their slots as fallback capacity.
- Moving a stored tattoo to a different slot automatically.
- Best-effort loading of an incomplete loadout when a tattoo pack is missing.
- Recording every restored tattoo in Recently Used.
- Import, export, automatic sorting, or in-game move-up/move-down controls.
- Combining this slice with controlled alternate glow/bump texture selection.

## Terminology

**Saved Tattoo Loadout** is a named, ordered, global snapshot of the supported
appearance values and exact area/slot placement for SlaveTats-managed tattoos.

**Loadout Plan** is an immutable, actor-scoped description of the additions,
replacements, removals, appearance updates, unchanged slots, and preserved
external slots required for exact restore.

**Preflight** is the read-only validation that creates a Loadout Plan from
fresh runtime and catalog state. A rejected Preflight performs no mutation.

**Rollback Snapshot** is an ephemeral runtime snapshot captured immediately
before mutation. It is not persisted in the loadout JSON and may include
runtime-only fields required to restore the original state safely.

## Persistent Domain Model

The persistence layer owns transport-independent values equivalent to:

```cpp
struct TattooLoadoutEntry {
    core::TattooArea area;
    std::int32_t slot;
    std::string domain;
    std::string section;
    std::string name;
    std::int32_t color;
    float alpha;
    std::int32_t glow;
    float emissiveMult;
    float glossiness;
    float specularStrength;
};

struct TattooLoadout {
    std::string name;
    std::vector<TattooLoadoutEntry> entries;
};
```

The exact C++ ownership and file placement are implementation decisions, but
the store contract must not depend on Skyrim types, raw Actor pointers,
JContainers handles, or UI state.

Names are trimmed before validation. A trimmed name must not be empty. Name
membership is case-insensitive under the same deterministic ASCII-folding rule
used by Appearance Presets. Stored spelling is preserved. The store accepts at
most twenty loadouts; a full store still permits overwrite, rename, and delete.

Every entry must have a supported area, a non-negative slot, non-empty domain,
section, and name, valid colors, and finite appearance values within the same
ranges accepted by the existing appearance contract. A loadout must not contain
duplicate `(area, slot)` entries. An empty `entries` array is valid and means
remove every mutable SlaveTats-managed tattoo during exact restore.

## Ordering and Persistence

Add a dedicated `TattooLoadoutStore` backed by the shared `PluginConfigFile`.
The versioned payload is:

```json
{
  "tattooLoadouts": {
    "version": 1,
    "entries": [
      {
        "name": "Crimson Set",
        "tattoos": [
          {
            "area": "Body",
            "slot": 3,
            "domain": "default",
            "section": "Marks",
            "name": "Crimson Sigil",
            "color": 16777215,
            "alpha": 1.0,
            "glow": 16711680,
            "emissiveMult": 2.0,
            "glossiness": 250.0,
            "specularStrength": 10.0
          }
        ]
      }
    ]
  }
}
```

The outer `entries` array is the source of truth for Dropdown order:

- create appends;
- overwrite and rename retain the existing index;
- delete preserves the relative order of remaining entries; and
- load never sorts or rewrites the file.

Users may reorder loadouts by editing the JSON array. Tattoo entries themselves
are serialized deterministically by area order and ascending slot index so
manual review and diffs remain stable; their serialized order does not control
execution order.

A missing file or member means an empty list and does not create a file during
load. Malformed entries, duplicates, unsupported versions, invalid appearance
values, or more than twenty loadouts fail the complete operation without
discarding data. Supported mutations preserve unknown root members and unknown
members inside the supported `tattooLoadouts` object.

## Architecture

```text
OfficialMenuFrameworkAdapter
  presents loadout intent, preview, confirmation, and result
              |
              v
NativeSlotWorkflowModel
  owns deterministic UI state and generation-scoped tickets
              |
              v
NativeSlotWorkflowRuntime / scheduler
  owns dispatch, single-in-flight protection, and completions
              |
              +--> TattooLoadoutStore --> PluginConfigFile
              |
              v
SlaveTatsService
  validates typed capture/preflight/restore requests
              |
              v
ITattooRuntime --> SlaveTatsRuntime
  owns fresh snapshots, mutation, synchronization, and rollback
```

The adapter expresses intent and presentation only. It must not parse JSON,
resolve Actors, call JContainers or SlaveTatsNG, build mutation plans, or run a
series of single-slot commands.

The workflow model owns selected loadout, naming and management state, preview
state, confirmations, errors, and target-generation checks. The runtime
coordinator owns asynchronous scheduling and allows only one Actor mutation at
a time. Store-only requests use request IDs independent of Actor generation;
capture, Preflight, and restore requests are scoped to the current Actor Target
and Target Generation.

`SlaveTatsService` and `ITattooRuntime` gain a typed batch boundary rather than
having the native workflow loop over the existing Apply/Remove APIs. This keeps
multi-slot validation, one-time synchronization, and rollback below the UI
boundary. Existing single-slot behavior remains unchanged.

## Capture Workflow

1. The user chooses `Save Current` for an explicit Actor Target.
2. The workflow requests fresh snapshots for Body, Face, Hands, and Feet; it
   must not persist cached slots without runtime revalidation.
3. The capture operation copies only SlaveTats-managed tattoos and supported
   appearance fields from those snapshots.
4. External and empty slots are omitted. Runtime handles, Actor identity,
   lock state, and texture paths are omitted.
5. The UI displays per-area tattoo counts and requests a name.
6. A unique name creates a new entry. A folded-name collision requires
   explicit overwrite confirmation.
7. The committed list changes only after successful store completion.

Capture rejects an unresolved or unloaded Actor, mismatched actor snapshots,
query failure in any area, invalid snapshot data, or target-generation change.
It does not save a partial subset of areas.

## Preflight and Preview

Selecting `Load` performs a read-only Preflight against fresh state. Preflight
must:

1. resolve exactly the requested Actor form ID and require loaded 3D;
2. query all four area snapshots for that Actor;
3. resolve every stored domain/section/name through the authoritative catalog
   and SlaveTats runtime boundary;
4. validate configured slot counts and exact target indices;
5. reject a target slot occupied by an external overlay;
6. reject any locked managed tattoo that would be removed, replaced, or have
   appearance changed;
7. calculate additions, replacements, removals, appearance updates, unchanged
   managed tattoos, and preserved external overlays; and
8. bind the resulting plan to the Actor form ID, Target Generation, and fresh
   state required to reject stale execution.

An unchanged locked tattoo does not block the plan because it requires no
mutation. An unrelated external overlay is preserved and does not block the
plan. Missing tattoos, insufficient configured slots, conflicting external
slots, or conflicting locked tattoos reject the complete Preflight. No partial
or degraded plan is offered.

The UI groups the preview by Body, Face, Hands, and Feet and summarizes `Add`,
`Replace`, `Remove`, `Appearance`, `Unchanged`, and `Preserved`. A rejected plan
shows each blocking area/slot and reason, disables `Apply Loadout`, and leaves
`Back` and `Cancel` available.

## Exact Restore Execution

Execution must revalidate the Actor and the assumptions that make the approved
plan safe. If state changed since Preflight, execution fails before mutation
and requires a new Preflight.

Immediately before mutation, the runtime captures a Rollback Snapshot of all
managed state it may change. Unlike persistent loadout entries, this ephemeral
snapshot may preserve pack/runtime texture metadata and other fields needed to
recover the original state. It must remain actor-scoped and must not retain raw
Actor pointers beyond the scheduled operation.

The runtime then:

1. applies the deterministic mutation plan without synchronizing each slot;
2. writes and verifies supported appearance values;
3. synchronizes the Actor once after all mutations succeed;
4. queries all four areas; and
5. verifies the managed result against the persistent Loadout contract while
   confirming external overlays remain preserved.

The implementation may choose the safest deterministic internal order, but
that order must be covered by tests and must not be derived from user-edited
JSON ordering. Once mutation starts, the UI does not offer cancellation.

Loading a loadout does not change Favorites and does not record restored
tattoos in Recently Used. Successful execution keeps the user's prior Selected
Area and refreshes all four cached area snapshots.

## Failure and Rollback Semantics

The result contract distinguishes:

- **Success:** exact restore and final verification completed.
- **Preflight rejected:** no mutation occurred.
- **Failed, rollback complete:** execution failed after mutation began, the
  original managed state was restored and verified.
- **Failed, rollback incomplete:** rollback or its verification failed; the
  result identifies every area/slot that may require manual inspection.

When execution fails after a mutation may have occurred, the runtime constructs
a compensating plan from the Rollback Snapshot, applies it, synchronizes once,
and verifies the recovered state. Rollback is best-effort because the game,
Actor, SlaveTatsNG, or JContainers may become unavailable during recovery.
Never report rollback complete without readback verification.

If forward mutation completed but only synchronization failed, retry is
synchronization-only and must not repeat the completed mutation. The same rule
applies to rollback synchronization: retry synchronization and verification,
not the compensating writes that already succeeded. Retry state remains bound
to the original Actor form ID and must never fall back to the Player.

A failed or incomplete rollback blocks further loadout mutation until the user
acknowledges the result and fresh snapshots have been loaded. The UI must not
describe a partially changed Actor as successfully loaded.

## UI/UX

Add a `Loadouts` section to Current Slots containing:

- a Dropdown in JSON order;
- `Load`;
- `Save Current`; and
- `Manage` for rename and delete.

When no loadouts exist, show `No loadouts saved.` Reaching twenty entries
disables only creation under a new name. Load, overwrite, rename, and delete
remain available. Store requests and Actor operations expose distinct pending
and error states.

The capture summary and load preview use dedicated workflow screens or modal
content without moving the stable footer. Navigation follows the established
placement contract:

- `Back` remains on the left;
- contextual actions are ordered through the middle; and
- `Cancel` or `Close` remains at the bottom-right.

Content may scroll, but the footer remains anchored. During restore, mutation
controls, target selection, and other tattoo operations are disabled. After
mutation begins, show progress without a Cancel action.

## Error and Retry Presentation

Store errors use a `Loadouts: <message>` prefix and retry only the failed store
request. A store retry never captures, applies, removes, or synchronizes a
tattoo.

Preflight errors retain the selected Loadout and Actor so the user can correct
the environment and run Preflight again. Execution errors present the typed
rollback outcome and affected slots. Synchronization-only retry is offered only
when the typed result proves all required writes for that phase completed.

Changing Actor Target invalidates capture summaries, Preflight plans,
confirmations, retry state, and actor-scoped errors. Stale completions cannot
update the new target, start a mutation, or replace its slot snapshots.

## Automated Verification

### Store

- Missing file/member loads an empty list without creating a file.
- All fields and outer JSON order round-trip exactly.
- Tattoo entries serialize deterministically by area and slot.
- Create appends; overwrite/rename retain index; delete preserves order.
- Empty names, folded duplicates, duplicate area/slot pairs, invalid fields,
  malformed entries, unsupported versions, and over-limit data fail without
  overwrite.
- Interleaved Hotkey, Favorites, Recently Used, Appearance Presets, and Loadout
  mutations preserve supported and unknown data.

### Core and Planning

- Requests reject zero Actor IDs, unsupported areas, negative/out-of-range
  slots, missing identities, duplicate positions, and invalid appearance.
- Exact plans cover add, replace, remove, appearance-only, unchanged, empty
  loadout, and all four areas.
- Missing tattoos, insufficient capacity, external conflicts, and locked
  conflicts reject before mutation.
- Unchanged locked slots and unrelated external slots are preserved.

### Runtime

- Capture queries all four areas and never returns a partial capture.
- Execution revalidates the explicit loaded Actor and stale plan assumptions.
- Successful multi-slot restore synchronizes once and verifies all four areas.
- Runtime handles are never persisted or reused across Actors.
- Forward failure triggers rollback from the actor-scoped snapshot.
- Complete and incomplete rollback results require readback evidence.
- Forward and rollback synchronization-only retries never repeat completed
  writes.
- No failure path silently falls back to Player.

### Workflow and Adapter

- Store request IDs and Actor Target Generations reject stale completions.
- Only one mutation is active; target and tattoo controls are disabled.
- Capture summary, Preflight preview, blocking reasons, progress, success, and
  both rollback outcomes render deterministically.
- A successful restore refreshes all four caches and preserves Selected Area.
- Loading does not change Favorites or Recently Used.
- Footer placement remains stable across empty, scrollable, pending, blocked,
  and result states.

### Regression

- Existing Apply, Replace, Remove, Lock, Edit Appearance, live preview,
  Favorites, Recently Used, Appearance Presets, and material filters pass.
- External overlays remain read-only.
- Debug and Release builds and their full CTest suites pass.
- `git diff --check` passes and the scoped diff contains no unrelated changes.

## Manual Acceptance

1. Save a mixed four-area Player loadout, restart the game, and confirm JSON
   order and values persist.
2. Load it onto the Player after adding, replacing, removing, and editing
   managed tattoos; confirm exact area/slot and appearance restoration.
3. Load the same global loadout onto a valid Crosshair Target and confirm the
   Player remains unchanged.
4. Load an empty loadout and confirm mutable managed tattoos are removed while
   external overlays remain.
5. Confirm a missing pack, target-slot external overlay, conflicting locked
   tattoo, insufficient slot count, unloaded Actor, or stale target rejects the
   whole operation before mutation.
6. Confirm an unchanged locked tattoo does not block a load.
7. Exercise a controlled mid-operation failure and verify complete rollback;
   separately verify incomplete rollback is reported without claiming success.
8. Exercise synchronization failure and confirm retry does not repeat writes.
9. Confirm Loadout use does not reorder Favorites, flood Recently Used, or
   change Appearance Presets.
10. Create twenty loadouts, reject a twenty-first unique name, and confirm
    overwrite, rename, delete, restart persistence, and manual JSON reordering.

## Implementation Boundaries

Expected implementation areas are:

- a focused `TattooLoadoutStore` and store tests;
- typed Core batch contracts and `SlaveTatsService` validation;
- an `ITattooRuntime` batch boundary and `SlaveTatsRuntime` implementation;
- isolated planning/rollback orchestration with narrow deterministic tests;
- `NativeSlotWorkflowModel` and `NativeSlotWorkflowRuntime` state/tickets;
- `OfficialMenuFrameworkAdapter` presentation helpers and UI; and
- application composition and existing config compatibility tests.

Do not add JContainers or SlaveTatsNG calls to the adapter or workflow model.
Do not implement batch behavior by driving existing UI callbacks in a loop.
Do not broaden this slice into arbitrary texture selection, NPC discovery,
external overlay mutation, or unrelated footer redesign.

## Completion Rule

Do not mark Saved Tattoo Sets / Loadouts complete in `ROADMAP.md` until the
implementation, full Debug and Release verification, deployment, and intended
in-game acceptance pass. Keep controlled alternate glow/bump texture selection
as a separate unchecked roadmap item.

## Acceptance Record

Accepted in-game on 2026-10-10 after deploying the Release DLL to the dedicated
MO2 SlaveTatsUI mod. The user confirmed the Saved Tattoo Loadouts workflow works
in the game. The deployed DLL matched the verified Release artifact at SHA-256
`BECDB613B11C6531E2C3EC19EA519291C027AC928D797652573017C42B7F8FE6`;
Debug and Release CTest suites each passed 38/38 before acceptance.
