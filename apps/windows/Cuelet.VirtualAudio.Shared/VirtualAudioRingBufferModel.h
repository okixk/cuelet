#pragma once

#include "CueletAudioFifoCore.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace cuelet::windows {

// Thread-safe user-mode storage wrapper around the exact arithmetic core used
// by the kernel bridge. The wrapper owns memory and synchronization; all FIFO,
// generation, startup, underflow, overflow, and wrap decisions remain in
// CueletAudioFifoCore.h.
class VirtualAudioRingBufferModel {
public:
    struct Reader {
        audio_fifo::ReaderCursor cursor{};
    };

    VirtualAudioRingBufferModel(
        std::size_t capacityBytes,
        std::size_t blockAlign,
        std::size_t targetBufferBytes)
    {
        if (!m_fifo.Configure(
                {toUint64(capacityBytes)},
                {toUint64(blockAlign)},
                {toUint64(targetBufferBytes)})) {
            throw std::invalid_argument(
                "The virtual-audio ring geometry is invalid.");
        }
        m_ring.resize(static_cast<std::size_t>(
            m_fifo.CurrentGeometry().capacityBytes.value));
    }

    void reset()
    {
        std::lock_guard lock(m_mutex);
        std::fill(m_ring.begin(), m_ring.end(), std::uint8_t{});
        m_fifo.Reset();
    }

    void beginTeardown() noexcept
    {
        std::lock_guard lock(m_mutex);
        m_fifo.BeginTeardown();
        std::fill(m_ring.begin(), m_ring.end(), std::uint8_t{});
    }

    bool publish(
        const std::uint8_t* bytes,
        std::size_t byteCount)
    {
        if (bytes == nullptr || byteCount == 0) {
            throw std::invalid_argument(
                "A publish operation requires audio bytes.");
        }

        std::lock_guard lock(m_mutex);
        const auto plan =
            m_fifo.PlanPublish({toUint64(byteCount)});
        if (plan.status ==
            audio_fifo::TransferStatus::InvalidArgument) {
            throw std::invalid_argument(
                "Virtual-audio writes must contain complete frames.");
        }
        if (plan.status !=
            audio_fifo::TransferStatus::Ready) {
            return false;
        }
        if (!audio_fifo::IsCopyPlanValid(
                plan, {toUint64(byteCount)},
                m_fifo.CurrentGeometry())) {
            throw std::runtime_error(
                "The FIFO produced an invalid publish copy plan.");
        }

        const auto sourceOffset =
            checkedSize(plan.sourceOffset.value);
        copyIn(plan, bytes + sourceOffset);
        return true;
    }

    bool publish(std::vector<std::uint8_t> const& bytes)
    {
        if (bytes.empty()) {
            return true;
        }
        return publish(bytes.data(), bytes.size());
    }

    std::vector<std::uint8_t> read(
        Reader& reader,
        std::size_t byteCount)
    {
        if (byteCount == 0) {
            throw std::invalid_argument(
                "A read operation requires complete frames.");
        }

        std::lock_guard lock(m_mutex);
        const auto plan =
            m_fifo.PlanRead(
                reader.cursor, {toUint64(byteCount)});
        if (plan.status ==
            audio_fifo::TransferStatus::InvalidArgument) {
            throw std::invalid_argument(
                "Virtual-audio reads must contain complete frames "
                "and fit in the ring.");
        }
        std::vector<std::uint8_t> result(byteCount);
        if (plan.status ==
            audio_fifo::TransferStatus::Ready) {
            if (!audio_fifo::IsCopyPlanValid(
                    plan, {toUint64(byteCount)},
                    m_fifo.CurrentGeometry())) {
                throw std::runtime_error(
                    "The FIFO produced an invalid read copy plan.");
            }
            copyOut(plan, result.data());
        }
        return result;
    }

    std::uint64_t writeSequence() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_fifo.WritePosition().value;
    }

    std::uint64_t generation() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_fifo.CurrentGeneration().value;
    }

    std::size_t capacity() const noexcept
    {
        return m_ring.size();
    }

#ifdef CUELET_AUDIO_FIFO_TESTING
    bool forceWritePositionForTesting(
        std::uint64_t position) noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_fifo.ForceWritePositionForTesting({position});
    }
#endif

private:
    static std::uint64_t toUint64(std::size_t value)
    {
        if constexpr (
            sizeof(std::size_t) > sizeof(std::uint64_t)) {
            if (value >
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint64_t>::max)())) {
                throw std::overflow_error(
                    "A byte count exceeds the FIFO cursor width.");
            }
        }
        return static_cast<std::uint64_t>(value);
    }

    static std::size_t checkedSize(std::uint64_t value)
    {
        if (value >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
            throw std::overflow_error(
                "A FIFO copy size exceeds the process address width.");
        }
        return static_cast<std::size_t>(value);
    }

    void copyIn(
        audio_fifo::CopyPlan const& plan,
        const std::uint8_t* source)
    {
        const auto offset = checkedSize(plan.ringOffset.value);
        const auto first = checkedSize(plan.firstBytes.value);
        const auto second = checkedSize(plan.secondBytes.value);
        if (offset >= m_ring.size() ||
            first > m_ring.size() - offset ||
            second > m_ring.size()) {
            throw std::out_of_range(
                "A FIFO publish copy exceeded ring storage.");
        }
        std::copy_n(source, first, m_ring.begin() + offset);
        if (second != 0) {
            std::copy_n(
                source + first, second, m_ring.begin());
        }
    }

    void copyOut(
        audio_fifo::CopyPlan const& plan,
        std::uint8_t* destination) const
    {
        const auto offset = checkedSize(plan.ringOffset.value);
        const auto first = checkedSize(plan.firstBytes.value);
        const auto second = checkedSize(plan.secondBytes.value);
        if (offset >= m_ring.size() ||
            first > m_ring.size() - offset ||
            second > m_ring.size()) {
            throw std::out_of_range(
                "A FIFO read copy exceeded ring storage.");
        }
        std::copy_n(
            m_ring.begin() + offset, first, destination);
        if (second != 0) {
            std::copy_n(
                m_ring.begin(), second, destination + first);
        }
    }

    mutable std::mutex m_mutex;
    audio_fifo::FifoCore m_fifo{};
    std::vector<std::uint8_t> m_ring;
};

} // namespace cuelet::windows
