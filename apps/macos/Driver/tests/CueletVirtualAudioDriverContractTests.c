#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnosticClient.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreFoundation/CFPlugInCOM.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

extern void* CueletVirtualAudio_Create(
    CFAllocatorRef allocator,
    CFUUIDRef requestedTypeUUID);

static unsigned gFailures = 0;
static unsigned gAssertions = 0;
static uint64_t gTestNextOutputStart = 1000;
static uint64_t gTestInitialOutputStart = 1000;
static uint64_t gTestReadStart[64];
static bool gTestReadInitialized[64];
static bool gTestHaveOutput = false;

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++gAssertions;                                                          \
        if (!(condition)) {                                                     \
            ++gFailures;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                       \
    } while (0)

static OSStatus hostPropertiesChanged(
    AudioServerPlugInHostRef host,
    AudioObjectID objectID,
    UInt32 addressCount,
    const AudioObjectPropertyAddress* addresses)
{
    (void)host;
    (void)objectID;
    (void)addressCount;
    (void)addresses;
    return noErr;
}

static OSStatus hostCopyFromStorage(
    AudioServerPlugInHostRef host,
    CFStringRef key,
    CFPropertyListRef* dataOut)
{
    (void)host;
    (void)key;
    *dataOut = NULL;
    return noErr;
}

static OSStatus hostWriteToStorage(
    AudioServerPlugInHostRef host,
    CFStringRef key,
    CFPropertyListRef data)
{
    (void)host;
    (void)key;
    (void)data;
    return noErr;
}

static OSStatus hostDeleteFromStorage(
    AudioServerPlugInHostRef host,
    CFStringRef key)
{
    (void)host;
    (void)key;
    return noErr;
}

static OSStatus hostRequestConfigurationChange(
    AudioServerPlugInHostRef host,
    AudioObjectID deviceObjectID,
    UInt64 changeAction,
    void* changeInfo)
{
    (void)host;
    (void)deviceObjectID;
    (void)changeAction;
    (void)changeInfo;
    return noErr;
}

static const AudioServerPlugInHostInterface gTestHost = {
    hostPropertiesChanged,
    hostCopyFromStorage,
    hostWriteToStorage,
    hostDeleteFromStorage,
    hostRequestConfigurationChange,
};

static AudioObjectPropertyAddress address(
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope)
{
    const AudioObjectPropertyAddress result = {
        selector,
        scope,
        kAudioObjectPropertyElementMain,
    };
    return result;
}

static void testIdentityAndProperties(AudioServerPlugInDriverRef driver)
{
    AudioObjectPropertyAddress property = address(
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal);
    UInt32 size = sizeof(CFStringRef);
    UInt32 used = 0;
    CFStringRef stringValue = NULL;
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        size,
        &used,
        &stringValue) == noErr);
    CHECK(used == sizeof(CFStringRef));
    CHECK(CFEqual(stringValue, CFSTR(CUELET_DRIVER_NAME)));

    property = address(
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal);
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        size,
        &used,
        &stringValue) == noErr);
    CHECK(CFEqual(stringValue, CFSTR(CUELET_DRIVER_DEVICE_UID)));

    property = address(
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeInput);
    AudioObjectID streams[2] = {0, 0};
    size = sizeof(streams);
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        size,
        &used,
        streams) == noErr);
    CHECK(used == sizeof(AudioObjectID));
    CHECK(streams[0] == kCueletObjectInputStream);

    property.mScope = kAudioObjectPropertyScopeOutput;
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        size,
        &used,
        streams) == noErr);
    CHECK(used == sizeof(AudioObjectID));
    CHECK(streams[0] == kCueletObjectOutputStream);

    property = address(
        kAudioStreamPropertyAvailablePhysicalFormats,
        kAudioObjectPropertyScopeGlobal);
    AudioStreamRangedDescription formats[2] = {0};
    size = sizeof(formats);
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectInputStream,
        0,
        &property,
        0,
        NULL,
        size,
        &used,
        formats) == noErr);
    CHECK(used == sizeof(formats));
    CHECK(formats[0].mFormat.mSampleRate == 44100.0);
    CHECK(formats[1].mFormat.mSampleRate == 48000.0);
    CHECK(formats[0].mFormat.mChannelsPerFrame == 2);

    property = address('nope', kAudioObjectPropertyScopeGlobal);
    CHECK(!(*driver)->HasProperty(
        driver,
        kCueletObjectDevice,
        0,
        &property));
    CHECK((*driver)->GetPropertyDataSize(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        &size) == kAudioHardwareUnknownPropertyError);

    property = address(
        kAudioStreamPropertyPhysicalFormat,
        kAudioObjectPropertyScopeGlobal);
    AudioStreamBasicDescription mono = CueletMakeStreamFormat(48000.0);
    mono.mChannelsPerFrame = 1;
    mono.mBytesPerFrame = 4;
    mono.mBytesPerPacket = 4;
    CHECK((*driver)->SetPropertyData(
        driver,
        kCueletObjectInputStream,
        0,
        &property,
        0,
        NULL,
        sizeof(mono),
        &mono) == kAudioDeviceUnsupportedFormatError);
}

static AudioServerPlugInClientInfo client(uint32_t clientID)
{
    const AudioServerPlugInClientInfo result = {
        clientID,
        1000 + (pid_t)clientID,
        true,
        NULL,
    };
    return result;
}

static OSStatus doIO(
    AudioServerPlugInDriverRef driver,
    uint32_t clientID,
    AudioObjectID streamID,
    uint32_t operation,
    Float32* samples,
    uint32_t frameCount)
{
    AudioServerPlugInIOCycleInfo cycle = {0};
    const int64_t outputAheadFrames = 200;
    const uint32_t loopbackDelay = frameCount;
    uint64_t timelineStart = 0;
    if (operation == kAudioServerPlugInIOOperationWriteMix) {
        timelineStart = gTestNextOutputStart;
        if (!gTestHaveOutput) {
            gTestInitialOutputStart = timelineStart;
            gTestHaveOutput = true;
        }
        gTestNextOutputStart += frameCount;
        cycle.mOutputTime.mSampleTime = (Float64)timelineStart;
        cycle.mInputTime.mSampleTime =
            (Float64)((int64_t)timelineStart - outputAheadFrames);
    } else {
        if (clientID >= 64) {
            return kAudioHardwareIllegalOperationError;
        }
        if (!gTestReadInitialized[clientID]) {
            gTestReadStart[clientID] = gTestInitialOutputStart;
            gTestReadInitialized[clientID] = true;
        }
        timelineStart = gTestReadStart[clientID];
        cycle.mInputTime.mSampleTime = (Float64)(
            (int64_t)timelineStart - outputAheadFrames + loopbackDelay);
        cycle.mOutputTime.mSampleTime = cycle.mInputTime.mSampleTime +
            outputAheadFrames;
        gTestReadStart[clientID] += frameCount;
    }
    cycle.mCurrentTime.mSampleTime = cycle.mInputTime.mSampleTime;
    cycle.mIOCycleCounter = timelineStart / frameCount + 1;
    cycle.mNominalIOBufferFrameSize = frameCount;
    cycle.mInputTime.mFlags = kAudioTimeStampSampleTimeValid;
    cycle.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
    cycle.mCurrentTime.mFlags = kAudioTimeStampSampleTimeValid;
    return (*driver)->DoIOOperation(
        driver,
        kCueletObjectDevice,
        streamID,
        clientID,
        operation,
        frameCount,
        &cycle,
        samples,
        NULL);
}

static void resetTestTimeline(void)
{
    gTestNextOutputStart = 1000;
    gTestInitialOutputStart = 1000;
    memset(gTestReadStart, 0, sizeof(gTestReadStart));
    memset(gTestReadInitialized, 0, sizeof(gTestReadInitialized));
    gTestHaveOutput = false;
}

static OSStatus doIOWithCycle(
    AudioServerPlugInDriverRef driver,
    uint32_t clientID,
    AudioObjectID streamID,
    uint32_t operation,
    Float32* samples,
    uint32_t frameCount,
    const AudioServerPlugInIOCycleInfo* cycle)
{
    return (*driver)->DoIOOperation(
        driver,
        kCueletObjectDevice,
        streamID,
        clientID,
        operation,
        frameCount,
        cycle,
        samples,
        NULL);
}

static AudioServerPlugInIOCycleInfo lunaWriteCycle(uint64_t outputStart)
{
    AudioServerPlugInIOCycleInfo cycle = {0};
    cycle.mIOCycleCounter = outputStart / 512U;
    cycle.mNominalIOBufferFrameSize = 512;
    cycle.mOutputTime.mSampleTime = (Float64)outputStart;
    cycle.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
    return cycle;
}

static AudioServerPlugInIOCycleInfo lunaReadCycle(uint64_t inputStart)
{
    AudioServerPlugInIOCycleInfo cycle = {0};
    cycle.mIOCycleCounter = inputStart / 512U;
    cycle.mNominalIOBufferFrameSize = 512;
    cycle.mInputTime.mSampleTime = (Float64)inputStart;
    cycle.mInputTime.mFlags = kAudioTimeStampSampleTimeValid;
    cycle.mOutputTime.mSampleTime = (Float64)(inputStart + 184U);
    cycle.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
    return cycle;
}

