#include "CueletVirtualAudioDiagnostics.h"
#include "CueletVirtualAudioCore.h"

#ifdef CUELET_AUDIO_DIAGNOSTICS

#include <stdatomic.h>
#include <string.h>

typedef struct CueletDiagnosticPayload {
    uint64_t eventKind;
    CueletDiagnosticRecordData data;
} CueletDiagnosticPayload;

enum {
    kCueletDiagnosticPayloadWordCount =
        sizeof(CueletDiagnosticPayload) / sizeof(uint64_t),
};

_Static_assert(
    sizeof(CueletDiagnosticPayload) % sizeof(uint64_t) == 0,
    "diagnostic payload must be representable as atomic words");
_Static_assert(
    ATOMIC_LLONG_LOCK_FREE == 2,
    "diagnostic event publication requires lock-free 64-bit atomics");

typedef union CueletDiagnosticPayloadWords {
    CueletDiagnosticPayload payload;
    uint64_t words[kCueletDiagnosticPayloadWordCount];
} CueletDiagnosticPayloadWords;

typedef struct CueletDiagnosticSlot {
    _Atomic uint64_t sequence;
    _Atomic uint64_t words[kCueletDiagnosticPayloadWordCount];
} CueletDiagnosticSlot;

typedef struct CueletDiagnosticAtomicCounters {
    _Atomic uint64_t writeMixCallCount;
    _Atomic uint64_t writeMixNonzeroCallCount;
    _Atomic uint64_t writeMixZeroCallCount;
    _Atomic uint64_t writeRequestedFrames;
    _Atomic uint64_t writeAcceptedFrames;
    _Atomic uint64_t writeRejectedFrames;
    _Atomic uint64_t readInputCallCount;
    _Atomic uint64_t readRequestedFrames;
    _Atomic uint64_t readValidFrames;
    _Atomic uint64_t readZeroFilledFrames;
    _Atomic uint64_t ringResetCount;
    _Atomic uint64_t generationChangeCount;
    _Atomic uint64_t startIOCount;
    _Atomic uint64_t stopIOCount;
    _Atomic uint64_t sampleRateChangeCount;
    _Atomic uint64_t streamActivationChangeCount;
    _Atomic uint64_t propertySnapshotCount;
    _Atomic uint64_t writeResultCounts[CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT];
    _Atomic uint64_t readResultCounts[CUELET_DIAGNOSTIC_READ_RESULT_COUNT];
    _Atomic uint64_t timelineResultCounts[CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT];
    _Atomic uint64_t writeInputFrames;
    _Atomic uint64_t writeValidatedFrames;
    _Atomic uint64_t writeStoredPayloadFrames;
    _Atomic uint64_t writePublishedTagFrames;
    _Atomic uint64_t writePublicationFailures;
    _Atomic uint64_t readFailureFrameCounts[kCueletDiagnosticReadFailureCodeCount];
    _Atomic uint64_t readPartialValidFrames;
    _Atomic uint64_t readOKCalls;
    _Atomic uint64_t readPartialCalls;
    _Atomic uint64_t readAllUnavailableCalls;
    _Atomic uint64_t readMappingInvalidCalls;
    _Atomic uint64_t readMappedCalls;
    _Atomic uint64_t readGenerationResolvedCalls;
    _Atomic uint64_t readPreRingAcceptedCalls;
    _Atomic uint64_t readRingLookupCalls;
    _Atomic uint64_t readRingLookupFrames;
    _Atomic uint64_t readMappedButNoGenerationCalls;
    _Atomic uint64_t readGenerationButNoRingCalls;
    _Atomic uint64_t readRingLookupUnavailableCalls;
} CueletDiagnosticAtomicCounters;

#define CUELET_ATOMIC_WORD_COUNT(type) (sizeof(type) / sizeof(uint64_t))

typedef union CueletDiagnosticCriticalWords {
    CueletDiagnosticCriticalEvent value;
    uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticCriticalEvent)];
} CueletDiagnosticCriticalWords;

typedef struct CueletDiagnosticCriticalSlot {
    _Atomic uint64_t sequence;
    _Atomic uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticCriticalEvent)];
} CueletDiagnosticCriticalSlot;

typedef union CueletDiagnosticFailureWords {
    CueletDiagnosticReadFailureSummary value;
    uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadFailureSummary)];
} CueletDiagnosticFailureWords;

typedef struct CueletDiagnosticFailureSlot {
    _Atomic uint64_t sequence;
    _Atomic uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadFailureSummary)];
} CueletDiagnosticFailureSlot;

typedef union CueletDiagnosticWriteRangeWords {
    CueletDiagnosticWriteRangeSummary value;
    uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticWriteRangeSummary)];
} CueletDiagnosticWriteRangeWords;

typedef struct CueletDiagnosticWriteRangeSlot {
    _Atomic uint64_t sequence;
    _Atomic uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticWriteRangeSummary)];
} CueletDiagnosticWriteRangeSlot;

typedef union CueletDiagnosticReadRangeWords {
    CueletDiagnosticReadRangeSummary value;
    uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadRangeSummary)];
} CueletDiagnosticReadRangeWords;

typedef struct CueletDiagnosticReadRangeSlot {
    _Atomic uint64_t sequence;
    _Atomic uint64_t words[CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadRangeSummary)];
} CueletDiagnosticReadRangeSlot;

_Static_assert(sizeof(CueletDiagnosticCriticalEvent) % sizeof(uint64_t) == 0,
    "critical diagnostic events require complete atomic words");
_Static_assert(sizeof(CueletDiagnosticReadFailureSummary) % sizeof(uint64_t) == 0,
    "failure summaries require complete atomic words");
_Static_assert(sizeof(CueletDiagnosticWriteRangeSummary) % sizeof(uint64_t) == 0,
    "write summaries require complete atomic words");
_Static_assert(sizeof(CueletDiagnosticReadRangeSummary) % sizeof(uint64_t) == 0,
    "read summaries require complete atomic words");

static CueletDiagnosticSlot gEvents[CUELET_DIAGNOSTIC_EVENT_CAPACITY];
static CueletDiagnosticAtomicCounters gCounters;
static _Atomic uint64_t gNextSequence;
static _Atomic uint64_t gClearSequence;
static _Atomic uint64_t gOverwriteCount;
static _Atomic uint64_t gOverwriteBaseline;
static _Atomic uint64_t gStateToken;
static _Atomic uint64_t gRingToken;
static CueletDiagnosticCriticalSlot
    gCriticalEvents[CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY];
static _Atomic uint32_t gCriticalWriteCount;
static _Atomic uint32_t gCriticalReadCount;
static CueletDiagnosticFailureSlot gFirstReadFailure;
static CueletDiagnosticFailureSlot gLastReadFailures[4];
static CueletDiagnosticWriteRangeSlot gLastAcceptedWrites[4];
static CueletDiagnosticWriteRangeSlot gLastPublishedWrites[4];
static CueletDiagnosticReadRangeSlot gLastReads[4];

