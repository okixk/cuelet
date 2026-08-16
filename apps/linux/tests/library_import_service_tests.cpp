#include "services/LinuxLibraryImportService.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& label)
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path()
            / ("cuelet-linux-import-" + label + "-" + std::to_string(::getpid())
               + "-" + std::to_string(nonce));
        std::error_code error;
        require(fs::create_directories(path_, error) && !error,
                "could not create temporary test directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeFile(const fs::path& path, const std::string& contents = "audio fixture")
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    require(!error, "could not create fixture parent directory");
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    require(stream.good(), "could not write fixture file");
}

std::string normalized(const fs::path& path)
{
    std::error_code error;
    const auto canonical = fs::weakly_canonical(fs::absolute(path, error), error);
    require(!error, "could not normalize fixture path");
    return canonical.generic_u8string();
}

void copyAndLinkProducePortableClipMetadata()
{
    TemporaryDirectory fixture("copy-link");
    const auto sources = fixture.path() / "sources";
    const auto library = fixture.path() / "library";
    require(fs::create_directories(sources) && fs::create_directories(library),
            "could not create import fixture folders");
    const auto source = sources / "Soft Rain.WAV";
    writeFile(source, "rain");

    LinuxLibraryImportService::ImportRequest copyRequest;
    copyRequest.libraryFolder = library;
    copyRequest.sources = {source};
    copyRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    copyRequest.categoryId = "weather";

    const auto copyPlan = LinuxLibraryImportService::planImport(copyRequest);
    require(copyPlan.items.size() == 1, "copy planning should produce one item");
    require(copyPlan.items.front().disposition
                == LinuxLibraryImportService::PlanDisposition::Ready,
            "supported regular files should be ready to copy");
    require(copyPlan.items.front().destination == library / "Soft Rain.WAV",
            "copy destination should use the source filename");

    const auto copyResult = LinuxLibraryImportService::executeImport(copyPlan);
    require(copyResult.succeededCount() == 1 && copyResult.failedCount() == 0,
            "copy execution should report one success");
    const auto& copied = copyResult.items.front();
    require(copied.status == LinuxLibraryImportService::ImportStatus::Imported
                && copied.clip.has_value(),
            "copy execution should return imported clip metadata");
    require(fs::is_regular_file(library / "Soft Rain.WAV"),
            "copy execution should create a managed library file");
    require(copied.clip->storageMode == cuelet::SoundStorageMode::Managed,
            "copied clip should be managed");
    require(copied.clip->relativePath == "Soft Rain.WAV"
                && copied.clip->filename == "Soft Rain.WAV",
            "managed metadata should use a portable relative path");
    require(copied.clip->categoryId == "weather",
            "copy metadata should retain the requested category");
    require(copied.clip->sourceFileName == "Soft Rain.WAV"
                && copied.clip->originalSourcePath == normalized(source)
                && copied.clip->externalPath.empty(),
            "managed metadata should retain source provenance without an external link");

    LinuxLibraryImportService::ImportRequest linkRequest;
    linkRequest.libraryFolder = library;
    linkRequest.sources = {source};
    linkRequest.mode = LinuxLibraryImportService::ImportMode::Link;
    const auto linkResult =
        LinuxLibraryImportService::executeImport(
            LinuxLibraryImportService::planImport(linkRequest));
    require(linkResult.succeededCount() == 1 && linkResult.items.front().clip.has_value(),
            "link execution should produce clip metadata");
    const auto& linked = *linkResult.items.front().clip;
    require(linkResult.items.front().status
                == LinuxLibraryImportService::ImportStatus::Linked,
            "link execution should report a linked item");
    require(linked.storageMode == cuelet::SoundStorageMode::Linked,
            "linked clip should retain linked storage mode");
    require(linked.relativePath.rfind("@linked/", 0) == 0
                && linked.id == cuelet::stableIdForPath(linked.relativePath),
            "linked clips should have stable metadata identity");
    require(linked.absolutePath == normalized(source)
                && linked.externalPath == normalized(source)
                && linked.originalSourcePath == normalized(source),
            "linked metadata should point to the normalized external source");
    require(!fs::exists(library / "Soft Rain (2).WAV"),
            "link mode must not create a managed copy");
}

