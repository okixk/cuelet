/*
 * Native Core Audio HAL output fixture injector for Cuelet validation.
 *
 * The IOProc only copies precomputed Float32 fixture samples into the output
 * buffer. It performs no filesystem I/O, allocation, logging, or blocking.
 *
 * Build:
 *   clang -O2 -Wall -Wextra -Werror -framework CoreAudio -framework CoreFoundation \
 *     apps/macos/Driver/tools/cuelet-hal-injector.c -o /tmp/cuelet-hal-injector
 *
 * Run:
 *   /tmp/cuelet-hal-injector <uid> <seconds> [44100|48000]
 */

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_OUTPUT_EVENTS 200000U

typedef struct InjectorEvent {
    uint64_t sequence;
    uint64_t hostTime;
    double sampleTime;
    uint32_t flags;
    uint32_t frameCount;
} InjectorEvent;

typedef struct InjectorContext {
    AudioStreamBasicDescription format;
    Float32* fixture;
    uint64_t fixtureFrames;
    uint64_t outputFramePosition;
    uint64_t callbackCount;
    uint64_t callbackFrameCount;
    uint64_t unsupportedBufferCount;
    InjectorEvent* events;
    uint64_t eventCount;
    uint64_t eventDrops;
} InjectorContext;

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
    if (AudioObjectGetPropertyDataSize(
            kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr) {
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

static OSStatus getOutputStream(
    AudioDeviceID device,
    AudioStreamID* streamOut)
{
    const AudioObjectPropertyAddress address = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeOutput,
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

static OSStatus getFormat(
    AudioDeviceID device,
    AudioStreamBasicDescription* formatOut)
{
    AudioStreamID stream = kAudioObjectUnknown;
    OSStatus status = getOutputStream(device, &stream);
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

static void copyOutput(
    InjectorContext* context,
    AudioBufferList* outputData,
    uint32_t frames)
{
    if (outputData == NULL || outputData->mNumberBuffers == 0 || frames == 0) {
        return;
    }
    if (context->format.mChannelsPerFrame != 2 ||
        context->format.mBitsPerChannel != 32 ||
        (context->format.mFormatFlags & kAudioFormatFlagIsFloat) == 0) {
        ++context->unsupportedBufferCount;
        return;
    }
    const bool nonInterleaved =
        (context->format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    const uint64_t sourceStart = context->outputFramePosition;
    if (nonInterleaved) {
        if (outputData->mNumberBuffers < 2 ||
            outputData->mBuffers[0].mData == NULL ||
            outputData->mBuffers[1].mData == NULL) {
            ++context->unsupportedBufferCount;
            return;
        }
        Float32* left = outputData->mBuffers[0].mData;
        Float32* right = outputData->mBuffers[1].mData;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const uint64_t sourceFrame = sourceStart + frame;
            const Float32* source = sourceFrame < context->fixtureFrames
                ? &context->fixture[sourceFrame * 2]
                : NULL;
            left[frame] = source == NULL ? 0.0F : source[0];
            right[frame] = source == NULL ? 0.0F : source[1];
        }
    } else {
        if (outputData->mBuffers[0].mData == NULL ||
            outputData->mBuffers[0].mDataByteSize < frames * 8U) {
            ++context->unsupportedBufferCount;
            return;
        }
        Float32* destination = outputData->mBuffers[0].mData;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const uint64_t sourceFrame = sourceStart + frame;
            const Float32* source = sourceFrame < context->fixtureFrames
                ? &context->fixture[sourceFrame * 2]
                : NULL;
            destination[frame * 2] = source == NULL ? 0.0F : source[0];
            destination[frame * 2 + 1] = source == NULL ? 0.0F : source[1];
        }
    }
    context->outputFramePosition += frames;
    context->callbackFrameCount += frames;
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
    (void)now;
    (void)inputData;
    (void)inputTime;
    InjectorContext* context = clientData;
    if (context->eventCount < MAX_OUTPUT_EVENTS) {
        InjectorEvent* event = &context->events[context->eventCount];
        event->sequence = context->eventCount;
        event->hostTime = outputTime != NULL ? outputTime->mHostTime : 0;
        event->sampleTime = outputTime != NULL ? outputTime->mSampleTime : 0.0;
        event->flags = outputTime != NULL ? outputTime->mFlags : 0;
        event->frameCount = outputData != NULL && outputData->mNumberBuffers > 0
            ? outputData->mBuffers[0].mDataByteSize / context->format.mBytesPerFrame
            : 0;
        ++context->eventCount;
    } else {
        ++context->eventDrops;
    }
    ++context->callbackCount;
    const uint32_t frames = outputData != NULL && outputData->mNumberBuffers > 0
        ? outputData->mBuffers[0].mDataByteSize / context->format.mBytesPerFrame
        : 0;
    copyOutput(context, outputData, frames);
    return noErr;
}

static void makeFixture(
    InjectorContext* context,
    double duration,
    double sampleRate)
{
    context->fixtureFrames = (uint64_t)(duration * sampleRate);
    context->fixture = calloc(
        (size_t)context->fixtureFrames * 2U,
        sizeof(Float32));
    const double pi = acos(-1.0);
    const uint64_t leadingFrames = (uint64_t)(0.5 * sampleRate);
    const uint64_t trailingFrames = (uint64_t)(0.5 * sampleRate);
    const uint64_t activeEnd = context->fixtureFrames > trailingFrames
        ? context->fixtureFrames - trailingFrames
        : 0;
    for (uint64_t frame = leadingFrames; frame < activeEnd; ++frame) {
        context->fixture[frame * 2] = (Float32)(0.25 *
            sin(2.0 * pi * 997.0 * (double)frame / sampleRate));
        context->fixture[frame * 2 + 1] = (Float32)(0.25 *
            sin(2.0 * pi * 1499.0 * (double)frame / sampleRate));
    }
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s <uid> <seconds> [44100|48000]\n", argv[0]);
        return 64;
    }
    const double duration = atof(argv[2]);
    const double requestedRate = argc == 4 ? atof(argv[3]) : 0.0;
    if (duration <= 0.0 || (requestedRate != 0.0 &&
        requestedRate != 44100.0 && requestedRate != 48000.0)) {
        return 64;
    }
    AudioDeviceID device = findDevice(argv[1]);
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "device not found\n");
        return 2;
    }
    if (requestedRate != 0.0 && setRate(device, requestedRate) != noErr) {
        fprintf(stderr, "could not set sample rate\n");
        return 3;
    }
    InjectorContext context = {0};
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (getFormat(device, &context.format) == noErr &&
            (requestedRate == 0.0 || context.format.mSampleRate == requestedRate)) {
            break;
        }
        struct timespec delay = {0, 10000000L};
        nanosleep(&delay, NULL);
    }
    if (context.format.mSampleRate <= 0.0 || context.format.mBytesPerFrame == 0) {
        fprintf(stderr, "could not query output format\n");
        return 4;
    }
    makeFixture(&context, duration, context.format.mSampleRate);
    if (context.fixture == NULL) {
        return 5;
    }
    context.events = calloc(MAX_OUTPUT_EVENTS, sizeof(*context.events));
    if (context.events == NULL) {
        free(context.fixture);
        return 5;
    }
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    AudioDeviceIOProcID proc = NULL;
    OSStatus status = AudioDeviceCreateIOProcID(device, ioProc, &context, &proc);
    if (status != noErr) {
        free(context.events);
        free(context.fixture);
        return 6;
    }
    status = AudioDeviceStart(device, proc);
    if (status != noErr) {
        AudioDeviceDestroyIOProcID(device, proc);
        free(context.events);
        free(context.fixture);
        return 7;
    }
    const uint64_t startHost = hostTimeNow();
    while (!gInterrupted && hostSeconds(hostTimeNow() - startHost) < duration) {
        struct timespec delay = {0, 5000000L};
        nanosleep(&delay, NULL);
    }
    AudioDeviceStop(device, proc);
    AudioDeviceDestroyIOProcID(device, proc);
    printf(
        "device=%u rate=%.3f callbacks=%llu outputFrames=%llu duration=%.9f "
        "unsupportedBuffers=%llu\n",
        device,
        context.format.mSampleRate,
        (unsigned long long)context.callbackCount,
        (unsigned long long)context.callbackFrameCount,
        (double)context.callbackFrameCount / context.format.mSampleRate,
        (unsigned long long)context.unsupportedBufferCount);
    for (uint64_t index = 0; index < context.eventCount; ++index) {
        const InjectorEvent* event = &context.events[index];
        printf(
            "{\"sequence\":%llu,\"hostTime\":%llu,\"sampleTime\":%.3f,"
            "\"flags\":%u,\"frameCount\":%u}\n",
            (unsigned long long)event->sequence,
            (unsigned long long)event->hostTime,
            event->sampleTime,
            event->flags,
            event->frameCount);
    }
    if (context.eventDrops != 0) {
        fprintf(stderr, "eventDrops=%llu\n", (unsigned long long)context.eventDrops);
    }
    free(context.events);
    free(context.fixture);
    return 0;
}
