# Nearby Actor Targeting Design

## Intent

Replace the fragile live Crosshair Target lookup with a one-time nearby Actor
snapshot created when the Tattoo Browser opens. The user chooses the Actor from
that snapshot, then the workflow keeps the selected identity stable for the
rest of the menu session.

## Goals

- Keep Player as an explicit target.
- Discover loaded Actors around the Player once per menu session.
- Sort candidates from nearest to farthest and expose at most 10 entries.
- Let the user choose a candidate by copied display name, form ID, and distance.
- Query and mutate only the selected Actor after selection.
- Avoid per-frame Crosshair reads and repeated Actor scans while the menu is open.
- Preserve actor-scoped stale-handle, external-slot, and synchronization safety.

## Discovery contract

SlaveTatsNG's public interface accepts an explicit `RE::Actor*` for tattoo
operations but does not expose an Actor discovery function. The native boundary
therefore uses CommonLibSSE-NG `RE::ProcessLists::ForAllActors` to copy loaded
Actor values; it does not duplicate SlaveTatsNG ownership or storage logic.

The production scan uses these fixed policies:

- center: the Player's current position at scan time;
- radius: 2048 Skyrim world units;
- candidates: non-Player Actors with nonzero form IDs and loaded 3D;
- ordering: ascending Euclidean distance, then ascending form ID for ties;
- limit: first 10 unique form IDs after ordering;
- output: value objects only, never retained raw Actor pointers.

The scan runs through the existing game-thread scheduler after the menu open
callback requests a new session. A scan failure leaves Player usable and shows a
retryable discovery status; it never changes the active target to another Actor.

## User behavior

**2026-10-16 presentation update:** The Nearby Actor combo and its retry button
are hidden in the browser header. The two visible buttons are Player and
Choose NPC, with one Target identity line. Followers are included in the nearby
NPC list. NPCs are selected
through the searchable loaded-Actor picker. The snapshot/runtime contract below
is retained for compatibility; the original selector behavior is historical.

The target header presents `Player` and a `Nearby Actors` selector. While the
snapshot is pending it shows `Scanning nearby Actors...`. Once ready, the list
contains the copied Actor name, form ID, and rounded distance. Selecting an
entry clears actor-scoped slot state, increments the target generation, and
queries only that Actor. The selected Actor remains fixed even if the Player
moves or the old Actor leaves the crosshair.
The selector remains a single-line combo. Its open popup shows a short,
scrollable list rather than extending down the main browser panel.

Closing and reopening the browser creates a new snapshot. There is no automatic
retargeting and no Crosshair Target mode. If a selected Actor unloads before a
query or mutation, the existing actor error path remains visible and no Player
fallback occurs. If discovery fails while the Player target is active, the
header exposes `Retry Nearby Scan`; it schedules one new snapshot attempt and
does not poll or retarget automatically.

## Architecture

```text
NativeMenu open
    |
    v
NativeSlotWorkflowModel -- scan ticket --> NativeSlotWorkflowRuntime
    |                                             |
    |                                             v
    |                                NearbyActorScanOperation
    |                                             |
    v                                             v
target selector <--- copied NearbyActorCandidate list --- ProcessLists
```

The adapter only renders the candidate list and emits a selection intent. The
model owns the snapshot, selection, target generation, and cache invalidation.
The runtime schedules discovery and delivers one generation-tagged completion.

## Non-goals

- no continuous crosshair polling;
- no actor loading or follower heuristics;
- no changes to SlaveTatsNG tattoo storage or mutation APIs;
- no mutation of external overlays;
- no persistence of nearby candidates across menu sessions;
- no camera, line-of-sight, or hover-to-apply behavior.

## Testing

- normalize candidates by distance, form-ID tie-break, duplicate removal, and
  ten-entry cap;
- reject zero form IDs, Player, unloaded 3D, and out-of-radius candidates at
  the provider boundary;
- request one scan on session reset and no scan from render callbacks;
- accept only the active scan generation and ignore stale completions;
- selecting a candidate carries its exact form ID into slot queries and
  mutations;
- target changes clear actor-scoped state and never fall back to Player;
- a failed discovery exposes one explicit retry and does not queue repeated
  scans while the menu renders;
- adapter presents the pending, empty, and populated selector states.

## Acceptance

The user opens the browser, sees at most ten nearby Actors ordered nearest-first,
selects one, and can use the existing slot-first workflow against that Actor.
Player behavior remains unchanged, the same Actor is retained while the menu is
open, and reopening the browser refreshes the nearby list.

## Acceptance Record (2026-10-14, partial)

- The user confirmed that the deployed candidate DLL is usable in-game.
- Candidate SHA-256: `9BD76B9DBA951C35F295111DEBA6EBD1D9358468BE7502108214C6C93837AF45`.
- This confirms operational use of the candidate; the complete Nearby Actor
  ordering, unload failure, no-fallback, and log checklist remains tracked in
  the active implementation plan.
