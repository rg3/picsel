// SPDX-License-Identifier: GPL-3.0-or-later
// Application: owns the QML engine and one window per open folder, and makes
// picsel single-instance (later launches forward their path over a socket).
#pragma once

#include <QJSEngine>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QtQmlIntegration>

namespace picsel {

struct DbProbe;

class Session;

class Application : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

public:
    // No default constructor on purpose: QML prefers one over create() and
    // would build a separate App (with its own engine) per QML engine.
    explicit Application(QObject *parent);
    ~Application() override;

    static Application *instance() { return s_instance; }
    static Application *create(QQmlEngine *, QJSEngine *engine);

    // Sends |path| to a running instance. Returns true if one took it.
    static bool forwardToRunningInstance(const QString &path);
    bool listen();

    // Opens a file's folder (highlighting the file) or a folder; empty shows a picker.
    void openPath(const QString &path, const QByteArray &activationToken = {});

    Q_INVOKABLE void openFolder(const QString &pathOrUrl);
    // Picker cancelled or error window closed.
    Q_INVOKABLE void dialogClosed();
    Q_INVOKABLE void setupAccepted(const QString &sourceDir, const QString &targetDir,
                                   bool defaultSelected, const QString &highlight);
    Q_INVOKABLE void setupCancelled();
    // Absolute paths only: a relative one would resolve against picsel's
    // working directory, which the user can't see.
    Q_INVOKABLE bool isWritableDir(const QString &pathOrUrl) const;
    Q_INVOKABLE bool isAbsolutePath(const QString &pathOrUrl) const;
    Q_INVOKABLE bool isSameDir(const QString &a, const QString &b) const;
    Q_INVOKABLE QString toLocalPath(const QString &pathOrUrl) const;
    // A file:// URL for FolderDialog.currentFolder; string joining breaks on '#', '?' or '%'.
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }

private:
    struct Open {
        Session *session;
        QPointer<QQuickWindow> window;
    };

    void onNewConnection();
    void showPicker();
    void showSetup(const QString &dir, const QString &highlight, const DbProbe &probe);
    void showMainWindow(Session *session, const QString &highlight);
    void showError(const QString &message);
    // Application palette from Theme.qml (see the .cpp).
    void applyThemePalette();
    QObject *createWindow(const char *type, const QVariantMap &props);
    void raise(QQuickWindow *w, const QByteArray &token);
    void quitIfIdle();

    static Application *s_instance;
    QQmlApplicationEngine m_engine;
    QLocalServer m_server;
    QVector<Open> m_open;
    int m_pendingWindows = 0; // setup/picker/error windows currently shown
    QHash<QString, QPointer<QQuickWindow>> m_setupWindows; // by source folder
};

} // namespace picsel
