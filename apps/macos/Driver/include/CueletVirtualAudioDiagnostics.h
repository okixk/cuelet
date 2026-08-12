#ifndef CUELET_VIRTUAL_AUDIO_DIAGNOSTICS_H
#define CUELET_VIRTUAL_AUDIO_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <CoreAudio/AudioServerPlugIn.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private device properties used only by Cuelet's diagnostic development build. */
enum {
    kCueletDiagnosticPropertySchema = 'cdsv',
    kCueletDiagnosticPropertyCounters = 'cdct',
    /* 'cdev' is public and 'cdes' is undocumented host-level Core Audio
     * device dispatch. Neither can carry Cuelet's custom CFPropertyList. */
    kCueletDiagnosticPropertyEvents = 'cqev',
    kCueletDiagnosticPropertyEventCount = 'cdec',
    kCueletDiagnosticPropertyClear = 'cdcl',
    kCueletDiagnosticPropertyBuild = 'cdbv',
    kCueletDiagnosticPropertyEnabled = 'cden',
};

#define CUELET_DIAGNOSTIC_SCHEMA_VERSION 4U
#define CUELET_DIAGNOSTIC_EVENT_CAPACITY 8192U
#define CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY 256U
#define CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY 32U
#define CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY 32U
#define CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY \
    (CUELET_DIAGNOSTIC_CRITICAL_WRITE_CAPACITY + \
     CUELET_DIAGNOSTIC_CRITICAL_READ_CAPACITY)
#define CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES 8192U
#define CUELET_DIAGNOSTIC_READ_RESULT_COUNT 16U
#define CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT 8U
#define CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT 16U

typedef enum CueletDiagnosticEventKind {
    kCueletDiagnosticDriverInitialize = 1,
    kCueletDiagnosticStartIO,
    kCueletDiagnosticStopIO,
    kCueletDiagnosticGetZeroTimeStamp,
    kCueletDiagnosticWillDoIOOperation,
    kCueletDiagnosticBeginIOOperation,
    kCueletDiagnosticDoIOOperation,
    kCueletDiagnosticEndIOOperation,
    kCueletDiagnosticWriteMix,
    kCueletDiagnosticReadInput,
    kCueletDiagnosticRingReset,
    kCueletDiagnosticRingWrite,
    kCueletDiagnosticRingRead,
    kCueletDiagnosticProducerRejected,
    kCueletDiagnosticSampleRateChange,
    kCueletDiagnosticStreamActivationChange,
    kCueletDiagnosticPropertySnapshot,
    kCueletDiagnosticEventKindCount,
} CueletDiagnosticEventKind;

typedef enum CueletDiagnosticBufferSelection {
    kCueletDiagnosticBufferNone = 0,
    kCueletDiagnosticBufferMain = 1,
    kCueletDiagnosticBufferSecondary = 2,
} CueletDiagnosticBufferSelection;

typedef enum CueletDiagnosticOperationDisposition {
    kCueletDiagnosticOperationNormal = 0,
    kCueletDiagnosticOperationMainBufferMissing,
    kCueletDiagnosticOperationWrongStream,
    kCueletDiagnosticOperationStreamInactive,
    kCueletDiagnosticOperationClientReaderMissing,
    kCueletDiagnosticOperationUnsupported,
} CueletDiagnosticOperationDisposition;

/* Mutually exclusive frame-level reasons used by the aggregate counter API. */
typedef enum CueletDiagnosticReadFailureCode {
    kCueletDiagnosticReadFailureNone = 0,
    kCueletDiagnosticReadFailureNotYetWritten,
    kCueletDiagnosticReadFailureOverwritten,
    kCueletDiagnosticReadFailureGenerationMismatch,
    kCueletDiagnosticReadFailureAbsoluteTagMismatch,
    kCueletDiagnosticReadFailureUnpublished,
    kCueletDiagnosticReadFailureTimelineUninitialized,
    kCueletDiagnosticReadFailureStreamInactive,
    kCueletDiagnosticReadFailureClientReaderUnavailable,
    kCueletDiagnosticReadFailureMappingInvalid,
    kCueletDiagnosticReadFailureInvalidArgument,
    kCueletDiagnosticReadFailureSampleRateReset,
    kCueletDiagnosticReadFailureCodeCount,
} CueletDiagnosticReadFailureCode;

