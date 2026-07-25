#include "WindowsWorkflowModel.h"

#include "WindowsUtf8.h"
#include "cuelet/LibraryScanner.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <set>
#include <system_error>

namespace cuelet::windows {
namespace {

std::wstring lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

std::filesystem::path absolutePath(const std::filesystem::path& value,
                                   const std::filesystem::path& currentDirectory)
{
    if (!value.is_relative()) return value.lexically_normal();
    std::error_code error;
    const auto base = currentDirectory.empty() ? std::filesystem::current_path(error) : currentDirectory;
    if (error) return value.lexically_normal();
    return (base / value).lexically_normal();
}

std::wstring normalizedPathKey(const std::filesystem::path& value)
{
    std::error_code error;
    auto absolute = std::filesystem::absolute(value, error);
    if (error) absolute = value;
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    if (!error) absolute = std::move(canonical);
    auto text = absolute.lexically_normal().wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    while (text.size() > 3 && text.back() == L'\\') text.pop_back();
    return lower(std::move(text));
}

std::filesystem::path uniqueDestination(const std::filesystem::path& libraryFolder,
                                        const std::filesystem::path& source,
                                        std::set<std::wstring>& reserved)
{
    auto candidate = libraryFolder / source.filename();
    const auto stem = source.stem().wstring();
    const auto extension = source.extension().wstring();
    unsigned int suffix = 2;
    while (std::filesystem::exists(candidate) || reserved.count(normalizedPathKey(candidate)) != 0) {
        candidate = libraryFolder / (stem + L" (" + std::to_wstring(suffix++) + L")" + extension);
    }
    reserved.insert(normalizedPathKey(candidate));
    return candidate;
}

std::string clipPath(const SoundClip& clip)
{
    if (clip.storageMode == SoundStorageMode::Linked && !clip.externalPath.empty()) return clip.externalPath;
    return clip.absolutePath;
}

std::string duplicateFor(const std::filesystem::path& source,
                         const std::vector<SoundClip>& existingClips)
{
    for (const auto& clip : existingClips) {
        const auto actual = clipPath(clip);
        if (!actual.empty() && pathsReferToSameFile(source, std::filesystem::u8path(actual))) return clip.id;
        if (!clip.originalSourcePath.empty() &&
            pathsReferToSameFile(source, std::filesystem::u8path(clip.originalSourcePath))) return clip.id;
    }
    return {};
}

void addCandidate(const std::filesystem::path& path,
                  const std::filesystem::path& libraryFolder,
                  ImportBehavior behavior,
                  const std::string& categoryId,
                  const std::vector<SoundClip>& existingClips,
                  std::set<std::wstring>& seen,
                  std::set<std::wstring>& reservedDestinations,
                  ImportPlan& plan)
{
    const auto key = normalizedPathKey(path);
    if (!seen.insert(key).second) return;
    if (!LibraryScanner::isSupportedAudioFile(path)) {
        plan.unsupported.push_back(path);
        return;
    }

    ImportPlanEntry entry;
    entry.source = path;
    entry.behavior = behavior;
    entry.categoryId = categoryId.empty() ? "uncategorized" : categoryId;
    entry.duplicateClipId = duplicateFor(path, existingClips);
    if (behavior == ImportBehavior::Copy && entry.duplicateClipId.empty()) {
        entry.destination = uniqueDestination(libraryFolder, path, reservedDestinations);
    }
    plan.entries.push_back(std::move(entry));
}

bool setMainCommand(CliRequest& request, CliCommand command, std::wstring value = {})
{
    if (request.command != CliCommand::Launch) {
        request.command = CliCommand::Invalid;
        request.error = L"Only one primary command may be specified.";
        return false;
    }
    request.command = command;
    request.value = std::move(value);
    return true;
}

bool needsValue(const std::vector<std::wstring>& arguments, std::size_t index, CliRequest& request)
{
    if (index + 1 < arguments.size()) return true;
    request.command = CliCommand::Invalid;
    request.error = arguments[index] + L" requires a value.";
    return false;
}

} // namespace

std::wstring importBehaviorSetting(ImportBehavior behavior)
{
    return behavior == ImportBehavior::Link ? L"link" : L"copy";
}

ImportBehavior importBehaviorFromSetting(std::wstring_view value)
{
    return lower(std::wstring(value)) == L"link" ? ImportBehavior::Link : ImportBehavior::Copy;
}

std::string categoryIdForNavigationTag(std::wstring_view tag)
{
    constexpr std::wstring_view prefix = L"category:";
    if (tag.size() >= prefix.size() && tag.substr(0, prefix.size()) == prefix) {
        const auto id = tag.substr(prefix.size());
        return id.empty() ? std::string{"uncategorized"} : wideToUtf8(std::wstring{id});
    }
    return "uncategorized";
}

bool reassignExistingSound(std::vector<cuelet::SoundClip>& clips,
                           std::string_view soundId,
                           std::string_view categoryId)
{
    const auto found = std::find_if(clips.begin(), clips.end(), [&](auto const& clip) {
        return clip.id == soundId;
    });
    if (found == clips.end()) return false;
    found->categoryId = categoryId.empty() ? "uncategorized" : std::string{categoryId};
    return true;
}

ImportPlan makeImportPlan(const std::vector<std::filesystem::path>& inputs,
                          const std::filesystem::path& libraryFolder,
                          ImportBehavior behavior,
                          bool scanSubfolders,
                          std::string categoryId,
                          const std::vector<cuelet::SoundClip>& existingClips)
{
    ImportPlan plan;
    std::set<std::wstring> seen;
    std::set<std::wstring> reservedDestinations;
    for (const auto& input : inputs) {
        std::error_code error;
        if (!std::filesystem::exists(input, error)) {
            plan.missing.push_back(input);
            continue;
        }
        if (std::filesystem::is_regular_file(input, error)) {
            addCandidate(input, libraryFolder, behavior, categoryId, existingClips,
                         seen, reservedDestinations, plan);
            continue;
        }
        if (!std::filesystem::is_directory(input, error)) {
            plan.unsupported.push_back(input);
            continue;
        }

        if (scanSubfolders) {
            for (std::filesystem::recursive_directory_iterator iterator(
                     input, std::filesystem::directory_options::skip_permission_denied, error), end;
                 iterator != end; iterator.increment(error)) {
                if (error) {
                    error.clear();
                    continue;
                }
                if (iterator->is_regular_file(error)) {
                    addCandidate(iterator->path(), libraryFolder, behavior, categoryId, existingClips,
                                 seen, reservedDestinations, plan);
                }
            }
        } else {
            for (std::filesystem::directory_iterator iterator(
                     input, std::filesystem::directory_options::skip_permission_denied, error), end;
                 iterator != end; iterator.increment(error)) {
                if (error) {
                    error.clear();
                    continue;
                }
                if (iterator->is_regular_file(error)) {
                    addCandidate(iterator->path(), libraryFolder, behavior, categoryId, existingClips,
                                 seen, reservedDestinations, plan);
                }
            }
        }
    }
    return plan;
}

std::string linkedMetadataKey(const std::filesystem::path& source)
{
    const auto stableSource = wideToUtf8(normalizedPathKey(source));
    return "@linked/" + stableIdForPath("linked:" + stableSource);
}

bool pathsReferToSameFile(const std::filesystem::path& left,
                          const std::filesystem::path& right) noexcept
{
    if (normalizedPathKey(left) == normalizedPathKey(right)) return true;

    const auto open = [](const std::filesystem::path& path) {
        return ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    };
    const auto leftHandle = open(left);
    if (leftHandle == INVALID_HANDLE_VALUE) return false;
    const auto rightHandle = open(right);
    if (rightHandle == INVALID_HANDLE_VALUE) {
        ::CloseHandle(leftHandle);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION leftInfo{};
    BY_HANDLE_FILE_INFORMATION rightInfo{};
    const bool result = ::GetFileInformationByHandle(leftHandle, &leftInfo) &&
                        ::GetFileInformationByHandle(rightHandle, &rightInfo) &&
                        leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber &&
                        leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh &&
                        leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
    ::CloseHandle(rightHandle);
    ::CloseHandle(leftHandle);
    return result;
}

bool renameFileTransaction(const std::filesystem::path& oldPath,
                           const std::filesystem::path& newPath,
                           const std::function<bool()>& commitMetadata,
                           std::string* error)
{
    std::error_code filesystemError;
    if (std::filesystem::exists(newPath, filesystemError)) {
        if (!filesystemError && pathsReferToSameFile(oldPath, newPath)) {
            if (commitMetadata()) return true;
            if (error) *error = "Metadata could not be saved.";
            return false;
        }
        if (error) *error = filesystemError
            ? filesystemError.message()
            : "A file with that name already exists.";
        return false;
    }
    filesystemError.clear();
    std::filesystem::rename(oldPath, newPath, filesystemError);
    if (filesystemError) {
        if (error) *error = filesystemError.message();
        return false;
    }
    if (commitMetadata()) return true;

    std::filesystem::rename(newPath, oldPath, filesystemError);
    if (error) {
        *error = filesystemError
            ? "Metadata could not be saved and the file rename could not be rolled back: " + filesystemError.message()
            : "Metadata could not be saved; the file rename was rolled back.";
    }
    return false;
}

std::filesystem::path renamedSoundPath(const std::filesystem::path& oldPath,
                                       std::wstring_view newStem)
{
    return oldPath.parent_path() /
           (std::wstring{newStem} + oldPath.extension().wstring());
}

void applyRenamedSoundMetadata(cuelet::SoundClip& clip,
                               const std::filesystem::path& newPath,
                               const std::filesystem::path& libraryFolder)
{
    clip.absolutePath = wideToUtf8(newPath.wstring());
    clip.filename = wideToUtf8(newPath.filename().wstring());
    clip.sourceFileName = clip.filename;
    clip.displayName = displayNameFromFilename(clip.filename);
    if (clip.storageMode == SoundStorageMode::Linked) {
        clip.externalPath = clip.absolutePath;
        clip.originalSourcePath = clip.absolutePath;
    } else {
        clip.relativePath = wideToUtf8(
            newPath.lexically_relative(libraryFolder).generic_wstring());
    }
    clip.durationSourcePath.clear();
}

bool durationCacheIsValid(const cuelet::SoundClip& clip,
                          std::string_view sourcePath,
                          std::uint64_t fileSize,
                          std::int64_t modifiedSeconds) noexcept
{
    return clip.durationKnown &&
           clip.durationSourcePath == sourcePath &&
           clip.durationFileSize == fileSize &&
           clip.durationModifiedSeconds == modifiedSeconds;
}

std::wstring formatDurationLabel(double seconds, bool known)
{
    if (!known || !std::isfinite(seconds) || seconds < 0.0) return L"\u2014";
    const auto rounded = static_cast<std::uint64_t>(seconds + 0.5);
    const auto minutes = rounded / 60;
    const auto remainder = rounded % 60;
    wchar_t buffer[48]{};
    swprintf_s(buffer, L"%llu:%02llu",
               static_cast<unsigned long long>(minutes),
               static_cast<unsigned long long>(remainder));
    return buffer;
}

std::optional<std::chrono::milliseconds> notificationDismissDelay(NotificationKind kind)
{
    using namespace std::chrono_literals;
    switch (kind) {
    case NotificationKind::Success: return 2750ms;
    case NotificationKind::Information: return 4000ms;
    case NotificationKind::Warning: return 6000ms;
    case NotificationKind::Error: return std::nullopt;
    }
    return std::nullopt;
}

LibraryStartupState libraryStartupState(const std::filesystem::path& configuredLibrary)
{
    if (configuredLibrary.empty()) return LibraryStartupState::NeedsOnboarding;
    std::error_code error;
    return std::filesystem::is_directory(configuredLibrary, error)
        ? LibraryStartupState::Ready : LibraryStartupState::ConfiguredLibraryMissing;
}

CliRequest parseCommandLine(const std::vector<std::wstring>& arguments,
                            const std::filesystem::path& currentDirectory)
{
    CliRequest request;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        const auto option = lower(argument);
        if (option == L"--json") {
            request.json = true;
        } else if (option == L"--help" || option == L"-h" || option == L"/?") {
            if (!setMainCommand(request, CliCommand::Help)) break;
        } else if (option == L"--list-sounds") {
            if (!setMainCommand(request, CliCommand::ListSounds)) break;
        } else if (option == L"--list-categories") {
            if (!setMainCommand(request, CliCommand::ListCategories)) break;
        } else if (option == L"--play-id" || option == L"--play-name" || option == L"--play-file" ||
                   option == L"--stop" || option == L"--reveal-id") {
            if (!needsValue(arguments, index, request)) break;
            auto value = arguments[++index];
            const auto command = option == L"--play-id" ? CliCommand::PlayId
                : option == L"--play-name" ? CliCommand::PlayName
                : option == L"--play-file" ? CliCommand::PlayFile
                : option == L"--stop" ? CliCommand::Stop : CliCommand::RevealId;
            if (command == CliCommand::PlayFile) value = absolutePath(value, currentDirectory).wstring();
            if (!setMainCommand(request, command, value)) break;
        } else if (option == L"--stop-all") {
            if (!setMainCommand(request, CliCommand::StopAll)) break;
        } else if (option == L"--show") {
            if (!setMainCommand(request, CliCommand::Show)) break;
        } else if (option == L"--hide") {
            if (!setMainCommand(request, CliCommand::Hide)) break;
        } else if (option == L"--rescan") {
            if (!setMainCommand(request, CliCommand::Rescan)) break;
        } else if (option == L"--demo") {
            if (!setMainCommand(request, CliCommand::Demo)) break;
        } else if (option == L"--library" || option == L"--use-library") {
            if (!needsValue(arguments, index, request)) break;
            request.library = absolutePath(arguments[++index], currentDirectory);
            if (option == L"--use-library" && !setMainCommand(request, CliCommand::UseLibrary)) break;
        } else if (option == L"--create-library") {
            if (!needsValue(arguments, index, request)) break;
            request.library = absolutePath(arguments[++index], currentDirectory);
            if (!setMainCommand(request, CliCommand::CreateLibrary)) break;
        } else if (option == L"--import") {
            if (!needsValue(arguments, index, request)) break;
            if (request.command == CliCommand::Launch) request.command = CliCommand::Import;
            if (request.command != CliCommand::Import) {
                request.command = CliCommand::Invalid;
                request.error = L"--import cannot be combined with another primary command.";
                break;
            }
            request.importPaths.push_back(absolutePath(arguments[++index], currentDirectory));
        } else if (option == L"--mode") {
            if (!needsValue(arguments, index, request)) break;
            const auto mode = lower(arguments[++index]);
            if (mode != L"copy" && mode != L"link") {
                request.command = CliCommand::Invalid;
                request.error = L"--mode must be copy or link.";
                break;
            }
            request.importBehavior = mode == L"link" ? ImportBehavior::Link : ImportBehavior::Copy;
            request.importBehaviorSpecified = true;
        } else if (option == L"--category") {
            if (!needsValue(arguments, index, request)) break;
            request.categoryName = arguments[++index];
        } else if (!argument.empty() && argument[0] != L'-' && arguments.size() == 1) {
            request.library = absolutePath(argument, currentDirectory);
            request.command = CliCommand::UseLibrary;
        } else {
            request.command = CliCommand::Invalid;
            request.error = L"Unknown argument: " + argument;
            break;
        }
    }

    if (request.command == CliCommand::Launch && !request.library.empty()) request.command = CliCommand::UseLibrary;
    if ((request.importBehaviorSpecified || !request.categoryName.empty()) && request.command != CliCommand::Import) {
        request.command = CliCommand::Invalid;
        request.error = L"--mode and --category may only be used with --import.";
    }
    if (request.command == CliCommand::Import && request.importPaths.empty()) {
        request.command = CliCommand::Invalid;
        request.error = L"--import requires at least one path.";
    }
    return request;
}

std::wstring cliHelpText()
{
    return
        L"Cuelet for Windows\n\n"
        L"Usage: Cuelet.exe [command] [options]\n\n"
        L"  --help                         Show this help\n"
        L"  --list-sounds [--json]         List sounds in the active library\n"
        L"  --list-categories [--json]     List categories\n"
        L"  --play-id <id>                 Play a sound by stable ID\n"
        L"  --play-name <name>             Play the best exact/prefix name match\n"
        L"  --play-file <path>             Play a supported audio file\n"
        L"  --stop <id>                    Stop a sound by ID\n"
        L"  --stop-all                     Stop all playback\n"
        L"  --show | --hide                Show or hide the Cuelet window\n"
        L"  --rescan                       Rescan the active library\n"
        L"  --library <folder>             Use this library for the command/launch\n"
        L"  --import <path> [...]          Import one or more files or folders\n"
        L"    --mode copy|link             Override the saved import behavior\n"
        L"    --category <name>            Assign imported sounds to a category\n"
        L"  --reveal-id <id>               Reveal a sound in File Explorer\n"
        L"  --create-library <folder>      Create and use a Cuelet library\n"
        L"  --use-library <folder>         Validate and use an existing library\n"
        L"  --demo                         Play the first available library sound\n";
}

bool addHiddenFileAttribute(const std::filesystem::path& path, DWORD* errorCode) noexcept
{
    const auto attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (errorCode) *errorCode = ::GetLastError();
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) {
        if (errorCode) *errorCode = ERROR_SUCCESS;
        return true;
    }
    if (!::SetFileAttributesW(path.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN)) {
        if (errorCode) *errorCode = ::GetLastError();
        return false;
    }
    if (errorCode) *errorCode = ERROR_SUCCESS;
    return true;
}

bool hasHiddenFileAttribute(const std::filesystem::path& path) noexcept
{
    const auto attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

} // namespace cuelet::windows
