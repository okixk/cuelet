[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$PdbPath
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$PdbPath = [IO.Path]::GetFullPath($PdbPath)
$sessionName = 'CueletStageE'
$kitsBin = 'C:\Program Files (x86)\Windows Kits\10\bin\' +
    '10.0.26100.0\x64'
$tracelog = Join-Path $kitsBin 'tracelog.exe'
$tracepdb = Join-Path $kitsBin 'tracepdb.exe'
$tracefmt = Join-Path $kitsBin 'tracefmt.exe'
$etlPath = Join-Path $EvidenceRoot 'stage-e-lifecycle.etl'
$tmfPath = Join-Path $EvidenceRoot 'stage-e-tmf'
$tracePath = Join-Path $EvidenceRoot 'stage-e-lifecycle-trace.txt'
$checkpointPath = Join-Path $EvidenceRoot `
    'stage-e-lifecycle-checkpoints.txt'

foreach ($path in @($tracelog, $tracepdb, $tracefmt, $PdbPath, $etlPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required WPP input or tool not found: $path"
    }
}

$stopText = (& $tracelog -stop $sessionName 2>&1 | Out-String).Trim()
$stopExit = $LASTEXITCODE
$stopText | Set-Content -LiteralPath (
    Join-Path $EvidenceRoot 'stage-e-tracelog-stop.txt') -Encoding utf8
if ($stopExit -ne 0 -and
    $stopText -notmatch (
        '(?i)not found|does not exist|cannot find|4201|' +
        'instance name passed was not recognized')) {
    throw "Could not stop the Stage E WPP session: $stopText"
}

New-Item -ItemType Directory -Path $tmfPath -Force | Out-Null
$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $tracepdb -f $PdbPath -p $tmfPath -v 2>&1 |
    Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-e-tracepdb.log') -Encoding utf8
$tracePdbExit = $LASTEXITCODE
& $tracefmt $etlPath -p $tmfPath -o $tracePath -nosummary `
    -hires -sortableTime -timeZoneSuffix -cp utf8 2>&1 |
    Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-e-tracefmt.log') -Encoding utf8
$traceFmtExit = $LASTEXITCODE
$ErrorActionPreference = $savedErrorActionPreference
if ($tracePdbExit -ne 0 -or $traceFmtExit -ne 0) {
    throw "WPP decode failed: tracepdb=$tracePdbExit tracefmt=$traceFmtExit"
}

$lines = @(Select-String -LiteralPath $tracePath `
    -Pattern 'checkpoint=CVA\d{3}' | ForEach-Object Line)
$lines | Set-Content -LiteralPath $checkpointPath -Encoding utf8
$records = @($lines | ForEach-Object {
    if ($_ -match 'checkpoint=(CVA\d{3}) (.*?) status=([^ ]+)') {
        [pscustomobject]@{
            Checkpoint = $Matches[1]
            Operation = $Matches[2]
            Status = $Matches[3]
        }
    }
})
$counts = @($records |
    Group-Object Checkpoint, Operation, Status |
    Sort-Object Name |
    ForEach-Object {
        [ordered]@{
            checkpoint = $_.Group[0].Checkpoint
            operation = $_.Group[0].Operation
            status = $_.Group[0].Status
            count = $_.Count
        }
    })
$counts | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-e-checkpoint-counts.json') `
        -Encoding utf8

function Count-Checkpoint {
    param([string]$Checkpoint)
    return @($records | Where-Object {
        $_.Checkpoint -eq $Checkpoint
    }).Count
}

$functionalFailures = @($records | Where-Object {
    $_.Status -notmatch 'STATUS_SUCCESS|0x00000000' -and
    -not (
        $_.Checkpoint -in @('CVA321', 'CVA323') -and
        $_.Status -match 'STATUS_OBJECT_NAME_NOT_FOUND|0xc0000034'
    ) -and
    $_.Checkpoint -notin @(
        'CVA123', # Optional IPortClsEtwHelper.
        'CVA124', # Optional IPortClsRuntimePower.
        'CVA125', # Optional power-control probe.
        'CVA126', # Optional power-control callback.
        'CVA127', # Optional power-control unregister.
        'CVA200', # Entry logs the initial dispatch status sentinel.
        'CVA209'  # Unsupported PnP minors are valid PortCls behavior.
    )
})
$summary = [ordered]@{
    completedAt = (Get-Date).ToString('o')
    traceSession = $sessionName
    traceStopExitCode = $stopExit
    tracePdbExitCode = $tracePdbExit
    traceFmtExitCode = $traceFmtExit
    etlBytes = (Get-Item -LiteralPath $etlPath).Length
    traceBytes = (Get-Item -LiteralPath $tracePath).Length
    checkpointRecords = $records.Count
    driverEntries = Count-Checkpoint 'CVA000'
    successfulStarts = @($records | Where-Object {
        $_.Checkpoint -eq 'CVA199' -and
        $_.Status -match 'STATUS_SUCCESS|0x00000000'
    }).Count
    bridgeEpochArms = Count-Checkpoint 'CVA401'
    bridgeTeardowns = Count-Checkpoint 'CVA409'
    streamInitializations = Count-Checkpoint 'CVA509'
    timerArms = Count-Checkpoint 'CVA531'
    streamStateExits = Count-Checkpoint 'CVA539'
    functionalFailureCount = $functionalFailures.Count
    functionalFailures = $functionalFailures
    passed = (
        $records.Count -gt 0 -and
        $functionalFailures.Count -eq 0)
}
$summary | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'stage-e-trace-summary.json') `
        -Encoding utf8
$summary | ConvertTo-Json -Depth 10
if (-not $summary.passed) { exit 10 }
exit 0
