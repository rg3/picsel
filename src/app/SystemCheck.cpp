// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemCheck.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QIcon>
#include <QMediaFormat>
#include <QMimeDatabase>
#include <QPixmap>
#include <QQmlComponent>
#include <QQmlEngine>

#include <cstdio>

namespace picsel {

namespace {

void report(const char *what, bool ok, const char *ifMissing) {
    std::printf("  %-34s %s\n", what, ok ? "ok" : ifMissing);
}

uint fileChooserPortalVersion() {
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.portal.FileChooser") << QStringLiteral("version");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return 0;
    return reply.arguments().constFirst().value<QDBusVariant>().variant().toUInt();
}

} // namespace

bool portalFolderDialogs() {
    return qgetenv("QT_QPA_PLATFORMTHEME") == "xdgdesktopportal" && fileChooserPortalVersion() >= 3;
}

int runSystemCheck() {
    bool fatal = false;
    std::printf("picsel %s, Qt %s\n", PICSEL_VERSION, qVersion());

    // Keep in sync with the imports under src/app/qml.
    std::printf("QML modules (required):\n");
    QQmlEngine engine;
    for (const char *module : {"QtQuick", "QtQuick.Controls", "QtQuick.Layouts", "QtQuick.Dialogs",
                               "QtQuick.Effects", "QtMultimedia"}) {
        QQmlComponent c(&engine);
        c.setData(QByteArray("import QtQml\nimport ") + module + "\nQtObject {}\n", QUrl());
        const bool ok = c.status() == QQmlComponent::Ready;
        fatal |= !ok;
        report(module, ok, "MISSING");
    }

    // An empty codec list also means no multimedia backend plugin at all.
    std::printf("Video (optional: without it, videos show a placeholder):\n");
    QMediaFormat any; // the supported*() getters are not const
    const auto files = any.supportedFileFormats(QMediaFormat::Decode);
    const auto video = any.supportedVideoCodecs(QMediaFormat::Decode);
    const auto audio = any.supportedAudioCodecs(QMediaFormat::Decode);
    report("multimedia backend", !files.isEmpty(), "MISSING (no Qt Multimedia backend plugin)");
    report("MP4 container", files.contains(QMediaFormat::MPEG4), "missing");
    report("QuickTime (.mov) container", files.contains(QMediaFormat::QuickTime), "missing");
    report("H.264 decoder", video.contains(QMediaFormat::VideoCodec::H264), "missing");
    report("H.265/HEVC decoder (newer phones)", video.contains(QMediaFormat::VideoCodec::H265), "missing");
    report("AAC decoder (video sound)", audio.contains(QMediaFormat::AudioCodec::AAC), "missing");

    std::printf("Other (optional):\n");
    const QIcon icon(QStringLiteral(":/icons/" PICSEL_APP_ID ".svg"));
    report("SVG icons (Qt SVG)", !icon.pixmap(64).isNull(), "missing (no window icon)");
    report("desktop folder dialogs (portal)", portalFolderDialogs(),
           "missing (Qt's own dialog is used; needs xdg-desktop-portal)");
    // Only installed systems have it: the type comes from picsel's own MIME file.
    report("picsel.db file type", QMimeDatabase().mimeTypeForName(QStringLiteral("application/x-picsel-db")).isValid(),
           "missing (picsel.db files do not open with picsel)");

    std::printf(fatal ? "picsel cannot start: install the missing QML modules.\n" : "picsel can run.\n");
    return fatal ? 1 : 0;
}

} // namespace picsel
