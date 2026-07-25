#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <devpkey.h>
#include <devguid.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include "QualitySuite.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t renderEndpointName[] = L"Cuelet Virtual Microphone Input";
constexpr wchar_t captureEndpointName[] = L"Cuelet Virtual Microphone";
constexpr wchar_t cueletHardwareId[] = L"ROOT\\CUELETVIRTUALAUDIO";
inline constexpr GUID audioEndpointClass{
    0xc166523c, 0xfe0c, 0x4a94,
    {0xa5, 0x86, 0xf1, 0xa8, 0x0c, 0xfb, 0xbf, 0x3e}};
constexpr double toneFrequency = 997.0;
constexpr double toneAmplitude = 0.35;
constexpr double pi = 3.1415926535897932384626433832795;

void check(HRESULT result, const char* operation)
{
    if (FAILED(result)) {
        throw std::runtime_error(
            std::string(operation) + " failed with HRESULT 0x" +
            [&] {
                char value[16]{};
                sprintf_s(value, "%08X", static_cast<unsigned>(result));
                return std::string(value);
            }());
    }
}

struct ComLifetime {
    ComLifetime() { check(::CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComLifetime() { ::CoUninitialize(); }
};

std::wstring deviceName(IMMDevice* device)
{
    ComPtr<IPropertyStore> properties;
    check(device->OpenPropertyStore(STGM_READ, &properties), "OpenPropertyStore");
    PROPVARIANT value;
    ::PropVariantInit(&value);
    const auto propertyResult =
        properties->GetValue(PKEY_Device_FriendlyName, &value);
    std::wstring result =
        SUCCEEDED(propertyResult) &&
        value.vt == VT_LPWSTR && value.pwszVal ? value.pwszVal : L"";
    ::PropVariantClear(&value);
    check(propertyResult, "GetValue(PKEY_Device_FriendlyName)");
    return result;
}

std::wstring deviceId(IMMDevice* device)
{
    wchar_t* value = nullptr;
    check(device->GetId(&value), "IMMDevice::GetId");
    std::wstring result = value ? value : L"";
    ::CoTaskMemFree(value);
    return result;
}

std::wstring lowercase(std::wstring value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
    return value;
}

bool startsWithInsensitive(
    std::wstring const& value,
    std::wstring_view prefix)
{
    return lowercase(value).rfind(lowercase(std::wstring(prefix)), 0) == 0;
}

std::wstring audioEndpointInstanceId(std::wstring endpointId)
{
    constexpr std::wstring_view prefix = L"SWD\\MMDEVAPI\\";
    if (!startsWithInsensitive(endpointId, prefix)) {
        endpointId = std::wstring(prefix) + endpointId;
    }
    return endpointId;
}

std::wstring pnpStringProperty(
    std::wstring instanceId,
    DEVPROPKEY const& key)
{
    instanceId = audioEndpointInstanceId(std::move(instanceId));
    const auto devices = ::SetupDiGetClassDevsW(
        &audioEndpointClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return {};
    SP_DEVINFO_DATA data{sizeof(data)};
    std::wstring result;
    if (::SetupDiOpenDeviceInfoW(
            devices, instanceId.c_str(), nullptr, 0, &data)) {
        DEVPROPTYPE type = 0;
        DWORD bytes = 0;
        ::SetupDiGetDevicePropertyW(
            devices, &data, &key, &type, nullptr, 0, &bytes, 0);
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            type == DEVPROP_TYPE_STRING &&
            bytes >= sizeof(wchar_t) &&
            bytes % sizeof(wchar_t) == 0) {
            std::vector<BYTE> buffer(bytes);
            if (::SetupDiGetDevicePropertyW(
                    devices, &data, &key, &type,
                    buffer.data(), bytes, nullptr, 0) &&
                type == DEVPROP_TYPE_STRING) {
                const auto* text =
                    reinterpret_cast<wchar_t const*>(buffer.data());
                const auto characters = bytes / sizeof(wchar_t);
                if (text[characters - 1] == L'\0') result.assign(text);
            }
        }
    }
    ::SetupDiDestroyDeviceInfoList(devices);
    return result;
}

bool multiStringContains(
    std::vector<BYTE> const& buffer,
    std::wstring_view expected)
{
    if (buffer.size() < sizeof(wchar_t) * 2 ||
        buffer.size() % sizeof(wchar_t) != 0) {
        return false;
    }
    const auto* current =
        reinterpret_cast<wchar_t const*>(buffer.data());
    const auto* const end = current + buffer.size() / sizeof(wchar_t);
    while (current < end && *current != L'\0') {
        const auto terminator = std::find(current, end, L'\0');
        if (terminator == end) return false;
        const std::wstring_view value(
            current, static_cast<std::size_t>(terminator - current));
        if (lowercase(std::wstring(value)) ==
            lowercase(std::wstring(expected))) {
            return true;
        }
        current += value.size() + 1;
    }
    return false;
}

bool isActiveCueletRoot(std::wstring const& instanceId)
{
    const auto devices = ::SetupDiGetClassDevsW(
        nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return false;

    bool matches = false;
    SP_DEVINFO_DATA data{sizeof(data)};
    if (::SetupDiOpenDeviceInfoW(
            devices, instanceId.c_str(), nullptr, 0, &data)) {
        DWORD type = 0;
        DWORD bytes = 0;
        ::SetupDiGetDeviceRegistryPropertyW(
            devices, &data, SPDRP_HARDWAREID,
            &type, nullptr, 0, &bytes);
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            type == REG_MULTI_SZ &&
            bytes >= sizeof(wchar_t) * 2) {
            std::vector<BYTE> ids(bytes);
            if (::SetupDiGetDeviceRegistryPropertyW(
                    devices, &data, SPDRP_HARDWAREID,
                    &type, ids.data(), bytes, nullptr) &&
                multiStringContains(ids, cueletHardwareId)) {
                matches = true;
            }
        }
    }
    ::SetupDiDestroyDeviceInfoList(devices);
    return matches;
}

std::wstring cueletParent(IMMDevice* device)
{
    auto parent =
        pnpStringProperty(deviceId(device), DEVPKEY_Device_Parent);
    const auto prefix = std::wstring(cueletHardwareId) + L"\\";
    if (parent.size() <= prefix.size() ||
        !startsWithInsensitive(parent, prefix) ||
        !isActiveCueletRoot(parent)) {
        return {};
    }
    return parent;
}

bool isCueletEndpoint(IMMDevice* device)
{
    return !cueletParent(device).empty();
}

ComPtr<IMMDevice> findEndpoint(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow,
    const wchar_t* expectedRole)
{
    ComPtr<IMMDeviceCollection> devices;
    check(
        enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices),
        "EnumAudioEndpoints");
    UINT count = 0;
    check(devices->GetCount(&count), "IMMDeviceCollection::GetCount");
    ComPtr<IMMDevice> found;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> candidate;
        check(devices->Item(index, &candidate), "IMMDeviceCollection::Item");
        if (isCueletEndpoint(candidate.Get())) {
            if (found) {
                throw std::runtime_error(
                    "More than one active Cuelet endpoint has the expected direction.");
            }
            found = candidate;
        }
    }
    if (!found) {
        std::wstring message = L"Active endpoint not found: ";
        message += expectedRole;
        const auto required = ::WideCharToMultiByte(
            CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
            nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(required), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
            utf8.data(), required, nullptr, nullptr);
        throw std::runtime_error(utf8);
    }
    return found;
}

