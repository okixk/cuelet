#include "CueletWindow.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "CueletWindowHelpers.h"

using namespace cuelet_linux;

CueletWindow::CueletWindow(AdwApplication* application, bool demoMode)
    : application_(application)
{
    categories_ = {cuelet::uncategorizedCategory()};
    settings_ = settingsStore_.load();
    applyAppearanceMode();
    audio_.setVolume(settings_.volume);
    audio_.setAllowsSimultaneousPlayback(settings_.allowsSimultaneousPlayback);
    audio_.setFinishCallback([this](const std::string&) {
        refreshContent();
        refreshNowPlaying();
        if (audio_.playingPaths().empty()) {
            withdrawPlaybackNotification();
        }
    });
    audio_.setErrorCallback([this](const std::string& message) {
        showError(message);
    });

    buildUi();
    LinuxAudioService::OutputSelection configuredOutput;
    const bool validOutputSetting =
        parseOutputSetting(settings_.outputDevice, configuredOutput);
    const bool backendAvailable =
        configuredOutput.backend == LinuxAudioService::OutputBackend::Automatic
        || LinuxAudioService::outputBackendAvailable(configuredOutput.backend);
    if (validOutputSetting && backendAvailable) {
        audio_.setOutputSelection(configuredOutput);
    } else {
        audio_.setOutputSelection({});
        showToast("The saved audio output is unavailable; using automatic output.");
    }
    loadInitialLibrary(demoMode);
}

CueletWindow::~CueletWindow()
{
    if (visualCaptureSourceId_ != 0) {
        g_source_remove(visualCaptureSourceId_);
    }
    if (progressTickId_ != 0) {
        g_source_remove(progressTickId_);
    }
    audio_.stopAll();
    pipeWireRouting_.stop();
}

void CueletWindow::present()
{
    scheduleVisualCaptureFromEnvironment();
    gtk_window_present(GTK_WINDOW(window_));
}

void CueletWindow::closeForCliExit()
{
    closedForCliExit_ = true;
    gtk_window_destroy(GTK_WINDOW(window_));
}

bool CueletWindow::isClosedForCliExit() const
{
    return closedForCliExit_;
}

bool CueletWindow::parseOutputSetting(
    const std::string& value,
    LinuxAudioService::OutputSelection& selection)
{
    // Stable persisted format: pipewire:<target-object> or
    // pulseaudio:<device>. Empty/"automatic" follows the desktop default.
    if (value.empty() || value == "automatic") {
        selection = {};
        return true;
    }

    static const std::string pipeWirePrefix = "pipewire:";
    static const std::string pulseAudioPrefix = "pulseaudio:";
    if (value.rfind(pipeWirePrefix, 0) == 0 && value.size() > pipeWirePrefix.size()) {
        selection.backend = LinuxAudioService::OutputBackend::PipeWire;
        selection.deviceId = value.substr(pipeWirePrefix.size());
        return true;
    }
    if (value.rfind(pulseAudioPrefix, 0) == 0 && value.size() > pulseAudioPrefix.size()) {
        selection.backend = LinuxAudioService::OutputBackend::PulseAudio;
        selection.deviceId = value.substr(pulseAudioPrefix.size());
        return true;
    }
    selection = {};
    return false;
}

std::string CueletWindow::outputSetting(
    const LinuxAudioService::OutputSelection& selection)
{
    switch (selection.backend) {
    case LinuxAudioService::OutputBackend::PipeWire:
        return "pipewire:" + selection.deviceId;
    case LinuxAudioService::OutputBackend::PulseAudio:
        return "pulseaudio:" + selection.deviceId;
    case LinuxAudioService::OutputBackend::Automatic:
        return {};
    }
    return {};
}

