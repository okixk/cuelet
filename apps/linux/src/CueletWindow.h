#pragma once

#include "CueletCli.h"
#include "cuelet/LibraryScanner.h"
#include "cuelet/MetadataStore.h"
#include "cuelet/SoundSearch.h"
#include "services/LinuxAudioService.h"
#include "services/LinuxGlobalShortcutsPortal.h"
#include "services/LinuxLibraryImportService.h"
#include "services/LinuxSettingsStore.h"
#include "services/LinuxVirtualMicrophoneService.h"

#include <adwaita.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class CueletWindow {
public:
    explicit CueletWindow(AdwApplication* application);
    ~CueletWindow();

    void present();
    void showAbout();
    void closeForCliExit();
    bool isClosedForCliExit() const;
    int executeCliCommand(const cuelet_linux::CliCommand& command,
                          std::string& standardOutput,
                          std::string& standardError);

private:
    enum class SidebarKind {
        Library,
        Favorites,
        Recent,
        AllCategories,
        Category,
    };

    struct SidebarSelection {
        SidebarKind kind = SidebarKind::Library;
        std::string categoryId;
    };

    AdwApplication* application_ = nullptr;
    AdwApplicationWindow* window_ = nullptr;
    AdwAboutDialog* aboutDialog_ = nullptr;
    AdwDialog* preferencesDialog_ = nullptr;
    AdwNavigationSplitView* splitView_ = nullptr;
    GtkWidget* toastOverlay_ = nullptr;
    GtkWidget* headerTitle_ = nullptr;
    GtkWidget* sidebarToggleButton_ = nullptr;
    GtkWidget* stopAllButton_ = nullptr;
    GtkWidget* sidebarList_ = nullptr;
    GtkWidget* titleLabel_ = nullptr;
    GtkWidget* subtitleLabel_ = nullptr;
    GtkWidget* countLabel_ = nullptr;
    GtkWidget* searchEntry_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* flowBox_ = nullptr;
    GtkWidget* listBox_ = nullptr;
    GtkWidget* emptyPage_ = nullptr;
    GtkWidget* emptyChooseButton_ = nullptr;
    GtkWidget* emptyImportButton_ = nullptr;
    GtkWidget* emptyClearSearchButton_ = nullptr;
    GtkWidget* emptyBrowseButton_ = nullptr;
    GtkWidget* emptyHelperLabel_ = nullptr;
    GtkWidget* nowPlayingBar_ = nullptr;
    GtkWidget* nowPlayingLabel_ = nullptr;
    GtkWidget* nowPlayingCategoryLabel_ = nullptr;
    GtkWidget* nowPlayingProgress_ = nullptr;
    GtkWidget* nowPlayingPauseButton_ = nullptr;
    GtkWidget* gridToggle_ = nullptr;
    GtkWidget* listToggle_ = nullptr;

    LinuxSettingsStore settingsStore_;
    LinuxSettings settings_;
    LinuxAudioService audio_;
    std::shared_ptr<cuelet_linux::LinuxGlobalShortcutsController> globalShortcuts_;
    std::unique_ptr<cuelet_linux::VirtualMicrophoneBackend> virtualMicrophoneBackend_;
    std::unique_ptr<cuelet_linux::LinuxVirtualMicrophoneService> virtualMicrophoneService_;
    cuelet::LibraryScanner scanner_;
    std::vector<cuelet::SoundClip> clips_;
    std::vector<cuelet::Category> categories_;
    std::filesystem::path libraryPath_;
    std::filesystem::path missingLibraryPath_;
    std::string lastSettingsSaveError_;
    SidebarSelection selection_;
    std::set<std::string> selectedPaths_;
    guint progressTickId_ = 0;
    guint virtualMicrophonePollId_ = 0;
    bool suppressToggleSignals_ = false;
    bool closedForCliExit_ = false;
#if defined(CUELET_ENABLE_DEVELOPER_TOOLS)
    bool visualCaptureScheduled_ = false;
    guint visualCaptureSourceId_ = 0;
#endif
    bool globalShortcutsStarted_ = false;

    void buildUi();
    void installActions();
    void installCss();
#if defined(CUELET_ENABLE_DEVELOPER_TOOLS)
    void scheduleVisualCaptureFromEnvironment();
