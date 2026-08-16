#include "services/LinuxSettingsStore.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* expression, int line)
{
    if (!condition) {
        throw TestFailure(
            "line " + std::to_string(line) + ": check failed: " + expression);
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

class ScopedEnvironment {
public:
    ScopedEnvironment(std::string name, std::optional<std::string> value)
        : name_(std::move(name))
    {
        if (const char* previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }

        if (value) {
            CHECK(setenv(name_.c_str(), value->c_str(), 1) == 0);
        } else {
            CHECK(unsetenv(name_.c_str()) == 0);
        }
    }

    ~ScopedEnvironment()
    {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

class TemporaryConfigHome {
public:
    TemporaryConfigHome()
    {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "cuelet-settings-test-XXXXXX").string();
        std::vector<char> writablePattern(pattern.begin(), pattern.end());
        writablePattern.push_back('\0');
        char* created = mkdtemp(writablePattern.data());
        if (!created) {
            throw TestFailure("could not create temporary config directory");
        }
        path_ = created;
    }

    ~TemporaryConfigHome()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryConfigHome(const TemporaryConfigHome&) = delete;
    TemporaryConfigHome& operator=(const TemporaryConfigHome&) = delete;

    const std::filesystem::path& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ScopedSettingsEnvironment {
private:
    ScopedEnvironment configHome_;
    ScopedEnvironment home_;

public:
    ScopedSettingsEnvironment(
        std::optional<std::string> configHome,
        std::optional<std::string> home)
        : configHome_("XDG_CONFIG_HOME", std::move(configHome))
        , home_("HOME", std::move(home))
    {
    }
};

class IsolatedSettingsFixture {
private:
    TemporaryConfigHome temporaryHome_;
    ScopedEnvironment configHome_;
    ScopedEnvironment home_;

public:
    IsolatedSettingsFixture()
        : configHome_("XDG_CONFIG_HOME", temporaryHome_.path().string())
        , home_("HOME", std::nullopt)
    {
    }

    LinuxSettingsStore store;
};

void writeSettingsFile(
    const LinuxSettingsStore& store,
    const std::string& contents)
{
    std::filesystem::create_directories(store.filePath().parent_path());
    std::ofstream output(store.filePath(), std::ios::binary | std::ios::trunc);
    CHECK(output.is_open());
    output << contents;
    CHECK(output.good());
}

std::string readSettingsFile(const LinuxSettingsStore& store)
{
    std::ifstream input(store.filePath(), std::ios::binary);
    CHECK(input.is_open());
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void checkDefaults(const LinuxSettings& settings)
{
    CHECK(settings.libraryPath.empty());
    CHECK(settings.viewMode == "grid");
    CHECK(settings.sortOption == cuelet::SortOption::NameAscending);
    CHECK(std::abs(settings.volume - 0.8) < 0.000001);
    CHECK(settings.allowsSimultaneousPlayback);
    CHECK(!settings.showFileExtensions);
    CHECK(settings.scansSubfolders);
    CHECK(settings.copiesImportedFiles);
    CHECK(settings.appearanceMode == "system");
    CHECK(settings.outputDevice.empty());
    CHECK(settings.virtualMicrophoneMode == "speakersOnly");
    CHECK(!settings.mixesPhysicalMicrophone);
    CHECK(settings.physicalMicrophoneDevice.empty());
    CHECK(std::abs(settings.virtualMicrophoneLevel - 0.25) < 0.000001);
    CHECK(std::abs(settings.physicalMicrophoneLevel - 0.25) < 0.000001);
    CHECK(settings.approvedLinkedPaths.empty());
}

void testMissingFileUsesDefaults()
{
    IsolatedSettingsFixture fixture;

    CHECK(fixture.store.filePath().parent_path().parent_path()
        == std::filesystem::path(std::getenv("XDG_CONFIG_HOME")));
    CHECK(!std::filesystem::exists(fixture.store.filePath()));
    checkDefaults(fixture.store.load());
    CHECK(fixture.store.lastError().empty());
}

void testCompleteRoundTrip()
{
    IsolatedSettingsFixture fixture;
    LinuxSettings expected;
    expected.libraryPath = "/tmp/Cuelet sounds";
    expected.viewMode = "list";
    expected.sortOption = cuelet::SortOption::DurationLongest;
    expected.volume = 0.37;
    expected.allowsSimultaneousPlayback = false;
    expected.showFileExtensions = true;
    expected.scansSubfolders = false;
    expected.copiesImportedFiles = false;
    expected.appearanceMode = "dark";
    expected.outputDevice = "alsa_output.usb-test";
    expected.virtualMicrophoneMode = "speakersAndVirtualMicrophone";
    expected.mixesPhysicalMicrophone = true;
    expected.physicalMicrophoneDevice = "alsa_input.usb-test";
    expected.virtualMicrophoneLevel = 0.42;
    expected.physicalMicrophoneLevel = 0.35;
    expected.approvedLinkedPaths = {
        "/tmp/Cuelet links/one.wav",
        "/tmp/Cuelet links/two.ogg",
    };

    CHECK(fixture.store.save(expected));
    CHECK(fixture.store.lastError().empty());
    CHECK(std::filesystem::is_regular_file(fixture.store.filePath()));
    struct stat fileStatus {};
    CHECK(::stat(fixture.store.filePath().c_str(), &fileStatus) == 0);
    CHECK((fileStatus.st_mode & 0777) == 0600);

    const LinuxSettings actual = fixture.store.load();
    CHECK(actual.libraryPath == expected.libraryPath);
    CHECK(actual.viewMode == expected.viewMode);
    CHECK(actual.sortOption == expected.sortOption);
    CHECK(std::abs(actual.volume - expected.volume) < 0.000001);
    CHECK(actual.allowsSimultaneousPlayback == expected.allowsSimultaneousPlayback);
    CHECK(actual.showFileExtensions == expected.showFileExtensions);
    CHECK(actual.scansSubfolders == expected.scansSubfolders);
    CHECK(actual.copiesImportedFiles == expected.copiesImportedFiles);
    CHECK(actual.appearanceMode == expected.appearanceMode);
    CHECK(actual.outputDevice == expected.outputDevice);
    CHECK(actual.virtualMicrophoneMode == expected.virtualMicrophoneMode);
    CHECK(actual.mixesPhysicalMicrophone == expected.mixesPhysicalMicrophone);
    CHECK(actual.physicalMicrophoneDevice == expected.physicalMicrophoneDevice);
    CHECK(std::abs(actual.virtualMicrophoneLevel - expected.virtualMicrophoneLevel) < 0.000001);
    CHECK(std::abs(actual.physicalMicrophoneLevel - expected.physicalMicrophoneLevel) < 0.000001);
    CHECK(actual.approvedLinkedPaths == expected.approvedLinkedPaths);
    CHECK(fixture.store.lastError().empty());
}

void testEveryAllowedEnumLikeValueRoundTrips()
{
    const std::vector<std::string> viewModes = {"grid", "list"};
    const std::vector<std::string> appearanceModes = {"system", "light", "dark"};
    const std::vector<cuelet::SortOption> sortOptions = {
        cuelet::SortOption::NameAscending,
        cuelet::SortOption::NameDescending,
        cuelet::SortOption::LatestAdded,
        cuelet::SortOption::OldestAdded,
        cuelet::SortOption::DurationShortest,
        cuelet::SortOption::DurationLongest,
        cuelet::SortOption::Category,
    };

    for (const auto& viewMode : viewModes) {
        for (const auto& appearanceMode : appearanceModes) {
            for (const auto sortOption : sortOptions) {
                IsolatedSettingsFixture fixture;
                LinuxSettings expected;
                expected.viewMode = viewMode;
                expected.appearanceMode = appearanceMode;
                expected.sortOption = sortOption;

                CHECK(fixture.store.save(expected));
                const LinuxSettings actual = fixture.store.load();
                CHECK(actual.viewMode == expected.viewMode);
                CHECK(actual.appearanceMode == expected.appearanceMode);
                CHECK(actual.sortOption == expected.sortOption);
            }
        }
    }
}

void testMalformedDocumentUsesDefaults()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(fixture.store, R"({"volume":)");

    checkDefaults(fixture.store.load());
    CHECK(!fixture.store.lastError().empty());
}

void testNonObjectDocumentUsesDefaults()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(fixture.store, R"(["not", "an", "object"])");

    checkDefaults(fixture.store.load());
    CHECK(!fixture.store.lastError().empty());
}

void testWrongTypesAndUnknownValuesUseFieldDefaults()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(
        fixture.store,
        R"({
            "libraryPath": 42,
            "viewMode": "tiles",
            "sortOption": "fastest",
            "volume": "loud",
            "allowsSimultaneousPlayback": "yes",
            "showFileExtensions": 1,
            "scansSubfolders": [],
            "copiesImportedFiles": null,
            "appearanceMode": "sepia",
            "outputDevice": false,
            "approvedLinkedPaths": [7, "relative.wav"]
        })");

    checkDefaults(fixture.store.load());
    CHECK(!fixture.store.lastError().empty());
}

void testInvalidFieldDoesNotDiscardValidFields()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(
        fixture.store,
        R"({
            "libraryPath": "/tmp/valid-library",
            "viewMode": false,
            "sortOption": "category",
            "volume": 0.25,
            "showFileExtensions": true,
            "appearanceMode": "light"
        })");

    const LinuxSettings loaded = fixture.store.load();
    CHECK(loaded.libraryPath == "/tmp/valid-library");
    CHECK(loaded.viewMode == "grid");
    CHECK(loaded.sortOption == cuelet::SortOption::Category);
    CHECK(std::abs(loaded.volume - 0.25) < 0.000001);
    CHECK(loaded.showFileExtensions);
    CHECK(loaded.appearanceMode == "light");
    CHECK(!fixture.store.lastError().empty());
}

