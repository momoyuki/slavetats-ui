param(
    [Parameter(Mandatory)][string] $Tag,
    [string] $CMakePath = 'CMakeLists.txt',
    [string] $VcpkgPath = 'vcpkg.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try {
    Import-Module (Join-Path $PSScriptRoot 'ReleaseMetadata.psm1') -Force
    $metadata = Get-ReleaseMetadata -Tag $Tag -CMakePath $CMakePath -VcpkgPath $VcpkgPath

    Write-Host "Validated release tag $($metadata.Tag) for source version $($metadata.BaseVersion)."

    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_OUTPUT)) {
        @(
            "version=$($metadata.Version)"
            "base_version=$($metadata.BaseVersion)"
            "is_prerelease=$($metadata.IsPrerelease.ToString().ToLowerInvariant())"
            "release_title=$($metadata.ReleaseTitle)"
            "asset_stem=$($metadata.AssetStem)"
        ) | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8
    }
}
catch {
    Write-Error "Release tag validation failed: $($_.Exception.Message)"
    exit 1
}
