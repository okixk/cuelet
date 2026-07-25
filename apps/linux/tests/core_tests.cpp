#include "cuelet/LibraryScanner.h"
#include "cuelet/MetadataStore.h"
#include "cuelet/SoundSearch.h"
#include "TestSupport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

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
    CUELET_REQUIRE(flat.clips.size() == 1);
    CUELET_REQUIRE(flat.unsupportedFiles.size() == 1);

    auto recursive = scanner.scan(root, true);
    CUELET_REQUIRE(recursive.clips.size() == 2);

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
    CUELET_REQUIRE(store.save(cuelet::MetadataStore::metadataFromClips({clip}, categories)));
    auto loaded = store.load();
    CUELET_REQUIRE(loaded.categories.size() == 2);
    const auto category = std::find_if(loaded.categories.begin(), loaded.categories.end(), [](const cuelet::Category& item) {
        return item.id == "user-fx-123456";
    });
    CUELET_REQUIRE(category != loaded.categories.end());
    CUELET_REQUIRE(category->colorHex == "#3478F6");
    CUELET_REQUIRE(category->iconName == "tag-symbolic");
    CUELET_REQUIRE(loaded.soundsByRelativePath.count("fx/hit.wav") == 1);
    CUELET_REQUIRE(loaded.soundsByRelativePath["fx/hit.wav"].favorite);
    CUELET_REQUIRE(loaded.soundsByRelativePath["fx/hit.wav"].shortcut.has_value());
    CUELET_REQUIRE(loaded.soundsByRelativePath["fx/hit.wav"].aliases.size() == 2);

    auto editedCategories = categories;
    editedCategories[1].name = "Sound Effects";
    editedCategories[1].colorHex = "#AF52DE";
    editedCategories[1].iconName = "music-note";
    CUELET_REQUIRE(store.save(cuelet::MetadataStore::metadataFromClips({clip}, editedCategories)));
    loaded = store.load();
    const auto editedCategory = std::find_if(loaded.categories.begin(), loaded.categories.end(), [](const cuelet::Category& item) {
        return item.id == "user-fx-123456";
    });
    CUELET_REQUIRE(editedCategory != loaded.categories.end());
    CUELET_REQUIRE(editedCategory->name == "Sound Effects");
    CUELET_REQUIRE(editedCategory->colorHex == "#AF52DE");
    CUELET_REQUIRE(editedCategory->iconName == "music-note");

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
    CUELET_REQUIRE(result.size() == 1);

    options.searchText = "storm";
    result = cuelet::filterAndSortSounds({clip}, categories, options);
    CUELET_REQUIRE(result.size() == 1);

    options.scope = cuelet::LibraryScope::Favorites;
    result = cuelet::filterAndSortSounds({clip}, categories, options);
    CUELET_REQUIRE(result.size() == 1);
}

cuelet::SoundClip searchableClip(
    std::string id,
    std::string displayName,
    std::string filename = "neutral.wav")
{
    cuelet::SoundClip clip;
    clip.id = std::move(id);
    clip.relativePath = filename;
    clip.filename = std::move(filename);
    clip.displayName = std::move(displayName);
    return clip;
}

void searchRankingCoversEveryMatchClass()
{
    const std::vector<cuelet::Category> categories = {
        cuelet::uncategorizedCategory(),
        {"effects", "Effects", "#3478F6", "tag", true},
    };

    auto exactName = searchableClip("name", "Door Knock");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        exactName, categories, "door knock") == 10000);

    auto exactFilename = searchableClip("filename", "Neutral", "door-knock");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        exactFilename, categories, "door knock") == 9500);

    auto exactAlias = searchableClip("alias", "Neutral");
    exactAlias.aliases = {"Door Knock"};
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        exactAlias, categories, "door knock") == 9000);

    auto namePrefix = searchableClip("name-prefix", "Door Knock Long");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        namePrefix, categories, "door") == 8500);

    auto filenamePrefix = searchableClip("filename-prefix", "Neutral", "door-long.wav");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        filenamePrefix, categories, "door") == 8000);

    auto aliasPrefix = searchableClip("alias-prefix", "Neutral");
    aliasPrefix.aliases = {"door chime"};
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        aliasPrefix, categories, "door") == 7500);

    auto wordPrefix = searchableClip("word-prefix", "Old Knock");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        wordPrefix, categories, "kno") == 7000);

    auto nameContains = searchableClip("name-contains", "Indoor Chime");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        nameContains, categories, "door") == 6000);

    auto filenameContains = searchableClip(
        "filename-contains", "Neutral", "indoor.wav");
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        filenameContains, categories, "door") == 5500);

    auto aliasContains = searchableClip("alias-contains", "Neutral");
    aliasContains.aliases = {"indoor ambience"};
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        aliasContains, categories, "door") == 5000);

    auto categoryContains = searchableClip("category", "Neutral");
    categoryContains.categoryId = "effects";
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        categoryContains, categories, "ffect") == 4000);
    CUELET_REQUIRE(cuelet::soundSearchRankingScore(
        categoryContains, categories, "missing") == 0);

    CUELET_REQUIRE(cuelet::categoryForId(categories, "effects") != nullptr);
    CUELET_REQUIRE(cuelet::categoryForId(categories, "missing") == nullptr);
    CUELET_REQUIRE(cuelet::bestMatchingSound(
        {exactName}, categories, "   ") == nullptr);

    auto tieHigh = searchableClip("z-id", "Same Name");
    auto tieLow = searchableClip("a-id", "Same Name");
    const std::vector<cuelet::SoundClip> ties = {tieHigh, tieLow};
    const auto* best = cuelet::bestMatchingSound(ties, categories, "same name");
    CUELET_REQUIRE(best != nullptr);
    CUELET_REQUIRE(best->id == "a-id");
}

