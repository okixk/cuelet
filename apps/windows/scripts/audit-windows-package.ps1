[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $windowsRoot '..\..'))
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$repositoryLicensePath = Join-Path $repositoryRoot 'LICENSE'
$releaseIdentity = Import-PowerShellDataFile -LiteralPath (
    Join-Path $windowsRoot 'release-identity.psd1')
$version = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
$expectedPackageVersion = "$version.$($releaseIdentity.VersionRevision)"

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "MSIX package was not found: $PackagePath"
}
if ([IO.Path]::GetExtension($PackagePath) -notin @('.msix', '.appx')) {
    throw 'PackagePath must identify an AppX or MSIX package.'
}
if (-not (Test-Path -LiteralPath $repositoryLicensePath -PathType Leaf)) {
    throw "Repository LICENSE was not found: $repositoryLicensePath"
}

Add-Type -AssemblyName System.IO.Compression
$packageStream = [IO.File]::OpenRead($PackagePath)
$archive = $null
try {
    $archive = [IO.Compression.ZipArchive]::new(
        $packageStream, [IO.Compression.ZipArchiveMode]::Read)
    $entries = @($archive.Entries)
    $entryNames = @($entries | ForEach-Object {
        $_.FullName.Replace('\', '/')
    })

    $licenseEntry = $archive.GetEntry('LICENSE')
    if ($null -eq $licenseEntry) {
        throw 'Release MSIX does not contain the full repository LICENSE at package root.'
    }
    $repositoryLicense = Get-Item -LiteralPath $repositoryLicensePath
    if ($licenseEntry.Length -ne $repositoryLicense.Length) {
        throw 'Packaged LICENSE length differs from the repository LICENSE.'
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $licenseStream = $licenseEntry.Open()
        try {
            $packagedLicenseHash = [BitConverter]::ToString(
                $sha256.ComputeHash($licenseStream)).Replace('-', '')
        } finally {
            $licenseStream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }
    $repositoryLicenseHash = (
        Get-FileHash -LiteralPath $repositoryLicensePath -Algorithm SHA256).Hash
    if ($packagedLicenseHash -ne $repositoryLicenseHash) {
        throw 'Packaged LICENSE is not byte-identical to the repository LICENSE.'
    }

    $manifestEntry = $archive.GetEntry('AppxManifest.xml')
    if ($null -eq $manifestEntry) {
        throw 'Release MSIX does not contain AppxManifest.xml.'
    }
    $manifestStream = $manifestEntry.Open()
    $manifestReader = [IO.StreamReader]::new($manifestStream)
    try {
        [xml]$manifest = $manifestReader.ReadToEnd()
    } finally {
        $manifestReader.Dispose()
        $manifestStream.Dispose()
    }
    $manifestNamespaces = [Xml.XmlNamespaceManager]::new($manifest.NameTable)
    $manifestNamespaces.AddNamespace(
        'p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $identity = $manifest.SelectSingleNode('/p:Package/p:Identity', $manifestNamespaces)
    $targetFamilies = @($manifest.SelectNodes(
        '/p:Package/p:Dependencies/p:TargetDeviceFamily', $manifestNamespaces))
    if ($identity.Name -ne $releaseIdentity.PackageIdentityName -or
        $identity.Publisher -ne $releaseIdentity.Publisher -or
        $identity.Version -ne $expectedPackageVersion -or
        $identity.ProcessorArchitecture -ne $releaseIdentity.Architecture) {
        throw 'Packaged identity, publisher, version, or architecture is inconsistent with release metadata.'
    }
    if ($targetFamilies.Count -eq 0 -or @($targetFamilies | Where-Object {
        $_.MinVersion -ne $releaseIdentity.MinimumWindowsVersion
    }).Count -ne 0) {
        throw 'Packaged minimum Windows version is inconsistent with release metadata.'
    }

    $requiredEntries = @('Cuelet.exe', 'LICENSE')
    foreach ($requiredEntry in $requiredEntries) {
        if ($entryNames -notcontains $requiredEntry) {
            throw "Release MSIX is missing required file: $requiredEntry"
        }
    }

    $forbiddenPatterns = @(
        '(?i)(^|/)(DriverPackage|Debug|Source|Sources|Tests?)(/|$)',
        '(?i)(^|/)Cuelet\.VirtualAudio\.(Driver|Installer)(\.|/|$)',
        '(?i)\.(pdb|ilk|iobj|ipdb|cpp|cxx|cc|c|hpp|h|pfx|p12|pem|key|cer|crt|sys|inf|cat)$',
        '(?i)(^|/)AppxSignature\.p7x$'
    )
    $forbiddenEntries = @($entryNames | Where-Object {
        $name = $_
        @($forbiddenPatterns | Where-Object { $name -match $_ }).Count -gt 0
    })
    if ($forbiddenEntries.Count -ne 0) {
        throw "Release MSIX contains forbidden development/signing content:`n$($forbiddenEntries -join "`n")"
    }

    $pathPatterns = @(
        '(?i)[a-z]:[\\/]Users[\\/][^\\/\x00]+',
        '(?i)/Users/[^/\x00]+',
        '(?i)/home/[^/\x00]+'
    )
    $pathHits = [Collections.Generic.List[string]]::new()
    foreach ($entry in $entries) {
        if ($entry.Length -eq 0 -or $entry.Length -gt 32MB) { continue }
        $entryStream = $entry.Open()
        $memory = [IO.MemoryStream]::new()
        try {
            $entryStream.CopyTo($memory)
            $bytes = $memory.ToArray()
        } finally {
            $memory.Dispose()
            $entryStream.Dispose()
        }
        $texts = @(
            [Text.Encoding]::UTF8.GetString($bytes),
            [Text.Encoding]::Unicode.GetString($bytes)
        )
        foreach ($text in $texts) {
            if (@($pathPatterns | Where-Object { $text -match $_ }).Count -gt 0) {
                $pathHits.Add($entry.FullName)
                break
            }
        }
    }
    if ($pathHits.Count -ne 0) {
        throw "Release MSIX contains developer-machine paths in:`n$($pathHits -join "`n")"
    }
} finally {
    if ($null -ne $archive) { $archive.Dispose() }
    $packageStream.Dispose()
}

$packageHash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
Write-Host "Windows package audit passed: Cuelet $version / MSIX $expectedPackageVersion / x64 / Windows 10 1809+."
Write-Host "Packaged LICENSE is byte-identical at LICENSE (SHA-256 $repositoryLicenseHash)."
Write-Host "Unsigned MSIX SHA-256: $packageHash"
exit 0
