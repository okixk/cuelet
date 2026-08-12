/*
 * Cuelet's first Audio Server Driver Plug-in.
 *
 * The object/property layout follows Apple's "Creating an Audio Server Driver
 * Plug-in" sample. The implementation is intentionally fixed: one device, one
 * stereo input stream, one stereo output stream, and four controls whose values
 * are applied to the actual loopback samples.
 */

#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreFoundation/CFPlugInCOM.h>
#include <dispatch/dispatch.h>
#include <limits.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CUELET_FACTORY_EXPORT __attribute__((visibility("default")))
#define CUELET_LATENCY_FRAMES 128U
#define CUELET_SAFETY_OFFSET_FRAMES 32U
#define CUELET_LOOPBACK_DELAY_MULTIPLIER 1U
#define CUELET_MIN_VOLUME_DB (-96.0F)
#define CUELET_MAX_VOLUME_DB 0.0F

#ifdef CUELET_AUDIO_DIAGNOSTICS
_Static_assert(
    kCueletDiagnosticPropertyEvents != 'cdev',
    "diagnostic selectors must not collide with Core Audio properties");
_Static_assert(
    kCueletDiagnosticPropertyEvents != 'cdes',
    "diagnostic selectors must not collide with host device properties");

static const AudioServerPlugInCustomPropertyInfo kCueletDiagnosticProperties[] = {
    { kCueletDiagnosticPropertySchema,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyCounters,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyEvents,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyEventCount,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyClear,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyBuild,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
    { kCueletDiagnosticPropertyEnabled,
      kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,
      kAudioServerPlugInCustomPropertyDataTypeNone },
};
#endif

typedef struct CueletClientSlot {
    _Atomic uint32_t clientID;
    CueletRingReader reader;
} CueletClientSlot;

static pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint32_t gRefCount = 0;
static AudioServerPlugInHostRef gHost = NULL;
static CueletIOState gIOState;
static CueletClientSlot gClients[CUELET_MAX_CLIENTS];
static _Atomic Float64 gSampleRate = 48000.0;
static _Atomic Float64 gHostTicksPerFrame = 0.0;
static _Atomic uint64_t gAnchorHostTime = 0;
static _Atomic uint64_t gTimelineSeed = 1;
static _Atomic uint32_t gTimelineMappingState = 0;
static _Atomic int64_t gTimelineOffsetFrames = 0;
static _Atomic uint32_t gLoopbackDelayFrames = 0;
static _Atomic uint64_t gTimelineInputOriginFrames = 0;
static _Atomic uint64_t gTimelineOutputOriginFrames = 0;
static _Atomic uint64_t gTimelineCalibrationGeneration = 0;
static _Atomic uint64_t gTimelineCalibrationSampleRateBits = 0;
static _Atomic Float32 gInputVolume = 1.0F;
static _Atomic Float32 gOutputVolume = 1.0F;
static _Atomic bool gInputMuted = false;
static _Atomic bool gOutputMuted = false;
static bool gInitialized = false;

/*
 * Core Audio can invoke ReadInput with only mInputTime valid and WriteMix with
 * only mOutputTime valid. These two single-writer latches pair authoritative
 * observations by device I/O cycle without requiring both timestamps in one
 * callback. Publication is a bounded nonblocking sequence protocol.
 */
typedef struct CueletTimelineObservation {
    _Atomic uint64_t sequence;
    _Atomic uint64_t cycleCounter;
    _Atomic uint64_t sampleFrame;
    _Atomic uint64_t generation;
    _Atomic uint64_t sampleRateBits;
    _Atomic uint32_t nominalFrameSize;
} CueletTimelineObservation;

typedef struct CueletTimelineObservationValue {
    uint64_t cycleCounter;
    uint64_t sampleFrame;
    uint64_t generation;
    uint64_t sampleRateBits;
    uint32_t nominalFrameSize;
} CueletTimelineObservationValue;

static CueletTimelineObservation gInputTimelineObservation;
static CueletTimelineObservation gOutputTimelineObservation;

static uint64_t CueletDiagnosticTokenForPointer(const void* pointer)
{
    uint64_t value = (uint64_t)(uintptr_t)pointer;
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33;
    return value == 0 ? 1 : value;
}

static uint64_t CueletFloat64Bits(Float64 value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void CueletRecordTimestamp(
    const AudioTimeStamp* timestamp,
    CueletTimelineStatus invalidStatus,
    uint32_t* flagsOut,
    uint64_t* sampleBitsOut,
    uint64_t* sampleFrameOut,
    uint32_t* conversionStatusOut,
    uint64_t* hostTimeOut)
{
    *flagsOut = timestamp == NULL ? 0 : timestamp->mFlags;
    *sampleBitsOut = timestamp == NULL
        ? 0
        : CueletFloat64Bits(timestamp->mSampleTime);
    *sampleFrameOut = 0;
    *conversionStatusOut = (uint32_t)CueletSampleFrameFromTimestamp(
        timestamp,
        invalidStatus,
        sampleFrameOut);
    *hostTimeOut = timestamp == NULL ? 0 : timestamp->mHostTime;
}

static void CueletPopulateCycleDiagnostics(
    CueletDiagnosticRecordData* data,
    const AudioServerPlugInIOCycleInfo* cycle)
{
    if (cycle == NULL) {
        data->currentFrameConversionStatus = kCueletTimelineInvalidArgument;
        data->inputFrameConversionStatus = kCueletTimelineInvalidArgument;
        data->outputFrameConversionStatus = kCueletTimelineInvalidArgument;
        return;
    }
    data->cycleCounter = cycle->mIOCycleCounter;
    CueletRecordTimestamp(
        &cycle->mCurrentTime,
        kCueletTimelineInvalidArgument,
        &data->currentTimeFlags,
        &data->currentSampleTimeBits,
        &data->currentSampleFrame,
        &data->currentFrameConversionStatus,
        &data->currentHostTime);
    CueletRecordTimestamp(
        &cycle->mInputTime,
        kCueletTimelineInputTimestampInvalid,
        &data->inputTimeFlags,
        &data->inputSampleTimeBits,
        &data->inputSampleFrame,
        &data->inputFrameConversionStatus,
        &data->inputHostTime);
    CueletRecordTimestamp(
        &cycle->mOutputTime,
        kCueletTimelineOutputTimestampInvalid,
        &data->outputTimeFlags,
        &data->outputSampleTimeBits,
        &data->outputSampleFrame,
        &data->outputFrameConversionStatus,
        &data->outputHostTime);
}

static void CueletAnalyzePayload(
    CueletDiagnosticRecordData* data,
    const Float32* samples,
    uint32_t frameCount)
{
    uint64_t checksum = UINT64_C(1469598103934665603);
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    const uint32_t analyzed = frameCount > CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES
        ? CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES
        : frameCount;
    data->analyzedFrameCount = analyzed;
    if (samples == NULL || analyzed == 0) {
        data->payloadChecksum = checksum;
        return;
    }
    memcpy(&data->payloadFirstBits, samples, sizeof(data->payloadFirstBits));
    for (uint32_t frame = 0; frame < analyzed; ++frame) {
        uint32_t leftBits = 0;
        uint32_t rightBits = 0;
        memcpy(&leftBits, &samples[frame * 2], sizeof(leftBits));
        memcpy(&rightBits, &samples[frame * 2 + 1], sizeof(rightBits));
        const uint64_t packed = (uint64_t)leftBits | ((uint64_t)rightBits << 32);
        checksum ^= packed;
        checksum *= UINT64_C(1099511628211);
        const Float32 left = samples[frame * 2];
        const Float32 right = samples[frame * 2 + 1];
        data->payloadPeakLeft = fmaxf(data->payloadPeakLeft, fabsf(left));
        data->payloadPeakRight = fmaxf(data->payloadPeakRight, fabsf(right));
        leftSquares += (double)left * (double)left;
        rightSquares += (double)right * (double)right;
        if (left == 0.0F && right == 0.0F) {
            ++data->payloadZeroFrameCount;
        } else {
            ++data->payloadNonzeroFrameCount;
        }
    }
    memcpy(
        &data->payloadLastBits,
        &samples[(analyzed - 1) * 2],
        sizeof(data->payloadLastBits));
    data->payloadChecksum = checksum;
    data->payloadRMSLeft = (Float32)sqrt(leftSquares / analyzed);
    data->payloadRMSRight = (Float32)sqrt(rightSquares / analyzed);
}

static void CueletResetTimelineMapping(void)
{
    atomic_store_explicit(&gTimelineMappingState, 0, memory_order_release);
    atomic_store_explicit(&gTimelineOffsetFrames, 0, memory_order_relaxed);
    atomic_store_explicit(&gLoopbackDelayFrames, 0, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineInputOriginFrames, 0, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineOutputOriginFrames, 0, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineCalibrationGeneration, 0, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineCalibrationSampleRateBits, 0, memory_order_relaxed);
    atomic_store_explicit(
        &gInputTimelineObservation.sequence, 0, memory_order_release);
    atomic_store_explicit(
        &gOutputTimelineObservation.sequence, 0, memory_order_release);
}

static void CueletPublishTimelineObservation(
    CueletTimelineObservation* observation,
    const CueletTimelineObservationValue* value)
{
    uint64_t sequence = atomic_load_explicit(
        &observation->sequence, memory_order_acquire);
    if ((sequence & 1U) != 0 || sequence > UINT64_MAX - 2) {
        return;
    }
    if (!atomic_compare_exchange_strong_explicit(
            &observation->sequence,
            &sequence,
            sequence + 1,
            memory_order_acq_rel,
            memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(
        &observation->cycleCounter, value->cycleCounter, memory_order_relaxed);
    atomic_store_explicit(
        &observation->sampleFrame, value->sampleFrame, memory_order_relaxed);
    atomic_store_explicit(
        &observation->generation, value->generation, memory_order_relaxed);
    atomic_store_explicit(
        &observation->sampleRateBits, value->sampleRateBits, memory_order_relaxed);
    atomic_store_explicit(
        &observation->nominalFrameSize,
        value->nominalFrameSize,
        memory_order_relaxed);
    atomic_store_explicit(
        &observation->sequence, sequence + 2, memory_order_release);
}

static bool CueletLoadTimelineObservation(
    const CueletTimelineObservation* observation,
    CueletTimelineObservationValue* valueOut)
{
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        const uint64_t before = atomic_load_explicit(
            &observation->sequence, memory_order_acquire);
        if (before == 0 || (before & 1U) != 0) {
            continue;
        }
        CueletTimelineObservationValue value = {
            .cycleCounter = atomic_load_explicit(
                &observation->cycleCounter, memory_order_relaxed),
            .sampleFrame = atomic_load_explicit(
                &observation->sampleFrame, memory_order_relaxed),
            .generation = atomic_load_explicit(
                &observation->generation, memory_order_relaxed),
            .sampleRateBits = atomic_load_explicit(
                &observation->sampleRateBits, memory_order_relaxed),
            .nominalFrameSize = atomic_load_explicit(
                &observation->nominalFrameSize, memory_order_relaxed),
        };
        const uint64_t after = atomic_load_explicit(
            &observation->sequence, memory_order_acquire);
        if (before == after && (after & 1U) == 0) {
            *valueOut = value;
            return true;
        }
    }
    return false;
}

static CueletTimelineStatus CueletPublishTimelineCalibration(
    uint64_t inputStart,
    uint64_t outputStart,
    uint32_t nominalFrameSize,
    uint64_t generation,
    uint64_t sampleRateBits)
{
    int64_t observedOffset = 0;
    if (outputStart >= inputStart) {
        const uint64_t difference = outputStart - inputStart;
        if (difference > (uint64_t)INT64_MAX) {
            return kCueletTimelineSampleTimeOverflow;
        }
        observedOffset = (int64_t)difference;
    } else {
        const uint64_t difference = inputStart - outputStart;
        if (difference > (uint64_t)INT64_MAX) {
            return kCueletTimelineSampleTimeOverflow;
        }
        observedOffset = -(int64_t)difference;
    }

    const uint64_t delay64 = (uint64_t)nominalFrameSize *
        CUELET_LOOPBACK_DELAY_MULTIPLIER;
    const uint32_t delay = delay64 > UINT32_MAX
        ? UINT32_MAX
        : (uint32_t)delay64;

    const uint32_t state = atomic_load_explicit(
        &gTimelineMappingState, memory_order_acquire);
    if (state == 2) {
        const uint64_t calibratedGeneration = atomic_load_explicit(
            &gTimelineCalibrationGeneration, memory_order_relaxed);
        const uint64_t calibratedRate = atomic_load_explicit(
            &gTimelineCalibrationSampleRateBits, memory_order_relaxed);
        return calibratedGeneration == generation &&
                calibratedRate == sampleRateBits
            ? kCueletTimelineOK
            : kCueletTimelineUninitialized;
    }
    if (state != 0) {
        return kCueletTimelineUninitialized;
    }

    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(
            &gTimelineMappingState,
            &expected,
            1,
            memory_order_acq_rel,
            memory_order_acquire)) {
        if (expected != 2) {
            return kCueletTimelineUninitialized;
        }
        const uint64_t calibratedGeneration = atomic_load_explicit(
            &gTimelineCalibrationGeneration, memory_order_relaxed);
        const uint64_t calibratedRate = atomic_load_explicit(
            &gTimelineCalibrationSampleRateBits, memory_order_relaxed);
        return calibratedGeneration == generation &&
                calibratedRate == sampleRateBits
            ? kCueletTimelineOK
            : kCueletTimelineUninitialized;
    }
    atomic_store_explicit(
        &gTimelineInputOriginFrames, inputStart, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineOutputOriginFrames, outputStart, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineOffsetFrames, observedOffset, memory_order_relaxed);
    atomic_store_explicit(
        &gLoopbackDelayFrames, delay, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineCalibrationGeneration, generation, memory_order_relaxed);
    atomic_store_explicit(
        &gTimelineCalibrationSampleRateBits,
        sampleRateBits,
        memory_order_relaxed);
    /* Release-publish only after every calibration field is complete. */
    atomic_store_explicit(&gTimelineMappingState, 2, memory_order_release);
    return kCueletTimelineOK;
}

static void CueletObserveInputTimeline(
    const AudioServerPlugInIOCycleInfo* cycle,
    uint64_t inputStart,
    uint32_t frameCount)
{
    if (cycle == NULL) {
        return;
    }
    const uint64_t generation = CueletRingGeneration(&gIOState.ring);
    const uint64_t sampleRateBits = CueletFloat64Bits(atomic_load_explicit(
        &gSampleRate, memory_order_relaxed));
    const uint32_t nominal = cycle->mNominalIOBufferFrameSize != 0
        ? cycle->mNominalIOBufferFrameSize
        : frameCount;
    const CueletTimelineObservationValue input = {
        .cycleCounter = cycle->mIOCycleCounter,
        .sampleFrame = inputStart,
        .generation = generation,
        .sampleRateBits = sampleRateBits,
        .nominalFrameSize = nominal,
    };
    CueletPublishTimelineObservation(&gInputTimelineObservation, &input);

    uint64_t outputStart = 0;
    if (CueletSampleFrameFromTimestamp(
            &cycle->mOutputTime,
            kCueletTimelineOutputTimestampInvalid,
            &outputStart) == kCueletTimelineOK) {
        (void)CueletPublishTimelineCalibration(
            inputStart,
            outputStart,
            nominal,
            generation,
            sampleRateBits);
        return;
    }
    CueletTimelineObservationValue output = {0};
    if (CueletLoadTimelineObservation(
            &gOutputTimelineObservation, &output) &&
        output.cycleCounter == cycle->mIOCycleCounter &&
        output.generation == generation &&
        output.sampleRateBits == sampleRateBits) {
        (void)CueletPublishTimelineCalibration(
            inputStart,
            output.sampleFrame,
            nominal != 0 ? nominal : output.nominalFrameSize,
            generation,
            sampleRateBits);
    }
}

static void CueletObserveOutputTimeline(
    const AudioServerPlugInIOCycleInfo* cycle,
    uint64_t outputStart,
    uint32_t frameCount)
{
    if (cycle == NULL) {
        return;
    }
    const uint64_t generation = CueletRingGeneration(&gIOState.ring);
    const uint64_t sampleRateBits = CueletFloat64Bits(atomic_load_explicit(
        &gSampleRate, memory_order_relaxed));
    const uint32_t nominal = cycle->mNominalIOBufferFrameSize != 0
        ? cycle->mNominalIOBufferFrameSize
        : frameCount;
    const CueletTimelineObservationValue output = {
        .cycleCounter = cycle->mIOCycleCounter,
        .sampleFrame = outputStart,
        .generation = generation,
        .sampleRateBits = sampleRateBits,
        .nominalFrameSize = nominal,
    };
    CueletPublishTimelineObservation(&gOutputTimelineObservation, &output);

    uint64_t inputStart = 0;
    if (CueletSampleFrameFromTimestamp(
            &cycle->mInputTime,
            kCueletTimelineInputTimestampInvalid,
            &inputStart) == kCueletTimelineOK) {
        (void)CueletPublishTimelineCalibration(
            inputStart,
            outputStart,
            nominal,
            generation,
            sampleRateBits);
        return;
    }
    CueletTimelineObservationValue input = {0};
    if (CueletLoadTimelineObservation(
            &gInputTimelineObservation, &input) &&
        input.cycleCounter == cycle->mIOCycleCounter &&
        input.generation == generation &&
        input.sampleRateBits == sampleRateBits) {
        (void)CueletPublishTimelineCalibration(
            input.sampleFrame,
            outputStart,
            nominal != 0 ? nominal : input.nominalFrameSize,
            generation,
            sampleRateBits);
    }
}

static CueletTimelineStatus CueletResolveReadTimelineMapping(
    const AudioServerPlugInIOCycleInfo* cycle,
    uint32_t frameCount,
    uint64_t* inputStartOut,
    uint64_t* outputStartOut,
    uint64_t* sourceStartOut,
    int64_t* observedOffsetOut,
    uint32_t* loopbackDelayOut)
{
    uint64_t inputStart = 0;
    uint64_t outputStart = 0;
    if (cycle == NULL) {
        return kCueletTimelineInvalidArgument;
    }
    CueletTimelineStatus status = CueletSampleFrameFromTimestamp(
        &cycle->mInputTime,
        kCueletTimelineInputTimestampInvalid,
        &inputStart);
    if (status != kCueletTimelineOK) {
        return status;
    }
    if (inputStartOut != NULL) {
        *inputStartOut = inputStart;
    }
    CueletObserveInputTimeline(cycle, inputStart, frameCount);

    const uint32_t state = atomic_load_explicit(
        &gTimelineMappingState, memory_order_acquire);
    if (state != 2) {
        return kCueletTimelineUninitialized;
    }

    const uint64_t generation = CueletRingGeneration(&gIOState.ring);
    const uint64_t sampleRateBits = CueletFloat64Bits(atomic_load_explicit(
        &gSampleRate, memory_order_relaxed));
    if (atomic_load_explicit(
            &gTimelineCalibrationGeneration,
            memory_order_relaxed) != generation ||
        atomic_load_explicit(
            &gTimelineCalibrationSampleRateBits,
            memory_order_relaxed) != sampleRateBits) {
        return kCueletTimelineUninitialized;
    }

    const int64_t fixedOffset = atomic_load_explicit(
        &gTimelineOffsetFrames,
        memory_order_acquire);
    const uint32_t fixedDelay = atomic_load_explicit(
        &gLoopbackDelayFrames,
        memory_order_acquire);
    if (inputStart > (uint64_t)INT64_MAX) {
        return kCueletTimelineSampleTimeOverflow;
    }
    int64_t sourceSigned = 0;
    if (__builtin_add_overflow(
            (int64_t)inputStart,
            fixedOffset,
            &sourceSigned) ||
        __builtin_sub_overflow(
            sourceSigned,
            (int64_t)fixedDelay,
            &sourceSigned)) {
        return kCueletTimelineSampleTimeOverflow;
    }
    if (sourceSigned < 0) {
        return kCueletTimelineNegativeSourceRange;
    }
    if (sourceStartOut != NULL) {
        *sourceStartOut = (uint64_t)sourceSigned;
    }
    int64_t outputSigned = 0;
    if (__builtin_add_overflow(
            (int64_t)inputStart,
            fixedOffset,
            &outputSigned) ||
        outputSigned < 0) {
        return kCueletTimelineSampleTimeOverflow;
    }
    outputStart = (uint64_t)outputSigned;
    if (outputStartOut != NULL) {
        *outputStartOut = outputStart;
    }
    if (observedOffsetOut != NULL) {
        *observedOffsetOut = fixedOffset;
    }
    if (loopbackDelayOut != NULL) {
        *loopbackDelayOut = fixedDelay;
    }
    return kCueletTimelineOK;
}

static void CueletRecordIOEvent(
    CueletDiagnosticEventKind kind,
    AudioObjectID streamObjectID,
    uint32_t clientID,
    uint32_t operationID,
    uint32_t frameCount,
    const AudioServerPlugInIOCycleInfo* cycle,
    uint64_t writePosition,
    uint64_t readerPosition,
    uint32_t producerContention,
    uint32_t readerJump,
    uint32_t writeAccepted)
{
    CueletDiagnosticRecordData data = {0};
    data.hostTimeSnapshot = mach_absolute_time();
    data.deviceObjectID = kCueletObjectDevice;
    data.operationID = operationID;
    data.streamObjectID = streamObjectID;
    data.clientID = clientID;
    data.frameCount = frameCount;
    data.sampleRate = atomic_load_explicit(&gSampleRate, memory_order_relaxed);
    data.writePosition = writePosition;
    data.readerPosition = readerPosition;
    data.resetGeneration = atomic_load_explicit(
        &gIOState.ring.resetGeneration,
        memory_order_relaxed);
    data.timelineSeed = atomic_load_explicit(
        &gTimelineSeed,
        memory_order_relaxed);
    data.underrunCount = atomic_load_explicit(
        &gIOState.ring.underrunCount,
        memory_order_relaxed);
    data.overrunCount = atomic_load_explicit(
        &gIOState.ring.overrunCount,
        memory_order_relaxed);
    data.rejectedWriteCount = atomic_load_explicit(
        &gIOState.ring.rejectedWriteCount,
        memory_order_relaxed);
    data.producerContention = producerContention;
    data.readerJump = readerJump;
    data.writeAccepted = writeAccepted;
    CueletPopulateCycleDiagnostics(&data, cycle);
    CueletDiagnosticRecord(kind, &data);
}

static void CueletRecordTimelineEvent(
    AudioObjectID streamObjectID,
    uint32_t clientID,
    uint32_t operationID,
    uint32_t frameCount,
    const AudioServerPlugInIOCycleInfo* cycle,
    CueletTimelineStatus timelineStatus,
    uint64_t inputStart,
    uint64_t outputStart,
    uint64_t sourceStart,
    int64_t observedOffset,
    uint32_t loopbackDelay,
    CueletRingReadResult readResult,
    CueletRingWriteResult writeResult,
    const Float32* samples,
    const void* mainBuffer,
    const void* secondaryBuffer,
    CueletDiagnosticBufferSelection selectedBuffer,
    CueletDiagnosticOperationDisposition disposition,
    const CueletDiagnosticRecordData* incomingPayload)
{
#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticRecordData data = {0};
    data.hostTimeSnapshot = mach_absolute_time();
    data.deviceObjectID = kCueletObjectDevice;
    data.operationID = operationID;
    data.streamObjectID = streamObjectID;
    data.clientID = clientID;
    data.frameCount = frameCount;
    data.sampleRate = atomic_load_explicit(&gSampleRate, memory_order_relaxed);
    data.writePosition = CueletRingLastWriteEnd(&gIOState.ring);
    data.resetGeneration = CueletRingGeneration(&gIOState.ring);
    data.inputStartFrame = inputStart;
    data.outputStartFrame = outputStart;
    data.sourceStartFrame = sourceStart;
    data.observedTimelineOffsetFrames = observedOffset;
    data.loopbackDelayFrames = loopbackDelay;
    data.mappingValid = timelineStatus == kCueletTimelineOK ? 1U : 0U;
    data.validFrameCount = readResult.validFrames;
    data.unavailableFrameCount = readResult.unavailableFrames;
    data.staleFrameCount = readResult.staleFrames;
    data.timelineStatus = (uint32_t)timelineStatus;
    data.ringReadStatus = (uint32_t)readResult.status;
    data.ringReadFirstRejection =
        (uint32_t)readResult.firstRejectionReason;
    data.ringReadFirstRejectedFrame = readResult.firstRejectedFrame;
    data.zeroFilledFrameCount = frameCount - readResult.validFrames;
    data.expectedGeneration = readResult.expectedGeneration;
    data.observedGeneration = readResult.observedGeneration;
    data.expectedAbsoluteTag = readResult.expectedFrame;
    data.observedAbsoluteTag = readResult.observedFrame;
    data.firstRingSlot = operationID == kAudioServerPlugInIOOperationWriteMix
        ? writeResult.firstSlot
        : readResult.firstSlot;
    data.finalRingSlot = operationID == kAudioServerPlugInIOOperationWriteMix
        ? writeResult.finalSlot
        : readResult.finalSlot;
    data.firstPublishedAbsoluteTag = writeResult.firstPublishedFrame;
    data.finalPublishedAbsoluteTag = writeResult.finalPublishedFrame;
    data.publishedGeneration = writeResult.generation;
    data.readerInitiallyInitialized = readResult.readerInitiallyInitialized;
    data.readerGenerationAdopted = readResult.readerGenerationAdopted;
    data.readMapped = timelineStatus == kCueletTimelineOK ? 1U : 0U;
    data.readGenerationResolved = readResult.generationResolved;
    data.readPreRingAccepted = readResult.preRingAccepted;
    data.readRingLookupReached = readResult.ringLookupReached;
    data.readRingLookupFrames = readResult.ringLookupFrames;
    data.ringWriteStatus = (uint32_t)writeResult.status;
    data.ringWriteAcceptedFrames = writeResult.acceptedFrames;
    if (operationID == kAudioServerPlugInIOOperationWriteMix) {
        data.writeInputFrames = frameCount;
        data.writeValidatedFrames =
            timelineStatus == kCueletTimelineOK &&
                disposition == kCueletDiagnosticOperationNormal
            ? frameCount : 0;
        /* CueletRingWriteAtDetailed publishes each absolute tag only after
         * storing payload and generation metadata. Its accepted count is
         * therefore also the exact stored and release-published count. */
        data.writeStoredPayloadFrames = writeResult.acceptedFrames;
        data.writePublishedTagFrames = writeResult.acceptedFrames;
        data.writePublicationFailures = data.writeValidatedFrames >=
                data.writePublishedTagFrames
            ? data.writeValidatedFrames - data.writePublishedTagFrames : 0;
    } else if (operationID == kAudioServerPlugInIOOperationReadInput) {
        for (uint32_t status = 0;
             status < kCueletRingReadStatusCount; ++status) {
            CueletDiagnosticReadFailureCode code =
                kCueletDiagnosticReadFailureNone;
            switch ((CueletRingReadStatus)status) {
            case kCueletRingReadNotYetWritten:
                code = kCueletDiagnosticReadFailureNotYetWritten;
                break;
            case kCueletRingReadOverwritten:
                code = kCueletDiagnosticReadFailureOverwritten;
                break;
            case kCueletRingReadGenerationMismatch:
                code = kCueletDiagnosticReadFailureGenerationMismatch;
                break;
            case kCueletRingReadAbsoluteFrameMismatch:
                code = kCueletDiagnosticReadFailureAbsoluteTagMismatch;
                break;
            case kCueletRingReadUnpublished:
                code = kCueletDiagnosticReadFailureUnpublished;
                break;
            case kCueletRingReadSampleRateReset:
                code = kCueletDiagnosticReadFailureSampleRateReset;
                break;
            case kCueletRingReadTimelineUninitialized:
                code = kCueletDiagnosticReadFailureTimelineUninitialized;
                break;
            case kCueletRingReadMappingInvalid:
                code = kCueletDiagnosticReadFailureMappingInvalid;
                break;
            case kCueletRingReadStreamInactive:
                code = kCueletDiagnosticReadFailureStreamInactive;
                break;
            case kCueletRingReadClientReaderUnavailable:
                code = kCueletDiagnosticReadFailureClientReaderUnavailable;
                break;
            case kCueletRingReadInvalidArgument:
                code = kCueletDiagnosticReadFailureInvalidArgument;
                break;
            case kCueletRingReadOK:
            case kCueletRingReadPartialRange:
            case kCueletRingReadStatusCount:
                break;
            }
            if (code != kCueletDiagnosticReadFailureNone) {
                data.readFailureFrameCounts[code] +=
                    readResult.rejectionFrameCounts[status];
            }
        }
        uint32_t categorized = 0;
        for (uint32_t code = kCueletDiagnosticReadFailureNotYetWritten;
             code < kCueletDiagnosticReadFailureCodeCount; ++code) {
            categorized += data.readFailureFrameCounts[code];
        }
        if (categorized < data.zeroFilledFrameCount) {
            CueletDiagnosticReadFailureCode fallback =
                kCueletDiagnosticReadFailureMappingInvalid;
            if (disposition == kCueletDiagnosticOperationWrongStream ||
                timelineStatus == kCueletTimelineInvalidArgument) {
                fallback = kCueletDiagnosticReadFailureInvalidArgument;
            } else if (disposition ==
                       kCueletDiagnosticOperationStreamInactive) {
                fallback = kCueletDiagnosticReadFailureStreamInactive;
            } else if (disposition ==
                       kCueletDiagnosticOperationClientReaderMissing) {
                fallback =
                    kCueletDiagnosticReadFailureClientReaderUnavailable;
            } else if (timelineStatus == kCueletTimelineUninitialized) {
                fallback =
                    kCueletDiagnosticReadFailureTimelineUninitialized;
            } else if (timelineStatus == kCueletTimelineOK) {
                switch (readResult.firstRejectionReason) {
                case kCueletRingReadNotYetWritten:
                    fallback = kCueletDiagnosticReadFailureNotYetWritten;
                    break;
                case kCueletRingReadOverwritten:
                    fallback = kCueletDiagnosticReadFailureOverwritten;
                    break;
                case kCueletRingReadGenerationMismatch:
                    fallback = kCueletDiagnosticReadFailureGenerationMismatch;
                    break;
                case kCueletRingReadAbsoluteFrameMismatch:
                    fallback =
                        kCueletDiagnosticReadFailureAbsoluteTagMismatch;
                    break;
                case kCueletRingReadUnpublished:
                    fallback = kCueletDiagnosticReadFailureUnpublished;
                    break;
                case kCueletRingReadSampleRateReset:
                    fallback = kCueletDiagnosticReadFailureSampleRateReset;
                    break;
                case kCueletRingReadTimelineUninitialized:
                    fallback =
                        kCueletDiagnosticReadFailureTimelineUninitialized;
                    break;
                case kCueletRingReadMappingInvalid:
                    fallback = kCueletDiagnosticReadFailureMappingInvalid;
                    break;
                case kCueletRingReadStreamInactive:
                    fallback = kCueletDiagnosticReadFailureStreamInactive;
                    break;
                case kCueletRingReadClientReaderUnavailable:
                    fallback =
                        kCueletDiagnosticReadFailureClientReaderUnavailable;
                    break;
                case kCueletRingReadInvalidArgument:
                    fallback = kCueletDiagnosticReadFailureInvalidArgument;
                    break;
                case kCueletRingReadOK:
                case kCueletRingReadPartialRange:
                case kCueletRingReadStatusCount:
                    fallback = kCueletDiagnosticReadFailureMappingInvalid;
                    break;
                }
            }
            data.readFailureFrameCounts[fallback] +=
                data.zeroFilledFrameCount - categorized;
        }
    }
    data.mainBufferPresent = mainBuffer != NULL;
    data.secondaryBufferPresent = secondaryBuffer != NULL;
    data.selectedBuffer = (uint32_t)selectedBuffer;
    data.operationDisposition = (uint32_t)disposition;
    data.bufferSelectionStatus = selectedBuffer == kCueletDiagnosticBufferMain
        ? 0U
        : 1U;
    CueletPopulateCycleDiagnostics(&data, cycle);
    CueletDiagnosticRecordData publishedPayload = {0};
    CueletAnalyzePayload(&publishedPayload, samples, frameCount);
    data.publishedPayloadChecksum = publishedPayload.payloadChecksum;
    data.publishedPayloadFirstBits = publishedPayload.payloadFirstBits;
    data.publishedPayloadLastBits = publishedPayload.payloadLastBits;
    data.publishedPayloadPeakLeft = publishedPayload.payloadPeakLeft;
    data.publishedPayloadPeakRight = publishedPayload.payloadPeakRight;
    data.publishedPayloadRMSLeft = publishedPayload.payloadRMSLeft;
    data.publishedPayloadRMSRight = publishedPayload.payloadRMSRight;
    data.publishedPayloadZeroFrameCount =
        publishedPayload.payloadZeroFrameCount;
    data.publishedPayloadNonzeroFrameCount =
        publishedPayload.payloadNonzeroFrameCount;
    if (incomingPayload != NULL) {
        data.analyzedFrameCount = incomingPayload->analyzedFrameCount;
        data.payloadChecksum = incomingPayload->payloadChecksum;
        data.payloadFirstBits = incomingPayload->payloadFirstBits;
        data.payloadLastBits = incomingPayload->payloadLastBits;
        data.payloadPeakLeft = incomingPayload->payloadPeakLeft;
        data.payloadPeakRight = incomingPayload->payloadPeakRight;
        data.payloadRMSLeft = incomingPayload->payloadRMSLeft;
        data.payloadRMSRight = incomingPayload->payloadRMSRight;
        data.payloadZeroFrameCount = incomingPayload->payloadZeroFrameCount;
        data.payloadNonzeroFrameCount = incomingPayload->payloadNonzeroFrameCount;
    } else {
        data.analyzedFrameCount = publishedPayload.analyzedFrameCount;
        data.payloadChecksum = publishedPayload.payloadChecksum;
        data.payloadFirstBits = publishedPayload.payloadFirstBits;
        data.payloadLastBits = publishedPayload.payloadLastBits;
        data.payloadPeakLeft = publishedPayload.payloadPeakLeft;
        data.payloadPeakRight = publishedPayload.payloadPeakRight;
        data.payloadRMSLeft = publishedPayload.payloadRMSLeft;
        data.payloadRMSRight = publishedPayload.payloadRMSRight;
        data.payloadZeroFrameCount = publishedPayload.payloadZeroFrameCount;
        data.payloadNonzeroFrameCount = publishedPayload.payloadNonzeroFrameCount;
    }
    CueletDiagnosticRecord(
        operationID == kAudioServerPlugInIOOperationWriteMix
            ? kCueletDiagnosticWriteMix
            : kCueletDiagnosticReadInput,
        &data);
#else
    (void)streamObjectID;
    (void)clientID;
    (void)operationID;
    (void)frameCount;
    (void)cycle;
    (void)timelineStatus;
    (void)inputStart;
    (void)outputStart;
    (void)sourceStart;
    (void)observedOffset;
    (void)loopbackDelay;
    (void)readResult;
    (void)writeResult;
    (void)samples;
    (void)mainBuffer;
    (void)secondaryBuffer;
    (void)selectedBuffer;
    (void)disposition;
    (void)incomingPayload;
#endif
}

static HRESULT Cuelet_QueryInterface(void* driver, REFIID uuid, LPVOID* interfaceOut);
static ULONG Cuelet_AddRef(void* driver);
static ULONG Cuelet_Release(void* driver);
static OSStatus Cuelet_Initialize(
    AudioServerPlugInDriverRef driver,
    AudioServerPlugInHostRef host);
static OSStatus Cuelet_CreateDevice(
    AudioServerPlugInDriverRef driver,
    CFDictionaryRef description,
    const AudioServerPlugInClientInfo* clientInfo,
    AudioObjectID* deviceObjectIDOut);
static OSStatus Cuelet_DestroyDevice(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID);
static OSStatus Cuelet_AddDeviceClient(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    const AudioServerPlugInClientInfo* clientInfo);
static OSStatus Cuelet_RemoveDeviceClient(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    const AudioServerPlugInClientInfo* clientInfo);
static OSStatus Cuelet_PerformDeviceConfigurationChange(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt64 changeAction,
    void* changeInfo);
static OSStatus Cuelet_AbortDeviceConfigurationChange(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt64 changeAction,
    void* changeInfo);
static Boolean Cuelet_HasProperty(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address);
static OSStatus Cuelet_IsPropertySettable(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    Boolean* isSettableOut);
static OSStatus Cuelet_GetPropertyDataSize(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32* dataSizeOut);
static OSStatus Cuelet_GetPropertyData(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut);
static OSStatus Cuelet_SetPropertyData(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    const void* data);
static OSStatus Cuelet_StartIO(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID);
static OSStatus Cuelet_StopIO(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID);
static OSStatus Cuelet_GetZeroTimeStamp(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    Float64* sampleTimeOut,
    UInt64* hostTimeOut,
    UInt64* seedOut);
static OSStatus Cuelet_WillDoIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    Boolean* willDoOut,
    Boolean* willDoInPlaceOut);
static OSStatus Cuelet_BeginIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo);
static OSStatus Cuelet_DoIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    AudioObjectID streamObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo,
    void* mainBuffer,
    void* secondaryBuffer);