static AudioServerPlugInIOCycleInfo lunaInputOnlyReadCycle(
    uint64_t inputStart,
    uint64_t cycleCounter,
    uint32_t nominalFrameSize)
{
    AudioServerPlugInIOCycleInfo cycle = {0};
    cycle.mIOCycleCounter = cycleCounter;
    cycle.mNominalIOBufferFrameSize = nominalFrameSize;
    cycle.mInputTime.mSampleTime = (Float64)inputStart;
    cycle.mInputTime.mFlags = kAudioTimeStampSampleTimeValid;
    return cycle;
}

static AudioServerPlugInIOCycleInfo lunaOutputOnlyWriteCycle(
    uint64_t outputStart,
    uint64_t cycleCounter,
    uint32_t nominalFrameSize)
{
    AudioServerPlugInIOCycleInfo cycle = {0};
    cycle.mIOCycleCounter = cycleCounter;
    cycle.mNominalIOBufferFrameSize = nominalFrameSize;
    cycle.mOutputTime.mSampleTime = (Float64)outputStart;
    cycle.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
    return cycle;
}

static void fillWaveformAtRate(
    Float32* samples,
    uint64_t absoluteStart,
    uint32_t frameCount,
    double sampleRate)
{
    const uint64_t outputOrigin = 98488;
    const double pi = acos(-1.0);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const double sourceFrame = (double)(absoluteStart + frame - outputOrigin);
        samples[frame * 2] = (Float32)(0.25 *
            sin(2.0 * pi * 997.0 * sourceFrame / sampleRate));
        samples[frame * 2 + 1] = (Float32)(0.25 *
            sin(2.0 * pi * 1499.0 * sourceFrame / sampleRate));
    }
}

static void fillLunaWaveform(
    Float32* samples,
    uint64_t absoluteStart,
    uint32_t frameCount)
{
    fillWaveformAtRate(samples, absoluteStart, frameCount, 48000.0);
}

static void testCrossOperationCalibrationAtRate(
    AudioServerPlugInDriverRef driver,
    Float64 sampleRate,
    uint32_t inputClientID,
    uint32_t outputClientID)
{
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver,
        kCueletObjectDevice,
        (UInt64)sampleRate,
        NULL) == noErr);
#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticClear();
#endif
    const AudioServerPlugInClientInfo inputClient = client(inputClientID);
    const AudioServerPlugInClientInfo outputClient = client(outputClientID);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, inputClientID) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);

    const uint64_t inputStart = 500000;
    const uint64_t outputStart = inputStart + 184;
    const uint64_t cycleCounter = 900;
    Float32 captured[512 * 2];
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]);
         ++index) {
        captured[index] = 1.0F;
    }

    /* Receiver-first startup: no calibration exists yet. */
    AudioServerPlugInIOCycleInfo readCycle = lunaInputOnlyReadCycle(
        inputStart, cycleCounter, 512);
    CHECK(doIOWithCycle(
        driver, inputClientID, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &readCycle) == noErr);
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]);
         ++index) {
        CHECK(captured[index] == 0.0F);
    }

    /* The matching output-only operation completes calibration for this cycle. */
    Float32 injected[512 * 2];
    fillWaveformAtRate(injected, outputStart, 512, sampleRate);
    AudioServerPlugInIOCycleInfo writeCycle = lunaOutputOnlyWriteCycle(
        outputStart, cycleCounter, 512);
    CHECK(doIOWithCycle(
        driver, outputClientID, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected, 512, &writeCycle) == noErr);

    /* Subsequent input-only reads resolve from persistent calibration. */
    memset(captured, 0, sizeof(captured));
    readCycle = lunaInputOnlyReadCycle(
        inputStart + 512, cycleCounter + 1, 512);
    CHECK(doIOWithCycle(
        driver, inputClientID, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &readCycle) == noErr);
    CHECK(memcmp(injected, captured, sizeof(injected)) == 0);

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.writeMixCallCount == 1);
    CHECK(counters.writePublishedTagFrames == 512);
    CHECK(counters.readInputCallCount == 2);
    CHECK(counters.readValidFrames == 512);
    CHECK(counters.readZeroFilledFrames == 512);
    CHECK(counters.readTimelineUninitializedFrames == 512);
    CHECK(counters.readMappingInvalidFrames == 0);
    CHECK(counters.readMappingInvalidCalls == 0);
    CHECK(counters.timelineResultCounts[kCueletTimelineUninitialized] == 1);
    CHECK(counters.timelineResultCounts[kCueletTimelineOutputTimestampInvalid] ==
        0);
    CHECK(counters.lastRead.inputStart == inputStart + 512);
    CHECK(counters.lastRead.sourceStart == outputStart);
    CHECK(counters.lastRead.resultCode == kCueletRingReadOK);
    CHECK((counters.lastRead.inputTimeFlags &
        kAudioTimeStampSampleTimeValid) != 0);
    CHECK((counters.lastRead.outputTimeFlags &
        kAudioTimeStampSampleTimeValid) == 0);
#endif

    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);
    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, inputClientID) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

static void testCrossOperationCalibration(AudioServerPlugInDriverRef driver)
{
    testCrossOperationCalibrationAtRate(driver, 48000.0, 33, 34);
    testCrossOperationCalibrationAtRate(driver, 44100.0, 35, 36);
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver, kCueletObjectDevice, 48000, NULL) == noErr);
}

static void testReadTimestampValidityMatrix(AudioServerPlugInDriverRef driver)
{
    const AudioServerPlugInClientInfo inputClient = client(37);
    const AudioServerPlugInClientInfo outputClient = client(38);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 37) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 38) == noErr);

    Float32 captured[512 * 2];
    AudioServerPlugInIOCycleInfo cycle = lunaReadCycle(599488);
    CHECK(doIOWithCycle(
        driver, 37, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);

    Float32 blocks[4][512 * 2];
    for (uint32_t block = 0; block < 4; ++block) {
        const uint64_t start = 600000U + (uint64_t)block * 512U;
        fillLunaWaveform(blocks[block], start, 512);
        cycle = lunaOutputOnlyWriteCycle(start, 1200U + block, 512);
        CHECK(doIOWithCycle(
            driver, 38, kCueletObjectOutputStream,
            kAudioServerPlugInIOOperationWriteMix,
            blocks[block], 512, &cycle) == noErr);
    }

    /* Both timestamps valid: input remains authoritative for ReadInput. */
    cycle = lunaReadCycle(600328);
    memset(captured, 0, sizeof(captured));
    CHECK(doIOWithCycle(
        driver, 37, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);
    CHECK(memcmp(blocks[0], captured, sizeof(captured)) == 0);

    /* The confirmed live shape: valid input and no operation-local output. */
    cycle = lunaInputOnlyReadCycle(600840, 1201, 512);
    memset(captured, 0, sizeof(captured));
    CHECK(doIOWithCycle(
        driver, 37, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);
    CHECK(memcmp(blocks[1], captured, sizeof(captured)) == 0);

    /* ReadInput must never fabricate an input position from output time. */
    cycle = lunaReadCycle(601352);
    cycle.mInputTime = (AudioTimeStamp){0};
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]); ++index) {
        captured[index] = 1.0F;
    }
    CHECK(doIOWithCycle(
        driver, 37, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]); ++index) {
        CHECK(captured[index] == 0.0F);
    }

    cycle = (AudioServerPlugInIOCycleInfo){0};
    cycle.mIOCycleCounter = 1203;
    cycle.mNominalIOBufferFrameSize = 512;
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]); ++index) {
        captured[index] = 1.0F;
    }
    CHECK(doIOWithCycle(
        driver, 37, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]); ++index) {
        CHECK(captured[index] == 0.0F);
    }

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 38) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 37) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

static void testDifferentInputOutputFrameSizes(
    AudioServerPlugInDriverRef driver)
{
    const AudioServerPlugInClientInfo inputClient = client(39);
    const AudioServerPlugInClientInfo outputClient = client(40);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 39) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 40) == noErr);

    Float32 captured[512 * 2] = {0};
    AudioServerPlugInIOCycleInfo cycle = lunaReadCycle(699488);
    CHECK(doIOWithCycle(
        driver, 39, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);

    Float32 injected[384 * 2];
    fillLunaWaveform(injected, 700184, 384);
    cycle = lunaOutputOnlyWriteCycle(700184, 1400, 512);
    CHECK(doIOWithCycle(
        driver, 40, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected, 384, &cycle) == noErr);

    cycle = lunaInputOnlyReadCycle(700512, 1401, 512);
    CHECK(doIOWithCycle(
        driver, 39, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 128, &cycle) == noErr);
    CHECK(memcmp(injected, captured, 128U * CUELET_AUDIO_BYTES_PER_FRAME) == 0);

    cycle = lunaInputOnlyReadCycle(700640, 1402, 512);
    CHECK(doIOWithCycle(
        driver, 39, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 256, &cycle) == noErr);
    CHECK(memcmp(
        &injected[128 * 2],
        captured,
        256U * CUELET_AUDIO_BYTES_PER_FRAME) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 40) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 39) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

