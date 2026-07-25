#include "pch.h"
#include "WindowsMetadataStore.h"
#include "WindowsHotkeyModel.h"
#include "WindowsWorkflowModel.h"
#include "WindowsText.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace winrt;
using namespace Windows::Data::Json;

namespace cuelet::windows {
namespace {

std::string readText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string stringValue(JsonObject const& object, wchar_t const* name, std::string fallback = {})
{
    if (!object.HasKey(name)) {
        return fallback;
    }
    try {
        return hstringToUtf8(object.GetNamedString(name));
    } catch (...) {
        return fallback;
    }
}

bool boolValue(JsonObject const& object, wchar_t const* name, bool fallback = false)
{
    try {
        return object.GetNamedBoolean(name, fallback);
    } catch (...) {
        return fallback;
    }
}

double numberValue(JsonObject const& object, wchar_t const* name, double fallback = 0)
{
    try {
        return object.GetNamedNumber(name, fallback);
    } catch (...) {
        return fallback;
    }
}

JsonObject shortcutToJson(const Shortcut& shortcut)
{
    const auto stored = shortcutToStorage(shortcut);
    JsonObject object;
    object.Insert(L"keyval", JsonValue::CreateNumberValue(stored.virtualKey));
    object.Insert(L"modifiers", JsonValue::CreateNumberValue(stored.modifiers));
    object.Insert(L"label", JsonValue::CreateStringValue(formatShortcut(shortcut)));
    object.Insert(L"global", JsonValue::CreateBooleanValue(stored.global));
    return object;
}

JsonObject metadataToJson(const SoundMetadata& metadata, const std::vector<Category>& categories,
                          JsonObject const& existing = nullptr)
{
    JsonObject object = existing ? existing : JsonObject();
    if (!metadata.soundId.empty()) {
        object.Insert(L"soundId", JsonValue::CreateStringValue(utf8ToHstring(metadata.soundId)));
    } else {
        object.Remove(L"soundId");
    }
    object.Insert(L"displayName", JsonValue::CreateStringValue(utf8ToHstring(metadata.displayName)));
    object.Insert(L"title", JsonValue::CreateStringValue(utf8ToHstring(metadata.displayName)));
    object.Insert(L"storageMode", JsonValue::CreateStringValue(
        utf8ToHstring(soundStorageModeName(metadata.storageMode))));
    if (!metadata.externalPath.empty()) {
        object.Insert(L"externalPath", JsonValue::CreateStringValue(utf8ToHstring(metadata.externalPath)));
    } else {
        object.Remove(L"externalPath");
    }
    if (!metadata.originalSourcePath.empty()) {
        object.Insert(L"originalSourcePath", JsonValue::CreateStringValue(
            utf8ToHstring(metadata.originalSourcePath)));
    } else {
        object.Remove(L"originalSourcePath");
    }
    if (!metadata.sourceFileName.empty()) {
        object.Insert(L"sourceFileName", JsonValue::CreateStringValue(utf8ToHstring(metadata.sourceFileName)));
    } else {
        object.Remove(L"sourceFileName");
    }
    object.Insert(L"categoryId", JsonValue::CreateStringValue(utf8ToHstring(metadata.categoryId)));
    auto categoryName = std::string{"Uncategorized"};
    for (auto const& category : categories) {
        if (category.id == metadata.categoryId) {
            categoryName = category.name;
            break;
        }
    }
    object.Insert(L"category", JsonValue::CreateStringValue(utf8ToHstring(categoryName)));
    object.Insert(L"favorite", JsonValue::CreateBooleanValue(metadata.favorite));
    object.Insert(L"durationSeconds", JsonValue::CreateNumberValue(metadata.durationSeconds));
    object.Insert(L"durationKnown", JsonValue::CreateBooleanValue(metadata.durationKnown));
    object.Insert(L"durationFileSize", JsonValue::CreateNumberValue(
        static_cast<double>(metadata.durationFileSize)));
    object.Insert(L"durationModifiedSeconds", JsonValue::CreateNumberValue(
        static_cast<double>(metadata.durationModifiedSeconds)));
    if (!metadata.durationSourcePath.empty()) {
        object.Insert(L"durationSourcePath", JsonValue::CreateStringValue(
            utf8ToHstring(metadata.durationSourcePath)));
    } else {
        object.Remove(L"durationSourcePath");
    }
    object.Insert(L"notes", JsonValue::CreateStringValue(utf8ToHstring(metadata.notes)));
    JsonArray aliases;
    for (auto const& alias : metadata.aliases) {
        aliases.Append(JsonValue::CreateStringValue(utf8ToHstring(alias)));
    }
    object.Insert(L"aliases", aliases);
    if (metadata.shortcut && !metadata.shortcut->empty()) {
        object.Insert(L"shortcut", shortcutToJson(*metadata.shortcut));
    } else {
        object.Remove(L"shortcut");
    }
    if (metadata.addedAt) {
        object.Insert(L"addedAt", JsonValue::CreateNumberValue(static_cast<double>(*metadata.addedAt)));
    } else {
        object.Remove(L"addedAt");
    }
    if (metadata.lastPlayedAt) {
        object.Insert(L"lastPlayedAt", JsonValue::CreateNumberValue(static_cast<double>(*metadata.lastPlayedAt)));
    } else {
        object.Remove(L"lastPlayedAt");
    }
    return object;
}

SoundMetadata parseSoundMetadata(JsonObject const& object, bool legacy,
                                 std::vector<Category>& categories)
{
    SoundMetadata metadata;
    metadata.soundId = stringValue(object, L"soundId");
    metadata.displayName = stringValue(object, L"displayName", stringValue(object, L"title"));
    metadata.storageMode = soundStorageModeFromName(stringValue(object, L"storageMode", "managed"));
    metadata.externalPath = stringValue(object, L"externalPath");
    metadata.originalSourcePath = stringValue(object, L"originalSourcePath");
    metadata.sourceFileName = stringValue(object, L"sourceFileName");
    metadata.categoryId = stringValue(object, L"categoryId");
    if (metadata.categoryId.empty()) {
        auto categoryName = stringValue(object, L"category", "Uncategorized");
        if (categoryName == "Uncategorized" || categoryName.empty()) {
            metadata.categoryId = "uncategorized";
        } else {
            auto found = std::find_if(categories.begin(), categories.end(), [&](auto const& category) {
                return category.name == categoryName;
            });
            if (found == categories.end()) {
                Category category{stableCategoryIdForName(categoryName), categoryName, "#3478F6", "tag", true};
                metadata.categoryId = category.id;
                categories.push_back(std::move(category));
            } else {
                metadata.categoryId = found->id;
            }
        }
    }
    metadata.favorite = boolValue(object, L"favorite");
    metadata.durationSeconds = numberValue(object, L"durationSeconds");
    metadata.durationKnown = boolValue(object, L"durationKnown");
    const auto durationFileSize = numberValue(object, L"durationFileSize");
    metadata.durationFileSize = static_cast<std::uint64_t>(
        durationFileSize > 0.0 ? durationFileSize : 0.0);
    metadata.durationModifiedSeconds = static_cast<std::int64_t>(
        numberValue(object, L"durationModifiedSeconds"));
    metadata.durationSourcePath = stringValue(object, L"durationSourcePath");
    metadata.notes = stringValue(object, L"notes", stringValue(object, L"note"));
    if (object.HasKey(L"aliases")) {
        try {
            for (auto const& value : object.GetNamedArray(L"aliases")) {
                if (value.ValueType() == JsonValueType::String) {
                    metadata.aliases.push_back(hstringToUtf8(value.GetString()));
                }
            }
        } catch (...) {}
    }
    if (object.HasKey(L"shortcut")) {
        try {
            auto shortcutObject = object.GetNamedObject(L"shortcut");
            const ShortcutStorageRecord stored{
                static_cast<unsigned int>(numberValue(shortcutObject, L"keyval")),
                static_cast<unsigned int>(numberValue(shortcutObject, L"modifiers")),
                boolValue(shortcutObject, L"global", false),
            };
            auto shortcut = shortcutFromStorage(stored);
            shortcut.label = wideToUtf8(formatShortcut(shortcut));
            if (!shortcut.empty()) {
                metadata.shortcut = shortcut;
            }
        } catch (...) {
            if (legacy) {
                auto label = stringValue(object, L"shortcut");
                if (!label.empty()) {
                    metadata.aliases.push_back(label);
                }
            }
        }
    }
    const auto addedAt = numberValue(object, L"addedAt");
    if (addedAt > 0) metadata.addedAt = static_cast<std::time_t>(addedAt);
    const auto lastPlayedAt = numberValue(object, L"lastPlayedAt");
    if (lastPlayedAt > 0) metadata.lastPlayedAt = static_cast<std::time_t>(lastPlayedAt);
    return metadata;
}

} // namespace

MetadataLoadResult WindowsMetadataStore::load(const std::filesystem::path& libraryFolder) const
{
    MetadataLoadResult result;
    const auto path = libraryFolder / fileName;
    if (!std::filesystem::exists(path)) {
        return result;
    }
    addHiddenFileAttribute(path);

    try {
        const auto text = readText(path);
        if (text.empty()) {
            result.warning = "The metadata file is empty; defaults were used.";
            return result;
        }
        auto root = JsonObject::Parse(utf8ToHstring(text));
        const auto version = static_cast<int>(numberValue(root, L"version", 1));
        result.metadata.schemaVersion = 2;
        result.migratedFromV1 = version < 2;

        if (root.HasKey(L"categories")) {
            for (auto const& value : root.GetNamedArray(L"categories")) {
                if (value.ValueType() != JsonValueType::Object) continue;
                auto object = value.GetObject();
                Category category;
                category.name = stringValue(object, L"name");
                category.id = stringValue(object, L"id", stableCategoryIdForName(category.name));
                category.colorHex = stringValue(object, L"color", "#3478F6");
                category.iconName = stringValue(object, L"icon", "tag");
                category.editable = boolValue(object, L"editable", true);
                if (!category.name.empty() && category.id != "uncategorized") {
                    result.metadata.categories.push_back(std::move(category));
                }
            }
        }

        if (root.HasKey(L"sounds")) {
            auto sounds = root.GetNamedObject(L"sounds");
            for (auto const& pair : sounds) {
                if (pair.Value().ValueType() != JsonValueType::Object) continue;
                result.metadata.soundsByRelativePath[hstringToUtf8(pair.Key())] =
                    parseSoundMetadata(pair.Value().GetObject(), version < 2, result.metadata.categories);
            }
        }
    } catch (hresult_error const& error) {
        result.warning = "Metadata could not be read: " + hstringToUtf8(error.message());
    } catch (std::exception const& error) {
        result.warning = std::string{"Metadata could not be read: "} + error.what();
    }
    return result;
}

bool WindowsMetadataStore::save(const std::filesystem::path& libraryFolder,
                                const LibraryMetadata& metadata,
                                std::string* error) const
{
    try {
        const auto path = libraryFolder / fileName;
        JsonObject previousRoot{nullptr};
        if (std::filesystem::exists(path)) {
            try {
                previousRoot = JsonObject::Parse(utf8ToHstring(readText(path)));
                if (static_cast<int>(numberValue(previousRoot, L"version", 1)) < 2) {
                    auto backup = path;
                    backup += L".v1.bak";
                    if (!std::filesystem::exists(backup)) {
                        std::filesystem::copy_file(path, backup);
                    }
                }
            } catch (...) {}
        }

        JsonObject root = previousRoot ? previousRoot : JsonObject();
        root.Insert(L"version", JsonValue::CreateNumberValue(2));
        JsonArray categories;
        for (auto const& category : metadata.categories) {
            if (category.id == "uncategorized") continue;
            JsonObject object;
            object.Insert(L"id", JsonValue::CreateStringValue(utf8ToHstring(category.id)));
            object.Insert(L"name", JsonValue::CreateStringValue(utf8ToHstring(category.name)));
            object.Insert(L"color", JsonValue::CreateStringValue(utf8ToHstring(category.colorHex)));
            object.Insert(L"icon", JsonValue::CreateStringValue(utf8ToHstring(category.iconName)));
            object.Insert(L"editable", JsonValue::CreateBooleanValue(category.editable));
            categories.Append(object);
        }
        root.Insert(L"categories", categories);
        JsonObject sounds;
        JsonObject previousSounds{nullptr};
        if (previousRoot && previousRoot.HasKey(L"sounds")) {
            try { previousSounds = previousRoot.GetNamedObject(L"sounds"); } catch (...) {}
        }
        for (auto const& [relativePath, sound] : metadata.soundsByRelativePath) {
            JsonObject previousSound{nullptr};
            const auto key = utf8ToHstring(relativePath);
            if (previousSounds && previousSounds.HasKey(key)) {
                try { previousSound = previousSounds.GetNamedObject(key); } catch (...) {}
            }
            sounds.Insert(key, metadataToJson(sound, metadata.categories, previousSound));
        }
        root.Insert(L"sounds", sounds);

        auto temporary = path;
        temporary += L".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Unable to open the temporary metadata file.");
        const auto serialized = hstringToUtf8(root.Stringify());
        stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        stream.close();
        if (!stream) throw std::runtime_error("Unable to write the metadata file.");
        if (!::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto code = ::GetLastError();
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            throw std::system_error(static_cast<int>(code), std::system_category(), "Unable to replace the metadata file");
        }
        // Atomic replacement creates a new directory entry, so the native hidden
        // attribute must be restored on the final path after every successful save.
        addHiddenFileAttribute(path);
        return true;
    } catch (std::exception const& exception) {
        if (error) *error = exception.what();
        return false;
    } catch (hresult_error const& exception) {
        if (error) *error = hstringToUtf8(exception.message());
        return false;
    }
}

} // namespace cuelet::windows
