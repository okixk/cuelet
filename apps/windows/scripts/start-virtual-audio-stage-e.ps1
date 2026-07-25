[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BundleRoot,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$BundleRoot = [IO.Path]::GetFullPath($BundleRoot)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$package = Join-Path $BundleRoot 'DriverPackage'
$helper = Join-Path $BundleRoot 'Cuelet.VirtualAudio.Installer.exe'
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$sessionName = 'CueletStageE'
$providerGuid = '#{1819CEB3-B714-493F-8B5F-771AFFB0DC63}'
$tracelog = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\tracelog.exe'

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
        $parent = [string](
            Get-PnpDeviceProperty -InstanceId $current `
                -KeyName 'DEVPKEY_Device_Parent' `
                -ErrorAction SilentlyContinue).Data
        if (-not $parent -or $chain.Contains($parent)) { break }
        $current = $parent
    }
    return @($chain)
}

if (-not $Elevated) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-BundleRoot', (Quote-Argument $BundleRoot),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-ExpectedVersion', (Quote-Argument $ExpectedVersion),
        '-ExpectedSysSha256', (Quote-Argument $ExpectedSysSha256),
        '-Elevated'
    )
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait `
        -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'Stage E installation requires elevation.'
}
foreach ($path in @($package, $helper, $tracelog)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required Stage E path not found: $path"
    }
}

New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
Start-Transcript -LiteralPath (
    Join-Path $EvidenceRoot 'stage-e-start-transcript.txt') -Force |
    Out-Null
$traceStarted = $false
try {
    $hashes = [ordered]@{}
    foreach ($line in Get-Content -LiteralPath (
            Join-Path $package 'candidate-hashes.sha256')) {
        if ($line -notmatch '^([A-Fa-f0-9]{64}) \*(.+)$') {
            throw "Malformed candidate hash line: $line"
        }
        $expected = $Matches[1].ToUpperInvariant()
        $name = $Matches[2]
        $actual = (Get-FileHash -LiteralPath (
            Join-Path $package $name) -Algorithm SHA256).Hash
        $hashes[$name] = $actual
        if ($actual -ne $expected) {
            throw "Stage E candidate hash mismatch: $name"
        }
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'candidate-hashes.json') `
        $hashes 6

    $preRoot = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $preEndpoints = @(Get-CueletEndpoints)
    $preService = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $preDrivers = @(Get-WindowsDriver -Online -All |
        Where-Object {
            $_.OriginalFileName -match '(?i)CueletVirtualAudio\.inf'
        })
    $preServiceKey = Test-Path -LiteralPath (
        'HKLM:\SYSTEM\CurrentControlSet\Services\cuelet_virtual_audio')
    if ($null -ne $preRoot -or $preEndpoints.Count -ne 0 -or
        $null -ne $preService -or $preDrivers.Count -ne 0 -or
        $preServiceKey) {
        throw 'Stage E did not begin from a clean Cuelet state.'
    }
    $dirtyBefore = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
    $volumeBefore = Get-Volume -DriveLetter C
    if ($dirtyBefore -notmatch '(?i)NOT Dirty' -or
        $volumeBefore.HealthStatus -ne 'Healthy' -or
        $volumeBefore.OperationalStatus -notcontains 'OK') {
        throw 'C: is not clean and healthy before Stage E.'
    }

    $baseline = [ordered]@{
        startedAt = (Get-Date).ToString('o')
        systemRecordId = Get-RecordId 'System'
        applicationRecordId = Get-RecordId 'Application'
        codeIntegrityRecordId = Get-RecordId `
            'Microsoft-Windows-CodeIntegrity/Operational'
        dirtyQuery = $dirtyBefore
        volumeHealth = [string]$volumeBefore.HealthStatus
        volumeOperationalStatus = @($volumeBefore.OperationalStatus)
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'stage-e-baseline.json') `
        $baseline 8
    $rollback = "& `"$helper`" uninstall"
    $rollback | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'rollback-command.txt') -Encoding utf8
    Write-Host "Rollback command before installation: $rollback"

    & $tracelog -stop $sessionName 2>&1 | Out-Null
    $etl = Join-Path $EvidenceRoot 'stage-e-lifecycle.etl'
    & $tracelog -start $sessionName -guid $providerGuid -f $etl `
        -level 5 -flag 0x3 -b 64 -min 4 -max 32 -ft 1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not start Stage E WPP session: $LASTEXITCODE"
    }
    $traceStarted = $true

    $installText = (
        & $helper install --allow-test-package 2>&1 | Out-String).Trim()
    $installExit = $LASTEXITCODE
    $installText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-install.json') -Encoding utf8
    $install = $installText | ConvertFrom-Json
    $deadline = (Get-Date).AddSeconds(30)
    do {
        $statusText = (& $helper status --json | Out-String).Trim()
        $status = $statusText | ConvertFrom-Json
        $endpoints = @(Get-CueletEndpoints)
        if ($status.endpointPairValid -and $endpoints.Count -eq 2) {
            break
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    $statusText | Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'installer-status.json') -Encoding utf8

    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $installedSysPath = [string]$service.PathName
    $installedHash = if (
        $installedSysPath -and
        (Test-Path -LiteralPath $installedSysPath -PathType Leaf)) {
        (Get-FileHash -LiteralPath $installedSysPath `
            -Algorithm SHA256).Hash
    } else {
        ''
    }
    $relationships = @($endpoints | ForEach-Object {
        $chain = @(Get-ParentChain $_.InstanceId)
        [ordered]@{
            friendlyName = $_.FriendlyName
            instanceId = $_.InstanceId
            ancestorChain = $chain
            belongsToCueletRoot = ($chain -contains $instanceId)
        }
    })
    $passed = (
        $installExit -eq 0 -and
        $install.packageInstalled -and
        $status.endpointPairValid -and
        $status.installedVersion -eq $ExpectedVersion -and
        $null -ne $root -and $root.Status -eq 'OK' -and
        $null -ne $service -and $service.State -eq 'Running' -and
        $endpoints.Count -eq 2 -and
        @($endpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -eq 0 -and
        @($relationships | Where-Object {
            -not $_.belongsToCueletRoot
        }).Count -eq 0 -and
        $installedHash -eq $ExpectedSysSha256)
    $result = [ordered]@{
        stage = 'E-start'
        passed = $passed
        candidateVersion = $ExpectedVersion
        installedSysPath = $installedSysPath
        installedSysSha256 = $installedHash
        status = $status
        root = $root |
            Select-Object Status, Class, FriendlyName, InstanceId
        endpoints = $endpoints
        endpointRelationships = $relationships
        service = $service |
            Select-Object Name, State, StartMode, PathName
        traceSession = $sessionName
        traceFile = $etl
        completedAt = (Get-Date).ToString('o')
    }
    Write-JsonFile (Join-Path $EvidenceRoot 'stage-e-start-result.json') `
        $result 16
    if (-not $passed) {
        & $tracelog -stop $sessionName 2>&1 | Out-Null
        $traceStarted = $false
        & $helper uninstall 2>&1 | Set-Content -LiteralPath (
            Join-Path $EvidenceRoot 'rollback-uninstall.txt') -Encoding utf8
        exit 10
    }
    $traceStarted = $false
    Write-Host 'Stage E installation and trace start passed.'
    exit 0
}
finally {
    if ($traceStarted) {
        & $tracelog -stop $sessionName 2>&1 | Out-Null
    }
    Stop-Transcript | Out-Null
}
