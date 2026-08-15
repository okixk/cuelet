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

std::vector<HelpSection> helpSections()
{
    return {
        {
            L"Getting Started",
            L"1. Choose Create a New Library or Use an Existing Library.\n"
            L"2. Select Import to add supported sound files.\n"
            L"3. Organize sounds with categories and Favorites.\n"
            L"4. Select Play or double-click a sound to play it.\n"
            L"5. Right-click a sound and choose Change Shortcut… when you want a shortcut.",
        },
        {
            L"Virtual Microphone",
            L"Cuelet uses VB-CABLE for virtual microphone routing. Cuelet does not redistribute or install VB-CABLE automatically. Install it from the official VB-Audio website; administrator permission and a Windows restart may be required.\n\n"
            L"After installation or restart, reopen Cuelet. It automatically detects the standard CABLE Input (VB-Audio Virtual Cable) and CABLE Output (VB-Audio Virtual Cable) pair. Additional endpoints such as CABLE In 16ch are excluded from the normal pair. In Settings > Audio routing, verify that the virtual microphone reports Connected.",
            L"Open the official VB-CABLE website",
            L"https://vb-audio.com/Cable/",
        },
        {
            L"Global Shortcuts",
            L"Right-click a sound and choose Change Shortcut… to assign a global shortcut. Assigned shortcuts work while Cuelet is running, including while its window is hidden or in the notification area. Cuelet rejects unsafe or Windows-reserved combinations and reports conflicts. Review or clear assignments in Settings > Keyboard.",
        },
        {
            L"Troubleshooting",
            L"Virtual microphone not connected\n"
            L"• Confirm that VB-CABLE is installed.\n"
            L"• Restart Windows if its installer requested it.\n"
            L"• Restart Cuelet, then check Settings > Audio routing.\n\n"
            L"Missing sound file\n"
            L"• For a linked sound, right-click it and choose Locate Source File…\n"
            L"• For a managed sound, restore the file to the library and select Rescan library.\n\n"
            L"No sound\n"
            L"• Check Play through speakers/headphones and the selected device in Settings > Audio routing.\n"
            L"• Confirm that the sound file is still available.\n"
            L"• Review the current Cuelet audio settings.",
        },
        {
            L"Command-line Help",
            L"After a normal Windows installation, open PowerShell or Command Prompt and run cuelet --help to see the supported commands.",
        },
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
