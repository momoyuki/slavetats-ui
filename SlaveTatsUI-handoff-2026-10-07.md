# SlaveTats UI Handoff — 2026-10-07

## Purpose

Continue Saved Tattoo Loadouts implementation on another Windows machine after
reinstalling this one. Preserve the accepted Hotkey fix, accepted design and
implementation plan, committed Task 1, and the uncommitted Task 2 planner.

## Repository and Exact Working State

- Repository: `https://github.com/momoyuki/slavetat-ui.git`
- Original worktree: `D:\Desktop\Desktop\SkyrimMo\slavetat-ui\.worktrees\native-slot-first-workflow`
- Branch: `codex/hotkey-menu-guard`
- HEAD: `cbfda49f65bd6c9cfa6e6ff0a3c7a3f17b2cc506`
- Upstream baseline: `origin/main` at `499345d`
- Branch is four commits ahead of `origin/main` and has not been pushed.
- Do not work from the parent checkout without first verifying branch, HEAD,
  worktree, and dirty diff.

Commits after `origin/main`:

```text
0f2c541 fix: block hotkey while game menus are active
e7e772d docs: define saved tattoo loadouts
6ad030d docs: plan saved tattoo loadouts
cbfda49 feat: persist saved tattoo loadouts
```

The user confirmed that the complete development files, including unpushed
work, have already been backed up. No additional Git bundle or source archive
was created by this handoff operation.

## Authoritative Artifacts

Read these instead of reconstructing requirements from conversation:

- `ROADMAP.md`
- `CONTEXT.md`
- `docs/superpowers/specs/2026-10-04-native-saved-tattoo-loadouts-design.md`
- `docs/superpowers/plans/active/2026-10-05-native-saved-tattoo-loadouts.md`
- `.superpowers/sdd/2026-10-05-native-saved-tattoo-loadouts/progress.md`

The design and implementation plan are accepted. Native/inline execution was
selected. Continue TDD task-by-task and keep the strict per-commit approval
gate: show scoped diff, tests, and exact Conventional Commit message before
every commit.

## Completed and Accepted Work

### Hotkey Menu Guard

Commit `0f2c541` prevents the global Hotkey from opening SlaveTats UI while
paused or while item, modal, application, Console, or RaceMenu contexts are
active. An already-open Tattoo Browser can still close with the Hotkey.

The user completed in-game testing across relevant menus and confirmed the fix
passes. It was locally deployed for testing but was intentionally not released.

### Saved Tattoo Loadouts Design and Plan

- Design accepted and committed as `e7e772d`.
- Implementation plan accepted and committed as `6ad030d`.
- Task 1 store/model slice committed as `cbfda49`.

Task 1 added:

- `src/core/TattooLoadoutModels.h`
- `src/runtime/TattooLoadoutStore.h/.cpp`
- `tests/runtime/TattooLoadoutStoreTests.cpp`
- CMake registration

Task 1 TDD evidence: missing-header RED, focused GREEN, full Debug CTest 36/36,
diff check clean, and secret scan clean.

## Dirty Worktree: Task 2 Awaiting Commit Approval

Do not lose or silently commit these files:

```text
M  CMakeLists.txt
?? src/core/TattooLoadoutPlanner.h
?? src/core/TattooLoadoutPlanner.cpp
?? tests/core/TattooLoadoutPlannerTests.cpp
```

Task 2 implements a pure deterministic Core Preflight planner. It classifies
Add, Replace, Remove, Appearance, and Unchanged; preserves unrelated external
slots; rejects missing tattoos, invalid slots, external conflicts, locked
mutations, Actor/area snapshot mismatches, and duplicate area/slot positions.

TDD evidence already observed:

1. RED: `core/TattooLoadoutPlanner.h` missing.
2. GREEN: focused planner/Core tests passed.
3. Second RED: duplicate desired area/slot incorrectly produced a successful
   plan.
4. Second GREEN: duplicate validation added.
5. Fresh Debug build passed.
6. Focused CTest passed 2/2.
7. Full Debug CTest passed 37/37.
8. `git diff --check` and secret scan passed.

The exact proposed commit message is:

```text
feat: plan exact tattoo loadout restores
```

The diff and message were shown to the user, but commit approval was not given
before this handoff request. Ask for explicit approval again after verifying
the restored diff and rerunning the relevant tests. Do not infer approval.

## Task 2 Rulings

- Use `TattooLoadoutSlotRef(area, slot)` for preserved external positions
  because `TattooSlot` alone loses area identity.
- Planner independently rejects duplicate desired `(area, slot)` even though
  the persistence store also validates it.
- Planner action and blocker ordering is deterministic by area and slot.

Read the ledger for the Task 1 rulings and complete pre-flight interface map.

## Next Work

1. Restore the Git bundle and copy the dirty-file payload into the exact repo
   paths.
2. Verify `git status`, branch, HEAD, and diff before editing.
3. Run:

   ```powershell
   .\build.ps1 -Config debug
   ctest --test-dir build\debug -R "TattooLoadoutPlannerTests|SlaveTatsUICoreTests" --output-on-failure
   ctest --test-dir build\debug --output-on-failure
   git diff --check
   ```

4. Show the restored scoped diff and proposed Task 2 commit message.
5. Commit only after explicit user approval.
6. Record Task 2 completion in the ignored ledger.
7. Continue Task 3 from the accepted plan: Typed Core Batch Service Contract.

Use the repository `build.ps1` because direct `cmake --build` in a fresh
PowerShell process may lack the MSVC standard-library environment.

## Release and Deployment State

- Current public release remains `v1.8.0-beta.1`.
- Release URL: `https://github.com/momoyuki/slavetat-ui/releases/tag/v1.8.0-beta.1`
- No new release, tag, push, merge, or PR was authorized for the Hotkey or
  Loadout work.
- Never deploy into Skyrim's physical `Data` directory. Use the dedicated MO2
  mod and obtain separate deployment approval.

## Restore from the Existing Backup

Restore the backed-up repository and worktree without discarding its `.git`
metadata. Confirm that branch `codex/hotkey-menu-guard` resolves to `cbfda49`
and that the four dirty Task 2 paths listed above are present. If the backup
contains source files but not Git metadata, clone the public repository first,
then copy the backed-up files into it; the four unpushed commits will need to be
reconstructed in the listed order from the backed-up contents.

Also restore the ignored ledger to
`.superpowers/sdd/2026-10-05-native-saved-tattoo-loadouts/progress.md` if the
backup excluded ignored files. Verify `git status`, `git log`, and the complete
Task 2 diff before continuing.

## Suggested Skills

- `superpowers:using-superpowers` — recover the required skill flow first.
- `superpowers:executing-plans` — resume the accepted inline implementation
  plan and ledger.
- `superpowers:test-driven-development` — preserve RED/GREEN evidence for every
  remaining task.
- `superpowers:verification-before-completion` — verify before each commit or
  completion claim.
- `superpowers:systematic-debugging` — use for unexpected build/test failures.
- `superpowers:requesting-code-review` — perform the required final whole-branch
  review after all tasks.
- `superpowers:finishing-a-development-branch` — use only after implementation,
  full verification, and final review.

## Safety Notes

- No secrets, credentials, or private keys are intentionally included.
- Do not prune, reset, clean, or delete the original worktree until the existing
  backup has been restored and verified successfully on the other machine.
- `%TEMP%` is not durable across Windows reinstall. Copy this Markdown handoff
  document into the existing external/cloud backup before wiping the machine.