void successfulImportReplacesMissingPathAndPreservesUserMetadata()
{
    cuelet::SoundClip missing;
    missing.id = "stable-user-id";
    missing.absolutePath = "/old/library/hit.wav";
    missing.relativePath = "hit.wav";
    missing.filename = "hit.wav";
    missing.displayName = "My favorite hit";
    missing.categoryId = "effects";
    missing.notes = "Trim the tail";
    missing.aliases = {"impact", "sting"};
    missing.shortcut = cuelet::Shortcut{42, 7, "Ctrl+Alt+K"};
    missing.favorite = true;
    missing.missing = true;
    missing.addedAt = 123;
    missing.lastPlayedAt = 456;

    cuelet::SoundClip imported;
    imported.id = "generated-path-id";
    imported.absolutePath = "/new/library/hit.wav";
    imported.relativePath = "hit.wav";
    imported.filename = "hit.wav";
    imported.sourceFileName = "replacement.wav";
    imported.originalSourcePath = "/incoming/replacement.wav";
    imported.displayName = "Hit";
    imported.categoryId = "uncategorized";
    imported.missing = false;
    imported.durationKnown = true;
    imported.durationSeconds = 1.25;
    imported.durationFileSize = 2048;
    imported.durationModifiedSeconds = 789;
    imported.durationSourcePath = imported.absolutePath;
    imported.addedAt = 999;

    cuelet::SoundClip unrelated;
    unrelated.id = "unrelated";
    unrelated.relativePath = "other.wav";

    const std::vector<cuelet::SoundClip> existing = {missing, unrelated};
    const auto merged =
        LinuxLibraryImportService::mergeImportedClip(existing, imported);

    require(existing.front().missing,
            "import reconciliation must not mutate the existing clip collection");
    require(merged.size() == 2,
            "re-importing a missing managed path must replace rather than append");
    require(std::count_if(
                merged.begin(), merged.end(), [](const cuelet::SoundClip& clip) {
                    return clip.relativePath == "hit.wav";
                }) == 1,
            "the reconciled collection must contain only one clip per imported path");

    const auto replacement = std::find_if(
        merged.begin(), merged.end(), [](const cuelet::SoundClip& clip) {
            return clip.relativePath == "hit.wav";
        });
    require(replacement != merged.end()
                && replacement->id == missing.id
                && replacement->displayName == missing.displayName
                && replacement->categoryId == missing.categoryId
                && replacement->notes == missing.notes
                && replacement->aliases == missing.aliases
                && replacement->shortcut.has_value()
                && replacement->favorite
                && replacement->addedAt == missing.addedAt
                && replacement->lastPlayedAt == missing.lastPlayedAt,
            "replacement must retain stable identity and user-authored metadata");
    require(!replacement->missing
                && replacement->absolutePath == imported.absolutePath
                && replacement->originalSourcePath == imported.originalSourcePath
                && replacement->sourceFileName == imported.sourceFileName
                && replacement->durationKnown
                && replacement->durationSeconds == imported.durationSeconds
                && replacement->durationSourcePath == imported.durationSourcePath,
            "replacement must use fresh file, provenance, availability, and duration state");
}

void missingManagedSourceCanBeRestoredByReimport()
{
    TemporaryDirectory fixture("restore-missing");
    const auto sources = fixture.path() / "sources";
    const auto library = fixture.path() / "library";
    require(fs::create_directories(sources) && fs::create_directories(library),
            "could not create missing-source recovery fixtures");
    const auto source = sources / "hit.wav";
    writeFile(source);

    cuelet::SoundClip missing;
    missing.id = "preserved-id";
    missing.absolutePath = normalized(library / "hit.wav");
    missing.relativePath = "hit.wav";
    missing.filename = "hit.wav";
    missing.originalSourcePath = normalized(source);
    missing.displayName = "Recovered Hit";
    missing.favorite = true;
    missing.missing = true;

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {source};
    request.mode = LinuxLibraryImportService::ImportMode::Copy;
    request.existingClips = {missing};

    const auto plan = LinuxLibraryImportService::planImport(request);
    require(plan.items.size() == 1
                && plan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Ready
                && plan.items.front().destination == library / "hit.wav",
            "a missing managed entry must not block recovery from its original source");
    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.succeededCount() == 1 && result.items.front().clip.has_value(),
            "missing managed recovery must copy the replacement source");
    const auto merged = LinuxLibraryImportService::mergeImportedClip(
        request.existingClips, *result.items.front().clip);
    require(merged.size() == 1
                && merged.front().id == missing.id
                && merged.front().displayName == missing.displayName
                && merged.front().favorite
                && !merged.front().missing
                && fs::is_regular_file(library / "hit.wav"),
            "missing managed recovery must replace metadata and restore the file");
}

