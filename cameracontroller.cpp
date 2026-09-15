#include "cameracontroller.h"
#include "udpcommunicator.h"
#include <QDebug>

CameraController::CameraController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
    m_zoomPollTimer = new QTimer(this);
    connect(m_zoomPollTimer, &QTimer::timeout, this, &CameraController::pollZoomPosition);


    if (m_udp) {
            connect(m_udp, &UdpCommunicator::packetReceived,
                    this, &CameraController::handleIncomingPacket);
        }
}

bool CameraController::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);







    if (settings.status() != QSettings::NoError) {
        qWarning() << "Не удалось открыть ini:" << iniPath << "status =" << settings.status();
        return false;
    }

    m_targetId = settings.value("TargetIDs/camera", 11).toUInt();

    QStringList posList = settings.value("Camera/zoom_positions",
                                         "0x0000,0x1000,0x2000,0x3000,0x4000")
                              .toString()
                              .split(',', Qt::SkipEmptyParts);

    m_zoomDirectCommands.clear();
    for (const QString& posRaw : posList) {
        QString pos = posRaw.trimmed();
        bool ok = false;
        // base = 0 — автоматически понимает 0x
        uint16_t val = pos.toUInt(&ok, 0);
        if (!ok) {
            qWarning() << "Не удалось распарсить zoom position:" << pos;
            continue;
        }

        QByteArray cmd;
        cmd.append(char(0x81));
        cmd.append(char(0x01));
        cmd.append(char(0x04));
        cmd.append(char(0x47));
        cmd.append(char((val >> 12) & 0x0F));
        cmd.append(char((val >> 8)  & 0x0F));
        cmd.append(char((val >> 4)  & 0x0F));
        cmd.append(char(val & 0x0F));
        cmd.append(char(0xFF));
        m_zoomDirectCommands.append(cmd);
    }

    m_zoomNames = settings.value("Camera/zoom_position_names", "1x,2x,4x,8x,10x")
                      .toString()
                      .split(',', Qt::SkipEmptyParts);

    m_currentZoomIndex = settings.value("Camera/default_zoom_index", 0).toInt();

    // Оптическая кривая MC-108-M3 (VISCA Zoom Position → Magnification)
    const QString defPos = "0x0000,0x1816,0x240B,0x28C7,0x31AB,0x363D,0x39B6,0x3C65,0x3E81,0x4000";
    const QString defMag = "1,2,3,4,5,6,7,8,9,10";
    const QStringList curvePosList = settings.value("Camera/zoom_curve_positions", defPos)
                                         .toString().split(',', Qt::SkipEmptyParts);
    const QStringList curveMagList = settings.value("Camera/zoom_curve_magnifications", defMag)
                                         .toString().split(',', Qt::SkipEmptyParts);
    m_zoomCurvePos.clear();
    m_zoomCurveMag.clear();
    const int nCurve = qMin(curvePosList.size(), curveMagList.size());
    for (int i = 0; i < nCurve; ++i) {
        bool okPos = false, okMag = false;
        const uint16_t p = static_cast<uint16_t>(curvePosList[i].trimmed().toUInt(&okPos, 0));
        const float m = curveMagList[i].trimmed().toFloat(&okMag);
        if (okPos && okMag && m > 0.0f)
        {
            if (!m_zoomCurvePos.isEmpty() && p < m_zoomCurvePos.last())
                continue;
            m_zoomCurvePos.append(p);
            m_zoomCurveMag.append(m);
        }
    }
    if (m_zoomCurvePos.size() < 2) {
        m_zoomCurvePos = {0x0000, 0x1816, 0x240B, 0x28C7, 0x31AB,
                          0x363D, 0x39B6, 0x3C65, 0x3E81, 0x4000};
        m_zoomCurveMag = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    }

    // Небольшая валидация
    if (m_zoomDirectCommands.isEmpty()) {
        qWarning() << "Не загружено ни одной позиции зума!";
        return false;
    }
    if (m_currentZoomIndex < 0 || m_currentZoomIndex >= m_zoomDirectCommands.size()) {
        m_currentZoomIndex = 0;
    }

    return true;
}

void CameraController::zoomIn()
{
    // Standard VISCA Zoom Tele (from typical VISCA for this camera)
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(0x02)); // Tele Standard
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::zoomOut()
{
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(0x03)); // Wide Standard
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::zoomStop()
{
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x07));
    cmd.append(char(0x00)); // stop
    cmd.append(char(0xFF));
    sendVisca(cmd);
}



void CameraController::setZoomPosition(int index)
{
    if (index < 0 || index >= m_zoomDirectCommands.size()) return;
    m_currentZoomIndex = index;
    sendVisca(m_zoomDirectCommands[index]);
}

void CameraController::setZoomPosition_prev()
{
    if (m_currentZoomIndex > 0)
    {
        m_currentZoomIndex--;
        sendVisca(m_zoomDirectCommands[m_currentZoomIndex]);
    }
}
void CameraController::setZoomPosition_next()
{
    if (m_currentZoomIndex < (m_zoomDirectCommands.size()-1))
    {
        m_currentZoomIndex++;
        sendVisca(m_zoomDirectCommands[m_currentZoomIndex]);
    }
}

