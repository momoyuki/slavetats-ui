# vNext.5 Integration and Beta 2 Implementation Plan

> **For agentic workers:** Use executing-plans inline. Review each gate before
> moving to a remote action. No commit, push, merge, tag, or publication occurs
> without the separate approval required by AGENTS.md and RELEASING.md.

**Goal:** Integrate the accepted vNext.5 UI work into `main` and prepare a
verified `v1.8.0-beta.2` release from the accepted main commit.

**Architecture:** Preserve the existing read-only CI and tag-driven Release
workflows. Treat local branch validation, hosted PR validation, and the
published artifact as separate evidence; each must pass before the next gate.

**Tech Stack:** C++23, MSVC, CMake/CTest, PowerShell 7, GitHub Actions, MO2.

**Spec:** `docs/superpowers/specs/2026-10-01-semantic-version-ci-release-design.md`;
`ROADMAP.md` vNext.5 acceptance.

## Global Constraints

- `v1.8.0-beta.1` is already published. Never recreate, move, or overwrite it.
- `v1.8.0-beta.2` is the proposed next tag; verify it is unused before approval.
- CMake and vcpkg base versions stay `1.8.0` for another beta on this line.
- Review the complete `main...HEAD` diff, including the worktree snapshot commit.
- Keep the approved Preview, camera, and cache behavior unchanged except for
  concrete review findings. Direct VRAM measurement is a separate follow-up.
- Do not use the locally deployed DLL as the GitHub Release artifact.

## Review Focus

- Cache Clear must not delete files from an ImGui render callback; scheduled
  work must report pending, success, and failure without racing a cache build.
- `git diff main...HEAD --check` must pass after the follow-up commit.
- A new-world Preview completion must not touch the prior Actor or reapply a
  completed mutation after a synchronization-only retry.
- The large six-commit PR must explain its camera, thumbnail, Preview, and
  acceptance-document slices so a reviewer can follow the actual behavior.
- The tag must point to the accepted `main` commit, and the hosted ZIP must
  contain only the documented install tree with a matching SHA-256 sidecar.

---

### Task 1: Close the local branch review

**Files:** `include/SmoothCamAPI.h`, `src/native/NativeThumbnailRuntime.{h,cpp}`,
`src/native/OfficialMenuFrameworkAdapter.cpp`,
`tests/native/NativeThumbnailRuntimeTests.cpp`, vNext.5 roadmap/spec/archive docs.

- [x] Review the six committed changes against project standards and vNext.5
  specs in separate passes. The spec pass found no concrete contract violation.
- [x] Reproduce the two standards findings: trailing whitespace in the imported
  SmoothCam header and synchronous disk deletion in the Clear Cache render path.
- [x] Resolve the follow-up review finding: protect the prune timestamp shared
  by the render pump and scheduled Clear Cache task.
- [x] Add a failing scheduler-boundary test, schedule Clear Cache outside the
  render callback, expose completion state, and trim the two whitespace lines.
- [x] Build and run Debug and Release CTest: 55/55 passed in each configuration.
- [x] Run all five release-tool tests, validate `v1.8.0-beta.2`, and inspect a
  local package and checksum. These checks do not publish an artifact.
- [ ] Review the full scoped worktree diff, run `git diff main --check`, scan for
  secrets, show the proposed Conventional Commit message, and obtain approval
  before committing the review fixes and vNext.5 acceptance record.

### Task 2: Prepare and validate the pull request

**Files:** PR body and existing `.github/workflows/ci.yml`.

- [ ] Show `git diff --stat main...HEAD` and explain each of the six existing
  commits plus the approved follow-up commit. State vNext.5 acceptance and the
  direct-VRAM limitation in the PR body.
- [ ] Obtain explicit push approval; push the named branch without force.
- [ ] Create or update one focused PR against `main` and attach its URL to the
  task. Creation is separate from merge.
- [ ] Verify the hosted `CI` run performs Debug/Release builds, 55 CTest targets
  per configuration, and release-tool tests. Investigate any failure before
  presenting the PR for merge approval.

### Task 3: Merge the reviewed branch

- [ ] Recheck the PR diff and current base, report hosted checks, and obtain
  explicit merge approval.
- [ ] Merge only after approval, then verify `origin/main` contains the accepted
  merge commit. Do not create a version tag as part of the merge.

### Task 4: Publish and smoke-test Beta 2

- [ ] Confirm no `v1.8.0-beta.2` tag or GitHub Release exists. Run
  `scripts/release/Validate-ReleaseTag.ps1 -Tag v1.8.0-beta.2` on the accepted
  main commit and verify the tag ancestry gate.
- [ ] Show the exact annotated-tag and push commands and obtain separate tag
  and publication approval. Pushing the tag triggers the public Release workflow.
- [ ] Verify the hosted Release workflow builds/tests the tagged source and
  publishes `SlaveTatsUI-1.8.0-beta.2.zip` plus its `.sha256` file.
- [ ] Download the hosted assets, compare SHA-256, inspect the exact ZIP layout,
  and smoke-test the hosted DLL in MO2: plugin load, menu open/close, Player and
  explicit NPC selection, thumbnail Browse, and one non-destructive Preview.
- [ ] Record the hosted artifact hash and user acceptance. Archive this plan
  after completion; keep any numeric VRAM measurement as a separate follow-up.

## Evidence so far

On 2026-10-18, `main..HEAD` contained six commits and the committed diff
touched 108 files. The two-axis branch review found no confirmed vNext.5 spec
defect and identified the two standards findings addressed in Task 1. A focused
follow-up review found the prune timestamp race, which was also fixed. Local
Debug and Release suites each passed 55/55 tests. The five release-tool tests
passed, the beta.2 tag metadata validated against base version `1.8.0`, and a
local readiness ZIP had the documented layout and a matching SHA-256. None of
this substitutes for hosted CI, merge approval, or a hosted artifact smoke test.
