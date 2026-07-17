#pragma once

#include "cuelet/SoundTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cuelet {

class MetadataStore {
public:
    explicit MetadataStore(std::filesystem::path metadataFile);

    LibraryMetadata load() const;
    bool save(const LibraryMetadata& metadata) const;

    std::string lastError() const;
    std::filesystem::path filePath() const;

    static std::filesystem::path metadataPathForLibrary(const std::filesystem::path& libraryFolder);
    static void applyMetadata(std::vector<SoundClip>& clips, const LibraryMetadata& metadata);
    static LibraryMetadata metadataFromClips(const std::vector<SoundClip>& clips,
                                             const std::vector<Category>& categories);

private:
    std::filesystem::path metadataFile_;
    mutable std::string lastError_;
    mutable int loadedVersion_ = 2;

    void backupLegacyFileIfNeeded() const;
};

std::vector<Category> mergeCategories(const std::vector<Category>& storedCategories,
                                      const std::vector<SoundClip>& clips);

} // namespace cuelet