static void testLunaOneSidedWriteTimestamp(
    AudioServerPlugInDriverRef driver)
{
#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticClear();
#endif
    const AudioServerPlugInClientInfo inputClient = client(31);
    const AudioServerPlugInClientInfo outputClient = client(32);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 31) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 32) == noErr);

    Float32 captured[512 * 2];
    AudioServerPlugInIOCycleInfo cycle = lunaReadCycle(121856);
    CHECK(doIOWithCycle(
        driver,
        31,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured,
        512,
        &cycle) == noErr);

    const uint64_t writeStarts[] = {122040, 122552, 123064, 123576};
    Float32 injected[512 * 2];
    for (size_t index = 0; index < sizeof(writeStarts) / sizeof(writeStarts[0]);
         ++index) {
        fillLunaWaveform(injected, writeStarts[index], 512);
        cycle = lunaWriteCycle(writeStarts[index]);
        CHECK(doIOWithCycle(
            driver,
            32,
            kCueletObjectOutputStream,
            kAudioServerPlugInIOOperationWriteMix,
            injected,
            512,
            &cycle) == noErr);
    }

    cycle = lunaReadCycle(122880);
    memset(captured, 0, sizeof(captured));
    CHECK(doIOWithCycle(
        driver,
        31,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured,
        512,
        &cycle) == noErr);
    fillLunaWaveform(injected, 122552, 512);
    CHECK(memcmp(injected, captured, sizeof(injected)) == 0);
    Float32 leftPeak = 0.0F;
    Float32 rightPeak = 0.0F;
    for (uint32_t frame = 0; frame < 512; ++frame) {
        leftPeak = fmaxf(leftPeak, fabsf(captured[frame * 2]));
        rightPeak = fmaxf(rightPeak, fabsf(captured[frame * 2 + 1]));
    }
    CHECK(leftPeak > 0.24F);
    CHECK(rightPeak > 0.24F);

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticSnapshot snapshots[128];
    uint64_t nextSequence = 0;
    const size_t count = CueletDiagnosticCopy(
        snapshots,
        sizeof(snapshots) / sizeof(snapshots[0]),
        &nextSequence);
    bool sawAcceptedOneSidedWrite = false;
    bool sawExactRead = false;
    for (size_t index = 0; index < count; ++index) {
        const CueletDiagnosticRecordData* data = &snapshots[index].data;
        if (snapshots[index].eventKind == kCueletDiagnosticWriteMix &&
            data->outputSampleFrame == 122552 &&
            data->payloadPeakLeft > 0.24F &&
            data->timelineStatus == kCueletTimelineOK &&
            data->ringWriteStatus == kCueletRingWriteOK &&
            data->ringWriteAcceptedFrames == 512) {
            sawAcceptedOneSidedWrite = true;
        }
        if (snapshots[index].eventKind == kCueletDiagnosticReadInput &&
            data->sourceStartFrame == 122552 &&
            data->ringReadStatus == kCueletRingReadOK &&
            data->validFrameCount == 512) {
            sawExactRead = true;
        }
    }
    CHECK(sawAcceptedOneSidedWrite);
    CHECK(sawExactRead);
#endif

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 32) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 31) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

#ifdef CUELET_AUDIO_DIAGNOSTICS
static void testLiveReadUsesStoredCalibrationWithoutOutputTimestamp(
    AudioServerPlugInDriverRef driver)
{
    CueletDiagnosticClear();
    const AudioServerPlugInClientInfo inputClient = client(54);
    const AudioServerPlugInClientInfo outputClient = client(55);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 54) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 55) == noErr);

    /*
     * The paired cycle establishes the device timeline relationship. Core
     * Audio may omit mOutputTime from later ReadInput operations, so clearing
     * telemetry here must not clear that persistent calibration.
     */
    Float32 calibrationCapture[512 * 2];
    AudioServerPlugInIOCycleInfo calibrationCycle = lunaReadCycle(121856);
    CHECK(doIOWithCycle(
        driver, 54, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        calibrationCapture, 512, &calibrationCycle) == noErr);
    CueletDiagnosticClear();

    Float32 injected[512 * 2];
    fillLunaWaveform(injected, 122552, 512);
    AudioServerPlugInIOCycleInfo writeCycle = lunaWriteCycle(122552);
    CHECK(doIOWithCycle(
        driver, 55, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected, 512, &writeCycle) == noErr);

    Float32 captured[512 * 2];
    for (size_t index = 0; index < sizeof(captured) / sizeof(captured[0]); ++index) {
        captured[index] = 1.0F;
    }
    AudioServerPlugInIOCycleInfo readCycle = lunaReadCycle(122880);
    readCycle.mOutputTime = (AudioTimeStamp){0};
    CHECK(doIOWithCycle(
        driver, 54, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &readCycle) == noErr);
    CHECK(memcmp(injected, captured, sizeof(injected)) == 0);

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.writeMixCallCount == 1);
    CHECK(counters.writeAcceptedFrames == 512);
    CHECK(counters.writeStoredPayloadFrames == 512);
    CHECK(counters.writePublishedTagFrames == 512);
    CHECK(counters.readInputCallCount == 1);
    CHECK(counters.readValidFrames == 512);
    CHECK(counters.readZeroFilledFrames == 0);
    CHECK(counters.readMappingInvalidFrames == 0);
    CHECK(counters.readMappingInvalidCalls == 0);
    CHECK(counters.timelineResultCounts[kCueletTimelineOK] == 2);
    CHECK(counters.timelineResultCounts[
        kCueletTimelineOutputTimestampInvalid] == 0);
    CHECK(counters.firstReadFailure.code ==
        kCueletDiagnosticReadFailureNone);
    CHECK(counters.lastPublishedWrite.start == 122552);
    CHECK(counters.lastRead.inputStart == 122880);
    CHECK(counters.lastRead.sourceStart == 122552);
    CHECK(counters.lastRead.resultCode == kCueletRingReadOK);
    CHECK((counters.lastRead.inputTimeFlags &
        kAudioTimeStampSampleTimeValid) != 0);
    CHECK((counters.lastRead.outputTimeFlags &
        kAudioTimeStampSampleTimeValid) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 55) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 54) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

static void testLongInputOnlyTimelineAtRate(
    AudioServerPlugInDriverRef driver,
    Float64 sampleRate,
    uint32_t inputClientID,
    uint32_t outputClientID)
{
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver,
        kCueletObjectDevice,
        (UInt64)sampleRate,
        NULL) == noErr);
    CueletDiagnosticClear();
    const AudioServerPlugInClientInfo inputClient = client(inputClientID);
    const AudioServerPlugInClientInfo outputClient = client(outputClientID);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, inputClientID) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);

    enum { kLongBlockFrames = 8192 };
    const uint64_t totalFrames = 305U * (uint64_t)sampleRate;
    const uint64_t inputOrigin = 2000000;
    const uint64_t outputOrigin = inputOrigin + 184;
    Float32 injected[kLongBlockFrames * 2];
    Float32 captured[kLongBlockFrames * 2];

    AudioServerPlugInIOCycleInfo readCycle = lunaInputOnlyReadCycle(
        inputOrigin, 5000, kLongBlockFrames);
    CHECK(doIOWithCycle(
        driver, inputClientID, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, kLongBlockFrames, &readCycle) == noErr);

    uint64_t position = 0;
    uint64_t cycleCounter = 5000;
    while (position < totalFrames) {
        const uint32_t frames = (uint32_t)fmin(
            (double)kLongBlockFrames,
            (double)(totalFrames - position));
        const uint64_t outputStart = outputOrigin + position;
        fillWaveformAtRate(injected, outputStart, frames, sampleRate);
        AudioServerPlugInIOCycleInfo writeCycle = lunaOutputOnlyWriteCycle(
            outputStart, cycleCounter, kLongBlockFrames);
        CHECK(doIOWithCycle(
            driver, outputClientID, kCueletObjectOutputStream,
            kAudioServerPlugInIOOperationWriteMix,
            injected, frames, &writeCycle) == noErr);

        readCycle = lunaInputOnlyReadCycle(
            inputOrigin + position + kLongBlockFrames,
            cycleCounter + 1,
            kLongBlockFrames);
        memset(captured, 0, (size_t)frames * CUELET_AUDIO_BYTES_PER_FRAME);
        CHECK(doIOWithCycle(
            driver, inputClientID, kCueletObjectInputStream,
            kAudioServerPlugInIOOperationReadInput,
            captured, frames, &readCycle) == noErr);
        CHECK(memcmp(
            injected,
            captured,
            (size_t)frames * CUELET_AUDIO_BYTES_PER_FRAME) == 0);
        position += frames;
        cycleCounter += 2;
    }

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.writePublishedTagFrames == totalFrames);
    CHECK(counters.readValidFrames == totalFrames);
    CHECK(counters.readTimelineUninitializedFrames == kLongBlockFrames);
    CHECK(counters.readMappingInvalidFrames == 0);
    CHECK(counters.readMappingInvalidCalls == 0);
    CHECK(counters.readGenerationMismatchFrames == 0);
    CHECK(counters.readAbsoluteTagMismatchFrames == 0);
    CHECK(counters.readNotYetWrittenFrames == 0);
    CHECK(counters.readOverwrittenFrames == 0);

    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);
    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, inputClientID) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
}

static void testLongInputOnlyTimelines(AudioServerPlugInDriverRef driver)
{
    testLongInputOnlyTimelineAtRate(driver, 44100.0, 41, 42);
    testLongInputOnlyTimelineAtRate(driver, 48000.0, 43, 44);
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver, kCueletObjectDevice, 48000, NULL) == noErr);
}
#endif

