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

GtkListItemFactory* makeCategoryColorFactory()
{
    auto* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_list_item_set_child(item, content);
        gtk_box_append(GTK_BOX(content), gtk_label_new(nullptr));
        gtk_box_append(GTK_BOX(content), gtk_label_new(nullptr));
    }), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        const guint position = gtk_list_item_get_position(item);
        if (position >= colorPalette().size()) {
            return;
        }
        GtkWidget* content = gtk_list_item_get_child(item);
        GtkWidget* preview = gtk_widget_get_first_child(content);
        GtkWidget* label = gtk_widget_get_next_sibling(preview);
        const auto& colorChoice = colorPalette()[position];
        const std::string markup = "<span foreground=\"" + colorChoice.second + "\">●</span>";
        gtk_label_set_markup(GTK_LABEL(preview), markup.c_str());
        gtk_label_set_text(GTK_LABEL(label), colorChoice.first.c_str());
    }), nullptr);
    return GTK_LIST_ITEM_FACTORY(factory);
}

GtkListItemFactory* makeCategoryIconFactory()
{
    auto* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_list_item_set_child(item, content);
        gtk_box_append(GTK_BOX(content), gtk_image_new());
        gtk_box_append(GTK_BOX(content), gtk_label_new(nullptr));
    }), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        const guint position = gtk_list_item_get_position(item);
        if (position >= iconChoices().size()) {
            return;
        }
        GtkWidget* content = gtk_list_item_get_child(item);
        GtkWidget* preview = gtk_widget_get_first_child(content);
        GtkWidget* label = gtk_widget_get_next_sibling(preview);
        const auto& icon = iconChoices()[position];
        gtk_image_set_from_icon_name(GTK_IMAGE(preview), icon.linuxIconName.c_str());
        gtk_label_set_text(GTK_LABEL(label), icon.label.c_str());
    }), nullptr);
    return GTK_LIST_ITEM_FACTORY(factory);
}

GtkWidget* makeCategoryDropDown(bool icons)
{
    GtkStringList* choices = gtk_string_list_new(nullptr);
    if (icons) {
        for (const auto& icon : iconChoices()) {
            gtk_string_list_append(choices, icon.label.c_str());
        }
    } else {
        for (const auto& [name, color] : colorPalette()) {
            (void)color;
            gtk_string_list_append(choices, name.c_str());
        }
    }

    GtkWidget* dropDown = gtk_drop_down_new(G_LIST_MODEL(choices), nullptr);
    g_object_unref(choices);
    gtk_drop_down_set_enable_search(GTK_DROP_DOWN(dropDown), icons);

    GtkListItemFactory* selectedFactory = icons ? makeCategoryIconFactory() : makeCategoryColorFactory();
    GtkListItemFactory* listFactory = icons ? makeCategoryIconFactory() : makeCategoryColorFactory();
    gtk_drop_down_set_factory(GTK_DROP_DOWN(dropDown), selectedFactory);
    gtk_drop_down_set_list_factory(GTK_DROP_DOWN(dropDown), listFactory);
    g_object_unref(selectedFactory);
    g_object_unref(listFactory);
    return dropDown;
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
        gtk_flow_box_append(GTK_FLOW_BOX(flowBox_), makeSoundCard(clip));
        gtk_list_box_append(GTK_LIST_BOX(listBox_), makeSoundRow(clip));
    }

    if (clips.empty()) {
        if (libraryPath_.empty() && !settings_.showsDemoLibrary) {
            adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sound Library");
            adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), "Choose a folder of audio files to start building your soundboard.");
        } else {
            adw_status_page_set_title(ADW_STATUS_PAGE(emptyPage_), "No Sounds Found");
            adw_status_page_set_description(ADW_STATUS_PAGE(emptyPage_), "Try a different search, category, or import audio files.");
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
    const std::string subtitle = settings_.showsDemoLibrary
        ? "Demo Library"
        : (libraryPath_.empty() ? "No library selected" : libraryPath_.string());
    gtk_label_set_text(GTK_LABEL(subtitleLabel_), subtitle.c_str());
    const auto count = visibleClips().size();
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
        if (progressTickId_ != 0) {
            g_source_remove(progressTickId_);
            progressTickId_ = 0;
        }
        return;
    }

    const cuelet::SoundClip* clip = clipByPath(playing.back());
    const std::string label = playing.size() == 1 && clip
        ? "Playing " + clip->searchableName()
        : "Playing " + std::to_string(playing.size()) + " sounds";
    gtk_label_set_text(GTK_LABEL(nowPlayingLabel_), label.c_str());
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

