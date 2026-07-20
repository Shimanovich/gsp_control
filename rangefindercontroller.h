#ifndef RANGEFINDERCONTROLLER_H
#define RANGEFINDERCONTROLLER_H

#include <QObject>
#include "udpcommunicator.h"

class RangefinderController : public QObject
{
    Q_OBJECT
public:
    explicit RangefinderController(UdpCommunicator* udp, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);
    void shoot();   // single measurement 0x01

signals:
    void measurementReceived(float distanceMeters, uint8_t status);
    void statusUpdated(const QString& status);

private slots:
    void handleIncomingPacket(uint8_t sourceId, const QByteArray& payload);

private:
    UdpCommunicator* m_udp = nullptr;
    uint8_t m_targetId = 13;

    void sendRangefinderCommand(uint8_t cmd, uint16_t data = 0);
};

#endif // RANGEFINDERCONTROLLER_H