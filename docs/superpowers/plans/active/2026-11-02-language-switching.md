# Language Switching Implementation Plan

> **For agentic workers:** Use executing-plans to implement this plan inline, with test-first checks and a final review.

**Goal:** Switch the native UI between English, Simplified Chinese, Korean and Japanese without rebuilding the DLL.

**Architecture:** A UI-only catalog loads UTF-8 translation files once at startup. English source text is the fallback. A setting uses the existing atomic PluginConfigFile update boundary; rendering never changes actor/runtime identifiers.

**Tech Stack:** C++23, existing nlohmann_json configuration, SKSE Menu Framework 3.x, CTest.

**Spec:** User-approved scope in the conversation: language selector, persistence, external translation files, English fallback; en, zh-CN, ko, ja.

## Global Constraints

- Keep tattoo names, paths, domains, actor names, user preset/loadout names and runtime requests unchanged.
- Translation files are UTF-8 TSV, with escaped newlines and tabs; printf/format placeholders must retain their types and order.
- Keep widget identities independent of translated captions.
- Use the framework's font system; document CJK glyph/font setup without changing its global configuration.
- Do not merge or release this PR.

## Review Focus

- Missing/malformed files and unknown locale fall back to English.
- Failed configuration writes leave the active language unchanged and preserve other settings.
- Format placeholders and hidden widget IDs cannot be changed by translation files.
- Pack/user content and canonical area/domain strings are not translated as data.
- Release archives contain all four catalogs and hosted tests can find them from a build directory.

## Task 1: Catalog and persistence

Files: src/localization/TranslationCatalog.{h,cpp}, src/runtime/LanguageSetting.{h,cpp}, tests/localization/TranslationCatalogTests.cpp, tests/runtime/LanguageSettingTests.cpp.

Interfaces: Language enum; languageCode/languageName/parseLanguage; TranslationCatalog::load, text and widget; global catalog/selectLanguage/tr/widgetLabel; LanguageSetting::load/select/language.

- [x] Write and run failing catalog tests for fallback, UTF-8 switching, invalid formats, stable IDs and malformed rows.
- [x] Implement catalog loading and lookup, with stable storage after initialization.
- [x] Add persistence tests for round trips, unknown locale and write failure.
- [x] Implement LanguageSetting through PluginConfigFile; save before activating.
- [x] Run focused tests and commit.

## Task 2: Native presentation and four catalogs

