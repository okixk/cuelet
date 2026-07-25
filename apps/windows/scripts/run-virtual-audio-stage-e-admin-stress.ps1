[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BundleRoot,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$FlowTestPath,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSysSha256,
    [switch]$InstallCyclesOnly,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$BundleRoot = [IO.Path]::GetFullPath($BundleRoot)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$FlowTestPath = [IO.Path]::GetFullPath($FlowTestPath)
$ExpectedSysSha256 = $ExpectedSysSha256.ToUpperInvariant()
$helper = Join-Path $BundleRoot 'Cuelet.VirtualAudio.Installer.exe'
$package = Join-Path $BundleRoot 'DriverPackage'
$instanceId = 'ROOT\CUELETVIRTUALAUDIO\0000'
$recorder = Join-Path $PSScriptRoot `
    'record-virtual-audio-stage-e-category.ps1'
$poolmon = 'C:\Program Files (x86)\Windows Kits\10\Tools\' +
    '10.0.26100.0\x64\poolmon.exe'

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

if (-not $Elevated) {
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-Argument $PSCommandPath),
        '-BundleRoot', (Quote-Argument $BundleRoot),
        '-EvidenceRoot', (Quote-Argument $EvidenceRoot),
        '-FlowTestPath', (Quote-Argument $FlowTestPath),
        '-ExpectedSysSha256', $ExpectedSysSha256,
        '-Elevated'
    )
    if ($InstallCyclesOnly) {
        $arguments += '-InstallCyclesOnly'
    }
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs `
        -Wait -PassThru -ArgumentList $arguments
    exit $process.ExitCode
}
if (-not (Test-Administrator)) {
    throw 'Stage E administrative stress requires elevation.'
}
foreach ($path in @($helper, $FlowTestPath, $recorder)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file not found: $path"
    }
}
if (-not (Test-Path -LiteralPath $package -PathType Container)) {
    throw "Driver package not found: $package"
}

$adminRoot = Join-Path $EvidenceRoot 'admin-stress'
New-Item -ItemType Directory -Path $adminRoot -Force | Out-Null

