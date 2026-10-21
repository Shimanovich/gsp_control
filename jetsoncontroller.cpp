#include "jetsoncontroller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QNetworkInterface>
#include <QSettings>
#include <QDebug>

namespace {

QByteArray jsonPidNumber(float v)
{
    if (v == 0.f)
        return QByteArrayLiteral("0.0");
    QByteArray s = QByteArray::number(static_cast<double>(v), 'g', 8);
    if (!s.contains('.') && !s.contains('e') && !s.contains('E'))
        s += ".0";
    return s;
}

} // namespace

JetsonController::JetsonController(QObject *parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &JetsonController::onReadyRead);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &JetsonController::onPollGetSet);
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &JetsonController::onPollHwStatus);
    m_devicePollTimer = new QTimer(this);
    connect(m_devicePollTimer, &QTimer::timeout, this, &JetsonController::onPollDeviceStatus);
}

JetsonController::~JetsonController()
{
    stop();
}

bool JetsonController::loadSettings(const QString& iniPath)
{
    m_iniPath = iniPath;
    QSettings s(iniPath, QSettings::IniFormat);

    const QString ip = s.value("Jetson/ip", "192.168.137.2").toString();
    m_remoteAddress = QHostAddress(ip);
    m_sendPort   = static_cast<quint16>(s.value("Jetson/send_port", 5700).toUInt());
    m_listenPort = static_cast<quint16>(s.value("Jetson/listen_port", 5704).toUInt());

    m_playPort = static_cast<quint16>(s.value("Jetson/play_port", s.value("Video/port", 5400)).toUInt());
    m_playIp = s.value("Jetson/play_ip").toString();
    if (m_playIp.isEmpty())
        m_playIp = detectLocalIp();

    m_defaultResolut = s.value("Jetson/resolut", "HD").toString();
    m_defaultBitrate = s.value("Jetson/bitrate", 2500000).toInt();
    m_pollIntervalMs = s.value("Tracking/poll_interval_ms", 200).toInt();
    m_statusPollIntervalMs = s.value("Jetson/status_poll_interval_ms", 5000).toInt();
    m_devicePollIntervalMs = s.value("Polling/device_poll_ms", 200).toInt();

    m_pid.pidXp = s.value("Tracking/pid_x_p", 0.04).toFloat();
    m_pid.pidXi = s.value("Tracking/pid_x_i", 0.04).toFloat();
    m_pid.pidXd = s.value("Tracking/pid_x_d", 0.0).toFloat();
    m_pid.pidYp = s.value("Tracking/pid_y_p", 0.04).toFloat();
    m_pid.pidYi = s.value("Tracking/pid_y_i", 0.04).toFloat();
    m_pid.pidYd = s.value("Tracking/pid_y_d", 0.0).toFloat();
    auto loadInv = [](const QVariant& v, int def) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (!ok) return def;
        if (d < 0.0) return -1;
        if (d > 0.0) return 1;
        return 0;
    };
    m_pid.invAz = loadInv(s.value("Tracking/inv_az", 1.0), 1);
    m_pid.invEl = loadInv(s.value("Tracking/inv_el", 1.0), 1);

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
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QString ip = e.ip().toString();
            if (fallback.isEmpty()) fallback = ip;
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
        emit errorOccurred(QString("Jetson: failed to bind UDP %1: %2").arg(m_listenPort).arg(m_socket->errorString()));
        return false;
    }
    m_started = true;
    if (m_pollIntervalMs > 0) m_pollTimer->start(m_pollIntervalMs);
    if (m_statusPollIntervalMs > 0) {
        m_statusTimer->start(m_statusPollIntervalMs);
        sendHwStatus();
    }
    if (m_devicePollIntervalMs > 0)
        m_devicePollTimer->start(m_devicePollIntervalMs);
    emit connectionStatusChanged(true);
    sendGetSet();
    return true;
}

void JetsonController::stop()
{
    if (m_pollTimer) m_pollTimer->stop();
    if (m_statusTimer) m_statusTimer->stop();
    if (m_devicePollTimer) m_devicePollTimer->stop();
    if (m_socket) m_socket->close();
    m_started = false;
    m_capt = CaptState{};
    emit captStateUpdated(m_capt);
    emit connectionStatusChanged(false);
}

bool JetsonController::isStarted() const { return m_started; }

bool JetsonController::sendPacket(const QByteArray& packet)
{
    if (!m_started || !m_socket || packet.isEmpty()) return false;
    return m_socket->writeDatagram(packet, m_remoteAddress, m_sendPort) == packet.size();
}

