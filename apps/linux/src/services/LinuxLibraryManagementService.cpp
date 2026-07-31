#include "services/LinuxLibraryImportService.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <sys/stat.h>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

struct NormalizedDirectory {
    fs::path path;
    bool usable = false;
};

fs::path normalizedAbsolutePath(const fs::path& value)
{
    std::error_code error;
    const auto absolute = fs::absolute(value, error);
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
    return (error ? value : absolute).lexically_normal();
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

bool validPortableStem(const std::string& value)
{
    if (value.empty() || value == "." || value == ".." || value.size() > 200) {
        return false;
    }
    if (value.find_first_of("/\\:*?\"<>|") != std::string::npos) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f;
    });
}

bool validDisplayName(const std::string& value)
{
    if (value.empty() || value.size() > 200) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f;
    });
}

std::optional<fs::path> managedClipPath(
    const cuelet::SoundClip& clip,
    const fs::path& libraryFolder)
{
    const auto library = normalizedDirectory(libraryFolder);
    if (!library.usable || clip.relativePath.empty()) {
        return std::nullopt;
    }
    const auto relative = fs::u8path(clip.relativePath);
    if (containsParentTraversal(relative)) {
        return std::nullopt;
    }

    const auto lexicalLibrary = lexicalAbsolutePath(libraryFolder);
    const auto fromRelative = (lexicalLibrary / relative).lexically_normal();
    const auto actual = clip.absolutePath.empty()
        ? fromRelative
        : lexicalAbsolutePath(fs::u8path(clip.absolutePath));
    const auto lexicalRelative = fromRelative.lexically_relative(lexicalLibrary);
    if (containsParentTraversal(lexicalRelative)
        || actual != fromRelative
        || !pathIsWithin(library.path, normalizedAbsolutePath(fromRelative))) {
        return std::nullopt;
    }

    auto inspected = lexicalLibrary;
    for (const auto& part : relative) {
        inspected /= part;
        std::error_code statusError;
        const auto status = fs::symlink_status(inspected, statusError);
        if (statusError) {
            if (inspected == fromRelative
                && statusError == std::errc::no_such_file_or_directory) {
                continue;
            }
            return std::nullopt;
        }
        if (fs::is_symlink(status)
            || (inspected != fromRelative && !fs::is_directory(status))) {
            return std::nullopt;
        }
    }
    return actual;
}

void invalidateDuration(cuelet::SoundClip& clip)
{
    clip.durationSeconds = 0.0;
    clip.durationKnown = false;
    clip.durationFileSize = 0;
    clip.durationModifiedSeconds = 0;
    clip.durationSourcePath.clear();
}

} // namespace

LinuxLibraryImportService::RemovalPlan LinuxLibraryImportService::planRemoval(
    const cuelet::SoundClip& clip,
    const fs::path& libraryFolder,
    RemovalMode mode)
{
    RemovalPlan plan;
    plan.metadataKey = clip.relativePath;
    if (plan.metadataKey.empty()) {
        plan.message = "The sound has no metadata key.";
        return plan;
    }

    if (mode == RemovalMode::MetadataOnly
        || clip.storageMode == cuelet::SoundStorageMode::Linked
        || clip.missing) {
        plan.valid = true;
        plan.metadataOnly = true;
        plan.message = clip.storageMode == cuelet::SoundStorageMode::Linked
            ? "Only the Cuelet entry will be removed; the external file is preserved."
            : "Only the Cuelet metadata entry will be removed.";
        return plan;
    }

    const auto actual = managedClipPath(clip, libraryFolder);
    if (!actual.has_value()) {
        plan.message = "The managed file is not safely contained by the library folder.";
        return plan;
    }
    struct stat status {};
    if (::lstat(actual->c_str(), &status) != 0) {
        if (errno == ENOENT) {
            plan.valid = true;
            plan.metadataOnly = true;
            plan.message = "The missing metadata entry will be removed.";
            return plan;
        }
        plan.message = "The managed path could not be inspected: "
            + std::string(std::strerror(errno)) + ".";
        return plan;
    }
    if (!S_ISREG(status.st_mode)) {
        plan.message = "The managed path is not a removable regular file.";
        return plan;
    }

    plan.valid = true;
    plan.metadataOnly = false;
    plan.fileToDelete = *actual;
    plan.fileDevice = static_cast<std::uint64_t>(status.st_dev);
    plan.fileInode = static_cast<std::uint64_t>(status.st_ino);
    plan.message = "The managed library file and its Cuelet entry may be removed.";
    return plan;
}

