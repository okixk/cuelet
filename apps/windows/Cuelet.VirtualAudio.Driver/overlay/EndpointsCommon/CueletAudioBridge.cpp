#include <sysvad.h>
#include "CueletAudioBridge.h"
#include "CueletAudioFifoCore.h"
#include "CueletStartupTrace.h"

namespace
{
    constexpr ULONG RingStorageBytes = 512 * 1024;
    constexpr ULONG MaximumReaders = 32;
    constexpr ULONG TargetBufferMilliseconds = 30;

    struct FormatSignature
    {
        USHORT formatTag{};
        USHORT channels{};
        ULONG samplesPerSecond{};
        ULONG averageBytesPerSecond{};
        USHORT blockAlign{};
        USHORT bitsPerSample{};
        USHORT validBitsPerSample{};
        ULONG channelMask{};
        GUID subFormat{};
    };

    struct Reader
    {
        const void* key{};
        cuelet::audio_fifo::ReaderCursor cursor{};
    };

    struct Diagnostics
    {
        ULONGLONG callbackEntries{};
        ULONGLONG callbackExits{};
        ULONGLONG publishEntries{};
        ULONGLONG readEntries{};
        ULONGLONG rejectedOperations{};
        ULONGLONG invalidCopyPlans{};
        ULONGLONG underflows{};
        ULONGLONG overflows{};
        ULONG activeWriters{};
        ULONG activeReaders{};
        ULONG registeredReaders{};
        ULONG peakRegisteredReaders{};
    };

    KSPIN_LOCK g_lock;
    BYTE g_ring[RingStorageBytes]{};
    Reader g_readers[MaximumReaders]{};
    FormatSignature g_format{};
    cuelet::audio_fifo::FifoCore g_fifo{};
    Diagnostics g_diagnostics{};
    BOOLEAN g_hasFormat{};
    BOOLEAN g_initialized{};
    BOOLEAN g_deviceActive{};
    ULONGLONG g_deviceEpoch{};