void batchValidationReportsPartialResults()
{
    TemporaryDirectory fixture("validation");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto valid = fixture.path() / "valid.ogg";
    const auto unsupported = fixture.path() / "cover.png";
    const auto directoryNamedLikeAudio = fixture.path() / "folder.wav";
    const auto missing = fixture.path() / "missing.mp3";
    writeFile(valid);
    writeFile(unsupported);
    require(fs::create_directories(directoryNamedLikeAudio),
            "could not create non-regular fixture");

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {valid, unsupported, directoryNamedLikeAudio, missing};
    request.mode = LinuxLibraryImportService::ImportMode::Copy;
    request.acceptDirectories = false;

    const auto plan = LinuxLibraryImportService::planImport(request);
    require(plan.items.size() == 4, "every explicit batch input should be reported");
    require(plan.items[0].disposition == LinuxLibraryImportService::PlanDisposition::Ready,
            "valid audio should be accepted");
    require(plan.items[1].error == LinuxLibraryImportService::ErrorCode::UnsupportedFormat,
            "unsupported extension should be rejected precisely");
    require(plan.items[2].error == LinuxLibraryImportService::ErrorCode::SourceNotRegular,
            "directories should be rejected when directory import is disabled");
    require(plan.items[3].error == LinuxLibraryImportService::ErrorCode::SourceMissing,
            "missing sources should be rejected precisely");

    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.succeededCount() == 1 && result.failedCount() == 3,
            "batch execution should retain partial success");
    require(fs::is_regular_file(library / "valid.ogg"),
            "valid batch members should still be copied");
    require(!fs::exists(library / "cover.png") && !fs::exists(library / "folder.wav"),
            "rejected batch members must not be copied");
}

void collisionNamesAreDeterministicAcrossABatch()
{
    TemporaryDirectory fixture("collisions");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    writeFile(library / "tone.wav", "existing one");
    writeFile(library / "tone (2).wav", "existing two");
    const auto first = fixture.path() / "one" / "tone.wav";
    const auto second = fixture.path() / "two" / "tone.wav";
    writeFile(first, "first");
    writeFile(second, "second");

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {first, second};
    request.mode = LinuxLibraryImportService::ImportMode::Copy;
    const auto plan = LinuxLibraryImportService::planImport(request);

    require(plan.items.size() == 2, "two distinct sources should produce two plan items");
    require(plan.items[0].destination.filename() == "tone (3).wav"
                && plan.items[1].destination.filename() == "tone (4).wav",
            "collision suffixes should account for disk and earlier batch reservations");
    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.succeededCount() == 2,
            "both collision-resolved imports should succeed");
    require(fs::is_regular_file(library / "tone (3).wav")
                && fs::is_regular_file(library / "tone (4).wav"),
            "collision-resolved files should both exist");
}

void directoryPlanningHonorsRecursionAndReportsUnsupportedFiles()
{
    TemporaryDirectory fixture("directory");
    const auto library = fixture.path() / "library";
    const auto droppedFolder = fixture.path() / "dropped";
    require(fs::create_directories(library), "could not create library fixture");
    writeFile(droppedFolder / "top.wav");
    writeFile(droppedFolder / "cover.jpg");
    writeFile(droppedFolder / "nested" / "deep.mp3");

    LinuxLibraryImportService::ImportRequest flatRequest;
    flatRequest.libraryFolder = library;
    flatRequest.sources = {droppedFolder};
    flatRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    flatRequest.scanSubfolders = false;
    const auto flatPlan = LinuxLibraryImportService::planImport(flatRequest);
    require(flatPlan.items.size() == 2,
            "flat directory planning should report direct regular files only");
    require(flatPlan.items[0].error == LinuxLibraryImportService::ErrorCode::UnsupportedFormat
                && flatPlan.items[1].disposition
                    == LinuxLibraryImportService::PlanDisposition::Ready,
            "sorted flat planning should reject unsupported files and accept audio");

    LinuxLibraryImportService::ImportRequest recursiveRequest = flatRequest;
    recursiveRequest.scanSubfolders = true;
    const auto recursivePlan = LinuxLibraryImportService::planImport(recursiveRequest);
    require(recursivePlan.items.size() == 3,
            "recursive directory planning should include nested regular files");
    const auto nested = std::find_if(
        recursivePlan.items.begin(), recursivePlan.items.end(), [](const auto& item) {
            return item.source.filename() == "deep.mp3";
        });
    require(nested != recursivePlan.items.end()
                && nested->disposition == LinuxLibraryImportService::PlanDisposition::Ready,
            "nested supported audio should be accepted when recursion is enabled");
}

