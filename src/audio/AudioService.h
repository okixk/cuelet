#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct AudioOutputDevice {
    QString id;
    QString description;
    bool isDefault = false;
};

class AudioService : public QObject {
    Q_OBJECT

public:
    explicit AudioService(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    virtual ~AudioService() = default;

    virtual bool play(const QString& filePath) = 0;
    virtual void stopAll() = 0;
    virtual void setVolume(double volume) = 0;
    virtual double volume() const = 0;
    virtual void setAllowMultiplePlayback(bool allowMultiple) = 0;
    virtual bool allowMultiplePlayback() const = 0;
    virtual QVector<AudioOutputDevice> outputDevices() const = 0;
    virtual void setOutputDeviceId(const QString& id) = 0;
    virtual QString outputDeviceId() const = 0;

signals:
    void errorOccurred(const QString& message);
    void playbackIdle();
};
