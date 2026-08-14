#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QKeyEvent>

class KeyboardManager : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardManager(QObject *parent = nullptr);
    ~KeyboardManager() override = default;

    bool loadSettings(const QString& iniPath);
    void installOn(QObject* target);
    void uninstall();

    float getAxisYaw() const { return keyYaw; }
    float getAxisPitch() const { return keyPitch; }

signals:
    void buttonPressed(int button);
    void buttonReleased(int button);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    float keyYaw = 0.0f;
    float keyPitch = 0.0f;

    // Кнопки (как у джойстика)
    QHash<int, int> m_keyToButton;
    QSet<int> m_pressedButtons;

    // Оси (стрелки)
    int m_keyYawLeft  = Qt::Key_Left;
    int m_keyYawRight = Qt::Key_Right;
    int m_keyPitchUp  = Qt::Key_Up;
    int m_keyPitchDown= Qt::Key_Down;

    bool m_left  = false;
    bool m_right = false;
    bool m_up    = false;
    bool m_down  = false;

    QObject* m_installedOn = nullptr;

    void handleKeyEvent(QKeyEvent* keyEvent, bool isPress);
    void updateAxes();
};

#endif // KEYBOARDMANAGER_H