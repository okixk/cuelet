#include "CueletWindow.h"
#include "CueletWindowHelpers.h"
#include "CueletPopoverLifecycle.h"

#include <cmath>
#include <functional>

using namespace cuelet_linux;

namespace {

GtkWidget* makeWaveformPreview(const std::string& seed, bool playing)
{
    GtkWidget* waveform = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_add_css_class(waveform, "cuelet-waveform");
    if (playing) {
        gtk_widget_add_css_class(waveform, "playing");
    }
    gtk_widget_set_hexpand(waveform, TRUE);

    const auto hash = std::hash<std::string>{}(seed);
    for (int index = 0; index < 24; ++index) {
        const auto value = static_cast<int>((hash >> ((index % 8) * 4)) & 0x0f);
        const int height = 8 + ((value + index * 5) % 28);
        GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(bar, "cuelet-waveform-bar");
        gtk_widget_set_size_request(bar, 3, height);
        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(waveform), bar);
    }

    return waveform;
}

void addMenuItem(GMenu* menu, const char* label, const char* action, const char* target)
{
    GMenuItem* item = g_menu_item_new(label, nullptr);
    g_menu_item_set_action_and_target(item, action, "s", target);
    g_menu_append_item(menu, item);
    g_object_unref(item);
}

GtkWidget* makeStatusBadge(const char* text, const char* cssClass)
{
    GtkWidget* badge = gtk_label_new(text);
    gtk_widget_add_css_class(badge, "cuelet-status-badge");
    gtk_widget_add_css_class(badge, cssClass);
    return badge;
}

} // namespace

void CueletWindow::presentPopover(GtkWidget* popover, GtkWidget* source, double x, double y)
{
    GtkWidget* parent = GTK_WIDGET(window_);
    graphene_point_t sourcePoint;
    sourcePoint.x = static_cast<float>(x);
    sourcePoint.y = static_cast<float>(y);
    graphene_point_t parentPoint;
    parentPoint.x = sourcePoint.x;
    parentPoint.y = sourcePoint.y;
    if (!gtk_widget_compute_point(source, parent, &sourcePoint, &parentPoint)) {
        parentPoint.x = 0.0f;
        parentPoint.y = 0.0f;
    }

    gtk_widget_set_parent(popover, parent);
    installDeferredPopoverCleanup(GTK_POPOVER(popover));

    GdkRectangle rect;
    rect.x = static_cast<int>(std::round(parentPoint.x));
    rect.y = static_cast<int>(std::round(parentPoint.y));
    rect.width = 1;
    rect.height = 1;
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
}

GtkWidget* CueletWindow::makeSidebarRow(const std::string& title,
                                        const char* iconName,
                                        SidebarKind kind,
                                        const std::string& categoryId)
{
    GtkWidget* row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "cuelet-sidebar-row");
    g_object_set_data(G_OBJECT(row), "sidebar-kind", GINT_TO_POINTER(static_cast<int>(kind)));
    setObjectString(G_OBJECT(row), "category-id", categoryId);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(box, "cuelet-sidebar-row-content");
    GtkWidget* icon = gtk_image_new_from_icon_name(iconName);
    gtk_widget_set_size_request(icon, 16, 16);
    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    if (!categoryId.empty()) {
        GtkWidget* dot = categoryDot(categoryColor(categoryId));
        gtk_widget_set_halign(dot, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(box), dot);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(row),
        GTK_ACCESSIBLE_PROPERTY_LABEL, title.c_str(),
        -1);

    if (kind == SidebarKind::Category || kind == SidebarKind::AllCategories) {
        GtkGesture* click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
        g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer userData) {
            auto* self = static_cast<CueletWindow*>(userData);
            GtkWidget* row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
            const std::string id = objectString(G_OBJECT(row), "category-id");
            const auto* category = self->categoryById(id);
            GtkWidget* popover = self->makeCategoryPopover(id, category && category->editable);
            self->presentPopover(popover, row, x, y);
        }), this);
        gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
    }

    return row;
}

