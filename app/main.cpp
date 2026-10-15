#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Microscope Capture"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
                                       QStringLiteral("Show the main window, then quit (CI launch check)."));
    parser.addOption(smokeTest);
    parser.process(app);

    msapp::MainWindow window;
    window.show();
    if (parser.isSet(smokeTest)) {
        // Queued, so it runs once the event loop has started: the window
        // has really been created and shown by then.
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
