#include "CueletWindow.h"
#include "CueletWindowHelpers.h"

#include <algorithm>
#include <ctime>
#include <utility>

using namespace cuelet_linux;

namespace {

GtkWidget* makeSidebarSectionRow(const char* title)
{
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    g_object_set_data(G_OBJECT(row), "sidebar-section", GINT_TO_POINTER(TRUE));

    GtkWidget* label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "caption-heading");
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 4);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 14);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    return row;
}


} // namespace

void CueletWindow::refreshAll()
{
    refreshSidebar();
    refreshContent();
    refreshHeader();
    refreshNowPlaying();
}

void CueletWindow::refreshSidebar()
{
    gtk_list_box_remove_all(GTK_LIST_BOX(sidebarList_));
    GtkListBoxRow* selectedRow = nullptr;
    auto isSelectedRow = [&](GtkWidget* row) {
        const auto kind = static_cast<SidebarKind>(
            GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "sidebar-kind")));
        if (kind != selection_.kind) {
            return false;
        }
        if (kind == SidebarKind::Category) {
            return objectString(G_OBJECT(row), "category-id") == selection_.categoryId;
        }
        return true;
    };
    auto appendSelectable = [&](GtkWidget* row) {
        gtk_list_box_append(GTK_LIST_BOX(sidebarList_), row);
        if (isSelectedRow(row)) {
            selectedRow = GTK_LIST_BOX_ROW(row);
        }
    };

    gtk_list_box_append(GTK_LIST_BOX(sidebarList_), makeSidebarSectionRow("Library"));
    appendSelectable(makeSidebarRow("Library", "view-grid-symbolic", SidebarKind::Library));
    appendSelectable(makeSidebarRow("Favorites", "starred-symbolic", SidebarKind::Favorites));
    appendSelectable(makeSidebarRow("Recent", "document-open-recent-symbolic", SidebarKind::Recent));

    gtk_list_box_append(GTK_LIST_BOX(sidebarList_), makeSidebarSectionRow("Categories"));
    appendSelectable(makeSidebarRow("All Categories", "folder-symbolic", SidebarKind::AllCategories));

    for (const auto& category : categories_) {
        appendSelectable(makeSidebarRow(category.name, linuxCategoryIconName(category.iconName).c_str(), SidebarKind::Category, category.id));
    }

    if (selectedRow) {
        gtk_list_box_select_row(GTK_LIST_BOX(sidebarList_), selectedRow);
    }
}

void CueletWindow::refreshContent()
{
    suppressToggleSignals_ = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gridToggle_), settings_.viewMode == "grid");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(listToggle_), settings_.viewMode == "list");
    suppressToggleSignals_ = false;

    const auto clips = visibleClips();
    gtk_flow_box_remove_all(GTK_FLOW_BOX(flowBox_));
    gtk_list_box_remove_all(GTK_LIST_BOX(listBox_));

    for (const auto& clip : clips) {
        GtkWidget* card = makeSoundCard(clip);
        gtk_flow_box_append(GTK_FLOW_BOX(flowBox_), card);
        GtkWidget* gridCell = gtk_widget_get_parent(card);
        if (GTK_IS_FLOW_BOX_CHILD(gridCell)) {
            gtk_widget_set_focusable(gridCell, TRUE);
            setObjectString(G_OBJECT(gridCell), "relative-path", clip.relativePath);
            const std::string accessibleLabel =
                objectString(G_OBJECT(card), "accessible-label");
            const std::string accessibleDescription =
                objectString(G_OBJECT(card), "accessible-description");
            gtk_accessible_update_property(
                GTK_ACCESSIBLE(gridCell),
                GTK_ACCESSIBLE_PROPERTY_LABEL, accessibleLabel.c_str(),
                GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, accessibleDescription.c_str(),
                -1);
        }
        gtk_list_box_append(GTK_LIST_BOX(listBox_), makeSoundRow(clip));
    }
    refreshSelectionVisuals();

    if (clips.empty()) {
        const char* searchText = gtk_editable_get_text(GTK_EDITABLE(searchEntry_));
        const bool hasSearch = searchText && *searchText;
        const bool hasLibrary = !libraryPath_.empty();

        gtk_widget_set_visible(emptyChooseButton_, FALSE);
        gtk_widget_set_visible(emptyImportButton_, FALSE);
        gtk_widget_set_visible(emptyClearSearchButton_, FALSE);
        gtk_widget_set_visible(emptyBrowseButton_, FALSE);
        gtk_widget_set_visible(emptyHelperLabel_, TRUE);

        if (!missingLibraryPath_.empty()) {
            adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "dialog-warning-symbolic");
            adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "Library Folder Not Found");
            const std::string description =
                "Cuelet could not find “" + missingLibraryPath_.string()
                + "”. Choose the folder again or select another library.";
            adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), description.c_str());
            gtk_label_set_text(
                GTK_LABEL(emptyHelperLabel_),
                "Your saved library choice is unchanged until another folder is selected.");
            gtk_widget_set_visible(emptyChooseButton_, TRUE);
        } else if (!hasLibrary) {
            adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "folder-music-symbolic");
            adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sound Library");
            adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), "Choose a folder of audio files to start building your soundboard.");
            gtk_label_set_text(
                GTK_LABEL(emptyHelperLabel_),
                "Supports mp3, wav, ogg, flac, m4a, aif, and aiff when codecs are available.");
            gtk_widget_set_visible(emptyChooseButton_, TRUE);
        } else if (hasSearch) {
            adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "edit-find-symbolic");
            adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Search Results");
            const std::string description =
                "No sounds match “" + std::string(searchText) + "” in this view.";
            adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), description.c_str());
            gtk_label_set_text(
                GTK_LABEL(emptyHelperLabel_),
                "Search includes sound names, filenames, notes, aliases, and categories.");
            gtk_widget_set_visible(emptyClearSearchButton_, TRUE);
        } else {
            switch (selection_.kind) {
            case SidebarKind::Favorites:
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "non-starred-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Favorite Sounds");
                adw_status_page_set_description(
                    ADW_STATUS_PAGE(emptyPage_),
                    "Mark sounds as favorites to keep them close at hand.");
                gtk_label_set_text(
                    GTK_LABEL(emptyHelperLabel_),
                    "Use the star button on a sound or choose Favorite from its menu.");
                gtk_widget_set_visible(emptyBrowseButton_, TRUE);
                break;
            case SidebarKind::Recent:
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "document-open-recent-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "Nothing Played Yet");
                adw_status_page_set_description(
                    ADW_STATUS_PAGE(emptyPage_),
                    "Sounds appear here after you play them.");
                gtk_label_set_text(
                    GTK_LABEL(emptyHelperLabel_),
                    "Browse the library and play a sound to start your recent history.");
                gtk_widget_set_visible(emptyBrowseButton_, TRUE);
                break;
            case SidebarKind::Category: {
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "folder-music-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sounds in This Category");
                const std::string description =
                    "Assign sounds to “" + categoryName(selection_.categoryId)
                    + "” from a sound menu.";
                adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), description.c_str());
                gtk_label_set_text(
                    GTK_LABEL(emptyHelperLabel_),
                    "You can also import supported audio files into the current library.");
                gtk_widget_set_visible(emptyBrowseButton_, TRUE);
                gtk_widget_set_visible(emptyImportButton_, !libraryPath_.empty());
                break;
            }
            case SidebarKind::AllCategories:
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "folder-music-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Categorized Sounds");
                adw_status_page_set_description(
                    ADW_STATUS_PAGE(emptyPage_),
                    "Import sounds, then organize them with categories.");
                gtk_label_set_text(
                    GTK_LABEL(emptyHelperLabel_),
                    "Create and edit categories from the navigation menu.");
                gtk_widget_set_visible(emptyImportButton_, !libraryPath_.empty());
                break;
            case SidebarKind::Library:
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(emptyPage_), "folder-music-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sounds Yet");
                adw_status_page_set_description(
                    ADW_STATUS_PAGE(emptyPage_),
                    "Import supported audio files to start building your soundboard.");
                gtk_label_set_text(
                    GTK_LABEL(emptyHelperLabel_),
                    "Supports mp3, wav, ogg, flac, m4a, aif, and aiff when codecs are available.");
                gtk_widget_set_visible(emptyImportButton_, !libraryPath_.empty());
                break;
            }
        }
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), "empty");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), settings_.viewMode == "list" ? "list" : "grid");
    }

    refreshHeader();
}

