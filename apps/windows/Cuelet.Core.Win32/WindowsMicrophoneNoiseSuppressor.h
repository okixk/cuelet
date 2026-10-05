#pragma once

#include <cstddef>
#include <vector>

namespace cuelet::windows {

// The processor accepts normalized, mono, 48 kHz samples. An RNNoise adapter
// must obtain frameSize() from rnnoise_get_frame_size() at initialization.
class MicrophoneFrameProcessor {
public:
    virtual ~MicrophoneFrameProcessor() = default;
    virtual std::size_t frameSize() const noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void processFrame(float const* input, float* output) noexcept = 0;
};

// Preallocated adapter from arbitrary graph quantum sizes to fixed DSP frames.
// Only physical microphone samples belong here; soundboard and endpoint audio
// must continue through their existing paths.
class MicrophoneNoiseSuppressor final {
public:
    explicit MicrophoneNoiseSuppressor(MicrophoneFrameProcessor& processor);

    bool enabled() const noexcept { return m_enabled; }
    void setEnabled(bool enabled) noexcept;
    void reset() noexcept;
    void process(float const* input, float* output, std::size_t sampleCount) noexcept;
    std::size_t bufferedSamples() const noexcept;
    std::size_t latencySamples() const noexcept { return m_input.size() - 1; }

private:
    MicrophoneFrameProcessor& m_processor;
    std::vector<float> m_input;
    std::vector<float> m_output;
    std::size_t m_inputCount = 0;
    std::size_t m_outputPosition = 0;
    bool m_enabled = false;
};

} // namespace cuelet::windows
