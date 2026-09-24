# Native Favorites Design

**Status:** Approved. Implementation in progress; deployment and in-game acceptance remain deferred.

**Milestone:** vNext.4 — Workflow Quality of Life.

**Implementation plan:** `docs/superpowers/plans/2026-09-24-native-favorites.md` (execution record until the approved implementation commit moves it active).

## Purpose and Agreed Scope

Let users mark frequently selected catalog tattoos and find them quickly across game restarts. Favorites are personal catalog preferences shared across MO2 profiles using the same configuration location; they are independent of the selected Actor and savegame.

The user selected persistent Favorites, shared profile scope, a separate `FavoriteStore` using the existing `SlaveTatsUI.json`, stars on Picker cards, and a Favorites-only filter. This design records those choices and the storage and scheduling details necessary to implement them safely.

Included:

- Independent add/remove star actions on Picker cards.
- Persistent membership with no arbitrary count limit.
- Favorites-only combined with existing catalog filters and Applied-only.
- Preservation of unavailable-pack favorites for later restoration.
- Explicit pending, failure, and retry behavior.

Excluded:

- Favorite ordering, folders, import/export, or a management screen.
- Per-Actor, per-save, or per-profile favorites.
- Recently used tattoos, presets, loadouts, or footer redesign.
- Tattoo application, appearance, ownership, or synchronization changes.
- Simultaneous game processes or live synchronization with external editors.

## Identity

A `FavoriteIdentity` is the tuple `(domain, sourceId, section, name)`.

- `sourceId` uses the existing scanner's normalized logical catalog identifier, such as `textures/actors/character/slavetats/marks.json`.
- Domain, section, and name retain the catalog's exact strings. Favorite matching is exact after source normalization; case-insensitive search/filter labels do not redefine favorite membership.
- Physical MO2 paths, texture paths, Actor IDs, slot numbers, runtime handles, and catalog row indices are not favorite identifiers.
- Duplicate definitions with all four fields equal share membership. Different sources or domains are distinct even when names match. Area is a separate eligibility filter; exact duplicate identities in different areas share membership.
- A renamed source/domain/section/name becomes a new identity. No heuristic migration is attempted.

This identity does not replace the existing section/name `TattooIdentity` used for In Use and Applied-only. SlaveTats runtime resolution and existing apply validation retain their current contracts.

## User Interaction

Each Picker card contains an outline star for an unmarked tattoo and a filled star for a favorite. The star has its own bounded hit target in the metadata region, a unique UI ID, and an Add to favorites or Remove from favorites tooltip. Clicking it consumes that action without also selecting the card or entering Preview.

Use existing drawing primitives if the deployed font lacks a suitable star glyph. Preserve the six-card geometry, thumbnail sizing, and existing footer placement.

A click queues the explicit desired state (`enabled = true` or `false`), not an instruction to invert whatever state exists later. While saving, retain the previous star state, show Saving, and disable additional favorite writes. Ordinary browsing and navigation remain available. Only a successful save changes membership.

`Favorites only` appears alongside `Applied only` in Filters. It defaults off at process start and survives ordinary menu, Picker, and Preview navigation. Only favorite membership is persisted; the filter toggle is session-local.

## Filtering and Pagination

The repository intersects favorite membership with search, domain, source, section, Selected Area, and Applied-only before computing matched counts and pages. The existing deterministic catalog order remains unchanged.

- An inactive favorite restriction permits all catalog entries.
- An active empty favorite set permits none.
- The browser retains the complete membership set to display stars even with Favorites-only off.
- Changing the toggle resets the requested page to the first page.
- A committed membership update preserves the requested page, then clamps it if the result shrinks. Removing the last result on a later page selects the last remaining valid page; zero results produce page `0 / 0`.
- Removing a star does not silently clear search/domain/source/section. A selected facet absent from the narrowed options remains accurately labeled until the user changes context or clears it.
- Explicit area/domain/source changes use the existing contextual reconciliation hierarchy. Facet choices are derived from eligible favorite/applied entries and preceding filters.
- Catalog replacement retains favorite membership and the toggle while applying established refresh behavior to other browser filters. Missing catalog identities remain in storage.

