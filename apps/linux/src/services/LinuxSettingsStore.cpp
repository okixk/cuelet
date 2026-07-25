#include "services/LinuxSettingsStore.h"

#include <json-glib/json-glib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace {

std::optional<std::filesystem::path> configHome()
{
    if (const char* config = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path path(config);
        if (*config && path.is_absolute()) {
            return path;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path path(home);
        if (*home && path.is_absolute()) {
            return path / ".config";
        }
    }
    return std::nullopt;
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

std::optional<cuelet::SortOption> sortOptionFromString(std::string_view value)
{
    if (value == "nameAscending") {
        return cuelet::SortOption::NameAscending;
    }
    if (value == "nameDescending") {
        return cuelet::SortOption::NameDescending;
    }
    if (value == "latestAdded") {
        return cuelet::SortOption::LatestAdded;
    }
    if (value == "oldestAdded") {
        return cuelet::SortOption::OldestAdded;
    }
    if (value == "durationShortest") {
        return cuelet::SortOption::DurationShortest;
    }
    if (value == "durationLongest") {
        return cuelet::SortOption::DurationLongest;
    }
    if (value == "category") {
        return cuelet::SortOption::Category;
    }
    return std::nullopt;
}

bool isAllowedViewMode(std::string_view value)
{
    return value == "grid" || value == "list";
}

bool isAllowedAppearanceMode(std::string_view value)
{
    return value == "system" || value == "light" || value == "dark";
}

std::optional<std::string> stringMember(
    JsonObject* object,
    const char* name,
    bool& invalidValue)
{
    if (!json_object_has_member(object, name)) {
        return std::nullopt;
    }

    JsonNode* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)
        || json_node_get_value_type(node) != G_TYPE_STRING) {
        invalidValue = true;
        return std::nullopt;
    }

    const char* value = json_node_get_string(node);
    if (!value) {
        invalidValue = true;
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<bool> boolMember(
    JsonObject* object,
    const char* name,
    bool& invalidValue)
{
    if (!json_object_has_member(object, name)) {
        return std::nullopt;
    }

    JsonNode* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)
        || json_node_get_value_type(node) != G_TYPE_BOOLEAN) {
        invalidValue = true;
        return std::nullopt;
    }
    return json_node_get_boolean(node);
}

std::optional<double> doubleMember(
    JsonObject* object,
    const char* name,
    bool& invalidValue)
{
    if (!json_object_has_member(object, name)) {
        return std::nullopt;
    }

    JsonNode* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        invalidValue = true;
        return std::nullopt;
    }

    const GType valueType = json_node_get_value_type(node);
    if (valueType != G_TYPE_DOUBLE && valueType != G_TYPE_INT64) {
        invalidValue = true;
        return std::nullopt;
    }

    const double value = json_node_get_double(node);
    if (!std::isfinite(value)) {
        invalidValue = true;
        return std::nullopt;
    }
    return value;
}

std::optional<std::vector<std::string>> stringArrayMember(
    JsonObject* object,
    const char* name,
    bool& invalidValue)
{
    if (!json_object_has_member(object, name)) {
        return std::nullopt;
    }

    JsonNode* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_ARRAY(node)) {
        invalidValue = true;
        return std::nullopt;
    }

    constexpr guint maximumEntries = 4096;
    JsonArray* array = json_node_get_array(node);
    const guint length = json_array_get_length(array);
    if (length > maximumEntries) {
        invalidValue = true;
    }

    std::vector<std::string> values;
    values.reserve(std::min(length, maximumEntries));
    for (guint index = 0; index < std::min(length, maximumEntries); ++index) {
        JsonNode* element = json_array_get_element(array, index);
        if (!element || !JSON_NODE_HOLDS_VALUE(element)
            || json_node_get_value_type(element) != G_TYPE_STRING) {
            invalidValue = true;
            continue;
        }
        const char* value = json_node_get_string(element);
        if (!value) {
            invalidValue = true;
            continue;
        }
        values.emplace_back(value);
    }
    return values;
}

double normalizedVolume(double volume)
{
    if (!std::isfinite(volume)) {
        return LinuxSettings{}.volume;
    }
    return std::clamp(volume, 0.0, 1.0);
}

