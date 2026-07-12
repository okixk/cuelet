#include "storage/LegacySettingsImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

namespace {

constexpr int kMinimumSidebarWidth = 148;
constexpr int kMaximumSidebarWidth = 360;

QString cleanRelativePath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    while (path.startsWith("./")) {
        path.remove(0, 2);
    }
    return QDir::cleanPath(path);
}

std::optional<bool> optionalBool(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return !qFuzzyIsNull(value.toDouble());
    }
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == "true" || text == "1" || text == "yes" || text == "on") {
            return true;
        }
        if (text == "false" || text == "0" || text == "no" || text == "off") {
            return false;
        }
    }
    return std::nullopt;
}

std::optional<int> optionalInt(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return qBound(kMinimumSidebarWidth, value.toInt(), kMaximumSidebarWidth);
    }
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        if (ok) {
            return qBound(kMinimumSidebarWidth, parsed, kMaximumSidebarWidth);
        }
    }
    return std::nullopt;
}

QString stringValue(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (value.isString()) {
        return value.toString().trimmed();
    }
    return {};
}

void appendUnique(QStringList& list, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !list.contains(trimmed, Qt::CaseInsensitive)) {
        list.append(trimmed);
    }
}

QString metadataPathKey(const QJsonObject& object)
{
    for (const QString& key : {"path", "relative_path", "file", "filename", "name"}) {
        const QString value = stringValue(object, key);
        if (!value.isEmpty()) {
            return cleanRelativePath(value);
        }
    }
    return {};
}

SoundMetadata metadataFromLegacyObject(const QJsonObject& object)
{
    SoundMetadata metadata;
    metadata.title = stringValue(object, "title");
    metadata.category = stringValue(object, "category");
    metadata.notes = stringValue(object, "notes");
    if (metadata.notes.isEmpty()) {
        metadata.notes = stringValue(object, "note");
    }
    metadata.icon = stringValue(object, "icon");
    if (metadata.icon.isEmpty()) {
        metadata.icon = stringValue(object, "emoji");
    }
    if (const auto favorite = optionalBool(object, "favorite"); favorite.has_value()) {
        metadata.favorite = favorite.value();
    }

    if (object.value("aliases").isArray()) {
        for (const QJsonValue& alias : object.value("aliases").toArray()) {
            if (alias.isString()) {
                appendUnique(metadata.aliases, alias.toString());
            }
        }
    }

    const QString shortcut = stringValue(object, "shortcut");
    if (!shortcut.isEmpty()) {
        appendUnique(metadata.aliases, QStringLiteral("shortcut:%1").arg(shortcut));
    }
    const QString link = stringValue(object, "link");
    if (!link.isEmpty()) {
        appendUnique(metadata.aliases, QStringLiteral("link:%1").arg(link));
    }

    return metadata;
}

void readMetadataContainer(const QJsonValue& value, QHash<QString, SoundMetadata>& metadata)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.value().isObject()) {
                metadata.insert(cleanRelativePath(it.key()), metadataFromLegacyObject(it.value().toObject()));
            }
        }
        return;
    }

    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (!item.isObject()) {
                continue;
            }
            const QJsonObject object = item.toObject();
            const QString path = metadataPathKey(object);
            if (!path.isEmpty()) {
                metadata.insert(path, metadataFromLegacyObject(object));
            }
        }
    }
}

QJsonObject objectFromIniFile(const QString& filePath)
{
    QSettings settings(filePath, QSettings::IniFormat);
    QJsonObject object;
    for (const QString& key : settings.allKeys()) {
        object.insert(key.section('/', -1), QJsonValue::fromVariant(settings.value(key)));
    }
    return object;
}

QStringList candidateConfigRoots()
{
    QStringList roots;
    for (QStandardPaths::StandardLocation location : {QStandardPaths::AppConfigLocation, QStandardPaths::ConfigLocation}) {
        for (const QString& path : QStandardPaths::standardLocations(location)) {
            appendUnique(roots, path);
            appendUnique(roots, QDir(path).filePath("Soundboard"));
            appendUnique(roots, QDir(path).filePath("soundboard"));
            appendUnique(roots, QDir(path).filePath("Cuelet"));
        }
    }
    return roots;
}

} // namespace

QStringList LegacySettingsImporter::likelyConfigFiles()
{
    const QStringList filenames = {"soundboard.json", "settings.json", "config.json", "soundboard.conf"};
    QStringList candidates;
    for (const QString& root : candidateConfigRoots()) {
        const QDir dir(root);
        for (const QString& filename : filenames) {
            const QString path = dir.filePath(filename);
            if (QFileInfo::exists(path)) {
                appendUnique(candidates, path);
            }
        }
    }
    return candidates;
}

