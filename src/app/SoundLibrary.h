#pragma once

#include "core/LibraryScanner.h"
#include "core/SoundClip.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

class SoundLibrary {
public:
    bool open(const QString& folder);
    bool rescan();
    bool saveMetadata();
    bool updateMetadata(const QString& relativePath, const SoundMetadata& metadata);
    bool mergeMetadata(const QHash<QString, SoundMetadata>& metadata);
    bool importFiles(const QStringList& sourceFiles);

    bool hasLibrary() const;
    QString folder() const;
    QString metadataFilePath() const;
    QString lastError() const;
    QString lastWarning() const;
    QStringList unsupportedFiles() const;

    QVector<SoundClip> clips() const;
    QHash<QString, SoundMetadata> metadata() const;
    QVector<SoundClip> filteredClips(const QString& query, bool favoritesOnly, const QString& category) const;
    QStringList categories() const;

private:
    QString destinationPathForImport(const QString& sourceFile) const;

    QString m_folder;
    QVector<SoundClip> m_clips;
    QStringList m_unsupportedFiles;
    QString m_lastError;
    QString m_lastWarning;
};
