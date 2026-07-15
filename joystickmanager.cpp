#include "joystickmanager.h"
#include <QDebug>

JoystickManager::JoystickManager(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10); // 100 Hz polling
    connect(m_pollTimer, &QTimer::timeout, this, &JoystickManager::pollJoystick);
}

JoystickManager::~JoystickManager()
{
    shutdown();
}

bool JoystickManager::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);
    m_deviceIndex = settings.value("Joystick/device_index", -1).toInt();
    m_invertYaw = settings.value("Joystick/invert_yaw", false).toBool();
    m_invertPitch = settings.value("Joystick/invert_pitch", false).toBool();
    return true;
}

bool JoystickManager::initialize()
{
    if (SDL_Init(SDL_INIT_JOYSTICK) < 0) {
        qWarning() << "SDL_Init failed:" << SDL_GetError();
        return false;
    }

    SDL_JoystickEventState(SDL_ENABLE);

    int numJoysticks = SDL_NumJoysticks();
    qDebug() << "SDL found" << numJoysticks << "joystick(s)";

    if (numJoysticks > 0) {
        int indexToOpen = (m_deviceIndex >= 0 && m_deviceIndex < numJoysticks) ? m_deviceIndex : 0;
        openJoystick(indexToOpen);
    }

    m_pollTimer->start();
    return true;
}

void JoystickManager::shutdown()
{
    m_pollTimer->stop();
    closeJoystick();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

bool JoystickManager::isConnected() const
{
    return m_joystick != nullptr;
}

int JoystickManager::getDeviceIndex() const
{
    return m_deviceIndex;
}

float JoystickManager::getAxisYaw() const
{
    if (!m_joystick) return 0.0f;
    Sint16 raw = SDL_JoystickGetAxis(m_joystick, 0); // X axis usually
    float value = raw / 32767.0f;
    return m_invertYaw ? -value : value;
}

float JoystickManager::getAxisPitch() const
{
    if (!m_joystick) return 0.0f;
    Sint16 raw = SDL_JoystickGetAxis(m_joystick, 1); // Y axis usually
    float value = raw / 32767.0f;
    return m_invertPitch ? -value : value;
}

void JoystickManager::openJoystick(int index)
{
    closeJoystick();
    m_joystick = SDL_JoystickOpen(index);
    if (m_joystick) {
        m_deviceIndex = index;
        qDebug() << "Joystick opened:" << SDL_JoystickName(m_joystick);
        emit connectedChanged(true);
    }
}

void JoystickManager::closeJoystick()
{
    if (m_joystick) {
        SDL_JoystickClose(m_joystick);
        m_joystick = nullptr;
        emit connectedChanged(false);
    }
}

void JoystickManager::pollJoystick()
{
    SDL_JoystickUpdate();

    // Simple hotplug detection (basic)
    if (!m_joystick && SDL_NumJoysticks() > 0) {
        openJoystick(0);
    } else if (m_joystick && SDL_JoystickGetAttached(m_joystick) == SDL_FALSE) {
        closeJoystick();
    }

    // Emit button events (simple polling for now)
    if (m_joystick) {
        for (int i = 0; i < SDL_JoystickNumButtons(m_joystick); ++i) {
            if (SDL_JoystickGetButton(m_joystick, i)) {
                emit buttonPressed(i);
            }
        }
    }
}
