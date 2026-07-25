[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',
    [switch]$Rebuild,
    [switch]$Clean,
    [switch]$PrepareOnly,
    [switch]$Analyze
)

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$driverRoot = Join-Path $windowsRoot 'Cuelet.VirtualAudio.Driver'
$generatedRoot = Join-Path $driverRoot 'obj\sysvad'
$prepare = Join-Path $driverRoot 'prepare-driver-source.ps1'
$sysvad = Join-Path $generatedRoot 'audio\sysvad'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if ($Clean) {
    $resolvedDriverRoot = [IO.Path]::GetFullPath($driverRoot)
    $resolvedGeneratedRoot = [IO.Path]::GetFullPath($generatedRoot)
    if (-not $resolvedGeneratedRoot.StartsWith(
        $resolvedDriverRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a generated path outside the driver component.'
    }
    if (Test-Path -LiteralPath $resolvedGeneratedRoot) {
        Remove-Item -LiteralPath $resolvedGeneratedRoot -Recurse -Force
    }
    if (-not $Rebuild -and -not $PrepareOnly) {
        Write-Host 'Cleaned generated Cuelet virtual-audio source.'
        exit 0
    }
}

& $prepare -Destination $generatedRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
if ($PrepareOnly) {
    exit 0
}

$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$driverTargets = Join-Path $kitsRoot 'build\10.0.26100.0\WindowsDriver.Common.targets'
$kernelHeaders = Join-Path $kitsRoot 'Include\10.0.26100.0\km'
if (-not (Test-Path -LiteralPath $driverTargets) -or
    -not (Test-Path -LiteralPath $kernelHeaders)) {
    throw @'
The Windows Driver Kit is not installed. Install the WDK matching Windows SDK
10.0.26100 and the Visual Studio Windows driver workload. Cuelet will not
substitute an unsigned binary or enable Windows test-signing automatically.
'@
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'vswhere.exe was not found. Install Visual Studio with C++ driver development tools.'
}
$installationPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
$msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found at $msbuild."
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$commonProject = Join-Path $sysvad 'EndpointsCommon\EndpointsCommon.vcxproj'
$driverProject = Join-Path $sysvad 'TabletAudioSample\TabletAudioSample.vcxproj'
$arguments = @(
    "/t:$target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Architecture",
    '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    '/p:VisualStudioVersion=17.0',
    '/p:TreatWarningAsError=true',
    '/p:SignMode=Off',
    '/v:minimal'
)
if ($Analyze) {
    $arguments += '/p:RunCodeAnalysis=true'
    $arguments += '/p:CueletRunDriverAnalysis=true'
}

Write-Host "Building pinned SysVAD common library ($Configuration, $Architecture)..."
& $msbuild $commonProject @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building Cuelet render-to-capture driver ($Configuration, $Architecture)..."
& $msbuild $driverProject @arguments
exit $LASTEXITCODE
