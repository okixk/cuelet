#pragma once

#if defined(_NTDDK_)
using CueletFifoUInt64 = ULONGLONG;
using CueletFifoUInt8 = UCHAR;
#define CUELET_FIFO_INLINE __forceinline
#else
#include <cstdint>
using CueletFifoUInt64 = std::uint64_t;
using CueletFifoUInt8 = std::uint8_t;
#define CUELET_FIFO_INLINE inline
#endif

constexpr CueletFifoUInt64 CueletFifoUInt64Maximum =
    ~static_cast<CueletFifoUInt64>(0);

namespace cuelet::audio_fifo
{
    struct Bytes
    {
        CueletFifoUInt64 value{};
    };

    struct Frames
    {
        CueletFifoUInt64 value{};
    };

    struct BytePosition
    {
        CueletFifoUInt64 value{};
    };

    struct Generation
    {
        CueletFifoUInt64 value{};
    };

    enum class TransferStatus : CueletFifoUInt8
    {
        Ready,
        InvalidArgument,
        NotConfigured,
        TeardownInProgress,
        StartupReserve,
        Underflow,
        ReaderResynchronized
    };

    struct Geometry
    {
        Bytes frameBytes{};
        Bytes capacityBytes{};
        Bytes baseStartupReserveBytes{};
    };

    struct ReaderCursor
    {
        BytePosition position{};
        Generation generation{};
        bool initialized{};
        bool started{};
    };

    struct CopyPlan
    {
        TransferStatus status{TransferStatus::InvalidArgument};
        Bytes sourceOffset{};
        Bytes ringOffset{};
        Bytes firstBytes{};
        Bytes secondBytes{};
        bool overflowRecovered{};
    };

    struct DisplacementPlan
    {
        Bytes byteCount{};
        CueletFifoUInt64 hundredNanosecondCarry{};
        CueletFifoUInt64 thousandthByteCarry{};
        bool valid{};
        bool clamped{};
    };

    CUELET_FIFO_INLINE bool IsFrameAligned(
        Bytes byteCount,
        Bytes frameBytes) noexcept;

    CUELET_FIFO_INLINE bool IsCopyPlanValid(
        const CopyPlan& plan,
        Bytes transferBytes,
        const Geometry& geometry) noexcept
    {
        if (plan.status != TransferStatus::Ready ||
            transferBytes.value == 0 ||
            geometry.frameBytes.value == 0 ||
            geometry.capacityBytes.value == 0 ||
            plan.ringOffset.value >= geometry.capacityBytes.value ||
            plan.sourceOffset.value > transferBytes.value ||
            !IsFrameAligned(transferBytes, geometry.frameBytes) ||
            !IsFrameAligned(plan.sourceOffset, geometry.frameBytes) ||
            !IsFrameAligned(plan.ringOffset, geometry.frameBytes) ||
            !IsFrameAligned(plan.firstBytes, geometry.frameBytes) ||
            !IsFrameAligned(plan.secondBytes, geometry.frameBytes))
        {
            return false;
        }

        const CueletFifoUInt64 bytesBeforeWrap =
            geometry.capacityBytes.value - plan.ringOffset.value;
        if (plan.firstBytes.value == 0 ||
            plan.firstBytes.value > bytesBeforeWrap ||
            (plan.secondBytes.value != 0 &&
                plan.firstBytes.value != bytesBeforeWrap) ||
            plan.secondBytes.value > plan.ringOffset.value ||
            plan.firstBytes.value >
                CueletFifoUInt64Maximum - plan.secondBytes.value)
        {
            return false;
        }

        const CueletFifoUInt64 copiedBytes =
            plan.firstBytes.value + plan.secondBytes.value;
        return copiedBytes <= geometry.capacityBytes.value &&
            copiedBytes ==
                transferBytes.value - plan.sourceOffset.value;
    }

