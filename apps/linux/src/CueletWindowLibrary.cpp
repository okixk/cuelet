#include "CueletWindow.h"

#include <gio/gio.h>

#include <sstream>

void CueletWindow::loadInitialLibrary(bool demoMode)
{
    if (demoMode) {
        loadDemoLibrary(false);
        return;
    }

    if (!settings_.libraryPath.empty()) {
        if (std::filesystem::exists(settings_.libraryPath)) {
            loadLibrary(settings_.libraryPath);
            return;
        }
        missingLibraryPath_ = std::filesystem::u8path(settings_.libraryPath);
    }

    if (settings_.showsDemoLibrary) {
        loadDemoLibrary(false);
        return;
    }

    refreshAll();
}

bool CueletWindow::loadLibrary(const std::filesystem::path& folder)
{
    const auto scan = scanner_.scan(folder, settings_.scansSubfolders);
    if (!scan.warning.empty()) {
        showError(scan.warning);
        return false;
    }

    libraryPath_ = folder;
    missingLibraryPath_.clear();
    demoLibraryActive_ = false;
    clips_ = scan.clips;
    cuelet::MetadataStore metadataStore(cuelet::MetadataStore::metadataPathForLibrary(folder));
    auto metadata = metadataStore.load();
    if (!metadataStore.lastError().empty()) {
        showToast(metadataStore.lastError());
    }
    cuelet::MetadataStore::applyMetadata(clips_, metadata, folder);
    categories_ = cuelet::mergeCategories(metadata.categories, clips_);

    std::size_t unapprovedLinkedSounds = 0;
    for (auto& clip : clips_) {
        if (clip.storageMode != cuelet::SoundStorageMode::Linked) {
            continue;
        }
        std::error_code pathError;
        const auto externalPath =
            std::filesystem::u8path(clip.externalPath);
        const auto externalStatus =
            std::filesystem::symlink_status(externalPath, pathError);
        const bool safeRegularFile =
            !pathError
            && std::filesystem::is_regular_file(externalStatus)
            && !std::filesystem::is_symlink(externalStatus);
        if (clip.externalPath.empty()
            || !safeRegularFile
            || !LinuxSettingsStore::isLinkedPathApproved(
                settings_, externalPath)) {
            clip.missing = true;
            clip.absolutePath.clear();
            ++unapprovedLinkedSounds;
        }
    }

    bool durationMetadataChanged = false;
    for (auto& clip : clips_) {
        if (clip.missing) {
            continue;
        }
        const auto previousDuration = clip.durationSeconds;
        const auto previousKnown = clip.durationKnown;
        const auto previousSize = clip.durationFileSize;
        const auto previousModified = clip.durationModifiedSeconds;
        const auto previousSource = clip.durationSourcePath;
        LinuxAudioService::updateDurationMetadata(clip);
        durationMetadataChanged = durationMetadataChanged
            || clip.durationSeconds != previousDuration
            || clip.durationKnown != previousKnown
            || clip.durationFileSize != previousSize
            || clip.durationModifiedSeconds != previousModified
            || clip.durationSourcePath != previousSource;
    }

    settings_.libraryPath = folder.string();
    settings_.showsDemoLibrary = false;
    saveSettings();
    if (durationMetadataChanged) {
        saveMetadata();
    }
    selection_ = SidebarSelection{};
    selectedPaths_.clear();
    syncGlobalShortcuts();
    refreshAll();
    if (unapprovedLinkedSounds > 0) {
        showToast(
            "External links from library metadata stay unavailable until "
            "you import those files explicitly.");
    }
    return true;
}

void CueletWindow::loadDemoLibrary(bool persistChoice)
{
    const auto makeDemoClip = [](const std::string& id,
                                 const std::string& relativePath,
                                 const std::string& displayName,
                                 const std::string& categoryId,
                                 guint shortcutKey,
                                 bool favorite,
                                 double durationSeconds,
                                 std::time_t addedAt) {
        cuelet::SoundClip clip;
        clip.id = id;
        clip.relativePath = relativePath;
        clip.filename = relativePath;
        clip.displayName = displayName;
        clip.categoryId = categoryId;
        clip.shortcut = cuelet::Shortcut{
            shortcutKey,
            GDK_ALT_MASK,
            "Alt+" + std::to_string(shortcutKey - GDK_KEY_0),
        };
        clip.favorite = favorite;
        clip.missing = true;
        clip.durationSeconds = durationSeconds;
        clip.durationKnown = true;
        clip.addedAt = addedAt;
        return clip;
    };

    libraryPath_.clear();
    missingLibraryPath_.clear();
    demoLibraryActive_ = true;
    categories_ = {
        cuelet::uncategorizedCategory(),
        {"demo-ambience", "Ambience", "#009688", "weather-showers-symbolic", true},
        {"demo-effects", "Effects", "#5856D6", "applications-games-symbolic", true},
        {"demo-music", "Music", "#AF52DE", "audio-x-generic-symbolic", true},
        {"demo-alerts", "Alerts", "#D9822B", "preferences-system-notifications-symbolic", true},
    };
    clips_ = {
        makeDemoClip("demo-rain", "rain-window.wav", "Rain on Window", "demo-ambience", GDK_KEY_1, true, 72.0, 1),
        makeDemoClip("demo-door", "door-knock.wav", "Door Knock", "demo-effects", GDK_KEY_2, false, 3.0, 2),
        makeDemoClip("demo-tone", "soft-room-tone.flac", "Soft Room Tone", "demo-ambience", GDK_KEY_3, false, 96.0, 3),
        makeDemoClip("demo-pop", "message-pop.wav", "Message Pop", "demo-alerts", GDK_KEY_4, true, 1.0, 4),
        makeDemoClip("demo-theme", "tension-bed.m4a", "Tension Bed", "demo-music", GDK_KEY_5, false, 124.0, 5),
    };
    if (persistChoice) {
        settings_.showsDemoLibrary = true;
        saveSettings();
    }
    syncGlobalShortcuts();
    refreshAll();
}