GtkWidget* CueletWindow::makeSoundCard(const cuelet::SoundClip& clip)
{
    const bool playing = audio_.isPlaying(clip.relativePath);
    const std::string displayName =
        settings_.showFileExtensions ? clip.filename : clip.searchableName();
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(card, "cuelet-sound-card");
    if (playing) {
        gtk_widget_add_css_class(card, "playing");
    }
    if (clip.missing) {
        gtk_widget_add_css_class(card, "missing");
    }
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        gtk_widget_add_css_class(card, "linked");
    }
    if (selectedPaths_.find(clip.relativePath) != selectedPaths_.end()) {
        gtk_widget_add_css_class(card, "selected");
    }
    gtk_widget_set_size_request(card, 200, -1);
    gtk_widget_set_hexpand(card, TRUE);
    gtk_widget_set_halign(card, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(card, FALSE);
    gtk_widget_set_valign(card, GTK_ALIGN_FILL);
    setObjectString(G_OBJECT(card), "relative-path", clip.relativePath);
    std::string accessibleDescription =
        categoryName(clip.categoryId) + ", " + formatDuration(clip.durationSeconds);
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        accessibleDescription += ", linked file";
    }
    if (clip.missing) {
        accessibleDescription += ", file missing";
    }
    if (clip.favorite) {
        accessibleDescription += ", favorite";
    }
    accessibleDescription += clip.missing
        ? ". Playback is unavailable while the file is missing. Press the Menu key for more actions."
        : ". Press Enter to play. Press the Menu key for more actions.";
    setObjectString(G_OBJECT(card), "accessible-label", displayName);
    setObjectString(G_OBJECT(card), "accessible-description", accessibleDescription);

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const std::string playLabel = clip.missing
        ? displayName + " is missing from disk"
        : (playing ? "Stop " : "Play ") + displayName;
    GtkWidget* play = iconButton(
        playing ? "media-playback-stop-symbolic" : "media-playback-start-symbolic",
        playLabel.c_str());
    gtk_widget_add_css_class(play, "circular");
    gtk_widget_set_sensitive(play, !clip.missing);
    if (playing) {
        gtk_widget_add_css_class(play, "suggested-action");
    }
    auto* playData = new WindowStringData{this, clip.relativePath};
    g_signal_connect_data(play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        data->self->togglePlayback(data->value);
    }), playData, +[](gpointer data, GClosure*) { delete static_cast<WindowStringData*>(data); }, G_CONNECT_DEFAULT);
    const std::string favoriteLabel =
        (clip.favorite ? "Remove " : "Add ") + displayName
        + (clip.favorite ? " from favorites" : " to favorites");
    GtkWidget* fav = iconButton(
        clip.favorite ? "starred-symbolic" : "non-starred-symbolic",
        favoriteLabel.c_str());
    gtk_widget_add_css_class(fav, "circular");
    if (clip.favorite) {
        gtk_widget_add_css_class(fav, "accent");
    }
    auto* favData = new WindowStringData{this, clip.relativePath};
    g_signal_connect_data(fav, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        data->self->toggleFavorite(data->value);
    }), favData, +[](gpointer data, GClosure*) { delete static_cast<WindowStringData*>(data); }, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(top), play);
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        gtk_box_append(GTK_BOX(top), makeStatusBadge("Linked", "linked"));
    }
    if (clip.missing) {
        gtk_box_append(GTK_BOX(top), makeStatusBadge("Missing", "missing"));
    }
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top), spacer);
    gtk_box_append(GTK_BOX(top), fav);

    GtkWidget* waveform = makeWaveformPreview(clip.relativePath.empty() ? clip.filename : clip.relativePath, playing);

    GtkWidget* name = gtk_label_new(displayName.c_str());
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(name), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(name), 2);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 22);
    gtk_widget_set_valign(name, GTK_ALIGN_START);
    gtk_widget_add_css_class(name, "cuelet-sound-name");

    GtkWidget* meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* category = categoryChip(categoryName(clip.categoryId), categoryColor(clip.categoryId));
    GtkWidget* duration = secondaryLabel(formatDuration(clip.durationSeconds));
    gtk_box_append(GTK_BOX(meta), category);
    GtkWidget* metaSpacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(metaSpacer, TRUE);
    gtk_box_append(GTK_BOX(meta), metaSpacer);
    if (clip.shortcut) {
        const std::string badge = shortcutBadgeText(clip);
        GtkWidget* shortcut = gtk_label_new(badge.c_str());
        gtk_widget_add_css_class(shortcut, "cuelet-shortcut-badge");
        gtk_box_append(GTK_BOX(meta), shortcut);
    }
    gtk_box_append(GTK_BOX(meta), duration);

    gtk_box_append(GTK_BOX(card), top);
    gtk_box_append(GTK_BOX(card), waveform);
    gtk_box_append(GTK_BOX(card), name);
    gtk_box_append(GTK_BOX(card), meta);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int pressCount, double x, double y, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        GtkWidget* card = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        const std::string path = objectString(G_OBJECT(card), "relative-path");
        const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        if (button == GDK_BUTTON_SECONDARY) {
            self->selectSound(path, false);
            GtkWidget* popover = self->makeSoundPopover(path);
            self->presentPopover(popover, card, x, y);
        } else if (pressCount == 2) {
            self->playSound(path);
        } else {
            const GdkModifierType modifiers =
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
            const bool toggles = (modifiers & GDK_CONTROL_MASK) != 0;

            self->selectSound(path, toggles);
        }
    }), this);
    gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(click));

    return card;
}