void CueletWindow::refreshHeader()
{
    std::string title = "Library";
    switch (selection_.kind) {
    case SidebarKind::Library:
        title = "Library";
        break;
    case SidebarKind::Favorites:
        title = "Favorites";
        break;
    case SidebarKind::Recent:
        title = "Recent";
        break;
    case SidebarKind::AllCategories:
        title = "All Categories";
        break;
    case SidebarKind::Category:
        title = categoryName(selection_.categoryId);
        break;
    }

    gtk_label_set_text(GTK_LABEL(titleLabel_), title.c_str());
    std::string subtitle = !missingLibraryPath_.empty()
        ? "Library folder unavailable"
        : (libraryPath_.empty() ? "No library selected" : libraryPath_.string());
    const auto visible = visibleClips();
    const auto missingCount = std::count_if(visible.begin(), visible.end(), [](const auto& clip) {
        return clip.missing;
    });
    if (missingCount > 0) {
        subtitle += missingCount == 1
            ? " • 1 file missing"
            : " • " + std::to_string(missingCount) + " files missing";
    }
    gtk_label_set_text(GTK_LABEL(subtitleLabel_), subtitle.c_str());
    const auto count = visible.size();
    const std::string countText = count == 1 ? "1 sound" : std::to_string(count) + " sounds";
    gtk_label_set_text(GTK_LABEL(countLabel_), countText.c_str());
    if (headerTitle_) {
        adw_window_title_set_title(ADW_WINDOW_TITLE(headerTitle_), title.c_str());
        adw_window_title_set_subtitle(ADW_WINDOW_TITLE(headerTitle_), subtitle.c_str());
    }
}

void CueletWindow::refreshNowPlaying()
{
    const auto playing = audio_.playingPaths();
    gtk_widget_set_visible(nowPlayingBar_, !playing.empty());
    if (stopAllButton_) {
        gtk_widget_set_sensitive(stopAllButton_, !playing.empty());
    }
    if (playing.empty()) {
        gtk_label_set_text(GTK_LABEL(nowPlayingLabel_), "Nothing playing");
        if (nowPlayingCategoryLabel_) {
            gtk_label_set_text(GTK_LABEL(nowPlayingCategoryLabel_), "");
        }
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(nowPlayingProgress_), 0.0);
        if (nowPlayingPauseButton_) {
            gtk_button_set_icon_name(
                GTK_BUTTON(nowPlayingPauseButton_),
                "media-playback-pause-symbolic");
            gtk_widget_set_tooltip_text(nowPlayingPauseButton_, "Pause Current Sound");
            gtk_accessible_update_property(
                GTK_ACCESSIBLE(nowPlayingPauseButton_),
                GTK_ACCESSIBLE_PROPERTY_LABEL, "Pause Current Sound",
                -1);
        }
        if (progressTickId_ != 0) {
            g_source_remove(progressTickId_);
            progressTickId_ = 0;
        }
        return;
    }

    const cuelet::SoundClip* clip = clipByPath(playing.back());
    const bool paused = audio_.isPaused(playing.back());
    const std::string label = playing.size() == 1 && clip
        ? (paused ? "Paused " : "Playing ") + clip->searchableName()
        : "Playing " + std::to_string(playing.size()) + " sounds";
    gtk_label_set_text(GTK_LABEL(nowPlayingLabel_), label.c_str());
    if (nowPlayingPauseButton_) {
        const char* iconName = paused
            ? "media-playback-start-symbolic"
            : "media-playback-pause-symbolic";
        const char* buttonLabel = paused ? "Resume Current Sound" : "Pause Current Sound";
        gtk_button_set_icon_name(GTK_BUTTON(nowPlayingPauseButton_), iconName);
        gtk_widget_set_tooltip_text(nowPlayingPauseButton_, buttonLabel);
        gtk_accessible_update_property(
            GTK_ACCESSIBLE(nowPlayingPauseButton_),
            GTK_ACCESSIBLE_PROPERTY_LABEL, buttonLabel,
            -1);
    }
    if (nowPlayingCategoryLabel_) {
        const std::string categoryText = playing.size() == 1 && clip
            ? categoryName(clip->categoryId)
            : "Multiple sounds";
        gtk_label_set_text(GTK_LABEL(nowPlayingCategoryLabel_), categoryText.c_str());
    }
    const auto progress = audio_.playbackProgress(playing.back());
    const double duration = progress && progress->durationSeconds > 0.0
        ? progress->durationSeconds
        : (clip ? clip->durationSeconds : 0.0);
    const double fraction = progress && duration > 0.0
        ? std::clamp(progress->positionSeconds / duration, 0.0, 1.0)
        : 0.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(nowPlayingProgress_), fraction);

    if (progressTickId_ == 0) {
        progressTickId_ = g_timeout_add(250, +[](gpointer userData) -> gboolean {
            auto* self = static_cast<CueletWindow*>(userData);
            if (self->audio_.playingPaths().empty()) {
                self->progressTickId_ = 0;
                self->refreshNowPlaying();
                return G_SOURCE_REMOVE;
            }
            self->refreshNowPlaying();
            return G_SOURCE_CONTINUE;
        }, this);
    }
}

