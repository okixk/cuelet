[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$NoBuild,
    [switch]$AllowTestDriver
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
if ($Configuration -eq 'Release' -and $AllowTestDriver) {
    throw 'Release never accepts the developer test-driver flag.'
}
if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$executable = Join-Path $windowsRoot "x64\$Configuration\Cuelet.WinUI\Cuelet.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Cuelet was not found at $executable. Run build-windows.ps1 first."
}

if ($AllowTestDriver) {
    $env:CUELET_ALLOW_TEST_DRIVER = '1'
}
Start-Process -FilePath $executable
exit 0