GtkWidget* CueletWindow::makeSoundRow(const cuelet::SoundClip& clip)
{
    const bool playing = audio_.isPlaying(clip.relativePath);
    const std::string displayName =
        settings_.showFileExtensions ? clip.filename : clip.searchableName();
    GtkWidget* row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "cuelet-sound-row");
    if (playing) {
        gtk_widget_add_css_class(row, "playing");
    }
    if (clip.missing) {
        gtk_widget_add_css_class(row, "missing");
    }
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        gtk_widget_add_css_class(row, "linked");
    }
    if (selectedPaths_.find(clip.relativePath) != selectedPaths_.end()) {
        gtk_widget_add_css_class(row, "selected");
    }
    gtk_widget_set_focusable(row, TRUE);
    setObjectString(G_OBJECT(row), "relative-path", clip.relativePath);
    std::string accessibleDescription =
        categoryName(clip.categoryId) + ", " + formatDuration(clip.durationSeconds);
    if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        accessibleDescription += ", linked file";
    }
    if (clip.missing) {
        accessibleDescription += ", file missing";
    }
    if (clip.favorite) {
        accessibleDescription += ", favorite";
    }
    accessibleDescription += clip.missing
        ? ". Playback is unavailable while the file is missing. Press the Menu key for more actions."
        : ". Press Enter to play. Press the Menu key for more actions.";
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(row),
        GTK_ACCESSIBLE_PROPERTY_LABEL, displayName.c_str(),
        GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, accessibleDescription.c_str(),
        -1);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(box, "cuelet-sound-row-content");

    const std::string playLabel = clip.missing
        ? displayName + " is missing from disk"
        : (playing ? "Stop " : "Play ") + displayName;
    GtkWidget* play = iconButton(
        playing ? "media-playback-stop-symbolic" : "media-playback-start-symbolic",
        playLabel.c_str());
    gtk_widget_add_css_class(play, "circular");
    gtk_widget_set_sensitive(play, !clip.missing);
    if (playing) {
        gtk_widget_add_css_class(play, "suggested-action");
    }
    gtk_widget_set_valign(play, GTK_ALIGN_CENTER);
    auto* playData = new WindowStringData{this, clip.relativePath};
    g_signal_connect_data(play, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        data->self->togglePlayback(data->value);
    }), playData, +[](gpointer data, GClosure*) { delete static_cast<WindowStringData*>(data); }, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(box), play);

    GtkWidget* textBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(textBox, TRUE);
    gtk_widget_set_valign(textBox, GTK_ALIGN_CENTER);
    GtkWidget* name = gtk_label_new(displayName.c_str());
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(name, "cuelet-list-name");
    std::string pathText = clip.relativePath.empty() ? clip.filename : clip.relativePath;
    if (clip.storageMode == cuelet::SoundStorageMode::Linked && clip.missing) {
        pathText = "Linked · Missing · " + pathText;
    } else if (clip.storageMode == cuelet::SoundStorageMode::Linked) {
        pathText = "Linked · " + pathText;
    } else if (clip.missing) {
        pathText = "Missing · " + pathText;
    }
    GtkWidget* sub = secondaryLabel(pathText);
    gtk_box_append(GTK_BOX(textBox), name);
    gtk_box_append(GTK_BOX(textBox), sub);
    gtk_box_append(GTK_BOX(box), textBox);

    GtkWidget* chip = categoryChip(categoryName(clip.categoryId), categoryColor(clip.categoryId));
    gtk_widget_set_size_request(chip, 120, -1);
    gtk_widget_set_valign(chip, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), chip);

    GtkWidget* duration = secondaryLabel(formatDuration(clip.durationSeconds));
    gtk_widget_add_css_class(duration, "cuelet-duration");
    gtk_widget_set_size_request(duration, 48, -1);
    gtk_widget_set_valign(duration, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), duration);

    if (clip.shortcut) {
        const std::string badge = shortcutBadgeText(clip);
        GtkWidget* shortcut = gtk_label_new(badge.c_str());
        gtk_widget_add_css_class(shortcut, "cuelet-shortcut-badge");
        gtk_box_append(GTK_BOX(box), shortcut);
    }
    const std::string favoriteLabel =
        (clip.favorite ? "Remove " : "Add ") + displayName
        + (clip.favorite ? " from favorites" : " to favorites");
    GtkWidget* fav = iconButton(
        clip.favorite ? "starred-symbolic" : "non-starred-symbolic",
        favoriteLabel.c_str());
    gtk_widget_add_css_class(fav, "circular");
    if (clip.favorite) {
        gtk_widget_add_css_class(fav, "accent");
    }
    gtk_widget_set_valign(fav, GTK_ALIGN_CENTER);
    auto* favData = new WindowStringData{this, clip.relativePath};
    g_signal_connect_data(fav, "clicked", G_CALLBACK(+[](GtkButton*, gpointer userData) {
        auto* data = static_cast<WindowStringData*>(userData);
        data->self->toggleFavorite(data->value);
    }), favData, +[](gpointer data, GClosure*) { delete static_cast<WindowStringData*>(data); }, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(box), fav);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int pressCount, double x, double y, gpointer userData) {
        auto* self = static_cast<CueletWindow*>(userData);
        GtkWidget* row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        const std::string path = objectString(G_OBJECT(row), "relative-path");
        const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
        if (button == GDK_BUTTON_SECONDARY) {
            self->selectSound(path, false);
            GtkWidget* popover = self->makeSoundPopover(path);
            self->presentPopover(popover, row, x, y);
        } else if (pressCount == 2) {
            self->playSound(path);
        } else {
            const GdkModifierType modifiers =
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
            self->selectSound(path, (modifiers & GDK_CONTROL_MASK) != 0);
        }
    }), this);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
    return row;
}

