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

    // Значения по умолчанию (можно менять в config.ini)
    // Формат: Qt::Key_* → номер кнопки, который уже обрабатывается в MainWindow
    auto add = [this, &settings](const QString& keyName, int defaultKey, int button) {
        int key = settings.value(QString("Keyboard/%1").arg(keyName), defaultKey).toInt();
        m_keyToButton.insert(key, button);
    };

    // Совпадает с case в onJoystickButtonPressed:
    // 9 = zoomIn, 7 = zoomOut, 6 = next, 8 = prev,
    // 10 = brightUp, 12 = brightDown,
    // 1 = shoot, 2 = autofocus, 3 = focusInfinity
    add("zoom_in",          Qt::Key_Plus,     9);
    add("zoom_out",         Qt::Key_Minus,    7);
    add("zoom_next",        Qt::Key_PageUp,   6);
    add("zoom_prev",        Qt::Key_PageDown, 8);
    add("brightness_up",    Qt::Key_Up,      10);
    add("brightness_down",  Qt::Key_Down,    12);
    //add("shoot",            Qt::Key_Space,    1);
    add("autofocus",        Qt::Key_F,        2);
    add("focus_infinity",   Qt::Key_I,        3);

    qDebug() << "KeyboardManager: loaded" << m_keyToButton.size() << "key mappings";
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
}

bool KeyboardManager::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        handleKeyEvent(keyEvent, event->type() == QEvent::KeyPress);
        // не блокируем событие — оставляем возможность другим обработчикам
    }
    return QObject::eventFilter(obj, event);
}

void KeyboardManager::handleKeyEvent(QKeyEvent* keyEvent, bool isPress)
{
    // Игнорируем автоповтор (иначе zoomIn будет сыпаться)
    if (keyEvent->isAutoRepeat())
        return;

    const int key = keyEvent->key();
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