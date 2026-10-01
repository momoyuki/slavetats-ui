# Semantic Version, CI, and GitHub Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare SlaveTats UI `v1.8.0-beta.1`, validate every pull request and `main` push on Windows, and publish a verified installable GitHub pre-release only from an approved Semantic Version tag.

**Architecture:** Keep product builds in the existing `build.ps1`/CTest path. Put deterministic version and packaging policy in small PowerShell release scripts that both local tests and GitHub Actions call, while workflows remain orchestration-only. Separate read-only CI from the tag-triggered publishing workflow so ordinary merges cannot create a release.

**Tech Stack:** CMake 3.21+, C++23, MSVC/Visual Studio 2022, vcpkg manifest mode, PowerShell 7, GitHub Actions, GitHub CLI, ZIP/SHA-256 packaging.

**Spec:** `docs/superpowers/specs/2026-10-01-semantic-version-ci-release-design.md`

## Global Constraints

- Base project version is exactly `1.8.0` in `CMakeLists.txt` and `vcpkg.json`.
- First published tag is exactly `v1.8.0-beta.1`; the leading `v` is tag-only.
- CI runs on `windows-2022`, uses Visual Studio 2022, and performs complete Debug and Release builds/tests.
- Reusable GitHub Actions are pinned to reviewed full commit SHAs, not floating major-version tags.
- Ordinary CI has read-only permissions and cannot create tags, commits, assets, or releases.
- Only the publishing job receives `contents: write`, using the repository `GITHUB_TOKEN`.
- A release tag must parse as Semantic Version, match source base versions, and point to a commit contained in `main`.
- A pre-existing draft or published release for the tag is a hard conflict; automation never overwrites it.
- The release archive contains only the documented install tree, license, notices, and README.
- Never create or move a release tag, merge, push, publish, or deploy without the separate explicit approval required for that operation.
- Before every commit, present its scoped diff, passing checks, secret scan, and exact Conventional Commit message and wait for explicit approval.

## Review Focus

- A valid-looking tag whose base version differs from either source file must fail before build or publication; Task 2 tests both mismatch directions.
- Numeric identifiers with leading zeroes, missing patch numbers, extra text, or invalid pre-release characters must be rejected; Task 2 supplies a rejection table.
- A release package must not accidentally contain PDBs, build paths, logs, config, or a second top-level directory; Task 3 asserts the exact archive entry set.
- A tag outside `main` must never publish even when its code builds; Task 5 tests the ancestry gate with a temporary Git repository.
- A rerun against an existing draft or published release must stop without replacing assets; Task 5 tests the conflict guard independently from the GitHub API call.

---

### Task 1: Align Version, License, and User-Facing Documentation

**Files:**
- Modify: `CMakeLists.txt:4`
- Modify: `vcpkg.json`
- Create from reviewed license branch: `LICENSE`
- Create from reviewed license branch: `THIRD_PARTY_NOTICES.md`
- Modify: `README.md`
- Modify: `DEVELOPMENT.md`
- Modify: `DEPLOY.md`
- Modify: `ROADMAP.md`
- Create: `RELEASING.md`

**Interfaces:**
- Consumes: approved version contract `1.8.0` and existing `codex/gpl-license` license content.
- Produces: one consistent source version, redistributable legal files, current feature documentation, and the maintainer release contract used by Tasks 2-6.

- [ ] **Step 1: Re-read and compare every affected document before editing**

Run:

```powershell
Get-Content -Raw CMakeLists.txt
Get-Content -Raw vcpkg.json
Get-Content -Raw README.md
Get-Content -Raw DEVELOPMENT.md
Get-Content -Raw DEPLOY.md
Get-Content -Raw ROADMAP.md
git show codex/gpl-license:LICENSE
git show codex/gpl-license:THIRD_PARTY_NOTICES.md
git show codex/gpl-license:README.md
```

Expected: identify the existing `0.1.0` values, stale Player-only/DLL-only text, current vNext.4 state, and exact approved license notices without changing files.

- [ ] **Step 2: Integrate the reviewed license files without merging unrelated branch state**

Apply the exact approved `LICENSE` and `THIRD_PARTY_NOTICES.md` content from commit `da82c04`, preserving:

