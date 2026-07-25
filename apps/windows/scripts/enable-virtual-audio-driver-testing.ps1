[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',
    [string]$CertificatePath
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($CertificatePath)) {
    $CertificatePath = Join-Path $windowsRoot (
        "$Architecture\Debug\CueletVirtualAudioDevelopment.cer")
}
$CertificatePath = [IO.Path]::GetFullPath($CertificatePath)

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdministrator) {
    throw @"
This one-time development-machine setup requires an elevated PowerShell.
Re-open PowerShell with Run as administrator, then run:

powershell -ExecutionPolicy Bypass -File "$PSCommandPath" -Architecture $Architecture -CertificatePath "$CertificatePath"
"@
}
if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
    throw @"
The exported Cuelet development certificate was not found:
$CertificatePath

Create the signed Debug package first with package-virtual-audio-driver.ps1.
"@
}

$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $CertificatePath)
if ($certificate.Subject -ne 'CN=Cuelet Virtual Audio Development') {
    throw "Refusing to trust an unexpected certificate: $($certificate.Subject)"
}

Import-Certificate -FilePath $CertificatePath `
    -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath $CertificatePath `
    -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null

& bcdedit.exe /set testsigning on
if ($LASTEXITCODE -ne 0) {
    throw "BCDEdit could not enable Windows test-signing mode (exit $LASTEXITCODE)."
}

Write-Host 'Cuelet development certificate trusted for this machine.'
Write-Warning @'
Windows test-signing is enabled for the next boot. Restart Windows before
installing the Debug driver. Test mode is for local driver development only.
'@