Empty-state precedence is:

| Condition | Presentation |
| --- | --- |
| Favorites cannot be loaded and Favorites-only is active | Favorites unavailable, with Retry; do not report an empty favorite list |
| Catalog snapshot missing or catalog has no definitions | Existing empty-catalog message |
| Both filters active and no matches | `No favorite applied tattoos match the current filters.` |
| Favorites-only active and no matches | `No favorite tattoos match the current filters.` |
| Applied-only active and no matches | Existing Applied-only message |
| Other filters yield no matches | Existing generic no-match message |

When Favorites are unavailable and Favorites-only is off, ordinary catalog browsing remains available with disabled star actions and a separate storage error.

## Architecture and Ownership

```text
Picker adapter -> workflow intent -> runtime scheduler -> FavoriteStore
                                                        |
                                                        v
HotkeyBinding ------------------------------------> PluginConfigFile
                                                        |
                                                        v
                                                 SlaveTatsUI.json

FavoriteStore result -> completion mailbox -> presentation-thread pump
                                           -> browser membership/query -> Picker
```

- `FavoriteIdentity` is a copied repository value with no environment access.
- `FavoriteStore` validates the favorites schema, loads membership, and persists desired-state updates. It has no dependency on Actor state or the current installed catalog.
- `PluginConfigFile` owns serialized read/modify/write operations on the shared JSON document. All production writers of this file receive the same instance.
- `NativeCatalogBrowserModel` owns copied membership, filter state, and cached query results. It performs no filesystem work.
- `NativeSlotWorkflowModel` owns deterministic request/pending/error state and emits intent. Favorites errors are separate from tattoo-operation errors.
- `NativeSlotWorkflowRuntime` schedules storage operations, captures failures, and publishes copied results. It must not modify browser vectors from the scheduled storage callback.
- The adapter expresses intent and renders state. It does not open files or call SlaveTatsNG/JContainers.

## Shared Configuration Location

Current `src/main.cpp` derives the configuration directory from `setupLog().parent_path()`, not the DLL directory. The existing fallback can use `Data/SKSE/Plugins`, and an exception can leave the directory empty.

Both stores must receive the same resolved absolute `SlaveTatsUI.json` path. Reject an empty/relative path for Favorites rather than writing into the process working directory. Log the resolved path for acceptance verification.

Sharing across sequential MO2 profile launches is an acceptance requirement. Verify that those launches use the same physical, non-profile-specific configuration file. Sharing a DLL alone is not evidence of shared storage. If the existing path is redirected per profile, report the conflict before any data migration; do not silently create separate lists or move existing configuration.

The feature does not promise sharing across Windows accounts, different game installations, or simultaneous game processes. The concrete configuration path is an execution/environment check, not an assumed MO2 guarantee.

## Persistence Format and Validation

```json
{
  "hotkey": 67,
  "favorites": {
    "version": 1,
    "entries": [
      {
        "domain": "default",
        "sourceId": "textures/actors/character/slavetats/marks.json",
        "section": "Marks",
        "name": "Rose"
      }
    ]
  }
}
```

- Missing file or missing `favorites` key means an empty list; loading Favorites alone does not create the file.
- An existing root must be a JSON object. `favorites` must contain supported integer version `1` and an entries array. Each entry must contain four non-empty string identity fields.
- Malformed JSON, invalid identity fields, and unsupported versions fail the entire Favorites load/update. Do not silently discard entries and later overwrite recoverable data.
- Deduplicate identities and serialize in stable tuple order. Preserve Unicode names exactly.
- Preserve unrelated root keys, including `hotkey`, and unknown members of the supported favorites object. Version-1 identity entries are the four defined fields; unknown entry fields have no meaning and are not guaranteed to survive canonical serialization.
- Keep unavailable-pack identities indefinitely until the user removes their star after the definition becomes available. No automatic pruning.

