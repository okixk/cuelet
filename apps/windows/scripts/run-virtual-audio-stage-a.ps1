[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CandidatePath,
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$CandidatePath = [IO.Path]::GetFullPath($CandidatePath)
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$sessionName = 'CueletStartup'
$providerGuid = '#{1819CEB3-B714-493F-8B5F-771AFFB0DC63}'
$hardwareId = 'ROOT\CUELETVIRTUALAUDIO'
$serviceName = 'cuelet_virtual_audio'
$serviceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\$serviceName"
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

$rollbackCommand = (
    "& {0} uninstall" -f (Quote-Argument $InstallerPath))
Write-Host "Rollback command: $rollbackCommand" -ForegroundColor Yellow

if (-not $Elevated) {
    if (Test-Path -LiteralPath $EvidenceRoot) {
        throw "Refusing to reuse Stage A evidence directory: $EvidenceRoot"
    }
    New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
    $rollbackCommand | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'rollback-command.txt') -Encoding utf8
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-CandidatePath', (Quote-Argument $CandidatePath),
        '-InstallerPath', (Quote-Argument $InstallerPath),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-Elevated'
    )
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait `
        -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}

if (-not (Test-Administrator)) {
    throw 'Stage A requires an elevated PowerShell token.'
}
if (-not (Test-Path -LiteralPath $CandidatePath -PathType Container)) {
    throw "Candidate package not found: $CandidatePath"
}
if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
    throw "Installer helper not found: $InstallerPath"
}
foreach ($tool in @($tracelog, $tracepdb, $tracefmt)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required WPP tool not found: $tool"
    }
}

$commandsPath = Join-Path $EvidenceRoot 'commands-executed.txt'
$transcriptPath = Join-Path $EvidenceRoot 'stage-a-transcript.txt'
$bundle = Join-Path $EvidenceRoot 'bundle'
$package = Join-Path $bundle 'DriverPackage'
$helper = Join-Path $bundle 'Cuelet.VirtualAudio.Installer.exe'
$etlPath = Join-Path $EvidenceRoot 'cuelet-startup.etl'
$tmfPath = Join-Path $EvidenceRoot 'tmf'
$traceTextPath = Join-Path $EvidenceRoot 'cuelet-startup-trace.txt'
$resultPath = Join-Path $EvidenceRoot 'stage-a-result.json'
$failures = [Collections.Generic.List[string]]::new()
$observations = [Collections.Generic.List[object]]::new()
$traceStarted = $false
$installProcess = $null

function Record-Command {
    param([string]$Command)
    $line = '[{0:o}] {1}' -f (Get-Date), $Command
    Add-Content -LiteralPath $commandsPath -Value $line -Encoding utf8
    Write-Host "`n> $Command"
}

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

function Get-CueletDevices {
    return @(Get-PnpDevice -PresentOnly:$false -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -match '(?i)CUELETVIRTUALAUDIO' -or
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        })
}

