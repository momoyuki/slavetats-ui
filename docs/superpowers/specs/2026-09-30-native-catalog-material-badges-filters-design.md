# Native Catalog Material Badges and Filters Design

## Context

SlaveTats UI already preserves optional SlaveTatsNG 0.8.x material metadata in
catalog tattoo definitions. The catalog browser does not currently expose that
metadata, so users must preview or apply a tattoo before learning whether it
declares glow, bump, or glossy material behavior.

This design adds compact catalog badges and matching filters without changing
the authoritative apply path. SlaveTatsNG remains responsible for resolving and
applying tattoo templates; catalog metadata is presentation and discovery data
only.

## Goals

- Classify catalog tattoos consistently as Glow, Bump, and/or Gloss.
- Show compact material badges on catalog thumbnails.
- Add matching catalog filters that compose with existing filters.
- Preserve compatibility with legacy packs that omit advanced metadata.
- Keep classification outside the native rendering adapter so repository and UI
  behavior cannot drift.
- Add deterministic tests at the repository, workflow, and presentation seams.

## Non-Goals

- No alternate glow or bump texture selection.
- No free-form texture path editing.
- No material-effect simulation in thumbnails.
- No changes to Edit Appearance.
- No changes to apply, replace, slot allocation, or synchronization behavior.
- No changes to tattoo JSON schemas or persistent user preference files.
- No broad catalog or filter-layout redesign.

## Material Classification

Material capabilities are derived from `repository::TattooDefinition` through a
single deterministic, side-effect-free classification helper. The helper is the
shared source of truth for repository filtering and presentation badges.

### Glow

A tattoo is classified as Glow when any of the following is true:

- `glow` is present and is not `0`;
- `glowTexture` is present and contains a non-empty path; or
- `emissiveMult` is present and differs materially from the default `1.0`.

The emissive comparison must use an explicit floating-point tolerance rather
than exact equality. Glow color, glow texture, and emission strength remain
independent signals; a tattoo does not need a glow texture to be classified as
Glow.

### Bump

A tattoo is classified as Bump when `bump` is present and contains a non-empty
path.

### Gloss

A tattoo is classified as Gloss when either of the following is true:

- `glossiness` is present and greater than `0`; or
- `specularStrength` is present and greater than `0`.

### Missing and Default Values

Missing optional fields in legacy tattoo packs do not imply a capability.
Explicit default values also do not imply a capability:

- `glow = 0`;
- empty `glowTexture` or `bump` paths;
- `emissiveMult = 1.0` within the documented tolerance;
- `glossiness = 0`; and
- `specularStrength = 0`.

The classifier must not inspect thumbnails or infer properties from diffuse
textures.

## Repository Filtering

Extend `repository::TattooFilter` with three independent Boolean conditions:

```cpp
bool glowOnly{false};
bool bumpOnly{false};
bool glossOnly{false};
```

When a condition is false, it does not restrict results. When multiple material
conditions are true, a tattoo must satisfy all of them. Material conditions also
combine with search, domain, source, section, area, Applied, Favorites, and
Recently Used constraints using the existing AND semantics.

With all three material conditions disabled, repository query behavior must be
identical to the current behavior.

## Workflow State

The native workflow model owns the three material-filter toggles as transient
picker state. Their initial values are false and they are not persisted between
game launches.

Changing a material filter follows the established contextual-filter behavior:

- rebuild the filtered catalog result through the repository boundary;
- reset pagination or selection to the first valid result according to the
  existing picker behavior; and
- use the existing empty state when no tattoos match.

The model exposes intent operations for toggling the filters. The adapter must
not perform its own repository queries or metadata classification.

## Native Presentation

### Filter Controls

Add `Glow`, `Bump`, and `Gloss` toggles after the existing `Applied` and
`Favorites` controls in the current Filters region.

- Reuse the visual and active-state treatment of existing filter toggles.
- Keep the controls in the existing filter layout.
- Allow the layout to wrap when horizontal space is insufficient.
- Do not introduce a dropdown, popup, or overlay for this slice.

### Thumbnail Badges

Show compact text badges in the lower-left corner of each catalog thumbnail.

