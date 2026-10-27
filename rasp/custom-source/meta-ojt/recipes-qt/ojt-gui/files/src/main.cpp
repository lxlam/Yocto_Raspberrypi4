/*
 * ojt-gui - Qt5 HMI for the USB video player
 *
 * Run:  ojt-gui                         (system bus, full screen - on the Pi)
 *       ojt-gui --session --windowed    (session bus, normal window - PC test)
 */
#include "MainWindow.h"
#include "PlayerClient.h"

#include <QApplication>
#include <QScreen>
#include <QStringList>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const QStringList args = app.arguments();
    const bool useSessionBus = args.contains("--session");
    const bool windowed      = args.contains("--windowed");

    PlayerClient client(useSessionBus);
    MainWindow window(&client);

    if (windowed) {
        window.resize(1024, 600);
        window.show();
    } else {
        // no window manager on the Pi -> set the size ourselves, then full screen
        window.setGeometry(app.primaryScreen()->geometry());
        window.showFullScreen();
    }
    window.activateWindow();   // give keyboard focus to the GUI (there is no window manager)
    window.start();   // after show(): the video window id now exists on the X server

    return app.exec();
}
