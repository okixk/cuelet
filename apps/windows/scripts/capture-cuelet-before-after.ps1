param(
    [Parameter(Mandatory = $true)]
    [string]$Source
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [switch]$AllowNonZeroExit
    )

    # Native programs such as FFmpeg legitimately write information to stderr.
    # Temporarily prevent PowerShell from turning that into a terminating error.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    try {
        $output = & $Executable @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    $text = ($output | ForEach-Object { $_.ToString() }) -join "`r`n"

    if (-not $AllowNonZeroExit -and $exitCode -ne 0) {
        throw @"
Native command failed with exit code $exitCode.

Executable:
$Executable

Arguments:
$($Arguments -join ' ')

Output:
$text
"@
    }

    [pscustomobject]@{
        ExitCode = $exitCode
        Text     = $text
    }
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path

$ffmpegCommand = Get-Command ffmpeg -ErrorAction SilentlyContinue
$ffprobeCommand = Get-Command ffprobe -ErrorAction SilentlyContinue

if (-not $ffmpegCommand -or -not $ffprobeCommand) {
    throw @"
FFmpeg or FFprobe was not found.

Check with:
  Get-Command ffmpeg, ffprobe
"@
}

$ffmpeg = $ffmpegCommand.Source
$ffprobe = $ffprobeCommand.Source

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDirectory = Join-Path `
    ([Environment]::GetFolderPath("Desktop")) `
    "Cuelet-Audio-Comparison-$timestamp"

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$referencePath = Join-Path $outputDirectory "01-reference-before-driver.wav"
$capturePath = Join-Path $outputDirectory "02-captured-after-driver.wav"
$deviceLog = Join-Path $outputDirectory "ffmpeg-devices.txt"
$captureLog = Join-Path $outputDirectory "ffmpeg-capture.log"

Write-Host "`n=== Creating canonical reference ===" -ForegroundColor Cyan

$referenceResult = Invoke-Native `
    -Executable $ffmpeg `
    -Arguments @(
        "-hide_banner",
        "-loglevel", "error",
        "-y",
        "-i", $sourcePath,
        "-vn",
        "-t", "30",
        "-ar", "48000",
        "-ac", "2",
        "-c:a", "pcm_s16le",
        $referencePath
    )

$probeResult = Invoke-Native `
    -Executable $ffprobe `
    -Arguments @(
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        $referencePath
    )

$durationText = $probeResult.Text.Trim()

$referenceDuration = [double]::Parse(
    $durationText,
    [Globalization.CultureInfo]::InvariantCulture
)

$captureDuration = [Math]::Ceiling($referenceDuration + 8)

Write-Host "`n=== Finding Cuelet capture endpoint ===" -ForegroundColor Cyan

# FFmpeg intentionally exits unsuccessfully because "dummy" is not a real input.
# Its device list is still valid and is written mostly to stderr.
$deviceResult = Invoke-Native `
    -Executable $ffmpeg `
    -Arguments @(
        "-hide_banner",
        "-list_devices", "true",
        "-f", "dshow",
        "-i", "dummy"
    ) `
    -AllowNonZeroExit

$deviceText = $deviceResult.Text
$deviceText | Set-Content -LiteralPath $deviceLog -Encoding UTF8

$devices = @(
    [regex]::Matches(
        $deviceText,
        '"(?<name>[^"]*Cuelet[^"]*)"\s+\(audio\)',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
    ) |
    ForEach-Object {
        $_.Groups["name"].Value
    } |
    Sort-Object -Unique
)

$preferredDevices = @(
    $devices |
    Where-Object {
        $_ -match "(?i)Virtual Microphone" -and
        $_ -notmatch "(?i)Input"
    }
)

if ($preferredDevices.Count -eq 1) {
    $captureDevice = $preferredDevices[0]
}
elseif ($devices.Count -eq 1) {
    $captureDevice = $devices[0]
}
elseif ($devices.Count -eq 0) {
    throw @"
No Cuelet capture device was found.

The complete FFmpeg device list is here:
  $deviceLog
"@
}
else {
    throw @"
Multiple Cuelet capture devices were found:

$($devices -join "`n")

The complete FFmpeg device list is here:
  $deviceLog
"@
}

Write-Host "Capture device: $captureDevice" -ForegroundColor Green
Write-Host "Reference duration: $($referenceDuration.ToString('0.00')) seconds"

