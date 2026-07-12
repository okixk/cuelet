#pragma once

#include "core/SoundClip.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

class MetadataStore {
public:
    explicit MetadataStore(QString metadataFilePath);

    QHash<QString, SoundMetadata> load() const;
    bool save(const QHash<QString, SoundMetadata>& metadata) const;

    QString lastError() const;
    QString filePath() const;

    static QString metadataPathForLibrary(const QString& libraryFolder);
    static QVector<SoundClip> mergeWithScannedClips(const QHash<QString, SoundMetadata>& metadata,
                                                    const QVector<SoundClip>& scannedClips);
    static QHash<QString, SoundMetadata> metadataFromClips(const QVector<SoundClip>& clips);

private:
    static SoundMetadata metadataFromObject(const QJsonObject& object, const SoundMetadata& fallback = {});
    static QJsonObject objectFromMetadata(const SoundMetadata& metadata);
    static void sortClips(QVector<SoundClip>& clips);

    QString m_filePath;
    mutable QString m_lastError;
};