```text
SlaveTats UI
Copyright (C) 2026 mskmktx
SPDX-License-Identifier: GPL-3.0-or-later
```

Review the notices against the dependencies currently declared in `vcpkg.json` and the vendored interface headers before accepting them. Do not copy a stale README wholesale from the license branch.

- [ ] **Step 3: Set the base version in both source manifests**

Change:

```cmake
project(SlaveTatsUI VERSION 1.8.0 DESCRIPTION "Native SlaveTats UI plugin" LANGUAGES CXX)
```

and:

```json
{
    "name": "slavetats-ui",
    "version": "1.8.0",
    "dependencies": [
        "commonlibsse-ng",
        "nlohmann-json",
        "directxtex"
    ]
}
```

- [ ] **Step 4: Update existing Markdown to describe the current product accurately**

Make these bounded edits:

- `README.md`: replace Player-only positioning with explicit Player/Crosshair Target support; remove “DLL-only”; describe Favorites, six-entry area-scoped Recently Used, Appearance Presets, Applied/Material filters, live appearance preview, lock/domain support, and the package legal files without duplicating the roadmap.
- `DEVELOPMENT.md`: replace Player-only model/lifecycle language with Actor Target terminology; document hosted CI commands and link `RELEASING.md`.
- `DEPLOY.md`: keep local MO2 deployment separate from GitHub publication; extend smoke testing to Player and Crosshair Target; link `RELEASING.md`.
- `ROADMAP.md`: add a concise release-baseline note naming `v1.8.0-beta.1`, while leaving unchecked Loadouts and alternate-texture work unchanged.

Do not change `CONTEXT.md` or `AGENTS.md` unless implementation reveals a factual conflict; their Actor Target and approval rules are already current.

- [ ] **Step 5: Add the maintainer release guide**

Create `RELEASING.md` with these exact sections and commands:

```markdown
# Releasing SlaveTats UI

## Versioning

Source files contain `MAJOR.MINOR.PATCH`; Git tags add the leading `v` and any
pre-release suffix.

## Before Merge

Run both builds and complete test suites, then review `git diff --check`.

## Publish a Pre-release

1. Confirm the release commit is on `main` and hosted CI is green.
2. Create annotated tag `v1.8.0-beta.1` only after explicit approval.
3. Push that tag only after separate explicit publication approval.
4. Verify the GitHub Release ZIP, checksum, archive layout, and MO2 smoke test.

## Recovery

Do not move a published tag. Fix source on `main` and increment the pre-release
number. Inspect and resolve any partial or draft GitHub Release before rerunning.
```

Expand the verification commands explicitly using `build.ps1`, CTest, the release script tests from later tasks, and `git diff --check`.

- [ ] **Step 6: Verify consistency and review the documentation diff**

Run:

```powershell
rg -n "0\.1\.0|Player only|DLL-only|1\.08" CMakeLists.txt vcpkg.json README.md DEVELOPMENT.md DEPLOY.md ROADMAP.md RELEASING.md
rg -n "1\.8\.0|Crosshair Target|GPL-3\.0-or-later" CMakeLists.txt vcpkg.json README.md DEVELOPMENT.md DEPLOY.md ROADMAP.md RELEASING.md LICENSE THIRD_PARTY_NOTICES.md
git diff --check
git diff -- CMakeLists.txt vcpkg.json LICENSE THIRD_PARTY_NOTICES.md README.md DEVELOPMENT.md DEPLOY.md ROADMAP.md RELEASING.md
```

Expected: the first search has no stale hits except historical explanation deliberately quoted in `RELEASING.md`; the second confirms the required current statements; whitespace check passes.

- [ ] **Step 7: Obtain approval and commit Task 1**

Present the scoped diff, verification results, and exact message:

```text
chore: prepare 1.8.0 release metadata
```

Wait for explicit approval, then stage only Task 1 files and commit without attribution.

---

### Task 2: Implement and Test Semantic Version Tag Validation

**Files:**
- Create: `scripts/release/ReleaseMetadata.psm1`
- Create: `scripts/release/Validate-ReleaseTag.ps1`
- Create: `tests/release/ReleaseMetadataTests.ps1`