static void CueletDiagnosticResetCounters(void)
{
#define CUELET_CLEAR_COUNTER(name) \
    atomic_store_explicit(&gCounters.name, 0, memory_order_relaxed)
    CUELET_CLEAR_COUNTER(writeMixCallCount);
    CUELET_CLEAR_COUNTER(writeMixNonzeroCallCount);
    CUELET_CLEAR_COUNTER(writeMixZeroCallCount);
    CUELET_CLEAR_COUNTER(writeRequestedFrames);
    CUELET_CLEAR_COUNTER(writeAcceptedFrames);
    CUELET_CLEAR_COUNTER(writeRejectedFrames);
    CUELET_CLEAR_COUNTER(readInputCallCount);
    CUELET_CLEAR_COUNTER(readRequestedFrames);
    CUELET_CLEAR_COUNTER(readValidFrames);
    CUELET_CLEAR_COUNTER(readZeroFilledFrames);
    CUELET_CLEAR_COUNTER(ringResetCount);
    CUELET_CLEAR_COUNTER(generationChangeCount);
    CUELET_CLEAR_COUNTER(startIOCount);
    CUELET_CLEAR_COUNTER(stopIOCount);
    CUELET_CLEAR_COUNTER(sampleRateChangeCount);
    CUELET_CLEAR_COUNTER(streamActivationChangeCount);
    CUELET_CLEAR_COUNTER(propertySnapshotCount);
    CUELET_CLEAR_COUNTER(writeInputFrames);
    CUELET_CLEAR_COUNTER(writeValidatedFrames);
    CUELET_CLEAR_COUNTER(writeStoredPayloadFrames);
    CUELET_CLEAR_COUNTER(writePublishedTagFrames);
    CUELET_CLEAR_COUNTER(writePublicationFailures);
    CUELET_CLEAR_COUNTER(readPartialValidFrames);
    CUELET_CLEAR_COUNTER(readOKCalls);
    CUELET_CLEAR_COUNTER(readPartialCalls);
    CUELET_CLEAR_COUNTER(readAllUnavailableCalls);
    CUELET_CLEAR_COUNTER(readMappingInvalidCalls);
    CUELET_CLEAR_COUNTER(readMappedCalls);
    CUELET_CLEAR_COUNTER(readGenerationResolvedCalls);
    CUELET_CLEAR_COUNTER(readPreRingAcceptedCalls);
    CUELET_CLEAR_COUNTER(readRingLookupCalls);
    CUELET_CLEAR_COUNTER(readRingLookupFrames);
    CUELET_CLEAR_COUNTER(readMappedButNoGenerationCalls);
    CUELET_CLEAR_COUNTER(readGenerationButNoRingCalls);
    CUELET_CLEAR_COUNTER(readRingLookupUnavailableCalls);
#undef CUELET_CLEAR_COUNTER
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT; ++index) {
        atomic_store_explicit(
            &gCounters.writeResultCounts[index], 0, memory_order_relaxed);
    }
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_READ_RESULT_COUNT; ++index) {
        atomic_store_explicit(
            &gCounters.readResultCounts[index], 0, memory_order_relaxed);
    }
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT; ++index) {
        atomic_store_explicit(
            &gCounters.timelineResultCounts[index], 0, memory_order_relaxed);
    }
    for (uint32_t index = 0;
         index < kCueletDiagnosticReadFailureCodeCount; ++index) {
        atomic_store_explicit(
            &gCounters.readFailureFrameCounts[index], 0, memory_order_relaxed);
    }
}

static void CueletDiagnosticResetSummaries(void)
{
    atomic_store_explicit(&gCriticalWriteCount, 0, memory_order_relaxed);
    atomic_store_explicit(&gCriticalReadCount, 0, memory_order_relaxed);
    for (uint32_t index = 0;
         index < CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY; ++index) {
        atomic_store_explicit(
            &gCriticalEvents[index].sequence, UINT64_MAX, memory_order_relaxed);
    }
    atomic_store_explicit(
        &gFirstReadFailure.sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t index = 0; index < 4; ++index) {
        atomic_store_explicit(
            &gLastReadFailures[index].sequence, UINT64_MAX, memory_order_relaxed);
        atomic_store_explicit(
            &gLastAcceptedWrites[index].sequence, UINT64_MAX, memory_order_relaxed);
        atomic_store_explicit(
            &gLastPublishedWrites[index].sequence, UINT64_MAX, memory_order_relaxed);
        atomic_store_explicit(
            &gLastReads[index].sequence, UINT64_MAX, memory_order_relaxed);
    }
}

void CueletDiagnosticInitialize(void)
{
    atomic_store_explicit(&gNextSequence, 0, memory_order_relaxed);
    atomic_store_explicit(&gClearSequence, 0, memory_order_relaxed);
    atomic_store_explicit(&gOverwriteCount, 0, memory_order_relaxed);
    atomic_store_explicit(&gOverwriteBaseline, 0, memory_order_relaxed);
    atomic_store_explicit(&gStateToken, 0, memory_order_relaxed);
    atomic_store_explicit(&gRingToken, 0, memory_order_relaxed);
    CueletDiagnosticResetCounters();
    CueletDiagnosticResetSummaries();
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_EVENT_CAPACITY; ++index) {
        atomic_store_explicit(
            &gEvents[index].sequence, UINT64_MAX, memory_order_relaxed);
        for (uint32_t word = 0; word < kCueletDiagnosticPayloadWordCount; ++word) {
            atomic_store_explicit(
                &gEvents[index].words[word], 0, memory_order_relaxed);
        }
    }
}

void CueletDiagnosticSetStateTokens(uint64_t stateToken, uint64_t ringToken)
{
    atomic_store_explicit(&gStateToken, stateToken, memory_order_release);
    atomic_store_explicit(&gRingToken, ringToken, memory_order_release);
}

void CueletDiagnosticClear(void)
{
    const uint64_t next = atomic_load_explicit(&gNextSequence, memory_order_acquire);
    atomic_store_explicit(&gClearSequence, next, memory_order_release);
    atomic_store_explicit(
        &gOverwriteBaseline,
        atomic_load_explicit(&gOverwriteCount, memory_order_acquire),
        memory_order_release);
    CueletDiagnosticResetCounters();
    CueletDiagnosticResetSummaries();
}

static CueletDiagnosticReadFailureCode CueletFailureCodeForRingStatus(
    uint32_t status)
{
    switch ((CueletRingReadStatus)status) {
    case kCueletRingReadNotYetWritten:
        return kCueletDiagnosticReadFailureNotYetWritten;
    case kCueletRingReadOverwritten:
        return kCueletDiagnosticReadFailureOverwritten;
    case kCueletRingReadGenerationMismatch:
        return kCueletDiagnosticReadFailureGenerationMismatch;
    case kCueletRingReadAbsoluteFrameMismatch:
        return kCueletDiagnosticReadFailureAbsoluteTagMismatch;
    case kCueletRingReadUnpublished:
        return kCueletDiagnosticReadFailureUnpublished;
    case kCueletRingReadSampleRateReset:
        return kCueletDiagnosticReadFailureSampleRateReset;
    case kCueletRingReadTimelineUninitialized:
        return kCueletDiagnosticReadFailureTimelineUninitialized;
    case kCueletRingReadMappingInvalid:
        return kCueletDiagnosticReadFailureMappingInvalid;
    case kCueletRingReadStreamInactive:
        return kCueletDiagnosticReadFailureStreamInactive;
    case kCueletRingReadClientReaderUnavailable:
        return kCueletDiagnosticReadFailureClientReaderUnavailable;
    case kCueletRingReadInvalidArgument:
        return kCueletDiagnosticReadFailureInvalidArgument;
    case kCueletRingReadOK:
    case kCueletRingReadPartialRange:
    case kCueletRingReadStatusCount:
        break;
    }
    return kCueletDiagnosticReadFailureNone;
}

