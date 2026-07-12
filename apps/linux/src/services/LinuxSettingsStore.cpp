#include "services/LinuxSettingsStore.h"

#include <json-glib/json-glib.h>

#include <cstdlib>

namespace {

std::filesystem::path configHome()
{
    if (const char* config = std::getenv("XDG_CONFIG_HOME")) {
        if (*config) {
            return config;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) {
            return std::filesystem::path(home) / ".config";
        }
    }
    return std::filesystem::temp_directory_path();
}

const char* sortOptionToString(cuelet::SortOption option)
{
    switch (option) {
    case cuelet::SortOption::NameAscending:
        return "nameAscending";
    case cuelet::SortOption::NameDescending:
        return "nameDescending";
    case cuelet::SortOption::LatestAdded:
        return "latestAdded";
    case cuelet::SortOption::OldestAdded:
        return "oldestAdded";
    case cuelet::SortOption::DurationShortest:
        return "durationShortest";
    case cuelet::SortOption::DurationLongest:
        return "durationLongest";
    case cuelet::SortOption::Category:
        return "category";
    }
    return "nameAscending";
}

cuelet::SortOption sortOptionFromString(const char* value)
{
    const std::string text = value ? value : "";
    if (text == "nameDescending") {
        return cuelet::SortOption::NameDescending;
    }
    if (text == "latestAdded") {
        return cuelet::SortOption::LatestAdded;
    }
    if (text == "oldestAdded") {
        return cuelet::SortOption::OldestAdded;
    }
    if (text == "durationShortest") {
        return cuelet::SortOption::DurationShortest;
    }
    if (text == "durationLongest") {
        return cuelet::SortOption::DurationLongest;
    }
    if (text == "category") {
        return cuelet::SortOption::Category;
    }
    return cuelet::SortOption::NameAscending;
}

const char* stringMember(JsonObject* object, const char* name, const char* fallback = "")
{
    return json_object_has_member(object, name) && json_object_get_string_member(object, name)
        ? json_object_get_string_member(object, name)
        : fallback;
}

bool boolMember(JsonObject* object, const char* name, bool fallback)
{
    return json_object_has_member(object, name) ? json_object_get_boolean_member(object, name) : fallback;
}

double doubleMember(JsonObject* object, const char* name, double fallback)
{
    return json_object_has_member(object, name) ? json_object_get_double_member(object, name) : fallback;
}

} // namespace

LinuxSettings LinuxSettingsStore::load() const
{
    lastError_.clear();
    LinuxSettings settings;
    const auto path = filePath();
    if (!std::filesystem::exists(path)) {
        return settings;
    }

    GError* error = nullptr;
    JsonParser* parser = json_parser_new();
    if (!json_parser_load_from_file(parser, path.c_str(), &error)) {
        lastError_ = error && error->message ? error->message : "Could not load Linux settings.";
        if (error) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return settings;
    }

    JsonNode* root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        lastError_ = "Linux settings root is not an object.";
        g_object_unref(parser);
        return settings;
    }

    JsonObject* object = json_node_get_object(root);
    settings.libraryPath = stringMember(object, "libraryPath");
    settings.viewMode = stringMember(object, "viewMode", settings.viewMode.c_str());
    settings.sortOption = sortOptionFromString(stringMember(object, "sortOption", "nameAscending"));
    settings.volume = doubleMember(object, "volume", settings.volume);
    settings.allowsSimultaneousPlayback = boolMember(
        object,
        "allowsSimultaneousPlayback",
        settings.allowsSimultaneousPlayback);
    settings.showFileExtensions = boolMember(object, "showFileExtensions", settings.showFileExtensions);
    settings.scansSubfolders = boolMember(object, "scansSubfolders", settings.scansSubfolders);
    settings.showsDemoLibrary = boolMember(object, "showsDemoLibrary", settings.showsDemoLibrary);
    settings.copiesImportedFiles = boolMember(object, "copiesImportedFiles", settings.copiesImportedFiles);
    settings.appearanceMode = stringMember(object, "appearanceMode", settings.appearanceMode.c_str());
    settings.outputDevice = stringMember(object, "outputDevice");

    g_object_unref(parser);
    return settings;
}

bool LinuxSettingsStore::save(const LinuxSettings& settings) const
{
    lastError_.clear();
    const auto path = filePath();
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        lastError_ = "Could not create settings directory.";
        return false;
    }

    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "libraryPath");
    json_builder_add_string_value(builder, settings.libraryPath.c_str());
    json_builder_set_member_name(builder, "viewMode");
    json_builder_add_string_value(builder, settings.viewMode.c_str());
    json_builder_set_member_name(builder, "sortOption");
    json_builder_add_string_value(builder, sortOptionToString(settings.sortOption));
    json_builder_set_member_name(builder, "volume");
    json_builder_add_double_value(builder, settings.volume);
    json_builder_set_member_name(builder, "allowsSimultaneousPlayback");
    json_builder_add_boolean_value(builder, settings.allowsSimultaneousPlayback);
    json_builder_set_member_name(builder, "showFileExtensions");
    json_builder_add_boolean_value(builder, settings.showFileExtensions);
    json_builder_set_member_name(builder, "scansSubfolders");
    json_builder_add_boolean_value(builder, settings.scansSubfolders);
    json_builder_set_member_name(builder, "showsDemoLibrary");
    json_builder_add_boolean_value(builder, settings.showsDemoLibrary);
    json_builder_set_member_name(builder, "copiesImportedFiles");
    json_builder_add_boolean_value(builder, settings.copiesImportedFiles);
    json_builder_set_member_name(builder, "appearanceMode");
    json_builder_add_string_value(builder, settings.appearanceMode.c_str());
    json_builder_set_member_name(builder, "outputDevice");
    json_builder_add_string_value(builder, settings.outputDevice.c_str());
    json_builder_end_object(builder);

    JsonGenerator* generator = json_generator_new();
    JsonNode* root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);

    GError* error = nullptr;
    const bool ok = json_generator_to_file(generator, path.c_str(), &error);
    if (!ok) {
        lastError_ = error && error->message ? error->message : "Could not write Linux settings.";
        if (error) {
            g_error_free(error);
        }
    }

    json_node_unref(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return ok;
}

std::filesystem::path LinuxSettingsStore::filePath() const
{
    return configHome() / "cuelet" / "settings.json";
}

std::string LinuxSettingsStore::lastError() const
{
    return lastError_;
}
