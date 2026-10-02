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
    $lookupOutput = @(
        & gh api --paginate "repos/$Repository/releases?per_page=100" --jq '.[].tag_name' 2>&1 |
            ForEach-Object { $_.ToString() }
    )
    $result = [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $lookupOutput -join [Environment]::NewLine
    }
}

if ($null -eq $result -or $null -eq $result.ExitCode -or $null -eq $result.Output) {
    throw 'Release lookup returned an invalid result.'
}

if ([int]$result.ExitCode -ne 0) {
    throw "Unable to verify release absence for '$Tag': $($result.Output)"
}

$releaseTags = @(
    ([string]$result.Output -split '\r?\n') |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
if ($releaseTags -contains $Tag) {
    throw "A GitHub Release already exists for tag '$Tag'."
}

Write-Host "Verified no GitHub Release exists for tag '$Tag'."