Files: src/native/LocalizedImGui.h, src/native/OfficialMenuFrameworkAdapter.cpp, src/main.cpp, resources/translations/*.tsv, CMakeLists.txt.

Consumes: catalog and LanguageSetting from Task 1. Produces: live Language selector and localized presentation, no runtime mutation.

- [x] Add tests that switch languages across real presentation helpers and preserve domain/area data.
- [x] Add translated text/widget wrappers and translate human-facing formatted presentation helpers.
- [x] Connect startup loading and launcher selector; apply language on the following frame.
- [x] Supply English, Simplified Chinese, Korean and Japanese catalogs; validate all entries through the production parser.
- [x] Register tests and run available suites.

## Task 3: Delivery and verification

Files: scripts/release/Package-Release.ps1, tests/release/PackageReleaseTests.ps1, README.md, THIRD_PARTY_NOTICES.md, ROADMAP.md.

- [x] Extend release fixture expectations for four required catalogs and missing-catalog rejection.
- [x] Package translations; document font setup, fallback, locale codes and contributor format.
- [x] Run portable Debug/Release tests, diff checks, full scoped review; record unavailable Windows/game checks.
- [x] Push a feature branch and create a PR targeting momoyuki/slavetats-ui main; inspect hosted CI.

## Verification record

- Passed: catalog tests in GCC Debug and optimized builds, including all four shipped catalogs.
- Passed: language setting tests in Debug and optimized builds using a temporary Linux filesystem compatibility shim for the Windows rename API.
- Passed: existing adapter presentation suite plus locale switching/name preservation checks, compiled on Linux with temporary Windows/D3D API declaration shims. These shims are not committed and do not validate the real Windows ABI or game rendering.
- Passed: adapter syntax check and git diff --check. A pre-existing missing-return warning exists in the vendored SKSEMenuFramework.h ImFormatString wrapper.
- Pending: actual MSVC Debug/Release DLL builds, Windows CTest/release PowerShell suite, CJK glyph/layout and in-game acceptance. The user will test on Windows.
- Delivery: draft PR requested now; do not wait for hosted CI or merge/release.

## Follow-up: community language packs

User approved automatic language discovery. Enumerate safe `*.tsv` filenames,
read optional `# language-name: ...` metadata, persist filename locale codes and
populate the selector from the catalog. Keep English available without a file.
Package additional catalogs automatically. Existing built-in enum helpers remain
only for compatibility/tests; they do not limit discovery or selection. Added
French discovery, per-key fallback, stable widget ID and persistence checks;
release fixture includes a fifth catalog. No additional translations are claimed.

Follow-up verification: discovery/parser tests and locale persistence (including externally added French) passed with GCC; adapter syntax check passed with temporary platform declaration shims. Windows/PowerShell/game checks remain pending; do not wait for CI.

## PR review fixes — 2026-11-02

- Keep `TextUnformatted` as a raw renderer boundary. Translate fixed captions
  explicitly before output; composed Actor/slot/preset text is never looked up
  again. The regression records the real `igTextUnformatted` export arguments
  through a test-only Menu Framework DLL, including a dictionary key that matches
  a complete rendered identity and a translated caption that is also a key.
- Support `\s` in TSV fields and encode the four catalogs' trailing spaces without
  changing decoded keys or translations. Document the escape and the package's
  required/discovered translation layout. The package fixture also rejects
  accidental inclusion of the test renderer DLL through its exact ZIP assertion.
- Verification: both new regression cases failed against the original production
  code and passed after the fix with MSVC. All five PowerShell release-tool suites
  passed. Native MSVC Debug and Release DLL builds passed, with 57/57 CTest tests
  passing in each configuration and no skipped-test markers in either log. Existing
  locally installed dependencies were reused without dependency installation.
  All 988 decoded key/translation pairs match the pre-fix catalogs; both the scoped
  diff and the cumulative PR diff pass `git diff --check`. An independent source
  review found no actionable issue in the fixes.
- CJK fonts, layout and in-game acceptance remain pending. These review fixes do
  not authorize a commit, push, merge, deployment or release.

## Rebase preparation — 2026-11-06

- User authorized rebasing PR #23 and updating its existing branch; keep it draft
  while the Windows/in-game testers report back.
- Replayed the three localization commits on `main` at `669490ca1620c497f9f33aa0fccef1cffc815fac`.
  Resolved CMake, launcher wiring, adapter declarations/rendering and presentation
  test conflicts while preserving PR #24's cache limit, saved Player views,
  persistent thumbnail background and read-only External thumbnails.
- Routed new fixed captions through localization without changing runtime data.
  Captions introduced by PR #24 use English fallback where the existing catalogs
  have no translation; additional wording is follow-up work.
- Passed: GCC catalog tests in Debug and optimized builds; language persistence
  tests in Debug and optimized builds with a temporary POSIX rename shim; full
  adapter presentation suite with temporary Windows/D3D declarations and the
  recording renderer. Temporary shims and downloaded headers are not committed.
- Passed: cumulative `git diff --check`, scoped history/diff review, and unchanged
  files outside the original PR's 24-file scope. The vendored framework still has
  its pre-existing missing-return warning.
- Pending for this rebased revision: native MSVC Debug/Release DLL builds,
  Windows CTest/PowerShell packaging checks and in-game CJK/layout/persistence
  acceptance. Prior Windows results above apply to the pre-rebase revision.