static void testOutputFirstAdditionalReaderAndProducerRestart(
    AudioServerPlugInDriverRef driver)
{
    const AudioServerPlugInClientInfo outputClient = client(51);
    const AudioServerPlugInClientInfo firstInputClient = client(52);
    const AudioServerPlugInClientInfo secondInputClient = client(53);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &firstInputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &secondInputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 51) == noErr);

    Float32 firstBlock[512 * 2];
    Float32 secondBlock[512 * 2];
    Float32 thirdBlock[512 * 2];
    Float32 capture[512 * 2];
    fillLunaWaveform(firstBlock, 200000, 512);
    fillLunaWaveform(secondBlock, 200512, 512);
    fillLunaWaveform(thirdBlock, 201024, 512);
    AudioServerPlugInIOCycleInfo cycle = lunaWriteCycle(200000);
    CHECK(doIOWithCycle(
        driver, 51, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        firstBlock, 512, &cycle) == noErr);
    cycle = lunaWriteCycle(200512);
    CHECK(doIOWithCycle(
        driver, 51, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        secondBlock, 512, &cycle) == noErr);

    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 52) == noErr);
    cycle = lunaReadCycle(200328);
    cycle.mOutputTime = (AudioTimeStamp){0};
    CHECK(doIOWithCycle(
        driver, 52, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        capture, 512, &cycle) == noErr);
    CHECK(memcmp(firstBlock, capture, sizeof(firstBlock)) == 0);

    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 53) == noErr);
    memset(capture, 0, sizeof(capture));
    CHECK(doIOWithCycle(
        driver, 53, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        capture, 512, &cycle) == noErr);
    CHECK(memcmp(firstBlock, capture, sizeof(firstBlock)) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 51) == noErr);
    cycle = lunaReadCycle(200840);
    cycle.mOutputTime = (AudioTimeStamp){0};
    CHECK(doIOWithCycle(
        driver, 52, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        capture, 512, &cycle) == noErr);
    CHECK(memcmp(secondBlock, capture, sizeof(secondBlock)) == 0);

    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 51) == noErr);
    cycle = lunaWriteCycle(201024);
    CHECK(doIOWithCycle(
        driver, 51, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        thirdBlock, 512, &cycle) == noErr);
    cycle = lunaReadCycle(201352);
    cycle.mOutputTime = (AudioTimeStamp){0};
    CHECK(doIOWithCycle(
        driver, 52, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        capture, 512, &cycle) == noErr);
    CHECK(memcmp(thirdBlock, capture, sizeof(thirdBlock)) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 51) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 53) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 52) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &secondInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &firstInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

static void testIntermittentReadClientSlotInterleaving(
    AudioServerPlugInDriverRef driver)
{
#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticClear();
#endif
    const uint32_t outputClientID = 57;
    const uint32_t startedInputClientID = 58;
    const uint32_t unstartedInputClientID = 59;
    const AudioServerPlugInClientInfo outputClient = client(outputClientID);
    const AudioServerPlugInClientInfo startedInputClient =
        client(startedInputClientID);
    const AudioServerPlugInClientInfo unstartedInputClient =
        client(unstartedInputClientID);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &startedInputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &unstartedInputClient) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);
    CHECK((*driver)->StartIO(
        driver, kCueletObjectDevice, startedInputClientID) == noErr);

    static const uint32_t readClients[] = {
        58, 58, 59, 59, 59, 58, 59, 59, 58, 58, 58, 58, 59, 59,
    };
    const uint64_t firstStart = 72528;
    for (uint32_t block = 0;
         block < sizeof(readClients) / sizeof(readClients[0]);
         ++block) {
        const uint64_t sourceStart = firstStart + (uint64_t)block * 512U;
        const uint64_t cycleCounter = 2000U + block;
        Float32 published[512 * 2];
        Float32 captured[512 * 2] = {0};
        fillLunaWaveform(published, sourceStart, 512);
        AudioServerPlugInIOCycleInfo cycle = lunaOutputOnlyWriteCycle(
            sourceStart, cycleCounter, 512);
        CHECK(doIOWithCycle(
            driver, outputClientID, kCueletObjectOutputStream,
            kAudioServerPlugInIOOperationWriteMix,
            published, 512, &cycle) == noErr);

        /* With the measured +184 mapping and 512-frame loopback delay,
         * inputStart + 184 - 512 resolves back to sourceStart. */
        cycle = block == 0
            ? lunaReadCycle(sourceStart + 328U)
            : lunaInputOnlyReadCycle(
                sourceStart + 328U, cycleCounter, 512);
        CHECK(doIOWithCycle(
            driver, readClients[block], kCueletObjectInputStream,
            kAudioServerPlugInIOOperationReadInput,
            captured, 512, &cycle) == noErr);
        CHECK(memcmp(published, captured, sizeof(published)) == 0);
    }

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.writeMixCallCount == 14);
    CHECK(counters.writePublishedTagFrames == 14U * 512U);
    CHECK(counters.readInputCallCount == 14);
    CHECK(counters.readValidFrames == 14U * 512U);
    CHECK(counters.readZeroFilledFrames == 0);
    CHECK(counters.readTimelineUninitializedFrames == 0);
    CHECK(counters.readMappedCalls == 14);
    CHECK(counters.readGenerationResolvedCalls == 14);
    CHECK(counters.readPreRingAcceptedCalls == 14);
    CHECK(counters.readRingLookupCalls == 14);
    CHECK(counters.readRingLookupFrames == 14U * 512U);
    CHECK(counters.readMappedButNoGenerationCalls == 0);
    CHECK(counters.readGenerationButNoRingCalls == 0);
    CHECK(counters.readRingLookupUnavailableCalls == 0);
    CHECK(counters.readRingLookupCalls <= counters.readPreRingAcceptedCalls);
    CHECK(counters.readPreRingAcceptedCalls <=
        counters.readGenerationResolvedCalls);
    CHECK(counters.readGenerationResolvedCalls <= counters.readMappedCalls);
    CHECK(counters.readMappedCalls <= counters.readInputCallCount);

    uint32_t adoptedClientReads = 0;
    uint32_t unstartedClientReads = 0;
    uint32_t initializedClientReads = 0;
    for (uint32_t index = 0; index < counters.criticalEventCount; ++index) {
        const CueletDiagnosticCriticalEvent* event =
            &counters.criticalEvents[index];
        if (event->kind != kCueletDiagnosticCriticalReadAfterNonzeroWrite) {
            continue;
        }
        CHECK(event->readMapped == 1);
        CHECK(event->readGenerationResolved == 1);
        CHECK(event->readPreRingAccepted == 1);
        CHECK(event->readRingLookupReached == 1);
        CHECK(event->validFrames == 512);
        CHECK(event->zeroFilledFrames == 0);
        if (event->clientID == unstartedInputClientID) {
            ++unstartedClientReads;
            CHECK(event->readerGenerationAdopted == 0 ||
                event->readerInitiallyInitialized == 0);
            if (event->readerGenerationAdopted != 0) {
                ++adoptedClientReads;
            }
        } else if (event->clientID == startedInputClientID) {
            ++initializedClientReads;
            CHECK(event->readerInitiallyInitialized == 1);
            CHECK(event->readerGenerationAdopted == 0);
        }
    }
    CHECK(adoptedClientReads == 1);
    CHECK(unstartedClientReads == 7);
    CHECK(initializedClientReads == 7);
    printf(
        "Cuelet intermittent client-slot regression: PASS "
        "ranges=[72528,79696) mapped=14 generation=14 ring=14 "
        "unstarted_client_callbacks=7 adopted_once=1 valid_frames=7168\n");
