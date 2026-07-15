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
    // CMD_CONTROL in SPEED mode
    QByteArray packet = buildCmdControl(SimpleBGC::MODE_SPEED, yawSpeed, pitchSpeed);
    if (m_udp) m_udp->sendPacket(m_targetId, packet);
}

void GyroController::goToZeroPosition()
{
    // CMD_CONTROL in ANGLE mode to 0,0,0
    QByteArray packet = buildCmdControl(SimpleBGC::MODE_ANGLE, 0, 0, 0);
    if (m_udp) m_udp->sendPacket(m_targetId, packet);
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
    QByteArray req = buildRealtimeDataRequest();
    if (m_udp) m_udp->sendPacket(m_targetId, req);
}

QByteArray GyroController::buildCmdControl(uint8_t mode, float yaw, float pitch, float roll)
{
    QByteArray payload;
    payload.append(char(SimpleBGC::CMD_CONTROL));
    payload.append(char(0x0E)); // payload size for basic control (adjust if needed)

    // Simplified CMD_CONTROL structure (common fields)
    payload.append(char(mode));           // control mode
    payload.append(char(0x00));           // reserved

    // Angles or speeds (16-bit signed, scaled)
    // For simplicity we send speed or angle in 0.1 deg units
    int16_t r = qToLittleEndian<int16_t>(static_cast<int16_t>(roll * 10));
    int16_t p = qToLittleEndian<int16_t>(static_cast<int16_t>(pitch * 10));
    int16_t y = qToLittleEndian<int16_t>(static_cast<int16_t>(yaw * 10));

    payload.append(reinterpret_cast<const char*>(&r), 2);
    payload.append(reinterpret_cast<const char*>(&p), 2);
    payload.append(reinterpret_cast<const char*>(&y), 2);

    // Add header + CRC for Protocol v2 would be done in a more complete implementation
    // For now we send simplified version

    return payload;
}

QByteArray GyroController::buildRealtimeDataRequest()
{
    QByteArray payload;
    payload.append(char(SimpleBGC::CMD_REALTIME_DATA_3));
    payload.append(char(0x00)); // request all data
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
