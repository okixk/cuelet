[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Arm')]
    [switch]$Arm,
    [Parameter(Mandatory = $true, ParameterSetName = 'Resume')]
    [switch]$Resume,
    [Parameter(Mandatory = $true, ParameterSetName = 'Continue')]
    [switch]$Continue,
    [Parameter(Mandatory = $true, ParameterSetName = 'Continue')]
    [ValidateRange(1, 2)]
    [int]$CompletedCycles,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$verifyScript = Join-Path $PSScriptRoot `
    'verify-virtual-audio-stage-e-reboot.ps1'
$taskName = 'CueletStageERebootSequence'
$statePath = Join-Path $EvidenceRoot 'reboot-sequence-state.json'
$logPath = Join-Path $EvidenceRoot 'reboot-sequence.log'
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'

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

function Save-State {
    param([System.Collections.IDictionary]$State)
    $State.updatedAt = (Get-Date).ToString('o')
    $State | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $statePath -Encoding utf8
}

function Invoke-Verification {
    param(
        [ValidateSet('Before', 'After')]
        [string]$Phase,
        [int]$Cycle
    )
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $verifyScript),
        '-Phase', $Phase,
        '-Cycle', $Cycle,
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-FlowTestPath', (Quote-Argument $FlowTestPath),
        '-ExpectedSysSha256', $ExpectedSysSha256
    )
    $stdout = Join-Path $EvidenceRoot (
        "reboot-$Cycle-$($Phase.ToLowerInvariant())-runner.stdout.log")
    $stderr = Join-Path $EvidenceRoot (
        "reboot-$Cycle-$($Phase.ToLowerInvariant())-runner.stderr.log")
    $process = Start-Process -FilePath 'powershell.exe' -Wait -PassThru `
        -WindowStyle Hidden -ArgumentList $arguments `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    return $process.ExitCode
}

function Test-CueletReady {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $roots = @(Get-PnpDevice -ErrorAction SilentlyContinue |
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
    return (
        $null -ne $root -and $root.Status -eq 'OK' -and
        $roots.Count -eq 1 -and
        $endpoints.Count -eq 2 -and
        @($endpoints | Where-Object { $_.Status -ne 'OK' }).Count -eq 0 -and
        $null -ne $service -and $service.State -eq 'Running' -and
        $hash -eq $ExpectedSysSha256)
}

function Remove-SequenceTask {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
        -ErrorAction SilentlyContinue
}

function Request-PlannedRestart {
    param([int]$CompletedCycle)
    $comment = (
        'Cuelet Stage E planned reboot; completed cycle ' +
        $CompletedCycle + ' of 3.')
    & shutdown.exe /r /t 15 /d p:4:1 /c $comment
    if ($LASTEXITCODE -ne 0) {
        throw "shutdown.exe failed with exit code $LASTEXITCODE."
    }
}

foreach ($path in @($verifyScript, $FlowTestPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required reboot-test file not found: $path"
    }
}
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

if ($Arm -or $Continue) {
    if (-not $Elevated) {
        $arguments = @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', (Quote-Argument $PSCommandPath),
            $(if ($Arm) { '-Arm' } else { '-Continue' }),
            '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
            '-FlowTestPath', (Quote-Argument $FlowTestPath),
            '-ExpectedSysSha256', $ExpectedSysSha256,
            '-Elevated'
        )
        if ($Continue) {
            $arguments += @('-CompletedCycles', $CompletedCycles)
        }
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs `
            -Wait -PassThru -ArgumentList $arguments
        exit $process.ExitCode
    }
    if (-not (Test-Administrator)) {
        throw 'The reboot sequence must be armed by an administrator.'
    }
    $existing = $null
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        $existing = Get-Content -LiteralPath $statePath -Raw |
            ConvertFrom-Json
        if ($existing.status -eq 'running') {
            throw 'A Cuelet Stage E reboot sequence is already running.'
        }
    }
    if (Get-ScheduledTask -TaskName $taskName `
        -ErrorAction SilentlyContinue) {
        throw "The scheduled task $taskName already exists."
    }

    $nextCycle = 1
    $completed = 0
    $recoveryEvidence = ''
    if ($Continue) {
        if ($null -eq $existing) {
            throw 'Cannot continue without the existing reboot state.'
        }
        if ([int]$existing.maximumCycles -ne 3) {
            throw 'The existing reboot state has an unexpected cycle limit.'
        }
        $afterPath = Join-Path $EvidenceRoot (
            "reboot-$CompletedCycles-after.json")
        $recoveryEvidence = Join-Path $EvidenceRoot (
            "reboot-$CompletedCycles-recovery-validation.json")
        foreach ($required in @($afterPath, $recoveryEvidence)) {
            if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
                throw "Required recovery evidence not found: $required"
            }
        }
        $after = Get-Content -LiteralPath $afterPath -Raw |
            ConvertFrom-Json
        $recovery = Get-Content -LiteralPath $recoveryEvidence -Raw |
            ConvertFrom-Json
        $currentBoot = (
            Get-CimInstance Win32_OperatingSystem).LastBootUpTime
        if (-not $after.didReboot -or -not $after.after.healthy -or
            -not $recovery.passed -or
            [datetime]$after.after.bootTime -ne [datetime]$currentBoot) {
            throw (
                'The prior reboot cycle is not eligible for recovery ' +
                'continuation.')
        }
        $completed = $CompletedCycles
        $nextCycle = $CompletedCycles + 1
    }

    if (Invoke-Verification -Phase Before -Cycle $nextCycle) {
        throw "Pre-reboot verification failed for cycle $nextCycle."
    }

    $state = [ordered]@{
        status = 'running'
        currentCycle = $nextCycle
        maximumCycles = 3
        startedAt = if ($null -ne $existing -and $existing.startedAt) {
            [string]$existing.startedAt
        } else {
            (Get-Date).ToString('o')
        }
        completedCycles = $completed
        lastBootTime = (
            Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
    }
    if ($Continue) {
        $state.recoveredCycle = $CompletedCycles
        $state.recoveryEvidence = $recoveryEvidence
    }
    Save-State -State $state

    $actionArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-Resume',
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-FlowTestPath', (Quote-Argument $FlowTestPath),
        '-ExpectedSysSha256', $ExpectedSysSha256,
        '-Elevated'
    ) -join ' '
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument $actionArguments
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 15) `
        -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $taskName -Action $action `
        -Trigger $trigger -Settings $settings -User 'SYSTEM' `
        -RunLevel Highest -Force | Out-Null
    Request-PlannedRestart -CompletedCycle $completed
    exit 0
}