**Interfaces:**
- Consumes: tag string and the version declarations in `CMakeLists.txt` and `vcpkg.json`.
- Produces: `Get-ReleaseMetadata -Tag [string] -CMakePath [string] -VcpkgPath [string]` returning an object with `Tag`, `Version`, `BaseVersion`, `Prerelease`, `IsPrerelease`, `ReleaseTitle`, `AssetStem`; `Validate-ReleaseTag.ps1` exports these values to `GITHUB_OUTPUT` when present and exits nonzero on invalid input.

- [ ] **Step 1: Write the failing metadata tests**

Create a dependency-free PowerShell test harness that imports the module, records failures, and exits `1` if any assertion fails. Cover:

```powershell
$valid = @(
    @{ Tag = 'v1.8.0'; Base = '1.8.0'; Pre = $false; Title = 'SlaveTats UI 1.8.0' },
    @{ Tag = 'v1.8.0-beta.1'; Base = '1.8.0'; Pre = $true; Title = 'SlaveTats UI 1.8.0 Beta 1' },
    @{ Tag = 'v1.8.0-rc.2'; Base = '1.8.0'; Pre = $true; Title = 'SlaveTats UI 1.8.0 RC 2' }
)

$invalid = @(
    '1.8.0', 'v1.8', 'v1.08.0', 'v01.8.0', 'v1.8.00',
    'v1.8.0-', 'v1.8.0-beta_1', 'v1.8.0+local', 'v1.8.0 beta.1'
)
```

Use temporary CMake/vcpkg fixtures to test CMake-only mismatch, vcpkg-only mismatch, malformed/missing declarations, invalid JSON, and agreement on `1.8.0`.

- [ ] **Step 2: Run the tests and verify the intended failure**

Run:

```powershell
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
```

Expected: FAIL because `scripts/release/ReleaseMetadata.psm1` does not exist.

- [ ] **Step 3: Implement the minimal metadata module**

Implement strict parsing with:

```powershell
$TagPattern = '^v(?<base>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))(?<prerelease>-(?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?$'
```

Read the CMake version only from the `project(SlaveTatsUI VERSION ...)` declaration. Parse `vcpkg.json` with `ConvertFrom-Json -AsHashtable`. Reject missing, duplicate, malformed, or disagreeing versions. Convert known `beta.N` and `rc.N` suffixes into readable release titles while retaining the exact tag/version for assets.

- [ ] **Step 4: Implement the workflow-facing validator**

`Validate-ReleaseTag.ps1` accepts:

```powershell
param(
    [Parameter(Mandatory)][string] $Tag,
    [string] $CMakePath = 'CMakeLists.txt',
    [string] $VcpkgPath = 'vcpkg.json'
)
```

It imports the module, prints a short validated summary, and when `$env:GITHUB_OUTPUT` is non-empty appends these keys using GitHub's environment-file syntax:

```text
version=1.8.0-beta.1
base_version=1.8.0
is_prerelease=true
release_title=SlaveTats UI 1.8.0 Beta 1
asset_stem=SlaveTatsUI-1.8.0-beta.1
```

Catch validation exceptions only to write a concise error through `Write-Error`, then exit `1`.

- [ ] **Step 5: Run focused tests and direct validation**

Run:

```powershell
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
pwsh -NoProfile -File scripts/release/Validate-ReleaseTag.ps1 -Tag v1.8.0-beta.1
pwsh -NoProfile -File scripts/release/Validate-ReleaseTag.ps1 -Tag v1.08
```

Expected: test harness passes; valid tag exits `0`; invalid tag exits nonzero with no output metadata.

- [ ] **Step 6: Obtain approval and commit Task 2**

Present diff, test output, secret scan, and exact message:

```text
feat: validate semantic release tags
```

Wait for explicit approval before staging and committing only the three Task 2 files.

---

### Task 3: Implement and Test Deterministic Release Packaging

**Files:**
- Create: `scripts/release/Package-Release.ps1`
- Create: `tests/release/PackageReleaseTests.ps1`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: validated `AssetStem`, built Release DLL path, repository legal/docs files, and an output directory.
- Produces: `<AssetStem>.zip` with the exact `SlaveTatsUI/...` layout and `<AssetStem>.zip.sha256` containing `<uppercase hash>  <zip filename>`.

