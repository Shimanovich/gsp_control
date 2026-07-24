
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_udp = new UdpCommunicator(this);
    m_joystick = new JoystickManager(this);
    m_camera = new CameraController(m_udp, this);
    m_gyro = new GyroController(m_udp, this);
    m_rangefinder = new RangefinderController(m_udp, this);

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
    if (m_frameMutex) {
        CloseHandle(m_frameMutex);
        m_frameMutex = nullptr;
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

    connect(m_camera, &CameraController::zoomPositionUpdated,this, &MainWindow::onZoomPositionUpdated);

    connect(m_rangefinder, &RangefinderController::measurementReceived,   this, &MainWindow::onMeasurementReceived);


}

void MainWindow::loadAllSettings()
{
    m_udp->loadSettings(m_configPath);
    m_joystick->loadSettings(m_configPath);
    m_camera->loadSettings(m_configPath);
    m_gyro->loadSettings(m_configPath);
    m_rangefinder->loadSettings(m_configPath);

    QSettings s(m_configPath, QSettings::IniFormat);
    m_videoPort = s.value("Video/port", 5004).toInt();
    m_videoTimeoutMs = s.value("Video/timeout_ms", 40).toInt();
}

void MainWindow::onConnectClicked()
{
    if (m_udp->start()) {
        m_joystick->initialize();



        if (ui->cBoxAutoSimpleIntr->isChecked())
        {
            m_gyro->startAnglePolling();
        }
        m_camera->startZoomPolling();
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

    switch (button) {


    case 9:  m_camera->zoomIn();                        break;
    case 7:  m_camera->zoomOut();                       break;
    case 6:  m_camera->setZoomPosition_next();          break;
    case 8:  m_camera->setZoomPosition_prev();          break;

    case 10:  m_camera->brightnessUp();                 break;
    case 12:  m_camera->brightnessDown();               break;

    case 1:  if (ui->checkSafety->isChecked())
            m_rangefinder->shoot();
        break;

    case 2:  m_camera->autofocus();                     break;
    case 3:  m_camera->focusInfinity();                 break;



    default: break;
    }
}

void MainWindow::onJoystickButtonReleased(int button)
{
    // Вызов ровно один раз при отпускании (только для непрерывных действий)
    switch (button) {
    case 9:  // zoomIn
    case 7:  // zoomOut
        m_camera->zoomStop();
        break;
    default:
        break;   // остальные действия не требуют release
    }
}



void MainWindow::onDisconnectClicked()
{
    if (m_udp) m_udp->stop();
    if (m_joystick) m_joystick->shutdown();
    if (m_gyro) m_gyro->stopAnglePolling();
    if (m_camera) m_camera->stopZoomPolling();

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

    if (m_speedSendTimer)
    {
        m_speedSendTimer->stop();
        if (m_isSpeedMode) {
            disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendZeroPos);
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);
        }
        else {
            disconnect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);
            connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendZeroPos);
        }
        m_speedSendTimer->start();
    }

}


void MainWindow::sendJoystickSpeed()
{
    if (!m_isSpeedMode || !m_joystick || !m_gyro) return;

    float yaw   = m_joystick->getAxisYaw()   * m_speedMultiplier*10.0;
    float pitch = -m_joystick->getAxisPitch() * m_speedMultiplier*1.0;

    ui->statusBar->showMessage(
        QString("Yaw: %1   Pitch: %2")
            .arg(yaw,0, 'f', 3)
            .arg(pitch,0, 'f', 3),0
        );

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

void MainWindow::onZoomPositionUpdated(float position)
{
    // position: 0.0 .. 1.0 (normalized from 0x0000..0x4000)
    ui->zoomVal->setText(QString("Zoom: %1%").arg(position * 100.0f, 0, 'f', 1));
}



void MainWindow::on_spinSpeedMultiplier_valueChanged(double arg1)
{
    m_speedMultiplier = arg1;
}

void MainWindow::sendZeroPos()
{
    m_gyro->goToZeroPosition();
}


void MainWindow::on_btMotor_on_clicked()
{
    m_gyro->motorOn();
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
    // Показать измеренное расстояние
    ui->ldDistance->setText(QString("Distance: %1 m").arg(distanceMeters, 0, 'f', 1));

    // Результат одиночного измерения (статус-байт D9 по протоколу)
    // Биты: 7-main wave, 6-echo, 5-laser OK, 4-timeout, 3-reserved=1, 2-APD OK, 1-prev target, 0-next target
    QString statusText;
    bool ok = (status & 0x20) && (status & 0x04); // laser OK (bit5) && APD OK (bit2)
    bool hasEcho = (status & 0x40);
    bool hasMainWave = (status & 0x80);

    if (!ok) {
        statusText = QString("Error (0x%1)").arg(status, 2, 16, QChar('0')).toUpper();
    } else if (!hasEcho && !hasMainWave) {
        statusText = QString("No target (0x%1)").arg(status, 2, 16, QChar('0')).toUpper();
    } else {
        statusText = QString("OK (0x%1)").arg(status, 2, 16, QChar('0')).toUpper();
    }

    ui->Ld_status->setText("Status: " + statusText);
}


// ============================================================================
// Video
// ============================================================================
void MainWindow::setupVideo()
{
    m_frameMutex = CreateMutexA(nullptr, FALSE, nullptr);

    udpDec::PlayerInitStructure p{};
    p.udpport        = m_videoPort;
    p.udptimeout     = m_videoTimeoutMs;
    p.imageWidth     = 0;
    p.imageHeight    = 0;
    p.pFrameOutQueue = &m_frameQueue;
    p.pHframeMutex   = &m_frameMutex;

    m_videoDec = new udpDec(&p);

    m_videoTimer = new QTimer(this);
    connect(m_videoTimer, &QTimer::timeout, this, &MainWindow::onVideoTimer);
}

void MainWindow::onVideoStartClicked()
{
    if (!m_videoDec) return;
    m_videoDec->on();
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

    if (m_frameMutex)
        WaitForSingleObject(m_frameMutex, INFINITE);
    while (!m_frameQueue.empty()) {
        AVFrame f = m_frameQueue.front();
        m_frameQueue.pop();
        if (f.data[0]) av_free(f.data[0]);
    }
    if (m_frameMutex)
        ReleaseMutex(m_frameMutex);

    ui->videoLabel->clear();
    ui->videoLabel->setText("No signal");
    ui->labelVideoStatus->setText("Stopped");
    ui->labelVideoStatus->setStyleSheet("color: gray;");
    ui->btnVideoStart->setEnabled(true);
    ui->btnVideoStop->setEnabled(false);
}

void MainWindow::onVideoTimer()
{
    AVFrame frame{};
    bool hasFrame = false;

    if (m_frameMutex)
        WaitForSingleObject(m_frameMutex, INFINITE);
    if (!m_frameQueue.empty()) {
        frame = m_frameQueue.front();
        m_frameQueue.pop();
        hasFrame = true;
    }
    if (m_frameMutex)
        ReleaseMutex(m_frameMutex);

    if (!hasFrame || !frame.data[0])
        return;

    QImage img(frame.data[0], frame.width, frame.height,
               frame.linesize[0], QImage::Format_BGR888);

    QPixmap pix = QPixmap::fromImage(img).scaled(
        ui->videoLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);

    ui->videoLabel->setPixmap(pix);

    if (frame.data[0]) {
        av_free(frame.data[0]);
        frame.data[0] = nullptr;
    }
}