void CueletWindow::buildUi()
{
    installCss();

    window_ = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(application_)));
    gtk_window_set_title(GTK_WINDOW(window_), "Cuelet");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1180, 760);

    GtkWidget* toolbarView = adw_toolbar_view_new();
    toastOverlay_ = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toastOverlay_), toolbarView);
    adw_application_window_set_content(window_, toastOverlay_);

    GtkWidget* header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarView), header);

    headerTitle_ = adw_window_title_new("Cuelet", "Soundboard");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), headerTitle_);

    installActions();

    sidebarToggleButton_ = iconButton("sidebar-show-symbolic", "Show Navigation");
    gtk_widget_set_visible(sidebarToggleButton_, FALSE);
    g_signal_connect_swapped(sidebarToggleButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        if (!self->splitView_) {
            return;
        }
        const bool showsContent = adw_navigation_split_view_get_show_content(self->splitView_);
        adw_navigation_split_view_set_show_content(self->splitView_, !showsContent);
    }), this);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), sidebarToggleButton_);

    GtkWidget* chooseButton = textIconButton("folder-open-symbolic", "Choose Library", "Choose Library");
    gtk_widget_add_css_class(chooseButton, "suggested-action");
    g_signal_connect_swapped(chooseButton, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->chooseLibrary();
    }), this);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), chooseButton);

    stopAllButton_ = iconButton("media-playback-stop-symbolic", "Stop All");
    gtk_widget_add_css_class(stopAllButton_, "destructive-action");
    gtk_widget_set_sensitive(stopAllButton_, FALSE);
    g_signal_connect_swapped(stopAllButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->stopAll();
    }), this);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), stopAllButton_);

    GtkWidget* appMenuButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(appMenuButton), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(appMenuButton, "Main Menu");
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(appMenuButton),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Main Menu",
        -1);
    GMenu* appMenu = g_menu_new();
    GMenu* librarySection = g_menu_new();
    g_menu_append(librarySection, "Choose Library", "win.choose-library");
    g_menu_append(librarySection, "Import Sounds", "win.import-sounds");
    g_menu_append(librarySection, "Rescan Library", "win.rescan-library");
    g_menu_append(librarySection, "New Category", "win.new-category");
    g_menu_append_section(appMenu, nullptr, G_MENU_MODEL(librarySection));
    g_object_unref(librarySection);
    GMenu* appSection = g_menu_new();
    g_menu_append(appSection, "Preferences", "win.preferences");
    g_menu_append_section(appMenu, nullptr, G_MENU_MODEL(appSection));
    g_object_unref(appSection);
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(appMenuButton), G_MENU_MODEL(appMenu));
    g_object_unref(appMenu);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), appMenuButton);

    GtkWidget* viewBox = linkedBox();
    gridToggle_ = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(gridToggle_), "view-grid-symbolic");
    gtk_widget_set_tooltip_text(gridToggle_, "Grid View");
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(gridToggle_),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Grid View",
        -1);
    listToggle_ = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(listToggle_), "view-list-symbolic");
    gtk_widget_set_tooltip_text(listToggle_, "List View");
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(listToggle_),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "List View",
        -1);
    gtk_box_append(GTK_BOX(viewBox), gridToggle_);
    gtk_box_append(GTK_BOX(viewBox), listToggle_);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), viewBox);

    g_signal_connect(gridToggle_, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        if (self->suppressToggleSignals_ || !gtk_toggle_button_get_active(button)) {
            return;
        }
        self->settings_.viewMode = "grid";
        self->saveSettings();
        self->refreshContent();
    }), this);
    g_signal_connect(listToggle_, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        if (self->suppressToggleSignals_ || !gtk_toggle_button_get_active(button)) {
            return;
        }
        self->settings_.viewMode = "list";
        self->saveSettings();
        self->refreshContent();
    }), this);

    GtkWidget* sortButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(sortButton), "view-sort-ascending-symbolic");
    gtk_widget_set_tooltip_text(sortButton, "Sort Sounds");
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(sortButton),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Sort Sounds",
        -1);
    GMenu* sortMenu = g_menu_new();
    const std::vector<std::pair<std::string, std::string>> sortItems = {
        {"Name A-Z", "nameAscending"},
        {"Name Z-A", "nameDescending"},
        {"Latest Added", "latestAdded"},
        {"Oldest Added", "oldestAdded"},
        {"Duration Shortest", "durationShortest"},
        {"Duration Longest", "durationLongest"},
        {"Category", "category"},
    };
    for (const auto& [label, id] : sortItems) {
        GMenuItem* item = g_menu_item_new(label.c_str(), nullptr);
        g_menu_item_set_action_and_target(item, "win.sort", "s", id.c_str());
        g_menu_append_item(sortMenu, item);
        g_object_unref(item);
    }
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(sortButton), G_MENU_MODEL(sortMenu));
    g_object_unref(sortMenu);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), sortButton);

    splitView_ = ADW_NAVIGATION_SPLIT_VIEW(adw_navigation_split_view_new());
    adw_navigation_split_view_set_min_sidebar_width(splitView_, 220);
    adw_navigation_split_view_set_max_sidebar_width(splitView_, 320);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarView), GTK_WIDGET(splitView_));

    AdwBreakpointCondition* narrowCondition =
        adw_breakpoint_condition_parse("max-width: 760sp");
    AdwBreakpoint* narrowBreakpoint = adw_breakpoint_new(narrowCondition);
    GValue collapsed = G_VALUE_INIT;
    g_value_init(&collapsed, G_TYPE_BOOLEAN);
    g_value_set_boolean(&collapsed, TRUE);
    adw_breakpoint_add_setter(
        narrowBreakpoint,
        G_OBJECT(splitView_),
        "collapsed",
        &collapsed);
    g_value_unset(&collapsed);
    adw_application_window_add_breakpoint(window_, narrowBreakpoint);

    g_object_bind_property(
        splitView_, "collapsed",
        sidebarToggleButton_, "visible",
        static_cast<GBindingFlags>(G_BINDING_SYNC_CREATE));
    g_signal_connect(splitView_, "notify::show-content", G_CALLBACK(+[](
        AdwNavigationSplitView* splitView,
        GParamSpec*,
        gpointer userData) {
        GtkWidget* button = GTK_WIDGET(userData);
        const bool showsContent = adw_navigation_split_view_get_show_content(splitView);
        const char* iconName = showsContent ? "sidebar-show-symbolic" : "go-next-symbolic";
        const char* label = showsContent ? "Show Navigation" : "Show Sounds";
        gtk_button_set_icon_name(GTK_BUTTON(button), iconName);
        gtk_widget_set_tooltip_text(button, label);
        gtk_accessible_update_property(
            GTK_ACCESSIBLE(button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, label,
            -1);
    }), sidebarToggleButton_);

    GtkWidget* sidebarBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebarBox, "cuelet-sidebar");
    gtk_widget_set_vexpand(sidebarBox, TRUE);
    GtkWidget* sidebarScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebarScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sidebarScroll, TRUE);
    sidebarList_ = gtk_list_box_new();
    gtk_widget_add_css_class(sidebarList_, "navigation-sidebar");
    gtk_widget_add_css_class(sidebarList_, "cuelet-sidebar-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebarList_), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(sidebarList_), TRUE);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(sidebarList_),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Library Navigation",
        -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebarScroll), sidebarList_);
    gtk_box_append(GTK_BOX(sidebarBox), sidebarScroll);
    g_signal_connect(sidebarList_, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer userData) {
        if (g_object_get_data(G_OBJECT(row), "sidebar-section")) {
            return;
        }
        auto* self = static_cast<CueletWindow*>(userData);
        self->selection_.kind = static_cast<SidebarKind>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "sidebar-kind")));
        self->selection_.categoryId = objectString(G_OBJECT(row), "category-id");
        self->selectedPaths_.clear();
        self->refreshContent();
        self->refreshHeader();
        if (self->splitView_ && adw_navigation_split_view_get_collapsed(self->splitView_)) {
            adw_navigation_split_view_set_show_content(self->splitView_, TRUE);
        }
    }), this);

    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(contentBox, "cuelet-content");

    GtkDropTarget* importDropTarget =
        gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(importDropTarget, "enter", G_CALLBACK(+[](
        GtkDropTarget* target,
        double,
        double,
        gpointer) -> GdkDragAction {
        GtkWidget* content = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
        gtk_widget_add_css_class(content, "drop-active");
        return GDK_ACTION_COPY;
    }), nullptr);
    g_signal_connect(importDropTarget, "leave", G_CALLBACK(+[](
        GtkDropTarget* target,
        gpointer) {
        GtkWidget* content = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
        gtk_widget_remove_css_class(content, "drop-active");
    }), nullptr);
    g_signal_connect(importDropTarget, "drop", G_CALLBACK(+[](
        GtkDropTarget* target,
        const GValue* value,
        double,
        double,
        gpointer userData) -> gboolean {
        GtkWidget* content = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
        gtk_widget_remove_css_class(content, "drop-active");
        auto* self = static_cast<CueletWindow*>(userData);
        auto* fileList = static_cast<GdkFileList*>(g_value_get_boxed(value));
        if (!fileList) {
            return FALSE;
        }

        std::vector<std::filesystem::path> sources;
        std::size_t unavailableSources = 0;
        for (GSList* item = gdk_file_list_get_files(fileList); item; item = item->next) {
            GFile* file = G_FILE(item->data);
            char* path = g_file_get_path(file);
            if (path) {
                sources.emplace_back(std::filesystem::u8path(path));
                g_free(path);
            } else {
                ++unavailableSources;
            }
        }
        self->importSources(sources, unavailableSources);
        return TRUE;
    }), this);
    gtk_widget_add_controller(
        contentBox,
        GTK_EVENT_CONTROLLER(importDropTarget));

    GtkWidget* contentHeader = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_add_css_class(contentHeader, "cuelet-content-header");
    titleLabel_ = titleLabel("Library");
    subtitleLabel_ = secondaryLabel("No library selected");
    GtkWidget* titleBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(titleBox, TRUE);
    gtk_box_append(GTK_BOX(titleBox), titleLabel_);
    gtk_box_append(GTK_BOX(titleBox), subtitleLabel_);

    GtkWidget* toolsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(toolsBox, GTK_ALIGN_END);
    gtk_widget_set_valign(toolsBox, GTK_ALIGN_CENTER);
    countLabel_ = secondaryLabel("0 sounds");
    gtk_widget_add_css_class(countLabel_, "cuelet-count-pill");
    GtkWidget* search = gtk_search_entry_new();
    searchEntry_ = search;
    gtk_widget_add_css_class(search, "cuelet-search");
    gtk_widget_set_size_request(search, 280, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), "Search sounds");
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(search),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Search Sounds",
        GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
            "Search names, filenames, notes, aliases, and categories",
        -1);
    gtk_box_append(GTK_BOX(toolsBox), countLabel_);
    gtk_box_append(GTK_BOX(toolsBox), search);
    gtk_box_append(GTK_BOX(contentHeader), titleBox);
    gtk_box_append(GTK_BOX(contentHeader), toolsBox);
    gtk_box_append(GTK_BOX(contentBox), contentHeader);

    g_signal_connect(searchEntry_, "search-changed", G_CALLBACK(+[](GtkSearchEntry*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->refreshContent();
    }), this);

    GtkEventController* searchKeys = gtk_event_controller_key_new();
    g_signal_connect(searchKeys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer userData) {
        return static_cast<CueletWindow*>(userData)->handleSearchKey(keyval) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
    }), this);
    gtk_widget_add_controller(searchEntry_, searchKeys);

    stack_ = gtk_stack_new();
    gtk_widget_set_vexpand(stack_, TRUE);
    gtk_widget_set_hexpand(stack_, TRUE);

    GtkWidget* gridScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gridScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    flowBox_ = gtk_flow_box_new();
    gtk_widget_add_css_class(flowBox_, "cuelet-grid");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowBox_), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flowBox_), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowBox_), 4);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flowBox_), 16);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flowBox_), 16);
    gtk_widget_set_margin_top(flowBox_, 4);
    gtk_widget_set_margin_start(flowBox_, 24);
    gtk_widget_set_margin_end(flowBox_, 24);
    gtk_widget_set_margin_bottom(flowBox_, 28);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(gridScroll), flowBox_);
    gtk_stack_add_named(GTK_STACK(stack_), gridScroll, "grid");

    GtkWidget* listScroll = gtk_scrolled_window_new();
    listBox_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listBox_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(listBox_, "boxed-list");
    gtk_widget_add_css_class(listBox_, "cuelet-list");
    gtk_widget_set_margin_top(listBox_, 4);
    gtk_widget_set_margin_start(listBox_, 24);
    gtk_widget_set_margin_end(listBox_, 24);
    gtk_widget_set_margin_bottom(listBox_, 28);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), listBox_);
    gtk_stack_add_named(GTK_STACK(stack_), listScroll, "list");

    emptyPage_ = adw_status_page_new();
    gtk_widget_add_css_class(emptyPage_, "cuelet-empty-state");
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "folder-music-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sound Library");
    adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), "Choose a folder of audio files to start building your soundboard.");
    GtkWidget* emptyChild = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(emptyChild, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(emptyChild, "cuelet-empty-actions");
    GtkWidget* emptyButtons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(emptyButtons, GTK_ALIGN_CENTER);
    emptyChooseButton_ = gtk_button_new_with_label("Choose Library");
    gtk_widget_add_css_class(emptyChooseButton_, "suggested-action");
    gtk_widget_set_tooltip_text(emptyChooseButton_, "Choose Library");
    g_signal_connect_swapped(emptyChooseButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->chooseLibrary();
    }), this);
    emptyImportButton_ = gtk_button_new_with_label("Import Sounds");
    gtk_widget_set_tooltip_text(emptyImportButton_, "Import Sounds");
    g_signal_connect_swapped(emptyImportButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->importSounds();
    }), this);
    emptyClearSearchButton_ = gtk_button_new_with_label("Clear Search");
    gtk_widget_add_css_class(emptyClearSearchButton_, "suggested-action");
    g_signal_connect_swapped(emptyClearSearchButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        gtk_editable_set_text(GTK_EDITABLE(self->searchEntry_), "");
        gtk_widget_grab_focus(self->searchEntry_);
    }), this);
    emptyBrowseButton_ = gtk_button_new_with_label("Browse Library");
    g_signal_connect_swapped(emptyBrowseButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->selection_ = SidebarSelection{};
        self->clearSelection();
        self->refreshSidebar();
        self->refreshContent();
        gtk_widget_grab_focus(self->sidebarList_);
    }), this);
    gtk_box_append(GTK_BOX(emptyButtons), emptyChooseButton_);
    gtk_box_append(GTK_BOX(emptyButtons), emptyImportButton_);
    gtk_box_append(GTK_BOX(emptyButtons), emptyClearSearchButton_);
    gtk_box_append(GTK_BOX(emptyButtons), emptyBrowseButton_);
    emptyHelperLabel_ = gtk_label_new("Supports mp3, wav, ogg, flac, and m4a when codecs are available.");
    gtk_label_set_wrap(GTK_LABEL(emptyHelperLabel_), TRUE);
    gtk_label_set_justify(GTK_LABEL(emptyHelperLabel_), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(emptyHelperLabel_, "caption");
    gtk_widget_add_css_class(emptyHelperLabel_, "dim-label");
    gtk_box_append(GTK_BOX(emptyChild), emptyButtons);
    gtk_box_append(GTK_BOX(emptyChild), emptyHelperLabel_);
    adw_status_page_set_child(ADW_STATUS_PAGE(emptyPage_), emptyChild);
    gtk_stack_add_named(GTK_STACK(stack_), emptyPage_, "empty");

    GtkGesture* emptyClick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(emptyClick), GDK_BUTTON_SECONDARY);
    g_signal_connect(emptyClick, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        GtkWidget* parent = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        GMenu* menu = g_menu_new();
        g_menu_append(menu, "Choose Library", "win.choose-library");
        g_menu_append(menu, "Import Sounds", "win.import-sounds");
        g_menu_append(menu, "Rescan Library", "win.rescan-library");
        g_menu_append(menu, "New Category", "win.new-category");
        GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
        g_object_unref(menu);
        self->presentPopover(popover, parent, x, y);
    }), this);
    gtk_widget_add_controller(emptyPage_, GTK_EVENT_CONTROLLER(emptyClick));

    gtk_box_append(GTK_BOX(contentBox), stack_);

    nowPlayingBar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(nowPlayingBar_, "cuelet-now-playing");
    gtk_widget_set_visible(nowPlayingBar_, FALSE);
    gtk_widget_set_valign(nowPlayingBar_, GTK_ALIGN_CENTER);
    GtkWidget* nowIcon = gtk_image_new_from_icon_name("audio-speakers-symbolic");
    gtk_widget_add_css_class(nowIcon, "cuelet-now-playing-icon");
    gtk_widget_set_valign(nowIcon, GTK_ALIGN_CENTER);
    GtkWidget* nowTextBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(nowTextBox, TRUE);
    gtk_widget_set_valign(nowTextBox, GTK_ALIGN_CENTER);
    nowPlayingLabel_ = gtk_label_new("Nothing playing");
    gtk_label_set_xalign(GTK_LABEL(nowPlayingLabel_), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(nowPlayingLabel_), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(nowPlayingLabel_, "cuelet-now-playing-title");
    nowPlayingCategoryLabel_ = secondaryLabel("");
    gtk_box_append(GTK_BOX(nowTextBox), nowPlayingLabel_);
    gtk_box_append(GTK_BOX(nowTextBox), nowPlayingCategoryLabel_);
    nowPlayingProgress_ = gtk_progress_bar_new();
    gtk_widget_add_css_class(nowPlayingProgress_, "cuelet-now-playing-progress");
    gtk_widget_set_size_request(nowPlayingProgress_, 150, -1);
    gtk_widget_set_valign(nowPlayingProgress_, GTK_ALIGN_CENTER);
    nowPlayingPauseButton_ = iconButton("media-playback-pause-symbolic", "Pause Current Sound");
    gtk_widget_set_valign(nowPlayingPauseButton_, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(nowPlayingPauseButton_, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        const auto playing = self->audio_.playingPaths();
        if (playing.empty()) {
            return;
        }
        const std::string& path = playing.back();
        if (self->audio_.isPaused(path)) {
            self->audio_.resume(path);
        } else {
            self->audio_.pause(path);
        }
        self->refreshContent();
        self->refreshNowPlaying();
    }), this);
    GtkWidget* stopCurrent = iconButton("media-playback-stop-symbolic", "Stop Current Sound");
    gtk_widget_set_valign(stopCurrent, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(stopCurrent, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        const auto playing = self->audio_.playingPaths();
        if (!playing.empty()) {
            self->stopSound(playing.back());
        }
    }), this);
    GtkWidget* stopMini = iconButton("process-stop-symbolic", "Stop All Sounds");
    gtk_widget_add_css_class(stopMini, "destructive-action");
    gtk_widget_set_valign(stopMini, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(stopMini, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->stopAll();
    }), this);
    gtk_box_append(GTK_BOX(nowPlayingBar_), nowIcon);
    gtk_box_append(GTK_BOX(nowPlayingBar_), nowTextBox);
    gtk_box_append(GTK_BOX(nowPlayingBar_), nowPlayingProgress_);
    gtk_box_append(GTK_BOX(nowPlayingBar_), nowPlayingPauseButton_);
    gtk_box_append(GTK_BOX(nowPlayingBar_), stopCurrent);
    gtk_box_append(GTK_BOX(nowPlayingBar_), stopMini);
    gtk_box_append(GTK_BOX(contentBox), nowPlayingBar_);

    AdwNavigationPage* sidebarPage = adw_navigation_page_new(sidebarBox, "Sidebar");
    AdwNavigationPage* contentPage = adw_navigation_page_new(contentBox, "Cuelet");
    adw_navigation_split_view_set_sidebar(splitView_, sidebarPage);
    adw_navigation_split_view_set_content(splitView_, contentPage);

    GtkEventController* windowKeys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(windowKeys, GTK_PHASE_CAPTURE);
    g_signal_connect(windowKeys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer userData) {
        return static_cast<CueletWindow*>(userData)->handleLocalShortcut(keyval, state)
            ? GDK_EVENT_STOP
            : GDK_EVENT_PROPAGATE;
    }), this);
    gtk_widget_add_controller(GTK_WIDGET(window_), windowKeys);
}

