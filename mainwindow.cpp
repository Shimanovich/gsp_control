#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QPainter>
#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_udp = new UdpCommunicator(this);
    m_joystick = new JoystickManager(this);
    m_keyboard = new KeyboardManager(this);
    m_camera = new CameraController(m_udp, this);
    m_gyro = new GyroController(m_udp, this);
    m_rangefinder = new RangefinderController(m_udp, this);
    m_jetson = new JetsonController(this);

    // Create 10Hz timer for speed control
    m_speedSendTimer = new QTimer(this);
    m_speedSendTimer->setInterval(100); // 10 Hz
    connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);



    setupControllers();
    loadAllSettings();

    // Initialize control mode after everything is ready
    QTimer::singleShot(50, this, &MainWindow::updateControlMode);

    // Connect UI signals
    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(ui->btnShoot, &QPushButton::clicked, this, &MainWindow::onShootClicked);

    // Video buttons
    connect(ui->btnVideoStart, &QPushButton::clicked, this, &MainWindow::onVideoStartClicked);
    connect(ui->btnVideoStop,  &QPushButton::clicked, this, &MainWindow::onVideoStopClicked);

    connect(ui->btnJetsonPlay, &QPushButton::clicked, this, &MainWindow::onJetsonPlayClicked);
    connect(ui->btnJetsonStop, &QPushButton::clicked, this, &MainWindow::onJetsonStopClicked);
    connect(ui->btnJetsonSet,  &QPushButton::clicked, this, &MainWindow::onJetsonSetClicked);
    connect(ui->btnTrackStart, &QPushButton::clicked, this, &MainWindow::onTrackStartClicked);
    connect(ui->btnTrackStop,  &QPushButton::clicked, this, &MainWindow::onTrackStopClicked);
    connect(ui->btnPidSettings, &QPushButton::clicked, this, &MainWindow::onPidSettingsClicked);

    // Status labels
    ui->labelGyroStatus->setText("Disconnected");
    ui->labelJoystickStatus->setText("Disconnected");
    ui->labelVideoStatus->setText("Stopped");

    setupVideo();
}

MainWindow::~MainWindow()
{
    stopVideo();
    if (m_videoDec) {
        m_videoDec->stopThread();
        delete m_videoDec;
        m_videoDec = nullptr;
    }
    delete ui;
}

void MainWindow::setupControllers()
{
    connect(m_udp, &UdpCommunicator::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);
    connect(m_joystick, &JoystickManager::connectedChanged, this, &MainWindow::updateJoystickStatus);



    connect(m_joystick, &JoystickManager::buttonPressed,this, &MainWindow::onJoystickButtonPressed);
    connect(m_joystick, &JoystickManager::buttonReleased,this, &MainWindow::onJoystickButtonReleased);


    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_gyro, &GyroController::anglesUpdated, this, &MainWindow::updateGyroAngles);
    connect(m_gyro, &GyroController::temperaturesUpdated, this, &MainWindow::updateGyroTemperatures);

    connect(m_camera, &CameraController::zoomPositionUpdated,this, &MainWindow::onZoomPositionUpdated);

    connect(m_rangefinder, &RangefinderController::measurementReceived,   this, &MainWindow::onMeasurementReceived);

    connect(m_keyboard, &KeyboardManager::buttonPressed,
            this, &MainWindow::onKeyBoardButtonPressed);
    connect(m_keyboard, &KeyboardManager::buttonReleased,
            this, &MainWindow::onKeyBoardButtonReleased);

    connect(m_jetson, &JetsonController::mdplStatus, this, &MainWindow::onMdplStatus);
    connect(m_jetson, &JetsonController::captAck, this, &MainWindow::onCaptAck);
    connect(m_jetson, &JetsonController::captStateUpdated, this, &MainWindow::onCaptStateUpdated);
    connect(m_jetson, &JetsonController::errorOccurred, this, [this](const QString& e) {
        ui->statusBar->showMessage(e, 4000);
        qWarning() << e;
    });
}

