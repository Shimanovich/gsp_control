#include "keyboardmanager.h"
#include <QDebug>

KeyboardManager::KeyboardManager(QObject *parent)
    : QObject(parent)
{
}

bool KeyboardManager::loadSettings(const QString& iniPath)
{
    QSettings settings(iniPath, QSettings::IniFormat);

    m_keyToButton.clear();

    auto add = [this, &settings](const QString& keyName, int defaultKey, int button) {
        int key = settings.value(QString("Keyboard/%1").arg(keyName), defaultKey).toInt();
        m_keyToButton.insert(key, button);
    };

    // Кнопки (совпадают с case в onJoystickButtonPressed)
    add("zoom_in",          Qt::Key_Plus,     9);
    add("zoom_out",         Qt::Key_Minus,    7);
    add("zoom_next",        Qt::Key_PageUp,   6);
    add("zoom_prev",        Qt::Key_PageDown, 8);
    add("brightness_up",    Qt::Key_0,        10);
    add("brightness_down",  Qt::Key_9,        12);
    add("autofocus",        Qt::Key_F,        2);
    add("focus_infinity",   Qt::Key_I,        3);
    add("track",            Qt::Key_T,        settings.value("Joystick/button_track", 4).toInt());

    // Оси (стрелки)
    m_keyYawLeft   = settings.value("Keyboard/yaw_left",   Qt::Key_Left).toInt();
    m_keyYawRight  = settings.value("Keyboard/yaw_right",  Qt::Key_Right).toInt();
    m_keyPitchUp   = settings.value("Keyboard/pitch_up",   Qt::Key_Up).toInt();
    m_keyPitchDown = settings.value("Keyboard/pitch_down", Qt::Key_Down).toInt();

    qDebug() << "KeyboardManager: loaded" << m_keyToButton.size() << "button mappings + axes";
    return true;
}

void KeyboardManager::installOn(QObject* target)
{
    if (m_installedOn)
        uninstall();

    if (target) {
        target->installEventFilter(this);
        m_installedOn = target;
    }
}

void KeyboardManager::uninstall()
{
    if (m_installedOn) {
        m_installedOn->removeEventFilter(this);
        m_installedOn = nullptr;
    }
    m_pressedButtons.clear();
    m_left = m_right = m_up = m_down = false;
    updateAxes();
}

bool KeyboardManager::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        handleKeyEvent(keyEvent, event->type() == QEvent::KeyPress);
        // События осей и кнопок потребляем, чтобы стрелки не уходили в навигацию фокуса
        if (m_keyToButton.contains(keyEvent->key()) ||
            keyEvent->key() == m_keyYawLeft  ||
            keyEvent->key() == m_keyYawRight ||
            keyEvent->key() == m_keyPitchUp  ||
            keyEvent->key() == m_keyPitchDown)
        {
            return true;   // consume
        }
    }
    return QObject::eventFilter(obj, event);
}

void KeyboardManager::handleKeyEvent(QKeyEvent* keyEvent, bool isPress)
{
    if (keyEvent->isAutoRepeat())
        return;

    const int key = keyEvent->key();

    // --- Оси ---
    bool axisChanged = false;
    if (key == m_keyYawLeft)  { m_left  = isPress; axisChanged = true; }
    if (key == m_keyYawRight) { m_right = isPress; axisChanged = true; }
    if (key == m_keyPitchUp)  { m_up    = isPress; axisChanged = true; }
    if (key == m_keyPitchDown){ m_down  = isPress; axisChanged = true; }

    if (axisChanged) {
        updateAxes();
        return;
    }

    // --- Кнопки ---
    if (!m_keyToButton.contains(key))
        return;

    const int button = m_keyToButton.value(key);

    if (isPress) {
        if (!m_pressedButtons.contains(button)) {
            m_pressedButtons.insert(button);
            emit buttonPressed(button);
        }
    } else {
        if (m_pressedButtons.contains(button)) {
            m_pressedButtons.remove(button);
            emit buttonReleased(button);
        }
    }
}

void KeyboardManager::updateAxes()
{
    keyYaw = 0.0f;
    if (m_right) keyYaw += 1.0f;
    if (m_left)  keyYaw -= 1.0f;

    keyPitch = 0.0f;
    if (m_down) keyPitch += 1.0f;
    if (m_up)   keyPitch -= 1.0f;   // знак согласован с −getAxisPitch() джойстика
}