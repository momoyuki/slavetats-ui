$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ciPath = Join-Path $repositoryRoot '.github/workflows/ci.yml'

if (-not (Test-Path -LiteralPath $ciPath -PathType Leaf)) {
    throw "CI workflow is missing: $ciPath"
}

$script:failures = [System.Collections.Generic.List[string]]::new()
$content = Get-Content -LiteralPath $ciPath -Raw

function Assert-Matches {
    param(
        [Parameter(Mandatory)][string] $Pattern,
        [Parameter(Mandatory)][string] $Because
    )

    if ($content -notmatch $Pattern) {
        $script:failures.Add("Missing policy: $Because.")
    }
}

function Assert-DoesNotMatch {
    param(
        [Parameter(Mandatory)][string] $Pattern,
        [Parameter(Mandatory)][string] $Because
    )

    if ($content -match $Pattern) {
        $script:failures.Add("Forbidden policy: $Because.")
    }
}

Assert-Matches -Pattern '(?ms)^on:\s*.*?pull_request:\s*.*?branches:\s*\[main\]' -Because 'pull requests targeting main trigger CI'
Assert-Matches -Pattern '(?ms)^on:\s*.*?push:\s*.*?branches:\s*\[main\]' -Because 'pushes to main trigger CI'
Assert-Matches -Pattern '(?m)^\s{2}workflow_dispatch:\s*$' -Because 'maintainers can dispatch CI manually'
Assert-Matches -Pattern '(?ms)^permissions:\s*\r?\n\s{2}contents:\s*read\s*$' -Because 'workflow permissions are read-only'
Assert-Matches -Pattern '(?m)^\s+runs-on:\s*windows-2022\s*$' -Because 'CI uses the Visual Studio 2022 runner'
Assert-Matches -Pattern '(?m)^\s+run:\s*\.\/build\.ps1 -Config debug\s*$' -Because 'CI builds Debug through build.ps1'
Assert-Matches -Pattern '(?m)^\s+run:\s*ctest --test-dir build/debug --output-on-failure\s*$' -Because 'CI runs the complete Debug suite'
Assert-Matches -Pattern '(?m)^\s+run:\s*\.\/build\.ps1 -Config release\s*$' -Because 'CI builds Release through build.ps1'
Assert-Matches -Pattern '(?m)^\s+run:\s*ctest --test-dir build/release --output-on-failure\s*$' -Because 'CI runs the complete Release suite'
Assert-Matches -Pattern '(?m)^\s+\.\/tests/release/ReleaseMetadataTests\.ps1\s*$' -Because 'CI tests release metadata behavior'
Assert-Matches -Pattern '(?m)^\s+\.\/tests/release/PackageReleaseTests\.ps1\s*$' -Because 'CI tests release packaging behavior'
Assert-Matches -Pattern '(?m)^\s+\.\/tests/release/WorkflowPolicyTests\.ps1\s*$' -Because 'CI checks its release policy'

Assert-DoesNotMatch -Pattern '(?im)^\s*(contents|pull-requests|actions):\s*write\s*$' -Because 'ordinary CI cannot write repository state'
Assert-DoesNotMatch -Pattern '(?im)\b(git\s+push|gh\s+release|actions/create-release)\b' -Because 'ordinary CI cannot push or publish'

$usesLines = @($content -split '\r?\n' | Where-Object { $_ -match '^\s+(?:-\s+)?uses:' })
if ($usesLines.Count -eq 0) {
    $script:failures.Add('CI must use at least the official checkout action.')
}

foreach ($line in $usesLines) {
    if ($line -notmatch '^\s+(?:-\s+)?uses:\s+[^@\s]+@[0-9a-f]{40}(?:\s+#.*)?$') {
        $script:failures.Add("Action reference is not pinned to a full commit SHA: $($line.Trim())")
    }
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'WorkflowPolicyTests: all CI policy checks passed.'

$releasePath = Join-Path $repositoryRoot '.github/workflows/release.yml'
if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf)) {
    throw "Release workflow is missing: $releasePath"
}

