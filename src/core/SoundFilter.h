#pragma once

#include "core/SoundClip.h"

#include <QString>
#include <QVector>

class SoundFilter {
public:
    static QVector<SoundClip> filter(const QVector<SoundClip>& clips,
                                     const QString& query,
                                     bool favoritesOnly,
                                     const QString& category);

    static QStringList categories(const QVector<SoundClip>& clips);

private:
    static bool matchesQuery(const SoundClip& clip, const QStringList& tokens);
};
