#include "cameracontroller.h"
#include "udpcommunicator.h"
#include <QDebug>

CameraController::CameraController(UdpCommunicator* udp, QObject *parent)
    : QObject(parent), m_udp(udp)
{
    m_zoomPollTimer = new QTimer(this);
    connect(m_zoomPollTimer, &QTimer::timeout, this, &CameraController::pollZoomPosition);
}

bool CameraController::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);
    m_targetId = settings.value("TargetIDs/camera", 11).toUInt();

    // Load fixed zoom positions (example values)
    QStringList posList = settings.value("Camera/zoom_positions", "0x0000,0x1000,0x2000,0x3000,0x4000").toString().split(',');
    m_zoomDirectCommands.clear();
    for (const QString& pos : posList) {
        bool ok;
        uint16_t val = pos.toUInt(&ok, 16);
        if (ok) {
            QByteArray cmd;
            cmd.append(char(0x81)); // VISCA address
            cmd.append(char(0x01));
            cmd.append(char(0x04));
            cmd.append(char(0x47));
            cmd.append(char((val >> 12) & 0x0F));
            cmd.append(char((val >> 8) & 0x0F));
            cmd.append(char((val >> 4) & 0x0F));
            cmd.append(char(val & 0x0F));
            cmd.append(char(0xFF));
            m_zoomDirectCommands.append(cmd);
        }
    }

    m_zoomNames = settings.value("Camera/zoom_position_names", "1x,2x,4x,8x,10x").toString().split(',');
    m_currentZoomIndex = settings.value("Camera/default_zoom_index", 0).toInt();

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

void CameraController::setZoomPosition(int index)
{
    if (index < 0 || index >= m_zoomDirectCommands.size()) return;
    m_currentZoomIndex = index;
    sendVisca(m_zoomDirectCommands[index]);
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

void CameraController::brightnessUp()
{
    // CAM_Bright Up (from datasheet page 35)
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x0D));
    cmd.append(char(0x02));
    cmd.append(char(0xFF));
    sendVisca(cmd);
}

void CameraController::brightnessDown()
{
    QByteArray cmd;
    cmd.append(char(0x81));
    cmd.append(char(0x01));
    cmd.append(char(0x04));
    cmd.append(char(0x0D));
    cmd.append(char(0x03));
    cmd.append(char(0xFF));
    sendVisca(cmd);
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
