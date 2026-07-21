#ifndef JOYSTICKMANAGER_H
#define JOYSTICKMANAGER_H

#include <QObject>
#include <QTimer>
#include <SDL2/SDL.h>
#include <QSettings>

class JoystickManager : public QObject
{
    Q_OBJECT
public:
    explicit JoystickManager(QObject *parent = nullptr);
    ~JoystickManager();

    bool loadSettings(const QString& iniPath);
    bool initialize();
    void shutdown();

    bool isConnected() const;
    int getDeviceIndex() const;

    // Get normalized axis values (-1.0 .. 1.0)
    float getAxisYaw() const;
    float getAxisPitch() const;

signals:
    void connectedChanged(bool connected);
    void buttonPressed(int button);   // фронт 0→1
    void buttonReleased(int button);  // фронт 1→0
    void axisMoved(int axis, float value);

private slots:
    void pollJoystick();

private:

    QVector<bool> m_prevButtonStates;
    SDL_Joystick* m_joystick = nullptr;
    int m_deviceIndex = -1;
    bool m_invertYaw = false;
    bool m_invertPitch = false;

    QTimer* m_pollTimer = nullptr;

    void openJoystick(int index);
    void closeJoystick();
};

#endif // JOYSTICKMANAGER_H