#include "QualitySuite.h"

#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <propsys.h>
#include <devguid.h>
#include <devpkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <psapi.h>
#include <setupapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t cueletHardwareId[] = L"ROOT\\CUELETVIRTUALAUDIO";
inline constexpr GUID audioEndpointClass{
    0xc166523c, 0xfe0c, 0x4a94,
    {0xa5, 0x86, 0xf1, 0xa8, 0x0c, 0xfb, 0xbf, 0x3e}};
constexpr std::uint32_t canonicalSampleRate = 48'000;
constexpr std::uint16_t canonicalChannels = 2;
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double signalAmplitude = 0.35;
constexpr double leadingSilenceSeconds = 0.25;
constexpr double trailingSilenceSeconds = 0.25;

void check(HRESULT result, char const* operation)
{
    if (FAILED(result)) {
        char value[16]{};
        sprintf_s(value, "%08X", static_cast<unsigned>(result));
        throw std::runtime_error(
            std::string(operation) + " failed with HRESULT 0x" + value);
    }
}

struct ComLifetime {
    ComLifetime()
    {
        const auto result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (result != RPC_E_CHANGED_MODE) check(result, "CoInitializeEx");
        initialized = result != RPC_E_CHANGED_MODE;
    }
    ~ComLifetime()
    {
        if (initialized) ::CoUninitialize();
    }
    bool initialized{};
};

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

std::wstring endpointId(IMMDevice* device)
{
    wchar_t* raw = nullptr;
    check(device->GetId(&raw), "IMMDevice::GetId");
    std::wstring value = raw ? raw : L"";
    ::CoTaskMemFree(raw);
    return value;
}

std::wstring endpointName(IMMDevice* device)
{
    ComPtr<IPropertyStore> properties;
    check(device->OpenPropertyStore(STGM_READ, &properties), "OpenPropertyStore");
    PROPVARIANT value;
    ::PropVariantInit(&value);
    const auto result = properties->GetValue(PKEY_Device_FriendlyName, &value);
    std::wstring name =
        SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal
            ? value.pwszVal : L"";
    ::PropVariantClear(&value);
    check(result, "GetValue(PKEY_Device_FriendlyName)");
    return name;
}

std::wstring audioEndpointInstanceId(std::wstring id)
{
    constexpr std::wstring_view prefix = L"SWD\\MMDEVAPI\\";
    if (!startsWithInsensitive(id, prefix)) {
        id = std::wstring(prefix) + id;
    }
    return id;
}

std::wstring pnpStringProperty(
    std::wstring instanceId,
    DEVPROPKEY const& key)
{
    instanceId = audioEndpointInstanceId(std::move(instanceId));
    const auto devices = ::SetupDiGetClassDevsW(
        &audioEndpointClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return {};

    std::wstring result;
    SP_DEVINFO_DATA data{sizeof(data)};
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
        current = terminator + 1;
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
        pnpStringProperty(endpointId(device), DEVPKEY_Device_Parent);
    const auto prefix = std::wstring(cueletHardwareId) + L"\\";
    if (parent.size() <= prefix.size() ||
        !startsWithInsensitive(parent, prefix) ||
        !isActiveCueletRoot(parent)) {
        return {};
    }
    return parent;
}

ComPtr<IMMDevice> findCueletEndpoint(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow)
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
        if (!cueletParent(candidate.Get()).empty()) {
            if (found) {
                throw std::runtime_error(
                    "More than one active Cuelet endpoint has the same direction.");
            }
            found = candidate;
        }
    }
    if (!found) {
        throw std::runtime_error("The complete Cuelet endpoint pair is not active.");
    }
    return found;
}

ComPtr<IMMDevice> findActiveEndpointByName(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow,
    std::wstring_view expectedName)
{
    ComPtr<IMMDeviceCollection> devices;
    check(
        enumerator->EnumAudioEndpoints(
            flow, DEVICE_STATE_ACTIVE, &devices),
        "EnumAudioEndpoints(named)");
    UINT count = 0;
    check(devices->GetCount(&count), "GetCount(named)");
    ComPtr<IMMDevice> found;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> candidate;
        check(devices->Item(index, &candidate), "Item(named)");
        if (lowercase(endpointName(candidate.Get())) !=
            lowercase(std::wstring(expectedName))) {
            continue;
        }
        if (found) {
            throw std::runtime_error(
                "More than one active endpoint has the requested name.");
        }
        found = candidate;
    }
    if (!found) {
        throw std::runtime_error(
            "The requested active audio endpoint was not found.");
    }
    return found;
}

bool isFloatFormat(WAVEFORMATEX const* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize <
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto* extended =
        reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(format);
    return ::IsEqualGUID(
        extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

bool isPcmFormat(WAVEFORMATEX const* format)
{
    if (format->wFormatTag == WAVE_FORMAT_PCM) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize <
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return false;
    }
    const auto* extended =
        reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(format);
    return ::IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
}

std::uint16_t validBits(WAVEFORMATEX const* format)
{
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >=
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(format)
            ->Samples.wValidBitsPerSample;
    }
    return format->wBitsPerSample;
}

DWORD channelMask(WAVEFORMATEX const* format)
{
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >=
            sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        return reinterpret_cast<WAVEFORMATEXTENSIBLE const*>(format)
            ->dwChannelMask;
    }
    return 0;
}

void validateFormat(WAVEFORMATEX const* format, char const* role)
{
    if (!format || format->nSamplesPerSec == 0 ||
        format->nChannels != canonicalChannels ||
        format->nBlockAlign == 0 ||
        format->nBlockAlign !=
            format->nChannels * (format->wBitsPerSample / 8) ||
        format->nAvgBytesPerSec !=
            format->nSamplesPerSec * format->nBlockAlign ||
        (!isFloatFormat(format) && !isPcmFormat(format))) {
        throw std::runtime_error(
            std::string(role) + " exposes an inconsistent mix format.");
    }
    if (format->nSamplesPerSec != canonicalSampleRate) {
        throw std::runtime_error(
            std::string(role) + " is not 48 kHz.");
    }
    if (isFloatFormat(format) && format->wBitsPerSample != 32) {
        throw std::runtime_error(
            std::string(role) + " floating-point width is not 32 bits.");
    }
    if (isPcmFormat(format) &&
        format->wBitsPerSample != 16 &&
        format->wBitsPerSample != 24 &&
        format->wBitsPerSample != 32) {
        throw std::runtime_error(
            std::string(role) + " PCM width is unsupported.");
    }
}

std::wstring formatDescription(WAVEFORMATEX const* format)
{
    std::wostringstream stream;
    stream << format->nSamplesPerSec << L" Hz, "
           << format->wBitsPerSample << L"-bit ("
           << validBits(format) << L" valid), "
           << format->nChannels << L" channels, "
           << (isFloatFormat(format) ? L"float" : L"PCM")
           << L", block " << format->nBlockAlign
           << L", byte rate " << format->nAvgBytesPerSec
           << L", mask 0x" << std::hex << channelMask(format);
    return stream.str();
}

void writeSample(
    BYTE* destination,
    WAVEFORMATEX const* format,
    double sample)
{
    if (!std::isfinite(sample)) sample = 0.0;
    sample = std::clamp(sample, -1.0, 1.0);
    if (isFloatFormat(format)) {
        *reinterpret_cast<float*>(destination) =
            static_cast<float>(sample);
    } else if (format->wBitsPerSample == 16) {
        const auto scaled =
            sample <= -1.0 ? -32768 : static_cast<int>(
                std::lround(sample * 32767.0));
        *reinterpret_cast<std::int16_t*>(destination) =
            static_cast<std::int16_t>(scaled);
    } else if (format->wBitsPerSample == 24) {
        const auto scaled =
            sample <= -1.0 ? -8'388'608 : static_cast<std::int32_t>(
                std::llround(sample * 8'388'607.0));
        destination[0] = static_cast<BYTE>(scaled);
        destination[1] = static_cast<BYTE>(scaled >> 8);
        destination[2] = static_cast<BYTE>(scaled >> 16);
    } else {
        const auto scaled =
            sample <= -1.0
                ? std::numeric_limits<std::int32_t>::min()
                : static_cast<std::int32_t>(
                    std::llround(sample * 2'147'483'647.0));
        *reinterpret_cast<std::int32_t*>(destination) = scaled;
    }
}

double readSample(
    BYTE const* source,
    WAVEFORMATEX const* format)
{
    double value = 0.0;
    if (isFloatFormat(format)) {
        value = *reinterpret_cast<float const*>(source);
    } else if (format->wBitsPerSample == 16) {
        value =
            static_cast<double>(
                *reinterpret_cast<std::int16_t const*>(source)) /
            32768.0;
    } else if (format->wBitsPerSample == 24) {
        std::int32_t integer =
            static_cast<std::int32_t>(source[0]) |
            (static_cast<std::int32_t>(source[1]) << 8) |
            (static_cast<std::int32_t>(source[2]) << 16);
        if ((integer & 0x00800000) != 0) {
            integer |= static_cast<std::int32_t>(0xFF000000);
        }
        value = static_cast<double>(integer) / 8'388'608.0;
    } else {
        value =
            static_cast<double>(
                *reinterpret_cast<std::int32_t const*>(source)) /
            2'147'483'648.0;
    }
    return std::isfinite(value)
        ? std::clamp(value, -1.0, 1.0) : 0.0;
}

struct StereoBuffer {
    std::vector<float> samples;

    std::size_t frames() const
    {
        return samples.size() / canonicalChannels;
    }

    float& at(std::size_t frame, std::size_t channel)
    {
        return samples[frame * canonicalChannels + channel];
    }

    float at(std::size_t frame, std::size_t channel) const
    {
        return samples[frame * canonicalChannels + channel];
    }
};

enum class SignalKind {
    Silence,
    Tone,
    Sweep,
    MultiTone,
    Impulse,
    PinkNoise,
    Reference
};

struct TestSignal {
    std::string name;
    SignalKind kind{};
    StereoBuffer audio;
    std::size_t activeStart{};
    std::size_t activeEnd{};
    std::vector<double> frequencies;
};

std::size_t secondsToFrames(double seconds)
{
    return static_cast<std::size_t>(
        std::llround(seconds * canonicalSampleRate));
}

TestSignal makeSignal(
    std::string name,
    SignalKind kind,
    double activeSeconds,
    std::vector<double> frequencies = {})
{
    TestSignal signal;
    signal.name = std::move(name);
    signal.kind = kind;
    signal.activeStart = secondsToFrames(leadingSilenceSeconds);
    signal.activeEnd =
        signal.activeStart + secondsToFrames(activeSeconds);
    signal.frequencies = std::move(frequencies);
    signal.audio.samples.resize(
        (signal.activeEnd + secondsToFrames(trailingSilenceSeconds)) *
        canonicalChannels);
    return signal;
}

double fadeGain(std::size_t offset, std::size_t length)
{
    const auto fade = std::min<std::size_t>(
        secondsToFrames(0.01), length / 4);
    if (fade == 0) return 1.0;
    if (offset < fade) {
        return static_cast<double>(offset) /
               static_cast<double>(fade);
    }
    if (offset + fade >= length) {
        return static_cast<double>(length - 1 - offset) /
               static_cast<double>(fade);
    }
    return 1.0;
}

TestSignal makeTone(double frequency, double activeSeconds = 2.0)
{
    std::ostringstream name;
    name << std::fixed << std::setprecision(
        std::floor(frequency) == frequency ? 0 : 1)
         << frequency << "Hz";
    auto signal = makeSignal(
        name.str(), SignalKind::Tone, activeSeconds, {frequency});
    const auto length = signal.activeEnd - signal.activeStart;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto value = static_cast<float>(
            signalAmplitude * fadeGain(offset, length) *
            std::sin(
                2.0 * pi * frequency *
                static_cast<double>(offset) /
                canonicalSampleRate));
        signal.audio.at(signal.activeStart + offset, 0) = value;
        signal.audio.at(signal.activeStart + offset, 1) = value;
    }
    return signal;
}

TestSignal makeSilence()
{
    return makeSignal("silence", SignalKind::Silence, 2.0);
}

TestSignal makeSweep()
{
    auto signal = makeSignal(
        "log-sweep-20Hz-20kHz", SignalKind::Sweep, 5.0);
    const auto length = signal.activeEnd - signal.activeStart;
    constexpr double startFrequency = 20.0;
    constexpr double endFrequency = 20'000.0;
    constexpr double duration = 5.0;
    const auto ratio = endFrequency / startFrequency;
    const auto phaseScale =
        2.0 * pi * startFrequency * duration / std::log(ratio);
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto time =
            static_cast<double>(offset) / canonicalSampleRate;
        const auto phase =
            phaseScale * (std::pow(ratio, time / duration) - 1.0);
        const auto value = static_cast<float>(
            signalAmplitude * fadeGain(offset, length) *
            std::sin(phase));
        signal.audio.at(signal.activeStart + offset, 0) = value;
        signal.audio.at(signal.activeStart + offset, 1) = value;
    }
    return signal;
}

