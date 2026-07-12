#include <QtTest/QtTest>

#include "core/LibraryScanner.h"
#include "core/SoundFilter.h"
#include "storage/AppSettings.h"

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void coreServicesCanBeConstructed();
};

void SmokeTest::coreServicesCanBeConstructed()
{
    LibraryScanner scanner;
    QVERIFY(scanner.supportedExtensions().contains("mp3"));
    QVERIFY(scanner.supportedExtensions().contains("m4a"));

    const QVector<SoundClip> clips;
    QVERIFY(SoundFilter::categories(clips).isEmpty());

    AppSettings settings("CueletTests", "Smoke");
    QVERIFY(settings.libraryFolder().isEmpty());
}

QTEST_MAIN(SmokeTest)

#include "test_smoke.moc"