void MainWindow::loadAllSettings()
{
    m_udp->loadSettings(m_configPath);
    m_joystick->loadSettings(m_configPath);
    m_camera->loadSettings(m_configPath);
    m_gyro->loadSettings(m_configPath);
    m_rangefinder->loadSettings(m_configPath);
    m_keyboard->loadSettings(m_configPath);
    m_jetson->loadSettings(m_configPath);

    QSettings s(m_configPath, QSettings::IniFormat);
    m_videoPort = s.value("Video/port", 5004).toInt();
    m_videoTimeoutMs = s.value("Video/timeout_ms", 40).toInt();
    m_btnZoomIn = s.value("Joystick/button_zoom_in", 9).toInt();
    m_btnZoomOut = s.value("Joystick/button_zoom_out", 7).toInt();
    m_btnZoomNext = s.value("Joystick/button_zoom_next", 6).toInt();
    m_btnZoomPrev = s.value("Joystick/button_zoom_prev", 8).toInt();
    m_btnBrightnessUp = s.value("Joystick/button_brightness_up", 10).toInt();
    m_btnBrightnessDown = s.value("Joystick/button_brightness_down", 12).toInt();
    m_btnRangefinderShot = s.value("Joystick/button_rangefinder_shot", 1).toInt();
    m_btnAutofocus = s.value("Joystick/button_autofocus", 2).toInt();
    m_btnFocusInfinity = s.value("Joystick/button_focus_infinity", 3).toInt();
    m_btnTrack = s.value("Joystick/button_track", 4).toInt();
    m_btnTrackCancel = s.value("Joystick/button_track_cancel", 5).toInt();
    m_btnHeading = s.value("Joystick/button_heading", 11).toInt();
    m_btnSpeedUp = s.value("Joystick/button_speed_up", 13).toInt();
    m_btnSpeedDown = s.value("Joystick/button_speed_down", 14).toInt();
    m_headingYawDeg = s.value("Gyro/heading_yaw", 0.0).toFloat();
    m_headingPitchDeg = s.value("Gyro/heading_pitch", 0.0).toFloat();
    m_angleAzMin = s.value("Gyro/angle_az_min", -180.0).toFloat();
    m_angleAzMax = s.value("Gyro/angle_az_max", 180.0).toFloat();
    m_angleElMin = s.value("Gyro/angle_el_min", -90.0).toFloat();
    m_angleElMax = s.value("Gyro/angle_el_max", 90.0).toFloat();
    m_angleAzMin = qBound(-720.0f, m_angleAzMin, 720.0f);
    m_angleAzMax = qBound(-720.0f, m_angleAzMax, 720.0f);
    m_angleElMin = qBound(-720.0f, m_angleElMin, 720.0f);
    m_angleElMax = qBound(-720.0f, m_angleElMax, 720.0f);
    if (m_angleAzMin > m_angleAzMax)
        qSwap(m_angleAzMin, m_angleAzMax);
    if (m_angleElMin > m_angleElMax)
        qSwap(m_angleElMin, m_angleElMax);

    if (ui->spinAngleAz) {
        ui->spinAngleAz->setDecimals(2);
        ui->spinAngleAz->setRange(m_angleAzMin, m_angleAzMax);
        ui->spinAngleAz->setValue(s.value("Gyro/angle_az", 0.0).toDouble());
    }
    if (ui->spinAngleEl) {
        ui->spinAngleEl->setDecimals(2);
        ui->spinAngleEl->setRange(m_angleElMin, m_angleElMax);
        ui->spinAngleEl->setValue(s.value("Gyro/angle_el", 0.0).toDouble());
    }
    connect(ui->spinAngleAz, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onAngleTargetChanged);
    connect(ui->spinAngleEl, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onAngleTargetChanged);

    const bool invertPitch = s.value("Joystick/invert_pitch", false).toBool();
    if (m_joystick)
        m_joystick->setInvertPitch(invertPitch);
    if (ui->checkInvertPitch) {
        QSignalBlocker blocker(ui->checkInvertPitch);
        ui->checkInvertPitch->setChecked(invertPitch);
    }
    m_drawStrobeW = s.value("Tracking/strobe_x_sz", 64).toInt();
    m_drawStrobeH = s.value("Tracking/strobe_y_sz", 64).toInt();

    applyJetsonUiDefaults();
}

void MainWindow::onConnectClicked()
{
    if (m_udp->start()) {
        m_joystick->initialize();

        m_keyboard->installOn(this);          // this = MainWindow
        this->setFocusPolicy(Qt::StrongFocus);
        this->setFocus();



        m_gyro->startAnglePolling();


        m_camera->startZoomPolling();
        if (m_jetson && !m_jetson->isStarted()) {
            if (!m_jetson->start()) {
                QMessageBox::warning(this, "Jetson",
                    "JEP UDP не открыт. Проверьте [Jetson] listen_port в config.ini");
            }
        }
        ui->btnConnect->setEnabled(false);
        ui->btnDisconnect->setEnabled(true);

        // Apply current mode after connection
        QTimer::singleShot(100, this, &MainWindow::updateControlMode);
    } else {
        QMessageBox::warning(this, "Error", "Failed to start UDP communication");
    }
     ui->btnDisconnect->setEnabled(true);
}

void MainWindow::onGoToZeroClicked()
{
    m_gyro->goToZeroPosition();
}

void MainWindow::onShootClicked()
{
    if (ui->checkSafety->isChecked()) {
        m_rangefinder->shoot();
    } else {
        QMessageBox::warning(this, "Safety", "Safety lock is disabled!");
    }
}

void MainWindow::updateGyroAngles( float pitch,float yaw)
{
    ui->labelRoll->setText(QString::number(yaw, 'f', 1) + "°");
    ui->labelPitch->setText(QString::number(pitch, 'f', 1) + "°");
}

void MainWindow::updateGyroTemperatures(int imuTempC, int frameImuTempC)
{
    auto formatTemp = [](int t) {
        return QString("%1 °C").arg(t);
    };
    if (ui->labelImuTemp)
        ui->labelImuTemp->setText("IMU: " + formatTemp(imuTempC));
    if (ui->labelFrameImuTemp)
        ui->labelFrameImuTemp->setText("Frame IMU: " + formatTemp(frameImuTempC));
}

void MainWindow::updateJoystickStatus(bool connected)
{
    ui->labelJoystickStatus->setText(connected ? "Connected" : "Disconnected");
    ui->labelJoystickStatus->setStyleSheet(connected ? "color: green;" : "color: red;");
}

