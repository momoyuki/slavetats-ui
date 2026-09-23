# SlaveTats UI Roadmap

This file is the durable source of truth for **project direction and milestone order**.

Implementation details belong in `docs/superpowers/specs/` and `docs/superpowers/plans/`. Current terminology and shared mental models belong in `CONTEXT.md`. GitHub issues and pull requests track execution status, but should not replace this roadmap.

## Project goal

Make day-to-day SlaveTats management fast and practical from a native in-game UI while keeping **SlaveTatsNG as the authoritative tattoo runtime**.

The target experience is a slot-first workflow where users can inspect current overlays, browse installed packs, apply or replace tattoos, edit appearance, remove tattoos safely, and eventually manage more than the Player without returning to MCM for routine work.

## Product principles

1. **SlaveTatsNG remains authoritative.** Do not reimplement tattoo ownership, slot allocation, synchronization, or pack semantics in the UI layer when SlaveTatsNG already owns them.
2. **Respect external overlays.** External slots are visible but read-only unless an explicit future design says otherwise.
3. **Keep runtime access behind typed boundaries.** Native UI and workflow models must not call JContainers or SlaveTatsNG directly.
4. **Preserve stale-handle safety.** Session-local tattoo handles must be revalidated against the requested actor before mutation.
5. **Prefer deterministic workflow state.** UI callbacks express intent; model/runtime layers own state transitions and mutation.
6. **Keep milestones reviewable.** Do not silently fold unrelated features into the active milestone.
7. **Preserve compatibility first.** Existing pack metadata and SlaveTatsNG defaults should survive apply/edit flows unless the user explicitly changes them.
8. **Treat synchronization failure as a partial-success case when storage mutation already happened.** Retry synchronization without silently repeating writes.

## Current baseline

The native workflow currently supports:

- Player Current Slots for Body, Face, Hands, and Feet;
- empty, SlaveTats-managed, and external slot states;
- catalog browse, search, source/section/area filters, and pagination;
- DDS thumbnails from loose files and BSA archives;
- apply and replace with explicit confirmation;
- remove with synchronization retry behavior;
- Edit Appearance for diffuse color and visible alpha;
- stale-handle validation before appearance mutation;
- native SKSE Menu Framework integration.

This baseline is the foundation. Future milestones should extend it instead of creating parallel workflows.

---

# Milestones

## vNext.1 — Full SlaveTatsNG 0.8 Appearance

**Status:** Complete

**Design:** `docs/superpowers/specs/2026-09-13-native-advanced-appearance-design.md`

Bring the applied-tattoo appearance model and native editor up to the SlaveTatsNG 0.8.x material feature set.

### Required fields

- [x] `glow` — emissive RGB color
- [x] `glowTexture` — glow/detail texture metadata
- [x] `emissiveMult` — emissive intensity
- [x] `glossiness`
- [x] `specularStrength`
- [x] `bump` — bump/normal texture metadata

### Required implementation slices

- [x] Extend Core tattoo snapshot/model fields.
- [x] Extend runtime snapshot/readback from JContainers.
- [x] Extend typed appearance update request and validation.
- [x] Add verified runtime writes for editable advanced fields.
- [x] Preserve synchronization-only retry semantics.
- [x] Extend Edit Appearance with Material / Emission controls.
- [x] Preserve read-only texture metadata initially for `glowTexture` and `bump`.
- [x] Extend local tattoo JSON parsing so 0.8.x metadata is not discarded.
- [x] Add Core, Runtime, Workflow, Adapter, parser, and in-game regression coverage.

### Acceptance target

A SlaveTatsNG 0.8.x tattoo with emissive/material metadata can be applied without losing pack-defined values, inspected after application, edited through the native appearance editor where supported, saved safely, and re-synchronized without corrupting unrelated slot state.

### Explicitly deferred

The following are not required to complete vNext.1:

- Lock / Unlock
- Domain selector
- NPC targeting
- live actor preview while dragging controls
- arbitrary glow/bump texture path editing
- favorites / recently used
- appearance presets

