#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned gAssertions;
static unsigned gFailures;

enum { kConcurrentWriterIterations = 10000 };

typedef struct ConcurrentWriterContext {
    uint64_t base;
} ConcurrentWriterContext;

static void* concurrentWriter(void* rawContext)
{
    const ConcurrentWriterContext* context = rawContext;
    CueletDiagnosticRecordData event = {0};
    for (uint32_t index = 0; index < kConcurrentWriterIterations; ++index) {
        event.cycleCounter = context->base + index;
        event.frameCount = 512;
        CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &event);
    }
    return NULL;
}

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++gAssertions;                                                          \
        if (!(condition)) {                                                     \
            ++gFailures;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                       \
    } while (0)

static void testSchemaBuildAndCounters(void)
{
    CueletDiagnosticInitialize();
    CueletDiagnosticSetStateTokens(UINT64_C(0x1234), UINT64_C(0x5678));
    CueletDiagnosticSchema schema = {0};
    CueletDiagnosticGetSchema(&schema);
    CHECK(schema.schemaVersion == CUELET_DIAGNOSTIC_SCHEMA_VERSION);
    CHECK(schema.eventCapacity == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
    CHECK(schema.eventSize == sizeof(CueletDiagnosticSnapshot));
    CHECK(schema.maximumAnalyzedFrames == CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES);
    CHECK(schema.diagnosticEnabled == 1);
    CHECK(schema.eventSnapshotCapacity ==
        CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);

    CueletDiagnosticBuildInfo build = {0};
    CueletDiagnosticGetBuildInfo(&build);
    CHECK(build.versionMajor == 0);
    CHECK(build.versionMinor == 1);
    CHECK(build.versionPatch == 11);
    CHECK(build.buildNumber == 12);
    CHECK(build.diagnosticEnabled == 1);

    CueletDiagnosticRecordData write = {0};
    write.frameCount = 512;
    write.payloadNonzeroFrameCount = 512;
    write.ringWriteStatus = kCueletRingWriteOK;
    write.ringWriteAcceptedFrames = 512;
    write.writeInputFrames = 512;
    write.writeValidatedFrames = 512;
    write.writeStoredPayloadFrames = 512;
    write.writePublishedTagFrames = 512;
    write.outputStartFrame = 123064;
    write.firstPublishedAbsoluteTag = 123064;
    write.finalPublishedAbsoluteTag = 123575;
    write.firstRingSlot = 8680;
    write.finalRingSlot = 9191;
    write.publishedGeneration = 2;
    write.payloadChecksum = UINT64_C(0x123456789abcdef0);
    write.payloadPeakLeft = 0.25F;
    write.payloadPeakRight = 0.25F;
    write.timelineStatus = kCueletTimelineOK;
    CueletDiagnosticRecord(kCueletDiagnosticWriteMix, &write);

    CueletDiagnosticRecordData read = {0};
    read.frameCount = 512;
    read.validFrameCount = 384;
    read.zeroFilledFrameCount = 128;
    read.ringReadStatus = kCueletRingReadPartialRange;
    read.ringReadFirstRejection = kCueletRingReadNotYetWritten;
    read.readFailureFrameCounts[
        kCueletDiagnosticReadFailureNotYetWritten] = 128;
    read.inputStartFrame = 122880;
    read.sourceStartFrame = 122552;
    read.expectedGeneration = 2;
    read.expectedAbsoluteTag = 122936;
    read.observedAbsoluteTag = UINT64_MAX;
    read.firstRingSlot = 8168;
    read.timelineStatus = kCueletTimelineOK;
    CueletDiagnosticRecord(kCueletDiagnosticReadInput, &read);

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.stateToken == UINT64_C(0x1234));
    CHECK(counters.ringToken == UINT64_C(0x5678));
    CHECK(counters.writeMixCallCount == 1);
    CHECK(counters.writeMixNonzeroCallCount == 1);
    CHECK(counters.writeRequestedFrames == 512);
    CHECK(counters.writeAcceptedFrames == 512);
    CHECK(counters.writeInputFrames == 512);
    CHECK(counters.writeValidatedFrames == 512);
    CHECK(counters.writeStoredPayloadFrames == 512);
    CHECK(counters.writePublishedTagFrames == 512);
    CHECK(counters.writePublicationFailures == 0);
    CHECK(counters.readInputCallCount == 1);
    CHECK(counters.readValidFrames == 384);
    CHECK(counters.readZeroFilledFrames == 128);
    CHECK(counters.readOKFrames == 384);
    CHECK(counters.readNotYetWrittenFrames == 128);
    CHECK(counters.readPartialValidFrames == 384);
    CHECK(counters.readPartialCalls == 1);
    CHECK(counters.readOKFrames + counters.readNotYetWrittenFrames ==
        counters.readRequestedFrames);
    CHECK(counters.lastAcceptedWrite.start == 123064);
    CHECK(counters.lastPublishedWrite.firstTag == 123064);
    CHECK(counters.lastPublishedWrite.finalTag == 123575);
    CHECK(counters.firstReadFailure.code ==
        kCueletDiagnosticReadFailureNotYetWritten);
    CHECK(counters.firstReadFailure.sourceStart == 122552);
    CHECK(counters.writeResultCounts[kCueletRingWriteOK] == 1);
    CHECK(counters.readResultCounts[kCueletRingReadPartialRange] == 1);
}

static void testSnapshotAndClear(void)
{
    const size_t exportSize = CueletDiagnosticExportSize();
    void* allocation = calloc(1, exportSize);
    CHECK(allocation != NULL);
    if (allocation == NULL) return;
    size_t used = 0;
    CHECK(CueletDiagnosticExportEventSnapshot(
        allocation, exportSize, &used) == noErr);
    CueletDiagnosticEventExportHeader* header = allocation;
    CHECK(header->schemaVersion == CUELET_DIAGNOSTIC_SCHEMA_VERSION);
    CHECK(header->eventRecordSize == sizeof(CueletDiagnosticSnapshot));
    CHECK(header->snapshotCapacity ==
        CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
    CHECK(header->returnedEventCount == 2);
    CHECK(header->availableEventCount == 2);
    CHECK(header->oldestAvailableSequence == 0);
    CHECK(header->newestAvailableSequence == 1);
    CHECK(header->firstReturnedSequence == 0);
    CHECK(header->lastReturnedSequence == 1);
    CHECK(used == sizeof(*header) + 2 * sizeof(CueletDiagnosticSnapshot));
    CueletDiagnosticSnapshot* events = (CueletDiagnosticSnapshot*)(header + 1);
    CHECK(events[0].eventKind == kCueletDiagnosticWriteMix);
    CHECK(events[0].data.stateToken == UINT64_C(0x1234));
    CHECK(events[1].eventKind == kCueletDiagnosticReadInput);
    free(allocation);

    used = UINT64_MAX;
    CHECK(CueletDiagnosticExportEventSnapshot(
        NULL, exportSize, &used) ==
        kAudioHardwareBadPropertySizeError);
    uint8_t undersized[sizeof(CueletDiagnosticEventExportHeader) - 1] = {0};
    CHECK(CueletDiagnosticExportEventSnapshot(
        undersized, sizeof(undersized), &used) ==
        kAudioHardwareBadPropertySizeError);

    CueletDiagnosticClear();
    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.availableEventCount == 0);
    CHECK(counters.writeMixCallCount == 0);
    CHECK(counters.readInputCallCount == 0);
    CHECK(counters.droppedEventCount == 0);

    allocation = calloc(1, exportSize);
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        used = 0;
        CHECK(CueletDiagnosticExportEventSnapshot(
            allocation, exportSize, &used) == noErr);
        header = allocation;
        CHECK(header->returnedEventCount == 0);
        CHECK(header->availableEventCount == 0);
        CHECK(header->firstReturnedSequence == 0);
        CHECK(header->lastReturnedSequence == 0);
        CHECK(used == sizeof(*header));
        free(allocation);
    }

    CueletDiagnosticRecordData one = {.cycleCounter = 77};
    CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &one);
    allocation = calloc(1, exportSize);
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        used = 0;
        CHECK(CueletDiagnosticExportEventSnapshot(
            allocation, exportSize, &used) == noErr);
        header = allocation;
        CHECK(header->returnedEventCount == 1);
        CHECK(header->availableEventCount == 1);
        CHECK(header->firstReturnedSequence ==
            header->lastReturnedSequence);
        const CueletDiagnosticSnapshot* oneEvent =
            (const CueletDiagnosticSnapshot*)(header + 1);
        CHECK(oneEvent->data.cycleCounter == 77);
        free(allocation);
    }
    CueletDiagnosticClear();

    CueletDiagnosticRecordData bounded = {0};
    for (uint32_t index = 0;
         index < CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY; ++index) {
        bounded.cycleCounter = index;
        CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &bounded);
    }
    allocation = calloc(1, exportSize);
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        used = 0;
        CHECK(CueletDiagnosticExportEventSnapshot(
            allocation, exportSize, &used) == noErr);
        header = allocation;
        CHECK(header->returnedEventCount ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(header->availableEventCount ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        const CueletDiagnosticSnapshot* boundedEvents =
            (const CueletDiagnosticSnapshot*)(header + 1);
        CHECK(boundedEvents[0].data.cycleCounter == 0);
        CHECK(boundedEvents[
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY - 1U]
                .data.cycleCounter ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY - 1U);
        free(allocation);
    }
    CueletDiagnosticClear();
}

