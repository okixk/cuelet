#pragma once

#include "cuelet/SoundTypes.h"

#include <adwaita.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

class CueletWindow;

namespace cuelet_linux {

struct WindowStringData {
    CueletWindow* self = nullptr;
    std::string value;
};

struct CategoryActionData {
    CueletWindow* self = nullptr;
    std::string categoryId;
};

struct TextDialogData {
    CueletWindow* self = nullptr;
    GtkWidget* entry = nullptr;
    std::string context;
    std::string assignPath;
};

struct CategoryEditDialogData {
    CueletWindow* self = nullptr;
    GtkWidget* nameEntry = nullptr;
    GtkWidget* colorDropDown = nullptr;
    GtkWidget* iconDropDown = nullptr;
    std::string categoryId;
};

struct ShortcutDialogData {
    CueletWindow* self = nullptr;
    GtkWidget* label = nullptr;
    std::string relativePath;
    cuelet::Shortcut shortcut;
};

struct CategoryIconChoice {
    std::string label;
    std::string id;
    std::string linuxIconName;
};

inline const std::vector<std::pair<std::string, std::string>>& colorPalette()
{
    static const std::vector<std::pair<std::string, std::string>> colors = {
        {"Gray", "#8E8E93"},
        {"Blue", "#3478F6"},
        {"Teal", "#009688"},
        {"Green", "#2E8B57"},
        {"Yellow", "#B38B00"},
        {"Orange", "#D9822B"},
        {"Red", "#D64545"},
        {"Pink", "#D65780"},
        {"Purple", "#AF52DE"},
    };
    return colors;
}

inline guint categoryColorIndex(const std::string& colorHex)
{
    const auto& colors = colorPalette();
    const auto found = std::find_if(colors.begin(), colors.end(), [&](const auto& item) {
        return item.second == colorHex;
    });
    return found == colors.end() ? 0 : static_cast<guint>(std::distance(colors.begin(), found));
}

inline const std::vector<CategoryIconChoice>& iconChoices()
{
    static const std::vector<CategoryIconChoice> icons = {
        {"Tag", "tag", "tag-symbolic"},
        {"Folder", "folder", "folder-symbolic"},
        {"Music Note", "music-note", "audio-x-generic-symbolic"},
        {"Audio Speakers", "audio-speakers", "audio-speakers-symbolic"},
        {"Waveform", "waveform", "sound-wave-symbolic"},
        {"Bell", "bell", "preferences-system-notifications-symbolic"},
        {"Sparkles", "sparkles", "emblem-default-symbolic"},
        {"Weather Showers", "weather-showers", "weather-showers-symbolic"},
        {"Games", "applications-games", "applications-games-symbolic"},
        {"Microphone", "microphone", "audio-input-microphone-symbolic"},
        {"Chat Message", "chat-message", "chat-message-new-symbolic"},
        {"Star", "star", "starred-symbolic"},
        {"Heart", "heart", "emblem-favorite-symbolic"},
        {"Bolt", "bolt", "thunderbolt-symbolic"},
        {"Flame", "flame", "weather-clear-symbolic"},
        {"Smile", "face-smile", "face-smile-symbolic"},
    };
    return icons;
}

inline std::string canonicalCategoryIconId(const std::string& value)
{
    if (value.empty()) {
        return "tag";
    }

    for (const auto& choice : iconChoices()) {
        if (value == choice.id || value == choice.linuxIconName) {
            return choice.id;
        }
    }

    static const std::vector<std::pair<std::string, std::string>> aliases = {
        {"tray", "folder"},
        {"music.note", "music-note"},
        {"media-playlist-symbolic", "music-note"},
        {"speaker.wave.2", "audio-speakers"},
        {"waveform", "waveform"},
        {"cloud.rain", "weather-showers"},
        {"weather-showers-scattered-symbolic", "weather-showers"},
        {"gamecontroller", "applications-games"},
        {"games-app-symbolic", "applications-games"},
        {"mic", "microphone"},
        {"message", "chat-message"},
        {"quote.bubble", "chat-message"},
        {"weather-storm-symbolic", "bolt"},
        {"bolt.fill", "bolt"},
        {"flame.fill", "flame"},
        {"face.smiling", "face-smile"},
        {"wand.and.stars", "sparkles"},
    };
    const auto alias = std::find_if(aliases.begin(), aliases.end(), [&](const auto& item) {
        return item.first == value;
    });
    return alias == aliases.end() ? value : alias->second;
}

inline guint categoryIconIndex(const std::string& value)
{
    const auto canonicalId = canonicalCategoryIconId(value);
    const auto& icons = iconChoices();
    const auto found = std::find_if(icons.begin(), icons.end(), [&](const auto& item) {
        return item.id == canonicalId;
    });
    return found == icons.end() ? 0 : static_cast<guint>(std::distance(icons.begin(), found));
}

inline std::string linuxCategoryIconName(const std::string& value)
{
    const auto id = canonicalCategoryIconId(value);
    const auto choice = std::find_if(iconChoices().begin(), iconChoices().end(), [&](const auto& item) {
        return item.id == id;
    });
    return choice == iconChoices().end() ? (value.empty() ? "tag-symbolic" : value) : choice->linuxIconName;
}

inline std::string formatDuration(double seconds)
{
    if (seconds <= 0.0 || !std::isfinite(seconds)) {
        return "--:--";
    }
    const auto rounded = static_cast<int>(std::round(seconds));
    std::ostringstream stream;
    stream << rounded / 60 << ":";
    if (rounded % 60 < 10) {
        stream << "0";
    }
    stream << rounded % 60;
    return stream.str();
}

inline std::string shortcutLabel(guint keyval, GdkModifierType state)
{
    std::string label;
    if ((state & GDK_CONTROL_MASK) != 0) {
        label += "Ctrl+";
    }
    if ((state & GDK_ALT_MASK) != 0) {
        label += "Alt+";
    }
    if ((state & GDK_SUPER_MASK) != 0) {
        label += "Super+";
    }
    if ((state & GDK_SHIFT_MASK) != 0) {
        label += "Shift+";
    }

    const char* keyName = gdk_keyval_name(gdk_keyval_to_upper(keyval));
    label += keyName ? keyName : "Key";
    return label;
}

inline GdkModifierType shortcutModifierMask(GdkModifierType state)
{
    return static_cast<GdkModifierType>(state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK));
}