void duplicatesAreDetectedBySourceIdentity()
{
    TemporaryDirectory fixture("duplicates");
    const auto library = fixture.path() / "library";
    const auto sources = fixture.path() / "sources";
    require(fs::create_directories(library) && fs::create_directories(sources),
            "could not create duplicate fixture folders");
    const auto source = sources / "same.flac";
    const auto hardLink = sources / "same-alias.flac";
    writeFile(source);
    std::error_code hardLinkError;
    fs::create_hard_link(source, hardLink, hardLinkError);
    require(!hardLinkError, "could not create hard-link identity fixture");

    cuelet::SoundClip existing;
    existing.id = "existing-linked-id";
    existing.storageMode = cuelet::SoundStorageMode::Linked;
    existing.absolutePath = normalized(source);
    existing.externalPath = normalized(source);
    existing.originalSourcePath = normalized(source);

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {hardLink};
    request.mode = LinuxLibraryImportService::ImportMode::Link;
    request.existingClips = {existing};
    const auto plan = LinuxLibraryImportService::planImport(request);

    require(plan.items.size() == 1
                && plan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Duplicate
                && plan.items.front().duplicateClipId == existing.id,
            "existing linked sources should be detected by file identity");
    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.duplicateCount() == 1 && result.succeededCount() == 0,
            "duplicates should be reported without creating another clip");
}

void executionRejectsATamperedTraversalDestination()
{
    TemporaryDirectory fixture("traversal");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto source = fixture.path() / "source.wav";
    writeFile(source);

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {source};
    request.mode = LinuxLibraryImportService::ImportMode::Copy;
    auto plan = LinuxLibraryImportService::planImport(request);
    require(plan.items.size() == 1, "expected one traversal test plan item");
    plan.items.front().destination = library / ".." / "escaped.wav";

    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.failedCount() == 1
                && result.items.front().error
                    == LinuxLibraryImportService::ErrorCode::UnsafePath,
            "execution should revalidate a planned destination");
    require(!fs::exists(fixture.path() / "escaped.wav"),
            "a traversal destination must never be created");
}

void executionNeverOverwritesADestinationCreatedAfterPlanning()
{
    TemporaryDirectory fixture("race");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto source = fixture.path() / "source.wav";
    writeFile(source, "new source");

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = library;
    request.sources = {source};
    request.mode = LinuxLibraryImportService::ImportMode::Copy;
    const auto plan = LinuxLibraryImportService::planImport(request);
    require(plan.items.size() == 1
                && plan.items.front().destination == library / "source.wav",
            "expected the initially free destination to be planned");
    writeFile(library / "source.wav", "created after planning");

    const auto result = LinuxLibraryImportService::executeImport(plan);
    require(result.failedCount() == 1
                && result.items.front().error
                    == LinuxLibraryImportService::ErrorCode::DestinationExists,
            "a destination race should fail without overwrite");
    std::ifstream destination(library / "source.wav", std::ios::binary);
    const std::string contents(
        (std::istreambuf_iterator<char>(destination)),
        std::istreambuf_iterator<char>());
    require(contents == "created after planning",
            "execution must preserve a destination created after planning");
}

