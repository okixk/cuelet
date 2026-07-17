#include "cuelet/LibraryScanner.h"

#include <algorithm>
#include <chrono>
#include <set>

namespace cuelet {

namespace {

const std::set<std::string>& extensionSet()
{
    static const std::set<std::string> extensions = {
        ".aif",
        ".aiff",
        ".flac",
        ".m4a",
        ".mp3",
        ".ogg",
        ".wav",
    };
    return extensions;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::time_t fileTimeToTimeT(const std::filesystem::file_time_type time)
{
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(systemTime);
}

bool isHiddenPath(const std::filesystem::path& path)
{
    for (const auto& part : path) {
        const auto text = part.u8string();
        if (!text.empty() && text[0] == '.') {
            return true;
        }
    }
    return false;
}

SoundClip makeClip(const std::filesystem::path& root, const std::filesystem::path& file)
{
    SoundClip clip;
    clip.absolutePath = std::filesystem::absolute(file).lexically_normal().u8string();
    clip.relativePath = LibraryScanner::normalizeRelativePath(root, file);
    clip.filename = filenameFromPath(clip.relativePath);
    clip.displayName = displayNameFromFilename(clip.filename);
    clip.categoryId = "uncategorized";
    clip.id = stableIdForPath(clip.relativePath);
    std::error_code error;
    const auto lastWrite = std::filesystem::last_write_time(file, error);
    clip.addedAt = error ? 0 : fileTimeToTimeT(lastWrite);
    return clip;
}

bool byDisplayName(const SoundClip& left, const SoundClip& right)
{
    const auto lhs = normalizeForSearch(left.searchableName());
    const auto rhs = normalizeForSearch(right.searchableName());
    if (lhs != rhs) {
        return lhs < rhs;
    }
    return normalizeForSearch(left.relativePath) < normalizeForSearch(right.relativePath);
}

void scanEntry(const std::filesystem::path& root,
               const std::filesystem::directory_entry& entry,
               ScanResult& result)
{
    std::error_code error;
    if (!entry.is_regular_file(error)) {
        return;
    }

    const auto relativePath = std::filesystem::relative(entry.path(), root, error);
    if (error || isHiddenPath(relativePath)) {
        return;
    }

    if (!LibraryScanner::isSupportedAudioFile(entry.path())) {
        result.unsupportedFiles.push_back(LibraryScanner::normalizeRelativePath(root, entry.path()));
        return;
    }

    result.clips.push_back(makeClip(root, entry.path()));
}

} // namespace

bool LibraryScanner::isSupportedAudioFile(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return false;
    }
    return extensionSet().count(lower(path.extension().u8string())) > 0;
}

std::vector<std::string> LibraryScanner::supportedExtensions()
{
    std::vector<std::string> extensions;
    for (const auto& extension : extensionSet()) {
        extensions.push_back(extension.substr(1));
    }
    return extensions;
}

std::string LibraryScanner::normalizeRelativePath(const std::filesystem::path& root,
                                                  const std::filesystem::path& file)
{
    std::error_code error;
    auto relative = std::filesystem::relative(file, root, error);
    if (error) {
        relative = file.filename();
    }
    return relative.generic_u8string();
}

ScanResult LibraryScanner::scan(const std::filesystem::path& libraryFolder, bool recursive) const
{
    ScanResult result;
    std::error_code error;

    const auto root = std::filesystem::absolute(libraryFolder, error).lexically_normal();
    if (error || !std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
        result.warning = "The selected library folder does not exist or cannot be opened.";
        return result;
    }

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error)) {
            if (!error) {
                scanEntry(root, entry, result);
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error)) {
            if (!error) {
                scanEntry(root, entry, result);
            }
        }
    }

    std::sort(result.clips.begin(), result.clips.end(), byDisplayName);
    std::sort(result.unsupportedFiles.begin(), result.unsupportedFiles.end());
    return result;
}

} // namespace cuelet
