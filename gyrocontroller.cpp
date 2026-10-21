#include "gyrocontroller.h"
#include "jetsoncontroller.h"
#include <QDebug>

GyroController::GyroController(JetsonController* jetson, QObject *parent)
    : QObject(parent), m_jetson(jetson)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &GyroController::pollAngles);

    if (m_jetson) {
        connect(m_jetson, &JetsonController::gyroAnglesUpdated, this, &GyroController::onGyroAngles);
        connect(m_jetson, &JetsonController::gyroTempsUpdated, this, &GyroController::onGyroTemps);
        connect(m_jetson, &JetsonController::gyroStatusUpdated, this, &GyroController::onGyroStatus);
    }
}

bool GyroController::loadSettings(const QString&) { return true; }

void GyroController::setSpeed(float yaw, float pitch) { if (m_jetson) m_jetson->sendGyroSpeed(yaw, pitch); }
void GyroController::goToZeroPosition() { if (m_jetson) m_jetson->sendGyroHome(); }
void GyroController::goToHeadingPosition(float yaw, float pitch) { if (m_jetson) m_jetson->sendGyroHeading(yaw, pitch); }
void GyroController::goToStabNullPosition(float yaw, float pitch) { if (m_jetson) m_jetson->sendGyroStabNull(yaw, pitch); }
void GyroController::goToAngle(float yaw, float pitch) { if (m_jetson) m_jetson->sendGyroAngle(yaw, pitch); }
void GyroController::motorOn() { setMotorsPower(true); }
void GyroController::setMotorsPower(bool on) { if (m_jetson) m_jetson->sendGyroMotors(on); }
void GyroController::resetBoard() { if (m_jetson) m_jetson->sendGyroReset(); }

void GyroController::startAnglePolling()
{
    if (m_pollTimer && !m_pollTimer->isActive()) m_pollTimer->start(100);
}
void GyroController::stopAnglePolling()
{
    if (m_pollTimer) m_pollTimer->stop();
}
void GyroController::pollAngles() { if (m_jetson) m_jetson->sendGyroStatus(); }

void GyroController::onGyroAngles(float yaw, float pitch)
{
    emit anglesUpdated(pitch, yaw);
}
void GyroController::onGyroTemps(int imu, int frame)
{
    emit temperaturesUpdated(imu, frame);
}
void GyroController::onGyroStatus(bool motorsOn, bool ready, float yaw, float pitch)
{
    m_motorsPowered = motorsOn;
    m_controlReady = ready;
    emit controlReadyChanged(ready);
    emit anglesUpdated(pitch, yaw);
}
