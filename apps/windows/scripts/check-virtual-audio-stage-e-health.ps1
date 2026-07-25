[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [int]$SoakProcessId
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$baseline = Get-Content -LiteralPath (
    Join-Path $EvidenceRoot 'stage-e-baseline.json') -Raw |
    ConvertFrom-Json
$samplePath = Join-Path $EvidenceRoot 'soak-2h\soak-samples.jsonl'
$telemetry = @()
if (Test-Path -LiteralPath $samplePath -PathType Leaf) {
    $telemetry = @(Get-Content -LiteralPath $samplePath |
        ForEach-Object { $_ | ConvertFrom-Json })
}
$process = Get-Process -Id $SoakProcessId -ErrorAction SilentlyContinue
$root = Get-PnpDevice -InstanceId 'ROOT\CUELETVIRTUALAUDIO\0000' `
    -ErrorAction SilentlyContinue
$endpoints = @(Get-PnpDevice -Class AudioEndpoint `
    -ErrorAction SilentlyContinue | Where-Object {
        $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
    })
$service = Get-CimInstance Win32_SystemDriver -Filter (
    "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
function Get-EventsAfter {
    param([string]$LogName, [long]$RecordId)
    return @(Get-WinEvent -LogName $LogName `
        -FilterXPath "*[System[EventRecordID > $RecordId]]" `
        -ErrorAction SilentlyContinue)
}
$newSystem = Get-EventsAfter -LogName System `
    -RecordId $baseline.systemRecordId
$newApplication = Get-EventsAfter -LogName Application `
    -RecordId $baseline.applicationRecordId
$newCi = Get-EventsAfter `
    -LogName 'Microsoft-Windows-CodeIntegrity/Operational' `
    -RecordId $baseline.codeIntegrityRecordId
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
$volume = Get-Volume -DriveLetter C
$failedFixtures = @($telemetry | Where-Object { -not $_.passed }).Count
$zeroQuanta = ($telemetry |
    Measure-Object -Property zeroFilledQuanta -Sum).Sum
$duplicateRuns = ($telemetry |
    Measure-Object -Property duplicateFrameRuns -Sum).Sum
$clippedSamples = ($telemetry |
    Measure-Object -Property clippedSamples -Sum).Sum
$excessDiscontinuities = ($telemetry | ForEach-Object {
    [Math]::Max(0, [int]$_.positionDiscontinuities - 1)
} | Measure-Object -Sum).Sum
$endpointHealthy = (
    $null -ne $root -and $root.Status -eq 'OK' -and
    $endpoints.Count -eq 2 -and
    @($endpoints | Where-Object { $_.Status -ne 'OK' }).Count -eq 0 -and
    $null -ne $service -and $service.State -eq 'Running')
$trueStop = (
    $storageErrors.Count -ne 0 -or
    $ciErrors.Count -ne 0 -or
    $bugchecks.Count -ne 0 -or
    $volume.HealthStatus -ne 'Healthy' -or
    $volume.OperationalStatus -notcontains 'OK')
$audioFailure = (
    $failedFixtures -ne 0 -or
    $zeroQuanta -ne 0 -or
    $duplicateRuns -ne 0 -or
    $clippedSamples -ne 0 -or
    $excessDiscontinuities -ne 0 -or
    -not $endpointHealthy)
$latest = if ($telemetry.Count) {
    $telemetry[-1]
} else {
    $null
}
$snapshot = [ordered]@{
    sampledAt = (Get-Date).ToString('o')
    processRunning = ($null -ne $process)
    process = $process |
        Select-Object Id, CPU, WorkingSet64, PrivateMemorySize64, HandleCount
    fixtureCount = $telemetry.Count
    latestFixture = $latest
    cumulative = [ordered]@{
        failedFixtures = $failedFixtures
        zeroFilledQuanta = [uint64]$zeroQuanta
        duplicateFrameRuns = [uint64]$duplicateRuns
        clippedSamples = [uint64]$clippedSamples
        excessPositionDiscontinuities = [uint64]$excessDiscontinuities
    }
    endpointHealthy = $endpointHealthy
    rootStatus = [string]$root.Status
    endpointCount = $endpoints.Count
    serviceState = [string]$service.State
    volumeHealth = [string]$volume.HealthStatus
    volumeOperationalStatus = @($volume.OperationalStatus)
    storageOrHardwareErrors = $storageErrors.Count
    candidateCodeIntegrityErrors = $ciErrors.Count
    bugcheckOrLiveKernelEvents = $bugchecks.Count
    audioFailure = $audioFailure
    trueStopCondition = $trueStop
}
($snapshot | ConvertTo-Json -Depth 10 -Compress) |
    Add-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-e-health-samples.jsonl') `
        -Encoding utf8
$snapshot | ConvertTo-Json -Depth 10
if ($trueStop) { exit 99 }
if ($audioFailure) { exit 10 }
exit 0