std::vector<std::string> normalizedApprovedLinkedPaths(
    const std::vector<std::string>& paths,
    bool* invalidValue = nullptr)
{
    std::vector<std::string> normalized;
    std::set<std::string> seen;
    normalized.reserve(paths.size());
    for (const auto& value : paths) {
        if (value.empty() || value.size() > 4096) {
            if (invalidValue) {
                *invalidValue = true;
            }
            continue;
        }
        const auto path = std::filesystem::u8path(value);
        if (!path.is_absolute()) {
            if (invalidValue) {
                *invalidValue = true;
            }
            continue;
        }

        const auto safePath = path.lexically_normal();
        const auto serialized = safePath.generic_u8string();
        if (serialized.empty() || safePath == safePath.root_path()) {
            if (invalidValue) {
                *invalidValue = true;
            }
            continue;
        }
        if (seen.insert(serialized).second) {
            normalized.push_back(serialized);
        }
    }
    return normalized;
}

LinuxSettings normalizedSettings(const LinuxSettings& input)
{
    return LinuxSettings{
        input.libraryPath,
        isAllowedViewMode(input.viewMode) ? input.viewMode : "grid",
        sortOptionFromString(sortOptionToString(input.sortOption))
            .value_or(cuelet::SortOption::NameAscending),
        normalizedVolume(input.volume),
        input.allowsSimultaneousPlayback,
        input.showFileExtensions,
        input.scansSubfolders,
        input.showsDemoLibrary,
        input.copiesImportedFiles,
        isAllowedAppearanceMode(input.appearanceMode) ? input.appearanceMode : "system",
        input.outputDevice,
        normalizedApprovedLinkedPaths(input.approvedLinkedPaths),
    };
}

} // namespace

LinuxSettings LinuxSettingsStore::load() const
{
    lastError_.clear();
    LinuxSettings settings;
    const auto path = filePath();
    if (path.empty()) {
        lastError_ = "Could not resolve a per-user Linux settings directory.";
        return settings;
    }
    std::error_code fileError;
    const bool exists = std::filesystem::exists(path, fileError);
    if (fileError) {
        lastError_ = "Could not inspect Linux settings file.";
        return settings;
    }
    if (!exists) {
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
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        lastError_ = "Linux settings root is not an object.";
        g_object_unref(parser);
        return settings;
    }

    JsonObject* object = json_node_get_object(root);
    bool invalidValue = false;
    if (const auto value = stringMember(object, "libraryPath", invalidValue)) {
        settings.libraryPath = *value;
    }
    if (const auto value = stringMember(object, "viewMode", invalidValue)) {
        if (isAllowedViewMode(*value)) {
            settings.viewMode = *value;
        } else {
            invalidValue = true;
        }
    }
    if (const auto value = stringMember(object, "sortOption", invalidValue)) {
        if (const auto sortOption = sortOptionFromString(*value)) {
            settings.sortOption = *sortOption;
        } else {
            invalidValue = true;
        }
    }
    if (const auto value = doubleMember(object, "volume", invalidValue)) {
        settings.volume = normalizedVolume(*value);
    }
    if (const auto value = boolMember(object, "allowsSimultaneousPlayback", invalidValue)) {
        settings.allowsSimultaneousPlayback = *value;
    }
    if (const auto value = boolMember(object, "showFileExtensions", invalidValue)) {
        settings.showFileExtensions = *value;
    }
    if (const auto value = boolMember(object, "scansSubfolders", invalidValue)) {
        settings.scansSubfolders = *value;
    }
    if (const auto value = boolMember(object, "showsDemoLibrary", invalidValue)) {
        settings.showsDemoLibrary = *value;
    }
    if (const auto value = boolMember(object, "copiesImportedFiles", invalidValue)) {
        settings.copiesImportedFiles = *value;
    }
    if (const auto value = stringMember(object, "appearanceMode", invalidValue)) {
        if (isAllowedAppearanceMode(*value)) {
            settings.appearanceMode = *value;
        } else {
            invalidValue = true;
        }
    }
    if (const auto value = stringMember(object, "outputDevice", invalidValue)) {
        settings.outputDevice = *value;
    }
    if (const auto value = stringArrayMember(
            object, "approvedLinkedPaths", invalidValue)) {
        settings.approvedLinkedPaths =
            normalizedApprovedLinkedPaths(*value, &invalidValue);
    }

    g_object_unref(parser);
    if (invalidValue) {
        lastError_ = "Ignored invalid values in Linux settings.";
    }
    return settings;
}

