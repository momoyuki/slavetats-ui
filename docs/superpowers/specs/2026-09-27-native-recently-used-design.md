# Native Recently Used Design

**Status:** Implementation complete; automated verification passed; in-game acceptance pending.

**Milestone:** vNext.4 — Workflow Quality of Life.

**Implementation plan:** Not written. This spec must be approved before implementation planning begins.

## Purpose and Agreed Scope

Let users quickly return to tattoos they successfully applied or replaced without searching the full catalog again. Recently Used is a persistent personal history shared across sequential MO2 profile launches that resolve the same configuration file. It is independent of Actor Target and savegame, but entries are separated by overlay area so history never offers a Body tattoo as a Face, Hands, or Feet result.

Included:

- Record a tattoo only after Apply or Replace and its required synchronization complete successfully.
- Keep the six most recently used identities per Body, Face, Hands, and Feet area.
- Move a repeated identity to the newest position without creating a duplicate.
- Persist history in the existing `SlaveTatsUI.json` through the shared configuration transaction boundary.
- Add a session-local `Recently used only` filter that composes with all existing catalog filters.
- Preserve unavailable-pack entries until normal per-area eviction removes them.
- Expose non-blocking load/save errors and retry without repeating tattoo mutation.

Excluded:

- Recording Preview, Cancel, Favorite, failed mutation, failed synchronization, Remove, Edit Appearance, Lock, or Unlock actions.
- Timestamps, usage counts, badges, manual ordering, a management screen, or Clear History.
- Per-Actor, per-save, or per-profile history.
- Import/export, migration by display name, or simultaneous game-process synchronization.
- Changes to SlaveTatsNG ownership, slot allocation, mutation, or synchronization behavior.

## Identity and Area

A `RecentTattooIdentity` is the tuple `(domain, sourceId, section, name, area)`.

- The first four fields follow the existing `FavoriteIdentity` rules. `sourceId` is the normalized logical catalog source identifier; domain, section, and name retain exact catalog strings.
- `area` is the canonical Selected Area copied into the confirmed Apply or Replace request. It is never inferred later from mutable UI state or an Actor snapshot.
- Supported serialized area values are exactly `Body`, `Face`, `Hands`, and `Feet`.
- Physical MO2 paths, texture paths, Actor form IDs, slot numbers, handles, target generations, and catalog indices are not history identity.
- Equal five-field identities are duplicates. Reusing one removes its prior occurrence and places it at the front.
- The same four-field tattoo identity used in different areas produces distinct history entries. This protects area eligibility even if pack metadata contains duplicate definitions.
- A renamed domain, source, section, or name is a new identity. No heuristic migration is attempted.
- A catalog definition whose area changes no longer matches the old area-scoped history entry. Applying it successfully in the new area creates the new identity; the old identity remains unavailable until evicted.

This identity does not change the section/name `TattooIdentity` used by SlaveTats runtime resolution and Applied-only matching, and it does not change `FavoriteIdentity` membership semantics.

## Retention and Ordering

The store keeps at most six entries per supported area in one ordered history.

When recording a successful use:

1. Read and validate the latest on-disk history inside the shared configuration transaction.
2. Remove an equal five-field identity if present.
3. Insert the identity at the front of the ordered list.
4. Remove entries beyond the sixth item for that identity's area only.
5. Preserve the relative order and membership of every other area.

The serialized array represents newest-to-oldest order. The UI derives an area's sequence by retaining that area's entries in serialized order. No timestamp is required and wall-clock changes cannot reorder history.

Unavailable catalog identities count toward the six-entry area limit and are not pruned during catalog refresh. A later successful use in that area can evict the oldest stored identity whether or not it is currently installed. Re-enabling a pack restores visibility at its retained position when the exact five-field identity matches.

## Recording Boundary

Recently Used records user-visible completed use, not intent.

- A normal Apply or Replace records only after storage mutation and required visual synchronization succeed.
- If storage mutation succeeds but synchronization fails, do not record yet. Extend the typed Apply contract with a synchronization-only retry path, matching the safety already used by Remove and Appearance, and do not repeat the completed mutation.
- When synchronization-only retry succeeds, enqueue exactly one history update for the copied identity and area from the original request.
- Preview, Cancel, rejected validation, stale handle, foreign Actor, scheduler rejection, mutation failure, and synchronization failure without successful retry do not record.
- A history-save failure does not convert a successful Apply or Replace into a tattoo-operation failure.
- Retrying history persistence writes only the retained history identity. It never calls Apply, Replace, SlaveTatsNG, JContainers, or synchronization again.

The workflow must retain enough copied completion context to distinguish a successful Apply/Replace from unrelated refreshes. It must not rediscover the catalog row by mutable index after asynchronous work completes.

