#include <QtTest/QtTest>

#include "core/LibraryScanner.h"
#include "storage/MetadataStore.h"

class PathHandlingTest : public QObject {
    Q_OBJECT

private slots:
    void missingLibraryFolderReturnsWarningInsteadOfCrashing();
    void metadataForDisappearedFilesCanBeMarkedMissingAfterRescan();
};

void PathHandlingTest::missingLibraryFolderReturnsWarningInsteadOfCrashing()
{
    LibraryScanner scanner;
    const ScanResult result = scanner.scan(QDir::temp().filePath("cuelet-folder-that-should-not-exist"));

    QVERIFY(result.clips.isEmpty());
    QVERIFY(!result.warning.isEmpty());
}

void PathHandlingTest::metadataForDisappearedFilesCanBeMarkedMissingAfterRescan()
{
    QHash<QString, SoundMetadata> metadata;
    SoundMetadata old;
    old.title = "Vanished sting";
    old.favorite = true;
    metadata.insert("old/sting.wav", old);

    QVector<SoundClip> scanned;
    SoundClip current;
    current.relativePath = "new/hit.wav";
    current.filePath = "/library/new/hit.wav";
    scanned.append(current);

    const QVector<SoundClip> merged = MetadataStore::mergeWithScannedClips(metadata, scanned);

    QCOMPARE(merged.size(), 2);
    const auto missing = std::find_if(merged.cbegin(), merged.cend(), [](const SoundClip& clip) {
        return clip.relativePath == "old/sting.wav";
    });
    QVERIFY(missing != merged.cend());
    QVERIFY(missing->missing);
    QCOMPARE(missing->metadata.title, QString("Vanished sting"));
    QVERIFY(missing->metadata.favorite);
}

QTEST_MAIN(PathHandlingTest)

#include "test_path_handling.moc"
