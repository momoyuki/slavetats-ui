$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$modulePath = Join-Path $repositoryRoot 'scripts/release/ReleaseMetadata.psm1'

if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "Release metadata module is missing: $modulePath"
}

Import-Module $modulePath -Force

$script:failures = [System.Collections.Generic.List[string]]::new()

function Assert-Equal {
    param(
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)][string] $Because
    )

    if ($Expected -ne $Actual) {
        $script:failures.Add("$Because; expected '$Expected', got '$Actual'.")
    }
}

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
        # The rejection is the observable contract.
    }
}

function Write-VersionFixture {
    param(
        [Parameter(Mandatory)][string] $Directory,
        [string] $CMakeVersion = '1.8.0',
        [string] $VcpkgVersion = '1.8.0'
    )

    $cmakePath = Join-Path $Directory 'CMakeLists.txt'
    $vcpkgPath = Join-Path $Directory 'vcpkg.json'
    ('project(SlaveTatsUI VERSION {0} DESCRIPTION "fixture" LANGUAGES CXX)' -f $CMakeVersion) |
        Set-Content -LiteralPath $cmakePath -Encoding utf8
    @{ name = 'slavetats-ui'; version = $VcpkgVersion; dependencies = @() } |
        ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath $vcpkgPath -Encoding utf8

    return @{ CMakePath = $cmakePath; VcpkgPath = $vcpkgPath }
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SlaveTatsUI-release-metadata-{0}" -f [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

try {
    $fixture = Write-VersionFixture -Directory $temporaryRoot

    $validCases = @(
        @{ Tag = 'v1.8.0'; Version = '1.8.0'; Prerelease = ''; IsPrerelease = $false; Title = 'SlaveTats UI 1.8.0' },
        @{ Tag = 'v1.8.0-beta.1'; Version = '1.8.0-beta.1'; Prerelease = 'beta.1'; IsPrerelease = $true; Title = 'SlaveTats UI 1.8.0 Beta 1' },
        @{ Tag = 'v1.8.0-rc.2'; Version = '1.8.0-rc.2'; Prerelease = 'rc.2'; IsPrerelease = $true; Title = 'SlaveTats UI 1.8.0 RC 2' }
    )

    foreach ($case in $validCases) {
        $actual = Get-ReleaseMetadata -Tag $case.Tag -CMakePath $fixture.CMakePath -VcpkgPath $fixture.VcpkgPath
        Assert-Equal -Expected $case.Tag -Actual $actual.Tag -Because "$($case.Tag) retains its exact tag"
        Assert-Equal -Expected $case.Version -Actual $actual.Version -Because "$($case.Tag) derives the package version"
        Assert-Equal -Expected '1.8.0' -Actual $actual.BaseVersion -Because "$($case.Tag) derives the base version"
        Assert-Equal -Expected $case.Prerelease -Actual $actual.Prerelease -Because "$($case.Tag) derives the prerelease identifier"
        Assert-Equal -Expected $case.IsPrerelease -Actual $actual.IsPrerelease -Because "$($case.Tag) derives prerelease state"
        Assert-Equal -Expected $case.Title -Actual $actual.ReleaseTitle -Because "$($case.Tag) derives the release title"
        Assert-Equal -Expected "SlaveTatsUI-$($case.Version)" -Actual $actual.AssetStem -Because "$($case.Tag) derives the asset stem"
    }

    $invalidTags = @(
        '1.8.0', 'v1.8', 'v1.08.0', 'v01.8.0', 'v1.8.00',
        'v1.8.0-', 'v1.8.0-beta_1', 'v1.8.0+local', 'v1.8.0 beta.1'
    )

    foreach ($tag in $invalidTags) {
        Assert-Throws -Because "invalid tag '$tag' is rejected" -Operation {
            Get-ReleaseMetadata -Tag $tag -CMakePath $fixture.CMakePath -VcpkgPath $fixture.VcpkgPath
        }
    }

    $cmakeMismatchDirectory = Join-Path $temporaryRoot 'cmake-mismatch'
    New-Item -ItemType Directory -Path $cmakeMismatchDirectory | Out-Null
    $cmakeMismatch = Write-VersionFixture -Directory $cmakeMismatchDirectory -CMakeVersion '1.7.0'
    Assert-Throws -Because 'a CMake base-version mismatch is rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0-beta.1' -CMakePath $cmakeMismatch.CMakePath -VcpkgPath $cmakeMismatch.VcpkgPath
    }

    $vcpkgMismatchDirectory = Join-Path $temporaryRoot 'vcpkg-mismatch'
    New-Item -ItemType Directory -Path $vcpkgMismatchDirectory | Out-Null
    $vcpkgMismatch = Write-VersionFixture -Directory $vcpkgMismatchDirectory -VcpkgVersion '1.9.0'
    Assert-Throws -Because 'a vcpkg base-version mismatch is rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0-beta.1' -CMakePath $vcpkgMismatch.CMakePath -VcpkgPath $vcpkgMismatch.VcpkgPath
    }

    $duplicateCmakePath = Join-Path $temporaryRoot 'duplicate-CMakeLists.txt'
    @(
        'project(SlaveTatsUI VERSION 1.8.0 LANGUAGES CXX)'
        'project(SlaveTatsUI VERSION 1.8.0 LANGUAGES CXX)'
    ) | Set-Content -LiteralPath $duplicateCmakePath -Encoding utf8
    Assert-Throws -Because 'duplicate SlaveTatsUI project declarations are rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0' -CMakePath $duplicateCmakePath -VcpkgPath $fixture.VcpkgPath
    }

    $missingCmakePath = Join-Path $temporaryRoot 'missing-CMakeLists.txt'
    'project(OtherProject VERSION 1.8.0)' | Set-Content -LiteralPath $missingCmakePath -Encoding utf8
    Assert-Throws -Because 'a missing SlaveTatsUI version declaration is rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0' -CMakePath $missingCmakePath -VcpkgPath $fixture.VcpkgPath
    }

    $invalidJsonPath = Join-Path $temporaryRoot 'invalid-vcpkg.json'
    '{ invalid json' | Set-Content -LiteralPath $invalidJsonPath -Encoding utf8
    Assert-Throws -Because 'malformed vcpkg JSON is rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0' -CMakePath $fixture.CMakePath -VcpkgPath $invalidJsonPath
    }

    $missingVersionPath = Join-Path $temporaryRoot 'missing-version-vcpkg.json'
    @{ name = 'slavetats-ui' } | ConvertTo-Json | Set-Content -LiteralPath $missingVersionPath -Encoding utf8
    Assert-Throws -Because 'a missing vcpkg version is rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0' -CMakePath $fixture.CMakePath -VcpkgPath $missingVersionPath
    }

    $duplicateVersionPath = Join-Path $temporaryRoot 'duplicate-version-vcpkg.json'
    '{ "name": "slavetats-ui", "version": "1.7.0", "version": "1.8.0" }' |
        Set-Content -LiteralPath $duplicateVersionPath -Encoding utf8
    Assert-Throws -Because 'duplicate vcpkg version keys are rejected' -Operation {
        Get-ReleaseMetadata -Tag 'v1.8.0' -CMakePath $fixture.CMakePath -VcpkgPath $duplicateVersionPath
    }
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'ReleaseMetadataTests: all checks passed.'