function Get-CueletEndpoints {
    return @(Get-PnpDevice -Class AudioEndpoint `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.FriendlyName -match '(?i)Cuelet Virtual Audio Device'
        })
}

function Test-Healthy {
    $root = Get-PnpDevice -InstanceId $instanceId `
        -ErrorAction SilentlyContinue
    $endpoints = @(Get-CueletEndpoints)
    $service = Get-CimInstance Win32_SystemDriver -Filter (
        "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
    if ($null -eq $root -or $root.Status -ne 'OK' -or
        $endpoints.Count -ne 2 -or
        @($endpoints | Where-Object {
            $_.Status -ne 'OK'
        }).Count -ne 0 -or
        $null -eq $service -or $service.State -ne 'Running' -or
        -not (Test-Path -LiteralPath $service.PathName -PathType Leaf)) {
        return $false
    }
    return (Get-FileHash -LiteralPath $service.PathName `
        -Algorithm SHA256).Hash -eq $ExpectedSysSha256
}

function Wait-Healthy {
    param([int]$Seconds = 30)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if (Test-Healthy) { return $true }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Wait-Disabled {
    param([int]$Seconds = 20)
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $root = Get-PnpDevice -InstanceId $instanceId `
            -ErrorAction SilentlyContinue
        $activeEndpoints = @(Get-CueletEndpoints | Where-Object {
            $_.Status -eq 'OK'
        })
        $service = Get-CimInstance Win32_SystemDriver -Filter (
            "Name='cuelet_virtual_audio'") -ErrorAction SilentlyContinue
        if (($null -eq $root -or $root.Status -ne 'OK') -and
            $activeEndpoints.Count -eq 0 -and
            ($null -eq $service -or $service.State -ne 'Running')) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Invoke-FlowProbe {
    param([string]$Name)
    $output = Join-Path $adminRoot $Name
    $attempts = @()
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        $oldErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $text = (& $FlowTestPath --bounded-tone --frequency 997 `
                --seconds 0.2 --output-dir $output *>&1 |
                Out-String).Trim()
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $oldErrorAction
        }
        $attempts += [ordered]@{
            attempt = $attempt
            exitCode = $exitCode
            output = $text
        }
        if ($exitCode -eq 0) {
            break
        }
        $deviceInvalidated =
            $text -match '(?i)0x88890004|AUDCLNT_E_DEVICE_INVALIDATED'
        if (-not $deviceInvalidated -or $attempt -eq 3) {
            $attempts | ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath ($output + '.log') `
                    -Encoding utf8
            throw "The post-transition audio probe failed for $Name."
        }
        Start-Sleep -Seconds 2
    }
    $attempts | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath ($output + '.log') -Encoding utf8
    if ($attempts[-1].exitCode -ne 0) {
        throw "The post-transition audio probe failed for $Name."
    }
}

function Record-Category {
    param([string]$Name)
    & $recorder -EvidenceRoot $EvidenceRoot -Category $Name `
        -ExpectedSysSha256 $ExpectedSysSha256 |
        Set-Content -LiteralPath (
            Join-Path $adminRoot ($Name + '-health.json')) `
            -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Stage E health failed after $Name."
    }
}

function Invoke-PoolSnapshot {
    param([string]$Name)
    if (-not (Test-Path -LiteralPath $poolmon -PathType Leaf)) {
        return
    }
    $snapshot = Join-Path $adminRoot ($Name + '-poolmon.txt')
    & $poolmon -n $snapshot -p -b 2>&1 |
        Set-Content -LiteralPath (
            Join-Path $adminRoot ($Name + '-poolmon-command.log')) `
            -Encoding utf8
}

$result = [ordered]@{
    startedAt = (Get-Date).ToString('o')
    expectedSysSha256 = $ExpectedSysSha256
    devnodeCyclesRequested = if ($InstallCyclesOnly) { 0 } else { 20 }
    devnodeCyclesCompleted = 0
    audioServiceRestartsRequested = if ($InstallCyclesOnly) { 0 } else { 10 }
    audioServiceRestartsCompleted = 0
    installUninstallCyclesRequested = 5
    installUninstallCyclesCompleted = 0
    passed = $false
}

try {
    if (-not (Wait-Healthy)) {
        throw 'Cuelet was not healthy before administrative stress.'
    }
    Invoke-PoolSnapshot -Name 'before'

    if (-not $InstallCyclesOnly) {
        $devnodeLog = @()
        for ($iteration = 1; $iteration -le 20; ++$iteration) {
        $disabled = $false
        try {
            $disableText = (& pnputil.exe /disable-device `
                $instanceId 2>&1 | Out-String).Trim()
            $disableExit = $LASTEXITCODE
            $disabled = Wait-Disabled
        } finally {
            $enableText = (& pnputil.exe /enable-device `
                $instanceId 2>&1 | Out-String).Trim()
            $enableExit = $LASTEXITCODE
        }
        $healthy = Wait-Healthy
        $devnodeLog += [ordered]@{
            iteration = $iteration
            disableExitCode = $disableExit
            disabledObserved = $disabled
            enableExitCode = $enableExit
            healthyAfterEnable = $healthy
            disableOutput = $disableText
            enableOutput = $enableText
        }
        if ($disableExit -ne 0 -or -not $disabled -or
            $enableExit -ne 0 -or -not $healthy) {
            throw "Devnode cycle $iteration failed."
        }
            ++$result.devnodeCyclesCompleted
        }
        $devnodeLog | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath (
                Join-Path $adminRoot 'devnode-cycles.json') -Encoding utf8
        Invoke-FlowProbe -Name 'after-devnode-cycles'
        Invoke-PoolSnapshot -Name 'after-devnode-cycles'
        Record-Category -Name 'devnode-20'

        $serviceLog = @()
        for ($iteration = 1; $iteration -le 10; ++$iteration) {
        Restart-Service -Name Audiosrv -Force
        $serviceReady = (
            (Get-Service -Name Audiosrv).WaitForStatus(
                [ServiceProcess.ServiceControllerStatus]::Running,
                [TimeSpan]::FromSeconds(30)) -eq $null)
        $healthy = Wait-Healthy
        Invoke-FlowProbe -Name (
            'audio-service-restart-' + $iteration)
        $serviceLog += [ordered]@{
            iteration = $iteration
            audioServiceRunning = (
                (Get-Service -Name Audiosrv).Status -eq 'Running')
            waitCompleted = $serviceReady
            cueletHealthy = $healthy
        }
        if (-not $healthy) {
            throw "Audio service restart $iteration did not recover Cuelet."
        }
            ++$result.audioServiceRestartsCompleted
        }
        $serviceLog | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (
                Join-Path $adminRoot 'audio-service-restarts.json') `
                -Encoding utf8
        Invoke-PoolSnapshot -Name 'after-audiosrv-restarts'
        Record-Category -Name 'audiosrv-restart-10'
    }

    $installLog = @()
    for ($iteration = 1; $iteration -le 5; ++$iteration) {
        $uninstallText = (& $helper uninstall --json |
            Out-String).Trim()
        $uninstallExit = $LASTEXITCODE
        $uninstall = $uninstallText | ConvertFrom-Json
        if ($uninstallExit -ne 0 -or
            $uninstall.status.packageInstalled) {
            throw "Supported uninstall failed in cycle $iteration."
        }
        $installText = (& $helper install --json `
            --allow-test-package | Out-String).Trim()
        $installExit = $LASTEXITCODE
        $install = $installText | ConvertFrom-Json
        $healthy = Wait-Healthy
        $cycleResult = [ordered]@{
            iteration = $iteration
            uninstallExitCode = $uninstallExit
            uninstall = $uninstall
            installExitCode = $installExit
            install = $install
            healthyAfterInstall = $healthy
            probePassed = $false
        }
        $cycleResult | ConvertTo-Json -Depth 12 |
            Set-Content -LiteralPath (
                Join-Path $adminRoot (
                    'install-cycle-' + $iteration + '-installer.json')) `
                -Encoding utf8
        Invoke-FlowProbe -Name ('install-cycle-' + $iteration)
        $cycleResult.probePassed = $true
        $cycleResult | ConvertTo-Json -Depth 12 |
            Set-Content -LiteralPath (
                Join-Path $adminRoot (
                    'install-cycle-' + $iteration + '-installer.json')) `
                -Encoding utf8
        $installLog += $cycleResult
        if ($installExit -ne 0 -or
            -not $install.endpointPairValid -or
            -not $healthy) {
            throw "Install failed in cycle $iteration."
        }
        ++$result.installUninstallCyclesCompleted
    }
    $installLog | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath (
            Join-Path $adminRoot 'install-uninstall-cycles.json') `
            -Encoding utf8
    Invoke-PoolSnapshot -Name 'after-install-uninstall-cycles'
    Record-Category -Name 'install-uninstall-5'

    $result.passed = $true
} catch {
    $result.failure = $_.Exception.Message
    throw
} finally {
    $result.completedAt = (Get-Date).ToString('o')
    $result | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (
            Join-Path $adminRoot 'admin-stress-result.json') `
            -Encoding utf8
}

$result | ConvertTo-Json -Depth 8