void CueletWindow::saveSettings()
{
    settingsStore_.save(settings_);
}

void CueletWindow::saveMetadata()
{
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

void CueletWindow::playSound(const std::string& relativePath)
{
    auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    if (audio_.play(*clip)) {
        clip->lastPlayedAt = std::time(nullptr);
        saveMetadata();
        refreshContent();
        refreshNowPlaying();
    }
}

void CueletWindow::togglePlayback(const std::string& relativePath)
{
    if (audio_.isPlaying(relativePath)) {
        stopSound(relativePath);
    } else {
        playSound(relativePath);
    }
}

void CueletWindow::stopSound(const std::string& relativePath)
{
    audio_.stop(relativePath);
    refreshContent();
    refreshNowPlaying();
}

void CueletWindow::stopAll()
{
    audio_.stopAll();
    refreshContent();
    refreshNowPlaying();
}

void CueletWindow::toggleFavorite(const std::string& relativePath)
{
    auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    clip->favorite = !clip->favorite;
    saveMetadata();
    refreshContent();
    refreshSidebar();
}

void CueletWindow::assignCategory(const std::string& relativePath, const std::string& categoryId)
{
    auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    clip->categoryId = categoryId.empty() ? "uncategorized" : categoryId;
    saveMetadata();
    refreshAll();
}

void CueletWindow::promptNewCategory(const std::string& assignRelativePath)
{
    AdwDialog* dialog = adw_alert_dialog_new("New Category", "Enter a name for the new category.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "create", "Create", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "create", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "create");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Category name");
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);

    auto* data = new TextDialogData{this, entry, "new-category", assignRelativePath};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<TextDialogData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "create") == 0) {
            const std::string name = cuelet::trim(gtk_editable_get_text(GTK_EDITABLE(data->entry)));
            if (!name.empty()) {
                const auto id = cuelet::stableCategoryIdForName(name);
                const bool exists = std::any_of(
                    data->self->categories_.begin(),
                    data->self->categories_.end(),
                    [&](const cuelet::Category& category) { return category.id == id; });
                if (exists) {
                    data->self->showError("A category with that name already exists.");
                    delete data;
                    return;
                }
                auto updatedCategories = data->self->categories_;
                updatedCategories.push_back(cuelet::Category{id, name, "#009688", "tag", true});
                data->self->categories_ = std::move(updatedCategories);
                if (!data->assignPath.empty()) {
                    data->self->assignCategory(data->assignPath, id);
                }
                data->self->saveMetadata();
                data->self->refreshAll();
            }
        }
        delete data;
    }, data);
}

