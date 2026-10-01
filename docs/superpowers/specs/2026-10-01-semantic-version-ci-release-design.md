# Semantic Version, CI, and GitHub Release Design

**Date:** 2026-10-01  
**Target release:** `v1.8.0-beta.1`  
**Release title:** `SlaveTats UI 1.8.0 Beta 1`

## Purpose

Establish a repeatable release process for SlaveTats UI before merging the
current native workflow into `main`. Pull requests and `main` must receive
automated Windows build and test coverage. A deliberate Semantic Version tag
must produce the distributable archive and GitHub Release without turning every
merge into a public release.

This work packages and publishes the already accepted product behavior. It does
not add a gameplay feature or complete the remaining vNext.4 roadmap items.

## Version Contract

SlaveTats UI uses Semantic Versioning in `MAJOR.MINOR.PATCH` form.

- `MAJOR` changes when compatibility is deliberately broken.
- `MINOR` changes for backward-compatible feature additions.
- `PATCH` changes for backward-compatible fixes.
- Pre-release identifiers such as `beta.1` and `rc.1` are appended after a
  hyphen.

The current release line has base version `1.8.0` in both `CMakeLists.txt` and
`vcpkg.json`. The first public test release uses tag `v1.8.0-beta.1`. The leading
`v` belongs to the Git tag only; source metadata retains `1.8.0`.

The human-facing release title may spell out the pre-release stage, but it must
not use a competing numeric form such as `1.08`. Subsequent releases follow
these examples:

- `v1.8.0-beta.2` for another beta on the same base version;
- `v1.8.0-rc.1` for a release candidate;
- `v1.8.0` for the stable release;
- `v1.8.1` for a compatible fix;
- `v1.9.0-beta.1` for the next compatible feature release.

## Source and License Preparation

Before the release workflow is enabled, the release branch must contain:

- GPL-3.0-or-later project licensing with copyright holder `mskmktx`;
- third-party notices for redistributed or incorporated dependencies;
- current README behavior and requirements, including Player and Crosshair
  Target support;
- matching `1.8.0` base versions in CMake and vcpkg metadata.

The existing license work may be integrated from its focused branch, but its
content must be reviewed with the release changes. No binary release may omit
the project license or third-party notices from the downloadable archive.

## Continuous Integration Workflow

Add `.github/workflows/ci.yml` with read-only repository permissions.

### Triggers

- Pull requests targeting `main`.
- Pushes to `main`.
- Manual dispatch for maintainers who need to reproduce the hosted check.

### Execution

The workflow runs on `windows-2022`, matching the documented Visual Studio 2022
toolchain, and checks out the exact commit being evaluated. Reusable GitHub
Actions are pinned to reviewed full commit SHAs rather than floating major-version
tags. The job initializes the MSVC environment and repository vcpkg dependencies,
then performs these steps in order:

1. configure and build Debug through the repository-supported build path;
2. run the complete Debug CTest suite with failure output;
3. configure and build Release through the same supported path;
4. run the complete Release CTest suite with failure output.

A failed configure, dependency restore, build, or test fails the check. Tests
must never run against a stale binary after a failed build. Dependency caching
may be added only when its key includes the dependency manifests, registry
configuration, triplet, runner environment, and configuration inputs that can
change the result.

The CI workflow does not create tags, releases, or repository commits.

## Tag-Driven Release Workflow

Add `.github/workflows/release.yml` triggered by tags matching `v*`. It may also
support manual dispatch for validation, but manual execution must not publish a
release unless an existing valid tag is selected explicitly.

### Validation Gates

Before publishing, the workflow must verify all of the following:

1. The ref is a complete Semantic Version tag shaped like
   `vMAJOR.MINOR.PATCH` with an optional valid pre-release suffix.
2. The tag's `MAJOR.MINOR.PATCH` equals the base version in both
   `CMakeLists.txt` and `vcpkg.json`.
3. The tagged commit is contained in `main` after fetching sufficient history.
4. The Release build succeeds from a clean checkout of the tag.
5. The complete Release CTest suite passes.
6. Every required package input exists before archive creation.

If any gate fails, the workflow stops before creating or modifying a GitHub
Release.

### Permissions and Concurrency

Default workflow permissions remain read-only. Only the publishing job receives
`contents: write`, which is required to create the GitHub Release and upload its
asset. The workflow uses the repository-provided `GITHUB_TOKEN`; no personal
access token or release credential is stored.

Release runs are serialized by tag. The publish step fails with a clear conflict
if any release, including a draft, already exists for the tag. It never replaces
an existing release or asset automatically. A maintainer must inspect and resolve
the existing GitHub state before rerunning publication.

