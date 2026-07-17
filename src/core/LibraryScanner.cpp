#include "core/LibraryScanner.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>

#include <algorithm>

QSet<QString> LibraryScanner::extensionSet()
{
    return {"mp3", "wav", "ogg", "flac", "m4a"};
}

QStringList LibraryScanner::supportedExtensions() const
{
    QStringList extensions = extensionSet().values();
    extensions.sort(Qt::CaseInsensitive);
    return extensions;
}

bool LibraryScanner::isSupportedAudioFile(const QString& filePath)
{
    const QFileInfo info(filePath);
    return info.isFile() && extensionSet().contains(info.suffix().toLower());
}

QString LibraryScanner::normalizeRelativePath(const QString& rootFolder, const QString& filePath)
{
    const QDir root(QDir::cleanPath(rootFolder));
    return QDir::fromNativeSeparators(root.relativeFilePath(QDir::cleanPath(filePath)));
}

QString LibraryScanner::defaultCategoryForRelativePath(const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators(relativePath);
    const int slash = normalized.indexOf('/');
    if (slash <= 0) {
        return {};
    }
    return normalized.left(slash);
}

ScanResult LibraryScanner::scan(const QString& libraryFolder) const
{
    ScanResult result;

    const QFileInfo rootInfo(QDir::cleanPath(libraryFolder));
    if (libraryFolder.trimmed().isEmpty() || !rootInfo.exists() || !rootInfo.isDir()) {
        result.warning = QObject::tr("The selected library folder does not exist or cannot be opened.");
        return result;
    }

    QDirIterator iterator(rootInfo.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        const QFileInfo info(absolutePath);
        const QString relativePath = normalizeRelativePath(rootInfo.absoluteFilePath(), absolutePath);

        if (!isSupportedAudioFile(absolutePath)) {
            result.unsupportedFiles.append(relativePath);
            continue;
        }

        SoundClip clip;
        clip.filePath = QDir::cleanPath(info.absoluteFilePath());
        clip.relativePath = relativePath;
        clip.metadata.title = info.completeBaseName();
        clip.metadata.category = defaultCategoryForRelativePath(relativePath);
        result.clips.append(clip);
    }

    auto byTitle = [](const SoundClip& left, const SoundClip& right) {
        const int titleCompare = QString::compare(left.displayTitle(), right.displayTitle(), Qt::CaseInsensitive);
        if (titleCompare != 0) {
            return titleCompare < 0;
        }
        return QString::compare(left.relativePath, right.relativePath, Qt::CaseInsensitive) < 0;
    };
    std::sort(result.clips.begin(), result.clips.end(), byTitle);
    std::sort(result.unsupportedFiles.begin(), result.unsupportedFiles.end(), [](const QString& left, const QString& right) {
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });

    return result;
}
