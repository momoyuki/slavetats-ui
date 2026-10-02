$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.IO.Compression.FileSystem

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$packageScript = Join-Path $repositoryRoot 'scripts/release/Package-Release.ps1'

if (-not (Test-Path -LiteralPath $packageScript -PathType Leaf)) {
    throw "Release package script is missing: $packageScript"
}

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

function Assert-SequenceEqual {
    param(
        [Parameter(Mandatory)][string[]] $Expected,
        [Parameter(Mandatory)][string[]] $Actual,
        [Parameter(Mandatory)][string] $Because
    )

    $expectedText = $Expected -join '|'
    $actualText = $Actual -join '|'
    if ($expectedText -ne $actualText) {
        $script:failures.Add("$Because; expected '$expectedText', got '$actualText'.")
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
        # Rejection is the observable contract.
    }
}

function New-PackageFixture {
    param(
        [Parameter(Mandatory)][string] $Parent,
        [Parameter(Mandatory)][string] $Name
    )

    $root = Join-Path $Parent $Name
    $dllDirectory = Join-Path $root 'build/release'
    New-Item -ItemType Directory -Path $dllDirectory -Force | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $dllDirectory 'SlaveTatsUI.dll'), [byte[]](1, 8, 0, 1))
    'license marker' | Set-Content -LiteralPath (Join-Path $root 'LICENSE') -Encoding utf8
    'notice marker' | Set-Content -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Encoding utf8
    'readme marker' | Set-Content -LiteralPath (Join-Path $root 'README.md') -Encoding utf8
    'debug symbols' | Set-Content -LiteralPath (Join-Path $dllDirectory 'SlaveTatsUI.pdb') -Encoding utf8
    'runtime log' | Set-Content -LiteralPath (Join-Path $root 'SlaveTatsUI.log') -Encoding utf8
    '{"hotkey":67}' | Set-Content -LiteralPath (Join-Path $root 'SlaveTatsUI.json') -Encoding utf8
    return $root
}

function Invoke-PackageInFixture {
    param(
        [Parameter(Mandatory)][string] $Fixture,
        [string] $AssetStem = 'SlaveTatsUI-1.8.0-beta.1'
    )

    Push-Location $Fixture
    try {
        & $packageScript -AssetStem $AssetStem -DllPath 'build/release/SlaveTatsUI.dll' -OutputDirectory 'artifacts/release'
    }
    finally {
        Pop-Location
    }
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SlaveTatsUI package tests {0}" -f [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

try {
    $fixture = New-PackageFixture -Parent $temporaryRoot -Name 'valid fixture with spaces'
    Invoke-PackageInFixture -Fixture $fixture

    $outputDirectory = Join-Path $fixture 'artifacts/release'
    $zipPath = Join-Path $outputDirectory 'SlaveTatsUI-1.8.0-beta.1.zip'
    $checksumPath = "$zipPath.sha256"
    Assert-Equal -Expected $true -Actual (Test-Path -LiteralPath $zipPath -PathType Leaf) -Because 'packaging creates the ZIP'
    Assert-Equal -Expected $true -Actual (Test-Path -LiteralPath $checksumPath -PathType Leaf) -Because 'packaging creates the checksum'

    $archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $actualEntries = @($archive.Entries |
            Where-Object { -not [string]::IsNullOrEmpty($_.Name) } |
            ForEach-Object { $_.FullName.Replace('\', '/') } |
            Sort-Object)
    }
    finally {
        $archive.Dispose()
    }

    $expectedEntries = @(
        'SlaveTatsUI/LICENSE',
        'SlaveTatsUI/README.md',
        'SlaveTatsUI/SKSE/Plugins/SlaveTatsUI.dll',
        'SlaveTatsUI/THIRD_PARTY_NOTICES.md'
    ) | Sort-Object
    Assert-SequenceEqual -Expected $expectedEntries -Actual $actualEntries -Because 'the ZIP contains only the installable files'

    $expectedHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $expectedChecksum = "$expectedHash  SlaveTatsUI-1.8.0-beta.1.zip"
    $actualChecksum = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    Assert-Equal -Expected $expectedChecksum -Actual $actualChecksum -Because 'the checksum sidecar identifies the exact ZIP'

    Assert-Throws -Because 'an existing output is not overwritten' -Operation {
        Invoke-PackageInFixture -Fixture $fixture
    }

    Assert-Throws -Because 'an invalid asset stem is rejected' -Operation {
        $invalidFixture = New-PackageFixture -Parent $temporaryRoot -Name 'invalid stem fixture'
        Invoke-PackageInFixture -Fixture $invalidFixture -AssetStem '../unsafe'
    }

    foreach ($missingPath in @(
        'build/release/SlaveTatsUI.dll',
        'LICENSE',
        'THIRD_PARTY_NOTICES.md',
        'README.md'
    )) {
        $safeName = $missingPath.Replace('/', '-').Replace('.', '-')
        $missingFixture = New-PackageFixture -Parent $temporaryRoot -Name "missing-$safeName"
        Remove-Item -LiteralPath (Join-Path $missingFixture $missingPath) -Force
        Assert-Throws -Because "missing required input '$missingPath' is rejected" -Operation {
            Invoke-PackageInFixture -Fixture $missingFixture
        }
    }
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}

if ($script:failures.Count -gt 0) {
    $script:failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'PackageReleaseTests: all checks passed.'
