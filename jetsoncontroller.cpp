#include "jetsoncontroller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkInterface>
#include <QSettings>
#include <QDebug>

JetsonController::JetsonController(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &JetsonController::onReadyRead);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &JetsonController::onPollGetSet);
}

JetsonController::~JetsonController()
{
    stop();
}

bool JetsonController::loadSettings(const QString& iniPath)
{
    QSettings s(iniPath, QSettings::IniFormat);

    const QString ip = s.value("Jetson/ip", "192.168.137.2").toString();
    m_remoteAddress = QHostAddress(ip);
    m_sendPort   = static_cast<quint16>(s.value("Jetson/send_port", 5700).toUInt());
    m_listenPort = static_cast<quint16>(s.value("Jetson/listen_port", 5704).toUInt());

    m_playPort = static_cast<quint16>(s.value("Jetson/play_port",
                                     s.value("Video/port", 5400)).toUInt());
    m_playIp = s.value("Jetson/play_ip").toString();
    if (m_playIp.isEmpty())
        m_playIp = detectLocalIp();

    m_defaultResolut = s.value("Jetson/resolut", "HD").toString();
    m_defaultBitrate = s.value("Jetson/bitrate", 2500000).toInt();

    m_defaultStrobeW = s.value("Tracking/strobe_x_sz", 64).toInt();
    m_defaultStrobeH = s.value("Tracking/strobe_y_sz", 64).toInt();
    m_defaultTrackCmd = s.value("Tracking/track_cmd", 1).toInt();
    m_defaultVideoChannel = s.value("Tracking/video_channel", 1).toInt();
    m_trackButton = s.value("Joystick/button_track", 4).toInt();
    m_pollIntervalMs = s.value("Tracking/poll_interval_ms", 200).toInt();

    m_pid.pidXp = s.value("Tracking/pid_x_p", 0.0).toFloat();
    m_pid.pidXi = s.value("Tracking/pid_x_i", 0.0).toFloat();
    m_pid.pidXd = s.value("Tracking/pid_x_d", 0.0).toFloat();
    m_pid.pidYp = s.value("Tracking/pid_y_p", 0.0).toFloat();
    m_pid.pidYi = s.value("Tracking/pid_y_i", 0.0).toFloat();
    m_pid.pidYd = s.value("Tracking/pid_y_d", 0.0).toFloat();
    m_pid.invAz = s.value("Tracking/inv_az", 0).toInt();
    m_pid.invEl = s.value("Tracking/inv_el", 0).toInt();

    if (m_remoteAddress.isNull()) {
        emit errorOccurred(QStringLiteral("Jetson: invalid IP address"));
        return false;
    }
    return true;
}

QString JetsonController::detectLocalIp() const
{
    const quint32 peer = m_remoteAddress.toIPv4Address();
    QString fallback;

    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;

        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString ip = e.ip().toString();
            if (fallback.isEmpty())
                fallback = ip;

            const quint32 mask = e.netmask().toIPv4Address();
            if (mask != 0 && (e.ip().toIPv4Address() & mask) == (peer & mask))
                return ip;
        }
    }
    return fallback.isEmpty() ? QStringLiteral("192.168.137.100") : fallback;
}

bool JetsonController::start()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();

    if (!m_socket->bind(QHostAddress::AnyIPv4, m_listenPort)) {
        emit errorOccurred(QString("Jetson: failed to bind UDP %1: %2")
                               .arg(m_listenPort).arg(m_socket->errorString()));
        return false;
    }

    m_started = true;
    if (m_pollIntervalMs > 0)
        m_pollTimer->start(m_pollIntervalMs);

    qDebug() << "Jetson JEP listening on" << m_listenPort
             << "sending to" << m_remoteAddress.toString() << m_sendPort
             << "play target" << m_playIp << m_playPort;

    emit connectionStatusChanged(true);
    sendGetSet();
    return true;
}

void JetsonController::stop()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_socket)
        m_socket->close();
    m_started = false;
    m_capt = CaptState{};
    emit captStateUpdated(m_capt);
    emit connectionStatusChanged(false);
}

bool JetsonController::isStarted() const
{
    return m_started;
}

bool JetsonController::sendPacket(const QByteArray& packet)
{
    if (!m_started || !m_socket || packet.isEmpty())
        return false;
    const qint64 n = m_socket->writeDatagram(packet, m_remoteAddress, m_sendPort);
    return n == packet.size();
}

