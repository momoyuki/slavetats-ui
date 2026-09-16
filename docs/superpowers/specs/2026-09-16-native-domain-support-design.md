# Native Domain Support Design

## Context

SlaveTatsNG identifies an available tattoo by its domain, section, and name.
The native workflow currently parses its local JSON catalog but discards the
JSON `domain` value, then always sends `default` when applying a selected
tattoo. This makes tattoos declared outside `default` indistinguishable in the
Picker and can prevent their safe application.

SlaveTatsNG exposes `query_available_tattoos` for a known domain but no public
domain-enumeration API. Its internal cache is not a supported public boundary.
The native catalog already scans the MO2-resolved tattoo JSON sources that
SlaveTatsNG uses, so those sources are the discovery authority for this slice.

## Goal

Let a Player browse and apply tattoo definitions from every domain visible in
the native JSON catalog. The Picker must select Domain before Source and
Section, and every apply must preserve the selected definition's exact domain.

## Scope

- Retain JSON `domain` in each parsed `TattooDefinition`.
- Interpret missing or empty JSON `domain` as `default`.
- Add a deterministic Domain facet to the repository catalog.
- Add a Domain filter to the native Picker before Source and Section.
- Default the Picker to `All Domains`.
- Reconcile Source and Section when the selected Domain changes.
- Display the domain on Picker cards and in SlaveTats-managed Slot Actions.
- Forward the selected definition's domain through `ApplyTattooRequest`.
- Preserve the existing runtime re-query of the requested domain before it
  mutates a slot.

## Non-goals

- No direct read of `.SlaveTatsNG.cache` or its JContainers state.
- No JContainers or SlaveTatsNG API expansion for domain enumeration.
- No manual domain text entry or editable domain names.
- No cross-domain migration of an applied tattoo.
- No persistence subsystem for the selected Domain.
- No NPC targeting, Lock / Unlock changes, thumbnail-background QoL work, or
  changes to external-slot ownership.

## Domain semantics

A domain is the SlaveTatsNG namespace component of a tattoo identity. Tattoos
with the same Section and Name but different domains are distinct definitions.

```text
default / Marks / Rose
custom  / Marks / Rose
```

`All Domains` is a Picker filter state only. It is never stored as a tattoo
domain and is never sent to SlaveTatsNG. Every concrete definition retains one
non-empty domain string.

## Architecture

```text
MO2-resolved tattoo JSON
        |
        v
TattooSourceParser -> TattooDefinition.domain -> TattooRepository facets
                                                  |
                                                  v
Picker: Domain -> Source -> Section -> Area -> selected definition
                                                  |
                                                  v
ApplyTattooRequest.domain -> SlaveTatsRuntime query + validation -> apply
```

### Parser and repository

`TattooDefinition` gains a `domain` value. The parser reads `domain` exactly
from JSON when it is a non-empty string; an absent or empty value becomes
`default` for compatibility with existing packs.

The repository exposes a Domain facet and filter. Domain options are deduped
case-insensitively, sorted deterministically, and retain a stable canonical
spelling from the catalog. The filter compares domain case-insensitively, like
the current source, section, and area filters.

### Native Picker state

`NativeCatalogBrowserModel` owns the selected Domain beside existing filter
values. An empty internal Domain filter means `All Domains`.

Domain is the first contextual filter. A Domain selection resets the Picker
page to the first page and reconciles Source and Section. A Source or Section
that is not available under the newly selected Domain is cleared. Area remains
the Selected Area constraint owned by the workflow.

The selection survives normal Picker navigation, preview, Current Slots, and
area changes while the native menu session lives. A catalog refresh applies
the existing reconciliation policy; an unavailable selected Domain resets to
`All Domains`.

### Presentation

The Picker presents `Domain: All Domains` followed by discovered domain
options before Source and Section controls. Each Picker card displays its
domain badge. SlaveTats-managed Slot Actions display the domain copied from the
applied slot snapshot. Missing runtime domain values remain presented as
`default` through the existing snapshot fallback.

The adapter only presents filter state and workflow intent. It must not query
JContainers, enumerate SlaveTatsNG cache data, or synthesize tattoo domains.

### Apply safety

`NativeSlotWorkflowModel::confirmApply()` copies the selected definition's
domain into `ApplyTattooRequest`; it must not substitute `default`.

`SlaveTatsRuntime::applyToSlot()` continues to query the exact requested
domain, locate the exact Section/Name inside that domain, recheck external
occupancy, and only then call SlaveTatsNG. A definition no longer available at
apply time returns the existing `tattooNotFound` error without mutating the
target slot.

## Error handling

- Empty/missing JSON domain: normalize to `default`, not an error.
- A stale Domain selection after catalog refresh: reset it to `All Domains`.
- No entries for a selected domain and selected area: use the existing
  no-matches presentation; do not make a runtime call.
- Runtime query failure: retain the Preview and target for retry under the
  existing apply-error behavior.
- Runtime domain query with no matching tattoo: return `tattooNotFound` and
  leave the existing slot unchanged.

## Test coverage

- Parser preserves a non-default domain and normalizes missing/empty domains.
- Repository returns deterministic domain options, filters by domain, and
  handles case variants without duplicate options.
- Contextual filtering clears incompatible Source/Section after Domain change.
- `All Domains` retains entries from every discovered domain.
- Workflow forwards the selected definition's exact domain in Apply requests.
- Slot snapshot and Slot Actions present the applied tattoo's domain.
- Runtime checks only the requested domain, rejects a missing definition, and
  does not mutate an external or stale target slot.
- Existing Debug and Release suites remain green.

## Acceptance criteria

1. The Picker defaults to `All Domains` and lists every domain discovered from
   enabled tattoo JSON sources.
2. Selecting a domain constrains Source and Section options contextually.
3. Two same-named tattoos from different domains are distinguishable in the
   Picker through their domain badges.
4. Selecting a tattoo from a non-default domain applies that exact domain.
5. Existing JSON packs without `domain` continue to browse and apply as
   `default`.
6. An unavailable definition at apply time leaves the selected slot unchanged.
7. Slot Actions show the real applied domain.
8. External slots remain read-only and no new direct JContainers interaction
   occurs outside `SlaveTatsRuntime`.

## Follow-up sequencing

This is the remaining functional slice of vNext.2. After automated and
in-game acceptance, mark vNext.2 complete before starting the standalone
thumbnail-background toggle from PR #9. That QoL feature remains a separate,
presentation-only change.
