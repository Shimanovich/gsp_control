#include "gyrocontroller.h"
#include "udpcommunicator.h"
#include <QDebug>
#include <QtEndian>

GyroController::GyroController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &GyroController::pollAngles);

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
     QByteArray payload = buildControlPayload(SimpleBGC::CONTROL_MODE_SPEED, yawSpeed, pitchSpeed);
     QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
     if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);
}

void GyroController::goToZeroPosition()
{
    //QByteArray payload = buildControlPayload(SimpleBGC::CONTROL_MODE_ANGLE, 0, 0);
    QByteArray payload = buildZeroPosCmd();
    QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
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
   // QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_MOTORS_ON);
   // if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);

   QByteArray payload = buildZeroPosCmd();
   QByteArray fullPacket = SimpleBGC::buildPacket(SimpleBGC::CMD_CONTROL, payload);
   if (m_udp) m_udp->sendPacket(m_targetId, fullPacket);

}



QByteArray GyroController::buildZeroPosCmd()
{

    SimpleBGC::cmd_control_t cmdout;

    cmdout.mode 		= SimpleBGC::CONTROL_MODE_ANGLE;
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

     if (mode==SimpleBGC::CONTROL_MODE_ANGLE)
     {
         int16_t p = qToLittleEndian<int16_t>(static_cast<int16_t>(pitch/0.02197265625));
         int16_t y = qToLittleEndian<int16_t>(static_cast<int16_t>(yaw/0.02197265625));

         cmdout.data[1].angle = p;
         cmdout.data[1].speed = 0;
         cmdout.data[2].angle = y;
         cmdout.data[2].speed = 0;
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
    }
}