bool LinuxSettingsStore::save(const LinuxSettings& settings) const
{
    lastError_.clear();
    const LinuxSettings normalized = normalizedSettings(settings);
    const auto path = filePath();
    if (path.empty()) {
        lastError_ = "Could not resolve a per-user Linux settings directory.";
        return false;
    }
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        lastError_ = "Could not create settings directory.";
        return false;
    }

    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "libraryPath");
    json_builder_add_string_value(builder, normalized.libraryPath.c_str());
    json_builder_set_member_name(builder, "viewMode");
    json_builder_add_string_value(builder, normalized.viewMode.c_str());
    json_builder_set_member_name(builder, "sortOption");
    json_builder_add_string_value(builder, sortOptionToString(normalized.sortOption));
    json_builder_set_member_name(builder, "volume");
    json_builder_add_double_value(builder, normalized.volume);
    json_builder_set_member_name(builder, "allowsSimultaneousPlayback");
    json_builder_add_boolean_value(builder, normalized.allowsSimultaneousPlayback);
    json_builder_set_member_name(builder, "showFileExtensions");
    json_builder_add_boolean_value(builder, normalized.showFileExtensions);
    json_builder_set_member_name(builder, "scansSubfolders");
    json_builder_add_boolean_value(builder, normalized.scansSubfolders);
    json_builder_set_member_name(builder, "showsDemoLibrary");
    json_builder_add_boolean_value(builder, normalized.showsDemoLibrary);
    json_builder_set_member_name(builder, "copiesImportedFiles");
    json_builder_add_boolean_value(builder, normalized.copiesImportedFiles);
    json_builder_set_member_name(builder, "appearanceMode");
    json_builder_add_string_value(builder, normalized.appearanceMode.c_str());
    json_builder_set_member_name(builder, "outputDevice");
    json_builder_add_string_value(builder, normalized.outputDevice.c_str());
    json_builder_set_member_name(builder, "approvedLinkedPaths");
    json_builder_begin_array(builder);
    for (const auto& approvedPath : normalized.approvedLinkedPaths) {
        json_builder_add_string_value(builder, approvedPath.c_str());
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonGenerator* generator = json_generator_new();
    JsonNode* root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);

    gsize length = 0;
    gchar* contents = json_generator_to_data(generator, &length);
    GError* error = nullptr;
    const bool ok = contents
        && g_file_set_contents_full(
            path.c_str(),
            contents,
            static_cast<gssize>(length),
            static_cast<GFileSetContentsFlags>(
                G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE),
            0600,
            &error);
    if (!ok) {
        lastError_ = error && error->message ? error->message : "Could not write Linux settings.";
        if (error) {
            g_error_free(error);
        }
    }

    g_free(contents);
    json_node_unref(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return ok;
}

std::filesystem::path LinuxSettingsStore::filePath() const
{
    const auto config = configHome();
    return config ? *config / "cuelet" / "settings.json" : std::filesystem::path{};
}

std::string LinuxSettingsStore::lastError() const
{
    return lastError_;
}

bool LinuxSettingsStore::isLinkedPathApproved(
    const LinuxSettings& settings,
    const std::filesystem::path& path)
{
    const auto normalized = normalizedApprovedLinkedPaths({path.generic_u8string()});
    return normalized.size() == 1
        && std::find(
            settings.approvedLinkedPaths.begin(),
            settings.approvedLinkedPaths.end(),
            normalized.front()) != settings.approvedLinkedPaths.end();
}

LinuxSettings LinuxSettingsStore::approvingLinkedPath(
    const LinuxSettings& settings,
    const std::filesystem::path& path)
{
    LinuxSettings updated = settings;
    updated.approvedLinkedPaths.push_back(path.generic_u8string());
    updated.approvedLinkedPaths =
        normalizedApprovedLinkedPaths(updated.approvedLinkedPaths);
    return updated;
}
