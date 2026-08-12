/*
 * Input-only Core Audio diagnostic receiver for Cuelet.
 *
 * This uses Apple's HAL Output Audio Unit with input enabled and output
 * disabled. AudioUnitRender is called from the real-time input callback into
 * preallocated non-interleaved Float32 buffers. The callback performs no file
 * I/O, allocation, formatted logging, or blocking synchronization. Captured
 * blocks and timestamp records are drained and serialized by the non-real-time
 * thread in main().
 *
 * Build:
 *   clang -O2 -Wall -Wextra -Werror -framework AudioToolbox \
 *     -framework CoreAudio -framework CoreFoundation \
 *     apps/macos/Driver/tools/cuelet-auhal-receiver.c \
 *     -o /tmp/cuelet-auhal-receiver
 *
 * Run:
 *   /tmp/cuelet-auhal-receiver <uid> <seconds> <wav> <jsonl> [44100|48000]
 */

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_BLOCKS 2048U
#define MAX_BLOCK_FRAMES 8192U
#define MAX_EVENTS 250000U

typedef struct CaptureBlock {
    _Atomic bool ready;
    uint32_t frameCount;
    uint64_t eventIndex;
    Float32 samples[MAX_BLOCK_FRAMES * 2U];
} CaptureBlock;

typedef struct PayloadSummary {
    uint64_t checksum;
    Float32 peakLeft;
    Float32 peakRight;
    Float32 rmsLeft;
    Float32 rmsRight;
    uint32_t zeroFrames;
    uint32_t nonzeroFrames;
    uint32_t firstZeroFrame;
    uint32_t longestZeroRunStart;
    uint32_t longestZeroRunFrames;
} PayloadSummary;

typedef struct CaptureEvent {
    uint64_t sequence;
    uint64_t hostTime;
    Float64 sampleTime;
    uint32_t flags;
    uint32_t requestedFrames;
    uint32_t actualFrames;
    int32_t renderStatus;
    int32_t sampleDelta;
    int64_t hostDelta;
    uint32_t callbackSizeChanged;
    uint32_t sampleTimeJump;
    uint32_t hostTimeRegression;
    uint32_t callbackIntervalGap;
    uint32_t processID;
    uint32_t renderActionFlagsBefore;
    uint32_t renderActionFlagsAfter;
    uint32_t renderBufferCount;
    uint32_t renderBuffer0Bytes;
    uint32_t renderBuffer1Bytes;
    uint32_t renderBuffersValid;
    uint32_t renderSentinelFrames;
    uint64_t renderChecksum;
    Float32 renderPeakLeft;
    Float32 renderPeakRight;
    Float32 renderRMSLeft;
    Float32 renderRMSRight;
    uint32_t renderZeroFrames;
    uint32_t renderNonzeroFrames;
    uint32_t renderFirstZeroFrame;
    uint32_t renderLongestZeroRunStart;
    uint32_t renderLongestZeroRunFrames;
    uint64_t copiedChecksum;
    Float32 copiedPeakLeft;
    Float32 copiedPeakRight;
    Float32 copiedRMSLeft;
    Float32 copiedRMSRight;
    uint32_t copiedZeroFrames;
    uint32_t copiedNonzeroFrames;
    uint32_t copiedFirstZeroFrame;
    uint32_t copiedLongestZeroRunStart;
    uint32_t copiedLongestZeroRunFrames;
    uint64_t wavChecksum;
    uint64_t wavStartFrame;
    uint32_t wavFrames;
    uint32_t wavWriteShort;
} CaptureEvent;

typedef struct CaptureContext {
    AudioUnit audioUnit;
    AudioStreamBasicDescription format;
    CaptureBlock* blocks;
    CaptureEvent* events;
    _Atomic uint64_t nextBlock;
    _Atomic uint64_t nextBlockToDrain;
    _Atomic uint64_t nextEvent;
    _Atomic uint64_t blockDrops;
    _Atomic uint64_t eventDrops;
    _Atomic uint64_t callbackCount;
    _Atomic uint64_t capturedFrames;
    _Atomic uint64_t renderErrors;
    _Atomic uint64_t invalidRenderBuffers;
    _Atomic uint64_t allZeroCallbacks;
    _Atomic uint64_t shortWavWrites;
    Float32 discardSamples[MAX_BLOCK_FRAMES * 2U];
    Float32 renderSamples[MAX_BLOCK_FRAMES * 2U];
    uint64_t previousHostTime;
    Float64 previousSampleTime;
    uint32_t previousFrames;
    bool previousTimestampValid;
    bool previousFrameCountValid;
    uint32_t processID;
    uint32_t deviceID;
    uint64_t audioUnitStartHostTime;
    uint64_t stopRequestedHostTime;
    uint64_t audioUnitStoppedHostTime;
} CaptureContext;