- [ ] **Step 1: Write the failing package tests**

The dependency-free harness creates a temporary repository fixture containing a fake DLL and marker files, invokes the script, expands the ZIP, and compares the sorted relative entries exactly to:

```powershell
$ExpectedEntries = @(
    'SlaveTatsUI/LICENSE',
    'SlaveTatsUI/README.md',
    'SlaveTatsUI/SKSE/Plugins/SlaveTatsUI.dll',
    'SlaveTatsUI/THIRD_PARTY_NOTICES.md'
)
```

Also cover missing DLL, missing required document, pre-existing output file, paths containing spaces, checksum recomputation, and a fixture containing unrelated `.pdb`, `.log`, JSON config, and build files that must not enter the archive.

- [ ] **Step 2: Run the package tests and verify failure**

Run:

```powershell
pwsh -NoProfile -File tests/release/PackageReleaseTests.ps1
```

Expected: FAIL because `Package-Release.ps1` does not exist.

- [ ] **Step 3: Implement minimal staging and archive creation**

Use explicit `-LiteralPath` operations and a unique temporary staging directory. The script parameters are:

```powershell
param(
    [Parameter(Mandatory)][string] $AssetStem,
    [string] $DllPath = 'build/release/SlaveTatsUI.dll',
    [string] $OutputDirectory = 'artifacts/release'
)
```

Validate `$AssetStem` against `^SlaveTatsUI-[0-9A-Za-z.-]+$`, reject existing output files, copy only the four declared inputs, call `Compress-Archive`, calculate SHA-256 with `Get-FileHash`, write the checksum as ASCII, and remove only the script-created staging directory in `finally`.

- [ ] **Step 4: Ignore only local generated release artifacts**

Add:

```gitignore
/artifacts/release/
```

Do not broaden ignores to all artifacts, DLLs, ZIPs, or checksum files.

- [ ] **Step 5: Run package and metadata suites**

Run:

```powershell
pwsh -NoProfile -File tests/release/PackageReleaseTests.ps1
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
pwsh -NoProfile -File scripts/release/Package-Release.ps1 -AssetStem SlaveTatsUI-1.8.0-beta.1
tar -tf artifacts/release/SlaveTatsUI-1.8.0-beta.1.zip
Get-Content artifacts/release/SlaveTatsUI-1.8.0-beta.1.zip.sha256
```

Expected: all script tests pass; archive lists only the four expected files; checksum matches `Get-FileHash`.

- [ ] **Step 6: Obtain approval and commit Task 3**

Present diff, test output, archive listing, secret scan, and exact message:

```text
feat: package verified release archives
```

Wait for explicit approval before committing Task 3 files.

---

### Task 4: Add Read-Only Windows CI

**Files:**
- Create: `.github/workflows/ci.yml`
- Create: `tests/release/WorkflowPolicyTests.ps1`
- Modify: `RELEASING.md`

**Interfaces:**
- Consumes: repository `build.ps1`, Debug/Release CTest suites, and pinned GitHub Actions.
- Produces: required read-only checks for PRs into `main`, pushes to `main`, and manual validation; workflow-policy tests consumed again in Task 5.

- [ ] **Step 1: Write the failing CI policy tests**

Implement direct assertions over `.github/workflows/ci.yml` that require:

```text
pull_request -> main
push -> main
workflow_dispatch
windows-2022
permissions -> contents: read
build.ps1 -Config debug
ctest --test-dir build/debug --output-on-failure
build.ps1 -Config release
ctest --test-dir build/release --output-on-failure
```

Reject `contents: write`, `pull-requests: write`, `git push`, `gh release`, `actions/create-release`, and action `uses:` values not ending in a 40-character commit SHA. Parse only the constrained policy the test owns; hosted GitHub validation remains authoritative for complete YAML semantics.

- [ ] **Step 2: Run the policy test and verify failure**

Run:

```powershell
pwsh -NoProfile -File tests/release/WorkflowPolicyTests.ps1
```

Expected: FAIL because `.github/workflows/ci.yml` is absent.

- [ ] **Step 3: Implement `.github/workflows/ci.yml`**