$releaseContent = Get-Content -LiteralPath $releasePath -Raw
$requiredReleasePatterns = @(
    @{ Pattern = '(?ms)^on:\s*.*?push:\s*.*?tags:\s*\[''v\*''\]'; Because = 'version tags trigger releases' },
    @{ Pattern = '(?m)^\s+fetch-depth:\s*0\s*$'; Because = 'release checkout fetches full history' },
    @{ Pattern = '(?m)^\s+runs-on:\s*windows-2022\s*$'; Because = 'release jobs use the Visual Studio 2022 runner' },
    @{ Pattern = '(?m)^\s+run:\s*\.\/scripts/release/Validate-ReleaseTag\.ps1'; Because = 'release tags are validated' },
    @{ Pattern = '(?m)^\s+(?:run:\s*)?\.\/scripts/release/Assert-TagOnMain\.ps1'; Because = 'tag ancestry is validated' },
    @{ Pattern = '(?m)^\s+run:\s*\.\/build\.ps1 -Config release\s*$'; Because = 'the tagged source receives a fresh Release build' },
    @{ Pattern = '(?m)^\s+run:\s*ctest --test-dir build/release --output-on-failure\s*$'; Because = 'the tagged source receives complete Release tests' },
    @{ Pattern = '(?m)^\s+run:\s*\.\/scripts/release/Package-Release\.ps1'; Because = 'the workflow uses the tested packager' },
    @{ Pattern = '(?m)^\s+run:\s*\.\/scripts/release/Assert-ReleaseAbsent\.ps1'; Because = 'existing releases are rejected' },
    @{ Pattern = '(?m)^\s+gh release create\b'; Because = 'the publish job creates the GitHub Release' },
    @{ Pattern = '(?ms)^\s{2}publish:\s*.*?needs:\s*prepare\s*.*?permissions:\s*\r?\n\s{6}contents:\s*write'; Because = 'only the dependent publish job receives write permission' }
)

foreach ($requirement in $requiredReleasePatterns) {
    if ($releaseContent -notmatch $requirement.Pattern) {
        $script:failures.Add("Missing release policy: $($requirement.Because).")
    }
}

if ($releaseContent -match '(?ms)^on:\s*.*?push:\s*.*?branches:') {
    $script:failures.Add('Release publication must not trigger from a branch push.')
}

if ([regex]::Matches($releaseContent, '(?im)^\s*contents:\s*write\s*$').Count -ne 1) {
    $script:failures.Add('Release workflow must contain exactly one contents: write permission.')
}

if ($releaseContent -match '(?im)\b(git\s+tag|git\s+push|--force|Modding\\|SKYRIM-MOD)\b') {
    $script:failures.Add('Release workflow must not create/push tags, force-push, or deploy to MO2.')
}

$releaseUsesLines = @($releaseContent -split '\r?\n' | Where-Object { $_ -match '^\s+(?:-\s+)?uses:' })
if ($releaseUsesLines.Count -lt 3) {
    $script:failures.Add('Release workflow must pin checkout, upload-artifact, and download-artifact actions.')
}

foreach ($line in $releaseUsesLines) {
    if ($line -notmatch '^\s+(?:-\s+)?uses:\s+[^@\s]+@[0-9a-f]{40}(?:\s+#.*)?$') {
        $script:failures.Add("Release action reference is not pinned to a full commit SHA: $($line.Trim())")
    }
}

$orderedMarkers = @(
    'Validate release tag',
    'Verify tag is on main',
    'Build Release',
    'Test Release',
    'Package release',
    'Upload release package',
    'Publish GitHub Release'
)
$previousIndex = -1
foreach ($marker in $orderedMarkers) {
    $currentIndex = $releaseContent.IndexOf($marker, [System.StringComparison]::Ordinal)
    if ($currentIndex -lt 0 -or $currentIndex -le $previousIndex) {
        $script:failures.Add("Release workflow step ordering is invalid at '$marker'.")
        break
    }
    $previousIndex = $currentIndex
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'WorkflowPolicyTests: all release policy checks passed.'
