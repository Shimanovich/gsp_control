#ifndef GYROCONTROLLER_H
#define GYROCONTROLLER_H

#include <QObject>
#include <QTimer>
#include "simplebgc_protocol.h"

class UdpCommunicator;

class GyroController : public QObject
{
    Q_OBJECT
public:
    explicit GyroController(UdpCommunicator* udp, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);

    void setSpeed(float yawSpeed, float pitchSpeed);   // deg/s
    void goToZeroPosition();
    void startAnglePolling();
    void stopAnglePolling();

signals:
    void anglesUpdated(float roll, float pitch, float yaw); // degrees
    void error(const QString& msg);

private slots:
    void pollAngles();
    void handleIncomingPacket(uint8_t sourceId, const QByteArray& payload);

private:
    UdpCommunicator* m_udp = nullptr;
    uint8_t m_targetId = 41;
    int m_pollIntervalMs = 100; // 10 Hz

    QTimer* m_pollTimer = nullptr;

    QByteArray buildControlPayload(uint8_t mode, float yaw, float pitch, float roll = 0);
    QByteArray buildRealtimeDataRequest();
};

#endif // GYROCONTROLLER_H