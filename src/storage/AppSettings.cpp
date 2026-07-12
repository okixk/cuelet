#include "storage/AppSettings.h"

#include <QtGlobal>

#include <utility>

AppSettings::AppSettings(QString organization, QString application)
    : m_settings(std::move(organization), std::move(application))
{
}

QString AppSettings::libraryFolder() const
{
    return m_settings.value("library/folder").toString();
}

void AppSettings::setLibraryFolder(const QString& folder)
{
    m_settings.setValue("library/folder", folder);
}

bool AppSettings::hasLibraryFolder() const
{
    return m_settings.contains("library/folder") && !libraryFolder().trimmed().isEmpty();
}

double AppSettings::volume() const
{
    return qBound(0.0, m_settings.value("audio/volume", 0.75).toDouble(), 1.0);
}

void AppSettings::setVolume(double volume)
{
    m_settings.setValue("audio/volume", qBound(0.0, volume, 1.0));
}

bool AppSettings::allowMultiplePlayback() const
{
    return m_settings.value("audio/allowMultiplePlayback", true).toBool();
}

void AppSettings::setAllowMultiplePlayback(bool enabled)
{
    m_settings.setValue("audio/allowMultiplePlayback", enabled);
}

bool AppSettings::showFileExtensions() const
{
    return m_settings.value("display/showFileExtensions", true).toBool();
}

void AppSettings::setShowFileExtensions(bool enabled)
{
    m_settings.setValue("display/showFileExtensions", enabled);
}

bool AppSettings::hasShowFileExtensionsSetting() const
{
    return m_settings.contains("display/showFileExtensions");
}

bool AppSettings::loudnessNormalizationEnabled() const
{
    return m_settings.value("audio/loudnessNormalization", false).toBool();
}

void AppSettings::setLoudnessNormalizationEnabled(bool enabled)
{
    m_settings.setValue("audio/loudnessNormalization", enabled);
}

bool AppSettings::hasLoudnessNormalizationSetting() const
{
    return m_settings.contains("audio/loudnessNormalization");
}

int AppSettings::sidebarWidth() const
{
    return qBound(148, m_settings.value("display/sidebarWidth", 196).toInt(), 360);
}

void AppSettings::setSidebarWidth(int width)
{
    m_settings.setValue("display/sidebarWidth", qBound(148, width, 360));
}

bool AppSettings::hasSidebarWidthSetting() const
{
    return m_settings.contains("display/sidebarWidth");
}

QString AppSettings::audioOutputDeviceId() const
{
    return m_settings.value("audio/outputDeviceId").toString();
}

void AppSettings::setAudioOutputDeviceId(const QString& deviceId)
{
    m_settings.setValue("audio/outputDeviceId", deviceId);
}

bool AppSettings::hasAudioOutputDeviceId() const
{
    return m_settings.contains("audio/outputDeviceId") && !audioOutputDeviceId().trimmed().isEmpty();
}

bool AppSettings::legacyMigrationAttempted() const
{
    return m_settings.value("legacy/migrationAttempted", false).toBool();
}

void AppSettings::setLegacyMigrationAttempted(bool attempted)
{
    m_settings.setValue("legacy/migrationAttempted", attempted);
}

QString AppSettings::legacyMigrationSummary() const
{
    return m_settings.value("legacy/migrationSummary").toString();
}

void AppSettings::setLegacyMigrationSummary(const QString& summary)
{
    m_settings.setValue("legacy/migrationSummary", summary);
}

QString AppSettings::legacyMigrationSourcePath() const
{
    return m_settings.value("legacy/sourcePath").toString();
}

void AppSettings::setLegacyMigrationSourcePath(const QString& path)
{
    m_settings.setValue("legacy/sourcePath", path);
}

bool AppSettings::legacyVirtualMicEnabled() const
{
    return m_settings.value("legacy/virtualMicEnabled", false).toBool();
}

void AppSettings::setLegacyVirtualMicEnabled(bool enabled)
{
    m_settings.setValue("legacy/virtualMicEnabled", enabled);
}

bool AppSettings::legacyMicLoopbackEnabled() const
{
    return m_settings.value("legacy/micLoopbackEnabled", false).toBool();
}

void AppSettings::setLegacyMicLoopbackEnabled(bool enabled)
{
    m_settings.setValue("legacy/micLoopbackEnabled", enabled);
}

QString AppSettings::legacyVirtualMicOutputDevice() const
{
    return m_settings.value("legacy/virtualMicOutputDevice").toString();
}

void AppSettings::setLegacyVirtualMicOutputDevice(const QString& device)
{
    m_settings.setValue("legacy/virtualMicOutputDevice", device);
}

QString AppSettings::legacyVirtualMicInputDevice() const
{
    return m_settings.value("legacy/virtualMicInputDevice").toString();
}

void AppSettings::setLegacyVirtualMicInputDevice(const QString& device)
{
    m_settings.setValue("legacy/virtualMicInputDevice", device);
}

QString AppSettings::settingsFilePath() const
{
    return m_settings.fileName();
}