void testRelativeXdgConfigHomeFallsBackToAbsoluteHome()
{
    TemporaryConfigHome temporaryHome;
    const std::string relativeConfig =
        "relative-" + temporaryHome.path().filename().string();
    ScopedSettingsEnvironment environment(relativeConfig, temporaryHome.path().string());
    LinuxSettingsStore store;

    CHECK(store.filePath()
        == temporaryHome.path() / ".config" / "cuelet" / "settings.json");
    CHECK(store.save(LinuxSettings{}));
    CHECK(std::filesystem::is_regular_file(store.filePath()));
    CHECK(!std::filesystem::exists(
        std::filesystem::current_path() / relativeConfig / "cuelet" / "settings.json"));
}

void testMissingPerUserConfigRootFailsSafely()
{
    ScopedSettingsEnvironment environment(std::nullopt, std::nullopt);
    LinuxSettingsStore store;

    CHECK(store.filePath().empty());
    checkDefaults(store.load());
    CHECK(!store.lastError().empty());
    CHECK(!store.save(LinuxSettings{}));
    CHECK(!store.lastError().empty());
}

void testSaveFailureIsReportedWithoutReplacingAnotherFile()
{
    TemporaryConfigHome temporaryHome;
    const auto blocker = temporaryHome.path() / "not-a-directory";
    {
        std::ofstream output(blocker, std::ios::binary);
        CHECK(output.is_open());
        output << "preserve me";
        CHECK(output.good());
    }

    ScopedSettingsEnvironment environment(blocker.string(), std::nullopt);
    LinuxSettingsStore store;
    CHECK(!store.save(LinuxSettings{}));
    CHECK(!store.lastError().empty());

    std::ifstream input(blocker, std::ios::binary);
    CHECK(input.is_open());
    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
    CHECK(contents == "preserve me");
}

