#include "cuelet/MetadataStore.h"

#include <json-glib/json-glib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace cuelet {

namespace {

JsonNode* memberNode(JsonObject* object, const char* name)
{
    return object && json_object_has_member(object, name)
        ? json_object_get_member(object, name)
        : nullptr;
}

JsonObject* objectMember(JsonObject* object, const char* name)
{
    JsonNode* node = memberNode(object, name);
    return node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : nullptr;
}

JsonArray* arrayMember(JsonObject* object, const char* name)
{
    JsonNode* node = memberNode(object, name);
    return node && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : nullptr;
}

const char* stringMember(JsonObject* object, const char* name, const char* fallback = "")
{
    JsonNode* node = memberNode(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        return fallback;
    }
    const char* value = json_node_get_string(node);
    return value ? value : fallback;
}

bool boolMember(JsonObject* object, const char* name, bool fallback = false)
{
    JsonNode* node = memberNode(object, name);
    return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_BOOLEAN
        ? json_node_get_boolean(node)
        : fallback;
}

std::optional<long double> numericMember(JsonObject* object, const char* name)
{
    JsonNode* node = memberNode(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node)) {
        return std::nullopt;
    }

    const GType valueType = json_node_get_value_type(node);
    if (valueType == G_TYPE_INT64) {
        return static_cast<long double>(json_node_get_int(node));
    }
    if (valueType == G_TYPE_DOUBLE) {
        const double value = json_node_get_double(node);
        if (std::isfinite(value)) {
            return static_cast<long double>(value);
        }
    }
    return std::nullopt;
}

std::optional<double> nonnegativeDoubleMember(JsonObject* object, const char* name)
{
    const auto value = numericMember(object, name);
    if (!value.has_value() || *value < 0.0L
        || *value > static_cast<long double>(std::numeric_limits<double>::max())) {
        return std::nullopt;
    }
    return static_cast<double>(*value);
}

std::uint64_t unsignedIntegerMember(JsonObject* object,
                                    const char* name,
                                    std::uint64_t fallback = 0)
{
    const auto value = numericMember(object, name);
    if (!value.has_value() || *value < 0.0L
        || *value > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        return fallback;
    }
    return static_cast<std::uint64_t>(*value);
}

unsigned int unsignedIntMember(JsonObject* object,
                               const char* name,
                               unsigned int fallback = 0)
{
    const auto value = numericMember(object, name);
    if (!value.has_value() || *value < 0.0L
        || *value > static_cast<long double>(std::numeric_limits<unsigned int>::max())) {
        return fallback;
    }
    return static_cast<unsigned int>(*value);
}

std::int64_t signedIntegerMember(JsonObject* object,
                                 const char* name,
                                 std::int64_t fallback = 0)
{
    const auto value = numericMember(object, name);
    if (!value.has_value()
        || *value < static_cast<long double>(std::numeric_limits<std::int64_t>::min())
        || *value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return fallback;
    }
    return static_cast<std::int64_t>(*value);
}

