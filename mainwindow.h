#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "udpcommunicator.h"
#include "joystickmanager.h"
#include "cameracontroller.h"
#include "gyrocontroller.h"
#include "rangefindercontroller.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onGoToZeroClicked();
    void onShootClicked();
    void updateGyroAngles(float roll, float pitch, float yaw);
    void updateJoystickStatus(bool connected);
    void updateConnectionStatus(bool connected);
    void onJoystickButtonPressed(int button);

    void on_radioZeroMode_clicked(bool checked);

    void on_radioSpeedMode_clicked(bool checked);

    void onDisconnectClicked();

private:
    Ui::MainWindow *ui;

    UdpCommunicator* m_udp = nullptr;
    JoystickManager* m_joystick = nullptr;
    CameraController* m_camera = nullptr;
    GyroController* m_gyro = nullptr;
    RangefinderController* m_rangefinder = nullptr;

    QString m_configPath = "config.ini";

    bool m_isSpeedMode = false;
    QTimer* m_speedSendTimer = nullptr;
    double m_speedMultiplier = 1.0;

    void setupControllers();
    void loadAllSettings();
    void updateControlMode();
    void sendJoystickSpeed();
};

#endif // MAINWINDOW_H