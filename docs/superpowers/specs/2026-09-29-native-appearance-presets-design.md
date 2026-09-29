# Native Appearance Presets Design

**Status:** Implemented; in-game acceptance pending.

**Roadmap milestone:** vNext.4 — Workflow Quality of Life

## Goal

Let users save named appearance values and preview them on any editable
SlaveTats-managed tattoo without coupling a preset to an Actor, area, slot, or
tattoo definition. Presets persist in the existing `SlaveTatsUI.json` document
and are shared by sequential MO2 profiles that resolve the same configuration
path.

## Scope

The MVP supports creating, loading, overwriting, renaming, and deleting up to
twenty named appearance presets. Loading a preset updates the current Edit
Appearance session and live preview only. The user must still choose `Save` to
commit the appearance update or `Cancel` to restore the original appearance.

The following editable values belong to a preset:

- diffuse color;
- visible alpha;
- glow color;
- emissive multiplier;
- glossiness; and
- specular strength.

The following do not belong to a preset:

- tattoo identity, domain, source, section, or name;
- Actor Target, Selected Area, or Current Slot;
- runtime tattoo handle;
- lock state;
- glow texture; or
- bump texture.

Loading a preset must preserve the current tattoo's glow and bump texture
metadata and every other excluded field.

## Non-goals

- Import or export.
- Move Up or Move Down controls.
- Automatic sorting.
- Preset thumbnails.
- Actor-, area-, slot-, or tattoo-specific presets.
- Arbitrary texture-path editing.
- Saved tattoo sets or loadouts.

## Domain Model

Introduce a transport-independent preset value:

```cpp
struct AppearancePreset {
    std::string name;
    std::uint32_t color;
    float alpha;
    std::uint32_t glow;
    float emissiveMult;
    float glossiness;
    float specularStrength;
};
```

Names are trimmed before validation. A trimmed name must not be empty. Name
membership is case-insensitive using the same deterministic ASCII folding rule
for load, create, overwrite, rename, and delete. The stored spelling is the
user's spelling.

The store permits at most twenty entries. A full store rejects creation of a
new name but still permits overwrite, rename, and delete.

All numeric values must be finite and valid under the same ranges accepted by
the Edit Appearance model and Core update contract. Colors use the current
appearance model's integer representation; the preset feature does not add a
second channel-order or conversion contract.

## Ordering Contract

The JSON `entries` array is the source of truth for presentation order. The
Dropdown displays entries in that exact order.

- Create appends to the end.
- Overwrite retains the existing index.
- Rename retains the existing index.
- Delete removes only the selected entry and preserves the relative order of
  every remaining entry.
- Load never sorts or rewrites the file.

This permits advanced users to reorder presets by editing the JSON array. The
MVP provides no in-game reordering controls. A manually edited order becomes
visible after the configuration is loaded again.

## Persistence

Add a dedicated `AppearancePresetStore` backed by the same shared
`PluginConfigFile` instance used by Hotkey, Favorites, and Recently Used. The
store must use the existing checked read/modify/write and replacement behavior
so independent writers preserve one another's state and unknown root members.

The versioned payload is:

```json
{
  "appearancePresets": {
    "version": 1,
    "entries": [
      {
        "name": "Warm Glow",
        "color": 16777215,
        "alpha": 1.0,
        "glow": 16737792,
        "emissiveMult": 2.0,
        "glossiness": 250.0,
        "specularStrength": 10.0
      }
    ]
  }
}
```

A missing file or missing `appearancePresets` member means an empty list.
Loading alone must not create or normalize the file. Malformed JSON, malformed
entries, duplicate folded names, invalid numeric values, more than twenty
entries, and unsupported versions fail the complete preset load or mutation.
The store must not discard invalid entries and overwrite the remaining data.

Unknown root keys and unknown members inside a supported
`appearancePresets` object must survive supported mutations. An unsupported
preset schema remains untouched by Hotkey, Favorites, and Recently Used
operations; preset operations fail until the schema is supported.

## Architecture

Preserve the existing layering:

```text
Native UI / adapter
        |
        v
NativeSlotWorkflowModel
        |
        v
NativeSlotWorkflowRuntime / scheduler / completion mailbox
        |
        v
AppearancePresetStore
        |
        v
PluginConfigFile
```

The adapter renders intent and presentation only. It does not parse or write
JSON. The workflow owns deterministic preset selection, confirmation, pending,
error, and retry state. The runtime owns asynchronous dispatch, in-flight
protection, exception conversion, and completion delivery. The store owns
schema validation and persistence semantics.

