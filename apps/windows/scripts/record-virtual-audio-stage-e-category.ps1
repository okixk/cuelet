[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$')]
    [string]$Category,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$cursorPath = Join-Path $EvidenceRoot 'stage-e-category-cursor.json'
$baselinePath = Join-Path $EvidenceRoot 'stage-e-baseline.json'

function Get-RecordId {
    param([string]$LogName)
    return (Get-WinEvent -LogName $LogName -MaxEvents 1).RecordId
}

function Get-EventsAfter {
    param([string]$LogName, [long]$RecordId)
    return @(Get-WinEvent -LogName $LogName `
        -FilterXPath "*[System[EventRecordID > $RecordId]]" `
        -ErrorAction SilentlyContinue)
}

if (Test-Path -LiteralPath $cursorPath -PathType Leaf) {
    $cursor = Get-Content -LiteralPath $cursorPath -Raw |
        ConvertFrom-Json
} else {
    $cursor = Get-Content -LiteralPath $baselinePath -Raw |
        ConvertFrom-Json
}

$newSystem = Get-EventsAfter -LogName System `
    -RecordId $cursor.systemRecordId
$newApplication = Get-EventsAfter -LogName Application `
    -RecordId $cursor.applicationRecordId
$newCi = Get-EventsAfter `
    -LogName 'Microsoft-Windows-CodeIntegrity/Operational' `
    -RecordId $cursor.codeIntegrityRecordId

$storageErrors = @($newSystem | Where-Object {
    $_.ProviderName -match (
        '(?i)^(disk|stornvme|storport|Microsoft-Windows-WHEA-Logger|Ntfs)$') -and
    ($_.Level -le 3 -or $_.Id -eq 55)
})
$candidateCiErrors = @($newCi | Where-Object {
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
$audioErrors = @($newSystem | Where-Object {
    $_.Level -le 3 -and
    $_.ProviderName -match (
        '(?i)Audio|Kernel-PnP|UserPnp|DriverFrameworks') -and
    $_.Message -match '(?i)Cuelet|CUELETVIRTUALAUDIO|cuelet_virtual_audio'
})

$root = Get-PnpDevice -InstanceId 'ROOT\CUELETVIRTUALAUDIO\0000' `
    -ErrorAction SilentlyContinue
$endpoints = @(Get-PnpDevice -Class AudioEndpoint `
    -ErrorAction SilentlyContinue | Where-Object {
        $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
    })
$service = Get-CimInstance Win32_SystemDriver -Filter (
    "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
$installedHash = if ($null -ne $service -and
    (Test-Path -LiteralPath $service.PathName -PathType Leaf)) {
    (Get-FileHash -LiteralPath $service.PathName -Algorithm SHA256).Hash
} else {
    ''
}
$dirtyText = (& fsutil.exe dirty query C: 2>&1 | Out-String).Trim()
$dirtyKnown = $dirtyText -match '(?i)\bis (NOT )?Dirty\b'
$dirtyConfirmed = (
    $dirtyKnown -and
    $dirtyText -match '(?i)\bis Dirty\b' -and
    $dirtyText -notmatch '(?i)\bis NOT Dirty\b')
$volume = Get-Volume -DriveLetter C
$endpointHealthy = (
    $null -ne $root -and $root.Status -eq 'OK' -and
    $endpoints.Count -eq 2 -and
    @($endpoints | Where-Object { $_.Status -ne 'OK' }).Count -eq 0 -and
    $null -ne $service -and $service.State -eq 'Running' -and
    $installedHash -eq $ExpectedSysSha256)
$trueStop = (
    $storageErrors.Count -ne 0 -or
    $candidateCiErrors.Count -ne 0 -or
    $bugchecks.Count -ne 0 -or
    $dirtyConfirmed -or
    $volume.HealthStatus -ne 'Healthy' -or
    $volume.OperationalStatus -notcontains 'OK')

$nextCursor = [ordered]@{
    sampledAt = (Get-Date).ToString('o')
    systemRecordId = Get-RecordId -LogName System
    applicationRecordId = Get-RecordId -LogName Application
    codeIntegrityRecordId = Get-RecordId `
        -LogName 'Microsoft-Windows-CodeIntegrity/Operational'
}
$result = [ordered]@{
    category = $Category
    sampledAt = $nextCursor.sampledAt
    passed = ($endpointHealthy -and -not $trueStop)
    trueStopCondition = $trueStop
    endpointHealthy = $endpointHealthy
    rootStatus = [string]$root.Status
    endpoints = $endpoints |
        Select-Object FriendlyName, Status, InstanceId
    service = $service |
        Select-Object Name, State, StartMode, PathName
    installedSysSha256 = $installedHash
    expectedSysSha256 = $ExpectedSysSha256
    filesystemDirtyQuery = $dirtyText
    filesystemDirtyStateKnown = $dirtyKnown
    filesystemDirtyConfirmed = $dirtyConfirmed
    volumeHealth = [string]$volume.HealthStatus
    volumeOperationalStatus = @($volume.OperationalStatus)
    newEventCounts = [ordered]@{
        system = $newSystem.Count
        application = $newApplication.Count
        codeIntegrity = $newCi.Count
        storageOrHardwareErrors = $storageErrors.Count
        candidateCodeIntegrityErrors = $candidateCiErrors.Count
        bugcheckOrLiveKernelEvents = $bugchecks.Count
        cueletAudioErrors = $audioErrors.Count
    }
    eventCursorBefore = $cursor
    eventCursorAfter = $nextCursor
}

$resultPath = Join-Path $EvidenceRoot (
    'category-' + $Category + '-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
$result | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath $resultPath -Encoding utf8
$nextCursor | ConvertTo-Json |
    Set-Content -LiteralPath $cursorPath -Encoding utf8
$result | ConvertTo-Json -Depth 10
if ($trueStop) { exit 99 }
if (-not $endpointHealthy) { exit 10 }
exit 0
