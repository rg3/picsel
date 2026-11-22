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
#include <QPalette>
#include <QQuickStyle>
#include <QUrl>

namespace {
// One dark look regardless of the desktop's theme: pictures are judged on a
// neutral dark background.
QPalette darkPalette() {
    QPalette p;
    const QColor window(0x1c, 0x1c, 0x1f), base(0x26, 0x26, 0x2a), text(0xe8, 0xe8, 0xea);
    const QColor button(0x30, 0x30, 0x35), accent(0x4c, 0x8d, 0xff), dim(0x7a, 0x7a, 0x82);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Mid, QColor(0x45, 0x45, 0x4c));
    p.setColor(QPalette::Dark, QColor(0x12, 0x12, 0x14));
    p.setColor(QPalette::Light, QColor(0x50, 0x50, 0x58));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x40));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Button, QColor(0x26, 0x26, 0x2a));
    return p;
}
} // namespace

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
    QGuiApplication::setPalette(darkPalette());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pick which pictures of a folder to keep."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption checkOption(QStringLiteral("check"),
        QStringLiteral("Check that the Qt modules, plugins and codecs picsel needs are installed."));
    parser.addOption(checkOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("A picture (opens its folder) or a folder."),
                                 QStringLiteral("[path]"));
    parser.process(app);
    if (parser.isSet(checkOption))
        return picsel::runSystemCheck();

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
    if (!standalone && picsel::Application::forwardToRunningInstance(path))
        return 0;

    picsel::Application picselApp(nullptr);
    if (!standalone)
        picselApp.listen();
    picselApp.openPath(path);
    return app.exec();
}