void MainWindow::updateConnectionStatus(bool connected)
{
    ui->labelGyroStatus->setText(connected ? "Connected" : "Disconnected");
    ui->labelGyroStatus->setStyleSheet(connected ? "color: green;" : "color: red;");
}
void MainWindow::onJoystickButtonPressed(int button)
{

    qDebug() << "MainWindow: processing pressed button" << button;


    if (button == m_btnTrackCancel) {
        sendTrackCommand(0);
        return;
    }
    if (button == m_btnTrack) {
        const int cmd = qMax(1, ui->comboTrackCmd->currentData().toInt());
        sendTrackCommand(cmd);
        return;
    }
    if (button == m_btnHeading) {
        toggleHeadingSpeedMode();
        return;
    }
    if (button == m_btnSpeedUp) {
        adjustSpeedMultiplier(+1);
        return;
    }
    if (button == m_btnSpeedDown) {
        adjustSpeedMultiplier(-1);
        return;
    }
    if (button == m_btnZoomIn) {
        m_camera->zoomIn();
        return;
    }
    if (button == m_btnZoomOut) {
        m_camera->zoomOut();
        return;
    }
    if (button == m_btnZoomNext) {
        m_camera->setZoomPosition_next();
        return;
    }
    if (button == m_btnZoomPrev) {
        m_camera->setZoomPosition_prev();
        return;
    }
    if (button == m_btnBrightnessUp) {
        m_camera->brightnessUp();
        return;
    }
    if (button == m_btnBrightnessDown) {
        m_camera->brightnessDown();
        return;
    }
    if (button == m_btnRangefinderShot) {
        if (ui->checkSafety->isChecked())
            m_rangefinder->shoot();
        return;
    }
    if (button == m_btnAutofocus) {
        m_camera->autofocus();
        return;
    }
    if (button == m_btnFocusInfinity) {
        m_camera->focusInfinity();
        return;
    }
}


void MainWindow::onJoystickButtonReleased(int button)
{
    if (button == m_btnZoomIn || button == m_btnZoomOut)
        m_camera->zoomStop();
}


void MainWindow::onKeyBoardButtonPressed(int button)
{
    onJoystickButtonPressed(button);
}

void MainWindow::onKeyBoardButtonReleased(int button)
{
    onJoystickButtonReleased(button);
}



void MainWindow::onDisconnectClicked()
{
    if (m_udp) m_udp->stop();
    if (m_joystick) m_joystick->shutdown();
    if (m_gyro) m_gyro->stopAnglePolling();
    if (m_camera) m_camera->stopZoomPolling();
    if (m_keyboard) m_keyboard->uninstall();
    if (m_jetson) m_jetson->stop();

    ui->btnConnect->setEnabled(true);
    ui->btnDisconnect->setEnabled(false);

    ui->labelGyroStatus->setText("Disconnected");
    ui->labelGyroStatus->setStyleSheet("color: red;");
    ui->labelJoystickStatus->setText("Disconnected");
    ui->labelJoystickStatus->setStyleSheet("color: red;");
}

void MainWindow::updateControlMode()
{
    m_isSpeedMode = ui->radioSpeedMode->isChecked();
    m_isHeadingMode = ui->radioHeadingMode->isChecked();
    m_isAngleMode = ui->radioAngleMode->isChecked();
    m_isMotorsOffMode = ui->radioMotorsOff && ui->radioMotorsOff->isChecked();

    if (m_isAngleMode) {
        float az = 0.0f, el = 0.0f;
        if (!readValidatedAngles(az, el, true)) {
            ui->radioSpeedMode->setChecked(true);
            m_isAngleMode = false;
            m_isSpeedMode = true;
            m_isMotorsOffMode = false;
        }
    }

    if (m_speedSendTimer)
    {
        m_speedSendTimer->stop();
        disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendZeroPos);
        disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);
        disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendHeadingPos);
        disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendAnglePos);
        if (m_isMotorsOffMode) {
            if (m_gyro)
                m_gyro->setMotorsPower(false);
            return;
        }
        if (m_gyro)
            m_gyro->setMotorsPower(true);
        if (m_isSpeedMode) {
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);
        } else if (m_isHeadingMode) {
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendHeadingPos);
        } else if (m_isAngleMode) {
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendAnglePos);
        } else {
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendZeroPos);
        }
        m_speedSendTimer->start();
    }
}


void MainWindow::sendJoystickSpeed()
{
    if (!m_isSpeedMode || !m_gyro) return;

    float joyYaw   = m_joystick ? m_joystick->getAxisYaw()   : 0.0f;
    float joyPitch = m_joystick ? m_joystick->getAxisPitch() : 0.0f;

    float keyYaw   = m_keyboard ? m_keyboard->getAxisYaw()   : 0.0f;
    float keyPitch = m_keyboard ? m_keyboard->getAxisPitch() : 0.0f;

    const float zoomK = qMax(1.0f, m_zoomMagnification);
    float yaw   = (joyYaw   + keyYaw)   * m_speedMultiplier / zoomK;
    float pitch = -(joyPitch + keyPitch) * m_speedMultiplier / zoomK;

    ui->statusBar->showMessage(
        QString("Yaw: %1   Pitch: %2")
            .arg(yaw, 0, 'f', 3)
            .arg(pitch, 0, 'f', 3), 0);

    m_gyro->setSpeed(yaw, pitch);
}

void MainWindow::on_radioZeroMode_clicked(bool checked)
{
    if (checked)
    {
        updateControlMode();
    }
}


void MainWindow::on_radioSpeedMode_clicked(bool checked)
{
    if (checked)
    {
        updateControlMode();
    }
}

