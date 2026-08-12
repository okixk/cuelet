#include "CueletVirtualAudioCore.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static CueletRingBuffer* makeRing(void)
{
    CueletRingBuffer* ring = calloc(1, sizeof(*ring));
    CHECK(ring != NULL);
    if (ring != NULL) {
        CueletRingInitialize(ring);
    }
    return ring;
}

static void fillFrames(
    Float32* samples,
    uint64_t firstFrame,
    uint32_t frameCount,
    Float32 bias)
{
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const Float32 value = bias + (Float32)(firstFrame + frame);
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = -value;
    }
}

static void fillWaveform(
    Float32* samples,
    uint64_t firstFrame,
    uint32_t frameCount,
    double sampleRate)
{
    const double pi = acos(-1.0);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const double sample = (double)(firstFrame + frame);
        samples[frame * 2] = (Float32)(0.25 *
            sin(2.0 * pi * 997.0 * sample / sampleRate));
        samples[frame * 2 + 1] = (Float32)(0.25 *
            sin(2.0 * pi * 1499.0 * sample / sampleRate));
    }
}

static bool sameFrames(
    const Float32* first,
    const Float32* second,
    uint32_t frameCount)
{
    return memcmp(
        first,
        second,
        (size_t)frameCount * CUELET_AUDIO_BYTES_PER_FRAME) == 0;
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

static CueletRingReadResult readAt(
    CueletRingBuffer* ring,
    CueletRingReader* reader,
    uint64_t startFrame,
    Float32* output,
    uint32_t frameCount)
{
    return CueletRingReadAt(
        ring,
        reader,
        CueletRingGeneration(ring),
        startFrame,
        output,
        frameCount);
}

static bool writeAt(
    CueletRingBuffer* ring,
    uint64_t startFrame,
    const Float32* input,
    uint32_t frameCount)
{
    return CueletRingWriteAt(
        ring,
        CueletRingGeneration(ring),
        startFrame,
        input,
        frameCount);
}

static void testFormatsAndState(void)
{
    const AudioStreamBasicDescription fortyFour =
        CueletMakeStreamFormat(44100.0);
    const AudioStreamBasicDescription fortyEight =
        CueletMakeStreamFormat(48000.0);
    CHECK(CueletValidateStreamFormat(&fortyFour) == noErr);
    CHECK(CueletValidateStreamFormat(&fortyEight) == noErr);
    CHECK(fortyEight.mChannelsPerFrame == 2);
    AudioStreamBasicDescription mono = fortyEight;
    mono.mChannelsPerFrame = 1;
    mono.mBytesPerFrame = 4;
    mono.mBytesPerPacket = 4;
    CHECK(CueletValidateStreamFormat(&mono) == kAudioDeviceUnsupportedFormatError);
    AudioStreamBasicDescription unsupported = fortyEight;
    unsupported.mSampleRate = 96000.0;
    CHECK(CueletValidateStreamFormat(&unsupported) ==
        kAudioDeviceUnsupportedFormatError);

    CueletIOState state = {0};
    CueletIOStateInitialize(&state);
    const uint64_t initialGeneration = CueletRingGeneration(&state.ring);
    CHECK(CueletIOStateStart(&state) == noErr);
    const uint64_t runningGeneration = CueletRingGeneration(&state.ring);
    CHECK(runningGeneration == initialGeneration + 1);
    CHECK(CueletIOStateStart(&state) == noErr);
    CHECK(CueletRingGeneration(&state.ring) == runningGeneration);
    CHECK(atomic_load(&state.runningClientCount) == 2);
    CHECK(CueletIOStateStop(&state) == noErr);
    CHECK(CueletRingGeneration(&state.ring) == runningGeneration);
    CHECK(CueletIOStateStop(&state) == noErr);
    CHECK(CueletRingGeneration(&state.ring) == runningGeneration + 1);
    CHECK(CueletIOStateStop(&state) == kAudioHardwareIllegalOperationError);
}

static void testTimestampConversion(void)
{
    const Float64 accepted[] = {
        0.0,
        1.0,
        184.0,
        328.0,
        512.0,
        9007199254740991.0,
    };
    for (size_t index = 0; index < sizeof(accepted) / sizeof(accepted[0]);
         ++index) {
        AudioTimeStamp timestamp = {0};
        timestamp.mSampleTime = accepted[index];
        timestamp.mFlags = kAudioTimeStampSampleTimeValid;
        uint64_t frame = UINT64_MAX;
        CHECK(CueletSampleFrameFromTimestamp(
            &timestamp,
            kCueletTimelineInputTimestampInvalid,
            &frame) == kCueletTimelineOK);
        CHECK(frame == (uint64_t)accepted[index]);
    }

    AudioTimeStamp invalid = {0};
    uint64_t frame = 0;
    CHECK(CueletSampleFrameFromTimestamp(
        &invalid,
        kCueletTimelineInputTimestampInvalid,
        &frame) == kCueletTimelineInputTimestampInvalid);
    invalid.mFlags = kAudioTimeStampSampleTimeValid;
    invalid.mSampleTime = -1.0;
    CHECK(CueletSampleFrameFromTimestamp(
        &invalid,
        kCueletTimelineInputTimestampInvalid,
        &frame) == kCueletTimelineNegativeSampleTime);
    invalid.mSampleTime = 184.5;
    CHECK(CueletSampleFrameFromTimestamp(
        &invalid,
        kCueletTimelineInputTimestampInvalid,
        &frame) == kCueletTimelineFractionalSampleTime);
    invalid.mSampleTime = INFINITY;
    CHECK(CueletSampleFrameFromTimestamp(
        &invalid,
        kCueletTimelineInputTimestampInvalid,
        &frame) == kCueletTimelineInputTimestampInvalid);
    invalid.mSampleTime = (Float64)INT64_MAX * 2.0;
    CHECK(CueletSampleFrameFromTimestamp(
        &invalid,
        kCueletTimelineInputTimestampInvalid,
        &frame) == kCueletTimelineSampleTimeOverflow);
}

static void testDetailedRejectionReasonsAndLunaRanges(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 input[512 * 2];
    Float32 output[512 * 2];
    CueletRingReset(ring);
    CueletRingReaderReset(&reader, ring);
    const uint64_t generation = CueletRingGeneration(ring);

    CueletRingReadResult unpublished = CueletRingReadAt(
        ring, &reader, generation, 122552, output, 512);
    CHECK(unpublished.status == kCueletRingReadUnpublished);
    CHECK(unpublished.firstRejectionReason == kCueletRingReadUnpublished);
    CHECK(unpublished.firstRejectedFrame == 122552);
    CHECK(unpublished.unavailableFrames == 512);

    const uint64_t starts[] = {122040, 122552, 123064, 123576};
    for (size_t index = 0; index < sizeof(starts) / sizeof(starts[0]); ++index) {
        fillWaveform(input, starts[index], 512, 48000.0);
        CueletRingWriteResult write = CueletRingWriteAtDetailed(
            ring, generation, starts[index], input, 512);
        CHECK(write.status == kCueletRingWriteOK);
        CHECK(write.acceptedFrames == 512);
    }
    fillWaveform(input, 122552, 512, 48000.0);
    CueletRingReadResult luna = CueletRingReadAt(
        ring, &reader, generation, 122552, output, 512);
    CHECK(luna.status == kCueletRingReadOK);
    CHECK(luna.validFrames == 512);
    CHECK(luna.unavailableFrames == 0);
    CHECK(luna.staleFrames == 0);
    CHECK(sameFrames(input, output, 512));

    CueletRingReset(ring);
    CueletRingReaderReset(&reader, ring);
    const uint64_t partialGeneration = CueletRingGeneration(ring);
    fillFrames(input, 5000, 256, 1.0F);
    CHECK(CueletRingWriteAt(
        ring, partialGeneration, 5000, input, 256));
    CueletRingReadResult partial = CueletRingReadAt(
        ring, &reader, partialGeneration, 5000, output, 512);
    CHECK(partial.status == kCueletRingReadPartialRange);
    CHECK(partial.firstRejectionReason == kCueletRingReadUnpublished);
    CHECK(partial.firstRejectedFrame == 5256);
    CHECK(partial.validFrames == 256);
    CHECK(partial.unavailableFrames == 256);

    fillFrames(input, 7000, 1, 2.0F);
    CHECK(CueletRingWriteAt(
        ring, partialGeneration, 7000, input, 1));
    CueletRingReadResult notYet = CueletRingReadAt(
        ring,
        &reader,
        partialGeneration,
        7000 + CUELET_RING_CAPACITY_FRAMES,
        output,
        1);
    CHECK(notYet.status == kCueletRingReadNotYetWritten);
    CHECK(notYet.firstRejectedFrame == 7000 + CUELET_RING_CAPACITY_FRAMES);

    fillFrames(input, 7000 + CUELET_RING_CAPACITY_FRAMES, 1, 3.0F);
    CHECK(CueletRingWriteAt(
        ring,
        partialGeneration,
        7000 + CUELET_RING_CAPACITY_FRAMES,
        input,
        1));
    CueletRingReadResult overwritten = CueletRingReadAt(
        ring, &reader, partialGeneration, 7000, output, 1);
    CHECK(overwritten.status == kCueletRingReadOverwritten);
    CHECK(overwritten.firstRejectedFrame == 7000);

    CueletRingReset(ring);
    CueletRingReadResult generationMismatch = CueletRingReadAt(
        ring, &reader, partialGeneration, 5000, output, 1);
    CHECK(generationMismatch.status == kCueletRingReadGenerationMismatch);
    CueletRingWriteResult rejectedWrite = CueletRingWriteAtDetailed(
        ring, partialGeneration, 5000, input, 1);
    CHECK(rejectedWrite.status == kCueletRingWriteGenerationMismatch);
    CHECK(rejectedWrite.acceptedFrames == 0);

    CueletRingWriteResult badTime = CueletRingWriteAtDetailed(
        ring,
        CueletRingGeneration(ring),
        UINT64_MAX,
        input,
        2);
    CHECK(badTime.status == kCueletRingWriteInvalidSampleTime);
    free(ring);
}

static void testReadBeforeAndAfterWrite(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 input[16];
    Float32 output[16];
    fillFrames(input, 1000, 8, 1.0F);
    CueletRingReaderReset(&reader, ring);
    memset(output, 0x7F, sizeof(output));
    CueletRingReadResult before = readAt(ring, &reader, 1000, output, 8);
    CHECK(before.validFrames == 0);
    CHECK(before.unavailableFrames == 8);
    CHECK(allZero(output, 8));
    CHECK(writeAt(ring, 1000, input, 8));
    CueletRingReadResult after = readAt(ring, &reader, 1000, output, 8);
    CHECK(after.validFrames == 8);
    CHECK(after.unavailableFrames == 0);
    CHECK(after.staleFrames == 0);
    CHECK(sameFrames(input, output, 8));
    free(ring);
}

static void testVariableSizesAndSplitRanges(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 input[1024 * 2];
    Float32 output[1024 * 2];
    const uint32_t pattern[] = {127, 256, 511, 1024, 3, 512, 255};
    uint64_t position = 100000;
    CueletRingReaderReset(&reader, ring);
    for (uint32_t index = 0; index < 40; ++index) {
        const uint32_t count = pattern[index % 7];
        fillFrames(input, position, count, 3.0F);
        CHECK(writeAt(ring, position, input, count));
        memset(output, 0, sizeof(output));
        CueletRingReadResult result = readAt(
            ring, &reader, position, output, count);
        CHECK(result.validFrames == count);
        CHECK(result.unavailableFrames == 0);
        CHECK(result.staleFrames == 0);
        CHECK(sameFrames(input, output, count));
        position += count;
    }

    fillFrames(input, position, 400, 7.0F);
    CHECK(writeAt(ring, position, input, 400));
    CHECK(readAt(ring, &reader, position, output, 127).validFrames == 127);
    CHECK(readAt(ring, &reader, position + 127, output, 273).validFrames == 273);
    free(ring);
}

static void testTwoReadersAndGenuineSilence(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader first = {0};
    CueletRingReader second = {0};
    Float32 silence[32] = {0};
    Float32 firstOutput[32];
    Float32 secondOutput[32];
    CueletRingReaderReset(&first, ring);
    CueletRingReaderReset(&second, ring);
    CHECK(writeAt(ring, 5000, silence, 16));
    CueletRingReadResult firstResult = readAt(
        ring, &first, 5000, firstOutput, 16);
    CueletRingReadResult secondResult = readAt(
        ring, &second, 5000, secondOutput, 16);
    CHECK(firstResult.validFrames == 16);
    CHECK(secondResult.validFrames == 16);
    CHECK(firstResult.unavailableFrames == 0);
    CHECK(secondResult.unavailableFrames == 0);
    CHECK(allZero(firstOutput, 16));
    CHECK(allZero(secondOutput, 16));
    free(ring);
}

static void testWrapAndNoNewestWindowJump(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    const uint64_t start = (uint64_t)CUELET_RING_CAPACITY_FRAMES * 37 - 7;
    Float32 input[64 * 2];
    Float32 output[64 * 2];
    fillFrames(input, start, 64, 11.0F);
    CueletRingReaderReset(&reader, ring);
    CHECK(writeAt(ring, start, input, 64));
    CHECK(readAt(ring, &reader, start, output, 64).validFrames == 64);
    CHECK(sameFrames(input, output, 64));

    fillFrames(input, start + CUELET_RING_CAPACITY_FRAMES, 64, 13.0F);
    CHECK(writeAt(
        ring,
        start + CUELET_RING_CAPACITY_FRAMES,
        input,
        64));
    memset(output, 0x55, sizeof(output));
    CueletRingReadResult stale = readAt(ring, &reader, start, output, 64);
    CHECK(stale.validFrames == 0);
    CHECK(stale.staleFrames == 64);
    CHECK(allZero(output, 64));
    free(ring);
}

static void testResetGeneration(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 input[16];
    Float32 output[16];
    fillFrames(input, 700, 8, 17.0F);
    CueletRingReaderReset(&reader, ring);
    const uint64_t oldGeneration = CueletRingGeneration(ring);
    CHECK(writeAt(ring, 700, input, 8));
    CHECK(readAt(ring, &reader, 700, output, 8).validFrames == 8);
    CueletRingReset(ring);
    CHECK(CueletRingGeneration(ring) != oldGeneration);
    memset(output, 0x7F, sizeof(output));
    CueletRingReadResult oldResult = readAt(ring, &reader, 700, output, 8);
    CHECK(oldResult.validFrames == 0);
    CHECK(allZero(output, 8));
    fillFrames(input, 700, 8, 19.0F);
    CHECK(writeAt(ring, 700, input, 8));
    CHECK(readAt(ring, &reader, 700, output, 8).validFrames == 8);
    CHECK(sameFrames(input, output, 8));
    free(ring);
}

static void testUninitializedReaderGenerationAdoption(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 published[512 * 2];
    Float32 captured[512 * 2];
    fillWaveform(published, 73552, 512, 48000.0);
    CHECK(writeAt(ring, 73552, published, 512));
    CHECK(!atomic_load_explicit(&reader.initialized, memory_order_acquire));

    const CueletRingReadResult result = readAt(
        ring, &reader, 73552, captured, 512);
    CHECK(result.status == kCueletRingReadOK);
    CHECK(result.validFrames == 512);
    CHECK(result.unavailableFrames == 0);
    CHECK(result.staleFrames == 0);
    CHECK(result.readerInitiallyInitialized == 0);
    CHECK(result.readerGenerationAdopted == 1);
    CHECK(result.generationResolved == 1);
    CHECK(result.preRingAccepted == 1);
    CHECK(result.ringLookupReached == 1);
    CHECK(result.ringLookupFrames == 512);
    CHECK(sameFrames(published, captured, 512));
    free(ring);
}

typedef struct ConcurrentReaderContext {
    CueletRingBuffer* ring;
    CueletRingReader* reader;
    const Float32* expected;
    _Atomic bool* begin;
    _Atomic bool valid;
} ConcurrentReaderContext;

static void* concurrentGenerationReader(void* opaque)
{
    ConcurrentReaderContext* context = opaque;
    Float32 captured[512 * 2];
    while (!atomic_load_explicit(context->begin, memory_order_acquire)) {
        sched_yield();
    }
    for (uint32_t iteration = 0; iteration < 10000; ++iteration) {
        const CueletRingReadResult result = readAt(
            context->ring, context->reader, 73552, captured, 512);
        if (result.status != kCueletRingReadOK ||
            result.generationResolved != 1 ||
            result.preRingAccepted != 1 ||
            result.ringLookupReached != 1 ||
            !sameFrames(context->expected, captured, 512)) {
            atomic_store_explicit(
                &context->valid, false, memory_order_release);
            break;
        }
    }
    return NULL;
}

static void testConcurrentGenerationAdoption(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 published[512 * 2];
    fillWaveform(published, 73552, 512, 48000.0);
    CHECK(writeAt(ring, 73552, published, 512));
    _Atomic bool begin = false;
    ConcurrentReaderContext first = {
        ring, &reader, published, &begin, true,
    };
    ConcurrentReaderContext second = {
        ring, &reader, published, &begin, true,
    };
    pthread_t firstThread;
    pthread_t secondThread;
    CHECK(pthread_create(
        &firstThread, NULL, concurrentGenerationReader, &first) == 0);
    CHECK(pthread_create(
        &secondThread, NULL, concurrentGenerationReader, &second) == 0);
    atomic_store_explicit(&begin, true, memory_order_release);
    CHECK(pthread_join(firstThread, NULL) == 0);
    CHECK(pthread_join(secondThread, NULL) == 0);
    CHECK(atomic_load_explicit(&first.valid, memory_order_acquire));
    CHECK(atomic_load_explicit(&second.valid, memory_order_acquire));
    free(ring);
}

typedef struct PublicationWindowContext {
    CueletRingBuffer* ring;
    _Atomic bool stop;
} PublicationWindowContext;

static void* concurrentPublicationWindowWriter(void* opaque)
{
    PublicationWindowContext* context = opaque;
    while (!atomic_load_explicit(&context->stop, memory_order_acquire)) {
        atomic_store_explicit(
            &context->ring->lastWriteStart, UINT64_MAX, memory_order_release);
        atomic_store_explicit(
            &context->ring->lastWriteEnd, 0, memory_order_release);
        atomic_store_explicit(
            &context->ring->lastWriteStart, 73552, memory_order_release);
        atomic_store_explicit(
            &context->ring->lastWriteEnd, 74064, memory_order_release);
    }
    return NULL;
}

static void testPublicationWindowCannotRejectPublishedSlots(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 published[512 * 2];
    Float32 captured[512 * 2];
    fillWaveform(published, 73552, 512, 48000.0);
    CHECK(writeAt(ring, 73552, published, 512));
    PublicationWindowContext context = {ring, false};
    pthread_t windowThread;
    CHECK(pthread_create(
        &windowThread, NULL, concurrentPublicationWindowWriter, &context) == 0);
    for (uint32_t iteration = 0; iteration < 10000; ++iteration) {
        const CueletRingReadResult result = readAt(
            ring, &reader, 73552, captured, 512);
        CHECK(result.status == kCueletRingReadOK);
        CHECK(result.ringLookupReached == 1);
        CHECK(sameFrames(published, captured, 512));
    }
    atomic_store_explicit(&context.stop, true, memory_order_release);
    CHECK(pthread_join(windowThread, NULL) == 0);
    free(ring);
}

static void testLongTimelineAndPhaseContinuity(double sampleRate)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader first = {0};
    CueletRingReader second = {0};
    enum { maximumFrames = 1024 };
    Float32 input[maximumFrames * 2];
    Float32 firstOutput[maximumFrames * 2];
    Float32 secondOutput[maximumFrames * 2];
    const uint32_t pattern[] = {127, 256, 511, 1024, 3, 512, 255};
    const uint64_t totalFrames = 305ULL * (uint64_t)sampleRate;
    const uint64_t base = (uint64_t)CUELET_RING_CAPACITY_FRAMES * 5;
    const uint32_t delay = 512;
    uint64_t position = 0;
    uint32_t patternIndex = 0;
    CueletRingReaderReset(&first, ring);
    CueletRingReaderReset(&second, ring);

    while (position < totalFrames) {
        uint32_t count = pattern[patternIndex++ % 7];
        if (position + count > totalFrames) {
            count = (uint32_t)(totalFrames - position);
        }
        const uint64_t outputStart = base + delay + position;
        fillWaveform(input, outputStart, count, sampleRate);
        CHECK(writeAt(ring, outputStart, input, count));
        if (position >= delay) {
            fillWaveform(input, base + position, count, sampleRate);
            CueletRingReadResult firstResult = readAt(
                ring,
                &first,
                base + position,
                firstOutput,
                count);
            CueletRingReadResult secondResult = readAt(
                ring,
                &second,
                base + position,
                secondOutput,
                count);
            CHECK(firstResult.validFrames == count);
            CHECK(secondResult.validFrames == count);
            CHECK(firstResult.unavailableFrames == 0);
            CHECK(secondResult.unavailableFrames == 0);
            CHECK(sameFrames(input, firstOutput, count));
            CHECK(sameFrames(input, secondOutput, count));
        }
        position += count;
    }
    CHECK(atomic_load(&ring->underrunCount) == 0);
    CHECK(atomic_load(&ring->overrunCount) == 0);
    CHECK(atomic_load(&ring->rejectedWriteCount) == 0);
    free(ring);
}