Use this job shape. The checkout SHA resolves official
`actions/checkout@v4.4.0`; verify its upstream tag again before implementation
and record any deliberate update in the scoped diff:

```yaml
name: CI

on:
  pull_request:
    branches: [main]
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ci-${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build-and-test:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0
      - name: Configure vcpkg environment
        shell: pwsh
        run: '"VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT" | Out-File -FilePath $env:GITHUB_ENV -Append'
      - name: Build Debug
        shell: pwsh
        run: ./build.ps1 -Config debug
      - name: Test Debug
        shell: pwsh
        run: ctest --test-dir build/debug --output-on-failure
      - name: Build Release
        shell: pwsh
        run: ./build.ps1 -Config release
      - name: Test Release
        shell: pwsh
        run: ctest --test-dir build/release --output-on-failure
      - name: Test release tooling
        shell: pwsh
        run: |
          ./tests/release/ReleaseMetadataTests.ps1
          ./tests/release/PackageReleaseTests.ps1
          ./tests/release/WorkflowPolicyTests.ps1
```

Do not add publishing permissions, release steps, deployment, or automatic commits.

- [ ] **Step 4: Document the hosted CI contract**

Update `RELEASING.md` with the exact CI workflow name, triggers, runner, and the rule that a green local build does not replace hosted CI acceptance.

- [ ] **Step 5: Run all local policy/release tests and full product verification**

Run sequentially and stop at the first failure:

```powershell
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
pwsh -NoProfile -File tests/release/PackageReleaseTests.ps1
pwsh -NoProfile -File tests/release/WorkflowPolicyTests.ps1
./build.ps1 -Config debug
ctest --test-dir build/debug --output-on-failure
./build.ps1 -Config release
ctest --test-dir build/release --output-on-failure
git diff --check
```

Expected: release-tool tests pass; Debug and Release each pass all registered CTest tests; whitespace check passes.

- [ ] **Step 6: Obtain approval and commit Task 4**

Present diff, all results, action-SHA provenance, secret scan, and exact message:

```text
ci: add Windows build and test workflow
```

Wait for explicit approval before committing.

---

### Task 5: Add Guarded Tag-Driven GitHub Publication

**Files:**
- Create: `.github/workflows/release.yml`
- Create: `scripts/release/Assert-TagOnMain.ps1`
- Create: `scripts/release/Assert-ReleaseAbsent.ps1`
- Create: `tests/release/ReleaseGuardsTests.ps1`
- Modify: `tests/release/WorkflowPolicyTests.ps1`
- Modify: `RELEASING.md`

**Interfaces:**
- Consumes: `Validate-ReleaseTag.ps1` outputs, the exact tagged Git commit, Release DLL, `Package-Release.ps1`, Git history containing `origin/main`, and `gh` authenticated by `GITHUB_TOKEN` only in the publish job.
- Produces: a GitHub prerelease or stable release with ZIP and checksum assets; guard scripts exit nonzero before publication on invalid ancestry or an existing release.

- [ ] **Step 1: Write failing guard and release-policy tests**

`ReleaseGuardsTests.ps1` must create a temporary Git repository with `main`, a contained release commit, and a divergent commit. Assert `Assert-TagOnMain.ps1` accepts the contained commit and rejects the divergent commit using explicit `-Commit` and `-MainRef` parameters.

Test `Assert-ReleaseAbsent.ps1` without network by injecting a `-LookupCommand` script block in test mode. Cover lookup exit `1` as absent, exit `0` as conflict, and any other exit code as lookup failure.

Extend workflow policy tests to require:

```text
push.tags -> v*
windows-2022
fetch-depth: 0
validate -> ancestry -> build -> test -> package -> publish ordering
contents: write only on publish job
release conflict guard
artifact transfer from build job to publish job
```

Reject any command that creates or pushes tags, force-pushes, deploys to MO2, or publishes on `push.branches`.

- [ ] **Step 2: Run the new tests and verify failure**

Run:

```powershell
pwsh -NoProfile -File tests/release/ReleaseGuardsTests.ps1
pwsh -NoProfile -File tests/release/WorkflowPolicyTests.ps1
```

Expected: FAIL because guard scripts and `release.yml` do not exist.

