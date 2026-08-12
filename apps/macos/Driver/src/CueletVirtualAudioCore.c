#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static uint32_t CueletFloatBits(Float32 value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static Float32 CueletBitsFloat(uint32_t bits)
{
    Float32 value = 0.0F;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t CueletPackStereo(Float32 left, Float32 right)
{
    return (uint64_t)CueletFloatBits(left) |
        ((uint64_t)CueletFloatBits(right) << 32);
}

static void CueletUnpackStereo(
    uint64_t packed,
    Float32* leftOut,
    Float32* rightOut)
{
    *leftOut = CueletBitsFloat((uint32_t)(packed & UINT32_MAX));
    *rightOut = CueletBitsFloat((uint32_t)(packed >> 32));
}

static void CueletRecordRingEvent(
    CueletDiagnosticEventKind kind,
    uint32_t frameCount,
    uint64_t writePosition,
    uint64_t readerPosition,
    uint64_t resetGeneration,
    uint64_t underrunCount,
    uint64_t overrunCount,
    uint64_t rejectedWriteCount,
    uint32_t producerContention,
    uint32_t readerJump,
    uint32_t writeAccepted)
{
    CueletDiagnosticRecordData data = {0};
    data.frameCount = frameCount;
    data.writePosition = writePosition;
    data.readerPosition = readerPosition;
    data.resetGeneration = resetGeneration;
    data.underrunCount = underrunCount;
    data.overrunCount = overrunCount;
    data.rejectedWriteCount = rejectedWriteCount;
    data.producerContention = producerContention;
    data.readerJump = readerJump;
    data.writeAccepted = writeAccepted;
    data.expectedGeneration = resetGeneration;
    CueletDiagnosticRecord(kind, &data);
}

CueletObjectKind CueletObjectKindForID(AudioObjectID objectID)
{
    switch (objectID) {
    case kCueletObjectPlugIn:
        return kCueletObjectKindPlugIn;
    case kCueletObjectDevice:
        return kCueletObjectKindDevice;
    case kCueletObjectInputStream:
        return kCueletObjectKindInputStream;
    case kCueletObjectOutputStream:
        return kCueletObjectKindOutputStream;
    case kCueletObjectInputVolume:
    case kCueletObjectOutputVolume:
        return kCueletObjectKindVolumeControl;
    case kCueletObjectInputMute:
    case kCueletObjectOutputMute:
        return kCueletObjectKindMuteControl;
    default:
        return kCueletObjectKindUnknown;
    }
}

AudioObjectID CueletObjectOwner(AudioObjectID objectID)
{
    switch (CueletObjectKindForID(objectID)) {
    case kCueletObjectKindDevice:
        return kCueletObjectPlugIn;
    case kCueletObjectKindInputStream:
    case kCueletObjectKindOutputStream:
    case kCueletObjectKindVolumeControl:
    case kCueletObjectKindMuteControl:
        return kCueletObjectDevice;
    case kCueletObjectKindPlugIn:
    case kCueletObjectKindUnknown:
        return kAudioObjectUnknown;
    }
}

bool CueletObjectSupportsProperty(
    AudioObjectID objectID,
    const AudioObjectPropertyAddress* address)
{
    if (address == NULL) {
        return false;
    }

    const CueletObjectKind kind = CueletObjectKindForID(objectID);
#ifdef CUELET_AUDIO_DIAGNOSTICS
    if (kind == kCueletObjectKindDevice &&
        (address->mSelector == kAudioObjectPropertyCustomPropertyInfoList ||
         address->mSelector == kCueletDiagnosticPropertySchema ||
         address->mSelector == kCueletDiagnosticPropertyCounters ||
         address->mSelector == kCueletDiagnosticPropertyEvents ||
         address->mSelector == kCueletDiagnosticPropertyEventCount ||
         address->mSelector == kCueletDiagnosticPropertyClear ||
         address->mSelector == kCueletDiagnosticPropertyBuild ||
         address->mSelector == kCueletDiagnosticPropertyEnabled)) {
        return address->mScope == kAudioObjectPropertyScopeGlobal &&
            address->mElement == kAudioObjectPropertyElementMain;
    }
#endif

    switch (kind) {
    case kCueletObjectKindPlugIn:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyBoxList:
        case kAudioPlugInPropertyTranslateUIDToBox:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
            return true;
        default:
            return false;
        }
    case kCueletObjectKindDevice:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyStreams:
        case kAudioObjectPropertyControlList:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
            return true;
        default:
            return false;
        }
    case kCueletObjectKindInputStream:
    case kCueletObjectKindOutputStream:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            return true;
        default:
            return false;
        }
    case kCueletObjectKindVolumeControl:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
        case kAudioLevelControlPropertyScalarValue:
        case kAudioLevelControlPropertyDecibelValue:
        case kAudioLevelControlPropertyDecibelRange:
        case kAudioLevelControlPropertyConvertScalarToDecibels:
        case kAudioLevelControlPropertyConvertDecibelsToScalar:
            return true;
        default:
            return false;
        }
    case kCueletObjectKindMuteControl:
        switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyOwnedObjects:
        case kAudioControlPropertyScope:
        case kAudioControlPropertyElement:
        case kAudioBooleanControlPropertyValue:
            return true;
        default:
            return false;
        }
    case kCueletObjectKindUnknown:
        return false;
    }
}