void CameraController::autofocus()
{
    // CAM_FocusMode Auto (standard VISCA)
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x38));
    cmd.append(char(0x02));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::focusInfinity()
{
    // Focus to far limit (common way)
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x48));
    cmd.append(char(0x0F));
    cmd.append(char(0x0F));
    cmd.append(char(0x0F));
    cmd.append(char(0x0F));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::setAeBrightMode()
{
    // CAM_AE Bright: 8x 01 04 39 0D FF (MC-108-M3, Approval sheet p.35)
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x39));
    cmd.append(char(0x0D));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::brightnessUp()
{
    setAeBrightMode();
    // Камера буферизует одну команду; шаг яркости — после смены AE.
    QTimer::singleShot(80, this, [this]() {
        QByteArray cmd;
        cmd.append(char(0x81));
        cmd.append(char(0x01));
        cmd.append(char(0x04));
        cmd.append(char(0x0D));
        cmd.append(char(0x02));
        cmd.append(char(0xFF));
        sendVisca(cmd);
    });
}

void CameraController::brightnessDown()
{
    setAeBrightMode();
    QTimer::singleShot(80, this, [this]() {
        QByteArray cmd;
        cmd.append(char(0x81));
        cmd.append(char(0x01));
        cmd.append(char(0x04));
        cmd.append(char(0x0D));
        cmd.append(char(0x03));
        cmd.append(char(0xFF));
        sendVisca(cmd);
    });
}

void CameraController::startZoomPolling()
{
    m_zoomPollTimer->start(2500);
}

void CameraController::stopZoomPolling()
{
    m_zoomPollTimer->stop();
}

void CameraController::pollZoomPosition()
{
    // CAM_ZoomPosInq
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x09));
    cmd.append(char(0x04));
    cmd.append(char(0x47));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

QByteArray CameraController::buildViscaCommand(const QByteArray& cmd)
{
    return cmd; // For now just return as-is. Can add address if needed.
}

void CameraController::sendVisca(const QByteArray& cmd)
{
    if (m_udp) {
        m_udp->sendPacket(m_targetId, cmd);
    }
}

void CameraController::handleIncomingPacket(uint8_t sourceId, const QByteArray& payload)
{
    if (sourceId != m_targetId || payload.size() < 3)
        return;

    // VISCA-ответ должен начинаться с 0x90 и заканчиваться 0xFF
    if (static_cast<uint8_t>(payload[0]) != 0x90 ||
        static_cast<uint8_t>(payload.back()) != 0xFF)
        return;

    const uint8_t second = static_cast<uint8_t>(payload[1]);

    // ACK
    if ((second & 0xF0) == 0x40) {
        // emit ackReceived();  // при необходимости
        return;
    }

    // Completion (обычная команда)
    if ((second & 0xF0) == 0x50 && payload.size() == 3) {
        // emit completionReceived();
        return;
    }

    // Ответ на CAM_ZoomPosInq: 90 50 0p 0q 0r 0s FF
    if ((second & 0xF0) == 0x50 && payload.size() == 7)
    {
        // Дополнительная проверка, что это именно 4 полубайта позиции
        bool isZoomPosReply =
            (static_cast<uint8_t>(payload[2]) & 0xF0) == 0x00 &&
            (static_cast<uint8_t>(payload[3]) & 0xF0) == 0x00 &&
            (static_cast<uint8_t>(payload[4]) & 0xF0) == 0x00 &&
            (static_cast<uint8_t>(payload[5]) & 0xF0) == 0x00;

        if (isZoomPosReply)
        {
            uint16_t zoomPos =
                ((static_cast<uint8_t>(payload[2]) & 0x0F) << 12) |
                ((static_cast<uint8_t>(payload[3]) & 0x0F) <<  8) |
                ((static_cast<uint8_t>(payload[4]) & 0x0F) <<  4) |
                (static_cast<uint8_t>(payload[5]) & 0x0F);


            m_currentMagnification = magnificationFromPosition(zoomPos);
            emit zoomPositionUpdated(m_currentMagnification);
            return;
        }
    }

    // Error
    if ((second & 0xF0) == 0x60) {
        int errCode = second & 0x0F;
        emit error(QString("VISCA error 0x%1").arg(errCode, 2, 16, QChar('0')));
        return;
    }
}

float CameraController::magnificationFromPosition(uint16_t zoomPos) const
{
    if (m_zoomCurvePos.size() < 2)
        return 1.0f;
    if (zoomPos <= m_zoomCurvePos.first())
        return m_zoomCurveMag.first();
    if (zoomPos >= m_zoomCurvePos.last())
        return m_zoomCurveMag.last();

    for (int i = 0; i < m_zoomCurvePos.size() - 1; ++i) {
        const uint16_t p0 = m_zoomCurvePos[i];
        const uint16_t p1 = m_zoomCurvePos[i + 1];
        if (zoomPos >= p0 && zoomPos <= p1) {
            const float span = static_cast<float>(p1 - p0);
            if (span <= 0.0f)
                return m_zoomCurveMag[i];
            const float t = static_cast<float>(zoomPos - p0) / span;
            return m_zoomCurveMag[i] + t * (m_zoomCurveMag[i + 1] - m_zoomCurveMag[i]);
        }
    }
    return m_zoomCurveMag.last();
}