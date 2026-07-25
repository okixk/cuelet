#pragma once

#include <guiddef.h>
#include <devpropdef.h>

namespace cuelet::virtual_audio {

inline constexpr wchar_t providerName[] = L"Cuelet";
inline constexpr wchar_t hardwareId[] = L"ROOT\\CUELETVIRTUALAUDIO";
inline constexpr wchar_t driverInfName[] = L"CueletVirtualAudio.inf";
inline constexpr wchar_t driverServiceName[] = L"cuelet_virtual_audio";
inline constexpr wchar_t driverBinaryName[] = L"CueletVirtualAudio.sys";
inline constexpr wchar_t renderEndpointName[] = L"Cuelet Virtual Microphone Input";
inline constexpr wchar_t captureEndpointName[] = L"Cuelet Virtual Microphone";
inline constexpr wchar_t pairingId[] = L"{8B9D3BB9-8C4E-4EF5-94D5-4BE741D4D892}";
inline constexpr wchar_t containerId[] = L"{262B6214-5A42-4C36-99C5-3C3EA38AB238}";

// Published by the driver/INF and queried by Cuelet in addition to the parent
// hardware ID. Display names are deliberately not the ownership boundary.
inline constexpr DEVPROPKEY pairingProperty{
    {0x1a7b44f5, 0x2c93, 0x48f5, {0xa1, 0x8b, 0x46, 0x39, 0x9d, 0x69, 0xe1, 0x3f}},
    2};

} // namespace cuelet::virtual_audio
