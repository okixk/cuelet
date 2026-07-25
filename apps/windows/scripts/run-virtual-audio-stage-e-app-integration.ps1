[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CueletPath,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [Parameter(Mandatory = $true)]
    [string]$FixturePath,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256
)

$ErrorActionPreference = 'Stop'
$CueletPath = [IO.Path]::GetFullPath($CueletPath)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$FixturePath = [IO.Path]::GetFullPath($FixturePath)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$compareScript = Join-Path $PSScriptRoot `
    'compare-virtual-audio-capture.py'
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'
$integrationRoot = Join-Path $EvidenceRoot 'application-integration'
$settingsPath = 'HKCU:\Software\Cuelet'
$settingNames = @(
    'AllowMultiple',
    'KeepRunningInBackground',
    'BroadcastOutputId',
    'VirtualCaptureId',
    'MicrophoneInputId',
    'MixPhysicalMicrophone',
    'LibraryPath',
    'MonitorLocally'
)

foreach ($path in @(
    $CueletPath, $FlowTestPath, $InstallerPath, $FixturePath,
    $compareScript, $recorder)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required application-integration file not found: $path"
    }
}
New-Item -ItemType Directory -Path $integrationRoot -Force | Out-Null
New-Item -Path $settingsPath -Force | Out-Null

function Get-CueletProcesses {
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -in @('Cuelet', 'Cuelet.WinUI')
    })
}

function Wait-CueletWindow {
    param([int]$Seconds = 20)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $window = Get-CueletProcesses | Where-Object {
            $_.MainWindowHandle -ne 0
        } | Select-Object -First 1
        if ($null -ne $window) { return $window }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Wait-CueletExit {
    param([int]$Seconds = 15)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if ((Get-CueletProcesses).Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Stop-CueletNormally {
    $windows = @(Get-CueletProcesses | Where-Object {
        $_.MainWindowHandle -ne 0
    })
    foreach ($window in $windows) {
        [void]$window.CloseMainWindow()
    }
    if (-not (Wait-CueletExit)) {
        Get-CueletProcesses | Stop-Process -Force
        throw 'Cuelet did not exit normally after application integration.'
    }
}

function Start-Capture {
    param([string]$Name, [double]$Seconds)
    $output = Join-Path $integrationRoot $Name
    $process = Start-Process -FilePath $FlowTestPath `
        -ArgumentList @(
            '--capture-sample', '--seconds', ([string]$Seconds),
            '--output-dir', ('"' + $output + '"')) `
        -WindowStyle Hidden -PassThru
    return [pscustomobject]@{
        Name = $Name
        Output = $output
        Process = $process
    }
}

function Complete-Capture {
    param([object]$Capture, [int]$TimeoutSeconds = 30)
    if (-not $Capture.Process.WaitForExit($TimeoutSeconds * 1000)) {
        $Capture.Process.Kill()
        throw "Capture timed out: $($Capture.Name)"
    }
    $summaryPath = Join-Path $Capture.Output `
        'real-application-capture.json'
    if ($Capture.Process.ExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw "Capture failed: $($Capture.Name)"
    }
    return Get-Content -LiteralPath $summaryPath -Raw |
        ConvertFrom-Json
}

