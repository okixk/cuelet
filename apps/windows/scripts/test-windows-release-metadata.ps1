[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $windowsRoot '..\..'))
$projectRoot = Join-Path $windowsRoot 'Cuelet.WinUI'
$assetRoot = Join-Path $projectRoot 'Assets'

$version = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain a three-component application version; found '$version'."
}
$packageVersion = "$version.0"

$packagePath = Join-Path $projectRoot 'Package.appxmanifest'
[xml]$package = Get-Content -LiteralPath $packagePath -Raw
$packageNamespaces = [Xml.XmlNamespaceManager]::new($package.NameTable)
$packageNamespaces.AddNamespace('p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$packageNamespaces.AddNamespace('uap', 'http://schemas.microsoft.com/appx/manifest/uap/windows10')
$identity = $package.SelectSingleNode('/p:Package/p:Identity', $packageNamespaces)
$properties = $package.SelectSingleNode('/p:Package/p:Properties', $packageNamespaces)
$application = $package.SelectSingleNode('/p:Package/p:Applications/p:Application', $packageNamespaces)
$visuals = $application.SelectSingleNode('uap:VisualElements', $packageNamespaces)
$tile = $visuals.SelectSingleNode('uap:DefaultTile', $packageNamespaces)
$splash = $visuals.SelectSingleNode('uap:SplashScreen', $packageNamespaces)
$targetFamily = $package.SelectSingleNode('/p:Package/p:Dependencies/p:TargetDeviceFamily', $packageNamespaces)

$developmentPublisher = 'CN=Cuelet Development'
if ($identity.Name -ne 'ch.oki.cuelet' -or $identity.Publisher -ne $developmentPublisher -or
    $identity.Version -ne $packageVersion) {
    throw "MSIX identity/version must be ch.oki.cuelet $packageVersion."
}
if ($properties.DisplayName -ne 'Cuelet' -or $properties.PublisherDisplayName -ne 'Cuelet' -or
    $visuals.DisplayName -ne 'Cuelet' -or $application.Id -ne 'App') {
    throw 'Cuelet package/application display metadata is inconsistent.'
}
if ($targetFamily.Name -ne 'Windows.Desktop' -or
    $targetFamily.MinVersion -ne '10.0.17763.0' -or
    $targetFamily.MaxVersionTested -ne '10.0.26100.0') {
    throw 'Windows desktop minimum/target metadata changed unexpectedly.'
}

$nativeManifestPath = Join-Path $projectRoot 'app.manifest'
[xml]$nativeManifest = Get-Content -LiteralPath $nativeManifestPath -Raw
$nativeNamespaces = [Xml.XmlNamespaceManager]::new($nativeManifest.NameTable)
$nativeNamespaces.AddNamespace('asm', 'urn:schemas-microsoft-com:asm.v1')
$nativeIdentity = $nativeManifest.SelectSingleNode('/asm:assembly/asm:assemblyIdentity', $nativeNamespaces)
if ($nativeIdentity.version -ne $packageVersion) {
    throw "Native executable manifest version must be $packageVersion."
}

[xml]$project = Get-Content -LiteralPath (Join-Path $projectRoot 'Cuelet.WinUI.vcxproj') -Raw
$projectNamespaces = [Xml.XmlNamespaceManager]::new($project.NameTable)
$projectNamespaces.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$configurations = @($project.SelectNodes('//m:ProjectConfiguration', $projectNamespaces) |
    ForEach-Object { $_.Include } | Sort-Object)
if (($configurations -join ',') -ne 'Debug|x64,Release|x64') {
    throw "Cuelet release architecture must remain x64; found $($configurations -join ', ')."
}

$expectedAssets = [Collections.Generic.List[string]]::new()
foreach ($name in @('LargeTile', 'SmallTile', 'SplashScreen', 'Square150x150Logo',
                     'Square44x44Logo', 'StoreLogo', 'Wide310x150Logo')) {
    foreach ($scale in @(100, 125, 150, 200, 400)) {
        $expectedAssets.Add("$name.scale-$scale.png")
    }
}
foreach ($size in @(16, 24, 32, 48, 256)) {
    $expectedAssets.Add("Square44x44Logo.targetsize-$size.png")
    $expectedAssets.Add("Square44x44Logo.altform-lightunplated_targetsize-$size.png")
    if ($size -eq 24) {
        $expectedAssets.Add('Square44x44Logo.targetsize-24_altform-unplated.png')
    } else {
        $expectedAssets.Add("Square44x44Logo.altform-unplated_targetsize-$size.png")
    }
}

$actualAssets = @(Get-ChildItem -LiteralPath $assetRoot -Filter '*.png' -File |
    ForEach-Object { $_.Name } | Sort-Object)
$differences = @(Compare-Object ($expectedAssets | Sort-Object) $actualAssets)
if ($differences.Count -ne 0) {
    throw "Windows PNG asset set is incomplete or contains unexpected files:`n$($differences | Out-String)"
}

function Test-MsixAssetReference([string]$Reference) {
    if ([string]::IsNullOrWhiteSpace($Reference) -or
        [IO.Path]::GetExtension($Reference) -ne '.png') {
        return $false
    }
    $name = [IO.Path]::GetFileNameWithoutExtension($Reference)
    return @(Get-ChildItem -LiteralPath $assetRoot -Filter "$name*.png" -File).Count -gt 0
}

$references = @(
    [string]$properties.Logo,
    [string]$visuals.Square150x150Logo,
    [string]$visuals.Square44x44Logo,
    [string]$tile.Square71x71Logo,
    [string]$tile.Wide310x150Logo,
    [string]$tile.Square310x310Logo,
    [string]$splash.Image
)
foreach ($reference in $references) {
    if (-not (Test-MsixAssetReference $reference)) {
        throw "MSIX image reference does not resolve to a prepared resource: $reference"
    }
}

& (Join-Path $PSScriptRoot 'generate-windows-icon.ps1') -Check
Write-Warning 'The MSIX is unsigned and retains the existing development publisher placeholder. Public packages require the real Store/certificate identity and signing.'
Write-Host "Windows release metadata is consistent: Cuelet $version -> MSIX $packageVersion, x64, Windows 10 1809+."