#endif
    void applyAppearanceMode();
    void loadInitialLibrary();
    bool loadLibrary(const std::filesystem::path& folder);
    bool rescanLibrary();
    void chooseLibrary();
    void importSounds();
    void importSources(const std::vector<std::filesystem::path>& sources,
                       std::size_t unavailableSources = 0);
    void showPreferences();
    void refreshAll();
    void refreshSidebar();
    void refreshContent();
    void refreshHeader();
    void refreshNowPlaying();
    void refreshSelectionVisuals();
    void selectSound(const std::string& relativePath, bool extendSelection);
    void selectAllVisible();
    void clearSelection();
    std::string focusedSoundPath() const;
    bool presentSelectedSoundMenu();
    bool handleEscape();
    bool saveSettings();
    void saveMetadata();
    void syncGlobalShortcuts();
    void handleGlobalShortcutActivation(const std::string& soundId);
    void refreshShortcutPreferenceRows();
    std::string shortcutStatusText(const cuelet::SoundClip& clip) const;
    std::string shortcutBadgeText(const cuelet::SoundClip& clip) const;
    void showToast(const std::string& message);
    void showError(const std::string& message);
    void notifyPlaybackStarted();
    void withdrawPlaybackNotification();
    static bool parseOutputSetting(
        const std::string& value,
        LinuxAudioService::OutputSelection& selection);
    static std::string outputSetting(
        const LinuxAudioService::OutputSelection& selection);
    bool enableVirtualMicrophone();
    bool disableVirtualMicrophone();
    bool applyVirtualMicrophoneSettings(bool reportFailure = true);
    void pollVirtualMicrophone();
    bool virtualMicrophoneActive();
    bool virtualMicrophoneNeedsCleanup() const;
    std::string virtualMicrophoneStatus();
    std::string virtualMicrophoneEndpoint() const;
    std::vector<cuelet_linux::PhysicalMicrophoneInfo> physicalMicrophones();

    std::vector<cuelet::SoundClip> visibleClips() const;
    cuelet::FilterOptions filterOptions() const;
    cuelet::SoundClip* clipByPath(const std::string& relativePath);
    const cuelet::SoundClip* clipByPath(const std::string& relativePath) const;
    cuelet::Category* categoryById(const std::string& categoryId);
    const cuelet::Category* categoryById(const std::string& categoryId) const;
    std::string categoryName(const std::string& categoryId) const;
    std::string categoryColor(const std::string& categoryId) const;

    void playSound(const std::string& relativePath);
    void togglePlayback(const std::string& relativePath);
    void stopSound(const std::string& relativePath);
    void stopAll();
    void toggleFavorite(const std::string& relativePath);
    void assignCategory(const std::string& relativePath, const std::string& categoryId);
    void promptNewCategory(const std::string& assignRelativePath = {});
    void promptRenameCategory(const std::string& categoryId);
    void setCategoryColor(const std::string& categoryId, const std::string& colorHex);
    void setCategoryIcon(const std::string& categoryId, const std::string& iconId);
    void confirmDeleteCategory(const std::string& categoryId);
    void promptRenameSound(const std::string& relativePath);
    void revealSound(const std::string& relativePath);
    void eraseClipEntry(const std::string& relativePath);
    void confirmRemoveSound(const std::string& relativePath);
    void confirmDeleteManagedFile(const std::string& relativePath);
    void recordShortcut(const std::string& relativePath);
    void clearShortcut(const std::string& relativePath);
    void setShortcutGlobal(const std::string& relativePath, bool global);
    void copyGnomeShortcutCommand(const std::string& relativePath);
    bool handleLocalShortcut(guint keyval, GdkModifierType state);
    bool handleSearchKey(guint keyval);
    void playSelectionOrTopSearchResult();
    void playTopSearchResult();

    GtkWidget* makeSidebarRow(const std::string& title,
                              const char* iconName,
                              SidebarKind kind,
                              const std::string& categoryId = {});
    GtkWidget* makeSoundCard(const cuelet::SoundClip& clip);
    GtkWidget* makeSoundRow(const cuelet::SoundClip& clip);
    GtkWidget* makeSoundPopover(const std::string& relativePath);
    GtkWidget* makeCategoryPopover(const std::string& categoryId, bool editable);
    void presentPopover(GtkWidget* popover, GtkWidget* source, double x, double y);
};
