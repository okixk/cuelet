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

struct VirtualMicrophonePreferenceData {
    CueletWindow* self = nullptr;
    GtkWidget* enabledRow = nullptr;
    GtkWidget* diagnosticRow = nullptr;
    GtkWidget* endpointRow = nullptr;
    GtkWidget* backendDropDown = nullptr;
    GtkWidget* targetEntry = nullptr;
    GtkWidget* applyOutputButton = nullptr;
    bool changing = false;
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
    if (preferencesDialog_) {
        adw_dialog_present(preferencesDialog_, GTK_WIDGET(window_));
        return;
    }

    AdwDialog* dialog = adw_preferences_dialog_new();
    preferencesDialog_ = dialog;
    g_object_add_weak_pointer(
        G_OBJECT(dialog),
        reinterpret_cast<gpointer*>(&preferencesDialog_));
    adw_dialog_set_title(dialog, "Preferences");
    adw_dialog_set_content_width(dialog, 720);
    adw_dialog_set_content_height(dialog, 640);
    adw_preferences_dialog_set_search_enabled(ADW_PREFERENCES_DIALOG(dialog), TRUE);

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
        G_OBJECT(dialog),
        "cuelet-preferences-callback-context",
        context,
        +[](gpointer data) {
            auto* context = static_cast<PreferencesCallbackContext*>(data);
            context->closing = true;
            context->self = nullptr;
            if (context->pendingWork) {
                context->pendingWork->context = nullptr;
                context->pendingWork->self = nullptr;
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
        adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));
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
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(copyRow),
        "Turn off to link original files in place. Linked sources are never moved or deleted by import.");
    adw_switch_row_set_active(ADW_SWITCH_ROW(copyRow), settings_.copiesImportedFiles);
    g_signal_connect(copyRow, "notify::active", G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        self->settings_.copiesImportedFiles = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
        self->saveSettings();
    }), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(importGroup), copyRow);

    GtkWidget* audioPage = addPage("Audio", "audio-speakers-symbolic");
    adw_preferences_page_set_name(ADW_PREFERENCES_PAGE(audioPage), "audio");
    GtkWidget* audioGroup = addGroup(
        audioPage,
        "Output",
        "Automatic follows the desktop default. Explicit targets are stored as "
        "pipewire:target-object or pulseaudio:device and never change the system default.");
    LinuxAudioService::OutputSelection savedOutput;
    const bool hasSavedOutput =
        parseOutputSetting(settings_.outputDevice, savedOutput);

    GtkWidget* backendRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(backendRow), "Output Backend");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(backendRow),
        "Choose automatic output, or target an existing session device.");
    const char* backendChoices[] = {
        "Automatic",
        "PipeWire",
        "PulseAudio",
        nullptr,
    };
    GtkWidget* backendDropDown = gtk_drop_down_new_from_strings(backendChoices);
    gtk_widget_set_valign(backendDropDown, GTK_ALIGN_CENTER);
    guint backendIndex = 0;
    if (hasSavedOutput && savedOutput.backend == LinuxAudioService::OutputBackend::PipeWire) {
        backendIndex = 1;
    } else if (hasSavedOutput
               && savedOutput.backend == LinuxAudioService::OutputBackend::PulseAudio) {
        backendIndex = 2;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(backendDropDown), backendIndex);
    adw_action_row_add_suffix(ADW_ACTION_ROW(backendRow), backendDropDown);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(backendRow), backendDropDown);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(audioGroup), backendRow);

    GtkWidget* targetRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(targetRow), "Device Target");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(targetRow),
        "Use a PipeWire target-object or PulseAudio device identifier from your current session.");
    GtkWidget* targetEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(targetEntry), "Device identifier");
    gtk_widget_set_size_request(targetEntry, 240, -1);
    gtk_widget_set_valign(targetEntry, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(targetEntry, backendIndex != 0);
    if (hasSavedOutput) {
        gtk_editable_set_text(GTK_EDITABLE(targetEntry), savedOutput.deviceId.c_str());
    }
    g_signal_connect(backendDropDown, "notify::selected", G_CALLBACK(+[](
        GObject* object,
        GParamSpec*,
        gpointer userData) {
        gtk_widget_set_sensitive(
            GTK_WIDGET(userData),
            gtk_drop_down_get_selected(GTK_DROP_DOWN(object)) != 0);
    }), targetEntry);
    adw_action_row_add_suffix(ADW_ACTION_ROW(targetRow), targetEntry);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(targetRow), targetEntry);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(audioGroup), targetRow);

    GtkWidget* applyOutputRow = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(applyOutputRow), "Apply Output");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(applyOutputRow),
        "Stop playback before changing output. The selected target is used only by Cuelet.");
    GtkWidget* applyOutputButton = gtk_button_new_with_label("Apply");
    gtk_widget_add_css_class(applyOutputButton, "suggested-action");
    gtk_widget_set_valign(applyOutputButton, GTK_ALIGN_CENTER);
    auto* outputData = new cuelet_linux::OutputPreferenceData{
        this,
        backendDropDown,
        targetEntry,
    };
    g_signal_connect_data(applyOutputButton, "clicked", G_CALLBACK(+[](
        GtkButton*,
        gpointer userData) {
        auto* data = static_cast<cuelet_linux::OutputPreferenceData*>(userData);
        if (!data || !data->self) {
            return;
        }
        LinuxAudioService::OutputSelection selection;
        const guint backend =
            gtk_drop_down_get_selected(GTK_DROP_DOWN(data->backendDropDown));
        if (backend == 1) {
            selection.backend = LinuxAudioService::OutputBackend::PipeWire;
        } else if (backend == 2) {
            selection.backend = LinuxAudioService::OutputBackend::PulseAudio;
        }
        if (selection.backend != LinuxAudioService::OutputBackend::Automatic) {
            selection.deviceId = cuelet::trim(
                gtk_editable_get_text(GTK_EDITABLE(data->targetEntry)));
            if (selection.deviceId.empty()) {
                data->self->showError("Enter an output device identifier.");
                return;
            }
        }
        if (!data->self->audio_.setOutputSelection(selection)) {
            return;
        }
        data->self->settings_.outputDevice = outputSetting(selection);
        data->self->saveSettings();
        data->self->showToast(
            selection.backend == LinuxAudioService::OutputBackend::Automatic
                ? "Using automatic audio output."
                : "Audio output target updated.");
    }), outputData, +[](gpointer data, GClosure*) {
        delete static_cast<cuelet_linux::OutputPreferenceData*>(data);
    }, G_CONNECT_DEFAULT);
    adw_action_row_add_suffix(ADW_ACTION_ROW(applyOutputRow), applyOutputButton);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(applyOutputRow), applyOutputButton);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(audioGroup), applyOutputRow);

    GtkWidget* virtualMicrophoneGroup = addGroup(
        audioPage,
        "Virtual Microphone",
        "Creates a temporary Cuelet-owned PipeWire sink/source pair for this "
        "user session. Cuelet never changes the desktop defaults or writes "
        "PipeWire configuration. Receiving-application compatibility must be "
        "verified on that application.");
    GtkWidget* virtualMicrophoneRow = adw_switch_row_new();
    adw_preferences_row_set_title(
        ADW_PREFERENCES_ROW(virtualMicrophoneRow),
        "Enable Temporary Cuelet Route");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(virtualMicrophoneRow),
        "While enabled, Cuelet playback is sent to the temporary virtual "
        "microphone instead of local speakers.");
    const bool virtualMicrophoneIsActive = virtualMicrophoneActive();
    adw_switch_row_set_active(
        ADW_SWITCH_ROW(virtualMicrophoneRow),
        virtualMicrophoneIsActive);
    adw_preferences_group_add(
        ADW_PREFERENCES_GROUP(virtualMicrophoneGroup),
        virtualMicrophoneRow);

    GtkWidget* virtualMicrophoneDiagnosticRow = adw_action_row_new();
    adw_preferences_row_set_title(
        ADW_PREFERENCES_ROW(virtualMicrophoneDiagnosticRow),
        "PipeWire Diagnostics");
    const auto initialVirtualMicrophoneStatus = virtualMicrophoneStatus();
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(virtualMicrophoneDiagnosticRow),
        initialVirtualMicrophoneStatus.c_str());
    GtkWidget* refreshDiagnosticButton = gtk_button_new_with_label("Refresh");
    gtk_widget_set_valign(refreshDiagnosticButton, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(
        ADW_ACTION_ROW(virtualMicrophoneDiagnosticRow),
        refreshDiagnosticButton);
    adw_action_row_set_activatable_widget(
        ADW_ACTION_ROW(virtualMicrophoneDiagnosticRow),
        refreshDiagnosticButton);
    adw_preferences_group_add(
        ADW_PREFERENCES_GROUP(virtualMicrophoneGroup),
        virtualMicrophoneDiagnosticRow);

    GtkWidget* virtualMicrophoneEndpointRow = adw_action_row_new();
    adw_preferences_row_set_title(
        ADW_PREFERENCES_ROW(virtualMicrophoneEndpointRow),
        "Virtual Input Endpoint");
    const auto initialVirtualMicrophoneEndpoint = virtualMicrophoneEndpoint();
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(virtualMicrophoneEndpointRow),
        initialVirtualMicrophoneEndpoint.c_str());
    adw_preferences_group_add(
        ADW_PREFERENCES_GROUP(virtualMicrophoneGroup),
        virtualMicrophoneEndpointRow);

    auto* virtualMicrophoneData = new VirtualMicrophonePreferenceData{
        this,
        virtualMicrophoneRow,
        virtualMicrophoneDiagnosticRow,
        virtualMicrophoneEndpointRow,
        backendDropDown,
        targetEntry,
        applyOutputButton,
        false,
    };
    g_object_set_data_full(
        G_OBJECT(dialog),
        "cuelet-virtual-microphone-preferences",
        virtualMicrophoneData,
        +[](gpointer data) {
            delete static_cast<VirtualMicrophonePreferenceData*>(data);
        });
    gtk_widget_set_sensitive(backendDropDown, !virtualMicrophoneIsActive);
    gtk_widget_set_sensitive(
        targetEntry,
        !virtualMicrophoneIsActive && backendIndex != 0);
    gtk_widget_set_sensitive(applyOutputButton, !virtualMicrophoneIsActive);

    g_signal_connect(
        virtualMicrophoneRow,
        "notify::active",
        G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
            auto* data = static_cast<VirtualMicrophonePreferenceData*>(userData);
            if (!data || data->changing || !ADW_IS_SWITCH_ROW(object)) {
                return;
            }

            data->changing = true;
            const bool requested =
                adw_switch_row_get_active(ADW_SWITCH_ROW(object));
            const bool operationSucceeded = requested
                ? data->self->enableVirtualMicrophone()
                : data->self->disableVirtualMicrophone();
            bool active = data->self->virtualMicrophoneActive();
            if (!active && data->self->virtualMicrophoneNeedsCleanup()) {
                data->self->disableVirtualMicrophone();
                active = false;
            }
            if (!operationSucceeded || requested != active) {
                adw_switch_row_set_active(ADW_SWITCH_ROW(object), active);
            }
            const auto status = data->self->virtualMicrophoneStatus();
            const auto endpoint = data->self->virtualMicrophoneEndpoint();
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(data->diagnosticRow),
                status.c_str());
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(data->endpointRow),
                endpoint.c_str());
            gtk_widget_set_sensitive(data->backendDropDown, !active);
            const guint backend = gtk_drop_down_get_selected(
                GTK_DROP_DOWN(data->backendDropDown));
            gtk_widget_set_sensitive(
                data->targetEntry,
                !active && backend != 0);
            gtk_widget_set_sensitive(data->applyOutputButton, !active);
            data->changing = false;
        }),
        virtualMicrophoneData);

    g_signal_connect(
        refreshDiagnosticButton,
        "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer userData) {
            auto* data = static_cast<VirtualMicrophonePreferenceData*>(userData);
            if (!data || data->changing) {
                return;
            }
            data->changing = true;
            bool active = data->self->virtualMicrophoneActive();
            if (!active && data->self->virtualMicrophoneNeedsCleanup()) {
                data->self->disableVirtualMicrophone();
                active = false;
            }
            adw_switch_row_set_active(
                ADW_SWITCH_ROW(data->enabledRow),
                active);
            const auto status = data->self->virtualMicrophoneStatus();
            const auto endpoint = data->self->virtualMicrophoneEndpoint();
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(data->diagnosticRow),
                status.c_str());
            adw_action_row_set_subtitle(
                ADW_ACTION_ROW(data->endpointRow),
                endpoint.c_str());
            gtk_widget_set_sensitive(data->backendDropDown, !active);
            const guint backend = gtk_drop_down_get_selected(
                GTK_DROP_DOWN(data->backendDropDown));
            gtk_widget_set_sensitive(
                data->targetEntry,
                !active && backend != 0);
            gtk_widget_set_sensitive(data->applyOutputButton, !active);
            data->changing = false;
        }),
        virtualMicrophoneData);

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

    g_signal_connect(dialog, "closed", G_CALLBACK(+[](AdwDialog*, gpointer userData) {
        auto* context = static_cast<PreferencesCallbackContext*>(userData);
        if (!context) {
            return;
        }

        context->closing = true;
        if (context->viewModeHandler != 0 && context->viewDropDown) {
            g_signal_handler_disconnect(context->viewDropDown, context->viewModeHandler);
            context->viewModeHandler = 0;
        }
    }), context);

    adw_dialog_present(dialog, GTK_WIDGET(window_));
}
