#include "pch.h"
#include "CueletResources.h"
#include "MainWindow.xaml.h"
#include "WindowsText.h"
#include "WindowsAudioRoutingModel.h"
#include "WindowsDiagnostics.h"
#include "WindowsInformationModel.h"
#include "VirtualAudioInstallerClient.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <initguid.h>
#include <devpkey.h>
#include <fstream>
#include <limits>
#include <sstream>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Data::Json;
using namespace Windows::Media::Audio;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Core;
using namespace Windows::Media::Devices;
using namespace Windows::Media::Editing;
using namespace Windows::Media::Playback;
using namespace Windows::Storage;
using namespace Windows::Storage::FileProperties;
using namespace Windows::Storage::Pickers;
using namespace Windows::System;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Microsoft::UI::Xaml::Automation;

namespace winrt::Cuelet::WinUI::implementation
{
    namespace
    {
        constexpr int32_t categoryNameMaxLength = 64;
        constexpr int32_t soundFileNameMaxLength = 120;
        constexpr double categoryDialogWidth = 440;
        constexpr double categoryDialogContentWidth = 392;
        constexpr std::size_t menuLabelMaxCharacters = 32;
        constexpr wchar_t cueletSoundDataFormat[] = L"application/x-cuelet-sound+json";

        class AsyncOperationScope
        {
        public:
            explicit AsyncOperationScope(std::atomic_uint32_t& count) noexcept
                : m_count(count)
            {
                m_count.fetch_add(1, std::memory_order_relaxed);
            }

            ~AsyncOperationScope()
            {
                m_count.fetch_sub(1, std::memory_order_relaxed);
            }

            AsyncOperationScope(AsyncOperationScope const&) = delete;
            AsyncOperationScope& operator=(AsyncOperationScope const&) = delete;

        private:
            std::atomic_uint32_t& m_count;
        };

        class BoolFlagScope
        {
        public:
            explicit BoolFlagScope(bool& flag) noexcept : m_flag(flag)
            {
                m_flag = true;
            }

            ~BoolFlagScope()
            {
                m_flag = false;
            }

            BoolFlagScope(BoolFlagScope const&) = delete;
            BoolFlagScope& operator=(BoolFlagScope const&) = delete;

        private:
            bool& m_flag;
        };

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

        std::wstring ellipsizeForMenu(std::wstring const& value)
        {
            if (value.size() <= menuLabelMaxCharacters) return value;
            auto length = menuLabelMaxCharacters - 1;
            if (length > 0 && value[length - 1] >= 0xD800 && value[length - 1] <= 0xDBFF) --length;
            return value.substr(0, length) + L"\u2026";
        }

        void setConstrainedMenuText(MenuFlyoutItem const& item, std::wstring const& value)
        {
            item.Text(ellipsizeForMenu(value));
            item.MaxWidth(320);
            ToolTipService::SetToolTip(item, box_value(value));
            AutomationProperties::SetName(item, value);
        }

        TextBlock makeTrimmedLabel(std::wstring const& value, double maxWidth)
        {
            TextBlock label;
            label.Text(value);
            label.MaxWidth(maxWidth);
            label.TextTrimming(TextTrimming::CharacterEllipsis);
            label.TextWrapping(TextWrapping::NoWrap);
            label.VerticalAlignment(VerticalAlignment::Center);
            ToolTipService::SetToolTip(label, box_value(value));
            return label;
        }

        Grid makeCategoryRowContent(std::wstring const& name, std::string const& colorHex, double width)
        {
            Grid content;
            // Keep the entire realized NavigationViewItem row hit-testable.  A
            // fixed-width content panel left the trailing padding outside the
            // visual child, which made drag/drop dependent on the pointer being
            // over the text or dot.  The item itself owns the routed handlers;
            // this transparent, stretched child ensures every visible pixel is
            // part of the same drop surface.
            content.Width(std::numeric_limits<double>::quiet_NaN());
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            content.Background(themeBrush(L"Transparent"));
            content.ColumnSpacing(9);
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().Append(ColumnDefinition());
            content.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            content.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

            auto text = makeTrimmedLabel(name, width - 17);
            content.Children().Append(text);

            Microsoft::UI::Xaml::Shapes::Ellipse dot;
            dot.Width(8);
            dot.Height(8);
            dot.Fill(cuelet::windows::categoryColorBrush(colorHex));
            dot.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(dot, 1);
            content.Children().Append(dot);
            return content;
        }

        void setFixedDialogWidth(ContentDialog const& dialog, StackPanel const& content, double dialogWidth, double contentWidth)
        {
            dialog.Resources().Insert(
                box_value(L"ContentDialogMinWidth"), box_value(dialogWidth));
            dialog.Resources().Insert(
                box_value(L"ContentDialogMaxWidth"), box_value(dialogWidth));
            content.Width(contentWidth);
            content.MaxWidth(contentWidth);
        }

        TextBlock makeInformationHeading(std::wstring const& text)
        {
            TextBlock heading;
            heading.Text(text);
            heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            heading.FontSize(16);
            heading.TextWrapping(TextWrapping::Wrap);
            return heading;
        }

        TextBlock makeInformationBody(std::wstring const& text)
        {
            TextBlock body;
            body.Text(text);
            body.TextWrapping(TextWrapping::Wrap);
            body.IsTextSelectionEnabled(true);
            return body;
        }

        HyperlinkButton makeExternalLink(
            std::wstring const& label,
            std::wstring const& uri,
            std::wstring const& accessibleName)
        {
            HyperlinkButton link;
            link.Content(box_value(label));
            link.NavigateUri(Uri(uri));
            link.Padding(ThicknessHelper::FromUniformLength(0));
            link.HorizontalAlignment(HorizontalAlignment::Left);
            ToolTipService::SetToolTip(link, box_value(L"Open " + uri));
            AutomationProperties::SetName(link, accessibleName);
            return link;
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

        NavigationViewItem navigationItemFromSource(
            IInspectable const& source, DependencyObject const& boundary)
        {
            auto current = source.try_as<DependencyObject>();
            while (current && current != boundary) {
                if (auto item = current.try_as<NavigationViewItem>()) return item;
                current = VisualTreeHelper::GetParent(current);
            }
            return nullptr;
        }

        std::wstring formatDuration(double seconds, bool known = true)
        {
            return cuelet::windows::formatDurationLabel(seconds, known);
        }

        std::wstring devicePropertyText(DeviceInformation const& device, wchar_t const* key)
        {
            if (!device.Properties().HasKey(key)) return {};
            const auto value = device.Properties().Lookup(key);
            if (const auto guidValue = value.try_as<IReference<guid>>()) {
                return to_hstring(guidValue.Value()).c_str();
            }
            return unbox_value_or<hstring>(value, L"").c_str();
        }

        std::wstring audioEndpointInstanceId(std::wstring_view identifier)
        {
            constexpr std::wstring_view instancePrefix = L"SWD\\MMDEVAPI\\";
            constexpr std::wstring_view interfacePrefix = L"\\\\?\\SWD#MMDEVAPI#";
            if (identifier.size() >= instancePrefix.size() &&
                ::_wcsnicmp(
                    identifier.data(), instancePrefix.data(),
                    instancePrefix.size()) == 0) {
                return std::wstring(identifier);
            }
            if (identifier.size() > interfacePrefix.size() &&
                ::_wcsnicmp(
                    identifier.data(), interfacePrefix.data(),
                    interfacePrefix.size()) == 0) {
                const auto interfaceSuffix = identifier.rfind(L"#{");
                if (interfaceSuffix > interfacePrefix.size()) {
                    return std::wstring(instancePrefix) +
                        std::wstring(identifier.substr(
                            interfacePrefix.size(),
                            interfaceSuffix - interfacePrefix.size()));
                }
                return {};
            }
            if (identifier.rfind(L"{0.0.", 0) == 0) {
                return std::wstring(instancePrefix) + std::wstring(identifier);
            }
            return {};
        }

        bool audioEndpointIdsEqual(
            std::wstring_view left, std::wstring_view right)
        {
            const auto leftInstance = audioEndpointInstanceId(left);
            const auto rightInstance = audioEndpointInstanceId(right);
            if (leftInstance.empty() || rightInstance.empty()) {
                return left.size() == right.size() &&
                    ::_wcsnicmp(left.data(), right.data(), left.size()) == 0;
            }
            return ::_wcsicmp(
                leftInstance.c_str(), rightInstance.c_str()) == 0;
        }

        std::wstring pnpStringProperty(
            std::wstring const& instanceId,
            DEVPROPKEY const& property)
        {
            if (instanceId.empty()) return {};
            const auto devices = ::SetupDiGetClassDevsW(
                nullptr, nullptr, nullptr,
                DIGCF_ALLCLASSES | DIGCF_PRESENT);
            if (devices == INVALID_HANDLE_VALUE) return {};

            std::wstring value;
            SP_DEVINFO_DATA data{sizeof(data)};
            if (::SetupDiOpenDeviceInfoW(
                    devices, instanceId.c_str(), nullptr, 0, &data)) {
                DEVPROPTYPE type = 0;
                DWORD bytes = 0;
                ::SetupDiGetDevicePropertyW(
                    devices, &data, &property,
                    &type, nullptr, 0, &bytes, 0);
                if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
                    type == DEVPROP_TYPE_STRING &&
                    bytes >= sizeof(wchar_t) &&
                    bytes % sizeof(wchar_t) == 0) {
                    std::vector<BYTE> buffer(bytes);
                    if (::SetupDiGetDevicePropertyW(
                            devices, &data, &property,
                            &type, buffer.data(), bytes, nullptr, 0) &&
                        type == DEVPROP_TYPE_STRING) {
                        const auto* text =
                            reinterpret_cast<wchar_t const*>(buffer.data());
                        const auto characters = bytes / sizeof(wchar_t);
                        if (text[characters - 1] == L'\0') {
                            value.assign(text);
                        }
                    }
                }
            }
            ::SetupDiDestroyDeviceInfoList(devices);
            return value;
        }

        std::wstring audioEndpointParent(DeviceInformation const& device)
        {
            auto instanceId = audioEndpointInstanceId(devicePropertyText(
                device, L"System.Devices.DeviceInstanceId"));
            if (instanceId.empty()) {
                instanceId = audioEndpointInstanceId(device.Id().c_str());
            }
            return pnpStringProperty(
                instanceId, DEVPKEY_Device_Parent);
        }