    CUELET_FIFO_INLINE bool TryFramesToBytes(
        Frames frames,
        Bytes frameBytes,
        Bytes* result) noexcept
    {
        if (result == nullptr || frameBytes.value == 0 ||
            frames.value >
                CueletFifoUInt64Maximum / frameBytes.value)
        {
            return false;
        }

        result->value = frames.value * frameBytes.value;
        return true;
    }

    CUELET_FIFO_INLINE bool IsFrameAligned(
        Bytes byteCount,
        Bytes frameBytes) noexcept
    {
        return frameBytes.value != 0 &&
            byteCount.value % frameBytes.value == 0;
    }

    CUELET_FIFO_INLINE Bytes AlignDown(
        Bytes byteCount,
        Bytes frameBytes) noexcept
    {
        if (frameBytes.value == 0)
        {
            return {};
        }

        return {
            byteCount.value -
                byteCount.value % frameBytes.value};
    }

    CUELET_FIFO_INLINE DisplacementPlan PlanByteDisplacement(
        Bytes averageBytesPerSecond,
        CueletFifoUInt64 elapsedHundredNanoseconds,
        CueletFifoUInt64 hundredNanosecondCarry,
        CueletFifoUInt64 thousandthByteCarry,
        Bytes maximumBytes,
        Bytes frameBytes) noexcept
    {
        DisplacementPlan result{};
        const Bytes alignedMaximum =
            AlignDown(maximumBytes, frameBytes);
        if (averageBytesPerSecond.value == 0 ||
            frameBytes.value == 0 ||
            alignedMaximum.value == 0 ||
            frameBytes.value > CueletFifoUInt64Maximum / 1000 ||
            thousandthByteCarry >= frameBytes.value * 1000)
        {
            return result;
        }

        CueletFifoUInt64 totalHundredNanoseconds{};
        if (elapsedHundredNanoseconds >
            CueletFifoUInt64Maximum - hundredNanosecondCarry)
        {
            totalHundredNanoseconds = CueletFifoUInt64Maximum;
        }
        else
        {
            totalHundredNanoseconds =
                elapsedHundredNanoseconds +
                hundredNanosecondCarry;
        }
        const CueletFifoUInt64 elapsedMilliseconds =
            totalHundredNanoseconds / 10000;
        result.hundredNanosecondCarry =
            totalHundredNanoseconds % 10000;

        // One circular DMA buffer is the maximum useful catch-up. More work
        // would duplicate already-overwritten frames while holding a stream
        // spin lock, particularly after sleep/resume or a long scheduler stall.
        if (alignedMaximum.value > CueletFifoUInt64Maximum / 1000)
        {
            return result;
        }
        const CueletFifoUInt64 maximumThousandthBytes =
            alignedMaximum.value * 1000;
        if (thousandthByteCarry > maximumThousandthBytes ||
            elapsedMilliseconds >
                (maximumThousandthBytes - thousandthByteCarry) /
                    averageBytesPerSecond.value)
        {
            result.byteCount = alignedMaximum;
            result.thousandthByteCarry = 0;
            result.valid = true;
            result.clamped = true;
            return result;
        }

        const CueletFifoUInt64 totalThousandthBytes =
            elapsedMilliseconds * averageBytesPerSecond.value +
                thousandthByteCarry;
        const Bytes completeBytes{
            totalThousandthBytes / 1000};
        result.byteCount = AlignDown(completeBytes, frameBytes);
        result.thousandthByteCarry =
            totalThousandthBytes - result.byteCount.value * 1000;
        result.valid = true;
        return result;
    }

    CUELET_FIFO_INLINE CueletFifoUInt64 Minimum(
        CueletFifoUInt64 left,
        CueletFifoUInt64 right) noexcept
    {
        return left < right ? left : right;
    }

    CUELET_FIFO_INLINE CueletFifoUInt64 Maximum(
        CueletFifoUInt64 left,
        CueletFifoUInt64 right) noexcept
    {
        return left > right ? left : right;
    }

