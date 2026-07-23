QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

DEFINES += QT_NO_ENTRYPOINT

DEFINES += SDL_MAIN_HANDLED

CONFIG += c++17

SDL2_PATH = d:/work/SDL2-2.0.14/x86_64-w64-mingw32/

INCLUDEPATH += $$SDL2_PATH/include

LIBS += -L$$SDL2_PATH/lib -lSDL2

# SDL2 support
unix: LIBS += -lSDL2
win32: LIBS += -lSDL2

win32 {
    INCLUDEPATH += d:/work/6176/ffmpeg-8.1.2-full_build-shared/include/
    LIBS += -Ld:/work/6176/ffmpeg-8.1.2-full_build-shared/lib/ \
            -lavcodec -lavutil -lswscale -lavformat
}
unix {
    # Вариант через pkg-config (рекомендуется)
    CONFIG += link_pkgconfig
    PKGCONFIG += libavcodec libavutil libswscale libavformat
    # Или вручную:
    # INCLUDEPATH += /usr/include/ffmpeg
    # LIBS += -lavcodec -lavutil -lswscale -lavformat
}

TARGET = gsp_control
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    udpcommunicator.cpp \
    joystickmanager.cpp \
    cameracontroller.cpp \
    gyrocontroller.cpp \
    rangefindercontroller.cpp \
    udpReceiveAndDecode.cpp

HEADERS += \
    mainwindow.h \
    udpcommunicator.h \
    joystickmanager.h \
    cameracontroller.h \
    gyrocontroller.h \
    rangefindercontroller.h \
    simplebgc_protocol.h \
    udpReceiveAndDecode.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc