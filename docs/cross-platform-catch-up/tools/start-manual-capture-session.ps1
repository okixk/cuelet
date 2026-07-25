[CmdletBinding()]
param(
    [string] $Configuration = 'Debug',
    [string] $SessionPath
)

$ErrorActionPreference = 'Stop'
$docsRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $docsRoot)
if ([string]::IsNullOrWhiteSpace($SessionPath)) { $SessionPath = Join-Path $docsRoot 'metadata/manual-capture-session.json' }
$captureScript = Join-Path $PSScriptRoot 'capture-cuelet-window.ps1'
$runScript = Join-Path $repoRoot 'apps/windows/scripts/run-windows.ps1'
$states = @(
    'app-default-window','library-populated','library-playing-sound','favorites-view','recent-view','uncategorized-view','category-selected','category-editor','search-results','search-no-results','sound-context-menu','rename-sound','shortcut-capture','mini-player-playing','settings-audio-routing','settings-virtual-microphone-connected','settings-driver-diagnostics-expanded','settings-microphone-mixing','settings-local-playback','tray-context-menu','navigation-collapsed','window-maximized','empty-category'
)

New-Item -ItemType Directory -Path (Split-Path -Parent $SessionPath) -Force | Out-Null
if (Test-Path -LiteralPath $SessionPath) {
    $session = Get-Content -Raw -LiteralPath $SessionPath | ConvertFrom-Json
} else {
    $session = [pscustomobject]@{ startedAt = (Get-Date).ToUniversalTime().ToString('o'); executable = $null; realtekConfirmed = $false; captures = [pscustomobject]@{} }
}

& $runScript -Configuration $Configuration -NoBuild -AllowTestDriver
$exe = Join-Path $repoRoot "apps/windows/x64/$Configuration/Cuelet.WinUI/Cuelet.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Expected executable was not found: $exe" }
$process = Get-Process -Name Cuelet -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Sort-Object StartTime -Descending | Select-Object -First 1
$session.executable = $exe

$realtek = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'Realtek' -and $_.Class -match 'MEDIA|AudioEndpoint' -and $_.Status -eq 'OK' }
if (-not $realtek) { throw 'No present OK Realtek audio device was found; refusing to continue.' }
Write-Host "Executable: $exe"
Write-Host "Realtek audio device(s) detected: $($realtek.FriendlyName -join '; ')"
$confirmation = Read-Host 'Confirm Windows normal output is still the physical Realtek device (type YES)'
if ($confirmation -cne 'YES') { throw 'Physical-output confirmation was not provided; no capture taken.' }
$session.realtekConfirmed = $true

while ($true) {
    Write-Host "`nCapture states (already completed are marked [x]):"
    for ($i = 0; $i -lt $states.Count; $i++) {
        $mark = if ($session.captures.PSObject.Properties.Name -contains $states[$i]) { '[x]' } else { '[ ]' }
        Write-Host ("{0,2}. {1} {2}" -f ($i + 1), $mark, $states[$i])
    }
    $choice = Read-Host 'Enter a state number/name, or FINISH'
    if ($choice -match '^finish$') { break }
    $name = $choice
    if ($choice -match '^\d+$' -and [int]$choice -ge 1 -and [int]$choice -le $states.Count) { $name = $states[[int]$choice - 1] }
    if ($states -notcontains $name) { Write-Warning "Unknown state '$name'. Choose a listed state."; continue }
    & $captureScript -Name $name
    if (-not $session.captures) { $session.captures = [pscustomobject]@{} }
    $session.captures | Add-Member -NotePropertyName $name -NotePropertyValue ([pscustomobject]@{ capturedAt = (Get-Date).ToUniversalTime().ToString('o'); path = "screenshots/$name.png" }) -Force
    $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $SessionPath -Encoding utf8
    Write-Host "Progress saved to $SessionPath"
}
$session.finishedAt = (Get-Date).ToUniversalTime().ToString('o')
$session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $SessionPath -Encoding utf8
Write-Host "Session finished. Progress remains in $SessionPath"
