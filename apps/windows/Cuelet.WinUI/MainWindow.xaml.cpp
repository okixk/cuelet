#include "pch.h"
#include "MainWindow.xaml.h"
#include "WindowsText.h"
#include "WindowsAudioRoutingModel.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Media::Audio;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Core;
using namespace Windows::Media::Devices;
using namespace Windows::Media::Playback;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::System;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Microsoft::UI::Xaml::Automation;

namespace winrt::Cuelet::WinUI::implementation
{
    namespace
    {
        Brush themeBrush(wchar_t const* name)
        {
            if (auto value = Application::Current().Resources().TryLookup(box_value(name))) {
                return value.try_as<Brush>();
            }
            return nullptr;
        }

        Style applicationStyle(wchar_t const* name)
        {
            if (auto value = Application::Current().Resources().TryLookup(box_value(name))) {
                return value.try_as<Style>();
            }
            return nullptr;
        }

        bool eventOriginatesInInteractiveControl(IInspectable const& source, DependencyObject const& boundary)
        {
            auto current = source.try_as<DependencyObject>();
            while (current && current != boundary) {
                if (current.try_as<ButtonBase>() || current.try_as<TextBox>() ||
                    current.try_as<ComboBox>() || current.try_as<ScrollBar>()) return true;
                current = VisualTreeHelper::GetParent(current);
            }
            return false;
        }

        bool eventOriginatesInItem(IInspectable const& source, DependencyObject const& boundary)
        {
            auto current = source.try_as<DependencyObject>();
            while (current && current != boundary) {
                if (current.try_as<GridViewItem>() || current.try_as<ListViewItem>() ||
                    current.try_as<ScrollBar>() || current.try_as<ButtonBase>()) return true;
                current = VisualTreeHelper::GetParent(current);
            }
            return false;
        }

        std::vector<std::string> splitAliases(std::wstring const& value)
        {
            std::vector<std::string> aliases;
            std::wstringstream stream(value);
            std::wstring item;
            while (std::getline(stream, item, L',')) {
                auto alias = cuelet::trim(cuelet::windows::wideToUtf8(item));
                if (!alias.empty()) aliases.push_back(std::move(alias));
            }
            return aliases;
        }

        std::wstring joinAliases(std::vector<std::string> const& aliases)
        {
            std::wstring joined;
            for (auto const& alias : aliases) {
                if (!joined.empty()) joined += L", ";
                joined += cuelet::windows::utf8ToWide(alias);
            }
            return joined;
        }

        std::wstring formatDuration(double seconds)
        {
            if (seconds <= 0) return L"";
            auto total = static_cast<int>(seconds + 0.5);
            const auto minutes = total / 60;
            const auto remainder = total % 60;
            wchar_t buffer[32]{};
            swprintf_s(buffer, L"%d:%02d", minutes, remainder);
            return buffer;
        }

        std::optional<std::wstring> readRegistryString(wchar_t const* name)
        {
            DWORD bytes = 0;
            if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Cuelet", name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
                return std::nullopt;
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Cuelet", name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
                return std::nullopt;
            }
            while (!value.empty() && value.back() == L'\0') value.pop_back();
            return value;
        }