static volatile sig_atomic_t gInterrupted = 0;

static void handleSignal(int signalNumber)
{
    (void)signalNumber;
    gInterrupted = 1;
}

static uint64_t hostTimeNow(void)
{
    return mach_absolute_time();
}

static double hostSeconds(uint64_t ticks)
{
    static mach_timebase_info_data_t timebase;
    static bool initialized = false;
    if (!initialized) {
        mach_timebase_info(&timebase);
        initialized = true;
    }
    return (double)ticks * (double)timebase.numer /
        ((double)timebase.denom * 1000000000.0);
}

static PayloadSummary summarizeNoninterleaved(
    const Float32* left,
    const Float32* right,
    uint32_t frameCount)
{
    PayloadSummary summary = {
        .checksum = UINT64_C(1469598103934665603),
        .firstZeroFrame = UINT32_MAX,
        .longestZeroRunStart = UINT32_MAX,
    };
    if (left == NULL || right == NULL || frameCount == 0) return summary;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    uint32_t zeroRunStart = UINT32_MAX;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint32_t leftBits = 0;
        uint32_t rightBits = 0;
        memcpy(&leftBits, &left[frame], sizeof(leftBits));
        memcpy(&rightBits, &right[frame], sizeof(rightBits));
        const uint64_t packed = (uint64_t)leftBits |
            ((uint64_t)rightBits << 32);
        summary.checksum ^= packed;
        summary.checksum *= UINT64_C(1099511628211);
        summary.peakLeft = fmaxf(summary.peakLeft, fabsf(left[frame]));
        summary.peakRight = fmaxf(summary.peakRight, fabsf(right[frame]));
        leftSquares += (double)left[frame] * (double)left[frame];
        rightSquares += (double)right[frame] * (double)right[frame];
        if (left[frame] == 0.0F && right[frame] == 0.0F) {
            if (summary.firstZeroFrame == UINT32_MAX) {
                summary.firstZeroFrame = frame;
            }
            if (zeroRunStart == UINT32_MAX) zeroRunStart = frame;
            ++summary.zeroFrames;
        } else {
            if (zeroRunStart != UINT32_MAX) {
                const uint32_t runFrames = frame - zeroRunStart;
                if (runFrames > summary.longestZeroRunFrames) {
                    summary.longestZeroRunStart = zeroRunStart;
                    summary.longestZeroRunFrames = runFrames;
                }
                zeroRunStart = UINT32_MAX;
            }
            ++summary.nonzeroFrames;
        }
    }
    if (zeroRunStart != UINT32_MAX) {
        const uint32_t runFrames = frameCount - zeroRunStart;
        if (runFrames > summary.longestZeroRunFrames) {
            summary.longestZeroRunStart = zeroRunStart;
            summary.longestZeroRunFrames = runFrames;
        }
    }
    summary.rmsLeft = (Float32)sqrt(leftSquares / frameCount);
    summary.rmsRight = (Float32)sqrt(rightSquares / frameCount);
    return summary;
}

static PayloadSummary summarizeInterleaved(
    const Float32* samples,
    uint32_t frameCount)
{
    PayloadSummary summary = {
        .checksum = UINT64_C(1469598103934665603),
        .firstZeroFrame = UINT32_MAX,
        .longestZeroRunStart = UINT32_MAX,
    };
    if (samples == NULL || frameCount == 0) return summary;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    uint32_t zeroRunStart = UINT32_MAX;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint32_t leftBits = 0;
        uint32_t rightBits = 0;
        memcpy(&leftBits, &samples[frame * 2], sizeof(leftBits));
        memcpy(&rightBits, &samples[frame * 2 + 1], sizeof(rightBits));
        const uint64_t packed = (uint64_t)leftBits |
            ((uint64_t)rightBits << 32);
        summary.checksum ^= packed;
        summary.checksum *= UINT64_C(1099511628211);
        const Float32 left = samples[frame * 2];
        const Float32 right = samples[frame * 2 + 1];
        summary.peakLeft = fmaxf(summary.peakLeft, fabsf(left));
        summary.peakRight = fmaxf(summary.peakRight, fabsf(right));
        leftSquares += (double)left * (double)left;
        rightSquares += (double)right * (double)right;
        if (left == 0.0F && right == 0.0F) {
            if (summary.firstZeroFrame == UINT32_MAX) {
                summary.firstZeroFrame = frame;
            }
            if (zeroRunStart == UINT32_MAX) zeroRunStart = frame;
            ++summary.zeroFrames;
        } else {
            if (zeroRunStart != UINT32_MAX) {
                const uint32_t runFrames = frame - zeroRunStart;
                if (runFrames > summary.longestZeroRunFrames) {
                    summary.longestZeroRunStart = zeroRunStart;
                    summary.longestZeroRunFrames = runFrames;
                }
                zeroRunStart = UINT32_MAX;
            }
            ++summary.nonzeroFrames;
        }
    }
    if (zeroRunStart != UINT32_MAX) {
        const uint32_t runFrames = frameCount - zeroRunStart;
        if (runFrames > summary.longestZeroRunFrames) {
            summary.longestZeroRunStart = zeroRunStart;
            summary.longestZeroRunFrames = runFrames;
        }
    }
    summary.rmsLeft = (Float32)sqrt(leftSquares / frameCount);
    summary.rmsRight = (Float32)sqrt(rightSquares / frameCount);
    return summary;
}

