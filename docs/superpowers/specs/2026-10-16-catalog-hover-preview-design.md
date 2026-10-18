# Catalog Click Preview and Quick Appearance Design

**Date:** 2026-10-16
**Milestone:** vNext.5 — Visual Tattoo Browser
**Status:** Implemented and accepted for the approved vNext.5 interaction.
The user reported no problems in the normal save/load pass and the final
quicksave/autosave/co-save, NPC Preview, and camera test groups on 2026-10-18.

## Intent and user requirements

Browse tattoo thumbnails and click to see a reversible preview on the exact Actor
Target. The user supplied SlavetatsUIPzx as the interaction reference. After
in-game feedback on 2026-10-17, the user chose click-only visual preview:

- Pointer over a tattoo shows its tooltip and card outline only.
- Clicking previews and selects that tattoo and makes Save available after paint.
- The selection stays previewed while moving into Ink / Glow / Glossy controls.
- Save keeps the selected identity and quick appearance values; detailed Edit
  Appearance remains available afterward.
- Undo or Close cancels the unsaved selection and restores the original slot.

This is a catalog identity preview, not the existing appearance-only edit preview.
The six-column scrollable catalog and explicit slot-first workflow stay in place.

## Interaction contract

The model keeps one clicked selection, draft overrides and runtime
visual state separately. It has no hover-paint state or hover selection API.
A candidate key includes actor/generation, preview
session, area/slot, catalog identity, effective appearance and request revision.

1. Hover changes only the tile outline and tooltip. It schedules no Actor paint.
2. Click selects the candidate and requests its visual preview on the next
   available scheduled game-thread task. Repeated clicks on the same effective
   candidate do nothing; a newer click supersedes pending preview intent.
3. Selection retains the candidate identity and effective appearance, even
   while its paint is pending. It does not reset colors or materials to defaults.
4. Moving the pointer over another tile does not change the selected preview.
   Clicking another tile replaces the selection. Save waits for the latest
   selected appearance to finish previewing successfully.
5. Quick controls update the selected draft. Without a selection, controls edit
   local overrides for the next click; they do not enable Save.
6. Undo clears the selection and quick overrides, invalidates pending
   paint revisions and restores the original slot. Browse remains open.
7. Close, Back, area/Actor changes and leaving Browse cancel the transaction.
   Navigation completes only after required restoration succeeds. These actions
   cannot silently drop a rollback obligation when an Actor unloads.
8. Changing filters retains the selected tattoo and names it in the footer
   until Undo or another click, even when filtered out.

The selected tattoo is the only visual preview and Save candidate. User controls may be
edited while runtime work is pending; pending work coalesces to the newest draft.

## Compact appearance controls

Use one fixed footer below the scrolling grid, keeping controls reachable without
scrolling the catalog. Display the selected tattoo name, Actor/area/slot context,
Ink, Glow, Glossy, Save Tattoo, Undo, and a compact status/retry message.

- **Ink:** RGB color picker. Existing alpha is preserved; detailed alpha remains
  available in Edit Appearance.
- **Glow:** RGB color picker. Black disables glow. Preserve pack glowTexture and
  emissive multiplier, including zero and values above one.
- **Glossy:** quick toggle controlling an explicit glossiness/specular pair. With
  no user override, preserve both pack values exactly. Builder proposal: On keeps
  positive pack values, using 5 only for a zero component; Off sets both to zero.
  Restore the remembered enabled pair when toggled back on. This proposal uses
  the reference's quick strength as a fallback, not a replacement for pack data.

Quick overrides are session-local and field-specific. On a new candidate, merge
its pack defaults with only fields the user explicitly changed. Never clear bump,
glowTexture, alpha or emissive metadata as a side effect of quick glossy/ink edits.
Switching candidates preserves explicit quick overrides; Undo clears them.
Candidate-specific enabled glossy values must not leak from another pack.

Statuses distinguish Previewing, Selected — not saved, Updating preview, Restoring,
Saving and errors. Save requires a pin and a successfully applied latest pinned
revision; a pending Save intent flushes/waits for that revision rather than saving
an old paint. External or locked slots cannot start catalog preview.

## Runtime architecture and authority

Preserve the existing route:

Native adapter -> deterministic workflow model -> scheduler/runtime coordinator
-> SlaveTatsService -> ITattooRuntime -> SlaveTatsRuntime -> runtime visual backend.

