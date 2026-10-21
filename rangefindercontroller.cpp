#include "rangefindercontroller.h"
#include "jetsoncontroller.h"
#include <QDebug>

RangefinderController::RangefinderController(JetsonController* jetson, QObject *parent)
    : QObject(parent), m_jetson(jetson)
{
    if (m_jetson) {
        connect(m_jetson, &JetsonController::rangeMeasurementReceived, this, &RangefinderController::onRangeMeasurement);
        connect(m_jetson, &JetsonController::rangeStatusUpdated, this, &RangefinderController::onRangeStatus);
    }
}

bool RangefinderController::loadSettings(const QString&) { return true; }

void RangefinderController::shoot() { if (m_jetson) m_jetson->sendRangeShoot(); }
void RangefinderController::stop() { if (m_jetson) m_jetson->sendRangeStop(); }
void RangefinderController::continuous(int periodMs) { if (m_jetson) m_jetson->sendRangeContinuous(periodMs); }
void RangefinderController::selftest() { if (m_jetson) m_jetson->sendRangeSelftest(); }
void RangefinderController::setMinDist(int meters) { if (m_jetson) m_jetson->sendRangeSetMinDist(meters); }
void RangefinderController::setTargetMode(int mode) { if (m_jetson) m_jetson->sendRangeSetTargetMode(mode); }

void RangefinderController::onRangeMeasurement(float d1, float, float, int status)
{
    emit measurementReceived(d1, static_cast<uint8_t>(status));
}
void RangefinderController::onRangeStatus(float last, bool ok)
{
    emit statusUpdated(ok ? QStringLiteral("OK") : QStringLiteral("Error"));
    if (last > 0) emit measurementReceived(last, 0);
}
