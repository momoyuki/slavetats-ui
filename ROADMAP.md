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

## Active UI integration — FLICK presentation host

**Status:** Complete

**Design:** `docs/superpowers/specs/2026-10-14-flick-ui-design.md`

Add an optional FLICK entry point that opens the existing Menu Framework
browser, and improve the left-side presentation of all workflow screens.
Reuse the current model, scheduler, service, and SlaveTatsNG runtime. Camera control and
hover-to-apply preview are separate future decisions because they affect game
state outside the current UI boundary.

The FLICK launcher and unified presentation were merged into `main` in PR #15.
The in-game acceptance checklist was completed on the merged candidate.

---

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

**Status:** Implemented; explicit NPC in-game safety checks pending

**Design:** `docs/superpowers/specs/2026-09-17-native-actor-targeting-design.md`

Move the native workflow beyond the hard-coded Player target while preserving the same slot-first model.

### Candidate targets

- [x] Player
- [x] Nearby Actor snapshot (nearest-first, capped at 10)
- [ ] Selected/explicit NPC — merged in PR #17; in-game checks 1–4 and 7 passed. Checks 5–6 (unloaded NPC and stale Refresh completion) remain deferred by user acceptance after merge.
- [x] Follower-oriented target convenience using the player-teammate flag — merged in PR #18; in-game discovery, Search, Refresh, Close, exact selection, and switching targets passed.

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

**Status:** Complete

**Active design:** `docs/superpowers/specs/2026-10-10-controlled-material-texture-selection-design.md`

**Release baseline:** The accepted capabilities checked below are being prepared
for public testing as `v1.8.0-beta.1`. This release does not mark the remaining
unchecked candidates complete.

Add convenience features only after the full appearance model and actor-scoped runtime behavior are stable.

Candidates:

- [x] Live actor preview with throttling/debounce and explicit synchronization policy
- [x] Favorites
- [x] Recently used tattoos
- [x] Appearance presets
- [x] Saved tattoo sets / loadouts
- [x] Applied-only browser filter
- [x] Glow / Bump / Gloss catalog badges and filters
- [x] Controlled alternate glow/bump texture selection

These are convenience features, not compatibility prerequisites. The complete
vNext.4 acceptance record is maintained in the controlled material texture
selection spec.

---

## vNext.5 — Visual Tattoo Browser

**Status:** In progress on `codex/ui-improvements`

The 2026-10-16 UI refinement consolidates nearby NPCs and Followers into one
Choose NPC picker beside Player, with an explicit 2048-unit discovery radius.

The 2026-10-16 catalog preview request adds hover-to-preview, click-to-pin,
Ink/Glow/Glossy draft controls and explicit Save/Undo semantics. The approved
transaction and nonpersistent visual-backend contract is recorded in
`docs/superpowers/specs/2026-10-16-catalog-hover-preview-design.md`; implementation
is deployed for in-game testing. The user confirmed that the corrected hover
preview no longer turns the Player's Body overlay purple. Save, Undo, NPC
behavior, in-use card indicator, and save/co-save/reload isolation remain to be
accepted in game. Active plan:
`docs/superpowers/plans/active/2026-10-16-catalog-hover-preview.md`.

**Design:** `docs/superpowers/specs/2026-10-15-scrollable-tattoo-grid-design.md`,
`docs/superpowers/specs/2026-10-15-browser-position-and-camera-design.md`

Improve the Browse screen for visual selection: a six-column gallery on a
desktop-width panel, vertical scrolling, readable names, and bounded thumbnail
work for visible rows. Narrow panels use fewer columns. This remains in the
existing SKSE Menu Framework browser with the Actor visible beside it.

Hovering a tattoo should eventually show an uncommitted preview on the exact
Actor Target and restore the prior visual state on pointer exit. This requires
its own actor-scoped transaction and save/co-save safety design before runtime
implementation. The browser position can be selected as Left or Right in the
Menu Framework launcher. Camera compatibility is a separate slice: SmoothCam
ownership and a game-thread turntable must be implemented and validated together.
The first camera QA pass is limited to Player; selecting an NPC restores normal
camera control while the NPC tattoo workflow remains available.
The Player camera now has one initial framing request aimed at the open control
area opposite the configured browser side, retaining the captured angle and
distance. The user confirmed the Left and Right menu positions in-game.
First-gesture continuity, close/restore, and the SmoothCam handoff still need
separate acceptance.

**Next camera follow-up (recorded 2026-10-17; not implemented):** On the first
Player browser opening in a game session, use the configured initial Center
composition (the current side-aware Whole body framing until a future setting
exists). On later openings, restore the last browser orbit, pan/focus, and zoom
for that menu side so repeated tattoo comparisons do not require re-framing.
Keep separate Left and Right in-memory views; reset them when a save is loaded
or the game process exits. Closing the browser must still restore the captured
game camera. Do not persist a raw camera pointer or world-space position, and
do not extend this behavior to NPC camera control in the Player-first slice.
Define and test the capture point, invalidation, and SmoothCam handoff in the
camera spec before implementation. This session recall needs no new setting;
user-adjustable initial framing remains a future settings candidate.

---

## Future UI settings to evaluate

These are candidates, not committed milestones. Revisit them after vNext.5
in-game acceptance; the [SlavetatsUIPzx settings comparison](docs/superpowers/research/2026-10-17-slavetatsuipzx-settings.md)
records the reference behavior and source links.

- **Initial framing adjustment by menu side:** If automatic placement still
  leaves the Player off-center, offer a small lateral adjustment for Left and
  Right while preserving the captured camera angle and zoom. Decide the range
  from in-game tests on both sides before adding a persistent setting.
- **Hover preview toggle:** Consider an off switch if users need to browse
  without temporary visual changes. Validate Save, Undo, and save/co-save/reload
  isolation first; a toggle must not conceal an unsafe preview implementation.
- **Additional camera controls:** Consider saved offset, zoom, FOV, or rotation
  defaults only if framing plus Center/Reset View proves insufficient. Avoid a
  large settings panel without a demonstrated need.
- **Pause-world toggle:** Evaluate separately because it changes menu and input
  ownership, not just presentation.

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