bool JetsonController::sendPlay(const QString& playIp, int playPort)
{
    QJsonObject o;
    o.insert(QStringLiteral("command"), QStringLiteral("play"));
    o.insert(QStringLiteral("play_ip"), playIp);
    o.insert(QStringLiteral("play_port"), playPort);
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return sendPacket(JEPProtocol::pack(json, JEP_HD::MDPL));
}

bool JetsonController::sendStop()
{
    const QByteArray json = QByteArrayLiteral("{\"command\":\"stop\"}");
    return sendPacket(JEPProtocol::pack(json, JEP_HD::MDPL));
}

bool JetsonController::sendSet(int bitrate, const QString& resolut)
{
    QJsonObject o;
    o.insert(QStringLiteral("command"), QStringLiteral("set"));
    o.insert(QStringLiteral("bitrate"), bitrate);
    o.insert(QStringLiteral("resolut"), resolut);
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return sendPacket(JEPProtocol::pack(json, JEP_HD::MDPL));
}

bool JetsonController::sendTrackSet(int trackCmd, int videoChannel,
                                    int strobX, int strobY, int strobW, int strobH)
{
    QJsonObject o;
    o.insert(QStringLiteral("INV_EL"), m_pid.invEl);
    o.insert(QStringLiteral("INV_AZ"), m_pid.invAz);
    o.insert(QStringLiteral("PID_Y_D"), static_cast<double>(m_pid.pidYd));
    o.insert(QStringLiteral("PID_Y_I"), static_cast<double>(m_pid.pidYi));
    o.insert(QStringLiteral("PID_Y_P"), static_cast<double>(m_pid.pidYp));
    o.insert(QStringLiteral("PID_X_D"), static_cast<double>(m_pid.pidXd));
    o.insert(QStringLiteral("PID_X_I"), static_cast<double>(m_pid.pidXi));
    o.insert(QStringLiteral("PID_X_P"), static_cast<double>(m_pid.pidXp));
    o.insert(QStringLiteral("STROB_Y_SZ"), strobH);
    o.insert(QStringLiteral("STROB_X_SZ"), strobW);
    o.insert(QStringLiteral("STROB_Y_POS"), strobY);
    o.insert(QStringLiteral("STROB_X_POS"), strobX);
    o.insert(QStringLiteral("VIDEO_CHANEL"), videoChannel);
    o.insert(QStringLiteral("TRACK_CMD"), trackCmd);
    o.insert(QStringLiteral("command"), QStringLiteral("set"));

    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const bool ok = sendPacket(JEPProtocol::pack(json, JEP_HD::CAPT));
    if (ok)
        QTimer::singleShot(80, this, &JetsonController::sendGetSet);
    return ok;
}

bool JetsonController::sendGetSet()
{
    const QByteArray json = QByteArrayLiteral("{\"command\":\"get_set\"}");
    return sendPacket(JEPProtocol::pack(json, JEP_HD::CAPT));
}

void JetsonController::onPollGetSet()
{
    if (m_started)
        sendGetSet();
}

void JetsonController::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(datagram.data(), datagram.size());
        handlePacket(datagram);
    }
}

void JetsonController::handlePacket(const QByteArray& packet)
{
    if (!JEPProtocol::validate(packet)) {
        qWarning() << "Jetson: invalid JEP packet, size" << packet.size();
        return;
    }

    const auto unpacked = JEPProtocol::unpack(packet);
    if (unpacked.second.isEmpty())
        return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(unpacked.second, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Jetson: JSON parse error" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    const QString stat = obj.value(QStringLiteral("stat")).toString();

    if (unpacked.first == JEP_HD::MDPL) {
        emit mdplStatus(stat);
        return;
    }

    if (unpacked.first != JEP_HD::CAPT)
        return;

    if (stat == QLatin1String("set")) {
        emit captAck(stat);
        return;
    }

    if (stat == QLatin1String("get_set") || obj.contains(QStringLiteral("TRACK_STATUS"))) {
        CaptState st;
        st.trackStatus  = obj.value(QStringLiteral("TRACK_STATUS")).toInt();
        st.videoChannel = obj.value(QStringLiteral("VIDEO_CHANEL")).toInt();
        st.strobXPos    = obj.value(QStringLiteral("STROB_X_POS")).toInt();
        st.strobYPos    = obj.value(QStringLiteral("STROB_Y_POS")).toInt();
        st.strobXSz     = obj.value(QStringLiteral("STROB_X_SZ")).toInt();
        st.strobYSz     = obj.value(QStringLiteral("STROB_Y_SZ")).toInt();
        m_capt = st;
        emit captStateUpdated(st);
        emit captAck(stat.isEmpty() ? QStringLiteral("get_set") : stat);
    }
}
