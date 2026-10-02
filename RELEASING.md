# Releasing SlaveTats UI

This guide covers GitHub publication. Local Mod Organizer 2 deployment remains
documented separately in [DEPLOY.md](DEPLOY.md).

## Versioning

SlaveTats UI follows Semantic Versioning. Source files contain
`MAJOR.MINOR.PATCH`; Git tags add the leading `v` and any pre-release suffix.

The current base version is `1.8.0`. Its first public test tag is
`v1.8.0-beta.1`. Increment the suffix for another beta, remove it for the stable
`v1.8.0` release, increment PATCH for compatible fixes, and increment MINOR for
compatible features.

The base version must agree in both files:

```text
CMakeLists.txt
vcpkg.json
```

## Before Merge

Run the repository-supported builds and complete test suites sequentially:

```powershell
.\build.ps1 -Config debug
ctest --test-dir build/debug --output-on-failure
.\build.ps1 -Config release
ctest --test-dir build/release --output-on-failure
git diff --check
```

Run every release-script test under `tests/release/` once those tests are
present. Review the scoped diff and obtain explicit approval before committing,
pushing, or merging.

The GitHub Actions workflow named `CI` repeats Debug and Release verification on
the `windows-2022` runner for pull requests into `main`, pushes to `main`, and
manual dispatches. It also runs the release-tool test scripts and has read-only
repository permissions. A green local run does not replace hosted CI
acceptance.

## Publish a Pre-release

1. Confirm the release commit is on `main` and hosted CI is green.
2. Validate the proposed tag:

   ```powershell
   .\scripts\release\Validate-ReleaseTag.ps1 -Tag v1.8.0-beta.1
   ```

3. Present the exact annotated-tag command and obtain explicit approval.
4. Create annotated tag `v1.8.0-beta.1` from the accepted `main` commit:

   ```powershell
   git tag -a v1.8.0-beta.1 -m "SlaveTats UI 1.8.0 Beta 1"
   ```

5. Obtain separate publication approval before pushing the tag.

   ```powershell
   git push origin v1.8.0-beta.1
   ```

6. Verify the GitHub Release ZIP, checksum, archive layout, and MO2 smoke test.

The tag-triggered workflow builds the Release DLL again; it never publishes a
locally deployed binary. A pre-release contains:

```text
SlaveTatsUI/
|-- SKSE/
|   `-- Plugins/
|       `-- SlaveTatsUI.dll
|-- LICENSE
|-- THIRD_PARTY_NOTICES.md
`-- README.md
```

## Recovery

Do not move or rewrite a published tag. Fix the source on `main` and increment
the pre-release number. Inspect and resolve any partial or draft GitHub Release
before rerunning publication; automation must not overwrite an existing release
or asset silently. Use the GitHub `Release` Actions run to distinguish build,
test, packaging, artifact transfer, and publication failures. Download both the
ZIP and `.sha256` asset and compare the sidecar value with:

```powershell
Get-FileHash .\SlaveTatsUI-1.8.0-beta.1.zip -Algorithm SHA256
```