- [ ] **Step 3: Implement the two narrow guard scripts**

`Assert-TagOnMain.ps1` runs:

```powershell
git merge-base --is-ancestor $Commit $MainRef
```

and fails clearly for missing refs, Git errors, or a non-ancestor result.

`Assert-ReleaseAbsent.ps1` accepts `-Repository` and `-Tag`, then invokes:

```powershell
gh release view $Tag --repo $Repository
```

Exit `0` is a conflict; GitHub CLI's documented not-found exit is accepted as absent; authentication, API, and transport failures remain errors. Keep command injection available only as a parameter used by tests, defaulting to the real lookup.

- [ ] **Step 4: Implement the build/package job with read-only permissions**

Create `.github/workflows/release.yml` triggered only by:

```yaml
on:
  push:
    tags: ['v*']
```

The `prepare` job uses `contents: read`, `windows-2022`, checkout pinned to
`actions/checkout@11d5960a326750d5838078e36cf38b85af677262` (`v4.4.0`) with
`fetch-depth: 0`, fetches `main`, runs tag validation and ancestry validation,
performs a fresh Release build and complete Release tests, runs all release
script tests, packages the ZIP/checksum, and uploads those two files with
`actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02`
(`v4.6.2`). Re-verify each upstream tag before implementation.

- [ ] **Step 5: Implement the isolated publishing job**

The `publish` job:

```yaml
needs: prepare
permissions:
  contents: write
```

It downloads the exact artifact with
`actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093`
(`v4` at plan time), verifies the checksum again, runs
`Assert-ReleaseAbsent.ps1`, and creates the release with `gh release create`.
Re-verify the upstream tag before implementation. Pass `--prerelease` only
when the validator output is `true`; use `--verify-tag`, `--generate-notes`,
the validated release title, ZIP, and checksum paths. Do not pass `--latest`
for pre-releases.

- [ ] **Step 6: Document publication and recovery commands**

Complete `RELEASING.md` with:

- annotated-tag creation and push commands shown as examples that require approval;
- how to inspect the `Release` Actions run;
- how to download and validate the checksum;
- how to inspect a partial/draft release;
- the rule to increment beta number rather than moving a published tag.

- [ ] **Step 7: Run release automation verification**

Run:

```powershell
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
pwsh -NoProfile -File tests/release/PackageReleaseTests.ps1
pwsh -NoProfile -File tests/release/ReleaseGuardsTests.ps1
pwsh -NoProfile -File tests/release/WorkflowPolicyTests.ps1
git diff --check
```

Inspect the workflow manually for permission scope, action SHA pinning, expression quoting, job output propagation, and artifact paths. Expected: all local tests and whitespace checks pass.

- [ ] **Step 8: Obtain approval and commit Task 5**

Present diff, test outputs, action-SHA provenance, secret scan, and exact message:

```text
ci: publish releases from version tags
```

Wait for explicit approval before committing.

---

### Task 6: Final Verification, Documentation Acceptance, and Pull Request

**Files:**
- Modify if validation changes facts: `README.md`
- Modify if validation changes commands: `DEVELOPMENT.md`
- Modify if validation changes local smoke steps: `DEPLOY.md`
- Modify if validation changes release procedure: `RELEASING.md`
- Modify: `docs/superpowers/specs/2026-10-01-semantic-version-ci-release-design.md`
- Move after acceptance: `docs/superpowers/plans/active/2026-10-01-semantic-version-ci-release.md` -> `docs/superpowers/plans/archive/2026-10-01-semantic-version-ci-release.md`

**Interfaces:**
- Consumes: all prior tasks and their committed, reviewed outputs.
- Produces: one fully verified PR-ready branch; after hosted acceptance and separately approved publication, an archived execution record and verified `v1.8.0-beta.1` release.

- [ ] **Step 1: Audit all project Markdown for stale release and Actor Target claims**

Run:

```powershell
rg -n "0\.1\.0|1\.08|Player only|DLL-only|release|Release|Crosshair Target|v1\.8\.0" --glob "*.md" .
```

Classify every hit as current, historical design archaeology, or stale active guidance. Update only active user/developer documents whose current instructions are wrong. Do not rewrite archived specs/plans merely to replace historical version language.

