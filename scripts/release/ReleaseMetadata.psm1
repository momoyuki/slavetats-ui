$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:TagPattern = '^v(?<base>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))(?<prerelease>-(?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?$'
$script:BaseVersionPattern = '^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$'

function Get-CMakeProjectVersion {
    param([Parameter(Mandatory)][string] $Path)

    $content = Get-Content -LiteralPath $Path -Raw
    $pattern = '(?im)^\s*project\s*\(\s*SlaveTatsUI\s+VERSION\s+(?<version>[^\s\)]+)'
    $matches = [regex]::Matches($content, $pattern)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one SlaveTatsUI project version in '$Path'; found $($matches.Count)."
    }

    return $matches[0].Groups['version'].Value
}

function Get-VcpkgProjectVersion {
    param([Parameter(Mandatory)][string] $Path)

    try {
        $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable
    }
    catch {
        throw "Unable to parse vcpkg manifest '$Path': $($_.Exception.Message)"
    }

    if (-not $manifest.ContainsKey('version') -or $manifest['version'] -isnot [string]) {
        throw "Expected one string version in vcpkg manifest '$Path'."
    }

    return $manifest['version']
}

function Get-ReleaseTitle {
    param(
        [Parameter(Mandatory)][string] $BaseVersion,
        [AllowEmptyString()][string] $Prerelease
    )

    if ([string]::IsNullOrEmpty($Prerelease)) {
        return "SlaveTats UI $BaseVersion"
    }

    if ($Prerelease -match '^beta\.(?<number>\d+)$') {
        return "SlaveTats UI $BaseVersion Beta $($Matches['number'])"
    }

    if ($Prerelease -match '^rc\.(?<number>\d+)$') {
        return "SlaveTats UI $BaseVersion RC $($Matches['number'])"
    }

    return "SlaveTats UI $BaseVersion-$Prerelease"
}

function Get-ReleaseMetadata {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Tag,
        [Parameter(Mandatory)][string] $CMakePath,
        [Parameter(Mandatory)][string] $VcpkgPath
    )

    $tagMatch = [regex]::Match($Tag, $script:TagPattern)
    if (-not $tagMatch.Success) {
        throw "Release tag '$Tag' is not a supported Semantic Version tag."
    }

    $baseVersion = $tagMatch.Groups['base'].Value
    $prerelease = $tagMatch.Groups['prerelease'].Value.TrimStart('-')
    $cmakeVersion = Get-CMakeProjectVersion -Path $CMakePath
    $vcpkgVersion = Get-VcpkgProjectVersion -Path $VcpkgPath

    foreach ($sourceVersion in @($cmakeVersion, $vcpkgVersion)) {
        if ($sourceVersion -notmatch $script:BaseVersionPattern) {
            throw "Source version '$sourceVersion' is not MAJOR.MINOR.PATCH."
        }
    }

    if ($cmakeVersion -ne $vcpkgVersion) {
        throw "Source versions disagree: CMake '$cmakeVersion', vcpkg '$vcpkgVersion'."
    }

    if ($baseVersion -ne $cmakeVersion) {
        throw "Tag base version '$baseVersion' does not match source version '$cmakeVersion'."
    }

    $version = if ($prerelease) { "$baseVersion-$prerelease" } else { $baseVersion }

    return [pscustomobject]@{
        Tag = $Tag
        Version = $version
        BaseVersion = $baseVersion
        Prerelease = $prerelease
        IsPrerelease = -not [string]::IsNullOrEmpty($prerelease)
        ReleaseTitle = Get-ReleaseTitle -BaseVersion $baseVersion -Prerelease $prerelease
        AssetStem = "SlaveTatsUI-$version"
    }
}

Export-ModuleMember -Function Get-ReleaseMetadata