void CueletWindow::promptRenameCategory(const std::string& categoryId)
{
    const auto* category = categoryById(categoryId);
    if (!category || !category->editable) {
        return;
    }

    AdwDialog* dialog = adw_alert_dialog_new("Edit Category", "Update the category name, color, and icon.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "save", "Save", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

    GtkWidget* form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(form, 360, -1);

    GtkWidget* nameLabel = gtk_label_new("Name");
    gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0f);
    gtk_widget_add_css_class(nameLabel, "heading");
    GtkWidget* nameEntry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(nameEntry), category->name.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(nameEntry), TRUE);
    gtk_box_append(GTK_BOX(form), nameLabel);
    gtk_box_append(GTK_BOX(form), nameEntry);

    GtkWidget* colorLabel = gtk_label_new("Color");
    gtk_label_set_xalign(GTK_LABEL(colorLabel), 0.0f);
    gtk_widget_add_css_class(colorLabel, "heading");
    GtkWidget* colorDropDown = makeCategoryDropDown(false);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(colorDropDown), categoryColorIndex(category->colorHex));
    gtk_box_append(GTK_BOX(form), colorLabel);
    gtk_box_append(GTK_BOX(form), colorDropDown);

    GtkWidget* iconLabel = gtk_label_new("Icon");
    gtk_label_set_xalign(GTK_LABEL(iconLabel), 0.0f);
    gtk_widget_add_css_class(iconLabel, "heading");
    GtkWidget* iconDropDown = makeCategoryDropDown(true);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(iconDropDown), categoryIconIndex(category->iconName));
    gtk_box_append(GTK_BOX(form), iconLabel);
    gtk_box_append(GTK_BOX(form), iconDropDown);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), form);

    auto* data = new CategoryEditDialogData{this, nameEntry, colorDropDown, iconDropDown, categoryId};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<CategoryEditDialogData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "save") == 0) {
            const std::string name = cuelet::trim(gtk_editable_get_text(GTK_EDITABLE(data->nameEntry)));
            const guint colorIndex = gtk_drop_down_get_selected(GTK_DROP_DOWN(data->colorDropDown));
            const guint iconIndex = gtk_drop_down_get_selected(GTK_DROP_DOWN(data->iconDropDown));
            if (!name.empty() && colorIndex < colorPalette().size() && iconIndex < iconChoices().size()) {
                const auto found = std::find_if(data->self->categories_.begin(), data->self->categories_.end(), [&](const auto& item) {
                    return item.id == data->categoryId;
                });
                if (found != data->self->categories_.end() && found->editable) {
                    const auto& color = colorPalette()[colorIndex].second;
                    const auto& icon = iconChoices()[iconIndex].id;
                    const bool changed = found->name != name || found->colorHex != color || found->iconName != icon;
                    if (changed) {
                        std::vector<cuelet::Category> updatedCategories;
                        updatedCategories.reserve(data->self->categories_.size());
                        for (const auto& existing : data->self->categories_) {
                            cuelet::Category updated = existing;
                            if (updated.id == data->categoryId) {
                                updated.name = name;
                                updated.colorHex = color;
                                updated.iconName = icon;
                            }
                            updatedCategories.push_back(std::move(updated));
                        }
                        data->self->categories_ = std::move(updatedCategories);
                        data->self->saveMetadata();
                        data->self->refreshAll();
                    }
                }
            }
        }
        delete data;
    }, data);
}

void CueletWindow::setCategoryColor(const std::string& categoryId, const std::string& colorHex)
{
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const cuelet::Category& item) {
        return item.id == categoryId;
    });
    const bool validColor = std::any_of(colorPalette().begin(), colorPalette().end(), [&](const auto& item) {
        return item.second == colorHex;
    });
    if (category == categories_.end() || !category->editable || !validColor || category->colorHex == colorHex) {
        return;
    }

    std::vector<cuelet::Category> updatedCategories;
    updatedCategories.reserve(categories_.size());
    for (const auto& existing : categories_) {
        cuelet::Category updated = existing;
        if (updated.id == categoryId) {
            updated.colorHex = colorHex;
        }
        updatedCategories.push_back(std::move(updated));
    }
    categories_ = std::move(updatedCategories);
    saveMetadata();
    refreshAll();
}

void CueletWindow::setCategoryIcon(const std::string& categoryId, const std::string& iconId)
{
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const cuelet::Category& item) {
        return item.id == categoryId;
    });
    const std::string canonicalIconId = canonicalCategoryIconId(iconId);
    const bool validIcon = std::any_of(iconChoices().begin(), iconChoices().end(), [&](const auto& item) {
        return item.id == canonicalIconId;
    });
    if (category == categories_.end() || !category->editable || !validIcon || category->iconName == canonicalIconId) {
        return;
    }

    std::vector<cuelet::Category> updatedCategories;
    updatedCategories.reserve(categories_.size());
    for (const auto& existing : categories_) {
        cuelet::Category updated = existing;
        if (updated.id == categoryId) {
            updated.iconName = canonicalIconId;
        }
        updatedCategories.push_back(std::move(updated));
    }
    categories_ = std::move(updatedCategories);
    saveMetadata();
    refreshAll();
}

