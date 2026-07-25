[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 5)]
    [int]$Cycle,
    [Parameter(Mandatory = $true)]
    [ValidateSet('StreamStopped', 'EndpointsIdle', 'CueletIdle')]
    [string]$Context,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [string]$CueletPath = '',
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
if ($CueletPath) {
    $CueletPath = [IO.Path]::GetFullPath($CueletPath)
}
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$cycleRoot = Join-Path $EvidenceRoot (
    'sleep-' + $Cycle + '-' + $Context.ToLowerInvariant())
$resultPath = Join-Path $cycleRoot 'sleep-cycle-result.json'
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'
$wakeTaskName = 'CueletStageEWake-' + [guid]::NewGuid().ToString('N')

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
        '-Cycle', $Cycle,
        '-Context', $Context,
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-FlowTestPath', (Quote-Argument $FlowTestPath),
        '-ExpectedSysSha256', $ExpectedSysSha256,
        '-Elevated'
    )
    if ($CueletPath) {
        $arguments += @('-CueletPath', (Quote-Argument $CueletPath))
    }
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs `
        -Wait -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'The one-shot sleep/resume test requires elevation.'
}
foreach ($path in @($FlowTestPath, $recorder)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}
if ($Context -eq 'CueletIdle' -and
    -not (Test-Path -LiteralPath $CueletPath -PathType Leaf)) {
    throw "Cuelet executable not found: $CueletPath"
}
New-Item -ItemType Directory -Path $cycleRoot -Force | Out-Null

function Get-RecordId {
    param([string]$LogName)
    return (Get-WinEvent -LogName $LogName -MaxEvents 1).RecordId
}

function Get-State {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $rootDevices = @(Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -match '(?i)^ROOT\\CUELETVIRTUALAUDIO\\'
        })
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
    return [ordered]@{
        sampledAt = (Get-Date).ToString('o')
        bootTime = (
            Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
        systemRecordId = Get-RecordId -LogName System
        applicationRecordId = Get-RecordId -LogName Application
        codeIntegrityRecordId = Get-RecordId -LogName `
            'Microsoft-Windows-CodeIntegrity/Operational'
        root = $root |
            Select-Object Status, Class, FriendlyName, InstanceId
        rootDevices = $rootDevices |
            Select-Object Status, Present, Class, FriendlyName, InstanceId
        endpoints = $endpoints |
            Select-Object Status, Present, FriendlyName, InstanceId
        service = $service |
            Select-Object Name, State, StartMode, PathName
        installedSysSha256 = $hash
        healthy = (
            $null -ne $root -and $root.Status -eq 'OK' -and
            $rootDevices.Count -eq 1 -and
            $endpoints.Count -eq 2 -and
            @($endpoints | Where-Object {
                $_.Status -ne 'OK'
            }).Count -eq 0 -and
            $null -ne $service -and $service.State -eq 'Running' -and
            $hash -eq $ExpectedSysSha256)
    }
}