- [ ] **Step 2: Run the complete clean verification sequence**

From the repository root, run sequentially:

```powershell
pwsh -NoProfile -File tests/release/ReleaseMetadataTests.ps1
pwsh -NoProfile -File tests/release/PackageReleaseTests.ps1
pwsh -NoProfile -File tests/release/ReleaseGuardsTests.ps1
pwsh -NoProfile -File tests/release/WorkflowPolicyTests.ps1
./build.ps1 -Config debug
ctest --test-dir build/debug --output-on-failure
./build.ps1 -Config release
ctest --test-dir build/release --output-on-failure
pwsh -NoProfile -File scripts/release/Validate-ReleaseTag.ps1 -Tag v1.8.0-beta.1
pwsh -NoProfile -File scripts/release/Package-Release.ps1 -AssetStem SlaveTatsUI-1.8.0-beta.1
tar -tf artifacts/release/SlaveTatsUI-1.8.0-beta.1.zip
git diff --check
git status --short
```

Expected: all script tests pass; Debug and Release complete suites pass; validator reports beta metadata; archive entries and checksum are exact; no unrelated working-tree changes exist.

- [ ] **Step 3: Perform focused security and distribution review**

Run:

```powershell
git diff -- .github scripts tests CMakeLists.txt vcpkg.json '*.md'
rg -n "(?i)(token|secret|password|api[_-]?key)\s*[:=]\s*['\"][^'\"]+" .github scripts tests README.md DEVELOPMENT.md DEPLOY.md RELEASING.md
tar -tf artifacts/release/SlaveTatsUI-1.8.0-beta.1.zip
```

Expected: no embedded credential; only `GITHUB_TOKEN` environment usage; no unpinned reusable action; exact package layout; no local path or MO2 artifact in the ZIP.

- [ ] **Step 4: Obtain approval and commit any final documentation corrections**

If Step 1 required changes, present their scoped diff and exact message:

```text
docs: document automated release workflow
```

Wait for explicit approval before committing. If no corrections were required, record that no additional commit is needed.

- [ ] **Step 5: Present the complete branch review before any push**

Report:

- branch and HEAD;
- commits compared with `origin/main`;
- complete Debug/Release and release-script test totals;
- archive name, entries, size, and SHA-256;
- all action names and pinned SHAs;
- clean/dirty status;
- remaining known limitation that GitHub-hosted validation has not run yet.

Show `git diff origin/main...HEAD` summary and the proposed PR title/body. Obtain explicit push/PR approval.

- [ ] **Step 6: Push and open or update the focused pull request**

After approval only, push the named branch without force and create/update the PR against `main`. The PR body must state release target, CI/release behavior, package contents, tests, license, remaining vNext.4 non-goals, and that tag publication is a later approval.

Attach the PR to the current task after creation.

- [ ] **Step 7: Verify hosted CI and obtain merge approval**

Wait for the PR checks. If CI fails, diagnose and return to the owning task; do not merge. When green, report exact checks and current PR diff, then obtain explicit merge approval before merging into `main`.

- [ ] **Step 8: Obtain separate tag/publication approval**

After merge, verify `origin/main` contains the accepted merge commit and is green. Show the exact annotated tag command and explain that pushing it publishes a public pre-release. Wait for explicit approval before creating or pushing `v1.8.0-beta.1`.

- [ ] **Step 9: Verify the published GitHub pre-release**

Confirm:

- title `SlaveTats UI 1.8.0 Beta 1`;
- tag `v1.8.0-beta.1` points to accepted `main`;
- pre-release flag is true and stable latest is not incorrectly changed;
- ZIP and checksum are downloadable;
- downloaded ZIP hash matches the checksum;
- archive contains only the four expected files.

- [ ] **Step 10: Smoke-test the hosted DLL and close the execution record**

Deploy the downloaded hosted DLL to the dedicated MO2 mod only after separate deployment approval. Verify plugin load, menu open/close, Player target, Crosshair Target, and one non-destructive catalog interaction. Record the hosted artifact SHA-256 and user acceptance in the spec, archive this plan, present the documentation-only diff and proposed message `docs: accept 1.8.0 beta release`, then obtain explicit approval before the closing commit.