static CueletDiagnosticReadFailureCode CueletFirstReadFailureCode(
    const CueletDiagnosticRecordData* data)
{
    for (uint32_t code = kCueletDiagnosticReadFailureNotYetWritten;
         code < kCueletDiagnosticReadFailureCodeCount; ++code) {
        if (data->readFailureFrameCounts[code] != 0) {
            return (CueletDiagnosticReadFailureCode)code;
        }
    }
    CueletDiagnosticReadFailureCode code = CueletFailureCodeForRingStatus(
        data->ringReadFirstRejection);
    if (code == kCueletDiagnosticReadFailureNone) {
        code = CueletFailureCodeForRingStatus(data->ringReadStatus);
    }
    if (code == kCueletDiagnosticReadFailureNone &&
        data->zeroFilledFrameCount > 0) {
        code = data->timelineStatus == kCueletTimelineOK
            ? kCueletDiagnosticReadFailureInvalidArgument
            : kCueletDiagnosticReadFailureMappingInvalid;
    }
    return code;
}

static void CueletStoreCritical(
    uint32_t index,
    const CueletDiagnosticCriticalEvent* event)
{
    if (index >= CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY || event == NULL) {
        return;
    }
    CueletDiagnosticCriticalWords words = {.value = *event};
    CueletDiagnosticCriticalSlot* slot = &gCriticalEvents[index];
    atomic_store_explicit(&slot->sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t word = 0;
         word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticCriticalEvent); ++word) {
        atomic_store_explicit(
            &slot->words[word], words.words[word], memory_order_relaxed);
    }
    atomic_store_explicit(&slot->sequence, event->sequence, memory_order_release);
}

static void CueletStoreFailure(
    CueletDiagnosticFailureSlot* slot,
    const CueletDiagnosticReadFailureSummary* summary)
{
    CueletDiagnosticFailureWords words = {.value = *summary};
    atomic_store_explicit(&slot->sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t word = 0;
         word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadFailureSummary);
         ++word) {
        atomic_store_explicit(
            &slot->words[word], words.words[word], memory_order_relaxed);
    }
    atomic_store_explicit(
        &slot->sequence, summary->sequence, memory_order_release);
}

static void CueletStoreWriteRange(
    CueletDiagnosticWriteRangeSlot* slot,
    const CueletDiagnosticWriteRangeSummary* summary)
{
    CueletDiagnosticWriteRangeWords words = {.value = *summary};
    atomic_store_explicit(&slot->sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t word = 0;
         word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticWriteRangeSummary);
         ++word) {
        atomic_store_explicit(
            &slot->words[word], words.words[word], memory_order_relaxed);
    }
    atomic_store_explicit(
        &slot->sequence, summary->sequence, memory_order_release);
}

static void CueletStoreReadRange(
    CueletDiagnosticReadRangeSlot* slot,
    const CueletDiagnosticReadRangeSummary* summary)
{
    CueletDiagnosticReadRangeWords words = {.value = *summary};
    atomic_store_explicit(&slot->sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t word = 0;
         word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadRangeSummary);
         ++word) {
        atomic_store_explicit(
            &slot->words[word], words.words[word], memory_order_relaxed);
    }
    atomic_store_explicit(
        &slot->sequence, summary->sequence, memory_order_release);
}

static CueletDiagnosticCriticalEvent CueletMakeCriticalEvent(
    uint64_t sequence,
    uint32_t kind,
    const CueletDiagnosticRecordData* data)
{
    return (CueletDiagnosticCriticalEvent){
        .sequence = sequence,
        .kind = kind,
        .frameCount = data->frameCount,
        .inputSampleFrame = data->inputSampleFrame,
        .outputSampleFrame = data->outputSampleFrame,
        .absoluteStart = kind == kCueletDiagnosticCriticalNonzeroWrite
            ? data->outputStartFrame : data->sourceStartFrame,
        .generation = kind == kCueletDiagnosticCriticalNonzeroWrite
            ? data->publishedGeneration : data->expectedGeneration,
        .firstSlot = data->firstRingSlot,
        .finalSlot = data->finalRingSlot,
        .expectedTag = kind == kCueletDiagnosticCriticalNonzeroWrite
            ? data->firstPublishedAbsoluteTag : data->expectedAbsoluteTag,
        .observedTag = kind == kCueletDiagnosticCriticalNonzeroWrite
            ? data->finalPublishedAbsoluteTag : data->observedAbsoluteTag,
        .observedGeneration = data->observedGeneration,
        .payloadChecksum = data->payloadChecksum,
        .peakLeft = data->payloadPeakLeft,
        .peakRight = data->payloadPeakRight,
        .rmsLeft = data->payloadRMSLeft,
        .rmsRight = data->payloadRMSRight,
        .resultCode = kind == kCueletDiagnosticCriticalNonzeroWrite
            ? data->ringWriteStatus : CueletFirstReadFailureCode(data),
        .validFrames = data->validFrameCount,
        .zeroFilledFrames = data->zeroFilledFrameCount,
        .timelineStatus = data->timelineStatus,
        .inputTimeFlags = data->inputTimeFlags,
        .outputTimeFlags = data->outputTimeFlags,
        .clientID = data->clientID,
        .readMapped = data->readMapped,
        .readGenerationResolved = data->readGenerationResolved,
        .readPreRingAccepted = data->readPreRingAccepted,
        .readRingLookupReached = data->readRingLookupReached,
        .readerInitiallyInitialized = data->readerInitiallyInitialized,
        .readerGenerationAdopted = data->readerGenerationAdopted,
    };
}