void executionRejectsPathReplacementWithSymbolicLinks()
{
    TemporaryDirectory fixture("execution-symlink-race");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto source = fixture.path() / "source.wav";
    const auto replacement = fixture.path() / "private.wav";
    writeFile(source, "planned source");
    writeFile(replacement, "replacement target");

    LinuxLibraryImportService::ImportRequest sourceRequest;
    sourceRequest.libraryFolder = library;
    sourceRequest.sources = {source};
    sourceRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    const auto sourcePlan =
        LinuxLibraryImportService::planImport(sourceRequest);
    require(sourcePlan.items.size() == 1
                && sourcePlan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Ready,
            "expected a ready source replacement plan");

    std::error_code error;
    require(fs::remove(source, error) && !error,
            "could not remove the planned source fixture");
    fs::create_symlink(replacement, source, error);
    require(!error, "could not replace the planned source with a symlink");

    const auto sourceResult =
        LinuxLibraryImportService::executeImport(sourcePlan);
    require(sourceResult.failedCount() == 1
                && sourceResult.items.front().error
                    == LinuxLibraryImportService::ErrorCode::UnsafePath
                && !fs::exists(library / "source.wav"),
            "execution must reject a source replaced by a symbolic link");

    const auto secondSource = fixture.path() / "second.wav";
    writeFile(secondSource, "second source");
    LinuxLibraryImportService::ImportRequest destinationRequest;
    destinationRequest.libraryFolder = library;
    destinationRequest.sources = {secondSource};
    destinationRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    const auto destinationPlan =
        LinuxLibraryImportService::planImport(destinationRequest);
    require(destinationPlan.items.size() == 1
                && destinationPlan.items.front().destination
                    == library / "second.wav",
            "expected a ready destination replacement plan");

    fs::create_symlink(
        fixture.path() / "nonexistent-target.wav",
        library / "second.wav",
        error);
    require(!error, "could not create dangling destination symlink");
    const auto destinationResult =
        LinuxLibraryImportService::executeImport(destinationPlan);
    require(destinationResult.failedCount() == 1
                && destinationResult.items.front().error
                    == LinuxLibraryImportService::ErrorCode::DestinationExists
                && fs::is_symlink(fs::symlink_status(library / "second.wav")),
            "exclusive destination creation must reject even a dangling symlink");

    const auto linkedSource = fixture.path() / "linked.wav";
    const auto linkedReplacement = fixture.path() / "linked-replacement.wav";
    writeFile(linkedSource, "selected linked source");
    writeFile(linkedReplacement, "different regular file");
    LinuxLibraryImportService::ImportRequest linkRequest;
    linkRequest.libraryFolder = library;
    linkRequest.sources = {linkedSource};
    linkRequest.mode = LinuxLibraryImportService::ImportMode::Link;
    const auto linkPlan = LinuxLibraryImportService::planImport(linkRequest);
    require(linkPlan.items.size() == 1
                && linkPlan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Ready,
            "expected a ready linked-source replacement plan");
    const auto selectedBackup = fixture.path() / "selected-backup.wav";
    fs::rename(linkedSource, selectedBackup, error);
    require(!error, "could not move the selected linked source");
    fs::rename(linkedReplacement, linkedSource, error);
    require(!error, "could not replace the selected path with another regular file");

    const auto linkResult =
        LinuxLibraryImportService::executeImport(linkPlan);
    require(linkResult.failedCount() == 1
                && linkResult.items.front().error
                    == LinuxLibraryImportService::ErrorCode::UnsafePath
                && !linkResult.items.front().clip,
            "link execution must remain bound to the file selected during planning");
}

void symbolicLinksAreRejectedWithoutReadingTheirTargets()
{
    TemporaryDirectory fixture("symlink");
    const auto library = fixture.path() / "library";
    const auto droppedFolder = fixture.path() / "dropped";
    const auto privateSource = fixture.path() / "private" / "private.wav";
    require(fs::create_directories(library)
                && fs::create_directories(droppedFolder),
            "could not create symlink import fixtures");
    writeFile(privateSource, "private fixture contents");

    const auto linkedFile = droppedFolder / "linked.wav";
    std::error_code error;
    fs::create_symlink(privateSource, linkedFile, error);
    require(!error, "could not create file symlink fixture");

    LinuxLibraryImportService::ImportRequest directRequest;
    directRequest.libraryFolder = library;
    directRequest.sources = {linkedFile};
    directRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    const auto directPlan =
        LinuxLibraryImportService::planImport(directRequest);
    require(directPlan.items.size() == 1
                && directPlan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Rejected
                && directPlan.items.front().error
                    == LinuxLibraryImportService::ErrorCode::UnsafePath,
            "direct symbolic-link imports must be rejected before resolving the target");

    LinuxLibraryImportService::ImportRequest directoryRequest;
    directoryRequest.libraryFolder = library;
    directoryRequest.sources = {droppedFolder};
    directoryRequest.mode = LinuxLibraryImportService::ImportMode::Copy;
    directoryRequest.acceptDirectories = true;
    directoryRequest.scanSubfolders = true;
    const auto directoryPlan =
        LinuxLibraryImportService::planImport(directoryRequest);
    require(directoryPlan.items.size() == 1
                && directoryPlan.items.front().source == linkedFile
                && directoryPlan.items.front().disposition
                    == LinuxLibraryImportService::PlanDisposition::Rejected
                && directoryPlan.items.front().error
                    == LinuxLibraryImportService::ErrorCode::UnsafePath,
            "directory imports must report rather than follow file symlinks");

    const auto result =
        LinuxLibraryImportService::executeImport(directoryPlan);
    require(result.failedCount() == 1
                && !fs::exists(library / "linked.wav")
                && fs::is_regular_file(privateSource),
            "executing a rejected symlink plan must not copy or alter its target");
}