        cuelet::windows::AudioEndpointDescriptor describeEndpoint(
            DeviceInformation const& device, bool capture)
        {
            cuelet::windows::AudioEndpointDescriptor descriptor;
            descriptor.id = cuelet::windows::hstringToUtf8(device.Id());
            descriptor.name = device.Name().c_str();
            descriptor.containerId = devicePropertyText(device, L"System.Devices.ContainerId");
            descriptor.manufacturer = devicePropertyText(device, L"System.Devices.Manufacturer");
            descriptor.instanceId = audioEndpointInstanceId(device.Id().c_str());
            descriptor.providerName = devicePropertyText(device, L"System.Devices.DriverProvider");
            descriptor.pairingId = devicePropertyText(
                device, L"{1a7b44f5-2c93-48f5-a18b-46399d69e13f} 2");
            descriptor.parentInstanceId = audioEndpointParent(device);
            if (!descriptor.parentInstanceId.empty()) {
                const auto parentManufacturer = pnpStringProperty(
                    descriptor.parentInstanceId,
                    DEVPKEY_Device_Manufacturer);
                const auto parentProvider = pnpStringProperty(
                    descriptor.parentInstanceId,
                    DEVPKEY_Device_DriverProvider);
                if (!parentManufacturer.empty()) {
                    descriptor.manufacturer = parentManufacturer;
                }
                if (!parentProvider.empty()) {
                    descriptor.providerName = parentProvider;
                }
            }
            descriptor.capture = capture;
            descriptor.enabled = device.IsEnabled();
            return descriptor;
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
        cuelet::windows::setDiagnosticShutdownState(m_shutdown.state());
        InitializeComponent();
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&m_hwnd));
        const auto module = ::GetModuleHandleW(nullptr);
        const auto largeIcon = static_cast<HICON>(::LoadImageW(
            module, MAKEINTRESOURCEW(IDI_CUELET), IMAGE_ICON,
            ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_SHARED));
        const auto smallIcon = static_cast<HICON>(::LoadImageW(
            module, MAKEINTRESOURCEW(IDI_CUELET), IMAGE_ICON,
            ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (largeIcon) ::SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        if (smallIcon) ::SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
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
        ImportBehaviorCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            if (m_loadingSettings) return;
            m_importBehavior = ImportBehaviorCombo().SelectedIndex() == 1
                ? cuelet::windows::ImportBehavior::Link : cuelet::windows::ImportBehavior::Copy;
            saveSettings();
        });
        KeepBackgroundToggle().Toggled({this, &MainWindow::Setting_Toggled});
        MinimizeToTrayToggle().Toggled({this, &MainWindow::Setting_Toggled});
        MonitorLocallyToggle().Toggled([this](IInspectable const&, RoutedEventArgs const&) { audioRoutingChanged(); });
        MixMicrophoneToggle().Toggled([this](IInspectable const&, RoutedEventArgs const&) { audioRoutingChanged(); });
        PlaybackOutputCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
        BroadcastOutputCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
        VirtualCaptureCombo().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) { audioRoutingChanged(); });
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
        m_notificationTimer = DispatcherQueue().CreateTimer();
        m_notificationTimer.IsRepeating(false);
        m_notificationTimerToken = m_notificationTimer.Tick([weak = get_weak()](
            Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&) {
            if (auto self = weak.get()) self->StatusInfoBar().IsOpen(false);
        });
        m_audioRefreshTimer = DispatcherQueue().CreateTimer();
        m_audioRefreshTimer.IsRepeating(false);
        m_audioRefreshTimer.Interval(std::chrono::milliseconds(350));
        m_audioRefreshTimerToken = m_audioRefreshTimer.Tick([weak = get_weak()](
            Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&) {
            if (auto self = weak.get()) self->initializeAudioRoutingAsync();
        });
        m_microphoneLevelTimer = DispatcherTimer();
        m_microphoneLevelTimer.Interval(std::chrono::milliseconds(80));
        m_microphoneLevelTimerToken = m_microphoneLevelTimer.Tick(
            [weak = get_weak()](IInspectable const&, IInspectable const&) {
                if (auto self = weak.get()) self->updateMicrophoneLevel();
            });
        m_microphoneOpenTimer = DispatcherQueue().CreateTimer();
        m_microphoneOpenTimer.IsRepeating(false);
        m_microphoneOpenTimer.Interval(std::chrono::seconds(10));
        m_microphoneOpenTimerToken = m_microphoneOpenTimer.Tick(
            [weak = get_weak()](Microsoft::UI::Dispatching::DispatcherQueueTimer const&,
                                IInspectable const&) {
                if (auto self = weak.get(); self && self->m_testingMicrophone) {
                    self->stopMicrophoneTest();
                    self->MicrophoneAccessInfo().Severity(InfoBarSeverity::Error);
                    self->MicrophoneAccessInfo().Title(L"Microphone test timed out");
                    self->MicrophoneAccessInfo().Message(
                        L"Windows did not open the selected endpoint within 10 seconds. It may be busy or disconnected.");
                    self->MicrophoneAccessInfo().IsOpen(true);
                }
            });
        const auto navigationDragOver = DragEventHandler(
            [weak = get_weak()](IInspectable const&, DragEventArgs const& args) {
                if (auto self = weak.get()) {
                    if (navigationItemFromSource(args.OriginalSource(), self->Navigation())) return;
                    const bool internalSound = args.DataView().Contains(cueletSoundDataFormat);
                    const bool storageItems = args.DataView().Contains(
                        StandardDataFormats::StorageItems());
                    if (!internalSound && !storageItems) return;
                    if (cuelet::windows::libraryStartupState(self->m_libraryFolder) !=
                        cuelet::windows::LibraryStartupState::Ready) {
                        args.AcceptedOperation(DataPackageOperation::None);
                    } else {
                        args.AcceptedOperation(internalSound
                            ? DataPackageOperation::Move
                            : self->m_importBehavior == cuelet::windows::ImportBehavior::Link
                                ? DataPackageOperation::Link : DataPackageOperation::Copy);
                        self->showDropState("uncategorized", internalSound);
                    }
                    args.Handled(true);
                }
            });
        Navigation().AddHandler(
            UIElement::DragOverEvent(),
            box_value(navigationDragOver),
            true);
        const auto navigationDrop = DragEventHandler(
            [weak = get_weak()](IInspectable const&, DragEventArgs const& args) {
                if (auto self = weak.get()) {
                    if (navigationItemFromSource(args.OriginalSource(), self->Navigation())) return;
                    const bool internalSound = args.DataView().Contains(cueletSoundDataFormat);
                    const bool storageItems = args.DataView().Contains(
                        StandardDataFormats::StorageItems());
                    if (!internalSound && !storageItems) return;
                    args.AcceptedOperation(internalSound
                        ? DataPackageOperation::Move
                        : self->m_importBehavior == cuelet::windows::ImportBehavior::Link
                            ? DataPackageOperation::Link : DataPackageOperation::Copy);
                    self->DropOverlay().Visibility(Visibility::Collapsed);
                    self->dropOnCategoryAsync(args.DataView(), "uncategorized");
                    args.Handled(true);
                }
            });
        Navigation().AddHandler(
            UIElement::DropEvent(),
            box_value(navigationDrop),
            true);
        const auto navigationDragLeave = DragEventHandler(
            [weak = get_weak()](IInspectable const&, DragEventArgs const&) {
                if (auto self = weak.get()) {
                    self->clearCategoryDropVisual();
                    self->DropOverlay().Visibility(Visibility::Collapsed);
                }
            });
        Navigation().AddHandler(
            UIElement::DragLeaveEvent(),
            box_value(navigationDragLeave),
            true);
        StatusInfoBar().Closed([weak = get_weak()](InfoBar const&, InfoBarClosedEventArgs const&) {
            if (auto self = weak.get()) if (self->m_notificationTimer) self->m_notificationTimer.Stop();
        });
        Closed([weak = get_weak()](IInspectable const&, WindowEventArgs const&) {
            if (auto self = weak.get()) {
                self->m_shutdown.stopped();
                cuelet::windows::setDiagnosticShutdownState(self->m_shutdown.state());
                cuelet::windows::logDiagnostic(L"shutdown.window_closed");
            }
        });
        Activated([weak = get_weak()](IInspectable const&, WindowActivatedEventArgs const& args) {
            if (args.WindowActivationState() != WindowActivationState::Deactivated) {
                if (auto self = weak.get()) {
                    self->m_shutdown.windowShown();
                    cuelet::windows::setDiagnosticShutdownState(self->m_shutdown.state());
                    self->refreshMicrophoneAccessState();
                }
            }
        });
        initializeAudioRoutingAsync();
        refreshVirtualDriverStatusAsync();
        startAudioDeviceWatchers();

        if (cuelet::windows::libraryStartupState(m_libraryFolder) ==
            cuelet::windows::LibraryStartupState::Ready) {
            scanLibrary();
        } else {
            showLibraryStartupState();
            refreshSounds();
        }
    }

    MainWindow::~MainWindow()
    {
        cleanupNativeResources();
        if (m_hwnd) {
            ::RemoveWindowSubclass(m_hwnd, &MainWindow::windowSubclassProc, 1);
            m_hwnd = nullptr;
        }
        m_shutdown.stopped();
        cuelet::windows::setDiagnosticShutdownState(m_shutdown.state());
    }

    bool MainWindow::acceptsUiWork(std::uint64_t generation) const noexcept
    {
        return m_shutdown.acceptsUiWork(generation);
    }

    IAsyncOperation<ContentDialogResult> MainWindow::showDialogAsync(
        ContentDialog const& dialog)
    {
        const auto generation = m_shutdown.generation();
        if (!acceptsUiWork(generation)) {
            co_return ContentDialogResult::None;
        }
        if (m_activeDialog && m_activeDialog != dialog) {
            m_activeDialog.Focus(FocusState::Programmatic);
            co_return ContentDialogResult::None;
        }
        dialog.RequestedTheme(RootGrid().ActualTheme());
        m_activeDialog = dialog;
        try {
            const auto result = co_await dialog.ShowAsync();
            if (m_activeDialog == dialog) m_activeDialog = nullptr;
            if (!acceptsUiWork(generation)) {
                co_return ContentDialogResult::None;
            }
            co_return result;
        } catch (...) {
            if (m_activeDialog == dialog) m_activeDialog = nullptr;
            if (!acceptsUiWork(generation)) {
                co_return ContentDialogResult::None;
            }
            throw;
        }
    }

    fire_and_forget MainWindow::showAboutAsync()
    {
        auto lifetime = get_strong();
        const auto information = cuelet::windows::aboutInformation(
            cuelet::windows::applicationVersionFromCurrentModule());

        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"About Cuelet"));
        dialog.CloseButtonText(L"Close");
        dialog.DefaultButton(ContentDialogButton::Close);
        AutomationProperties::SetName(dialog, L"About Cuelet");

        StackPanel content;
        content.Spacing(12);
        setFixedDialogWidth(dialog, content, 570, 506);

        Grid identity;
        identity.ColumnSpacing(18);
        identity.ColumnDefinitions().Append(ColumnDefinition());
        identity.ColumnDefinitions().Append(ColumnDefinition());
        identity.ColumnDefinitions().GetAt(0).Width(
            GridLengthHelper::FromPixels(76));
        identity.ColumnDefinitions().GetAt(1).Width(
            GridLengthHelper::FromValueAndType(1, GridUnitType::Star));

        Image icon;
        icon.Width(72);
        icon.Height(72);
        icon.Stretch(Stretch::Uniform);
        icon.VerticalAlignment(VerticalAlignment::Center);
        icon.Source(BitmapImage(Uri(
            L"ms-appx:///Assets/Square44x44Logo.targetsize-256.png")));
        AutomationProperties::SetName(icon, L"Cuelet application icon");
        identity.Children().Append(icon);

        StackPanel identityText;
        identityText.Spacing(3);
        identityText.VerticalAlignment(VerticalAlignment::Center);
        TextBlock applicationName;
        applicationName.Text(information.applicationName);
        applicationName.FontSize(30);
        applicationName.FontWeight(
            Windows::UI::Text::FontWeights::SemiBold());
        applicationName.Foreground(SolidColorBrush(
            Windows::UI::ColorHelper::FromArgb(255, 0x6a, 0x00, 0xff)));
        AutomationProperties::SetName(applicationName, L"Cuelet");
        identityText.Children().Append(applicationName);
        identityText.Children().Append(
            makeInformationBody(information.contributors));
        identityText.Children().Append(
            makeInformationBody(L"Version " + information.version));
        Grid::SetColumn(identityText, 1);
        identity.Children().Append(identityText);
        content.Children().Append(identity);

        content.Children().Append(makeInformationBody(information.description));
        content.Children().Append(makeInformationBody(
            information.licenseStatement));

        Grid links;
        links.ColumnSpacing(12);
        links.RowSpacing(2);
        links.ColumnDefinitions().Append(ColumnDefinition());
        links.ColumnDefinitions().Append(ColumnDefinition());
        links.ColumnDefinitions().GetAt(0).Width(
            GridLengthHelper::FromPixels(92));
        links.ColumnDefinitions().GetAt(1).Width(
            GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        links.RowDefinitions().Append(RowDefinition());
        links.RowDefinitions().Append(RowDefinition());
        auto projectLabel = makeInformationHeading(L"Project");
        links.Children().Append(projectLabel);
        auto projectLink = makeExternalLink(
            information.projectUri, information.projectUri,
            L"Open the Cuelet project website");
        Grid::SetColumn(projectLink, 1);
        links.Children().Append(projectLink);
        auto issuesLabel = makeInformationHeading(L"Issue tracker");
        Grid::SetRow(issuesLabel, 1);
        links.Children().Append(issuesLabel);
        auto issuesLink = makeExternalLink(
            information.issueTrackerUri, information.issueTrackerUri,
            L"Open the Cuelet issue tracker");
        Grid::SetColumn(issuesLink, 1);
        Grid::SetRow(issuesLink, 1);
        links.Children().Append(issuesLink);
        content.Children().Append(links);

        dialog.Content(content);
        co_await showDialogAsync(dialog);
    }

    fire_and_forget MainWindow::showHelpAsync()
    {
        auto lifetime = get_strong();
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Cuelet Help"));
        dialog.CloseButtonText(L"Close");
        dialog.DefaultButton(ContentDialogButton::Close);
        AutomationProperties::SetName(dialog, L"Cuelet Help");

        StackPanel content;
        content.Spacing(18);
        content.Width(620);
        content.MaxWidth(620);
        for (auto const& section : cuelet::windows::helpSections()) {
            StackPanel sectionPanel;
            sectionPanel.Spacing(6);
            sectionPanel.Children().Append(
                makeInformationHeading(section.title));
            sectionPanel.Children().Append(makeInformationBody(section.body));
            if (!section.linkUri.empty()) {
                sectionPanel.Children().Append(makeExternalLink(
                    section.linkLabel, section.linkUri,
                    L"Open the official VB-CABLE website"));
            }
            content.Children().Append(sectionPanel);
        }

        const auto information = cuelet::windows::aboutInformation({});
        StackPanel support;
        support.Spacing(2);
        support.Children().Append(makeInformationHeading(L"Support & Online"));
        support.Children().Append(makeExternalLink(
            L"Project: " + information.projectUri, information.projectUri,
            L"Open the Cuelet project website"));
        support.Children().Append(makeExternalLink(
            L"Report an issue: " + information.issueTrackerUri,
            information.issueTrackerUri, L"Report a Cuelet issue"));
        content.Children().Append(support);

        ScrollViewer scroll;
        scroll.MaxHeight(360);
        scroll.VerticalScrollMode(ScrollMode::Enabled);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.HorizontalScrollMode(ScrollMode::Disabled);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.Content(content);
        dialog.Resources().Insert(
            box_value(L"ContentDialogMinWidth"), box_value(684.0));
        dialog.Resources().Insert(
            box_value(L"ContentDialogMaxWidth"), box_value(684.0));
        dialog.Content(scroll);
        co_await showDialogAsync(dialog);
    }

    void MainWindow::closeActiveDialog() noexcept
    {
        try {
            if (m_activeDialog) {
                cuelet::windows::logDiagnostic(L"shutdown.dialog.close");
                auto dialog = m_activeDialog;
                m_activeDialog = nullptr;
                dialog.Hide();
            }
        } catch (...) {
            m_activeDialog = nullptr;
            cuelet::windows::logDiagnostic(L"shutdown.dialog.error");
        }
    }

    void MainWindow::beginFinalShutdown(cuelet::windows::ShutdownReason reason) noexcept
    {
        if (m_shutdown.state() != cuelet::windows::ShutdownState::ShuttingDown) return;
        cuelet::windows::setDiagnosticShutdownState(m_shutdown.state());
        cuelet::windows::logDiagnostic(
            L"shutdown.begin", std::to_wstring(static_cast<int>(reason)));
        try {
            saveSettings();
        } catch (...) {
            cuelet::windows::logDiagnostic(L"shutdown.settings.error");
        }
        closeActiveDialog();
        cleanupNativeResources();
        cuelet::windows::logDiagnostic(L"shutdown.cleanup.complete");
    }

    void MainWindow::cleanupNativeResources() noexcept
    {
        if (m_nativeCleanupCompleted) return;
        m_nativeCleanupCompleted = true;
        cuelet::windows::logDiagnostic(L"shutdown.cleanup.start");
        cuelet::windows::logDiagnostic(
            L"shutdown.async.outstanding",
            std::to_wstring(m_asyncOperations.load(std::memory_order_relaxed)));

        try {
            cuelet::windows::logDiagnostic(L"shutdown.timers.start");
            if (m_driverCancellation) {
                m_driverCancellation->store(true, std::memory_order_release);
                m_driverCancellation.reset();
                cuelet::windows::logDiagnostic(L"shutdown.driver_wait.canceled");
            }
            if (m_playbackTimer) {
                m_playbackTimer.Stop();
                m_playbackTimer.Tick(m_playbackTimerToken);
            }
            if (m_notificationTimer) {
                m_notificationTimer.Stop();
                m_notificationTimer.Tick(m_notificationTimerToken);
            }
            if (m_audioRefreshTimer) {
                m_audioRefreshTimer.Stop();
                m_audioRefreshTimer.Tick(m_audioRefreshTimerToken);
            }
            if (m_microphoneOpenTimer) {
                m_microphoneOpenTimer.Stop();
                m_microphoneOpenTimer.Tick(m_microphoneOpenTimerToken);
            }
            if (m_microphoneLevelTimer) {
                m_microphoneLevelTimer.Stop();
                m_microphoneLevelTimer.Tick(m_microphoneLevelTimerToken);
            }
            cuelet::windows::logDiagnostic(L"shutdown.timers.end");
        } catch (...) {
            cuelet::windows::logDiagnostic(L"shutdown.timers.error");
        }

        try {
            cuelet::windows::logDiagnostic(L"shutdown.audio_test.start");
            stopMicrophoneTest(false);
            cuelet::windows::logDiagnostic(L"shutdown.audio_test.end");
        } catch (...) {
            cuelet::windows::logDiagnostic(L"shutdown.audio_test.error");
        }
        stopAudioDeviceWatchers();

        try {
            cuelet::windows::logDiagnostic(L"shutdown.playback.start");
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
            cuelet::windows::logDiagnostic(L"shutdown.playback.end");
        } catch (...) {
            cuelet::windows::logDiagnostic(L"shutdown.playback.error");
        }

        try {
            cuelet::windows::logDiagnostic(L"shutdown.audio_graph.start");
            if (m_microphoneGraph) {
                if (m_microphoneGraphErrorToken.value) {
                    m_microphoneGraph.UnrecoverableErrorOccurred(m_microphoneGraphErrorToken);
                    m_microphoneGraphErrorToken = {};
                }
                m_microphoneGraph.Stop();
                m_microphoneGraph.Close();
            }
            m_microphoneInputNode = nullptr;
            m_microphoneOutputNode = nullptr;
            m_microphoneGraph = nullptr;
            cuelet::windows::logDiagnostic(L"shutdown.audio_graph.end");
        } catch (...) {
            cuelet::windows::logDiagnostic(L"shutdown.audio_graph.error");
        }

        m_globalShortcuts.unregisterAll();
        m_trayIcon.remove();
        // The subclass is removed by WM_NCDESTROY. Removing the current
        // subclass from inside its WM_CLOSE callback before DefSubclassProc
        // returns can invalidate the active dispatch chain.
    }

    LRESULT CALLBACK MainWindow::windowSubclassProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                     UINT_PTR subclassId, DWORD_PTR referenceData)
    {
        constexpr ULONG_PTR activationMessageId = 0x4355454C;
        constexpr ULONG_PTR cliMessageId = 0x434C4931; // CLI1
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
            if (data && data->dwData == cliMessageId && data->lpData && data->cbData >= sizeof(wchar_t))
            {
                self->handleCliCopyData(static_cast<wchar_t const*>(data->lpData));
                return TRUE;
            }
        }
        if (message == WM_HOTKEY && self) {
            const auto focused = FocusManager::GetFocusedElement(self->RootGrid().XamlRoot());
            if (focused.try_as<TextBox>() || focused.try_as<AutoSuggestBox>()) return 0;
            if (self->m_globalShortcuts.handleHotKey(wparam)) return 0;
        }
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
        if (message == WM_CLOSE && self) {
            const auto reason = self->m_exiting
                ? cuelet::windows::ShutdownReason::TrayExit
                : cuelet::windows::ShutdownReason::WindowClose;
            const auto decision = self->m_shutdown.request(
                reason, self->m_keepRunningInBackground);
            cuelet::windows::setDiagnosticShutdownState(self->m_shutdown.state());
            if (decision == cuelet::windows::ShutdownDecision::HideWindow) {
                const bool showHint = !self->m_backgroundHintShown;
                self->m_backgroundHintShown = true;
                self->saveSettings();
                self->m_trayIcon.add(
                    self->m_globalShortcuts.failureCount() == 0, showHint);
                ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (decision == cuelet::windows::ShutdownDecision::BeginFinalShutdown) {
                self->beginFinalShutdown(reason);
            } else if (decision == cuelet::windows::ShutdownDecision::AlreadyStopped) {
                return 0;
            }
        }
        if (message == WM_NCDESTROY) {
            ::RemoveWindowSubclass(hwnd, &MainWindow::windowSubclassProc, subclassId);
            if (self) self->m_hwnd = nullptr;
        }
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
        m_importBehavior = cuelet::windows::importBehaviorFromSetting(
            readRegistryString(L"ImportBehavior").value_or(L"copy"));
        if (auto value = readRegistryString(L"PlaybackOutputId")) m_playbackOutputId = cuelet::windows::wideToUtf8(*value);
        if (auto value = readRegistryString(L"BroadcastOutputId")) m_broadcastOutputId = cuelet::windows::wideToUtf8(*value);
        if (auto value = readRegistryString(L"VirtualCaptureId")) m_virtualCaptureId = cuelet::windows::wideToUtf8(*value);
        if (auto value = readRegistryString(L"MicrophoneInputId")) m_microphoneInputId = cuelet::windows::wideToUtf8(*value);
        m_monitorLocally = readRegistryDword(L"MonitorLocally", 1) != 0;
        m_mixPhysicalMicrophone = readRegistryDword(L"MixPhysicalMicrophone", 1) != 0;
        m_audioSetupCompleted = readRegistryDword(L"AudioSetupCompleted", 0) != 0;
        m_broadcastVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"BroadcastVolume", 800));
        m_microphoneVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"MicrophoneVolume", 800));
        m_soundboardVolume = cuelet::windows::volumeFromSetting(readRegistryDword(L"SoundboardVolume", 1000));
        const auto sortIndex = static_cast<int>(readRegistryDword(L"SortIndex", 0));

        VolumeSlider().Value(m_volume * 100.0);
        MultiplePlaybackToggle().IsOn(m_allowMultiple);
        RecursiveScanToggle().IsOn(m_recursiveScan);
        ShowExtensionsToggle().IsOn(m_showExtensions);
        ImportBehaviorCombo().SelectedIndex(m_importBehavior == cuelet::windows::ImportBehavior::Link ? 1 : 0);
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
        writeRegistryString(key, L"ImportBehavior", cuelet::windows::importBehaviorSetting(m_importBehavior));
        writeRegistryString(key, L"PlaybackOutputId", cuelet::windows::utf8ToWide(m_playbackOutputId));
        writeRegistryString(key, L"BroadcastOutputId", cuelet::windows::utf8ToWide(m_broadcastOutputId));
        writeRegistryString(key, L"VirtualCaptureId", cuelet::windows::utf8ToWide(m_virtualCaptureId));
        writeRegistryString(key, L"MicrophoneInputId", cuelet::windows::utf8ToWide(m_microphoneInputId));
        writeRegistryDword(key, L"MonitorLocally", m_monitorLocally ? 1 : 0);
        writeRegistryDword(key, L"MixPhysicalMicrophone", m_mixPhysicalMicrophone ? 1 : 0);
        writeRegistryDword(key, L"AudioSetupCompleted", m_audioSetupCompleted ? 1 : 0);
        writeRegistryDword(key, L"BroadcastVolume", cuelet::windows::volumeToSetting(m_broadcastVolume));
        writeRegistryDword(key, L"MicrophoneVolume", cuelet::windows::volumeToSetting(m_microphoneVolume));
        writeRegistryDword(key, L"SoundboardVolume", cuelet::windows::volumeToSetting(m_soundboardVolume));
        const auto sortIndex = SortCombo().SelectedIndex();
        writeRegistryDword(key, L"SortIndex", static_cast<DWORD>(sortIndex < 0 ? 0 : sortIndex));
        ::RegCloseKey(key);
    }

    void MainWindow::scanLibrary(bool showSuccess)
    {
        if (cuelet::windows::libraryStartupState(m_libraryFolder) !=
            cuelet::windows::LibraryStartupState::Ready) {
            m_clips.clear();
            registerGlobalShortcuts();
            showLibraryStartupState();
            refreshSounds();
            return;
        }

        OnboardingPage().Visibility(Visibility::Collapsed);
        LibraryPage().Visibility(Visibility::Visible);
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
        bool migratedLocalShortcuts = false;
        for (auto& clip : m_clips) {
            if (clip.shortcut && !clip.shortcut->global) {
                clip.shortcut->global = true;
                clip.shortcut->label = cuelet::windows::wideToUtf8(
                    cuelet::windows::formatShortcut(*clip.shortcut));
                migratedLocalShortcuts = true;
            }
        }
        rebuildCategories();
        refreshSounds();
        indexSoundDurationsAsync(showSuccess);
        registerGlobalShortcuts(true);
        if (migratedLocalShortcuts) saveMetadata(false);

        ImportButton().IsEnabled(true);
        RescanButton().IsEnabled(true);
        LibraryPathText().Text(m_libraryFolder.wstring());

        if (!scan.warning.empty()) {
            showStatus(cuelet::windows::utf8ToWide(scan.warning), InfoBarSeverity::Error);
        } else if (!loaded.warning.empty()) {
            showStatus(cuelet::windows::utf8ToWide(loaded.warning), InfoBarSeverity::Warning);
        } else if (loaded.migratedFromV1) {
            showStatus(L"Legacy library metadata loaded. A backup will be created when changes are saved.", InfoBarSeverity::Informational);
        } else if (migratedLocalShortcuts && m_globalShortcuts.failureCount() == 0) {
            showStatus(L"Existing shortcuts were migrated to global shortcuts.", InfoBarSeverity::Informational);
        } else if (showSuccess) {
            showStatus(L"Library rescanned successfully.", InfoBarSeverity::Success);
        }
        maybeRunAudioSetup();
    }

    void MainWindow::mergeMetadata(cuelet::LibraryMetadata const& metadata)
    {
        std::vector<std::string> matched;
        for (auto& clip : m_clips) {
            auto it = metadata.soundsByRelativePath.find(clip.relativePath);
            if (it == metadata.soundsByRelativePath.end()) continue;
            auto const& stored = it->second;
            if (!stored.soundId.empty()) clip.id = stored.soundId;
            if (!stored.displayName.empty()) clip.displayName = stored.displayName;
            clip.storageMode = stored.storageMode;
            clip.externalPath = stored.externalPath;
            clip.originalSourcePath = stored.originalSourcePath;
            clip.sourceFileName = stored.sourceFileName.empty() ? clip.filename : stored.sourceFileName;
            clip.categoryId = stored.categoryId.empty() ? "uncategorized" : stored.categoryId;
            clip.notes = stored.notes;
            clip.aliases = stored.aliases;
            clip.shortcut = stored.shortcut;
            clip.favorite = stored.favorite;
            clip.durationSeconds = stored.durationSeconds;
            clip.durationKnown = stored.durationKnown;
            clip.durationFileSize = stored.durationFileSize;
            clip.durationModifiedSeconds = stored.durationModifiedSeconds;
            clip.durationSourcePath = stored.durationSourcePath;
            if (stored.addedAt) clip.addedAt = *stored.addedAt;
            clip.lastPlayedAt = stored.lastPlayedAt;
            matched.push_back(clip.relativePath);
        }

        for (auto const& [relativePath, stored] : metadata.soundsByRelativePath) {
            if (std::find(matched.begin(), matched.end(), relativePath) != matched.end()) continue;
            cuelet::SoundClip clip;
            clip.relativePath = relativePath;
            clip.storageMode = stored.storageMode;
            clip.externalPath = stored.externalPath;
            clip.originalSourcePath = stored.originalSourcePath;
            clip.sourceFileName = stored.sourceFileName;
            if (stored.storageMode == cuelet::SoundStorageMode::Linked) {
                clip.absolutePath = stored.externalPath;
                clip.filename = stored.sourceFileName.empty()
                    ? cuelet::filenameFromPath(stored.externalPath) : stored.sourceFileName;
            } else {
                clip.absolutePath = cuelet::windows::pathToUtf8(
                    m_libraryFolder / cuelet::windows::pathFromUtf8(relativePath));
                clip.filename = cuelet::filenameFromPath(relativePath);
            }
            clip.displayName = stored.displayName.empty() ? cuelet::displayNameFromFilename(clip.filename) : stored.displayName;
            clip.id = stored.soundId.empty() ? cuelet::stableIdForPath(relativePath) : stored.soundId;
            clip.categoryId = stored.categoryId.empty() ? "uncategorized" : stored.categoryId;
            clip.notes = stored.notes;
            clip.aliases = stored.aliases;
            clip.shortcut = stored.shortcut;
            clip.favorite = stored.favorite;
            clip.durationSeconds = stored.durationSeconds;
            clip.durationKnown = stored.durationKnown;
            clip.durationFileSize = stored.durationFileSize;
            clip.durationModifiedSeconds = stored.durationModifiedSeconds;
            clip.durationSourcePath = stored.durationSourcePath;
            std::error_code error;
            clip.missing = !std::filesystem::is_regular_file(
                cuelet::windows::pathFromUtf8(clip.absolutePath), error);
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
            stored.soundId = clip.id;
            stored.displayName = clip.displayName;
            stored.storageMode = clip.storageMode;
            stored.externalPath = clip.externalPath;
            stored.originalSourcePath = clip.originalSourcePath;
            stored.sourceFileName = clip.sourceFileName.empty() ? clip.filename : clip.sourceFileName;
            stored.categoryId = clip.categoryId;
            stored.notes = clip.notes;
            stored.aliases = clip.aliases;
            stored.shortcut = clip.shortcut;
            stored.favorite = clip.favorite;
            stored.durationSeconds = clip.durationSeconds;
            stored.durationKnown = clip.durationKnown;
            stored.durationFileSize = clip.durationFileSize;
            stored.durationModifiedSeconds = clip.durationModifiedSeconds;
            stored.durationSourcePath = clip.durationSourcePath;
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
        clearCategoryDropVisual();
        auto items = Navigation().MenuItems();
        while (items.Size() > 7) items.RemoveAtEnd();
        const auto configureDrop = [weak = get_weak()](NavigationViewItem const& item) {
            item.AllowDrop(true);
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            const auto weakItem = make_weak(item);
            const auto dragOver = DragEventHandler([weak, weakItem](IInspectable const&, DragEventArgs const& args) {
                if (auto self = weak.get()) {
                    const auto currentItem = weakItem.get();
                    if (!currentItem) return;
                    const auto categoryId = cuelet::windows::categoryIdForNavigationTag(
                        unbox_value_or<hstring>(currentItem.Tag(), L""));
                    const bool internalSound = args.DataView().Contains(cueletSoundDataFormat);
                    const bool storageItems = args.DataView().Contains(StandardDataFormats::StorageItems());
                    if (!internalSound && !storageItems) return;
                    if (cuelet::windows::libraryStartupState(self->m_libraryFolder) !=
                        cuelet::windows::LibraryStartupState::Ready) {
                        args.AcceptedOperation(DataPackageOperation::None);
                        args.Handled(true);
                        return;
                    }
                    args.AcceptedOperation(internalSound
                        ? DataPackageOperation::Move
                        : self->m_importBehavior == cuelet::windows::ImportBehavior::Link
                            ? DataPackageOperation::Link : DataPackageOperation::Copy);
                    args.DragUIOverride().IsCaptionVisible(true);
                    const auto category = cuelet::categoryForId(self->m_categories, categoryId);
                    const auto categoryName = category
                        ? cuelet::windows::utf8ToWide(category->name) : std::wstring{L"Uncategorized"};
                    args.DragUIOverride().Caption(internalSound
                        ? L"Move to \u201c" + categoryName + L"\u201d"
                        : self->m_importBehavior == cuelet::windows::ImportBehavior::Link
                            ? L"Link in \u201c" + categoryName + L"\u201d"
                            : L"Copy to \u201c" + categoryName + L"\u201d");
                    self->setCategoryDropVisual(currentItem);
                    self->showDropState(categoryId, internalSound);
                    if (!internalSound) self->inspectDragItemsAsync(args, categoryId);
                    args.Handled(true);
                }
            });
            item.AddHandler(UIElement::DragEnterEvent(), box_value(dragOver), true);
            item.AddHandler(UIElement::DragOverEvent(), box_value(dragOver), true);
            const auto dragLeave = DragEventHandler(
                [weak](IInspectable const&, DragEventArgs const& args) {
                    if (auto self = weak.get()) {
                        self->clearCategoryDropVisual();
                        self->DropOverlay().Visibility(Visibility::Collapsed);
                    }
                    args.Handled(true);
                });
            item.AddHandler(
                UIElement::DragLeaveEvent(),
                box_value(dragLeave),
                true);
            const auto drop = DragEventHandler(
                [weak, weakItem](IInspectable const&, DragEventArgs const& args) {
                    if (auto self = weak.get()) {
                        const auto currentItem = weakItem.get();
                        const auto categoryId = currentItem
                            ? cuelet::windows::categoryIdForNavigationTag(
                                unbox_value_or<hstring>(currentItem.Tag(), L""))
                            : std::string{"uncategorized"};
                        const bool internalSound = args.DataView().Contains(cueletSoundDataFormat);
                        args.AcceptedOperation(internalSound
                            ? DataPackageOperation::Move
                            : self->m_importBehavior == cuelet::windows::ImportBehavior::Link
                                ? DataPackageOperation::Link : DataPackageOperation::Copy);
                        self->clearCategoryDropVisual();
                        self->DropOverlay().Visibility(Visibility::Collapsed);
                        self->dropOnCategoryAsync(args.DataView(), categoryId);
                        args.Handled(true);
                    }
                });
            item.AddHandler(
                UIElement::DropEvent(),
                box_value(drop),
                true);
        };
        for (std::uint32_t index : {0u, 1u, 2u, 5u}) {
            if (auto neutralItem = items.GetAt(index).try_as<NavigationViewItem>()) {
                configureDrop(neutralItem);
            }
        }
        if (auto allCategories = items.GetAt(5).try_as<NavigationViewItem>()) {
            allCategories.ContextFlyout(makeCategoryMenu(std::nullopt));
        }
        if (auto uncategorizedItem = items.GetAt(6).try_as<NavigationViewItem>()) {
            const auto uncategorized = cuelet::uncategorizedCategory();
            uncategorizedItem.Icon(makeCategoryIcon(uncategorized.iconName));
            uncategorizedItem.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            uncategorizedItem.Content(makeCategoryRowContent(L"Uncategorized", uncategorized.colorHex, 166));
            ToolTipService::SetToolTip(uncategorizedItem, box_value(L"Uncategorized"));
            uncategorizedItem.ContextFlyout(makeCategoryMenu(std::nullopt));
            configureDrop(uncategorizedItem);
        }
        for (auto const& category : m_categories) {
            if (category.id == "uncategorized") continue;
            NavigationViewItem item;
            item.Tag(box_value(L"category:" + cuelet::windows::utf8ToHstring(category.id)));
            item.Icon(makeCategoryIcon(category.iconName));
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            const auto categoryName = cuelet::windows::utf8ToWide(category.name);
            item.Content(makeCategoryRowContent(categoryName, category.colorHex, 166));
            ToolTipService::SetToolTip(item, box_value(categoryName));
            AutomationProperties::SetName(item, categoryName);
            item.ContextFlyout(makeCategoryMenu(category.id));
            configureDrop(item);
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
        if (m_notificationTimer) m_notificationTimer.Stop();
        StatusInfoBar().Message(message);
        StatusInfoBar().Severity(severity);
        StatusInfoBar().Title(L"");
        StatusInfoBar().ActionButton(nullptr);
        StatusInfoBar().IsClosable(true);
        StatusInfoBar().IsOpen(true);
        const auto kind = severity == InfoBarSeverity::Success
            ? cuelet::windows::NotificationKind::Success
            : severity == InfoBarSeverity::Informational
                ? cuelet::windows::NotificationKind::Information
                : severity == InfoBarSeverity::Warning
                    ? cuelet::windows::NotificationKind::Warning
                    : cuelet::windows::NotificationKind::Error;
        if (const auto delay = cuelet::windows::notificationDismissDelay(kind); delay && m_notificationTimer) {
            m_notificationTimer.Interval(*delay);
            m_notificationTimer.Start();
        }
    }

    void MainWindow::showLibraryStartupState()
    {
        const auto state = cuelet::windows::libraryStartupState(m_libraryFolder);
        if (state == cuelet::windows::LibraryStartupState::Ready) {
            OnboardingPage().Visibility(Visibility::Collapsed);
            LibraryPage().Visibility(Visibility::Visible);
            return;
        }
        OnboardingPage().Visibility(Visibility::Visible);
        LibraryPage().Visibility(Visibility::Collapsed);
        SettingsPage().Visibility(Visibility::Collapsed);
        ImportButton().IsEnabled(false);
        RescanButton().IsEnabled(false);
        const bool missing = state == cuelet::windows::LibraryStartupState::ConfiguredLibraryMissing;
        OnboardingTitle().Text(missing ? L"Library Not Found" : L"Welcome to Cuelet");
        OnboardingDescription().Text(missing
            ? L"The configured library is unavailable. Locate it or explicitly choose a replacement."
            : L"Create a sound library or use an existing folder to get started.");
        MissingLibraryPathText().Visibility(missing ? Visibility::Visible : Visibility::Collapsed);
        MissingLibraryPathText().Text(missing ? m_libraryFolder.wstring() : L"");
        WelcomeLibraryActions().Visibility(missing ? Visibility::Collapsed : Visibility::Visible);
        MissingLibraryActions().Visibility(missing ? Visibility::Visible : Visibility::Collapsed);
    }

    std::string MainWindow::importCategoryForCurrentScope() const
    {
        return m_filter.scope == cuelet::LibraryScope::Category && !m_filter.categoryId.empty()
            ? m_filter.categoryId : std::string{"uncategorized"};
    }

    void MainWindow::showDropState(std::optional<std::string> categoryId, bool internalSound)
    {
        const auto category = categoryId.value_or(importCategoryForCurrentScope());
        const auto categoryInfo = cuelet::categoryForId(m_categories, category);
        const auto categoryName = categoryInfo ? cuelet::windows::utf8ToWide(categoryInfo->name)
                                               : std::wstring{L"Uncategorized"};
        DropOverlayTitle().Text(internalSound
            ? L"Move to \u201c" + categoryName + L"\u201d"
            : m_importBehavior == cuelet::windows::ImportBehavior::Link
                ? L"Link in \u201c" + categoryName + L"\u201d"
                : L"Copy to \u201c" + categoryName + L"\u201d");
        DropOverlayDescription().Text(internalSound
            ? L"Reassign this existing Cuelet sound without importing a duplicate."
            : category == "uncategorized"
                ? L"Import as Uncategorized"
                : L"Assign every imported sound to this category.");
        DropOverlay().Visibility(Visibility::Visible);
    }

    void MainWindow::setCategoryDropVisual(NavigationViewItem const& item)
    {
        if (m_activeCategoryDropItem == item) return;
        clearCategoryDropVisual();
        m_activeCategoryDropItem = item;
        item.Background(themeBrush(L"SubtleFillColorSecondaryBrush"));
    }

    void MainWindow::clearCategoryDropVisual()
    {
        if (!m_activeCategoryDropItem) return;
        m_activeCategoryDropItem.ClearValue(Control::BackgroundProperty());
        m_activeCategoryDropItem = nullptr;
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
        state.Text(clip.missing
            ? (clip.storageMode == cuelet::SoundStorageMode::Linked ? L"Missing linked file" : L"Missing")
            : isClipPlaying(clip.id) ? L"Playing"
            : clip.storageMode == cuelet::SoundStorageMode::Linked ? L"Linked" : L"");
        state.Foreground(themeBrush(L"AccentTextFillColorPrimaryBrush"));
        state.VerticalAlignment(VerticalAlignment::Center);
        StackPanel stateAndDrag;
        stateAndDrag.Orientation(Orientation::Horizontal);
        stateAndDrag.Spacing(8);
        stateAndDrag.VerticalAlignment(VerticalAlignment::Center);
        FontIcon dragHandle;
        dragHandle.Glyph(L"\xE700");
        dragHandle.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        dragHandle.FontSize(14);
        dragHandle.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        dragHandle.CanDrag(!clip.missing);
        ToolTipService::SetToolTip(dragHandle, box_value(L"Drag audio file"));
        AutomationProperties::SetName(dragHandle, L"Drag " + displayLabel(clip) + L" as a file");
        dragHandle.DragStarting([weak = get_weak(), id = clip.id](
            UIElement const&, DragStartingEventArgs const& args) {
            if (auto self = weak.get()) self->startSoundDragAsync(args, id);
        });
        stateAndDrag.Children().Append(dragHandle);
        stateAndDrag.Children().Append(state);
        controls.Children().Append(stateAndDrag);
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
        const auto titleText = displayLabel(clip);
        title.Text(titleText);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        title.TextWrapping(TextWrapping::NoWrap);
        ToolTipService::SetToolTip(title, box_value(titleText));
        Grid::SetRow(title, 2);
        layout.Children().Append(title);

        Grid footer;
        footer.ColumnDefinitions().Append(ColumnDefinition());
        footer.ColumnDefinitions().Append(ColumnDefinition());
        footer.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        footer.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());
        footer.Children().Append(makeCategoryChip(clip));
        TextBlock badge;
        badge.Text(formatDuration(clip.durationSeconds, clip.durationKnown));
        badge.MinWidth(42);
        badge.TextAlignment(TextAlignment::Right);
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
        const auto titleText = displayLabel(clip);
        title.Text(titleText);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.VerticalAlignment(VerticalAlignment::Center);
        title.TextTrimming(TextTrimming::CharacterEllipsis);
        title.TextWrapping(TextWrapping::NoWrap);
        ToolTipService::SetToolTip(title, box_value(titleText));
        Grid::SetColumn(title, 1);
        row.Children().Append(title);
        auto category = makeCategoryChip(clip);
        category.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(category, 2);
        row.Children().Append(category);
        TextBlock shortcut;
        shortcut.Text(formatDuration(clip.durationSeconds, clip.durationKnown));
        shortcut.MinWidth(42);
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
        StackPanel stateAndDrag;
        stateAndDrag.Orientation(Orientation::Horizontal);
        stateAndDrag.Spacing(5);
        stateAndDrag.HorizontalAlignment(HorizontalAlignment::Center);
        stateAndDrag.VerticalAlignment(VerticalAlignment::Center);
        FontIcon state;
        state.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        state.Glyph(L"\xE71B");
        state.Foreground(themeBrush(L"AccentTextFillColorPrimaryBrush"));
        if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
            ToolTipService::SetToolTip(state, box_value(
                clip.missing ? L"Linked source file is missing" : L"Linked to the original file"));
        }
        FontIcon dragHandle;
        dragHandle.Glyph(L"\xE700");
        dragHandle.FontFamily(FontFamily(L"Segoe Fluent Icons"));
        dragHandle.FontSize(14);
        dragHandle.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        dragHandle.CanDrag(!clip.missing);
        ToolTipService::SetToolTip(dragHandle, box_value(L"Drag audio file"));
        AutomationProperties::SetName(dragHandle, L"Drag " + displayLabel(clip) + L" as a file");
        dragHandle.DragStarting([weak = get_weak(), id = clip.id](
            UIElement const&, DragStartingEventArgs const& args) {
            if (auto self = weak.get()) self->startSoundDragAsync(args, id);
        });
        if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
            stateAndDrag.Children().Append(state);
        }
        stateAndDrag.Children().Append(dragHandle);
        Grid::SetColumn(stateAndDrag, 5);
        row.Children().Append(stateAndDrag);
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
        chip.MaxWidth(160);
        chip.HorizontalAlignment(HorizontalAlignment::Left);
        chip.PointerPressed([](IInspectable const&, PointerRoutedEventArgs const& args) { args.Handled(true); });
        StackPanel content;
        content.Orientation(Orientation::Horizontal);
        content.Spacing(5);
        Microsoft::UI::Xaml::Shapes::Ellipse dot;
        dot.Width(7); dot.Height(7);
        dot.Fill(cuelet::windows::categoryColorBrush(color));
        dot.VerticalAlignment(VerticalAlignment::Center);
        TextBlock label;
        const auto categoryName = categoryLabel(clip);
        label.Text(categoryName);
        label.Style(applicationStyle(L"CaptionTextBlockStyle"));
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        label.TextWrapping(TextWrapping::NoWrap);
        label.MaxWidth(120);
        ToolTipService::SetToolTip(chip, box_value(categoryName));
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
                setConstrainedMenuText(choice, cuelet::windows::utf8ToWide(category.name));
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
        assign.Text(L"Assign Category");
        assign.Icon(SymbolIcon(Symbol::Tag));
        for (auto const& category : m_categories) {
            MenuFlyoutItem choice;
            setConstrainedMenuText(choice, cuelet::windows::utf8ToWide(category.name));
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
        shortcut.Text(L"Change Shortcut…");
        shortcut.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->changeShortcutAsync(clipId);
        });
        flyout.Items().Append(shortcut);
        if (auto clip = findClip(clipId); clip && clip->shortcut) {
            MenuFlyoutItem clearShortcut;
            clearShortcut.Text(L"Clear Shortcut");
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
        rename.Icon(SymbolIcon(Symbol::Edit));
        rename.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->renameClipAsync(clipId);
        });
        if (auto clip = findClip(clipId)) rename.IsEnabled(!clip->missing);
        flyout.Items().Append(rename);
        if (auto clip = findClip(clipId);
            clip && clip->storageMode == cuelet::SoundStorageMode::Linked && clip->missing) {
            MenuFlyoutItem locate;
            locate.Text(L"Locate Source File…");
            locate.Icon(SymbolIcon(Symbol::OpenFile));
            locate.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) self->locateLinkedSourceAsync(clipId);
            });
            flyout.Items().Append(locate);
        }
        MenuFlyoutItem reveal;
        reveal.Text(L"Reveal in File Explorer");
        reveal.Icon(SymbolIcon(Symbol::OpenFile));
        reveal.Click([weak = get_weak(), clipId](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->showClipInExplorer(clipId);
        });
        flyout.Items().Append(reveal);
        flyout.Items().Append(MenuFlyoutSeparator());
        MenuFlyoutItem remove;
        remove.Text(L"Remove from Library…");
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
            const auto selected = std::filesystem::path(folder.Path().c_str());
            std::error_code validationError;
            if (!std::filesystem::is_directory(selected, validationError)) {
                showStatus(L"The selected library folder is not accessible.", InfoBarSeverity::Error);
                co_return;
            }
            const bool initializeMetadata = !std::filesystem::exists(
                selected / cuelet::windows::WindowsMetadataStore::fileName);
            m_libraryFolder = selected;
            saveSettings();
            setScope(cuelet::LibraryScope::All);
            scanLibrary();
            if (initializeMetadata && !saveMetadata()) co_return;
            showStatus(L"Library ready.", InfoBarSeverity::Success);
        } catch (hresult_error const& error) {
            showStatus(L"Could not open the folder picker: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::createLibraryAsync()
    {
        auto lifetime = get_strong();
        try {
            FolderPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L"*");
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_hwnd));
            const auto parent = co_await picker.PickSingleFolderAsync();
            if (!parent) co_return;

            ContentDialog dialog;
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(box_value(L"Create a New Library"));
            dialog.PrimaryButtonText(L"Create Library");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(ContentDialogButton::Primary);
            StackPanel form;
            form.Spacing(10);
            TextBox name;
            name.Header(box_value(L"Library folder name"));
            name.Text(L"Cuelet Library");
            name.MaxLength(80);
            form.Children().Append(name);
            TextBlock finalLabel;
            finalLabel.Text(L"Final path");
            finalLabel.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            form.Children().Append(finalLabel);
            TextBlock finalPath;
            finalPath.TextWrapping(TextWrapping::Wrap);
            finalPath.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
            form.Children().Append(finalPath);
            const auto updatePath = [=](auto const&, auto const&) {
                finalPath.Text((std::filesystem::path(parent.Path().c_str()) /
                    std::filesystem::path(name.Text().c_str())).wstring());
            };
            name.TextChanged(updatePath);
            updatePath(nullptr, nullptr);
            dialog.Content(form);
            if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;

            const auto trimmedName = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text()));
            if (trimmedName.empty() || trimmedName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
                showStatus(L"Enter a valid library folder name.", InfoBarSeverity::Warning);
                co_return;
            }
            const auto created = std::filesystem::path(parent.Path().c_str()) /
                cuelet::windows::pathFromUtf8(trimmedName);
            std::error_code error;
            if (std::filesystem::exists(created, error)) {
                showStatus(L"A folder with that name already exists. Use Existing Library to select it.",
                           InfoBarSeverity::Warning);
                co_return;
            }
            if (!std::filesystem::create_directories(created, error) || error) {
                showStatus(L"Could not create the library folder: " +
                    cuelet::windows::utf8ToWide(error.message()), InfoBarSeverity::Error);
                co_return;
            }

            cuelet::LibraryMetadata initial;
            std::string metadataError;
            if (!m_metadataStore.save(created, initial, &metadataError)) {
                std::filesystem::remove(created, error);
                showStatus(L"Could not initialize library metadata: " +
                    cuelet::windows::utf8ToWide(metadataError), InfoBarSeverity::Error);
                co_return;
            }
            m_libraryFolder = created;
            saveSettings();
            setScope(cuelet::LibraryScope::All);
            scanLibrary();
            showStatus(L"Library created.", InfoBarSeverity::Success);
        } catch (hresult_error const& error) {
            showStatus(L"Could not create the library: " + std::wstring(error.message()), InfoBarSeverity::Error);
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
            std::vector<std::filesystem::path> paths;
            paths.reserve(files.Size());
            for (auto const& file : files) paths.emplace_back(file.Path().c_str());
            const auto summary = importPaths(paths, m_importBehavior, importCategoryForCurrentScope());
            const auto succeeded = summary.imported + summary.linked;
            std::wstring message = m_importBehavior == cuelet::windows::ImportBehavior::Link
                ? std::to_wstring(summary.linked) + (summary.linked == 1 ? L" sound linked." : L" sounds linked.")
                : std::to_wstring(summary.imported) + (summary.imported == 1 ? L" sound imported." : L" sounds imported.");
            if (summary.duplicates > 0) {
                message += L" " + std::to_wstring(summary.duplicates) +
                    (summary.duplicates == 1 ? L" existing sound updated." : L" existing sounds updated.");
            }
            if (summary.skipped > 0) {
                message += L" " + std::to_wstring(summary.skipped) + L" skipped.";
                showImportWarning(message, summary.details);
            } else if (succeeded > 0 || summary.duplicates > 0) {
                showStatus(message, InfoBarSeverity::Success);
            }
        } catch (hresult_error const& error) {
            showStatus(L"Import failed: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    MainWindow::ImportSummary MainWindow::importPaths(
        std::vector<std::filesystem::path> const& paths,
        cuelet::windows::ImportBehavior behavior,
        std::string const& categoryId)
    {
        ImportSummary summary;
        if (m_libraryFolder.empty()) return summary;
        auto plan = cuelet::windows::makeImportPlan(
            paths, m_libraryFolder, behavior, m_recursiveScan, categoryId, m_clips);
        summary.skipped = plan.unsupported.size() + plan.missing.size();
        for (auto const& path : plan.unsupported) {
            summary.details.push_back(path.filename().wstring() + L" \u2014 unsupported format");
        }
        for (auto const& path : plan.missing) {
            summary.details.push_back(path.wstring() + L" \u2014 file or folder is missing");
        }
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copied;

        for (auto const& entry : plan.entries) {
            if (!entry.duplicateClipId.empty()) {
                if (auto duplicate = findClip(entry.duplicateClipId)) {
                    duplicate->categoryId = entry.categoryId;
                }
                ++summary.duplicates;
                continue;
            }
            if (entry.behavior == cuelet::windows::ImportBehavior::Copy) {
                std::error_code error;
                std::filesystem::copy_file(entry.source, entry.destination,
                                           std::filesystem::copy_options::none, error);
                if (error) {
                    ++summary.skipped;
                    summary.details.push_back(entry.source.filename().wstring() + L" \u2014 copy failed: " +
                        cuelet::windows::utf8ToWide(error.message()));
                    continue;
                }
                copied.emplace_back(entry.destination, entry.source);
                ++summary.imported;
                continue;
            }

            cuelet::SoundClip clip;
            clip.relativePath = cuelet::windows::linkedMetadataKey(entry.source);
            clip.id = cuelet::stableIdForPath(clip.relativePath);
            clip.storageMode = cuelet::SoundStorageMode::Linked;
            clip.externalPath = cuelet::windows::pathToUtf8(entry.source);
            clip.absolutePath = clip.externalPath;
            clip.originalSourcePath = clip.externalPath;
            clip.filename = cuelet::windows::pathToUtf8(entry.source.filename());
            clip.sourceFileName = clip.filename;
            clip.displayName = cuelet::displayNameFromFilename(clip.filename);
            clip.categoryId = entry.categoryId;
            clip.addedAt = std::time(nullptr);
            m_clips.push_back(std::move(clip));
            ++summary.linked;
        }

        if (!copied.empty()) {
            scanLibrary();
            for (auto const& [destination, source] : copied) {
                const auto found = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) {
                    return cuelet::windows::pathsReferToSameFile(
                        destination, cuelet::windows::pathFromUtf8(clip.absolutePath));
                });
                if (found == m_clips.end()) continue;
                found->storageMode = cuelet::SoundStorageMode::Managed;
                found->sourceFileName = found->filename;
                found->originalSourcePath = cuelet::windows::pathToUtf8(source);
                found->categoryId = categoryId.empty() ? "uncategorized" : categoryId;
            }
        }
        if (summary.linked > 0 || summary.duplicates > 0 || !copied.empty()) {
            saveMetadata();
            refreshSounds(true);
            indexSoundDurationsAsync();
        }
        return summary;
    }

    fire_and_forget MainWindow::importStorageItemsAsync(DataPackageView data, std::string categoryId)
    {
        auto lifetime = get_strong();
        try {
            if (!data.Contains(StandardDataFormats::StorageItems())) co_return;
            const auto items = co_await data.GetStorageItemsAsync();
            std::vector<std::filesystem::path> paths;
            for (auto const& item : items) {
                if (!item.Path().empty()) paths.emplace_back(item.Path().c_str());
            }
            const auto summary = importPaths(paths, m_importBehavior, categoryId);
            const auto count = summary.imported + summary.linked;
            std::wstring message;
            if (summary.linked > 0 && summary.imported == 0) {
                message = std::to_wstring(summary.linked) +
                    (summary.linked == 1 ? L" sound linked." : L" sounds linked.");
            } else {
                message = std::to_wstring(summary.imported) +
                    (summary.imported == 1 ? L" sound imported." : L" sounds imported.");
            }
            if (summary.duplicates > 0) {
                message += L" " + std::to_wstring(summary.duplicates) +
                    (summary.duplicates == 1 ? L" existing sound updated." : L" existing sounds updated.");
            }
            if (summary.skipped > 0) {
                message += L" " + std::to_wstring(summary.skipped) + L" skipped.";
                showImportWarning(message, summary.details);
            } else if (count > 0 || summary.duplicates > 0) {
                showStatus(message, InfoBarSeverity::Success);
            } else {
                showStatus(L"No supported new sounds were found.", InfoBarSeverity::Informational);
            }
        } catch (hresult_error const& error) {
            showStatus(L"Drop import failed: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::dropOnCategoryAsync(DataPackageView data, std::string categoryId)
    {
        auto lifetime = get_strong();
        try {
            if (data.Contains(cueletSoundDataFormat)) {
                const auto value = co_await data.GetDataAsync(cueletSoundDataFormat);
                const auto json = unbox_value_or<hstring>(value, L"");
                if (!json.empty()) {
                    const auto object = JsonObject::Parse(json);
                    const auto soundId = cuelet::windows::hstringToUtf8(
                        object.GetNamedString(L"soundId", L""));
                    if (!soundId.empty()) {
                        const auto clip = findClip(soundId);
                        if (clip) {
                            const auto previousCategory = clip->categoryId;
                            cuelet::windows::reassignExistingSound(m_clips, soundId, categoryId);
                            if (!saveMetadata(false)) {
                                cuelet::windows::reassignExistingSound(
                                    m_clips, soundId, previousCategory);
                                showStatus(L"Could not save the category change.",
                                           InfoBarSeverity::Error);
                                co_return;
                            }
                            refreshSounds(true);
                            const auto destination = cuelet::categoryForId(m_categories, categoryId);
                            showStatus(L"Moved to \u201c" +
                                (destination ? cuelet::windows::utf8ToWide(destination->name)
                                             : std::wstring{L"Uncategorized"}) +
                                L"\u201d.", InfoBarSeverity::Success);
                            co_return;
                        }
                    }
                }
            }
            importStorageItemsAsync(data, std::move(categoryId));
        } catch (hresult_error const& error) {
            showStatus(L"Could not complete the category drop: " +
                       std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::inspectDragItemsAsync(
        DragEventArgs args, std::optional<std::string> categoryId)
    {
        auto lifetime = get_strong();
        const auto deferral = args.GetDeferral();
        try {
            const auto items = co_await args.DataView().GetStorageItemsAsync();
            bool hasSupportedInput = false;
            for (auto const& item : items) {
                if (item.IsOfType(StorageItemTypes::Folder)) {
                    hasSupportedInput = true;
                    break;
                }
                if (!item.Path().empty() &&
                    cuelet::LibraryScanner::isSupportedAudioFile(
                        std::filesystem::path(item.Path().c_str()))) {
                    hasSupportedInput = true;
                    break;
                }
            }
            if (!hasSupportedInput) {
                args.AcceptedOperation(DataPackageOperation::None);
                DropOverlayTitle().Text(L"Unsupported file");
                DropOverlayDescription().Text(L"Drop WAV, MP3, M4A, FLAC, OGG, AIFF, or AIF audio files.");
                DropOverlay().Visibility(Visibility::Visible);
            } else {
                showDropState(categoryId);
            }
        } catch (...) {
            args.AcceptedOperation(DataPackageOperation::None);
            DropOverlayTitle().Text(L"Unsupported drop");
            DropOverlayDescription().Text(L"Cuelet could not read the dropped items.");
            DropOverlay().Visibility(Visibility::Visible);
        }
        deferral.Complete();
    }

    void MainWindow::showImportWarning(std::wstring const& message, std::vector<std::wstring> details)
    {
        showStatus(message, InfoBarSeverity::Warning);
        if (details.empty()) return;
        Button action;
        action.Content(box_value(L"Details"));
        action.Click([weak = get_weak(), details = std::move(details)](
            IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->showImportDetailsAsync(details);
        });
        StatusInfoBar().ActionButton(action);
    }

    fire_and_forget MainWindow::showImportDetailsAsync(std::vector<std::wstring> details)
    {
        auto lifetime = get_strong();
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Import Details"));
        dialog.CloseButtonText(L"Close");
        std::wstring text;
        const auto count = std::min<std::size_t>(details.size(), 50);
        for (std::size_t index = 0; index < count; ++index) {
            if (!text.empty()) text += L"\n";
            text += L"\u2022 " + details[index];
        }
        if (details.size() > count) {
            text += L"\n\u2022 \u2026and " + std::to_wstring(details.size() - count) + L" more";
        }
        TextBlock content;
        content.Text(text);
        content.TextWrapping(TextWrapping::Wrap);
        ScrollViewer viewer;
        viewer.MaxHeight(420);
        viewer.Content(content);
        dialog.Content(viewer);
        co_await showDialogAsync(dialog);
    }

    fire_and_forget MainWindow::startSoundDragAsync(DragStartingEventArgs args, std::string clipId)
    {
        auto lifetime = get_strong();
        const auto deferral = args.GetDeferral();
        try {
            const auto clip = findClip(clipId);
            if (!clip || clip->missing) {
                args.Cancel(true);
                deferral.Complete();
                co_return;
            }
            const auto path = cuelet::windows::pathFromUtf8(clip->absolutePath);
            if (!std::filesystem::is_regular_file(path)) {
                args.Cancel(true);
                deferral.Complete();
                co_return;
            }
            const auto file = co_await StorageFile::GetFileFromPathAsync(path.wstring());
            std::vector<Windows::Storage::IStorageItem> items{file};
            args.Data().SetStorageItems(items);
            args.Data().RequestedOperation(
                DataPackageOperation::Copy | DataPackageOperation::Link | DataPackageOperation::Move);
            args.Data().Properties().Title(displayLabel(*clip));
            args.Data().Properties().ApplicationName(L"Cuelet");
            args.Data().Properties().Description(L"Audio file from Cuelet");
            args.Data().SetText(path.wstring());

            JsonObject metadata;
            metadata.Insert(L"version", JsonValue::CreateNumberValue(1));
            metadata.Insert(L"soundId", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip->id)));
            metadata.Insert(L"displayName", JsonValue::CreateStringValue(displayLabel(*clip)));
            metadata.Insert(L"categoryId", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip->categoryId)));
            metadata.Insert(L"categoryName", JsonValue::CreateStringValue(categoryLabel(*clip)));
            metadata.Insert(L"favorite", JsonValue::CreateBooleanValue(clip->favorite));
            metadata.Insert(L"durationSeconds", JsonValue::CreateNumberValue(clip->durationSeconds));
            metadata.Insert(L"storageMode", JsonValue::CreateStringValue(
                clip->storageMode == cuelet::SoundStorageMode::Linked ? L"linked" : L"managed"));
            args.Data().SetData(cueletSoundDataFormat, box_value(metadata.Stringify()));
            args.DragUI().SetContentFromDataPackage();
        } catch (...) {
            args.Cancel(true);
        }
        deferral.Complete();
    }

    fire_and_forget MainWindow::indexSoundDurationsAsync(bool force)
    {
        auto lifetime = get_strong();
        if (m_durationIndexRunning) {
            m_durationIndexRequested = true;
            co_return;
        }
        m_durationIndexRunning = true;
        bool changed = false;
        std::vector<std::string> ids;
        ids.reserve(m_clips.size());
        for (auto const& clip : m_clips) ids.push_back(clip.id);

        for (auto const& id : ids) {
            auto clip = findClip(id);
            if (!clip || clip->missing || clip->absolutePath.empty()) continue;
            const auto expectedPath = clip->absolutePath;
            try {
                const auto file = co_await StorageFile::GetFileFromPathAsync(
                    cuelet::windows::utf8ToHstring(expectedPath));
                const auto properties = co_await file.GetBasicPropertiesAsync();
                const auto fileSize = properties.Size();
                const auto modifiedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                    properties.DateModified().time_since_epoch()).count();
                clip = findClip(id);
                if (!clip || clip->absolutePath != expectedPath) continue;
                if (!force && cuelet::windows::durationCacheIsValid(
                        *clip, expectedPath, fileSize, modifiedSeconds)) {
                    continue;
                }

                double durationSeconds = 0.0;
                bool known = false;
                try {
                    const auto music = co_await file.Properties().GetMusicPropertiesAsync();
                    durationSeconds = static_cast<double>(music.Duration().count()) / 10'000'000.0;
                    known = durationSeconds > 0.0;
                } catch (...) {
                }
                if (!known) {
                    try {
                        const auto mediaClip = co_await MediaClip::CreateFromFileAsync(file);
                        durationSeconds = static_cast<double>(
                            mediaClip.OriginalDuration().count()) / 10'000'000.0;
                        known = durationSeconds >= 0.0;
                    } catch (...) {
                        durationSeconds = 0.0;
                    }
                }

                clip = findClip(id);
                if (!clip || clip->absolutePath != expectedPath) continue;
                clip->durationSeconds = durationSeconds;
                clip->durationKnown = known;
                clip->durationFileSize = fileSize;
                clip->durationModifiedSeconds = modifiedSeconds;
                clip->durationSourcePath = expectedPath;
                changed = true;
            } catch (...) {
                clip = findClip(id);
                if (clip && clip->absolutePath == expectedPath && clip->durationKnown) {
                    clip->durationKnown = false;
                    clip->durationSourcePath.clear();
                    changed = true;
                }
            }
        }

        if (changed) {
            saveMetadata(false);
            refreshSounds(true);
            updatePlaybackBar();
        }
        m_durationIndexRunning = false;
        if (m_durationIndexRequested) {
            m_durationIndexRequested = false;
            indexSoundDurationsAsync();
        }
    }

    fire_and_forget MainWindow::initializeAudioRoutingAsync()
    {
        AsyncOperationScope asyncOperation(m_asyncOperations);
        auto lifetime = get_strong();
        const auto generation = m_shutdown.generation();
        if (!acceptsUiWork(generation)) co_return;
        if (m_audioDeviceEnumerationRunning) {
            cuelet::windows::logDiagnostic(
                L"audio_devices.enumeration.skipped", L"already running");
            co_return;
        }
        BoolFlagScope enumerationRunning(m_audioDeviceEnumerationRunning);
        cuelet::windows::logDiagnostic(L"audio_devices.enumeration.start");
        try {
            m_loadingAudioDevices = true;
            stopMicrophoneTest();
            PlaybackOutputCombo().Items().Clear();
            BroadcastOutputCombo().Items().Clear();
            VirtualCaptureCombo().Items().Clear();
            MicrophoneInputCombo().Items().Clear();
            const auto properties = single_threaded_vector<hstring>({
                L"System.Devices.ContainerId",
                L"System.Devices.Manufacturer",
            });
            const auto renders = co_await DeviceInformation::FindAllAsync(
                MediaDevice::GetAudioRenderSelector(), properties);
            if (!acceptsUiWork(generation)) co_return;
            const auto captures = co_await DeviceInformation::FindAllAsync(
                MediaDevice::GetAudioCaptureSelector(), properties);
            if (!acceptsUiWork(generation)) co_return;
            m_renderDevices.assign(renders.begin(), renders.end());
            m_captureDevices.assign(captures.begin(), captures.end());

            auto addChoice = [](ComboBox const& combo, std::wstring const& label,
                                std::string const& id, bool enabled = true) {
                ComboBoxItem item;
                item.Content(box_value(label));
                item.Tag(box_value(cuelet::windows::utf8ToHstring(id)));
                item.IsEnabled(enabled);
                combo.Items().Append(item);
            };
            addChoice(PlaybackOutputCombo(), L"Default speakers/headphones", {});
            addChoice(BroadcastOutputCombo(), L"Off", {});
            addChoice(VirtualCaptureCombo(), L"Choose the matching recording endpoint", {});
            addChoice(MicrophoneInputCombo(), L"Default microphone", {});
            int playbackIndex = 0;
            int broadcastIndex = 0;
            int virtualCaptureIndex = 0;
            int microphoneIndex = 0;
            const auto selectById = [](ComboBox const& combo, std::string const& id) {
                if (id.empty()) return 0;
                const auto wanted = cuelet::windows::utf8ToWide(id);
                for (std::uint32_t index = 0; index < combo.Items().Size(); ++index) {
                    if (auto item = combo.Items().GetAt(index).try_as<ComboBoxItem>()) {
                        if (audioEndpointIdsEqual(
                                unbox_value_or<hstring>(item.Tag(), L"").c_str(),
                                wanted)) {
                            return static_cast<int>(index);
                        }
                    }
                }
                return 0;
            };
            for (auto const& device : m_renderDevices) {
                const auto id = cuelet::windows::hstringToUtf8(device.Id());
                const auto descriptor = describeEndpoint(device, false);
                const auto kind = cuelet::windows::classifyAudioEndpoint(descriptor);
                if (kind == cuelet::windows::AudioEndpointKind::LocalPlayback) {
                    addChoice(PlaybackOutputCombo(), device.Name().c_str(), id, device.IsEnabled());
                }
                if (kind == cuelet::windows::AudioEndpointKind::CueletVirtualRender ||
                    kind == cuelet::windows::AudioEndpointKind::SupportedVirtualRender) {
                    addChoice(
                        BroadcastOutputCombo(),
                        kind == cuelet::windows::AudioEndpointKind::CueletVirtualRender
                            ? L"Cuelet Virtual Microphone Input"
                            : std::wstring(device.Name().c_str()),
                        id, device.IsEnabled());
                } else if (id == m_broadcastOutputId) {
                    addChoice(
                        BroadcastOutputCombo(),
                        L"Advanced manual: " + std::wstring(device.Name().c_str()),
                        id, device.IsEnabled());
                }
            }
            for (auto const& device : m_captureDevices) {
                const auto id = cuelet::windows::hstringToUtf8(device.Id());
                const auto descriptor = describeEndpoint(device, true);
                const auto kind = cuelet::windows::classifyAudioEndpoint(descriptor);
                if (kind == cuelet::windows::AudioEndpointKind::CueletVirtualCapture ||
                    kind == cuelet::windows::AudioEndpointKind::SupportedVirtualCapture) {
                    addChoice(
                        VirtualCaptureCombo(),
                        kind == cuelet::windows::AudioEndpointKind::CueletVirtualCapture
                            ? L"Cuelet Virtual Microphone"
                            : std::wstring(device.Name().c_str()),
                        id, device.IsEnabled());
                } else if (id == m_virtualCaptureId) {
                    addChoice(
                        VirtualCaptureCombo(),
                        L"Advanced manual: " + std::wstring(device.Name().c_str()),
                        id, device.IsEnabled());
                }
                if (cuelet::windows::isPhysicalMicrophone(descriptor)) {
                    addChoice(MicrophoneInputCombo(), device.Name().c_str(), id, device.IsEnabled());
                }
            }
            playbackIndex = selectById(PlaybackOutputCombo(), m_playbackOutputId);
            broadcastIndex = selectById(BroadcastOutputCombo(), m_broadcastOutputId);
            virtualCaptureIndex = selectById(VirtualCaptureCombo(), m_virtualCaptureId);
            std::vector<cuelet::windows::AudioEndpointDescriptor> captureDescriptors;
            captureDescriptors.reserve(m_captureDevices.size());
            for (auto const& device : m_captureDevices) {
                captureDescriptors.push_back(describeEndpoint(device, true));
            }
            if (broadcastIndex == 0 && virtualCaptureIndex == 0 &&
                m_broadcastOutputId.empty() &&
                m_virtualCaptureId.empty()) {
                for (auto const& device : m_renderDevices) {
                    const auto renderDescriptor =
                        describeEndpoint(device, false);
                    if (!cuelet::windows::isCueletVirtualEndpoint(
                            renderDescriptor)) {
                        continue;
                    }
                    const auto captureIndex =
                        cuelet::windows::findBestVirtualCapture(
                            renderDescriptor, captureDescriptors);
                    if (!captureIndex ||
                        !cuelet::windows::isCueletVirtualEndpoint(
                            captureDescriptors[*captureIndex])) {
                        continue;
                    }
                    broadcastIndex = selectById(
                        BroadcastOutputCombo(), renderDescriptor.id);
                    virtualCaptureIndex = selectById(
                        VirtualCaptureCombo(),
                        captureDescriptors[*captureIndex].id);
                    if (broadcastIndex > 0 &&
                        virtualCaptureIndex > 0) {
                        cuelet::windows::logDiagnostic(
                            L"audio_devices.cuelet_pair_auto_selected");
                        break;
                    }
                    broadcastIndex = 0;
                    virtualCaptureIndex = 0;
                }
            }
            const auto defaultCommunications = cuelet::windows::hstringToUtf8(
                MediaDevice::GetDefaultAudioCaptureId(AudioDeviceRole::Communications));
            const auto defaultCapture = cuelet::windows::hstringToUtf8(
                MediaDevice::GetDefaultAudioCaptureId(AudioDeviceRole::Default));
            if (const auto selected = cuelet::windows::choosePhysicalMicrophone(
                    captureDescriptors, m_microphoneInputId,
                    defaultCommunications, defaultCapture)) {
                microphoneIndex = selectById(
                    MicrophoneInputCombo(), captureDescriptors[*selected].id);
            }
            if (virtualCaptureIndex == 0 && broadcastIndex > 0) {
                const auto selectedItem =
                    BroadcastOutputCombo().Items().GetAt(
                        static_cast<std::uint32_t>(broadcastIndex))
                        .try_as<ComboBoxItem>();
                const auto selectedId = selectedItem
                    ? cuelet::windows::hstringToUtf8(
                          unbox_value_or<hstring>(selectedItem.Tag(), L""))
                    : std::string{};
                if (const auto selectedRender = findRenderDevice(selectedId)) {
                    if (const auto match = cuelet::windows::findBestVirtualCapture(
                            describeEndpoint(selectedRender, false),
                            captureDescriptors)) {
                        virtualCaptureIndex = selectById(
                            VirtualCaptureCombo(), captureDescriptors[*match].id);
                    }
                }
            }
            PlaybackOutputCombo().SelectedIndex(playbackIndex);
            BroadcastOutputCombo().SelectedIndex(broadcastIndex);
            VirtualCaptureCombo().SelectedIndex(virtualCaptureIndex);
            MicrophoneInputCombo().SelectedIndex(microphoneIndex);
            m_loadingAudioDevices = false;
            refreshMicrophoneAccessState();
            audioRoutingChanged();
            updateVirtualDriverControls();
            cuelet::windows::logDiagnostic(
                L"audio_devices.enumeration.complete",
                L"render=" + std::to_wstring(m_renderDevices.size()) +
                    L" capture=" + std::to_wstring(m_captureDevices.size()));
            maybeRunAudioSetup();
        } catch (hresult_error const& error) {
            if (!acceptsUiWork(generation)) co_return;
            m_loadingAudioDevices = false;
            AudioRoutingInfo().Severity(InfoBarSeverity::Error);
            AudioRoutingInfo().Title(L"Audio routing: Error");
            AudioRoutingInfo().Message(L"Windows audio devices could not be enumerated: " + error.message());
            cuelet::windows::logDiagnostic(
                L"audio_devices.enumeration.error", error.message().c_str());
        }
    }

    void MainWindow::startAudioDeviceWatchers()
    {
        try {
            if (m_renderDeviceWatcher || m_captureDeviceWatcher) return;
            const auto weak = get_weak();
            const auto queue = DispatcherQueue();
            const auto refresh = [weak, queue]() {
                queue.TryEnqueue([weak] {
                    if (auto self = weak.get();
                        self && self->m_shutdown.state() !=
                            cuelet::windows::ShutdownState::ShuttingDown &&
                        self->m_shutdown.state() !=
                            cuelet::windows::ShutdownState::Stopped) {
                        self->scheduleAudioDeviceRefresh();
                    }
                });
            };
            m_renderDeviceWatcher = DeviceInformation::CreateWatcher(
                MediaDevice::GetAudioRenderSelector());
            m_captureDeviceWatcher = DeviceInformation::CreateWatcher(
                MediaDevice::GetAudioCaptureSelector());
            m_renderWatcherAddedToken = m_renderDeviceWatcher.Added([refresh](DeviceWatcher const&, DeviceInformation const&) {
                refresh();
            });
            m_renderWatcherRemovedToken = m_renderDeviceWatcher.Removed([refresh](DeviceWatcher const&, DeviceInformationUpdate const&) {
                refresh();
            });
            m_renderWatcherUpdatedToken = m_renderDeviceWatcher.Updated([refresh](DeviceWatcher const&, DeviceInformationUpdate const&) {
                refresh();
            });
            m_captureWatcherAddedToken = m_captureDeviceWatcher.Added([refresh](DeviceWatcher const&, DeviceInformation const&) {
                refresh();
            });
            m_captureWatcherRemovedToken = m_captureDeviceWatcher.Removed([refresh](DeviceWatcher const&, DeviceInformationUpdate const&) {
                refresh();
            });
            m_captureWatcherUpdatedToken = m_captureDeviceWatcher.Updated([refresh](DeviceWatcher const&, DeviceInformationUpdate const&) {
                refresh();
            });
            m_renderDeviceWatcher.Start();
            m_captureDeviceWatcher.Start();
        } catch (...) {
            m_renderDeviceWatcher = nullptr;
            m_captureDeviceWatcher = nullptr;
        }
    }

    void MainWindow::stopAudioDeviceWatchers() noexcept
    {
        cuelet::windows::logDiagnostic(L"shutdown.watchers.start");
        try {
            if (m_renderDeviceWatcher) {
                if (m_renderWatcherAddedToken.value) {
                    m_renderDeviceWatcher.Added(m_renderWatcherAddedToken);
                    m_renderWatcherAddedToken = {};
                }
                if (m_renderWatcherRemovedToken.value) {
                    m_renderDeviceWatcher.Removed(m_renderWatcherRemovedToken);
                    m_renderWatcherRemovedToken = {};
                }
                if (m_renderWatcherUpdatedToken.value) {
                    m_renderDeviceWatcher.Updated(m_renderWatcherUpdatedToken);
                    m_renderWatcherUpdatedToken = {};
                }
                const auto status = m_renderDeviceWatcher.Status();
                if (status == DeviceWatcherStatus::Started ||
                    status == DeviceWatcherStatus::EnumerationCompleted) {
                    m_renderDeviceWatcher.Stop();
                }
                m_renderDeviceWatcher = nullptr;
            }
            if (m_captureDeviceWatcher) {
                if (m_captureWatcherAddedToken.value) {
                    m_captureDeviceWatcher.Added(m_captureWatcherAddedToken);
                    m_captureWatcherAddedToken = {};
                }
                if (m_captureWatcherRemovedToken.value) {
                    m_captureDeviceWatcher.Removed(m_captureWatcherRemovedToken);
                    m_captureWatcherRemovedToken = {};
                }
                if (m_captureWatcherUpdatedToken.value) {
                    m_captureDeviceWatcher.Updated(m_captureWatcherUpdatedToken);
                    m_captureWatcherUpdatedToken = {};
                }
                const auto status = m_captureDeviceWatcher.Status();
                if (status == DeviceWatcherStatus::Started ||
                    status == DeviceWatcherStatus::EnumerationCompleted) {
                    m_captureDeviceWatcher.Stop();
                }
                m_captureDeviceWatcher = nullptr;
            }
            cuelet::windows::logDiagnostic(L"shutdown.watchers.end");
        } catch (...) {
            m_renderDeviceWatcher = nullptr;
            m_captureDeviceWatcher = nullptr;
            cuelet::windows::logDiagnostic(L"shutdown.watchers.error");
        }
    }

    void MainWindow::scheduleAudioDeviceRefresh()
    {
        if (m_shutdown.state() == cuelet::windows::ShutdownState::ShuttingDown ||
            m_shutdown.state() == cuelet::windows::ShutdownState::Stopped) return;
        DispatcherQueue().TryEnqueue([weak = get_weak()] {
            if (auto self = weak.get()) {
                if (self->m_shutdown.state() ==
                        cuelet::windows::ShutdownState::ShuttingDown ||
                    self->m_shutdown.state() ==
                        cuelet::windows::ShutdownState::Stopped) return;
                if (self->m_audioRefreshTimer) {
                    self->m_audioRefreshTimer.Stop();
                    self->m_audioRefreshTimer.Start();
                }
            }
        });
    }

    void MainWindow::refreshMicrophoneAccessState()
    {
        const auto action = MicrophoneAccessInfo().ActionButton().try_as<Button>();
        DeviceAccessStatus access = DeviceAccessStatus::Unspecified;
        try {
            access = DeviceAccessInformation::CreateFromDeviceClass(
                DeviceClass::AudioCapture).CurrentStatus();
        } catch (hresult_error const& error) {
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Warning);
            MicrophoneAccessInfo().Title(L"Microphone privacy status unavailable");
            MicrophoneAccessInfo().Message(error.message());
            if (action) action.Visibility(Visibility::Visible);
            MicrophoneAccessInfo().IsOpen(true);
            return;
        }
        if (access == DeviceAccessStatus::DeniedByUser ||
            access == DeviceAccessStatus::DeniedBySystem) {
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Error);
            MicrophoneAccessInfo().Title(L"Microphone access is disabled");
            MicrophoneAccessInfo().Message(access == DeviceAccessStatus::DeniedBySystem
                ? L"Windows or your organization has disabled microphone access for Cuelet."
                : L"Allow microphone access in Windows Privacy & security settings, then refresh devices.");
            if (action) action.Visibility(Visibility::Visible);
            MicrophoneAccessInfo().IsOpen(true);
            return;
        }
        const bool hasPhysicalMicrophone = std::any_of(
            m_captureDevices.begin(), m_captureDevices.end(), [](auto const& device) {
                return cuelet::windows::isPhysicalMicrophone(describeEndpoint(device, true));
            });
        if (!hasPhysicalMicrophone) {
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Warning);
            MicrophoneAccessInfo().Title(L"No microphone detected");
            MicrophoneAccessInfo().Message(
                L"Connect a physical microphone and choose Refresh. Local sound playback remains available.");
            if (action) action.Visibility(Visibility::Collapsed);
            MicrophoneAccessInfo().IsOpen(true);
            return;
        }
        if (action) action.Visibility(Visibility::Collapsed);
        MicrophoneAccessInfo().IsOpen(false);
    }

    DeviceInformation MainWindow::findRenderDevice(std::string const& id) const
    {
        const auto wanted = cuelet::windows::utf8ToWide(id);
        const auto found = std::find_if(m_renderDevices.begin(), m_renderDevices.end(), [&](auto const& device) {
            return audioEndpointIdsEqual(device.Id().c_str(), wanted);
        });
        return found == m_renderDevices.end() ? nullptr : *found;
    }

    DeviceInformation MainWindow::findCaptureDevice(std::string const& id) const
    {
        const auto wanted = cuelet::windows::utf8ToWide(id);
        const auto found = std::find_if(m_captureDevices.begin(), m_captureDevices.end(), [&](auto const& device) {
            return audioEndpointIdsEqual(device.Id().c_str(), wanted);
        });
        return found == m_captureDevices.end() ? nullptr : *found;
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
        m_virtualCaptureId = selectedId(VirtualCaptureCombo());
        m_microphoneInputId = selectedId(MicrophoneInputCombo());
        if (m_virtualCaptureId.empty() && !m_broadcastOutputId.empty()) {
            const auto renderDevice = findRenderDevice(m_broadcastOutputId);
            if (renderDevice) {
                std::vector<cuelet::windows::AudioEndpointDescriptor> captures;
                for (auto const& capture : m_captureDevices) {
                    captures.push_back(describeEndpoint(capture, true));
                }
                if (const auto match = cuelet::windows::findBestVirtualCapture(
                        describeEndpoint(renderDevice, false), captures)) {
                    m_virtualCaptureId = captures[*match].id;
                    for (std::uint32_t index = 0;
                         index < VirtualCaptureCombo().Items().Size(); ++index) {
                        const auto item =
                            VirtualCaptureCombo().Items().GetAt(index).try_as<ComboBoxItem>();
                        if (item && cuelet::windows::hstringToUtf8(
                                unbox_value_or<hstring>(item.Tag(), L"")) ==
                                m_virtualCaptureId) {
                            VirtualCaptureCombo().SelectedIndex(static_cast<int>(index));
                            break;
                        }
                    }
                }
            }
        }
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
        AsyncOperationScope asyncOperation(m_asyncOperations);
        auto lifetime = get_strong();
        const auto shutdownGeneration = m_shutdown.generation();
        const auto graphGeneration = ++m_audioGraphGeneration;
        const auto isCurrent = [&] {
            return acceptsUiWork(shutdownGeneration) &&
                graphGeneration == m_audioGraphGeneration;
        };
        if (!isCurrent()) co_return;
        cuelet::windows::logDiagnostic(L"audio_graph.configure.start");
        AudioGraph pendingGraph{nullptr};
        event_token pendingGraphErrorToken{};
        try {
            if (m_microphoneGraph) {
                if (m_microphoneGraphErrorToken.value) {
                    m_microphoneGraph.UnrecoverableErrorOccurred(
                        m_microphoneGraphErrorToken);
                    m_microphoneGraphErrorToken = {};
                }
                m_microphoneGraph.Stop();
                m_microphoneGraph.Close();
                m_microphoneGraph = nullptr;
                m_microphoneInputNode = nullptr;
                m_microphoneOutputNode = nullptr;
            }
            if (m_broadcastOutputId.empty()) {
                AudioRoutingInfo().Severity(InfoBarSeverity::Informational);
                AudioRoutingInfo().Title(L"Voice-chat output: Off");
                AudioRoutingInfo().Message(L"Physical microphone detection and local playback remain available.");
                cuelet::windows::logDiagnostic(L"audio_graph.configure.disabled");
                co_return;
            }
            if (!m_mixPhysicalMicrophone) {
                const auto selectedRender = findRenderDevice(m_broadcastOutputId);
                const auto endpointKind = selectedRender
                    ? cuelet::windows::classifyAudioEndpoint(
                          describeEndpoint(selectedRender, false))
                    : cuelet::windows::AudioEndpointKind::UnknownRender;
                const bool cueletEndpoint =
                    endpointKind ==
                    cuelet::windows::AudioEndpointKind::CueletVirtualRender;
                const bool thirdPartyEndpoint =
                    endpointKind ==
                    cuelet::windows::AudioEndpointKind::SupportedVirtualRender;
                const auto selectedCapture = findCaptureDevice(m_virtualCaptureId);
                AudioRoutingInfo().Severity(InfoBarSeverity::Success);
                AudioRoutingInfo().Title(
                    cueletEndpoint
                        ? L"Cuelet Virtual Microphone: Connected"
                        : thirdPartyEndpoint
                            ? L"Existing virtual cable: Connected"
                            : L"Broadcast output: Connected");
                AudioRoutingInfo().Message(
                    cueletEndpoint || thirdPartyEndpoint
                    ? L"Soundboard audio will be sent to this virtual route. Select \u201c" +
                        (selectedCapture ? std::wstring(selectedCapture.Name().c_str())
                                         : std::wstring(L"the paired recording endpoint")) +
                        L"\u201d in the target app."
                    : L"Soundboard audio will also be sent to the selected render endpoint. Cuelet has not installed a virtual microphone.");
                cuelet::windows::logDiagnostic(L"audio_graph.configure.soundboard_only");
                co_return;
            }
            const auto renderDevice = findRenderDevice(m_broadcastOutputId);
            if (!renderDevice) throw hresult_error(E_INVALIDARG, L"The selected broadcast output is no longer available.");
            AudioGraphSettings settings(Windows::Media::Render::AudioRenderCategory::Communications);
            settings.PrimaryRenderDevice(renderDevice);
            settings.QuantumSizeSelectionMode(QuantumSizeSelectionMode::LowestLatency);
            const auto graphResult = co_await AudioGraph::CreateAsync(settings);
            if (!isCurrent()) {
                if (graphResult.Graph()) graphResult.Graph().Close();
                co_return;
            }
            if (graphResult.Status() != AudioGraphCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not create the broadcast audio graph.");
            pendingGraph = graphResult.Graph();
            const auto outputResult =
                co_await pendingGraph.CreateDeviceOutputNodeAsync();
            if (!isCurrent()) {
                pendingGraph.Close();
                co_return;
            }
            if (outputResult.Status() != AudioDeviceNodeCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not open the broadcast output.");
            const auto outputNode = outputResult.DeviceOutputNode();

            Windows::Media::Audio::CreateAudioDeviceInputNodeResult inputResult{nullptr};
            if (m_microphoneInputId.empty()) {
                inputResult = co_await pendingGraph.CreateDeviceInputNodeAsync(
                    MediaCategory::Communications);
            } else {
                const auto found = std::find_if(m_captureDevices.begin(), m_captureDevices.end(), [&](auto const& device) {
                    return cuelet::windows::hstringToUtf8(device.Id()) == m_microphoneInputId;
                });
                if (found == m_captureDevices.end()) throw hresult_error(E_INVALIDARG, L"The selected microphone is no longer available.");
                inputResult = co_await pendingGraph.CreateDeviceInputNodeAsync(
                    MediaCategory::Communications, nullptr, *found);
            }
            if (!isCurrent()) {
                pendingGraph.Close();
                co_return;
            }
            if (inputResult.Status() != AudioDeviceNodeCreationStatus::Success) throw hresult_error(E_FAIL, L"Windows could not open the microphone input.");
            const auto inputNode = inputResult.DeviceInputNode();
            inputNode.OutgoingGain(m_microphoneVolume);
            inputNode.AddOutgoingConnection(outputNode);
            pendingGraphErrorToken = pendingGraph.UnrecoverableErrorOccurred(
                [weak = get_weak()](
                    AudioGraph const&,
                    AudioGraphUnrecoverableErrorOccurredEventArgs const&) {
                    if (auto self = weak.get()) {
                        self->DispatcherQueue().TryEnqueue([weak] {
                            if (auto current = weak.get();
                                current && current->m_shutdown.state() !=
                                    cuelet::windows::ShutdownState::ShuttingDown &&
                                current->m_shutdown.state() !=
                                    cuelet::windows::ShutdownState::Stopped) {
                                current->AudioRoutingInfo().Severity(
                                    InfoBarSeverity::Error);
                                current->AudioRoutingInfo().Title(
                                    L"Audio routing: Error");
                                current->AudioRoutingInfo().Message(
                                    L"The selected audio device disconnected or "
                                    L"the broadcast graph stopped unexpectedly.");
                            }
                        });
                    }
                });
            pendingGraph.Start();
            m_microphoneGraph = pendingGraph;
            m_microphoneGraphErrorToken = pendingGraphErrorToken;
            m_microphoneInputNode = inputNode;
            m_microphoneOutputNode = outputNode;
            pendingGraph = nullptr;
            pendingGraphErrorToken = {};
            cuelet::windows::logDiagnostic(L"audio_graph.state", L"Started");
            const auto selectedCapture = findCaptureDevice(m_virtualCaptureId);
            AudioRoutingInfo().Severity(InfoBarSeverity::Success);
            AudioRoutingInfo().Title(L"Broadcast output: Connected");
            AudioRoutingInfo().Message(L"The physical microphone and soundboard are routed to the selected render endpoint. Select \u201c" +
                (selectedCapture ? std::wstring(selectedCapture.Name().c_str())
                                 : std::wstring(L"its paired recording endpoint")) +
                L"\u201d in Discord or games. Cuelet cannot verify that an external app received audio.");
        } catch (hresult_error const& error) {
            if (pendingGraph) {
                try {
                    if (pendingGraphErrorToken.value) {
                        pendingGraph.UnrecoverableErrorOccurred(
                            pendingGraphErrorToken);
                    }
                    pendingGraph.Stop();
                    pendingGraph.Close();
                } catch (...) {
                }
            }
            if (!isCurrent()) co_return;
            cuelet::windows::logDiagnostic(
                L"audio_graph.configure.error", error.message().c_str());
            AudioRoutingInfo().Severity(InfoBarSeverity::Error);
            AudioRoutingInfo().Title(L"Audio routing: Error");
            AudioRoutingInfo().Message(error.message());
        }
    }

    fire_and_forget MainWindow::testMicrophoneAsync()
    {
        AsyncOperationScope asyncOperation(m_asyncOperations);
        auto lifetime = get_strong();
        const auto generation = m_shutdown.generation();
        if (!acceptsUiWork(generation)) co_return;
        if (m_testingMicrophone) {
            stopMicrophoneTest();
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Informational);
            MicrophoneAccessInfo().Title(L"Microphone test stopped");
            MicrophoneAccessInfo().Message(L"");
            MicrophoneAccessInfo().IsOpen(true);
            co_return;
        }
        refreshMicrophoneAccessState();
        const auto access = DeviceAccessInformation::CreateFromDeviceClass(
            DeviceClass::AudioCapture).CurrentStatus();
        if (access == DeviceAccessStatus::DeniedByUser ||
            access == DeviceAccessStatus::DeniedBySystem) {
            co_return;
        }
        m_testingMicrophone = true;
        m_microphoneOpenTimer.Start();
        TestMicrophoneButton().Content(box_value(L"Stop Test"));
        MicrophoneAccessInfo().Severity(InfoBarSeverity::Informational);
        MicrophoneAccessInfo().Title(L"Opening microphone");
        MicrophoneAccessInfo().Message(L"Windows is opening the selected capture endpoint.");
        MicrophoneAccessInfo().IsOpen(true);
        try {
            AudioGraphSettings settings(Windows::Media::Render::AudioRenderCategory::Communications);
            settings.QuantumSizeSelectionMode(QuantumSizeSelectionMode::LowestLatency);
            const auto graphResult = co_await AudioGraph::CreateAsync(settings);
            if (!m_testingMicrophone || !acceptsUiWork(generation)) co_return;
            if (graphResult.Status() != AudioGraphCreationStatus::Success) {
                throw hresult_error(E_FAIL,
                    graphResult.Status() == AudioGraphCreationStatus::DeviceNotAvailable
                        ? L"The Windows audio engine is not available."
                        : graphResult.Status() == AudioGraphCreationStatus::FormatNotSupported
                            ? L"The selected audio format is not supported."
                            : L"Windows could not create a microphone test graph.");
            }
            m_microphoneTestGraph = graphResult.Graph();
            CreateAudioDeviceInputNodeResult inputResult{nullptr};
            if (m_microphoneInputId.empty()) {
                inputResult = co_await m_microphoneTestGraph.CreateDeviceInputNodeAsync(
                    MediaCategory::Communications);
            } else {
                const auto device = findCaptureDevice(m_microphoneInputId);
                if (!device) {
                    throw hresult_error(E_INVALIDARG,
                        L"The selected microphone disconnected. Refresh devices and choose another microphone.");
                }
                inputResult = co_await m_microphoneTestGraph.CreateDeviceInputNodeAsync(
                    MediaCategory::Communications, nullptr, device);
            }
            if (!m_testingMicrophone || !acceptsUiWork(generation)) co_return;
            if (inputResult.Status() != AudioDeviceNodeCreationStatus::Success) {
                const auto message = inputResult.Status() == AudioDeviceNodeCreationStatus::AccessDenied
                    ? L"Windows denied microphone access. Check Privacy & security > Microphone."
                    : inputResult.Status() == AudioDeviceNodeCreationStatus::DeviceNotAvailable
                        ? L"The selected microphone is disconnected or busy."
                        : inputResult.Status() == AudioDeviceNodeCreationStatus::FormatNotSupported
                            ? L"The selected microphone format is not supported."
                            : L"Windows could not open the selected microphone.";
                throw hresult_error(E_FAIL, message);
            }
            m_microphoneTestInputNode = inputResult.DeviceInputNode();
            m_microphoneTestFrameNode = m_microphoneTestGraph.CreateFrameOutputNode(
                m_microphoneTestInputNode.EncodingProperties());
            m_microphoneTestInputNode.AddOutgoingConnection(m_microphoneTestFrameNode);
            m_microphoneTestGraph.Start();
            m_microphoneOpenTimer.Stop();
            MicrophoneLevel().Value(0);
            MicrophoneLevel().Visibility(Visibility::Visible);
            m_microphoneLevelTimer.Start();
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Success);
            MicrophoneAccessInfo().Title(L"Microphone is working");
            MicrophoneAccessInfo().Message(L"The selected capture endpoint opened successfully. Speak to check the live level.");
            MicrophoneAccessInfo().IsOpen(true);
        } catch (hresult_error const& error) {
            if (!acceptsUiWork(generation)) co_return;
            stopMicrophoneTest();
            MicrophoneAccessInfo().Severity(InfoBarSeverity::Error);
            MicrophoneAccessInfo().Title(L"Microphone test failed");
            MicrophoneAccessInfo().Message(error.message());
            MicrophoneAccessInfo().IsOpen(true);
        }
    }

    void MainWindow::stopMicrophoneTest(bool updateUi)
    {
        m_testingMicrophone = false;
        if (m_microphoneOpenTimer) m_microphoneOpenTimer.Stop();
        if (m_microphoneLevelTimer) m_microphoneLevelTimer.Stop();
        if (m_microphoneTestGraph) {
            m_microphoneTestGraph.Stop();
            m_microphoneTestGraph.Close();
        }
        m_microphoneTestFrameNode = nullptr;
        m_microphoneTestInputNode = nullptr;
        m_microphoneTestGraph = nullptr;
        if (!updateUi) return;
        if (TestMicrophoneButton()) TestMicrophoneButton().Content(box_value(L"Test Microphone"));
        if (MicrophoneLevel()) {
            MicrophoneLevel().Value(0);
            MicrophoneLevel().Visibility(Visibility::Collapsed);
        }
    }

    void MainWindow::updateMicrophoneLevel()
    {
        if (!m_microphoneTestFrameNode) return;
        try {
            const auto frame = m_microphoneTestFrameNode.GetFrame();
            const auto buffer = frame.LockBuffer(Windows::Media::AudioBufferAccessMode::Read);
            const auto reference = buffer.CreateReference();
            std::uint8_t* bytes = nullptr;
            std::uint32_t capacity = 0;
            check_hresult(reference.as<winrt::impl::IMemoryBufferByteAccess>()->GetBuffer(
                &bytes, &capacity));
            const auto sampleCount = capacity / sizeof(float);
            const auto samples = reinterpret_cast<float const*>(bytes);
            double sum = 0.0;
            for (std::size_t index = 0; index < sampleCount; ++index) {
                const auto sample = std::clamp(static_cast<double>(samples[index]), -1.0, 1.0);
                sum += sample * sample;
            }
            const auto rms = sampleCount == 0 ? 0.0 : std::sqrt(sum / sampleCount);
            MicrophoneLevel().Value(std::clamp(rms * 4.0, 0.0, 1.0));
            frame.Close();
        } catch (...) {
            MicrophoneLevel().Value(0);
        }
    }

    fire_and_forget MainWindow::playClipAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip) co_return;
        const bool sourceExists = std::filesystem::exists(
            cuelet::windows::pathFromUtf8(clip->absolutePath));
        if (!sourceExists) clip->missing = true;
        if (clip->missing && sourceExists) clip->missing = false;
        if (!sourceExists) {
            showStatus(clip->storageMode == cuelet::SoundStorageMode::Linked
                ? L"The original linked file is missing. Use Locate Source File… from the sound menu."
                : L"This library file is missing. Restore it and rescan the library.",
                InfoBarSeverity::Warning);
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
            if (primaryIsBroadcast) {
                player.AudioCategory(MediaPlayerAudioCategory::Other);
                player.AudioDevice(broadcastDevice);
            }
            else if (playbackDevice) player.AudioDevice(playbackDevice);
            player.Volume(m_volume * m_soundboardVolume * (primaryIsBroadcast ? m_broadcastVolume : 1.0));
            player.Source(MediaSource::CreateFromStorageFile(file));
            MediaPlayer broadcastPlayer{nullptr};
            if (m_monitorLocally && broadcastDevice) {
                broadcastPlayer = MediaPlayer();
                broadcastPlayer.CommandManager().IsEnabled(false);
                broadcastPlayer.AudioCategory(
                    MediaPlayerAudioCategory::Other);
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
                        if (auto currentClip = current->findClip(clipId)) {
                            currentClip->durationSeconds = seconds;
                            currentClip->durationKnown = seconds >= 0.0;
                        }
                        current->refreshSounds(true);
                        current->indexSoundDurationsAsync();
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

    fire_and_forget MainWindow::playExternalFileAsync(std::filesystem::path path)
    {
        auto lifetime = get_strong();
        cuelet::windows::logDiagnostic(
            L"playback.external.start", path.wstring());
        try {
            if (!m_allowMultiple) stopAll();
            const auto file = co_await StorageFile::GetFileFromPathAsync(path.wstring());
            const auto token = m_nextPlaybackToken++;
            MediaPlayer player;
            player.CommandManager().IsEnabled(false);
            const auto playbackDevice = findRenderDevice(m_playbackOutputId);
            const auto broadcastDevice = findRenderDevice(m_broadcastOutputId);
            const bool primaryIsBroadcast = !m_monitorLocally && broadcastDevice;
            cuelet::windows::logDiagnostic(
                L"playback.external.route",
                L"monitor=" +
                    std::to_wstring(m_monitorLocally ? 1 : 0) +
                    L" broadcast=" +
                    std::to_wstring(broadcastDevice ? 1 : 0) +
                    L" primaryBroadcast=" +
                    std::to_wstring(primaryIsBroadcast ? 1 : 0));
            if (primaryIsBroadcast) {
                player.AudioCategory(MediaPlayerAudioCategory::Other);
                player.AudioDevice(broadcastDevice);
            }
            else if (playbackDevice) player.AudioDevice(playbackDevice);
            player.Volume(m_volume * m_soundboardVolume *
                          (primaryIsBroadcast ? m_broadcastVolume : 1.0));
            player.Source(MediaSource::CreateFromStorageFile(file));
            MediaPlayer broadcastPlayer{nullptr};
            if (m_monitorLocally && broadcastDevice) {
                broadcastPlayer = MediaPlayer();
                broadcastPlayer.CommandManager().IsEnabled(false);
                broadcastPlayer.AudioCategory(
                    MediaPlayerAudioCategory::Other);
                broadcastPlayer.AudioDevice(broadcastDevice);
                broadcastPlayer.Volume(m_volume * m_soundboardVolume * m_broadcastVolume);
                broadcastPlayer.Source(MediaSource::CreateFromStorageFile(file));
            }
            auto weak = get_weak();
            player.MediaEnded([weak, token](MediaPlayer const&, IInspectable const&) {
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak, token] {
                    if (auto current = weak.get()) current->stopPlayer(token);
                });
            });
            player.MediaFailed([weak, token](MediaPlayer const&, MediaPlayerFailedEventArgs const& args) {
                const auto message = args.ErrorMessage();
                cuelet::windows::logDiagnostic(
                    L"playback.external.failed", message.c_str());
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak, token, message] {
                    if (auto current = weak.get()) {
                        current->showStatus(L"Playback failed: " + std::wstring(message), InfoBarSeverity::Error);
                        current->stopPlayer(token);
                    }
                });
            });
            m_players.push_back({token, {}, player, broadcastPlayer});
            if (broadcastPlayer) broadcastPlayer.Play();
            player.Play();
            cuelet::windows::logDiagnostic(
                L"playback.external.play_called",
                L"token=" + std::to_wstring(token));
            updatePlaybackBar();
        } catch (hresult_error const& error) {
            cuelet::windows::logDiagnostic(
                L"playback.external.error", error.message().c_str());
            showStatus(L"Could not play this file: " + std::wstring(error.message()), InfoBarSeverity::Error);
        }
    }

    fire_and_forget MainWindow::testOutputDeviceAsync(
        std::string deviceId, bool shortTest)
    {
        AsyncOperationScope asyncOperation(m_asyncOperations);
        auto lifetime = get_strong();
        const auto generation = m_shutdown.generation();
        auto device = findRenderDevice(deviceId);
        if (!device && !deviceId.empty()) {
            try {
                device = co_await DeviceInformation::CreateFromIdAsync(
                    cuelet::windows::utf8ToHstring(deviceId));
            } catch (...) {
            }
        }
        if (!acceptsUiWork(generation)) co_return;
        const auto clip = std::find_if(m_clips.begin(), m_clips.end(), [](auto const& value) {
            return !value.missing;
        });
        if (!device || clip == m_clips.end()) {
            showStatus(L"No test sound or selected output device is available.", InfoBarSeverity::Warning);
            co_return;
        }
        try {
            const auto file = co_await StorageFile::GetFileFromPathAsync(
                cuelet::windows::utf8ToHstring(clip->absolutePath));
            const auto token = m_nextPlaybackToken++;
            MediaPlayer player;
            player.CommandManager().IsEnabled(false);
            player.AudioDevice(device);
            player.Volume(m_volume * m_soundboardVolume * m_broadcastVolume);
            player.Source(MediaSource::CreateFromStorageFile(file));
            auto weak = get_weak();
            player.MediaEnded([weak, token](MediaPlayer const&, IInspectable const&) {
                if (auto self = weak.get()) self->DispatcherQueue().TryEnqueue([weak, token] {
                    if (auto current = weak.get()) current->stopPlayer(token);
                });
            });
            m_players.push_back({token, clip->id, player, nullptr});
            player.Play();
            updatePlaybackBar();
            showStatus(L"Playing the test sound through the selected device.",
                       InfoBarSeverity::Informational);
            if (shortTest) {
                apartment_context uiThread;
                co_await resume_after(std::chrono::milliseconds(1500));
                co_await uiThread;
                if (acceptsUiWork(generation)) stopPlayer(token);
            }
        } catch (hresult_error const& error) {
            if (!acceptsUiWork(generation)) co_return;
            showStatus(L"Could not test the selected device: " + std::wstring(error.message()),
                       InfoBarSeverity::Error);
        }
    }

    void MainWindow::maybeRunAudioSetup()
    {
        if (!m_audioSetupCompleted && !m_audioSetupRunning && !m_loadingAudioDevices &&
            cuelet::windows::libraryStartupState(m_libraryFolder) ==
                cuelet::windows::LibraryStartupState::Ready) {
            runAudioSetupAsync();
        }
    }

    void MainWindow::updateVirtualDriverControls()
    {
#if !defined(_DEBUG)
        const bool cableConnected = std::any_of(
            m_renderDevices.begin(), m_renderDevices.end(),
            [&](auto const& render) {
                const auto renderDescriptor = describeEndpoint(render, false);
                if (cuelet::windows::classifyAudioEndpoint(renderDescriptor) !=
                    cuelet::windows::AudioEndpointKind::SupportedVirtualRender) {
                    return false;
                }
                return std::any_of(
                    m_captureDevices.begin(), m_captureDevices.end(),
                    [&](auto const& capture) {
                        const auto captureDescriptor =
                            describeEndpoint(capture, true);
                        return cuelet::windows::classifyAudioEndpoint(
                                   captureDescriptor) ==
                                   cuelet::windows::AudioEndpointKind::
                                       SupportedVirtualCapture &&
                               cuelet::windows::isCompatibleVirtualPair(
                                   renderDescriptor, captureDescriptor);
                    });
            });
        VirtualDriverInfo().Title(
            cableConnected
                ? L"VB-CABLE virtual microphone \u00b7 Connected"
                : L"VB-CABLE virtual microphone \u00b7 Not detected");
        VirtualDriverInfo().Severity(
            cableConnected ? InfoBarSeverity::Success
                           : InfoBarSeverity::Informational);
        VirtualDriverInfo().Message(
            cableConnected
                ? L"Cuelet found a matching CABLE Input/CABLE Output pair. "
                  L"Choose CABLE Output as the microphone in Discord, games, "
                  L"or recording apps."
                : L"Install VB-CABLE from VB-Audio, restart Windows, then "
                  L"refresh audio devices. Cuelet does not bundle or install "
                  L"the third-party driver.");
        VirtualDriverInfo().IsOpen(true);
        InstallVirtualDriverButton().Content(box_value(L"Get VB-CABLE"));
        InstallVirtualDriverButton().Visibility(
            cableConnected ? Visibility::Collapsed : Visibility::Visible);
        RepairVirtualDriverButton().Visibility(Visibility::Collapsed);
        UninstallVirtualDriverButton().Visibility(Visibility::Collapsed);
        RefreshVirtualDriverButton().Content(box_value(L"Refresh audio devices"));
        RefreshVirtualDriverButton().IsEnabled(true);
        VirtualDriverProgress().IsActive(false);
        VirtualDriverProgress().Visibility(Visibility::Collapsed);
        VirtualDriverDiagnostics().Visibility(Visibility::Collapsed);
        VirtualDriverTroubleshootingLink().Visibility(Visibility::Collapsed);
        return;
#else
        InstallVirtualDriverButton().Content(box_value(L"Install"));
        RefreshVirtualDriverButton().Content(box_value(L"Refresh"));
        const auto statusLabel =
            cuelet::windows::driverStatusLabel(m_virtualDriverStatus);
        VirtualDriverInfo().Title(
            L"Cuelet Virtual Microphone \u00b7 " + statusLabel);
        const bool installed = m_virtualDriverVerification.packageInstalled;
        const bool connected =
            m_virtualDriverStatus ==
            cuelet::windows::VirtualAudioDriverStatus::Connected;
        const bool failed =
            m_virtualDriverStatus ==
            cuelet::windows::VirtualAudioDriverStatus::InstallationFailed;
        const bool update =
            m_virtualDriverStatus ==
            cuelet::windows::VirtualAudioDriverStatus::UpdateAvailable;
        const bool repair =
            m_virtualDriverStatus ==
                cuelet::windows::VirtualAudioDriverStatus::RepairRequired ||
            m_virtualDriverStatus ==
                cuelet::windows::VirtualAudioDriverStatus::RestartRequired;

        if (m_driverActionRunning) {
            VirtualDriverInfo().Severity(InfoBarSeverity::Informational);
            VirtualDriverInfo().Message(
                L"Windows may request administrator permission. Cuelet will refresh audio devices when the helper finishes.");
        } else if (connected) {
            VirtualDriverInfo().Severity(InfoBarSeverity::Success);
#if defined(_DEBUG)
            const auto developmentNotice =
                m_virtualDriverVerification.signatureTrusted
                    ? std::wstring{}
                    : std::wstring(
                          L"\nDevelopment status: Windows accepted the "
                          L"test-signed Debug driver; it is not production trusted.");
#else
            const auto developmentNotice = std::wstring{};
#endif
            VirtualDriverInfo().Message(
                L"Render endpoint: Cuelet Virtual Microphone Input\n"
                L"Capture endpoint for Discord/games: Cuelet Virtual Microphone" +
                developmentNotice);
        } else if (repair || update) {
            VirtualDriverInfo().Severity(InfoBarSeverity::Warning);
            VirtualDriverInfo().Message(
                update
                    ? L"A newer signed Cuelet driver is bundled with this app."
                    : m_virtualDriverVerification.restartRequired
                    ? L"Windows must restart before Cuelet can verify both audio endpoints."
                    : L"The driver package is present, but both paired Cuelet endpoints were not verified.");
        } else if (failed) {
            VirtualDriverInfo().Severity(InfoBarSeverity::Error);
            VirtualDriverInfo().Message(
                L"Installation did not complete. Local playback remains available; expand diagnostics for details.");
        } else {
            VirtualDriverInfo().Severity(InfoBarSeverity::Informational);
            VirtualDriverInfo().Message(
                L"Install Cuelet's signed driver so Discord, games, and recording apps can receive your microphone mixed with soundboard audio.");
        }
        VirtualDriverInfo().IsOpen(true);

        InstallVirtualDriverButton().Visibility(
            !installed && !m_driverActionRunning ? Visibility::Visible
                                                 : Visibility::Collapsed);
        RepairVirtualDriverButton().Visibility(
            installed && !connected && !m_driverActionRunning
                ? Visibility::Visible : Visibility::Collapsed);
        RepairVirtualDriverButton().Content(
            box_value(update ? L"Update" : L"Repair"));
        UninstallVirtualDriverButton().Visibility(
            installed && !m_driverActionRunning ? Visibility::Visible
                                                : Visibility::Collapsed);
        RefreshVirtualDriverButton().IsEnabled(!m_driverActionRunning);
        VirtualDriverProgress().IsActive(m_driverActionRunning);
        VirtualDriverProgress().Visibility(
            m_driverActionRunning ? Visibility::Visible : Visibility::Collapsed);
        VirtualDriverDiagnostics().Visibility(
            m_virtualDriverDiagnostic.empty() ? Visibility::Collapsed
                                              : Visibility::Visible);
        VirtualDriverDiagnosticText().Text(m_virtualDriverDiagnostic);
        VirtualDriverTroubleshootingLink().Visibility(
            failed || repair ? Visibility::Visible : Visibility::Collapsed);
#endif
    }

    void MainWindow::applyVirtualDriverResult(JsonObject const& result)
    {
#if defined(_DEBUG)
        constexpr bool allowDevelopmentTestPackage = true;
#else
        constexpr bool allowDevelopmentTestPackage = false;
#endif
        const auto getBoolean = [&](wchar_t const* name) {
            return result.GetNamedBoolean(name, false);
        };
        const auto getString = [&](wchar_t const* name) -> std::wstring {
            return result.GetNamedString(name, L"").c_str();
        };
        m_virtualDriverVerification.packageInstalled =
            getBoolean(L"packageInstalled");
        m_virtualDriverVerification.signatureTrusted =
            getBoolean(L"signatureTrusted");
        m_virtualDriverVerification.renderEndpointPresent =
            getBoolean(L"renderEndpointPresent");
        m_virtualDriverVerification.captureEndpointPresent =
            getBoolean(L"captureEndpointPresent");
        m_virtualDriverVerification.endpointPairValid =
            getBoolean(L"endpointPairValid");
        m_virtualDriverVerification.restartRequired =
            getBoolean(L"restartRequired");
        m_virtualDriverVerification.updateAvailable =
            getBoolean(L"updateAvailable");

        const auto exitCode = static_cast<int>(
            result.GetNamedNumber(L"exitCode", ERROR_INVALID_DATA));
        const auto operation = getString(L"operation");
        const auto message = getString(L"message");
        const auto diagnostic = getString(L"diagnostic");
        const auto publishedInf = getString(L"publishedInf");
        const auto installedVersion = getString(L"installedVersion");
        const auto bundledVersion = getString(L"bundledVersion");
        const auto renderId = getString(L"renderEndpointId");
        const auto captureId = getString(L"captureEndpointId");
        std::wostringstream details;
        if (!message.empty()) details << message;
        if (!diagnostic.empty()) details << L"\n" << diagnostic;
        if (!publishedInf.empty()) details << L"\nPublished INF: " << publishedInf;
        if (!installedVersion.empty()) {
            details << L"\nInstalled version: " << installedVersion;
        }
        if (!bundledVersion.empty()) {
            details << L"\nBundled version: " << bundledVersion;
        }
        if (!renderId.empty()) details << L"\nRender endpoint ID: " << renderId;
        if (!captureId.empty()) details << L"\nCapture endpoint ID: " << captureId;
#if defined(_DEBUG)
        if (m_virtualDriverVerification.packageInstalled &&
            !m_virtualDriverVerification.signatureTrusted) {
            details << L"\nDevelopment status: test-signed Debug driver; "
                       L"not production trusted.";
        }
#endif
        m_virtualDriverDiagnostic = details.str();

        if (exitCode != 0) {
            m_virtualDriverStatus =
                cuelet::windows::VirtualAudioDriverStatus::InstallationFailed;
        } else {
            m_virtualDriverStatus =
                cuelet::windows::driverStatus(
                    m_virtualDriverVerification,
                    allowDevelopmentTestPackage);
        }
        if (exitCode == 0 &&
            operation != L"uninstall" &&
            cuelet::windows::isCompleteCueletEndpointPair(
                m_virtualDriverVerification,
                allowDevelopmentTestPackage)) {
            bool settingsChanged = false;
            if (operation == L"install" ||
                m_broadcastOutputId.empty() ||
                m_virtualCaptureId.empty()) {
                m_broadcastOutputId =
                    cuelet::windows::wideToUtf8(renderId);
                m_virtualCaptureId =
                    cuelet::windows::wideToUtf8(captureId);
                settingsChanged = true;
            }
            // An explicit install is part of the voice-routing setup flow.
            // A routine status refresh must never override the user's saved
            // physical-microphone privacy/mixing choice.
            if (operation == L"install" &&
                !m_mixPhysicalMicrophone) {
                m_mixPhysicalMicrophone = true;
                MixMicrophoneToggle().IsOn(true);
                settingsChanged = true;
            }
            if (settingsChanged) saveSettings();
        }
        updateVirtualDriverControls();
    }

    IAsyncOperation<bool> MainWindow::invokeVirtualDriverActionAsync(
        std::wstring operation, bool confirm)
    {
        AsyncOperationScope asyncOperation(m_asyncOperations);
        const auto generation = m_shutdown.generation();
        if (!acceptsUiWork(generation) || m_driverActionRunning) co_return false;
        if (confirm) {
            ContentDialog dialog;
            dialog.XamlRoot(RootGrid().XamlRoot());
            const bool uninstalling = operation == L"uninstall";
            dialog.Title(box_value(
                uninstalling
                    ? L"Remove Cuelet Virtual Microphone?"
                    : L"Install Cuelet Virtual Microphone?"));
            dialog.PrimaryButtonText(
                uninstalling ? L"Uninstall" : L"Continue");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(ContentDialogButton::Primary);
            TextBlock explanation;
            explanation.Text(
                uninstalling
                    ? L"Cuelet will stop using its virtual endpoints and Windows will request administrator permission to remove only the Cuelet-owned device and driver package."
                    : L"Windows will request administrator permission. Cuelet will install an audio-device driver. You can remove it later from Cuelet Settings.");
            explanation.TextWrapping(TextWrapping::Wrap);
            dialog.Content(explanation);
            if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary ||
                !acceptsUiWork(generation)) {
                co_return false;
            }
        }

        const bool statusOnly = operation == L"status";
        if (!statusOnly) {
            m_driverActionRunning = true;
            m_virtualDriverStatus =
                cuelet::windows::VirtualAudioDriverStatus::Installing;
            m_virtualDriverDiagnostic.clear();
            updateVirtualDriverControls();
            cuelet::windows::logDiagnostic(L"driver_installer.start", operation);
        }
        auto cancellation = std::make_shared<std::atomic_bool>(false);
        m_driverCancellation = cancellation;
        const auto json = co_await cuelet::windows::VirtualAudioInstallerClient::runAsync(
            operation, !statusOnly, cancellation);
        if (m_driverCancellation == cancellation) {
            m_driverCancellation.reset();
        }
        if (!acceptsUiWork(generation)) co_return false;
        m_driverActionRunning = false;
        try {
            const auto result = JsonObject::Parse(json);
            applyVirtualDriverResult(result);
            const auto exitCode = static_cast<int>(
                result.GetNamedNumber(L"exitCode", ERROR_INVALID_DATA));
            cuelet::windows::logDiagnostic(
                L"driver_installer.complete",
                operation + L" exitCode=" + std::to_wstring(exitCode));
            if (exitCode == 0 && !statusOnly) {
                initializeAudioRoutingAsync();
            }
            co_return exitCode == 0 &&
                (operation == L"uninstall" ||
                 cuelet::windows::isCompleteCueletEndpointPair(
                     m_virtualDriverVerification,
#if defined(_DEBUG)
                     true
#else
                     false
#endif
                     ));
        } catch (hresult_error const& error) {
            m_virtualDriverStatus =
                cuelet::windows::VirtualAudioDriverStatus::InstallationFailed;
            m_virtualDriverDiagnostic =
                L"Installer result parsing failed: " +
                std::wstring(error.message());
            updateVirtualDriverControls();
            cuelet::windows::logDiagnostic(
                L"driver_installer.result_error", error.message().c_str());
            co_return false;
        }
    }

    fire_and_forget MainWindow::refreshVirtualDriverStatusAsync()
    {
        auto lifetime = get_strong();
#if defined(_DEBUG)
        co_await invokeVirtualDriverActionAsync(L"status", false);
#else
        updateVirtualDriverControls();
        co_return;
#endif
    }

    fire_and_forget MainWindow::runVirtualDriverActionAsync(std::wstring operation)
    {
        auto lifetime = get_strong();
        co_await invokeVirtualDriverActionAsync(std::move(operation), true);
    }

    fire_and_forget MainWindow::runAudioSetupAsync()
    {
        AsyncOperationScope asyncOperation(m_asyncOperations);
        auto lifetime = get_strong();
        const auto generation = m_shutdown.generation();
        if (!acceptsUiWork(generation)) co_return;
        if (m_audioSetupRunning) co_return;
        m_audioSetupRunning = true;
        struct RunningGuard {
            bool& value;
            ~RunningGuard() { value = false; }
        } finish{m_audioSetupRunning};
        try {
            ContentDialog intentDialog;
            intentDialog.XamlRoot(RootGrid().XamlRoot());
            intentDialog.Title(box_value(L"Audio Setup · Intended Use"));
            intentDialog.PrimaryButtonText(L"Continue");
            intentDialog.CloseButtonText(L"Cancel");
            intentDialog.DefaultButton(ContentDialogButton::Primary);
            StackPanel intent;
            intent.Spacing(10);
            TextBlock question;
            question.Text(L"How do you want to use Cuelet?");
            question.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            intent.Children().Append(question);
            RadioButton localOnly;
            localOnly.Content(box_value(L"Play sounds on this PC"));
            localOnly.GroupName(L"AudioIntent");
            RadioButton voiceOnly;
            voiceOnly.Content(box_value(L"Use Cuelet in voice chats and games"));
            voiceOnly.GroupName(L"AudioIntent");
            RadioButton both;
            both.Content(box_value(L"Both"));
            both.GroupName(L"AudioIntent");
            if (m_broadcastOutputId.empty()) localOnly.IsChecked(true); else both.IsChecked(true);
            intent.Children().Append(localOnly);
            intent.Children().Append(voiceOnly);
            intent.Children().Append(both);
            intentDialog.Content(intent);
            if (co_await showDialogAsync(intentDialog) != ContentDialogResult::Primary ||
                !acceptsUiWork(generation)) co_return;
            bool wantsVoice = voiceOnly.IsChecked().GetBoolean() || both.IsChecked().GetBoolean();
            const bool wantsLocal = localOnly.IsChecked().GetBoolean() || both.IsChecked().GetBoolean();

            std::string selectedBroadcastId;
            std::string selectedVirtualCaptureId;
            std::wstring selectedVirtualCaptureName;
            bool cueletDriverInstalledDuringSetup = false;
            if (wantsVoice) {
                bool advancedManualPairing = false;
                const auto renderDescriptors = [&] {
                    std::vector<cuelet::windows::AudioEndpointDescriptor> result;
                    for (auto const& device : m_renderDevices) {
                        result.push_back(describeEndpoint(device, false));
                    }
                    return result;
                }();
                const auto captureDescriptors = [&] {
                    std::vector<cuelet::windows::AudioEndpointDescriptor> result;
                    for (auto const& device : m_captureDevices) {
                        result.push_back(describeEndpoint(device, true));
                    }
                    return result;
                }();
                std::vector<std::pair<std::size_t, std::size_t>> compatiblePairs;
                for (std::size_t renderIndex = 0;
                     renderIndex < renderDescriptors.size(); ++renderIndex) {
                    if (const auto captureIndex =
                            cuelet::windows::findBestVirtualCapture(
                                renderDescriptors[renderIndex], captureDescriptors)) {
                        compatiblePairs.emplace_back(renderIndex, *captureIndex);
                    }
                }
                const auto cueletPair = std::find_if(
                    compatiblePairs.begin(), compatiblePairs.end(),
                    [&](auto const& pair) {
                        return cuelet::windows::isCueletVirtualEndpoint(
                                   renderDescriptors[pair.first]) &&
                               cuelet::windows::isCueletVirtualEndpoint(
                                   captureDescriptors[pair.second]);
                    });

                if (cueletPair == compatiblePairs.end() &&
                    compatiblePairs.empty()) {
                    ContentDialog setupDialog;
                    setupDialog.XamlRoot(RootGrid().XamlRoot());
#if defined(_DEBUG)
                    setupDialog.Title(box_value(L"Set up Cuelet Virtual Microphone"));
                    setupDialog.PrimaryButtonText(L"Install Virtual Microphone");
#else
                    setupDialog.Title(box_value(L"Set up VB-CABLE"));
                    setupDialog.PrimaryButtonText(L"Open VB-CABLE Website");
#endif
                    setupDialog.SecondaryButtonText(L"Continue with Local Playback Only");
                    setupDialog.CloseButtonText(L"Cancel");
                    setupDialog.DefaultButton(ContentDialogButton::Primary);
                    StackPanel setupContent;
                    setupContent.Spacing(10);
                    TextBlock setupExplanation;
#if defined(_DEBUG)
                    setupExplanation.Text(
                        L"Cuelet can install a virtual microphone so Discord, games, and recording apps can receive your microphone mixed with soundboard audio.");
#else
                    setupExplanation.Text(
                        L"Cuelet works with the VB-CABLE virtual audio driver. "
                        L"Install it separately from VB-Audio, restart Windows, "
                        L"then run Audio Setup again. Cuelet does not download, "
                        L"bundle, or install this third-party driver.");
#endif
                    setupExplanation.TextWrapping(TextWrapping::Wrap);
                    setupContent.Children().Append(setupExplanation);
                    Button advanced;
                    advanced.Content(box_value(
                        L"Advanced: Use Existing Virtual Audio Device"));
                    advanced.HorizontalAlignment(HorizontalAlignment::Left);
                    advanced.Click([&](IInspectable const&, RoutedEventArgs const&) {
                        advancedManualPairing = true;
                        setupDialog.Hide();
                    });
                    setupContent.Children().Append(advanced);
                    setupDialog.Content(setupContent);
                    const auto setupResult = co_await showDialogAsync(setupDialog);
                    if (!acceptsUiWork(generation)) co_return;
                    if (setupResult == ContentDialogResult::Primary) {
#if defined(_DEBUG)
                        const bool installed = co_await invokeVirtualDriverActionAsync(
                            L"install", true);
                        if (!acceptsUiWork(generation)) co_return;
                        if (!installed) {
                            ContentDialog failedDialog;
                            failedDialog.XamlRoot(RootGrid().XamlRoot());
                            failedDialog.Title(box_value(
                                L"Virtual microphone installation failed"));
                            failedDialog.PrimaryButtonText(
                                L"Continue with Local Playback Only");
                            failedDialog.CloseButtonText(L"Cancel");
                            StackPanel failedContent;
                            failedContent.Spacing(8);
                            TextBlock failedMessage;
                            failedMessage.Text(
                                L"Cuelet could not verify a trusted driver package and both paired endpoints. Local playback is unchanged.");
                            failedMessage.TextWrapping(TextWrapping::Wrap);
                            failedContent.Children().Append(failedMessage);
                            HyperlinkButton guide;
                            guide.Content(box_value(L"Open troubleshooting guide"));
                            guide.NavigateUri(Uri(
                                L"https://github.com/okixk/cuelet/blob/main/apps/windows/docs/VIRTUAL_AUDIO_DRIVER.md"));
                            failedContent.Children().Append(guide);
                            failedDialog.Content(failedContent);
                            if (co_await showDialogAsync(failedDialog) !=
                                    ContentDialogResult::Primary ||
                                !acceptsUiWork(generation)) {
                                co_return;
                            }
                            wantsVoice = false;
                        } else {
                            cueletDriverInstalledDuringSetup = true;
                            selectedBroadcastId = m_broadcastOutputId;
                            selectedVirtualCaptureId = m_virtualCaptureId;
                            selectedVirtualCaptureName =
                                L"Cuelet Virtual Microphone";
                        }
#else
                        Launcher::LaunchUriAsync(Uri(L"https://vb-audio.com/Cable/"));
                        wantsVoice = false;
#endif
                    } else if (setupResult == ContentDialogResult::Secondary) {
                        wantsVoice = false;
                    } else if (!advancedManualPairing) {
                        co_return;
                    }
                }

                if (wantsVoice && selectedBroadcastId.empty() &&
                    cueletPair != compatiblePairs.end()) {
                    selectedBroadcastId =
                        renderDescriptors[cueletPair->first].id;
                    selectedVirtualCaptureId =
                        captureDescriptors[cueletPair->second].id;
                    selectedVirtualCaptureName =
                        captureDescriptors[cueletPair->second].name;
                }

                if (wantsVoice && selectedBroadcastId.empty()) {
                ContentDialog virtualDialog;
                virtualDialog.XamlRoot(RootGrid().XamlRoot());
                virtualDialog.Title(box_value(
                    advancedManualPairing
                        ? L"Advanced: Use Existing Virtual Audio Device"
                        : L"Audio Setup · Compatible Virtual Audio Device"));
                virtualDialog.PrimaryButtonText(L"Continue");
                virtualDialog.CloseButtonText(L"Cancel");
                StackPanel content;
                content.Spacing(10);
                TextBlock explanation;
                explanation.Text(
                    advancedManualPairing
                        ? L"Manual pairing does not turn ordinary speakers or microphones into a virtual cable. Select endpoints only if you know the render stream is transferred to the capture endpoint."
                        : L"Only Cuelet-owned endpoints or deliberately supported virtual-cable pairs are shown.");
                explanation.TextWrapping(TextWrapping::Wrap);
                content.Children().Append(explanation);
                ComboBox devices;
                devices.Header(box_value(
                    advancedManualPairing
                        ? L"Manual render endpoint"
                        : L"Compatible virtual render endpoint"));
                devices.HorizontalAlignment(HorizontalAlignment::Stretch);
                int selectedIndex = -1;
                std::vector<std::size_t> shownRenderIndices;
                for (std::size_t index = 0; index < m_renderDevices.size(); ++index) {
                    const auto& device = m_renderDevices[index];
                    const bool show = advancedManualPairing ||
                        std::any_of(
                            compatiblePairs.begin(), compatiblePairs.end(),
                            [&](auto const& pair) { return pair.first == index; });
                    if (!show) continue;
                    ComboBoxItem item;
                    item.Content(box_value(device.Name()));
                    item.Tag(box_value(device.Id()));
                    devices.Items().Append(item);
                    shownRenderIndices.push_back(index);
                    if (cuelet::windows::hstringToUtf8(device.Id()) == m_broadcastOutputId) {
                        selectedIndex = static_cast<int>(devices.Items().Size()) - 1;
                    }
                }
                if (selectedIndex < 0 && !shownRenderIndices.empty()) selectedIndex = 0;
                devices.SelectedIndex(selectedIndex);
                content.Children().Append(devices);
                ComboBox pairedCapture;
                pairedCapture.Header(box_value(
                    advancedManualPairing
                        ? L"Manual capture endpoint"
                        : L"Matching microphone endpoint for Discord/games"));
                pairedCapture.HorizontalAlignment(HorizontalAlignment::Stretch);
                const auto selectBestPair = [&, devices, pairedCapture]() {
                    pairedCapture.Items().Clear();
                    const auto selected = devices.SelectedItem().try_as<ComboBoxItem>();
                    if (!selected) return;
                    const auto renderId = cuelet::windows::hstringToUtf8(
                        unbox_value_or<hstring>(selected.Tag(), L""));
                    const auto render = std::find_if(
                        renderDescriptors.begin(), renderDescriptors.end(),
                        [&](auto const& descriptor) {
                            return descriptor.id == renderId;
                        });
                    if (render == renderDescriptors.end()) return;
                    for (std::size_t index = 0;
                         index < captureDescriptors.size(); ++index) {
                        if (!advancedManualPairing &&
                            !cuelet::windows::isCompatibleVirtualPair(
                                *render, captureDescriptors[index])) {
                            continue;
                        }
                        ComboBoxItem item;
                        item.Content(box_value(captureDescriptors[index].name));
                        item.Tag(box_value(cuelet::windows::utf8ToHstring(
                            captureDescriptors[index].id)));
                        pairedCapture.Items().Append(item);
                    }
                    if (pairedCapture.Items().Size() > 0) {
                        pairedCapture.SelectedIndex(0);
                    }
                };
                selectBestPair();
                devices.SelectionChanged([selectBestPair](
                    IInspectable const&, SelectionChangedEventArgs const&) {
                    selectBestPair();
                });
                content.Children().Append(pairedCapture);
                Button testDevice;
                testDevice.Content(box_value(L"Test Selected Device"));
                const bool hasTestSound = std::any_of(m_clips.begin(), m_clips.end(), [](auto const& clip) {
                    return !clip.missing;
                });
                testDevice.IsEnabled(devices.SelectedIndex() >= 0 && hasTestSound);
                devices.SelectionChanged([testDevice, hasTestSound](
                    IInspectable const& sender, SelectionChangedEventArgs const&) {
                    const auto combo = sender.try_as<ComboBox>();
                    testDevice.IsEnabled(combo && combo.SelectedIndex() >= 0 && hasTestSound);
                });
                testDevice.Click([weak = get_weak(), devices](IInspectable const&, RoutedEventArgs const&) {
                    if (auto self = weak.get()) {
                        if (auto selected = devices.SelectedItem().try_as<ComboBoxItem>()) {
                            self->testOutputDeviceAsync(cuelet::windows::hstringToUtf8(
                                unbox_value_or<hstring>(selected.Tag(), L"")));
                        }
                    }
                });
                content.Children().Append(testDevice);
                Button refreshDevices;
                refreshDevices.Content(box_value(L"Refresh Devices"));
                refreshDevices.Click([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
                    if (auto self = weak.get()) self->initializeAudioRoutingAsync();
                });
                content.Children().Append(refreshDevices);
                virtualDialog.Content(content);
                if (co_await showDialogAsync(virtualDialog) != ContentDialogResult::Primary ||
                    !acceptsUiWork(generation)) co_return;
                if (auto selected = devices.SelectedItem().try_as<ComboBoxItem>()) {
                    selectedBroadcastId = cuelet::windows::hstringToUtf8(
                        unbox_value_or<hstring>(selected.Tag(), L""));
                }
                if (auto selected = pairedCapture.SelectedItem().try_as<ComboBoxItem>()) {
                    selectedVirtualCaptureId = cuelet::windows::hstringToUtf8(
                        unbox_value_or<hstring>(selected.Tag(), L""));
                    selectedVirtualCaptureName = unbox_value_or<hstring>(
                        selected.Content(), L"").c_str();
                }
                }
            }

            ContentDialog microphoneDialog;
            microphoneDialog.XamlRoot(RootGrid().XamlRoot());
            microphoneDialog.Title(box_value(L"Audio Setup · Microphone"));
            microphoneDialog.PrimaryButtonText(L"Continue");
            microphoneDialog.CloseButtonText(L"Cancel");
            StackPanel microphoneContent;
            microphoneContent.Spacing(10);
            TextBlock micQuestion;
            micQuestion.Text(L"Select your normal physical microphone.");
            micQuestion.TextWrapping(TextWrapping::Wrap);
            microphoneContent.Children().Append(micQuestion);
            ComboBox microphones;
            microphones.Header(box_value(L"Physical microphone"));
            microphones.HorizontalAlignment(HorizontalAlignment::Stretch);
            ComboBoxItem defaultMic;
            defaultMic.Content(box_value(L"System default"));
            defaultMic.Tag(box_value(L""));
            microphones.Items().Append(defaultMic);
            int microphoneIndex = 0;
            for (auto const& device : m_captureDevices) {
                if (!cuelet::windows::isPhysicalMicrophone(
                        describeEndpoint(device, true))) {
                    continue;
                }
                ComboBoxItem item;
                item.Content(box_value(device.Name()));
                item.Tag(box_value(device.Id()));
                microphones.Items().Append(item);
                if (cuelet::windows::hstringToUtf8(device.Id()) == m_microphoneInputId) {
                    microphoneIndex = static_cast<int>(microphones.Items().Size()) - 1;
                }
            }
            microphones.SelectedIndex(microphoneIndex);
            microphoneContent.Children().Append(microphones);
            CheckBox mix;
            mix.Content(box_value(L"Mix my microphone with Cuelet sounds"));
            mix.IsChecked(true);
            mix.IsEnabled(wantsVoice);
            microphoneContent.Children().Append(mix);
            microphoneDialog.Content(microphoneContent);
            if (co_await showDialogAsync(microphoneDialog) != ContentDialogResult::Primary ||
                !acceptsUiWork(generation)) co_return;
            std::string selectedMicrophoneId;
            if (auto selected = microphones.SelectedItem().try_as<ComboBoxItem>()) {
                selectedMicrophoneId = cuelet::windows::hstringToUtf8(
                    unbox_value_or<hstring>(selected.Tag(), L""));
            }

            ContentDialog monitorDialog;
            monitorDialog.XamlRoot(RootGrid().XamlRoot());
            monitorDialog.Title(box_value(L"Audio Setup · Local Monitoring"));
            monitorDialog.PrimaryButtonText(L"Continue");
            monitorDialog.CloseButtonText(L"Cancel");
            StackPanel monitorContent;
            monitorContent.Spacing(10);
            TextBlock monitorQuestion;
            monitorQuestion.Text(L"Hear Cuelet through your speakers or headphones too?");
            monitorQuestion.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            monitorContent.Children().Append(monitorQuestion);
            RadioButton monitorYes;
            monitorYes.Content(box_value(L"Yes"));
            monitorYes.GroupName(L"MonitorChoice");
            monitorYes.IsChecked(wantsLocal);
            RadioButton monitorNo;
            monitorNo.Content(box_value(L"No"));
            monitorNo.GroupName(L"MonitorChoice");
            monitorNo.IsChecked(!wantsLocal);
            monitorContent.Children().Append(monitorYes);
            monitorContent.Children().Append(monitorNo);
            monitorDialog.Content(monitorContent);
            if (co_await showDialogAsync(monitorDialog) != ContentDialogResult::Primary ||
                !acceptsUiWork(generation)) co_return;

            const auto selectById = [](ComboBox const& combo, std::string const& id) {
                for (std::uint32_t index = 0; index < combo.Items().Size(); ++index) {
                    if (auto item = combo.Items().GetAt(index).try_as<ComboBoxItem>()) {
                        if (cuelet::windows::hstringToUtf8(
                                unbox_value_or<hstring>(item.Tag(), L"")) == id) {
                            combo.SelectedIndex(static_cast<int>(index));
                            return;
                        }
                    }
                }
                combo.SelectedIndex(0);
            };
            selectById(BroadcastOutputCombo(), selectedBroadcastId);
            selectById(VirtualCaptureCombo(), selectedVirtualCaptureId);
            selectById(MicrophoneInputCombo(), selectedMicrophoneId);
            MonitorLocallyToggle().IsOn(monitorYes.IsChecked().GetBoolean());
            MixMicrophoneToggle().IsOn(wantsVoice && mix.IsChecked().GetBoolean());
            audioRoutingChanged();
            if (wantsVoice && !selectedBroadcastId.empty()) {
                // A just-installed endpoint may not yet exist in the ComboBox
                // snapshot that predates installation. Preserve the verified
                // helper IDs, then let the refresh select them when Windows
                // enumeration catches up.
                m_broadcastOutputId = selectedBroadcastId;
                m_virtualCaptureId = selectedVirtualCaptureId;
                m_mixPhysicalMicrophone = mix.IsChecked().GetBoolean();
                saveSettings();
                initializeAudioRoutingAsync();
            }
            if (cueletDriverInstalledDuringSetup) {
                testOutputDeviceAsync(selectedBroadcastId, true);
            }

            ContentDialog testDialog;
            testDialog.XamlRoot(RootGrid().XamlRoot());
            testDialog.Title(box_value(L"Audio Setup · Test"));
            testDialog.PrimaryButtonText(L"Finish");
            StackPanel testContent;
            testContent.Spacing(10);
            InfoBar outputStatus;
            outputStatus.IsOpen(true);
            outputStatus.IsClosable(false);
            outputStatus.Severity(wantsVoice && selectedBroadcastId.empty()
                ? InfoBarSeverity::Warning : InfoBarSeverity::Success);
            outputStatus.Title(wantsVoice && selectedBroadcastId.empty()
                ? L"Voice-chat output is not configured" : L"Audio choices saved");
            outputStatus.Message(wantsVoice && selectedBroadcastId.empty()
                ? L"Local playback will work, but an installed virtual audio endpoint is still required for voice-chat microphone output."
                : L"Broadcast and local-monitor choices are ready to test.");
            testContent.Children().Append(outputStatus);
            TextBlock microphoneStatus;
            microphoneStatus.Text(m_mixPhysicalMicrophone
                ? (m_microphoneGraph ? L"Microphone test: device opened successfully."
                                     : L"Microphone test: opening the selected device…")
                : L"Microphone mixing is off.");
            microphoneStatus.TextWrapping(TextWrapping::Wrap);
            testContent.Children().Append(microphoneStatus);
            Button testMicrophone;
            testMicrophone.Content(box_value(L"Test Microphone Access"));
            testMicrophone.IsEnabled(m_mixPhysicalMicrophone);
            testMicrophone.Click([weak = get_weak(), microphoneStatus](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) {
                    self->testMicrophoneAsync();
                    microphoneStatus.Text(L"Microphone test status is shown in Settings under Audio routing.");
                }
            });
            testContent.Children().Append(testMicrophone);
            Button testSound;
            testSound.Content(box_value(L"Play Test Sound"));
            const auto playable = std::find_if(m_clips.begin(), m_clips.end(), [](auto const& clip) {
                return !clip.missing;
            });
            testSound.IsEnabled(playable != m_clips.end());
            const auto testClipId = playable == m_clips.end() ? std::string{} : playable->id;
            testSound.Click([weak = get_weak(), testClipId](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get()) if (!testClipId.empty()) self->playClipAsync(testClipId);
            });
            testContent.Children().Append(testSound);
            TextBlock routeSummary;
            routeSummary.Text((selectedBroadcastId.empty()
                    ? L"Voice-chat output: Off"
                    : L"Voice-chat output: Selected render endpoint") +
                std::wstring(selectedVirtualCaptureName.empty()
                    ? L"\nRecording endpoint: Not selected"
                    : L"\nSelect in Discord/games: " + selectedVirtualCaptureName) +
                std::wstring(monitorYes.IsChecked().GetBoolean()
                    ? L"\nLocal monitor: On" : L"\nLocal monitor: Off"));
            testContent.Children().Append(routeSummary);
            testDialog.Content(testContent);
            if (co_await showDialogAsync(testDialog) != ContentDialogResult::Primary ||
                !acceptsUiWork(generation)) co_return;
            m_audioSetupCompleted = true;
            saveSettings();
            showStatus(L"Audio setup completed.", InfoBarSeverity::Success);
        } catch (hresult_error const& error) {
            if (!acceptsUiWork(generation)) co_return;
            showStatus(L"Audio setup could not be completed: " + std::wstring(error.message()),
                       InfoBarSeverity::Error);
        }
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
        TextBlock globalExplanation;
        globalExplanation.Text(L"Shortcuts are global and remain active while Cuelet is hidden.");
        globalExplanation.Foreground(themeBrush(L"TextFillColorSecondaryBrush"));
        globalExplanation.TextWrapping(TextWrapping::Wrap);
        form.Children().Append(globalExplanation);

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
            pending->global = true;
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
            recorded.global = true;
            recorded = cuelet::windows::normalizeShortcut(recorded);
            recorded.label = cuelet::windows::wideToUtf8(cuelet::windows::formatShortcut(recorded));
            pending = recorded;
            replace.IsChecked(false);
            validate();
        });
        replace.Checked([&](IInspectable const&, RoutedEventArgs const&) { validate(); });
        replace.Unchecked([&](IInspectable const&, RoutedEventArgs const&) { validate(); });
        dialog.Opened([&](ContentDialog const&, ContentDialogOpenedEventArgs const&) {
            recorder.Focus(FocusState::Programmatic);
        });
        dialog.Content(form);
        validate();

        const auto result = co_await showDialogAsync(dialog);
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
        const bool linked = clip->storageMode == cuelet::SoundStorageMode::Linked;
        const std::filesystem::path oldPath = cuelet::windows::pathFromUtf8(clip->absolutePath);
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(L"Rename sound"));
        dialog.PrimaryButtonText(L"Rename");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.MinWidth(categoryDialogWidth);
        dialog.MaxWidth(categoryDialogWidth);
        dialog.HorizontalAlignment(HorizontalAlignment::Center);
        TextBox name;
        name.Header(box_value(L"Name"));
        name.Width(categoryDialogContentWidth);
        name.MaxWidth(categoryDialogContentWidth);
        name.MaxLength(soundFileNameMaxLength);
        name.Text(oldPath.stem().wstring());
        name.SelectAll();
        StackPanel content;
        content.Spacing(10);
        if (linked) {
            InfoBar warning;
            warning.IsOpen(true);
            warning.IsClosable(false);
            warning.Severity(InfoBarSeverity::Warning);
            warning.Title(L"This is a linked sound");
            warning.Message(L"This sound is linked to its original file. Renaming it will also rename the original file.");
            content.Children().Append(warning);
        }
        content.Children().Append(name);
        dialog.Content(content);
        if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;
        const auto trimmed = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text()));
        if (trimmed.empty() || trimmed.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            showStatus(L"Enter a valid Windows file name.", InfoBarSeverity::Warning);
            co_return;
        }
        const auto newPath = cuelet::windows::renamedSoundPath(
            oldPath, cuelet::windows::utf8ToWide(trimmed));
        if (std::filesystem::exists(newPath)) {
            showStatus(L"A file with that name already exists.", InfoBarSeverity::Warning);
            co_return;
        }
        const auto oldClips = m_clips;
        const auto oldMetadata = m_metadata;
        stopClip(clipId);
        std::string transactionError;
        const bool renamed = cuelet::windows::renameFileTransaction(
            oldPath, newPath, [&]() {
                auto current = findClip(clipId);
                if (!current) return false;
                cuelet::windows::applyRenamedSoundMetadata(
                    *current, newPath, m_libraryFolder);
                return saveMetadata(false);
            }, &transactionError);
        if (!renamed) {
            m_clips = oldClips;
            m_metadata = oldMetadata;
            refreshSounds(true);
            showStatus(L"Could not rename the sound: " +
                cuelet::windows::utf8ToWide(transactionError), InfoBarSeverity::Error);
            co_return;
        }
        m_selectedClipId = clipId;
        refreshSounds(true);
        indexSoundDurationsAsync();
        showStatus(L"Sound renamed.", InfoBarSeverity::Success);
    }

    fire_and_forget MainWindow::locateLinkedSourceAsync(std::string clipId)
    {
        auto lifetime = get_strong();
        auto clip = findClip(clipId);
        if (!clip || clip->storageMode != cuelet::SoundStorageMode::Linked) co_return;
        try {
            FileOpenPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::MusicLibrary);
            for (auto const& extension : cuelet::LibraryScanner::supportedExtensions()) {
                picker.FileTypeFilter().Append(L"." + cuelet::windows::utf8ToHstring(extension));
            }
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_hwnd));
            const auto file = co_await picker.PickSingleFileAsync();
            if (!file) co_return;
            const auto selected = std::filesystem::path(file.Path().c_str());
            for (auto const& other : m_clips) {
                if (other.id == clipId) continue;
                if (cuelet::windows::pathsReferToSameFile(
                        selected, cuelet::windows::pathFromUtf8(other.absolutePath))) {
                    showStatus(L"That file is already used by another Cuelet sound.", InfoBarSeverity::Warning);
                    co_return;
                }
            }
            const auto oldClips = m_clips;
            const auto oldMetadata = m_metadata;
            clip = findClip(clipId);
            clip->externalPath = cuelet::windows::pathToUtf8(selected);
            clip->absolutePath = clip->externalPath;
            clip->originalSourcePath = clip->externalPath;
            clip->filename = cuelet::windows::pathToUtf8(selected.filename());
            clip->sourceFileName = clip->filename;
            clip->missing = false;
            clip->durationKnown = false;
            clip->durationSourcePath.clear();
            if (!saveMetadata(false)) {
                m_clips = oldClips;
                m_metadata = oldMetadata;
                showStatus(L"Could not update the linked source. The previous reference was preserved.",
                           InfoBarSeverity::Error);
                co_return;
            }
            refreshSounds(true);
            indexSoundDurationsAsync();
            showStatus(L"Linked source updated.", InfoBarSeverity::Success);
        } catch (hresult_error const& error) {
            showStatus(L"Could not locate the linked source: " + std::wstring(error.message()),
                       InfoBarSeverity::Error);
        }
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
        const bool allLinked = std::all_of(clipIds.begin(), clipIds.end(), [&](auto const& id) {
            const auto current = findClip(id);
            return current && current->storageMode == cuelet::SoundStorageMode::Linked;
        });
        dialog.Content(box_value(allLinked
            ? L"The Cuelet entries will be removed. Original external files will not be deleted."
            : clipIds.size() == 1 && clip->missing
                ? L"The missing metadata entry will be removed."
                : L"Managed audio files will be permanently deleted from the library folder. Linked originals will remain untouched."));
        if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;
        for (auto const& id : clipIds) stopClip(id);
        std::vector<std::string> removed;
        for (auto const& id : clipIds) {
            clip = findClip(id);
            if (!clip) continue;
            if (!clip->missing && clip->storageMode == cuelet::SoundStorageMode::Managed) {
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
        StackPanel form;
        form.Spacing(12);
        setFixedDialogWidth(dialog, form, categoryDialogWidth, categoryDialogContentWidth);
        TextBox name;
        name.Header(box_value(L"Category name"));
        name.PlaceholderText(L"Category name");
        name.MaxLength(categoryNameMaxLength);
        name.Text(cuelet::windows::utf8ToHstring(original.name));
        form.Children().Append(name);

        Border preview;
        preview.MinHeight(42);
        preview.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        preview.BorderThickness(ThicknessHelper::FromUniformLength(1));
        preview.BorderBrush(themeBrush(L"CardStrokeColorDefaultBrush"));
        preview.Background(themeBrush(L"CardBackgroundFillColorDefaultBrush"));
        preview.Padding(ThicknessHelper::FromUniformLength(10));
        Grid previewContent;
        previewContent.ColumnSpacing(9);
        previewContent.ColumnDefinitions().Append(ColumnDefinition());
        previewContent.ColumnDefinitions().Append(ColumnDefinition());
        previewContent.ColumnDefinitions().Append(ColumnDefinition());
        previewContent.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::Auto());
        previewContent.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        previewContent.ColumnDefinitions().GetAt(2).Width(GridLengthHelper::Auto());
        auto previewIcon = makeCategoryIcon(original.iconName, 20);
        previewIcon.Width(20);
        previewIcon.Height(20);
        previewIcon.VerticalAlignment(VerticalAlignment::Center);
        previewContent.Children().Append(previewIcon);
        TextBlock previewName;
        const auto originalPreviewName = original.name.empty() ? std::wstring{L"Category preview"} : cuelet::windows::utf8ToWide(original.name);
        previewName.Text(originalPreviewName);
        previewName.TextTrimming(TextTrimming::CharacterEllipsis);
        previewName.TextWrapping(TextWrapping::NoWrap);
        previewName.VerticalAlignment(VerticalAlignment::Center);
        ToolTipService::SetToolTip(previewName, box_value(originalPreviewName));
        Grid::SetColumn(previewName, 1);
        previewContent.Children().Append(previewName);
        Microsoft::UI::Xaml::Shapes::Ellipse previewDot;
        previewDot.Width(8);
        previewDot.Height(8);
        previewDot.Fill(cuelet::windows::categoryColorBrush(original.colorHex));
        previewDot.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(previewDot, 2);
        previewContent.Children().Append(previewDot);
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
            const auto previewText = cuelet::trim(cuelet::windows::hstringToUtf8(name.Text())).empty()
                ? hstring(L"Category preview")
                : name.Text();
            previewName.Text(previewText);
            ToolTipService::SetToolTip(previewName, box_value(previewText));
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
        if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;
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
            if (m_filter.scope == cuelet::LibraryScope::Category && m_filter.categoryId == savedId) {
                const auto title = cuelet::windows::utf8ToHstring(categoryName);
                PageTitle().Text(title);
                ToolTipService::SetToolTip(PageTitle(), box_value(title));
            }
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
        if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;
        for (auto& clip : m_clips) if (clip.categoryId == categoryId) clip.categoryId = "uncategorized";
        std::erase_if(m_categories, [&](auto const& category) { return category.id == categoryId; });
        if (m_filter.scope == cuelet::LibraryScope::Category && m_filter.categoryId == categoryId) {
            m_filter.scope = cuelet::LibraryScope::All;
            m_filter.categoryId.clear();
            PageTitle().Text(L"Library");
            ToolTipService::SetToolTip(PageTitle(), box_value(L"Library"));
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
        auto durationSeconds = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
        if (const auto clip = findClip(active.clipId);
            clip && clip->durationKnown && clip->durationSeconds >= 0.0) {
            durationSeconds = clip->durationSeconds;
        }
        const auto progress = cuelet::makePlaybackProgress(positionSeconds, durationSeconds);
        PlaybackProgress().Maximum(progress.durationSeconds > 0.0 ? progress.durationSeconds : 1.0);
        PlaybackProgress().Value(progress.durationSeconds > 0.0 ? progress.positionSeconds : 0.0);
        const auto elapsed = progress.positionSeconds > 0 ? formatDuration(progress.positionSeconds) : std::wstring{L"0:00"};
        const auto duration = progress.durationSeconds > 0 ? formatDuration(progress.durationSeconds) : std::wstring{L"\u2014"};
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
            if (!SearchBox().Text().empty()) {
                SearchBox().Text(L"");
                args.Handled(true);
                return;
            }
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
        const auto current = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) { return clip.id == clipId; });
        if (current != m_clips.end() && current->shortcut &&
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
            shortcut->global = true;
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
            const auto soundName = displayLabel(clip);
            name.Text(soundName);
            name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
            name.TextTrimming(TextTrimming::CharacterEllipsis);
            name.TextWrapping(TextWrapping::NoWrap);
            ToolTipService::SetToolTip(name, box_value(soundName));
            details.Children().Append(name);
            TextBlock binding;
            binding.Text(cuelet::windows::formatShortcut(*clip.shortcut) + L" · Global");
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
        dialog.Content(box_value(L"This removes every global shortcut in the current library."));
        dialog.PrimaryButtonText(L"Clear all");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        if (co_await showDialogAsync(dialog) != ContentDialogResult::Primary) co_return;

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
            OnboardingPage().Visibility(Visibility::Collapsed);
            SettingsPage().Visibility(Visibility::Visible);
            SettingsScrollViewer().ChangeView(nullptr, 0.0, nullptr, true);
            refreshMicrophoneAccessState();
            refreshShortcutSettings();
            return;
        }
        const auto item = args.InvokedItemContainer().try_as<NavigationViewItem>();
        const auto tag = item
            ? unbox_value_or<hstring>(item.Tag(), L"library")
            : hstring(L"library");
        if (tag == L"help") {
            showHelpAsync();
            return;
        }
        if (tag == L"about") {
            showAboutAsync();
            return;
        }
        if (cuelet::windows::libraryStartupState(m_libraryFolder) !=
            cuelet::windows::LibraryStartupState::Ready) {
            showLibraryStartupState();
            return;
        }
        LibraryPage().Visibility(Visibility::Visible);
        OnboardingPage().Visibility(Visibility::Collapsed);
        SettingsPage().Visibility(Visibility::Collapsed);
        if (!item) return;
        if (tag == L"new-category") { createCategoryAsync(); return; }
        auto setPageTitle = [&](hstring const& title) {
            PageTitle().Text(title);
            ToolTipService::SetToolTip(PageTitle(), box_value(title));
        };
        if (tag == L"favorites") { setPageTitle(L"Favorites"); setScope(cuelet::LibraryScope::Favorites); }
        else if (tag == L"recent") { setPageTitle(L"Recent"); setScope(cuelet::LibraryScope::Recent); }
        else if (tag == L"all-categories") { setPageTitle(L"All Categories"); setScope(cuelet::LibraryScope::AllCategories); }
        else if (tag.starts_with(L"category:")) {
            const auto id = cuelet::windows::wideToUtf8(std::wstring(tag.c_str()).substr(9));
            auto category = cuelet::categoryForId(m_categories, id);
            setPageTitle(category ? cuelet::windows::utf8ToHstring(category->name) : hstring(L"Category"));
            setScope(cuelet::LibraryScope::Category, id);
        } else { setPageTitle(L"Library"); setScope(cuelet::LibraryScope::All); }
    }

    void MainWindow::ChooseLibrary_Click(IInspectable const&, RoutedEventArgs const&) { chooseLibraryAsync(); }
    void MainWindow::CreateLibrary_Click(IInspectable const&, RoutedEventArgs const&) { createLibraryAsync(); }
    void MainWindow::Import_Click(IInspectable const&, RoutedEventArgs const&) { importAsync(); }
    void MainWindow::Rescan_Click(IInspectable const&, RoutedEventArgs const&) { scanLibrary(true); }
    void MainWindow::StopAll_Click(IInspectable const&, RoutedEventArgs const&) { stopAll(); refreshSounds(true); }
    void MainWindow::StopCurrent_Click(IInspectable const&, RoutedEventArgs const&) { stopCurrent(); }
    void MainWindow::SearchBox_TextChanged(AutoSuggestBox const&, AutoSuggestBoxTextChangedEventArgs const&) { refreshSounds(); }
    void MainWindow::SearchBox_QuerySubmitted(
        AutoSuggestBox const& sender, AutoSuggestBoxQuerySubmittedEventArgs const&)
    {
        const auto query = cuelet::windows::hstringToUtf8(sender.Text());
        if (cuelet::trim(query).empty()) return;
        std::string targetId;
        if (!m_selectedClipId.empty()) {
            const auto selectedVisible = std::find_if(m_visibleClips.begin(), m_visibleClips.end(), [&](auto const& clip) {
                return clip.id == m_selectedClipId;
            });
            if (selectedVisible != m_visibleClips.end()) {
                targetId = selectedVisible->id;
            }
        }
        if (targetId.empty()) {
            if (const auto best = cuelet::bestMatchingSound(m_visibleClips, m_categories, query)) targetId = best->id;
        }
        if (targetId.empty()) return;
        const auto now = std::chrono::steady_clock::now();
        if (targetId == m_lastSearchPlayId &&
            now - m_lastSearchPlayAt < std::chrono::milliseconds(500)) return;
        m_lastSearchPlayId = targetId;
        m_lastSearchPlayAt = now;
        playClipAsync(targetId);
    }

    void MainWindow::SoundDropTarget_DragOver(IInspectable const&, DragEventArgs const& args)
    {
        clearCategoryDropVisual();
        if (m_libraryFolder.empty() || !args.DataView().Contains(StandardDataFormats::StorageItems())) {
            args.AcceptedOperation(DataPackageOperation::None);
            DropOverlayTitle().Text(L"Unsupported drop");
            DropOverlayDescription().Text(m_libraryFolder.empty()
                ? L"Create or select a library first." : L"Drop files or folders from File Explorer.");
            DropOverlay().Visibility(Visibility::Visible);
            args.Handled(true);
            return;
        }
        args.AcceptedOperation(m_importBehavior == cuelet::windows::ImportBehavior::Link
            ? DataPackageOperation::Link : DataPackageOperation::Copy);
        args.DragUIOverride().IsCaptionVisible(true);
        args.DragUIOverride().Caption(m_importBehavior == cuelet::windows::ImportBehavior::Link
            ? L"Link to original file" : L"Copy to library");
        showDropState();
        inspectDragItemsAsync(args);
        args.Handled(true);
    }

    void MainWindow::SoundDropTarget_DragLeave(IInspectable const&, DragEventArgs const& args)
    {
        clearCategoryDropVisual();
        DropOverlay().Visibility(Visibility::Collapsed);
        args.Handled(true);
    }

    void MainWindow::SoundDropTarget_Drop(IInspectable const&, DragEventArgs const& args)
    {
        clearCategoryDropVisual();
        DropOverlay().Visibility(Visibility::Collapsed);
        if (!m_libraryFolder.empty() && args.DataView().Contains(StandardDataFormats::StorageItems())) {
            importStorageItemsAsync(args.DataView(), importCategoryForCurrentScope());
        }
        args.Handled(true);
    }

    void MainWindow::RunAudioSetup_Click(IInspectable const&, RoutedEventArgs const&) { runAudioSetupAsync(); }

    void MainWindow::RefreshAudioDevices_Click(IInspectable const&, RoutedEventArgs const&)
    {
        m_loadingAudioDevices = true;
        initializeAudioRoutingAsync();
    }

    void MainWindow::TestMicrophone_Click(IInspectable const&, RoutedEventArgs const&)
    {
        testMicrophoneAsync();
    }

    void MainWindow::OpenMicrophonePrivacy_Click(IInspectable const&, RoutedEventArgs const&)
    {
        Launcher::LaunchUriAsync(Uri(L"ms-settings:privacy-microphone"));
    }

    void MainWindow::InstallVirtualDriver_Click(IInspectable const&, RoutedEventArgs const&)
    {
#if defined(_DEBUG)
        runVirtualDriverActionAsync(L"install");
#else
        Launcher::LaunchUriAsync(Uri(L"https://vb-audio.com/Cable/"));
#endif
    }

    void MainWindow::RepairVirtualDriver_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runVirtualDriverActionAsync(L"repair");
    }

    void MainWindow::UninstallVirtualDriver_Click(IInspectable const&, RoutedEventArgs const&)
    {
        runVirtualDriverActionAsync(L"uninstall");
    }

    void MainWindow::RefreshVirtualDriver_Click(IInspectable const&, RoutedEventArgs const&)
    {
#if defined(_DEBUG)
        refreshVirtualDriverStatusAsync();
#endif
        initializeAudioRoutingAsync();
    }
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

    cuelet::windows::CliExecutionResult MainWindow::ExecuteCli(
        cuelet::windows::CliRequest const& request)
    {
        using cuelet::windows::CliCommand;
        cuelet::windows::CliExecutionResult result;
        const auto fail = [&](int code, std::wstring const& message) {
            result.exitCode = code;
            result.standardError = cuelet::windows::wideToUtf8(message) + "\n";
            return result;
        };

        if (request.command == CliCommand::Invalid) return fail(2, request.error);
        if (request.command == CliCommand::Help) {
            result.standardOutput = cuelet::windows::wideToUtf8(cuelet::windows::cliHelpText());
            return result;
        }

        if (request.command == CliCommand::CreateLibrary) {
            if (request.library.empty()) return fail(2, L"--create-library requires a folder.");
            std::error_code error;
            if (std::filesystem::exists(request.library, error) &&
                !std::filesystem::is_empty(request.library, error)) {
                return fail(4, L"The create-library destination already exists and is not empty.");
            }
            std::filesystem::create_directories(request.library, error);
            if (error) return fail(4, L"Could not create the library: " +
                cuelet::windows::utf8ToWide(error.message()));
            cuelet::LibraryMetadata initial;
            std::string metadataError;
            if (!m_metadataStore.save(request.library, initial, &metadataError)) {
                return fail(4, L"Could not initialize library metadata: " +
                    cuelet::windows::utf8ToWide(metadataError));
            }
        }

        if (!request.library.empty()) {
            std::error_code error;
            if (!std::filesystem::is_directory(request.library, error)) {
                return fail(3, L"The library path does not exist or is not accessible.");
            }
            m_libraryFolder = request.library;
            saveSettings();
            scanLibrary();
            const auto metadataPath = m_libraryFolder / cuelet::windows::WindowsMetadataStore::fileName;
            if (!std::filesystem::exists(metadataPath) && !saveMetadata(false)) {
                return fail(4, L"The library metadata could not be initialized.");
            }
        }

        if (request.command == CliCommand::CreateLibrary ||
            request.command == CliCommand::UseLibrary) {
            result.standardOutput = "Using library: " +
                cuelet::windows::pathToUtf8(m_libraryFolder) + "\n";
            result.keepRunning = true;
            return result;
        }

        if (request.command != CliCommand::Show && request.command != CliCommand::Hide &&
            request.command != CliCommand::Launch &&
            cuelet::windows::libraryStartupState(m_libraryFolder) !=
                cuelet::windows::LibraryStartupState::Ready) {
            return fail(3, L"No valid Cuelet library is configured. Use --library <folder>.");
        }

        const auto enqueueClipPlayback = [weak = get_weak()](
                                             std::string id) {
            if (auto self = weak.get()) {
                return self->DispatcherQueue().TryEnqueue(
                    [weak, id = std::move(id)] {
                        if (auto current = weak.get()) {
                            current->playClipAsync(id);
                        }
                    });
            }
            return false;
        };
        const auto enqueueFilePlayback = [weak = get_weak()](
                                             std::filesystem::path path) {
            if (auto self = weak.get()) {
                return self->DispatcherQueue().TryEnqueue(
                    [weak, path = std::move(path)] {
                        if (auto current = weak.get()) {
                            current->playExternalFileAsync(path);
                        }
                    });
            }
            return false;
        };

        switch (request.command) {
        case CliCommand::Launch:
            result.keepRunning = true;
            return result;
        case CliCommand::ListSounds: {
            if (request.json) {
                JsonArray values;
                for (auto const& clip : m_clips) {
                    JsonObject value;
                    value.Insert(L"id", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip.id)));
                    value.Insert(L"displayName", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip.searchableName())));
                    value.Insert(L"filename", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip.filename)));
                    value.Insert(L"categoryId", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip.categoryId)));
                    value.Insert(L"storageMode", JsonValue::CreateStringValue(
                        clip.storageMode == cuelet::SoundStorageMode::Linked ? L"linked" : L"managed"));
                    value.Insert(L"path", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(clip.absolutePath)));
                    value.Insert(L"missing", JsonValue::CreateBooleanValue(clip.missing));
                    values.Append(value);
                }
                result.standardOutput = cuelet::windows::hstringToUtf8(values.Stringify()) + "\n";
            } else {
                for (auto const& clip : m_clips) {
                    result.standardOutput += clip.id + "\t" + clip.searchableName() + "\t" +
                        clip.absolutePath + "\n";
                }
            }
            return result;
        }
        case CliCommand::ListCategories: {
            if (request.json) {
                JsonArray values;
                for (auto const& category : m_categories) {
                    JsonObject value;
                    value.Insert(L"id", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(category.id)));
                    value.Insert(L"name", JsonValue::CreateStringValue(cuelet::windows::utf8ToHstring(category.name)));
                    values.Append(value);
                }
                result.standardOutput = cuelet::windows::hstringToUtf8(values.Stringify()) + "\n";
            } else {
                for (auto const& category : m_categories) {
                    result.standardOutput += category.id + "\t" + category.name + "\n";
                }
            }
            return result;
        }
        case CliCommand::PlayId:
        case CliCommand::Stop:
        case CliCommand::RevealId: {
            const auto id = cuelet::windows::wideToUtf8(request.value);
            auto clip = findClip(id);
            if (!clip) return fail(3, L"No sound has ID " + request.value + L".");
            if (request.command == CliCommand::PlayId) {
                if (clip->missing || !std::filesystem::exists(cuelet::windows::pathFromUtf8(clip->absolutePath))) {
                    return fail(3, clip->storageMode == cuelet::SoundStorageMode::Linked
                        ? L"The linked source file is missing. Use Locate Source File in Cuelet."
                        : L"The managed sound file is missing.");
                }
                if (!enqueueClipPlayback(id)) {
                    return fail(
                        5, L"Cuelet could not queue the play command.");
                }
                result.keepRunning = true;
                result.standardOutput = "Playing " + id + "\n";
            } else if (request.command == CliCommand::Stop) {
                stopClip(id);
                result.standardOutput = "Stopped " + id + "\n";
            } else {
                if (clip->missing) return fail(3, L"The sound source is missing and cannot be revealed.");
                showClipInExplorer(id);
                result.standardOutput = "Revealed " + id + "\n";
            }
            return result;
        }
        case CliCommand::PlayName: {
            const auto query = cuelet::windows::wideToUtf8(request.value);
            const auto match = cuelet::bestMatchingSound(m_clips, m_categories, query);
            if (!match) return fail(3, L"No sound matches \u201c" + request.value + L"\u201d.");
            if (match->missing) return fail(3, L"The best matching sound source is missing.");
            const auto id = match->id;
            if (!enqueueClipPlayback(id)) {
                return fail(
                    5, L"Cuelet could not queue the play command.");
            }
            result.keepRunning = true;
            result.standardOutput = "Playing " + id + "\n";
            return result;
        }
        case CliCommand::PlayFile: {
            const auto path = std::filesystem::path(request.value);
            if (!cuelet::LibraryScanner::isSupportedAudioFile(path)) {
                return fail(3, L"The play-file path is missing or uses an unsupported audio format.");
            }
            const auto known = std::find_if(m_clips.begin(), m_clips.end(), [&](auto const& clip) {
                return cuelet::windows::pathsReferToSameFile(
                    path, cuelet::windows::pathFromUtf8(clip.absolutePath));
            });
            const auto queued =
                known != m_clips.end()
                    ? enqueueClipPlayback(known->id)
                    : enqueueFilePlayback(path);
            if (!queued) {
                return fail(
                    5, L"Cuelet could not queue the play command.");
            }
            result.keepRunning = true;
            result.standardOutput = "Playing " + cuelet::windows::pathToUtf8(path) + "\n";
            return result;
        }
        case CliCommand::StopAll:
            stopAll();
            result.standardOutput = "Stopped all sounds.\n";
            return result;
        case CliCommand::Show:
            ::ShowWindow(m_hwnd, SW_RESTORE);
            Activate();
            result.keepRunning = true;
            return result;
        case CliCommand::Hide:
            ::ShowWindow(m_hwnd, SW_HIDE);
            result.keepRunning = true;
            return result;
        case CliCommand::Rescan:
            scanLibrary();
            result.standardOutput = std::to_string(m_clips.size()) + " sounds indexed.\n";
            return result;
        case CliCommand::Import: {
            auto categoryId = std::string{"uncategorized"};
            if (!request.categoryName.empty()) {
                const auto normalized = cuelet::normalizeForSearch(
                    cuelet::windows::wideToUtf8(request.categoryName));
                const auto category = std::find_if(m_categories.begin(), m_categories.end(), [&](auto const& value) {
                    return cuelet::normalizeForSearch(value.name) == normalized;
                });
                if (category == m_categories.end()) {
                    return fail(3, L"No category is named \u201c" + request.categoryName + L"\u201d.");
                }
                categoryId = category->id;
            }
            const auto behavior = request.importBehaviorSpecified
                ? request.importBehavior : m_importBehavior;
            const auto summary = importPaths(request.importPaths, behavior, categoryId);
            if (request.json) {
                JsonObject value;
                value.Insert(L"imported", JsonValue::CreateNumberValue(static_cast<double>(summary.imported)));
                value.Insert(L"linked", JsonValue::CreateNumberValue(static_cast<double>(summary.linked)));
                value.Insert(L"duplicates", JsonValue::CreateNumberValue(static_cast<double>(summary.duplicates)));
                value.Insert(L"skipped", JsonValue::CreateNumberValue(static_cast<double>(summary.skipped)));
                result.standardOutput = cuelet::windows::hstringToUtf8(value.Stringify()) + "\n";
            } else {
                result.standardOutput = std::to_string(summary.imported) + " imported, " +
                    std::to_string(summary.linked) + " linked, " +
                    std::to_string(summary.duplicates) + " duplicates, " +
                    std::to_string(summary.skipped) + " skipped.\n";
            }
            return result;
        }
        case CliCommand::Help:
        case CliCommand::UseLibrary:
        case CliCommand::CreateLibrary:
        case CliCommand::Invalid:
            break;
        }
        return fail(2, L"The command is not implemented.");
    }

    void MainWindow::handleCliCopyData(std::wstring const& payload)
    {
        const auto first = payload.find(L'\n');
        const auto second = first == std::wstring::npos ? std::wstring::npos : payload.find(L'\n', first + 1);
        if (first == std::wstring::npos || second == std::wstring::npos) return;
        const auto responsePath = std::filesystem::path(payload.substr(0, first));
        const auto currentDirectory = std::filesystem::path(payload.substr(first + 1, second - first - 1));
        const auto rawCommandLine = payload.substr(second + 1);

        int count = 0;
        auto values = ::CommandLineToArgvW(rawCommandLine.c_str(), &count);
        std::vector<std::wstring> arguments;
        if (values) {
            for (int index = 1; index < count; ++index) arguments.emplace_back(values[index]);
            ::LocalFree(values);
        }
        auto result = ExecuteCli(cuelet::windows::parseCommandLine(arguments, currentDirectory));
        std::ofstream stream(responsePath, std::ios::binary | std::ios::trunc);
        if (!stream) return;
        stream << result.exitCode << '\n'
               << result.standardOutput.size() << '\n'
               << result.standardError.size() << '\n';
        stream.write(result.standardOutput.data(), static_cast<std::streamsize>(result.standardOutput.size()));
        stream.write(result.standardError.data(), static_cast<std::streamsize>(result.standardError.size()));
    }

    void MainWindow::OpenLibraryFromActivation(std::wstring const& path)
    {
        std::error_code error;
        if (!path.empty() && std::filesystem::is_directory(path, error)) {
            m_libraryFolder = std::filesystem::path(path);
            saveSettings();
            scanLibrary();
            const auto metadataPath = m_libraryFolder / cuelet::windows::WindowsMetadataStore::fileName;
            if (!std::filesystem::exists(metadataPath)) saveMetadata();
            ::ShowWindow(m_hwnd, SW_RESTORE);
            Activate();
        } else {
            showStatus(L"The command-line library path does not exist or is not accessible.",
                       InfoBarSeverity::Error);
        }
    }
}