#endif

    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, startedInputClientID) == noErr);
    CHECK((*driver)->StopIO(
        driver, kCueletObjectDevice, outputClientID) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &unstartedInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &startedInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

typedef struct CalibrationConcurrencyContext {
    AudioServerPlugInDriverRef driver;
    const Float32* targetPayload;
    const Float32* observationPayload;
    _Atomic bool begin;
    _Atomic bool valid;
} CalibrationConcurrencyContext;

static void* concurrentCalibrationObservationWriter(void* opaque)
{
    CalibrationConcurrencyContext* context = opaque;
    while (!atomic_load_explicit(&context->begin, memory_order_acquire)) {
        sched_yield();
    }
    for (uint32_t iteration = 0; iteration < 2000; ++iteration) {
        Float32 payload[512 * 2];
        memcpy(payload, context->observationPayload, sizeof(payload));
        const AudioServerPlugInIOCycleInfo cycle = lunaOutputOnlyWriteCycle(
            310000, 5000U + iteration, 512);
        if (doIOWithCycle(
                context->driver, 60, kCueletObjectOutputStream,
                kAudioServerPlugInIOOperationWriteMix,
                payload, 512, &cycle) != noErr) {
            atomic_store_explicit(
                &context->valid, false, memory_order_release);
            break;
        }
    }
    return NULL;
}

static void* concurrentCalibratedReader(void* opaque)
{
    CalibrationConcurrencyContext* context = opaque;
    while (!atomic_load_explicit(&context->begin, memory_order_acquire)) {
        sched_yield();
    }
    for (uint32_t iteration = 0; iteration < 2000; ++iteration) {
        Float32 captured[512 * 2];
        const AudioServerPlugInIOCycleInfo cycle = lunaInputOnlyReadCycle(
            300328, 5000U + iteration, 512);
        if (doIOWithCycle(
                context->driver, 61, kCueletObjectInputStream,
                kAudioServerPlugInIOOperationReadInput,
                captured, 512, &cycle) != noErr ||
            memcmp(captured, context->targetPayload, sizeof(captured)) != 0) {
            atomic_store_explicit(
                &context->valid, false, memory_order_release);
            break;
        }
    }
    return NULL;
}

static void testStableCalibrationDuringConcurrentObservations(
    AudioServerPlugInDriverRef driver)
{
#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticClear();
#endif
    const AudioServerPlugInClientInfo outputClient = client(60);
    const AudioServerPlugInClientInfo inputClient = client(61);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 60) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 61) == noErr);

    Float32 targetPayload[512 * 2];
    Float32 observationPayload[512 * 2];
    Float32 calibrationCapture[512 * 2];
    fillLunaWaveform(targetPayload, 300000, 512);
    fillLunaWaveform(observationPayload, 310000, 512);
    AudioServerPlugInIOCycleInfo cycle = lunaOutputOnlyWriteCycle(
        300000, 4000, 512);
    CHECK(doIOWithCycle(
        driver, 60, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        targetPayload, 512, &cycle) == noErr);
    cycle = lunaReadCycle(300328);
    CHECK(doIOWithCycle(
        driver, 61, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        calibrationCapture, 512, &cycle) == noErr);
    CHECK(memcmp(
        targetPayload, calibrationCapture, sizeof(targetPayload)) == 0);

    CalibrationConcurrencyContext context = {
        .driver = driver,
        .targetPayload = targetPayload,
        .observationPayload = observationPayload,
        .begin = false,
        .valid = true,
    };
    pthread_t writerThread;
    pthread_t readerThread;
    CHECK(pthread_create(
        &writerThread, NULL, concurrentCalibrationObservationWriter,
        &context) == 0);
    CHECK(pthread_create(
        &readerThread, NULL, concurrentCalibratedReader, &context) == 0);
    atomic_store_explicit(&context.begin, true, memory_order_release);
    CHECK(pthread_join(writerThread, NULL) == 0);
    CHECK(pthread_join(readerThread, NULL) == 0);
    CHECK(atomic_load_explicit(&context.valid, memory_order_acquire));

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.timelineResultCounts[kCueletTimelineUninitialized] == 0);
    CHECK(counters.readTimelineUninitializedFrames == 0);
    CHECK(counters.readMappingInvalidCalls == 0);
    CHECK(counters.readRingLookupUnavailableCalls == 0);
    CHECK(counters.readMappedCalls == 2001);
    CHECK(counters.readGenerationResolvedCalls == 2001);
    CHECK(counters.readPreRingAcceptedCalls == 2001);
    CHECK(counters.readRingLookupCalls == 2001);
#endif

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 61) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 60) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

static void testFunctionalLoopback(AudioServerPlugInDriverRef driver)
{
    const AudioServerPlugInClientInfo outputClient = client(11);
    const AudioServerPlugInClientInfo firstInputClient = client(12);
    const AudioServerPlugInClientInfo secondInputClient = client(13);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &firstInputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &secondInputClient) == noErr);
    resetTestTimeline();
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 11) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 12) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 13) == noErr);

    Float32 injected[16];
    Float32 firstCapture[16] = {0};
    Float32 secondCapture[16] = {0};
    for (uint32_t frame = 0; frame < 8; ++frame) {
        injected[frame * 2] = (Float32)frame + 0.25F;
        injected[frame * 2 + 1] = -((Float32)frame + 0.25F);
    }
    CHECK(doIO(
        driver,
        11,
        kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected,
        8) == noErr);
    Float32 nextInjected[16];
    for (uint32_t frame = 0; frame < 8; ++frame) {
        nextInjected[frame * 2] = (Float32)frame + 100.25F;
        nextInjected[frame * 2 + 1] = -((Float32)frame + 100.25F);
    }
    CHECK(doIO(
        driver,
        11,
        kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        nextInjected,
        8) == noErr);
    CHECK(doIO(
        driver,
        12,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        firstCapture,
        8) == noErr);
    CHECK(doIO(
        driver,
        13,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        secondCapture,
        8) == noErr);
    CHECK(memcmp(injected, firstCapture, sizeof(injected)) == 0);
    CHECK(memcmp(injected, secondCapture, sizeof(injected)) == 0);

    for (uint32_t index = 0; index < 16; ++index) {
        firstCapture[index] = 10.0F;
    }
    CHECK(doIO(
        driver,
        12,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        firstCapture,
        8) == noErr);
    CHECK(memcmp(nextInjected, firstCapture, sizeof(nextInjected)) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 13) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 12) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 11) == noErr);
    resetTestTimeline();
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 11) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 12) == noErr);
    memset(firstCapture, 0x7F, sizeof(firstCapture));
    CHECK(doIO(
        driver,
        12,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        firstCapture,
        8) == noErr);
    for (uint32_t index = 0; index < 16; ++index) {
        CHECK(firstCapture[index] == 0.0F);
    }
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 12) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 11) == noErr);

    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &secondInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &firstInputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

static void setFloatControl(
    AudioServerPlugInDriverRef driver,
    AudioObjectID control,
    AudioObjectPropertySelector selector,
    Float32 value)
{
    AudioObjectPropertyAddress property = address(
        selector,
        kAudioObjectPropertyScopeGlobal);
    CHECK((*driver)->SetPropertyData(
        driver,
        control,
        0,
        &property,
        0,
        NULL,
        sizeof(value),
        &value) == noErr);
}

static void testFunctionalControls(AudioServerPlugInDriverRef driver)
{
    const AudioServerPlugInClientInfo outputClient = client(21);
    const AudioServerPlugInClientInfo inputClient = client(22);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    resetTestTimeline();
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 21) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 22) == noErr);

    setFloatControl(
        driver,
        kCueletObjectOutputVolume,
        kAudioLevelControlPropertyScalarValue,
        0.5F);
    setFloatControl(
        driver,
        kCueletObjectInputVolume,
        kAudioLevelControlPropertyScalarValue,
        0.5F);
    Float32 injected[4] = {1.0F, -1.0F, 0.5F, -0.5F};
    Float32 captured[4] = {0};
    CHECK(doIO(
        driver,
        21,
        kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected,
        2) == noErr);
    Float32 nextInjected[4] = {2.0F, -2.0F, 1.0F, -1.0F};
    CHECK(doIO(
        driver,
        21,
        kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        nextInjected,
        2) == noErr);
    CHECK(doIO(
        driver,
        22,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured,
        2) == noErr);
    CHECK(fabsf(captured[0] - 0.25F) < 0.0001F);
    CHECK(fabsf(captured[1] + 0.25F) < 0.0001F);
    CHECK(fabsf(captured[2] - 0.125F) < 0.0001F);
    CHECK(fabsf(captured[3] + 0.125F) < 0.0001F);

    UInt32 muted = 1;
    AudioObjectPropertyAddress muteProperty = address(
        kAudioBooleanControlPropertyValue,
        kAudioObjectPropertyScopeGlobal);
    CHECK((*driver)->SetPropertyData(
        driver,
        kCueletObjectInputMute,
        0,
        &muteProperty,
        0,
        NULL,
        sizeof(muted),
        &muted) == noErr);
    injected[0] = 1.0F;
    injected[1] = 1.0F;
    CHECK(doIO(
        driver,
        21,
        kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected,
        1) == noErr);
    CHECK(doIO(
        driver,
        22,
        kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured,
        1) == noErr);
    CHECK(captured[0] == 0.0F && captured[1] == 0.0F);

    muted = 0;
    CHECK((*driver)->SetPropertyData(
        driver,
        kCueletObjectInputMute,
        0,
        &muteProperty,
        0,
        NULL,
        sizeof(muted),
        &muted) == noErr);
    setFloatControl(
        driver,
        kCueletObjectOutputVolume,
        kAudioLevelControlPropertyScalarValue,
        1.0F);
    setFloatControl(
        driver,
        kCueletObjectInputVolume,
        kAudioLevelControlPropertyScalarValue,
        1.0F);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 22) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 21) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputClient) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

static void testSampleRateChange(AudioServerPlugInDriverRef driver)
{
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver,
        kCueletObjectDevice,
        44100,
        NULL) == noErr);
    AudioObjectPropertyAddress property = address(
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal);
    Float64 sampleRate = 0;
    UInt32 used = 0;
    CHECK((*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &property,
        0,
        NULL,
        sizeof(sampleRate),
        &used,
        &sampleRate) == noErr);
    CHECK(sampleRate == 44100.0);
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver,
        kCueletObjectDevice,
        96000,
        NULL) == kAudioHardwareIllegalOperationError);
    CHECK((*driver)->PerformDeviceConfigurationChange(
        driver,
        kCueletObjectDevice,
        48000,
        NULL) == noErr);
}

#ifdef CUELET_AUDIO_DIAGNOSTICS
static bool getDiagnosticData(
    AudioServerPlugInDriverRef driver,
    AudioObjectPropertySelector selector,
    void* output,
    size_t expectedSize)
{
    AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
        selector);
    UInt32 size = 0;
    if ((*driver)->GetPropertyDataSize(
            driver, kCueletObjectDevice, 0, &property,
            0, NULL, &size) != noErr ||
        size != sizeof(CFPropertyListRef)) {
        return false;
    }
    CFPropertyListRef value = NULL;
    UInt32 used = 0;
    if ((*driver)->GetPropertyData(
            driver, kCueletObjectDevice, 0, &property,
            0, NULL, sizeof(value), &used, &value) != noErr ||
        used != sizeof(value) || value == NULL ||
        CFGetTypeID(value) != CFDataGetTypeID()) {
        if (value != NULL) CFRelease(value);
        return false;
    }
    const CFDataRef data = (CFDataRef)value;
    const bool valid = CFDataGetLength(data) == (CFIndex)expectedSize;
    if (valid) {
        memcpy(output, CFDataGetBytePtr(data), expectedSize);
    }
    CFRelease(value);
    return valid;
}