void MainWindow::on_radioHeadingMode_clicked(bool checked)
{
    if (checked)
    {
        updateControlMode();
    }
}

void MainWindow::on_radioAngleMode_clicked(bool checked)
{
    if (checked)
    {
        updateControlMode();
    }
}

void MainWindow::on_radioMotorsOff_clicked(bool checked)
{
    if (checked)
        updateControlMode();
}

void MainWindow::onAngleTargetChanged()
{
    if (!m_isAngleMode)
        return;
    sendAnglePos();
}

bool MainWindow::readValidatedAngles(float& azDeg, float& elDeg, bool showError)
{
    if (!ui->spinAngleAz || !ui->spinAngleEl) {
        if (showError)
            QMessageBox::warning(this, "Угол", "Поля AZ/EL недоступны.");
        return false;
    }

    if (!ui->spinAngleAz->hasAcceptableInput() || !ui->spinAngleEl->hasAcceptableInput()) {
        if (showError)
            QMessageBox::warning(this, "Угол",
                QString("Некорректный ввод AZ/EL.\nДопустимо AZ: %1…%2 °, EL: %3…%4 °.")
                    .arg(m_angleAzMin, 0, 'f', 2)
                    .arg(m_angleAzMax, 0, 'f', 2)
                    .arg(m_angleElMin, 0, 'f', 2)
                    .arg(m_angleElMax, 0, 'f', 2));
        return false;
    }

    azDeg = static_cast<float>(ui->spinAngleAz->value());
    elDeg = static_cast<float>(ui->spinAngleEl->value());

    if (azDeg < m_angleAzMin || azDeg > m_angleAzMax ||
        elDeg < m_angleElMin || elDeg > m_angleElMax) {
        if (showError)
            QMessageBox::warning(this, "Угол",
                QString("Угол вне диапазона.\nAZ: %1…%2 °, EL: %3…%4 °.")
                    .arg(m_angleAzMin, 0, 'f', 2)
                    .arg(m_angleAzMax, 0, 'f', 2)
                    .arg(m_angleElMin, 0, 'f', 2)
                    .arg(m_angleElMax, 0, 'f', 2));
        return false;
    }
    return true;
}

void MainWindow::sendAnglePos()
{
    if (!m_gyro)
        return;
    float az = 0.0f, el = 0.0f;
    if (!readValidatedAngles(az, el, false))
        return;
    m_gyro->goToHeadingPosition(az, el);
}

void MainWindow::activateHeadingMode()
{
    ui->radioHeadingMode->setChecked(true);
    updateControlMode();
}

void MainWindow::toggleHeadingSpeedMode()
{
    if (ui->radioHeadingMode->isChecked())
        ui->radioSpeedMode->setChecked(true);
    else
        ui->radioHeadingMode->setChecked(true);
    updateControlMode();
}

void MainWindow::adjustSpeedMultiplier(int direction)
{
    if (!ui->spinSpeedMultiplier)
        return;
    const int minV = ui->spinSpeedMultiplier->minimum();
    const int maxV = ui->spinSpeedMultiplier->maximum();
    const int span = maxV - minV;
    const int step = qMax(1, qRound(0.1 * span));
    const int next = qBound(minV, ui->spinSpeedMultiplier->value() + direction * step, maxV);
    ui->spinSpeedMultiplier->setValue(next);
}

void MainWindow::sendHeadingPos()
{
    if (!m_gyro) return;
    m_gyro->goToHeadingPosition(m_headingYawDeg, m_headingPitchDeg);
}


void MainWindow::on_cBoxAutoSimpleIntr_checkStateChanged(const Qt::CheckState &arg1)
{
    if (arg1 == Qt::Checked)
    {
        m_gyro->startAnglePolling();
        return;
    }
    if (arg1 == Qt::Unchecked)
    {
        m_gyro->stopAnglePolling();
        return ;
    }
}

void MainWindow::onZoomPositionUpdated(float magnification)
{
    m_zoomMagnification = (magnification > 0.0f) ? magnification : 1.0f;
    ui->zoomVal->setText(QString("Zoom: %1x").arg(m_zoomMagnification, 0, 'f', 2));
}




void MainWindow::on_checkInvertPitch_toggled(bool checked)
{
    if (m_joystick)
        m_joystick->setInvertPitch(checked);
}

void MainWindow::on_spinSpeedMultiplier_valueChanged(int value)
{
    m_speedMultiplier = (double)value;

    //ui->labelRoll->setText(QString::number(yaw, 'f', 1) + "°");
    ui->labelMultiplier->setText("speed " + QString::number((float)m_speedMultiplier, 'f', 1));
}

void MainWindow::sendZeroPos()
{
    m_gyro->goToZeroPosition();
}






void MainWindow::on_btnZoomIn_clicked()
{
    m_camera->zoomIn();
}


void MainWindow::on_btnZoomOut_clicked()
{
    m_camera->zoomOut();
}


void MainWindow::on_btnAutofocus_clicked()
{
    m_camera->autofocus();
}


void MainWindow::on_btnFocusInf_clicked()
{
    m_camera->focusInfinity();
}


void MainWindow::on_zoom_prev_clicked()
{
    m_camera->setZoomPosition_prev();
}


void MainWindow::on_zoom_next_clicked()
{
    m_camera->setZoomPosition_next();
}


void MainWindow::on_btnZoomIn_released()
{
    m_camera->zoomStop();
}

