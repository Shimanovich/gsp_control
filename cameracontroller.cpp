#include "cameracontroller.h"
#include "jetsoncontroller.h"
#include <QDebug>

CameraController::CameraController(JetsonController* jetson, QObject *parent)
    : QObject(parent), m_jetson(jetson)
{
    m_zoomPollTimer = new QTimer(this);
    connect(m_zoomPollTimer, &QTimer::timeout, this, &CameraController::pollZoom);

    if (m_jetson) {
        connect(m_jetson, &JetsonController::cameraZoomUpdated, this, &CameraController::onCameraZoomUpdated);
        connect(m_jetson, &JetsonController::cameraStatusUpdated, this, &CameraController::onCameraStatusUpdated);
    }
}

bool CameraController::loadSettings(const QString&)
{
    return true;
}

void CameraController::zoomIn() { if (m_jetson) m_jetson->sendCamZoomIn(); }
void CameraController::zoomOut() { if (m_jetson) m_jetson->sendCamZoomOut(); }
void CameraController::zoomStop() { if (m_jetson) m_jetson->sendCamZoomStop(); }
void CameraController::setZoomPosition(int index) { if (m_jetson) m_jetson->sendCamZoomPos(index); }
void CameraController::autofocus() { if (m_jetson) m_jetson->sendCamAutofocus(); }
void CameraController::focusInfinity() { if (m_jetson) m_jetson->sendCamFocusInfinity(); }
void CameraController::brightnessUp() { if (m_jetson) m_jetson->sendCamBrightnessUp(); }
void CameraController::brightnessDown() { if (m_jetson) m_jetson->sendCamBrightnessDown(); }
void CameraController::setAeBrightMode() { if (m_jetson) m_jetson->sendCamAeBright(); }
void CameraController::tvFps25() { if (m_jetson) m_jetson->sendCamTvFps25(); }
void CameraController::tvFps30() { if (m_jetson) m_jetson->sendCamTvFps30(); }
void CameraController::tvPwrReset() { if (m_jetson) m_jetson->sendCamTvPwrReset(); }

void CameraController::startZoomPolling()
{
    if (m_zoomPollTimer && !m_zoomPollTimer->isActive())
        m_zoomPollTimer->start(1000);
}
void CameraController::stopZoomPolling()
{
    if (m_zoomPollTimer) m_zoomPollTimer->stop();
}
void CameraController::pollZoom()
{
    if (m_jetson) m_jetson->sendCamStatus();
}

void CameraController::onCameraZoomUpdated(float mag)
{
    m_currentMagnification = mag;
    emit zoomPositionUpdated(mag);
}
void CameraController::onCameraStatusUpdated(int, float mag, int)
{
    m_currentMagnification = mag;
    emit zoomPositionUpdated(mag);
}
