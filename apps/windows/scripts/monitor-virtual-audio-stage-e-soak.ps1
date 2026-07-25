[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [int]$SoakProcessId
)

$ErrorActionPreference = 'Stop'
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$healthScript = Join-Path $PSScriptRoot `
    'check-virtual-audio-stage-e-health.ps1'
$logPath = Join-Path $EvidenceRoot 'soak-monitor.log'
$resultPath = Join-Path $EvidenceRoot 'soak-monitor-result.json'
$checks = 0
$monitorResult = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    soakProcessId = $SoakProcessId
    healthChecks = 0
    stoppedForHealthFailure = $false
}

try {
    while ($null -ne (
        Get-Process -Id $SoakProcessId -ErrorAction SilentlyContinue)) {
        # Run each audit in a short-lived host so event-log and PnP provider
        # allocations cannot accumulate in this long-lived safety monitor.
        $healthText = (& powershell.exe -NoProfile -NonInteractive `
            -ExecutionPolicy Bypass -File $healthScript `
            -EvidenceRoot $EvidenceRoot -SoakProcessId $SoakProcessId |
            Out-String).Trim()
        $healthExit = $LASTEXITCODE
        ++$checks
        Add-Content -LiteralPath $logPath -Encoding utf8 -Value (
            (Get-Date).ToString('o') +
            " exit=$healthExit`r`n" + $healthText)
        if ($healthExit -in @(10, 99)) {
            Stop-Process -Id $SoakProcessId -Force `
                -ErrorAction SilentlyContinue
            $monitorResult.stoppedForHealthFailure = $true
            $monitorResult.healthExitCode = $healthExit
            break
        }
        Start-Sleep -Seconds 30
    }
} finally {
    $monitorResult.healthChecks = $checks
    $monitorResult.completedAt = (Get-Date).ToString('o')
    $monitorResult.soakProcessStillRunning = (
        $null -ne (
            Get-Process -Id $SoakProcessId -ErrorAction SilentlyContinue))
    $monitorResult | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $resultPath -Encoding utf8
}
