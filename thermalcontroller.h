#ifndef THERMALCONTROLLER_H
#define THERMALCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>

class UdpCommunicator;

class ThermalController : public QObject
{
    Q_OBJECT
public:
    explicit ThermalController(UdpCommunicator* udp, QObject* parent = nullptr);

    bool loadSettings(const QString& iniPath);
    void startPolling();
    void stopPolling();

    // Image
    void setPalette(int code);
    void setTone(int code);          // 0 warm, 1 cold, 2 green hot
    void setImageMode(int code);     // 0 soft, 1 standard, 3 highlight
    void setDenoise(int level);      // 1..3
    void setDetail(int level);       // 1..4
    void setBrightness(int level);   // 1..5
    void setContrast(int level);     // 1..5

    // Compensation
    void shutterCompensation();
    void sceneCompensation();

    void queryStatus();

signals:
    void focalTempUpdated(double celsius);
    void error(const QString& msg);

private slots:
    void handleIncomingPacket(uint8_t sourceId, const QByteArray& payload);

private:
    QByteArray buildPacket(const QByteArray& body) const;
    bool send(const QByteArray& body);

    UdpCommunicator* m_udp = nullptr;
    uint8_t m_targetId = 12;
    QTimer* m_pollTimer = nullptr;
};

#endif
