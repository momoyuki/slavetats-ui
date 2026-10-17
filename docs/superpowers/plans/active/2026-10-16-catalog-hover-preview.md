# Catalog Hover Preview Implementation Plan

> 2026-10-17 interaction update: Hover now changes only the card outline and
> tooltip. Clicking a card selects and starts visual preview. While a clicked
> selection remains active, the runtime may reassert its visual paint every two
> seconds because in-game testing showed the visual can revert after about five
> seconds. The earlier hover-specific tasks below document the original design;
> the current behavior is defined in the linked spec.

> 2026-10-18 stability follow-up: The workflow model now stores only the clicked
> selection. Its Hover/Pin paint API and transient hover state were removed.
> Selection, quick appearance, rollback, stale completion, Save, and sync-only
> retry tests cover the revised state machine. Debug and Release suites pass
> 55/55, and the tested DLL was deployed with a backup. In-game acceptance is
> pending.

> 2026-10-18 authority guard: A failing runtime test showed that changing a
> managed tattoo's texture while retaining its handle and name still allowed
> preview paint. The runtime now compares the complete captured tattoo snapshot
> before paint, restore, and first Save, and rejects inconsistent empty/managed
> slot snapshots. Tests also cover external appearance changes and zero handles.
> Debug and Release suites passed 55/55; the tested DLL was deployed with the
> prior DLL backed up. In-game acceptance remains pending.

> **For agentic workers:** Use executing-plans inline; a fresh reviewer checks the final change.

**Goal:** Click-to-preview catalog tattoos, retain exact quick appearance, Save once, and cancel without persistent preview writes.
**Architecture:** Adapter intent -> deterministic model -> serialized runtime -> service -> SlaveTatsRuntime -> optional nonpersistent visual backend.
**Tech Stack:** Existing C++23, CommonLibSSE-NG, CTest; no new package dependency.
**Spec:** docs/superpowers/specs/2026-10-16-catalog-hover-preview-design.md

## Global constraints

SlaveTatsNG remains authoritative. External and locked slots remain read-only.
Actor/session/slot validation precedes every paint and commit. No implicit Player fallback.
No persistent AddNodeOverride calls for previewing. No commit/push without separate approval.
Approval in chat covers implementation; execute inline in the existing worktree.
The quick glossy fallback is 5 per zero component; untouched pack fields survive.

## Review focus

- Save/co-save during unsaved selection must retain original records and durable overrides.
- Missing nodes and unloaded Actors must preserve cleanup obligations.
- Hover B cannot replace pinned A at Save.
- A successful mutation plus failed sync must not be applied a second time.
- New/load game must invalidate old-world work without restoring into the new world.

### Task 1: Prove installed nonpersistent backend capability

Files: docs/superpowers/research/2026-10-16-catalog-preview-runtime-capability.md;
existing hover-preview research/spec.
- [x] Inspect installed SKEE metadata and exported/native interface against primary source.
- [x] Confirm installed runtime ABI and actual node/material availability in game (2026-10-17).
- [x] Establish exact synchronous shader-property capture/set/restore API, texture ownership,
  empty overlay behavior and durable registry exclusion. Record unavailable capabilities.
- [ ] Require runtime save/co-save QA before enabling a backend; never emulate capability
  with persistent writes. If no supported interface exists, complete independent tested
  foundations below and leave live painting unavailable with a concrete blocker.

### Task 2: Full appearance commit transport

Files: src/core/TattooModels.h, src/core/SlaveTatsService.cpp,
src/runtime/SlaveTatsRuntime.cpp; tests/core/SlaveTatsServiceTests.cpp;
tests/runtime/SlaveTatsRuntimeAppearanceTests.cpp.
- [x] Add failing tests for optional advanced apply overrides: no overrides preserves pack
  defaults; invalid glow/nonfinite or negative strengths reject; zero and >1 emission pass.
- [x] Add ApplyTattooAppearanceOverrides and optional field at end of ApplyTattooRequest.
  Service validates overrides only for real mutation; synchronization-only bypasses them.
- [x] Use a runtime-owned template copy for apply overrides; never edit the shared template.
  Transport all effective fields in one authoritative add, preserving sync-only retry.
- [x] Run relevant service/runtime tests before moving on.

### Task 3: Deterministic hover/pin/quick state

