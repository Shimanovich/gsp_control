#include "rangefindercontroller.h"

RangefinderController::RangefinderController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
}

bool RangefinderController::loadSettings(const QString& iniPath)
{
    // Load if needed
    return true;
}

void RangefinderController::shoot()
{
    // Command 0x01 - single measurement
    sendRangefinderCommand(0x01);
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
