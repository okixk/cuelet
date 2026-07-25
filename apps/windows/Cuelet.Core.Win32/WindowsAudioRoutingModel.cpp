#include "WindowsAudioRoutingModel.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>

namespace cuelet::windows {
namespace {

constexpr std::wstring_view cueletHardwareId = L"ROOT\\CUELETVIRTUALAUDIO";

std::wstring lowercase(std::wstring_view value)
{
    std::wstring normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return normalized;
}

bool equalsInsensitive(std::wstring_view left, std::wstring_view right)
{
    return lowercase(left) == lowercase(right);
}

bool startsWithInsensitive(std::wstring_view value, std::wstring_view prefix)
{
    const auto normalized = lowercase(value);
    const auto normalizedPrefix = lowercase(prefix);
    return normalized.rfind(normalizedPrefix, 0) == 0;
}

bool containsInsensitive(std::wstring_view value, std::wstring_view part)
{
    return lowercase(value).find(lowercase(part)) != std::wstring::npos;
}

bool hasCueletOwnership(const AudioEndpointDescriptor& endpoint)
{
    const auto prefix = std::wstring(cueletHardwareId) + L"\\";
    return endpoint.parentInstanceId.size() > prefix.size() &&
           startsWithInsensitive(endpoint.parentInstanceId, prefix);
}

bool isVbAudioCableRender(const AudioEndpointDescriptor& endpoint)
{
    return !endpoint.capture &&
        (containsInsensitive(endpoint.manufacturer, L"VB-Audio") ||
         containsInsensitive(endpoint.providerName, L"VB-Audio")) &&
        (startsWithInsensitive(endpoint.name, L"CABLE Input") ||
         startsWithInsensitive(endpoint.name, L"CABLE-A Input") ||
         startsWithInsensitive(endpoint.name, L"CABLE-B Input"));
}

bool isVbAudioCableCapture(const AudioEndpointDescriptor& endpoint)
{
    return endpoint.capture &&
        (containsInsensitive(endpoint.manufacturer, L"VB-Audio") ||
         containsInsensitive(endpoint.providerName, L"VB-Audio")) &&
        (startsWithInsensitive(endpoint.name, L"CABLE Output") ||
         startsWithInsensitive(endpoint.name, L"CABLE-A Output") ||
         startsWithInsensitive(endpoint.name, L"CABLE-B Output"));
}

std::wstring vbCableFamily(std::wstring_view name)
{
    const auto normalized = lowercase(name);
    if (normalized.rfind(L"cable-a ", 0) == 0) return L"cable-a";
    if (normalized.rfind(L"cable-b ", 0) == 0) return L"cable-b";
    if (normalized.rfind(L"cable ", 0) == 0) return L"cable";
    return {};
}

} // namespace

unsigned long volumeToSetting(double value) noexcept
{
    return static_cast<unsigned long>(std::clamp(value, 0.0, 1.0) * 1000.0);
}

double volumeFromSetting(unsigned long value) noexcept
{
    return std::clamp(static_cast<double>(value) / 1000.0, 0.0, 1.0);
}

bool looksLikeVirtualAudioEndpoint(std::wstring_view name)
{
    const auto normalized = lowercase(name);
    return normalized == L"cuelet virtual microphone input" ||
           normalized == L"cuelet virtual microphone" ||
           normalized.rfind(L"cable input", 0) == 0 ||
           normalized.rfind(L"cable output", 0) == 0 ||
           normalized.rfind(L"cable-a input", 0) == 0 ||
           normalized.rfind(L"cable-a output", 0) == 0 ||
           normalized.rfind(L"cable-b input", 0) == 0 ||
           normalized.rfind(L"cable-b output", 0) == 0;
}

AudioEndpointKind classifyAudioEndpoint(const AudioEndpointDescriptor& endpoint)
{
    if (!endpoint.enabled) {
        return endpoint.capture ? AudioEndpointKind::UnknownCapture
                                : AudioEndpointKind::UnknownRender;
    }
    if (hasCueletOwnership(endpoint)) {
        return endpoint.capture ? AudioEndpointKind::CueletVirtualCapture
                                : AudioEndpointKind::CueletVirtualRender;
    }
    if (isVbAudioCableRender(endpoint)) return AudioEndpointKind::SupportedVirtualRender;
    if (isVbAudioCableCapture(endpoint)) return AudioEndpointKind::SupportedVirtualCapture;
    return endpoint.capture ? AudioEndpointKind::PhysicalMicrophone
                            : AudioEndpointKind::LocalPlayback;
}

bool isCueletVirtualEndpoint(const AudioEndpointDescriptor& endpoint)
{
    const auto kind = classifyAudioEndpoint(endpoint);
    return kind == AudioEndpointKind::CueletVirtualRender ||
           kind == AudioEndpointKind::CueletVirtualCapture;
}

bool isSupportedVirtualEndpoint(const AudioEndpointDescriptor& endpoint)
{
    const auto kind = classifyAudioEndpoint(endpoint);
    return kind == AudioEndpointKind::CueletVirtualRender ||
           kind == AudioEndpointKind::CueletVirtualCapture ||
           kind == AudioEndpointKind::SupportedVirtualRender ||
           kind == AudioEndpointKind::SupportedVirtualCapture;
}

bool isPhysicalMicrophone(const AudioEndpointDescriptor& endpoint)
{
    return classifyAudioEndpoint(endpoint) == AudioEndpointKind::PhysicalMicrophone;
}

bool isCompatibleVirtualPair(const AudioEndpointDescriptor& render,
                             const AudioEndpointDescriptor& capture)
{
    if (render.capture || !capture.capture || !render.enabled || !capture.enabled) return false;
    const auto renderKind = classifyAudioEndpoint(render);
    const auto captureKind = classifyAudioEndpoint(capture);
    if (renderKind == AudioEndpointKind::CueletVirtualRender &&
        captureKind == AudioEndpointKind::CueletVirtualCapture) {
        return equalsInsensitive(
            render.parentInstanceId, capture.parentInstanceId);
    }
    if (renderKind == AudioEndpointKind::SupportedVirtualRender &&
        captureKind == AudioEndpointKind::SupportedVirtualCapture) {
        const auto renderFamily = vbCableFamily(render.name);
        return !renderFamily.empty() && renderFamily == vbCableFamily(capture.name);
    }
    return false;
}

std::optional<std::size_t> choosePhysicalMicrophone(
    const std::vector<AudioEndpointDescriptor>& captures,
    std::string_view persistedId,
    std::string_view defaultCommunicationsId,
    std::string_view defaultId)
{
    const auto findPhysical = [&](std::string_view id) -> std::optional<std::size_t> {
        if (id.empty()) return std::nullopt;
        for (std::size_t index = 0; index < captures.size(); ++index) {
            if (captures[index].id == id && isPhysicalMicrophone(captures[index])) return index;
        }
        return std::nullopt;
    };
    if (!persistedId.empty()) {
        if (const auto persisted = findPhysical(persistedId)) return persisted;
    }
    if (const auto communications = findPhysical(defaultCommunicationsId)) return communications;
    if (const auto systemDefault = findPhysical(defaultId)) return systemDefault;
    for (std::size_t index = 0; index < captures.size(); ++index) {
        if (isPhysicalMicrophone(captures[index])) return index;
    }
    return std::nullopt;
}

std::set<std::wstring> endpointTokens(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return std::iswalnum(character) ? static_cast<wchar_t>(std::towlower(character)) : L' ';
    });
    static const std::set<std::wstring> ignored{
        L"audio", L"device", L"input", L"output", L"speaker", L"speakers",
        L"microphone", L"recording", L"playback", L"virtual"
    };
    std::set<std::wstring> result;
    std::wstring token;
    for (const auto character : value) {
        if (character != L' ') {
            token += character;
        } else if (!token.empty()) {
            if (token.size() > 2 && ignored.find(token) == ignored.end()) result.insert(token);
            token.clear();
        }
    }
    if (!token.empty() && token.size() > 2 && ignored.find(token) == ignored.end()) result.insert(token);
    return result;
}