static OSStatus Cuelet_EndIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo);

static AudioServerPlugInDriverInterface gDriverInterface = {
    NULL,
    Cuelet_QueryInterface,
    Cuelet_AddRef,
    Cuelet_Release,
    Cuelet_Initialize,
    Cuelet_CreateDevice,
    Cuelet_DestroyDevice,
    Cuelet_AddDeviceClient,
    Cuelet_RemoveDeviceClient,
    Cuelet_PerformDeviceConfigurationChange,
    Cuelet_AbortDeviceConfigurationChange,
    Cuelet_HasProperty,
    Cuelet_IsPropertySettable,
    Cuelet_GetPropertyDataSize,
    Cuelet_GetPropertyData,
    Cuelet_SetPropertyData,
    Cuelet_StartIO,
    Cuelet_StopIO,
    Cuelet_GetZeroTimeStamp,
    Cuelet_WillDoIOOperation,
    Cuelet_BeginIOOperation,
    Cuelet_DoIOOperation,
    Cuelet_EndIOOperation,
};
static AudioServerPlugInDriverInterface* gDriverInterfacePointer =
    &gDriverInterface;
static AudioServerPlugInDriverRef gDriverRef = &gDriverInterfacePointer;

static bool CueletValidDriver(AudioServerPlugInDriverRef driver)
{
    return driver == gDriverRef;
}