// MDPL
bool JetsonController::sendPlay(const QString& playIp, int playPort)
{
    QJsonObject o; o["command"] = "play"; o["play_ip"] = playIp; o["play_port"] = playPort;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::MDPL));
}
bool JetsonController::sendStop()
{
    return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"stop\"}"), JEP_HD::MDPL));
}
bool JetsonController::sendSet(int bitrate, const QString& resolut)
{
    QJsonObject o; o["command"] = "set"; o["bitrate"] = bitrate; o["resolut"] = resolut;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::MDPL));
}
bool JetsonController::sendHwStatus()
{
    return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"status\"}"), JEP_HD::MDPL));
}

// CAPT (existing logic kept simplified)
QByteArray JetsonController::pidInvJsonFields() const
{
    return QByteArrayLiteral("\"PID_X_P\":") + jsonPidNumber(m_pid.pidXp) +
           QByteArrayLiteral(",\"PID_X_I\":") + jsonPidNumber(m_pid.pidXi) +
           QByteArrayLiteral(",\"PID_X_D\":") + jsonPidNumber(m_pid.pidXd) +
           QByteArrayLiteral(",\"PID_Y_P\":") + jsonPidNumber(m_pid.pidYp) +
           QByteArrayLiteral(",\"PID_Y_I\":") + jsonPidNumber(m_pid.pidYi) +
           QByteArrayLiteral(",\"PID_Y_D\":") + jsonPidNumber(m_pid.pidYd) +
           QByteArrayLiteral(",\"INV_AZ\":") + QByteArray::number(m_pid.invAz) +
           QByteArrayLiteral(",\"INV_EL\":") + QByteArray::number(m_pid.invEl);
}
bool JetsonController::sendTrackSet(int trackCmd, int videoChannel, int strobX, int strobY, int strobW, int strobH)
{
    const QByteArray json = QByteArrayLiteral("{\"command\":\"set\","
                              "\"TRACK_CMD\":") + QByteArray::number(trackCmd) +
            QByteArrayLiteral(",\"VIDEO_CHANEL\":") + QByteArray::number(videoChannel) +
            QByteArrayLiteral(",\"STROB_X_POS\":") + QByteArray::number(strobX) +
            QByteArrayLiteral(",\"STROB_Y_POS\":") + QByteArray::number(strobY) +
            QByteArrayLiteral(",\"STROB_X_SZ\":") + QByteArray::number(strobW) +
            QByteArrayLiteral(",\"STROB_Y_SZ\":") + QByteArray::number(strobH) +
            QByteArrayLiteral(",") + pidInvJsonFields() + QByteArrayLiteral("}");
    return sendPacket(JEPProtocol::pack(json, JEP_HD::CAPT));
}
bool JetsonController::sendPidSet() { /* simplified */ return true; }
bool JetsonController::sendGetSet()
{
    return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"get_set\"}"), JEP_HD::CAPT));
}
void JetsonController::onPollGetSet() { if (m_started) sendGetSet(); }
void JetsonController::onPollHwStatus() { if (m_started) sendHwStatus(); }
void JetsonController::onPollDeviceStatus()
{
    if (!m_started) return;
    sendCamStatus();
    sendGyroStatus();
    sendRangeStatus();
}

// CAMR
bool JetsonController::sendCamZoomIn() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"zoom_in\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamZoomOut() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"zoom_out\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamZoomStop() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"zoom_stop\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamZoomPos(int index) {
    QJsonObject o; o["command"] = "zoom_pos"; o["index"] = index;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::CAMR));
}
bool JetsonController::sendCamAutofocus() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"autofocus\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamFocusInfinity() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"focus_infinity\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamBrightnessUp() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"brightness_up\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamBrightnessDown() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"brightness_down\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamAeBright() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"ae_bright\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamTvFps25() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"tv_fps25\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamTvFps30() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"tv_fps30\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamTvPwrReset() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"tv_pwr_reset\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamZoomInquiry() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"zoom_inquiry\"}"), JEP_HD::CAMR)); }
bool JetsonController::sendCamStatus() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"status\"}"), JEP_HD::CAMR)); }

// GYRO
bool JetsonController::sendGyroSpeed(float yaw, float pitch) {
    QJsonObject o; o["command"] = "speed"; o["yaw"] = yaw; o["pitch"] = pitch;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::GYRO));
}
bool JetsonController::sendGyroHeading(float yaw, float pitch) {
    QJsonObject o; o["command"] = "heading"; o["yaw"] = yaw; o["pitch"] = pitch;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::GYRO));
}
bool JetsonController::sendGyroStabNull(float yaw, float pitch) {
    QJsonObject o; o["command"] = "stab_null"; o["yaw"] = yaw; o["pitch"] = pitch;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::GYRO));
}
bool JetsonController::sendGyroAngle(float yaw, float pitch) {
    QJsonObject o; o["command"] = "angle"; o["yaw"] = yaw; o["pitch"] = pitch;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::GYRO));
}
bool JetsonController::sendGyroHome() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"home\"}"), JEP_HD::GYRO)); }
bool JetsonController::sendGyroMotors(bool on) {
    QJsonObject o; o["command"] = "motors"; o["on"] = on;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::GYRO));
}
bool JetsonController::sendGyroReset() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"reset\"}"), JEP_HD::GYRO)); }
bool JetsonController::sendGyroStatus() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"status\"}"), JEP_HD::GYRO)); }