    FormatSignature signatureOf(_In_ const WAVEFORMATEX* format)
    {
        FormatSignature result{};
        if (format == nullptr)
        {
            return result;
        }

        result.formatTag = format->wFormatTag;
        result.channels = format->nChannels;
        result.samplesPerSecond = format->nSamplesPerSec;
        result.averageBytesPerSecond = format->nAvgBytesPerSec;
        result.blockAlign = format->nBlockAlign;
        result.bitsPerSample = format->wBitsPerSample;
        result.validBitsPerSample = format->wBitsPerSample;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >=
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            const auto extended =
                reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            result.validBitsPerSample =
                extended->Samples.wValidBitsPerSample;
            result.channelMask = extended->dwChannelMask;
            result.subFormat = extended->SubFormat;
        }
        return result;
    }

    bool formatsMatch(
        const FormatSignature& left,
        const FormatSignature& right)
    {
        return
            left.formatTag == right.formatTag &&
            left.channels == right.channels &&
            left.samplesPerSecond == right.samplesPerSecond &&
            left.averageBytesPerSecond ==
                right.averageBytesPerSecond &&
            left.blockAlign == right.blockAlign &&
            left.bitsPerSample == right.bitsPerSample &&
            left.validBitsPerSample ==
                right.validBitsPerSample &&
            left.channelMask == right.channelMask &&
            IsEqualGUIDAligned(left.subFormat, right.subFormat);
    }

    bool isPcmFormat(const FormatSignature& format)
    {
        if (format.formatTag == WAVE_FORMAT_PCM)
        {
            return true;
        }
        return
            format.formatTag == WAVE_FORMAT_EXTENSIBLE &&
            IsEqualGUIDAligned(
                format.subFormat,
                KSDATAFORMAT_SUBTYPE_PCM);
    }

    bool isValidFormat(const FormatSignature& format)
    {
        if (!isPcmFormat(format) ||
            format.channels == 0 ||
            format.samplesPerSecond == 0 ||
            format.blockAlign == 0 ||
            format.bitsPerSample == 0 ||
            format.bitsPerSample % 8 != 0 ||
            format.validBitsPerSample == 0 ||
            format.validBitsPerSample > format.bitsPerSample)
        {
            return false;
        }

        const ULONGLONG bytesPerSample =
            format.bitsPerSample / 8;
        const ULONGLONG calculatedBlockAlign =
            static_cast<ULONGLONG>(format.channels) *
                bytesPerSample;
        const ULONGLONG calculatedAverageBytes =
            static_cast<ULONGLONG>(
                format.samplesPerSecond) *
                format.blockAlign;
        return
            calculatedBlockAlign <= MAXUSHORT &&
            calculatedBlockAlign == format.blockAlign &&
            calculatedAverageBytes <= MAXULONG &&
            calculatedAverageBytes ==
                format.averageBytesPerSecond;
    }

    cuelet::audio_fifo::Bytes targetBufferBytes(
        const FormatSignature& format)
    {
        const ULONGLONG numerator =
            static_cast<ULONGLONG>(
                format.averageBytesPerSecond) *
                TargetBufferMilliseconds;
        return {numerator / 1000};
    }

    Reader* findOrCreateReader(_In_ const void* key)
    {
        Reader* empty = nullptr;
        for (auto& reader : g_readers)
        {
            if (reader.key == key)
            {
                return &reader;
            }
            if (reader.key == nullptr && empty == nullptr)
            {
                empty = &reader;
            }
        }

        if (empty != nullptr)
        {
            empty->key = key;
            empty->cursor = {};
            ++g_diagnostics.registeredReaders;
            if (g_diagnostics.registeredReaders >
                g_diagnostics.peakRegisteredReaders)
            {
                g_diagnostics.peakRegisteredReaders =
                    g_diagnostics.registeredReaders;
            }
        }
        return empty;
    }

    bool copyFitsStorage(
        const cuelet::audio_fifo::CopyPlan& plan)
    {
        return
            plan.ringOffset.value < RingStorageBytes &&
            plan.firstBytes.value <=
                RingStorageBytes - plan.ringOffset.value &&
            plan.secondBytes.value <= RingStorageBytes;
    }

    bool copyIntoRing(
        const cuelet::audio_fifo::CopyPlan& plan,
        _In_reads_bytes_(
            plan.firstBytes.value +
            plan.secondBytes.value) const BYTE* source)
    {
        const bool valid = copyFitsStorage(plan);
        NT_ASSERT(valid);
        if (!valid || source == nullptr)
        {
            return false;
        }

        const SIZE_T first =
            static_cast<SIZE_T>(plan.firstBytes.value);
        const SIZE_T second =
            static_cast<SIZE_T>(plan.secondBytes.value);
        const SIZE_T offset =
            static_cast<SIZE_T>(plan.ringOffset.value);
        RtlCopyMemory(g_ring + offset, source, first);
        if (second != 0)
        {
            RtlCopyMemory(g_ring, source + first, second);
        }
        return true;
    }

    bool copyFromRing(
        const cuelet::audio_fifo::CopyPlan& plan,
        _Out_writes_bytes_(
            plan.firstBytes.value +
            plan.secondBytes.value) BYTE* destination)
    {
        const bool valid = copyFitsStorage(plan);
        NT_ASSERT(valid);
        if (!valid || destination == nullptr)
        {
            return false;
        }

        const SIZE_T first =
            static_cast<SIZE_T>(plan.firstBytes.value);
        const SIZE_T second =
            static_cast<SIZE_T>(plan.secondBytes.value);
        const SIZE_T offset =
            static_cast<SIZE_T>(plan.ringOffset.value);
        RtlCopyMemory(destination, g_ring + offset, first);
        if (second != 0)
        {
            RtlCopyMemory(destination + first, g_ring, second);
        }
        return true;
    }

    void countRejected(
        cuelet::audio_fifo::TransferStatus status)
    {
        if (status != cuelet::audio_fifo::TransferStatus::Ready)
        {
            ++g_diagnostics.rejectedOperations;
        }
    }
}

void CueletAudioBridgeInitialize()
{
    KeInitializeSpinLock(&g_lock);
    RtlZeroMemory(g_ring, sizeof(g_ring));
    RtlZeroMemory(g_readers, sizeof(g_readers));
    RtlZeroMemory(&g_format, sizeof(g_format));
    RtlZeroMemory(&g_diagnostics, sizeof(g_diagnostics));
    g_fifo = {};
    g_hasFormat = FALSE;
    g_deviceActive = FALSE;
    g_deviceEpoch = 0;
    g_initialized = TRUE;
    DPF(
        D_TERSE,
        ("Cuelet bridge created: storage=%lu", RingStorageBytes));
    CUELET_TRACE_CHECKPOINT(
        "CVA400 bridge/FIFO initialized",
        STATUS_SUCCESS,
        RingStorageBytes,
        MaximumReaders,
        TargetBufferMilliseconds,
        g_deviceEpoch,
        g_initialized,
        g_deviceActive,
        g_fifo.CurrentGeneration().value,
        g_fifo.WritePosition().value);
}