        DWORD readRegistryDword(wchar_t const* name, DWORD fallback)
        {
            DWORD value = fallback;
            DWORD bytes = sizeof(value);
            if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Cuelet", name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) {
                return fallback;
            }
            return value;
        }

        void writeRegistryString(HKEY key, wchar_t const* name, std::wstring const& value)
        {
            ::RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<BYTE const*>(value.c_str()),
                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        }

        void writeRegistryDword(HKEY key, wchar_t const* name, DWORD value)
        {
            ::RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof(value));
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&m_hwnd));
        if (!::SetWindowSubclass(m_hwnd, &MainWindow::windowSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
            throw_last_error();
        }
        m_globalShortcuts.attach(m_hwnd, [weak = get_weak()](std::string const& clipId) {
            if (auto self = weak.get()) self->playClipAsync(clipId);
        });
        m_trayIcon.attach(m_hwnd);
        m_categories.push_back(cuelet::uncategorizedCategory());
        loadSettings();
        rebuildCategories();
        SearchBox().TextChanged({this, &MainWindow::SearchBox_TextChanged});
        SortCombo().SelectionChanged({this, &MainWindow::SortCombo_SelectionChanged});
        VolumeSlider().ValueChanged({this, &MainWindow::VolumeSlider_ValueChanged});
        MultiplePlaybackToggle().Toggled({this, &MainWindow::Setting_Toggled});
        RecursiveScanToggle().Toggled({this, &MainWindow::Setting_Toggled});
        ShowExtensionsToggle().Toggled({this, &MainWindow::Setting_Toggled});
        KeepBackgroundToggle().Toggled({this, &MainWindow::Setting_Toggled});
        MinimizeToTrayToggle().Toggled({this, &MainWindow::Setting_Toggled});
        MonitorLocallyToggle().Toggled([this](IInspectable const&, RoutedEventArgs const&) { audioRoutingChanged(); });
        MixMicrophoneToggle().Toggled([this](IInspectable const&, RoutedEventArgs const&) { audioRoutingChanged(); });
        PlaybackOutputCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
        BroadcastOutputCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
        MicrophoneInputCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
        BroadcastVolumeSlider().ValueChanged([this](IInspectable const&, RangeBaseValueChangedEventArgs const&) { audioRoutingChanged(); });
        MicrophoneVolumeSlider().ValueChanged([this](IInspectable const&, RangeBaseValueChangedEventArgs const&) { audioRoutingChanged(); });
        SoundboardVolumeSlider().ValueChanged([this](IInspectable const&, RangeBaseValueChangedEventArgs const&) { audioRoutingChanged(); });
        ReRegisterShortcutsButton().Click([this](IInspectable const&, RoutedEventArgs const&) {
            m_globalShortcuts.unregisterAll();
            registerGlobalShortcuts(true);
            refreshShortcutSettings();
        });
        ClearAllShortcutsButton().Click([this](IInspectable const&, RoutedEventArgs const&) { clearAllShortcutsAsync(); });
        RootGrid().KeyDown({this, &MainWindow::handleKeyDown});
        m_playbackTimer = DispatcherTimer();
        m_playbackTimer.Interval(std::chrono::milliseconds(100));
        m_playbackTimerToken = m_playbackTimer.Tick([weak = get_weak()](IInspectable const&, IInspectable const&) {
            if (auto self = weak.get()) self->updatePlaybackBar();
        });
        Closed([this](IInspectable const&, WindowEventArgs const&) {
            saveSettings();
            stopAll();
        });
        initializeAudioRoutingAsync();

        if (!m_libraryFolder.empty()) {
            scanLibrary();
        } else {
            refreshSounds();
        }
    }

    MainWindow::~MainWindow()
    {
        m_globalShortcuts.unregisterAll();
        if (m_hwnd) ::RemoveWindowSubclass(m_hwnd, &MainWindow::windowSubclassProc, 1);
        if (m_playbackTimer) {
            m_playbackTimer.Stop();
            m_playbackTimer.Tick(m_playbackTimerToken);
        }
        for (auto& active : m_players) {
            if (active.player) {
                active.player.Pause();
                active.player.Source(nullptr);
                active.player.Close();
            }
            if (active.broadcastPlayer) {
                active.broadcastPlayer.Pause();
                active.broadcastPlayer.Source(nullptr);
                active.broadcastPlayer.Close();
            }
        }
        m_players.clear();
        if (m_microphoneGraph) {
            m_microphoneGraph.Stop();
            m_microphoneGraph.Close();
        }
    }

    LRESULT CALLBACK MainWindow::windowSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                     UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        constexpr ULONG_PTR activationMessageId = 0x4355454C;
        auto self = reinterpret_cast<MainWindow*>(referenceData);
        if (message == WM_COPYDATA && self)
        {
            auto data = reinterpret_cast<COPYDATASTRUCT const*>(lparam);
            if (data && data->dwData == activationMessageId && data->lpData && data->cbData >= sizeof(wchar_t))
            {
                auto text = static_cast<wchar_t const*>(data->lpData);
                self->OpenLibraryFromActivation(text);
                return TRUE;
            }
        }
        if (message == WM_HOTKEY && self && self->m_globalShortcuts.handleHotKey(wparam)) return 0;
        if (message == cuelet::windows::WindowsTrayIcon::callbackMessage && self) {
            if (const auto command = self->m_trayIcon.handleCallback(lparam)) {
                if (*command == cuelet::windows::TrayCommand::Open) {
                    ::ShowWindow(hwnd, SW_RESTORE);
                    self->Activate();
                } else if (*command == cuelet::windows::TrayCommand::StopAll) {
                    self->stopAll();
                    self->refreshSounds(true);
                } else if (*command == cuelet::windows::TrayCommand::Exit) {
                    self->m_exiting = true;
                    self->m_globalShortcuts.unregisterAll();
                    self->m_trayIcon.remove();
                    self->stopAll();
                    ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            }
            return 0;
        }
        if (message == WM_SIZE && self && wparam == SIZE_MINIMIZED && self->m_minimizeToTray) {
            self->m_trayIcon.add(self->m_globalShortcuts.failureCount() == 0);
            ::ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (message == WM_CLOSE && self && self->m_keepRunningInBackground && !self->m_exiting) {
            const bool showHint = !self->m_backgroundHintShown;
            self->m_backgroundHintShown = true;
            self->saveSettings();
            self->m_trayIcon.add(self->m_globalShortcuts.failureCount() == 0, showHint);
            ::ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &MainWindow::windowSubclassProc, subclassId);
        return ::DefSubclassProc(hwnd, message, wparam, lparam);
    }

    void MainWindow::loadSettings()
    {
        if (auto path = readRegistryString(L"LibraryPath"); path && !path->empty()) m_libraryFolder = *path;
        m_volume = static_cast<double>(readRegistryDword(L"Volume", 800)) / 1000.0;
        m_allowMultiple = readRegistryDword(L"AllowMultiple", 1) != 0;
        m_recursiveScan = readRegistryDword(L"RecursiveScan", 1) != 0;
        m_showExtensions = readRegistryDword(L"ShowExtensions", 0) != 0;
        m_gridView = readRegistryDword(L"GridView", 1) != 0;
        m_keepRunningInBackground = readRegistryDword(L"KeepRunningInBackground", 0) != 0;
        m_minimizeToTray = readRegistryDword(L"MinimizeToTray", 0) != 0;
        m_backgroundHintShown = readRegistryDword(L"BackgroundHintShown", 0) != 0;
        if (auto value = readRegistryString(L"PlaybackOutputId")) m_playbackOutputId = cuelet::windows::wideToUtf8(*value);
        if (auto value = readRegistryString(L"BroadcastOutputId")) m_broadcastOutputId = cuelet::windows::wideToUtf8(*value);
        if (auto value = readRegistryString(L"MicrophoneInputId")) m_microphoneInputId = cuelet::windows::wideToUtf8(*value);
        m_monitorLocally = readRegistryDword(L"MonitorLocally", 1) != 0;
        m_mixPhysicalMicrophone = readRegistryDword(L"MixPhysicalMicrophone", 0) != 0;
        m_broadcastVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"BroadcastVolume", 800));
        m_microphoneVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"MicrophoneVolume", 800));
        m_soundboardVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"SoundboardVolume", 1000));
        const auto sortIndex = static_cast<int>(readRegistryDword(L"SortIndex", 0));

        VolumeSlider().Value(m_volume * 100.0);
        MultiplePlaybackToggle().IsOn(m_allowMultiple);
        RecursiveScanToggle().IsOn(m_recursiveScan);
        ShowExtensionsToggle().IsOn(m_showExtensions);
        GridViewButton().IsChecked(m_gridView);
        ListViewButton().IsChecked(!m_gridView);
        KeepBackgroundToggle().IsOn(m_keepRunningInBackground);
        MinimizeToTrayToggle().IsOn(m_minimizeToTray);
        MonitorLocallyToggle().IsOn(m_monitorLocally);
        MixMicrophoneToggle().IsOn(m_mixPhysicalMicrophone);
        BroadcastVolumeSlider().Value(m_broadcastVolume * 100.0);
        MicrophoneVolumeSlider().Value(m_microphoneVolume * 100.0);
        SoundboardVolumeSlider().Value(m_soundboardVolume * 100.0);
        SortCombo().SelectedIndex(std::clamp(sortIndex, 0, 6));
        m_loadingSettings = false;
        if (m_keepRunningInBackground) m_trayIcon.add(false);
    }

    void MainWindow::saveSettings()
    {
        HKEY key{};
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Cuelet", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        writeRegistryString(key, L"LibraryPath", m_libraryFolder.wstring());
        writeRegistryDword(key, L"Volume", static_cast<DWORD>(std::clamp(m_volume, 0.0, 1.0) * 1000.0));
        writeRegistryDword(key, L"AllowMultiple", m_allowMultiple ? 1 : 0);
        writeRegistryDword(key, L"RecursiveScan", m_recursiveScan ? 1 : 0);
        writeRegistryDword(key, L"ShowExtensions", m_showExtensions ? 1 : 0);
        writeRegistryDword(key, L"GridView", m_gridView ? 1 : 0);
        writeRegistryDword(key, L"KeepRunningInBackground", m_keepRunningInBackground ? 1 : 0);
        writeRegistryDword(key, L"MinimizeToTray", m_minimizeToTray ? 1 : 0);
        writeRegistryDword(key, L"BackgroundHintShown", m_backgroundHintShown ? 1 : 0);
        writeRegistryString(key, L"PlaybackOutputId", cuelet::windows::utf8ToWide(m_playbackOutputId));
        writeRegistryString(key, L"BroadcastOutputId", cuelet::windows::utf8ToWide(m_broadcastOutputId));
        writeRegistryString(key, L"MicrophoneInputId", cuelet::windows::utf8ToWide(m_microphoneInputId));
        writeRegistryDword(key, L"MonitorLocally", m_monitorLocally ? 1 : 0);
        writeRegistryDword(key, L"MixPhysicalMicrophone", m_mixPhysicalMicrophone ? 1 : 0);
        writeRegistryDword(key, L"BroadcastVolume", cuelet::windows::volumeToSetting(m_broadcastVolume));
        writeRegistryDword(key, L"MicrophoneVolume", cuelet::windows::volumeToSetting(m_microphoneVolume));
        writeRegistryDword(key, L"SoundboardVolume", cuelet::windows::volumeToSetting(m_soundboardVolume));
        const auto sortIndex = SortCombo().SelectedIndex();
        writeRegistryDword(key, L"SortIndex", static_cast<DWORD>(sortIndex < 0 ? 0 : sortIndex));
        ::RegCloseKey(key);
    }

    void MainWindow::scanLibrary(bool showSuccess)
    {
        if (m_libraryFolder.empty()) {
            m_clips.clear();
            registerGlobalShortcuts();
            refreshSounds();
            return;
        }

        auto scan = m_scanner.scan(m_libraryFolder, m_recursiveScan);
        auto loaded = m_metadataStore.load(m_libraryFolder);
        m_metadata = std::move(loaded.metadata);
        m_categories.clear();
        m_categories.push_back(cuelet::uncategorizedCategory());
        for (auto const& category : m_metadata.categories) {
            if (category.id != "uncategorized") m_categories.push_back(category);
        }
        m_clips = std::move(scan.clips);
        mergeMetadata(m_metadata);
        rebuildCategories();
        refreshSounds();
        registerGlobalShortcuts(true);

        ImportButton().IsEnabled(true);
        RescanButton().IsEnabled(true);
        LibraryPathText().Text(m_libraryFolder.wstring());

        if (!scan.warning.empty()) {
            showStatus(cuelet::windows::utf8ToWide(scan.warning), InfoBarSeverity::Error);
        } else if (!loaded.warning.empty()) {
            showStatus(cuelet::windows::utf8ToWide(loaded.warning), InfoBarSeverity::Warning);
        } else if (loaded.migratedFromV1) {
            showStatus(L"Legacy library metadata loaded. A backup will be created when changes are saved.", InfoBarSeverity::Informational);
        } else if (showSuccess) {
            showStatus(L"Library rescanned successfully.", InfoBarSeverity::Success);
        }
    }

    void MainWindow::mergeMetadata(cuelet::LibraryMetadata const& metadata)
    {
        std::vector<std::string> matched;
        for (auto& clip : m_clips) {
            auto it = metadata.soundsByRelativePath.find(clip.relativePath);
            if (it == metadata.soundsByRelativePath.end()) continue;
            auto const& stored = it->second;
            if (!stored.displayName.empty()) clip.displayName = stored.displayName;
            clip.categoryId = stored.categoryId.empty() ? "uncategorized" : stored.categoryId;
            clip.notes = stored.notes;
            clip.aliases = stored.aliases;
            clip.shortcut = stored.shortcut;
            clip.favorite = stored.favorite;
            if (stored.addedAt) clip.addedAt = *stored.addedAt;
            clip.lastPlayedAt = stored.lastPlayedAt;
            matched.push_back(clip.relativePath);
        }

        for (auto const& [relativePath, stored] : metadata.soundsByRelativePath) {
            if (std::find(matched.begin(), matched.end(), relativePath) != matched.end()) continue;
            cuelet::SoundClip clip;
            clip.relativePath = relativePath;
            clip.absolutePath = cuelet::windows::pathToUtf8(m_libraryFolder / cuelet::windows::pathFromUtf8(relativePath));
            clip.filename = cuelet::filenameFromPath(relativePath);
            clip.displayName = stored.displayName.empty() ? cuelet::displayNameFromFilename(clip.filename) : stored.displayName;
            clip.id = cuelet::stableIdForPath(relativePath);
            clip.categoryId = stored.categoryId;
            clip.notes = stored.notes;
            clip.aliases = stored.aliases;
            clip.shortcut = stored.shortcut;
            clip.favorite = stored.favorite;
            clip.missing = true;
            clip.addedAt = stored.addedAt.value_or(0);
            clip.lastPlayedAt = stored.lastPlayedAt;
            m_clips.push_back(std::move(clip));
        }
    }

    bool MainWindow::saveMetadata(bool reportFailure)
    {
        if (m_libraryFolder.empty()) return false;
        m_metadata.schemaVersion = 2;
        m_metadata.categories.clear();
        for (auto const& category : m_categories) {
            if (category.id != "uncategorized") m_metadata.categories.push_back(category);
        }
        m_metadata.soundsByRelativePath.clear();
        for (auto const& clip : m_clips) {
            cuelet::SoundMetadata stored;
            stored.displayName = clip.displayName;
            stored.categoryId = clip.categoryId;
            stored.notes = clip.notes;
            stored.aliases = clip.aliases;
            stored.shortcut = clip.shortcut;
            stored.favorite = clip.favorite;
            if (clip.addedAt > 0) stored.addedAt = clip.addedAt;
            stored.lastPlayedAt = clip.lastPlayedAt;
            m_metadata.soundsByRelativePath[clip.relativePath] = std::move(stored);
        }
        std::string error;
        if (!m_metadataStore.save(m_libraryFolder, m_metadata, &error)) {
            if (reportFailure) {
                showStatus(L"Could not save library metadata: " + cuelet::windows::utf8ToWide(error), InfoBarSeverity::Error);
            }
            return false;
        }
        return true;
    }

    void MainWindow::registerGlobalShortcuts(bool reportFailures)
    {
        m_globalShortcuts.update(m_clips);
        if (m_trayIcon.visible()) m_trayIcon.add(m_globalShortcuts.failureCount() == 0);
        if (reportFailures && m_globalShortcuts.failureCount() > 0) {
            showStatus(std::to_wstring(m_globalShortcuts.failureCount()) +
                       L" global shortcut(s) could not be registered. Open Change shortcut… for details.",
                       InfoBarSeverity::Warning);
        }
        refreshShortcutSettings();
    }

    void MainWindow::rebuildCategories()
    {
        auto items = Navigation().MenuItems();
        while (items.Size() > 7) items.RemoveAtEnd();
        if (auto allCategories = items.GetAt(5).try_as<NavigationViewItem>()) {
            allCategories.ContextFlyout(makeCategoryMenu(std::nullopt));
        }
        if (auto uncategorizedItem = items.GetAt(6).try_as<NavigationViewItem>()) {
            const auto uncategorized = cuelet::uncategorizedCategory();
            uncategorizedItem.Icon(makeCategoryIcon(uncategorized.iconName));
            uncategorizedItem.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            Grid content;
            content.Width(166);
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            content.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
            Microsoft::UI::Xaml::Shapes::Ellipse dot;
            dot.Width(8); dot.Height(8);
            dot.Fill(cuelet::windows::categoryColorBrush(uncategorized.colorHex));
            dot.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(dot, 1);
            TextBlock text;
            text.Text(L"Uncategorized");
            content.Children().Append(text);
            content.Children().Append(dot);
            uncategorizedItem.Content(content);
            uncategorizedItem.ContextFlyout(makeCategoryMenu(std::nullopt));
        }
        for (auto const& category : m_categories) {
            if (category.id == "uncategorized") continue;
            NavigationViewItem item;
            item.Tag(box_value(L"category:" + cuelet::windows::utf8ToHstring(category.id)));
            item.Icon(makeCategoryIcon(category.iconName));
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            Grid content;
            content.Width(166);
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            content.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
            Microsoft::UI::Xaml::Shapes::Ellipse dot;
            dot.Width(8); dot.Height(8);
            dot.Fill(cuelet::windows::categoryColorBrush(category.colorHex));
            dot.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(dot, 1);
            TextBlock text;
            text.Text(cuelet::windows::utf8ToHstring(category.name));
            content.Children().Append(text);
            content.Children().Append(dot);
            item.Content(content);
            item.ContextFlyout(makeCategoryMenu(category.id));
            items.Append(item);
        }
    }

    MenuFlyout MainWindow::makeCategoryMenu(std::optional<std::string> categoryId)
    {
        MenuFlyout menu;
        if (categoryId) {
            MenuFlyoutItem edit;
            edit.Text(L"Edit category…");
            edit.Icon(SymbolIcon(Symbol::Edit));
            edit.Click([weak = get_weak(), id = *categoryId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->editCategoryAsync(id);
            });
            menu.Items().Append(edit);
            MenuFlyoutItem remove;
            remove.Text(L"Delete Category");
            remove.Icon(SymbolIcon(Symbol::Delete));
            remove.Click([weak = get_weak(), id = *categoryId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->deleteCategoryAsync(id);
            });
            menu.Items().Append(remove);
            menu.Items().Append(MenuFlyoutSeparator());
        }
        MenuFlyoutItem create;
        create.Text(L"New category…");
        create.Icon(SymbolIcon(Symbol::Add));
        create.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->createCategoryAsync();
        });
        menu.Items().Append(create);
        return menu;
    }

    void MainWindow::refreshSounds(bool preserveSelection)
    {
        const auto selection = preserveSelection ? selectedClipIds() : std::vector<std::string>{};
        m_filter.searchText = cuelet::windows::hstringToUtf8(SearchBox().Text());
        switch (SortCombo().SelectedIndex()) {
        case 1: m_filter.sort = cuelet::SortOption::NameDescending; break;
        case 2: m_filter.sort = cuelet::SortOption::LatestAdded; break;
        case 4: m_filter.sort = cuelet::SortOption::DurationShortest; break;
        case 5: m_filter.sort = cuelet::SortOption::DurationLongest; break;
        case 6: m_filter.sort = cuelet::SortOption::Category; break;
        default: m_filter.sort = cuelet::SortOption::NameAscending; break;
        }
        m_visibleClips = cuelet::filterAndSortSounds(m_clips, m_categories, m_filter);
        if (SortCombo().SelectedIndex() == 3 && m_filter.scope != cuelet::LibraryScope::Recent) {
            std::stable_sort(m_visibleClips.begin(), m_visibleClips.end(), [](auto const& left, auto const& right) {
                return left.lastPlayedAt.value_or(0) > right.lastPlayedAt.value_or(0);
            });
        }

        SoundGrid().Items().Clear();
        SoundList().Items().Clear();
        if (m_gridView) {
            for (auto const& clip : m_visibleClips) SoundGrid().Items().Append(makeGridItem(clip));
        } else {
            for (auto const& clip : m_visibleClips) SoundList().Items().Append(makeListItem(clip));
        }
        SoundGrid().SelectedIndex(-1);
        SoundList().SelectedIndex(-1);
        m_selectedClipId.clear();
        if (!selection.empty()) restoreSoundSelection(selection);
        SoundGrid().Visibility(m_gridView && !m_visibleClips.empty() ? Visibility::Visible : Visibility::Collapsed);
        SoundList().Visibility(!m_gridView && !m_visibleClips.empty() ? Visibility::Visible : Visibility::Collapsed);
        updateEmptyState(m_visibleClips.size());
        refreshShortcutSettings();
    }

    std::vector<std::string> MainWindow::selectedClipIds()
    {
        std::vector<std::string> result;
        const auto list = m_gridView ? SoundGrid().as<ListViewBase>() : SoundList().as<ListViewBase>();
        for (auto const& value : list.SelectedItems()) {
            const auto id = itemClipId(value);
            if (!id.empty()) result.push_back(id);
        }
        return result;
    }

    void MainWindow::restoreSoundSelection(std::vector<std::string> const& clipIds)
    {
        const auto list = m_gridView ? SoundGrid().as<ListViewBase>() : SoundList().as<ListViewBase>();
        for (auto const& value : list.Items()) {
            if (auto element = value.try_as<FrameworkElement>()) {
                const auto id = itemClipId(element);
                if (std::find(clipIds.begin(), clipIds.end(), id) != clipIds.end()) {
                    if (auto selectorItem = element.try_as<SelectorItem>()) selectorItem.IsSelected(true);
                    m_selectedClipId = id;
                }
            }
        }
    }

    void MainWindow::clearSoundSelection()
    {
        SoundGrid().SelectedItems().Clear();
        SoundList().SelectedItems().Clear();
        SoundGrid().SelectedIndex(-1);
        SoundList().SelectedIndex(-1);
        m_selectedClipId.clear();
        updateSoundVisualStates();
    }

    void MainWindow::updateSoundVisualStates()
    {
        auto update = [&](auto const& items) {
            for (auto const& value : items) {
                if (auto element = value.template try_as<FrameworkElement>()) updateItemVisual(element);
            }
        };
        update(SoundGrid().Items());
        update(SoundList().Items());
    }

    void MainWindow::updateItemVisual(FrameworkElement const& item, bool pointerOver)
    {
        const auto content = item.try_as<ContentControl>();
        const auto card = content ? content.Content().try_as<Border>() : nullptr;
        if (!card) return;
        const auto selector = item.try_as<SelectorItem>();
        const bool selected = selector && selector.IsSelected();
        const bool playing = isClipPlaying(itemClipId(item));
        const bool selectionOutline = cuelet::shouldShowSelectionOutline(selected, playing);
        card.Background(themeBrush(selected ? L"SubtleFillColorSecondaryBrush"
                                            : playing ? L"LayerFillColorAltBrush"
                                            : pointerOver ? L"CardBackgroundFillColorSecondaryBrush"
                                                          : L"CardBackgroundFillColorDefaultBrush"));
        card.BorderBrush(selectionOutline ? themeBrush(L"AccentFillColorDefaultBrush")
                                  : pointerOver ? themeBrush(L"CardStrokeColorDefaultBrush")
                                                : SolidColorBrush(Windows::UI::Colors::Transparent()));
        card.BorderThickness(ThicknessHelper::FromUniformLength(2));
    }

    void MainWindow::updateEmptyState(std::size_t visibleCount)
    {
        const auto empty = visibleCount == 0;
        EmptyPanel().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        if (!empty) return;

        if (m_libraryFolder.empty()) {
            EmptyStateTitle().Text(L"No sound library");
            EmptyStateDescription().Text(L"Choose a folder of audio files to start building your soundboard.");
            EmptyChooseButton().Visibility(Visibility::Visible);
        } else if (!m_filter.searchText.empty()) {
            EmptyStateTitle().Text(L"No matching sounds");
            EmptyStateDescription().Text(L"Try another search or choose a different category.");
            EmptyChooseButton().Visibility(Visibility::Collapsed);
        } else if (m_filter.scope == cuelet::LibraryScope::Favorites) {
            EmptyStateTitle().Text(L"No favorites yet");
            EmptyStateDescription().Text(L"Open a sound’s menu and mark it as a favorite.");
            EmptyChooseButton().Visibility(Visibility::Collapsed);
        } else if (m_filter.scope == cuelet::LibraryScope::Recent) {
            EmptyStateTitle().Text(L"Nothing played recently");
            EmptyStateDescription().Text(L"Sounds you play will appear here.");
            EmptyChooseButton().Visibility(Visibility::Collapsed);
        } else {
            EmptyStateTitle().Text(L"No sounds here");
            EmptyStateDescription().Text(L"Import WAV, MP3, M4A, FLAC, OGG, AIFF, or AIF files, or rescan the folder.");
            EmptyChooseButton().Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::showStatus(std::wstring const& message, InfoBarSeverity severity)
    {
        StatusInfoBar().Message(message);
        StatusInfoBar().Severity(severity);
        StatusInfoBar().IsOpen(true);
    }

    void MainWindow::setScope(cuelet::LibraryScope scope, std::string categoryId)
    {
        m_filter.scope = scope;
        m_filter.categoryId = std::move(categoryId);
        refreshSounds();
    }

    GridViewItem MainWindow::makeGridItem(cuelet::SoundClip const& clip)
    {
        GridViewItem item;
        item.Tag(box_value(cuelet::windows::utf8ToHstring(clip.id)));
        item.Width(288);
        item.Height(190);
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        item.VerticalContentAlignment(VerticalAlignment::Stretch);
        item.Padding(ThicknessHelper::FromUniformLength(0));
        item.Style(applicationStyle(L"SoundGridItemContainerStyle"));
        item.ContextFlyout(makeSoundMenu(clip.id));

        Border card;
        card.Padding(ThicknessHelper::FromUniformLength(12));
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
        card.BorderThickness(ThicknessHelper::FromUniformLength(2));
        card.Background(themeBrush(L"CardBackgroundFillColorDefaultBrush"));
        card.BorderBrush(SolidColorBrush(Windows::UI::Colors::Transparent()));

        Grid layout;
        layout.RowSpacing(5);
        layout.RowDefinitions().Append(RowDefinition());
        layout.RowDefinitions().Append(RowDefinition());
        layout.RowDefinitions().Append(RowDefinition());
        layout.RowDefinitions().Append(RowDefinition());
        layout.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
        layout.RowDefinitions().GetAt(1).Height(GridLengthHelper::FromPixels(58));
        layout.RowDefinitions().GetAt(2).Height(GridLengthHelper::Auto());
        layout.RowDefinitions().GetAt(3).Height(GridLengthHelper::Auto());

        Grid controls;
        controls.ColumnSpacing(4);
        for (int i = 0; i < 4; ++i) controls.ColumnDefinitions().Append(ColumnDefinition());
        controls.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        for (int i = 1; i < 4; ++i) controls.ColumnDefinitions().GetAt(i).Width(GridLengthHelper::Auto());
        TextBlock state;
        state.Text(clip.missing ? L"Missing" : isClipPlaying(clip.id) ? L"Playing" : L"");
        state.Foreground(themeBrush(L"AccentTextFillColorPrimaryBrush"));
        state.VerticalAlignment(VerticalAlignment::Center);
        controls.Children().Append(state);
        Button play;
        play.Width(30); play.Height(30); play.Padding(ThicknessHelper::FromUniformLength(4));
        play.Content(SymbolIcon(Symbol::Play));
        ToolTipService::SetToolTip(play, box_value(L"Play " + displayLabel(clip)));
        AutomationProperties::SetName(play, L"Play " + displayLabel(clip));
        play.IsEnabled(!clip.missing);
        play.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) { if (auto self = weak.get()) self->playClipAsync(id); });
        Grid::SetColumn(play, 1); controls.Children().Append(play);
        Button stop;
        stop.Width(30); stop.Height(30); stop.Padding(ThicknessHelper::FromUniformLength(4));
        stop.Content(SymbolIcon(Symbol::Stop));
        stop.IsEnabled(isClipPlaying(clip.id));
        ToolTipService::SetToolTip(stop, box_value(L"Stop this sound"));
        AutomationProperties::SetName(stop, L"Stop " + displayLabel(clip));
        stop.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) { if (auto self = weak.get()) self->stopClip(id); });
        Grid::SetColumn(stop, 2); controls.Children().Append(stop);
        Button favorite;
        favorite.Width(30); favorite.Height(30); favorite.Padding(ThicknessHelper::FromUniformLength(4));
        FontIcon favoriteIcon;
        favoriteIcon.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        favoriteIcon.Glyph(clip.favorite ? L"\xE735" : L"\xE734");
        favorite.Content(favoriteIcon);
        ToolTipService::SetToolTip(favorite, box_value(clip.favorite ? L"Remove from favorites" : L"Add to favorites"));
        AutomationProperties::SetName(favorite, clip.favorite ? L"Remove from favorites" : L"Add to favorites");
        favorite.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) { if (auto self = weak.get()) self->toggleFavorite(id); });
        Grid::SetColumn(favorite, 3); controls.Children().Append(favorite);
        layout.Children().Append(controls);

        StackPanel waveform;
        waveform.Orientation(Orientation::Horizontal);
        waveform.Spacing(4);
        waveform.HorizontalAlignment(HorizontalAlignment::Stretch);
        waveform.VerticalAlignment(VerticalAlignment::Center);
        const auto waveformBrush = themeBrush(isClipPlaying(clip.id) ? L"AccentFillColorDefaultBrush" : L"TextFillColorSecondaryBrush");
        const auto seed = std::hash<std::string>{}(clip.relativePath.empty() ? clip.filename : clip.relativePath);
        for (int index = 0; index < 24; ++index) {
            Microsoft::UI::Xaml::Shapes::Rectangle bar;
            bar.Width(4);
            bar.Height(8 + static_cast<double>(((seed >> (index % 16)) + index * 7) % 30));
            bar.RadiusX(2); bar.RadiusY(2);
            bar.Fill(waveformBrush);
            bar.VerticalAlignment(VerticalAlignment::Center);
            waveform.Children().Append(bar);
        }
        Grid::SetRow(waveform, 1);
        layout.Children().Append(waveform);

        TextBlock title;
        title.Text(displayLabel(clip));
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        Grid::SetRow(title, 2);
        layout.Children().Append(title);

        Grid footer;
        footer.ColumnDefinitions().Append(ColumnDefinition());
        footer.ColumnDefinitions().Append(ColumnDefinition());
        footer.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        footer.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
        footer.Children().Append(makeCategoryChip(clip));
        TextBlock badge;
        badge.Text(clip.shortcut ? hstring(cuelet::windows::formatShortcut(*clip.shortcut)) : hstring(formatDuration(clip.durationSeconds)));
        badge.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        badge.PointerPressed([](IInspectable const&, PointerRoutedEventArgs const& args) { args.Handled(true); });
        Grid::SetColumn(badge, 1);
        footer.Children().Append(badge);
        Grid::SetRow(footer, 3);
        layout.Children().Append(footer);
        card.Child(layout);
        item.Content(card);
        auto weakItem = make_weak(item);
        card.PointerEntered([weak = get_weak(), weakItem](IInspectable const&, PointerRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->updateItemVisual(current, true);
        });
        card.PointerExited([weak = get_weak(), weakItem](IInspectable const&, PointerRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->updateItemVisual(current);
        });
        item.DoubleTapped([weak = get_weak(), id = clip.id, boundary = DependencyObject(card)](IInspectable const&, DoubleTappedRoutedEventArgs const& args) {
            if (!eventOriginatesInInteractiveControl(args.OriginalSource(), boundary)) if (auto self = weak.get()) self->playClipAsync(id);
        });
        item.RightTapped([weak = get_weak(), weakItem](IInspectable const&, RightTappedRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->prepareItemContextMenu(self->SoundGrid(), current);
        });
        return item;
    }

    ListViewItem MainWindow::makeListItem(cuelet::SoundClip const& clip)
    {
        ListViewItem item;
        item.Tag(box_value(cuelet::windows::utf8ToHstring(clip.id)));
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        item.VerticalContentAlignment(VerticalAlignment::Stretch);
        item.Padding(ThicknessHelper::FromUniformLength(0));
        item.Style(applicationStyle(L"SoundItemContainerStyle"));
        item.ContextFlyout(makeSoundMenu(clip.id));

        Border card;
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        card.BorderThickness(ThicknessHelper::FromUniformLength(2));
        card.BorderBrush(SolidColorBrush(Windows::UI::Colors::Transparent()));
        card.Background(themeBrush(L"CardBackgroundFillColorDefaultBrush"));
        card.Padding(Thickness{10, 7, 10, 7});
        card.Margin(Thickness{0, 2, 0, 2});
        Grid row;
        row.ColumnSpacing(12);
        for (int i = 0; i < 6; ++i) row.ColumnDefinitions().Append(ColumnDefinition());
        row.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(32));
        row.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        row.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::FromPixels(170));
        row.ColumnDefinitions().GetAt(3).Width(GridLengthHelper::FromPixels(110));
        row.ColumnDefinitions().GetAt(4).Width(GridLengthHelper::FromPixels(34));
        row.ColumnDefinitions().GetAt(5).Width(GridLengthHelper::FromPixels(34));
        Button play;
        play.Width(30); play.Height(30); play.Padding(ThicknessHelper::FromUniformLength(4));
        play.Content(SymbolIcon(isClipPlaying(clip.id) ? Symbol::Stop : Symbol::Play));
        play.IsEnabled(!clip.missing);
        ToolTipService::SetToolTip(play, box_value(isClipPlaying(clip.id) ? L"Stop this sound" : L"Play this sound"));
        AutomationProperties::SetName(play, isClipPlaying(clip.id) ? L"Stop " + displayLabel(clip) : L"Play " + displayLabel(clip));
        play.Click([weak = get_weak(), id = clip.id, playing = isClipPlaying(clip.id)](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) { if (playing) self->stopClip(id); else self->playClipAsync(id); }
        });
        row.Children().Append(play);
        TextBlock title;
        title.Text(displayLabel(clip));
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.VerticalAlignment(VerticalAlignment::Center);
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        Grid::SetColumn(title, 1);
        row.Children().Append(title);
        auto category = makeCategoryChip(clip);
        category.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(category, 2);
        row.Children().Append(category);
        TextBlock shortcut;
        shortcut.Text(clip.shortcut ? hstring(cuelet::windows::formatShortcut(*clip.shortcut)) : hstring(formatDuration(clip.durationSeconds)));
        shortcut.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        shortcut.HorizontalAlignment(HorizontalAlignment::Right);
        shortcut.VerticalAlignment(VerticalAlignment::Center);
        shortcut.PointerPressed([](IInspectable const&, PointerRoutedEventArgs const& args) { args.Handled(true); });
        Grid::SetColumn(shortcut, 3);
        row.Children().Append(shortcut);
        Button favorite;
        favorite.Width(30); favorite.Height(30); favorite.Padding(ThicknessHelper::FromUniformLength(4));
        FontIcon favoriteIcon;
        favoriteIcon.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        favoriteIcon.Glyph(clip.favorite ? L"\xE735" : L"\xE734");
        favorite.Content(favoriteIcon);
        ToolTipService::SetToolTip(favorite, box_value(clip.favorite ? L"Remove from favorites" : L"Add to favorites"));
        AutomationProperties::SetName(favorite, clip.favorite ? L"Remove from favorites" : L"Add to favorites");
        favorite.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) { if (auto self = weak.get()) self->toggleFavorite(id); });
        Grid::SetColumn(favorite, 4); row.Children().Append(favorite);
        FontIcon state;
        state.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        state.Glyph(isClipPlaying(clip.id) ? L"\xE767" : L"");
        state.Foreground(themeBrush(L"AccentTextFillColorPrimaryBrush"));
        state.HorizontalAlignment(HorizontalAlignment::Center);
        state.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(state, 5); row.Children().Append(state);
        card.Child(row);
        item.Content(card);
        auto weakItem = make_weak(item);
        card.PointerEntered([weak = get_weak(), weakItem](IInspectable const&, PointerRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->updateItemVisual(current, true);
        });
        card.PointerExited([weak = get_weak(), weakItem](IInspectable const&, PointerRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->updateItemVisual(current);
        });
        item.DoubleTapped([weak = get_weak(), id = clip.id, boundary = DependencyObject(card)](IInspectable const&, DoubleTappedRoutedEventArgs const& args) {
            if (!eventOriginatesInInteractiveControl(args.OriginalSource(), boundary)) if (auto self = weak.get()) self->playClipAsync(id);
        });
        item.RightTapped([weak = get_weak(), weakItem](IInspectable const&, RightTappedRoutedEventArgs const&) {
            if (auto self = weak.get()) if (auto current = weakItem.get()) self->prepareItemContextMenu(self->SoundList(), current);
        });
        return item;
    }

    Border MainWindow::makeCategoryChip(cuelet::SoundClip const& clip) const
    {
        const auto category = cuelet::categoryForId(m_categories, clip.categoryId);
        const auto color = category ? category->colorHex : std::string{"#8E8E93"};
        Border chip;
        chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
        chip.Padding(Thickness{6, 2, 7, 2});
        chip.Background(themeBrush(L"SubtleFillColorSecondaryBrush"));
        chip.PointerPressed([](IInspectable const&, PointerRoutedEventArgs const& args) { args.Handled(true); });
        StackPanel content;
        content.Orientation(Orientation::Horizontal);
        content.Spacing(5);
        Microsoft::UI::Xaml::Shapes::Ellipse dot;
        dot.Width(7); dot.Height(7);
        dot.Fill(cuelet::windows::categoryColorBrush(color));
        dot.VerticalAlignment(VerticalAlignment::Center);
        TextBlock label;
        label.Text(categoryLabel(clip));
        label.Style(applicationStyle(L"CaptionTextBlockStyle"));
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        label.MaxWidth(120);
        content.Children().Append(label);
        content.Children().Append(dot);
        chip.Child(content);
        return chip;
    }

    IconSourceElement MainWindow::makeCategoryIcon(std::string const& iconId, double size) const
    {
        IconSourceElement element;
        auto source = cuelet::windows::iconForCategoryId(iconId);
        if (auto font = source.try_as<FontIconSource>()) font.FontSize(size);
        element.IconSource(source);
        return element;
    }

    MenuFlyout MainWindow::makeSoundMenu(std::string const& clipId)
    {
        MenuFlyout flyout;
        const auto selected = selectedClipIds();
        if (selected.size() > 1 && std::find(selected.begin(), selected.end(), clipId) != selected.end()) {
            const bool allFavorite = std::all_of(selected.begin(), selected.end(), [&](auto const& id) {
                const auto clip = findClip(id);
                return clip && clip->favorite;
            });
            MenuFlyoutItem favoriteSelected;
            favoriteSelected.Text(allFavorite ? L"Unfavorite selected" : L"Favorite selected");
            favoriteSelected.Icon(SymbolIcon(Symbol::Favorite));
            favoriteSelected.Click([weak = get_weak(), selected, value = !allFavorite](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) {
                    for (auto const& id : selected) if (auto clip = self->findClip(id)) clip->favorite = value;
                    self->saveMetadata();
                    self->refreshSounds(true);
                }
            });
            flyout.Items().Append(favoriteSelected);
            const bool anyPlaying = std::any_of(selected.begin(), selected.end(), [&](auto const& id) { return isClipPlaying(id); });
            if (anyPlaying) {
                MenuFlyoutItem stopSelected;
                stopSelected.Text(L"Stop selected");
                stopSelected.Icon(SymbolIcon(Symbol::Stop));
                stopSelected.Click([weak = get_weak(), selected](IInspectable const&, RoutedEventArgs const&) {
                    if (auto self = weak.get()) for (auto const& id : selected) self->stopClip(id);
                });
                flyout.Items().Append(stopSelected);
            }
            MenuFlyoutSubItem assignSelected;
            assignSelected.Text(L"Assign category to selected");
            assignSelected.Icon(SymbolIcon(Symbol::Tag));
            for (auto const& category : m_categories) {
                MenuFlyoutItem choice;
                choice.Text(cuelet::windows::utf8ToHstring(category.name));
                choice.Icon(makeCategoryIcon(category.iconName));
                choice.Click([weak = get_weak(), selected, categoryId = category.id](IInspectable const&, RoutedEventArgs const&) {
                    if (auto self = weak.get()) {
                        for (auto const& id : selected) if (auto clip = self->findClip(id)) clip->categoryId = categoryId;
                        self->saveMetadata();
                        self->refreshSounds(true);
                    }
                });
                assignSelected.Items().Append(choice);
            }
            flyout.Items().Append(assignSelected);
            flyout.Items().Append(MenuFlyoutSeparator());
            MenuFlyoutItem removeSelected;
            removeSelected.Text(L"Remove selected…");
            removeSelected.Icon(SymbolIcon(Symbol::Delete));
            removeSelected.Click([weak = get_weak(), selected](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->removeClipsAsync(selected);
            });
            flyout.Items().Append(removeSelected);
            return flyout;
        }
        MenuFlyoutItem play;
        play.Text(L"Play");
        play.Icon(SymbolIcon(Symbol::Play));
        play.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->playClipAsync(clipId);
        });
        flyout.Items().Append(play);
        if (isClipPlaying(clipId)) {
            MenuFlyoutItem stop;
            stop.Text(L"Stop");
            stop.Icon(SymbolIcon(Symbol::Stop));
            stop.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->stopClip(clipId);
            });
            flyout.Items().Append(stop);
        }
        MenuFlyoutItem favorite;
        if (auto clip = findClip(clipId)) favorite.Text(clip->favorite ? L"Unfavorite" : L"Favorite");
        favorite.Icon(SymbolIcon(Symbol::Favorite));
        favorite.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->toggleFavorite(clipId);
        });
        flyout.Items().Append(favorite);
        flyout.Items().Append(MenuFlyoutSeparator());
        MenuFlyoutSubItem assign;
        assign.Text(L"Assign category");
        assign.Icon(SymbolIcon(Symbol::Tag));
        for (auto const& category : m_categories) {
            MenuFlyoutItem choice;
            choice.Text(cuelet::windows::utf8ToHstring(category.name));
            choice.Icon(makeCategoryIcon(category.iconName));
            choice.Click([weak = get_weak(), clipId, categoryId = category.id](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->assignCategory(clipId, categoryId);
            });
            assign.Items().Append(choice);
        }
        assign.Items().Append(MenuFlyoutSeparator());
        MenuFlyoutItem newCategory;
        newCategory.Text(L"New category…");
        newCategory.Icon(SymbolIcon(Symbol::Add));
        newCategory.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->createCategoryAsync(clipId);
        });
        assign.Items().Append(newCategory);
        flyout.Items().Append(assign);
        MenuFlyoutItem shortcut;
        shortcut.Text(L"Change shortcut…");
        shortcut.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->changeShortcutAsync(clipId);
        });
        flyout.Items().Append(shortcut);
        if (auto clip = findClip(clipId); clip && clip->shortcut) {
            MenuFlyoutItem clearShortcut;
            clearShortcut.Text(L"Clear shortcut");
            clearShortcut.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) {
                    std::wstring reason;
                    if (!self->assignShortcutTransactional(clipId, std::nullopt, false, &reason)) {
                        self->showStatus(reason, InfoBarSeverity::Error);
                    }
                }
            });
            flyout.Items().Append(clearShortcut);
        }
        MenuFlyoutItem rename;
        rename.Text(L"Rename…");
        rename.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->renameClipAsync(clipId);
        });
        flyout.Items().Append(rename);
        MenuFlyoutItem edit;
        edit.Text(L"Edit metadata…");
        edit.Icon(SymbolIcon(Symbol::Edit));
        edit.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->editClipAsync(clipId);
        });
        flyout.Items().Append(edit);
        MenuFlyoutItem reveal;
        reveal.Text(L"Reveal in File Explorer");
        reveal.Icon(SymbolIcon(Symbol::OpenFile));
        reveal.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->showClipInExplorer(clipId);
        });
        flyout.Items().Append(reveal);
        flyout.Items().Append(MenuFlyoutSeparator());
        MenuFlyoutItem remove;
        remove.Text(L"Remove from library…");
        remove.Icon(SymbolIcon(Symbol::Delete));
        remove.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->removeClipAsync(clipId);
        });
        flyout.Items().Append(remove);
        return flyout;
    }

    std::wstring MainWindow::displayLabel(cuelet::SoundClip const& clip) const
    {
        if (m_showExtensions) return cuelet::windows::utf8ToWide(clip.filename);
        return cuelet::windows::utf8ToWide(clip.searchableName());
    }

    std::wstring MainWindow::categoryLabel(cuelet::SoundClip const& clip) const
    {
        if (auto category = cuelet::categoryForId(m_categories, clip.categoryId)) return cuelet::windows::utf8ToWide(category->name);
        return L"Uncategorized";
    }

    cuelet::SoundClip* MainWindow::findClip(std::string const& id)
    {
        auto found = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) { return clip.id == id; });
        return found == m_clips.end() ? nullptr : &*found;
    }

    std::string MainWindow::itemClipId(IInspectable const& item) const
    {
        if (auto container = item.try_as<FrameworkElement>()) return cuelet::windows::hstringToUtf8(unbox_value_or<hstring>(container.Tag(), L""));
        return {};
    }

    fire_and_forget MainWindow::chooseLibraryAsync()
    {
        auto lifetime = get_strong();
        try {
            FolderPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::MusicLibrary);
            picker.FileTypeFilter().Append(L"*");
            HWND hwnd{};
            check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            auto folder = co_await picker.PickSingleFolderAsync();
            if (!folder) co_return;
            m_libraryFolder = std::filesystem::path(folder.Path().c_str());
            saveSettings();
            setScope(cuelet::LibraryScope::All);
            scanLibrary();
        } catch (hresult_error const& error) {
            showStatus(L"Could not open the folder picker: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::importAsync()
    {
        auto lifetime = get_strong();
        if (m_libraryFolder.empty()) co_return;
        try {
            FileOpenPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::MusicLibrary);
            for (auto const& extension : cuelet::LibraryScanner::supportedExtensions()) {
                picker.FileTypeFilter().Append(L"." + cuelet::windows::utf8ToHstring(extension));
            }
            HWND hwnd{};
            check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            auto files = co_await picker.PickMultipleFilesAsync();
            if (files.Size() == 0) co_return;
            auto destination = co_await StorageFolder::GetFolderFromPathAsync(m_libraryFolder.wstring());
            unsigned int imported = 0;
            for (auto const& file : files) {
                co_await file.CopyAsync(destination, file.Name(), NameCollisionOption::GenerateUniqueName);
                ++imported;
            }
            scanLibrary();
            showStatus(std::to_wstring(imported) + (imported == 1 ? L" sound imported." : L" sounds imported."), InfoBarSeverity::Success);
        } catch (hresult_error const& error) {
            showStatus(L"Import failed: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::initializeAudioRoutingAsync()
    {
        auto lifetime = get_strong();
        try {
            const auto renders = co_await DeviceInformation::FindAllAsync(MediaDevice::GetAudioRenderSelector());
            const auto captures = co_await DeviceInformation::FindAllAsync(MediaDevice::GetAudioCaptureSelector());
            m_renderDevices.assign(renders.begin(), renders.end());
            m_captureDevices.assign(captures.begin(), captures.end());

            auto addChoice = [](ComboBox const& combo, std::wstring const& label, std::string const& id) {
                ComboBoxItem item;
                item.Content(box_value(label));
                item.Tag(box_value(cuelet::windows::utf8ToHstring(id)));
                combo.Items().Append(item);
            };
            addChoice(PlaybackOutputCombo(), L"System default", {});
            addChoice(BroadcastOutputCombo(), L"Disabled", {});
            addChoice(MicrophoneInputCombo(), L"System default", {});
            int playbackIndex = 0;
            int broadcastIndex = 0;
            int microphoneIndex = 0;
            for (auto const& device : m_renderDevices) {
                const auto id = cuelet::windows::hstringToUtf8(device.Id());
                addChoice(PlaybackOutputCombo(), device.Name().c_str(), id);
                addChoice(BroadcastOutputCombo(), device.Name().c_str(), id);
                if (id == m_playbackOutputId) playbackIndex = static_cast<int>(PlaybackOutputCombo().Items().Size()) - 1;
                if (id == m_broadcastOutputId) broadcastIndex = static_cast<int>(BroadcastOutputCombo().Items().Size()) - 1;
            }
            for (auto const& device : m_captureDevices) {
                const auto id = cuelet::windows::hstringToUtf8(device.Id());
                addChoice(MicrophoneInputCombo(), device.Name().c_str(), id);
                if (id == m_microphoneInputId) microphoneIndex = static_cast<int>(MicrophoneInputCombo().Items().Size()) - 1;
            }
            PlaybackOutputCombo().SelectedIndex(playbackIndex);
            BroadcastOutputCombo().SelectedIndex(broadcastIndex);
            MicrophoneInputCombo().SelectedIndex(microphoneIndex);
            m_loadingAudioDevices = false;
            audioRoutingChanged();
        } catch (hresult_error const& error) {
            m_loadingAudioDevices = false;
            AudioRoutingInfo().Severity(InfoBarSeverity::Error);
            AudioRoutingInfo().Title(L"Audio routing: Error");
            AudioRoutingInfo().Message(L"Windows audio devices could not be enumerated: " + error.message());
        }
    }

    DeviceInformation MainWindow::findRenderDevice(std::string const& id) const
    {
        const auto found = std::find_if(m_renderDevices.begin(), m_renderDevices.end(), [&](auto const& device) {
            return cuelet::windows::hstringToUtf8(device.Id()) == id;
        });
        return found == m_renderDevices.end() ? nullptr : *found;
    }

    void MainWindow::audioRoutingChanged()
    {
        if (m_loadingAudioDevices || m_loadingSettings) return;
        auto selectedId = [](ComboBox const& combo) {
            if (auto item = combo.SelectedItem().try_as<ComboBoxItem>()) {
                return cuelet::windows::hstringToUtf8(unbox_value_or<hstring>(item.Tag(), L""));
            }
            return std::string{};
        };
        m_playbackOutputId = selectedId(PlaybackOutputCombo());
        m_broadcastOutputId = selectedId(BroadcastOutputCombo());
        m_microphoneInputId = selectedId(MicrophoneInputCombo());
        m_monitorLocally = MonitorLocallyToggle().IsOn();
        m_mixPhysicalMicrophone = MixMicrophoneToggle().IsOn();
        m_broadcastVolume = BroadcastVolumeSlider().Value() / 100.0;
        m_microphoneVolume = MicrophoneVolumeSlider().Value() / 100.0;
        m_soundboardVolume = SoundboardVolumeSlider().Value() / 100.0;
        const auto broadcastDevice = findRenderDevice(m_broadcastOutputId);
        for (auto& active : m_players) {
            if (active.player) {
                const bool primaryIsBroadcast = !m_monitorLocally && broadcastDevice;
                active.player.Volume(m_volume * m_soundboardVolume * (primaryIsBroadcast ? m_broadcastVolume : 1.0));
            }
            if (active.broadcastPlayer) active.broadcastPlayer.Volume(m_volume * m_soundboardVolume * m_broadcastVolume);
        }
        saveSettings();
        configureMicrophoneMixAsync();
    }

    fire_and_forget MainWindow::configureMicrophoneMixAsync()
    {
        auto lifetime = get_strong();
        try {
            if (m_microphoneGraph) {
                m_microphoneGraph.Stop();
                m_microphoneGraph.Close();
                m_microphoneGraph = nullptr;
                m_microphoneInputNode = nullptr;
                m_microphoneOutputNode = nullptr;
            }
            if (m_broadcastOutputId.empty()) {
                AudioRoutingInfo().Severity(InfoBarSeverity::Informational);
                AudioRoutingInfo().Title(L"Virtual microphone: Not installed");
                AudioRoutingInfo().Message(L"Select an existing virtual cable as Broadcast output. Cuelet does not currently install a capture endpoint.");
                co_return;
            }
            if (!m_mixPhysicalMicrophone) {
                const auto selectedRender = findRenderDevice(m_broadcastOutputId);
                const bool compatibleVirtualEndpoint = selectedRender && cuelet::windows::looksLikeVirtualAudioEndpoint(selectedRender.Name().c_str());
                AudioRoutingInfo().Severity(InfoBarSeverity::Success);
                AudioRoutingInfo().Title(compatibleVirtualEndpoint ? L"Existing virtual cable: Connected" : L"Broadcast output: Connected");
                AudioRoutingInfo().Message(compatibleVirtualEndpoint
                    ? L"Soundboard audio will be sent to this installed cable. Select its paired recording endpoint in the target app; this is not a Cuelet-installed microphone."
                    : L"Soundboard audio will also be sent to the selected render endpoint. Cuelet has not installed a virtual microphone.");
                co_return;
            }
            const auto renderDevice = findRenderDevice(m_broadcastOutputId);
            if (!renderDevice) throw hresult_error(E_INVALIDARG, L"The selected broadcast output is no longer available.");
            AudioGraphSettings settings(Windows::Media::Render::AudioRenderCategory::Communications);
            settings.PrimaryRenderDevice(renderDevice);
            settings.QuantumSizeSelectionMode(QuantumSizeSelectionMode::LowestLatency);
            const auto graphResult = co_await AudioGraph::CreateAsync(settings);
            if (graphResult.Status() != AudioGraphCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not create the broadcast audio graph.");
            m_microphoneGraph = graphResult.Graph();
            m_microphoneGraph.UnrecoverableErrorOccurred([weak = get_weak()](AudioGraph const&, AudioGraphUnrecoverableErrorOccurredEventArgs const&) {
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak] {
                    if (auto current = weak.get()) {
                        current->AudioRoutingInfo().Severity(InfoBarSeverity::Error);
                        current->AudioRoutingInfo().Title(L"Audio routing: Error");
                        current->AudioRoutingInfo().Message(L"The selected audio device disconnected or the broadcast graph stopped unexpectedly.");
                    }
                });
            });
            const auto outputResult = co_await m_microphoneGraph.CreateDeviceOutputNodeAsync();
            if (outputResult.Status() != AudioDeviceNodeCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not open the broadcast output.");
            m_microphoneOutputNode = outputResult.DeviceOutputNode();

            Windows::Media::Audio::CreateAudioDeviceInputNodeResult inputResult{nullptr};
            if (m_microphoneInputId.empty()) {
                inputResult = co_await m_microphoneGraph.CreateDeviceInputNodeAsync(MediaCategory::Communications);
            } else {
                const auto found = std::find_if(m_captureDevices.begin(), m_captureDevices.end(), [&](auto const& device) {
                    return cuelet::windows::hstringToUtf8(device.Id()) == m_microphoneInputId;
                });
                if (found == m_captureDevices.end()) throw hresult_error(E_INVALIDARG, L"The selected microphone is no longer available.");
                inputResult = co_await m_microphoneGraph.CreateDeviceInputNodeAsync(MediaCategory::Communications, nullptr, *found);
            }
            if (inputResult.Status() != AudioDeviceNodeCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not open the microphone input.");
            m_microphoneInputNode = inputResult.DeviceInputNode();
            m_microphoneInputNode.OutgoingGain(m_microphoneVolume);
            m_microphoneInputNode.AddOutgoingConnection(m_microphoneOutputNode);
            m_microphoneGraph.Start();
            AudioRoutingInfo().Severity(InfoBarSeverity::Success);
            AudioRoutingInfo().Title(L"Broadcast output: Connected");
            AudioRoutingInfo().Message(L"The physical microphone and soundboard are routed to the selected render endpoint. Cuelet has not installed a virtual microphone.");
        } catch (hresult_error const& error) {
            AudioRoutingInfo().Severity(InfoBarSeverity::Error);
            AudioRoutingInfo().Title(L"Audio routing: Error");
            AudioRoutingInfo().Message(error.message());
        }
    }

    fire_and_forget MainWindow::playClipAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip) co_return;
        if (clip->missing || !std::filesystem::exists(cuelet::windows::pathFromUtf8(clip->absolutePath))) {
            showStatus(L"This sound file is missing. Restore it and rescan the library.", InfoBarSeverity::Warning);
            co_return;
        }
        try {
            if (!m_allowMultiple) stopAll();
            auto file = co_await StorageFile::GetFileFromPathAsync(cuelet::windows::utf8ToHstring(clip->absolutePath));
            const auto token = m_nextPlaybackToken++;
            MediaPlayer player;
            player.CommandManager().IsEnabled(false);
            const auto playbackDevice = findRenderDevice(m_playbackOutputId);
            const auto broadcastDevice = findRenderDevice(m_broadcastOutputId);
            const bool primaryIsBroadcast = !m_monitorLocally && broadcastDevice;
            if (primaryIsBroadcast) player.AudioDevice(broadcastDevice);
            else if (playbackDevice) player.AudioDevice(playbackDevice);
            player.Volume(m_volume * m_soundboardVolume * (primaryIsBroadcast ? m_broadcastVolume : 1.0));
            player.Source(MediaSource::CreateFromStorageFile(file));
            MediaPlayer broadcastPlayer{nullptr};
            if (m_monitorLocally && broadcastDevice) {
                broadcastPlayer = MediaPlayer();
                broadcastPlayer.CommandManager().IsEnabled(false);
                broadcastPlayer.AudioDevice(broadcastDevice);
                broadcastPlayer.Volume(m_volume * m_soundboardVolume * m_broadcastVolume);
                broadcastPlayer.Source(MediaSource::CreateFromStorageFile(file));
            }
            auto weak = get_weak();
            player.MediaEnded([weak, token](MediaPlayer const&, IInspectable const&) {
                if (auto self = weak.get()) {
                    self->DispatcherQueue().TryEnqueue([weak, token] {
                        if (auto current = weak.get()) {
                            current->stopPlayer(token);
                        }
                    });
                }
            });
            player.MediaFailed([weak, token](MediaPlayer const&, MediaPlayerFailedEventArgs const& args) {
                const auto message = args.ErrorMessage();
                if (auto self = weak.get()) {
                    self->DispatcherQueue().TryEnqueue([weak, token, message] {
                        if (auto current = weak.get()) {
                            current->showStatus(L"Playback failed: " + std::wstring(message), InfoBarSeverity::Error);
                            current->stopPlayer(token);
                        }
                    });
                }
            });
            player.PlaybackSession().PositionChanged([weak](MediaPlaybackSession const&, IInspectable const&) {
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak] { if (auto current = weak.get()) current->updatePlaybackBar(); });
            });
            player.PlaybackSession().NaturalDurationChanged([weak, clipId](MediaPlaybackSession const& session, IInspectable const&) {
                const auto seconds = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak, clipId, seconds] {
                    if (auto current = weak.get()) {
                        if (auto currentClip = current->findClip(clipId)) currentClip->durationSeconds = seconds;
                        current->refreshSounds(true);
                    }
                });
            });
            player.PlaybackSession().PlaybackStateChanged([weak](MediaPlaybackSession const&, IInspectable const&) {
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak] { if (auto current = weak.get()) current->updatePlaybackBar(); });
            });
            m_players.push_back({token, clipId, player, broadcastPlayer});
            clip = findClip(clipId);
            clip->lastPlayedAt = std::time(nullptr);
            saveMetadata();
            if (broadcastPlayer) broadcastPlayer.Play();
            player.Play();
            updatePlaybackBar();
            refreshSounds(true);
        } catch (hresult_error const& error) {
            showStatus(L"Could not play this sound: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::editClipAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip) co_return;

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Edit sound"));
        dialog.PrimaryButtonText(L"Save");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        StackPanel form;
        form.Spacing(12);
        TextBox name;
        name.Header(box_value(L"Display name"));
        name.Text(cuelet::windows::utf8ToHstring(clip->displayName));
        form.Children().Append(name);
        ComboBox category;
        category.Header(box_value(L"Category"));
        category.HorizontalAlignment(HorizontalAlignment::Stretch);
        int selected = 0;
        for (std::size_t index = 0; index < m_categories.size(); ++index) {
            ComboBoxItem item;
            item.Content(box_value(cuelet::windows::utf8ToHstring(m_categories[index].name)));
            item.Tag(box_value(cuelet::windows::utf8ToHstring(m_categories[index].id)));
            category.Items().Append(item);
            if (m_categories[index].id == clip->categoryId) selected = static_cast<int>(index);
        }
        category.SelectedIndex(selected);
        form.Children().Append(category);
        TextBox notes;
        notes.Header(box_value(L"Notes"));
        notes.Text(cuelet::windows::utf8ToHstring(clip->notes));
        notes.AcceptsReturn(true);
        notes.TextWrapping(TextWrapping::Wrap);
        form.Children().Append(notes);
        TextBox aliases;
        aliases.Header(box_value(L"Search aliases (comma separated)"));
        aliases.Text(joinAliases(clip->aliases));
        form.Children().Append(aliases);
        CheckBox favorite;
        favorite.Content(box_value(L"Favorite"));
        favorite.IsChecked(clip->favorite);
        form.Children().Append(favorite);
        dialog.Content(form);

        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        clip = findClip(clipId);
        if (!clip) co_return;
        auto trimmedName = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text()));
        clip->displayName = trimmedName.empty() ? cuelet::displayNameFromFilename(clip->filename) : trimmedName;
        if (auto item = category.SelectedItem().try_as<ComboBoxItem>()) clip->categoryId = cuelet::windows::hstringToUtf8(unbox_value<hstring>(item.Tag()));
        clip->notes = cuelet::windows::hstringToUtf8(notes.Text());
        clip->aliases = splitAliases(aliases.Text().c_str());
        clip->favorite = favorite.IsChecked().GetBoolean();
        saveMetadata();
        refreshSounds(true);
    }

    fire_and_forget MainWindow::changeShortcutAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip) co_return;
        const auto original = clip->shortcut;

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Shortcut for \u201c" + displayLabel(*clip) + L"\u201d"));
        dialog.PrimaryButtonText(L"Save");
        dialog.SecondaryButtonText(L"Clear");
        dialog.CloseButtonText(L"Cancel");
        dialog.IsPrimaryButtonEnabled(false);

        StackPanel form;
        form.Spacing(14);
        TextBlock scopeLabel;
        scopeLabel.Text(L"Scope");
        scopeLabel.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        form.Children().Append(scopeLabel);
        StackPanel scopes;
        scopes.Orientation(Orientation::Horizontal);
        scopes.Spacing(18);
        RadioButton localScope;
        localScope.Content(box_value(L"Local"));
        localScope.GroupName(L"ShortcutScope");
        RadioButton globalScope;
        globalScope.Content(box_value(L"Global"));
        globalScope.GroupName(L"ShortcutScope");
        if (original && original->global) globalScope.IsChecked(true); else localScope.IsChecked(true);
        scopes.Children().Append(localScope);
        scopes.Children().Append(globalScope);
        form.Children().Append(scopes);

        TextBlock recorderLabel;
        recorderLabel.Text(L"Shortcut recorder");
        recorderLabel.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        form.Children().Append(recorderLabel);
        Button recorder;
        recorder.HorizontalAlignment(HorizontalAlignment::Stretch);
        recorder.HorizontalContentAlignment(HorizontalAlignment::Center);
        recorder.MinHeight(72);
        recorder.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));
        TextBlock recordedText;
        recordedText.FontSize(18);
        recordedText.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        recordedText.Text(original ? cuelet::windows::formatShortcut(*original) : L"Press a key combination");
        recorder.Content(recordedText);
        AutomationProperties::SetName(recorder, L"Shortcut recorder. Press the desired key combination.");
        form.Children().Append(recorder);

        TextBlock current;
        current.Text(L"Current shortcut: " + (original ? cuelet::windows::formatShortcut(*original) : std::wstring{L"None"}));
        current.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        form.Children().Append(current);
        InfoBar status;
        status.IsOpen(true);
        status.IsClosable(false);
        status.Severity(InfoBarSeverity::Informational);
        status.Title(L"Recording");
        status.Message(L"Press modifiers and a final key. Backspace or Delete clears the pending recording.");
        form.Children().Append(status);
        CheckBox replace;
        replace.Content(box_value(L"Replace existing assignment"));
        replace.Visibility(Visibility::Collapsed);
        form.Children().Append(replace);

        std::optional<cuelet::Shortcut> pending = original;
        const auto validate = [&]() {
            replace.Visibility(Visibility::Collapsed);
            if (!pending) {
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Informational);
                status.Title(L"Recording");
                status.Message(L"Press a key combination, or choose Clear to remove the saved assignment.");
                return;
            }
            pending->global = globalScope.IsChecked().GetBoolean();
            *pending = cuelet::windows::normalizeShortcut(*pending);
            pending->label = cuelet::windows::wideToUtf8(cuelet::windows::formatShortcut(*pending));
            recordedText.Text(cuelet::windows::formatShortcut(*pending));
            const auto check = checkShortcut(clipId, *pending);
            const auto label = cuelet::windows::formatShortcut(*pending);
            switch (check.availability) {
            case cuelet::windows::ShortcutAvailability::Available:
                dialog.IsPrimaryButtonEnabled(true);
                status.Severity(InfoBarSeverity::Success);
                status.Title(L"Available");
                status.Message(label + L" is available.");
                break;
            case cuelet::windows::ShortcutAvailability::CueletConflict: {
                const auto conflict = cuelet::windows::findShortcutConflict(m_clips, *pending, clipId);
                replace.Visibility(Visibility::Visible);
                const bool approved = replace.IsChecked().GetBoolean();
                dialog.IsPrimaryButtonEnabled(approved);
                status.Severity(InfoBarSeverity::Warning);
                status.Title(L"Already assigned");
                const auto name = conflict ? cuelet::windows::utf8ToWide(conflict->soundName) : std::wstring{L"another sound"};
                status.Message(label + L" is already assigned to \u2018" + name +
                               (approved ? L"\u2019. Saving will replace it." : L"\u2019. Choose Replace existing assignment to continue."));
                break;
            }
            case cuelet::windows::ShortcutAvailability::ReservedBySystem:
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Error);
                status.Title(L"Reserved by Windows");
                status.Message(L"This combination is reserved by Windows and cannot be used.");
                break;
            case cuelet::windows::ShortcutAvailability::RegisteredByAnotherApplication:
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Error);
                status.Title(L"Used by another application");
                status.Message(label + L" is already being used by Windows or another application.");
                break;
            case cuelet::windows::ShortcutAvailability::Unsupported:
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Error);
                status.Title(L"Unsupported shortcut");
                status.Message(L"Global typing keys require two modifiers; F13\u2013F24 may be used alone.");
                break;
            case cuelet::windows::ShortcutAvailability::RegistrationError:
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Error);
                status.Title(L"Registration failed");
                status.Message(L"Windows could not test this shortcut (error " + std::to_wstring(check.errorCode) + L").");
                break;
            }
        };

        recorder.Click([&](IInspectable const&, RoutedEventArgs const&) { recorder.Focus(FocusState::Programmatic); });
        recorder.KeyDown([&](IInspectable const&, KeyRoutedEventArgs const& args) {
            args.Handled(true);
            if (args.KeyStatus().RepeatCount > 1) return;
            const auto key = static_cast<unsigned int>(args.Key());
            if (key == VK_ESCAPE) {
                pending = original;
                replace.IsChecked(false);
                recordedText.Text(original ? cuelet::windows::formatShortcut(*original) : L"Press a key combination");
                validate();
                return;
            }
            if (key == VK_BACK || key == VK_DELETE) {
                pending.reset();
                replace.IsChecked(false);
                recordedText.Text(L"Press a key combination");
                validate();
                return;
            }
            unsigned int modifiers = 0;
            if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierCtrl;
            if ((::GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierAlt;
            if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierShift;
            if ((::GetKeyState(VK_LWIN) & 0x8000) != 0 || (::GetKeyState(VK_RWIN) & 0x8000) != 0) {
                modifiers |= cuelet::windows::shortcutModifierWin;
            }
            if (cuelet::windows::isModifierKey(key)) {
                cuelet::Shortcut held;
                held.modifiers = modifiers;
                const auto label = cuelet::windows::formatShortcut(held);
                recordedText.Text(label.empty() ? L"Press a key combination" : label + L"+\u2026");
                dialog.IsPrimaryButtonEnabled(false);
                status.Severity(InfoBarSeverity::Informational);
                status.Title(L"Recording");
                status.Message(L"Now press the final key.");
                return;
            }
            cuelet::Shortcut recorded;
            recorded.keyval = key;
            recorded.modifiers = modifiers;
            recorded.global = globalScope.IsChecked().GetBoolean();
            recorded = cuelet::windows::normalizeShortcut(recorded);
            recorded.label = cuelet::windows::wideToUtf8(cuelet::windows::formatShortcut(recorded));
            pending = recorded;
            replace.IsChecked(false);
            validate();
        });
        localScope.Checked([&](IInspectable const&, RoutedEventArgs const&) { if (pending) validate(); });
        globalScope.Checked([&](IInspectable const&, RoutedEventArgs const&) { if (pending) validate(); });
        replace.Checked([&](IInspectable const&, RoutedEventArgs const&) { validate(); });
        replace.Unchecked([&](IInspectable const&, RoutedEventArgs const&) { validate(); });
        dialog.Opened([&](ContentDialog const&, ContentDialogOpenedEventArgs const&) {
            recorder.Focus(FocusState::Programmatic);
        });
        dialog.Content(form);
        validate();

        const auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Secondary) {
            std::wstring reason;
            if (!assignShortcutTransactional(clipId, std::nullopt, false, &reason)) showStatus(reason, InfoBarSeverity::Error);
            co_return;
        }
        if (result != ContentDialogResult::Primary) co_return;
        std::wstring reason;
        if (!assignShortcutTransactional(clipId, pending, replace.IsChecked().GetBoolean(), &reason)) {
            showStatus(reason, InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::renameClipAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip || clip->missing) co_return;
        const std::filesystem::path oldPath = cuelet::windows::pathFromUtf8(clip->absolutePath);
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Rename Sound"));
        dialog.PrimaryButtonText(L"Rename");
        dialog.CloseButtonText(L"Cancel");
        TextBox name;
        name.Header(box_value(L"File name"));
        name.Text(oldPath.stem().wstring());
        name.SelectAll();
        dialog.Content(name);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        const auto trimmed = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text()));
        if (trimmed.empty() || trimmed.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            showStatus(L"Enter a valid Windows file name.", InfoBarSeverity::Warning);
            co_return;
        }
        const auto newPath = oldPath.parent_path() / (cuelet::windows::utf8ToWide(trimmed) + oldPath.extension().wstring());
        if (std::filesystem::exists(newPath)) {
            showStatus(L"A file with that name already exists.", InfoBarSeverity::Warning);
            co_return;
        }
        stopClip(clipId);
        std::error_code error;
        std::filesystem::rename(oldPath, newPath, error);
        if (error) {
            showStatus(L"Could not rename the sound: " + cuelet::windows::utf8ToWide(error.message()), InfoBarSeverity::Error);
            co_return;
        }
        clip = findClip(clipId);
        if (!clip) co_return;
        const auto oldDefault = cuelet::displayNameFromFilename(clip->filename);
        clip->absolutePath = cuelet::windows::pathToUtf8(newPath);
        clip->relativePath = cuelet::windows::wideToUtf8(newPath.lexically_relative(m_libraryFolder).generic_wstring());
        clip->filename = cuelet::windows::wideToUtf8(newPath.filename().wstring());
        if (clip->displayName.empty() || clip->displayName == oldDefault) clip->displayName = cuelet::displayNameFromFilename(clip->filename);
        clip->id = cuelet::stableIdForPath(clip->relativePath);
        m_selectedClipId = clip->id;
        saveMetadata();
        registerGlobalShortcuts();
        refreshSounds();
    }

    fire_and_forget MainWindow::removeClipAsync(std::string clipId)
    {
        removeClipsAsync({std::move(clipId)});
        co_return;
    }

    fire_and_forget MainWindow::removeClipsAsync(std::vector<std::string> clipIds)
    {
        auto lifetime = get_strong();
        std::erase_if(clipIds, [&](auto const& id) { return findClip(id) == nullptr; });
        if (clipIds.empty()) co_return;
        auto clip = findClip(clipIds.front());
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(clipIds.size() == 1
            ? L"Remove “" + displayLabel(*clip) + L"” from the library?"
            : L"Remove " + std::to_wstring(clipIds.size()) + L" selected sounds from the library?"));
        dialog.PrimaryButtonText(L"Remove");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        dialog.Content(box_value(clipIds.size() == 1 && clip->missing
            ? L"The missing metadata entry will be removed."
            : L"Existing audio files will be permanently deleted from the library folder."));
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        for (auto const& id : clipIds) stopClip(id);
        std::vector<std::string> removed;
        for (auto const& id : clipIds) {
            clip = findClip(id);
            if (!clip) continue;
            if (!clip->missing) {
                std::error_code error;
                if (!std::filesystem::remove(cuelet::windows::pathFromUtf8(clip->absolutePath), error) || error) {
                    showStatus(L"One or more sound files could not be removed.", InfoBarSeverity::Error);
                    continue;
                }
            }
            removed.push_back(id);
        }
        std::erase_if(m_clips, [&](auto const& value) { return std::find(removed.begin(), removed.end(), value.id) != removed.end(); });
        clearSoundSelection();
        saveMetadata();
        registerGlobalShortcuts();
        refreshSounds();
    }

    void MainWindow::assignCategory(std::string const& clipId, std::string const& categoryId)
    {
        if (!cuelet::categoryForId(m_categories, categoryId)) return;
        if (auto clip = findClip(clipId)) {
            clip->categoryId = categoryId;
            saveMetadata();
            refreshSounds(true);
        }
    }

    void MainWindow::showClipInExplorer(std::string const& clipId)
    {
        const auto clip = findClip(clipId);
        if (!clip) {
            showStatus(L"The selected sound is no longer in the library.", InfoBarSeverity::Warning);
            return;
        }
        const auto path = cuelet::windows::pathFromUtf8(clip->absolutePath);
        std::error_code error;
        if (clip->missing || !std::filesystem::is_regular_file(path, error)) {
            showStatus(L"The sound file is missing and cannot be revealed in File Explorer.", InfoBarSeverity::Warning);
            return;
        }

        PIDLIST_ABSOLUTE itemId = nullptr;
        const auto parseResult = ::SHParseDisplayName(path.c_str(), nullptr, &itemId, 0, nullptr);
        HRESULT openResult = FAILED(parseResult) ? parseResult : ::SHOpenFolderAndSelectItems(itemId, 0, nullptr, 0);
        if (itemId) ::CoTaskMemFree(itemId);
        if (SUCCEEDED(openResult)) return;

        const auto parent = path.parent_path();
        if (parent.empty() || reinterpret_cast<INT_PTR>(::ShellExecuteW(
                m_hwnd, L"open", parent.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            showStatus(L"File Explorer could not open the sound’s folder.", InfoBarSeverity::Error);
        } else {
            showStatus(L"The folder opened, but File Explorer could not select the sound.", InfoBarSeverity::Warning);
        }
    }

    fire_and_forget MainWindow::createCategoryAsync(std::optional<std::string> assignClipId)
    {
        if (m_libraryFolder.empty()) {
            showStatus(L"Choose a library before creating categories.", InfoBarSeverity::Informational);
            co_return;
        }
        editCategoryAsync(std::nullopt, std::move(assignClipId));
    }

    fire_and_forget MainWindow::editCategoryAsync(std::optional<std::string> categoryId, std::optional<std::string> assignClipId)
    {
        auto lifetime = get_strong();
        cuelet::Category original{"", "", "#3478F6", "tag", true};
        if (categoryId) {
            const auto found = std::find_if(m_categories.begin(), m_categories.end(), [&](auto const& category) { return category.id == *categoryId; });
            if (found == m_categories.end() || !found->editable) co_return;
            original = *found;
        }

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(categoryId ? L"Edit Category" : L"New Category"));
        dialog.PrimaryButtonText(categoryId ? L"Save" : L"Create");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.MinWidth(440);
        StackPanel form;
        form.Spacing(12);
        TextBox name;
        name.Header(box_value(L"Category name"));
        name.PlaceholderText(L"Category name");
        name.Text(cuelet::windows::utf8ToHstring(original.name));
        form.Children().Append(name);

        Border preview;
        preview.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        preview.BorderThickness(ThicknessHelper::FromUniformLength(1));
        preview.BorderBrush(themeBrush(L"CardStrokeColorDefaultBrush"));
        preview.Background(themeBrush(L"CardBackgroundFillColorDefaultBrush"));
        preview.Padding(ThicknessHelper::FromUniformLength(10));
        StackPanel previewContent;
        previewContent.Orientation(Orientation::Horizontal);
        previewContent.Spacing(9);
        auto previewIcon = makeCategoryIcon(original.iconName, 20);
        previewContent.Children().Append(previewIcon);
        Microsoft::UI::Xaml::Shapes::Ellipse previewDot;
        previewDot.Width(10); previewDot.Height(10);
        previewDot.Fill(cuelet::windows::categoryColorBrush(original.colorHex));
        previewDot.VerticalAlignment(VerticalAlignment::Center);
        previewContent.Children().Append(previewDot);
        TextBlock previewName;
        previewName.Text(original.name.empty() ? L"Category preview" : cuelet::windows::utf8ToHstring(original.name));
        previewName.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        previewName.VerticalAlignment(VerticalAlignment::Center);
        previewContent.Children().Append(previewName);
        preview.Child(previewContent);
        form.Children().Append(preview);

        ComboBox color;
        color.Header(box_value(L"Color"));
        color.HorizontalAlignment(HorizontalAlignment::Stretch);
        int selectedColor = 0;
        const auto& colors = cuelet::availableCategoryColors();
        for (std::size_t index = 0; index < colors.size(); ++index) {
            ComboBoxItem item;
            item.Tag(box_value(cuelet::windows::utf8ToHstring(colors[index].colorHex)));
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(9);
            Microsoft::UI::Xaml::Shapes::Ellipse swatch;
            swatch.Width(16); swatch.Height(16);
            swatch.Fill(cuelet::windows::categoryColorBrush(colors[index].colorHex));
            content.Children().Append(swatch);
            TextBlock label;
            label.Text(cuelet::windows::utf8ToHstring(colors[index].name));
            content.Children().Append(label);
            item.Content(content);
            color.Items().Append(item);
            if (colors[index].colorHex == original.colorHex) selectedColor = static_cast<int>(index);
        }
        color.SelectedIndex(selectedColor);
        form.Children().Append(color);

        ComboBox icon;
        icon.Header(box_value(L"Icon"));
        icon.HorizontalAlignment(HorizontalAlignment::Stretch);
        int selectedIcon = 0;
        const auto canonicalOriginalIcon = cuelet::canonicalCategoryIconId(original.iconName);
        const auto icons = cuelet::windows::availableWindowsCategoryIcons();
        for (std::size_t index = 0; index < icons.size(); ++index) {
            ComboBoxItem item;
            item.Tag(box_value(cuelet::windows::utf8ToHstring(icons[index].id)));
            StackPanel content;
            content.Orientation(Orientation::Horizontal);
            content.Spacing(9);
            IconSourceElement image;
            image.IconSource(cuelet::windows::iconForCategoryId(icons[index].id));
            image.Width(20); image.Height(20);
            content.Children().Append(image);
            TextBlock label;
            label.Text(icons[index].displayName);
            content.Children().Append(label);
            item.Content(content);
            icon.Items().Append(item);
            if (icons[index].id == canonicalOriginalIcon) selectedIcon = static_cast<int>(index);
        }
        icon.SelectedIndex(selectedIcon);
        form.Children().Append(icon);

        auto updatePreview = [=](auto const&, auto const&) {
            previewName.Text(cuelet::trim(cuelet::windows::hstringToUtf8(name.Text())).empty() ? L"Category preview" : name.Text());
            if (auto selected = color.SelectedItem().try_as<ComboBoxItem>()) {
                previewDot.Fill(cuelet::windows::categoryColorBrush(cuelet::windows::hstringToUtf8(unbox_value<hstring>(selected.Tag()))));
            }
            if (auto selected = icon.SelectedItem().try_as<ComboBoxItem>()) {
                previewIcon.IconSource(cuelet::windows::iconForCategoryId(cuelet::windows::hstringToUtf8(unbox_value<hstring>(selected.Tag()))));
            }
        };
        name.TextChanged(updatePreview);
        color.SelectionChanged(updatePreview);
        icon.SelectionChanged(updatePreview);
        dialog.Content(form);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        auto categoryName = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text()));
        if (categoryName.empty()) co_return;
        const auto duplicate = std::any_of(m_categories.begin(), m_categories.end(), [&](auto const& category) {
            return (!categoryId || category.id != *categoryId) && cuelet::normalizeForSearch(category.name) == cuelet::normalizeForSearch(categoryName);
        });
        if (duplicate) {
            showStatus(L"A category with that name already exists.", InfoBarSeverity::Warning);
            co_return;
        }
        auto selectedColorItem = color.SelectedItem().try_as<ComboBoxItem>();
        auto selectedIconItem = icon.SelectedItem().try_as<ComboBoxItem>();
        const auto colorHex = selectedColorItem ? cuelet::windows::hstringToUtf8(unbox_value<hstring>(selectedColorItem.Tag())) : std::string{"#3478F6"};
        const auto iconId = selectedIconItem ? cuelet::canonicalCategoryIconId(cuelet::windows::hstringToUtf8(unbox_value<hstring>(selectedIconItem.Tag()))) : std::string{"tag"};
        std::string savedId;
        if (categoryId) {
            auto found = std::find_if(m_categories.begin(), m_categories.end(), [&](auto const& category) { return category.id == *categoryId; });
            if (found == m_categories.end() || !found->editable) co_return;
            found->name = categoryName;
            found->colorHex = colorHex;
            found->iconName = iconId;
            savedId = found->id;
            if (m_filter.scope == cuelet::LibraryScope::Category && m_filter.categoryId == savedId) PageTitle().Text(cuelet::windows::utf8ToHstring(categoryName));
        } else {
            savedId = cuelet::stableCategoryIdForName(categoryName);
            m_categories.push_back({savedId, categoryName, colorHex, iconId, true});
        }
        if (assignClipId) assignCategory(*assignClipId, savedId);
        saveMetadata();
        rebuildCategories();
        refreshSounds(true);
    }

    fire_and_forget MainWindow::deleteCategoryAsync(std::string categoryId)
    {
        auto lifetime = get_strong();
        auto found = std::find_if(m_categories.begin(), m_categories.end(), [&](auto const& category) { return category.id == categoryId; });
        if (found == m_categories.end() || !found->editable) co_return;
        const auto assigned = std::count_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) { return clip.categoryId == categoryId; });
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Delete “" + cuelet::windows::utf8ToHstring(found->name) + L"”?"));
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        dialog.Content(box_value(assigned == 0
            ? L"The category will be removed."
            : std::to_wstring(assigned) + (assigned == 1 ? L" sound will be moved to Uncategorized." : L" sounds will be moved to Uncategorized.")));
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;
        for (auto& clip : m_clips) if (clip.categoryId == categoryId) clip.categoryId = "uncategorized";
        std::erase_if(m_categories, [&](auto const& category) { return category.id == categoryId; });
        if (m_filter.scope == cuelet::LibraryScope::Category && m_filter.categoryId == categoryId) {
            m_filter.scope = cuelet::LibraryScope::All;
            m_filter.categoryId.clear();
            PageTitle().Text(L"Library");
        }
        saveMetadata();
        rebuildCategories();
        refreshSounds(true);
    }

    void MainWindow::toggleFavorite(std::string const& clipId)
    {
        if (auto clip = findClip(clipId)) {
            clip->favorite = !clip->favorite;
            saveMetadata();
            refreshSounds(true);
        }
    }

    bool MainWindow::isClipPlaying(std::string const& clipId) const
    {
        return std::any_of(m_players.begin(), m_players.end(), [&](auto const& active) {
            return active.clipId == clipId && active.player;
        });
    }

    void MainWindow::stopClip(std::string const& clipId)
    {
        for (auto& active : m_players) {
            if (active.clipId == clipId && active.player) {
                active.player.Pause();
                active.player.Source(nullptr);
                active.player.Close();
            }
            if (active.clipId == clipId && active.broadcastPlayer) {
                active.broadcastPlayer.Pause();
                active.broadcastPlayer.Source(nullptr);
                active.broadcastPlayer.Close();
            }
        }
        std::erase_if(m_players, [&](auto const& active) { return active.clipId == clipId; });
        updatePlaybackBar();
        refreshSounds(true);
    }

    void MainWindow::stopPlayer(std::uint64_t token)
    {
        const auto found = std::find_if(m_players.begin(), m_players.end(), [&](auto const& active) { return active.token == token; });
        if (found == m_players.end()) return;
        if (found->player) {
            found->player.Pause();
            found->player.Source(nullptr);
            found->player.Close();
        }
        if (found->broadcastPlayer) {
            found->broadcastPlayer.Pause();
            found->broadcastPlayer.Source(nullptr);
            found->broadcastPlayer.Close();
        }
        m_players.erase(found);
        updatePlaybackBar();
        refreshSounds(true);
    }

    void MainWindow::stopCurrent()
    {
        if (!m_players.empty()) stopPlayer(m_players.back().token);
    }

    void MainWindow::stopAll()
    {
        for (auto& active : m_players) {
            if (active.player) {
                active.player.Pause();
                active.player.Source(nullptr);
                active.player.Close();
            }
            if (active.broadcastPlayer) {
                active.broadcastPlayer.Pause();
                active.broadcastPlayer.Source(nullptr);
                active.broadcastPlayer.Close();
            }
        }
        m_players.clear();
        updatePlaybackBar();
    }

    void MainWindow::pruneStoppedPlayers()
    {
        std::erase_if(m_players, [](auto const& active) {
            if (!active.player) return true;
            const auto state = active.player.PlaybackSession().PlaybackState();
            return state == MediaPlaybackState::None && !active.player.Source();
        });
    }

    void MainWindow::updatePlaybackBar()
    {
        pruneStoppedPlayers();
        const auto playing = !m_players.empty();
        StopAllButton().IsEnabled(playing);
        StopCurrentButton().IsEnabled(playing);
        NowPlayingBar().Visibility(playing ? Visibility::Visible : Visibility::Collapsed);
        if (!playing) {
            m_playbackTimer.Stop();
            PlaybackProgress().Value(0);
            PlaybackTime().Text(L"");
            NowPlayingTitle().Text(L"");
            NowPlayingCategory().Text(L"");
            return;
        }
        if (!m_playbackTimer.IsEnabled()) m_playbackTimer.Start();
        auto& active = m_players.back();
        if (auto clip = findClip(active.clipId)) {
            NowPlayingTitle().Text(displayLabel(*clip));
            auto category = categoryLabel(*clip);
            if (m_players.size() > 1) category += L" · " + std::to_wstring(m_players.size()) + L" sounds playing";
            if (!m_broadcastOutputId.empty()) category += L" · Broadcast";
            NowPlayingCategory().Text(category);
        }
        const auto session = active.player.PlaybackSession();
        const auto positionSeconds = static_cast<double>(session.Position().count()) / 10000000.0;
        const auto durationSeconds = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
        const auto progress = cuelet::makePlaybackProgress(positionSeconds, durationSeconds);
        PlaybackProgress().Value(progress.fraction);
        const auto elapsed = progress.positionSeconds > 0 ? formatDuration(progress.positionSeconds) : std::wstring{L"0:00"};
        const auto duration = progress.durationSeconds > 0 ? formatDuration(progress.durationSeconds) : std::wstring{L"--:--"};
        PlaybackTime().Text(elapsed + L" / " + duration);
    }

    void MainWindow::handleKeyDown(IInspectable const&, KeyRoutedEventArgs const& args)
    {
        const auto key = args.Key();
        const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (control && key == VirtualKey::F) {
            LibraryPage().Visibility(Visibility::Visible);
            SettingsPage().Visibility(Visibility::Collapsed);
            SearchBox().Focus(FocusState::Keyboard);
            args.Handled(true);
            return;
        }
        if (key == VirtualKey::Escape) {
            clearSoundSelection();
            args.Handled(true);
            return;
        }
        auto focused = FocusManager::GetFocusedElement(RootGrid().XamlRoot());
        if (focused.try_as<TextBox>() || focused.try_as<AutoSuggestBox>()) return;
        if (control && key == VirtualKey::A && LibraryPage().Visibility() == Visibility::Visible) {
            const auto list = m_gridView ? SoundGrid().as<ListViewBase>() : SoundList().as<ListViewBase>();
            list.SelectAll();
            args.Handled(true);
            return;
        }
        if (control && shift && key == VirtualKey::R && selectedClipIds().size() == 1) {
            showClipInExplorer(selectedClipIds().front());
            args.Handled(true);
            return;
        }
        if (key == VirtualKey::Enter && !m_selectedClipId.empty()) {
            playClipAsync(m_selectedClipId);
            args.Handled(true);
            return;
        }
        if (handleSoundShortcut(key)) args.Handled(true);
    }

    bool MainWindow::handleSoundShortcut(VirtualKey key)
    {
        unsigned int modifiers = 0;
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierCtrl;
        if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierShift;
        if ((::GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= cuelet::windows::shortcutModifierAlt;
        if ((::GetKeyState(VK_LWIN) & 0x8000) != 0 || (::GetKeyState(VK_RWIN) & 0x8000) != 0) {
            modifiers |= cuelet::windows::shortcutModifierWin;
        }
        cuelet::Shortcut pressed;
        pressed.keyval = static_cast<unsigned int>(key);
        pressed.modifiers = modifiers;
        pressed.global = false;
        for (auto const& clip : m_clips) {
            if (clip.shortcut && !clip.shortcut->global && cuelet::windows::shortcutEquals(*clip.shortcut, pressed)) {
                playClipAsync(clip.id);
                return true;
            }
        }
        return false;
    }

    cuelet::windows::ShortcutCheckResult MainWindow::checkShortcut(
        std::string const& clipId, cuelet::Shortcut const& value) const
    {
        const auto shortcut = cuelet::windows::normalizeShortcut(value);
        if (cuelet::windows::isWindowsReservedShortcut(shortcut)) {
            return {cuelet::windows::ShortcutAvailability::ReservedBySystem};
        }
        if (!cuelet::windows::isShortcutSupported(shortcut)) {
            return {cuelet::windows::ShortcutAvailability::Unsupported};
        }
        if (const auto conflict = cuelet::windows::findShortcutConflict(m_clips, shortcut, clipId)) {
            return {cuelet::windows::ShortcutAvailability::CueletConflict, conflict->clipId};
        }
        if (!shortcut.global) return {};

        const auto current = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) { return clip.id == clipId; });
        if (current != m_clips.end() && current->shortcut && current->shortcut->global &&
            cuelet::windows::shortcutEquals(*current->shortcut, shortcut) && m_globalShortcuts.isRegistered(clipId)) {
            return {};
        }
        return m_globalShortcuts.probe(shortcut);
    }

    bool MainWindow::assignShortcutTransactional(
        std::string const& clipId,
        std::optional<cuelet::Shortcut> shortcut,
        bool replaceExisting,
        std::wstring* failureReason)
    {
        const auto fail = [&](std::wstring reason) {
            if (failureReason) *failureReason = std::move(reason);
            return false;
        };
        const auto target = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) { return clip.id == clipId; });
        if (target == m_clips.end()) return fail(L"The sound no longer exists.");

        std::optional<cuelet::windows::ShortcutConflict> conflict;
        if (shortcut) {
            *shortcut = cuelet::windows::normalizeShortcut(*shortcut);
            shortcut->label = cuelet::windows::wideToUtf8(cuelet::windows::formatShortcut(*shortcut));
            const auto check = checkShortcut(clipId, *shortcut);
            if (check.availability == cuelet::windows::ShortcutAvailability::CueletConflict) {
                conflict = cuelet::windows::findShortcutConflict(m_clips, *shortcut, clipId);
                if (!replaceExisting || !conflict) return fail(L"Choose Replace existing assignment before overwriting another sound.");
            } else if (!check.available()) {
                switch (check.availability) {
                case cuelet::windows::ShortcutAvailability::ReservedBySystem:
                    return fail(L"This combination is reserved by Windows and cannot be used.");
                case cuelet::windows::ShortcutAvailability::RegisteredByAnotherApplication:
                    return fail(cuelet::windows::formatShortcut(*shortcut) + L" is already being used by Windows or another application.");
                case cuelet::windows::ShortcutAvailability::Unsupported:
                    return fail(L"This shortcut is unsupported or unsafe as a global shortcut.");
                default:
                    return fail(L"Windows could not register this shortcut (error " + std::to_wstring(check.errorCode) + L").");
                }
            }
        }

        const auto oldClips = m_clips;
        const auto oldMetadata = m_metadata;
        auto proposed = m_clips;
        if (conflict) {
            const auto replaced = std::find_if(proposed.begin(), proposed.end(), [&](auto const& clip) {
                return clip.id == conflict->clipId;
            });
            if (replaced != proposed.end()) replaced->shortcut.reset();
        }
        const auto proposedTarget = std::find_if(proposed.begin(), proposed.end(), [&](auto const& clip) { return clip.id == clipId; });
        proposedTarget->shortcut = shortcut;

        // The registry reuses an existing OS registration when a Cuelet conflict
        // is transferred, and otherwise registers all new combinations before it
        // unregisters any old combination.
        if (!m_globalShortcuts.tryUpdate(proposed)) {
            auto reason = m_globalShortcuts.lastFailureReason();
            return fail(reason.empty() ? L"Registration failed. The previous shortcut is still active." : reason);
        }

        m_clips = std::move(proposed);
        if (!saveMetadata(false)) {
            m_clips = oldClips;
            m_metadata = oldMetadata;
            const bool registrationRestored = m_globalShortcuts.tryUpdate(m_clips);
            refreshSounds(true);
            return fail(registrationRestored
                ? L"Could not save library metadata. The previous shortcut was restored."
                : L"Could not save library metadata, and Windows could not restore a previous global registration.");
        }

        refreshSounds(true);
        showStatus(shortcut ? cuelet::windows::formatShortcut(*shortcut) + L" saved."
                            : L"Shortcut cleared.", InfoBarSeverity::Success);
        return true;
    }

    void MainWindow::refreshShortcutSettings()
    {
        auto list = ShortcutSettingsList();
        list.Children().Clear();
        const bool hasAssignments = std::any_of(m_clips.begin(), m_clips.end(), [](auto const& clip) {
            return clip.shortcut && !clip.shortcut->empty();
        });
        ClearAllShortcutsButton().IsEnabled(hasAssignments);
        ReRegisterShortcutsButton().IsEnabled(hasAssignments);
        if (!hasAssignments) {
            TextBlock empty;
            empty.Text(L"No sound shortcuts are assigned.");
            empty.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
            list.Children().Append(empty);
            return;
        }

        for (auto const& clip : m_clips) {
            if (!clip.shortcut || clip.shortcut->empty()) continue;
            Border row;
            row.Background(themeBrush(L"CardBackgroundFillColorSecondaryBrush"));
            row.BorderBrush(themeBrush(L"CardStrokeColorDefaultBrush"));
            row.BorderThickness(ThicknessHelper::FromUniformLength(1));
            row.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            row.Padding(ThicknessHelper::FromUniformLength(10));
            Grid layout;
            layout.ColumnSpacing(10);
            layout.ColumnDefinitions().Append(ColumnDefinition());
            layout.ColumnDefinitions().Append(ColumnDefinition());
            layout.ColumnDefinitions().Append(ColumnDefinition());
            layout.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            layout.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
            layout.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());

            StackPanel details;
            details.Spacing(2);
            TextBlock name;
            name.Text(displayLabel(clip));
            name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            details.Children().Append(name);
            TextBlock binding;
            binding.Text(cuelet::windows::formatShortcut(*clip.shortcut) +
                         (clip.shortcut->global ? L" · Global" : L" · Local"));
            binding.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
            details.Children().Append(binding);
            TextBlock registration;
            const auto internalConflict = cuelet::windows::findShortcutConflict(m_clips, *clip.shortcut, clip.id);
            if (cuelet::windows::isWindowsReservedShortcut(*clip.shortcut)) {
                registration.Text(L"Reserved by Windows");
                registration.Foreground(themeBrush(L"SystemFillColorCriticalBrush"));
            } else if (!cuelet::windows::isShortcutSupported(*clip.shortcut)) {
                registration.Text(L"Unsupported shortcut");
                registration.Foreground(themeBrush(L"SystemFillColorCriticalBrush"));
            } else if (internalConflict) {
                registration.Text(L"Conflict with \u2018" + cuelet::windows::utf8ToWide(internalConflict->soundName) + L"\u2019");
                registration.Foreground(themeBrush(L"SystemFillColorCriticalBrush"));
            } else if (!clip.shortcut->global) {
                registration.Text(L"Local only");
                registration.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
            } else {
                const auto shortcutStatus = m_globalShortcuts.statusFor(clip.id);
                registration.Text(shortcutStatus.registered ? L"Registered"
                    : shortcutStatus.reason.empty() ? L"Registration failed" : shortcutStatus.reason);
                registration.Foreground(shortcutStatus.registered
                    ? themeBrush(L"SystemFillColorSuccessBrush") : themeBrush(L"SystemFillColorCriticalBrush"));
            }
            registration.TextWrapping(TextWrapping::Wrap);
            details.Children().Append(registration);
            layout.Children().Append(details);

            Button edit;
            edit.Content(box_value(L"Edit"));
            edit.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->changeShortcutAsync(id);
            });
            Grid::SetColumn(edit, 1);
            layout.Children().Append(edit);
            Button clear;
            clear.Content(box_value(L"Clear"));
            clear.Click([weak = get_weak(), id = clip.id](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) {
                    std::wstring reason;
                    if (!self->assignShortcutTransactional(id, std::nullopt, false, &reason)) {
                        self->showStatus(reason, InfoBarSeverity::Error);
                    }
                }
            });
            Grid::SetColumn(clear, 2);
            layout.Children().Append(clear);
            row.Child(layout);
            list.Children().Append(row);
        }
    }

    fire_and_forget MainWindow::clearAllShortcutsAsync()
    {
        auto lifetime = get_strong();
        if (!std::any_of(m_clips.begin(), m_clips.end(), [](auto const& clip) { return clip.shortcut.has_value(); })) co_return;
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Clear all shortcuts?"));
        dialog.Content(box_value(L"This removes every local and global shortcut in the current library."));
        dialog.PrimaryButtonText(L"Clear all");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary) co_return;

        const auto oldClips = m_clips;
        const auto oldMetadata = m_metadata;
        auto proposed = m_clips;
        for (auto& clip : proposed) clip.shortcut.reset();
        if (!m_globalShortcuts.tryUpdate(proposed)) {
            showStatus(L"Windows could not clear the active shortcut registrations.", InfoBarSeverity::Error);
            co_return;
        }
        m_clips = std::move(proposed);
        if (!saveMetadata(false)) {
            m_clips = oldClips;
            m_metadata = oldMetadata;
            m_globalShortcuts.tryUpdate(m_clips);
            refreshSounds(true);
            showStatus(L"Could not save library metadata. The previous shortcuts were restored.", InfoBarSeverity::Error);
            co_return;
        }
        refreshSounds(true);
        showStatus(L"All shortcuts cleared.", InfoBarSeverity::Success);
    }

    void MainWindow::Navigation_ItemInvoked(NavigationView const&, NavigationViewItemInvokedEventArgs const& args)
    {
        if (args.IsSettingsInvoked()) {
            LibraryPage().Visibility(Visibility::Collapsed);
            SettingsPage().Visibility(Visibility::Visible);
            refreshShortcutSettings();
            return;
        }
        LibraryPage().Visibility(Visibility::Visible);
        SettingsPage().Visibility(Visibility::Collapsed);
        const auto item = args.InvokedItemContainer().try_as<NavigationViewItem>();
        if (!item) return;
        const auto tag = unbox_value_or<hstring>(item.Tag(), L"library");
        if (tag == L"new-category") { createCategoryAsync(); return; }
        if (tag == L"favorites") { PageTitle().Text(L"Favorites"); setScope(cuelet::LibraryScope::Favorites); }
        else if (tag == L"recent") { PageTitle().Text(L"Recent"); setScope(cuelet::LibraryScope::Recent); }
        else if (tag == L"all-categories") { PageTitle().Text(L"All Categories"); setScope(cuelet::LibraryScope::AllCategories); }
        else if (tag.starts_with(L"category:")) {
            const auto id = cuelet::windows::wideToUtf8(std::wstring(tag.c_str()).substr(9));
            auto category = cuelet::categoryForId(m_categories, id);
            PageTitle().Text(category ? cuelet::windows::utf8ToHstring(category->name) : hstring(L"Category"));
            setScope(cuelet::LibraryScope::Category, id);
        } else { PageTitle().Text(L"Library"); setScope(cuelet::LibraryScope::All); }
    }

    void MainWindow::ChooseLibrary_Click(IInspectable const&, RoutedEventArgs const&) { chooseLibraryAsync(); }
    void MainWindow::Import_Click(IInspectable const&, RoutedEventArgs const&) { importAsync(); }
    void MainWindow::Rescan_Click(IInspectable const&, RoutedEventArgs const&) { scanLibrary(true); }
    void MainWindow::StopAll_Click(IInspectable const&, RoutedEventArgs const&) { stopAll(); refreshSounds(true); }
    void MainWindow::StopCurrent_Click(IInspectable const&, RoutedEventArgs const&) { stopCurrent(); }
    void MainWindow::SearchBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&) { refreshSounds(); }
    void MainWindow::SortCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) { if (!m_loadingSettings) { saveSettings(); refreshSounds(); } }
    void MainWindow::GridView_Click(IInspectable const&, RoutedEventArgs const&) { m_gridView = true; GridViewButton().IsChecked(true); ListViewButton().IsChecked(false); saveSettings(); refreshSounds(); }
    void MainWindow::ListView_Click(IInspectable const&, RoutedEventArgs const&) { m_gridView = false; GridViewButton().IsChecked(false); ListViewButton().IsChecked(true); saveSettings(); refreshSounds(); }

    void MainWindow::SoundCollection_PointerPressed(IInspectable const& sender, PointerRoutedEventArgs const& args)
    {
        const auto list = sender.try_as<ListViewBase>();
        if (!list) return;
        if (eventOriginatesInInteractiveControl(args.OriginalSource(), list)) {
            const auto selection = selectedClipIds();
            DispatcherQueue().TryEnqueue([weak = get_weak(), selection] {
                if (auto self = weak.get()) {
                    self->clearSoundSelection();
                    self->restoreSoundSelection(selection);
                }
            });
            return;
        }
        if (eventOriginatesInItem(args.OriginalSource(), list)) return;
        clearSoundSelection();
    }

    void MainWindow::prepareItemContextMenu(ListViewBase const& owner, SelectorItem const& item)
    {
        if (!item.IsSelected()) {
            owner.SelectedItems().Clear();
            item.IsSelected(true);
        }
        item.ContextFlyout(makeSoundMenu(itemClipId(item)));
    }

    void MainWindow::SoundSelection_Changed(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (auto list = sender.try_as<ListViewBase>()) {
            if (auto selected = list.SelectedItem()) m_selectedClipId = itemClipId(selected);
            else m_selectedClipId.clear();
        }
        updateSoundVisualStates();
    }

    void MainWindow::VolumeSlider_ValueChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        m_volume = args.NewValue() / 100.0;
        const auto broadcastDevice = findRenderDevice(m_broadcastOutputId);
        for (auto& active : m_players) {
            if (active.player) {
                const bool primaryIsBroadcast = !m_monitorLocally && broadcastDevice;
                active.player.Volume(m_volume * m_soundboardVolume * (primaryIsBroadcast ? m_broadcastVolume : 1.0));
            }
            if (active.broadcastPlayer) active.broadcastPlayer.Volume(m_volume * m_soundboardVolume * m_broadcastVolume);
        }
        if (!m_loadingSettings) saveSettings();
    }

    void MainWindow::Setting_Toggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loadingSettings) return;
        const auto recursiveChanged = m_recursiveScan != RecursiveScanToggle().IsOn();
        m_allowMultiple = MultiplePlaybackToggle().IsOn();
        m_recursiveScan = RecursiveScanToggle().IsOn();
        m_showExtensions = ShowExtensionsToggle().IsOn();
        m_keepRunningInBackground = KeepBackgroundToggle().IsOn();
        m_minimizeToTray = MinimizeToTrayToggle().IsOn();
        if (m_keepRunningInBackground) m_trayIcon.add(m_globalShortcuts.failureCount() == 0);
        else m_trayIcon.remove();
        saveSettings();
        if (recursiveChanged && !m_libraryFolder.empty()) scanLibrary(); else refreshSounds();
    }

    void MainWindow::OpenLibraryFromActivation(std::wstring const& path)
    {
        std::error_code error;
        if (!path.empty() && std::filesystem::is_directory(path, error)) {
            m_libraryFolder = std::filesystem::path(path);
            saveSettings();
            scanLibrary();
            ::ShowWindow(m_hwnd, SW_RESTORE);
            Activate();
        }
    }
}