@"
Source:
$sourcePath

Required configuration:
Voice-chat output: Cuelet Virtual Microphone Input
Voice-chat microphone endpoint: Cuelet Virtual Microphone
Mix my microphone: Off
Play through speakers/headphones: Off
Windows output: Speakers (Realtek(R) Audio)
"@ | Set-Content `
    -LiteralPath (Join-Path $outputDirectory "test-configuration.txt") `
    -Encoding UTF8

Write-Host "`nConfigure Cuelet like this:" -ForegroundColor Yellow
Write-Host "  Voice-chat output: Cuelet Virtual Microphone Input"
Write-Host "  Mix my microphone: Off"
Write-Host "  Play through speakers/headphones: Off"
Write-Host ""
Write-Host "Prepare the exact same file in Cuelet, but do not play it yet."

Read-Host "`nPress Enter when ready"

$job = Start-Job `
    -ScriptBlock {
        param(
            $Ffmpeg,
            $Device,
            $Seconds,
            $Destination
        )

        $ErrorActionPreference = "Continue"

        $output = & $Ffmpeg `
            -hide_banner `
            -nostdin `
            -loglevel warning `
            -y `
            -f dshow `
            -audio_buffer_size 50 `
            -thread_queue_size 1024 `
            -rtbufsize 512M `
            -i "audio=$Device" `
            -t $Seconds `
            -ar 48000 `
            -ac 2 `
            -c:a pcm_s16le `
            $Destination 2>&1

        [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output   = (($output | ForEach-Object {
                $_.ToString()
            }) -join "`r`n")
        }
    } `
    -ArgumentList @(
        $ffmpeg,
        $captureDevice,
        $captureDuration,
        $capturePath
    )

Start-Sleep -Milliseconds 1500

Write-Host "`nSwitch to Cuelet now." -ForegroundColor Yellow
Write-Host "Play the sound exactly ONCE after the final high beep."

foreach ($number in 3..1) {
    Write-Host "$number..."
    [Console]::Beep(650, 120)
    Start-Sleep -Milliseconds 880
}

[Console]::Beep(1200, 250)
Write-Host "PLAY NOW" -ForegroundColor Green

Wait-Job -Job $job | Out-Null
$result = Receive-Job -Job $job
Remove-Job -Job $job -Force

$result.Output |
    Set-Content -LiteralPath $captureLog -Encoding UTF8

if ($result.ExitCode -ne 0) {
    throw @"
FFmpeg capture failed with exit code $($result.ExitCode).

Log:
$captureLog
"@
}

if (-not (Test-Path -LiteralPath $capturePath)) {
    throw "The captured WAV was not created."
}

Invoke-Native `
    -Executable $ffprobe `
    -Arguments @(
        "-v", "error",
        "-show_entries",
        "stream=codec_name,sample_rate,channels,bits_per_sample:format=duration",
        "-of", "json",
        $referencePath
    ) |
    Select-Object -ExpandProperty Text |
    Set-Content `
        -LiteralPath (Join-Path $outputDirectory "reference-info.json") `
        -Encoding UTF8

Invoke-Native `
    -Executable $ffprobe `
    -Arguments @(
        "-v", "error",
        "-show_entries",
        "stream=codec_name,sample_rate,channels,bits_per_sample:format=duration",
        "-of", "json",
        $capturePath
    ) |
    Select-Object -ExpandProperty Text |
    Set-Content `
        -LiteralPath (Join-Path $outputDirectory "capture-info.json") `
        -Encoding UTF8

Get-FileHash `
    -Algorithm SHA256 `
    -LiteralPath $referencePath, $capturePath |
    Format-List |
    Out-String |
    Set-Content `
        -LiteralPath (Join-Path $outputDirectory "hashes.txt") `
        -Encoding UTF8

$zipPath = "$outputDirectory.zip"

Compress-Archive `
    -Path (Join-Path $outputDirectory "*") `
    -DestinationPath $zipPath `
    -Force

Write-Host "`n=== Capture completed ===" -ForegroundColor Green
Write-Host "Folder: $outputDirectory"
Write-Host "ZIP:    $zipPath"
Write-Host ""
Write-Host "Upload the ZIP here for waveform comparison."

Invoke-Item $outputDirectory