void testLoadFailureUsesDefaultsAndReportsError()
{
    IsolatedSettingsFixture fixture;
    std::filesystem::create_directories(fixture.store.filePath());

    checkDefaults(fixture.store.load());
    CHECK(!fixture.store.lastError().empty());
}

void testVolumeIsClampedOnLoadAndSave()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(fixture.store, R"({"volume": -5})");
    CHECK(fixture.store.load().volume == 0.0);

    writeSettingsFile(fixture.store, R"({"volume": 8.25})");
    CHECK(fixture.store.load().volume == 1.0);

    LinuxSettings settings;
    settings.volume = 9.0;
    CHECK(fixture.store.save(settings));
    CHECK(fixture.store.load().volume == 1.0);

    settings.volume = -9.0;
    CHECK(fixture.store.save(settings));
    CHECK(fixture.store.load().volume == 0.0);

    settings.volume = std::nan("");
    settings.virtualMicrophoneLevel = std::nan("");
    settings.physicalMicrophoneLevel = 9.0;
    CHECK(fixture.store.save(settings));
    const auto normalized = fixture.store.load();
    CHECK(std::abs(normalized.volume - 0.8) < 0.000001);
    CHECK(std::abs(normalized.virtualMicrophoneLevel - 0.25) < 0.000001);
    CHECK(normalized.physicalMicrophoneLevel == 1.0);
}

void testInvalidValuesAreNormalizedOnSave()
{
    IsolatedSettingsFixture fixture;
    LinuxSettings settings;
    settings.viewMode = "tiles";
    settings.appearanceMode = "sepia";
    settings.sortOption = static_cast<cuelet::SortOption>(999);
    settings.approvedLinkedPaths = {
        "relative.wav",
        "/tmp/allowed.wav",
        "/tmp/allowed.wav",
        "",
    };

    CHECK(fixture.store.save(settings));
    const LinuxSettings loaded = fixture.store.load();
    CHECK(loaded.viewMode == "grid");
    CHECK(loaded.appearanceMode == "system");
    CHECK(loaded.sortOption == cuelet::SortOption::NameAscending);
    CHECK(loaded.approvedLinkedPaths
        == std::vector<std::string>{"/tmp/allowed.wav"});

    const std::string serialized = readSettingsFile(fixture.store);
    CHECK(serialized.find("\"tiles\"") == std::string::npos);
    CHECK(serialized.find("\"sepia\"") == std::string::npos);
}

