[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $windowsRoot '..\..'))
$version = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
$releaseIdentity = Import-PowerShellDataFile -LiteralPath (
    Join-Path $windowsRoot 'release-identity.psd1')
$expectedPackageVersion = "$version.$($releaseIdentity.VersionRevision)"
$artifactName = "Cuelet-$version-beta.1-windows-x64-unsigned.zip"
$applicationLayout = Join-Path $windowsRoot 'x64\Release\Cuelet.WinUI'
$assetSource = Join-Path $windowsRoot 'Cuelet.WinUI\Assets'
$noticeSource = Join-Path $windowsRoot 'docs\BETA-NOTICE.txt'
$readmeSource = Join-Path $windowsRoot 'docs\WINDOWS-BETA-README.txt'

if ($version -ne '0.1.0') {
    throw "The Windows beta artifact is pinned to Cuelet 0.1.0; found VERSION '$version'."
}
if ($releaseIdentity.Architecture -ne 'x64' -or
    $releaseIdentity.VersionRevision -ne 0 -or
    $releaseIdentity.MinimumWindowsVersion -ne '10.0.17763.0') {
    throw 'Windows beta release identity is inconsistent with the supported x64/Windows 10 1809 target.'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $windowsRoot "dist\$artifactName"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) {
    throw "Refusing to overwrite an existing beta artifact: $OutputPath"
}

foreach ($requiredSource in @($noticeSource, $readmeSource, (Join-Path $repositoryRoot 'LICENSE'))) {
    if (-not (Test-Path -LiteralPath $requiredSource -PathType Leaf)) {
        throw "Required beta source file was not found: $requiredSource"
    }
}

& (Join-Path $PSScriptRoot 'test-windows-release-metadata.ps1')
if (-not $?) { exit 1 }

& (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration Release -Rebuild -PortableBeta
if (-not $?) { exit 1 }

if (-not (Test-Path -LiteralPath $applicationLayout -PathType Container)) {
    throw "The fresh portable Release application layout was not found: $applicationLayout"
}

$requiredLayoutFiles = @(
    'Cuelet.exe',
    'App.xbf',
    'MainWindow.xbf',
    'Cuelet.pri',
    'Microsoft.WindowsAppRuntime.dll',
    'Microsoft.UI.Xaml.dll',
    'Microsoft.Web.WebView2.Core.dll',
    'LICENSE'
)
foreach ($requiredFile in $requiredLayoutFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $applicationLayout $requiredFile) -PathType Leaf)) {
        throw "The portable Release layout is missing required runtime content: $requiredFile"
    }
}
if (Test-Path -LiteralPath (Join-Path $applicationLayout 'AppxManifest.xml')) {
    throw 'The portable beta layout contains an MSIX manifest.'
}
if (@(Get-ChildItem -LiteralPath $applicationLayout -Recurse -File -Filter '*.msix').Count -ne 0) {
    throw 'The portable beta layout contains a generated MSIX.'
}

$forbiddenLayoutPatterns = @(
    '(?i)(^|[\\/])(DriverPackage|Debug|Source|Sources|Tests?)([\\/]|$)',
    '(?i)(^|[\\/])Cuelet\.VirtualAudio\.(Driver|Installer)(\.|[\\/]|$)',
    '(?i)(SysVAD|VBCABLE|VB-CABLE)',
    '(?i)\.(sys|inf|cat|pfx|p12|pem|key|cer|crt)$',
    '(?i)(^|[\\/])AppxSignature\.p7x$'
)
$forbiddenLayoutFiles = @(Get-ChildItem -LiteralPath $applicationLayout -Recurse -File |
    Where-Object {
        $relative = $_.FullName.Substring($applicationLayout.Length + 1)
        @($forbiddenLayoutPatterns | Where-Object { $relative -match $_ }).Count -gt 0
    })
if ($forbiddenLayoutFiles.Count -ne 0) {
    $forbiddenList = $forbiddenLayoutFiles | ForEach-Object { $_.FullName }
    throw "The portable Release layout contains forbidden development/signing content: $($forbiddenList -join [Environment]::NewLine)"
}