// RNGF
bool JetsonController::sendRangeShoot() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"shoot\"}"), JEP_HD::RNGF)); }
bool JetsonController::sendRangeStop() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"stop\"}"), JEP_HD::RNGF)); }
bool JetsonController::sendRangeContinuous(int periodMs) {
    QJsonObject o; o["command"] = "continuous"; o["period_ms"] = periodMs;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::RNGF));
}
bool JetsonController::sendRangeSelftest() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"selftest\"}"), JEP_HD::RNGF)); }
bool JetsonController::sendRangeSetMinDist(int meters) {
    QJsonObject o; o["command"] = "set_min_dist"; o["meters"] = meters;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::RNGF));
}
bool JetsonController::sendRangeSetTargetMode(int mode) {
    QJsonObject o; o["command"] = "set_target_mode"; o["mode"] = mode;
    return sendPacket(JEPProtocol::pack(QJsonDocument(o).toJson(QJsonDocument::Compact), JEP_HD::RNGF));
}
bool JetsonController::sendRangeStatus() { return sendPacket(JEPProtocol::pack(QByteArrayLiteral("{\"command\":\"status\"}"), JEP_HD::RNGF)); }

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
    if (!JEPProtocol::validate(packet)) return;
    const auto unpacked = JEPProtocol::unpack(packet);
    if (unpacked.second.isEmpty()) return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(unpacked.second, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString stat = obj.value("stat").toString();

    if (unpacked.first == JEP_HD::MDPL) {
        if (stat == "status" || obj.contains("temp_cpu")) {
            JetsonHwStatus st;
            st.cpuUse = obj.value("cpu_use").toDouble();
            st.tempCpu = obj.value("temp_cpu").toDouble();
            st.tempGpu = obj.value("temp_gpu").toDouble();
            st.freeRam = static_cast<qint64>(obj.value("free_ram").toDouble());
            st.freeFlash = static_cast<qint64>(obj.value("free_flash").toDouble());
            emit hwStatusUpdated(st);
        } else {
            emit mdplStatus(stat);
        }
        return;
    }
    if (unpacked.first == JEP_HD::CAPT) {
        if (stat == "get_set" || obj.contains("TRACK_STATUS")) {
            CaptState st;
            st.trackStatus = obj.value("TRACK_STATUS").toInt();
            st.videoChannel = obj.value("VIDEO_CHANEL").toInt();
            st.strobXPos = obj.value("STROB_X_POS").toInt();
            st.strobYPos = obj.value("STROB_Y_POS").toInt();
            st.strobXSz = obj.value("STROB_X_SZ").toInt();
            st.strobYSz = obj.value("STROB_Y_SZ").toInt();
            m_capt = st;
            emit captStateUpdated(st);
        }
        emit captAck(stat);
        return;
    }
    if (unpacked.first == JEP_HD::CAMR) {
        if (stat == "zoom_pos") emit cameraZoomUpdated(obj.value("data").toDouble());
        else if (stat == "status") emit cameraStatusUpdated(obj.value("zoom_pos").toInt(), obj.value("magnification").toDouble(), obj.value("fps").toInt());
        return;
    }
    if (unpacked.first == JEP_HD::GYRO) {
        if (stat == "angles") emit gyroAnglesUpdated(obj.value("yaw").toDouble(), obj.value("pitch").toDouble());
        else if (stat == "temps") emit gyroTempsUpdated(obj.value("imu").toInt(), obj.value("frame_imu").toInt());
        else if (stat == "status") emit gyroStatusUpdated(obj.value("motors_on").toBool(), obj.value("control_ready").toBool(), obj.value("yaw").toDouble(), obj.value("pitch").toDouble());
        return;
    }
    if (unpacked.first == JEP_HD::RNGF) {
        if (stat == "measurement") emit rangeMeasurementReceived(obj.value("dist1_m").toDouble(), obj.value("dist2_m").toDouble(), obj.value("dist3_m").toDouble(), obj.value("status_byte").toInt());
        else if (stat == "status") emit rangeStatusUpdated(obj.value("last_dist_m").toDouble(), obj.value("laser_ok").toBool());
        return;
    }
}
