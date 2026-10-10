#!/usr/bin/env pwsh
# Build script — sets up MSVC environment then runs CMake

param(
    [ValidateSet("release", "debug")]
    [string]$Config = "release"
)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Install Visual Studio 2022 (any edition) with the 'Desktop development with C++' workload."
    exit 1
}

$vsInstallPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstallPath) {
    Write-Error "No VS 2022 install with the C++ toolset found. Install the 'Desktop development with C++' workload."
    exit 1
}

$vcvarsall = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvarsall)) {
    Write-Error "vcvarsall.bat not found at expected path: $vcvarsall"
    exit 1
}

# Import MSVC environment into current session
$configuredVcpkgRoot = $env:VCPKG_ROOT
Write-Host "Setting up MSVC x64 environment..."
cmd /c "`"$vcvarsall`" x64 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
}

# vcvarsall may select Visual Studio's bundled vcpkg; retain the user's install.
if ($configuredVcpkgRoot) {
    $env:VCPKG_ROOT = $configuredVcpkgRoot
}

if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Point it at your vcpkg install, e.g. `$env:VCPKG_ROOT = 'C:\vcpkg'`."
    exit 1
}

Write-Host "Configuring ($Config)..."
cmake --preset "build-$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building..."
cmake --build "build/$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Build complete: build/$Config/SlaveTatsUI.dll"
Write-Host ""
Write-Host "Deploy to your MO2 mod folder (never copy directly into Data\ — see DEPLOY.md):"
Write-Host "  SlaveTatsUI.dll  -> <mod>\SKSE\Plugins\SlaveTatsUI.dll"
