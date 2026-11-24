// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"

#include "Automation.h"
#include "Session.h"
#include "SystemCheck.h"
#include "core/SelectionDb.h"
#include "image/ImageProviders.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQmlComponent>
#include <QUrl>

#include <unistd.h>

namespace picsel {

Application *Application::s_instance = nullptr;

namespace {
QString serverName() {
    return QStringLiteral("picsel-%1").arg(::getuid());
}
} // namespace

Application::Application(QObject *parent) : QObject(parent) {
    s_instance = this;
    m_engine.addImageProvider(QStringLiteral("thumb"),
                              new CacheImageProvider(CacheImageProvider::Kind::Thumb));
    m_engine.addImageProvider(QStringLiteral("full"),
                              new CacheImageProvider(CacheImageProvider::Kind::Full));
    connect(&m_server, &QLocalServer::newConnection, this, &Application::onNewConnection);
    QGuiApplication::setQuitOnLastWindowClosed(false);
}

Application::~Application() {
    for (Open &o : m_open) {
        delete o.window;
        delete o.session;
    }
    s_instance = nullptr;
}

Application *Application::create(QQmlEngine *, QJSEngine *engine) {
    Q_ASSERT(s_instance);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    Q_UNUSED(engine);
    return s_instance;
}

bool Application::forwardToRunningInstance(const QString &path) {
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(500))
        return false;
    QJsonObject msg;
    msg[QStringLiteral("path")] = path;
    msg[QStringLiteral("token")] = QString::fromUtf8(qgetenv("XDG_ACTIVATION_TOKEN"));
    sock.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    if (!sock.waitForBytesWritten(1000))
        return false;
    // Wait for the acknowledgement so we know it was handled. Generous: the
    // running instance may be busy for a moment (e.g. opening a big folder).
    return sock.waitForReadyRead(10000) && sock.readAll().startsWith("ok");
}

bool Application::listen() {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server.listen(serverName()))
        return true;
    // Only remove the socket if nobody answers on it (left over from a crash);
    // never take it over from a live instance.
    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(300)) {
        qWarning("picsel: another instance owns the socket; running on our own");
        return false;
    }
    QLocalServer::removeServer(serverName());
    return m_server.listen(serverName());
}

void Application::onNewConnection() {
    while (QLocalSocket *sock = m_server.nextPendingConnection()) {
        connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
            if (!sock->canReadLine())
                return;
            const QJsonObject msg = QJsonDocument::fromJson(sock->readLine()).object();
            sock->write("ok\n");
            sock->flush();
            openPath(msg.value(QStringLiteral("path")).toString(),
                     msg.value(QStringLiteral("token")).toString().toUtf8());
        });
    }
}

