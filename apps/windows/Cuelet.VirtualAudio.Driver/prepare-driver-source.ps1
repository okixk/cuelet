[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$RefreshUpstream
)

$ErrorActionPreference = 'Stop'
$Destination = if ([string]::IsNullOrWhiteSpace($Destination)) {
    Join-Path $PSScriptRoot 'obj\sysvad'
} else {
    $Destination
}
$upstreamRepository = 'https://github.com/microsoft/Windows-driver-samples.git'
$upstreamCommit = '2ee527bfeb0aeb6be11f0a8b6dce4011b358ce89'
$driverRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$destinationPath = [IO.Path]::GetFullPath($Destination)

if (-not $destinationPath.StartsWith(
    $driverRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) {
    throw "The generated SysVAD tree must remain below $driverRoot."
}

function Set-TextFile {
    param([string]$Path, [string]$Value)
    [IO.File]::WriteAllText($Path, $Value, [Text.UTF8Encoding]::new($false))
}

function Replace-Required {
    param(
        [string]$Path,
        [string]$Old,
        [string]$New
    )

    $content = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $Old = $Old.Replace("`r`n", "`n")
    $New = $New.Replace("`r`n", "`n")
    if (-not $content.Contains($Old)) {
        $previewLength = [Math]::Min(160, $Old.Length)
        $preview = $Old.Substring(0, $previewLength).Replace("`n", '\n')
        throw "Pinned upstream source changed; expected '$preview' was not found in $Path."
    }
    Set-TextFile -Path $Path -Value $content.Replace($Old, $New)
}

function Replace-RangeRequired {
    param(
        [string]$Path,
        [string]$StartMarker,
        [string]$EndMarker,
        [string]$Replacement
    )

    $content = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $StartMarker = $StartMarker.Replace("`r`n", "`n")
    $EndMarker = $EndMarker.Replace("`r`n", "`n")
    $Replacement = $Replacement.Replace("`r`n", "`n")
    $start = $content.IndexOf($StartMarker, [StringComparison]::Ordinal)
    $end = $content.IndexOf($EndMarker, $start, [StringComparison]::Ordinal)
    if ($start -lt 0 -or $end -lt 0) {
        throw "Pinned upstream range changed in $Path."
    }
    Set-TextFile -Path $Path -Value (
        $content.Substring(0, $start) +
        $Replacement +
        $content.Substring($end))
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git is required to retrieve the pinned Microsoft SysVAD source.'
}

$gitDirectory = Join-Path $destinationPath '.git'
if (-not (Test-Path -LiteralPath $gitDirectory)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $destinationPath) | Out-Null
    & git clone --filter=blob:none --no-checkout --sparse $upstreamRepository $destinationPath
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not clone the pinned Windows driver samples repository.'
    }
    & git -C $destinationPath sparse-checkout set audio/sysvad
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not configure the SysVAD sparse checkout.'
    }
}
elseif ($RefreshUpstream) {
    & git -C $destinationPath fetch origin $upstreamCommit
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not refresh the pinned SysVAD commit.'
    }
}

& git -C $destinationPath checkout --detach --force $upstreamCommit
if ($LASTEXITCODE -ne 0) {
    throw "Could not check out pinned SysVAD commit $upstreamCommit."
}

