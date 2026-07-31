#include "CueletWindow.h"
#include "CueletWindowHelpers.h"

#include <gio/gio.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <utility>

using namespace cuelet_linux;

namespace {

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

void CueletWindow::playSound(const std::string& relativePath)
{
    auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    if (audio_.play(*clip)) {
        clip->lastPlayedAt = std::time(nullptr);
        notifyPlaybackStarted();
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
    if (audio_.playingPaths().empty()) {
        withdrawPlaybackNotification();
    }
    refreshContent();
    refreshNowPlaying();
}

void CueletWindow::stopAll()
{
    audio_.stopAll();
    withdrawPlaybackNotification();
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
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(entry),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Category Name",
        -1);
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
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(nameEntry),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Category Name",
        -1);
    gtk_editable_set_text(GTK_EDITABLE(nameEntry), category->name.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(nameEntry), TRUE);
    gtk_box_append(GTK_BOX(form), nameLabel);
    gtk_box_append(GTK_BOX(form), nameEntry);

    GtkWidget* colorLabel = gtk_label_new("Color");
    gtk_label_set_xalign(GTK_LABEL(colorLabel), 0.0f);
    gtk_widget_add_css_class(colorLabel, "heading");
    GtkWidget* colorDropDown = makeCategoryDropDown(false);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(colorDropDown),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Category Color",
        -1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(colorDropDown), categoryColorIndex(category->colorHex));
    gtk_box_append(GTK_BOX(form), colorLabel);
    gtk_box_append(GTK_BOX(form), colorDropDown);

    GtkWidget* iconLabel = gtk_label_new("Icon");
    gtk_label_set_xalign(GTK_LABEL(iconLabel), 0.0f);
    gtk_widget_add_css_class(iconLabel, "heading");
    GtkWidget* iconDropDown = makeCategoryDropDown(true);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(iconDropDown),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Category Icon",
        -1);
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
    if (!clip || libraryPath_.empty()
        || (clip->missing && clip->storageMode == cuelet::SoundStorageMode::Managed)) {
        return;
    }

    const bool linked = clip->storageMode == cuelet::SoundStorageMode::Linked;
    AdwDialog* dialog = adw_alert_dialog_new(
        linked ? "Edit Sound Name" : "Rename Sound",
        linked
            ? "Change the name shown in Cuelet. The external source file will not be renamed."
            : "Enter a new file name without the extension.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "rename", "Rename", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "rename", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "rename");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    GtkWidget* entry = gtk_entry_new();
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(entry),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Sound Name",
        -1);
    gtk_editable_set_text(
        GTK_EDITABLE(entry),
        linked
            ? clip->searchableName().c_str()
            : cuelet::displayNameFromFilename(clip->filename).c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);

    auto* data = new TextDialogData{this, entry, relativePath, {}};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<TextDialogData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "rename") == 0) {
            auto* clip = data->self->clipByPath(data->context);
            if (clip) {
                const std::string name = cuelet::trim(
                    gtk_editable_get_text(GTK_EDITABLE(data->entry)));
                const auto mode = clip->storageMode == cuelet::SoundStorageMode::Linked
                    ? LinuxLibraryImportService::RenameMode::DisplayNameOnly
                    : LinuxLibraryImportService::RenameMode::RenameFile;
                const auto plan = LinuxLibraryImportService::planRename(
                    *clip, name, data->self->libraryPath_, mode);
                if (!plan.valid || !plan.updatedClip) {
                    data->self->showError(
                        plan.message.empty() ? "Could not rename the sound." : plan.message);
                } else {
                    std::string renameError;
                    if (plan.requiresFileRename) {
                        GFile* oldFile = g_file_new_for_path(plan.oldPath.c_str());
                        GFile* newFile = g_file_new_for_path(plan.newPath.c_str());
                        GError* error = nullptr;
                        if (!g_file_move(
                                oldFile,
                                newFile,
                                G_FILE_COPY_NONE,
                                nullptr,
                                nullptr,
                                nullptr,
                                &error)) {
                            renameError = error && error->message
                                ? error->message
                                : "The file could not be renamed.";
                        }
                        g_clear_error(&error);
                        g_object_unref(oldFile);
                        g_object_unref(newFile);
                    }
                    if (!renameError.empty()) {
                        data->self->showError("Could not rename the file: " + renameError);
                    } else {
                        const std::string oldRelativePath = clip->relativePath;
                        cuelet::SoundClip updatedClip = *plan.updatedClip;
                        if (plan.requiresFileRename) {
                            data->self->audio_.stop(oldRelativePath);
                        }
                        if (mode == LinuxLibraryImportService::RenameMode::RenameFile) {
                            LinuxAudioService::updateDurationMetadata(updatedClip);
                        }
                        *clip = std::move(updatedClip);
                        if (data->self->selectedPaths_.erase(oldRelativePath) > 0) {
                            data->self->selectedPaths_.insert(clip->relativePath);
                        }
                        data->self->saveMetadata();
                        data->self->refreshAll();
                        if (mode == LinuxLibraryImportService::RenameMode::DisplayNameOnly) {
                            data->self->showToast(
                                "Updated the Cuelet name; the external file was unchanged.");
                        }
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

void CueletWindow::eraseClipEntry(const std::string& relativePath)
{
    clips_.erase(
        std::remove_if(clips_.begin(), clips_.end(), [&](const cuelet::SoundClip& clip) {
            return clip.relativePath == relativePath;
        }),
        clips_.end());
    selectedPaths_.erase(relativePath);
    saveMetadata();
    refreshAll();
}

void CueletWindow::confirmRemoveSound(const std::string& relativePath)
{
    const auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    const auto plan = LinuxLibraryImportService::planRemoval(
        *clip,
        libraryPath_,
        LinuxLibraryImportService::RemovalMode::MetadataOnly);
    if (!plan.valid) {
        showError(plan.message);
        return;
    }

    std::string message;
    if (clip->storageMode == cuelet::SoundStorageMode::Linked) {
        message = "The external audio file will stay on disk. Only its Cuelet entry will be removed.";
    } else if (clip->missing) {
        message = "Only the missing Cuelet entry will be removed.";
    } else {
        message = "The audio file will stay on disk. A later rescan can add it to Cuelet again.";
    }
    AdwDialog* dialog = adw_alert_dialog_new("Remove from Library?", message.c_str());
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel", "remove", "Remove", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "remove", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    auto* data = new WindowStringData{this, relativePath};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "remove") == 0) {
            data->self->stopSound(data->value);
            data->self->eraseClipEntry(data->value);
        }
        delete data;
    }, data);
}

void CueletWindow::confirmDeleteManagedFile(const std::string& relativePath)
{
    const auto* clip = clipByPath(relativePath);
    if (!clip) {
        return;
    }
    const auto plan = LinuxLibraryImportService::planRemoval(
        *clip,
        libraryPath_,
        LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
    if (!plan.valid || plan.metadataOnly || !plan.fileToDelete.has_value()) {
        showError(plan.message.empty()
            ? "Only an available managed library file can be deleted."
            : plan.message);
        return;
    }

    const std::string message = "“" + clip->filename
        + "” will be permanently deleted from the Cuelet library folder. This cannot be undone.";
    AdwDialog* dialog = adw_alert_dialog_new("Delete Managed File?", message.c_str());
    adw_alert_dialog_add_responses(
        ADW_ALERT_DIALOG(dialog),
        "cancel", "Cancel",
        "delete", "Delete File",
        nullptr);
    adw_alert_dialog_set_response_appearance(
        ADW_ALERT_DIALOG(dialog), "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

    auto* data = new WindowStringData{this, relativePath};
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(window_), nullptr, +[](GObject* source, GAsyncResult* result, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        const char* response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source), result);
        if (g_strcmp0(response, "delete") == 0) {
            auto* clip = data->self->clipByPath(data->value);
            if (!clip) {
                delete data;
                return;
            }
            const auto freshPlan = LinuxLibraryImportService::planRemoval(
                *clip,
                data->self->libraryPath_,
                LinuxLibraryImportService::RemovalMode::DeleteManagedFile);
            if (!freshPlan.valid
                || freshPlan.metadataOnly
                || !freshPlan.fileToDelete.has_value()) {
                data->self->showError(freshPlan.message.empty()
                    ? "The managed file is no longer safe to delete."
                    : freshPlan.message);
                delete data;
                return;
            }

            data->self->stopSound(data->value);
            const auto removal = LinuxLibraryImportService::executeRemoval(freshPlan);
            if (!removal.succeeded || !removal.fileDeleted) {
                data->self->showError(removal.message.empty()
                    ? "The managed file could not be deleted."
                    : removal.message);
                delete data;
                return;
            }
            data->self->eraseClipEntry(data->value);
        }
        delete data;
    }, data);
}

void CueletWindow::recordShortcut(const std::string& relativePath)
{
    AdwDialog* dialog = adw_alert_dialog_new(
        "Choose Sound Shortcut",
        "Press a key combination, then choose Request Global for a desktop-wide "
        "portal shortcut or Use Locally for a shortcut that works only while "
        "Cuelet is focused. GNOME may show its own confirmation dialog.");
    adw_alert_dialog_add_responses(
        ADW_ALERT_DIALOG(dialog),
        "cancel", "Cancel",
        "local", "Use Locally",
        "global", "Request Global",
        nullptr);
    adw_alert_dialog_set_response_appearance(
        ADW_ALERT_DIALOG(dialog), "global", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "global");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
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
        const bool requestGlobal = g_strcmp0(response, "global") == 0;
        const bool useLocally = g_strcmp0(response, "local") == 0;
        if ((requestGlobal || useLocally) && !data->shortcut.empty()) {
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
                data->shortcut.global = requestGlobal;
                clip->shortcut = data->shortcut;
                data->self->saveMetadata();
                data->self->refreshContent();
                data->self->showToast(requestGlobal
                    ? "Requested a global shortcut. GNOME may ask for confirmation."
                    : "Assigned a local Cuelet shortcut.");
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
