# ============================================================
#  gsp_control
# ------------------------------------------------------------
#  Переключатель режима сборки (меняйте ТОЛЬКО эту строку):
#
#    app  — отдельное приложение для отладки
#    lib  — shared-библиотека для хоста
# ============================================================
GSP_BUILD_AS = app


QT       += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG  += c++17
CONFIG  += skip_target_version_ext
DEFINES += SDL_MAIN_HANDLED
DEFINES += QT_NO_ENTRYPOINT
DEFINES += QT_DEPRECATED_WARNINGS

TARGET = gsp_control

# ------------------------------------------------------------
#  Режим: приложение или библиотека
# ------------------------------------------------------------
equals(GSP_BUILD_AS, app) {
    CONFIG += console
    TEMPLATE = app
    DEFINES += GSP_CONTROL_APP
    SOURCES += main.cpp
    message(Building gsp_control as APPLICATION)
} else {
    TEMPLATE = lib
    CONFIG  += shared
    DEFINES += GSP_CONTROL_LIBRARY
    DESTDIR  = $$PWD
    message(Building gsp_control as LIBRARY)
}

# ------------------------------------------------------------
#  Зависимости: SDL2
#  Измените путь под свою систему
# ------------------------------------------------------------
win32 {
    SDL2_PATH = d:/work/SDL2-2.0.14/x86_64-w64-mingw32/
    INCLUDEPATH += $$SDL2_PATH/include
    LIBS        += -L$$SDL2_PATH/lib -lSDL2
}
unix {
    LIBS += -lSDL2
}

# ------------------------------------------------------------
#  Зависимости: FFmpeg
# ------------------------------------------------------------
win32 {
    FFMPEG_PATH = d:/work/6176/ffmpeg-8.1.2-full_build-shared
    INCLUDEPATH += $$FFMPEG_PATH/include
    LIBS += -L$$FFMPEG_PATH/lib \
            -lavcodec -lavutil -lswscale -lavformat
    LIBS += -lws2_32
}
unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += libavcodec libavutil libswscale libavformat
}

# ------------------------------------------------------------
#  Общие исходники
# ------------------------------------------------------------
SOURCES += \
    mainwindow.cpp \
    udpcommunicator.cpp \
    joystickmanager.cpp \
    keyboardmanager.cpp \
    cameracontroller.cpp \
    gyrocontroller.cpp \
    rangefindercontroller.cpp \
    udpReceiveAndDecode.cpp \
    jepprotocol.cpp \
    jetsoncontroller.cpp

HEADERS += \
    gsp_control_global.h \
    mainwindow.h \
    udpcommunicator.h \
    joystickmanager.h \
    keyboardmanager.h \
    cameracontroller.h \
    gyrocontroller.h \
    rangefindercontroller.h \
    simplebgc_protocol.h \
    udpReceiveAndDecode.h \
    jepprotocol.h \
    jetsoncontroller.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc

# ------------------------------------------------------------
#  Копирование dll/a в корень — только в режиме библиотеки
# ------------------------------------------------------------
equals(GSP_BUILD_AS, lib) {
    win32 {
        COPY_DST = $$shell_path($$PWD)
        COPY_SRC_DIR = $$shell_path($$OUT_PWD)

        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\gsp_control.dll\" copy /Y \"$$COPY_SRC_DIR\\gsp_control.dll\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\libgsp_control.a\" copy /Y \"$$COPY_SRC_DIR\\libgsp_control.a\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\libgsp_control.dll.a\" copy /Y \"$$COPY_SRC_DIR\\libgsp_control.dll.a\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\gsp_control.lib\" copy /Y \"$$COPY_SRC_DIR\\gsp_control.lib\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\debug\\gsp_control.dll\" copy /Y \"$$COPY_SRC_DIR\\debug\\gsp_control.dll\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\debug\\libgsp_control.a\" copy /Y \"$$COPY_SRC_DIR\\debug\\libgsp_control.a\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\release\\gsp_control.dll\" copy /Y \"$$COPY_SRC_DIR\\release\\gsp_control.dll\" \"$$COPY_DST\") $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += \
            $$quote(cmd /c if exist \"$$COPY_SRC_DIR\\release\\libgsp_control.a\" copy /Y \"$$COPY_SRC_DIR\\release\\libgsp_control.a\" \"$$COPY_DST\")
    }

    unix {
        QMAKE_POST_LINK += $$quote(cp -f $$OUT_PWD/libgsp_control.so* $$PWD/ 2>/dev/null || true)
        QMAKE_POST_LINK += $$quote(; cp -f $$OUT_PWD/libgsp_control.a $$PWD/ 2>/dev/null || true)
    }
}