static void testWrapAndDroppedCounter(void)
{
    CueletDiagnosticRecordData event = {0};
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_EVENT_CAPACITY + 17U;
         ++index) {
        event.cycleCounter = index;
        CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &event);
    }
    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.availableEventCount == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
    CHECK(counters.droppedEventCount == 17);

    const size_t snapshotSize = CueletDiagnosticExportSize();
    void* snapshotExport = calloc(1, snapshotSize);
    CHECK(snapshotExport != NULL);
    if (snapshotExport != NULL) {
        size_t used = 0;
        CHECK(CueletDiagnosticExportEventSnapshot(
            snapshotExport, snapshotSize, &used) == noErr);
        const CueletDiagnosticEventExportHeader* header = snapshotExport;
        const CueletDiagnosticSnapshot* snapshotEvents =
            (const CueletDiagnosticSnapshot*)(header + 1);
        CHECK(header->returnedEventCount ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(header->availableEventCount == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
        CHECK(header->droppedEventCount == 17);
        CHECK(header->oldestAvailableSequence ==
            counters.nextSequence - CUELET_DIAGNOSTIC_EVENT_CAPACITY);
        CHECK(header->newestAvailableSequence == counters.nextSequence - 1U);
        CHECK(header->firstReturnedSequence ==
            counters.nextSequence -
                CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(header->lastReturnedSequence ==
            header->newestAvailableSequence);
        CHECK(snapshotEvents[0].data.cycleCounter ==
            CUELET_DIAGNOSTIC_EVENT_CAPACITY + 17U -
                CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(used == sizeof(*header) +
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY *
                sizeof(CueletDiagnosticSnapshot));
        for (uint32_t index = 1;
             index < header->returnedEventCount; ++index) {
            CHECK(snapshotEvents[index].sequence ==
                snapshotEvents[index - 1U].sequence + 1U);
        }
        free(snapshotExport);
    }

    CueletDiagnosticSnapshot* events = calloc(
        CUELET_DIAGNOSTIC_EVENT_CAPACITY, sizeof(*events));
    CHECK(events != NULL);
    if (events == NULL) return;
    uint64_t next = 0;
    const size_t copied = CueletDiagnosticCopy(
        events, CUELET_DIAGNOSTIC_EVENT_CAPACITY, &next);
    CHECK(copied == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
    CHECK(events[0].data.cycleCounter == 17);
    CHECK(events[copied - 1].data.cycleCounter ==
        CUELET_DIAGNOSTIC_EVENT_CAPACITY + 16U);
    free(events);
}

static void testResultDecoders(void)
{
    CHECK(strcmp(CueletDiagnosticEventName(kCueletDiagnosticWriteMix),
        "write_mix") == 0);
    CHECK(strcmp(CueletDiagnosticWriteResultName(kCueletRingWriteOK),
        "WRITE_OK") == 0);
    CHECK(strcmp(CueletDiagnosticReadResultName(kCueletRingReadUnpublished),
        "READ_UNPUBLISHED") == 0);
    CHECK(strcmp(CueletDiagnosticTimelineResultName(
        kCueletTimelineOutputTimestampInvalid),
        "TIMELINE_OUTPUT_INVALID") == 0);
    CHECK(strcmp(CueletDiagnosticReadResultName(UINT32_MAX),
        "READ_UNKNOWN") == 0);
}

static void testLiveAllZeroCounterPattern(void)
{
    CueletDiagnosticInitialize();
    CueletDiagnosticSetStateTokens(
        UINT64_C(0x2ebdb30ac10a0c62),
        UINT64_C(0x5cd4dd6dec7e6309));

    CueletDiagnosticRecordData write = {
        .frameCount = 512,
        .payloadNonzeroFrameCount = 512,
        .payloadChecksum = UINT64_C(0xa102030405060708),
        .payloadPeakLeft = 0.25F,
        .payloadPeakRight = 0.25F,
        .timelineStatus = kCueletTimelineOK,
        .ringWriteStatus = kCueletRingWriteOK,
        .ringWriteAcceptedFrames = 512,
        .writeInputFrames = 512,
        .writeValidatedFrames = 512,
        .writeStoredPayloadFrames = 512,
        .writePublishedTagFrames = 512,
        .outputStartFrame = 123064,
        .publishedGeneration = 2,
        .firstRingSlot = 8680,
        .finalRingSlot = 9191,
        .firstPublishedAbsoluteTag = 123064,
        .finalPublishedAbsoluteTag = 123575,
    };
    CueletDiagnosticRecord(kCueletDiagnosticWriteMix, &write);

    CueletDiagnosticRecordData read = {
        .frameCount = 512,
        .inputStartFrame = 122880,
        .sourceStartFrame = 0,
        .timelineStatus = kCueletTimelineOutputTimestampInvalid,
        .ringReadStatus = kCueletRingReadTimelineUninitialized,
        .ringReadFirstRejection = kCueletRingReadTimelineUninitialized,
        .zeroFilledFrameCount = 512,
        .unavailableFrameCount = 512,
    };
    read.readFailureFrameCounts[
        kCueletDiagnosticReadFailureMappingInvalid] = 512;
    CueletDiagnosticRecord(kCueletDiagnosticReadInput, &read);

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.writeMixCallCount == 1);
    CHECK(counters.writeMixNonzeroCallCount == 1);
    CHECK(counters.writeAcceptedFrames == 512);
    CHECK(counters.writePublishedTagFrames == 512);
    CHECK(counters.writePublicationFailures == 0);
    CHECK(counters.readInputCallCount == 1);
    CHECK(counters.readValidFrames == 0);
    CHECK(counters.readZeroFilledFrames == 512);
    CHECK(counters.readMappingInvalidFrames == 512);
    CHECK(counters.readMappingInvalidCalls == 1);
    CHECK(counters.readAllUnavailableCalls == 1);
    CHECK(counters.firstReadFailure.code ==
        kCueletDiagnosticReadFailureMappingInvalid);
    CHECK(counters.firstReadFailure.inputStart == 122880);
    CHECK(counters.lastPublishedWrite.start == 123064);
    CHECK(counters.lastPublishedWrite.frameCount == 512);
    CHECK(counters.lastRead.inputStart == 122880);
    CHECK(counters.lastRead.sourceStart == 0);
    CHECK(counters.lastRead.resultCode ==
        kCueletDiagnosticReadFailureMappingInvalid);
    CHECK(counters.writePublishedTagFrames <=
        counters.writeStoredPayloadFrames);
    CHECK(counters.writeStoredPayloadFrames <= counters.writeValidatedFrames);
    CHECK(counters.writeValidatedFrames <= counters.writeInputFrames);
    CHECK(counters.readValidFrames + counters.readZeroFilledFrames ==
        counters.readRequestedFrames);
    const uint64_t rejected =
        counters.readNotYetWrittenFrames +
        counters.readOverwrittenFrames +
        counters.readGenerationMismatchFrames +
        counters.readAbsoluteTagMismatchFrames +
        counters.readUnpublishedFrames +
        counters.readTimelineUninitializedFrames +
        counters.readMappingInvalidFrames +
        counters.readInvalidArgumentFrames +
        counters.readSampleRateResetFrames;
    CHECK(rejected == counters.readZeroFilledFrames);
}

static void testCriticalEventPreservation(void)
{
    CueletDiagnosticInitialize();
    CueletDiagnosticSetStateTokens(UINT64_C(0x1111), UINT64_C(0x2222));
    for (uint32_t index = 0; index < 40; ++index) {
        CueletDiagnosticRecordData write = {
            .frameCount = 512,
            .payloadNonzeroFrameCount = 512,
            .payloadChecksum = UINT64_C(0xabc00000) + index,
            .payloadPeakLeft = 0.25F,
            .payloadPeakRight = 0.25F,
            .timelineStatus = kCueletTimelineOK,
            .ringWriteStatus = kCueletRingWriteOK,
            .ringWriteAcceptedFrames = 512,
            .writeInputFrames = 512,
            .writeValidatedFrames = 512,
            .writeStoredPayloadFrames = 512,
            .writePublishedTagFrames = 512,
            .outputStartFrame = 123064 + (uint64_t)index * 512U,
            .publishedGeneration = 2,
            .firstPublishedAbsoluteTag = 123064 + (uint64_t)index * 512U,
            .finalPublishedAbsoluteTag = 123575 + (uint64_t)index * 512U,
        };
        CueletDiagnosticRecord(kCueletDiagnosticWriteMix, &write);
    }
    for (uint32_t index = 0; index < 40; ++index) {
        CueletDiagnosticRecordData read = {
            .frameCount = 512,
            .inputStartFrame = 122880 + (uint64_t)index * 512U,
            .timelineStatus = kCueletTimelineOutputTimestampInvalid,
            .ringReadStatus = kCueletRingReadTimelineUninitialized,
            .ringReadFirstRejection = kCueletRingReadTimelineUninitialized,
            .zeroFilledFrameCount = 512,
            .unavailableFrameCount = 512,
        };
        read.readFailureFrameCounts[
            kCueletDiagnosticReadFailureMappingInvalid] = 512;
        CueletDiagnosticRecord(kCueletDiagnosticReadInput, &read);
    }
    CueletDiagnosticRecordData ordinary = {0};
    for (uint32_t index = 0;
         index < CUELET_DIAGNOSTIC_EVENT_CAPACITY + 100U; ++index) {
        ordinary.cycleCounter = index;
        CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &ordinary);
    }

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.droppedEventCount >= 100);
    CHECK(counters.criticalWriteCount ==
        CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY);
    CHECK(counters.criticalReadCount ==
        CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY);
    CHECK(counters.criticalEventCount ==
        CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY);
    CHECK(counters.criticalEvents[0].kind ==
        kCueletDiagnosticCriticalNonzeroWrite);
    CHECK(counters.criticalEvents[0].sequence == 0);
    CHECK(counters.criticalEvents[0].absoluteStart == 123064);
    CHECK(counters.criticalEvents[0].payloadChecksum ==
        UINT64_C(0xabc00000));
    CHECK(counters.criticalEvents[
        CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY].kind ==
        kCueletDiagnosticCriticalReadAfterNonzeroWrite);
    CHECK(counters.criticalEvents[
        CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY].sequence == 40);
    CHECK(counters.criticalEvents[
        CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY].resultCode ==
        kCueletDiagnosticReadFailureMappingInvalid);
}

static void testConcurrentPublicationAndSnapshot(void)
{
    CueletDiagnosticInitialize();
    CueletDiagnosticSetStateTokens(UINT64_C(0x1111), UINT64_C(0x2222));
    ConcurrentWriterContext first = {.base = 100000};
    ConcurrentWriterContext second = {.base = 200000};
    pthread_t writers[2];
    CHECK(pthread_create(&writers[0], NULL, concurrentWriter, &first) == 0);
    CHECK(pthread_create(&writers[1], NULL, concurrentWriter, &second) == 0);

    CueletDiagnosticSnapshot events[64];
    uint64_t next = 0;
    const size_t snapshotSize = CueletDiagnosticExportSize();
    void* snapshot = calloc(1, snapshotSize);
    CHECK(snapshot != NULL);
    for (uint32_t pass = 0; pass < 250; ++pass) {
        (void)CueletDiagnosticCopy(events, 64, &next);
        if (snapshot != NULL) {
            size_t used = 0;
            CHECK(CueletDiagnosticExportEventSnapshot(
                snapshot, snapshotSize, &used) == noErr);
            CHECK(used >= sizeof(CueletDiagnosticEventExportHeader));
        }
        CueletDiagnosticCounters counters = {0};
        CueletDiagnosticGetCounters(&counters);
        CHECK(counters.stateToken == UINT64_C(0x1111));
        CHECK(counters.ringToken == UINT64_C(0x2222));
    }
    CHECK(pthread_join(writers[0], NULL) == 0);
    CHECK(pthread_join(writers[1], NULL) == 0);
    free(snapshot);

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.nextSequence == 2U * kConcurrentWriterIterations);
    CHECK(counters.availableEventCount == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
    CHECK(counters.droppedEventCount ==
        2U * kConcurrentWriterIterations - CUELET_DIAGNOSTIC_EVENT_CAPACITY);
}

int main(void)
{
    testSchemaBuildAndCounters();
    testSnapshotAndClear();
    testWrapAndDroppedCounter();
    testResultDecoders();
    testLiveAllZeroCounterPattern();
    testCriticalEventPreservation();
    testConcurrentPublicationAndSnapshot();
    if (gFailures != 0) {
        fprintf(stderr,
            "Cuelet diagnostic store: %u failures in %u assertions\n",
            gFailures, gAssertions);
        return 1;
    }
    printf("Cuelet diagnostic store: PASS (%u assertions)\n", gAssertions);
    return 0;
}
