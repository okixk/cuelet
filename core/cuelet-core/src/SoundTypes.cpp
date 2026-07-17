#include "cuelet/SoundTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cuelet {

namespace {

constexpr unsigned long long fnvOffset = 14695981039346656037ull;
constexpr unsigned long long fnvPrime = 1099511628211ull;

unsigned long long fnv1a(const std::string& value)
{
    unsigned long long hash = fnvOffset;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= fnvPrime;
    }
    return hash;
}

bool isWordChar(unsigned char value)
{
    return std::isalnum(value) != 0;
}

} // namespace

bool Shortcut::empty() const
{
    return keyval == 0;
}

bool Shortcut::sameCombination(const Shortcut& other) const
{
    return keyval == other.keyval && modifiers == other.modifiers;
}

std::string SoundClip::searchableName() const
{
    return displayName.empty() ? displayNameFromFilename(filename) : displayName;
}

Category uncategorizedCategory()
{
    return Category{"uncategorized", "Uncategorized", "#8E8E93", "folder-symbolic", false};
}

const std::vector<CategoryColorChoice>& availableCategoryColors()
{
    static const std::vector<CategoryColorChoice> colors = {
        {"Gray", "#8E8E93"},
        {"Blue", "#3478F6"},
        {"Teal", "#009688"},
        {"Green", "#2E8B57"},
        {"Yellow", "#B38B00"},
        {"Orange", "#D9822B"},
        {"Red", "#D64545"},
        {"Pink", "#D65780"},
        {"Purple", "#AF52DE"},
    };
    return colors;
}

const std::vector<CategoryIconChoice>& availableCategoryIcons()
{
    // These IDs are portable metadata values. Platform frontends map them to
    // native symbols instead of persisting platform glyphs or icon names.
    static const std::vector<CategoryIconChoice> icons = {
        {"Tag", "tag"},
        {"Folder", "folder"},
        {"Music", "music-note"},
        {"Speaker", "audio-speakers"},
        {"Waveform", "waveform"},
        {"Bell", "bell"},
        {"Sparkles", "sparkles"},
        {"Weather", "weather-showers"},
        {"Game", "applications-games"},
        {"Microphone", "microphone"},
        {"Chat", "chat-message"},
        {"Star", "star"},
        {"Heart", "heart"},
        {"Bolt", "bolt"},
        {"Flame", "flame"},
        {"Smile", "face-smile"},
    };
    return icons;
}

std::string canonicalCategoryIconId(const std::string& iconId)
{
    if (iconId.empty()) return "tag";
    for (const auto& choice : availableCategoryIcons()) {
        if (iconId == choice.id) return choice.id;
    }

    static const std::vector<std::pair<std::string, std::string>> aliases = {
        {"folder-symbolic", "folder"}, {"tray", "folder"},
        {"music", "music-note"}, {"music.note", "music-note"},
        {"audio-x-generic-symbolic", "music-note"},
        {"speaker", "audio-speakers"}, {"speaker.wave.2", "audio-speakers"},
        {"audio-speakers-symbolic", "audio-speakers"},
        {"sound-wave-symbolic", "waveform"},
        {"weather", "weather-showers"}, {"cloud.rain", "weather-showers"},
        {"weather-showers-symbolic", "weather-showers"},
        {"game", "applications-games"}, {"gamecontroller", "applications-games"},
        {"applications-games-symbolic", "applications-games"},
        {"mic", "microphone"}, {"audio-input-microphone-symbolic", "microphone"},
        {"chat", "chat-message"}, {"message", "chat-message"},
        {"chat-message-new-symbolic", "chat-message"},
        {"starred-symbolic", "star"}, {"emblem-favorite-symbolic", "heart"},
        {"weather-storm-symbolic", "bolt"}, {"bolt.fill", "bolt"},
        {"flame.fill", "flame"}, {"face.smiling", "face-smile"},
        {"wand.and.stars", "sparkles"},
    };
    const auto found = std::find_if(aliases.begin(), aliases.end(), [&](const auto& alias) {
        return alias.first == iconId;
    });
    return found == aliases.end() ? "tag" : found->second;
}

PlaybackProgress makePlaybackProgress(double positionSeconds, double durationSeconds)
{
    PlaybackProgress progress;
    if (std::isfinite(durationSeconds) && durationSeconds > 0.0) {
        progress.durationSeconds = durationSeconds;
    }
    if (std::isfinite(positionSeconds) && positionSeconds > 0.0) {
        progress.positionSeconds = progress.durationSeconds > 0.0
            ? std::min(positionSeconds, progress.durationSeconds)
            : positionSeconds;
    }
    if (progress.durationSeconds > 0.0) {
        progress.fraction = std::clamp(progress.positionSeconds / progress.durationSeconds, 0.0, 1.0);
    }
    return progress;
}

bool shouldShowSelectionOutline(bool selected, bool playing)
{
    (void)playing;
    return selected;
}

std::string stableIdForPath(const std::string& relativePath)
{
    const unsigned long long hash = fnv1a(relativePath);
    std::ostringstream stream;
    stream << "00000000-0000-4000-8000-"
           << std::hex << std::setfill('0') << std::setw(12) << (hash & 0xFFFFFFFFFFFFull);
    return stream.str();
}

std::string stableCategoryIdForName(const std::string& name)
{
    std::string normalized;
    normalized.reserve(name.size());
    bool lastWasDash = false;
    for (const unsigned char byte : name) {
        if (isWordChar(byte)) {
            normalized.push_back(static_cast<char>(std::tolower(byte)));
            lastWasDash = false;
        } else if (!lastWasDash && !normalized.empty()) {
            normalized.push_back('-');
            lastWasDash = true;
        }
    }
    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        normalized = "category";
    }

    std::ostringstream stream;
    stream << "user-" << normalized << "-"
           << std::hex << std::setfill('0') << std::setw(6) << (fnv1a(name) & 0xFFFFFFull);
    return stream.str();
}

std::string trim(std::string value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string normalizeForSearch(const std::string& value)
{
    std::string normalized;
    normalized.reserve(value.size());
    bool pendingSpace = false;

    for (const unsigned char byte : value) {
        if (std::isalnum(byte) != 0) {
            if (pendingSpace && !normalized.empty()) {
                normalized.push_back(' ');
            }
            normalized.push_back(static_cast<char>(std::tolower(byte)));
            pendingSpace = false;
        } else {
            pendingSpace = true;
        }
    }

    return normalized;
}

std::string filenameFromPath(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string displayNameFromFilename(const std::string& filename)
{
    const auto dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return filename.empty() ? "Untitled Sound" : filename;
    }
    return filename.substr(0, dot);
}

} // namespace cuelet
