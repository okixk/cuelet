#include <QtTest/QtTest>

#include "core/SoundFilter.h"

class SoundFilterTest : public QObject {
    Q_OBJECT

private slots:
    void searchesTitleFileNameNotesAndAliasesCaseInsensitively();
    void filtersFavoritesAndCategoriesTogether();
};

static SoundClip clip(QString relativePath, QString title, QString category, bool favorite, QStringList aliases = {})
{
    SoundClip sound;
    sound.relativePath = std::move(relativePath);
    sound.filePath = QDir::cleanPath("/library/" + sound.relativePath);
    sound.metadata.title = std::move(title);
    sound.metadata.category = std::move(category);
    sound.metadata.favorite = favorite;
    sound.metadata.aliases = std::move(aliases);
    return sound;
}

void SoundFilterTest::searchesTitleFileNameNotesAndAliasesCaseInsensitively()
{
    QVector<SoundClip> clips = {
        clip("voice/hello.wav", "Friendly Hello", "Voice", false, {"greeting"}),
        clip("fx/thunder.ogg", "Storm Hit", "Weather", true, {"boom"}),
        clip("music/theme.flac", "Opening Theme", "Music", false, {"intro"}),
    };
    clips[1].metadata.notes = "Very loud thunder clap.";

    QCOMPARE(SoundFilter::filter(clips, "HELLO", false, {}).size(), 1);
    QCOMPARE(SoundFilter::filter(clips, "boom", false, {}).size(), 1);
    QCOMPARE(SoundFilter::filter(clips, "thunder clap", false, {}).size(), 1);
    QCOMPARE(SoundFilter::filter(clips, "theme.flac", false, {}).size(), 1);
    QCOMPARE(SoundFilter::filter(clips, "missing", false, {}).size(), 0);
}

void SoundFilterTest::filtersFavoritesAndCategoriesTogether()
{
    const QVector<SoundClip> clips = {
        clip("voice/hello.wav", "Friendly Hello", "Voice", false),
        clip("fx/thunder.ogg", "Storm Hit", "Weather", true),
        clip("fx/rain.ogg", "Rain Bed", "Weather", false),
    };

    const QVector<SoundClip> favorites = SoundFilter::filter(clips, "", true, {});
    QCOMPARE(favorites.size(), 1);
    QCOMPARE(favorites.first().relativePath, QString("fx/thunder.ogg"));

    const QVector<SoundClip> weather = SoundFilter::filter(clips, "", false, "Weather");
    QCOMPARE(weather.size(), 2);

    const QVector<SoundClip> favoriteWeather = SoundFilter::filter(clips, "storm", true, "Weather");
    QCOMPARE(favoriteWeather.size(), 1);
    QCOMPARE(favoriteWeather.first().metadata.title, QString("Storm Hit"));
}

QTEST_MAIN(SoundFilterTest)

#include "test_sound_filter.moc"
