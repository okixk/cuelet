[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$AllowTestDriver
)

$ErrorActionPreference = 'Stop'
if ($Configuration -eq 'Release' -and $AllowTestDriver) {
    throw 'Release never accepts the developer test-driver flag.'
}

& (Join-Path $PSScriptRoot 'build-windows.ps1') `
    -Configuration $Configuration -Rebuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $PSScriptRoot 'run-windows.ps1') `
    -Configuration $Configuration -NoBuild `
    -AllowTestDriver:$AllowTestDriver
exit $LASTEXITCODE
