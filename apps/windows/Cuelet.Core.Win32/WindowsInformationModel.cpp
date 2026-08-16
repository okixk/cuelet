#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WindowsInformationModel.h"

#include <windows.h>

#include <utility>
#include <vector>

#pragma comment(lib, "version.lib")

namespace cuelet::windows {

AboutInformation aboutInformation(std::wstring version)
{
    return {
        L"Cuelet",
        L"Cuelet contributors",
        std::move(version),
        L"A cross-platform soundboard and virtual microphone.",
        L"Cuelet is free and open-source software licensed under the GNU Affero General Public License version 3 only.",
        L"AGPL-3.0-only",
        L"https://github.com/okixk/cuelet",
        L"https://github.com/okixk/cuelet/issues",
    };
}

std::wstring applicationVersionFromFile(const std::filesystem::path& executable)
{
    DWORD ignored = 0;
    const auto size = ::GetFileVersionInfoSizeW(executable.c_str(), &ignored);
    if (size == 0) return {};

    std::vector<unsigned char> data(size);
    if (!::GetFileVersionInfoW(executable.c_str(), 0, size, data.data())) return {};

    VS_FIXEDFILEINFO* information = nullptr;
    UINT informationSize = 0;
    if (!::VerQueryValueW(
            data.data(), L"\\", reinterpret_cast<void**>(&information),
            &informationSize) ||
        !information || informationSize < sizeof(VS_FIXEDFILEINFO) ||
        information->dwSignature != 0xfeef04bd) {
        return {};
    }

    return std::to_wstring(HIWORD(information->dwProductVersionMS)) + L"." +
        std::to_wstring(LOWORD(information->dwProductVersionMS)) + L"." +
        std::to_wstring(HIWORD(information->dwProductVersionLS));
}

std::wstring applicationVersionFromCurrentModule()
{
    std::vector<wchar_t> path(32768);
    const auto length = ::GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    return applicationVersionFromFile(std::filesystem::path(
        std::wstring(path.data(), length)));
}

} // namespace cuelet::windows