typedef struct CueletDiagnosticRecordData {
    uint64_t stateToken;
    uint64_t ringToken;
    uint32_t deviceObjectID;
    uint32_t streamObjectID;
    uint32_t clientID;
    uint32_t operationID;
    uint32_t frameCount;
    uint32_t analyzedFrameCount;
    uint64_t cycleCounter;
    uint64_t hostTimeSnapshot;
    Float64 sampleRate;

    uint32_t currentTimeFlags;
    uint32_t currentFrameConversionStatus;
    uint64_t currentSampleTimeBits;
    uint64_t currentSampleFrame;
    uint64_t currentHostTime;
    uint32_t inputTimeFlags;
    uint32_t inputFrameConversionStatus;
    uint64_t inputSampleTimeBits;
    uint64_t inputSampleFrame;
    uint64_t inputHostTime;
    uint32_t outputTimeFlags;
    uint32_t outputFrameConversionStatus;
    uint64_t outputSampleTimeBits;
    uint64_t outputSampleFrame;
    uint64_t outputHostTime;

    uint32_t mainBufferPresent;
    uint32_t secondaryBufferPresent;
    uint32_t selectedBuffer;
    uint32_t bufferSelectionStatus;
    uint32_t operationDisposition;
    uint32_t reservedPayload;
    uint64_t payloadChecksum;
    uint64_t payloadFirstBits;
    uint64_t payloadLastBits;
    Float32 payloadPeakLeft;
    Float32 payloadPeakRight;
    Float32 payloadRMSLeft;
    Float32 payloadRMSRight;
    uint32_t payloadZeroFrameCount;
    uint32_t payloadNonzeroFrameCount;
    uint64_t publishedPayloadChecksum;
    uint64_t publishedPayloadFirstBits;
    uint64_t publishedPayloadLastBits;
    Float32 publishedPayloadPeakLeft;
    Float32 publishedPayloadPeakRight;
    Float32 publishedPayloadRMSLeft;
    Float32 publishedPayloadRMSRight;
    uint32_t publishedPayloadZeroFrameCount;
    uint32_t publishedPayloadNonzeroFrameCount;

    uint64_t writePosition;
    uint64_t readerPosition;
    uint64_t resetGeneration;
    uint64_t timelineSeed;
    uint64_t underrunCount;
    uint64_t overrunCount;
    uint64_t rejectedWriteCount;
    uint64_t runningClientCountBefore;
    uint64_t runningClientCountAfter;
    uint64_t resetGenerationBefore;
    uint64_t resetGenerationAfter;

    uint64_t inputStartFrame;
    uint64_t outputStartFrame;
    uint64_t sourceStartFrame;
    int64_t observedTimelineOffsetFrames;
    uint32_t loopbackDelayFrames;
    uint32_t mappingValid;
    uint32_t timelineStatus;
    uint32_t ringWriteStatus;
    uint32_t ringWriteAcceptedFrames;
    uint32_t writeInputFrames;
    uint32_t writeValidatedFrames;
    uint32_t writeStoredPayloadFrames;
    uint32_t writePublishedTagFrames;
    uint32_t writePublicationFailures;
    uint32_t ringReadStatus;
    uint32_t ringReadFirstRejection;
    uint32_t validFrameCount;
    uint32_t zeroFilledFrameCount;
    uint32_t unavailableFrameCount;
    uint32_t staleFrameCount;
    uint64_t ringReadFirstRejectedFrame;
    uint64_t expectedGeneration;
    uint64_t observedGeneration;
    uint64_t expectedAbsoluteTag;
    uint64_t observedAbsoluteTag;
    uint64_t firstRingSlot;
    uint64_t finalRingSlot;
    uint64_t firstPublishedAbsoluteTag;
    uint64_t finalPublishedAbsoluteTag;
    uint64_t publishedGeneration;
    uint32_t readerInitiallyInitialized;
    uint32_t readerGenerationAdopted;
    uint32_t readMapped;
    uint32_t readGenerationResolved;
    uint32_t readPreRingAccepted;
    uint32_t readRingLookupReached;
    uint32_t readRingLookupFrames;
    uint32_t reservedReadProgress;
    uint32_t readFailureFrameCounts[kCueletDiagnosticReadFailureCodeCount];

    uint32_t producerContention;
    uint32_t readerJump;
    uint32_t writeAccepted;
    uint32_t reserved;
} CueletDiagnosticRecordData;

