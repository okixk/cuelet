#include "storage/MetadataStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

MetadataStore::MetadataStore(QString metadataFilePath)
    : m_filePath(std::move(metadataFilePath))
{
}

QString MetadataStore::lastError() const
{
    return m_lastError;
}

QString MetadataStore::filePath() const
{
    return m_filePath;
}

QString MetadataStore::metadataPathForLibrary(const QString& libraryFolder)
{
    return QDir(QDir::cleanPath(libraryFolder)).filePath(".cuelet-metadata.json");
}

SoundMetadata MetadataStore::metadataFromObject(const QJsonObject& object, const SoundMetadata& fallback)
{
    SoundMetadata metadata = fallback;

    if (object.value("title").isString()) {
        metadata.title = object.value("title").toString();
    }
    if (object.value("category").isString()) {
        metadata.category = object.value("category").toString();
    }
    if (object.value("favorite").isBool()) {
        metadata.favorite = object.value("favorite").toBool();
    }
    if (object.value("icon").isString()) {
        metadata.icon = object.value("icon").toString();
    }
    if (object.value("notes").isString()) {
        metadata.notes = object.value("notes").toString();
    }
    if (object.value("aliases").isArray()) {
        metadata.aliases.clear();
        const QJsonArray aliases = object.value("aliases").toArray();
        for (const QJsonValue& value : aliases) {
            if (value.isString()) {
                metadata.aliases.append(value.toString());
            }
        }
    }

    return metadata;
}

QJsonObject MetadataStore::objectFromMetadata(const SoundMetadata& metadata)
{
    QJsonArray aliases;
    for (const QString& alias : metadata.aliases) {
        if (!alias.trimmed().isEmpty()) {
            aliases.append(alias.trimmed());
        }
    }

    return {
        {"title", metadata.title},
        {"category", metadata.category},
        {"favorite", metadata.favorite},
        {"icon", metadata.icon},
        {"notes", metadata.notes},
        {"aliases", aliases},
    };
}

QHash<QString, SoundMetadata> MetadataStore::load() const
{
    m_lastError.clear();

    QFile file(m_filePath);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QObject::tr("Could not open metadata file: %1").arg(file.errorString());
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_lastError = QObject::tr("Metadata file is invalid JSON: %1").arg(parseError.errorString());
        return {};
    }

    const QJsonObject root = document.object();
    const QJsonValue soundsValue = root.value("sounds");
    if (!soundsValue.isObject()) {
        m_lastError = QObject::tr("Metadata file does not contain a valid sounds object.");
        return {};
    }

    QHash<QString, SoundMetadata> metadata;
    const QJsonObject sounds = soundsValue.toObject();
    for (auto it = sounds.begin(); it != sounds.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        metadata.insert(QDir::fromNativeSeparators(it.key()), metadataFromObject(it.value().toObject()));
    }

    return metadata;
}

bool MetadataStore::save(const QHash<QString, SoundMetadata>& metadata) const
{
    m_lastError.clear();

    const QFileInfo info(m_filePath);
    if (!info.absoluteDir().exists() && !info.absoluteDir().mkpath(".")) {
        m_lastError = QObject::tr("Could not create metadata directory.");
        return false;
    }

    QJsonObject sounds;
    QStringList keys = metadata.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString& key : keys) {
        sounds.insert(QDir::fromNativeSeparators(key), objectFromMetadata(metadata.value(key)));
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("sounds", sounds);

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QObject::tr("Could not write metadata file: %1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        m_lastError = QObject::tr("Could not commit metadata file: %1").arg(file.errorString());
        return false;
    }
    return true;
}

void MetadataStore::sortClips(QVector<SoundClip>& clips)
{
    std::sort(clips.begin(), clips.end(), [](const SoundClip& left, const SoundClip& right) {
        const int titleCompare = QString::compare(left.displayTitle(), right.displayTitle(), Qt::CaseInsensitive);
        if (titleCompare != 0) {
            return titleCompare < 0;
        }
        return QString::compare(left.relativePath, right.relativePath, Qt::CaseInsensitive) < 0;
    });
}

QVector<SoundClip> MetadataStore::mergeWithScannedClips(const QHash<QString, SoundMetadata>& metadata,
                                                        const QVector<SoundClip>& scannedClips)
{
    QVector<SoundClip> merged;
    merged.reserve(scannedClips.size() + metadata.size());

    QSet<QString> seen;
    for (SoundClip clip : scannedClips) {
        const QString key = QDir::fromNativeSeparators(clip.relativePath);
        seen.insert(key);
        if (metadata.contains(key)) {
            const SoundMetadata scannedDefaults = clip.metadata;
            clip.metadata = metadata.value(key);
            if (clip.metadata.title.trimmed().isEmpty()) {
                clip.metadata.title = scannedDefaults.title;
            }
            if (clip.metadata.category.trimmed().isEmpty()) {
                clip.metadata.category = scannedDefaults.category;
            }
        }
        clip.missing = false;
        merged.append(clip);
    }

    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        const QString key = QDir::fromNativeSeparators(it.key());
        if (seen.contains(key)) {
            continue;
        }
        SoundClip missing;
        missing.relativePath = key;
        missing.metadata = it.value();
        missing.missing = true;
        merged.append(missing);
    }

    sortClips(merged);
    return merged;
}

QHash<QString, SoundMetadata> MetadataStore::metadataFromClips(const QVector<SoundClip>& clips)
{
    QHash<QString, SoundMetadata> metadata;
    for (const SoundClip& clip : clips) {
        if (!clip.relativePath.isEmpty()) {
            metadata.insert(QDir::fromNativeSeparators(clip.relativePath), clip.metadata);
        }
    }
    return metadata;
}