function Get-RootSnapshot {
    $root = @(Get-CueletDevices | Where-Object {
        $_.InstanceId -match '(?i)^ROOT\\CUELETVIRTUALAUDIO(?:\\|$)'
    } | Select-Object -First 1)
    if ($root.Count -eq 0) { return $null }
    $device = $root[0]
    $problemCode = Get-PropertyData $device.InstanceId `
        'DEVPKEY_Device_ProblemCode'
    $problemStatus = Get-PropertyData $device.InstanceId `
        'DEVPKEY_Device_ProblemStatus'
    return [ordered]@{
        observedAt = (Get-Date).ToString('o')
        status = [string]$device.Status
        class = [string]$device.Class
        friendlyName = [string]$device.FriendlyName
        instanceId = [string]$device.InstanceId
        problemCode = if ($null -eq $problemCode) {
            $null
        } else {
            [uint32]$problemCode
        }
        problemStatus = if ($null -eq $problemStatus) {
            ''
        } else {
            '0x{0:X8}' -f ([uint32]$problemStatus)
        }
        driverVersion = [string](Get-PropertyData $device.InstanceId `
            'DEVPKEY_Device_DriverVersion')
        driverInfPath = [string](Get-PropertyData $device.InstanceId `
            'DEVPKEY_Device_DriverInfPath')
        service = [string](Get-PropertyData $device.InstanceId `
            'DEVPKEY_Device_Service')
    }
}

function Resolve-ServiceImage {
    param([string]$ImagePath)
    if ([string]::IsNullOrWhiteSpace($ImagePath)) { return '' }
    $path = $ImagePath.Trim().Trim('"')
    if ($path.StartsWith('\??\')) { $path = $path.Substring(4) }
    if ($path.StartsWith(
        '\SystemRoot\', [StringComparison]::OrdinalIgnoreCase)) {
        $path = Join-Path $env:SystemRoot $path.Substring(12)
    }
    return [Environment]::ExpandEnvironmentVariables($path)
}

function Get-EventRecordId {
    param([string]$LogName)
    $event = Get-WinEvent -LogName $LogName -MaxEvents 1 `
        -ErrorAction SilentlyContinue
    if ($null -eq $event) { return 0L }
    return [long]$event.RecordId
}

function Get-NewEvents {
    param([string]$LogName, [long]$AfterRecordId)
    return @(Get-WinEvent -LogName $LogName -ErrorAction SilentlyContinue |
        Where-Object { $_.RecordId -gt $AfterRecordId } |
        Sort-Object RecordId)
}

function Get-CleanupState {
    $devices = @(Get-CueletDevices)
    $drivers = @(Get-WindowsDriver -Online -All -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProviderName -eq 'Cuelet' -or
            $_.OriginalFileName -match '(?i)CueletVirtualAudio\.inf'
        })
    $systemDrivers = @(Get-CimInstance Win32_SystemDriver `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.Name -match '(?i)cuelet' -or
            $_.PathName -match '(?i)CueletVirtualAudio'
        })
    $services = @(Get-Service -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)cuelet' })
    return [ordered]@{
        driverStoreCueletCount = $drivers.Count
        pnpCueletCount = $devices.Count
        systemDriverCueletCount = $systemDrivers.Count
        serviceCueletCount = $services.Count
        serviceRegistryPresent = Test-Path -LiteralPath $serviceKey
        processes = @(Get-Process -ErrorAction SilentlyContinue |
            Where-Object {
                $_.ProcessName -match '(?i)^Cuelet' -and
                $_.ProcessName -ne 'Cuelet'
            } | Select-Object Id, ProcessName, Path)
    }
}

Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
try {
    Record-Command "Copy immutable candidate and installer into $bundle"
    New-Item -ItemType Directory -Path $bundle -Force | Out-Null
    Copy-Item -LiteralPath $CandidatePath -Destination $package -Recurse
    Copy-Item -LiteralPath $InstallerPath -Destination $helper
    $certificateSource = Join-Path (Split-Path -Parent $InstallerPath) `
        'CueletVirtualAudioDevelopment.cer'
    if (Test-Path -LiteralPath $certificateSource -PathType Leaf) {
        Copy-Item -LiteralPath $certificateSource -Destination $bundle
    }
    Copy-Item -LiteralPath $PSCommandPath -Destination (
        Join-Path $EvidenceRoot 'stage-a-runner.ps1')

    Record-Command 'Verify candidate-hashes.sha256 and manifest identity'
    $hashListPath = Join-Path $package 'candidate-hashes.sha256'
    if (-not (Test-Path -LiteralPath $hashListPath -PathType Leaf)) {
        throw 'Candidate hash lock file is missing.'
    }
    $hashResults = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath $hashListPath) {
        if ($line -notmatch '^([A-Fa-f0-9]{64}) \*(.+)$') {
            throw "Malformed candidate hash line: $line"
        }
        $expectedHash = $Matches[1].ToUpperInvariant()
        $fileName = $Matches[2]
        $filePath = Join-Path $package $fileName
        $actualHash = (Get-FileHash -LiteralPath $filePath `
            -Algorithm SHA256).Hash
        $hashResults[$fileName] = $actualHash
        if ($actualHash -ne $expectedHash) {
            $failures.Add("Hash mismatch for $fileName.")
        }
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'bundle-hashes.json') $hashResults
    $manifest = Get-Content -LiteralPath (
        Join-Path $package 'candidate-manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.candidate -ne (
        "CueletVirtualAudio $ExpectedVersion Debug x64")) {
        $failures.Add('Candidate manifest identity is wrong.')
    }
    $candidateSysHash = $manifest.files.'CueletVirtualAudio.sys'.sha256

    Record-Command 'Verify exact SYS/CAT signatures and certificate trust'
    $sysSignature = Get-AuthenticodeSignature -LiteralPath (
        Join-Path $package 'CueletVirtualAudio.sys')
    $catSignature = Get-AuthenticodeSignature -LiteralPath (
        Join-Path $package 'cueletvirtualaudio.cat')
    if ($sysSignature.Status -ne 'Valid' -or
        $catSignature.Status -ne 'Valid' -or
        $sysSignature.SignerCertificate.Thumbprint -ne
            $catSignature.SignerCertificate.Thumbprint) {
        $failures.Add('Candidate SYS/CAT signature verification failed.')
    }
    $thumbprint = $sysSignature.SignerCertificate.Thumbprint
    if (-not (Test-Path -LiteralPath "Cert:\LocalMachine\Root\$thumbprint") -or
        -not (Test-Path -LiteralPath (
            "Cert:\LocalMachine\TrustedPublisher\$thumbprint"))) {
        $failures.Add('Development certificate trust is incomplete.')
    }

    Record-Command "bcdedit.exe /enum '{current}' and Confirm-SecureBootUEFI"
    $bcd = (& bcdedit.exe /enum '{current}' 2>&1 | Out-String)
    $bcd | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'bcd-current.txt') -Encoding utf8
    if ($bcd -notmatch '(?im)^testsigning\s+Yes\s*$') {
        $failures.Add('Test-signing is not active.')
    }
    try {
        $secureBoot = Confirm-SecureBootUEFI
        if ($secureBoot) { $failures.Add('Secure Boot is enabled.') }
    }
    catch {
        $secureBoot = $null
        $failures.Add("Could not verify Secure Boot: $($_.Exception.Message)")
    }

    Record-Command 'Verify clean pre-install package, devnode, service, and volume state'
    $cleanBefore = Get-CleanupState
    Write-JsonFile (Join-Path $EvidenceRoot 'pre-install-state.json') `
        $cleanBefore 8
    if ($cleanBefore.driverStoreCueletCount -ne 0 -or
        $cleanBefore.pnpCueletCount -ne 0 -or
        $cleanBefore.systemDriverCueletCount -ne 0 -or
        $cleanBefore.serviceCueletCount -ne 0 -or
        $cleanBefore.serviceRegistryPresent) {
        $failures.Add('Cuelet driver residue exists before Stage A.')
    }
    $dirtyBefore = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volumeBefore = Get-Volume -DriveLetter C
    if ($dirtyBefore -notmatch '(?i)NOT Dirty' -or
        $volumeBefore.HealthStatus -ne 'Healthy' -or
        $volumeBefore.OperationalStatus -notcontains 'OK') {
        $failures.Add('System volume is not clean and healthy before Stage A.')
    }

    Record-Command "& '$helper' status --json"
    $statusBeforeText = (& $helper status --json | Out-String).Trim()
    $statusBeforeText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-before.json') -Encoding utf8
    $statusBefore = $statusBeforeText | ConvertFrom-Json
    if ($statusBefore.bundledVersion -ne $ExpectedVersion -or
        $statusBefore.packageInstalled -or
        $statusBefore.renderEndpointPresent -or
        $statusBefore.captureEndpointPresent) {
        $failures.Add('Installer does not select exactly the clean candidate.')
    }
    if ($failures.Count -ne 0) {
        throw "Stage A preflight failed: $($failures -join '; ')"
    }

    $baseline = [ordered]@{
        local = (Get-Date).ToString('o')
        systemRecordId = Get-EventRecordId 'System'
        applicationRecordId = Get-EventRecordId 'Application'
        codeIntegrityRecordId = Get-EventRecordId `
            'Microsoft-Windows-CodeIntegrity/Operational'
        setupApiBytes = (Get-Item -LiteralPath (
            Join-Path $env:SystemRoot 'inf\setupapi.dev.log')).Length
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'event-baseline.json') $baseline

    Record-Command "Rollback command before installation: $rollbackCommand"
    $rollbackCommand | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'rollback-command.txt') -Encoding utf8

    Record-Command "$tracelog -start $sessionName -guid $providerGuid -f $etlPath"
    & $tracelog -stop $sessionName 2>&1 | Out-Null
    & $tracelog -start $sessionName -guid $providerGuid -f $etlPath `
        -level 5 -flag 0x3 -b 64 -min 4 -max 16 -ft 1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not start WPP trace session (exit $LASTEXITCODE)."
    }
    $traceStarted = $true

    $stdoutPath = Join-Path $EvidenceRoot 'installer-install.stdout.txt'
    $stderrPath = Join-Path $EvidenceRoot 'installer-install.stderr.txt'
    $installStarted = Get-Date
    Record-Command "& '$helper' install --allow-test-package (asynchronous polling)"
    $installProcess = Start-Process -FilePath $helper `
        -ArgumentList @('install', '--allow-test-package') -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath `
        -NoNewWindow

    $installedSysPath = ''
    $installedSysHash = ''
    $capturedImagePath = ''
    $lastObservationKey = ''
    $pollDeadline = (Get-Date).AddSeconds(50)
    while ((Get-Date) -lt $pollDeadline) {
        $snapshot = Get-RootSnapshot
        $imagePath = ''
        if (Test-Path -LiteralPath $serviceKey) {
            $imagePath = [string](Get-ItemProperty -LiteralPath $serviceKey `
                -Name ImagePath -ErrorAction SilentlyContinue).ImagePath
            if (-not [string]::IsNullOrWhiteSpace($imagePath)) {
                $capturedImagePath = $imagePath
                $resolvedImage = Resolve-ServiceImage $imagePath
                if (Test-Path -LiteralPath $resolvedImage -PathType Leaf) {
                    $installedSysPath = [IO.Path]::GetFullPath($resolvedImage)
                    $installedSysHash = (Get-FileHash -LiteralPath `
                        $installedSysPath -Algorithm SHA256).Hash
                }
            }
        }
        $endpointCount = @(Get-CueletDevices | Where-Object {
            $_.Class -eq 'AudioEndpoint'
        }).Count
        $observationKey = '{0}|{1}|{2}|{3}|{4}' -f `
            $snapshot.status, $snapshot.problemCode, $snapshot.problemStatus,
            $imagePath, $endpointCount
        if ($observationKey -ne $lastObservationKey) {
            $observations.Add([ordered]@{
                observedAt = (Get-Date).ToString('o')
                root = $snapshot
                serviceImagePath = $imagePath
                installedSysPath = $installedSysPath
                installedSysSha256 = $installedSysHash
                endpointCount = $endpointCount
            })
            $lastObservationKey = $observationKey
        }
        if ($installProcess.HasExited) { break }
        Start-Sleep -Milliseconds 250
        $installProcess.Refresh()
    }
    if (-not $installProcess.HasExited) {
        $failures.Add('Installer exceeded the bounded 50-second Stage A window.')
        Stop-Process -Id $installProcess.Id -Force
    }
    $installProcess.WaitForExit()
    $installExitCode = $installProcess.ExitCode
    $installerText = (Get-Content -LiteralPath $stdoutPath -Raw `
        -ErrorAction SilentlyContinue).Trim()
    try { $installerResult = $installerText | ConvertFrom-Json }
    catch { $installerResult = $null }

    Record-Command "$tracelog -stop $sessionName"
    & $tracelog -stop $sessionName
    $traceStarted = $false

    Record-Command 'Copy and isolate the new SetupAPI installation section'
    $setupApiPath = Join-Path $env:SystemRoot 'inf\setupapi.dev.log'
    Copy-Item -LiteralPath $setupApiPath -Destination (
        Join-Path $EvidenceRoot 'setupapi.dev.log')
    $setupLines = @(Get-Content -LiteralPath $setupApiPath)
    $matchingIndexes = for ($index = 0; $index -lt $setupLines.Count; ++$index) {
        if ($setupLines[$index] -like "*$EvidenceRoot*" -or
            $setupLines[$index] -match [regex]::Escape($ExpectedVersion)) {
            $index
        }
    }
    if ($matchingIndexes.Count -ne 0) {
        $startIndex = [Math]::Max(0, ($matchingIndexes |
            Measure-Object -Minimum).Minimum - 2)
        $setupLines[$startIndex..($setupLines.Count - 1)] |
            Set-Content -LiteralPath (
                Join-Path $EvidenceRoot 'setupapi-stage-a-section.txt') `
                -Encoding utf8
    }

    Record-Command "$tracepdb and $tracefmt decode startup trace"
    New-Item -ItemType Directory -Path $tmfPath -Force | Out-Null
    $pdbPath = Join-Path $package 'CueletVirtualAudio.pdb'
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $tracepdb -f $pdbPath -p $tmfPath -v 2>&1 |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'tracepdb.log') -Encoding utf8
    $tracePdbExit = $LASTEXITCODE
    & $tracefmt $etlPath -p $tmfPath -o $traceTextPath -nosummary `
        -hires -sortableTime -timeZoneSuffix -cp utf8 2>&1 |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'tracefmt.log') -Encoding utf8
    $traceFmtExit = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference
    if ($tracePdbExit -ne 0 -or $traceFmtExit -ne 0) {
        $failures.Add(
            "WPP decode failed: tracepdb=$tracePdbExit tracefmt=$traceFmtExit")
    }

    $finalDevices = @(Get-CueletDevices)
    $finalRoot = Get-RootSnapshot
    $finalEndpoints = @($finalDevices | Where-Object {
        $_.Class -eq 'AudioEndpoint' -and
        $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
    })
    $stagePassed = (
        $installExitCode -eq 0 -and
        $null -ne $finalRoot -and
        $finalRoot.status -eq 'OK' -and
        $finalEndpoints.Count -eq 2 -and
        @($finalEndpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -eq 0)

    if ($stagePassed -and
        ($installedSysHash -ne $candidateSysHash -or
         [string]::IsNullOrWhiteSpace($installedSysPath))) {
        $stagePassed = $false
        $failures.Add('Installed SYS path/hash was not the exact candidate.')
    }

    $cleanupExitCode = $null
    if (-not $stagePassed) {
        Record-Command "& '$helper' uninstall (automatic Stage A cleanup)"
        & $helper uninstall 2>&1 | Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'installer-uninstall.txt') -Encoding utf8
        $cleanupExitCode = $LASTEXITCODE
        Start-Sleep -Seconds 5
    }

    $cleanupState = Get-CleanupState
    $dirtyAfter = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volumeAfter = Get-Volume -DriveLetter C
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
    $bugcheckEvents = @($systemEvents + $applicationEvents |
        Where-Object {
            $_.ProviderName -match '(?i)BugCheck|Windows Error Reporting' -and
            $_.Message -match '(?i)bugcheck|live kernel|BlueScreen'
        })
    $candidateCiEvents = @($codeIntegrityEvents | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
    })
    $cleanupPassed = if ($stagePassed) {
        $cleanupState.driverStoreCueletCount -eq 1 -and
        $cleanupState.pnpCueletCount -ge 3 -and
        $cleanupState.systemDriverCueletCount -eq 1 -and
        $cleanupState.serviceCueletCount -eq 1 -and
        $cleanupState.serviceRegistryPresent
    } else {
        $cleanupState.driverStoreCueletCount -eq 0 -and
        $cleanupState.pnpCueletCount -eq 0 -and
        $cleanupState.systemDriverCueletCount -eq 0 -and
        $cleanupState.serviceCueletCount -eq 0 -and
        -not $cleanupState.serviceRegistryPresent
    }
    $trueStop = (
        $storageEvents.Count -ne 0 -or
        $bugcheckEvents.Count -ne 0 -or
        $candidateCiEvents.Count -ne 0 -or
        $dirtyAfter -notmatch '(?i)NOT Dirty' -or
        $volumeAfter.HealthStatus -ne 'Healthy' -or
        $volumeAfter.OperationalStatus -notcontains 'OK' -or
        -not $cleanupPassed)

    $traceCheckpoints = @()
    if (Test-Path -LiteralPath $traceTextPath -PathType Leaf) {
        $traceCheckpoints = @(Select-String -LiteralPath $traceTextPath `
            -Pattern 'CVA\d{3}' | ForEach-Object { $_.Line })
        $traceCheckpoints | Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'startup-checkpoints.txt') -Encoding utf8
    }
    $lastCheckpoint = if ($traceCheckpoints.Count -eq 0) {
        ''
    } else {
        $traceCheckpoints[-1]
    }
    $result = [ordered]@{
        stage = 'A'
        version = $ExpectedVersion
        passed = $stagePassed
        safeToBeginStageB = ($stagePassed -and -not $trueStop)
        trueStopCondition = $trueStop
        installStartedAt = $installStarted.ToString('o')
        completedAt = (Get-Date).ToString('o')
        installerExitCode = $installExitCode
        installerResult = $installerResult
        candidateSysSha256 = $candidateSysHash
        installedSysPath = $installedSysPath
        installedSysSha256 = $installedSysHash
        configuredServiceImage = $capturedImagePath
        root = $finalRoot
        endpoints = @($finalEndpoints | Select-Object Status, Class,
            FriendlyName, InstanceId)
        observations = @($observations)
        tracing = [ordered]@{
            providerGuid = $providerGuid
            tracePdbExitCode = $tracePdbExit
            traceFmtExitCode = $traceFmtExit
            checkpointLines = $traceCheckpoints.Count
            lastCheckpoint = $lastCheckpoint
        }
        cleanup = [ordered]@{
            attempted = -not $stagePassed
            installerExitCode = $cleanupExitCode
            passed = $cleanupPassed
            state = $cleanupState
        }
        system = [ordered]@{
            dirtyQueryBefore = $dirtyBefore
            dirtyQueryAfter = $dirtyAfter
            volumeHealthAfter = [string]$volumeAfter.HealthStatus
            volumeOperationalStatusAfter = @($volumeAfter.OperationalStatus)
            storageOrHardwareErrors = $storageEvents.Count
            bugcheckOrLiveKernelEvents = $bugcheckEvents.Count
            candidateCodeIntegrityErrors = $candidateCiEvents.Count
        }
        failures = @($failures)
    }
    Write-JsonFile $resultPath $result 14
    Write-Host "`nStage A passed: $stagePassed"
    Write-Host "Installed SYS: $installedSysPath"
    Write-Host "Installed SHA-256: $installedSysHash"
    Write-Host "Last WPP checkpoint: $lastCheckpoint"
    Write-Host "True stop condition: $trueStop"

    if ($trueStop) { exit 99 }
    if (-not $stagePassed) { exit 10 }
    exit 0
}
finally {
    if ($traceStarted) {
        & $tracelog -stop $sessionName 2>&1 | Out-Null
    }
    Stop-Transcript | Out-Null
}