bool CueletWindow::rescanLibrary()
{
    if (libraryPath_.empty()) {
        showToast("Choose a library first.");
        return false;
    }
    return loadLibrary(libraryPath_);
}

void CueletWindow::chooseLibrary()
{
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose Sound Library");
    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        GError* error = nullptr;
        GFile* file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
        if (!file) {
            if (error) {
                g_error_free(error);
            }
            return;
        }
        char* path = g_file_get_path(file);
        if (path) {
            self->loadLibrary(path);
            g_free(path);
        }
        g_object_unref(file);
    }, this);
    g_object_unref(dialog);
}

void CueletWindow::importSounds()
{
    if (libraryPath_.empty()) {
        showToast("Choose a library before importing sounds.");
        chooseLibrary();
        return;
    }

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Import Sounds");
    gtk_file_dialog_open_multiple(dialog, GTK_WINDOW(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        GError* error = nullptr;
        GListModel* files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);
        if (!files) {
            if (error) {
                g_error_free(error);
            }
            return;
        }

        std::vector<std::filesystem::path> sources;
        std::size_t unavailableSources = 0;
        const guint count = g_list_model_get_n_items(files);
        sources.reserve(count);
        for (guint index = 0; index < count; ++index) {
            GFile* file = G_FILE(g_list_model_get_item(files, index));
            char* path = g_file_get_path(file);
            if (path) {
                sources.emplace_back(std::filesystem::u8path(path));
                g_free(path);
            } else {
                ++unavailableSources;
            }
            g_object_unref(file);
        }
        g_object_unref(files);
        self->importSources(sources, unavailableSources);
    }, this);
    g_object_unref(dialog);
}

void CueletWindow::importSources(
    const std::vector<std::filesystem::path>& sources,
    std::size_t unavailableSources)
{
    if (libraryPath_.empty()) {
        showToast("Choose a library before importing sounds.");
        return;
    }
    if (sources.empty()) {
        showToast(unavailableSources > 0
            ? "Only local files and folders can be imported."
            : "No files were selected for import.");
        return;
    }

    LinuxLibraryImportService::ImportRequest request;
    request.libraryFolder = libraryPath_;
    request.sources = sources;
    request.mode = settings_.copiesImportedFiles
        ? LinuxLibraryImportService::ImportMode::Copy
        : LinuxLibraryImportService::ImportMode::Link;
    request.acceptDirectories = true;
    request.scanSubfolders = settings_.scansSubfolders;
    request.categoryId = selection_.kind == SidebarKind::Category
        ? selection_.categoryId
        : "uncategorized";
    request.existingClips = clips_;

    const auto plan = LinuxLibraryImportService::planImport(request);
    const auto result = LinuxLibraryImportService::executeImport(plan);
    auto updatedClips = clips_;
    std::size_t copied = 0;
    std::size_t linked = 0;
    bool linkedApprovalChanged = false;
    std::string firstFailure;
    for (const auto& item : result.items) {
        if (item.clip) {
            cuelet::SoundClip imported = *item.clip;
            if (imported.storageMode == cuelet::SoundStorageMode::Linked
                && !imported.externalPath.empty()
                && !LinuxSettingsStore::isLinkedPathApproved(
                    settings_, std::filesystem::u8path(imported.externalPath))) {
                settings_ = LinuxSettingsStore::approvingLinkedPath(
                    settings_, std::filesystem::u8path(imported.externalPath));
                linkedApprovalChanged = true;
            }
            LinuxAudioService::updateDurationMetadata(imported);
            updatedClips = LinuxLibraryImportService::mergeImportedClip(
                updatedClips, std::move(imported));
        }
        if (item.status == LinuxLibraryImportService::ImportStatus::Imported) {
            ++copied;
        } else if (item.status == LinuxLibraryImportService::ImportStatus::Linked) {
            ++linked;
        } else if (firstFailure.empty()
                   && item.status != LinuxLibraryImportService::ImportStatus::Duplicate) {
            firstFailure = item.message;
        }
    }

    if (copied + linked > 0) {
        clips_ = std::move(updatedClips);
        if (linkedApprovalChanged) {
            saveSettings();
        }
        saveMetadata();
        refreshAll();
    }

    const std::size_t failed = result.failedCount() + unavailableSources;
    std::ostringstream summary;
    summary << "Added " << (copied + linked) << ((copied + linked) == 1 ? " sound" : " sounds");
    if (copied > 0 && linked > 0) {
        summary << " (" << copied << " copied, " << linked << " linked)";
    } else if (copied > 0) {
        summary << " (" << copied << " copied)";
    } else if (linked > 0) {
        summary << " (" << linked << " linked)";
    }
    if (result.duplicateCount() > 0) {
        summary << "; " << result.duplicateCount() << " already in the library";
    }
    if (failed > 0) {
        summary << "; " << failed << " could not be imported";
        if (!firstFailure.empty()) {
            summary << ": " << firstFailure;
        } else if (unavailableSources > 0) {
            summary << ": only local files and folders are supported";
        }
    }
    summary << ".";
    showToast(summary.str());
}
