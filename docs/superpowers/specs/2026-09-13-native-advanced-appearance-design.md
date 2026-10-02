# Native Advanced Appearance Design

## Context

SlaveTats UI currently provides a native slot-first workflow for browsing installed tattoo packs, inspecting Player overlay slots, applying and replacing tattoos, removing tattoos, and editing an applied tattoo's diffuse color and visible alpha.

SlaveTatsNG 0.8.x exposes additional material properties that are not represented by the current SlaveTats UI core model or Edit Appearance flow. These properties include emissive color (`glow`), glow/detail texture (`glowTexture`), emissive multiplier (`emissiveMult`), glossiness (`glossiness`), specular strength (`specularStrength`), and bump/normal texture (`bump`).

Pack-defined values are already preserved by the normal apply path because SlaveTats UI looks up the SlaveTatsNG tattoo template, temporarily overrides only color and inverted alpha, and passes that template to `add_and_get_tattoo`. The missing capability is visibility and editing of those properties after a tattoo is applied.

The current Edit Appearance design intentionally limited the editor to color and alpha. This proposal extends that design without changing the slot-first architecture or bypassing the existing service/runtime boundaries.

## Goals

- Represent all SlaveTatsNG 0.8.x appearance/material fields required for applied tattoo editing.
- Preserve the existing color and visible-alpha behavior.
- Read current advanced appearance values into the Current Slots snapshot.
- Expose advanced appearance controls for SlaveTats-managed occupied slots.
- Update only the selected tattoo object, mark the actor as changed, and synchronize once.
- Retain stale-handle validation before mutation.
- Preserve synchronization-only retry semantics after a partial success.
- Keep pack-defined advanced values unchanged when applying a tattoo unless the user explicitly edits them afterward.
- Add deterministic tests for every new field at Core, Runtime, Workflow, and Adapter seams.

## Non-Goals

- No NPC selector in this change.
- No live actor preview while dragging controls.
- No domain selector or cross-domain browser redesign.
- No tattoo template authoring or JSON file editing.
- No arbitrary texture file picker.
- No editing of external overlay slots.
- No automatic mutation of pack defaults.
- No change to slot allocation behavior.
- No automatic rollback after a synchronization failure.
- No direct JContainers or SlaveTatsNG calls from the workflow model or ImGui adapter.

Lock/unlock support is related but orthogonal to material appearance and remains deferred to a follow-up milestone.

## SlaveTatsNG Appearance Fields

The native model should distinguish the following concepts:

| SlaveTats field | Meaning | UI treatment |
|---|---|---|
| `color` | Diffuse RGB tint | Color picker |
| `invertedAlpha` | Stored inverse of visible alpha | Existing Alpha slider using visible `0.0-1.0` |
| `glow` | Emissive RGB color | Glow Color picker |
| `glowTexture` | Glow/detail texture path | Read-only pack path initially |
| `emissiveMult` | Emissive intensity multiplier | Numeric slider/input |
| `glossiness` | Surface glossiness | Numeric slider/input |
| `specularStrength` | Specular response strength | Numeric slider/input |
| `bump` | Bump/normal texture path | Read-only pack path initially |

`glow`, `glowTexture`, and `emissiveMult` must be treated as separate properties. A tattoo may have an emissive color without a custom glow texture, a glow texture with default emissive multiplier, or all three.

## Core Model

Extend `core::TattooEntry` with advanced appearance values:

```cpp
std::int32_t glow{0};
float glossiness{0.0F};
float specularStrength{0.0F};
std::string bump;
std::string glowTexture;
float emissiveMult{1.0F};
```

The Slot Snapshot becomes the authoritative UI source for the currently applied tattoo's appearance.

Extend the appearance request model so the runtime can update the complete editable appearance atomically from the UI's perspective:

```cpp
struct UpdateTattooAppearanceRequest {
    std::uint32_t actorFormId{};
    std::int32_t runtimeHandle{};

    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};

    std::int32_t glow{0};
    float glossiness{0.0F};
    float specularStrength{0.0F};
    float emissiveMult{1.0F};

    UpdateTattooAppearanceMode mode{
        UpdateTattooAppearanceMode::updateAndSynchronize};
};
```

`bump` and `glowTexture` should initially be displayed as metadata rather than free-form editable text. Arbitrary path editing creates a larger validation and asset-resolution problem and is not required to expose the practical 0.8.x material controls.

A later change may add controlled texture selection using pack metadata.

## Runtime Snapshot

`SlaveTatsRuntime::queryAvailable()` and `querySlots()` should read the advanced fields from the JMap when present.

Recommended defaults:

```text
glow             = 0
glossiness       = 0.0
specularStrength = 0.0
bump              = ""
glowTexture       = ""
emissiveMult      = 1.0
```

The runtime must use the exact SlaveTatsNG field names and preserve missing-field compatibility with older tattoo packs.

The snapshot should never synthesize a glow texture or bump path from the diffuse texture.

## Appearance Update Runtime

The current runtime update path already:

