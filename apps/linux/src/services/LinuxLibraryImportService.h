#pragma once

#include "cuelet/SoundTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class LinuxLibraryImportService {
public:
    enum class ImportMode {
        Copy,
        Link,
    };

    enum class PlanDisposition {
        Ready,
        Duplicate,
        Rejected,
    };

    enum class ImportStatus {
        Imported,
        Linked,
        Duplicate,
        Rejected,
        Failed,
    };

    enum class ErrorCode {
        None,
        LibraryUnavailable,
        SourceMissing,
        SourceNotRegular,
        UnsupportedFormat,
        UnsafePath,
        DestinationExists,
        CopyFailed,
    };

    struct ImportRequest {
        std::filesystem::path libraryFolder;
        std::vector<std::filesystem::path> sources;
        ImportMode mode = ImportMode::Copy;
        bool acceptDirectories = true;
        bool scanSubfolders = true;
        std::string categoryId = "uncategorized";
        std::vector<cuelet::SoundClip> existingClips;
    };

    struct PlannedItem {
        std::filesystem::path source;
        std::filesystem::path destination;
        ImportMode mode = ImportMode::Copy;
        PlanDisposition disposition = PlanDisposition::Rejected;
        ErrorCode error = ErrorCode::None;
        std::string categoryId = "uncategorized";
        std::string duplicateClipId;
        std::string message;
        std::uint64_t sourceDevice = 0;
        std::uint64_t sourceInode = 0;
    };

    struct ImportPlan {
        std::filesystem::path libraryFolder;
        std::vector<PlannedItem> items;
    };

    struct ImportItemResult {
        std::filesystem::path source;
        std::filesystem::path destination;
        ImportStatus status = ImportStatus::Failed;
        ErrorCode error = ErrorCode::None;
        std::string duplicateClipId;
        std::string message;
        std::optional<cuelet::SoundClip> clip;
    };

    struct ImportBatchResult {
        std::vector<ImportItemResult> items;

        std::size_t succeededCount() const;
        std::size_t duplicateCount() const;
        std::size_t failedCount() const;
    };

    enum class RemovalMode {
        MetadataOnly,
        DeleteManagedFile,
    };

    struct RemovalPlan {
        bool valid = false;
        bool metadataOnly = true;
        std::string metadataKey;
        std::optional<std::filesystem::path> fileToDelete;
        std::string message;
    };

    enum class RenameMode {
        DisplayNameOnly,
        RenameFile,
    };

    struct RenamePlan {
        bool valid = false;
        bool requiresFileRename = false;
        bool affectsExternalFile = false;
        std::filesystem::path oldPath;
        std::filesystem::path newPath;
        std::optional<cuelet::SoundClip> updatedClip;
        std::string message;
    };

    // Planning is read-only. Execution revalidates every source and destination,
    // never overwrites an existing file, and reports each batch item separately.
    static ImportPlan planImport(const ImportRequest& request);
    static ImportBatchResult executeImport(const ImportPlan& plan);

    // Reconciles one successful import with the current view model. A matching
    // path is replaced (rather than duplicated) while user-authored metadata is
    // retained and file/provenance/duration state comes from the fresh import.
    static std::vector<cuelet::SoundClip> mergeImportedClip(
        const std::vector<cuelet::SoundClip>& existingClips,
        cuelet::SoundClip importedClip);

    static std::string linkedMetadataKey(const std::filesystem::path& source);
    static bool pathsReferToSameFile(const std::filesystem::path& left,
                                     const std::filesystem::path& right) noexcept;

    // These methods never delete or rename files. Callers must present the
    // described external effect to the user and revalidate before executing it.
    static RemovalPlan planRemoval(const cuelet::SoundClip& clip,
                                   const std::filesystem::path& libraryFolder,
                                   RemovalMode mode);
    static RenamePlan planRename(const cuelet::SoundClip& clip,
                                 const std::string& newName,
                                 const std::filesystem::path& libraryFolder,
                                 RenameMode mode);
};
