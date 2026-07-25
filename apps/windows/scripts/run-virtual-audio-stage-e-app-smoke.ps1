[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$smokeRoot = Join-Path $EvidenceRoot 'application-smoke'
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'
$obsPath = 'C:\Program Files\obs-studio\bin\64bit\obs64.exe'
$discordPath = Get-ChildItem -LiteralPath (
    Join-Path $env:LOCALAPPDATA 'Discord') -Directory `
    -Filter 'app-*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object {
        Join-Path $_.FullName 'Discord.exe'
    } |
    Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } |
    Select-Object -First 1
$soundRecorder = Join-Path $env:LOCALAPPDATA (
    'Microsoft\WindowsApps\' +
    'Microsoft.WindowsSoundRecorder_8wekyb3d8bbwe.exe')
New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null

function Get-ProcessIds {
    param([string[]]$Names)
    return @(Get-Process -Name $Names -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Id)
}

function Get-NewProcesses {
    param([string[]]$Names, [int[]]$Before)
    return @(Get-Process -Name $Names -ErrorAction SilentlyContinue |
        Where-Object { $_.Id -notin $Before })
}

function Close-NewProcesses {
    param([Diagnostics.Process[]]$Processes)
    foreach ($process in $Processes) {
        try {
            $process.Refresh()
            if (-not $process.HasExited -and
                $process.MainWindowHandle -ne 0) {
                [void]$process.CloseMainWindow()
            }
        } catch {}
    }
    Start-Sleep -Seconds 3
    foreach ($process in $Processes) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
        } catch {}
    }
}

function Find-CueletLogLines {
    param([IO.FileInfo[]]$Files, [datetime]$Since)
    return @($Files | Where-Object {
        $_.LastWriteTime -ge $Since
    } | ForEach-Object {
        Select-String -LiteralPath $_.FullName `
            -Pattern '(?i)Cuelet Virtual (Audio|Microphone)' `
            -ErrorAction SilentlyContinue |
            ForEach-Object {
                [ordered]@{
                    file = $_.Path
                    lineNumber = $_.LineNumber
                    line = $_.Line.Trim()
                }
            }
    })
}

$result = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    soundSettings = [ordered]@{}
    soundRecorder = [ordered]@{
        installed = Test-Path -LiteralPath $soundRecorder -PathType Leaf
        tested = $false
        reason = 'Not installed; no unrelated application was added.'
    }
    obs = [ordered]@{
        installed = Test-Path -LiteralPath $obsPath -PathType Leaf
    }
    discord = [ordered]@{
        installed = [bool]$discordPath
    }
    passed = $false
}

$settingsProcesses = @()
$obsProcesses = @()
$discordProcesses = @()
try {
    $settingsBefore = @(Get-ProcessIds -Names @(
        'SystemSettings', 'ApplicationFrameHost'))
    Start-Process 'ms-settings:sound'
    Start-Sleep -Seconds 8
    $settingsProcesses = @(Get-NewProcesses -Names @(
        'SystemSettings', 'ApplicationFrameHost') -Before $settingsBefore)
    $result.soundSettings = [ordered]@{
        launchSucceeded = $true
        newProcesses = @($settingsProcesses |
            Select-Object Id, ProcessName, MainWindowTitle, Responding)
        cueletCaptureEndpoint = Get-PnpDevice -Class AudioEndpoint `
            -ErrorAction SilentlyContinue | Where-Object {
                $_.FriendlyName -match (
                    '(?i)^Cuelet Virtual Microphone ' +
                    '\(Cuelet Virtual Audio Device\)$')
            } | Select-Object Status, FriendlyName, InstanceId
    }
    Close-NewProcesses -Processes $settingsProcesses
    $settingsProcesses = @()

    if ($result.obs.installed) {
        $obsStarted = Get-Date
        $obsBefore = @(Get-ProcessIds -Names @('obs64'))
        Start-Process -FilePath $obsPath `
            -WorkingDirectory (Split-Path $obsPath) `
            -ArgumentList @('--multi', '--minimize-to-tray') | Out-Null
        Start-Sleep -Seconds 12
        $obsProcesses = @(Get-NewProcesses -Names @('obs64') `
            -Before $obsBefore)
        $obsAfter = @(Get-Process -Name 'obs64' `
            -ErrorAction SilentlyContinue)
        $obsLogs = @(Get-ChildItem -LiteralPath (
            Join-Path $env:APPDATA 'obs-studio\logs') `
            -File -Filter '*.txt' -ErrorAction SilentlyContinue)
        $result.obs.preexistingProcessCount = $obsBefore.Count
        $result.obs.launched = $obsAfter.Count -ne 0
        $result.obs.newProcesses = @($obsProcesses |
            Select-Object Id, ProcessName, MainWindowTitle, Responding)
        $result.obs.cueletEndpointLogLines = @(
            Find-CueletLogLines -Files $obsLogs -Since $obsStarted)
        Close-NewProcesses -Processes $obsProcesses
        $obsProcesses = @()
    } else {
        $result.obs.launched = $false
        $result.obs.reason = 'OBS is not installed.'
    }

    if ($result.discord.installed) {
        $discordStarted = Get-Date
        $discordBefore = @(Get-ProcessIds -Names @('Discord'))
        Start-Process -FilePath $discordPath | Out-Null
        Start-Sleep -Seconds 12
        # Open Voice & Video so Discord refreshes its audio device list.
        Start-Process 'discord://-/settings/voice-video' `
            -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 10
        $discordProcesses = @(Get-NewProcesses -Names @('Discord') `
            -Before $discordBefore)
        $discordAfter = @(Get-Process -Name 'Discord' `
            -ErrorAction SilentlyContinue)
        $discordLogs = @(Get-ChildItem -LiteralPath (
            Join-Path $env:APPDATA 'discord\logs') `
            -File -ErrorAction SilentlyContinue)
        $result.discord.preexistingProcessCount = $discordBefore.Count
        $result.discord.launched = $discordAfter.Count -ne 0
        $result.discord.newProcesses = @($discordProcesses |
            Select-Object Id, ProcessName, MainWindowTitle, Responding)
        $result.discord.cueletEndpointLogLines = @(
            Find-CueletLogLines -Files $discordLogs `
                -Since $discordStarted)
        Close-NewProcesses -Processes $discordProcesses
        $discordProcesses = @()
    } else {
        $result.discord.launched = $false
        $result.discord.reason = 'Discord is not installed.'
    }

    $endpoints = @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        })
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $hash = if ($null -ne $service -and
        (Test-Path -LiteralPath $service.PathName -PathType Leaf)) {
        (Get-FileHash -LiteralPath $service.PathName `
            -Algorithm SHA256).Hash
    } else {
        ''
    }
    $result.endpointHealthAfterSmoke = [ordered]@{
        endpoints = $endpoints |
            Select-Object Status, FriendlyName, InstanceId
        serviceState = [string]$service.State
        installedSysSha256 = $hash
    }
    $result.passed = (
        $result.soundSettings.launchSucceeded -and
        $null -ne $result.soundSettings.cueletCaptureEndpoint -and
        (-not $result.obs.installed -or $result.obs.launched) -and
        (-not $result.discord.installed -or $result.discord.launched) -and
        $endpoints.Count -eq 2 -and
        @($endpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -eq 0 -and
        $null -ne $service -and $service.State -eq 'Running' -and
        $hash -eq $ExpectedSysSha256)
    if (-not $result.passed) {
        throw 'One or more installed-application smoke checks failed.'
    }
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    Close-NewProcesses -Processes $settingsProcesses
    Close-NewProcesses -Processes $obsProcesses
    Close-NewProcesses -Processes $discordProcesses
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath (
            Join-Path $smokeRoot 'application-smoke-result.json') `
            -Encoding utf8
}

& $recorder -EvidenceRoot $EvidenceRoot `
    -Category 'application-smoke' `
    -ExpectedSysSha256 $ExpectedSysSha256 |
    Set-Content -LiteralPath (
        Join-Path $smokeRoot 'category-health.json') -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw 'Health audit failed after application smoke tests.'
}
$result | ConvertTo-Json -Depth 12
exit 0