static CFDataRef readDiagnosticEventSnapshotForClient(
    AudioServerPlugInDriverRef driver,
    pid_t clientProcessID)
{
    AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyEvents);
    UInt32 size = 0;
    CHECK((*driver)->GetPropertyDataSize(
        driver, kCueletObjectDevice, clientProcessID, &property,
        0, NULL, &size) == noErr);
    CHECK(size == sizeof(CFPropertyListRef));
    CFPropertyListRef value = NULL;
    UInt32 used = 0;
    const OSStatus status = (*driver)->GetPropertyData(
        driver, kCueletObjectDevice, clientProcessID, &property,
        0, NULL, sizeof(value), &used, &value);
    CHECK(status == noErr);
    CHECK(used == sizeof(value));
    CHECK(value != NULL);
    if (status != noErr || value == NULL) {
        if (value != NULL) CFRelease(value);
        return NULL;
    }
    CHECK(CFGetTypeID(value) == CFDataGetTypeID());
    if (CFGetTypeID(value) != CFDataGetTypeID()) {
        CFRelease(value);
        return NULL;
    }
    return (CFDataRef)value;
}

static CFDataRef getDiagnosticEventSnapshot(
    AudioServerPlugInDriverRef driver)
{
    return readDiagnosticEventSnapshotForClient(driver, 7001);
}

