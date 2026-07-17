#include "cuelet/MetadataStore.h"

#include <json-glib/json-glib.h>

#include <algorithm>
#include <fstream>
#include <set>

namespace cuelet {

namespace {

const char* stringMember(JsonObject* object, const char* name, const char* fallback = "")
{
    return json_object_has_member(object, name) && json_object_get_string_member(object, name)
        ? json_object_get_string_member(object, name)
        : fallback;
}

bool boolMember(JsonObject* object, const char* name, bool fallback = false)
{
    return json_object_has_member(object, name) ? json_object_get_boolean_member(object, name) : fallback;
}

double doubleMember(JsonObject* object, const char* name, double fallback = 0.0)
{
    return json_object_has_member(object, name) ? json_object_get_double_member(object, name) : fallback;
}

std::optional<std::time_t> timeMember(JsonObject* object, const char* name)
{
    if (!json_object_has_member(object, name)) {
        return std::nullopt;
    }

    JsonNode* node = json_object_get_member(object, name);
    if (JSON_NODE_HOLDS_VALUE(node)) {
        if (json_node_get_value_type(node) == G_TYPE_INT64) {
            return static_cast<std::time_t>(json_object_get_int_member(object, name));
        }
        if (json_node_get_value_type(node) == G_TYPE_DOUBLE) {
            return static_cast<std::time_t>(json_object_get_double_member(object, name));
        }
    }

    return std::nullopt;
}

std::vector<std::string> stringArrayMember(JsonObject* object, const char* name)
{
    std::vector<std::string> values;
    if (!json_object_has_member(object, name)) {
        return values;
    }

    JsonArray* array = json_object_get_array_member(object, name);
    if (!array) {
        return values;
    }

    const guint length = json_array_get_length(array);
    values.reserve(length);
    for (guint index = 0; index < length; ++index) {
        const char* value = json_array_get_string_element(array, index);
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

    JsonObject* shortcutObject = json_object_get_object_member(object, "shortcut");
    if (!shortcutObject) {
        return std::nullopt;
    }

    Shortcut shortcut;
    shortcut.keyval = static_cast<unsigned int>(doubleMember(shortcutObject, "keyval"));
    shortcut.modifiers = static_cast<unsigned int>(doubleMember(shortcutObject, "modifiers"));
    shortcut.label = stringMember(shortcutObject, "label");
    if (shortcut.empty()) {
        return std::nullopt;
    }
    return shortcut;
}

SoundMetadata soundMetadataFromObject(JsonObject* object, const std::string& categoryName)
{
    SoundMetadata metadata;
    metadata.displayName = stringMember(object, "displayName", stringMember(object, "title"));

    if (json_object_has_member(object, "categoryId")) {
        metadata.categoryId = stringMember(object, "categoryId", "uncategorized");
    } else if (!categoryName.empty()) {
        metadata.categoryId = stableCategoryIdForName(categoryName);
    }
    if (metadata.categoryId.empty()) {
        metadata.categoryId = "uncategorized";
    }

    metadata.favorite = boolMember(object, "favorite");
    metadata.notes = stringMember(object, "notes");
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
    loadedVersion_ = json_object_has_member(root, "version") ? json_object_get_int_member(root, "version") : 1;
    metadata.schemaVersion = 2;

    if (json_object_has_member(root, "categories")) {
        JsonArray* categories = json_object_get_array_member(root, "categories");
        if (categories) {
            const guint length = json_array_get_length(categories);
            for (guint index = 0; index < length; ++index) {
                JsonObject* object = json_array_get_object_element(categories, index);
                if (!object) {
                    continue;
                }
                Category category = categoryFromObject(object);
                if (!category.id.empty() && !category.name.empty()
                    && category.id != uncategorizedCategory().id) {
                    metadata.categories.push_back(category);
                }
            }
        }
    }

    std::map<std::string, std::string> categoryNamesById;
    if (json_object_has_member(root, "sounds")) {
        JsonObject* sounds = json_object_get_object_member(root, "sounds");
        if (sounds) {
            GList* members = json_object_get_members(sounds);
            for (GList* node = members; node; node = node->next) {
                const char* relativePath = static_cast<const char*>(node->data);
                JsonObject* object = json_object_get_object_member(sounds, relativePath);
                if (!object) {
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
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, sound.displayName.c_str());
        json_builder_set_member_name(builder, "categoryId");
        json_builder_add_string_value(builder, sound.categoryId.c_str());
        json_builder_set_member_name(builder, "category");
        json_builder_add_string_value(builder, categoryNameForId(metadata.categories, sound.categoryId).c_str());
        json_builder_set_member_name(builder, "favorite");
        json_builder_add_boolean_value(builder, sound.favorite);
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

    GError* error = nullptr;
    const bool ok = json_generator_to_file(generator, metadataFile_.c_str(), &error);
    if (!ok) {
        lastError_ = error && error->message ? error->message : "Could not write metadata file.";
        if (error) {
            g_error_free(error);
        }
    }

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
    std::set<std::string> seen;
    for (auto& clip : clips) {
        seen.insert(clip.relativePath);
        const auto found = metadata.soundsByRelativePath.find(clip.relativePath);
        if (found == metadata.soundsByRelativePath.end()) {
            continue;
        }

        const auto& sound = found->second;
        if (!trim(sound.displayName).empty()) {
            clip.displayName = sound.displayName;
        }
        clip.categoryId = sound.categoryId.empty() ? "uncategorized" : sound.categoryId;
        clip.notes = sound.notes;
        clip.aliases = sound.aliases;
        clip.shortcut = sound.shortcut;
        clip.favorite = sound.favorite;
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
        missing.id = stableIdForPath(relativePath);
        missing.relativePath = relativePath;
        missing.filename = filenameFromPath(relativePath);
        missing.displayName = sound.displayName.empty()
            ? displayNameFromFilename(missing.filename)
            : sound.displayName;
        missing.categoryId = sound.categoryId.empty() ? "uncategorized" : sound.categoryId;
        missing.notes = sound.notes;
        missing.aliases = sound.aliases;
        missing.shortcut = sound.shortcut;
        missing.favorite = sound.favorite;
        missing.addedAt = sound.addedAt.value_or(0);
        missing.lastPlayedAt = sound.lastPlayedAt;
        missing.missing = true;
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
        sound.displayName = clip.displayName;
        sound.categoryId = clip.categoryId.empty() ? "uncategorized" : clip.categoryId;
        sound.notes = clip.notes;
        sound.aliases = clip.aliases;
        sound.shortcut = clip.shortcut;
        sound.favorite = clip.favorite;
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
