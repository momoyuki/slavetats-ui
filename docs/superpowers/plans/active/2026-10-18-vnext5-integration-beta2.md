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
  concrete review findings. The user reported RAM/VRAM testing passed on
  2026-10-28; numerical measurements were not supplied.
- Do not use the locally deployed DLL as the GitHub Release artifact.

## Review Focus

- Cache Clear must not delete files from an ImGui render callback; scheduled
  work must report pending, success, and failure without racing a cache build.
- `git diff main...HEAD --check` must pass on the final PR branch.
- A new-world Preview completion must not touch the prior Actor or reapply a
  completed mutation after a synchronization-only retry.
- The large PR must explain its camera, thumbnail, Preview, and
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
- [x] Review the full scoped worktree diff, run `git diff main --check`, scan for
  secrets, show the proposed Conventional Commit messages, and obtain approval
  before committing the review fixes and vNext.5 acceptance record.

### Task 2: Prepare and validate the pull request

**Files:** PR body and existing `.github/workflows/ci.yml`.

- [ ] Show `git diff --stat main...HEAD` and explain all committed changes.
  State vNext.5 acceptance and the direct-VRAM limitation in the PR body.
- [x] Obtain explicit push approval; push the named branch without force.
- [x] Create or update one focused PR against `main` and attach its URL to the
  task. Creation is separate from merge.
- [x] Inspect the successful hosted `CI` run: Debug and Release build/test
  steps and release-tool tests all reported success.

### Task 3: Merge the reviewed branch

- [x] PR #21 was merged on GitHub at commit
  `9277799ec62c52d81da44a46536dd6086ca63dbe`; hosted CI reported success.
- [x] Verify GitHub `main` contains the accepted merge commit; the comparison
  reported identical on 2026-10-28.

### Task 4: Prepare the accepted release source

**Files:** `ROADMAP.md`, this plan, `RELEASING.md`,
`scripts/release/Validate-ReleaseTag.ps1`, `.github/workflows/release.yml`.

- [ ] Preserve the pending roadmap/plan edits. Show their diff and proposed
  `docs: record vNext.5 acceptance and release readiness` commit for approval.
  Integrate approved documentation through a separate branch/PR; do not add
  new feature work to the already merged PR #21 branch.
- [ ] Fetch `origin/main` and use a clean checkout of the accepted release
  revision. Record its full SHA and verify it contains
  `9277799ec62c52d81da44a46536dd6086ca63dbe` using
  `git merge-base --is-ancestor` (expected exit code 0).
- [ ] Inspect the hosted CI run for that exact revision. Require successful
  Debug build/test, Release build/test, and release-tool tests.

- [ ] Confirm no `v1.8.0-beta.2` tag or GitHub Release exists. Run
  `scripts/release/Validate-ReleaseTag.ps1 -Tag v1.8.0-beta.2` on the accepted
  main commit and verify the tag ancestry gate.
- [ ] Prepare release notes covering the visible gallery, 192 px disk cache,
  click Preview, camera recall, compatibility requirements, and user-reported
  acceptance. Describe RAM/VRAM testing as passed without inventing numbers.

### Task 5: Tag and publish the accepted revision

- [ ] Show the accepted SHA and the exact command below, replacing
  `<accepted-main-sha>` with that SHA, and obtain explicit tag approval:

  `git tag -a v1.8.0-beta.2 <accepted-main-sha> -m "SlaveTats UI 1.8.0 Beta 2"`
- [ ] After approval, create the tag and verify `git rev-parse
  v1.8.0-beta.2^{commit}` equals the accepted SHA.
- [ ] Obtain separate publication approval for
  `git push origin v1.8.0-beta.2`. Pushing triggers the public Release workflow.
- [ ] Verify the hosted Release workflow builds/tests the tagged source and
  publishes `SlaveTatsUI-1.8.0-beta.2.zip` plus its `.sha256` file.

### Task 6: Verify the delivered package

**Files:** downloaded ZIP and sidecar, `DEPLOY.md`, this plan.

- [ ] Download the hosted assets, compare SHA-256, inspect the exact ZIP layout,
  and reject missing, extra, or mismatched files against `RELEASING.md`.
- [ ] Obtain deployment approval for the downloaded package. Close Skyrim
  before replacing its DLL and preserve the currently working MO2 deployment.
- [ ] Smoke-test the hosted DLL in MO2: plugin load, menu open/close, Player
  and explicit NPC selection, thumbnail Browse, and one Preview followed by
  Undo/Cancel. This verifies the delivered artifact; the broader feature and
  RAM/VRAM acceptance already passed.
- [ ] If verification fails, preserve logs and the prior working deployment.
  Never move the published tag; fix the source and prepare the next beta suffix.
- [ ] Record the hosted artifact hash and user acceptance. Archive this plan
  after completion; revisit performance if actual use reveals a need.

## Evidence so far

At the initial review on 2026-10-18, `main..HEAD` contained six commits and
the committed diff touched 108 files. Two approved follow-up commits brought
the branch to eight commits and 114 changed files. The two-axis branch review
found no confirmed vNext.5 spec defect and identified the two standards
findings addressed in Task 1. A focused
follow-up review found the prune timestamp race, which was also fixed. Local
Debug and Release suites each passed 55/55 tests. The five release-tool tests
passed, the beta.2 tag metadata validated against base version `1.8.0`, and a
local readiness ZIP had the documented layout and a matching SHA-256.
The hosted CI later passed and PR #21 was merged. A hosted artifact smoke test
remains outstanding.
