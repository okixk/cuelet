[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $OutputPath,
    [string] $ProcessName = 'Cuelet'
)

$ErrorActionPreference = 'Stop'
$capturePath = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
$windowProcess = Get-Process -Name $ProcessName -ErrorAction Stop |
    Where-Object { $_.MainWindowHandle -ne 0 } |
    Select-Object -First 1
if (-not $windowProcess) { throw "No visible $ProcessName window was found." }

$directory = Split-Path -Parent $capturePath
New-Item -ItemType Directory -Path $directory -Force | Out-Null
& ffmpeg.exe -y -f gdigrab -framerate 1 -i ("title=" + $ProcessName) -frames:v 1 $capturePath 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $capturePath)) {
    throw "The native window capture failed."
}
Write-Output $capturePath
