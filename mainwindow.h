#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <queue>

#include "udpcommunicator.h"
#include "joystickmanager.h"
#include "cameracontroller.h"
#include "gyrocontroller.h"
#include "rangefindercontroller.h"
#include "udpReceiveAndDecode.h"

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
    void updateGyroAngles(float roll, float pitch);
    void updateJoystickStatus(bool connected);
    void updateConnectionStatus(bool connected);

    void onJoystickButtonPressed(int button);
    void onJoystickButtonReleased(int button);

    void on_radioZeroMode_clicked(bool checked);
    void on_radioSpeedMode_clicked(bool checked);
    void onDisconnectClicked();
    void on_cBoxAutoSimpleIntr_checkStateChanged(const Qt::CheckState &arg1);
    void on_spinSpeedMultiplier_valueChanged(double arg1);
    void on_btMotor_on_clicked();

    void on_btnZoomIn_clicked();
    void on_btnZoomOut_clicked();
    void on_btnAutofocus_clicked();
    void on_btnFocusInf_clicked();
    void onZoomPositionUpdated(float position);
    void on_zoom_prev_clicked();
    void on_zoom_next_clicked();
    void on_btnZoomIn_released();
    void on_btnZoomOut_released();
    void on_BrIghtUP_clicked();
    void on_BrightDW_clicked();
    void onMeasurementReceived(float distanceMeters, uint8_t status);

    // Video
    void onVideoStartClicked();
    void onVideoStopClicked();
    void onVideoTimer();

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

    // Video
    udpDec*                 m_videoDec = nullptr;
    std::queue<AVFrame>     m_frameQueue;
    HANDLE                  m_frameMutex = nullptr;
    QTimer*                 m_videoTimer = nullptr;
    int                     m_videoPort = 5004;
    int                     m_videoTimeoutMs = 40;

    void setupControllers();
    void loadAllSettings();
    void updateControlMode();
    void sendJoystickSpeed();
    void sendZeroPos();

    void setupVideo();
    void stopVideo();
};

#endif // MAINWINDOW_H
