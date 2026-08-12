#pragma once

#include "cuelet/SoundTypes.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cuelet::windows {

enum class ImportBehavior {
    Copy,
    Link,
};

std::wstring importBehaviorSetting(ImportBehavior behavior);
ImportBehavior importBehaviorFromSetting(std::wstring_view value);
std::string categoryIdForNavigationTag(std::wstring_view tag);
bool reassignExistingSound(std::vector<cuelet::SoundClip>& clips,
                           std::string_view soundId,
                           std::string_view categoryId);

struct ImportPlanEntry {
    std::filesystem::path source;
    std::filesystem::path destination;
    ImportBehavior behavior = ImportBehavior::Copy;
    std::string categoryId = "uncategorized";
    std::string duplicateClipId;
};

struct ImportPlan {
    std::vector<ImportPlanEntry> entries;
    std::vector<std::filesystem::path> unsupported;
    std::vector<std::filesystem::path> missing;
};

ImportPlan makeImportPlan(const std::vector<std::filesystem::path>& inputs,
                          const std::filesystem::path& libraryFolder,
                          ImportBehavior behavior,
                          bool scanSubfolders,
                          std::string categoryId,
                          const std::vector<cuelet::SoundClip>& existingClips);
std::string linkedMetadataKey(const std::filesystem::path& source);
bool pathsReferToSameFile(const std::filesystem::path& left,
                          const std::filesystem::path& right) noexcept;

bool renameFileTransaction(const std::filesystem::path& oldPath,
                           const std::filesystem::path& newPath,
                           const std::function<bool()>& commitMetadata,
                           std::string* error = nullptr);
std::filesystem::path renamedSoundPath(const std::filesystem::path& oldPath,
                                       std::wstring_view newStem);
void applyRenamedSoundMetadata(cuelet::SoundClip& clip,
                               const std::filesystem::path& newPath,
                               const std::filesystem::path& libraryFolder);

bool durationCacheIsValid(const cuelet::SoundClip& clip,
                          std::string_view sourcePath,
                          std::uint64_t fileSize,
                          std::int64_t modifiedSeconds) noexcept;
std::wstring formatDurationLabel(double seconds, bool known);

enum class NotificationKind {
    Success,
    Information,
    Warning,
    Error,
};

std::optional<std::chrono::milliseconds> notificationDismissDelay(NotificationKind kind);

enum class LibraryStartupState {
    Ready,
    NeedsOnboarding,
    ConfiguredLibraryMissing,
};

LibraryStartupState libraryStartupState(const std::filesystem::path& configuredLibrary);

enum class CliCommand {
    Launch,
    Help,
    ListSounds,
    ListCategories,
    PlayId,
    PlayName,
    PlayFile,
    Stop,
    StopAll,
    Show,
    Hide,
    Rescan,
    UseLibrary,
    Import,
    RevealId,
    CreateLibrary,
    Invalid,
};

struct CliRequest {
    CliCommand command = CliCommand::Launch;
    std::wstring value;
    std::filesystem::path library;
    std::vector<std::filesystem::path> importPaths;
    ImportBehavior importBehavior = ImportBehavior::Copy;
    bool importBehaviorSpecified = false;
    std::wstring categoryName;
    bool json = false;
    std::wstring error;

    bool isCliCommand() const noexcept { return command != CliCommand::Launch; }
};

struct CliExecutionResult {
    int exitCode = 0;
    std::string standardOutput;
    std::string standardError;
    bool keepRunning = false;
};

CliRequest parseCommandLine(const std::vector<std::wstring>& arguments,
                            const std::filesystem::path& currentDirectory = {});
std::wstring cliHelpText();

bool addHiddenFileAttribute(const std::filesystem::path& path, DWORD* errorCode = nullptr) noexcept;
bool hasHiddenFileAttribute(const std::filesystem::path& path) noexcept;

} // namespace cuelet::windows
