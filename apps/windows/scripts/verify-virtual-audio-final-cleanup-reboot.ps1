[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Arm')]
    [switch]$Arm,
    [Parameter(Mandatory = $true, ParameterSetName = 'Resume')]
    [switch]$Resume,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [Parameter(Mandatory = $true)]
    [string]$RuntimeBaselinePath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$RuntimeBaselinePath = [IO.Path]::GetFullPath($RuntimeBaselinePath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$finalizer = Join-Path $PSScriptRoot 'finalize-virtual-audio-runtime.ps1'
$taskName = 'CueletStageEFinalCleanupReboot'
$beforePath = Join-Path $EvidenceRoot 'final-cleanup-reboot-before.json'
$resultPath = Join-Path $EvidenceRoot 'final-cleanup-reboot-result.json'
$runnerOut = Join-Path $EvidenceRoot 'final-cleanup-reboot-runner.stdout.log'
$runnerErr = Join-Path $EvidenceRoot 'final-cleanup-reboot-runner.stderr.log'
$cleanupResultPath = Join-Path $EvidenceRoot 'final-cleanup-result.json'

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

function Remove-CleanupTask {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false `
        -ErrorAction SilentlyContinue
}

foreach ($path in @(
    $InstallerPath, $RuntimeBaselinePath, $finalizer)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required final-reboot file not found: $path"
    }
}
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

if ($Arm) {
    if (-not $Elevated) {
        $arguments = @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', (Quote-Argument $PSCommandPath),
            '-Arm',
            '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
            '-InstallerPath', (Quote-Argument $InstallerPath),
            '-RuntimeBaselinePath', (Quote-Argument $RuntimeBaselinePath),
            '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
            '-ExpectedSysSha256', $ExpectedSysSha256,
            '-Elevated'
        )
        $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs `
            -Wait -PassThru -ArgumentList $arguments
        exit $process.ExitCode
    }
    if (-not (Test-Administrator)) {
        throw 'The final cleanup reboot must be armed by an administrator.'
    }
    if (Get-ScheduledTask -TaskName $taskName `
        -ErrorAction SilentlyContinue) {
        throw "The scheduled task $taskName already exists."
    }
    if (-not (Test-Path -LiteralPath $cleanupResultPath -PathType Leaf)) {
        throw 'The passing supported-uninstall result is missing.'
    }
    $cleanup = Get-Content -LiteralPath $cleanupResultPath -Raw |
        ConvertFrom-Json
    if (-not $cleanup.passed -or
        -not $cleanup.residue.residueFree -or
        $cleanup.trueStopCondition) {
        throw 'Supported uninstall is not eligible for the cleanup reboot.'
    }

    $statusText = (
        & $InstallerPath status --json 2>&1 | Out-String).Trim()
    $status = $statusText | ConvertFrom-Json
    $before = [ordered]@{
        sampledAt = (Get-Date).ToString('o')
        bootTime = (
            Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
        status = $status
        supportedUninstall = $cleanup
        taskName = $taskName
    }
    if ($status.packageInstalled) {
        throw 'The installer still reports a package before cleanup reboot.'
    }
    $before | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $beforePath -Encoding utf8

    $actionArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-Resume',
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-InstallerPath', (Quote-Argument $InstallerPath),
        '-RuntimeBaselinePath', (Quote-Argument $RuntimeBaselinePath),
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-ExpectedSysSha256', $ExpectedSysSha256,
        '-Elevated'
    ) -join ' '
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument $actionArguments
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 10) `
        -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $taskName -Action $action `
        -Trigger $trigger -Settings $settings -User 'SYSTEM' `
        -RunLevel Highest -Force | Out-Null

    & shutdown.exe /r /t 15 /d p:4:1 /c (
        'Cuelet Stage E final cleanup verification reboot.')
    if ($LASTEXITCODE -ne 0) {
        Remove-CleanupTask
        throw "shutdown.exe failed with exit code $LASTEXITCODE."
    }
    exit 0
}

if (-not (Test-Administrator)) {
    throw 'The final cleanup reboot verifier must run as administrator.'
}
$result = [ordered]@{
    sampledAt = (Get-Date).ToString('o')
    taskName = $taskName
    passed = $false
}
try {
    if (-not (Test-Path -LiteralPath $beforePath -PathType Leaf)) {
        throw 'The pre-reboot cleanup snapshot is missing.'
    }
    $before = Get-Content -LiteralPath $beforePath -Raw |
        ConvertFrom-Json
    Start-Sleep -Seconds 30
    $afterBoot = (
        Get-CimInstance Win32_OperatingSystem).LastBootUpTime
    $result.beforeBootTime = [string]$before.bootTime
    $result.afterBootTime = $afterBoot.ToString('o')
    $result.didReboot = (
        [datetime]$afterBoot -gt [datetime]$before.bootTime)
    if (-not $result.didReboot) {
        throw 'The boot identifier did not advance.'
    }

    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $finalizer),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-InstallerPath', (Quote-Argument $InstallerPath),
        '-RuntimeBaselinePath', (Quote-Argument $RuntimeBaselinePath),
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-ExpectedSysSha256', $ExpectedSysSha256,
        '-ResumeAfterDeferredServiceDelete',
        '-Elevated'
    )
    $process = Start-Process -FilePath 'powershell.exe' -Wait -PassThru `
        -WindowStyle Hidden -ArgumentList $arguments `
        -RedirectStandardOutput $runnerOut -RedirectStandardError $runnerErr
    $result.cleanupAuditExitCode = $process.ExitCode
    if (-not (Test-Path -LiteralPath $cleanupResultPath -PathType Leaf)) {
        throw 'The post-reboot cleanup audit did not write a result.'
    }
    $cleanup = Get-Content -LiteralPath $cleanupResultPath -Raw |
        ConvertFrom-Json
    $result.cleanup = $cleanup
    $result.passed = (
        $process.ExitCode -eq 0 -and
        $cleanup.passed -and
        $cleanup.residue.residueFree -and
        -not $cleanup.trueStopCondition)
    if (-not $result.passed) {
        throw 'The post-reboot cleanup audit failed.'
    }
} catch {
    $result.failure = $_.Exception.Message
} finally {
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 14 |
        Set-Content -LiteralPath $resultPath -Encoding utf8
    Remove-CleanupTask
}
if (-not $result.passed) { exit 40 }
exit 0