static void testMappingNoCompressedTimeline(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    Float32 input[512 * 2];
    Float32 output[512 * 2];
    const uint64_t base = 1000000;
    const uint32_t delay = 512;
    CueletRingReaderReset(&reader, ring);
    for (uint32_t block = 0; block < 2000; ++block) {
        const uint64_t outputStart = base + delay + (uint64_t)block * 512;
        fillFrames(input, outputStart, 512, 23.0F);
        CHECK(writeAt(ring, outputStart, input, 512));
        if (block > 0) {
            CHECK(readAt(
                ring,
                &reader,
                outputStart - delay,
                output,
                512).validFrames == 512);
            fillFrames(input, outputStart - delay, 512, 23.0F);
            CHECK(sameFrames(input, output, 512));
        }
    }
    free(ring);
}

static void testMappingComparisonAndMarkerOrdering(void)
{
    const int64_t inputStart = 122880;
    const int64_t measuredOffset = 184;
    const int64_t delay = 512;
    const int64_t inputOrigin = 121856;
    const int64_t outputOrigin = 122040;
    const int64_t mappingA = inputStart + measuredOffset - delay;
    const int64_t mappingB = inputStart + measuredOffset;
    const int64_t mappingC = outputOrigin +
        (inputStart - inputOrigin) - delay;
    CHECK(mappingA == 122552);
    CHECK(mappingB == 123064);
    CHECK(mappingC == 122552);
    CHECK(mappingA == mappingC);
    CHECK(mappingA != mappingB);

    CueletRingBuffer* ring = makeRing();
    CueletRingReader reader = {0};
    CueletRingReaderReset(&reader, ring);
    const uint64_t base = 800000;
    const double markers[] = {440.0, 660.0, 1000.0, 1200.0};
    Float32 input[512 * 2];
    Float32 output[512 * 2];
    const double pi = acos(-1.0);
    uint64_t position = base;
    for (size_t marker = 0;
         marker < sizeof(markers) / sizeof(markers[0]);
         ++marker) {
        for (uint32_t frame = 0; frame < 512; ++frame) {
            const Float32 value = (Float32)(0.25 * sin(
                2.0 * pi * markers[marker] * frame / 48000.0));
            input[frame * 2] = value;
            input[frame * 2 + 1] = -value;
        }
        CHECK(writeAt(ring, position, input, 512));
        CHECK(readAt(ring, &reader, position, output, 512).status ==
            kCueletRingReadOK);
        CHECK(sameFrames(input, output, 512));
        position += 512;
        memset(input, 0, sizeof(input));
        CHECK(writeAt(ring, position, input, 512));
        CHECK(readAt(ring, &reader, position, output, 512).status ==
            kCueletRingReadOK);
        CHECK(allZero(output, 512));
        position += 512;
    }
    free(ring);
}