void removalPlansProtectLinkedAndOutOfLibraryFiles()
{
    TemporaryDirectory fixture("remove");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto managedPath = library / "managed.wav";
    const auto externalPath = fixture.path() / "external.wav";
    writeFile(managedPath);
    writeFile(externalPath);

    cuelet::SoundClip managed;
    managed.id = "managed";
    managed.relativePath = "managed.wav";
    managed.absolutePath = normalized(managedPath);
    managed.storageMode = cuelet::SoundStorageMode::Managed;

    const auto metadataOnly = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::MetadataOnly);
    require(metadataOnly.valid && metadataOnly.metadataOnly
                && !metadataOnly.fileToDelete.has_value(),
            "metadata-only removal should never plan a file deletion");

    const auto managedDelete = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    require(managedDelete.valid && !managedDelete.metadataOnly
                && managedDelete.fileToDelete == fs::weakly_canonical(managedPath),
            "managed deletion may target a regular file inside the library");

    cuelet::SoundClip linked = managed;
    linked.id = "linked";
    linked.relativePath = "@linked/stable";
    linked.storageMode = cuelet::SoundStorageMode::Linked;
    linked.absolutePath = normalized(externalPath);
    linked.externalPath = normalized(externalPath);
    const auto linkedDelete = LinuxLibraryImportService::planRemoval(
        linked, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    require(linkedDelete.valid && linkedDelete.metadataOnly
                && !linkedDelete.fileToDelete.has_value()
                && fs::is_regular_file(externalPath),
            "linked removal must remain metadata-only");

    cuelet::SoundClip unsafeManaged = managed;
    unsafeManaged.absolutePath = normalized(externalPath);
    unsafeManaged.relativePath = "../external.wav";
    const auto unsafeDelete = LinuxLibraryImportService::planRemoval(
        unsafeManaged, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    require(!unsafeDelete.valid && !unsafeDelete.fileToDelete.has_value(),
            "managed paths outside the library must never be deletion targets");

    const auto realManagedPath = library / "real.wav";
    const auto symlinkManagedPath = library / "alias.wav";
    writeFile(realManagedPath, "real managed target");
    std::error_code error;
    fs::create_symlink(realManagedPath, symlinkManagedPath, error);
    require(!error, "could not create the managed deletion symlink fixture");
    cuelet::SoundClip symlinkManaged = managed;
    symlinkManaged.relativePath = "alias.wav";
    symlinkManaged.absolutePath = fs::absolute(symlinkManagedPath).lexically_normal().string();
    const auto symlinkDelete = LinuxLibraryImportService::planRemoval(
        symlinkManaged, library,
        LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    require(!symlinkDelete.valid && !symlinkDelete.fileToDelete.has_value()
                && fs::is_symlink(fs::symlink_status(symlinkManagedPath))
                && fs::is_regular_file(realManagedPath),
            "managed deletion planning must reject symlink paths without touching their targets");

    const auto realLibrary = fixture.path() / "real-library";
    const auto libraryLink = fixture.path() / "library-link";
    require(fs::create_directories(realLibrary),
            "could not create the symlinked library-root fixture");
    fs::create_directory_symlink(realLibrary, libraryLink, error);
    require(!error, "could not create the library-root symlink fixture");
    const auto rootedManagedPath = libraryLink / "rooted.wav";
    writeFile(rootedManagedPath, "rooted managed file");
    cuelet::SoundClip rootedManaged = managed;
    rootedManaged.relativePath = "rooted.wav";
    rootedManaged.absolutePath = fs::absolute(rootedManagedPath).lexically_normal().string();
    const auto rootedDelete = LinuxLibraryImportService::planRemoval(
        rootedManaged, libraryLink,
        LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    require(rootedDelete.valid && !rootedDelete.metadataOnly
                && rootedDelete.fileToDelete == fs::absolute(rootedManagedPath).lexically_normal(),
            "a deliberately selected symlinked library root must remain usable");
}

void removalExecutionDeletesOnlyRevalidatedManagedFiles()
{
    TemporaryDirectory fixture("remove-execution");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create removal execution fixture");

    const auto managedPath = library / "managed.wav";
    const auto externalPath = fixture.path() / "external.wav";
    writeFile(managedPath, "managed");
    writeFile(externalPath, "external");

    cuelet::SoundClip managed;
    managed.id = "managed";
    managed.relativePath = "managed.wav";
    managed.absolutePath = normalized(managedPath);
    managed.storageMode = cuelet::SoundStorageMode::Managed;

    const auto metadataPlan = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::MetadataOnly);
    const auto metadataResult = LinuxLibraryImportService::executeRemoval(metadataPlan);
    require(metadataResult.succeeded && !metadataResult.fileDeleted
                && metadataResult.metadataKey == managed.relativePath
                && fs::is_regular_file(managedPath),
            "metadata-only execution must preserve the managed audio file");

    cuelet::SoundClip linked = managed;
    linked.id = "linked";
    linked.relativePath = LinuxLibraryImportService::linkedMetadataKey(externalPath);
    linked.absolutePath = normalized(externalPath);
    linked.externalPath = normalized(externalPath);
    linked.storageMode = cuelet::SoundStorageMode::Linked;
    const auto linkedPlan = LinuxLibraryImportService::planRemoval(
        linked, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    const auto linkedResult = LinuxLibraryImportService::executeRemoval(linkedPlan);
    require(linkedResult.succeeded && !linkedResult.fileDeleted
                && fs::is_regular_file(externalPath),
            "linked removal execution must never delete the external source");

    const auto deletePlan = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    const auto deleteResult = LinuxLibraryImportService::executeRemoval(deletePlan);
    require(deleteResult.succeeded && deleteResult.fileDeleted
                && !fs::exists(managedPath),
            "confirmed managed deletion must remove the planned library file");

    writeFile(managedPath, "replacement target");
    const auto stalePlan = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    std::error_code error;
    require(fs::remove(managedPath, error) && !error,
            "could not replace the planned deletion fixture");
    fs::create_symlink(externalPath, managedPath, error);
    require(!error, "could not create the deletion symlink race fixture");

    const auto staleResult = LinuxLibraryImportService::executeRemoval(stalePlan);
    require(!staleResult.succeeded && !staleResult.fileDeleted
                && fs::is_symlink(fs::symlink_status(managedPath))
                && fs::is_regular_file(externalPath),
            "execution must reject a managed path replaced by a symlink");

    require(fs::remove(managedPath, error) && !error,
            "could not remove the deletion symlink fixture");
    writeFile(managedPath, "original identity");
    const auto replacedFilePlan = LinuxLibraryImportService::planRemoval(
        managed, library, LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    const auto replacementPath = library / "replacement-identity.wav";
    writeFile(replacementPath, "different identity");
    require(fs::remove(managedPath, error) && !error,
            "could not replace the planned regular file");
    fs::rename(replacementPath, managedPath, error);
    require(!error, "could not move the different file identity into place");

    const auto replacedFileResult =
        LinuxLibraryImportService::executeRemoval(replacedFilePlan);
    require(!replacedFileResult.succeeded && !replacedFileResult.fileDeleted
                && fs::is_regular_file(managedPath),
            "execution must reject a regular file whose identity changed after planning");
}

void renamePlanningIsNonMutatingAndTraversalSafe()
{
    TemporaryDirectory fixture("rename");
    const auto library = fixture.path() / "library";
    require(fs::create_directories(library), "could not create library fixture");
    const auto oldPath = library / "old name.aiff";
    writeFile(oldPath);

    cuelet::SoundClip clip;
    clip.id = "stable-id";
    clip.absolutePath = normalized(oldPath);
    clip.relativePath = "old name.aiff";
    clip.filename = "old name.aiff";
    clip.sourceFileName = "old name.aiff";
    clip.displayName = "Old Name";
    clip.categoryId = "effects";
    clip.favorite = true;
    clip.durationKnown = true;
    clip.durationSeconds = 2.5;
    clip.durationSourcePath = normalized(oldPath);

    const auto displayPlan = LinuxLibraryImportService::planRename(
        clip, "A nicer label", library, LinuxLibraryImportService::RenameMode::DisplayNameOnly);
    require(displayPlan.valid && !displayPlan.requiresFileRename
                && displayPlan.updatedClip.has_value()
                && displayPlan.updatedClip->displayName == "A nicer label",
            "display-only rename should return updated metadata");
    require(clip.displayName == "Old Name" && fs::is_regular_file(oldPath),
            "rename planning must not mutate the source clip or filesystem");

    const auto filePlan = LinuxLibraryImportService::planRename(
        clip, "renamed", library, LinuxLibraryImportService::RenameMode::RenameFile);
    require(filePlan.valid && filePlan.requiresFileRename
                && filePlan.oldPath == fs::weakly_canonical(oldPath)
                && filePlan.newPath == library / "renamed.aiff"
                && filePlan.updatedClip.has_value(),
            "file rename should produce an explicit non-executing plan");
    require(filePlan.updatedClip->id == clip.id
                && filePlan.updatedClip->categoryId == clip.categoryId
                && filePlan.updatedClip->favorite
                && filePlan.updatedClip->relativePath == "renamed.aiff"
                && filePlan.updatedClip->displayName == "renamed"
                && !filePlan.updatedClip->durationKnown
                && filePlan.updatedClip->durationSourcePath.empty(),
            "rename metadata should preserve identity/user state and invalidate duration cache");
    require(fs::is_regular_file(oldPath) && !fs::exists(filePlan.newPath),
            "rename planning must not move files");

    const auto sameNamePlan = LinuxLibraryImportService::planRename(
        clip, "old name", library, LinuxLibraryImportService::RenameMode::RenameFile);
    require(sameNamePlan.valid && !sameNamePlan.requiresFileRename
                && sameNamePlan.updatedClip.has_value()
                && sameNamePlan.updatedClip->displayName == clip.displayName
                && sameNamePlan.updatedClip->durationKnown
                && sameNamePlan.updatedClip->durationSourcePath
                    == clip.durationSourcePath,
            "submitting an unchanged filename must preserve custom display and duration metadata");

    const auto traversal = LinuxLibraryImportService::planRename(
        clip, "../escape", library, LinuxLibraryImportService::RenameMode::RenameFile);
    require(!traversal.valid && !traversal.updatedClip.has_value(),
            "rename stems containing traversal must be rejected");
    const auto malformedDisplay = LinuxLibraryImportService::planRename(
        clip, "line one\nline two", library,
        LinuxLibraryImportService::RenameMode::DisplayNameOnly);
    require(!malformedDisplay.valid && !malformedDisplay.updatedClip.has_value(),
            "control characters should be rejected at the metadata boundary");
    require(!fs::exists(fixture.path() / "escape.aiff"),
            "invalid rename planning must not touch the filesystem");

    const auto externalPath = fixture.path() / "external sound.ogg";
    writeFile(externalPath);
    cuelet::SoundClip linked = clip;
    linked.relativePath = LinuxLibraryImportService::linkedMetadataKey(externalPath);
    linked.storageMode = cuelet::SoundStorageMode::Linked;
    linked.absolutePath = normalized(externalPath);
    linked.externalPath = normalized(externalPath);
    linked.originalSourcePath = normalized(externalPath);
    linked.filename = externalPath.filename().u8string();
    linked.sourceFileName = linked.filename;
    const auto linkedRename = LinuxLibraryImportService::planRename(
        linked, "external renamed", library,
        LinuxLibraryImportService::RenameMode::RenameFile);
    require(linkedRename.valid && linkedRename.requiresFileRename
                && linkedRename.affectsExternalFile
                && linkedRename.updatedClip.has_value()
                && linkedRename.updatedClip->relativePath == linked.relativePath,
            "linked rename planning should expose the external effect and preserve metadata identity");
    require(linkedRename.updatedClip->externalPath
                == normalized(fixture.path() / "external renamed.ogg")
                && linkedRename.updatedClip->originalSourcePath
                    == linkedRename.updatedClip->externalPath,
            "linked rename metadata should consistently describe the planned external path");
    require(fs::is_regular_file(externalPath) && !fs::exists(linkedRename.newPath),
            "linked rename planning must not rename the original without confirmation");
}

} // namespace

int main()
{
    try {
        copyAndLinkProducePortableClipMetadata();
        successfulImportReplacesMissingPathAndPreservesUserMetadata();
        missingManagedSourceCanBeRestoredByReimport();
        batchValidationReportsPartialResults();
        collisionNamesAreDeterministicAcrossABatch();
        directoryPlanningHonorsRecursionAndReportsUnsupportedFiles();
        duplicatesAreDetectedBySourceIdentity();
        executionRejectsATamperedTraversalDestination();
        executionNeverOverwritesADestinationCreatedAfterPlanning();
        executionRejectsPathReplacementWithSymbolicLinks();
        symbolicLinksAreRejectedWithoutReadingTheirTargets();
        removalPlansProtectLinkedAndOutOfLibraryFiles();
        removalExecutionDeletesOnlyRevalidatedManagedFiles();
        renamePlanningIsNonMutatingAndTraversalSafe();
        std::cout << "cuelet Linux library import service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cuelet Linux library import service test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