## Persistence Format and Validation

Recently Used is stored as a separate versioned object in the existing shared configuration document:

```json
{
  "hotkey": 67,
  "favorites": {
    "version": 1,
    "entries": []
  },
  "recentlyUsed": {
    "version": 1,
    "entries": [
      {
        "domain": "default",
        "sourceId": "textures/actors/character/slavetats/marks.json",
        "section": "Marks",
        "name": "Rose",
        "area": "Body"
      }
    ]
  }
}
```

- Missing file or missing `recentlyUsed` means an empty history. Loading alone does not create the file.
- The root must remain a JSON object. `recentlyUsed` must contain supported integer version `1` and an entries array.
- Every entry requires five non-empty string fields and a supported canonical area.
- Malformed JSON, invalid entries, and unsupported Recently Used versions fail the complete history load/update. Do not discard invalid entries and then overwrite recoverable data.
- Load canonicalizes exact duplicate identities by keeping the first, newest occurrence. For compatibility, version 1 histories containing seven through ten unique entries in an area load only the six newest entries without rewriting the file; the next successful record persists canonical six-entry output. More than ten unique entries in an area remain invalid and must not be overwritten.
- Preserve Unicode exactly and preserve unrelated root keys, Favorites, Hotkey, and unknown members of the supported `recentlyUsed` object.
- Unknown members inside version-1 entries have no defined meaning and are not guaranteed to survive canonical serialization.
- A supported root with an unsupported Recently Used version must still permit Hotkey and Favorites edits that preserve the unsupported payload unchanged. Recently Used operations themselves fail until the schema is supported.

All production stores receive the same `std::shared_ptr<PluginConfigFile>`. Recently Used must use its checked read/modify/write, temporary-file, and replacement behavior rather than introducing an independent writer.

## Architecture and Ownership

```text
Apply/Replace completion -> workflow completion context
                         -> runtime scheduler -> RecentTattooStore
                                              -> PluginConfigFile
                                              -> SlaveTatsUI.json

RecentTattooStore result -> completion mailbox -> presentation-thread pump
                                              -> browser history/query
                                              -> Picker Filters
```

- `RecentTattooIdentity` is a copied repository value with no environment access.
- `RecentTattooStore` validates the schema, loads the ordered history, and performs record/retry transactions. It has no Actor, SlaveTatsNG, JContainers, catalog, or UI dependency.
- `PluginConfigFile` remains the sole serialized transaction boundary shared by Hotkey, Favorites, and Recently Used writers.
- `NativeCatalogBrowserModel` owns a copied history and the session-local filter toggle. It performs no filesystem work.
- `NativeSlotWorkflowModel` owns deterministic pending/error/retry state and the copied successful-use intent. History errors remain separate from tattoo-operation and Favorites errors.
- `NativeSlotWorkflowRuntime` schedules storage calls and moves copied completions through a mutex-protected mailbox. Scheduled callbacks must not mutate browser vectors.
- The adapter renders filter state, errors, and Retry. It never records history directly and never performs filesystem or runtime mutation work.

The feature reuses existing architecture boundaries. It must not add direct JContainers or SlaveTatsNG calls outside `SlaveTatsRuntime`, nor bypass the runtime scheduler.

## Asynchronous State and Recovery

History loads during initialization before `Recently used only` can report a valid empty result. A failed load produces an unavailable state and an explicit scheduled Retry; it is not treated as an empty history.

History persistence has its own monotonically increasing request IDs, independent of Actor Target Generation and Favorite request IDs. Permit one outstanding history write at a time. If Apply/Replace completions occur while a history write is pending, queue their copied identities in successful-completion order and persist them serially so accepted uses are not lost. Coalesce adjacent duplicate identities to the newest single record without reordering different identities.

A scheduled completion enters a mutex-protected mailbox. The presentation-thread pump consumes it before the adapter takes page/card references or collects thumbnail paths. Scheduling rejection and storage exceptions become recoverable history errors and release in-flight state.

On write failure:

- Keep the last committed browser history and current catalog view.
- Retain the failed identity and any later queued successful identities.
- Expose Retry for the failed history write; do not automatically loop.
- Retry the failed identity first with a fresh request ID, then continue queued identities after it commits.
- Never repeat tattoo mutation or synchronization.

Menu close, session reset, Actor/Area changes, and navigation must not discard committed or queued global history. Processing a history completion must not restore an obsolete screen, target, slot, preview, or Actor. Duplicate or out-of-order completions are ignored by request ID. Runtime/store lifetime must cover outstanding scheduled work.

## Filtering, Ordering, and Pagination