void CueletWindow::refreshSelectionVisuals()
{
    for (GtkWidget* child = gtk_widget_get_first_child(flowBox_);
         child;
         child = gtk_widget_get_next_sibling(child)) {
        if (!GTK_IS_FLOW_BOX_CHILD(child)) {
            continue;
        }
        GtkWidget* card = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child));
        if (!card) {
            continue;
        }
        const bool selected = selectedPaths_.count(
            objectString(G_OBJECT(card), "relative-path")) > 0;
        if (selected) {
            gtk_widget_add_css_class(card, "selected");
        } else {
            gtk_widget_remove_css_class(card, "selected");
        }
        gtk_accessible_update_state(
            GTK_ACCESSIBLE(child),
            GTK_ACCESSIBLE_STATE_SELECTED, selected,
            -1);
    }

    for (GtkWidget* row = gtk_widget_get_first_child(listBox_);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        const bool selected = selectedPaths_.count(
            objectString(G_OBJECT(row), "relative-path")) > 0;
        if (selected) {
            gtk_widget_add_css_class(row, "selected");
        } else {
            gtk_widget_remove_css_class(row, "selected");
        }
        gtk_accessible_update_state(
            GTK_ACCESSIBLE(row),
            GTK_ACCESSIBLE_STATE_SELECTED, selected,
            -1);
    }
}

void CueletWindow::selectSound(const std::string& relativePath, bool extendSelection)
{
    if (!clipByPath(relativePath)) {
        return;
    }
    if (!extendSelection) {
        selectedPaths_.clear();
    }
    if (extendSelection && selectedPaths_.erase(relativePath) > 0) {
        refreshSelectionVisuals();
        return;
    }
    selectedPaths_.insert(relativePath);
    refreshSelectionVisuals();
}

void CueletWindow::selectAllVisible()
{
    selectedPaths_.clear();
    for (const auto& clip : visibleClips()) {
        selectedPaths_.insert(clip.relativePath);
    }
    refreshSelectionVisuals();
}

void CueletWindow::clearSelection()
{
    if (selectedPaths_.empty()) {
        return;
    }
    selectedPaths_.clear();
    refreshSelectionVisuals();
}

std::string CueletWindow::focusedSoundPath() const
{
    for (GtkWidget* widget = gtk_window_get_focus(GTK_WINDOW(window_));
         widget && widget != GTK_WIDGET(window_);
         widget = gtk_widget_get_parent(widget)) {
        const std::string path = objectString(G_OBJECT(widget), "relative-path");
        if (!path.empty()) {
            return path;
        }
    }
    return {};
}

bool CueletWindow::presentSelectedSoundMenu()
{
    std::string path = focusedSoundPath();
    if (path.empty()) {
        for (const auto& clip : visibleClips()) {
            if (selectedPaths_.count(clip.relativePath) > 0) {
                path = clip.relativePath;
                break;
            }
        }
    }
    if (path.empty()) {
        return false;
    }

    GtkWidget* source = nullptr;
    if (settings_.viewMode == "list") {
        for (GtkWidget* row = gtk_widget_get_first_child(listBox_);
             row;
             row = gtk_widget_get_next_sibling(row)) {
            if (objectString(G_OBJECT(row), "relative-path") == path) {
                source = row;
                break;
            }
        }
    } else {
        for (GtkWidget* child = gtk_widget_get_first_child(flowBox_);
             child;
             child = gtk_widget_get_next_sibling(child)) {
            if (!GTK_IS_FLOW_BOX_CHILD(child)) {
                continue;
            }
            GtkWidget* card = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child));
            if (card && objectString(G_OBJECT(card), "relative-path") == path) {
                source = card;
                break;
            }
        }
    }
    if (!source) {
        return false;
    }

    selectSound(path, false);
    GtkWidget* popover = makeSoundPopover(path);
    presentPopover(
        popover,
        source,
        gtk_widget_get_width(source) / 2.0,
        gtk_widget_get_height(source) / 2.0);
    return true;
}