static bool CueletValidDevice(AudioObjectID objectID)
{
    return objectID == kCueletObjectDevice;
}

static Float32 CueletClamp(Float32 value, Float32 minimum, Float32 maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static Float32 CueletScalarToDecibels(Float32 scalar)
{
    if (scalar <= 0.0F) {
        return CUELET_MIN_VOLUME_DB;
    }
    return CueletClamp(
        20.0F * log10f(scalar),
        CUELET_MIN_VOLUME_DB,
        CUELET_MAX_VOLUME_DB);
}

static Float32 CueletDecibelsToScalar(Float32 decibels)
{
    if (decibels <= CUELET_MIN_VOLUME_DB) {
        return 0.0F;
    }
    return powf(
        10.0F,
        CueletClamp(
            decibels,
            CUELET_MIN_VOLUME_DB,
            CUELET_MAX_VOLUME_DB) /
            20.0F);
}

static void CueletUpdateHostTicksPerFrame(Float64 sampleRate)
{
    mach_timebase_info_data_t timebase = {0};
    mach_timebase_info(&timebase);
    const Float64 ticksPerSecond =
        (1000000000.0 * (Float64)timebase.denom) / (Float64)timebase.numer;
    atomic_store_explicit(
        &gHostTicksPerFrame,
        ticksPerSecond / sampleRate,
        memory_order_seq_cst);
}

static CueletRingReader* CueletReaderForClient(uint32_t clientID)
{
    for (uint32_t index = 0; index < CUELET_MAX_CLIENTS; ++index) {
        if (atomic_load_explicit(
                &gClients[index].clientID,
                memory_order_seq_cst) == clientID) {
            return &gClients[index].reader;
        }
    }
    return NULL;
}

static OSStatus CueletWriteValue(
    UInt32 dataSize,
    UInt32 requiredSize,
    UInt32* dataSizeOut,
    void* dataOut,
    const void* value)
{
    if (dataSize < requiredSize) {
        return kAudioHardwareBadPropertySizeError;
    }
    memcpy(dataOut, value, requiredSize);
    *dataSizeOut = requiredSize;
    return noErr;
}

static OSStatus CueletWriteCFString(
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut,
    CFStringRef value)
{
    return CueletWriteValue(
        dataSize,
        (UInt32)sizeof(value),
        dataSizeOut,
        dataOut,
        &value);
}

#ifdef CUELET_AUDIO_DIAGNOSTICS
/*
 * Audio Server custom properties cross the coreaudiod process boundary. The
 * host only marshals the CF types declared in AudioServerPlugInCustomPropertyInfo.
 * Values created here are returned at +1, matching Apple's NullAudio sample.
 */
static OSStatus CueletWriteOwnedPropertyList(
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut,
    CFPropertyListRef value)
{
    if (value == NULL) {
        return kAudioHardwareUnspecifiedError;
    }
    if (dataSize < sizeof(value)) {
        CFRelease(value);
        return kAudioHardwareBadPropertySizeError;
    }
    *(CFPropertyListRef*)dataOut = value;
    *dataSizeOut = sizeof(value);
    return noErr;
}

static OSStatus CueletWriteDataProperty(
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut,
    const void* bytes,
    size_t byteCount)
{
    if (byteCount > (size_t)LONG_MAX) {
        return kAudioHardwareBadPropertySizeError;
    }
    CFDataRef value = CFDataCreate(
        kCFAllocatorDefault,
        bytes,
        (CFIndex)byteCount);
    return CueletWriteOwnedPropertyList(
        dataSize, dataSizeOut, dataOut, value);
}

static OSStatus CueletWriteUInt64Property(
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut,
    uint64_t number)
{
    CFNumberRef value = CFNumberCreate(
        kCFAllocatorDefault,
        kCFNumberSInt64Type,
        &number);
    return CueletWriteOwnedPropertyList(
        dataSize, dataSizeOut, dataOut, value);
}

static OSStatus CueletWriteBooleanProperty(
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut,
    bool enabled)
{
    CFBooleanRef value = enabled ? kCFBooleanTrue : kCFBooleanFalse;
    CFRetain(value);
    return CueletWriteOwnedPropertyList(
        dataSize, dataSizeOut, dataOut, value);
}

static OSStatus CueletWriteEventProperty(
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    if (qualifierDataSize != 0 || qualifierData != NULL) {
        return kAudioHardwareBadPropertySizeError;
    }
    const size_t exportSize = CueletDiagnosticExportSize();
    if (exportSize > (size_t)LONG_MAX) {
        return kAudioHardwareBadPropertySizeError;
    }
    void* exportBytes = malloc(exportSize);
    if (exportBytes == NULL) {
        return kAudioHardwareUnspecifiedError;
    }
    size_t used = 0;
    const OSStatus exportStatus = CueletDiagnosticExportEventSnapshot(
        exportBytes, exportSize, &used);
    if (exportStatus != noErr) {
        free(exportBytes);
        return exportStatus;
    }
    /* The Core Audio custom-property marshaller owns a retained, immutable,
     * exact-length CFPropertyList value. It never sees the 8,192-event backing
     * capacity or mutable storage used while constructing the page. */
    const OSStatus result = CueletWriteDataProperty(
        dataSize, dataSizeOut, dataOut, exportBytes, used);
    free(exportBytes);
    return result;
}
#endif

static UInt32 CueletCopyObjectList(
    const AudioObjectID* objects,
    UInt32 objectCount,
    UInt32 dataSize,
    void* dataOut)
{
    const UInt32 requestedCount = dataSize / (UInt32)sizeof(AudioObjectID);
    const UInt32 copiedCount = requestedCount < objectCount
        ? requestedCount
        : objectCount;
    if (copiedCount > 0) {
        memcpy(dataOut, objects, copiedCount * sizeof(AudioObjectID));
    }
    return copiedCount * (UInt32)sizeof(AudioObjectID);
}

static void CueletRequestSampleRate(Float64 sampleRate)
{
    const UInt64 action = (UInt64)sampleRate;
    AudioServerPlugInHostRef host = gHost;
    if (host == NULL) {
        return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        host->RequestDeviceConfigurationChange(
            host,
            kCueletObjectDevice,
            action,
            NULL);
    });
}

