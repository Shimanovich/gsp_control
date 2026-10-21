#ifndef GYROCONTROLLER_H
#define GYROCONTROLLER_H

#include <QObject>
#include <QTimer>

class JetsonController;

class GyroController : public QObject
{
    Q_OBJECT
public:
    explicit GyroController(JetsonController* jetson, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);

    void setSpeed(float yawSpeed, float pitchSpeed);
    void goToZeroPosition();
    void goToHeadingPosition(float yawDeg, float pitchDeg);
    void goToStabNullPosition(float yawDeg, float pitchDeg);
    void goToAngle(float yawDeg, float pitchDeg);
    void startAnglePolling();
    void stopAnglePolling();
    void motorOn();
    void setMotorsPower(bool on);
    void resetBoard();
    bool motorsPowered() const { return m_motorsPowered; }
    bool controlReady() const { return m_motorsPowered && m_controlReady; }

signals:
    void anglesUpdated(float pitch, float yaw);
    void temperaturesUpdated(int imuTempC, int frameImuTempC);
    void error(const QString& msg);
    void controlReadyChanged(bool ready);

private slots:
    void onGyroAngles(float yaw, float pitch);
    void onGyroTemps(int imu, int frame);
    void onGyroStatus(bool motorsOn, bool ready, float yaw, float pitch);
    void pollAngles();

private:
    JetsonController* m_jetson = nullptr;
    QTimer* m_pollTimer = nullptr;
    bool m_motorsPowered = true;
    bool m_controlReady = true;
};

#endif // GYROCONTROLLER_H