function Test-NewEvents {
    param([object]$Before)
    $system = @(Get-WinEvent -LogName System `
        -FilterXPath (
            "*[System[EventRecordID > $($Before.systemRecordId)]]") `
        -ErrorAction SilentlyContinue)
    $application = @(Get-WinEvent -LogName Application `
        -FilterXPath (
            "*[System[EventRecordID > $($Before.applicationRecordId)]]") `
        -ErrorAction SilentlyContinue)
    $ci = @(Get-WinEvent -LogName `
        'Microsoft-Windows-CodeIntegrity/Operational' `
        -FilterXPath (
            "*[System[EventRecordID > $($Before.codeIntegrityRecordId)]]") `
        -ErrorAction SilentlyContinue)
    $storage = @($system | Where-Object {
        $_.ProviderName -match (
            '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
        ($_.Level -le 3 -or $_.Id -eq 55)
    })
    $candidateCi = @($ci | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
    })
    $unexpectedRestart = @(@($system) + @($application) |
        Where-Object {
            $_.ProviderName -match (
                '(?i)BugCheck|Windows Error Reporting|Kernel-Power|EventLog') -and
            ($_.Id -in @(41, 1001, 6008) -or
                $_.Message -match (
                    '(?i)bugcheck|live kernel|BlueScreen|unexpected shutdown'))
        })
    $cueletPnp = @($system | Where-Object {
        $_.Level -le 3 -and
        $_.ProviderName -match '(?i)Kernel-PnP|UserPnp|Audio' -and
        $_.Message -match '(?i)Cuelet|CUELETVIRTUALAUDIO'
    })
    $powerTransitions = @($system | Where-Object {
        $_.ProviderName -match '(?i)Kernel-Power|Power-Troubleshooter' -or
        $_.Id -in @(1, 42, 107, 506, 507)
    } | Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, Message)
    return [ordered]@{
        system = $system.Count
        application = $application.Count
        codeIntegrity = $ci.Count
        storageOrHardwareErrors = $storage.Count
        candidateCodeIntegrityErrors = $candidateCi.Count
        unexpectedRestartOrKernelFailure = $unexpectedRestart.Count
        cueletPnpOrAudioErrors = $cueletPnp.Count
        powerTransitions = $powerTransitions
        clean = (
            $storage.Count -eq 0 -and
            $candidateCi.Count -eq 0 -and
            $unexpectedRestart.Count -eq 0 -and
            $cueletPnp.Count -eq 0)
    }
}

$cueletProcess = $null
$wakeRegistered = $false
$result = [ordered]@{
    cycle = $Cycle
    context = $Context
    startedAt = (Get-Date).ToString('o')
    wakeTaskName = $wakeTaskName
    requestedWakeDelaySeconds = 120
    passed = $false
}

try {
    $availableStates = (& powercfg.exe /a 2>&1 | Out-String).Trim()
    $result.availableSleepStates = $availableStates
    $before = Get-State
    $result.before = $before
    if (-not $before.healthy) {
        throw 'Cuelet was not healthy before the sleep cycle.'
    }

    if ($Context -eq 'StreamStopped') {
        $flowRoot = Join-Path $cycleRoot 'pre-sleep-stream'
        & $FlowTestPath --bounded-tone --frequency 997 --seconds 2 `
            --output-dir $flowRoot *>&1 |
            Set-Content -LiteralPath (
                Join-Path $cycleRoot 'pre-sleep-stream.log') -Encoding utf8
        $result.preSleepFlowExitCode = $LASTEXITCODE
        if ($LASTEXITCODE -ne 0) {
            throw 'The pre-sleep start/stop audio flow failed.'
        }
    } elseif ($Context -eq 'CueletIdle') {
        $cueletProcess = Start-Process -FilePath $CueletPath -PassThru
        Start-Sleep -Seconds 5
        $cueletProcess.Refresh()
        $result.cueletIdleBeforeSleep = (
            -not $cueletProcess.HasExited -and $cueletProcess.Responding)
        if (-not $result.cueletIdleBeforeSleep) {
            throw 'Cuelet did not remain running and idle before sleep.'
        }
    }

    $wakeAt = (Get-Date).AddSeconds(120)
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' `
        -Argument '/d /c exit 0'
    $trigger = New-ScheduledTaskTrigger -Once -At $wakeAt
    $settings = New-ScheduledTaskSettingsSet -WakeToRun `
        -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $wakeTaskName -Action $action `
        -Trigger $trigger -Settings $settings -User 'SYSTEM' `
        -RunLevel Highest -Force | Out-Null
    $wakeRegistered = $true
    $result.wakeScheduledFor = $wakeAt.ToString('o')
    $result.suspendRequestedAt = (Get-Date).ToString('o')
    $suspendCallStarted = Get-Date

    if (-not ('Cuelet.NativePower' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Cuelet {
    public static class NativePower {
        [DllImport("powrprof.dll", SetLastError = true)]
        public static extern bool SetSuspendState(
            bool hibernate, bool forceCritical, bool disableWakeEvent);
    }
}
'@
    }
    $suspended = [Cuelet.NativePower]::SetSuspendState(
        $false, $false, $false)
    $result.suspendApiReturned = $suspended
    $result.suspendApiWin32Error = if ($suspended) {
        0
    } else {
        [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    }
    if (-not $suspended) {
        throw ('SetSuspendState failed with Win32 error ' +
            $result.suspendApiWin32Error + '.')
    }

    $suspendCallReturned = Get-Date
    $result.suspendApiReturnedAt = $suspendCallReturned.ToString('o')
    $result.suspendApiCallSeconds = (
        $suspendCallReturned - $suspendCallStarted).TotalSeconds

    # On Modern Standby systems SetSuspendState can return before the
    # screen-off transition has completed. Keep this process passive until the
    # one-shot wake task has had time to fire. If the API itself blocked across
    # a real suspend/resume, only allow a short event-log settlement delay.
    $passiveWaitSeconds = if ($result.suspendApiCallSeconds -ge 30) {
        10
    } else {
        [Math]::Max(
            10,
            [int][Math]::Ceiling(($wakeAt - (Get-Date)).TotalSeconds) + 15)
    }
    $result.passiveTransitionWaitSeconds = $passiveWaitSeconds
    Start-Sleep -Seconds $passiveWaitSeconds
    $result.resumedAt = (Get-Date).ToString('o')

    $wakeInfo = Get-ScheduledTaskInfo -TaskName $wakeTaskName
    $result.wakeTaskLastRunTime = $wakeInfo.LastRunTime.ToString('o')
    $result.wakeTaskRan = (
        $wakeInfo.LastRunTime -ge $wakeAt.AddSeconds(-5))
    $after = Get-State
    $result.after = $after
    $result.sameBootSession = (
        [datetime]$after.bootTime -eq [datetime]$before.bootTime)
    $result.newEvents = Test-NewEvents -Before $before
    $transitionIds = @(
        $result.newEvents.powerTransitions |
            ForEach-Object { [int]$_.Id })
    $result.modernStandbyEntryObserved = $transitionIds -contains 506
    $result.modernStandbyExitObserved = $transitionIds -contains 507
    $result.actualPowerTransitionObserved = (
        $result.modernStandbyEntryObserved -and
        $result.modernStandbyExitObserved)

    $postFlowRoot = Join-Path $cycleRoot 'post-resume-flow'
    & $FlowTestPath --bounded-tone --frequency 997 --seconds 1 `
        --output-dir $postFlowRoot *>&1 |
        Set-Content -LiteralPath (
            Join-Path $cycleRoot 'post-resume-flow.log') -Encoding utf8
    $result.postResumeFlowExitCode = $LASTEXITCODE

    if ($null -ne $cueletProcess) {
        $cueletProcess.Refresh()
        $result.cueletIdleAfterResume = (
            -not $cueletProcess.HasExited -and $cueletProcess.Responding)
    }
    $result.passed = (
        $after.healthy -and
        $result.sameBootSession -and
        $result.actualPowerTransitionObserved -and
        $result.wakeTaskRan -and
        $result.newEvents.clean -and
        $result.postResumeFlowExitCode -eq 0 -and
        ($Context -ne 'CueletIdle' -or $result.cueletIdleAfterResume))
    if (-not $result.passed) {
        throw (
            'Sleep/resume did not produce a verified Modern Standby ' +
            'entry/exit pair and healthy post-resume state.')
    }
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    if ($null -ne $cueletProcess) {
        try {
            $cueletProcess.Refresh()
            if (-not $cueletProcess.HasExited) {
                [void]$cueletProcess.CloseMainWindow()
                if (-not $cueletProcess.WaitForExit(5000)) {
                    $cueletProcess.Kill()
                    $cueletProcess.WaitForExit()
                }
            }
        } catch {
            $result.cueletCleanupError = $_.Exception.Message
        }
    }
    if ($wakeRegistered) {
        try {
            Unregister-ScheduledTask -TaskName $wakeTaskName `
                -Confirm:$false
        } catch {
            $result.wakeTaskCleanupError = $_.Exception.Message
        }
    }
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $resultPath -Encoding utf8
}

& $recorder -EvidenceRoot $EvidenceRoot `
    -Category ('sleep-' + $Cycle + '-' + $Context.ToLowerInvariant()) `
    -ExpectedSysSha256 $ExpectedSysSha256 |
    Set-Content -LiteralPath (
        Join-Path $cycleRoot 'category-health.json') -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw 'The category health audit failed after sleep/resume.'
}
$result | ConvertTo-Json -Depth 12
exit 0
