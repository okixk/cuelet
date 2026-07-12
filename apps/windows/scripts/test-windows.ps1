[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$tests = Join-Path $windowsRoot "x64\$Configuration\Cuelet.Core.Tests.exe"
if (-not (Test-Path -LiteralPath $tests)) {
    throw "Cuelet Windows tests were not found at $tests."
}

& $tests
exit $LASTEXITCODE