void CueletWindow::installActions()
{
    GSimpleActionGroup* group = g_simple_action_group_new();

    GSimpleAction* chooseLibrary = g_simple_action_new("choose-library", nullptr);
    g_signal_connect(chooseLibrary, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->chooseLibrary();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(chooseLibrary));
    g_object_unref(chooseLibrary);

    GSimpleAction* importSounds = g_simple_action_new("import-sounds", nullptr);
    g_signal_connect(importSounds, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->importSounds();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(importSounds));
    g_object_unref(importSounds);

    GSimpleAction* rescan = g_simple_action_new("rescan-library", nullptr);
    g_signal_connect(rescan, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->rescanLibrary();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(rescan));
    g_object_unref(rescan);

    GSimpleAction* stopAllAction = g_simple_action_new("stop-all", nullptr);
    g_signal_connect(stopAllAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->stopAll();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(stopAllAction));
    g_object_unref(stopAllAction);

    GSimpleAction* preferences = g_simple_action_new("preferences", nullptr);
    g_signal_connect(preferences, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->showPreferences();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(preferences));
    g_object_unref(preferences);

    GSimpleAction* newCategory = g_simple_action_new("new-category", nullptr);
    g_signal_connect(newCategory, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer userData) {
        static_cast<CueletWindow*>(userData)->promptNewCategory();
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(newCategory));
    g_object_unref(newCategory);

    GSimpleAction* sort = g_simple_action_new("sort", G_VARIANT_TYPE_STRING);
    g_signal_connect(sort, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.sortOption = sortOptionFromId(g_variant_get_string(parameter, nullptr));
        self->saveSettings();
        self->refreshContent();
        self->showToast("Sorted by " + sortOptionTitle(self->settings_.sortOption));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(sort));
    g_object_unref(sort);

    GSimpleAction* playSoundAction = g_simple_action_new("play-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(playSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->playSound(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(playSoundAction));
    g_object_unref(playSoundAction);

    GSimpleAction* stopSoundAction = g_simple_action_new("stop-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(stopSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->stopSound(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(stopSoundAction));
    g_object_unref(stopSoundAction);

    GSimpleAction* favoriteAction = g_simple_action_new("toggle-favorite", G_VARIANT_TYPE_STRING);
    g_signal_connect(favoriteAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->toggleFavorite(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(favoriteAction));
    g_object_unref(favoriteAction);

    GSimpleAction* assignCategoryAction = g_simple_action_new("assign-category", G_VARIANT_TYPE("(ss)"));
    g_signal_connect(assignCategoryAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        const char* path = "";
        const char* categoryId = "";
        g_variant_get(parameter, "(&s&s)", &path, &categoryId);
        static_cast<CueletWindow*>(userData)->assignCategory(path, categoryId);
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(assignCategoryAction));
    g_object_unref(assignCategoryAction);

    GSimpleAction* newCategoryForSoundAction = g_simple_action_new("new-category-for-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(newCategoryForSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->promptNewCategory(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(newCategoryForSoundAction));
    g_object_unref(newCategoryForSoundAction);

    GSimpleAction* shortcutAction = g_simple_action_new("record-shortcut", G_VARIANT_TYPE_STRING);
    g_signal_connect(shortcutAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->recordShortcut(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(shortcutAction));
    g_object_unref(shortcutAction);

    GSimpleAction* clearShortcutAction = g_simple_action_new("clear-shortcut", G_VARIANT_TYPE_STRING);
    g_signal_connect(clearShortcutAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->clearShortcut(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(clearShortcutAction));
    g_object_unref(clearShortcutAction);

    GSimpleAction* copyShortcutCommandAction = g_simple_action_new("copy-shortcut-command", G_VARIANT_TYPE_STRING);
    g_signal_connect(copyShortcutCommandAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->copyGnomeShortcutCommand(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(copyShortcutCommandAction));
    g_object_unref(copyShortcutCommandAction);

    GSimpleAction* renameSoundAction = g_simple_action_new("rename-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(renameSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->promptRenameSound(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(renameSoundAction));
    g_object_unref(renameSoundAction);

    GSimpleAction* revealSoundAction = g_simple_action_new("reveal-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(revealSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->revealSound(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(revealSoundAction));
    g_object_unref(revealSoundAction);

    GSimpleAction* removeSoundAction = g_simple_action_new("remove-sound", G_VARIANT_TYPE_STRING);
    g_signal_connect(removeSoundAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->confirmRemoveSound(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(removeSoundAction));
    g_object_unref(removeSoundAction);

    GSimpleAction* renameCategoryAction = g_simple_action_new("rename-category", G_VARIANT_TYPE_STRING);
    g_signal_connect(renameCategoryAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->promptRenameCategory(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(renameCategoryAction));
    g_object_unref(renameCategoryAction);

    GSimpleAction* colorCategoryAction = g_simple_action_new("set-category-color", G_VARIANT_TYPE("(ss)"));
    g_signal_connect(colorCategoryAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        const char* categoryId = "";
        const char* colorHex = "";
        g_variant_get(parameter, "(&s&s)", &categoryId, &colorHex);
        static_cast<CueletWindow*>(userData)->setCategoryColor(categoryId, colorHex);
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(colorCategoryAction));
    g_object_unref(colorCategoryAction);

    GSimpleAction* iconCategoryAction = g_simple_action_new("set-category-icon", G_VARIANT_TYPE("(ss)"));
    g_signal_connect(iconCategoryAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        const char* categoryId = "";
        const char* iconId = "";
        g_variant_get(parameter, "(&s&s)", &categoryId, &iconId);
        static_cast<CueletWindow*>(userData)->setCategoryIcon(categoryId, iconId);
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(iconCategoryAction));
    g_object_unref(iconCategoryAction);

    GSimpleAction* deleteCategoryAction = g_simple_action_new("delete-category", G_VARIANT_TYPE_STRING);
    g_signal_connect(deleteCategoryAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* parameter, gpointer userData) {
        static_cast<CueletWindow*>(userData)->confirmDeleteCategory(g_variant_get_string(parameter, nullptr));
    }), this);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(deleteCategoryAction));
    g_object_unref(deleteCategoryAction);

    gtk_widget_insert_action_group(GTK_WIDGET(window_), "win", G_ACTION_GROUP(group));
    g_object_unref(group);
}

void CueletWindow::installCss()
{
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/io/cuelet/linux/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}
