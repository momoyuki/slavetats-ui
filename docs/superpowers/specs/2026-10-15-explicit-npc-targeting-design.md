# Explicit NPC Targeting Design

## Context

The native workflow can currently target Player or choose one of the nearest
loaded Actors from a session snapshot. The remaining actor-targeting candidate
is an explicit NPC picker that lets a user deliberately choose a loaded Actor
outside the short nearby list without introducing raw form-ID input or unsafe
heuristics.

## Goals

- Add an explicit `Choose NPC` flow backed by one immutable loaded-Actor snapshot.
- Keep Player and the existing nearest-first nearby selector unchanged.
- Search and filter the copied snapshot without rescanning during rendering or typing.
- Preserve form-ID identity, target generations, stale-completion rejection, and
  no-Player-fallback behavior.
- Reuse the existing slot-first workflow, scheduler, service, and runtime paths.
- Keep external overlays read-only and all mutations actor-scoped.

## Non-goals

- No arbitrary Form ID text entry.
- No follower detection, faction heuristics, or automatic target selection.
- No target persistence across menu sessions.
- No mutation of unloaded Actors or Actors without loaded 3D.
- No live process-list polling while the picker is rendered.
- No change to SlaveTatsNG ownership or synchronization semantics.

## User Behavior

The target selector keeps `Player` and `Nearby Actors` as the fast paths and
adds `Choose NPC`. Choosing it performs one game-thread scan and opens a picker
over the copied snapshot. The picker provides a local name search and stable
ordering by normalized display name, then form ID. Search changes only filter
the captured values; they never trigger a new runtime scan.

Each row shows the display name, eight-digit uppercase form ID, and distance
from the Player at scan time. Empty names display as `Unnamed Actor`. Selecting
a row advances the target generation, clears actor-scoped workflow state, and
queries only the selected form ID. Closing the picker leaves the current target
unchanged. A refresh explicitly replaces the snapshot.

If an Actor unloads after selection, the existing runtime error path is shown
and no operation queries or mutates the Player. The user must choose another
Actor or explicitly select Player.

If the loaded-Actor scan fails, the picker shows the failure reason and a Retry
action while preserving the current target.

## Architecture

```text
OfficialMenuFrameworkAdapter
  presents picker/search intent only
              |
              v
NativeSlotWorkflowModel
  owns snapshot, filter, target generation, and selection state
              |
              v
NativeSlotWorkflowRuntime
  schedules one loaded-Actor scan and actor-scoped slot queries
              |
              +--> LoadedActorProvider
              |      copies loaded Actor identity values only
              |
              +--> SlaveTatsService --> ITattooRuntime --> SlaveTatsRuntime
```

The provider returns value objects only: form ID, display name, distance, and
the existing nearby/explicit target kind. It never exposes or stores an Actor
pointer beyond the game-thread enumeration callback.

## Snapshot Contract

- Scan only after `Choose NPC` or explicit refresh is requested.
- Include non-Player Actors with nonzero form IDs and loaded 3D.
- Deduplicate by form ID.
- Cap the snapshot at 64 entries after deterministic ordering.
- Order by case-insensitive display name, then form ID.
- Preserve the scan distance as presentation metadata only.
- A stale scan completion must not replace a newer snapshot or target.

The existing nearest-first list remains capped at ten and keeps its current
distance ordering. The explicit picker is a separate snapshot and does not
change nearby-list behavior.

## Safety and State Transitions

Opening or refreshing the picker advances a target-discovery generation and
blocks target changes while a scan is in flight. Selecting an NPC advances the
target generation again, clears all four area caches, pagination, preview,
appearance sessions, and actor-scoped errors, then requests slots for the
selected form ID. Every query, apply, replace, remove, appearance, lock, and
retry request carries that exact ID.

An obsolete scan, query, or mutation completion is ignored. A missing target,
foreign slot snapshot, stale handle, external slot, or unloaded Actor produces
no mutation ticket.

## Testing

### Provider

- filters Player, zero-ID, unloaded, and invalid candidates;
- deduplicates form IDs and caps at 64;
- orders case-insensitive names with form-ID tie-breaks;
- normalizes empty names without retaining raw Actor pointers.

### Workflow

- opening and refreshing scan exactly once per request;
- search filters only the captured snapshot;
- selecting an entry carries its exact form ID to slot queries;
- closing the picker preserves the existing target;
- stale scan/query/mutation completions cannot affect a newer target;
- failed or unloaded explicit targets never query Player.

### Adapter and regression

- picker labels and stable IDs remain deterministic;
- mutation controls are disabled while discovery or target transition is active;
- nearby selection, Player selection, external read-only slots, and sync-only
  retry remain unchanged;
- Debug and Release suites pass in full.

## In-Game Acceptance

1. Open the picker and confirm it scans once and lists loaded NPCs.
2. Filter by a partial name without triggering another scan.
3. Select an NPC outside the nearby ten and verify its name/form ID and slots.
4. Apply, replace, remove, edit appearance, Lock, and Unlock on that NPC.
5. Unload or leave the selected NPC unavailable and confirm no Player fallback.
6. Refresh the picker and confirm the old snapshot and stale completions cannot
   overwrite the new selection.
7. Return to Player and nearby selection and confirm existing behavior remains.