void MainWindow::on_btnZoomOut_released()
{
        m_camera->zoomStop();
}

void MainWindow::on_BrIghtUP_clicked()
{
    m_camera->brightnessUp();
}

void MainWindow::on_BrightDW_clicked()
{
    m_camera->brightnessDown();
}

void MainWindow::onMeasurementReceived(float distanceMeters, uint8_t status)
{
    ui->ldDistance->setText(QString("Дальность: %1 м").arg(distanceMeters, 0, 'f', 1));

    // Байт D9 протокола дальномера:
    // bit7 основная волна, bit6 эхо, bit5 лазер, bit4 таймаут (1=норма),
    // bit3 резерв, bit2 APD, bit1 предыдущая цель, bit0 последующая цель
    const bool mainWave = status & 0x80;
    const bool echo     = status & 0x40;
    const bool laserOk  = status & 0x20;
    const bool timeoutOk = status & 0x10;
    const bool apdOk    = status & 0x04;
    const bool prevTgt  = status & 0x02;
    const bool nextTgt  = status & 0x01;

    QStringList parts;
    parts << (laserOk ? QStringLiteral("лазер норма") : QStringLiteral("лазер неисправен"));
    parts << (apdOk ? QStringLiteral("APD норма") : QStringLiteral("APD ошибка"));
    parts << (timeoutOk ? QStringLiteral("таймаут нет") : QStringLiteral("таймаут превышен"));
    parts << (mainWave ? QStringLiteral("осн. волна есть") : QStringLiteral("осн. волны нет"));
    parts << (echo ? QStringLiteral("эхо есть") : QStringLiteral("эха нет"));
    parts << (prevTgt ? QStringLiteral("цель перед основной") : QStringLiteral("перед основной цели нет"));
    parts << (nextTgt ? QStringLiteral("цель за основной") : QStringLiteral("за основной цели нет"));

    QString summary;
    if (!laserOk || !apdOk)
        summary = QStringLiteral("Отказ");
    else if (!timeoutOk)
        summary = QStringLiteral("Таймаут");
    else if (!echo && !mainWave)
        summary = QStringLiteral("Нет цели");
    else
        summary = QStringLiteral("Норма");

    ui->Ld_status->setText(
        QStringLiteral("Статус: %1 (D9=0x%2)\n%3")
            .arg(summary)
            .arg(status, 2, 16, QChar('0'))
            .arg(parts.join(QStringLiteral("; "))));
}


// ============================================================================
// Video
// ============================================================================
void MainWindow::setupVideo()
{
    // Decoder and timer are created once here.
    // This avoids leaks and allows clean Stop/Start cycles.


    udpDec::PlayerInitStructure p{};
    p.udpport        = m_videoPort;
    p.udptimeout     = m_videoTimeoutMs;
    p.imageWidth     = 0;
    p.imageHeight    = 0;
    p.pFrameOutQueue = &m_frameQueue;
    p.phframeMutex   = &m_frameMutex;

    m_videoDec = new udpDec(&p, this);
    connect(m_videoDec, &udpDec::incomingResolutionChanged,
            this, &MainWindow::onIncomingResolutionChanged,
            Qt::QueuedConnection);

    m_videoTimer = new QTimer(this);
    connect(m_videoTimer, &QTimer::timeout, this, &MainWindow::onVideoTimer);
}

void MainWindow::onVideoStartClicked()
{
    if (!m_videoDec) {
        // Fallback if setupVideo was not called
        setupVideo();
    }
    if (!m_videoDec) return;

    if (!m_videoDec->on()) {
        ui->labelVideoStatus->setText("Open failed");
        ui->labelVideoStatus->setStyleSheet("color: red;");
        ui->btnVideoStart->setEnabled(true);
        ui->btnVideoStop->setEnabled(false);
        return;
    }

    m_videoTimer->start(33);
    ui->labelVideoStatus->setText("Running");
    ui->labelVideoStatus->setStyleSheet("color: green;");
    ui->btnVideoStart->setEnabled(false);
    ui->btnVideoStop->setEnabled(true);
    ui->videoLabel->setText("");
}

void MainWindow::onVideoStopClicked()
{
    stopVideo();
}

void MainWindow::stopVideo()
{
    if (m_videoTimer)
        m_videoTimer->stop();
    if (m_videoDec)
        m_videoDec->off();

    // ЗАМЕНА: используем std::lock_guard вместо WaitForSingleObject/ReleaseMutex
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        while (!m_frameQueue.empty()) {
            AVFrame f = m_frameQueue.front();
            m_frameQueue.pop();
            if (f.data[0]) av_free(f.data[0]);
        }
    }

    ui->videoLabel->clear();
    ui->videoLabel->setText("No signal");
    ui->labelVideoStatus->setText("Stopped");
    ui->labelVideoStatus->setStyleSheet("color: gray;");
    ui->labelVideoStatus->setToolTip(QString());
    m_lastFrameW = 0;
    m_lastFrameH = 0;
    m_dispResW = 0;
    m_dispResH = 0;
    ui->btnVideoStart->setEnabled(true);
    ui->btnVideoStop->setEnabled(false);
}

namespace {
QString resolutionClassName(int w, int h)
{
    if (w <= 0 || h <= 0)
        return QString();
    const int pixels = w * h;
    if (pixels >= 1920 * 1000)
        return QStringLiteral("FHD");
    if (pixels >= 1280 * 700)
        return QStringLiteral("HD");
    return QStringLiteral("SD");
}
} // namespace