- Use the fixed order `Glow`, `Bump`, `Gloss`.
- Render only capabilities returned by the shared classifier.
- Arrange multiple badges horizontally and keep them within the thumbnail.
- Do not reserve badge space for tattoos without material capabilities.
- Do not overlap the Favorite star in the upper-left corner or the existing
  `In Use` indicator.
- Do not tint badges or thumbnails in a way that implies an accurate preview of
  Skyrim lighting.

Presentation helpers may convert the shared classification result into ordered
labels, but they must not duplicate the classification rules.

## Architecture

The change preserves the existing layering:

```text
Native UI / adapter
        |
        v
Workflow model
        |
        v
Tattoo repository and material classifier
```

The feature reads catalog metadata only. It does not call JContainers,
SlaveTatsNG, `SlaveTatsService`, or runtime scheduling code, and it does not
mutate an Actor Target.

The classifier should live at the repository/domain seam next to
`TattooDefinition`, with a small public result or predicate interface suitable
for both filtering and presentation. Do not add cached Boolean fields to
`TattooDefinition`; they would duplicate derivable metadata and could drift.

## Error Handling and Compatibility

- Missing material metadata is valid and produces no badges.
- Existing parser validation continues to reject malformed optional fields and
  report indexed issues without discarding valid sibling entries.
- The new filters introduce no error state; zero matches use the existing empty
  catalog state.
- Existing tattoo packs and user JSON files require no migration.
- Apply and Edit Appearance continue to use their current authoritative runtime
  behavior.

## Automated Testing

### Classification

- missing advanced fields produce no capabilities;
- explicit default values produce no capabilities;
- nonzero glow color produces Glow;
- a non-empty glow texture produces Glow;
- a materially non-default emissive multiplier produces Glow;
- values within the emissive tolerance remain default;
- a non-empty bump path produces Bump;
- positive glossiness produces Gloss;
- positive specular strength produces Gloss;
- independent signals produce the expected combinations.

### Repository

- each material filter includes only matching tattoos;
- multiple enabled material filters use AND semantics;
- material filters compose with existing filters;
- disabling all material filters preserves existing query results and ordering;
- legacy definitions without metadata remain queryable when material filters are
  disabled.

### Workflow

- material filters default to disabled;
- each toggle updates the repository filter and visible results;
- enabling multiple toggles retains only tattoos satisfying all conditions;
- changing a toggle resets picker pagination or selection consistently with
  existing filter transitions;
- no-match results use the existing empty state.

### Adapter

- ordered labels are `Glow`, `Bump`, `Gloss`;
- absent capabilities render no badge labels;
- filter controls emit workflow intent rather than querying metadata directly;
- badge layout stays within the thumbnail and does not collide with Favorite or
  `In Use` indicators at supported menu sizes.

## In-Game Acceptance

1. Open the tattoo catalog with all material filters disabled and confirm the
   existing result set and ordering are unchanged.
2. Confirm tattoos with declared material properties show the correct ordered
   badges while ordinary and legacy tattoos show none.
3. Toggle Glow, Bump, and Gloss individually and confirm each result set.
4. Enable two or three material filters and confirm every visible tattoo has all
   requested badges.
5. Combine material filters with Applied, Favorites, search, and contextual
   source/section filters.
6. Resize or inspect the menu at supported resolutions and confirm filter
   wrapping and badge placement do not obscure thumbnails, Favorite stars, or
   `In Use` indicators.
7. Apply a filtered tattoo and confirm normal preview/apply behavior is
   unchanged.

## Follow-Ups

The following remain separate roadmap work:

- Saved tattoo sets / loadouts.
- Controlled alternate glow/bump texture selection.
- Any broader redesign of the Filters region or thumbnail overlays.

## Implementation Boundaries

Expected implementation areas include:

- `src/repository/TattooSourceParser.h` or a focused repository classification
  helper;
- `src/repository/TattooRepository.*`;
- `src/native/NativeSlotWorkflowModel.*`;
- `src/native/OfficialMenuFrameworkAdapter.*`;
- corresponding repository, workflow, and adapter tests.

Implement test-first and keep the change within this roadmap checkbox. Before
merge, require relevant Debug and Release builds and test suites,
`git diff --check`, a scoped diff review, and the in-game acceptance checks
above.
