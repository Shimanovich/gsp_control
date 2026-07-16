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

    setupControllers();
    loadAllSettings();

    // Connect UI signals (example buttons)
    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(ui->btnGoToZero, &QPushButton::clicked, this, &MainWindow::onGoToZeroClicked);
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
        m_gyro->startAnglePolling();
        m_camera->startZoomPolling();
        ui->btnConnect->setEnabled(false);
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

void MainWindow::updateGyroAngles(float roll, float pitch, float yaw)
{
    ui->labelRoll->setText(QString::number(roll, 'f', 1) + "°");
    ui->labelPitch->setText(QString::number(pitch, 'f', 1) + "°");
    ui->labelYaw->setText(QString::number(yaw, 'f', 1) + "°");
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


void MainWindow::onJoystickButtonPressed(int button)
{
    // Map buttons according to config (simplified)
    if (button == 0) m_camera->zoomIn();
    else if (button == 1) m_camera->zoomOut();
    else if (button == 2) m_camera->autofocus();
    // ... add more mappings from INI later
}