static void testDiagnosticProperties(AudioServerPlugInDriverRef driver)
{
    AudioObjectPropertyAddress customInfo = CueletDiagnosticPropertyAddress(
        kAudioObjectPropertyCustomPropertyInfoList);
    CHECK((*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &customInfo));
    Boolean settable = true;
    CHECK((*driver)->IsPropertySettable(
        driver, kCueletObjectDevice, 0, &customInfo, &settable) == noErr);
    CHECK(!settable);
    UInt32 size = 0;
    CHECK((*driver)->GetPropertyDataSize(
        driver, kCueletObjectDevice, 0, &customInfo,
        0, NULL, &size) == noErr);
    CHECK(size == 7 * sizeof(AudioServerPlugInCustomPropertyInfo));
    AudioServerPlugInCustomPropertyInfo customProperties[7] = {0};
    UInt32 used = 0;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &customInfo,
        0, NULL, sizeof(customProperties), &used, customProperties) == noErr);
    CHECK(used == sizeof(customProperties));
    const AudioObjectPropertySelector expectedSelectors[] = {
        kCueletDiagnosticPropertySchema,
        kCueletDiagnosticPropertyCounters,
        kCueletDiagnosticPropertyEvents,
        kCueletDiagnosticPropertyEventCount,
        kCueletDiagnosticPropertyClear,
        kCueletDiagnosticPropertyBuild,
        kCueletDiagnosticPropertyEnabled,
    };
    CHECK(sizeof(AudioServerPlugInCustomPropertyInfo) ==
        3U * sizeof(UInt32));
    CHECK(kCueletDiagnosticPropertyEvents != 'cdev');
    CHECK(kCueletDiagnosticPropertyEvents != 'cdes');
    for (size_t index = 0;
         index < sizeof(expectedSelectors) / sizeof(expectedSelectors[0]);
         ++index) {
        CHECK(customProperties[index].mSelector == expectedSelectors[index]);
        CHECK(customProperties[index].mPropertyDataType ==
            kAudioServerPlugInCustomPropertyDataTypeCFPropertyList);
        const AudioServerPlugInCustomPropertyDataType expectedQualifier =
            kAudioServerPlugInCustomPropertyDataTypeNone;
        CHECK(customProperties[index].mQualifierDataType == expectedQualifier);
        AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
            expectedSelectors[index]);
        CHECK((*driver)->HasProperty(
            driver, kCueletObjectDevice, 0, &property));
        CHECK(!(*driver)->HasProperty(
            driver, kCueletObjectPlugIn, 0, &property));
        CHECK(!(*driver)->HasProperty(
            driver, kCueletObjectInputStream, 0, &property));
        CHECK(!(*driver)->HasProperty(
            driver, kCueletObjectOutputStream, 0, &property));
        UInt32 rejectedSize = 0;
        CHECK((*driver)->GetPropertyDataSize(
            driver, kCueletObjectPlugIn, 0, &property,
            0, NULL, &rejectedSize) == kAudioHardwareUnknownPropertyError);
        property.mScope = kAudioObjectPropertyScopeInput;
        CHECK(!(*driver)->HasProperty(
            driver, kCueletObjectDevice, 0, &property));
        CHECK((*driver)->GetPropertyDataSize(
            driver, kCueletObjectDevice, 0, &property,
            0, NULL, &rejectedSize) == kAudioHardwareUnknownPropertyError);
        property.mScope = kAudioObjectPropertyScopeGlobal;
        property.mElement = 1;
        CHECK(!(*driver)->HasProperty(
            driver, kCueletObjectDevice, 0, &property));
        CHECK((*driver)->GetPropertyDataSize(
            driver, kCueletObjectDevice, 0, &property,
            0, NULL, &rejectedSize) == kAudioHardwareUnknownPropertyError);
    }
    CHECK(customProperties[1].mSelector == kCueletDiagnosticPropertyCounters);
    CHECK(customProperties[1].mPropertyDataType ==
        customProperties[2].mPropertyDataType);
    CHECK(customProperties[1].mQualifierDataType ==
        customProperties[2].mQualifierDataType);
    CHECK(customProperties[2].mSelector == kCueletDiagnosticPropertyEvents);
    CHECK(customProperties[2].mSelector == 'cqev');
    CHECK(customProperties[5].mPropertyDataType ==
        customProperties[2].mPropertyDataType);
    CHECK(customProperties[5].mQualifierDataType ==
        customProperties[2].mQualifierDataType);
    AudioObjectPropertyAddress reservedCdev = CueletDiagnosticPropertyAddress(
        'cdev');
    CHECK(!(*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &reservedCdev));
    AudioObjectPropertyAddress reservedCdes = CueletDiagnosticPropertyAddress(
        'cdes');
    CHECK(!(*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &reservedCdes));

    AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertySchema);
    CHECK((*driver)->IsPropertySettable(
        driver, kCueletObjectDevice, 0, &property, &settable) == noErr);
    CHECK(!settable);
    CueletDiagnosticSchema schema = {0};
    CHECK(getDiagnosticData(
        driver, kCueletDiagnosticPropertySchema, &schema, sizeof(schema)));
    CHECK(schema.schemaVersion == CUELET_DIAGNOSTIC_SCHEMA_VERSION);
    CHECK(schema.eventCapacity == CUELET_DIAGNOSTIC_EVENT_CAPACITY);
    CHECK(schema.eventSnapshotCapacity ==
        CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
    CHECK(schema.maximumAnalyzedFrames == CUELET_DIAGNOSTIC_MAX_ANALYZED_FRAMES);

    CueletDiagnosticBuildInfo build = {0};
    CHECK(getDiagnosticData(
        driver, kCueletDiagnosticPropertyBuild, &build, sizeof(build)));
    CHECK(build.versionPatch == 11);
    CHECK(build.buildNumber == 12);
    CHECK(build.diagnosticEnabled == 1);

    CueletDiagnosticCounters counters = {0};
    CHECK(getDiagnosticData(
        driver, kCueletDiagnosticPropertyCounters,
        &counters, sizeof(counters)));
    CHECK(counters.stateToken != 0);
    CHECK(counters.ringToken != 0);
    CHECK(counters.stateToken != counters.ringToken);

    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyEventCount);
    CFPropertyListRef countValue = NULL;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(countValue), &used, &countValue) == noErr);
    CHECK(countValue != NULL);
    CHECK(CFGetTypeID(countValue) == CFNumberGetTypeID());
    int64_t eventCount = 0;
    CHECK(CFNumberGetValue(
        (CFNumberRef)countValue, kCFNumberSInt64Type, &eventCount));
    CHECK(eventCount > 0);
    CFRelease(countValue);

    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyEvents);
    CHECK((*driver)->IsPropertySettable(
        driver, kCueletObjectDevice, 0, &property, &settable) == noErr);
    CHECK(!settable);
    CHECK((*driver)->GetPropertyDataSize(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, &size) == noErr);
    CHECK(size == sizeof(CFPropertyListRef));
    const CFPropertyListRef wrongQualifier = CFSTR("wrong-type");
    CHECK((*driver)->GetPropertyDataSize(
        driver, kCueletObjectDevice, 0, &property,
        sizeof(wrongQualifier), &wrongQualifier, &size) ==
        kAudioHardwareBadPropertySizeError);
    uint32_t insufficientOutput = 0;
    used = UINT32_MAX;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL,
        sizeof(insufficientOutput), &used, &insufficientOutput) ==
        kAudioHardwareBadPropertySizeError);
    volatile uintptr_t nullBits = 0;
    void* nullOutput = (void*)nullBits;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL,
        sizeof(CFPropertyListRef), &used, nullOutput) ==
        kAudioHardwareIllegalOperationError);

    CFPropertyListRef rejectedOutput = NULL;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        sizeof(wrongQualifier), &wrongQualifier,
        sizeof(rejectedOutput), &used, &rejectedOutput) ==
        kAudioHardwareBadPropertySizeError);
    const CFPropertyListRef rejectedSet = kCFBooleanTrue;
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(rejectedSet), &rejectedSet) ==
        kAudioHardwareIllegalOperationError);
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        sizeof(wrongQualifier), &wrongQualifier,
        sizeof(rejectedSet), &rejectedSet) ==
        kAudioHardwareIllegalOperationError);

    const CFDataRef eventData = getDiagnosticEventSnapshot(driver);
    CHECK(eventData != NULL);
    if (eventData != NULL) {
        CHECK(CFDataGetLength(eventData) >=
            (CFIndex)sizeof(CueletDiagnosticEventExportHeader));
        const CueletDiagnosticEventExportHeader* header =
            (const CueletDiagnosticEventExportHeader*)CFDataGetBytePtr(eventData);
        CHECK(header->schemaVersion == CUELET_DIAGNOSTIC_SCHEMA_VERSION);
        CHECK(header->returnedEventCount > 0);
        CHECK(header->returnedEventCount <=
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(header->eventRecordSize == sizeof(CueletDiagnosticSnapshot));
        CHECK(header->snapshotCapacity ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(header->availableEventCount >= header->returnedEventCount);
        CHECK(header->firstReturnedSequence >=
            header->oldestAvailableSequence);
        CHECK(header->lastReturnedSequence <=
            header->newestAvailableSequence);
        CHECK(CFDataGetLength(eventData) ==
            (CFIndex)(sizeof(*header) +
                header->returnedEventCount *
                    sizeof(CueletDiagnosticSnapshot)));
        const CueletDiagnosticSnapshot* snapshotEvents =
            (const CueletDiagnosticSnapshot*)(header + 1);
        for (uint32_t index = 1;
             index < header->returnedEventCount; ++index) {
            CHECK(snapshotEvents[index].sequence ==
                snapshotEvents[index - 1U].sequence + 1U);
        }
        CHECK(CFPropertyListIsValid(
            eventData, kCFPropertyListBinaryFormat_v1_0));
        CFErrorRef serializationError = NULL;
        CFDataRef serialized = CFPropertyListCreateData(
            kCFAllocatorDefault,
            eventData,
            kCFPropertyListBinaryFormat_v1_0,
            0,
            &serializationError);
        CHECK(serialized != NULL);
        CHECK(serializationError == NULL);
        if (serialized != NULL) {
            CFPropertyListFormat format = kCFPropertyListOpenStepFormat;
            CFErrorRef decodingError = NULL;
            CFPropertyListRef decoded = CFPropertyListCreateWithData(
                kCFAllocatorDefault,
                serialized,
                kCFPropertyListImmutable,
                &format,
                &decodingError);
            CHECK(decoded != NULL);
            CHECK(decodingError == NULL);
            CHECK(decoded != NULL &&
                CFGetTypeID(decoded) == CFDataGetTypeID());
            CHECK(decoded != NULL && CFEqual(decoded, eventData));
            if (decoded != NULL) CFRelease(decoded);
            if (decodingError != NULL) CFRelease(decodingError);
            CFRelease(serialized);
        }
        if (serializationError != NULL) CFRelease(serializationError);

        const CFDataRef secondClientSnapshot =
            readDiagnosticEventSnapshotForClient(driver, 7002);
        CHECK(secondClientSnapshot != NULL);
        CHECK(secondClientSnapshot != NULL &&
            CFEqual(secondClientSnapshot, eventData));
        if (secondClientSnapshot != NULL) CFRelease(secondClientSnapshot);
        CFRelease(eventData);
    }

    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyEnabled);
    CFPropertyListRef enabledValue = NULL;
    CHECK((*driver)->GetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(enabledValue), &used, &enabledValue) == noErr);
    CHECK(enabledValue == kCFBooleanTrue);
    CFRelease(enabledValue);

    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyClear);
    CHECK((*driver)->IsPropertySettable(
        driver, kCueletObjectDevice, 0, &property, &settable) == noErr);
    CHECK(settable);
    CFPropertyListRef clear = kCFBooleanFalse;
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(clear), &clear) ==
        kAudioHardwareIllegalOperationError);
    clear = kCFBooleanTrue;
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(clear), &clear) == noErr);
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.availableEventCount == 0);
    CHECK(counters.droppedEventCount == 0);
    const CFDataRef emptyEventData = getDiagnosticEventSnapshot(driver);
    CHECK(emptyEventData != NULL);
    if (emptyEventData != NULL) {
        const CueletDiagnosticEventExportHeader* emptyHeader =
            (const CueletDiagnosticEventExportHeader*)
                CFDataGetBytePtr(emptyEventData);
        CHECK(CFDataGetLength(emptyEventData) == (CFIndex)sizeof(*emptyHeader));
        CHECK(emptyHeader->returnedEventCount == 0);
        CHECK(emptyHeader->availableEventCount == 0);
        CFRelease(emptyEventData);
    }

    CueletDiagnosticRecordData oneEvent = {.cycleCounter = 77};
    CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &oneEvent);
    const CFDataRef oneEventData = getDiagnosticEventSnapshot(driver);
    CHECK(oneEventData != NULL);
    if (oneEventData != NULL) {
        const CueletDiagnosticEventExportHeader* oneHeader =
            (const CueletDiagnosticEventExportHeader*)
                CFDataGetBytePtr(oneEventData);
        CHECK(oneHeader->returnedEventCount == 1);
        CHECK(oneHeader->availableEventCount == 1);
        const CueletDiagnosticSnapshot* returned =
            (const CueletDiagnosticSnapshot*)(oneHeader + 1);
        CHECK(returned->data.cycleCounter == 77);
        CFRelease(oneEventData);
    }
    clear = kCFBooleanTrue;
    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyClear);
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(clear), &clear) == noErr);

    CueletDiagnosticRecordData wrappedEvent = {0};
    for (uint32_t index = 0;
         index < CUELET_DIAGNOSTIC_EVENT_CAPACITY + 17U;
         ++index) {
        wrappedEvent.cycleCounter = index;
        CueletDiagnosticRecord(kCueletDiagnosticDoIOOperation, &wrappedEvent);
    }
    CueletDiagnosticGetCounters(&counters);
    const uint64_t oldestExpected = counters.nextSequence -
        CUELET_DIAGNOSTIC_EVENT_CAPACITY;
    const CFDataRef wrappedData = getDiagnosticEventSnapshot(driver);
    CHECK(wrappedData != NULL);
    if (wrappedData != NULL) {
        const CueletDiagnosticEventExportHeader* wrappedHeader =
            (const CueletDiagnosticEventExportHeader*)
                CFDataGetBytePtr(wrappedData);
        const CueletDiagnosticSnapshot* wrappedEvents =
            (const CueletDiagnosticSnapshot*)(wrappedHeader + 1);
        CHECK(wrappedHeader->oldestAvailableSequence == oldestExpected);
        CHECK(wrappedHeader->newestAvailableSequence ==
            counters.nextSequence - 1U);
        CHECK(wrappedHeader->availableEventCount ==
            CUELET_DIAGNOSTIC_EVENT_CAPACITY);
        CHECK(wrappedHeader->returnedEventCount ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(wrappedHeader->droppedEventCount == 17);
        CHECK(wrappedHeader->firstReturnedSequence ==
            counters.nextSequence -
                CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY);
        CHECK(wrappedHeader->lastReturnedSequence ==
            counters.nextSequence - 1U);
        for (uint32_t index = 1;
             index < wrappedHeader->returnedEventCount; ++index) {
            CHECK(wrappedEvents[index].sequence ==
                wrappedEvents[index - 1U].sequence + 1U);
        }
        CFRelease(wrappedData);
    }

    clear = kCFBooleanTrue;
    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyClear);
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(clear), &clear) == noErr);
    memset(&counters, 0xff, sizeof(counters));
    CHECK(getDiagnosticData(
        driver, kCueletDiagnosticPropertyCounters,
        &counters, sizeof(counters)));
    CHECK(counters.availableEventCount == 0);
    CHECK(counters.droppedEventCount == 0);

    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyBuild);
    CHECK((*driver)->SetPropertyData(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, sizeof(clear), &clear) ==
        kAudioHardwareIllegalOperationError);
    property = CueletDiagnosticPropertyAddress('zzzz');
    CHECK(!(*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &property));
    CHECK((*driver)->GetPropertyDataSize(
        driver, kCueletObjectDevice, 0, &property,
        0, NULL, &size) == kAudioHardwareUnknownPropertyError);
    printf(
        "Cuelet diagnostic property dispatch: PASS "
        "object=%u scope=global element=main selectors=7 "
        "type=CFPropertyList clear=CFBoolean build=0.1.11/12\n",
        kCueletObjectDevice);
}

