[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',
    [switch]$SkipBuild,
    [switch]$PrepareSubmission,
    [string]$SignedCatalogPath,
    [switch]$AllowTestPackage,
    [string]$TestCertificateThumbprint,
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$driverRoot = Join-Path $windowsRoot 'Cuelet.VirtualAudio.Driver'
$generatedSysvad = Join-Path $driverRoot 'obj\sysvad\audio\sysvad'
$outputRoot = Join-Path $windowsRoot "$Architecture\$Configuration"
$package = if (-not [string]::IsNullOrWhiteSpace($PackageDirectory)) {
    [IO.Path]::GetFullPath($PackageDirectory)
} elseif ($PrepareSubmission) {
    Join-Path $outputRoot 'Cuelet.VirtualAudio.UnsignedSubmission'
} else {
    Join-Path $outputRoot 'DriverPackage'
}
$resolvedOutputRoot = [IO.Path]::GetFullPath($outputRoot)
$resolvedPackage = [IO.Path]::GetFullPath($package)
if (-not $resolvedPackage.StartsWith(
    $resolvedOutputRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) {
    throw "The driver package must remain below $resolvedOutputRoot."
}
$package = $resolvedPackage
$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$wdkVersion = '10.0.26100.0'
$inf2Cat = Join-Path $kitsRoot "bin\$wdkVersion\x86\Inf2Cat.exe"
$infVerif = Join-Path $kitsRoot "Tools\$wdkVersion\x64\InfVerif.exe"
$signTool = Join-Path $kitsRoot "bin\$wdkVersion\x64\signtool.exe"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-virtual-audio-driver.ps1') `
        -Configuration $Configuration -Architecture $Architecture
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path -LiteralPath $inf2Cat)) {
    throw "Inf2Cat.exe from WDK $wdkVersion was not found."
}
if (-not (Test-Path -LiteralPath $infVerif)) {
    throw "InfVerif.exe from WDK $wdkVersion was not found."
}
if (-not (Test-Path -LiteralPath $signTool)) {
    throw "SignTool.exe from WDK $wdkVersion was not found."
}
if ($Configuration -eq 'Debug') {
    if (-not $AllowTestPackage) {
        throw @'
Debug packaging is developer-only. Re-run with -AllowTestPackage and provide a
test certificate thumbprint. This script never enables test-signing and never
installs a trust root.
'@
    }
    if ([string]::IsNullOrWhiteSpace($TestCertificateThumbprint)) {
        throw @'
An installable Debug package requires -TestCertificateThumbprint. Cuelet
test-signs both the driver image and its generated catalog so the package also
works on development systems with Memory Integrity enabled.
'@
    }
}

$driverOutput = Join-Path $generatedSysvad "TabletAudioSample\$Architecture\$Configuration"
$driverBinary = Join-Path $driverOutput 'CueletVirtualAudio.sys'
$driverInf = Join-Path $driverOutput 'CueletVirtualAudio.inf'
$driverPdb = Join-Path $driverOutput 'CueletVirtualAudio.pdb'
if (-not (Test-Path -LiteralPath $driverBinary -PathType Leaf) -or
    -not (Test-Path -LiteralPath $driverInf -PathType Leaf)) {
    throw 'The real WDK build did not produce CueletVirtualAudio.sys and CueletVirtualAudio.inf.'
}
if ($Configuration -eq 'Debug' -and
    -not (Test-Path -LiteralPath $driverPdb -PathType Leaf)) {
    throw 'The Debug WDK build did not preserve CueletVirtualAudio.pdb.'
}

& $infVerif /w $driverInf
if ($LASTEXITCODE -ne 0) {
    throw 'InfVerif rejected the Cuelet driver INF.'
}

if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath $driverBinary -Destination $package
Copy-Item -LiteralPath $driverInf -Destination $package
if ($Configuration -eq 'Debug') {
    Copy-Item -LiteralPath $driverPdb -Destination $package
}

if ($Configuration -eq 'Debug') {
    $packagedDriver = Join-Path $package 'CueletVirtualAudio.sys'
    & $signTool sign /sha1 $TestCertificateThumbprint /fd sha256 $packagedDriver
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $package -Recurse -Force
        throw 'Signing the Debug driver image with the selected test certificate failed.'
    }
}

$osTargets = if ($Architecture -eq 'x64') {
    '10_X64,Server10_X64'
} else {
    '10_ARM64,Server10_ARM64'
}
& $inf2Cat "/driver:$package" "/os:$osTargets" /uselocaltime
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $package -Recurse -Force
    throw 'Inf2Cat rejected the Cuelet driver package.'
}

$catalog = Join-Path $package 'CueletVirtualAudio.cat'
if (-not (Test-Path -LiteralPath $catalog)) {
    throw 'Inf2Cat did not produce CueletVirtualAudio.cat.'
}

if ($PrepareSubmission) {
    if ($Configuration -ne 'Release') {
        throw 'Hardware Dev Center submission packages must be created from Release.'
    }
    Write-Warning @'
This is an unsigned submission artifact, not an end-user driver package.
Submit it through the Microsoft Hardware Dev Center signing process. Do not put
this folder next to the Cuelet installer helper.
'@
    Write-Host "Unsigned submission package: $package"
    exit 0
}

if ($Configuration -eq 'Release') {
    if ([string]::IsNullOrWhiteSpace($SignedCatalogPath) -or
        -not (Test-Path -LiteralPath $SignedCatalogPath)) {
        Remove-Item -LiteralPath $package -Recurse -Force
        throw @'
Release packaging requires -SignedCatalogPath pointing to the Microsoft-signed
catalog returned for this exact package. An ordinary local Authenticode or
test certificate is not accepted as a production-signing substitute.
'@
    }
    Copy-Item -LiteralPath $SignedCatalogPath -Destination $catalog -Force
    & $signTool verify /kp /v $catalog
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $package -Recurse -Force
        throw 'The supplied Release catalog failed kernel-mode signature verification.'
    }
}
else {
    & $signTool sign /sha1 $TestCertificateThumbprint /fd sha256 $catalog
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $package -Recurse -Force
        throw 'Signing the Debug catalog with the selected test certificate failed.'
    }
    $certificate = Get-Item -LiteralPath (
        "Cert:\CurrentUser\My\$TestCertificateThumbprint") -ErrorAction Stop
    $certificatePath = Join-Path $outputRoot 'CueletVirtualAudioDevelopment.cer'
    Export-Certificate -Cert $certificate -FilePath $certificatePath -Force |
        Out-Null
    Write-Host "Development certificate: $certificatePath"
}

Write-Host "Cuelet driver package: $package"