typedef struct CueletDiagnosticSnapshot {
    uint64_t sequence;
    uint32_t eventKind;
    uint32_t reserved;
    CueletDiagnosticRecordData data;
} CueletDiagnosticSnapshot;

typedef struct CueletDiagnosticSchema {
    uint32_t schemaVersion;
    uint32_t eventSize;
    uint32_t eventCapacity;
    uint32_t maximumAnalyzedFrames;
    uint32_t countersSize;
    uint32_t buildInfoSize;
    uint32_t diagnosticEnabled;
    uint32_t eventSnapshotCapacity;
} CueletDiagnosticSchema;

typedef struct CueletDiagnosticBuildInfo {
    uint32_t versionMajor;
    uint32_t versionMinor;
    uint32_t versionPatch;
    uint32_t buildNumber;
    uint32_t diagnosticEnabled;
    uint32_t schemaVersion;
    uint32_t architecture;
    uint32_t reserved;
} CueletDiagnosticBuildInfo;

typedef struct CueletDiagnosticReadFailureSummary {
    uint64_t sequence;
    uint32_t code;
    uint32_t frameCount;
    uint32_t timelineStatus;
    uint32_t inputTimeFlags;
    uint32_t outputTimeFlags;
    uint32_t clientID;
    uint64_t inputStart;
    uint64_t sourceStart;
    uint64_t expectedGeneration;
    uint64_t observedGeneration;
    uint64_t expectedTag;
    uint64_t observedTag;
    uint64_t slot;
} CueletDiagnosticReadFailureSummary;

typedef struct CueletDiagnosticWriteRangeSummary {
    uint64_t sequence;
    uint64_t start;
    uint32_t frameCount;
    uint32_t reserved;
    uint64_t generation;
    uint64_t firstSlot;
    uint64_t finalSlot;
    uint64_t firstTag;
    uint64_t finalTag;
} CueletDiagnosticWriteRangeSummary;

typedef struct CueletDiagnosticReadRangeSummary {
    uint64_t sequence;
    uint64_t inputStart;
    uint64_t sourceStart;
    uint32_t frameCount;
    uint32_t resultCode;
    uint32_t timelineStatus;
    uint32_t inputTimeFlags;
    uint32_t outputTimeFlags;
    uint32_t clientID;
    uint32_t readMapped;
    uint32_t readGenerationResolved;
    uint32_t readPreRingAccepted;
    uint32_t readRingLookupReached;
    uint32_t readerInitiallyInitialized;
    uint32_t readerGenerationAdopted;
    uint32_t reserved;
    uint64_t expectedGeneration;
    uint64_t observedGeneration;
    uint64_t expectedTag;
    uint64_t observedTag;
    uint64_t firstSlot;
} CueletDiagnosticReadRangeSummary;

typedef enum CueletDiagnosticCriticalKind {
    kCueletDiagnosticCriticalNonzeroWrite = 1,
    kCueletDiagnosticCriticalReadAfterNonzeroWrite = 2,
} CueletDiagnosticCriticalKind;

typedef struct CueletDiagnosticCriticalEvent {
    uint64_t sequence;
    uint32_t kind;
    uint32_t frameCount;
    uint64_t inputSampleFrame;
    uint64_t outputSampleFrame;
    uint64_t absoluteStart;
    uint64_t generation;
    uint64_t firstSlot;
    uint64_t finalSlot;
    uint64_t expectedTag;
    uint64_t observedTag;
    uint64_t observedGeneration;
    uint64_t payloadChecksum;
    Float32 peakLeft;
    Float32 peakRight;
    Float32 rmsLeft;
    Float32 rmsRight;
    uint32_t resultCode;
    uint32_t validFrames;
    uint32_t zeroFilledFrames;
    uint32_t timelineStatus;
    uint32_t inputTimeFlags;
    uint32_t outputTimeFlags;
    uint32_t clientID;
    uint32_t readMapped;
    uint32_t readGenerationResolved;
    uint32_t readPreRingAccepted;
    uint32_t readRingLookupReached;
    uint32_t readerInitiallyInitialized;
    uint32_t readerGenerationAdopted;
    uint32_t reserved;
} CueletDiagnosticCriticalEvent;