Preset request IDs are independent of Actor Target Generation. A submitted
preset persistence request remains valid if the user changes Actor, area, slot,
or screen because presets are global configuration. Only one preset mutation
may be active at a time. Stale or out-of-order completions must not replace the
committed preset list.

## Workflow

### Initialization

Production initialization creates `AppearancePresetStore` with the existing
shared `PluginConfigFile`, then requests the initial load through the normal
scheduler and completion mailbox. A failed load produces an unavailable state,
not a successful empty list.

### Load for Preview

1. The user selects a preset and chooses `Load`.
2. The workflow copies the six included values into the current Edit
   Appearance session.
3. Excluded identity, target, lock, and texture metadata remain unchanged.
4. Existing live-preview debounce and scheduling applies the preview.
5. `Save` follows the existing validated update-and-synchronize path.
6. `Cancel` follows the existing restore path and returns the tattoo to the
   appearance captured when editing began.

Loading a preset is session-local and requires no persistence request.

### Create and Overwrite

`Save Preset` opens a naming popup using the current edited values. A unique
name creates a new final entry if the list contains fewer than twenty entries.
A folded-name collision requires explicit `Overwrite` confirmation. An
overwrite replaces the stored name spelling and six values at the existing
index.

### Rename and Delete

`Manage` permits rename and delete. Rename trims and validates the new name and
rejects collision with any different entry; rename never implies overwrite.
Delete requires confirmation that names the target preset. Both operations
preserve the ordering contract.

## UI/UX

Add an `Appearance Preset` section above `Basic` inside Edit Appearance. Keep
the primary footer and its `Cancel`, `Save`, and `Close` behavior unchanged.

The section contains:

- a Dropdown in stored order;
- `Load`;
- `Save Preset`; and
- `Manage`.

When no presets exist, display `No appearance presets saved.` Mutation controls
are disabled while a preset storage request is pending. Reaching the limit
disables only creation under a new name; overwrite, rename, delete, and load
remain available.

Successful Load presents the values as unsaved edit-session state. It must not
imply that the tattoo was saved. Popup and button placement must not alter the
stable footer layout.

## Error and Retry Semantics

Preset persistence errors are separate from tattoo-operation, live-preview,
Favorites, and Recently Used errors. A storage failure must not disable Edit
Appearance, tattoo `Save`, tattoo `Cancel`, or restoration.

Display `Appearance Presets: <message>` with an explicit `Retry` action. Retry
replays only the failed preset load or mutation with a new preset request ID.
It must not load values into the edit session, save appearance, repeat an
appearance mutation, or synchronize the tattoo.

The UI must not optimistically publish create, overwrite, rename, or delete.
The committed list changes only after a successful persistence completion.

## Automated Verification

### Store

- Missing file and missing member return an empty list without creating a
  file.
- All six fields round-trip exactly within the supported numeric contract.
- JSON order is preserved.
- Create appends; overwrite and rename retain index; delete preserves relative
  order.
- Empty, folded-duplicate, malformed, non-finite, out-of-range, over-limit,
  and unsupported-version data fails without overwrite.
- Interleaved Hotkey, Favorites, Recently Used, and preset mutations preserve
  all supported and unknown data.

### Workflow and Runtime

- Initial load, every mutation, exception, and scheduler rejection passes
  through the scheduler and completion mailbox.
- Stale completions do not replace current state.
- Loading changes exactly the six included edit values.
- Texture metadata, lock, identity, Actor, area, slot, and handle remain
  unchanged.
- Load then Cancel restores the original appearance.
- Load then Save uses the existing appearance update and synchronization
  contract.
- Persistence failure leaves appearance editing usable.
- Retry repeats only the failed preset request.

### Adapter

- Empty, unavailable, pending, limit, overwrite, rename collision, and delete
  confirmation states have deterministic presentation.
- The preset section does not alter footer placement.

## Manual Acceptance

- Create and persist twenty presets; reject a twenty-first unique name.
- Confirm display order matches creation order across restart.
- Reorder the JSON array manually and confirm the Dropdown follows it after
  reload.
- Confirm overwrite and rename keep position and delete preserves remaining
  order.
- Confirm the same resolved configuration path exposes the same presets across
  sequential MO2 profiles.
- Confirm live preview, Save, Cancel, deferred Close, and synchronization-only
  retry retain their existing behavior.
- Exercise zero, greater-than-one, and upper-bound material values.
- Load a preset created from another tattoo and confirm the destination
  tattoo's glow and bump textures remain unchanged.

## Completion Rule

Do not mark Appearance Presets complete in `ROADMAP.md` until implementation,
automated Debug and Release verification, deployment, and intended in-game
acceptance pass. Keep Saved Tattoo Sets / Loadouts as a separate roadmap item.