Files: src/native/CatalogPreviewModel.{h,cpp}, tests/native/CatalogPreviewModelTests.cpp,
CMakeLists.txt.
- [x] Tests first: hover priority, exit-to-pin/original, pin pending candidate, overrides
  retained, candidate-specific glossy pair, filter-away hover, Undo revision barrier.
- [x] Implement copied catalog candidates, field-specific overrides, pinned identity and
  revision tracking. Save captures pinned effective values, never hovered candidate.
- [x] Tests first for stale completion, busy latest-wins, failed restore, cancellation
  barrier, target/session scope, commit+sync failure and no history for hover.
- [ ] Keep runtime tokens separate from UI/catalog identity and route all writes through service.

### Task 4: Runtime and UI integration (requires Task 1 capability)

Files: src/core/ITattooRuntime.h, src/core/SlaveTatsService.{h,cpp},
src/runtime/SlaveTatsRuntime.{h,cpp}, src/native/NativeSlotWorkflowModel.{h,cpp},
src/native/NativeSlotWorkflowRuntime.cpp, src/native/OfficialMenuFrameworkAdapter.cpp,
src/main.cpp and corresponding tests.
- [ ] Add typed begin/paint/restore/commit contracts with backend capability reporting.
- [ ] Revalidate actor, area, slot, locked/external state and original membership on each operation.
- [ ] Serialize latest paint with non-supersedable Save/restore barriers; tests cover
  rapid hover and late old-Actor completion after cancel, unload and game-load invalidation.
- [ ] Add fixed footer Ink/Glow/Glossy, Save Tattoo/Undo/status; hover does not navigate.
  Pin enables Save after latest pinned revision succeeds; preserve existing fallback workflow.
- [ ] Native backend tests prove no authoritative/registry writes until commit, exact restoration
  including missing/empty nodes and full advanced materials.

### Task 5: Verification and acceptance

- [ ] Fresh review, full Debug/Release builds and CTest; git diff --check.
- [ ] Deploy only a verified candidate with game stopped, backup and hash.
- [ ] In-game save/quicksave/autosave/co-save/reload proof before enabling live preview.
- [ ] Record actual completion and remaining capability gates without marking UI ready prematurely.

## Ledger

2026-10-16: User explicitly approved implementation after distinguishing completed camera
work from unimplemented catalog preview. Camera controls passed user in-game testing.
No catalog preview product code existed at task start. Existing worktree reused.


### Foundation and diagnostic checkpoint

Ruling: Keep live painting and its UI integration disabled pending installed ABI
and material evidence. Upstream key7 can replace material/flags and key0 can
dereference missing emissive storage, so a scalar-only generic writer is unsafe.
The deployed candidate only exchanges the public interface/version and reads
scene geometry/material types on scheduled slot queries. No SKEE shader getter
or setter is called. Naming probes use upstream's normal Ovl formats and report
only once per Actor/area. User is not available for in-game testing yet; do not
repeat the request or infer results from installed metadata.

Implemented independent foundations: field-specific advanced apply transport,
copy-only template preparation, hover/pin/quick draft state, paint/restore revision
barriers, frozen Save intent and synchronization-only commit retry. Native UI and
real visual transaction bindings remain pending Task 1. Existing legacy Apply
remains on its original branch. New runtime tests exercise the copied-handle add,
unchanged original template, zero/empty metadata and no duplicate add on sync retry.

Fresh source review found no important defects; requested test gaps were covered.
Diagnostic candidate was built/tested in Debug and Release (50/50 each) and deployed
with game stopped, backup SlaveTatsUI.dll.backup-20261016-201112.
SHA256 1E66672C68B1B9E58BF5C0CD6093D264E853C5921A5CD8150ED55CCA10386388;
3,976,192 bytes. Further offline commit-model tests do not enable live preview.
No commit, push or merge. Full hover-preview acceptance is not complete.

### Final offline verification after commit barriers

Debug and Release builds succeeded. Running complete CTest suites sequentially
passed 51/51 each. The new commit model tests cover pinned Save waiting for paint,
foreign/old-session commit completion, pre-write failure cancellation, and repeated
synchronization-only failure retaining the no-rollback/no-duplicate-add barrier.
The final source review found no important model defect; its two additional
regression requests were implemented and pass.

