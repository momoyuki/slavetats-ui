# Controlled Material Texture Selection Design

## Context

SlaveTats UI currently preserves one `glowTexture` path and one `bump` path from
SlaveTatsNG tattoo metadata. Edit Appearance displays those paths as read-only
text. The remaining vNext.4 roadmap item is controlled selection of alternate
glow and bump textures without turning the editor into an arbitrary file or path
editor.

SlaveTatsNG copies tattoo maps into the applied tattoo record and remains the
authoritative renderer and synchronizer. SlaveTats UI may update the two string
fields on a revalidated applied tattoo, mark the actor updated, and request the
same synchronization used by other appearance changes.

## Goals

- Let a tattoo pack explicitly declare approved glow and bump texture choices.
- Show those choices in Edit Appearance for the matching applied tattoo.
- Preview, save, cancel, and retry texture changes through the existing live
  appearance workflow.
- Preserve exact paths and restore the original texture values on Cancel/Close.
- Keep old packs and tattoos with no option metadata unchanged.
- Preserve stale-handle, actor-target, external-slot, and synchronization-only
  retry guarantees.

## Non-Goals

- No free-form path entry or filesystem browser.
- No inference from neighboring DDS filenames.
- No recursive scan of loose files or BSA archives.
- No texture conversion, validation by decoding, or material preview outside the
  existing actor synchronization path.
- No change to SlaveTatsNG's apply/template lookup behavior.
- No persistent per-user texture overrides outside the applied tattoo record.
- No selection for external overlay slots.

## Pack Metadata Extension

SlaveTats UI recognizes two optional extension fields on a tattoo definition:

```json
{
  "glowTexture": "marks/base_g.dds",
  "bump": "marks/base_n.dds",
  "glowTextureOptions": [
    { "label": "Soft", "path": "marks/soft_g.dds" },
    { "label": "Sharp", "path": "marks/sharp_g.dds" }
  ],
  "bumpOptions": [
    { "label": "Smooth", "path": "marks/smooth_n.dds" },
    { "label": "Raised", "path": "marks/raised_n.dds" }
  ]
}
```

Each option is an object with exactly the fields needed by the UI:

- `label` is a non-empty display label. Leading and trailing whitespace is
  trimmed. ASCII control characters and ImGui's `##` identifier delimiter are
  rejected.
- `path` is a non-empty texture path preserved byte-for-byte after parsing. It
  must not contain a NUL character.
- Options retain declaration order.
- Paths must be unique within their own list using ASCII case-insensitive
  comparison, matching Windows texture lookup behavior.
- A malformed list or option rejects that tattoo entry through the existing
  indexed parser-issue behavior; valid sibling entries remain available.
- Unknown fields continue to be ignored for forward compatibility.

The existing singular `glowTexture` and `bump` fields remain the pack defaults.
When non-empty, the editor prepends the default as `Pack default` unless an
option already declares the same path. Packs do not need to repeat defaults in
the option arrays.

This is a SlaveTats UI metadata extension. SlaveTatsNG may ignore the arrays;
its existing shallow-copy behavior continues carrying the recognized singular
fields used for rendering.

## Catalog Resolution

Texture choices belong to the tattoo definition, not to the actor or slot. When
Edit Appearance opens, the workflow resolves the applied tattoo against the
current immutable catalog snapshot using exact `(domain, section, name, area)`
runtime identity.

- Exactly one match exposes that definition's effective options.
- No match exposes no selector and leaves the current paths read-only.
- Multiple matches are treated as ambiguous and expose no selector.
- A catalog refresh does not replace options captured by an open edit session.

The currently applied path is always preserved in the edit session. If it is not
in the resolved allowlist, the UI displays it as `Current (unlisted)` but does
not add it as a selectable pack option. Cancel/Close may still restore that exact
original path because restoration is part of the captured runtime snapshot.

## Workflow and Presentation

`TattooAppearance` owns `glowTexture` and `bump` alongside numeric appearance
values so equality, dirty state, live preview, Save, and rollback include the
complete editable appearance.

`AppearanceEditSession` captures immutable glow and bump option lists when it
opens. The adapter renders a combo only when its corresponding effective list is
non-empty. Each row shows the pack label; the exact path appears as supporting
text or a tooltip. The current read-only path remains visible in all cases.

Selecting an option updates the edited appearance. It uses the existing
one-second debounce and latest-wins behavior. Save commits the latest successful
preview without repeating it. Cancel and Close restore both strings and all
numeric fields to the exact original snapshot.

The selector has no implicit `None` entry. A pack that intends an empty texture
must continue using an empty singular default; this milestone does not add a
general-purpose clearing control.

## Runtime Mutation

`UpdateTattooAppearanceRequest` carries `glowTexture` and `bump`. In
`updateAndSynchronize` mode the runtime:

1. resolves the explicit actor;
2. confirms the session handle still belongs to that actor;
3. writes and reads back all numeric and string appearance fields;
4. marks the actor updated; and
5. synchronizes once.

String writes use the existing `setTattooString`/`getTattooString` bindings or
the JContainers fallback. Any failed readback returns `updateFailed` with
`mayHaveOccurred`, matching the existing partial-write model. A later retry of a
failed mutation may repeat the full desired write. A synchronization-only retry
writes neither strings nor numeric fields.

## Compatibility and Safety

- Missing option arrays produce the current read-only behavior.
- Existing singular fields retain their meaning and exact bytes.
- Old packs require no migration.
- Pack options never alter the available-tattoo template during Edit Appearance;
  only the revalidated applied tattoo handle is mutated.
- Actor identity is never inferred from the catalog or UI selection.
- External slots remain read-only.
- Switching Actor Target or refreshing slots invalidates stale completions under
  the existing target-generation rules.
- Appearance presets remain numeric and do not capture texture selections.
- Tattoo loadouts continue preserving the concrete applied glow/bump paths in
  rollback snapshots; they do not store the option lists.

## Error Handling

- Malformed metadata reports an indexed catalog parse issue.
- Missing or ambiguous catalog identity disables selection without blocking
  ordinary numeric editing.
- A stale/foreign handle performs no writes and requests a slot refresh.
- Missing string bindings or failed string readback reports `updateFailed`.
- A synchronization failure after verified writes exposes `Retry Sync` and does
  not repeat the mutation.
- Cancel/Close restoration failure remains visible and retryable through the
  existing restore state.

## Automated Testing

### Parser

- accepts ordered valid option objects and preserves exact paths;
- trims labels and rejects empty labels, controls, `##`, empty paths, NULs,
  duplicate paths, non-arrays, and malformed option objects;
- missing arrays leave legacy definitions unchanged;
- singular defaults are not required to appear in the arrays.

### Repository and Resolution

- exact runtime identity returns one definition and its options;
- missing or ambiguous identity returns no editable options;
- singular non-empty defaults are prepended and duplicate declared defaults are
  collapsed without reordering the remaining choices.

### Core and Runtime

- the service forwards exact string paths in update mode;
- the runtime writes and verifies both string fields after handle membership;
- any string readback failure reports a possible mutation;
- stale handles and synchronization-only retries perform zero string writes;
- synchronization failure after successful string writes retains sync-only retry
  semantics.

### Native Workflow

- an edit session captures options from a unique catalog match;
- missing/ambiguous matches retain read-only paths;
- only captured allowlist values can be selected through workflow intents;
- string selection participates in dirty state, debounce, latest-wins, Save,
  Cancel, Close, and stale-completion rejection;
- Cancel restores an original unlisted path exactly;
- appearance presets do not change either texture path.

### Adapter

- combos appear only when effective options exist;
- labels and exact paths render without format-string interpretation;
- an unlisted current path is identified without becoming selectable;
- selecting a row emits workflow intent and performs no direct runtime access.

## In-Game Acceptance

1. Confirm a legacy tattoo with no option arrays has unchanged Edit Appearance
   behavior.
2. Apply a fixture tattoo whose pack declares at least two glow and two bump
   choices.
3. Select each option and confirm the actor updates after the existing debounce.
4. Save, close, reopen, and confirm the concrete applied paths persist.
5. Change both paths and Cancel; confirm the exact original appearance returns.
6. Rapidly change options and confirm only the latest desired state remains.
7. Exercise a synchronization failure and confirm Retry Sync performs no second
   appearance write.
8. Switch from Player to Crosshair Target and confirm no selection or completion
   crosses actor boundaries.
9. Confirm external overlays expose no editor.
10. Replace/remove the test tattoo and confirm no stale glow or bump state remains
    in the reused slot.

## Documentation

Document the two extension fields for pack authors with one valid example and
their validation rules. Do not mark the roadmap item complete until Debug and
Release suites, scoped review, deployment, and the in-game acceptance checks
pass.

## Acceptance Record (2026-10-13, partial)

- The user confirmed the QA fixture exposes both Glow and Bump choices, and
  Save, reopen, and Cancel preserve or restore the intended appearance.
- The user confirmed Crosshair Target works, Replace and Remove leave no
  reported residual appearance, and external overlays are locked against
  editing.
- Debug and Release CTest suites passed 38/38 tests each at commit `36fea96`;
  release policy checks and `git diff --check` passed.
- In-game checks still pending explicit confirmation: legacy tattoos without
  option arrays, rapid selection/latest-wins, and a naturally occurring
  synchronization failure followed by Retry Sync. The latter's no-repeat-write
  behavior is covered by automated tests.
- The current game log was not available at its documented locations during
  this review, so log inspection remains pending.
