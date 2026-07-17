#pragma once

#include "core/SoundClip.h"

#include <QSet>
#include <QString>
#include <QVector>

struct ScanResult {
    QVector<SoundClip> clips;
    QVector<QString> unsupportedFiles;
    QString warning;
};

class LibraryScanner {
public:
    ScanResult scan(const QString& libraryFolder) const;

    QStringList supportedExtensions() const;
    static bool isSupportedAudioFile(const QString& filePath);
    static QString normalizeRelativePath(const QString& rootFolder, const QString& filePath);

private:
    static QSet<QString> extensionSet();
    static QString defaultCategoryForRelativePath(const QString& relativePath);
};
