#pragma once

#include "core/SoundClip.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

struct LegacySettingsImport {
    bool valid = false;
    QString sourcePath;
    QString error;
    QString libraryFolder;
    std::optional<bool> showExtensions;
    std::optional<bool> useLoudness;
    std::optional<int> sidebarWidth;
    QString outputDeviceName;
    QStringList favoritePaths;
    std::optional<bool> virtualMicEnabled;
    std::optional<bool> micLoopbackEnabled;
    QString virtualMicOutputDevice;
    QString virtualMicInputDevice;
    QHash<QString, SoundMetadata> metadataByPath;
    mutable QStringList notes;
    QStringList imported;
    QStringList skipped;
};

class LegacySettingsImporter {
public:
    static QStringList likelyConfigFiles();
    static LegacySettingsImport readFile(const QString& filePath);
    static LegacySettingsImport readObject(const QJsonObject& object, const QString& sourcePath);

    static QStringList favoritePathsForLibrary(const LegacySettingsImport& legacy, const QString& libraryFolder);
    static QHash<QString, SoundMetadata> mergeFavoritePaths(const QHash<QString, SoundMetadata>& current,
                                                            const QStringList& favoriteRelativePaths);
    static QHash<QString, SoundMetadata> mergeMetadata(const QHash<QString, SoundMetadata>& current,
                                                       const QHash<QString, SoundMetadata>& legacy);
};
