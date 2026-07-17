#include "core/SoundFilter.h"

#include <QRegularExpression>
#include <QSet>

static QString searchableTextFor(const SoundClip& clip)
{
    QStringList parts;
    parts << clip.displayTitle()
          << clip.relativePath
          << clip.metadata.category
          << clip.metadata.notes
          << clip.metadata.aliases;
    return parts.join(' ').toCaseFolded();
}

bool SoundFilter::matchesQuery(const SoundClip& clip, const QStringList& tokens)
{
    if (tokens.isEmpty()) {
        return true;
    }

    const QString haystack = searchableTextFor(clip);
    for (const QString& token : tokens) {
        if (!haystack.contains(token.toCaseFolded())) {
            return false;
        }
    }
    return true;
}

QVector<SoundClip> SoundFilter::filter(const QVector<SoundClip>& clips,
                                       const QString& query,
                                       bool favoritesOnly,
                                       const QString& category)
{
    const QStringList tokens = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString normalizedCategory = category.trimmed();

    QVector<SoundClip> filtered;
    filtered.reserve(clips.size());

    for (const SoundClip& clip : clips) {
        if (favoritesOnly && !clip.metadata.favorite) {
            continue;
        }
        if (!normalizedCategory.isEmpty()
            && QString::compare(clip.metadata.category, normalizedCategory, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!matchesQuery(clip, tokens)) {
            continue;
        }
        filtered.append(clip);
    }

    return filtered;
}

QStringList SoundFilter::categories(const QVector<SoundClip>& clips)
{
    QSet<QString> seen;
    for (const SoundClip& clip : clips) {
        const QString category = clip.metadata.category.trimmed();
        if (!category.isEmpty()) {
            seen.insert(category);
        }
    }

    QStringList result = seen.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}
