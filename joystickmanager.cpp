#include "joystickmanager.h"
#include <QDebug>

JoystickManager::JoystickManager(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(100); // 100 Hz polling
    connect(m_pollTimer, &QTimer::timeout, this, &JoystickManager::pollJoystick);
}

JoystickManager::~JoystickManager()
{
    shutdown();
}



float JoystickManager::mapValueClamped(float value, float inMin, float inMax, float outMin, float outMax) {
    if (qFuzzyCompare(inMax, inMin)) {
        return outMin;
    }
    float result = outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);

    // Ensure outMin is always the lower bound for qBound to work properly
    float actualMin = qMin(outMin, outMax);
    float actualMax = qMax(outMin, outMax);

    return qBound(actualMin, result, actualMax);
}

bool JoystickManager::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);
    m_iniPath = iniPath;
    m_deviceIndex = settings.value("Joystick/device_index", -1).toInt();
    m_invertYaw = settings.value("Joystick/invert_yaw", false).toBool();
    m_invertPitch = settings.value("Joystick/invert_pitch", false).toBool();
    m_zeroYaw = settings.value("Joystick/zero_yaw", 0.0).toFloat();
    m_zeroPitch = settings.value("Joystick/zero_pitch", 0.0).toFloat();
    return true;
}

float JoystickManager::readNormalized(int axis) const
{
    if (!m_joystick) return 0.0f;
    return SDL_JoystickGetAxis(m_joystick, axis) / 32767.0f;
}

bool JoystickManager::calibrateAxes()
{
    if (!m_joystick)
        return false;
    SDL_JoystickUpdate();
    m_zeroYaw = readNormalized(0);
    m_zeroPitch = readNormalized(1);
    if (!m_iniPath.isEmpty()) {
        QSettings s(m_iniPath, QSettings::IniFormat);
        s.setValue("Joystick/zero_yaw", m_zeroYaw);
        s.setValue("Joystick/zero_pitch", m_zeroPitch);
        s.sync();
    }
    qDebug() << "Joystick zero:" << m_zeroYaw << m_zeroPitch;
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

float JoystickManager::getAxisYaw()
{
    if (!m_joystick) return 0.0f;
    float value = qBound(-1.0f, readNormalized(0) - m_zeroYaw, 1.0f);
    return m_invertYaw ? -value : value;
}

float JoystickManager::getAxisPitch()
{
    if (!m_joystick) return 0.0f;
    float value = qBound(-1.0f, readNormalized(1) - m_zeroPitch, 1.0f);
    return m_invertPitch ? -value : value;
}

void JoystickManager::getHat(int& dx, int& dy)
{
    dx = 0;
    dy = 0;
    if (!m_joystick || SDL_JoystickNumHats(m_joystick) < 1)
        return;
    const Uint8 hat = SDL_JoystickGetHat(m_joystick, 0);
    if (hat & SDL_HAT_RIGHT) dx = 1;
    else if (hat & SDL_HAT_LEFT) dx = -1;
    if (hat & SDL_HAT_DOWN) dy = 1;
    else if (hat & SDL_HAT_UP) dy = -1;
}

void JoystickManager::openJoystick(int index)
{
    closeJoystick();
    m_joystick = SDL_JoystickOpen(index);
    if (m_joystick) {
        m_deviceIndex = index;
        const int n = SDL_JoystickNumButtons(m_joystick);
        m_prevButtonStates = QVector<bool>(n, false);   // сброс
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

    // hotplug (как было)
    if (!m_joystick && SDL_NumJoysticks() > 0) {
        openJoystick(0);
    } else if (m_joystick && SDL_JoystickGetAttached(m_joystick) == SDL_FALSE) {
        closeJoystick();
        return;
    }

    if (!m_joystick) return;

    const int n = SDL_JoystickNumButtons(m_joystick);
    if (m_prevButtonStates.size() != n)
        m_prevButtonStates = QVector<bool>(n, false);

    for (int i = 0; i < n; ++i) {
        const bool pressed = SDL_JoystickGetButton(m_joystick, i);
        if (pressed && !m_prevButtonStates[i]) {
            emit buttonPressed(i);          // единожды при нажатии
        } else if (!pressed && m_prevButtonStates[i]) {
            emit buttonReleased(i);         // единожды при отпускании
        }
        m_prevButtonStates[i] = pressed;
    }
}