typedef struct CueletDiagnosticCounters {
    uint64_t stateToken;
    uint64_t ringToken;
    uint64_t nextSequence;
    uint64_t clearSequence;
    uint64_t availableEventCount;
    uint64_t droppedEventCount;
    uint64_t writeMixCallCount;
    uint64_t writeMixNonzeroCallCount;
    uint64_t writeMixZeroCallCount;
    uint64_t writeRequestedFrames;
    uint64_t writeAcceptedFrames;
    uint64_t writeRejectedFrames;
    uint64_t readInputCallCount;
    uint64_t readRequestedFrames;
    uint64_t readValidFrames;
    uint64_t readZeroFilledFrames;
    uint64_t ringResetCount;
    uint64_t generationChangeCount;
    uint64_t startIOCount;
    uint64_t stopIOCount;
    uint64_t sampleRateChangeCount;
    uint64_t streamActivationChangeCount;
    uint64_t propertySnapshotCount;
    uint64_t writeResultCounts[CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT];
    uint64_t readResultCounts[CUELET_DIAGNOSTIC_READ_RESULT_COUNT];
    uint64_t timelineResultCounts[CUELET_DIAGNOSTIC_TIMELINE_RESULT_COUNT];
    uint64_t writeInputFrames;
    uint64_t writeValidatedFrames;
    uint64_t writeStoredPayloadFrames;
    uint64_t writePublishedTagFrames;
    uint64_t writePublicationFailures;
    uint64_t readOKFrames;
    uint64_t readNotYetWrittenFrames;
    uint64_t readOverwrittenFrames;
    uint64_t readGenerationMismatchFrames;
    uint64_t readAbsoluteTagMismatchFrames;
    uint64_t readUnpublishedFrames;
    uint64_t readTimelineUninitializedFrames;
    uint64_t readStreamInactiveFrames;
    uint64_t readClientReaderUnavailableFrames;
    uint64_t readMappingInvalidFrames;
    uint64_t readInvalidArgumentFrames;
    uint64_t readSampleRateResetFrames;
    uint64_t readPartialValidFrames;
    uint64_t readOKCalls;
    uint64_t readPartialCalls;
    uint64_t readAllUnavailableCalls;
    uint64_t readMappingInvalidCalls;
    uint64_t readMappedCalls;
    uint64_t readGenerationResolvedCalls;
    uint64_t readPreRingAcceptedCalls;
    uint64_t readRingLookupCalls;
    uint64_t readRingLookupFrames;
    uint64_t readMappedButNoGenerationCalls;
    uint64_t readGenerationButNoRingCalls;
    uint64_t readRingLookupUnavailableCalls;
    CueletDiagnosticReadFailureSummary firstReadFailure;
    CueletDiagnosticReadFailureSummary lastReadFailure;
    CueletDiagnosticWriteRangeSummary lastAcceptedWrite;
    CueletDiagnosticWriteRangeSummary lastPublishedWrite;
    CueletDiagnosticReadRangeSummary lastRead;
    uint32_t criticalWriteCount;
    uint32_t criticalReadCount;
    uint32_t criticalEventCount;
    uint32_t criticalEventCapacity;
    CueletDiagnosticCriticalEvent
        criticalEvents[CUELET_DIAGNOSTIC_CRITICAL_EVENT_CAPACITY];
} CueletDiagnosticCounters;

typedef struct CueletDiagnosticEventExportHeader {
    uint32_t schemaVersion;
    uint32_t eventRecordSize;
    uint32_t snapshotCapacity;
    uint32_t returnedEventCount;
    uint64_t oldestAvailableSequence;
    uint64_t newestAvailableSequence;
    uint64_t firstReturnedSequence;
    uint64_t lastReturnedSequence;
    uint64_t availableEventCount;
    uint64_t droppedEventCount;
} CueletDiagnosticEventExportHeader;