TestSignal makeMultiTone()
{
    std::vector<double> frequencies{40.0, 100.0, 440.0, 997.0, 5'000.0, 12'000.0};
    auto signal = makeSignal(
        "multitone", SignalKind::MultiTone, 3.0, frequencies);
    const auto length = signal.activeEnd - signal.activeStart;
    const auto perTone = 0.11;
    for (std::size_t offset = 0; offset < length; ++offset) {
        double value = 0.0;
        for (std::size_t index = 0; index < frequencies.size(); ++index) {
            value += perTone * std::sin(
                2.0 * pi * frequencies[index] *
                static_cast<double>(offset) / canonicalSampleRate +
                static_cast<double>(index) * 0.37);
        }
        value *= fadeGain(offset, length);
        const auto sample = static_cast<float>(value);
        signal.audio.at(signal.activeStart + offset, 0) = sample;
        signal.audio.at(signal.activeStart + offset, 1) = sample;
    }
    return signal;
}

TestSignal makeImpulse()
{
    auto signal = makeSignal("impulse", SignalKind::Impulse, 1.5);
    constexpr std::array<double, 4> offsets{0.1, 0.4, 0.8, 1.2};
    for (const auto seconds : offsets) {
        const auto frame =
            signal.activeStart + secondsToFrames(seconds);
        signal.audio.at(frame, 0) = 0.7f;
        signal.audio.at(frame, 1) = 0.7f;
        if (frame + 1 < signal.activeEnd) {
            signal.audio.at(frame + 1, 0) = -0.35f;
            signal.audio.at(frame + 1, 1) = -0.35f;
        }
    }
    return signal;
}

TestSignal makePinkNoise()
{
    auto signal = makeSignal("pink-noise", SignalKind::PinkNoise, 3.0);
    std::mt19937 generator(0xC0E1E7u);
    std::uniform_real_distribution<double> white(-1.0, 1.0);
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double b3 = 0.0;
    double b4 = 0.0;
    double b5 = 0.0;
    double b6 = 0.0;
    const auto length = signal.activeEnd - signal.activeStart;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto input = white(generator);
        b0 = 0.99886 * b0 + input * 0.0555179;
        b1 = 0.99332 * b1 + input * 0.0750759;
        b2 = 0.96900 * b2 + input * 0.1538520;
        b3 = 0.86650 * b3 + input * 0.3104856;
        b4 = 0.55000 * b4 + input * 0.5329522;
        b5 = -0.7616 * b5 - input * 0.0168980;
        const auto pink =
            b0 + b1 + b2 + b3 + b4 + b5 + b6 +
            input * 0.5362;
        b6 = input * 0.115926;
        const auto value = static_cast<float>(
            std::clamp(
                pink * 0.035 * fadeGain(offset, length),
                -0.65, 0.65));
        signal.audio.at(signal.activeStart + offset, 0) = value;
        signal.audio.at(signal.activeStart + offset, 1) = value;
    }
    return signal;
}