    class FifoCore final
    {
    public:
        CUELET_FIFO_INLINE bool Configure(
            Bytes storageBytes,
            Bytes frameBytes,
            Bytes requestedStartupReserveBytes) noexcept
        {
            if (frameBytes.value == 0)
            {
                return false;
            }

            const Bytes capacity =
                AlignDown(storageBytes, frameBytes);
            Bytes fourFrames{};
            if (!TryFramesToBytes(
                    Frames{4}, frameBytes, &fourFrames) ||
                capacity.value < fourFrames.value)
            {
                return false;
            }

            const Bytes maximumReserve = AlignDown(
                Bytes{capacity.value / 4}, frameBytes);
            const Bytes requestedReserve = AlignDown(
                requestedStartupReserveBytes, frameBytes);
            const CueletFifoUInt64 reserve = Minimum(
                maximumReserve.value,
                Maximum(requestedReserve.value, frameBytes.value));

            m_geometry = {
                frameBytes,
                capacity,
                Bytes{reserve}};
            m_writePosition = {};
            AdvanceGeneration();
            m_configured = true;
            m_teardown = false;
            return true;
        }

        CUELET_FIFO_INLINE void Reset() noexcept
        {
            m_writePosition = {};
            AdvanceGeneration();
        }

        CUELET_FIFO_INLINE void BeginTeardown() noexcept
        {
            if (m_teardown)
            {
                return;
            }
            m_teardown = true;
            m_writePosition = {};
            AdvanceGeneration();
        }

        CUELET_FIFO_INLINE CopyPlan PlanPublish(Bytes byteCount) noexcept
        {
            CopyPlan plan{};
            plan.status = ValidateTransfer(byteCount);
            if (plan.status != TransferStatus::Ready)
            {
                return plan;
            }

            // Do not permit an absolute byte cursor to wrap. A generation
            // change safely invalidates every reader instead of relying on
            // unsigned subtraction or capacity-dependent wrap semantics.
            if (byteCount.value >
                CueletFifoUInt64Maximum -
                    m_writePosition.value)
            {
                Reset();
            }

            const CueletFifoUInt64 retained = Minimum(
                byteCount.value,
                m_geometry.capacityBytes.value);
            const CueletFifoUInt64 skipped =
                byteCount.value - retained;
            const CueletFifoUInt64 copyPosition =
                m_writePosition.value + skipped;
            const CueletFifoUInt64 ringOffset =
                copyPosition % m_geometry.capacityBytes.value;
            const CueletFifoUInt64 first = Minimum(
                retained,
                m_geometry.capacityBytes.value - ringOffset);

            plan.status = TransferStatus::Ready;
            plan.sourceOffset = Bytes{skipped};
            plan.ringOffset = Bytes{ringOffset};
            plan.firstBytes = Bytes{first};
            plan.secondBytes = Bytes{retained - first};
            m_writePosition.value += byteCount.value;
            return plan;
        }