bool CueletWindow::handleEscape()
{
    const char* text = gtk_editable_get_text(GTK_EDITABLE(searchEntry_));
    if (text && *text) {
        gtk_editable_set_text(GTK_EDITABLE(searchEntry_), "");
        return true;
    }
    if (!selectedPaths_.empty()) {
        clearSelection();
        return true;
    }
    if (splitView_
        && adw_navigation_split_view_get_collapsed(splitView_)
        && !adw_navigation_split_view_get_show_content(splitView_)) {
        adw_navigation_split_view_set_show_content(splitView_, TRUE);
        return true;
    }
    if (!audio_.playingPaths().empty()) {
        stopAll();
        return true;
    }
    return false;
}

bool CueletWindow::saveSettings()
{
    if (settingsStore_.save(settings_)) {
        lastSettingsSaveError_.clear();
        return true;
    }
    const std::string error = settingsStore_.lastError().empty()
        ? "Cuelet settings could not be saved."
        : settingsStore_.lastError();
    if (cuelet_linux::shouldReportPersistenceError(
            lastSettingsSaveError_, error)) {
        lastSettingsSaveError_ = error;
        showError(error);
    }
    return false;
}

void CueletWindow::saveMetadata()
{
    syncGlobalShortcuts();
    if (libraryPath_.empty()) {
        return;
    }
    cuelet::MetadataStore store(cuelet::MetadataStore::metadataPathForLibrary(libraryPath_));
    if (!store.save(cuelet::MetadataStore::metadataFromClips(clips_, categories_))) {
        showError(store.lastError());
    }
}

void CueletWindow::showToast(const std::string& message)
{
    AdwToast* toast = adw_toast_new(message.c_str());
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toastOverlay_), toast);
}

void CueletWindow::showError(const std::string& message)
{
    AdwDialog* dialog = adw_alert_dialog_new("Cuelet", message.c_str());
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "OK");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
    adw_dialog_present(dialog, GTK_WIDGET(window_));
}

void CueletWindow::notifyPlaybackStarted()
{
    if (gtk_widget_get_visible(GTK_WIDGET(window_))) {
        return;
    }

    GNotification* notification = g_notification_new("Cuelet is playing");
    g_notification_set_body(
        notification,
        "Playback started while Cuelet is hidden.");
    g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
    g_application_send_notification(
        G_APPLICATION(application_),
        "cuelet-playback",
        notification);
    g_object_unref(notification);
}

void CueletWindow::withdrawPlaybackNotification()
{
    g_application_withdraw_notification(
        G_APPLICATION(application_),
        "cuelet-playback");
}

std::vector<cuelet::SoundClip> CueletWindow::visibleClips() const
{
    return cuelet::filterAndSortSounds(clips_, categories_, filterOptions());
}

cuelet::FilterOptions CueletWindow::filterOptions() const
{
    cuelet::FilterOptions options;
    options.sort = settings_.sortOption;
    options.searchText = gtk_editable_get_text(GTK_EDITABLE(searchEntry_));
    switch (selection_.kind) {
    case SidebarKind::Library:
        options.scope = cuelet::LibraryScope::All;
        break;
    case SidebarKind::Favorites:
        options.scope = cuelet::LibraryScope::Favorites;
        break;
    case SidebarKind::Recent:
        options.scope = cuelet::LibraryScope::Recent;
        break;
    case SidebarKind::AllCategories:
        options.scope = cuelet::LibraryScope::AllCategories;
        break;
    case SidebarKind::Category:
        options.scope = cuelet::LibraryScope::Category;
        options.categoryId = selection_.categoryId;
        break;
    }
    return options;
}

cuelet::SoundClip* CueletWindow::clipByPath(const std::string& relativePath)
{
    const auto found = std::find_if(clips_.begin(), clips_.end(), [&](const cuelet::SoundClip& clip) {
        return clip.relativePath == relativePath;
    });
    return found == clips_.end() ? nullptr : &*found;
}