TestSignal makeBassReference()
{
    auto signal = makeSignal(
        "bass-heavy-reference", SignalKind::Reference, 5.0,
        {45.0, 90.0, 180.0, 440.0, 2'400.0});
    const auto length = signal.activeEnd - signal.activeStart;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto time =
            static_cast<double>(offset) / canonicalSampleRate;
        const auto beatPosition = std::fmod(time, 0.5);
        const auto kickEnvelope = std::exp(-beatPosition * 12.0);
        const auto kickFrequency = 45.0 + 55.0 * std::exp(-beatPosition * 25.0);
        const auto kick =
            0.25 * kickEnvelope *
            std::sin(2.0 * pi * kickFrequency * beatPosition);
        const auto bassEnvelope =
            0.5 + 0.5 * std::sin(2.0 * pi * 0.5 * time);
        const auto bass =
            0.18 * bassEnvelope * std::sin(2.0 * pi * 45.0 * time) +
            0.08 * std::sin(2.0 * pi * 90.0 * time + 0.2);
        const auto mid =
            0.07 * std::sin(2.0 * pi * 180.0 * time) +
            0.045 * std::sin(2.0 * pi * 440.0 * time + 0.7);
        const auto top =
            0.025 * std::sin(2.0 * pi * 2'400.0 * time);
        const auto left = static_cast<float>(
            std::clamp(
                (kick + bass + mid + top) * fadeGain(offset, length),
                -0.8, 0.8));
        const auto right = static_cast<float>(
            std::clamp(
                (kick + bass +
                 0.95 * mid + 0.8 * top) * fadeGain(offset, length),
                -0.8, 0.8));
        signal.audio.at(signal.activeStart + offset, 0) = left;
        signal.audio.at(signal.activeStart + offset, 1) = right;
    }
    return signal;
}

TestSignal makeMusicReference()
{
    // Deterministic bass-led musical material with kick, bass, chords,
    // melody, stereo movement, and short rests. This remains reproducible
    // while exercising music-like transients and low-frequency continuity.
    auto signal = makeSignal(
        "bass-music-reference", SignalKind::Reference, 8.0,
        {43.65, 55.0, 65.41, 82.41, 110.0, 220.0, 440.0, 880.0});
    constexpr std::array<double, 8> bassNotes{
        43.65, 43.65, 55.0, 65.41, 43.65, 82.41, 55.0, 65.41};
    constexpr std::array<double, 8> melodyNotes{
        440.0, 523.25, 659.25, 523.25,
        392.0, 440.0, 329.63, 392.0};
    const auto length = signal.activeEnd - signal.activeStart;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto time =
            static_cast<double>(offset) / canonicalSampleRate;
        const auto step = static_cast<std::size_t>(
            std::floor(time / 0.5)) % bassNotes.size();
        const auto withinStep = std::fmod(time, 0.5);
        const auto noteEnvelope =
            std::exp(-withinStep * 1.8) *
            std::clamp(withinStep / 0.008, 0.0, 1.0);
        const auto kickPosition = std::fmod(time, 0.5);
        const auto kickEnvelope = std::exp(-kickPosition * 15.0);
        const auto kick =
            0.23 * kickEnvelope *
            std::sin(
                2.0 * pi *
                (48.0 + 45.0 * std::exp(-kickPosition * 28.0)) *
                kickPosition);
        const auto bass =
            0.19 * noteEnvelope *
            std::sin(2.0 * pi * bassNotes[step] * time) +
            0.055 * noteEnvelope *
            std::sin(4.0 * pi * bassNotes[step] * time + 0.15);
        const auto chordRoot = bassNotes[step] * 4.0;
        const auto chord =
            0.040 * std::sin(2.0 * pi * chordRoot * time) +
            0.030 * std::sin(2.0 * pi * chordRoot * 1.25 * time) +
            0.025 * std::sin(2.0 * pi * chordRoot * 1.5 * time);
        const auto melodyGate =
            withinStep < 0.38
                ? std::sin(pi * withinStep / 0.38)
                : 0.0;
        const auto melody =
            0.055 * melodyGate *
            std::sin(2.0 * pi * melodyNotes[step] * time);
        const auto rest =
            (time >= 3.75 && time < 4.0) ||
            (time >= 7.50 && time < 7.75)
                ? 0.0 : 1.0;
        const auto pan =
            0.12 * std::sin(2.0 * pi * 0.17 * time);
        const auto common =
            rest * (kick + bass + chord + melody) *
            fadeGain(offset, length);
        signal.audio.at(signal.activeStart + offset, 0) =
            static_cast<float>(
                std::clamp(common * (1.0 - pan), -0.85, 0.85));
        signal.audio.at(signal.activeStart + offset, 1) =
            static_cast<float>(
                std::clamp(common * (1.0 + pan), -0.85, 0.85));
    }
    return signal;
}

TestSignal makeSpeechReference()
{
    // A deterministic voiced/unvoiced speech-like reference exercises the
    // same fundamental, formant, and sibilance bands as ordinary speech.
    // Real microphone comparison remains a separate acceptance test.
    auto signal = makeSignal(
        "speech-reference", SignalKind::Reference, 4.0,
        {120.0, 700.0, 1'200.0, 2'600.0, 7'000.0});
    std::mt19937 generator(0x5EEC4u);
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    const auto length = signal.activeEnd - signal.activeStart;
    double glottalPhase = 0.0;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto time =
            static_cast<double>(offset) / canonicalSampleRate;
        const auto syllable = std::fmod(time, 0.48);
        const auto envelope =
            syllable < 0.36
                ? std::sin(pi * std::clamp(syllable / 0.08, 0.0, 1.0)) *
                  std::clamp((0.36 - syllable) / 0.06, 0.0, 1.0)
                : 0.0;
        const auto fundamental =
            115.0 + 18.0 * std::sin(2.0 * pi * 0.8 * time);
        glottalPhase += 2.0 * pi * fundamental / canonicalSampleRate;
        const auto voiced =
            0.12 * std::sin(glottalPhase) +
            0.055 * std::sin(2.0 * glottalPhase) +
            0.03 * std::sin(3.0 * glottalPhase);
        const auto formants =
            0.055 * std::sin(2.0 * pi * 700.0 * time) +
            0.035 * std::sin(2.0 * pi * 1'200.0 * time + 0.4) +
            0.025 * std::sin(2.0 * pi * 2'600.0 * time + 0.9);
        const auto sibilance =
            (syllable > 0.27 && syllable < 0.34)
                ? 0.035 * noise(generator) *
                  std::sin(2.0 * pi * 7'000.0 * time)
                : 0.0;
        const auto value = static_cast<float>(
            std::clamp(
                envelope * (voiced + formants) + sibilance,
                -0.55, 0.55));
        signal.audio.at(signal.activeStart + offset, 0) = value;
        signal.audio.at(signal.activeStart + offset, 1) = value;
    }
    return signal;
}

std::vector<TestSignal> makeSuite()
{
    std::vector<TestSignal> tests;
    tests.push_back(makeSilence());
    for (const auto frequency :
         {40.0, 80.0, 100.0, 440.0, 997.0, 5'000.0, 12'000.0}) {
        tests.push_back(makeTone(frequency));
    }
    tests.push_back(makeSweep());
    tests.push_back(makeMultiTone());
    tests.push_back(makeImpulse());
    tests.push_back(makePinkNoise());
    tests.push_back(makeBassReference());
    tests.push_back(makeMusicReference());
    tests.push_back(makeSpeechReference());
    return tests;
}

std::filesystem::path diagnosticsDirectory()
{
    std::array<wchar_t, MAX_PATH + 1> temp{};
    const auto length =
        ::GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (length == 0 || length >= temp.size()) {
        throw std::runtime_error("GetTempPathW failed.");
    }
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    wchar_t stamp[64]{};
    swprintf_s(
        stamp, L"%04u%02u%02u-%02u%02u%02u-%03u",
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds);
    auto directory =
        std::filesystem::path(temp.data()) /
        L"Cuelet" / L"AudioDiagnostics" / stamp;
    std::filesystem::create_directories(directory);
    return directory;
}

std::filesystem::path diagnosticsDirectory(
    std::filesystem::path const& requested)
{
    if (requested.empty()) {
        return diagnosticsDirectory();
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(requested, error);
    if (error) {
        throw std::runtime_error(
            "Could not resolve the requested diagnostics directory.");
    }
    std::filesystem::create_directories(absolute, error);
    if (error) {
        throw std::runtime_error(
            "Could not create the requested diagnostics directory.");
    }
    return absolute;
}

#pragma pack(push, 1)
struct WavHeader {
    std::array<char, 4> riff{'R', 'I', 'F', 'F'};
    std::uint32_t fileSizeMinus8{};
    std::array<char, 4> wave{'W', 'A', 'V', 'E'};
    std::array<char, 4> fmt{'f', 'm', 't', ' '};
    std::uint32_t fmtSize{16};
    std::uint16_t formatTag{3};
    std::uint16_t channels{canonicalChannels};
    std::uint32_t sampleRate{canonicalSampleRate};
    std::uint32_t byteRate{
        canonicalSampleRate * canonicalChannels * sizeof(float)};
    std::uint16_t blockAlign{canonicalChannels * sizeof(float)};
    std::uint16_t bitsPerSample{32};
    std::array<char, 4> data{'d', 'a', 't', 'a'};
    std::uint32_t dataSize{};
};
#pragma pack(pop)

void saveFloatWav(
    std::filesystem::path const& path,
    StereoBuffer const& audio)
{
    const auto bytes =
        audio.samples.size() * sizeof(audio.samples.front());
    if (bytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Diagnostic WAV is too large.");
    }
    WavHeader header;
    header.dataSize = static_cast<std::uint32_t>(bytes);
    header.fileSizeMinus8 =
        static_cast<std::uint32_t>(sizeof(WavHeader) - 8 + bytes);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Could not create diagnostic WAV.");
    }
    stream.write(
        reinterpret_cast<char const*>(&header), sizeof(header));
    stream.write(
        reinterpret_cast<char const*>(audio.samples.data()),
        static_cast<std::streamsize>(bytes));
    if (!stream) {
        throw std::runtime_error("Could not finish diagnostic WAV.");
    }
}

struct CaptureResult {
    StereoBuffer audio;
    DWORD discontinuities{};
    double renderStreamLatencyMs{};
    double captureStreamLatencyMs{};
};

void fillRenderBuffer(
    BYTE* destination,
    UINT32 frameCount,
    WAVEFORMATEX const* format,
    TestSignal const& signal,
    std::uint64_t& frameIndex)
{
    const auto bytesPerChannel =
        format->nBlockAlign / format->nChannels;
    for (UINT32 frame = 0; frame < frameCount; ++frame, ++frameIndex) {
        const auto sourcePosition =
            static_cast<double>(frameIndex) *
            canonicalSampleRate /
            static_cast<double>(format->nSamplesPerSec);
        const auto sourceFrame =
            static_cast<std::size_t>(sourcePosition);
        const auto nextSourceFrame =
            sourceFrame + 1 < signal.audio.frames()
                ? sourceFrame + 1 : sourceFrame;
        const auto fraction =
            sourcePosition - static_cast<double>(sourceFrame);
        const bool hasSource =
            sourceFrame < signal.audio.frames();
        auto* frameStart =
            destination +
            static_cast<std::size_t>(frame) * format->nBlockAlign;
        for (WORD channel = 0; channel < format->nChannels; ++channel) {
            double sample = 0.0;
            if (hasSource) {
                const auto interpolate =
                    [&](std::size_t sourceChannel) {
                        const auto first =
                            signal.audio.at(
                                sourceFrame, sourceChannel);
                        const auto second =
                            signal.audio.at(
                                nextSourceFrame, sourceChannel);
                        return first + (second - first) * fraction;
                    };
                sample =
                    format->nChannels == 1
                        ? 0.5 * (
                            interpolate(0) + interpolate(1))
                        : interpolate(std::min<std::size_t>(
                            channel, canonicalChannels - 1));
            }
            writeSample(
                frameStart +
                    static_cast<std::size_t>(channel) * bytesPerChannel,
                format,
                sample);
        }
    }
}

void appendCapture(
    StereoBuffer& capture,
    BYTE const* source,
    UINT32 frameCount,
    DWORD flags,
    WAVEFORMATEX const* format)
{
    const auto oldSize = capture.samples.size();
    capture.samples.resize(
        oldSize +
        static_cast<std::size_t>(frameCount) * canonicalChannels);
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ||
        source == nullptr) {
        std::fill(
            capture.samples.begin() +
                static_cast<std::ptrdiff_t>(oldSize),
            capture.samples.end(), 0.0f);
        return;
    }
    const auto bytesPerChannel =
        format->nBlockAlign / format->nChannels;
    for (UINT32 frame = 0; frame < frameCount; ++frame) {
        const auto* frameStart =
            source +
            static_cast<std::size_t>(frame) * format->nBlockAlign;
        if (format->nChannels == 1) {
            const auto sample = static_cast<float>(
                readSample(frameStart, format));
            capture.samples[
                oldSize +
                static_cast<std::size_t>(frame) *
                    canonicalChannels] = sample;
            capture.samples[
                oldSize +
                static_cast<std::size_t>(frame) *
                    canonicalChannels + 1] = sample;
            continue;
        }
        for (std::size_t channel = 0;
             channel < canonicalChannels;
             ++channel) {
            capture.samples[
                oldSize +
                static_cast<std::size_t>(frame) *
                    canonicalChannels + channel] =
                static_cast<float>(readSample(
                    frameStart + channel * bytesPerChannel,
                    format));
        }
    }
}

StereoBuffer resampleToCanonical(
    StereoBuffer const& source,
    std::uint32_t sourceSampleRate)
{
    if (sourceSampleRate == canonicalSampleRate ||
        source.frames() == 0) {
        return source;
    }
    StereoBuffer result;
    const auto outputFrames = static_cast<std::size_t>(
        std::llround(
            static_cast<double>(source.frames()) *
            canonicalSampleRate / sourceSampleRate));
    result.samples.resize(outputFrames * canonicalChannels);
    for (std::size_t frame = 0; frame < outputFrames; ++frame) {
        const auto sourcePosition =
            static_cast<double>(frame) *
            sourceSampleRate / canonicalSampleRate;
        const auto sourceFrame =
            std::min(
                static_cast<std::size_t>(sourcePosition),
                source.frames() - 1);
        const auto nextSourceFrame =
            std::min(sourceFrame + 1, source.frames() - 1);
        const auto fraction =
            sourcePosition - static_cast<double>(sourceFrame);
        for (std::size_t channel = 0;
             channel < canonicalChannels;
             ++channel) {
            const auto first = source.at(sourceFrame, channel);
            const auto second = source.at(nextSourceFrame, channel);
            result.at(frame, channel) = static_cast<float>(
                first + (second - first) * fraction);
        }
    }
    return result;
}

struct FormatPointers {
    WAVEFORMATEX* render{};
    WAVEFORMATEX* capture{};
    ~FormatPointers()
    {
        ::CoTaskMemFree(render);
        ::CoTaskMemFree(capture);
    }
};

WAVEFORMATEXTENSIBLE engineFormat(
    std::uint32_t sampleRate,
    std::uint16_t channels,
    std::uint16_t bitsPerSample,
    bool floatingPoint)
{
    WAVEFORMATEXTENSIBLE result{};
    result.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    result.Format.nChannels = channels;
    result.Format.nSamplesPerSec = sampleRate;
    result.Format.wBitsPerSample = bitsPerSample;
    result.Format.nBlockAlign = static_cast<WORD>(
        channels * (bitsPerSample / 8));
    result.Format.nAvgBytesPerSec =
        sampleRate * result.Format.nBlockAlign;
    result.Format.cbSize =
        sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    result.Samples.wValidBitsPerSample = bitsPerSample;
    result.dwChannelMask =
        channels == 1
            ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO;
    result.SubFormat =
        floatingPoint
            ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            : KSDATAFORMAT_SUBTYPE_PCM;
    return result;
}

HRESULT sharedFormatSupport(
    IMMDevice* device,
    WAVEFORMATEX const* format)
{
    ComPtr<IAudioClient> client;
    check(
        device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf())),
        "Activate shared-format probe");
    WAVEFORMATEX* closest = nullptr;
    const auto status = client->IsFormatSupported(
        AUDCLNT_SHAREMODE_SHARED, format, &closest);
    ::CoTaskMemFree(closest);
    return status;
}

CaptureResult runSignal(
    IMMDevice* renderDevice,
    IMMDevice* captureDevice,
    TestSignal const& signal,
    WAVEFORMATEX const* expectedRenderFormat,
    WAVEFORMATEX const* expectedCaptureFormat,
    WAVEFORMATEX const* requestedRenderFormat = nullptr,
    WAVEFORMATEX const* requestedCaptureFormat = nullptr)
{
    ComPtr<IAudioClient> renderClient;
    ComPtr<IAudioClient> captureClient;
    check(
        renderDevice->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(
                renderClient.GetAddressOf())),
        "Activate render IAudioClient");
    check(
        captureDevice->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(
                captureClient.GetAddressOf())),
        "Activate capture IAudioClient");

    FormatPointers formats;
    check(
        renderClient->GetMixFormat(&formats.render),
        "GetMixFormat(render)");
    check(
        captureClient->GetMixFormat(&formats.capture),
        "GetMixFormat(capture)");
    validateFormat(formats.render, "Render");
    validateFormat(formats.capture, "Capture");
    if (formats.render->nBlockAlign !=
            expectedRenderFormat->nBlockAlign ||
        formats.render->wFormatTag !=
            expectedRenderFormat->wFormatTag ||
        formats.capture->nBlockAlign !=
            expectedCaptureFormat->nBlockAlign ||
        formats.capture->wFormatTag !=
            expectedCaptureFormat->wFormatTag) {
        throw std::runtime_error(
            "The endpoint mix format changed while the suite was running.");
    }
    const auto* streamRenderFormat =
        requestedRenderFormat != nullptr
            ? requestedRenderFormat : formats.render;
    const auto* streamCaptureFormat =
        requestedCaptureFormat != nullptr
            ? requestedCaptureFormat : formats.capture;

    constexpr REFERENCE_TIME bufferDuration =
        1'000'000; // 100 ms.
    constexpr DWORD sharedConversionFlags =
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    check(
        renderClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED, sharedConversionFlags,
            bufferDuration, 0,
            streamRenderFormat, nullptr),
        "Initialize(render)");
    check(
        captureClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED, sharedConversionFlags,
            bufferDuration, 0,
            streamCaptureFormat, nullptr),
        "Initialize(capture)");

    REFERENCE_TIME renderLatency = 0;
    REFERENCE_TIME captureLatency = 0;
    check(
        renderClient->GetStreamLatency(&renderLatency),
        "GetStreamLatency(render)");
    check(
        captureClient->GetStreamLatency(&captureLatency),
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
    check(
        renderClient->GetBufferSize(&renderBufferFrames),
        "GetBufferSize(render)");
    std::uint64_t renderedFrames = 0;
    BYTE* initial = nullptr;
    check(
        renderService->GetBuffer(renderBufferFrames, &initial),
        "IAudioRenderClient::GetBuffer(initial)");
    fillRenderBuffer(
        initial, renderBufferFrames, streamRenderFormat,
        signal, renderedFrames);
    check(
        renderService->ReleaseBuffer(renderBufferFrames, 0),
        "IAudioRenderClient::ReleaseBuffer(initial)");

    CaptureResult result;
    result.audio.samples.reserve(
        (signal.audio.frames() + canonicalSampleRate) *
        canonicalChannels);
    result.renderStreamLatencyMs =
        static_cast<double>(renderLatency) / 10'000.0;
    result.captureStreamLatencyMs =
        static_cast<double>(captureLatency) / 10'000.0;

    check(captureClient->Start(), "IAudioClient::Start(capture)");
    check(renderClient->Start(), "IAudioClient::Start(render)");
    const auto runSeconds =
        static_cast<double>(signal.audio.frames()) /
            canonicalSampleRate +
        0.65;
    const auto stopAt =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(runSeconds));

    while (std::chrono::steady_clock::now() < stopAt) {
        UINT32 padding = 0;
        check(
            renderClient->GetCurrentPadding(&padding),
            "GetCurrentPadding(render)");
        if (padding < renderBufferFrames) {
            const auto available = renderBufferFrames - padding;
            BYTE* buffer = nullptr;
            check(
                renderService->GetBuffer(available, &buffer),
                "IAudioRenderClient::GetBuffer");
            fillRenderBuffer(
                buffer, available, streamRenderFormat,
                signal, renderedFrames);
            check(
                renderService->ReleaseBuffer(available, 0),
                "IAudioRenderClient::ReleaseBuffer");
        }

        for (;;) {
            UINT32 packetFrames = 0;
            check(
                captureService->GetNextPacketSize(&packetFrames),
                "IAudioCaptureClient::GetNextPacketSize");
            if (packetFrames == 0) break;
            BYTE* buffer = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            check(
                captureService->GetBuffer(
                    &buffer, &frames, &flags, nullptr, nullptr),
                "IAudioCaptureClient::GetBuffer");
            if ((flags &
                 AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                ++result.discontinuities;
            }
            appendCapture(
                result.audio, buffer, frames, flags,
                streamCaptureFormat);
            check(
                captureService->ReleaseBuffer(frames),
                "IAudioCaptureClient::ReleaseBuffer");
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(2));
    }
    check(renderClient->Stop(), "IAudioClient::Stop(render)");
    check(captureClient->Stop(), "IAudioClient::Stop(capture)");
    result.audio = resampleToCanonical(
        result.audio, streamCaptureFormat->nSamplesPerSec);
    return result;
}

