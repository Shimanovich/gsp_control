#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "gsp_control_global.h"

#include <QMainWindow>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <queue>
#include <mutex>

#include "udpcommunicator.h"
#include "joystickmanager.h"
#include "cameracontroller.h"
#include "gyrocontroller.h"
#include "rangefindercontroller.h"
#include "udpReceiveAndDecode.h"
#include "keyboardmanager.h"
#include "jetsoncontroller.h"


class QSettings;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class GSP_CONTROL_EXPORT MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onGoToZeroClicked();
    void onShootClicked();
    void updateGyroAngles(float roll, float pitch);
    void updateGyroTemperatures(int imuTempC, int frameImuTempC);
    void updateJoystickStatus(bool connected);
    void updateConnectionStatus(bool connected);

    void onJoystickButtonPressed(int button);
    void onJoystickButtonReleased(int button);

    void onKeyBoardButtonPressed(int button);
    void onKeyBoardButtonReleased(int button);

    void on_radioZeroMode_clicked(bool checked);
    void on_radioSpeedMode_clicked(bool checked);
    void on_radioHeadingMode_clicked(bool checked);
    void on_radioAngleMode_clicked(bool checked);
    void on_radioMotorsOff_clicked(bool checked);
    void onAngleTargetChanged();
    void onDisconnectClicked();
    void on_cBoxAutoSimpleIntr_checkStateChanged(const Qt::CheckState &arg1);

    void on_btnZoomIn_clicked();
    void on_btnZoomOut_clicked();
    void on_btnAutofocus_clicked();
    void on_btnFocusInf_clicked();
    void onZoomPositionUpdated(float magnification);
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
    void onIncomingResolutionChanged(int width, int height);

    // Jetson / JEP
    void onJetsonPlayClicked();
    void onJetsonStopClicked();
    void onJetsonSetClicked();
    void onTrackStartClicked();
    void onTrackStopClicked();
    void onPidSettingsClicked();
    void onMdplStatus(const QString& stat);
    void onJetsonHwStatus(const JetsonHwStatus& st);
    void onCaptAck(const QString& stat);
    void onCaptStateUpdated(CaptState state);

    void on_spinSpeedMultiplier_valueChanged(int value);
    void on_checkInvertPitch_toggled(bool checked);

private:
    Ui::MainWindow *ui;

    UdpCommunicator* m_udp = nullptr;
    JoystickManager* m_joystick = nullptr;
    KeyboardManager* m_keyboard = nullptr;

    CameraController* m_camera = nullptr;
    GyroController* m_gyro = nullptr;
    RangefinderController* m_rangefinder = nullptr;
    JetsonController* m_jetson = nullptr;

    QString m_configPath = "config.ini";

    bool m_isSpeedMode = false;
    bool m_isHeadingMode = false;
    bool m_isAngleMode = false;
    bool m_isMotorsOffMode = false;
    QTimer* m_speedSendTimer = nullptr;
    double m_speedMultiplier = 1.0;
    float m_zoomMagnification = 1.0f;
    float m_headingYawDeg = 0.0f;
    float m_headingPitchDeg = 0.0f;
    float m_angleAzMin = -180.0f;
    float m_angleAzMax = 180.0f;
    float m_angleElMin = -90.0f;
    float m_angleElMax = 90.0f;

    // SDL-индексы кнопок из [Joystick] config.ini
    int m_btnZoomIn = 9;
    int m_btnZoomOut = 7;
    int m_btnZoomNext = 6;
    int m_btnZoomPrev = 8;
    int m_btnBrightnessUp = 10;
    int m_btnBrightnessDown = 12;
    int m_btnRangefinderShot = 1;
    int m_btnAutofocus = 2;
    int m_btnFocusInfinity = 3;
    int m_btnTrack = 4;
    int m_btnTrackCancel = 5;
    int m_btnHeading = 11;
    int m_btnSpeedUp = 13;
    int m_btnSpeedDown = 14;

    // Video
    udpDec*                 m_videoDec = nullptr;
    std::queue<AVFrame>     m_frameQueue;
    std::mutex              m_frameMutex;  // Вместо HANDLE m_frameMutex = nullptr;
    QTimer*                 m_videoTimer = nullptr;
    int                     m_videoPort = 5004;
    int                     m_videoTimeoutMs = 40;

    CaptState               m_captState;
    int                     m_lastFrameW = 0;
    int                     m_lastFrameH = 0;
    int                     m_dispResW = 0;
    int                     m_dispResH = 0;
    int                     m_drawStrobeW = 64;
    int                     m_drawStrobeH = 64;

    void setupControllers();
    void loadAllSettings();
    void updateControlMode();
    void sendJoystickSpeed();
    void sendZeroPos();
    void sendHeadingPos();
    void sendAnglePos();
    void loadHeadingAnglesFromConfig(QSettings& s);
    void activateHeadingMode();
    void toggleHeadingSpeedMode();
    void adjustSpeedMultiplier(int direction);
    bool readValidatedAngles(float& azDeg, float& elDeg, bool showError);

    void setupVideo();
    void stopVideo();

    void drawCaptureStrobe(QPainter& painter, const QSize& pixSize,
                           int frameW, int frameH,
                           int capX, int capY);

    void applyJetsonUiDefaults();
    void sendTrackCommand(int trackCmd);
    void drawOverlays(QPixmap& pix, int srcW, int srcH);


};


#endif // MAINWINDOW_H
