[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'test-windows-release-metadata.ps1')
& (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration -Package
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Configuration -eq 'Release') {
    $windowsRoot = Split-Path -Parent $PSScriptRoot
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $windowsRoot '..\..'))
    $version = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
    $releaseIdentity = Import-PowerShellDataFile -LiteralPath (
        Join-Path $windowsRoot 'release-identity.psd1')
    $packageVersion = "$version.$($releaseIdentity.VersionRevision)"
    $packagePath = Join-Path $windowsRoot (
        "AppPackages\Cuelet.WinUI\Cuelet.WinUI_${packageVersion}_x64_Test\" +
        "Cuelet.WinUI_${packageVersion}_x64.msix")
    if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
        throw "The expected unsigned Release MSIX was not generated: $packagePath"
    }
    & (Join-Path $PSScriptRoot 'audit-windows-package.ps1') -PackagePath $packagePath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

exit 0