struct Analysis {
    double peak{};
    double rms{};
    double dc{};
    std::uint64_t clippedSamples{};
    double clippedPercent{};
    double snrDb{};
    double thdnDb{};
    double tonePurity{};
    double frequencyResponseErrorDb{};
    double expectedFrequencyResponseDb{};
    double referenceCorrelation{};
    double channelBalanceDb{};
    double phaseCorrelation{};
    std::uint64_t duplicateFrameRuns{};
    std::uint64_t zeroFilledQuanta{};
    std::uint64_t clickCandidates{};
    std::size_t captureFrames{};
    std::size_t alignedFrames{};
    double onsetMs{};
    double durationDriftMs{};
    bool passed{};
    std::string failure;
};

struct Range {
    std::size_t captureStart{};
    std::size_t referenceStart{};
    std::size_t frames{};
};

double frameMagnitude(StereoBuffer const& audio, std::size_t frame)
{
    return 0.5 *
        (std::abs(audio.at(frame, 0)) +
         std::abs(audio.at(frame, 1)));
}

std::size_t firstActiveFrame(
    StereoBuffer const& audio,
    double threshold)
{
    const auto window = secondsToFrames(0.005);
    for (std::size_t start = 0;
         start + window <= audio.frames();
         start += window) {
        double power = 0.0;
        for (std::size_t index = 0; index < window; ++index) {
            const auto value =
                frameMagnitude(audio, start + index);
            power += value * value;
        }
        if (std::sqrt(power / window) >= threshold) {
            return start;
        }
    }
    return audio.frames();
}

std::size_t lastActiveFrame(
    StereoBuffer const& audio,
    double threshold)
{
    const auto window = secondsToFrames(0.005);
    if (audio.frames() < window) return 0;
    auto start = audio.frames() - window;
    for (;;) {
        double power = 0.0;
        for (std::size_t index = 0; index < window; ++index) {
            const auto value =
                frameMagnitude(audio, start + index);
            power += value * value;
        }
        if (std::sqrt(power / window) >= threshold) {
            return start + window;
        }
        if (start < window) break;
        start -= window;
    }
    return 0;
}

Range alignedRange(
    TestSignal const& signal,
    StereoBuffer const& capture)
{
    const auto referenceOnset =
        firstActiveFrame(signal.audio, 0.01);
    const auto captureOnset =
        firstActiveFrame(capture, 0.01);
    if (referenceOnset == signal.audio.frames() ||
        captureOnset == capture.frames()) {
        return {};
    }

    // Refine the onset alignment by correlating a bounded 250 ms window.
    const auto correlationFrames = std::min<std::size_t>(
        secondsToFrames(0.25),
        signal.activeEnd > referenceOnset
            ? signal.activeEnd - referenceOnset : 0);
    const auto radius = secondsToFrames(0.025);
    double best = -std::numeric_limits<double>::infinity();
    std::size_t bestCapture = captureOnset;
    const auto searchBegin =
        captureOnset > radius ? captureOnset - radius : 0;
    const auto searchEnd = std::min(
        capture.frames(), captureOnset + radius + 1);
    for (auto candidate = searchBegin;
         candidate < searchEnd;
         ++candidate) {
        if (candidate + correlationFrames > capture.frames()) {
            break;
        }
        double dot = 0.0;
        double referencePower = 0.0;
        double capturePower = 0.0;
        for (std::size_t index = 0;
             index < correlationFrames;
             ++index) {
            const auto reference =
                0.5 * (
                    signal.audio.at(referenceOnset + index, 0) +
                    signal.audio.at(referenceOnset + index, 1));
            const auto observed =
                0.5 * (
                    capture.at(candidate + index, 0) +
                    capture.at(candidate + index, 1));
            dot += reference * observed;
            referencePower += reference * reference;
            capturePower += observed * observed;
        }
        const auto denominator =
            std::sqrt(referencePower * capturePower);
        const auto correlation =
            denominator > 1e-12 ? dot / denominator : -1.0;
        if (correlation > best) {
            best = correlation;
            bestCapture = candidate;
        }
    }
    const auto frames = std::min(
        signal.activeEnd - referenceOnset,
        capture.frames() - bestCapture);
    return {bestCapture, referenceOnset, frames};
}

double rmsOf(
    StereoBuffer const& audio,
    std::size_t start,
    std::size_t frames,
    std::size_t channel)
{
    if (frames == 0) return 0.0;
    double power = 0.0;
    for (std::size_t index = 0; index < frames; ++index) {
        const auto value = audio.at(start + index, channel);
        power += value * value;
    }
    return std::sqrt(power / frames);
}

std::pair<double, double> toneProjection(
    StereoBuffer const& audio,
    std::size_t start,
    std::size_t frames,
    std::size_t channel,
    double frequency)
{
    double sine = 0.0;
    double cosine = 0.0;
    for (std::size_t index = 0; index < frames; ++index) {
        const auto phase =
            2.0 * pi * frequency *
            static_cast<double>(index) /
            canonicalSampleRate;
        const auto value = audio.at(start + index, channel);
        sine += value * std::sin(phase);
        cosine += value * std::cos(phase);
    }
    const auto scale = 2.0 / frames;
    return {sine * scale, cosine * scale};
}

double db(double ratio)
{
    return ratio > 1e-15
        ? 20.0 * std::log10(ratio)
        : -300.0;
}

Analysis analyzeSilence(CaptureResult const& capture)
{
    Analysis result;
    result.captureFrames = capture.audio.frames();
    if (capture.audio.frames() == 0) {
        result.failure = "no capture frames";
        return result;
    }
    double sum = 0.0;
    double power = 0.0;
    for (const auto sample : capture.audio.samples) {
        result.peak =
            std::max(result.peak, std::abs(static_cast<double>(sample)));
        sum += sample;
        power += static_cast<double>(sample) * sample;
        if (std::abs(sample) >= 0.999969) {
            ++result.clippedSamples;
        }
    }
    result.dc = sum / capture.audio.samples.size();
    result.rms =
        std::sqrt(power / capture.audio.samples.size());
    result.clippedPercent =
        100.0 * result.clippedSamples /
        capture.audio.samples.size();
    result.snrDb =
        result.rms > 0.0 ? db(signalAmplitude / result.rms) : 300.0;
    result.phaseCorrelation = 1.0;
    result.passed =
        result.rms <= 0.0001 &&
        result.peak <= 0.001 &&
        result.clippedSamples == 0 &&
        capture.discontinuities <= 1;
    if (!result.passed) {
        result.failure = "digital silence contains noise or discontinuities";
    }
    return result;
}