bool isFloatFormat(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return ::IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

bool isPcmFormat(const WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_PCM) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    return ::IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
}

void validateFormat(const WAVEFORMATEX* format, const char* role)
{
    if (!format || format->nChannels == 0 || format->nSamplesPerSec == 0 ||
        format->nBlockAlign == 0 || (!isFloatFormat(format) && !isPcmFormat(format))) {
        throw std::runtime_error(std::string(role) + " mix format is not supported by the verifier.");
    }
    if (isFloatFormat(format) && format->wBitsPerSample != 32) {
        throw std::runtime_error(std::string(role) + " uses an unsupported floating-point width.");
    }
    if (isPcmFormat(format) &&
        format->wBitsPerSample != 16 &&
        format->wBitsPerSample != 24 &&
        format->wBitsPerSample != 32) {
        throw std::runtime_error(std::string(role) + " uses an unsupported PCM width.");
    }
    if (format->nSamplesPerSec != 48000 || format->nChannels != 2) {
        throw std::runtime_error(
            std::string(role) +
            " must expose Cuelet's expected 48000 Hz stereo format.");
    }
    if (format->nAvgBytesPerSec !=
        format->nSamplesPerSec * format->nBlockAlign) {
        throw std::runtime_error(
            std::string(role) + " reports an inconsistent byte rate.");
    }
}