function Invoke-CueletCommand {
    param([string[]]$Arguments)
    $process = Start-Process -FilePath $CueletPath `
        -ArgumentList $Arguments -PassThru -Wait
    if ($process.ExitCode -ne 0) {
        throw ('Cuelet command failed with exit code ' +
            $process.ExitCode + ': ' + ($Arguments -join ' '))
    }
    return $process.ExitCode
}

function Get-NormalizedEndpointId {
    param([string]$Value)
    if ($Value -match (
        '(?i)(\{0\.0\.[01]\.00000000\}\.' +
        '\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-' +
        '[0-9a-f]{4}-[0-9a-f]{12}\})')) {
        return $Matches[1].ToUpperInvariant()
    }
    return $Value.Trim().ToUpperInvariant()
}

function Start-CueletAndWait {
    Start-Process -FilePath $CueletPath | Out-Null
    $window = Wait-CueletWindow
    if ($null -eq $window) {
        throw 'Cuelet did not create a window.'
    }
    Start-Sleep -Seconds 6
    return $window
}

$settingsKey = Get-Item -LiteralPath $settingsPath
$settingsSnapshot = [ordered]@{}
foreach ($name in $settingNames) {
    $present = $settingsKey.GetValueNames() -contains $name
    $settingsSnapshot[$name] = [ordered]@{
        present = $present
        value = if ($present) { $settingsKey.GetValue($name) } else { $null }
        kind = if ($present) {
            [string]$settingsKey.GetValueKind($name)
        } else {
            ''
        }
    }
}
$settingsSnapshot | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (
        Join-Path $integrationRoot 'settings-before.json') -Encoding utf8

$statusText = (& $InstallerPath status --json | Out-String).Trim()
$status = $statusText | ConvertFrom-Json
if (-not $status.endpointPairValid) {
    throw 'Installer status did not report a valid Cuelet endpoint pair.'
}
$result = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    installerStatus = $status
    endpointAutoSelectionPassed = $false
    sourceCapturePassed = $false
    overlapPassed = $false
    physicalMicrophoneObserved = $false
    closeQuiescedOutput = $false
    passed = $false
}

try {
    if ((Get-CueletProcesses).Count -ne 0) {
        throw 'Close the existing Cuelet process before integration testing.'
    }
    Set-ItemProperty -LiteralPath $settingsPath `
        -Name KeepRunningInBackground -Type DWord -Value 0
    Set-ItemProperty -LiteralPath $settingsPath `
        -Name AllowMultiple -Type DWord -Value 1
    Set-ItemProperty -LiteralPath $settingsPath `
        -Name MixPhysicalMicrophone -Type DWord -Value 0
    Set-ItemProperty -LiteralPath $settingsPath `
        -Name MonitorLocally -Type DWord -Value 0
    $fixtureLibrary = Join-Path $integrationRoot 'fixture-library'
    New-Item -ItemType Directory -Path $fixtureLibrary -Force |
        Out-Null
    Set-ItemProperty -LiteralPath $settingsPath `
        -Name LibraryPath -Type String -Value $fixtureLibrary

    $window = Start-CueletAndWait
    $selectedRender = [string](Get-ItemPropertyValue `
        -LiteralPath $settingsPath -Name BroadcastOutputId)
    $selectedCapture = [string](Get-ItemPropertyValue `
        -LiteralPath $settingsPath -Name VirtualCaptureId)
    $result.endpointAutoSelection = [ordered]@{
        expectedRender = $status.renderEndpointId
        actualRender = $selectedRender
        expectedCapture = $status.captureEndpointId
        actualCapture = $selectedCapture
    }
    $result.endpointAutoSelectionPassed = (
        (Get-NormalizedEndpointId $selectedRender) -eq
            (Get-NormalizedEndpointId $status.renderEndpointId) -and
        (Get-NormalizedEndpointId $selectedCapture) -eq
            (Get-NormalizedEndpointId $status.captureEndpointId))
    if (-not $result.endpointAutoSelectionPassed) {
        throw 'Cuelet did not persist the installer-verified endpoint pair.'
    }

    $sourceCapture = Start-Capture -Name 'source-capture' -Seconds 12
    Start-Sleep -Seconds 1
    [void](Invoke-CueletCommand -Arguments @(
        '--play-file', ('"' + $FixturePath + '"')))
    $sourceSummary = Complete-Capture -Capture $sourceCapture
    $sourceWav = Join-Path $sourceCapture.Output `
        'real-application-capture.wav'
    $comparisonPath = Join-Path $integrationRoot `
        'source-capture-comparison.json'
    & python $compareScript --reference $FixturePath `
        --capture $sourceWav --output $comparisonPath `
        --media-player --expected-preroll-ms 1000 *>&1 |
        Set-Content -LiteralPath (
            Join-Path $integrationRoot 'source-comparison.log') `
            -Encoding utf8
    $comparisonExit = $LASTEXITCODE
    $comparison = Get-Content -LiteralPath $comparisonPath -Raw |
        ConvertFrom-Json
    $result.sourceCapture = [ordered]@{
        capture = $sourceSummary
        comparisonExitCode = $comparisonExit
        comparison = $comparison
    }
    $result.sourceCapturePassed = (
        $sourceSummary.passed -and $comparisonExit -eq 0 -and
        $comparison.passed)
    if (-not $result.sourceCapturePassed) {
        throw 'The real Cuelet source-to-virtual-microphone sample failed.'
    }

    $overlapCapture = Start-Capture -Name 'overlap-capture' -Seconds 8
    Start-Sleep -Seconds 1
    [void](Invoke-CueletCommand -Arguments @(
        '--play-file', ('"' + $FixturePath + '"')))
    Start-Sleep -Milliseconds 250
    [void](Invoke-CueletCommand -Arguments @(
        '--play-file', ('"' + $FixturePath + '"')))
    Start-Sleep -Seconds 3
    [void](Invoke-CueletCommand -Arguments @('--stop-all'))
    $overlapSummary = Complete-Capture -Capture $overlapCapture
    $result.overlapCapture = $overlapSummary
    $result.overlapPassed = (
        $overlapSummary.passed -and
        $overlapSummary.peak -gt 0.01 -and
        $overlapSummary.peak -lt 0.999969)
    if (-not $result.overlapPassed) {
        throw 'Overlapping playback did not follow the active mixing path.'
    }
    Stop-CueletNormally

    Set-ItemProperty -LiteralPath $settingsPath `
        -Name MixPhysicalMicrophone -Type DWord -Value 1
    $window = Start-CueletAndWait
    $microphoneCapture = Start-Capture `
        -Name 'physical-microphone-idle' -Seconds 5
    $microphoneSummary = Complete-Capture -Capture $microphoneCapture
    $result.physicalMicrophoneCapture = $microphoneSummary
    $result.physicalMicrophoneObserved = (
        $microphoneSummary.passed -and
        $microphoneSummary.peak -gt 0.00001)
    Stop-CueletNormally

    # The immutable candidate's FIFO is bounded to about 1.36 seconds at
    # 48 kHz stereo PCM16. Allow it to drain before asserting silence.
    Start-Sleep -Seconds 2
    $quiescedCapture = Start-Capture `
        -Name 'after-cuelet-close' -Seconds 3
    $quiescedSummary = Complete-Capture -Capture $quiescedCapture
    $result.afterCloseCapture = $quiescedSummary
    $result.closeQuiescedOutput = (
        $quiescedSummary.passed -and
        $quiescedSummary.peak -le 0.00001)
    if (-not $result.closeQuiescedOutput) {
        throw 'Closing Cuelet left non-silent virtual microphone output.'
    }

    $result.passed = (
        $result.endpointAutoSelectionPassed -and
        $result.sourceCapturePassed -and
        $result.overlapPassed -and
        $result.physicalMicrophoneObserved -and
        $result.closeQuiescedOutput)
    if (-not $result.passed) {
        throw 'One or more real-application checks failed.'
    }
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    Get-CueletProcesses | Stop-Process -Force `
        -ErrorAction SilentlyContinue
    $key = Get-Item -LiteralPath $settingsPath
    foreach ($name in $settingNames) {
        $saved = $settingsSnapshot[$name]
        if (-not $saved.present) {
            Remove-ItemProperty -LiteralPath $settingsPath `
                -Name $name -ErrorAction SilentlyContinue
            continue
        }
        $kind = switch ($saved.kind) {
            'DWord' { 'DWord' }
            'QWord' { 'QWord' }
            'ExpandString' { 'ExpandString' }
            'MultiString' { 'MultiString' }
            'Binary' { 'Binary' }
            default { 'String' }
        }
        Set-ItemProperty -LiteralPath $settingsPath `
            -Name $name -Type $kind -Value $saved.value
    }
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath (
            Join-Path $integrationRoot `
                'application-integration-result.json') -Encoding utf8
}

& $recorder -EvidenceRoot $EvidenceRoot `
    -Category 'application-integration' `
    -ExpectedSysSha256 $ExpectedSysSha256 |
    Set-Content -LiteralPath (
        Join-Path $integrationRoot 'category-health.json') -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw 'Health audit failed after application integration.'
}
$result | ConvertTo-Json -Depth 12
exit 0
