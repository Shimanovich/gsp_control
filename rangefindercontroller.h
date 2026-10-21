#ifndef RANGEFINDERCONTROLLER_H
#define RANGEFINDERCONTROLLER_H

#include <QObject>

class JetsonController;

class RangefinderController : public QObject
{
    Q_OBJECT
public:
    explicit RangefinderController(JetsonController* jetson, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);
    void shoot();
    void stop();
    void continuous(int periodMs);
    void selftest();
    void setMinDist(int meters);
    void setTargetMode(int mode);

signals:
    void measurementReceived(float distanceMeters, uint8_t status);
    void statusUpdated(const QString& status);

private slots:
    void onRangeMeasurement(float d1, float d2, float d3, int status);
    void onRangeStatus(float last, bool ok);

private:
    JetsonController* m_jetson = nullptr;
};

#endif // RANGEFINDERCONTROLLER_H