inline bool isModifierOnly(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:
    case GDK_KEY_Super_L:
    case GDK_KEY_Super_R:
        return true;
    default:
        return false;
    }
}

inline GtkWidget* iconButton(const char* iconName, const char* tooltip)
{
    GtkWidget* button = gtk_button_new_from_icon_name(iconName);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "flat");
    return button;
}

inline GtkWidget* textIconButton(const char* iconName, const char* label, const char* tooltip)
{
    GtkWidget* button = gtk_button_new();
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* image = gtk_image_new_from_icon_name(iconName);
    GtkWidget* text = gtk_label_new(label);
    gtk_box_append(GTK_BOX(content), image);
    gtk_box_append(GTK_BOX(content), text);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_widget_set_tooltip_text(button, tooltip);
    return button;
}

inline GtkWidget* linkedBox()
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "linked");
    return box;
}

inline GtkWidget* titleLabel(const std::string& text)
{
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "heading");
    return label;
}

inline GtkWidget* secondaryLabel(const std::string& text)
{
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

inline GtkWidget* categoryDot(const std::string& color)
{
    std::string markup = "<span foreground=\"" + color + "\">●</span>";
    GtkWidget* label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
    return label;
}

inline GtkWidget* categoryChip(const std::string& name, const std::string& color)
{
    GtkWidget* chip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_add_css_class(chip, "cuelet-category-chip");
    gtk_box_append(GTK_BOX(chip), categoryDot(color));
    GtkWidget* label = gtk_label_new(name.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 18);
    gtk_box_append(GTK_BOX(chip), label);
    return chip;
}

inline void setObjectString(GObject* object, const char* key, const std::string& value)
{
    g_object_set_data_full(object, key, g_strdup(value.c_str()), g_free);
}

inline std::string objectString(GObject* object, const char* key)
{
    const char* value = static_cast<const char*>(g_object_get_data(object, key));
    return value ? value : "";
}

inline cuelet::SortOption sortOptionFromId(const std::string& id)
{
    if (id == "nameDescending") {
        return cuelet::SortOption::NameDescending;
    }
    if (id == "latestAdded") {
        return cuelet::SortOption::LatestAdded;
    }
    if (id == "oldestAdded") {
        return cuelet::SortOption::OldestAdded;
    }
    if (id == "durationShortest") {
        return cuelet::SortOption::DurationShortest;
    }
    if (id == "durationLongest") {
        return cuelet::SortOption::DurationLongest;
    }
    if (id == "category") {
        return cuelet::SortOption::Category;
    }
    return cuelet::SortOption::NameAscending;
}

inline std::string sortOptionTitle(cuelet::SortOption option)
{
    switch (option) {
    case cuelet::SortOption::NameAscending:
        return "Name A-Z";
    case cuelet::SortOption::NameDescending:
        return "Name Z-A";
    case cuelet::SortOption::LatestAdded:
        return "Latest Added";
    case cuelet::SortOption::OldestAdded:
        return "Oldest Added";
    case cuelet::SortOption::DurationShortest:
        return "Duration Shortest";
    case cuelet::SortOption::DurationLongest:
        return "Duration Longest";
    case cuelet::SortOption::Category:
        return "Category";
    }
    return "Name A-Z";
}

inline std::filesystem::path duplicateImportDestination(const std::filesystem::path& library,
                                                        const std::filesystem::path& source)
{
    auto destination = library / source.filename();
    int index = 1;
    while (std::filesystem::exists(destination)) {
        const auto stem = source.stem().string();
        const auto extension = source.extension().string();
        destination = library / (stem + " (" + std::to_string(index) + ")" + extension);
        ++index;
    }
    return destination;
}

} // namespace cuelet_linux