QString Application::toLocalPath(const QString &pathOrUrl) const {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

bool Application::isAbsolutePath(const QString &pathOrUrl) const {
    return QDir::isAbsolutePath(toLocalPath(pathOrUrl));
}

bool Application::isWritableDir(const QString &pathOrUrl) const {
    const QFileInfo fi(toLocalPath(pathOrUrl));
    return fi.isAbsolute() && fi.isDir() && fi.isWritable();
}

bool Application::isSameDir(const QString &a, const QString &b) const {
    const QString ca = QFileInfo(toLocalPath(a)).canonicalFilePath();
    return !ca.isEmpty() && ca == QFileInfo(toLocalPath(b)).canonicalFilePath();
}

void Application::openPath(const QString &path, const QByteArray &token) {
    if (path.isEmpty()) {
        showPicker();
        return;
    }
    const QFileInfo fi(path);
    if (!fi.exists()) {
        showError(tr("%1 does not exist.").arg(path));
        return;
    }
    const QString dir = fi.isDir() ? fi.canonicalFilePath() : fi.canonicalPath();
    const QString highlight = fi.isDir() ? QString() : fi.fileName();

    for (Open &o : m_open) {
        if (o.session->sourceDir() == dir && o.window) {
            raise(o.window, token);
            if (!highlight.isEmpty())
                o.session->requestHighlight(highlight);
            return;
        }
    }

    if (auto it = m_setupWindows.constFind(dir); it != m_setupWindows.constEnd() && *it) {
        raise(*it, token); // already asking how to set this folder up
        return;
    }

    const DbProbe probe = SelectionDb::probe(dir);
    switch (probe.state) {
    case DbProbe::State::Unusable:
        showError(probe.error);
        return;
    case DbProbe::State::NotSetUp:
    case DbProbe::State::Moved:
        showSetup(dir, highlight, probe);
        return;
    case DbProbe::State::Ready:
        break;
    }
    auto *session = new Session(dir);
    if (!session->open()) {
        showError(session->errorString());
        delete session;
        return;
    }
    showMainWindow(session, highlight);
}

void Application::openFolder(const QString &pathOrUrl) {
    --m_pendingWindows;
    openPath(toLocalPath(pathOrUrl));
    quitIfIdle();
}

void Application::dialogClosed() {
    --m_pendingWindows;
    quitIfIdle();
}

QObject *Application::createWindow(const char *type, const QVariantMap &props) {
    QQmlComponent component(&m_engine);
    component.loadFromModule("Picsel", type);
    QObject *obj = component.createWithInitialProperties(props);
    if (!obj)
        qWarning().noquote() << component.errorString();
    else if (auto *win = qobject_cast<QQuickWindow *>(obj))
        Automation::startIfRequested(win);
    return obj;
}

void Application::showPicker() {
    QObject *w = createWindow("FolderPicker", {{QStringLiteral("portalDialog"), portalFolderDialogs()}});
    if (auto *win = qobject_cast<QQuickWindow *>(w)) {
        ++m_pendingWindows;
        connect(win, SIGNAL(finished()), win, SLOT(deleteLater())); // QML signal
    } else {
        delete w;
        quitIfIdle();
    }
}

void Application::showSetup(const QString &dir, const QString &highlight, const DbProbe &probe) {
    // Suggest the stored destination when it still works, else the parent
    // folder. A folder that was moved or copied gets the parent of where it
    // is now: its stored destination belongs to the old place.
    const auto usable = [&](const QString &d) {
        return !d.isEmpty() && QFileInfo(d).canonicalFilePath() != dir && isWritableDir(d);
    };
    QString target;
    if (probe.state != DbProbe::State::Moved && usable(probe.targetDir))
        target = probe.targetDir;
    else if (const QString parent = QFileInfo(dir).absolutePath(); usable(parent))
        target = parent;
    const QString notice = probe.state == DbProbe::State::Moved
        ? tr("This folder was set up as %1. It has been moved or copied since, so check where the "
             "selected pictures should go. The selection so far is kept.").arg(probe.setUpFor)
        : QString();
    QObject *w = createWindow("SetupWindow", {
        {QStringLiteral("sourceDir"), dir},
        {QStringLiteral("targetDir"), target},
        {QStringLiteral("highlight"), highlight},
        {QStringLiteral("notice"), notice},
        {QStringLiteral("startSelected"), probe.defaultSelected},
    });
    if (auto *win = qobject_cast<QQuickWindow *>(w)) {
        ++m_pendingWindows;
        m_setupWindows.insert(dir, win);
        connect(win, &QObject::destroyed, this, [this, dir] { m_setupWindows.remove(dir); });
    } else {
        delete w;
        quitIfIdle();
    }
}

void Application::setupAccepted(const QString &sourceDir, const QString &targetDir,
                                bool defaultSelected, const QString &highlight) {
    --m_pendingWindows;
    if (QPointer<QQuickWindow> w = m_setupWindows.take(sourceDir))
        w->deleteLater();
    auto *session = new Session(sourceDir);
    if (!session->create(toLocalPath(targetDir), defaultSelected)) {
        showError(session->errorString());
        delete session;
        quitIfIdle();
        return;
    }
    showMainWindow(session, highlight);
}

void Application::setupCancelled() {
    --m_pendingWindows;
    for (auto it = m_setupWindows.begin(); it != m_setupWindows.end();) {
        if (!*it || !(*it)->isVisible()) {
            if (*it)
                (*it)->deleteLater();
            it = m_setupWindows.erase(it);
        } else {
            ++it;
        }
    }
    quitIfIdle();
}

void Application::showMainWindow(Session *session, const QString &highlight) {
    const int row = highlight.isEmpty() ? 0 : std::max(0, session->rowOf(highlight));
    QObject *obj = createWindow("Main", {
        {QStringLiteral("session"), QVariant::fromValue(session)},
        {QStringLiteral("initialIndex"), row},
    });
    auto *window = qobject_cast<QQuickWindow *>(obj);
    if (!window) {
        delete obj;
        delete session;
        showError(tr("Could not create the main window."));
        return;
    }
    m_open.push_back({session, window});
    // Camera clock corrections are global: re-sort every open folder.
    connect(session, &Session::cameraOffsetsEdited, this, [this, session] {
        for (const Open &o : m_open)
            if (o.session != session)
                o.session->reloadCameraOffsets();
    });
    connect(window, &QQuickWindow::closing, this, [this, window] {
        for (int i = 0; i < m_open.size(); ++i) {
            if (m_open[i].window == window) {
                Session *s = m_open[i].session;
                m_open.removeAt(i);
                window->deleteLater();
                s->deleteLater();
                break;
            }
        }
        QMetaObject::invokeMethod(this, &Application::quitIfIdle, Qt::QueuedConnection);
    });
}

void Application::showError(const QString &message) {
    if (createWindow("ErrorWindow", {{QStringLiteral("message"), message}}))
        ++m_pendingWindows;
    else
        qCritical().noquote() << message;
}

void Application::raise(QQuickWindow *w, const QByteArray &token) {
    if (!token.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", token); // consumed by Qt's Wayland activation
    if (w->visibility() == QWindow::Minimized)
        w->showMaximized();
    w->raise();
    w->requestActivate();
}

void Application::quitIfIdle() {
    if (m_open.isEmpty() && m_pendingWindows <= 0)
        QCoreApplication::quit();
}

} // namespace picsel
