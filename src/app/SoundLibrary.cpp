#include "app/SoundLibrary.h"

#include "core/SoundFilter.h"
#include "storage/MetadataStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>

bool SoundLibrary::open(const QString& folder)
{
    m_lastError.clear();
    m_lastWarning.clear();
    m_unsupportedFiles.clear();

    LibraryScanner scanner;
    const ScanResult scan = scanner.scan(folder);
    if (!scan.warning.isEmpty()) {
        m_folder.clear();
        m_clips.clear();
        m_lastError = scan.warning;
        return false;
    }

    m_folder = QDir::cleanPath(folder);
    m_unsupportedFiles = QStringList(scan.unsupportedFiles.cbegin(), scan.unsupportedFiles.cend());

    MetadataStore store(MetadataStore::metadataPathForLibrary(m_folder));
    const QHash<QString, SoundMetadata> metadata = store.load();
    if (!store.lastError().isEmpty()) {
        m_lastWarning = store.lastError();
    }

    m_clips = MetadataStore::mergeWithScannedClips(metadata, scan.clips);
    return true;
}

bool SoundLibrary::rescan()
{
    if (m_folder.isEmpty()) {
        m_lastError = QObject::tr("No library folder is selected.");
        return false;
    }
    return open(m_folder);
}

bool SoundLibrary::saveMetadata()
{
    if (m_folder.isEmpty()) {
        m_lastError = QObject::tr("No library folder is selected.");
        return false;
    }

    MetadataStore store(metadataFilePath());
    if (!store.save(MetadataStore::metadataFromClips(m_clips))) {
        m_lastError = store.lastError();
        return false;
    }

    m_lastError.clear();
    return true;
}

bool SoundLibrary::updateMetadata(const QString& relativePath, const SoundMetadata& metadata)
{
    for (SoundClip& clip : m_clips) {
        if (clip.relativePath == relativePath) {
            clip.metadata = metadata;
            return saveMetadata();
        }
    }

    m_lastError = QObject::tr("The selected sound no longer exists in the library.");
    return false;
}

bool SoundLibrary::mergeMetadata(const QHash<QString, SoundMetadata>& metadata)
{
    if (m_folder.isEmpty()) {
        m_lastError = QObject::tr("No library folder is selected.");
        return false;
    }

    QSet<QString> seen;
    for (SoundClip& clip : m_clips) {
        const QString key = QDir::fromNativeSeparators(clip.relativePath);
        seen.insert(key);
        if (metadata.contains(key)) {
            clip.metadata = metadata.value(key);
        }
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
        m_clips.append(missing);
    }

    return saveMetadata();
}

QString SoundLibrary::destinationPathForImport(const QString& sourceFile) const
{
    const QFileInfo sourceInfo(sourceFile);
    const QDir libraryDir(m_folder);
    const QString baseName = sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix();

    QString candidate = libraryDir.filePath(sourceInfo.fileName());
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = libraryDir.filePath(QString("%1 (%2).%3").arg(baseName).arg(counter).arg(suffix));
        ++counter;
    }
    return candidate;
}

bool SoundLibrary::importFiles(const QStringList& sourceFiles)
{
    m_lastError.clear();
    m_lastWarning.clear();

    if (m_folder.isEmpty()) {
        m_lastError = QObject::tr("Choose a library before importing sounds.");
        return false;
    }

    int copied = 0;
    QStringList skipped;
    for (const QString& source : sourceFiles) {
        const QFileInfo sourceInfo(source);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            skipped.append(source);
            continue;
        }
        if (!LibraryScanner::isSupportedAudioFile(source)) {
            skipped.append(sourceInfo.fileName());
            continue;
        }

        const QString destination = destinationPathForImport(source);
        if (!QFile::copy(source, destination)) {
            m_lastError = QObject::tr("Could not copy %1 into the library.").arg(sourceInfo.fileName());
            return false;
        }
        ++copied;
    }

    if (!skipped.isEmpty()) {
        m_lastWarning = QObject::tr("Skipped unsupported or unavailable files: %1").arg(skipped.join(", "));
    }
    if (copied == 0) {
        if (m_lastWarning.isEmpty()) {
            m_lastWarning = QObject::tr("No supported audio files were imported.");
        }
        return false;
    }

    return rescan();
}

bool SoundLibrary::hasLibrary() const
{
    return !m_folder.isEmpty();
}

QString SoundLibrary::folder() const
{
    return m_folder;
}

QString SoundLibrary::metadataFilePath() const
{
    return MetadataStore::metadataPathForLibrary(m_folder);
}

QString SoundLibrary::lastError() const
{
    return m_lastError;
}

QString SoundLibrary::lastWarning() const
{
    return m_lastWarning;
}

QStringList SoundLibrary::unsupportedFiles() const
{
    return m_unsupportedFiles;
}

QVector<SoundClip> SoundLibrary::clips() const
{
    return m_clips;
}

QHash<QString, SoundMetadata> SoundLibrary::metadata() const
{
    return MetadataStore::metadataFromClips(m_clips);
}

QVector<SoundClip> SoundLibrary::filteredClips(const QString& query, bool favoritesOnly, const QString& category) const
{
    return SoundFilter::filter(m_clips, query, favoritesOnly, category);
}

QStringList SoundLibrary::categories() const
{
    return SoundFilter::categories(m_clips);
}
