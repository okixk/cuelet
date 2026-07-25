[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Before', 'After')]
    [string]$Phase,
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 3)]
    [int]$Cycle,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$prefix = Join-Path $EvidenceRoot ('reboot-' + $Cycle)
$beforePath = $prefix + '-before.json'
$afterPath = $prefix + '-after.json'
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'

function Snapshot {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $endpoints = @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        })
    $allRoots = @(Get-PnpDevice -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -match '(?i)^ROOT\\CUELETVIRTUALAUDIO\\'
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
        systemRecordId = (
            Get-WinEvent -LogName System -MaxEvents 1).RecordId
        applicationRecordId = (
            Get-WinEvent -LogName Application -MaxEvents 1).RecordId
        codeIntegrityRecordId = (
            Get-WinEvent -LogName `
                'Microsoft-Windows-CodeIntegrity/Operational' `
                -MaxEvents 1).RecordId
        root = $root |
            Select-Object Status, Class, FriendlyName, InstanceId
        rootDevices = $allRoots |
            Select-Object Status, Present, Class, FriendlyName, InstanceId
        endpoints = $endpoints |
            Select-Object Status, Present, FriendlyName, InstanceId
        service = $service |
            Select-Object Name, State, StartMode, PathName
        installedSysSha256 = $hash
        healthy = (
            $null -ne $root -and $root.Status -eq 'OK' -and
            $allRoots.Count -eq 1 -and
            $endpoints.Count -eq 2 -and
            @($endpoints | Where-Object {
                $_.Status -ne 'OK'
            }).Count -eq 0 -and
            $null -ne $service -and $service.State -eq 'Running' -and
            $hash -eq $ExpectedSysSha256)
    }
}

if ($Phase -eq 'Before') {
    $before = Snapshot
    if (-not $before.healthy) {
        throw "Cuelet is not healthy before reboot cycle $Cycle."
    }
    $before | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath $beforePath -Encoding utf8
    $before | ConvertTo-Json -Depth 10
    exit 0
}

if (-not (Test-Path -LiteralPath $beforePath -PathType Leaf)) {
    throw "Missing pre-reboot evidence: $beforePath"
}
$before = Get-Content -LiteralPath $beforePath -Raw |
    ConvertFrom-Json
$after = Snapshot
$didReboot = [datetime]$after.bootTime -gt [datetime]$before.bootTime

$flowAttempts = @()
$flowExit = -1
for ($attempt = 1; $attempt -le 3; ++$attempt) {
    $flowRoot = $prefix + "-flow-attempt-$attempt"
    $flowLog = $prefix + "-flow-attempt-$attempt.log"
    & $FlowTestPath --bounded-tone --frequency 997 --seconds 2 `
        --output-dir $flowRoot *>&1 |
        Set-Content -LiteralPath $flowLog -Encoding utf8
    $flowExit = $LASTEXITCODE
    $flowAttempts += [ordered]@{
        attempt = $attempt
        exitCode = $flowExit
        output = $flowRoot
        log = $flowLog
    }
    if ($flowExit -eq 0) {
        break
    }
    if ($attempt -lt 3) {
        Start-Sleep -Seconds 5
    }
}

function Get-EventsAfter {
    param([string]$LogName, [long]$RecordId)
    return @(Get-WinEvent -LogName $LogName `
        -FilterXPath "*[System[EventRecordID > $RecordId]]" `
        -ErrorAction SilentlyContinue)
}
$newSystem = Get-EventsAfter -LogName System `
    -RecordId $before.systemRecordId
$newApplication = Get-EventsAfter -LogName Application `
    -RecordId $before.applicationRecordId
$newCi = Get-EventsAfter `
    -LogName 'Microsoft-Windows-CodeIntegrity/Operational' `
    -RecordId $before.codeIntegrityRecordId
$storageErrors = @($newSystem | Where-Object {
    $_.ProviderName -match (
        '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
    ($_.Level -le 3 -or $_.Id -eq 55)
})
$candidateCiErrors = @($newCi | Where-Object {
    $_.Level -le 3 -and
    $_.Message -match '(?i)CueletVirtualAudio|cuelet_virtual_audio'
})
$pnpErrors = @($newSystem | Where-Object {
    $_.Level -le 3 -and
    $_.ProviderName -match '(?i)Kernel-PnP|UserPnp|Audio' -and
    $_.Message -match '(?i)Cuelet|CUELETVIRTUALAUDIO'
})
$result = [ordered]@{
    cycle = $Cycle
    passed = (
        $didReboot -and $after.healthy -and $flowExit -eq 0 -and
        $storageErrors.Count -eq 0 -and
        $candidateCiErrors.Count -eq 0 -and
        $pnpErrors.Count -eq 0)
    didReboot = $didReboot
    before = $before
    after = $after
    flowExitCode = $flowExit
    flowAttempts = $flowAttempts
    newEventCounts = [ordered]@{
        system = $newSystem.Count
        application = $newApplication.Count
        codeIntegrity = $newCi.Count
        storageOrHardwareErrors = $storageErrors.Count
        candidateCodeIntegrityErrors = $candidateCiErrors.Count
        cueletPnpOrAudioErrors = $pnpErrors.Count
    }
}
$result | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath $afterPath -Encoding utf8
& $recorder -EvidenceRoot $EvidenceRoot `
    -Category ('reboot-' + $Cycle) `
    -ExpectedSysSha256 $ExpectedSysSha256 |
    Set-Content -LiteralPath ($prefix + '-category-health.json') `
    -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw "Health audit failed after reboot cycle $Cycle."
}
$result | ConvertTo-Json -Depth 12
if (-not $result.passed) { exit 10 }
exit 0
