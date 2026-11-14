// WSPR Solar 2 - Qt 6 port of the C# WSPR Solar (solar and geomagnetic conditions from NOAA, kept in
// the wspr_sol database for WSPR Scheduler).
#include "appfont.h"
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

#ifndef WSPRSOLAR_VERSION
#define WSPRSOLAR_VERSION "0"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("WSPR Solar 2");
    QApplication::setOrganizationName("G4GCI");
    QApplication::setApplicationVersion(WSPRSOLAR_VERSION);
    QApplication::setWindowIcon(QIcon(":/wsprsolar.ico"));
    applyAppFont();

    MainWindow w;
    w.show();
    w.start();
    return app.exec();
}
