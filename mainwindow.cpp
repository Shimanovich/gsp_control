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
    m_trackButton = s.value("Joystick/button_track", 4).toInt();

    applyJetsonUiDefaults();
}

void MainWindow::onConnectClicked()
{
    if (m_udp->start()) {
        m_joystick->initialize();

        m_keyboard->installOn(this);          // this = MainWindow
        this->setFocusPolicy(Qt::StrongFocus);
        this->setFocus();


        if (ui->cBoxAutoSimpleIntr->isChecked())
        {
            m_gyro->startAnglePolling();
        }
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


    // special for track start
    if (button == m_trackButton) {
        const int cmd = (m_captState.trackStatus != 0)
            ? 0
            : qMax(1, ui->comboTrackCmd->currentData().toInt());
        sendTrackCommand(cmd);
        return;
    }

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
    if (!m_isSpeedMode || !m_gyro) return;

    float joyYaw   = m_joystick ? m_joystick->getAxisYaw()   : 0.0f;
    float joyPitch = m_joystick ? m_joystick->getAxisPitch() : 0.0f;

    float keyYaw   = m_keyboard ? m_keyboard->getAxisYaw()   : 0.0f;
    float keyPitch = m_keyboard ? m_keyboard->getAxisPitch() : 0.0f;

    float yaw   = (joyYaw   + keyYaw)   * m_speedMultiplier;
    float pitch = -(joyPitch + keyPitch) * m_speedMultiplier;

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
    // Decoder and timer are created once here.
    // This avoids leaks and allows clean Stop/Start cycles.
    m_frameMutex = CreateMutexA(nullptr, FALSE, nullptr);

    udpDec::PlayerInitStructure p{};
    p.udpport        = m_videoPort;
    p.udptimeout     = m_videoTimeoutMs;
    p.imageWidth     = 0;
    p.imageHeight    = 0;
    p.pFrameOutQueue = &m_frameQueue;
    p.pHframeMutex   = &m_frameMutex;

    m_videoDec = new udpDec(&p, this);

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

    qDebug()<<"cX="<<capX<<" cY="<<capY;

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
    const int srcX = frameW - 1 - capX;
    const int srcY = frameH - 1 - capY;

    const double sx = double(pixSize.width())  / double(frameW);
    const double sy = double(pixSize.height()) / double(frameH);
    const int x = int(srcX * sx + 0.5);
    const int y = int(srcY * sy + 0.5);

    // Уголковый строб. Размер от меньшей стороны кадра, не меньше 16 px на экране.
    const int halfSrc = qMax(16, qMin(frameW, frameH) / 40);
    const int half = qMax(12, int(halfSrc * qMin(sx, sy) + 0.5));
    const int corner = qMax(6, half / 2);

    QPen pen(QColor(255, 220, 0, 230));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // левый верх
    painter.drawLine(x - half, y - half, x - half + corner, y - half);
    painter.drawLine(x - half, y - half, x - half, y - half + corner);
    // правый верх
    painter.drawLine(x + half, y - half, x + half - corner, y - half);
    painter.drawLine(x + half, y - half, x + half, y - half + corner);
    // левый низ
    painter.drawLine(x - half, y + half, x - half + corner, y + half);
    painter.drawLine(x - half, y + half, x - half, y + half - corner);
    // правый низ
    painter.drawLine(x + half, y + half, x + half - corner, y + half);
    painter.drawLine(x + half, y + half, x + half, y + half - corner);

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



