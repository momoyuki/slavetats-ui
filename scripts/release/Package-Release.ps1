param(
    [Parameter(Mandatory)][string] $AssetStem,
    [string] $DllPath = 'build/release/SlaveTatsUI.dll',
    [string] $OutputDirectory = 'artifacts/release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($AssetStem -notmatch '^SlaveTatsUI-[0-9A-Za-z.-]+$') {
    throw "Asset stem '$AssetStem' is invalid."
}

$workingDirectory = (Get-Location).Path
$requiredInputs = @(
    @{ Source = $DllPath; Destination = 'SKSE/Plugins/SlaveTatsUI.dll' },
    @{ Source = 'LICENSE'; Destination = 'LICENSE' },
    @{ Source = 'THIRD_PARTY_NOTICES.md'; Destination = 'THIRD_PARTY_NOTICES.md' },
    @{ Source = 'README.md'; Destination = 'README.md' }
)

foreach ($input in $requiredInputs) {
    $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $workingDirectory $input.Source))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required release input is missing: $($input.Source)"
    }
    $input.SourcePath = $sourcePath
}

$outputPath = [System.IO.Path]::GetFullPath((Join-Path $workingDirectory $OutputDirectory))
$zipPath = Join-Path $outputPath "$AssetStem.zip"
$checksumPath = "$zipPath.sha256"
if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $checksumPath)) {
    throw "Release output already exists for '$AssetStem'."
}

$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SlaveTatsUI-release-{0}" -f [guid]::NewGuid())
$packageRoot = Join-Path $stagingRoot 'SlaveTatsUI'

try {
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

    foreach ($input in $requiredInputs) {
        $destinationPath = Join-Path $packageRoot $input.Destination
        $destinationDirectory = Split-Path -Parent $destinationPath
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $input.SourcePath -Destination $destinationPath
    }

    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal

    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToUpperInvariant()
    "$hash  $AssetStem.zip" | Set-Content -LiteralPath $checksumPath -Encoding ascii

    Write-Host "Created $zipPath"
    Write-Host "Created $checksumPath"
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
