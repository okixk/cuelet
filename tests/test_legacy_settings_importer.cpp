#include <QtTest/QtTest>

#include "storage/LegacySettingsImporter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

class LegacySettingsImporterTest : public QObject {
    Q_OBJECT

private slots:
    void readsLegacyJsonConfig();
    void convertsFavoritePathsInsideLibrary();
    void mergesFavoritesWithoutClearingExistingMetadata();
    void mapsLegacySidecarMetadataWithoutOverwritingCueletFields();
};

static void writeJson(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

void LegacySettingsImporterTest::readsLegacyJsonConfig()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString configPath = temp.filePath("soundboard.json");
    writeJson(configPath,
              {
                  {"library_dir", temp.filePath("Library")},
                  {"show_extensions", false},
                  {"use_loudness", true},
                  {"sidebar_width", 244},
                  {"output_device", "Studio Display Speakers"},
                  {"virtual_mic_enabled", true},
                  {"mic_loopback_enabled", false},
                  {"virtual_mic_output_device", "BlackHole 2ch"},
                  {"virtual_mic_input_device", "External Mic"},
              });

    const LegacySettingsImport parsed = LegacySettingsImporter::readFile(configPath);

    QVERIFY(parsed.valid);
    QCOMPARE(parsed.sourcePath, configPath);
    QCOMPARE(parsed.libraryFolder, temp.filePath("Library"));
    QCOMPARE(parsed.showExtensions.value_or(true), false);
    QCOMPARE(parsed.useLoudness.value_or(false), true);
    QCOMPARE(parsed.sidebarWidth.value_or(0), 244);
    QCOMPARE(parsed.outputDeviceName, QString("Studio Display Speakers"));
    QVERIFY(parsed.virtualMicEnabled.value_or(false));
    QCOMPARE(parsed.micLoopbackEnabled.value_or(true), false);
    QCOMPARE(parsed.virtualMicOutputDevice, QString("BlackHole 2ch"));
    QCOMPARE(parsed.virtualMicInputDevice, QString("External Mic"));
}

void LegacySettingsImporterTest::convertsFavoritePathsInsideLibrary()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QDir library(temp.filePath("Library"));
    QVERIFY(library.mkpath("fx"));

    const LegacySettingsImport parsed = LegacySettingsImporter::readObject(
        {
            {"library_dir", library.absolutePath()},
            {"favorite_paths", QJsonArray{library.filePath("fx/hit.wav"), "/outside/other.wav", "ui/click.wav"}},
        },
        "memory");

    const QStringList favorites = LegacySettingsImporter::favoritePathsForLibrary(parsed, library.absolutePath());

    QCOMPARE(favorites, QStringList({"fx/hit.wav", "ui/click.wav"}));
    QVERIFY(parsed.notes.join('\n').contains("/outside/other.wav"));
}

void LegacySettingsImporterTest::mergesFavoritesWithoutClearingExistingMetadata()
{
    QHash<QString, SoundMetadata> current;
    SoundMetadata existing;
    existing.title = "Door";
    existing.favorite = true;
    current.insert("fx/door.wav", existing);

    const QStringList legacyFavorites = {"fx/door.wav", "fx/thunder.wav"};
    const QHash<QString, SoundMetadata> merged = LegacySettingsImporter::mergeFavoritePaths(current, legacyFavorites);

    QCOMPARE(merged.size(), 2);
    QVERIFY(merged.value("fx/door.wav").favorite);
    QCOMPARE(merged.value("fx/door.wav").title, QString("Door"));
    QVERIFY(merged.value("fx/thunder.wav").favorite);
}

void LegacySettingsImporterTest::mapsLegacySidecarMetadataWithoutOverwritingCueletFields()
{
    SoundMetadata cuelet;
    cuelet.title = "New Title";
    cuelet.notes = "Existing note";
    cuelet.aliases = {"current"};

    QHash<QString, SoundMetadata> current;
    current.insert("fx/laser.wav", cuelet);

    const LegacySettingsImport parsed = LegacySettingsImporter::readObject(
        {
            {"sounds",
             QJsonObject{
                 {"fx/laser.wav",
                  QJsonObject{
                      {"title", "Old Title"},
                      {"category", "Sci-Fi"},
                      {"note", "Old note"},
                      {"link", "https://example.com"},
                      {"shortcut", "L"},
                      {"emoji", "zap"},
                      {"favorite", true},
                  }},
             }},
        },
        "metadata");

    const QHash<QString, SoundMetadata> merged = LegacySettingsImporter::mergeMetadata(current, parsed.metadataByPath);
    const SoundMetadata value = merged.value("fx/laser.wav");

    QCOMPARE(value.title, QString("New Title"));
    QCOMPARE(value.category, QString("Sci-Fi"));
    QCOMPARE(value.notes, QString("Existing note"));
    QCOMPARE(value.icon, QString("zap"));
    QVERIFY(value.favorite);
    QVERIFY(value.aliases.contains("current"));
    QVERIFY(value.aliases.contains("shortcut:L"));
    QVERIFY(value.aliases.contains("link:https://example.com"));
}

QTEST_MAIN(LegacySettingsImporterTest)

#include "test_legacy_settings_importer.moc"