Only the runtime resolves Actor handles, reads JContainers, calls SlaveTatsNG or
changes a visual overlay. The UI never calls NiOverride or tattoo APIs directly.
SlaveTatsNG remains authoritative for ownership, identities, slot allocation and
persistent tattoo state. The optional visual backend may change only the selected
managed/empty slot's temporary visual; it must not create authoritative tattoo
records or change external overlays.

Add a typed catalog preview session contract: begin/capture, preview latest,
restore, commit and synchronization-only retry. Session tokens are runtime-owned;
workflow state contains copied identities/values, never engine pointers or raw
JContainer handles. Revalidate loaded Actor, exact area/slot, authority and original
membership before every operation. Revalidate the pinned candidate at commit.
For an occupied slot, compare the full authoritative tattoo snapshot captured at
session start, including material paths and appearance values; reject changes
even when the runtime handle and section/name remain the same. Reject managed
slots without a valid tattoo handle and empty slots that still carry a tattoo.
Save must also confirm that the captured visual target still resolves to the
same Actor root, geometry, and material; a rebuilt material is a preview
conflict and cannot trigger an authoritative tattoo mutation.
Runtime handles invalidated by mutation must be replaced by fresh readback.

Only one preview/restore/commit operation may run at a time. Required restoration
and Save are barriers, never superseded by selection. Stale completion rejection must
not discard physical cleanup for an old Actor. Actor switching waits for cleanup;
there is no implicit Player fallback. Unexpected foreign/external slot changes
stop further writes and expose an actionable conflict instead of overwriting them.

## Preview persistence: prerequisite, not an assumption

The bundled SlaveTatsNG interface has no catalog visual-preview entrypoint. Its
existing add/update calls modify authoritative records. Repeating real apply and
remove operations during browsing is not an acceptable preview implementation.

In-game trace on 2026-10-17 showed a successful nonpersistent BellyHeart2 paint
while UI hover remained unchanged for more than seven seconds, although the
Actor visual reverted to its original Body Cum tattoo after about five seconds.
While a clicked selection remains active, the runtime checks the material every
500 milliseconds and reasserts visual-only SKEE properties only when they differ.
The earlier unconditional two-second repaint did not prevent visible returns of
the original tattoo during in-game testing on 2026-10-18. The refresh
uses the existing Actor/slot validation and paint scheduler, stops on Undo,
Close, Save, failure, or world invalidation, and never repeats an authoritative
SlaveTatsNG mutation.

The reference paints without tattoo-record writes, but passes persist=true to
NiOverride; this is not evidence that the preview is excluded from save/co-save.
See the accompanying primary-source research note.

Before enabling live catalog preview, establish a nonpersistent visual operation
through a supported runtime interface, behind the boundary above. Prove its
storage/serialization behavior from upstream implementation and integration tests.
The concrete candidate is RaceMenu's native SetNodeProperty/GetNodeProperty:
upstream SetNodeProperty changes the loaded shader without writing nodeData,
whereas native AddNodeOverride always writes that persistent registry. Use a
runtime-owned adapter for the verified installed interface; do not call native
AddNodeOverride or copy Pzx's persistent painter. Verify exact property capture,
texture lifetime, deferred task cancellation/ordering and missing overlay nodes
before enabling the adapter. The existing browser/apply workflow remains usable
when this optional visual capability is unavailable, with explicit status.
Do not infer safety from a parameter name or successful Undo alone. If the runtime
cannot provide that capability, keep this feature unavailable and report the
missing capability; do not silently substitute persistent preview writes.

Required save/load proof covers normal saves, quicksave/autosave while Browse is
open, plugin co-save serialization, and reload while an unsaved visual is active.
Persistent tattoo records and durable overrides must represent the original slot
until Save succeeds. On load/new game, invalidate old session work; it must never
restore old-save data into the new world. Actor unload/3D rebuild needs an exact
Actor-scoped cleanup obligation and retry or authoritative re-sync strategy.
No recovery may write to a different Actor or suppress a real restore failure.

## Save and cancel contracts