static void CueletDiagnosticUpdateSummaries(
    uint64_t sequence,
    CueletDiagnosticEventKind kind,
    const CueletDiagnosticRecordData* data)
{
    if (kind == kCueletDiagnosticWriteMix) {
        if (data->ringWriteAcceptedFrames > 0) {
            const CueletDiagnosticWriteRangeSummary accepted = {
                .sequence = sequence,
                .start = data->outputStartFrame,
                .frameCount = data->ringWriteAcceptedFrames,
                .generation = data->publishedGeneration,
                .firstSlot = data->firstRingSlot,
                .finalSlot = data->finalRingSlot,
                .firstTag = data->firstPublishedAbsoluteTag,
                .finalTag = data->finalPublishedAbsoluteTag,
            };
            CueletStoreWriteRange(
                &gLastAcceptedWrites[sequence % 4U], &accepted);
        }
        if (data->writePublishedTagFrames > 0) {
            const CueletDiagnosticWriteRangeSummary published = {
                .sequence = sequence,
                .start = data->firstPublishedAbsoluteTag,
                .frameCount = data->writePublishedTagFrames,
                .generation = data->publishedGeneration,
                .firstSlot = data->firstRingSlot,
                .finalSlot = data->finalRingSlot,
                .firstTag = data->firstPublishedAbsoluteTag,
                .finalTag = data->finalPublishedAbsoluteTag,
            };
            CueletStoreWriteRange(
                &gLastPublishedWrites[sequence % 4U], &published);
        }
        if (data->payloadNonzeroFrameCount > 0) {
            const uint32_t index = atomic_fetch_add_explicit(
                &gCriticalWriteCount, 1, memory_order_relaxed);
            if (index < CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY) {
                const CueletDiagnosticCriticalEvent event =
                    CueletMakeCriticalEvent(
                        sequence,
                        kCueletDiagnosticCriticalNonzeroWrite,
                        data);
                CueletStoreCritical(index, &event);
            }
        }
        return;
    }
    if (kind != kCueletDiagnosticReadInput) {
        return;
    }

    const CueletDiagnosticReadFailureCode code =
        CueletFirstReadFailureCode(data);
    const CueletDiagnosticReadRangeSummary read = {
        .sequence = sequence,
        .inputStart = data->inputStartFrame,
        .sourceStart = data->sourceStartFrame,
        .frameCount = data->frameCount,
        .resultCode = code,
        .timelineStatus = data->timelineStatus,
        .inputTimeFlags = data->inputTimeFlags,
        .outputTimeFlags = data->outputTimeFlags,
        .clientID = data->clientID,
        .readMapped = data->readMapped,
        .readGenerationResolved = data->readGenerationResolved,
        .readPreRingAccepted = data->readPreRingAccepted,
        .readRingLookupReached = data->readRingLookupReached,
        .readerInitiallyInitialized = data->readerInitiallyInitialized,
        .readerGenerationAdopted = data->readerGenerationAdopted,
        .expectedGeneration = data->expectedGeneration,
        .observedGeneration = data->observedGeneration,
        .expectedTag = data->expectedAbsoluteTag,
        .observedTag = data->observedAbsoluteTag,
        .firstSlot = data->firstRingSlot,
    };
    CueletStoreReadRange(&gLastReads[sequence % 4U], &read);

    if (code != kCueletDiagnosticReadFailureNone) {
        const CueletDiagnosticReadFailureSummary failure = {
            .sequence = sequence,
            .code = code,
            .frameCount = data->zeroFilledFrameCount,
            .timelineStatus = data->timelineStatus,
            .inputTimeFlags = data->inputTimeFlags,
            .outputTimeFlags = data->outputTimeFlags,
            .clientID = data->clientID,
            .inputStart = data->inputStartFrame,
            .sourceStart = data->sourceStartFrame,
            .expectedGeneration = data->expectedGeneration,
            .observedGeneration = data->observedGeneration,
            .expectedTag = data->expectedAbsoluteTag,
            .observedTag = data->observedAbsoluteTag,
            .slot = data->firstRingSlot,
        };
        uint64_t expected = UINT64_MAX;
        if (atomic_compare_exchange_strong_explicit(
                &gFirstReadFailure.sequence,
                &expected,
                UINT64_MAX - 1U,
                memory_order_acq_rel,
                memory_order_relaxed)) {
            CueletDiagnosticFailureWords words = {.value = failure};
            for (uint32_t word = 0;
                 word < CUELET_ATOMIC_WORD_COUNT(
                     CueletDiagnosticReadFailureSummary);
                 ++word) {
                atomic_store_explicit(
                    &gFirstReadFailure.words[word],
                    words.words[word],
                    memory_order_relaxed);
            }
            atomic_store_explicit(
                &gFirstReadFailure.sequence,
                failure.sequence,
                memory_order_release);
        }
        CueletStoreFailure(&gLastReadFailures[sequence % 4U], &failure);
    }
    if (atomic_load_explicit(&gCriticalWriteCount, memory_order_acquire) > 0) {
        const uint32_t readIndex = atomic_fetch_add_explicit(
            &gCriticalReadCount, 1, memory_order_relaxed);
        if (readIndex < CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY) {
            const CueletDiagnosticCriticalEvent event = CueletMakeCriticalEvent(
                sequence,
                kCueletDiagnosticCriticalReadAfterNonzeroWrite,
                data);
            CueletStoreCritical(
                CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY + readIndex,
                &event);
        }
    }
}

static void CueletDiagnosticUpdateCounters(
    uint64_t sequence,
    CueletDiagnosticEventKind kind,
    const CueletDiagnosticRecordData* data)
{
    if (kind == kCueletDiagnosticWriteMix) {
        atomic_fetch_add_explicit(
            &gCounters.writeMixCallCount, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(
            data->payloadNonzeroFrameCount > 0
                ? &gCounters.writeMixNonzeroCallCount
                : &gCounters.writeMixZeroCallCount,
            1,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeRequestedFrames, data->frameCount, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeAcceptedFrames,
            data->ringWriteAcceptedFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeRejectedFrames,
            data->frameCount - data->ringWriteAcceptedFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeInputFrames,
            data->writeInputFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeValidatedFrames,
            data->writeValidatedFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writeStoredPayloadFrames,
            data->writeStoredPayloadFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writePublishedTagFrames,
            data->writePublishedTagFrames,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.writePublicationFailures,
            data->writePublicationFailures,
            memory_order_relaxed);
        if (data->ringWriteStatus < CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT) {
            atomic_fetch_add_explicit(
                &gCounters.writeResultCounts[data->ringWriteStatus],
                1,
                memory_order_relaxed);
        }
    } else if (kind == kCueletDiagnosticReadInput) {
        atomic_fetch_add_explicit(
            &gCounters.readInputCallCount, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.readRequestedFrames, data->frameCount, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.readValidFrames, data->validFrameCount, memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.readZeroFilledFrames,
            data->zeroFilledFrameCount,
            memory_order_relaxed);
        atomic_fetch_add_explicit(
            &gCounters.readFailureFrameCounts[kCueletDiagnosticReadFailureNone],
            data->validFrameCount,
            memory_order_relaxed);
        uint32_t categorizedFrames = 0;
        for (uint32_t code = kCueletDiagnosticReadFailureNotYetWritten;
             code < kCueletDiagnosticReadFailureCodeCount; ++code) {
            atomic_fetch_add_explicit(
                &gCounters.readFailureFrameCounts[code],
                data->readFailureFrameCounts[code],
                memory_order_relaxed);
            categorizedFrames += data->readFailureFrameCounts[code];
        }
        if (categorizedFrames < data->zeroFilledFrameCount) {
            const CueletDiagnosticReadFailureCode fallback =
                CueletFirstReadFailureCode(data);
            atomic_fetch_add_explicit(
                &gCounters.readFailureFrameCounts[fallback],
                data->zeroFilledFrameCount - categorizedFrames,
                memory_order_relaxed);
        }
        if (data->validFrameCount == data->frameCount) {
            atomic_fetch_add_explicit(
                &gCounters.readOKCalls, 1, memory_order_relaxed);
        } else if (data->validFrameCount > 0) {
            atomic_fetch_add_explicit(
                &gCounters.readPartialCalls, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(
                &gCounters.readPartialValidFrames,
                data->validFrameCount,
                memory_order_relaxed);
        } else if (data->frameCount > 0) {
            atomic_fetch_add_explicit(
                &gCounters.readAllUnavailableCalls, 1, memory_order_relaxed);
        }
        if (data->readFailureFrameCounts[
                kCueletDiagnosticReadFailureMappingInvalid] > 0) {
            atomic_fetch_add_explicit(
                &gCounters.readMappingInvalidCalls, 1, memory_order_relaxed);
        }
        if (data->readMapped != 0) {
            atomic_fetch_add_explicit(
                &gCounters.readMappedCalls, 1, memory_order_relaxed);
        }
        if (data->readGenerationResolved != 0) {
            atomic_fetch_add_explicit(
                &gCounters.readGenerationResolvedCalls, 1,
                memory_order_relaxed);
        }
        if (data->readPreRingAccepted != 0) {
            atomic_fetch_add_explicit(
                &gCounters.readPreRingAcceptedCalls, 1,
                memory_order_relaxed);
        }
        if (data->readRingLookupReached != 0) {
            atomic_fetch_add_explicit(
                &gCounters.readRingLookupCalls, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(
                &gCounters.readRingLookupFrames,
                data->readRingLookupFrames,
                memory_order_relaxed);
            if (data->zeroFilledFrameCount > 0) {
                atomic_fetch_add_explicit(
                    &gCounters.readRingLookupUnavailableCalls, 1,
                    memory_order_relaxed);
            }
        }
        if (data->readMapped != 0 && data->readGenerationResolved == 0) {
            atomic_fetch_add_explicit(
                &gCounters.readMappedButNoGenerationCalls, 1,
                memory_order_relaxed);
        }
        if (data->readGenerationResolved != 0 &&
            data->readRingLookupReached == 0) {
            atomic_fetch_add_explicit(
                &gCounters.readGenerationButNoRingCalls, 1,
                memory_order_relaxed);
        }
        if (data->ringReadStatus < CUELET_DIAGNOSTIC_READ_RESULT_COUNT) {
            atomic_fetch_add_explicit(
                &gCounters.readResultCounts[data->ringReadStatus],
                1,
                memory_order_relaxed);
        }
    } else if (kind == kCueletDiagnosticRingReset) {
        atomic_fetch_add_explicit(
            &gCounters.ringResetCount, 1, memory_order_relaxed);
        if (data->resetGenerationAfter != data->resetGenerationBefore) {
            atomic_fetch_add_explicit(
                &gCounters.generationChangeCount, 1, memory_order_relaxed);
        }
    } else if (kind == kCueletDiagnosticStartIO) {
        atomic_fetch_add_explicit(&gCounters.startIOCount, 1, memory_order_relaxed);
    } else if (kind == kCueletDiagnosticStopIO) {
        atomic_fetch_add_explicit(&gCounters.stopIOCount, 1, memory_order_relaxed);
    } else if (kind == kCueletDiagnosticSampleRateChange) {
        atomic_fetch_add_explicit(
            &gCounters.sampleRateChangeCount, 1, memory_order_relaxed);
    } else if (kind == kCueletDiagnosticStreamActivationChange) {
        atomic_fetch_add_explicit(
            &gCounters.streamActivationChangeCount, 1, memory_order_relaxed);
    } else if (kind == kCueletDiagnosticPropertySnapshot) {
        atomic_fetch_add_explicit(
            &gCounters.propertySnapshotCount, 1, memory_order_relaxed);
    }
    if (data->timelineStatus < CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT &&
        (kind == kCueletDiagnosticWriteMix || kind == kCueletDiagnosticReadInput)) {
        atomic_fetch_add_explicit(
            &gCounters.timelineResultCounts[data->timelineStatus],
            1,
            memory_order_relaxed);
    }
    CueletDiagnosticUpdateSummaries(sequence, kind, data);
}

void CueletDiagnosticRecord(
    CueletDiagnosticEventKind kind,
    const CueletDiagnosticRecordData* data)
{
    if (data == NULL) {
        return;
    }
    CueletDiagnosticPayloadWords payload = {0};
    payload.payload.eventKind = (uint64_t)kind;
    payload.payload.data = *data;
    if (payload.payload.data.stateToken == 0) {
        payload.payload.data.stateToken = atomic_load_explicit(
            &gStateToken, memory_order_relaxed);
    }
    if (payload.payload.data.ringToken == 0) {
        payload.payload.data.ringToken = atomic_load_explicit(
            &gRingToken, memory_order_relaxed);
    }
    const uint64_t sequence = atomic_fetch_add_explicit(
        &gNextSequence, 1, memory_order_relaxed);
    CueletDiagnosticUpdateCounters(sequence, kind, &payload.payload.data);
    CueletDiagnosticSlot* slot =
        &gEvents[sequence % CUELET_DIAGNOSTIC_EVENT_CAPACITY];
    atomic_store_explicit(&slot->sequence, UINT64_MAX, memory_order_relaxed);
    for (uint32_t word = 0; word < kCueletDiagnosticPayloadWordCount; ++word) {
        atomic_store_explicit(
            &slot->words[word], payload.words[word], memory_order_relaxed);
    }
    atomic_store_explicit(&slot->sequence, sequence, memory_order_release);
    const uint64_t clear = atomic_load_explicit(&gClearSequence, memory_order_relaxed);
    if (sequence >= clear + CUELET_DIAGNOSTIC_EVENT_CAPACITY) {
        atomic_fetch_add_explicit(&gOverwriteCount, 1, memory_order_relaxed);
    }
}

static size_t CueletDiagnosticCopyRange(
    CueletDiagnosticSnapshot* output,
    size_t capacity,
    uint64_t* nextSequenceInOut,
    uint64_t end,
    uint64_t clear)
{
    if (output == NULL || capacity == 0 || nextSequenceInOut == NULL) {
        return 0;
    }
    const uint64_t oldest = end > CUELET_DIAGNOSTIC_EVENT_CAPACITY
        ? end - CUELET_DIAGNOSTIC_EVENT_CAPACITY
        : 0;
    uint64_t sequence = *nextSequenceInOut;
    if (sequence < clear) {
        sequence = clear;
    }
    if (sequence < oldest) {
        sequence = oldest;
    }
    size_t copied = 0;
    while (sequence < end && copied < capacity) {
        CueletDiagnosticSlot* slot =
            &gEvents[sequence % CUELET_DIAGNOSTIC_EVENT_CAPACITY];
        const uint64_t before = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before != sequence) {
            break;
        }
        CueletDiagnosticPayloadWords payload = {0};
        for (uint32_t word = 0; word < kCueletDiagnosticPayloadWordCount; ++word) {
            payload.words[word] = atomic_load_explicit(
                &slot->words[word], memory_order_relaxed);
        }
        const uint64_t after = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (after != sequence) {
            break;
        }
        output[copied].sequence = sequence;
        output[copied].eventKind = (uint32_t)payload.payload.eventKind;
        output[copied].reserved = 0;
        output[copied].data = payload.payload.data;
        ++copied;
        ++sequence;
    }
    *nextSequenceInOut = sequence;
    return copied;
}

size_t CueletDiagnosticCopy(
    CueletDiagnosticSnapshot* output,
    size_t capacity,
    uint64_t* nextSequenceInOut)
{
    const uint64_t end = atomic_load_explicit(
        &gNextSequence, memory_order_acquire);
    const uint64_t clear = atomic_load_explicit(
        &gClearSequence, memory_order_acquire);
    return CueletDiagnosticCopyRange(
        output, capacity, nextSequenceInOut, end, clear);
}

uint64_t CueletDiagnosticDroppedCount(void)
{
    const uint64_t total = atomic_load_explicit(&gOverwriteCount, memory_order_acquire);
    const uint64_t baseline = atomic_load_explicit(
        &gOverwriteBaseline, memory_order_acquire);
    return total >= baseline ? total - baseline : 0;
}

uint64_t CueletDiagnosticEventCount(void)
{
    const uint64_t end = atomic_load_explicit(&gNextSequence, memory_order_acquire);
    const uint64_t clear = atomic_load_explicit(&gClearSequence, memory_order_acquire);
    const uint64_t count = end >= clear ? end - clear : 0;
    return count > CUELET_DIAGNOSTIC_EVENT_CAPACITY
        ? CUELET_DIAGNOSTIC_EVENT_CAPACITY
        : count;
}

void CueletDiagnosticGetSchema(CueletDiagnosticSchema* schemaOut)
{
    if (schemaOut == NULL) {
        return;
    }
    *schemaOut = (CueletDiagnosticSchema){
        .schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION,
        .eventSize = sizeof(CueletDiagnosticSnapshot),
        .eventCapacity = CUELET_DIAGNOSTIC_EVENT_CAPACITY,
        .maximumAnalyzedFrames = CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES,
        .countersSize = sizeof(CueletDiagnosticCounters),
        .buildInfoSize = sizeof(CueletDiagnosticBuildInfo),
        .diagnosticEnabled = 1,
        .eventSnapshotCapacity = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY,
    };
}

void CueletDiagnosticGetBuildInfo(CueletDiagnosticBuildInfo* buildOut)
{
    if (buildOut == NULL) {
        return;
    }
    *buildOut = (CueletDiagnosticBuildInfo){
        .versionMajor = 0,
        .versionMinor = 1,
        .versionPatch = 11,
        .buildNumber = 12,
        .diagnosticEnabled = 1,
        .schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION,
        .architecture = 1,
    };
}

static bool CueletLoadCritical(
    const CueletDiagnosticCriticalSlot* slot,
    CueletDiagnosticCriticalEvent* output)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before >= UINT64_MAX - 1U) return false;
        CueletDiagnosticCriticalWords words = {0};
        for (uint32_t word = 0;
             word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticCriticalEvent);
             ++word) {
            words.words[word] = atomic_load_explicit(
                &slot->words[word], memory_order_relaxed);
        }
        const uint64_t after = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == after && words.value.sequence == before) {
            *output = words.value;
            return true;
        }
    }
    return false;
}

static bool CueletLoadFailure(
    const CueletDiagnosticFailureSlot* slot,
    CueletDiagnosticReadFailureSummary* output)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before >= UINT64_MAX - 1U) return false;
        CueletDiagnosticFailureWords words = {0};
        for (uint32_t word = 0;
             word < CUELET_ATOMIC_WORD_COUNT(
                 CueletDiagnosticReadFailureSummary); ++word) {
            words.words[word] = atomic_load_explicit(
                &slot->words[word], memory_order_relaxed);
        }
        const uint64_t after = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == after && words.value.sequence == before) {
            *output = words.value;
            return true;
        }
    }
    return false;
}

static bool CueletLoadWriteRange(
    const CueletDiagnosticWriteRangeSlot* slot,
    CueletDiagnosticWriteRangeSummary* output)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == UINT64_MAX) return false;
        CueletDiagnosticWriteRangeWords words = {0};
        for (uint32_t word = 0;
             word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticWriteRangeSummary);
             ++word) {
            words.words[word] = atomic_load_explicit(
                &slot->words[word], memory_order_relaxed);
        }
        const uint64_t after = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == after && words.value.sequence == before) {
            *output = words.value;
            return true;
        }
    }
    return false;
}

static bool CueletLoadReadRange(
    const CueletDiagnosticReadRangeSlot* slot,
    CueletDiagnosticReadRangeSummary* output)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == UINT64_MAX) return false;
        CueletDiagnosticReadRangeWords words = {0};
        for (uint32_t word = 0;
             word < CUELET_ATOMIC_WORD_COUNT(CueletDiagnosticReadRangeSummary);
             ++word) {
            words.words[word] = atomic_load_explicit(
                &slot->words[word], memory_order_relaxed);
        }
        const uint64_t after = atomic_load_explicit(
            &slot->sequence, memory_order_acquire);
        if (before == after && words.value.sequence == before) {
            *output = words.value;
            return true;
        }
    }
    return false;
}

