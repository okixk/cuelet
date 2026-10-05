#include "WindowsMicrophoneNoiseSuppressor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, char const* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TestFrameProcessor final : public cuelet::windows::MicrophoneFrameProcessor {
public:
    std::size_t frameSize() const noexcept override { return size; }
    void reset() noexcept override { ++resets; }
    void processFrame(float const* input, float* output) noexcept override
    {
        ++frames;
        for (std::size_t index = 0; index < frameSize(); ++index) {
            output[index] = invalidOutput
                ? std::numeric_limits<float>::quiet_NaN()
                : input[index] * gain;
        }
    }

    int frames = 0;
    int resets = 0;
    bool invalidOutput = false;
    float gain = 0.5f;
    std::size_t size = 4;
};

} // namespace

void runMicrophoneNoiseSuppressorTests()
{
    TestFrameProcessor processor;
    cuelet::windows::MicrophoneNoiseSuppressor suppressor(processor);
    float input[] = {0.2f, -0.4f, 0.6f, -0.8f};
    float output[4]{};

    require(!suppressor.enabled(), "noise suppression must default off");
    suppressor.process(input, output, 4);
    require(std::equal(std::begin(input), std::end(input), std::begin(output)),
            "disabled microphone suppression must be an exact bypass");
    require(processor.frames == 0 && suppressor.bufferedSamples() == 0,
            "disabled suppression must not buffer or process audio");

    suppressor.setEnabled(true);
    require(suppressor.enabled() && processor.resets == 1,
            "enabling must reset processor state");
    suppressor.process(input, output, 2);
    require(output[0] == 0.0f && output[1] == 0.0f &&
                processor.frames == 0 && suppressor.bufferedSamples() == 2,
            "short input blocks must accumulate one fixed frame");
    suppressor.process(input + 2, output + 2, 2);
    require(processor.frames == 1 && output[2] == 0.0f && output[3] == 0.1f,
            "the completed frame must enter output with bounded latency");

    float next[] = {0.4f, 0.2f, -0.6f, -0.4f, 1.0f};
    float nextOutput[5]{};
    suppressor.process(next, nextOutput, 5);
    require(processor.frames == 2 && nextOutput[0] == -0.2f &&
                nextOutput[1] == 0.3f && nextOutput[2] == -0.4f &&
                nextOutput[3] == 0.2f && nextOutput[4] == 0.1f,
            "arbitrary block boundaries must retain frame order without drift");
    require(suppressor.bufferedSamples() <= processor.frameSize() &&
                suppressor.latencySamples() == processor.frameSize() - 1,
            "frame accumulation and latency must stay bounded");

    suppressor.setEnabled(false);
    require(!suppressor.enabled() && processor.resets == 2 &&
                suppressor.bufferedSamples() == 0,
            "disabling must discard pending audio and reset processor state");
    suppressor.process(next, nextOutput, 5);
    require(std::equal(std::begin(next), std::end(next), std::begin(nextOutput)),
            "disabling must immediately restore exact bypass");

    suppressor.setEnabled(true);
    float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity(), 2.0f};
    suppressor.process(invalid, output, 4);
    for (float sample : output) {
        require(std::isfinite(sample) && sample >= -1.0f && sample <= 1.0f,
                "processed output must always be finite and clipped to range");
    }
    processor.invalidOutput = true;
    suppressor.process(input, output, 4);
    suppressor.process(input, output, 4);
    for (float sample : output) {
        require(std::isfinite(sample),
                "nonfinite DSP output must never reach the microphone route");
    }
    suppressor.reset();
    require(processor.resets == 4 && suppressor.bufferedSamples() == 0,
            "graph or device restart must clear processor and frame state");

    TestFrameProcessor identity;
    identity.gain = 1.0f;
    cuelet::windows::MicrophoneNoiseSuppressor sustained(identity);
    sustained.setEnabled(true);
    std::vector<float> sequence(1001);
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        sequence[index] = static_cast<float>(index % 101) / 101.0f;
    }
    std::vector<float> delayed(sequence.size());
    std::size_t position = 0;
    for (std::size_t block : {1u, 7u, 3u, 119u, 2u, 301u, 568u}) {
        sustained.process(sequence.data() + position, delayed.data() + position, block);
        position += block;
        require(sustained.bufferedSamples() <= identity.frameSize(),
                "processor buffering must not grow with stream duration");
    }
    require(position == sequence.size(), "test blocks must cover the full stream");
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        const float expected = index < sustained.latencySamples()
            ? 0.0f : sequence[index - sustained.latencySamples()];
        require(delayed[index] == expected,
                "frame adaptation must preserve every sample across uneven blocks");
    }

    sustained.reset();
    sustained.process(sequence.data(), delayed.data(), 2);
    sustained.reset();
    sustained.process(sequence.data() + 2, delayed.data(), 4);
    require(identity.frames == 251 && delayed[3] == sequence[2],
            "reset must discard a partial frame from the previous device");

    TestFrameProcessor invalidSize;
    invalidSize.size = 4097;
    bool rejected = false;
    try {
        cuelet::windows::MicrophoneNoiseSuppressor invalid(invalidSize);
    } catch (std::invalid_argument const&) {
        rejected = true;
    }
    require(rejected, "unbounded processor frame sizes must be rejected before allocation");
}

#ifdef CUELET_MICROPHONE_TEST_MAIN
int main()
{
    runMicrophoneNoiseSuppressorTests();
}
#endif
