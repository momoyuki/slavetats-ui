# External Slot Thumbnail Implementation

User approved read-only External thumbnail display on 2026-11-03. External
slots remain externally owned and reject all writes, previews and removals.

## Evidence and constraints

- `SlaveTatsRuntime::querySlotsForActor` receives only External slot indices;
  its model currently supplies no image path for them.
- SKEE's existing node property binding can read a diffuse path, but its use on
  externally owned slots has not been verified in game. An absent SKEE value is
  not proof that an external overlay has no texture.
- Existing thumbnail resolution is restricted to the SlaveTats texture root;
  external resources may live elsewhere in Data/Textures or archives.
- Read only on the existing scheduled runtime path, scope every result to the
  resolved Actor and slot, and discard stale results using existing generations.

## Revised execution gates (reviewed 2026-11-03)

Execute the gates below in order; the older sequence below is subordinate to
these gates. User approved development, not a commit, push or new deployment.

### Gate A: Prove one-slot texture read before product integration

- On the scheduled game thread, resolve an explicit loaded Actor, area and slot
  to its Overlay geometry. Copy only the diffuse path into owned string storage;
  do not retain actor, geometry, material or texture-set pointers.
- Compare SKEE GetNodeProperty texture key9/index0 with the resolved lighting
  material texture-set diffuse path. Inspect the installed CommonLib declarations
  for access and synchronization requirements before reading material state.
  Neither source has yet been proven for External overlays. Do not infer that
  a successful SKEE read alone identifies the currently visible material.
- Use an external slot on Player first, then an explicit loaded NPC. Record node,
  read success/failure and matching resource identity. Never invoke setters,
  synchronize overlays or start a Preview transaction for this experiment.
- Missing node/material/path or conflicting results must produce unavailable
  metadata. Do not guess a tattoo name or silently substitute Player.
- Gate acceptance requires real runtime evidence for at least one External
  overlay. Automated binding tests prove safety/arguments, not visual truth.
  If game evidence is unavailable, leave this gate pending and report it;
  independent path-validation tests may proceed but do not ship the UI as proven.

### Gate B: One resource identity for lookup and cache invalidation

- Add optional external image metadata separate from TattooEntry/runtimeHandle.
  Keep SlotOccupancy::external and existing mutation rejection unchanged.
- Define resource kind plus canonical Data-relative texture path. Legacy catalog
  paths keep their SlaveTats-relative meaning; do not reinterpret them globally.
- Normalize valid texture DDS paths beneath Data/Textures; reject absolute, UNC,
  drive-qualified, traversal, embedded NUL and non-texture resource paths. Check
  loose-file containment and use the existing resource reader for archive access.
- Carry the same identity through controller lookup, GPU cache, disk entry key,
  file metadata stamp and archive fallback. The existing disk cache assumes a
  SlaveTats root; changing only TextureResolver will leave stale/missing entries.
- Test same relative filename in different roots, changed loose file, missing
  loose file with archive fallback, malformed paths and existing catalog paths.
- Preserve 192px output limit, GPU budget and configured disk budget. No full
  catalog prebuild expansion for External is required: load visible slots only.

### Gate C: Snapshot integration and read-only presentation

- Collect external metadata when opening/refreshing Current Slots or switching
  Actor/Area. Do not traverse scene nodes or load files every render frame.
- Use the existing runtime/scheduler/service boundaries and Target Generation.
  Discard stale completions; never fall back to Player. A failed image read must
  not fail the whole slot snapshot or change the slot's External occupancy.
- Render ready images in Current Slots using the shared thumbnail pipeline;
  show External with unavailable/loading status otherwise. Keep write actions
  disabled. This is a source-image thumbnail, not a screenshot of composited
  skin, tint, alpha or emission; do not promise a rendered appearance match.
- Test target switches, unloaded actors, absent SKEE/material, metadata failure,
  resource failure and External Apply/Replace/Edit/Remove/Preview rejection.
- Run full Debug and Release builds/CTest, git diff --check and scoped review.
  Report numerical results. Deployment and in-game acceptance are separate.

## Existing implementation outline

1. Add a separate optional external thumbnail resource identity to slot state;
   never synthesize a SlaveTats tattoo or handle for an external slot.
2. Test runtime lookup using explicit Actor, area and slot, including unloaded
   actor, unsupported SKEE, missing texture and stale target. Read the diffuse
   property without invoking setters; no mutation on any path.
3. Resolve safe normalized Data/Textures DDS paths through the existing loose and
   archive boundary, rejecting traversal, absolute paths and other resource
   roots; isolate resource identities and cache keys from SlaveTats paths.
4. Render an available thumbnail in Current Slots, otherwise show External with
   unavailable status. Keep the slot non-interactive for mutation.
5. Test external slot immutability, absent/invalid paths, both resource sources,
   target switches, cache separation and existing managed thumbnails. Run full
   Debug and Release builds/tests and inspect the scoped diff.
6. Obtain deployment approval separately, then verify Player and NPC external
   overlays from representative mods in game, including missing textures.

## Open technical question

Verify whether GetNodeProperty reads the actual visible texture for external
overlays, rather than only a registered override. Do not present an unverified
path as the active visual; fall back to External when the read is unavailable.

## Implementation and remaining acceptance (2026-11-06)

The Current Slots snapshot now carries separately validated, optional External
source-image identities. Matching SKEE property and material paths are required;
missing or conflicting metadata leaves the slot External and unavailable.
Visible slot requests use kind-scoped Data-relative identities, source revisions,
and the existing bounded GPU and disk caches. Snapshot epochs discard stale
completions. External actions remain read-only.

On 2026-11-06, the user approved deployment and the installed DLL was verified
with SHA-256 `595C451DAD65CC43FA30C02BA220F20944D650F1284638F6519E5C7F13DD8D16`.
The user accepted Player in-game External thumbnail display; screenshots show
Body slots 0, 1, and 16–22 across both pages. Earlier Player probe evidence covers
nine Body nodes for actor 0x14. The user explicitly waived NPC verification; NPC
behavior remains unverified in game, and its implementation remains unchanged.
