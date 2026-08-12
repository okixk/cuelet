[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Package,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $windowsRoot 'Cuelet.Windows.sln'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'vswhere.exe was not found. Install Visual Studio 2026 with Desktop development with C++ and Windows application development.'
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw 'Visual Studio with the MSVC x64 tools was not found.'
}

$msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found at $msbuild"
}

& (Join-Path $PSScriptRoot 'generate-windows-icon.ps1')

Write-Host "Restoring Cuelet Windows dependencies..."
& $msbuild $solution /t:Restore /p:RestorePackagesConfig=true /p:Configuration=$Configuration /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
$arguments = @(
    $solution,
    '/m',
    $target,
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/v:minimal'
)
if ($Package) {
    $arguments += '/p:GenerateAppxPackageOnBuild=true'
    $arguments += '/p:AppxBundle=Never'
    $arguments += '/p:AppxPackageSigningEnabled=false'
}

$action = if ($Rebuild) { 'Rebuilding' } else { 'Building' }
Write-Host "$action Cuelet Windows ($Configuration, x64)..."
& $msbuild @arguments
exit $LASTEXITCODE