1. resolves the actor;
2. validates the session-local tattoo handle against the actor's currently applied tattoos;
3. writes color and inverted alpha;
4. marks `.SlaveTats.updated = 1`;
5. calls `synchronize_tattoos` once;
6. supports synchronization-only retry after a partial success.

Extend the validated write to include:

```text
color
invertedAlpha
glow
glossiness
specularStrength
emissiveMult
```

All writes should use the same verified JContainers boundary already used for color and alpha. If any requested field cannot be written or verified, return `updateFailed` before marking the actor updated or synchronizing.

The update operation should remain all-or-fail at the storage-validation layer as far as practical. If sequential writes make partial storage mutation possible, document that limitation and add tests around failure order. Do not hide partial mutation behind a generic success.

### Validation

Validate before mutation:

- actor form ID is nonzero;
- runtime handle is nonzero in update mode;
- runtime handle belongs to the requested actor;
- diffuse color is within `0x000000-0xFFFFFF`;
- glow color is within `0x000000-0xFFFFFF`;
- visible alpha is finite and within `0.0-1.0`;
- glossiness is finite and non-negative;
- specular strength is finite and non-negative;
- emissive multiplier is finite and non-negative.

The service should not invent undocumented hard upper limits for SlaveTatsNG material floats. The UI may provide a practical slider range while still allowing a numeric input for larger valid values.

## Native Workflow Model

Replace the current two-field preview/edit state with a richer copyable appearance value object, for example:

```cpp
struct TattooAppearance {
    std::int32_t color{0xFFFFFF};
    float alpha{1.0F};
    std::int32_t glow{0};
    float glossiness{0.0F};
    float specularStrength{0.0F};
    float emissiveMult{1.0F};
};
```

`AppearanceEditSession` continues to store immutable `original` and mutable `edited` values.

Dirty state compares every editable field after normalization.

The model must continue to emit no mutation while controls are being edited. A request is created only after Save.

On successful Save:

- clear the edit session;
- return to Current Slots;
- refresh only the selected area.

On write/validation failure:

- retain edited values;
- keep Save retry available.

On synchronization failure after mutation:

- retain the edited appearance;
- switch to synchronization-only mode;
- show `Retry Sync`;
- do not rewrite any appearance field during retry.

## Native UI

Keep the existing Edit Appearance screen and extend it into two visual groups.

### Basic

```text
Color        [ color picker ]
Alpha        [ slider / numeric ]
```

### Material / Emission

```text
Glow Color         [ color picker ]
Emission Strength  [ slider + numeric input ]
Glossiness         [ slider + numeric input ]
Specular Strength  [ slider + numeric input ]

Glow Texture       <pack path>   (read-only initially, hidden when absent)
Bump Texture       <pack path>   (read-only initially, hidden when absent)
```

The advanced section may be collapsed by default if needed to preserve a compact native menu.
Glossiness uses a logarithmic slider from 0 to 1000. Specular Strength uses a
logarithmic slider from 0 to 100. Their numeric inputs remain available for
precise entry.

### Presentation

- Continue tinting the diffuse thumbnail with edited diffuse color and alpha.
- Present the editor thumbnail as a centered square, preferring 160 by 160
  pixels and shrinking only when the available content area requires it.
- Do not pretend the 2D thumbnail accurately previews Skyrim emissive/specular lighting.
- Show a small `Glow` badge in the editor when any of `glow != 0`, non-empty `glowTexture`, or `emissiveMult` differs materially from its default.
- Show `Bump` when a bump texture is present.
- Hide an advanced texture metadata row when its path is absent.

## Catalog Metadata

The repository parser currently preserves optional `glow`, `in_bsa`, and `credit` from tattoo JSON. Expand the parser model to preserve the following optional fields when valid:

```text
glow
glowTexture
emissiveMult
glossiness
specularStrength
bump
```

This metadata is useful for catalog badges and diagnostics even though apply continues to use the authoritative SlaveTatsNG template lookup.

Malformed optional advanced fields should produce an indexed parse issue for that entry without discarding valid sibling entries, matching existing parser behavior.

The catalog does not need editing controls in this change.

## Apply Behavior

Do not copy advanced material values from the local repository definition into the actor manually during Apply.

The authoritative apply flow remains:

```text
catalog selection
-> identify SlaveTatsNG template by domain/section/name
-> temporarily override requested diffuse color and alpha
-> add_and_get_tattoo
-> synchronize
```

This preserves the template's pack-defined glow, bump, glossiness, specular strength, glow texture, and emissive multiplier automatically.

The temporary appearance guard must continue restoring the queried template after `add_and_get_tattoo` so UI-selected diffuse color/alpha do not mutate global available-tattoo template state.

## Compatibility

- Missing advanced keys in legacy packs must remain valid.
- Existing tattoos with only color/alpha should render and edit as before.
- Existing `updateTattoo` callers that only provide color and alpha must remain compatible unless a deliberate API version bump is introduced.
- If the Prisma compatibility layer remains present, omitted advanced fields should preserve current tattoo values rather than reset them to defaults.
- SlaveTatsNG 0.8.x should be the reference capability level for the advanced material fields.

## Prisma / External Contract

