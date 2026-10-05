#include "WindowsMicrophoneNoiseSuppressor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cuelet::windows {

namespace {

float safeSample(float sample) noexcept
{
    return std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0.0f;
}

std::size_t checkedFrameSize(MicrophoneFrameProcessor const& processor)
{
    const auto size = processor.frameSize();
    if (size == 0 || size > 4096) {
        throw std::invalid_argument("invalid microphone DSP frame size");
    }
    return size;
}

} // namespace

MicrophoneNoiseSuppressor::MicrophoneNoiseSuppressor(MicrophoneFrameProcessor& processor)
    : m_processor(processor),
      m_input(checkedFrameSize(processor)),
      m_output(m_input.size())
{
    m_outputPosition = m_output.size();
}

void MicrophoneNoiseSuppressor::setEnabled(bool enabled) noexcept
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    reset();
}

void MicrophoneNoiseSuppressor::reset() noexcept
{
    m_processor.reset();
    m_inputCount = 0;
    m_outputPosition = m_output.size();
    std::fill(m_input.begin(), m_input.end(), 0.0f);
    std::fill(m_output.begin(), m_output.end(), 0.0f);
}

void MicrophoneNoiseSuppressor::process(
    float const* input, float* output, std::size_t sampleCount) noexcept
{
    if (!m_enabled) {
        std::copy_n(input, sampleCount, output);
        return;
    }

    for (std::size_t index = 0; index < sampleCount; ++index) {
        m_input[m_inputCount++] = safeSample(input[index]);
        if (m_inputCount == m_input.size()) {
            m_processor.processFrame(m_input.data(), m_output.data());
            m_inputCount = 0;
            m_outputPosition = 0;
        }
        output[index] = m_outputPosition < m_output.size()
            ? safeSample(m_output[m_outputPosition++]) : 0.0f;
    }
}

std::size_t MicrophoneNoiseSuppressor::bufferedSamples() const noexcept
{
    return m_inputCount + m_output.size() - m_outputPosition;
}

} // namespace cuelet::windows
