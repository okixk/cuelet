[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'test-windows-release-metadata.ps1')
& (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration -Package
exit $LASTEXITCODE
