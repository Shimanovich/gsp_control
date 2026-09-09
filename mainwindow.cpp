#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QSettings>
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

    m_speedSendTimer = new QTimer(this);
    m_speedSendTimer->setInterval(100);
    connect(m_speedSendTimer, &QTimer::timeout, this, &MainWindow::sendJoystickSpeed);

    setupControllers();
    loadAllSettings();
    QTimer::singleShot(50, this, &MainWindow::updateControlMode);

    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(ui->btnShoot, &QPushButton::clicked, this, &MainWindow::onShootClicked);
    connect(ui->btnVideoStart, &QPushButton::clicked, this, &MainWindow::onVideoStartClicked);
    connect(ui->btnVideoStop,  &QPushButton::clicked, this, &MainWindow::onVideoStopClicked);
    connect(ui->btnJetsonPlay, &QPushButton::clicked, this, &MainWindow::onJetsonPlayClicked);
    connect(ui->btnJetsonStop, &QPushButton::clicked, this, &MainWindow::onJetsonStopClicked);
    connect(ui->btnJetsonSet,  &QPushButton::clicked, this, &MainWindow::onJetsonSetClicked);
    connect(ui->btnTrackStart, &QPushButton::clicked, this, &MainWindow::onTrackStartClicked);
    connect(ui->btnTrackStop,  &QPushButton::clicked, this, &MainWindow::onTrackStopClicked);
    connect(ui->btnPidSettings, &QPushButton::clicked, this, &MainWindow::onPidSettingsClicked);

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
