# Native Slot-First Workflow Design

## Context

The native SlaveTatsUI browser can display six current-page thumbnails with search, filters, pagination, and a bounded D3D11 cache. It remains read-only. PrismaUI still owns actor slot inspection and tattoo mutations through `Bridge`, where Skyrim, SlaveTatsNG, JContainers, JSON serialization, and presentation behavior are coupled.

The next migration slice makes the Player's current overlay slots the native entry point. A user selects an empty or SlaveTats-owned slot, chooses a tattoo from the existing catalog picker, reviews the target, and explicitly applies it. The design intentionally keeps presentation state separate from slot queries and mutations so the UX can be revised after in-game testing without rewriting the shared service.

## Goals

- Open the native window on the Player's current tattoo slots.
- Support BODY, FACE, HANDS, and FEET areas.
- Show six large slot cards per page in the established two-column by three-row layout.
- Display the current texture thumbnail for every SlaveTats-owned slot.
- Distinguish empty, SlaveTats-owned, and external overlay slots.
- Open the existing catalog as a target-aware tattoo picker after selecting a mutable slot.
- Preserve picker search, filters, and page state across slot selection and apply operations.
- Require an explicit Apply confirmation after choosing a tattoo.
- Add and replace tattoos through one shared core service used by Native UI and PrismaUI.
- Keep all Skyrim, SlaveTatsNG, JContainers, filesystem, and GPU work outside ImGui render callbacks.
- Preserve F1 SKSE Menu Framework and F8 PrismaUI behavior throughout the migration.

## Non-Goals

- No NPC actor selector in this slice.
- No remove-only action, color picker, alpha editor, lock editor, or synchronize button.
- No immediate apply when a catalog thumbnail is clicked.
- No mutation of external overlay slots.
- No automatic polling of slot state every frame or on a timer.
- No PrismaUI retirement or Prisma-specific thumbnail transport removal.
- No off-page slot or catalog thumbnail loading.

## Approved User Flow

1. Open `SlaveTatsUI/Tattoo Browser` through SKSE Menu Framework.
2. The window opens on `Current Tattoos` for the Player, BODY, page one.
3. Select BODY, FACE, HANDS, or FEET. Each area retains its own slot page.
4. View six slot cards per page:
   - a SlaveTats-owned slot shows its current thumbnail and tattoo name;
   - an empty slot shows an Add affordance;
   - an external slot shows a locked, read-only treatment.
5. Select an empty slot to add a tattoo or a SlaveTats-owned slot to replace it.
6. The catalog picker opens and identifies the target, for example `Player / BODY / Slot 2`.
7. Search, filter, and paginate the existing two-column by three-row catalog.
8. Select a tattoo thumbnail to open a preview. No mutation occurs yet.
9. Confirm with `Apply to Slot N`, or cancel/back without changing the actor.
10. On success, return to Current Tattoos and refresh only the selected area.
11. On failure, retain the preview and target context, show the error, and permit retry or cancel.

The initial apply uses domain `default`, white `0xFFFFFF`, and alpha `1.0F`. The parsed catalog definition does not carry domain, color, or alpha fields, and the current Prisma catalog also queries domain `default`. Other domains and color/alpha controls are deferred to later slices.

## Architecture

```text
OfficialMenuFrameworkAdapter
  ImGui intent + presentation only
              |
              v
NativeSlotWorkflowModel
  CurrentSlots -> Picker -> Preview -> Applying
       |             |
       |             +---- NativeCatalogBrowserModel
       |
       +------------------ Native thumbnail presentation
              |
              v
NativeSlotWorkflowRuntime
  game-thread scheduling + completion publication
              |
              v
SlaveTatsService
  validation + shared query/apply contract
              |
              v
ITattooRuntime / SlaveTatsRuntime
  RE::Actor + SlaveTatsNG + JContainers + skee64.ini
```

### Shared Core Models

Core defines transport-independent values:

- `TattooArea`: BODY, FACE, HANDS, or FEET;
- `SlotOccupancy`: empty, SlaveTats, or external;
- `TattooSlot`: zero-based slot number, occupancy, and optional copied tattoo metadata;
- `TattooSlots`: area, configured slot count, and the complete ordered slot collection;
- `ApplyTattooRequest`: actor form ID, area, slot, domain, section, name, color, and alpha;
- `ApplyTattooResult`: target identity and the applied tattoo metadata required for refresh/presentation.

External runtime handles and JContainer objects never cross into core models. Any handle retained for later editing is copied as a numeric runtime identifier and treated as session-local.