static AudioDeviceID findDevice(const char* wantedUID)
{
    const AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(
            kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr ||
        size < sizeof(AudioDeviceID)) {
        return kAudioObjectUnknown;
    }
    AudioDeviceID* devices = calloc(size, 1);
    if (devices == NULL) {
        return kAudioObjectUnknown;
    }
    if (AudioObjectGetPropertyData(
            kAudioObjectSystemObject, &address, 0, NULL, &size, devices) != noErr) {
        free(devices);
        return kAudioObjectUnknown;
    }
    CFStringRef wanted = CFStringCreateWithCString(
        NULL, wantedUID, kCFStringEncodingUTF8);
    AudioDeviceID result = kAudioObjectUnknown;
    if (wanted != NULL) {
        for (UInt32 index = 0; index < size / sizeof(AudioDeviceID); ++index) {
            const AudioObjectPropertyAddress uidAddress = {
                kAudioDevicePropertyDeviceUID,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain,
            };
            CFStringRef uid = NULL;
            UInt32 uidSize = sizeof(uid);
            if (AudioObjectGetPropertyData(
                    devices[index], &uidAddress, 0, NULL, &uidSize, &uid) == noErr &&
                uid != NULL &&
                CFStringCompare(uid, wanted, 0) == kCFCompareEqualTo) {
                result = devices[index];
                break;
            }
        }
        CFRelease(wanted);
    }
    free(devices);
    return result;
}

static OSStatus setRate(AudioDeviceID device, double rate)
{
    const AudioObjectPropertyAddress address = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    Float64 value = rate;
    return AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(value), &value);
}

static void writeLE16(FILE* file, uint16_t value)
{
    fwrite(&value, sizeof(value), 1, file);
}

static void writeLE32(FILE* file, uint32_t value)
{
    fwrite(&value, sizeof(value), 1, file);
}

static void writeWavHeader(FILE* file, uint32_t sampleRate, uint32_t frames)
{
    const uint32_t dataSize = frames * 2U * sizeof(Float32);
    fseek(file, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, file);
    writeLE32(file, 36U + dataSize);
    fwrite("WAVEfmt ", 1, 8, file);
    writeLE32(file, 16);
    writeLE16(file, 3);
    writeLE16(file, 2);
    writeLE32(file, sampleRate);
    writeLE32(file, sampleRate * 2U * sizeof(Float32));
    writeLE16(file, 2U * sizeof(Float32));
    writeLE16(file, 32);
    fwrite("data", 1, 4, file);
    writeLE32(file, dataSize);
}

