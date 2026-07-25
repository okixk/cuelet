#pragma once

#include <cstdint>

namespace cuelet::windows {

enum class ShutdownState {
    Running,
    HidingWindow,
    ShuttingDown,
    Stopped,
};

enum class ShutdownReason {
    WindowClose,
    TrayExit,
    ApplicationExit,
    DestructorFallback,
};

enum class ShutdownDecision {
    HideWindow,
    BeginFinalShutdown,
    AlreadyShuttingDown,
    AlreadyStopped,
};

class ShutdownCoordinator {
public:
    ShutdownDecision request(ShutdownReason reason, bool keepRunningInBackground) noexcept;
    void windowShown() noexcept;
    void stopped() noexcept;

    ShutdownState state() const noexcept { return m_state; }
    ShutdownReason reason() const noexcept { return m_reason; }
    std::uint64_t generation() const noexcept { return m_generation; }
    bool acceptsUiWork(std::uint64_t generation) const noexcept;

private:
    ShutdownState m_state = ShutdownState::Running;
    ShutdownReason m_reason = ShutdownReason::WindowClose;
    std::uint64_t m_generation = 1;
};

} // namespace cuelet::windows
