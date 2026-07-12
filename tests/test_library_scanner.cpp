#include <QtTest/QtTest>

#include "core/LibraryScanner.h"

class LibraryScannerTest : public QObject {
    Q_OBJECT

private slots:
    void findsSupportedAudioRecursively();
    void reportsUnsupportedFilesWithoutTreatingThemAsClips();
};

static void writeTinyFile(const QString& path)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    file.write("not real audio, but enough for scanner tests");
}

void LibraryScannerTest::findsSupportedAudioRecursively()
{
    QTemporaryDir library;
    QVERIFY(library.isValid());
    QVERIFY(QDir(library.path()).mkpath("effects/hits"));

    writeTinyFile(library.filePath("intro.wav"));
    writeTinyFile(library.filePath("effects/hits/impact.FLAC"));
    writeTinyFile(library.filePath("effects/hits/readme.txt"));

    LibraryScanner scanner;
    const ScanResult result = scanner.scan(library.path());

    QCOMPARE(result.clips.size(), 2);
    QCOMPARE(result.unsupportedFiles.size(), 1);

    const QStringList keys = {result.clips.at(0).relativePath, result.clips.at(1).relativePath};
    QVERIFY(keys.contains("intro.wav"));
    QVERIFY(keys.contains("effects/hits/impact.FLAC"));
}

void LibraryScannerTest::reportsUnsupportedFilesWithoutTreatingThemAsClips()
{
    QTemporaryDir library;
    QVERIFY(library.isValid());

    writeTinyFile(library.filePath("cue.mp3"));
    writeTinyFile(library.filePath("cover.png"));
    writeTinyFile(library.filePath("notes.md"));

    LibraryScanner scanner;
    const ScanResult result = scanner.scan(library.path());

    QCOMPARE(result.clips.size(), 1);
    QCOMPARE(result.unsupportedFiles.size(), 2);
    QVERIFY(result.warning.isEmpty());
}

QTEST_MAIN(LibraryScannerTest)

#include "test_library_scanner.moc"