static OSStatus captureCallback(
    void* refCon,
    AudioUnitRenderActionFlags* flags,
    const AudioTimeStamp* timeStamp,
    UInt32 bus,
    UInt32 frameCount,
    AudioBufferList* ioData)
{
    (void)ioData;
    CaptureContext* context = refCon;
    const uint32_t actionFlagsBefore = flags == NULL ? 0U : *flags;
    const uint64_t sequence = atomic_fetch_add_explicit(
        &context->nextEvent, 1, memory_order_relaxed);
    const uint64_t callbackIndex = atomic_fetch_add_explicit(
        &context->callbackCount, 1, memory_order_relaxed);
    uint32_t actualFrames = frameCount;
    int32_t renderStatus = noErr;
    const bool supported = bus == 1 && frameCount <= MAX_BLOCK_FRAMES;

    CaptureBlock* block = NULL;
    uint64_t blockIndex = atomic_fetch_add_explicit(
        &context->nextBlock, 1, memory_order_relaxed);
    if (supported) {
        block = &context->blocks[blockIndex % MAX_BLOCKS];
        if (atomic_load_explicit(&block->ready, memory_order_acquire)) {
            block = NULL;
            atomic_fetch_add_explicit(&context->blockDrops, 1, memory_order_relaxed);
        }
    } else {
        atomic_fetch_add_explicit(&context->blockDrops, 1, memory_order_relaxed);
    }

    struct {
        UInt32 mNumberBuffers;
        AudioBuffer mBuffers[2];
    } buffers = {0};
    buffers.mNumberBuffers = 2;
    buffers.mBuffers[0].mNumberChannels = 1;
    buffers.mBuffers[0].mDataByteSize = frameCount * sizeof(Float32);
    buffers.mBuffers[0].mData = block != NULL
        ? context->renderSamples
        : context->discardSamples;
    buffers.mBuffers[1].mNumberChannels = 1;
    buffers.mBuffers[1].mDataByteSize = frameCount * sizeof(Float32);
    buffers.mBuffers[1].mData = block != NULL
        ? context->renderSamples + MAX_BLOCK_FRAMES
        : context->discardSamples + MAX_BLOCK_FRAMES;

    const uint32_t sentinelBits = UINT32_C(0x7fc01234);
    for (UInt32 frame = 0; frame < frameCount; ++frame) {
        memcpy(
            &((Float32*)buffers.mBuffers[0].mData)[frame],
            &sentinelBits,
            sizeof(sentinelBits));
        memcpy(
            &((Float32*)buffers.mBuffers[1].mData)[frame],
            &sentinelBits,
            sizeof(sentinelBits));
    }

    renderStatus = AudioUnitRender(
        context->audioUnit,
        flags,
        timeStamp,
        bus,
        frameCount,
        (AudioBufferList*)&buffers);
    const uint32_t actionFlagsAfter = flags == NULL ? 0U : *flags;
    uint32_t sentinelFrames = 0;
    const bool bufferShapeValid = buffers.mNumberBuffers == 2 &&
        buffers.mBuffers[0].mData != NULL &&
        buffers.mBuffers[1].mData != NULL &&
        buffers.mBuffers[0].mDataByteSize >= frameCount * sizeof(Float32) &&
        buffers.mBuffers[1].mDataByteSize >= frameCount * sizeof(Float32);
    if (renderStatus == noErr && bufferShapeValid) {
        for (UInt32 frame = 0; frame < frameCount; ++frame) {
            uint32_t leftBits = 0;
            uint32_t rightBits = 0;
            memcpy(
                &leftBits,
                &((Float32*)buffers.mBuffers[0].mData)[frame],
                sizeof(leftBits));
            memcpy(
                &rightBits,
                &((Float32*)buffers.mBuffers[1].mData)[frame],
                sizeof(rightBits));
            if (leftBits == sentinelBits || rightBits == sentinelBits) {
                ++sentinelFrames;
            }
        }
    }
    const bool renderBuffersValid = renderStatus == noErr &&
        bufferShapeValid && sentinelFrames == 0;
    PayloadSummary renderSummary = {
        .checksum = UINT64_C(1469598103934665603),
        .firstZeroFrame = UINT32_MAX,
        .longestZeroRunStart = UINT32_MAX,
    };
    PayloadSummary copiedSummary = renderSummary;
    if (renderBuffersValid) {
        renderSummary = summarizeNoninterleaved(
            buffers.mBuffers[0].mData,
            buffers.mBuffers[1].mData,
            frameCount);
        if (renderSummary.zeroFrames == frameCount) {
            atomic_fetch_add_explicit(
                &context->allZeroCallbacks, 1, memory_order_relaxed);
        }
    } else if (renderStatus != noErr) {
        atomic_fetch_add_explicit(
            &context->renderErrors, 1, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(
            &context->invalidRenderBuffers, 1, memory_order_relaxed);
    }
    if (block != NULL) {
        if (!renderBuffersValid) {
            actualFrames = 0;
            memset(block->samples, 0, sizeof(block->samples));
        } else {
            for (UInt32 frame = 0; frame < frameCount; ++frame) {
                block->samples[frame * 2] =
                    ((Float32*)buffers.mBuffers[0].mData)[frame];
                block->samples[frame * 2 + 1] =
                    ((Float32*)buffers.mBuffers[1].mData)[frame];
            }
            copiedSummary = summarizeInterleaved(block->samples, frameCount);
        }
        block->frameCount = actualFrames;
        block->eventIndex = sequence < MAX_EVENTS ? sequence : UINT64_MAX;
    }

    if (sequence < MAX_EVENTS) {
        CaptureEvent* event = &context->events[sequence];
        event->sequence = callbackIndex;
        event->hostTime = timeStamp != NULL ? timeStamp->mHostTime : 0;
        event->sampleTime = timeStamp != NULL ? timeStamp->mSampleTime : 0.0;
        event->flags = timeStamp != NULL ? timeStamp->mFlags : 0;
        event->requestedFrames = frameCount;
        event->actualFrames = actualFrames;
        event->renderStatus = renderStatus;
        event->sampleDelta = 0;
        event->hostDelta = 0;
        event->callbackSizeChanged = context->previousFrameCountValid &&
            context->previousFrames != frameCount;
        event->sampleTimeJump = false;
        event->hostTimeRegression = false;
        event->callbackIntervalGap = false;
        event->processID = context->processID;
        event->renderActionFlagsBefore = actionFlagsBefore;
        event->renderActionFlagsAfter = actionFlagsAfter;
        event->renderBufferCount = buffers.mNumberBuffers;
        event->renderBuffer0Bytes = buffers.mBuffers[0].mDataByteSize;
        event->renderBuffer1Bytes = buffers.mBuffers[1].mDataByteSize;
        event->renderBuffersValid = renderBuffersValid;
        event->renderSentinelFrames = sentinelFrames;
        event->renderChecksum = renderSummary.checksum;
        event->renderPeakLeft = renderSummary.peakLeft;
        event->renderPeakRight = renderSummary.peakRight;
        event->renderRMSLeft = renderSummary.rmsLeft;
        event->renderRMSRight = renderSummary.rmsRight;
        event->renderZeroFrames = renderSummary.zeroFrames;
        event->renderNonzeroFrames = renderSummary.nonzeroFrames;
        event->renderFirstZeroFrame = renderSummary.firstZeroFrame;
        event->renderLongestZeroRunStart = renderSummary.longestZeroRunStart;
        event->renderLongestZeroRunFrames = renderSummary.longestZeroRunFrames;
        event->copiedChecksum = copiedSummary.checksum;
        event->copiedPeakLeft = copiedSummary.peakLeft;
        event->copiedPeakRight = copiedSummary.peakRight;
        event->copiedRMSLeft = copiedSummary.rmsLeft;
        event->copiedRMSRight = copiedSummary.rmsRight;
        event->copiedZeroFrames = copiedSummary.zeroFrames;
        event->copiedNonzeroFrames = copiedSummary.nonzeroFrames;
        event->copiedFirstZeroFrame = copiedSummary.firstZeroFrame;
        event->copiedLongestZeroRunStart = copiedSummary.longestZeroRunStart;
        event->copiedLongestZeroRunFrames = copiedSummary.longestZeroRunFrames;
        if (context->previousTimestampValid && timeStamp != NULL) {
            event->sampleDelta = (int32_t)(timeStamp->mSampleTime -
                context->previousSampleTime);
            event->sampleTimeJump = event->sampleDelta != (int32_t)context->previousFrames;
            event->hostDelta = (int64_t)timeStamp->mHostTime -
                (int64_t)context->previousHostTime;
            event->hostTimeRegression = event->hostDelta < 0;
            event->callbackIntervalGap = event->hostDelta > 0 &&
                hostSeconds((uint64_t)event->hostDelta) >
                    ((double)context->previousFrames / context->format.mSampleRate) * 2.0;
        }
        context->previousTimestampValid = timeStamp != NULL;
        if (timeStamp != NULL) {
            context->previousSampleTime = timeStamp->mSampleTime;
            context->previousHostTime = timeStamp->mHostTime;
        }
        context->previousFrames = frameCount;
        context->previousFrameCountValid = true;
    } else {
        atomic_fetch_add_explicit(&context->eventDrops, 1, memory_order_relaxed);
    }
    if (block != NULL) {
        atomic_store_explicit(&block->ready, true, memory_order_release);
    }
    atomic_fetch_add_explicit(
        &context->capturedFrames, actualFrames, memory_order_relaxed);
    return noErr;
}

static uint64_t drainBlocks(
    CaptureContext* context,
    FILE* wav,
    uint64_t wavStartBase)
{
    uint64_t written = 0;
    uint64_t drain = atomic_load_explicit(&context->nextBlockToDrain, memory_order_relaxed);
    const uint64_t produced = atomic_load_explicit(&context->nextBlock, memory_order_acquire);
    while (drain < produced) {
        CaptureBlock* block = &context->blocks[drain % MAX_BLOCKS];
        if (!atomic_load_explicit(&block->ready, memory_order_acquire)) {
            break;
        }
        if (block->frameCount > 0) {
            const uint64_t wavStartFrame = wavStartBase + written;
            const size_t saved = fwrite(
                block->samples,
                sizeof(Float32) * 2U,
                block->frameCount,
                wav);
            const uint32_t savedFrames = (uint32_t)saved;
            const PayloadSummary wavSummary = summarizeInterleaved(
                block->samples, savedFrames);
            if (block->eventIndex < MAX_EVENTS) {
                CaptureEvent* event = &context->events[block->eventIndex];
                event->wavChecksum = wavSummary.checksum;
                event->wavStartFrame = wavStartFrame;
                event->wavFrames = savedFrames;
                event->wavWriteShort = savedFrames != block->frameCount;
            }
            if (savedFrames != block->frameCount) {
                atomic_fetch_add_explicit(
                    &context->shortWavWrites, 1, memory_order_relaxed);
            }
            written += savedFrames;
        }
        block->frameCount = 0;
        atomic_store_explicit(&block->ready, false, memory_order_release);
        ++drain;
    }
    atomic_store_explicit(&context->nextBlockToDrain, drain, memory_order_relaxed);
    return written;
}

static void writeEvents(const char* path, const CaptureContext* context)
{
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file,
        "{\"type\":\"receiver_lifecycle\",\"event\":\"audio_unit_start\","
        "\"pid\":%u,\"device\":%u,\"hostTime\":%llu}\n",
        context->processID,
        context->deviceID,
        (unsigned long long)context->audioUnitStartHostTime);
    uint64_t count = atomic_load_explicit(&context->nextEvent, memory_order_relaxed);
    if (count > MAX_EVENTS) {
        count = MAX_EVENTS;
    }
    for (uint64_t index = 0; index < count; ++index) {
        const CaptureEvent* event = &context->events[index];
        fprintf(file,
            "{\"type\":\"receiver_callback\",\"pid\":%u,"
            "\"sequence\":%llu,\"hostTime\":%llu,\"sampleTime\":%.3f,"
            "\"flags\":%u,\"requestedFrames\":%u,\"actualFrames\":%u,"
            "\"renderStatus\":%d,\"sampleDelta\":%d,\"hostDelta\":%lld,"
            "\"callbackSizeChanged\":%u,\"sampleTimeJump\":%u,"
            "\"hostTimeRegression\":%u,\"callbackIntervalGap\":%u,"
            "\"actionFlagsBefore\":%u,\"actionFlagsAfter\":%u,"
            "\"renderBufferCount\":%u,\"renderBuffer0Bytes\":%u,"
            "\"renderBuffer1Bytes\":%u,\"renderBuffersValid\":%u,"
            "\"renderSentinelFrames\":%u,"
            "\"renderChecksum\":\"%016llx\","
            "\"renderPeakLeft\":%.9g,\"renderPeakRight\":%.9g,"
            "\"renderRMSLeft\":%.9g,\"renderRMSRight\":%.9g,"
            "\"renderZeroFrames\":%u,\"renderNonzeroFrames\":%u,"
            "\"renderFirstZeroFrame\":%u,"
            "\"renderLongestZeroRunStart\":%u,"
            "\"renderLongestZeroRunFrames\":%u,"
            "\"renderAllZero\":%u,"
            "\"copiedChecksum\":\"%016llx\","
            "\"copiedPeakLeft\":%.9g,\"copiedPeakRight\":%.9g,"
            "\"copiedRMSLeft\":%.9g,\"copiedRMSRight\":%.9g,"
            "\"copiedZeroFrames\":%u,\"copiedNonzeroFrames\":%u,"
            "\"copiedFirstZeroFrame\":%u,"
            "\"copiedLongestZeroRunStart\":%u,"
            "\"copiedLongestZeroRunFrames\":%u,"
            "\"wavChecksum\":\"%016llx\",\"wavStartFrame\":%llu,"
            "\"wavFrames\":%u,\"wavWriteShort\":%u}\n",
            event->processID,
            (unsigned long long)event->sequence,
            (unsigned long long)event->hostTime,
            event->sampleTime,
            event->flags,
            event->requestedFrames,
            event->actualFrames,
            event->renderStatus,
            event->sampleDelta,
            (long long)event->hostDelta,
            event->callbackSizeChanged,
            event->sampleTimeJump,
            event->hostTimeRegression,
            event->callbackIntervalGap,
            event->renderActionFlagsBefore,
            event->renderActionFlagsAfter,
            event->renderBufferCount,
            event->renderBuffer0Bytes,
            event->renderBuffer1Bytes,
            event->renderBuffersValid,
            event->renderSentinelFrames,
            (unsigned long long)event->renderChecksum,
            event->renderPeakLeft,
            event->renderPeakRight,
            event->renderRMSLeft,
            event->renderRMSRight,
            event->renderZeroFrames,
            event->renderNonzeroFrames,
            event->renderFirstZeroFrame,
            event->renderLongestZeroRunStart,
            event->renderLongestZeroRunFrames,
            event->actualFrames > 0 && event->renderNonzeroFrames == 0,
            (unsigned long long)event->copiedChecksum,
            event->copiedPeakLeft,
            event->copiedPeakRight,
            event->copiedRMSLeft,
            event->copiedRMSRight,
            event->copiedZeroFrames,
            event->copiedNonzeroFrames,
            event->copiedFirstZeroFrame,
            event->copiedLongestZeroRunStart,
            event->copiedLongestZeroRunFrames,
            (unsigned long long)event->wavChecksum,
            (unsigned long long)event->wavStartFrame,
            event->wavFrames,
            event->wavWriteShort);
    }
    fprintf(file,
        "{\"type\":\"receiver_lifecycle\",\"event\":\"stop_requested\","
        "\"pid\":%u,\"device\":%u,\"hostTime\":%llu}\n",
        context->processID,
        context->deviceID,
        (unsigned long long)context->stopRequestedHostTime);
    fprintf(file,
        "{\"type\":\"receiver_lifecycle\",\"event\":\"audio_unit_stopped\","
        "\"pid\":%u,\"device\":%u,\"hostTime\":%llu}\n",
        context->processID,
        context->deviceID,
        (unsigned long long)context->audioUnitStoppedHostTime);
    fclose(file);
}