`Recently used only` appears in the existing Filters overlay alongside `Applied only` and `Favorites only`. The overlay must not move the thumbnail grid. The toggle defaults off at process start and survives ordinary Picker/Preview navigation; it is not persisted.

When Recently Used is inactive, existing deterministic catalog ordering is unchanged. When active:

- Restrict candidates to installed catalog definitions whose exact five-field recent identity belongs to the Selected Area history.
- Order surviving candidates by that area's newest-to-oldest history order.
- Intersect search, domain, source, section, Selected Area, Applied-only, and Favorites-only before pagination without replacing recent ordering.
- Reset the requested page to the first page when the toggle or Selected Area changes.
- Preserve and clamp the requested page when a committed history update changes results.
- Derive contextual facet choices from eligible entries using the existing reconciliation hierarchy.
- Retain selected facet labels accurately when another active restriction makes their options temporarily absent.
- Request thumbnails only for the final visible page after history completions and filter changes are consumed.

An inactive recent restriction permits all catalog entries. An active but empty history permits none. Missing-pack identities remain stored but cannot produce catalog rows.

Empty-state precedence is:

| Condition | Presentation |
| --- | --- |
| Recently Used cannot be loaded and its filter is active | Recently Used unavailable with Retry |
| Catalog snapshot missing or catalog has no definitions | Existing empty-catalog message |
| Favorites, Applied, and Recently Used active with no matches | `No favorite applied recently used tattoos match the current filters.` |
| Favorites and Recently Used active with no matches | `No favorite recently used tattoos match the current filters.` |
| Applied and Recently Used active with no matches | `No applied recently used tattoos match the current filters.` |
| Recently Used active with no matches | `No recently used tattoos match the current filters.` |
| Existing Favorites/Applied combinations active | Existing messages |
| Other filters yield no matches | Existing generic no-match message |

When history is unavailable but its filter is off, ordinary browsing, Favorites, and Applied-only remain usable. Show the history storage error separately without presenting unrelated catalog data as history.

## Shared Configuration Scope

Recently Used uses the same resolved absolute `SlaveTatsUI.json` path as Hotkey and Favorites. Reject an empty or relative path rather than writing to the process working directory. Log the resolved path for acceptance verification.

Sharing across sequential MO2 profile launches is required only when both launches resolve the same physical non-profile-specific file. If profiles resolve different paths, report the environment conflict before migration; do not copy, merge, or relocate user configuration automatically.

The feature does not promise sharing across Windows accounts, game installations, or simultaneous game processes.

## Validation and Acceptance

Automated coverage must include:

- Missing/legacy config, real save/reopen, malformed JSON, invalid identities/areas, unsupported versions, duplicate entries, overflow rejection, Unicode, and checked write/replacement failure.
- Interleaved Hotkey, Favorites, and Recently Used transactions preserving every supported setting and unrelated root keys.
- Newest-first ordering, duplicate promotion, exactly six entries per area, legacy ten-entry compatibility, independent area eviction, and unavailable-pack retention/restoration.
- Exact distinctions for source, domain, section, name, and area, including same-named catalog entries.
- Successful Apply and Replace recording; Preview, Cancel, stale/rejected/failed operations not recording.
- Synchronization failure followed by synchronization-only retry recording once without repeating mutation.
- Pending serialization, queued successful uses, adjacent duplicate coalescing, scheduling rejection, exceptions, retry ordering, stale/duplicate completion, menu reset, and Actor/Area changes.
- Recent filtering before pagination, recent ordering through intersections, all empty-state combinations, page clamping, contextual facets, catalog replacement, and current-page thumbnail collection.
- Full relevant Debug and Release builds/test suites, `git diff --check`, and scoped diff review.

In-game acceptance requires:

- Apply and Replace add the expected entry only after visible success.
- Reusing an entry moves it to the newest position without duplication.
- Body, Face, Hands, and Feet histories remain separate and stop at six entries each.
- History survives restart and matches across two sequential MO2 profiles that log the same absolute configuration path.
- Recently Used composes correctly with Favorites, Applied-only, search, domain, source, and section.
- A recoverable history persistence failure leaves Apply successful, preserves committed history, and retries without repeating tattoo mutation.
- Filters remain an overlay and thumbnail/card/footer layout does not regress.

## Documentation and Roadmap

Do not mark Recently Used complete in `ROADMAP.md` until implementation, automated validation, deployment, and intended in-game acceptance pass. Update `CONTEXT.md` with the canonical Recently Used terminology when implementation begins. Keep unresolved Favorites acceptance checks separate; this feature must not silently declare them complete.

## Document Relationship

This spec defines intended behavior and architectural boundaries. A later approved implementation plan will define exact files, interfaces, task order, and commands. Writing this spec does not authorize implementation, commit, deployment, migration, or roadmap completion.