Analysis analyzeSignal(
    TestSignal const& signal,
    CaptureResult const& capture,
    double expectedFrequencyResponseDb = 0.0)
{
    Analysis result;
    result.expectedFrequencyResponseDb =
        expectedFrequencyResponseDb;
    const auto range = alignedRange(signal, capture.audio);
    result.captureFrames = capture.audio.frames();
    result.alignedFrames = range.frames;
    if (range.frames < secondsToFrames(0.20)) {
        result.failure = "could not align enough captured signal";
        return result;
    }

    const auto trim = std::min<std::size_t>(
        secondsToFrames(0.02), range.frames / 8);
    const auto captureStart = range.captureStart + trim;
    const auto referenceStart = range.referenceStart + trim;
    const auto frames = range.frames - 2 * trim;
    double sum = 0.0;
    double power = 0.0;
    double leftPower = 0.0;
    double rightPower = 0.0;
    double cross = 0.0;
    double referencePower = 0.0;
    double referenceCaptureDot = 0.0;
    double captureMonoPower = 0.0;
    std::uint64_t adjacentEqualRun = 0;
    constexpr std::uint64_t frozenFrameThreshold =
        canonicalSampleRate / 1'000;
    double previousLeft = capture.audio.at(captureStart, 0);
    double previousRight = capture.audio.at(captureStart, 1);

    for (std::size_t index = 0; index < frames; ++index) {
        const auto left =
            static_cast<double>(
                capture.audio.at(captureStart + index, 0));
        const auto right =
            static_cast<double>(
                capture.audio.at(captureStart + index, 1));
        const auto mono = 0.5 * (left + right);
        const auto reference =
            0.5 * (
                signal.audio.at(referenceStart + index, 0) +
                signal.audio.at(referenceStart + index, 1));
        result.peak =
            std::max({result.peak, std::abs(left), std::abs(right)});
        sum += left + right;
        power += left * left + right * right;
        leftPower += left * left;
        rightPower += right * right;
        cross += left * right;
        referencePower += reference * reference;
        referenceCaptureDot += reference * mono;
        captureMonoPower += mono * mono;
        if (std::abs(left) >= 0.999969) ++result.clippedSamples;
        if (std::abs(right) >= 0.999969) ++result.clippedSamples;

        if (index > 0) {
            if (left == previousLeft && right == previousRight &&
                (std::abs(left) + std::abs(right)) > 1e-5) {
                ++adjacentEqualRun;
                // The native Cuelet pin is 16-bit PCM. Smooth low-frequency
                // material can legitimately quantize to the same stereo
                // frame for several adjacent samples near an extremum.
                // Treat only a full 1 ms nonzero freeze as a transport
                // duplicate; shorter corruption remains covered by the
                // reference residual, click, drift, and discontinuity checks.
                if (adjacentEqualRun + 1 == frozenFrameThreshold) {
                    ++result.duplicateFrameRuns;
                }
            } else {
                adjacentEqualRun = 0;
            }
        }
        previousLeft = left;
        previousRight = right;
    }

    constexpr std::size_t oneMillisecondFrames =
        canonicalSampleRate / 1'000;
    for (std::size_t start = 0;
         start + oneMillisecondFrames <= frames;
         start += oneMillisecondFrames) {
        double referencePeak = 0.0;
        double capturePeak = 0.0;
        for (std::size_t offset = 0;
             offset < oneMillisecondFrames;
             ++offset) {
            for (std::size_t channel = 0;
                 channel < canonicalChannels;
                 ++channel) {
                referencePeak = std::max(
                    referencePeak,
                    std::abs(static_cast<double>(
                        signal.audio.at(
                            referenceStart + start + offset,
                            channel))));
                capturePeak = std::max(
                    capturePeak,
                    std::abs(static_cast<double>(
                        capture.audio.at(
                            captureStart + start + offset,
                            channel))));
            }
        }
        if (referencePeak >= 0.02 && capturePeak <= 1e-7) {
            ++result.zeroFilledQuanta;
        }
    }

    result.dc = sum / (2.0 * frames);
    result.rms = std::sqrt(power / (2.0 * frames));
    result.clippedPercent =
        100.0 * result.clippedSamples / (2.0 * frames);
    const auto leftRms = std::sqrt(leftPower / frames);
    const auto rightRms = std::sqrt(rightPower / frames);
    result.channelBalanceDb =
        db(std::max(leftRms, 1e-15) /
           std::max(rightRms, 1e-15));
    result.phaseCorrelation =
        cross / std::sqrt(
            std::max(leftPower * rightPower, 1e-30));

    const auto fittedScale =
        referencePower > 1e-15
            ? referenceCaptureDot / referencePower : 0.0;
    result.referenceCorrelation =
        referencePower > 1e-15 && captureMonoPower > 1e-15
            ? referenceCaptureDot /
                std::sqrt(referencePower * captureMonoPower)
            : 1.0;
    double residualPower = 0.0;
    for (std::size_t index = 0; index < frames; ++index) {
        const auto observed =
            0.5 * (
                capture.audio.at(captureStart + index, 0) +
                capture.audio.at(captureStart + index, 1));
        const auto reference =
            0.5 * (
                signal.audio.at(referenceStart + index, 0) +
                signal.audio.at(referenceStart + index, 1));
        const auto residual =
            observed - fittedScale * reference;
        residualPower += residual * residual;
    }
    const auto signalRms =
        std::abs(fittedScale) *
        std::sqrt(referencePower / frames);
    const auto residualRms =
        std::sqrt(residualPower / frames);
    result.snrDb = db(signalRms / std::max(residualRms, 1e-15));
    if (signal.kind != SignalKind::Impulse && frames > 1) {
        const auto clickThreshold =
            std::max(0.10, 8.0 * residualRms);
        const auto firstObserved =
            0.5 * (
                capture.audio.at(captureStart, 0) +
                capture.audio.at(captureStart, 1));
        const auto firstReference =
            0.5 * (
                signal.audio.at(referenceStart, 0) +
                signal.audio.at(referenceStart, 1));
        auto previousResidual =
            firstObserved - fittedScale * firstReference;
        for (std::size_t index = 1; index < frames; ++index) {
            const auto observed =
                0.5 * (
                    capture.audio.at(captureStart + index, 0) +
                    capture.audio.at(captureStart + index, 1));
            const auto reference =
                0.5 * (
                    signal.audio.at(referenceStart + index, 0) +
                    signal.audio.at(referenceStart + index, 1));
            const auto residual =
                observed - fittedScale * reference;
            if (std::abs(residual - previousResidual) > clickThreshold) {
                ++result.clickCandidates;
            }
            previousResidual = residual;
        }
    }

    const auto captureOnset =
        firstActiveFrame(capture.audio, 0.01);
    const auto referenceOnset =
        firstActiveFrame(signal.audio, 0.01);
    result.onsetMs =
        1000.0 *
        (static_cast<double>(captureOnset) -
         static_cast<double>(referenceOnset)) /
        canonicalSampleRate;
    const auto captureEnd =
        lastActiveFrame(capture.audio, 0.01);
    const auto referenceEnd =
        lastActiveFrame(signal.audio, 0.01);
    result.durationDriftMs =
        1000.0 *
        ((static_cast<double>(captureEnd) -
          static_cast<double>(captureOnset)) -
         (static_cast<double>(referenceEnd) -
          static_cast<double>(referenceOnset))) /
        canonicalSampleRate;

    if (signal.kind == SignalKind::Tone &&
        !signal.frequencies.empty()) {
        const auto frequency = signal.frequencies.front();
        double totalTonePower = 0.0;
        for (std::size_t channel = 0;
             channel < canonicalChannels; ++channel) {
            const auto [sine, cosine] = toneProjection(
                capture.audio, captureStart, frames,
                channel, frequency);
            totalTonePower +=
                (sine * sine + cosine * cosine) / 2.0;
        }
        totalTonePower /= canonicalChannels;
        const auto fundamentalRms =
            std::sqrt(totalTonePower);
        result.tonePurity =
            result.rms > 1e-15
                ? fundamentalRms / result.rms : 0.0;
        result.thdnDb = db(
            std::sqrt(std::max(
                result.rms * result.rms -
                    fundamentalRms * fundamentalRms,
                0.0)) /
            std::max(fundamentalRms, 1e-15));
        result.frequencyResponseErrorDb =
            db(fundamentalRms /
               (signalAmplitude / std::sqrt(2.0)));
    } else {
        result.tonePurity = 0.0;
        result.thdnDb = db(
            residualRms / std::max(signalRms, 1e-15));
        result.frequencyResponseErrorDb = 0.0;
    }

    const auto commonPass =
        result.clippedSamples == 0 &&
        std::abs(result.dc) <= 0.005 &&
        std::abs(result.channelBalanceDb) <= 0.5 &&
        result.phaseCorrelation >= 0.98 &&
        result.duplicateFrameRuns == 0 &&
        result.zeroFilledQuanta == 0 &&
        capture.discontinuities <= 1 &&
        std::abs(result.durationDriftMs) <= 30.0;
    bool signalPass = result.rms >= 0.001;
    if (signal.kind == SignalKind::Tone) {
        signalPass =
            result.tonePurity >= 0.98 &&
            result.thdnDb <= -17.0 &&
            std::abs(
                result.frequencyResponseErrorDb -
                result.expectedFrequencyResponseDb) <= 1.0 &&
            result.clickCandidates == 0;
    } else if (signal.kind == SignalKind::Impulse) {
        signalPass = result.peak >= 0.3;
    } else {
        signalPass = result.snrDb >= 20.0;
    }
    result.passed = commonPass && signalPass;
    if (!result.passed) {
        result.failure =
            "one or more fidelity thresholds were exceeded";
    }
    return result;
}

void printAnalysis(
    TestSignal const& signal,
    CaptureResult const& capture,
    Analysis const& analysis)
{
    std::cout << std::fixed << std::setprecision(5)
              << "  peak=" << analysis.peak
              << " rms=" << analysis.rms
              << " dc=" << analysis.dc
              << " clipped=" << analysis.clippedSamples
              << " (" << analysis.clippedPercent << "%)"
              << " snr=" << analysis.snrDb << " dB"
              << " reference-correlation="
              << analysis.referenceCorrelation
              << " thd+n=" << analysis.thdnDb << " dB";
    if (signal.kind == SignalKind::Tone) {
        std::cout << " purity=" << analysis.tonePurity
                  << " response=" <<
                     analysis.frequencyResponseErrorDb << " dB"
                  << " expected-response="
                  << analysis.expectedFrequencyResponseDb << " dB";
    }
    std::cout << "\n"
              << "  balance=" << analysis.channelBalanceDb << " dB"
              << " phase=" << analysis.phaseCorrelation
              << " duplicate-runs=" << analysis.duplicateFrameRuns
              << " zero-filled-1ms-quanta="
              << analysis.zeroFilledQuanta
              << " clicks=" << analysis.clickCandidates
              << " discontinuities=" << capture.discontinuities
              << " onset=" << analysis.onsetMs << " ms"
              << " drift=" << analysis.durationDriftMs << " ms"
              << "\n"
              << "  capture-frames=" << analysis.captureFrames
              << " aligned-frames=" << analysis.alignedFrames
              << " canonical-stereo-bytes="
              << analysis.captureFrames *
                     canonicalChannels * sizeof(float)
              << "\n"
              << "  result=" << (analysis.passed ? "PASS" : "FAIL");
    if (!analysis.failure.empty()) {
        std::cout << " (" << analysis.failure << ")";
    }
    std::cout << "\n";
}

std::wstring defaultEndpointId(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow,
    ERole role)
{
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(
            flow, role, &device)) ||
        !device) {
        return {};
    }
    return endpointId(device.Get());
}

void printActiveEndpointFormats(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow,
    wchar_t const* heading)
{
    const auto consoleDefault =
        lowercase(defaultEndpointId(enumerator, flow, eConsole));
    const auto communicationsDefault =
        lowercase(defaultEndpointId(
            enumerator, flow, eCommunications));
    ComPtr<IMMDeviceCollection> devices;
    check(
        enumerator->EnumAudioEndpoints(
            flow, DEVICE_STATE_ACTIVE, &devices),
        "EnumAudioEndpoints(format inventory)");
    UINT count = 0;
    check(
        devices->GetCount(&count),
        "IMMDeviceCollection::GetCount(format inventory)");
    std::wcout << heading << L":\n";
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        check(
            devices->Item(index, &device),
            "IMMDeviceCollection::Item(format inventory)");
        ComPtr<IAudioClient> client;
        const auto activate = device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(activate)) continue;
        WAVEFORMATEX* raw = nullptr;
        if (FAILED(client->GetMixFormat(&raw)) || raw == nullptr) {
            ::CoTaskMemFree(raw);
            continue;
        }
        const auto id = endpointId(device.Get());
        const auto lowered = lowercase(id);
        std::wstring roles;
        if (lowered == consoleDefault) roles += L" [default]";
        if (lowered == communicationsDefault) {
            roles += L" [default communications]";
        }
        std::wcout
            << L"  " << endpointName(device.Get()) << roles << L"\n"
            << L"    ID: " << id << L"\n"
            << L"    Shared mix: " << formatDescription(raw) << L"\n";
        ::CoTaskMemFree(raw);
    }
}

void verifyPair(IMMDevice* render, IMMDevice* capture)
{
    const auto renderParent = cueletParent(render);
    const auto captureParent = cueletParent(capture);
    if (renderParent.empty() || captureParent.empty() ||
        lowercase(renderParent) != lowercase(captureParent)) {
        throw std::runtime_error(
            "Cuelet endpoints do not share one active root parent.");
    }
}

void initializeClient(
    IMMDevice* device,
    ComPtr<IAudioClient>& client,
    WAVEFORMATEX** format)
{
    check(
        device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf())),
        "Activate stress IAudioClient");
    check(
        client->GetMixFormat(format),
        "GetMixFormat(stress)");
    validateFormat(*format, "Stress");
    constexpr REFERENCE_TIME bufferDuration =
        1'000'000; // 100 ms.
    check(
        client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0,
            *format, nullptr),
        "Initialize(stress)");
}

void primeSilentRender(IAudioClient* client)
{
    ComPtr<IAudioRenderClient> service;
    check(
        client->GetService(IID_PPV_ARGS(&service)),
        "GetService(stress render)");
    UINT32 frames = 0;
    check(
        client->GetBufferSize(&frames),
        "GetBufferSize(stress render)");
    BYTE* buffer = nullptr;
    check(
        service->GetBuffer(frames, &buffer),
        "GetBuffer(stress render)");
    check(
        service->ReleaseBuffer(
            frames, AUDCLNT_BUFFERFLAGS_SILENT),
        "ReleaseBuffer(stress render)");
}

