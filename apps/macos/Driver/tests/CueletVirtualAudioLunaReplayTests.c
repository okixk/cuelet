#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreFoundation/CFPlugInCOM.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void* CueletVirtualAudio_Create(
    CFAllocatorRef allocator,
    CFUUIDRef requestedTypeUUID);

static unsigned gFailures = 0;
static unsigned gAssertions = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++gAssertions;                                                          \
        if (!(condition)) {                                                     \
            ++gFailures;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                       \
    } while (0)

static OSStatus hostPropertiesChanged(AudioServerPlugInHostRef host,
    AudioObjectID objectID, UInt32 count,
    const AudioObjectPropertyAddress* addresses)
{
    (void)host; (void)objectID; (void)count; (void)addresses; return noErr;
}

static OSStatus hostCopyFromStorage(AudioServerPlugInHostRef host,
    CFStringRef key, CFPropertyListRef* valueOut)
{
    (void)host; (void)key; *valueOut = NULL; return noErr;
}

static OSStatus hostWriteToStorage(AudioServerPlugInHostRef host,
    CFStringRef key, CFPropertyListRef value)
{
    (void)host; (void)key; (void)value; return noErr;
}

static OSStatus hostDeleteFromStorage(AudioServerPlugInHostRef host,
    CFStringRef key)
{
    (void)host; (void)key; return noErr;
}

static OSStatus hostRequestConfigurationChange(AudioServerPlugInHostRef host,
    AudioObjectID deviceID, UInt64 action, void* info)
{
    (void)host; (void)deviceID; (void)action; (void)info; return noErr;
}

static const AudioServerPlugInHostInterface gHost = {
    hostPropertiesChanged,
    hostCopyFromStorage,
    hostWriteToStorage,
    hostDeleteFromStorage,
    hostRequestConfigurationChange,
};

static AudioServerPlugInClientInfo client(UInt32 clientID)
{
    const AudioServerPlugInClientInfo result = {
        clientID, 2000 + (pid_t)clientID, true, NULL,
    };
    return result;
}

static void fillFixture(
    Float32* samples,
    uint64_t absoluteStart,
    uint32_t frameCount)
{
    const double sampleRate = 48000.0;
    const uint64_t outputOrigin = 98488;
    const uint64_t leadingFrames = 24000;
    const uint64_t totalFrames = 35U * 48000U;
    const double pi = acos(-1.0);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const uint64_t absoluteFrame = absoluteStart + frame;
        const uint64_t sourceFrame = absoluteFrame >= outputOrigin
            ? absoluteFrame - outputOrigin
            : UINT64_MAX;
        if (sourceFrame >= leadingFrames &&
            sourceFrame < totalFrames - leadingFrames) {
            samples[frame * 2] = (Float32)(0.25 * sin(
                2.0 * pi * 997.0 * (double)sourceFrame / sampleRate));
            samples[frame * 2 + 1] = (Float32)(0.25 * sin(
                2.0 * pi * 1499.0 * (double)sourceFrame / sampleRate));
        } else {
            samples[frame * 2] = 0.0F;
            samples[frame * 2 + 1] = 0.0F;
        }
    }
}

static bool allZero(const Float32* samples, uint32_t frameCount)
{
    for (uint32_t index = 0; index < frameCount * 2; ++index) {
        if (samples[index] != 0.0F) {
            return false;
        }
    }
    return true;
}

