#include "CueletWindow.h"
#include "CueletWindowHelpers.h"

namespace {

enum class PreferencesDeferredAction {
    None,
    RefreshContent,
    RescanLibrary,
    ReloadLibrary,
};

struct PreferencesCallbackContext;

struct PreferencesDeferredWork {
    PreferencesCallbackContext* context = nullptr;
    CueletWindow* self = nullptr;
    GObject* lifetime = nullptr;
    PreferencesDeferredAction action = PreferencesDeferredAction::None;
    void (*run)(CueletWindow*, PreferencesDeferredAction) = nullptr;
};

struct PreferencesCallbackContext {
    CueletWindow* self = nullptr;
    GObject* lifetime = nullptr;
    GtkWidget* viewDropDown = nullptr;
    gulong viewModeHandler = 0;
    PreferencesDeferredWork* pendingWork = nullptr;
    bool closing = false;
    void (*runDeferred)(CueletWindow*, PreferencesDeferredAction) = nullptr;

    void schedule(PreferencesDeferredAction action)
    {
        if (closing || !self) {
            return;
        }

        if (pendingWork) {
            if (static_cast<int>(action) > static_cast<int>(pendingWork->action)) {
                pendingWork->action = action;
            }
            return;
        }

        auto* work = new PreferencesDeferredWork{
            this,
            self,
            G_OBJECT(g_object_ref(lifetime)),
            action,
            runDeferred,
        };
        pendingWork = work;
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer data) -> gboolean {
                auto* work = static_cast<PreferencesDeferredWork*>(data);
                if (work->context) {
                    work->context->pendingWork = nullptr;
                }
                if (work->self && work->run) {
                    work->run(work->self, work->action);
                }
                return G_SOURCE_REMOVE;
            },
            work,
            +[](gpointer data) {
                auto* work = static_cast<PreferencesDeferredWork*>(data);
                g_object_unref(work->lifetime);
                delete work;
            });
    }
};

} // namespace

void CueletWindow::applyAppearanceMode()
{
    AdwStyleManager* styleManager = adw_style_manager_get_default();
    if (settings_.appearanceMode == "dark") {
        adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_FORCE_DARK);
        return;
    }
    if (settings_.appearanceMode == "light") {
        adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_FORCE_LIGHT);
        return;
    }
    adw_style_manager_set_color_scheme(styleManager, ADW_COLOR_SCHEME_DEFAULT);
}

