[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') `
        -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$helper = Join-Path $windowsRoot (
    "x64\$Configuration\Cuelet.VirtualAudio.Installer.exe")
$flowTest = Join-Path $windowsRoot (
    "x64\$Configuration\Cuelet.VirtualAudio.FlowTest.exe")
if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) {
    throw "The virtual-audio installer helper was not found: $helper"
}
if (-not (Test-Path -LiteralPath $flowTest -PathType Leaf)) {
    throw "The WASAPI flow verifier was not found: $flowTest"
}

$statusText = (& $helper status --json) -join ''
if ($LASTEXITCODE -ne 0) {
    throw "Cuelet driver status failed with exit code $LASTEXITCODE."
}
$status = $statusText | ConvertFrom-Json
if (-not $status.packageInstalled -or
    -not $status.renderEndpointPresent -or
    -not $status.captureEndpointPresent -or
    -not $status.endpointPairValid) {
    throw "Cuelet's complete paired endpoint is not installed: $statusText"
}

Write-Host $statusText
& $flowTest
exit $LASTEXITCODE