void runSingleEndpointOpenClose(
    IMMDevice* device,
    bool render,
    std::uint32_t iterations)
{
    for (std::uint32_t iteration = 0;
         iteration < iterations;
         ++iteration) {
        ComPtr<IAudioClient> client;
        WAVEFORMATEX* format = nullptr;
        initializeClient(device, client, &format);
        struct FormatCleanup {
            WAVEFORMATEX* value;
            ~FormatCleanup() { ::CoTaskMemFree(value); }
        } cleanup{format};
        if (render) {
            primeSilentRender(client.Get());
        }
        check(client->Start(), "Start(single endpoint)");
        std::this_thread::sleep_for(
            std::chrono::milliseconds(40));
        check(client->Stop(), "Stop(single endpoint)");
        check(client->Reset(), "Reset(single endpoint)");
    }
}

void runRepeatedStartStop(
    IMMDevice* render,
    IMMDevice* capture,
    std::uint32_t iterations,
    bool resetCaptureDuringRender)
{
    ComPtr<IAudioClient> renderClient;
    ComPtr<IAudioClient> captureClient;
    FormatPointers formats;
    initializeClient(
        render, renderClient, &formats.render);
    initializeClient(
        capture, captureClient, &formats.capture);

    for (std::uint32_t iteration = 0;
         iteration < iterations;
         ++iteration) {
        primeSilentRender(renderClient.Get());
        check(
            captureClient->Start(),
            "Start(stress capture)");
        check(
            renderClient->Start(),
            "Start(stress render)");
        std::this_thread::sleep_for(
            std::chrono::milliseconds(40));

        if (resetCaptureDuringRender) {
            check(
                captureClient->Stop(),
                "Stop(capture for active reset)");
            check(
                captureClient->Reset(),
                "Reset(capture while render active)");
            check(
                captureClient->Start(),
                "Restart(capture after active reset)");
            std::this_thread::sleep_for(
                std::chrono::milliseconds(40));
        }

        check(
            renderClient->Stop(),
            "Stop(stress render)");
        check(
            captureClient->Stop(),
            "Stop(stress capture)");
        check(
            renderClient->Reset(),
            "Reset(stress render)");
        check(
            captureClient->Reset(),
            "Reset(stress capture)");
    }
}

struct RuntimeResourceSnapshot {
    std::uint64_t workingSetBytes{};
    std::uint64_t privateBytes{};
    std::uint64_t systemNonpagedPoolBytes{};
    DWORD processHandles{};
};

RuntimeResourceSnapshot runtimeResourceSnapshot()
{
    RuntimeResourceSnapshot result;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (::GetProcessMemoryInfo(
            ::GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory))) {
        result.workingSetBytes = memory.WorkingSetSize;
        result.privateBytes = memory.PrivateUsage;
    }
    ::GetProcessHandleCount(
        ::GetCurrentProcess(), &result.processHandles);
    PERFORMANCE_INFORMATION performance{};
    performance.cb = sizeof(performance);
    if (::GetPerformanceInfo(
            &performance, sizeof(performance))) {
        result.systemNonpagedPoolBytes =
            static_cast<std::uint64_t>(performance.KernelNonpaged) *
            performance.PageSize;
    }
    return result;
}

bool endpointIsActive(IMMDevice* device)
{
    DWORD state = 0;
    return
        device != nullptr &&
        SUCCEEDED(device->GetState(&state)) &&
        state == DEVICE_STATE_ACTIVE;
}

} // namespace