Save captures the pin and its full effective appearance by value, invalidates
pending selection paints, and waits for in-flight work. Perform one authoritative
apply/replace with all quick values in the service/runtime operation, then
synchronize and read back. Current ApplyTattooRequest transports only color/alpha;
Glow/Glossy must be added to the typed commit path, not saved by an unrelated UI
follow-up mutation. Untouched advanced fields retain pack defaults.

After an authoritative mutation succeeds, a synchronization failure is a
synchronization-only retry; do not repeat the apply/replace. No late preview may
paint over the committed result. Save is complete only after synchronization and
readback confirm the requested identity/appearance. History updates once on success.

Undo/Close restores the exact original visual, including an empty slot, texture,
alpha, glow/emission, gloss/specular and bump state. Restore failure stays visible
and retryable. Navigation cannot complete while a required restore is unresolved.
An untouched session, or one whose original appearance was already restored,
can close without resolving an old material or slot snapshot; it performs no
write. A session with a pending visual restore still requires the original
Actor, slot, and material to be stable before any restore write.
SKEE scalar snapshots and paint values with NaN or infinity are rejected before
the first write: they cannot pass exact restore readback comparison.
A successful commit followed by sync failure cannot be treated as an unsaved
preview and rolled back by a stale Close/selection ticket.

After Save, return to Current Slots with fresh readback and the usual detailed
Edit Appearance action available. Do not open the detailed editor automatically.
If the nonpersistent preview session cannot begin, Browse stays open, shows the
error, and offers an explicit Retry Preview action. Failed begins do not retry
continuously in the background.

## Verification and implementation sequence

The visual-only SKEE write resolves pack-relative tattoo, bump and glow paths
under `textures\\actors\\character\\slavetats\\` before changing the loaded
material. The authoritative SlaveTatsNG Save request retains the pack-relative
paths. A hovered catalog card has a light outline; a clicked, pinned card has a
stronger outline. A green dot marks a tattoo already in use, independently of
hover and selection.

1. Prove nonpersistent preview/save/load behavior at the runtime boundary first.
2. Add typed session lifecycle and exact-slot validation/rollback tests.
3. Add workflow tests for selection/draft/Save/Undo, revision and target barriers.
4. Add full-appearance commit transport/readback without duplicate mutations.
5. Add footer/selection presentation with bounded visible-thumbnail work unchanged.
6. Run full Debug/Release builds and CTest, diff checks, focused review, then
   obtain in-game feedback for Player and nearby NPC targets.

Required cases: empty versus occupied slot; external/locked rejection; hover without
painting; click A then B while paint is pending; hover B then Save selected A; quick-control
coalescing; click retaining preview values; filtering away a pin; Undo/Close during
in-flight work; unloaded Actor; stale target/session; restore failure; foreign slot
changes; glow-to-non-glow; custom glow/bump paths; zero/>1 emissive; successful commit
plus sync-only retry; no history for unsaved selection; save/co-save/load isolation.

## Scope and decisions still requiring validation

Camera work and side composition stay separate. No global appearance persistence,
new texture-path editor, favorite/lock controls or multi-slot preview is included.
The quick Glossy fallback and post-Save destination are implementation defaults
within the approved interaction. Nonpersistent backend feasibility was
established for the installed runtime. State/commit foundations and live
painting/UI integration are implemented and deployed for testing. The normal
save/load cycle passed in game. The user subsequently confirmed that the
requested save/co-save and NPC Preview test groups also had no problems. The
acceptance is based on the user's in-game report, not an independent co-save
binary inspection by this project.

Builder check: a fresh reviewer found no important unanswered planning questions
in the interaction/runtime contract. The user-reported save/co-save pass is
the recorded product acceptance for this milestone.

## References

- docs/superpowers/research/2026-10-16-catalog-hover-preview-references.md
- docs/superpowers/specs/2026-09-20-native-live-actor-preview-design.md
- docs/superpowers/specs/2026-10-15-scrollable-tattoo-grid-design.md


### Material safety refinement

The runtime capability research establishes that tint key7 can convert material
type and shader flags; emissive key0 can dereference null storage. Before any
property API call, inspect exact loaded geometry, shader/material type and
emissive storage. A scalar snapshot backend must reject material conversion and
missing storage; broader support needs exact structural material/flag restoration
with independent tests. The current node probe reads these facts without calling
SKEE properties or creating missing nodes. A version2Detected log is only an ABI
probe result and does not enable preview or establish save/load safety.
