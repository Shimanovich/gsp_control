#include "gyrocontroller.h"
#include "udpcommunicator.h"
#include <QDebug>
#include <QtEndian>

GyroController::GyroController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &GyroController::pollAngles);

    if (m_udp) {
        connect(m_udp, &UdpCommunicator::packetReceived,
                this, &GyroController::handleIncomingPacket);
    }
}

bool GyroController::loadSettings(const QString& iniPath)
{
    // Can load more params later
    return true;
}

void GyroController::setSpeed(float yawSpeed, float pitchSpeed)
{
    QByteArray payload = buildControlPayload(SimpleBGC::MODE_SPEED, yawSpeed, pitchSpeed);
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::goToZeroPosition()
{
    QByteArray payload = buildControlPayload(SimpleBGC::MODE_ANGLE, 0, 0, 0);
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::startAnglePolling()
{
    m_pollTimer->start(m_pollIntervalMs);
}

void GyroController::stopAnglePolling()
{
    m_pollTimer->stop();
}

void GyroController::pollAngles()
{
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_REALTIME_DATA_3);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

QByteArray GyroController::buildControlPayload(uint8_t mode, float yaw, float pitch, float roll)
{
    QByteArray payload;

    payload.append(char(mode));           // control mode
    payload.append(char(0x00));           // reserved / flags

    // Roll, Pitch, Yaw (16-bit signed, in 0.1 degree units)
    int16_t r = qToLittleEndian<int16_t>(static_cast<int16_t>(roll * 10));
    int16_t p = qToLittleEndian<int16_t>(static_cast<int16_t>(pitch * 10));
    int16_t y = qToLittleEndian<int16_t>(static_cast<int16_t>(yaw * 10));

    payload.append(reinterpret_cast<const char*>(&r), 2);
    payload.append(reinterpret_cast<const char*>(&p), 2);
    payload.append(reinterpret_cast<const char*>(&y), 2);

    // TODO: Add more fields if you need full CMD_CONTROL structure (speed, etc.)

    return payload;
}

QByteArray GyroController::buildRealtimeDataRequest()
{
    QByteArray payload;
    // Usually no payload needed for CMD_REALTIME_DATA_3 request
    return payload;
}

void GyroController::handleIncomingPacket(uint8_t sourceId, const QByteArray& payload)
{
    if (sourceId != m_targetId || payload.isEmpty()) return;

    uint8_t cmd = static_cast<uint8_t>(payload.at(0));

    if (cmd == SimpleBGC::CMD_REALTIME_DATA_3 && payload.size() > 20) {
        // Parse simplified realtime data (actual structure is bigger)
        // For demo we take some bytes as angles
        float roll  = qFromLittleEndian<int16_t>(payload.constData() + 2) / 10.0f;
        float pitch = qFromLittleEndian<int16_t>(payload.constData() + 4) / 10.0f;
        float yaw   = qFromLittleEndian<int16_t>(payload.constData() + 6) / 10.0f;

        emit anglesUpdated(roll, pitch, yaw);
    }
}
