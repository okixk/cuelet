#include "services/LinuxLibraryImportService.h"

#include "cuelet/LibraryScanner.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {

namespace fs = std::filesystem;

using ErrorCode = LinuxLibraryImportService::ErrorCode;
using ImportMode = LinuxLibraryImportService::ImportMode;
using PlannedItem = LinuxLibraryImportService::PlannedItem;
using PlanDisposition = LinuxLibraryImportService::PlanDisposition;

struct NormalizedDirectory {
    fs::path path;
    bool usable = false;
};

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1)
        : value_(value)
    {
    }

    ~FileDescriptor()
    {
        if (value_ >= 0) {
            ::close(value_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const { return value_; }
    bool valid() const { return value_ >= 0; }

private:
    int value_;
};

struct SecureCopyResult {
    bool succeeded = false;
    ErrorCode error = ErrorCode::CopyFailed;
    std::string message;
};

std::string systemErrorMessage(const std::string& prefix, int errorNumber)
{
    return prefix + ": " + std::strerror(errorNumber);
}

bool sameFileIdentity(const struct stat& left, const struct stat& right)
{
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

void removeCreatedDestination(
    int directoryFd,
    int destinationFd,
    const std::string& destinationName)
{
    struct stat createdStatus {};
    struct stat currentStatus {};
    if (::fstat(destinationFd, &createdStatus) == 0
        && ::fstatat(
            directoryFd,
            destinationName.c_str(),
            &currentStatus,
            AT_SYMLINK_NOFOLLOW) == 0
        && sameFileIdentity(createdStatus, currentStatus)) {
        ::unlinkat(directoryFd, destinationName.c_str(), 0);
    }
}

SecureCopyResult copyRegularFileWithoutFollowingSymlinks(
    const fs::path& source,
    const fs::path& libraryFolder,
    const fs::path& destination,
    std::uint64_t expectedDevice,
    std::uint64_t expectedInode)
{
    FileDescriptor sourceFd(::open(
        source.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!sourceFd.valid()) {
        const int errorNumber = errno;
        return {
            false,
            errorNumber == ELOOP ? ErrorCode::UnsafePath : ErrorCode::CopyFailed,
            systemErrorMessage("The source could not be opened safely", errorNumber),
        };
    }

    struct stat sourceStatus {};
    if (::fstat(sourceFd.get(), &sourceStatus) != 0) {
        const int errorNumber = errno;
        return {
            false,
            ErrorCode::CopyFailed,
            systemErrorMessage("The source could not be inspected", errorNumber),
        };
    }
    if (!S_ISREG(sourceStatus.st_mode)) {
        return {
            false,
            ErrorCode::SourceNotRegular,
            "The source is no longer a regular file.",
        };
    }
    if (static_cast<std::uint64_t>(sourceStatus.st_dev) != expectedDevice
        || static_cast<std::uint64_t>(sourceStatus.st_ino) != expectedInode) {
        return {
            false,
            ErrorCode::UnsafePath,
            "The source changed after it was selected.",
        };
    }

    FileDescriptor directoryFd(::open(
        libraryFolder.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!directoryFd.valid()) {
        return {
            false,
            ErrorCode::LibraryUnavailable,
            systemErrorMessage(
                "The Cuelet library folder could not be opened safely", errno),
        };
    }

    const auto destinationName = destination.filename().u8string();
    FileDescriptor destinationFd(::openat(
        directoryFd.get(),
        destinationName.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        static_cast<mode_t>(sourceStatus.st_mode & 0777)));
    if (!destinationFd.valid()) {
        const int errorNumber = errno;
        return {
            false,
            errorNumber == EEXIST
                ? ErrorCode::DestinationExists
                : ErrorCode::CopyFailed,
            errorNumber == EEXIST
                ? "The managed destination already exists."
                : systemErrorMessage(
                    "The managed destination could not be created safely",
                    errorNumber),
        };
    }

    auto failAndClean = [&](const std::string& message) {
        removeCreatedDestination(
            directoryFd.get(), destinationFd.get(), destinationName);
        return SecureCopyResult{false, ErrorCode::CopyFailed, message};
    };

    std::array<char, 64 * 1024> buffer {};
    while (true) {
        const ssize_t bytesRead =
            ::read(sourceFd.get(), buffer.data(), buffer.size());
        if (bytesRead == 0) {
            break;
        }
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            return failAndClean(
                systemErrorMessage("The source could not be read", errno));
        }

        ssize_t written = 0;
        while (written < bytesRead) {
            const ssize_t bytesWritten = ::write(
                destinationFd.get(),
                buffer.data() + written,
                static_cast<std::size_t>(bytesRead - written));
            if (bytesWritten < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return failAndClean(systemErrorMessage(
                    "The managed destination could not be written", errno));
            }
            written += bytesWritten;
        }
    }

    if (::fsync(destinationFd.get()) != 0) {
        return failAndClean(systemErrorMessage(
            "The managed destination could not be synchronized", errno));
    }

    struct stat destinationStatus {};
    struct stat currentStatus {};
    if (::fstat(destinationFd.get(), &destinationStatus) != 0
        || ::fstatat(
            directoryFd.get(),
            destinationName.c_str(),
            &currentStatus,
            AT_SYMLINK_NOFOLLOW) != 0
        || !sameFileIdentity(destinationStatus, currentStatus)) {
        return failAndClean(
            "The managed destination changed while it was being copied.");
    }

    return {true, ErrorCode::None, {}};
}

fs::path normalizedAbsolutePath(const fs::path& value)
{
    std::error_code error;
    auto absolute = fs::absolute(value, error);
    if (error) {
        return value.lexically_normal();
    }

    const auto canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

fs::path lexicalAbsolutePath(const fs::path& value)
{
    std::error_code error;
    const auto absolute = fs::absolute(value, error);
    return error ? value.lexically_normal() : absolute.lexically_normal();
}

NormalizedDirectory normalizedDirectory(const fs::path& value)
{
    const auto normalized = normalizedAbsolutePath(value);
    std::error_code error;
    const bool usable = fs::is_directory(normalized, error) && !error;
    return {normalized, usable};
}

bool containsParentTraversal(const fs::path& relative)
{
    if (relative.empty() || relative.is_absolute()) {
        return true;
    }
    return std::any_of(relative.begin(), relative.end(), [](const fs::path& part) {
        return part == "..";
    });
}

bool pathIsWithin(const fs::path& root, const fs::path& candidate)
{
    const auto normalizedRoot = normalizedAbsolutePath(root);
    const auto normalizedCandidate = normalizedAbsolutePath(candidate);
    const auto relative = normalizedCandidate.lexically_relative(normalizedRoot);
    return !relative.empty() && !containsParentTraversal(relative);
}

bool safeDirectLibraryDestination(const fs::path& root, const fs::path& destination)
{
    if (destination.filename().empty() || destination.filename() == "."
        || destination.filename() == "..") {
        return false;
    }

    const auto normalizedRoot = normalizedAbsolutePath(root);
    const auto normalizedDestination = normalizedAbsolutePath(destination);
    return normalizedDestination.parent_path() == normalizedRoot
        && pathIsWithin(normalizedRoot, normalizedDestination);
}

std::string normalizedPathKey(const fs::path& path)
{
    return normalizedAbsolutePath(path).generic_u8string();
}

std::string duplicateFor(const fs::path& source,
                         const std::vector<cuelet::SoundClip>& existingClips)
{
    for (const auto& clip : existingClips) {
        // A missing entry is recoverable state, not an active duplicate.
        // Re-importing its source must be allowed so the successful result can
        // replace the stale metadata entry by relative path.
        if (clip.missing) {
            continue;
        }
        const auto actualPath =
            clip.storageMode == cuelet::SoundStorageMode::Linked && !clip.externalPath.empty()
            ? clip.externalPath
            : clip.absolutePath;
        if (!actualPath.empty()
            && LinuxLibraryImportService::pathsReferToSameFile(
                source, fs::u8path(actualPath))) {
            return clip.id;
        }
        if (!clip.originalSourcePath.empty()
            && LinuxLibraryImportService::pathsReferToSameFile(
                source, fs::u8path(clip.originalSourcePath))) {
            return clip.id;
        }
    }
    return {};
}

fs::path uniqueDestination(const fs::path& libraryFolder,
                           const fs::path& source,
                           std::set<std::string>& reserved)
{
    auto candidate = libraryFolder / source.filename();
    const auto stem = source.stem().u8string();
    const auto extension = source.extension().u8string();
    unsigned int suffix = 2;
    while (true) {
        std::error_code error;
        const bool exists = fs::exists(candidate, error);
        if (error || (!exists && reserved.count(normalizedPathKey(candidate)) == 0)) {
            break;
        }
        candidate = libraryFolder
            / fs::u8path(stem + " (" + std::to_string(suffix++) + ")" + extension);
    }
    reserved.insert(normalizedPathKey(candidate));
    return candidate;
}

PlannedItem rejectedItem(const fs::path& source,
                         ImportMode mode,
                         const std::string& categoryId,
                         ErrorCode error,
                         std::string message)
{
    PlannedItem item;
    item.source = source;
    item.mode = mode;
    item.disposition = PlanDisposition::Rejected;
    item.error = error;
    item.categoryId = categoryId;
    item.message = std::move(message);
    return item;
}

std::string effectiveCategoryId(const std::string& requested)
{
    const auto trimmed = cuelet::trim(requested);
    return trimmed.empty() ? "uncategorized" : trimmed;
}

void addCandidate(const fs::path& source,
                  const NormalizedDirectory& library,
                  const LinuxLibraryImportService::ImportRequest& request,
                  const std::string& categoryId,
                  std::set<std::string>& seenSources,
                  std::set<std::string>& reservedDestinations,
                  LinuxLibraryImportService::ImportPlan& plan)
{
    std::error_code symlinkError;
    const auto sourceStatus = fs::symlink_status(source, symlinkError);
    if (!symlinkError && fs::is_symlink(sourceStatus)) {
        plan.items.push_back(rejectedItem(
            source, request.mode, categoryId, ErrorCode::UnsafePath,
            "Symbolic links are not imported."));
        return;
    }

    const auto normalizedSource = lexicalAbsolutePath(source);
    std::error_code error;
    if (!fs::exists(normalizedSource, error) || error) {
        plan.items.push_back(rejectedItem(
            normalizedSource, request.mode, categoryId, ErrorCode::SourceMissing,
            "The source file does not exist."));
        return;
    }
    if (!fs::is_regular_file(normalizedSource, error) || error) {
        plan.items.push_back(rejectedItem(
            normalizedSource, request.mode, categoryId, ErrorCode::SourceNotRegular,
            "The source is not a regular file."));
        return;
    }
    if (!cuelet::LibraryScanner::isSupportedAudioFile(normalizedSource)) {
        plan.items.push_back(rejectedItem(
            normalizedSource, request.mode, categoryId, ErrorCode::UnsupportedFormat,
            "The source is not a supported audio file."));
        return;
    }

    FileDescriptor selectedSource(::open(
        normalizedSource.c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat selectedStatus {};
    struct stat selectedPathStatus {};
    if (!selectedSource.valid()
        || ::fstat(selectedSource.get(), &selectedStatus) != 0
        || !S_ISREG(selectedStatus.st_mode)
        || ::fstatat(
            AT_FDCWD,
            normalizedSource.c_str(),
            &selectedPathStatus,
            AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(selectedPathStatus.st_mode)
        || !sameFileIdentity(selectedStatus, selectedPathStatus)) {
        plan.items.push_back(rejectedItem(
            normalizedSource, request.mode, categoryId, ErrorCode::UnsafePath,
            "The source changed while it was being selected."));
        return;
    }
    if (!library.usable) {
        plan.items.push_back(rejectedItem(
            normalizedSource, request.mode, categoryId, ErrorCode::LibraryUnavailable,
            "The Cuelet library folder is unavailable."));
        return;
    }

    const auto sourceKey = normalizedPathKey(normalizedSource);
    if (!seenSources.insert(sourceKey).second) {
        PlannedItem duplicate;
        duplicate.source = normalizedSource;
        duplicate.mode = request.mode;
        duplicate.disposition = PlanDisposition::Duplicate;
        duplicate.categoryId = categoryId;
        duplicate.message = "The source already appears in this import batch.";
        plan.items.push_back(std::move(duplicate));
        return;
    }

    PlannedItem item;
    item.source = normalizedSource;
    item.mode = request.mode;
    item.disposition = PlanDisposition::Ready;
    item.categoryId = categoryId;
    item.sourceDevice =
        static_cast<std::uint64_t>(selectedStatus.st_dev);
    item.sourceInode =
        static_cast<std::uint64_t>(selectedStatus.st_ino);
    item.duplicateClipId = duplicateFor(normalizedSource, request.existingClips);
    if (!item.duplicateClipId.empty()) {
        item.disposition = PlanDisposition::Duplicate;
        item.message = "The source is already present in the library.";
    } else if (request.mode == ImportMode::Copy) {
        item.destination = uniqueDestination(
            library.path, normalizedSource, reservedDestinations);
        if (!safeDirectLibraryDestination(library.path, item.destination)) {
            item.destination.clear();
            item.disposition = PlanDisposition::Rejected;
            item.error = ErrorCode::UnsafePath;
            item.message = "The managed destination would leave the library folder.";
        }
    }
    plan.items.push_back(std::move(item));
}

std::vector<fs::path> directoryFiles(const fs::path& directory, bool recursive)
{
    std::vector<fs::path> paths;
    std::error_code error;
    const auto options = fs::directory_options::skip_permission_denied;
    if (recursive) {
        for (fs::recursive_directory_iterator iterator(directory, options, error), end;
             iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            const auto status = iterator->symlink_status(error);
            if (!error
                && (fs::is_regular_file(status) || fs::is_symlink(status))) {
                paths.push_back(iterator->path());
            }
            error.clear();
        }
    } else {
        for (fs::directory_iterator iterator(directory, options, error), end;
             iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            const auto status = iterator->symlink_status(error);
            if (!error
                && (fs::is_regular_file(status) || fs::is_symlink(status))) {
                paths.push_back(iterator->path());
            }
            error.clear();
        }
    }
    std::sort(paths.begin(), paths.end(), [](const fs::path& left, const fs::path& right) {
        return left.generic_u8string() < right.generic_u8string();
    });
    return paths;
}

cuelet::SoundClip clipForImport(const PlannedItem& item, const fs::path& actualPath)
{
    cuelet::SoundClip clip;
    const auto normalizedActual =
        item.mode == ImportMode::Link
        ? lexicalAbsolutePath(actualPath)
        : normalizedAbsolutePath(actualPath);
    const auto sourceName = item.source.filename().u8string();
    clip.absolutePath = normalizedActual.generic_u8string();
    clip.filename = actualPath.filename().u8string();
    clip.sourceFileName = sourceName;
    clip.displayName = cuelet::displayNameFromFilename(clip.filename);
    clip.categoryId = item.categoryId;
    clip.originalSourcePath = normalizedPathKey(item.source);
    clip.addedAt = std::time(nullptr);

    if (item.mode == ImportMode::Link) {
        clip.storageMode = cuelet::SoundStorageMode::Linked;
        clip.relativePath = LinuxLibraryImportService::linkedMetadataKey(item.source);
        clip.externalPath = clip.absolutePath;
    } else {
        clip.storageMode = cuelet::SoundStorageMode::Managed;
        clip.relativePath = item.destination.filename().generic_u8string();
    }
    clip.id = cuelet::stableIdForPath(clip.relativePath);
    return clip;
}

LinuxLibraryImportService::ImportItemResult resultFromPlan(const PlannedItem& item)
{
    LinuxLibraryImportService::ImportItemResult result;
    result.source = item.source;
    result.destination = item.destination;
    result.error = item.error;
    result.duplicateClipId = item.duplicateClipId;
    result.message = item.message;
    if (item.disposition == PlanDisposition::Duplicate) {
        result.status = LinuxLibraryImportService::ImportStatus::Duplicate;
    } else if (item.disposition == PlanDisposition::Rejected) {
        result.status = LinuxLibraryImportService::ImportStatus::Rejected;
    }
    return result;
}

} // namespace

std::size_t LinuxLibraryImportService::ImportBatchResult::succeededCount() const
{
    return static_cast<std::size_t>(std::count_if(
        items.begin(), items.end(), [](const ImportItemResult& item) {
            return item.status == ImportStatus::Imported
                || item.status == ImportStatus::Linked;
        }));
}

std::size_t LinuxLibraryImportService::ImportBatchResult::duplicateCount() const
{
    return static_cast<std::size_t>(std::count_if(
        items.begin(), items.end(), [](const ImportItemResult& item) {
            return item.status == ImportStatus::Duplicate;
        }));
}

std::size_t LinuxLibraryImportService::ImportBatchResult::failedCount() const
{
    return static_cast<std::size_t>(std::count_if(
        items.begin(), items.end(), [](const ImportItemResult& item) {
            return item.status == ImportStatus::Rejected
                || item.status == ImportStatus::Failed;
        }));
}

LinuxLibraryImportService::ImportPlan LinuxLibraryImportService::planImport(
    const ImportRequest& request)
{
    ImportPlan plan;
    const auto library = normalizedDirectory(request.libraryFolder);
    plan.libraryFolder = library.path;
    const auto categoryId = effectiveCategoryId(request.categoryId);
    std::set<std::string> seenSources;
    std::set<std::string> reservedDestinations;

    for (const auto& input : request.sources) {
        std::error_code symlinkError;
        const auto inputStatus = fs::symlink_status(input, symlinkError);
        if (!symlinkError && fs::is_symlink(inputStatus)) {
            plan.items.push_back(rejectedItem(
                input, request.mode, categoryId, ErrorCode::UnsafePath,
                "Symbolic links are not imported."));
            continue;
        }

        const auto normalizedInput = lexicalAbsolutePath(input);
        std::error_code error;
        if (!fs::exists(normalizedInput, error) || error) {
            plan.items.push_back(rejectedItem(
                normalizedInput, request.mode, categoryId, ErrorCode::SourceMissing,
                "The import source does not exist."));
            continue;
        }
        if (fs::is_regular_file(normalizedInput, error) && !error) {
            addCandidate(normalizedInput, library, request, categoryId, seenSources,
                         reservedDestinations, plan);
            continue;
        }
        error.clear();
        if (fs::is_directory(normalizedInput, error) && !error && request.acceptDirectories) {
            for (const auto& source : directoryFiles(
                     normalizedInput, request.scanSubfolders)) {
                addCandidate(source, library, request, categoryId, seenSources,
                             reservedDestinations, plan);
            }
            continue;
        }
        plan.items.push_back(rejectedItem(
            normalizedInput, request.mode, categoryId, ErrorCode::SourceNotRegular,
            "The import source is not a regular file."));
    }
    return plan;
}

LinuxLibraryImportService::ImportBatchResult LinuxLibraryImportService::executeImport(
    const ImportPlan& plan)
{
    ImportBatchResult batch;
    batch.items.reserve(plan.items.size());
    const auto library = normalizedDirectory(plan.libraryFolder);

    for (const auto& item : plan.items) {
        auto result = resultFromPlan(item);
        if (item.disposition != PlanDisposition::Ready) {
            batch.items.push_back(std::move(result));
            continue;
        }

        std::error_code error;
        const auto sourceStatus = fs::symlink_status(item.source, error);
        if (!error && fs::is_symlink(sourceStatus)) {
            result.status = ImportStatus::Failed;
            result.error = ErrorCode::UnsafePath;
            result.message = "The source became a symbolic link.";
            batch.items.push_back(std::move(result));
            continue;
        }
        error.clear();
        if (!fs::is_regular_file(item.source, error) || error) {
            result.status = ImportStatus::Failed;
            result.error = ErrorCode::SourceNotRegular;
            result.message = "The source is no longer a regular file.";
            batch.items.push_back(std::move(result));
            continue;
        }
        if (!cuelet::LibraryScanner::isSupportedAudioFile(item.source)) {
            result.status = ImportStatus::Failed;
            result.error = ErrorCode::UnsupportedFormat;
            result.message = "The source is no longer a supported audio file.";
            batch.items.push_back(std::move(result));
            continue;
        }
        if (!library.usable) {
            result.status = ImportStatus::Failed;
            result.error = ErrorCode::LibraryUnavailable;
            result.message = "The Cuelet library folder is unavailable.";
            batch.items.push_back(std::move(result));
            continue;
        }

        if (item.mode == ImportMode::Link) {
            FileDescriptor linkedSource(::open(
                item.source.c_str(),
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
            struct stat openedStatus {};
            struct stat pathStatus {};
            if (!linkedSource.valid()
                || ::fstat(linkedSource.get(), &openedStatus) != 0
                || !S_ISREG(openedStatus.st_mode)
                || ::fstatat(
                    AT_FDCWD,
                    item.source.c_str(),
                    &pathStatus,
                    AT_SYMLINK_NOFOLLOW) != 0
                || !S_ISREG(pathStatus.st_mode)
                || !sameFileIdentity(openedStatus, pathStatus)
                || static_cast<std::uint64_t>(openedStatus.st_dev)
                    != item.sourceDevice
                || static_cast<std::uint64_t>(openedStatus.st_ino)
                    != item.sourceInode) {
                result.status = ImportStatus::Failed;
                result.error = ErrorCode::UnsafePath;
                result.message =
                    "The linked source changed while it was being imported.";
                batch.items.push_back(std::move(result));
                continue;
            }
            result.status = ImportStatus::Linked;
            result.error = ErrorCode::None;
            result.clip = clipForImport(item, item.source);
            batch.items.push_back(std::move(result));
            continue;
        }

        if (!safeDirectLibraryDestination(library.path, item.destination)) {
            result.status = ImportStatus::Failed;
            result.error = ErrorCode::UnsafePath;
            result.message = "The managed destination is outside the library folder.";
            batch.items.push_back(std::move(result));
            continue;
        }
        const auto copy = copyRegularFileWithoutFollowingSymlinks(
            item.source,
            library.path,
            item.destination,
            item.sourceDevice,
            item.sourceInode);
        if (!copy.succeeded) {
            result.status = ImportStatus::Failed;
            result.error = copy.error;
            result.message = copy.message;
            batch.items.push_back(std::move(result));
            continue;
        }

        result.status = ImportStatus::Imported;
        result.error = ErrorCode::None;
        result.clip = clipForImport(item, item.destination);
        batch.items.push_back(std::move(result));
    }
    return batch;
}

std::vector<cuelet::SoundClip> LinuxLibraryImportService::mergeImportedClip(
    const std::vector<cuelet::SoundClip>& existingClips,
    cuelet::SoundClip importedClip)
{
    auto merged = existingClips;
    const auto existing = std::find_if(
        merged.begin(), merged.end(), [&](const cuelet::SoundClip& clip) {
            return clip.relativePath == importedClip.relativePath;
        });
    if (existing == merged.end()) {
        merged.push_back(std::move(importedClip));
        return merged;
    }

    importedClip.id = existing->id.empty() ? importedClip.id : existing->id;
    importedClip.displayName =
        existing->displayName.empty() ? importedClip.displayName : existing->displayName;
    importedClip.categoryId =
        existing->categoryId.empty() ? importedClip.categoryId : existing->categoryId;
    importedClip.notes = existing->notes;
    importedClip.aliases = existing->aliases;
    importedClip.shortcut = existing->shortcut;
    importedClip.favorite = existing->favorite;
    importedClip.addedAt =
        existing->addedAt == 0 ? importedClip.addedAt : existing->addedAt;
    importedClip.lastPlayedAt = existing->lastPlayedAt;
    *existing = std::move(importedClip);
    return merged;
}

std::string LinuxLibraryImportService::linkedMetadataKey(const fs::path& source)
{
    const auto stableSource = normalizedPathKey(source);
    return "@linked/" + cuelet::stableIdForPath("linked:" + stableSource);
}

bool LinuxLibraryImportService::pathsReferToSameFile(
    const fs::path& left, const fs::path& right) noexcept
{
    try {
        std::error_code error;
        if (fs::equivalent(left, right, error) && !error) {
            return true;
        }
        return normalizedPathKey(left) == normalizedPathKey(right);
    } catch (...) {
        return false;
    }
}
