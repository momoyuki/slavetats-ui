# Follower Target Convenience Design

## Context

PR #17 adds an explicit loaded-NPC picker backed by a copied snapshot. The
remaining vNext.3 candidate is a follower-oriented shortcut. The shortcut must
reuse the same actor-scoped workflow and must not infer followers from names,
factions, distance, or UI state.

## Goals

- Add a `Followers` entry point beside `Choose NPC`.
- Use CommonLibSSE-NG `RE::Actor::IsPlayerTeammate()` as the only follower
  predicate.
- Reuse one loaded-Actor snapshot, search, refresh, stale-generation, and
  no-Player-fallback behavior from the explicit picker.
- Keep the selected target represented by its exact form ID and existing
  `explicitNpc` target kind.
- Keep follower discovery limited to non-Player Actors with loaded 3D.

## Non-goals

- No follower loading or teleporting.
- No follower persistence across menu sessions.
- No faction, race, name, distance, or proximity heuristics.
- No changes to SlaveTatsNG ownership, external-slot policy, or mutation paths.
- No new target kind or runtime request shape.

## User behavior

**2026-10-16 unified-picker update:** The user requested merging Followers into
Choose NPC. The browser now exposes Player and Choose NPC only, listing ordinary
NPCs and Followers with loaded 3D within 2048 world units. Choose NPC toggles
the list without retargeting. The separate follower mode described below is a
retained internal contract and historical implementation record, not a visible
entry point in the current browser.

The target header presents three buttons: `Player`, `Choose NPC`, and
`Followers`. The redundant Nearby Actor combo is hidden. `Followers` opens
the same snapshot picker with a follower-only
view. Search filters copied values without rescanning. Refresh replaces the
snapshot and keeps the follower-only view. Closing leaves the current target
unchanged. Selecting a row uses the existing explicit-NPC target transition and
queries only that form ID.

If no loaded teammate exists, the picker shows an empty state and does not
change the active target. A follower that unloads after selection follows the
existing actor error path; the workflow never falls back to Player.

## Architecture

`LoadedActorCandidate` gains a copied `isPlayerTeammate` value. The production
provider fills it from `RE::Actor::IsPlayerTeammate()` while enumerating loaded
Actors. The workflow stores a picker mode (`all` or `followers`) and filters
the immutable snapshot locally. Adapter code only presents the mode and emits
selection intent.

## Testing

- provider normalization preserves the teammate flag while filtering and
  deduplicating identities;
- follower mode returns only teammate candidates and search never rescans;
- closing follower mode preserves the active target;
- selecting a follower carries the exact form ID into the existing slot query;
- stale scan completions remain ignored;
- adapter labels and existing Player/nearby/explicit-NPC behavior stay stable;
- full Debug and Release suites pass.

## Acceptance record

On 2026-10-14, the user confirmed the in-game follower-only list, Search,
Refresh, Close, exact follower selection, and switching back to Choose
NPC/Player. PR #18 merged into `main` on 2026-10-15.