void writeSample(BYTE* destination, const WAVEFORMATEX* format, double sample)
{
    sample = std::clamp(sample, -1.0, 1.0);
    if (isFloatFormat(format)) {
        *reinterpret_cast<float*>(destination) = static_cast<float>(sample);
    } else if (format->wBitsPerSample == 16) {
        *reinterpret_cast<std::int16_t*>(destination) =
            static_cast<std::int16_t>(std::lround(sample * 32767.0));
    } else if (format->wBitsPerSample == 24) {
        const auto value = static_cast<std::int32_t>(
            std::lround(sample * 8388607.0));
        destination[0] = static_cast<BYTE>(value);
        destination[1] = static_cast<BYTE>(value >> 8);
        destination[2] = static_cast<BYTE>(value >> 16);
    } else {
        *reinterpret_cast<std::int32_t*>(destination) =
            static_cast<std::int32_t>(std::llround(sample * 2147483647.0));
    }
}

double readSample(const BYTE* source, const WAVEFORMATEX* format)
{
    if (isFloatFormat(format)) {
        return std::clamp(
            static_cast<double>(*reinterpret_cast<const float*>(source)),
            -1.0, 1.0);
    }
    if (format->wBitsPerSample == 16) {
        return static_cast<double>(*reinterpret_cast<const std::int16_t*>(source)) /
               32768.0;
    }
    if (format->wBitsPerSample == 24) {
        std::int32_t value =
            static_cast<std::int32_t>(source[0]) |
            (static_cast<std::int32_t>(source[1]) << 8) |
            (static_cast<std::int32_t>(source[2]) << 16);
        if ((value & 0x00800000) != 0) value |= static_cast<std::int32_t>(0xFF000000);
        return static_cast<double>(value) / 8388608.0;
    }
    return static_cast<double>(*reinterpret_cast<const std::int32_t*>(source)) /
           2147483648.0;
}

void fillRenderBuffer(
    BYTE* buffer,
    UINT32 frameCount,
    const WAVEFORMATEX* format,
    std::uint64_t& frameIndex)
{
    const auto bytesPerChannel = format->nBlockAlign / format->nChannels;
    for (UINT32 frame = 0; frame < frameCount; ++frame, ++frameIndex) {
        const auto sample = toneAmplitude * std::sin(
            2.0 * pi * toneFrequency *
            static_cast<double>(frameIndex) /
            static_cast<double>(format->nSamplesPerSec));
        auto frameStart = buffer + static_cast<std::size_t>(frame) * format->nBlockAlign;
        for (WORD channel = 0; channel < format->nChannels; ++channel) {
            writeSample(
                frameStart + static_cast<std::size_t>(channel) * bytesPerChannel,
                format, sample);
        }
    }
}

void appendCapture(
    std::vector<double>& samples,
    const BYTE* buffer,
    UINT32 frameCount,
    DWORD flags,
    const WAVEFORMATEX* format)
{
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || buffer == nullptr) {
        samples.insert(samples.end(), frameCount, 0.0);
        return;
    }
    const auto bytesPerChannel = format->nBlockAlign / format->nChannels;
    for (UINT32 frame = 0; frame < frameCount; ++frame) {
        const auto frameStart =
            buffer + static_cast<std::size_t>(frame) * format->nBlockAlign;
        double mixed = 0.0;
        for (WORD channel = 0; channel < format->nChannels; ++channel) {
            mixed += readSample(
                frameStart + static_cast<std::size_t>(channel) * bytesPerChannel,
                format);
        }
        samples.push_back(mixed / static_cast<double>(format->nChannels));
    }
}

struct WindowMeasurement {
    double rms{};
    double toneRms{};
    double purity{};
    double startSeconds{};
};