void CueletWindow::confirmDeleteCategory(const std::string& categoryId)
{
    const auto* category = categoryById(categoryId);
    if (!category || !category->editable) {
        return;
    }
    AdwDialog* dialog = adw_alert_dialog_new("Delete Category", "Sounds in this category will move to Uncategorized.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "delete", "Delete", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "delete", ADW_RESPONSE_DESTRUCTIVE);
    auto* data = new CategoryActionData{this, categoryId};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<CategoryActionData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "delete") == 0) {
            data->self->categories_.erase(
                std::remove_if(data->self->categories_.begin(), data->self->categories_.end(), [&](const cuelet::Category& category) {
                    return category.id == data->categoryId;
                }),
                data->self->categories_.end());
            for (auto& clip : data->self->clips_) {
                if (clip.categoryId == data->categoryId) {
                    clip.categoryId = "uncategorized";
                }
            }
            if (data->self->selection_.categoryId == data->categoryId) {
                data->self->selection_ = SidebarSelection{SidebarKind::AllCategories, {}};
            }
            data->self->saveMetadata();
            data->self->refreshAll();
        }
        delete data;
    }, data);
}

void CueletWindow::promptRenameSound(const std::string& relativePath)
{
    auto* clip = clipByPath(relativePath);
    if (!clip || clip->missing || libraryPath_.empty()) {
        return;
    }

    AdwDialog* dialog = adw_alert_dialog_new("Rename Sound", "Enter a new file name without the extension.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "rename", "Rename", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "rename", ADW_RESPONSE_SUGGESTED);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), cuelet::displayNameFromFilename(clip->filename).c_str());
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);

    auto* data = new TextDialogData{this, entry, relativePath, {}};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<TextDialogData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "rename") == 0) {
            auto* clip = data->self->clipByPath(data->context);
            if (clip) {
                const std::string base = cuelet::trim(gtk_editable_get_text(GTK_EDITABLE(data->entry)));
                if (base.empty() || base.find('/') != std::string::npos || base.find('\\') != std::string::npos) {
                    data->self->showError("Enter a file name without folders or path separators.");
                } else {
                    const std::string oldRelativePath = clip->relativePath;
                    const std::filesystem::path oldPath = clip->absolutePath;
                    const auto extension = oldPath.extension().string();
                    const auto newPath = oldPath.parent_path() / (base + extension);
                    if (newPath == oldPath) {
                        delete data;
                        return;
                    }
                    if (std::filesystem::exists(newPath)) {
                        data->self->showError("A file with that name already exists.");
                        delete data;
                        return;
                    }
                    std::error_code error;
                    std::filesystem::rename(oldPath, newPath, error);
                    if (error) {
                        data->self->showError("Could not rename the file: " + error.message());
                    } else {
                        data->self->audio_.stop(oldRelativePath);
                        clip->absolutePath = newPath.string();
                        clip->relativePath = cuelet::LibraryScanner::normalizeRelativePath(data->self->libraryPath_, newPath);
                        clip->filename = cuelet::filenameFromPath(clip->relativePath);
                        clip->displayName = base;
                        clip->id = cuelet::stableIdForPath(clip->relativePath);
                        if (data->self->selectedPaths_.erase(oldRelativePath) > 0) {
                            data->self->selectedPaths_.insert(clip->relativePath);
                        }
                        data->self->saveMetadata();
                        data->self->refreshAll();
                    }
                }
            }
        }
        delete data;
    }, data);
}