int RunCueletQualitySuite()
{
    try {
        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        const auto renderParent = cueletParent(render.Get());
        const auto captureParent = cueletParent(capture.Get());
        if (renderParent.empty() || captureParent.empty() ||
            lowercase(renderParent) != lowercase(captureParent)) {
            throw std::runtime_error(
                "Cuelet endpoints do not share one active root parent.");
        }

        ComPtr<IAudioClient> renderProbe;
        ComPtr<IAudioClient> captureProbe;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    renderProbe.GetAddressOf())),
            "Activate render format probe");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    captureProbe.GetAddressOf())),
            "Activate capture format probe");
        FormatPointers formats;
        check(
            renderProbe->GetMixFormat(&formats.render),
            "GetMixFormat(render probe)");
        check(
            captureProbe->GetMixFormat(&formats.capture),
            "GetMixFormat(capture probe)");
        validateFormat(formats.render, "Render");
        validateFormat(formats.capture, "Capture");

        const auto diagnostics = diagnosticsDirectory();
        std::wcout
            << L"Cuelet virtual-audio broadband quality suite\n"
            << L"Render endpoint: " << endpointName(render.Get()) << L"\n"
            << L"Render endpoint ID: " << endpointId(render.Get()) << L"\n"
            << L"Render parent: " << renderParent << L"\n"
            << L"Render shared mix format: "
            << formatDescription(formats.render) << L"\n"
            << L"Capture endpoint: " << endpointName(capture.Get()) << L"\n"
            << L"Capture endpoint ID: " << endpointId(capture.Get()) << L"\n"
            << L"Capture parent: " << captureParent << L"\n"
            << L"Capture shared mix format: "
            << formatDescription(formats.capture) << L"\n"
            << L"Canonical reference format: 48000 Hz, 32-bit float, "
               L"stereo interleaved\n"
            << L"Diagnostics directory: " << diagnostics << L"\n";
        printActiveEndpointFormats(
            enumerator.Get(), eRender, L"Active render formats");
        printActiveEndpointFormats(
            enumerator.Get(), eCapture, L"Active capture formats");

        auto suite = makeSuite();
        bool allPassed = true;
        for (auto const& signal : suite) {
            saveFloatWav(
                diagnostics /
                    (std::wstring(signal.name.begin(), signal.name.end()) +
                     L"-reference.wav"),
                signal.audio);
            std::cout << "\n[" << signal.name << "]\n";
            const auto captured = runSignal(
                render.Get(), capture.Get(), signal,
                formats.render, formats.capture);
            saveFloatWav(
                diagnostics /
                    (std::wstring(signal.name.begin(), signal.name.end()) +
                     L"-capture.wav"),
                captured.audio);
            const auto analysis =
                signal.kind == SignalKind::Silence
                    ? analyzeSilence(captured)
                    : analyzeSignal(signal, captured);
            printAnalysis(signal, captured, analysis);
            allPassed = allPassed && analysis.passed;
        }

        std::cout
            << "\nBroadband render-to-capture quality: "
            << (allPassed ? "PASS" : "FAIL") << "\n";
        return allPassed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet broadband quality suite failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletBoundedTone(
    double frequency,
    double activeSeconds,
    std::filesystem::path const& outputDirectory)
{
    try {
        if (!std::isfinite(frequency) ||
            frequency < 20.0 ||
            frequency > 20'000.0 ||
            !std::isfinite(activeSeconds) ||
            activeSeconds < 0.1 ||
            activeSeconds > 5.0) {
            throw std::runtime_error(
                "Bounded tone parameters are outside safe limits.");
        }

        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        verifyPair(render.Get(), capture.Get());

        ComPtr<IAudioClient> renderProbe;
        ComPtr<IAudioClient> captureProbe;
        FormatPointers formats;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    renderProbe.GetAddressOf())),
            "Activate render format probe");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    captureProbe.GetAddressOf())),
            "Activate capture format probe");
        check(
            renderProbe->GetMixFormat(&formats.render),
            "GetMixFormat(render probe)");
        check(
            captureProbe->GetMixFormat(&formats.capture),
            "GetMixFormat(capture probe)");
        validateFormat(formats.render, "Render");
        validateFormat(formats.capture, "Capture");

        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        const auto signal =
            makeTone(frequency, activeSeconds);
        const auto baseName =
            std::wstring(signal.name.begin(), signal.name.end());
        saveFloatWav(
            diagnostics / (baseName + L"-reference.wav"),
            signal.audio);
        const auto captured = runSignal(
            render.Get(), capture.Get(), signal,
            formats.render, formats.capture);
        saveFloatWav(
            diagnostics / (baseName + L"-capture.wav"),
            captured.audio);
        const auto analysis =
            analyzeSignal(signal, captured);
        std::cout << "\n[" << signal.name << "]\n";
        printAnalysis(signal, captured, analysis);
        std::wcout
            << L"Diagnostics: "
            << diagnostics.wstring() << L"\n";
        return analysis.passed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet bounded tone failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletStageEFixture(
    std::wstring_view fixtureName,
    std::filesystem::path const& outputDirectory)
{
    try {
        auto suite = makeSuite();
        const auto selected = std::find_if(
            suite.begin(), suite.end(),
            [fixtureName](TestSignal const& signal) {
                return std::wstring(
                           signal.name.begin(), signal.name.end()) ==
                       fixtureName;
            });
        if (selected == suite.end()) {
            throw std::runtime_error(
                "Unknown Stage E fixture name.");
        }

        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        verifyPair(render.Get(), capture.Get());

        ComPtr<IAudioClient> renderProbe;
        ComPtr<IAudioClient> captureProbe;
        FormatPointers formats;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    renderProbe.GetAddressOf())),
            "Activate Stage E fixture render probe");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    captureProbe.GetAddressOf())),
            "Activate Stage E fixture capture probe");
        check(
            renderProbe->GetMixFormat(&formats.render),
            "GetMixFormat(Stage E fixture render)");
        check(
            captureProbe->GetMixFormat(&formats.capture),
            "GetMixFormat(Stage E fixture capture)");
        validateFormat(formats.render, "Stage E fixture render");
        validateFormat(formats.capture, "Stage E fixture capture");

        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        const auto baseName = std::wstring(
            selected->name.begin(), selected->name.end());
        saveFloatWav(
            diagnostics / (baseName + L"-reference.wav"),
            selected->audio);
        const auto captured = runSignal(
            render.Get(), capture.Get(), *selected,
            formats.render, formats.capture);
        saveFloatWav(
            diagnostics / (baseName + L"-capture.wav"),
            captured.audio);
        const auto analysis =
            selected->kind == SignalKind::Silence
                ? analyzeSilence(captured)
                : analyzeSignal(*selected, captured);
        std::cout << "\n[" << selected->name << "]\n";
        printAnalysis(*selected, captured, analysis);
        std::wcout
            << L"Diagnostics: "
            << diagnostics.wstring() << L"\n";
        return analysis.passed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet Stage E fixture failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletFormatMatrix(
    std::filesystem::path const& outputDirectory)
{
    try {
        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        verifyPair(render.Get(), capture.Get());

        ComPtr<IAudioClient> renderProbe;
        ComPtr<IAudioClient> captureProbe;
        FormatPointers mixFormats;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    renderProbe.GetAddressOf())),
            "Activate format-matrix render probe");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    captureProbe.GetAddressOf())),
            "Activate format-matrix capture probe");
        check(
            renderProbe->GetMixFormat(&mixFormats.render),
            "GetMixFormat(format-matrix render)");
        check(
            captureProbe->GetMixFormat(&mixFormats.capture),
            "GetMixFormat(format-matrix capture)");
        validateFormat(mixFormats.render, "Format-matrix render");
        validateFormat(mixFormats.capture, "Format-matrix capture");

        struct MatrixCase {
            std::string name;
            bool renderSide{};
            WAVEFORMATEXTENSIBLE format{};
        };
        const std::array baseCases{
            MatrixCase{
                "44100-mono-pcm16", true,
                engineFormat(44'100, 1, 16, false)},
            MatrixCase{
                "44100-stereo-pcm16", true,
                engineFormat(44'100, 2, 16, false)},
            MatrixCase{
                "44100-stereo-float32", true,
                engineFormat(44'100, 2, 32, true)},
            MatrixCase{
                "48000-mono-pcm16", true,
                engineFormat(48'000, 1, 16, false)},
            MatrixCase{
                "48000-stereo-pcm16", true,
                engineFormat(48'000, 2, 16, false)},
            MatrixCase{
                "48000-stereo-float32", true,
                engineFormat(48'000, 2, 32, true)},
        };
        std::vector<MatrixCase> cases;
        cases.reserve(baseCases.size() * 2);
        for (auto const& base : baseCases) {
            cases.push_back(base);
            cases.back().name = "render-" + base.name;
        }
        for (auto const& base : baseCases) {
            cases.push_back(base);
            cases.back().name = "capture-" + base.name;
            cases.back().renderSide = false;
        }

        struct MatrixResult {
            std::string name;
            HRESULT supportStatus{};
            bool passed{};
            Analysis analysis{};
            DWORD discontinuities{};
            std::string failure;
        };
        std::vector<MatrixResult> results;
        results.reserve(cases.size());
        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        const auto signal = makeTone(997.0, 1.5);
        saveFloatWav(
            diagnostics / L"format-matrix-reference.wav",
            signal.audio);
        bool allPassed = true;
        for (auto const& entry : cases) {
            MatrixResult result;
            result.name = entry.name;
            const auto* requested = &entry.format.Format;
            const auto device =
                entry.renderSide ? render.Get() : capture.Get();
            result.supportStatus =
                sharedFormatSupport(device, requested);
            try {
                if (FAILED(result.supportStatus)) {
                    throw std::runtime_error(
                        "Windows Audio Engine rejected the "
                        "requested shared-mode conversion format.");
                }
                const auto captured = runSignal(
                    render.Get(), capture.Get(), signal,
                    mixFormats.render, mixFormats.capture,
                    entry.renderSide ? requested : nullptr,
                    entry.renderSide ? nullptr : requested);
                result.discontinuities = captured.discontinuities;
                // The Windows Audio Engine uses a constant-power mono to
                // stereo matrix, so a mono render client is expected to
                // arrive at each stereo endpoint channel at -3.01 dB.
                const auto expectedResponseDb =
                    entry.renderSide &&
                    entry.format.Format.nChannels == 1
                        ? db(1.0 / std::sqrt(2.0))
                        : 0.0;
                result.analysis = analyzeSignal(
                    signal, captured, expectedResponseDb);
                result.passed = result.analysis.passed;
                saveFloatWav(
                    diagnostics /
                        (std::wstring(
                             entry.name.begin(), entry.name.end()) +
                         L"-capture.wav"),
                    captured.audio);
                printAnalysis(
                    signal, captured, result.analysis);
            } catch (std::exception const& error) {
                result.failure = error.what();
            }
            allPassed = allPassed && result.passed;
            std::cout
                << "format case=" << entry.name
                << " support=0x" << std::hex
                << static_cast<unsigned long>(result.supportStatus)
                << std::dec
                << " result=" << (result.passed ? "PASS" : "FAIL");
            if (!result.failure.empty()) {
                std::cout << " failure=" << result.failure;
            }
            std::cout << "\n";
            results.push_back(std::move(result));
        }

        std::ofstream summary(
            diagnostics / L"format-matrix.json",
            std::ios::binary | std::ios::trunc);
        if (!summary) {
            throw std::runtime_error(
                "Could not create format-matrix summary.");
        }
        summary
            << "{\n"
            << "  \"passed\": "
            << (allPassed ? "true" : "false") << ",\n"
            << "  \"sharedMode\": true,\n"
            << "  \"automaticPcmConversion\": true,\n"
            << "  \"cases\": [\n";
        for (std::size_t index = 0;
             index < results.size();
             ++index) {
            auto const& result = results[index];
            summary
                << "    {\"name\":\"" << result.name << "\""
                << ",\"supportStatus\":"
                << static_cast<long>(result.supportStatus)
                << ",\"passed\":"
                << (result.passed ? "true" : "false")
                << ",\"snrDb\":" << result.analysis.snrDb
                << ",\"referenceCorrelation\":"
                << result.analysis.referenceCorrelation
                << ",\"frequencyResponseDb\":"
                << result.analysis.frequencyResponseErrorDb
                << ",\"expectedFrequencyResponseDb\":"
                << result.analysis.expectedFrequencyResponseDb
                << ",\"clippedSamples\":"
                << result.analysis.clippedSamples
                << ",\"duplicateFrameRuns\":"
                << result.analysis.duplicateFrameRuns
                << ",\"zeroFilledQuanta\":"
                << result.analysis.zeroFilledQuanta
                << ",\"clickCandidates\":"
                << result.analysis.clickCandidates
                << ",\"positionDiscontinuities\":"
                << result.discontinuities
                << "}"
                << (index + 1 == results.size() ? "\n" : ",\n");
        }
        summary << "  ]\n}\n";
        std::cout
            << "Shared-mode format matrix "
            << (allPassed ? "PASS" : "FAIL")
            << ": " << results.size() << " cases\n";
        return allPassed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet format matrix failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletCaptureSample(
    double durationSeconds,
    std::filesystem::path const& outputDirectory,
    std::wstring_view endpointName,
    bool renderLoopback)
{
    try {
        if (!std::isfinite(durationSeconds) ||
            durationSeconds < 1.0 ||
            durationSeconds > 60.0) {
            throw std::runtime_error(
                "Capture duration must be between 1 and 60 seconds.");
        }
        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto capture = endpointName.empty()
            ? findCueletEndpoint(enumerator.Get(), eCapture)
            : findActiveEndpointByName(
                  enumerator.Get(),
                  renderLoopback ? eRender : eCapture,
                  endpointName);
        ComPtr<IAudioClient> client;
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf())),
            "Activate capture-sample client");
        FormatPointers format;
        check(
            client->GetMixFormat(&format.capture),
            "GetMixFormat(capture sample)");
        validateFormat(format.capture, "Capture sample");
        constexpr REFERENCE_TIME bufferDuration =
            1'000'000; // 100 ms.
        check(
            client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                renderLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                bufferDuration, 0,
                format.capture, nullptr),
            "Initialize(capture sample)");
        ComPtr<IAudioCaptureClient> service;
        check(
            client->GetService(IID_PPV_ARGS(&service)),
            "GetService(capture sample)");

        CaptureResult captured;
        captured.audio.samples.reserve(
            static_cast<std::size_t>(
                durationSeconds * canonicalSampleRate) *
            canonicalChannels);
        check(client->Start(), "Start(capture sample)");
        const auto stopAt =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(durationSeconds));
        while (std::chrono::steady_clock::now() < stopAt) {
            for (;;) {
                UINT32 packetFrames = 0;
                check(
                    service->GetNextPacketSize(&packetFrames),
                    "GetNextPacketSize(capture sample)");
                if (packetFrames == 0) break;
                BYTE* buffer = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                check(
                    service->GetBuffer(
                        &buffer, &frames, &flags, nullptr, nullptr),
                    "GetBuffer(capture sample)");
                if ((flags &
                     AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
                    ++captured.discontinuities;
                }
                appendCapture(
                    captured.audio, buffer, frames, flags,
                    format.capture);
                check(
                    service->ReleaseBuffer(frames),
                    "ReleaseBuffer(capture sample)");
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(2));
        }
        check(client->Stop(), "Stop(capture sample)");
        captured.audio = resampleToCanonical(
            captured.audio, format.capture->nSamplesPerSec);
        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        const auto baseName = renderLoopback
            ? L"real-application-render-loopback"
            : L"real-application-capture";
        const auto output = diagnostics / (baseName + std::wstring(L".wav"));
        saveFloatWav(output, captured.audio);

        double peak = 0.0;
        double power = 0.0;
        for (const auto sample : captured.audio.samples) {
            peak = std::max(
                peak, std::abs(static_cast<double>(sample)));
            power += static_cast<double>(sample) * sample;
        }
        const auto rms =
            captured.audio.samples.empty()
                ? 0.0
                : std::sqrt(
                    power / captured.audio.samples.size());
        const bool passed =
            !captured.audio.samples.empty() &&
            captured.discontinuities <= 1;
        std::ofstream summary(
            diagnostics / (baseName + std::wstring(L".json")),
            std::ios::binary | std::ios::trunc);
        if (!summary) {
            throw std::runtime_error(
                "Could not create capture-sample summary.");
        }
        summary
            << std::fixed << std::setprecision(8)
            << "{\n"
            << "  \"passed\": "
            << (passed ? "true" : "false") << ",\n"
            << "  \"requestedSeconds\": "
            << durationSeconds << ",\n"
            << "  \"frames\": "
            << captured.audio.frames() << ",\n"
            << "  \"positionDiscontinuities\": "
            << captured.discontinuities << ",\n"
            << "  \"renderLoopback\": "
            << (renderLoopback ? "true" : "false") << ",\n"
            << "  \"peak\": " << peak << ",\n"
            << "  \"rms\": " << rms << "\n"
            << "}\n";
        std::wcout
            << L"Capture sample: " << output.wstring() << L"\n"
            << L"Format: "
            << formatDescription(format.capture) << L"\n"
            << L"Frames: " << captured.audio.frames()
            << L", discontinuities: " << captured.discontinuities
            << L", peak: " << peak
            << L", RMS: " << rms << L"\n"
            << L"Result: " << (passed ? L"PASS" : L"FAIL")
            << L"\n";
        return passed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet capture sample failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletStressPhase(
    std::wstring_view phase,
    std::uint32_t iterations,
    std::filesystem::path const& outputDirectory)
{
    try {
        if (iterations == 0 || iterations > 100) {
            throw std::runtime_error(
                "Stress iterations must be between 1 and 100.");
        }
        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        verifyPair(render.Get(), capture.Get());

        std::wcout
            << L"Stress phase: " << phase
            << L", iterations=" << iterations
            << L", diagnostics=" << diagnostics.wstring()
            << L"\n";
        if (phase == L"render-open-close") {
            runSingleEndpointOpenClose(
                render.Get(), true, iterations);
        } else if (phase == L"capture-open-close") {
            runSingleEndpointOpenClose(
                capture.Get(), false, iterations);
        } else if (
            phase == L"simultaneous" ||
            phase == L"reader-recreation") {
            const auto signal = makeTone(997.0, 0.2);
            ComPtr<IAudioClient> renderProbe;
            ComPtr<IAudioClient> captureProbe;
            FormatPointers formats;
            check(
                render->Activate(
                    __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                    reinterpret_cast<void**>(
                        renderProbe.GetAddressOf())),
                "Activate render stress probe");
            check(
                capture->Activate(
                    __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                    reinterpret_cast<void**>(
                        captureProbe.GetAddressOf())),
                "Activate capture stress probe");
            check(
                renderProbe->GetMixFormat(&formats.render),
                "GetMixFormat(render stress probe)");
            check(
                captureProbe->GetMixFormat(&formats.capture),
                "GetMixFormat(capture stress probe)");
            for (std::uint32_t iteration = 0;
                 iteration < iterations;
                 ++iteration) {
                const auto captured = runSignal(
                    render.Get(), capture.Get(), signal,
                    formats.render, formats.capture);
                const auto analysis =
                    analyzeSignal(signal, captured);
                if (!analysis.passed) {
                    throw std::runtime_error(
                        "A simultaneous stream iteration failed analysis.");
                }
            }
        } else if (phase == L"start-stop") {
            runRepeatedStartStop(
                render.Get(), capture.Get(),
                iterations, false);
        } else if (phase == L"reset-during-activity") {
            runRepeatedStartStop(
                render.Get(), capture.Get(),
                iterations, true);
        } else {
            throw std::runtime_error(
                "Unknown bounded stress phase.");
        }
        std::wcout
            << L"Stress phase PASS: " << phase << L"\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet bounded stress failed: "
            << error.what() << "\n";
        return 2;
    }
}

int RunCueletStageESoak(
    double durationSeconds,
    std::filesystem::path const& outputDirectory)
{
    try {
        if (!std::isfinite(durationSeconds) ||
            durationSeconds < 60.0 ||
            durationSeconds > 10'800.0) {
            throw std::runtime_error(
                "Stage E soak duration must be between 60 seconds and 3 hours.");
        }
        const auto diagnostics =
            diagnosticsDirectory(outputDirectory);
        std::ofstream samples(
            diagnostics / L"soak-samples.jsonl",
            std::ios::binary | std::ios::trunc);
        if (!samples) {
            throw std::runtime_error(
                "Could not create Stage E soak telemetry.");
        }

        ComLifetime com;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(
            ::CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
            "CoCreateInstance(MMDeviceEnumerator)");
        const auto render =
            findCueletEndpoint(enumerator.Get(), eRender);
        const auto capture =
            findCueletEndpoint(enumerator.Get(), eCapture);
        verifyPair(render.Get(), capture.Get());

        ComPtr<IAudioClient> renderProbe;
        ComPtr<IAudioClient> captureProbe;
        FormatPointers formats;
        check(
            render->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    renderProbe.GetAddressOf())),
            "Activate Stage E render probe");
        check(
            capture->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(
                    captureProbe.GetAddressOf())),
            "Activate Stage E capture probe");
        check(
            renderProbe->GetMixFormat(&formats.render),
            "GetMixFormat(Stage E render)");
        check(
            captureProbe->GetMixFormat(&formats.capture),
            "GetMixFormat(Stage E capture)");
        validateFormat(formats.render, "Stage E render");
        validateFormat(formats.capture, "Stage E capture");

        auto suite = makeSuite();
        const auto started = std::chrono::steady_clock::now();
        const auto stopAt =
            started +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(durationSeconds));
        const auto initialResources = runtimeResourceSnapshot();
        std::uint64_t cycles = 0;
        std::uint64_t fixtures = 0;
        std::uint64_t failedFixtures = 0;
        std::uint64_t capturedFrames = 0;
        std::uint64_t alignedFrames = 0;
        std::uint64_t clippedSamples = 0;
        std::uint64_t duplicateRuns = 0;
        std::uint64_t zeroQuanta = 0;
        std::uint64_t clickCandidates = 0;
        std::uint64_t discontinuities = 0;
        std::uint64_t excessDiscontinuities = 0;
        std::uint64_t inactiveEndpointSamples = 0;
        double minimumSnr = 300.0;
        double minimumCorrelation = 1.0;
        double maximumAbsoluteDrift = 0.0;
        double maximumOnset = 0.0;
        std::uint64_t peakWorkingSet =
            initialResources.workingSetBytes;
        std::uint64_t peakPrivateBytes =
            initialResources.privateBytes;
        std::uint64_t minimumNonpagedPool =
            initialResources.systemNonpagedPoolBytes;
        std::uint64_t maximumNonpagedPool =
            initialResources.systemNonpagedPoolBytes;
        DWORD peakHandles = initialResources.processHandles;

        std::wcout
            << L"Cuelet Stage E changing-fixture soak\n"
            << L"Requested seconds: " << durationSeconds << L"\n"
            << L"Render: " << endpointName(render.Get()) << L"\n"
            << L"Capture: " << endpointName(capture.Get()) << L"\n"
            << L"Output: " << diagnostics.wstring() << L"\n";

        bool finished = false;
        while (!finished) {
            ++cycles;
            for (auto const& signal : suite) {
                if (fixtures != 0 &&
                    std::chrono::steady_clock::now() >= stopAt) {
                    finished = true;
                    break;
                }
                const auto captured = runSignal(
                    render.Get(), capture.Get(), signal,
                    formats.render, formats.capture);
                const auto analysis =
                    signal.kind == SignalKind::Silence
                        ? analyzeSilence(captured)
                        : analyzeSignal(signal, captured);
                const auto resources = runtimeResourceSnapshot();
                const bool renderActive =
                    endpointIsActive(render.Get());
                const bool captureActive =
                    endpointIsActive(capture.Get());
                const auto elapsed =
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() -
                        started).count();

                ++fixtures;
                if (!analysis.passed) ++failedFixtures;
                capturedFrames += analysis.captureFrames;
                alignedFrames += analysis.alignedFrames;
                clippedSamples += analysis.clippedSamples;
                duplicateRuns += analysis.duplicateFrameRuns;
                zeroQuanta += analysis.zeroFilledQuanta;
                clickCandidates += analysis.clickCandidates;
                discontinuities += captured.discontinuities;
                if (captured.discontinuities > 1) {
                    excessDiscontinuities +=
                        captured.discontinuities - 1;
                }
                if (!renderActive || !captureActive) {
                    ++inactiveEndpointSamples;
                }
                if (signal.kind != SignalKind::Silence) {
                    minimumSnr =
                        std::min(minimumSnr, analysis.snrDb);
                    minimumCorrelation = std::min(
                        minimumCorrelation,
                        analysis.referenceCorrelation);
                }
                maximumAbsoluteDrift = std::max(
                    maximumAbsoluteDrift,
                    std::abs(analysis.durationDriftMs));
                maximumOnset =
                    std::max(maximumOnset, analysis.onsetMs);
                peakWorkingSet = std::max(
                    peakWorkingSet, resources.workingSetBytes);
                peakPrivateBytes = std::max(
                    peakPrivateBytes, resources.privateBytes);
                minimumNonpagedPool = std::min(
                    minimumNonpagedPool,
                    resources.systemNonpagedPoolBytes);
                maximumNonpagedPool = std::max(
                    maximumNonpagedPool,
                    resources.systemNonpagedPoolBytes);
                peakHandles =
                    std::max(peakHandles, resources.processHandles);

                samples
                    << std::fixed << std::setprecision(8)
                    << "{\"fixture\":" << fixtures
                    << ",\"cycle\":" << cycles
                    << ",\"elapsedSeconds\":" << elapsed
                    << ",\"name\":\"" << signal.name << "\""
                    << ",\"passed\":"
                    << (analysis.passed ? "true" : "false")
                    << ",\"snrDb\":" << analysis.snrDb
                    << ",\"referenceCorrelation\":"
                    << analysis.referenceCorrelation
                    << ",\"clippedSamples\":"
                    << analysis.clippedSamples
                    << ",\"duplicateFrameRuns\":"
                    << analysis.duplicateFrameRuns
                    << ",\"zeroFilledQuanta\":"
                    << analysis.zeroFilledQuanta
                    << ",\"clickCandidates\":"
                    << analysis.clickCandidates
                    << ",\"positionDiscontinuities\":"
                    << captured.discontinuities
                    << ",\"durationDriftMs\":"
                    << analysis.durationDriftMs
                    << ",\"onsetMs\":" << analysis.onsetMs
                    << ",\"captureFrames\":"
                    << analysis.captureFrames
                    << ",\"alignedFrames\":"
                    << analysis.alignedFrames
                    << ",\"renderEndpointActive\":"
                    << (renderActive ? "true" : "false")
                    << ",\"captureEndpointActive\":"
                    << (captureActive ? "true" : "false")
                    << ",\"workingSetBytes\":"
                    << resources.workingSetBytes
                    << ",\"privateBytes\":"
                    << resources.privateBytes
                    << ",\"processHandles\":"
                    << resources.processHandles
                    << ",\"systemNonpagedPoolBytes\":"
                    << resources.systemNonpagedPoolBytes
                    << "}\n";
                samples.flush();
                std::cout
                    << "soak fixture=" << fixtures
                    << " cycle=" << cycles
                    << " name=" << signal.name
                    << " elapsed=" << std::fixed
                    << std::setprecision(1) << elapsed
                    << " result="
                    << (analysis.passed ? "PASS" : "FAIL")
                    << " zero=" << analysis.zeroFilledQuanta
                    << " duplicate="
                    << analysis.duplicateFrameRuns
                    << " discontinuities="
                    << captured.discontinuities
                    << " drift-ms=" << analysis.durationDriftMs
                    << "\n";
            }
        }

        const auto finalResources = runtimeResourceSnapshot();
        const auto elapsed =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() -
                started).count();
        const bool passed =
            elapsed >= durationSeconds &&
            failedFixtures == 0 &&
            clippedSamples == 0 &&
            duplicateRuns == 0 &&
            zeroQuanta == 0 &&
            clickCandidates == 0 &&
            excessDiscontinuities == 0 &&
            inactiveEndpointSamples == 0;
        std::ofstream summary(
            diagnostics / L"soak-summary.json",
            std::ios::binary | std::ios::trunc);
        if (!summary) {
            throw std::runtime_error(
                "Could not create Stage E soak summary.");
        }
        summary
            << std::fixed << std::setprecision(8)
            << "{\n"
            << "  \"passed\": " << (passed ? "true" : "false") << ",\n"
            << "  \"requestedSeconds\": " << durationSeconds << ",\n"
            << "  \"elapsedSeconds\": " << elapsed << ",\n"
            << "  \"cycles\": " << cycles << ",\n"
            << "  \"fixtures\": " << fixtures << ",\n"
            << "  \"failedFixtures\": " << failedFixtures << ",\n"
            << "  \"capturedFrames\": " << capturedFrames << ",\n"
            << "  \"alignedFrames\": " << alignedFrames << ",\n"
            << "  \"clippedSamples\": " << clippedSamples << ",\n"
            << "  \"duplicateFrameRuns\": " << duplicateRuns << ",\n"
            << "  \"zeroFilledQuanta\": " << zeroQuanta << ",\n"
            << "  \"clickCandidates\": " << clickCandidates << ",\n"
            << "  \"positionDiscontinuities\": " << discontinuities << ",\n"
            << "  \"excessPositionDiscontinuities\": "
            << excessDiscontinuities << ",\n"
            << "  \"inactiveEndpointSamples\": "
            << inactiveEndpointSamples << ",\n"
            << "  \"minimumSnrDb\": " << minimumSnr << ",\n"
            << "  \"minimumReferenceCorrelation\": "
            << minimumCorrelation << ",\n"
            << "  \"maximumAbsoluteDurationDriftMs\": "
            << maximumAbsoluteDrift << ",\n"
            << "  \"maximumOnsetMs\": " << maximumOnset << ",\n"
            << "  \"initialWorkingSetBytes\": "
            << initialResources.workingSetBytes << ",\n"
            << "  \"finalWorkingSetBytes\": "
            << finalResources.workingSetBytes << ",\n"
            << "  \"peakWorkingSetBytes\": "
            << peakWorkingSet << ",\n"
            << "  \"initialPrivateBytes\": "
            << initialResources.privateBytes << ",\n"
            << "  \"finalPrivateBytes\": "
            << finalResources.privateBytes << ",\n"
            << "  \"peakPrivateBytes\": "
            << peakPrivateBytes << ",\n"
            << "  \"peakProcessHandles\": " << peakHandles << ",\n"
            << "  \"minimumSystemNonpagedPoolBytes\": "
            << minimumNonpagedPool << ",\n"
            << "  \"maximumSystemNonpagedPoolBytes\": "
            << maximumNonpagedPool << "\n"
            << "}\n";
        summary.close();
        std::cout
            << "Stage E soak " << (passed ? "PASS" : "FAIL")
            << ": elapsed=" << elapsed
            << " fixtures=" << fixtures
            << " failed=" << failedFixtures << "\n";
        return passed ? 0 : 3;
    } catch (std::exception const& error) {
        std::cerr
            << "Cuelet Stage E soak failed: "
            << error.what() << "\n";
        return 2;
    }
}
