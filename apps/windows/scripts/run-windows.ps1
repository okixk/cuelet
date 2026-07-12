[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if ($Configuration -eq 'Release') {
    throw 'Release is an MSIX-packaged build. Run Debug directly, or install and launch the package produced by package-windows.ps1.'
}
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$executable = Join-Path $windowsRoot "x64\$Configuration\Cuelet.WinUI\Cuelet.WinUI.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Cuelet was not found at $executable. Run build-windows.ps1 first."
}

Start-Process -FilePath $executable