void CueletWindow::revealSound(const std::string& relativePath)
{
    const auto* clip = clipByPath(relativePath);
    if (!clip || clip->absolutePath.empty()) {
        return;
    }
    GError* error = nullptr;
    char* itemUri = g_filename_to_uri(clip->absolutePath.c_str(), nullptr, &error);
    g_clear_error(&error);
    const auto folder = std::filesystem::path(clip->absolutePath).parent_path();
    char* folderUri = g_filename_to_uri(folder.c_str(), nullptr, &error);
    if (!folderUri) {
        showError("Could not reveal the sound: " + std::string(error ? error->message : "invalid file path"));
        g_clear_error(&error);
        g_free(itemUri);
        return;
    }

    struct RevealData {
        CueletWindow* self;
        GObject* lifetime;
        GWeakRef window;
        std::string itemUri;
        std::string folderUri;
        void (*finish)(RevealData*);
        void (*launchFolder)(RevealData*);
    };

    auto* data = new RevealData{
        this,
        G_OBJECT(g_object_ref(application_)),
        {},
        itemUri ? itemUri : "",
        folderUri,
        nullptr,
        nullptr,
    };
    g_weak_ref_init(&data->window, G_OBJECT(window_));
    g_free(itemUri);
    g_free(folderUri);
    data->finish = +[](RevealData* data) {
        g_weak_ref_clear(&data->window);
        g_object_unref(data->lifetime);
        delete data;
    };
    data->launchFolder = +[](RevealData* data) {
        auto* parent = static_cast<GObject*>(g_weak_ref_get(&data->window));
        if (!parent || !gtk_widget_get_root(GTK_WIDGET(parent))) {
            if (parent) {
                g_object_unref(parent);
            }
            data->finish(data);
            return;
        }
        GtkUriLauncher* launcher = gtk_uri_launcher_new(data->folderUri.c_str());
        gtk_uri_launcher_launch(launcher, GTK_WINDOW(parent), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* data = static_cast<RevealData*>(userData);
            GError* error = nullptr;
            gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source), result, &error);
            if (error) {
                auto* window = static_cast<GObject*>(g_weak_ref_get(&data->window));
                if (window && gtk_widget_get_root(GTK_WIDGET(window))) {
                    data->self->showError("Could not reveal the sound: " + std::string(error->message));
                }
                if (window) {
                    g_object_unref(window);
                }
                g_clear_error(&error);
            }
            data->finish(data);
        }, data);
        g_object_unref(launcher);
        g_object_unref(parent);
    };

    if (data->itemUri.empty()) {
        data->launchFolder(data);
        return;
    }

    g_bus_get(G_BUS_TYPE_SESSION, nullptr, +[](GObject*, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<RevealData*>(userData);
        GError* error = nullptr;
        GDBusConnection* connection = g_bus_get_finish(result, &error);
        g_clear_error(&error);
        if (!connection) {
            data->launchFolder(data);
            return;
        }

        const char* uris[] = {data->itemUri.c_str(), nullptr};
        g_dbus_connection_call(
            connection,
            "org.freedesktop.FileManager1",
            "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1",
            "ShowItems",
            g_variant_new("(^ass)", uris, ""),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            2000,
            nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* data = static_cast<RevealData*>(userData);
                GError* error = nullptr;
                GVariant* response = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
                g_clear_error(&error);
                if (response) {
                    g_variant_unref(response);
                    data->finish(data);
                    return;
                }
                data->launchFolder(data);
            },
            data);
        g_object_unref(connection);
    }, data);
}

void CueletWindow::confirmRemoveSound(const std::string& relativePath)
{
    AdwDialog* dialog = adw_alert_dialog_new("Remove Sound", "The audio file will stay on disk. This removes it from the current Cuelet view.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "remove", "Remove", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "remove", ADW_RESPONSE_DESTRUCTIVE);
    auto* data = new WindowStringData{this, relativePath};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "remove") == 0) {
            data->self->stopSound(data->value);
            data->self->clips_.erase(
                std::remove_if(data->self->clips_.begin(), data->self->clips_.end(), [&](const cuelet::SoundClip& clip) {
                    return clip.relativePath == data->value;
                }),
                data->self->clips_.end());
            data->self->saveMetadata();
            data->self->refreshAll();
        }
        delete data;
    }, data);
}

