#pragma once

#include "cuelet/SoundTypes.h"

#include <filesystem>
#include <string>
#include <vector>

struct LinuxSettings {
    std::string libraryPath;
    std::string viewMode = "grid";
    cuelet::SortOption sortOption = cuelet::SortOption::NameAscending;
    double volume = 0.8;
    bool allowsSimultaneousPlayback = true;
    bool showFileExtensions = false;
    bool scansSubfolders = true;
    bool showsDemoLibrary = false;
    bool copiesImportedFiles = true;
    std::string appearanceMode = "system";
    std::string outputDevice;
    std::vector<std::string> approvedLinkedPaths;
};

class LinuxSettingsStore {
public:
    LinuxSettings load() const;
    bool save(const LinuxSettings& settings) const;

    std::filesystem::path filePath() const;
    std::string lastError() const;

    static bool isLinkedPathApproved(
        const LinuxSettings& settings,
        const std::filesystem::path& path);
    static LinuxSettings approvingLinkedPath(
        const LinuxSettings& settings,
        const std::filesystem::path& path);

private:
    mutable std::string lastError_;
};
