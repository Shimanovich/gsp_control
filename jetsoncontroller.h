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
    int trackStatus = 0;     // 0 = off
    int videoChannel = 0;    // 1 = TV, 2 = thermal
    int strobXPos = 0;       // 0 = center of frame
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

    QString playIp() const { return m_playIp; }
    quint16 playPort() const { return m_playPort; }
    QString defaultResolut() const { return m_defaultResolut; }
    int defaultBitrate() const { return m_defaultBitrate; }
    QString tvResolut() const { return m_tvResolut; }
    int tvBitrate() const { return m_tvBitrate; }
    QString tpvResolut() const { return m_tpvResolut; }
    int tpvBitrate() const { return m_tpvBitrate; }
    int tvPlayPort() const { return m_tvPlayPort; }
    int tpvPlayPort() const { return m_tpvPlayPort; }
    QString displayChannel() const { return m_displayChannel; }
    int defaultStrobeW() const { return m_defaultStrobeW; }
    int defaultStrobeH() const { return m_defaultStrobeH; }
    int defaultTrackCmd() const { return m_defaultTrackCmd; }
    int defaultVideoChannel() const { return m_defaultVideoChannel; }
    int trackButton() const { return m_trackButton; }
    TrackingParams trackingParams() const { return m_pid; }
    void setTrackingParams(const TrackingParams& params);
    bool saveTrackingParams(const QString& iniPath = QString()) const;
    CaptState lastCaptState() const { return m_capt; }

    bool sendPlay(const QString& playIp, int playPort, const QString& channel = QStringLiteral("TV"));
    bool sendStop(const QString& channel = QStringLiteral("TV"));
    bool sendSet(int bitrate, const QString& resolut, const QString& channel = QStringLiteral("TV"));
    bool sendTrackSet(int trackCmd, int videoChannel, int strobX, int strobY, int strobW, int strobH);
    bool sendStrobePos(int strobX, int strobY);
    bool sendPidSet();
    bool sendGetSet();
    bool sendHwStatus();
    bool sendZoomIn();
    bool sendZoomOut();
    bool sendZoomStop();
    bool sendZoomPos();


signals:
    void mdplStatus(const QString& stat);
    void hwStatusUpdated(const JetsonHwStatus& st);
    void opticalZoomUpdated(double magnification);
    void captAck(const QString& stat);
    void captStateUpdated(CaptState state);
    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);

private slots:
    void onReadyRead();
    void onPollGetSet();
    void onPollHwStatus();

private:
    bool sendPacket(const QByteArray& packet);
    bool sendMdplCommand(const char* command);
    void handlePacket(const QByteArray& packet);
    QString detectLocalIp() const;
    QByteArray pidInvJsonFields() const;

    QUdpSocket* m_socket = nullptr;
    QTimer* m_pollTimer = nullptr;
    QTimer* m_statusTimer = nullptr;

    QHostAddress m_remoteAddress;
    quint16 m_sendPort = 5700;
    quint16 m_listenPort = 5704;

    QString m_playIp;
    quint16 m_playPort = 5400;
    QString m_defaultResolut = QStringLiteral("HD");
    int m_defaultBitrate = 2500000;
    QString m_tvResolut = QStringLiteral("FHD");
    int m_tvBitrate = 6000000;
    QString m_tpvResolut = QStringLiteral("SD1");
    int m_tpvBitrate = 4000000;
    int m_tvPlayPort = 5400;
    int m_tpvPlayPort = 5402;
    QString m_displayChannel = QStringLiteral("TV");
    int m_defaultStrobeW = 64;
    int m_defaultStrobeH = 64;
    int m_defaultTrackCmd = 1;
    int m_defaultVideoChannel = 1;
    int m_trackButton = 4;
    int m_pollIntervalMs = 200;
    int m_statusPollIntervalMs = 5000;

    TrackingParams m_pid;
    QString m_iniPath;
    CaptState m_capt;
    bool m_started = false;
};

#endif // JETSONCONTROLLER_H