const cuelet::SoundClip* CueletWindow::clipByPath(const std::string& relativePath) const
{
    const auto found = std::find_if(clips_.begin(), clips_.end(), [&](const cuelet::SoundClip& clip) {
        return clip.relativePath == relativePath;
    });
    return found == clips_.end() ? nullptr : &*found;
}

cuelet::Category* CueletWindow::categoryById(const std::string& categoryId)
{
    const auto found = std::find_if(categories_.begin(), categories_.end(), [&](const cuelet::Category& category) {
        return category.id == categoryId;
    });
    return found == categories_.end() ? nullptr : &*found;
}

const cuelet::Category* CueletWindow::categoryById(const std::string& categoryId) const
{
    const auto found = std::find_if(categories_.begin(), categories_.end(), [&](const cuelet::Category& category) {
        return category.id == categoryId;
    });
    return found == categories_.end() ? nullptr : &*found;
}

std::string CueletWindow::categoryName(const std::string& categoryId) const
{
    const auto* category = categoryById(categoryId);
    return category ? category->name : "Uncategorized";
}

std::string CueletWindow::categoryColor(const std::string& categoryId) const
{
    const auto* category = categoryById(categoryId);
    return category ? category->colorHex : "#8E8E93";
}

bool CueletWindow::handleLocalShortcut(guint keyval, GdkModifierType state)
{
    const auto modifiers = shortcutModifierMask(state);
    const guint lowerKey = gdk_keyval_to_lower(keyval);
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window_));

    if (modifiers == GDK_CONTROL_MASK && lowerKey == GDK_KEY_f) {
        gtk_widget_grab_focus(searchEntry_);
        gtk_editable_select_region(GTK_EDITABLE(searchEntry_), 0, -1);
        return true;
    }
    if (modifiers == GDK_CONTROL_MASK && lowerKey == GDK_KEY_a) {
        if (GTK_IS_EDITABLE(focus)) {
            return false;
        }
        selectAllVisible();
        return true;
    }
    if (modifiers == 0 && keyval == GDK_KEY_Escape) {
        return handleEscape();
    }
    if ((modifiers == 0 && keyval == GDK_KEY_Menu)
        || (modifiers == GDK_SHIFT_MASK && keyval == GDK_KEY_F10)) {
        if (GTK_IS_EDITABLE(focus)) {
            return false;
        }
        return presentSelectedSoundMenu();
    }

    if (GTK_IS_EDITABLE(focus)) {
        return false;
    }
    if (modifiers == 0
        && isSoundActivationKey(keyval)
        && !GTK_IS_BUTTON(focus)
        && !GTK_IS_MENU_BUTTON(focus)
        && !focusedSoundPath().empty()) {
        playSelectionOrTopSearchResult();
        return true;
    }

    const auto shortcutModifiers = static_cast<unsigned int>(modifiers);
    for (const auto& clip : clips_) {
        if (clip.shortcut
            && gdk_keyval_to_lower(clip.shortcut->keyval) == lowerKey
            && clip.shortcut->modifiers == shortcutModifiers
            && cuelet_linux::shouldHandleShortcutLocally(
                *clip.shortcut,
                globalShortcuts_
                    ? globalShortcuts_->statusForSound(clip.id)
                    : std::nullopt)) {
            playSound(clip.relativePath);
            return true;
        }
    }
    return false;
}

bool CueletWindow::handleSearchKey(guint keyval)
{
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        playSelectionOrTopSearchResult();
        return true;
    }
    if (keyval == GDK_KEY_Escape) {
        return handleEscape();
    }
    return false;
}

void CueletWindow::playSelectionOrTopSearchResult()
{
    std::string path = focusedSoundPath();
    if (path.empty()) {
        for (const auto& clip : visibleClips()) {
            if (selectedPaths_.count(clip.relativePath) > 0) {
                path = clip.relativePath;
                break;
            }
        }
    }
    if (!path.empty()) {
        playSound(path);
        return;
    }
    playTopSearchResult();
}

void CueletWindow::playTopSearchResult()
{
    const auto clips = visibleClips();
    if (!clips.empty()) {
        playSound(clips.front().relativePath);
    }
}
