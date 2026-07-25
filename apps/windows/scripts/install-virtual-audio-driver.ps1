[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$AllowTestPackage
)

$ErrorActionPreference = 'Stop'
if ($Configuration -eq 'Release' -and $AllowTestPackage) {
    throw 'Release installation never accepts the developer test-package flag.'
}

$windowsRoot = Split-Path -Parent $PSScriptRoot
$helper = Join-Path $windowsRoot "x64\$Configuration\Cuelet.VirtualAudio.Installer.exe"
$package = Join-Path $windowsRoot "x64\$Configuration\DriverPackage"
if (-not (Test-Path -LiteralPath $helper)) {
    throw "Build Cuelet.VirtualAudio.Installer first: $helper"
}
if (-not (Test-Path -LiteralPath (Join-Path $package 'CueletVirtualAudio.inf'))) {
    throw "A packaged Cuelet driver was not found at $package."
}

if ($AllowTestPackage) {
    $env:CUELET_ALLOW_TEST_DRIVER = '1'
}
$arguments = 'install'
if ($AllowTestPackage) {
    $arguments += ' --allow-test-package'
}
$process = Start-Process -FilePath $helper -ArgumentList $arguments `
    -Verb RunAs -Wait -PassThru
exit $process.ExitCode
