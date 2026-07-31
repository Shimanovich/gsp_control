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
    void installOn(QObject* target);   // обычно MainWindow
    void uninstall();

signals:
    void buttonPressed(int button);    // те же номера, что у джойстика
    void buttonReleased(int button);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Qt::Key → виртуальный номер кнопки (совпадает с case в MainWindow)
    QHash<int, int> m_keyToButton;

    // Какие виртуальные кнопки сейчас зажаты (для корректных фронтов)
    QSet<int> m_pressedButtons;

    QObject* m_installedOn = nullptr;

    void handleKeyEvent(QKeyEvent* keyEvent, bool isPress);
};

#endif // KEYBOARDMANAGER_H