WindowMeasurement measureBestWindow(
    const std::vector<double>& samples,
    UINT32 sampleRate)
{
    const auto windowSize = static_cast<std::size_t>(sampleRate);
    const auto step = std::max<std::size_t>(1, sampleRate / 4);
    if (samples.size() < windowSize) {
        throw std::runtime_error("The capture endpoint returned less than one second of audio.");
    }

    WindowMeasurement best;
    for (std::size_t start = 0; start + windowSize <= samples.size(); start += step) {
        double mean = 0.0;
        for (std::size_t index = 0; index < windowSize; ++index) {
            mean += samples[start + index];
        }
        mean /= static_cast<double>(windowSize);

        double power = 0.0;
        double sineProjection = 0.0;
        double cosineProjection = 0.0;
        for (std::size_t index = 0; index < windowSize; ++index) {
            const auto value = samples[start + index] - mean;
            const auto phase =
                2.0 * pi * toneFrequency *
                static_cast<double>(index) /
                static_cast<double>(sampleRate);
            power += value * value;
            sineProjection += value * std::sin(phase);
            cosineProjection += value * std::cos(phase);
        }
        WindowMeasurement candidate;
        candidate.rms = std::sqrt(power / static_cast<double>(windowSize));
        const auto sineAmplitude =
            2.0 * sineProjection / static_cast<double>(windowSize);
        const auto cosineAmplitude =
            2.0 * cosineProjection / static_cast<double>(windowSize);
        candidate.toneRms =
            std::sqrt((sineAmplitude * sineAmplitude +
                       cosineAmplitude * cosineAmplitude) / 2.0);
        candidate.purity =
            candidate.rms > std::numeric_limits<double>::epsilon()
                ? candidate.toneRms / candidate.rms : 0.0;
        candidate.startSeconds =
            static_cast<double>(start) / static_cast<double>(sampleRate);
        if (candidate.toneRms > best.toneRms) best = candidate;
    }
    return best;
}

double observedSignalOnsetSeconds(
    const std::vector<double>& samples,
    UINT32 sampleRate)
{
    const auto windowSize =
        std::max<std::size_t>(1, sampleRate / 100); // 10 ms.
    for (std::size_t start = 0;
         start + windowSize <= samples.size();
         start += windowSize) {
        double power = 0.0;
        for (std::size_t index = 0; index < windowSize; ++index) {
            const auto value = samples[start + index];
            power += value * value;
        }
        const auto rms =
            std::sqrt(power / static_cast<double>(windowSize));
        if (rms >= 0.03) {
            return static_cast<double>(start) /
                   static_cast<double>(sampleRate);
        }
    }
    return std::numeric_limits<double>::infinity();
}

