[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$helper = Join-Path $windowsRoot "x64\$Configuration\Cuelet.VirtualAudio.Installer.exe"
if (-not (Test-Path -LiteralPath $helper)) {
    throw "Build Cuelet.VirtualAudio.Installer first: $helper"
}
$process = Start-Process -FilePath $helper -ArgumentList 'uninstall' `
    -Verb RunAs -Wait -PassThru
exit $process.ExitCode