LinuxLibraryImportService::RemovalResult LinuxLibraryImportService::executeRemoval(
    const RemovalPlan& plan)
{
    RemovalResult result;
    result.metadataKey = plan.metadataKey;
    if (!plan.valid || plan.metadataKey.empty()) {
        result.message = plan.message.empty()
            ? "The removal plan is invalid."
            : plan.message;
        return result;
    }
    if (plan.metadataOnly) {
        result.succeeded = true;
        result.message = plan.message;
        return result;
    }
    if (!plan.fileToDelete.has_value() || !plan.fileToDelete->is_absolute()) {
        result.message = "The managed deletion target is invalid.";
        return result;
    }

    struct stat status {};
    if (::lstat(plan.fileToDelete->c_str(), &status) != 0) {
        result.message = "The managed file could not be revalidated: "
            + std::string(std::strerror(errno)) + ".";
        return result;
    }
    if (!S_ISREG(status.st_mode)
        || static_cast<std::uint64_t>(status.st_dev) != plan.fileDevice
        || static_cast<std::uint64_t>(status.st_ino) != plan.fileInode) {
        result.message = "The managed file changed before it could be deleted.";
        return result;
    }

    std::error_code error;
    if (!fs::remove(*plan.fileToDelete, error) || error) {
        result.message = error
            ? "The managed file could not be deleted: " + error.message()
            : "The managed file could not be deleted.";
        return result;
    }

    result.succeeded = true;
    result.fileDeleted = true;
    result.message = "The managed library file was deleted.";
    return result;
}

LinuxLibraryImportService::RenamePlan LinuxLibraryImportService::planRename(
    const cuelet::SoundClip& clip,
    const std::string& newName,
    const fs::path& libraryFolder,
    RenameMode mode)
{
    RenamePlan plan;
    const auto trimmedName = cuelet::trim(newName);
    if (!validDisplayName(trimmedName)) {
        plan.message = "Enter a valid sound name without control characters.";
        return plan;
    }

    if (mode == RenameMode::DisplayNameOnly) {
        auto updated = clip;
        updated.displayName = trimmedName;
        plan.valid = true;
        plan.updatedClip = std::move(updated);
        return plan;
    }
    if (!validPortableStem(trimmedName)) {
        plan.message = "The sound name contains unsupported filename characters.";
        return plan;
    }
    if (clip.missing) {
        plan.message = "A missing sound file cannot be renamed.";
        return plan;
    }

    fs::path oldPath;
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        const auto external = !clip.externalPath.empty()
            ? clip.externalPath
            : clip.absolutePath;
        if (external.empty()) {
            plan.message = "The linked sound has no external source path.";
            return plan;
        }
        oldPath = normalizedAbsolutePath(fs::u8path(external));
        plan.affectsExternalFile = true;
    } else {
        const auto managed = managedClipPath(clip, libraryFolder);
        if (!managed.has_value()) {
            plan.message = "The managed sound is not safely contained by the library folder.";
            return plan;
        }
        oldPath = *managed;
    }

    std::error_code error;
    if (!fs::is_regular_file(oldPath, error) || error) {
        plan.message = "The sound source is not a regular file.";
        return plan;
    }
    const auto newPath = oldPath.parent_path()
        / fs::u8path(trimmedName + oldPath.extension().u8string());
    if (newPath != oldPath && (fs::exists(newPath, error) || error)) {
        plan.message = error
            ? "The rename destination could not be inspected: " + error.message()
            : "A file with that name already exists.";
        return plan;
    }
    if (newPath == oldPath) {
        plan.valid = true;
        plan.updatedClip = clip;
        return plan;
    }

    auto updated = clip;
    updated.absolutePath = normalizedAbsolutePath(newPath).generic_u8string();
    updated.filename = newPath.filename().u8string();
    updated.sourceFileName = updated.filename;
    updated.displayName = cuelet::displayNameFromFilename(updated.filename);
    if (updated.storageMode == cuelet::SoundStorageMode::Linked) {
        updated.externalPath = updated.absolutePath;
        updated.originalSourcePath = updated.absolutePath;
    } else {
        const auto library = normalizedDirectory(libraryFolder);
        if (!library.usable || !pathIsWithin(library.path, newPath)) {
            plan.message = "The rename destination would leave the library folder.";
            return plan;
        }
        updated.relativePath =
            normalizedAbsolutePath(newPath)
                .lexically_relative(library.path)
                .generic_u8string();
    }
    invalidateDuration(updated);

    plan.valid = true;
    plan.requiresFileRename = newPath != oldPath;
    plan.oldPath = oldPath;
    plan.newPath = newPath;
    plan.updatedClip = std::move(updated);
    return plan;
}