void CueletDiagnosticGetCounters(CueletDiagnosticCounters* countersOut)
{
    if (countersOut == NULL) {
        return;
    }
    memset(countersOut, 0, sizeof(*countersOut));
    countersOut->stateToken = atomic_load_explicit(&gStateToken, memory_order_acquire);
    countersOut->ringToken = atomic_load_explicit(&gRingToken, memory_order_acquire);
    countersOut->nextSequence = atomic_load_explicit(&gNextSequence, memory_order_acquire);
    countersOut->clearSequence = atomic_load_explicit(&gClearSequence, memory_order_acquire);
    countersOut->availableEventCount = CueletDiagnosticEventCount();
    countersOut->droppedEventCount = CueletDiagnosticDroppedCount();
#define CUELET_LOAD_COUNTER(name) \
    countersOut->name = atomic_load_explicit(&gCounters.name, memory_order_acquire)
    CUELET_LOAD_COUNTER(writeMixCallCount);
    CUELET_LOAD_COUNTER(writeMixNonzeroCallCount);
    CUELET_LOAD_COUNTER(writeMixZeroCallCount);
    CUELET_LOAD_COUNTER(writeRequestedFrames);
    CUELET_LOAD_COUNTER(writeAcceptedFrames);
    CUELET_LOAD_COUNTER(writeRejectedFrames);
    CUELET_LOAD_COUNTER(readInputCallCount);
    CUELET_LOAD_COUNTER(readRequestedFrames);
    CUELET_LOAD_COUNTER(readValidFrames);
    CUELET_LOAD_COUNTER(readZeroFilledFrames);
    CUELET_LOAD_COUNTER(ringResetCount);
    CUELET_LOAD_COUNTER(generationChangeCount);
    CUELET_LOAD_COUNTER(startIOCount);
    CUELET_LOAD_COUNTER(stopIOCount);
    CUELET_LOAD_COUNTER(sampleRateChangeCount);
    CUELET_LOAD_COUNTER(streamActivationChangeCount);
    CUELET_LOAD_COUNTER(propertySnapshotCount);
    CUELET_LOAD_COUNTER(writeInputFrames);
    CUELET_LOAD_COUNTER(writeValidatedFrames);
    CUELET_LOAD_COUNTER(writeStoredPayloadFrames);
    CUELET_LOAD_COUNTER(writePublishedTagFrames);
    CUELET_LOAD_COUNTER(writePublicationFailures);
    CUELET_LOAD_COUNTER(readPartialValidFrames);
    CUELET_LOAD_COUNTER(readOKCalls);
    CUELET_LOAD_COUNTER(readPartialCalls);
    CUELET_LOAD_COUNTER(readAllUnavailableCalls);
    CUELET_LOAD_COUNTER(readMappingInvalidCalls);
    CUELET_LOAD_COUNTER(readMappedCalls);
    CUELET_LOAD_COUNTER(readGenerationResolvedCalls);
    CUELET_LOAD_COUNTER(readPreRingAcceptedCalls);
    CUELET_LOAD_COUNTER(readRingLookupCalls);
    CUELET_LOAD_COUNTER(readRingLookupFrames);
    CUELET_LOAD_COUNTER(readMappedButNoGenerationCalls);
    CUELET_LOAD_COUNTER(readGenerationButNoRingCalls);
    CUELET_LOAD_COUNTER(readRingLookupUnavailableCalls);
#undef CUELET_LOAD_COUNTER
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT; ++index) {
        countersOut->writeResultCounts[index] = atomic_load_explicit(
            &gCounters.writeResultCounts[index], memory_order_acquire);
    }
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_READ_RESULT_COUNT; ++index) {
        countersOut->readResultCounts[index] = atomic_load_explicit(
            &gCounters.readResultCounts[index], memory_order_acquire);
    }
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT; ++index) {
        countersOut->timelineResultCounts[index] = atomic_load_explicit(
            &gCounters.timelineResultCounts[index], memory_order_acquire);
    }
    const uint64_t readFailureCounts[kCueletDiagnosticReadFailureCodeCount] = {
        [kCueletDiagnosticReadFailureNone] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureNone], memory_order_acquire),
        [kCueletDiagnosticReadFailureNotYetWritten] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureNotYetWritten], memory_order_acquire),
        [kCueletDiagnosticReadFailureOverwritten] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureOverwritten], memory_order_acquire),
        [kCueletDiagnosticReadFailureGenerationMismatch] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureGenerationMismatch], memory_order_acquire),
        [kCueletDiagnosticReadFailureAbsoluteTagMismatch] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureAbsoluteTagMismatch], memory_order_acquire),
        [kCueletDiagnosticReadFailureUnpublished] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureUnpublished], memory_order_acquire),
        [kCueletDiagnosticReadFailureTimelineUninitialized] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureTimelineUninitialized], memory_order_acquire),
        [kCueletDiagnosticReadFailureStreamInactive] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureStreamInactive], memory_order_acquire),
        [kCueletDiagnosticReadFailureClientReaderUnavailable] =
            atomic_load_explicit(
                &gCounters.readFailureFrameCounts[
                    kCueletDiagnosticReadFailureClientReaderUnavailable],
                memory_order_acquire),
        [kCueletDiagnosticReadFailureMappingInvalid] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureMappingInvalid], memory_order_acquire),
        [kCueletDiagnosticReadFailureInvalidArgument] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureInvalidArgument], memory_order_acquire),
        [kCueletDiagnosticReadFailureSampleRateReset] = atomic_load_explicit(
            &gCounters.readFailureFrameCounts[
                kCueletDiagnosticReadFailureSampleRateReset], memory_order_acquire),
    };
    countersOut->readOKFrames =
        readFailureCounts[kCueletDiagnosticReadFailureNone];
    countersOut->readNotYetWrittenFrames =
        readFailureCounts[kCueletDiagnosticReadFailureNotYetWritten];
    countersOut->readOverwrittenFrames =
        readFailureCounts[kCueletDiagnosticReadFailureOverwritten];
    countersOut->readGenerationMismatchFrames =
        readFailureCounts[kCueletDiagnosticReadFailureGenerationMismatch];
    countersOut->readAbsoluteTagMismatchFrames =
        readFailureCounts[kCueletDiagnosticReadFailureAbsoluteTagMismatch];
    countersOut->readUnpublishedFrames =
        readFailureCounts[kCueletDiagnosticReadFailureUnpublished];
    countersOut->readTimelineUninitializedFrames =
        readFailureCounts[kCueletDiagnosticReadFailureTimelineUninitialized];
    countersOut->readStreamInactiveFrames =
        readFailureCounts[kCueletDiagnosticReadFailureStreamInactive];
    countersOut->readClientReaderUnavailableFrames =
        readFailureCounts[kCueletDiagnosticReadFailureClientReaderUnavailable];
    countersOut->readMappingInvalidFrames =
        readFailureCounts[kCueletDiagnosticReadFailureMappingInvalid];
    countersOut->readInvalidArgumentFrames =
        readFailureCounts[kCueletDiagnosticReadFailureInvalidArgument];
    countersOut->readSampleRateResetFrames =
        readFailureCounts[kCueletDiagnosticReadFailureSampleRateReset];

    (void)CueletLoadFailure(&gFirstReadFailure, &countersOut->firstReadFailure);
    for (uint32_t index = 0; index < 4; ++index) {
        CueletDiagnosticReadFailureSummary failure = {0};
        if (CueletLoadFailure(&gLastReadFailures[index], &failure) &&
            failure.sequence >= countersOut->lastReadFailure.sequence) {
            countersOut->lastReadFailure = failure;
        }
        CueletDiagnosticWriteRangeSummary write = {0};
        if (CueletLoadWriteRange(&gLastAcceptedWrites[index], &write) &&
            write.sequence >= countersOut->lastAcceptedWrite.sequence) {
            countersOut->lastAcceptedWrite = write;
        }
        memset(&write, 0, sizeof(write));
        if (CueletLoadWriteRange(&gLastPublishedWrites[index], &write) &&
            write.sequence >= countersOut->lastPublishedWrite.sequence) {
            countersOut->lastPublishedWrite = write;
        }
        CueletDiagnosticReadRangeSummary read = {0};
        if (CueletLoadReadRange(&gLastReads[index], &read) &&
            read.sequence >= countersOut->lastRead.sequence) {
            countersOut->lastRead = read;
        }
    }
    const uint32_t writeCount = atomic_load_explicit(
        &gCriticalWriteCount, memory_order_acquire);
    const uint32_t readCount = atomic_load_explicit(
        &gCriticalReadCount, memory_order_acquire);
    countersOut->criticalWriteCount = writeCount >
            CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY
        ? CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY : writeCount;
    countersOut->criticalReadCount = readCount >
            CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY
        ? CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY : readCount;
    countersOut->criticalEventCapacity =
        CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY;
    for (uint32_t index = 0;
         index < CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY; ++index) {
        CueletDiagnosticCriticalEvent event = {0};
        if (CueletLoadCritical(&gCriticalEvents[index], &event)) {
            countersOut->criticalEvents[countersOut->criticalEventCount++] = event;
        }
    }
}