void CueletAudioBridgePrepareForDeviceStart()
{
    if (!g_initialized)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_lock, &oldIrql);
    NT_ASSERT(g_diagnostics.activeReaders == 0);
    NT_ASSERT(g_diagnostics.activeWriters == 0);
    const ULONG staleReaders = g_diagnostics.registeredReaders;
    RtlZeroMemory(g_ring, sizeof(g_ring));
    RtlZeroMemory(g_readers, sizeof(g_readers));
    RtlZeroMemory(&g_format, sizeof(g_format));
    RtlZeroMemory(&g_diagnostics, sizeof(g_diagnostics));
    g_fifo = {};
    g_hasFormat = FALSE;
    g_deviceActive = TRUE;
    ++g_deviceEpoch;
    if (g_deviceEpoch == 0)
    {
        g_deviceEpoch = 1;
    }
    const ULONGLONG epoch = g_deviceEpoch;
    KeReleaseSpinLock(&g_lock, oldIrql);
    UNREFERENCED_PARAMETER(epoch);
    UNREFERENCED_PARAMETER(staleReaders);

    DPF(
        D_TERSE,
        (
            "Cuelet bridge device start: epoch=%llu "
            "staleReaders=%lu",
            epoch,
            staleReaders));
    CUELET_TRACE_CHECKPOINT(
        "CVA401 bridge lifecycle epoch armed",
        STATUS_SUCCESS,
        epoch,
        staleReaders,
        RingStorageBytes,
        0,
        0, 0, 0, 0);
}

void CueletAudioBridgeBeginTeardown()
{
    if (!g_initialized)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_lock, &oldIrql);
    const BOOLEAN wasActive = g_deviceActive;
    g_deviceActive = FALSE;
    if (!g_fifo.IsTearingDown())
    {
        g_fifo.BeginTeardown();
    }
    const Diagnostics diagnostics = g_diagnostics;
    const ULONGLONG epoch = g_deviceEpoch;
    const auto generation =
        g_fifo.CurrentGeneration().value;
    const auto writePosition =
        g_fifo.WritePosition().value;
    KeReleaseSpinLock(&g_lock, oldIrql);
    UNREFERENCED_PARAMETER(wasActive);
    UNREFERENCED_PARAMETER(epoch);
    UNREFERENCED_PARAMETER(generation);
    UNREFERENCED_PARAMETER(writePosition);
    UNREFERENCED_PARAMETER(diagnostics);

    DPF(
        D_TERSE,
        (
            "Cuelet bridge teardown: epoch=%llu transitioned=%u "
            "generation=%llu write=%llu "
            "callbacks=%llu/%llu readers=%lu peak=%lu "
            "active=%lu/%lu rejected=%llu underflows=%llu "
            "overflows=%llu invalidPlans=%llu",
            epoch,
            wasActive,
            generation,
            writePosition,
            diagnostics.callbackEntries,
            diagnostics.callbackExits,
            diagnostics.registeredReaders,
            diagnostics.peakRegisteredReaders,
            diagnostics.activeReaders,
            diagnostics.activeWriters,
            diagnostics.rejectedOperations,
            diagnostics.underflows,
            diagnostics.overflows,
            diagnostics.invalidCopyPlans));
    CUELET_TRACE_CHECKPOINT(
        "CVA409 bridge teardown",
        STATUS_SUCCESS,
        epoch,
        wasActive,
        generation,
        writePosition,
        diagnostics.registeredReaders,
        diagnostics.activeReaders,
        diagnostics.activeWriters,
        diagnostics.invalidCopyPlans);
}