Do not silently change the meaning of the existing `updateTattoo` request.

Two acceptable implementation strategies are:

1. extend the request with optional advanced fields and preserve current values when omitted; or
2. add a distinct advanced appearance action/versioned request.

Prefer option 1 if it can be implemented without ambiguity.

Example extended request:

```json
{
  "action": "updateTattoo",
  "actorId": 20,
  "tattooHandle": 123,
  "color": 16777215,
  "alpha": 1.0,
  "glow": 16711680,
  "glossiness": 2.0,
  "specularStrength": 1.5,
  "emissiveMult": 3.0
}
```

All new fields should be optional for compatibility.

## Error Handling

- Missing SlaveTatsNG or JContainers: reject before mutation.
- Missing actor: keep editor state and report the error.
- Stale or foreign tattoo handle: write nothing and request refresh.
- Invalid numeric values: reject before mutation.
- JContainers write/readback failure: report `updateFailed` and do not synchronize.
- Synchronization failure after successful writes: report partial success and expose `Retry Sync`.
- Retry Sync must perform no second appearance write.
- Empty glow/bump metadata is not an error.

## Testing

### Parser

- preserves valid glow color;
- preserves `glowTexture` and `bump` paths;
- preserves finite float values for emissive multiplier, glossiness, and specular strength;
- rejects malformed optional field types with an indexed issue;
- missing advanced fields remain valid and use model defaults.

### Core Service

- forwards all advanced values unchanged when valid;
- rejects diffuse/glow colors outside RGB range;
- rejects NaN/infinite alpha or material floats;
- rejects negative material floats;
- synchronization-only mode bypasses appearance-value validation that is irrelevant to retry.

### Runtime Snapshot

- `queryAvailable` and `querySlots` read every advanced field;
- missing keys map to the documented defaults;
- glow texture and bump paths are preserved exactly.

### Runtime Mutation

- valid handle writes color, inverted alpha, glow, glossiness, specular strength, and emissive multiplier;
- each write is verified;
- stale handle performs zero writes and zero synchronization;
- successful mutation marks the actor updated and synchronizes exactly once;
- sync failure returns partial-success semantics;
- retry sync performs zero appearance writes.

### Native Workflow

- edit session initializes all fields from the selected Slot Snapshot;
- changing any advanced field makes the session dirty;
- changing a field back to its normalized original value clears dirty state;
- Cancel before Save performs no mutation;
- Save forwards all edited values;
- write failure preserves editor values;
- synchronization failure switches to Retry Sync without a second mutation;
- stale completions cannot overwrite a newer edit session.

### Adapter

- diffuse and glow RGB conversion preserve channel order;
- sliders/numeric inputs clamp only UI presentation, not undocumented runtime maxima;
- advanced metadata renders `None` for empty paths;
- Save enablement includes advanced-field dirty state;
- existing Color/Alpha UX remains functional.

## In-Game Acceptance

1. Open an occupied SlaveTats-managed slot and choose Edit Appearance.
2. Current diffuse color and alpha match the actor's applied tattoo.
3. Current glow color, emissive multiplier, glossiness, and specular strength are displayed correctly.
4. Glow and bump texture paths are visible when present.
5. Changing Glow Color and saving visibly changes emissive tint after synchronization.
6. Changing Emission Strength changes glow intensity after synchronization.
7. Changing Glossiness or Specular Strength affects the material after synchronization.
8. A non-glowing tattoo can be given an emissive color/multiplier without corrupting its diffuse appearance.
9. Saving a normal tattoo with no advanced changes does not introduce glow, bump, or gloss unexpectedly.
10. Removing or replacing a tattoo still relies on SlaveTatsNG cleanup and does not leave stale advanced state in a reused slot.
11. A stale handle never edits another tattoo.
12. A failed synchronization can be retried without reapplying the appearance write.

## Follow-Ups

These are intentionally outside this PR's implementation scope:

- Lock / Unlock toggle for SlaveTats-managed tattoos.
- Domain selector / multi-domain browsing.
- NPC or crosshair-target actor selector.
- Live actor preview with debounced synchronization.
- Favorites and Recently Used.
- Glow/Bump/Gloss catalog filters.
- Appearance presets.
- Controlled selection of alternate glow/bump textures from pack metadata.

## Implementation Boundaries

Expected implementation areas include:

- `src/core/TattooModels.h`
- `src/core/SlaveTatsService.*`
- `src/runtime/SlaveTatsRuntime.*`
- `src/runtime/UpdateTattooAppearanceOrchestration.*`
- `src/repository/TattooSourceParser.*`
- `src/native/NativeSlotWorkflowModel.*`
- `src/native/NativeSlotWorkflowRuntime.*`
- `src/native/OfficialMenuFrameworkAdapter.*`
- corresponding Core, Runtime, Repository, Native, and Adapter tests

Keep the implementation incremental and test-first. Avoid unrelated refactors in feature commits.

Before merge, require Debug and Release builds, complete CTest suites, `git diff --check`, focused diff review, and in-game acceptance against a SlaveTatsNG 0.8.x environment.
