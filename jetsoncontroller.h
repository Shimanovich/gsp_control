#ifndef JETSONCONTROLLER_H
#define JETSONCONTROLLER_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QString>

#include "jepprotocol.h"

struct CaptState
{
    int trackStatus = 0;
    int videoChannel = 0;
    int strobXPos = 0;
    int strobYPos = 0;
    int strobXSz = 0;
    int strobYSz = 0;
};

struct JetsonHwStatus
{
    double cpuUse = 0.0;
    double tempCpu = 0.0;
    double tempGpu = 0.0;
    qint64 freeRam = 0;
    qint64 freeFlash = 0;
};

struct TrackingParams
{
    float pidXp = 0.0f;
    float pidXi = 0.0f;
    float pidXd = 0.0f;
    float pidYp = 0.0f;
    float pidYi = 0.0f;
    float pidYd = 0.0f;
    int invAz = 1;
    int invEl = 1;
};

class JetsonController : public QObject
{
    Q_OBJECT
public:
    explicit JetsonController(QObject *parent = nullptr);
    ~JetsonController() override;

    bool loadSettings(const QString& iniPath);
    bool start();
    void stop();
    bool isStarted() const;

    // MDPL
    bool sendPlay(const QString& playIp, int playPort);
    bool sendStop();
    bool sendSet(int bitrate, const QString& resolut);
    bool sendHwStatus();

    // CAPT
    bool sendTrackSet(int trackCmd, int videoChannel, int strobX, int strobY, int strobW, int strobH);
    bool sendPidSet();
    bool sendGetSet();

    // CAMR
    bool sendCamZoomIn();
    bool sendCamZoomOut();
    bool sendCamZoomStop();
    bool sendCamZoomPos(int index);
    bool sendCamAutofocus();
    bool sendCamFocusInfinity();
    bool sendCamBrightnessUp();
    bool sendCamBrightnessDown();
    bool sendCamAeBright();
    bool sendCamTvFps25();
    bool sendCamTvFps30();
    bool sendCamTvPwrReset();
    bool sendCamZoomInquiry();
    bool sendCamStatus();

    // GYRO
    bool sendGyroSpeed(float yaw, float pitch);
    bool sendGyroHeading(float yaw, float pitch);
    bool sendGyroStabNull(float yaw, float pitch);
    bool sendGyroAngle(float yaw, float pitch);
    bool sendGyroHome();
    bool sendGyroMotors(bool on);
    bool sendGyroReset();
    bool sendGyroStatus();

    // RNGF
    bool sendRangeShoot();
    bool sendRangeStop();
    bool sendRangeContinuous(int periodMs);
    bool sendRangeSelftest();
    bool sendRangeSetMinDist(int meters);
    bool sendRangeSetTargetMode(int mode);
    bool sendRangeStatus();

    TrackingParams trackingParams() const { return m_pid; }
    void setTrackingParams(const TrackingParams& params);
    bool saveTrackingParams(const QString& iniPath = QString()) const;
    CaptState lastCaptState() const { return m_capt; }

signals:
    void mdplStatus(const QString& stat);
    void hwStatusUpdated(const JetsonHwStatus& st);
    void captAck(const QString& stat);
    void captStateUpdated(CaptState state);

    void cameraZoomUpdated(float magnification);
    void cameraStatusUpdated(int zoomPos, float magnification, int fps);

    void gyroAnglesUpdated(float yaw, float pitch);
    void gyroTempsUpdated(int imu, int frameImu);
    void gyroStatusUpdated(bool motorsOn, bool controlReady, float yaw, float pitch);

    void rangeMeasurementReceived(float dist1, float dist2, float dist3, int statusByte);
    void rangeStatusUpdated(float lastDist, bool laserOk);

    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);

private slots:
    void onReadyRead();
    void onPollGetSet();
    void onPollHwStatus();
    void onPollDeviceStatus();

private:
    bool sendPacket(const QByteArray& packet);
    void handlePacket(const QByteArray& packet);
    QString detectLocalIp() const;
    QByteArray pidInvJsonFields() const;

    QUdpSocket* m_socket = nullptr;
    QTimer* m_pollTimer = nullptr;
    QTimer* m_statusTimer = nullptr;
    QTimer* m_devicePollTimer = nullptr;

    QHostAddress m_remoteAddress;
    quint16 m_sendPort = 5700;
    quint16 m_listenPort = 5704;

    QString m_playIp;
    quint16 m_playPort = 5400;
    QString m_defaultResolut = QStringLiteral("HD");
    int m_defaultBitrate = 2500000;
    int m_pollIntervalMs = 200;
    int m_statusPollIntervalMs = 5000;
    int m_devicePollIntervalMs = 200;

    TrackingParams m_pid;
    QString m_iniPath;
    CaptState m_capt;
    bool m_started = false;
};

#endif // JETSONCONTROLLER_H
