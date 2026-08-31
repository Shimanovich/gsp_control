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

struct TrackingParams
{
    float pidXp = 0.0f;
    float pidXi = 0.0f;
    float pidXd = 0.0f;
    float pidYp = 0.0f;
    float pidYi = 0.0f;
    float pidYd = 0.0f;
    int invAz = 0;
    int invEl = 0;
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
    int defaultStrobeW() const { return m_defaultStrobeW; }
    int defaultStrobeH() const { return m_defaultStrobeH; }
    int defaultTrackCmd() const { return m_defaultTrackCmd; }
    int defaultVideoChannel() const { return m_defaultVideoChannel; }
    int trackButton() const { return m_trackButton; }
    TrackingParams trackingParams() const { return m_pid; }
    CaptState lastCaptState() const { return m_capt; }

    bool sendPlay(const QString& playIp, int playPort);
    bool sendStop();
    bool sendSet(int bitrate, const QString& resolut);
    bool sendTrackSet(int trackCmd, int videoChannel, int strobX, int strobY, int strobW, int strobH);
    bool sendGetSet();

signals:
    void mdplStatus(const QString& stat);
    void captAck(const QString& stat);
    void captStateUpdated(CaptState state);
    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);

private slots:
    void onReadyRead();
    void onPollGetSet();

private:
    bool sendPacket(const QByteArray& packet);
    void handlePacket(const QByteArray& packet);
    QString detectLocalIp() const;

    QUdpSocket* m_socket = nullptr;
    QTimer* m_pollTimer = nullptr;

    QHostAddress m_remoteAddress;
    quint16 m_sendPort = 5700;
    quint16 m_listenPort = 5704;

    QString m_playIp;
    quint16 m_playPort = 5400;
    QString m_defaultResolut = QStringLiteral("HD");
    int m_defaultBitrate = 2500000;
    int m_defaultStrobeW = 64;
    int m_defaultStrobeH = 64;
    int m_defaultTrackCmd = 1;
    int m_defaultVideoChannel = 1;
    int m_trackButton = 4;
    int m_pollIntervalMs = 200;

    TrackingParams m_pid;
    CaptState m_capt;
    bool m_started = false;
};

#endif // JETSONCONTROLLER_H
