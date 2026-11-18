#include "thermalcontroller.h"
#include "udpcommunicator.h"

#include <QSettings>
#include <QDebug>

ThermalController::ThermalController(UdpCommunicator* udp, QObject* parent)
    : QObject(parent), m_udp(udp)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10000);
    connect(m_pollTimer, &QTimer::timeout, this, &ThermalController::queryStatus);
    if (m_udp) {
        connect(m_udp, &UdpCommunicator::packetReceived,
                this, &ThermalController::handleIncomingPacket);
    }
}

bool ThermalController::loadSettings(const QString& iniPath)
{
    QSettings s(iniPath, QSettings::IniFormat);
    m_targetId = static_cast<uint8_t>(s.value("TargetIDs/thermal", 12).toUInt());
    return true;
}

void ThermalController::startPolling()
{
    queryStatus();
    m_pollTimer->start();
}

void ThermalController::stopPolling()
{
    m_pollTimer->stop();
}

QByteArray ThermalController::buildPacket(const QByteArray& body) const
{
    // 55 AA | body | XOR(body) | F0
    // XOR covers all bytes of body (length byte included), as in the protocol examples.
    quint8 xorVal = 0;
    for (unsigned char b : body)
        xorVal ^= b;
    QByteArray pkt;
    pkt.append(char(0x55));
    pkt.append(char(0xAA));
    pkt.append(body);
    pkt.append(char(xorVal));
    pkt.append(char(0xF0));
    return pkt;
}

bool ThermalController::send(const QByteArray& body)
{
    if (!m_udp)
        return false;
    return m_udp->sendPacket(m_targetId, buildPacket(body));
}

void ThermalController::setPalette(int code)
{
    // 55 AA 07 02 00 04 00 00 00 <code> XOR F0
    QByteArray body;
    body.append(char(0x07));
    body.append(char(0x02));
    body.append(char(0x00));
    body.append(char(0x04));
    body.append(char(0x00));
    body.append(char(0x00));
    body.append(char(0x00));
    body.append(char(code & 0xFF));
    send(body);
}

void ThermalController::setTone(int code)
{
    QByteArray body = QByteArray::fromHex("07020219000000");
    body.append(char(code & 0xFF));
    send(body);
}

void ThermalController::setImageMode(int code)
{
    // Soft=0, Standard=1, Highlight=3
    QByteArray body = QByteArray::fromHex("07020206000000");
    body.append(char(code & 0xFF));
    send(body);
}

void ThermalController::setDenoise(int level)
{
    level = qBound(1, level, 3);
    QByteArray body = QByteArray::fromHex("0702021C000000");
    body.append(char(level));
    send(body);
}

void ThermalController::setDetail(int level)
{
    level = qBound(1, level, 4);
    QByteArray body = QByteArray::fromHex("0702021D000000");
    body.append(char(level));
    send(body);
}

void ThermalController::setBrightness(int level)
{
    level = qBound(1, level, 5);
    QByteArray body = QByteArray::fromHex("0702021E000000");
    body.append(char(level));
    send(body);
}

void ThermalController::setContrast(int level)
{
    level = qBound(1, level, 5);
    QByteArray body = QByteArray::fromHex("0702021F000000");
    body.append(char(level));
    send(body);
}

void ThermalController::shutterCompensation()
{
    // 55 AA 07 02 01 08 00 00 00 01 0D F0
    send(QByteArray::fromHex("0702010800000001"));
}

void ThermalController::sceneCompensation()
{
    // 55 AA 07 02 01 07 00 00 00 01 02 F0
    send(QByteArray::fromHex("0702010700000001"));
}

void ThermalController::queryStatus()
{
    // 55 AA 07 00 00 80 00 00 00 00 87 F0
    send(QByteArray::fromHex("0700008000000000"));
}

void ThermalController::handleIncomingPacket(uint8_t sourceId, const QByteArray& payload)
{
    if (sourceId != m_targetId || payload.size() < 12)
        return;
    // Expect 55 AA ... focal temp at bytes 10-11
    if (static_cast<quint8>(payload.at(0)) != 0x55 ||
        static_cast<quint8>(payload.at(1)) != 0xAA)
        return;
    // Status page reply has length 0x13 and page class 0x00
    if (payload.size() >= 12 &&
        static_cast<quint8>(payload.at(2)) == 0x13 &&
        static_cast<quint8>(payload.at(3)) == 0x00) {
        const int raw = (static_cast<quint8>(payload.at(10)) << 8) |
                        static_cast<quint8>(payload.at(11));
        const double celsius = raw / 100.0;
        emit focalTempUpdated(celsius);
    }
}
