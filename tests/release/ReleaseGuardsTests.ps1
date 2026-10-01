$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ancestryScript = Join-Path $repositoryRoot 'scripts/release/Assert-TagOnMain.ps1'
$releaseScript = Join-Path $repositoryRoot 'scripts/release/Assert-ReleaseAbsent.ps1'

foreach ($requiredScript in @($ancestryScript, $releaseScript)) {
    if (-not (Test-Path -LiteralPath $requiredScript -PathType Leaf)) {
        throw "Release guard script is missing: $requiredScript"
    }
}

$script:failures = [System.Collections.Generic.List[string]]::new()

function Assert-Throws {
    param(
        [Parameter(Mandatory)][scriptblock] $Operation,
        [Parameter(Mandatory)][string] $Because
    )

    try {
        & $Operation
        $script:failures.Add("$Because; expected an exception.")
    }
    catch {
        # Rejection is the observable contract.
    }
}

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments)][string[]] $Arguments)

    $output = @(& git @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE`: $($output -join ' ')"
    }
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SlaveTatsUI release guards {0}" -f [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

try {
    Push-Location $temporaryRoot
    try {
        Invoke-Git init -b main
        Invoke-Git config user.name 'Release Guard Tests'
        Invoke-Git config user.email 'release-guards@example.invalid'
        Invoke-Git config core.autocrlf false
        $emptyIgnore = Join-Path $temporaryRoot 'empty-global-ignore'
        '' | Set-Content -LiteralPath $emptyIgnore -Encoding ascii
        Invoke-Git config core.excludesFile $emptyIgnore

        'root' | Set-Content -LiteralPath 'root.txt' -Encoding utf8
        Invoke-Git add root.txt
        Invoke-Git commit -m 'root'
        $rootCommit = (& git rev-parse HEAD).Trim()

        'main' | Set-Content -LiteralPath 'main.txt' -Encoding utf8
        Invoke-Git add main.txt
        Invoke-Git commit -m 'main release commit'
        $mainCommit = (& git rev-parse HEAD).Trim()

        Invoke-Git switch -c divergent $rootCommit
        'divergent' | Set-Content -LiteralPath 'divergent.txt' -Encoding utf8
        Invoke-Git add divergent.txt
        Invoke-Git commit -m 'divergent commit'
        $divergentCommit = (& git rev-parse HEAD).Trim()

        & $ancestryScript -Commit $mainCommit -MainRef main
        Assert-Throws -Because 'a divergent tag commit is rejected' -Operation {
            & $ancestryScript -Commit $divergentCommit -MainRef main
        }
        Assert-Throws -Because 'a missing main ref is rejected' -Operation {
            & $ancestryScript -Commit $mainCommit -MainRef refs/remotes/origin/missing
        }
    }
    finally {
        Pop-Location
    }

    $notFoundLookup = { param($Repository, $Tag) [pscustomobject]@{ ExitCode = 1; Output = "release not found: $Repository $Tag" } }
    & $releaseScript -Repository 'owner/repository' -Tag 'v1.8.0-beta.1' -LookupCommand $notFoundLookup

    $existingLookup = { param($Repository, $Tag) [pscustomobject]@{ ExitCode = 0; Output = "existing: $Repository $Tag" } }
    Assert-Throws -Because 'an existing release is a conflict' -Operation {
        & $releaseScript -Repository 'owner/repository' -Tag 'v1.8.0-beta.1' -LookupCommand $existingLookup
    }

    $authenticationLookup = { param($Repository, $Tag) [pscustomobject]@{ ExitCode = 1; Output = "authentication failed: $Repository $Tag" } }
    Assert-Throws -Because 'an authentication failure is not treated as absence' -Operation {
        & $releaseScript -Repository 'owner/repository' -Tag 'v1.8.0-beta.1' -LookupCommand $authenticationLookup
    }

    $transportLookup = { param($Repository, $Tag) [pscustomobject]@{ ExitCode = 2; Output = "network unavailable: $Repository $Tag" } }
    Assert-Throws -Because 'a transport failure is not treated as absence' -Operation {
        & $releaseScript -Repository 'owner/repository' -Tag 'v1.8.0-beta.1' -LookupCommand $transportLookup
    }
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'ReleaseGuardsTests: all checks passed.'
