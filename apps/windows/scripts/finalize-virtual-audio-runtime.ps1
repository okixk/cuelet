[CmdletBinding()]
param(
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
    [switch]$ResumeAfterDeferredServiceDelete,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$RuntimeBaselinePath = [IO.Path]::GetFullPath($RuntimeBaselinePath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
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

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]
        $Value,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$Depth = 10
    )
    ConvertTo-Json -InputObject $Value -Depth $Depth |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-CueletEndpoints {
    return @(Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        } | Select-Object Status, Class, FriendlyName, InstanceId)
}

if (-not $Elevated) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-InstallerPath', (Quote-Argument $InstallerPath),
        '-RuntimeBaselinePath', (Quote-Argument $RuntimeBaselinePath),
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-ExpectedSysSha256', (Quote-Argument $ExpectedSysSha256),
        '-Elevated'
    )
    if ($ResumeAfterDeferredServiceDelete) {
        $arguments += '-ResumeAfterDeferredServiceDelete'
    }
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait `
        -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'Final runtime cleanup and audit require elevation.'
}
foreach ($path in @($InstallerPath, $RuntimeBaselinePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}

New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$transcriptName = if ($ResumeAfterDeferredServiceDelete) {
    'final-cleanup-resumed-transcript.txt'
} else {
    'final-cleanup-transcript.txt'
}
$transcript = Join-Path $EvidenceRoot $transcriptName
Start-Transcript -LiteralPath $transcript -Force | Out-Null
try {
    $baseline = Get-Content -LiteralPath $RuntimeBaselinePath -Raw |
        ConvertFrom-Json
    if ($ResumeAfterDeferredServiceDelete) {
        $snapshotPath = Join-Path $EvidenceRoot 'pre-cleanup-snapshot.json'
        if (-not (Test-Path -LiteralPath $snapshotPath -PathType Leaf)) {
            throw 'The original pre-cleanup snapshot is required to resume.'
        }
        $savedBefore = Get-Content -LiteralPath $snapshotPath -Raw |
            ConvertFrom-Json
        $beforeSysPath = [string]$savedBefore.installedSysPath
        $beforeSysHash = [string]$savedBefore.installedSysSha256
        if (
            [string]$savedBefore.status.installedVersion -ne $ExpectedVersion -or
            $beforeSysHash.ToUpperInvariant() -ne $ExpectedSysSha256) {
            throw 'The saved pre-cleanup snapshot does not match the candidate.'
        }
        $uninstallExit = 0
    } else {
        $beforeStatusText = (
            & $InstallerPath status --json 2>&1 | Out-String).Trim()
        $beforeStatusText | Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'installer-status-before.json') `
            -Encoding utf8
        $beforeStatus = $beforeStatusText | ConvertFrom-Json
        $beforeService = Get-CimInstance Win32_SystemDriver -Filter (
            "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
        $beforeSysPath = [string]$beforeService.PathName
        $beforeSysHash = if (
            $beforeSysPath -and
            (Test-Path -LiteralPath $beforeSysPath -PathType Leaf)) {
            (Get-FileHash -LiteralPath $beforeSysPath -Algorithm SHA256).Hash
        } else {
            ''
        }
        $before = [ordered]@{
            status = $beforeStatus
            root = Get-PnpDevice -InstanceId $instanceId `
                -ErrorAction SilentlyContinue |
                Select-Object Status, Class, FriendlyName, InstanceId
            endpoints = @(Get-CueletEndpoints)
            service = $beforeService |
                Select-Object Name, State, StartMode, PathName
            installedSysPath = $beforeSysPath
            installedSysSha256 = $beforeSysHash
            flowTestProcesses = @(
                Get-Process -Name 'Cuelet.VirtualAudio.FlowTest' `
                    -ErrorAction SilentlyContinue |
                    Select-Object Id, ProcessName, StartTime)
        }
        Write-JsonFile -Value $before -Path (
            Join-Path $EvidenceRoot 'pre-cleanup-snapshot.json') -Depth 12

        if (
            -not $beforeStatus.packageInstalled -or
            -not $beforeStatus.endpointPairValid -or
            [string]$beforeStatus.installedVersion -ne $ExpectedVersion -or
            $beforeSysHash.ToUpperInvariant() -ne $ExpectedSysSha256) {
            throw 'Pre-cleanup state does not match the tested candidate.'
        }

        $uninstallText = (& $InstallerPath uninstall 2>&1 | Out-String).Trim()
        $uninstallExit = $LASTEXITCODE
        $uninstallText | Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'installer-uninstall.txt') -Encoding utf8
    }

    $deadline = (Get-Date).AddSeconds(20)
    do {
        $root = Get-PnpDevice -InstanceId $instanceId `
            -ErrorAction SilentlyContinue
        $service = Get-CimInstance Win32_SystemDriver -Filter (
            "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
        $endpoints = @(Get-CueletEndpoints)
        if ($null -eq $root -and $null -eq $service -and
            $endpoints.Count -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)

    $afterStatusText = (& $InstallerPath status --json 2>&1 | Out-String).Trim()
    $afterStatusText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-after.json') -Encoding utf8
    $afterStatus = $afterStatusText | ConvertFrom-Json
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $endpoints = @(Get-CueletEndpoints)
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $drivers = @(Get-WindowsDriver -Online -All -ErrorAction SilentlyContinue |
        Where-Object {
            $_.OriginalFileName -match '(?i)CueletVirtualAudio\.inf'
        } | Select-Object Driver, OriginalFileName, Version, Date,
            ProviderName, ClassName)
    $serviceKeyExists = Test-Path -LiteralPath (
        'HKLM:\SYSTEM\CurrentControlSet\Services\cuelet_virtual_audio')
    $installedSysStillExists = (
        $beforeSysPath -and
        (Test-Path -LiteralPath $beforeSysPath -PathType Leaf))
    $flowTestProcesses = @(
        Get-Process -Name 'Cuelet.VirtualAudio.FlowTest' `
            -ErrorAction SilentlyContinue |
            Select-Object Id, ProcessName, StartTime)
    (& pnputil.exe /enum-drivers /files 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'pnputil-enum-drivers-files.txt') `
            -Encoding utf8
    (& driverquery.exe /fo csv /v 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'driverquery.csv') -Encoding utf8

    $dirty = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volume = Get-Volume -DriveLetter C
    $newSystemEvents = @(Get-WinEvent -LogName System `
        -FilterXPath (
            "*[System[EventRecordID > $($baseline.systemRecordId)]]") `
        -ErrorAction SilentlyContinue)
    $newApplicationEvents = @(Get-WinEvent -LogName Application `
        -FilterXPath (
            "*[System[EventRecordID > $($baseline.applicationRecordId)]]") `
        -ErrorAction SilentlyContinue)
    $newCodeIntegrityEvents = @(Get-WinEvent -LogName `
        'Microsoft-Windows-CodeIntegrity/Operational' `
        -FilterXPath (
            "*[System[EventRecordID > $($baseline.codeIntegrityRecordId)]]") `
        -ErrorAction SilentlyContinue)
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
            ($_.ProviderName -match (
                '(?i)BugCheck|Windows Error Reporting|Kernel-Power|EventLog')) -and
            ($_.Id -in @(41, 1001, 6008) -or
                $_.Message -match '(?i)bugcheck|live kernel|BlueScreen|unexpected shutdown')
        })
    $cueletApplicationErrors = @($newApplicationEvents | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)Cuelet'
    })
    $eventProjection = {
        param($event)
        [ordered]@{
            recordId = $event.RecordId
            timeCreated = $event.TimeCreated.ToString('o')
            provider = $event.ProviderName
            id = $event.Id
            level = $event.LevelDisplayName
            message = [string]$event.Message
        }
    }
    Write-JsonFile -Value @($storageErrors | ForEach-Object $eventProjection) `
        -Path (Join-Path $EvidenceRoot 'storage-hardware-events.json') -Depth 5
    Write-JsonFile -Value @(
        $candidateCiErrors | ForEach-Object $eventProjection) `
        -Path (Join-Path $EvidenceRoot 'candidate-code-integrity-events.json') `
        -Depth 5
    Write-JsonFile -Value @($bugchecks | ForEach-Object $eventProjection) `
        -Path (Join-Path $EvidenceRoot 'bugcheck-livekernel-events.json') `
        -Depth 5
    Write-JsonFile -Value @(
        $cueletApplicationErrors | ForEach-Object $eventProjection) `
        -Path (Join-Path $EvidenceRoot 'cuelet-application-errors.json') `
        -Depth 5

    $residueFree = (
        $null -eq $root -and
        $endpoints.Count -eq 0 -and
        $null -eq $service -and
        $drivers.Count -eq 0 -and
        -not $serviceKeyExists -and
        -not $installedSysStillExists -and
        $flowTestProcesses.Count -eq 0 -and
        -not $afterStatus.packageInstalled)
    $trueStop = (
        $storageErrors.Count -ne 0 -or
        $candidateCiErrors.Count -ne 0 -or
        $bugchecks.Count -ne 0 -or
        $dirty -notmatch '(?i)NOT Dirty' -or
        $volume.HealthStatus -ne 'Healthy' -or
        $volume.OperationalStatus -notcontains 'OK')
    $passed = (
        $uninstallExit -eq 0 -and
        $residueFree -and
        -not $trueStop)
    $result = [ordered]@{
        stage = 'final-runtime-cleanup'
        candidateVersion = $ExpectedVersion
        passed = $passed
        trueStopCondition = $trueStop
        uninstallExitCode = $uninstallExit
        preCleanupInstalledSysPath = $beforeSysPath
        preCleanupInstalledSysSha256 = $beforeSysHash
        residue = [ordered]@{
            rootDevnodePresent = ($null -ne $root)
            endpointCount = $endpoints.Count
            servicePresent = ($null -ne $service)
            driverPackageCount = $drivers.Count
            serviceRegistryKeyPresent = $serviceKeyExists
            installedSysStillExists = [bool]$installedSysStillExists
            flowTestProcessCount = $flowTestProcesses.Count
            installerReportsPackageInstalled = [bool]$afterStatus.packageInstalled
            residueFree = $residueFree
        }
        system = [ordered]@{
            auditStartedAt = [string]$baseline.startedAt
            dirtyQuery = $dirty
            volumeHealth = [string]$volume.HealthStatus
            volumeOperationalStatus = @($volume.OperationalStatus)
            storageOrHardwareErrors = $storageErrors.Count
            candidateCodeIntegrityErrors = $candidateCiErrors.Count
            bugcheckOrLiveKernelEvents = $bugchecks.Count
            cueletApplicationErrors = $cueletApplicationErrors.Count
        }
        completedAt = (Get-Date).ToString('o')
    }
    Write-JsonFile -Value $result -Path (
        Join-Path $EvidenceRoot 'final-cleanup-result.json') -Depth 12
    Write-Host "Final cleanup passed: $passed"
    Write-Host "Residue free: $residueFree"
    Write-Host "True stop condition: $trueStop"
    if ($trueStop) { exit 99 }
    if (-not $passed) { exit 10 }
    exit 0
}
finally {
    Stop-Transcript | Out-Null
}