size_t CueletDiagnosticExportSize(void)
{
    return CueletDiagnosticEventSnapshotSizeForCount(
        CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
}

size_t CueletDiagnosticEventSnapshotSizeForCount(size_t eventCount)
{
    if (eventCount > CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY) {
        eventCount = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY;
    }
    return sizeof(CueletDiagnosticEventExportHeader) +
        eventCount * sizeof(CueletDiagnosticSnapshot);
}

OSStatus CueletDiagnosticExportEventSnapshot(
    void* output,
    size_t outputSize,
    size_t* usedSizeOut)
{
    if (output == NULL || usedSizeOut == NULL ||
        outputSize < sizeof(CueletDiagnosticEventExportHeader)) {
        return kAudioHardwareBadPropertySizeError;
    }
    CueletDiagnosticEventExportHeader* header = output;
    CueletDiagnosticSnapshot* events = (CueletDiagnosticSnapshot*)(header + 1);
    size_t capacity = (outputSize - sizeof(*header)) / sizeof(*events);
    if (capacity > CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY) {
        capacity = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY;
    }
    const uint64_t end = atomic_load_explicit(
        &gNextSequence, memory_order_acquire);
    const uint64_t clear = atomic_load_explicit(
        &gClearSequence, memory_order_acquire);
    uint64_t oldest = end > CUELET_DIAGNOSTIC_EVENT_CAPACITY
        ? end - CUELET_DIAGNOSTIC_EVENT_CAPACITY : 0;
    if (oldest < clear) oldest = clear;
    const uint64_t available = end >= oldest ? end - oldest : 0;
    const uint64_t requestedCount = available < capacity
        ? available : capacity;
    uint64_t next = end - requestedCount;
    const size_t copied = CueletDiagnosticCopyRange(
        events, capacity, &next, end, clear);
    header->schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION;
    header->eventRecordSize = sizeof(CueletDiagnosticSnapshot);
    header->snapshotCapacity = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY;
    header->returnedEventCount = (uint32_t)copied;
    header->oldestAvailableSequence = copied > 0 || available == 0
        ? oldest : end;
    header->newestAvailableSequence = copied > 0 && available > 0
        ? end - 1U : 0;
    header->firstReturnedSequence = copied > 0 ? events[0].sequence : 0;
    header->lastReturnedSequence = copied > 0
        ? events[copied - 1U].sequence : 0;
    header->availableEventCount = copied > 0 || available == 0
        ? available : 0;
    header->droppedEventCount = CueletDiagnosticDroppedCount();
    *usedSizeOut = sizeof(*header) + copied * sizeof(*events);
    return noErr;
}

const char* CueletDiagnosticEventName(uint32_t kind)
{
    switch ((CueletDiagnosticEventKind)kind) {
    case kCueletDiagnosticDriverInitialize: return "driver_initialize";
    case kCueletDiagnosticStartIO: return "start_io";
    case kCueletDiagnosticStopIO: return "stop_io";
    case kCueletDiagnosticGetZeroTimeStamp: return "zero_timestamp";
    case kCueletDiagnosticWillDoIOOperation: return "will_do_io";
    case kCueletDiagnosticBeginIOOperation: return "begin_io";
    case kCueletDiagnosticDoIOOperation: return "do_io";
    case kCueletDiagnosticEndIOOperation: return "end_io";
    case kCueletDiagnosticWriteMix: return "write_mix";
    case kCueletDiagnosticReadInput: return "read_input";
    case kCueletDiagnosticRingReset: return "ring_reset";
    case kCueletDiagnosticRingWrite: return "ring_write";
    case kCueletDiagnosticRingRead: return "ring_read";
    case kCueletDiagnosticProducerRejected: return "producer_rejected";
    case kCueletDiagnosticSampleRateChange: return "sample_rate_change";
    case kCueletDiagnosticStreamActivationChange: return "stream_activation";
    case kCueletDiagnosticPropertySnapshot: return "property_snapshot";
    case kCueletDiagnosticEventKindCount: break;
    }
    return "unknown";
}

const char* CueletDiagnosticWriteResultName(uint32_t status)
{
    switch ((CueletRingWriteStatus)status) {
    case kCueletRingWriteOK: return "WRITE_OK";
    case kCueletRingWriteGenerationMismatch: return "WRITE_GENERATION_MISMATCH";
    case kCueletRingWriteTimelineUninitialized: return "WRITE_TIMELINE_UNINITIALIZED";
    case kCueletRingWriteInvalidSampleTime: return "WRITE_INVALID_SAMPLE_TIME";
    case kCueletRingWriteInvalidArgument: return "WRITE_INVALID_ARGUMENT";
    }
    return "WRITE_UNKNOWN";
}

const char* CueletDiagnosticReadResultName(uint32_t status)
{
    switch ((CueletRingReadStatus)status) {
    case kCueletRingReadOK: return "READ_OK";
    case kCueletRingReadNotYetWritten: return "READ_NOT_YET_WRITTEN";
    case kCueletRingReadOverwritten: return "READ_OVERWRITTEN";
    case kCueletRingReadGenerationMismatch: return "READ_GENERATION_MISMATCH";
    case kCueletRingReadAbsoluteFrameMismatch: return "READ_ABSOLUTE_FRAME_MISMATCH";
    case kCueletRingReadPartialRange: return "READ_PARTIAL_RANGE";
    case kCueletRingReadUnpublished: return "READ_UNPUBLISHED";
    case kCueletRingReadSampleRateReset: return "READ_SAMPLE_RATE_RESET";
    case kCueletRingReadTimelineUninitialized: return "READ_TIMELINE_UNINITIALIZED";
    case kCueletRingReadMappingInvalid: return "READ_MAPPING_INVALID";
    case kCueletRingReadStreamInactive: return "READ_STREAM_INACTIVE";
    case kCueletRingReadClientReaderUnavailable:
        return "READ_CLIENT_READER_UNAVAILABLE";
    case kCueletRingReadInvalidArgument: return "READ_INVALID_ARGUMENT";
    case kCueletRingReadStatusCount: break;
    }
    return "READ_UNKNOWN";
}

const char* CueletDiagnosticTimelineResultName(uint32_t status)
{
    switch ((CueletTimelineStatus)status) {
    case kCueletTimelineOK: return "TIMELINE_OK";
    case kCueletTimelineInvalidArgument: return "TIMELINE_INVALID_ARGUMENT";
    case kCueletTimelineInputTimestampInvalid: return "TIMELINE_INPUT_INVALID";
    case kCueletTimelineOutputTimestampInvalid: return "TIMELINE_OUTPUT_INVALID";
    case kCueletTimelineNegativeSampleTime: return "TIMELINE_NEGATIVE_SAMPLE_TIME";
    case kCueletTimelineFractionalSampleTime: return "TIMELINE_FRACTIONAL_SAMPLE_TIME";
    case kCueletTimelineSampleTimeOverflow: return "TIMELINE_SAMPLE_TIME_OVERFLOW";
    case kCueletTimelineUninitialized: return "TIMELINE_UNINITIALIZED";
    case kCueletTimelineNegativeSourceRange: return "TIMELINE_NEGATIVE_SOURCE_RANGE";
    }
    return "TIMELINE_UNKNOWN";
}

#endif
