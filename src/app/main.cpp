// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include "SystemCheck.h"
#include "core/Metadata.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QUrl>

int main(int argc, char *argv[]) {
    // Native folder dialogs: KDE's need a QtWidgets app, but Qt's portal
    // theme asks the desktop (KDE, GNOME...) to show its own, and keeps the
    // desktop's theme for everything else. A theme the user chose wins.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("picsel"));
    QGuiApplication::setOrganizationName(QStringLiteral("picsel"));
    QGuiApplication::setDesktopFileName(QStringLiteral(PICSEL_APP_ID));
    QGuiApplication::setApplicationVersion(QStringLiteral(PICSEL_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/" PICSEL_APP_ID ".svg")));
    picsel::initMetadataLibrary();
    // The ffmpeg media backend logs every file's streams at info level. Qt
    // also warns when the portal already knows our app id (harmless; picsel
    // uses no other desktop services that log there).
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg*.info=false\n"
                                                    "qt.qpa.services.warning=false"));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pick which pictures of a folder to keep."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption checkOption(QStringLiteral("check"),
        QStringLiteral("Check that the Qt modules, plugins and codecs picsel needs are installed."));
    parser.addOption(checkOption);
    const QCommandLineOption resumeOption(QStringLiteral("resume"),
        QStringLiteral("Start at the picture highlighted when the folder was last closed: \"always\" "
                       "(even when a picture is given; the default), \"folder\" (only when a folder "
                       "is given) or \"never\". Overrides the setting in the side panel."),
        QStringLiteral("when"));
    parser.addOption(resumeOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("A picture (opens its folder) or a folder."),
                                 QStringLiteral("[path]"));
    parser.process(app);
    if (parser.isSet(checkOption))
        return picsel::runSystemCheck();
    std::optional<picsel::settings::Resume> resume;
    if (parser.isSet(resumeOption)) {
        resume = picsel::settings::parseResume(parser.value(resumeOption));
        if (!resume) {
            qCritical().noquote() << "picsel: --resume must be always, folder or never";
            return 1; // as for Qt's own option errors
        }
    }

    QString path;
    if (!parser.positionalArguments().isEmpty()) {
        path = parser.positionalArguments().constFirst();
        const QUrl url(path);
        if (url.isLocalFile())
            path = url.toLocalFile();
        path = QFileInfo(path).absoluteFilePath(); // relative to our cwd, before forwarding
    }

    // Scripted/test runs stay independent: never hand the path to (or take
    // requests from) the user's running instance.
    const bool standalone = qEnvironmentVariableIsSet("PICSEL_AUTOMATION")
        || qEnvironmentVariableIsSet("PICSEL_STANDALONE");
    if (!standalone && picsel::Application::forwardToRunningInstance(path, resume))
        return 0;

    picsel::Application picselApp(nullptr);
    if (!standalone)
        picselApp.listen();
    picselApp.openPath(path, {}, resume);
    return app.exec();
}
