[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackagePath,
    [Parameter(Mandatory)]
    [string]$CertificateThumbprint,
    [Parameter(Mandatory)]
    [string]$OutputPath,
    [string]$TimestampUrl,
    [switch]$AllowDevelopmentPublisher
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$identity = Import-PowerShellDataFile -LiteralPath (
    Join-Path $windowsRoot 'release-identity.psd1')
$manifestPath = Join-Path $windowsRoot 'Cuelet.WinUI\Package.appxmanifest'

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Package not found: $PackagePath"
}
$extension = [IO.Path]::GetExtension($PackagePath).ToLowerInvariant()
if ($extension -notin @('.appx', '.msix')) {
    throw 'PackagePath must identify an AppX or MSIX package.'
}
$source = [IO.Path]::GetFullPath($PackagePath)
$destination = [IO.Path]::GetFullPath($OutputPath)
if ($source -eq $destination) {
    throw 'OutputPath must differ from PackagePath so the unsigned artifact is preserved.'
}
if (Test-Path -LiteralPath $destination) {
    throw "OutputPath already exists: $destination"
}

[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
$namespaces = [Xml.XmlNamespaceManager]::new($manifest.NameTable)
$namespaces.AddNamespace(
    'p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$manifestIdentity = $manifest.SelectSingleNode(
    '/p:Package/p:Identity', $namespaces)
if ($manifestIdentity.Name -ne $identity.PackageIdentityName -or
    $manifestIdentity.Publisher -ne $identity.Publisher) {
    throw 'Package.appxmanifest does not match release-identity.psd1.'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($source)
try {
    $entry = $archive.GetEntry('AppxManifest.xml')
    if (-not $entry) {
        throw 'The package does not contain AppxManifest.xml.'
    }
    $reader = [IO.StreamReader]::new($entry.Open())
    try {
        [xml]$packagedManifest = $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
    }
} finally {
    $archive.Dispose()
}
$packagedNamespaces = [Xml.XmlNamespaceManager]::new(
    $packagedManifest.NameTable)
$packagedNamespaces.AddNamespace(
    'p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$packagedIdentity = $packagedManifest.SelectSingleNode(
    '/p:Package/p:Identity', $packagedNamespaces)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $windowsRoot '..\..'))
$applicationVersion = (
    Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
$expectedVersion = "$applicationVersion.$($identity.VersionRevision)"
if ($packagedIdentity.Name -ne $identity.PackageIdentityName -or
    $packagedIdentity.Publisher -ne $identity.Publisher -or
    $packagedIdentity.Version -ne $expectedVersion -or
    $packagedIdentity.ProcessorArchitecture -ne $identity.Architecture) {
    throw 'The package identity, version, or architecture does not match the configured Windows release.'
}
if ($identity.DevelopmentPublisher -and -not $AllowDevelopmentPublisher) {
    throw @'
The configured Publisher is explicitly a development placeholder. Replace the
manifest and release-identity.psd1 with the real Store/certificate identity, or
pass -AllowDevelopmentPublisher only for a disposable local installation test.
'@
}

$thumbprint = ($CertificateThumbprint -replace '\s', '').ToUpperInvariant()
$certificate = $null
$certificateStore = $null
foreach ($store in @('Cert:\CurrentUser\My', 'Cert:\LocalMachine\My')) {
    $candidate = Get-Item -LiteralPath (Join-Path $store $thumbprint) `
        -ErrorAction SilentlyContinue
    if ($candidate) {
        $certificate = $candidate
        $certificateStore = $store
        break
    }
}
if (-not $certificate) {
    throw 'The requested certificate was not found in a Personal certificate store.'
}
if (-not $certificate.HasPrivateKey) {
    throw 'The requested signing certificate has no accessible private key.'
}
if ($certificate.NotBefore -gt (Get-Date) -or $certificate.NotAfter -le (Get-Date)) {
    throw 'The requested signing certificate is not currently valid.'
}
if ($certificate.Subject -ne $identity.Publisher) {
    throw "Certificate subject '$($certificate.Subject)' does not match manifest Publisher '$($identity.Publisher)'."
}
$codeSigningOid = '1.3.6.1.5.5.7.3.3'
$hasCodeSigningUsage = @($certificate.EnhancedKeyUsageList |
    Where-Object { $_.ObjectId -eq $codeSigningOid }).Count -gt 0
if (-not $hasCodeSigningUsage) {
    throw 'The requested certificate is not valid for code signing.'
}

$kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$signTool = Get-ChildItem -LiteralPath $kitsBin -Recurse -Filter signtool.exe |
    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $signTool) {
    throw 'SignTool.exe from the Windows SDK was not found.'
}

$outputDirectory = Split-Path -Parent $destination
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
Copy-Item -LiteralPath $source -Destination $destination
$arguments = @('sign', '/sha1', $thumbprint, '/fd', 'SHA256')
if ($certificateStore -like 'Cert:\LocalMachine\*') {
    $arguments += '/sm'
}
if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
    $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256')
}
$arguments += $destination
& $signTool @arguments
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
    throw "SignTool failed with exit code $LASTEXITCODE."
}

& $signTool verify /pa /v $destination
if ($LASTEXITCODE -ne 0) {
    throw "SignTool verification failed with exit code $LASTEXITCODE."
}
$hash = Get-FileHash -LiteralPath $destination -Algorithm SHA256
Write-Host "Signed package: $destination"
Write-Host "Certificate: $($certificate.Subject) [$thumbprint]"
Write-Host "SHA-256: $($hash.Hash)"
