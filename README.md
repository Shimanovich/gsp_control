# gsp_control — Qt Library

Библиотека управления гиростабилизированной платформой (GSP).

Содержит класс `MainWindow` (наследует `QMainWindow`), который можно использовать как виджет в любом Qt-приложении.

## Зависимости

- Qt 5.15+ / Qt 6 (modules: core, gui, widgets, network)
- SDL2
- FFmpeg (libavcodec, libavutil, libswscale, libavformat)

## Сборка в Qt Creator

1. Откройте `gsp_control.pro` в Qt Creator.
2. **Важно**: отредактируйте пути к SDL2 и FFmpeg в файле `.pro` под свою систему.
3. Выберите конфигурацию (Debug / Release).
4. Соберите проект (Ctrl+B).

После сборки получите:
- Windows: `gsp_control.dll` + `gsp_control.lib` (или `.a`)
- Linux: `libgsp_control.so`

## Использование в другом проекте

### 1. В `.pro` хост-приложения добавьте:

```pro
LIBS += -L/path/to/gsp_control_lib -lgsp_control
INCLUDEPATH += /path/to/gsp_control_lib

# Не забудьте те же зависимости SDL2 + FFmpeg
```

### 2. В коде:

```cpp
#include "mainwindow.h"

// ...
MainWindow *gsp = new MainWindow(this);
// можно добавить в layout:
layout->addWidget(gsp);
```

Или как центральный виджет:

```cpp
setCentralWidget(new MainWindow(this));
```

## Конфигурация

Файл `config.ini` должен лежать рядом с исполняемым файлом (или укажите путь через `QSettings`).

## Структура

```
gsp_control_lib/
├── gsp_control.pro          ← файл проекта библиотеки
├── gsp_control_global.h     ← макросы экспорта
├── mainwindow.h / .cpp / .ui
├── udpcommunicator.*
├── joystickmanager.*
├── keyboardmanager.*
├── cameracontroller.*
├── gyrocontroller.*
├── rangefindercontroller.*
├── udpReceiveAndDecode.*
├── simplebgc_protocol.h
├── config.ini
└── resources.qrc
```

## Примечания

- Класс `MainWindow` экспортируется через `GSP_CONTROL_EXPORT`.
- Видео-декодер содержит Windows-специфичный код (`HANDLE`). На Linux потребуется доработка.
- Для static-библиотеки измените в `.pro`: `CONFIG += static` вместо `shared`.