Slot pagination is presentation state rather than a core service concern. Prisma receives the complete collection, while `NativeSlotWorkflowModel` derives a six-entry page without copying runtime objects.

### Shared Service Boundary

`ITattooRuntime` gains typed slot query and apply operations. `SlaveTatsService` performs availability validation before forwarding to the runtime. Both calls return `std::expected` with stable service error codes.

The runtime adapter owns environment-specific behavior:

- resolve the actor form ID to `RE::Actor`;
- normalize and validate the requested area;
- read and cache the configured overlay count from `skee64.ini`;
- query external overlay slots;
- query each SlaveTats slot and copy JContainer fields into values;
- resolve the selected tattoo within its domain;
- recheck external occupancy immediately before applying so a stale UI snapshot cannot overwrite a slot claimed by another mod;
- call the existing SlaveTatsNG slot application API;
- mark the actor updated and synchronize after a successful mutation;
- clean every scoped JContainer pool on all return paths.

Replacing an occupied SlaveTats slot uses the existing SlaveTatsNG slot operation. The workflow does not pre-remove the current tattoo. If apply fails, the existing slot remains and the UI reports the failure.

### Prisma Compatibility

`Bridge::handleQuerySlots`, `handleQueryAllSlots`, and `handleApplyToSlot` delegate to `SlaveTatsService`. Prisma-specific JSON serialization remains in `Bridge` and retains its current payload shapes. This removes duplicated mutation logic without changing the F8 UI contract.

Other Bridge mutations remain unchanged until their own migration slices.

### Native Workflow Model

`NativeSlotWorkflowModel` owns only testable presentation state:

- current screen: slots, picker, preview, or applying;
- selected area and per-area page indices;
- current slot page and query status;
- target slot;
- selected catalog tattoo;
- apply status and user-facing error;
- retained `NativeCatalogBrowserModel` state.

Entering Slots for the first time requests Player/BODY once. Changing area requests that area only when it has no cached result. Refresh invalidates and requests the selected area. No method performs Skyrim work synchronously.

Back navigation is deterministic:

- Picker to Current Slots clears the target but preserves catalog state;
- Preview to Picker clears only the selected tattoo;
- Preview Cancel returns to Current Slots and clears the target and preview state;
- a successful Apply returns to Current Slots and refreshes the selected area;
- closing and reopening the native window preserves session state.

### Game-Thread Coordination

`NativeSlotWorkflowRuntime` schedules service calls through the SKSE task interface. It permits one slot query or mutation at a time and publishes completions back into the workflow model using a generation token. Results from an obsolete area, target, or closed workflow generation are discarded.

The adapter reads immutable/copyable presentation state. It never resolves actors, reads INI files, touches JContainers, calls SlaveTatsNG, or waits for a task.

### Thumbnail Reuse

The existing native thumbnail source and bounded D3D11 cache remain the only native thumbnail implementation. The presentation controller is generalized to accept the current visible texture paths rather than being tied only to a repository page.

- Current Slots requests thumbnails for the six visible slot cards.
- Picker requests thumbnails for the six visible catalog cards.
- Empty and external slots request no texture.
- Switching screens cancels stale visible requests but retains cache entries under the existing capacity and two-minute idle TTL.
- The legacy PrismaUI cache and transport remain independent.

## UI Behavior

### Current Slots

- Header: `Current Tattoos — Player` and Refresh.
- Area tabs: BODY, FACE, HANDS, FEET.
- Grid: two columns by three rows.
- Footer: Prev, one-based page input/label, Next, and Close.
- Empty card: Add affordance.
- SlaveTats card: current thumbnail, name tooltip, and selectable treatment.
- External card: locked label and disabled interaction.

BODY normally spans two pages with twelve slots. FACE, HANDS, and FEET normally fit on one page with three slots, but page counts derive from `skee64.ini` rather than fixed UI assumptions.

### Tattoo Picker

- Back control returns to Current Slots without mutation.
- Target context remains visible while browsing.
- Existing Filters toggle, two-by-three catalog grid, Area badge, hover name, and pagination are reused.
- Catalog filters and page do not reset when a tattoo is selected, canceled, or applied.

### Preview

- Show selected tattoo thumbnail and name.
- Show actor, area, and slot target.
- `Apply to Slot N` starts one mutation and becomes disabled while applying.
- Back returns to Picker without mutation and retains the selected slot's appearance state.
- Cancel returns to Current Slots without mutation.
- No color or alpha controls in this slice.

### Footer Placement

Every workflow footer is pinned to the bottom edge of the native window. Back
is the leftmost navigation action. Contextual actions occupy the middle region.
Cancel, when present, is immediately left of Close, and Close is always the
rightmost action.