std::optional<std::time_t> timeMember(JsonObject* object, const char* name)
{
    const auto value = numericMember(object, name);
    if (!value.has_value()
        || *value < static_cast<long double>(std::numeric_limits<std::time_t>::min())
        || *value > static_cast<long double>(std::numeric_limits<std::time_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::time_t>(*value);
}

std::vector<std::string> stringArrayMember(JsonObject* object, const char* name)
{
    std::vector<std::string> values;
    JsonArray* array = arrayMember(object, name);
    if (!array) {
        return values;
    }

    const guint length = json_array_get_length(array);
    values.reserve(length);
    for (guint index = 0; index < length; ++index) {
        JsonNode* node = json_array_get_element(array, index);
        if (!node || !JSON_NODE_HOLDS_VALUE(node)
            || json_node_get_value_type(node) != G_TYPE_STRING) {
            continue;
        }
        const char* value = json_node_get_string(node);
        if (value && *value) {
            values.emplace_back(value);
        }
    }
    return values;
}

std::optional<Shortcut> shortcutMember(JsonObject* object)
{
    if (!json_object_has_member(object, "shortcut")) {
        return std::nullopt;
    }

    JsonObject* shortcutObject = objectMember(object, "shortcut");
    if (!shortcutObject) {
        return std::nullopt;
    }

    Shortcut shortcut;
    shortcut.keyval = unsignedIntMember(shortcutObject, "keyval");
    shortcut.modifiers = unsignedIntMember(shortcutObject, "modifiers");
    shortcut.label = stringMember(shortcutObject, "label");
    shortcut.global = boolMember(shortcutObject, "global", true);
    if (shortcut.empty()) {
        return std::nullopt;
    }
    return shortcut;
}

SoundMetadata soundMetadataFromObject(JsonObject* object, const std::string& categoryName)
{
    SoundMetadata metadata;
    metadata.soundId = stringMember(object, "soundId");
    metadata.displayName = stringMember(object, "displayName", stringMember(object, "title"));
    metadata.storageMode = soundStorageModeFromName(stringMember(object, "storageMode", "managed"));
    metadata.externalPath = stringMember(object, "externalPath");
    metadata.originalSourcePath = stringMember(object, "originalSourcePath");
    metadata.sourceFileName = stringMember(object, "sourceFileName");

    if (json_object_has_member(object, "categoryId")) {
        metadata.categoryId = stringMember(object, "categoryId", "uncategorized");
    } else if (!categoryName.empty()) {
        metadata.categoryId = stableCategoryIdForName(categoryName);
    }
    if (metadata.categoryId.empty()) {
        metadata.categoryId = "uncategorized";
    }

    metadata.favorite = boolMember(object, "favorite");
    const auto durationSeconds = nonnegativeDoubleMember(object, "durationSeconds");
    metadata.durationSeconds = durationSeconds.value_or(0.0);
    metadata.durationKnown = durationSeconds.has_value() && boolMember(object, "durationKnown");
    metadata.durationFileSize = unsignedIntegerMember(object, "durationFileSize");
    metadata.durationModifiedSeconds = signedIntegerMember(object, "durationModifiedSeconds");
    metadata.durationSourcePath = stringMember(object, "durationSourcePath");
    metadata.notes = stringMember(object, "notes", stringMember(object, "note"));
    metadata.aliases = stringArrayMember(object, "aliases");
    metadata.shortcut = shortcutMember(object);
    metadata.addedAt = timeMember(object, "addedAt");
    metadata.lastPlayedAt = timeMember(object, "lastPlayedAt");
    return metadata;
}

Category categoryFromObject(JsonObject* object)
{
    Category category;
    category.id = stringMember(object, "id");
    category.name = stringMember(object, "name");
    category.colorHex = stringMember(object, "color", stringMember(object, "defaultColorHex", "#8E8E93"));
    category.iconName = stringMember(object, "icon", stringMember(object, "systemImage", "tag"));
    category.editable = boolMember(object, "editable", boolMember(object, "isUserEditable", true));
    return category;
}

std::string categoryNameForId(const std::vector<Category>& categories, const std::string& id)
{
    const auto it = std::find_if(categories.begin(), categories.end(), [&](const Category& category) {
        return category.id == id;
    });
    return it == categories.end() ? "" : it->name;
}

void addStringArray(JsonBuilder* builder, const char* name, const std::vector<std::string>& values)
{
    json_builder_set_member_name(builder, name);
    json_builder_begin_array(builder);
    for (const auto& value : values) {
        if (!trim(value).empty()) {
            json_builder_add_string_value(builder, trim(value).c_str());
        }
    }
    json_builder_end_array(builder);
}

void addShortcut(JsonBuilder* builder, const std::optional<Shortcut>& shortcut)
{
    if (!shortcut || shortcut->empty()) {
        return;
    }

    json_builder_set_member_name(builder, "shortcut");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "keyval");
    json_builder_add_int_value(builder, shortcut->keyval);
    json_builder_set_member_name(builder, "modifiers");
    json_builder_add_int_value(builder, shortcut->modifiers);
    json_builder_set_member_name(builder, "label");
    json_builder_add_string_value(builder, shortcut->label.c_str());
    json_builder_set_member_name(builder, "global");
    json_builder_add_boolean_value(builder, shortcut->global);
    json_builder_end_object(builder);
}

void addOptionalTime(JsonBuilder* builder, const char* name, const std::optional<std::time_t>& value)
{
    if (!value.has_value()) {
        return;
    }

    json_builder_set_member_name(builder, name);
    json_builder_add_int_value(builder, static_cast<gint64>(*value));
}

void ensureLegacyCategories(LibraryMetadata& metadata, const std::map<std::string, std::string>& categoryNamesById)
{
    std::set<std::string> known;
    for (const auto& category : metadata.categories) {
        known.insert(category.id);
    }

    for (const auto& [id, name] : categoryNamesById) {
        if (id == "uncategorized" || known.count(id) > 0) {
            continue;
        }
        metadata.categories.push_back(Category{id, name, "#3478F6", "tag", true});
        known.insert(id);
    }
}

std::string managedAbsolutePath(const std::filesystem::path& libraryFolder,
                                const std::string& relativePath)
{
    if (libraryFolder.empty() || relativePath.empty()) {
        return {};
    }

    const auto relative = std::filesystem::u8path(relativePath).lexically_normal();
    if (relative.empty() || relative.is_absolute()) {
        return {};
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return {};
        }
    }

    std::error_code error;
    const auto root = std::filesystem::absolute(libraryFolder, error).lexically_normal();
    if (error) {
        return {};
    }
    return (root / relative).lexically_normal().u8string();
}

} // namespace