/* Diagnostic correctness baseline: deliberately use a large fixed delay and
   the same absolute-range API, without any newest-window recovery shortcut. */
static void testConservativeTimelineBaseline(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader first = {0};
    CueletRingReader second = {0};
    Float32 input[1024 * 2];
    Float32 firstOutput[1024 * 2];
    Float32 secondOutput[1024 * 2];
    const uint32_t pattern[] = {127, 256, 511, 1024, 3, 512, 255};
    const uint64_t base = (uint64_t)CUELET_RING_CAPACITY_FRAMES * 8;
    const uint32_t delay = 4096;
    uint64_t position = 0;
    uint32_t patternIndex = 0;
    CueletRingReaderReset(&first, ring);
    CueletRingReaderReset(&second, ring);
    for (uint32_t block = 0; block < 20000; ++block) {
        uint32_t count = pattern[patternIndex++ % 7];
        const uint64_t outputStart = base + delay + position;
        fillWaveform(input, outputStart, count, 48000.0);
        CHECK(writeAt(ring, outputStart, input, count));
        if (position >= delay) {
            fillWaveform(input, base + position, count, 48000.0);
            CHECK(readAt(
                ring, &first, base + position, firstOutput, count).validFrames ==
                count);
            CHECK(readAt(
                ring, &second, base + position, secondOutput, count).validFrames ==
                count);
            CHECK(sameFrames(input, firstOutput, count));
            CHECK(sameFrames(input, secondOutput, count));
        }
        position += count;
    }
    free(ring);
}

