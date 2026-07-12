#pragma once

#include <QSettings>
#include <QString>

class AppSettings {
public:
    explicit AppSettings(QString organization = "Cuelet", QString application = "Cuelet");

    QString libraryFolder() const;
    void setLibraryFolder(const QString& folder);
    bool hasLibraryFolder() const;

    double volume() const;
    void setVolume(double volume);

    bool allowMultiplePlayback() const;
    void setAllowMultiplePlayback(bool enabled);

    bool showFileExtensions() const;
    void setShowFileExtensions(bool enabled);
    bool hasShowFileExtensionsSetting() const;

    bool loudnessNormalizationEnabled() const;
    void setLoudnessNormalizationEnabled(bool enabled);
    bool hasLoudnessNormalizationSetting() const;

    int sidebarWidth() const;
    void setSidebarWidth(int width);
    bool hasSidebarWidthSetting() const;

    QString audioOutputDeviceId() const;
    void setAudioOutputDeviceId(const QString& deviceId);
    bool hasAudioOutputDeviceId() const;

    bool legacyMigrationAttempted() const;
    void setLegacyMigrationAttempted(bool attempted);

    QString legacyMigrationSummary() const;
    void setLegacyMigrationSummary(const QString& summary);

    QString legacyMigrationSourcePath() const;
    void setLegacyMigrationSourcePath(const QString& path);

    bool legacyVirtualMicEnabled() const;
    void setLegacyVirtualMicEnabled(bool enabled);

    bool legacyMicLoopbackEnabled() const;
    void setLegacyMicLoopbackEnabled(bool enabled);

    QString legacyVirtualMicOutputDevice() const;
    void setLegacyVirtualMicOutputDevice(const QString& device);

    QString legacyVirtualMicInputDevice() const;
    void setLegacyVirtualMicInputDevice(const QString& device);

    QString settingsFilePath() const;

private:
    QSettings m_settings;
};
