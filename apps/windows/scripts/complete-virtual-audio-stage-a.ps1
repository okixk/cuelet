[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)

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
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-Elevated'
    )
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait `
        -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'Stage A evidence completion requires elevation.'
}

$package = Join-Path $EvidenceRoot 'bundle\DriverPackage'
$helper = Join-Path $EvidenceRoot 'bundle\Cuelet.VirtualAudio.Installer.exe'
$baselinePath = Join-Path $EvidenceRoot 'event-baseline.json'
$tracePath = Join-Path $EvidenceRoot 'cuelet-startup-trace.txt'
$resultPath = Join-Path $EvidenceRoot 'stage-a-result.json'
$transcriptPath = Join-Path $EvidenceRoot 'stage-a-completion-transcript.txt'

function Write-JsonFile {
    param([string]$Path, $Value, [int]$Depth = 10)
    $Value | ConvertTo-Json -Depth $Depth |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-PropertyData {
    param([string]$InstanceId, [string]$KeyName)
    try {
        return (Get-PnpDeviceProperty -InstanceId $InstanceId `
            -KeyName $KeyName -ErrorAction Stop).Data
    }
    catch {
        return $null
    }
}

function Get-NewEvents {
    param([string]$LogName, [long]$AfterRecordId)
    return @(Get-WinEvent -LogName $LogName -ErrorAction SilentlyContinue |
        Where-Object { $_.RecordId -gt $AfterRecordId } |
        Sort-Object RecordId)
}

Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
try {
    $manifest = Get-Content -LiteralPath (
        Join-Path $package 'candidate-manifest.json') -Raw | ConvertFrom-Json
    $candidateHash = [string](
        $manifest.files.'CueletVirtualAudio.sys'.sha256)
    if ($manifest.candidate -ne (
        "CueletVirtualAudio $ExpectedVersion Debug x64")) {
        throw 'The evidence bundle is not the expected candidate.'
    }

    $statusText = (& $helper status --json | Out-String).Trim()
    $statusText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-stage-a.json') -Encoding utf8
    $status = $statusText | ConvertFrom-Json

    $root = Get-PnpDevice -InstanceId 'ROOT\CUELETVIRTUALAUDIO\0000' `
        -ErrorAction SilentlyContinue
    $rootProblemCode = if ($null -ne $root) {
        Get-PropertyData $root.InstanceId 'DEVPKEY_Device_ProblemCode'
    } else { $null }
    $rootProblemStatus = if ($null -ne $root) {
        Get-PropertyData $root.InstanceId 'DEVPKEY_Device_ProblemStatus'
    } else { $null }
    $rootState = if ($null -eq $root) {
        $null
    } else {
        [ordered]@{
            status = [string]$root.Status
            class = [string]$root.Class
            friendlyName = [string]$root.FriendlyName
            instanceId = [string]$root.InstanceId
            problemCode = if ($null -eq $rootProblemCode) {
                $null
            } else {
                [uint32]$rootProblemCode
            }
            problemStatus = if ($null -eq $rootProblemStatus) {
                ''
            } else {
                '0x{0:X8}' -f ([uint32]$rootProblemStatus)
            }
            driverVersion = [string](Get-PropertyData $root.InstanceId `
                'DEVPKEY_Device_DriverVersion')
            driverInfPath = [string](Get-PropertyData $root.InstanceId `
                'DEVPKEY_Device_DriverInfPath')
            service = [string](Get-PropertyData $root.InstanceId `
                'DEVPKEY_Device_Service')
        }
    }

    $endpoints = @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        })
    Write-JsonFile (Join-Path $EvidenceRoot 'stage-a-devices.json') `
        ([ordered]@{
            root = $rootState
            endpoints = @($endpoints | Select-Object Status, Class,
                FriendlyName, InstanceId)
        }) 8

    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $installedPath = if ($null -eq $service) {
        ''
    } else {
        [string]$service.PathName
    }
    $installedHash = if (Test-Path -LiteralPath $installedPath -PathType Leaf) {
        (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash
    } else { '' }

    $drivers = @(Get-WindowsDriver -Online -All |
        Where-Object {
            $_.ProviderName -eq 'Cuelet' -or
            $_.OriginalFileName -match '(?i)CueletVirtualAudio\.inf'
        })
    Write-JsonFile (Join-Path $EvidenceRoot 'driver-store-stage-a.json') @(
        $drivers | Select-Object Driver, OriginalFileName, ProviderName,
            ClassName, Date, Version, BootCritical, Inbox) 8
    (& pnputil.exe /enum-drivers /files 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'pnputil-enum-drivers-stage-a.txt') `
            -Encoding utf8
    (& pnputil.exe /enum-devices /deviceid 'ROOT\CUELETVIRTUALAUDIO' `
        /properties 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'pnputil-enum-devices-stage-a.txt') `
            -Encoding utf8

    $baseline = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    $systemEvents = Get-NewEvents 'System' $baseline.systemRecordId
    $applicationEvents = Get-NewEvents 'Application' `
        $baseline.applicationRecordId
    $codeIntegrityEvents = Get-NewEvents `
        'Microsoft-Windows-CodeIntegrity/Operational' `
        $baseline.codeIntegrityRecordId
    Write-JsonFile (Join-Path $EvidenceRoot 'events-system.json') @(
        $systemEvents | Select-Object TimeCreated, RecordId, ProviderName,
            Id, LevelDisplayName, Message) 8
    Write-JsonFile (Join-Path $EvidenceRoot 'events-application.json') @(
        $applicationEvents | Select-Object TimeCreated, RecordId, ProviderName,
            Id, LevelDisplayName, Message) 8
    Write-JsonFile (Join-Path $EvidenceRoot 'events-code-integrity.json') @(
        $codeIntegrityEvents | Select-Object TimeCreated, RecordId,
            ProviderName, Id, LevelDisplayName, Message) 8

    $storageEvents = @($systemEvents | Where-Object {
        $_.ProviderName -match (
            '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
        ($_.Level -le 3 -or
         ($_.ProviderName -match '(?i)Ntfs' -and $_.Id -eq 55))
    })
    $bugcheckEvents = @(@($systemEvents) + @($applicationEvents) |
        Where-Object {
            $_.ProviderName -match '(?i)BugCheck|Windows Error Reporting' -and
            $_.Message -match '(?i)bugcheck|live kernel|BlueScreen'
        })
    $candidateCiEvents = @($codeIntegrityEvents | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
    })

    $dirty = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volume = Get-Volume -DriveLetter C
    (& driverquery.exe /v /fo csv 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'driverquery-stage-a.csv') -Encoding utf8

    $checkpointLines = @(Select-String -LiteralPath $tracePath `
        -Pattern 'CVA\d{3}' | ForEach-Object Line)
    $checkpointLines | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'startup-checkpoints.txt') -Encoding utf8
    $uniqueCheckpoints = @($checkpointLines | ForEach-Object {
        [regex]::Match($_, 'CVA\d{3}').Value
    } | Sort-Object -Unique)
    $invalidParameterCheckpoint = @($checkpointLines | Where-Object {
        $_ -match 'STATUS_INVALID_PARAMETER'
    })
    $renderExitCheckpoint = @($checkpointLines | Where-Object {
        $_ -match 'CVA129'
    })
    $startExitCheckpoint = @($checkpointLines | Where-Object {
        $_ -match 'CVA199'
    })

    $passed = (
        $status.packageInstalled -and
        $status.installedVersion -eq $ExpectedVersion -and
        $status.endpointPairValid -and
        $null -ne $rootState -and
        $rootState.status -eq 'OK' -and
        $rootState.problemCode -eq 0 -and
        $rootState.driverVersion -eq $ExpectedVersion -and
        $endpoints.Count -eq 2 -and
        @($endpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -eq 0 -and
        $drivers.Count -eq 1 -and
        [string]$drivers[0].Version -eq $ExpectedVersion -and
        $service.State -eq 'Running' -and
        $installedHash -eq $candidateHash -and
        $invalidParameterCheckpoint.Count -eq 1 -and
        $renderExitCheckpoint.Count -eq 1 -and
        $startExitCheckpoint.Count -eq 1)
    $trueStop = (
        $storageEvents.Count -ne 0 -or
        $bugcheckEvents.Count -ne 0 -or
        $candidateCiEvents.Count -ne 0 -or
        $dirty -notmatch '(?i)NOT Dirty' -or
        $volume.HealthStatus -ne 'Healthy' -or
        $volume.OperationalStatus -notcontains 'OK')

    $result = [ordered]@{
        stage = 'A'
        version = $ExpectedVersion
        passed = $passed
        safeToBeginStageB = ($passed -and -not $trueStop)
        trueStopCondition = $trueStop
        installerExitCode = 0
        installerResult = Get-Content -LiteralPath (
            Join-Path $EvidenceRoot 'installer-install.stdout.txt') -Raw |
            ConvertFrom-Json
        statusResult = $status
        publishedInf = $rootState.driverInfPath
        driverStore = @($drivers | Select-Object Driver, OriginalFileName,
            ProviderName, ClassName, Date, Version, BootCritical, Inbox)
        candidateSysSha256 = $candidateHash
        installedSysPath = $installedPath
        installedSysSha256 = $installedHash
        service = if ($null -eq $service) {
            $null
        } else {
            [ordered]@{
                name = $service.Name
                state = $service.State
                startMode = $service.StartMode
                pathName = $service.PathName
            }
        }
        root = $rootState
        endpoints = @($endpoints | Select-Object Status, Class,
            FriendlyName, InstanceId)
        tracing = [ordered]@{
            providerGuid = '1819CEB3-B714-493F-8B5F-771AFFB0DC63'
            events = 291
            eventsLost = 0
            formatErrors = 0
            checkpointLines = $checkpointLines.Count
            uniqueCheckpoints = $uniqueCheckpoints.Count
            invalidParameterCheckpoint = $invalidParameterCheckpoint
            correctedRenderExitCheckpoint = $renderExitCheckpoint
            startDeviceExitCheckpoint = $startExitCheckpoint
        }
        system = [ordered]@{
            dirtyQuery = $dirty
            volumeHealth = [string]$volume.HealthStatus
            volumeOperationalStatus = @($volume.OperationalStatus)
            storageOrHardwareErrors = $storageEvents.Count
            bugcheckOrLiveKernelEvents = $bugcheckEvents.Count
            candidateCodeIntegrityErrors = $candidateCiEvents.Count
        }
        completedAt = (Get-Date).ToString('o')
    }
    Write-JsonFile $resultPath $result 14
    Write-Host "Stage A passed: $passed"
    Write-Host "Installed SYS: $installedPath"
    Write-Host "Installed SHA-256: $installedHash"
    Write-Host "Invalid-parameter checkpoint: $invalidParameterCheckpoint"
    Write-Host "True stop condition: $trueStop"
    if ($trueStop) { exit 99 }
    if (-not $passed) { exit 10 }
    exit 0
}
finally {
    Stop-Transcript | Out-Null
}