int main(int argc, char** argv)
{
    if (argc < 5 || argc > 6) {
        fprintf(stderr,
            "usage: %s <uid> <seconds> <wav> <jsonl> [44100|48000]\n",
            argv[0]);
        return 64;
    }
    const double seconds = atof(argv[2]);
    const double requestedRate = argc == 6 ? atof(argv[5]) : 0.0;
    if (seconds <= 0.0 || (requestedRate != 0.0 &&
        requestedRate != 44100.0 && requestedRate != 48000.0)) {
        return 64;
    }
    AudioDeviceID device = findDevice(argv[1]);
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "device not found: %s\n", argv[1]);
        return 2;
    }
    if (requestedRate != 0.0 && setRate(device, requestedRate) != noErr) {
        fprintf(stderr, "could not set rate\n");
        return 3;
    }

    AudioComponentDescription description = {
        kAudioUnitType_Output,
        kAudioUnitSubType_HALOutput,
        kAudioUnitManufacturer_Apple,
        0,
        0,
    };
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    CaptureContext context = {0};
    context.processID = (uint32_t)getpid();
    context.deviceID = device;
    if (component == NULL || AudioComponentInstanceNew(component, &context.audioUnit) != noErr) {
        return 4;
    }
    UInt32 enabled = 1;
    OSStatus inputEnableStatus = AudioUnitSetProperty(
        context.audioUnit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input,
        1,
        &enabled,
        sizeof(enabled));
    enabled = 0;
    OSStatus outputDisableStatus = AudioUnitSetProperty(
        context.audioUnit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output,
        0,
        &enabled,
        sizeof(enabled));
    OSStatus deviceStatus = AudioUnitSetProperty(
        context.audioUnit,
        kAudioOutputUnitProperty_CurrentDevice,
        kAudioUnitScope_Global,
        0,
        &device,
        sizeof(device));
    if (inputEnableStatus != noErr || outputDisableStatus != noErr || deviceStatus != noErr) {
        fprintf(stderr, "AUHAL setup failed input=%d output=%d device=%d\n",
            inputEnableStatus, outputDisableStatus, deviceStatus);
        AudioComponentInstanceDispose(context.audioUnit);
        return 5;
    }

    UInt32 formatSize = sizeof(context.format);
    OSStatus formatStatus = AudioUnitGetProperty(
        context.audioUnit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        1,
        &context.format,
        &formatSize);
    if (formatStatus != noErr) {
        AudioComponentInstanceDispose(context.audioUnit);
        return 6;
    }
    AudioStreamBasicDescription desired = context.format;
    desired.mFormatFlags = kAudioFormatFlagsNativeFloatPacked |
        kAudioFormatFlagIsNonInterleaved;
    desired.mBytesPerPacket = sizeof(Float32);
    desired.mFramesPerPacket = 1;
    desired.mBytesPerFrame = sizeof(Float32);
    desired.mBitsPerChannel = 32;
    OSStatus desiredStatus = AudioUnitSetProperty(
        context.audioUnit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output,
        1,
        &desired,
        sizeof(desired));
    if (desiredStatus != noErr) {
        fprintf(stderr, "AUHAL format setup failed: %d\n", desiredStatus);
        AudioComponentInstanceDispose(context.audioUnit);
        return 7;
    }
    context.format = desired;

    context.blocks = calloc(MAX_BLOCKS, sizeof(*context.blocks));
    context.events = calloc(MAX_EVENTS, sizeof(*context.events));
    if (context.blocks == NULL || context.events == NULL) {
        free(context.blocks);
        free(context.events);
        AudioComponentInstanceDispose(context.audioUnit);
        return 8;
    }
    for (uint32_t index = 0; index < MAX_BLOCKS; ++index) {
        atomic_init(&context.blocks[index].ready, false);
    }
    atomic_init(&context.nextBlock, 0);
    atomic_init(&context.nextBlockToDrain, 0);
    atomic_init(&context.nextEvent, 0);
    atomic_init(&context.blockDrops, 0);
    atomic_init(&context.eventDrops, 0);
    atomic_init(&context.callbackCount, 0);
    atomic_init(&context.capturedFrames, 0);
    atomic_init(&context.renderErrors, 0);
    atomic_init(&context.invalidRenderBuffers, 0);
    atomic_init(&context.allZeroCallbacks, 0);
    atomic_init(&context.shortWavWrites, 0);

    FILE* wav = fopen(argv[3], "wb");
    if (wav == NULL) {
        free(context.blocks);
        free(context.events);
        AudioComponentInstanceDispose(context.audioUnit);
        return 9;
    }
    writeWavHeader(wav, (uint32_t)context.format.mSampleRate, 0);

    AURenderCallbackStruct callback = {captureCallback, &context};
    OSStatus callbackStatus = AudioUnitSetProperty(
        context.audioUnit,
        kAudioOutputUnitProperty_SetInputCallback,
        kAudioUnitScope_Global,
        0,
        &callback,
        sizeof(callback));
    OSStatus initializeStatus = callbackStatus == noErr
        ? AudioUnitInitialize(context.audioUnit)
        : callbackStatus;
    OSStatus startStatus = initializeStatus == noErr
        ? AudioOutputUnitStart(context.audioUnit)
        : initializeStatus;
    fprintf(stderr,
        "pid=%u device=%u rate=%.3f channels=%u inputEnable=%d outputDisable=%d "
        "format=%d desired=%d callback=%d initialize=%d start=%d\n",
        context.processID,
        device,
        context.format.mSampleRate,
        context.format.mChannelsPerFrame,
        inputEnableStatus,
        outputDisableStatus,
        formatStatus,
        desiredStatus,
        callbackStatus,
        initializeStatus,
        startStatus);
    if (startStatus != noErr) {
        fclose(wav);
        free(context.blocks);
        free(context.events);
        AudioComponentInstanceDispose(context.audioUnit);
        return 10;
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    const uint64_t startHost = hostTimeNow();
    context.audioUnitStartHostTime = startHost;
    uint64_t writtenFrames = 0;
    while (!gInterrupted && hostSeconds(hostTimeNow() - startHost) < seconds) {
        writtenFrames += drainBlocks(&context, wav, writtenFrames);
        struct timespec delay = {0, 2000000L};
        nanosleep(&delay, NULL);
    }
    context.stopRequestedHostTime = hostTimeNow();
    AudioOutputUnitStop(context.audioUnit);
    context.audioUnitStoppedHostTime = hostTimeNow();
    AudioUnitUninitialize(context.audioUnit);
    for (uint32_t attempt = 0; attempt < 1000; ++attempt) {
        const uint64_t before = writtenFrames;
        writtenFrames += drainBlocks(&context, wav, writtenFrames);
        if (writtenFrames == before &&
            atomic_load_explicit(&context.nextBlockToDrain, memory_order_relaxed) >=
                atomic_load_explicit(&context.nextBlock, memory_order_acquire)) {
            break;
        }
    }
    writeWavHeader(wav, (uint32_t)context.format.mSampleRate, (uint32_t)writtenFrames);
    fclose(wav);
    writeEvents(argv[4], &context);
    printf(
        "device=%u rate=%.3f frames=%llu duration=%.9f callbacks=%llu "
        "blockDrops=%llu eventDrops=%llu renderErrors=%llu "
        "invalidRenderBuffers=%llu allZeroCallbacks=%llu shortWavWrites=%llu\n",
        device,
        context.format.mSampleRate,
        (unsigned long long)writtenFrames,
        (double)writtenFrames / context.format.mSampleRate,
        (unsigned long long)atomic_load_explicit(&context.callbackCount, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.blockDrops, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.eventDrops, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.renderErrors, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.invalidRenderBuffers, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.allZeroCallbacks, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context.shortWavWrites, memory_order_relaxed));
    free(context.blocks);
    free(context.events);
    AudioComponentInstanceDispose(context.audioUnit);
    return 0;
}
