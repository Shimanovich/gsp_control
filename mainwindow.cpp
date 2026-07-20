#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>

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

    // Connect UI signals (example buttons)
    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    //connect(ui->btnGoToZero, &QPushButton::clicked, this, &MainWindow::onGoToZeroClicked);
    connect(ui->btnShoot, &QPushButton::clicked, this, &MainWindow::onShootClicked);

    // Status labels
    ui->labelGyroStatus->setText("Disconnected");
    ui->labelJoystickStatus->setText("Disconnected");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupControllers()
{
    connect(m_udp, &UdpCommunicator::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);
    connect(m_joystick, &JoystickManager::connectedChanged, this, &MainWindow::updateJoystickStatus);
    connect(m_joystick, &JoystickManager::buttonPressed, this, &MainWindow::onJoystickButtonPressed);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_gyro, &GyroController::anglesUpdated, this, &MainWindow::updateGyroAngles);


}

void MainWindow::loadAllSettings()
{
    m_udp->loadSettings(m_configPath);
    m_joystick->loadSettings(m_configPath);
    m_camera->loadSettings(m_configPath);
    m_gyro->loadSettings(m_configPath);
    m_rangefinder->loadSettings(m_configPath);
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
    // Map buttons according to config (simplified)
    if (button == 0) m_camera->zoomIn();
    else if (button == 1) m_camera->zoomOut();
    else if (button == 2) m_camera->autofocus();
    // ... add more mappings from INI later
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
    float pitch = -m_joystick->getAxisPitch() * m_speedMultiplier*10.0;

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