---

## vNext.2 — Lock and Domain

**Status:** Complete

Close two important SlaveTats workflow gaps without expanding actor targeting yet.

### Lock / Unlock

- [x] Expose current `locked` state for SlaveTats-managed slots.
- [x] Add a safe Lock / Unlock mutation path.
- [x] Revalidate tattoo ownership before mutation.
- [x] Preserve external-slot read-only behavior.

### Domain support

- [x] Stop treating `default` as the only user-facing domain.
- [x] Discover available domains through the MO2-resolved catalog boundary.
- [x] Add a domain selector/filter where it improves browse/apply behavior.
- [x] Preserve the selected/applied tattoo's real domain in slot snapshots.

### Acceptance target

Users can manage locked state and browse/apply tattoos outside the default domain without weakening current slot safety guarantees.

---

## vNext.3 — Actor Targeting

**Status:** Complete

**Design:** `docs/superpowers/specs/2026-09-17-native-actor-targeting-design.md`

Move the native workflow beyond the hard-coded Player target while preserving the same slot-first model.

### Candidate targets

- [x] Player
- [x] Crosshair target
- [ ] Selected/explicit NPC
- [ ] Follower-oriented target convenience if it can be implemented without heuristic mutation

### Required safety work

- [x] Actor identity must be explicit in every typed request.
- [x] Slot snapshots must remain actor-scoped.
- [x] Stale completions from a previous actor selection must not update the current view.
- [x] Actor 3D/load state and synchronization constraints must be handled deliberately.
- [x] Never fall back silently to the Player if a requested NPC cannot be resolved.

### Acceptance target

The same browse/apply/edit/remove workflow works for a deliberately selected loaded actor without cross-actor handle or state leakage.

---

## vNext.4 — Workflow Quality of Life

**Status:** Current priority

Add convenience features only after the full appearance model and actor-scoped runtime behavior are stable.

Candidates:

- [x] Live actor preview with throttling/debounce and explicit synchronization policy
- [ ] Favorites
- [ ] Recently used tattoos
- [ ] Appearance presets
- [ ] Saved tattoo sets / loadouts
- [ ] Applied-only browser filter
- [ ] Glow / Bump / Gloss catalog badges and filters
- [ ] Controlled alternate glow/bump texture selection

These are convenience features, not compatibility prerequisites.

---

# Known upstream/runtime concerns

These are not necessarily SlaveTats UI features, but they can affect acceptance testing and debugging:

- SlaveTatsNG overlay cleanup behavior must be regression-tested when replacing/removing tattoos with glow, bump, alpha, specular, or emissive state.
- SlaveTatsNG synchronization has historical threading and actor-3D-load constraints; UI code must continue routing work through the appropriate scheduled runtime boundary.
- Custom RaceMenu/`skee64.dll` builds can have address-table compatibility concerns inside SlaveTatsNG.
- Environment-specific failures should be diagnosed at the SlaveTatsNG/runtime boundary rather than worked around by duplicating overlay logic in SlaveTats UI.

# Milestone maintenance rules

When project direction changes:

1. Update this `ROADMAP.md` first.
2. Add or update a focused design in `docs/superpowers/specs/` when behavior or architecture needs definition.
3. Add/update an implementation plan in `docs/superpowers/plans/` when the design is approved and implementation is ready to start.
4. Use GitHub issues/PRs to track concrete execution.
5. Mark roadmap checkboxes only when the capability is present on the target branch and its intended validation has passed.

A merged PR is not automatically a completed milestone if acceptance criteria remain unverified.

# Out of scope unless deliberately promoted

Do not let these distract from the active milestone without an explicit roadmap change:

- replacing SlaveTatsNG as the tattoo runtime;
- editing external overlays owned by other systems;
- arbitrary tattoo-pack authoring inside the runtime UI;
- unrelated RaceMenu overlay management;
- broad refactors that do not unlock the active milestone.