void CueletWindow::showPreferences()
{
    GListModel* topLevels = gtk_window_get_toplevels();
    const guint topLevelCount = g_list_model_get_n_items(topLevels);
    for (guint index = 0; index < topLevelCount; ++index) {
        auto* item = static_cast<GObject*>(g_list_model_get_item(topLevels, index));
        const bool isExistingPreferences = ADW_IS_PREFERENCES_WINDOW(item)
            && gtk_window_get_transient_for(GTK_WINDOW(item)) == GTK_WINDOW(window_);
        if (isExistingPreferences) {
            gtk_window_present(GTK_WINDOW(item));
            g_object_unref(item);
            return;
        }
        if (item) {
            g_object_unref(item);
        }
    }

    GtkWidget* window = adw_preferences_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Preferences");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 640);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    adw_preferences_window_set_search_enabled(ADW_PREFERENCES_WINDOW(window), TRUE);

    auto* context = new PreferencesCallbackContext{this, G_OBJECT(application_)};
    context->runDeferred = +[](CueletWindow* self, PreferencesDeferredAction action) {
        switch (action) {
        case PreferencesDeferredAction::ReloadLibrary:
            if (self->settings_.showsDemoLibrary) {
                self->loadDemoLibrary(true);
            } else if (!self->settings_.libraryPath.empty()) {
                self->loadLibrary(self->settings_.libraryPath);
            } else {
                self->clips_.clear();
                self->categories_ = {cuelet::uncategorizedCategory()};
                self->refreshAll();
            }
            break;
        case PreferencesDeferredAction::RescanLibrary:
            if (!self->libraryPath_.empty()) {
                self->rescanLibrary();
            }
            break;
        case PreferencesDeferredAction::RefreshContent:
            self->refreshContent();
            break;
        case PreferencesDeferredAction::None:
            break;
        }
    };
    g_object_set_data_full(
        G_OBJECT(window),
        "cuelet-preferences-callback-context",
        context,
        +[](gpointer data) {
            auto* context = static_cast<PreferencesCallbackContext*>(data);
            context->closing = true;
            context->self = nullptr;
            if (context->pendingWork) {
                context->pendingWork->context = nullptr;
                context->pendingWork = nullptr;
            }
            if (context->viewDropDown) {
                g_object_remove_weak_pointer(
                    G_OBJECT(context->viewDropDown),
                    reinterpret_cast<gpointer*>(&context->viewDropDown));
            }
            delete context;
        });

    auto addPage = [&](const char* title, const char* icon) {
        GtkWidget* page = adw_preferences_page_new();
        adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), title);
        adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), icon);
        adw_preferences_window_add(ADW_PREFERENCES_WINDOW(window), ADW_PREFERENCES_PAGE(page));
        return page;
    };

    auto addGroup = [](GtkWidget* page, const char* title, const char* description = nullptr) {
        GtkWidget* group = adw_preferences_group_new();
        adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), title);
        if (description) {
            adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), description);
        }
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
        return group;
    };

    GtkWidget* libraryPage = addPage("Library", "folder-symbolic");
    GtkWidget* libraryGroup = addGroup(libraryPage, "Sound Library");
    GtkWidget* pathRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pathRow), "Selected Library");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(pathRow),
        settings_.libraryPath.empty() ? "No library selected" : settings_.libraryPath.c_str());
    GtkWidget* chooseButton = gtk_button_new_with_label("Choose...");
    gtk_widget_set_valign(chooseButton, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(chooseButton, "suggested-action");
    g_signal_connect_swapped(chooseButton, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->chooseLibrary();
    }), this);
    adw_action_row_add_suffix(ADW_ACTION_ROW(pathRow), chooseButton);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(pathRow), chooseButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(libraryGroup), pathRow);

    GtkWidget* scanRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(scanRow), "Scan Subfolders");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(scanRow), "Include nested folders when building the sound library.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(scanRow), settings_.scansSubfolders);
    g_signal_connect(scanRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context || context->closing || !ADW_IS_SWITCH_ROW(object)) {
            return;
        }
        const bool active = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        if (context->self->settings_.scansSubfolders == active) {
            return;
        }
        context->self->settings_.scansSubfolders = active;
        context->self->saveSettings();
        if (!context->self->libraryPath_.empty()) {
            context->schedule(PreferencesDeferredAction::RescanLibrary);
        }
    }), context);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(libraryGroup), scanRow);

    GtkWidget* demoRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(demoRow), "Demo Mode");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(demoRow), "Show sample sounds without selecting a folder.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(demoRow), settings_.showsDemoLibrary);
    g_signal_connect(demoRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context || context->closing || !ADW_IS_SWITCH_ROW(object)) {
            return;
        }
        const bool active = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        if (context->self->settings_.showsDemoLibrary == active) {
            return;
        }
        context->self->settings_.showsDemoLibrary = active;
        context->self->saveSettings();
        context->schedule(PreferencesDeferredAction::ReloadLibrary);
    }), context);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(libraryGroup), demoRow);

    GtkWidget* supportedRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(supportedRow), "Supported Formats");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(supportedRow), "mp3, wav, ogg, flac, m4a, aif, and aiff when GStreamer codecs are available.");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(libraryGroup), supportedRow);

    GtkWidget* playbackPage = addPage("Playback", "media-playback-start-symbolic");
    GtkWidget* playbackGroup = addGroup(playbackPage, "Playback");
    GtkWidget* volumeRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(volumeRow), "Volume");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(volumeRow), "Default volume for newly played sounds.");
    GtkWidget* volumeScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_range_set_value(GTK_RANGE(volumeScale), settings_.volume * 100.0);
    gtk_widget_set_valign(volumeScale, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(volumeScale, 220, -1);
    adw_action_row_add_suffix(ADW_ACTION_ROW(volumeRow), volumeScale);
    g_signal_connect(volumeScale, "value-changed", G_CALLBACK(+[](GtkRange* range, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.volume = gtk_range_get_value(range) / 100.0;
        self->audio_.setVolume(self->settings_.volume);
        self->saveSettings();
    }), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(playbackGroup), volumeRow);

    GtkWidget* multipleRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(multipleRow), "Allow Multiple Sounds");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(multipleRow), "Keep existing playback running when another sound starts.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(multipleRow), settings_.allowsSimultaneousPlayback);
    g_signal_connect(multipleRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.allowsSimultaneousPlayback = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        self->audio_.setAllowsSimultaneousPlayback(self->settings_.allowsSimultaneousPlayback);
        self->saveSettings();
    }), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(playbackGroup), multipleRow);

    GtkWidget* stopRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(stopRow), "Stop All Sounds");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(stopRow), "Immediately stop every active sound.");
    GtkWidget* stopButton = gtk_button_new_with_label("Stop All");
    gtk_widget_set_valign(stopButton, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(stopButton, "destructive-action");
    gtk_widget_set_sensitive(stopButton, !audio_.playingPaths().empty());
    g_signal_connect_swapped(stopButton, "clicked", G_CALLBACK(+[](CueletWindow* self) {
        self->stopAll();
    }), this);
    adw_action_row_add_suffix(ADW_ACTION_ROW(stopRow), stopButton);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(stopRow), stopButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(playbackGroup), stopRow);

    GtkWidget* appearancePage = addPage("Appearance", "preferences-desktop-theme-symbolic");
    GtkWidget* appearanceGroup = addGroup(appearancePage, "Display");
    GtkWidget* followSystemRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(followSystemRow), "Follow System Theme");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(followSystemRow), "When disabled, Cuelet uses a dark appearance.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(followSystemRow), settings_.appearanceMode != "dark");
    g_signal_connect(followSystemRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.appearanceMode = adw_switch_row_get_active(ADW_SWITCH_ROW(object)) ? "system" : "dark";
        self->applyAppearanceMode();
        self->saveSettings();
    }), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearanceGroup), followSystemRow);

    GtkWidget* viewModeRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(viewModeRow), "Default View");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(viewModeRow), "Choose how library results are shown.");
    const char* viewModes[] = {"Grid", "List", nullptr};
    GtkWidget* viewDropDown = gtk_drop_down_new_from_strings(viewModes);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(viewDropDown), settings_.viewMode == "list" ? 1 : 0);
    gtk_widget_set_valign(viewDropDown, GTK_ALIGN_CENTER);
    context->viewDropDown = viewDropDown;
    g_object_add_weak_pointer(
        G_OBJECT(viewDropDown),
        reinterpret_cast<gpointer*>(&context->viewDropDown));
    context->viewModeHandler = g_signal_connect_data(
        viewDropDown,
        "notify::selected",
        G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
            auto* context = static_cast<PreferencesCallbackContext*>(userData);
            if (!context || context->closing || !GTK_IS_DROP_DOWN(object)) {
                return;
            }

            auto* dropDown = GTK_DROP_DOWN(object);
            GListModel* model = gtk_drop_down_get_model(dropDown);
            if (!G_IS_LIST_MODEL(model)) {
                return;
            }

            const guint selected = gtk_drop_down_get_selected(dropDown);
            if (selected == GTK_INVALID_LIST_POSITION || selected > 1) {
                return;
            }

            const std::string mode = selected == 1 ? "list" : "grid";
            if (context->self->settings_.viewMode == mode) {
                return;
            }

            context->self->settings_.viewMode = mode;
            context->self->saveSettings();
            context->schedule(PreferencesDeferredAction::RefreshContent);
        }),
        context,
        nullptr,
        G_CONNECT_DEFAULT);
    adw_action_row_add_suffix(ADW_ACTION_ROW(viewModeRow), viewDropDown);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(viewModeRow), viewDropDown);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearanceGroup), viewModeRow);

    GtkWidget* extensionRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(extensionRow), "Show File Extensions");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(extensionRow), "Display full file names in grid and list views.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(extensionRow), settings_.showFileExtensions);
    g_signal_connect(extensionRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context || context->closing || !ADW_IS_SWITCH_ROW(object)) {
            return;
        }
        const bool active = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        if (context->self->settings_.showFileExtensions == active) {
            return;
        }
        context->self->settings_.showFileExtensions = active;
        context->self->saveSettings();
        context->schedule(PreferencesDeferredAction::RefreshContent);
    }), context);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearanceGroup), extensionRow);

    GtkWidget* importPage = addPage("Import Behavior", "document-open-symbolic");
    GtkWidget* importGroup = addGroup(importPage, "Import Behavior");
    GtkWidget* copyRow = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(copyRow), "Copy Imported Files into Library");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(copyRow), "Imported audio is copied into the selected library folder.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(copyRow), settings_.copiesImportedFiles);
    g_signal_connect(copyRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.copiesImportedFiles = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        self->saveSettings();
    }), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(importGroup), copyRow);

    GtkWidget* audioPage = addPage("Audio", "audio-speakers-symbolic");
    GtkWidget* audioGroup = addGroup(audioPage, "Output");
    GtkWidget* outputRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(outputRow), "Output Device");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(outputRow), "System default through GStreamer. PipeWire device routing is not wired yet.");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(audioGroup), outputRow);

    GtkWidget* shortcutsPage = addPage("Shortcuts", "input-keyboard-symbolic");
    const char* sessionType = g_getenv("XDG_SESSION_TYPE");
    const bool isWayland = g_strcmp0(sessionType, "wayland") == 0;
    GtkWidget* shortcutsGroup = addGroup(
        shortcutsPage,
        "Assigned Shortcuts",
        isWayland
            ? "Local shortcuts work while Cuelet is focused. For GNOME global shortcuts, copy a sound command from its context menu and assign it in Settings → Keyboard → Custom Shortcuts."
            : "Local shortcuts work while Cuelet is focused. Copy a GNOME shortcut command from a sound's context menu for desktop-wide shortcuts.");
    int shortcutCount = 0;
    for (const auto& clip : clips_) {
        if (!clip.shortcut) {
            continue;
        }
        ++shortcutCount;
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), clip.searchableName().c_str());
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), clip.shortcut->label.c_str());
        GtkWidget* clearButton = gtk_button_new_from_icon_name("edit-clear-symbolic");
        gtk_widget_set_tooltip_text(clearButton, "Clear Shortcut");
        gtk_widget_set_valign(clearButton, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(clearButton, "flat");
        auto* data = new cuelet_linux::WindowStringData{this, clip.relativePath};
        g_signal_connect_data(clearButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
            auto* data = static_cast<cuelet_linux::WindowStringData*>(userData);
            data->self->clearShortcut(data->value);
        }), data, +[](gpointer userData, GClosure*) {
            delete static_cast<cuelet_linux::WindowStringData*>(userData);
        }, G_CONNECT_DEFAULT);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), clearButton);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(shortcutsGroup), row);
    }
    if (shortcutCount == 0) {
        GtkWidget* emptyShortcutRow = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(emptyShortcutRow), "No Shortcuts Assigned");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(emptyShortcutRow), "Assign shortcuts from a sound context menu.");
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(shortcutsGroup), emptyShortcutRow);
    }

    GtkWidget* shortcutToolsGroup = addGroup(shortcutsPage, "Shortcut Tools");
    GtkWidget* clearAllRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(clearAllRow), "Clear All Shortcuts");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(clearAllRow), "Remove every per-sound shortcut from the current library.");
    GtkWidget* clearAllButton = gtk_button_new_with_label("Clear All");
    gtk_widget_set_valign(clearAllButton, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(clearAllButton, "destructive-action");
    gtk_widget_set_sensitive(clearAllButton, shortcutCount > 0);
    g_signal_connect(clearAllButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context || context->closing) {
            return;
        }
        for (auto& clip : context->self->clips_) {
            clip.shortcut.reset();
        }
        context->self->saveMetadata();
        context->schedule(PreferencesDeferredAction::RefreshContent);
        context->self->showToast("Cleared all shortcuts.");
    }), context);
    adw_action_row_add_suffix(ADW_ACTION_ROW(clearAllRow), clearAllButton);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(clearAllRow), clearAllButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(shortcutToolsGroup), clearAllRow);

    GtkWidget* advancedPage = addPage("Advanced", "applications-system-symbolic");
    GtkWidget* pathsGroup = addGroup(advancedPage, "Paths");
    GtkWidget* metadataRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(metadataRow), "Library Metadata");
    const auto metadataPath = libraryPath_.empty()
        ? std::string("No active library")
        : cuelet::MetadataStore::metadataPathForLibrary(libraryPath_).string();
    adw_action_row_set_subtitle(ADW_ACTION_ROW(metadataRow), metadataPath.c_str());
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(pathsGroup), metadataRow);

    GtkWidget* settingsRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(settingsRow), "Linux Settings");
    const auto settingsPath = settingsStore_.filePath().string();
    adw_action_row_set_subtitle(ADW_ACTION_ROW(settingsRow), settingsPath.c_str());
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(pathsGroup), settingsRow);

    GtkWidget* maintenanceGroup = addGroup(advancedPage, "Maintenance");
    GtkWidget* rescanRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(rescanRow), "Rescan Library");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(rescanRow), "Refresh the current library from disk.");
    GtkWidget* rescanButton = gtk_button_new_with_label("Rescan");
    gtk_widget_set_valign(rescanButton, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(rescanButton, !libraryPath_.empty());
    g_signal_connect(rescanButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context || context->closing) {
            return;
        }
        context->schedule(PreferencesDeferredAction::RescanLibrary);
    }), context);
    adw_action_row_add_suffix(ADW_ACTION_ROW(rescanRow), rescanButton);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(rescanRow), rescanButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(maintenanceGroup), rescanRow);

    g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer userData) -> gboolean {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context) {
            return FALSE;
        }

        context->closing = true;
        if (context->viewModeHandler != 0 && context->viewDropDown) {
            g_signal_handler_disconnect(context->viewDropDown, context->viewModeHandler);
            context->viewModeHandler = 0;
        }
        return FALSE;
    }), context);

    gtk_window_present(GTK_WINDOW(window));
}
