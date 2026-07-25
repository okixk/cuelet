[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CueletPath,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$FixturePath,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [switch]$ForcedOnly
)

$ErrorActionPreference = 'Stop'
$CueletPath = [IO.Path]::GetFullPath($CueletPath)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$FixturePath = [IO.Path]::GetFullPath($FixturePath)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'
$processRoot = Join-Path $EvidenceRoot 'process-stress'
$settingsPath = 'HKCU:\Software\Cuelet'

foreach ($path in @(
    $CueletPath, $FlowTestPath, $FixturePath, $recorder)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}
New-Item -ItemType Directory -Path $processRoot -Force | Out-Null

function Get-CueletProcesses {
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -in @('Cuelet', 'Cuelet.WinUI')
    })
}

function Wait-CueletWindow {
    param([int]$Seconds = 20)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $processes = @(Get-CueletProcesses)
        $window = $processes | Where-Object {
            $_.MainWindowHandle -ne 0
        } | Select-Object -First 1
        if ($null -ne $window) { return $window }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Wait-CueletExit {
    param([int]$Seconds = 20)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if ((Get-CueletProcesses).Count -eq 0) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Start-CueletWindow {
    Start-Process -FilePath $CueletPath | Out-Null
    $window = Wait-CueletWindow
    if ($null -eq $window) {
        throw 'Cuelet did not create its main window.'
    }
    return $window
}

function Invoke-FlowProbe {
    param([string]$Name)
    $output = Join-Path $processRoot $Name
    & $FlowTestPath --bounded-tone --frequency 997 `
        --seconds 0.2 --output-dir $output *>&1 |
        Set-Content -LiteralPath ($output + '.log') -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "The post-process audio probe failed for $Name."
    }
}

function Record-Category {
    param([string]$Name)
    & $recorder -EvidenceRoot $EvidenceRoot -Category $Name `
        -ExpectedSysSha256 $ExpectedSysSha256 |
        Set-Content -LiteralPath (
            Join-Path $processRoot ($Name + '-health.json')) `
            -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Stage E health failed after $Name."
    }
}

if ((Get-CueletProcesses).Count -ne 0) {
    throw 'Close the existing Cuelet user-mode process before process stress.'
}
$settingsExisted = Test-Path -LiteralPath $settingsPath
$oldSetting = if ($settingsExisted) {
    Get-ItemPropertyValue -LiteralPath $settingsPath `
        -Name KeepRunningInBackground -ErrorAction SilentlyContinue
} else {
    $null
}
$settingsSnapshot = if ($settingsExisted) {
    Get-ItemProperty -LiteralPath $settingsPath
} else {
    $null
}
$librarySettingExisted =
    $null -ne $settingsSnapshot -and
    $null -ne $settingsSnapshot.PSObject.Properties['LibraryPath']
$oldLibraryPath = if ($librarySettingExisted) {
    [string]$settingsSnapshot.LibraryPath
} else {
    $null
}
$fixtureLibrary = Join-Path $processRoot 'fixture-library'
New-Item -ItemType Directory -Path $fixtureLibrary -Force | Out-Null
New-Item -Path $settingsPath -Force | Out-Null
Set-ItemProperty -LiteralPath $settingsPath `
    -Name KeepRunningInBackground -Type DWord -Value 0
Set-ItemProperty -LiteralPath $settingsPath `
    -Name LibraryPath -Type String -Value $fixtureLibrary

$result = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    normalCyclesRequested = if ($ForcedOnly) { 0 } else { 25 }
    normalCyclesCompleted = 0
    forcedCyclesRequested = 10
    forcedCyclesCompleted = 0
    passed = $false
}

try {
    if (-not $ForcedOnly) {
        $normalLog = @()
        for ($iteration = 1; $iteration -le 25; ++$iteration) {
            $window = Start-CueletWindow
            Start-Sleep -Milliseconds 500
            $closeRequested = $window.CloseMainWindow()
            $exited = Wait-CueletExit
            $normalLog += [ordered]@{
                iteration = $iteration
                processId = $window.Id
                windowTitle = $window.MainWindowTitle
                closeRequested = $closeRequested
                exitedNormally = $exited
            }
            if (-not $closeRequested -or -not $exited) {
                throw "Normal Cuelet process cycle $iteration failed."
            }
            ++$result.normalCyclesCompleted
        }
        $normalLog | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (
                Join-Path $processRoot 'normal-process-cycles.json') `
                -Encoding utf8
        Invoke-FlowProbe -Name 'after-normal-process-cycles'
        Record-Category -Name 'cuelet-normal-exit-25'
    }

    $forcedLog = @()
    for ($iteration = 1; $iteration -le 10; ++$iteration) {
        $window = Start-CueletWindow
        # Allow driver-status refresh to select the current immutable
        # candidate's render/capture endpoint IDs.
        Start-Sleep -Seconds 2
        $captureRoot = Join-Path $processRoot (
            'forced-active-capture-' + $iteration)
        $captureProcess = Start-Process -FilePath $FlowTestPath `
            -ArgumentList @(
                '--capture-sample', '--seconds', '6',
                '--output-dir', ('"' + $captureRoot + '"')) `
            -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 300
        $play = Start-Process -FilePath $CueletPath `
            -ArgumentList @('--play-file', ('"' + $FixturePath + '"')) `
            -PassThru -Wait
        # External-file playback is initialized asynchronously by the
        # already-running WinUI process. Keep the bounded capture open long
        # enough to prove that its AudioGraph stream is active before the
        # forced termination.
        Start-Sleep -Milliseconds 2500
        $running = @(Get-CueletProcesses)
        $running | Stop-Process -Force
        $exited = Wait-CueletExit
        if (-not $captureProcess.WaitForExit(10000)) {
            $captureProcess.Kill()
            throw "Capture evidence timed out in forced cycle $iteration."
        }
        $captureExit = $captureProcess.ExitCode
        $captureSummaryPath = Join-Path $captureRoot `
            'real-application-capture.json'
        $captureSummary = if (Test-Path -LiteralPath `
            $captureSummaryPath -PathType Leaf) {
            Get-Content -LiteralPath $captureSummaryPath -Raw |
                ConvertFrom-Json
        } else {
            $null
        }
        $cycleResult = [ordered]@{
            iteration = $iteration
            originalProcessId = $window.Id
            playCommandExitCode = $play.ExitCode
            terminatedProcessIds = @($running.Id)
            processesExited = $exited
            captureExitCode = $captureExit
            capturePath = $captureRoot
            captureSummary = $captureSummary
        }
        $failures = @()
        if ($play.ExitCode -ne 0) {
            $failures += "play exit $($play.ExitCode)"
        }
        if (-not $exited) { $failures += 'Cuelet processes remained' }
        if ($captureExit -ne 0) {
            $failures += "capture exit $captureExit"
        }
        if ($null -eq $captureSummary) {
            $failures += 'capture summary missing'
        } else {
            if ($captureSummary.peak -lt 0.001) {
                $failures += "capture peak $($captureSummary.peak)"
            }
            if ($captureSummary.rms -lt 0.0001) {
                $failures += "capture RMS $($captureSummary.rms)"
            }
        }
        $cycleResult.failures = $failures
        $forcedLog += $cycleResult
        $forcedLog | ConvertTo-Json -Depth 7 |
            Set-Content -LiteralPath (
                Join-Path $processRoot 'forced-process-cycles.json') `
                -Encoding utf8
        if ($failures.Count -ne 0) {
            throw "Forced active-audio cycle $iteration failed: " +
                ($failures -join '; ')
        }
        Invoke-FlowProbe -Name ('after-forced-cycle-' + $iteration)
        ++$result.forcedCyclesCompleted
    }
    Record-Category -Name 'cuelet-forced-active-10'
    $result.passed = $true
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    @(Get-CueletProcesses) | Stop-Process -Force `
        -ErrorAction SilentlyContinue
    if ($null -eq $oldSetting) {
        Remove-ItemProperty -LiteralPath $settingsPath `
            -Name KeepRunningInBackground -ErrorAction SilentlyContinue
    } else {
        Set-ItemProperty -LiteralPath $settingsPath `
            -Name KeepRunningInBackground -Type DWord -Value $oldSetting
    }
    if ($librarySettingExisted) {
        Set-ItemProperty -LiteralPath $settingsPath `
            -Name LibraryPath -Type String -Value $oldLibraryPath
    } else {
        Remove-ItemProperty -LiteralPath $settingsPath `
            -Name LibraryPath -ErrorAction SilentlyContinue
    }
    if (-not $settingsExisted) {
        $key = Get-Item -LiteralPath $settingsPath
        if ($key.Property.Count -eq 0) {
            Remove-Item -LiteralPath $settingsPath
        }
    }
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (
            Join-Path $processRoot 'process-stress-result.json') `
            -Encoding utf8
}

$result | ConvertTo-Json -Depth 8
