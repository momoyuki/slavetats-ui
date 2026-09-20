# Native Live Actor Preview Design

**Date:** 2026-09-20
**Roadmap milestone:** vNext.4 — Workflow Quality of Life
**Status:** Approved design

## Goal

Show appearance edits on the explicitly selected loaded Actor after one second of inactivity, while preserving Save as the commit action and guaranteeing that Cancel or Close restores the original appearance before leaving the editor.

## Scope

Live Preview covers every value currently editable in Native Edit Appearance:

- diffuse color;
- alpha;
- glow color;
- emissive multiplier;
- glossiness;
- specular strength.

`glowTexture` and `bump` remain read-only metadata. Arbitrary texture selection, Favorites, Recently Used, presets, loadouts, catalog badges, and Recent NPC Targets remain separate roadmap slices.

## Safety Invariants

- SlaveTatsNG remains the authoritative tattoo runtime.
- The workflow stores copied Actor identity and tattoo values only; it never retains an Actor pointer.
- Every preview, commit, restore, and synchronization retry uses the exact Actor form ID, area, slot, runtime handle, and target generation captured by the edit session.
- The runtime revalidates the requested Actor, loaded 3D state, tattoo ownership, and runtime handle before every write.
- A completion from an obsolete target generation or edit session cannot update the current workflow.
- At most one appearance operation may be in flight.
- Synchronization-only retry never repeats a completed appearance write.
- The user cannot switch Actor, navigate away, mutate another slot, or close the menu while a preview or restore operation is active.
- The workflow never falls back to the Player when an NPC cannot be resolved or is unloaded.

## Architecture

The existing layer order remains unchanged:

```text
Native UI / adapter
        |
        v
Workflow model and preview transaction
        |
        v
Workflow runtime / scheduler
        |
        v
SlaveTatsService
        |
        v
SlaveTatsRuntime
        |
        +--> SlaveTatsNG API
        +--> JContainers
```

The adapter renders controls, status, and intents only. The workflow model owns deterministic preview state, debounce eligibility, Save/Cancel/Close transitions, and stale-completion rejection. The runtime coordinator owns clock-driven scheduling and in-flight protection. Existing service and runtime appearance contracts perform validation, mutation, readback, and synchronization.

## Live Preview Session

Opening Edit Appearance creates one copyable session containing:

- Actor form ID;
- target generation;
- area, slot, runtime handle, and texture identity;
- `original` appearance captured from the Slot Snapshot;
- locally `edited` appearance;
- optional `lastPreviewed` appearance;
- time of the most recent local edit;
- preview transaction state;
- retry mode for the current operation.

The transaction states are conceptually:

```text
clean
  -> pendingPreview
  -> previewing
  -> previewApplied
  -> pendingPreview ...

previewApplied -> restoring -> clean/cancelled
previewApplied -> committing -> committed
```

Failures return to a retryable editor state without discarding the original, edited, or last-previewed values.

## Debounce and Latest-Wins Policy

- Editing any supported value updates local state immediately.
- The model records a pending preview and the monotonic time of the latest edit.
- A preview becomes eligible after 1,000 milliseconds without another local edit.
- Slider movement never schedules an operation directly from the render callback.
- If edits occur while a preview is in flight, the model retains only the newest appearance.
- When the active preview completes, the newest pending value begins a fresh one-second debounce interval measured from its own edit time.
- Equivalent values are not written again.
- The clock is injected at a testable coordinator boundary; tests do not sleep or depend on wall-clock timing.

## Save Semantics

Save means that the latest edited appearance becomes authoritative.

- If no value changed, Save exits without an appearance write.
- If the latest edited value already matches a successful preview, Save commits the session without writing again, then refreshes the Slot Snapshot.
- If a preview is pending, Save bypasses the remaining debounce delay and schedules the latest value immediately.
- If a preview is in flight, Save records commit intent and waits for that completion.
- If the in-flight preview is older than the latest edit, the latest value is scheduled immediately after completion and becomes the commit operation.
- Save completes only after the latest required write and synchronization succeed.
- A write failure remains a full update retry. A synchronization failure becomes synchronization-only retry.

## Cancel and Close Semantics

Cancel and Close share the same rollback policy.