## Error Handling

- Missing SlaveTatsNG or JContainers: show an unavailable state and keep Refresh enabled.
- Missing Player actor: show an actor-not-found error without entering Picker.
- Invalid area or slot: reject before runtime mutation.
- External target: reject in both the model and service boundary.
- Slot query failure: retain the prior successful page, mark it stale, and show Refresh.
- Apply failure: preserve Preview, selected tattoo, and target; permit retry or cancel.
- Synchronize failure after an applied mutation: report a partial-success error and refresh slots so the visible state reflects the runtime.
- Task or render exceptions: convert to stable errors; no exception crosses the SKSE Menu Framework callback boundary.

## Testing

### Core and Runtime Seams

- Service stops before runtime calls when SlaveTatsNG or JContainers is unavailable.
- Area and slot validation reject invalid requests.
- Runtime slot copies represent empty, SlaveTats, and external occupancy correctly.
- Overlay counts respect injected configuration values.
- JContainer pools are cleaned on success and failure.
- Apply preserves domain `default`, section, name, area, slot, color `0xFFFFFF`, and alpha `1.0F`.
- Apply failure does not issue synchronize or report success.
- Successful apply marks updated and synchronizes once.

### Workflow Model

- Initial entry requests Player/BODY once.
- Render-like repeated reads do not re-query.
- Area changes load only uncached areas.
- Per-area slot pages are preserved and clamped.
- External slots cannot become targets.
- Empty and SlaveTats slots enter Picker with the correct target.
- Picker state survives Preview, Cancel, Apply, and return to another slot.
- Thumbnail selection enters Preview without mutation.
- Duplicate Apply input is ignored while a mutation is active.
- Successful Apply returns to slots and refreshes the selected area.
- Failed Apply retains Preview and supports retry.
- Stale task generations cannot overwrite current state.

### Adapter and Integration

- Slot and picker grids contain at most six visible texture requests.
- Empty/external slots create no texture requests.
- BODY pagination and smaller-area pagination render without vertical scrolling.
- Prisma slot query and apply payloads retain their current JSON shapes.
- Debug and Release builds, complete CTest suites, diff checks, and secret scans pass.

## In-Game Acceptance

1. F1 opens SKSE Menu Framework and F8 still opens PrismaUI.
2. Native SlaveTatsUI opens on Player/BODY current slots.
3. BODY, FACE, HANDS, and FEET report the expected slot counts from the installed configuration.
4. SlaveTats thumbnails, empty slots, and external slots render correctly.
5. BODY pagination shows six large cards without vertical scrolling.
6. Selecting an empty slot opens Picker with the correct target.
7. Selecting an occupied SlaveTats slot prepares a replace operation.
8. External slots cannot be selected.
9. Cancel and Back never mutate the actor.
10. Apply changes the intended Player slot once and refreshes that area.
11. Picker filters and page remain unchanged after cancel or apply.
12. PrismaUI query/apply behavior remains functional after the shared-service migration.
13. Slot browsing and thumbnail loading do not introduce visible frame stutter.

## Reversibility

The workflow state machine, service boundary, and ImGui layout remain separate. In-game UX feedback may change the default area, grid labels, navigation placement, confirmation layout, or return behavior without changing the runtime query/apply contracts. PrismaUI remains available as the compatibility path until the full native workflow is validated.

## Implementation Slices

1. Add typed slot/query/apply core contracts and service tests.
2. Implement the runtime adapter and route Prisma slot operations through the service.
3. Add the native slot workflow model and asynchronous coordinator.
4. Render Current Slots with pagination and current thumbnails.
5. Turn the catalog browser into a target-aware Picker while preserving its state.
6. Add Preview and confirmed Apply behavior.
7. Verify automated suites, deploy, and complete the in-game acceptance checklist.

Each implementation slice receives its own test-first commit gate. No implementation commit includes Prisma retirement or unrelated cleanup.

## Acceptance Criteria

- Native SlaveTatsUI opens on Player current slots and never queries slots every frame.
- Current slots show six large cards per page with correct empty, SlaveTats, and external states.
- Only mutable slots open the target-aware picker.
- Selecting a catalog tattoo requires explicit Apply confirmation.
- Apply uses domain `default`, white `0xFFFFFF`, and alpha `1.0F` and targets the selected Player area/slot.
- Successful and failed mutations transition predictably without losing picker state.
- Native and Prisma slot queries and apply operations use the same core service.
- Current Slots and Picker request thumbnails only for their six visible cards.
- F1, F8, and existing PrismaUI workflows remain operational.
