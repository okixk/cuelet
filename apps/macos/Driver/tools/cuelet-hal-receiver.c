/*
 * Native Core Audio HAL receiver for Cuelet driver validation.
 *
 * This is deliberately independent of AVFoundation and Audio Unit hosting.
 * The AudioDeviceIOProc only copies into a preallocated bounded queue and
 * stores numeric event data. WAV and JSONL serialization happens on the
 * non-real-time thread after (or while) the device is running.
 *
 * Build:
 *   clang -O2 -Wall -Wextra -Werror -framework CoreAudio -framework CoreFoundation \
 *     apps/macos/Driver/tools/cuelet-hal-receiver.c -o /tmp/cuelet-hal-receiver
 *
 * Run:
 *   /tmp/cuelet-hal-receiver <uid> <seconds> <wav> <jsonl> [44100|48000]
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_BLOCKS 256U
#define MAX_BLOCK_FRAMES 8192U
#define MAX_EVENTS 250000U

typedef struct CaptureEvent {
    uint64_t sequence;
    uint64_t blockSequence;
    uint64_t nowHostTime;
    uint64_t inputHostTime;
    double nowSampleTime;
    double inputSampleTime;
    uint64_t sampleDelta;
    uint64_t hostDelta;
    uint32_t nowFlags;
    uint32_t inputFlags;
    uint32_t requestedFrames;
    uint32_t actualFrames;
    uint32_t bufferCount;
    uint32_t firstBufferBytes;
    uint32_t secondBufferBytes;
    uint32_t outputBufferCount;
    uint32_t outputDataPresent;
    uint32_t outputFirstBufferBytes;
    uint32_t callbackSizeChanged;
    uint32_t sampleTimeJump;
    uint32_t hostTimeRegression;
    uint32_t callbackIntervalGap;
    uint32_t blockDropped;
    uint32_t unsupportedFormat;
} CaptureEvent;

typedef struct CaptureBlock {
    _Atomic bool ready;
    uint64_t sequence;
    uint32_t frames;
    Float32 samples[MAX_BLOCK_FRAMES * 2U];
} CaptureBlock;

typedef struct CaptureContext {
    AudioStreamBasicDescription format;
    CaptureBlock blocks[MAX_BLOCKS];
    CaptureEvent events[MAX_EVENTS];
    _Atomic uint64_t nextBlock;
    _Atomic uint64_t nextBlockToWrite;
    _Atomic uint64_t nextEvent;
    _Atomic uint64_t blockDrops;
    _Atomic uint64_t eventDrops;
    _Atomic uint64_t callbackCount;
    _Atomic uint64_t capturedFrames;
    _Atomic bool stopRequested;
    uint64_t previousSampleTime;
    uint64_t previousHostTime;
    uint32_t previousFrames;
    bool previousSampleValid;
    bool previousHostValid;
    bool previousFrameCountValid;
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

static AudioDeviceID findDevice(const char* wantedUID)
{
    const AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, &address, 0, NULL, &size);
    if (status != noErr || size == 0) {
        return kAudioObjectUnknown;
    }
    AudioDeviceID* devices = calloc(size, 1);
    if (devices == NULL) {
        return kAudioObjectUnknown;
    }
    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &address, 0, NULL, &size, devices);
    if (status != noErr) {
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

static OSStatus getInputStream(AudioDeviceID device, AudioStreamID* streamOut)
{
    const AudioObjectPropertyAddress address = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(device, &address, 0, NULL, &size);
    if (status != noErr || size < sizeof(AudioStreamID)) {
        return status != noErr ? status : kAudioHardwareBadPropertySizeError;
    }
    AudioStreamID* streams = calloc(size, 1);
    if (streams == NULL) {
        return kAudioHardwareIllegalOperationError;
    }
    status = AudioObjectGetPropertyData(device, &address, 0, NULL, &size, streams);
    if (status == noErr) {
        *streamOut = streams[0];
    }
    free(streams);
    return status;
}

static OSStatus getFormat(AudioDeviceID device, AudioStreamBasicDescription* formatOut)
{
    AudioStreamID stream = kAudioObjectUnknown;
    OSStatus status = getInputStream(device, &stream);
    if (status != noErr) {
        return status;
    }
    const AudioObjectPropertyAddress address = {
        kAudioStreamPropertyPhysicalFormat,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = sizeof(*formatOut);
    return AudioObjectGetPropertyData(stream, &address, 0, NULL, &size, formatOut);
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

static OSStatus setStreamUsage(
    AudioDeviceID device,
    AudioDeviceIOProc proc,
    AudioObjectPropertyScope scope,
    uint32_t enabled)
{
    AudioHardwareIOProcStreamUsage usage = {
        .mIOProc = (void*)proc,
        .mNumberStreams = 1,
        .mStreamIsOn = {enabled},
    };
    const AudioObjectPropertyAddress address = {
        kAudioDevicePropertyIOProcStreamUsage,
        scope,
        kAudioObjectPropertyElementMain,
    };
    const UInt32 size = (UInt32)offsetof(
        AudioHardwareIOProcStreamUsage,
        mStreamIsOn) + sizeof(UInt32);
    return AudioObjectSetPropertyData(device, &address, 0, NULL, size, &usage);
}

static OSStatus getStreamUsage(
    AudioDeviceID device,
    AudioDeviceIOProc proc,
    AudioObjectPropertyScope scope,
    uint32_t* enabledOut)
{
    AudioHardwareIOProcStreamUsage usage = {
        .mIOProc = (void*)proc,
        .mNumberStreams = 1,
        .mStreamIsOn = {0},
    };
    const AudioObjectPropertyAddress address = {
        kAudioDevicePropertyIOProcStreamUsage,
        scope,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = (UInt32)offsetof(
        AudioHardwareIOProcStreamUsage,
        mStreamIsOn) + sizeof(UInt32);
    OSStatus status = AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &usage);
    if (status == noErr && enabledOut != NULL) {
        *enabledOut = usage.mStreamIsOn[0];
    }
    return status;
}

static bool timestampHasSampleTime(const AudioTimeStamp* timestamp)
{
    return timestamp != NULL &&
        (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0;
}

static bool timestampHasHostTime(const AudioTimeStamp* timestamp)
{
    return timestamp != NULL &&
        (timestamp->mFlags & kAudioTimeStampHostTimeValid) != 0;
}

static uint32_t inputFrameCount(
    const CaptureContext* context,
    const AudioBufferList* inputData)
{
    if (inputData == NULL || inputData->mNumberBuffers == 0) {
        return 0;
    }
    const AudioBuffer* first = &inputData->mBuffers[0];
    if (context->format.mBytesPerFrame == 0) {
        return 0;
    }
    return first->mDataByteSize / context->format.mBytesPerFrame;
}

static bool copyInput(
    const CaptureContext* context,
    const AudioBufferList* inputData,
    Float32* destination,
    uint32_t frames)
{
    if (inputData == NULL || inputData->mNumberBuffers == 0 || frames == 0) {
        return false;
    }
    if ((context->format.mFormatFlags & kAudioFormatFlagIsFloat) == 0 ||
        context->format.mBitsPerChannel != 32 ||
        context->format.mChannelsPerFrame != 2) {
        return false;
    }
    if ((context->format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0) {
        if (inputData->mNumberBuffers < 2 ||
            inputData->mBuffers[0].mData == NULL ||
            inputData->mBuffers[1].mData == NULL) {
            return false;
        }
        const Float32* left = inputData->mBuffers[0].mData;
        const Float32* right = inputData->mBuffers[1].mData;
        for (uint32_t index = 0; index < frames; ++index) {
            destination[index * 2] = left[index];
            destination[index * 2 + 1] = right[index];
        }
        return true;
    }
    if (inputData->mBuffers[0].mData == NULL ||
        inputData->mBuffers[0].mDataByteSize <
            frames * context->format.mBytesPerFrame) {
        return false;
    }
    memcpy(
        destination,
        inputData->mBuffers[0].mData,
        (size_t)frames * context->format.mBytesPerFrame);
    return true;
}

static OSStatus ioProc(
    AudioObjectID device,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* inputTime,
    AudioBufferList* outputData,
    const AudioTimeStamp* outputTime,
    void* clientData)
{
    (void)device;
    (void)outputData;
    (void)outputTime;
    CaptureContext* context = clientData;
    const uint64_t sequence = atomic_fetch_add_explicit(
        &context->callbackCount, 1, memory_order_relaxed);
    const uint32_t requestedFrames = inputFrameCount(context, inputData);
    const uint64_t eventIndex = atomic_fetch_add_explicit(
        &context->nextEvent, 1, memory_order_relaxed);
    CaptureEvent* event = eventIndex < MAX_EVENTS
        ? &context->events[eventIndex]
        : NULL;
    if (event == NULL) {
        atomic_fetch_add_explicit(&context->eventDrops, 1, memory_order_relaxed);
    }

    uint64_t blockSequence = UINT64_MAX;
    uint32_t actualFrames = 0;
    bool unsupportedFormat = false;
    const uint64_t blockIndex = atomic_load_explicit(
        &context->nextBlock, memory_order_relaxed);
    const uint64_t blockReadIndex = atomic_load_explicit(
        &context->nextBlockToWrite, memory_order_acquire);
    const uint32_t frameLimit = requestedFrames < MAX_BLOCK_FRAMES
        ? requestedFrames
        : MAX_BLOCK_FRAMES;
    if (requestedFrames > MAX_BLOCK_FRAMES) {
        unsupportedFormat = true;
    } else if (blockIndex - blockReadIndex >= MAX_BLOCKS) {
        atomic_fetch_add_explicit(&context->blockDrops, 1, memory_order_relaxed);
    } else if (frameLimit > 0) {
        CaptureBlock* block = &context->blocks[blockIndex % MAX_BLOCKS];
        if (copyInput(context, inputData, block->samples, frameLimit)) {
            block->sequence = sequence;
            block->frames = frameLimit;
            blockSequence = blockIndex;
            actualFrames = frameLimit;
            atomic_store_explicit(&block->ready, true, memory_order_release);
            atomic_store_explicit(&context->nextBlock, blockIndex + 1, memory_order_release);
            atomic_fetch_add_explicit(
                &context->capturedFrames, frameLimit, memory_order_relaxed);
        } else {
            unsupportedFormat = true;
        }
    }

    uint32_t sampleTimeJump = 0;
    uint32_t hostTimeRegression = 0;
    uint32_t callbackIntervalGap = 0;
    uint32_t callbackSizeChanged = 0;
    if (context->previousFrameCountValid && requestedFrames != context->previousFrames) {
        callbackSizeChanged = 1;
    }
    const bool sampleValid = timestampHasSampleTime(inputTime);
    const bool hostValid = timestampHasHostTime(inputTime);
    uint64_t sampleDelta = 0;
    uint64_t hostDelta = 0;
    if (sampleValid && context->previousSampleValid) {
        const double current = inputTime->mSampleTime;
        const double previous = (double)context->previousSampleTime;
        if (current >= previous) {
            sampleDelta = (uint64_t)(current - previous);
            if (sampleDelta != requestedFrames) {
                sampleTimeJump = 1;
            }
        } else {
            sampleTimeJump = 1;
        }
    }
    if (hostValid && context->previousHostValid) {
        if (inputTime->mHostTime >= context->previousHostTime) {
            hostDelta = inputTime->mHostTime - context->previousHostTime;
            const uint64_t expected = (uint64_t)(
                ((double)requestedFrames * 1000000000.0) /
                context->format.mSampleRate);
            if (hostDelta > expected * 2U && expected > 0) {
                callbackIntervalGap = 1;
            }
        } else {
            hostTimeRegression = 1;
        }
    }
    if (event != NULL) {
        event->sequence = sequence;
        event->blockSequence = blockSequence;
        event->nowHostTime = now != NULL ? now->mHostTime : 0;
        event->inputHostTime = inputTime != NULL ? inputTime->mHostTime : 0;
        event->nowSampleTime = now != NULL ? now->mSampleTime : 0.0;
        event->inputSampleTime = inputTime != NULL ? inputTime->mSampleTime : 0.0;
        event->sampleDelta = sampleDelta;
        event->hostDelta = hostDelta;
        event->nowFlags = now != NULL ? now->mFlags : 0;
        event->inputFlags = inputTime != NULL ? inputTime->mFlags : 0;
        event->requestedFrames = requestedFrames;
        event->actualFrames = actualFrames;
        event->bufferCount = inputData != NULL ? inputData->mNumberBuffers : 0;
        event->firstBufferBytes = inputData != NULL && inputData->mNumberBuffers > 0
            ? inputData->mBuffers[0].mDataByteSize : 0;
        event->secondBufferBytes = inputData != NULL && inputData->mNumberBuffers > 1
            ? inputData->mBuffers[1].mDataByteSize : 0;
        event->outputBufferCount = outputData != NULL ? outputData->mNumberBuffers : 0;
        event->outputDataPresent = outputData != NULL &&
            outputData->mNumberBuffers > 0 &&
            outputData->mBuffers[0].mData != NULL;
        event->outputFirstBufferBytes = outputData != NULL &&
            outputData->mNumberBuffers > 0
            ? outputData->mBuffers[0].mDataByteSize : 0;
        event->callbackSizeChanged = callbackSizeChanged;
        event->sampleTimeJump = sampleTimeJump;
        event->hostTimeRegression = hostTimeRegression;
        event->callbackIntervalGap = callbackIntervalGap;
        event->blockDropped = blockSequence == UINT64_MAX && requestedFrames > 0;
        event->unsupportedFormat = unsupportedFormat;
    }
    if (sampleValid) {
        context->previousSampleTime = (uint64_t)inputTime->mSampleTime;
    }
    if (hostValid) {
        context->previousHostTime = inputTime->mHostTime;
    }
    context->previousFrames = requestedFrames;
    context->previousSampleValid = sampleValid;
    context->previousHostValid = hostValid;
    context->previousFrameCountValid = true;
    return noErr;
}

static void writeLE16(FILE* file, uint16_t value)
{
    fwrite(&value, sizeof(value), 1, file);
}

static void writeLE32(FILE* file, uint32_t value)
{
    fwrite(&value, sizeof(value), 1, file);
}

static void writeWavHeader(FILE* file, uint32_t sampleRate, uint32_t dataBytes)
{
    fseek(file, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, file);
    writeLE32(file, 36U + dataBytes);
    fwrite("WAVEfmt ", 1, 8, file);
    writeLE32(file, 16);
    writeLE16(file, 3);
    writeLE16(file, 2);
    writeLE32(file, sampleRate);
    writeLE32(file, sampleRate * 8U);
    writeLE16(file, 8);
    writeLE16(file, 32);
    fwrite("data", 1, 4, file);
    writeLE32(file, dataBytes);
}

static uint64_t drainBlocks(CaptureContext* context, FILE* wav)
{
    uint64_t frames = 0;
    for (;;) {
        const uint64_t readIndex = atomic_load_explicit(
            &context->nextBlockToWrite, memory_order_relaxed);
        const uint64_t writeIndex = atomic_load_explicit(
            &context->nextBlock, memory_order_acquire);
        if (readIndex >= writeIndex) {
            break;
        }
        CaptureBlock* block = &context->blocks[readIndex % MAX_BLOCKS];
        if (!atomic_load_explicit(&block->ready, memory_order_acquire)) {
            break;
        }
        fwrite(block->samples, sizeof(Float32) * 2U, block->frames, wav);
        frames += block->frames;
        atomic_store_explicit(&block->ready, false, memory_order_release);
        atomic_store_explicit(
            &context->nextBlockToWrite, readIndex + 1, memory_order_release);
    }
    return frames;
}

static void writeEvents(const char* path, const CaptureContext* context)
{
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    const uint64_t count = atomic_load_explicit(
        &context->nextEvent, memory_order_relaxed);
    const uint64_t limited = count < MAX_EVENTS ? count : MAX_EVENTS;
    for (uint64_t index = 0; index < limited; ++index) {
        const CaptureEvent* event = &context->events[index];
        fprintf(file,
            "{\"sequence\":%llu,\"blockSequence\":%llu,"
            "\"nowHostTime\":%llu,\"inputHostTime\":%llu,"
            "\"nowSampleTime\":%.3f,\"inputSampleTime\":%.3f,"
            "\"sampleDelta\":%llu,\"hostDelta\":%llu,"
            "\"nowFlags\":%u,\"inputFlags\":%u,"
            "\"requestedFrames\":%u,\"actualFrames\":%u,"
            "\"bufferCount\":%u,\"firstBufferBytes\":%u,"
            "\"secondBufferBytes\":%u,\"outputBufferCount\":%u,"
            "\"outputDataPresent\":%u,\"outputFirstBufferBytes\":%u,"
            "\"callbackSizeChanged\":%u,"
            "\"sampleTimeJump\":%u,\"hostTimeRegression\":%u,"
            "\"callbackIntervalGap\":%u,\"blockDropped\":%u,"
            "\"unsupportedFormat\":%u}\n",
            (unsigned long long)event->sequence,
            (unsigned long long)event->blockSequence,
            (unsigned long long)event->nowHostTime,
            (unsigned long long)event->inputHostTime,
            event->nowSampleTime,
            event->inputSampleTime,
            (unsigned long long)event->sampleDelta,
            (unsigned long long)event->hostDelta,
            event->nowFlags,
            event->inputFlags,
            event->requestedFrames,
            event->actualFrames,
            event->bufferCount,
            event->firstBufferBytes,
            event->secondBufferBytes,
            event->outputBufferCount,
            event->outputDataPresent,
            event->outputFirstBufferBytes,
            event->callbackSizeChanged,
            event->sampleTimeJump,
            event->hostTimeRegression,
            event->callbackIntervalGap,
            event->blockDropped,
            event->unsupportedFormat);
    }
    fclose(file);
}

static void writeSummary(
    const char* path,
    const CaptureContext* context,
    uint64_t writtenFrames,
    double elapsedSeconds,
    AudioDeviceID device)
{
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    const uint64_t eventCount = atomic_load_explicit(
        &context->nextEvent, memory_order_relaxed);
    fprintf(file,
        "{\"device\":%u,\"sampleRate\":%.3f,\"channels\":%u,"
        "\"bytesPerFrame\":%u,\"formatFlags\":%u,"
        "\"requestedSeconds\":%.6f,\"elapsedSeconds\":%.6f,"
        "\"callbackCount\":%llu,\"eventCount\":%llu,"
        "\"capturedFrames\":%llu,\"writtenFrames\":%llu,"
        "\"capturedDurationSeconds\":%.9f,\"writtenDurationSeconds\":%.9f,"
        "\"blockDrops\":%llu,\"eventDrops\":%llu}\n",
        device,
        context->format.mSampleRate,
        context->format.mChannelsPerFrame,
        context->format.mBytesPerFrame,
        context->format.mFormatFlags,
        elapsedSeconds,
        elapsedSeconds,
        (unsigned long long)atomic_load_explicit(&context->callbackCount, memory_order_relaxed),
        (unsigned long long)eventCount,
        (unsigned long long)atomic_load_explicit(&context->capturedFrames, memory_order_relaxed),
        (unsigned long long)writtenFrames,
        (double)atomic_load_explicit(&context->capturedFrames, memory_order_relaxed) /
            context->format.mSampleRate,
        (double)writtenFrames / context->format.mSampleRate,
        (unsigned long long)atomic_load_explicit(&context->blockDrops, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context->eventDrops, memory_order_relaxed));
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
    const char* uid = argv[1];
    const double seconds = atof(argv[2]);
    const char* wavPath = argv[3];
    const char* jsonPath = argv[4];
    const double requestedRate = argc == 6 ? atof(argv[5]) : 0.0;
    if (seconds <= 0.0 || (requestedRate != 0.0 &&
        requestedRate != 44100.0 && requestedRate != 48000.0)) {
        fprintf(stderr, "invalid duration or sample rate\n");
        return 64;
    }

    AudioDeviceID device = findDevice(uid);
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "device not found: %s\n", uid);
        return 2;
    }
    if (requestedRate != 0.0) {
        OSStatus status = setRate(device, requestedRate);
        if (status != noErr) {
            fprintf(stderr, "could not set rate %.1f: %d\n", requestedRate, status);
            return 3;
        }
        for (int attempt = 0; attempt < 100; ++attempt) {
            AudioStreamBasicDescription current = {0};
            if (getFormat(device, &current) == noErr &&
                current.mSampleRate == requestedRate) {
                break;
            }
            struct timespec delay = {0, 10000000L};
            nanosleep(&delay, NULL);
        }
    }

    CaptureContext* context = calloc(1, sizeof(*context));
    if (context == NULL || getFormat(device, &context->format) != noErr) {
        fprintf(stderr, "could not query input format\n");
        free(context);
        return 4;
    }
    if (context->format.mSampleRate <= 0.0 ||
        context->format.mBytesPerFrame == 0) {
        fprintf(stderr, "invalid input format\n");
        free(context);
        return 5;
    }
    for (uint32_t index = 0; index < MAX_BLOCKS; ++index) {
        atomic_init(&context->blocks[index].ready, false);
    }
    atomic_init(&context->nextBlock, 0);
    atomic_init(&context->nextBlockToWrite, 0);
    atomic_init(&context->nextEvent, 0);
    atomic_init(&context->blockDrops, 0);
    atomic_init(&context->eventDrops, 0);
    atomic_init(&context->callbackCount, 0);
    atomic_init(&context->capturedFrames, 0);
    atomic_init(&context->stopRequested, false);

    FILE* wav = fopen(wavPath, "wb");
    if (wav == NULL) {
        fprintf(stderr, "could not open WAV: %s\n", wavPath);
        free(context);
        return 6;
    }
    writeWavHeader(wav, (uint32_t)context->format.mSampleRate, 0);

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    AudioDeviceIOProcID proc = NULL;
    OSStatus status = AudioDeviceCreateIOProcID(device, ioProc, context, &proc);
    if (status != noErr) {
        fprintf(stderr, "AudioDeviceCreateIOProcID failed: %d\n", status);
        fclose(wav);
        free(context);
        return 7;
    }
    status = setStreamUsage(
        device,
        ioProc,
        kAudioObjectPropertyScopeInput,
        1);
    if (status != noErr) {
        fprintf(stderr, "could not enable input-only stream usage: %d\n", status);
        AudioDeviceDestroyIOProcID(device, proc);
        fclose(wav);
        free(context);
        return 7;
    }
    status = setStreamUsage(
        device,
        ioProc,
        kAudioObjectPropertyScopeOutput,
        0);
    if (status != noErr) {
        fprintf(stderr, "could not disable output stream usage: %d\n", status);
        AudioDeviceDestroyIOProcID(device, proc);
        fclose(wav);
        free(context);
        return 7;
    }
    uint32_t inputUsage = 0;
    uint32_t outputUsage = 0;
    OSStatus inputUsageStatus = getStreamUsage(
        device, ioProc, kAudioObjectPropertyScopeInput, &inputUsage);
    OSStatus outputUsageStatus = getStreamUsage(
        device, ioProc, kAudioObjectPropertyScopeOutput, &outputUsage);
    fprintf(
        stderr,
        "stream usage input status=%d value=%u output status=%d value=%u\n",
        inputUsageStatus,
        inputUsage,
        outputUsageStatus,
        outputUsage);
    status = AudioDeviceStart(device, proc);
    if (status != noErr) {
        fprintf(stderr, "AudioDeviceStart failed: %d\n", status);
        AudioDeviceDestroyIOProcID(device, proc);
        fclose(wav);
        free(context);
        return 8;
    }

    const uint64_t startHost = hostTimeNow();
    uint64_t writtenFrames = 0;
    while (!gInterrupted && hostSeconds(hostTimeNow() - startHost) < seconds) {
        writtenFrames += drainBlocks(context, wav);
        struct timespec delay = {0, 5000000L};
        nanosleep(&delay, NULL);
    }
    AudioDeviceStop(device, proc);
    AudioDeviceDestroyIOProcID(device, proc);
    for (int attempt = 0; attempt < 100; ++attempt) {
        const uint64_t before = writtenFrames;
        writtenFrames += drainBlocks(context, wav);
        if (writtenFrames == before &&
            atomic_load_explicit(&context->nextBlockToWrite, memory_order_relaxed) >=
                atomic_load_explicit(&context->nextBlock, memory_order_relaxed)) {
            break;
        }
        struct timespec delay = {0, 1000000L};
        nanosleep(&delay, NULL);
    }
    const uint64_t dataBytes64 = writtenFrames * 8U;
    const uint32_t dataBytes = dataBytes64 > UINT32_MAX
        ? UINT32_MAX
        : (uint32_t)dataBytes64;
    writeWavHeader(wav, (uint32_t)context->format.mSampleRate, dataBytes);
    fclose(wav);
    writeEvents(jsonPath, context);
    char summaryPath[4096];
    snprintf(summaryPath, sizeof(summaryPath), "%s.summary.json", jsonPath);
    writeSummary(summaryPath, context, writtenFrames,
        hostSeconds(hostTimeNow() - startHost), device);
    fprintf(stdout,
        "device=%u rate=%.3f callbacks=%llu capturedFrames=%llu writtenFrames=%llu "
        "duration=%.6f blockDrops=%llu eventDrops=%llu\n",
        device,
        context->format.mSampleRate,
        (unsigned long long)atomic_load_explicit(&context->callbackCount, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context->capturedFrames, memory_order_relaxed),
        (unsigned long long)writtenFrames,
        (double)writtenFrames / context->format.mSampleRate,
        (unsigned long long)atomic_load_explicit(&context->blockDrops, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&context->eventDrops, memory_order_relaxed));
    free(context);
    return 0;
}