if (-not (Test-Administrator)) {
    throw 'The reboot resume verifier must run as an administrator.'
}
if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
    Remove-SequenceTask
    throw 'The reboot sequence state file is missing.'
}

$stateObject = Get-Content -LiteralPath $statePath -Raw |
    ConvertFrom-Json
$state = [ordered]@{}
$stateObject.PSObject.Properties | ForEach-Object {
    $state[$_.Name] = $_.Value
}
if ($state.status -ne 'running') {
    Remove-SequenceTask
    exit 0
}
$cycle = [int]$state.currentCycle
if ($cycle -lt 1 -or $cycle -gt 3) {
    $state.status = 'failed'
    $state.failure = "Invalid reboot cycle in state: $cycle"
    Save-State -State $state
    Remove-SequenceTask
    exit 20
}

try {
    Add-Content -LiteralPath $logPath -Encoding utf8 -Value (
        "$(Get-Date -Format o) boot verifier entered for cycle $cycle")
    $deadline = (Get-Date).AddMinutes(4)
    do {
        if (Test-CueletReady) {
            break
        }
        Start-Sleep -Seconds 5
    } while ((Get-Date) -lt $deadline)
    if (-not (Test-CueletReady)) {
        throw "Cuelet did not become healthy after reboot cycle $cycle."
    }

    $afterExit = Invoke-Verification -Phase After -Cycle $cycle
    if ($afterExit -ne 0) {
        throw (
            "Post-reboot verification for cycle $cycle failed with " +
            "exit code $afterExit.")
    }
    $state.completedCycles = $cycle
    $state.lastBootTime = (
        Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')

    if ($cycle -eq 3) {
        $state.status = 'complete'
        $state.completedAt = (Get-Date).ToString('o')
        Save-State -State $state
        Remove-SequenceTask
        Add-Content -LiteralPath $logPath -Encoding utf8 -Value (
            "$(Get-Date -Format o) all three reboot cycles passed")
        exit 0
    }

    $nextCycle = $cycle + 1
    if (Invoke-Verification -Phase Before -Cycle $nextCycle) {
        throw "Pre-reboot verification failed for cycle $nextCycle."
    }
    $state.currentCycle = $nextCycle
    Save-State -State $state
    Add-Content -LiteralPath $logPath -Encoding utf8 -Value (
        "$(Get-Date -Format o) cycle $cycle passed; scheduling $nextCycle")
    Request-PlannedRestart -CompletedCycle $cycle
    exit 0
} catch {
    $state.status = 'failed'
    $state.failure = $_.Exception.Message
    $state.failedAt = (Get-Date).ToString('o')
    Save-State -State $state
    Remove-SequenceTask
    Add-Content -LiteralPath $logPath -Encoding utf8 -Value (
        "$(Get-Date -Format o) FAILED: $($_.Exception.Message)")
    exit 30
}