## Transaction and Failure Semantics

Hotkey and Favorites cannot use independent read/modify/write sequences: either can overwrite the other's update. Route both through one `PluginConfigFile` transaction mutex.

For each save, under that mutex:

1. Read the current on-disk document and validate the root.
2. Apply only the requested setting edit. Favorites validates and updates its current on-disk entries rather than writing a cached whole document.
3. Serialize to a uniquely named sibling temporary file; check writes, flush, and close.
4. Replace/move into the destination using checked Windows operations without truncating the destination first.
5. Return success and the committed favorite list only after replacement succeeds.

On failure, preserve the destination and previous UI state, report the error, and clean up only the temporary file owned by that attempt. Do not claim power-loss durability beyond the guarantees of the chosen filesystem operations. Store-local locks, when required, are acquired before the shared transaction lock; avoid callbacks that reacquire store locks.

Existing Hotkey parsing and rollback remain compatible. Malformed root configuration must not be overwritten by either writer. A supported root with an unsupported Favorites version can still have its hotkey updated while preserving that Favorites payload unchanged.

## Asynchronous State and Recovery

Favorites load once before star actions become available. Loading failure produces an unavailable state and a scheduled Retry action; it does not silently publish an empty list as successfully loaded. A successful retry publishes membership and clears the error.

Favorite writes use their own increasing request IDs, independent of Actor Target Generation. Permit one outstanding favorite request at a time. Each request contains a copied identity and desired membership; reject duplicate input while pending.

A scheduled completion enters a mutex-protected mailbox. The presentation-thread runtime pump drains it before the adapter takes page/card references or collects visible thumbnail paths. Scheduling/storage exceptions become error completions and release pending state. A scheduler that cannot accept work must signal failure rather than silently dropping the request.

Menu close, session reset, and Actor/Area changes must not discard a committed global membership result. Processing that result updates Favorites only; it must not restore an obsolete screen, slot target, preview, or Actor. Ignore duplicate/out-of-order favorite completions by request ID. If the menu is closed when saving finishes, retain the mailbox result until the next pump. Runtime/store lifetime must cover outstanding scheduled work.

On save failure, expose Retry using the same desired state with a fresh request ID. On load failure, Retry reloads rather than attempting a write. Error messages remain separate from Actor/tattoo failures. Do not automatically retry in a loop.

## Validation and Acceptance

Automated coverage must include:

- Real temporary-file load/save/reopen; legacy/missing config; malformed and unsupported schemas; failed write/replacement; Hotkey/Favorites interleaving and unknown-key preservation.
- Exact source/domain distinctions, Unicode, duplicate identities, missing/reinstalled pack identity restoration, and idempotent desired-state requests.
- Combined filters before pagination, empty membership, full six-entry pages, page clamping, preserved selections, and catalog replacement.
- Pending/success/failure/retry, scheduling rejection, duplicate/stale completion, menu reset, and Actor change during a save.
- Star action isolation, accurate empty states, and current-page thumbnail collection after completion/filter changes.
- Full Debug and Release builds/test suites and scoped diff review.

In-game acceptance requires persistence after restart; identical resolved configuration paths and Favorites across two sequential profile launches; pack disable/re-enable; independent star/card clicks; visible pending/error/retry behavior; filtering and thumbnail consistency; and alternating Hotkey/Favorites updates surviving restart.

Deployment remains deferred. Automated success does not complete manual acceptance. Keep Favorites unchecked in the Roadmap until intended validation passes, and retain the previously deferred Applied-only acceptance as a separate open checklist item.

## Document Relationship

This spec defines intended behavior and architectural boundaries. The linked plan defines implementation order and tests. Implementation has been authorized; commit and deployment remain separate approval gates. If execution discovers a conflict between shared-profile scope and the resolved configuration path, surface it before changing the agreed persistence location.
