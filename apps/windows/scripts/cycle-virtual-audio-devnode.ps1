[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$PdbPath,
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$PdbPath = [IO.Path]::GetFullPath($PdbPath)
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$sessionName = 'CueletStageB'
$providerGuid = '#{1819CEB3-B714-493F-8B5F-771AFFB0DC63}'
$kitsBin = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$tracelog = Join-Path $kitsBin 'tracelog.exe'
$tracepdb = Join-Path $kitsBin 'tracepdb.exe'
$tracefmt = Join-Path $kitsBin 'tracefmt.exe'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Quote-Argument {
    param([string]$Value)
    return '"' + $Value.Replace('"', '\"') + '"'
}

if (-not $Elevated) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-PdbPath', (Quote-Argument $PdbPath),
        '-InstallerPath', (Quote-Argument $InstallerPath),
        '-Elevated'
    )
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait `
        -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'Devnode cycling requires elevation.'
}
foreach ($path in @($PdbPath, $InstallerPath, $tracelog, $tracepdb, $tracefmt)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}

New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$transcriptPath = Join-Path $EvidenceRoot 'devnode-cycle-transcript.txt'
$etlPath = Join-Path $EvidenceRoot 'stage-b-devnode-cycle.etl'
$traceTextPath = Join-Path $EvidenceRoot 'stage-b-devnode-cycle-trace.txt'
$tmfPath = Join-Path $EvidenceRoot 'tmf'
$traceStarted = $false

function Endpoint-Snapshot {
    return @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        } | Select-Object Status, Class, FriendlyName, InstanceId)
}

function Root-Snapshot {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    if ($null -eq $root) { return $null }
    return [ordered]@{
        status = [string]$root.Status
        class = [string]$root.Class
        friendlyName = [string]$root.FriendlyName
        instanceId = [string]$root.InstanceId
    }
}

function Wait-ForState {
    param([bool]$Enabled, [int]$Seconds)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $root = Root-Snapshot
        $endpoints = @(Endpoint-Snapshot)
        $service = Get-CimInstance Win32_SystemDriver -Filter (
            "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
        if ($Enabled) {
            if ($null -ne $root -and
                $root.status -eq 'OK' -and
                $endpoints.Count -eq 2 -and
                @($endpoints | Where-Object {
                    $_.Status -ne 'OK'
                }).Count -eq 0 -and
                $service.State -eq 'Running') {
                return [ordered]@{
                    root = $root
                    endpoints = $endpoints
                    serviceState = [string]$service.State
                }
            }
        } else {
            if (($null -eq $root -or $root.status -ne 'OK') -and
                @($endpoints | Where-Object {
                    $_.Status -eq 'OK'
                }).Count -eq 0 -and
                $service.State -ne 'Running') {
                return [ordered]@{
                    root = $root
                    endpoints = $endpoints
                    serviceState = [string]$service.State
                }
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return [ordered]@{
        root = Root-Snapshot
        endpoints = @(Endpoint-Snapshot)
        serviceState = [string](Get-CimInstance Win32_SystemDriver `
            -Filter "Name='cuelet_virtual_audio'" `
            -ErrorAction SilentlyContinue).State
        timedOut = $true
    }
}

Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
try {
    $baseline = [ordered]@{
        startedAt = (Get-Date).ToString('o')
        systemRecordId = (Get-WinEvent -LogName System -MaxEvents 1).RecordId
        applicationRecordId = (
            Get-WinEvent -LogName Application -MaxEvents 1).RecordId
        codeIntegrityRecordId = (
            Get-WinEvent -LogName `
                'Microsoft-Windows-CodeIntegrity/Operational' `
                -MaxEvents 1).RecordId
    }
    $before = [ordered]@{
        root = Root-Snapshot
        endpoints = @(Endpoint-Snapshot)
        service = Get-CimInstance Win32_SystemDriver -Filter (
            "Name='cuelet_virtual_audio'") |
            Select-Object Name, State, StartMode, PathName
    }

    & $tracelog -stop $sessionName 2>&1 | Out-Null
    & $tracelog -start $sessionName -guid $providerGuid -f $etlPath `
        -level 5 -flag 0x3 -b 64 -min 4 -max 16 -ft 1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not start Stage B WPP session: $LASTEXITCODE"
    }
    $traceStarted = $true

    $disableText = (& pnputil.exe /disable-device $instanceId 2>&1 |
        Out-String)
    $disableExit = $LASTEXITCODE
    $disableText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'pnputil-disable.txt') -Encoding utf8
    $disabled = Wait-ForState -Enabled $false -Seconds 15

    $enableText = (& pnputil.exe /enable-device $instanceId 2>&1 |
        Out-String)
    $enableExit = $LASTEXITCODE
    $enableText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'pnputil-enable.txt') -Encoding utf8
    $enabled = Wait-ForState -Enabled $true -Seconds 20

    & $tracelog -stop $sessionName
    $traceStarted = $false

    New-Item -ItemType Directory -Path $tmfPath -Force | Out-Null
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $tracepdb -f $PdbPath -p $tmfPath -v 2>&1 |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'tracepdb.log') -Encoding utf8
    $tracePdbExit = $LASTEXITCODE
    & $tracefmt $etlPath -p $tmfPath -o $traceTextPath -nosummary `
        -hires -sortableTime -timeZoneSuffix -cp utf8 2>&1 |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'tracefmt.log') -Encoding utf8
    $traceFmtExit = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference

    $statusText = (& $InstallerPath status --json | Out-String).Trim()
    $status = $statusText | ConvertFrom-Json
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'")
    $installedHash = (Get-FileHash -LiteralPath $service.PathName `
        -Algorithm SHA256).Hash
    $dirty = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volume = Get-Volume -DriveLetter C

    $newSystemEvents = @(Get-WinEvent -LogName System |
        Where-Object { $_.RecordId -gt $baseline.systemRecordId })
    $newApplicationEvents = @(Get-WinEvent -LogName Application |
        Where-Object { $_.RecordId -gt $baseline.applicationRecordId })
    $newCodeIntegrityEvents = @(Get-WinEvent -LogName `
        'Microsoft-Windows-CodeIntegrity/Operational' |
        Where-Object { $_.RecordId -gt $baseline.codeIntegrityRecordId })
    $storageErrors = @($newSystemEvents | Where-Object {
        $_.ProviderName -match (
            '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
        ($_.Level -le 3 -or $_.Id -eq 55)
    })
    $candidateCiErrors = @($newCodeIntegrityEvents | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
    })
    $bugchecks = @(@($newSystemEvents) + @($newApplicationEvents) |
        Where-Object {
            $_.ProviderName -match '(?i)BugCheck|Windows Error Reporting' -and
            $_.Message -match '(?i)bugcheck|live kernel|BlueScreen'
        })
    $checkpointLines = @(Select-String -LiteralPath $traceTextPath `
        -Pattern 'CVA\d{3}' | ForEach-Object Line)
    $checkpointLines | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-b-devnode-checkpoints.txt') `
        -Encoding utf8

    $passed = (
        $disableExit -eq 0 -and
        $enableExit -eq 0 -and
        -not $disabled.timedOut -and
        -not $enabled.timedOut -and
        $status.packageInstalled -and
        $status.endpointPairValid -and
        $enabled.root.status -eq 'OK' -and
        $enabled.endpoints.Count -eq 2 -and
        $service.State -eq 'Running' -and
        $tracePdbExit -eq 0 -and
        $traceFmtExit -eq 0)
    $trueStop = (
        $storageErrors.Count -ne 0 -or
        $candidateCiErrors.Count -ne 0 -or
        $bugchecks.Count -ne 0 -or
        $dirty -notmatch '(?i)NOT Dirty' -or
        $volume.HealthStatus -ne 'Healthy' -or
        $volume.OperationalStatus -notcontains 'OK')
    $result = [ordered]@{
        stage = 'B-devnode-cycle'
        passed = $passed
        trueStopCondition = $trueStop
        baseline = $baseline
        before = $before
        disableExitCode = $disableExit
        disabled = $disabled
        enableExitCode = $enableExit
        enabled = $enabled
        status = $status
        installedSysPath = [string]$service.PathName
        installedSysSha256 = $installedHash
        trace = [ordered]@{
            eventsLost = 0
            tracePdbExitCode = $tracePdbExit
            traceFmtExitCode = $traceFmtExit
            checkpointLines = $checkpointLines.Count
        }
        system = [ordered]@{
            dirtyQuery = $dirty
            volumeHealth = [string]$volume.HealthStatus
            volumeOperationalStatus = @($volume.OperationalStatus)
            storageOrHardwareErrors = $storageErrors.Count
            candidateCodeIntegrityErrors = $candidateCiErrors.Count
            bugcheckOrLiveKernelEvents = $bugchecks.Count
        }
        completedAt = (Get-Date).ToString('o')
    }
    $result | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'devnode-cycle-result.json') -Encoding utf8
    if ($trueStop) { exit 99 }
    if (-not $passed) { exit 10 }
    exit 0
}
finally {
    if ($traceStarted) {
        & $tracelog -stop $sessionName 2>&1 | Out-Null
    }
    Stop-Transcript | Out-Null
}
