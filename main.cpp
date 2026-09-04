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
    QApplication a(argc, argv);



    a.setApplicationName("GSP Control");
    a.setOrganizationName("GSP");

    MainWindow w;
    w.show();
    return a.exec();
}