void MainWindow::onIncomingResolutionChanged(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    m_dispResW = width;
    m_dispResH = height;
    const QString cls = resolutionClassName(width, height);
    const QString text = QStringLiteral("Running %1x%2 (%3)")
                             .arg(width)
                             .arg(height)
                             .arg(cls);
    ui->labelVideoStatus->setText(text);
    ui->labelVideoStatus->setStyleSheet("color: green;");
    ui->labelVideoStatus->setToolTip(
        QStringLiteral("Фактический размер входящего декодированного кадра"));
}

void MainWindow::onVideoTimer()
{
    AVFrame frame{};
    bool hasFrame = false;

    // ЗАМЕНА: используем std::lock_guard вместо WaitForSingleObject/ReleaseMutex
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_frameQueue.empty()) {
            frame = m_frameQueue.front();
            m_frameQueue.pop();
            hasFrame = true;
        }
    }
    if (!hasFrame || !frame.data[0])
        return;

    QImage img(frame.data[0], frame.width, frame.height,
               frame.linesize[0], QImage::Format_BGR888);

    // Отражение как у изображения: оба оси. Координаты SEI — в исходном кадре.
    const int srcW = frame.width;
    const int srcH = frame.height;

    if (srcW > 0 && srcH > 0) {
        m_lastFrameW = srcW;
        m_lastFrameH = srcH;
    }

    const bool hasStrobe = (frame.crop_right == 1);
    const int capX = static_cast<int>(frame.crop_left);
    const int capY = static_cast<int>(frame.crop_top);

    img = img.mirrored(true, true);

    QPixmap pix = QPixmap::fromImage(img).scaled(
        ui->videoLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);

    {
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPen pen(QColor(0, 255, 0, 220));
        pen.setWidth(2);
        painter.setPen(pen);

        const int cx = pix.width()  / 2;
        const int cy = pix.height() / 2;
        const int arm = 28;
        const int gap = 6;

        painter.drawLine(cx - arm, cy, cx - gap, cy);
        painter.drawLine(cx + gap, cy, cx + arm, cy);
        painter.drawLine(cx, cy - arm, cx, cy - gap);
        painter.drawLine(cx, cy + gap, cx, cy + arm);

        painter.setBrush(QColor(0, 255, 0, 220));
        painter.drawEllipse(QPoint(cx, cy), 2, 2);

        if (hasStrobe && srcW > 0 && srcH > 0)
            drawCaptureStrobe(painter, pix.size(), srcW, srcH, capX, capY);
    }

    ui->videoLabel->setPixmap(pix);

    if (srcW > 0 && srcH > 0 &&
        (srcW != m_dispResW || srcH != m_dispResH)) {
        onIncomingResolutionChanged(srcW, srcH);
    }

    if (frame.data[0]) {
        av_free(frame.data[0]);
        frame.data[0] = nullptr;
    }
}

void MainWindow::drawCaptureStrobe(QPainter& painter, const QSize& pixSize,
                                   int frameW, int frameH,
                                   int capX, int capY)
{
    if (frameW <= 0 || frameH <= 0 || pixSize.isEmpty())
        return;
    if (capX < 0 || capY < 0 || capX >= frameW || capY >= frameH)
        return;

    // То же mirrored(true, true), что и у кадра: поворот 180°.
    const int srcX = frameW - 1 - (capX);
    const int srcY = frameH - 1 - (capY);



    const double sx = double(pixSize.width())  / double(frameW);
    const double sy = double(pixSize.height()) / double(frameH);
    const int x = int(srcX * sx + 0.5);
    const int y = int(srcY * sy + 0.5);

    // Размер строба — тот, что ушёл в последней команде старта слежения.
    const int strobeW = (m_drawStrobeW > 0) ? m_drawStrobeW : 64;
    const int strobeH = (m_drawStrobeH > 0) ? m_drawStrobeH : 64;
    const int halfW = qMax(4, int(strobeW * sx * 0.5 + 0.5));
    const int halfH = qMax(4, int(strobeH * sy * 0.5 + 0.5));
    const int corner = qMax(6, qMin(halfW, halfH) / 2);

    QPen pen(QColor(255, 0, 0, 230));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // левый верх
    painter.drawLine(x - halfW, y - halfH, x - halfW + corner, y - halfH);
    painter.drawLine(x - halfW, y - halfH, x - halfW, y - halfH + corner);
    // правый верх
    painter.drawLine(x + halfW, y - halfH, x + halfW - corner, y - halfH);
    painter.drawLine(x + halfW, y - halfH, x + halfW, y - halfH + corner);
    // левый низ
    painter.drawLine(x - halfW, y + halfH, x - halfW + corner, y + halfH);
    painter.drawLine(x - halfW, y + halfH, x - halfW, y + halfH - corner);
    // правый низ
    painter.drawLine(x + halfW, y + halfH, x + halfW - corner, y + halfH);
    painter.drawLine(x + halfW, y + halfH, x + halfW, y + halfH - corner);

    painter.setBrush(QColor(255, 220, 0, 230));
    painter.drawEllipse(QPoint(x, y), 2, 2);
}

