#pragma once

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cuelet {

enum class SoundStorageMode {
    Managed,
    Linked,
};

struct Shortcut {
    unsigned int keyval = 0;
    unsigned int modifiers = 0;
    std::string label;
    bool global = true;

    bool empty() const;
    bool sameCombination(const Shortcut& other) const;
};

struct Category {
    std::string id;
    std::string name;
    std::string colorHex = "#8E8E93";
    std::string iconName = "tag";
    bool editable = true;
};

struct SoundClip {
    std::string id;
    std::string absolutePath;
    std::string relativePath;
    std::string filename;
    SoundStorageMode storageMode = SoundStorageMode::Managed;
    std::string externalPath;
    std::string originalSourcePath;
    std::string sourceFileName;
    std::string displayName;
    std::string categoryId = "uncategorized";
    std::string notes;
    std::vector<std::string> aliases;
    std::optional<Shortcut> shortcut;
    bool favorite = false;
    bool missing = false;
    double durationSeconds = 0.0;
    bool durationKnown = false;
    std::uint64_t durationFileSize = 0;
    std::int64_t durationModifiedSeconds = 0;
    std::string durationSourcePath;
    std::time_t addedAt = 0;
    std::optional<std::time_t> lastPlayedAt;

    std::string searchableName() const;
};

struct SoundMetadata {
    std::string soundId;
    std::string displayName;
    SoundStorageMode storageMode = SoundStorageMode::Managed;
    std::string externalPath;
    std::string originalSourcePath;
    std::string sourceFileName;
    std::string categoryId = "uncategorized";
    std::string notes;
    std::vector<std::string> aliases;
    std::optional<Shortcut> shortcut;
    bool favorite = false;
    double durationSeconds = 0.0;
    bool durationKnown = false;
    std::uint64_t durationFileSize = 0;
    std::int64_t durationModifiedSeconds = 0;
    std::string durationSourcePath;
    std::optional<std::time_t> addedAt;
    std::optional<std::time_t> lastPlayedAt;
};

struct LibraryMetadata {
    int schemaVersion = 2;
    std::vector<Category> categories;
    std::map<std::string, SoundMetadata> soundsByRelativePath;
};

struct CategoryColorChoice {
    std::string name;
    std::string colorHex;
};

struct CategoryIconChoice {
    std::string name;
    std::string id;
};

struct PlaybackProgress {
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    double fraction = 0.0;
};

enum class LibraryScope {
    All,
    Favorites,
    Recent,
    AllCategories,
    Category,
};

enum class SortOption {
    NameAscending,
    NameDescending,
    LatestAdded,
    OldestAdded,
    DurationShortest,
    DurationLongest,
    Category,
};

struct FilterOptions {
    LibraryScope scope = LibraryScope::All;
    std::string categoryId;
    std::string searchText;
    SortOption sort = SortOption::NameAscending;
};

Category uncategorizedCategory();
const std::vector<CategoryColorChoice>& availableCategoryColors();
const std::vector<CategoryIconChoice>& availableCategoryIcons();
std::string canonicalCategoryIconId(const std::string& iconId);
PlaybackProgress makePlaybackProgress(double positionSeconds, double durationSeconds);
bool shouldShowSelectionOutline(bool selected, bool playing);
std::string stableIdForPath(const std::string& relativePath);
std::string stableCategoryIdForName(const std::string& name);
std::string soundStorageModeName(SoundStorageMode mode);
SoundStorageMode soundStorageModeFromName(const std::string& value);
std::string trim(std::string value);
std::string normalizeForSearch(const std::string& value);
std::string filenameFromPath(const std::string& path);
std::string displayNameFromFilename(const std::string& filename);

} // namespace cuelet