MetadataStore::MetadataStore(std::filesystem::path metadataFile)
    : metadataFile_(std::move(metadataFile))
{
}

LibraryMetadata MetadataStore::load() const
{
    lastError_.clear();
    loadedVersion_ = 2;
    LibraryMetadata metadata;
    metadata.categories.push_back(uncategorizedCategory());

    if (!std::filesystem::exists(metadataFile_)) {
        return metadata;
    }

    GError* error = nullptr;
    JsonParser* parser = json_parser_new();
    if (!json_parser_load_from_file(parser, metadataFile_.c_str(), &error)) {
        lastError_ = error && error->message ? error->message : "Could not load metadata file.";
        if (error) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return metadata;
    }

    JsonNode* rootNode = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(rootNode)) {
        lastError_ = "Metadata file root is not an object.";
        g_object_unref(parser);
        return metadata;
    }

    JsonObject* root = json_node_get_object(rootNode);
    const std::int64_t storedVersion = signedIntegerMember(root, "version", 1);
    loadedVersion_ = storedVersion >= 0
            && storedVersion <= static_cast<std::int64_t>(std::numeric_limits<int>::max())
        ? static_cast<int>(storedVersion)
        : 1;
    metadata.schemaVersion = 2;

    if (JsonArray* categories = arrayMember(root, "categories")) {
        const guint length = json_array_get_length(categories);
        for (guint index = 0; index < length; ++index) {
            JsonNode* node = json_array_get_element(categories, index);
            if (!node || !JSON_NODE_HOLDS_OBJECT(node)) {
                continue;
            }
            Category category = categoryFromObject(json_node_get_object(node));
            if (!category.id.empty() && !category.name.empty()
                && category.id != uncategorizedCategory().id) {
                metadata.categories.push_back(category);
            }
        }
    }

    std::map<std::string, std::string> categoryNamesById;
    if (JsonObject* sounds = objectMember(root, "sounds")) {
        GList* members = json_object_get_members(sounds);
        for (GList* node = members; node; node = node->next) {
            const char* relativePath = static_cast<const char*>(node->data);
            JsonObject* object = objectMember(sounds, relativePath);
            if (!object || !relativePath || !*relativePath) {
                continue;
            }

            const std::string legacyCategory = stringMember(object, "category");
            SoundMetadata sound = soundMetadataFromObject(object, legacyCategory);
            if (!legacyCategory.empty()) {
                categoryNamesById[sound.categoryId] = legacyCategory;
            }
            metadata.soundsByRelativePath[relativePath] = sound;
        }
        g_list_free(members);
    }

    ensureLegacyCategories(metadata, categoryNamesById);
    g_object_unref(parser);
    return metadata;
}