Test harness limitation: running Debug and Release CTest simultaneously reproduced
failures in AppearancePresetStoreTests and TattooLoadoutStoreTests. Both fixtures
use fixed shared temporary directory names and remove/recreate those directories.
Sequential full runs passed without changing assertions or product code. Run these
configuration suites sequentially until their fixture isolation is improved in a
separate testing task. No tests were skipped or disabled.

Final diff check passed with the pre-existing CRLF advisory only. No separate
C++ linter/typechecker command is configured; C++ compilation and /W4 /WX focused
model checks were used. Later model changes affect test executables only: the
Release plugin SHA256 still matches deployed diagnostic candidate
1E66672C68B1B9E58BF5C0CD6093D264E853C5921A5CD8150ED55CCA10386388.
Task 1 runtime evidence, Task 4 live backend/UI integration and save/co-save/load
acceptance remain pending. The user explicitly said testing is not convenient;
no approval is required again and no in-game results may be assumed.

### In-game diagnostic evidence — 2026-10-17

User completed Player/NPC area-tab diagnostic run and reported success. The live
log at D:/Documents/My Games/Skyrim Special Edition/SKSE/SlaveTatsUI.log records:

- 09:20:15: SKEE Override version 2 detected (probe result 4).
- Player 00000014 Body: 10 eligible scalar candidates (9 empty, 1 managed),
  no missing eligible nodes, unsupported shaders, tint conversions or missing
  emission storage. Other configured slots were excluded by locked/external rules.
- Player Face/Hands/Feet: 20 scalar candidates per area, all empty; same zero
  structural blockers.
- NPC 000198B4 Body/Feet: all 20 default-format node probes missing per area.
- NPC 000198B4 Face/Hands: 20 scalar candidates per area, all empty, no structural
  blockers.

The installed ABI and actual Player geometry gate is now supported by runtime
evidence. This does NOT establish property round-trip, texture replacement,
restoration or save/co-save/load safety. Missing NPC Body/Feet nodes must return
capability-unavailable; do not create persistent overlays during hover. The log
only probed upstream's normal node names, so missing means those exact nodes were
not found, not proof that the actor has no geometry under any alternative name.
Next implementation: bind the verified v2 property interface for eligible existing
nodes, add exact typed capture/readback/restore and actor/slot validation, then
connect the existing deterministic model/footer and run visual/save acceptance.
The user's earlier unavailability is superseded by this completed diagnostic run.


### User correction — NPC Body and Feet, 2026-10-17

The user confirmed that applying a Body tattoo to the NPC works. Feet could not
be tested because no Feet tattoo is available in the catalog. Record Body apply
as user-confirmed success and Feet as untested due to missing test content; do
not classify either area as generally unsupported based on the earlier probe.

Source inspection shows CatalogPreviewProbe caches reported actor/area pairs and
logs only the first query. It cannot observe nodes that appear after an ordinary
SlaveTats apply/synchronize or 3D rebuild. Timing or lookup assumptions can explain
the mismatch; neither mechanism is confirmed by this log. A live preview backend
must resolve and revalidate current geometry for the exact requested slot at
execution time, never retain a negative capability decision from this diagnostic.
Body apply success also does not itself establish temporary preview rollback or
save/co-save safety. No additional test of Feet is requested until content exists.

### Typed nonpersistent backend checkpoint — 2026-10-17

Implemented the runtime-only path without exposing it to the UI yet:

- independently declared the complete public SKEE Override v2 virtual layout and
  bind it only after the exchange map returns the supported version;
- resolve the exact Actor/area/slot node on each operation and require the same
  Actor, root, geometry and material throughout a preview session;
- require an existing compatible tint material and non-null emission storage
  before any property getter or setter call;
- capture, write, read back and exactly restore diffuse, normal, glow texture,
  tint, alpha, glow color, emissive multiplier, glossiness and specular strength;
- added typed Service/ITattooRuntime begin, paint and restore contracts;
- re-query SlaveTats slot authority before every operation and reject external,
  locked, foreign or changed slots without Player fallback;
- invalidate any runtime preview session on pre-load/new-game boundaries.

New focused tests cover typed property completeness, exact restoration, failed
readback cleanup obligation, material replacement, stale Actor/generation/slot,
external/locked rejection and authoritative slot changes. The backend is bound
at runtime but no adapter event currently starts a session, so this checkpoint
does not enable hover preview or require an in-game visual test yet. UI wiring,
commit orchestration and save/co-save/reload acceptance remain Task 4/5 work.