typedef struct ModelSlot {
    uint64_t frame;
    uint64_t generation;
    Float32 value;
    bool occupied;
    bool valid;
} ModelSlot;

static uint32_t nextRandom(uint32_t* state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void testDeterministicEventOrderModel(void)
{
    CueletRingBuffer* ring = makeRing();
    CueletRingReader first = {0};
    CueletRingReader second = {0};
    ModelSlot model[CUELET_RING_CAPACITY_FRAMES] = {0};
    Float32 input[96 * 2];
    Float32 firstOutput[96 * 2];
    Float32 secondOutput[96 * 2];
    uint32_t randomState = 0xC0FFEE11U;
    uint64_t generation = CueletRingGeneration(ring);
    CueletRingReaderReset(&first, ring);
    CueletRingReaderReset(&second, ring);

    for (uint32_t event = 0; event < 12000; ++event) {
        const uint64_t start = nextRandom(&randomState) %
            (CUELET_RING_CAPACITY_FRAMES * 2ULL);
        const uint32_t count = 1 + nextRandom(&randomState) % 96;
        if ((nextRandom(&randomState) & 3U) != 0) {
            fillFrames(input, start, count, (Float32)event + 100.0F);
            CHECK(writeAt(ring, start, input, count));
            for (uint32_t frame = 0; frame < count; ++frame) {
                ModelSlot* slot = &model[(start + frame) %
                    CUELET_RING_CAPACITY_FRAMES];
                slot->frame = start + frame;
                slot->generation = generation;
                slot->value = (Float32)event + 100.0F +
                    (Float32)(start + frame);
                slot->occupied = true;
                slot->valid = true;
            }
        } else {
            memset(firstOutput, 0x55, sizeof(firstOutput));
            CueletRingReadResult firstResult = CueletRingReadAt(
                ring,
                &first,
                generation,
                start,
                firstOutput,
                count);
            CueletRingReadResult secondResult = CueletRingReadAt(
                ring,
                &second,
                generation,
                start,
                secondOutput,
                count);
            uint32_t validCount = 0;
            uint32_t unavailableCount = 0;
            uint32_t staleCount = 0;
            for (uint32_t frame = 0; frame < count; ++frame) {
                const ModelSlot* slot = &model[(start + frame) %
                    CUELET_RING_CAPACITY_FRAMES];
                const bool valid = slot->valid &&
                    slot->frame == start + frame &&
                    slot->generation == generation;
                if (valid) {
                    ++validCount;
                    CHECK(firstOutput[frame * 2] == slot->value);
                    CHECK(firstOutput[frame * 2 + 1] == -slot->value);
                    CHECK(secondOutput[frame * 2] == slot->value);
                    CHECK(secondOutput[frame * 2 + 1] == -slot->value);
                } else {
                    if (slot->occupied) {
                        ++staleCount;
                    } else {
                        ++unavailableCount;
                    }
                    CHECK(firstOutput[frame * 2] == 0.0F);
                    CHECK(firstOutput[frame * 2 + 1] == 0.0F);
                    CHECK(secondOutput[frame * 2] == 0.0F);
                    CHECK(secondOutput[frame * 2 + 1] == 0.0F);
                }
            }
            CHECK(validCount + unavailableCount + staleCount == count);
            CHECK(firstResult.validFrames == validCount);
            CHECK(secondResult.validFrames == validCount);
            CHECK(firstResult.validFrames + firstResult.unavailableFrames +
                firstResult.staleFrames == count);
            CHECK(secondResult.validFrames + secondResult.unavailableFrames +
                secondResult.staleFrames == count);
        }
        if (event == 6000) {
            CueletRingReset(ring);
            generation = CueletRingGeneration(ring);
            for (uint32_t slot = 0; slot < CUELET_RING_CAPACITY_FRAMES; ++slot) {
                model[slot].valid = false;
            }
        }
    }
    free(ring);
}

typedef struct ConcurrentContext {
    CueletRingBuffer* ring;
    _Atomic bool valid;
    uint32_t writer;
    uint32_t iterations;
} ConcurrentContext;

static void* concurrentWriter(void* opaque)
{
    ConcurrentContext* context = opaque;
    Float32 samples[64 * 2];
    const uint64_t base = (uint64_t)context->writer * 1000000ULL;
    for (uint32_t iteration = 0;
         iteration < context->iterations;
         ++iteration) {
        const uint64_t start = base + (uint64_t)iteration * 64;
        fillFrames(samples, start, 64, (Float32)context->writer);
        if (!writeAt(context->ring, start, samples, 64)) {
            atomic_store(&context->valid, false);
        }
    }
    return NULL;
}

static void testConcurrentDisjointPublication(void)
{
    CueletRingBuffer* ring = makeRing();
    uint32_t iterations = 10000;
    const char* stress = getenv("CUELET_DRIVER_STRESS_ITERATIONS");
    if (stress != NULL) {
        const unsigned long parsed = strtoul(stress, NULL, 10);
        if (parsed > 0 && parsed <= 1000000UL) {
            iterations = (uint32_t)parsed;
        }
    }
    ConcurrentContext first = {ring, true, 1, iterations};
    ConcurrentContext second = {ring, true, 2, iterations};
    pthread_t firstThread;
    pthread_t secondThread;
    CHECK(pthread_create(&firstThread, NULL, concurrentWriter, &first) == 0);
    CHECK(pthread_create(&secondThread, NULL, concurrentWriter, &second) == 0);
    CHECK(pthread_join(firstThread, NULL) == 0);
    CHECK(pthread_join(secondThread, NULL) == 0);
    CHECK(atomic_load(&first.valid));
    CHECK(atomic_load(&second.valid));
    CHECK(atomic_load(&ring->rejectedWriteCount) == 0);
    free(ring);
}

int main(void)
{
    testFormatsAndState();
    testTimestampConversion();
    testDetailedRejectionReasonsAndLunaRanges();
    testReadBeforeAndAfterWrite();
    testVariableSizesAndSplitRanges();
    testTwoReadersAndGenuineSilence();
    testWrapAndNoNewestWindowJump();
    testResetGeneration();
    testUninitializedReaderGenerationAdoption();
    testConcurrentGenerationAdoption();
    testPublicationWindowCannotRejectPublishedSlots();
    testLongTimelineAndPhaseContinuity(44100.0);
    testLongTimelineAndPhaseContinuity(48000.0);
    testMappingNoCompressedTimeline();
    testMappingComparisonAndMarkerOrdering();
    testConservativeTimelineBaseline();
    testDeterministicEventOrderModel();
    testConcurrentDisjointPublication();

    if (gFailures != 0) {
        fprintf(
            stderr,
            "Cuelet virtual audio timeline core: %u failures in %u assertions\n",
            gFailures,
            gAssertions);
        return 1;
    }
    printf(
        "Cuelet virtual audio timeline core: PASS (%u assertions)\n",
        gAssertions);
    return 0;
}