GtkWidget* CueletWindow::makeSoundPopover(const std::string& relativePath)
{
    const auto* clip = clipByPath(relativePath);
    GMenu* menu = g_menu_new();
    if (!clip) {
        g_menu_append(menu, "Sound Unavailable", nullptr);
        GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
        g_object_unref(menu);
        return popover;
    }
    const auto policy = soundMenuPolicy(clip);

    GMenu* playbackSection = g_menu_new();
    if (!policy.canPlay) {
        g_menu_append(playbackSection, "File Missing", nullptr);
    } else {
        addMenuItem(playbackSection, "Play", "win.play-sound", relativePath.c_str());
        if (audio_.isPlaying(relativePath)) {
            addMenuItem(playbackSection, "Stop", "win.stop-sound", relativePath.c_str());
        }
    }
    addMenuItem(playbackSection, clip->favorite ? "Unfavorite" : "Favorite", "win.toggle-favorite", relativePath.c_str());
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(playbackSection));
    g_object_unref(playbackSection);

    GMenu* assignMenu = g_menu_new();
    for (const auto& category : categories_) {
        GMenuItem* item = g_menu_item_new(category.name.c_str(), nullptr);
        g_menu_item_set_action_and_target(
            item,
            "win.assign-category",
            "(ss)",
            relativePath.c_str(),
            category.id.c_str());
        g_menu_append_item(assignMenu, item);
        g_object_unref(item);
    }
    addMenuItem(assignMenu, "New Category...", "win.new-category-for-sound", relativePath.c_str());
    g_menu_append_submenu(menu, "Assign Category", G_MENU_MODEL(assignMenu));
    g_object_unref(assignMenu);

    GMenu* editSection = g_menu_new();
    addMenuItem(editSection, "Change Shortcut...", "win.record-shortcut", relativePath.c_str());
    addMenuItem(editSection, "Copy GNOME Shortcut Command", "win.copy-shortcut-command", relativePath.c_str());
    if (clip->shortcut) {
        addMenuItem(editSection, "Clear Shortcut", "win.clear-shortcut", relativePath.c_str());
    }
    if (policy.canRename) {
        addMenuItem(editSection, "Rename...", "win.rename-sound", relativePath.c_str());
    }
    if (policy.canReveal) {
        addMenuItem(editSection, "Reveal in Files", "win.reveal-sound", relativePath.c_str());
    }
    addMenuItem(editSection, "Remove from Library...", "win.remove-sound", relativePath.c_str());
    if (policy.canDeleteManagedFile) {
        addMenuItem(
            editSection,
            "Delete Managed File...",
            "win.delete-managed-file",
            relativePath.c_str());
    }
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(editSection));
    g_object_unref(editSection);

    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    return popover;
}

GtkWidget* CueletWindow::makeCategoryPopover(const std::string& categoryId, bool editable)
{
    GMenu* menu = g_menu_new();

    if (editable) {
        GMenu* editSection = g_menu_new();
        addMenuItem(editSection, "Edit Category…", "win.rename-category", categoryId.c_str());
        addMenuItem(editSection, "Delete Category", "win.delete-category", categoryId.c_str());
        g_menu_append_section(menu, nullptr, G_MENU_MODEL(editSection));
        g_object_unref(editSection);
    }

    GMenu* createSection = g_menu_new();
    g_menu_append(createSection, "New Category", "win.new-category");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(createSection));
    g_object_unref(createSection);

    GtkWidget* popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    return popover;
}
