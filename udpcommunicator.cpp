#include "udpcommunicator.h"
#include <QDateTime>
#include <QDebug>

UdpCommunicator::UdpCommunicator(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpCommunicator::onReadyRead);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setInterval(1000);
    connect(m_timeoutTimer, &QTimer::timeout, this, &UdpCommunicator::checkConnectionTimeout);
}

UdpCommunicator::~UdpCommunicator()
{
    stop();
}

bool UdpCommunicator::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);

    QString ip = settings.value("Network/ip", "192.168.1.100").toString();
    m_remoteAddress = QHostAddress(ip);
    m_port = settings.value("Network/port", 5000).toUInt();
    m_connectionTimeoutMs = settings.value("Network/connection_timeout_ms", 5000).toInt();

    if (m_remoteAddress.isNull()) {
        emit errorOccurred("Invalid IP address in config");
        return false;
    }
    return true;
}

bool UdpCommunicator::start()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }

    // Bind to the same port for receiving
    if (!m_socket->bind(QHostAddress::AnyIPv4, m_port)) {
        emit errorOccurred(QString("Failed to bind UDP port %1: %2").arg(m_port).arg(m_socket->errorString()));
        return false;
    }

    qDebug() << "UDP bound to port" << m_port << "for send/receive";
    m_timeoutTimer->start();
    m_connected = false;
    emit connectionStatusChanged(false);
    return true;
}

void UdpCommunicator::stop()
{
    if (m_timeoutTimer) m_timeoutTimer->stop();
    if (m_socket) m_socket->close();
    m_connected = false;
}

bool UdpCommunicator::sendPacket(uint8_t targetId, const QByteArray& payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::BoundState)
        return false;

    QByteArray packet;
    packet.append(static_cast<char>(targetId));
    packet.append(payload);

    qint64 bytes = m_socket->writeDatagram(packet, m_remoteAddress, m_port);
    return bytes == packet.size();
}

bool UdpCommunicator::isConnected() const
{
    return m_connected;
}

void UdpCommunicator::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket->pendingDatagramSize());
        QHostAddress sender;
        quint16 senderPort;

        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        if (datagram.size() < 1) continue;

        uint8_t sourceId = static_cast<uint8_t>(datagram.at(0));
        QByteArray payload = datagram.mid(1);

        // Update last receive time for this source
        m_lastReceiveTime[sourceId] = QDateTime::currentMSecsSinceEpoch();

        emit packetReceived(sourceId, payload);
    }
}

void UdpCommunicator::checkConnectionTimeout()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool anyActive = false;

    for (auto it = m_lastReceiveTime.begin(); it != m_lastReceiveTime.end(); ++it) {
        if (now - it.value() < m_connectionTimeoutMs) {
            anyActive = true;
            break;
        }
    }

    if (anyActive != m_connected) {
        m_connected = anyActive;
        emit connectionStatusChanged(m_connected);
    }
}