static void CueletNotifyProperty(
    AudioObjectID objectID,
    AudioObjectPropertySelector selector)
{
    AudioServerPlugInHostRef host = gHost;
    if (host == NULL) {
        return;
    }
    AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    host->PropertiesChanged(host, objectID, 1, &address);
}

CUELET_FACTORY_EXPORT void* CueletVirtualAudio_Create(
    CFAllocatorRef allocator,
    CFUUIDRef requestedTypeUUID)
{
    (void)allocator;
    return CFEqual(requestedTypeUUID, kAudioServerPlugInTypeUUID)
        ? gDriverRef
        : NULL;
}

static HRESULT Cuelet_QueryInterface(
    void* driver,
    REFIID uuid,
    LPVOID* interfaceOut)
{
    if (driver != gDriverRef || interfaceOut == NULL) {
        return E_POINTER;
    }
    *interfaceOut = NULL;
    CFUUIDRef requestedUUID = CFUUIDCreateFromUUIDBytes(NULL, uuid);
    if (requestedUUID == NULL) {
        return E_OUTOFMEMORY;
    }
    const bool supported =
        CFEqual(requestedUUID, IUnknownUUID) ||
        CFEqual(requestedUUID, kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(requestedUUID);
    if (!supported) {
        return E_NOINTERFACE;
    }
    Cuelet_AddRef(driver);
    *interfaceOut = gDriverRef;
    return S_OK;
}

static ULONG Cuelet_AddRef(void* driver)
{
    if (driver != gDriverRef) {
        return 0;
    }
    return atomic_fetch_add_explicit(
        &gRefCount,
        1,
        memory_order_relaxed) + 1;
}

static ULONG Cuelet_Release(void* driver)
{
    if (driver != gDriverRef) {
        return 0;
    }
    uint32_t count = atomic_load_explicit(&gRefCount, memory_order_relaxed);
    while (count > 0 && !atomic_compare_exchange_weak_explicit(
        &gRefCount,
        &count,
        count - 1,
        memory_order_relaxed,
        memory_order_relaxed)) {
    }
    return count > 0 ? count - 1 : 0;
}

static OSStatus Cuelet_Initialize(
    AudioServerPlugInDriverRef driver,
    AudioServerPlugInHostRef host)
{
    if (!CueletValidDriver(driver) || host == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    pthread_mutex_lock(&gStateMutex);
    gHost = host;
    if (!gInitialized) {
        CueletDiagnosticInitialize();
        CueletIOStateInitialize(&gIOState);
        CueletDiagnosticSetStateTokens(
            CueletDiagnosticTokenForPointer(&gIOState),
            CueletDiagnosticTokenForPointer(&gIOState.ring) ^
                UINT64_C(0x72696e672d746f6b));
        for (uint32_t index = 0; index < CUELET_MAX_CLIENTS; ++index) {
            atomic_init(&gClients[index].clientID, 0);
            atomic_init(&gClients[index].reader.generation, 0);
            atomic_init(&gClients[index].reader.initialized, false);
            atomic_init(&gClients[index].reader.lastRequestedStart, 0);
            atomic_init(&gClients[index].reader.lastRequestedEnd, 0);
        }
        CueletUpdateHostTicksPerFrame(
            atomic_load_explicit(&gSampleRate, memory_order_seq_cst));
        gInitialized = true;
        CueletDiagnosticRecordData data = {0};
        data.deviceObjectID = kCueletObjectDevice;
        data.hostTimeSnapshot = mach_absolute_time();
        data.sampleRate = atomic_load_explicit(
            &gSampleRate, memory_order_relaxed);
        data.resetGeneration = CueletRingGeneration(&gIOState.ring);
        CueletDiagnosticRecord(kCueletDiagnosticDriverInitialize, &data);
    }
    pthread_mutex_unlock(&gStateMutex);
    return noErr;
}

static OSStatus Cuelet_CreateDevice(
    AudioServerPlugInDriverRef driver,
    CFDictionaryRef description,
    const AudioServerPlugInClientInfo* clientInfo,
    AudioObjectID* deviceObjectIDOut)
{
    (void)description;
    (void)clientInfo;
    (void)deviceObjectIDOut;
    return CueletValidDriver(driver)
        ? kAudioHardwareUnsupportedOperationError
        : kAudioHardwareBadObjectError;
}

static OSStatus Cuelet_DestroyDevice(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID)
{
    (void)deviceObjectID;
    return CueletValidDriver(driver)
        ? kAudioHardwareUnsupportedOperationError
        : kAudioHardwareBadObjectError;
}

static OSStatus Cuelet_AddDeviceClient(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    const AudioServerPlugInClientInfo* clientInfo)
{
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    if (clientInfo == NULL || clientInfo->mClientID == 0) {
        return kAudioHardwareIllegalOperationError;
    }

    OSStatus result = kAudioHardwareIllegalOperationError;
    pthread_mutex_lock(&gStateMutex);
    CueletClientSlot* empty = NULL;
    for (uint32_t index = 0; index < CUELET_MAX_CLIENTS; ++index) {
        const uint32_t current = atomic_load_explicit(
            &gClients[index].clientID,
            memory_order_seq_cst);
        if (current == clientInfo->mClientID) {
            result = noErr;
            empty = NULL;
            break;
        }
        if (current == 0 && empty == NULL) {
            empty = &gClients[index];
        }
    }
    if (result != noErr && empty != NULL) {
        atomic_store_explicit(
            &empty->reader.initialized,
            false,
            memory_order_seq_cst);
        atomic_store_explicit(
            &empty->clientID,
            clientInfo->mClientID,
            memory_order_seq_cst);
        result = noErr;
    }
    pthread_mutex_unlock(&gStateMutex);
    return result;
}

static OSStatus Cuelet_RemoveDeviceClient(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    const AudioServerPlugInClientInfo* clientInfo)
{
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    if (clientInfo == NULL || clientInfo->mClientID == 0) {
        return kAudioHardwareIllegalOperationError;
    }

    pthread_mutex_lock(&gStateMutex);
    for (uint32_t index = 0; index < CUELET_MAX_CLIENTS; ++index) {
        if (atomic_load_explicit(
                &gClients[index].clientID,
                memory_order_seq_cst) == clientInfo->mClientID) {
            atomic_store_explicit(
                &gClients[index].clientID,
                0,
                memory_order_seq_cst);
            atomic_store_explicit(
                &gClients[index].reader.initialized,
                false,
                memory_order_seq_cst);
            break;
        }
    }
    pthread_mutex_unlock(&gStateMutex);
    return noErr;
}

static OSStatus Cuelet_PerformDeviceConfigurationChange(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt64 changeAction,
    void* changeInfo)
{
    (void)changeInfo;
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    const Float64 sampleRate = (Float64)changeAction;
    if (!CueletIsSupportedSampleRate(sampleRate)) {
        return kAudioHardwareIllegalOperationError;
    }
    atomic_store_explicit(&gSampleRate, sampleRate, memory_order_seq_cst);
    CueletUpdateHostTicksPerFrame(sampleRate);
    CueletRingReset(&gIOState.ring);
    CueletResetTimelineMapping();
    atomic_fetch_add_explicit(&gTimelineSeed, 1, memory_order_seq_cst);
    CueletDiagnosticRecordData data = {0};
    data.sampleRate = sampleRate;
    data.timelineSeed = atomic_load_explicit(
        &gTimelineSeed,
        memory_order_relaxed);
    data.resetGeneration = atomic_load_explicit(
        &gIOState.ring.resetGeneration,
        memory_order_relaxed);
    CueletDiagnosticRecord(kCueletDiagnosticSampleRateChange, &data);
    return noErr;
}

static OSStatus Cuelet_AbortDeviceConfigurationChange(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt64 changeAction,
    void* changeInfo)
{
    (void)changeAction;
    (void)changeInfo;
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    return noErr;
}

static Boolean Cuelet_HasProperty(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address)
{
    (void)clientProcessID;
    return CueletValidDriver(driver) &&
        CueletObjectSupportsProperty(objectID, address);
}

static OSStatus Cuelet_IsPropertySettable(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    Boolean* isSettableOut)
{
    (void)clientProcessID;
    if (!CueletValidDriver(driver) ||
        CueletObjectKindForID(objectID) == kCueletObjectKindUnknown) {
        return kAudioHardwareBadObjectError;
    }
    if (address == NULL || isSettableOut == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    if (!CueletObjectSupportsProperty(objectID, address)) {
        return kAudioHardwareUnknownPropertyError;
    }

    *isSettableOut = false;
    const CueletObjectKind kind = CueletObjectKindForID(objectID);
    if (kind == kCueletObjectKindDevice &&
        address->mSelector == kAudioDevicePropertyNominalSampleRate) {
        *isSettableOut = true;
#ifdef CUELET_AUDIO_DIAGNOSTICS
    } else if (kind == kCueletObjectKindDevice &&
        address->mSelector == kCueletDiagnosticPropertyClear) {
        *isSettableOut = true;
#endif
    } else if ((kind == kCueletObjectKindInputStream ||
                kind == kCueletObjectKindOutputStream) &&
        (address->mSelector == kAudioStreamPropertyIsActive ||
         address->mSelector == kAudioStreamPropertyVirtualFormat ||
         address->mSelector == kAudioStreamPropertyPhysicalFormat)) {
        *isSettableOut = true;
    } else if (kind == kCueletObjectKindVolumeControl &&
        (address->mSelector == kAudioLevelControlPropertyScalarValue ||
         address->mSelector == kAudioLevelControlPropertyDecibelValue)) {
        *isSettableOut = true;
    } else if (kind == kCueletObjectKindMuteControl &&
        address->mSelector == kAudioBooleanControlPropertyValue) {
        *isSettableOut = true;
    }
    return noErr;
}

static UInt32 CueletDeviceObjectCount(AudioObjectPropertyScope scope)
{
    return scope == kAudioObjectPropertyScopeGlobal ? 6U : 3U;
}

static UInt32 CueletDeviceControlCount(AudioObjectPropertyScope scope)
{
    return scope == kAudioObjectPropertyScopeGlobal ? 4U : 2U;
}

static OSStatus Cuelet_GetPropertyDataSize(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32* dataSizeOut)
{
    (void)clientProcessID;
#ifndef CUELET_AUDIO_DIAGNOSTICS
    (void)qualifierDataSize;
    (void)qualifierData;
#endif
    if (!CueletValidDriver(driver) ||
        CueletObjectKindForID(objectID) == kCueletObjectKindUnknown) {
        return kAudioHardwareBadObjectError;
    }
    if (address == NULL || dataSizeOut == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    if (!CueletObjectSupportsProperty(objectID, address)) {
        return kAudioHardwareUnknownPropertyError;
    }

    const CueletObjectKind kind = CueletObjectKindForID(objectID);
#ifdef CUELET_AUDIO_DIAGNOSTICS
    if (kind == kCueletObjectKindDevice &&
        address->mSelector == kCueletDiagnosticPropertyEvents &&
        (qualifierDataSize != 0 || qualifierData != NULL)) {
        return kAudioHardwareBadPropertySizeError;
    }
#endif
    switch (kind) {
    case kCueletObjectKindPlugIn:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioPlugInPropertyTranslateUIDToBox:
        case kAudioPlugInPropertyTranslateUIDToDevice:
            *dataSizeOut = sizeof(AudioObjectID);
            return noErr;
        case kAudioObjectPropertyManufacturer:
        case kAudioPlugInPropertyResourceBundle:
            *dataSizeOut = sizeof(CFStringRef);
            return noErr;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
            *dataSizeOut = sizeof(AudioObjectID);
            return noErr;
        case kAudioPlugInPropertyBoxList:
            *dataSizeOut = 0;
            return noErr;
        default:
            break;
        }
        break;
    case kCueletObjectKindDevice:
        switch (address->mSelector) {
#ifdef CUELET_AUDIO_DIAGNOSTICS
        case kAudioObjectPropertyCustomPropertyInfoList:
            *dataSizeOut = sizeof(kCueletDiagnosticProperties);
            return noErr;
        case kCueletDiagnosticPropertySchema:
        case kCueletDiagnosticPropertyCounters:
        case kCueletDiagnosticPropertyEvents:
        case kCueletDiagnosticPropertyEventCount:
        case kCueletDiagnosticPropertyClear:
        case kCueletDiagnosticPropertyBuild:
        case kCueletDiagnosticPropertyEnabled:
            *dataSizeOut = sizeof(CFPropertyListRef);
            return noErr;
#endif
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
            *dataSizeOut = sizeof(UInt32);
            return noErr;
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
            *dataSizeOut = sizeof(CFStringRef);
            return noErr;
        case kAudioObjectPropertyOwnedObjects:
            *dataSizeOut = CueletDeviceObjectCount(address->mScope) *
                sizeof(AudioObjectID);
            return noErr;
        case kAudioDevicePropertyRelatedDevices:
            *dataSizeOut = sizeof(AudioObjectID);
            return noErr;
        case kAudioDevicePropertyNominalSampleRate:
            *dataSizeOut = sizeof(Float64);
            return noErr;
        case kAudioDevicePropertyAvailableNominalSampleRates:
            *dataSizeOut = 2 * sizeof(AudioValueRange);
            return noErr;
        case kAudioDevicePropertyStreams:
            *dataSizeOut = address->mScope == kAudioObjectPropertyScopeGlobal
                ? 2 * sizeof(AudioObjectID)
                : sizeof(AudioObjectID);
            return noErr;
        case kAudioObjectPropertyControlList:
            *dataSizeOut = CueletDeviceControlCount(address->mScope) *
                sizeof(AudioObjectID);
            return noErr;
        case kAudioDevicePropertyPreferredChannelsForStereo:
            *dataSizeOut = 2 * sizeof(UInt32);
            return noErr;
        case kAudioDevicePropertyPreferredChannelLayout:
            *dataSizeOut = (UInt32)offsetof(
                AudioChannelLayout,
                mChannelDescriptions) +
                2 * sizeof(AudioChannelDescription);
            return noErr;
        default:
            break;
        }
        break;
    case kCueletObjectKindInputStream:
    case kCueletObjectKindOutputStream:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
            *dataSizeOut = sizeof(UInt32);
            return noErr;
        case kAudioObjectPropertyName:
            *dataSizeOut = sizeof(CFStringRef);
            return noErr;
        case kAudioObjectPropertyOwnedObjects:
            *dataSizeOut = 0;
            return noErr;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            *dataSizeOut = sizeof(AudioStreamBasicDescription);
            return noErr;
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            *dataSizeOut = 2 * sizeof(AudioStreamRangedDescription);
            return noErr;
        default:
            break;
        }
        break;
    case kCueletObjectKindVolumeControl:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
            *dataSizeOut = sizeof(UInt32);
            return noErr;
        case kAudioObjectPropertyOwnedObjects:
            *dataSizeOut = 0;
            return noErr;
        case kAudioLevelControlPropertyScalarValue:
        case kAudioLevelControlPropertyDecibelValue:
        case kAudioLevelControlPropertyConvertScalarToDecibels:
        case kAudioLevelControlPropertyConvertDecibelsToScalar:
            *dataSizeOut = sizeof(Float32);
            return noErr;
        case kAudioLevelControlPropertyDecibelRange:
            *dataSizeOut = sizeof(AudioValueRange);
            return noErr;
        default:
            break;
        }
        break;
    case kCueletObjectKindMuteControl:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
        case kAudioBooleanControlPropertyValue:
            *dataSizeOut = sizeof(UInt32);
            return noErr;
        case kAudioObjectPropertyOwnedObjects:
            *dataSizeOut = 0;
            return noErr;
        default:
            break;
        }
        break;
    case kCueletObjectKindUnknown:
        break;
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus CueletGetPlugInProperty(
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    AudioObjectID objectID = kAudioObjectUnknown;
    switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass:
        objectID = kAudioObjectClassID;
        return CueletWriteValue(dataSize, sizeof(objectID), dataSizeOut, dataOut, &objectID);
    case kAudioObjectPropertyClass:
        objectID = kAudioPlugInClassID;
        return CueletWriteValue(dataSize, sizeof(objectID), dataSizeOut, dataOut, &objectID);
    case kAudioObjectPropertyOwner:
        return CueletWriteValue(dataSize, sizeof(objectID), dataSizeOut, dataOut, &objectID);
    case kAudioObjectPropertyManufacturer:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(CUELET_DRIVER_MANUFACTURER));
    case kAudioObjectPropertyOwnedObjects:
    case kAudioPlugInPropertyDeviceList: {
        const AudioObjectID device = kCueletObjectDevice;
        *dataSizeOut = CueletCopyObjectList(&device, 1, dataSize, dataOut);
        return noErr;
    }
    case kAudioPlugInPropertyBoxList:
        *dataSizeOut = 0;
        return noErr;
    case kAudioPlugInPropertyTranslateUIDToBox:
        if (qualifierDataSize != sizeof(CFStringRef) || qualifierData == NULL) {
            return kAudioHardwareBadPropertySizeError;
        }
        return CueletWriteValue(dataSize, sizeof(objectID), dataSizeOut, dataOut, &objectID);
    case kAudioPlugInPropertyTranslateUIDToDevice:
        if (qualifierDataSize != sizeof(CFStringRef) || qualifierData == NULL) {
            return kAudioHardwareBadPropertySizeError;
        }
        if (CFStringCompare(
                *(const CFStringRef*)qualifierData,
                CFSTR(CUELET_DRIVER_DEVICE_UID),
                0) == kCFCompareEqualTo) {
            objectID = kCueletObjectDevice;
        }
        return CueletWriteValue(dataSize, sizeof(objectID), dataSizeOut, dataOut, &objectID);
    case kAudioPlugInPropertyResourceBundle:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(""));
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static const AudioObjectID kInputObjects[] = {
    kCueletObjectInputStream,
    kCueletObjectInputVolume,
    kCueletObjectInputMute,
};
static const AudioObjectID kOutputObjects[] = {
    kCueletObjectOutputStream,
    kCueletObjectOutputVolume,
    kCueletObjectOutputMute,
};
static const AudioObjectID kAllDeviceObjects[] = {
    kCueletObjectInputStream,
    kCueletObjectInputVolume,
    kCueletObjectInputMute,
    kCueletObjectOutputStream,
    kCueletObjectOutputVolume,
    kCueletObjectOutputMute,
};
static const AudioObjectID kAllControls[] = {
    kCueletObjectInputVolume,
    kCueletObjectInputMute,
    kCueletObjectOutputVolume,
    kCueletObjectOutputMute,
};

static void CueletListForScope(
    AudioObjectPropertyScope scope,
    const AudioObjectID** objectsOut,
    UInt32* countOut)
{
    if (scope == kAudioObjectPropertyScopeInput) {
        *objectsOut = kInputObjects;
        *countOut = 3;
    } else if (scope == kAudioObjectPropertyScopeOutput) {
        *objectsOut = kOutputObjects;
        *countOut = 3;
    } else {
        *objectsOut = kAllDeviceObjects;
        *countOut = 6;
    }
}

static OSStatus CueletGetDeviceProperty(
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    (void)clientProcessID;
#ifndef CUELET_AUDIO_DIAGNOSTICS
    (void)qualifierDataSize;
    (void)qualifierData;
#endif
    UInt32 uintValue = 0;
    AudioObjectID objectValue = 0;
    switch (address->mSelector) {
#ifdef CUELET_AUDIO_DIAGNOSTICS
    case kAudioObjectPropertyCustomPropertyInfoList:
        return CueletWriteValue(
            dataSize,
            sizeof(kCueletDiagnosticProperties),
            dataSizeOut,
            dataOut,
            kCueletDiagnosticProperties);
    case kCueletDiagnosticPropertySchema: {
        CueletDiagnosticSchema schema = {0};
        CueletDiagnosticGetSchema(&schema);
        return CueletWriteDataProperty(
            dataSize, dataSizeOut, dataOut, &schema, sizeof(schema));
    }
    case kCueletDiagnosticPropertyCounters: {
        CueletDiagnosticCounters counters = {0};
        CueletDiagnosticGetCounters(&counters);
        CueletDiagnosticRecordData event = {0};
        event.deviceObjectID = kCueletObjectDevice;
        event.hostTimeSnapshot = mach_absolute_time();
        event.resetGeneration = CueletRingGeneration(&gIOState.ring);
        CueletDiagnosticRecord(kCueletDiagnosticPropertySnapshot, &event);
        return CueletWriteDataProperty(
            dataSize, dataSizeOut, dataOut, &counters, sizeof(counters));
    }
    case kCueletDiagnosticPropertyEvents:
        return CueletWriteEventProperty(
            qualifierDataSize,
            qualifierData,
            dataSize,
            dataSizeOut,
            dataOut);
    case kCueletDiagnosticPropertyEventCount: {
        const uint64_t eventCount = CueletDiagnosticEventCount();
        return CueletWriteUInt64Property(
            dataSize, dataSizeOut, dataOut, eventCount);
    }
    case kCueletDiagnosticPropertyClear:
        return CueletWriteBooleanProperty(
            dataSize, dataSizeOut, dataOut, false);
    case kCueletDiagnosticPropertyBuild: {
        CueletDiagnosticBuildInfo build = {0};
        CueletDiagnosticGetBuildInfo(&build);
        return CueletWriteDataProperty(
            dataSize, dataSizeOut, dataOut, &build, sizeof(build));
    }
    case kCueletDiagnosticPropertyEnabled:
        return CueletWriteBooleanProperty(
            dataSize, dataSizeOut, dataOut, true);
#endif
    case kAudioObjectPropertyBaseClass:
        objectValue = kAudioObjectClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyClass:
        objectValue = kAudioDeviceClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyOwner:
        objectValue = kCueletObjectPlugIn;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyName:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(CUELET_DRIVER_NAME));
    case kAudioObjectPropertyManufacturer:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(CUELET_DRIVER_MANUFACTURER));
    case kAudioObjectPropertyOwnedObjects: {
        const AudioObjectID* objects = NULL;
        UInt32 count = 0;
        CueletListForScope(address->mScope, &objects, &count);
        *dataSizeOut = CueletCopyObjectList(objects, count, dataSize, dataOut);
        return noErr;
    }
    case kAudioDevicePropertyDeviceUID:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(CUELET_DRIVER_DEVICE_UID));
    case kAudioDevicePropertyModelUID:
        return CueletWriteCFString(dataSize, dataSizeOut, dataOut, CFSTR(CUELET_DRIVER_MODEL_UID));
    case kAudioDevicePropertyTransportType:
        uintValue = kAudioDeviceTransportTypeVirtual;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyRelatedDevices:
        objectValue = kCueletObjectDevice;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioDevicePropertyClockDomain:
        uintValue = 0;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyDeviceIsAlive:
        uintValue = 1;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyDeviceIsRunning:
        uintValue = atomic_load_explicit(
            &gIOState.runningClientCount,
            memory_order_seq_cst) > 0;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        uintValue = 1;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyLatency:
        uintValue = CUELET_LATENCY_FRAMES;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertySafetyOffset:
        uintValue = CUELET_SAFETY_OFFSET_FRAMES;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyNominalSampleRate: {
        const Float64 sampleRate = atomic_load_explicit(
            &gSampleRate,
            memory_order_seq_cst);
        return CueletWriteValue(dataSize, sizeof(sampleRate), dataSizeOut, dataOut, &sampleRate);
    }
    case kAudioDevicePropertyAvailableNominalSampleRates: {
        const AudioValueRange ranges[2] = {
            {44100.0, 44100.0},
            {48000.0, 48000.0},
        };
        const UInt32 copied = dataSize < sizeof(ranges) ? dataSize : sizeof(ranges);
        const UInt32 aligned = copied - (copied % sizeof(AudioValueRange));
        memcpy(dataOut, ranges, aligned);
        *dataSizeOut = aligned;
        return noErr;
    }
    case kAudioDevicePropertyIsHidden:
        uintValue = 0;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyZeroTimeStampPeriod:
        uintValue = CUELET_RING_CAPACITY_FRAMES;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioDevicePropertyStreams: {
        if (address->mScope == kAudioObjectPropertyScopeInput) {
            const AudioObjectID stream = kCueletObjectInputStream;
            *dataSizeOut = CueletCopyObjectList(&stream, 1, dataSize, dataOut);
        } else if (address->mScope == kAudioObjectPropertyScopeOutput) {
            const AudioObjectID stream = kCueletObjectOutputStream;
            *dataSizeOut = CueletCopyObjectList(&stream, 1, dataSize, dataOut);
        } else {
            const AudioObjectID streams[2] = {
                kCueletObjectInputStream,
                kCueletObjectOutputStream,
            };
            *dataSizeOut = CueletCopyObjectList(streams, 2, dataSize, dataOut);
        }
        return noErr;
    }
    case kAudioObjectPropertyControlList: {
        if (address->mScope == kAudioObjectPropertyScopeInput) {
            *dataSizeOut = CueletCopyObjectList(
                &kInputObjects[1], 2, dataSize, dataOut);
        } else if (address->mScope == kAudioObjectPropertyScopeOutput) {
            *dataSizeOut = CueletCopyObjectList(
                &kOutputObjects[1], 2, dataSize, dataOut);
        } else {
            *dataSizeOut = CueletCopyObjectList(
                kAllControls, 4, dataSize, dataOut);
        }
        return noErr;
    }
    case kAudioDevicePropertyPreferredChannelsForStereo: {
        const UInt32 channels[2] = {1, 2};
        return CueletWriteValue(dataSize, sizeof(channels), dataSizeOut, dataOut, channels);
    }
    case kAudioDevicePropertyPreferredChannelLayout: {
        const UInt32 requiredSize = (UInt32)offsetof(
            AudioChannelLayout,
            mChannelDescriptions) +
            2 * sizeof(AudioChannelDescription);
        if (dataSize < requiredSize) {
            return kAudioHardwareBadPropertySizeError;
        }
        AudioChannelLayout* layout = dataOut;
        memset(layout, 0, requiredSize);
        layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
        layout->mNumberChannelDescriptions = 2;
        layout->mChannelDescriptions[0].mChannelLabel = kAudioChannelLabel_Left;
        layout->mChannelDescriptions[1].mChannelLabel = kAudioChannelLabel_Right;
        *dataSizeOut = requiredSize;
        return noErr;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus CueletGetStreamProperty(
    AudioObjectID objectID,
    const AudioObjectPropertyAddress* address,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    const bool isInput = objectID == kCueletObjectInputStream;
    UInt32 uintValue = 0;
    AudioObjectID objectValue = 0;
    switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass:
        objectValue = kAudioObjectClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyClass:
        objectValue = kAudioStreamClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyOwner:
        objectValue = kCueletObjectDevice;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyName:
        return CueletWriteCFString(
            dataSize,
            dataSizeOut,
            dataOut,
            isInput ? CFSTR("Cuelet Loopback Input") : CFSTR("Cuelet Injection Output"));
    case kAudioObjectPropertyOwnedObjects:
        *dataSizeOut = 0;
        return noErr;
    case kAudioStreamPropertyIsActive:
        uintValue = atomic_load_explicit(
            isInput ? &gIOState.inputStreamActive : &gIOState.outputStreamActive,
            memory_order_seq_cst);
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioStreamPropertyDirection:
        uintValue = isInput ? 1U : 0U;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioStreamPropertyTerminalType:
        uintValue = isInput
            ? kAudioStreamTerminalTypeMicrophone
            : kAudioStreamTerminalTypeSpeaker;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioStreamPropertyStartingChannel:
        uintValue = 1;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioStreamPropertyLatency:
        uintValue = 0;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
        const AudioStreamBasicDescription format = CueletMakeStreamFormat(
            atomic_load_explicit(&gSampleRate, memory_order_seq_cst));
        return CueletWriteValue(dataSize, sizeof(format), dataSizeOut, dataOut, &format);
    }
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats: {
        const AudioStreamRangedDescription formats[2] = {
            {CueletMakeStreamFormat(44100.0), {44100.0, 44100.0}},
            {CueletMakeStreamFormat(48000.0), {48000.0, 48000.0}},
        };
        const UInt32 copied = dataSize < sizeof(formats) ? dataSize : sizeof(formats);
        const UInt32 aligned = copied - (copied % sizeof(AudioStreamRangedDescription));
        memcpy(dataOut, formats, aligned);
        *dataSizeOut = aligned;
        return noErr;
    }
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus CueletGetControlProperty(
    AudioObjectID objectID,
    const AudioObjectPropertyAddress* address,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    const bool isVolume = CueletObjectKindForID(objectID) ==
        kCueletObjectKindVolumeControl;
    const bool isInput = objectID == kCueletObjectInputVolume ||
        objectID == kCueletObjectInputMute;
    UInt32 uintValue = 0;
    AudioObjectID objectValue = 0;
    switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass:
        objectValue = isVolume
            ? kAudioLevelControlClassID
            : kAudioBooleanControlClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyClass:
        objectValue = isVolume
            ? kAudioVolumeControlClassID
            : kAudioMuteControlClassID;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyOwner:
        objectValue = kCueletObjectDevice;
        return CueletWriteValue(dataSize, sizeof(objectValue), dataSizeOut, dataOut, &objectValue);
    case kAudioObjectPropertyOwnedObjects:
        *dataSizeOut = 0;
        return noErr;
    case kAudioControlPropertyScope:
        uintValue = isInput
            ? kAudioObjectPropertyScopeInput
            : kAudioObjectPropertyScopeOutput;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioControlPropertyElement:
        uintValue = kAudioObjectPropertyElementMain;
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    case kAudioLevelControlPropertyScalarValue: {
        const Float32 value = atomic_load_explicit(
            isInput ? &gInputVolume : &gOutputVolume,
            memory_order_seq_cst);
        return CueletWriteValue(dataSize, sizeof(value), dataSizeOut, dataOut, &value);
    }
    case kAudioLevelControlPropertyDecibelValue: {
        const Float32 value = CueletScalarToDecibels(atomic_load_explicit(
            isInput ? &gInputVolume : &gOutputVolume,
            memory_order_seq_cst));
        return CueletWriteValue(dataSize, sizeof(value), dataSizeOut, dataOut, &value);
    }
    case kAudioLevelControlPropertyDecibelRange: {
        const AudioValueRange range = {
            CUELET_MIN_VOLUME_DB,
            CUELET_MAX_VOLUME_DB,
        };
        return CueletWriteValue(dataSize, sizeof(range), dataSizeOut, dataOut, &range);
    }
    case kAudioLevelControlPropertyConvertScalarToDecibels:
        if (dataSize < sizeof(Float32)) {
            return kAudioHardwareBadPropertySizeError;
        }
        *(Float32*)dataOut = CueletScalarToDecibels(*(Float32*)dataOut);
        *dataSizeOut = sizeof(Float32);
        return noErr;
    case kAudioLevelControlPropertyConvertDecibelsToScalar:
        if (dataSize < sizeof(Float32)) {
            return kAudioHardwareBadPropertySizeError;
        }
        *(Float32*)dataOut = CueletDecibelsToScalar(*(Float32*)dataOut);
        *dataSizeOut = sizeof(Float32);
        return noErr;
    case kAudioBooleanControlPropertyValue:
        uintValue = atomic_load_explicit(
            isInput ? &gInputMuted : &gOutputMuted,
            memory_order_seq_cst);
        return CueletWriteValue(dataSize, sizeof(uintValue), dataSizeOut, dataOut, &uintValue);
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus Cuelet_GetPropertyData(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    UInt32* dataSizeOut,
    void* dataOut)
{
    (void)clientProcessID;
    if (!CueletValidDriver(driver) ||
        CueletObjectKindForID(objectID) == kCueletObjectKindUnknown) {
        return kAudioHardwareBadObjectError;
    }
    if (address == NULL || dataSizeOut == NULL || dataOut == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    if (!CueletObjectSupportsProperty(objectID, address)) {
        return kAudioHardwareUnknownPropertyError;
    }
    *dataSizeOut = 0;
    switch (CueletObjectKindForID(objectID)) {
    case kCueletObjectKindPlugIn:
        return CueletGetPlugInProperty(
            address,
            qualifierDataSize,
            qualifierData,
            dataSize,
            dataSizeOut,
            dataOut);
    case kCueletObjectKindDevice:
        return CueletGetDeviceProperty(
            clientProcessID,
            address,
            qualifierDataSize,
            qualifierData,
            dataSize,
            dataSizeOut,
            dataOut);
    case kCueletObjectKindInputStream:
    case kCueletObjectKindOutputStream:
        return CueletGetStreamProperty(
            objectID, address, dataSize, dataSizeOut, dataOut);
    case kCueletObjectKindVolumeControl:
    case kCueletObjectKindMuteControl:
        return CueletGetControlProperty(
            objectID, address, dataSize, dataSizeOut, dataOut);
    case kCueletObjectKindUnknown:
        return kAudioHardwareBadObjectError;
    }
}

static OSStatus Cuelet_SetPropertyData(
    AudioServerPlugInDriverRef driver,
    AudioObjectID objectID,
    pid_t clientProcessID,
    const AudioObjectPropertyAddress* address,
    UInt32 qualifierDataSize,
    const void* qualifierData,
    UInt32 dataSize,
    const void* data)
{
    (void)clientProcessID;
    (void)qualifierDataSize;
    (void)qualifierData;
    if (!CueletValidDriver(driver) ||
        CueletObjectKindForID(objectID) == kCueletObjectKindUnknown) {
        return kAudioHardwareBadObjectError;
    }
    if (address == NULL || data == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    if (!CueletObjectSupportsProperty(objectID, address)) {
        return kAudioHardwareUnknownPropertyError;
    }

    const CueletObjectKind kind = CueletObjectKindForID(objectID);
#ifdef CUELET_AUDIO_DIAGNOSTICS
    if (kind == kCueletObjectKindDevice &&
        address->mSelector == kCueletDiagnosticPropertyClear) {
        if (dataSize != sizeof(CFPropertyListRef)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const CFPropertyListRef command = *(const CFPropertyListRef*)data;
        bool shouldClear = command == kCFBooleanTrue;
        if (command != NULL && CFGetTypeID(command) == CFNumberGetTypeID()) {
            int64_t numericValue = 0;
            shouldClear = CFNumberGetValue(
                (CFNumberRef)command,
                kCFNumberSInt64Type,
                &numericValue) && numericValue == 1;
        }
        if (!shouldClear) {
            return kAudioHardwareIllegalOperationError;
        }
        CueletDiagnosticClear();
        return noErr;
    } else if (kind == kCueletObjectKindDevice &&
        (address->mSelector == kCueletDiagnosticPropertySchema ||
         address->mSelector == kCueletDiagnosticPropertyCounters ||
         address->mSelector == kCueletDiagnosticPropertyEvents ||
         address->mSelector == kCueletDiagnosticPropertyEventCount ||
         address->mSelector == kCueletDiagnosticPropertyBuild ||
         address->mSelector == kCueletDiagnosticPropertyEnabled ||
         address->mSelector == kAudioObjectPropertyCustomPropertyInfoList)) {
        return kAudioHardwareIllegalOperationError;
    }
#endif
    if (kind == kCueletObjectKindDevice &&
        address->mSelector == kAudioDevicePropertyNominalSampleRate) {
        if (dataSize != sizeof(Float64)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const Float64 sampleRate = *(const Float64*)data;
        if (!CueletIsSupportedSampleRate(sampleRate)) {
            return kAudioHardwareIllegalOperationError;
        }
        if (sampleRate != atomic_load_explicit(
                &gSampleRate,
                memory_order_seq_cst)) {
            CueletRequestSampleRate(sampleRate);
        }
        return noErr;
    }

    if (kind == kCueletObjectKindInputStream ||
        kind == kCueletObjectKindOutputStream) {
        const bool isInput = kind == kCueletObjectKindInputStream;
        if (address->mSelector == kAudioStreamPropertyIsActive) {
            if (dataSize != sizeof(UInt32)) {
                return kAudioHardwareBadPropertySizeError;
            }
            const bool requestedActive = *(const UInt32*)data != 0;
            if (CueletIOStateSetStreamActive(
                    &gIOState,
                    isInput,
                    requestedActive)) {
                CueletDiagnosticRecordData event = {0};
                event.hostTimeSnapshot = mach_absolute_time();
                event.deviceObjectID = kCueletObjectDevice;
                event.streamObjectID = objectID;
                event.writeAccepted = requestedActive;
                event.resetGeneration = CueletRingGeneration(&gIOState.ring);
                CueletDiagnosticRecord(
                    kCueletDiagnosticStreamActivationChange, &event);
                CueletNotifyProperty(objectID, kAudioStreamPropertyIsActive);
            }
            return noErr;
        }
        if (address->mSelector == kAudioStreamPropertyVirtualFormat ||
            address->mSelector == kAudioStreamPropertyPhysicalFormat) {
            if (dataSize != sizeof(AudioStreamBasicDescription)) {
                return kAudioHardwareBadPropertySizeError;
            }
            const AudioStreamBasicDescription* format = data;
            const OSStatus validation = CueletValidateStreamFormat(format);
            if (validation != noErr) {
                return validation;
            }
            if (format->mSampleRate != atomic_load_explicit(
                    &gSampleRate,
                    memory_order_seq_cst)) {
                CueletRequestSampleRate(format->mSampleRate);
            }
            return noErr;
        }
    }

    if (kind == kCueletObjectKindVolumeControl &&
        (address->mSelector == kAudioLevelControlPropertyScalarValue ||
         address->mSelector == kAudioLevelControlPropertyDecibelValue)) {
        if (dataSize != sizeof(Float32)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const bool isInput = objectID == kCueletObjectInputVolume;
        const Float32 requested = *(const Float32*)data;
        const Float32 value = address->mSelector ==
                kAudioLevelControlPropertyScalarValue
            ? CueletClamp(requested, 0.0F, 1.0F)
            : CueletDecibelsToScalar(requested);
        _Atomic Float32* target = isInput ? &gInputVolume : &gOutputVolume;
        if (atomic_exchange_explicit(
                target,
                value,
                memory_order_seq_cst) != value) {
            CueletNotifyProperty(objectID, address->mSelector);
        }
        return noErr;
    }

    if (kind == kCueletObjectKindMuteControl &&
        address->mSelector == kAudioBooleanControlPropertyValue) {
        if (dataSize != sizeof(UInt32)) {
            return kAudioHardwareBadPropertySizeError;
        }
        const bool isInput = objectID == kCueletObjectInputMute;
        const bool value = *(const UInt32*)data != 0;
        _Atomic bool* target = isInput ? &gInputMuted : &gOutputMuted;
        if (atomic_exchange_explicit(
                target,
                value,
                memory_order_seq_cst) != value) {
            CueletNotifyProperty(objectID, address->mSelector);
        }
        return noErr;
    }

    return kAudioHardwareUnknownPropertyError;
}

static OSStatus Cuelet_StartIO(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID)
{
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    CueletRingReader* reader = CueletReaderForClient(clientID);
    if (reader == NULL) {
        return kAudioHardwareIllegalOperationError;
    }

    const uint64_t runningBefore = atomic_load_explicit(
        &gIOState.runningClientCount, memory_order_seq_cst);
    const uint64_t generationBefore = CueletRingGeneration(&gIOState.ring);
    pthread_mutex_lock(&gStateMutex);
    const OSStatus result = CueletIOStateStart(&gIOState);
    if (result == noErr) {
        CueletRingReaderReset(reader, &gIOState.ring);
        if (atomic_load_explicit(
                &gIOState.runningClientCount,
                memory_order_seq_cst) == 1) {
            CueletResetTimelineMapping();
            atomic_store_explicit(
                &gAnchorHostTime,
                mach_absolute_time(),
                memory_order_seq_cst);
            atomic_fetch_add_explicit(
                &gTimelineSeed,
                1,
                memory_order_seq_cst);
        }
    }
    pthread_mutex_unlock(&gStateMutex);
    CueletDiagnosticRecordData data = {0};
    data.hostTimeSnapshot = mach_absolute_time();
    data.deviceObjectID = deviceObjectID;
    data.clientID = clientID;
    data.sampleRate = atomic_load_explicit(&gSampleRate, memory_order_relaxed);
    data.writePosition = CueletRingLastWriteEnd(&gIOState.ring);
    data.readerPosition = atomic_load_explicit(
        &reader->lastRequestedEnd, memory_order_relaxed);
    data.runningClientCountBefore = runningBefore;
    data.runningClientCountAfter = atomic_load_explicit(
        &gIOState.runningClientCount, memory_order_seq_cst);
    data.resetGenerationBefore = generationBefore;
    data.resetGenerationAfter = CueletRingGeneration(&gIOState.ring);
    data.resetGeneration = data.resetGenerationAfter;
    data.writeAccepted = result == noErr;
    CueletDiagnosticRecord(kCueletDiagnosticStartIO, &data);
    return result;
}

static OSStatus Cuelet_StopIO(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID)
{
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    CueletRingReader* reader = CueletReaderForClient(clientID);
    if (reader == NULL) {
        return kAudioHardwareIllegalOperationError;
    }

    const uint64_t runningBefore = atomic_load_explicit(
        &gIOState.runningClientCount, memory_order_seq_cst);
    const uint64_t generationBefore = CueletRingGeneration(&gIOState.ring);
    pthread_mutex_lock(&gStateMutex);
    const OSStatus result = CueletIOStateStop(&gIOState);
    atomic_store_explicit(&reader->initialized, false, memory_order_seq_cst);
    if (result == noErr && atomic_load_explicit(
            &gIOState.runningClientCount,
            memory_order_seq_cst) == 0) {
        CueletResetTimelineMapping();
    }
    pthread_mutex_unlock(&gStateMutex);
    CueletDiagnosticRecordData data = {0};
    data.hostTimeSnapshot = mach_absolute_time();
    data.deviceObjectID = deviceObjectID;
    data.clientID = clientID;
    data.sampleRate = atomic_load_explicit(&gSampleRate, memory_order_relaxed);
    data.writePosition = CueletRingLastWriteEnd(&gIOState.ring);
    data.readerPosition = atomic_load_explicit(
        &reader->lastRequestedEnd, memory_order_relaxed);
    data.runningClientCountBefore = runningBefore;
    data.runningClientCountAfter = atomic_load_explicit(
        &gIOState.runningClientCount, memory_order_seq_cst);
    data.resetGenerationBefore = generationBefore;
    data.resetGenerationAfter = CueletRingGeneration(&gIOState.ring);
    data.resetGeneration = data.resetGenerationAfter;
    data.writeAccepted = result == noErr;
    CueletDiagnosticRecord(kCueletDiagnosticStopIO, &data);
    return result;
}

static OSStatus Cuelet_GetZeroTimeStamp(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    Float64* sampleTimeOut,
    UInt64* hostTimeOut,
    UInt64* seedOut)
{
    (void)clientID;
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    if (sampleTimeOut == NULL || hostTimeOut == NULL || seedOut == NULL) {
        return kAudioHardwareIllegalOperationError;
    }

    const uint64_t anchor = atomic_load_explicit(
        &gAnchorHostTime,
        memory_order_seq_cst);
    const uint64_t now = mach_absolute_time();
    const Float64 ticksPerFrame = atomic_load_explicit(
        &gHostTicksPerFrame,
        memory_order_seq_cst);
    const Float64 ticksPerPeriod =
        ticksPerFrame * CUELET_RING_CAPACITY_FRAMES;
    const uint64_t period = anchor == 0 || ticksPerPeriod <= 0.0
        ? 0
        : (uint64_t)(((Float64)(now - anchor)) / ticksPerPeriod);
    *sampleTimeOut = (Float64)(period * CUELET_RING_CAPACITY_FRAMES);
    *hostTimeOut = anchor + (uint64_t)((Float64)period * ticksPerPeriod);
    *seedOut = atomic_load_explicit(&gTimelineSeed, memory_order_seq_cst);
    CueletDiagnosticRecordData data = {0};
    data.hostTimeSnapshot = now;
    data.deviceObjectID = deviceObjectID;
    data.clientID = clientID;
    data.sampleRate = atomic_load_explicit(&gSampleRate, memory_order_relaxed);
    data.currentTimeFlags =
        kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid;
    data.currentSampleTimeBits = CueletFloat64Bits(*sampleTimeOut);
    data.currentSampleFrame = (uint64_t)*sampleTimeOut;
    data.currentFrameConversionStatus = kCueletTimelineOK;
    data.currentHostTime = *hostTimeOut;
    data.writePosition = CueletRingLastWriteEnd(&gIOState.ring);
    data.resetGeneration = atomic_load_explicit(
        &gIOState.ring.resetGeneration,
        memory_order_relaxed);
    data.timelineSeed = *seedOut;
    CueletDiagnosticRecord(kCueletDiagnosticGetZeroTimeStamp, &data);
    return noErr;
}

static OSStatus Cuelet_WillDoIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    Boolean* willDoOut,
    Boolean* willDoInPlaceOut)
{
    (void)clientID;
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    const bool supported =
        operationID == kAudioServerPlugInIOOperationReadInput ||
        operationID == kAudioServerPlugInIOOperationWriteMix;
    if (willDoOut != NULL) {
        *willDoOut = supported;
    }
    if (willDoInPlaceOut != NULL) {
        *willDoInPlaceOut = true;
    }
    CueletRecordIOEvent(
        kCueletDiagnosticWillDoIOOperation,
        kAudioObjectUnknown,
        clientID,
        operationID,
        0,
        NULL,
        CueletRingLastWriteEnd(&gIOState.ring),
        0,
        0,
        0,
        supported);
    return noErr;
}

static OSStatus Cuelet_BeginIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo)
{
    (void)clientID;
    (void)operationID;
    (void)ioBufferFrameSize;
    const OSStatus result = CueletValidDriver(driver) &&
        CueletValidDevice(deviceObjectID)
        ? noErr
        : kAudioHardwareBadObjectError;
    CueletRecordIOEvent(
        kCueletDiagnosticBeginIOOperation,
        kAudioObjectUnknown,
        clientID,
        operationID,
        ioBufferFrameSize,
        ioCycleInfo,
        CueletRingLastWriteEnd(&gIOState.ring),
        0,
        0,
        0,
        result == noErr);
    return result;
}

static OSStatus Cuelet_DoIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    AudioObjectID streamObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo,
    void* mainBuffer,
    void* secondaryBuffer)
{
    /* This entire routine is a real-time boundary: no locks, allocation,
       logging, IPC, filesystem calls, Objective-C, or Swift. */
    if (!CueletValidDriver(driver) || !CueletValidDevice(deviceObjectID)) {
        return kAudioHardwareBadObjectError;
    }
    if (mainBuffer == NULL) {
        CueletDiagnosticRecordData data = {0};
        data.hostTimeSnapshot = mach_absolute_time();
        data.deviceObjectID = deviceObjectID;
        data.streamObjectID = streamObjectID;
        data.clientID = clientID;
        data.operationID = operationID;
        data.frameCount = ioBufferFrameSize;
        data.secondaryBufferPresent = secondaryBuffer != NULL;
        data.selectedBuffer = kCueletDiagnosticBufferNone;
        data.bufferSelectionStatus = kAudioHardwareIllegalOperationError;
        data.operationDisposition = kCueletDiagnosticOperationMainBufferMissing;
        data.timelineStatus = kCueletTimelineInvalidArgument;
        data.ringWriteStatus = kCueletRingWriteInvalidArgument;
        data.ringReadStatus = kCueletRingReadInvalidArgument;
        CueletPopulateCycleDiagnostics(&data, ioCycleInfo);
        const CueletDiagnosticEventKind kind =
            operationID == kAudioServerPlugInIOOperationWriteMix
                ? kCueletDiagnosticWriteMix
                : (operationID == kAudioServerPlugInIOOperationReadInput
                    ? kCueletDiagnosticReadInput
                    : kCueletDiagnosticDoIOOperation);
        CueletDiagnosticRecord(kind, &data);
        return kAudioHardwareIllegalOperationError;
    }

    Float32* samples = mainBuffer;
    if (operationID == kAudioServerPlugInIOOperationReadInput) {
        if (streamObjectID != kCueletObjectInputStream) {
            CueletRecordTimelineEvent(
                streamObjectID, clientID, operationID, ioBufferFrameSize,
                ioCycleInfo, kCueletTimelineInvalidArgument,
                0, 0, 0, 0, 0,
                (CueletRingReadResult){
                    .status = kCueletRingReadInvalidArgument,
                    .firstRejectionReason = kCueletRingReadInvalidArgument,
                    .unavailableFrames = ioBufferFrameSize,
                },
                (CueletRingWriteResult){.status = kCueletRingWriteInvalidArgument},
                NULL, mainBuffer, secondaryBuffer,
                kCueletDiagnosticBufferMain,
                kCueletDiagnosticOperationWrongStream,
                NULL);
            return kAudioHardwareBadObjectError;
        }
        if (!atomic_load_explicit(
                &gIOState.inputStreamActive,
                memory_order_seq_cst)) {
            memset(samples, 0, (size_t)ioBufferFrameSize * CUELET_AUDIO_BYTES_PER_FRAME);
            CueletRecordTimelineEvent(
                streamObjectID, clientID, operationID, ioBufferFrameSize,
                ioCycleInfo, kCueletTimelineUninitialized,
                0, 0, 0, 0, 0,
                (CueletRingReadResult){
                    .status = kCueletRingReadStreamInactive,
                    .firstRejectionReason = kCueletRingReadStreamInactive,
                    .unavailableFrames = ioBufferFrameSize,
                },
                (CueletRingWriteResult){.status = kCueletRingWriteTimelineUninitialized},
                samples, mainBuffer, secondaryBuffer,
                kCueletDiagnosticBufferMain,
                kCueletDiagnosticOperationStreamInactive,
                NULL);
            return noErr;
        }
        CueletRingReader* reader = CueletReaderForClient(clientID);
        if (reader == NULL) {
            memset(samples, 0, (size_t)ioBufferFrameSize * CUELET_AUDIO_BYTES_PER_FRAME);
            CueletRecordTimelineEvent(
                streamObjectID, clientID, operationID, ioBufferFrameSize,
                ioCycleInfo, kCueletTimelineUninitialized,
                0, 0, 0, 0, 0,
                (CueletRingReadResult){
                    .status = kCueletRingReadClientReaderUnavailable,
                    .firstRejectionReason =
                        kCueletRingReadClientReaderUnavailable,
                    .unavailableFrames = ioBufferFrameSize,
                },
                (CueletRingWriteResult){.status = kCueletRingWriteTimelineUninitialized},
                samples, mainBuffer, secondaryBuffer,
                kCueletDiagnosticBufferMain,
                kCueletDiagnosticOperationClientReaderMissing,
                NULL);
            return noErr;
        }
        uint64_t inputStart = 0;
        uint64_t outputStart = 0;
        uint64_t sourceStart = 0;
        int64_t observedOffset = 0;
        uint32_t loopbackDelay = 0;
        const CueletTimelineStatus timelineStatus =
            CueletResolveReadTimelineMapping(
            ioCycleInfo,
            ioBufferFrameSize,
            &inputStart,
            &outputStart,
            &sourceStart,
            &observedOffset,
            &loopbackDelay);
        const bool mappingValid = timelineStatus == kCueletTimelineOK;
        const CueletRingReadStatus mappingFailureStatus =
            timelineStatus == kCueletTimelineUninitialized
            ? kCueletRingReadTimelineUninitialized
            : kCueletRingReadMappingInvalid;
        CueletRingReadResult readResult = {
            .status = mappingFailureStatus,
            .firstRejectionReason = mappingFailureStatus,
            .firstRejectedFrame = inputStart,
        };
        if (mappingValid) {
            readResult = CueletRingReadAt(
                &gIOState.ring,
                reader,
                CueletRingGeneration(&gIOState.ring),
                sourceStart,
                samples,
                ioBufferFrameSize);
        } else {
            memset(
                samples,
                0,
                (size_t)ioBufferFrameSize * CUELET_AUDIO_BYTES_PER_FRAME);
            readResult.unavailableFrames = ioBufferFrameSize;
        }
        const bool muted = atomic_load_explicit(
            &gInputMuted,
            memory_order_seq_cst);
        const Float32 volume = atomic_load_explicit(
            &gInputVolume,
            memory_order_seq_cst);
        if (muted || volume == 0.0F) {
            memset(samples, 0, (size_t)ioBufferFrameSize * CUELET_AUDIO_BYTES_PER_FRAME);
        } else if (volume != 1.0F) {
            for (uint32_t index = 0; index < ioBufferFrameSize * 2; ++index) {
                samples[index] *= volume;
            }
        }
        CueletRecordIOEvent(
            kCueletDiagnosticDoIOOperation,
            streamObjectID,
            clientID,
            operationID,
            ioBufferFrameSize,
            ioCycleInfo,
            CueletRingLastWriteEnd(&gIOState.ring),
            atomic_load_explicit(&reader->lastRequestedEnd, memory_order_relaxed),
            0,
            readResult.unavailableFrames > 0 || readResult.staleFrames > 0,
            mappingValid);
        CueletRecordTimelineEvent(
            streamObjectID,
            clientID,
            operationID,
            ioBufferFrameSize,
            ioCycleInfo,
            timelineStatus,
            inputStart,
            outputStart,
            sourceStart,
            observedOffset,
            loopbackDelay,
            readResult,
            (CueletRingWriteResult){.status = kCueletRingWriteOK},
            samples,
            mainBuffer,
            secondaryBuffer,
            kCueletDiagnosticBufferMain,
            kCueletDiagnosticOperationNormal,
            NULL);
        return noErr;
    }

    if (operationID == kAudioServerPlugInIOOperationWriteMix) {
        CueletDiagnosticRecordData incomingPayload = {0};
        CueletAnalyzePayload(&incomingPayload, samples, ioBufferFrameSize);
        if (streamObjectID != kCueletObjectOutputStream) {
            CueletRecordTimelineEvent(
                streamObjectID, clientID, operationID, ioBufferFrameSize,
                ioCycleInfo, kCueletTimelineInvalidArgument,
                0, 0, 0, 0, 0,
                (CueletRingReadResult){
                    .status = kCueletRingReadInvalidArgument,
                    .firstRejectionReason = kCueletRingReadInvalidArgument,
                    .unavailableFrames = ioBufferFrameSize,
                },
                (CueletRingWriteResult){.status = kCueletRingWriteInvalidArgument},
                samples, mainBuffer, secondaryBuffer,
                kCueletDiagnosticBufferMain,
                kCueletDiagnosticOperationWrongStream,
                &incomingPayload);
            return kAudioHardwareBadObjectError;
        }
        if (!atomic_load_explicit(
                &gIOState.outputStreamActive,
                memory_order_seq_cst)) {
            CueletRecordTimelineEvent(
                streamObjectID, clientID, operationID, ioBufferFrameSize,
                ioCycleInfo, kCueletTimelineUninitialized,
                0, 0, 0, 0, 0,
                (CueletRingReadResult){
                    .status = kCueletRingReadStreamInactive,
                    .firstRejectionReason = kCueletRingReadStreamInactive,
                    .unavailableFrames = ioBufferFrameSize,
                },
                (CueletRingWriteResult){.status = kCueletRingWriteTimelineUninitialized},
                samples, mainBuffer, secondaryBuffer,
                kCueletDiagnosticBufferMain,
                kCueletDiagnosticOperationStreamInactive,
                &incomingPayload);
            return noErr;
        }
        const bool muted = atomic_load_explicit(
            &gOutputMuted,
            memory_order_seq_cst);
        const Float32 volume = atomic_load_explicit(
            &gOutputVolume,
            memory_order_seq_cst);
        if (muted || volume == 0.0F) {
            memset(samples, 0, (size_t)ioBufferFrameSize * CUELET_AUDIO_BYTES_PER_FRAME);
        } else if (volume != 1.0F) {
            for (uint32_t index = 0; index < ioBufferFrameSize * 2; ++index) {
                samples[index] *= volume;
            }
        }
        uint64_t inputStart = 0;
        uint64_t outputStart = 0;
        uint64_t sourceStart = 0;
        int64_t observedOffset = 0;
        const uint32_t loopbackDelay = atomic_load_explicit(
            &gLoopbackDelayFrames,
            memory_order_acquire);
        CueletTimelineStatus timelineStatus = ioCycleInfo == NULL
            ? kCueletTimelineInvalidArgument
            : CueletSampleFrameFromTimestamp(
                &ioCycleInfo->mOutputTime,
                kCueletTimelineOutputTimestampInvalid,
                &outputStart);
        if (timelineStatus == kCueletTimelineOK) {
            CueletObserveOutputTimeline(
                ioCycleInfo, outputStart, ioBufferFrameSize);
        }
        if (ioCycleInfo != NULL) {
            uint64_t optionalInputStart = 0;
            if (CueletSampleFrameFromTimestamp(
                    &ioCycleInfo->mInputTime,
                    kCueletTimelineInputTimestampInvalid,
                    &optionalInputStart) == kCueletTimelineOK) {
                inputStart = optionalInputStart;
                observedOffset = outputStart >= inputStart
                    ? (int64_t)(outputStart - inputStart)
                    : -(int64_t)(inputStart - outputStart);
            }
        }
        CueletRingWriteResult writeResult = {
            .status = kCueletRingWriteInvalidSampleTime,
            .acceptedFrames = 0,
        };
        if (timelineStatus == kCueletTimelineOK) {
            writeResult = CueletRingWriteAtDetailed(
                &gIOState.ring,
                CueletRingGeneration(&gIOState.ring),
                outputStart,
                samples,
                ioBufferFrameSize);
        }
        const bool writeAccepted =
            writeResult.status == kCueletRingWriteOK;
        CueletRingReadResult writeReadResult = {
            .validFrames = writeAccepted ? ioBufferFrameSize : 0,
            .unavailableFrames = writeAccepted ? 0 : ioBufferFrameSize,
            .status = writeAccepted
                ? kCueletRingReadOK
                : kCueletRingReadMappingInvalid,
            .firstRejectionReason = writeAccepted
                ? kCueletRingReadOK
                : kCueletRingReadMappingInvalid,
            .firstRejectedFrame = writeAccepted ? UINT64_MAX : outputStart,
        };
        CueletRecordIOEvent(
            kCueletDiagnosticDoIOOperation,
            streamObjectID,
            clientID,
            operationID,
            ioBufferFrameSize,
            ioCycleInfo,
                CueletRingLastWriteEnd(&gIOState.ring),
            0,
            writeAccepted ? 0 : 1,
            0,
            writeAccepted);
        CueletRecordTimelineEvent(
            streamObjectID,
            clientID,
            operationID,
            ioBufferFrameSize,
            ioCycleInfo,
            timelineStatus,
            inputStart,
            outputStart,
            sourceStart,
            observedOffset,
            loopbackDelay,
            writeReadResult,
            writeResult,
            samples,
            mainBuffer,
            secondaryBuffer,
            kCueletDiagnosticBufferMain,
            kCueletDiagnosticOperationNormal,
            &incomingPayload);
        return noErr;
    }

    CueletDiagnosticRecordData unsupported = {0};
    unsupported.hostTimeSnapshot = mach_absolute_time();
    unsupported.deviceObjectID = deviceObjectID;
    unsupported.streamObjectID = streamObjectID;
    unsupported.clientID = clientID;
    unsupported.operationID = operationID;
    unsupported.frameCount = ioBufferFrameSize;
    unsupported.sampleRate = atomic_load_explicit(
        &gSampleRate, memory_order_relaxed);
    unsupported.resetGeneration = CueletRingGeneration(&gIOState.ring);
    unsupported.mainBufferPresent = mainBuffer != NULL;
    unsupported.secondaryBufferPresent = secondaryBuffer != NULL;
    unsupported.selectedBuffer = kCueletDiagnosticBufferNone;
    unsupported.operationDisposition =
        kCueletDiagnosticOperationUnsupported;
    CueletPopulateCycleDiagnostics(&unsupported, ioCycleInfo);
    CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &unsupported);
    return noErr;
}

static OSStatus Cuelet_EndIOOperation(
    AudioServerPlugInDriverRef driver,
    AudioObjectID deviceObjectID,
    UInt32 clientID,
    UInt32 operationID,
    UInt32 ioBufferFrameSize,
    const AudioServerPlugInIOCycleInfo* ioCycleInfo)
{
    const OSStatus result = CueletValidDriver(driver) &&
        CueletValidDevice(deviceObjectID)
        ? noErr
        : kAudioHardwareBadObjectError;
    CueletRecordIOEvent(
        kCueletDiagnosticEndIOOperation,
        kAudioObjectUnknown,
        clientID,
        operationID,
        ioBufferFrameSize,
        ioCycleInfo,
        CueletRingLastWriteEnd(&gIOState.ring),
        0,
        0,
        0,
        result == noErr);
    return result;
}
