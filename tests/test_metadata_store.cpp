#include <QtTest/QtTest>

#include "storage/MetadataStore.h"

class MetadataStoreTest : public QObject {
    Q_OBJECT

private slots:
    void savesAndLoadsMetadataByRelativePath();
    void invalidJsonReturnsEmptyMetadataAndWarning();
};

void MetadataStoreTest::savesAndLoadsMetadataByRelativePath()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    MetadataStore store(temp.filePath("cuelet-metadata.json"));

    SoundMetadata metadata;
    metadata.title = "Door slam";
    metadata.category = "Tabletop";
    metadata.favorite = true;
    metadata.icon = "!";
    metadata.notes = "Good for dramatic exits.";
    metadata.aliases = {"door", "slam", "exit"};

    QHash<QString, SoundMetadata> values;
    values.insert("fx/door.wav", metadata);

    QVERIFY2(store.save(values), qPrintable(store.lastError()));

    const auto loaded = store.load();
    QVERIFY2(store.lastError().isEmpty(), qPrintable(store.lastError()));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.value("fx/door.wav").title, QString("Door slam"));
    QCOMPARE(loaded.value("fx/door.wav").category, QString("Tabletop"));
    QVERIFY(loaded.value("fx/door.wav").favorite);
    QCOMPARE(loaded.value("fx/door.wav").aliases, QStringList({"door", "slam", "exit"}));
}

void MetadataStoreTest::invalidJsonReturnsEmptyMetadataAndWarning()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QFile file(temp.filePath("cuelet-metadata.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ definitely not valid json");

    MetadataStore store(file.fileName());
    const auto loaded = store.load();

    QVERIFY(loaded.isEmpty());
    QVERIFY(!store.lastError().isEmpty());
}

QTEST_MAIN(MetadataStoreTest)

#include "test_metadata_store.moc"