int virtualAudioPairScore(const AudioEndpointDescriptor& render,
                          const AudioEndpointDescriptor& capture)
{
    if (!isCompatibleVirtualPair(render, capture)) return 0;
    int score = 1;
    if (isCueletVirtualEndpoint(render) && isCueletVirtualEndpoint(capture)) score += 1000;
    if (!render.containerId.empty() &&
        equalsInsensitive(render.containerId, capture.containerId)) score += 100;
    if (!render.parentInstanceId.empty() &&
        equalsInsensitive(render.parentInstanceId, capture.parentInstanceId)) {
        score += 200;
    }
    if (!render.manufacturer.empty() &&
        equalsInsensitive(render.manufacturer, capture.manufacturer)) score += 20;
    const auto renderTokens = endpointTokens(render.name);
    const auto captureTokens = endpointTokens(capture.name);
    for (auto const& token : renderTokens) {
        if (captureTokens.find(token) != captureTokens.end()) score += 10;
    }
    return score;
}

std::optional<std::size_t> findBestVirtualCapture(
    const AudioEndpointDescriptor& render,
    const std::vector<AudioEndpointDescriptor>& captures)
{
    std::optional<std::size_t> best;
    int bestScore = 0;
    for (std::size_t index = 0; index < captures.size(); ++index) {
        const auto score = virtualAudioPairScore(render, captures[index]);
        if (score > bestScore) {
            best = index;
            bestScore = score;
        }
    }
    return best;
}

} // namespace cuelet::windows