void CueletAudioBridgePublish(
    _In_reads_bytes_(byteCount) const BYTE* source,
    _In_ ULONG byteCount,
    _In_opt_ const WAVEFORMATEX* format)
{
    if (!g_initialized || source == nullptr ||
        format == nullptr || byteCount == 0)
    {
        return;
    }

    const FormatSignature incoming = signatureOf(format);
    if (!isValidFormat(incoming) ||
        byteCount % incoming.blockAlign != 0)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_lock, &oldIrql);
    ++g_diagnostics.callbackEntries;
    ++g_diagnostics.publishEntries;
    ++g_diagnostics.activeWriters;

    if (!g_deviceActive || g_fifo.IsTearingDown())
    {
        ++g_diagnostics.rejectedOperations;
        --g_diagnostics.activeWriters;
        ++g_diagnostics.callbackExits;
        KeReleaseSpinLock(&g_lock, oldIrql);
        return;
    }

    if (!g_hasFormat || !formatsMatch(g_format, incoming))
    {
        const bool configured = g_fifo.Configure(
            {RingStorageBytes},
            {incoming.blockAlign},
            targetBufferBytes(incoming));
        if (!configured)
        {
            ++g_diagnostics.rejectedOperations;
            --g_diagnostics.activeWriters;
            ++g_diagnostics.callbackExits;
            KeReleaseSpinLock(&g_lock, oldIrql);
            return;
        }

        g_format = incoming;
        g_hasFormat = TRUE;
        RtlZeroMemory(g_ring, sizeof(g_ring));
    }

    const auto plan = g_fifo.PlanPublish({byteCount});
    countRejected(plan.status);
    if (plan.status ==
        cuelet::audio_fifo::TransferStatus::Ready)
    {
        const bool validPlan =
            cuelet::audio_fifo::IsCopyPlanValid(
                plan,
                {byteCount},
                g_fifo.CurrentGeometry());
        NT_ASSERT(validPlan);
        if (!validPlan ||
            !copyIntoRing(
                plan,
                source +
                    static_cast<SIZE_T>(
                        plan.sourceOffset.value)))
        {
            ++g_diagnostics.invalidCopyPlans;
            ++g_diagnostics.rejectedOperations;
        }
    }

    --g_diagnostics.activeWriters;
    ++g_diagnostics.callbackExits;
    KeReleaseSpinLock(&g_lock, oldIrql);
}

void CueletAudioBridgeRead(
    _In_ const void* readerKey,
    _Out_writes_bytes_(byteCount) BYTE* destination,
    _In_ ULONG byteCount,
    _In_opt_ const WAVEFORMATEX* format)
{
    if (destination == nullptr || byteCount == 0)
    {
        return;
    }

    RtlZeroMemory(destination, byteCount);
    if (!g_initialized || readerKey == nullptr ||
        format == nullptr)
    {
        return;
    }

    const FormatSignature requested = signatureOf(format);
    if (!isValidFormat(requested) ||
        byteCount % requested.blockAlign != 0)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_lock, &oldIrql);
    ++g_diagnostics.callbackEntries;
    ++g_diagnostics.readEntries;
    ++g_diagnostics.activeReaders;

    if (!g_hasFormat ||
        !formatsMatch(g_format, requested) ||
        !g_deviceActive ||
        g_fifo.IsTearingDown())
    {
        ++g_diagnostics.rejectedOperations;
        --g_diagnostics.activeReaders;
        ++g_diagnostics.callbackExits;
        KeReleaseSpinLock(&g_lock, oldIrql);
        return;
    }

    Reader* const reader = findOrCreateReader(readerKey);
    if (reader == nullptr)
    {
        ++g_diagnostics.rejectedOperations;
        --g_diagnostics.activeReaders;
        ++g_diagnostics.callbackExits;
        KeReleaseSpinLock(&g_lock, oldIrql);
        return;
    }

    const auto plan =
        g_fifo.PlanRead(reader->cursor, {byteCount});
    if (plan.overflowRecovered)
    {
        ++g_diagnostics.overflows;
    }
    if (plan.status ==
            cuelet::audio_fifo::TransferStatus::Underflow ||
        plan.status ==
            cuelet::audio_fifo::TransferStatus::StartupReserve)
    {
        ++g_diagnostics.underflows;
    }
    else if (
        plan.status !=
            cuelet::audio_fifo::TransferStatus::Ready &&
        plan.status !=
            cuelet::audio_fifo::TransferStatus::ReaderResynchronized)
    {
        ++g_diagnostics.rejectedOperations;
    }

    if (plan.status ==
        cuelet::audio_fifo::TransferStatus::Ready)
    {
        const bool validPlan =
            cuelet::audio_fifo::IsCopyPlanValid(
                plan,
                {byteCount},
                g_fifo.CurrentGeometry());
        NT_ASSERT(validPlan);
        if (!validPlan || !copyFromRing(plan, destination))
        {
            ++g_diagnostics.invalidCopyPlans;
            ++g_diagnostics.rejectedOperations;
            RtlZeroMemory(destination, byteCount);
        }
    }

    --g_diagnostics.activeReaders;
    ++g_diagnostics.callbackExits;
    KeReleaseSpinLock(&g_lock, oldIrql);
}

void CueletAudioBridgeReleaseReader(_In_ const void* readerKey)
{
    if (!g_initialized || readerKey == nullptr)
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&g_lock, &oldIrql);
    for (auto& reader : g_readers)
    {
        if (reader.key == readerKey)
        {
            reader = {};
            NT_ASSERT(g_diagnostics.registeredReaders != 0);
            if (g_diagnostics.registeredReaders != 0)
            {
                --g_diagnostics.registeredReaders;
            }
            break;
        }
    }
    KeReleaseSpinLock(&g_lock, oldIrql);
}
