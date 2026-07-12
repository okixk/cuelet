#include "cuelet/LibraryScanner.h"
#include "cuelet/MetadataStore.h"
#include "cuelet/SoundSearch.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void writeFile(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << "test";
}

void scannerRespectsRecursion()
{
    const auto root = std::filesystem::temp_directory_path() / "cuelet-core-scanner-test";
    std::filesystem::remove_all(root);
    writeFile(root / "one.wav");
    writeFile(root / "nested" / "two.mp3");
    writeFile(root / "cover.png");

    cuelet::LibraryScanner scanner;
    auto flat = scanner.scan(root, false);
    assert(flat.clips.size() == 1);
    assert(flat.unsupportedFiles.size() == 1);

    auto recursive = scanner.scan(root, true);
    assert(recursive.clips.size() == 2);

    std::filesystem::remove_all(root);
}

void metadataRoundTripPreservesCategoriesAndShortcuts()
{
    const auto root = std::filesystem::temp_directory_path() / "cuelet-core-metadata-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    cuelet::SoundClip clip;
    clip.relativePath = "fx/hit.wav";
    clip.filename = "hit.wav";
    clip.displayName = "Impact Hit";
    clip.categoryId = "user-fx-123456";
    clip.favorite = true;
    clip.shortcut = cuelet::Shortcut{120, 5, "Ctrl+Shift+X"};
    clip.notes = "Layered impact";
    clip.aliases = {"boom", "slam"};
    clip.addedAt = 10;

    std::vector<cuelet::Category> categories = {
        cuelet::uncategorizedCategory(),
        {"user-fx-123456", "FX", "#3478F6", "tag-symbolic", true},
    };

    cuelet::MetadataStore store(root / ".cuelet-metadata.json");
    assert(store.save(cuelet::MetadataStore::metadataFromClips({clip}, categories)));
    auto loaded = store.load();
    assert(loaded.categories.size() == 2);
    const auto category = std::find_if(loaded.categories.begin(), loaded.categories.end(), [](const cuelet::Category& item) {
        return item.id == "user-fx-123456";
    });
    assert(category != loaded.categories.end());
    assert(category->colorHex == "#3478F6");
    assert(category->iconName == "tag-symbolic");
    assert(loaded.soundsByRelativePath.count("fx/hit.wav") == 1);
    assert(loaded.soundsByRelativePath["fx/hit.wav"].favorite);
    assert(loaded.soundsByRelativePath["fx/hit.wav"].shortcut.has_value());
    assert(loaded.soundsByRelativePath["fx/hit.wav"].aliases.size() == 2);

    auto editedCategories = categories;
    editedCategories[1].name = "Sound Effects";
    editedCategories[1].colorHex = "#AF52DE";
    editedCategories[1].iconName = "music-note";
    assert(store.save(cuelet::MetadataStore::metadataFromClips({clip}, editedCategories)));
    loaded = store.load();
    const auto editedCategory = std::find_if(loaded.categories.begin(), loaded.categories.end(), [](const cuelet::Category& item) {
        return item.id == "user-fx-123456";
    });
    assert(editedCategory != loaded.categories.end());
    assert(editedCategory->name == "Sound Effects");
    assert(editedCategory->colorHex == "#AF52DE");
    assert(editedCategory->iconName == "music-note");

    std::filesystem::remove_all(root);
}

void searchFindsNotesAliasesAndCategories()
{
    std::vector<cuelet::Category> categories = {
        cuelet::uncategorizedCategory(),
        {"weather", "Weather", "#009688", "tag-symbolic", true},
    };
    cuelet::SoundClip clip;
    clip.relativePath = "rain.ogg";
    clip.filename = "rain.ogg";
    clip.displayName = "Soft Rain";
    clip.categoryId = "weather";
    clip.notes = "Loopable drizzle";
    clip.aliases = {"storm"};
    clip.favorite = true;

    cuelet::FilterOptions options;
    options.searchText = "drizzle";
    auto result = cuelet::filterAndSortSounds({clip}, categories, options);
    assert(result.size() == 1);

    options.searchText = "storm";
    result = cuelet::filterAndSortSounds({clip}, categories, options);
    assert(result.size() == 1);

    options.scope = cuelet::LibraryScope::Favorites;
    result = cuelet::filterAndSortSounds({clip}, categories, options);
    assert(result.size() == 1);
}

} // namespace

int main()
{
    scannerRespectsRecursion();
    metadataRoundTripPreservesCategoriesAndShortcuts();
    searchFindsNotesAliasesAndCategories();
    std::cout << "cuelet core tests passed\n";
    return 0;
}