bool CueletIsSupportedSampleRate(Float64 sampleRate)
{
    return sampleRate == 44100.0 || sampleRate == 48000.0;
}

AudioStreamBasicDescription CueletMakeStreamFormat(Float64 sampleRate)
{
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsFloat |
        kAudioFormatFlagsNativeEndian |
        kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = CUELET_AUDIO_BYTES_PER_FRAME;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = CUELET_AUDIO_BYTES_PER_FRAME;
    format.mChannelsPerFrame = CUELET_AUDIO_CHANNEL_COUNT;
    format.mBitsPerChannel = 32;
    return format;
}

OSStatus CueletValidateStreamFormat(
    const AudioStreamBasicDescription* format)
{
    if (format == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    const AudioFormatFlags expectedFlags =
        kAudioFormatFlagIsFloat |
        kAudioFormatFlagsNativeEndian |
        kAudioFormatFlagIsPacked;
    if (format->mFormatID != kAudioFormatLinearPCM ||
        format->mFormatFlags != expectedFlags ||
        format->mBytesPerPacket != CUELET_AUDIO_BYTES_PER_FRAME ||
        format->mFramesPerPacket != 1 ||
        format->mBytesPerFrame != CUELET_AUDIO_BYTES_PER_FRAME ||
        format->mChannelsPerFrame != CUELET_AUDIO_CHANNEL_COUNT ||
        format->mBitsPerChannel != 32 ||
        !CueletIsSupportedSampleRate(format->mSampleRate)) {
        return kAudioDeviceUnsupportedFormatError;
    }
    return noErr;
}

CueletTimelineStatus CueletSampleFrameFromTimestamp(
    const AudioTimeStamp* timestamp,
    CueletTimelineStatus invalidTimestampStatus,
    uint64_t* frameOut)
{
    if (timestamp == NULL || frameOut == NULL) {
        return kCueletTimelineInvalidArgument;
    }
    if ((timestamp->mFlags & kAudioTimeStampSampleTimeValid) == 0 ||
        !isfinite(timestamp->mSampleTime)) {
        return invalidTimestampStatus;
    }
    if (timestamp->mSampleTime < 0.0) {
        return kCueletTimelineNegativeSampleTime;
    }
    if (timestamp->mSampleTime > (Float64)INT64_MAX) {
        return kCueletTimelineSampleTimeOverflow;
    }
    Float64 integral = 0.0;
    if (modf(timestamp->mSampleTime, &integral) != 0.0) {
        return kCueletTimelineFractionalSampleTime;
    }
    *frameOut = (uint64_t)integral;
    return kCueletTimelineOK;
}

void CueletRingInitialize(CueletRingBuffer* ring)
{
    if (ring == NULL) {
        return;
    }
    atomic_init(&ring->resetGeneration, 0);
    atomic_init(&ring->underrunCount, 0);
    atomic_init(&ring->overrunCount, 0);
    atomic_init(&ring->rejectedWriteCount, 0);
    atomic_init(&ring->lastWriteStart, 0);
    atomic_init(&ring->lastWriteEnd, 0);
    for (uint32_t index = 0; index < CUELET_RING_CAPACITY_FRAMES; ++index) {
        atomic_init(&ring->frames[index].frame, UINT64_MAX);
        atomic_init(&ring->frames[index].generation, 0);
        atomic_init(&ring->frames[index].sampleBits, 0);
    }
}

void CueletRingReset(CueletRingBuffer* ring)
{
    if (ring == NULL) {
        return;
    }
    const uint64_t generationBefore = atomic_fetch_add_explicit(
        &ring->resetGeneration,
        1,
        memory_order_release);
    atomic_store_explicit(&ring->lastWriteStart, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->lastWriteEnd, 0, memory_order_relaxed);
    CueletDiagnosticRecordData data = {0};
    data.resetGeneration = generationBefore + 1;
    data.resetGenerationBefore = generationBefore;
    data.resetGenerationAfter = generationBefore + 1;
    data.underrunCount = atomic_load_explicit(
        &ring->underrunCount, memory_order_relaxed);
    data.overrunCount = atomic_load_explicit(
        &ring->overrunCount, memory_order_relaxed);
    data.rejectedWriteCount = atomic_load_explicit(
        &ring->rejectedWriteCount, memory_order_relaxed);
    CueletDiagnosticRecord(kCueletDiagnosticRingReset, &data);
}

uint64_t CueletRingGeneration(const CueletRingBuffer* ring)
{
    return ring == NULL
        ? 0
        : atomic_load_explicit(&ring->resetGeneration, memory_order_acquire);
}

uint64_t CueletRingLastWriteEnd(const CueletRingBuffer* ring)
{
    return ring == NULL
        ? 0
        : atomic_load_explicit(&ring->lastWriteEnd, memory_order_acquire);
}

void CueletRingReaderReset(
    CueletRingReader* reader,
    const CueletRingBuffer* ring)
{
    if (reader == NULL || ring == NULL) {
        return;
    }
    atomic_store_explicit(&reader->lastRequestedStart, 0, memory_order_relaxed);
    atomic_store_explicit(&reader->lastRequestedEnd, 0, memory_order_relaxed);
    atomic_store_explicit(
        &reader->generation,
        CueletRingGeneration(ring),
        memory_order_release);
    atomic_store_explicit(&reader->initialized, true, memory_order_release);
}

CueletRingWriteResult CueletRingWriteAtDetailed(
    CueletRingBuffer* ring,
    uint64_t generation,
    uint64_t startFrame,
    const Float32* interleavedStereo,
    uint32_t frameCount)
{
    CueletRingWriteResult result = {
        .status = kCueletRingWriteInvalidArgument,
        .acceptedFrames = 0,
        .generation = generation,
        .firstSlot = UINT64_MAX,
        .finalSlot = UINT64_MAX,
        .firstPublishedFrame = UINT64_MAX,
        .finalPublishedFrame = UINT64_MAX,
    };
    if (ring == NULL || interleavedStereo == NULL || frameCount == 0) {
        return result;
    }
    if (startFrame > UINT64_MAX - frameCount) {
        result.status = kCueletRingWriteInvalidSampleTime;
        return result;
    }
    if (generation != CueletRingGeneration(ring)) {
        result.status = kCueletRingWriteGenerationMismatch;
        atomic_fetch_add_explicit(
            &ring->rejectedWriteCount,
            1,
            memory_order_relaxed);
        CueletRecordRingEvent(
            kCueletDiagnosticProducerRejected,
            frameCount,
            atomic_load_explicit(&ring->lastWriteEnd, memory_order_relaxed),
            0,
            generation,
            atomic_load_explicit(&ring->underrunCount, memory_order_relaxed),
            atomic_load_explicit(&ring->overrunCount, memory_order_relaxed),
            atomic_load_explicit(&ring->rejectedWriteCount, memory_order_relaxed),
            0,
            0,
            0);
        return result;
    }

    const uint32_t storedFrames = frameCount > CUELET_RING_CAPACITY_FRAMES
        ? CUELET_RING_CAPACITY_FRAMES
        : frameCount;
    const uint32_t sourceOffset = frameCount - storedFrames;
    const uint64_t storedStart = startFrame + sourceOffset;

    for (uint32_t index = 0; index < storedFrames; ++index) {
        const uint64_t absoluteFrame = storedStart + index;
        CueletTimelineFrame* slot =
            &ring->frames[absoluteFrame % CUELET_RING_CAPACITY_FRAMES];
        atomic_store_explicit(
            &slot->frame,
            UINT64_MAX,
            memory_order_relaxed);
        atomic_store_explicit(
            &slot->sampleBits,
            CueletPackStereo(
                interleavedStereo[(sourceOffset + index) * 2],
                interleavedStereo[(sourceOffset + index) * 2 + 1]),
            memory_order_relaxed);
        atomic_store_explicit(
            &slot->generation,
            generation,
            memory_order_relaxed);
        atomic_store_explicit(
            &slot->frame,
            absoluteFrame,
            memory_order_release);
    }
    atomic_store_explicit(
        &ring->lastWriteStart,
        storedStart,
        memory_order_release);
    atomic_store_explicit(
        &ring->lastWriteEnd,
        startFrame + frameCount,
        memory_order_release);
    CueletRecordRingEvent(
        kCueletDiagnosticRingWrite,
        frameCount,
        startFrame + frameCount,
        0,
        generation,
        atomic_load_explicit(&ring->underrunCount, memory_order_relaxed),
        atomic_load_explicit(&ring->overrunCount, memory_order_relaxed),
        atomic_load_explicit(&ring->rejectedWriteCount, memory_order_relaxed),
        0,
        0,
        1);
    result.status = kCueletRingWriteOK;
    result.acceptedFrames = storedFrames;
    result.firstSlot = storedStart % CUELET_RING_CAPACITY_FRAMES;
    result.finalSlot = (startFrame + frameCount - 1) % CUELET_RING_CAPACITY_FRAMES;
    result.firstPublishedFrame = storedStart;
    result.finalPublishedFrame = startFrame + frameCount - 1;
    return result;
}

bool CueletRingWriteAt(
    CueletRingBuffer* ring,
    uint64_t generation,
    uint64_t startFrame,
    const Float32* interleavedStereo,
    uint32_t frameCount)
{
    return CueletRingWriteAtDetailed(
        ring,
        generation,
        startFrame,
        interleavedStereo,
        frameCount).status == kCueletRingWriteOK;
}

CueletRingReadResult CueletRingReadAt(
    CueletRingBuffer* ring,
    CueletRingReader* reader,
    uint64_t generation,
    uint64_t startFrame,
    Float32* interleavedStereo,
    uint32_t frameCount)
{
    CueletRingReadResult result = {
        .status = kCueletRingReadInvalidArgument,
        .firstRejectionReason = kCueletRingReadOK,
        .firstRejectedFrame = UINT64_MAX,
        .expectedGeneration = generation,
        .observedGeneration = UINT64_MAX,
        .expectedFrame = UINT64_MAX,
        .observedFrame = UINT64_MAX,
        .firstSlot = startFrame % CUELET_RING_CAPACITY_FRAMES,
        .finalSlot = frameCount > 0
            ? (startFrame + frameCount - 1) % CUELET_RING_CAPACITY_FRAMES
            : UINT64_MAX,
    };
    if (ring == NULL || reader == NULL || interleavedStereo == NULL ||
        frameCount == 0 || startFrame > UINT64_MAX - frameCount) {
        return result;
    }

    memset(
        interleavedStereo,
        0,
        (size_t)frameCount * CUELET_AUDIO_BYTES_PER_FRAME);

    const uint64_t currentGeneration = CueletRingGeneration(ring);
    const bool readerInitialized = atomic_load_explicit(
        &reader->initialized, memory_order_acquire);
    result.readerInitiallyInitialized = readerInitialized ? 1U : 0U;
    uint64_t readerGeneration = atomic_load_explicit(
        &reader->generation, memory_order_acquire);
    if (!readerInitialized) {
        /* AddDeviceClient and StopIO are control-plane lifecycle events, but
         * Core Audio may still select that client's reader for a mapped
         * ReadInput operation while the device remains globally active. A
         * false per-client lifecycle latch is therefore not evidence that the
         * absolute ring range is unavailable. Adopt the coherent ring
         * generation and let the authoritative per-slot generation/tag checks
         * decide whether audio exists. */
        readerGeneration = currentGeneration;
        atomic_store_explicit(
            &reader->generation,
            readerGeneration,
            memory_order_release);
        atomic_store_explicit(
            &reader->initialized,
            true,
            memory_order_release);
        result.readerGenerationAdopted = 1;
    }
    result.generationResolved = 1;
    if (generation != currentGeneration || readerGeneration != generation) {
        atomic_store_explicit(
            &reader->generation,
            currentGeneration,
            memory_order_release);
        atomic_store_explicit(
            &reader->initialized,
            true,
            memory_order_release);
        atomic_fetch_add_explicit(
            &ring->underrunCount,
            1,
            memory_order_relaxed);
        result.unavailableFrames = frameCount;
        result.status = kCueletRingReadGenerationMismatch;
        result.firstRejectionReason = result.status;
        result.firstRejectedFrame = startFrame;
        result.rejectionFrameCounts[kCueletRingReadGenerationMismatch] =
            frameCount;
        return result;
    }

    result.preRingAccepted = 1;

    atomic_store_explicit(
        &reader->lastRequestedStart,
        startFrame,
        memory_order_relaxed);
    atomic_store_explicit(
        &reader->lastRequestedEnd,
        startFrame + frameCount,
        memory_order_relaxed);

    result.ringLookupReached = 1;
    result.ringLookupFrames = frameCount;
    for (uint32_t index = 0; index < frameCount; ++index) {
        const uint64_t absoluteFrame = startFrame + index;
        CueletTimelineFrame* slot =
            &ring->frames[absoluteFrame % CUELET_RING_CAPACITY_FRAMES];
        const uint64_t frameBefore = atomic_load_explicit(
            &slot->frame,
            memory_order_acquire);
        const uint64_t generationBefore = atomic_load_explicit(
            &slot->generation,
            memory_order_acquire);
        CueletRingReadStatus rejection = kCueletRingReadOK;
        if (frameBefore == UINT64_MAX) {
            rejection = kCueletRingReadUnpublished;
        } else if (frameBefore < absoluteFrame) {
            rejection = kCueletRingReadNotYetWritten;
        } else if (frameBefore > absoluteFrame) {
            rejection = kCueletRingReadOverwritten;
        } else if (generationBefore != generation) {
            rejection = kCueletRingReadGenerationMismatch;
        }
        if (rejection != kCueletRingReadOK) {
            if (result.firstRejectionReason == kCueletRingReadOK) {
                result.firstRejectionReason = rejection;
                result.firstRejectedFrame = absoluteFrame;
                result.expectedFrame = absoluteFrame;
                result.observedFrame = frameBefore;
                result.observedGeneration = generationBefore;
            }
            if (frameBefore != UINT64_MAX) {
                ++result.staleFrames;
            } else {
                ++result.unavailableFrames;
            }
            ++result.rejectionFrameCounts[rejection];
            continue;
        }
        const uint64_t packed = atomic_load_explicit(
            &slot->sampleBits,
            memory_order_relaxed);
        const uint64_t frameAfter = atomic_load_explicit(
            &slot->frame,
            memory_order_acquire);
        const uint64_t generationAfter = atomic_load_explicit(
            &slot->generation,
            memory_order_acquire);
        if (frameAfter != absoluteFrame || generationAfter != generation) {
            const CueletRingReadStatus postReadRejection =
                generationAfter != generation
                ? kCueletRingReadGenerationMismatch
                : kCueletRingReadAbsoluteFrameMismatch;
            if (result.firstRejectionReason == kCueletRingReadOK) {
                result.firstRejectionReason = postReadRejection;
                result.firstRejectedFrame = absoluteFrame;
                result.expectedFrame = absoluteFrame;
                result.observedFrame = frameAfter;
                result.observedGeneration = generationAfter;
            }
            ++result.staleFrames;
            ++result.rejectionFrameCounts[postReadRejection];
            continue;
        }
        CueletUnpackStereo(
            packed,
            &interleavedStereo[index * 2],
            &interleavedStereo[index * 2 + 1]);
        ++result.validFrames;
    }

    if (result.unavailableFrames > 0) {
        atomic_fetch_add_explicit(
            &ring->underrunCount,
            1,
            memory_order_relaxed);
    }
    if (result.staleFrames > 0) {
        atomic_fetch_add_explicit(
            &ring->overrunCount,
            1,
            memory_order_relaxed);
    }
    if (result.firstRejectionReason == kCueletRingReadOK) {
        result.status = kCueletRingReadOK;
    } else if (result.validFrames > 0) {
        result.status = kCueletRingReadPartialRange;
    } else {
        result.status = result.firstRejectionReason;
    }
    CueletRecordRingEvent(
        kCueletDiagnosticRingRead,
        frameCount,
        atomic_load_explicit(&ring->lastWriteEnd, memory_order_relaxed),
        startFrame + frameCount,
        generation,
        atomic_load_explicit(&ring->underrunCount, memory_order_relaxed),
        atomic_load_explicit(&ring->overrunCount, memory_order_relaxed),
        atomic_load_explicit(&ring->rejectedWriteCount, memory_order_relaxed),
        0,
        result.staleFrames > 0,
        0);
    return result;
}

void CueletIOStateInitialize(CueletIOState* state)
{
    if (state == NULL) {
        return;
    }
    CueletRingInitialize(&state->ring);
    atomic_init(&state->runningClientCount, 0);
    atomic_init(&state->inputStreamActive, true);
    atomic_init(&state->outputStreamActive, true);
}

OSStatus CueletIOStateStart(CueletIOState* state)
{
    if (state == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    const uint64_t previous = atomic_fetch_add_explicit(
        &state->runningClientCount,
        1,
        memory_order_seq_cst);
    if (previous == UINT64_MAX) {
        atomic_store_explicit(
            &state->runningClientCount,
            UINT64_MAX,
            memory_order_seq_cst);
        return kAudioHardwareIllegalOperationError;
    }
    if (previous == 0) {
        CueletRingReset(&state->ring);
    }
    return noErr;
}

OSStatus CueletIOStateStop(CueletIOState* state)
{
    if (state == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    uint64_t current = atomic_load_explicit(
        &state->runningClientCount,
        memory_order_seq_cst);
    do {
        if (current == 0) {
            return kAudioHardwareIllegalOperationError;
        }
    } while (!atomic_compare_exchange_weak_explicit(
        &state->runningClientCount,
        &current,
        current - 1,
        memory_order_seq_cst,
        memory_order_seq_cst));
    if (current == 1) {
        CueletRingReset(&state->ring);
    }
    return noErr;
}

bool CueletIOStateSetStreamActive(
    CueletIOState* state,
    bool isInput,
    bool isActive)
{
    if (state == NULL) {
        return false;
    }
    _Atomic bool* target = isInput
        ? &state->inputStreamActive
        : &state->outputStreamActive;
    return atomic_exchange_explicit(
        target,
        isActive,
        memory_order_seq_cst) != isActive;
}

#ifdef CUELET_AUDIO_TESTING
void CueletRingForceGenerationForTesting(
    CueletRingBuffer* ring,
    uint64_t generation)
{
    if (ring != NULL) {
        atomic_store_explicit(
            &ring->resetGeneration,
            generation,
            memory_order_seq_cst);
    }
}
#endif