bool MetadataStore::save(const LibraryMetadata& metadata) const
{
    lastError_.clear();
    std::error_code errorCode;
    std::filesystem::create_directories(metadataFile_.parent_path(), errorCode);
    if (errorCode) {
        lastError_ = "Could not create metadata directory.";
        return false;
    }
    backupLegacyFileIfNeeded();

    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, 2);

    json_builder_set_member_name(builder, "categories");
    json_builder_begin_array(builder);
    for (const auto& category : metadata.categories) {
        if (category.id == "uncategorized") {
            continue;
        }
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "id");
        json_builder_add_string_value(builder, category.id.c_str());
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, category.name.c_str());
        json_builder_set_member_name(builder, "color");
        json_builder_add_string_value(builder, category.colorHex.c_str());
        json_builder_set_member_name(builder, "icon");
        json_builder_add_string_value(builder, category.iconName.c_str());
        json_builder_set_member_name(builder, "editable");
        json_builder_add_boolean_value(builder, category.editable);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);

    json_builder_set_member_name(builder, "sounds");
    json_builder_begin_object(builder);
    for (const auto& [relativePath, sound] : metadata.soundsByRelativePath) {
        json_builder_set_member_name(builder, relativePath.c_str());
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "displayName");
        json_builder_add_string_value(builder, sound.displayName.c_str());
        if (!sound.soundId.empty()) {
            json_builder_set_member_name(builder, "soundId");
            json_builder_add_string_value(builder, sound.soundId.c_str());
        }
        json_builder_set_member_name(builder, "storageMode");
        json_builder_add_string_value(builder, soundStorageModeName(sound.storageMode).c_str());
        if (!sound.externalPath.empty()) {
            json_builder_set_member_name(builder, "externalPath");
            json_builder_add_string_value(builder, sound.externalPath.c_str());
        }
        if (!sound.originalSourcePath.empty()) {
            json_builder_set_member_name(builder, "originalSourcePath");
            json_builder_add_string_value(builder, sound.originalSourcePath.c_str());
        }
        if (!sound.sourceFileName.empty()) {
            json_builder_set_member_name(builder, "sourceFileName");
            json_builder_add_string_value(builder, sound.sourceFileName.c_str());
        }
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, sound.displayName.c_str());
        json_builder_set_member_name(builder, "categoryId");
        json_builder_add_string_value(builder, sound.categoryId.c_str());
        json_builder_set_member_name(builder, "category");
        json_builder_add_string_value(builder, categoryNameForId(metadata.categories, sound.categoryId).c_str());
        json_builder_set_member_name(builder, "favorite");
        json_builder_add_boolean_value(builder, sound.favorite);
        const bool validDuration = std::isfinite(sound.durationSeconds)
            && sound.durationSeconds >= 0.0;
        json_builder_set_member_name(builder, "durationSeconds");
        json_builder_add_double_value(builder, validDuration ? sound.durationSeconds : 0.0);
        json_builder_set_member_name(builder, "durationKnown");
        json_builder_add_boolean_value(builder, validDuration && sound.durationKnown);
        json_builder_set_member_name(builder, "durationFileSize");
        json_builder_add_double_value(builder, static_cast<double>(sound.durationFileSize));
        json_builder_set_member_name(builder, "durationModifiedSeconds");
        json_builder_add_int_value(builder, sound.durationModifiedSeconds);
        if (!sound.durationSourcePath.empty()) {
            json_builder_set_member_name(builder, "durationSourcePath");
            json_builder_add_string_value(builder, sound.durationSourcePath.c_str());
        }
        json_builder_set_member_name(builder, "notes");
        json_builder_add_string_value(builder, sound.notes.c_str());
        addStringArray(builder, "aliases", sound.aliases);
        addShortcut(builder, sound.shortcut);
        addOptionalTime(builder, "addedAt", sound.addedAt);
        addOptionalTime(builder, "lastPlayedAt", sound.lastPlayedAt);
        json_builder_end_object(builder);
    }
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    JsonGenerator* generator = json_generator_new();
    JsonNode* root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);

    gsize length = 0;
    gchar* contents = json_generator_to_data(generator, &length);
    GError* error = nullptr;
    const auto flags = static_cast<GFileSetContentsFlags>(
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE);
    const bool ok = contents && g_file_set_contents_full(
        metadataFile_.c_str(), contents, static_cast<gssize>(length), flags, 0666, &error);
    if (!ok) {
        lastError_ = error && error->message ? error->message : "Could not write metadata file.";
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

std::string MetadataStore::lastError() const
{
    return lastError_;
}

std::filesystem::path MetadataStore::filePath() const
{
    return metadataFile_;
}

std::filesystem::path MetadataStore::metadataPathForLibrary(const std::filesystem::path& libraryFolder)
{
    return libraryFolder / ".cuelet-metadata.json";
}

void MetadataStore::applyMetadata(std::vector<SoundClip>& clips, const LibraryMetadata& metadata)
{
    applyMetadata(clips, metadata, {});
}

void MetadataStore::applyMetadata(std::vector<SoundClip>& clips,
                                  const LibraryMetadata& metadata,
                                  const std::filesystem::path& libraryFolder)
{
    std::set<std::string> seen;
    for (auto& clip : clips) {
        seen.insert(clip.relativePath);
        const auto found = metadata.soundsByRelativePath.find(clip.relativePath);
        if (found == metadata.soundsByRelativePath.end()) {
            continue;
        }

        const auto& sound = found->second;
        if (!sound.soundId.empty()) clip.id = sound.soundId;
        if (!trim(sound.displayName).empty()) {
            clip.displayName = sound.displayName;
        }
        clip.storageMode = sound.storageMode;
        clip.externalPath = sound.externalPath;
        clip.originalSourcePath = sound.originalSourcePath;
        clip.sourceFileName = sound.sourceFileName;
        if (sound.storageMode == SoundStorageMode::Linked && !sound.externalPath.empty()) {
            clip.absolutePath = sound.externalPath;
            clip.filename = sound.sourceFileName.empty()
                ? filenameFromPath(sound.externalPath) : sound.sourceFileName;
        }
        clip.categoryId = sound.categoryId.empty() ? "uncategorized" : sound.categoryId;
        clip.notes = sound.notes;
        clip.aliases = sound.aliases;
        clip.shortcut = sound.shortcut;
        clip.favorite = sound.favorite;
        const bool validDuration = std::isfinite(sound.durationSeconds)
            && sound.durationSeconds >= 0.0;
        clip.durationSeconds = validDuration ? sound.durationSeconds : 0.0;
        clip.durationKnown = validDuration && sound.durationKnown;
        clip.durationFileSize = sound.durationFileSize;
        clip.durationModifiedSeconds = sound.durationModifiedSeconds;
        clip.durationSourcePath = sound.durationSourcePath;
        if (sound.addedAt.has_value()) {
            clip.addedAt = *sound.addedAt;
        }
        clip.lastPlayedAt = sound.lastPlayedAt;
    }

    for (const auto& [relativePath, sound] : metadata.soundsByRelativePath) {
        if (seen.count(relativePath) > 0) {
            continue;
        }

        SoundClip missing;
        missing.id = sound.soundId.empty() ? stableIdForPath(relativePath) : sound.soundId;
        missing.relativePath = relativePath;
        missing.storageMode = sound.storageMode;
        missing.externalPath = sound.externalPath;
        missing.originalSourcePath = sound.originalSourcePath;
        missing.sourceFileName = sound.sourceFileName;
        missing.absolutePath = sound.storageMode == SoundStorageMode::Linked
            ? sound.externalPath
            : managedAbsolutePath(libraryFolder, relativePath);
        missing.filename = sound.sourceFileName.empty()
            ? filenameFromPath(sound.storageMode == SoundStorageMode::Linked ? sound.externalPath : relativePath)
            : sound.sourceFileName;
        missing.displayName = sound.displayName.empty()
            ? displayNameFromFilename(missing.filename)
            : sound.displayName;
        missing.categoryId = sound.categoryId.empty() ? "uncategorized" : sound.categoryId;
        missing.notes = sound.notes;
        missing.aliases = sound.aliases;
        missing.shortcut = sound.shortcut;
        missing.favorite = sound.favorite;
        const bool validDuration = std::isfinite(sound.durationSeconds)
            && sound.durationSeconds >= 0.0;
        missing.durationSeconds = validDuration ? sound.durationSeconds : 0.0;
        missing.durationKnown = validDuration && sound.durationKnown;
        missing.durationFileSize = sound.durationFileSize;
        missing.durationModifiedSeconds = sound.durationModifiedSeconds;
        missing.durationSourcePath = sound.durationSourcePath;
        missing.addedAt = sound.addedAt.value_or(0);
        missing.lastPlayedAt = sound.lastPlayedAt;
        if (!missing.absolutePath.empty()) {
            std::error_code error;
            missing.missing = !std::filesystem::is_regular_file(
                std::filesystem::u8path(missing.absolutePath), error);
        } else {
            missing.missing = true;
        }
        clips.push_back(missing);
    }
}

LibraryMetadata MetadataStore::metadataFromClips(const std::vector<SoundClip>& clips,
                                                 const std::vector<Category>& categories)
{
    LibraryMetadata metadata;
    metadata.categories = categories.empty() ? std::vector<Category>{uncategorizedCategory()} : categories;

    for (const auto& clip : clips) {
        if (clip.relativePath.empty()) {
            continue;
        }

        SoundMetadata sound;
        sound.soundId = clip.id;
        sound.displayName = clip.displayName;
        sound.storageMode = clip.storageMode;
        sound.externalPath = clip.externalPath;
        sound.originalSourcePath = clip.originalSourcePath;
        sound.sourceFileName = clip.sourceFileName;
        sound.categoryId = clip.categoryId.empty() ? "uncategorized" : clip.categoryId;
        sound.notes = clip.notes;
        sound.aliases = clip.aliases;
        sound.shortcut = clip.shortcut;
        sound.favorite = clip.favorite;
        const bool validDuration = std::isfinite(clip.durationSeconds)
            && clip.durationSeconds >= 0.0;
        sound.durationSeconds = validDuration ? clip.durationSeconds : 0.0;
        sound.durationKnown = validDuration && clip.durationKnown;
        sound.durationFileSize = clip.durationFileSize;
        sound.durationModifiedSeconds = clip.durationModifiedSeconds;
        sound.durationSourcePath = clip.durationSourcePath;
        sound.addedAt = clip.addedAt;
        sound.lastPlayedAt = clip.lastPlayedAt;
        metadata.soundsByRelativePath[clip.relativePath] = sound;
    }

    return metadata;
}

void MetadataStore::backupLegacyFileIfNeeded() const
{
    if (loadedVersion_ >= 2 || !std::filesystem::exists(metadataFile_)) {
        return;
    }

    const auto backup = metadataFile_.string() + ".v1.bak";
    if (std::filesystem::exists(backup)) {
        return;
    }

    std::error_code errorCode;
    std::filesystem::copy_file(metadataFile_, backup, std::filesystem::copy_options::none, errorCode);
}

std::vector<Category> mergeCategories(const std::vector<Category>& storedCategories,
                                      const std::vector<SoundClip>& clips)
{
    std::vector<Category> categories;
    std::set<std::string> seen;

    auto addCategory = [&](Category category) {
        if (category.id.empty()) {
            return;
        }
        if (seen.insert(category.id).second) {
            categories.push_back(std::move(category));
        }
    };

    addCategory(uncategorizedCategory());
    for (const auto& category : storedCategories) {
        addCategory(category);
    }

    for (const auto& clip : clips) {
        if (clip.categoryId.empty() || clip.categoryId == "uncategorized" || seen.count(clip.categoryId) > 0) {
            continue;
        }
        addCategory(Category{clip.categoryId, clip.categoryId, "#3478F6", "tag", true});
    }

    return categories;
}

} // namespace cuelet
