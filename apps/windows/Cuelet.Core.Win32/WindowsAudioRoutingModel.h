#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cuelet::windows {

struct AudioEndpointDescriptor {
    std::string id;
    std::wstring name;
    std::wstring containerId;
    std::wstring manufacturer;
    std::wstring instanceId;
    std::wstring providerName;
    std::wstring pairingId;
    bool capture = false;
    bool enabled = true;
    std::wstring parentInstanceId;
};

enum class AudioEndpointKind {
    PhysicalMicrophone,
    LocalPlayback,
    CueletVirtualRender,
    CueletVirtualCapture,
    SupportedVirtualRender,
    SupportedVirtualCapture,
    UnknownCapture,
    UnknownRender,
};

unsigned long volumeToSetting(double value) noexcept;
double volumeFromSetting(unsigned long value) noexcept;
bool looksLikeVirtualAudioEndpoint(std::wstring_view name);
AudioEndpointKind classifyAudioEndpoint(const AudioEndpointDescriptor& endpoint);
bool isCueletVirtualEndpoint(const AudioEndpointDescriptor& endpoint);
bool isSupportedVirtualEndpoint(const AudioEndpointDescriptor& endpoint);
bool isPhysicalMicrophone(const AudioEndpointDescriptor& endpoint);
bool isCompatibleVirtualPair(const AudioEndpointDescriptor& render,
                             const AudioEndpointDescriptor& capture);
std::optional<std::size_t> choosePhysicalMicrophone(
    const std::vector<AudioEndpointDescriptor>& captures,
    std::string_view persistedId,
    std::string_view defaultCommunicationsId,
    std::string_view defaultId);
int virtualAudioPairScore(const AudioEndpointDescriptor& render,
                          const AudioEndpointDescriptor& capture);
std::optional<std::size_t> findBestVirtualCapture(
    const AudioEndpointDescriptor& render,
    const std::vector<AudioEndpointDescriptor>& captures);

} // namespace cuelet::windows
