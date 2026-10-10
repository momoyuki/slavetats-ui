$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$presetsPath = Join-Path $repositoryRoot 'CMakePresets.json'
$cmakePath = Join-Path $repositoryRoot 'CMakeLists.txt'
$portDirectory = Join-Path $repositoryRoot 'ports/commonlibsse-ng'
$portManifestPath = Join-Path $portDirectory 'vcpkg.json'
$portfilePath = Join-Path $portDirectory 'portfile.cmake'
$vcpkgConfigurationPath = Join-Path $repositoryRoot 'vcpkg-configuration.json'
$ciPath = Join-Path $repositoryRoot '.github/workflows/ci.yml'
$releasePath = Join-Path $repositoryRoot '.github/workflows/release.yml'

$script:failures = [System.Collections.Generic.List[string]]::new()

function Get-RequiredContent {
    param([Parameter(Mandatory)][string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $script:failures.Add("Required file is missing: $Path")
        return ''
    }

    return Get-Content -LiteralPath $Path -Raw
}

$presets = Get-RequiredContent -Path $presetsPath
$cmake = Get-RequiredContent -Path $cmakePath
$portManifest = Get-RequiredContent -Path $portManifestPath
$portfile = Get-RequiredContent -Path $portfilePath
$vcpkgConfiguration = Get-RequiredContent -Path $vcpkgConfigurationPath
$ci = Get-RequiredContent -Path $ciPath
$release = Get-RequiredContent -Path $releasePath

if ($presets -notmatch '"VCPKG_OVERLAY_PORTS"\s*:\s*"\$\{sourceDir\}/ports"') {
    $script:failures.Add('CMake presets must use the repository-owned vcpkg overlay ports directory.')
}

if ($cmake -notmatch 'add_commonlibsse_plugin\s*\([^)]*\bUSE_ADDRESS_LIBRARY\b') {
    $script:failures.Add('The plugin must explicitly declare Address Library runtime compatibility.')
}

if ($portManifest) {
    try {
        $manifest = $portManifest | ConvertFrom-Json
        if ($manifest.name -ne 'commonlibsse-ng') {
            $script:failures.Add("Overlay port name must be 'commonlibsse-ng'.")
        }
        if ($manifest.'version-semver' -ne '11.0.0') {
            $script:failures.Add("Overlay port must pin CommonLibSSE-NG 11.0.0.")
        }
        $requiredVersions = @{
            directxmath = '2025-04-03'
            directxtk = '2025-10-27'
            fmt = '12.1.0'
            'nlohmann-json' = '3.12.0'
            rapidcsv = '8.90'
            simpleini = '4.25'
            spdlog = '1.16.0'
            toml11 = '4.4.0'
            xbyak = '7.28'
        }
        foreach ($dependency in $requiredVersions.GetEnumerator()) {
            $entry = @($manifest.dependencies | Where-Object {
                $_ -isnot [string] -and $_.name -eq $dependency.Key
            })
            if ($entry.Count -ne 1 -or $entry[0].'version>=' -ne $dependency.Value) {
                $script:failures.Add("Overlay port must require $($dependency.Key) >= $($dependency.Value).")
            }
        }
    }
    catch {
        $script:failures.Add("Overlay port manifest is not valid JSON: $($_.Exception.Message)")
    }
}

if ($vcpkgConfiguration) {
    try {
        $configuration = $vcpkgConfiguration | ConvertFrom-Json
        if ($configuration.'default-registry'.baseline -ne 'ee12231b20c95013c6638d845d04c91559a1d1ff') {
            $script:failures.Add('The Microsoft vcpkg baseline must match CommonLibSSE-NG v11.0.0.')
        }
    }
    catch {
        $script:failures.Add("vcpkg-configuration.json is not valid JSON: $($_.Exception.Message)")
    }
}

if ($portfile) {
    if ($portfile -notmatch '(?m)^\s*REPO\s+alandtse/CommonLibSSE-NG\s*$') {
        $script:failures.Add('Overlay port must fetch CommonLibSSE-NG from alandtse/CommonLibSSE-NG.')
    }
    if ($portfile -notmatch '(?m)^\s*REF\s+"?v\$\{VERSION\}"?\s*$') {
        $script:failures.Add('Overlay port must derive the release tag from its pinned manifest version.')
    }
    if ($portfile -notmatch '(?m)^\s*SHA512\s+[0-9a-fA-F]{128}\s*$') {
        $script:failures.Add('Overlay port must verify the source archive with a full SHA-512 hash.')
    }
    if ($portfile -notmatch '(?m)^\s*REPO\s+ValveSoftware/openvr\s*$') {
        $script:failures.Add('Overlay port must restore the OpenVR submodule omitted by GitHub source archives.')
    }
    if ($portfile -notmatch '(?m)^\s*REF\s+60eb187801956ad277f1cae6680e3a410ee0873b\s*$') {
        $script:failures.Add('Overlay port must pin the OpenVR commit recorded by CommonLibSSE-NG v11.0.0.')
    }
    if ($portfile -notmatch 'vcpkg_install_copyright\(FILE_LIST\s+"\$\{SOURCE_PATH\}/COPYING\.txt"\)') {
        $script:failures.Add('Overlay port must install the GPL notice shipped as COPYING.txt in v11.0.0.')
    }
    if ($portfile -notmatch 'cmake/CommonLibSSE\.cmake') {
        $script:failures.Add('Overlay port must install the add_commonlibsse_plugin helper omitted by upstream install rules.')
    }
    if ($portfile -notmatch 'openvr/lib/win64/openvr_api\.lib') {
        $script:failures.Add('Overlay port must package the OpenVR import library referenced by the CommonLib target.')
    }
    if ($portfile -notmatch 'OPENVR_SOURCE_PATH\}/headers/') {
        $script:failures.Add('Overlay port must expose the OpenVR headers required by CommonLib public headers.')
    }
    if ($portfile -notmatch 'find_dependency\(directxtk CONFIG\)') {
        $script:failures.Add('Overlay port must make the exported CommonLib target load DirectXTK.')
    }
    if ($portfile -notmatch '\\\$\{_IMPORT_PREFIX\}/lib/openvr_api\.lib') {
        $script:failures.Add('Overlay port must replace the build-tree OpenVR path with a relocatable package path.')
    }
}

foreach ($workflow in @(
    @{ Name = 'CI'; Content = $ci },
    @{ Name = 'release'; Content = $release }
)) {
    if ($workflow.Content -notmatch '(?m)^\s+\.\/tests/release/CommonLibDependencyTests\.ps1\s*$') {
        $script:failures.Add("The $($workflow.Name) workflow must run CommonLib dependency provenance checks.")
    }
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'CommonLibDependencyTests: all dependency provenance checks passed.'