# Do not run a broad clean over the generated checkout. Refresh only the
# Cuelet-owned untracked files that this script creates; MSBuild's own
# intermediates may remain in place for incremental builds.
$generatedFiles = @(
    'audio\sysvad\EndpointsCommon\CueletAudioBridge.cpp',
    'audio\sysvad\EndpointsCommon\CueletAudioFifoCore.h',
    'audio\sysvad\EndpointsCommon\CueletAudioBridge.h',
    'audio\sysvad\TabletAudioSample\CueletVirtualAudio.inx',
    'audio\sysvad\CueletStartupTrace.h'
)
foreach ($relativePath in $generatedFiles) {
    $generatedFile = [IO.Path]::GetFullPath(
        (Join-Path $destinationPath $relativePath))
    if (-not $generatedFile.StartsWith(
        $destinationPath + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to refresh a generated file outside $destinationPath."
    }
    if (Test-Path -LiteralPath $generatedFile) {
        Remove-Item -LiteralPath $generatedFile -Force
    }
}

$actualCommit = (& git -C $destinationPath rev-parse HEAD).Trim()
if ($actualCommit -ne $upstreamCommit) {
    throw "Expected SysVAD $upstreamCommit but checked out $actualCommit."
}

$sysvad = Join-Path $destinationPath 'audio\sysvad'
$endpoints = Join-Path $sysvad 'EndpointsCommon'
$tablet = Join-Path $sysvad 'TabletAudioSample'
$overlay = Join-Path $PSScriptRoot 'overlay\EndpointsCommon'
Copy-Item -LiteralPath (Join-Path $overlay 'CueletAudioBridge.h') -Destination $endpoints
Copy-Item -LiteralPath (Join-Path $overlay 'CueletAudioBridge.cpp') -Destination $endpoints
Copy-Item -LiteralPath (Join-Path $overlay 'CueletAudioFifoCore.h') -Destination $endpoints
Copy-Item -LiteralPath (Join-Path $overlay 'CueletStartupTrace.h') -Destination $sysvad

$stream = Join-Path $endpoints 'minwavertstream.cpp'
Replace-Required $stream @'
    ULONG ulBufferDurationMs = 0;

    if ( (0 == RequestedSize_) || (RequestedSize_ < m_pWfExt->Format.nBlockAlign) )
'@ @'
    ULONG ulBufferDurationMs = 0;
    CUELET_TRACE_CHECKPOINT(
        "CVA510 AllocateBufferWithNotification enter",
        STATUS_SUCCESS,
        RequestedSize_,
        NotificationCount_,
        m_pWfExt == nullptr ? 0 : m_pWfExt->Format.nBlockAlign,
        m_pWfExt == nullptr ? 0 : m_pWfExt->Format.nSamplesPerSec,
        m_pWfExt == nullptr ? 0 : m_pWfExt->Format.nChannels,
        m_pWfExt == nullptr ? 0 : m_pWfExt->Format.wBitsPerSample,
        m_ulDmaMovementRate,
        0);

    if ( (0 == RequestedSize_) || (RequestedSize_ < m_pWfExt->Format.nBlockAlign) )
'@
Replace-Required $stream @'
    if ((NotificationCount_ == 0) || (RequestedSize_ % NotificationCount_ != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
'@ @'
    if ((NotificationCount_ == 0) || (RequestedSize_ % NotificationCount_ != 0))
    {
        CUELET_TRACE_CHECKPOINT(
            "CVA511 invalid notification buffer parameters",
            STATUS_INVALID_PARAMETER,
            RequestedSize_,
            NotificationCount_,
            0, 0, 0, 0, 0, 0);
        return STATUS_INVALID_PARAMETER;
    }
'@
Replace-Required $stream @'
    *CacheType_ = MmCached;

    return STATUS_SUCCESS;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CMiniportWaveRTStream::FreeBufferWithNotification
'@ @'
    *CacheType_ = MmCached;

    CUELET_TRACE_CHECKPOINT(
        "CVA519 AllocateBufferWithNotification exit",
        STATUS_SUCCESS,
        m_ulDmaBufferSize,
        m_ulNotificationsPerBuffer,
        m_ulNotificationIntervalMs,
        ulBufferDurationMs,
        reinterpret_cast<ULONG_PTR>(m_pDmaBuffer),
        reinterpret_cast<ULONG_PTR>(pBufferMdl),
        m_ulDmaMovementRate,
        0);
    return STATUS_SUCCESS;
}

//=============================================================================
#pragma code_seg("PAGE")
VOID CMiniportWaveRTStream::FreeBufferWithNotification
'@
Replace-Required $stream @'
    m_ulDmaBufferSize = RequestedSize_;
    m_ulNotificationsPerBuffer = 0;

    *AudioBufferMdl_ = pBufferMdl;
'@ @'
    m_ulDmaBufferSize = RequestedSize_;
    m_ulNotificationsPerBuffer = 0;

    CUELET_TRACE_CHECKPOINT(
        "CVA529 AllocateAudioBuffer",
        STATUS_SUCCESS,
        m_ulDmaBufferSize,
        m_pWfExt == nullptr ? 0 : m_pWfExt->Format.nBlockAlign,
        reinterpret_cast<ULONG_PTR>(m_pDmaBuffer),
        reinterpret_cast<ULONG_PTR>(pBufferMdl),
        m_ulDmaMovementRate,
        0, 0, 0);
    *AudioBufferMdl_ = pBufferMdl;
'@
Replace-Required $stream @'
    NTSTATUS        ntStatus        = STATUS_SUCCESS;
    PADAPTERCOMMON  pAdapterComm    = m_pMiniport->GetAdapterCommObj();
    KIRQL oldIrql;
'@ @'
    NTSTATUS        ntStatus        = STATUS_SUCCESS;
    PADAPTERCOMMON  pAdapterComm    = m_pMiniport->GetAdapterCommObj();
    KIRQL oldIrql;
    CUELET_TRACE_CHECKPOINT(
        "CVA530 SetState enter",
        STATUS_SUCCESS,
        m_KsState,
        State_,
        m_ulDmaBufferSize,
        m_ulNotificationsPerBuffer,
        m_ulNotificationIntervalMs,
        m_ulDmaMovementRate,
        m_bCapture,
        reinterpret_cast<ULONG_PTR>(m_pNotificationTimer));
'@
Replace-Required $stream @'
                ExSetTimer
                (
                    m_pNotificationTimer,
                    (-1) * HNSTIME_PER_MILLISECOND,
                    HNSTIME_PER_MILLISECOND, // 1 ms 
                    NULL
                 );
'@ @'
                const BOOLEAN timerArmed = ExSetTimer
                (
                    m_pNotificationTimer,
                    (-1) * HNSTIME_PER_MILLISECOND,
                    HNSTIME_PER_MILLISECOND, // 1 ms
                    NULL
                 );
                UNREFERENCED_PARAMETER(timerArmed);
                CUELET_TRACE_CHECKPOINT(
                    "CVA531 ExSetTimer armed",
                    STATUS_SUCCESS,
                    timerArmed,
                    m_ulNotificationIntervalMs,
                    HNSTIME_PER_MILLISECOND,
                    m_ulDmaBufferSize,
                    m_ulNotificationsPerBuffer,
                    0, 0, 0);
'@
Replace-Required $stream @'
#endif  // defined(SYSVAD_BTH_BYPASS) || defined(SYSVAD_USB_SIDEBAND)
    return ntStatus;
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::SetFormat
'@ @'
#endif  // defined(SYSVAD_BTH_BYPASS) || defined(SYSVAD_USB_SIDEBAND)
    CUELET_TRACE_CHECKPOINT(
        "CVA539 SetState exit",
        ntStatus,
        m_KsState,
        State_,
        m_ulDmaBufferSize,
        m_ulNotificationsPerBuffer,
        m_ulNotificationIntervalMs,
        0, 0, 0);
    return ntStatus;
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CMiniportWaveRTStream::SetFormat
'@
Replace-Required $stream @'
#include "AudioModuleHelper.h"
'@ @'
#include "AudioModuleHelper.h"
#include "CueletAudioBridge.h"
#include "CueletAudioFifoCore.h"
#include "CueletStartupTrace.h"
'@
Replace-Required $stream @'
    m_pNotificationTimer = ExAllocateTimer(
         TimerNotifyRT,
         this,
         EX_TIMER_HIGH_RESOLUTION
    );
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA500 WaveRT stream Init enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(this),
        reinterpret_cast<ULONG_PTR>(Miniport_),
        reinterpret_cast<ULONG_PTR>(PortStream_),
        Pin_,
        Capture_,
        reinterpret_cast<ULONG_PTR>(DataFormat_),
        0, 0);
    m_pNotificationTimer = ExAllocateTimer(
         TimerNotifyRT,
         this,
         EX_TIMER_HIGH_RESOLUTION
    );
    CUELET_TRACE_CHECKPOINT(
        "CVA501 ExAllocateTimer",
        m_pNotificationTimer == nullptr
            ? STATUS_INSUFFICIENT_RESOURCES
            : STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(m_pNotificationTimer),
        EX_TIMER_HIGH_RESOLUTION,
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $stream @'
    pWfEx = GetWaveFormatEx(DataFormat_);
    if (NULL == pWfEx) 
'@ @'
    pWfEx = GetWaveFormatEx(DataFormat_);
    CUELET_TRACE_CHECKPOINT(
        "CVA502 WaveRT stream format",
        pWfEx == nullptr ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS,
        pWfEx == nullptr ? 0 : pWfEx->nSamplesPerSec,
        pWfEx == nullptr ? 0 : pWfEx->nChannels,
        pWfEx == nullptr ? 0 : pWfEx->wBitsPerSample,
        pWfEx == nullptr ? 0 : pWfEx->nBlockAlign,
        pWfEx == nullptr ? 0 : pWfEx->nAvgBytesPerSec,
        pWfEx == nullptr ? 0 : pWfEx->wFormatTag,
        pWfEx == nullptr ? 0 : pWfEx->cbSize,
        0);
    if (NULL == pWfEx) 
'@
Replace-Required $stream @'
    m_pDpc = (PRKDPC)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(KDPC), MINWAVERTSTREAM_POOLTAG);
'@ @'
    m_pDpc = (PRKDPC)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(KDPC),
        MINWAVERTSTREAM_POOLTAG);
    CUELET_TRACE_CHECKPOINT(
        "CVA503 DPC allocation",
        m_pDpc == nullptr ? STATUS_INSUFFICIENT_RESOURCES : STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(m_pDpc),
        sizeof(KDPC),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $stream @'
    ntStatus = m_pMiniport->StreamCreated(m_ulPin, this);
'@ @'
    ntStatus = m_pMiniport->StreamCreated(m_ulPin, this);
    CUELET_TRACE_CHECKPOINT(
        "CVA509 WaveRT stream Init/StreamCreated exit",
        ntStatus,
        m_ulPin,
        m_bCapture,
        m_ulDmaMovementRate,
        reinterpret_cast<ULONG_PTR>(m_pNotificationTimer),
        reinterpret_cast<ULONG_PTR>(m_pDpc),
        reinterpret_cast<ULONG_PTR>(m_pWfExt),
        m_AudioModuleCount,
        0);
'@
Replace-Required $stream @'
    PAGED_CODE();
    if (NULL != m_pMiniport)
'@ @'
    PAGED_CODE();

    // The notification callback owns a raw stream pointer and can run at
    // DISPATCH_LEVEL. Quiesce it before releasing any callback-visible state.
    if (m_pNotificationTimer)
    {
        ExDeleteTimer
        (
            m_pNotificationTimer,
            TRUE,
            TRUE,
            NULL
        );
        m_pNotificationTimer = NULL;
    }
    KeFlushQueuedDpcs();

    CueletAudioBridgeReleaseReader(this);
    if (NULL != m_pMiniport)
'@
Replace-Required $stream @'
    if (m_pNotificationTimer)
    {
        ExDeleteTimer
        (
            m_pNotificationTimer, 
            TRUE, // Cancel the timer if it is currently set.
            TRUE, // Wait for the timer to finish expiring and for any callback to a ExTimerCallback routine to finish.
            NULL
         );
    }

    // Since we just cancelled the notification timer, wait for all queued 
    // DPCs to complete before we free the notification DPC.
    //
    KeFlushQueuedDpcs();

'@ ''
Replace-Required $stream @'
        if (!g_DoNotCreateDataFiles)
        {
            // Read from buffer and write to a file.
            ReadBytes(ByteDisplacement);
        }
'@ @'
        // Publish every rendered byte to the paired capture endpoint. Optional
        // sample file output remains controlled by the upstream debug setting.
        ReadBytes(ByteDisplacement);
'@
Replace-Required $stream @'
    // Convert ticks to 100ns units.
    LONGLONG  hnsCurrentTime = KSCONVERT_PERFORMANCE_TIME(m_ullPerformanceCounterFrequency.QuadPart, ilQPC);
    
    // Calculate the time elapsed since the last call to GetPosition() or since the
    // DMA engine started.  Note that the division by 10000 to convert to milliseconds
    // may cause us to lose some of the time, so we will carry the remainder forward 
    // to the next GetPosition() call.
    //
    ULONG TimeElapsedInMS = (ULONG)(hnsCurrentTime - m_ullDmaTimeStamp + m_hnsElapsedTimeCarryForward)/10000;
    
    // Carry forward the remainder of this division so we don't fall behind with our position too much.
    //
    m_hnsElapsedTimeCarryForward = (hnsCurrentTime - m_ullDmaTimeStamp + m_hnsElapsedTimeCarryForward) % 10000;
    
    // Calculate how many bytes in the DMA buffer would have been processed in the elapsed
    // time.  Note that the division by 1000 to convert to milliseconds may cause us to 
    // lose some bytes, so we will carry the remainder forward to the next GetPosition() call.
    //
    // need to divide by 1000 because m_ulDmaMovementRate is average bytes per sec.

    ULONG ByteDisplacement = ((m_ulDmaMovementRate * TimeElapsedInMS) + m_byteDisplacementCarryForward) / 1000 ;
    m_byteDisplacementCarryForward = ((m_ulDmaMovementRate * TimeElapsedInMS) + m_byteDisplacementCarryForward) % 1000;
'@ @'
    // Convert ticks to 100ns units and reject a regressing clock without
    // allowing unsigned subtraction to wrap.
    const LONGLONG hnsCurrentTime =
        KSCONVERT_PERFORMANCE_TIME(
            m_ullPerformanceCounterFrequency.QuadPart, ilQPC);
    const ULONGLONG currentHundredNanoseconds =
        hnsCurrentTime > 0
            ? static_cast<ULONGLONG>(hnsCurrentTime)
            : 0;
    ULONGLONG elapsedHundredNanoseconds = 0;
    if (currentHundredNanoseconds >= m_ullDmaTimeStamp)
    {
        elapsedHundredNanoseconds =
            currentHundredNanoseconds - m_ullDmaTimeStamp;
    }
    else
    {
        m_hnsElapsedTimeCarryForward = 0;
        m_byteDisplacementCarryForward = 0;
    }

    const auto displacement =
        cuelet::audio_fifo::PlanByteDisplacement(
            {m_ulDmaMovementRate},
            elapsedHundredNanoseconds,
            m_hnsElapsedTimeCarryForward,
            m_byteDisplacementCarryForward,
            {m_ulDmaBufferSize},
            {static_cast<ULONGLONG>(
                m_pWfExt == nullptr
                    ? 0
                    : m_pWfExt->Format.nBlockAlign)});
    ULONG ByteDisplacement = 0;
    if (displacement.valid)
    {
        NT_ASSERT(displacement.byteCount.value <= MAXULONG);
        NT_ASSERT(displacement.thousandthByteCarry <= MAXULONG);
        ByteDisplacement =
            static_cast<ULONG>(displacement.byteCount.value);
        m_hnsElapsedTimeCarryForward =
            displacement.hundredNanosecondCarry;
        m_byteDisplacementCarryForward =
            static_cast<ULONG>(
                displacement.thousandthByteCarry);
    }
'@
Replace-Required $stream `
    '    m_ullDmaTimeStamp = hnsCurrentTime;' `
    '    m_ullDmaTimeStamp = currentHundredNanoseconds;'
Replace-Required $stream @'
            m_ToneGenerator.GenerateSine(m_pDmaBuffer + bufferOffset, runWrite);
'@ @'
        CueletAudioBridgeRead(
            this,
            m_pDmaBuffer + bufferOffset,
            runWrite,
            m_pWfExt == nullptr ? nullptr : &m_pWfExt->Format);
'@
Replace-Required $stream @'
        m_SaveData.WriteData(m_pDmaBuffer + bufferOffset, runWrite);
'@ @'
        CueletAudioBridgePublish(
            m_pDmaBuffer + bufferOffset,
            runWrite,
            m_pWfExt == nullptr ? nullptr : &m_pWfExt->Format);
        if (!g_DoNotCreateDataFiles)
        {
            m_SaveData.WriteData(m_pDmaBuffer + bufferOffset, runWrite);
        }
'@

$audioModuleHelper = Join-Path $endpoints 'AudioModuleHelper.cpp'
Replace-Required $audioModuleHelper @'
    ASSERT(ParameterInfo);
    ASSERT(BufferCb);

    //
    // Compute total size of property.
'@ @'
    ASSERT(ParameterInfo);
    ASSERT(BufferCb);

    if (Buffer == NULL && *BufferCb != 0)
    {
        *BufferCb = 0;
        return STATUS_INVALID_PARAMETER;
    }

    //
    // Compute total size of property.
'@
Replace-Required $audioModuleHelper @'
    if (BufferCb < ParameterInfo->Size)
    {
        validParam = FALSE;
        goto exit;
    }
'@ @'
    if (ParameterInfo->Size == 0 ||
        BufferCb < ParameterInfo->Size)
    {
        validParam = FALSE;
        goto exit;
    }
'@
Replace-Required $audioModuleHelper @'
            for (j=0; j < ParameterInfo->Size; ++j)
            {
                if (buffer[j] != pattern[j])
'@ @'
            for (j=0; j < ParameterInfo->Size; ++j)
            {
                if (j >= BufferCb)
                {
                    validParam = FALSE;
                    goto exit;
                }
                if (buffer[j] != pattern[j])
'@
Replace-Required $audioModuleHelper @'
        for (i = 0; i < ParameterInfo->Size; ++i)
        {
            if (buffer[i] != 0xFF)
'@ @'
        for (i = 0; i < ParameterInfo->Size; ++i)
        {
            if (i >= BufferCb)
            {
                validParam = FALSE;
                goto exit;
            }
            if (buffer[i] != 0xFF)
'@
Replace-Required $audioModuleHelper @'
        if (*OutBufferCb < cbMinSize)
        {
            *OutBufferCb = 0;
            return STATUS_BUFFER_TOO_SMALL;
'@ @'
        if (*OutBufferCb < cbMinSize || OutBuffer == NULL)
        {
            *OutBufferCb = 0;
            return STATUS_BUFFER_TOO_SMALL;
'@
Replace-Required $audioModuleHelper @'
        if (ParameterInfo->Size != 
                RtlCompareMemory(CurrentValue, InBuffer, ParameterInfo->Size))
'@ @'
        if (InBuffer == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (ParameterInfo->Size !=
                RtlCompareMemory(CurrentValue, InBuffer, ParameterInfo->Size))
'@

$newDelete = Join-Path $endpoints 'NewDelete.cpp'
Replace-Required $newDelete @'
{
    PVOID result = ExAllocatePool2(poolFlags, iSize, tag);
'@ @'
{
    if ((poolFlags & POOL_FLAG_NON_PAGED_EXECUTE) != 0)
    {
        return NULL;
    }
    PVOID result = ExAllocatePool2(poolFlags, iSize, tag);
'@
Replace-Required $newDelete @'
{
    PVOID result = ExAllocatePool2(poolFlags, iSize, SYSVAD_POOLTAG);
'@ @'
{
    if ((poolFlags & POOL_FLAG_NON_PAGED_EXECUTE) != 0)
    {
        return NULL;
    }
    PVOID result = ExAllocatePool2(poolFlags, iSize, SYSVAD_POOLTAG);
'@

$adapter = Join-Path $sysvad 'adapter.cpp'
Replace-Required $adapter @'
#include "IHVPrivatePropertySet.h"
'@ @'
#include "IHVPrivatePropertySet.h"
#include "CueletAudioBridge.h"
#include "CueletStartupTrace.h"

#if DBG
#include "adapter.tmh"
#endif
'@
Replace-Required $adapter @'
UNICODE_STRING g_RegistryPath;      // This is used to store the registry settings path for the driver
'@ @'
UNICODE_STRING g_RegistryPath;      // This is used to store the registry settings path for the driver

#if DBG
extern "C"
void
CueletTraceCheckpoint(
    _In_z_ PCSTR checkpoint,
    _In_ NTSTATUS status,
    _In_ ULONGLONG value0,
    _In_ ULONGLONG value1,
    _In_ ULONGLONG value2,
    _In_ ULONGLONG value3,
    _In_ ULONGLONG value4,
    _In_ ULONGLONG value5,
    _In_ ULONGLONG value6,
    _In_ ULONGLONG value7)
{
    TraceEvents(
        TRACE_LEVEL_INFORMATION,
        CUELET_TRACE_STARTUP,
        "checkpoint=%s status=%!STATUS! "
        "v0=%I64u v1=%I64u v2=%I64u v3=%I64u "
        "v4=%I64u v5=%I64u v6=%I64u v7=%I64u",
        checkpoint,
        status,
        value0,
        value1,
        value2,
        value3,
        value4,
        value5,
        value6,
        value7);
}
#endif
'@
Replace-Required $adapter @'
    ntStatus = CopyRegistrySettingsPath(RegistryPathName);
'@ @'
    ntStatus = CopyRegistrySettingsPath(RegistryPathName);
    CUELET_TRACE_CHECKPOINT(
        "CVA002 CopyRegistrySettingsPath",
        ntStatus,
        g_RegistryPath.Length,
        g_RegistryPath.MaximumLength,
        reinterpret_cast<ULONG_PTR>(g_RegistryPath.Buffer),
        0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = WdfDriverCreate(DriverObject,
                               RegistryPathName,
                               WDF_NO_OBJECT_ATTRIBUTES,
                               &config,
                               WDF_NO_HANDLE);
'@ @'
    ntStatus = WdfDriverCreate(DriverObject,
                               RegistryPathName,
                               WDF_NO_OBJECT_ATTRIBUTES,
                               &config,
                               WDF_NO_HANDLE);
    CUELET_TRACE_CHECKPOINT(
        "CVA003 WdfDriverCreate",
        ntStatus,
        config.DriverInitFlags,
        config.DriverPoolTag,
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = GetRegistrySettings(RegistryPathName);
'@ @'
    ntStatus = GetRegistrySettings(RegistryPathName);
    CUELET_TRACE_CHECKPOINT(
        "CVA004 GetRegistrySettings",
        ntStatus,
        g_DoNotCreateDataFiles,
        g_DisableToneGenerator,
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus =  PcInitializeAdapterDriver(DriverObject,
                                          RegistryPathName,
                                          (PDRIVER_ADD_DEVICE)AddDevice);
'@ @'
    ntStatus = PcInitializeAdapterDriver(DriverObject,
                                         RegistryPathName,
                                         (PDRIVER_ADD_DEVICE)AddDevice);
    CUELET_TRACE_CHECKPOINT(
        "CVA005 PcInitializeAdapterDriver",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(AddDevice),
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
        ReleaseRegistryStringBuffer();
    }
    
    return ntStatus;
'@ @'
        ReleaseRegistryStringBuffer();
    }

    CUELET_TRACE_CHECKPOINT(
        "CVA009 DriverEntry exit",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(DriverObject->MajorFunction[IRP_MJ_PNP]),
        reinterpret_cast<ULONG_PTR>(DriverObject->DriverUnload),
        0, 0, 0, 0, 0, 0);
#if DBG
    if (!NT_SUCCESS(ntStatus))
    {
        WPP_CLEANUP(DriverObject);
    }
#endif

    return ntStatus;
'@
Replace-Required $adapter @'
    PPORTCLSStreamResourceManager pPortClsResMgr        = NULL;
    PPORTCLSStreamResourceManager2 pPortClsResMgr2      = NULL;
'@ @'
    NTSTATUS                    optionalStatus          = STATUS_SUCCESS;
'@
Replace-Required $adapter @'
        ntStatus = unknownWave->QueryInterface (IID_IPortClsEtwHelper, (PVOID *)&pPortClsEtwHelper);
        if (NT_SUCCESS(ntStatus))
'@ @'
        optionalStatus = unknownWave->QueryInterface(
            IID_IPortClsEtwHelper,
            (PVOID *)&pPortClsEtwHelper);
        CUELET_TRACE_CHECKPOINT(
            "CVA123 optional QueryInterface IPortClsEtwHelper",
            optionalStatus,
            reinterpret_cast<ULONG_PTR>(unknownWave),
            reinterpret_cast<ULONG_PTR>(pPortClsEtwHelper),
            0, 0, 0, 0, 0, 0);
        if (NT_SUCCESS(optionalStatus))
'@
Replace-Required $adapter @'
        ntStatus = unknownWave->QueryInterface(IID_IPortClsRuntimePower, (PVOID *)&pPortClsRuntimePower);
        if (NT_SUCCESS(ntStatus))
'@ @'
        optionalStatus = unknownWave->QueryInterface(
            IID_IPortClsRuntimePower,
            (PVOID *)&pPortClsRuntimePower);
        CUELET_TRACE_CHECKPOINT(
            "CVA124 optional QueryInterface IPortClsRuntimePower",
            optionalStatus,
            reinterpret_cast<ULONG_PTR>(unknownWave),
            reinterpret_cast<ULONG_PTR>(pPortClsRuntimePower),
            0, 0, 0, 0, 0, 0);
        if (NT_SUCCESS(optionalStatus))
'@
Replace-Required $adapter @'
            NTSTATUS ntStatusTest =
                pPortClsRuntimePower->SendPowerControl
                (
                    _pDeviceObject,
                    &GUID_NULL,
                    NULL,
                    0,
                    NULL,
                    0,
                    NULL
                );
'@ @'
            NTSTATUS ntStatusTest =
                pPortClsRuntimePower->SendPowerControl
                (
                    _pDeviceObject,
                    &GUID_NULL,
                    NULL,
                    0,
                    NULL,
                    0,
                    NULL
                );
            CUELET_TRACE_CHECKPOINT(
                "CVA125 optional SendPowerControl GUID_NULL",
                ntStatusTest,
                reinterpret_cast<ULONG_PTR>(_pDeviceObject),
                0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
                ntStatus = pPortClsRuntimePower->RegisterPowerControlCallback(_pDeviceObject, &PowerControlCallback, NULL);
                if (NT_SUCCESS(ntStatus))
                {
                    ntStatus = pPortClsRuntimePower->UnregisterPowerControlCallback(_pDeviceObject);
                }
'@ @'
                optionalStatus =
                    pPortClsRuntimePower->RegisterPowerControlCallback(
                        _pDeviceObject,
                        &PowerControlCallback,
                        NULL);
                CUELET_TRACE_CHECKPOINT(
                    "CVA126 optional RegisterPowerControlCallback",
                    optionalStatus,
                    reinterpret_cast<ULONG_PTR>(_pDeviceObject),
                    0, 0, 0, 0, 0, 0, 0);
                if (NT_SUCCESS(optionalStatus))
                {
                    optionalStatus =
                        pPortClsRuntimePower->UnregisterPowerControlCallback(
                            _pDeviceObject);
                    CUELET_TRACE_CHECKPOINT(
                        "CVA127 optional UnregisterPowerControlCallback",
                        optionalStatus,
                        reinterpret_cast<ULONG_PTR>(_pDeviceObject),
                        0, 0, 0, 0, 0, 0, 0);
                }
'@
Replace-Required $adapter @'
                ntStatus = ntStatusTest;
'@ @'
                optionalStatus = ntStatusTest;
'@
Replace-RangeRequired $adapter `
    @'
        //
        // Test: add and remove current thread as streaming audio resource.  
'@ `
    @'
    }

    SAFE_RELEASE(unknownTopology);
'@ `
    ''
Replace-Required $adapter @'
    ntStatus = _pAdapterCommon->InstallEndpointFilters(
        _pIrp,
        _pAeMiniports,
        NULL,
        &unknownTopology,
        &unknownWave,
        NULL, NULL);
'@ @'
    ntStatus = _pAdapterCommon->InstallEndpointFilters(
        _pIrp,
        _pAeMiniports,
        NULL,
        &unknownTopology,
        &unknownWave,
        NULL, NULL);
    CUELET_TRACE_CHECKPOINT(
        "CVA122 InstallEndpointFilters render",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(_pAeMiniports),
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->PinDeviceFormatsAndModesCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->PhysicalConnectionCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->WaveInterfacePropertyCount,
        reinterpret_cast<ULONG_PTR>(unknownTopology),
        reinterpret_cast<ULONG_PTR>(unknownWave),
        0, 0);
'@
Replace-Required $adapter @'
    SAFE_RELEASE(unknownTopology);
    SAFE_RELEASE(unknownWave);

    return ntStatus;
}

#pragma code_seg("PAGE")
NTSTATUS 
InstallAllRenderFilters(
'@ @'
    SAFE_RELEASE(unknownTopology);
    SAFE_RELEASE(unknownWave);

    CUELET_TRACE_CHECKPOINT(
        "CVA129 InstallEndpointRenderFilters exit",
        ntStatus,
        optionalStatus,
        0, 0, 0, 0, 0, 0, 0);
    return ntStatus;
}

#pragma code_seg("PAGE")
NTSTATUS 
InstallAllRenderFilters(
'@
Replace-Required $adapter @'
    for(ULONG i = 0; i < g_cRenderEndpoints; ++i, ++ppAeMiniports)
    {
        ntStatus = InstallEndpointRenderFilters(_pDeviceObject, _pIrp, _pAdapterCommon, *ppAeMiniports);
        IF_FAILED_JUMP(ntStatus, Exit);
    }
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA120 InstallAllRenderFilters enter",
        STATUS_SUCCESS,
        g_cRenderEndpoints,
        0, 0, 0, 0, 0, 0, 0);
    for(ULONG i = 0; i < g_cRenderEndpoints; ++i, ++ppAeMiniports)
    {
        PENDPOINT_MINIPAIR endpoint = *ppAeMiniports;
        CUELET_TRACE_CHECKPOINT(
            "CVA121 render endpoint descriptor",
            STATUS_SUCCESS,
            i,
            reinterpret_cast<ULONG_PTR>(endpoint),
            endpoint == nullptr ? 0 : endpoint->DeviceMaxChannels,
            endpoint == nullptr ? 0 : endpoint->PinDeviceFormatsAndModesCount,
            endpoint == nullptr ? 0 : endpoint->PhysicalConnectionCount,
            endpoint == nullptr ? 0 : endpoint->WaveInterfacePropertyCount,
            endpoint == nullptr ? 0 : endpoint->TopoInterfacePropertyCount,
            endpoint == nullptr ? 0 : endpoint->DeviceFlags);
        ntStatus = InstallEndpointRenderFilters(
            _pDeviceObject,
            _pIrp,
            _pAdapterCommon,
            endpoint);
        CUELET_TRACE_CHECKPOINT(
            "CVA130 render endpoint installed",
            ntStatus,
            i,
            0, 0, 0, 0, 0, 0, 0);
        IF_FAILED_JUMP(ntStatus, Exit);
    }
'@
Replace-Required $adapter @'
    ntStatus = _pAdapterCommon->InstallEndpointFilters(
        _pIrp,
        _pAeMiniports,
        NULL,
        NULL,
        NULL,
        NULL, NULL);
        
    return ntStatus;
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA142 capture endpoint descriptor",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(_pAeMiniports),
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->DeviceMaxChannels,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->PinDeviceFormatsAndModesCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->PhysicalConnectionCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->WaveInterfacePropertyCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->TopoInterfacePropertyCount,
        _pAeMiniports == nullptr ? 0 : _pAeMiniports->DeviceFlags,
        0);
    ntStatus = _pAdapterCommon->InstallEndpointFilters(
        _pIrp,
        _pAeMiniports,
        NULL,
        NULL,
        NULL,
        NULL, NULL);
    CUELET_TRACE_CHECKPOINT(
        "CVA143 InstallEndpointFilters capture exit",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(_pAeMiniports),
        0, 0, 0, 0, 0, 0, 0);

    return ntStatus;
'@
Replace-Required $adapter @'
    for(ULONG i = 0; i < g_cCaptureEndpoints; ++i, ++ppAeMiniports)
    {
        ntStatus = InstallEndpointCaptureFilters(_pDeviceObject, _pIrp, _pAdapterCommon, *ppAeMiniports);
        IF_FAILED_JUMP(ntStatus, Exit);
    }
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA140 InstallAllCaptureFilters enter",
        STATUS_SUCCESS,
        g_cCaptureEndpoints,
        0, 0, 0, 0, 0, 0, 0);
    for(ULONG i = 0; i < g_cCaptureEndpoints; ++i, ++ppAeMiniports)
    {
        ntStatus = InstallEndpointCaptureFilters(
            _pDeviceObject,
            _pIrp,
            _pAdapterCommon,
            *ppAeMiniports);
        CUELET_TRACE_CHECKPOINT(
            "CVA150 capture endpoint installed",
            ntStatus,
            i,
            0, 0, 0, 0, 0, 0, 0);
        IF_FAILED_JUMP(ntStatus, Exit);
    }
'@
Replace-Required $adapter @'
    DPF(D_TERSE, ("[DriverEntry]"));

'@ @'
    DPF(D_TERSE, ("[DriverEntry]"));
#if DBG
    WPP_INIT_TRACING(DriverObject, RegistryPathName);
#endif
    CUELET_TRACE_CHECKPOINT(
        "CVA000 DriverEntry enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(DriverObject),
        reinterpret_cast<ULONG_PTR>(RegistryPathName),
        RegistryPathName == nullptr ? 0 : RegistryPathName->Length,
        0, 0, 0, 0, 0);
    CueletAudioBridgeInitialize();
    CUELET_TRACE_CHECKPOINT(
        "CVA001 bridge initialized",
        STATUS_SUCCESS,
        0, 0, 0, 0, 0, 0, 0, 0);

'@
Replace-Required $adapter @'
    DPF(D_TERSE, ("[AddDevice]"));

    maxObjects = g_MaxMiniports;
'@ @'
    DPF(D_TERSE, ("[AddDevice]"));
    CUELET_TRACE_CHECKPOINT(
        "CVA010 AddDevice enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(DriverObject),
        reinterpret_cast<ULONG_PTR>(PhysicalDeviceObject),
        g_MaxMiniports,
        g_cRenderEndpoints,
        g_cCaptureEndpoints,
        0, 0, 0);

    maxObjects = g_MaxMiniports;
'@
Replace-Required $adapter @'
        );



    return ntStatus;
} // AddDevice
'@ @'
        );

    CUELET_TRACE_CHECKPOINT(
        "CVA019 PcAddAdapterDevice / AddDevice exit",
        ntStatus,
        maxObjects,
        reinterpret_cast<ULONG_PTR>(PhysicalDeviceObject),
        0, 0, 0, 0, 0, 0);

    return ntStatus;
} // AddDevice
'@
Replace-Required $adapter @'
    DPF(D_TERSE, ("[DriverUnload]"));

    ReleaseRegistryStringBuffer();
'@ @'
    DPF(D_TERSE, ("[DriverUnload]"));
    CUELET_TRACE_CHECKPOINT(
        "CVA090 DriverUnload enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(DriverObject),
        0, 0, 0, 0, 0, 0, 0);
    CueletAudioBridgeBeginTeardown();

    ReleaseRegistryStringBuffer();
'@
Replace-Required $adapter @'
Done:
    return;
}

//=============================================================================
#pragma code_seg("INIT")
'@ @'
Done:
    CUELET_TRACE_CHECKPOINT(
        "CVA099 DriverUnload exit",
        STATUS_SUCCESS,
        0, 0, 0, 0, 0, 0, 0, 0);
#if DBG
    if (DriverObject != nullptr)
    {
        WPP_CLEANUP(DriverObject);
    }
#endif
    return;
}

//=============================================================================
#pragma code_seg("INIT")
'@
Replace-Required $adapter @'
    stack = IoGetCurrentIrpStackLocation(_Irp);

    switch (stack->MinorFunction)
'@ @'
    stack = IoGetCurrentIrpStackLocation(_Irp);
    const UCHAR pnpMinorFunction = stack->MinorFunction;
    UNREFERENCED_PARAMETER(pnpMinorFunction);

    CUELET_TRACE_CHECKPOINT(
        "CVA200 PnP transition enter",
        _Irp->IoStatus.Status,
        pnpMinorFunction,
        reinterpret_cast<ULONG_PTR>(_DeviceObject),
        reinterpret_cast<ULONG_PTR>(_Irp),
        0, 0, 0, 0, 0);

    if (stack->MinorFunction == IRP_MN_STOP_DEVICE ||
        stack->MinorFunction == IRP_MN_REMOVE_DEVICE ||
        stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL)
    {
        CUELET_TRACE_CHECKPOINT(
            "CVA201 PnP teardown gate",
            STATUS_SUCCESS,
            pnpMinorFunction,
            0, 0, 0, 0, 0, 0, 0);
        CueletAudioBridgeBeginTeardown();
    }

    switch (stack->MinorFunction)
'@
Replace-Required $adapter @'
    ntStatus = PcDispatchIrp(_DeviceObject, _Irp);

    return ntStatus;
'@ @'
    ntStatus = PcDispatchIrp(_DeviceObject, _Irp);
    CUELET_TRACE_CHECKPOINT(
        "CVA209 PnP transition exit",
        ntStatus,
        pnpMinorFunction,
        0, 0, 0, 0, 0, 0, 0);

    return ntStatus;
'@
Replace-Required $adapter @'
    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    ASSERT(DeviceObject);
    ASSERT(Irp);
    ASSERT(ResourceList);
'@ @'
    PAGED_CODE();

    ASSERT(DeviceObject);
    ASSERT(Irp);

    const ULONG resourceEntryCount =
        ResourceList == nullptr ? 0 : ResourceList->NumberOfEntries();
    PCM_RESOURCE_LIST rawResourceList =
        ResourceList == nullptr ? nullptr : ResourceList->UntranslatedList();
    PCM_RESOURCE_LIST translatedResourceList =
        ResourceList == nullptr ? nullptr : ResourceList->TranslatedList();
    UNREFERENCED_PARAMETER(resourceEntryCount);
    UNREFERENCED_PARAMETER(rawResourceList);
    UNREFERENCED_PARAMETER(translatedResourceList);
    CUELET_TRACE_CHECKPOINT(
        "CVA100 StartDevice enter/resources",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(DeviceObject),
        reinterpret_cast<ULONG_PTR>(Irp),
        reinterpret_cast<ULONG_PTR>(ResourceList),
        resourceEntryCount,
        reinterpret_cast<ULONG_PTR>(rawResourceList),
        rawResourceList == nullptr ? 0 : rawResourceList->Count,
        reinterpret_cast<ULONG_PTR>(translatedResourceList),
        translatedResourceList == nullptr ? 0 : translatedResourceList->Count);
'@
Replace-Required $adapter @'
    ntStatus = NewAdapterCommon( 
                                &pUnknownCommon,
                                IID_IAdapterCommon,
                                NULL,
                                POOL_FLAG_NON_PAGED 
                                );
'@ @'
    ntStatus = NewAdapterCommon(
                                &pUnknownCommon,
                                IID_IAdapterCommon,
                                NULL,
                                POOL_FLAG_NON_PAGED
                                );
    CUELET_TRACE_CHECKPOINT(
        "CVA101 NewAdapterCommon",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(pUnknownCommon),
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = pUnknownCommon->QueryInterface( IID_IAdapterCommon,(PVOID *) &pAdapterCommon);
'@ @'
    ntStatus = pUnknownCommon->QueryInterface(
        IID_IAdapterCommon,
        (PVOID *)&pAdapterCommon);
    CUELET_TRACE_CHECKPOINT(
        "CVA102 adapter QueryInterface",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(pAdapterCommon),
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = pAdapterCommon->Init(DeviceObject);
'@ @'
    ntStatus = pAdapterCommon->Init(DeviceObject);
    CUELET_TRACE_CHECKPOINT(
        "CVA103 CAdapterCommon::Init",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(pAdapterCommon),
        reinterpret_cast<ULONG_PTR>(DeviceObject),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = PcRegisterAdapterPowerManagement( PUNKNOWN(pAdapterCommon), DeviceObject);
'@ @'
    ntStatus = PcRegisterAdapterPowerManagement(
        PUNKNOWN(pAdapterCommon),
        DeviceObject);
    CUELET_TRACE_CHECKPOINT(
        "CVA104 PcRegisterAdapterPowerManagement",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(pAdapterCommon),
        reinterpret_cast<ULONG_PTR>(DeviceObject),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = InstallAllRenderFilters(DeviceObject, Irp, pAdapterCommon);
'@ @'
    ntStatus = InstallAllRenderFilters(DeviceObject, Irp, pAdapterCommon);
    CUELET_TRACE_CHECKPOINT(
        "CVA131 InstallAllRenderFilters exit",
        ntStatus,
        g_cRenderEndpoints,
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
    ntStatus = InstallAllCaptureFilters(DeviceObject, Irp, pAdapterCommon);
'@ @'
    ntStatus = InstallAllCaptureFilters(DeviceObject, Irp, pAdapterCommon);
    CUELET_TRACE_CHECKPOINT(
        "CVA151 InstallAllCaptureFilters exit",
        ntStatus,
        g_cCaptureEndpoints,
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $adapter @'
Exit:

    //
    // Stash the adapter common object in the device extension so
'@ @'
Exit:

    if (NT_SUCCESS(ntStatus))
    {
        CueletAudioBridgePrepareForDeviceStart();
        CUELET_TRACE_CHECKPOINT(
            "CVA160 bridge prepared for device start",
            ntStatus,
            0, 0, 0, 0, 0, 0, 0, 0);
    }
    else
    {
        CUELET_TRACE_CHECKPOINT(
            "CVA190 StartDevice partial cleanup begin",
            ntStatus,
            reinterpret_cast<ULONG_PTR>(pAdapterCommon),
            reinterpret_cast<ULONG_PTR>(pUnknownCommon),
            0, 0, 0, 0, 0, 0);
        CueletAudioBridgeBeginTeardown();
    }

    //
    // Stash the adapter common object in the device extension so
'@
Replace-Required $adapter @'
    SAFE_RELEASE(pUnknownCommon);
    
    return ntStatus;
} // StartDevice
'@ @'
    SAFE_RELEASE(pUnknownCommon);

    CUELET_TRACE_CHECKPOINT(
        "CVA199 StartDevice exit",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(pAdapterCommon),
        reinterpret_cast<ULONG_PTR>(pExtension),
        pExtension == nullptr
            ? 0
            : reinterpret_cast<ULONG_PTR>(pExtension->m_pCommon),
        0, 0, 0, 0, 0);
    return ntStatus;
} // StartDevice
'@

$common = Join-Path $sysvad 'common.cpp'
Replace-Required $common @'
#include "IHVPrivatePropertySet.h"
#include "simple.h"
'@ @'
#include "IHVPrivatePropertySet.h"
#include "simple.h"
#include "CueletStartupTrace.h"
'@
Replace-Required $common @'
    NTSTATUS        ntStatus    = STATUS_SUCCESS;

#ifdef SYSVAD_BTH_BYPASS
'@ @'
    NTSTATUS        ntStatus    = STATUS_SUCCESS;
    CUELET_TRACE_CHECKPOINT(
        "CVA110 CAdapterCommon::Init enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(this),
        reinterpret_cast<ULONG_PTR>(DeviceObject),
        0, 0, 0, 0, 0, 0);

#ifdef SYSVAD_BTH_BYPASS
'@
Replace-Required $common @'
    ntStatus = PcGetPhysicalDeviceObject(DeviceObject, &m_pPhysicalDeviceObject);
'@ @'
    ntStatus = PcGetPhysicalDeviceObject(
        DeviceObject,
        &m_pPhysicalDeviceObject);
    CUELET_TRACE_CHECKPOINT(
        "CVA111 PcGetPhysicalDeviceObject",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(m_pPhysicalDeviceObject),
        0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    ntStatus = WdfDeviceMiniportCreate( WdfGetDriver(),
                                        WDF_NO_OBJECT_ATTRIBUTES,
                                        DeviceObject,           // FDO
                                        NULL,                   // Next device.
                                        NULL,                   // PDO
                                       &m_WdfDevice);
'@ @'
    ntStatus = WdfDeviceMiniportCreate(
        WdfGetDriver(),
        WDF_NO_OBJECT_ATTRIBUTES,
        DeviceObject,           // FDO
        NULL,                   // Next device.
        NULL,                   // PDO
        &m_WdfDevice);
    CUELET_TRACE_CHECKPOINT(
        "CVA112 WdfDeviceMiniportCreate",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(m_WdfDevice),
        reinterpret_cast<ULONG_PTR>(m_pPhysicalDeviceObject),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    m_pHW = new (POOL_FLAG_NON_PAGED, SYSVAD_POOLTAG)  CSYSVADHW;
'@ @'
    m_pHW = new (POOL_FLAG_NON_PAGED, SYSVAD_POOLTAG) CSYSVADHW;
    CUELET_TRACE_CHECKPOINT(
        "CVA113 hardware object allocation",
        m_pHW == nullptr ? STATUS_INSUFFICIENT_RESOURCES : STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(m_pHW),
        sizeof(CSYSVADHW),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
Done:

    return ntStatus;
} // Init
'@ @'
Done:
    CUELET_TRACE_CHECKPOINT(
        "CVA119 CAdapterCommon::Init exit",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(m_pPhysicalDeviceObject),
        reinterpret_cast<ULONG_PTR>(m_WdfDevice),
        reinterpret_cast<ULONG_PTR>(m_pHW),
        0, 0, 0, 0, 0);
    return ntStatus;
} // Init
'@
Replace-Required $common @'
    ntStatus = IoRegisterDeviceInterface(
        GetPhysicalDeviceObject(),
        &KSCATEGORY_AUDIO,
        &referenceString,
        AudioSymbolicLinkName);
'@ @'
    ntStatus = IoRegisterDeviceInterface(
        GetPhysicalDeviceObject(),
        &KSCATEGORY_AUDIO,
        &referenceString,
        AudioSymbolicLinkName);
    CUELET_TRACE_CHECKPOINT(
        "CVA300 IoRegisterDeviceInterface audio",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(GetPhysicalDeviceObject()),
        referenceString.Length,
        reinterpret_cast<ULONG_PTR>(AudioSymbolicLinkName->Buffer),
        AudioSymbolicLinkName->Length,
        0, 0, 0, 0);
'@
Replace-Required $common @'
        ntStatus = MigrateDeviceInterfaceTemplateParameters(AudioSymbolicLinkName, TemplateReferenceString);
'@ @'
        ntStatus = MigrateDeviceInterfaceTemplateParameters(
            AudioSymbolicLinkName,
            TemplateReferenceString);
        CUELET_TRACE_CHECKPOINT(
            "CVA301 MigrateDeviceInterfaceTemplateParameters",
            ntStatus,
            reinterpret_cast<ULONG_PTR>(AudioSymbolicLinkName->Buffer),
            reinterpret_cast<ULONG_PTR>(TemplateReferenceString),
            0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    ntStatus = SysvadIoSetDeviceInterfacePropertyDataMultiple(AudioSymbolicLinkName, cPropertyCount, pProperties);
'@ @'
    ntStatus = SysvadIoSetDeviceInterfacePropertyDataMultiple(
        AudioSymbolicLinkName,
        cPropertyCount,
        pProperties);
    CUELET_TRACE_CHECKPOINT(
        "CVA302 interface property setup",
        ntStatus,
        cPropertyCount,
        reinterpret_cast<ULONG_PTR>(pProperties),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    ntStatus = CreateAudioInterfaceWithProperties(Name, TemplateName, cPropertyCount, pProperties, &symbolicLink);
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA310 InstallSubdevice enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(Name),
        reinterpret_cast<ULONG_PTR>(TemplateName),
        cPropertyCount,
        reinterpret_cast<ULONG_PTR>(MiniportPair),
        reinterpret_cast<ULONG_PTR>(ResourceList),
        reinterpret_cast<ULONG_PTR>(Irp),
        0, 0);
    ntStatus = CreateAudioInterfaceWithProperties(
        Name,
        TemplateName,
        cPropertyCount,
        pProperties,
        &symbolicLink);
    CUELET_TRACE_CHECKPOINT(
        "CVA311 CreateAudioInterfaceWithProperties",
        ntStatus,
        symbolicLink.Length,
        reinterpret_cast<ULONG_PTR>(symbolicLink.Buffer),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
        ntStatus = PcNewPort(&port, PortClassId);
'@ @'
        ntStatus = PcNewPort(&port, PortClassId);
        CUELET_TRACE_CHECKPOINT(
            "CVA312 PcNewPort",
            ntStatus,
            reinterpret_cast<ULONG_PTR>(port),
            0, 0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
            ntStatus = 
                MiniportCreate
                ( 
                    &miniport,
                    MiniportClassId,
                    NULL,
                    POOL_FLAG_NON_PAGED,
                    adapterCommon,
                    DeviceContext,
                    MiniportPair
                );
'@ @'
            ntStatus =
                MiniportCreate
                (
                    &miniport,
                    MiniportClassId,
                    NULL,
                    POOL_FLAG_NON_PAGED,
                    adapterCommon,
                    DeviceContext,
                    MiniportPair
                );
            CUELET_TRACE_CHECKPOINT(
                "CVA313 MiniportCreate",
                ntStatus,
                reinterpret_cast<ULONG_PTR>(miniport),
                reinterpret_cast<ULONG_PTR>(MiniportCreate),
                reinterpret_cast<ULONG_PTR>(MiniportPair),
                0, 0, 0, 0, 0);
'@
Replace-Required $common @'
        ntStatus = 
            port->Init
            ( 
                m_pDeviceObject,
                Irp,
                miniport,
                adapterCommon,
                ResourceList 
            );
'@ @'
        ntStatus =
            port->Init
            (
                m_pDeviceObject,
                Irp,
                miniport,
                adapterCommon,
                ResourceList
            );
        CUELET_TRACE_CHECKPOINT(
            "CVA314 port Init",
            ntStatus,
            reinterpret_cast<ULONG_PTR>(port),
            reinterpret_cast<ULONG_PTR>(miniport),
            reinterpret_cast<ULONG_PTR>(ResourceList),
            reinterpret_cast<ULONG_PTR>(m_pDeviceObject),
            0, 0, 0, 0);
'@
Replace-Required $common @'
            ntStatus = 
                PcRegisterSubdevice
                ( 
                    m_pDeviceObject,
                    Name,
                    port 
                );
'@ @'
            ntStatus =
                PcRegisterSubdevice
                (
                    m_pDeviceObject,
                    Name,
                    port
                );
            CUELET_TRACE_CHECKPOINT(
                "CVA315 PcRegisterSubdevice",
                ntStatus,
                reinterpret_cast<ULONG_PTR>(Name),
                reinterpret_cast<ULONG_PTR>(port),
                0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    if (port)
    {
        port->Release();
    }

    if (miniport)
    {
        miniport->Release();
    }

    return ntStatus;
} // InstallSubDevice
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA319 InstallSubdevice exit",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(port),
        reinterpret_cast<ULONG_PTR>(miniport),
        reinterpret_cast<ULONG_PTR>(OutPortUnknown),
        reinterpret_cast<ULONG_PTR>(OutMiniportUnknown),
        0, 0, 0, 0);
    if (port)
    {
        port->Release();
    }

    if (miniport)
    {
        miniport->Release();
    }

    return ntStatus;
} // InstallSubDevice
'@
Replace-Required $common @'
    NTSTATUS            ntStatus            = STATUS_SUCCESS;
    PUNKNOWN            unknownTopology     = NULL;
    PUNKNOWN            unknownWave         = NULL;
    BOOL                bTopologyCreated    = FALSE;
'@ @'
    NTSTATUS            ntStatus            = STATUS_SUCCESS;
    PUNKNOWN            unknownTopology     = NULL;
    PUNKNOWN            unknownWave         = NULL;
    BOOL                bTopologyCreated    = FALSE;
    CUELET_TRACE_CHECKPOINT(
        "CVA320 InstallEndpointFilters enter",
        STATUS_SUCCESS,
        reinterpret_cast<ULONG_PTR>(MiniportPair),
        MiniportPair == nullptr ? 0 : MiniportPair->PinDeviceFormatsAndModesCount,
        MiniportPair == nullptr ? 0 : MiniportPair->PhysicalConnectionCount,
        MiniportPair == nullptr ? 0 : MiniportPair->WaveInterfacePropertyCount,
        MiniportPair == nullptr ? 0 : MiniportPair->TopoInterfacePropertyCount,
        MiniportPair == nullptr ? 0 : MiniportPair->DeviceMaxChannels,
        MiniportPair == nullptr ? 0 : MiniportPair->DeviceFlags,
        0);
'@
Replace-Required $common @'
    ntStatus = GetCachedSubdevice(MiniportPair->TopoName, &unknownTopology, &unknownMiniTopo);
'@ @'
    ntStatus = GetCachedSubdevice(
        MiniportPair->TopoName,
        &unknownTopology,
        &unknownMiniTopo);
    CUELET_TRACE_CHECKPOINT(
        "CVA321 GetCachedSubdevice topology",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(unknownTopology),
        reinterpret_cast<ULONG_PTR>(unknownMiniTopo),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
            ntStatus = CacheSubdevice(MiniportPair->TopoName, unknownTopology, unknownMiniTopo);
'@ @'
            ntStatus = CacheSubdevice(
                MiniportPair->TopoName,
                unknownTopology,
                unknownMiniTopo);
            CUELET_TRACE_CHECKPOINT(
                "CVA322 CacheSubdevice topology",
                ntStatus,
                reinterpret_cast<ULONG_PTR>(unknownTopology),
                reinterpret_cast<ULONG_PTR>(unknownMiniTopo),
                0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    ntStatus = GetCachedSubdevice(MiniportPair->WaveName, &unknownWave, &unknownMiniWave);
'@ @'
    ntStatus = GetCachedSubdevice(
        MiniportPair->WaveName,
        &unknownWave,
        &unknownMiniWave);
    CUELET_TRACE_CHECKPOINT(
        "CVA323 GetCachedSubdevice wave",
        ntStatus,
        reinterpret_cast<ULONG_PTR>(unknownWave),
        reinterpret_cast<ULONG_PTR>(unknownMiniWave),
        0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
            ntStatus = CacheSubdevice(MiniportPair->WaveName, unknownWave, unknownMiniWave);
'@ @'
            ntStatus = CacheSubdevice(
                MiniportPair->WaveName,
                unknownWave,
                unknownMiniWave);
            CUELET_TRACE_CHECKPOINT(
                "CVA324 CacheSubdevice wave",
                ntStatus,
                reinterpret_cast<ULONG_PTR>(unknownWave),
                reinterpret_cast<ULONG_PTR>(unknownMiniWave),
                0, 0, 0, 0, 0, 0);
'@
Replace-Required $common @'
        ntStatus = ConnectTopologies(
            unknownTopology,
            unknownWave,
            MiniportPair->PhysicalConnections,
            MiniportPair->PhysicalConnectionCount);
'@ @'
        ntStatus = ConnectTopologies(
            unknownTopology,
            unknownWave,
            MiniportPair->PhysicalConnections,
            MiniportPair->PhysicalConnectionCount);
        CUELET_TRACE_CHECKPOINT(
            "CVA325 ConnectTopologies endpoint",
            ntStatus,
            MiniportPair->PhysicalConnectionCount,
            reinterpret_cast<ULONG_PTR>(unknownTopology),
            reinterpret_cast<ULONG_PTR>(unknownWave),
            0, 0, 0, 0, 0);
'@
Replace-Required $common @'
    SAFE_RELEASE(unknownMiniTopo);
    SAFE_RELEASE(unknownTopology);
    SAFE_RELEASE(unknownMiniWave);
    SAFE_RELEASE(unknownWave);

    return ntStatus;
}

//=============================================================================
#pragma code_seg("PAGE")
STDMETHODIMP_(NTSTATUS)
CAdapterCommon::RemoveEndpointFilters
'@ @'
    CUELET_TRACE_CHECKPOINT(
        "CVA329 InstallEndpointFilters exit",
        ntStatus,
        bTopologyCreated,
        bWaveCreated,
        reinterpret_cast<ULONG_PTR>(unknownTopology),
        reinterpret_cast<ULONG_PTR>(unknownWave),
        0, 0, 0, 0);
    SAFE_RELEASE(unknownMiniTopo);
    SAFE_RELEASE(unknownTopology);
    SAFE_RELEASE(unknownMiniWave);
    SAFE_RELEASE(unknownWave);

    return ntStatus;
}

//=============================================================================
#pragma code_seg("PAGE")
STDMETHODIMP_(NTSTATUS)
CAdapterCommon::RemoveEndpointFilters
'@
Replace-Required $common @'
                if (!NT_SUCCESS(ntStatus))
                {
                    DPF(D_TERSE, ("ConnectTopologies: PcRegisterPhysicalConnection(render) failed, 0x%x", ntStatus));
                }
                break;
'@ @'
                CUELET_TRACE_CHECKPOINT(
                    "CVA330 PcRegisterPhysicalConnection render",
                    ntStatus,
                    i,
                    PhysicalConnections[i].ulTopology,
                    PhysicalConnections[i].ulWave,
                    0, 0, 0, 0, 0);
                if (!NT_SUCCESS(ntStatus))
                {
                    DPF(D_TERSE, ("ConnectTopologies: PcRegisterPhysicalConnection(render) failed, 0x%x", ntStatus));
                }
                break;
'@
Replace-Required $common @'
                if (!NT_SUCCESS(ntStatus))
                {
                    DPF(D_TERSE, ("ConnectTopologies: PcRegisterPhysicalConnection(capture) failed, 0x%x", ntStatus));
                }
                break;
'@ @'
                CUELET_TRACE_CHECKPOINT(
                    "CVA331 PcRegisterPhysicalConnection capture",
                    ntStatus,
                    i,
                    PhysicalConnections[i].ulTopology,
                    PhysicalConnections[i].ulWave,
                    0, 0, 0, 0, 0);
                if (!NT_SUCCESS(ntStatus))
                {
                    DPF(D_TERSE, ("ConnectTopologies: PcRegisterPhysicalConnection(capture) failed, 0x%x", ntStatus));
                }
                break;
'@
Replace-Required $common @'
    InterlockedDecrement(&CAdapterCommon::m_AdapterInstances);
    ASSERT(CAdapterCommon::m_AdapterInstances == 0);
'@ @'
    LONG remainingAdapterInstances =
        InterlockedDecrement(&CAdapterCommon::m_AdapterInstances);
    ASSERT(remainingAdapterInstances == 0);
    UNREFERENCED_PARAMETER(remainingAdapterInstances);
'@
Replace-Required $common `
    '_Out_ _At_(AudioSymbolicLinkName->Buffer, __drv_allocatesMem(Mem)) PUNICODE_STRING AudioSymbolicLinkName' `
    '_Out_ PUNICODE_STRING AudioSymbolicLinkName'
Replace-Required $common @'
        pwstrKeyValueName = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, kvFullInfo->NameLength + sizeof(WCHAR)*2, MINADAPTER_POOLTAG);
        IF_TRUE_ACTION_JUMP(kvFullInfo == NULL, ntStatus = STATUS_INSUFFICIENT_RESOURCES, loop_exit);
'@ @'
        pwstrKeyValueName = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, kvFullInfo->NameLength + sizeof(WCHAR)*2, MINADAPTER_POOLTAG);
        IF_TRUE_ACTION_JUMP(pwstrKeyValueName == NULL, ntStatus = STATUS_INSUFFICIENT_RESOURCES, loop_exit);
'@
Replace-Required $common @'
        if (pwstrKeyValueName)
        {
            ExFreePoolWithTag(pwstrKeyValueName, MINADAPTER_POOLTAG);
        }
'@ @'
        if (pwstrKeyValueName)
        {
            ExFreePoolWithTag(pwstrKeyValueName, MINADAPTER_POOLTAG);
            pwstrKeyValueName = NULL;
        }
'@
Replace-Required $common @'
        pwstrKeyName = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, kBasicInfo->NameLength + sizeof(WCHAR), MINADAPTER_POOLTAG);
        IF_TRUE_ACTION_JUMP(kBasicInfo == NULL, ntStatus = STATUS_INSUFFICIENT_RESOURCES, loop_exit);
'@ @'
        pwstrKeyName = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, kBasicInfo->NameLength + sizeof(WCHAR), MINADAPTER_POOLTAG);
        IF_TRUE_ACTION_JUMP(pwstrKeyName == NULL, ntStatus = STATUS_INSUFFICIENT_RESOURCES, loop_exit);
'@
Replace-Required $common @'
        ntStatus = ZwOpenKey(&hCurrentSourceKey, KEY_READ, &hCurrentSourceKeyAttributes);
        IF_FAILED_ACTION_JUMP(ntStatus, ZwClose(hCurrentSourceKey), loop_exit);
'@ @'
        ntStatus = ZwOpenKey(&hCurrentSourceKey, KEY_READ, &hCurrentSourceKeyAttributes);
        IF_FAILED_JUMP(ntStatus, loop_exit);
'@
Replace-Required $common @'
        ntStatus = ZwCreateKey(&hNewDestinationKey, KEY_WRITE, &hNewDestinationKeyAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &ulDisposition);
        IF_FAILED_ACTION_JUMP(ntStatus, ZwClose(hCurrentSourceKey), loop_exit);
'@ @'
        ntStatus = ZwCreateKey(&hNewDestinationKey, KEY_WRITE, &hNewDestinationKeyAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, &ulDisposition);
        IF_FAILED_JUMP(ntStatus, loop_exit);
'@
Replace-Required $common @'
        if (pwstrKeyName)
        {
            ExFreePoolWithTag(pwstrKeyName, MINADAPTER_POOLTAG);
        }

        // Close the current source key
        if (hCurrentSourceKey)
        {
            ZwClose(hCurrentSourceKey);
        }

        // Close the new destination key
        if (hNewDestinationKey)
        {
            ZwClose(hNewDestinationKey);
        }
'@ @'
        if (pwstrKeyName)
        {
            ExFreePoolWithTag(pwstrKeyName, MINADAPTER_POOLTAG);
            pwstrKeyName = NULL;
        }

        // Close the current source key
        if (hCurrentSourceKey)
        {
            ZwClose(hCurrentSourceKey);
            hCurrentSourceKey = NULL;
        }

        // Close the new destination key
        if (hNewDestinationKey)
        {
            ZwClose(hNewDestinationKey);
            hNewDestinationKey = NULL;
        }
'@
Replace-Required $common @'
    ntStatus = IoRegisterDeviceInterface(
        GetPhysicalDeviceObject(),
        &KSCATEGORY_AUDIO,
        &referenceString,
        &TemplateSymbolicLinkName);

    // Open the template device interface's registry key path
'@ @'
    ntStatus = IoRegisterDeviceInterface(
        GetPhysicalDeviceObject(),
        &KSCATEGORY_AUDIO,
        &referenceString,
        &TemplateSymbolicLinkName);
    IF_FAILED_JUMP(ntStatus, Exit);

    // Open the template device interface's registry key path
'@

$saveData = Join-Path $sysvad 'savedata.cpp'
Replace-Required $saveData `
    'L"%s_%s_%d.wav"' `
    'L"%s_%s_%lu.wav"'
Replace-Required $saveData `
    '("[SaveFrameWorkerCallback], %d", pParam->ulFrameNo)' `
    '("[SaveFrameWorkerCallback], %lu", pParam->ulFrameNo)'
Replace-Required $saveData `
    '("[Frame %d is in use]", m_ulFrameIndex)' `
    '("[Frame %lu is in use]", m_ulFrameIndex)'

$commonProject = Join-Path $endpoints 'EndpointsCommon.vcxproj'
Replace-Required $commonProject @'
    <ClCompile>
'@ @'
    <ClCompile>
      <EnablePREfast Condition="'$(CueletRunDriverAnalysis)' == 'true'">true</EnablePREfast>
      <TreatWarningAsError Condition="'$(CueletRunDriverAnalysis)' == 'true'">false</TreatWarningAsError>
'@
Replace-Required $commonProject @'
    <ClCompile Include="minwavertstream.cpp" />
'@ @'
    <ClCompile Include="CueletAudioBridge.cpp" />
    <ClCompile Include="minwavertstream.cpp" />
'@
Replace-Required $commonProject `
    ';SYSVAD_BTH_BYPASS;SYSVAD_USB_SIDEBAND' `
    ';SYSVAD_BTH_BYPASS'

$minipairs = Join-Path $tablet 'minipairs.h'
Replace-Required $minipairs @'
PENDPOINT_MINIPAIR  g_RenderEndpoints[] = 
{
    &SpeakerMiniports,
    &SpeakerHpMiniports,
    &HdmiMiniports,
    &SpdifMiniports,
};
'@ @'
PENDPOINT_MINIPAIR  g_RenderEndpoints[] =
{
    &SpeakerMiniports,
};
'@
Replace-Required $minipairs @'
PENDPOINT_MINIPAIR  g_CaptureEndpoints[] = 
{
    &MicInMiniports,
    &MicArray1Miniports,
    &MicArray2Miniports,
    &MicArray3Miniports,
};
'@ @'
PENDPOINT_MINIPAIR  g_CaptureEndpoints[] =
{
    &MicInMiniports,
};
'@
Replace-Required $minipairs '    ENDPOINT_OFFLOAD_SUPPORTED,' '    ENDPOINT_NO_FLAGS,'

$speakerTopology = Join-Path $endpoints 'speakertoptable.h'
Replace-Required $speakerTopology @'
#define _SYSVAD_SPEAKERTOPTABLE_H_
'@ @'
#define _SYSVAD_SPEAKERTOPTABLE_H_

// {A02B8FB2-E6F7-4C0A-96E7-8F6B6329B653}
DEFINE_GUID(KSNODETYPE_CUELET_VIRTUAL_RENDER,
0xa02b8fb2, 0xe6f7, 0x4c0a, 0x96, 0xe7, 0x8f, 0x6b, 0x63, 0x29, 0xb6, 0x53);
'@
Replace-Required $speakerTopology @'
      &KSNODETYPE_SPEAKER,                              // Category
      NULL,                                             // Name
'@ @'
      &KSNODETYPE_SPEAKER,                              // Category
      &KSNODETYPE_CUELET_VIRTUAL_RENDER,                // Name
'@

function New-SingleStereoFormatTable {
    param([string]$Name)

    @"
static
KSDATAFORMAT_WAVEFORMATEXTENSIBLE $Name[] =
{
    {
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0, 0, 0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                2,
                48000,
                192000,
                4,
                16,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            16,
            KSAUDIO_SPEAKER_STEREO,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
        }
    }
};

"@
}

$singleAudioEngineFormat =
    New-SingleStereoFormatTable 'SpeakerAudioEngineSupportedDeviceFormats'
$singleHostFormat =
    New-SingleStereoFormatTable 'SpeakerHostPinSupportedDeviceFormats'
$singleOffloadFormat =
    New-SingleStereoFormatTable 'SpeakerOffloadPinSupportedDeviceFormats'
$speakerFormats = Join-Path $endpoints 'speakerwavtable.h'
Replace-RangeRequired $speakerFormats `
    @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerAudioEngineSupportedDeviceFormats[] =
'@ `
    @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
'@ `
    $singleAudioEngineFormat
Replace-RangeRequired $speakerFormats `
    @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
'@ `
    @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerOffloadPinSupportedDeviceFormats[] =
'@ `
    $singleHostFormat
Replace-RangeRequired $speakerFormats `
    @'
static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerOffloadPinSupportedDeviceFormats[] =
'@ `
    '// Supported modes (only on streaming pins).' `
    $singleOffloadFormat
Replace-Required $speakerFormats `
    'SpeakerHostPinSupportedDeviceFormats[3]' `
    'SpeakerHostPinSupportedDeviceFormats[0]'
Replace-Required $speakerFormats `
    'SpeakerOffloadPinSupportedDeviceFormats[1]' `
    'SpeakerOffloadPinSupportedDeviceFormats[0]'
Replace-Required $speakerFormats '#define SPEAKER_HOST_MIN_SAMPLE_RATE                24000' '#define SPEAKER_HOST_MIN_SAMPLE_RATE                48000'
Replace-Required $speakerFormats '#define SPEAKER_HOST_MAX_SAMPLE_RATE                96000' '#define SPEAKER_HOST_MAX_SAMPLE_RATE                48000'

$singleCaptureFormat = @'
static
KSDATAFORMAT_WAVEFORMATEXTENSIBLE MicInPinSupportedDeviceFormats[] =
{
    {
        {
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),
            0, 0, 0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        {
            {
                WAVE_FORMAT_EXTENSIBLE,
                2,
                48000,
                192000,
                4,
                16,
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
            },
            16,
            KSAUDIO_SPEAKER_STEREO,
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)
        }
    }
};

'@
$captureFormats = Join-Path $tablet 'micinwavtable.h'
Replace-RangeRequired $captureFormats `
    'static ' `
    '// Supported modes (only on streaming pins).' `
    $singleCaptureFormat
Replace-Required $captureFormats '#define MICIN_DEVICE_MAX_CHANNELS           1' '#define MICIN_DEVICE_MAX_CHANNELS           2'
Replace-Required $captureFormats '#define MICIN_MIN_SAMPLE_RATE               8000' '#define MICIN_MIN_SAMPLE_RATE               48000'
$captureModeStart = @'
static
MODE_AND_DEFAULT_FORMAT MicInPinSupportedDeviceModes[] =
'@
Replace-RangeRequired $captureFormats `
    $captureModeStart `
    '// The entries here must follow the same order as the filter''s pin' `
    @'
static
MODE_AND_DEFAULT_FORMAT MicInPinSupportedDeviceModes[] =
{
    {
        STATIC_AUDIO_SIGNALPROCESSINGMODE_RAW,
        &MicInPinSupportedDeviceFormats[0].DataFormat,
    },
    {
        STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,
        &MicInPinSupportedDeviceFormats[0].DataFormat,
    },
    {
        STATIC_AUDIO_SIGNALPROCESSINGMODE_COMMUNICATIONS,
        &MicInPinSupportedDeviceFormats[0].DataFormat,
    },
};

'@

$tabletProject = Join-Path $tablet 'TabletAudioSample.vcxproj'
Replace-Required $tabletProject @'
    <ClCompile>
'@ @'
    <ClCompile>
      <EnablePREfast Condition="'$(CueletRunDriverAnalysis)' == 'true'">true</EnablePREfast>
      <TreatWarningAsError Condition="'$(CueletRunDriverAnalysis)' == 'true'">false</TreatWarningAsError>
'@
Replace-Required $tabletProject @'
    <ClCompile Include="..\adapter.cpp" />
'@ @'
    <ClCompile Include="..\adapter.cpp">
      <WppEnabled Condition="'$(Configuration)' == 'Debug'">true</WppEnabled>
      <WppKernelMode Condition="'$(Configuration)' == 'Debug'">true</WppKernelMode>
      <WppTraceFunction Condition="'$(Configuration)' == 'Debug'">TraceEvents(LEVEL,FLAGS,MSG,...)</WppTraceFunction>
      <WppGenerateUsingTemplateFile Condition="'$(Configuration)' == 'Debug'">{km-default.tpl}*.tmh</WppGenerateUsingTemplateFile>
    </ClCompile>
'@
Replace-Required $tabletProject '<TargetName>TabletAudioSample</TargetName>' '<TargetName>CueletVirtualAudio</TargetName>'
Replace-Required $tabletProject `
    ';SYSVAD_BTH_BYPASS;SYSVAD_USB_SIDEBAND' `
    ''
Replace-Required $tabletProject @'
    <Inf Exclude="@(Inx)" Include="*.inx" />
'@ @'
    <Inf Include="CueletVirtualAudio.inx">
      <DateStamp>07/24/2026</DateStamp>
      <TimeStamp>20.43.0.721</TimeStamp>
    </Inf>
'@

$sourceInf = Join-Path $tablet 'ComponentizedAudioSample.inx'
$inf = Join-Path $tablet 'CueletVirtualAudio.inx'
Copy-Item -LiteralPath $sourceInf -Destination $inf
Replace-Required $inf 'CatalogFile = sysvad.cat' 'CatalogFile = CueletVirtualAudio.cat'
Replace-Required $inf '222="SYSVAD Driver Disk","",222' '222="Cuelet Virtual Audio Driver","",222'
Replace-Required $inf 'tabletaudiosample.sys=222' 'CueletVirtualAudio.sys=222'
Replace-Required $inf 'keywordDetectorContosoAdapter.dll=222' ''
Replace-Required $inf 'tabletaudiosample.sys=SignatureAttributes.DRM' 'CueletVirtualAudio.sys=SignatureAttributes.DRM'
Replace-Required $inf 'keyworddetectorcontosoadapter.dll=SignatureAttributes.PETrust' ''
Replace-Required $inf 'Root\sysvad_ComponentizedAudioSample' 'ROOT\CUELETVIRTUALAUDIO'
Replace-Required $inf 'tabletaudiosample.sys' 'CueletVirtualAudio.sys'
Replace-Required $inf ',KEYWORDDETECTORCONTOSOADAPTER.CopyList' ''
Replace-Required $inf ',KEYWORDDETECTORCONTOSOADAPTER.AddReg' ''
Replace-Required $inf 'sysvad_componentizedaudiosample' 'cuelet_virtual_audio'
Replace-Required $inf 'sysvad_ComponentizedAudioSample_Service_Inst' 'CueletVirtualAudio_Service_Inst'
Replace-Required $inf 'SYSVAD_SA_WdfSect' 'CueletVirtualAudio_WdfSect'
Replace-Required $inf 'KEYWORDDETECTORCONTOSOADAPTER.CopyList=13 ;' ''
Replace-RangeRequired $inf `
    '[KEYWORDDETECTORCONTOSOADAPTER.CopyList]' `
    '[SYSVAD_SA.AddReg]' `
    ''
Replace-RangeRequired $inf `
    '[KEYWORDDETECTORCONTOSOADAPTER.AddReg]' `
    ';======================================================' `
    ''

Replace-RangeRequired $inf `
    '[SYSVAD_SA.NT.Interfaces]' `
    '[SYSVAD_SA.NT.Services]' `
    @'
[SYSVAD_SA.NT.Interfaces]
; Cuelet render endpoint.
AddInterface=%KSCATEGORY_AUDIO%, %KSNAME_WaveSpeaker%, SYSVAD.I.WaveSpeaker
AddInterface=%KSCATEGORY_RENDER%, %KSNAME_WaveSpeaker%, SYSVAD.I.WaveSpeaker
AddInterface=%KSCATEGORY_REALTIME%, %KSNAME_WaveSpeaker%, SYSVAD.I.WaveSpeaker
AddInterface=%KSCATEGORY_AUDIO%, %KSNAME_TopologySpeaker%, SYSVAD.I.TopologySpeaker
AddInterface=%KSCATEGORY_TOPOLOGY%, %KSNAME_TopologySpeaker%, SYSVAD.I.TopologySpeaker

; Paired Cuelet capture endpoint.
AddInterface=%KSCATEGORY_AUDIO%, %KSNAME_WaveMicIn%, SYSVAD.I.WaveMicIn
AddInterface=%KSCATEGORY_REALTIME%, %KSNAME_WaveMicIn%, SYSVAD.I.WaveMicIn
AddInterface=%KSCATEGORY_CAPTURE%, %KSNAME_WaveMicIn%, SYSVAD.I.WaveMicIn
AddInterface=%KSCATEGORY_AUDIO%, %KSNAME_TopologyMicIn%, SYSVAD.I.TopologyMicIn
AddInterface=%KSCATEGORY_TOPOLOGY%, %KSNAME_TopologyMicIn%, SYSVAD.I.TopologyMicIn

'@
Replace-Required $inf @'
HKR,EP\0,%PKEY_AudioEndpoint_Association%,,%KSNODETYPE_ANY%
'@ @'
HKR,EP\0,%PKEY_AudioEndpoint_Association%,,%KSNODETYPE_ANY%
HKR,EP\0,%PKEY_Cuelet_PairingId%,,%CueletPairingId%
'@
Replace-Required $inf @'
HKR,%MEDIA_CATEGORIES%\%MicInCustomNameGUID%,Name,,%MicInCustomName%
'@ @'
HKR,%MEDIA_CATEGORIES%\%MicInCustomNameGUID%,Name,,%MicInCustomName%
HKR,%MEDIA_CATEGORIES%\%CueletRenderCustomNameGUID%,Name,,%CueletRenderName%
'@
Replace-Required $inf @'
PKEY_AudioEndpoint_Supports_EventDriven_Mode = "{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E},7"
'@ @'
PKEY_AudioEndpoint_Supports_EventDriven_Mode = "{1DA5D803-D492-4EDD-8C23-E0C0FFEE7F0E},7"
PKEY_Cuelet_PairingId = "{1A7B44F5-2C93-48F5-A18B-46399D69E13F},2"
CueletPairingId = "{8B9D3BB9-8C4E-4EF5-94D5-4BE741D4D892}"
'@
Replace-Required $inf @'
MicInCustomNameGUID = {d48deb08-fd1c-4d1e-b821-9064d49ae96e}
'@ @'
MicInCustomNameGUID = {d48deb08-fd1c-4d1e-b821-9064d49ae96e}
CueletRenderCustomNameGUID = {a02b8fb2-e6f7-4c0a-96e7-8f6b6329b653}
'@
Replace-Required $inf 'ProviderName = "TODO-Set-Provider"' 'ProviderName = "Cuelet"'
Replace-Required $inf 'MfgName      = "TODO-Set-Manufacturer"' 'MfgName      = "Cuelet"'
Replace-Required $inf 'MsCopyRight  = "TODO-Set-Copyright"' 'MsCopyRight  = "Copyright (c) Cuelet contributors"'
Replace-Required $inf 'SYSVAD_SA.DeviceDesc="Virtual Audio Device (WDM) - Tablet Sample"' 'SYSVAD_SA.DeviceDesc="Cuelet Virtual Audio Device"'
Replace-Required $inf 'SYSVAD_ComponentizedAudioSample.SvcDesc="Virtual Audio Device (WDM) - Tablet Sample Driver"' 'SYSVAD_ComponentizedAudioSample.SvcDesc="Cuelet Virtual Audio Driver"'
Replace-Required $inf 'SYSVAD.WaveSpeaker.szPname="SYSVAD Wave Speaker"' 'SYSVAD.WaveSpeaker.szPname="Cuelet Virtual Microphone Input"'
Replace-Required $inf 'SYSVAD.TopologySpeaker.szPname="SYSVAD Topology Speaker"' 'SYSVAD.TopologySpeaker.szPname="Cuelet Virtual Microphone Input"'
Replace-Required $inf 'SYSVAD.WaveMicIn.szPname="SYSVAD Wave Microphone Headphone"' 'SYSVAD.WaveMicIn.szPname="Cuelet Virtual Microphone"'
Replace-Required $inf 'SYSVAD.TopologyMicIn.szPname="SYSVAD Topology Microphone Headphone"' 'SYSVAD.TopologyMicIn.szPname="Cuelet Virtual Microphone"'
Replace-Required $inf 'MicInCustomName= "External Microphone Headphone"' @'
MicInCustomName= "Cuelet Virtual Microphone"
CueletRenderName= "Cuelet Virtual Microphone Input"
'@
Replace-Required $inf 'DriverVer   = 02/22/2016, 1.0.0.1' 'DriverVer   = 07/24/2026, 20.43.0.721'

Write-Host "Prepared Cuelet virtual-audio source from Microsoft SysVAD $upstreamCommit"
Write-Host "Generated source: $sysvad"
