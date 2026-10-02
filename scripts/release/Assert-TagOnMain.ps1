param(
    [Parameter(Mandatory)][string] $Commit,
    [Parameter(Mandatory)][string] $MainRef
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedCommit = (& git rev-parse --verify "$Commit^{commit}" 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($resolvedCommit)) {
    throw "Release commit '$Commit' cannot be resolved."
}

$resolvedMain = (& git rev-parse --verify "$MainRef^{commit}" 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($resolvedMain)) {
    throw "Main ref '$MainRef' cannot be resolved."
}

& git merge-base --is-ancestor $resolvedCommit.Trim() $resolvedMain.Trim()
$ancestryExitCode = $LASTEXITCODE
if ($ancestryExitCode -eq 1) {
    throw "Release commit '$Commit' is not contained in '$MainRef'."
}
if ($ancestryExitCode -ne 0) {
    throw "Unable to verify release ancestry (git exit $ancestryExitCode)."
}

Write-Host "Verified release commit '$Commit' is contained in '$MainRef'."