void filtersScopesAndEverySortMode()
{
    const std::vector<cuelet::Category> categories = {
        cuelet::uncategorizedCategory(),
        {"ambience", "Ambience", "#009688", "weather-showers", true},
        {"effects", "Effects", "#3478F6", "bolt", true},
    };
    auto alpha = searchableClip("alpha", "Alpha", "z-alpha.wav");
    alpha.categoryId = "effects";
    alpha.favorite = true;
    alpha.addedAt = 30;
    alpha.lastPlayedAt = 100;
    alpha.durationKnown = true;
    alpha.durationSeconds = 3.0;
    alpha.notes = "soft layered";

    auto beta = searchableClip("beta", "Beta", "beta.wav");
    beta.categoryId = "ambience";
    beta.addedAt = 10;
    beta.lastPlayedAt = 200;
    beta.durationKnown = true;
    beta.durationSeconds = 1.0;
    beta.aliases = {"soft rain"};

    auto gamma = searchableClip("gamma", "Gamma", "gamma.wav");
    gamma.addedAt = 20;
    gamma.durationKnown = false;
    gamma.durationSeconds = 0.0;

    const std::vector<cuelet::SoundClip> clips = {alpha, beta, gamma};
    cuelet::FilterOptions options;

    options.scope = cuelet::LibraryScope::Favorites;
    CUELET_REQUIRE(cuelet::filterAndSortSounds(
        clips, categories, options).front().id == "alpha");

    options.scope = cuelet::LibraryScope::Recent;
    auto result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.size() == 2);
    CUELET_REQUIRE(result[0].id == "beta");
    CUELET_REQUIRE(result[1].id == "alpha");

    options.scope = cuelet::LibraryScope::Category;
    options.categoryId = "ambience";
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.size() == 1 && result.front().id == "beta");

    options.scope = cuelet::LibraryScope::AllCategories;
    options.categoryId.clear();
    options.searchText = "soft";
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.size() == 2);

    options.scope = cuelet::LibraryScope::All;
    options.searchText.clear();
    options.sort = cuelet::SortOption::NameDescending;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.front().id == "gamma");

    options.sort = cuelet::SortOption::LatestAdded;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.front().id == "alpha");

    options.sort = cuelet::SortOption::OldestAdded;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.front().id == "beta");

    options.sort = cuelet::SortOption::DurationShortest;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result[0].id == "beta");
    CUELET_REQUIRE(result.back().id == "gamma");

    options.sort = cuelet::SortOption::DurationLongest;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result[0].id == "alpha");
    CUELET_REQUIRE(result.back().id == "gamma");

    options.sort = cuelet::SortOption::Category;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result[0].id == "beta");
    CUELET_REQUIRE(result[1].id == "alpha");
    CUELET_REQUIRE(result[2].id == "gamma");

    options.searchText = "alpha";
    options.sort = cuelet::SortOption::NameAscending;
    result = cuelet::filterAndSortSounds(clips, categories, options);
    CUELET_REQUIRE(result.size() == 1 && result.front().id == "alpha");
}

