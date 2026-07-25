#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VirtualAudioRingBufferModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using cuelet::windows::VirtualAudioRingBufferModel;

constexpr std::size_t StereoFrameBytes = 4;
constexpr double Pi = 3.14159265358979323846;

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::array<std::uint8_t, StereoFrameBytes> wordFrame(
    std::uint32_t value)
{
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24)};
}

void appendFrame(
    std::vector<std::uint8_t>& bytes,
    std::array<std::uint8_t, StereoFrameBytes> const& frame)
{
    bytes.insert(bytes.end(), frame.begin(), frame.end());
}

std::vector<std::uint8_t> makeTone(
    std::vector<double> const& frequencies,
    std::size_t frameCount)
{
    std::vector<std::uint8_t> result;
    result.reserve(frameCount * StereoFrameBytes);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        double sample = 0.0;
        for (const double frequency : frequencies) {
            sample += std::sin(
                2.0 * Pi * frequency *
                static_cast<double>(frame) / 48'000.0);
        }
        sample /= static_cast<double>(frequencies.size());
        const auto pcm = static_cast<std::int16_t>(
            std::lround(sample * 0.78 * 32767.0));
        appendFrame(result, {
            static_cast<std::uint8_t>(pcm),
            static_cast<std::uint8_t>(
                static_cast<std::uint16_t>(pcm) >> 8),
            static_cast<std::uint8_t>(pcm),
            static_cast<std::uint8_t>(
                static_cast<std::uint16_t>(pcm) >> 8)});
    }
    return result;
}

void exerciseContinuity(
    std::vector<std::uint8_t> const& source,
    const char* failure)
{
    VirtualAudioRingBufferModel fifo(
        4096, StereoFrameBytes, 128);
    VirtualAudioRingBufferModel::Reader reader;
    check(
        fifo.read(reader, StereoFrameBytes) ==
            std::vector<std::uint8_t>(StereoFrameBytes),
        "a continuity reader must initialize with silence");

    constexpr std::size_t leadBytes = 512;
    check(source.size() > leadBytes, "continuity input is too short");
    check(
        fifo.publish(source.data(), leadBytes),
        "continuity prefill was rejected");

    constexpr std::array<std::size_t, 8> chunkFrames{
        1, 3, 16, 7, 31, 2, 23, 11};
    std::size_t readOffset = 0;
    std::size_t writeOffset = leadBytes;
    std::size_t chunkIndex = 0;
    while (readOffset < source.size()) {
        const auto requestedBytes = std::min(
            chunkFrames[chunkIndex % chunkFrames.size()] *
                StereoFrameBytes,
            source.size() - readOffset);
        const auto block = fifo.read(reader, requestedBytes);
        check(
            std::equal(
                block.begin(), block.end(),
                source.begin() +
                    static_cast<std::ptrdiff_t>(readOffset)),
            failure);
        readOffset += requestedBytes;

        const auto publishBytes = std::min(
            requestedBytes, source.size() - writeOffset);
        if (publishBytes != 0) {
            check(
                fifo.publish(
                    source.data() + writeOffset,
                    publishBytes),
                "continuity publish was rejected");
            writeOffset += publishBytes;
        }
        ++chunkIndex;
    }
}

void runDeterministicSignalTests()
{
    constexpr std::size_t frames = 96'000;
    for (const double frequency :
         {40.0, 80.0, 100.0, 440.0, 997.0}) {
        exerciseContinuity(
            makeTone({frequency}, frames),
            "a single tone changed across FIFO wraparound");
    }
    exerciseContinuity(
        makeTone({40.0, 100.0, 440.0, 997.0}, frames),
        "a multitone changed across FIFO wraparound");

    std::vector<std::uint8_t> impulse(
        frames * StereoFrameBytes);
    impulse[0] = 0xff;
    impulse[1] = 0x7f;
    impulse[2] = 0x00;
    impulse[3] = 0x80;
    exerciseContinuity(
        impulse, "an impulse changed across FIFO wraparound");
    exerciseContinuity(
        std::vector<std::uint8_t>(
            frames * StereoFrameBytes),
        "silence changed across FIFO wraparound");

    std::vector<std::uint8_t> extrema;
    for (std::size_t frame = 0; frame < 20'000; ++frame) {
        appendFrame(
            extrema,
            frame % 2 == 0
                ? std::array<std::uint8_t, 4>{
                    0x00, 0x80, 0xff, 0x7f}
                : std::array<std::uint8_t, 4>{
                    0xff, 0x7f, 0x00, 0x80});
    }
    exerciseContinuity(
        extrema,
        "stereo minimum/maximum samples changed in the FIFO");
}

