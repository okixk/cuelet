#pragma once

#include <QString>
#include <QStringList>

struct SoundMetadata {
    QString title;
    QString category;
    bool favorite = false;
    QString icon;
    QString notes;
    QStringList aliases;
};

struct SoundClip {
    QString filePath;
    QString relativePath;
    SoundMetadata metadata;
    bool missing = false;

    QString displayTitle() const
    {
        if (!metadata.title.trimmed().isEmpty()) {
            return metadata.title.trimmed();
        }
        const int slash = relativePath.lastIndexOf('/');
        const QString fileName = slash >= 0 ? relativePath.mid(slash + 1) : relativePath;
        const int dot = fileName.lastIndexOf('.');
        return dot > 0 ? fileName.left(dot) : fileName;
    }
};
