#pragma once

#include "audio/AudioService.h"

#include <QAudioDevice>
#include <QPointer>
#include <QVector>

class QAudioOutput;
class QMediaPlayer;

class QtAudioService : public AudioService {
    Q_OBJECT

public:
    explicit QtAudioService(QObject* parent = nullptr);

    bool play(const QString& filePath) override;
    void stopAll() override;
    void setVolume(double volume) override;
    double volume() const override;
    void setAllowMultiplePlayback(bool allowMultiple) override;
    bool allowMultiplePlayback() const override;
    QVector<AudioOutputDevice> outputDevices() const override;
    void setOutputDeviceId(const QString& id) override;
    QString outputDeviceId() const override;

private:
    struct Playback {
        QPointer<QMediaPlayer> player;
        QPointer<QAudioOutput> output;
    };

    QAudioDevice selectedDevice() const;
    void pruneFinishedPlayer(QMediaPlayer* player);

    QVector<Playback> m_playbacks;
    double m_volume = 0.75;
    bool m_allowMultiplePlayback = true;
    QString m_outputDeviceId;
};
