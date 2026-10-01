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
