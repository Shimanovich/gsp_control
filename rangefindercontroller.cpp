#include "rangefindercontroller.h"

RangefinderController::RangefinderController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{

    if (m_udp) {
        connect(m_udp, &UdpCommunicator::packetReceived,
                this, &RangefinderController::handleIncomingPacket);
    }
}

bool RangefinderController::loadSettings(const QString& iniPath)
{
    // Load if needed
    return true;
}

void RangefinderController::shoot()
{
    // Command 0x01 - single measurement
    //sendRangefinderCommand(0x01);
    sendRangefinderCommand(0x03);
}

void RangefinderController::sendRangefinderCommand(uint8_t cmd, uint16_t data)
{
    if (!m_udp) return;

    QByteArray packet;
    packet.append(char(0x55));           // STX
    packet.append(char(cmd));
    packet.append(char(0x02));           // LEN
    packet.append(char(data >> 8));
    packet.append(char(data & 0xFF));

    // XOR checksum
    uint8_t chk = 0;
    for (int i = 0; i < packet.size(); ++i) chk ^= packet.at(i);
    packet.append(char(chk));

    m_udp->sendPacket(m_targetId, packet);
}


void RangefinderController::handleIncomingPacket(uint8_t sourceId, const QByteArray& payload)
{
    if (sourceId != m_targetId || payload.size() < 4)
        return;

    if (static_cast<uint8_t>(payload[0]) != 0x55)
        return;

    const uint8_t cmd  = static_cast<uint8_t>(payload[1]);
    const uint8_t len  = static_cast<uint8_t>(payload[2]);
    const int expectedSize = 3 + len + 1; // STX+CMD+LEN + DATA + CHK

    if (payload.size() != expectedSize)
        return;

    // Проверка XOR
    uint8_t chk = 0;
    for (int i = 0; i < payload.size() - 1; ++i)
        chk ^= static_cast<uint8_t>(payload[i]);
    if (chk != static_cast<uint8_t>(payload.back()))
        return;

    // Одиночное / непрерывное измерение (CMD 0x01 / 0x02)
    if ((cmd == 0x01 || cmd == 0x02) && len == 10) { // 14 байт всего → DATA = 10 байт
        // D9 D8 D7 D6 D5 D4 D3 D2 D1 D0
        const uint8_t* d = reinterpret_cast<const uint8_t*>(payload.constData() + 3);

        uint8_t statusByte = d[0]; // D9
        // Расстояние 1-й цели (D8-D6) — 3 байта, единица 0.1 м
        uint32_t dist1_raw = (d[1] << 16) | (d[2] << 8) | d[3];
        float dist1_m = dist1_raw * 0.1f;

        // аналогично dist2, dist3 при необходимости
        // ...

        emit measurementReceived(dist1_m, statusByte);
        return;
    }

    // Остальные CMD (0x00, 0x03, 0x04, 0x06, 0x22, 0x26) — по таблице 5 PDF
    // ...
}