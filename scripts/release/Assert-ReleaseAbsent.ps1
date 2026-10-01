param(
    [Parameter(Mandatory)][string] $Repository,
    [Parameter(Mandatory)][string] $Tag,
    [scriptblock] $LookupCommand
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($LookupCommand) {
    $result = & $LookupCommand $Repository $Tag
}
else {
    $lookupOutput = @(& gh release view $Tag --repo $Repository 2>&1 | ForEach-Object { $_.ToString() })
    $result = [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $lookupOutput -join [Environment]::NewLine
    }
}

if ($null -eq $result -or $null -eq $result.ExitCode -or $null -eq $result.Output) {
    throw 'Release lookup returned an invalid result.'
}

if ([int]$result.ExitCode -eq 0) {
    throw "A GitHub Release already exists for tag '$Tag'."
}

if ([int]$result.ExitCode -eq 1 -and [string]$result.Output -match '(?i)\brelease(?:\s+[^\r\n]+)?\s+not found\b') {
    Write-Host "Verified no GitHub Release exists for tag '$Tag'."
    return
}

throw "Unable to verify release absence for '$Tag': $($result.Output)"