## Distribution Archive

The release asset is named from the exact tag without its leading `v`, for
example:

```text
SlaveTatsUI-1.8.0-beta.1.zip
```

The ZIP contains one top-level directory and this exact installable layout:

```text
SlaveTatsUI/
|-- SKSE/
|   `-- Plugins/
|       `-- SlaveTatsUI.dll
|-- LICENSE
|-- THIRD_PARTY_NOTICES.md
`-- README.md
```

No Debug DLL, PDB, build directory, local configuration, MO2 path, log, cache,
credential, or developer-machine artifact is included. The Release DLL is built
by the same workflow run that creates the archive; a previously deployed local
DLL is never reused.

The workflow calculates and reports a SHA-256 hash for the ZIP and writes it to
`SlaveTatsUI-1.8.0-beta.1.zip.sha256`. The checksum file is attached beside the
ZIP so users can verify the download independently.

## GitHub Release Behavior

The GitHub Release is created only after validation, build, tests, packaging,
and hash generation succeed.

- A version containing a pre-release suffix is marked as a GitHub pre-release.
- A plain `vMAJOR.MINOR.PATCH` version is a stable release.
- GitHub-generated release notes may supply the change list for the initial
  automation, with a short maintained heading for requirements and beta status.
- The ZIP and checksum are attached to the Release.
- Source archives generated by GitHub remain available as required by the
  repository license and normal GitHub behavior.

The workflow never marks a beta as `latest` stable. Stable release behavior may
use GitHub's normal latest-release selection.

## Merge and Publication Sequence

Release preparation stays on the current feature branch until review completes.
The intended sequence is:

1. integrate the approved license content;
2. update version metadata and current user documentation;
3. add and locally validate CI, version validation, packaging, and release
   workflows at the narrowest practical seams;
4. run the complete local Debug and Release builds and test suites;
5. review the complete scoped diff and obtain explicit commit approval;
6. commit and push the branch only after approval;
7. open or update the pull request targeting `main`;
8. require hosted CI to pass and obtain explicit merge approval;
9. merge the pull request into `main`;
10. create and push annotated tag `v1.8.0-beta.1` from the accepted `main`
    commit only after explicit tag/release approval;
11. verify the GitHub pre-release, attached ZIP, checksum, and archive layout.

Tag creation is a separate publication decision from merging. Neither workflow
creates a version tag automatically from a merge.

## Testing and Acceptance

Automated acceptance requires:

- local Debug build and complete Debug CTest suite;
- local Release build and complete Release CTest suite;
- `git diff --check`;
- deterministic tests or script-level checks for accepted/rejected tag forms,
  base-version mismatch, and pre-release detection;
- a package-layout check that opens or lists the ZIP and verifies only the
  expected paths;
- a check that publishing permissions are absent from ordinary CI;
- hosted CI success on the pull request;
- one successful `v1.8.0-beta.1` GitHub pre-release with downloadable ZIP and
  matching SHA-256.

In-game acceptance already completed for the current feature baseline remains
valid for its tested DLL. The GitHub-built release DLL still requires a short
release smoke test through MO2: plugin load, menu open/close, Player selection,
Crosshair Target selection, and one non-destructive catalog interaction. This
confirms the hosted artifact rather than only the local build.

## Failure Handling

- A CI failure blocks merge but does not modify repository state.
- A release validation, build, test, or packaging failure creates no Release.
- A publish failure retains workflow logs and the packaged workflow artifact
  needed for diagnosis. If GitHub contains a partial or draft release afterward,
  the next run stops until a maintainer inspects and resolves that state.
- A tag pointing outside `main` is rejected rather than published.
- A version mismatch is fixed in source and merged before a new tag is made;
  published tag history is not rewritten.
- Failed or incorrect public tags and releases are not force-moved. A corrected
  pre-release increments the pre-release number.

## Non-Goals

- Releasing on every merge to `main`.
- Automatically incrementing or committing version numbers.
- Automatically creating a tag.
- Publishing to Nexus Mods or another external distribution service.
- Deploying the hosted DLL into the developer's local MO2 installation.
- Completing Saved Tattoo Sets / Loadouts or controlled alternate Glow/Bump
  texture selection.

## Completion Criteria

This release-preparation slice is complete when the license and current docs are
present on `main`, CI is green, tag `v1.8.0-beta.1` has produced the expected
GitHub pre-release and verified archive, and the hosted DLL passes the release
smoke test. Further roadmap work then branches from the accepted `main` state.
