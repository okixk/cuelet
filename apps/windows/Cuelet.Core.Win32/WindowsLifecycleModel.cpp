#include "WindowsLifecycleModel.h"

namespace cuelet::windows {

ShutdownDecision ShutdownCoordinator::request(
    ShutdownReason reason, bool keepRunningInBackground) noexcept
{
    if (m_state == ShutdownState::Stopped) return ShutdownDecision::AlreadyStopped;
    if (m_state == ShutdownState::ShuttingDown) return ShutdownDecision::AlreadyShuttingDown;
    const bool finalExit = reason != ShutdownReason::WindowClose || !keepRunningInBackground;
    m_reason = reason;
    if (!finalExit) {
        m_state = ShutdownState::HidingWindow;
        return ShutdownDecision::HideWindow;
    }
    m_state = ShutdownState::ShuttingDown;
    ++m_generation;
    return ShutdownDecision::BeginFinalShutdown;
}

void ShutdownCoordinator::windowShown() noexcept
{
    if (m_state == ShutdownState::HidingWindow) m_state = ShutdownState::Running;
}

void ShutdownCoordinator::stopped() noexcept
{
    if (m_state != ShutdownState::Stopped) {
        m_state = ShutdownState::Stopped;
        ++m_generation;
    }
}

bool ShutdownCoordinator::acceptsUiWork(std::uint64_t generation) const noexcept
{
    return generation == m_generation &&
           (m_state == ShutdownState::Running || m_state == ShutdownState::HidingWindow);
}

} // namespace cuelet::windows
