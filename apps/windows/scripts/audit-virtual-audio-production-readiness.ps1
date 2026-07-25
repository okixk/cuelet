[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CandidatePackage,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot
)

$ErrorActionPreference = 'Stop'
$CandidatePackage = [IO.Path]::GetFullPath($CandidatePackage)
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$driverProject = Join-Path $PSScriptRoot (
    '..\Cuelet.VirtualAudio.Driver\obj\sysvad\audio\sysvad\' +
    'TabletAudioSample\TabletAudioSample.vcxproj')
$preparedInf = Join-Path $CandidatePackage 'CueletVirtualAudio.inf'
$prepareScript = Join-Path $PSScriptRoot `
    '..\Cuelet.VirtualAudio.Driver\prepare-driver-source.ps1'

foreach ($path in @(
    $CandidatePackage, $preparedInf, $driverProject, $prepareScript)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Production-readiness input not found: $path"
    }
}
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

$hashManifest = Join-Path $CandidatePackage 'candidate-hashes.sha256'
$hashChecks = @()
foreach ($line in Get-Content -LiteralPath $hashManifest) {
    if ($line -notmatch '^([A-Fa-f0-9]{64}) \*(.+)$') {
        throw "Malformed candidate hash line: $line"
    }
    $file = Join-Path $CandidatePackage $Matches[2]
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    $hashChecks += [ordered]@{
        name = $Matches[2]
        expectedSha256 = $Matches[1].ToUpperInvariant()
        actualSha256 = $actual
        matches = $actual -eq $Matches[1].ToUpperInvariant()
        bytes = (Get-Item -LiteralPath $file).Length
    }
}

[xml]$projectXml = Get-Content -LiteralPath $driverProject -Raw
$namespace = [Xml.XmlNamespaceManager]::new($projectXml.NameTable)
$namespace.AddNamespace(
    'msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
$compileUnits = @($projectXml.SelectNodes(
    '//msb:ClCompile', $namespace) | ForEach-Object {
        [string]$_.Include
    })
$infLines = @(Get-Content -LiteralPath $preparedInf)
$infSections = @($infLines | Where-Object {
    $_ -match '^\[[^\]]+\]$'
} | ForEach-Object {
    $_.Trim('[', ']')
})
$sddl = [string]($infLines | Where-Object {
    $_ -match '^\s*HKR,,Security'
} | Select-Object -First 1)
$inheritedTerms = @(
    'Hdmi', 'Spdif', 'SpeakerHeadphone', 'MicIn', 'MicArray',
    'BthHfp', 'UsbHs', 'MsApo', 'KeywordDetector', 'A2dp')
$inheritedSections = @($infSections | Where-Object {
    $section = $_
    @($inheritedTerms | Where-Object {
        $section -match [regex]::Escape($_)
    }).Count -ne 0
})
$unusedCompilePatterns = @(
    'A2dpHpDevice', 'BthhfpDevice', 'UsbHsDevice',
    'hdmitopo', 'micintopo', 'spdiftopo')
$inheritedCompileUnits = @($compileUnits | Where-Object {
    $unit = $_
    @($unusedCompilePatterns | Where-Object {
        $unit -match [regex]::Escape($_)
    }).Count -ne 0
})
$releaseTraceGuard = Select-String -LiteralPath $prepareScript `
    -Pattern '#if DBG' -SimpleMatch
$iconDirectives = @($infLines | Where-Object {
    $_ -match '(?i)iconpath|iconsource|deviceicon'
})

$releaseRoot = Join-Path $PSScriptRoot (
    '..\Cuelet.VirtualAudio.Driver\obj\sysvad\audio\sysvad\' +
    'TabletAudioSample\x64\Release')
$releaseDriverFiles = @(
    'CueletVirtualAudio.sys',
    'CueletVirtualAudio.pdb',
    'CueletVirtualAudio.inf'
)
$releaseArtifacts = @($releaseDriverFiles | ForEach-Object {
    $path = Join-Path $releaseRoot $_
    [ordered]@{
        path = $path
        present = Test-Path -LiteralPath $path -PathType Leaf
        bytes = if (Test-Path -LiteralPath $path -PathType Leaf) {
            (Get-Item -LiteralPath $path).Length
        } else {
            0
        }
        sha256 = if (Test-Path -LiteralPath $path -PathType Leaf) {
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        } else {
            ''
        }
    }
})

$audit = [ordered]@{
    completedAt = (Get-Date).ToString('o')
    candidatePackage = $CandidatePackage
    candidateHashesLocked = (
        @($hashChecks | Where-Object { -not $_.matches }).Count -eq 0)
    candidateArtifacts = $hashChecks
    releaseArtifacts = $releaseArtifacts
    upstreamRevision = (
        Get-Content -LiteralPath (
            Join-Path $PSScriptRoot `
                '..\Cuelet.VirtualAudio.Driver\UPSTREAM_SYSVAD_REVISION.txt') `
            -Raw).Trim()
    compileUnitCount = $compileUnits.Count
    compileUnits = $compileUnits
    inheritedCompileUnitsToReview = $inheritedCompileUnits
    infBytes = (Get-Item -LiteralPath $preparedInf).Length
    infSectionCount = $infSections.Count
    inheritedInfSectionsToReview = $inheritedSections
    inheritedInfSectionCount = $inheritedSections.Count
    deviceSecurityLine = $sddl
    deviceSecurityAllowsWorldGenericAccess = (
        $sddl -match '\(A;;GRGWGX;;;WD\)')
    customIconDirectiveCount = $iconDirectives.Count
    customIconDirectives = $iconDirectives
    releaseWppGuardOccurrences = @($releaseTraceGuard).Count
    wppCompiledOutOfRelease = @($releaseTraceGuard).Count -gt 0
    review = [ordered]@{
        removeUnusedSysvadSourceAndInfSurface = (
            $inheritedCompileUnits.Count -ne 0 -or
            $inheritedSections.Count -ne 0)
        narrowDeviceSecurityBeforePublicRelease = (
            $sddl -match '\(A;;GRGWGX;;;WD\)')
        addIntentionalEndpointIconBeforePublicRelease = (
            $iconDirectives.Count -eq 0)
        productionSigningRequired = $true
        supportedUpgradeNeedsTwoVersionValidation = $true
        failedUpgradeMustPreservePreviousWorkingVersion = $true
    }
}
$audit | ConvertTo-Json -Depth 12 |
    Set-Content -LiteralPath (
        Join-Path $EvidenceRoot 'production-readiness-audit.json') `
        -Encoding utf8
$audit | ConvertTo-Json -Depth 12
if (-not $audit.candidateHashesLocked) { exit 10 }
exit 0