LegacySettingsImport LegacySettingsImporter::readFile(const QString& filePath)
{
    LegacySettingsImport result;
    result.sourcePath = filePath;

    QFile file(filePath);
    if (!file.exists()) {
        result.error = QObject::tr("Legacy config file does not exist.");
        return result;
    }

    if (filePath.endsWith(".conf", Qt::CaseInsensitive)) {
        return readObject(objectFromIniFile(filePath), filePath);
    }

    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QObject::tr("Could not open legacy config: %1").arg(file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QObject::tr("Legacy config is not a JSON object: %1").arg(parseError.errorString());
        return result;
    }

    return readObject(document.object(), filePath);
}

LegacySettingsImport LegacySettingsImporter::readObject(const QJsonObject& object, const QString& sourcePath)
{
    LegacySettingsImport result;
    result.valid = true;
    result.sourcePath = sourcePath;
    result.libraryFolder = stringValue(object, "library_dir");
    result.showExtensions = optionalBool(object, "show_extensions");
    result.useLoudness = optionalBool(object, "use_loudness");
    result.sidebarWidth = optionalInt(object, "sidebar_width");
    result.outputDeviceName = stringValue(object, "output_device");
    result.virtualMicEnabled = optionalBool(object, "virtual_mic_enabled");
    result.micLoopbackEnabled = optionalBool(object, "mic_loopback_enabled");
    result.virtualMicOutputDevice = stringValue(object, "virtual_mic_output_device");
    result.virtualMicInputDevice = stringValue(object, "virtual_mic_input_device");

    if (object.value("favorite_paths").isArray()) {
        for (const QJsonValue& path : object.value("favorite_paths").toArray()) {
            if (path.isString()) {
                appendUnique(result.favoritePaths, path.toString());
            }
        }
    }

    readMetadataContainer(object.value("metadata"), result.metadataByPath);
    readMetadataContainer(object.value("sidecars"), result.metadataByPath);
    readMetadataContainer(object.value("sounds"), result.metadataByPath);
    readMetadataContainer(object.value("clips"), result.metadataByPath);

    return result;
}

QStringList LegacySettingsImporter::favoritePathsForLibrary(const LegacySettingsImport& legacy, const QString& libraryFolder)
{
    const QDir library(QDir::cleanPath(libraryFolder.isEmpty() ? legacy.libraryFolder : libraryFolder));
    QStringList converted;
    for (const QString& favorite : legacy.favoritePaths) {
        const QString normalized = QDir::fromNativeSeparators(favorite.trimmed());
        if (normalized.isEmpty()) {
            continue;
        }

        const QFileInfo info(normalized);
        if (info.isAbsolute()) {
            const QString relative = library.relativeFilePath(info.absoluteFilePath());
            if (!relative.startsWith("..") && !QDir::isAbsolutePath(relative)) {
                appendUnique(converted, cleanRelativePath(relative));
            } else {
                legacy.notes.append(QObject::tr("Skipped favorite outside selected library: %1").arg(normalized));
            }
            continue;
        }

        appendUnique(converted, cleanRelativePath(normalized));
    }
    return converted;
}

QHash<QString, SoundMetadata> LegacySettingsImporter::mergeFavoritePaths(const QHash<QString, SoundMetadata>& current,
                                                                         const QStringList& favoriteRelativePaths)
{
    QHash<QString, SoundMetadata> merged = current;
    for (const QString& path : favoriteRelativePaths) {
        const QString key = cleanRelativePath(path);
        if (key.isEmpty() || key == ".") {
            continue;
        }
        SoundMetadata metadata = merged.value(key);
        metadata.favorite = true;
        merged.insert(key, metadata);
    }
    return merged;
}

QHash<QString, SoundMetadata> LegacySettingsImporter::mergeMetadata(const QHash<QString, SoundMetadata>& current,
                                                                    const QHash<QString, SoundMetadata>& legacy)
{
    QHash<QString, SoundMetadata> merged = current;
    for (auto it = legacy.begin(); it != legacy.end(); ++it) {
        const QString key = cleanRelativePath(it.key());
        const SoundMetadata oldValue = it.value();
        SoundMetadata value = merged.value(key);

        if (value.title.trimmed().isEmpty()) {
            value.title = oldValue.title;
        }
        if (value.category.trimmed().isEmpty()) {
            value.category = oldValue.category;
        }
        if (value.notes.trimmed().isEmpty()) {
            value.notes = oldValue.notes;
        }
        if (value.icon.trimmed().isEmpty()) {
            value.icon = oldValue.icon;
        }
        value.favorite = value.favorite || oldValue.favorite;
        for (const QString& alias : oldValue.aliases) {
            appendUnique(value.aliases, alias);
        }

        merged.insert(key, value);
    }
    return merged;
}
