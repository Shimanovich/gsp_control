#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#ifndef QT_NO_ENTRYPOINT
#define QT_NO_ENTRYPOINT
#endif

#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_ASSUME_STDERR_HAS_CONSOLE", "1");
    qputenv("QT_LOGGING_TO_CONSOLE", "1");
    QApplication a(argc, argv);
    qDebug() << "boot";

    a.setApplicationName("GSP Control");
    a.setOrganizationName("GSP");

    MainWindow w;
    w.show();
    return a.exec();
}
