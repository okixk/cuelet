#pragma once

#include <winrt/Windows.Foundation.h>

#include <atomic>
#include <memory>
#include <string_view>

namespace cuelet::windows {

struct VirtualAudioInstallerClient {
    static winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> runAsync(
        std::wstring_view operation,
        bool elevate,
        std::shared_ptr<std::atomic_bool> const& cancellation = {});
};

} // namespace cuelet::windows
