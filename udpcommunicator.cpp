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

    m_sendPort   = settings.value("Network/send_port", 5000).toUInt();
    m_listenPort = settings.value("Network/listen_port", m_sendPort).toUInt(); // если не задан — используем send_port

    m_connectionTimeoutMs = settings.value("Network/connection_timeout_ms", 5000).toInt();

    if (m_remoteAddress.isNull()) {
        emit errorOccurred("Invalid IP address");
        return false;
    }
    return true;
}

bool UdpCommunicator::start()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();

    // Привязываемся именно к listen_port
    if (!m_socket->bind(QHostAddress::AnyIPv4, m_listenPort)) {
        emit errorOccurred(QString("Failed to bind to port %1: %2")
                               .arg(m_listenPort).arg(m_socket->errorString()));
        return false;
    }

    qDebug() << "UDP listening on port" << m_listenPort
             << ", sending to port" << m_sendPort;

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

    if (targetId !=0xff)
    {
        packet.append(static_cast<char>(targetId));
    }
    packet.append(payload);

    // Отправляем на m_sendPort
    qint64 bytes = m_socket->writeDatagram(packet, m_remoteAddress, m_sendPort);
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

        int sz = datagram.size();

        if (sz < 1) continue;

        QByteArray payload;
        uint8_t sourceId = static_cast<uint8_t>(datagram.at(0));
        if (sourceId !=0x3e)
        {
             payload = datagram.mid(1,datagram.size()-1);
        }
        else
        {
            sourceId = 0xff;
            payload = datagram.mid(0,datagram.size());
        }

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
