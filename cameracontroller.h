#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QSettings>
#include <QVector>

class JetsonController;

class CameraController : public QObject
{
    Q_OBJECT
public:
    explicit CameraController(JetsonController* jetson, QObject *parent = nullptr);

    bool loadSettings(const QString& iniPath);

    void zoomIn();
    void zoomOut();
    void zoomStop();
    void setZoomPosition(int index);
    void autofocus();
    void focusInfinity();
    void brightnessUp();
    void brightnessDown();
    void setAeBrightMode();
    void tvFps25();
    void tvFps30();
    void tvPwrReset();

    void startZoomPolling();
    void stopZoomPolling();

    float currentMagnification() const { return m_currentMagnification; }

signals:
    void zoomPositionUpdated(float magnification);
    void error(const QString& msg);

private slots:
    void onCameraZoomUpdated(float mag);
    void onCameraStatusUpdated(int zoomPos, float mag, int fps);
    void pollZoom();

private:
    JetsonController* m_jetson = nullptr;
    QTimer* m_zoomPollTimer = nullptr;
    float m_currentMagnification = 1.0f;
    int m_currentZoomIndex = 0;
};

#endif // CAMERACONTROLLER_H