void soundTypeUtilitiesHandleBoundaryValues()
{
    const cuelet::Shortcut emptyShortcut{};
    const cuelet::Shortcut firstShortcut{42, 3, "Ctrl+K"};
    const cuelet::Shortcut sameShortcut{42, 3, "Different label"};
    const cuelet::Shortcut otherShortcut{43, 3, "Ctrl+L"};
    CUELET_REQUIRE(emptyShortcut.empty());
    CUELET_REQUIRE(!firstShortcut.empty());
    CUELET_REQUIRE(firstShortcut.sameCombination(sameShortcut));
    CUELET_REQUIRE(!firstShortcut.sameCombination(otherShortcut));

    cuelet::SoundClip unnamed;
    unnamed.filename = "soft-rain.wav";
    CUELET_REQUIRE(unnamed.searchableName() == "soft-rain");
    unnamed.displayName = "Rain";
    CUELET_REQUIRE(unnamed.searchableName() == "Rain");

    const auto uncategorized = cuelet::uncategorizedCategory();
    CUELET_REQUIRE(uncategorized.id == "uncategorized");
    CUELET_REQUIRE(!uncategorized.editable);
    CUELET_REQUIRE(cuelet::availableCategoryColors().size() >= 9);
    CUELET_REQUIRE(cuelet::availableCategoryIcons().size() >= 16);
    CUELET_REQUIRE(cuelet::canonicalCategoryIconId("") == "tag");
    CUELET_REQUIRE(cuelet::canonicalCategoryIconId("bell") == "bell");
    CUELET_REQUIRE(cuelet::canonicalCategoryIconId("folder-symbolic") == "folder");
    CUELET_REQUIRE(cuelet::canonicalCategoryIconId("unknown") == "tag");

    const auto normalProgress = cuelet::makePlaybackProgress(3.0, 10.0);
    CUELET_REQUIRE(normalProgress.positionSeconds == 3.0);
    CUELET_REQUIRE(normalProgress.durationSeconds == 10.0);
    CUELET_REQUIRE(normalProgress.fraction == 0.3);
    const auto clampedProgress = cuelet::makePlaybackProgress(20.0, 10.0);
    CUELET_REQUIRE(clampedProgress.positionSeconds == 10.0);
    CUELET_REQUIRE(clampedProgress.fraction == 1.0);
    const auto unknownDuration = cuelet::makePlaybackProgress(
        2.0, std::numeric_limits<double>::quiet_NaN());
    CUELET_REQUIRE(unknownDuration.positionSeconds == 2.0);
    CUELET_REQUIRE(unknownDuration.durationSeconds == 0.0);
    CUELET_REQUIRE(cuelet::shouldShowSelectionOutline(true, false));
    CUELET_REQUIRE(!cuelet::shouldShowSelectionOutline(false, true));

    const auto firstId = cuelet::stableIdForPath("fx/hit.wav");
    CUELET_REQUIRE(firstId == cuelet::stableIdForPath("fx/hit.wav"));
    CUELET_REQUIRE(firstId != cuelet::stableIdForPath("fx/other.wav"));
    CUELET_REQUIRE(firstId.rfind("00000000-0000-4000-8000-", 0) == 0);
    CUELET_REQUIRE(cuelet::stableCategoryIdForName("Sound Effects")
        == cuelet::stableCategoryIdForName("Sound Effects"));
    CUELET_REQUIRE(cuelet::stableCategoryIdForName("!!!").rfind(
        "user-category-", 0) == 0);

    CUELET_REQUIRE(cuelet::soundStorageModeName(
        cuelet::SoundStorageMode::Managed) == "managed");
    CUELET_REQUIRE(cuelet::soundStorageModeName(
        cuelet::SoundStorageMode::Linked) == "linked");
    CUELET_REQUIRE(cuelet::soundStorageModeFromName(" LINKED ")
        == cuelet::SoundStorageMode::Linked);
    CUELET_REQUIRE(cuelet::soundStorageModeFromName("other")
        == cuelet::SoundStorageMode::Managed);
    CUELET_REQUIRE(cuelet::trim(" \t hello \n") == "hello");
    CUELET_REQUIRE(cuelet::trim(" \t ") == "");
    CUELET_REQUIRE(cuelet::normalizeForSearch("  Soft--RAIN.wav ")
        == "soft rain wav");
    CUELET_REQUIRE(cuelet::filenameFromPath("/tmp/fx/hit.wav") == "hit.wav");
    CUELET_REQUIRE(cuelet::filenameFromPath("C:\\fx\\hit.wav") == "hit.wav");
    CUELET_REQUIRE(cuelet::filenameFromPath("hit.wav") == "hit.wav");
    CUELET_REQUIRE(cuelet::displayNameFromFilename("hit.wav") == "hit");
    CUELET_REQUIRE(cuelet::displayNameFromFilename(".hidden") == ".hidden");
    CUELET_REQUIRE(cuelet::displayNameFromFilename("") == "Untitled Sound");
}

} // namespace

int main()
{
    return cuelet_linux::tests::run("cuelet core tests", [] {
        scannerRespectsRecursion();
        metadataRoundTripPreservesCategoriesAndShortcuts();
        searchFindsNotesAliasesAndCategories();
        searchRankingCoversEveryMatchClass();
        filtersScopesAndEverySortMode();
        soundTypeUtilitiesHandleBoundaryValues();
    });
}