std::vector<std::uint8_t> numberedFrames(
    std::size_t frameCount,
    std::uint32_t first = 1)
{
    std::vector<std::uint8_t> result;
    result.reserve(frameCount * StereoFrameBytes);
    for (std::size_t index = 0; index < frameCount; ++index) {
        appendFrame(
            result,
            wordFrame(
                first + static_cast<std::uint32_t>(index)));
    }
    return result;
}

void runGeometryAndPolicyTests()
{
    {
        using namespace cuelet::audio_fifo;
        const Geometry geometry{{4}, {32}, {8}};
        CopyPlan valid{};
        valid.status = TransferStatus::Ready;
        valid.sourceOffset = {8};
        valid.ringOffset = {28};
        valid.firstBytes = {4};
        valid.secondBytes = {4};
        check(
            IsCopyPlanValid(valid, {16}, geometry),
            "a bounded, frame-aligned wrap copy plan was rejected");

        auto invalidOffset = valid;
        invalidOffset.ringOffset = {32};
        check(
            !IsCopyPlanValid(invalidOffset, {16}, geometry),
            "a ring offset at capacity must be rejected");

        auto invalidSpan = valid;
        invalidSpan.firstBytes = {8};
        check(
            !IsCopyPlanValid(invalidSpan, {20}, geometry),
            "a first span crossing the ring boundary must be rejected");

        auto invalidAlignment = valid;
        invalidAlignment.sourceOffset = {6};
        check(
            !IsCopyPlanValid(invalidAlignment, {14}, geometry),
            "an unaligned source span must be rejected");

        FifoCore lifecycle;
        check(
            lifecycle.Configure({64}, {4}, {8}),
            "lifecycle test FIFO configuration failed");
        lifecycle.BeginTeardown();
        const auto teardownGeneration =
            lifecycle.CurrentGeneration().value;
        lifecycle.BeginTeardown();
        check(
            lifecycle.CurrentGeneration().value == teardownGeneration,
            "repeated FIFO teardown must be idempotent");

        const auto oneMillisecond = PlanByteDisplacement(
            {192000}, 10000, 0, 0, {4096}, {4});
        check(
            oneMillisecond.valid && !oneMillisecond.clamped &&
                oneMillisecond.byteCount.value == 192,
            "48 kHz displacement must preserve exact frame geometry");

        CueletFifoUInt64 fractionalCarry = 0;
        CueletFifoUInt64 fractionalFrames = 0;
        for (std::size_t millisecond = 0;
             millisecond < 10;
             ++millisecond) {
            const auto fractional = PlanByteDisplacement(
                {176400}, 10000, 0, fractionalCarry,
                {4096}, {4});
            check(
                fractional.valid && !fractional.clamped,
                "fractional displacement planning failed");
            fractionalFrames += fractional.byteCount.value;
            fractionalCarry = fractional.thousandthByteCarry;
        }
        check(
            fractionalFrames == 1764 && fractionalCarry == 0,
            "fractional bytes must carry forward without splitting frames");

        const auto delayed = PlanByteDisplacement(
            {192000}, 60ULL * 10'000'000ULL, 0, 0,
            {4096}, {4});
        check(
            delayed.valid && delayed.clamped &&
                delayed.byteCount.value == 4096,
            "a long callback delay must be bounded to one DMA buffer");
    }

    {
        VirtualAudioRingBufferModel fifo(32, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto exact = numberedFrames(8);
        fifo.publish(exact);
        check(
            fifo.read(reader, exact.size()) == exact,
            "an exact-capacity write/read must be lossless");
    }
    {
        VirtualAudioRingBufferModel fifo(32, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto almost = numberedFrames(7);
        fifo.publish(almost);
        check(
            fifo.read(reader, almost.size()) == almost,
            "capacity minus one frame must be lossless");
    }
    {
        VirtualAudioRingBufferModel fifo(32, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto overflow = numberedFrames(9);
        fifo.publish(overflow);
        check(
            fifo.read(reader, 4) ==
                std::vector<std::uint8_t>(
                    overflow.begin() + 28,
                    overflow.begin() + 32),
            "capacity plus one frame must use bounded overflow recovery");
    }
    {
        VirtualAudioRingBufferModel fifo(31, 6, 6);
        check(
            fifo.capacity() == 30,
            "capacity must be aligned to whole frames");
    }

    {
        VirtualAudioRingBufferModel fifo(256, 4, 16);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto input = numberedFrames(32);
        for (std::size_t offset = 0;
             offset < input.size();
             offset += StereoFrameBytes) {
            fifo.publish(
                input.data() + offset, StereoFrameBytes);
        }
        check(
            fifo.read(reader, 64) ==
                std::vector<std::uint8_t>(
                    input.begin(), input.begin() + 64),
            "many tiny writes followed by a large read lost order");
    }
    {
        VirtualAudioRingBufferModel fifo(256, 4, 16);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto input = numberedFrames(48);
        fifo.publish(input);
        for (std::size_t index = 0; index < 32; ++index) {
            check(
                fifo.read(reader, 4) ==
                    std::vector<std::uint8_t>(
                        input.begin() +
                            static_cast<std::ptrdiff_t>(index * 4),
                        input.begin() +
                            static_cast<std::ptrdiff_t>(
                                index * 4 + 4)),
                "a large write followed by tiny reads lost order");
        }
    }

    for (std::uint64_t offset = 0; offset < 64; offset += 4) {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        check(
            fifo.forceWritePositionForTesting(offset),
            "test cursor must be frame aligned");
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        const auto pair = numberedFrames(
            2, static_cast<std::uint32_t>(offset + 1));
        fifo.publish(pair);
        check(
            fifo.read(reader, 4) ==
                std::vector<std::uint8_t>(
                    pair.begin(), pair.begin() + 4),
            "wraparound at a frame boundary changed a frame");
    }

    {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        fifo.publish(numberedFrames(2));
        check(
            fifo.read(reader, 4) == numberedFrames(1),
            "startup reserve did not preserve the oldest frame");
        check(
            fifo.read(reader, 8) ==
                std::vector<std::uint8_t>(8),
            "underflow must return a whole quantum of silence");
        fifo.publish(numberedFrames(2, 3));
        check(
            fifo.read(reader, 8) == numberedFrames(2, 2),
            "underflow must not consume or expose stale samples");
    }

    {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        VirtualAudioRingBufferModel::Reader first;
        VirtualAudioRingBufferModel::Reader second;
        fifo.read(first, 4);
        fifo.read(second, 4);
        const auto input = numberedFrames(4);
        fifo.publish(input);
        check(
            fifo.read(first, 4) == numberedFrames(1),
            "the first reader lost its cursor");
        check(
            fifo.read(second, 4) == numberedFrames(1),
            "independent readers must see the same live data");

        VirtualAudioRingBufferModel::Reader recreated;
        fifo.read(recreated, 4);
        fifo.publish(numberedFrames(2, 5));
        check(
            fifo.read(recreated, 4) == numberedFrames(1, 5),
            "a recreated reader must start at its new live edge");
    }

    {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        fifo.publish(numberedFrames(3));
        fifo.reset();
        check(
            fifo.read(reader, 4) ==
                std::vector<std::uint8_t>(4),
            "reset during a simulated read must change generation");
        fifo.publish(numberedFrames(1, 100));
        fifo.reset();
        check(
            fifo.read(reader, 4) ==
                std::vector<std::uint8_t>(4),
            "a write-side reset must resynchronize the reader");
        fifo.publish(numberedFrames(2, 200));
        check(
            fifo.read(reader, 4) == numberedFrames(1, 200),
            "reset during a simulated write must discard old data");
    }

    bool rejectedWrite = false;
    bool rejectedRead = false;
    try {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        fifo.publish(std::vector<std::uint8_t>{1, 2, 3});
    } catch (std::invalid_argument const&) {
        rejectedWrite = true;
    }
    try {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        static_cast<void>(fifo.read(reader, 6));
    } catch (std::invalid_argument const&) {
        rejectedRead = true;
    }
    check(
        rejectedWrite && rejectedRead,
        "unaligned byte counts must fail safely");

    {
        cuelet::audio_fifo::Bytes converted{};
        check(
            !cuelet::audio_fifo::TryFramesToBytes(
                {
                    (std::numeric_limits<std::uint64_t>::max)()},
                {2},
                &converted),
            "frame-to-byte multiplication must reject overflow");

        VirtualAudioRingBufferModel fifo(64, 4, 8);
        const auto oldGeneration = fifo.generation();
        check(
            fifo.forceWritePositionForTesting(
                (std::numeric_limits<std::uint64_t>::max)() - 3),
            "the integer-boundary cursor must remain frame aligned");
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        fifo.publish(numberedFrames(2, 10));
        check(
            fifo.generation() != oldGeneration,
            "cursor overflow must rebase with a new generation");
        check(
            fifo.read(reader, 4) ==
                std::vector<std::uint8_t>(4),
            "a rebase must invalidate the prior reader generation");
        fifo.publish(numberedFrames(2, 12));
        check(
            fifo.read(reader, 4) == numberedFrames(1, 12),
            "post-rebase data must be readable without truncation");
    }

    for (std::size_t cycle = 0; cycle < 3000; ++cycle) {
        VirtualAudioRingBufferModel fifo(64, 4, 8);
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        fifo.publish(numberedFrames(2, 1));
        check(
            fifo.read(reader, 4) == numberedFrames(1, 1),
            "a repeated start/stop cycle changed FIFO behavior");
    }
}

void runOneWriterOneReaderStress()
{
    constexpr std::size_t frameCount = 40'000;
    constexpr std::size_t maximumLeadFrames = 4096;
    VirtualAudioRingBufferModel fifo(64 * 1024, 4, 64);
    VirtualAudioRingBufferModel::Reader reader;
    fifo.read(reader, 4);

    std::atomic<std::size_t> produced{};
    std::atomic<std::size_t> consumed{};
    std::atomic<bool> failed{};
    std::atomic<bool> writerDone{};

    std::thread writer([&] {
        try {
            for (std::size_t index = 0;
                 index < frameCount && !failed.load();
                 ++index) {
                while (
                    index > consumed.load() + maximumLeadFrames &&
                    !failed.load()) {
                    std::this_thread::yield();
                }
                const auto frame = wordFrame(
                    static_cast<std::uint32_t>(index + 1));
                if (!fifo.publish(frame.data(), frame.size())) {
                    failed = true;
                    break;
                }
                produced = index + 1;
            }
        } catch (...) {
            failed = true;
        }
        writerDone = true;
    });

    std::thread capture([&] {
        try {
            std::size_t emptyPolls = 0;
            while (consumed.load() < frameCount &&
                   !failed.load()) {
                const auto output = fifo.read(reader, 4);
                if (output == std::vector<std::uint8_t>(4)) {
                    ++emptyPolls;
                    if (writerDone.load() &&
                        emptyPolls > 1'000'000) {
                        failed = true;
                    }
                    std::this_thread::yield();
                    continue;
                }
                emptyPolls = 0;
                const auto expected = wordFrame(
                    static_cast<std::uint32_t>(
                        consumed.load() + 1));
                if (!std::equal(
                        output.begin(), output.end(),
                        expected.begin())) {
                    failed = true;
                    break;
                }
                ++consumed;
            }
        } catch (...) {
            failed = true;
        }
    });

    writer.join();
    capture.join();
    check(
        !failed.load() &&
        produced.load() == frameCount &&
        consumed.load() == frameCount,
        "one-writer/one-reader stress lost or corrupted a frame");
}

void runReaderRecreationStress()
{
    VirtualAudioRingBufferModel fifo(4096, 4, 32);
    std::atomic<bool> stop{};
    std::atomic<bool> failed{};
    std::atomic<std::uint32_t> next{1};

    std::thread writer([&] {
        try {
            while (!stop.load()) {
                const auto frame = wordFrame(next.fetch_add(1));
                if (!fifo.publish(frame.data(), frame.size())) {
                    failed = true;
                    break;
                }
                std::this_thread::yield();
            }
        } catch (...) {
            failed = true;
        }
    });

    for (std::size_t recreation = 0;
         recreation < 1000 && !failed.load();
         ++recreation) {
        VirtualAudioRingBufferModel::Reader reader;
        fifo.read(reader, 4);
        bool received = false;
        for (std::size_t attempt = 0;
             attempt < 100'000 && !received;
             ++attempt) {
            const auto output = fifo.read(reader, 4);
            received =
                output != std::vector<std::uint8_t>(4);
            if (!received) {
                std::this_thread::yield();
            }
        }
        if (!received) {
            failed = true;
        }
    }

    stop = true;
    writer.join();
    check(
        !failed.load(),
        "reader recreation stress stalled or rejected an operation");
}

void runResetAndTeardownStress()
{
    VirtualAudioRingBufferModel fifo(4096, 4, 32);
    VirtualAudioRingBufferModel::Reader reader;
    fifo.read(reader, 4);
    std::atomic<bool> stop{};
    std::atomic<bool> failed{};
    std::atomic<std::size_t> operations{};
    std::atomic<std::size_t> teardownRejections{};

    std::thread writer([&] {
        std::uint32_t value = 1;
        try {
            while (!stop.load()) {
                const auto frame = wordFrame(value++);
                if (!fifo.publish(frame.data(), frame.size())) {
                    ++teardownRejections;
                }
                ++operations;
            }
        } catch (...) {
            failed = true;
        }
    });
    std::thread capture([&] {
        try {
            while (!stop.load()) {
                static_cast<void>(fifo.read(reader, 4));
                ++operations;
            }
        } catch (...) {
            failed = true;
        }
    });

    while (operations.load() < 1000 && !failed.load()) {
        std::this_thread::yield();
    }
    for (std::size_t reset = 0; reset < 1000; ++reset) {
        fifo.reset();
    }
    while (operations.load() < 5000 && !failed.load()) {
        std::this_thread::yield();
    }

    // This is the portable equivalent of a destruction request: it is made
    // while reader/writer calls are active, rejects subsequent writes, and
    // returns silence until the operation threads have quiesced.
    fifo.beginTeardown();
    const auto operationsAtTeardown = operations.load();
    while (
        operations.load() < operationsAtTeardown + 1000 &&
        !failed.load()) {
        std::this_thread::yield();
    }
    stop = true;
    writer.join();
    capture.join();

    check(!failed.load(), "reset/teardown stress threw an exception");
    check(
        teardownRejections.load() != 0,
        "teardown must reject active or later writer operations");
    check(
        fifo.read(reader, 4) ==
            std::vector<std::uint8_t>(4),
        "teardown must fail capture safely to silence");
}

} // namespace

void runVirtualAudioFifoExtendedTests()
{
    runDeterministicSignalTests();
    runGeometryAndPolicyTests();
    runOneWriterOneReaderStress();
    runReaderRecreationStress();
    runResetAndTeardownStress();
}
