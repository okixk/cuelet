#include "audio/QtAudioService.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

QtAudioService::QtAudioService(QObject* parent)
    : AudioService(parent)
{
}

bool QtAudioService::play(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        emit errorOccurred(tr("The selected sound file is missing."));
        return false;
    }

    if (!m_allowMultiplePlayback) {
        stopAll();
    }

    QAudioOutput* output = nullptr;
    const QAudioDevice device = selectedDevice();
    if (!device.isNull()) {
        output = new QAudioOutput(device, this);
    } else {
        output = new QAudioOutput(this);
    }
    output->setVolume(static_cast<float>(m_volume));

    QMediaPlayer* player = new QMediaPlayer(this);
    player->setAudioOutput(output);
    player->setSource(QUrl::fromLocalFile(info.absoluteFilePath()));

    connect(player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& errorString) {
        emit errorOccurred(errorString.isEmpty() ? tr("Qt Multimedia could not play this sound.") : errorString);
    });
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, player](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
            pruneFinishedPlayer(player);
        }
    });

    m_playbacks.append({player, output});
    player->play();
    return true;
}

void QtAudioService::stopAll()
{
    const bool hadPlayback = !m_playbacks.isEmpty();
    const QVector<Playback> playbacks = m_playbacks;
    m_playbacks.clear();
    for (const Playback& playback : playbacks) {
        if (playback.player) {
            playback.player->stop();
            playback.player->deleteLater();
        }
        if (playback.output) {
            playback.output->deleteLater();
        }
    }
    if (hadPlayback) {
        emit playbackIdle();
    }
}

void QtAudioService::setVolume(double volume)
{
    m_volume = qBound(0.0, volume, 1.0);
    for (const Playback& playback : m_playbacks) {
        if (playback.output) {
            playback.output->setVolume(static_cast<float>(m_volume));
        }
    }
}

double QtAudioService::volume() const
{
    return m_volume;
}

void QtAudioService::setAllowMultiplePlayback(bool allowMultiple)
{
    m_allowMultiplePlayback = allowMultiple;
}

bool QtAudioService::allowMultiplePlayback() const
{
    return m_allowMultiplePlayback;
}

QVector<AudioOutputDevice> QtAudioService::outputDevices() const
{
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    QVector<AudioOutputDevice> devices;
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        AudioOutputDevice item;
        item.id = QString::fromLatin1(device.id().toBase64());
        item.description = device.description();
        item.isDefault = device.id() == defaultDevice.id();
        devices.append(item);
    }
    return devices;
}

void QtAudioService::setOutputDeviceId(const QString& id)
{
    m_outputDeviceId = id;
}

QString QtAudioService::outputDeviceId() const
{
    return m_outputDeviceId;
}

QAudioDevice QtAudioService::selectedDevice() const
{
    if (m_outputDeviceId.isEmpty()) {
        return QAudioDevice();
    }

    const QByteArray wantedId = QByteArray::fromBase64(m_outputDeviceId.toLatin1());
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        if (device.id() == wantedId) {
            return device;
        }
    }

    return QAudioDevice();
}

void QtAudioService::pruneFinishedPlayer(QMediaPlayer* player)
{
    for (auto it = m_playbacks.begin(); it != m_playbacks.end(); ++it) {
        if (it->player == player) {
            if (it->player) {
                it->player->deleteLater();
            }
            if (it->output) {
                it->output->deleteLater();
            }
            m_playbacks.erase(it);
            if (m_playbacks.isEmpty()) {
                emit playbackIdle();
            }
            return;
        }
    }
}