void testMalformedLinkedPathApprovalArrayIsIgnoredSafely()
{
    IsolatedSettingsFixture fixture;
    writeSettingsFile(
        fixture.store,
        R"({
            "libraryPath": "/tmp/library",
            "approvedLinkedPaths": "not-an-array"
        })");

    const LinuxSettings loaded = fixture.store.load();
    CHECK(loaded.libraryPath == "/tmp/library");
    CHECK(loaded.approvedLinkedPaths.empty());
    CHECK(!fixture.store.lastError().empty());
}

void testLinkedPathApprovalIsNormalizedAndImmutable()
{
    LinuxSettings original;
    const LinuxSettings approved = LinuxSettingsStore::approvingLinkedPath(
        original, std::filesystem::path("/tmp/folder/../approved.wav"));

    CHECK(original.approvedLinkedPaths.empty());
    CHECK(approved.approvedLinkedPaths
        == std::vector<std::string>{"/tmp/approved.wav"});
    CHECK(LinuxSettingsStore::isLinkedPathApproved(
        approved, std::filesystem::path("/tmp/./approved.wav")));
    CHECK(!LinuxSettingsStore::isLinkedPathApproved(
        original, std::filesystem::path("/tmp/approved.wav")));

    const LinuxSettings duplicate = LinuxSettingsStore::approvingLinkedPath(
        approved, std::filesystem::path("/tmp/approved.wav"));
    CHECK(duplicate.approvedLinkedPaths == approved.approvedLinkedPaths);

    const LinuxSettings relative = LinuxSettingsStore::approvingLinkedPath(
        approved, std::filesystem::path("relative.wav"));
    CHECK(relative.approvedLinkedPaths == approved.approvedLinkedPaths);
}

void testLinkedPathApprovalNormalizationDoesNotFollowSymlinks()
{
    IsolatedSettingsFixture fixture;
    const auto target =
        fixture.store.filePath().parent_path() / "external-target.wav";
    const auto symlink =
        fixture.store.filePath().parent_path() / "approved-link.wav";
    std::filesystem::create_directories(target.parent_path());
    {
        std::ofstream output(target, std::ios::binary);
        CHECK(output.is_open());
        output << "target fixture";
        CHECK(output.good());
    }
    std::error_code error;
    std::filesystem::create_symlink(target, symlink, error);
    CHECK(!error);

    LinuxSettings settings;
    settings.approvedLinkedPaths = {symlink.string()};
    CHECK(fixture.store.save(settings));
    const LinuxSettings loaded = fixture.store.load();
    CHECK(loaded.approvedLinkedPaths
        == std::vector<std::string>{symlink.lexically_normal().string()});
    CHECK(LinuxSettingsStore::isLinkedPathApproved(loaded, symlink));
    CHECK(!LinuxSettingsStore::isLinkedPathApproved(loaded, target));
}

} // namespace

int main()
{
    try {
        testMissingFileUsesDefaults();
        testCompleteRoundTrip();
        testEveryAllowedEnumLikeValueRoundTrips();
        testMalformedDocumentUsesDefaults();
        testNonObjectDocumentUsesDefaults();
        testWrongTypesAndUnknownValuesUseFieldDefaults();
        testInvalidFieldDoesNotDiscardValidFields();
        testRelativeXdgConfigHomeFallsBackToAbsoluteHome();
        testMissingPerUserConfigRootFailsSafely();
        testSaveFailureIsReportedWithoutReplacingAnotherFile();
        testLoadFailureUsesDefaultsAndReportsError();
        testVolumeIsClampedOnLoadAndSave();
        testInvalidValuesAreNormalizedOnSave();
        testMalformedLinkedPathApprovalArrayIsIgnoredSafely();
        testLinkedPathApprovalIsNormalizedAndImmutable();
        testLinkedPathApprovalNormalizationDoesNotFollowSymlinks();
    } catch (const std::exception& error) {
        std::cerr << "cuelet settings store test failure: " << error.what() << '\n';
        return 1;
    }

    std::cout << "cuelet settings store tests passed\n";
    return 0;
}