        CUELET_FIFO_INLINE CopyPlan PlanRead(
            ReaderCursor& reader,
            Bytes byteCount) const noexcept
        {
            CopyPlan plan{};
            plan.status = ValidateTransfer(byteCount);
            if (plan.status != TransferStatus::Ready)
            {
                return plan;
            }
            if (byteCount.value > m_geometry.capacityBytes.value)
            {
                plan.status = TransferStatus::InvalidArgument;
                return plan;
            }

            if (!reader.initialized ||
                reader.generation.value != m_generation.value ||
                reader.position.value > m_writePosition.value)
            {
                reader.position = m_writePosition;
                reader.generation = m_generation;
                reader.initialized = true;
                reader.started = false;
                plan.status = TransferStatus::ReaderResynchronized;
                return plan;
            }

            const CueletFifoUInt64 target =
                StartupReserveFor(byteCount);
            CueletFifoUInt64 available =
                m_writePosition.value - reader.position.value;
            if (available > m_geometry.capacityBytes.value)
            {
                reader.position.value =
                    m_writePosition.value > target
                        ? m_writePosition.value - target
                        : 0;
                reader.position.value =
                    AlignDown(
                        Bytes{reader.position.value},
                        m_geometry.frameBytes).value;
                available =
                    m_writePosition.value - reader.position.value;
                plan.overflowRecovered = true;
            }

            if (!reader.started)
            {
                if (available < target)
                {
                    plan.status = TransferStatus::StartupReserve;
                    return plan;
                }
                reader.started = true;
            }

            if (available < byteCount.value)
            {
                plan.status = TransferStatus::Underflow;
                return plan;
            }

            const CueletFifoUInt64 ringOffset =
                reader.position.value %
                    m_geometry.capacityBytes.value;
            const CueletFifoUInt64 first = Minimum(
                byteCount.value,
                m_geometry.capacityBytes.value - ringOffset);
            plan.status = TransferStatus::Ready;
            plan.ringOffset = Bytes{ringOffset};
            plan.firstBytes = Bytes{first};
            plan.secondBytes =
                Bytes{byteCount.value - first};
            reader.position.value += byteCount.value;
            return plan;
        }

        CUELET_FIFO_INLINE const Geometry& CurrentGeometry() const noexcept
        {
            return m_geometry;
        }

        CUELET_FIFO_INLINE BytePosition WritePosition() const noexcept
        {
            return m_writePosition;
        }

        CUELET_FIFO_INLINE Generation CurrentGeneration() const noexcept
        {
            return m_generation;
        }

        CUELET_FIFO_INLINE bool IsConfigured() const noexcept
        {
            return m_configured;
        }

        CUELET_FIFO_INLINE bool IsTearingDown() const noexcept
        {
            return m_teardown;
        }

#ifdef CUELET_AUDIO_FIFO_TESTING
        CUELET_FIFO_INLINE bool ForceWritePositionForTesting(
            BytePosition position) noexcept
        {
            if (!m_configured ||
                !IsFrameAligned(
                    Bytes{position.value},
                    m_geometry.frameBytes))
            {
                return false;
            }

            m_writePosition = position;
            return true;
        }
#endif

    private:
        CUELET_FIFO_INLINE TransferStatus ValidateTransfer(
            Bytes byteCount) const noexcept
        {
            if (!m_configured)
            {
                return TransferStatus::NotConfigured;
            }
            if (m_teardown)
            {
                return TransferStatus::TeardownInProgress;
            }
            if (byteCount.value == 0 ||
                !IsFrameAligned(
                    byteCount, m_geometry.frameBytes))
            {
                return TransferStatus::InvalidArgument;
            }
            return TransferStatus::Ready;
        }

        CUELET_FIFO_INLINE CueletFifoUInt64 StartupReserveFor(
            Bytes requestBytes) const noexcept
        {
            const CueletFifoUInt64 twoRequests =
                requestBytes.value >
                    CueletFifoUInt64Maximum / 2
                    ? CueletFifoUInt64Maximum
                    : requestBytes.value * 2;
            const CueletFifoUInt64 requested = Maximum(
                m_geometry.baseStartupReserveBytes.value,
                twoRequests);
            const Bytes maximum = AlignDown(
                Bytes{m_geometry.capacityBytes.value / 4},
                m_geometry.frameBytes);
            return Minimum(requested, maximum.value);
        }

        CUELET_FIFO_INLINE void AdvanceGeneration() noexcept
        {
            ++m_generation.value;
            if (m_generation.value == 0)
            {
                m_generation.value = 1;
            }
        }

        Geometry m_geometry{};
        BytePosition m_writePosition{};
        Generation m_generation{};
        bool m_configured{};
        bool m_teardown{};
    };
}

#undef CUELET_FIFO_INLINE
