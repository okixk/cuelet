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
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$serviceKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\cuelet_virtual_audio'
$commands = [Collections.Generic.List[string]]::new()

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
    param([string]$Path, $Value, [int]$Depth = 12)
    ConvertTo-Json -InputObject $Value -Depth $Depth |
        Set-Content -LiteralPath $Path -Encoding utf8
}

function Record-Command {
    param([string]$Text)
    $line = "[$((Get-Date).ToString('o'))] $Text"
    $commands.Add($line)
    $commands | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'commands-executed.txt') -Encoding utf8
}

function Get-RecordId {
    param([string]$LogName)
    return (Get-WinEvent -LogName $LogName -MaxEvents 1).RecordId
}

function Get-CueletEndpoints {
    return @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        } | Select-Object Status, Class, FriendlyName, InstanceId)
}

function Get-ParentChain {
    param([string]$DeviceInstanceId)
    $chain = [Collections.Generic.List[string]]::new()
    $current = $DeviceInstanceId
    for ($depth = 0; $depth -lt 16 -and $current; ++$depth) {
        $chain.Add($current)
        $property = Get-PnpDeviceProperty -InstanceId $current `
            -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue
        $parent = [string]$property.Data
        if (-not $parent -or $chain.Contains($parent)) { break }
        $current = $parent
    }
    return @($chain)
}

function Get-CueletState {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $endpoints = @(Get-CueletEndpoints)
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $drivers = @(Get-WindowsDriver -Online -All `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.OriginalFileName -match '(?i)CueletVirtualAudio\.inf'
        } | Select-Object Driver, OriginalFileName, ProviderName,
            ClassName, Date, Version)
    $loadedText = (& driverquery.exe /fo csv /v 2>&1 | Out-String)
    return [ordered]@{
        root = $root | Select-Object Status, Class, FriendlyName, InstanceId
        endpoints = $endpoints
        service = $service |
            Select-Object Name, State, StartMode, PathName
        drivers = $drivers
        serviceKeyPresent = Test-Path -LiteralPath $serviceKey
        loadedDriverPresent = (
            $loadedText -match '(?i)cuelet_virtual_audio|CueletVirtualAudio')
        installedSysPresent = (
            $null -ne $service -and
            (Test-Path -LiteralPath ([string]$service.PathName) `
                -PathType Leaf))
    }
}

function Test-CleanState {
    param($State)
    return (
        $null -eq $State.root -and
        $State.endpoints.Count -eq 0 -and
        $null -eq $State.service -and
        $State.drivers.Count -eq 0 -and
        -not $State.serviceKeyPresent -and
        -not $State.loadedDriverPresent -and
        -not $State.installedSysPresent)
}

if (-not $Elevated) {
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
    throw 'The installer regression requires elevation.'
}
foreach ($path in @($CandidatePath, $InstallerPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required path not found: $path"
    }
}

New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$bundle = Join-Path $EvidenceRoot 'bundle'
$package = Join-Path $bundle 'DriverPackage'
$helper = Join-Path $bundle 'Cuelet.VirtualAudio.Installer.exe'
$transcript = Join-Path $EvidenceRoot 'installer-regression-transcript.txt'
Start-Transcript -LiteralPath $transcript -Force | Out-Null
try {
    Record-Command 'Copy the immutable candidate and rebuilt installer to an isolated bundle'
    New-Item -ItemType Directory -Path $bundle -Force | Out-Null
    Copy-Item -LiteralPath $CandidatePath -Destination $package -Recurse
    Copy-Item -LiteralPath $InstallerPath -Destination $helper
    $certificate = Join-Path (Split-Path -Parent $InstallerPath) `
        'CueletVirtualAudioDevelopment.cer'
    if (Test-Path -LiteralPath $certificate -PathType Leaf) {
        Copy-Item -LiteralPath $certificate -Destination $bundle
    }

    $failures = [Collections.Generic.List[string]]::new()
    $hashResults = [ordered]@{}
    Record-Command 'Verify every hash in candidate-hashes.sha256'
    foreach ($line in Get-Content -LiteralPath (
            Join-Path $package 'candidate-hashes.sha256')) {
        if ($line -notmatch '^([A-Fa-f0-9]{64}) \*(.+)$') {
            throw "Malformed candidate hash line: $line"
        }
        $expected = $Matches[1].ToUpperInvariant()
        $name = $Matches[2]
        $actual = (Get-FileHash -LiteralPath (
            Join-Path $package $name) -Algorithm SHA256).Hash
        $hashResults[$name] = [ordered]@{
            expected = $expected
            actual = $actual
            matched = ($actual -eq $expected)
        }
        if ($actual -ne $expected) {
            $failures.Add("Candidate hash mismatch: $name")
        }
    }
    $manifest = Get-Content -LiteralPath (
        Join-Path $package 'candidate-manifest.json') -Raw |
        ConvertFrom-Json
    if ($manifest.candidate -ne (
        "CueletVirtualAudio $ExpectedVersion Debug x64")) {
        $failures.Add('Candidate manifest identity mismatch.')
    }
    $expectedSysHash =
        [string]$manifest.files.'CueletVirtualAudio.sys'.sha256
    Write-JsonFile (Join-Path $EvidenceRoot 'candidate-hash-verification.json') `
        $hashResults 8
    Write-JsonFile (Join-Path $EvidenceRoot 'bundle-identity.json') `
        ([ordered]@{
            candidate = $manifest.candidate
            sourceCandidatePath = $CandidatePath
            sourceInstallerPath = $InstallerPath
            sourceInstallerSha256 = (
                Get-FileHash -LiteralPath $InstallerPath `
                    -Algorithm SHA256).Hash
            bundleInstallerSha256 = (
                Get-FileHash -LiteralPath $helper `
                    -Algorithm SHA256).Hash
            expectedSysSha256 = $expectedSysHash
        })

    Record-Command 'Verify clean pre-install state'
    $before = Get-CueletState
    Write-JsonFile (Join-Path $EvidenceRoot 'pre-install-state.json') `
        $before 12
    if (-not (Test-CleanState $before)) {
        $failures.Add('Cuelet residue exists before installer regression.')
    }
    $dirtyBefore = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volumeBefore = Get-Volume -DriveLetter C
    if ($dirtyBefore -notmatch '(?i)NOT Dirty' -or
        $volumeBefore.HealthStatus -ne 'Healthy' -or
        $volumeBefore.OperationalStatus -notcontains 'OK') {
        $failures.Add('C: is not clean and healthy before installation.')
    }
    $statusBeforeText = (& $helper status --json | Out-String).Trim()
    $statusBeforeText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-before.json') -Encoding utf8
    $statusBefore = $statusBeforeText | ConvertFrom-Json
    if ($statusBefore.bundledVersion -ne $ExpectedVersion -or
        $statusBefore.packageInstalled) {
        $failures.Add('Isolated installer selected the wrong package or found residue.')
    }
    if ($failures.Count -ne 0) {
        throw "Installer regression preflight failed: $($failures -join '; ')"
    }

    $baseline = [ordered]@{
        startedAt = (Get-Date).ToString('o')
        systemRecordId = Get-RecordId 'System'
        applicationRecordId = Get-RecordId 'Application'
        codeIntegrityRecordId = Get-RecordId `
            'Microsoft-Windows-CodeIntegrity/Operational'
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'event-baseline.json') $baseline
    $rollback = "& `"$helper`" uninstall"
    Record-Command "Rollback command before installation: $rollback"
    $rollback | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'rollback-command.txt') -Encoding utf8

    Record-Command "& '$helper' install --allow-test-package"
    $installText = (
        & $helper install --allow-test-package 2>&1 | Out-String).Trim()
    $installExit = $LASTEXITCODE
    $installText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-install.json') -Encoding utf8
    $installResult = $installText | ConvertFrom-Json

    $deadline = (Get-Date).AddSeconds(30)
    do {
        $statusText = (& $helper status --json | Out-String).Trim()
        $status = $statusText | ConvertFrom-Json
        if ($status.endpointPairValid) { break }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    $statusText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-installed.json') `
        -Encoding utf8

    $installed = Get-CueletState
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $installedSysPath = [string]$service.PathName
    $installedSysHash = if (
        $installedSysPath -and
        (Test-Path -LiteralPath $installedSysPath -PathType Leaf)) {
        (Get-FileHash -LiteralPath $installedSysPath `
            -Algorithm SHA256).Hash
    } else {
        ''
    }
    $rootProblemCode = (
        Get-PnpDeviceProperty -InstanceId $instanceId `
            -KeyName 'DEVPKEY_Device_ProblemCode' `
            -ErrorAction SilentlyContinue).Data
    $rootProblemStatus = (
        Get-PnpDeviceProperty -InstanceId $instanceId `
            -KeyName 'DEVPKEY_Device_ProblemStatus' `
            -ErrorAction SilentlyContinue).Data
    $rootDriverVersion = (
        Get-PnpDeviceProperty -InstanceId $instanceId `
            -KeyName 'DEVPKEY_Device_DriverVersion' `
            -ErrorAction SilentlyContinue).Data
    $relationships = @($installed.endpoints | ForEach-Object {
        [ordered]@{
            friendlyName = $_.FriendlyName
            instanceId = $_.InstanceId
            ancestorChain = @(Get-ParentChain $_.InstanceId)
            belongsToCueletRoot = @(
                Get-ParentChain $_.InstanceId) -contains $instanceId
        }
    })
    $installedVerification = [ordered]@{
        installerExitCode = $installExit
        installerResult = $installResult
        status = $status
        state = $installed
        publishedInf = [string]$status.publishedInf
        installedSysPath = $installedSysPath
        installedSysSha256 = $installedSysHash
        rootProblemCode = $rootProblemCode
        rootProblemStatus = [string]$rootProblemStatus
        rootDriverVersion = [string]$rootDriverVersion
        endpointRelationships = $relationships
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'installed-verification.json') `
        $installedVerification 16
    $installPassed = (
        $installExit -eq 0 -and
        $status.packageInstalled -and
        $status.endpointPairValid -and
        $status.installedVersion -eq $ExpectedVersion -and
        $status.publishedInf -match '^oem\d+\.inf$' -and
        $installed.drivers.Count -eq 1 -and
        $installed.service.State -eq 'Running' -and
        $installed.root.Status -eq 'OK' -and
        $installed.endpoints.Count -eq 2 -and
        @($installed.endpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -eq 0 -and
        @($relationships | Where-Object {
            -not $_.belongsToCueletRoot
        }).Count -eq 0 -and
        $rootProblemCode -eq 0 -and
        [string]$rootDriverVersion -eq $ExpectedVersion -and
        $installedSysHash -eq $expectedSysHash -and
        $installed.loadedDriverPresent)

    Record-Command "& '$helper' uninstall (normal supported workflow)"
    $uninstallText = (& $helper uninstall 2>&1 | Out-String).Trim()
    $uninstallExit = $LASTEXITCODE
    $uninstallText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-uninstall.json') -Encoding utf8
    $uninstallResult = $uninstallText | ConvertFrom-Json

    $deadline = (Get-Date).AddSeconds(30)
    do {
        $after = Get-CueletState
        if (Test-CleanState $after) { break }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    Write-JsonFile (Join-Path $EvidenceRoot 'post-uninstall-state.json') `
        $after 12
    $statusAfterText = (& $helper status --json | Out-String).Trim()
    $statusAfterText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status-after.json') -Encoding utf8
    $statusAfter = $statusAfterText | ConvertFrom-Json
    $automaticServiceCleanup = (
        $null -eq $after.service -and
        -not $after.serviceKeyPresent)
    $cleanupPassed = (
        $uninstallExit -eq 0 -and
        -not $statusAfter.packageInstalled -and
        (Test-CleanState $after) -and
        $automaticServiceCleanup -and
        -not (Test-Path -LiteralPath $installedSysPath))

    Copy-Item -LiteralPath (
        Join-Path $env:SystemRoot 'inf\setupapi.dev.log') -Destination (
        Join-Path $EvidenceRoot 'setupapi.dev.log') -Force
    (& pnputil.exe /enum-drivers /files 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'pnputil-enum-drivers-files.txt') `
            -Encoding utf8
    (& driverquery.exe /fo csv /v 2>&1 | Out-String) |
        Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'driverquery-after.csv') -Encoding utf8

    $dirtyAfter = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volumeAfter = Get-Volume -DriveLetter C
    $newSystem = @(Get-WinEvent -LogName System | Where-Object {
        $_.RecordId -gt $baseline.systemRecordId
    })
    $newApplication = @(Get-WinEvent -LogName Application |
        Where-Object { $_.RecordId -gt $baseline.applicationRecordId })
    $newCi = @(Get-WinEvent -LogName `
        'Microsoft-Windows-CodeIntegrity/Operational' | Where-Object {
            $_.RecordId -gt $baseline.codeIntegrityRecordId
        })
    $storageErrors = @($newSystem | Where-Object {
        $_.ProviderName -match (
            '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
        ($_.Level -le 3 -or $_.Id -eq 55)
    })
    $ciErrors = @($newCi | Where-Object {
        $_.Level -le 3 -and
        $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
    })
    $bugchecks = @(@($newSystem) + @($newApplication) |
        Where-Object {
            $_.ProviderName -match (
                '(?i)BugCheck|Windows Error Reporting|Kernel-Power|EventLog') -and
            ($_.Id -in @(41, 1001, 6008) -or
                $_.Message -match (
                    '(?i)bugcheck|live kernel|BlueScreen|unexpected shutdown'))
        })
    $trueStop = (
        $storageErrors.Count -ne 0 -or
        $ciErrors.Count -ne 0 -or
        $bugchecks.Count -ne 0 -or
        $dirtyAfter -notmatch '(?i)NOT Dirty' -or
        $volumeAfter.HealthStatus -ne 'Healthy' -or
        $volumeAfter.OperationalStatus -notcontains 'OK')
    $passed = $installPassed -and $cleanupPassed -and -not $trueStop
    $result = [ordered]@{
        stage = 'hardened-installer-regression'
        candidate = $manifest.candidate
        passed = $passed
        trueStopCondition = $trueStop
        installPassed = $installPassed
        uninstallPassed = $cleanupPassed
        normalUninstallExitCode = $uninstallExit
        manualScDeleteUsed = $false
        automaticDeferredServiceCleanup = $automaticServiceCleanup
        publishedInf = [string]$status.publishedInf
        installedSysPath = $installedSysPath
        installedSysSha256 = $installedSysHash
        expectedSysSha256 = $expectedSysHash
        endpointRelationshipCount = $relationships.Count
        endpointRelationshipsValid = (
            @($relationships | Where-Object {
                -not $_.belongsToCueletRoot
            }).Count -eq 0)
        postUninstallClean = (Test-CleanState $after)
        system = [ordered]@{
            dirtyBefore = $dirtyBefore
            dirtyAfter = $dirtyAfter
            volumeHealth = [string]$volumeAfter.HealthStatus
            volumeOperationalStatus = @($volumeAfter.OperationalStatus)
            storageOrHardwareErrors = $storageErrors.Count
            candidateCodeIntegrityErrors = $ciErrors.Count
            bugcheckOrLiveKernelEvents = $bugchecks.Count
        }
        startedAt = $baseline.startedAt
        completedAt = (Get-Date).ToString('o')
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'installer-regression-result.json') `
        $result 14
    Write-Host "Installer regression passed: $passed"
    Write-Host "Automatic deferred service cleanup: $automaticServiceCleanup"
    if ($trueStop) { exit 99 }
    if (-not $passed) { exit 10 }
    exit 0
}
finally {
    Stop-Transcript | Out-Null
}
