#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QSettings>
#include <QList>

class UdpCommunicator;

class CameraController : public QObject
{
    Q_OBJECT
public:
    explicit CameraController(UdpCommunicator* udp, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);

    // Commands
    void zoomIn();
    void zoomOut();
    void setZoomPosition(int index);           // fixed positions from config
    void autofocus();
    void focusInfinity();
    void brightnessUp();
    void brightnessDown();

    void startZoomPolling();
    void stopZoomPolling();

signals:
    void zoomPositionUpdated(float position);  // 0.0 .. 1.0 normalized
    void error(const QString& msg);

private slots:

    void handleIncomingPacket(uint8_t sourceId, const QByteArray& payload);

    void pollZoomPosition();

private:
    UdpCommunicator* m_udp = nullptr;
    uint8_t m_targetId = 11;

    QList<QByteArray> m_zoomDirectCommands;
    QList<QString> m_zoomNames;
    int m_currentZoomIndex = 0;

    QTimer* m_zoomPollTimer = nullptr;

    QByteArray buildViscaCommand(const QByteArray& cmd);
    void sendVisca(const QByteArray& cmd);
};

#endif // CAMERACONTROLLER_H