- If no preview write succeeded, local edits are discarded immediately.
- If Cancel or Close is requested while a preview is in flight, the workflow records rollback intent and waits for that exact completion. A failed write requires no restore; a successful write or synchronization-only failure proceeds to restore the original appearance.
- If the Actor currently has a previewed value, the workflow schedules restoration of `original`.
- Navigation or menu closure completes only after restoration and synchronization succeed.
- A restore write failure exposes `Retry Restore`.
- A restore synchronization failure exposes `Retry Restore Sync` and does not rewrite the original value.
- If the Actor becomes unavailable or unloaded, the session remains open and retryable. It does not close, select the Player, or silently accept the previewed value.
- A requested Close is remembered while restoration runs; the menu closes only after successful restoration.

## Runtime Operations

Preview and restore reuse the typed appearance update path. Each ticket additionally identifies its purpose so completion handling cannot confuse preview, commit, and restore behavior.

The operation purposes are:

- `preview` — apply the latest debounced appearance;
- `commit` — apply an immediately flushed Save value when it is not already previewed;
- `restore` — apply the original appearance before Cancel or Close;
- synchronization-only variants of the same logical purpose.

The scheduler accepts one appearance ticket at a time. Exceptions map to the existing typed appearance errors and are delivered through the normal completion boundary. The workflow model, not the scheduler, decides whether the result is current and which state transition follows.

## UI Behavior

The Edit Appearance screen preserves its current control groups and footer placement. A compact status line appears below the appearance controls:

- `Preview pending...`
- `Updating preview...`
- `Preview applied`
- `Restoring original appearance...`

Failures expose an action matching the failed operation:

- `Retry Preview`
- `Retry Preview Sync`
- `Retry Restore`
- `Retry Restore Sync`

Save flushes a pending preview immediately. Cancel initiates restoration when required. Close follows Cancel semantics when the edit session contains an applied preview. Target controls, navigation, and other mutation actions are disabled while preview, commit, or restore work is active.

## Error Handling

- Preview write failure preserves local edits and offers full preview retry.
- Preview synchronization failure preserves the successful write and offers synchronization-only retry.
- Restore write failure preserves rollback intent and offers full restore retry.
- Restore synchronization failure preserves the restored write and offers synchronization-only retry.
- Actor-not-found or unloaded-Actor errors retain the exact Actor target and operation purpose for retry.
- Stale completions release only their matching runtime guard and cannot change the visible session, error, target, or Slot Snapshot.
- Repeated button presses cannot enqueue duplicate work.

## Testing

Tests are added at the narrowest stable seams.

### Workflow model

- preview eligibility at exactly 1,000 milliseconds;
- no operation before the deadline;
- latest-wins coalescing;
- one in-flight operation;
- equivalent-value suppression;
- exact Player and NPC Actor identity;
- Save immediate flush and no duplicate write after successful preview;
- Cancel before any preview and restore after a successful preview;
- Close waits for restoration;
- target/navigation/mutation blocking;
- stale generation and obsolete session completions;
- preview, commit, and restore retry-mode preservation.

### Runtime coordinator

- injected monotonic clock behavior without sleeping;
- scheduler and operation exception mapping;
- queued latest value after in-flight completion;
- synchronization-only requests do not repeat appearance writes;
- in-flight guard release for success, failure, and stale completion.

### Runtime and service

- loaded-Actor and stale-handle rejection before writes;
- write/readback validation for every editable field;
- restoration of the exact original values;
- zero and greater-than-one emissive multiplier behavior;
- glow-to-non-glow transitions;
- external slots remain read-only.

### Adapter

- exact status and retry labels;
- Save/Cancel/Close disabled and pending behavior;
- unchanged footer placement;
- UI callbacks express workflow intent only.

Full Debug and Release builds and CTest suites must pass before deployment. In-game acceptance covers Player and Crosshair NPC preview, one-second debounce, latest-wins behavior, Save without duplicate visible work, Cancel/Close restoration, Actor unload and retry, glow-to-non-glow transitions, and existing Lock/external-slot safety.

## Non-Goals

- continuous per-frame writes;
- previewing arbitrary glow or bump texture paths;
- temporary overlays outside SlaveTatsNG;
- preview persistence after Cancel or Close;
- automatic Actor loading or fallback;
- camera/freeze controls;
- Favorites, history, presets, loadouts, catalog badges, or Recent NPC Targets.

## Acceptance Criteria

The user can edit any supported appearance value and see it on the selected loaded Actor after one second of inactivity. Rapid edits coalesce to the latest value without overlapping writes. Save commits the latest value without repeating an already successful preview. Cancel and Close restore the exact original appearance before leaving. Failures remain retryable with correct full-write versus synchronization-only semantics, and no operation can leak across Actor targets, edit sessions, or stale handles.