static void testOperationBufferSelection(AudioServerPlugInDriverRef driver)
{
    CueletDiagnosticClear();
    const AudioServerPlugInClientInfo outputClient = client(61);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 61) == noErr);

    Boolean willDo = false;
    Boolean inPlace = false;
    CHECK((*driver)->WillDoIOOperation(
        driver, kCueletObjectDevice, 61,
        kAudioServerPlugInIOOperationWriteMix,
        &willDo, &inPlace) == noErr);
    CHECK(willDo && inPlace);
    CHECK((*driver)->WillDoIOOperation(
        driver, kCueletObjectDevice, 61,
        kAudioServerPlugInIOOperationProcessMix,
        &willDo, &inPlace) == noErr);
    CHECK(!willDo);

    Float32 mainSamples[16];
    Float32 secondarySamples[16];
    Float32 zeroSamples[16] = {0};
    for (uint32_t index = 0; index < 16; ++index) {
        mainSamples[index] = (Float32)index + 0.25F;
        secondarySamples[index] = -((Float32)index + 100.0F);
    }
    AudioServerPlugInIOCycleInfo cycle = lunaWriteCycle(300000);
    CHECK((*driver)->DoIOOperation(
        driver, kCueletObjectDevice, kCueletObjectOutputStream, 61,
        kAudioServerPlugInIOOperationWriteMix, 8, &cycle,
        mainSamples, secondarySamples) == noErr);
    cycle = lunaWriteCycle(300008);
    CHECK((*driver)->DoIOOperation(
        driver, kCueletObjectDevice, kCueletObjectOutputStream, 61,
        kAudioServerPlugInIOOperationWriteMix, 8, &cycle,
        mainSamples, NULL) == noErr);
    cycle = lunaWriteCycle(300016);
    CHECK((*driver)->DoIOOperation(
        driver, kCueletObjectDevice, kCueletObjectOutputStream, 61,
        kAudioServerPlugInIOOperationWriteMix, 7, &cycle,
        zeroSamples, NULL) == noErr);
    CHECK((*driver)->DoIOOperation(
        driver, kCueletObjectDevice, kCueletObjectOutputStream, 61,
        kAudioServerPlugInIOOperationWriteMix, 8, &cycle,
        NULL, secondarySamples) == kAudioHardwareIllegalOperationError);
    CHECK((*driver)->DoIOOperation(
        driver, kCueletObjectDevice, kCueletObjectInputStream, 61,
        kAudioServerPlugInIOOperationWriteMix, 8, &cycle,
        mainSamples, NULL) == kAudioHardwareBadObjectError);

    CueletDiagnosticSnapshot events[128];
    uint64_t next = 0;
    const size_t count = CueletDiagnosticCopy(events, 128, &next);
    bool sawBothMainSelected = false;
    bool sawMainOnly = false;
    bool sawMissingMain = false;
    bool sawZeroPayload = false;
    bool sawVariableFrameCount = false;
    for (size_t index = 0; index < count; ++index) {
        const CueletDiagnosticRecordData* data = &events[index].data;
        if (events[index].eventKind != kCueletDiagnosticWriteMix) continue;
        if (data->mainBufferPresent && data->secondaryBufferPresent &&
            data->selectedBuffer == kCueletDiagnosticBufferMain &&
            data->payloadNonzeroFrameCount == 8 &&
            data->publishedPayloadNonzeroFrameCount == 8 &&
            data->ringWriteAcceptedFrames == 8) {
            sawBothMainSelected = true;
        }
        if (data->mainBufferPresent && !data->secondaryBufferPresent &&
            data->selectedBuffer == kCueletDiagnosticBufferMain) {
            sawMainOnly = true;
        }
        if (!data->mainBufferPresent && data->secondaryBufferPresent &&
            data->selectedBuffer == kCueletDiagnosticBufferNone &&
            data->bufferSelectionStatus ==
                (uint32_t)kAudioHardwareIllegalOperationError) {
            sawMissingMain = true;
        }
        if (data->frameCount == 7 && data->payloadZeroFrameCount == 7 &&
            data->publishedPayloadZeroFrameCount == 7 &&
            data->ringWriteAcceptedFrames == 7) {
            sawZeroPayload = true;
            sawVariableFrameCount = true;
        }
    }
    CHECK(sawBothMainSelected);
    CHECK(sawMainOnly);
    CHECK(sawMissingMain);
    CHECK(sawZeroPayload);
    CHECK(sawVariableFrameCount);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 61) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}

static void testSharedStateAndLifecycleTelemetry(
    AudioServerPlugInDriverRef driver)
{
    CueletDiagnosticClear();
    const AudioServerPlugInClientInfo outputClient = client(54);
    const AudioServerPlugInClientInfo inputA = client(55);
    const AudioServerPlugInClientInfo inputB = client(56);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputA) == noErr);
    CHECK((*driver)->AddDeviceClient(
        driver, kCueletObjectDevice, &inputB) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 54) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 55) == noErr);
    CHECK((*driver)->StartIO(driver, kCueletObjectDevice, 56) == noErr);

    Float32 injected[1024];
    Float32 captured[1024] = {0};
    fillLunaWaveform(injected, 400000, 512);
    AudioServerPlugInIOCycleInfo cycle = lunaWriteCycle(400000);
    CHECK(doIOWithCycle(
        driver, 54, kCueletObjectOutputStream,
        kAudioServerPlugInIOOperationWriteMix,
        injected, 512, &cycle) == noErr);
    cycle = lunaReadCycle(400328);
    CHECK(doIOWithCycle(
        driver, 55, kCueletObjectInputStream,
        kAudioServerPlugInIOOperationReadInput,
        captured, 512, &cycle) == noErr);
    CHECK(memcmp(injected, captured, sizeof(injected)) == 0);

    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 56) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 55) == noErr);
    CHECK((*driver)->StopIO(driver, kCueletObjectDevice, 54) == noErr);

    CueletDiagnosticCounters counters = {0};
    CueletDiagnosticGetCounters(&counters);
    CHECK(counters.startIOCount == 3);
    CHECK(counters.stopIOCount == 3);
    CHECK(counters.ringResetCount == 2);
    CHECK(counters.generationChangeCount == 2);
    CHECK(counters.writeMixNonzeroCallCount == 1);
    CHECK(counters.writeAcceptedFrames == 512);
    CHECK(counters.readValidFrames == 512);

    CueletDiagnosticSnapshot events[256];
    uint64_t next = 0;
    const size_t count = CueletDiagnosticCopy(events, 256, &next);
    bool sawWrite = false;
    bool sawRead = false;
    for (size_t index = 0; index < count; ++index) {
        const CueletDiagnosticRecordData* data = &events[index].data;
        if (data->stateToken != 0) CHECK(data->stateToken == counters.stateToken);
        if (data->ringToken != 0) CHECK(data->ringToken == counters.ringToken);
        sawWrite |= events[index].eventKind == kCueletDiagnosticWriteMix;
        sawRead |= events[index].eventKind == kCueletDiagnosticReadInput;
    }
    CHECK(sawWrite);
    CHECK(sawRead);

    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputB) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &inputA) == noErr);
    CHECK((*driver)->RemoveDeviceClient(
        driver, kCueletObjectDevice, &outputClient) == noErr);
}
#else
static void testDiagnosticPropertiesDisabled(
    AudioServerPlugInDriverRef driver)
{
    AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
        kAudioObjectPropertyCustomPropertyInfoList);
    CHECK(!(*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &property));
    property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyEnabled);
    CHECK(!(*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &property));
}
#endif

int main(void)
{
    AudioServerPlugInDriverRef driver = CueletVirtualAudio_Create(
        NULL,
        kAudioServerPlugInTypeUUID);
    CHECK(driver != NULL);
    CHECK(CueletVirtualAudio_Create(NULL, CFUUIDGetConstantUUIDWithBytes(
        NULL,
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15)) == NULL);
    CHECK((*driver)->Initialize(driver, &gTestHost) == noErr);

    testIdentityAndProperties(driver);
    testFunctionalLoopback(driver);
    testCrossOperationCalibration(driver);
    testReadTimestampValidityMatrix(driver);
    testDifferentInputOutputFrameSizes(driver);
    testLunaOneSidedWriteTimestamp(driver);
    testOutputFirstAdditionalReaderAndProducerRestart(driver);
    testIntermittentReadClientSlotInterleaving(driver);
    testStableCalibrationDuringConcurrentObservations(driver);
    testFunctionalControls(driver);
    testSampleRateChange(driver);
#ifdef CUELET_AUDIO_DIAGNOSTICS
    testLiveReadUsesStoredCalibrationWithoutOutputTimestamp(driver);
    testLongInputOnlyTimelines(driver);
    testDiagnosticProperties(driver);
    testOperationBufferSelection(driver);
    testSharedStateAndLifecycleTelemetry(driver);
#else
    testDiagnosticPropertiesDisabled(driver);
#endif

#ifdef CUELET_AUDIO_DIAGNOSTICS
    CueletDiagnosticSnapshot snapshots[32];
    uint64_t nextDiagnosticSequence = 0;
    const size_t diagnosticCount = CueletDiagnosticCopy(
        snapshots,
        sizeof(snapshots) / sizeof(snapshots[0]),
        &nextDiagnosticSequence);
    printf(
        "Cuelet diagnostics: copied=%zu next=%llu dropped=%llu\n",
        diagnosticCount,
        (unsigned long long)nextDiagnosticSequence,
        (unsigned long long)CueletDiagnosticDroppedCount());
#endif

    if (gFailures != 0) {
        fprintf(
            stderr,
            "Cuelet virtual audio driver contract: %u failures in %u assertions\n",
            gFailures,
            gAssertions);
        return 1;
    }
    printf(
        "Cuelet virtual audio driver contract: PASS (%u assertions)\n",
        gAssertions);
    return 0;
}