$buildOnlyPatterns = @(
    '(?i)\.(pdb|ilk|iobj|ipdb|exp|lib)$',
    '(?i)\.build\.appxrecipe$',
    '(?i)(^|[\\/])microsoft\.system\.package\.metadata$'
)
$stageRoot = Join-Path ([IO.Path]::GetTempPath()) ("cuelet-beta-" + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

    $layoutFiles = @(Get-ChildItem -LiteralPath $applicationLayout -Recurse -File |
        Where-Object {
            $relative = $_.FullName.Substring($applicationLayout.Length + 1)
            @($buildOnlyPatterns | Where-Object { $relative -match $_ }).Count -eq 0
        })
    foreach ($file in $layoutFiles) {
        $relative = $file.FullName.Substring($applicationLayout.Length + 1)
        $destination = Join-Path $stageRoot $relative
        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
    Copy-Item -LiteralPath $noticeSource -Destination (Join-Path $stageRoot 'BETA-NOTICE.txt')
    Copy-Item -LiteralPath $readmeSource -Destination (Join-Path $stageRoot 'README-WINDOWS-BETA.txt')

    $stagedLicense = Join-Path $stageRoot 'LICENSE'
    $repositoryLicense = Join-Path $repositoryRoot 'LICENSE'
    if ((Get-FileHash -LiteralPath $stagedLicense -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $repositoryLicense -Algorithm SHA256).Hash) {
        throw 'The portable beta LICENSE is not byte-identical to the repository LICENSE.'
    }

    $stagedExecutable = Join-Path $stageRoot 'Cuelet.exe'
    $executableBytes = [IO.File]::ReadAllBytes($stagedExecutable)
    if ($executableBytes.Length -lt 64 -or
        $executableBytes[0] -ne 0x4D -or $executableBytes[1] -ne 0x5A) {
        throw 'Cuelet.exe is not a valid PE executable.'
    }
    $peOffset = [BitConverter]::ToInt32($executableBytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $executableBytes.Length -or
        $executableBytes[$peOffset] -ne 0x50 -or $executableBytes[$peOffset + 1] -ne 0x45) {
        throw 'Cuelet.exe does not contain a valid PE header.'
    }
    $machine = [BitConverter]::ToUInt16($executableBytes, $peOffset + 4)
    if ($machine -ne 0x8664) {
        throw ('Cuelet.exe is not x64 (PE machine 0x{0:X4}).' -f $machine)
    }
    $fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($stagedExecutable).FileVersion
    if ($fileVersion -ne $expectedPackageVersion) {
        throw "Cuelet.exe file version '$fileVersion' does not match $expectedPackageVersion."
    }
    $signature = Get-AuthenticodeSignature -FilePath $stagedExecutable
    if ($null -ne $signature.SignerCertificate -or $signature.Status -ne 'NotSigned') {
        throw "Cuelet.exe is not intentionally unsigned (signature status: $($signature.Status))."
    }

    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory) {
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    }
    Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $OutputPath -CompressionLevel Optimal

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($OutputPath)
    try {
        $entries = @($archive.Entries)
        $entryNames = @($entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($requiredEntry in @('Cuelet.exe', 'LICENSE', 'BETA-NOTICE.txt', 'README-WINDOWS-BETA.txt')) {
            if ($entryNames -notcontains $requiredEntry) {
                throw "Beta ZIP is missing required root entry: $requiredEntry"
            }
        }
        $zipForbiddenPatterns = @(
            '(?i)\.(msix|appx|pdb|ilk|iobj|ipdb|exp|lib|pfx|p12|pem|key|cer|crt|sys|inf|cat)$',
            '(?i)(^|/)AppxManifest\.xml$',
            '(?i)(^|/)AppxSignature\.p7x$',
            '(?i)(SysVAD|VBCABLE|VB-CABLE|Cuelet\.VirtualAudio\.(Driver|Installer))'
        )
        $zipForbiddenEntries = @($entryNames | Where-Object {
            $name = $_
            @($zipForbiddenPatterns | Where-Object { $name -match $_ }).Count -gt 0
        })
        if ($zipForbiddenEntries.Count -ne 0) {
            throw "Beta ZIP contains forbidden content: $($zipForbiddenEntries -join [Environment]::NewLine)"
        }
        $assetEntries = @($entryNames | Where-Object { $_ -match '(?i)^Assets/.+\.png$' })
        $expectedAssetCount = @(Get-ChildItem -LiteralPath $assetSource -Filter '*.png' -File).Count
        if ($assetEntries.Count -ne $expectedAssetCount) {
            throw "Beta ZIP contains $($assetEntries.Count) PNG assets; expected $expectedAssetCount."
        }
        $pathPatterns = @(
            '(?i)[a-z]:[\\/]Users[\\/][^\\/\x00]+',
            '(?i)/Users/[^/\x00]+',
            '(?i)/home/[^/\x00]+'
        )
        $pathHits = @()
        foreach ($entry in $entries) {
            if ($entry.Length -eq 0 -or $entry.Length -gt 32MB) { continue }
            $stream = $entry.Open()
            $memory = [IO.MemoryStream]::new()
            try {
                $stream.CopyTo($memory)
                $bytes = $memory.ToArray()
            } finally {
                $memory.Dispose()
                $stream.Dispose()
            }
            $texts = @(
                [Text.Encoding]::UTF8.GetString($bytes),
                [Text.Encoding]::Unicode.GetString($bytes)
            )
            if (@($texts | Where-Object {
                $text = $_
                @($pathPatterns | Where-Object { $text -match $_ }).Count -gt 0
            }).Count -gt 0) {
                $pathHits += $entry.FullName.Replace('\', '/')
            }
        }
        if ($pathHits.Count -ne 0) {
            throw "Beta ZIP contains developer-machine paths: $($pathHits -join [Environment]::NewLine)"
        }
        $licenseEntry = $archive.GetEntry('LICENSE')
        $licenseStream = $licenseEntry.Open()
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $zipLicenseHash = [BitConverter]::ToString($sha256.ComputeHash($licenseStream)).Replace('-', '')
        } finally {
            $sha256.Dispose()
            $licenseStream.Dispose()
        }
        $repositoryLicenseHash = (Get-FileHash -LiteralPath $repositoryLicense -Algorithm SHA256).Hash
        if ($zipLicenseHash -ne $repositoryLicenseHash) {
            throw 'Beta ZIP LICENSE hash differs from the repository LICENSE.'
        }
    } finally {
        $archive.Dispose()
    }
} finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}

$zipHash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
$zipSize = (Get-Item -LiteralPath $OutputPath).Length
Write-Host "Unsigned portable Windows beta created: $OutputPath"
Write-Host "Cuelet $version / PE $expectedPackageVersion / x64 / Windows 10 1809+"
Write-Host "ZIP size: $zipSize bytes"
Write-Host "ZIP SHA-256: $zipHash"
exit 0
