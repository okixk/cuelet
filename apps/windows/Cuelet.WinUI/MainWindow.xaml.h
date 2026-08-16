#pragma once

#include "MainWindow.g.h"
#include "CategoryIconMapper.h"
#include "WindowsMetadataStore.h"
#include "WindowsGlobalShortcutService.h"
#include "WindowsLifecycleModel.h"
#include "WindowsTrayIcon.h"
#include "WindowsVirtualAudioModel.h"
#include "WindowsWorkflowModel.h"
#include "cuelet/LibraryScanner.h"
#include "cuelet/SoundSearch.h"

#include <atomic>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <vector>

namespace winrt::Cuelet::WinUI::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();

        void Navigation_ItemInvoked(Microsoft::UI::Xaml::Controls::NavigationView const&, Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& args);
        void ChooseLibrary_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void CreateLibrary_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Import_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Rescan_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StopAll_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StopCurrent_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SearchBox_TextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const&);
        void SearchBox_QuerySubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const&, Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const&);
        void SoundDropTarget_DragOver(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void SoundDropTarget_DragLeave(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void SoundDropTarget_Drop(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
        void RunAudioSetup_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RefreshAudioDevices_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void TestMicrophone_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenMicrophonePrivacy_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void InstallVirtualDriver_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RepairVirtualDriver_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void UninstallVirtualDriver_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void RefreshVirtualDriver_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SortCombo_SelectionChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void GridView_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void ListView_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SoundSelection_Changed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void SoundCollection_PointerPressed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void VolumeSlider_ValueChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void Setting_Toggled(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        void OpenLibraryFromActivation(std::wstring const& path);
        cuelet::windows::CliExecutionResult ExecuteCli(cuelet::windows::CliRequest const& request);

    private:
        struct ActivePlayer {
            std::uint64_t token = 0;
            std::string clipId;
            Windows::Media::Playback::MediaPlayer player{nullptr};
            Windows::Media::Playback::MediaPlayer broadcastPlayer{nullptr};
        };

        void loadSettings();
        void saveSettings();
        void scanLibrary(bool showSuccess = false);
        void mergeMetadata(cuelet::LibraryMetadata const& metadata);
        bool saveMetadata(bool reportFailure = true);
        void rebuildCategories();
        Microsoft::UI::Xaml::Controls::MenuFlyout makeCategoryMenu(std::optional<std::string> categoryId);
        void refreshSounds(bool preserveSelection = false);
        std::vector<std::string> selectedClipIds();
        void restoreSoundSelection(std::vector<std::string> const& clipIds);
        void clearSoundSelection();
        void updateSoundVisualStates();
        void updateItemVisual(Microsoft::UI::Xaml::FrameworkElement const& item, bool pointerOver = false);
        void updateEmptyState(std::size_t visibleCount);
        void showStatus(std::wstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
        void showLibraryStartupState();
        void showDropState(std::optional<std::string> categoryId = std::nullopt,
                           bool internalSound = false);
        void setCategoryDropVisual(Microsoft::UI::Xaml::Controls::NavigationViewItem const& item);
        void clearCategoryDropVisual();
        std::string importCategoryForCurrentScope() const;
        void setScope(cuelet::LibraryScope scope, std::string categoryId = {});

        Microsoft::UI::Xaml::Controls::GridViewItem makeGridItem(cuelet::SoundClip const& clip);
        Microsoft::UI::Xaml::Controls::ListViewItem makeListItem(cuelet::SoundClip const& clip);
        Microsoft::UI::Xaml::Controls::MenuFlyout makeSoundMenu(std::string const& clipId);
        Microsoft::UI::Xaml::Controls::Border makeCategoryChip(cuelet::SoundClip const& clip) const;
        Microsoft::UI::Xaml::Controls::IconSourceElement makeCategoryIcon(std::string const& iconId, double size = 16) const;
        std::wstring displayLabel(cuelet::SoundClip const& clip) const;
        std::wstring categoryLabel(cuelet::SoundClip const& clip) const;
        cuelet::SoundClip* findClip(std::string const& id);
        std::string itemClipId(Windows::Foundation::IInspectable const& item) const;
        void prepareItemContextMenu(Microsoft::UI::Xaml::Controls::ListViewBase const& owner,
                                    Microsoft::UI::Xaml::Controls::Primitives::SelectorItem const& item);

        fire_and_forget chooseLibraryAsync();
        fire_and_forget createLibraryAsync();
        fire_and_forget importAsync();
        fire_and_forget importStorageItemsAsync(Windows::ApplicationModel::DataTransfer::DataPackageView data,
                                                std::string categoryId);
        fire_and_forget dropOnCategoryAsync(Windows::ApplicationModel::DataTransfer::DataPackageView data,
                                            std::string categoryId);
        fire_and_forget inspectDragItemsAsync(Microsoft::UI::Xaml::DragEventArgs args,
                                              std::optional<std::string> categoryId = std::nullopt);
        struct ImportSummary {
            std::size_t imported = 0;
            std::size_t linked = 0;
            std::size_t duplicates = 0;
            std::size_t skipped = 0;
            std::vector<std::wstring> details;
        };
        ImportSummary importPaths(std::vector<std::filesystem::path> const& paths,
                                  cuelet::windows::ImportBehavior behavior,
                                  std::string const& categoryId);
        void showImportWarning(std::wstring const& message, std::vector<std::wstring> details);
        fire_and_forget showImportDetailsAsync(std::vector<std::wstring> details);
        fire_and_forget startSoundDragAsync(Microsoft::UI::Xaml::DragStartingEventArgs args,
                                            std::string clipId);
        fire_and_forget indexSoundDurationsAsync(bool force = false);
        fire_and_forget playClipAsync(std::string clipId);
        fire_and_forget playExternalFileAsync(std::filesystem::path path);
        fire_and_forget testOutputDeviceAsync(
            std::string deviceId, bool shortTest = false);
        fire_and_forget initializeAudioRoutingAsync();
        void startAudioDeviceWatchers();
        void stopAudioDeviceWatchers() noexcept;
        void scheduleAudioDeviceRefresh();
        void refreshMicrophoneAccessState();
        fire_and_forget runAudioSetupAsync();
        void maybeRunAudioSetup();
        fire_and_forget refreshVirtualDriverStatusAsync();
        fire_and_forget runVirtualDriverActionAsync(std::wstring operation);
        Windows::Foundation::IAsyncOperation<bool> invokeVirtualDriverActionAsync(
            std::wstring operation, bool confirm);
        void applyVirtualDriverResult(Windows::Data::Json::JsonObject const& result);
        void updateVirtualDriverControls();
        fire_and_forget configureMicrophoneMixAsync();
        fire_and_forget testMicrophoneAsync();
        void stopMicrophoneTest(bool updateUi = true);
        void updateMicrophoneLevel();
        void audioRoutingChanged();
        Windows::Devices::Enumeration::DeviceInformation findRenderDevice(std::string const& id) const;
        Windows::Devices::Enumeration::DeviceInformation findCaptureDevice(std::string const& id) const;
        fire_and_forget renameClipAsync(std::string clipId);
        fire_and_forget locateLinkedSourceAsync(std::string clipId);
        fire_and_forget changeShortcutAsync(std::string clipId);
        fire_and_forget clearAllShortcutsAsync();
        fire_and_forget removeClipAsync(std::string clipId);
        fire_and_forget removeClipsAsync(std::vector<std::string> clipIds);
        fire_and_forget createCategoryAsync(std::optional<std::string> assignClipId = std::nullopt);
        fire_and_forget editCategoryAsync(std::optional<std::string> categoryId, std::optional<std::string> assignClipId = std::nullopt);
        fire_and_forget deleteCategoryAsync(std::string categoryId);
        void assignCategory(std::string const& clipId, std::string const& categoryId);
        void showClipInExplorer(std::string const& clipId);
        void toggleFavorite(std::string const& clipId);
        bool isClipPlaying(std::string const& clipId) const;
        void stopClip(std::string const& clipId);
        void stopPlayer(std::uint64_t token);
        void stopCurrent();
        void stopAll();
        void pruneStoppedPlayers();
        void updatePlaybackBar();
        void handleKeyDown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void registerGlobalShortcuts(bool reportFailures = false);
        cuelet::windows::ShortcutCheckResult checkShortcut(
            std::string const& clipId, cuelet::Shortcut const& shortcut) const;
        bool assignShortcutTransactional(std::string const& clipId,
                                         std::optional<cuelet::Shortcut> shortcut,
                                         bool replaceExisting,
                                         std::wstring* failureReason = nullptr);
        void refreshShortcutSettings();
        static LRESULT CALLBACK windowSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                   UINT_PTR subclassId, DWORD_PTR referenceData);
        void handleCliCopyData(std::wstring const& payload);
        void beginFinalShutdown(cuelet::windows::ShutdownReason reason) noexcept;
        void cleanupNativeResources() noexcept;
        bool acceptsUiWork(std::uint64_t generation) const noexcept;
        Windows::Foundation::IAsyncOperation<
            Microsoft::UI::Xaml::Controls::ContentDialogResult>
        showDialogAsync(Microsoft::UI::Xaml::Controls::ContentDialog const& dialog);
        winrt::fire_and_forget showAboutAsync();
        void closeActiveDialog() noexcept;

        cuelet::LibraryScanner m_scanner;
        cuelet::windows::WindowsMetadataStore m_metadataStore;
        cuelet::windows::WindowsGlobalShortcutService m_globalShortcuts;
        cuelet::windows::WindowsTrayIcon m_trayIcon;
        cuelet::LibraryMetadata m_metadata;
        std::vector<cuelet::Category> m_categories;
        std::vector<cuelet::SoundClip> m_clips;
        std::vector<cuelet::SoundClip> m_visibleClips;
        std::vector<ActivePlayer> m_players;
        Microsoft::UI::Xaml::DispatcherTimer m_playbackTimer{nullptr};
        event_token m_playbackTimerToken{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_notificationTimer{nullptr};
        event_token m_notificationTimerToken{};
        cuelet::FilterOptions m_filter;
        std::filesystem::path m_libraryFolder;
        std::vector<Windows::Devices::Enumeration::DeviceInformation> m_renderDevices;
        std::vector<Windows::Devices::Enumeration::DeviceInformation> m_captureDevices;
        Windows::Media::Audio::AudioGraph m_microphoneGraph{nullptr};
        std::uint64_t m_audioGraphGeneration = 0;
        event_token m_microphoneGraphErrorToken{};
        Windows::Media::Audio::AudioDeviceInputNode m_microphoneInputNode{nullptr};
        Windows::Media::Audio::AudioDeviceOutputNode m_microphoneOutputNode{nullptr};
        Windows::Media::Audio::AudioGraph m_microphoneTestGraph{nullptr};
        Windows::Media::Audio::AudioDeviceInputNode m_microphoneTestInputNode{nullptr};
        Windows::Media::Audio::AudioFrameOutputNode m_microphoneTestFrameNode{nullptr};
        Microsoft::UI::Xaml::DispatcherTimer m_microphoneLevelTimer{nullptr};
        event_token m_microphoneLevelTimerToken{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_microphoneOpenTimer{nullptr};
        event_token m_microphoneOpenTimerToken{};
        Windows::Devices::Enumeration::DeviceWatcher m_renderDeviceWatcher{nullptr};
        Windows::Devices::Enumeration::DeviceWatcher m_captureDeviceWatcher{nullptr};
        event_token m_renderWatcherAddedToken{};
        event_token m_renderWatcherRemovedToken{};
        event_token m_renderWatcherUpdatedToken{};
        event_token m_captureWatcherAddedToken{};
        event_token m_captureWatcherRemovedToken{};
        event_token m_captureWatcherUpdatedToken{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_audioRefreshTimer{nullptr};
        event_token m_audioRefreshTimerToken{};
        Microsoft::UI::Xaml::Controls::NavigationViewItem m_activeCategoryDropItem{nullptr};
        Microsoft::UI::Xaml::Controls::ContentDialog m_activeDialog{nullptr};
        std::string m_playbackOutputId;
        std::string m_broadcastOutputId;
        std::string m_virtualCaptureId;
        std::string m_microphoneInputId;
        std::string m_selectedClipId;
        double m_volume = 0.8;
        double m_broadcastVolume = 0.8;
        double m_microphoneVolume = 0.8;
        double m_soundboardVolume = 1.0;
        bool m_allowMultiple = true;
        bool m_recursiveScan = true;
        bool m_showExtensions = false;
        bool m_gridView = true;
        bool m_monitorLocally = true;
        bool m_mixPhysicalMicrophone = true;
        bool m_audioSetupCompleted = false;
        bool m_audioSetupRunning = false;
        cuelet::windows::ImportBehavior m_importBehavior = cuelet::windows::ImportBehavior::Copy;
        bool m_loadingAudioDevices = true;
        bool m_audioDeviceEnumerationRunning = false;
        bool m_testingMicrophone = false;
        bool m_durationIndexRunning = false;
        bool m_durationIndexRequested = false;
        bool m_loadingSettings = true;
        bool m_keepRunningInBackground = false;
        bool m_minimizeToTray = false;
        bool m_backgroundHintShown = false;
        bool m_exiting = false;
        bool m_nativeCleanupCompleted = false;
        bool m_driverActionRunning = false;
        std::shared_ptr<std::atomic_bool> m_driverCancellation;
        std::atomic_uint32_t m_asyncOperations{0};
        cuelet::windows::VirtualAudioVerification m_virtualDriverVerification;
        cuelet::windows::VirtualAudioDriverStatus m_virtualDriverStatus =
            cuelet::windows::VirtualAudioDriverStatus::NotInstalled;
        std::wstring m_virtualDriverDiagnostic;
        cuelet::windows::ShutdownCoordinator m_shutdown;
        std::uint64_t m_nextPlaybackToken = 1;
        std::chrono::steady_clock::time_point m_lastSearchPlayAt{};
        std::string m_lastSearchPlayId;
        HWND m_hwnd = nullptr;
    };
}

namespace winrt::Cuelet::WinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
