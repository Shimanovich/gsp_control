#include "gyrocontroller.h"
#include "udpcommunicator.h"
#include <QDebug>
#include <QtEndian>

GyroController::GyroController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &GyroController::pollAngles);
    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    connect(m_settleTimer, &QTimer::timeout, this, &GyroController::onMotorsSettle);

    if (m_udp) {
        connect(m_udp, &UdpCommunicator::packetReceived,
                this, &GyroController::handleIncomingPacket);
    }
}

bool GyroController::loadSettings(const QString& iniPath)
{
    // Can load more params later
    return true;
}

void GyroController::setSpeed(float yawSpeed, float pitchSpeed)
{
     if (!m_motorsPowered || !m_controlReady)
         return;
     QByteArray payload = buildControlPayload(SimpleBGC::CONTROL_MODE_SPEED, yawSpeed, pitchSpeed);
     QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
     if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::goToHeadingPosition(float yawDeg, float pitchDeg)
{
    if (!m_motorsPowered || !m_controlReady)
        return;
    QByteArray payload = buildControlPayload(SimpleBGC::CONTROL_MODE_ANGLE_REL_FRAME, yawDeg, pitchDeg);
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::goToStabNullPosition(float yawDeg, float pitchDeg)
{
    if (!m_motorsPowered || !m_controlReady)
        return;
    QByteArray payload = buildControlPayload(SimpleBGC::CONTROL_MODE_ANGLE, yawDeg, pitchDeg);
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::goToZeroPosition()
{
    if (!m_motorsPowered || !m_controlReady)
        return;
    QByteArray payload;
    payload.append(static_cast<char>(18)); // MENU_CMD_HOME_POSITION
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_EXECUTE_MENU, payload);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::startAnglePolling()
{
    if (!m_pollTimer->isActive())
    {
        m_pollTimer->start(m_pollIntervalMs);
    }
}

void GyroController::stopAnglePolling()
{
    if (m_pollTimer->isActive())
    {
        m_pollTimer->stop();
    }
}

void GyroController::pollAngles()
{
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_REALTIME_DATA_4);
    if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}


void GyroController::motorOn()
{
    setMotorsPower(true);
}

void GyroController::setMotorsPower(bool on)
{
    if (!m_udp)
        return;

    if (on) {
        if (m_motorsPowered && m_controlReady)
            return;
        m_motorsPowered = true;
        m_controlReady = false;
        // CMD_MOTORS_ON по документации BaseCam сбрасывает систему в home.
        // Второй раз (меню 11) не шлём — двойной reset сбивает нуль yaw.
        m_udp->sendPacket(m_targetId, SimpleBGC::buildPacket(SimpleBGC::CMD_MOTORS_ON));
        if (m_settleTimer)
            m_settleTimer->start(1200);
        emit controlReadyChanged(false);
        return;
    }

    if (!m_motorsPowered && !m_controlReady)
        return;

    m_motorsPowered = false;
    m_controlReady = false;
    if (m_settleTimer)
        m_settleTimer->stop();

    // MODE=1 — Motors OFF safely (2.66+): не high-Z, IMU меньше теряет горизонт.
    QByteArray offMode;
    offMode.append(static_cast<char>(1));
    m_udp->sendPacket(m_targetId, SimpleBGC::buildPacket(SimpleBGC::CMD_MOTORS_OFF, offMode));
    emit controlReadyChanged(false);
}

void GyroController::onMotorsSettle()
{
    if (!m_motorsPowered)
        return;
    m_controlReady = true;
    emit controlReadyChanged(true);
}

void GyroController::resetBoard()
{
    if (!m_udp)
        return;
    // CMD_RESET #114 — полный рестарт платы, как цикл питания.
    m_udp->sendPacket(m_targetId, SimpleBGC::buildPacket(SimpleBGC::CMD_RESET));
    m_motorsPowered = true;
    m_controlReady = false;
    if (m_settleTimer)
        m_settleTimer->start(3000);
    emit controlReadyChanged(false);
}



QByteArray GyroController::buildZeroPosCmd()
{

    SimpleBGC::cmd_control_t cmdout;

    cmdout.mode 		= SimpleBGC::CONTROL_MODE_ANGLE_REL_FRAME;
    cmdout.speedPITCH 	= 0;
    cmdout.speedYAW 	= 0;
    cmdout.speedROLL  	= 0;
    cmdout.angleYAW     = 0;
    cmdout.anglePITCH   = 0;
    cmdout.angleYAW     = 0;

    QByteArray payload(reinterpret_cast<const char*>(&cmdout), sizeof(cmdout));
    return payload;
}

QByteArray GyroController::buildControlPayload(uint8_t mode, float yaw, float pitch)
{
     //QByteArray payload;
     SimpleBGC::cmd_control_ext_t cmdout;


     cmdout.mode[0] = 0;
     cmdout.data[0].angle = 0;
     cmdout.data[0].speed = 0;

     cmdout.mode[1] = mode;
     cmdout.mode[2] = mode;

     if (mode== SimpleBGC::CONTROL_MODE_SPEED)
     {
         int16_t p = qToLittleEndian<int16_t>(static_cast<int16_t>(pitch/0.1220740379));
         int16_t y = qToLittleEndian<int16_t>(static_cast<int16_t>(yaw/0.1220740379));

         cmdout.data[1].angle = 0;
         cmdout.data[1].speed = p;
         cmdout.data[2].angle = 0;
         cmdout.data[2].speed = y;
     }

     if (mode==SimpleBGC::CONTROL_MODE_ANGLE_REL_FRAME
         || mode==SimpleBGC::CONTROL_MODE_ANGLE)
     {
         int16_t p = qToLittleEndian<int16_t>(static_cast<int16_t>(pitch/0.02197265625));
         int16_t y = qToLittleEndian<int16_t>(static_cast<int16_t>(yaw/0.02197265625));
         // SPEED в MODE_ANGLE — макс. скорость набора угла (0.122 °/с).
         // 0 на части прошивок даёт «не двигаться» по оси, у yaw это видно чаще.
         const int16_t maxSpd = qToLittleEndian<int16_t>(
             static_cast<int16_t>(60.0f / 0.1220740379f));

         cmdout.data[1].angle = p;
         cmdout.data[1].speed = maxSpd;
         cmdout.data[2].angle = y;
         cmdout.data[2].speed = maxSpd;
     }


     QByteArray payload(reinterpret_cast<const char*>(&cmdout), sizeof(cmdout));

     return payload;


    return payload;
}

void GyroController::handleIncomingPacket(uint8_t sourceId, const QByteArray& payload)
{
    if (sourceId != m_targetId || payload.isEmpty()) return;

    uint8_t cmd = static_cast<uint8_t>(payload.at(1));

    if (cmd == SimpleBGC::CMD_REALTIME_DATA_4) {

        const SimpleBGC::SerialCommand_t *msg = reinterpret_cast<const SimpleBGC::SerialCommand_t*>(payload.constData());
        float pitch = ((float)msg->rtdata.rotor_angle[1]*0.02197265625);
        float yaw = ((float)msg->rtdata.rotor_angle[2]*0.02197265625);

        emit anglesUpdated( pitch, yaw);
        emit temperaturesUpdated(msg->rtdata.imu_temp_celcius,
                                 msg->rtdata.frame_imu_temp_celcius);
    }
}