std::wstring formatDescription(const WAVEFORMATEX* format)
{
    return std::to_wstring(format->nSamplesPerSec) + L" Hz, " +
           std::to_wstring(format->wBitsPerSample) + L"-bit, " +
           std::to_wstring(format->nChannels) + L" channels, " +
           (isFloatFormat(format) ? L"float" : L"PCM");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--bounded-tone") {
        double frequency = 0.0;
        double seconds = 0.0;
        std::filesystem::path outputDirectory;
        try {
            for (int index = 2; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--frequency" &&
                    index + 1 < argc) {
                    frequency = std::stod(argv[++index]);
                } else if (
                    argument == L"--seconds" &&
                    index + 1 < argc) {
                    seconds = std::stod(argv[++index]);
                } else if (
                    argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown bounded-tone argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletBoundedTone(
            frequency, seconds, outputDirectory);
    }
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--stage-e-fixture") {
        if (argc < 3) {
            std::cerr << "A Stage E fixture name is required.\n";
            return 64;
        }
        const std::wstring_view fixtureName(argv[2]);
        std::filesystem::path outputDirectory;
        try {
            for (int index = 3; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown Stage E fixture argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletStageEFixture(
            fixtureName, outputDirectory);
    }
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--format-matrix") {
        std::filesystem::path outputDirectory;
        try {
            for (int index = 2; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown format-matrix argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletFormatMatrix(outputDirectory);
    }
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--capture-sample") {
        double seconds = 0.0;
        std::filesystem::path outputDirectory;
        try {
            for (int index = 2; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--seconds" &&
                    index + 1 < argc) {
                    seconds = std::stod(argv[++index]);
                } else if (
                    argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown capture-sample argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletCaptureSample(seconds, outputDirectory);
    }
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--stress-phase") {
        if (argc < 3) {
            std::cerr << "A stress phase name is required.\n";
            return 64;
        }
        const std::wstring_view phase(argv[2]);
        std::uint32_t iterations = 0;
        std::filesystem::path outputDirectory;
        try {
            for (int index = 3; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--iterations" &&
                    index + 1 < argc) {
                    const auto parsed = std::stoul(argv[++index]);
                    if (parsed >
                        (std::numeric_limits<
                            std::uint32_t>::max)()) {
                        throw std::runtime_error(
                            "Iteration count is too large.");
                    }
                    iterations =
                        static_cast<std::uint32_t>(parsed);
                } else if (
                    argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown stress-phase argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletStressPhase(
            phase, iterations, outputDirectory);
    }
    if (argc >= 2 &&
        std::wstring_view(argv[1]) == L"--stage-e-soak") {
        double seconds = 0.0;
        std::filesystem::path outputDirectory;
        try {
            for (int index = 2; index < argc; ++index) {
                const std::wstring_view argument(argv[index]);
                if (argument == L"--seconds" &&
                    index + 1 < argc) {
                    seconds = std::stod(argv[++index]);
                } else if (
                    argument == L"--output-dir" &&
                    index + 1 < argc) {
                    outputDirectory = argv[++index];
                } else {
                    throw std::runtime_error(
                        "Unknown Stage E soak argument.");
                }
            }
        } catch (std::exception const& error) {
            std::cerr << error.what() << "\n";
            return 64;
        }
        if (outputDirectory.empty()) {
            std::cerr << "--output-dir is required.\n";
            return 64;
        }
        return RunCueletStageESoak(seconds, outputDirectory);
    }
    if (argc == 1 ||
        (argc == 2 && std::wstring_view(argv[1]) == L"--quality")) {
        return RunCueletQualitySuite();
    }
    if (argc != 2 || std::wstring_view(argv[1]) != L"--legacy-tone") {
        std::wcerr
            << L"Usage: Cuelet.VirtualAudio.FlowTest.exe "
               L"[--quality|--legacy-tone|--bounded-tone ...|"
               L"--stage-e-fixture ...|--format-matrix ...|"
               L"--capture-sample ...|--stress-phase ...|"
               L"--stage-e-soak ...]\n";
        return 64;
    }
    try {
        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");

        const auto render =
            findEndpoint(enumerator.Get(), eRender, renderEndpointName);
        const auto capture =
            findEndpoint(enumerator.Get(), eCapture, captureEndpointName);
        const auto actualRenderName = deviceName(render.Get());
        const auto actualCaptureName = deviceName(capture.Get());
        const auto renderEndpointId = deviceId(render.Get());
        const auto captureEndpointId = deviceId(capture.Get());
        const auto renderParent = cueletParent(render.Get());
        const auto captureParent = cueletParent(capture.Get());
        if (renderParent.empty() || captureParent.empty() ||
            lowercase(renderParent) != lowercase(captureParent)) {
            throw std::runtime_error(
                "Cuelet render and capture endpoints do not share one active root parent.");
        }

        ComPtr<IAudioClient> renderClient;
        ComPtr<IAudioClient> captureClient;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(renderClient.GetAddressOf())),
            "Activate render IAudioClient");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(captureClient.GetAddressOf())),
            "Activate capture IAudioClient");

        WAVEFORMATEX* rawRenderFormat = nullptr;
        WAVEFORMATEX* rawCaptureFormat = nullptr;
        check(renderClient->GetMixFormat(&rawRenderFormat), "GetMixFormat(render)");
        check(captureClient->GetMixFormat(&rawCaptureFormat), "GetMixFormat(capture)");
        struct FormatCleanup {
            WAVEFORMATEX* render{};
            WAVEFORMATEX* capture{};
            ~FormatCleanup() { ::CoTaskMemFree(render); ::CoTaskMemFree(capture); }
        } formats{rawRenderFormat, rawCaptureFormat};
        validateFormat(formats.render, "Render");
        validateFormat(formats.capture, "Capture");

        constexpr REFERENCE_TIME bufferDuration = 1'000'000; // 100 ms.
        check(
            renderClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0,
                formats.render, nullptr),
            "Initialize(render)");
        check(
            captureClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0,
                formats.capture, nullptr),
            "Initialize(capture)");
        REFERENCE_TIME renderStreamLatency = 0;
        REFERENCE_TIME captureStreamLatency = 0;
        check(
            renderClient->GetStreamLatency(&renderStreamLatency),
            "GetStreamLatency(render)");
        check(
            captureClient->GetStreamLatency(&captureStreamLatency),
            "GetStreamLatency(capture)");

        ComPtr<IAudioRenderClient> renderService;
        ComPtr<IAudioCaptureClient> captureService;
        check(
            renderClient->GetService(IID_PPV_ARGS(&renderService)),
            "GetService(IAudioRenderClient)");
        check(
            captureClient->GetService(IID_PPV_ARGS(&captureService)),
            "GetService(IAudioCaptureClient)");

        UINT32 renderBufferFrames = 0;
        check(renderClient->GetBufferSize(&renderBufferFrames), "GetBufferSize(render)");
        std::uint64_t renderFrameIndex = 0;
        BYTE* initialRenderBuffer = nullptr;
        check(
            renderService->GetBuffer(renderBufferFrames, &initialRenderBuffer),
            "IAudioRenderClient::GetBuffer(initial)");
        fillRenderBuffer(
            initialRenderBuffer, renderBufferFrames, formats.render,
            renderFrameIndex);
        check(
            renderService->ReleaseBuffer(renderBufferFrames, 0),
            "IAudioRenderClient::ReleaseBuffer(initial)");

        std::vector<double> captured;
        captured.reserve(static_cast<std::size_t>(
            formats.capture->nSamplesPerSec) * 5);
        check(captureClient->Start(), "IAudioClient::Start(capture)");
        check(renderClient->Start(), "IAudioClient::Start(render)");

        const auto stopAt =
            std::chrono::steady_clock::now() + std::chrono::seconds(4);
        DWORD discontinuities = 0;
        while (std::chrono::steady_clock::now() < stopAt) {
            UINT32 padding = 0;
            check(renderClient->GetCurrentPadding(&padding), "GetCurrentPadding(render)");
            if (padding < renderBufferFrames) {
                const auto available = renderBufferFrames - padding;
                BYTE* buffer = nullptr;
                check(
                    renderService->GetBuffer(available, &buffer),
                    "IAudioRenderClient::GetBuffer");
                fillRenderBuffer(
                    buffer, available, formats.render, renderFrameIndex);
                check(
                    renderService->ReleaseBuffer(available, 0),
                    "IAudioRenderClient::ReleaseBuffer");
            }

            for (;;) {
                UINT32 nextPacketFrames = 0;
                check(
                    captureService->GetNextPacketSize(&nextPacketFrames),
                    "IAudioCaptureClient::GetNextPacketSize");
                if (nextPacketFrames == 0) break;
                BYTE* buffer = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                check(
                    captureService->GetBuffer(
                        &buffer, &frames, &flags, nullptr, nullptr),
                    "IAudioCaptureClient::GetBuffer");
                if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                    ++discontinuities;
                }
                appendCapture(captured, buffer, frames, flags, formats.capture);
                check(
                    captureService->ReleaseBuffer(frames),
                    "IAudioCaptureClient::ReleaseBuffer");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(renderClient->Stop(), "IAudioClient::Stop(render)");
        check(captureClient->Stop(), "IAudioClient::Stop(capture)");

        const auto measurement =
            measureBestWindow(captured, formats.capture->nSamplesPerSec);
        const auto observedLatency =
            observedSignalOnsetSeconds(
                captured, formats.capture->nSamplesPerSec);
        const auto nominalStreamLatencyMs =
            static_cast<double>(
                renderStreamLatency + captureStreamLatency) / 10'000.0;
        const bool passed =
            measurement.rms >= 0.08 &&
            measurement.toneRms >= 0.08 &&
            measurement.purity >= 0.85 &&
            std::isfinite(observedLatency) &&
            discontinuities <= 1;

        std::wcout << L"Render endpoint: " << actualRenderName << L"\n"
                   << L"Render endpoint ID: " << renderEndpointId << L"\n"
                   << L"Render AudioEndpoint instance ID: "
                   << audioEndpointInstanceId(renderEndpointId) << L"\n"
                   << L"Render parent: " << renderParent << L"\n"
                   << L"Render format: " << formatDescription(formats.render) << L"\n"
                   << L"Capture endpoint: " << actualCaptureName << L"\n"
                   << L"Capture endpoint ID: " << captureEndpointId << L"\n"
                   << L"Capture AudioEndpoint instance ID: "
                   << audioEndpointInstanceId(captureEndpointId) << L"\n"
                   << L"Capture parent: " << captureParent << L"\n"
                   << L"Capture format: " << formatDescription(formats.capture) << L"\n"
                   << L"Captured frames: " << captured.size() << L"\n"
                   << L"Discontinuities: " << discontinuities << L"\n"
                   << std::fixed << std::setprecision(4)
                   << L"Best one-second window: " << measurement.startSeconds << L" s\n"
                   << L"RMS: " << measurement.rms << L"\n"
                   << L"997 Hz RMS: " << measurement.toneRms << L"\n"
                   << L"Tone purity: " << measurement.purity << L"\n"
                   << L"Observed signal onset: "
                   << observedLatency * 1000.0 << L" ms\n"
                   << L"Nominal WASAPI stream latency: "
                   << nominalStreamLatencyMs << L" ms\n"
                   << L"Render-to-capture flow: " << (passed ? L"PASS" : L"FAIL")
                   << L"\n";
        return passed ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "Cuelet virtual-audio flow test failed: " << error.what() << "\n";
        return 2;
    }
}