void CueletWindow::recordShortcut(const std::string& relativePath)
{
    AdwDialog* dialog = adw_alert_dialog_new("Record Shortcut", "Press the key combination to assign to this sound.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "assign", "Assign", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "assign", ADW_RESPONSE_SUGGESTED);
    GtkWidget* label = gtk_label_new("Waiting for shortcut…");
    gtk_widget_add_css_class(label, "shortcut-recorder");
    gtk_widget_set_focusable(label, TRUE);
    gtk_widget_set_size_request(label, 260, 72);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), label);

    auto* data = new ShortcutDialogData{this, label, relativePath, {}};
    GtkEventController* controller = gtk_event_controller_key_new();
    g_signal_connect(controller, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer userData) {
        auto* data = static_cast<ShortcutDialogData*>(userData);
        if (isModifierOnly(keyval)) {
            return GDK_EVENT_STOP;
        }
        const auto modifiers = shortcutModifierMask(state);
        data->shortcut.keyval = keyval;
        data->shortcut.modifiers = static_cast<unsigned int>(modifiers);
        data->shortcut.label = shortcutLabel(keyval, modifiers);
        gtk_label_set_text(GTK_LABEL(data->label), data->shortcut.label.c_str());
        return GDK_EVENT_STOP;
    }), data);
    gtk_widget_add_controller(GTK_WIDGET(dialog), controller);

    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<ShortcutDialogData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "assign") == 0 && !data->shortcut.empty()) {
            for (const auto& clip : data->self->clips_) {
                if (clip.relativePath != data->relativePath
                    && clip.shortcut
                    && clip.shortcut->sameCombination(data->shortcut)) {
                    data->self->showError("That shortcut is already assigned to " + clip.searchableName() + ".");
                    delete data;
                    return;
                }
            }
            if (auto* clip = data->self->clipByPath(data->relativePath)) {
                clip->shortcut = data->shortcut;
                data->self->saveMetadata();
                data->self->refreshContent();
            }
        }
        delete data;
    }, data);
}

void CueletWindow::clearShortcut(const std::string& relativePath)
{
    if (auto* clip = clipByPath(relativePath)) {
        clip->shortcut.reset();
        saveMetadata();
        refreshContent();
    }
}

void CueletWindow::copyGnomeShortcutCommand(const std::string& relativePath)
{
    const auto* clip = clipByPath(relativePath);
    if (!clip) {
        showError("Could not create a shortcut command for this sound.");
        return;
    }

    const std::string command = cuelet_linux::shortcutCommandForSound(
        *clip,
        cuelet_linux::cueletExecutablePath());
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(window_)), command.c_str());
    showToast("Copied GNOME shortcut command.");
}

bool CueletWindow::handleLocalShortcut(guint keyval, GdkModifierType state)
{
    if (GTK_IS_EDITABLE(gtk_window_get_focus(GTK_WINDOW(window_)))) {
        return false;
    }
    const auto modifiers = static_cast<unsigned int>(shortcutModifierMask(state));
    for (const auto& clip : clips_) {
        if (clip.shortcut && clip.shortcut->keyval == keyval && clip.shortcut->modifiers == modifiers) {
            playSound(clip.relativePath);
            return true;
        }
    }
    return false;
}

bool CueletWindow::handleSearchKey(guint keyval)
{
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        playTopSearchResult();
        return true;
    }
    if (keyval == GDK_KEY_Escape) {
        const char* text = gtk_editable_get_text(GTK_EDITABLE(searchEntry_));
        if (text && *text) {
            gtk_editable_set_text(GTK_EDITABLE(searchEntry_), "");
        } else {
            stopAll();
        }
        return true;
    }
    return false;
}

void CueletWindow::playTopSearchResult()
{
    const auto clips = visibleClips();
    if (!clips.empty()) {
        playSound(clips.front().relativePath);
    }
}
