$ErrorActionPreference = "Stop"

$root = "C:\Users\oki\projects\cuelet"
$windowsRoot = Join-Path $root "apps\windows"
$solution = Join-Path $windowsRoot "Cuelet.Windows.sln"
$appDir = Join-Path $windowsRoot "x64\Release\Cuelet.WinUI"
$manifest = Join-Path $appDir "AppxManifest.xml"

Set-Location $root

Write-Host ""
Write-Host "=== Stopping old Cuelet build ===" -ForegroundColor Cyan

Get-Process "Cuelet.WinUI" -ErrorAction SilentlyContinue |
    Stop-Process -Force

Get-AppxPackage -Name "ch.oki.cuelet" |
    Remove-AppxPackage -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== Correcting dependency publisher typo ===" -ForegroundColor Cyan

$extensions = @(
    ".xml",
    ".props",
    ".targets",
    ".manifest",
    ".appxmanifest",
    ".vcxproj",
    ".config",
    ".ps1",
    ".json"
)

Get-ChildItem $windowsRoot -Recurse -File |
    Where-Object { $_.Extension -in $extensions } |
    ForEach-Object {
        $content = [System.IO.File]::ReadAllText($_.FullName)

        if ($content.Contains("Microsoft Corporation")) {
            Write-Host "Fixing: $($_.FullName)"

            $content = $content.Replace(
                "Microsoft Corporation",
                "Microsoft Corporation"
            )

            [System.IO.File]::WriteAllText(
                $_.FullName,
                $content,
                [System.Text.UTF8Encoding]::new($false)
            )
        }
    }

Write-Host ""
Write-Host "=== Finding MSBuild ===" -ForegroundColor Cyan

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found. Check that Visual Studio is installed."
}

$msbuild = & $vswhere `
    -latest `
    -products * `
    -requires Microsoft.Component.MSBuild `
    -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1

if (-not $msbuild) {
    throw "MSBuild was not found in the Visual Studio installation."
}

Write-Host "Using: $msbuild"

Write-Host ""
Write-Host "=== Restoring NuGet packages ===" -ForegroundColor Cyan

& $msbuild $solution `
    /t:Restore `
    /p:RestorePackagesConfig=true `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /m

if ($LASTEXITCODE -ne 0) {
    throw "NuGet restore failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "=== Rebuilding Cuelet Release x64 ===" -ForegroundColor Cyan

& $msbuild $solution `
    /t:Rebuild `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /m

if ($LASTEXITCODE -ne 0) {
    throw "Cuelet build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path $manifest)) {
    throw "The generated manifest was not found at: $manifest"
}

# Safety check for the generated manifest.
$generatedManifest = [System.IO.File]::ReadAllText($manifest)

if ($generatedManifest.Contains("Microsoft Corporation")) {
    $generatedManifest = $generatedManifest.Replace(
        "Microsoft Corporation",
        "Microsoft Corporation"
    )

    [System.IO.File]::WriteAllText(
        $manifest,
        $generatedManifest,
        [System.Text.UTF8Encoding]::new($false)
    )
}

Write-Host ""
Write-Host "=== Registering new Cuelet build ===" -ForegroundColor Cyan

Add-AppxPackage `
    -Register $manifest `
    -ForceApplicationShutdown

[xml]$xml = Get-Content -LiteralPath $manifest

$packageName = $xml.Package.Identity.Name
$appId = @($xml.Package.Applications.Application)[0].Id

$package = Get-AppxPackage -Name $packageName |
    Sort-Object Version -Descending |
    Select-Object -First 1

if (-not $package) {
    throw "Cuelet was built but could not be registered."
}

$appUserModelId = "$($package.PackageFamilyName)!$appId"

Write-Host ""
Write-Host "=== Launching Cuelet ===" -ForegroundColor Green

Start-Process explorer.exe `
    -ArgumentList "shell:AppsFolder\$appUserModelId"