void MainWindow::drawOverlays(QPixmap& pix, int srcW, int srcH)
{
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int cx = pix.width()  / 2;
    const int cy = pix.height() / 2;

    QPen pen(QColor(0, 255, 0, 220));
    pen.setWidth(2);
    painter.setPen(pen);

    const int arm = 28;
    const int gap = 6;
    painter.drawLine(cx - arm, cy, cx - gap, cy);
    painter.drawLine(cx + gap, cy, cx + arm, cy);
    painter.drawLine(cx, cy - arm, cx, cy - gap);
    painter.drawLine(cx, cy + gap, cx, cy + arm);
    painter.setBrush(QColor(0, 255, 0, 220));
    painter.drawEllipse(QPoint(cx, cy), 2, 2);

    // Strobe from CAPT get_set: STROB_*_POS = 0 is the frame center.
    if (m_captState.trackStatus == 0)
        return;

    const float sx = (srcW > 0) ? float(pix.width())  / float(srcW) : 1.0f;
    const float sy = (srcH > 0) ? float(pix.height()) / float(srcH) : 1.0f;

    const int rw = qMax(4, int(m_captState.strobXSz * sx));
    const int rh = qMax(4, int(m_captState.strobYSz * sy));
    const int rx = cx + int(m_captState.strobXPos * sx) - rw / 2;
    const int ry = cy + int(m_captState.strobYPos * sy) - rh / 2;

    QPen strobePen(QColor(255, 200, 0, 230));
    strobePen.setWidth(2);
    painter.setPen(strobePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rx, ry, rw, rh);

    painter.setPen(QColor(255, 200, 0, 230));
    painter.drawText(rx, qMax(12, ry - 4),
                     QString("TRACK %1").arg(m_captState.trackStatus));
}

void MainWindow::applyJetsonUiDefaults()
{
    if (!m_jetson)
        return;

    if (ui->comboVideoChannel->itemData(0).isNull()) {
        ui->comboVideoChannel->setItemData(0, 1);
        ui->comboVideoChannel->setItemData(1, 2);
        ui->comboTrackCmd->setItemData(0, 0);
        ui->comboTrackCmd->setItemData(1, 1);
        ui->comboTrackCmd->setItemData(2, 2);
    }

    const QString res = m_jetson->defaultResolut();
    const int resIdx = ui->comboResolut->findText(res);
    ui->comboResolut->setCurrentIndex(resIdx >= 0 ? resIdx : 1);
    ui->spinBitrate->setValue(m_jetson->defaultBitrate());
    ui->spinStrobeW->setValue(m_jetson->defaultStrobeW());
    ui->spinStrobeH->setValue(m_jetson->defaultStrobeH());

    const int ch = m_jetson->defaultVideoChannel();
    const int chIdx = ui->comboVideoChannel->findData(ch);
    ui->comboVideoChannel->setCurrentIndex(chIdx >= 0 ? chIdx : 0);

    const int cmd = m_jetson->defaultTrackCmd();
    const int cmdIdx = ui->comboTrackCmd->findData(cmd);
    ui->comboTrackCmd->setCurrentIndex(cmdIdx >= 0 ? cmdIdx : 1);

    ui->labelJetsonStatus->setText("JEP: idle");
    ui->labelTrackStatus->setText("Track: off");
}

void MainWindow::sendTrackCommand(int trackCmd)
{
    if (!m_jetson) {
        ui->statusBar->showMessage("Jetson controller is not created", 3000);
        return;
    }
    if (!m_jetson->isStarted() && !m_jetson->start()) {
        QMessageBox::warning(this, "Jetson", "Не удалось открыть JEP UDP");
        return;
    }

    const int channel = ui->comboVideoChannel->currentData().toInt();
    const int w = ui->spinStrobeW->value();
    const int h = ui->spinStrobeH->value();
    if (trackCmd != 0) {
        m_drawStrobeW = w;
        m_drawStrobeH = h;
    }

    const int frameW = (m_lastFrameW > 0) ? m_lastFrameW : 1920;
    const int frameH = (m_lastFrameH > 0) ? m_lastFrameH : 1080;
    const int strobX = frameW / 2;
    const int strobY = frameH / 2;

    // 0,0 = центр отображаемого кадра по протоколу CAPT
    if (!m_jetson->sendTrackSet(trackCmd, channel, strobX, strobY, w, h)) {
        ui->statusBar->showMessage("JEP CAPT set send failed", 3000);
        return;
    }

    ui->labelTrackStatus->setText(trackCmd == 0
        ? QStringLiteral("Track: stopping")
        : QString("Track: cmd %1 sent").arg(trackCmd));
}

void MainWindow::onJetsonPlayClicked()
{
    if (!m_jetson) return;
    if (!m_jetson->isStarted() && !m_jetson->start()) {
        QMessageBox::warning(this, "Jetson", "Не удалось открыть JEP UDP");
        return;
    }

    const QString ip = m_jetson->playIp();
    const int port = m_jetson->playPort() != 0 ? m_jetson->playPort() : m_videoPort;
    if (!m_jetson->sendPlay(ip, port)) {
        ui->labelJetsonStatus->setText("JEP play failed");
        ui->labelJetsonStatus->setStyleSheet("color: red;");
        return;
    }
    ui->labelJetsonStatus->setText(QString("JEP play %1:%2").arg(ip).arg(port));
    ui->labelJetsonStatus->setStyleSheet("color: orange;");

    if (ui->btnVideoStart->isEnabled())
        onVideoStartClicked();
}

