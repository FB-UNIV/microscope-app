#include "build_version.hpp"
#include "catalog.hpp"
#include "main_window.hpp"
#include "qt_image.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QTimer>

#include <exception>
#include <filesystem>
#include <utility>

namespace {

// The workstation config, catalog and Vue root. Nothing here is fatal:
// whatever can't be read falls back to defaults — the Moticam A5 profile,
// no catalog (Vues go to Unsorted, ADR 0008) — and says so in `warnings`.
msapp::AppSetup loadSetup() {
    msapp::AppSetup setup;
    try {
        setup.configPath = msapp::defaultWorkstationConfigPath();
        setup.catalogPath = msapp::defaultCatalogPath();
    } catch (const std::exception& e) {
        setup.warnings << QObject::tr("Pas de dossier de configuration, réglages par défaut : %1")
                              .arg(QString::fromStdString(e.what()));
    }
    if (!setup.configPath.empty()) {
        const msapp::LoadedWorkstationConfig loaded = msapp::loadWorkstationConfig(setup.configPath);
        setup.config = loaded.config;
        if (!loaded.error.empty()) {
            setup.warnings << QObject::tr("Configuration du poste illisible, réglages par défaut : %1")
                                  .arg(QString::fromStdString(loaded.error));
        }
    }
    if (!setup.catalogPath.empty()) {
        const msapp::LoadedCatalog loaded = msapp::loadCatalog(setup.catalogPath);
        setup.catalog = loaded.catalog;
        if (!loaded.error.empty()) {
            setup.warnings << QObject::tr("Catalogue de TP illisible, Vues rangées dans « Non classé » : %1")
                                  .arg(QString::fromStdString(loaded.error));
        }
    }
    setup.camera = setup.config.camera;
    if (!setup.config.vueRoot.empty()) {
        setup.vueRoot = setup.config.vueRoot;
    } else if (!setup.configPath.empty()) {
        setup.vueRoot = msapp::defaultVueRoot();
    } else {
        setup.vueRoot = msapp::toPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) / "vues";
    }
    return setup;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Microscope Capture"));
    QApplication::setApplicationVersion(QString::fromUtf8(msapp::kBuildVersion));

    QCommandLineParser parser;
    parser.addHelpOption();
    // Like --help, on Windows it shows a message box unless the output is
    // redirected (microscope_app.exe --version | Out-String).
    parser.addVersionOption();
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

    msapp::AppSetup setup = loadSetup();
    msapp::CameraConfig& camera = setup.camera;
    if (parser.isSet(device)) {
        bool ok = false;
        const int index = parser.value(device).toInt(&ok);
        if (ok && index >= 0) {
            camera.deviceIndex = index;
        } else {
            setup.warnings << QObject::tr("--device attend un numéro de caméra (0, 1, ...) : ignoré.");
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
            setup.warnings << QObject::tr("--size attend LARGEURxHAUTEUR, par exemple 1920x1080 : ignoré.");
        }
    }
    if (parser.isSet(replay)) {
        camera.backend = "file";
        camera.frames.clear();
        for (const QString& image : parser.values(replay)) {
            camera.frames.push_back(std::filesystem::u8path(image.toStdString()));
        }
    }

    msapp::MainWindow window(std::move(setup));
    window.show();
    if (parser.isSet(smokeTest)) {
        // Queued, so it runs once the event loop has started: the window
        // has really been created and shown by then.
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
