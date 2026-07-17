#pragma once

#include "cuelet/SoundTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cuelet {

struct ScanResult {
    std::vector<SoundClip> clips;
    std::vector<std::string> unsupportedFiles;
    std::string warning;
};

class LibraryScanner {
public:
    static bool isSupportedAudioFile(const std::filesystem::path& path);
    static std::vector<std::string> supportedExtensions();
    static std::string normalizeRelativePath(const std::filesystem::path& root,
                                             const std::filesystem::path& file);

    ScanResult scan(const std::filesystem::path& libraryFolder, bool recursive) const;
};

} // namespace cuelet