void MainWindow::onJetsonStopClicked()
{
    if (!m_jetson) return;
    if (!m_jetson->isStarted() && !m_jetson->start())
        return;
    if (!m_jetson->sendStop()) {
        ui->labelJetsonStatus->setText("JEP stop failed");
        ui->labelJetsonStatus->setStyleSheet("color: red;");
        return;
    }
    ui->labelJetsonStatus->setText("JEP stop sent");
    ui->labelJetsonStatus->setStyleSheet("color: orange;");
}

void MainWindow::onJetsonSetClicked()
{
    if (!m_jetson) return;
    if (!m_jetson->isStarted() && !m_jetson->start()) {
        QMessageBox::warning(this, "Jetson", "Не удалось открыть JEP UDP");
        return;
    }
    const int bitrate = ui->spinBitrate->value();
    const QString res = ui->comboResolut->currentText();
    if (!m_jetson->sendSet(bitrate, res)) {
        ui->labelJetsonStatus->setText("JEP set failed");
        ui->labelJetsonStatus->setStyleSheet("color: red;");
        return;
    }
    ui->labelJetsonStatus->setText(QString("JEP set %1 %2").arg(res).arg(bitrate));
    ui->labelJetsonStatus->setStyleSheet("color: orange;");
}


void MainWindow::onPidSettingsClicked()
{
    if (!m_jetson)
        return;

    TrackingParams pid = m_jetson->trackingParams();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("PID tracking"));
    auto *form = new QFormLayout();

    auto makeSpin = [](double v) {
        auto *sp = new QDoubleSpinBox();
        sp->setDecimals(4);
        sp->setRange(-1000.0, 1000.0);
        sp->setSingleStep(0.1);
        sp->setValue(v);
        return sp;
    };

    QDoubleSpinBox *xp = makeSpin(pid.pidXp);
    QDoubleSpinBox *xi = makeSpin(pid.pidXi);
    QDoubleSpinBox *xd = makeSpin(pid.pidXd);
    QDoubleSpinBox *yp = makeSpin(pid.pidYp);
    QDoubleSpinBox *yi = makeSpin(pid.pidYi);
    QDoubleSpinBox *yd = makeSpin(pid.pidYd);
    form->addRow(QStringLiteral("PID_X_P"), xp);
    form->addRow(QStringLiteral("PID_X_I"), xi);
    form->addRow(QStringLiteral("PID_X_D"), xd);
    form->addRow(QStringLiteral("PID_Y_P"), yp);
    form->addRow(QStringLiteral("PID_Y_I"), yi);
    form->addRow(QStringLiteral("PID_Y_D"), yd);

    // auto *invAz = new QCheckBox(QStringLiteral("INV_AZ"));
    // invAz->setChecked(pid.invAz != 0);
    // auto *invEl = new QCheckBox(QStringLiteral("INV_EL"));
    // invEl->setChecked(pid.invEl != 0);
    // form->addRow(invAz);
    // form->addRow(invEl);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(box);

    if (dlg.exec() != QDialog::Accepted)
        return;

    pid.pidXp = float(xp->value());
    pid.pidXi = float(xi->value());
    pid.pidXd = float(xd->value());
    pid.pidYp = float(yp->value());
    pid.pidYi = float(yi->value());
    pid.pidYd = float(yd->value());
    //pid.invAz = invAz->isChecked() ? 1 : -1;
    //pid.invEl = invEl->isChecked() ? 1 : -1;
    pid.invAz = 1;
    pid.invEl = 1;
    m_jetson->setTrackingParams(pid);

    if (!m_jetson->isStarted() && !m_jetson->start()) {
        QMessageBox::warning(this, "Jetson", "Не удалось открыть JEP UDP");
        return;
    }
    if (!m_jetson->sendPidSet()) {
        ui->statusBar->showMessage(QStringLiteral("CAPT PID set send failed"), 3000);
        return;
    }
    ui->statusBar->showMessage(
        QString("PID отправлены (только PID_*): Xp=%1 Xi=%2 Xd=%3 Yp=%4 Yi=%5 Yd=%6")
            .arg(pid.pidXp).arg(pid.pidXi).arg(pid.pidXd)
            .arg(pid.pidYp).arg(pid.pidYi).arg(pid.pidYd), 4000);
}

void MainWindow::onTrackStartClicked()
{
    int cmd = ui->comboTrackCmd->currentData().toInt();
    if (cmd == 0)
        cmd = 1;
    sendTrackCommand(cmd);
}

void MainWindow::onTrackStopClicked()
{
    sendTrackCommand(0);
}

void MainWindow::onMdplStatus(const QString& stat)
{
    ui->labelJetsonStatus->setText(QString("JEP: %1").arg(stat));
    ui->labelJetsonStatus->setStyleSheet("color: green;");
}

void MainWindow::onCaptAck(const QString& stat)
{
    ui->statusBar->showMessage(QString("CAPT %1").arg(stat), 1500);
}

void MainWindow::onCaptStateUpdated(CaptState state)
{
    m_captState = state;
    if (state.trackStatus == 0) {
        ui->labelTrackStatus->setText("Track: off");
        ui->labelTrackStatus->setStyleSheet("color: gray;");
    } else {
        ui->labelTrackStatus->setText(
            QString("Track: ON (%1) ch=%2  %3x%4")
                .arg(state.trackStatus)
                .arg(state.videoChannel)
                .arg(state.strobXSz)
                .arg(state.strobYSz));
        ui->labelTrackStatus->setStyleSheet("color: orange;");
    }
}