Fresh Debug and Release builds completed after this checkpoint. Both full CTest
suites passed sequentially: 55/55 Debug and 55/55 Release. No plugin deployment
was performed because the adapter does not yet start preview sessions and this
candidate would have no new user-visible hover behavior to test.

### Hover-exit restoration checkpoint — 2026-10-17

The typed paint request now accepts an empty candidate to restore the captured
visual while retaining the exact Actor/area/slot session. This is needed for
pointer exit followed by hovering another catalog tile. Closing still uses the
separate restore operation, which ends the session. A clean session performs no
unnecessary material write. Service and runtime tests cover hover A, clear,
hover B, close and rejection of old session tokens. The adapter is still not
wired, so this checkpoint is not a deployable hover-preview feature. The model
also waits for a runtime end acknowledgement when Close follows an untouched
session, preventing a leaked session token. Debug and Release builds completed;
both complete test suites passed 55/55 sequentially. `git diff --check` passed
with the existing CRLF advisory.

### Integrated preview checkpoint — 2026-10-17

The catalog now starts an exact Actor/area/slot visual session, previews a card
on hover, pins it on click, and offers Ink, Glow, Glossy, Save, Undo and retry
controls. Save carries the selected appearance through the typed service and
runtime commit path, then refreshes Current Slots and records Recently Used only
after confirmed success. Cancel/Close waits for restoration, including when a
paint was already queued when the menu closed. The active preview blocks target
and slot navigation until the visual session ends. These changes are in the
worktree only; they have not been deployed or accepted in game.

Debug and Release builds and their complete 55-test suites passed after the
initial integration. In-game Player/NPC hover, rollback, Save, co-save/reload
and synchronization-only retry acceptance remain open. The installed SKEE
property ABI and material behavior have not yet been visually validated with
live hover.

### Purple texture correction — 2026-10-17

The first in-game hover report showed the Actor's Body overlay as a purple
missing texture. A tested catalog entry stores a pack-relative texture path,
while the visual-only SKEE writer passed it directly to the shader. The preview
writer now validates and prefixes diffuse, bump and glow paths with the resource
root used by the texture resolver. The Save request still uses the original
pack-relative values. UI cards now show a green in-use dot and distinct hover
and pinned borders. Both complete Debug and Release suites pass (55/55 each).
The corrected build was deployed to MO2 with the prior DLL backed up and its
hash verified. The user confirmed the preview now passes the previously failing
visual case. The green in-use dot was not checked in that run. Save, Undo, NPC,
and save/co-save/reload acceptance remain open.

### Independent Save/Undo regression checkpoint — 2026-10-17

Added a workflow test that Cancel after hover creates no Recently Used record,
while confirmed Save records the exact pinned tattoo once. This test does not
substitute for in-game Save, Undo or co-save/reload acceptance.
The same test now also closes while session creation is queued and confirms the
newly opened visual session restores itself without a hover or history write.

### In-use dot draw-order correction — 2026-10-17

In-game screenshots showed the in-use dot on the same catalog entry when the
pointer hovered an earlier entry, but not when it hovered a later entry. The
in-use lookup only reads the current slot snapshot and does not depend on
hover. The dot was drawn on the parent table draw list after the thumbnail
child, where subsequent table cells could obscure it. Draw the dot inside its
own thumbnail child after the image and badges so it shares the thumbnail's
draw order and clipping. Debug and Release builds and full 55-test suites pass.
The corrected Release DLL was deployed to the dedicated MO2 mod after backing
up the previous DLL, and the deployed SHA-256 matches the build. In-game
confirmation of the dot across neighboring hover positions remains open.

### Stale material Save guard — 2026-10-17

While in-game camera and gallery acceptance is pending, a runtime regression
test showed that Save could call the authoritative apply after the Actor's
previewed material had been rebuilt. The visual session now re-resolves its
captured Actor/root/geometry/material before a first-time Save. A changed
target returns a preview conflict before any SlaveTats mutation. The
synchronization-only retry path remains unaffected because its mutation has
already completed. The new test failed before the guard and passed afterward;
full Debug and Release suites pass 55/55. This change is in the worktree only
and is not part of the currently deployed in-game test DLL.
