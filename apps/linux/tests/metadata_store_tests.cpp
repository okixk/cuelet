#include "cuelet/MetadataStore.h"
#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& label)
        : path_(std::filesystem::temp_directory_path()
                / ("cuelet-" + label + "-"
                   + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    CUELET_REQUIRE(static_cast<bool>(stream));
    stream << text;
    CUELET_REQUIRE(stream.good());
}

void durationCacheRoundTrips()
{
    TemporaryDirectory temporary("metadata-duration");
    cuelet::SoundClip clip;
    clip.id = "duration-id";
    clip.relativePath = "effects/impact.wav";
    clip.filename = "impact.wav";
    clip.displayName = "Impact";
    clip.durationSeconds = 12.75;
    clip.durationKnown = true;
    clip.durationFileSize = 4'294'967'300ULL;
    clip.durationModifiedSeconds = 1'725'000'123;
    clip.durationSourcePath = "/home/example/Sounds/impact.wav";

    cuelet::SoundClip zeroLength;
    zeroLength.id = "zero-length-id";
    zeroLength.relativePath = "effects/zero.wav";
    zeroLength.filename = "zero.wav";
    zeroLength.displayName = "Zero length";
    zeroLength.durationSeconds = 0.0;
    zeroLength.durationKnown = true;

    const auto metadata = cuelet::MetadataStore::metadataFromClips(
        {clip, zeroLength}, {cuelet::uncategorizedCategory()});
    const auto generated = metadata.soundsByRelativePath.at(clip.relativePath);
    CUELET_REQUIRE(generated.durationSeconds == clip.durationSeconds);
    CUELET_REQUIRE(generated.durationKnown == clip.durationKnown);
    CUELET_REQUIRE(generated.durationFileSize == clip.durationFileSize);
    CUELET_REQUIRE(generated.durationModifiedSeconds == clip.durationModifiedSeconds);
    CUELET_REQUIRE(generated.durationSourcePath == clip.durationSourcePath);

    cuelet::MetadataStore store(temporary.path() / ".cuelet-metadata.json");
    CUELET_REQUIRE(store.save(metadata));
    const auto loaded = store.load();
    CUELET_REQUIRE(store.lastError().empty());
    const auto stored = loaded.soundsByRelativePath.at(clip.relativePath);
    CUELET_REQUIRE(std::abs(stored.durationSeconds - clip.durationSeconds) < 0.0001);
    CUELET_REQUIRE(stored.durationKnown);
    CUELET_REQUIRE(stored.durationFileSize == clip.durationFileSize);
    CUELET_REQUIRE(stored.durationModifiedSeconds == clip.durationModifiedSeconds);
    CUELET_REQUIRE(stored.durationSourcePath == clip.durationSourcePath);
    const auto storedZeroLength = loaded.soundsByRelativePath.at(zeroLength.relativePath);
    CUELET_REQUIRE(storedZeroLength.durationSeconds == 0.0);
    CUELET_REQUIRE(storedZeroLength.durationKnown);

    std::vector<cuelet::SoundClip> scanned(1);
    scanned.front().relativePath = clip.relativePath;
    cuelet::MetadataStore::applyMetadata(scanned, loaded);
    CUELET_REQUIRE(std::abs(scanned.front().durationSeconds - clip.durationSeconds) < 0.0001);
    CUELET_REQUIRE(scanned.front().durationKnown);
    CUELET_REQUIRE(scanned.front().durationFileSize == clip.durationFileSize);
    CUELET_REQUIRE(scanned.front().durationModifiedSeconds == clip.durationModifiedSeconds);
    CUELET_REQUIRE(scanned.front().durationSourcePath == clip.durationSourcePath);
}

void missingManagedPathsAreReconstructedInsideTheLibrary()
{
    TemporaryDirectory temporary("metadata-managed-path");
    const auto library = temporary.path() / "library";
    std::filesystem::create_directories(library);

    cuelet::LibraryMetadata metadata;
    cuelet::SoundMetadata managed;
    managed.displayName = "Missing managed sound";
    managed.sourceFileName = "managed.wav";
    managed.durationSeconds = 3.5;
    managed.durationKnown = true;
    metadata.soundsByRelativePath["nested/managed.wav"] = managed;
    metadata.soundsByRelativePath["../outside.wav"] = managed;

    std::vector<cuelet::SoundClip> clips;
    cuelet::MetadataStore::applyMetadata(clips, metadata, library);

    CUELET_REQUIRE(clips.size() == 2);
    const auto managedClip = std::find_if(clips.begin(), clips.end(), [](const auto& clip) {
        return clip.relativePath == "nested/managed.wav";
    });
    CUELET_REQUIRE(managedClip != clips.end());
    CUELET_REQUIRE(managedClip->absolutePath
           == std::filesystem::absolute(library / "nested/managed.wav").lexically_normal().u8string());
    CUELET_REQUIRE(managedClip->missing);
    CUELET_REQUIRE(managedClip->durationKnown);
    CUELET_REQUIRE(managedClip->durationSeconds == 3.5);

    const auto traversalClip = std::find_if(clips.begin(), clips.end(), [](const auto& clip) {
        return clip.relativePath == "../outside.wav";
    });
    CUELET_REQUIRE(traversalClip != clips.end());
    CUELET_REQUIRE(traversalClip->absolutePath.empty());
    CUELET_REQUIRE(traversalClip->missing);
}

void malformedMembersFallBackWithoutDiscardingValidSounds()
{
    TemporaryDirectory temporary("metadata-malformed");
    const auto metadataPath = temporary.path() / ".cuelet-metadata.json";
    writeText(metadataPath, R"json({
  "version": "not-a-number",
  "categories": [42, null, {"id": 7, "name": false}, {
    "id": "valid", "name": "Valid", "color": 9, "icon": [], "editable": "yes"
  }],
  "sounds": {
    "primitive.wav": 7,
    "valid.wav": {
      "soundId": false,
      "displayName": 10,
      "storageMode": [],
      "externalPath": {},
      "categoryId": "valid",
      "favorite": "yes",
      "durationSeconds": "long",
      "durationKnown": true,
      "durationFileSize": {},
      "durationModifiedSeconds": [],
      "durationSourcePath": false,
      "notes": ["bad"],
      "note": "Legacy note",
      "aliases": {"not": "an array"},
      "shortcut": {"keyval": 1e300, "modifiers": -4, "label": false},
      "addedAt": {},
      "lastPlayedAt": []
    }
  }
})json");

    cuelet::MetadataStore store(metadataPath);
    const auto loaded = store.load();
    CUELET_REQUIRE(store.lastError().empty());
    CUELET_REQUIRE(loaded.schemaVersion == 2);
    CUELET_REQUIRE(loaded.categories.size() == 2);
    CUELET_REQUIRE(loaded.categories[1].id == "valid");
    CUELET_REQUIRE(loaded.categories[1].name == "Valid");
    CUELET_REQUIRE(loaded.categories[1].colorHex == "#8E8E93");
    CUELET_REQUIRE(loaded.categories[1].iconName == "tag");
    CUELET_REQUIRE(loaded.categories[1].editable);
    CUELET_REQUIRE(loaded.soundsByRelativePath.size() == 1);

    const auto sound = loaded.soundsByRelativePath.at("valid.wav");
    CUELET_REQUIRE(sound.soundId.empty());
    CUELET_REQUIRE(sound.displayName.empty());
    CUELET_REQUIRE(sound.storageMode == cuelet::SoundStorageMode::Managed);
    CUELET_REQUIRE(sound.externalPath.empty());
    CUELET_REQUIRE(sound.categoryId == "valid");
    CUELET_REQUIRE(!sound.favorite);
    CUELET_REQUIRE(sound.durationSeconds == 0.0);
    CUELET_REQUIRE(!sound.durationKnown);
    CUELET_REQUIRE(sound.durationFileSize == 0);
    CUELET_REQUIRE(sound.durationModifiedSeconds == 0);
    CUELET_REQUIRE(sound.durationSourcePath.empty());
    CUELET_REQUIRE(sound.notes == "Legacy note");
    CUELET_REQUIRE(sound.aliases.empty());
    CUELET_REQUIRE(!sound.shortcut.has_value());
    CUELET_REQUIRE(!sound.addedAt.has_value());
    CUELET_REQUIRE(!sound.lastPlayedAt.has_value());
}

void saveReplacesExistingMetadataWithoutLeavingTemporaryFiles()
{
    TemporaryDirectory temporary("metadata-atomic");
    const auto metadataPath = temporary.path() / ".cuelet-metadata.json";
    writeText(metadataPath, R"json({"version":2,"categories":[],"sounds":{}})json");

    cuelet::SoundClip clip;
    clip.id = "replacement";
    clip.relativePath = "replacement.wav";
    clip.filename = "replacement.wav";
    clip.displayName = "Replacement";

    cuelet::MetadataStore store(metadataPath);
    CUELET_REQUIRE(store.save(cuelet::MetadataStore::metadataFromClips(
        {clip}, {cuelet::uncategorizedCategory()})));
    CUELET_REQUIRE(store.load().soundsByRelativePath.count("replacement.wav") == 1);

    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(temporary.path())) {
        ++entries;
    }
    CUELET_REQUIRE(entries == 1);
}

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet metadata store tests", [] {
        durationCacheRoundTrips();
        missingManagedPathsAreReconstructedInsideTheLibrary();
        malformedMembersFallBackWithoutDiscardingValidSounds();
        saveReplacesExistingMetadataWithoutLeavingTemporaryFiles();
    });
}
