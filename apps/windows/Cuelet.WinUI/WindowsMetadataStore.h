#pragma once

#include "cuelet/SoundTypes.h"

#include <filesystem>
#include <string>

namespace cuelet::windows {

struct MetadataLoadResult {
    LibraryMetadata metadata;
    std::string warning;
    bool migratedFromV1 = false;
};

class WindowsMetadataStore {
public:
    static constexpr const wchar_t* fileName = L".cuelet-metadata.json";

    MetadataLoadResult load(const std::filesystem::path& libraryFolder) const;
    bool save(const std::filesystem::path& libraryFolder,
              const LibraryMetadata& metadata,
              std::string* error = nullptr) const;
};

} // namespace cuelet::windows
