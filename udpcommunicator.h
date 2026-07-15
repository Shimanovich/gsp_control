#ifndef UDPCOMMUNICATOR_H
#define UDPCOMMUNICATOR_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QSettings>
#include <QMap>

class UdpCommunicator : public QObject
{
    Q_OBJECT
public:
    explicit UdpCommunicator(QObject *parent = nullptr);
    ~UdpCommunicator();

    bool loadSettings(const QString& iniPath);
    bool start();
    void stop();

    // Send packet: target_id + payload
    bool sendPacket(uint8_t targetId, const QByteArray& payload);

    // Connection status
    bool isConnected() const;

signals:
    void packetReceived(uint8_t sourceId, const QByteArray& payload);
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString& error);

private slots:
    void onReadyRead();
    void checkConnectionTimeout();

private:
    QUdpSocket* m_socket = nullptr;
    QHostAddress m_remoteAddress;
    quint16 m_port = 5000;
    int m_connectionTimeoutMs = 5000;

    QTimer* m_timeoutTimer = nullptr;
    bool m_connected = false;

    // Last time we received valid packet from each source
    QMap<uint8_t, qint64> m_lastReceiveTime;

    void updateConnectionStatus();
};

#endif // UDPCOMMUNICATOR_H