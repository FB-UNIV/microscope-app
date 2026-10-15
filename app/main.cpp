#include "main_window.hpp"
#include "workstation_config.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

#include <exception>
#include <filesystem>

namespace {

// The workstation's camera profile, or the Moticam A5 defaults when it has
// none yet or its config can't be read — said in `warnings`, never fatal.
msapp::CameraConfig loadCameraProfile(QStringList& warnings) {
    try {
        const msapp::LoadedWorkstationConfig loaded = msapp::loadWorkstationConfig(msapp::defaultWorkstationConfigPath());
        if (!loaded.error.empty()) {
            warnings << QObject::tr("Configuration du poste illisible, profil Moticam A5 par défaut utilisé : %1")
                            .arg(QString::fromStdString(loaded.error));
        }
        return loaded.config.camera;
    } catch (const std::exception& e) {
        warnings << QObject::tr("Configuration du poste introuvable, profil Moticam A5 par défaut utilisé : %1")
                        .arg(QString::fromStdString(e.what()));
        return msapp::moticamA5Profile();
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Microscope Capture"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
                                       QStringLiteral("Show the main window, then quit (CI launch check)."));
    // Overrides for this run only, e.g. a laptop whose built-in webcam takes
    // index 0; the workstation config is not changed.
    const QCommandLineOption device(QStringLiteral("device"), QStringLiteral("Camera index to open."),
                                    QStringLiteral("index"));
    const QCommandLineOption api(QStringLiteral("api"), QStringLiteral("Capture API: auto, dshow, msmf or v4l2."),
                                 QStringLiteral("name"));
    const QCommandLineOption size(QStringLiteral("size"),
                                  QStringLiteral("Frame size to request, WIDTHxHEIGHT; 0x0 keeps the device default."),
                                  QStringLiteral("size"));
    const QCommandLineOption replay(QStringLiteral("replay"),
                                    QStringLiteral("Preview this image file instead of a camera; repeat for several."),
                                    QStringLiteral("image"));
    parser.addOptions({smokeTest, device, api, size, replay});
    parser.process(app);

    QStringList warnings;
    msapp::CameraConfig camera = loadCameraProfile(warnings);
    if (parser.isSet(device)) {
        bool ok = false;
        const int index = parser.value(device).toInt(&ok);
        if (ok && index >= 0) {
            camera.deviceIndex = index;
        } else {
            warnings << QObject::tr("--device attend un numéro de caméra (0, 1, ...) : ignoré.");
        }
    }
    if (parser.isSet(api)) {
        camera.captureApi = parser.value(api).toStdString();  // an unknown name is reported by the preview
    }
    if (parser.isSet(size)) {
        const QStringList parts = parser.value(size).split(QLatin1Char('x'));
        bool okWidth = false;
        bool okHeight = false;
        const int width = parts.size() == 2 ? parts[0].toInt(&okWidth) : 0;
        const int height = parts.size() == 2 ? parts[1].toInt(&okHeight) : 0;
        if (okWidth && okHeight && width >= 0 && height >= 0) {
            camera.width = width;
            camera.height = height;
        } else {
            warnings << QObject::tr("--size attend LARGEURxHAUTEUR, par exemple 1920x1080 : ignoré.");
        }
    }
    if (parser.isSet(replay)) {
        camera.backend = "file";
        camera.frames.clear();
        for (const QString& image : parser.values(replay)) {
            camera.frames.push_back(std::filesystem::u8path(image.toStdString()));
        }
    }

    msapp::MainWindow window(camera, warnings);
    window.show();
    if (parser.isSet(smokeTest)) {
        // Queued, so it runs once the event loop has started: the window
        // has really been created and shown by then.
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