#ifdef CUELET_AUDIO_DIAGNOSTICS

void CueletDiagnosticInitialize(void);
void CueletDiagnosticSetStateTokens(uint64_t stateToken, uint64_t ringToken);
void CueletDiagnosticClear(void);
void CueletDiagnosticRecord(
    CueletDiagnosticEventKind kind,
    const CueletDiagnosticRecordData* data);
size_t CueletDiagnosticCopy(
    CueletDiagnosticSnapshot* output,
    size_t capacity,
    uint64_t* nextSequenceInOut);
uint64_t CueletDiagnosticDroppedCount(void);
uint64_t CueletDiagnosticEventCount(void);
void CueletDiagnosticGetSchema(CueletDiagnosticSchema* schemaOut);
void CueletDiagnosticGetBuildInfo(CueletDiagnosticBuildInfo* buildOut);
void CueletDiagnosticGetCounters(CueletDiagnosticCounters* countersOut);
size_t CueletDiagnosticExportSize(void);
size_t CueletDiagnosticEventSnapshotSizeForCount(size_t eventCount);
OSStatus CueletDiagnosticExportEventSnapshot(
    void* output,
    size_t outputSize,
    size_t* usedSizeOut);
const char* CueletDiagnosticEventName(uint32_t kind);
const char* CueletDiagnosticWriteResultName(uint32_t status);
const char* CueletDiagnosticReadResultName(uint32_t status);
const char* CueletDiagnosticTimelineResultName(uint32_t status);

#else

static inline void CueletDiagnosticInitialize(void) {}
static inline void CueletDiagnosticSetStateTokens(uint64_t state, uint64_t ring)
{ (void)state; (void)ring; }
static inline void CueletDiagnosticClear(void) {}
static inline void CueletDiagnosticRecord(
    CueletDiagnosticEventKind kind,
    const CueletDiagnosticRecordData* data)
{ (void)kind; (void)data; }
static inline size_t CueletDiagnosticCopy(
    CueletDiagnosticSnapshot* output,
    size_t capacity,
    uint64_t* nextSequenceInOut)
{ (void)output; (void)capacity; (void)nextSequenceInOut; return 0; }
static inline uint64_t CueletDiagnosticDroppedCount(void) { return 0; }
static inline uint64_t CueletDiagnosticEventCount(void) { return 0; }
static inline void CueletDiagnosticGetSchema(CueletDiagnosticSchema* output)
{ if (output != NULL) { *output = (CueletDiagnosticSchema){0}; } }
static inline void CueletDiagnosticGetBuildInfo(CueletDiagnosticBuildInfo* output)
{ if (output != NULL) { *output = (CueletDiagnosticBuildInfo){0}; } }
static inline void CueletDiagnosticGetCounters(CueletDiagnosticCounters* output)
{ if (output != NULL) { *output = (CueletDiagnosticCounters){0}; } }
static inline size_t CueletDiagnosticExportSize(void) { return 0; }
static inline size_t CueletDiagnosticEventSnapshotSizeForCount(size_t count)
{ (void)count; return 0; }
static inline OSStatus CueletDiagnosticExportEventSnapshot(
    void* output, size_t outputSize, size_t* usedSizeOut)
{ (void)output; (void)outputSize;
  (void)usedSizeOut; return kAudioHardwareUnsupportedOperationError; }
static inline const char* CueletDiagnosticEventName(uint32_t kind)
{ (void)kind; return "diagnostics_disabled"; }
static inline const char* CueletDiagnosticWriteResultName(uint32_t status)
{ (void)status; return "diagnostics_disabled"; }
static inline const char* CueletDiagnosticReadResultName(uint32_t status)
{ (void)status; return "diagnostics_disabled"; }
static inline const char* CueletDiagnosticTimelineResultName(uint32_t status)
{ (void)status; return "diagnostics_disabled"; }

#endif

#ifdef __cplusplus
}
#endif

#endif
