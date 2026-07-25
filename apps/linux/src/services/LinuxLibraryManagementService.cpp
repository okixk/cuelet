#include "services/LinuxLibraryImportService.h"

#include <algorithm>
#include <optional>
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

    const auto fromRelative = normalizedAbsolutePath(library.path / relative);
    const auto actual = clip.absolutePath.empty()
        ? fromRelative
        : normalizedAbsolutePath(fs::u8path(clip.absolutePath));
    if (!pathIsWithin(library.path, actual) || actual != fromRelative) {
        return std::nullopt;
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
    std::error_code error;
    if (!fs::exists(*actual, error) && !error) {
        plan.valid = true;
        plan.metadataOnly = true;
        plan.message = "The missing metadata entry will be removed.";
        return plan;
    }
    if (error || !fs::is_regular_file(*actual, error) || error) {
        plan.message = "The managed path is not a removable regular file.";
        return plan;
    }

    plan.valid = true;
    plan.metadataOnly = false;
    plan.fileToDelete = *actual;
    plan.message = "The managed library file and its Cuelet entry may be removed.";
    return plan;
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