static uint64_t checksum(
    const Float32* samples,
    uint32_t frameCount,
    Float32* peakLeftOut,
    Float32* peakRightOut,
    Float32* rmsLeftOut,
    Float32* rmsRightOut)
{
    uint64_t hash = 1469598103934665603ULL;
    double leftSquares = 0.0;
    double rightSquares = 0.0;
    Float32 peakLeft = 0.0F;
    Float32 peakRight = 0.0F;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        uint32_t leftBits = 0;
        uint32_t rightBits = 0;
        memcpy(&leftBits, &samples[frame * 2], sizeof(leftBits));
        memcpy(&rightBits, &samples[frame * 2 + 1], sizeof(rightBits));
        hash ^= (uint64_t)leftBits | ((uint64_t)rightBits << 32);
        hash *= 1099511628211ULL;
        const Float32 left = samples[frame * 2];
        const Float32 right = samples[frame * 2 + 1];
        peakLeft = fmaxf(peakLeft, fabsf(left));
        peakRight = fmaxf(peakRight, fabsf(right));
        leftSquares += (double)left * left;
        rightSquares += (double)right * right;
    }
    *peakLeftOut = peakLeft;
    *peakRightOut = peakRight;
    *rmsLeftOut = (Float32)sqrt(leftSquares / frameCount);
    *rmsRightOut = (Float32)sqrt(rightSquares / frameCount);
    return hash;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <luna-events.csv>\n", argv[0]);
        return 64;
    }
    FILE* input = fopen(argv[1], "r");
    if (input == NULL) {
        perror("fopen");
        return 2;
    }
    AudioServerPlugInDriverRef driver = CueletVirtualAudio_Create(
        NULL, kAudioServerPlugInTypeUUID);
    CHECK(driver != NULL);
    CHECK((*driver)->Initialize(driver, &gHost) == noErr);
    const AudioServerPlugInClientInfo inputClient = client(41);
    const AudioServerPlugInClientInfo outputClient = client(42);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);

    char line[512];
    unsigned eventNumber = 0;
    while (fgets(line, sizeof(line), input) != NULL) {
        if (line[0] == '#') {
            continue;
        }
        char event[16] = {0};
        char expectation[16] = {0};
        unsigned clientID = 0;
        unsigned inputValid = 0;
        unsigned outputValid = 0;
        unsigned frames = 0;
        unsigned long long inputSample = 0;
        unsigned long long outputSample = 0;
        unsigned long long expectedSource = 0;
        const int fields = sscanf(line,
            "%15[^,],%u,%llu,%u,%llu,%u,%u,%llu,%15s",
            event,
            &clientID,
            &inputSample,
            &inputValid,
            &outputSample,
            &outputValid,
            &frames,
            &expectedSource,
            expectation);
        CHECK(fields == 9);
        if (fields != 9) {
            continue;
        }
        ++eventNumber;
        if (strcmp(event, "START") == 0) {
            CHECK((*driver)->StartIO(
                driver, kCueletObjectDevice, clientID) == noErr);
            printf("event=%u START client=%u\n", eventNumber, clientID);
            continue;
        }
        if (strcmp(event, "STOP") == 0) {
            CHECK((*driver)->StopIO(
                driver, kCueletObjectDevice, clientID) == noErr);
            printf("event=%u STOP client=%u\n", eventNumber, clientID);
            continue;
        }

        AudioServerPlugInIOCycleInfo cycle = {0};
        cycle.mNominalIOBufferFrameSize = frames;
        cycle.mIOCycleCounter = outputSample != 0
            ? outputSample / (frames == 0 ? 1 : frames)
            : inputSample / (frames == 0 ? 1 : frames);
        cycle.mInputTime.mSampleTime = (Float64)inputSample;
        cycle.mOutputTime.mSampleTime = (Float64)outputSample;
        if (inputValid != 0) {
            cycle.mInputTime.mFlags = kAudioTimeStampSampleTimeValid;
        }
        if (outputValid != 0) {
            cycle.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
        }
        Float32 samples[512 * 2];
        CHECK(frames <= 512);
        if (strcmp(event, "WRITE") == 0) {
            fillFixture(samples, outputSample, frames);
            CHECK((*driver)->DoIOOperation(
                driver,
                kCueletObjectDevice,
                kCueletObjectOutputStream,
                clientID,
                kAudioServerPlugInIOOperationWriteMix,
                frames,
                &cycle,
                samples,
                NULL) == noErr);
        } else if (strcmp(event, "READ") == 0) {
            memset(samples, 0x55, sizeof(samples));
            CHECK((*driver)->DoIOOperation(
                driver,
                kCueletObjectDevice,
                kCueletObjectInputStream,
                clientID,
                kAudioServerPlugInIOOperationReadInput,
                frames,
                &cycle,
                samples,
                NULL) == noErr);
            if (strcmp(expectation, "AUDIO") == 0) {
                Float32 expected[512 * 2];
                fillFixture(expected, expectedSource, frames);
                CHECK(memcmp(samples, expected,
                    (size_t)frames * CUELET_AUDIO_BYTES_PER_FRAME) == 0);
                CHECK(!allZero(samples, frames));
            } else if (strcmp(expectation, "ZERO") == 0) {
                CHECK(allZero(samples, frames));
            }
        } else {
            CHECK(false);
        }
        Float32 peakLeft = 0.0F;
        Float32 peakRight = 0.0F;
        Float32 rmsLeft = 0.0F;
        Float32 rmsRight = 0.0F;
        const uint64_t hash = checksum(samples, frames, &peakLeft, &peakRight,
            &rmsLeft, &rmsRight);
        printf("event=%u %s client=%u input=%llu/%u output=%llu/%u frames=%u "
               "expectedSource=%llu expectation=%s checksum=%llu "
               "peak=(%.9f,%.9f) rms=(%.9f,%.9f)\n",
            eventNumber, event, clientID, inputSample, inputValid, outputSample,
            outputValid, frames, expectedSource, expectation,
            (unsigned long long)hash, peakLeft, peakRight, rmsLeft, rmsRight);
    }
    fclose(input);

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticSnapshot snapshots[256];
    uint64_t nextSequence = 0;
    const size_t diagnosticCount = CueletDiagnosticCopy(
        snapshots,
        sizeof(snapshots) / sizeof(snapshots[0]),
        &nextSequence);
    bool sawWrite = false;
    bool sawRead = false;
    for (size_t index = 0; index < diagnosticCount; ++index) {
        const CueletDiagnosticRecordData* data = &snapshots[index].data;
        if (snapshots[index].eventKind == kCueletDiagnosticWriteMix &&
            data->outputSampleFrame == 122552 &&
            data->timelineStatus == kCueletTimelineOK &&
            data->ringWriteStatus == kCueletRingWriteOK &&
            data->ringWriteAcceptedFrames == 512 &&
            data->payloadPeakLeft > 0.24F) {
            sawWrite = true;
            printf("diagnostic WriteMix range=[122552,123064) status=%u "
                   "accepted=%u generation=%llu checksum=%llu\n",
                data->ringWriteStatus, data->ringWriteAcceptedFrames,
                (unsigned long long)data->resetGeneration,
                (unsigned long long)data->payloadChecksum);
        }
        if (snapshots[index].eventKind == kCueletDiagnosticReadInput &&
            data->sourceStartFrame == 122552 &&
            data->ringReadStatus == kCueletRingReadOK &&
            data->validFrameCount == 512 &&
            data->payloadPeakLeft > 0.24F) {
            sawRead = true;
            printf("diagnostic ReadInput source=[122552,123064) status=%u "
                   "valid=%u generation=%llu checksum=%llu\n",
                data->ringReadStatus, data->validFrameCount,
                (unsigned long long)data->resetGeneration,
                (unsigned long long)data->payloadChecksum);
        }
    }
    CHECK(sawWrite);
    CHECK(sawRead);
#endif

    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    if (gFailures != 0) {
        fprintf(stderr, "Cuelet Luna replay: %u failures in %u assertions\n",
            gFailures, gAssertions);
        return 1;
    }
    printf("Cuelet Luna replay: PASS (%u assertions)\n", gAssertions);
    return 0;
}
