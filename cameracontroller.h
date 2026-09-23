#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QSettings>
#include <QList>
#include <QVector>

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
    void zoomStop();

    void setZoomPosition(int index);           // fixed positions from config


    void setZoomPosition_prev();
    void setZoomPosition_next();


    void autofocus();
    void focusInfinity();
    void brightnessUp();
    void brightnessDown();
    void setAeBrightMode();

    void startZoomPolling();
    void stopZoomPolling();

    void queryHdDelay();
    void setHdDelay(bool enabled);
    bool hdDelayEnabled() const { return m_hdDelayEnabled; }

    float currentMagnification() const { return m_currentMagnification; }

signals:
    void zoomPositionUpdated(float magnification);
    void hdDelayUpdated(bool enabled);
    void error(const QString& msg);

private slots:

    void handleIncomingPacket(uint8_t sourceId, const QByteArray& payload);

    void pollZoomPosition();

private:
    UdpCommunicator* m_udp = nullptr;
    uint8_t m_targetId = 11;

    QList<QByteArray> m_zoomDirectCommands;
    QVector<uint16_t> m_zoomPresetPos;
    QList<QString> m_zoomNames;
    int m_currentZoomIndex = 0;
    uint16_t m_currentZoomPos = 0;
    bool m_haveZoomPos = false;

    int nearestPresetIndex(uint16_t pos) const;
    int stepPresetIndex(int direction) const;
    QVector<uint16_t> m_zoomCurvePos;
    QVector<float> m_zoomCurveMag;
    float m_currentMagnification = 1.0f;

    float magnificationFromPosition(uint16_t zoomPos) const;

    QTimer* m_zoomPollTimer = nullptr;
    bool m_hdDelayEnabled = false;
    bool m_expectHdDelayReply = false;
    static constexpr uint8_t kRegHdDelay = 0x75;

    QByteArray buildViscaCommand(const QByteArray& cmd);
    void sendVisca(const QByteArray& cmd);
};

#endif // CAMERACONTROLLER_H